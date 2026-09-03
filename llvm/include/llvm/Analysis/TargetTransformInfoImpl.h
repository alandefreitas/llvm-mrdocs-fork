//===- TargetTransformInfoImpl.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file provides helpers for the implementation of
/// a TargetTransformInfo-conforming class.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_TARGETTRANSFORMINFOIMPL_H
#define LLVM_ANALYSIS_TARGETTRANSFORMINFOIMPL_H

#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PatternMatch.h"
#include <optional>
#include <utility>

namespace llvm {

class Function;

/// Base class for use as a mix-in that aids implementing
/// a TargetTransformInfo-compatible class.
class LLVM_ABI TargetTransformInfoImplBase {

protected:
  /// Alias for TargetTransformInfo used by default TTI implementations.
  typedef TargetTransformInfo TTI;

  /// Data layout for the module being analyzed.
  const DataLayout &DL;

  /// Construct a default TTI implementation over the given data layout.
  /// @param DL Data layout for the module being analyzed.
  explicit TargetTransformInfoImplBase(const DataLayout &DL) : DL(DL) {}

public:
  /// Destroy the default TTI implementation.
  virtual ~TargetTransformInfoImplBase();

  // Provide value semantics. MSVC requires that we spell all of these out.
  /// Copy-construct a default TTI implementation.
  /// @param Arg Source object when copy-constructing.
  TargetTransformInfoImplBase(const TargetTransformInfoImplBase &Arg) = default;
  /// Move-construct a default TTI implementation.
  /// @param Arg Source object being moved from.
  TargetTransformInfoImplBase(TargetTransformInfoImplBase &&Arg) : DL(Arg.DL) {}

  /// Return the data layout used by this TTI implementation.
  /// @return The data layout used by this TTI implementation.
  virtual const DataLayout &getDataLayout() const { return DL; }

  // FIXME: It looks like this implementation is dead. All clients appear to
  //  use the (non-const) version from `TargetTransformInfoImplCRTPBase`.
  /// Estimate the cost of a GEP operation when lowered.
  /// @param PointeeType Source element type of the GEP.
  /// @param Ptr Pointer value or SCEV pointer expression being queried.
  /// @param Operands Index operands or other operand values for the query.
  /// @param CostKind Kind of cost to compute.
  /// @param AccessType Hint for the memory access type that may use the GEP.
  /// @return Estimated cost of the GEP operation when lowered.
  virtual InstructionCost getGEPCost(Type *PointeeType, const Value *Ptr,
                                     ArrayRef<const Value *> Operands,
                                     TTI::TargetCostKind CostKind,
                                     Type *AccessType) const {
    // In the basic model, we just assume that all-constant GEPs will be folded
    // into their uses via addressing modes.
    for (const Value *Operand : Operands)
      if (!isa<Constant>(Operand))
        return TTI::TCC_Basic;

    return TTI::TCC_Free;
  }

  /// Estimate the cost of a chain of related pointer values when lowered.
  /// @param Ptrs Pointer values in the chain.
  /// @param Base Base pointer of the chain, if known.
  /// @param Info Known properties of the pointer chain.
  /// @param AccessTy Type of the loads or stores that ultimately use the
  /// pointers.
  /// @param CostKind Kind of cost to compute.
  /// @return Estimated cost of the pointer-chain operations when lowered.
  virtual InstructionCost
  getPointersChainCost(ArrayRef<const Value *> Ptrs, const Value *Base,
                       const TTI::PointersChainInfo &Info, Type *AccessTy,
                       const TTI::TargetCostKind CostKind) const {
    llvm_unreachable("Not implemented");
  }

  /// Estimate the number of case clusters for lowering a switch.
  /// @param SI Switch instruction being analyzed.
  /// @param JTSize Set to the estimated jump-table size.
  /// @param PSI Optional profile summary information.
  /// @param BFI Optional block frequency information.
  /// @return The estimated number of case clusters when lowering \p SI.
  virtual unsigned
  getEstimatedNumberOfCaseClusters(const SwitchInst &SI, unsigned &JTSize,
                                   ProfileSummaryInfo *PSI,
                                   BlockFrequencyInfo *BFI) const {
    (void)PSI;
    (void)BFI;
    JTSize = 0;
    return SI.getNumCases();
  }

  /// Estimate the cost of a given IR user when lowered.
  /// @param U IR user whose lowering cost is estimated.
  /// @param Operands Index operands or other operand values for the query.
  /// @param CostKind Kind of cost to compute.
  /// @return Estimated cost of the IR user when lowered.
  virtual InstructionCost
  getInstructionCost(const User *U, ArrayRef<const Value *> Operands,
                     TTI::TargetCostKind CostKind) const {
    llvm_unreachable("Not implemented");
  }

  /// Return a multiplier applied to the inlining threshold for this target.
  /// @return A multiplier applied to the inlining threshold for this target.
  virtual unsigned getInliningThresholdMultiplier() const { return 1; }
  /// Return the savings multiplier used by inlining cost-benefit analysis.
  /// @return The savings multiplier used by inlining cost-benefit analysis.
  virtual unsigned getInliningCostBenefitAnalysisSavingsMultiplier() const {
    return 8;
  }
  /// Return the profitable multiplier used by inlining cost-benefit analysis.
  /// @return The profitability multiplier used by inlining cost-benefit analysis.
  virtual unsigned getInliningCostBenefitAnalysisProfitableMultiplier() const {
    return 8;
  }
  /// Return the bonus applied when inlining the last call to a static function.
  /// @return The bonus of inlining the last call to a static function.
  virtual int getInliningLastCallToStaticBonus() const {
    // This is the value of InlineConstants::LastCallToStaticBonus before it was
    // removed along with the introduction of this function.
    return 15000;
  }
  /// Return a target-specific adjustment to the inlining threshold for a call.
  /// @param CB Call site being queried.
  /// @return A value to be added to the inlining threshold.
  virtual unsigned adjustInliningThreshold(const CallBase *CB) const {
    return 0;
  }
  /// Return the extra inlining cost of a caller alloca relative to a call.
  /// @param CB Call site being queried.
  /// @param AI Alloca in the caller related to the call.
  /// @return The cost of leaving an Alloca in the caller when not inlined.
  virtual unsigned getCallerAllocaCost(const CallBase *CB,
                                       const AllocaInst *AI) const {
    return 0;
  };

  /// Return the percent bonus for inlining vector-dense callees.
  /// @return The vector-instruction inlining bonus as a percent.
  virtual int getInlinerVectorBonusPercent() const { return 150; }

  /// Estimate the cost of a memcpy intrinsic instruction.
  /// @param I Instruction providing context for the query.
  /// @return The expected cost of a memcpy.
  virtual InstructionCost getMemcpyCost(const Instruction *I) const {
    return TTI::TCC_Expensive;
  }

  /// Return the max memcpy/memset size inlined as a sequence of loads and
  /// stores.
  /// @return The maximum memset / memcpy size in bytes that still makes it.
  virtual uint64_t getMaxMemIntrinsicInlineSizeThreshold() const { return 64; }

  // Although this default value is arbitrary, it is not random. It is assumed
  // that a condition that evaluates the same way by a higher percentage than
  // this is best represented as control flow. Therefore, the default value N
  // should be set such that the win from N% correct executions is greater than
  // the loss from (100 - N)% mispredicted executions for the majority of
  //  intended targets.
  /// Return the probability threshold for treating a branch as predictable.
  /// @return Branch probability threshold above which a branch or select is considered predictable.
  virtual BranchProbability getPredictableBranchThreshold() const {
    return BranchProbability(99, 100);
  }

  /// Return the estimated latency penalty of a branch misprediction.
  /// @return Estimated branch-misprediction penalty in latency.
  virtual InstructionCost getBranchMispredictPenalty() const { return 0; }

  /// Return true if the target has divergent control flow for the given
  /// function.
  /// @param F Function being queried.
  /// @return True if branch divergence exists.
  virtual bool hasBranchDivergence(const Function *F = nullptr) const {
    return false;
  }

  /// Return target-specific uniformity information for a value.
  /// @param V Value being queried.
  /// @return Target-specific uniformity information for the value.
  virtual ValueUniformity getValueUniformity(const Value *V) const {
    return ValueUniformity::Default;
  }

  /// Return true if a cast between the given address spaces is valid.
  /// @param FromAS Source address space.
  /// @param ToAS Destination address space.
  /// @return True if the address-space cast is valid on this target.
  virtual bool isValidAddrSpaceCast(unsigned FromAS, unsigned ToAS) const {
    return false;
  }

  /// Return true if pointers in the two address spaces may alias.
  /// @param AS0 First address space.
  /// @param AS1 Second address space.
  /// @return False if a \p AS0 address cannot possibly alias a \p AS1 address.
  virtual bool addrspacesMayAlias(unsigned AS0, unsigned AS1) const {
    return true;
  }

  /// Return the flat address space used by this target, or -1 if none.
  /// @return The flat address space ID, or ~0u if the target has none.
  virtual unsigned getFlatAddressSpace() const { return -1; }

  /// Collect intrinsic operand indexes that may be rewritten for flat
  /// addressing.
  /// @param OpIndexes Filled with operand indexes that may be rewritten.
  /// @param IID Intrinsic ID.
  /// @return True if the intrinsic was handled.
  virtual bool collectFlatAddressOperands(SmallVectorImpl<int> &OpIndexes,
                                          Intrinsic::ID IID) const {
    return false;
  }

  /// Return true if a cast between the given address spaces is a no-op.
  /// @param FromAS Source address space.
  /// @param ToAS Destination address space.
  /// @return True if casting from \p FromAS to \p ToAS is a no-op.
  virtual bool isNoopAddrSpaceCast(unsigned FromAS, unsigned ToAS) const {
    return false;
  }

  /// Compute known bits across an address-space cast.
  /// @param ToAS Destination address space.
  /// @param PtrOp Pointer operand of the cast.
  /// @return Known bits for the source and destination pointers.
  virtual std::pair<KnownBits, KnownBits>
  computeKnownBitsAddrSpaceCast(unsigned ToAS, const Value &PtrOp) const {
    const Type *PtrTy = PtrOp.getType();
    assert(PtrTy->isPtrOrPtrVectorTy() &&
           "expected pointer or pointer vector type");
    unsigned FromAS = PtrTy->getPointerAddressSpace();

    if (DL.isNonIntegralAddressSpace(FromAS))
      return std::pair(KnownBits(DL.getPointerSizeInBits(FromAS)),
                       KnownBits(DL.getPointerSizeInBits(ToAS)));

    KnownBits FromPtrBits;
    if (const AddrSpaceCastInst *CastI = dyn_cast<AddrSpaceCastInst>(&PtrOp)) {
      std::pair<KnownBits, KnownBits> KB = computeKnownBitsAddrSpaceCast(
          CastI->getDestAddressSpace(), *CastI->getPointerOperand());
      FromPtrBits = KB.second;
    } else {
      FromPtrBits = computeKnownBits(&PtrOp, DL, nullptr);
    }

    KnownBits ToPtrBits =
        computeKnownBitsAddrSpaceCast(FromAS, ToAS, FromPtrBits);

    return {FromPtrBits, ToPtrBits};
  }

  /// Compute known bits across an address-space cast.
  /// @param FromAS Source address space.
  /// @param ToAS Destination address space.
  /// @param FromPtrBits Known bits of the pointer in the source address space.
  /// @return Known bits of the resulting pointer in the destination address space.
  virtual KnownBits
  computeKnownBitsAddrSpaceCast(unsigned FromAS, unsigned ToAS,
                                const KnownBits &FromPtrBits) const {
    unsigned ToASBitSize = DL.getPointerSizeInBits(ToAS);

    if (DL.isNonIntegralAddressSpace(FromAS))
      return KnownBits(ToASBitSize);

    // By default, we assume that all valid "larger" (e.g. 64-bit) to "smaller"
    // (e.g. 32-bit) casts work by chopping off the high bits.
    // By default, we do not assume that null results in null again.
    return FromPtrBits.anyextOrTrunc(ToASBitSize);
  }

  /// Return a mask of pointer bits preserved by an address-space cast.
  /// @param SrcAS Source address space.
  /// @param DstAS Destination address space.
  /// @return A mask of pointer bits preserved by an address-space cast.
  virtual APInt getAddrSpaceCastPreservedPtrMask(unsigned SrcAS,
                                                 unsigned DstAS) const {
    return {DL.getPointerSizeInBits(SrcAS), 0};
  }

  /// Return true if globals in this address space may have non-undef
  /// initializers.
  /// @param AS Address space.
  /// @return True if globals in this address space can have initializers other.
  virtual bool
  canHaveNonUndefGlobalInitializerInAddressSpace(unsigned AS) const {
    return AS == 0;
  };

  /// Return an assumed address space for a value, or -1 if unknown.
  /// @param V Value being queried.
  /// @return An assumed address space for value \p V.
  virtual unsigned getAssumedAddrSpace(const Value *V) const { return -1; }

  /// Return true if the target is treated as single-threaded.
  /// @return True if the target is known to be single-threaded.
  virtual bool isSingleThreaded() const { return false; }

  /// Return a predicated base and address space for a value, if any.
  /// @param V Value being queried.
  /// @return A predicated pointer and its address space for value \p V.
  virtual std::pair<const Value *, unsigned>
  getPredicatedAddrSpace(const Value *V) const {
    return std::make_pair(nullptr, -1);
  }

  /// Rewrite an intrinsic when an address-space operand changes.
  /// @param II Intrinsic instruction being rewritten or combined.
  /// @param OldV Previous address-space value.
  /// @param NewV Replacement address-space value.
  /// @return The rewritten value, or nullptr if not handled.
  virtual Value *rewriteIntrinsicWithAddressSpace(IntrinsicInst *II,
                                                  Value *OldV,
                                                  Value *NewV) const {
    return nullptr;
  }

