//===- ExecutorProcessControl.h - Executor process control APIs -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Utilities for interacting with the executor processes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_EXECUTORPROCESSCONTROL_H
#define LLVM_EXECUTIONENGINE_ORC_EXECUTORPROCESSCONTROL_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"
#include "llvm/ExecutionEngine/Orc/Shared/TargetProcessControlTypes.h"
#include "llvm/ExecutionEngine/Orc/Shared/WrapperFunctionUtils.h"
#include "llvm/ExecutionEngine/Orc/SymbolStringPool.h"
#include "llvm/ExecutionEngine/Orc/TargetProcess/UnwindInfoManager.h"
#include "llvm/ExecutionEngine/Orc/TaskDispatch.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"

#include <future>
#include <mutex>
#include <vector>

namespace llvm::jitlink {

class JITLinkMemoryManager;

} // namespace llvm::jitlink

namespace llvm::orc {

class DylibManager;
class ExecutionSession;
class MemoryAccess;

/// ExecutorProcessControl supports interaction with a JIT target process.
class LLVM_ABI ExecutorProcessControl {
  friend class ExecutionSession;
public:

  /// A handler or incoming WrapperFunctionBuffers -- either return values from
  /// callWrapper* calls, or incoming JIT-dispatch requests.
  ///
  /// IncomingWFRHandlers are constructible from
  /// unique_function<void(shared::WrapperFunctionBuffer)>s using the
  /// runInPlace function or a RunWithDispatch object.
  class IncomingWFRHandler {
    friend class ExecutorProcessControl;
  public:
    /// Construct an empty (invalid) handler.
    IncomingWFRHandler() = default;
    /// Return true if this handler contains a callable.
    /// @return True if this handler is non-empty and callable.
    explicit operator bool() const { return !!H; }
    /// Invoke the wrapped handler with the given wrapper function result.
    /// \param WFR Wrapper function result buffer to deliver to the handler.
    void operator()(shared::WrapperFunctionBuffer WFR) { H(std::move(WFR)); }
  private:
    template <typename FnT> IncomingWFRHandler(FnT &&Fn)
      : H(std::forward<FnT>(Fn)) {}

    unique_function<void(shared::WrapperFunctionBuffer)> H;
  };

  /// Run policy that invokes the wrapper-function result handler in place.
  ///
  /// Constructs an IncomingWFRHandler from a function object that is callable
  /// as void(shared::WrapperFunctionBuffer). The function object will be called
  /// directly. This should be used with care as it may block listener threads
  /// in remote EPCs. It is only suitable for simple tasks (e.g. setting a
  /// future), or for performing some quick analysis before dispatching "real"
  /// work as a Task.
  class RunInPlace {
  public:
    /// Build an IncomingWFRHandler that invokes \p Fn directly.
    /// \param Fn Callable accepting a shared::WrapperFunctionBuffer.
    /// @return Handler that invokes \p Fn in place.
    template <typename FnT>
    IncomingWFRHandler operator()(FnT &&Fn) {
      return IncomingWFRHandler(std::forward<FnT>(Fn));
    }
  };

  /// Run policy that dispatches the wrapper-function result handler as a Task.
  ///
  /// Constructs an IncomingWFRHandler from a function object by creating a new
  /// function object that dispatches the original using a TaskDispatcher,
  /// wrapping the original as a GenericNamedTask.
  ///
  /// This is the default approach for running WFR handlers.
  class RunAsTask {
  public:
    /// Construct a run-as-task policy using the given dispatcher.
    /// \param D Task dispatcher used to run wrapped handlers.
    RunAsTask(TaskDispatcher &D) : D(D) {}

    /// Build an IncomingWFRHandler that dispatches \p Fn as a named task.
    /// \param Fn Callable accepting a shared::WrapperFunctionBuffer.
    /// @return Handler that dispatches \p Fn via the task dispatcher.
    template <typename FnT>
    IncomingWFRHandler operator()(FnT &&Fn) {
      return IncomingWFRHandler(
          [&D = this->D, Fn = std::move(Fn)]
          (shared::WrapperFunctionBuffer WFR) mutable {
              D.dispatch(
                makeGenericNamedTask(
                    [Fn = std::move(Fn), WFR = std::move(WFR)]() mutable {
                      Fn(std::move(WFR));
                    }, "WFR handler task"));
          });
    }
  private:
    TaskDispatcher &D;
  };

