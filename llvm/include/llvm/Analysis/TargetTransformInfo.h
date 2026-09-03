//===- TargetTransformInfo.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This pass exposes codegen information to IR-level passes. Every
/// transformation that uses codegen information is broken into three parts:
/// 1. The IR-level analysis pass.
/// 2. The IR-level transformation interface which provides the needed
///    information.
/// 3. Codegen-level implementation which uses target-specific hooks.
///
/// This file defines #2, which is the interface that IR-level transformations
/// use for querying the codegen.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_TARGETTRANSFORMINFO_H
#define LLVM_ANALYSIS_TARGETTRANSFORMINFO_H

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/Uniformity.h"
#include "llvm/Analysis/IVDescriptors.h"
#include "llvm/Analysis/InterestingMemoryOperand.h"
#include "llvm/IR/FMF.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/InstructionCost.h"
#include <functional>
#include <optional>
#include <utility>

namespace llvm {

namespace Intrinsic {
typedef unsigned ID;
}

class AllocaInst;
class AssumptionCache;
class BlockFrequencyInfo;
class DominatorTree;
class CondBrInst;
class Function;
class GlobalValue;
class InstCombiner;
class OptimizationRemarkEmitter;
class InterleavedAccessInfo;
class IntrinsicInst;
class LoadInst;
class Loop;
class LoopInfo;
class LoopVectorizationLegality;
class ProfileSummaryInfo;
class RecurrenceDescriptor;
class SCEV;
class ScalarEvolution;
class SmallBitVector;
class StoreInst;
class SwitchInst;
class TargetLibraryInfo;
class Type;
class VPIntrinsic;
struct KnownBits;

/// Information about a load/store intrinsic defined by the target.
struct MemIntrinsicInfo {
  /// Pointer the intrinsic loads from or stores to.
  ///
  /// If this is non-null, then analysis/optimization passes can assume that
  /// this intrinsic is functionally equivalent to a load/store from this
  /// pointer.
  Value *PtrVal = nullptr;

  /// Ordering for atomic operations.
  AtomicOrdering Ordering = AtomicOrdering::NotAtomic;

  /// Matching id set by the target for corresponding load/store intrinsics.
  unsigned short MatchingId = 0;

  /// True if the intrinsic may read memory.
  bool ReadMem = false;
  /// True if the intrinsic may write memory.
  bool WriteMem = false;
  /// True if the intrinsic is volatile.
  bool IsVolatile = false;

  /// Interesting memory operands of the intrinsic.
  SmallVector<InterestingMemoryOperand, 1> InterestingOperands;

  /// Return true if the intrinsic is unordered and non-volatile.
  /// @return True if the intrinsic is unordered and non-volatile.
  bool isUnordered() const {
    return (Ordering == AtomicOrdering::NotAtomic ||
            Ordering == AtomicOrdering::Unordered) &&
           !IsVolatile;
  }
};

/// Attributes of a target dependent hardware loop.
struct HardwareLoopInfo {
  /// Deleted default constructor; a hardware loop requires a Loop.
  HardwareLoopInfo() = delete;
  /// Construct hardware-loop info for loop \p L.
  /// \param L Loop being considered for a hardware loop.
  LLVM_ABI HardwareLoopInfo(Loop *L);
  /// Loop being considered for a hardware loop.
  Loop *L = nullptr;
  /// Exit block of the loop.
  BasicBlock *ExitBlock = nullptr;
  /// Exit branch of the loop.
  CondBrInst *ExitBranch = nullptr;
  /// Exit count of the loop.
  const SCEV *ExitCount = nullptr;
  /// Integer type used for the hardware loop counter.
  IntegerType *CountType = nullptr;
  /// Value by which the loop counter is decremented each iteration.
  Value *LoopDecrement = nullptr;
  /// True if a hardware loop may nest another hardware loop.
  bool IsNestingLegal = false;
  /// True if the loop counter should be updated in-loop via a phi.
  bool CounterInReg = false;
  /// True if entry should be guarded by an icmp-ne-zero intrinsic.
  bool PerformEntryTest = false;
  /// Return true if \p L is a candidate for a hardware loop.
  /// \param SE Scalar evolution analysis.
  /// \param LI Loop info.
  /// \param DT Dominator tree.
  /// \param ForceNestedLoop Force consideration of nested hardware loops.
  /// \param ForceHardwareLoopPHI Force a hardware-loop PHI counter.
  /// @return True if \p L is a candidate for a hardware loop.
  LLVM_ABI bool isHardwareLoopCandidate(ScalarEvolution &SE, LoopInfo &LI,
                                        DominatorTree &DT,
                                        bool ForceNestedLoop = false,
                                        bool ForceHardwareLoopPHI = false);
  /// Return true if the loop nest can be analyzed for hardware loops.
  /// \param LI Loop info.
  /// @return True if the loop nest can be analyzed for hardware loops.
  LLVM_ABI bool canAnalyze(LoopInfo &LI);
};

/// Information for memory intrinsic cost model.
class MemIntrinsicCostAttributes {
  /// Optional context instruction, if one exists, e.g. the
  /// load/store to transform to the intrinsic.
  const Instruction *I = nullptr;

  /// Address in memory.
  const Value *Ptr = nullptr;

  /// Vector type of the data to be loaded or stored.
  Type *DataTy = nullptr;

  /// ID of the memory intrinsic.
  Intrinsic::ID IID;

  /// True when the memory access is predicated with a mask
  /// that is not a compile-time constant.
  bool VariableMask = true;

  /// Address space of the pointer.
  unsigned AddressSpace = 0;

  /// Alignment of single element.
  Align Alignment;

public:
  /// Construct cost attributes for a memory intrinsic with a pointer operand.
  /// \param Id Intrinsic ID.
  /// \param DataTy Vector type of the data being accessed.
  /// \param Ptr Address being accessed.
  /// \param VariableMask True if the mask is not a compile-time constant.
  /// \param Alignment Alignment of a single element.
  /// \param I Optional context instruction.
  MemIntrinsicCostAttributes(Intrinsic::ID Id, Type *DataTy, const Value *Ptr,
                             bool VariableMask, Align Alignment,
                             const Instruction *I = nullptr)
      : I(I), Ptr(Ptr), DataTy(DataTy), IID(Id), VariableMask(VariableMask),
        Alignment(Alignment) {}

  /// Construct cost attributes for a memory intrinsic without a pointer.
  /// \param Id Intrinsic ID.
  /// \param DataTy Vector type of the data being accessed.
  /// \param Alignment Alignment of a single element.
  /// \param AddressSpace Address space of the access.
  MemIntrinsicCostAttributes(Intrinsic::ID Id, Type *DataTy, Align Alignment,
                             unsigned AddressSpace = 0)
      : DataTy(DataTy), IID(Id), AddressSpace(AddressSpace),
        Alignment(Alignment) {}

  /// Construct cost attributes for a memory intrinsic with a mask.
  /// \param Id Intrinsic ID.
  /// \param DataTy Vector type of the data being accessed.
  /// \param VariableMask True if the mask is not a compile-time constant.
  /// \param Alignment Alignment of a single element.
  /// \param I Optional context instruction.
  MemIntrinsicCostAttributes(Intrinsic::ID Id, Type *DataTy, bool VariableMask,
                             Align Alignment, const Instruction *I = nullptr)
      : I(I), DataTy(DataTy), IID(Id), VariableMask(VariableMask),
        Alignment(Alignment) {}

  /// Return the intrinsic ID.
  /// @return The intrinsic ID.
  Intrinsic::ID getID() const { return IID; }
  /// Return the optional context instruction.
  /// @return The optional context instruction.
  const Instruction *getInst() const { return I; }
  /// Return the address being accessed.
  /// @return The address being accessed.
  const Value *getPointer() const { return Ptr; }
  /// Return the vector data type.
  /// @return The vector data type.
  Type *getDataType() const { return DataTy; }
  /// Return true if the mask is not a compile-time constant.
  /// @return True if the mask is not a compile-time constant.
  bool getVariableMask() const { return VariableMask; }
  /// Return the address space of the pointer.
  /// @return The address space of the pointer.
  unsigned getAddressSpace() const { return AddressSpace; }
  /// Return the alignment of a single element.
  /// @return The alignment of a single element.
  Align getAlignment() const { return Alignment; }
};

/// Represents a hint about the context in which a vector instruction or
/// intrinsic is used.
///
/// On some targets, inserts/extracts can cheaply be folded into loads/stores.
/// Similarly, vp.merge can also be folded into binary ops on some targets.
///
/// This enum allows the vectorizer to give getVectorInstrCost and
/// getIntrinsicInstrCost an idea of how the values are used.
///
/// See \c getVectorInstrContextHint to compute a VectorInstrContext from an
/// insert/extract Instruction*.
enum class VectorInstrContext : uint8_t {
  None,  ///< The instruction is not folded.
  Load,  ///< The value being inserted comes from a load (InsertElement only).
  Store, ///< The extracted value is stored (ExtractElement only).
  BinaryOp, ///< One of the operands is a binary op.
};

/// Attributes describing an intrinsic for cost modeling.
class IntrinsicCostAttributes {
  const IntrinsicInst *II = nullptr;
  Type *RetTy = nullptr;
  Intrinsic::ID IID;
  SmallVector<Type *, 4> ParamTys;
  SmallVector<const Value *, 4> Arguments;
  FastMathFlags FMF;
  // If ScalarizationCost is UINT_MAX, the cost of scalarizing the
  // arguments and the return value will be computed based on types.
  InstructionCost ScalarizationCost = InstructionCost::getInvalid();
  VectorInstrContext VIC = VectorInstrContext::None;

public:
  /// Construct cost attributes from a call instruction.
  /// \param Id Intrinsic ID.
  /// \param CI Call providing argument values and types.
  /// \param ScalarCost Optional precomputed scalarization cost.
  /// \param TypeBasedOnly True to ignore concrete argument values.
  LLVM_ABI IntrinsicCostAttributes(
      Intrinsic::ID Id, const CallBase &CI,
      InstructionCost ScalarCost = InstructionCost::getInvalid(),
      bool TypeBasedOnly = false);

  /// Construct cost attributes from a return type and argument types.
  /// \param Id Intrinsic ID.
  /// \param RTy Return type.
  /// \param Tys Argument types.
  /// \param Flags Fast-math flags.
  /// \param I Optional intrinsic instruction.
  /// \param ScalarCost Optional precomputed scalarization cost.
  LLVM_ABI IntrinsicCostAttributes(
      Intrinsic::ID Id, Type *RTy, ArrayRef<Type *> Tys,
      FastMathFlags Flags = FastMathFlags(), const IntrinsicInst *I = nullptr,
      InstructionCost ScalarCost = InstructionCost::getInvalid());

  /// Construct cost attributes from a return type and argument values.
  /// \param Id Intrinsic ID.
  /// \param RTy Return type.
  /// \param Args Argument values.
  LLVM_ABI IntrinsicCostAttributes(Intrinsic::ID Id, Type *RTy,
                                   ArrayRef<const Value *> Args);

  /// Construct cost attributes with full type, value, and context information.
  /// \param Id Intrinsic ID.
  /// \param RTy Return type.
  /// \param Args Argument values.
  /// \param Tys Argument types.
  /// \param Flags Fast-math flags.
  /// \param I Optional intrinsic instruction.
  /// \param ScalarCost Optional precomputed scalarization cost.
  /// \param VIC Hint about how the intrinsic is used.
  LLVM_ABI IntrinsicCostAttributes(
      Intrinsic::ID Id, Type *RTy, ArrayRef<const Value *> Args,
      ArrayRef<Type *> Tys, FastMathFlags Flags = FastMathFlags(),
      const IntrinsicInst *I = nullptr,
      InstructionCost ScalarCost = InstructionCost::getInvalid(),
      VectorInstrContext VIC = VectorInstrContext::None);

  /// Return the intrinsic ID.
  /// @return The intrinsic ID.
  Intrinsic::ID getID() const { return IID; }
  /// Return the optional intrinsic instruction.
  /// @return The optional intrinsic instruction.
  const IntrinsicInst *getInst() const { return II; }
  /// Return the return type.
  /// @return The return type.
  Type *getReturnType() const { return RetTy; }
  /// Return the fast-math flags.
  /// @return The fast-math flags.
  FastMathFlags getFlags() const { return FMF; }
  /// Return the precomputed scalarization cost, if any.
  /// @return The precomputed scalarization cost, if any.
  InstructionCost getScalarizationCost() const { return ScalarizationCost; }
  /// Return the vector-instruction context hint.
  /// @return The vector-instruction context hint.
  VectorInstrContext getVectorInstrContext() const { return VIC; }
  /// Return the argument values.
  /// @return The argument values.
  const SmallVectorImpl<const Value *> &getArgs() const { return Arguments; }
  /// Return the argument types.
  /// @return The argument types.
  const SmallVectorImpl<Type *> &getArgTypes() const { return ParamTys; }

  /// Return true if cost modeling should use types only, not values.
  /// @return True if cost modeling should use types only, not values.
  bool isTypeBasedOnly() const {
    return Arguments.empty();
  }

  /// Return true if a valid scalarization cost was provided.
  /// @return True if a valid scalarization cost was provided.
  bool skipScalarizationCost() const { return ScalarizationCost.isValid(); }
};

/// Style of tail folding used by the loop vectorizer.
enum class TailFoldingStyle {
  /// Don't use tail folding
  None,
  /// Use predicate only to mask operations on data in the loop.
  /// When the VL is not known to be a power-of-2, this method requires a
  /// runtime overflow check for the i + VL in the loop because it compares the
  /// scalar induction variable against the tripcount rounded up by VL which may
  /// overflow. When the VL is a power-of-2, both the increment and uprounded
  /// tripcount will overflow to 0, which does not require a runtime check
  /// since the loop is exited when the loop induction variable equals the
  /// uprounded trip-count, which are both 0.
  Data,
  /// Same as Data, but avoids using the get.active.lane.mask intrinsic to
  /// calculate the mask and instead implements this with a
  /// splat/stepvector/cmp.
  /// FIXME: Can this kind be removed now that SelectionDAGBuilder expands the
  /// active.lane.mask intrinsic when it is not natively supported?
  DataWithoutLaneMask,
  /// Use predicate to control both data and control flow.
  /// This method always requires a runtime overflow check for the i + VL
  /// increment inside the loop, because it uses the result direclty in the
  /// active.lane.mask to calculate the mask for the next iteration. If the
  /// increment overflows, the mask is no longer correct.
  DataAndControlFlow,
  /// Use predicated EVL instructions for tail-folding.
  /// Indicates that VP intrinsics should be used.
  DataWithEVL,
};

/// Context used when choosing a preferred tail-folding style.
struct TailFoldingInfo {
  /// Target library info.
  TargetLibraryInfo *TLI;
  /// Loop vectorization legality analysis.
  LoopVectorizationLegality *LVL;
  /// Interleaved access info.
  InterleavedAccessInfo *IAI;
  /// Construct tail-folding info from the given analyses.
  /// \param TLI Target library info.
  /// \param LVL Loop vectorization legality.
  /// \param IAI Interleaved access info.
  TailFoldingInfo(TargetLibraryInfo *TLI, LoopVectorizationLegality *LVL,
                  InterleavedAccessInfo *IAI)
      : TLI(TLI), LVL(LVL), IAI(IAI) {}
};

class TargetTransformInfo;
/// Convenience alias for TargetTransformInfo.
typedef TargetTransformInfo TTI;
class TargetTransformInfoImplBase;

/// This pass provides access to the codegen interfaces that are needed
/// for IR-level transformations.
class TargetTransformInfo {
public:
  /// Kind of extend used in a partial reduction.
  enum PartialReductionExtendKind {
    PR_None,       ///< No extension.
    PR_SignExtend, ///< Sign extension.
    PR_ZeroExtend, ///< Zero extension.
    PR_FPExtend    ///< Floating-point extension.
  };

  /// Get the kind of extension that an instruction represents.
  /// \param I Instruction to classify.
  /// @return The kind of extension that an instruction represents.
  LLVM_ABI static PartialReductionExtendKind
  getPartialReductionExtendKind(Instruction *I);
  /// Get the kind of extension that a cast opcode represents.
  /// \param CastOpc Cast opcode to classify.
  /// @return The kind of extension that a cast opcode represents.
  LLVM_ABI static PartialReductionExtendKind
  getPartialReductionExtendKind(Instruction::CastOps CastOpc);
  /// Get the cast opcode for an extension kind.
  /// \param Kind Extension kind to convert.
  /// @return The cast opcode for an extension kind.
  LLVM_ABI static Instruction::CastOps
  getOpcodeForPartialReductionExtendKind(PartialReductionExtendKind Kind);

  /// Construct a TTI object using a type implementing the \c Concept
  /// API below.
  ///
  /// This is used by targets to construct a TTI wrapping their target-specific
  /// implementation that encodes appropriate costs for their target.
  /// \param Impl Target-specific TTI implementation.
  LLVM_ABI explicit TargetTransformInfo(
      std::unique_ptr<const TargetTransformInfoImplBase> Impl);

  /// Construct a baseline TTI object using a minimal implementation of
  /// the \c Concept API below.
  ///
  /// The TTI implementation will reflect the information in the DataLayout
  /// provided if non-null.
  /// \param DL Data layout used by the baseline implementation.
  LLVM_ABI explicit TargetTransformInfo(const DataLayout &DL);

  /// Move-construct TargetTransformInfo.
  /// \param Arg Instance to move from.
  LLVM_ABI TargetTransformInfo(TargetTransformInfo &&Arg);
  /// Move-assign TargetTransformInfo.
  /// \param RHS Instance to move from.
  /// @return Reference to this object after move assignment.
  LLVM_ABI TargetTransformInfo &operator=(TargetTransformInfo &&RHS);

  /// Destroy TargetTransformInfo.
  ///
  /// We need to define the destructor out-of-line to define our sub-classes
  /// out-of-line.
  LLVM_ABI ~TargetTransformInfo();

