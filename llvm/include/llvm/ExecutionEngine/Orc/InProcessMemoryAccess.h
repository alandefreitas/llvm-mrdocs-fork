//===-- InProcessMemoryAccess.h - Direct, in-process mem access -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Accesses memory in the current process.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_INPROCESSMEMORYACCESS_H
#define LLVM_EXECUTIONENGINE_ORC_INPROCESSMEMORYACCESS_H

#include "llvm/ExecutionEngine/Orc/MemoryAccess.h"

namespace llvm::orc {

/// MemoryAccess implementation that reads and writes memory in the current
/// process.
class LLVM_ABI InProcessMemoryAccess : public MemoryAccess {
public:
  /// Construct an in-process memory access object.
  /// @param IsArch64Bit True if the process uses a 64-bit address space.
  InProcessMemoryAccess(bool IsArch64Bit) : IsArch64Bit(IsArch64Bit) {}

  /// Asynchronously write uint8 values into in-process memory.
  /// @param Ws Writes to perform.
  /// @param OnWriteComplete Callback invoked when the write completes.
  void writeUInt8sAsync(ArrayRef<tpctypes::UInt8Write> Ws,
                        WriteResultFn OnWriteComplete) override;

  /// Asynchronously write uint16 values into in-process memory.
  /// @param Ws Writes to perform.
  /// @param OnWriteComplete Callback invoked when the write completes.
  void writeUInt16sAsync(ArrayRef<tpctypes::UInt16Write> Ws,
                         WriteResultFn OnWriteComplete) override;

  /// Asynchronously write uint32 values into in-process memory.
  /// @param Ws Writes to perform.
  /// @param OnWriteComplete Callback invoked when the write completes.
  void writeUInt32sAsync(ArrayRef<tpctypes::UInt32Write> Ws,
                         WriteResultFn OnWriteComplete) override;

  /// Asynchronously write uint64 values into in-process memory.
  /// @param Ws Writes to perform.
  /// @param OnWriteComplete Callback invoked when the write completes.
  void writeUInt64sAsync(ArrayRef<tpctypes::UInt64Write> Ws,
                         WriteResultFn OnWriteComplete) override;

  /// Asynchronously write pointer values into in-process memory.
  /// @param Ws Writes to perform.
  /// @param OnWriteComplete Callback invoked when the write completes.
  void writePointersAsync(ArrayRef<tpctypes::PointerWrite> Ws,
                          WriteResultFn OnWriteComplete) override;

  /// Asynchronously write buffer contents into in-process memory.
  /// @param Ws Writes to perform.
  /// @param OnWriteComplete Callback invoked when the write completes.
  void writeBuffersAsync(ArrayRef<tpctypes::BufferWrite> Ws,
                         WriteResultFn OnWriteComplete) override;

  /// Asynchronously read uint8 values from in-process memory.
  /// @param Rs Addresses to read from.
  /// @param OnComplete Callback invoked with the read results.
  void readUInt8sAsync(ArrayRef<ExecutorAddr> Rs,
                       OnReadUIntsCompleteFn<uint8_t> OnComplete) override;

  /// Asynchronously read uint16 values from in-process memory.
  /// @param Rs Addresses to read from.
  /// @param OnComplete Callback invoked with the read results.
  void readUInt16sAsync(ArrayRef<ExecutorAddr> Rs,
                        OnReadUIntsCompleteFn<uint16_t> OnComplete) override;

  /// Asynchronously read uint32 values from in-process memory.
  /// @param Rs Addresses to read from.
  /// @param OnComplete Callback invoked with the read results.
  void readUInt32sAsync(ArrayRef<ExecutorAddr> Rs,
                        OnReadUIntsCompleteFn<uint32_t> OnComplete) override;

  /// Asynchronously read uint64 values from in-process memory.
  /// @param Rs Addresses to read from.
  /// @param OnComplete Callback invoked with the read results.
  void readUInt64sAsync(ArrayRef<ExecutorAddr> Rs,
                        OnReadUIntsCompleteFn<uint64_t> OnComplete) override;

  /// Asynchronously read pointer values from in-process memory.
  /// @param Rs Addresses to read from.
  /// @param OnComplete Callback invoked with the read results.
  void readPointersAsync(ArrayRef<ExecutorAddr> Rs,
                         OnReadPointersCompleteFn OnComplete) override;

  /// Asynchronously read buffer ranges from in-process memory.
  /// @param Rs Address ranges to read from.
  /// @param OnComplete Callback invoked with the read results.
  void readBuffersAsync(ArrayRef<ExecutorAddrRange> Rs,
                        OnReadBuffersCompleteFn OnComplete) override;

  /// Asynchronously read null-terminated strings from in-process memory.
  /// @param Rs Addresses of the strings to read.
  /// @param OnComplete Callback invoked with the read results.
  void readStringsAsync(ArrayRef<ExecutorAddr> Rs,
                        OnReadStringsCompleteFn OnComplete) override;

private:
  bool IsArch64Bit;
};

} // namespace llvm::orc

#endif // LLVM_EXECUTIONENGINE_ORC_INPROCESSMEMORYACCESS_H
