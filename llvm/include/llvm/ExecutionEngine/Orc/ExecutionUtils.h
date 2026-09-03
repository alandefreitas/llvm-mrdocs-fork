//===- ExecutionUtils.h - Utilities for executing code in Orc ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Contains utilities for executing code in Orc.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_EXECUTIONUTILS_H
#define LLVM_EXECUTIONENGINE_ORC_EXECUTIONUTILS_H

#include "llvm/ADT/iterator_range.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/Mangling.h"
#include "llvm/ExecutionEngine/Orc/ObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/Orc/Shared/OrcError.h"
#include "llvm/ExecutionEngine/RuntimeDyld.h"
#include "llvm/Object/Archive.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DynamicLibrary.h"
#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace llvm {

class ConstantArray;
class GlobalVariable;
class Function;
class Module;
class Value;

namespace object {
class MachOUniversalBinary;
}

namespace orc {

class ObjectLayer;

/// This iterator provides a convenient way to iterate over the elements
///        of an llvm.global_ctors/llvm.global_dtors instance.
///
///   The easiest way to get hold of instances of this class is to use the
/// getConstructors/getDestructors functions.
class CtorDtorIterator {
public:
  /// Accessor for an element of the global_ctors/global_dtors array.
  ///
  ///   This class provides a read-only view of the element with any casts on
  /// the function stripped away.
  struct Element {
    /// Construct an element view from priority, function, and associated data.
    /// @param Priority Initialization or finalization priority.
    /// @param Func Constructor or destructor function, or nullptr.
    /// @param Data Associated data argument, or nullptr.
    Element(unsigned Priority, Function *Func, Value *Data)
      : Priority(Priority), Func(Func), Data(Data) {}

    /// Initialization or finalization priority for this entry.
    unsigned Priority;
    /// Constructor or destructor function for this entry.
    Function *Func;
    /// Associated data argument for this entry.
    Value *Data;
  };

  /// Construct an iterator instance. If End is true then this iterator
  ///        acts as the end of the range, otherwise it is the beginning.
  /// @param GV Global variable holding the ctor/dtor array.
  /// @param End Whether to position this iterator at the end of the range.
  LLVM_ABI CtorDtorIterator(const GlobalVariable *GV, bool End);

  /// Test iterators for equality.
  /// @param Other Iterator to compare against.
  /// @return True if the iterators are equal.
  LLVM_ABI bool operator==(const CtorDtorIterator &Other) const;

  /// Test iterators for inequality.
  /// @param Other Iterator to compare against.
  /// @return True if the iterators are not equal.
  LLVM_ABI bool operator!=(const CtorDtorIterator &Other) const;

  /// Pre-increment iterator.
  /// @return Reference to this iterator after advancing.
  LLVM_ABI CtorDtorIterator &operator++();

  /// Post-increment iterator.
  /// @param Unused Ignored postfix-increment discriminator.
  /// @return Copy of the iterator before advancing.
  LLVM_ABI CtorDtorIterator operator++(int Unused);

  /// Dereference iterator. The resulting value provides a read-only view
  ///        of this element of the global_ctors/global_dtors list.
  /// @return Read-only view of the current ctor/dtor entry.
  LLVM_ABI Element operator*() const;

private:
  const ConstantArray *InitList;
  unsigned I;
};

/// Create an iterator range over the entries of the llvm.global_ctors
///        array.
/// @param M Module whose llvm.global_ctors array should be iterated.
/// @return Iterator range over the llvm.global_ctors entries.
LLVM_ABI iterator_range<CtorDtorIterator> getConstructors(const Module &M);

/// Create an iterator range over the entries of the llvm.global_dtors
///        array.
/// @param M Module whose llvm.global_dtors array should be iterated.
/// @return Iterator range over the llvm.global_dtors entries.
LLVM_ABI iterator_range<CtorDtorIterator> getDestructors(const Module &M);

/// This iterator provides a convenient way to iterate over GlobalValues that
/// have initialization effects.
class StaticInitGVIterator {
public:
  /// Construct an end iterator.
  StaticInitGVIterator() = default;

