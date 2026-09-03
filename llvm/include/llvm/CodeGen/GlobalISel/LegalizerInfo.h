//===- llvm/CodeGen/GlobalISel/LegalizerInfo.h ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// Interface for Targets to specify which operations they can successfully
/// select and how the others should be expanded most efficiently.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALISEL_LEGALIZERINFO_H
#define LLVM_CODEGEN_GLOBALISEL_LEGALIZERINFO_H

#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGenTypes/LowLevelType.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include <cassert>
#include <cstdint>
#include <tuple>
#include <utility>

namespace llvm {

/// Command-line option that disables GlobalISel legality verification checks.
LLVM_ABI extern cl::opt<bool> DisableGISelLegalityCheck;

class MachineFunction;
class raw_ostream;
class LegalizerHelper;
class LostDebugLocObserver;
class MachineInstr;
class MachineRegisterInfo;
class MCInstrInfo;

/// Legalization actions that describe how an illegal operation should be fixed.
namespace LegalizeActions {
/// Action describing how to legalize an operation for GlobalISel.
enum LegalizeAction : std::uint8_t {
  /// The operation is expected to be selectable directly by the target, and
  /// no transformation is necessary.
  Legal,

  /// The operation should be synthesized from multiple instructions acting on
  /// a narrower scalar base-type. For example a 64-bit add might be
  /// implemented in terms of 32-bit add-with-carry.
  NarrowScalar,

  /// The operation should be implemented in terms of a wider scalar
  /// base-type. For example a <2 x s8> add could be implemented as a <2
  /// x s32> add (ignoring the high bits).
  WidenScalar,

  /// The (vector) operation should be implemented by splitting it into
  /// sub-vectors where the operation is legal. For example a <8 x s64> add
  /// might be implemented as 4 separate <2 x s64> adds. There can be a leftover
  /// if there are not enough elements for last sub-vector e.g. <7 x s64> add
  /// will be implemented as 3 separate <2 x s64> adds and one s64 add. Leftover
  /// types can be avoided by doing MoreElements first.
  FewerElements,

  /// The (vector) operation should be implemented by widening the input
  /// vector and ignoring the lanes added by doing so. For example <2 x i8> is
  /// rarely legal, but you might perform an <8 x i8> and then only look at
  /// the first two results.
  MoreElements,

  /// Perform the operation on a different, but equivalently sized type.
  Bitcast,

  /// The operation itself must be expressed in terms of simpler actions on
  /// this target. E.g. a SREM replaced by an SDIV and subtraction.
  Lower,

  /// The operation should be implemented as a call to some kind of runtime
  /// support library. For example this usually happens on machines that don't
  /// support floating-point operations natively.
  Libcall,

  /// The target wants to do something special with this combination of
  /// operand and type. A callback will be issued when it is needed.
  Custom,

  /// This operation is completely unsupported on the target. A programming
  /// error has occurred.
  Unsupported,

  /// Sentinel value for when no action was found in the specified table.
  NotFound,
};
} // end namespace LegalizeActions
/// Print legalize action \p Action to stream \p OS.
///
/// \returns The output stream \p OS.
/// \param OS Output stream that receives the action name.
/// \param Action Legalize action being printed.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS,
                                 LegalizeActions::LegalizeAction Action);

/// Convenient alias for LegalizeActions::LegalizeAction.
using LegalizeActions::LegalizeAction;

/// Bundle the information needed to decide whether an operation is legal.
///
/// For efficiency, it doesn't make a copy of Types so care must be taken not
/// to free it before using the query.
struct LegalityQuery {
  /// Opcode whose legality is being queried.
  unsigned Opcode;
  /// Operand and result types for the operation.
  ArrayRef<LLT> Types;

  /// Memory-operand constraints attached to a legality query.
  struct MemDesc {
    /// Memory access type.
    LLT MemoryTy;
    /// Required alignment in bits.
    uint64_t AlignInBits;
    /// Atomic success ordering; for cmpxchg this is the success ordering.
    AtomicOrdering Ordering;
    /// Atomic failure ordering; for cmpxchg, otherwise NotAtomic.
    AtomicOrdering FailureOrdering;

    /// Construct an empty memory descriptor.
    MemDesc() = default;
    /// Construct a memory descriptor from explicit type, alignment, and ordering.
    ///
    /// \param MemoryTy Memory access type.
    /// \param AlignInBits Required alignment in bits.
    /// \param Ordering Atomic success ordering.
    /// \param FailureOrdering Atomic failure ordering.
    MemDesc(LLT MemoryTy, uint64_t AlignInBits, AtomicOrdering Ordering,
            AtomicOrdering FailureOrdering)
        : MemoryTy(MemoryTy), AlignInBits(AlignInBits), Ordering(Ordering),
          FailureOrdering(FailureOrdering) {}
    /// Construct a memory descriptor from machine memory operand \p MMO.
    ///
    /// \param MMO Machine memory operand providing type, alignment, and ordering.
    MemDesc(const MachineMemOperand &MMO)
        : MemDesc(MMO.getMemoryType(), MMO.getAlign().value() * 8,
                  MMO.getSuccessOrdering(), MMO.getFailureOrdering()) {}
  };

  /// Operations which require memory can use this to place requirements on the
  /// memory type for each MMO.
  ArrayRef<MemDesc> MMODescrs;

  /// Immediate operands associated with the operation, if any.
  ArrayRef<int64_t> Immediates;

  /// Construct a legality query for \p Opcode with the given type and memory info.
  ///
  /// \param Opcode Opcode whose legality is being queried.
  /// \param Types Operand and result types for the operation.
  /// \param MMODescrs Optional memory operand descriptors.
  /// \param Immediates Optional immediate operand values.
  constexpr LegalityQuery(unsigned Opcode, ArrayRef<LLT> Types,
                          ArrayRef<MemDesc> MMODescrs = {},
                          ArrayRef<int64_t> Immediates = {})
      : Opcode(Opcode), Types(Types), MMODescrs(MMODescrs),
        Immediates(Immediates) {}

  /// Print this query to \p OS and return the stream.
  ///
  /// \returns The output stream \p OS.
  /// \param OS Output stream that receives the query description.
  LLVM_ABI raw_ostream &print(raw_ostream &OS) const;
};

/// The result of a query. It either indicates a final answer of Legal or
/// Unsupported or describes an action that must be taken to make an operation
/// more legal.
struct LegalizeActionStep {
  /// The action to take or the final answer.
  LegalizeAction Action;
  /// If describing an action, the type index to change. Otherwise zero.
  unsigned TypeIdx;
  /// If describing an action, the new type for TypeIdx. Otherwise LLT{}.
  LLT NewType;

  /// Construct a step that performs \p Action on \p TypeIdx toward \p NewType.
  ///
  /// \param Action Legalize action described by this step.
  /// \param TypeIdx Type index affected by the action, or zero if none.
  /// \param NewType Replacement type for \p TypeIdx, or an empty LLT if none.
  LegalizeActionStep(LegalizeAction Action, unsigned TypeIdx,
                     const LLT NewType)
      : Action(Action), TypeIdx(TypeIdx), NewType(NewType) {}

  /// Return whether this step equals \p RHS.
  ///
  /// \returns true if both steps describe the same action.
  /// \param RHS Other legalize action step compared for equality.
  bool operator==(const LegalizeActionStep &RHS) const {
    return std::tie(Action, TypeIdx, NewType) ==
        std::tie(RHS.Action, RHS.TypeIdx, RHS.NewType);
  }
};

/// Predicate that decides whether a legality query matches a rule.
using LegalityPredicate = std::function<bool (const LegalityQuery &)>;
/// Mutation that selects a type index and replacement LLT for an action.
using LegalizeMutation =
    std::function<std::pair<unsigned, LLT>(const LegalityQuery &)>;

/// Helpers that build LegalityPredicate callables for legalizer rules.
namespace LegalityPredicates {
/// Pair of value types plus a memory type and alignment constraint.
struct TypePairAndMemDesc {
  /// First value type in the pair.
  LLT Type0;
  /// Second value type in the pair.
  LLT Type1;
  /// Memory access type associated with the pair.
  LLT MemTy;
  /// Minimum required alignment in bits.
  uint64_t Align;

  /// Return whether this descriptor equals \p Other.
  ///
  /// \returns true if both descriptors are equal.
  /// \param Other Descriptor compared for equality.
  bool operator==(const TypePairAndMemDesc &Other) const {
    return Type0 == Other.Type0 && Type1 == Other.Type1 &&
           Align == Other.Align && MemTy == Other.MemTy;
  }