  /// Return true if calls to the function lower to an actual call instruction.
  /// @param F Function being queried.
  /// @return True if calls to the function lower to an actual program function call.
  virtual bool isLoweredToCall(const Function *F) const {
    assert(F && "A concrete function must be provided to this routine.");

    // FIXME: These should almost certainly not be handled here, and instead
    // handled with the help of TLI or the target itself. This was largely
    // ported from existing analysis heuristics here so that such refactorings
    // can take place in the future.

    if (F->isIntrinsic())
      return false;

    if (F->hasLocalLinkage() || !F->hasName())
      return true;

    StringRef Name = F->getName();

    // These will all likely lower to a single selection DAG node.
    // clang-format off
    if (Name == "copysign" || Name == "copysignf" || Name == "copysignl" ||
        Name == "fabs"  || Name == "fabsf"  || Name == "fabsl" ||
        Name == "fmin"  || Name == "fminf"  || Name == "fminl" ||
        Name == "fmax"  || Name == "fmaxf"  || Name == "fmaxl" ||
        Name == "sin"   || Name == "sinf"   || Name == "sinl"  ||
        Name == "cos"   || Name == "cosf"   || Name == "cosl"  ||
        Name == "tan"   || Name == "tanf"   || Name == "tanl"  ||
        Name == "asin"  || Name == "asinf"  || Name == "asinl" ||
        Name == "acos"  || Name == "acosf"  || Name == "acosl" ||
        Name == "atan"  || Name == "atanf"  || Name == "atanl" ||
        Name == "atan2" || Name == "atan2f" || Name == "atan2l"||
        Name == "sinh"  || Name == "sinhf"  || Name == "sinhl" ||
        Name == "cosh"  || Name == "coshf"  || Name == "coshl" ||
        Name == "tanh"  || Name == "tanhf"  || Name == "tanhl" ||
        Name == "sqrt"  || Name == "sqrtf"  || Name == "sqrtl" ||
        Name == "exp10"  || Name == "exp10l"  || Name == "exp10f")
      return false;
    // clang-format on
    // These are all likely to be optimized into something smaller.
    if (Name == "pow" || Name == "powf" || Name == "powl" || Name == "exp2" ||
        Name == "exp2l" || Name == "exp2f" || Name == "floor" ||
        Name == "floorf" || Name == "ceil" || Name == "round" ||
        Name == "ffs" || Name == "ffsl" || Name == "abs" || Name == "labs" ||
        Name == "llabs")
      return false;

    return true;
  }

  /// Return true if converting the loop into a hardware loop is profitable.
  /// @param L Loop being queried.
  /// @param SE Scalar evolution analysis.
  /// @param AC Assumption cache.
  /// @param LibInfo Target library information.
  /// @param HWLoopInfo Hardware loop attributes to fill in.
  /// @return True if converting the loop into a hardware loop is profitable.
  virtual bool isHardwareLoopProfitable(Loop *L, ScalarEvolution &SE,
                                        AssumptionCache &AC,
                                        TargetLibraryInfo *LibInfo,
                                        HardwareLoopInfo &HWLoopInfo) const {
    return false;
  }

  /// Return the minimum VF for which epilogue vectorization is considered.
  /// @return Minimum vectorization factor for which epilogue vectorization should be used.
  virtual unsigned getEpilogueVectorizationMinVF() const { return 16; }

  /// Return true if tail folding is preferred over an epilogue loop.
  /// @param TFI Tail-folding information for the loop.
  /// @return True if creating a tail-folded loop is preferred over an epilogue.
  virtual bool preferTailFoldingOverEpilogue(TailFoldingInfo *TFI) const {
    return false;
  }

  /// Return the preferred style of loop tail folding for this target.
  /// @return Preferred style of tail folding for this target.
  virtual TailFoldingStyle getPreferredTailFoldingStyle() const {
    return TailFoldingStyle::DataWithoutLaneMask;
  }

  /// Attempt target-specific InstCombine folds for a target intrinsic.
  /// @param IC InstCombiner performing the transform.
  /// @param II Intrinsic instruction being rewritten or combined.
  /// @return The combined instruction, nullopt if unchanged, or nullptr to stop further processing.
  virtual std::optional<Instruction *>
  instCombineIntrinsic(InstCombiner &IC, IntrinsicInst &II) const {
    return std::nullopt;
  }

  /// Simplify a target intrinsic given a demanded-bits mask.
  /// @param IC InstCombiner performing the transform.
  /// @param II Intrinsic instruction being rewritten or combined.
  /// @param DemandedMask Bits demanded from the intrinsic result.
  /// @param Known Known bits computed for the result.
  /// @param KnownBitsComputed Set when known bits were computed.
  /// @return The simplified value, or std::nullopt if unchanged.
  virtual std::optional<Value *>
  simplifyDemandedUseBitsIntrinsic(InstCombiner &IC, IntrinsicInst &II,
                                   APInt DemandedMask, KnownBits &Known,
                                   bool &KnownBitsComputed) const {
    return std::nullopt;
  }

  /// Simplify a target intrinsic given demanded vector elements.
  /// @param IC InstCombiner performing the transform.
  /// @param II Intrinsic instruction being rewritten or combined.
  /// @param DemandedElts Demanded vector elements.
  /// @param UndefElts Elements known undef in the first operand.
  /// @param UndefElts2 Elements known undef in the second operand.
  /// @param UndefElts3 Elements known undef in the third operand.
  /// @param SimplifyAndSetOp Callback used to simplify and update an operand.
  /// @return The simplified value, or std::nullopt if unchanged.
  virtual std::optional<Value *> simplifyDemandedVectorEltsIntrinsic(
      InstCombiner &IC, IntrinsicInst &II, APInt DemandedElts, APInt &UndefElts,
      APInt &UndefElts2, APInt &UndefElts3,
      std::function<void(Instruction *, unsigned, APInt, APInt &)>
          SimplifyAndSetOp) const {
    return std::nullopt;
  }

  /// Fill in target-specific loop unrolling preferences.
  /// @param L Loop being queried.
  /// @param SE Scalar evolution analysis.
  /// @param UP Unrolling preferences to populate.
  /// @param ORE Optimization remark emitter.
  virtual void getUnrollingPreferences(Loop *L, ScalarEvolution &SE,
                                       TTI::UnrollingPreferences &UP,
                                       OptimizationRemarkEmitter *ORE) const {}

  /// Fill in target-specific loop peeling preferences.
  /// @param L Loop being queried.
  /// @param SE Scalar evolution analysis.
  /// @param PP Peeling preferences to populate.
  virtual void getPeelingPreferences(Loop *L, ScalarEvolution &SE,
                                     TTI::PeelingPreferences &PP) const {}

  /// Return true if the immediate is a legal add immediate.
  /// @param Imm Immediate value being tested or costed.
  /// @return True if \p Imm is a legal immediate operand of an add.
  virtual bool isLegalAddImmediate(int64_t Imm) const { return false; }

  /// Return true if the scalable immediate is a legal add immediate.
  /// @param Imm Immediate value being tested or costed.
  /// @return True if adding \p Imm scaled by vscale is a legal add immediate.
  virtual bool isLegalAddScalableImmediate(int64_t Imm) const { return false; }

  /// Return true if the immediate is a legal icmp immediate.
  /// @param Imm Immediate value being tested or costed.
  /// @return True if \p Imm is a legal immediate operand of an icmp.
  virtual bool isLegalICmpImmediate(int64_t Imm) const { return false; }

  /// Return true if the described addressing mode is legal for this target.
  /// @param Ty Type involved in the query.
  /// @param BaseGV Optional global base of the addressing mode.
  /// @param BaseOffset Constant base offset of the addressing mode.
  /// @param HasBaseReg Whether a base register is present.
  /// @param Scale Scaling factor applied to the index.
  /// @param AddrSpace Address space of the pointer.
  /// @param I Instruction providing context for the query.
  /// @param ScalableOffset Scalable component of the base offset.
  /// @return True if the given addressing mode is legal for a load/store of \p Ty.
  virtual bool isLegalAddressingMode(Type *Ty, GlobalValue *BaseGV,
                                     int64_t BaseOffset, bool HasBaseReg,
                                     int64_t Scale, unsigned AddrSpace,
                                     Instruction *I = nullptr,
                                     int64_t ScalableOffset = 0) const {
    // Guess that only reg and reg+reg addressing is allowed. This heuristic is
    // taken from the implementation of LSR.
    return !BaseGV && BaseOffset == 0 && (Scale == 0 || Scale == 1);
  }

  /// Return true if LSR cost C1 is considered cheaper than C2.
  /// @param C1 First LSR cost.
  /// @param C2 Second LSR cost.
  /// @return True if LSR cost of C1 is lower than C2.
  virtual bool isLSRCostLess(const TTI::LSRCost &C1,
                             const TTI::LSRCost &C2) const {
    return std::tie(C1.NumRegs, C1.AddRecCost, C1.NumIVMuls, C1.NumBaseAdds,
                    C1.ScaleCost, C1.ImmCost, C1.SetupCost) <
           std::tie(C2.NumRegs, C2.AddRecCost, C2.NumIVMuls, C2.NumBaseAdds,
                    C2.ScaleCost, C2.ImmCost, C2.SetupCost);
  }

  /// Return true if register count is the primary LSR cost metric.
  /// @return True if LSR should treat the number of registers as the major cost.
  virtual bool isNumRegsMajorCostOfLSR() const { return true; }

  /// Return true if LSR should discard a less profitable found solution.
  /// @return True if LSR should drop a found solution if it's calculated to be.
  virtual bool shouldDropLSRSolutionIfLessProfitable() const { return false; }

  /// Return true if the instruction is a profitable LSR chain element.
  /// @param I Instruction providing context for the query.
  /// @return True if LSR should not optimize a chain that includes \p I.
  virtual bool isProfitableLSRChainElement(Instruction *I) const {
    return false;
  }

  /// Return true if the target can macro-fuse a compare and branch.
  /// @return True if the target can fuse a compare and branch.
  virtual bool canMacroFuseCmp() const { return false; }

  /// Return true if the target can save a loop-count compare.
  /// @param L Loop being queried.
  /// @param BI Set to the conditional branch that can be saved, if any.
  /// @param SE Scalar evolution analysis.
  /// @param LI Loop info.
  /// @param DT Dominator tree.
  /// @param AC Assumption cache.
  /// @param LibInfo Target library information.
  /// @return True if the target can save a compare for loop count, for example.
  virtual bool canSaveCmp(Loop *L, CondBrInst **BI, ScalarEvolution *SE,
                          LoopInfo *LI, DominatorTree *DT, AssumptionCache *AC,
                          TargetLibraryInfo *LibInfo) const {
    return false;
  }

  /// Return the preferred addressing mode for LSR to generate.
  /// @param L Loop being queried.
  /// @param SE Scalar evolution analysis.
  /// @return The preferred addressing mode LSR should make efforts to generate.
  virtual TTI::AddressingModeKind
  getPreferredAddressingMode(const Loop *L, ScalarEvolution *SE) const {
    return TTI::AMK_None;
  }

  /// Return true if the target supports masked stores of the given type.
  /// @param DataType Element or vector data type of the memory operation.
  /// @param Alignment Required alignment of the access.
  /// @param AddressSpace Address space of the memory operation.
  /// @param MaskKind Kind of mask used by the masked operation.
  /// @return True if the target supports masked store.
  virtual bool isLegalMaskedStore(Type *DataType, Align Alignment,
                                  unsigned AddressSpace,
                                  TTI::MaskKind MaskKind) const {
    return false;
  }

  /// Return true if the target supports masked loads of the given type.
  /// @param DataType Element or vector data type of the memory operation.
  /// @param Alignment Required alignment of the access.
  /// @param AddressSpace Address space of the memory operation.
  /// @param MaskKind Kind of mask used by the masked operation.
  /// @return True if the target supports masked load.
  virtual bool isLegalMaskedLoad(Type *DataType, Align Alignment,
                                 unsigned AddressSpace,
                                 TTI::MaskKind MaskKind) const {
    return false;
  }

  /// Return true if the target supports nontemporal stores of the given type.
  /// @param DataType Element or vector data type of the memory operation.
  /// @param Alignment Required alignment of the access.
  /// @return True if the target supports nontemporal store.
  virtual bool isLegalNTStore(Type *DataType, Align Alignment) const {
    // By default, assume nontemporal memory stores are available for stores
    // that are aligned and have a size that is a power of 2.
    unsigned DataSize = DL.getTypeStoreSize(DataType);
    return Alignment >= DataSize && isPowerOf2_32(DataSize);
  }

  /// Return true if the target supports nontemporal loads of the given type.
  /// @param DataType Element or vector data type of the memory operation.
  /// @param Alignment Required alignment of the access.
  /// @return True if the target supports nontemporal load.
  virtual bool isLegalNTLoad(Type *DataType, Align Alignment) const {
    // By default, assume nontemporal memory loads are available for loads that
    // are aligned and have a size that is a power of 2.
    unsigned DataSize = DL.getTypeStoreSize(DataType);
    return Alignment >= DataSize && isPowerOf2_32(DataSize);
  }

  /// Return true if the target supports broadcast loads of the given shape.
  /// @param ElementTy Element type of the broadcast load.
  /// @param NumElements Element count of the broadcast load.
  /// @return True if the target supports broadcasting a load to a vector.
  virtual bool isLegalBroadcastLoad(Type *ElementTy,
                                    ElementCount NumElements) const {
    return false;
  }

  /// Return true if the target supports masked scatter of the given type.
  /// @param DataType Element or vector data type of the memory operation.
  /// @param Alignment Required alignment of the access.
  /// @return True if the target supports masked scatter.
  virtual bool isLegalMaskedScatter(Type *DataType, Align Alignment) const {
    return false;
  }

  /// Return true if the target supports masked gather of the given type.
  /// @param DataType Element or vector data type of the memory operation.
  /// @param Alignment Required alignment of the access.
  /// @return True if the target supports masked gather.
  virtual bool isLegalMaskedGather(Type *DataType, Align Alignment) const {
    return false;
  }

  /// Return true if masked gathers must be scalarized for this type.
  /// @param DataType Element or vector data type of the memory operation.
  /// @param Alignment Required alignment of the access.
  /// @return True if the target forces scalarizing of llvm.masked.gather.
  virtual bool forceScalarizeMaskedGather(VectorType *DataType,
                                          Align Alignment) const {
    return false;
  }

  /// Return true if masked scatters must be scalarized for this type.
  /// @param DataType Element or vector data type of the memory operation.
  /// @param Alignment Required alignment of the access.
  /// @return True if the target forces scalarizing of llvm.masked.scatter.
  virtual bool forceScalarizeMaskedScatter(VectorType *DataType,
                                           Align Alignment) const {
    return false;
  }

  /// Return true if the target supports masked compress stores.
  /// @param DataType Element or vector data type of the memory operation.
  /// @param Alignment Required alignment of the access.
  /// @return True if the target supports masked compress store.
  virtual bool isLegalMaskedCompressStore(Type *DataType,
                                          Align Alignment) const {
    return false;
  }

  /// Return true if the alternating-opcode pattern can be lowered efficiently.
  /// @param VecTy Vector type involved in the query.
  /// @param Opcode0 First alternating opcode.
  /// @param Opcode1 Second alternating opcode.
  /// @param OpcodeMask Mask selecting which lanes use which opcode.
  /// @return True if an alternating-opcode pattern can be a single instruction.
  virtual bool isLegalAltInstr(VectorType *VecTy, unsigned Opcode0,
                               unsigned Opcode1,
                               const SmallBitVector &OpcodeMask) const {
    return false;
  }

