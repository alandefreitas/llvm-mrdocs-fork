//===- EPCGenericDylibManager.h -- Generic EPC Dylib management -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements dylib loading and searching by calling executor-side wrapper
// functions through Proxy objects.
//
// This simplifies the implementaton of new ExecutorProcessControl instances,
// as this implementation will always work (at the cost of some performance
// overhead for the calls).
//
// This header is protocol-agnostic. To build an instance that targets the ORC
// runtime's SPS controller interface, see EPCGenericDylibManagerSPS.h.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_EPCGENERICDYLIBMANAGER_H
#define LLVM_EXECUTIONENGINE_ORC_EPCGENERICDYLIBMANAGER_H

#include "llvm/ExecutionEngine/Orc/DylibManager.h"
#include "llvm/ExecutionEngine/Orc/ExecutorProcessControl.h"
#include "llvm/ExecutionEngine/Orc/Proxy.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorSymbolDef.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
namespace orc {

class JITDylib;
class SymbolLookupSet;

/// Implements DylibManager via executor-side wrappers reached through Proxies.
class LLVM_ABI EPCGenericDylibManager : public DylibManager {
public:
  /// Proxy for the executor-side dylib-open function. Given the manager
  /// instance address, a path and mode flags it returns a handle to the opened
  /// dylib.
  using OpenProxy =
      Proxy<Expected<tpctypes::DylibHandle>(ExecutorAddr, StringRef, uint64_t)>;

  /// Proxy for the executor-side symbol-resolution function. Given the manager
  /// instance address, a dylib handle and a lookup set it returns the resolved
  /// addresses.
  using ResolveProxy = Proxy<Expected<tpctypes::LookupResult>(
      ExecutorAddr, ExecutorAddr, SymbolLookupSet)>;

  /// Controller-side bindings to an executor-side dylib manager.
  ///
  /// The resolved controller-side handle to an executor-side dylib manager: the
  /// address of the manager instance (passed as the first argument to each
  /// call) plus the proxies for its functions. These are protocol-agnostic:
  /// sps::createEPCGenericDylibManager populates them for the runtime's SPS
  /// controller interface, but a client targeting a different protocol can
  /// build its own Bindings and pass them to the constructor.
  struct Bindings {
    /// Address of the executor-side dylib manager instance.
    ExecutorAddr Instance;
    /// Proxy for the executor-side dylib-open function.
    OpenProxy Open;
    /// Proxy for the executor-side symbol-resolution function.
    ResolveProxy Resolve;
  };

  /// Create an EPCGenericDylibManager instance from a given set of
  /// dylib-manager bindings.
  /// @param ES Execution session used to issue remote calls.
  /// @param B Bindings to the executor-side dylib manager.
  EPCGenericDylibManager(ExecutionSession &ES, Bindings B)
      : ES(ES), B(std::move(B)) {}

  /// Loads the dylib with the given name.
  /// @param Path Path to the dylib to open.
  /// @param Mode Flags passed to the executor-side open function.
  /// @return A handle to the opened dylib, or an error on failure.
  Expected<tpctypes::DylibHandle> open(StringRef Path, uint64_t Mode);

  /// Looks up symbols within the given dylib.
  /// @param H Handle to the dylib to search.
  /// @param Lookup Set of symbols to look up.
  /// @return Target addresses for the looked-up symbols, or an error on failure.
  Expected<tpctypes::LookupResult> lookup(tpctypes::DylibHandle H,
                                          const SymbolLookupSet &Lookup) {
    std::promise<MSVCPExpected<tpctypes::LookupResult>> RP;
    auto RF = RP.get_future();
    lookupAsync(H, Lookup, [&RP](auto R) { RP.set_value(std::move(R)); });
    return RF.get();
  }

  /// Callback invoked when an asynchronous symbol lookup completes.
  using SymbolLookupCompleteFn =
      unique_function<void(Expected<tpctypes::LookupResult>)>;

  /// Looks up symbols within the given dylib.
  /// @param H Handle to the dylib to search.
  /// @param Lookup Set of symbols to look up.
  /// @param Complete Callback invoked with the lookup result.
  void lookupAsync(tpctypes::DylibHandle H, const SymbolLookupSet &Lookup,
                   SymbolLookupCompleteFn Complete);

  /// Load the dynamic library at the given path and return a handle to it.
  /// If DylibPath is null this function will return the global handle for
  /// the target process.
  /// @param DylibPath Path to the dylib to load, or null for the global handle.
  /// @return A handle to the loaded dylib, or an error on failure.
  Expected<tpctypes::DylibHandle> loadDylib(const char *DylibPath) override;

  /// Search for symbols in the target process.
  /// @param H Handle to the dylib to search.
  /// @param Symbols Set of symbols to look up.
  /// @param Complete Callback invoked with the lookup result.
  void
  lookupSymbolsAsync(tpctypes::DylibHandle H, const SymbolLookupSet &Symbols,
                     DylibManager::SymbolLookupCompleteFn Complete) override;

private:
  ExecutionSession &ES;
  Bindings B;
};

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_EPCGENERICDYLIBMANAGER_H