  /// Construct an iterator over static-init globals in \p M.
  /// @param M Module whose global values should be scanned.
  StaticInitGVIterator(Module &M)
      : I(M.global_values().begin()), E(M.global_values().end()),
        ObjFmt(M.getTargetTriple().getObjectFormat()) {
    if (I != E) {
      if (!isStaticInitGlobal(*I))
        moveToNextStaticInitGlobal();
    } else
      I = E = Module::global_value_iterator();
  }

  /// Test iterators for equality.
  /// @param O Iterator to compare against.
  /// @return True if the iterators are equal.
  bool operator==(const StaticInitGVIterator &O) const { return I == O.I; }
  /// Test iterators for inequality.
  /// @param O Iterator to compare against.
  /// @return True if the iterators are not equal.
  bool operator!=(const StaticInitGVIterator &O) const { return I != O.I; }

  /// Advance to the next static-init global.
  /// @return Reference to this iterator after advancing.
  StaticInitGVIterator &operator++() {
    assert(I != E && "Increment past end of range");
    moveToNextStaticInitGlobal();
    return *this;
  }

  /// Return the current static-init global value.
  /// @return Reference to the current static-init global value.
  GlobalValue &operator*() { return *I; }

private:
  LLVM_ABI bool isStaticInitGlobal(GlobalValue &GV);
  void moveToNextStaticInitGlobal() {
    ++I;
    while (I != E && !isStaticInitGlobal(*I))
      ++I;
    if (I == E)
      I = E = Module::global_value_iterator();
  }

  Module::global_value_iterator I, E;
  Triple::ObjectFormatType ObjFmt;
};

/// Create an iterator range over the GlobalValues that contribute to static
/// initialization.
/// @param M Module whose static-init globals should be iterated.
/// @return Iterator range over GlobalValues that contribute to static init.
inline iterator_range<StaticInitGVIterator> getStaticInitGVs(Module &M) {
  return make_range(StaticInitGVIterator(M), StaticInitGVIterator());
}

/// Utility for collecting and running constructors or destructors from a
/// JITDylib.
class CtorDtorRunner {
public:
  /// Construct a runner that records and runs ctors/dtors in \p JD.
  /// @param JD JITDylib that will define the ctor/dtor symbols.
  CtorDtorRunner(JITDylib &JD) : JD(JD) {}
  /// Add constructor or destructor entries to be run later.
  /// @param CtorDtors Range of ctor/dtor array entries to record.
  LLVM_ABI void add(iterator_range<CtorDtorIterator> CtorDtors);
  /// Look up and run all recorded constructors or destructors by priority.
  /// @return Success, or an error if lookup or execution fails.
  LLVM_ABI Error run();

private:
  using CtorDtorList = std::vector<SymbolStringPtr>;
  using CtorDtorPriorityMap = std::map<unsigned, CtorDtorList>;