  /// Construct an ExecutorProcessControl with the given pool and dispatcher.
  /// \param SSP Symbol string pool for this instance.
  /// \param D Task dispatcher for this instance.
  ExecutorProcessControl(std::shared_ptr<SymbolStringPool> SSP,
                         std::unique_ptr<TaskDispatcher> D)
      : SSP(std::move(SSP)), D(std::move(D)) {}

  /// Destroy this ExecutorProcessControl.
  virtual ~ExecutorProcessControl();

  /// Return the ExecutionSession associated with this instance.
  /// Not callable until the ExecutionSession has been associated.
  /// @return Associated ExecutionSession.
  ExecutionSession &getExecutionSession() {
    assert(ES && "No ExecutionSession associated yet");
    return *ES;
  }

  /// Intern a symbol name in the SymbolStringPool.
  /// \param SymName Symbol name to intern.
  /// @return Interned symbol string pointer for \p SymName.
  SymbolStringPtr intern(StringRef SymName) { return SSP->intern(SymName); }

  /// Return a shared pointer to the SymbolStringPool for this instance.
  /// @return Shared pointer to the symbol string pool.
  std::shared_ptr<SymbolStringPool> getSymbolStringPool() const { return SSP; }

  /// Return the TaskDispatcher for this instance.
  /// @return Task dispatcher owned by this instance.
  TaskDispatcher &getDispatcher() { return *D; }

  /// Return the Triple for the target process.
  /// @return Target triple describing the executor process.
  const Triple &getTargetTriple() const { return TargetTriple; }

  /// Get the page size for the target process.
  /// @return Page size of the target process in bytes.
  unsigned getPageSize() const { return PageSize; }

  /// Create a default JITLinkMemoryManager for the target process.
  /// @return Default JITLink memory manager, or an error on failure.
  virtual Expected<std::unique_ptr<jitlink::JITLinkMemoryManager>>
  createDefaultMemoryManager() = 0;

  /// Create a default DylibManager for the target process.
  /// @return Default DylibManager for the target, or an error on failure.
  virtual Expected<std::unique_ptr<DylibManager>> createDefaultDylibMgr() = 0;

  /// Create a default MemoryAccess for the target process.
  /// @return Default MemoryAccess for the target, or an error on failure.
  virtual Expected<std::unique_ptr<MemoryAccess>>
  createDefaultMemoryAccess() = 0;

  /// Returns the bootstrap map.
  /// @return Map from bootstrap keys to SPS-serialized value bytes.
  const StringMap<std::vector<char>> &getBootstrapMap() const {
    return BootstrapMap;
  }

  /// Look up and SPS-deserialize a bootstrap map value.
  /// \param Key Bootstrap map key to look up.
  /// \param Val Set to the deserialized value if present, or nullopt if absent.
  /// @return Success, or an error if the stored value cannot be deserialized.
  template <typename T, typename SPSTagT>
  Error getBootstrapMapValue(StringRef Key, std::optional<T> &Val) const {
    Val = std::nullopt;

    auto I = BootstrapMap.find(Key);
    if (I == BootstrapMap.end())
      return Error::success();

    T Tmp;
    shared::SPSInputBuffer IB(I->second.data(), I->second.size());
    if (!shared::SPSArgList<SPSTagT>::deserialize(IB, Tmp))
      return make_error<StringError>("Could not deserialize value for key " +
                                         Key,
                                     inconvertibleErrorCode());

    Val = std::move(Tmp);
    return Error::success();
  }

  /// Returns the bootstrap symbol map.
  /// @return Map from bootstrap symbol names to executor addresses.
  const StringMap<ExecutorAddr> &getBootstrapSymbolsMap() const {
    return BootstrapSymbols;
  }

