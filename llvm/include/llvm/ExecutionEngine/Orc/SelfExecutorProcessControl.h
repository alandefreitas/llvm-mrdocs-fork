//===-- SelfExecutorProcessControl.h - EPC for in-process JITs --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Executor process control implementation for JITs that run JIT'd code in the
// same process.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_SELFEXECUTORPROCESSCONTROL_H
#define LLVM_EXECUTIONENGINE_ORC_SELFEXECUTORPROCESSCONTROL_H

#include "llvm/ExecutionEngine/Orc/ExecutorProcessControl.h"

#include <memory>

namespace llvm::orc {

/// A ExecutorProcessControl implementation targeting the current process.
class LLVM_ABI SelfExecutorProcessControl : public ExecutorProcessControl {
public:
  /// Construct a SelfExecutorProcessControl for the current process.
  /// \param SSP Symbol string pool for this instance.
  /// \param D Task dispatcher for this instance.
  /// \param TargetTriple Triple for the current process.
  /// \param PageSize Page size for the current process.
  SelfExecutorProcessControl(std::shared_ptr<SymbolStringPool> SSP,
                             std::unique_ptr<TaskDispatcher> D,
                             Triple TargetTriple, unsigned PageSize);

  /// Create a SelfExecutorProcessControl for the current process.
  ///
  /// If no symbol string pool is given then one will be created.
  /// If no memory manager is given a jitlink::InProcessMemoryManager will
  /// be created and used by default.
  /// \param SSP Symbol string pool for the new instance, or nullptr to create
  ///        one.
  /// \param D Task dispatcher for the new instance, or nullptr to create a
  ///        default.
  /// \return Unique pointer to the new instance, or an error on failure.
  static Expected<std::unique_ptr<SelfExecutorProcessControl>>
  Create(std::shared_ptr<SymbolStringPool> SSP = nullptr,
         std::unique_ptr<TaskDispatcher> D = nullptr);

  /// Run function with a main-like signature.
  /// \param MainFnAddr Address of the main-like function in the executor.
  /// \param Args Argument strings passed as argv to the function.
  /// \return Integer result of the main-like function, or an error on failure.
  Expected<int32_t> runAsMain(ExecutorAddr MainFnAddr,
                              ArrayRef<std::string> Args) override;

  /// Run a wrapper function in the executor. The given WFRHandler will be
  /// called on the result when it is returned.
  /// \param WrapperFnAddr Address of the wrapper function in the executor.
  /// \param OnComplete Handler invoked with the wrapper function result.
  /// \param ArgBuffer Serialized argument buffer for the wrapper call.
  void callWrapperAsync(ExecutorAddr WrapperFnAddr,
                        IncomingWFRHandler OnComplete,
                        ArrayRef<char> ArgBuffer) override;

  /// Create a default JITLinkMemoryManager for the current process.
  /// \return Default JITLink memory manager, or an error on failure.
  Expected<std::unique_ptr<jitlink::JITLinkMemoryManager>>
  createDefaultMemoryManager() override;

  /// Create a default DylibManager for the current process.
  /// \return Default DylibManager for the current process, or an error on
  ///         failure.
  Expected<std::unique_ptr<DylibManager>> createDefaultDylibMgr() override;

  /// Create a default MemoryAccess for the current process.
  /// \return Default MemoryAccess for the current process, or an error on
  ///         failure.
  Expected<std::unique_ptr<MemoryAccess>> createDefaultMemoryAccess() override;

  /// Disconnect from the current process.
  ///
  /// This should be called after the JIT session is shut down.
  /// \return Success, or an error if disconnection fails.
  Error disconnect() override;

private:
  class InProcessDylibManager;

  static shared::CWrapperFunctionBuffer
  jitDispatchViaWrapperFunctionManager(void *Ctx, const void *FnTag,
                                       const char *Data, size_t Size);

#ifdef __APPLE__
  std::unique_ptr<UnwindInfoManager> UnwindInfoMgr;
#endif // __APPLE__
};

} // namespace llvm::orc

#endif // LLVM_EXECUTIONENGINE_ORC_SELFEXECUTORPROCESSCONTROL_H
