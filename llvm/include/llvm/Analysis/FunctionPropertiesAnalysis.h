//===- FunctionPropertiesAnalysis.h - Function Properties Analysis --*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the FunctionPropertiesInfo and FunctionPropertiesAnalysis
// classes used to extract function properties.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_FUNCTIONPROPERTIESANALYSIS_H
#define LLVM_ANALYSIS_FUNCTIONPROPERTIESANALYSIS_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/Analysis/IR2Vec.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class BasicBlock;
class CallBase;
class DominatorTree;
class Function;
class LoopInfo;

/// Collection of numeric and embedding properties for a single function.
class FunctionPropertiesInfo {
  friend class FunctionPropertiesUpdater;
  void updateForBB(const BasicBlock &BB, int64_t Direction);
  void updateAggregateStats(const Function &F, const LoopInfo &LI);
  void reIncludeBB(const BasicBlock &BB);

  ir2vec::Embedding FunctionEmbedding = ir2vec::Embedding(0.0);
  const ir2vec::Vocabulary *IR2VecVocab = nullptr;

public:
  /// Compute function properties from \p F, \p DT, \p LI, and optional IR2Vec
  /// vocabulary.
  /// @param F Function to analyze.
  /// @param DT Dominator tree for \p F.
  /// @param LI Loop info for \p F.
  /// @param Vocabulary Optional IR2Vec vocabulary for the function embedding.
  /// @return Computed function properties for \p F.
  LLVM_ABI static FunctionPropertiesInfo
  getFunctionPropertiesInfo(const Function &F, const DominatorTree &DT,
                            const LoopInfo &LI,
                            const ir2vec::Vocabulary *Vocabulary);

  /// Compute function properties for \p F using analyses from \p FAM.
  /// @param F Function to analyze.
  /// @param FAM Function analysis manager providing required analyses.
  /// @return Computed function properties for \p F.
  LLVM_ABI static FunctionPropertiesInfo
  getFunctionPropertiesInfo(Function &F, FunctionAnalysisManager &FAM);

  /// Return true if this info equals \p FPI.
  /// @param FPI Other function properties to compare against.
  /// @return True if this info equals \p FPI.
  LLVM_ABI bool operator==(const FunctionPropertiesInfo &FPI) const;

  /// Return true if this info differs from \p FPI.
  /// @param FPI Other function properties to compare against.
  /// @return True if this info differs from \p FPI.
  bool operator!=(const FunctionPropertiesInfo &FPI) const {
    return !(*this == FPI);
  }

  /// Print these function properties to \p OS.
  /// @param OS Output stream.
  LLVM_ABI void print(raw_ostream &OS) const;

  /// Number of basic blocks
  int64_t BasicBlockCount = 0;

  /// Number of blocks reached from a conditional instruction, or that are
  /// 'cases' of a SwitchInstr.
  int64_t BlocksReachedFromConditionalInstruction = 0;
  // FIXME: We may want to replace this with a more meaningful metric, like
  // number of conditionally executed blocks:
  // 'if (a) s();' would be counted here as 2 blocks, just like
  // 'if (a) s(); else s2(); s3();' would.

  /// Number of uses of this function, plus 1 if the function is callable
  /// outside the module.
  int64_t Uses = 0;

  /// Number of direct calls made from this function to other functions
  /// defined in this module.
  int64_t DirectCallsToDefinedFunctions = 0;

  /// Number of load instructions in the function.
  int64_t LoadInstCount = 0;

  /// Number of store instructions in the function.
  int64_t StoreInstCount = 0;

  /// Maximum loop nesting depth in the function.
  int64_t MaxLoopDepth = 0;

  /// Number of top-level loops in the function.
  int64_t TopLevelLoopCount = 0;

  /// Number of non-debug instructions in the function.
  int64_t TotalInstructionCount = 0;

  /// Number of basic blocks with exactly one successor.
  int64_t BasicBlocksWithSingleSuccessor = 0;
  /// Number of basic blocks with exactly two successors.
  int64_t BasicBlocksWithTwoSuccessors = 0;
  /// Number of basic blocks with more than two successors.
  int64_t BasicBlocksWithMoreThanTwoSuccessors = 0;

