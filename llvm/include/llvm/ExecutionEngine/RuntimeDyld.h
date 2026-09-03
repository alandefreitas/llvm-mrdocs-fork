//===- RuntimeDyld.h - Run-time dynamic linker for MC-JIT -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Interface for the runtime dynamic linker facilities of the MC-JIT.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_RUNTIMEDYLD_H
#define LLVM_EXECUTIONENGINE_RUNTIMEDYLD_H

#include "llvm/ADT/FunctionExtras.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <system_error>

namespace llvm {

namespace object {

template <typename T> class OwningBinary;

} // end namespace object

/// Base class for errors originating in RuntimeDyld, e.g. missing relocation
/// support.
class LLVM_ABI RuntimeDyldError : public ErrorInfo<RuntimeDyldError> {
public:
  /// ErrorInfo identifier for RuntimeDyldError.
  static char ID;

  /// Construct a RuntimeDyld error with the given message.
  /// \param ErrMsg Human-readable description of the failure.
  RuntimeDyldError(std::string ErrMsg) : ErrMsg(std::move(ErrMsg)) {}

  /// Write the error message to \p OS.
  /// \param OS Stream that receives the logged message.
  void log(raw_ostream &OS) const override;
  /// Return the stored error message string.
  /// \returns The human-readable error message.
  const std::string &getErrorMessage() const { return ErrMsg; }
  /// Convert this error to a std::error_code.
  /// \returns A generic error_code representing this RuntimeDyld failure.
  std::error_code convertToErrorCode() const override;

private:
  std::string ErrMsg;
};

/// Internal implementation class behind RuntimeDyld.
class RuntimeDyldImpl;

/// Runtime dynamic linker used by MCJIT and related ORC layers.
class RuntimeDyld {
public:
  /// Change the address associated with a section when resolving relocations.
  ///
  /// Any relocations already associated with the symbol will be re-resolved.
  /// \param SectionID Identifier of the section whose address is updated.
  /// \param Addr New address to associate with the section.
  LLVM_ABI void reassignSectionAddress(unsigned SectionID, uint64_t Addr);

  /// Callback invoked when RuntimeDyld emits a stub for a symbol.
  using NotifyStubEmittedFunction = std::function<void(
      StringRef FileName, StringRef SectionName, StringRef SymbolName,
      unsigned SectionID, uint32_t StubOffset)>;

  /// Information about the loaded object.
  class LLVM_ABI LoadedObjectInfo : public llvm::LoadedObjectInfo {
    friend class RuntimeDyldImpl;

  public:
    /// Map from object sections to RuntimeDyld section IDs.
    using ObjSectionToIDMap = std::map<object::SectionRef, unsigned>;

    /// Construct loaded-object info for a RuntimeDyldImpl instance.
    /// \param RTDyld RuntimeDyld implementation that owns the loaded sections.
    /// \param ObjSecToIDMap Mapping from object sections to section IDs.
    LoadedObjectInfo(RuntimeDyldImpl &RTDyld, ObjSectionToIDMap ObjSecToIDMap)
        : RTDyld(RTDyld), ObjSecToIDMap(std::move(ObjSecToIDMap)) {}

    /// Return an owning copy of \p Obj suitable for debug info use.
    /// \param Obj Object file to duplicate for debugging.
    /// \returns An owning binary that can be used for debug inspection.
    virtual object::OwningBinary<object::ObjectFile>
    getObjectForDebug(const object::ObjectFile &Obj) const = 0;

    /// Return the load address of section \p Sec, or 0 if unknown.
    /// \param Sec Object section whose load address is requested.
    /// \returns The section load address, or 0 if unknown.
    uint64_t
    getSectionLoadAddress(const object::SectionRef &Sec) const override;

  protected:
    /// Anchor the vtable in a translation unit.
    virtual void anchor();

    /// RuntimeDyld implementation that loaded this object.
    RuntimeDyldImpl &RTDyld;
    /// Mapping from object sections to RuntimeDyld section IDs.
    ObjSectionToIDMap ObjSecToIDMap;
  };

  /// Memory Management.
  class LLVM_ABI MemoryManager {
    friend class RuntimeDyld;

  public:
    /// Construct a default memory manager.
    MemoryManager() = default;
    /// Destroy the memory manager.
    virtual ~MemoryManager() = default;

