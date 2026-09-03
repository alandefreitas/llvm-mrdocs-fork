//===- LazyValueInfo.h - Value constraint analysis --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the interface for lazy computation of value constraint
// information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_LAZYVALUEINFO_H
#define LLVM_ANALYSIS_LAZYVALUEINFO_H

#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

namespace llvm {
  class AssumptionCache;
  class BasicBlock;
  class Constant;
  class DataLayout;
  class DominatorTree;
  class Instruction;
  class Value;
  class Use;
  /// Opaque implementation of LazyValueInfo constraint analysis.
  class LazyValueInfoImpl;
  /// This pass computes, caches, and vends lazy value constraint information.
  class LazyValueInfo {
    friend class LazyValueInfoWrapperPass;
    Function *F = nullptr;
    AssumptionCache *AC = nullptr;
    LazyValueInfoImpl *PImpl = nullptr;
    LazyValueInfo(const LazyValueInfo &) = delete;
    void operator=(const LazyValueInfo &) = delete;

    LazyValueInfoImpl *getImpl();
    LazyValueInfoImpl &getOrCreateImpl();

  public:
    /// Destroy this LazyValueInfo and release owned implementation state.
    LLVM_ABI ~LazyValueInfo();
    /// Construct an empty LazyValueInfo with no associated function.
    LazyValueInfo() = default;
    /// Construct LazyValueInfo for function \p F using assumption cache \p AC.
    /// @param F Function whose values are analyzed.
    /// @param AC Assumption cache used for context-sensitive facts.
    LazyValueInfo(Function *F, AssumptionCache *AC) : F(F), AC(AC) {}
    /// Move-construct LazyValueInfo, taking ownership of \p Arg's state.
    /// @param Arg LazyValueInfo to move from.
    LazyValueInfo(LazyValueInfo &&Arg)
        : F(Arg.F), AC(Arg.AC), PImpl(Arg.PImpl) {
      Arg.PImpl = nullptr;
    }
    /// Move-assign LazyValueInfo, taking ownership of \p Arg's state.
    /// @param Arg LazyValueInfo to move from.
    /// @return Reference to this LazyValueInfo.
    LazyValueInfo &operator=(LazyValueInfo &&Arg) {
      releaseMemory();
      F = Arg.F;
      AC = Arg.AC;
      PImpl = Arg.PImpl;
      Arg.PImpl = nullptr;
      return *this;
    }

    // Public query interface.

    /// Determine whether the specified value comparison with a constant is
    /// known to be true or false on the specified CFG edge.
    ///
    /// Pred is a CmpInst predicate.
    /// @param Pred Comparison predicate to evaluate.
    /// @param V Value on the left-hand side of the comparison.
    /// @param C Constant on the right-hand side of the comparison.
    /// @param FromBB Source block of the CFG edge.
    /// @param ToBB Destination block of the CFG edge.
    /// @param CxtI Optional context instruction for the query.
    /// @return A constant true/false if known, otherwise null.
    LLVM_ABI Constant *getPredicateOnEdge(CmpInst::Predicate Pred, Value *V,
                                          Constant *C, BasicBlock *FromBB,
                                          BasicBlock *ToBB,
                                          Instruction *CxtI = nullptr);

    /// Determine whether a value-vs-constant comparison is known true or false
    /// at an instruction.
    ///
    /// \p Pred is a CmpInst predicate. If \p UseBlockValue is true, the block
    /// value is also taken into account.
    /// @param Pred Comparison predicate to evaluate.
    /// @param V Value on the left-hand side of the comparison.
    /// @param C Constant on the right-hand side of the comparison.
    /// @param CxtI Instruction providing the query context.
    /// @param UseBlockValue Whether to also use the block value for \p V.
    /// @return A constant true/false if known, otherwise null.
    LLVM_ABI Constant *getPredicateAt(CmpInst::Predicate Pred, Value *V,
                                      Constant *C, Instruction *CxtI,
                                      bool UseBlockValue);

    /// Determine whether a value comparison is known true or false at an
    /// instruction.
    ///
    /// While this takes two Value's, it still requires that one of them is a
    /// constant. \p Pred is a CmpInst predicate. If \p UseBlockValue is true,
    /// the block value is also taken into account.
    /// @param Pred Comparison predicate to evaluate.
    /// @param LHS Left-hand side of the comparison.
    /// @param RHS Right-hand side of the comparison.
    /// @param CxtI Instruction providing the query context.
    /// @param UseBlockValue Whether to also use block values in the query.
    /// @return A constant true/false if known, otherwise null.
    LLVM_ABI Constant *getPredicateAt(CmpInst::Predicate Pred, Value *LHS,
                                      Value *RHS, Instruction *CxtI,
                                      bool UseBlockValue);

    /// Determine whether the specified value is known to be a constant at the
    /// specified instruction. Return null if not.
    /// @param V Value to query.
    /// @param CxtI Instruction providing the query context.
    /// @return The known constant, or null if unknown.
    LLVM_ABI Constant *getConstant(Value *V, Instruction *CxtI);

    /// Return the ConstantRange constraint that is known to hold for the
    /// specified value at the specified instruction. This may only be called
    /// on integer-typed Values.
    /// @param V Integer-typed value to query.
    /// @param CxtI Instruction providing the query context.
    /// @param UndefAllowed Whether undef may be included in the range.
    /// @return Known constant range for \p V at \p CxtI.
    LLVM_ABI ConstantRange getConstantRange(Value *V, Instruction *CxtI,
                                            bool UndefAllowed);