  /// Number of basic blocks with exactly one predecessor.
  int64_t BasicBlocksWithSinglePredecessor = 0;
  /// Number of basic blocks with exactly two predecessors.
  int64_t BasicBlocksWithTwoPredecessors = 0;
  /// Number of basic blocks with more than two predecessors.
  int64_t BasicBlocksWithMoreThanTwoPredecessors = 0;

  /// Number of large basic blocks by non-debug instruction count.
  int64_t BigBasicBlocks = 0;
  /// Number of medium basic blocks by non-debug instruction count.
  int64_t MediumBasicBlocks = 0;
  /// Number of small basic blocks by non-debug instruction count.
  int64_t SmallBasicBlocks = 0;

  /// Number of cast instructions in the function.
  int64_t CastInstructionCount = 0;

  /// Number of floating-point instructions in the function.
  int64_t FloatingPointInstructionCount = 0;

  /// Number of integer instructions in the function.
  int64_t IntegerInstructionCount = 0;

  /// Number of constant-integer operands.
  int64_t ConstantIntOperandCount = 0;
  /// Number of constant floating-point operands.
  int64_t ConstantFPOperandCount = 0;
  /// Number of other constant operands.
  int64_t ConstantOperandCount = 0;
  /// Number of instruction operands.
  int64_t InstructionOperandCount = 0;
  /// Number of basic-block operands.
  int64_t BasicBlockOperandCount = 0;
  /// Number of global-value operands.
  int64_t GlobalValueOperandCount = 0;
  /// Number of inline-assembly operands.
  int64_t InlineAsmOperandCount = 0;
  /// Number of argument operands.
  int64_t ArgumentOperandCount = 0;
  /// Number of operands of unrecognized kind.
  int64_t UnknownOperandCount = 0;

  /// Number of critical edges in the CFG.
  int64_t CriticalEdgeCount = 0;
  /// Number of control-flow edges in the CFG.
  int64_t ControlFlowEdgeCount = 0;
  /// Number of unconditional branches.
  int64_t UnconditionalBranchCount = 0;
  /// Number of conditional branches.
  int64_t ConditionalBranchCount = 0;
  /// Number of branch instructions.
  int64_t BranchInstructionCount = 0;
  /// Number of successors of branch instructions.
  int64_t BranchSuccessorCount = 0;
  /// Number of switch instructions.
  int64_t SwitchInstructionCount = 0;
  /// Number of successors of switch instructions.
  int64_t SwitchSuccessorCount = 0;

  /// Number of intrinsic calls.
  int64_t IntrinsicCount = 0;
  /// Number of calls that never return.
  int64_t NoReturnCallCount = 0;
  /// Number of direct calls.
  int64_t DirectCallCount = 0;
  /// Number of indirect calls.
  int64_t IndirectCallCount = 0;
  /// Number of calls returning an integer.
  int64_t CallReturnsIntegerCount = 0;
  /// Number of calls returning a floating-point value.
  int64_t CallReturnsFloatCount = 0;
  /// Number of calls returning a pointer.
  int64_t CallReturnsPointerCount = 0;
  /// Number of calls returning a vector of integers.
  int64_t CallReturnsVectorIntCount = 0;
  /// Number of calls returning a vector of floating-point values.
  int64_t CallReturnsVectorFloatCount = 0;
  /// Number of calls returning a vector of pointers.
  int64_t CallReturnsVectorPointerCount = 0;
  /// Number of calls with many arguments.
  int64_t CallWithManyArgumentsCount = 0;
  /// Number of calls with at least one pointer argument.
  int64_t CallWithPointerArgumentCount = 0;

  /// Return the IR2Vec embedding for the function.
  /// @return The IR2Vec embedding for the function.
  const ir2vec::Embedding &getFunctionEmbedding() const {
    return FunctionEmbedding;
  }

  /// Return the IR2Vec vocabulary used for the embedding, if any.
  /// @return The IR2Vec vocabulary, or nullptr if none was provided.
  const ir2vec::Vocabulary *getIR2VecVocab() const { return IR2VecVocab; }

  /// Set the function embedding for unit tests.
  /// @param Embedding Embedding to store.
  void setFunctionEmbeddingForTest(const ir2vec::Embedding &Embedding) {
    FunctionEmbedding = Embedding;
  }
};

