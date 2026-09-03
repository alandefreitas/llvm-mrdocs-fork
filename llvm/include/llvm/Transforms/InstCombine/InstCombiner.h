//===- InstCombiner.h - InstCombine implementation --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file provides the interface for the instcombine pass implementation.
/// The interface is used for generic transformations in this folder and
/// target specific combinations in the targets.
/// The visitor implementation is in \c InstCombinerImpl in
/// \c InstCombineInternal.h.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_INSTCOMBINE_INSTCOMBINER_H
#define LLVM_TRANSFORMS_INSTCOMBINE_INSTCOMBINER_H

#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/Analysis/DomConditionCache.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/TargetFolder.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/KnownBits.h"
#include <cassert>

#define DEBUG_TYPE "instcombine"
#include "llvm/Transforms/Utils/InstructionWorklist.h"

namespace llvm {

class AAResults;
class AssumptionCache;
class OptimizationRemarkEmitter;
class ProfileSummaryInfo;
class TargetLibraryInfo;
class TargetTransformInfo;

/// The core instruction combiner logic.
///
/// This class provides both the logic to recursively visit instructions and
/// combine them.
class LLVM_LIBRARY_VISIBILITY InstCombiner {
  /// IRBuilder inserter that adds new instructions to the worklist and new
  /// assumptions to the AssumptionCache.
  class LLVM_ABI IRBuilderInstCombineInserter final
      : public IRBuilderDefaultInserter {
    InstCombiner &IC;

  public:
    ~IRBuilderInstCombineInserter() override;
    IRBuilderInstCombineInserter(InstCombiner &IC) : IC(IC) {}

    void InsertHelper(Instruction *I, const Twine &Name,
                      BasicBlock::iterator InsertPt) const override;
  };

  /// Only used to call target specific intrinsic combining.
  /// It must **NOT** be used for any other purpose, as InstCombine is a
  /// target-independent canonicalization transform.
  TargetTransformInfo &TTIForTargetIntrinsicsOnly;

public:
  /// Maximum size of array considered when transforming.
  uint64_t MaxArraySizeForCombine = 0;

  /// An IRBuilder that automatically inserts new instructions into the
  /// worklist.
  using BuilderTy = IRBuilder<TargetFolder, IRBuilderInstCombineInserter>;
  /// IR builder that inserts new instructions and adds them to the worklist.
  BuilderTy Builder;

protected:
  /// A worklist of the instructions that need to be simplified.
  InstructionWorklist &Worklist;

  /// The function being simplified by the combiner.
  Function &F;

  /// Whether the combiner is running in minimize-size mode.
  const bool MinimizeSize;

  /// Optional alias-analysis results used by some combines.
  AAResults *AA;

  // Required analyses.
  /// The assumption cache for the function.
  AssumptionCache &AC;
  /// Target library information for recognized libcalls.
  TargetLibraryInfo &TLI;
  /// The dominator tree for the function.
  DominatorTree &DT;
  /// The data layout for the module or function.
  const DataLayout &DL;
  /// Simplify query bundling analyses used by value-tracking helpers.
  SimplifyQuery SQ;
  /// Emitter for optimization remarks produced by the combiner.
  OptimizationRemarkEmitter &ORE;
  /// Optional block-frequency information for profile-guided combines.
  BlockFrequencyInfo *BFI;
  /// Optional branch-probability information for profile-guided combines.
  BranchProbabilityInfo *BPI;
  /// Optional profile-summary information for profile-guided combines.
  ProfileSummaryInfo *PSI;
  /// Cache of dominating conditions used to refine known bits.
  DomConditionCache DC;

  /// Reverse post-order traversal of the function's basic blocks.
  ReversePostOrderTraversal<BasicBlock *> &RPOT;

  /// Whether this combiner instance has modified the IR.
  bool MadeIRChange = false;

  /// Edges that are known to never be taken.
  SmallDenseSet<std::pair<BasicBlock *, BasicBlock *>, 8> DeadEdges;

  /// Order of predecessors to canonicalize phi nodes towards.
  SmallDenseMap<BasicBlock *, SmallVector<BasicBlock *>, 8> PredOrder;

  /// Backedges, used to avoid pushing instructions across backedges in cases.
  ///
  /// where this may result in infinite combine loops. For irreducible loops this picks an arbitrary backedge.
  SmallDenseSet<std::pair<const BasicBlock *, const BasicBlock *>, 8> BackEdges;
  /// Whether \c BackEdges has already been computed for this function.
  bool ComputedBackEdges = false;