  /// Return whether this descriptor is compatible with access \p Other.
  ///
  /// \returns true if this memory access is legal for the access described by
  /// \p Other (The alignment is sufficient for the size and result type).
  /// \param Other Access descriptor being compared for compatibility.
  bool isCompatible(const TypePairAndMemDesc &Other) const {
    return Type0 == Other.Type0 && Type1 == Other.Type1 &&
           Align >= Other.Align &&
           // FIXME: This perhaps should be stricter, but the current legality
           // rules are written only considering the size.
           MemTy.getSizeInBits() == Other.MemTy.getSizeInBits();
  }
};

/// True iff P is false.
///
/// \returns A predicate that is true iff \p P is false.
/// \param P Predicate to negate.
template <typename Predicate> Predicate predNot(Predicate P) {
  return [=](const LegalityQuery &Query) { return !P(Query); };
}

/// True iff P0 and P1 are true.
///
/// \returns A predicate that is true iff both predicates are true.
/// \param P0 First predicate.
/// \param P1 Second predicate.
template<typename Predicate>
Predicate all(Predicate P0, Predicate P1) {
  return [=](const LegalityQuery &Query) {
    return P0(Query) && P1(Query);
  };
}
/// True iff all given predicates are true.
///
/// \returns A predicate that is true iff all given predicates are true.
/// \param P0 First predicate.
/// \param P1 Second predicate.
/// \param args Additional predicates combined with a logical and.
template<typename Predicate, typename... Args>
Predicate all(Predicate P0, Predicate P1, Args... args) {
  return all(all(P0, P1), args...);
}

/// True iff P0 or P1 are true.
///
/// \returns A predicate that is true iff either predicate is true.
/// \param P0 First predicate.
/// \param P1 Second predicate.
template<typename Predicate>
Predicate any(Predicate P0, Predicate P1) {
  return [=](const LegalityQuery &Query) {
    return P0(Query) || P1(Query);
  };
}
/// True iff any given predicates are true.
///
/// \returns A predicate that is true iff any given predicate is true.
/// \param P0 First predicate.
/// \param P1 Second predicate.
/// \param args Additional predicates combined with a logical or.
template<typename Predicate, typename... Args>
Predicate any(Predicate P0, Predicate P1, Args... args) {
  return any(any(P0, P1), args...);
}

/// True iff the given type index is the specified type.
///
/// \returns A legality predicate for the described type check.
/// \param TypeIdx Type index inspected by the predicate.
/// \param TypesInit Required type for \p TypeIdx.
LLVM_ABI LegalityPredicate typeIs(unsigned TypeIdx, LLT TypesInit);
/// True iff the given type index is one of the specified types.
///
/// \returns A legality predicate for the described type-set check.
/// \param TypeIdx Type index inspected by the predicate.
/// \param TypesInit Accepted types for \p TypeIdx.
LLVM_ABI LegalityPredicate typeInSet(unsigned TypeIdx,
                                     std::initializer_list<LLT> TypesInit);

/// True iff the given type index is not the specified type.
///
/// \returns A legality predicate for the described type inequality check.
/// \param TypeIdx Type index inspected by the predicate.
/// \param Type Type that must not match.
inline LegalityPredicate typeIsNot(unsigned TypeIdx, LLT Type) {
  return [=](const LegalityQuery &Query) {
           return Query.Types[TypeIdx] != Type;
         };
}

/// True iff the given types for the given pair of type indexes is one of the
/// specified type pairs.
///
/// \returns A legality predicate for the described type-pair check.
/// \param TypeIdx0 First type index compared.
/// \param TypeIdx1 Second type index compared.
/// \param TypesInit Accepted type pairs.
LLVM_ABI LegalityPredicate
typePairInSet(unsigned TypeIdx0, unsigned TypeIdx1,
              std::initializer_list<std::pair<LLT, LLT>> TypesInit);
/// True iff the given types for the given tuple of type indexes is one of the
/// specified type tuple.
///
/// \returns A legality predicate for the described type-tuple check.
/// \param TypeIdx0 First type index compared.
/// \param TypeIdx1 Second type index compared.
/// \param Type2 Third type index compared.
/// \param TypesInit Accepted type triples.
LLVM_ABI LegalityPredicate
typeTupleInSet(unsigned TypeIdx0, unsigned TypeIdx1, unsigned Type2,
               std::initializer_list<std::tuple<LLT, LLT, LLT>> TypesInit);
/// True iff the given types for the given pair of type indexes is one of the
/// specified type pairs.
///
/// \returns A legality predicate for the described type-pair and memory-descriptor check.
/// \param TypeIdx0 First type index compared.
/// \param TypeIdx1 Second type index compared.
/// \param MMOIdx Memory operand index compared.
/// \param TypesAndMemDescInit Accepted type-pair and memory-descriptor values.
LLVM_ABI LegalityPredicate typePairAndMemDescInSet(
    unsigned TypeIdx0, unsigned TypeIdx1, unsigned MMOIdx,
    std::initializer_list<TypePairAndMemDesc> TypesAndMemDescInit);
/// True iff the specified type index is a scalar.
///
/// \returns A legality predicate that is true iff the type index is a scalar.
/// \param TypeIdx Type index inspected by the predicate.
LLVM_ABI LegalityPredicate isScalar(unsigned TypeIdx);
/// True iff the specified type index is a vector.
///
/// \returns A legality predicate that is true iff the type index is a vector.
/// \param TypeIdx Type index inspected by the predicate.
LLVM_ABI LegalityPredicate isVector(unsigned TypeIdx);
/// True iff the specified type index is a pointer (with any address space).
///
/// \returns A legality predicate that is true iff the type index is a pointer.
/// \param TypeIdx Type index inspected by the predicate.
LLVM_ABI LegalityPredicate isPointer(unsigned TypeIdx);
/// True iff the specified type index is a pointer with the specified address
/// space.
///
/// \returns A legality predicate that is true iff the type index is a pointer in \p AddrSpace.
/// \param TypeIdx Type index inspected by the predicate.
/// \param AddrSpace Required pointer address space.
LLVM_ABI LegalityPredicate isPointer(unsigned TypeIdx, unsigned AddrSpace);
/// True iff the specified type index is a vector of pointers (with any address
/// space).
///
/// \returns A legality predicate that is true iff the type index is a pointer vector.
/// \param TypeIdx Type index inspected by the predicate.
LLVM_ABI LegalityPredicate isPointerVector(unsigned TypeIdx);

/// True if the type index is a vector with element type \p EltTy.
///
/// \returns A legality predicate that is true iff the vector element type is \p EltTy.
/// \param TypeIdx Type index inspected by the predicate.
/// \param EltTy Required vector element type.
LLVM_ABI LegalityPredicate elementTypeIs(unsigned TypeIdx, LLT EltTy);

/// True iff the specified type index is a scalar that's narrower than the given
/// size.
///
/// \returns A legality predicate that is true iff the scalar is narrower than \p Size bits.
/// \param TypeIdx Type index inspected by the predicate.
/// \param Size Exclusive upper bound on scalar width in bits.
LLVM_ABI LegalityPredicate scalarNarrowerThan(unsigned TypeIdx, unsigned Size);

/// True iff the specified type index is a scalar that's wider than the given
/// size.
///
/// \returns A legality predicate that is true iff the scalar is wider than \p Size bits.
/// \param TypeIdx Type index inspected by the predicate.
/// \param Size Exclusive lower bound on scalar width in bits.
LLVM_ABI LegalityPredicate scalarWiderThan(unsigned TypeIdx, unsigned Size);

/// True iff the specified type index is a scalar or vector with an element type
/// that's narrower than the given size.
///
/// \returns A legality predicate that is true iff the scalar or element is narrower than \p Size bits.
/// \param TypeIdx Type index inspected by the predicate.
/// \param Size Exclusive upper bound on scalar or element width in bits.
LLVM_ABI LegalityPredicate scalarOrEltNarrowerThan(unsigned TypeIdx,
                                                   unsigned Size);

/// True iff the specified type index is a vector with a number of elements
/// that's greater than the given size.
///
/// \returns A legality predicate that is true iff the vector element count exceeds \p Size.
/// \param TypeIdx Type index inspected by the predicate.
/// \param Size Exclusive lower bound on the vector element count.
LLVM_ABI LegalityPredicate vectorElementCountIsGreaterThan(unsigned TypeIdx,
                                                           unsigned Size);

/// True iff the specified type index is a vector with a number of elements
/// that's less than or equal to the given size.
///
/// \returns A legality predicate that is true iff the vector element count is at most \p Size.
/// \param TypeIdx Type index inspected by the predicate.
/// \param Size Inclusive upper bound on the vector element count.
LLVM_ABI LegalityPredicate
vectorElementCountIsLessThanOrEqualTo(unsigned TypeIdx, unsigned Size);

/// True iff the specified type index is a scalar or a vector with an element
/// type that's wider than the given size.
///
/// \returns A legality predicate that is true iff the scalar or element is wider than \p Size bits.
/// \param TypeIdx Type index inspected by the predicate.
/// \param Size Exclusive lower bound on scalar or element width in bits.
LLVM_ABI LegalityPredicate scalarOrEltWiderThan(unsigned TypeIdx,
                                                unsigned Size);

/// True iff the specified type index is a scalar whose size is not a multiple
/// of Size.
///
/// \returns A legality predicate that is true iff the scalar size is not a multiple of \p Size.
/// \param TypeIdx Type index inspected by the predicate.
/// \param Size Required size multiple in bits.
LLVM_ABI LegalityPredicate sizeNotMultipleOf(unsigned TypeIdx, unsigned Size);

/// True iff the specified type index is a scalar whose size is not a power of
/// 2.
///
/// \returns A legality predicate that is true iff the scalar size is not a power of two.
/// \param TypeIdx Type index inspected by the predicate.
LLVM_ABI LegalityPredicate sizeNotPow2(unsigned TypeIdx);

/// True iff the specified type index is a scalar or vector whose element size
/// is not a power of 2.
///
/// \returns A legality predicate that is true iff the scalar or element size is not a power of two.
/// \param TypeIdx Type index inspected by the predicate.
LLVM_ABI LegalityPredicate scalarOrEltSizeNotPow2(unsigned TypeIdx);

/// True if the total bitwidth of the specified type index is \p Size bits.
///
/// \returns A legality predicate that is true iff the total bit width equals \p Size.
/// \param TypeIdx Type index inspected by the predicate.
/// \param Size Required total bit width.
LLVM_ABI LegalityPredicate sizeIs(unsigned TypeIdx, unsigned Size);

/// True iff the specified type indices are both the same bit size.
///
/// \returns A legality predicate that is true iff both type indices have the same bit size.
/// \param TypeIdx0 First type index compared by size.
/// \param TypeIdx1 Second type index compared by size.
LLVM_ABI LegalityPredicate sameSize(unsigned TypeIdx0, unsigned TypeIdx1);

/// True iff the first type index has a larger total bit size than second type
/// index.
///
/// \returns A legality predicate that is true iff the first type index is larger than the second.
/// \param TypeIdx0 Type index whose size is compared.
/// \param TypeIdx1 Type index providing the smaller size bound.
LLVM_ABI LegalityPredicate largerThan(unsigned TypeIdx0, unsigned TypeIdx1);

/// True iff the first type index has a smaller total bit size than second type
/// index.
///
/// \returns A legality predicate that is true iff the first type index is smaller than the second.
/// \param TypeIdx0 Type index whose size is compared.
/// \param TypeIdx1 Type index providing the larger size bound.
LLVM_ABI LegalityPredicate smallerThan(unsigned TypeIdx0, unsigned TypeIdx1);

/// True iff the specified MMO index has a size (rounded to bytes) that is not a
/// power of 2.
///
/// \returns A legality predicate that is true iff the memory size in bytes is not a power of two.
/// \param MMOIdx Memory operand index inspected by the predicate.
LLVM_ABI LegalityPredicate memSizeInBytesNotPow2(unsigned MMOIdx);

/// True iff the specified MMO index has a size that is not an even byte size,
/// or that even byte size is not a power of 2.
///
/// \returns A legality predicate that is true iff the memory size is not a power-of-two byte size.
/// \param MMOIdx Memory operand index inspected by the predicate.
LLVM_ABI LegalityPredicate memSizeNotByteSizePow2(unsigned MMOIdx);

/// True iff the specified type index is a vector whose element count is not a
/// power of 2.
///
/// \returns A legality predicate that is true iff the vector element count is not a power of two.
/// \param TypeIdx Type index inspected by the predicate.
LLVM_ABI LegalityPredicate numElementsNotPow2(unsigned TypeIdx);
/// True iff the specified MMO index has at an atomic ordering of at Ordering or
/// stronger.
///
/// \returns A legality predicate that is true iff the MMO atomic ordering is at least \p Ordering.
/// \param MMOIdx Memory operand index inspected by the predicate.
/// \param Ordering Minimum atomic ordering required.
LLVM_ABI LegalityPredicate
atomicOrderingAtLeastOrStrongerThan(unsigned MMOIdx, AtomicOrdering Ordering);

/// True iff the immediate at the given index has the specified value.
///
/// \returns A legality predicate that is true iff the immediate equals \p Imm.
/// \param ImmIdx Immediate index inspected by the predicate.
/// \param Imm Immediate value that must match.
LLVM_ABI LegalityPredicate immIs(unsigned ImmIdx, int64_t Imm);
/// True iff the immediate at the given index has one of the specified values.
///
/// \returns A legality predicate that is true iff the immediate is one of the given values.
/// \param ImmIdx Immediate index inspected by the predicate.
/// \param ImmsInit Immediate values accepted by the predicate.
LLVM_ABI LegalityPredicate immInSet(unsigned ImmIdx,
                                    std::initializer_list<int64_t> ImmsInit);
/// True iff the immediate at the given index does not have the specified value.
///
/// \returns A legality predicate that is true iff the immediate does not equal \p Imm.
/// \param ImmIdx Immediate index inspected by the predicate.
/// \param Imm Immediate value that must not match.
LLVM_ABI LegalityPredicate immIsNot(unsigned ImmIdx, int64_t Imm);
} // end namespace LegalityPredicates