/// Analysis pass that computes FunctionPropertiesInfo for a function.
class FunctionPropertiesAnalysis
    : public AnalysisInfoMixin<FunctionPropertiesAnalysis> {

public:
  /// Analysis key for FunctionPropertiesAnalysis.
  LLVM_ABI static AnalysisKey Key;

  /// Result type of FunctionPropertiesAnalysis.
  using Result = const FunctionPropertiesInfo;

  /// Run the analysis over \p F and produce FunctionPropertiesInfo.
  /// @param F Function to analyze.
  /// @param FAM Function analysis manager providing required analyses.
  /// @return Computed function properties for \p F.
  LLVM_ABI FunctionPropertiesInfo run(Function &F,
                                      FunctionAnalysisManager &FAM);
};

/// Printer pass for the FunctionPropertiesAnalysis results.
class FunctionPropertiesPrinterPass
    : public RequiredPassInfoMixin<FunctionPropertiesPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes to \p OS.
  /// @param OS Output stream for the printed properties.
  explicit FunctionPropertiesPrinterPass(raw_ostream &OS) : OS(OS) {}

  /// Print FunctionPropertiesInfo for \p F and preserve all analyses.
  /// @param F Function whose properties are printed.
  /// @param AM Function analysis manager providing FunctionPropertiesInfo.
  /// @return Analyses that are preserved (all of them).
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Statistics pass for the FunctionPropertiesAnalysis results.
class FunctionPropertiesStatisticsPass
    : public RequiredPassInfoMixin<FunctionPropertiesStatisticsPass> {
  bool IsPreOptimization;

public:
  /// Construct a statistics pass, optionally marking pre-optimization stats.
  /// @param IsPreOptimization When true, record stats as pre-optimization.
  explicit FunctionPropertiesStatisticsPass(bool IsPreOptimization = false)
      : IsPreOptimization(IsPreOptimization) {}

  /// Emit FunctionPropertiesInfo statistics for \p F and preserve all analyses.
  /// @param F Function whose properties are recorded.
  /// @param FAM Function analysis manager providing FunctionPropertiesInfo.
  /// @return Analyses that are preserved (all of them).
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
};

/// Helper that updates FunctionPropertiesInfo after inlining.
///
/// A FunctionPropertiesUpdater keeps the state necessary for tracking the
/// changes llvm::InlineFunction makes. The idea is that inlining will at most
/// modify a few BBs of the Caller (maybe the entry BB and definitely the
/// callsite BB) and potentially affect exception handling BBs in the case of
/// invoke inlining.
class FunctionPropertiesUpdater {
public:
  /// Begin tracking property updates for inlining call \p CB into \p FPI.
  /// @param FPI Function properties of the caller to update.
  /// @param CB Call being inlined.
  LLVM_ABI FunctionPropertiesUpdater(FunctionPropertiesInfo &FPI, CallBase &CB);

  /// Finish updating \p FPI after inlining using analyses from \p FAM.
  /// @param FAM Function analysis manager for the caller.
  LLVM_ABI void finish(FunctionAnalysisManager &FAM) const;

  /// Finish the update and return whether the result matches a full recompute.
  /// @param FAM Function analysis manager for the caller.
  /// @return True if the incremental update matches a full recompute.
  bool finishAndTest(FunctionAnalysisManager &FAM) const {
    finish(FAM);
    return isUpdateValid(Caller, FPI, FAM);
  }

private:
  FunctionPropertiesInfo &FPI;
  BasicBlock &CallSiteBB;
  Function &Caller;

  LLVM_ABI static bool isUpdateValid(Function &F,
                                     const FunctionPropertiesInfo &FPI,
                                     FunctionAnalysisManager &FAM);

  DominatorTree &getUpdatedDominatorTree(FunctionAnalysisManager &FAM) const;

  DenseSet<const BasicBlock *> Successors;
  DenseSet<const BasicBlock *> CallUsers;

  // Edges we might potentially need to remove from the dominator tree.
  SmallVector<DominatorTree::UpdateType, 2> DomTreeUpdates;
};
} // namespace llvm
#endif // LLVM_ANALYSIS_FUNCTIONPROPERTIESANALYSIS_H
