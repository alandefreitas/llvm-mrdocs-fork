//===- RedirectionManager.h - Redirection manager interface -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Redirection manager interface that redirects a call to symbol to another.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_REDIRECTIONMANAGER_H
#define LLVM_EXECUTIONENGINE_ORC_REDIRECTIONMANAGER_H

#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
namespace orc {

/// Base class for performing redirection of call to symbol to another symbol in
/// runtime.
class LLVM_ABI RedirectionManager {
public:
  /// Destroy this redirection manager.
  virtual ~RedirectionManager() = default;

  /// Change the redirection destination of given symbols to new destination
  /// symbols.
  /// \param JD JITDylib containing the symbols to redirect.
  /// \param NewDests Map of symbols to their new destination definitions.
  /// \return Success, or an error if redirection fails.
  virtual Error redirect(JITDylib &JD, const SymbolMap &NewDests) = 0;

  /// Change the redirection destination of given symbol to new destination
  /// symbol.
  /// \param JD JITDylib containing the symbol to redirect.
  /// \param Symbol Symbol whose redirection destination should change.
  /// \param NewDest New destination definition for \p Symbol.
  /// \return Success, or an error if redirection fails.
  Error redirect(JITDylib &JD, SymbolStringPtr Symbol,
                 ExecutorSymbolDef NewDest) {
    return redirect(JD, {{std::move(Symbol), NewDest}});
  }

private:
  virtual void anchor();
};

/// Base class for managing redirectable symbols in which a call
/// gets redirected to another symbol in runtime.
class RedirectableSymbolManager : public RedirectionManager {
public:
  /// Create redirectable symbols with given symbol names and initial
  /// desitnation symbol addresses.
  /// \param RT Resource tracker for the newly created symbols.
  /// \param InitialDests Map of symbol names to their initial destinations.
  /// \return Success, or an error if the symbols could not be created.
  LLVM_ABI Error createRedirectableSymbols(ResourceTrackerSP RT,
                                           SymbolMap InitialDests);

  /// Create a single redirectable symbol with given symbol name and initial
  /// desitnation symbol address.
  /// \param RT Resource tracker for the newly created symbol.
  /// \param Symbol Name of the redirectable symbol to create.
  /// \param InitialDest Initial destination definition for \p Symbol.
  /// \return Success, or an error if the symbol could not be created.
  Error createRedirectableSymbol(ResourceTrackerSP RT, SymbolStringPtr Symbol,
                                 ExecutorSymbolDef InitialDest) {
    return createRedirectableSymbols(RT, {{std::move(Symbol), InitialDest}});
  }

  /// Emit redirectable symbol
  /// \param MR Materialization responsibility for the symbols being emitted.
  /// \param InitialDests Map of symbol names to their initial destinations.
  virtual void
  emitRedirectableSymbols(std::unique_ptr<MaterializationResponsibility> MR,
                          SymbolMap InitialDests) = 0;
};

/// RedirectableMaterializationUnit materializes redirectable symbol
/// by invoking RedirectableSymbolManager::emitRedirectableSymbols
class RedirectableMaterializationUnit : public MaterializationUnit {
public:
  /// Construct a materialization unit for the given redirectable symbols.
  /// \param RM Manager used to emit the redirectable symbols.
  /// \param InitialDests Map of symbol names to their initial destinations.
  RedirectableMaterializationUnit(RedirectableSymbolManager &RM,
                                  SymbolMap InitialDests)
      : MaterializationUnit(convertToFlags(InitialDests)), RM(RM),
        InitialDests(std::move(InitialDests)) {}

  /// Return the name of this materialization unit.
  /// \return The name of this materialization unit.
  StringRef getName() const override {
    return "RedirectableSymbolMaterializationUnit";
  }

  /// Materialize the redirectable symbols covered by this unit.
  /// \param R Materialization responsibility for the definitions being emitted.
  void materialize(std::unique_ptr<MaterializationResponsibility> R) override {
    RM.emitRedirectableSymbols(std::move(R), std::move(InitialDests));
  }

  /// Discard the given symbol from this materialization unit.
  /// \param JD JITDylib in which the symbol is being discarded.
  /// \param Name Symbol that has been overridden and should be discarded.
  void discard(const JITDylib &JD, const SymbolStringPtr &Name) override {
    InitialDests.erase(Name);
  }

private:
  static MaterializationUnit::Interface
  convertToFlags(const SymbolMap &InitialDests) {
    SymbolFlagsMap Flags;
    for (auto [K, V] : InitialDests)
      Flags[K] = V.getFlags();
    return MaterializationUnit::Interface(Flags, {});
  }

  RedirectableSymbolManager &RM;
  SymbolMap InitialDests;
};

} // namespace orc
} // namespace llvm

#endif