  /// Source for annotation metadata, used by the IRBuilder inserter.
  Instruction *AnnotationMetadataSource = nullptr;

public:
  /// Construct an instruction combiner for \p F with the given analyses.
  /// \param Worklist Worklist of instructions to simplify.
  /// \param F Function being simplified.
  /// \param AA Optional alias-analysis results.
  /// \param AC Assumption cache for the function.
  /// \param TLI Target library information.
  /// \param TTI Target transform info used only for intrinsic combines.
  /// \param DT Dominator tree for the function.
  /// \param ORE Emitter for optimization remarks.
  /// \param BFI Optional block-frequency information.
  /// \param BPI Optional branch-probability information.
  /// \param PSI Optional profile-summary information.
  /// \param DL Data layout for the module or function.
  /// \param RPOT Reverse post-order traversal of the function.
  InstCombiner(InstructionWorklist &Worklist, Function &F, AAResults *AA,
               AssumptionCache &AC, TargetLibraryInfo &TLI,
               TargetTransformInfo &TTI, DominatorTree &DT,
               OptimizationRemarkEmitter &ORE, BlockFrequencyInfo *BFI,
               BranchProbabilityInfo *BPI, ProfileSummaryInfo *PSI,
               const DataLayout &DL,
               ReversePostOrderTraversal<BasicBlock *> &RPOT)
      : TTIForTargetIntrinsicsOnly(TTI),
        Builder(F.getContext(), TargetFolder(DL),
                IRBuilderInstCombineInserter(*this)),
        Worklist(Worklist), F(F), MinimizeSize(F.hasMinSize()), AA(AA), AC(AC),
        TLI(TLI), DT(DT), DL(DL),
        SQ(DL, &TLI, &DT, &AC, nullptr, /*UseInstrInfo*/ true,
           /*CanUseUndef*/ true, &DC),
        ORE(ORE), BFI(BFI), BPI(BPI), PSI(PSI), RPOT(RPOT) {}

  /// Destroy the instruction combiner.
  virtual ~InstCombiner() = default;

  /// Return the source of a bitcast, or \p V if there is none.
  ///
  /// Return the source operand of a potentially bitcasted value while
  /// optionally checking if it has one use. If there is no bitcast or the one
  /// use check is not met, return the input value itself.
  /// \param V Value that may be a bitcast.
  /// \param OneUseOnly If true, only peek through a one-use bitcast.
  /// @return The bitcast source, or \p V if there is none.
  static Value *peekThroughBitcast(Value *V, bool OneUseOnly = false) {
    if (auto *BitCast = dyn_cast<BitCastInst>(V))
      if (!OneUseOnly || BitCast->hasOneUse())
        return BitCast->getOperand(0);

    // V is not a bitcast or V has more than one use and OneUseOnly is true.
    return V;
  }

  /// Assign a complexity rank to \p V for canonicalization.
  ///
  /// Assign a complexity or rank value to LLVM Values. This is used to reduce
  /// the amount of pattern matching needed for compares and commutative
  /// instructions. For example, if we have:
  ///   icmp ugt X, Constant
  /// or
  ///   xor (add X, Constant), cast Z
  ///
  /// We do not have to consider the commuted variants of these patterns because
  /// canonicalization based on complexity guarantees the above ordering.
  ///
  /// This routine maps IR values to various complexity ranks:
  ///   0 -> undef
  ///   1 -> Constants
  ///   2 -> Cast and (f)neg/not instructions
  ///   3 -> Other instructions and arguments
  /// \param V Value whose complexity rank is computed.
  /// @return Complexity rank of \p V.
  static unsigned getComplexity(Value *V) {
    if (isa<Constant>(V))
      return isa<UndefValue>(V) ? 0 : 1;

    using namespace llvm::PatternMatch;
    if (isa<CastInst>(V) || match(V, m_Neg(m_Value())) ||
        match(V, m_Not(m_Value())) || match(V, m_FNeg(m_Value())))
      return 2;

    return 3;
  }

  /// Predicate canonicalization reduces the number of patterns that need to be matched by other transforms.
  ///
  /// For example, we may swap the operands of a conditional branch or select to create a compare with a canonical (inverted) predicate which is then more likely to be matched with other values.
  /// \param Pred Compare predicate to test for canonical form.
  /// @return True if \p Pred is in canonical form.
  static bool isCanonicalPredicate(CmpPredicate Pred) {
    switch (Pred) {
    case CmpInst::ICMP_NE:
    case CmpInst::ICMP_ULE:
    case CmpInst::ICMP_SLE:
    case CmpInst::ICMP_UGE:
    case CmpInst::ICMP_SGE:
    // TODO: There are 16 FCMP predicates. Should others be (not) canonical?
    case CmpInst::FCMP_ONE:
    case CmpInst::FCMP_OLE:
    case CmpInst::FCMP_OGE:
      return false;
    default:
      return true;
    }
  }