  JITDylib &JD;
  CtorDtorPriorityMap CtorDtorsByPriority;
};

/// Support class for static dtor execution. For hosted (in-process) JITs
///        only!
///
///   If a __cxa_atexit function isn't found C++ programs that use static
/// destructors will fail to link. However, we don't want to use the host
/// process's __cxa_atexit, because it will schedule JIT'd destructors to run
/// after the JIT has been torn down, which is no good. This class makes it easy
/// to override __cxa_atexit (and the related __dso_handle).
///
///   To use, clients should manually call searchOverrides from their symbol
/// resolver. This should generally be done after attempting symbol resolution
/// inside the JIT, but before searching the host process's symbol table. When
/// the client determines that destructors should be run (generally at JIT
/// teardown or after a return from main), the runDestructors method should be
/// called.
class LocalCXXRuntimeOverridesBase {
public:
  /// Run any destructors recorded by the overriden __cxa_atexit function
  /// (CXAAtExitOverride).
  LLVM_ABI void runDestructors();

protected:
  /// Function pointer type for a C++ destructor invoked via __cxa_atexit.
  using DestructorPtr = void (*)(void *);
  /// Pair of a destructor function and its argument.
  using CXXDestructorDataPair = std::pair<DestructorPtr, void *>;
  /// List of recorded destructor/argument pairs.
  using CXXDestructorDataPairList = std::vector<CXXDestructorDataPair>;
  /// Recorded destructors scheduled through the overridden __cxa_atexit.
  CXXDestructorDataPairList DSOHandleOverride;
  /// Override for __cxa_atexit that records destructors for later execution.
  /// @param Destructor Destructor function to invoke later.
  /// @param Arg Argument to pass to \p Destructor.
  /// @param DSOHandle DSO handle identifying the registration scope.
  /// @return Zero on success.
  LLVM_ABI static int CXAAtExitOverride(DestructorPtr Destructor, void *Arg,
                                        void *DSOHandle);
};

/// LocalCXXRuntimeOverridesBase with helpers to enable overrides in a JITDylib.
class LocalCXXRuntimeOverrides : public LocalCXXRuntimeOverridesBase {
public:
  /// Enable __dso_handle and __cxa_atexit overrides in \p JD.
  /// @param JD JITDylib in which to define the override symbols.
  /// @param Mangler Mangle-and-intern helper for symbol names.
  /// @return Success, or an error if the override symbols cannot be defined.
  LLVM_ABI Error enable(JITDylib &JD, MangleAndInterner &Mangler);
};

/// An interface for Itanium __cxa_atexit interposer implementations.
class ItaniumCXAAtExitSupport {
public:
  /// One destructor callback registered via __cxa_atexit.
  struct AtExitRecord {
    /// Destructor function to invoke.
    void (*F)(void *);
    /// Argument passed to \p F.
    void *Ctx;
  };

  /// Register a destructor to run when \p DSOHandle is torn down.
  /// @param F Destructor function to invoke.
  /// @param Ctx Argument passed to \p F.
  /// @param DSOHandle DSO handle identifying the registration scope.
  LLVM_ABI void registerAtExit(void (*F)(void *), void *Ctx, void *DSOHandle);
  /// Run and clear all destructors registered for \p DSOHandle.
  /// @param DSOHandle DSO handle whose registered exits should run.
  LLVM_ABI void runAtExits(void *DSOHandle);

private:
  std::mutex AtExitsMutex;
  DenseMap<void *, std::vector<AtExitRecord>> AtExitRecords;
};

/// A utility class to expose symbols found via dlsym to the JIT.
///
/// If an instance of this class is attached to a JITDylib as a fallback
/// definition generator, then any symbol found in the given DynamicLibrary that
/// passes the 'Allow' predicate will be added to the JITDylib.
class LLVM_ABI DynamicLibrarySearchGenerator : public DefinitionGenerator {
public:
  /// Predicate that selects which symbols may be imported from the library.
  using SymbolPredicate = std::function<bool(const SymbolStringPtr &)>;
  /// Callback used to define absolute symbols in a JITDylib.
  using AddAbsoluteSymbolsFn = unique_function<Error(JITDylib &, SymbolMap)>;

  /// Create a DynamicLibrarySearchGenerator that searches for symbols in the
  /// given sys::DynamicLibrary.
  ///
  /// If the Allow predicate is given then only symbols matching the predicate
  /// will be searched for. If the predicate is not given then all symbols will
  /// be searched for.
  ///
  /// If \p AddAbsoluteSymbols is provided, it is used to add the symbols to the
  /// \c JITDylib; otherwise it uses JD.define(absoluteSymbols(...)).
  /// @param Dylib Dynamic library to search for symbol definitions.
  /// @param GlobalPrefix Target-specific global symbol prefix character.
  /// @param Allow Optional predicate restricting which symbols may be found.
  /// @param AddAbsoluteSymbols Optional callback used to define found symbols.
  DynamicLibrarySearchGenerator(
      sys::DynamicLibrary Dylib, char GlobalPrefix,
      SymbolPredicate Allow = SymbolPredicate(),
      AddAbsoluteSymbolsFn AddAbsoluteSymbols = nullptr);

