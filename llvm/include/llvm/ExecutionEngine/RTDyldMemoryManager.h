//===-- RTDyldMemoryManager.cpp - Memory manager for MC-JIT -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Interface of the runtime dynamic memory manager base class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_RTDYLDMEMORYMANAGER_H
#define LLVM_EXECUTIONENGINE_RTDYLDMEMORYMANAGER_H

#include "llvm-c/ExecutionEngine.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/RuntimeDyld.h"
#include "llvm/Support/CBindingWrapping.h"
#include "llvm/Support/Compiler.h"
#include <cstddef>
#include <cstdint>
#include <string>

namespace llvm {

class ExecutionEngine;

namespace object {
  class ObjectFile;
} // end namespace object

/// Memory manager interface for MCJIT with ExecutionEngine-aware callbacks.
class LLVM_ABI MCJITMemoryManager : public RuntimeDyld::MemoryManager {
public:
  /// Bring in RuntimeDyld::MemoryManager::notifyObjectLoaded.
  using RuntimeDyld::MemoryManager::notifyObjectLoaded;

  /// Notify after an object is loaded but before relocations are applied.
  ///
  /// This method is called after an object has been loaded into memory but
  /// before relocations are applied to the loaded sections.  The object load
  /// may have been initiated by MCJIT to resolve an external symbol for another
  /// object that is being finalized.  In that case, the object about which
  /// the memory manager is being notified will be finalized immediately after
  /// the memory manager returns from this call.
  ///
  /// Memory managers which are preparing code for execution in an external
  /// address space can use this call to remap the section addresses for the
  /// newly loaded object.
  /// \param EE Execution engine that loaded the object.
  /// \param Obj Object file that was loaded into memory.
  virtual void notifyObjectLoaded(ExecutionEngine *EE,
                                  const object::ObjectFile &Obj) {}

private:
  void anchor() override;
};

/// Memory manager and symbol resolver for RuntimeDyld and MCJIT clients.
///
/// RuntimeDyld clients often want to handle the memory management of what gets
/// placed where. For JIT clients, this is the subset of JITMemoryManager
/// required for dynamic loading of binaries.
///
/// FIXME: As the RuntimeDyld fills out, additional routines will be needed
///        for the varying types of objects to be allocated.
class LLVM_ABI RTDyldMemoryManager : public MCJITMemoryManager,
                                     public LegacyJITSymbolResolver {
public:
  /// Construct a default RTDyld memory manager.
  RTDyldMemoryManager() = default;
  /// Deleted copy constructor.
  /// \param Unused Ignored; copy construction is not supported.
  RTDyldMemoryManager(const RTDyldMemoryManager &Unused) = delete;
  /// Deleted copy assignment operator.
  /// \param Unused Ignored; copy assignment is not supported.
  void operator=(const RTDyldMemoryManager &Unused) = delete;
  /// Destroy the RTDyld memory manager.
  ~RTDyldMemoryManager() override;

  /// Register EH frames in the current process.
  /// \param Addr Local address of the EH frame section data.
  /// \param Size Size in bytes of the EH frame section.
  static void registerEHFramesInProcess(uint8_t *Addr, size_t Size);

  /// Deregister EH frames in the current process.
  /// \param Addr Local address of the EH frame section data.
  /// \param Size Size in bytes of the EH frame section.
  static void deregisterEHFramesInProcess(uint8_t *Addr, size_t Size);

  /// Register the EH frames with the runtime so that C++ exceptions work.
  ///
  /// \p Addr parameter provides the local address of the EH frame section
  /// data, while \p LoadAddr provides the address of the data in the target
  /// address space.  If the section has not been remapped (which will usually
  /// be the case for local execution) these two values will be the same.
  /// \param Addr Local address of the EH frame section data.
  /// \param LoadAddr Address of the EH frame data in the target address space.
  /// \param Size Size in bytes of the EH frame section.
  void registerEHFrames(uint8_t *Addr, uint64_t LoadAddr, size_t Size) override;
  /// Deregister previously registered EH frames with the runtime.
  void deregisterEHFrames() override;

  /// This method returns the address of the specified function or variable in
  /// the current process.
  /// \param Name Symbol name to look up in the current process.
  /// \returns Address of \p Name in the current process, or zero if not found.
  static uint64_t getSymbolAddressInProcess(const std::string &Name);

  /// Legacy symbol lookup - DEPRECATED! Please override findSymbol instead.
  ///
  /// This method returns the address of the specified function or variable.
  /// It is used to resolve symbols during module linking.
  /// \param Name Symbol name to look up.
  /// \returns Address of \p Name, or zero if not found.
  virtual uint64_t getSymbolAddress(const std::string &Name) {
    return getSymbolAddressInProcess(Name);
  }

  /// This method returns a RuntimeDyld::SymbolInfo for the specified function
  /// or variable. It is used to resolve symbols during module linking.
  ///
  /// By default this falls back on the legacy lookup method:
  /// 'getSymbolAddress'. The address returned by getSymbolAddress is treated as
  /// a strong, exported symbol, consistent with historical treatment by
  /// RuntimeDyld.
  ///
  /// Clients writing custom RTDyldMemoryManagers are encouraged to override
  /// this method and return a SymbolInfo with the flags set correctly. This is
  /// necessary for RuntimeDyld to correctly handle weak and non-exported symbols.
  /// \param Name Symbol name to look up.
  /// \returns A JITSymbol for \p Name, or an empty symbol if not found.
  JITSymbol findSymbol(const std::string &Name) override {
    return JITSymbol(getSymbolAddress(Name), JITSymbolFlags::Exported);
  }

  /// Legacy symbol lookup -- DEPRECATED! Please override
  /// findSymbolInLogicalDylib instead.
  ///
  /// Default to treating all modules as separate.
  /// \param Name Symbol name to look up in the logical dylib.
  /// \returns Address of \p Name in the logical dylib, or zero if not found.
  virtual uint64_t getSymbolAddressInLogicalDylib(const std::string &Name) {
    return 0;
  }

  /// Default to treating all modules as separate.
  ///
  /// By default this falls back on the legacy lookup method:
  /// 'getSymbolAddressInLogicalDylib'. The address returned by
  /// getSymbolAddressInLogicalDylib is treated as a strong, exported symbol,
  /// consistent with historical treatment by RuntimeDyld.
  ///
  /// Clients writing custom RTDyldMemoryManagers are encouraged to override
  /// this method and return a SymbolInfo with the flags set correctly. This is
  /// necessary for RuntimeDyld to correctly handle weak and non-exported symbols.
  /// \param Name Symbol name to look up in the logical dylib.
  /// \returns A JITSymbol for \p Name in the logical dylib, or an empty symbol
  /// if not found.
  JITSymbol
  findSymbolInLogicalDylib(const std::string &Name) override {
    return JITSymbol(getSymbolAddressInLogicalDylib(Name),
                          JITSymbolFlags::Exported);
  }

  /// This method returns the address of the specified function. As such it is
  /// only useful for resolving library symbols, not code generated symbols.
  ///
  /// If \p AbortOnFailure is false and no function with the given name is
  /// found, this function returns a null pointer. Otherwise, it prints a
  /// message to stderr and aborts.
  ///
  /// This function is deprecated for memory managers to be used with
  /// MCJIT or RuntimeDyld.  Use getSymbolAddress instead.
  /// \param Name Function name to look up.
  /// \param AbortOnFailure Whether to abort if the function is not found.
  /// \returns Host pointer to the named function, or null if not found and
  /// \p AbortOnFailure is false.
  virtual void *getPointerToNamedFunction(const std::string &Name,
                                          bool AbortOnFailure = true);

protected:
  /// An EH frame section recorded for later registration or deregistration.
  struct EHFrame {
    /// Local address of the EH frame section data.
    uint8_t *Addr;
    /// Size in bytes of the EH frame section.
    size_t Size;
  };
  /// List of EH frame sections managed by this memory manager.
  typedef std::vector<EHFrame> EHFrameInfos;
  /// EH frame sections registered with this memory manager.
  EHFrameInfos EHFrames;

private:
  void anchor() override;
};

// Create wrappers for C Binding types (see CBindingWrapping.h).
/// C API conversion helpers for \c RTDyldMemoryManager /
/// \c LLVMMCJITMemoryManagerRef, including \c unwrap and \c wrap.
/// \param P Value to convert between the C++ type and the C API reference.
/// \returns The corresponding C++ pointer or C API reference.
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(
    RTDyldMemoryManager, LLVMMCJITMemoryManagerRef)

} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_RTDYLDMEMORYMANAGER_H
