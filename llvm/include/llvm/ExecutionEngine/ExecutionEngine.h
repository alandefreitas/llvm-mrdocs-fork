//===- ExecutionEngine.h - Abstract Execution Engine Interface --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the abstract interface that implements execution support
// for LLVM.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_EXECUTIONENGINE_H
#define LLVM_EXECUTIONENGINE_EXECUTIONENGINE_H

#include "llvm-c/ExecutionEngine.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Module.h"
#include "llvm/Object/Binary.h"
#include "llvm/Support/CBindingWrapping.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Mutex.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace llvm {

class Constant;
class Function;
struct GenericValue;
class GlobalValue;
class GlobalVariable;
class JITEventListener;
class MCJITMemoryManager;
class ObjectCache;
class RTDyldMemoryManager;
class Triple;
class Type;

namespace object {

class Archive;
class ObjectFile;

} // end namespace object

/// Helper class for helping synchronize access to the global address map
/// table.  Access to this class should be serialized under a mutex.
class ExecutionEngineState {
public:
  /// Map from mangled global symbol names to their materialized addresses.
  using GlobalAddressMapTy = StringMap<uint64_t>;

private:
  /// GlobalAddressMap - A mapping between LLVM global symbol names values and
  /// their actualized version...
  GlobalAddressMapTy GlobalAddressMap;

  /// GlobalAddressReverseMap - This is the reverse mapping of GlobalAddressMap,
  /// used to convert raw addresses into the LLVM global value that is emitted
  /// at the address.  This map is not computed unless getGlobalValueAtAddress
  /// is called at some point.
  std::map<uint64_t, std::string> GlobalAddressReverseMap;

public:
  /// Return the mutable map from global symbol names to addresses.
  /// \returns Mutable map from mangled global symbol names to addresses.
  GlobalAddressMapTy &getGlobalAddressMap() {
    return GlobalAddressMap;
  }

  /// Return the mutable reverse map from addresses to global symbol names.
  /// \returns Mutable map from addresses to mangled global symbol names.
  std::map<uint64_t, std::string> &getGlobalAddressReverseMap() {
    return GlobalAddressReverseMap;
  }

  /// Erase an entry from the mapping table.
  ///
  /// \param Name Global symbol name whose mapping should be removed.
  /// \returns The address that \p Name was mapped to.
  LLVM_ABI uint64_t RemoveMapping(StringRef Name);
};

/// Callback that materializes an unknown function by name for the JIT.
using FunctionCreator = std::function<void *(const std::string &)>;

/// Abstract interface for implementation execution of LLVM modules,
/// designed to support both interpreter and just-in-time (JIT) compiler
/// implementations.
class LLVM_ABI ExecutionEngine {
  /// The state object holding the global address mapping, which must be
  /// accessed synchronously.
  //
  // FIXME: There is no particular need the entire map needs to be
  // synchronized.  Wouldn't a reader-writer design be better here?
  ExecutionEngineState EEState;

  /// The target data for the platform for which execution is being performed.
  ///
  /// Note: the DataLayout is LLVMContext specific because it has an
  /// internal cache based on type pointers. It makes unsafe to reuse the
  /// ExecutionEngine across context, we don't enforce this rule but undefined
  /// behavior can occurs if the user tries to do it.
  const DataLayout DL;

  /// Whether lazy JIT compilation is enabled.
  bool CompilingLazily;

  /// Whether JIT compilation of external global variables is allowed.
  bool GVCompilationDisabled;

  /// Whether the JIT should perform lookups of external symbols (e.g.,
  /// using dlsym).
  bool SymbolSearchingDisabled;

  /// Whether the JIT should verify IR modules during compilation.
  bool VerifyModules;

  friend class EngineBuilder;  // To allow access to JITCtor and InterpCtor.

protected:
  /// The list of Modules that we are JIT'ing from.  We use a SmallVector to
  /// optimize for the case where there is only one module.
  SmallVector<std::unique_ptr<Module>, 1> Modules;

  /// Allocate memory for a global variable.
  /// \param GV Global variable for which to allocate storage.
  /// \returns Pointer to the allocated storage for \p GV.
  virtual char *getMemoryForGV(const GlobalVariable *GV);

