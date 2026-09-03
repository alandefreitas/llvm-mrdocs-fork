//===--------------- MapperJITLinkMemoryManager.h -*- C++ -*---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements JITLinkMemoryManager using MemoryMapper
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_MAPPERJITLINKMEMORYMANAGER_H
#define LLVM_EXECUTIONENGINE_ORC_MAPPERJITLINKMEMORYMANAGER_H

#include "llvm/ADT/IntervalMap.h"
#include "llvm/ExecutionEngine/JITLink/JITLinkMemoryManager.h"
#include "llvm/ExecutionEngine/Orc/MemoryMapper.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
namespace orc {

/// JITLinkMemoryManager that allocates memory through a MemoryMapper.
class LLVM_ABI MapperJITLinkMemoryManager
    : public jitlink::JITLinkMemoryManager {
public:
  /// Create a MapperJITLinkMemoryManager with the given reservation
  /// granularity and memory mapper.
  /// \param ReservationGranularity Size of address-space reservation units.
  /// \param Mapper Mapper used to reserve and map executor memory.
  MapperJITLinkMemoryManager(size_t ReservationGranularity,
                             std::unique_ptr<MemoryMapper> Mapper);

  /// Create a MapperJITLinkMemoryManager that owns a newly constructed
  /// MemoryMapper of type \p MemoryMapperType.
  /// \param ReservationGranularity Size of address-space reservation units.
  /// \param A Arguments forwarded to MemoryMapperType::Create.
  /// \return A MapperJITLinkMemoryManager on success, or an error on failure.
  template <class MemoryMapperType, class... Args>
  static Expected<std::unique_ptr<MapperJITLinkMemoryManager>>
  CreateWithMapper(size_t ReservationGranularity, Args &&...A) {
    auto Mapper = MemoryMapperType::Create(std::forward<Args>(A)...);
    if (!Mapper)
      return Mapper.takeError();

    return std::make_unique<MapperJITLinkMemoryManager>(ReservationGranularity,
                                                        std::move(*Mapper));
  }

  /// Allocate memory for the segments of \p G via the owned MemoryMapper.
  /// \param JD JITLink dylib associated with the allocation, or null.
  /// \param G Link graph whose segments are being allocated.
  /// \param OnAllocated Continuation invoked when allocation completes.
  void allocate(const jitlink::JITLinkDylib *JD, jitlink::LinkGraph &G,
                OnAllocatedFunction OnAllocated) override;
  /// Inherit the convenience allocate overloads from the base class.
  using JITLinkMemoryManager::allocate;

  /// Deallocate a list of finalized allocations via the owned MemoryMapper.
  /// \param Allocs Finalized allocations to deallocate.
  /// \param OnDeallocated Continuation invoked when deallocation completes.
  void deallocate(std::vector<FinalizedAlloc> Allocs,
                  OnDeallocatedFunction OnDeallocated) override;
  /// Inherit the convenience deallocate overloads from the base class.
  using JITLinkMemoryManager::deallocate;

private:
  class InFlightAlloc;

  std::mutex Mutex;

  // We reserve multiples of this from the executor address space
  size_t ReservationUnits;

  // Ranges that have been reserved in executor but not yet allocated
  using AvailableMemoryMap = IntervalMap<ExecutorAddr, bool>;
  AvailableMemoryMap::Allocator AMAllocator;
  IntervalMap<ExecutorAddr, bool> AvailableMemory;

  // Ranges that have been reserved in executor and already allocated
  DenseMap<ExecutorAddr, ExecutorAddrDiff> UsedMemory;

  std::unique_ptr<MemoryMapper> Mapper;
};

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_MAPPERJITLINKMEMORYMANAGER_H