  /// Look up addresses for the given bootstrap symbol name pairs.
  ///
  /// For each (ExecutorAddr&, StringRef) pair, looks up the string in the
  /// bootstrap symbols map and writes its address to the ExecutorAddr if
  /// found. If any symbol is not found then the function returns an error.
  /// \param Pairs Pairs of address destinations and bootstrap symbol names.
  /// @return Success, or an error if any bootstrap symbol is missing.
  Error getBootstrapSymbols(
      ArrayRef<std::pair<ExecutorAddr &, StringRef>> Pairs) const {
    for (const auto &KV : Pairs) {
      auto I = BootstrapSymbols.find(KV.second);
      if (I == BootstrapSymbols.end())
        return make_error<StringError>("Symbol \"" + KV.second +
                                           "\" not found "
                                           "in bootstrap symbols map",
                                       inconvertibleErrorCode());

      KV.first = I->second;
    }
    return Error::success();
  }

  /// Run function with a main-like signature.
  /// \param MainFnAddr Address of the main-like function in the executor.
  /// \param Args Argument strings passed as argv to the function.
  /// @return Integer result of the main-like function, or an error on failure.
  virtual Expected<int32_t> runAsMain(ExecutorAddr MainFnAddr,
                                      ArrayRef<std::string> Args) = 0;

  /// Run a wrapper function in the executor. The given WFRHandler will be
  /// called on the result when it is returned.
  ///
  /// The wrapper function should be callable as:
  ///
  /// \code{.cpp}
  ///   CWrapperFunctionBuffer fn(uint8_t *Data, uint64_t Size);
  /// \endcode{.cpp}
  /// \param WrapperFnAddr Address of the wrapper function in the executor.
  /// \param OnComplete Handler invoked with the wrapper function result.
  /// \param ArgBuffer Serialized argument buffer for the wrapper call.
  virtual void callWrapperAsync(ExecutorAddr WrapperFnAddr,
                                IncomingWFRHandler OnComplete,
                                ArrayRef<char> ArgBuffer) = 0;

  /// Run a wrapper function in the executor using the given Runner to dispatch
  /// OnComplete when the result is ready.
  /// \param Runner Policy used to wrap OnComplete into an IncomingWFRHandler.
  /// \param WrapperFnAddr Address of the wrapper function in the executor.
  /// \param OnComplete Callable invoked with the wrapper function result.
  /// \param ArgBuffer Serialized argument buffer for the wrapper call.
  template <typename RunPolicyT, typename FnT>
  void callWrapperAsync(RunPolicyT &&Runner, ExecutorAddr WrapperFnAddr,
                        FnT &&OnComplete, ArrayRef<char> ArgBuffer) {
    callWrapperAsync(
        WrapperFnAddr, Runner(std::forward<FnT>(OnComplete)), ArgBuffer);
  }

  /// Run a wrapper function in the executor. OnComplete will be dispatched
  /// as a GenericNamedTask using this instance's TaskDispatch object.
  /// \param WrapperFnAddr Address of the wrapper function in the executor.
  /// \param OnComplete Callable invoked with the wrapper function result.
  /// \param ArgBuffer Serialized argument buffer for the wrapper call.
  template <typename FnT>
  void callWrapperAsync(ExecutorAddr WrapperFnAddr, FnT &&OnComplete,
                        ArrayRef<char> ArgBuffer) {
    callWrapperAsync(RunAsTask(*D), WrapperFnAddr,
                     std::forward<FnT>(OnComplete), ArgBuffer);
  }

  /// Run a wrapper function in the executor. The wrapper function should be
  /// callable as:
  ///
  /// \code{.cpp}
  ///   CWrapperFunctionBuffer fn(uint8_t *Data, uint64_t Size);
  /// \endcode{.cpp}
  /// \param WrapperFnAddr Address of the wrapper function in the executor.
  /// \param ArgBuffer Serialized argument buffer for the wrapper call.
  /// @return Wrapper function result buffer from the executor.
  shared::WrapperFunctionBuffer callWrapper(ExecutorAddr WrapperFnAddr,
                                            ArrayRef<char> ArgBuffer) {
    std::promise<shared::WrapperFunctionBuffer> RP;
    auto RF = RP.get_future();
    callWrapperAsync(
        RunInPlace(), WrapperFnAddr,
        [&](shared::WrapperFunctionBuffer R) {
          RP.set_value(std::move(R));
        }, ArgBuffer);
    return RF.get();
  }