  /// Factory used by EngineBuilder to construct an MCJIT execution engine.
  static ExecutionEngine *(*MCJITCtor)(
      std::unique_ptr<Module> M, std::string *ErrorStr,
      std::shared_ptr<MCJITMemoryManager> MM,
      std::shared_ptr<LegacyJITSymbolResolver> SR,
      std::unique_ptr<TargetMachine> TM);

  /// Factory used by EngineBuilder to construct an interpreter engine.
  static ExecutionEngine *(*InterpCtor)(std::unique_ptr<Module> M,
                                        std::string *ErrorStr);

  /// LazyFunctionCreator - If an unknown function is needed, this function
  /// pointer is invoked to create it.  If this returns null, the JIT will
  /// abort.
  FunctionCreator LazyFunctionCreator;

  /// Get the mangled name of a global value.
  /// \param GV Global value whose mangled name is requested.
  /// \returns The mangled name of \p GV.
  std::string getMangledName(const GlobalValue *GV);

  /// Most recent error message recorded by the execution engine.
  std::string ErrMsg;

public:
  /// lock - This lock protects the ExecutionEngine and MCJIT classes. It must
  /// be held while changing the internal state of any of those classes.
  sys::Mutex lock;

  //===--------------------------------------------------------------------===//
  //  ExecutionEngine Startup
  //===--------------------------------------------------------------------===//

  /// Destroy the execution engine and release owned resources.
  virtual ~ExecutionEngine();

  /// Add a Module to the list of modules that we can JIT from.
  /// \param M Module to take ownership of and make available for JIT.
  virtual void addModule(std::unique_ptr<Module> M) {
    Modules.push_back(std::move(M));
  }

  /// Add an ObjectFile to the execution engine.
  ///
  /// This method is only supported by MCJIT.  MCJIT will immediately load the
  /// object into memory and adds its symbols to the list used to resolve
  /// external symbols while preparing other objects for execution.
  ///
  /// Objects added using this function will not be made executable until
  /// needed by another object.
  ///
  /// MCJIT will take ownership of the ObjectFile.
  /// \param O Object file to load into the execution engine.
  virtual void addObjectFile(std::unique_ptr<object::ObjectFile> O);
  /// Add an owning ObjectFile binary to the execution engine.
  /// \param O Owning binary wrapping the object file to load.
  virtual void addObjectFile(object::OwningBinary<object::ObjectFile> O);

  /// Add an Archive to the execution engine.
  ///
  /// This method is only supported by MCJIT.  MCJIT will use the archive to
  /// resolve external symbols in objects it is loading.  If a symbol is found
  /// in the Archive the contained object file will be extracted (in memory)
  /// and loaded for possible execution.
  /// \param A Archive whose members may be loaded to resolve symbols.
  virtual void addArchive(object::OwningBinary<object::Archive> A);

  //===--------------------------------------------------------------------===//

  /// Return the data layout used by this execution engine.
  /// \returns The DataLayout associated with this engine.
  const DataLayout &getDataLayout() const { return DL; }

  /// Remove a module from the engine without freeing it.
  ///
  /// Removes a Module from the list of modules, but does not free the module's
  /// memory. Returns true if M is found, in which case the caller assumes
  /// responsibility for deleting the module.
  ///
  /// \param M Module to remove from the execution engine.
  /// \returns True if \p M was found and removed.
  //
  // FIXME: This stealth ownership transfer is horrible. This will probably be
  //        fixed by deleting ExecutionEngine.
  virtual bool removeModule(Module *M);

  /// Search active modules for the function that defines \p FnName.
  ///
  /// This is a very slow operation and shouldn't be used for general code.
  /// \param FnName Name of the function definition to find.
  /// \returns The matching Function, or null if not found.
  virtual Function *FindFunctionNamed(StringRef FnName);

  /// Search active modules for the global variable that defines \p Name.
  ///
  /// This is a very slow operation and shouldn't be used for general code.
  /// \param Name Name of the global variable definition to find.
  /// \param AllowInternal If true, also consider internal linkage globals.
  /// \returns The matching GlobalVariable, or null if not found.
  virtual GlobalVariable *FindGlobalVariableNamed(StringRef Name, bool AllowInternal = false);

