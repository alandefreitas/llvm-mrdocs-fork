//===--- SimpleRemoteEPCUtils.h - Utils for Simple Remote EPC ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Message definitions and other utilities for SimpleRemoteEPC and
// SimpleRemoteEPCServer.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_SHARED_SIMPLEREMOTEEPCUTILS_H
#define LLVM_EXECUTIONENGINE_ORC_SHARED_SIMPLEREMOTEEPCUTILS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"
#include "llvm/ExecutionEngine/Orc/Shared/SimplePackedSerialization.h"
#include "llvm/ExecutionEngine/Orc/Shared/WrapperFunctionUtils.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace llvm {
namespace orc {

/// Default bootstrap symbol names used by SimpleRemoteEPC.
namespace SimpleRemoteEPCDefaultBootstrapSymbolNames {
/// Name of the executor session object bootstrap symbol.
LLVM_ABI extern const char *ExecutorSessionObjectName;
/// Name of the dispatch function bootstrap symbol.
LLVM_ABI extern const char *DispatchFnName;
} // end namespace SimpleRemoteEPCDefaultBootstrapSymbolNames

/// Opcode values for SimpleRemoteEPC protocol messages.
enum class SimpleRemoteEPCOpcode : uint8_t {
  /// Initial setup message carrying executor info.
  Setup,
  /// Session hangup message carrying a serialized Error payload.
  Hangup,
  /// Result message for a prior CallWrapper request.
  Result,
  /// Request to run a wrapper function in the executor.
  CallWrapper,
  /// Sentinel equal to the last valid opcode.
  LastOpC = CallWrapper
};

/// Executor process information exchanged during SimpleRemoteEPC setup.
struct SimpleRemoteEPCExecutorInfo {
  /// Target triple string for the executor process.
  std::string TargetTriple;
  /// Executor process page size in bytes.
  uint64_t PageSize;
  /// Named bootstrap blobs keyed by symbol name.
  StringMap<std::vector<char>> BootstrapMap;
  /// Named bootstrap symbol addresses keyed by symbol name.
  StringMap<ExecutorAddr> BootstrapSymbols;
};

/// Encode an Error as the payload of a Hangup message.
///
/// A Hangup always carries a serialized Error saying why the session is ending:
/// a success value for an orderly disconnect, otherwise the reason. Both ends
/// of the protocol encode and decode hangups through these two functions, so
/// that their idea of the payload format cannot drift apart.
/// @param Err Error to encode as the hangup reason.
/// @return Serialized hangup payload buffer.
LLVM_ABI shared::WrapperFunctionBuffer encodeHangupPayload(Error Err);

/// Decode a Hangup payload produced by encodeHangupPayload.
///
/// Returns the encoded Error, or an Error describing the payload if it cannot
/// be decoded -- including an empty payload, which is never valid. Both
/// outcomes end the session with an error; they are distinguished only by the
/// message.
/// @param Payload Serialized hangup payload to decode.
/// @return The decoded Error, or an error if the payload cannot be decoded.
LLVM_ABI Error decodeHangupPayload(shared::WrapperFunctionBuffer Payload);

/// Client interface that receives messages from a SimpleRemoteEPCTransport.
class LLVM_ABI SimpleRemoteEPCTransportClient {
public:
  /// Action returned by handleMessage to control the session.
  enum HandleMessageAction {
    /// Keep accepting further messages.
    ContinueSession,
    /// Stop accepting further messages.
    EndSession
  };

  /// Destroy this transport client.
  virtual ~SimpleRemoteEPCTransportClient();

  /// Handle receipt of a message.
  ///
  /// Returns an Error if the message cannot be handled, 'EndSession' if the
  /// client will not accept any further messages, and 'ContinueSession'
  /// otherwise.
  /// @param OpC Opcode of the received message.
  /// @param SeqNo Sequence number of the received message.
  /// @param TagAddr Tag address associated with the message.
  /// @param ArgBytes Serialized argument bytes for the message.
  /// @return ContinueSession, EndSession, or an error if the message cannot be
  /// handled.
  virtual Expected<HandleMessageAction>
  handleMessage(SimpleRemoteEPCOpcode OpC, uint64_t SeqNo, ExecutorAddr TagAddr,
                shared::WrapperFunctionBuffer ArgBytes) = 0;

  /// Handle a disconnection from the underlying transport.
  ///
  /// No further messages should be sent to handleMessage after this is called.
  /// Err may contain an Error value indicating unexpected disconnection. This
  /// allows clients to log such errors, but no attempt should be made at
  /// recovery (which should be handled inside the transport class, if it is
  /// supported at all).
  /// @param Err Error describing an unexpected disconnect, or success.
  virtual void handleDisconnect(Error Err) = 0;
};

/// Transport interface used to exchange SimpleRemoteEPC messages.
class LLVM_ABI SimpleRemoteEPCTransport {
public:
  /// Destroy this transport.
  virtual ~SimpleRemoteEPCTransport();