    /// Allocate a memory block suitable for executable code.
    ///
    /// The SectionID is a unique identifier assigned by the RuntimeDyld
    /// instance, and optionally recorded by the memory manager to access a
    /// loaded section.
    /// \param Size Minimum number of bytes to allocate.
    /// \param Alignment Required alignment of the allocated block.
    /// \param SectionID Unique identifier for the section being allocated.
    /// \param SectionName Name of the section being allocated.
    /// \returns Pointer to the allocated code memory, or nullptr on failure.
    virtual uint8_t *allocateCodeSection(uintptr_t Size, unsigned Alignment,
                                         unsigned SectionID,
                                         StringRef SectionName) = 0;

    /// Allocate a memory block suitable for data.
    ///
    /// The SectionID is a unique identifier assigned by the JIT engine, and
    /// optionally recorded by the memory manager to access a loaded section.
    /// \param Size Minimum number of bytes to allocate.
    /// \param Alignment Required alignment of the allocated block.
    /// \param SectionID Unique identifier for the section being allocated.
    /// \param SectionName Name of the section being allocated.
    /// \param IsReadOnly Whether the section should be treated as read-only.
    /// \returns Pointer to the allocated data memory, or nullptr on failure.
    virtual uint8_t *allocateDataSection(uintptr_t Size, unsigned Alignment,
                                         unsigned SectionID,
                                         StringRef SectionName,
                                         bool IsReadOnly) = 0;

    /// An allocated TLS section
    struct TLSSection {
      /// The pointer to the initialization image
      uint8_t *InitializationImage;
      /// The TLS offset
      intptr_t Offset;
    };

    /// Allocate a memory block to be used for thread-local storage (TLS).
    /// \param Size Minimum number of bytes to allocate.
    /// \param Alignment Required alignment of the allocated block.
    /// \param SectionID Unique identifier for the section being allocated.
    /// \param SectionName Name of the section being allocated.
    /// \returns The allocated TLS section, including init image and offset.
    virtual TLSSection allocateTLSSection(uintptr_t Size, unsigned Alignment,
                                          unsigned SectionID,
                                          StringRef SectionName);

    /// Inform the memory manager of total memory needed for all sections.
    ///
    /// Note that by default the callback is disabled. To enable it
    /// redefine the method needsToReserveAllocationSpace to return true.
    /// \param CodeSize Total size of all code sections.
    /// \param CodeAlign Alignment required for code sections.
    /// \param RODataSize Total size of all read-only data sections.
    /// \param RODataAlign Alignment required for read-only data sections.
    /// \param RWDataSize Total size of all read-write data sections.
    /// \param RWDataAlign Alignment required for read-write data sections.
    virtual void reserveAllocationSpace(uintptr_t CodeSize, Align CodeAlign,
                                        uintptr_t RODataSize, Align RODataAlign,
                                        uintptr_t RWDataSize,
                                        Align RWDataAlign) {}

    /// Override to return true to enable the reserveAllocationSpace callback.
    /// \returns True if reserveAllocationSpace should be invoked.
    virtual bool needsToReserveAllocationSpace() { return false; }

    /// Return whether stub space may be allocated for this memory manager.
    ///
    /// Override to return false to tell LLVM no stub space will be needed.
    /// This requires some guarantees depending on architecture, but when you
    /// know what you are doing it saves allocated space.
    /// \returns True if stub allocation is allowed; false otherwise.
    virtual bool allowStubAllocation() const { return true; }

    /// Register the EH frames with the runtime so that c++ exceptions work.
    ///
    /// \p Addr parameter provides the local address of the EH frame section
    /// data, while \p LoadAddr provides the address of the data in the target
    /// address space.  If the section has not been remapped (which will usually
    /// be the case for local execution) these two values will be the same.
    /// \param Addr Local address of the EH frame section data.
    /// \param LoadAddr Address of the EH frame data in the target address space.
    /// \param Size Size in bytes of the EH frame section.
    virtual void registerEHFrames(uint8_t *Addr, uint64_t LoadAddr,
                                  size_t Size) = 0;
    /// Deregister previously registered EH frames with the runtime.
    virtual void deregisterEHFrames() = 0;

    /// Apply section permissions and cache coherency after loading completes.
    ///
    /// This method is called when object loading is complete and section page
    /// permissions can be applied.  It is up to the memory manager
    /// implementation to decide whether or not to act on this method.  The
    /// memory manager will typically allocate all sections as read-write and
    /// then apply specific permissions when this method is called.  Code
    /// sections cannot be executed until this function has been called.  In
    /// addition, any cache coherency operations needed to reliably use the
    /// memory are also performed.
    ///
    /// Returns true if an error occurred, false otherwise.
    /// \param ErrMsg Optional string that receives an error description.
    /// \returns True if an error occurred, false otherwise.
    virtual bool finalizeMemory(std::string *ErrMsg = nullptr) = 0;