  /// Load a library and return a generator that searches it.
  ///
  /// Permanently loads the library at the given path and, on success, returns
  /// a DynamicLibrarySearchGenerator that will search it for symbol definitions
  /// in the library. On failure returns the reason the library failed to load.
  /// @param FileName Path of the dynamic library to load, or nullptr for the
  ///        current process.
  /// @param GlobalPrefix Target-specific global symbol prefix character.
  /// @param Allow Optional predicate restricting which symbols may be found.
  /// @param AddAbsoluteSymbols Optional callback used to define found symbols.
  /// @return A generator for the loaded library, or an error if loading fails.
  static Expected<std::unique_ptr<DynamicLibrarySearchGenerator>>
  Load(const char *FileName, char GlobalPrefix,
       SymbolPredicate Allow = SymbolPredicate(),
       AddAbsoluteSymbolsFn AddAbsoluteSymbols = nullptr);

  /// Creates a DynamicLibrarySearchGenerator that searches for symbols in
  /// the current process.
  /// @param GlobalPrefix Target-specific global symbol prefix character.
  /// @param Allow Optional predicate restricting which symbols may be found.
  /// @param AddAbsoluteSymbols Optional callback used to define found symbols.
  /// @return A generator that searches the current process, or an error on
  ///         failure.
  static Expected<std::unique_ptr<DynamicLibrarySearchGenerator>>
  GetForCurrentProcess(char GlobalPrefix,
                       SymbolPredicate Allow = SymbolPredicate(),
                       AddAbsoluteSymbolsFn AddAbsoluteSymbols = nullptr) {
    return Load(nullptr, GlobalPrefix, std::move(Allow),
                std::move(AddAbsoluteSymbols));
  }

  /// Search the loaded library for unresolved symbols and define matches.
  /// @param LS Lookup state that may be suspended while definitions are sought.
  /// @param K Kind of lookup being performed.
  /// @param JD Target JITDylib being searched.
  /// @param JDLookupFlags Whether the search should match hidden symbols.
  /// @param Symbols Unresolved symbols and their associated lookup flags.
  /// @return Success, or an error if matching symbols cannot be defined.
  Error tryToGenerate(LookupState &LS, LookupKind K, JITDylib &JD,
                      JITDylibLookupFlags JDLookupFlags,
                      const SymbolLookupSet &Symbols) override;

private:
  sys::DynamicLibrary Dylib;
  SymbolPredicate Allow;
  AddAbsoluteSymbolsFn AddAbsoluteSymbols;
  char GlobalPrefix;
};

/// A utility class to expose symbols from a static library.
///
/// If an instance of this class is attached to a JITDylib as a fallback
/// definition generator, then any symbol found in the archive will result in
/// the containing object being added to the JITDylib.
class LLVM_ABI StaticLibraryDefinitionGenerator : public DefinitionGenerator {
public:
  /// Interface builder function for objects loaded from this archive.
  using GetObjectFileInterface =
      unique_function<Expected<MaterializationUnit::Interface>(
          ExecutionSession &ES, MemoryBufferRef ObjBuffer)>;

