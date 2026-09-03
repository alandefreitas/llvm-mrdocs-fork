//===- FunctionSpecialization.h - Function Specialization -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Overview:
// ---------
// Function Specialization is a transformation which propagates the constant
// parameters of a function call from the caller to the callee. It is part of
// the Inter-Procedural Sparse Conditional Constant Propagation (IPSCCP) pass.
// The transformation runs iteratively a number of times which is controlled
// by the option `funcspec-max-iters`. Running it multiple times is needed
// for specializing recursive functions, but also exposes new opportunities
// arising from specializations which return constant values or contain calls
// which can be specialized.
//
// Function Specialization supports propagating constant parameters like
// function pointers, literal constants and addresses of global variables.
// By propagating function pointers, indirect calls become direct calls. This
// exposes inlining opportunities which we would have otherwise missed. That's
// why function specialization is run before the inliner in the optimization
// pipeline; that is by design.
//
// Cost Model:
// -----------
// The cost model facilitates a utility for estimating the specialization bonus
// from propagating a constant argument. This is the InstCostVisitor, a class
// that inherits from the InstVisitor. The bonus itself is expressed as codesize
// and latency savings. Codesize savings means the amount of code that becomes
// dead in the specialization from propagating the constant, whereas latency
// savings represents the cycles we are saving from replacing instructions with
// constant values. The InstCostVisitor overrides a set of `visit*` methods to
// be able to handle different types of instructions. These attempt to constant-
// fold the instruction in which case a constant is returned and propagated
// further.
//
// Function pointers are not handled by the InstCostVisitor. They are treated
// separately as they could expose inlining opportunities via indirect call
// promotion. The inlining bonus contributes to the total specialization score.
//
// For a specialization to be profitable its bonus needs to exceed a minimum
// threshold. There are three options for controlling the threshold which are
// expressed as percentages of the original function size:
//  * funcspec-min-codesize-savings
//  * funcspec-min-latency-savings
//  * funcspec-min-inlining-bonus
// There's also an option for controlling the codesize growth from recursive
// specializations. That is `funcspec-max-codesize-growth`.
//
// Once we have all the potential specializations with their score we need to
// choose the best ones, which fit in the module specialization budget. That
// is controlled by the option `funcspec-max-clones`. To find the best `NSpec`
// specializations we use a max-heap. For more details refer to D139346.
//
// Ideas:
// ------
// - With a function specialization attribute for arguments, we could have
//   a direct way to steer function specialization, avoiding the cost-model,
//   and thus control compile-times / code-size.
//
// - Perhaps a post-inlining function specialization pass could be more
//   aggressive on literal constants.
//
// Limitations:
// ------------
// - We are unable to consider specializations of functions called from indirect
//   callsites whose pointer operand has a lattice value that is known to be
//   constant, either from IPSCCP or previous iterations of FuncSpec. This is
//   because SCCP has not yet replaced the uses of the known constant.
//
// References:
// -----------
// 2021 LLVM Dev Mtg “Introducing function specialisation, and can we enable
// it by default?”, https://www.youtube.com/watch?v=zJiCjeXgV5Q
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_FUNCTIONSPECIALIZATION_H
#define LLVM_TRANSFORMS_IPO_FUNCTIONSPECIALIZATION_H

#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/CodeMetrics.h"
#include "llvm/Analysis/InlineCost.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Transforms/Scalar/SCCP.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/SCCPSolver.h"
#include "llvm/Transforms/Utils/SizeOpts.h"

namespace llvm {
/// Map from functions to ranges of candidate specializations.
///
/// The FunctionSpecializer keeps the discovered specialisation opportunities
/// for the module in a single vector, where the specialisations of each
/// function form a contiguous range. This map's value is the beginning and the
/// end of that range.
using SpecMap = DenseMap<Function *, std::pair<unsigned, unsigned>>;

/// Instruction cost type used by specialization profitability estimates.
///
/// A shorter abbreviation of \c InstructionCost to improve indentation.
using Cost = InstructionCost;

/// Map of known constants found during specialization bonus estimation.
using ConstMap = DenseMap<Value *, Constant *>;

/// Specialization signature uniquely designating a specialization of a
/// function.
struct SpecSig {
  /// Hashing key used to distinguish ordinary and empty map keys.
  unsigned Key = 0;
  /// Formal arguments replaced by constants in this specialization.
  SmallVector<ArgInfo, 4> Args;

