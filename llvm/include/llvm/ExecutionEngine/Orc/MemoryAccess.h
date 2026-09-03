//===------- MemoryAccess.h - Executor memory access APIs -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Utilities for accessing memory in the executor processes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_MEMORYACCESS_H
#define LLVM_EXECUTIONENGINE_ORC_MEMORYACCESS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/FunctionExtras.h"
#include "llvm/ExecutionEngine/Orc/Shared/TargetProcessControlTypes.h"
#include "llvm/Support/MSVCErrorWorkarounds.h"

#include <future>

namespace llvm::orc {

/// APIs for manipulating memory in the target process.
class LLVM_ABI MemoryAccess {
public:
  /// Callback function for asynchronous writes.
  using WriteResultFn = unique_function<void(Error)>;

  /// Result type for asynchronous unsigned-integer reads.
  /// @tparam T Integer element type of the values read.
  template <typename T> using ReadUIntsResult = std::vector<T>;
  /// Callback invoked when an asynchronous unsigned-integer read completes.
  /// @tparam T Integer element type of the values read.
  template <typename T>
  using OnReadUIntsCompleteFn =
      unique_function<void(Expected<ReadUIntsResult<T>>)>;

  /// Result type for asynchronous pointer reads.
  using ReadPointersResult = std::vector<ExecutorAddr>;
  /// Callback invoked when an asynchronous pointer read completes.
  using OnReadPointersCompleteFn =
      unique_function<void(Expected<ReadPointersResult>)>;

  /// Result type for asynchronous buffer reads.
  using ReadBuffersResult = std::vector<std::vector<uint8_t>>;
  /// Callback invoked when an asynchronous buffer read completes.
  using OnReadBuffersCompleteFn =
      unique_function<void(Expected<ReadBuffersResult>)>;

  /// Result type for asynchronous null-terminated string reads.
  using ReadStringsResult = std::vector<std::string>;
  /// Callback invoked when an asynchronous string read completes.
  using OnReadStringsCompleteFn =
      unique_function<void(Expected<ReadStringsResult>)>;

  /// Destroys the memory access object.
  virtual ~MemoryAccess();

  /// Asynchronously write uint8 values into executor memory.
  /// @param Ws Writes to perform.
  /// @param OnWriteComplete Callback invoked when the write completes.
  virtual void writeUInt8sAsync(ArrayRef<tpctypes::UInt8Write> Ws,
                                WriteResultFn OnWriteComplete) = 0;

  /// Asynchronously write uint16 values into executor memory.
  /// @param Ws Writes to perform.
  /// @param OnWriteComplete Callback invoked when the write completes.
  virtual void writeUInt16sAsync(ArrayRef<tpctypes::UInt16Write> Ws,
                                 WriteResultFn OnWriteComplete) = 0;

  /// Asynchronously write uint32 values into executor memory.
  /// @param Ws Writes to perform.
  /// @param OnWriteComplete Callback invoked when the write completes.
  virtual void writeUInt32sAsync(ArrayRef<tpctypes::UInt32Write> Ws,
                                 WriteResultFn OnWriteComplete) = 0;

  /// Asynchronously write uint64 values into executor memory.
  /// @param Ws Writes to perform.
  /// @param OnWriteComplete Callback invoked when the write completes.
  virtual void writeUInt64sAsync(ArrayRef<tpctypes::UInt64Write> Ws,
                                 WriteResultFn OnWriteComplete) = 0;

  /// Asynchronously write pointer values into executor memory.
  /// @param Ws Writes to perform.
  /// @param OnWriteComplete Callback invoked when the write completes.
  virtual void writePointersAsync(ArrayRef<tpctypes::PointerWrite> Ws,
                                  WriteResultFn OnWriteComplete) = 0;

  /// Asynchronously write buffer contents into executor memory.
  /// @param Ws Writes to perform.
  /// @param OnWriteComplete Callback invoked when the write completes.
  virtual void writeBuffersAsync(ArrayRef<tpctypes::BufferWrite> Ws,
                                 WriteResultFn OnWriteComplete) = 0;

  /// Asynchronously read uint8 values from executor memory.
  /// @param Rs Addresses to read from.
  /// @param OnComplete Callback invoked with the read results.
  virtual void readUInt8sAsync(ArrayRef<ExecutorAddr> Rs,
                               OnReadUIntsCompleteFn<uint8_t> OnComplete) = 0;

