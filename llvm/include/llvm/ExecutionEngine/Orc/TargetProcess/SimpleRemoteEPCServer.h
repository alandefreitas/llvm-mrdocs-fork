//===---- SimpleRemoteEPCServer.h - EPC over abstract channel ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// EPC over simple abstract channel.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_SIMPLEREMOTEEPCSERVER_H
#define LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_SIMPLEREMOTEEPCSERVER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/FunctionExtras.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/ExecutionEngine/Orc/Shared/SimpleRemoteEPCUtils.h"
#include "llvm/ExecutionEngine/Orc/Shared/TargetProcessControlTypes.h"
#include "llvm/ExecutionEngine/Orc/Shared/WrapperFunctionUtils.h"
#include "llvm/ExecutionEngine/Orc/TargetProcess/ExecutorBootstrapService.h"
#include "llvm/ExecutionEngine/Orc/TargetProcess/SimpleExecutorDylibManager.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/Error.h"

#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>

namespace llvm {
namespace orc {

/// A simple EPC server implementation.
class LLVM_ABI SimpleRemoteEPCServer : public SimpleRemoteEPCTransportClient {
public:
  /// Callback type used to report errors from this server.
  using ReportErrorFunction = unique_function<void(Error)>;

  /// Dispatches calls to runWrapper.
  class LLVM_ABI Dispatcher {
  public:
    /// Destroy this dispatcher.
    virtual ~Dispatcher();
    /// Dispatch work to be run by this dispatcher.
    /// @param Work Callable to run.
    virtual void dispatch(unique_function<void()> Work) = 0;
    /// Shut down this dispatcher and wait for outstanding work to finish.
    virtual void shutdown() = 0;
  };

#if LLVM_ENABLE_THREADS
  /// Dispatcher that runs work on a pool of background threads.
  class LLVM_ABI ThreadDispatcher : public Dispatcher {
  public:
    /// Dispatch work onto a background thread.
    /// @param Work Callable to run.
    void dispatch(unique_function<void()> Work) override;
    /// Shut down the thread dispatcher and wait for outstanding work.
    void shutdown() override;

  private:
    std::mutex DispatchMutex;
    bool Running = true;
    size_t Outstanding = 0;
    std::condition_variable OutstandingCV;
  };
#endif

  /// Configuration helper used while constructing a SimpleRemoteEPCServer.
  class Setup {
    friend class SimpleRemoteEPCServer;

  public:
    /// Return the server instance being configured.
    /// @return Reference to the server under construction.
    SimpleRemoteEPCServer &server() { return S; }
    /// Return the mutable bootstrap map of named serialized values.
    /// @return Mutable map of bootstrap keys to serialized values.
    StringMap<std::vector<char>> &bootstrapMap() { return BootstrapMap; }
    /// Serialize \p Value under SPS tag \p SPSTagT and store it at \p Key.
    /// @param Key Bootstrap map key under which to store the value.
    /// @param Value Value to serialize into the bootstrap map.
    template <typename T, typename SPSTagT>
    void setBootstrapMapValue(std::string Key, const T &Value) {
      std::vector<char> Buffer;
      Buffer.resize(shared::SPSArgList<SPSTagT>::size(Value));
      shared::SPSOutputBuffer OB(Buffer.data(), Buffer.size());
      bool Success = shared::SPSArgList<SPSTagT>::serialize(OB, Value);
      (void)Success;
      assert(Success && "Bootstrap map value serialization failed");
      BootstrapMap[std::move(Key)] = std::move(Buffer);
    }
    /// Return the mutable map of named bootstrap symbol addresses.
    /// @return Mutable map of bootstrap symbol names to addresses.
    StringMap<ExecutorAddr> &bootstrapSymbols() { return BootstrapSymbols; }
    /// Return the mutable list of executor bootstrap services to install.
    /// @return Mutable list of bootstrap services to install.
    std::vector<std::unique_ptr<ExecutorBootstrapService>> &services() {
      return Services;
    }
    /// Install the dispatcher used to run wrapper-function work.
    /// @param D Dispatcher to take ownership of.
    void setDispatcher(std::unique_ptr<Dispatcher> D) { S.D = std::move(D); }
    /// Install the error reporter used by the server under construction.
    /// @param ReportError Callback invoked to report server errors.
    void setErrorReporter(unique_function<void(Error)> ReportError) {
      S.ReportError = std::move(ReportError);
    }

  private:
    Setup(SimpleRemoteEPCServer &S) : S(S) {}
    SimpleRemoteEPCServer &S;
    StringMap<std::vector<char>> BootstrapMap;
    StringMap<ExecutorAddr> BootstrapSymbols;
    std::vector<std::unique_ptr<ExecutorBootstrapService>> Services;
  };

  /// Return the default bootstrap symbol map for a SimpleRemoteEPCServer.
  /// @return Default map of bootstrap symbol names to addresses.
  static StringMap<ExecutorAddr> defaultBootstrapSymbols();