  /// Add one to a Constant
  /// \param C Constant to increment.
  /// @return \p C plus one.
  static Constant *AddOne(Constant *C) {
    return ConstantExpr::getAdd(C, ConstantInt::get(C->getType(), 1));
  }

  /// Subtract one from a Constant
  /// \param C Constant to decrement.
  /// @return \p C minus one.
  static Constant *SubOne(Constant *C) {
    return ConstantExpr::getSub(C, ConstantInt::get(C->getType(), 1));
  }

  /// Return true if absorbing a not into \p SI would break canonical form.
  ///
  /// a ? b : false and a ? true : b are the canonical form of logical and/or.
  /// This includes !a ? b : false and !a ? true : b. Absorbing the not into
  /// the select by swapping operands would break recognition of this pattern
  /// in other analyses, so don't do that.
  /// \param SI Select that may be a logical and/or.
  /// @return True if absorbing a not into \p SI would break canonical form.
  static bool shouldAvoidAbsorbingNotIntoSelect(const SelectInst &SI) {
    // a ? b : false and a ? true : b are the canonical form of logical and/or.
    // This includes !a ? b : false and !a ? true : b. Absorbing the not into
    // the select by swapping operands would break recognition of this pattern
    // in other analyses, so don't do that.
    return match(&SI, PatternMatch::m_LogicalAnd(PatternMatch::m_Value(),
                                                 PatternMatch::m_Value())) ||
           match(&SI, PatternMatch::m_LogicalOr(PatternMatch::m_Value(),
                                                PatternMatch::m_Value()));
  }

  /// Return a freely inverted form of \p V, or null if none exists.
  ///
  /// Return nonnull value if V is free to invert under the condition of
  /// WillInvertAllUses.
  /// If Builder is nonnull, it will return a simplified ~V.
  /// If Builder is null, it will return an arbitrary nonnull value (not
  /// dereferenceable).
  /// If the inversion will consume instructions, `DoesConsume` will be set to
  /// true. Otherwise it will be false.
  /// \param V Value to invert if free.
  /// \param WillInvertAllUses Whether all uses of \p V will become uses of ~V.
  /// \param Builder Optional builder used to materialize a simplified ~V.
  /// \param DoesConsume Set to true if inversion consumes instructions.
  /// \param Depth Current recursion depth for this query.
  /// @return A freely inverted form of \p V, or null if none exists.
  LLVM_ABI Value *getFreelyInvertedImpl(Value *V, bool WillInvertAllUses,
                                        BuilderTy *Builder, bool &DoesConsume,
                                        unsigned Depth);

  /// Return a freely inverted form of \p V, or null if none exists.
  /// \param V Value to invert if free.
  /// \param WillInvertAllUses Whether all uses of \p V will become uses of ~V.
  /// \param Builder Optional builder used to materialize a simplified ~V.
  /// \param DoesConsume Set to true if inversion consumes instructions.
  /// @return A freely inverted form of \p V, or null if none exists.
  Value *getFreelyInverted(Value *V, bool WillInvertAllUses,
                                  BuilderTy *Builder, bool &DoesConsume) {
    DoesConsume = false;
    return getFreelyInvertedImpl(V, WillInvertAllUses, Builder, DoesConsume,
                                 /*Depth*/ 0);
  }

  /// Return a freely inverted form of \p V, or null if none exists.
  /// \param V Value to invert if free.
  /// \param WillInvertAllUses Whether all uses of \p V will become uses of ~V.
  /// \param Builder Optional builder used to materialize a simplified ~V.
  /// @return A freely inverted form of \p V, or null if none exists.
  Value *getFreelyInverted(Value *V, bool WillInvertAllUses,
                                  BuilderTy *Builder) {
    bool Unused;
    return getFreelyInverted(V, WillInvertAllUses, Builder, Unused);
  }

  /// Return true if the specified value is free to invert (apply ~ to).
  ///
  /// This happens in cases where the ~ can be eliminated. If WillInvertAllUses is true, work under the assumption that the caller intends to remove all uses of V and only keep uses of ~V. See also: canFreelyInvertAllUsersOf()
  /// \param V Value to test for free inversion.
  /// \param WillInvertAllUses Whether all uses of \p V will become uses of ~V.
  /// \param DoesConsume Set to true if inversion consumes instructions.
  /// @return True if \p V is free to invert under \p WillInvertAllUses.
  bool isFreeToInvert(Value *V, bool WillInvertAllUses,
                             bool &DoesConsume) {
    return getFreelyInverted(V, WillInvertAllUses, /*Builder*/ nullptr,
                             DoesConsume) != nullptr;
  }