  /// Return true if the target supports masked expand loads.
  /// @param DataType Element or vector data type of the memory operation.
  /// @param Alignment Required alignment of the access.
  /// @return True if the target supports masked expand load.
  virtual bool isLegalMaskedExpandLoad(Type *DataType, Align Alignment) const {
    return false;
  }

  /// Return true if the target supports strided load/store of the given type.
  /// @param DataType Element or vector data type of the memory operation.
  /// @param Alignment Required alignment of the access.
  /// @return True if the target supports strided load.
  virtual bool isLegalStridedLoadStore(Type *DataType, Align Alignment) const {
    return false;
  }

  /// Return true if interleaved access is legal for the given vector type.
  /// @param VTy Vector type involved in the query.
  /// @param Factor Interleave factor.
  /// @param Alignment Required alignment of the access.
  /// @param AddrSpace Address space of the pointer.
  /// @return True is the target supports interleaved access for the given vector.
  virtual bool isLegalInterleavedAccessType(VectorType *VTy, unsigned Factor,
                                            Align Alignment,
                                            unsigned AddrSpace) const {
    return false;
  }

  /// Return true if the target supports masked vector histogram updates.
  /// @param AddrType Type parameter AddrType.
  /// @param DataType Element or vector data type of the memory operation.
  /// @return True if the target supports masked vector histograms.
  virtual bool isLegalMaskedVectorHistogram(Type *AddrType,
                                            Type *DataType) const {
    return false;
  }

  /// Return true if ordered reductions should be enabled for this target.
  /// @return True if we should be enabling ordered reductions for the target.
  virtual bool enableOrderedReductions() const { return false; }

  /// Return true if the target has a combined div/rem operation for the type.
  /// @param DataType Element or vector data type of the memory operation.
  /// @param IsSigned Whether the operation is signed.
  /// @return True if the target has a unified division-and-remainder operation.
  virtual bool hasDivRemOp(Type *DataType, bool IsSigned) const {
    return false;
  }

  /// Return true if the memory instruction has a volatile variant for the
  /// address space.
  /// @param I Instruction providing context for the query.
  /// @param AddrSpace Address space of the pointer.
  /// @return True if the memory access \p I has a volatile variant.
  virtual bool hasVolatileVariant(Instruction *I, unsigned AddrSpace) const {
    return false;
  }

  /// Return true if the target prefers addresses kept in vector form.
  /// @return True if target doesn't mind addresses in vectors.
  virtual bool prefersVectorizedAddressing() const { return true; }

  /// Return the cost of using the given scaling factor in an addressing mode.
  /// @param Ty Type involved in the query.
  /// @param BaseGV Optional global base of the addressing mode.
  /// @param BaseOffset Constant base offset of the addressing mode.
  /// @param HasBaseReg Whether a base register is present.
  /// @param Scale Scaling factor applied to the index.
  /// @param AddrSpace Address space of the pointer.
  /// @return The cost of the scaling factor used in addressing mode AM.
  virtual InstructionCost getScalingFactorCost(Type *Ty, GlobalValue *BaseGV,
                                               StackOffset BaseOffset,
                                               bool HasBaseReg, int64_t Scale,
                                               unsigned AddrSpace) const {
    // Guess that all legal addressing mode are free.
    if (isLegalAddressingMode(Ty, BaseGV, BaseOffset.getFixed(), HasBaseReg,
                              Scale, AddrSpace, /*I=*/nullptr,
                              BaseOffset.getScalable()))
      return 0;
    return InstructionCost::getInvalid();
  }

  /// Return true if LSR should use instruction-level TTI queries.
  /// @return True if LSR should query isLegalAddressingMode with an Instruction.
  virtual bool LSRWithInstrQueries() const { return false; }

  /// Return true if truncating from Ty1 to Ty2 is free on this target.
  /// @param Ty1 Source type being truncated.
  /// @param Ty2 Destination type after truncation.
  /// @return True if it's free to truncate a value of type Ty1 to type Ty2.
  virtual bool isTruncateFree(Type *Ty1, Type *Ty2) const { return false; }

  /// Return true if hoisting the instruction is profitable on this target.
  /// @param I Instruction providing context for the query.
  /// @return True if it is profitable to hoist instruction in the.
  virtual bool isProfitableToHoist(Instruction *I) const { return true; }

  /// Return true if alias analysis should be used for this target.
  /// @return True if alias analysis should be used in target-specific transforms.
  virtual bool useAA() const { return false; }

  /// Return true if the type is legal for this target.
  /// @param Ty Type involved in the query.
  /// @return True if this type is legal.
  virtual bool isTypeLegal(Type *Ty) const { return false; }

  /// Return the estimated number of registers needed to hold the type.
  /// @param Ty Type involved in the query.
  /// @return The estimated number of registers required to represent \p Ty.
  virtual unsigned getRegUsageForType(Type *Ty) const { return 1; }

  /// Return true if switches may be lowered to lookup tables.
  /// @return True if switches should be turned into lookup tables for the.
  virtual bool shouldBuildLookupTables() const { return true; }

  /// Return true if a switch on this constant may use a lookup table.
  /// @param C Constant being considered as a lookup-table key.
  /// @return True if switches should be turned into lookup tables.
  virtual bool shouldBuildLookupTablesForConstant(Constant *C) const {
    return true;
  }

  /// Return the minimum integer bit width for switch lookup table entries.
  /// @return The minimum bit width to use for integer switch lookup table.
  virtual unsigned getMinimumLookupTableEntryBitWidth() const { return 8; }

  /// Return true if relative lookup tables should be used.
  /// @return True if lookup tables should be turned into relative lookup tables.
  virtual bool shouldBuildRelLookupTables() const { return false; }

  /// Return true if cold calling convention should be used for cold calls.
  /// @param F Function being queried.
  /// @return True if the input function which is cold at all call sites,.
  virtual bool useColdCCForColdCall(Function &F) const { return false; }

  /// Return true if fastcc should be used for internal calls.
  /// @param F Function being queried.
  /// @return True if the input function is internal, should use fastcc calling.
  virtual bool useFastCCForInternalCall(Function &F) const { return true; }

  /// Return true if the vector intrinsic keeps a scalar operand at the index.
  /// @param ID Intrinsic ID.
  /// @param ScalarOpdIdx Operand index expected to remain scalar.
  /// @return True if the vector form of the intrinsic has a scalar operand at the given argument.
  virtual bool isTargetIntrinsicWithScalarOpAtArg(Intrinsic::ID ID,
                                                  unsigned ScalarOpdIdx) const {
    return false;
  }

  /// Return true if the vector intrinsic is overloaded on the operand type.
  /// @param ID Intrinsic ID.
  /// @param OpdIdx Operand index whose type participates in overloading.
  /// @return True if the vector form of the intrinsic is overloaded at \p OpdIdx.
  virtual bool isTargetIntrinsicWithOverloadTypeAtArg(Intrinsic::ID ID,
                                                      int OpdIdx) const {
    return OpdIdx == -1;
  }

  /// Return true if a struct-returning intrinsic is overloaded at the field.
  /// @param ID Intrinsic ID.
  /// @param RetIdx Result field index that participates in overloading.
  /// @return True if the struct-returning vector intrinsic is overloaded at \p RetIdx.
  virtual bool
  isTargetIntrinsicWithStructReturnOverloadAtField(Intrinsic::ID ID,
                                                   int RetIdx) const {
    return RetIdx == 0;
  }

  /// Estimate the overhead of scalarizing a vector operation.
  /// @param Ty Type involved in the query.
  /// @param DemandedElts Demanded vector elements.
  /// @param Insert Whether to include insert-element overhead.
  /// @param Extract Whether to include extract-element overhead.
  /// @param CostKind Kind of cost to compute.
  /// @param ForPoisonSrc Whether the scalarized source may be poison.
  /// @param VL Optional concrete scalar values being packed.
  /// @param VIC Context hint for how the vector instruction is used.
  /// @return Estimated overhead of scalarizing the instruction.
  virtual InstructionCost getScalarizationOverhead(
      VectorType *Ty, const APInt &DemandedElts, bool Insert, bool Extract,
      TTI::TargetCostKind CostKind, bool ForPoisonSrc = true,
      ArrayRef<Value *> VL = {},
      TTI::VectorInstrContext VIC = TTI::VectorInstrContext::None) const {
    // Default implementation returns 0.
    // BasicTTIImpl provides the actual implementation.
    return 0;
  }

  /// Estimate the overhead of scalarizing operands of the given types.
  /// @param Tys Argument or live types for the query.
  /// @param CostKind Kind of cost to compute.
  /// @param VIC Context hint for how the vector instruction is used.
  /// @return Estimated overhead of scalarizing operands with the given types.
  virtual InstructionCost getOperandsScalarizationOverhead(
      ArrayRef<Type *> Tys, TTI::TargetCostKind CostKind,
      TTI::VectorInstrContext VIC = TTI::VectorInstrContext::None) const {
    return 0;
  }

  /// Return true if element-wise vector load/store is efficient.
  /// @return True if vector element load/store is cheap enough to skip insert/extract.
  virtual bool supportsEfficientVectorElementLoadStore() const { return false; }

  /// Return true if the target supports tail calls.
  /// @return True if the target supports tail calls.
  virtual bool supportsTailCalls() const { return true; }

  /// Return true if the target supports a tail call for the given call site.
  /// @param CB Call site being queried.
  /// @return True if the target supports a tail call on \p CB.
  virtual bool supportsTailCallFor(const CallBase *CB) const {
    llvm_unreachable("Not implemented");
  }

  /// Return true if aggressive interleaved unrolling should be enabled.
  /// @param LoopHasReductions Whether the loop has reductions.
  /// @return True if interleaved unrolling should not be restricted to small loops.
  virtual bool enableAggressiveInterleaving(bool LoopHasReductions) const {
    return false;
  }

  /// Return options controlling expansion of memcmp-like operations.
  /// @param OptSize Whether optimizing for size.
  /// @param IsZeroCmp Whether the memcmp compares against zero.
  /// @return Target options for expanding memcmp.
  virtual TTI::MemCmpExpansionOptions
  enableMemCmpExpansion(bool OptSize, bool IsZeroCmp) const {
    return {};
  }

  /// Return true if the Select Optimization pass should run.
  /// @return True if the Select Optimization pass should be enabled.
  virtual bool enableSelectOptimize() const { return true; }

  /// Return true if the instruction should be treated like a select.
  /// @param I Instruction providing context for the query.
  /// @return True if SelectOpt should treat \p I like a select.
  virtual bool shouldTreatInstructionLikeSelect(const Instruction *I) const {
    // A select with two constant operands will usually be better left as a
    // select.
    using namespace llvm::PatternMatch;
    if (match(I, m_Select(m_Value(), m_Constant(), m_Constant())))
      return false;
    // If the select is a logical-and/logical-or then it is better treated as a
    // and/or by the backend.
    return isa<SelectInst>(I) &&
           !match(I, m_CombineOr(m_LogicalAnd(m_Value(), m_Value()),
                                 m_LogicalOr(m_Value(), m_Value())));
  }

  /// Return true if interleaved access vectorization is enabled.
  /// @return True if matching of interleaved access groups is enabled.
  virtual bool enableInterleavedAccessVectorization() const { return false; }

  /// Return true if masked interleaved access vectorization is enabled.
  /// @return True if matching of predicated interleaved access groups is enabled.
  virtual bool enableMaskedInterleavedAccessVectorization() const {
    return false;
  }

  /// Return true if FP vectorization may be unsafe on this target.
  /// @return True if auto-vectorizing FP ops may change scalar FP semantics.
  virtual bool isFPVectorizationPotentiallyUnsafe() const { return false; }

  /// Return true if misaligned accesses of the given width are allowed.
  /// @param Context LLVM context.
  /// @param BitWidth Access width in bits.
  /// @param AddressSpace Address space of the memory operation.
  /// @param Alignment Required alignment of the access.
  /// @param Fast Optional out-parameter set when the access is fast.
  /// @return True if the target supports unaligned memory accesses of the given type.
  virtual bool allowsMisalignedMemoryAccesses(LLVMContext &Context,
                                              unsigned BitWidth,
                                              unsigned AddressSpace,
                                              Align Alignment,
                                              unsigned *Fast) const {
    return false;
  }

  /// Return how population-count is supported for the integer width.
  /// @param IntTyWidthInBit Integer type width in bits.
  /// @return Hardware support for population count.
  virtual TTI::PopcntSupportKind
  getPopcntSupport(unsigned IntTyWidthInBit) const {
    return TTI::PSK_Software;
  }

  /// Return true if the target has a fast square-root for the type.
  /// @param Ty Type involved in the query.
  /// @return True if the hardware has a fast square-root instruction.
  virtual bool haveFastSqrt(Type *Ty) const { return false; }

  /// Return true if the target has a fast carry-less multiply for the type.
  /// @param Ty Type involved in the query.
  /// @return True if the hardware has a fast carry-less multiplication.
  virtual bool haveFastClmul(IntegerType *Ty) const { return false; }

  /// Return true if the instruction is too expensive to speculate.
  /// @param I Instruction providing context for the query.
  /// @return True if \p I is too expensive to speculatively execute.
  virtual bool isExpensiveToSpeculativelyExecute(const Instruction *I) const {
    return true;
  }

  /// Return true if ordered fcmp is cheaper than compare-against-zero.
  /// @param Ty Type involved in the query.
  /// @return True if an FP NaN check is cheaper than comparing against 0.0.
  virtual bool isFCmpOrdCheaperThanFCmpZero(Type *Ty) const { return true; }

  /// Estimate the cost of a basic floating-point operation on the type.
  /// @param Ty Type involved in the query.
  /// @return The expected cost of supporting the floating point operation.
  virtual InstructionCost getFPOpCost(Type *Ty) const {
    return TargetTransformInfo::TCC_Basic;
  }

  /// Estimate the code-size cost of materializing an integer immediate.
  /// @param Opcode Instruction opcode.
  /// @param Idx Operand index of the immediate.
  /// @param Imm Immediate value being tested or costed.
  /// @param Ty Type involved in the query.
  /// @return The size-optimization cost of materializing the given integer.
  virtual InstructionCost getIntImmCodeSizeCost(unsigned Opcode, unsigned Idx,
                                                const APInt &Imm,
                                                Type *Ty) const {
    return 0;
  }

  /// Estimate the cost of materializing an integer immediate of the type.
  /// @param Imm Immediate value being tested or costed.
  /// @param Ty Type involved in the query.
  /// @param CostKind Kind of cost to compute.
  /// @return The expected cost of materializing the given integer immediate.
  virtual InstructionCost getIntImmCost(const APInt &Imm, Type *Ty,
                                        TTI::TargetCostKind CostKind) const {
    return TTI::TCC_Basic;
  }