/// Helpers that build LegalizeMutation callables for legalizer rules.
namespace LegalizeMutations {
/// Select this specific type for the given type index.
///
/// \returns A mutation that selects \p Ty for type index \p TypeIdx.
/// \param TypeIdx Type index being rewritten.
/// \param Ty Replacement type to select.
LLVM_ABI LegalizeMutation changeTo(unsigned TypeIdx, LLT Ty);

/// Keep the same type as the given type index.
///
/// \returns A mutation that copies the type from \p FromTypeIdx.
/// \param TypeIdx Type index being rewritten.
/// \param FromTypeIdx Type index whose type is copied.
LLVM_ABI LegalizeMutation changeTo(unsigned TypeIdx, unsigned FromTypeIdx);

/// Keep the same scalar or element type as the given type index.
///
/// \returns A mutation that copies the scalar or element type from \p FromTypeIdx.
/// \param TypeIdx Type index being rewritten.
/// \param FromTypeIdx Type index providing the scalar or element type.
LLVM_ABI LegalizeMutation changeElementTo(unsigned TypeIdx,
                                          unsigned FromTypeIdx);

/// Keep the same scalar or element type as the given type.
///
/// \returns A mutation that applies scalar or element type \p Ty.
/// \param TypeIdx Type index being rewritten.
/// \param Ty Scalar or element type to apply.
LLVM_ABI LegalizeMutation changeElementTo(unsigned TypeIdx, LLT Ty);

/// Keep the same scalar or element type as \p TypeIdx, but take the number of
/// elements from \p FromTypeIdx.
///
/// \returns A mutation that copies the element count from \p FromTypeIdx.
/// \param TypeIdx Type index whose element type is preserved.
/// \param FromTypeIdx Type index providing the element count.
LLVM_ABI LegalizeMutation changeElementCountTo(unsigned TypeIdx,
                                               unsigned FromTypeIdx);

/// Keep the same scalar or element type as \p TypeIdx, but take the number of
/// elements from \p EC.
///
/// \returns A mutation that applies element count \p EC.
/// \param TypeIdx Type index whose element type is preserved.
/// \param EC Element count to apply.
LLVM_ABI LegalizeMutation changeElementCountTo(unsigned TypeIdx,
                                               ElementCount EC);

/// Change the scalar or element size to match type index \p FromTypeIdx.
///
/// Unlike changeElementTo, this discards pointer types and only changes the
/// size.
///
/// \returns A mutation that matches the scalar size of \p FromTypeIdx.
/// \param TypeIdx Type index being resized.
/// \param FromTypeIdx Type index whose scalar size is matched.
LLVM_ABI LegalizeMutation changeElementSizeTo(unsigned TypeIdx,
                                              unsigned FromTypeIdx);

/// Change the scalar or element size to match the scalar size of \p NewTy.
///
/// Unlike changeElementTo, this discards pointer types and only changes the
/// size.
///
/// \returns A mutation that matches the scalar size of \p NewTy.
/// \param TypeIdx Type index being resized.
/// \param NewTy Type whose scalar size is matched.
LLVM_ABI LegalizeMutation changeElementSizeTo(unsigned TypeIdx, LLT NewTy);

/// Widen the scalar type or vector element type for the given type index to the
/// next power of 2.
///
/// \returns A mutation that widens the scalar or element to the next power of two.
/// \param TypeIdx Type index being widened.
/// \param Min Minimum bit width after widening.
LLVM_ABI LegalizeMutation widenScalarOrEltToNextPow2(unsigned TypeIdx,
                                                     unsigned Min = 0);

/// Widen the scalar type or vector element type for the given type index to
/// next multiple of \p Size.
///
/// \returns A mutation that widens the scalar or element to the next multiple of \p Size.
/// \param TypeIdx Type index being widened.
/// \param Size Alignment multiple for the scalar or element bit width.
LLVM_ABI LegalizeMutation widenScalarOrEltToNextMultipleOf(unsigned TypeIdx,
                                                           unsigned Size);

/// Add more elements to the type for the given type index to the next power of
/// 2.
///
/// \returns A mutation that increases the element count to the next power of two.
/// \param TypeIdx Type index of the vector gaining elements.
/// \param Min Minimum element count after the mutation.
LLVM_ABI LegalizeMutation moreElementsToNextPow2(unsigned TypeIdx,
                                                 unsigned Min = 0);
/// Break up the vector type for the given type index into the element type.
///
/// \returns A mutation that scalarizes the vector at type index \p TypeIdx.
/// \param TypeIdx Type index of the vector being scalarized.
LLVM_ABI LegalizeMutation scalarize(unsigned TypeIdx);
} // end namespace LegalizeMutations

/// A single rule in a legalizer info ruleset.
///
/// The specified action is chosen when the predicate is true. Where appropriate
/// for the action (e.g. for WidenScalar) the new type is selected using the
/// given mutator.
class LegalizeRule {
  LegalityPredicate Predicate;
  LegalizeAction Action;
  LegalizeMutation Mutation;

public:
  /// Construct a rule that selects \p Action when \p Predicate holds.
  ///
  /// \param Predicate Condition that must hold for this rule to match.
  /// \param Action Legalize action to take when the predicate matches.
  /// \param Mutation Optional mutation selecting a replacement type.
  LegalizeRule(LegalityPredicate Predicate, LegalizeAction Action,
               LegalizeMutation Mutation = nullptr)
      : Predicate(Predicate), Action(Action), Mutation(Mutation) {}

  /// Test whether the LegalityQuery matches.
  ///
  /// \returns true if this rule's predicate matches \p Query.
  /// \param Query Legality query evaluated against this rule's predicate.
  bool match(const LegalityQuery &Query) const {
    return Predicate(Query);
  }

  /// Return the legalize action selected by this rule.
  /// \returns The legalize action selected by this rule.
  LegalizeAction getAction() const { return Action; }

  /// Determine the change to make.
  ///
  /// \returns The type index and replacement type produced by this rule's mutation.
  /// \param Query Legality query used to compute the mutation.
  std::pair<unsigned, LLT> determineMutation(const LegalityQuery &Query) const {
    if (Mutation)
      return Mutation(Query);
    return std::make_pair(0, LLT{});
  }
};

/// Builder for the ordered legalization rules of one opcode (or alias group).
class LegalizeRuleSet {
  /// When non-zero, the opcode we are an alias of
  unsigned AliasOf = 0;
  /// If true, there is another opcode that aliases this one
  bool IsAliasedByAnother = false;
  SmallVector<LegalizeRule, 2> Rules;

#ifndef NDEBUG
  /// If bit I is set, this rule set contains a rule that may handle (predicate
  /// or perform an action upon (or both)) the type index I. The uncertainty
  /// comes from free-form rules executing user-provided lambda functions. We
  /// conservatively assume such rules do the right thing and cover all type
  /// indices. The bitset is intentionally 1 bit wider than it absolutely needs
  /// to be to distinguish such cases from the cases where all type indices are
  /// individually handled.
  SmallBitVector TypeIdxsCovered{MCOI::OPERAND_LAST_GENERIC -
                                 MCOI::OPERAND_FIRST_GENERIC + 2};
  SmallBitVector ImmIdxsCovered{MCOI::OPERAND_LAST_GENERIC_IMM -
                                MCOI::OPERAND_FIRST_GENERIC_IMM + 2};
#endif

  unsigned typeIdx(unsigned TypeIdx) {
    assert(TypeIdx <=
               (MCOI::OPERAND_LAST_GENERIC - MCOI::OPERAND_FIRST_GENERIC) &&
           "Type Index is out of bounds");
#ifndef NDEBUG
    TypeIdxsCovered.set(TypeIdx);
#endif
    return TypeIdx;
  }

  void markAllIdxsAsCovered() {
#ifndef NDEBUG
    TypeIdxsCovered.set();
    ImmIdxsCovered.set();
#endif
  }

  void add(const LegalizeRule &Rule) {
    assert(AliasOf == 0 &&
           "RuleSet is aliased, change the representative opcode instead");
    Rules.push_back(Rule);
  }

  static bool always(const LegalityQuery &) { return true; }

  /// Use the given action when the predicate is true.
  /// Action should not be an action that requires mutation.
  LegalizeRuleSet &actionIf(LegalizeAction Action,
                            LegalityPredicate Predicate) {
    add({Predicate, Action});
    return *this;
  }
  /// Use the given action when the predicate is true.
  /// Action should be an action that requires mutation.
  LegalizeRuleSet &actionIf(LegalizeAction Action, LegalityPredicate Predicate,
                            LegalizeMutation Mutation) {
    add({Predicate, Action, Mutation});
    return *this;
  }
  /// Use the given action when type index 0 is any type in the given list.
  /// Action should not be an action that requires mutation.
  LegalizeRuleSet &actionFor(LegalizeAction Action,
                             std::initializer_list<LLT> Types) {
    using namespace LegalityPredicates;
    return actionIf(Action, typeInSet(typeIdx(0), Types));
  }
  /// Use the given action when type index 0 is any type in the given list.
  /// Action should be an action that requires mutation.
  LegalizeRuleSet &actionFor(LegalizeAction Action,
                             std::initializer_list<LLT> Types,
                             LegalizeMutation Mutation) {
    using namespace LegalityPredicates;
    return actionIf(Action, typeInSet(typeIdx(0), Types), Mutation);
  }
  /// Use the given action when type indexes 0 and 1 is any type pair in the
  /// given list.
  /// Action should not be an action that requires mutation.
  LegalizeRuleSet &actionFor(LegalizeAction Action,
                             std::initializer_list<std::pair<LLT, LLT>> Types) {
    using namespace LegalityPredicates;
    return actionIf(Action, typePairInSet(typeIdx(0), typeIdx(1), Types));
  }

  LegalizeRuleSet &
  actionFor(LegalizeAction Action,
            std::initializer_list<std::tuple<LLT, LLT, LLT>> Types) {
    using namespace LegalityPredicates;
    return actionIf(Action,
                    typeTupleInSet(typeIdx(0), typeIdx(1), typeIdx(2), Types));
  }

  /// Use the given action when type indexes 0 and 1 is any type pair in the
  /// given list.
  /// Action should be an action that requires mutation.
  LegalizeRuleSet &actionFor(LegalizeAction Action,
                             std::initializer_list<std::pair<LLT, LLT>> Types,
                             LegalizeMutation Mutation) {
    using namespace LegalityPredicates;
    return actionIf(Action, typePairInSet(typeIdx(0), typeIdx(1), Types),
                    Mutation);
  }
  /// Use the given action when type index 0 is any type in the given list and
  /// imm index 0 is anything. Action should not be an action that requires
  /// mutation.
  LegalizeRuleSet &actionForTypeWithAnyImm(LegalizeAction Action,
                                           std::initializer_list<LLT> Types) {
    using namespace LegalityPredicates;
    immIdx(0); // Inform verifier imm idx 0 is handled.
    return actionIf(Action, typeInSet(typeIdx(0), Types));
  }

