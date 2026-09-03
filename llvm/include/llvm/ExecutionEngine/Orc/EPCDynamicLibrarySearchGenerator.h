//===------------ EPCDynamicLibrarySearchGenerator.h ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Support loading and searching of dynamic libraries in an executor process
// via the ExecutorProcessControl class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_EPCDYNAMICLIBRARYSEARCHGENERATOR_H
#define LLVM_EXECUTIONENGINE_ORC_EPCDYNAMICLIBRARYSEARCHGENERATOR_H

#include "llvm/ADT/FunctionExtras.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/DylibManager.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
namespace orc {

class ExecutorProcessControl;

/// A utility class to expose symbols found in an executor-process dylib.
///
/// If an instance of this class is attached to a JITDylib as a fallback
/// definition generator, then any symbol found in the given executor dylib that
/// passes the 'Allow' predicate will be added to the JITDylib.
class LLVM_ABI EPCDynamicLibrarySearchGenerator : public DefinitionGenerator {
public:
  /// Predicate that selects which symbols may be imported from the library.
  using SymbolPredicate = unique_function<bool(const SymbolStringPtr &)>;
  /// Callback used to define absolute symbols in a JITDylib.
  using AddAbsoluteSymbolsFn = unique_function<Error(JITDylib &, SymbolMap)>;

  /// Create an EPCDynamicLibrarySearchGenerator that searches for symbols in
  /// the library with the given handle.
  ///
  /// If the Allow predicate is given then only symbols matching the predicate
  /// will be searched for. If the predicate is not given then all symbols will
  /// be searched for.
  ///
  /// If \p AddAbsoluteSymbols is provided, it is used to add the symbols to the
  /// \c JITDylib; otherwise it uses JD.define(absoluteSymbols(...)).
  /// @param ES Execution session for the generator.
  /// @param DylibMgr Manager used to look up symbols in the executor.
  /// @param H Handle of the library to search for symbol definitions.
  /// @param Allow Optional predicate restricting which symbols may be found.
  /// @param AddAbsoluteSymbols Optional callback used to define found symbols.
  EPCDynamicLibrarySearchGenerator(
      ExecutionSession &ES, DylibManager &DylibMgr, tpctypes::DylibHandle H,
      SymbolPredicate Allow = SymbolPredicate(),
      AddAbsoluteSymbolsFn AddAbsoluteSymbols = nullptr)
      : ES(ES), DylibMgr(DylibMgr), H(H), Allow(std::move(Allow)),
        AddAbsoluteSymbols(std::move(AddAbsoluteSymbols)) {}

  /// Create a generator that resolves matching symbols to null.
  ///
  /// Create an EPCDynamicLibrarySearchGenerator that resolves all symbols
  /// matching the Allow predicate to null. This can be used to emulate linker
  /// options like -weak-l / -weak_library where the library is missing at
  /// runtime. (Note: here we're explicitly returning null for these symbols,
  /// rather than returning no value at all for them, which is the usual
  /// "missing symbol" behavior in ORC. This distinction shouldn't matter for
  /// most use-cases).
  /// @param ES Execution session for the generator.
  /// @param DylibMgr Manager used to look up symbols in the executor.
  /// @param Allow Predicate selecting symbols to resolve to null.
  /// @param AddAbsoluteSymbols Optional callback used to define found symbols.
  EPCDynamicLibrarySearchGenerator(
      ExecutionSession &ES, DylibManager &DylibMgr, SymbolPredicate Allow,
      AddAbsoluteSymbolsFn AddAbsoluteSymbols = nullptr)
      : ES(ES), DylibMgr(DylibMgr), Allow(std::move(Allow)),
        AddAbsoluteSymbols(std::move(AddAbsoluteSymbols)) {}

  /// Load a library and return a generator that searches it.
  ///
  /// Permanently loads the library at the given path and, on success, returns
  /// an EPCDynamicLibrarySearchGenerator that will search it for symbol
  /// definitions in the library. On failure returns the reason the library
  /// failed to load.
  /// @param ES Execution session for the generator.
  /// @param DylibMgr Manager used to load the library in the executor.
  /// @param LibraryPath Path of the dynamic library to load, or nullptr for the
  ///        target process.
  /// @param Allow Optional predicate restricting which symbols may be found.
  /// @param AddAbsoluteSymbols Optional callback used to define found symbols.
  /// @return A generator for the loaded library, or an error if the library
  ///         failed to load.
  static Expected<std::unique_ptr<EPCDynamicLibrarySearchGenerator>>
  Load(ExecutionSession &ES, DylibManager &DylibMgr, const char *LibraryPath,
       SymbolPredicate Allow = SymbolPredicate(),
       AddAbsoluteSymbolsFn AddAbsoluteSymbols = nullptr);

  /// Creates a EPCDynamicLibrarySearchGenerator that searches for symbols in
  /// the target process.
  /// @param ES Execution session for the generator.
  /// @param DylibMgr Manager used to look up symbols in the executor.
  /// @param Allow Optional predicate restricting which symbols may be found.
  /// @param AddAbsoluteSymbols Optional callback used to define found symbols.
  /// @return A generator that searches the target process, or an error on
  ///         failure.
  static Expected<std::unique_ptr<EPCDynamicLibrarySearchGenerator>>
  GetForTargetProcess(ExecutionSession &ES, DylibManager &DylibMgr,
                      SymbolPredicate Allow = SymbolPredicate(),
                      AddAbsoluteSymbolsFn AddAbsoluteSymbols = nullptr) {
    return Load(ES, DylibMgr, nullptr, std::move(Allow),
                std::move(AddAbsoluteSymbols));
  }

  /// Search the loaded library for unresolved symbols and define matches.
  /// @param LS Lookup state that may be suspended while definitions are sought.
  /// @param K Kind of lookup being performed.
  /// @param JD Target JITDylib being searched.
  /// @param JDLookupFlags Whether the search should match hidden symbols.
  /// @param Symbols Unresolved symbols and their associated lookup flags.
  /// @return Success, or an error if definition generation fails.
  Error tryToGenerate(LookupState &LS, LookupKind K, JITDylib &JD,
                      JITDylibLookupFlags JDLookupFlags,
                      const SymbolLookupSet &Symbols) override;

private:
  Error addAbsolutes(JITDylib &JD, SymbolMap Symbols);

  ExecutionSession &ES;
  DylibManager &DylibMgr;
  std::optional<tpctypes::DylibHandle> H;
  SymbolPredicate Allow;
  AddAbsoluteSymbolsFn AddAbsoluteSymbols;
};

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_EPCDYNAMICLIBRARYSEARCHGENERATOR_H