  /// Estimate the cost of an integer immediate used by an instruction.
  /// @param Opcode Instruction opcode.
  /// @param Idx Operand index of the immediate.
  /// @param Imm Immediate value being tested or costed.
  /// @param Ty Type involved in the query.
  /// @param CostKind Kind of cost to compute.
  /// @param Inst Optional instruction using the immediate.
  /// @return The materialization cost of an immediate used by an instruction.
  virtual InstructionCost getIntImmCostInst(unsigned Opcode, unsigned Idx,
                                            const APInt &Imm, Type *Ty,
                                            TTI::TargetCostKind CostKind,
                                            Instruction *Inst = nullptr) const {
    return TTI::TCC_Free;
  }

  /// Estimate the cost of an integer immediate used by an intrinsic.
  /// @param IID Intrinsic ID.
  /// @param Idx Operand index of the immediate.
  /// @param Imm Immediate value being tested or costed.
  /// @param Ty Type involved in the query.
  /// @param CostKind Kind of cost to compute.
  /// @return The materialization cost of an immediate used by an intrinsic.
  virtual InstructionCost
  getIntImmCostIntrin(Intrinsic::ID IID, unsigned Idx, const APInt &Imm,
                      Type *Ty, TTI::TargetCostKind CostKind) const {
    return TTI::TCC_Free;
  }

  /// Return true if constants should stay attached to the instruction.
  /// @param Inst Optional instruction using the immediate.
  /// @param Fn Containing function.
  /// @return True if complex constants should stay attached to \p Inst.
  virtual bool preferToKeepConstantsAttached(const Instruction &Inst,
                                             const Function &Fn) const {
    return false;
  }

  /// Return the number of registers in the given register class.
  /// @param ClassID Register class identifier.
  /// @return The number of registers in the target-provided register class.
  virtual unsigned getNumberOfRegisters(unsigned ClassID) const { return 8; }
  /// Return true if conditional load/store is available for the type.
  /// @param Ty Type involved in the query.
  /// @param IsStore True when querying conditional stores rather than loads.
  /// @return True if the target supports fault-suppressing predicated load/store.
  virtual bool hasConditionalLoadStoreForType(Type *Ty, bool IsStore) const {
    return false;
  }

  /// Return the register class ID used for the type.
  /// @param Vector True when selecting a vector register class.
  /// @param Ty Type involved in the query.
  /// @return The target-provided register class ID for the provided type.
  virtual unsigned getRegisterClassForType(bool Vector,
                                           Type *Ty = nullptr) const {
    return Vector ? 1 : 0;
  }

  /// Return a printable name for the register class ID.
  /// @param ClassID Register class identifier.
  /// @return The target-provided register class name.
  virtual const char *getRegisterClassName(unsigned ClassID) const {
    switch (ClassID) {
    default:
      return "Generic::Unknown Register Class";
    case 0:
      return "Generic::ScalarRC";
    case 1:
      return "Generic::VectorRC";
    }
  }

  /// Estimate the cost of spilling a register from the class.
  /// @param ClassID Register class identifier.
  /// @param CostKind Kind of cost to compute.
  /// @return The cost of spilling a register in the given class to the stack.
  virtual InstructionCost
  getRegisterClassSpillCost(unsigned ClassID,
                            TTI::TargetCostKind CostKind) const {
    return TTI::TCC_Basic;
  }

  /// Estimate the cost of reloading a register into the class.
  /// @param ClassID Register class identifier.
  /// @param CostKind Kind of cost to compute.
  /// @return The cost of reloading a register in the given class from the stack.
  virtual InstructionCost
  getRegisterClassReloadCost(unsigned ClassID,
                             TTI::TargetCostKind CostKind) const {
    return TTI::TCC_Basic;
  }

  /// Return the bit width of registers of the given kind.
  /// @param K Register kind.
  /// @return The width of the largest scalar or vector register type.
  virtual TypeSize
  getRegisterBitWidth(TargetTransformInfo::RegisterKind K) const {
    return TypeSize::get(32, K == TargetTransformInfo::RGK_ScalableVector);
  }

  /// Return the minimum vector register bit width for this target.
  /// @return The width of the smallest vector register type.
  virtual unsigned getMinVectorRegisterBitWidth() const { return 128; }

  /// Return the maximum vscale value, if known.
  /// @return The architectural maximum vscale, if the target specifies one.
  virtual std::optional<unsigned> getMaxVScale() const { return std::nullopt; }
  /// Return the vscale value used for cost tuning, if known.
  /// @return The vscale value used to tune the cost model.
  virtual std::optional<unsigned> getVScaleForTuning() const {
    return std::nullopt;
  }

  /// Return true if vectorization should maximize bandwidth for the kind.
  /// @param K Register kind.
  /// @return True if VF should match the smallest element type to a register.
  virtual bool
  shouldMaximizeVectorBandwidth(TargetTransformInfo::RegisterKind K) const {
    return false;
  }

  /// Return the minimum vectorization factor for the element width.
  /// @param ElemWidth Element bit width.
  /// @param IsScalable Whether a scalable VF is requested.
  /// @return The minimum VF for the given element width, or 0 if there is none.
  virtual ElementCount getMinimumVF(unsigned ElemWidth, bool IsScalable) const {
    return ElementCount::get(0, IsScalable);
  }

  /// Return the maximum vectorization factor for the element width and opcode.
  /// @param ElemWidth Element bit width.
  /// @param Opcode Instruction opcode.
  /// @return The maximum VF for the given element width and opcode, or 0.
  virtual unsigned getMaximumVF(unsigned ElemWidth, unsigned Opcode) const {
    return 0;
  }
  /// Return the minimum store VF for the given scalar memory and value types.
  /// @param VF Vectorization factor.
  /// @param ScalarMemTy Scalar memory type of the store.
  /// @param ScalarValTy Scalar value type of the store.
  /// @param Alignment Required alignment of the access.
  /// @param AddrSpace Address space of the pointer.
  /// @return The minimum vectorization factor for a store instruction.
  virtual unsigned getStoreMinimumVF(unsigned VF, Type *ScalarMemTy,
                                     Type *ScalarValTy, Align Alignment,
                                     unsigned AddrSpace) const {
    return VF;
  }

  /// Return true if address type promotion should be considered for the
  /// instruction.
  /// @param I Instruction providing context for the query.
  /// @param AllowPromotionWithoutCommonHeader Set when promotion is profitable
  /// without a common header.
  /// @return True if address type promotion should be considered for \p I.
  virtual bool shouldConsiderAddressTypePromotion(
      const Instruction &I, bool &AllowPromotionWithoutCommonHeader) const {
    AllowPromotionWithoutCommonHeader = false;
    return false;
  }

  /// Return the cache line size in bytes, or 0 if unknown.
  /// @return The size of a cache line in bytes.
  virtual unsigned getCacheLineSize() const { return 0; }
  /// Return the size of the given cache level, if known.
  /// @param Level Cache level.
  /// @return The size of the cache level in bytes, if available.
  virtual std::optional<unsigned>
  getCacheSize(TargetTransformInfo::CacheLevel Level) const {
    switch (Level) {
    case TargetTransformInfo::CacheLevel::L1D:
      [[fallthrough]];
    case TargetTransformInfo::CacheLevel::L2D:
      return std::nullopt;
    }
    llvm_unreachable("Unknown TargetTransformInfo::CacheLevel");
  }

  /// Return the associativity of the given cache level, if known.
  /// @param Level Cache level.
  /// @return The associativity of the cache level, if available.
  virtual std::optional<unsigned>
  getCacheAssociativity(TargetTransformInfo::CacheLevel Level) const {
    switch (Level) {
    case TargetTransformInfo::CacheLevel::L1D:
      [[fallthrough]];
    case TargetTransformInfo::CacheLevel::L2D:
      return std::nullopt;
    }

    llvm_unreachable("Unknown TargetTransformInfo::CacheLevel");
  }

  /// Return the minimum page size in bytes, if known.
  /// @return The minimum architectural page size for the target.
  virtual std::optional<unsigned> getMinPageSize() const { return {}; }

  /// Return the preferred software prefetch distance.
  /// @return How many instructions before a load a prefetch should be placed.
  virtual unsigned getPrefetchDistance() const { return 0; }
  /// Return the minimum stride for which prefetching is profitable.
  /// @param NumMemAccesses Number of memory accesses in the loop.
  /// @param NumStridedMemAccesses Number of strided memory accesses.
  /// @param NumPrefetches Number of prefetches already planned.
  /// @param HasCall Whether the loop contains a call.
  /// @return The minimum stride in bytes where software prefetching makes sense.
  virtual unsigned getMinPrefetchStride(unsigned NumMemAccesses,
                                        unsigned NumStridedMemAccesses,
                                        unsigned NumPrefetches,
                                        bool HasCall) const {
    return 1;
  }
  /// Return the maximum number of loop iterations to prefetch ahead.
  /// @return The maximum number of iterations to prefetch ahead.
  virtual unsigned getMaxPrefetchIterationsAhead() const { return UINT_MAX; }
  /// Return true if write prefetching should be enabled.
  /// @return True if prefetching should also be done for writes.
  virtual bool enableWritePrefetching() const { return false; }
  /// Return true if the address space should be considered for prefetching.
  /// @param AS Address space.
  /// @return True if the target wants a prefetch in address space \p AS.
  virtual bool shouldPrefetchAddressSpace(unsigned AS) const { return !AS; }

  /// Estimate the cost of a partial reduction pattern.
  /// @param Opcode Instruction opcode.
  /// @param InputTypeA First input type of the partial reduction.
  /// @param InputTypeB Second input type of the partial reduction.
  /// @param AccumType Accumulator type of the partial reduction.
  /// @param VF Vectorization factor.
  /// @param OpAExtend Extend kind applied to the first input.
  /// @param OpBExtend Extend kind applied to the second input.
  /// @param BinOp Optional binary opcode combined with the reduction.
  /// @param CostKind Kind of cost to compute.
  /// @param FMF Fast-math flags for the reduction or operation.
  /// @return The cost of a partial reduction from a vector to a narrower vector.
  virtual InstructionCost getPartialReductionCost(
      unsigned Opcode, Type *InputTypeA, Type *InputTypeB, Type *AccumType,
      ElementCount VF, TTI::PartialReductionExtendKind OpAExtend,
      TTI::PartialReductionExtendKind OpBExtend, std::optional<unsigned> BinOp,
      TTI::TargetCostKind CostKind, std::optional<FastMathFlags> FMF) const {
    return InstructionCost::getInvalid();
  }

  /// Return the maximum interleave factor for the given VF.
  /// @param VF Vectorization factor.
  /// @param HasUnorderedReductions Whether the loop has unordered reductions.
  /// @return The maximum interleave factor any transform should try for this target.
  virtual unsigned getMaxInterleaveFactor(ElementCount VF,
                                          bool HasUnorderedReductions) const {
    return 1;
  }

  /// Estimate the cost of an arithmetic instruction of the given type.
  /// @param Opcode Instruction opcode.
  /// @param Ty Type involved in the query.
  /// @param CostKind Kind of cost to compute.
  /// @param Opd1Info Operand value info for the first operand.
  /// @param Opd2Info Operand value info for the second operand.
  /// @param Args Concrete operand values when available.
  /// @param CxtI Optional context instruction.
  /// @return Estimated reciprocal-throughput cost of the math or logic instruction.
  virtual InstructionCost getArithmeticInstrCost(
      unsigned Opcode, Type *Ty, TTI::TargetCostKind CostKind,
      TTI::OperandValueInfo Opd1Info, TTI::OperandValueInfo Opd2Info,
      ArrayRef<const Value *> Args, const Instruction *CxtI = nullptr) const {
    // Widenable conditions will eventually lower into constants, so some
    // operations with them will be trivially optimized away.
    auto IsWidenableCondition = [](const Value *V) {
      if (auto *II = dyn_cast<IntrinsicInst>(V))
        if (II->getIntrinsicID() == Intrinsic::experimental_widenable_condition)
          return true;
      return false;
    };
    // FIXME: A number of transformation tests seem to require these values
    // which seems a little odd for how arbitary there are.
    switch (Opcode) {
    default:
      break;
    case Instruction::FDiv:
    case Instruction::FRem:
    case Instruction::SDiv:
    case Instruction::SRem:
    case Instruction::UDiv:
    case Instruction::URem:
      // FIXME: Unlikely to be true for CodeSize.
      return TTI::TCC_Expensive;
    case Instruction::And:
    case Instruction::Or:
      if (any_of(Args, IsWidenableCondition))
        return TTI::TCC_Free;
      break;
    }

    // Assume a 3cy latency for fp arithmetic ops.
    if (CostKind == TTI::TCK_Latency)
      if (Ty->getScalarType()->isFloatingPointTy())
        return 3;

    return 1;
  }

  /// Estimate the cost of an alternating-opcode vector instruction.
  /// @param VecTy Vector type involved in the query.
  /// @param Opcode0 First alternating opcode.
  /// @param Opcode1 Second alternating opcode.
  /// @param OpcodeMask Mask selecting which lanes use which opcode.
  /// @param CostKind Kind of cost to compute.
  /// @return The cost of an alternating-opcode pattern lowered as one instruction.
  virtual InstructionCost getAltInstrCost(VectorType *VecTy, unsigned Opcode0,
                                          unsigned Opcode1,
                                          const SmallBitVector &OpcodeMask,
                                          TTI::TargetCostKind CostKind) const {
    return InstructionCost::getInvalid();
  }

  /// Estimate the cost of a vector shuffle of the given kind.
  /// @param Kind Shuffle kind or recurrence kind.
  /// @param DstTy Destination type of the operation.
  /// @param SrcTy Source vector type of the shuffle.
  /// @param CostKind Kind of cost to compute.
  /// @param Mask Shuffle mask.
  /// @param Index Lane or subvector index.
  /// @param SubTp Subvector type for insert/extract subvector shuffles.
  /// @param Args Concrete operand values when available.
  /// @param CxtI Optional context instruction.
  /// @return The cost of a shuffle of kind \p Kind from \p SrcTy to \p DstTy.
  virtual InstructionCost
  getShuffleCost(TTI::ShuffleKind Kind, VectorType *DstTy, VectorType *SrcTy,
                 TTI::TargetCostKind CostKind, ArrayRef<int> Mask, int Index,
                 VectorType *SubTp, ArrayRef<const Value *> Args = {},
                 const Instruction *CxtI = nullptr) const {
    return 1;
  }