  LegalizeRuleSet &actionForTypeWithAnyImm(
    LegalizeAction Action, std::initializer_list<std::pair<LLT, LLT>> Types) {
    using namespace LegalityPredicates;
    immIdx(0); // Inform verifier imm idx 0 is handled.
    return actionIf(Action, typePairInSet(typeIdx(0), typeIdx(1), Types));
  }

  /// Use the given action when type indexes 0 and 1 are both in the given list.
  /// That is, the type pair is in the cartesian product of the list.
  /// Action should not be an action that requires mutation.
  LegalizeRuleSet &actionForCartesianProduct(LegalizeAction Action,
                                             std::initializer_list<LLT> Types) {
    using namespace LegalityPredicates;
    return actionIf(Action, all(typeInSet(typeIdx(0), Types),
                                typeInSet(typeIdx(1), Types)));
  }
  /// Use the given action when type indexes 0 and 1 are both in their
  /// respective lists.
  /// That is, the type pair is in the cartesian product of the lists
  /// Action should not be an action that requires mutation.
  LegalizeRuleSet &
  actionForCartesianProduct(LegalizeAction Action,
                            std::initializer_list<LLT> Types0,
                            std::initializer_list<LLT> Types1) {
    using namespace LegalityPredicates;
    return actionIf(Action, all(typeInSet(typeIdx(0), Types0),
                                typeInSet(typeIdx(1), Types1)));
  }
  /// Use the given action when type indexes 0, 1, and 2 are all in their
  /// respective lists.
  /// That is, the type triple is in the cartesian product of the lists
  /// Action should not be an action that requires mutation.
  LegalizeRuleSet &actionForCartesianProduct(
      LegalizeAction Action, std::initializer_list<LLT> Types0,
      std::initializer_list<LLT> Types1, std::initializer_list<LLT> Types2) {
    using namespace LegalityPredicates;
    return actionIf(Action, all(typeInSet(typeIdx(0), Types0),
                                all(typeInSet(typeIdx(1), Types1),
                                    typeInSet(typeIdx(2), Types2))));
  }

public:
  /// Construct an empty legalize rule set.
  LegalizeRuleSet() = default;

  /// Return whether another opcode aliases this rule set.
  /// \returns true if another opcode aliases this rule set.
  bool isAliasedByAnother() { return IsAliasedByAnother; }
  /// Record that another opcode aliases this rule set.
  void setIsAliasedByAnother() { IsAliasedByAnother = true; }
  /// Make this rule set an alias of \p Opcode.
  ///
  /// \param Opcode Representative opcode whose definitions are reused.
  void aliasTo(unsigned Opcode) {
    assert((AliasOf == 0 || AliasOf == Opcode) &&
           "Opcode is already aliased to another opcode");
    assert(Rules.empty() && "Aliasing will discard rules");
    AliasOf = Opcode;
  }
  /// Return the opcode this rule set aliases, or zero if none.
  /// \returns The aliased opcode, or zero if this rule set is not an alias.
  unsigned getAlias() const { return AliasOf; }

  /// Mark immediate index \p ImmIdx as covered and return it.
  ///
  /// \returns The immediate index \p ImmIdx.
  /// \param ImmIdx Immediate index handled by subsequent rules.
  unsigned immIdx(unsigned ImmIdx) {
    assert(ImmIdx <= (MCOI::OPERAND_LAST_GENERIC_IMM -
                      MCOI::OPERAND_FIRST_GENERIC_IMM) &&
           "Imm Index is out of bounds");
#ifndef NDEBUG
    ImmIdxsCovered.set(ImmIdx);
#endif
    return ImmIdx;
  }

  /// The instruction is legal if predicate is true.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Predicate Condition that selects the Legal action.
  LegalizeRuleSet &legalIf(LegalityPredicate Predicate) {
    // We have no choice but conservatively assume that the free-form
    // user-provided Predicate properly handles all type indices:
    markAllIdxsAsCovered();
    return actionIf(LegalizeAction::Legal, Predicate);
  }
  /// The instruction is legal when type index 0 is any type in the given list.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Types Legal types for type index 0.
  LegalizeRuleSet &legalFor(std::initializer_list<LLT> Types) {
    return actionFor(LegalizeAction::Legal, Types);
  }
  /// The instruction is legal for types in \p Types when \p Pred is true.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Pred Guard that enables this legality rule when true.
  /// \param Types Legal types for type index 0.
  LegalizeRuleSet &legalFor(bool Pred, std::initializer_list<LLT> Types) {
    if (!Pred)
      return *this;
    return actionFor(LegalizeAction::Legal, Types);
  }
  /// The instruction is legal when type indexes 0 and 1 is any type pair in the
  /// given list.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Types Legal type pairs for type indexes 0 and 1.
  LegalizeRuleSet &legalFor(std::initializer_list<std::pair<LLT, LLT>> Types) {
    return actionFor(LegalizeAction::Legal, Types);
  }
  /// The instruction is legal for type pairs in \p Types when \p Pred is true.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Pred Guard that enables this legality rule when true.
  /// \param Types Legal type pairs for type indexes 0 and 1.
  LegalizeRuleSet &legalFor(bool Pred,
                            std::initializer_list<std::pair<LLT, LLT>> Types) {
    if (!Pred)
      return *this;
    return actionFor(LegalizeAction::Legal, Types);
  }
  /// The instruction is legal for type triples in \p Types when \p Pred is true.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Pred Guard that enables this legality rule when true.
  /// \param Types Legal type triples for type indexes 0, 1, and 2.
  LegalizeRuleSet &
  legalFor(bool Pred, std::initializer_list<std::tuple<LLT, LLT, LLT>> Types) {
    if (!Pred)
      return *this;
    return actionFor(LegalizeAction::Legal, Types);
  }
  /// The instruction is legal when type index 0 is any type in the given list
  /// and imm index 0 is anything.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Types Legal types for type index 0.
  LegalizeRuleSet &legalForTypeWithAnyImm(std::initializer_list<LLT> Types) {
    markAllIdxsAsCovered();
    return actionForTypeWithAnyImm(LegalizeAction::Legal, Types);
  }

  /// The instruction is legal for type pairs in \p Types with any immediate.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Types Legal type pairs for type indexes 0 and 1.
  LegalizeRuleSet &legalForTypeWithAnyImm(
    std::initializer_list<std::pair<LLT, LLT>> Types) {
    markAllIdxsAsCovered();
    return actionForTypeWithAnyImm(LegalizeAction::Legal, Types);
  }

  /// The instruction is legal when type indexes 0 and 1 along with the memory
  /// size and minimum alignment is any type and size tuple in the given list.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param TypesAndMemDesc Legal type-pair and memory-descriptor combinations.
  LegalizeRuleSet &legalForTypesWithMemDesc(
      std::initializer_list<LegalityPredicates::TypePairAndMemDesc>
          TypesAndMemDesc) {
    return actionIf(LegalizeAction::Legal,
                    LegalityPredicates::typePairAndMemDescInSet(
                        typeIdx(0), typeIdx(1), /*MMOIdx*/ 0, TypesAndMemDesc));
  }
  /// Conditionally legalize type/memory descriptors in \p TypesAndMemDesc when \p Pred is true.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Pred Guard that enables this legality rule when true.
  /// \param TypesAndMemDesc Legal type-pair and memory-descriptor combinations.
  LegalizeRuleSet &legalForTypesWithMemDesc(
      bool Pred, std::initializer_list<LegalityPredicates::TypePairAndMemDesc>
                     TypesAndMemDesc) {
    if (!Pred)
      return *this;
    return actionIf(LegalizeAction::Legal,
                    LegalityPredicates::typePairAndMemDescInSet(
                        typeIdx(0), typeIdx(1), /*MMOIdx=*/0, TypesAndMemDesc));
  }
  /// The instruction is legal when type indexes 0 and 1 are both in the given
  /// list. That is, the type pair is in the cartesian product of the list.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Types Legal types for type indexes 0 and 1.
  LegalizeRuleSet &legalForCartesianProduct(std::initializer_list<LLT> Types) {
    return actionForCartesianProduct(LegalizeAction::Legal, Types);
  }
  /// The instruction is legal when type indexes 0 and 1 are both their
  /// respective lists.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Types0 Legal types for type index 0.
  /// \param Types1 Legal types for type index 1.
  LegalizeRuleSet &legalForCartesianProduct(std::initializer_list<LLT> Types0,
                                            std::initializer_list<LLT> Types1) {
    return actionForCartesianProduct(LegalizeAction::Legal, Types0, Types1);
  }
  /// The instruction is legal when type indexes 0, 1, and 2 are both their
  /// respective lists.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Types0 Legal types for type index 0.
  /// \param Types1 Legal types for type index 1.
  /// \param Types2 Legal types for type index 2.
  LegalizeRuleSet &legalForCartesianProduct(std::initializer_list<LLT> Types0,
                                            std::initializer_list<LLT> Types1,
                                            std::initializer_list<LLT> Types2) {
    return actionForCartesianProduct(LegalizeAction::Legal, Types0, Types1,
                                     Types2);
  }

  /// Mark the instruction legal unconditionally.
  /// \returns A reference to this rule set for chaining.
  LegalizeRuleSet &alwaysLegal() {
    using namespace LegalizeMutations;
    markAllIdxsAsCovered();
    return actionIf(LegalizeAction::Legal, always);
  }

  /// The specified type index is coerced if predicate is true.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Predicate Condition that selects Bitcast.
  /// \param Mutation Mutation selecting the bitcast destination type.
  LegalizeRuleSet &bitcastIf(LegalityPredicate Predicate,
                             LegalizeMutation Mutation) {
    // We have no choice but conservatively assume that lowering with a
    // free-form user provided Predicate properly handles all type indices:
    markAllIdxsAsCovered();
    return actionIf(LegalizeAction::Bitcast, Predicate, Mutation);
  }

