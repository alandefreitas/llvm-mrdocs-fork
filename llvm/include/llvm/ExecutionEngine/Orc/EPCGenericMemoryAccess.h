//===- EPCGenericMemoryAccess.h - Generic EPC MemoryAccess impl -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements the MemoryAccess interface by calling executor-side wrapper
// functions through Proxy objects.
//
// This simplifies the implementaton of new ExecutorProcessControl instances,
// as this implementation will always work (at the cost of some performance
// overhead for the calls).
//
// This header is protocol-agnostic. To build an instance that targets the ORC
// runtime's SPS controller interface, see EPCGenericMemoryAccessSPS.h.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_EPCGENERICMEMORYACCESS_H
#define LLVM_EXECUTIONENGINE_ORC_EPCGENERICMEMORYACCESS_H

#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/MemoryAccess.h"
#include "llvm/ExecutionEngine/Orc/Proxy.h"
#include "llvm/ExecutionEngine/Orc/Shared/TargetProcessControlTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace llvm {
namespace orc {

/// MemoryAccess implementation that calls executor-side wrappers via Proxies.
class EPCGenericMemoryAccess : public MemoryAccess {
public:
  /// Proxy for writing uint8 values in the executor.
  using WriteUInt8sProxy = Proxy<void(ArrayRef<tpctypes::UInt8Write>)>;
  /// Proxy for writing uint16 values in the executor.
  using WriteUInt16sProxy = Proxy<void(ArrayRef<tpctypes::UInt16Write>)>;
  /// Proxy for writing uint32 values in the executor.
  using WriteUInt32sProxy = Proxy<void(ArrayRef<tpctypes::UInt32Write>)>;
  /// Proxy for writing uint64 values in the executor.
  using WriteUInt64sProxy = Proxy<void(ArrayRef<tpctypes::UInt64Write>)>;
  /// Proxy for writing pointer values in the executor.
  using WritePointersProxy = Proxy<void(ArrayRef<tpctypes::PointerWrite>)>;
  /// Proxy for writing buffer contents in the executor.
  using WriteBuffersProxy = Proxy<void(ArrayRef<tpctypes::BufferWrite>)>;
  /// Proxy for reading uint8 values from the executor.
  using ReadUInt8sProxy = Proxy<std::vector<uint8_t>(ArrayRef<ExecutorAddr>)>;
  /// Proxy for reading uint16 values from the executor.
  using ReadUInt16sProxy = Proxy<std::vector<uint16_t>(ArrayRef<ExecutorAddr>)>;
  /// Proxy for reading uint32 values from the executor.
  using ReadUInt32sProxy = Proxy<std::vector<uint32_t>(ArrayRef<ExecutorAddr>)>;
  /// Proxy for reading uint64 values from the executor.
  using ReadUInt64sProxy = Proxy<std::vector<uint64_t>(ArrayRef<ExecutorAddr>)>;
  /// Proxy for reading pointer values from the executor.
  using ReadPointersProxy =
      Proxy<std::vector<ExecutorAddr>(ArrayRef<ExecutorAddr>)>;
  /// Proxy for reading buffer ranges from the executor.
  using ReadBuffersProxy =
      Proxy<std::vector<std::vector<uint8_t>>(ArrayRef<ExecutorAddrRange>)>;
  /// Proxy for reading null-terminated strings from the executor.
  using ReadStringsProxy =
      Proxy<std::vector<std::string>(ArrayRef<ExecutorAddr>)>;

  /// Proxies for the executor-side memory-access functions.
  ///
  /// These are protocol-agnostic: sps::createEPCGenericMemoryAccess populates
  /// them for the runtime's SPS controller interface, but a client targeting a
  /// different protocol can build its own Funcs and pass them to the
  /// constructor.
  struct Funcs {
    /// Proxy that writes uint8 values in the executor.
    WriteUInt8sProxy WriteUInt8s;
    /// Proxy that writes uint16 values in the executor.
    WriteUInt16sProxy WriteUInt16s;
    /// Proxy that writes uint32 values in the executor.
    WriteUInt32sProxy WriteUInt32s;
    /// Proxy that writes uint64 values in the executor.
    WriteUInt64sProxy WriteUInt64s;
    /// Proxy that writes pointer values in the executor.
    WritePointersProxy WritePointers;
    /// Proxy that writes buffer contents in the executor.
    WriteBuffersProxy WriteBuffers;
    /// Proxy that reads uint8 values from the executor.
    ReadUInt8sProxy ReadUInt8s;
    /// Proxy that reads uint16 values from the executor.
    ReadUInt16sProxy ReadUInt16s;
    /// Proxy that reads uint32 values from the executor.
    ReadUInt32sProxy ReadUInt32s;
    /// Proxy that reads uint64 values from the executor.
    ReadUInt64sProxy ReadUInt64s;
    /// Proxy that reads pointer values from the executor.
    ReadPointersProxy ReadPointers;
    /// Proxy that reads buffer ranges from the executor.
    ReadBuffersProxy ReadBuffers;
    /// Proxy that reads null-terminated strings from the executor.
    ReadStringsProxy ReadStrings;
  };

  /// Create an EPCGenericMemoryAccess instance from a given set of memory
  /// access proxies.
  /// @param ES Execution session used to issue memory-access calls.
  /// @param Fns Proxies for the executor-side memory-access functions.
  EPCGenericMemoryAccess(ExecutionSession &ES, Funcs Fns)
      : ES(ES), Fns(std::move(Fns)) {}