  /// Estimate the cost of a cast from Src to Dst.
  /// @param Opcode Instruction opcode.
  /// @param Dst Destination type of the cast or extract.
  /// @param Src Source type of the cast or memory value.
  /// @param CCH Cast context hint.
  /// @param CostKind Kind of cost to compute.
  /// @param I Instruction providing context for the query.
  /// @return The expected cost of cast instructions such as bitcast or trunc.
  virtual InstructionCost getCastInstrCost(unsigned Opcode, Type *Dst,
                                           Type *Src, TTI::CastContextHint CCH,
                                           TTI::TargetCostKind CostKind,
                                           const Instruction *I) const {
    switch (Opcode) {
    default:
      break;
    case Instruction::IntToPtr: {
      unsigned SrcSize = Src->getScalarSizeInBits();
      if (DL.isLegalInteger(SrcSize) &&
          SrcSize <= DL.getPointerTypeSizeInBits(Dst))
        return 0;
      break;
    }
    case Instruction::PtrToAddr: {
      unsigned DstSize = Dst->getScalarSizeInBits();
      assert(DstSize == DL.getAddressSizeInBits(Src));
      if (DL.isLegalInteger(DstSize))
        return 0;
      break;
    }
    case Instruction::PtrToInt: {
      unsigned DstSize = Dst->getScalarSizeInBits();
      if (DL.isLegalInteger(DstSize) &&
          DstSize >= DL.getPointerTypeSizeInBits(Src))
        return 0;
      break;
    }
    case Instruction::BitCast:
      if (Dst == Src || (Dst->isPointerTy() && Src->isPointerTy()))
        // Identity and pointer-to-pointer casts are free.
        return 0;
      break;
    case Instruction::Trunc: {
      // trunc to a native type is free (assuming the target has compare and
      // shift-right of the same width).
      TypeSize DstSize = DL.getTypeSizeInBits(Dst);
      if (!DstSize.isScalable() && DL.isLegalInteger(DstSize.getFixedValue()))
        return 0;
      break;
    }
    }
    return 1;
  }

  /// Estimate the cost of extracting a lane and extending it.
  /// @param Opcode Instruction opcode.
  /// @param Dst Destination type of the cast or extract.
  /// @param VecTy Vector type involved in the query.
  /// @param Index Lane or subvector index.
  /// @param CostKind Kind of cost to compute.
  /// @return The expected cost of a sign- or zero-extended vector extract.
  virtual InstructionCost
  getExtractWithExtendCost(unsigned Opcode, Type *Dst, VectorType *VecTy,
                           unsigned Index, TTI::TargetCostKind CostKind) const {
    return 1;
  }

  /// Estimate the cost of a control-flow instruction opcode.
  /// @param Opcode Instruction opcode.
  /// @param CostKind Kind of cost to compute.
  /// @param I Instruction providing context for the query.
  /// @return The expected cost of control-flow instructions such as Phi or Br.
  virtual InstructionCost getCFInstrCost(unsigned Opcode,
                                         TTI::TargetCostKind CostKind,
                                         const Instruction *I = nullptr) const {
    // A phi would be free, unless we're costing the throughput because it
    // will require a register.
    if (Opcode == Instruction::PHI && CostKind != TTI::TCK_RecipThroughput)
      return 0;
    return 1;
  }

  /// Estimate the cost of a compare or select instruction.
  /// @param Opcode Instruction opcode.
  /// @param ValTy Value type of the compare or select.
  /// @param CondTy Condition type of the select or compare.
  /// @param VecPred Vector predicate when costing a compare.
  /// @param CostKind Kind of cost to compute.
  /// @param Op1Info Operand value info for the first operand.
  /// @param Op2Info Operand value info for the second operand.
  /// @param I Instruction providing context for the query.
  /// @return The expected cost of compare and select instructions.
  virtual InstructionCost getCmpSelInstrCost(
      unsigned Opcode, Type *ValTy, Type *CondTy, CmpInst::Predicate VecPred,
      TTI::TargetCostKind CostKind, TTI::OperandValueInfo Op1Info,
      TTI::OperandValueInfo Op2Info, const Instruction *I) const {
    return 1;
  }

  /// Estimate the cost of a vector insert or extract instruction.
  /// @param Opcode Instruction opcode.
  /// @param Val Vector type of the insert or extract.
  /// @param CostKind Kind of cost to compute.
  /// @param Index Lane or subvector index.
  /// @param Op0 First operand of the vector instruction.
  /// @param Op1 Second operand of the vector instruction.
  /// @param VIC Context hint for how the vector instruction is used.
  /// @return The expected cost of vector Insert and Extract.
  virtual InstructionCost getVectorInstrCost(
      unsigned Opcode, Type *Val, TTI::TargetCostKind CostKind, unsigned Index,
      const Value *Op0, const Value *Op1,
      TTI::VectorInstrContext VIC = TTI::VectorInstrContext::None) const {
    return 1;
  }

  /// Estimate the cost of a vector insert or extract instruction.
  /// @param Opcode Instruction opcode.
  /// @param Val Vector type of the insert or extract.
  /// @param CostKind Kind of cost to compute.
  /// @param Index Lane or subvector index.
  /// @param Scalar Scalar value being inserted or extracted.
  /// @param ScalarUserAndIdx Encodes extracts of Scalar with user and lane;
  /// User may be nullptr when unknown before vectorization.
  /// @param VIC Context hint for how the vector instruction is used.
  /// @return The expected cost of vector Insert and Extract.
  virtual InstructionCost getVectorInstrCost(
      unsigned Opcode, Type *Val, TTI::TargetCostKind CostKind, unsigned Index,
      Value *Scalar,
      ArrayRef<std::tuple<Value *, User *, int>> ScalarUserAndIdx,
      TTI::VectorInstrContext VIC = TTI::VectorInstrContext::None) const {
    return 1;
  }

  /// Estimate the cost of a vector insert or extract instruction.
  /// @param I Instruction providing context for the query.
  /// @param Val Vector type of the insert or extract.
  /// @param CostKind Kind of cost to compute.
  /// @param Index Lane or subvector index.
  /// @param VIC Context hint for how the vector instruction is used.
  /// @return The expected cost of vector Insert and Extract.
  virtual InstructionCost getVectorInstrCost(
      const Instruction &I, Type *Val, TTI::TargetCostKind CostKind,
      unsigned Index,
      TTI::VectorInstrContext VIC = TTI::VectorInstrContext::None) const {
    return 1;
  }

  /// Estimate the cost of a vector insert/extract indexed from the end.
  /// @param Opcode Instruction opcode.
  /// @param Val Vector type of the insert or extract.
  /// @param CostKind Kind of cost to compute.
  /// @param Index Lane or subvector index.
  /// @return The cost of inserting or extracting a lane from the vector end.
  virtual InstructionCost
  getIndexedVectorInstrCostFromEnd(unsigned Opcode, Type *Val,
                                   TTI::TargetCostKind CostKind,
                                   unsigned Index) const {
    return 1;
  }

  /// Estimate the cost of a replication shuffle pattern.
  /// @param EltTy Element type of the shuffle or replication pattern.
  /// @param ReplicationFactor How many times each lane is replicated.
  /// @param VF Vectorization factor.
  /// @param DemandedDstElts Demanded elements of the shuffle result.
  /// @param CostKind Kind of cost to compute.
  /// @return The cost of a replication shuffle of \p VF elements of type \p EltTy.
  virtual InstructionCost
  getReplicationShuffleCost(Type *EltTy, int ReplicationFactor, int VF,
                            const APInt &DemandedDstElts,
                            TTI::TargetCostKind CostKind) const {
    return 1;
  }

  /// Estimate the cost of an insertvalue or extractvalue instruction.
  /// @param Opcode Instruction opcode.
  /// @param CostKind Kind of cost to compute.
  /// @return The expected cost of aggregate inserts and extracts.
  virtual InstructionCost
  getInsertExtractValueCost(unsigned Opcode,
                            TTI::TargetCostKind CostKind) const {
    // Note: The `insertvalue` cost here is chosen to match the default case of
    // getInstructionCost() -- as prior to adding this helper `insertvalue` was
    // not handled.
    if (Opcode == Instruction::InsertValue &&
        CostKind != TTI::TCK_RecipThroughput)
      return TTI::TCC_Basic;
    return TTI::TCC_Free;
  }

  /// Estimate the cost of a load or store of the given type.
  /// @param Opcode Instruction opcode.
  /// @param Src Source type of the cast or memory value.
  /// @param Alignment Required alignment of the access.
  /// @param AddressSpace Address space of the memory operation.
  /// @param CostKind Kind of cost to compute.
  /// @param OpInfo Operand value info for the stored or loaded value.
  /// @param I Instruction providing context for the query.
  /// @return The cost of Load and Store instructions.
  virtual InstructionCost
  getMemoryOpCost(unsigned Opcode, Type *Src, Align Alignment,
                  unsigned AddressSpace, TTI::TargetCostKind CostKind,
                  TTI::OperandValueInfo OpInfo, const Instruction *I) const {
    return 1;
  }

  /// Estimate the cost of an interleaved memory operation.
  /// @param Opcode Instruction opcode.
  /// @param VecTy Vector type involved in the query.
  /// @param Factor Interleave factor.
  /// @param Indices Interleaved access indices.
  /// @param Alignment Required alignment of the access.
  /// @param AddressSpace Address space of the memory operation.
  /// @param CostKind Kind of cost to compute.
  /// @param UseMaskForCond Whether a mask is used for the condition.
  /// @param UseMaskForGaps Whether a mask is used for gaps.
  /// @return The cost of an interleaved memory operation.
  virtual InstructionCost getInterleavedMemoryOpCost(
      unsigned Opcode, Type *VecTy, unsigned Factor, ArrayRef<unsigned> Indices,
      Align Alignment, unsigned AddressSpace, TTI::TargetCostKind CostKind,
      bool UseMaskForCond, bool UseMaskForGaps) const {
    return 1;
  }

  /// Estimate the cost of an intrinsic call with the given attributes.
  /// @param ICA Intrinsic cost attributes.
  /// @param CostKind Kind of cost to compute.
  /// @return The cost of Intrinsic instructions. Analyses the real arguments.
  virtual InstructionCost
  getIntrinsicInstrCost(const IntrinsicCostAttributes &ICA,
                        TTI::TargetCostKind CostKind) const {
    switch (ICA.getID()) {
    default:
      break;
    case Intrinsic::allow_runtime_check:
    case Intrinsic::allow_ubsan_check:
    case Intrinsic::annotation:
    case Intrinsic::assume:
    case Intrinsic::sideeffect:
    case Intrinsic::pseudoprobe:
    case Intrinsic::arithmetic_fence:
    case Intrinsic::dbg_assign:
    case Intrinsic::dbg_declare:
    case Intrinsic::dbg_value:
    case Intrinsic::dbg_label:
    case Intrinsic::invariant_start:
    case Intrinsic::invariant_end:
    case Intrinsic::launder_invariant_group:
    case Intrinsic::strip_invariant_group:
    case Intrinsic::is_constant:
    case Intrinsic::lifetime_start:
    case Intrinsic::lifetime_end:
    case Intrinsic::experimental_noalias_scope_decl:
    case Intrinsic::objectsize:
    case Intrinsic::ptr_annotation:
    case Intrinsic::var_annotation:
    case Intrinsic::experimental_gc_result:
    case Intrinsic::experimental_gc_relocate:
    case Intrinsic::coro_alloc:
    case Intrinsic::coro_begin:
    case Intrinsic::coro_begin_custom_abi:
    case Intrinsic::coro_dead:
    case Intrinsic::coro_id:
    case Intrinsic::coro_id_async:
    case Intrinsic::coro_id_retcon:
    case Intrinsic::coro_id_retcon_once:
    case Intrinsic::coro_noop:
    case Intrinsic::coro_free:
    case Intrinsic::coro_end:
    case Intrinsic::coro_frame:
    case Intrinsic::coro_size:
    case Intrinsic::coro_align:
    case Intrinsic::coro_suspend:
    case Intrinsic::coro_subfn_addr:
    case Intrinsic::threadlocal_address:
    case Intrinsic::experimental_widenable_condition:
    case Intrinsic::ssa_copy:
      // These intrinsics don't actually represent code after lowering.
      return 0;
    case Intrinsic::bswap:
      if (!ICA.getReturnType()->isVectorTy() &&
          !isPowerOf2_64(DL.getTypeSizeInBits(ICA.getReturnType())))
        return InstructionCost::getInvalid();
    }
    return 1;
  }

  /// Estimate the cost of a memory intrinsic with the given attributes.
  /// @param MICA Memory intrinsic cost attributes.
  /// @param CostKind Kind of cost to compute.
  /// @return The cost of memory intrinsic instructions.
  virtual InstructionCost
  getMemIntrinsicInstrCost(const MemIntrinsicCostAttributes &MICA,
                           TTI::TargetCostKind CostKind) const {
    switch (MICA.getID()) {
    case Intrinsic::masked_scatter:
    case Intrinsic::masked_gather:
    case Intrinsic::masked_load:
    case Intrinsic::masked_store:
    case Intrinsic::vp_scatter:
    case Intrinsic::vp_gather:
    case Intrinsic::masked_compressstore:
    case Intrinsic::masked_expandload:
      return 1;
    }
    return InstructionCost::getInvalid();
  }

  /// Estimate the cost of a call with the given signature.
  /// @param F Function being queried.
  /// @param RetTy Return type of the call.
  /// @param Tys Argument or live types for the query.
  /// @param CostKind Kind of cost to compute.
  /// @return The cost of Call instructions.
  virtual InstructionCost getCallInstrCost(Function *F, Type *RetTy,
                                           ArrayRef<Type *> Tys,
                                           TTI::TargetCostKind CostKind) const {
    return 1;
  }

  // Assume that we have a register of the right size for the type.
  /// Return how many registers are needed to hold a value of the type.
  /// @param Tp Type whose register parts are counted.
  /// @return How many parts \p Tp splits into during legalization.
  virtual unsigned getNumberOfParts(Type *Tp) const { return 1; }

  /// Estimate the cost of computing the given address.
  /// @param PtrTy Pointer type of the address computation.
  /// @param SE Scalar evolution analysis.
  /// @param Ptr Pointer value or SCEV pointer expression being queried.
  /// @param CostKind Kind of cost to compute.
  /// @return The cost of computing an address for a memory operation.
  virtual InstructionCost getAddressComputationCost(Type *PtrTy,
                                                    ScalarEvolution *SE,
                                                    const SCEV *Ptr,
                                                    TTI::TargetCostKind CostKind) const {
    return 0;
  }

  /// Estimate the cost of an arithmetic vector reduction.
  /// @param Opcode Instruction opcode.
  /// @param Ty Type involved in the query.
  /// @param FMF Fast-math flags for the reduction or operation.
  /// @param CostKind Kind of cost to compute.
  /// @return Estimated cost of the vector reduction intrinsic.
  virtual InstructionCost
  getArithmeticReductionCost(unsigned Opcode, VectorType *Ty,
                             std::optional<FastMathFlags> FMF,
                             TTI::TargetCostKind CostKind) const {
    return 1;
  }