  /// Execute the specified function with the specified arguments.
  ///
  /// For MCJIT execution engines, clients are encouraged to use the
  /// "GetFunctionAddress" method (rather than runFunction) and cast the
  /// returned uint64_t to the desired function pointer type. However, for
  /// backwards compatibility MCJIT's implementation can execute 'main-like'
  /// function (i.e. those returning void or int, and taking either no
  /// arguments or (int, char*[])).
  /// \param F Function to execute.
  /// \param ArgValues Argument values to pass to \p F.
  /// \returns Result of executing \p F as a GenericValue.
  virtual GenericValue runFunction(Function *F,
                                   ArrayRef<GenericValue> ArgValues) = 0;

  /// Return the address of a named function via dlsym.
  ///
  /// As such it is only useful for resolving library symbols, not code
  /// generated symbols.
  ///
  /// If AbortOnFailure is false and no function with the given name is
  /// found, this function silently returns a null pointer. Otherwise,
  /// it prints a message to stderr and aborts.
  ///
  /// This function is deprecated for the MCJIT execution engine.
  /// \param Name Name of the library function to look up.
  /// \param AbortOnFailure If true, abort when the symbol cannot be found.
  /// \returns Host pointer to the named function, or null if not found and
  ///          AbortOnFailure is false.
  virtual void *getPointerToNamedFunction(StringRef Name,
                                          bool AbortOnFailure = true) = 0;

  /// Map a JIT section's local address to its target address.
  ///
  /// Map the address of a JIT section as returned from the memory manager
  /// to the address in the target process as the running code will see it.
  /// This is the address which will be used for relocation resolution.
  /// \param LocalAddress Address of the section in the host process.
  /// \param TargetAddress Address of the section in the target address space.
  virtual void mapSectionAddress(const void *LocalAddress,
                                 uint64_t TargetAddress) {
    llvm_unreachable("Re-mapping of section addresses not supported with this "
                     "EE!");
  }

  /// Run code generation for the specified module and load it into memory.
  ///
  /// When this function has completed, all code and data for the specified
  /// module, and any module on which this module depends, will be generated
  /// and loaded into memory, but relocations will not yet have been applied
  /// and all memory will be readable and writable but not executable.
  ///
  /// This function is primarily useful when generating code for an external
  /// target, allowing the client an opportunity to remap section addresses
  /// before relocations are applied.  Clients that intend to execute code
  /// locally can use the getFunctionAddress call, which will generate code
  /// and apply final preparations all in one step.
  ///
  /// This method has no effect for the interpreter.
  /// \param M Module for which to generate and load code.
  virtual void generateCodeForModule(Module *M) {}

  /// finalizeObject - ensure the module is fully processed and is usable.
  ///
  /// It is the user-level function for completing the process of making the
  /// object usable for execution.  It should be called after sections within an
  /// object have been relocated using mapSectionAddress.  When this method is
  /// called the MCJIT execution engine will reapply relocations for a loaded
  /// object.  This method has no effect for the interpreter.
  ///
  /// Returns true on success, false on failure. Error messages can be retrieved
  /// by calling getError();
  virtual void finalizeObject() {}

  /// Returns true if an error has been recorded.
  /// \returns True if the engine has a non-empty error message.
  bool hasError() const { return !ErrMsg.empty(); }

  /// Clear the error message.
  void clearErrorMessage() { ErrMsg.clear(); }

  /// Returns the most recent error message.
  /// \returns The last recorded error string, or empty if none.
  const std::string &getErrorMessage() const { return ErrMsg; }

  /// runStaticConstructorsDestructors - This method is used to execute all of
  /// the static constructors or destructors for a program.
  ///
  /// \param isDtors - Run the destructors instead of constructors.
  virtual void runStaticConstructorsDestructors(bool isDtors);

  /// Execute static constructors or destructors for a particular module.
  ///
  /// \param module Module whose global ctors or dtors should be run.
  /// \param isDtors - Run the destructors instead of constructors.
  void runStaticConstructorsDestructors(Module &module, bool isDtors);


  /// Run \p Fn as a program entry point with argc, argv, and envp.
  ///
  /// This is a helper which wraps runFunction to handle the common task of
  /// starting up main with the specified argc, argv, and envp parameters.
  /// \param Fn Function to run as main.
  /// \param argv Argument strings passed as argv to \p Fn.
  /// \param envp Null-terminated environment pointer array, or null.
  /// \returns The integer exit status returned by \p Fn.
  int runFunctionAsMain(Function *Fn, const std::vector<std::string> &argv,
                        const char * const * envp);