  /// Return true if \p V is free to invert under \p WillInvertAllUses.
  /// \param V Value to test for free inversion.
  /// \param WillInvertAllUses Whether all uses of \p V will become uses of ~V.
  /// @return True if \p V is free to invert under \p WillInvertAllUses.
  bool isFreeToInvert(Value *V, bool WillInvertAllUses) {
    bool Unused;
    return isFreeToInvert(V, WillInvertAllUses, Unused);
  }

  /// Return true if every user of i1 \p V can adapt to !V for free.
  ///
  /// Given i1 V, can every user of V be freely adapted if V is changed to !V ?
  /// InstCombine's freelyInvertAllUsersOf() must be kept in sync with this fn.
  /// NOTE: for Instructions only!
  ///
  /// See also: isFreeToInvert()
  /// \param V Instruction whose users are checked for free inversion.
  /// \param IgnoredUser Optional user to skip while checking.
  /// @return True if every user of \p V can adapt to !V for free.
  bool canFreelyInvertAllUsersOf(Instruction *V, Value *IgnoredUser) {
    // Look at every user of V.
    for (Use &U : V->uses()) {
      if (U.getUser() == IgnoredUser)
        continue; // Don't consider this user.

      auto *I = cast<Instruction>(U.getUser());
      switch (I->getOpcode()) {
      case Instruction::Select:
        if (U.getOperandNo() != 0) // Only if the value is used as select cond.
          return false;
        if (shouldAvoidAbsorbingNotIntoSelect(*cast<SelectInst>(I)))
          return false;
        break;
      case Instruction::CondBr:
        assert(U.getOperandNo() == 0 && "Must be branching on that value.");
        break; // Free to invert by swapping true/false values/destinations.
      case Instruction::Xor: // Can invert 'xor' if it's a 'not', by ignoring
                             // it.
        if (!match(I, PatternMatch::m_Not(PatternMatch::m_Value())))
          return false; // Not a 'not'.
        break;
      default:
        return false; // Don't know, likely not freely invertible.
      }
      // So far all users were free to invert...
    }
    return true; // Can freely invert all users!
  }

  /// Some binary operators require special handling to avoid poison and undefined behavior.
  ///
  /// If a constant vector has undef elements, replace those undefs with identity constants if possible because those are always safe to execute. If no identity constant exists, replace undef with some other safe constant.
  /// \param Opcode Binary operator whose identity or safe constant is needed.
  /// \param In Constant vector that may contain undef elements.
  /// \param IsRHSConstant Whether \p In is used as the right-hand operand.
  /// @return A constant vector with undef elements replaced by safe constants.
  static Constant *
  getSafeVectorConstantForBinop(BinaryOperator::BinaryOps Opcode, Constant *In,
                                bool IsRHSConstant) {
    auto *InVTy = cast<FixedVectorType>(In->getType());

    Type *EltTy = InVTy->getElementType();
    auto *SafeC = ConstantExpr::getBinOpIdentity(Opcode, EltTy, IsRHSConstant);
    if (!SafeC) {
      // TODO: Should this be available as a constant utility function? It is
      // similar to getBinOpAbsorber().
      if (IsRHSConstant) {
        switch (Opcode) {
        case Instruction::SRem: // X % 1 = 0
        case Instruction::URem: // X %u 1 = 0
          SafeC = ConstantInt::get(EltTy, 1);
          break;
        case Instruction::FRem: // X % 1.0 (doesn't simplify, but it is safe)
          SafeC = ConstantFP::get(EltTy, 1.0);
          break;
        default:
          llvm_unreachable(
              "Only rem opcodes have no identity constant for RHS");
        }
      } else {
        switch (Opcode) {
        case Instruction::Shl:  // 0 << X = 0
        case Instruction::LShr: // 0 >>u X = 0
        case Instruction::AShr: // 0 >> X = 0
        case Instruction::SDiv: // 0 / X = 0
        case Instruction::UDiv: // 0 /u X = 0
        case Instruction::SRem: // 0 % X = 0
        case Instruction::URem: // 0 %u X = 0
        case Instruction::Sub:  // 0 - X (doesn't simplify, but it is safe)
        case Instruction::FSub: // 0.0 - X (doesn't simplify, but it is safe)
        case Instruction::FDiv: // 0.0 / X (doesn't simplify, but it is safe)
        case Instruction::FRem: // 0.0 % X = 0
          SafeC = Constant::getNullValue(EltTy);
          break;
        default:
          llvm_unreachable("Expected to find identity constant for opcode");
        }
      }
    }
    assert(SafeC && "Must have safe constant for binop");
    unsigned NumElts = InVTy->getNumElements();
    SmallVector<Constant *, 16> Out(NumElts);
    for (unsigned i = 0; i != NumElts; ++i) {
      Constant *C = In->getAggregateElement(i);
      Out[i] = isa<UndefValue>(C) ? SafeC : C;
    }
    return ConstantVector::get(Out);
  }