  /// Estimate the cost of a min/max vector reduction.
  /// @param IID Intrinsic ID.
  /// @param Ty Type involved in the query.
  /// @param FMF Fast-math flags for the reduction or operation.
  /// @param CostKind Kind of cost to compute.
  /// @return The cost of a min/max vector reduction.
  virtual InstructionCost getMinMaxReductionCost(Intrinsic::ID IID,
                                                 VectorType *Ty,
                                                 FastMathFlags FMF,
                                                 TTI::TargetCostKind CostKind) const {
    return 1;
  }

  /// Estimate the cost of an extended vector reduction pattern.
  /// @param Opcode Instruction opcode.
  /// @param IsUnsigned Whether the reduction uses unsigned semantics.
  /// @param ResTy Result scalar type of the reduction.
  /// @param Ty Type involved in the query.
  /// @param FMF Fast-math flags for the reduction or operation.
  /// @param CostKind Kind of cost to compute.
  /// @return The cost of an extended reduction pattern.
  virtual InstructionCost
  getExtendedReductionCost(unsigned Opcode, bool IsUnsigned, Type *ResTy,
                           VectorType *Ty, std::optional<FastMathFlags> FMF,
                           TTI::TargetCostKind CostKind) const {
    return 1;
  }

  /// Estimate the cost of a multiply-accumulate reduction pattern.
  /// @param IsUnsigned Whether the reduction uses unsigned semantics.
  /// @param RedOpcode Reduction opcode.
  /// @param ResTy Result scalar type of the reduction.
  /// @param Ty Type involved in the query.
  /// @param CostKind Kind of cost to compute.
  /// @return The cost of a multiply-accumulate reduction pattern.
  virtual InstructionCost
  getMulAccReductionCost(bool IsUnsigned, unsigned RedOpcode, Type *ResTy,
                         VectorType *Ty, TTI::TargetCostKind CostKind) const {
    return 1;
  }

  /// Estimate the cost of keeping values of the given types live across a call.
  /// @param Tys Argument or live types for the query.
  /// @return The cost of keeping values of the given types alive over a call.
  virtual InstructionCost
  getCostOfKeepingLiveOverCall(ArrayRef<Type *> Tys) const {
    return 0;
  }

  /// Fill MemIntrinsicInfo for a target memory intrinsic, if recognized.
  /// @param Inst Optional instruction using the immediate.
  /// @param Info Known properties of the pointer chain.
  /// @return True if the intrinsic is a supported memory intrinsic.
  virtual bool getTgtMemIntrinsic(IntrinsicInst *Inst,
                                  MemIntrinsicInfo &Info) const {
    return false;
  }

  /// Return the max element size for unordered-atomic memory intrinsics.
  /// @return The max element size in bytes for unordered-atomic mem intrinsics.
  virtual unsigned getAtomicMemIntrinsicMaxElementSize() const {
    // Note for overrides: You must ensure for all element unordered-atomic
    // memory intrinsics that all power-of-2 element sizes up to, and
    // including, the return value of this method have a corresponding
    // runtime lib call. These runtime lib call definitions can be found
    // in RuntimeLibcalls.h
    return 0;
  }

  /// Return or create a value representing the result of a memory intrinsic.
  /// @param Inst Optional instruction using the immediate.
  /// @param ExpectedType Expected result type of the memory intrinsic.
  /// @param CanCreate Whether a new result value may be created.
  /// @return A value holding the result of the given memory intrinsic.
  virtual Value *
  getOrCreateResultFromMemIntrinsic(IntrinsicInst *Inst, Type *ExpectedType,
                                    bool CanCreate = true) const {
    return nullptr;
  }

  /// Return the type used to lower a memcpy loop iteration.
  /// @param Context LLVM context.
  /// @param Length Memcpy length value.
  /// @param SrcAddrSpace Source address space of the memcpy.
  /// @param DestAddrSpace Destination address space of the memcpy.
  /// @param SrcAlign Source alignment.
  /// @param DestAlign Destination alignment.
  /// @param AtomicElementSize Atomic element size in bytes, if any.
  /// @return The type to use in a loop expansion of a memcpy call.
  virtual Type *
  getMemcpyLoopLoweringType(LLVMContext &Context, Value *Length,
                            unsigned SrcAddrSpace, unsigned DestAddrSpace,
                            Align SrcAlign, Align DestAlign,
                            std::optional<uint32_t> AtomicElementSize) const {
    return AtomicElementSize ? Type::getIntNTy(Context, *AtomicElementSize * 8)
                             : Type::getInt8Ty(Context);
  }

  /// Append types used to lower residual bytes of a memcpy loop.
  /// @param OpsOut Filled with residual lowering types.
  /// @param Context LLVM context.
  /// @param RemainingBytes Number of residual bytes to lower.
  /// @param SrcAddrSpace Source address space of the memcpy.
  /// @param DestAddrSpace Destination address space of the memcpy.
  /// @param SrcAlign Source alignment.
  /// @param DestAlign Destination alignment.
  /// @param AtomicCpySize Atomic copy element size in bytes, if any.
  virtual void getMemcpyLoopResidualLoweringType(
      SmallVectorImpl<Type *> &OpsOut, LLVMContext &Context,
      unsigned RemainingBytes, unsigned SrcAddrSpace, unsigned DestAddrSpace,
      Align SrcAlign, Align DestAlign,
      std::optional<uint32_t> AtomicCpySize) const {
    unsigned OpSizeInBytes = AtomicCpySize.value_or(1);
    Type *OpType = Type::getIntNTy(Context, OpSizeInBytes * 8);
    for (unsigned i = 0; i != RemainingBytes; i += OpSizeInBytes)
      OpsOut.push_back(OpType);
  }

  /// Return true if caller and callee are ABI-compatible for inlining.
  /// @param Caller Caller function.
  /// @param Callee Callee function.
  /// @return True if the two functions have compatible attributes for inlining.
  virtual bool areInlineCompatible(const Function *Caller,
                                   const Function *Callee) const {
    return (Caller->getFnAttribute("target-cpu") ==
            Callee->getFnAttribute("target-cpu")) &&
           (Caller->getFnAttribute("target-features") ==
            Callee->getFnAttribute("target-features"));
  }

  /// Return the call penalty used when evaluating inlining of a call.
  /// @param F Function being queried.
  /// @param Call Call being evaluated for inlining.
  /// @param DefaultCallPenalty Default call penalty before target adjustment.
  /// @return A penalty for invoking call \p Call from function \p F.
  virtual unsigned getInlineCallPenalty(const Function *F, const CallBase &Call,
                                        unsigned DefaultCallPenalty) const {
    return DefaultCallPenalty;
  }

  /// Return true if the attribute should be copied when outlining from the
  /// caller.
  /// @param Caller Caller function.
  /// @param Attr Function attribute considered for outlining.
  /// @return True if \p Attr should be copied onto a function outlined from \p Caller.
  virtual bool
  shouldCopyAttributeWhenOutliningFrom(const Function *Caller,
                                       const Attribute &Attr) const {
    // Copy attributes by default
    return true;
  }

  /// Return true if the types are ABI-compatible between caller and callee.
  /// @param Caller Caller function.
  /// @param Callee Callee function.
  /// @param Types Types checked for ABI compatibility.
  /// @return True if caller and callee agree on how \p Types are passed.
  virtual bool areTypesABICompatible(const Function *Caller,
                                     const Function *Callee,
                                     ArrayRef<Type *> Types) const {
    return (Caller->getFnAttribute("target-cpu") ==
            Callee->getFnAttribute("target-cpu")) &&
           (Caller->getFnAttribute("target-features") ==
            Callee->getFnAttribute("target-features"));
  }

  /// Return true if indexed loads of the given mode and type are legal.
  /// @param Mode Indexed memory addressing mode.
  /// @param Ty Type involved in the query.
  /// @return True if the specified indexed load for the given type is legal.
  virtual bool isIndexedLoadLegal(TTI::MemIndexedMode Mode, Type *Ty) const {
    return false;
  }

  /// Return true if indexed stores of the given mode and type are legal.
  /// @param Mode Indexed memory addressing mode.
  /// @param Ty Type involved in the query.
  /// @return True if the specified indexed store for the given type is legal.
  virtual bool isIndexedStoreLegal(TTI::MemIndexedMode Mode, Type *Ty) const {
    return false;
  }

  /// Return the vector register bit width used for load/store in the address
  /// space.
  /// @param AddrSpace Address space of the pointer.
  /// @return The largest vector bitwidth for loads and stores in \p AddrSpace.
  virtual unsigned getLoadStoreVecRegBitWidth(unsigned AddrSpace) const {
    return 128;
  }

  /// Return true if the load may be vectorized.
  /// @param LI Load instruction being considered for vectorization.
  /// @return True if the load instruction is legal to vectorize.
  virtual bool isLegalToVectorizeLoad(LoadInst *LI) const { return true; }

  /// Return true if the store may be vectorized.
  /// @param SI Store instruction being considered for vectorization.
  /// @return True if the store instruction is legal to vectorize.
  virtual bool isLegalToVectorizeStore(StoreInst *SI) const { return true; }

  /// Return true if a load chain of the given size may be vectorized.
  /// @param ChainSizeInBytes Total size of the memory chain in bytes.
  /// @param Alignment Required alignment of the access.
  /// @param AddrSpace Address space of the pointer.
  /// @return True if it is legal to vectorize the given load chain.
  virtual bool isLegalToVectorizeLoadChain(unsigned ChainSizeInBytes,
                                           Align Alignment,
                                           unsigned AddrSpace) const {
    return true;
  }

  /// Return true if a store chain of the given size may be vectorized.
  /// @param ChainSizeInBytes Total size of the memory chain in bytes.
  /// @param Alignment Required alignment of the access.
  /// @param AddrSpace Address space of the pointer.
  /// @return True if it is legal to vectorize the given store chain.
  virtual bool isLegalToVectorizeStoreChain(unsigned ChainSizeInBytes,
                                            Align Alignment,
                                            unsigned AddrSpace) const {
    return true;
  }

  /// Return true if the reduction may be vectorized at the given VF.
  /// @param RdxDesc Reduction descriptor.
  /// @param VF Vectorization factor.
  /// @return True if it is legal to vectorize the given reduction kind.
  virtual bool isLegalToVectorizeReduction(const RecurrenceDescriptor &RdxDesc,
                                           ElementCount VF) const {
    return true;
  }

  /// Return true if the element type is legal in a scalable vector.
  /// @param Ty Type involved in the query.
  /// @return True if the given type is supported for scalable vectors.
  virtual bool isElementTypeLegalForScalableVector(Type *Ty) const {
    return true;
  }

  /// Return the load vectorization factor capped for this target.
  /// @param VF Vectorization factor.
  /// @param LoadSize Size of each scalar load in bytes.
  /// @param ChainSizeInBytes Total size of the memory chain in bytes.
  /// @param VecTy Vector type involved in the query.
  /// @return The adjusted VF if the target cannot use \p SizeInBytes loads.
  virtual unsigned getLoadVectorFactor(unsigned VF, unsigned LoadSize,
                                       unsigned ChainSizeInBytes,
                                       VectorType *VecTy) const {
    return VF;
  }

  /// Return the store vectorization factor capped for this target.
  /// @param VF Vectorization factor.
  /// @param StoreSize Size of each scalar store in bytes.
  /// @param ChainSizeInBytes Total size of the memory chain in bytes.
  /// @param VecTy Vector type involved in the query.
  /// @return The adjusted VF if the target cannot use \p SizeInBytes stores.
  virtual unsigned getStoreVectorFactor(unsigned VF, unsigned StoreSize,
                                        unsigned ChainSizeInBytes,
                                        VectorType *VecTy) const {
    return VF;
  }

  /// Return true if fixed vectors are preferred when costs are equal.
  /// @return True if equal-cost fixed-width vectorization is preferred.
  virtual bool preferFixedOverScalableIfEqualCost() const { return false; }

  /// Return true if in-loop reductions are preferred for the recurrence.
  /// @param Kind Shuffle kind or recurrence kind.
  /// @param Ty Type involved in the query.
  /// @return True if reductions of \p Kind should stay inside the loop.
  virtual bool preferInLoopReduction(RecurKind Kind, Type *Ty) const {
    return false;
  }
  /// Return true if alternating-opcode vectorization is preferred.
  /// @return True if SLP should use alternate-opcode vectorization.
  virtual bool preferAlternateOpcodeVectorization() const { return true; }

  /// Return true if SLP should use the instruction-count profitability check.
  /// @return True if SLP should apply the 2-element instruction-count check.
  virtual bool preferSLPInstCountCheck() const { return true; }

  /// Return true if predicated reduction selects are preferred.
  /// @return True if a predicated reduction select should stay in the loop.
  virtual bool preferPredicatedReductionSelect() const { return false; }

  /// Return true if epilogue vectorization is preferred for the iteration
  /// count.
  /// @param Iters Epilogue iteration vectorization factor.
  /// @return True if a scalar epilogue should still be considered for vectorization.
  virtual bool preferEpilogueVectorization(ElementCount Iters) const {
    // We consider epilogue vectorization unprofitable for targets that
    // don't consider interleaving beneficial (eg. MVE).
    return getMaxInterleaveFactor(Iters, false) > 1;
  }

  /// Return true if register pressure should influence vectorization decisions.
  /// @return True if VFs that exceed register pressure should be discarded.
  virtual bool shouldConsiderVectorizationRegPressure() const { return false; }

  /// Return true if the reduction intrinsic should be expanded.
  /// @param II Intrinsic instruction being rewritten or combined.
  /// @return True if the reduction intrinsic should be expanded to shuffles.
  virtual bool shouldExpandReduction(const IntrinsicInst *II) const {
    return true;
  }

  /// Return the preferred shuffle strategy when expanding a reduction.
  /// @param II Intrinsic instruction being rewritten or combined.
  /// @return The shuffle pattern used to expand the given reduction intrinsic.
  virtual TTI::ReductionShuffle
  getPreferredExpandedReductionShuffle(const IntrinsicInst *II) const {
    return TTI::ReductionShuffle::SplitHalf;
  }

  /// Return the GlobalISel cost of rematerializing a global value.
  /// @return The size cost of rematerializing a GlobalValue versus a reload.
  virtual unsigned getGISelRematGlobalCost() const { return 1; }

  /// Return the minimum trip count for considering tail folding.
  /// @return The minimum trip count to consider vectorizing with tail-folding.
  virtual unsigned getMinTripCountTailFoldingThreshold() const { return 0; }

  /// Return true if the target supports scalable vectors.
  /// @return True if the target supports scalable vectors.
  virtual bool supportsScalableVectors() const { return false; }

  /// Return true if scalable vectorization should be enabled.
  /// @return True when scalable vectorization is preferred.
  virtual bool enableScalableVectorization() const { return false; }

  /// Return true if the target has an active vector length mechanism.
  /// @return True if the target supports the %evl parameter of VP intrinsics.
  virtual bool hasActiveVectorLength() const { return false; }

