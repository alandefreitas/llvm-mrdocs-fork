//===------ DylibManager.h - Manage dylibs in the executor ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// APIs for managing real (non-JIT) dylibs in the executing process.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_DYLIBMANAGER_H
#define LLVM_EXECUTIONENGINE_ORC_DYLIBMANAGER_H

#include "llvm/ExecutionEngine/Orc/Shared/TargetProcessControlTypes.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MSVCErrorWorkarounds.h"

#include <future>

namespace llvm::orc {

class SymbolLookupSet;

/// Manages real (non-JIT) dynamic libraries in the executor process.
class LLVM_ABI DylibManager {
public:
  /// Destroy the dylib manager.
  virtual ~DylibManager();

  /// Load the dynamic library at the given path and return a handle to it.
  /// If DylibPath is null this function will return the global handle for
  /// the target process.
  /// @param DylibPath Path to the dylib to load, or null for the global handle.
  /// @return A handle to the loaded dylib, or an error on failure.
  virtual Expected<tpctypes::DylibHandle> loadDylib(const char *DylibPath) = 0;

  /// Search for symbols in the target process.
  ///
  /// The result of the lookup is an array of target addresses that correspond
  /// to the lookup order. If a required symbol is not found then this method
  /// will return an error. If a weakly referenced symbol is not found then it
  /// will be assigned a '0' value.
  /// @param H Handle to the dylib to search.
  /// @param Symbols Set of symbols to look up.
  /// @return Target addresses for the looked-up symbols, or an error if a
  /// required symbol is not found.
  Expected<tpctypes::LookupResult>
  lookupSymbols(tpctypes::DylibHandle H, const SymbolLookupSet &Symbols) {
    std::promise<MSVCPExpected<tpctypes::LookupResult>> RP;
    auto RF = RP.get_future();
    lookupSymbolsAsync(H, Symbols,
                       [&RP](auto Result) { RP.set_value(std::move(Result)); });
    return RF.get();
  }

  /// Callback invoked when an asynchronous symbol lookup completes.
  using SymbolLookupCompleteFn =
      unique_function<void(Expected<tpctypes::LookupResult>)>;

  /// Search for symbols in the target process.
  ///
  /// The result of the lookup is an array of target addresses that correspond
  /// to the lookup order. If a required symbol is not found then this method
  /// will return an error. If a weakly referenced symbol is not found then it
  /// will be assigned a '0' value.
  /// @param H Handle to the dylib to search.
  /// @param Symbols Set of symbols to look up.
  /// @param F Callback invoked with the lookup result.
  virtual void lookupSymbolsAsync(tpctypes::DylibHandle H,
                                  const SymbolLookupSet &Symbols,
                                  SymbolLookupCompleteFn F) = 0;
};

} // end namespace llvm::orc

#endif // LLVM_EXECUTIONENGINE_ORC_DYLIBMANAGER_H
