//===---------------- SimpleExecutorMemoryManager.h -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A simple allocator class suitable for basic remote-JIT use.
//
// FIXME: The functionality in this file should be moved to the ORC runtime.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_SIMPLEEXECUTORMEMORYMANAGER_H
#define LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_SIMPLEEXECUTORMEMORYMANAGER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"
#include "llvm/ExecutionEngine/Orc/Shared/TargetProcessControlTypes.h"
#include "llvm/ExecutionEngine/Orc/Shared/WrapperFunctionUtils.h"
#include "llvm/ExecutionEngine/Orc/TargetProcess/ExecutorBootstrapService.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"

#include <mutex>

namespace llvm {
namespace orc {
namespace rt_bootstrap {

/// Simple page-based allocator.
class LLVM_ABI SimpleExecutorMemoryManager : public ExecutorBootstrapService {
public:
  /// Destroys the simple executor memory manager.
  ~SimpleExecutorMemoryManager() override;

  /// Reserves address space in this process.
  /// \param Size Number of bytes to reserve.
  /// \return Base address of the reserved range, or an error.
  Expected<ExecutorAddr> reserve(uint64_t Size);
  /// Copies content, applies protections, and runs finalization actions.
  /// \param FR Finalize request describing segments and actions to apply.
  /// \return Allocation key identifying the initialized region, or an error.
  Expected<ExecutorAddr> initialize(tpctypes::FinalizeRequest &FR);
  /// Runs previously registered deallocation actions.
  /// \param InitKeys Allocation keys previously returned by initialize.
  /// \return Success, or an error if deinitialization fails.
  Error deinitialize(const std::vector<ExecutorAddr> &InitKeys);
  /// Releases address space acquired through reserve.
  /// \param Bases Base addresses of ranges previously returned by reserve.
  /// \return Success, or an error if release fails.
  Error release(const std::vector<ExecutorAddr> &Bases);

  /// Shuts down this service and releases any remaining reservations.
  /// \return Success, or an error if shutdown fails.
  Error shutdown() override;
  /// Adds this service's bootstrap symbols to \p M.
  /// \param M Map of bootstrap symbol names to executor addresses to update.
  void addBootstrapSymbols(StringMap<ExecutorAddr> &M) override;

private:
  struct RegionInfo {
    size_t Size = 0;
    std::vector<shared::WrapperFunctionCall> DeallocActions;
  };

  struct SlabInfo {
    using RegionMap = std::map<ExecutorAddr, RegionInfo>;
    size_t Size = 0;
    RegionMap Regions;
  };

  using SlabMap = std::map<void *, SlabInfo>;

  /// Get a reference to the slab information for the slab containing the given
  /// address.
  Expected<SlabInfo &> getSlabInfo(ExecutorAddr A, StringRef Context);

  /// Get a reference to the slab information for the slab *covering* the given
  /// range. The given range must be a subrange of e(possibly equal to) the
  /// range of the slab itself.
  Expected<SlabInfo &> getSlabInfo(ExecutorAddrRange R, StringRef Context);

  /// Create a RegionInfo for the given range, which must not overlap any
  /// existing region.
  Expected<RegionInfo &> createRegionInfo(ExecutorAddrRange R,
                                          StringRef Context);

  /// Get a reference to the region information for the given address. This
  /// address must represent the start of an existing initialized region.
  Expected<RegionInfo &> getRegionInfo(SlabInfo &Slab, ExecutorAddr A,
                                       StringRef Context);

  /// Get a reference to the region information for the given address. This
  /// address must represent the start of an existing initialized region.
  Expected<RegionInfo &> getRegionInfo(ExecutorAddr A, StringRef Context);

  static llvm::orc::shared::CWrapperFunctionBuffer
  reserveWrapper(const char *ArgData, size_t ArgSize);

  static llvm::orc::shared::CWrapperFunctionBuffer
  initializeWrapper(const char *ArgData, size_t ArgSize);

  static llvm::orc::shared::CWrapperFunctionBuffer
  deinitializeWrapper(const char *ArgData, size_t ArgSize);

  static llvm::orc::shared::CWrapperFunctionBuffer
  releaseWrapper(const char *ArgData, size_t ArgSize);

  std::mutex M;
  SlabMap Slabs;
};

} // end namespace rt_bootstrap
} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_SIMPLEEXECUTORMEMORYMANAGER_H
