//===----- ExecutorResolver.h - Symbol resolver -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Executor Symbol resolver.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_EXECUTORRESOLVER_H
#define LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_EXECUTORRESOLVER_H

#include "llvm/ADT/FunctionExtras.h"

#include "llvm/ExecutionEngine/Orc/Shared/ExecutorSymbolDef.h"
#include "llvm/ExecutionEngine/Orc/Shared/SimpleRemoteEPCUtils.h"
#include "llvm/ExecutionEngine/Orc/Shared/TargetProcessControlTypes.h"

namespace llvm::orc {

/// Asynchronous symbol resolver for the executor process.
class ExecutorResolver {
public:
  /// Result of resolving a set of symbols to optional executor addresses.
  using ResolveResult = Expected<std::vector<std::optional<ExecutorAddr>>>;
  /// Callback invoked with the result of an asynchronous resolve.
  using YieldResolveResultFn = unique_function<void(ResolveResult)>;

  /// Destroy an ExecutorResolver.
  virtual ~ExecutorResolver() = default;

  /// Asynchronously resolve the given symbols and invoke \p OnResolve.
  /// \param L Set of remote symbols to look up.
  /// \param OnResolve Callback invoked with the resolve result or an error.
  virtual void resolveAsync(const RemoteSymbolLookupSet &L,
                            YieldResolveResultFn &&OnResolve) = 0;
};

/// ExecutorResolver that looks up symbols in a loaded dynamic library.
class LLVM_ABI DylibSymbolResolver : public ExecutorResolver {
public:
  /// Construct a resolver for the dynamic library identified by \p H.
  /// \param H Handle of the dynamic library to search.
  DylibSymbolResolver(tpctypes::DylibHandle H) : Handle(H) {}

  /// Asynchronously resolve the given symbols in this library.
  /// \param L Set of remote symbols to look up.
  /// \param OnResolve Callback invoked with the resolve result or an error.
  void
  resolveAsync(const RemoteSymbolLookupSet &L,
               ExecutorResolver::YieldResolveResultFn &&OnResolve) override;

private:
  tpctypes::DylibHandle Handle;
};

} // end namespace llvm::orc
#endif // LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_EXECUTORRESOLVER_H