  /// Handle the invalidation of this information.
  ///
  /// When used as a result of \c TargetIRAnalysis this method will be called
  /// when the function this was computed for changes. When it returns false,
  /// the information is preserved across those changes.
  /// \param F Function whose analyses may be invalidated.
  /// \param PA Set of preserved analyses.
  /// \param Inv Invalidator for dependent analyses.
  /// @return False if the information is preserved across the changes; true if it should be invalidated.
  bool invalidate(Function &F, const PreservedAnalyses &PA,
                  FunctionAnalysisManager::Invalidator &Inv) {
    // FIXME: We should probably in some way ensure that the subtarget
    // information for a function hasn't changed.
    return false;
  }

  /// \name Generic Target Information
  /// @{

  /// The kind of cost model.
  ///
  /// There are several different cost models that can be customized by the
  /// target. The normalization of each cost model may be target specific.
  /// e.g. TCK_SizeAndLatency should be comparable to target thresholds such as
  /// those derived from MCSchedModel::LoopMicroOpBufferSize etc.
  enum TargetCostKind {
    TCK_RecipThroughput, ///< Reciprocal throughput.
    TCK_Latency,         ///< The latency of instruction.
    TCK_CodeSize,        ///< Instruction code size.
    TCK_SizeAndLatency   ///< The weighted sum of size and latency.
  };

  /// Underlying constants for 'cost' values in this interface.
  ///
  /// Many APIs in this interface return a cost. This enum defines the
  /// fundamental values that should be used to interpret (and produce) those
  /// costs. The costs are returned as an int rather than a member of this
  /// enumeration because it is expected that the cost of one IR instruction
  /// may have a multiplicative factor to it or otherwise won't fit directly
  /// into the enum. Moreover, it is common to sum or average costs which works
  /// better as simple integral values. Thus this enum only provides constants.
  /// Also note that the returned costs are signed integers to make it natural
  /// to add, subtract, and test with zero (a common boundary condition). It is
  /// not expected that 2^32 is a realistic cost to be modeling at any point.
  ///
  /// Note that these costs should usually reflect the intersection of code-size
  /// cost and execution cost. A free instruction is typically one that folds
  /// into another instruction. For example, reg-to-reg moves can often be
  /// skipped by renaming the registers in the CPU, but they still are encoded
  /// and thus wouldn't be considered 'free' here.
  enum TargetCostConstants {
    TCC_Free = 0,     ///< Expected to fold away in lowering.
    TCC_Basic = 1,    ///< The cost of a typical 'add' instruction.
    TCC_Expensive = 4 ///< The cost of a 'div' instruction on x86.
  };

  /// Estimate the cost of a GEP operation when lowered.
  ///
  /// \param PointeeType Source element type of the GEP.
  /// \param Ptr Base pointer operand.
  /// \param Operands List of indices following the base pointer.
  /// \param CostKind Kind of cost model to apply.
  /// \param AccessType Hint as to what type of memory might be accessed by
  /// users of the GEP. getGEPCost will use it to determine if the GEP can be
  /// folded into the addressing mode of a load/store. If AccessType is null,
  /// then the resulting target type based off of PointeeType will be used as an
  /// approximation.
  /// @return Estimated cost of the GEP operation when lowered.
  LLVM_ABI InstructionCost getGEPCost(Type *PointeeType, const Value *Ptr,
                                      ArrayRef<const Value *> Operands,
                                      TargetCostKind CostKind,
                                      Type *AccessType = nullptr) const;

  /// Describe known properties for a set of pointers.
  struct PointersChainInfo {
    /// All the GEPs in a set have same base address.
    unsigned IsSameBaseAddress : 1;
    /// These properties only valid if SameBaseAddress is set.
    /// True if all pointers are separated by a unit stride.
    unsigned IsUnitStride : 1;
    /// True if distance between any two neigbouring pointers is a known value.
    unsigned IsKnownStride : 1;
    /// Reserved bits for future use.
    unsigned Reserved : 29;

    /// Return true if all pointers share the same base address.
    /// @return True if all pointers share the same base address.
    bool isSameBase() const { return IsSameBaseAddress; }
    /// Return true if all pointers share a base and are unit-strided.
    /// @return True if all pointers share a base and are unit-strided.
    bool isUnitStride() const { return IsSameBaseAddress && IsUnitStride; }
    /// Return true if all pointers share a base and have a known stride.
    /// @return True if all pointers share a base and have a known stride.
    bool isKnownStride() const { return IsSameBaseAddress && IsKnownStride; }

    /// Return info for a unit-stride chain with a common base.
    /// @return Info for a unit-stride chain with a common base.
    static PointersChainInfo getUnitStride() {
      return {/*IsSameBaseAddress=*/1, /*IsUnitStride=*/1,
              /*IsKnownStride=*/1, 0};
    }
    /// Return info for a known-stride chain with a common base.
    /// @return Info for a known-stride chain with a common base.
    static PointersChainInfo getKnownStride() {
      return {/*IsSameBaseAddress=*/1, /*IsUnitStride=*/0,
              /*IsKnownStride=*/1, 0};
    }
    /// Return info for an unknown-stride chain with a common base.
    /// @return Info for an unknown-stride chain with a common base.
    static PointersChainInfo getUnknownStride() {
      return {/*IsSameBaseAddress=*/1, /*IsUnitStride=*/0,
              /*IsKnownStride=*/0, 0};
    }
  };
  static_assert(sizeof(PointersChainInfo) == 4, "Was size increase justified?");

  /// Estimate the cost of a chain of pointer operations when lowered.
  ///
  /// Typically pointer operands of a chain of loads or stores within same
  /// block. \p AccessTy is the type of the loads/stores that will ultimately
  /// use the \p Ptrs.
  /// \param Ptrs Pointers in the chain.
  /// \param Base Common base pointer, if known.
  /// \param Info Known properties of the pointer chain.
  /// \param AccessTy Type of the eventual loads or stores.
  /// \param CostKind Kind of cost model to apply.
  /// @return Estimated cost of the pointer-chain operations when lowered.
  LLVM_ABI InstructionCost
  getPointersChainCost(ArrayRef<const Value *> Ptrs, const Value *Base,
                       const PointersChainInfo &Info, Type *AccessTy,
                       const TargetCostKind CostKind) const;

  /// \returns A value by which our inlining threshold should be multiplied.
  /// This is primarily used to bump up the inlining threshold wholesale on
  /// targets where calls are unusually expensive.
  ///
  /// TODO: This is a rather blunt instrument.  Perhaps altering the costs of
  /// individual classes of instructions would be better.
  LLVM_ABI unsigned getInliningThresholdMultiplier() const;

  /// Return the savings multiplier used by inlining cost-benefit analysis.
  /// @return The savings multiplier used by inlining cost-benefit analysis.
  LLVM_ABI unsigned getInliningCostBenefitAnalysisSavingsMultiplier() const;
  /// Return the profitability multiplier used by inlining cost-benefit analysis.
  /// @return The profitability multiplier used by inlining cost-benefit analysis.
  LLVM_ABI unsigned getInliningCostBenefitAnalysisProfitableMultiplier() const;

  /// Return the bonus of inlining the last call to a static function.
  /// @return The bonus of inlining the last call to a static function.
  LLVM_ABI int getInliningLastCallToStaticBonus() const;

  /// Return a value to be added to the inlining threshold.
  /// \param CB Call being considered for inlining.
  /// @return A value to be added to the inlining threshold.
  LLVM_ABI unsigned adjustInliningThreshold(const CallBase *CB) const;

  /// Return the cost of leaving an Alloca in the caller when not inlined.
  ///
  /// Added to the inlining threshold.
  /// \param CB Call being considered for inlining.
  /// \param AI Alloca in the caller.
  /// @return The cost of leaving an Alloca in the caller when not inlined.
  LLVM_ABI unsigned getCallerAllocaCost(const CallBase *CB,
                                        const AllocaInst *AI) const;

  /// Return the vector-instruction inlining bonus as a percent.
  ///
  /// Vector bonuses: We want to more aggressively inline vector-dense kernels
  /// and apply this bonus based on the percentage of vector instructions. A
  /// bonus is applied if the vector instructions exceed 50% and half that
  /// amount is applied if it exceeds 10%. Note that these bonuses are some what
  /// arbitrary and evolved over time by accident as much as because they are
  /// principled bonuses.
  /// FIXME: It would be nice to base the bonus values on something more
  /// scientific. A target may has no bonus on vector instructions.
  /// @return The vector-instruction inlining bonus as a percent.
  LLVM_ABI int getInlinerVectorBonusPercent() const;

  /// Return the expected cost of a memcpy.
  ///
  /// The cost could e.g. depend on the source/destination type and alignment
  /// and the number of bytes copied.
  /// \param I Memcpy instruction.
  /// @return The expected cost of a memcpy.
  LLVM_ABI InstructionCost getMemcpyCost(const Instruction *I) const;

  /// Returns the maximum memset / memcpy size in bytes that still makes it
  /// profitable to inline the call.
  /// @return The maximum memset / memcpy size in bytes that still makes it.
  LLVM_ABI uint64_t getMaxMemIntrinsicInlineSizeThreshold() const;

  /// Return the estimated number of case clusters when lowering \p SI.
  ///
  /// \param SI Switch being lowered.
  /// \param JTSize Set a jump table size only when \p SI is suitable for a jump
  /// table.
  /// \param PSI Optional profile summary info.
  /// \param BFI Optional block frequency info.
  /// @return The estimated number of case clusters when lowering \p SI.
  LLVM_ABI unsigned
  getEstimatedNumberOfCaseClusters(const SwitchInst &SI, unsigned &JTSize,
                                   ProfileSummaryInfo *PSI,
                                   BlockFrequencyInfo *BFI) const;

  /// Estimate the cost of a given IR user when lowered.
  ///
  /// This can estimate the cost of either a ConstantExpr or Instruction when
  /// lowered.
  ///
  /// \param U IR user whose cost is estimated.
  /// \param Operands List of operands which can be a result of transformations
  /// of the current operands. The number of the operands on the list must equal
  /// to the number of the current operands the IR user has. Their order on the
  /// list must be the same as the order of the current operands the IR user
  /// has.
  /// \param CostKind Kind of cost model to apply.
  ///
  /// The returned cost is defined in terms of \c TargetCostConstants, see its
  /// comments for a detailed explanation of the cost values.
  /// @return Estimated cost of the IR user when lowered.
  LLVM_ABI InstructionCost getInstructionCost(const User *U,
                                              ArrayRef<const Value *> Operands,
                                              TargetCostKind CostKind) const;

  /// Estimate the cost of \p U using its current operands.
  ///
  /// This is a helper function which calls the three-argument
  /// getInstructionCost with \p Operands which are the current operands U has.
  /// \param U IR user whose cost is estimated.
  /// \param CostKind Kind of cost model to apply.
  /// @return Estimated cost of \p U using its current operands.
  InstructionCost getInstructionCost(const User *U,
                                     TargetCostKind CostKind) const {
    SmallVector<const Value *, 4> Operands(U->operand_values());
    return getInstructionCost(U, Operands, CostKind);
  }

  /// If a branch or a select condition is skewed in one direction by more than
  /// this factor, it is very likely to be predicted correctly.
  /// @return Branch probability threshold above which a branch or select is considered predictable.
  LLVM_ABI BranchProbability getPredictableBranchThreshold() const;

  /// Return estimated branch-misprediction penalty in latency.
  ///
  /// Indicates how aggressive the target wants for eliminating unpredictable
  /// branches. A zero return value means extra optimization applied to them
  /// should be minimal.
  /// @return Estimated branch-misprediction penalty in latency.
  LLVM_ABI InstructionCost getBranchMispredictPenalty() const;

  /// Return true if branch divergence exists.
  ///
  /// Branch divergence has a significantly negative impact on GPU performance
  /// when threads in the same wavefront take different paths due to conditional
  /// branches.
  ///
  /// If \p F is passed, provides a context function. If \p F is known to only
  /// execute in a single threaded environment, the target may choose to skip
  /// uniformity analysis and assume all values are uniform.
  /// \param F Optional context function.
  /// @return True if branch divergence exists.
  LLVM_ABI bool hasBranchDivergence(const Function *F = nullptr) const;

  /// Get target-specific uniformity information for a value.
  ///
  /// This allows targets to provide more fine-grained control over uniformity
  /// analysis by specifying whether specific values should always or never be
  /// considered uniform, or require custom operand-based analysis.
  /// \param V The value to query for uniformity information.
  /// \return ValueUniformity.
  LLVM_ABI ValueUniformity getValueUniformity(const Value *V) const;

  /// Query the target whether the specified address space cast from FromAS to
  /// ToAS is valid.
  /// \param FromAS Source address space.
  /// \param ToAS Destination address space.
  /// @return True if the address-space cast is valid on this target.
  LLVM_ABI bool isValidAddrSpaceCast(unsigned FromAS, unsigned ToAS) const;

  /// Return false if a \p AS0 address cannot possibly alias a \p AS1 address.
  /// \param AS0 First address space.
  /// \param AS1 Second address space.
  /// @return False if a \p AS0 address cannot possibly alias a \p AS1 address.
  LLVM_ABI bool addrspacesMayAlias(unsigned AS0, unsigned AS1) const;

  /// Return the address space ID for a target's flat address space.
  ///
  /// Note this is not necessarily the same as addrspace(0), which LLVM
  /// sometimes refers to as the generic address space. The flat address space
  /// is a generic address space that can be used access multiple segments of
  /// memory with different address spaces. Access of a memory location through
  /// a pointer with this address space is expected to be legal but slower
  /// compared to the same memory location accessed through a pointer with a
  /// different address space.
  ///
  /// This is for targets with different pointer representations which can
  /// be converted with the addrspacecast instruction. If a pointer is converted
  /// to this address space, optimizations should attempt to replace the access
  /// with the source address space.
  ///
  /// \returns ~0u if the target does not have such a flat address space to
  /// optimize away.
  LLVM_ABI unsigned getFlatAddressSpace() const;

  /// Return any intrinsic address operand indexes which may be rewritten if
  /// they use a flat address space pointer.
  ///
  /// \param OpIndexes Filled with operand indexes that may be rewritten.
  /// \param IID Intrinsic ID being queried.
  /// \returns true if the intrinsic was handled.
  LLVM_ABI bool collectFlatAddressOperands(SmallVectorImpl<int> &OpIndexes,
                                           Intrinsic::ID IID) const;

  /// Return true if casting from \p FromAS to \p ToAS is a no-op.
  /// \param FromAS Source address space.
  /// \param ToAS Destination address space.
  /// @return True if casting from \p FromAS to \p ToAS is a no-op.
  LLVM_ABI bool isNoopAddrSpaceCast(unsigned FromAS, unsigned ToAS) const;

  /// Compute known bits for both sides of an address-space cast of \p PtrOp.
  ///
  /// Given an address space cast of the given pointer value, calculate the
  /// known bits of the source pointer in the source addrspace and the
  /// destination pointer in the destination addrspace.
  /// \param ToAS Destination address space.
  /// \param PtrOp Pointer operand being cast.
  /// @return Known bits for the source and destination pointers.
  LLVM_ABI std::pair<KnownBits, KnownBits>
  computeKnownBitsAddrSpaceCast(unsigned ToAS, const Value &PtrOp) const;

  /// Compute known bits of an address-space cast from known source bits.
  ///
  /// Given an address space cast, calculate the known bits of the resulting ptr
  /// in the destination addrspace using the known bits of the source pointer in
  /// the source addrspace.
  /// \param FromAS Source address space.
  /// \param ToAS Destination address space.
  /// \param FromPtrBits Known bits of the source pointer.
  /// @return Known bits of the resulting pointer in the destination address space.
  LLVM_ABI KnownBits computeKnownBitsAddrSpaceCast(
      unsigned FromAS, unsigned ToAS, const KnownBits &FromPtrBits) const;

  /// Return a mask of pointer bits preserved by an address-space cast.
  ///
  /// The returned APInt has the same bit width as the source address space
  /// pointer size.
  ///
  /// Some targets allow certain bits of a pointer to change (e.g., the low
  /// bits within a page) while still preserving the address space. This mask
  /// identifies those bits that are guaranteed to be preserved. If the mask is
  /// all zeros, no bits are preserved and address space inference cannot be
  /// performed safely.
  ///
  /// For example, given:
  ///   %gp = addrspacecast ptr addrspace(2) %sp to ptr
  ///   %a = ptrtoint ptr %gp to i64
  ///   %b = xor i64 7, %a
  ///   %gp2 = inttoptr i64 %b to ptr
  ///   store i16 0, ptr %gp2, align 2
  /// if the target preserves the upper bits, `%gp2` can be safely replaced
  /// with `inttoptr i64 %b to ptr addrspace(2)`.
  /// \param SrcAS Source address space.
  /// \param DstAS Destination address space.
  /// @return A mask of pointer bits preserved by an address-space cast.
  LLVM_ABI APInt getAddrSpaceCastPreservedPtrMask(unsigned SrcAS,
                                                  unsigned DstAS) const;

  /// Return true if globals in this address space can have initializers other
  /// than `undef`.
  /// \param AS Address space to query.
  /// @return True if globals in this address space can have initializers other.
  LLVM_ABI bool
  canHaveNonUndefGlobalInitializerInAddressSpace(unsigned AS) const;

  /// Return an assumed address space for value \p V.
  /// \param V Value whose address space is inferred.
  /// @return An assumed address space for value \p V.
  LLVM_ABI unsigned getAssumedAddrSpace(const Value *V) const;

  /// Return true if the target is known to be single-threaded.
  /// @return True if the target is known to be single-threaded.
  LLVM_ABI bool isSingleThreaded() const;

  /// Return a predicated pointer and its address space for value \p V.
  /// \param V Value whose predicated address space is requested.
  /// @return A predicated pointer and its address space for value \p V.
  LLVM_ABI std::pair<const Value *, unsigned>
  getPredicatedAddrSpace(const Value *V) const;