  /// Tell the engine that a named global lives at a given address.
  ///
  /// This is used internally as functions are JIT'd and as global variables
  /// are laid out in memory.  It can and should also be used by clients of the
  /// EE that want to have an LLVM global overlay existing data in memory.
  /// Values to be mapped should be named, and have external or weak linkage.
  /// Mappings are automatically removed when their GlobalValue is destroyed.
  /// \param GV Global value to associate with \p Addr.
  /// \param Addr Host address where \p GV is located.
  void addGlobalMapping(const GlobalValue *GV, void *Addr);
  /// Tell the engine that a symbol name maps to a given address.
  /// \param Name Mangled symbol name to map.
  /// \param Addr Address associated with \p Name.
  void addGlobalMapping(StringRef Name, uint64_t Addr);

  /// clearAllGlobalMappings - Clear all global mappings and start over again,
  /// for use in dynamic compilation scenarios to move globals.
  void clearAllGlobalMappings();

  /// Clear all global mappings that came from a particular module.
  ///
  /// Used when the module has been removed from the JIT.
  /// \param M Module whose global mappings should be cleared.
  void clearGlobalMappingsFromModule(Module *M);

  /// Replace an existing mapping for a global with a new address.
  ///
  /// This updates both maps as required.  If "Addr" is null, the entry for the
  /// global is removed from the mappings.  This returns the old value of the
  /// pointer, or null if it was not in the map.
  /// \param GV Global value whose mapping should be updated.
  /// \param Addr New address for \p GV, or null to remove the mapping.
  /// \returns The previous address mapped to \p GV, or zero if none existed.
  uint64_t updateGlobalMapping(const GlobalValue *GV, void *Addr);
  /// Replace an existing mapping for a symbol name with a new address.
  /// \param Name Mangled symbol name whose mapping should be updated.
  /// \param Addr New address for \p Name, or zero to remove the mapping.
  /// \returns The previous address mapped to \p Name, or zero if none existed.
  uint64_t updateGlobalMapping(StringRef Name, uint64_t Addr);

  /// Return the address of the specified global symbol if available.
  /// \param S Mangled symbol name to look up.
  /// \returns Mapped address of \p S, or zero if not available.
  uint64_t getAddressToGlobalIfAvailable(StringRef S);

  /// Return a pointer to a global if it has already been codegen'd.
  ///
  /// Returns null if the global is not yet available.
  /// \param S Mangled symbol name to look up.
  /// \returns Host pointer to the global if available, otherwise null.
  void *getPointerToGlobalIfAvailable(StringRef S);
  /// Return a pointer to a global value if it has already been codegen'd.
  /// \param GV Global value to look up.
  /// \returns Host pointer to \p GV if available, otherwise null.
  void *getPointerToGlobalIfAvailable(const GlobalValue *GV);

  /// Return the address of the specified global value.
  ///
  /// This may involve code generation if it's a function.
  ///
  /// This function is deprecated for the MCJIT execution engine.  Use
  /// getGlobalValueAddress instead.
  /// \param GV Global value whose address is requested.
  /// \returns Host pointer to the address of \p GV.
  void *getPointerToGlobal(const GlobalValue *GV);

  /// Return a pointer to the machine code for function \p F.
  ///
  /// The different EE's represent function bodies in different ways.  They
  /// should each implement this to say what a function pointer should look
  /// like.  When F is destroyed, the ExecutionEngine will remove its global
  /// mapping and free any machine code.  Be sure no threads are running inside
  /// F when that happens.
  ///
  /// This function is deprecated for the MCJIT execution engine.  Use
  /// getFunctionAddress instead.
  /// \param F Function whose code pointer is requested.
  /// \returns Host pointer to the machine code for \p F.
  virtual void *getPointerToFunction(Function *F) = 0;

  /// Return a pointer to \p F, compiling or using a lazy stub if needed.
  ///
  /// If the specified function has been code-gen'd, return a pointer to the
  /// function.  If not, compile it, or use a stub to implement lazy
  /// compilation if available.  See getPointerToFunction for the requirements
  /// on destroying F.
  ///
  /// This function is deprecated for the MCJIT execution engine.  Use
  /// getFunctionAddress instead.
  /// \param F Function whose code pointer or stub is requested.
  /// \returns Host pointer to the function code or a lazy stub for \p F.
  virtual void *getPointerToFunctionOrStub(Function *F) {
    // Default implementation, just codegen the function.
    return getPointerToFunction(F);
  }