  /// Ignore all operations which only change the sign of a value, returning the
  /// underlying magnitude value.
  /// \param Val Floating-point value whose sign-only ops are stripped.
  /// @return The magnitude value after stripping sign-only operations.
  static Value *stripSignOnlyFPOps(Value *Val) {
    using namespace llvm::PatternMatch;

    match(Val, m_FNeg(m_Value(Val)));
    match(Val, m_FAbs(m_Value(Val)));
    match(Val, m_CopySign(m_Value(Val), m_Value()));
    return Val;
  }

  /// Add \p I to the combiner worklist.
  /// \param I Instruction to schedule for further combining.
  void addToWorklist(Instruction *I) { Worklist.push(I); }

  /// Return the assumption cache used by this combiner.
  /// @return The assumption cache for the function.
  AssumptionCache &getAssumptionCache() const { return AC; }
  /// Return the target library information used by this combiner.
  /// @return The target library information.
  TargetLibraryInfo &getTargetLibraryInfo() const { return TLI; }
  /// Return the dominator tree used by this combiner.
  /// @return The dominator tree for the function.
  DominatorTree &getDominatorTree() const { return DT; }
  /// Return the data layout used by this combiner.
  /// @return The data layout for the module or function.
  const DataLayout &getDataLayout() const { return DL; }
  /// Return the simplify query used by this combiner.
  /// @return The simplify query bundling analyses for value tracking.
  const SimplifyQuery &getSimplifyQuery() const { return SQ; }
  /// Return the optimization-remark emitter used by this combiner.
  /// @return The optimization-remark emitter.
  OptimizationRemarkEmitter &getOptimizationRemarkEmitter() const {
    return ORE;
  }
  /// Return optional block-frequency information, or null if unavailable.
  /// @return Block-frequency info, or null if unavailable.
  BlockFrequencyInfo *getBlockFrequencyInfo() const { return BFI; }
  /// Return optional profile-summary information, or null if unavailable.
  /// @return Profile-summary info, or null if unavailable.
  ProfileSummaryInfo *getProfileSummaryInfo() const { return PSI; }

  /// Attempt a target-specific combine of intrinsic \p II.
  /// \param II Intrinsic call to combine.
  /// @return An optional replacement instruction, or nullopt if unchanged.
  LLVM_ABI std::optional<Instruction *>
  targetInstCombineIntrinsic(IntrinsicInst &II);
  /// Simplify demanded bits of a target-specific intrinsic use.
  /// \param II Intrinsic whose result bits are being demanded.
  /// \param DemandedMask Bits of the result that are demanded.
  /// \param Known Output known-zero and known-one bits.
  /// \param KnownBitsComputed Set when \p Known has been computed.
  /// @return An optional simplified value, or nullopt if unchanged.
  LLVM_ABI std::optional<Value *>
  targetSimplifyDemandedUseBitsIntrinsic(IntrinsicInst &II, APInt DemandedMask,
                                         KnownBits &Known,
                                         bool &KnownBitsComputed);
  /// Simplify demanded vector elements of a target-specific intrinsic.
  /// \param II Intrinsic whose result elements are being demanded.
  /// \param DemandedElts Elements of the result that are demanded.
  /// \param UndefElts Output mask of elements known undef/poison.
  /// \param UndefElts2 Additional undef/poison element mask for operand 2.
  /// \param UndefElts3 Additional undef/poison element mask for operand 3.
  /// \param SimplifyAndSetOp Callback to simplify and rewrite an operand.
  /// @return An optional simplified value, or nullopt if unchanged.
  LLVM_ABI std::optional<Value *> targetSimplifyDemandedVectorEltsIntrinsic(
      IntrinsicInst &II, APInt DemandedElts, APInt &UndefElts,
      APInt &UndefElts2, APInt &UndefElts3,
      std::function<void(Instruction *, unsigned, APInt, APInt &)>
          SimplifyAndSetOp);

  /// Compute the set of backedges in the function.
  LLVM_ABI void computeBackEdges();
  /// Return true if the edge from \p From to \p To is a backedge.
  /// \param From Source basic block of the candidate edge.
  /// \param To Destination basic block of the candidate edge.
  /// @return True if (\p From, \p To) is a backedge.
  bool isBackEdge(const BasicBlock *From, const BasicBlock *To) {
    if (!ComputedBackEdges)
      computeBackEdges();
    return BackEdges.contains({From, To});
  }