  /// Called during setup of the client to indicate that the client is ready
  /// to receive messages.
  ///
  /// Transport objects should not access the client until this method is
  /// called.
  /// @return Success, or an error if the transport cannot be started.
  virtual Error start() = 0;

  /// Send a SimpleRemoteEPC message.
  ///
  /// This function may be called concurrently. Subclasses should implement
  /// locking if required for the underlying transport.
  /// @param OpC Opcode of the message to send.
  /// @param SeqNo Sequence number of the message.
  /// @param TagAddr Tag address associated with the message.
  /// @param ArgBytes Serialized argument bytes for the message.
  /// @return Success, or an error if the message cannot be sent.
  virtual Error sendMessage(SimpleRemoteEPCOpcode OpC, uint64_t SeqNo,
                            ExecutorAddr TagAddr, ArrayRef<char> ArgBytes) = 0;

  /// Trigger disconnection from the transport.
  ///
  /// The implementation should respond by calling handleDisconnect on the
  /// client once disconnection is complete. May be called more than once and
  /// from different threads.
  virtual void disconnect() = 0;
};

/// Uses read/write on FileDescriptors for transport.
class LLVM_ABI FDSimpleRemoteEPCTransport : public SimpleRemoteEPCTransport {
public:
  /// Create a FDSimpleRemoteEPCTransport using the given FDs for
  /// reading (InFD) and writing (OutFD).
  /// @param C Transport client that will receive messages.
  /// @param InFD File descriptor used for reading.
  /// @param OutFD File descriptor used for writing.
  /// @return A transport on success, or an error if creation fails.
  static Expected<std::unique_ptr<FDSimpleRemoteEPCTransport>>
  Create(SimpleRemoteEPCTransportClient &C, int InFD, int OutFD);

  /// Create a FDSimpleRemoteEPCTransport using the given FD for both
  /// reading and writing.
  /// @param C Transport client that will receive messages.
  /// @param FD File descriptor used for both reading and writing.
  /// @return A transport on success, or an error if creation fails.
  static Expected<std::unique_ptr<FDSimpleRemoteEPCTransport>>
  Create(SimpleRemoteEPCTransportClient &C, int FD) {
    return Create(C, FD, FD);
  }

  /// Destroy this file-descriptor transport.
  ~FDSimpleRemoteEPCTransport() override;

  /// Start the listener thread for this transport.
  /// @return Success, or an error if the listener cannot be started.
  Error start() override;

  /// Send a SimpleRemoteEPC message over the outbound file descriptor.
  /// @param OpC Opcode of the message to send.
  /// @param SeqNo Sequence number of the message.
  /// @param TagAddr Tag address associated with the message.
  /// @param ArgBytes Serialized argument bytes for the message.
  /// @return Success, or an error if the message cannot be sent.
  Error sendMessage(SimpleRemoteEPCOpcode OpC, uint64_t SeqNo,
                    ExecutorAddr TagAddr, ArrayRef<char> ArgBytes) override;

  /// Disconnect and stop the listener thread for this transport.
  void disconnect() override;

private:
  FDSimpleRemoteEPCTransport(SimpleRemoteEPCTransportClient &C, int InFD,
                             int OutFD)
      : C(C), InFD(InFD), OutFD(OutFD) {}

  Error readBytes(char *Dst, size_t Size, bool *IsEOF = nullptr);
  int writeBytes(const char *Src, size_t Size);
  void listenLoop();