  /// Return the address of the specified global value.
  ///
  /// This may involve code generation.
  ///
  /// This function should not be called with the interpreter engine.
  /// \param Name Name of the global value whose address is requested.
  /// \returns Target address of the named global value, or zero if unavailable.
  virtual uint64_t getGlobalValueAddress(const std::string &Name) {
    // Default implementation for the interpreter.  MCJIT will override this.
    // JIT and interpreter clients should use getPointerToGlobal instead.
    return 0;
  }

  /// Return the address of the specified function.
  ///
  /// This may involve code generation.
  /// \param Name Name of the function whose address is requested.
  /// \returns Target address of the named function, or zero if unavailable.
  virtual uint64_t getFunctionAddress(const std::string &Name) {
    // Default implementation for the interpreter.  MCJIT will override this.
    // Interpreter clients should use getPointerToFunction instead.
    return 0;
  }

  /// Return the LLVM global value object that starts at the specified address.
  /// \param Addr Host address at which a global value begins.
  /// \returns The GlobalValue at \p Addr, or null if none is mapped there.
  const GlobalValue *getGlobalValueAtAddress(void *Addr);

  /// Store a GenericValue of type \p Ty into memory at \p Ptr.
  ///
  /// Ptr is the address of the memory at which to store Val, cast to
  /// GenericValue *.  It is not a pointer to a GenericValue containing the
  /// address at which to store Val.
  /// \param Val Value to store.
  /// \param Ptr Destination memory, cast as a GenericValue pointer.
  /// \param Ty LLVM type of the value being stored.
  void StoreValueToMemory(const GenericValue &Val, GenericValue *Ptr,
                          Type *Ty);

  /// Initialize memory at \p Addr from constant initializer \p Init.
  /// \param Init Constant used to initialize the memory.
  /// \param Addr Destination memory to initialize.
  void InitializeMemory(const Constant *Init, void *Addr);

  /// Return the address of a global variable, emitting it if needed.
  ///
  /// This is used by the Emitter.
  ///
  /// This function is deprecated for the MCJIT execution engine.  Use
  /// getGlobalValueAddress instead.
  /// \param GV Global variable whose address is requested.
  /// \returns Host pointer to the global variable's storage.
  virtual void *getOrEmitGlobalVariable(const GlobalVariable *GV) {
    return getPointerToGlobal((const GlobalValue *)GV);
  }

  /// Register a listener for JIT compilation events.
  ///
  /// See JITEventListener.h for more details.  Does not take ownership of the
  /// argument.  The argument may be NULL, in which case these functions do
  /// nothing.
  /// \param L Listener to register; may be null.
  virtual void RegisterJITEventListener(JITEventListener *L) {}
  /// Unregister a previously registered JIT event listener.
  /// \param L Listener to unregister; may be null.
  virtual void UnregisterJITEventListener(JITEventListener *L) {}

  /// Sets the pre-compiled object cache.  The ownership of the ObjectCache is
  /// not changed.  Supported by MCJIT but not the interpreter.
  /// \param Cache Object cache to use; ownership is not transferred.
  virtual void setObjectCache(ObjectCache *Cache) {
    llvm_unreachable("No support for an object cache");
  }

  /// Control whether MCJIT passes all object sections to the memory manager.
  ///
  /// By default, only sections that are "required for execution" are passed to
  /// the RTDyldMemoryManager, and other sections are discarded. Passing 'true'
  /// to this method will cause RuntimeDyld to pass all sections to its
  /// RTDyldMemoryManager regardless of whether they are "required to execute"
  /// in the usual sense.
  ///
  /// Rationale: Some MCJIT clients want to be able to inspect metadata
  /// sections (e.g. Dwarf, Stack-maps) to enable functionality or analyze
  /// performance. Passing these sections to the memory manager allows the
  /// client to make policy about the relevant sections, rather than having
  /// MCJIT do it.
  /// \param ProcessAllSections If true, deliver every section to the manager.
  virtual void setProcessAllSections(bool ProcessAllSections) {
    llvm_unreachable("No support for ProcessAllSections option");
  }

  /// Return the target machine (if available).
  /// \returns The associated TargetMachine, or null if none is available.
  virtual TargetMachine *getTargetMachine() { return nullptr; }

