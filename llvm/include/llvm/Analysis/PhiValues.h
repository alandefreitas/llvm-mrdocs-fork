//===- PhiValues.h - Phi Value Analysis -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the PhiValues class, and associated passes, which can be
// used to find the underlying values of the phis in a function, i.e. the
// non-phi values that can be found by traversing the phi graph.
//
// This information is computed lazily and cached. If new phis are added to the
// function they are handled correctly, but if an existing phi has its operands
// modified PhiValues has to be notified by calling invalidateValue.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_PHIVALUES_H
#define LLVM_ANALYSIS_PHIVALUES_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Pass.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class Value;
class PHINode;
class Function;

/// Class for calculating and caching the underlying values of phis in a
/// function.
///
/// Initially the PhiValues is empty, and gets incrementally populated whenever
/// it is queried.
class PhiValues {
public:
  /// Set of non-phi values reachable from a phi.
  using ValueSet = SmallSetVector<Value *, 4>;

  /// Construct an empty PhiValues.
  /// @param F Function whose phis will be analyzed.
  PhiValues(const Function &F) : F(F) {}

  /// Get the underlying values of a phi.
  ///
  /// This returns the cached value if PN has previously been processed,
  /// otherwise it processes it first.
  /// @param PN Phi whose underlying non-phi values are requested.
  /// @return Set of non-phi values reachable from \p PN.
  LLVM_ABI const ValueSet &getValuesForPhi(const PHINode *PN);

  /// Notify PhiValues that the cached information using V is no longer valid
  ///
  /// Whenever a phi has its operands modified the cached values for that phi
  /// (and the phis that use that phi) become invalid. A user of PhiValues has
  /// to notify it of this by calling invalidateValue on either the operand or
  /// the phi, which will then clear the relevant cached information.
  /// @param V Value whose related cached phi information is invalidated.
  LLVM_ABI void invalidateValue(const Value *V);

  /// Free the memory used by this class.
  LLVM_ABI void releaseMemory();

  /// Print out the values currently in the cache.
  /// @param OS Output stream for the printed cache contents.
  LLVM_ABI void print(raw_ostream &OS) const;

  /// Handle invalidation events in the new pass manager.
  /// @param F Function whose analysis result may be invalidated.
  /// @param PA Set of analyses preserved by the transform.
  /// @param Inv Invalidator for resolving analysis dependencies.
  /// @return True if this analysis result should be invalidated.
  LLVM_ABI bool invalidate(Function &F, const PreservedAnalyses &PA,
                           FunctionAnalysisManager::Invalidator &Inv);

private:
  using ConstValueSet = SmallSetVector<const Value *, 4>;

  /// The next depth number to be used by processPhi.
  unsigned int NextDepthNumber = 1;

  /// Depth numbers of phis. Phis with the same depth number are part of the
  /// same strongly connected component.
  DenseMap<const PHINode *, unsigned int> DepthMap;

  /// Non-phi values reachable from each component.
  DenseMap<unsigned int, ValueSet> NonPhiReachableMap;

  /// All values reachable from each component.
  DenseMap<unsigned int, ConstValueSet> ReachableMap;

  /// A CallbackVH to notify PhiValues when a value is deleted or replaced, so
  /// that the cached information for that value can be cleared to avoid
  /// dangling pointers to invalid values.
  class LLVM_ABI PhiValuesCallbackVH final : public CallbackVH {
    PhiValues *PV;
    void deleted() override;
    void allUsesReplacedWith(Value *New) override;

  public:
    PhiValuesCallbackVH(Value *V, PhiValues *PV = nullptr)
        : CallbackVH(V), PV(PV) {}
  };

  /// A set of callbacks to the values that processPhi has seen.
  DenseSet<PhiValuesCallbackVH, DenseMapInfo<Value *>> TrackedValues;

  /// The function that the PhiValues is for.
  const Function &F;

  /// Process a phi so that its entries in the depth and reachable maps are
  /// fully populated.
  void processPhi(const PHINode *PN, SmallVectorImpl<const PHINode *> &Stack);
};

/// The analysis pass which yields a PhiValues
///
/// The analysis does nothing by itself, and just returns an empty PhiValues
/// which will get filled in as it's used.
class PhiValuesAnalysis : public AnalysisInfoMixin<PhiValuesAnalysis> {
  friend AnalysisInfoMixin<PhiValuesAnalysis>;
  LLVM_ABI static AnalysisKey Key;

public:
  /// Provide the result type for this analysis pass.
  using Result = PhiValues;

  /// Run the analysis pass over a function and produce a PhiValues.
  /// @param F Function to analyze.
  /// @param AM Function analysis manager (unused).
  /// @return PhiValues for \p F.
  LLVM_ABI PhiValues run(Function &F, FunctionAnalysisManager &AM);
};

/// A pass for printing the PhiValues for a function.
///
/// This pass doesn't print whatever information the PhiValues happens to hold,
/// but instead first uses the PhiValues to analyze all the phis in the function
/// so the complete information is printed.
class PhiValuesPrinterPass
    : public RequiredPassInfoMixin<PhiValuesPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes to \p OS.
  /// @param OS Output stream for the printed PhiValues.
  explicit PhiValuesPrinterPass(raw_ostream &OS) : OS(OS) {}

  /// Print the PhiValues for \p F and return all analyses preserved.
  /// @param F Function whose PhiValues are printed.
  /// @param AM Function analysis manager providing PhiValues.
  /// @return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Wrapper pass for the legacy pass manager
class LLVM_ABI PhiValuesWrapperPass : public FunctionPass {
  std::unique_ptr<PhiValues> Result;

public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct the legacy PhiValues wrapper pass.
  PhiValuesWrapperPass();

  /// Return the PhiValues computed by this pass.
  /// @return The PhiValues computed by this pass.
  PhiValues &getResult() { return *Result; }
  /// Return the PhiValues computed by this pass.
  /// @return The PhiValues computed by this pass.
  const PhiValues &getResult() const { return *Result; }

  /// Compute the PhiValues for \p F.
  /// @param F Function to analyze.
  /// @return False; this analysis does not modify the function.
  bool runOnFunction(Function &F) override;

  /// Release the PhiValues owned by this pass.
  void releaseMemory() override;

  /// Report analysis usage for this pass.
  /// @param AU Analysis usage to populate.
  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

} // namespace llvm

#endif