  /// Inserts an instruction \p New before instruction \p Old
  ///
  /// Also adds the new instruction to the worklist and returns \p New so that
  /// it is suitable for use as the return from the visitation patterns.
  /// \param New Instruction to insert.
  /// \param Old Insertion point; \p New is inserted before this iterator.
  /// @return \p New after insertion and worklist update.
  Instruction *InsertNewInstBefore(Instruction *New, BasicBlock::iterator Old) {
    assert(New && !New->getParent() &&
           "New instruction already inserted into a basic block!");
    New->insertBefore(Old); // Insert inst
    Worklist.add(New);
    return New;
  }

  /// Same as InsertNewInstBefore, but also sets the debug loc.
  /// \param New Instruction to insert.
  /// \param Old Insertion point whose debug location is copied.
  /// @return \p New after insertion and worklist update.
  Instruction *InsertNewInstWith(Instruction *New, BasicBlock::iterator Old) {
    New->setDebugLoc(Old->getDebugLoc());
    return InsertNewInstBefore(New, Old);
  }

  /// A combiner-aware RAUW-like routine.
  ///
  /// This method is to be used when an instruction is found to be dead,
  /// replaceable with another preexisting expression. Here we add all uses of
  /// I to the worklist, replace all uses of I with the new value, then return
  /// I, so that the inst combiner will know that I was modified.
  /// \param I Instruction whose uses are replaced.
  /// \param V Replacement value for all uses of \p I.
  /// @return \p I if uses were replaced, or null if \p I had no uses.
  Instruction *replaceInstUsesWith(Instruction &I, Value *V) {
    // If there are no uses to replace, then we return nullptr to indicate that
    // no changes were made to the program.
    if (I.use_empty()) return nullptr;

    Worklist.pushUsersToWorkList(I); // Add all modified instrs to worklist.

    // If we are replacing the instruction with itself, this must be in a
    // segment of unreachable code, so just clobber the instruction.
    if (&I == V)
      V = PoisonValue::get(I.getType());

    LLVM_DEBUG(dbgs() << "IC: Replacing " << I << "\n"
                      << "    with " << *V << '\n');

    // If V is a new unnamed instruction, take the name from the old one.
    if (V->use_empty() && isa<Instruction>(V) && !V->hasName() && I.hasName())
      V->takeName(&I);

    I.replaceAllUsesWith(V);
    return &I;
  }

  /// Replace operand of instruction and add old operand to the worklist.
  /// \param I Instruction whose operand is replaced.
  /// \param OpNum Operand index to replace.
  /// \param V New operand value.
  /// @return \p I, so the combiner knows the instruction was modified.
  Instruction *replaceOperand(Instruction &I, unsigned OpNum, Value *V) {
    Value *OldOp = I.getOperand(OpNum);
    I.setOperand(OpNum, V);
    Worklist.handleUseCountDecrement(OldOp);
    return &I;
  }

  /// Replace use and add the previously used value to the worklist.
  /// \param U Use to rewrite.
  /// \param NewValue Value that \p U should refer to.
  void replaceUse(Use &U, Value *NewValue) {
    Value *OldOp = U;
    U = NewValue;
    Worklist.handleUseCountDecrement(OldOp);
  }

  /// Combiner aware instruction erasure.
  ///
  /// When dealing with an instruction that has side effects or produces a void
  /// value, we can't rely on DCE to delete the instruction. Instead, visit
  /// methods should return the value returned by this function.
  /// \param I Instruction to erase from the function.
  /// @return Null, indicating the instruction was erased.
  virtual Instruction *eraseInstFromFunction(Instruction &I) = 0;

  /// Compute known bits for \p V into \p Known using combiner context.
  /// \param V Value to analyze.
  /// \param Known Output known-zero and known-one bits.
  /// \param CxtI Optional context instruction for local analysis.
  /// \param Depth Current recursion depth for this query.
  void computeKnownBits(const Value *V, KnownBits &Known,
                        const Instruction *CxtI, unsigned Depth = 0) const {
    llvm::computeKnownBits(V, Known, SQ.getWithInstruction(CxtI), Depth);
  }

  /// Compute and return known bits for \p V using combiner context.
  /// \param V Value to analyze.
  /// \param CxtI Optional context instruction for local analysis.
  /// \param Depth Current recursion depth for this query.
  /// @return Known-zero and known-one bits of \p V.
  KnownBits computeKnownBits(const Value *V, const Instruction *CxtI,
                             unsigned Depth = 0) const {
    return llvm::computeKnownBits(V, SQ.getWithInstruction(CxtI), Depth);
  }