  /// Enable or disable lazy JIT compilation.
  ///
  /// When lazy compilation is off (the default), the JIT will eagerly compile
  /// every function reachable from the argument to getPointerToFunction.  If
  /// lazy compilation is turned on, the JIT will only compile the one function
  /// and emit stubs to compile the rest when they're first called.  If lazy
  /// compilation is turned off again while some lazy stubs are still around,
  /// and one of those stubs is called, the program will abort.
  ///
  /// In order to safely compile lazily in a threaded program, the user must
  /// ensure that 1) only one thread at a time can call any particular lazy
  /// stub, and 2) any thread modifying LLVM IR must hold the JIT's lock
  /// (ExecutionEngine::lock) or otherwise ensure that no other thread calls a
  /// lazy stub.  See http://llvm.org/PR5184 for details.
  /// \param Disabled If true, turn lazy compilation off (eager mode).
  void DisableLazyCompilation(bool Disabled = true) {
    CompilingLazily = !Disabled;
  }
  /// Return true if lazy JIT compilation is currently enabled.
  /// \returns True if lazy compilation is enabled.
  bool isCompilingLazily() const {
    return CompilingLazily;
  }

  /// Abort if asked to materialize a non-internal GlobalVariable.
  ///
  /// If called, the JIT will abort if it's asked to allocate space and
  /// populate a GlobalVariable that is not internal to the module.
  /// \param Disabled If true, disallow compiling external globals.
  void DisableGVCompilation(bool Disabled = true) {
    GVCompilationDisabled = Disabled;
  }
  /// Return true if compiling external global variables is disabled.
  /// \returns True if external global variable compilation is disabled.
  bool isGVCompilationDisabled() const {
    return GVCompilationDisabled;
  }

  /// Disable looking up unknown symbols with dlsym.
  ///
  /// A client can still use InstallLazyFunctionCreator to resolve symbols in a
  /// custom way.
  /// \param Disabled If true, do not search for symbols via dlsym.
  void DisableSymbolSearching(bool Disabled = true) {
    SymbolSearchingDisabled = Disabled;
  }
  /// Return true if dlsym-based symbol searching is disabled.
  /// \returns True if symbol searching is disabled.
  bool isSymbolSearchingDisabled() const {
    return SymbolSearchingDisabled;
  }

  /// Enable or disable IR module verification during compilation.
  ///
  /// Note: Module verification is enabled by default in Debug builds, and
  /// disabled by default in Release. Use this method to override the default.
  /// \param Verify If true, verify modules before compiling them.
  void setVerifyModules(bool Verify) {
    VerifyModules = Verify;
  }
  /// Return true if IR module verification is enabled.
  /// \returns True if module verification is enabled.
  bool getVerifyModules() const {
    return VerifyModules;
  }

  /// Install a callback used to materialize unknown functions.
  ///
  /// If an unknown function is needed, the specified function pointer is
  /// invoked to create it.  If it returns null, the JIT will abort.
  /// \param C Callback invoked with the unresolved function name.
  void InstallLazyFunctionCreator(FunctionCreator C) {
    LazyFunctionCreator = std::move(C);
  }

protected:
  /// Construct an execution engine with the given data layout.
  /// \param DL Data layout for the target being executed.
  ExecutionEngine(DataLayout DL) : DL(std::move(DL)) {}
  /// Construct an execution engine with a data layout and initial module.
  /// \param DL Data layout for the target being executed.
  /// \param M Module to take ownership of as the initial module.
  explicit ExecutionEngine(DataLayout DL, std::unique_ptr<Module> M);
  /// Construct an execution engine that takes ownership of \p M.
  /// \param M Module to take ownership of as the initial module.
  explicit ExecutionEngine(std::unique_ptr<Module> M);

  /// Emit all global variables from owned modules into memory.
  void emitGlobals();

  /// Emit a single global variable into memory.
  /// \param GV Global variable to emit.
  void emitGlobalVariable(const GlobalVariable *GV);

  /// Evaluate an LLVM constant as a GenericValue.
  /// \param C Constant to evaluate.
  /// \returns The constant interpreted as a GenericValue.
  GenericValue getConstantValue(const Constant *C);
  /// Load a value of type \p Ty from \p Ptr into \p Result.
  /// \param Result Destination GenericValue to fill.
  /// \param Ptr Source memory, cast as a GenericValue pointer.
  /// \param Ty LLVM type of the value being loaded.
  void LoadValueFromMemory(GenericValue &Result, GenericValue *Ptr,
                           Type *Ty);

private:
  void Init(std::unique_ptr<Module> M);
};