  /// Asynchronously read uint16 values from executor memory.
  /// @param Rs Addresses to read from.
  /// @param OnComplete Callback invoked with the read results.
  virtual void readUInt16sAsync(ArrayRef<ExecutorAddr> Rs,
                                OnReadUIntsCompleteFn<uint16_t> OnComplete) = 0;

  /// Asynchronously read uint32 values from executor memory.
  /// @param Rs Addresses to read from.
  /// @param OnComplete Callback invoked with the read results.
  virtual void readUInt32sAsync(ArrayRef<ExecutorAddr> Rs,
                                OnReadUIntsCompleteFn<uint32_t> OnComplete) = 0;

  /// Asynchronously read uint64 values from executor memory.
  /// @param Rs Addresses to read from.
  /// @param OnComplete Callback invoked with the read results.
  virtual void readUInt64sAsync(ArrayRef<ExecutorAddr> Rs,
                                OnReadUIntsCompleteFn<uint64_t> OnComplete) = 0;

  /// Asynchronously read pointer values from executor memory.
  /// @param Rs Addresses to read from.
  /// @param OnComplete Callback invoked with the read results.
  virtual void readPointersAsync(ArrayRef<ExecutorAddr> Rs,
                                 OnReadPointersCompleteFn OnComplete) = 0;

  /// Asynchronously read buffer ranges from executor memory.
  /// @param Rs Address ranges to read from.
  /// @param OnComplete Callback invoked with the read results.
  virtual void readBuffersAsync(ArrayRef<ExecutorAddrRange> Rs,
                                OnReadBuffersCompleteFn OnComplete) = 0;

  /// Asynchronously read null-terminated strings from executor memory.
  /// @param Rs Addresses of the strings to read.
  /// @param OnComplete Callback invoked with the read results.
  virtual void readStringsAsync(ArrayRef<ExecutorAddr> Rs,
                                OnReadStringsCompleteFn OnComplete) = 0;

  /// Synchronously write uint8 values into executor memory.
  /// @param Ws Writes to perform.
  /// @return Success, or an error if the write fails.
  Error writeUInt8s(ArrayRef<tpctypes::UInt8Write> Ws) {
    std::promise<MSVCPError> ResultP;
    auto ResultF = ResultP.get_future();
    writeUInt8sAsync(Ws, [&](Error Err) { ResultP.set_value(std::move(Err)); });
    return ResultF.get();
  }

  /// Synchronously write uint16 values into executor memory.
  /// @param Ws Writes to perform.
  /// @return Success, or an error if the write fails.
  Error writeUInt16s(ArrayRef<tpctypes::UInt16Write> Ws) {
    std::promise<MSVCPError> ResultP;
    auto ResultF = ResultP.get_future();
    writeUInt16sAsync(Ws,
                      [&](Error Err) { ResultP.set_value(std::move(Err)); });
    return ResultF.get();
  }

  /// Synchronously write uint32 values into executor memory.
  /// @param Ws Writes to perform.
  /// @return Success, or an error if the write fails.
  Error writeUInt32s(ArrayRef<tpctypes::UInt32Write> Ws) {
    std::promise<MSVCPError> ResultP;
    auto ResultF = ResultP.get_future();
    writeUInt32sAsync(Ws,
                      [&](Error Err) { ResultP.set_value(std::move(Err)); });
    return ResultF.get();
  }

  /// Synchronously write uint64 values into executor memory.
  /// @param Ws Writes to perform.
  /// @return Success, or an error if the write fails.
  Error writeUInt64s(ArrayRef<tpctypes::UInt64Write> Ws) {
    std::promise<MSVCPError> ResultP;
    auto ResultF = ResultP.get_future();
    writeUInt64sAsync(Ws,
                      [&](Error Err) { ResultP.set_value(std::move(Err)); });
    return ResultF.get();
  }

  /// Synchronously write pointer values into executor memory.
  /// @param Ws Writes to perform.
  /// @return Success, or an error if the write fails.
  Error writePointers(ArrayRef<tpctypes::PointerWrite> Ws) {
    std::promise<MSVCPError> ResultP;
    auto ResultF = ResultP.get_future();
    writePointersAsync(Ws,
                       [&](Error Err) { ResultP.set_value(std::move(Err)); });
    return ResultF.get();
  }