  /// Rewrite intrinsic \p II so \p OldV is replaced by address-space-cast \p NewV.
  ///
  /// This should happen for every operand index that collectFlatAddressOperands
  /// returned for the intrinsic. \returns nullptr if the intrinsic was not
  /// handled. Otherwise, returns the new value (which may be the original \p II
  /// with modified operands).
  /// \param II Intrinsic to rewrite.
  /// \param OldV Flat-address-space value being replaced.
  /// \param NewV Replacement value in a different address space.
  LLVM_ABI Value *rewriteIntrinsicWithAddressSpace(IntrinsicInst *II,
                                                   Value *OldV,
                                                   Value *NewV) const;

  /// Test whether calls to a function lower to actual program function
  /// calls.
  ///
  /// The idea is to test whether the program is likely to require a 'call'
  /// instruction or equivalent in order to call the given function.
  ///
  /// FIXME: It's not clear that this is a good or useful query API. Client's
  /// should probably move to simpler cost metrics using the above.
  /// Alternatively, we could split the cost interface into distinct code-size
  /// and execution-speed costs. This would allow modelling the core of this
  /// query more accurately as a call is a single small instruction, but
  /// incurs significant execution cost.
  /// \param F Function being called.
  /// @return True if calls to the function lower to an actual program function call.
  LLVM_ABI bool isLoweredToCall(const Function *F) const;

  /// Cost metrics used by Loop Strength Reduction.
  struct LSRCost {
    /// TODO: Some of these could be merged. Also, a lexical ordering
    /// isn't always optimal.
    unsigned Insns;
    /// Number of registers required by the solution.
    unsigned NumRegs;
    /// Cost of add-recurrence formulae.
    unsigned AddRecCost;
    /// Number of induction-variable multiplies.
    unsigned NumIVMuls;
    /// Number of base adds in the addressing formulae.
    unsigned NumBaseAdds;
    /// Cost of immediate constants.
    unsigned ImmCost;
    /// Setup cost outside the loop.
    unsigned SetupCost;
    /// Cost of scaled indexing.
    unsigned ScaleCost;
  };

  /// Parameters that control the generic loop unrolling transformation.
  struct UnrollingPreferences {
    /// Cost threshold for the unrolled loop.
    ///
    /// Should be relative to the getInstructionCost values returned by this
    /// API, and the expectation is that the unrolled loop's instructions when
    /// run through that interface should not exceed this cost. However, this is
    /// only an estimate. Also, specific loops may be unrolled even with a cost
    /// above this threshold if deemed profitable. Set this to UINT_MAX to
    /// disable the loop body cost restriction.
    unsigned Threshold;
    /// Maximum percent by which Threshold may be boosted for full unrolling.
    ///
    /// If complete unrolling will reduce the cost of the loop, we will boost
    /// the Threshold by a certain percent to allow more aggressive complete
    /// unrolling. This value provides the maximum boost percentage that we
    /// can apply to Threshold (The value should be no less than 100).
    /// BoostedThreshold = Threshold * min(RolledCost / UnrolledCost,
    ///                                    MaxPercentThresholdBoost / 100)
    /// E.g. if complete unrolling reduces the loop execution time by 50%
    /// then we boost the threshold by the factor of 2x. If unrolling is not
    /// expected to reduce the running time, then we do not increase the
    /// threshold.
    unsigned MaxPercentThresholdBoost;
    /// The cost threshold for the unrolled loop when optimizing for size (set
    /// to UINT_MAX to disable).
    unsigned OptSizeThreshold;
    /// The cost threshold for the unrolled loop, like Threshold, but used
    /// for partial/runtime unrolling (set to UINT_MAX to disable).
    unsigned PartialThreshold;
    /// The cost threshold for the unrolled loop when optimizing for size, like
    /// OptSizeThreshold, but used for partial/runtime unrolling (set to
    /// UINT_MAX to disable).
    unsigned PartialOptSizeThreshold;
    /// Default unroll count for loops with run-time trip count.
    unsigned DefaultUnrollRuntimeCount;
    /// Maximum unrolling factor for partial or runtime unrolling.
    ///
    /// The unrolling factor may be selected using the appropriate cost
    /// threshold, but may not exceed this number (set to UINT_MAX to disable).
    /// This does not apply in cases where the loop is being fully unrolled.
    unsigned MaxCount;
    /// Maximum trip-count upper bound considered for unrolling.
    ///
    /// Allowing the MaxUpperBound to be overrided by a target gives more
    /// flexiblity on certain cases. By default, MaxUpperBound uses
    /// UnrollMaxUpperBound which value is 8.
    unsigned MaxUpperBound;
    /// Maximum unrolling factor even when full unrolling is selected.
    ///
    /// Like MaxCount, but applies even if full unrolling is selected. This
    /// allows a target to fall back to Partial unrolling if full unrolling is
    /// above FullUnrollMaxCount.
    unsigned FullUnrollMaxCount;
    /// Number of instructions saved when a backedge becomes fallthrough.
    ///
    /// For now we count a conditional branch on a backedge and a comparison
    /// feeding it.
    unsigned BEInsns;
    /// Allow partial unrolling (unrolling of loops to expand the size of the
    /// loop body, not only to eliminate small constant-trip-count loops).
    bool Partial;
    /// Allow runtime unrolling (unrolling of loops to expand the size of the
    /// loop body even when the number of loop iterations is not known at
    /// compile time).
    bool Runtime;
    /// Allow generation of a loop remainder (extra iterations after unroll).
    bool AllowRemainder;
    /// Allow emitting expensive instructions (such as divisions) when computing
    /// the trip count of a loop for runtime unrolling.
    bool AllowExpensiveTripCount;
    /// Apply loop unroll on any kind of loop
    /// (mainly to loops that fail runtime unrolling).
    bool Force;
    /// Allow using trip count upper bound to unroll loops.
    bool UpperBound;
    /// Allow unrolling of all the iterations of the runtime loop remainder.
    bool UnrollRemainder;
    /// Allow unroll and jam. Used to enable unroll and jam for the target.
    bool UnrollAndJam;
    /// Threshold for the inner loop size during unroll-and-jam.
    ///
    /// The 'Threshold' value above is used during unroll and jam for the outer
    /// loop size. This value is used in the same manner to limit the size of
    /// the inner loop.
    unsigned UnrollAndJamInnerLoopThreshold;
    /// Don't allow loop unrolling to simulate more than this number of
    /// iterations when checking full unroll profitability
    unsigned MaxIterationsCountToAnalyze;
    /// Disable runtime unrolling by default for vectorized loops.
    bool UnrollVectorizedLoop = false;
    /// Don't allow runtime unrolling if expanding the trip count takes more
    /// than SCEVExpansionBudget.
    unsigned SCEVExpansionBudget;
    /// Allow runtime unrolling of multi-exit loops when profitable.
    ///
    /// Should only be set if the target determined that multi-exit unrolling is
    /// profitable for the loop. Fall back to the generic logic to determine
    /// whether multi-exit unrolling is profitable if set to false.
    bool RuntimeUnrollMultiExit;
    /// Allow unrolling to add parallel reduction phis.
    bool AddAdditionalAccumulators;
  };

  /// Get target-customized preferences for the generic loop unrolling
  /// transformation. The caller will initialize UP with the current
  /// target-independent defaults.
  /// \param L Loop being considered.
  /// \param SE Scalar evolution analysis.
  /// \param UP Unrolling preferences to fill in.
  /// \param ORE Optional remark emitter.
  LLVM_ABI void getUnrollingPreferences(Loop *L, ScalarEvolution &SE,
                                        UnrollingPreferences &UP,
                                        OptimizationRemarkEmitter *ORE) const;

  /// Query the target whether it would be profitable to convert the given loop
  /// into a hardware loop.
  /// \param L Loop being considered.
  /// \param SE Scalar evolution analysis.
  /// \param AC Assumption cache.
  /// \param LibInfo Target library info.
  /// \param HWLoopInfo Hardware-loop parameters to fill in.
  /// @return True if converting the loop into a hardware loop is profitable.
  LLVM_ABI bool isHardwareLoopProfitable(Loop *L, ScalarEvolution &SE,
                                         AssumptionCache &AC,
                                         TargetLibraryInfo *LibInfo,
                                         HardwareLoopInfo &HWLoopInfo) const;

  /// Query the target for which minimum VF epilogue vectorization should use.
  /// @return Minimum vectorization factor for which epilogue vectorization should be used.
  LLVM_ABI unsigned getEpilogueVectorizationMinVF() const;

  /// Query the target whether it would be preferred to create a tail-folded
  /// vector loop, which can avoid the need to emit a scalar epilogue loop.
  /// \param TFI Tail-folding context for the loop.
  /// @return True if creating a tail-folded loop is preferred over an epilogue.
  LLVM_ABI bool preferTailFoldingOverEpilogue(TailFoldingInfo *TFI) const;

  /// Query the target what the preferred style of tail folding is.
  /// @return Preferred style of tail folding for this target.
  LLVM_ABI TailFoldingStyle getPreferredTailFoldingStyle() const;

  /// Parameters that control the loop peeling transformation.
  struct PeelingPreferences {
    /// Forced count of loop bodies to peel before the loop, or 0 to decide.
    ///
    /// When set to 0, the a peeling factor based on profile information and
    /// other factors.
    unsigned PeelCount;
    /// Allow peeling off loop iterations.
    bool AllowPeeling;
    /// Allow peeling off loop iterations for loop nests.
    bool AllowLoopNestsPeeling;
    /// Allow peeling based on profile information.
    ///
    /// Uses to enable peeling off all iterations basing on provided profile.
    /// If the value is true the peeling cost model can decide to peel only
    /// some iterations and in this case it will set this to false.
    bool PeelProfiledIterations;

    /// Peel off the last PeelCount loop iterations.
    bool PeelLast;
  };

  /// Get target-customized preferences for generic loop peeling.
  ///
  /// The caller will initialize \p PP with the current target-independent
  /// defaults with information from \p L and \p SE.
  /// \param L Loop being considered.
  /// \param SE Scalar evolution analysis.
  /// \param PP Peeling preferences to fill in.
  LLVM_ABI void getPeelingPreferences(Loop *L, ScalarEvolution &SE,
                                      PeelingPreferences &PP) const;

  /// Combine a target-specific intrinsic during InstCombine.
  ///
  /// This function will be called from the InstCombine pass every time a
  /// target-specific intrinsic is encountered.
  ///
  /// \returns std::nullopt to not do anything target specific or a value that
  /// will be returned from the InstCombiner. It is possible to return null and
  /// stop further processing of the intrinsic by returning nullptr.
  /// \param IC InstCombiner invoking this hook.
  /// \param II Target-specific intrinsic to combine.
  LLVM_ABI std::optional<Instruction *>
  instCombineIntrinsic(InstCombiner &IC, IntrinsicInst &II) const;
  /// Can be used to implement target-specific instruction combining.
  /// \see instCombineIntrinsic
  /// \param IC InstCombiner invoking this hook.
  /// \param II Target-specific intrinsic to simplify.
  /// \param DemandedMask Bits demanded of the result.
  /// \param Known Known bits, filled in by the target.
  /// \param KnownBitsComputed Set to true if Known was computed.
  /// @return The simplified value, or std::nullopt if unchanged.
  LLVM_ABI std::optional<Value *>
  simplifyDemandedUseBitsIntrinsic(InstCombiner &IC, IntrinsicInst &II,
                                   APInt DemandedMask, KnownBits &Known,
                                   bool &KnownBitsComputed) const;
  /// Can be used to implement target-specific instruction combining.
  /// \see instCombineIntrinsic
  /// \param IC InstCombiner invoking this hook.
  /// \param II Target-specific intrinsic to simplify.
  /// \param DemandedElts Demanded vector elements of the result.
  /// \param UndefElts Elements known undef in the result.
  /// \param UndefElts2 Elements known undef in a second operand.
  /// \param UndefElts3 Elements known undef in a third operand.
  /// \param SimplifyAndSetOp Callback to simplify and replace an operand.
  /// @return The simplified value, or std::nullopt if unchanged.
  LLVM_ABI std::optional<Value *> simplifyDemandedVectorEltsIntrinsic(
      InstCombiner &IC, IntrinsicInst &II, APInt DemandedElts, APInt &UndefElts,
      APInt &UndefElts2, APInt &UndefElts3,
      std::function<void(Instruction *, unsigned, APInt, APInt &)>
          SimplifyAndSetOp) const;
  /// @}

  /// \name Scalar Target Information
  /// @{

  /// Flags indicating the kind of support for population count.
  ///
  /// Compared to the SW implementation, HW support is supposed to
  /// significantly boost the performance when the population is dense, and it
  /// may or may not degrade performance if the population is sparse. A HW
  /// support is considered as "Fast" if it can outperform, or is on a par
  /// with, SW implementation when the population is sparse; otherwise, it is
  /// considered as "Slow".
  enum PopcntSupportKind {
    PSK_Software,     ///< Population count is implemented in software.
    PSK_SlowHardware, ///< Hardware popcount is available but slower than SW when sparse.
    PSK_FastHardware  ///< Hardware popcount is as fast as software even when sparse.
  };

  /// Return true if \p Imm is a legal immediate operand of an add.
  ///
  /// That is, the target has add instructions which can add a register with the
  /// immediate without having to materialize the immediate into a register.
  /// \param Imm Immediate value.
  /// @return True if \p Imm is a legal immediate operand of an add.
  LLVM_ABI bool isLegalAddImmediate(int64_t Imm) const;

  /// Return true if adding \p Imm scaled by vscale is a legal add immediate.
  ///
  /// That is, the target has add instructions which can add a register with the
  /// immediate (multiplied by vscale) without having to materialize the
  /// immediate into a register.
  /// \param Imm Scalable immediate value.
  /// @return True if adding \p Imm scaled by vscale is a legal add immediate.
  LLVM_ABI bool isLegalAddScalableImmediate(int64_t Imm) const;

  /// Return true if \p Imm is a legal immediate operand of an icmp.
  ///
  /// That is, the target has icmp instructions which can compare a register
  /// against the immediate without having to materialize the immediate into a
  /// register.
  /// \param Imm Immediate value.
  /// @return True if \p Imm is a legal immediate operand of an icmp.
  LLVM_ABI bool isLegalICmpImmediate(int64_t Imm) const;

  /// Return true if the given addressing mode is legal for a load/store of \p Ty.
  ///
  /// The type may be VoidTy, in which case only return true if the addressing
  /// mode is legal for a load/store of any legal type.
  /// If target returns true in LSRWithInstrQueries(), I may be valid.
  /// \param Ty Type of the load or store, or VoidTy for any legal type.
  /// \param BaseGV Optional base global value.
  /// \param BaseOffset Constant base offset in bytes.
  /// \param HasBaseReg True if the mode uses a base register.
  /// \param Scale Scaled-index factor.
  /// \param AddrSpace Address space of the pointer.
  /// \param I Optional memory instruction when LSRWithInstrQueries is true.
  /// \param ScalableOffset Quantity of bytes multiplied by vscale, an invariant
  /// value known only at runtime. Most targets should not accept a scalable
  /// offset.
  ///
  /// TODO: Handle pre/postinc as well.
  /// @return True if the given addressing mode is legal for a load/store of \p Ty.
  LLVM_ABI bool isLegalAddressingMode(Type *Ty, GlobalValue *BaseGV,
                                      int64_t BaseOffset, bool HasBaseReg,
                                      int64_t Scale, unsigned AddrSpace = 0,
                                      Instruction *I = nullptr,
                                      int64_t ScalableOffset = 0) const;

  /// Return true if LSR cost of C1 is lower than C2.
  /// \param C1 First LSR cost.
  /// \param C2 Second LSR cost.
  /// @return True if LSR cost of C1 is lower than C2.
  LLVM_ABI bool isLSRCostLess(const TargetTransformInfo::LSRCost &C1,
                              const TargetTransformInfo::LSRCost &C2) const;

  /// Return true if LSR should treat the number of registers as the major cost.
  ///
  /// Targets which implement their own isLSRCostLess and unset number of
  /// registers as major cost should return false, otherwise return true.
  /// @return True if LSR should treat the number of registers as the major cost.
  LLVM_ABI bool isNumRegsMajorCostOfLSR() const;

  /// Return true if LSR should drop a found solution if it's calculated to be
  /// less profitable than the baseline.
  /// @return True if LSR should drop a found solution if it's calculated to be.
  LLVM_ABI bool shouldDropLSRSolutionIfLessProfitable() const;

  /// Return true if LSR should not optimize a chain that includes \p I.
  /// \param I Instruction in the LSR chain.
  /// @return True if LSR should not optimize a chain that includes \p I.
  LLVM_ABI bool isProfitableLSRChainElement(Instruction *I) const;

  /// Return true if the target can fuse a compare and branch.
  ///
  /// Loop-strength-reduction (LSR) uses that knowledge to adjust its cost
  /// calculation for the instructions in a loop.
  /// @return True if the target can fuse a compare and branch.
  LLVM_ABI bool canMacroFuseCmp() const;

  /// Return true if the target can save a compare for loop count, for example
  /// hardware loop saves a compare.
  /// \param L Loop being considered.
  /// \param BI Optional output for the loop latch compare-and-branch.
  /// \param SE Scalar evolution analysis.
  /// \param LI Loop info.
  /// \param DT Dominator tree.
  /// \param AC Assumption cache.
  /// \param LibInfo Target library info.
  /// @return True if the target can save a compare for loop count, for example.
  LLVM_ABI bool canSaveCmp(Loop *L, CondBrInst **BI, ScalarEvolution *SE,
                           LoopInfo *LI, DominatorTree *DT, AssumptionCache *AC,
                           TargetLibraryInfo *LibInfo) const;

  /// Which addressing mode Loop Strength Reduction will try to generate.
  enum AddressingModeKind {
    AMK_None = 0x0,        ///< Don't prefer any addressing mode
    AMK_PreIndexed = 0x1,  ///< Prefer pre-indexed addressing mode
    AMK_PostIndexed = 0x2, ///< Prefer post-indexed addressing mode
    AMK_All = 0x3,         ///< Consider all addressing modes
    /// Largest enumerator used by bitmask helpers.
    LLVM_MARK_AS_BITMASK_ENUM(/*LargestValue=*/AMK_All)
  };