  /// Return true if this signature equals \p Other.
  ///
  /// \param Other Signature to compare against.
  /// \return True if the keys and argument lists match.
  bool operator==(const SpecSig &Other) const {
    if (Key != Other.Key)
      return false;
    return Args == Other.Args;
  }

  /// Compute a hash code for specialization signature \p S.
  ///
  /// \param S Signature to hash.
  /// \return A hash code derived from \p S.
  friend hash_code hash_value(const SpecSig &S) {
    return hash_combine(hash_value(S.Key), hash_combine_range(S.Args));
  }
};

/// A candidate specialization of a function for a given signature.
struct Spec {
  /// Original function being specialized.
  Function *F;

  /// Cloned specialized version of the original function, if created.
  Function *Clone = nullptr;

  /// Signature identifying the constant arguments of this specialization.
  SpecSig Sig;

  /// Profitability score of the specialization.
  unsigned Score;

  /// Number of instructions in the specialized function.
  unsigned CodeSize;

  /// Call sites that match this specialization.
  SmallVector<CallBase *> CallSites;

  /// Construct a specialization candidate from a copied signature.
  ///
  /// \param F Original function to specialize.
  /// \param S Signature of constant arguments.
  /// \param Score Profitability score of the specialization.
  /// \param CodeSize Number of instructions in the specialization.
  Spec(Function *F, const SpecSig &S, unsigned Score, unsigned CodeSize)
      : F(F), Sig(S), Score(Score), CodeSize(CodeSize) {}
  /// Construct a specialization candidate from a moved signature.
  ///
  /// \param F Original function to specialize.
  /// \param S Signature of constant arguments.
  /// \param Score Profitability score of the specialization.
  /// \param CodeSize Number of instructions in the specialization.
  Spec(Function *F, const SpecSig &&S, unsigned Score, unsigned CodeSize)
      : F(F), Sig(S), Score(Score), CodeSize(CodeSize) {}
};

/// Estimates codesize and latency savings from specializing on constants.
///
/// Visits instructions while propagating known constants to measure how much
/// code becomes dead and how much latency is saved, informing the
/// specialization cost model.
class InstCostVisitor : public InstVisitor<InstCostVisitor, Constant *> {
  std::function<BlockFrequencyInfo &(Function &)> GetBFI;
  Function *F;
  const DataLayout &DL;
  TargetTransformInfo &TTI;
  const SCCPSolver &Solver;

  ConstMap KnownConstants;
  // Basic blocks known to be unreachable after constant propagation.
  DenseSet<BasicBlock *> DeadBlocks;
  // PHI nodes we have visited before.
  DenseSet<Instruction *> VisitedPHIs;
  // PHI nodes we have visited once without successfully constant folding them.
  // Once the InstCostVisitor has processed all the specialization arguments,
  // it should be possible to determine whether those PHIs can be folded
  // (some of their incoming values may have become constant or dead).
  SmallVector<Instruction *> PendingPHIs;

  ConstMap::iterator LastVisited;

public:
  /// Construct a visitor for estimating specialization bonus on \p F.
  ///
  /// \param GetBFI Callback returning block frequency info for a function.
  /// \param F Function whose specialization bonus is estimated.
  /// \param DL Data layout used for constant folding and cost queries.
  /// \param TTI Target transform info for instruction cost estimates.
  /// \param Solver IPSCCP solver providing lattice values and executability.
  InstCostVisitor(std::function<BlockFrequencyInfo &(Function &)> GetBFI,
                  Function *F, const DataLayout &DL, TargetTransformInfo &TTI,
                  SCCPSolver &Solver)
      : GetBFI(GetBFI), F(F), DL(DL), TTI(TTI), Solver(Solver) {}

