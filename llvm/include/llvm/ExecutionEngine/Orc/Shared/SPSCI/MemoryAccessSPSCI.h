//===--- MemoryAccessSPSCI.h - SPS CI for memory access ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// SPS controller-interface descriptors for the executor's memory-access
// operations. These wrappers perform the operation directly, so they take the
// operation's data arguments rather than a callee address.
//
// See CallSPSCI.h for a description of the descriptor scheme.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_SHARED_SPSCI_MEMORYACCESSSPSCI_H
#define LLVM_EXECUTIONENGINE_ORC_SHARED_SPSCI_MEMORYACCESSSPSCI_H

#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"
#include "llvm/ExecutionEngine/Orc/Shared/SimplePackedSerialization.h"
#include "llvm/ExecutionEngine/Orc/Shared/TargetProcessControlTypes.h"

#include <cstdint>

namespace llvm::orc::rt::sps_ci {

/// Writes a sequence of uint8 values into executor memory.
struct MemWriteUInt8s {
  /// Name of the MemWriteUInt8s SPS wrapper function.
  static constexpr char Name[] = "orc_rt_ci_sps_mem_write_uint8s";
  /// SPS signature for MemWriteUInt8s.
  using SPSSig = void(shared::SPSSequence<shared::SPSMemoryAccessUInt8Write>);
};

/// Writes a sequence of uint16 values into executor memory.
struct MemWriteUInt16s {
  /// Name of the MemWriteUInt16s SPS wrapper function.
  static constexpr char Name[] = "orc_rt_ci_sps_mem_write_uint16s";
  /// SPS signature for MemWriteUInt16s.
  using SPSSig = void(shared::SPSSequence<shared::SPSMemoryAccessUInt16Write>);
};

/// Writes a sequence of uint32 values into executor memory.
struct MemWriteUInt32s {
  /// Name of the MemWriteUInt32s SPS wrapper function.
  static constexpr char Name[] = "orc_rt_ci_sps_mem_write_uint32s";
  /// SPS signature for MemWriteUInt32s.
  using SPSSig = void(shared::SPSSequence<shared::SPSMemoryAccessUInt32Write>);
};

/// Writes a sequence of uint64 values into executor memory.
struct MemWriteUInt64s {
  /// Name of the MemWriteUInt64s SPS wrapper function.
  static constexpr char Name[] = "orc_rt_ci_sps_mem_write_uint64s";
  /// SPS signature for MemWriteUInt64s.
  using SPSSig = void(shared::SPSSequence<shared::SPSMemoryAccessUInt64Write>);
};

/// Writes a sequence of pointers into executor memory.
struct MemWritePointers {
  /// Name of the MemWritePointers SPS wrapper function.
  static constexpr char Name[] = "orc_rt_ci_sps_mem_write_pointers";
  /// SPS signature for MemWritePointers.
  using SPSSig = void(shared::SPSSequence<shared::SPSMemoryAccessPointerWrite>);
};

/// Writes a sequence of buffers into executor memory.
struct MemWriteBuffers {
  /// Name of the MemWriteBuffers SPS wrapper function.
  static constexpr char Name[] = "orc_rt_ci_sps_mem_write_buffers";
  /// SPS signature for MemWriteBuffers.
  using SPSSig = void(shared::SPSSequence<shared::SPSMemoryAccessBufferWrite>);
};

/// Reads a sequence of uint8 values from executor memory addresses.
struct MemReadUInt8s {
  /// Name of the MemReadUInt8s SPS wrapper function.
  static constexpr char Name[] = "orc_rt_ci_sps_mem_read_uint8s";
  /// SPS signature for MemReadUInt8s.
  using SPSSig = shared::SPSSequence<uint8_t>(
      shared::SPSSequence<shared::SPSExecutorAddr>);
};

/// Reads a sequence of uint16 values from executor memory addresses.
struct MemReadUInt16s {
  /// Name of the MemReadUInt16s SPS wrapper function.
  static constexpr char Name[] = "orc_rt_ci_sps_mem_read_uint16s";
  /// SPS signature for MemReadUInt16s.
  using SPSSig = shared::SPSSequence<uint16_t>(
      shared::SPSSequence<shared::SPSExecutorAddr>);
};

/// Reads a sequence of uint32 values from executor memory addresses.
struct MemReadUInt32s {
  /// Name of the MemReadUInt32s SPS wrapper function.
  static constexpr char Name[] = "orc_rt_ci_sps_mem_read_uint32s";
  /// SPS signature for MemReadUInt32s.
  using SPSSig = shared::SPSSequence<uint32_t>(
      shared::SPSSequence<shared::SPSExecutorAddr>);
};

/// Reads a sequence of uint64 values from executor memory addresses.
struct MemReadUInt64s {
  /// Name of the MemReadUInt64s SPS wrapper function.
  static constexpr char Name[] = "orc_rt_ci_sps_mem_read_uint64s";
  /// SPS signature for MemReadUInt64s.
  using SPSSig = shared::SPSSequence<uint64_t>(
      shared::SPSSequence<shared::SPSExecutorAddr>);
};

/// Reads a sequence of pointers from executor memory addresses.
struct MemReadPointers {
  /// Name of the MemReadPointers SPS wrapper function.
  static constexpr char Name[] = "orc_rt_ci_sps_mem_read_pointers";
  /// SPS signature for MemReadPointers.
  using SPSSig = shared::SPSSequence<shared::SPSExecutorAddr>(
      shared::SPSSequence<shared::SPSExecutorAddr>);
};

/// Reads a sequence of buffers from executor memory address ranges.
struct MemReadBuffers {
  /// Name of the MemReadBuffers SPS wrapper function.
  static constexpr char Name[] = "orc_rt_ci_sps_mem_read_buffers";
  /// SPS signature for MemReadBuffers.
  using SPSSig = shared::SPSSequence<shared::SPSSequence<uint8_t>>(
      shared::SPSSequence<shared::SPSExecutorAddrRange>);
};

/// Reads a sequence of null-terminated strings from executor memory addresses.
struct MemReadStrings {
  /// Name of the MemReadStrings SPS wrapper function.
  static constexpr char Name[] = "orc_rt_ci_sps_mem_read_strings";
  /// SPS signature for MemReadStrings.
  using SPSSig = shared::SPSSequence<shared::SPSString>(
      shared::SPSSequence<shared::SPSExecutorAddr>);
};

} // namespace llvm::orc::rt::sps_ci

#endif // LLVM_EXECUTIONENGINE_ORC_SHARED_SPSCI_MEMORYACCESSSPSCI_H