  /// Return the preferred addressing mode LSR should make efforts to generate.
  /// \param L Loop being considered.
  /// \param SE Scalar evolution analysis.
  /// @return The preferred addressing mode LSR should make efforts to generate.
  LLVM_ABI AddressingModeKind
  getPreferredAddressingMode(const Loop *L, ScalarEvolution *SE) const;

  /// Some targets only support masked load/store with a constant mask.
  enum MaskKind {
    VariableOrConstantMask, ///< Variable or constant masks are legal.
    ConstantMask,           ///< Only a constant mask is legal.
  };

  /// Return true if the target supports masked store.
  /// \param DataType Stored type.
  /// \param Alignment Alignment of the store.
  /// \param AddressSpace Address space of the pointer.
  /// \param MaskKind Kind of mask that must be supported.
  /// @return True if the target supports masked store.
  LLVM_ABI bool
  isLegalMaskedStore(Type *DataType, Align Alignment, unsigned AddressSpace,
                     MaskKind MaskKind = VariableOrConstantMask) const;
  /// Return true if the target supports masked load.
  /// \param DataType Loaded type.
  /// \param Alignment Alignment of the load.
  /// \param AddressSpace Address space of the pointer.
  /// \param MaskKind Kind of mask that must be supported.
  /// @return True if the target supports masked load.
  LLVM_ABI bool
  isLegalMaskedLoad(Type *DataType, Align Alignment, unsigned AddressSpace,
                    MaskKind MaskKind = VariableOrConstantMask) const;

  /// Return true if the target supports nontemporal store.
  /// \param DataType Stored type.
  /// \param Alignment Alignment of the store.
  /// @return True if the target supports nontemporal store.
  LLVM_ABI bool isLegalNTStore(Type *DataType, Align Alignment) const;
  /// Return true if the target supports nontemporal load.
  /// \param DataType Loaded type.
  /// \param Alignment Alignment of the load.
  /// @return True if the target supports nontemporal load.
  LLVM_ABI bool isLegalNTLoad(Type *DataType, Align Alignment) const;

  /// Return true if the target supports broadcasting a load to a vector.
  ///
  /// The broadcast produces a vector of type <NumElements x ElementTy>.
  /// \param ElementTy Element type of the broadcast.
  /// \param NumElements Number of vector elements.
  /// @return True if the target supports broadcasting a load to a vector.
  LLVM_ABI bool isLegalBroadcastLoad(Type *ElementTy,
                                     ElementCount NumElements) const;

  /// Return true if the target supports masked scatter.
  /// \param DataType Scattered type.
  /// \param Alignment Alignment of the scatter.
  /// @return True if the target supports masked scatter.
  LLVM_ABI bool isLegalMaskedScatter(Type *DataType, Align Alignment) const;
  /// Return true if the target supports masked gather.
  /// \param DataType Gathered type.
  /// \param Alignment Alignment of the gather.
  /// @return True if the target supports masked gather.
  LLVM_ABI bool isLegalMaskedGather(Type *DataType, Align Alignment) const;
  /// Return true if the target forces scalarizing of llvm.masked.gather
  /// intrinsics.
  /// \param Type Gathered vector type.
  /// \param Alignment Alignment of the gather.
  /// @return True if the target forces scalarizing of llvm.masked.gather.
  LLVM_ABI bool forceScalarizeMaskedGather(VectorType *Type,
                                           Align Alignment) const;
  /// Return true if the target forces scalarizing of llvm.masked.scatter
  /// intrinsics.
  /// \param Type Scattered vector type.
  /// \param Alignment Alignment of the scatter.
  /// @return True if the target forces scalarizing of llvm.masked.scatter.
  LLVM_ABI bool forceScalarizeMaskedScatter(VectorType *Type,
                                            Align Alignment) const;

  /// Return true if the target supports masked compress store.
  /// \param DataType Stored type.
  /// \param Alignment Alignment of the store.
  /// @return True if the target supports masked compress store.
  LLVM_ABI bool isLegalMaskedCompressStore(Type *DataType,
                                           Align Alignment) const;
  /// Return true if the target supports masked expand load.
  /// \param DataType Loaded type.
  /// \param Alignment Alignment of the load.
  /// @return True if the target supports masked expand load.
  LLVM_ABI bool isLegalMaskedExpandLoad(Type *DataType, Align Alignment) const;

  /// Return true if the target supports strided load.
  /// \param DataType Loaded or stored type.
  /// \param Alignment Alignment of the access.
  /// @return True if the target supports strided load.
  LLVM_ABI bool isLegalStridedLoadStore(Type *DataType, Align Alignment) const;

  /// Return true is the target supports interleaved access for the given vector
  /// type \p VTy, interleave factor \p Factor, alignment \p Alignment and
  /// address space \p AddrSpace.
  /// \param VTy Vector type of the access.
  /// \param Factor Interleave factor.
  /// \param Alignment Alignment of the access.
  /// \param AddrSpace Address space of the pointer.
  /// @return True is the target supports interleaved access for the given vector.
  LLVM_ABI bool isLegalInterleavedAccessType(VectorType *VTy, unsigned Factor,
                                             Align Alignment,
                                             unsigned AddrSpace) const;

  /// Return true if the target supports masked vector histograms.
  /// \param AddrType Type of the histogram addresses.
  /// \param DataType Type of the histogram data.
  /// @return True if the target supports masked vector histograms.
  LLVM_ABI bool isLegalMaskedVectorHistogram(Type *AddrType,
                                             Type *DataType) const;

  /// Return true if an alternating-opcode pattern can be a single instruction.
  ///
  /// In X86 this is for the addsub instruction which corrsponds to a Shuffle +
  /// Fadd + FSub pattern in IR. This function expectes two opcodes: \p Opcode1
  /// and \p Opcode2 being selected by \p OpcodeMask. The mask contains one bit
  /// per lane and is a `0` when \p Opcode0 is selected and `1` when Opcode1 is
  /// selected. \p VecTy is the vector type of the instruction to be generated.
  /// \param VecTy Vector type of the generated instruction.
  /// \param Opcode0 Opcode selected by a 0 bit in the mask.
  /// \param Opcode1 Opcode selected by a 1 bit in the mask.
  /// \param OpcodeMask Per-lane selector between Opcode0 and Opcode1.
  /// @return True if an alternating-opcode pattern can be a single instruction.
  LLVM_ABI bool isLegalAltInstr(VectorType *VecTy, unsigned Opcode0,
                                unsigned Opcode1,
                                const SmallBitVector &OpcodeMask) const;

  /// Return true if we should be enabling ordered reductions for the target.
  /// @return True if we should be enabling ordered reductions for the target.
  LLVM_ABI bool enableOrderedReductions() const;

  /// Return true if the target has a unified division-and-remainder operation.
  ///
  /// If so, the additional implicit multiplication and subtraction required to
  /// calculate a remainder from division are free. This can enable more
  /// aggressive transformations for division and remainder than would typically
  /// be allowed using throughput or size cost models.
  /// \param DataType Type of the division.
  /// \param IsSigned True if the division is signed.
  /// @return True if the target has a unified division-and-remainder operation.
  LLVM_ABI bool hasDivRemOp(Type *DataType, bool IsSigned) const;

  /// Return true if the memory access \p I has a volatile variant.
  ///
  /// If that's the case then we can avoid addrspacecast to generic AS for
  /// volatile loads/stores. Default implementation returns false, which
  /// prevents address space inference for volatile loads/stores.
  /// \param I Memory access instruction.
  /// \param AddrSpace Address space of the access.
  /// @return True if the memory access \p I has a volatile variant.
  LLVM_ABI bool hasVolatileVariant(Instruction *I, unsigned AddrSpace) const;

  /// Return true if target doesn't mind addresses in vectors.
  /// @return True if target doesn't mind addresses in vectors.
  LLVM_ABI bool prefersVectorizedAddressing() const;

  /// Return the cost of the scaling factor used in addressing mode AM.
  ///
  /// For a load/store of the specified type. If the AM is supported, the return
  /// value must be >= 0. If the AM is not supported, it returns a negative
  /// value.
  /// TODO: Handle pre/postinc as well.
  /// \param Ty Type of the load or store.
  /// \param BaseGV Optional base global value.
  /// \param BaseOffset Constant base offset.
  /// \param HasBaseReg True if the mode uses a base register.
  /// \param Scale Scaled-index factor.
  /// \param AddrSpace Address space of the pointer.
  /// @return The cost of the scaling factor used in addressing mode AM.
  LLVM_ABI InstructionCost getScalingFactorCost(Type *Ty, GlobalValue *BaseGV,
                                                StackOffset BaseOffset,
                                                bool HasBaseReg, int64_t Scale,
                                                unsigned AddrSpace = 0) const;

  /// Return true if LSR should query isLegalAddressingMode with an Instruction.
  ///
  /// This is needed on SystemZ, where e.g. a memcpy can only have a 12 bit
  /// unsigned immediate offset and no index register.
  /// @return True if LSR should query isLegalAddressingMode with an Instruction.
  LLVM_ABI bool LSRWithInstrQueries() const;

  /// Return true if it's free to truncate a value of type Ty1 to type Ty2.
  ///
  /// e.g. On x86 it's free to truncate a i32 value in register EAX to i16 by
  /// referencing its sub-register AX.
  /// \param Ty1 Source type.
  /// \param Ty2 Destination type.
  /// @return True if it's free to truncate a value of type Ty1 to type Ty2.
  LLVM_ABI bool isTruncateFree(Type *Ty1, Type *Ty2) const;

  /// Return true if it is profitable to hoist instruction in the
  /// then/else to before if.
  /// \param I Instruction to hoist.
  /// @return True if it is profitable to hoist instruction in the.
  LLVM_ABI bool isProfitableToHoist(Instruction *I) const;

  /// Return true if alias analysis should be used in target-specific transforms.
  /// @return True if alias analysis should be used in target-specific transforms.
  LLVM_ABI bool useAA() const;

  /// Return true if this type is legal.
  /// \param Ty Type to test.
  /// @return True if this type is legal.
  LLVM_ABI bool isTypeLegal(Type *Ty) const;

  /// Returns the estimated number of registers required to represent \p Ty.
  /// \param Ty Type whose register usage is estimated.
  /// @return The estimated number of registers required to represent \p Ty.
  LLVM_ABI unsigned getRegUsageForType(Type *Ty) const;

  /// Return true if switches should be turned into lookup tables for the
  /// target.
  /// @return True if switches should be turned into lookup tables for the.
  LLVM_ABI bool shouldBuildLookupTables() const;

  /// Return true if switches should be turned into lookup tables
  /// containing this constant value for the target.
  /// \param C Constant used as a lookup-table entry.
  /// @return True if switches should be turned into lookup tables.
  LLVM_ABI bool shouldBuildLookupTablesForConstant(Constant *C) const;

  /// Return the minimum bit width to use for integer switch lookup table
  /// elements on this target.
  /// @return The minimum bit width to use for integer switch lookup table.
  LLVM_ABI unsigned getMinimumLookupTableEntryBitWidth() const;

  /// Return true if lookup tables should be turned into relative lookup tables.
  /// @return True if lookup tables should be turned into relative lookup tables.
  LLVM_ABI bool shouldBuildRelLookupTables() const;

  /// Return true if the input function which is cold at all call sites,
  ///  should use coldcc calling convention.
  /// \param F Function that is cold at all call sites.
  /// @return True if the input function which is cold at all call sites,.
  LLVM_ABI bool useColdCCForColdCall(Function &F) const;

  /// Return true if the input function is internal, should use fastcc calling
  /// convention.
  /// \param F Internal function to consider.
  /// @return True if the input function is internal, should use fastcc calling.
  LLVM_ABI bool useFastCCForInternalCall(Function &F) const;

  /// Identify if the vector form of the intrinsic has a scalar operand.
  /// \param ID Intrinsic ID.
  /// \param ScalarOpdIdx Operand index expected to remain scalar.
  /// @return True if the vector form of the intrinsic has a scalar operand at the given argument.
  LLVM_ABI bool isTargetIntrinsicWithScalarOpAtArg(Intrinsic::ID ID,
                                                   unsigned ScalarOpdIdx) const;

  /// Identify if the vector form of the intrinsic is overloaded at \p OpdIdx.
  ///
  /// Overloaded on the type of the operand at index \p OpdIdx, or on the return
  /// type if \p OpdIdx is -1.
  /// \param ID Intrinsic ID.
  /// \param OpdIdx Operand index, or -1 for the return type.
  /// @return True if the vector form of the intrinsic is overloaded at \p OpdIdx.
  LLVM_ABI bool isTargetIntrinsicWithOverloadTypeAtArg(Intrinsic::ID ID,
                                                       int OpdIdx) const;

  /// Identify if a struct-returning vector intrinsic is overloaded at \p RetIdx.
  /// \param ID Intrinsic ID.
  /// \param RetIdx Struct element index of the overload.
  /// @return True if the struct-returning vector intrinsic is overloaded at \p RetIdx.
  LLVM_ABI bool
  isTargetIntrinsicWithStructReturnOverloadAtField(Intrinsic::ID ID,
                                                   int RetIdx) const;

  /// Alias of \c llvm::VectorInstrContext for use on TargetTransformInfo.
  using VectorInstrContext = llvm::VectorInstrContext;

  /// Calculates a VectorInstrContext from \p I.
  /// \param I Insert, extract, or related instruction, or null.
  /// @return A VectorInstrContext hint derived from \p I.
  LLVM_ABI static VectorInstrContext
  getVectorInstrContextHint(const Instruction *I);

  /// Estimate the overhead of scalarizing an instruction.
  ///
  /// Insert and Extract are set if the demanded result elements need to be
  /// inserted and/or extracted from vectors.  The involved values may be
  /// passed in VL if Insert is true.
  /// \param Ty Vector type being scalarized.
  /// \param DemandedElts Mask of demanded result elements.
  /// \param Insert True if results must be inserted into a vector.
  /// \param Extract True if results must be extracted from a vector.
  /// \param CostKind Kind of cost model to apply.
  /// \param ForPoisonSrc True if the source can be assumed poison.
  /// \param VL Optional values involved when Insert is true.
  /// \param VIC Hint about how the vector instruction is used.
  /// @return Estimated overhead of scalarizing the instruction.
  LLVM_ABI InstructionCost getScalarizationOverhead(
      VectorType *Ty, const APInt &DemandedElts, bool Insert, bool Extract,
      TTI::TargetCostKind CostKind, bool ForPoisonSrc = true,
      ArrayRef<Value *> VL = {},
      TTI::VectorInstrContext VIC = TTI::VectorInstrContext::None) const;

  /// Estimate the overhead of scalarizing operands with the given types.
  ///
  /// The (potentially vector) types to use for each of argument are passes via
  /// Tys.
  /// \param Tys Types of the operands being scalarized.
  /// \param CostKind Kind of cost model to apply.
  /// \param VIC Hint about how the vector instruction is used.
  /// @return Estimated overhead of scalarizing operands with the given types.
  LLVM_ABI InstructionCost getOperandsScalarizationOverhead(
      ArrayRef<Type *> Tys, TTI::TargetCostKind CostKind,
      TTI::VectorInstrContext VIC = TTI::VectorInstrContext::None) const;

  /// Return true if vector element load/store is cheap enough to skip insert/extract.
  ///
  /// If target has efficient vector element load/store instructions, it can
  /// return true here so that insertion/extraction costs are not added to
  /// the scalarization cost of a load/store.
  /// @return True if vector element load/store is cheap enough to skip insert/extract.
  LLVM_ABI bool supportsEfficientVectorElementLoadStore() const;

  /// If the target supports tail calls.
  /// @return True if the target supports tail calls.
  LLVM_ABI bool supportsTailCalls() const;

  /// If target supports tail call on \p CB
  /// \param CB Call to test.
  /// @return True if the target supports a tail call on \p CB.
  LLVM_ABI bool supportsTailCallFor(const CallBase *CB) const;

  /// Don't restrict interleaved unrolling to small loops.
  /// \param LoopHasReductions True if the loop contains reductions.
  /// @return True if interleaved unrolling should not be restricted to small loops.
  LLVM_ABI bool enableAggressiveInterleaving(bool LoopHasReductions) const;

  /// Returns options for expansion of memcmp. IsZeroCmp is
  // true if this is the expansion of memcmp(p1, p2, s) == 0.
  struct MemCmpExpansionOptions {
    /// Return true if memcmp expansion is enabled.
    /// @return True if memcmp expansion is enabled.
    operator bool() const { return MaxNumLoads > 0; }

    /// Maximum number of load operations.
    unsigned MaxNumLoads = 0;

    /// Available load sizes in bytes, sorted in decreasing order.
    SmallVector<unsigned, 8> LoadSizes;

    /// Maximum number of load pairs allowed in a single expanded block.
    ///
    /// For memcmp expansion, allow up to this number of load pairs per block.
    /// As an example, this may allow 'memcmp(a, b, 3) == 0' in a single block:
    ///   a0 = load2bytes &a[0]
    ///   b0 = load2bytes &b[0]
    ///   a2 = load1byte  &a[2]
    ///   b2 = load1byte  &b[2]
    ///   r  = cmp eq (a0 ^ b0 | a2 ^ b2), 0
    /// Equality comparisons combine the differences with xor/or. Ordering
    /// comparisons pack the loads in memory order into a wider integer before
    /// comparing, without exceeding the target's preferred load width.
    unsigned NumLoadsPerBlock = 1;

    /// Allow overlapping loads when expanding memcmp.
    ///
    /// For example, 7-byte compares can be done with two 4-byte compares
    /// instead of 4+2+1-byte compares. This requires all loads in LoadSizes to
    /// be doable in an unaligned way.
    bool AllowOverlappingLoads = false;