  /// Return true if sinking the instruction's operands is profitable.
  /// @param I Instruction providing context for the query.
  /// @param Ops Filled with operand uses that are profitable to sink.
  /// @return True if sinking operands of \p I into I's block is profitable.
  virtual bool isProfitableToSinkOperands(Instruction *I,
                                          SmallVectorImpl<Use *> &Ops) const {
    return false;
  }

  /// Return true if vector shifts by a scalar amount are cheap.
  /// @param Ty Type involved in the query.
  /// @return True if a uniform scalar shift is much cheaper than a per-lane one.
  virtual bool isVectorShiftByScalarCheap(Type *Ty) const { return false; }

  /// Return how vector-predicated operations should be legalized.
  /// @param PI Vector-predicated intrinsic being legalized.
  /// @return How the target needs this vector-predicated operation transformed.
  virtual TargetTransformInfo::VPLegalization
  getVPLegalizationStrategy(const VPIntrinsic &PI) const {
    return TargetTransformInfo::VPLegalization(
        /* EVLParamStrategy */ TargetTransformInfo::VPLegalization::Discard,
        /* OperatorStrategy */ TargetTransformInfo::VPLegalization::Convert);
  }

  /// Return true if the target has a wide ARM/Thumb branch.
  /// @param Thumb True when querying Thumb wide-branch support.
  /// @return True if a 32-bit branch instruction is available in Arm or Thumb.
  virtual bool hasArmWideBranch(bool Thumb) const { return false; }

  /// Return the feature bitmask for multi-versioning of the function.
  /// @param F Function being queried.
  /// @return A bitmask constructed from the target-features or fmv-features.
  virtual APInt getFeatureMask(const Function &F) const {
    return APInt::getZero(32);
  }

  /// Return the priority bitmask for multi-versioning of the function.
  /// @param F Function being queried.
  /// @return A bitmask constructed from the target-features or fmv-features.
  virtual APInt getPriorityMask(const Function &F) const {
    return APInt::getZero(32);
  }

  /// Return true if the function is multi-versioned.
  /// @param F Function being queried.
  /// @return True if this is an instance of a function with multiple versions.
  virtual bool isMultiversionedFunction(const Function &F) const {
    return false;
  }

  /// Return the maximum number of arguments supported for a call.
  /// @return The maximum number of function arguments the target supports.
  virtual unsigned getMaxNumArgs() const { return UINT_MAX; }

  /// Return how many padding bytes to append to a global array.
  /// @param Size Current size of the global array in bytes.
  /// @param ArrayType Element type of the global array.
  /// @return How many padding bytes to add for a global array of the given size.
  virtual unsigned getNumBytesToPadGlobalArray(unsigned Size,
                                               Type *ArrayType) const {
    return 0;
  }

  /// Collect kernel launch bounds metadata for the function.
  /// @param F Function being queried.
  /// @param LB Filled with kernel launch bound name/value pairs.
  virtual void collectKernelLaunchBounds(
      const Function &F,
      SmallVectorImpl<std::pair<StringRef, int64_t>> &LB) const {}

  /// Return true if vector element indexing via GEP is allowed.
  /// @return True if GEP should not be used to index into vectors for this.
  virtual bool allowVectorElementIndexingUsingGEP() const { return true; }

  /// Return true if the instruction is uniform given the uniform operand mask.
  /// @param I Instruction providing context for the query.
  /// @param UniformArgs Mask of operands known uniform.
  /// @return True if the operand is uniform across lanes.
  virtual bool isUniform(const Instruction *I,
                         const SmallBitVector &UniformArgs) const {
    llvm_unreachable("target must implement isUniform for Custom uniformity");
  }

protected:
  // Obtain the minimum required size to hold the value (without the sign)
  // In case of a vector it returns the min required size for one element.
  /// Return the minimum bit width needed to hold the value without the sign
  /// bit.
  /// @param Val Constant or cast value whose width is measured.
  /// @param isSigned Set to true when the value requires a sign bit.
  /// @return The minimum required element size in bits.
  unsigned minRequiredElementSize(const Value *Val, bool &isSigned) const {
    if (isa<ConstantDataVector>(Val) || isa<ConstantVector>(Val)) {
      const auto *VectorValue = cast<Constant>(Val);

      // In case of a vector need to pick the max between the min
      // required size for each element
      auto *VT = cast<FixedVectorType>(Val->getType());

      // Assume unsigned elements
      isSigned = false;

      // The max required size is the size of the vector element type
      unsigned MaxRequiredSize =
          VT->getElementType()->getPrimitiveSizeInBits().getFixedValue();

      unsigned MinRequiredSize = 0;
      for (unsigned i = 0, e = VT->getNumElements(); i < e; ++i) {
        if (auto *IntElement =
                dyn_cast<ConstantInt>(VectorValue->getAggregateElement(i))) {
          bool signedElement = IntElement->getValue().isNegative();
          // Get the element min required size.
          unsigned ElementMinRequiredSize =
              IntElement->getValue().getSignificantBits() - 1;
          // In case one element is signed then all the vector is signed.
          isSigned |= signedElement;
          // Save the max required bit size between all the elements.
          MinRequiredSize = std::max(MinRequiredSize, ElementMinRequiredSize);
        } else {
          // not an int constant element
          return MaxRequiredSize;
        }
      }
      return MinRequiredSize;
    }

    if (const auto *CI = dyn_cast<ConstantInt>(Val)) {
      isSigned = CI->getValue().isNegative();
      return CI->getValue().getSignificantBits() - 1;
    }

    if (const auto *Cast = dyn_cast<SExtInst>(Val)) {
      isSigned = true;
      return Cast->getSrcTy()->getScalarSizeInBits() - 1;
    }

    if (const auto *Cast = dyn_cast<ZExtInst>(Val)) {
      isSigned = false;
      return Cast->getSrcTy()->getScalarSizeInBits();
    }

    isSigned = false;
    return Val->getType()->getScalarSizeInBits();
  }

  /// Return true if the SCEV pointer expression is a strided access.
  /// @param Ptr Pointer value or SCEV pointer expression being queried.
  /// @return True if the pointer is a strided SCEV access.
  bool isStridedAccess(const SCEV *Ptr) const {
    return Ptr && isa<SCEVAddRecExpr>(Ptr);
  }

  /// Return the constant stride step of a strided SCEV pointer, if any.
  /// @param SE Scalar evolution analysis.
  /// @param Ptr Pointer value or SCEV pointer expression being queried.
  /// @return The constant stride step, or nullptr if none.
  const SCEVConstant *getConstantStrideStep(ScalarEvolution *SE,
                                            const SCEV *Ptr) const {
    if (!isStridedAccess(Ptr))
      return nullptr;
    const SCEVAddRecExpr *AddRec = cast<SCEVAddRecExpr>(Ptr);
    return dyn_cast<SCEVConstant>(AddRec->getStepRecurrence(*SE));
  }

  /// Return true if the constant stride is strictly less than MergeDistance.
  /// @param SE Scalar evolution analysis.
  /// @param Ptr Pointer value or SCEV pointer expression being queried.
  /// @param MergeDistance Maximum exclusive stride distance.
  /// @return True if the constant stride is strictly less than MergeDistance.
  bool isConstantStridedAccessLessThan(ScalarEvolution *SE, const SCEV *Ptr,
                                       int64_t MergeDistance) const {
    const SCEVConstant *Step = getConstantStrideStep(SE, Ptr);
    if (!Step)
      return false;
    APInt StrideVal = Step->getAPInt();
    if (StrideVal.getBitWidth() > 64)
      return false;
    // FIXME: Need to take absolute value for negative stride case.
    return StrideVal.getSExtValue() < MergeDistance;
  }
};

/// CRTP base class for use as a mix-in that aids implementing
/// a TargetTransformInfo-compatible class.
template <typename T>
class TargetTransformInfoImplCRTPBase : public TargetTransformInfoImplBase {
private:
  typedef TargetTransformInfoImplBase BaseT;

protected:
  /// Construct a CRTP TTI implementation over the given data layout.
  /// @param DL Data layout for the module being analyzed.
  explicit TargetTransformInfoImplCRTPBase(const DataLayout &DL) : BaseT(DL) {}

public:
  /// Estimate the cost of a GEP operation when lowered.
  /// @param PointeeType Source element type of the GEP.
  /// @param Ptr Pointer value or SCEV pointer expression being queried.
  /// @param Operands Index operands or other operand values for the query.
  /// @param CostKind Kind of cost to compute.
  /// @param AccessType Hint for the memory access type that may use the GEP.
  /// @return Estimated cost of a GEP operation when lowered.
  InstructionCost getGEPCost(Type *PointeeType, const Value *Ptr,
                             ArrayRef<const Value *> Operands,
                             TTI::TargetCostKind CostKind,
                             Type *AccessType) const override {
    assert(PointeeType && Ptr && "can't get GEPCost of nullptr");
    auto *BaseGV = dyn_cast<GlobalValue>(Ptr->stripPointerCasts());
    bool HasBaseReg = (BaseGV == nullptr);

    auto PtrSizeBits = DL.getPointerTypeSizeInBits(Ptr->getType());
    APInt BaseOffset(PtrSizeBits, 0);
    int64_t Scale = 0;

    auto GTI = gep_type_begin(PointeeType, Operands);
    Type *TargetType = nullptr;

    // Handle the case where the GEP instruction has a single operand,
    // the basis, therefore TargetType is a nullptr.
    if (Operands.empty())
      return !BaseGV ? TTI::TCC_Free : TTI::TCC_Basic;

    for (auto I = Operands.begin(); I != Operands.end(); ++I, ++GTI) {
      TargetType = GTI.getIndexedType();
      // We assume that the cost of Scalar GEP with constant index and the
      // cost of Vector GEP with splat constant index are the same.
      const ConstantInt *ConstIdx = dyn_cast<ConstantInt>(*I);
      if (!ConstIdx)
        if (auto Splat = getSplatValue(*I))
          ConstIdx = dyn_cast<ConstantInt>(Splat);
      if (StructType *STy = GTI.getStructTypeOrNull()) {
        // For structures the index is always splat or scalar constant
        assert(ConstIdx && "Unexpected GEP index");
        uint64_t Field = ConstIdx->getZExtValue();
        BaseOffset += DL.getStructLayout(STy)->getElementOffset(Field);
      } else {
        // If this operand is a scalable type, bail out early.
        // TODO: Make isLegalAddressingMode TypeSize aware.
        if (TargetType->isScalableTy())
          return TTI::TCC_Basic;
        int64_t ElementSize =
            GTI.getSequentialElementStride(DL).getFixedValue();
        if (ConstIdx) {
          BaseOffset +=
              ConstIdx->getValue().sextOrTrunc(PtrSizeBits) * ElementSize;
        } else {
          // Needs scale register.
          if (Scale != 0)
            // No addressing mode takes two scale registers.
            return TTI::TCC_Basic;
          Scale = ElementSize;
        }
      }
    }

    // If we haven't been provided a hint, use the target type for now.
    //
    // TODO: Take a look at potentially removing this: This is *slightly* wrong
    // as it's possible to have a GEP with a foldable target type but a memory
    // access that isn't foldable. For example, this load isn't foldable on
    // RISC-V:
    //
    // %p = getelementptr i32, ptr %base, i32 42
    // %x = load <2 x i32>, ptr %p
    if (!AccessType)
      AccessType = TargetType;

    // If the final address of the GEP is a legal addressing mode for the given
    // access type, then we can fold it into its users.
    if (static_cast<const T *>(this)->isLegalAddressingMode(
            AccessType, const_cast<GlobalValue *>(BaseGV),
            BaseOffset.sextOrTrunc(64).getSExtValue(), HasBaseReg, Scale,
            Ptr->getType()->getPointerAddressSpace()))
      return TTI::TCC_Free;

    // TODO: Instead of returning TCC_Basic here, we should use
    // getArithmeticInstrCost. Or better yet, provide a hook to let the target
    // model it.
    return TTI::TCC_Basic;
  }

  /// Estimate the cost of a chain of related pointer values when lowered.
  /// @param Ptrs Pointer values in the chain.
  /// @param Base Base pointer of the chain, if known.
  /// @param Info Known properties of the pointer chain.
  /// @param AccessTy Type of the loads or stores that ultimately use the
  /// pointers.
  /// @param CostKind Kind of cost to compute.
  /// @return Estimated cost of a chain of related pointer values when lowered.
  InstructionCost
  getPointersChainCost(ArrayRef<const Value *> Ptrs, const Value *Base,
                       const TTI::PointersChainInfo &Info, Type *AccessTy,
                       TTI::TargetCostKind CostKind) const override {
    InstructionCost Cost = TTI::TCC_Free;
    // In the basic model we take into account GEP instructions only
    // (although here can come alloca instruction, a value, constants and/or
    // constant expressions, PHIs, bitcasts ... whatever allowed to be used as a
    // pointer). Typically, if Base is a not a GEP-instruction and all the
    // pointers are relative to the same base address, all the rest are
    // either GEP instructions, PHIs, bitcasts or constants. When we have same
    // base, we just calculate cost of each non-Base GEP as an ADD operation if
    // any their index is a non-const.
    // If no known dependecies between the pointers cost is calculated as a sum
    // of costs of GEP instructions.
    for (const Value *V : Ptrs) {
      const auto *GEP = dyn_cast<GetElementPtrInst>(V);
      if (!GEP)
        continue;
      if (Info.isSameBase() && V != Base) {
        if (GEP->hasAllConstantIndices())
          continue;
        Cost += static_cast<const T *>(this)->getArithmeticInstrCost(
            Instruction::Add, GEP->getType(), CostKind,
            {TTI::OK_AnyValue, TTI::OP_None}, {TTI::OK_AnyValue, TTI::OP_None},
            {});
      } else {
        SmallVector<const Value *> Indices(GEP->indices());
        Cost += static_cast<const T *>(this)->getGEPCost(
            GEP->getSourceElementType(), GEP->getPointerOperand(), Indices,
            CostKind, AccessTy);
      }
    }
    return Cost;
  }