  /// Synchronously write buffer contents into executor memory.
  /// @param Ws Writes to perform.
  /// @return Success, or an error if the write fails.
  Error writeBuffers(ArrayRef<tpctypes::BufferWrite> Ws) {
    std::promise<MSVCPError> ResultP;
    auto ResultF = ResultP.get_future();
    writeBuffersAsync(Ws,
                      [&](Error Err) { ResultP.set_value(std::move(Err)); });
    return ResultF.get();
  }

  /// Synchronously read uint8 values from executor memory.
  /// @param Rs Addresses to read from.
  /// @return The values read, or an error if the read fails.
  Expected<ReadUIntsResult<uint8_t>> readUInt8s(ArrayRef<ExecutorAddr> Rs) {
    std::promise<MSVCPExpected<ReadUIntsResult<uint8_t>>> P;
    readUInt8sAsync(Rs, [&](Expected<ReadUIntsResult<uint8_t>> Result) {
      P.set_value(std::move(Result));
    });
    return P.get_future().get();
  }

  /// Synchronously read uint16 values from executor memory.
  /// @param Rs Addresses to read from.
  /// @return The values read, or an error if the read fails.
  Expected<ReadUIntsResult<uint16_t>> readUInt16s(ArrayRef<ExecutorAddr> Rs) {
    std::promise<MSVCPExpected<ReadUIntsResult<uint16_t>>> P;
    readUInt16sAsync(Rs, [&](Expected<ReadUIntsResult<uint16_t>> Result) {
      P.set_value(std::move(Result));
    });
    return P.get_future().get();
  }

  /// Synchronously read uint32 values from executor memory.
  /// @param Rs Addresses to read from.
  /// @return The values read, or an error if the read fails.
  Expected<ReadUIntsResult<uint32_t>> readUInt32s(ArrayRef<ExecutorAddr> Rs) {
    std::promise<MSVCPExpected<ReadUIntsResult<uint32_t>>> P;
    readUInt32sAsync(Rs, [&](Expected<ReadUIntsResult<uint32_t>> Result) {
      P.set_value(std::move(Result));
    });
    return P.get_future().get();
  }

  /// Synchronously read uint64 values from executor memory.
  /// @param Rs Addresses to read from.
  /// @return The values read, or an error if the read fails.
  Expected<ReadUIntsResult<uint64_t>> readUInt64s(ArrayRef<ExecutorAddr> Rs) {
    std::promise<MSVCPExpected<ReadUIntsResult<uint64_t>>> P;
    readUInt64sAsync(Rs, [&](Expected<ReadUIntsResult<uint64_t>> Result) {
      P.set_value(std::move(Result));
    });
    return P.get_future().get();
  }

  /// Synchronously read pointer values from executor memory.
  /// @param Rs Addresses to read from.
  /// @return The pointers read, or an error if the read fails.
  Expected<ReadPointersResult> readPointers(ArrayRef<ExecutorAddr> Rs) {
    std::promise<MSVCPExpected<ReadPointersResult>> P;
    readPointersAsync(Rs, [&](Expected<ReadPointersResult> Result) {
      P.set_value(std::move(Result));
    });
    return P.get_future().get();
  }

  /// Synchronously read buffer ranges from executor memory.
  /// @param Rs Address ranges to read from.
  /// @return The buffers read, or an error if the read fails.
  Expected<ReadBuffersResult> readBuffers(ArrayRef<ExecutorAddrRange> Rs) {
    std::promise<MSVCPExpected<ReadBuffersResult>> P;
    readBuffersAsync(Rs, [&](Expected<ReadBuffersResult> Result) {
      P.set_value(std::move(Result));
    });
    return P.get_future().get();
  }

  /// Synchronously read null-terminated strings from executor memory.
  /// @param Rs Addresses of the strings to read.
  /// @return The strings read, or an error if the read fails.
  Expected<ReadStringsResult> readStrings(ArrayRef<ExecutorAddr> Rs) {
    std::promise<MSVCPExpected<ReadStringsResult>> P;
    readStringsAsync(Rs, [&](Expected<ReadStringsResult> Result) {
      P.set_value(std::move(Result));
    });
    return P.get_future().get();
  }
};

} // namespace llvm::orc

#endif // LLVM_EXECUTIONENGINE_ORC_MEMORYACCESS_H