    /// Allowed memcmp tail sizes that can be merged into one expanded block.
    ///
    /// Sometimes, the amount of data that needs to be compared is smaller than
    /// the standard register size, but it cannot be loaded with just one load
    /// instruction. For example, if the size of the memory comparison is 6
    /// bytes, we can handle it more efficiently by loading all 6 bytes in a
    /// single block and generating an 8-byte number, instead of generating two
    /// separate blocks with conditional jumps for 4 and 2 byte loads. This
    /// approach simplifies the process and produces the comparison result as
    /// normal. This array lists the allowed sizes of memcmp tails that can be
    /// merged into one block
    SmallVector<unsigned, 4> AllowedTailExpansions;
  };
  /// Return target options for expanding memcmp.
  /// \param OptSize True when optimizing for size.
  /// \param IsZeroCmp True if expanding memcmp(...) == 0.
  /// @return Target options for expanding memcmp.
  LLVM_ABI MemCmpExpansionOptions enableMemCmpExpansion(bool OptSize,
                                                        bool IsZeroCmp) const;

  /// Should the Select Optimization pass be enabled and ran.
  /// @return True if the Select Optimization pass should be enabled.
  LLVM_ABI bool enableSelectOptimize() const;

  /// Return true if SelectOpt should treat \p I like a select.
  ///
  /// This can include select-like instructions like or(zext(c), x) that can be
  /// converted to selects.
  /// \param I Instruction to test.
  /// @return True if SelectOpt should treat \p I like a select.
  LLVM_ABI bool shouldTreatInstructionLikeSelect(const Instruction *I) const;

  /// Enable matching of interleaved access groups.
  /// @return True if matching of interleaved access groups is enabled.
  LLVM_ABI bool enableInterleavedAccessVectorization() const;

  /// Enable matching of interleaved access groups that contain predicated
  /// accesses or gaps and therefore vectorized using masked
  /// vector loads/stores.
  /// @return True if matching of predicated interleaved access groups is enabled.
  LLVM_ABI bool enableMaskedInterleavedAccessVectorization() const;

  /// Return true if auto-vectorizing FP ops may change scalar FP semantics.
  ///
  /// For example, ARM NEON v7 SIMD math does not support IEEE-754 denormal
  /// numbers, while depending on the platform, scalar floating-point math does.
  /// This applies to floating-point math operations and calls, not memory
  /// operations, shuffles, or casts.
  /// @return True if auto-vectorizing FP ops may change scalar FP semantics.
  LLVM_ABI bool isFPVectorizationPotentiallyUnsafe() const;

  /// Determine if the target supports unaligned memory accesses.
  /// \param Context LLVM context.
  /// \param BitWidth Access size in bits.
  /// \param AddressSpace Address space of the access.
  /// \param Alignment Alignment of the access.
  /// \param Fast Optional out-parameter set if the access is fast.
  /// @return True if the target supports unaligned memory accesses of the given type.
  LLVM_ABI bool allowsMisalignedMemoryAccesses(LLVMContext &Context,
                                               unsigned BitWidth,
                                               unsigned AddressSpace = 0,
                                               Align Alignment = Align(1),
                                               unsigned *Fast = nullptr) const;

  /// Return hardware support for population count.
  /// \param IntTyWidthInBit Width in bits of the integer type.
  /// @return Hardware support for population count.
  LLVM_ABI PopcntSupportKind getPopcntSupport(unsigned IntTyWidthInBit) const;

  /// Return true if the hardware has a fast square-root instruction.
  /// \param Ty Type of the square-root operation.
  /// @return True if the hardware has a fast square-root instruction.
  LLVM_ABI bool haveFastSqrt(Type *Ty) const;

  /// Return true if the hardware has a fast carry-less multiplication
  /// instruction.
  /// \param Ty Integer type of the clmul operation.
  /// @return True if the hardware has a fast carry-less multiplication.
  LLVM_ABI bool haveFastClmul(IntegerType *Ty) const;

  /// Return true if \p I is too expensive to speculatively execute.
  ///
  /// This normally just wraps around a getInstructionCost() call, but some
  /// targets might report a low TCK_SizeAndLatency value that is incompatible
  /// with the fixed TCC_Expensive value.
  /// NOTE: This assumes the instruction passes isSafeToSpeculativelyExecute().
  /// \param I Instruction to cost.
  /// @return True if \p I is too expensive to speculatively execute.
  LLVM_ABI bool isExpensiveToSpeculativelyExecute(const Instruction *I) const;

  /// Return true if an FP NaN check is cheaper than comparing against 0.0.
  ///
  /// Targets should override this if materializing a 0.0 for comparison is
  /// generally as cheap as checking for ordered/unordered.
  /// \param Ty Floating-point type being compared.
  /// @return True if an FP NaN check is cheaper than comparing against 0.0.
  LLVM_ABI bool isFCmpOrdCheaperThanFCmpZero(Type *Ty) const;

  /// Return the expected cost of supporting the floating point operation
  /// of the specified type.
  /// \param Ty Floating-point type.
  /// @return The expected cost of supporting the floating point operation.
  LLVM_ABI InstructionCost getFPOpCost(Type *Ty) const;

  /// Return the expected cost of materializing the given integer immediate.
  /// \param Imm Immediate value.
  /// \param Ty Type of the immediate.
  /// \param CostKind Kind of cost model to apply.
  /// @return The expected cost of materializing the given integer immediate.
  LLVM_ABI InstructionCost getIntImmCost(const APInt &Imm, Type *Ty,
                                         TargetCostKind CostKind) const;

  /// Return the materialization cost of an immediate used by an instruction.
  ///
  /// The cost can be zero if the immediate can be folded into the specified
  /// instruction.
  /// \param Opc Opcode of the using instruction.
  /// \param Idx Operand index of the immediate.
  /// \param Imm Immediate value.
  /// \param Ty Type of the immediate.
  /// \param CostKind Kind of cost model to apply.
  /// \param Inst Optional using instruction.
  /// @return The materialization cost of an immediate used by an instruction.
  LLVM_ABI InstructionCost getIntImmCostInst(unsigned Opc, unsigned Idx,
                                             const APInt &Imm, Type *Ty,
                                             TargetCostKind CostKind,
                                             Instruction *Inst = nullptr) const;
  /// Return the materialization cost of an immediate used by an intrinsic.
  /// \param IID Intrinsic ID of the using call.
  /// \param Idx Operand index of the immediate.
  /// \param Imm Immediate value.
  /// \param Ty Type of the immediate.
  /// \param CostKind Kind of cost model to apply.
  /// @return The materialization cost of an immediate used by an intrinsic.
  LLVM_ABI InstructionCost getIntImmCostIntrin(Intrinsic::ID IID, unsigned Idx,
                                               const APInt &Imm, Type *Ty,
                                               TargetCostKind CostKind) const;

  /// Return the size-optimization cost of materializing the given integer.
  ///
  /// This is different than the other integer immediate cost functions in that
  /// it is subtarget agnostic. This is useful when you e.g. target one ISA such
  /// as Aarch32 but smaller encodings could be possible with another such as
  /// Thumb. This return value is used as a penalty when the total costs for a
  /// constant is calculated (the bigger the cost, the more beneficial constant
  /// hoisting is).
  /// \param Opc Opcode of the using instruction.
  /// \param Idx Operand index of the immediate.
  /// \param Imm Immediate value.
  /// \param Ty Type of the immediate.
  /// @return The size-optimization cost of materializing the given integer.
  LLVM_ABI InstructionCost getIntImmCodeSizeCost(unsigned Opc, unsigned Idx,
                                                 const APInt &Imm,
                                                 Type *Ty) const;

  /// Return true if complex constants should stay attached to \p Inst.
  ///
  /// It can be advantageous to detach complex constants from their uses to make
  /// their generation cheaper. This hook allows targets to report when such
  /// transformations might negatively effect the code generation of the
  /// underlying operation. The motivating example is divides whereby hoisting
  /// constants prevents the code generator's ability to transform them into
  /// combinations of simpler operations.
  /// \param Inst Instruction that uses the constant.
  /// \param Fn Function containing the instruction.
  /// @return True if complex constants should stay attached to \p Inst.
  LLVM_ABI bool preferToKeepConstantsAttached(const Instruction &Inst,
                                              const Function &Fn) const;

  /// @}

  /// \name Vector Target Information
  /// @{

  /// The various kinds of shuffle patterns for vector queries.
  enum ShuffleKind {
    SK_Broadcast,        ///< Broadcast element 0 to all other elements.
    SK_Reverse,          ///< Reverse the order of the vector.
    SK_Select,           ///< Selects elements from the corresponding lane of
                         ///< either source operand. This is equivalent to a
                         ///< vector select with a constant condition operand.
    SK_Transpose,        ///< Transpose two vectors.
    SK_InsertSubvector,  ///< InsertSubvector. Index indicates start offset.
    SK_ExtractSubvector, ///< ExtractSubvector Index indicates start offset.
    SK_PermuteTwoSrc,    ///< Merge elements from two source vectors into one
                         ///< with any shuffle mask.
    SK_PermuteSingleSrc, ///< Shuffle elements of single source vector with any
                         ///< shuffle mask.
    SK_Splice            ///< Concatenates elements from the first input vector
                         ///< with elements of the second input vector. Returning
                         ///< a vector of the same type as the input vectors.
                         ///< Index indicates start offset in first input vector.
  };

  /// Additional information about an operand's possible values.
  enum OperandValueKind {
    OK_AnyValue,               ///< Operand can have any value.
    OK_UniformValue,           ///< Operand is uniform (splat of a value).
    OK_UniformConstantValue,   ///< Operand is uniform constant.
    OK_NonUniformConstantValue ///< Operand is a non uniform constant value.
  };

  /// Additional properties of an operand's values.
  enum OperandValueProperties {
    OP_None = 0,            ///< No extra properties.
    OP_PowerOf2 = 1,        ///< Operand is a power of two.
    OP_NegatedPowerOf2 = 2, ///< Operand is a negated power of two.
  };

  /// Describe the values an operand can take.
  ///
  /// We're in the process of migrating uses of OperandValueKind and
  /// OperandValueProperties to use this class, and then will change the
  /// internal representation.
  struct OperandValueInfo {
    /// Classification of whether the operand is uniform or constant.
    OperandValueKind Kind = OK_AnyValue;
    /// Extra properties such as power-of-two.
    OperandValueProperties Properties = OP_None;

    /// Return true if the operand is a constant (uniform or not).
    /// @return True if the operand is a constant (uniform or not).
    bool isConstant() const {
      return Kind == OK_UniformConstantValue || Kind == OK_NonUniformConstantValue;
    }
    /// Return true if the operand is uniform across lanes.
    /// @return True if the operand is uniform across lanes.
    bool isUniform() const {
      return Kind == OK_UniformConstantValue || Kind == OK_UniformValue;
    }
    /// Return true if the operand is a power of two.
    /// @return True if the operand is a power of two.
    bool isPowerOf2() const {
      return Properties == OP_PowerOf2;
    }
    /// Return true if the operand is a negated power of two.
    /// @return True if the operand is a negated power of two.
    bool isNegatedPowerOf2() const {
      return Properties == OP_NegatedPowerOf2;
    }

    /// Return a copy of this info with properties cleared.
    /// @return A copy of this info with properties cleared.
    OperandValueInfo getNoProps() const {
      return {Kind, OP_None};
    }

    /// Merge this operand info with \p OpInfoY.
    /// \param OpInfoY Other operand info to combine with.
    /// @return Operand value info combining properties from this info and \p OpInfoY.
    OperandValueInfo mergeWith(const OperandValueInfo OpInfoY) {
      OperandValueKind MergeKind = OK_AnyValue;
      if (isConstant() && OpInfoY.isConstant())
        MergeKind = OK_NonUniformConstantValue;

      OperandValueProperties MergeProp = OP_None;
      if (Properties == OpInfoY.Properties)
        MergeProp = Properties;
      return {MergeKind, MergeProp};
    }
  };

  /// Return the number of registers in the target-provided register class.
  /// \param ClassID Register class identifier.
  /// @return The number of registers in the target-provided register class.
  LLVM_ABI unsigned getNumberOfRegisters(unsigned ClassID) const;

  /// Return true if the target supports fault-suppressing predicated load/store.
  /// \param Ty Access type.
  /// \param IsStore True to query stores; false to query loads.
  /// @return True if the target supports fault-suppressing predicated load/store.
  LLVM_ABI bool hasConditionalLoadStoreForType(Type *Ty, bool IsStore) const;

  /// Return the target-provided register class ID for the provided type.
  ///
  /// Accounts for type promotion and other type-legalization techniques that
  /// the target might apply. However, it specifically does not account for the
  /// scalarization or splitting of vector types. Should a vector type require
  /// scalarization or splitting into multiple underlying vector registers, that
  /// type should be mapped to a register class containing no registers.
  /// Specifically, this is designed to provide a simple, high-level view of the
  /// register allocation later performed by the backend. These register classes
  /// don't necessarily map onto the register classes used by the backend.
  /// FIXME: It's not currently possible to determine how many registers
  /// are used by the provided type.
  /// \param Vector True if the type is a vector.
  /// \param Ty Type to classify, or null.
  /// @return The target-provided register class ID for the provided type.
  LLVM_ABI unsigned getRegisterClassForType(bool Vector,
                                            Type *Ty = nullptr) const;

  /// Return the target-provided register class name.
  /// \param ClassID Register class identifier.
  /// @return The target-provided register class name.
  LLVM_ABI const char *getRegisterClassName(unsigned ClassID) const;

  /// Return the cost of spilling a register in the given class to the stack.
  /// \param ClassID Register class identifier.
  /// \param CostKind Kind of cost model to apply.
  /// @return The cost of spilling a register in the given class to the stack.
  LLVM_ABI InstructionCost
  getRegisterClassSpillCost(unsigned ClassID, TargetCostKind CostKind) const;

  /// Return the cost of reloading a register in the given class from the stack.
  /// \param ClassID Register class identifier.
  /// \param CostKind Kind of cost model to apply.
  /// @return The cost of reloading a register in the given class from the stack.
  LLVM_ABI InstructionCost
  getRegisterClassReloadCost(unsigned ClassID, TargetCostKind CostKind) const;

  /// Kind of register whose bit width is being queried.
  enum RegisterKind {
    RGK_Scalar,           ///< Scalar registers.
    RGK_FixedWidthVector, ///< Fixed-width vector registers.
    RGK_ScalableVector    ///< Scalable vector registers.
  };

  /// Return the width of the largest scalar or vector register type.
  /// \param K Kind of register to query.
  /// @return The width of the largest scalar or vector register type.
  LLVM_ABI TypeSize getRegisterBitWidth(RegisterKind K) const;

  /// Return the width of the smallest vector register type.
  /// @return The width of the smallest vector register type.
  LLVM_ABI unsigned getMinVectorRegisterBitWidth() const;

  /// Return the architectural maximum vscale, if the target specifies one.
  /// @return The architectural maximum vscale, if the target specifies one.
  LLVM_ABI std::optional<unsigned> getMaxVScale() const;

  /// Return the vscale value used to tune the cost model.
  /// @return The vscale value used to tune the cost model.
  LLVM_ABI std::optional<unsigned> getVScaleForTuning() const;

  /// Return true if VF should match the smallest element type to a register.
  ///
  /// For wider element types, this could result in creating vectors that span
  /// multiple vector registers. If false, the vectorization factor will be
  /// chosen based on the size of the widest element type.
  /// \param K Register kind for vectorization.
  /// @return True if VF should match the smallest element type to a register.
  LLVM_ABI bool
  shouldMaximizeVectorBandwidth(TargetTransformInfo::RegisterKind K) const;

  /// Return the minimum VF for the given element width, or 0 if there is none.
  ///
  /// The returned value only applies when shouldMaximizeVectorBandwidth
  /// returns true. If IsScalable is true, the returned ElementCount must be a
  /// scalable VF.
  /// \param ElemWidth Element size in bits.
  /// \param IsScalable True to request a scalable VF.
  /// @return The minimum VF for the given element width, or 0 if there is none.
  LLVM_ABI ElementCount getMinimumVF(unsigned ElemWidth, bool IsScalable) const;

  /// Return the maximum VF for the given element width and opcode, or 0.
  ///
  /// Currently only used by the SLP vectorizer.
  /// \param ElemWidth Element size in bits.
  /// \param Opcode Instruction opcode.
  /// @return The maximum VF for the given element width and opcode, or 0.
  LLVM_ABI unsigned getMaximumVF(unsigned ElemWidth, unsigned Opcode) const;

  /// Return the minimum vectorization factor for a store instruction.
  ///
  /// Given the initial estimation of the minimum vector factor and store value
  /// type, it tries to find possible lowest VF, which still might be profitable
  /// for the vectorization.
  /// \param VF Initial estimation of the minimum vector factor.
  /// \param ScalarMemTy Scalar memory type of the store operation.
  /// \param ScalarValTy Scalar type of the stored value.
  /// \param Alignment Alignment of the store
  /// \param AddrSpace Address space of the store
  /// Currently only used by the SLP vectorizer.
  /// @return The minimum vectorization factor for a store instruction.
  LLVM_ABI unsigned getStoreMinimumVF(unsigned VF, Type *ScalarMemTy,
                                      Type *ScalarValTy, Align Alignment,
                                      unsigned AddrSpace) const;

  /// Return true if address type promotion should be considered for \p I.
  /// \param I Instruction being considered for promotion.
  /// \param AllowPromotionWithoutCommonHeader Set true if promoting \p I is
  /// profitable without finding other extensions fed by the same input.
  /// @return True if address type promotion should be considered for \p I.
  LLVM_ABI bool shouldConsiderAddressTypePromotion(
      const Instruction &I, bool &AllowPromotionWithoutCommonHeader) const;

  /// Return the size of a cache line in bytes.
  /// @return The size of a cache line in bytes.
  LLVM_ABI unsigned getCacheLineSize() const;

  /// The possible cache levels.
  enum class CacheLevel {
    L1D, ///< The L1 data cache.
    L2D, ///< The L2 data cache.

    // We currently do not model L3 caches, as their sizes differ widely between
    // microarchitectures. Also, we currently do not have a use for L3 cache
    // size modeling yet.
  };

  /// Return the size of the cache level in bytes, if available.
  /// \param Level Cache level to query.
  /// @return The size of the cache level in bytes, if available.
  LLVM_ABI std::optional<unsigned> getCacheSize(CacheLevel Level) const;