  /// Run a wrapper function using SPS to serialize the arguments and
  /// deserialize the results.
  /// \param Runner Policy used to dispatch SendResult when the result is ready.
  /// \param WrapperFnAddr Address of the SPS wrapper function in the executor.
  /// \param SendResult Callable that receives the deserialized result.
  /// \param Args Arguments to SPS-serialize and pass to the wrapper.
  template <typename SPSSignature, typename RunPolicyT, typename SendResultT,
            typename... ArgTs>
  void callSPSWrapperAsync(RunPolicyT &&Runner, ExecutorAddr WrapperFnAddr,
                           SendResultT &&SendResult, const ArgTs &...Args) {
    shared::WrapperFunction<SPSSignature>::callAsync(
        [this, WrapperFnAddr, Runner = std::move(Runner)]
        (auto &&SendResult, const char *ArgData, size_t ArgSize) mutable {
          this->callWrapperAsync(std::move(Runner), WrapperFnAddr,
                                 std::move(SendResult),
                                 ArrayRef<char>(ArgData, ArgSize));
        },
        std::forward<SendResultT>(SendResult), Args...);
  }

  /// Run a wrapper function using SPS to serialize the arguments and
  /// deserialize the results.
  /// \param WrapperFnAddr Address of the SPS wrapper function in the executor.
  /// \param SendResult Callable that receives the deserialized result.
  /// \param Args Arguments to SPS-serialize and pass to the wrapper.
  template <typename SPSSignature, typename SendResultT, typename... ArgTs>
  void callSPSWrapperAsync(ExecutorAddr WrapperFnAddr, SendResultT &&SendResult,
                           const ArgTs &...Args) {
    callSPSWrapperAsync<SPSSignature>(RunAsTask(*D), WrapperFnAddr,
                                      std::forward<SendResultT>(SendResult),
                                      Args...);
  }

  /// Run a wrapper function using SPS to serialize the arguments and
  /// deserialize the results.
  ///
  /// If SPSSignature is a non-void function signature then the second argument
  /// (the first in the Args list) should be a reference to a return value.
  /// \param WrapperFnAddr Address of the SPS wrapper function in the executor.
  /// \param WrapperCallArgs Arguments for the SPS wrapper call, including an
  ///        out-parameter for non-void return types.
  /// @return Success, or an error if the SPS wrapper call fails.
  template <typename SPSSignature, typename... WrapperCallArgTs>
  Error callSPSWrapper(ExecutorAddr WrapperFnAddr,
                       WrapperCallArgTs &&...WrapperCallArgs) {
    return shared::WrapperFunction<SPSSignature>::call(
        [this, WrapperFnAddr](const char *ArgData, size_t ArgSize) {
          return callWrapper(WrapperFnAddr, ArrayRef<char>(ArgData, ArgSize));
        },
        std::forward<WrapperCallArgTs>(WrapperCallArgs)...);
  }

  /// Disconnect from the target process.
  ///
  /// This should be called after the JIT session is shut down.
  /// @return Success, or an error if disconnection fails.
  virtual Error disconnect() = 0;

protected:

  /// Symbol string pool for this instance.
  std::shared_ptr<SymbolStringPool> SSP;
  /// Task dispatcher for this instance.
  std::unique_ptr<TaskDispatcher> D;
  /// Associated execution session, or nullptr until set.
  ExecutionSession *ES = nullptr;
  /// Target triple for the executor process.
  Triple TargetTriple;
  /// Page size of the executor process.
  unsigned PageSize = 0;
  /// SPS-serialized bootstrap key/value map from the executor.
  StringMap<std::vector<char>> BootstrapMap;
  /// Bootstrap symbol name to executor address map.
  StringMap<ExecutorAddr> BootstrapSymbols;
};

} // namespace llvm::orc

#endif // LLVM_EXECUTIONENGINE_ORC_EXECUTORPROCESSCONTROL_H
