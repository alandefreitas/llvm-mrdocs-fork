//===---- EPCGenericRTDyldMemoryManager.h - EPC-based MemMgr ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines a RuntimeDyld::MemoryManager that uses EPC and the ORC runtime
// bootstrap functions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_EPCGENERICRTDYLDMEMORYMANAGER_H
#define LLVM_EXECUTIONENGINE_ORC_EPCGENERICRTDYLDMEMORYMANAGER_H

#include "llvm/ExecutionEngine/Orc/ExecutorProcessControl.h"
#include "llvm/ExecutionEngine/RuntimeDyld.h"
#include "llvm/Support/Compiler.h"

#define DEBUG_TYPE "orc"

namespace llvm {
namespace orc {

/// Remote-mapped RuntimeDyld-compatible memory manager.
class LLVM_ABI EPCGenericRTDyldMemoryManager
    : public RuntimeDyld::MemoryManager {
public:
  /// Symbol addresses for memory access.
  struct SymbolAddrs {
    /// Executor address of the memory-manager instance.
    ExecutorAddr Instance;
    /// Executor address of the reserve-allocation wrapper.
    ExecutorAddr Reserve;
    /// Executor address of the initialize-allocation wrapper.
    ExecutorAddr Initialize;
    /// Executor address of the release-allocation wrapper.
    ExecutorAddr Release;
    /// Executor address of the register-eh-frame wrapper.
    ExecutorAddr RegisterEHFrame;
    /// Executor address of the deregister-eh-frame wrapper.
    ExecutorAddr DeregisterEHFrame;
  };

  /// Create an EPCGenericRTDyldMemoryManager using the given EPC, looking up
  /// the default symbol names in the bootstrap symbol set.
  /// @param EPC Process control used to look up bootstrap symbols and issue
  ///        remote memory operations.
  /// @return A memory manager on success, or an error if bootstrap symbols
  ///         cannot be resolved.
  static Expected<std::unique_ptr<EPCGenericRTDyldMemoryManager>>
  CreateWithDefaultBootstrapSymbols(ExecutorProcessControl &EPC);

  /// Create an EPCGenericRTDyldMemoryManager using the given EPC and symbol
  /// addrs.
  /// @param EPC Process control used to issue remote memory operations.
  /// @param SAs Executor addresses of the ORC runtime memory-manager wrappers.
  EPCGenericRTDyldMemoryManager(ExecutorProcessControl &EPC, SymbolAddrs SAs);

  /// Deleted copy constructor.
  /// @param Other Instance that would be copied.
  EPCGenericRTDyldMemoryManager(const EPCGenericRTDyldMemoryManager &Other) =
      delete;
  /// Deleted copy assignment operator.
  /// @param Other Instance that would be copied.
  EPCGenericRTDyldMemoryManager &
  operator=(const EPCGenericRTDyldMemoryManager &Other) = delete;
  /// Deleted move constructor.
  /// @param Other Instance that would be moved.
  EPCGenericRTDyldMemoryManager(EPCGenericRTDyldMemoryManager &&Other) = delete;
  /// Deleted move assignment operator.
  /// @param Other Instance that would be moved.
  EPCGenericRTDyldMemoryManager &
  operator=(EPCGenericRTDyldMemoryManager &&Other) = delete;
  /// Destroy the memory manager and release remote allocations.
  ~EPCGenericRTDyldMemoryManager() override;

  /// Allocate a memory block suitable for executable code.
  /// @param Size Minimum number of bytes to allocate.
  /// @param Alignment Required alignment of the allocated block.
  /// @param SectionID Unique identifier for the section being allocated.
  /// @param SectionName Name of the section being allocated.
  /// @return Pointer to the allocated code memory, or nullptr on failure.
  uint8_t *allocateCodeSection(uintptr_t Size, unsigned Alignment,
                               unsigned SectionID,
                               StringRef SectionName) override;

  /// Allocate a memory block suitable for data.
  /// @param Size Minimum number of bytes to allocate.
  /// @param Alignment Required alignment of the allocated block.
  /// @param SectionID Unique identifier for the section being allocated.
  /// @param SectionName Name of the section being allocated.
  /// @param IsReadOnly Whether the section should be treated as read-only.
  /// @return Pointer to the allocated data memory, or nullptr on failure.
  uint8_t *allocateDataSection(uintptr_t Size, unsigned Alignment,
                               unsigned SectionID, StringRef SectionName,
                               bool IsReadOnly) override;

  /// Reserve remote memory for upcoming code and data section allocations.
  /// @param CodeSize Total size of all code sections.
  /// @param CodeAlign Alignment required for code sections.
  /// @param RODataSize Total size of all read-only data sections.
  /// @param RODataAlign Alignment required for read-only data sections.
  /// @param RWDataSize Total size of all read-write data sections.
  /// @param RWDataAlign Alignment required for read-write data sections.
  void reserveAllocationSpace(uintptr_t CodeSize, Align CodeAlign,
                              uintptr_t RODataSize, Align RODataAlign,
                              uintptr_t RWDataSize, Align RWDataAlign) override;

  /// Return true to enable the reserveAllocationSpace callback.
  /// @return True if reserveAllocationSpace should be invoked.
  bool needsToReserveAllocationSpace() override;

  /// Register EH frames with the remote runtime so C++ exceptions work.
  /// @param Addr Local address of the EH frame section data.
  /// @param LoadAddr Address of the EH frame data in the target address space.
  /// @param Size Size in bytes of the EH frame section.
  void registerEHFrames(uint8_t *Addr, uint64_t LoadAddr, size_t Size) override;

  /// Deregister previously registered EH frames with the remote runtime.
  void deregisterEHFrames() override;

  /// Remap section addresses after an object has been loaded into memory.
  /// @param Dyld RuntimeDyld instance that loaded the object.
  /// @param Obj Object file that was loaded into memory.
  void notifyObjectLoaded(RuntimeDyld &Dyld,
                          const object::ObjectFile &Obj) override;

  /// Copy contents to the executor and apply final section permissions.
  /// @param ErrMsg Optional string that receives an error description.
  /// @return True if an error occurred, false otherwise.
  bool finalizeMemory(std::string *ErrMsg = nullptr) override;

private:
  struct SectionAlloc {
  public:
    SectionAlloc(uint64_t Size, unsigned Align)
        : Size(Size), Align(Align),
          Contents(std::make_unique<uint8_t[]>(Size + Align - 1)) {}

    uint64_t Size;
    unsigned Align;
    std::unique_ptr<uint8_t[]> Contents;
    ExecutorAddr RemoteAddr;
  };

  // Group of section allocations to be allocated together in the executor. The
  // RemoteCodeAddr will stand in as the id of the group for deallocation
  // purposes.
  struct SectionAllocGroup {
    SectionAllocGroup() = default;
    SectionAllocGroup(const SectionAllocGroup &) = delete;
    SectionAllocGroup &operator=(const SectionAllocGroup &) = delete;
    SectionAllocGroup(SectionAllocGroup &&) = default;
    SectionAllocGroup &operator=(SectionAllocGroup &&) = default;

    ExecutorAddrRange RemoteCode;
    ExecutorAddrRange RemoteROData;
    ExecutorAddrRange RemoteRWData;
    std::vector<ExecutorAddrRange> UnfinalizedEHFrames;
    std::vector<SectionAlloc> CodeAllocs, RODataAllocs, RWDataAllocs;
  };

  // Maps all allocations in SectionAllocs to aligned blocks
  void mapAllocsToRemoteAddrs(RuntimeDyld &Dyld,
                              std::vector<SectionAlloc> &SecAllocs,
                              ExecutorAddr NextAddr);

  ExecutorProcessControl &EPC;
  SymbolAddrs SAs;

  std::mutex M;
  std::vector<SectionAllocGroup> Unmapped;
  std::vector<SectionAllocGroup> Unfinalized;
  std::vector<ExecutorAddr> FinalizedAllocs;
  std::string ErrMsg;
};

} // end namespace orc
} // end namespace llvm

#undef DEBUG_TYPE

#endif // LLVM_EXECUTIONENGINE_ORC_EPCGENERICRTDYLDMEMORYMANAGER_H