  /// The instruction is lowered.
  /// \returns A reference to this rule set for chaining.
  LegalizeRuleSet &lower() {
    using namespace LegalizeMutations;
    // We have no choice but conservatively assume that predicate-less lowering
    // properly handles all type indices by design:
    markAllIdxsAsCovered();
    return actionIf(LegalizeAction::Lower, always);
  }
  /// The instruction is lowered if predicate is true. Keep type index 0 as the
  /// same type.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Predicate Condition that selects Lower.
  LegalizeRuleSet &lowerIf(LegalityPredicate Predicate) {
    using namespace LegalizeMutations;
    // We have no choice but conservatively assume that lowering with a
    // free-form user provided Predicate properly handles all type indices:
    markAllIdxsAsCovered();
    return actionIf(LegalizeAction::Lower, Predicate);
  }
  /// The instruction is lowered if predicate is true.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Predicate Condition that selects Lower.
  /// \param Mutation Mutation applied while lowering.
  LegalizeRuleSet &lowerIf(LegalityPredicate Predicate,
                           LegalizeMutation Mutation) {
    // We have no choice but conservatively assume that lowering with a
    // free-form user provided Predicate properly handles all type indices:
    markAllIdxsAsCovered();
    return actionIf(LegalizeAction::Lower, Predicate, Mutation);
  }
  /// The instruction is lowered when type index 0 is any type in the given
  /// list. Keep type index 0 as the same type.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Types Types for type index 0 that select Lower.
  LegalizeRuleSet &lowerFor(std::initializer_list<LLT> Types) {
    return actionFor(LegalizeAction::Lower, Types);
  }
  /// The instruction is lowered when type index 0 is any type in the given
  /// list.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Types Types for type index 0 that select Lower.
  /// \param Mutation Mutation applied while lowering.
  LegalizeRuleSet &lowerFor(std::initializer_list<LLT> Types,
                            LegalizeMutation Mutation) {
    return actionFor(LegalizeAction::Lower, Types, Mutation);
  }
  /// The instruction is lowered when type indexes 0 and 1 is any type pair in
  /// the given list. Keep type index 0 as the same type.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Types Type pairs that select Lower.
  LegalizeRuleSet &lowerFor(std::initializer_list<std::pair<LLT, LLT>> Types) {
    return actionFor(LegalizeAction::Lower, Types);
  }
  /// The instruction is lowered when type indexes 0 and 1 is any type pair in
  /// the given list, provided Predicate pred is true.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Pred Guard that enables this lower rule when true.
  /// \param Types Type pairs that select Lower.
  LegalizeRuleSet &lowerFor(bool Pred,
                            std::initializer_list<std::pair<LLT, LLT>> Types) {
    if (!Pred)
      return *this;
    return actionFor(LegalizeAction::Lower, Types);
  }
  /// The instruction is lowered when type indexes 0 and 1 is any type pair in
  /// the given list.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Types Type pairs that select Lower.
  /// \param Mutation Mutation applied while lowering.
  LegalizeRuleSet &lowerFor(std::initializer_list<std::pair<LLT, LLT>> Types,
                            LegalizeMutation Mutation) {
    return actionFor(LegalizeAction::Lower, Types, Mutation);
  }
  /// The instruction is lowered when type indexes 0 and 1 are both in their
  /// respective lists.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Types0 Types for type index 0 that select Lower.
  /// \param Types1 Types for type index 1 that select Lower.
  LegalizeRuleSet &lowerForCartesianProduct(std::initializer_list<LLT> Types0,
                                            std::initializer_list<LLT> Types1) {
    using namespace LegalityPredicates;
    return actionForCartesianProduct(LegalizeAction::Lower, Types0, Types1);
  }
  /// The instruction is lowered when type indexes 0, 1, and 2 are all in
  /// their respective lists.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Types0 Types for type index 0 that select Lower.
  /// \param Types1 Types for type index 1 that select Lower.
  /// \param Types2 Types for type index 2 that select Lower.
  LegalizeRuleSet &lowerForCartesianProduct(std::initializer_list<LLT> Types0,
                                            std::initializer_list<LLT> Types1,
                                            std::initializer_list<LLT> Types2) {
    using namespace LegalityPredicates;
    return actionForCartesianProduct(LegalizeAction::Lower, Types0, Types1,
                                     Types2);
  }

  /// The instruction is emitted as a library call.
  /// \returns A reference to this rule set for chaining.
  LegalizeRuleSet &libcall() {
    using namespace LegalizeMutations;
    // We have no choice but conservatively assume that predicate-less lowering
    // properly handles all type indices by design:
    markAllIdxsAsCovered();
    return actionIf(LegalizeAction::Libcall, always);
  }

  /// Like legalIf, but for the Libcall action.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Predicate Condition that selects the Libcall action.
  LegalizeRuleSet &libcallIf(LegalityPredicate Predicate) {
    // We have no choice but conservatively assume that a libcall with a
    // free-form user provided Predicate properly handles all type indices:
    markAllIdxsAsCovered();
    return actionIf(LegalizeAction::Libcall, Predicate);
  }
  /// Emit a libcall when type index 0 is any type in \p Types.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Types Types for type index 0 that select Libcall.
  LegalizeRuleSet &libcallFor(std::initializer_list<LLT> Types) {
    return actionFor(LegalizeAction::Libcall, Types);
  }
  /// Emit a libcall for types in \p Types when \p Pred is true.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Pred Guard that enables this libcall rule when true.
  /// \param Types Types for type index 0 that select Libcall.
  LegalizeRuleSet &libcallFor(bool Pred, std::initializer_list<LLT> Types) {
    if (!Pred)
      return *this;
    return actionFor(LegalizeAction::Libcall, Types);
  }
  /// Emit a libcall when type indexes 0 and 1 match a pair in \p Types.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Types Type pairs that select Libcall.
  LegalizeRuleSet &
  libcallFor(std::initializer_list<std::pair<LLT, LLT>> Types) {
    return actionFor(LegalizeAction::Libcall, Types);
  }
  /// Emit a libcall for type pairs in \p Types when \p Pred is true.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Pred Guard that enables this libcall rule when true.
  /// \param Types Type pairs that select Libcall.
  LegalizeRuleSet &
  libcallFor(bool Pred, std::initializer_list<std::pair<LLT, LLT>> Types) {
    if (!Pred)
      return *this;
    return actionFor(LegalizeAction::Libcall, Types);
  }
  /// Emit a libcall when type indexes 0 and 1 are both in \p Types.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Types Types for type indexes 0 and 1 that select Libcall.
  LegalizeRuleSet &
  libcallForCartesianProduct(std::initializer_list<LLT> Types) {
    return actionForCartesianProduct(LegalizeAction::Libcall, Types);
  }
  /// Emit a libcall when type indexes 0 and 1 are in \p Types0 and \p Types1.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Types0 Types for type index 0 that select Libcall.
  /// \param Types1 Types for type index 1 that select Libcall.
  LegalizeRuleSet &
  libcallForCartesianProduct(std::initializer_list<LLT> Types0,
                             std::initializer_list<LLT> Types1) {
    return actionForCartesianProduct(LegalizeAction::Libcall, Types0, Types1);
  }

  /// Widen the scalar to the one selected by the mutation if the predicate is
  /// true.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Predicate Condition that selects WidenScalar.
  /// \param Mutation Mutation selecting the wider type.
  LegalizeRuleSet &widenScalarIf(LegalityPredicate Predicate,
                                 LegalizeMutation Mutation) {
    // We have no choice but conservatively assume that an action with a
    // free-form user provided Predicate properly handles all type indices:
    markAllIdxsAsCovered();
    return actionIf(LegalizeAction::WidenScalar, Predicate, Mutation);
  }
  /// Widen the scalar, specified in mutation, when type index 0 is any type in
  /// the given list.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Types Types that select WidenScalar for type index 0.
  /// \param Mutation Mutation selecting the wider type.
  LegalizeRuleSet &widenScalarFor(std::initializer_list<LLT> Types,
                                  LegalizeMutation Mutation) {
    return actionFor(LegalizeAction::WidenScalar, Types, Mutation);
  }
  /// Widen the scalar, specified in mutation, when type indexes 0 and 1 is any
  /// type pair in the given list.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Types Type pairs that select WidenScalar.
  /// \param Mutation Mutation selecting the wider type.
  LegalizeRuleSet &
  widenScalarFor(std::initializer_list<std::pair<LLT, LLT>> Types,
                 LegalizeMutation Mutation) {
    return actionFor(LegalizeAction::WidenScalar, Types, Mutation);
  }

  /// Narrow the scalar to the one selected by the mutation if the predicate is
  /// true.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Predicate Condition that selects NarrowScalar.
  /// \param Mutation Mutation selecting the narrower type.
  LegalizeRuleSet &narrowScalarIf(LegalityPredicate Predicate,
                                  LegalizeMutation Mutation) {
    // We have no choice but conservatively assume that an action with a
    // free-form user provided Predicate properly handles all type indices:
    markAllIdxsAsCovered();
    return actionIf(LegalizeAction::NarrowScalar, Predicate, Mutation);
  }
  /// Narrow the scalar, specified in mutation, when type indexes 0 and 1 is any
  /// type pair in the given list.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Types Type pairs that select NarrowScalar.
  /// \param Mutation Mutation selecting the narrower type.
  LegalizeRuleSet &
  narrowScalarFor(std::initializer_list<std::pair<LLT, LLT>> Types,
                  LegalizeMutation Mutation) {
    return actionFor(LegalizeAction::NarrowScalar, Types, Mutation);
  }

  /// Add more elements to reach the type selected by the mutation if the
  /// predicate is true.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Predicate Condition that selects MoreElements.
  /// \param Mutation Mutation selecting the more-element type.
  LegalizeRuleSet &moreElementsIf(LegalityPredicate Predicate,
                                  LegalizeMutation Mutation) {
    // We have no choice but conservatively assume that an action with a
    // free-form user provided Predicate properly handles all type indices:
    markAllIdxsAsCovered();
    return actionIf(LegalizeAction::MoreElements, Predicate, Mutation);
  }
  /// Remove elements to reach the type selected by the mutation if the
  /// predicate is true.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Predicate Condition that selects FewerElements.
  /// \param Mutation Mutation selecting the fewer-element type.
  LegalizeRuleSet &fewerElementsIf(LegalityPredicate Predicate,
                                   LegalizeMutation Mutation) {
    // We have no choice but conservatively assume that an action with a
    // free-form user provided Predicate properly handles all type indices:
    markAllIdxsAsCovered();
    return actionIf(LegalizeAction::FewerElements, Predicate, Mutation);
  }

  /// The instruction is unsupported.
  /// \returns A reference to this rule set for chaining.
  LegalizeRuleSet &unsupported() {
    markAllIdxsAsCovered();
    return actionIf(LegalizeAction::Unsupported, always);
  }
  /// Mark the instruction unsupported when \p Predicate is true.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Predicate Condition that selects the Unsupported action.
  LegalizeRuleSet &unsupportedIf(LegalityPredicate Predicate) {
    return actionIf(LegalizeAction::Unsupported, Predicate);
  }

  /// Mark the instruction unsupported when type index 0 is any type in \p Types.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Types Unsupported types for type index 0.
  LegalizeRuleSet &unsupportedFor(std::initializer_list<LLT> Types) {
    return actionFor(LegalizeAction::Unsupported, Types);
  }

  /// Mark the instruction unsupported if the memory size in bytes is not a power of 2.
  /// \returns A reference to this rule set for chaining.
  LegalizeRuleSet &unsupportedIfMemSizeNotPow2() {
    return actionIf(LegalizeAction::Unsupported,
                    LegalityPredicates::memSizeInBytesNotPow2(0));
  }

  /// Lower a memory operation if the memory size, rounded to bytes, is not a
  /// power of 2. For example, this will not trigger for s1 or s7, but will for
  /// s24.
  /// \returns A reference to this rule set for chaining.
  LegalizeRuleSet &lowerIfMemSizeNotPow2() {
    return actionIf(LegalizeAction::Lower,
                    LegalityPredicates::memSizeInBytesNotPow2(0));
  }