  /// Create a SimpleRemoteEPCServer with the given setup and transport.
  /// @param SetupFunction Callback that configures the server before start.
  /// @param TransportTCtorArgs Constructor arguments forwarded to TransportT.
  /// @return A new server on success, or an error on failure.
  template <typename TransportT, typename... TransportTCtorArgTs>
  static Expected<std::unique_ptr<SimpleRemoteEPCServer>>
  Create(unique_function<Error(Setup &S)> SetupFunction,
         TransportTCtorArgTs &&...TransportTCtorArgs) {
    auto Server = std::make_unique<SimpleRemoteEPCServer>();
    Setup S(*Server);
    if (auto Err = SetupFunction(S))
      return std::move(Err);

    // Set ReportError up-front so that it can be used if construction
    // process fails.
    if (!Server->ReportError)
      Server->ReportError = [](Error Err) {
        logAllUnhandledErrors(std::move(Err), errs(), "SimpleRemoteEPCServer ");
      };

    // Attempt to create transport.
    auto T = TransportT::Create(
        *Server, std::forward<TransportTCtorArgTs>(TransportTCtorArgs)...);
    if (!T)
      return T.takeError();
    Server->T = std::move(*T);
    if (auto Err = Server->T->start())
      return std::move(Err);

    // If transport creation succeeds then start up services.
    Server->Services = std::move(S.services());
    Server->Services.push_back(
        std::make_unique<rt_bootstrap::SimpleExecutorDylibManager>());
    for (auto &Service : Server->Services)
      Service->addBootstrapSymbols(S.bootstrapSymbols());

    if (auto Err = Server->sendSetupMessage(std::move(S.BootstrapMap),
                                            std::move(S.BootstrapSymbols)))
      return std::move(Err);
    return std::move(Server);
  }

  /// Set an error reporter for this server.
  /// @param ReportError Callback invoked to report server errors.
  void setErrorReporter(ReportErrorFunction ReportError) {
    this->ReportError = std::move(ReportError);
  }

  /// Call to handle an incoming message.
  ///
  /// Returns 'Disconnect' if the message is a 'detach' message from the remote
  /// otherwise returns 'Continue'. If the server has moved to an error state,
  /// returns an error, which should be reported and treated as a 'Disconnect'.
  /// @param OpC Opcode of the received message.
  /// @param SeqNo Sequence number of the received message.
  /// @param TagAddr Tag address associated with the message.
  /// @param ArgBytes Serialized argument bytes for the message.
  /// @return Continue, Disconnect, or an error if the server is in an error
  /// state.
  Expected<HandleMessageAction>
  handleMessage(SimpleRemoteEPCOpcode OpC, uint64_t SeqNo, ExecutorAddr TagAddr,
                shared::WrapperFunctionBuffer ArgBytes) override;

  /// Block until the server has fully disconnected.
  /// @return Success, or an error describing a failed disconnect.
  Error waitForDisconnect();

  /// Handle a disconnection from the underlying transport.
  /// @param Err Error describing an unexpected disconnect, or success.
  void handleDisconnect(Error Err) override;

private:
  Error sendMessage(SimpleRemoteEPCOpcode OpC, uint64_t SeqNo,
                    ExecutorAddr TagAddr, ArrayRef<char> ArgBytes);

  Error sendSetupMessage(StringMap<std::vector<char>> BootstrapMap,
                         StringMap<ExecutorAddr> BootstrapSymbols);

  Error handleResult(uint64_t SeqNo, ExecutorAddr TagAddr,
                     shared::WrapperFunctionBuffer ArgBytes);
  void handleCallWrapper(uint64_t RemoteSeqNo, ExecutorAddr TagAddr,
                         shared::WrapperFunctionBuffer ArgBytes);

  shared::WrapperFunctionBuffer
  doJITDispatch(const void *FnTag, const char *ArgData, size_t ArgSize);

  static shared::CWrapperFunctionBuffer jitDispatchEntry(void *DispatchCtx,
                                                         const void *FnTag,
                                                         const char *ArgData,
                                                         size_t ArgSize);

  uint64_t getNextSeqNo() { return NextSeqNo++; }
  void releaseSeqNo(uint64_t) {}

  using PendingJITDispatchResultsMap =
      DenseMap<uint64_t, std::promise<shared::WrapperFunctionBuffer> *>;

  std::mutex ServerStateMutex;
  std::condition_variable ShutdownCV;
  enum { ServerRunning, ServerShuttingDown, ServerShutDown } RunState;

  // Whether the controller announced the end of the session. The server never
  // initiates a disconnection, so a transport disconnection without this is the
  // controller going away unexpectedly: see handleDisconnect.
  bool RemoteHangup = false;

  Error ShutdownErr = Error::success();
  std::unique_ptr<SimpleRemoteEPCTransport> T;
  std::unique_ptr<Dispatcher> D;
  std::vector<std::unique_ptr<ExecutorBootstrapService>> Services;
  ReportErrorFunction ReportError;

  uint64_t NextSeqNo = 0;
  PendingJITDispatchResultsMap PendingJITDispatchResults;
};

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_SIMPLEREMOTEEPCSERVER_H