  /// Estimate the cost of a given IR user when lowered.
  /// @param U IR user whose lowering cost is estimated.
  /// @param Operands Index operands or other operand values for the query.
  /// @param CostKind Kind of cost to compute.
  /// @return Estimated cost of \p U using its current operands.
  InstructionCost
  getInstructionCost(const User *U, ArrayRef<const Value *> Operands,
                     TTI::TargetCostKind CostKind) const override {
    using namespace llvm::PatternMatch;

    auto *TargetTTI = static_cast<const T *>(this);
    // Handle non-intrinsic calls, invokes, and callbr.
    // FIXME: Unlikely to be true for anything but CodeSize.
    auto *CB = dyn_cast<CallBase>(U);
    if (CB && !isa<IntrinsicInst>(U)) {
      if (const Function *F = CB->getCalledFunction()) {
        if (!TargetTTI->isLoweredToCall(F))
          return TTI::TCC_Basic; // Give a basic cost if it will be lowered

        return TTI::TCC_Basic * (F->getFunctionType()->getNumParams() + 1);
      }
      // For indirect or other calls, scale cost by number of arguments.
      return TTI::TCC_Basic * (CB->arg_size() + 1);
    }

    Type *Ty = U->getType();
    unsigned Opcode = Operator::getOpcode(U);
    auto *I = dyn_cast<Instruction>(U);
    switch (Opcode) {
    default:
      break;
    case Instruction::Call: {
      assert(isa<IntrinsicInst>(U) && "Unexpected non-intrinsic call");
      auto *Intrinsic = cast<IntrinsicInst>(U);
      IntrinsicCostAttributes CostAttrs(Intrinsic->getIntrinsicID(), *CB);
      return TargetTTI->getIntrinsicInstrCost(CostAttrs, CostKind);
    }
    case Instruction::UncondBr:
    case Instruction::CondBr:
    case Instruction::Ret:
    case Instruction::PHI:
    case Instruction::Switch:
      return TargetTTI->getCFInstrCost(Opcode, CostKind, I);
    case Instruction::Freeze:
      return TTI::TCC_Free;
    case Instruction::ExtractValue:
    case Instruction::InsertValue:
      return TargetTTI->getInsertExtractValueCost(Opcode, CostKind);
    case Instruction::Alloca:
      if (cast<AllocaInst>(U)->isStaticAlloca())
        return TTI::TCC_Free;
      break;
    case Instruction::GetElementPtr: {
      const auto *GEP = cast<GEPOperator>(U);
      Type *AccessType = nullptr;
      // For now, only provide the AccessType in the simple case where the GEP
      // only has one user.
      if (GEP->hasOneUser() && I)
        AccessType = I->user_back()->getAccessType();

      return TargetTTI->getGEPCost(GEP->getSourceElementType(),
                                   Operands.front(), Operands.drop_front(),
                                   CostKind, AccessType);
    }
    case Instruction::Add:
    case Instruction::FAdd:
    case Instruction::Sub:
    case Instruction::FSub:
    case Instruction::Mul:
    case Instruction::FMul:
    case Instruction::UDiv:
    case Instruction::SDiv:
    case Instruction::FDiv:
    case Instruction::URem:
    case Instruction::SRem:
    case Instruction::FRem:
    case Instruction::Shl:
    case Instruction::LShr:
    case Instruction::AShr:
    case Instruction::And:
    case Instruction::Or:
    case Instruction::Xor:
    case Instruction::FNeg: {
      const TTI::OperandValueInfo Op1Info = TTI::getOperandInfo(Operands[0]);
      TTI::OperandValueInfo Op2Info;
      if (Opcode != Instruction::FNeg)
        Op2Info = TTI::getOperandInfo(Operands[1]);
      return TargetTTI->getArithmeticInstrCost(Opcode, Ty, CostKind, Op1Info,
                                               Op2Info, Operands, I);
    }
    case Instruction::IntToPtr:
    case Instruction::PtrToAddr:
    case Instruction::PtrToInt:
    case Instruction::SIToFP:
    case Instruction::UIToFP:
    case Instruction::FPToUI:
    case Instruction::FPToSI:
    case Instruction::Trunc:
    case Instruction::FPTrunc:
    case Instruction::BitCast:
    case Instruction::FPExt:
    case Instruction::SExt:
    case Instruction::ZExt:
    case Instruction::AddrSpaceCast: {
      Type *OpTy = Operands[0]->getType();
      return TargetTTI->getCastInstrCost(
          Opcode, Ty, OpTy, TTI::getCastContextHint(I), CostKind, I);
    }
    case Instruction::Store: {
      auto *SI = cast<StoreInst>(U);
      Type *ValTy = Operands[0]->getType();
      TTI::OperandValueInfo OpInfo = TTI::getOperandInfo(Operands[0]);
      return TargetTTI->getMemoryOpCost(Opcode, ValTy, SI->getAlign(),
                                        SI->getPointerAddressSpace(), CostKind,
                                        OpInfo, I);
    }
    case Instruction::Load: {
      auto *LI = cast<LoadInst>(U);
      Type *LoadType = U->getType();
      // If there is a non-register sized type, the cost estimation may expand
      // it to be several instructions to load into multiple registers on the
      // target.  But, if the only use of the load is a trunc instruction to a
      // register sized type, the instruction selector can combine these
      // instructions to be a single load.  So, in this case, we use the
      // destination type of the trunc instruction rather than the load to
      // accurately estimate the cost of this load instruction.
      if (CostKind == TTI::TCK_CodeSize && LI->hasOneUse() &&
          !LoadType->isVectorTy()) {
        if (const TruncInst *TI = dyn_cast<TruncInst>(*LI->user_begin()))
          LoadType = TI->getDestTy();
      }
      return TargetTTI->getMemoryOpCost(Opcode, LoadType, LI->getAlign(),
                                        LI->getPointerAddressSpace(), CostKind,
                                        {TTI::OK_AnyValue, TTI::OP_None}, I);
    }
    case Instruction::Select: {
      const Value *Op0, *Op1;
      if (match(U, m_LogicalAnd(m_Value(Op0), m_Value(Op1))) ||
          match(U, m_LogicalOr(m_Value(Op0), m_Value(Op1)))) {
        // select x, y, false --> x & y
        // select x, true, y --> x | y
        const auto Op1Info = TTI::getOperandInfo(Op0);
        const auto Op2Info = TTI::getOperandInfo(Op1);
        assert(Op0->getType()->getScalarSizeInBits() == 1 &&
               Op1->getType()->getScalarSizeInBits() == 1);

        SmallVector<const Value *, 2> Operands{Op0, Op1};
        return TargetTTI->getArithmeticInstrCost(
            match(U, m_LogicalOr()) ? Instruction::Or : Instruction::And, Ty,
            CostKind, Op1Info, Op2Info, Operands, I);
      }
      const auto Op1Info = TTI::getOperandInfo(Operands[1]);
      const auto Op2Info = TTI::getOperandInfo(Operands[2]);
      Type *CondTy = Operands[0]->getType();
      return TargetTTI->getCmpSelInstrCost(Opcode, U->getType(), CondTy,
                                           CmpInst::BAD_ICMP_PREDICATE,
                                           CostKind, Op1Info, Op2Info, I);
    }
    case Instruction::ICmp:
    case Instruction::FCmp: {
      const auto Op1Info = TTI::getOperandInfo(Operands[0]);
      const auto Op2Info = TTI::getOperandInfo(Operands[1]);
      Type *ValTy = Operands[0]->getType();
      // TODO: Also handle ICmp/FCmp constant expressions.
      return TargetTTI->getCmpSelInstrCost(Opcode, ValTy, U->getType(),
                                           I ? cast<CmpInst>(I)->getPredicate()
                                             : CmpInst::BAD_ICMP_PREDICATE,
                                           CostKind, Op1Info, Op2Info, I);
    }
    case Instruction::InsertElement: {
      auto *IE = dyn_cast<InsertElementInst>(U);
      if (!IE)
        return TTI::TCC_Basic; // FIXME
      unsigned Idx = -1;
      if (auto *CI = dyn_cast<ConstantInt>(Operands[2]))
        if (CI->getValue().getActiveBits() <= 32)
          Idx = CI->getZExtValue();
      return TargetTTI->getVectorInstrCost(*IE, Ty, CostKind, Idx,
                                           TTI::getVectorInstrContextHint(IE));
    }
    case Instruction::ShuffleVector: {
      auto *Shuffle = dyn_cast<ShuffleVectorInst>(U);
      if (!Shuffle)
        return TTI::TCC_Basic; // FIXME

      auto *VecTy = cast<VectorType>(U->getType());
      auto *VecSrcTy = cast<VectorType>(Operands[0]->getType());
      ArrayRef<int> Mask = Shuffle->getShuffleMask();
      int NumSubElts, SubIndex;

      // Treat undef/poison mask as free (no matter the length).
      if (all_of(Mask, [](int M) { return M < 0; }))
        return TTI::TCC_Free;

      // TODO: move more of this inside improveShuffleKindFromMask.
      if (Shuffle->changesLength()) {
        // Treat a 'subvector widening' as a free shuffle.
        if (Shuffle->increasesLength() && Shuffle->isIdentityWithPadding())
          return TTI::TCC_Free;

        if (Shuffle->isExtractSubvectorMask(SubIndex))
          return TargetTTI->getShuffleCost(TTI::SK_ExtractSubvector, VecTy,
                                           VecSrcTy, CostKind, Mask, SubIndex,
                                           VecTy, Operands, Shuffle);

        if (Shuffle->isInsertSubvectorMask(NumSubElts, SubIndex))
          return TargetTTI->getShuffleCost(
              TTI::SK_InsertSubvector, VecTy, VecSrcTy, CostKind, Mask,
              SubIndex,
              FixedVectorType::get(VecTy->getScalarType(), NumSubElts),
              Operands, Shuffle);

        int ReplicationFactor, VF;
        if (Shuffle->isReplicationMask(ReplicationFactor, VF)) {
          APInt DemandedDstElts = APInt::getZero(Mask.size());
          for (auto I : enumerate(Mask)) {
            if (I.value() != PoisonMaskElem)
              DemandedDstElts.setBit(I.index());
          }
          return TargetTTI->getReplicationShuffleCost(
              VecSrcTy->getElementType(), ReplicationFactor, VF,
              DemandedDstElts, CostKind);
        }

        bool IsUnary = isa<UndefValue>(Operands[1]);
        NumSubElts = VecSrcTy->getElementCount().getKnownMinValue();
        SmallVector<int, 16> AdjustMask(Mask);

        // Widening shuffle - widening the source(s) to the new length
        // (treated as free - see above), and then perform the adjusted
        // shuffle at that width.
        if (Shuffle->increasesLength()) {
          for (int &M : AdjustMask)
            M = M >= NumSubElts ? (M + (Mask.size() - NumSubElts)) : M;

          return TargetTTI->getShuffleCost(
              IsUnary ? TTI::SK_PermuteSingleSrc : TTI::SK_PermuteTwoSrc, VecTy,
              VecTy, CostKind, AdjustMask, 0, nullptr, Operands, Shuffle);
        }

        // Narrowing shuffle - perform shuffle at original wider width and
        // then extract the lower elements.
        // FIXME: This can assume widening, which is not true of all vector
        // architectures (and is not even the default).
        AdjustMask.append(NumSubElts - Mask.size(), PoisonMaskElem);

        InstructionCost ShuffleCost = TargetTTI->getShuffleCost(
            IsUnary ? TTI::SK_PermuteSingleSrc : TTI::SK_PermuteTwoSrc,
            VecSrcTy, VecSrcTy, CostKind, AdjustMask, 0, nullptr, Operands,
            Shuffle);

        SmallVector<int, 16> ExtractMask(Mask.size());
        std::iota(ExtractMask.begin(), ExtractMask.end(), 0);
        return ShuffleCost + TargetTTI->getShuffleCost(
                                 TTI::SK_ExtractSubvector, VecTy, VecSrcTy,
                                 CostKind, ExtractMask, 0, VecTy, {}, Shuffle);
      }

      if (Shuffle->isIdentity())
        return TTI::TCC_Free;

      if (Shuffle->isReverse())
        return TargetTTI->getShuffleCost(TTI::SK_Reverse, VecTy, VecSrcTy,
                                         CostKind, Mask, 0, nullptr, Operands,
                                         Shuffle);

      if (Shuffle->isTranspose())
        return TargetTTI->getShuffleCost(TTI::SK_Transpose, VecTy, VecSrcTy,
                                         CostKind, Mask, 0, nullptr, Operands,
                                         Shuffle);

      if (Shuffle->isZeroEltSplat())
        return TargetTTI->getShuffleCost(TTI::SK_Broadcast, VecTy, VecSrcTy,
                                         CostKind, Mask, 0, nullptr, Operands,
                                         Shuffle);

      if (Shuffle->isSingleSource())
        return TargetTTI->getShuffleCost(TTI::SK_PermuteSingleSrc, VecTy,
                                         VecSrcTy, CostKind, Mask, 0, nullptr,
                                         Operands, Shuffle);

      if (Shuffle->isInsertSubvectorMask(NumSubElts, SubIndex))
        return TargetTTI->getShuffleCost(
            TTI::SK_InsertSubvector, VecTy, VecSrcTy, CostKind, Mask, SubIndex,
            FixedVectorType::get(VecTy->getScalarType(), NumSubElts), Operands,
            Shuffle);

      if (Shuffle->isSelect())
        return TargetTTI->getShuffleCost(TTI::SK_Select, VecTy, VecSrcTy,
                                         CostKind, Mask, 0, nullptr, Operands,
                                         Shuffle);

      if (Shuffle->isSplice(SubIndex))
        return TargetTTI->getShuffleCost(TTI::SK_Splice, VecTy, VecSrcTy,
                                         CostKind, Mask, SubIndex, nullptr,
                                         Operands, Shuffle);

      return TargetTTI->getShuffleCost(TTI::SK_PermuteTwoSrc, VecTy, VecSrcTy,
                                       CostKind, Mask, 0, nullptr, Operands,
                                       Shuffle);
    }
    case Instruction::ExtractElement: {
      auto *EEI = dyn_cast<ExtractElementInst>(U);
      if (!EEI)
        return TTI::TCC_Basic; // FIXME
      unsigned Idx = -1;
      if (auto *CI = dyn_cast<ConstantInt>(Operands[1]))
        if (CI->getValue().getActiveBits() <= 32)
          Idx = CI->getZExtValue();
      Type *DstTy = Operands[0]->getType();
      return TargetTTI->getVectorInstrCost(*EEI, DstTy, CostKind, Idx);
    }
    }

    // By default, just classify everything remaining as 'basic'.
    return TTI::TCC_Basic;
  }

  /// Return true if the instruction is too expensive to speculate.
  /// @param I Instruction providing context for the query.
  /// @return True if the instruction is too expensive to speculate.
  bool isExpensiveToSpeculativelyExecute(const Instruction *I) const override {
    auto *TargetTTI = static_cast<const T *>(this);
    SmallVector<const Value *, 4> Ops(I->operand_values());
    InstructionCost Cost = TargetTTI->getInstructionCost(
        I, Ops, TargetTransformInfo::TCK_SizeAndLatency);
    return Cost >= TargetTransformInfo::TCC_Expensive;
  }

  /// Return true if the target supports a tail call for the given call site.
  /// @param CB Call site being queried.
  /// @return True if the target supports a tail call for the given call site.
  bool supportsTailCallFor(const CallBase *CB) const override {
    return static_cast<const T *>(this)->supportsTailCalls();
  }
};
} // namespace llvm

#endif