  /// Lower a memory op whose access size is not a power-of-two byte size.
  ///
  /// This is stricter than lowerIfMemSizeNotPow2, and more likely what you want
  /// (e.g. this will lower s1, s7 and s24).
  /// \returns A reference to this rule set for chaining.
  LegalizeRuleSet &lowerIfMemSizeNotByteSizePow2() {
    return actionIf(LegalizeAction::Lower,
                    LegalityPredicates::memSizeNotByteSizePow2(0));
  }

  /// The instruction is custom when \p Predicate is true.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Predicate Condition that selects the Custom action.
  LegalizeRuleSet &customIf(LegalityPredicate Predicate) {
    // We have no choice but conservatively assume that a custom action with a
    // free-form user provided Predicate properly handles all type indices:
    markAllIdxsAsCovered();
    return actionIf(LegalizeAction::Custom, Predicate);
  }
  /// The instruction is custom when type index 0 is any type in \p Types.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Types Legal types for type index 0.
  LegalizeRuleSet &customFor(std::initializer_list<LLT> Types) {
    return actionFor(LegalizeAction::Custom, Types);
  }
  /// The instruction is custom for types in \p Types when \p Pred is true.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Pred Guard that enables this custom rule when true.
  /// \param Types Legal types for type index 0.
  LegalizeRuleSet &customFor(bool Pred, std::initializer_list<LLT> Types) {
    if (!Pred)
      return *this;
    return actionFor(LegalizeAction::Custom, Types);
  }

  /// The instruction is custom when type indexes 0 and 1 is any type pair in
  /// the given list.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Types Legal type pairs for type indexes 0 and 1.
  LegalizeRuleSet &customFor(std::initializer_list<std::pair<LLT, LLT>> Types) {
    return actionFor(LegalizeAction::Custom, Types);
  }
  /// The instruction is custom for type pairs in \p Types when \p Pred is true.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Pred Guard that enables this custom rule when true.
  /// \param Types Legal type pairs for type indexes 0 and 1.
  LegalizeRuleSet &customFor(bool Pred,
                             std::initializer_list<std::pair<LLT, LLT>> Types) {
    if (!Pred)
      return *this;
    return actionFor(LegalizeAction::Custom, Types);
  }

  /// The instruction is custom when type indexes 0 and 1 are both in \p Types.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Types Legal types for type indexes 0 and 1.
  LegalizeRuleSet &customForCartesianProduct(std::initializer_list<LLT> Types) {
    return actionForCartesianProduct(LegalizeAction::Custom, Types);
  }
  /// The instruction is custom when type indexes 0 and 1 are both in their
  /// respective lists.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Types0 Legal types for type index 0.
  /// \param Types1 Legal types for type index 1.
  LegalizeRuleSet &
  customForCartesianProduct(std::initializer_list<LLT> Types0,
                            std::initializer_list<LLT> Types1) {
    return actionForCartesianProduct(LegalizeAction::Custom, Types0, Types1);
  }
  /// The instruction is custom when type indexes 0, 1, and 2 are all in
  /// their respective lists.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Types0 Legal types for type index 0.
  /// \param Types1 Legal types for type index 1.
  /// \param Types2 Legal types for type index 2.
  LegalizeRuleSet &
  customForCartesianProduct(std::initializer_list<LLT> Types0,
                            std::initializer_list<LLT> Types1,
                            std::initializer_list<LLT> Types2) {
    return actionForCartesianProduct(LegalizeAction::Custom, Types0, Types1,
                                     Types2);
  }

  /// The instruction is custom when the predicate is true and type indexes 0
  /// and 1 are all in their respective lists.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Pred Guard that enables this custom rule when true.
  /// \param Types0 Legal types for type index 0.
  /// \param Types1 Legal types for type index 1.
  LegalizeRuleSet &
  customForCartesianProduct(bool Pred, std::initializer_list<LLT> Types0,
                            std::initializer_list<LLT> Types1) {
    if (!Pred)
      return *this;
    return actionForCartesianProduct(LegalizeAction::Custom, Types0, Types1);
  }

  /// Unconditionally custom lower.
  /// \returns A reference to this rule set for chaining.
  LegalizeRuleSet &custom() {
    return customIf(always);
  }

  /// Widen the scalar to the next power of two that is at least MinSize.
  ///
  /// No effect if the type is a power of two, except if the type is smaller
  /// than MinSize, or if the type is a vector type.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param TypeIdx Type index being widened.
  /// \param MinSize Minimum bit width after widening.
  LegalizeRuleSet &widenScalarToNextPow2(unsigned TypeIdx,
                                         unsigned MinSize = 0) {
    using namespace LegalityPredicates;
    return actionIf(
        LegalizeAction::WidenScalar, sizeNotPow2(typeIdx(TypeIdx)),
        LegalizeMutations::widenScalarOrEltToNextPow2(TypeIdx, MinSize));
  }

  /// Widen the scalar to the next multiple of Size.
  ///
  /// No effect if the type is not a scalar or is a multiple of Size.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param TypeIdx Type index being widened.
  /// \param Size Alignment multiple for the scalar bit width.
  LegalizeRuleSet &widenScalarToNextMultipleOf(unsigned TypeIdx,
                                               unsigned Size) {
    using namespace LegalityPredicates;
    return actionIf(
        LegalizeAction::WidenScalar, sizeNotMultipleOf(typeIdx(TypeIdx), Size),
        LegalizeMutations::widenScalarOrEltToNextMultipleOf(TypeIdx, Size));
  }

  /// Widen the scalar or vector element type to the next power of two.
  ///
  /// The result is at least MinSize. No effect if the scalar size is a power of
  /// two.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param TypeIdx Type index being widened.
  /// \param MinSize Minimum bit width after widening.
  LegalizeRuleSet &widenScalarOrEltToNextPow2(unsigned TypeIdx,
                                              unsigned MinSize = 0) {
    using namespace LegalityPredicates;
    return actionIf(
        LegalizeAction::WidenScalar, scalarOrEltSizeNotPow2(typeIdx(TypeIdx)),
        LegalizeMutations::widenScalarOrEltToNextPow2(TypeIdx, MinSize));
  }

  /// Widen the scalar or element to the next power of two, or at least MinSize.
  ///
  /// No effect if the scalar size is already a power of two and at least
  /// MinSize.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param TypeIdx Type index being widened.
  /// \param MinSize Minimum bit width after widening.
  LegalizeRuleSet &widenScalarOrEltToNextPow2OrMinSize(unsigned TypeIdx,
                                                       unsigned MinSize = 0) {
    using namespace LegalityPredicates;
    return actionIf(
        LegalizeAction::WidenScalar,
        any(scalarOrEltNarrowerThan(TypeIdx, MinSize),
            scalarOrEltSizeNotPow2(typeIdx(TypeIdx))),
        LegalizeMutations::widenScalarOrEltToNextPow2(TypeIdx, MinSize));
  }

  /// Narrow the scalar at \p TypeIdx using \p Mutation.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param TypeIdx Type index of the scalar being narrowed.
  /// \param Mutation Mutation selecting the narrower type.
  LegalizeRuleSet &narrowScalar(unsigned TypeIdx, LegalizeMutation Mutation) {
    using namespace LegalityPredicates;
    return actionIf(LegalizeAction::NarrowScalar, isScalar(typeIdx(TypeIdx)),
                    Mutation);
  }

  /// Scalarize the vector at \p TypeIdx into its element type.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param TypeIdx Type index of the vector being scalarized.
  LegalizeRuleSet &scalarize(unsigned TypeIdx) {
    using namespace LegalityPredicates;
    return actionIf(LegalizeAction::FewerElements, isVector(typeIdx(TypeIdx)),
                    LegalizeMutations::scalarize(TypeIdx));
  }

  /// Scalarize the vector at \p TypeIdx when \p Predicate holds.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Predicate Condition that must hold for scalarization to apply.
  /// \param TypeIdx Type index of the vector being scalarized.
  LegalizeRuleSet &scalarizeIf(LegalityPredicate Predicate, unsigned TypeIdx) {
    using namespace LegalityPredicates;
    return actionIf(LegalizeAction::FewerElements,
                    all(Predicate, isVector(typeIdx(TypeIdx))),
                    LegalizeMutations::scalarize(TypeIdx));
  }

  /// Ensure the scalar or element is at least as wide as Ty.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param TypeIdx Type index being widened.
  /// \param Ty Minimum scalar or element type.
  LegalizeRuleSet &minScalarOrElt(unsigned TypeIdx, const LLT Ty) {
    using namespace LegalityPredicates;
    using namespace LegalizeMutations;
    return actionIf(LegalizeAction::WidenScalar,
                    scalarOrEltNarrowerThan(TypeIdx, Ty.getScalarSizeInBits()),
                    changeElementSizeTo(typeIdx(TypeIdx), Ty));
  }

  /// Ensure the scalar or element is at least as wide as Ty when \p Predicate holds.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Predicate Condition that must hold for the widen to apply.
  /// \param TypeIdx Type index being widened.
  /// \param Ty Minimum scalar or element type.
  LegalizeRuleSet &minScalarOrEltIf(LegalityPredicate Predicate,
                                    unsigned TypeIdx, const LLT Ty) {
    using namespace LegalityPredicates;
    using namespace LegalizeMutations;
    return actionIf(LegalizeAction::WidenScalar,
                    all(Predicate, scalarOrEltNarrowerThan(
                                       TypeIdx, Ty.getScalarSizeInBits())),
                    changeElementSizeTo(typeIdx(TypeIdx), Ty));
  }

  /// Ensure the vector size is at least as wide as VectorSize by promoting the
  /// element.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param TypeIdx Type index of the vector being widened.
  /// \param VectorSize Minimum total vector size in bits.
  LegalizeRuleSet &widenVectorEltsToVectorMinSize(unsigned TypeIdx,
                                                  unsigned VectorSize) {
    using namespace LegalityPredicates;
    using namespace LegalizeMutations;
    return actionIf(
        LegalizeAction::WidenScalar,
        [=](const LegalityQuery &Query) {
          const LLT VecTy = Query.Types[TypeIdx];
          return VecTy.isFixedVector() && VecTy.getSizeInBits() < VectorSize;
        },
        [=](const LegalityQuery &Query) {
          const LLT VecTy = Query.Types[TypeIdx];
          unsigned NumElts = VecTy.getNumElements();
          unsigned MinSize = VectorSize / NumElts;
          LLT NewTy = LLT::fixed_vector(
              NumElts, VecTy.getElementType().changeElementSize(MinSize));
          return std::make_pair(TypeIdx, NewTy);
        });
  }

  /// Ensure the scalar is at least as wide as Ty.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param TypeIdx Type index being widened.
  /// \param Ty Minimum scalar type.
  LegalizeRuleSet &minScalar(unsigned TypeIdx, const LLT Ty) {
    using namespace LegalityPredicates;
    using namespace LegalizeMutations;
    return actionIf(LegalizeAction::WidenScalar,
                    scalarNarrowerThan(TypeIdx, Ty.getSizeInBits()),
                    changeElementSizeTo(typeIdx(TypeIdx), Ty));
  }
  /// Conditionally ensure the scalar is at least as wide as Ty when \p Pred is true.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Pred Guard that enables this widen when true.
  /// \param TypeIdx Type index being widened.
  /// \param Ty Minimum scalar type.
  LegalizeRuleSet &minScalar(bool Pred, unsigned TypeIdx, const LLT Ty) {
    if (!Pred)
      return *this;
    return minScalar(TypeIdx, Ty);
  }

