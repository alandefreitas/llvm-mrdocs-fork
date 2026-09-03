//===-- Speculation.h - Speculative Compilation --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Contains the definition to support speculative compilation when laziness is
// enabled.
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_SPECULATION_H
#define LLVM_EXECUTIONENGINE_ORC_SPECULATION_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/DebugUtils.h"
#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include <mutex>
#include <utility>

namespace llvm {
namespace orc {

class Speculator;

/// Tracks implementation symbols for lazy call-through trampolines.
///
/// Records the (JITDylib, Symbol) pairs of implementation symbols while lazy
/// call-through trampolines are created. Operations are guarded by locks to
/// ensure that the map stays in a consistent state after read/write.
class ImplSymbolMap {
  friend class Speculator;

public:
  /// Pair of an implementation symbol name and the JITDylib that owns it.
  using AliaseeDetails = std::pair<SymbolStringPtr, JITDylib *>;
  /// Alias / stub symbol name keyed in the implementation map.
  using Alias = SymbolStringPtr;
  /// Map from alias symbols to their implementation details.
  using ImapTy = DenseMap<Alias, AliaseeDetails>;
  /// Record implementation symbols for the given aliases in \p SrcJD.
  /// \param ImplMaps Map from alias names to their implementation symbols.
  /// \param SrcJD JITDylib that owns the implementation symbols.
  LLVM_ABI void trackImpls(SymbolAliasMap ImplMaps, JITDylib *SrcJD);

private:
  // FIX ME: find a right way to distinguish the pre-compile Symbols, and update
  // the callsite
  std::optional<AliaseeDetails> getImplFor(const SymbolStringPtr &StubSymbol) {
    std::lock_guard<std::mutex> Lockit(ConcurrentAccess);
    auto Position = Maps.find(StubSymbol);
    if (Position != Maps.end())
      return Position->getSecond();
    else
      return std::nullopt;
  }

  std::mutex ConcurrentAccess;
  ImapTy Maps;
};

/// Coordinates speculative compilation of likely-called functions.
class Speculator {
public:
  /// Executor address of a compiled function or stub.
  using TargetFAddr = ExecutorAddr;
  /// Map from a function symbol to the set of likely callees.
  using FunctionCandidatesMap = DenseMap<SymbolStringPtr, SymbolNameSet>;
  /// Map from a stub address to the set of likely callee symbols.
  using StubAddrLikelies = DenseMap<TargetFAddr, SymbolNameSet>;

private:
  void registerSymbolsWithAddr(TargetFAddr ImplAddr,
                               SymbolNameSet likelySymbols) {
    std::lock_guard<std::mutex> Lockit(ConcurrentAccess);
    GlobalSpecMap.insert({ImplAddr, std::move(likelySymbols)});
  }

  void launchCompile(ExecutorAddr FAddr) {
    SymbolNameSet CandidateSet;
    // Copy CandidateSet is necessary, to avoid unsynchronized access to
    // the datastructure.
    {
      std::lock_guard<std::mutex> Lockit(ConcurrentAccess);
      auto It = GlobalSpecMap.find(FAddr);
      if (It == GlobalSpecMap.end())
        return;
      CandidateSet = It->getSecond();
    }

    SymbolDependenceMap SpeculativeLookUpImpls;

    for (auto &Callee : CandidateSet) {
      auto ImplSymbol = AliaseeImplTable.getImplFor(Callee);
      // try to distinguish already compiled & library symbols
      if (!ImplSymbol)
        continue;
      const auto &ImplSymbolName = ImplSymbol->first;
      JITDylib *ImplJD = ImplSymbol->second;
      auto &SymbolsInJD = SpeculativeLookUpImpls[ImplJD];
      SymbolsInJD.insert(ImplSymbolName);
    }

    DEBUG_WITH_TYPE("orc", {
      for (auto &I : SpeculativeLookUpImpls) {
        llvm::dbgs() << "\n In " << I.first->getName() << " JITDylib ";
        for (auto &N : I.second)
          llvm::dbgs() << "\n Likely Symbol : " << N;
      }
    });

    // for a given symbol, there may be no symbol qualified for speculatively
    // compile try to fix this before jumping to this code if possible.
    for (auto &LookupPair : SpeculativeLookUpImpls)
      ES.lookup(
          LookupKind::Static,
          makeJITDylibSearchOrder(LookupPair.first,
                                  JITDylibLookupFlags::MatchAllSymbols),
          SymbolLookupSet(LookupPair.second), SymbolState::Ready,
          [this](Expected<SymbolMap> Result) {
            if (auto Err = Result.takeError())
              ES.reportError(std::move(Err));
          },
          NoDependenciesToRegister);
  }

public:
  /// Construct a Speculator bound to the given implementation map and session.
  /// \param Impl Map of alias symbols to their implementation details.
  /// \param ref Execution session used for speculative lookups.
  Speculator(ImplSymbolMap &Impl, ExecutionSession &ref)
      : AliaseeImplTable(Impl), ES(ref), GlobalSpecMap(0) {}
  /// Speculators are not copy-constructible.
  /// \param Other Instance that would be copied.
  Speculator(const Speculator &Other) = delete;
  /// Speculators are not move-constructible.
  /// \param Other Instance that would be moved.
  Speculator(Speculator &&Other) = delete;
  /// Speculators are not copy-assignable.
  /// \param Other Instance that would be copied.
  Speculator &operator=(const Speculator &Other) = delete;
  /// Speculators are not move-assignable.
  /// \param Other Instance that would be moved.
  Speculator &operator=(Speculator &&Other) = delete;

