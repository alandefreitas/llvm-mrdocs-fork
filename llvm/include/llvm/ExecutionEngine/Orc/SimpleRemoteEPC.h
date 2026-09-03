//===---- SimpleRemoteEPC.h - Simple remote executor control ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Simple remote executor process control.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_SIMPLEREMOTEEPC_H
#define LLVM_EXECUTIONENGINE_ORC_SIMPLEREMOTEEPC_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/FunctionExtras.h"
#include "llvm/ExecutionEngine/Orc/ExecutorProcessControl.h"
#include "llvm/ExecutionEngine/Orc/Shared/SimpleRemoteEPCUtils.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MSVCErrorWorkarounds.h"

#include <future>

namespace llvm {
namespace orc {

/// An ExecutorProcessControl implementation for a remote executor over a
/// SimpleRemoteEPCTransport.
class LLVM_ABI SimpleRemoteEPC : public ExecutorProcessControl,
                                 public SimpleRemoteEPCTransportClient {
public:
  /// Create a SimpleRemoteEPC using the given transport type and args.
  /// @param D Task dispatcher for the new instance.
  /// @param TransportTCtorArgs Arguments forwarded to TransportT::Create.
  /// @return A new SimpleRemoteEPC on success, or an error on failure.
  template <typename TransportT, typename... TransportTCtorArgTs>
  static Expected<std::unique_ptr<SimpleRemoteEPC>>
  Create(std::unique_ptr<TaskDispatcher> D,
         TransportTCtorArgTs &&...TransportTCtorArgs) {
    std::unique_ptr<SimpleRemoteEPC> SREPC(
        new SimpleRemoteEPC(std::make_shared<SymbolStringPool>(),
                            std::move(D)));
    auto T = TransportT::Create(
        *SREPC, std::forward<TransportTCtorArgTs>(TransportTCtorArgs)...);
    if (!T)
      return T.takeError();
    SREPC->T = std::move(*T);
    if (auto Err = SREPC->setup())
      return joinErrors(std::move(Err), SREPC->disconnect());
    return std::move(SREPC);
  }

  /// SimpleRemoteEPC is not copy-constructible.
  /// @param Other Instance that would be copied.
  SimpleRemoteEPC(const SimpleRemoteEPC &Other) = delete;
  /// SimpleRemoteEPC is not copy-assignable.
  /// @param Other Instance that would be copied.
  SimpleRemoteEPC &operator=(const SimpleRemoteEPC &Other) = delete;
  /// SimpleRemoteEPC is not move-constructible.
  /// @param Other Instance that would be moved.
  SimpleRemoteEPC(SimpleRemoteEPC &&Other) = delete;
  /// SimpleRemoteEPC is not move-assignable.
  /// @param Other Instance that would be moved.
  SimpleRemoteEPC &operator=(SimpleRemoteEPC &&Other) = delete;
  /// Destroy this SimpleRemoteEPC.
  ~SimpleRemoteEPC() override;

  /// Run a function with a main-like signature in the remote executor.
  /// @param MainFnAddr Address of the main-like function to run.
  /// @param Args Arguments to pass to the main-like function.
  /// @return Integer result of the main-like function, or an error on failure.
  Expected<int32_t> runAsMain(ExecutorAddr MainFnAddr,
                              ArrayRef<std::string> Args) override;

  /// Run a wrapper function asynchronously in the remote executor.
  /// @param WrapperFnAddr Address of the wrapper function to call.
  /// @param OnComplete Handler invoked with the wrapper function result.
  /// @param ArgBuffer Serialized argument bytes for the wrapper function.
  void callWrapperAsync(ExecutorAddr WrapperFnAddr,
                        IncomingWFRHandler OnComplete,
                        ArrayRef<char> ArgBuffer) override;

  /// Create a default JITLinkMemoryManager for the remote executor.
  /// @return Default JITLink memory manager, or an error on failure.
  Expected<std::unique_ptr<jitlink::JITLinkMemoryManager>>
  createDefaultMemoryManager() override;

  /// Create a default DylibManager for the remote executor.
  /// @return Default DylibManager for the remote executor, or an error on
  /// failure.
  Expected<std::unique_ptr<DylibManager>> createDefaultDylibMgr() override;

  /// Create a default MemoryAccess for the remote executor.
  /// @return Default MemoryAccess for the remote executor, or an error on
  /// failure.
  Expected<std::unique_ptr<MemoryAccess>> createDefaultMemoryAccess() override;

  /// Disconnect from the remote executor transport.
  /// @return Success, or an error if disconnection fails.
  Error disconnect() override;

  /// Handle receipt of a message from the remote executor transport.
  /// @param OpC Opcode of the received message.
  /// @param SeqNo Sequence number of the received message.
  /// @param TagAddr Tag address associated with the message.
  /// @param ArgBytes Serialized argument bytes for the message.
  /// @return ContinueSession, EndSession, or an error if the message cannot be
  /// handled.
  Expected<HandleMessageAction>
  handleMessage(SimpleRemoteEPCOpcode OpC, uint64_t SeqNo, ExecutorAddr TagAddr,
                shared::WrapperFunctionBuffer ArgBytes) override;

  /// Handle a disconnection from the underlying transport.
  /// @param Err Error describing an unexpected disconnect, or success.
  void handleDisconnect(Error Err) override;

private:
  SimpleRemoteEPC(std::shared_ptr<SymbolStringPool> SSP,
                  std::unique_ptr<TaskDispatcher> D)
      : ExecutorProcessControl(std::move(SSP), std::move(D)) {}

  Error sendMessage(SimpleRemoteEPCOpcode OpC, uint64_t SeqNo,
                    ExecutorAddr TagAddr, ArrayRef<char> ArgBytes);

  Error handleSetup(uint64_t SeqNo, ExecutorAddr TagAddr,
                    shared::WrapperFunctionBuffer ArgBytes);
  Error setup();

  Error handleResult(uint64_t SeqNo, ExecutorAddr TagAddr,
                     shared::WrapperFunctionBuffer ArgBytes);
  void handleCallWrapper(uint64_t RemoteSeqNo, ExecutorAddr TagAddr,
                         shared::WrapperFunctionBuffer ArgBytes);
  Error handleHangup(shared::WrapperFunctionBuffer ArgBytes);

  uint64_t getNextSeqNo() { return NextSeqNo++; }
  void releaseSeqNo(uint64_t SeqNo) {}

  using PendingCallWrapperResultsMap =
    DenseMap<uint64_t, IncomingWFRHandler>;

  std::mutex SimpleRemoteEPCMutex;
  std::condition_variable DisconnectCV;
  bool Disconnected = false;

  // Whether either side announced the end of the session. If the transport
  // reports a disconnection and neither of these is set then the executor went
  // away without saying so, which is reported as an error: see
  // handleDisconnect.
  //
  // LocalHangup has to be shared state: disconnect() sets it on the calling
  // thread, while handleDisconnect reads it on the transport's listener thread.
  //
  // RemoteHangup is shared state only because the read loop lives in the
  // transport: the hangup is observed in handleMessage but needed in
  // handleDisconnect, and the two are separate entry points on the
  // SimpleRemoteEPCTransportClient interface with no call edge between them.
  //
  // TODO: Once the read loop is reshaped into a reactor (mirroring
  // FDSimpleRemoteCA in the ORC runtime), RemoteHangup should fold into a stop
  // reason returned from it, leaving only LocalHangup as state -- as
  // FDSimpleRemoteCA does with ShutdownRequested.
  bool LocalHangup = false;
  bool RemoteHangup = false;

  Error DisconnectErr = Error::success();

  std::unique_ptr<SimpleRemoteEPCTransport> T;

  ExecutorAddr RunAsMainAddr;

  uint64_t NextSeqNo = 0;
  PendingCallWrapperResultsMap PendingCallWrapperResults;
};

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_SIMPLEREMOTEEPC_H