  std::mutex M;
  SimpleRemoteEPCTransportClient &C;
  std::thread ListenerThread;
  int InFD, OutFD;
  std::atomic<bool> Disconnected{false};
};

/// One symbol name and whether it is required in a remote lookup set.
struct RemoteSymbolLookupSetElement {
  /// Symbol name to look up.
  std::string Name;
  /// True if failure to resolve this symbol should fail the lookup.
  bool Required;
};

/// Ordered set of remote symbol lookup elements.
using RemoteSymbolLookupSet = std::vector<RemoteSymbolLookupSetElement>;

/// Remote symbol lookup request identified by a handle.
struct RemoteSymbolLookup {
  /// Handle identifying this lookup request.
  uint64_t H;
  /// Symbols to resolve for this lookup.
  RemoteSymbolLookupSet Symbols;
};

namespace shared {

/// SPS tag type for a RemoteSymbolLookupSetElement.
using SPSRemoteSymbolLookupSetElement = SPSTuple<SPSString, bool>;

/// SPS tag type for a RemoteSymbolLookupSet.
using SPSRemoteSymbolLookupSet = SPSSequence<SPSRemoteSymbolLookupSetElement>;

/// SPS tag type for a RemoteSymbolLookup.
using SPSRemoteSymbolLookup = SPSTuple<uint64_t, SPSRemoteSymbolLookupSet>;

/// Tuple containing target triple, page size, and bootstrap symbols.
using SPSSimpleRemoteEPCExecutorInfo =
    SPSTuple<SPSString, uint64_t,
             SPSSequence<SPSTuple<SPSString, SPSSequence<char>>>,
             SPSSequence<SPSTuple<SPSString, SPSExecutorAddr>>>;

/// SPS serialization traits for RemoteSymbolLookupSetElement.
template <>
class SPSSerializationTraits<SPSRemoteSymbolLookupSetElement,
                             RemoteSymbolLookupSetElement> {
public:
  /// Return the serialized size of \p V.
  /// @param V Element to measure.
  /// @return Number of bytes needed to serialize \p V.
  static size_t size(const RemoteSymbolLookupSetElement &V) {
    return SPSArgList<SPSString, bool>::size(V.Name, V.Required);
  }

  /// Serialize \p V into \p OB.
  /// @param OB Output buffer.
  /// @param V Element to serialize.
  /// @return True on success, false if serialization fails.
  static size_t serialize(SPSOutputBuffer &OB,
                          const RemoteSymbolLookupSetElement &V) {
    return SPSArgList<SPSString, bool>::serialize(OB, V.Name, V.Required);
  }

  /// Deserialize a RemoteSymbolLookupSetElement from \p IB into \p V.
  /// @param IB Input buffer.
  /// @param V Destination element.
  /// @return True on success, false if deserialization fails.
  static size_t deserialize(SPSInputBuffer &IB,
                            RemoteSymbolLookupSetElement &V) {
    return SPSArgList<SPSString, bool>::deserialize(IB, V.Name, V.Required);
  }
};

/// SPS serialization traits for RemoteSymbolLookup.
template <>
class SPSSerializationTraits<SPSRemoteSymbolLookup, RemoteSymbolLookup> {
public:
  /// Return the serialized size of \p V.
  /// @param V Lookup to measure.
  /// @return Number of bytes needed to serialize \p V.
  static size_t size(const RemoteSymbolLookup &V) {
    return SPSArgList<uint64_t, SPSRemoteSymbolLookupSet>::size(V.H, V.Symbols);
  }

  /// Serialize \p V into \p OB.
  /// @param OB Output buffer.
  /// @param V Lookup to serialize.
  /// @return True on success, false if serialization fails.
  static size_t serialize(SPSOutputBuffer &OB, const RemoteSymbolLookup &V) {
    return SPSArgList<uint64_t, SPSRemoteSymbolLookupSet>::serialize(OB, V.H,
                                                                     V.Symbols);
  }

  /// Deserialize a RemoteSymbolLookup from \p IB into \p V.
  /// @param IB Input buffer.
  /// @param V Destination lookup.
  /// @return True on success, false if deserialization fails.
  static size_t deserialize(SPSInputBuffer &IB, RemoteSymbolLookup &V) {
    return SPSArgList<uint64_t, SPSRemoteSymbolLookupSet>::deserialize(
        IB, V.H, V.Symbols);
  }
};

/// SPS serialization traits for SimpleRemoteEPCExecutorInfo.
template <>
class SPSSerializationTraits<SPSSimpleRemoteEPCExecutorInfo,
                             SimpleRemoteEPCExecutorInfo> {
public:
  /// Return the serialized size of \p SI.
  /// @param SI Executor info to measure.
  /// @return Number of bytes needed to serialize \p SI.
  static size_t size(const SimpleRemoteEPCExecutorInfo &SI) {
    return SPSSimpleRemoteEPCExecutorInfo::AsArgList ::size(
        SI.TargetTriple, SI.PageSize, SI.BootstrapMap, SI.BootstrapSymbols);
  }

  /// Serialize \p SI into \p OB.
  /// @param OB Output buffer.
  /// @param SI Executor info to serialize.
  /// @return True on success, false if serialization fails.
  static bool serialize(SPSOutputBuffer &OB,
                        const SimpleRemoteEPCExecutorInfo &SI) {
    return SPSSimpleRemoteEPCExecutorInfo::AsArgList ::serialize(
        OB, SI.TargetTriple, SI.PageSize, SI.BootstrapMap, SI.BootstrapSymbols);
  }

  /// Deserialize a SimpleRemoteEPCExecutorInfo from \p IB into \p SI.
  /// @param IB Input buffer.
  /// @param SI Destination executor info.
  /// @return True on success, false if deserialization fails.
  static bool deserialize(SPSInputBuffer &IB, SimpleRemoteEPCExecutorInfo &SI) {
    return SPSSimpleRemoteEPCExecutorInfo::AsArgList ::deserialize(
        IB, SI.TargetTriple, SI.PageSize, SI.BootstrapMap, SI.BootstrapSymbols);
  }
};

/// SPS signature for loading a dylib in the remote executor.
using SPSLoadDylibSignature = SPSExpected<SPSExecutorAddr>(SPSExecutorAddr,
                                                           SPSString, uint64_t);

/// SPS signature for looking up symbols in the remote executor.
using SPSLookupSymbolsSignature =
    SPSExpected<SPSSequence<SPSSequence<SPSExecutorAddr>>>(
        SPSExecutorAddr, SPSSequence<SPSRemoteSymbolLookup>);

} // end namespace shared
} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_SHARED_SIMPLEREMOTEEPCUTILS_H