  /// Define symbols for this Speculator object (__orc_speculator) and the
  /// speculation runtime entry point symbol (__orc_speculate_for) in the
  /// given JITDylib.
  /// \param JD JITDylib in which to define the speculation runtime symbols.
  /// \param Mangle Mangler used to intern the runtime symbol names.
  /// \return Success, or an error if the runtime symbols cannot be defined.
  LLVM_ABI Error addSpeculationRuntime(JITDylib &JD, MangleAndInterner &Mangle);

  /// Speculatively compile likely functions for the given stub address.
  ///
  /// This is the destination of the `__orc_speculate_for` jump.
  /// \param StubAddr Address of the stub whose likely callees should be
  ///        compiled.
  void speculateFor(TargetFAddr StubAddr) { launchCompile(StubAddr); }

  // FIXME : Register with Stub Address, after JITLink Fix.
  /// Register likely-callee candidates for the given symbols in \p JD.
  /// \param Candidates Map from target symbols to their likely callees.
  /// \param JD JITDylib in which to look up the target symbols.
  void registerSymbols(FunctionCandidatesMap Candidates, JITDylib *JD) {
    for (auto &SymPair : Candidates) {
      auto Target = SymPair.first;
      auto Likely = SymPair.second;

      auto OnReadyFixUp = [Likely, Target,
                           this](Expected<SymbolMap> ReadySymbol) {
        if (ReadySymbol) {
          auto RDef = (*ReadySymbol)[Target];
          registerSymbolsWithAddr(RDef.getAddress(), std::move(Likely));
        } else
          this->getES().reportError(ReadySymbol.takeError());
      };
      // Include non-exported symbols also.
      ES.lookup(
          LookupKind::Static,
          makeJITDylibSearchOrder(JD, JITDylibLookupFlags::MatchAllSymbols),
          SymbolLookupSet(Target, SymbolLookupFlags::WeaklyReferencedSymbol),
          SymbolState::Ready, OnReadyFixUp, NoDependenciesToRegister);
    }
  }

  /// Return the execution session associated with this Speculator.
  /// \return The execution session used for speculative lookups.
  ExecutionSession &getES() { return ES; }

private:
  static void speculateForEntryPoint(Speculator *Ptr, uint64_t StubId);
  std::mutex ConcurrentAccess;
  ImplSymbolMap &AliaseeImplTable;
  ExecutionSession &ES;
  StubAddrLikelies GlobalSpecMap;
};

/// IR layer that analyzes modules and registers speculation candidates.
class LLVM_ABI IRSpeculationLayer : public IRLayer {
public:
  /// Optional map from IR function names to likely callee name sets.
  using IRlikiesStrRef =
      std::optional<DenseMap<StringRef, DenseSet<StringRef>>>;
  /// Function that evaluates likely callees for an IR function.
  using ResultEval = std::function<IRlikiesStrRef(Function &)>;
  /// Map from interned target symbols to sets of likely callee symbols.
  using TargetAndLikelies = DenseMap<SymbolStringPtr, SymbolNameSet>;

  /// Construct an IR speculation layer.
  /// \param ES Execution session for this layer.
  /// \param BaseLayer Layer to emit modules to after analysis.
  /// \param Spec Speculator that receives registered candidates.
  /// \param Mangle Mangler used to intern IR names to JIT symbols.
  /// \param Interpreter Analysis that produces likely-callee maps.
  IRSpeculationLayer(ExecutionSession &ES, IRLayer &BaseLayer, Speculator &Spec,
                     MangleAndInterner &Mangle, ResultEval Interpreter)
      : IRLayer(ES, BaseLayer.getManglingOptions()), NextLayer(BaseLayer),
        S(Spec), Mangle(Mangle), QueryAnalysis(Interpreter) {}

  /// Emit the given module after registering speculation candidates.
  /// \param R Materialization responsibility for the definitions being emitted.
  /// \param TSM Thread-safe module to analyze and emit.
  void emit(std::unique_ptr<MaterializationResponsibility> R,
            ThreadSafeModule TSM) override;

private:
  TargetAndLikelies
  internToJITSymbols(DenseMap<StringRef, DenseSet<StringRef>> IRNames) {
    assert(!IRNames.empty() && "No IRNames received to Intern?");
    TargetAndLikelies InternedNames;
    for (auto &NamePair : IRNames) {
      DenseSet<SymbolStringPtr> TargetJITNames;
      for (auto &TargetNames : NamePair.second)
        TargetJITNames.insert(Mangle(TargetNames));
      InternedNames[Mangle(NamePair.first)] = std::move(TargetJITNames);
    }
    return InternedNames;
  }

  IRLayer &NextLayer;
  Speculator &S;
  MangleAndInterner &Mangle;
  ResultEval QueryAnalysis;
};

} // namespace orc
} // namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_SPECULATION_H