  /// Return the associativity of the cache level, if available.
  /// \param Level Cache level to query.
  /// @return The associativity of the cache level, if available.
  LLVM_ABI std::optional<unsigned>
  getCacheAssociativity(CacheLevel Level) const;

  /// Return the minimum architectural page size for the target.
  /// @return The minimum architectural page size for the target.
  LLVM_ABI std::optional<unsigned> getMinPageSize() const;

  /// Return how many instructions before a load a prefetch should be placed.
  /// @return How many instructions before a load a prefetch should be placed.
  LLVM_ABI unsigned getPrefetchDistance() const;

  /// Return the minimum stride in bytes at which software prefetching is useful.
  ///
  /// Some HW prefetchers can handle accesses up to a certain constant stride.
  /// Sometimes prefetching is beneficial even below the HW prefetcher limit,
  /// and the arguments provided are meant to serve as a basis for deciding this
  /// for a particular loop.
  ///
  /// \param NumMemAccesses        Number of memory accesses in the loop.
  /// \param NumStridedMemAccesses Number of the memory accesses that
  ///                              ScalarEvolution could find a known stride
  ///                              for.
  /// \param NumPrefetches         Number of software prefetches that will be
  ///                              emitted as determined by the addresses
  ///                              involved and the cache line size.
  /// \param HasCall               True if the loop contains a call.
  ///
  /// \return This is the minimum stride in bytes where it makes sense to start
  ///         adding SW prefetches. The default is 1, i.e. prefetch with any
  ///         stride.
  LLVM_ABI unsigned getMinPrefetchStride(unsigned NumMemAccesses,
                                         unsigned NumStridedMemAccesses,
                                         unsigned NumPrefetches,
                                         bool HasCall) const;

  /// Return the maximum number of iterations to prefetch ahead.
  ///
  /// If the required number of iterations is more than this number, no
  /// prefetching is performed.
  /// @return The maximum number of iterations to prefetch ahead.
  LLVM_ABI unsigned getMaxPrefetchIterationsAhead() const;

  /// Return true if prefetching should also be done for writes.
  /// @return True if prefetching should also be done for writes.
  LLVM_ABI bool enableWritePrefetching() const;

  /// Return true if the target wants a prefetch in address space \p AS.
  /// \param AS Address space to query.
  /// @return True if the target wants a prefetch in address space \p AS.
  LLVM_ABI bool shouldPrefetchAddressSpace(unsigned AS) const;

  /// Return the cost of a partial reduction from a vector to a narrower vector.
  ///
  /// They are represented by the llvm.vector.partial.reduce.add and
  /// llvm.vector.partial.reduce.fadd intrinsics, which take an accumulator of
  /// type \p AccumType and a second vector operand to be accumulated, whose
  /// element count is specified by \p VF. The type of reduction is specified by
  /// \p Opcode. The second operand passed to the intrinsic could be the result
  /// of an extend, such as sext or zext. In this case \p BinOp is nullopt,
  /// \p InputTypeA represents the type being extended and \p OpAExtend the
  /// operation, i.e. sign- or zero-extend.
  /// For floating-point partial reductions, any fast math flags (FMF) should be
  /// provided to govern which reductions are valid to perform (depending on
  /// reassoc or contract, for example), whereas this must be nullopt for
  /// integer partial reductions.
  /// Also, \p InputTypeB should be nullptr and OpBExtend should be None.
  /// Alternatively, the second operand could be the result of a binary
  /// operation performed on two extends, i.e.
  ///   mul(zext i8 %a -> i32, zext i8 %b -> i32).
  /// In this case \p BinOp may specify the opcode of the binary operation,
  /// \p InputTypeA and \p InputTypeB the types being extended, and
  /// \p OpAExtend, \p OpBExtend the form of extensions. An example of an
  /// operation that uses a partial reduction is a dot product, which reduces
  /// two vectors in binary mul operation to another of 4 times fewer and 4
  /// times larger elements.
  /// \param Opcode Reduction opcode.
  /// \param InputTypeA Type of the first extended operand, if any.
  /// \param InputTypeB Type of the second extended operand, if any.
  /// \param AccumType Accumulator type.
  /// \param VF Element count of the vector being reduced.
  /// \param OpAExtend Extend kind applied to the first operand.
  /// \param OpBExtend Extend kind applied to the second operand.
  /// \param BinOp Optional binary opcode combining the two extends.
  /// \param CostKind Kind of cost model to apply.
  /// \param FMF Fast-math flags for FP partial reductions, else nullopt.
  /// @return The cost of a partial reduction from a vector to a narrower vector.
  LLVM_ABI InstructionCost getPartialReductionCost(
      unsigned Opcode, Type *InputTypeA, Type *InputTypeB, Type *AccumType,
      ElementCount VF, PartialReductionExtendKind OpAExtend,
      PartialReductionExtendKind OpBExtend, std::optional<unsigned> BinOp,
      TTI::TargetCostKind CostKind, std::optional<FastMathFlags> FMF) const;

  /// Return the maximum interleave factor any transform should try for this target.
  ///
  /// This number depends on the level of parallelism and the number of
  /// execution units in the CPU. HasUnorderedReductions specifies whether
  /// (unordered) reductions are present in the loop being vectorized.
  /// \param VF Vectorization factor of the loop.
  /// \param HasUnorderedReductions True if the loop has unordered reductions.
  /// @return The maximum interleave factor any transform should try for this target.
  LLVM_ABI unsigned getMaxInterleaveFactor(ElementCount VF,
                                           bool HasUnorderedReductions) const;

  /// Collect properties of V used in cost analysis, e.g. OP_PowerOf2.
  /// \param V Value whose operand properties are collected.
  /// @return Operand value properties of \p V used in cost analysis.
  LLVM_ABI static OperandValueInfo getOperandInfo(const Value *V);

  /// Collect common data between two OperandValueInfo inputs.
  /// \param X First value.
  /// \param Y Second value.
  /// @return Operand value info containing properties common to both inputs.
  LLVM_ABI static OperandValueInfo commonOperandInfo(const Value *X,
                                                     const Value *Y);

  /// Estimate the reciprocal-throughput cost of a math or logic instruction.
  ///
  /// A higher cost indicates less expected throughput. From Agner Fog's
  /// guides, reciprocal throughput is "the average number of clock cycles per
  /// instruction when the instructions are not part of a limiting dependency
  /// chain." Therefore, costs should be scaled to account for multiple
  /// execution units on the target that can process this type of instruction.
  /// For example, if there are 5 scalar integer units and 2 vector integer
  /// units that can calculate an 'add' in a single cycle, this model should
  /// indicate that the cost of the vector add instruction is 2.5 times the
  /// cost of the scalar add instruction.
  /// \p Args is an optional argument which holds the instruction operands
  /// values so the TTI can analyze those values searching for special
  /// cases or optimizations based on those values.
  /// \p CxtI is the optional original context instruction, if one exists, to
  /// provide even more information.
  /// \p TLibInfo is used to search for platform specific vector library
  /// functions for instructions that might be converted to calls (e.g. frem).
  /// \param Opcode Arithmetic or logic opcode.
  /// \param Ty Type of the operation.
  /// \param CostKind Kind of cost model to apply.
  /// \param Opd1Info Properties of the first operand.
  /// \param Opd2Info Properties of the second operand.
  /// \param Args Optional operands of the instruction.
  /// \param CxtI Optional original instruction for extra context.
  /// \param TLibInfo Optional target library info for vector libcalls.
  /// @return Estimated reciprocal-throughput cost of the math or logic instruction.
  LLVM_ABI InstructionCost getArithmeticInstrCost(
      unsigned Opcode, Type *Ty, TTI::TargetCostKind CostKind,
      TTI::OperandValueInfo Opd1Info = {TTI::OK_AnyValue, TTI::OP_None},
      TTI::OperandValueInfo Opd2Info = {TTI::OK_AnyValue, TTI::OP_None},
      ArrayRef<const Value *> Args = {}, const Instruction *CxtI = nullptr,
      const TargetLibraryInfo *TLibInfo = nullptr) const;

  /// Return the cost of an alternating-opcode pattern lowered as one instruction.
  ///
  /// In X86 this is for the addsub instruction which corrsponds to a Shuffle +
  /// Fadd + FSub pattern in IR. This function expects two opcodes: \p Opcode1
  /// and \p Opcode2 being selected by \p OpcodeMask. The mask contains one bit
  /// per lane and is a `0` when \p Opcode0 is selected and `1` when Opcode1 is
  /// selected. \p VecTy is the vector type of the instruction to be generated.
  /// \param VecTy Vector type of the generated instruction.
  /// \param Opcode0 Opcode selected by a 0 bit in the mask.
  /// \param Opcode1 Opcode selected by a 1 bit in the mask.
  /// \param OpcodeMask Per-lane selector between Opcode0 and Opcode1.
  /// \param CostKind Kind of cost model to apply.
  /// @return The cost of an alternating-opcode pattern lowered as one instruction.
  LLVM_ABI InstructionCost getAltInstrCost(VectorType *VecTy, unsigned Opcode0,
                                           unsigned Opcode1,
                                           const SmallBitVector &OpcodeMask,
                                           TTI::TargetCostKind CostKind) const;

  /// Return the cost of a shuffle of kind \p Kind from \p SrcTy to \p DstTy.
  ///
  /// The exact mask may be passed as Mask, or else the array will be empty.
  /// The Index and SubTp parameters are used by the subvector insertions
  /// shuffle kinds to show the insert point and the type of the subvector
  /// being inserted. The operands of the shuffle can be passed through \p Args,
  /// which helps improve the cost estimation in some cases, like in broadcast
  /// loads.
  /// \param Kind Shuffle pattern kind.
  /// \param DstTy Result vector type.
  /// \param SrcTy Source vector type.
  /// \param CostKind Kind of cost model to apply.
  /// \param Mask Shuffle mask, or empty if unknown.
  /// \param Index Insert/extract subvector index.
  /// \param SubTp Subvector type for insert/extract kinds.
  /// \param Args Optional shuffle operands.
  /// \param CxtI Optional original shuffle instruction.
  /// @return The cost of a shuffle of kind \p Kind from \p SrcTy to \p DstTy.
  LLVM_ABI InstructionCost getShuffleCost(
      ShuffleKind Kind, VectorType *DstTy, VectorType *SrcTy,
      TTI::TargetCostKind CostKind, ArrayRef<int> Mask = {}, int Index = 0,
      VectorType *SubTp = nullptr, ArrayRef<const Value *> Args = {},
      const Instruction *CxtI = nullptr) const;

  /// Represents a hint about the context in which a cast is used.
  ///
  /// For zext/sext, the context of the cast is the operand, which must be a
  /// load of some kind. For trunc, the context is of the cast is the single
  /// user of the instruction, which must be a store of some kind.
  ///
  /// This enum allows the vectorizer to give getCastInstrCost an idea of the
  /// type of cast it's dealing with, as not every cast is equal. For instance,
  /// the zext of a load may be free, but the zext of an interleaving load can
  //// be (very) expensive!
  ///
  /// See \c getCastContextHint to compute a CastContextHint from a cast
  /// Instruction*. Callers can use it if they don't need to override the
  /// context and just want it to be calculated from the instruction.
  ///
  /// FIXME: This handles the types of load/store that the vectorizer can
  /// produce, which are the cases where the context instruction is most
  /// likely to be incorrect. There are other situations where that can happen
  /// too, which might be handled here but in the long run a more general
  /// solution of costing multiple instructions at the same times may be better.
  enum class CastContextHint : uint8_t {
    None,          ///< The cast is not used with a load/store of any kind.
    Normal,        ///< The cast is used with a normal load/store.
    Masked,        ///< The cast is used with a masked load/store.
    GatherScatter, ///< The cast is used with a gather/scatter.
    Interleave,    ///< The cast is used with an interleaved load/store.
    Reversed,      ///< The cast is used with a reversed load/store.
  };

  /// Calculates a CastContextHint from \p I.
  /// This should be used by callers of getCastInstrCost if they wish to
  /// determine the context from some instruction.
  /// \param I Cast instruction, or null.
  /// \returns the CastContextHint for ZExt/SExt/Trunc, None if \p I is nullptr,
  /// or if it's another type of cast.
  LLVM_ABI static CastContextHint getCastContextHint(const Instruction *I);

  /// Return the expected cost of cast instructions such as bitcast or trunc.
  ///
  /// If there is an existing instruction that holds Opcode, it may be passed
  /// in the 'I' parameter.
  /// \param Opcode Cast opcode.
  /// \param Dst Destination type.
  /// \param Src Source type.
  /// \param CCH Hint about the load/store context of the cast.
  /// \param CostKind Kind of cost model to apply.
  /// \param I Optional existing cast instruction.
  /// @return The expected cost of cast instructions such as bitcast or trunc.
  LLVM_ABI InstructionCost getCastInstrCost(
      unsigned Opcode, Type *Dst, Type *Src, TTI::CastContextHint CCH,
      TTI::TargetCostKind CostKind, const Instruction *I = nullptr) const;

  /// Return the expected cost of a sign- or zero-extended vector extract.
  ///
  /// Use Index = -1 to indicate that there is no information about the index
  /// value.
  /// \param Opcode Extend opcode (sext or zext).
  /// \param Dst Scalar destination type.
  /// \param VecTy Source vector type.
  /// \param Index Extract lane, or -1 if unknown.
  /// \param CostKind Kind of cost model to apply.
  /// @return The expected cost of a sign- or zero-extended vector extract.
  LLVM_ABI InstructionCost
  getExtractWithExtendCost(unsigned Opcode, Type *Dst, VectorType *VecTy,
                           unsigned Index, TTI::TargetCostKind CostKind) const;

  /// Return the expected cost of control-flow instructions such as Phi or Br.
  /// \param Opcode Control-flow opcode.
  /// \param CostKind Kind of cost model to apply.
  /// \param I Optional existing instruction.
  /// @return The expected cost of control-flow instructions such as Phi or Br.
  LLVM_ABI InstructionCost getCFInstrCost(unsigned Opcode,
                                          TTI::TargetCostKind CostKind,
                                          const Instruction *I = nullptr) const;

  /// Return the expected cost of compare and select instructions.
  ///
  /// If there is an existing instruction that holds Opcode, it may be passed in
  /// the 'I' parameter. The \p VecPred parameter can be used to indicate the
  /// select is using a compare with the specified predicate as condition. When
  /// vector types are passed, \p VecPred must be used for all lanes.  For a
  /// comparison, the two operands are the natural values.  For a select, the
  /// two operands are the *value* operands, not the condition operand.
  /// \param Opcode Compare or select opcode.
  /// \param ValTy Type of the compared or selected values.
  /// \param CondTy Type of the condition.
  /// \param VecPred Predicate used by a vector compare or select.
  /// \param CostKind Kind of cost model to apply.
  /// \param Op1Info Properties of the first value operand.
  /// \param Op2Info Properties of the second value operand.
  /// \param I Optional existing compare or select instruction.
  /// @return The expected cost of compare and select instructions.
  LLVM_ABI InstructionCost
  getCmpSelInstrCost(unsigned Opcode, Type *ValTy, Type *CondTy,
                     CmpInst::Predicate VecPred, TTI::TargetCostKind CostKind,
                     OperandValueInfo Op1Info = {OK_AnyValue, OP_None},
                     OperandValueInfo Op2Info = {OK_AnyValue, OP_None},
                     const Instruction *I = nullptr) const;

  /// Return the expected cost of vector Insert and Extract.
  ///
  /// Use -1 to indicate that there is no information on the index value.
  /// This is used when the instruction is not available; a typical use
  /// case is to provision the cost of vectorization/scalarization in
  /// vectorizer passes.
  /// \param Opcode Insert or extract opcode.
  /// \param Val Vector type of the insert or extract.
  /// \param CostKind Kind of cost model to apply.
  /// \param Index Lane index, or -1 if unknown.
  /// \param Op0 Optional first operand.
  /// \param Op1 Optional second operand.
  /// \param VIC Hint about how the insert/extract is used.
  /// @return The expected cost of vector Insert and Extract.
  LLVM_ABI InstructionCost getVectorInstrCost(
      unsigned Opcode, Type *Val, TTI::TargetCostKind CostKind,
      unsigned Index = -1, const Value *Op0 = nullptr,
      const Value *Op1 = nullptr,
      TTI::VectorInstrContext VIC = TTI::VectorInstrContext::None) const;

  /// Return the expected cost of vector Insert and Extract.
  ///
  /// Use -1 to indicate that there is no information on the index value.
  /// This is used when the instruction is not available; a typical use
  /// case is to provision the cost of vectorization/scalarization in
  /// vectorizer passes.
  /// \param Opcode Insert or extract opcode.
  /// \param Val Vector type of the insert or extract.
  /// \param CostKind Kind of cost model to apply.
  /// \param Index Lane index, or -1 if unknown.
  /// \param Scalar Value being extracted.
  /// \param ScalarUserAndIdx Encodes extracts with Scalar, User, and Idx.
  /// \param VIC Hint about how the insert/extract is used.
  /// @return The expected cost of vector Insert and Extract.
  LLVM_ABI InstructionCost getVectorInstrCost(
      unsigned Opcode, Type *Val, TTI::TargetCostKind CostKind, unsigned Index,
      Value *Scalar,
      ArrayRef<std::tuple<Value *, User *, int>> ScalarUserAndIdx,
      TTI::VectorInstrContext VIC = TTI::VectorInstrContext::None) const;

  /// Return the expected cost of vector Insert and Extract.
  ///
  /// This is used when instruction is available, and implementation
  /// asserts 'I' is not nullptr.
  ///
  /// A typical suitable use case is cost estimation when vector instruction
  /// exists (e.g., from basic blocks during transformation).
  /// \param I Insert or extract instruction.
  /// \param Val Vector type of the insert or extract.
  /// \param CostKind Kind of cost model to apply.
  /// \param Index Lane index, or -1 if unknown.
  /// \param VIC Hint about how the insert/extract is used.
  /// @return The expected cost of vector Insert and Extract.
  LLVM_ABI InstructionCost getVectorInstrCost(
      const Instruction &I, Type *Val, TTI::TargetCostKind CostKind,
      unsigned Index = -1,
      TTI::VectorInstrContext VIC = TTI::VectorInstrContext::None) const;