  /// Return true if \p V is known to be a power of two (or zero).
  /// \param V Value to test for being a power of two.
  /// \param OrZero Also accept zero as a successful result.
  /// \param CxtI Optional context instruction for local analysis.
  /// \param Depth Current recursion depth for this query.
  /// @return True if \p V is known to be a power of two (or zero if allowed).
  bool isKnownToBeAPowerOfTwo(const Value *V, bool OrZero = false,
                              const Instruction *CxtI = nullptr,
                              unsigned Depth = 0) {
    return llvm::isKnownToBeAPowerOfTwo(V, OrZero, SQ.getWithInstruction(CxtI),
                                        Depth);
  }

  /// Return true if \p V masked by \p Mask is known to be zero.
  /// \param V Value being masked.
  /// \param Mask Bits of \p V that must be proven zero.
  /// \param CxtI Optional context instruction for local analysis.
  /// \param Depth Current recursion depth for this query.
  /// @return True if \p V masked by \p Mask is known zero.
  bool MaskedValueIsZero(const Value *V, const APInt &Mask,
                         const Instruction *CxtI = nullptr,
                         unsigned Depth = 0) const {
    return llvm::MaskedValueIsZero(V, Mask, SQ.getWithInstruction(CxtI), Depth);
  }

  /// Compute the number of known sign bits of \p Op.
  /// \param Op Value whose sign bits are counted.
  /// \param CxtI Optional context instruction for local analysis.
  /// \param Depth Current recursion depth for this query.
  /// @return Number of bits known to match the sign bit of \p Op.
  unsigned ComputeNumSignBits(const Value *Op,
                              const Instruction *CxtI = nullptr,
                              unsigned Depth = 0) const {
    return llvm::ComputeNumSignBits(Op, DL, &AC, CxtI, &DT, Depth);
  }

  /// Compute an upper bound on significant bits needed for \p Op.
  /// \param Op Value whose maximum significant bit width is computed.
  /// \param CxtI Optional context instruction for local analysis.
  /// \param Depth Current recursion depth for this query.
  /// @return Upper bound on the number of significant bits in \p Op.
  unsigned ComputeMaxSignificantBits(const Value *Op,
                                     const Instruction *CxtI = nullptr,
                                     unsigned Depth = 0) const {
    return llvm::ComputeMaxSignificantBits(Op, DL, &AC, CxtI, &DT, Depth);
  }

  /// Return true if the cast from integer to FP can be proven to be exact
  /// for all possible inputs (the conversion does not lose any precision).
  /// \param I Integer-to-FP cast to analyze.
  /// @return True if the cast is exact for all possible inputs.
  LLVM_ABI bool isKnownExactCastIntToFP(CastInst &I) const;
  /// Return true if \p V can be cast exactly to floating-point type \p FPTy.
  /// \param V Integer value being cast.
  /// \param FPTy Destination floating-point type.
  /// \param IsSigned Whether \p V is treated as a signed integer.
  /// \param CxtI Optional context instruction for local analysis.
  /// @return True if the integer-to-FP cast does not lose precision.
  LLVM_ABI bool
  canBeCastedExactlyIntToFP(Value *V, Type *FPTy, bool IsSigned,
                            const Instruction *CxtI = nullptr) const;

  /// Compute whether an unsigned multiply of \p LHS and \p RHS can overflow.
  /// \param LHS Left-hand operand of the multiply.
  /// \param RHS Right-hand operand of the multiply.
  /// \param CxtI Optional context instruction for local analysis.
  /// \param IsNSW Whether the multiply is known NSW.
  /// @return Whether the unsigned multiply can overflow.
  OverflowResult computeOverflowForUnsignedMul(const Value *LHS,
                                               const Value *RHS,
                                               const Instruction *CxtI,
                                               bool IsNSW = false) const {
    return llvm::computeOverflowForUnsignedMul(
        LHS, RHS, SQ.getWithInstruction(CxtI), IsNSW);
  }

  /// Compute whether a signed multiply of \p LHS and \p RHS can overflow.
  /// \param LHS Left-hand operand of the multiply.
  /// \param RHS Right-hand operand of the multiply.
  /// \param CxtI Optional context instruction for local analysis.
  /// @return Whether the signed multiply can overflow.
  OverflowResult computeOverflowForSignedMul(const Value *LHS, const Value *RHS,
                                             const Instruction *CxtI) const {
    return llvm::computeOverflowForSignedMul(LHS, RHS,
                                             SQ.getWithInstruction(CxtI));
  }