  /// Ensure the scalar is at least as wide as Ty if condition is met.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Predicate Condition that must hold for the widen to apply.
  /// \param TypeIdx Type index being widened.
  /// \param Ty Minimum scalar type.
  LegalizeRuleSet &minScalarIf(LegalityPredicate Predicate, unsigned TypeIdx,
                               const LLT Ty) {
    using namespace LegalityPredicates;
    using namespace LegalizeMutations;
    return actionIf(
        LegalizeAction::WidenScalar,
        [=](const LegalityQuery &Query) {
          const LLT QueryTy = Query.Types[TypeIdx];
          return QueryTy.isScalar() &&
                 QueryTy.getSizeInBits() < Ty.getSizeInBits() &&
                 Predicate(Query);
        },
        changeElementSizeTo(typeIdx(TypeIdx), Ty));
  }

  /// Ensure the scalar or element is at most as wide as Ty.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param TypeIdx Type index being narrowed.
  /// \param Ty Maximum scalar or element type.
  LegalizeRuleSet &maxScalarOrElt(unsigned TypeIdx, const LLT Ty) {
    using namespace LegalityPredicates;
    using namespace LegalizeMutations;
    return actionIf(LegalizeAction::NarrowScalar,
                    scalarOrEltWiderThan(TypeIdx, Ty.getScalarSizeInBits()),
                    changeElementSizeTo(typeIdx(TypeIdx), Ty));
  }

  /// Ensure the scalar is at most as wide as Ty.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param TypeIdx Type index being narrowed.
  /// \param Ty Maximum scalar type.
  LegalizeRuleSet &maxScalar(unsigned TypeIdx, const LLT Ty) {
    using namespace LegalityPredicates;
    using namespace LegalizeMutations;
    return actionIf(LegalizeAction::NarrowScalar,
                    scalarWiderThan(TypeIdx, Ty.getSizeInBits()),
                    changeElementSizeTo(typeIdx(TypeIdx), Ty));
  }

  /// Conditionally limit the maximum size of the scalar.
  ///
  /// For example, when the maximum size of one type depends on the size of
  /// another such as extracting N bits from an M bit container.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Predicate Condition that must hold for the clamp to apply.
  /// \param TypeIdx Type index being narrowed.
  /// \param Ty Maximum scalar type.
  LegalizeRuleSet &maxScalarIf(LegalityPredicate Predicate, unsigned TypeIdx,
                               const LLT Ty) {
    using namespace LegalityPredicates;
    using namespace LegalizeMutations;
    return actionIf(
        LegalizeAction::NarrowScalar,
        [=](const LegalityQuery &Query) {
          const LLT QueryTy = Query.Types[TypeIdx];
          return QueryTy.isScalar() &&
                 QueryTy.getSizeInBits() > Ty.getSizeInBits() &&
                 Predicate(Query);
        },
        changeElementSizeTo(typeIdx(TypeIdx), Ty));
  }

  /// Limit the range of scalar sizes to MinTy and MaxTy.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param TypeIdx Type index being clamped.
  /// \param MinTy Minimum scalar type.
  /// \param MaxTy Maximum scalar type.
  LegalizeRuleSet &clampScalar(unsigned TypeIdx, const LLT MinTy,
                               const LLT MaxTy) {
    assert(MinTy.isScalar() && MaxTy.isScalar() && "Expected scalar types");
    return minScalar(TypeIdx, MinTy).maxScalar(TypeIdx, MaxTy);
  }

  /// Conditionally clamp scalar sizes to MinTy and MaxTy when \p Pred is true.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Pred Guard that enables this clamp when true.
  /// \param TypeIdx Type index being clamped.
  /// \param MinTy Minimum scalar type.
  /// \param MaxTy Maximum scalar type.
  LegalizeRuleSet &clampScalar(bool Pred, unsigned TypeIdx, const LLT MinTy,
                               const LLT MaxTy) {
    if (!Pred)
      return *this;
    return clampScalar(TypeIdx, MinTy, MaxTy);
  }

  /// Limit the range of scalar or element sizes to MinTy and MaxTy.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param TypeIdx Type index being clamped.
  /// \param MinTy Minimum scalar or element type.
  /// \param MaxTy Maximum scalar or element type.
  LegalizeRuleSet &clampScalarOrElt(unsigned TypeIdx, const LLT MinTy,
                                    const LLT MaxTy) {
    return minScalarOrElt(TypeIdx, MinTy).maxScalarOrElt(TypeIdx, MaxTy);
  }

  /// Widen the scalar to match the size of another.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param TypeIdx Type index being widened.
  /// \param LargeTypeIdx Type index providing the wider scalar size.
  LegalizeRuleSet &minScalarSameAs(unsigned TypeIdx, unsigned LargeTypeIdx) {
    typeIdx(TypeIdx);
    return actionIf(
        LegalizeAction::WidenScalar,
        [=](const LegalityQuery &Query) {
          return Query.Types[LargeTypeIdx].getScalarSizeInBits() >
                 Query.Types[TypeIdx].getSizeInBits();
        },
        LegalizeMutations::changeElementSizeTo(TypeIdx, LargeTypeIdx));
  }

  /// Narrow the scalar to match the size of another.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param TypeIdx Type index being narrowed.
  /// \param NarrowTypeIdx Type index providing the narrower scalar size.
  LegalizeRuleSet &maxScalarSameAs(unsigned TypeIdx, unsigned NarrowTypeIdx) {
    typeIdx(TypeIdx);
    return actionIf(
        LegalizeAction::NarrowScalar,
        [=](const LegalityQuery &Query) {
          return Query.Types[NarrowTypeIdx].getScalarSizeInBits() <
                 Query.Types[TypeIdx].getSizeInBits();
        },
        LegalizeMutations::changeElementSizeTo(TypeIdx, NarrowTypeIdx));
  }

  /// Change the type \p TypeIdx to have the same scalar size as type \p
  /// SameSizeIdx.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param TypeIdx Type index being resized.
  /// \param SameSizeIdx Type index whose scalar size is matched.
  LegalizeRuleSet &scalarSameSizeAs(unsigned TypeIdx, unsigned SameSizeIdx) {
    return minScalarSameAs(TypeIdx, SameSizeIdx)
          .maxScalarSameAs(TypeIdx, SameSizeIdx);
  }

  /// Conditionally widen the scalar or elt to match the size of another.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Predicate Additional condition that must hold for the rule to apply.
  /// \param TypeIdx Type index being widened.
  /// \param LargeTypeIdx Type index providing the wider scalar size.
  LegalizeRuleSet &minScalarEltSameAsIf(LegalityPredicate Predicate,
                                   unsigned TypeIdx, unsigned LargeTypeIdx) {
    typeIdx(TypeIdx);
    return widenScalarIf(
        [=](const LegalityQuery &Query) {
          return Query.Types[LargeTypeIdx].getScalarSizeInBits() >
                     Query.Types[TypeIdx].getScalarSizeInBits() &&
                 Predicate(Query);
        },
        [=](const LegalityQuery &Query) {
          LLT T = Query.Types[TypeIdx].changeElementSize(
              Query.Types[LargeTypeIdx].getScalarSizeInBits());
          return std::make_pair(TypeIdx, T);
        });
  }

  /// Conditionally narrow the scalar or elt to match the size of another.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param Predicate Additional condition that must hold for the rule to apply.
  /// \param TypeIdx Type index being narrowed.
  /// \param SmallTypeIdx Type index providing the narrower scalar size.
  LegalizeRuleSet &maxScalarEltSameAsIf(LegalityPredicate Predicate,
                                        unsigned TypeIdx,
                                        unsigned SmallTypeIdx) {
    typeIdx(TypeIdx);
    return narrowScalarIf(
        [=](const LegalityQuery &Query) {
          return Query.Types[SmallTypeIdx].getScalarSizeInBits() <
                     Query.Types[TypeIdx].getScalarSizeInBits() &&
                 Predicate(Query);
        },
        [=](const LegalityQuery &Query) {
          LLT T = Query.Types[SmallTypeIdx];
          return std::make_pair(TypeIdx, T);
        });
  }

  /// Add more elements to the vector to reach the next power of two.
  ///
  /// No effect if the type is not a vector or the element count is a power of
  /// two.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param TypeIdx Type index of the vector to widen in element count.
  LegalizeRuleSet &moreElementsToNextPow2(unsigned TypeIdx) {
    using namespace LegalityPredicates;
    return actionIf(LegalizeAction::MoreElements,
                    numElementsNotPow2(typeIdx(TypeIdx)),
                    LegalizeMutations::moreElementsToNextPow2(TypeIdx));
  }

  /// Limit the number of elements in EltTy vectors to at least MinElements.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param TypeIdx Type index of the vector being constrained.
  /// \param EltTy Element type of vectors this rule applies to.
  /// \param MinElements Minimum number of vector elements required.
  LegalizeRuleSet &clampMinNumElements(unsigned TypeIdx, const LLT EltTy,
                                       unsigned MinElements) {
    // Mark the type index as covered:
    typeIdx(TypeIdx);
    return actionIf(
        LegalizeAction::MoreElements,
        [=](const LegalityQuery &Query) {
          LLT VecTy = Query.Types[TypeIdx];
          return VecTy.isFixedVector() && VecTy.getElementType() == EltTy &&
                 VecTy.getNumElements() < MinElements;
        },
        [=](const LegalityQuery &Query) {
          LLT VecTy = Query.Types[TypeIdx];
          return std::make_pair(
              TypeIdx, LLT::fixed_vector(MinElements, VecTy.getElementType()));
        });
  }

  /// Set number of elements to nearest larger multiple of NumElts.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param TypeIdx Type index of the vector being aligned.
  /// \param EltTy Element type of vectors this rule applies to.
  /// \param NumElts Alignment multiple for the element count.
  LegalizeRuleSet &alignNumElementsTo(unsigned TypeIdx, const LLT EltTy,
                                      unsigned NumElts) {
    typeIdx(TypeIdx);
    return actionIf(
        LegalizeAction::MoreElements,
        [=](const LegalityQuery &Query) {
          LLT VecTy = Query.Types[TypeIdx];
          return VecTy.isFixedVector() && VecTy.getElementType() == EltTy &&
                 (VecTy.getNumElements() % NumElts != 0);
        },
        [=](const LegalityQuery &Query) {
          LLT VecTy = Query.Types[TypeIdx];
          unsigned NewSize = alignTo(VecTy.getNumElements(), NumElts);
          return std::make_pair(
              TypeIdx, LLT::fixed_vector(NewSize, VecTy.getElementType()));
        });
  }