  /// Return the cost of inserting or extracting a lane from the vector end.
  ///
  /// The lane is \p Index elements from the end of a vector, i.e. the
  /// mathematical expression for the lane is (VF - 1 - Index). This is required
  /// for scalable vectors where the exact lane index is unknown at compile
  /// time.
  /// \param Opcode Insert or extract opcode.
  /// \param Val Vector type of the insert or extract.
  /// \param CostKind Kind of cost model to apply.
  /// \param Index Distance from the last lane.
  /// @return The cost of inserting or extracting a lane from the vector end.
  LLVM_ABI InstructionCost getIndexedVectorInstrCostFromEnd(
      unsigned Opcode, Type *Val, TTI::TargetCostKind CostKind,
      unsigned Index) const;

  /// Return the expected cost of aggregate inserts and extracts.
  ///
  /// This is used when the instruction is not available; a typical use case is
  /// to provision the cost of vectorization/scalarization in vectorizer passes.
  /// \param Opcode Insertvalue or extractvalue opcode.
  /// \param CostKind Kind of cost model to apply.
  /// @return The expected cost of aggregate inserts and extracts.
  LLVM_ABI InstructionCost getInsertExtractValueCost(
      unsigned Opcode, TTI::TargetCostKind CostKind) const;

  /// Return the cost of a replication shuffle of \p VF elements of type \p EltTy.
  ///
  /// For example, the mask for \p ReplicationFactor=3 and \p VF=4 is:
  ///   <0,0,0,1,1,1,2,2,2,3,3,3>
  /// \param EltTy Element type being replicated.
  /// \param ReplicationFactor Times each element is repeated.
  /// \param VF Number of distinct elements in the source.
  /// \param DemandedDstElts Mask of demanded destination elements.
  /// \param CostKind Kind of cost model to apply.
  /// @return The cost of a replication shuffle of \p VF elements of type \p EltTy.
  LLVM_ABI InstructionCost getReplicationShuffleCost(
      Type *EltTy, int ReplicationFactor, int VF, const APInt &DemandedDstElts,
      TTI::TargetCostKind CostKind) const;

  /// Return the cost of Load and Store instructions.
  ///
  /// The operand info \p OpdInfo should refer to the stored value for stores
  /// and the address for loads.
  /// \param Opcode Load or store opcode.
  /// \param Src Type of the loaded or stored value.
  /// \param Alignment Alignment of the memory operation.
  /// \param AddressSpace Address space of the pointer.
  /// \param CostKind Kind of cost model to apply.
  /// \param OpdInfo Properties of the address or stored value.
  /// \param I Optional existing memory instruction.
  /// @return The cost of Load and Store instructions.
  LLVM_ABI InstructionCost
  getMemoryOpCost(unsigned Opcode, Type *Src, Align Alignment,
                  unsigned AddressSpace, TTI::TargetCostKind CostKind,
                  OperandValueInfo OpdInfo = {OK_AnyValue, OP_None},
                  const Instruction *I = nullptr) const;

  /// Return the cost of an interleaved memory operation.
  /// \param Opcode Memory operation code.
  /// \param VecTy Vector type of the interleaved access.
  /// \param Factor Interleave factor.
  /// \param Indices Indices for interleaved load members (gaps allowed).
  /// \param Alignment Alignment of the memory operation.
  /// \param AddressSpace Address space of the pointer.
  /// \param CostKind Kind of cost model to apply.
  /// \param UseMaskForCond True if the memory access is predicated.
  /// \param UseMaskForGaps True if gaps should be masked.
  /// @return The cost of an interleaved memory operation.
  LLVM_ABI InstructionCost getInterleavedMemoryOpCost(
      unsigned Opcode, Type *VecTy, unsigned Factor, ArrayRef<unsigned> Indices,
      Align Alignment, unsigned AddressSpace, TTI::TargetCostKind CostKind,
      bool UseMaskForCond = false, bool UseMaskForGaps = false) const;

  /// A helper function to determine the type of reduction algorithm used
  /// for a given \p Opcode and set of FastMathFlags \p FMF.
  /// \param FMF Fast-math flags; ordered reduction if reassoc is disallowed.
  /// @return True if an ordered reduction algorithm must be used.
  static bool requiresOrderedReduction(std::optional<FastMathFlags> FMF) {
    return FMF && !(*FMF).allowReassoc();
  }

  /// Calculate the cost of vector reduction intrinsics.
  ///
  /// This is the cost of reducing the vector value of type \p Ty to a scalar
  /// value using the operation denoted by \p Opcode. The FastMathFlags
  /// parameter \p FMF indicates what type of reduction we are performing:
  ///   1. Tree-wise. This is the typical 'fast' reduction performed that
  ///   involves successively splitting a vector into half and doing the
  ///   operation on the pair of halves until you have a scalar value. For
  ///   example:
  ///     (v0, v1, v2, v3)
  ///     ((v0+v2), (v1+v3), undef, undef)
  ///     ((v0+v2+v1+v3), undef, undef, undef)
  ///   This is the default behaviour for integer operations, whereas for
  ///   floating point we only do this if \p FMF indicates that
  ///   reassociation is allowed.
  ///   2. Ordered. For a vector with N elements this involves performing N
  ///   operations in lane order, starting with an initial scalar value, i.e.
  ///     result = InitVal + v0
  ///     result = result + v1
  ///     result = result + v2
  ///     result = result + v3
  ///   This is only the case for FP operations and when reassociation is not
  ///   allowed.
  /// \param Opcode Reduction opcode.
  /// \param Ty Vector type being reduced.
  /// \param FMF Fast-math flags selecting tree-wise vs ordered reduction.
  /// \param CostKind Kind of cost model to apply.
  /// @return Estimated cost of the vector reduction intrinsic.
  LLVM_ABI InstructionCost getArithmeticReductionCost(
      unsigned Opcode, VectorType *Ty, std::optional<FastMathFlags> FMF,
      TTI::TargetCostKind CostKind) const;

  /// Return the cost of a min/max vector reduction.
  /// \param IID Min/max intrinsic ID.
  /// \param Ty Vector type being reduced.
  /// \param FMF Fast-math flags for the reduction.
  /// \param CostKind Kind of cost model to apply.
  /// @return The cost of a min/max vector reduction.
  LLVM_ABI InstructionCost getMinMaxReductionCost(
      Intrinsic::ID IID, VectorType *Ty, FastMathFlags FMF = FastMathFlags(),
      TTI::TargetCostKind CostKind = TTI::TCK_RecipThroughput) const;

  /// Return the cost of a multiply-accumulate reduction pattern.
  ///
  /// Similar to getArithmeticReductionCost of an Add/Sub reduction with
  /// multiply and optional extensions. This is the cost of as:
  /// * ResTy vecreduce.add/sub(mul (A, B)) or,
  /// * ResTy vecreduce.add/sub(mul(ext(Ty A), ext(Ty B)).
  /// \param IsUnsigned True if extensions are unsigned.
  /// \param RedOpcode Reduction opcode (add or sub).
  /// \param ResTy Result type of the reduction.
  /// \param Ty Vector type of the multiplied operands.
  /// \param CostKind Kind of cost model to apply.
  /// @return The cost of a multiply-accumulate reduction pattern.
  LLVM_ABI InstructionCost
  getMulAccReductionCost(bool IsUnsigned, unsigned RedOpcode, Type *ResTy,
                         VectorType *Ty, TTI::TargetCostKind CostKind) const;

  /// Return the cost of an extended reduction pattern.
  ///
  /// Similar to getArithmeticReductionCost of a reduction with an extension.
  /// This is the cost of as:
  /// ResTy vecreduce.opcode(ext(Ty A)).
  /// \param Opcode Reduction opcode.
  /// \param IsUnsigned True if the extension is unsigned.
  /// \param ResTy Result type of the reduction.
  /// \param Ty Vector type being extended and reduced.
  /// \param FMF Fast-math flags for the reduction.
  /// \param CostKind Kind of cost model to apply.
  /// @return The cost of an extended reduction pattern.
  LLVM_ABI InstructionCost getExtendedReductionCost(
      unsigned Opcode, bool IsUnsigned, Type *ResTy, VectorType *Ty,
      std::optional<FastMathFlags> FMF, TTI::TargetCostKind CostKind) const;

  /// Return the cost of Intrinsic instructions. Analyses the real arguments.
  ///
  /// Three cases are handled: 1. scalar instruction 2. vector instruction
  /// 3. scalar instruction which is to be vectorized.
  /// \param ICA Attributes describing the intrinsic.
  /// \param CostKind Kind of cost model to apply.
  /// @return The cost of Intrinsic instructions. Analyses the real arguments.
  LLVM_ABI InstructionCost getIntrinsicInstrCost(
      const IntrinsicCostAttributes &ICA, TTI::TargetCostKind CostKind) const;

  /// Return the cost of memory intrinsic instructions.
  ///
  /// Used when IntrinsicInst is not materialized.
  /// \param MICA Attributes describing the memory intrinsic.
  /// \param CostKind Kind of cost model to apply.
  /// @return The cost of memory intrinsic instructions.
  LLVM_ABI InstructionCost
  getMemIntrinsicInstrCost(const MemIntrinsicCostAttributes &MICA,
                           TTI::TargetCostKind CostKind) const;

  /// Return the cost of Call instructions.
  /// \param F Callee, or null if unknown.
  /// \param RetTy Return type of the call.
  /// \param Tys Argument types of the call.
  /// \param CostKind Kind of cost model to apply.
  /// @return The cost of Call instructions.
  LLVM_ABI InstructionCost getCallInstrCost(Function *F, Type *RetTy,
                                            ArrayRef<Type *> Tys,
                                            TTI::TargetCostKind CostKind) const;

  /// Return how many parts \p Tp splits into during legalization.
  ///
  /// Zero is returned when the answer is unknown.
  /// \param Tp Type to legalize.
  /// @return How many parts \p Tp splits into during legalization.
  LLVM_ABI unsigned getNumberOfParts(Type *Tp) const;

  /// Return the cost of computing an address for a memory operation.
  ///
  /// For most targets this can be merged into the instruction indexing mode.
  /// Some targets might want to distinguish between address computation for
  /// memory operations with vector pointer types and scalar pointer types.
  /// Such targets should override this function. \p SE holds the pointer for
  /// the scalar evolution object which was used in order to get the Ptr step
  /// value. \p Ptr holds the SCEV of the access pointer.
  /// \param PtrTy Pointer type of the address.
  /// \param SE Scalar evolution used to analyze the pointer.
  /// \param Ptr SCEV of the access pointer.
  /// \param CostKind Kind of cost model to apply.
  /// @return The cost of computing an address for a memory operation.
  LLVM_ABI InstructionCost
  getAddressComputationCost(Type *PtrTy, ScalarEvolution *SE, const SCEV *Ptr,
                            TTI::TargetCostKind CostKind) const;

  /// Return the cost of keeping values of the given types alive over a call.
  ///
  /// Some types may require the use of register classes that do not have
  /// any callee-saved registers, so would require a spill and fill.
  /// \param Tys Types that must remain live across the callsite.
  /// @return The cost of keeping values of the given types alive over a call.
  LLVM_ABI InstructionCost
  getCostOfKeepingLiveOverCall(ArrayRef<Type *> Tys) const;

  /// Return true if the intrinsic is a supported memory intrinsic.
  ///
  /// Info will contain additional information - whether the intrinsic may write
  /// or read to memory, volatility and the pointer.  Info is undefined
  /// if false is returned.
  /// \param Inst Intrinsic to query.
  /// \param Info Filled with memory-intrinsic details on success.
  /// @return True if the intrinsic is a supported memory intrinsic.
  LLVM_ABI bool getTgtMemIntrinsic(IntrinsicInst *Inst,
                                   MemIntrinsicInfo &Info) const;

  /// Return the max element size in bytes for unordered-atomic mem intrinsics.
  /// @return The max element size in bytes for unordered-atomic mem intrinsics.
  LLVM_ABI unsigned getAtomicMemIntrinsicMaxElementSize() const;

  /// Return a value holding the result of the given memory intrinsic.
  ///
  /// If \p CanCreate is true, new instructions may be created to extract the
  /// result from the given intrinsic memory operation. Returns nullptr if the
  /// target cannot create a result from the given intrinsic.
  /// \param Inst Memory intrinsic to extract a result from.
  /// \param ExpectedType Expected type of the result.
  /// \param CanCreate True if new instructions may be created.
  /// @return A value holding the result of the given memory intrinsic.
  LLVM_ABI Value *
  getOrCreateResultFromMemIntrinsic(IntrinsicInst *Inst, Type *ExpectedType,
                                    bool CanCreate = true) const;

  /// Return the type to use in a loop expansion of a memcpy call.
  /// \param Context LLVM context used to construct types.
  /// \param Length Length of the copy.
  /// \param SrcAddrSpace Source address space.
  /// \param DestAddrSpace Destination address space.
  /// \param SrcAlign Source alignment.
  /// \param DestAlign Destination alignment.
  /// \param AtomicElementSize Optional atomic element size in bytes.
  /// @return The type to use in a loop expansion of a memcpy call.
  LLVM_ABI Type *getMemcpyLoopLoweringType(
      LLVMContext &Context, Value *Length, unsigned SrcAddrSpace,
      unsigned DestAddrSpace, Align SrcAlign, Align DestAlign,
      std::optional<uint32_t> AtomicElementSize = std::nullopt) const;

  /// Calculate operand types to copy \p RemainingBytes of residual memory.
  ///
  /// \param[out] OpsOut The operand types to copy RemainingBytes of memory.
  /// \param Context LLVM context used to construct types.
  /// \param RemainingBytes The number of bytes to copy.
  /// \param SrcAddrSpace Source address space.
  /// \param DestAddrSpace Destination address space.
  /// \param SrcAlign Source alignment.
  /// \param DestAlign Destination alignment.
  /// \param AtomicCpySize Optional atomic copy size in bytes.
  ///
  /// Calculates the operand types to use when copying \p RemainingBytes of
  /// memory, where source and destination alignments are \p SrcAlign and
  /// \p DestAlign respectively.
  LLVM_ABI void getMemcpyLoopResidualLoweringType(
      SmallVectorImpl<Type *> &OpsOut, LLVMContext &Context,
      unsigned RemainingBytes, unsigned SrcAddrSpace, unsigned DestAddrSpace,
      Align SrcAlign, Align DestAlign,
      std::optional<uint32_t> AtomicCpySize = std::nullopt) const;

  /// Return true if the two functions have compatible attributes for inlining.
  /// \param Caller Function containing the call.
  /// \param Callee Function being inlined.
  /// @return True if the two functions have compatible attributes for inlining.
  LLVM_ABI bool areInlineCompatible(const Function *Caller,
                                    const Function *Callee) const;

  /// Return a penalty for invoking call \p Call from function \p F.
  ///
  /// For example, if a function F calls a function G, which in turn calls
  /// function H, then getInlineCallPenalty(F, H()) would return the
  /// penalty of calling H from F, e.g. after inlining G into F.
  /// \p DefaultCallPenalty is passed to give a default penalty that
  /// the target can amend or override.
  /// \param F Function from which the call would be made.
  /// \param Call Call whose penalty is computed.
  /// \param DefaultCallPenalty Default penalty the target may amend or override.
  /// @return A penalty for invoking call \p Call from function \p F.
  LLVM_ABI unsigned getInlineCallPenalty(const Function *F,
                                         const CallBase &Call,
                                         unsigned DefaultCallPenalty) const;

  /// Return true if \p Attr should be copied onto a function outlined from \p Caller.
  /// \param Caller Function being outlined from.
  /// \param Attr Attribute to consider copying.
  /// @return True if \p Attr should be copied onto a function outlined from \p Caller.
  LLVM_ABI bool
  shouldCopyAttributeWhenOutliningFrom(const Function *Caller,
                                       const Attribute &Attr) const;

  /// Return true if caller and callee agree on how \p Types are passed.
  ///
  /// True if the caller and callee agree on how \p Types will be passed to or
  /// returned from the callee.
  /// \param Caller Function containing the call.
  /// \param Callee Function being called.
  /// \param Types List of types to check.
  /// @return True if caller and callee agree on how \p Types are passed.
  LLVM_ABI bool areTypesABICompatible(const Function *Caller,
                                      const Function *Callee,
                                      ArrayRef<Type *> Types) const;

  /// The type of load/store indexing.
  enum MemIndexedMode {
    MIM_Unindexed, ///< No indexing.
    MIM_PreInc,    ///< Pre-incrementing.
    MIM_PreDec,    ///< Pre-decrementing.
    MIM_PostInc,   ///< Post-incrementing.
    MIM_PostDec    ///< Post-decrementing.
  };

  /// Return true if the specified indexed load for the given type is legal.
  /// \param Mode Indexed addressing mode.
  /// \param Ty Type of the loaded value.
  /// @return True if the specified indexed load for the given type is legal.
  LLVM_ABI bool isIndexedLoadLegal(enum MemIndexedMode Mode, Type *Ty) const;

  /// Return true if the specified indexed store for the given type is legal.
  /// \param Mode Indexed addressing mode.
  /// \param Ty Type of the stored value.
  /// @return True if the specified indexed store for the given type is legal.
  LLVM_ABI bool isIndexedStoreLegal(enum MemIndexedMode Mode, Type *Ty) const;

  /// Return the largest vector bitwidth for loads and stores in \p AddrSpace.
  /// \param AddrSpace Address space of the memory operation.
  /// @return The largest vector bitwidth for loads and stores in \p AddrSpace.
  LLVM_ABI unsigned getLoadStoreVecRegBitWidth(unsigned AddrSpace) const;

  /// Return true if the load instruction is legal to vectorize.
  /// \param LI Load to test.
  /// @return True if the load instruction is legal to vectorize.
  LLVM_ABI bool isLegalToVectorizeLoad(LoadInst *LI) const;

  /// Return true if the store instruction is legal to vectorize.
  /// \param SI Store to test.
  /// @return True if the store instruction is legal to vectorize.
  LLVM_ABI bool isLegalToVectorizeStore(StoreInst *SI) const;