    /// This method is called after an object has been loaded into memory but
    /// before relocations are applied to the loaded sections.
    ///
    /// Memory managers which are preparing code for execution in an external
    /// address space can use this call to remap the section addresses for the
    /// newly loaded object.
    ///
    /// For clients that do not need access to an ExecutionEngine instance this
    /// method should be preferred to its cousin
    /// MCJITMemoryManager::notifyObjectLoaded as this method is compatible with
    /// ORC JIT stacks.
    /// \param RTDyld RuntimeDyld instance that loaded the object.
    /// \param Obj Object file that was loaded into memory.
    virtual void notifyObjectLoaded(RuntimeDyld &RTDyld,
                                    const object::ObjectFile &Obj) {}

  private:
    virtual void anchor();

    bool FinalizationLocked = false;
  };

  /// Construct a RuntimeDyld instance.
  /// \param MemMgr Memory manager used to allocate and finalize sections.
  /// \param Resolver Symbol resolver used for external references.
  LLVM_ABI RuntimeDyld(MemoryManager &MemMgr, JITSymbolResolver &Resolver);
  /// Deleted copy constructor.
  /// \param Other Unused; copy construction is deleted.
  RuntimeDyld(const RuntimeDyld &Other) = delete;
  /// Deleted copy assignment operator.
  /// \param Other Unused; copy assignment is deleted.
  RuntimeDyld &operator=(const RuntimeDyld &Other) = delete;
  /// Destroy the RuntimeDyld instance and release owned state.
  LLVM_ABI ~RuntimeDyld();

  /// Add the referenced object file to the list of objects to be loaded and
  /// relocated.
  /// \param O Object file to load and relocate.
  /// \returns Loaded-object info for \p O, or null if loading failed.
  LLVM_ABI std::unique_ptr<LoadedObjectInfo>
  loadObject(const object::ObjectFile &O);

  /// Get the address of our local copy of the named symbol.
  ///
  /// This may or may not be the address used for relocation (clients can copy
  /// the data around and resolve relocations based on where they put it).
  /// \param Name Symbol whose local address is requested.
  /// \returns Host pointer to the local copy of \p Name, or null if missing.
  LLVM_ABI void *getSymbolLocalAddress(StringRef Name) const;

  /// Get the section ID for the section containing the given symbol.
  /// \param Name Symbol whose containing section ID is requested.
  /// \returns Section ID that contains \p Name.
  LLVM_ABI unsigned getSymbolSectionID(StringRef Name) const;

  /// Get the target address and flags for the named symbol.
  /// This address is the one used for relocation.
  /// \param Name Symbol whose target address and flags are requested.
  /// \returns Evaluated symbol with target address and flags for \p Name.
  LLVM_ABI JITEvaluatedSymbol getSymbol(StringRef Name) const;

  /// Return a copy of the current symbol table.
  ///
  /// This can be used by on-finalized callbacks to extract the symbol table
  /// before throwing away the RuntimeDyld instance. Because the map keys
  /// (StringRefs) are backed by strings inside the RuntimeDyld instance, the
  /// map should be processed before the RuntimeDyld instance is discarded.
  /// \returns A map from symbol names to their evaluated addresses and flags.
  LLVM_ABI std::map<StringRef, JITEvaluatedSymbol> getSymbolTable() const;

  /// Resolve the relocations for all symbols we currently know about.
  LLVM_ABI void resolveRelocations();

  /// Map a local section address to its target address space value.
  ///
  /// Map the address of a JIT section as returned from the memory manager to
  /// the address in the target process as the running code will see it. This
  /// is the address which will be used for relocation resolution.
  /// \param LocalAddress Host address of the section as returned by the memory
  ///        manager.
  /// \param TargetAddress Address of the section in the target address space.
  LLVM_ABI void mapSectionAddress(const void *LocalAddress,
                                  uint64_t TargetAddress);

  /// Returns the section's working memory.
  /// \param SectionID Identifier of the section whose content is requested.
  /// \returns A StringRef over the section's local working-memory contents.
  LLVM_ABI StringRef getSectionContent(unsigned SectionID) const;

  /// Return the load address of the section identified by \p SectionID.
  /// \param SectionID Identifier of the section whose load address is
  ///        requested.
  /// \returns The section's load address.
  LLVM_ABI uint64_t getSectionLoadAddress(unsigned SectionID) const;

  /// Set the NotifyStubEmitted callback. This is used for debugging
  /// purposes. A callback is made for each stub that is generated.
  /// \param NotifyStubEmitted Callback invoked when a stub is emitted.
  void setNotifyStubEmitted(NotifyStubEmittedFunction NotifyStubEmitted) {
    this->NotifyStubEmitted = std::move(NotifyStubEmitted);
  }