  /// Callback for visiting archive members at construction time. Can be used
  /// to pre-load members.
  ///
  /// Callbacks are provided with a reference to the underlying archive, a
  /// MemoryBufferRef covering the bytes for the given member, and the index of
  /// the given member.
  ///
  /// Implementations should return true if the given member file should be
  /// loadable via the generator, false if it should not, and an Error if the
  /// member is malformed in a way that renders the archive itself invalid.
  ///
  /// Note: Linkers typically ignore invalid files within archives, so it's
  ///       expected that implementations will usually return `false` (i.e.
  ///       not-loadable) for malformed buffers, and will only return an
  ///       Error in exceptional circumstances.
  using VisitMembersFunction = unique_function<Expected<bool>(
      object::Archive &, MemoryBufferRef, size_t)>;

  /// A VisitMembersFunction that unconditionally loads all object files from
  /// the archive.
  /// Archive members that are not valid object files will be skipped.
  /// @param L Object layer used to add loaded members to \p JD.
  /// @param JD JITDylib that will receive pre-loaded archive members.
  /// @return Callback that loads every valid object-file member into \p JD.
  static VisitMembersFunction loadAllObjectFileMembers(ObjectLayer &L,
                                                       JITDylib &JD);

  /// Create a memory buffer covering one archive member.
  /// @param A Archive that owns the member.
  /// @param BufRef Buffer covering the member bytes.
  /// @param Index Index of the member within the archive.
  /// @return Memory buffer owning a copy of the member bytes.
  static std::unique_ptr<MemoryBuffer>
  createMemberBuffer(object::Archive &A, MemoryBufferRef BufRef, size_t Index);

  /// Try to create a StaticLibraryDefinitionGenerator from the given path.
  ///
  /// This call will succeed if the file at the given path is a static library
  /// or a MachO universal binary containing a static library that is compatible
  /// with the ExecutionSession's triple. Otherwise it will return an error.
  /// @param L Object layer used to materialize archive members.
  /// @param FileName Path of the static library or universal binary to load.
  /// @param VisitMembers Optional callback invoked for each archive member.
  /// @param GetObjFileInterface Optional interface builder for loaded objects.
  /// @return A generator for the archive, or an error if the path is invalid.
  static Expected<std::unique_ptr<StaticLibraryDefinitionGenerator>>
  Load(ObjectLayer &L, const char *FileName,
       VisitMembersFunction VisitMembers = VisitMembersFunction(),
       GetObjectFileInterface GetObjFileInterface = GetObjectFileInterface());

  /// Try to create a StaticLibrarySearchGenerator from the given memory buffer
  /// and Archive object.
  /// @param L Object layer used to materialize archive members.
  /// @param ArchiveBuffer Memory buffer owning the archive bytes.
  /// @param Archive Parsed archive object corresponding to \p ArchiveBuffer.
  /// @param VisitMembers Optional callback invoked for each archive member.
  /// @param GetObjFileInterface Optional interface builder for loaded objects.
  /// @return A generator for the archive, or an error if creation fails.
  static Expected<std::unique_ptr<StaticLibraryDefinitionGenerator>>
  Create(ObjectLayer &L, std::unique_ptr<MemoryBuffer> ArchiveBuffer,
         std::unique_ptr<object::Archive> Archive,
         VisitMembersFunction VisitMembers = VisitMembersFunction(),
         GetObjectFileInterface GetObjFileInterface = GetObjectFileInterface());

  /// Try to create a StaticLibrarySearchGenerator from a memory buffer.
  ///
  /// Try to create a StaticLibrarySearchGenerator from the given memory buffer.
  /// This call will succeed if the buffer contains a valid archive, otherwise
  /// it will return an error.
  ///
  /// This call will succeed if the buffer contains a valid static library or a
  /// MachO universal binary containing a static library that is compatible
  /// with the ExecutionSession's triple. Otherwise it will return an error.
  /// @param L Object layer used to materialize archive members.
  /// @param ArchiveBuffer Memory buffer containing the archive or universal
  ///        binary.
  /// @param VisitMembers Optional callback invoked for each archive member.
  /// @param GetObjFileInterface Optional interface builder for loaded objects.
  /// @return A generator for the archive, or an error if the buffer is invalid.
  static Expected<std::unique_ptr<StaticLibraryDefinitionGenerator>>
  Create(ObjectLayer &L, std::unique_ptr<MemoryBuffer> ArchiveBuffer,
         VisitMembersFunction VisitMembers = VisitMembersFunction(),
         GetObjectFileInterface GetObjFileInterface = GetObjectFileInterface());