  /// Return true if \p BB remains executable after constant propagation.
  ///
  /// \param BB Basic block to test for executability.
  /// \return True if \p BB is executable and not marked dead.
  bool isBlockExecutable(BasicBlock *BB) const {
    return Solver.isBlockExecutable(BB) && !DeadBlocks.contains(BB);
  }

  /// Estimate codesize savings from replacing argument \p A with constant \p C.
  ///
  /// \param A Formal argument specialized to a constant.
  /// \param C Constant value substituted for \p A.
  /// \return Estimated codesize savings as an instruction cost.
  LLVM_ABI Cost getCodeSizeSavingsForArg(Argument *A, Constant *C);

  /// Estimate additional codesize savings from folding pending PHI nodes.
  ///
  /// \return Estimated codesize savings from pending PHI folds.
  LLVM_ABI Cost getCodeSizeSavingsFromPendingPHIs();

  /// Estimate latency savings from replacing uses with known constants.
  ///
  /// \return Estimated latency savings as an instruction cost.
  LLVM_ABI Cost getLatencySavingsForKnownConstants();

private:
  friend class InstVisitor<InstCostVisitor, Constant *>;

  Constant *findConstantFor(Value *V) const;

  bool canEliminateSuccessor(BasicBlock *BB, BasicBlock *Succ) const;

  Cost getCodeSizeSavingsForUser(Instruction *User, Value *Use = nullptr,
                                 Constant *C = nullptr);

  Cost estimateBasicBlocks(SmallVectorImpl<BasicBlock *> &WorkList);
  Cost estimateSwitchInst(SwitchInst &I);
  Cost estimateCondBrInst(CondBrInst &I);

  // Transitively Incoming Values (TIV) is a set of Values that can "feed" a
  // value to the initial PHI-node. It is defined like this:
  //
  // * the initial PHI-node belongs to TIV.
  //
  // * for every PHI-node in TIV, its operands belong to TIV
  //
  // If TIV for the initial PHI-node (P) contains more than one constant or a
  // value that is not a PHI-node, then P cannot be folded to a constant.
  //
  // As soon as we detect these cases, we bail, without constructing the
  // full TIV.
  // Otherwise P can be folded to the one constant in TIV.
  bool discoverTransitivelyIncomingValues(Constant *Const, PHINode *Root,
                                          DenseSet<PHINode *> &TransitivePHIs);

  Constant *visitInstruction(Instruction &I) { return nullptr; }
  Constant *visitPHINode(PHINode &I);
  Constant *visitFreezeInst(FreezeInst &I);
  Constant *visitCallBase(CallBase &I);
  Constant *visitLoadInst(LoadInst &I);
  Constant *visitGetElementPtrInst(GetElementPtrInst &I);
  Constant *visitSelectInst(SelectInst &I);
  Constant *visitCastInst(CastInst &I);
  Constant *visitCmpInst(CmpInst &I);
  Constant *visitUnaryOperator(UnaryOperator &I);
  Constant *visitBinaryOperator(BinaryOperator &I);
};

/// Clones functions specialized for constant call arguments under IPSCCP.
///
/// Discovers profitable specializations, creates clones, updates matching call
/// sites, and cleans up functions that become dead after specialization.
class FunctionSpecializer {

  /// The IPSCCP Solver.
  SCCPSolver &Solver;

  Module &M;

  /// Analysis manager, needed to invalidate analyses.
  FunctionAnalysisManager *FAM;

  /// Analyses used to help determine if a function should be specialized.
  std::function<BlockFrequencyInfo &(Function &)> GetBFI;
  std::function<const TargetLibraryInfo &(Function &)> GetTLI;
  std::function<TargetTransformInfo &(Function &)> GetTTI;
  std::function<AssumptionCache &(Function &)> GetAC;

