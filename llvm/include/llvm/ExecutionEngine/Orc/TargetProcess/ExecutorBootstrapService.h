//===- ExecutorService.h - Provide bootstrap symbols to session -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Provides a service by supplying some set of bootstrap symbols.
//
// FIXME: The functionality in this file should be moved to the ORC runtime.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_EXECUTORBOOTSTRAPSERVICE_H
#define LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_EXECUTORBOOTSTRAPSERVICE_H

#include "llvm/ADT/StringMap.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
namespace orc {

/// Base interface for services that contribute bootstrap symbols.
class LLVM_ABI ExecutorBootstrapService {
public:
  /// Destroy an ExecutorBootstrapService.
  virtual ~ExecutorBootstrapService();

  /// Add this service's bootstrap symbols to \p BootstrapSymbols.
  /// \param BootstrapSymbols Map of bootstrap symbol names to executor
  ///        addresses to update.
  virtual void
  addBootstrapSymbols(StringMap<ExecutorAddr> &BootstrapSymbols) = 0;

  /// Shut down this service and release any associated resources.
  /// \return Success, or an error if shutdown fails.
  virtual Error shutdown() = 0;
};

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_EXECUTORBOOTSTRAPSERVICE_H
