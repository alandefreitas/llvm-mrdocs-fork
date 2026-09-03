//===---- InProcessEPC.h - In-process EPC for new ORC runtime ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// ExecutorProcessControl implementation for in-process JITs that use the new
// ORC runtime (orc-rt). Interfaces with orc_rt::InProcessControllerAccess via
// direct function calls.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_INPROCESSEPC_H
#define LLVM_EXECUTIONENGINE_ORC_INPROCESSEPC_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ExecutionEngine/Orc/ExecutorProcessControl.h"
#include "llvm/ExecutionEngine/Orc/InProcessMemoryAccess.h"
#include "llvm/ExecutionEngine/Orc/Shared/WrapperFunctionUtils.h"
#include "llvm/Support/Compiler.h"

#include <memory>
#include <mutex>

namespace llvm::orc {

/// An ExecutorProcessControl implementation for in-process JITs that use the
/// new ORC runtime (llvm-project/orc-rt).
///
/// This class communicates with the runtime's InProcessControllerAccess via
/// direct function calls through a virtual connection object.
class LLVM_ABI InProcessEPC : public ExecutorProcessControl {
public:
  /// Pseudo-connection C struct for InProcessEPC communication.
  ///
  /// Used to facilitate calls between InProcessEPC and
  /// InProcessControllerAccess without relying on anything but C ABI. Must be
  /// kept in-sync with the corresponding struct in
  /// orc_rt::InProcessControllerAccess.
  struct Connection {
    /// Retain a reference to this connection.
    void (*Retain)(Connection *C) = nullptr;
    /// Release a reference to this connection.
    void (*Release)(Connection *C) = nullptr;
    /// Disconnect this connection.
    void (*Disconnect)(Connection *C) = nullptr;
    /// Enter a message-handling scope on this connection.
    int (*EnterMessageScope)(Connection *C) = nullptr;
    /// Leave a message-handling scope on this connection.
    void (*LeaveMessageScope)(Connection *C) = nullptr;

    /// Accessors to be set by the InProcessEPC instance.
    void *IPEPC = nullptr;
    /// Call the JIT-dispatch handler in the InProcessEPC instance.
    void (*CallJITDispatch)(void *IPEPC, uint64_t CallId, void *HandlerTag,
                            shared::CWrapperFunctionBuffer ArgBytes) = nullptr;
    /// Deliver a wrapper-function result to the InProcessEPC instance.
    void (*ReturnWrapperResult)(void *IPEPC, uint64_t CallId,
                                shared::CWrapperFunctionBuffer ResultBytes) =
        nullptr;

    /// Accessors to be set by the InProcessControllerAccess instance.
    void *IPCA = nullptr;
    /// Call a wrapper function via the InProcessControllerAccess instance.
    void (*CallWrapper)(void *IPCA, uint64_t CallId, void *Fn,
                        shared::CWrapperFunctionBuffer ArgBytes) = nullptr;
    /// Deliver a JIT-dispatch result to the InProcessControllerAccess instance.
    void (*ReturnJITDispatchResult)(
        void *IPCA, uint64_t CallId,
        shared::CWrapperFunctionBuffer ResultBytes) = nullptr;
  };

  /// Provides access to bootstrap info.
  /// Must be kept in-sync with the corresponding struct in
  /// orc_rt::InProcessControllerAccess.
  struct BootstrapInfoAccess {
    /// Return the executor process page size.
    uint64_t (*GetPageSize)(void *BIA) = nullptr;
    /// Return the executor process target triple.
    const char *(*GetTargetTriple)(void *BIA) = nullptr;

    /// Fetch the next bootstrap map value, or indicate completion.
    int (*GetNextValue)(void *BIA, const char **Name, const char **ValueBytes,
                        uint64_t *ValueSize) = nullptr;
    /// Fetch the next bootstrap symbol address, or indicate completion.
    int (*GetNextSymbol)(void *BIA, const char **Name,
                         uint64_t *Addr) = nullptr;
  };