/// Identifiers for which kind of execution engine to build.
namespace EngineKind {

  // These are actually bitmasks that get or-ed together.
  /// Kind of execution engine requested from EngineBuilder.
  enum Kind {
    /// Just-in-time compiler execution engine.
    JIT         = 0x1,
    /// Interpreter execution engine.
    Interpreter = 0x2
  };
  const static Kind Either = (Kind)(JIT | Interpreter);

} // end namespace EngineKind

/// Builder class for ExecutionEngines. Use this by stack-allocating a builder,
/// chaining the various set* methods, and terminating it with a .create()
/// call.
class EngineBuilder {
private:
  std::unique_ptr<Module> M;
  EngineKind::Kind WhichEngine;
  std::string *ErrorStr;
  CodeGenOptLevel OptLevel;
  std::shared_ptr<MCJITMemoryManager> MemMgr;
  std::shared_ptr<LegacyJITSymbolResolver> Resolver;
  TargetOptions Options;
  std::optional<Reloc::Model> RelocModel;
  std::optional<CodeModel::Model> CMModel;
  std::string MArch;
  std::string MCPU;
  SmallVector<std::string, 4> MAttrs;
  bool VerifyModules;
  bool EmulatedTLS = true;

public:
  /// Default constructor for EngineBuilder.
  LLVM_ABI EngineBuilder();

  /// Construct an EngineBuilder that takes ownership of module \p M.
  /// \param M Module used as the initial module for the engine.
  LLVM_ABI EngineBuilder(std::unique_ptr<Module> M);

  /// Destroy the builder and release any unconsumed owned state.
  LLVM_ABI ~EngineBuilder();

  /// Select whether to build an interpreter, JIT, or either.
  ///
  /// This option defaults to EngineKind::Either.
  /// \param w Engine kind bitmask requesting JIT and/or interpreter.
  /// \returns Reference to this builder for chaining.
  EngineBuilder &setEngineKind(EngineKind::Kind w) {
    WhichEngine = w;
    return *this;
  }

  /// Set the MCJIT memory manager used for allocation policy.
  ///
  /// This allows clients to customize their memory allocation policies for the
  /// MCJIT. This is only appropriate for the MCJIT; setting this and
  /// configuring the builder to create anything other than MCJIT will cause a
  /// runtime error. If create() is called and is successful, the created
  /// engine takes ownership of the memory manager. This option defaults to
  /// NULL.
  /// \param mcjmm Memory manager to install for MCJIT.
  /// \returns Reference to this builder for chaining.
  LLVM_ABI EngineBuilder &
  setMCJITMemoryManager(std::unique_ptr<RTDyldMemoryManager> mcjmm);

  /// Set the MCJIT memory manager without taking an RTDyld wrapper.
  /// \param MM Memory manager shared with the constructed engine.
  /// \returns Reference to this builder for chaining.
  LLVM_ABI EngineBuilder &
  setMemoryManager(std::unique_ptr<MCJITMemoryManager> MM);

  /// Set the symbol resolver used when linking generated code.
  /// \param SR Symbol resolver shared with the constructed engine.
  /// \returns Reference to this builder for chaining.
  LLVM_ABI EngineBuilder &
  setSymbolResolver(std::unique_ptr<LegacyJITSymbolResolver> SR);

  /// Set the error string to write to on error.
  ///
  /// This option defaults to NULL.
  /// \param e String that receives a message if engine creation fails.
  /// \returns Reference to this builder for chaining.
  EngineBuilder &setErrorStr(std::string *e) {
    ErrorStr = e;
    return *this;
  }

  /// Set the optimization level for the JIT.
  ///
  /// This option defaults to CodeGenOptLevel::Default.
  /// \param l Code generation optimization level.
  /// \returns Reference to this builder for chaining.
  EngineBuilder &setOptLevel(CodeGenOptLevel l) {
    OptLevel = l;
    return *this;
  }

  /// Set the target options used by the ExecutionEngine target.
  ///
  /// Defaults to TargetOptions().
  /// \param Opts Target options to apply when creating the TargetMachine.
  /// \returns Reference to this builder for chaining.
  EngineBuilder &setTargetOptions(const TargetOptions &Opts) {
    Options = Opts;
    return *this;
  }