  SmallPtrSet<Function *, 32> Specializations;
  SmallPtrSet<Function *, 32> DeadFunctions;
  DenseMap<Function *, CodeMetrics> FunctionMetrics;
  DenseMap<Function *, unsigned> FunctionGrowth;
  unsigned NGlobals = 0;

public:
  /// Construct a function specializer for module \p M.
  ///
  /// \param Solver IPSCCP solver providing constant lattice values.
  /// \param M Module whose functions may be specialized.
  /// \param FAM Function analysis manager used to invalidate analyses.
  /// \param GetBFI Callback returning block frequency info for a function.
  /// \param GetTLI Callback returning target library info for a function.
  /// \param GetTTI Callback returning target transform info for a function.
  /// \param GetAC Callback returning the assumption cache for a function.
  FunctionSpecializer(
      SCCPSolver &Solver, Module &M, FunctionAnalysisManager *FAM,
      std::function<BlockFrequencyInfo &(Function &)> GetBFI,
      std::function<const TargetLibraryInfo &(Function &)> GetTLI,
      std::function<TargetTransformInfo &(Function &)> GetTTI,
      std::function<AssumptionCache &(Function &)> GetAC)
      : Solver(Solver), M(M), FAM(FAM), GetBFI(GetBFI), GetTLI(GetTLI),
        GetTTI(GetTTI), GetAC(GetAC) {}

  /// Destroy the specializer and release owned resources.
  LLVM_ABI ~FunctionSpecializer();

  /// Run function specialization over the module and return true if changed.
  ///
  /// \return True if the module was modified.
  LLVM_ABI bool run();

  /// Build an instruction-cost visitor for estimating bonus on \p F.
  ///
  /// \param F Function for which specialization savings are estimated.
  /// \return An InstCostVisitor configured for \p F.
  InstCostVisitor getInstCostVisitorFor(Function *F) {
    auto &TTI = GetTTI(*F);
    return InstCostVisitor(GetBFI, F, M.getDataLayout(), TTI, Solver);
  }

  /// Return true if \p F was fully specialized and marked dead.
  ///
  /// \param F Function to test for removal after specialization.
  /// \return True if \p F is in the dead-functions set.
  bool isDeadFunction(Function *F) { return DeadFunctions.contains(F); }

private:
  Constant *getPromotableAlloca(AllocaInst *Alloca, CallInst *Call);

  /// A constant stack value is an AllocaInst that has a single constant
  /// value stored to it. Return this constant if such an alloca stack value
  /// is a function argument.
  Constant *getConstantStackValue(CallInst *Call, Value *Val);

  /// See if there are any new constant values for the callers of \p F via
  /// stack variables and promote them to global variables.
  void promoteConstantStackValues(Function *F);

  /// Clean up fully specialized functions.
  void removeDeadFunctions();

  /// Remove any ssa_copy intrinsics that may have been introduced.
  void cleanUpSSA();

  /// @brief  Find potential specialization opportunities.
  /// @param F Function to specialize
  /// @param FuncSize Cost of specializing a function.
  /// @param AllSpecs A vector to add potential specializations to.
  /// @param SM  A map for a function's specialisation range
  /// @return True, if any potential specializations were found
  bool findSpecializations(Function *F, unsigned FuncSize,
                           SmallVectorImpl<Spec> &AllSpecs, SpecMap &SM);

  /// Compute the inlining bonus for replacing argument \p A with constant \p C.
  unsigned getInliningBonus(Argument *A, Constant *C);

  bool isCandidateFunction(Function *F);

  /// @brief Create a specialization of \p F and prime the SCCPSolver
  /// @param F Function to specialize
  /// @param S Which specialization to create
  /// @return The new, cloned function
  Function *createSpecialization(Function *F, const SpecSig &S);

  /// Determine if it is possible to specialise the function for constant values
  /// of the formal parameter \p A.
  bool isArgumentInteresting(Argument *A);

  /// Check if the value \p V  (an actual argument) is a constant or can only
  /// have a constant value. Return that constant.
  Constant *getCandidateConstant(Value *V);

  /// @brief Find and update calls to \p F, which match a specialization
  /// @param F Orginal function
  /// @param Begin Start of a range of possibly matching specialisations
  /// @param End End of a range (exclusive) of possibly matching specialisations
  void updateCallSites(Function *F, const Spec *Begin, const Spec *End);
};
} // namespace llvm

#endif // LLVM_TRANSFORMS_IPO_FUNCTIONSPECIALIZATION_H
