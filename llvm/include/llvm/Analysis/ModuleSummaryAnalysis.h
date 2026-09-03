//===- ModuleSummaryAnalysis.h - Module summary index builder ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This is the interface to build a ModuleSummaryIndex for a module.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_MODULESUMMARYANALYSIS_H
#define LLVM_ANALYSIS_MODULESUMMARYANALYSIS_H

#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/Compiler.h"
#include <functional>
#include <optional>

namespace llvm {

class BlockFrequencyInfo;
class Function;
class Module;
class ProfileSummaryInfo;
class StackSafetyInfo;

/// Direct function to compute a \c ModuleSummaryIndex from a given module.
///
/// If operating within a pass manager which has defined ways to compute the \c
/// BlockFrequencyInfo for a given function, that can be provided via
/// a std::function callback. Otherwise, this routine will manually construct
/// that information.
/// @param M Module to summarize.
/// @param GetBFICallback Optional callback that returns BlockFrequencyInfo for
///        a function, or null to compute it locally.
/// @param PSI Profile summary information used when building the index.
/// @param GetSSICallback Optional callback that returns StackSafetyInfo for a
///        function, or null when stack safety is unavailable.
/// @return ModuleSummaryIndex summarizing \p M.
LLVM_ABI ModuleSummaryIndex buildModuleSummaryIndex(
    const Module &M,
    std::function<BlockFrequencyInfo *(const Function &F)> GetBFICallback,
    ProfileSummaryInfo *PSI,
    std::function<const StackSafetyInfo *(const Function &F)> GetSSICallback =
        [](const Function &F) -> const StackSafetyInfo * { return nullptr; });

/// Analysis pass to provide the ModuleSummaryIndex object.
class ModuleSummaryIndexAnalysis
    : public AnalysisInfoMixin<ModuleSummaryIndexAnalysis> {
  friend AnalysisInfoMixin<ModuleSummaryIndexAnalysis>;

  LLVM_ABI static AnalysisKey Key;

public:
  /// Provide the result type for this analysis pass.
  using Result = ModuleSummaryIndex;

  /// Run the analysis pass over a module and produce a ModuleSummaryIndex.
  /// @param M Module to summarize.
  /// @param AM Module analysis manager providing dependencies.
  /// @return ModuleSummaryIndex computed for \p M.
  LLVM_ABI Result run(Module &M, ModuleAnalysisManager &AM);
};

/// Legacy wrapper pass to provide the ModuleSummaryIndex object.
class LLVM_ABI ModuleSummaryIndexWrapperPass : public ModulePass {
  std::optional<ModuleSummaryIndex> Index;

public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct the legacy module summary index wrapper pass.
  ModuleSummaryIndexWrapperPass();

  /// Get the index built by this pass.
  /// @return Mutable reference to the ModuleSummaryIndex built by this pass.
  ModuleSummaryIndex &getIndex() { return *Index; }
  /// Get the index built by this pass.
  /// @return Const reference to the ModuleSummaryIndex built by this pass.
  const ModuleSummaryIndex &getIndex() const { return *Index; }

  /// Build a ModuleSummaryIndex for module \p M.
  /// @param M Module to summarize.
  /// @return False; this analysis does not modify the module.
  bool runOnModule(Module &M) override;
  /// Release the cached ModuleSummaryIndex after the module is processed.
  /// @param M Module whose analysis state is being finalized.
  /// @return False; this pass does not modify the module.
  bool doFinalization(Module &M) override;
  /// Declare required and preserved analyses for this pass.
  /// @param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

/// Create a legacy pass that builds a ModuleSummaryIndex for the module.
///
/// The index is intended to be written to bitcode or LLVM assembly.
/// @return A new ModuleSummaryIndexWrapperPass instance.
LLVM_ABI ModulePass *createModuleSummaryIndexWrapperPass();

/// Legacy wrapper pass to provide the ModuleSummaryIndex object.
class LLVM_ABI ImmutableModuleSummaryIndexWrapperPass : public ImmutablePass {
  const ModuleSummaryIndex *Index;

public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct a wrapper around an optional externally built summary index.
  /// @param Index Summary index to expose, or null if none is provided.
  ImmutableModuleSummaryIndexWrapperPass(
      const ModuleSummaryIndex *Index = nullptr);
  /// Return the wrapped ModuleSummaryIndex, or null if none was provided.
  /// @return The wrapped index, or null if none was provided.
  const ModuleSummaryIndex *getIndex() const { return Index; }
  /// Declare required and preserved analyses for this pass.
  /// @param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

/// NPM analysis to provide an externally-built ModuleSummaryIndex (e.g. the
/// combined index from LTO).
class ImmutableModuleSummaryIndexAnalysis
    : public AnalysisInfoMixin<ImmutableModuleSummaryIndexAnalysis> {
  friend AnalysisInfoMixin<ImmutableModuleSummaryIndexAnalysis>;
  LLVM_ABI static AnalysisKey Key;
  const ModuleSummaryIndex *Index = nullptr;

public:
  /// Result holding an externally provided ModuleSummaryIndex pointer.
  class Result {
    const ModuleSummaryIndex *Index = nullptr;
    Result(const ModuleSummaryIndex *Index) : Index(Index) {}
    friend class ImmutableModuleSummaryIndexAnalysis;

  public:
    /// Return the wrapped ModuleSummaryIndex, or null if none was provided.
    /// @return The wrapped index, or null if none was provided.
    const ModuleSummaryIndex *getIndex() const { return Index; }
    /// Handle invalidation; the externally provided index is never invalidated.
    /// @param M Module being invalidated (unused).
    /// @param PA Set of preserved analyses (unused).
    /// @param Inv Invalidator for dependent analyses (unused).
    /// @return False; this result is never invalidated.
    bool invalidate(Module &M, const PreservedAnalyses &PA,
                    ModuleAnalysisManager::Invalidator &Inv) {
      return false;
    }
  };

  /// Construct an analysis with no summary index.
  ImmutableModuleSummaryIndexAnalysis() = default;
  /// Construct an analysis that exposes the given summary index.
  /// @param Index Externally built ModuleSummaryIndex to provide to clients.
  ImmutableModuleSummaryIndexAnalysis(const ModuleSummaryIndex *Index)
      : Index(Index) {}

  /// Return a result wrapping the configured ModuleSummaryIndex.
  /// @param M Module being analyzed; unused because the index is external.
  /// @param AM Module analysis manager; unused by this analysis.
  /// @return Result holding the configured ModuleSummaryIndex pointer.
  Result run(Module &M, ModuleAnalysisManager &AM) { return Result(Index); }
};

/// Create a legacy immutable pass that wraps a provided ModuleSummaryIndex.
///
/// The wrapped index is made available for use by other passes.
/// @param Index Summary index to expose to other passes.
/// @return A new immutable pass wrapping \p Index.
LLVM_ABI ImmutablePass *
createImmutableModuleSummaryIndexWrapperPass(const ModuleSummaryIndex *Index);

/// Returns true if the instruction could have memprof metadata, used to ensure
/// consistency between summary analysis and the ThinLTO backend processing.
/// @param CB Call instruction to inspect for possible memprof summary data.
/// @return True if \p CB could have memprof metadata relevant to summary.
LLVM_ABI bool mayHaveMemprofSummary(const CallBase *CB);

} // end namespace llvm

#endif // LLVM_ANALYSIS_MODULESUMMARYANALYSIS_H