    /// Return the ConstantRange constraint that is known to hold for the value
    /// at a specific use-site.
    /// @param U Use site of the value to query.
    /// @param UndefAllowed Whether undef may be included in the range.
    /// @return Known constant range for the value at \p U.
    LLVM_ABI ConstantRange getConstantRangeAtUse(const Use &U,
                                                 bool UndefAllowed);

    /// Determine whether the specified value is known to be a
    /// constant on the specified edge.  Return null if not.
    /// @param V Value to query.
    /// @param FromBB Source block of the CFG edge.
    /// @param ToBB Destination block of the CFG edge.
    /// @param CxtI Optional context instruction for the query.
    /// @return The known constant, or null if unknown.
    LLVM_ABI Constant *getConstantOnEdge(Value *V, BasicBlock *FromBB,
                                         BasicBlock *ToBB,
                                         Instruction *CxtI = nullptr);

    /// Return the ConstantRage constraint that is known to hold for the
    /// specified value on the specified edge. This may be only be called
    /// on integer-typed Values.
    /// @param V Integer-typed value to query.
    /// @param FromBB Source block of the CFG edge.
    /// @param ToBB Destination block of the CFG edge.
    /// @param CxtI Optional context instruction for the query.
    /// @return Known constant range for \p V on the edge.
    LLVM_ABI ConstantRange getConstantRangeOnEdge(Value *V, BasicBlock *FromBB,
                                                  BasicBlock *ToBB,
                                                  Instruction *CxtI = nullptr);

    /// Inform the analysis cache that we have threaded an edge from
    /// PredBB to OldSucc to be from PredBB to NewSucc instead.
    /// @param PredBB Predecessor block whose successor edge changed.
    /// @param OldSucc Former successor of \p PredBB.
    /// @param NewSucc New successor of \p PredBB after threading.
    LLVM_ABI void threadEdge(BasicBlock *PredBB, BasicBlock *OldSucc,
                             BasicBlock *NewSucc);

    /// Remove information related to this value from the cache.
    /// @param V Value whose cached facts should be discarded.
    LLVM_ABI void forgetValue(Value *V);

    /// Inform the analysis cache that we have erased a block.
    /// @param BB Basic block that was erased.
    LLVM_ABI void eraseBlock(BasicBlock *BB);

    /// Complete flush all previously computed values
    LLVM_ABI void clear();

    /// Print the \LazyValueInfo Analysis.
    /// We pass in the DTree that is required for identifying which basic blocks
    /// we can solve/print for, in the LVIPrinter.
    /// @param F Function whose LazyValueInfo is printed.
    /// @param DTree Dominator tree used to select printable blocks.
    /// @param OS Output stream for the printed analysis.
    LLVM_ABI void printLVI(Function &F, DominatorTree &DTree, raw_ostream &OS);

    // For old PM pass. Delete once LazyValueInfoWrapperPass is gone.
    /// Release cached LazyValueInfo results to free memory.
    LLVM_ABI void releaseMemory();

    /// Handle invalidation events in the new pass manager.
    /// @param F Function whose analysis result may be invalidated.
    /// @param PA Set of analyses preserved by the transform.
    /// @param Inv Invalidator for resolving analysis dependencies.
    /// @return True if this analysis result should be discarded.
    LLVM_ABI bool invalidate(Function &F, const PreservedAnalyses &PA,
                             FunctionAnalysisManager::Invalidator &Inv);
  };

/// Analysis to compute lazy value information.
class LazyValueAnalysis : public AnalysisInfoMixin<LazyValueAnalysis> {
public:
  /// The analysis result type; a LazyValueInfo for a function.
  typedef LazyValueInfo Result;
  /// Run the analysis over \p F and produce LazyValueInfo.
  /// @param F Function to analyze.
  /// @param FAM Function analysis manager providing dependencies.
  /// @return LazyValueInfo for \p F.
  LLVM_ABI Result run(Function &F, FunctionAnalysisManager &FAM);

private:
  static AnalysisKey Key;
  friend struct AnalysisInfoMixin<LazyValueAnalysis>;
};

/// Printer pass for the LazyValueAnalysis results.
class LazyValueInfoPrinterPass
    : public RequiredPassInfoMixin<LazyValueInfoPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes LazyValueInfo to \p OS.
  /// @param OS Output stream for the printed analysis.
  explicit LazyValueInfoPrinterPass(raw_ostream &OS) : OS(OS) {}

  /// Print LazyValueInfo for \p F and return all analyses preserved.
  /// @param F Function whose LazyValueInfo is printed.
  /// @param AM Function analysis manager providing LazyValueAnalysis.
  /// @return All analyses preserved; this pass does not transform IR.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Wrapper around LazyValueInfo.
class LLVM_ABI LazyValueInfoWrapperPass : public FunctionPass {
  LazyValueInfoWrapperPass(const LazyValueInfoWrapperPass&) = delete;
  void operator=(const LazyValueInfoWrapperPass&) = delete;
public:
  /// Pass identification, replacement for typeid.
  static char ID;
  /// Construct the legacy LazyValueInfo wrapper pass.
  LazyValueInfoWrapperPass();
  /// Destroy the legacy LazyValueInfo wrapper pass.
  ~LazyValueInfoWrapperPass() override {
    assert(!Info.PImpl && "releaseMemory not called");
  }

  /// Return the LazyValueInfo computed by this pass.
  /// @return LazyValueInfo computed by this pass.
  LazyValueInfo &getLVI();

  /// Declare required and preserved analyses for this pass.
  /// @param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  /// Release the LazyValueInfo owned by this pass.
  void releaseMemory() override;
  /// Compute LazyValueInfo for function \p F.
  /// @param F Function to analyze.
  /// @return False; this analysis does not modify the function.
  bool runOnFunction(Function &F) override;
private:
  LazyValueInfo Info;
};

}  // end namespace llvm

#endif
