//===------ GlobalMergeFunctions.h - Global merge functions -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass defines the implementation of a function merging mechanism
// that utilizes a stable function hash to track differences in constants and
// identify potential merge candidates. The process involves two rounds:
// 1. The first round collects stable function hashes and identifies merge
//    candidates with matching hashes. It also computes the set of parameters
//    that point to different constants during the stable function merge.
// 2. The second round leverages this collected global function information to
//    optimistically create a merged function in each module context, ensuring
//    correct transformation.
// Similar to the global outliner, this approach uses the linker's deduplication
// (ICF) to fold identical merged functions, thereby reducing the final binary
// size. The work is inspired by the concepts discussed in the following paper:
// https://dl.acm.org/doi/pdf/10.1145/3652032.3657575.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALMERGEFUNCTIONS_H
#define LLVM_CODEGEN_GLOBALMERGEFUNCTIONS_H

#include "llvm/CGData/StableFunctionMap.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

enum class HashFunctionMode {
  Local,
  BuildingHashFuncion,
  UsingHashFunction,
};

namespace llvm {

/// Locations (instruction, operand index pairs) reachable from a parameter.
using ParamLocs = SmallVector<IndexPair, 4>;
/// Vector of parameter location lists for a stable function merge.
using ParamLocsVecTy = SmallVector<ParamLocs, 8>;

/// Module pass that merges functions using stable function hashes.
///
/// Identifies and merges functions with matching hashes across modules to
/// optimize binary size.
class GlobalMergeFunc {
  HashFunctionMode MergerMode = HashFunctionMode::Local;

  std::unique_ptr<StableFunctionMap> LocalFunctionMap;

  const ModuleSummaryIndex *Index;

public:
  /// Suffix identifying the merged function that parameterizes constant values.
  ///
  /// The original function, without this suffix, becomes a thunk supplying
  /// contexts to the merged function via parameters.
  static constexpr char MergingInstanceSuffix[] = ".Tgm";

  /// Construct a global merge function pass.
  /// \param Index Optional module summary index used during merging.
  GlobalMergeFunc(const ModuleSummaryIndex *Index) : Index(Index) {};

  /// Initialize the merger mode from module \p M and any available summary.
  /// \param M Module used to decide local vs. hash-building/using modes.
  LLVM_ABI void initializeMergerMode(const Module &M);

  /// Run analysis and merging on module \p M.
  /// \param M Module to analyze and transform.
  /// \returns true if the module was modified.
  LLVM_ABI bool run(Module &M);

  /// Analyze module to create stable function into LocalFunctionMap.
  /// \param M Module whose functions are analyzed for stable hashes.
  LLVM_ABI void analyze(Module &M);

  /// Emit LocalFunctionMap into __llvm_merge section.
  /// \param M Module that receives the emitted function map.
  LLVM_ABI void emitFunctionMap(Module &M);

  /// Merge functions in the module using the given function map.
  /// \param M Module whose functions are merged.
  /// \param FunctionMap Stable function map describing merge candidates.
  /// \returns true if the module was modified.
  LLVM_ABI bool merge(Module &M, const StableFunctionMap *FunctionMap);
};

/// Global function merging pass for new pass manager.
struct GlobalMergeFuncPass : public OptionalPassInfoMixin<GlobalMergeFuncPass> {
  /// Optional summary used when importing merge information across modules.
  const ModuleSummaryIndex *ImportSummary = nullptr;
  /// Construct a pass that does not use an import summary.
  GlobalMergeFuncPass() = default;
  /// Construct a pass that uses \p ImportSummary for cross-module merging.
  /// \param ImportSummary Module summary index providing import information.
  GlobalMergeFuncPass(const ModuleSummaryIndex *ImportSummary)
      : ImportSummary(ImportSummary) {}
  /// Run global function merging on module \p M.
  /// \param M Module to transform.
  /// \param AM Module analysis manager providing required analyses.
  /// \returns The analyses preserved after merging.
  LLVM_ABI PreservedAnalyses run(Module &M, AnalysisManager<Module> &AM);
};

} // end namespace llvm
#endif // LLVM_CODEGEN_GLOBALMERGEFUNCTIONS_H