  /// Return true if it is legal to vectorize the given load chain.
  /// \param ChainSizeInBytes Total size of the load chain in bytes.
  /// \param Alignment Alignment of the loads.
  /// \param AddrSpace Address space of the loads.
  /// @return True if it is legal to vectorize the given load chain.
  LLVM_ABI bool isLegalToVectorizeLoadChain(unsigned ChainSizeInBytes,
                                            Align Alignment,
                                            unsigned AddrSpace) const;

  /// Return true if it is legal to vectorize the given store chain.
  /// \param ChainSizeInBytes Total size of the store chain in bytes.
  /// \param Alignment Alignment of the stores.
  /// \param AddrSpace Address space of the stores.
  /// @return True if it is legal to vectorize the given store chain.
  LLVM_ABI bool isLegalToVectorizeStoreChain(unsigned ChainSizeInBytes,
                                             Align Alignment,
                                             unsigned AddrSpace) const;

  /// Return true if it is legal to vectorize the given reduction kind.
  /// \param RdxDesc Descriptor of the reduction.
  /// \param VF Vectorization factor.
  /// @return True if it is legal to vectorize the given reduction kind.
  LLVM_ABI bool isLegalToVectorizeReduction(const RecurrenceDescriptor &RdxDesc,
                                            ElementCount VF) const;

  /// Return true if the given type is supported for scalable vectors.
  /// \param Ty Element type to test.
  /// @return True if the given type is supported for scalable vectors.
  LLVM_ABI bool isElementTypeLegalForScalableVector(Type *Ty) const;

  /// Return the adjusted VF if the target cannot use \p SizeInBytes loads.
  /// \param VF Current vectorization factor.
  /// \param LoadSize Load size in bytes.
  /// \param ChainSizeInBytes Total size of the load chain in bytes.
  /// \param VecTy Vector type of the load.
  /// @return The adjusted VF if the target cannot use \p SizeInBytes loads.
  LLVM_ABI unsigned getLoadVectorFactor(unsigned VF, unsigned LoadSize,
                                        unsigned ChainSizeInBytes,
                                        VectorType *VecTy) const;

  /// Return the adjusted VF if the target cannot use \p SizeInBytes stores.
  /// \param VF Current vectorization factor.
  /// \param StoreSize Store size in bytes.
  /// \param ChainSizeInBytes Total size of the store chain in bytes.
  /// \param VecTy Vector type of the store.
  /// @return The adjusted VF if the target cannot use \p SizeInBytes stores.
  LLVM_ABI unsigned getStoreVectorFactor(unsigned VF, unsigned StoreSize,
                                         unsigned ChainSizeInBytes,
                                         VectorType *VecTy) const;

  /// Return true if equal-cost fixed-width vectorization is preferred.
  ///
  /// True if the target prefers fixed width vectorization if the loop
  /// vectorizer's cost-model assigns an equal cost to the fixed and scalable
  /// version of the vectorized loop.
  /// @return True if equal-cost fixed-width vectorization is preferred.
  LLVM_ABI bool preferFixedOverScalableIfEqualCost() const;

  /// Return true if SLP should use alternate-opcode vectorization.
  /// @return True if SLP should use alternate-opcode vectorization.
  LLVM_ABI bool preferAlternateOpcodeVectorization() const;

  /// Return true if SLP should apply the 2-element instruction-count check.
  ///
  /// True if the SLP vectorizer should apply the instruction-count check that
  /// rejects 2-element vector trees when the vector instruction count exceeds
  /// the scalar instruction count, false if the target opts out of this
  /// heuristic.
  /// @return True if SLP should apply the 2-element instruction-count check.
  LLVM_ABI bool preferSLPInstCountCheck() const;

  /// Return true if reductions of \p Kind should stay inside the loop.
  /// \param Kind Recurrence kind of the reduction.
  /// \param Ty Type of the reduction.
  /// @return True if reductions of \p Kind should stay inside the loop.
  LLVM_ABI bool preferInLoopReduction(RecurKind Kind, Type *Ty) const;

  /// Return true if a predicated reduction select should stay in the loop.
  ///
  /// i.e.
  /// loop:
  ///   p = phi (0, s)
  ///   a = add (p, x)
  ///   s = select (mask, a, p)
  /// vecreduce.add(s)
  ///
  /// As opposed to the normal scheme of p = phi (0, a) which allows the select
  /// to be pulled out of the loop. If the select(.., add, ..) can be predicated
  /// by the target, this can lead to cleaner code generation.
  /// @return True if a predicated reduction select should stay in the loop.
  LLVM_ABI bool preferPredicatedReductionSelect() const;

  /// Return true if a scalar epilogue should still be considered for vectorization.
  ///
  /// The loop vectorizer should consider vectorizing an otherwise scalar
  /// epilogue loop if the loop already has been vectorized processing \p Iters
  /// scalar iterations per vector iteration.
  /// \param Iters Scalar iterations already processed per vector iteration.
  /// @return True if a scalar epilogue should still be considered for vectorization.
  LLVM_ABI bool preferEpilogueVectorization(ElementCount Iters) const;

  /// Return true if VFs that exceed register pressure should be discarded.
  ///
  /// True if the loop vectorizer should discard any VFs where the maximum
  /// register pressure exceeds getNumberOfRegisters.
  /// @return True if VFs that exceed register pressure should be discarded.
  LLVM_ABI bool shouldConsiderVectorizationRegPressure() const;

  /// Return true if the reduction intrinsic should be expanded to shuffles.
  /// \param II Reduction intrinsic to expand.
  /// @return True if the reduction intrinsic should be expanded to shuffles.
  LLVM_ABI bool shouldExpandReduction(const IntrinsicInst *II) const;

  /// Shuffle pattern used when expanding a reduction intrinsic.
  enum struct ReductionShuffle {
    SplitHalf, ///< Recursively split the vector in half.
    Pairwise   ///< Reduce adjacent pairs, then combine those results.
  };

  /// Return the shuffle pattern used to expand the given reduction intrinsic.
  /// \param II Reduction intrinsic to expand.
  /// @return The shuffle pattern used to expand the given reduction intrinsic.
  LLVM_ABI ReductionShuffle
  getPreferredExpandedReductionShuffle(const IntrinsicInst *II) const;

  /// Return the size cost of rematerializing a GlobalValue versus a reload.
  /// @return The size cost of rematerializing a GlobalValue versus a reload.
  LLVM_ABI unsigned getGISelRematGlobalCost() const;

  /// Return the minimum trip count to consider vectorizing with tail-folding.
  /// @return The minimum trip count to consider vectorizing with tail-folding.
  LLVM_ABI unsigned getMinTripCountTailFoldingThreshold() const;

  /// Return true if the target supports scalable vectors.
  /// @return True if the target supports scalable vectors.
  LLVM_ABI bool supportsScalableVectors() const;

  /// Return true when scalable vectorization is preferred.
  /// @return True when scalable vectorization is preferred.
  LLVM_ABI bool enableScalableVectorization() const;

  /// \name Vector Predication Information
  /// @{
  /// Return true if the target supports the %evl parameter of VP intrinsics.
  ///
  /// Whether the target supports the %evl parameter of VP intrinsic efficiently
  /// in hardware. (see LLVM Language Reference - "Vector Predication
  /// Intrinsics"). Use of %evl is discouraged when that is not the case.
  /// @return True if the target supports the %evl parameter of VP intrinsics.
  LLVM_ABI bool hasActiveVectorLength() const;

  /// Return true if sinking operands of \p I into I's block is profitable.
  ///
  /// For example, because the operands can be folded into a target instruction
  /// during instruction selection. After calling the function \p Ops contains
  /// the Uses to sink ordered by dominance (dominating users come first).
  /// \param I Instruction whose operands may be sunk.
  /// \param Ops Uses to sink, filled in dominance order.
  /// @return True if sinking operands of \p I into I's block is profitable.
  LLVM_ABI bool isProfitableToSinkOperands(Instruction *I,
                                           SmallVectorImpl<Use *> &Ops) const;

  /// Return true if a uniform scalar shift is much cheaper than a per-lane one.
  ///
  /// On x86 before AVX2 for example, there is a "psllw" instruction for the
  /// former case, but no simple instruction for a general "a << b" operation
  /// on vectors. This should also apply to lowering for vector funnel shifts
  /// (rotates).
  /// \param Ty Vector type of the shift.
  /// @return True if a uniform scalar shift is much cheaper than a per-lane one.
  LLVM_ABI bool isVectorShiftByScalarCheap(Type *Ty) const;

  /// Describe how a vector-predicated operation should be legalized.
  struct VPLegalization {
    /// Strategy for transforming a VP operator or its EVL parameter.
    enum VPTransform {
      /// Keep the predicating parameter.
      Legal = 0,
      /// Where legal, discard the predicate parameter.
      Discard = 1,
      /// Transform into something else that is also predicating.
      Convert = 2
    };

    /// How to transform the EVL parameter.
    ///
    /// Legal:   keep the EVL parameter as it is.
    /// Discard: Ignore the EVL parameter where it is safe to do so.
    /// Convert: Fold the EVL into the mask parameter.
    VPTransform EVLParamStrategy;

    /// How to transform the operator.
    ///
    /// Legal:   The target supports this operator.
    /// Convert: Convert this to a non-VP operation.
    /// The 'Discard' strategy is invalid.
    VPTransform OpStrategy;

    /// Return true if both the EVL parameter and the operator can stay as-is.
    /// @return True if both the EVL parameter and the operator can stay as-is.
    bool shouldDoNothing() const {
      return (EVLParamStrategy == Legal) && (OpStrategy == Legal);
    }
    /// Construct a legalization strategy for EVL and the operator.
    /// \param EVLParamStrategy How to transform the EVL parameter.
    /// \param OpStrategy How to transform the VP operator.
    VPLegalization(VPTransform EVLParamStrategy, VPTransform OpStrategy)
        : EVLParamStrategy(EVLParamStrategy), OpStrategy(OpStrategy) {}
  };

  /// Return how the target needs this vector-predicated operation transformed.
  /// \param PI Vector-predicated intrinsic to legalize.
  /// @return How the target needs this vector-predicated operation transformed.
  LLVM_ABI VPLegalization
  getVPLegalizationStrategy(const VPIntrinsic &PI) const;
  /// @}

  /// Return whether a 32-bit branch instruction is available in Arm or Thumb.
  ///
  /// Used by the LowerTypeTests pass, which constructs an IR inline assembler
  /// node containing a jump table in a format suitable for the target, so it
  /// needs to know what format of jump table it can legally use.
  ///
  /// For non-Arm targets, this function isn't used. It defaults to returning
  /// false, but it shouldn't matter what it returns anyway.
  /// \param Thumb True to query Thumb state; false to query Arm state.
  /// @return True if a 32-bit branch instruction is available in Arm or Thumb.
  LLVM_ABI bool hasArmWideBranch(bool Thumb) const;

  /// Returns a bitmask constructed from the target-features or fmv-features
  /// metadata of a function corresponding to its Arch Extensions.
  /// \param F Function whose feature mask is requested.
  /// @return A bitmask constructed from the target-features or fmv-features.
  LLVM_ABI APInt getFeatureMask(const Function &F) const;

  /// Returns a bitmask constructed from the target-features or fmv-features
  /// metadata of a function corresponding to its FMV priority.
  /// \param F Function whose FMV priority mask is requested.
  /// @return A bitmask constructed from the target-features or fmv-features.
  LLVM_ABI APInt getPriorityMask(const Function &F) const;

  /// Returns true if this is an instance of a function with multiple versions.
  /// \param F Function to test for multiversioning.
  /// @return True if this is an instance of a function with multiple versions.
  LLVM_ABI bool isMultiversionedFunction(const Function &F) const;

  /// Return the maximum number of function arguments the target supports.
  /// @return The maximum number of function arguments the target supports.
  LLVM_ABI unsigned getMaxNumArgs() const;

  /// Return how many padding bytes to add for a global array of the given size.
  ///
  /// Default is no padding.
  /// \param Size Array size in bytes.
  /// \param ArrayType Element type of the global array.
  /// @return How many padding bytes to add for a global array of the given size.
  LLVM_ABI unsigned getNumBytesToPadGlobalArray(unsigned Size,
                                                Type *ArrayType) const;

  /// @}

  /// Collect kernel launch bounds for \p F into \p LB.
  ///
  /// \param F Function whose launch bounds are collected.
  /// \param LB Output list of (name, value) launch bound pairs.
  LLVM_ABI void collectKernelLaunchBounds(
      const Function &F,
      SmallVectorImpl<std::pair<StringRef, int64_t>> &LB) const;

  /// Returns true if GEP should not be used to index into vectors for this
  /// target.
  /// @return True if GEP should not be used to index into vectors for this.
  LLVM_ABI bool allowVectorElementIndexingUsingGEP() const;

  /// Determine if an instruction with Custom uniformity can be proven uniform
  /// based on which operands are uniform.
  ///
  /// \param I The instruction to check.
  /// \param UniformArgs A bitvector indicating which operands are known to be
  ///                    uniform (bit N corresponds to operand N).
  /// \returns true if the instruction result can be proven uniform given the
  ///          uniform operands, false otherwise.
  LLVM_ABI bool isUniform(const Instruction *I,
                          const SmallBitVector &UniformArgs) const;

private:
  std::unique_ptr<const TargetTransformInfoImplBase> TTIImpl;
};

/// Analysis pass providing the \c TargetTransformInfo.
///
/// The core idea of the TargetIRAnalysis is to expose an interface through
/// which LLVM targets can analyze and provide information about the middle
/// end's target-independent IR. This supports use cases such as target-aware
/// cost modeling of IR constructs.
///
/// This is a function analysis because much of the cost modeling for targets
/// is done in a subtarget specific way and LLVM supports compiling different
/// functions targeting different subtargets in order to support runtime
/// dispatch according to the observed subtarget.
class TargetIRAnalysis : public AnalysisInfoMixin<TargetIRAnalysis> {
public:
  /// Result type produced by this analysis.
  typedef TargetTransformInfo Result;

  /// Default construct a target IR analysis.
  ///
  /// This will use the module's datalayout to construct a baseline
  /// conservative TTI result.
  LLVM_ABI TargetIRAnalysis();

  /// Construct an IR analysis pass around a target-provide callback.
  ///
  /// The callback will be called with a particular function for which the TTI
  /// is needed and must return a TTI object for that function.
  /// \param TTICallback Callback that builds TTI for a given function.
  LLVM_ABI
  TargetIRAnalysis(std::function<Result(const Function &)> TTICallback);

  /// Copy-construct a target IR analysis.
  /// \param Arg Analysis to copy.
  TargetIRAnalysis(const TargetIRAnalysis &Arg)
      : TTICallback(Arg.TTICallback) {}
  /// Move-construct a target IR analysis.
  /// \param Arg Analysis to move from.
  TargetIRAnalysis(TargetIRAnalysis &&Arg)
      : TTICallback(std::move(Arg.TTICallback)) {}
  /// Copy-assign a target IR analysis.
  /// \param RHS Analysis to copy.
  /// @return Reference to this analysis after copy assignment.
  TargetIRAnalysis &operator=(const TargetIRAnalysis &RHS) {
    TTICallback = RHS.TTICallback;
    return *this;
  }
  /// Move-assign a target IR analysis.
  /// \param RHS Analysis to move from.
  /// @return Reference to this analysis after move assignment.
  TargetIRAnalysis &operator=(TargetIRAnalysis &&RHS) {
    TTICallback = std::move(RHS.TTICallback);
    return *this;
  }

  /// Run the analysis on function \p F and return target transform info.
  /// \param F Function to analyze.
  /// \param AM Function analysis manager (unused).
  /// @return Target transform info for \p F.
  LLVM_ABI Result run(const Function &F, FunctionAnalysisManager &AM);

private:
  friend AnalysisInfoMixin<TargetIRAnalysis>;
  LLVM_ABI static AnalysisKey Key;

  /// The callback used to produce a result.
  ///
  /// We use a completely opaque callback so that targets can provide whatever
  /// mechanism they desire for constructing the TTI for a given function.
  ///
  /// FIXME: Should we really use std::function? It's relatively inefficient.
  /// It might be possible to arrange for even stateful callbacks to outlive
  /// the analysis and thus use a function_ref which would be lighter weight.
  /// This may also be less error prone as the callback is likely to reference
  /// the external TargetMachine, and that reference needs to never dangle.
  std::function<Result(const Function &)> TTICallback;

  /// Helper function used as the callback in the default constructor.
  static Result getDefaultTTI(const Function &F);
};

/// Wrapper pass for TargetTransformInfo.
///
/// This pass can be constructed from a TTI object which it stores internally
/// and is queried by passes.
class LLVM_ABI TargetTransformInfoWrapperPass : public ImmutablePass {
  TargetIRAnalysis TIRA;
  std::optional<TargetTransformInfo> TTI;

  virtual void anchor();

public:
  /// Pass identification used by LLVM's pass manager.
  static char ID;

  /// Default constructor required by the pass infrastructure; do not use it.
  ///
  /// Use the constructor below or call one of the creation routines.
  TargetTransformInfoWrapperPass();

  /// Construct a wrapper pass from a target IR analysis.
  /// \param TIRA Analysis used to construct TargetTransformInfo for a function.
  explicit TargetTransformInfoWrapperPass(TargetIRAnalysis TIRA);

  /// Return the TargetTransformInfo for function \p F.
  /// \param F Function whose target transform info is requested.
  /// @return Target transform info for \p F.
  TargetTransformInfo &getTTI(const Function &F);
};

/// Create an analysis pass wrapper around a TTI object.
///
/// This analysis pass just holds the TTI instance and makes it available to
/// clients.
/// \param TIRA Analysis used to construct TargetTransformInfo for a function.
/// @return A new ImmutablePass that provides TargetTransformInfo from \p TIRA.
LLVM_ABI ImmutablePass *
createTargetTransformInfoWrapperPass(TargetIRAnalysis TIRA);

} // namespace llvm

#endif