  /// Limit the number of elements in EltTy vectors to at most MaxElements.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param TypeIdx Type index of the vector being constrained.
  /// \param EltTy Element type of vectors this rule applies to.
  /// \param MaxElements Maximum number of vector elements allowed.
  LegalizeRuleSet &clampMaxNumElements(unsigned TypeIdx, const LLT EltTy,
                                       unsigned MaxElements) {
    // Mark the type index as covered:
    typeIdx(TypeIdx);
    return actionIf(
        LegalizeAction::FewerElements,
        [=](const LegalityQuery &Query) {
          LLT VecTy = Query.Types[TypeIdx];
          return VecTy.isFixedVector() && VecTy.getElementType() == EltTy &&
                 VecTy.getNumElements() > MaxElements;
        },
        [=](const LegalityQuery &Query) {
          LLT VecTy = Query.Types[TypeIdx];
          LLT NewTy = LLT::scalarOrVector(ElementCount::getFixed(MaxElements),
                                          VecTy.getElementType());
          return std::make_pair(TypeIdx, NewTy);
        });
  }
  /// Limit vector element counts to the range described by \p MinTy and \p MaxTy.
  ///
  /// No effect if the type is not a vector or does not have the same element
  /// type as the constraints. The element type of MinTy and MaxTy must match.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param TypeIdx Type index of the vector being constrained.
  /// \param MinTy Vector type describing the minimum element count.
  /// \param MaxTy Vector type describing the maximum element count.
  LegalizeRuleSet &clampNumElements(unsigned TypeIdx, const LLT MinTy,
                                    const LLT MaxTy) {
    assert(MinTy.getElementType() == MaxTy.getElementType() &&
           "Expected element types to agree");

    assert((!MinTy.isScalableVector() && !MaxTy.isScalableVector()) &&
           "Unexpected scalable vectors");

    const LLT EltTy = MinTy.getElementType();
    return clampMinNumElements(TypeIdx, EltTy, MinTy.getNumElements())
        .clampMaxNumElements(TypeIdx, EltTy, MaxTy.getNumElements());
  }

  /// Express \p EltTy vectors using only vectors (or scalars) of \p NumElts lanes.
  ///
  /// First pad with undef elements to nearest larger multiple of \p NumElts.
  /// Then perform split with all sub-instructions having the same type.
  /// Using clampMaxNumElements (non-strict) can result in leftover instruction
  /// with different type (fewer elements then \p NumElts or scalar).
  /// No effect if the type is not a vector.
  ///
  /// \returns A reference to this rule set for chaining.
  /// \param TypeIdx Type index of the vector being constrained.
  /// \param EltTy Element type of vectors this rule applies to.
  /// \param NumElts Exact element count required after padding and splitting.
  LegalizeRuleSet &clampMaxNumElementsStrict(unsigned TypeIdx, const LLT EltTy,
                                             unsigned NumElts) {
    return alignNumElementsTo(TypeIdx, EltTy, NumElts)
        .clampMaxNumElements(TypeIdx, EltTy, NumElts);
  }

  /// Check if there is no type index which is obviously not handled by the
  /// LegalizeRuleSet in any way at all.
  ///
  /// \returns true if every type index in [0, \p NumTypeIdxs) is covered by this rule set.
  /// \pre Type indices of the opcode form a dense [0, \p NumTypeIdxs) set.
  /// \param NumTypeIdxs Number of type indices the opcode is expected to use.
  LLVM_ABI bool verifyTypeIdxsCoverage(unsigned NumTypeIdxs) const;
  /// Check if there is no imm index which is obviously not handled by the
  /// LegalizeRuleSet in any way at all.
  ///
  /// \returns true if every immediate index in [0, \p NumImmIdxs) is covered by this rule set.
  /// \pre Imm indices of the opcode form a dense [0, \p NumImmIdxs) set.
  /// \param NumImmIdxs Number of immediate indices the opcode is expected to use.
  LLVM_ABI bool verifyImmIdxsCoverage(unsigned NumImmIdxs) const;

  /// Apply the ruleset to the given LegalityQuery.
  ///
  /// \returns The legalize action step selected for \p Query.
  /// \param Query Legality query to evaluate against this ruleset.
  LLVM_ABI LegalizeActionStep apply(const LegalityQuery &Query) const;
};

/// Describe which GlobalISel operations are legal and how to legalize others.
class LLVM_ABI LegalizerInfo {
public:
  /// Destroy the legalizer info.
  virtual ~LegalizerInfo() = default;

  /// Map a target opcode to an index in the action-definition tables.
  ///
  /// \returns The table index corresponding to \p Opcode.
  /// \param Opcode Opcode to translate into a table index.
  unsigned getOpcodeIdxForOpcode(unsigned Opcode) const;
  /// Return the table index holding action definitions for \p Opcode.
  ///
  /// \returns The index of the action-definition entry for \p Opcode.
  /// \param Opcode Opcode whose action-definition index is requested.
  unsigned getActionDefinitionsIdx(unsigned Opcode) const;

  /// Perform simple self-diagnostic and assert if actions are set up incorrectly.
  ///
  /// \param MII Instruction info used to verify opcode coverage.
  void verify(const MCInstrInfo &MII) const;

  /// Get the action definitions for the given opcode.
  ///
  /// Use this to run a LegalityQuery through the definitions.
  ///
  /// \returns The action definitions for \p Opcode.
  /// \param Opcode Opcode whose action definitions are requested.
  const LegalizeRuleSet &getActionDefinitions(unsigned Opcode) const;

  /// Get the action definition builder for the given opcode.
  ///
  /// Use this to define the action definitions.
  ///
  /// It is an error to request an opcode that has already been requested by the
  /// multiple-opcode variant.
  ///
  /// \returns The rule-set builder for \p Opcode.
  /// \param Opcode Opcode whose rule set is being defined.
  LegalizeRuleSet &getActionDefinitionsBuilder(unsigned Opcode);

  /// Get the action definition builder for several opcodes at once.
  ///
  /// Use this to define the action definitions for multiple opcodes at once.
  /// The first opcode given will be considered the representative opcode and
  /// will hold the definitions whereas the other opcodes will be configured to
  /// refer to the representative opcode. This lowers memory requirements and
  /// very slightly improves performance.
  ///
  /// It would be very easy to introduce unexpected side-effects as a result of
  /// this aliasing if it were permitted to request different but intersecting
  /// sets of opcodes but that is difficult to keep track of. It is therefore an
  /// error to request the same opcode twice using this API, to request an
  /// opcode that already has definitions, or to use the single-opcode API on an
  /// opcode that has already been requested by this API.
  ///
  /// \returns The shared rule-set builder for \p Opcodes.
  /// \param Opcodes Opcodes that share one representative rule set.
  LegalizeRuleSet &
  getActionDefinitionsBuilder(std::initializer_list<unsigned> Opcodes);
  /// Make \p OpcodeTo reuse the action definitions of \p OpcodeFrom.
  ///
  /// \param OpcodeTo Opcode that becomes an alias.
  /// \param OpcodeFrom Opcode whose definitions are reused.
  void aliasActionDefinitions(unsigned OpcodeTo, unsigned OpcodeFrom);

  /// Determine what action should be taken to legalize the described instruction.
  ///
  /// Requires computeTables to have been called.
  ///
  /// \returns a description of the next legalization step to perform.
  /// \param Query Legality query describing the operation and types.
  LegalizeActionStep getAction(const LegalityQuery &Query) const;

  /// Determine what action should be taken to legalize the given generic
  /// instruction.
  ///
  /// \returns a description of the next legalization step to perform.
  /// \param MI Generic machine instruction to legalize.
  /// \param MRI Register information used to inspect operand types.
  LegalizeActionStep getAction(const MachineInstr &MI,
                               const MachineRegisterInfo &MRI) const;

  /// Return whether \p Query describes a fully legal operation.
  ///
  /// \returns true if \p Query describes a fully legal operation.
  /// \param Query Legality query describing the operation and types.
  bool isLegal(const LegalityQuery &Query) const {
    return getAction(Query).Action == LegalizeAction::Legal;
  }

  /// Return whether \p Query is legal or requires only custom legalization.
  ///
  /// \returns true if \p Query is legal or only requires custom legalization.
  /// \param Query Legality query describing the operation and types.
  bool isLegalOrCustom(const LegalityQuery &Query) const {
    auto Action = getAction(Query).Action;
    return Action == LegalizeAction::Legal || Action == LegalizeAction::Custom;
  }

  /// Return whether generic instruction \p MI is fully legal.
  ///
  /// \returns true if \p MI is fully legal.
  /// \param MI Generic machine instruction to check.
  /// \param MRI Register information used to inspect operand types.
  bool isLegal(const MachineInstr &MI, const MachineRegisterInfo &MRI) const;
  /// Return whether \p MI is legal or requires only custom legalization.
  ///
  /// \returns true if \p MI is legal or only requires custom legalization.
  /// \param MI Generic machine instruction to check.
  /// \param MRI Register information used to inspect operand types.
  bool isLegalOrCustom(const MachineInstr &MI,
                       const MachineRegisterInfo &MRI) const;

  /// Legalize an instruction that selected the Custom legalization action.
  ///
  /// \returns true if \p MI was successfully custom-legalized.
  /// \param Helper Legalizer helper used to rewrite the instruction.
  /// \param MI Instruction with the Custom legalization action.
  /// \param LocObserver Observer for lost debug locations during legalization.
  virtual bool legalizeCustom(LegalizerHelper &Helper, MachineInstr &MI,
                              LostDebugLocObserver &LocObserver) const {
    llvm_unreachable("must implement this if custom action is used");
  }

  /// Legalize intrinsic instruction \p MI, or report whether it is already legal.
  ///
  /// \returns true if \p MI is either legal or has been legalized and false if
  /// not legal.
  /// \param Helper Legalizer helper used to rewrite the instruction.
  /// \param MI Intrinsic instruction to legalize.
  virtual bool legalizeIntrinsic(LegalizerHelper &Helper,
                                 MachineInstr &MI) const {
    return true;
  }

  /// Return the extension opcode to use when widening a constant of \p SmallTy.
  ///
  /// Targets can override this. For eg, the DAG does
  /// (SmallTy.isByteSized() ? G_SEXT : G_ZEXT) which will be the default.
  ///
  /// \returns The extension opcode to use when widening a constant of type \p SmallTy.
  /// \param SmallTy Constant type being widened.
  virtual unsigned getExtOpcodeForWideningConstant(LLT SmallTy) const;

private:
  static const int FirstOp = TargetOpcode::PRE_ISEL_GENERIC_OPCODE_START;
  static const int LastOp = TargetOpcode::PRE_ISEL_GENERIC_OPCODE_END;

  LegalizeRuleSet RulesForOpcode[LastOp - FirstOp + 1];
};

#ifndef NDEBUG
/// Return an illegal instruction in \p MF, or nullptr if MIR is fully legal.
///
/// \returns An illegal instruction in \p MF, or nullptr if MIR is fully legal.
/// \param MF Machine function whose MIR legality is checked.
const MachineInstr *machineFunctionIsIllegal(const MachineFunction &MF);
#endif

} // end namespace llvm.

#endif // LLVM_CODEGEN_GLOBALISEL_LEGALIZERINFO_H