  /// Search the archive for unresolved symbols and materialize defining
  /// members.
  /// @param LS Lookup state that may be suspended while definitions are sought.
  /// @param K Kind of lookup being performed.
  /// @param JD Target JITDylib being searched.
  /// @param JDLookupFlags Whether the search should match hidden symbols.
  /// @param Symbols Unresolved symbols and their associated lookup flags.
  /// @return Success, or an error if defining members cannot be materialized.
  Error tryToGenerate(LookupState &LS, LookupKind K, JITDylib &JD,
                      JITDylibLookupFlags JDLookupFlags,
                      const SymbolLookupSet &Symbols) override;

private:
  StaticLibraryDefinitionGenerator(
      ObjectLayer &L, std::unique_ptr<MemoryBuffer> ArchiveBuffer,
      std::unique_ptr<object::Archive> Archive,
      GetObjectFileInterface GetObjFileInterface,
      DenseMap<SymbolStringPtr, size_t> SymbolToMemberIndexMap);

  ObjectLayer &L;
  GetObjectFileInterface GetObjFileInterface;
  std::unique_ptr<MemoryBuffer> ArchiveBuffer;
  std::unique_ptr<object::Archive> Archive;
  DenseMap<SymbolStringPtr, size_t> SymbolToMemberIndexMap;
};

/// A utility class to create COFF dllimport GOT symbols (__imp_*) and PLT
/// stubs.
///
/// If an instance of this class is attached to a JITDylib as a fallback
/// definition generator, PLT stubs and dllimport __imp_ symbols will be
/// generated for external symbols found outside the given jitdylib. Currently
/// only supports x86_64 architecture.
class LLVM_ABI DLLImportDefinitionGenerator : public DefinitionGenerator {
public:
  /// Creates a DLLImportDefinitionGenerator instance.
  /// @param ES Execution session for the generator.
  /// @param L Object linking layer used to emit synthesized stubs.
  /// @return Newly created DLLImportDefinitionGenerator.
  static std::unique_ptr<DLLImportDefinitionGenerator>
  Create(ExecutionSession &ES, ObjectLinkingLayer &L);

  /// Synthesize dllimport GOT entries and PLT stubs for unresolved symbols.
  /// @param LS Lookup state that may be suspended while definitions are sought.
  /// @param K Kind of lookup being performed.
  /// @param JD Target JITDylib being searched.
  /// @param JDLookupFlags Whether the search should match hidden symbols.
  /// @param Symbols Unresolved symbols and their associated lookup flags.
  /// @return Success, or an error if stubs cannot be synthesized.
  Error tryToGenerate(LookupState &LS, LookupKind K, JITDylib &JD,
                      JITDylibLookupFlags JDLookupFlags,
                      const SymbolLookupSet &Symbols) override;

private:
  DLLImportDefinitionGenerator(ExecutionSession &ES, ObjectLinkingLayer &L)
      : ES(ES), L(L) {}

  static Expected<unsigned> getTargetPointerSize(const Triple &TT);
  static Expected<llvm::endianness> getEndianness(const Triple &TT);
  Expected<std::unique_ptr<jitlink::LinkGraph>>
  createStubsGraph(const SymbolMap &Resolved);

  static StringRef getImpPrefix() { return "__imp_"; }

  static StringRef getSectionName() { return "$__DLLIMPORT_STUBS"; }

  ExecutionSession &ES;
  ObjectLinkingLayer &L;
};

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_EXECUTIONUTILS_H