  /// Set the relocation model used by the ExecutionEngine target.
  ///
  /// Defaults to target specific default "Reloc::Default".
  /// \param RM Relocation model for the TargetMachine.
  /// \returns Reference to this builder for chaining.
  EngineBuilder &setRelocationModel(Reloc::Model RM) {
    RelocModel = RM;
    return *this;
  }

  /// Set the code model used by the ExecutionEngine target.
  ///
  /// Defaults to target specific default "CodeModel::JITDefault".
  /// \param M Code model for the TargetMachine.
  /// \returns Reference to this builder for chaining.
  EngineBuilder &setCodeModel(CodeModel::Model M) {
    CMModel = M;
    return *this;
  }

  /// Override the architecture set by the Module's triple.
  /// \param march Architecture name used instead of the module triple's arch.
  /// \returns Reference to this builder for chaining.
  EngineBuilder &setMArch(StringRef march) {
    MArch.assign(march.begin(), march.end());
    return *this;
  }

  /// Target a specific cpu type.
  /// \param mcpu CPU name passed to the TargetMachine.
  /// \returns Reference to this builder for chaining.
  EngineBuilder &setMCPU(StringRef mcpu) {
    MCPU.assign(mcpu.begin(), mcpu.end());
    return *this;
  }

  /// Set whether the JIT implementation should verify IR modules.
  /// \param Verify If true, verify modules during compilation.
  /// \returns Reference to this builder for chaining.
  EngineBuilder &setVerifyModules(bool Verify) {
    VerifyModules = Verify;
    return *this;
  }

  /// Set cpu-specific attributes.
  /// \param mattrs Sequence of target feature/attribute strings.
  /// \returns Reference to this builder for chaining.
  template<typename StringSequence>
  EngineBuilder &setMAttrs(const StringSequence &mattrs) {
    MAttrs.clear();
    MAttrs.append(mattrs.begin(), mattrs.end());
    return *this;
  }

  /// Enable or disable emulated TLS for the constructed engine.
  /// \param EmulatedTLS If true, use emulated thread-local storage.
  void setEmulatedTLS(bool EmulatedTLS) {
    this->EmulatedTLS = EmulatedTLS;
  }

  /// Select a TargetMachine for the host using builder options.
  /// \returns The selected TargetMachine, or null on failure.
  LLVM_ABI TargetMachine *selectTarget();

  /// Pick a target for the given triple, arch, CPU, and attributes.
  ///
  /// Add any CPU features specified via -mcpu or -mattr.
  /// \param TargetTriple Triple describing the target platform.
  /// \param MArch Architecture override, or empty to use the triple.
  /// \param MCPU CPU name to target.
  /// \param MAttrs Target feature/attribute strings.
  /// \returns The selected TargetMachine, or null on failure.
  LLVM_ABI TargetMachine *
  selectTarget(const Triple &TargetTriple, StringRef MArch, StringRef MCPU,
               const SmallVectorImpl<std::string> &MAttrs);

  /// Create an ExecutionEngine using a TargetMachine from selectTarget().
  /// \returns The created ExecutionEngine, or null on failure.
  ExecutionEngine *create() {
    return create(selectTarget());
  }

  /// Create an ExecutionEngine for the given TargetMachine.
  /// \param TM Target machine to use; may be taken ownership of.
  /// \returns The created ExecutionEngine, or null on failure.
  LLVM_ABI ExecutionEngine *create(TargetMachine *TM);
};

// Create wrappers for C Binding types (see CBindingWrapping.h).
/// Convert an opaque \c LLVMExecutionEngineRef to an \c ExecutionEngine pointer.
/// \param P Opaque C API execution-engine reference to unwrap.
/// \returns The ExecutionEngine pointer corresponding to \p P.
inline ExecutionEngine *unwrap(LLVMExecutionEngineRef P) {
  return reinterpret_cast<ExecutionEngine *>(P);
}

/// Convert an \c ExecutionEngine pointer to an opaque \c LLVMExecutionEngineRef.
/// \param P Execution engine to wrap for the C API.
/// \returns An opaque C API execution-engine reference for \p P.
inline LLVMExecutionEngineRef wrap(const ExecutionEngine *P) {
  return reinterpret_cast<LLVMExecutionEngineRef>(
      const_cast<ExecutionEngine *>(P));
}

} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_EXECUTIONENGINE_H