  /// Register loaded EH frame sections that are not yet registered.
  ///
  /// Note, RuntimeDyld is responsible for identifying the EH frame and calling
  /// the memory manager with the EH frame section data. However, the memory
  /// manager itself will handle the actual target-specific EH frame
  /// registration.
  LLVM_ABI void registerEHFrames();

  /// Deregister EH frames previously registered with the memory manager.
  LLVM_ABI void deregisterEHFrames();

  /// Return whether RuntimeDyld has recorded an error.
  /// \returns True if an error message has been recorded.
  LLVM_ABI bool hasError();
  /// Return the most recent RuntimeDyld error message.
  /// \returns The most recent error string, or empty if none.
  LLVM_ABI StringRef getErrorString();

  /// Enable or disable processing of all object sections.
  ///
  /// By default, only sections that are "required for execution" are passed to
  /// the RTDyldMemoryManager, and other sections are discarded. Passing 'true'
  /// to this method will cause RuntimeDyld to pass all sections to its
  /// memory manager regardless of whether they are "required to execute" in the
  /// usual sense. This is useful for inspecting metadata sections that may not
  /// contain relocations, E.g. Debug info, stackmaps.
  ///
  /// Must be called before the first object file is loaded.
  /// \param ProcessAllSections If true, pass every section to the memory
  ///        manager.
  void setProcessAllSections(bool ProcessAllSections) {
    assert(!Dyld && "setProcessAllSections must be called before loadObject.");
    this->ProcessAllSections = ProcessAllSections;
  }

  /// Perform all actions needed to make the code owned by this RuntimeDyld
  /// instance executable:
  ///
  /// 1) Apply relocations.
  /// 2) Register EH frames.
  /// 3) Update memory permissions*.
  ///
  /// * Finalization is potentially recursive**, and the 3rd step will only be
  ///   applied by the outermost call to finalize. This allows different
  ///   RuntimeDyld instances to share a memory manager without the innermost
  ///   finalization locking the memory and causing relocation fixup errors in
  ///   outer instances.
  ///
  /// ** Recursive finalization occurs when one RuntimeDyld instances needs the
  ///   address of a symbol owned by some other instance in order to apply
  ///   relocations.
  ///
  LLVM_ABI void finalizeWithMemoryManagerLocking();

private:
  LLVM_ABI friend void jitLinkForORC(
      object::OwningBinary<object::ObjectFile> O,
      RuntimeDyld::MemoryManager &MemMgr, JITSymbolResolver &Resolver,
      bool ProcessAllSections,
      unique_function<Error(const object::ObjectFile &Obj, LoadedObjectInfo &,
                            std::map<StringRef, JITEvaluatedSymbol>)>
          OnLoaded,
      unique_function<void(object::OwningBinary<object::ObjectFile> O,
                           std::unique_ptr<LoadedObjectInfo>, Error)>
          OnEmitted);

  // RuntimeDyldImpl is the actual class. RuntimeDyld is just the public
  // interface.
  std::unique_ptr<RuntimeDyldImpl> Dyld;
  MemoryManager &MemMgr;
  JITSymbolResolver &Resolver;
  bool ProcessAllSections;
  NotifyStubEmittedFunction NotifyStubEmitted;
};

/// Asynchronously link \p O for ORC using RuntimeDyld.
///
/// Warning: This API is experimental and probably should not be used by anyone
/// but ORC's RTDyldObjectLinkingLayer2. Internally it constructs a RuntimeDyld
/// instance and uses continuation passing to perform the fix-up and finalize
/// steps asynchronously.
/// \param O Object file to link, wrapped in an owning binary.
/// \param MemMgr Memory manager used to allocate and finalize sections.
/// \param Resolver Symbol resolver used for external references.
/// \param ProcessAllSections Whether all sections should be passed to
///        \p MemMgr.
/// \param OnLoaded Continuation invoked after the object is loaded.
/// \param OnEmitted Continuation invoked after linking completes or fails.
LLVM_ABI void jitLinkForORC(
    object::OwningBinary<object::ObjectFile> O,
    RuntimeDyld::MemoryManager &MemMgr, JITSymbolResolver &Resolver,
    bool ProcessAllSections,
    unique_function<Error(const object::ObjectFile &Obj,
                          RuntimeDyld::LoadedObjectInfo &,
                          std::map<StringRef, JITEvaluatedSymbol>)>
        OnLoaded,
    unique_function<void(object::OwningBinary<object::ObjectFile>,
                         std::unique_ptr<RuntimeDyld::LoadedObjectInfo>, Error)>
        OnEmitted);

} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_RUNTIMEDYLD_H