  /// Create a new InProcessEPC.
  ///
  /// If no symbol string pool is given then one will be created.
  /// If no task dispatcher is given an InPlaceTaskDispatcher will be used.
  /// @param C Connection to the in-process ORC runtime controller.
  /// @param BIA Accessors for executor bootstrap information.
  /// @param SSP Optional symbol string pool; created if null.
  /// @param D Optional task dispatcher; defaults to InPlaceTaskDispatcher.
  /// @return A new InProcessEPC on success, or an error on failure.
  static Expected<std::unique_ptr<InProcessEPC>>
  Create(Connection *C, BootstrapInfoAccess *BIA,
         std::shared_ptr<SymbolStringPool> SSP = nullptr,
         std::unique_ptr<TaskDispatcher> D = nullptr);

  /// Destroy this InProcessEPC and release its connection.
  ~InProcessEPC();

  /// Run a function with a main-like signature in-process.
  /// @param MainFnAddr Address of the main-like function to run.
  /// @param Args Arguments to pass to the main-like function.
  /// @return Integer result of the main-like function, or an error on failure.
  Expected<int32_t> runAsMain(ExecutorAddr MainFnAddr,
                              ArrayRef<std::string> Args) override;

  /// Run a wrapper function asynchronously in-process.
  /// @param WrapperFnAddr Address of the wrapper function to call.
  /// @param OnComplete Handler invoked with the wrapper function result.
  /// @param ArgBuffer Serialized argument bytes for the wrapper function.
  void callWrapperAsync(ExecutorAddr WrapperFnAddr,
                        IncomingWFRHandler OnComplete,
                        ArrayRef<char> ArgBuffer) override;

  /// Create a default JITLinkMemoryManager for the in-process executor.
  /// @return Default JITLink memory manager, or an error on failure.
  Expected<std::unique_ptr<jitlink::JITLinkMemoryManager>>
  createDefaultMemoryManager() override;

  /// Create a default DylibManager for the in-process executor.
  /// @return Default DylibManager for the in-process executor, or an error on
  /// failure.
  Expected<std::unique_ptr<DylibManager>> createDefaultDylibMgr() override;

  /// Create a default MemoryAccess for the in-process executor.
  /// @return Default MemoryAccess for the in-process executor, or an error on
  /// failure.
  Expected<std::unique_ptr<MemoryAccess>> createDefaultMemoryAccess() override;

  /// Disconnect from the in-process ORC runtime controller.
  /// @return Success, or an error if disconnection fails.
  Error disconnect() override;

private:
  InProcessEPC(Connection *C, std::shared_ptr<SymbolStringPool> SSP,
               std::unique_ptr<TaskDispatcher> D)
      : ExecutorProcessControl(std::move(SSP), std::move(D)), C(C) {
    C->Retain(C);
  }

  uint64_t registerPendingCallWrapperResult(IncomingWFRHandler H);
  void doDisconnect();

  // Incoming JIT-dispatch call from the ORC runtime.
  void callJITDispatch(uint64_t CallId, void *HandlerTag,
                       shared::CWrapperFunctionBuffer ArgBytes);
  static void callJITDispatchEntry(void *IPEPC, uint64_t CallId,
                                   void *HandlerTag,
                                   shared::CWrapperFunctionBuffer ArgBytes);

  // Incoming wrapper function result from the ORC runtime.
  void returnWrapperResult(uint64_t CallId,
                           shared::CWrapperFunctionBuffer ResultBytes);
  static void
  returnWrapperResultEntry(void *IPEPC, uint64_t CallId,
                           shared::CWrapperFunctionBuffer ResultBytes);

  Connection *C;

  std::mutex M;
  uint64_t NextCallId = 0;
  DenseMap<uint64_t, IncomingWFRHandler> PendingCallWrapperResults;
};

} // namespace llvm::orc

#endif // LLVM_EXECUTIONENGINE_ORC_INPROCESSEPC_H