  /// Compute whether an unsigned add of \p LHS and \p RHS can overflow.
  /// \param LHS Left-hand operand of the add.
  /// \param RHS Right-hand operand of the add.
  /// \param CxtI Optional context instruction for local analysis.
  /// @return Whether the unsigned add can overflow.
  OverflowResult
  computeOverflowForUnsignedAdd(const WithCache<const Value *> &LHS,
                                const WithCache<const Value *> &RHS,
                                const Instruction *CxtI) const {
    return llvm::computeOverflowForUnsignedAdd(LHS, RHS,
                                               SQ.getWithInstruction(CxtI));
  }

  /// Compute whether a signed add of \p LHS and \p RHS can overflow.
  /// \param LHS Left-hand operand of the add.
  /// \param RHS Right-hand operand of the add.
  /// \param CxtI Optional context instruction for local analysis.
  /// @return Whether the signed add can overflow.
  OverflowResult
  computeOverflowForSignedAdd(const WithCache<const Value *> &LHS,
                              const WithCache<const Value *> &RHS,
                              const Instruction *CxtI) const {
    return llvm::computeOverflowForSignedAdd(LHS, RHS,
                                             SQ.getWithInstruction(CxtI));
  }

  /// Compute whether an unsigned subtract of \p LHS and \p RHS can overflow.
  /// \param LHS Left-hand operand of the subtract.
  /// \param RHS Right-hand operand of the subtract.
  /// \param CxtI Optional context instruction for local analysis.
  /// @return Whether the unsigned subtract can overflow.
  OverflowResult computeOverflowForUnsignedSub(const Value *LHS,
                                               const Value *RHS,
                                               const Instruction *CxtI) const {
    return llvm::computeOverflowForUnsignedSub(LHS, RHS,
                                               SQ.getWithInstruction(CxtI));
  }

  /// Compute whether a signed subtract of \p LHS and \p RHS can overflow.
  /// \param LHS Left-hand operand of the subtract.
  /// \param RHS Right-hand operand of the subtract.
  /// \param CxtI Optional context instruction for local analysis.
  /// @return Whether the signed subtract can overflow.
  OverflowResult computeOverflowForSignedSub(const Value *LHS, const Value *RHS,
                                             const Instruction *CxtI) const {
    return llvm::computeOverflowForSignedSub(LHS, RHS,
                                             SQ.getWithInstruction(CxtI));
  }

  /// Simplify operand \p OpNo of \p I based on demanded bits.
  /// \param I Instruction whose operand may be simplified.
  /// \param OpNo Operand index to simplify.
  /// \param DemandedMask Bits of the operand that are demanded.
  /// \param Known Output known-zero and known-one bits for the operand.
  /// \param Q Simplify query providing context for the analysis.
  /// \param Depth Current recursion depth for this query.
  /// @return True if the operand or instruction was simplified.
  virtual bool SimplifyDemandedBits(Instruction *I, unsigned OpNo,
                                    const APInt &DemandedMask, KnownBits &Known,
                                    const SimplifyQuery &Q,
                                    unsigned Depth = 0) = 0;

  /// Simplify operand \p OpNo of \p I based on demanded bits.
  /// \param I Instruction whose operand may be simplified.
  /// \param OpNo Operand index to simplify.
  /// \param DemandedMask Bits of the operand that are demanded.
  /// \param Known Output known-zero and known-one bits for the operand.
  /// @return True if the operand or instruction was simplified.
  bool SimplifyDemandedBits(Instruction *I, unsigned OpNo,
                            const APInt &DemandedMask, KnownBits &Known) {
    return SimplifyDemandedBits(I, OpNo, DemandedMask, Known,
                                SQ.getWithInstruction(I));
  }

  /// Simplify \p V based on which vector elements are demanded.
  /// \param V Value whose demanded elements may be simplified.
  /// \param DemandedElts Elements of \p V that are demanded.
  /// \param UndefElts Output mask of elements known undef/poison.
  /// \param Depth Current recursion depth for this query.
  /// \param AllowMultipleUsers Whether multi-use values may still be simplified.
  /// @return The simplified value, or null if no change was made.
  virtual Value *
  SimplifyDemandedVectorElts(Value *V, APInt DemandedElts, APInt &UndefElts,
                             unsigned Depth = 0,
                             bool AllowMultipleUsers = false) = 0;

  /// Return true if casting from address space \p FromAS to \p ToAS is valid.
  /// \param FromAS Source address space.
  /// \param ToAS Destination address space.
  /// @return True if the address-space cast is valid.
  LLVM_ABI bool isValidAddrSpaceCast(unsigned FromAS, unsigned ToAS) const;
};

} // namespace llvm

#undef DEBUG_TYPE

#endif
