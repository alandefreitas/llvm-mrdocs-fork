//===----- CallSPSCI.h - SPS CI for running functions -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// SPS controller-interface descriptors for running functions in the executor.
//
// Each descriptor names one operation in the runtime's SPS controller
// interface and gives its wire signature. This is the contract between the
// party that implements the operation (the ORC runtime, or LLVM's
// OrcTargetProcess) and the party that calls it (the ORC controller), so it
// must not depend on either side's implementation.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_SHARED_SPSCI_CALLSPSCI_H
#define LLVM_EXECUTIONENGINE_ORC_SHARED_SPSCI_CALLSPSCI_H

#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"
#include "llvm/ExecutionEngine/Orc/Shared/SimplePackedSerialization.h"

#include <cstdint>

namespace llvm::orc::rt {
/// SPS controller-interface descriptors for ORC runtime operations.
namespace sps_ci {

/// Runs a main-like function (int(int argc, char *argv[])) in the executor.
/// Takes the function's address and an argument vector.
struct CallMain {
  /// Name of the CallMain SPS wrapper function.
  static constexpr char Name[] = "orc_rt_ci_sps_call_main";
  /// SPS signature for CallMain.
  using SPSSig = int64_t(shared::SPSExecutorAddr,
                         shared::SPSSequence<shared::SPSString>);
};

/// Runs a void() function in the executor, given its address.
/// WARNING: This operation is experimental and may be removed.
struct CallVoidVoid {
  /// Name of the CallVoidVoid SPS wrapper function.
  static constexpr char Name[] = "orc_rt_ci_sps_call_void_void";
  /// SPS signature for CallVoidVoid.
  using SPSSig = void(shared::SPSExecutorAddr);
};

/// Runs an int32_t() function in the executor, given its address.
/// WARNING: This operation is experimental and may be removed.
struct CallInt32Void {
  /// Name of the CallInt32Void SPS wrapper function.
  static constexpr char Name[] = "orc_rt_ci_sps_call_int32_void";
  /// SPS signature for CallInt32Void.
  using SPSSig = int32_t(shared::SPSExecutorAddr);
};

/// Runs an int32_t(int32_t) function in the executor, given its address.
/// WARNING: This operation is experimental and may be removed.
struct CallInt32Int32 {
  /// Name of the CallInt32Int32 SPS wrapper function.
  static constexpr char Name[] = "orc_rt_ci_sps_call_int32_int32";
  /// SPS signature for CallInt32Int32.
  using SPSSig = int32_t(shared::SPSExecutorAddr, int32_t);
};

} // namespace sps_ci
} // namespace llvm::orc::rt

#endif // LLVM_EXECUTIONENGINE_ORC_SHARED_SPSCI_CALLSPSCI_H
