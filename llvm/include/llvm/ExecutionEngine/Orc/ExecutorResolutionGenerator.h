//===- ExecutorResolutionGenerator.h - Resolve syms in executor -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Declares ExecutorResolutionGenerator for symbol resolution,
// dynamic library loading, and lookup in an executor process via
// ExecutorResolver.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_EXECUTORRESOLUTIONGENERATOR_H
#define LLVM_EXECUTIONENGINE_ORC_EXECUTORRESOLUTIONGENERATOR_H

#include "llvm/ADT/FunctionExtras.h"
#include "llvm/ExecutionEngine/Orc/AbsoluteSymbols.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/DylibManager.h"

namespace llvm::orc {

/// A definition generator that resolves symbols via an executor resolver.
///
/// If an instance of this class is attached to a JITDylib as a fallback
/// definition generator, then any symbol found through the given resolver that
/// passes the 'Allow' predicate will be added to the JITDylib.
class LLVM_ABI ExecutorResolutionGenerator : public DefinitionGenerator {
public:
  /// Predicate that selects which symbols may be imported from the library.
  using SymbolPredicate = unique_function<bool(const SymbolStringPtr &)>;
  /// Callback used to build a materialization unit for absolute symbols.
  using AbsoluteSymbolsFn =
      unique_function<std::unique_ptr<MaterializationUnit>(SymbolMap)>;

  /// Create an ExecutorResolutionGenerator that searches for symbols with the
  /// given resolver handle.
  ///
  /// If the Allow predicate is given then only symbols matching the predicate
  /// will be searched for. If the predicate is not given then all symbols will
  /// be searched for.
  ///
  /// AbsoluteSymbols is used to build a materialization unit for found symbols;
  /// it defaults to absoluteSymbols.
  /// @param ES Execution session for the generator.
  /// @param DylibMgr Manager used to look up symbols in the executor.
  /// @param H Resolver handle used to search for symbol definitions.
  /// @param Allow Optional predicate restricting which symbols may be found.
  /// @param AbsoluteSymbols Callback used to define found symbols.
  ExecutorResolutionGenerator(
      ExecutionSession &ES, DylibManager &DylibMgr, tpctypes::ResolverHandle H,
      SymbolPredicate Allow = SymbolPredicate(),
      AbsoluteSymbolsFn AbsoluteSymbols = absoluteSymbols)
      : ES(ES), DylibMgr(DylibMgr), H(H), Allow(std::move(Allow)),
        AbsoluteSymbols(std::move(AbsoluteSymbols)) {}

  /// Create a generator that uses the dylib manager without a resolver handle.
  ///
  /// If the Allow predicate is given then only symbols matching the predicate
  /// will be searched for. If the predicate is not given then all symbols will
  /// be searched for.
  ///
  /// AbsoluteSymbols is used to build a materialization unit for found symbols;
  /// it defaults to absoluteSymbols.
  /// @param ES Execution session for the generator.
  /// @param DylibMgr Manager used to look up symbols in the executor.
  /// @param Allow Optional predicate restricting which symbols may be found.
  /// @param AbsoluteSymbols Callback used to define found symbols.
  ExecutorResolutionGenerator(
      ExecutionSession &ES, DylibManager &DylibMgr,
      SymbolPredicate Allow = SymbolPredicate(),
      AbsoluteSymbolsFn AbsoluteSymbols = absoluteSymbols)
      : ES(ES), DylibMgr(DylibMgr), Allow(std::move(Allow)),
        AbsoluteSymbols(std::move(AbsoluteSymbols)) {}

  /// Load a library and return a generator that searches it.
  ///
  /// Permanently loads the library at the given path and, on success, returns
  /// an ExecutorResolutionGenerator that will search it for symbol
  /// definitions in the library. On failure returns the reason the library
  /// failed to load.
  /// @param ES Execution session for the generator.
  /// @param DylibMgr Manager used to load the library in the executor.
  /// @param LibraryPath Path of the dynamic library to load, or nullptr for the
  ///        target process.
  /// @param Allow Optional predicate restricting which symbols may be found.
  /// @param AbsoluteSymbols Callback used to define found symbols.
  /// @return A generator for the loaded library, or an error if the library
  ///         failed to load.
  static Expected<std::unique_ptr<ExecutorResolutionGenerator>>
  Load(ExecutionSession &ES, DylibManager &DylibMgr, const char *LibraryPath,
       SymbolPredicate Allow = SymbolPredicate(),
       AbsoluteSymbolsFn AbsoluteSymbols = absoluteSymbols);

  /// Creates an ExecutorResolutionGenerator that searches for symbols in
  /// the target process.
  /// @param ES Execution session for the generator.
  /// @param DylibMgr Manager used to look up symbols in the executor.
  /// @param Allow Optional predicate restricting which symbols may be found.
  /// @param AbsoluteSymbols Callback used to define found symbols.
  /// @return A generator that searches the target process, or an error on
  ///         failure.
  static Expected<std::unique_ptr<ExecutorResolutionGenerator>>
  GetForTargetProcess(ExecutionSession &ES, DylibManager &DylibMgr,
                      SymbolPredicate Allow = SymbolPredicate(),
                      AbsoluteSymbolsFn AbsoluteSymbols = absoluteSymbols) {
    return Load(ES, DylibMgr, nullptr, std::move(Allow),
                std::move(AbsoluteSymbols));
  }

  /// Search the resolver for unresolved symbols and define matches.
  /// @param LS Lookup state that may be suspended while definitions are sought.
  /// @param K Kind of lookup being performed.
  /// @param JD Target JITDylib being searched.
  /// @param JDLookupFlags Whether the search should match hidden symbols.
  /// @param LookupSet Unresolved symbols and their associated lookup flags.
  /// @return Success, or an error if definition generation fails.
  Error tryToGenerate(LookupState &LS, LookupKind K, JITDylib &JD,
                      JITDylibLookupFlags JDLookupFlags,
                      const SymbolLookupSet &LookupSet) override;

private:
  ExecutionSession &ES;
  DylibManager &DylibMgr;
  tpctypes::ResolverHandle H;
  SymbolPredicate Allow;
  AbsoluteSymbolsFn AbsoluteSymbols;
};

} // namespace llvm::orc

#endif // LLVM_EXECUTIONENGINE_ORC_EXECUTORRESOLUTIONGENERATOR_H