  /// Asynchronously write uint8 values into executor memory.
  /// @param Ws Writes to perform.
  /// @param OnWriteComplete Callback invoked when the write completes.
  void writeUInt8sAsync(ArrayRef<tpctypes::UInt8Write> Ws,
                        WriteResultFn OnWriteComplete) override {
    Fns.WriteUInt8s(std::move(OnWriteComplete), ES, Ws);
  }

  /// Asynchronously write uint16 values into executor memory.
  /// @param Ws Writes to perform.
  /// @param OnWriteComplete Callback invoked when the write completes.
  void writeUInt16sAsync(ArrayRef<tpctypes::UInt16Write> Ws,
                         WriteResultFn OnWriteComplete) override {
    Fns.WriteUInt16s(std::move(OnWriteComplete), ES, Ws);
  }

  /// Asynchronously write uint32 values into executor memory.
  /// @param Ws Writes to perform.
  /// @param OnWriteComplete Callback invoked when the write completes.
  void writeUInt32sAsync(ArrayRef<tpctypes::UInt32Write> Ws,
                         WriteResultFn OnWriteComplete) override {
    Fns.WriteUInt32s(std::move(OnWriteComplete), ES, Ws);
  }

  /// Asynchronously write uint64 values into executor memory.
  /// @param Ws Writes to perform.
  /// @param OnWriteComplete Callback invoked when the write completes.
  void writeUInt64sAsync(ArrayRef<tpctypes::UInt64Write> Ws,
                         WriteResultFn OnWriteComplete) override {
    Fns.WriteUInt64s(std::move(OnWriteComplete), ES, Ws);
  }

  /// Asynchronously write pointer values into executor memory.
  /// @param Ws Writes to perform.
  /// @param OnWriteComplete Callback invoked when the write completes.
  void writePointersAsync(ArrayRef<tpctypes::PointerWrite> Ws,
                          WriteResultFn OnWriteComplete) override {
    Fns.WritePointers(std::move(OnWriteComplete), ES, Ws);
  }

  /// Asynchronously write buffer contents into executor memory.
  /// @param Ws Writes to perform.
  /// @param OnWriteComplete Callback invoked when the write completes.
  void writeBuffersAsync(ArrayRef<tpctypes::BufferWrite> Ws,
                         WriteResultFn OnWriteComplete) override {
    Fns.WriteBuffers(std::move(OnWriteComplete), ES, Ws);
  }

  /// Asynchronously read uint8 values from executor memory.
  /// @param Rs Addresses to read from.
  /// @param OnComplete Callback invoked with the read results.
  void readUInt8sAsync(ArrayRef<ExecutorAddr> Rs,
                       OnReadUIntsCompleteFn<uint8_t> OnComplete) override {
    Fns.ReadUInt8s(std::move(OnComplete), ES, Rs);
  }

  /// Asynchronously read uint16 values from executor memory.
  /// @param Rs Addresses to read from.
  /// @param OnComplete Callback invoked with the read results.
  void readUInt16sAsync(ArrayRef<ExecutorAddr> Rs,
                        OnReadUIntsCompleteFn<uint16_t> OnComplete) override {
    Fns.ReadUInt16s(std::move(OnComplete), ES, Rs);
  }

  /// Asynchronously read uint32 values from executor memory.
  /// @param Rs Addresses to read from.
  /// @param OnComplete Callback invoked with the read results.
  void readUInt32sAsync(ArrayRef<ExecutorAddr> Rs,
                        OnReadUIntsCompleteFn<uint32_t> OnComplete) override {
    Fns.ReadUInt32s(std::move(OnComplete), ES, Rs);
  }

  /// Asynchronously read uint64 values from executor memory.
  /// @param Rs Addresses to read from.
  /// @param OnComplete Callback invoked with the read results.
  void readUInt64sAsync(ArrayRef<ExecutorAddr> Rs,
                        OnReadUIntsCompleteFn<uint64_t> OnComplete) override {
    Fns.ReadUInt64s(std::move(OnComplete), ES, Rs);
  }

  /// Asynchronously read pointer values from executor memory.
  /// @param Rs Addresses to read from.
  /// @param OnComplete Callback invoked with the read results.
  void readPointersAsync(ArrayRef<ExecutorAddr> Rs,
                         OnReadPointersCompleteFn OnComplete) override {
    Fns.ReadPointers(std::move(OnComplete), ES, Rs);
  }

  /// Asynchronously read buffer ranges from executor memory.
  /// @param Rs Address ranges to read from.
  /// @param OnComplete Callback invoked with the read results.
  void readBuffersAsync(ArrayRef<ExecutorAddrRange> Rs,
                        OnReadBuffersCompleteFn OnComplete) override {
    Fns.ReadBuffers(std::move(OnComplete), ES, Rs);
  }

  /// Asynchronously read null-terminated strings from executor memory.
  /// @param Rs Addresses of the strings to read.
  /// @param OnComplete Callback invoked with the read results.
  void readStringsAsync(ArrayRef<ExecutorAddr> Rs,
                        OnReadStringsCompleteFn OnComplete) override {
    Fns.ReadStrings(std::move(OnComplete), ES, Rs);
  }

private:
  ExecutionSession &ES;
  Funcs Fns;
};

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_EPCGENERICMEMORYACCESS_H
