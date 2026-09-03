//===- llvm/Analysis/ScalarEvolution.h - Scalar Evolution -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The ScalarEvolution class is an LLVM pass which can be used to analyze and
// categorize scalar expressions in loops.  It specializes in recognizing
// general induction variables, representing them with the abstract and opaque
// SCEV class.  Given this analysis, trip counts of loops and other important
// properties can be obtained.
//
// This analysis is primarily useful for induction variable substitution and
// strength reduction.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_SCALAREVOLUTION_H
#define LLVM_ANALYSIS_SCALAREVOLUTION_H

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/Pass.h"
#include "llvm/Support/Compiler.h"
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

namespace llvm {

/// Binary operator that can signal signed or unsigned overflow.
class OverflowingBinaryOperator;
class AssumptionCache;
class BasicBlock;
class Constant;
class ConstantInt;
class DataLayout;
class DominatorTree;
class GEPOperator;
class LLVMContext;
class Loop;
class LoopInfo;
class raw_ostream;
class ScalarEvolution;
/// SCEV add-recurrence expression representing a loop induction formula.
class SCEVAddRecExpr;
/// SCEV expression wrapping a constant integer value.
class SCEVConstant;
/// SCEV expression for a value that ScalarEvolution cannot analyze further.
class SCEVUnknown;
class StructType;
class TargetLibraryInfo;
class Type;
/// Vector predication SCEV expander (defined in SCEVExpander.h).
class VPSCEVExpander;
enum SCEVTypes : unsigned short;

/// If true, verify ScalarEvolution after each transformation that uses it.
LLVM_ABI extern bool VerifySCEV;

/// NoWrapFlags are bitfield indices into SCEV's SubclassData.
///
/// Add and Mul expressions may have no-unsigned-wrap <NUW> or
/// no-signed-wrap <NSW> properties, which are derived from the IR
/// operator. NSW is a misnomer that we use to mean no signed overflow or
/// underflow. NUW and NSW must hold for all subsets and orders of
/// Add/Mul operands. That is, in `(a + b + c)<nsw>`, all of `a + b`,
/// `b + c`, `a + c` must be nsw as well.
///
/// AddRec expressions may have a no-self-wraparound <NW> property if, in
/// the integer domain, abs(step) * max-iteration(loop) <=
/// unsigned-max(bitwidth).  This means that the recurrence will never reach
/// its start value if the step is non-zero.  Computing the same value on
/// each iteration is not considered wrapping, and recurrences with step = 0
/// are trivially <NW>.  <NW> is independent of the sign of step and the
/// value the add recurrence starts with.
///
/// Note that NUW and NSW are also valid properties of a recurrence, and
/// either implies NW. For convenience, NW will be set for a recurrence
/// whenever either NUW or NSW are set.
///
/// We require that the flag on a SCEV apply to the entire scope in which
/// that SCEV is defined.  A SCEV's scope is set of locations dominated by
/// a defining location, which is in turn described by the following rules:
/// * A SCEVUnknown is at the point of definition of the Value.
/// * A SCEVConstant is defined at all points.
/// * A SCEVAddRec is defined starting with the header of the associated
///   loop.
/// * All other SCEVs are defined at the earlest point all operands are
///   defined.
///
/// The above rules describe a maximally hoisted form (without regards to
/// potential control dependence).  A SCEV is defined anywhere a
/// corresponding instruction could be defined in said maximally hoisted
/// form.  Note that SCEVUDivExpr (currently the only expression type which
/// can trap) can be defined per these rules in regions where it would trap
/// at runtime.  A SCEV being defined does not require the existence of any
/// instruction within the defined scope.
enum class SCEVNoWrapFlags {
  FlagAnyWrap = 0,    ///< No wrap guarantee.
  FlagNW = (1 << 0),  ///< No self-wrap.
  FlagNUW = (1 << 1), ///< No unsigned wrap.
  FlagNSW = (1 << 2), ///< No signed wrap.
  NoWrapMask = (1 << 3) - 1, ///< Mask of all no-wrap flag bits.
  LLVM_MARK_AS_BITMASK_ENUM(/*LargestValue=*/NoWrapMask) ///< Largest enumerator.
};

class SCEV;

/// SCEV pointer plus optional use-specific NUW/NSW flags.
template <typename SCEVPtrT = const SCEV *>
struct SCEVUseT : private PointerIntPair<SCEVPtrT, 2> {
  /// Underlying PointerIntPair storage.
  using Base = PointerIntPair<SCEVPtrT, 2>;
  /// Return the packed pointer-int bits.
  using Base::getOpaqueValue;
  /// Return the wrapped SCEV pointer.
  using Base::getPointer;

  /// Construct a null SCEVUse with no extra flags.
  SCEVUseT() : Base(nullptr, 0) {}
  /// Construct a SCEVUse for \p S with no extra flags.
  ///
  /// \param S The SCEV pointer to wrap.
  SCEVUseT(SCEVPtrT S) : Base(S, 0) {}
  /// Construct a SCEVUse for \p S with extra no-wrap flags.
  ///
  /// Only NUW/NSW are encoded; NW is dropped. \p S must be an expression
  /// supporting flags. Only flags not already present on \p S are added.
  /// Note that the expression may gain flags also part of the SCEVUse later,
  /// via settNoWrapFlags.
  ///
  /// \param S The SCEV pointer to wrap.
  /// \param Flags Extra no-wrap flags to encode on this use.
  SCEVUseT(SCEVPtrT S, SCEVNoWrapFlags Flags);
  /// Convert from a SCEVUse of a compatible pointer type.
  ///
  /// \param Other SCEVUse to convert from.
  template <typename OtherPtrT, typename = std::enable_if_t<
                                    std::is_convertible_v<OtherPtrT, SCEVPtrT>>>
  SCEVUseT(const SCEVUseT<OtherPtrT> &Other)
      : SCEVUseT(Other.getPointer(), Other.getUseNoWrapFlags()) {}

  /// Return the wrapped SCEV pointer.
  /// @return The wrapped SCEV pointer.
  operator SCEVPtrT() const { return getPointer(); }
  /// Return the wrapped SCEV pointer.
  /// @return The wrapped SCEV pointer.
  SCEVPtrT operator->() const { return getPointer(); }

  /// Returns true if the SCEVUse is canonical, i.e. no SCEVUse flags set in any
  /// operands.
  /// @return True if the SCEVUse is canonical, i.e. no SCEVUse flags set in any operands.
  bool isCanonical() const { return getCanonical() == getOpaqueValue(); }

  /// Returns true if this use itself carries use-specific no-wrap flags.
  /// @return True if this use itself carries use-specific no-wrap flags.
  bool hasUseFlags() const { return getOpaqueValue() != getPointer(); }

  /// Return the canonical SCEV for this SCEVUse.
  /// @return The canonical SCEV for this SCEVUse.
  const SCEV *getCanonical() const;

  /// Return the no-wrap flags for this SCEVUse, which is the union of the
  /// use-specific flags and the underlying SCEV's flags, masked by \p Mask.
  ///
  /// \param Mask Flag bits to keep.
  /// @return The no-wrap flags for this SCEVUse, which is the union of the use-specific flags and
  /// the underlying SCEV's flags, masked by \p Mask.
  SCEVNoWrapFlags
  getNoWrapFlags(SCEVNoWrapFlags Mask = SCEVNoWrapFlags::NoWrapMask) const;

  /// Return only the use-specific no-wrap flags (NUW/NSW) without the
  /// underlying SCEV's flags.
  /// @return Only the use-specific no-wrap flags (NUW/NSW) without the underlying SCEV's flags.
  SCEVNoWrapFlags getUseNoWrapFlags() const {
    SCEVNoWrapFlags UseFlags =
        static_cast<SCEVNoWrapFlags>(Base::getInt() << 1);
    if (any(UseFlags & (SCEVNoWrapFlags::FlagNUW | SCEVNoWrapFlags::FlagNSW)))
      UseFlags |= SCEVNoWrapFlags::FlagNW;
    return UseFlags;
  }

  /// Return true if this SCEVUse equals \p RHS, including use flags.
  ///
  /// \param RHS Other SCEVUse.
  /// @return True if this SCEVUse equals \p RHS, including use flags.
  bool operator==(const SCEVUseT &RHS) const {
    return getOpaqueValue() == RHS.getOpaqueValue();
  }

  /// Return true if this SCEVUse differs from \p RHS, including use flags.
  ///
  /// \param RHS Other SCEVUse.
  /// @return True if this SCEVUse differs from \p RHS, including use flags.
  bool operator!=(const SCEVUseT &RHS) const { return !(*this == RHS); }

  /// Return true if this SCEVUse compares greater than \p RHS.
  ///
  /// \param RHS Other SCEVUse.
  /// @return True if this SCEVUse compares greater than \p RHS.
  bool operator>(const SCEVUseT &RHS) const { return Base::operator>(RHS); }

  /// Return true if the opaque value equals SCEV pointer \p RHS.
  ///
  /// \param RHS SCEV pointer to compare.
  /// @return True if the opaque value equals SCEV pointer \p RHS.
  bool operator==(const SCEV *RHS) const { return getOpaqueValue() == RHS; }
  /// Return true if the opaque value differs from SCEV pointer \p RHS.
  ///
  /// \param RHS SCEV pointer to compare.
  /// @return True if the opaque value differs from SCEV pointer \p RHS.
  bool operator!=(const SCEV *RHS) const { return getOpaqueValue() != RHS; }

  /// Print this SCEVUse to \p OS, including any use-specific flags.
  ///
  /// This should really only be used for debugging purposes.
  ///
  /// \param OS Stream to print to.
  void print(raw_ostream &OS) const;

  /// This method is used for debugging.
  void dump() const;

private:
  using Base::setFromOpaqueValue;
  friend struct PointerLikeTypeTraits<SCEVUseT>;
};

/// Deduction guide for various SCEV subclass pointers.
template <typename SCEVPtrT> SCEVUseT(SCEVPtrT) -> SCEVUseT<SCEVPtrT>;

/// SCEVUse of a const SCEV pointer.
using SCEVUse = SCEVUseT<const SCEV *>;

/// Provide PointerLikeTypeTraits for SCEVUse, so it can be used with
/// SmallPtrSet, among others.
template <> struct PointerLikeTypeTraits<SCEVUse> {
  /// Convert \p U to a void pointer.
  ///
  /// \param U SCEVUse to convert.
  /// @return Void pointer representation of \p U.
  static inline void *getAsVoidPointer(SCEVUse U) { return U.getOpaqueValue(); }
  /// Reconstruct a SCEVUse from void pointer \p P.
  ///
  /// \param P Pointer previously produced by getAsVoidPointer.
  /// @return A SCEVUse from void pointer \p P.
  static inline SCEVUse getFromVoidPointer(void *P) {
    SCEVUse U;
    U.setFromOpaqueValue(P);
    return U;
  }

  /// The Low bits are used by the PointerIntPair.
  static constexpr int NumLowBitsAvailable = 0;
};

/// DenseMapInfo specialization for SCEVUse.
template <> struct DenseMapInfo<SCEVUse> {
  /// Return the hash of \p U.
  ///
  /// \param U SCEVUse to hash.
  /// @return Hash of \p U.
  static unsigned getHashValue(SCEVUse U) {
    return hash_value(U.getOpaqueValue());
  }

  /// Return true if \p LHS and \p RHS have the same opaque value.
  ///
  /// \param LHS Left-hand SCEVUse.
  /// \param RHS Right-hand SCEVUse.
  /// @return True if \p LHS and \p RHS have the same opaque value.
  static bool isEqual(const SCEVUse LHS, const SCEVUse RHS) {
    return LHS.getOpaqueValue() == RHS.getOpaqueValue();
  }
};

/// simplify_type specialization that unwraps SCEVUse to a SCEV pointer.
template <> struct simplify_type<SCEVUse> {
  /// Unwrapped type.
  using SimpleType = const SCEV *;

  /// Return the SCEV pointer stored in \p Val.
  ///
  /// \param Val SCEVUse to unwrap.
  /// @return The SCEV pointer stored in \p Val.
  static SimpleType getSimplifiedValue(SCEVUse &Val) {
    return Val.getPointer();
  }
};

/// Provide CastInfo for SCEVUseT so that cast<SCEVUseT<const To *>>(use)
/// returns SCEVUseT<const To *> with flags preserved.
template <typename ToSCEVPtrT>
struct CastInfo<SCEVUseT<ToSCEVPtrT>, SCEVUse,
                std::enable_if_t<!is_simple_type<SCEVUse>::value>> {
  /// Unqualified SCEV subclass being cast to.
  using To = std::remove_cv_t<std::remove_pointer_t<ToSCEVPtrT>>;
  /// Resulting SCEVUse type after a successful cast.
  using CastReturnType = SCEVUseT<ToSCEVPtrT>;

  /// Return true if \p U can be cast to \c To.
  ///
  /// \param U SCEVUse to test.
  /// @return True if \p U can be cast to \c To.
  static bool isPossible(const SCEVUse &U) { return isa<To>(U.getPointer()); }
  /// Cast \p U to \c To, preserving use flags.
  ///
  /// \param U SCEVUse to cast.
  /// @return Result of casting \p U to \c To, preserving use flags.
  static CastReturnType doCast(const SCEVUse &U) {
    return CastReturnType(cast<To>(U.getPointer()), U.getUseNoWrapFlags());
  }
  /// Return a null SCEVUse when a cast fails.
  /// @return A null SCEVUse when a cast fails.
  static CastReturnType castFailed() { return CastReturnType(nullptr); }
  /// Cast \p U to \c To if possible, else return a null SCEVUse.
  ///
  /// \param U SCEVUse to cast.
  /// @return \p U cast to \c To if possible; otherwise a null SCEVUse.
  static CastReturnType doCastIfPossible(const SCEVUse &U) {
    if (!isPossible(U))
      return castFailed();
    return doCast(U);
  }
};

/// CastInfo specialization for const SCEVUse, forwarding to the non-const case.
template <typename ToSCEVPtrT>
struct CastInfo<SCEVUseT<ToSCEVPtrT>, const SCEVUse,
                std::enable_if_t<!is_simple_type<const SCEVUse>::value>>
    : CastInfo<SCEVUseT<ToSCEVPtrT>, SCEVUse> {};

/// This class represents an analyzed expression in the program.  These are
/// opaque objects that the client is not allowed to do much with directly.
///
class SCEV : public FoldingSetNode {
  friend struct FoldingSetTrait<SCEV>;

  /// A reference to an Interned FoldingSetNodeID for this node.  The
  /// ScalarEvolution's BumpPtrAllocator holds the data.
  FoldingSetNodeIDRef FastID;

  // The SCEV baseclass this node corresponds to
  const SCEVTypes SCEVType;

protected:
  /// Estimated complexity of this node's expression tree size.
  const unsigned short ExpressionSize;

  /// This field is initialized to zero and may be used in subclasses to store
  /// miscellaneous information.
  unsigned short SubclassData = 0;

  /// Pointer to the canonical version of the SCEV, i.e. one where all operands
  /// have no SCEVUse flags.
  const SCEV *CanonicalSCEV = nullptr;

  /// Immutable type of the SCEV.
  Type *const Ty;

public:
  /// SCEV no-wrap flags; alias of SCEVNoWrapFlags.
  using NoWrapFlags = SCEVNoWrapFlags;
  /// No wrap guarantee.
  static constexpr auto FlagAnyWrap = SCEVNoWrapFlags::FlagAnyWrap;
  /// No self-wrap.
  static constexpr auto FlagNW = SCEVNoWrapFlags::FlagNW;
  /// No unsigned wrap.
  static constexpr auto FlagNUW = SCEVNoWrapFlags::FlagNUW;
  /// No signed wrap.
  static constexpr auto FlagNSW = SCEVNoWrapFlags::FlagNSW;
  /// Mask of all no-wrap flag bits.
  static constexpr auto NoWrapMask = SCEVNoWrapFlags::NoWrapMask;

  /// Construct a SCEV of kind \p SCEVTy with interned ID \p ID.
  ///
  /// \param ID Interned FoldingSet node ID.
  /// \param SCEVTy SCEV opcode.
  /// \param ExpressionSize Estimated expression-tree size.
  /// \param Ty LLVM type of the expression.
  explicit SCEV(const FoldingSetNodeIDRef ID, SCEVTypes SCEVTy,
                unsigned short ExpressionSize, Type *Ty)
      : FastID(ID), SCEVType(SCEVTy), ExpressionSize(ExpressionSize), Ty(Ty) {}
  /// Deleted copy constructor.
  ///
  /// \param Other Unused; copy construction is deleted.
  SCEV(const SCEV &Other) = delete;
  /// Deleted copy assignment.
  ///
  /// \param Other Unused; copy assignment is deleted.
  /// @return Reference to this object (deleted; not callable).
  SCEV &operator=(const SCEV &Other) = delete;

  /// Return the SCEV opcode of this expression.
  /// @return The SCEV opcode of this expression.
  SCEVTypes getSCEVType() const { return SCEVType; }

  /// Return the LLVM type of this SCEV expression.
  /// @return The LLVM type of this SCEV expression.
  Type *getType() const { return Ty; }

  /// Return operands of this SCEV expression.
  /// @return Operands of this SCEV expression.
  LLVM_ABI ArrayRef<SCEVUse> operands() const;

  /// Return true if the expression is a constant zero.
  /// @return True if the expression is a constant zero.
  LLVM_ABI bool isZero() const;

  /// Return true if the expression is a constant one.
  /// @return True if the expression is a constant one.
  LLVM_ABI bool isOne() const;

  /// Return true if the expression is a constant all-ones value.
  /// @return True if the expression is a constant all-ones value.
  LLVM_ABI bool isAllOnesValue() const;

  /// Return true if the specified scev is negated, but not a constant.
  /// @return True if the specified scev is negated, but not a constant.
  LLVM_ABI bool isNonConstantNegative() const;

  /// Return the estimated size of this SCEV's expression tree.
  ///
  /// The rules of its calculation are following:
  /// 1) Size of a SCEV without operands (like constants and SCEVUnknown) is 1;
  /// 2) Size SCEV with operands Op1, Op2, ..., OpN is calculated by formula:
  ///    (1 + Size(Op1) + ... + Size(OpN)).
  /// This value gives us an estimation of time we need to traverse through this
  /// SCEV and all its operands recursively. We may use it to avoid performing
  /// heavy transformations on SCEVs of excessive size for sake of saving the
  /// compilation time.
  /// @return The estimated size of this SCEV's expression tree.
  unsigned short getExpressionSize() const {
    return ExpressionSize;
  }

  /// Print this SCEV to \p OS.
  ///
  /// This should really only be used for debugging purposes.
  ///
  /// \param OS Stream to print to.
  LLVM_ABI void print(raw_ostream &OS) const;

  /// This method is used for debugging.
  LLVM_ABI void dump() const;

  /// Compute and set the canonical SCEV, by constructing a SCEV with the same
  /// operands, but all SCEVUse flags dropped.
  ///
  /// \param SE ScalarEvolution used to rebuild the canonical expression.
  LLVM_ABI void computeAndSetCanonical(ScalarEvolution &SE);

  /// Return the canonical SCEV.
  /// @return The canonical SCEV.
  const SCEV *getCanonical() const {
    assert(CanonicalSCEV && "canonical SCEV not yet computed");
    return CanonicalSCEV;
  }
};

/// FoldingSetTrait specialization for SCEV using interned FastID.
template <> struct FoldingSetTrait<SCEV> : DefaultFoldingSetTrait<SCEV> {
  /// Profile \p X into \p ID using its interned FastID.
  ///
  /// \param X SCEV to profile.
  /// \param ID Destination node ID.
  static void Profile(const SCEV &X, FoldingSetNodeID &ID) { ID = X.FastID; }

  /// Return true if \p X's interned ID equals \p ID.
  ///
  /// \param X SCEV to compare.
  /// \param ID Folding-set ID to match.
  /// \param IDHash Unused hash of \p ID.
  /// \param TempID Unused scratch ID.
  /// @return True if \p X's interned ID equals \p ID.
  static bool Equals(const SCEV &X, const FoldingSetNodeID &ID, unsigned IDHash,
                     FoldingSetNodeID &TempID) {
    return ID == X.FastID;
  }

  /// Return the hash of \p X's interned ID.
  ///
  /// \param X SCEV to hash.
  /// \param TempID Unused scratch ID.
  /// @return The hash of \p X's interned ID.
  static unsigned ComputeHash(const SCEV &X, FoldingSetNodeID &TempID) {
    return X.FastID.ComputeHash();
  }
};

/// Print SCEV \p S to \p OS.
///
/// \param OS Stream to print to.
/// \param S SCEV to print.
/// @return The output stream \p OS after printing \p S.
inline raw_ostream &operator<<(raw_ostream &OS, const SCEV &S) {
  S.print(OS);
  return OS;
}

/// Print SCEVUse \p U to \p OS.
///
/// \param OS Stream to print to.
/// \param U SCEVUse to print.
/// @return The output stream \p OS after printing \p U.
inline raw_ostream &operator<<(raw_ostream &OS, SCEVUse U) {
  U.print(OS);
  return OS;
}

/// Sentinel SCEV returned when a query cannot be answered.
///
/// An object of this class is returned by queries that could not be answered.
/// For example, if you ask for the number of iterations of a linked-list
/// traversal loop, you will get one of these.  None of the standard SCEV
/// operations are valid on this class, it is just a marker.
struct SCEVCouldNotCompute : public SCEV {
  /// Construct a SCEVCouldNotCompute sentinel.
  LLVM_ABI SCEVCouldNotCompute();

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  ///
  /// \param S SCEV to test.
  /// @return True if \p S is a SCEVCouldNotCompute.
  LLVM_ABI static bool classof(const SCEV *S);
};

/// This class represents an assumption made using SCEV expressions which can
/// be checked at run-time.
class SCEVPredicate : public FoldingSetNode {
  friend struct FoldingSetTrait<SCEVPredicate>;

  /// A reference to an Interned FoldingSetNodeID for this node.  The
  /// ScalarEvolution's BumpPtrAllocator holds the data.
  FoldingSetNodeIDRef FastID;

public:
  /// Kind of SCEVPredicate subclass.
  enum SCEVPredicateKind {
    P_Union,  ///< Union of predicates (logical AND).
    P_Compare, ///< Comparison predicate LHS Pred RHS.
    P_Wrap     ///< Wrap predicate on an add recurrence.
  };

protected:
  SCEVPredicateKind Kind; ///< Discriminator for the predicate subclass.
  /// Destroy this predicate.
  ~SCEVPredicate() = default;
  /// Copy-construct this predicate.
  ///
  /// \param Other Predicate to copy from.
  SCEVPredicate(const SCEVPredicate &Other) = default;
  /// Copy-assign this predicate.
  ///
  /// \param Other Predicate to assign from.
  /// @return Reference to this predicate.
  SCEVPredicate &operator=(const SCEVPredicate &Other) = default;

public:
  /// Construct a predicate of \p Kind with interned ID \p ID.
  ///
  /// \param ID Interned FoldingSet node ID.
  /// \param Kind Predicate subclass kind.
  LLVM_ABI SCEVPredicate(const FoldingSetNodeIDRef ID, SCEVPredicateKind Kind);

  /// Return the kind of this predicate.
  /// @return The kind of this predicate.
  SCEVPredicateKind getKind() const { return Kind; }

  /// Returns the estimated complexity of this predicate.  This is roughly
  /// measured in the number of run-time checks required.
  /// @return The estimated complexity of this predicate. This is roughly measured in the number of
  /// run-time checks required.
  virtual unsigned getComplexity() const { return 1; }

  /// Returns true if the predicate is always true. This means that no
  /// assumptions were made and nothing needs to be checked at run-time.
  /// @return True if the predicate is always true. This means that no assumptions were made and
  /// nothing needs to be checked at run-time.
  virtual bool isAlwaysTrue() const = 0;

  /// Returns true if this predicate implies \p N.
  ///
  /// \param N Predicate that may be implied.
  /// \param SE ScalarEvolution used for implication.
  /// @return True if this predicate implies \p N.
  virtual bool implies(const SCEVPredicate *N, ScalarEvolution &SE) const = 0;

  /// Prints a textual representation of this predicate with an indentation of
  /// \p Depth.
  ///
  /// \param OS Stream to print to.
  /// \param Depth Indentation depth.
  virtual void print(raw_ostream &OS, unsigned Depth = 0) const = 0;
};

/// Print SCEVPredicate \p P to \p OS.
///
/// \param OS Stream to print to.
/// \param P Predicate to print.
/// @return The output stream \p OS after printing \p P.
inline raw_ostream &operator<<(raw_ostream &OS, const SCEVPredicate &P) {
  P.print(OS);
  return OS;
}

/// FoldingSetTrait specialization for SCEVPredicate using interned FastID.
template <>
struct FoldingSetTrait<SCEVPredicate> : DefaultFoldingSetTrait<SCEVPredicate> {
  /// Profile \p X into \p ID using its interned FastID.
  ///
  /// \param X Predicate to profile.
  /// \param ID Destination node ID.
  static void Profile(const SCEVPredicate &X, FoldingSetNodeID &ID) {
    ID = X.FastID;
  }

  /// Return true if \p X's interned ID equals \p ID.
  ///
  /// \param X Predicate to compare.
  /// \param ID Folding-set ID to match.
  /// \param IDHash Unused hash of \p ID.
  /// \param TempID Unused scratch ID.
  /// @return True if \p X's interned ID equals \p ID.
  static bool Equals(const SCEVPredicate &X, const FoldingSetNodeID &ID,
                     unsigned IDHash, FoldingSetNodeID &TempID) {
    return ID == X.FastID;
  }

  /// Return the hash of \p X's interned ID.
  ///
  /// \param X Predicate to hash.
  /// \param TempID Unused scratch ID.
  /// @return The hash of \p X's interned ID.
  static unsigned ComputeHash(const SCEVPredicate &X,
                              FoldingSetNodeID &TempID) {
    return X.FastID.ComputeHash();
  }
};

/// This class represents an assumption that the expression LHS Pred RHS
/// evaluates to true, and this can be checked at run-time.
class LLVM_ABI SCEVComparePredicate final : public SCEVPredicate {
  /// We assume that LHS Pred RHS is true.
  const ICmpInst::Predicate Pred;
  const SCEV *LHS;
  const SCEV *RHS;

public:
  /// Construct a compare predicate that \p LHS \p Pred \p RHS holds.
  ///
  /// \param ID Interned FoldingSet node ID.
  /// \param Pred Comparison predicate.
  /// \param LHS Left-hand SCEV.
  /// \param RHS Right-hand SCEV.
  SCEVComparePredicate(const FoldingSetNodeIDRef ID,
                       const ICmpInst::Predicate Pred,
                       const SCEV *LHS, const SCEV *RHS);

  /// Return true if this predicate implies \p N.
  ///
  /// \param N Predicate that may be implied.
  /// \param SE ScalarEvolution used for implication.
  /// @return True if this predicate implies \p N.
  bool implies(const SCEVPredicate *N, ScalarEvolution &SE) const override;
  /// Print this predicate to \p OS, indented by \p Depth.
  ///
  /// \param OS Stream to print to.
  /// \param Depth Indentation depth.
  void print(raw_ostream &OS, unsigned Depth = 0) const override;
  /// Return true if this predicate is always true.
  /// @return True if this predicate is always true.
  bool isAlwaysTrue() const override;

  /// Return the icmp predicate of this comparison.
  /// @return The icmp predicate of this comparison.
  ICmpInst::Predicate getPredicate() const { return Pred; }

  /// Returns the left hand side of the predicate.
  /// @return The left hand side of the predicate.
  const SCEV *getLHS() const { return LHS; }

  /// Returns the right hand side of the predicate.
  /// @return The right hand side of the predicate.
  const SCEV *getRHS() const { return RHS; }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  ///
  /// \param P Predicate to test.
  /// @return True if \p P is a SCEVComparePredicate.
  static bool classof(const SCEVPredicate *P) {
    return P->getKind() == P_Compare;
  }
};

/// Wrap assumption on an affine add recurrence for its first X iterations.
///
/// Given an affine AddRec expression {a,+,b}, we assume that it has the nssw
/// or nusw flags (defined below) in the first X iterations of the loop, where
/// X is a SCEV expression returned by getPredicatedBackedgeTakenCount).
///
/// Note that this does not imply that X is equal to the backedge taken
/// count. This means that if we have a nusw predicate for i32 {0,+,1} with a
/// predicated backedge taken count of X, we only guarantee that {0,+,1} has
/// nusw in the first X iterations. {0,+,1} may still wrap in the loop if we
/// have more than X iterations.
class LLVM_ABI SCEVWrapPredicate final : public SCEVPredicate {
public:
  /// No-wrap flags for an add-recurrence increment.
  ///
  /// Similar to SCEV::NoWrapFlags, but with slightly different semantics
  /// for FlagNUSW. The increment is considered to be signed, and a + b
  /// (where b is the increment) is considered to wrap if:
  ///    zext(a + b) != zext(a) + sext(b)
  ///
  /// If Signed is a function that takes an n-bit tuple and maps to the
  /// integer domain as the tuples value interpreted as twos complement,
  /// and Unsigned a function that takes an n-bit tuple and maps to the
  /// integer domain as the base two value of input tuple, then a + b
  /// has IncrementNUSW iff:
  ///
  /// 0 <= Unsigned(a) + Signed(b) < 2^n
  ///
  /// The IncrementNSSW flag has identical semantics with SCEV::FlagNSW.
  ///
  /// Note that the IncrementNUSW flag is not commutative: if base + inc
  /// has IncrementNUSW, then inc + base doesn't neccessarily have this
  /// property. The reason for this is that this is used for sign/zero
  /// extending affine AddRec SCEV expressions when a SCEVWrapPredicate is
  /// assumed. A {base,+,inc} expression is already non-commutative with
  /// regards to base and inc, since it is interpreted as:
  ///     (((base + inc) + inc) + inc) ...
  enum IncrementWrapFlags {
    IncrementAnyWrap = 0,     ///< No wrap guarantee.
    IncrementNUSW = (1 << 0), ///< No unsigned wrap with signed increment.
    IncrementNSSW = (1 << 1), ///< No signed wrap with signed increment.
    IncrementNoWrapMask = (1 << 2) - 1 ///< Mask of increment wrap flags.
  };

  /// Return \p Flags with \p OffFlags cleared.
  ///
  /// \param Flags Existing flags.
  /// \param OffFlags Flags to clear.
  /// @return \p Flags with \p OffFlags cleared.
  [[nodiscard]] static SCEVWrapPredicate::IncrementWrapFlags
  clearFlags(SCEVWrapPredicate::IncrementWrapFlags Flags,
             SCEVWrapPredicate::IncrementWrapFlags OffFlags) {
    assert((Flags & IncrementNoWrapMask) == Flags && "Invalid flags value!");
    assert((OffFlags & IncrementNoWrapMask) == OffFlags &&
           "Invalid flags value!");
    return (SCEVWrapPredicate::IncrementWrapFlags)(Flags & ~OffFlags);
  }

  /// Return the bits of \p Flags selected by \p Mask.
  ///
  /// \param Flags Flags to mask.
  /// \param Mask Bits to keep.
  /// @return The bits of \p Flags selected by \p Mask.
  [[nodiscard]] static SCEVWrapPredicate::IncrementWrapFlags
  maskFlags(SCEVWrapPredicate::IncrementWrapFlags Flags, int Mask) {
    assert((Flags & IncrementNoWrapMask) == Flags && "Invalid flags value!");
    assert((Mask & IncrementNoWrapMask) == Mask && "Invalid mask value!");

    return (SCEVWrapPredicate::IncrementWrapFlags)(Flags & Mask);
  }

  /// Return \p Flags with \p OnFlags set.
  ///
  /// \param Flags Existing flags.
  /// \param OnFlags Flags to set.
  /// @return \p Flags with \p OnFlags set.
  [[nodiscard]] static SCEVWrapPredicate::IncrementWrapFlags
  setFlags(SCEVWrapPredicate::IncrementWrapFlags Flags,
           SCEVWrapPredicate::IncrementWrapFlags OnFlags) {
    assert((Flags & IncrementNoWrapMask) == Flags && "Invalid flags value!");
    assert((OnFlags & IncrementNoWrapMask) == OnFlags &&
           "Invalid flags value!");

    return (SCEVWrapPredicate::IncrementWrapFlags)(Flags | OnFlags);
  }

  /// Returns the set of SCEVWrapPredicate no wrap flags implied by a
  /// SCEVAddRecExpr.
  ///
  /// \param AR Add recurrence to inspect.
  /// \param SE ScalarEvolution used to prove flags.
  /// @return The set of SCEVWrapPredicate no wrap flags implied by a SCEVAddRecExpr.
  [[nodiscard]] static SCEVWrapPredicate::IncrementWrapFlags
  getImpliedFlags(const SCEVAddRecExpr *AR, ScalarEvolution &SE);

private:
  const SCEVAddRecExpr *AR;
  IncrementWrapFlags Flags;

public:
  /// Construct a wrap predicate for add recurrence \p AR with \p Flags.
  ///
  /// \param ID Interned FoldingSet node ID.
  /// \param AR Add recurrence being constrained.
  /// \param Flags Assumed increment wrap flags.
  explicit SCEVWrapPredicate(const FoldingSetNodeIDRef ID,
                             const SCEVAddRecExpr *AR,
                             IncrementWrapFlags Flags);

  /// Returns the set assumed no overflow flags.
  /// @return The set assumed no overflow flags.
  IncrementWrapFlags getFlags() const { return Flags; }

  /// Return the add recurrence this predicate constrains.
  /// @return The add recurrence this predicate constrains.
  const SCEVAddRecExpr *getExpr() const;
  /// Return true if this predicate implies \p N.
  ///
  /// \param N Predicate that may be implied.
  /// \param SE ScalarEvolution used for implication.
  /// @return True if this predicate implies \p N.
  bool implies(const SCEVPredicate *N, ScalarEvolution &SE) const override;
  /// Print this predicate to \p OS, indented by \p Depth.
  ///
  /// \param OS Stream to print to.
  /// \param Depth Indentation depth.
  void print(raw_ostream &OS, unsigned Depth = 0) const override;
  /// Return true if this predicate is always true.
  /// @return True if this predicate is always true.
  bool isAlwaysTrue() const override;

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  ///
  /// \param P Predicate to test.
  /// @return True if \p P is a SCEVWrapPredicate.
  static bool classof(const SCEVPredicate *P) {
    return P->getKind() == P_Wrap;
  }
};

/// Logical AND of a set of SCEV predicates.
///
/// This is equivalent to a logical "AND" of all the predicates in the union.
/// This is the class that most clients will interact with.
///
/// NB! Unlike other SCEVPredicate sub-classes this class does not live in the
/// ScalarEvolution::Preds folding set.  This is why the \c add function is sound.
class LLVM_ABI SCEVUnionPredicate final : public SCEVPredicate {
private:
  using PredicateMap =
      DenseMap<const SCEV *, SmallVector<const SCEVPredicate *, 4>>;

  /// Vector with references to all predicates in this union.
  SmallVector<const SCEVPredicate *, 16> Preds;

  /// Adds a predicate to this union.
  void add(const SCEVPredicate *N, ScalarEvolution &SE);

public:
  /// Construct a union of \p Preds using \p SE.
  ///
  /// \param Preds Predicates to combine.
  /// \param SE ScalarEvolution used to intern predicates.
  SCEVUnionPredicate(ArrayRef<const SCEVPredicate *> Preds,
                     ScalarEvolution &SE);

  /// Return the predicates in this union.
  /// @return The predicates in this union.
  ArrayRef<const SCEVPredicate *> getPredicates() const { return Preds; }

  /// Returns a new SCEVUnionPredicate that is the union of this predicate
  /// and the given predicate \p N.
  ///
  /// \param N Predicate to add.
  /// \param SE ScalarEvolution used to intern predicates.
  /// @return A new SCEVUnionPredicate that is the union of this predicate and the given predicate
  /// \p N.
  SCEVUnionPredicate getUnionWith(const SCEVPredicate *N,
                                  ScalarEvolution &SE) const {
    SCEVUnionPredicate Result(Preds, SE);
    Result.add(N, SE);
    return Result;
  }

  /// Return true if every predicate in this union is always true.
  /// @return True if every predicate in this union is always true.
  bool isAlwaysTrue() const override;
  /// Return true if this union implies \p N.
  ///
  /// \param N Predicate that may be implied.
  /// \param SE ScalarEvolution used for implication.
  /// @return True if this union implies \p N.
  bool implies(const SCEVPredicate *N, ScalarEvolution &SE) const override;
  /// Print this predicate to \p OS, indented by \p Depth.
  ///
  /// \param OS Stream to print to.
  /// \param Depth Indentation depth.
  void print(raw_ostream &OS, unsigned Depth) const override;

  /// We estimate the complexity of a union predicate as the size number of
  /// predicates in the union.
  /// @return We estimate the complexity of a union predicate as the size number of predicates in
  /// the union.
  unsigned getComplexity() const override { return Preds.size(); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  ///
  /// \param P Predicate to test.
  /// @return True if \p P is a SCEVUnionPredicate.
  static bool classof(const SCEVPredicate *P) {
    return P->getKind() == P_Union;
  }
};

/// The main scalar evolution driver. Because client code (intentionally)
/// can't do much with the SCEV objects directly, they must ask this class
/// for services.
class ScalarEvolution {
  /// Test helper that needs access to ScalarEvolution internals.
  friend class ScalarEvolutionsTest;

public:
  /// An enum describing the relationship between a SCEV and a loop.
  enum LoopDisposition {
    LoopVariant,   ///< The SCEV is loop-variant (unknown).
    LoopInvariant, ///< The SCEV is loop-invariant.
    LoopUniform,   ///< The SCEV is loop-uniform.
    LoopComputable ///< The SCEV varies predictably with the loop.
  };

  /// An enum describing the relationship between a SCEV and a basic block.
  enum BlockDisposition {
    DoesNotDominateBlock,  ///< The SCEV does not dominate the block.
    DominatesBlock,        ///< The SCEV dominates the block.
    ProperlyDominatesBlock ///< The SCEV properly dominates the block.
  };

  /// Return the bits of \p Flags selected by \p Mask.
  ///
  /// Convenient NoWrapFlags manipulation. TODO: Replace with & operator of
  /// enum class.
  ///
  /// \param Flags Flags to mask.
  /// \param Mask Bits to keep.
  /// @return The bits of \p Flags selected by \p Mask.
  [[nodiscard]] static SCEV::NoWrapFlags maskFlags(SCEV::NoWrapFlags Flags,
                                                   SCEV::NoWrapFlags Mask) {
    return Flags & Mask;
  }
  /// Return \p Flags with \p OnFlags set.
  ///
  /// \param Flags Existing flags.
  /// \param OnFlags Flags to set.
  /// @return \p Flags with \p OnFlags set.
  [[nodiscard]] static SCEV::NoWrapFlags setFlags(SCEV::NoWrapFlags Flags,
                                                  SCEV::NoWrapFlags OnFlags) {
    return Flags | OnFlags;
  }
  /// Return \p Flags with \p OffFlags cleared.
  ///
  /// \param Flags Existing flags.
  /// \param OffFlags Flags to clear.
  /// @return \p Flags with \p OffFlags cleared.
  [[nodiscard]] static SCEV::NoWrapFlags
  clearFlags(SCEV::NoWrapFlags Flags, SCEV::NoWrapFlags OffFlags) {
    return Flags & ~OffFlags;
  }
  /// Return true if \p Flags contains all of \p TestFlags.
  ///
  /// \param Flags Flags to test.
  /// \param TestFlags Flags that must be present.
  /// @return True if \p Flags contains all of \p TestFlags.
  [[nodiscard]] static bool hasFlags(SCEV::NoWrapFlags Flags,
                                     SCEV::NoWrapFlags TestFlags) {
    return TestFlags == maskFlags(Flags, TestFlags);
  };

  /// Construct ScalarEvolution for function \p F.
  ///
  /// \param F Function to analyze.
  /// \param TLI Target library info.
  /// \param AC Assumption cache.
  /// \param DT Dominator tree.
  /// \param LI Loop info.
  LLVM_ABI ScalarEvolution(Function &F, TargetLibraryInfo &TLI,
                           AssumptionCache &AC, DominatorTree &DT,
                           LoopInfo &LI);
  /// Move-construct from \p Arg.
  ///
  /// \param Arg ScalarEvolution to move from.
  LLVM_ABI ScalarEvolution(ScalarEvolution &&Arg);
  /// Destroy this ScalarEvolution instance.
  LLVM_ABI ~ScalarEvolution();

  /// Return the LLVMContext of the analyzed function.
  /// @return The LLVMContext of the analyzed function.
  LLVMContext &getContext() const { return F.getContext(); }

  /// Return true if values of type \p Ty are analyzable by SCEV.
  ///
  /// This primarily includes integer types, and it can optionally
  /// include pointer types if the ScalarEvolution class has access to
  /// target-specific information.
  ///
  /// \param Ty Type to test.
  /// @return True if values of type \p Ty are analyzable by SCEV.
  LLVM_ABI bool isSCEVable(Type *Ty) const;

  /// Return the size in bits of the specified type, for which isSCEVable must
  /// return true.
  ///
  /// \param Ty SCEVable type.
  /// @return The size in bits of the specified type, for which isSCEVable must return true.
  LLVM_ABI uint64_t getTypeSizeInBits(Type *Ty) const;

  /// Return the integer type SCEV uses internally for \p Ty.
  ///
  /// Return a type with the same bitwidth as the given type and which
  /// represents how SCEV will treat the given type, for which isSCEVable must
  /// return true. For pointer types, this is the pointer-sized integer type.
  ///
  /// \param Ty SCEVable type.
  /// @return The integer type SCEV uses internally for \p Ty.
  LLVM_ABI Type *getEffectiveSCEVType(Type *Ty) const;

  /// Return the wider of \p Ty1 and \p Ty2.
  ///
  /// \param Ty1 First type.
  /// \param Ty2 Second type.
  /// @return The wider of \p Ty1 and \p Ty2.
  LLVM_ABI Type *getWiderType(Type *Ty1, Type *Ty2) const;

  /// Return true if \p A and \p B could be operands of one instruction.
  ///
  /// SCEV expressions are generally assumed to correspond to instructions
  /// which could exists in IR.  In general, this requires that there exists
  /// a use point in the program where all operands dominate the use.
  ///
  /// Example:
  /// loop {
  ///   if
  ///     loop { v1 = load @global1; }
  ///   else
  ///     loop { v2 = load @global2; }
  /// }
  /// No SCEV with operand V1, and v2 can exist in this program.
  ///
  /// \param A First operand SCEV.
  /// \param B Second operand SCEV.
  /// @return True if \p A and \p B could be operands of one instruction.
  LLVM_ABI bool instructionCouldExistWithOperands(const SCEV *A, const SCEV *B);

  /// Return true if the SCEV is a scAddRecExpr or it contains
  /// scAddRecExpr. The result will be cached in HasRecMap.
  ///
  /// \param S SCEV to search.
  /// @return True if the SCEV is a scAddRecExpr or it contains scAddRecExpr. The result will be
  /// cached in HasRecMap.
  LLVM_ABI bool containsAddRecurrence(const SCEV *S);

  /// Return true if \p BinOp of \p LHS and \p RHS cannot overflow.
  ///
  /// If \p CtxI is specified, the no-overflow fact should be true in the
  /// context of this instruction.
  ///
  /// \param BinOp Binary opcode.
  /// \param Signed True to check signed overflow, false for unsigned.
  /// \param LHS Left-hand SCEV.
  /// \param RHS Right-hand SCEV.
  /// \param CtxI Optional context instruction.
  /// @return True if \p BinOp of \p LHS and \p RHS cannot overflow.
  LLVM_ABI bool willNotOverflow(Instruction::BinaryOps BinOp, bool Signed,
                                const SCEV *LHS, const SCEV *RHS,
                                const Instruction *CtxI = nullptr);

  /// Strengthen NSW/NUW flags on overflowing binary operator \p OBO.
  ///
  /// Parse NSW/NUW flags from add/sub/mul IR binary operation \p Op into
  /// SCEV no-wrap flags, and deduce flag[s] that aren't known yet.
  /// Does not mutate the original instruction. Returns std::nullopt if it could
  /// not deduce more precise flags than the instruction already has, otherwise
  /// returns proven flags.
  ///
  /// \param OBO Overflowing binary operator to inspect.
  /// @return Strengthen NSW/NUW flags on overflowing binary operator \p OBO.
  LLVM_ABI std::optional<SCEV::NoWrapFlags>
  getStrengthenedNoWrapFlagsFromBinOp(const OverflowingBinaryOperator *OBO);

  /// Notify this ScalarEvolution that \p User directly uses SCEVs in \p Ops.
  ///
  /// \param User SCEV that uses \p Ops.
  /// \param Ops Operand SCEVs.
  LLVM_ABI void registerUser(const SCEV *User, ArrayRef<const SCEV *> Ops);
  /// Notify this ScalarEvolution that \p User directly uses SCEVUses in \p
  /// Ops.
  ///
  /// \param User SCEV that uses \p Ops.
  /// \param Ops Operand SCEVUses.
  LLVM_ABI void registerUser(const SCEV *User, ArrayRef<SCEVUse> Ops);

  /// Return true if the SCEV expression contains an undef value.
  ///
  /// \param S SCEV to search.
  /// @return True if the SCEV expression contains an undef value.
  LLVM_ABI bool containsUndefs(const SCEV *S) const;

  /// Return true if the SCEV expression contains a Value that has been
  /// optimised out and is now a nullptr.
  ///
  /// \param S SCEV to search.
  /// @return True if the SCEV expression contains a Value that has been optimised out and is now a
  /// nullptr.
  LLVM_ABI bool containsErasedValue(const SCEV *S) const;

  /// Return a SCEV expression for the full generality of the specified
  /// expression.
  ///
  /// \param V Value whose SCEV to compute.
  /// @return A SCEV expression for the full generality of the specified expression.
  LLVM_ABI const SCEV *getSCEV(Value *V);

  /// Return an existing SCEV for V if there is one, otherwise return nullptr.
  ///
  /// \param V Value whose cached SCEV to look up.
  /// @return An existing SCEV for V if there is one, otherwise return nullptr.
  LLVM_ABI const SCEV *getExistingSCEV(Value *V);

  /// Return a SCEVConstant for integer constant \p V.
  ///
  /// \param V Integer constant.
  /// @return A SCEVConstant for integer constant \p V.
  LLVM_ABI const SCEV *getConstant(ConstantInt *V);
  /// Return a SCEVConstant for APInt \p Val.
  ///
  /// \param Val Integer value.
  /// @return A SCEVConstant for APInt \p Val.
  LLVM_ABI const SCEV *getConstant(const APInt &Val);
  /// Return a SCEVConstant of type \p Ty with value \p V.
  ///
  /// \param Ty Type of the constant.
  /// \param V Integer value.
  /// \param isSigned True if \p V is signed.
  /// @return A SCEVConstant of type \p Ty with value \p V.
  LLVM_ABI const SCEV *getConstant(Type *Ty, uint64_t V, bool isSigned = false);

  /// Return a SCEV for a ptrtoaddr of \p Op.
  ///
  /// \param Op Pointer SCEV.
  /// @return A SCEV for a ptrtoaddr of \p Op.
  LLVM_ABI const SCEV *getPtrToAddrExpr(const SCEV *Op);
  /// Return a SCEV that truncates \p Op to \p Ty.
  ///
  /// \param Op SCEV to truncate.
  /// \param Ty Destination type.
  /// \param Depth Recursion depth.
  /// @return A SCEV that truncates \p Op to \p Ty.
  LLVM_ABI const SCEV *getTruncateExpr(SCEVUse Op, Type *Ty,
                                       unsigned Depth = 0);
  /// Return a SCEV for vscale of type \p Ty.
  ///
  /// \param Ty Type of the vscale expression.
  /// @return A SCEV for vscale of type \p Ty.
  LLVM_ABI const SCEV *getVScale(Type *Ty);
  /// Return a SCEV for element count \p EC of type \p Ty.
  ///
  /// \param Ty Type of the result.
  /// \param EC Element count.
  /// \param Flags Optional no-wrap flags.
  /// @return A SCEV for element count \p EC of type \p Ty.
  LLVM_ABI const SCEV *
  getElementCount(Type *Ty, ElementCount EC,
                  SCEV::NoWrapFlags Flags = SCEV::FlagAnyWrap);
  /// Return a SCEV that zero-extends \p Op to \p Ty.
  ///
  /// \param Op SCEV to extend.
  /// \param Ty Destination type.
  /// \param Depth Recursion depth.
  /// @return A SCEV that zero-extends \p Op to \p Ty.
  LLVM_ABI const SCEV *getZeroExtendExpr(SCEVUse Op, Type *Ty,
                                         unsigned Depth = 0);
  /// Implementation of getZeroExtendExpr.
  ///
  /// \param Op SCEV to extend.
  /// \param Ty Destination type.
  /// \param Depth Recursion depth.
  /// @return Implementation of getZeroExtendExpr.
  LLVM_ABI const SCEV *getZeroExtendExprImpl(SCEVUse Op, Type *Ty,
                                             unsigned Depth = 0);
  /// Return a SCEV that sign-extends \p Op to \p Ty.
  ///
  /// \param Op SCEV to extend.
  /// \param Ty Destination type.
  /// \param Depth Recursion depth.
  /// @return A SCEV that sign-extends \p Op to \p Ty.
  LLVM_ABI const SCEV *getSignExtendExpr(SCEVUse Op, Type *Ty,
                                         unsigned Depth = 0);
  /// Implementation of getSignExtendExpr.
  ///
  /// \param Op SCEV to extend.
  /// \param Ty Destination type.
  /// \param Depth Recursion depth.
  /// @return Implementation of getSignExtendExpr.
  LLVM_ABI const SCEV *getSignExtendExprImpl(SCEVUse Op, Type *Ty,
                                             unsigned Depth = 0);
  /// Return a SCEV cast of \p Kind from \p Op to \p Ty.
  ///
  /// \param Kind Cast SCEV opcode.
  /// \param Op SCEV to cast.
  /// \param Ty Destination type.
  /// @return A SCEV cast of \p Kind from \p Op to \p Ty.
  LLVM_ABI const SCEV *getCastExpr(SCEVTypes Kind, SCEVUse Op, Type *Ty);
  /// Return a SCEV that any-extends \p Op to \p Ty.
  ///
  /// \param Op SCEV to extend.
  /// \param Ty Destination type.
  /// @return A SCEV that any-extends \p Op to \p Ty.
  LLVM_ABI const SCEV *getAnyExtendExpr(SCEVUse Op, Type *Ty);

  /// Return a SCEV for the sum of \p Ops.
  ///
  /// \param Ops Add operands.
  /// \param Flags Optional no-wrap flags.
  /// \param Depth Recursion depth.
  /// @return A SCEV for the sum of \p Ops.
  LLVM_ABI const SCEV *getAddExpr(SmallVectorImpl<SCEVUse> &Ops,
                                  SCEV::NoWrapFlags Flags = SCEV::FlagAnyWrap,
                                  unsigned Depth = 0);
  /// Return a SCEV for \p LHS + \p RHS.
  ///
  /// \param LHS Left-hand addend.
  /// \param RHS Right-hand addend.
  /// \param Flags Optional no-wrap flags.
  /// \param Depth Recursion depth.
  /// @return A SCEV for \p LHS + \p RHS.
  const SCEV *getAddExpr(SCEVUse LHS, SCEVUse RHS,
                         SCEV::NoWrapFlags Flags = SCEV::FlagAnyWrap,
                         unsigned Depth = 0) {
    SmallVector<SCEVUse, 2> Ops = {LHS, RHS};
    return getAddExpr(Ops, Flags, Depth);
  }
  /// Return a SCEV for \p Op0 + \p Op1 + \p Op2.
  ///
  /// \param Op0 First addend.
  /// \param Op1 Second addend.
  /// \param Op2 Third addend.
  /// \param Flags Optional no-wrap flags.
  /// \param Depth Recursion depth.
  /// @return A SCEV for \p Op0 + \p Op1 + \p Op2.
  const SCEV *getAddExpr(SCEVUse Op0, SCEVUse Op1, SCEVUse Op2,
                         SCEV::NoWrapFlags Flags = SCEV::FlagAnyWrap,
                         unsigned Depth = 0) {
    SmallVector<SCEVUse, 3> Ops = {Op0, Op1, Op2};
    return getAddExpr(Ops, Flags, Depth);
  }
  /// Return a SCEV for the product of \p Ops.
  ///
  /// \param Ops Multiply operands.
  /// \param Flags Optional no-wrap flags.
  /// \param Depth Recursion depth.
  /// @return A SCEV for the product of \p Ops.
  LLVM_ABI const SCEV *getMulExpr(SmallVectorImpl<SCEVUse> &Ops,
                                  SCEV::NoWrapFlags Flags = SCEV::FlagAnyWrap,
                                  unsigned Depth = 0);
  /// Return a SCEV for \p LHS * \p RHS.
  ///
  /// \param LHS Left-hand factor.
  /// \param RHS Right-hand factor.
  /// \param Flags Optional no-wrap flags.
  /// \param Depth Recursion depth.
  /// @return A SCEV for \p LHS * \p RHS.
  const SCEV *getMulExpr(SCEVUse LHS, SCEVUse RHS,
                         SCEV::NoWrapFlags Flags = SCEV::FlagAnyWrap,
                         unsigned Depth = 0) {
    SmallVector<SCEVUse, 2> Ops = {LHS, RHS};
    return getMulExpr(Ops, Flags, Depth);
  }
  /// Return a SCEV for \p Op0 * \p Op1 * \p Op2.
  ///
  /// \param Op0 First factor.
  /// \param Op1 Second factor.
  /// \param Op2 Third factor.
  /// \param Flags Optional no-wrap flags.
  /// \param Depth Recursion depth.
  /// @return A SCEV for \p Op0 * \p Op1 * \p Op2.
  const SCEV *getMulExpr(SCEVUse Op0, SCEVUse Op1, SCEVUse Op2,
                         SCEV::NoWrapFlags Flags = SCEV::FlagAnyWrap,
                         unsigned Depth = 0) {
    SmallVector<SCEVUse, 3> Ops = {Op0, Op1, Op2};
    return getMulExpr(Ops, Flags, Depth);
  }
  /// Return a SCEV for unsigned \p LHS / \p RHS.
  ///
  /// \param LHS Dividend.
  /// \param RHS Divisor.
  /// @return A SCEV for unsigned \p LHS / \p RHS.
  LLVM_ABI const SCEV *getUDivExpr(SCEVUse LHS, SCEVUse RHS);
  /// Return a SCEV for exact unsigned \p LHS / \p RHS.
  ///
  /// \param LHS Dividend.
  /// \param RHS Divisor.
  /// @return A SCEV for exact unsigned \p LHS / \p RHS.
  LLVM_ABI const SCEV *getUDivExactExpr(SCEVUse LHS, SCEVUse RHS);
  /// Return a SCEV for unsigned \p LHS % \p RHS.
  ///
  /// \param LHS Dividend.
  /// \param RHS Divisor.
  /// @return A SCEV for unsigned \p LHS % \p RHS.
  LLVM_ABI const SCEV *getURemExpr(SCEVUse LHS, SCEVUse RHS);
  /// Return an add recurrence SCEV {\p Start,+, \p Step}<\p L>.
  ///
  /// \param Start Recurrence start.
  /// \param Step Recurrence step.
  /// \param L Loop of the add recurrence.
  /// \param Flags Optional no-wrap flags.
  /// @return An add recurrence SCEV {\p Start,+, \p Step}<\p L>.
  LLVM_ABI const SCEV *getAddRecExpr(SCEVUse Start, SCEVUse Step, const Loop *L,
                                     SCEV::NoWrapFlags Flags);
  /// Return an add recurrence SCEV over \p Operands in loop \p L.
  ///
  /// \param Operands Recurrence operands (start then steps).
  /// \param L Loop of the add recurrence.
  /// \param Flags Optional no-wrap flags.
  /// @return An add recurrence SCEV over \p Operands in loop \p L.
  LLVM_ABI const SCEV *getAddRecExpr(SmallVectorImpl<SCEVUse> &Operands,
                                     const Loop *L, SCEV::NoWrapFlags Flags);
  /// Return an add recurrence SCEV over \p Operands in loop \p L.
  ///
  /// \param Operands Recurrence operands (start then steps).
  /// \param L Loop of the add recurrence.
  /// \param Flags Optional no-wrap flags.
  /// @return An add recurrence SCEV over \p Operands in loop \p L.
  const SCEV *getAddRecExpr(const SmallVectorImpl<SCEVUse> &Operands,
                            const Loop *L, SCEV::NoWrapFlags Flags) {
    SmallVector<SCEVUse, 4> NewOp(Operands.begin(), Operands.end());
    return getAddRecExpr(NewOp, L, Flags);
  }

  /// Attempt to rewrite \p SymbolicPHI as an AddRecExpr under predicates.
  ///
  /// If successful return these <AddRecExpr, Predicates>;
  /// The function is intended to be called from PSCEV (the caller will decide
  /// whether to actually add the predicates and carry out the rewrites).
  ///
  /// \param SymbolicPHI Unknown SCEV representing a PHI.
  /// @return Result of attempting to rewrite \p SymbolicPHI as an AddRecExpr under predicates.
  LLVM_ABI std::optional<
      std::pair<const SCEV *, SmallVector<const SCEVPredicate *, 3>>>
  createAddRecFromPHIWithCasts(const SCEVUnknown *SymbolicPHI);

  /// Returns an expression for a GEP
  ///
  /// \p GEP The GEP. The indices contained in the GEP itself are ignored,
  /// instead we use IndexExprs.
  /// \p IndexExprs The expressions for the indices.
  ///
  /// \param GEP GEP whose base and type to use.
  /// \param IndexExprs Index SCEVs replacing those on \p GEP.
  /// @return An expression for a GEP.
  LLVM_ABI const SCEV *getGEPExpr(GEPOperator *GEP,
                                  ArrayRef<SCEVUse> IndexExprs);
  /// Return a SCEV for a GEP from \p BaseExpr and \p IndexExprs.
  ///
  /// \param BaseExpr Base pointer SCEV.
  /// \param IndexExprs Index SCEVs.
  /// \param SrcElementTy Source element type of the GEP.
  /// \param NW GEP no-wrap flags.
  /// @return A SCEV for a GEP from \p BaseExpr and \p IndexExprs.
  LLVM_ABI const SCEV *getGEPExpr(SCEVUse BaseExpr,
                                  ArrayRef<SCEVUse> IndexExprs,
                                  Type *SrcElementTy,
                                  GEPNoWrapFlags NW = GEPNoWrapFlags::none());
  /// Return a SCEV for the absolute value of \p Op.
  ///
  /// \param Op SCEV whose absolute value to compute.
  /// \param IsNSW True if the negation is nsw.
  /// @return A SCEV for the absolute value of \p Op.
  LLVM_ABI const SCEV *getAbsExpr(const SCEV *Op, bool IsNSW);
  /// Return a SCEV for a min/max of \p Kind over \p Operands.
  ///
  /// \param Kind Min/max SCEV opcode.
  /// \param Operands Operands to reduce.
  /// @return A SCEV for a min/max of \p Kind over \p Operands.
  LLVM_ABI const SCEV *getMinMaxExpr(SCEVTypes Kind,
                                     SmallVectorImpl<SCEVUse> &Operands);
  /// Return a SCEV for a sequential min/max of \p Kind over \p Operands.
  ///
  /// \param Kind Sequential min/max SCEV opcode.
  /// \param Operands Operands to reduce.
  /// @return A SCEV for a sequential min/max of \p Kind over \p Operands.
  LLVM_ABI const SCEV *
  getSequentialMinMaxExpr(SCEVTypes Kind, SmallVectorImpl<SCEVUse> &Operands);
  /// Return a SCEV for the signed maximum of \p LHS and \p RHS.
  ///
  /// \param LHS Left-hand SCEV.
  /// \param RHS Right-hand SCEV.
  /// @return A SCEV for the signed maximum of \p LHS and \p RHS.
  LLVM_ABI const SCEV *getSMaxExpr(SCEVUse LHS, SCEVUse RHS);
  /// Return a SCEV for the signed maximum of \p Operands.
  ///
  /// \param Operands Operands to reduce.
  /// @return A SCEV for the signed maximum of \p Operands.
  LLVM_ABI const SCEV *getSMaxExpr(SmallVectorImpl<SCEVUse> &Operands);
  /// Return a SCEV for the unsigned maximum of \p LHS and \p RHS.
  ///
  /// \param LHS Left-hand SCEV.
  /// \param RHS Right-hand SCEV.
  /// @return A SCEV for the unsigned maximum of \p LHS and \p RHS.
  LLVM_ABI const SCEV *getUMaxExpr(SCEVUse LHS, SCEVUse RHS);
  /// Return a SCEV for the unsigned maximum of \p Operands.
  ///
  /// \param Operands Operands to reduce.
  /// @return A SCEV for the unsigned maximum of \p Operands.
  LLVM_ABI const SCEV *getUMaxExpr(SmallVectorImpl<SCEVUse> &Operands);
  /// Return a SCEV for the signed minimum of \p LHS and \p RHS.
  ///
  /// \param LHS Left-hand SCEV.
  /// \param RHS Right-hand SCEV.
  /// @return A SCEV for the signed minimum of \p LHS and \p RHS.
  LLVM_ABI const SCEV *getSMinExpr(SCEVUse LHS, SCEVUse RHS);
  /// Return a SCEV for the signed minimum of \p Operands.
  ///
  /// \param Operands Operands to reduce.
  /// @return A SCEV for the signed minimum of \p Operands.
  LLVM_ABI const SCEV *getSMinExpr(SmallVectorImpl<SCEVUse> &Operands);
  /// Return a SCEV for the unsigned minimum of \p LHS and \p RHS.
  ///
  /// \param LHS Left-hand SCEV.
  /// \param RHS Right-hand SCEV.
  /// \param Sequential True to use sequential umin.
  /// @return A SCEV for the unsigned minimum of \p LHS and \p RHS.
  LLVM_ABI const SCEV *getUMinExpr(SCEVUse LHS, SCEVUse RHS,
                                   bool Sequential = false);
  /// Return a SCEV for the unsigned minimum of \p Operands.
  ///
  /// \param Operands Operands to reduce.
  /// \param Sequential True to use sequential umin.
  /// @return A SCEV for the unsigned minimum of \p Operands.
  LLVM_ABI const SCEV *getUMinExpr(SmallVectorImpl<SCEVUse> &Operands,
                                   bool Sequential = false);
  /// Return a SCEVUnknown for IR value \p V.
  ///
  /// \param V Value to wrap as a SCEVUnknown.
  /// @return A SCEVUnknown for IR value \p V.
  LLVM_ABI const SCEV *getUnknown(Value *V);
  /// Return the unique SCEVCouldNotCompute sentinel.
  /// @return The unique SCEVCouldNotCompute sentinel.
  LLVM_ABI const SCEV *getCouldNotCompute();

  /// Return a SCEV for the constant 0 of a specific type.
  ///
  /// \param Ty Type of the constant.
  /// @return A SCEV for the constant 0 of a specific type.
  const SCEV *getZero(Type *Ty) { return getConstant(Ty, 0); }

  /// Return a SCEV for the constant 1 of a specific type.
  ///
  /// \param Ty Type of the constant.
  /// @return A SCEV for the constant 1 of a specific type.
  const SCEV *getOne(Type *Ty) { return getConstant(Ty, 1); }

  /// Return a SCEV for the constant \p Power of two.
  ///
  /// \param Ty Type of the constant.
  /// \param Power Bit position of the single set bit.
  /// @return A SCEV for the constant \p Power of two.
  const SCEV *getPowerOfTwo(Type *Ty, unsigned Power) {
    assert(Power < getTypeSizeInBits(Ty) && "Power out of range");
    return getConstant(APInt::getOneBitSet(getTypeSizeInBits(Ty), Power));
  }

  /// Return a SCEV for the constant -1 of a specific type.
  ///
  /// \param Ty Type of the constant.
  /// @return A SCEV for the constant -1 of a specific type.
  const SCEV *getMinusOne(Type *Ty) {
    return getConstant(Ty, -1, /*isSigned=*/true);
  }

  /// Return an expression for a TypeSize.
  ///
  /// \param IntTy Integer type of the result.
  /// \param Size Type size to represent.
  /// @return An expression for a TypeSize.
  LLVM_ABI const SCEV *getSizeOfExpr(Type *IntTy, TypeSize Size);

  /// Return an expression for the alloc size of AllocTy that is type IntTy
  ///
  /// \param IntTy Integer type of the result.
  /// \param AllocTy Type whose allocation size to represent.
  /// @return An expression for the alloc size of AllocTy that is type IntTy.
  LLVM_ABI const SCEV *getSizeOfExpr(Type *IntTy, Type *AllocTy);

  /// Return an expression for the store size of StoreTy that is type IntTy
  ///
  /// \param IntTy Integer type of the result.
  /// \param StoreTy Type whose store size to represent.
  /// @return An expression for the store size of StoreTy that is type IntTy.
  LLVM_ABI const SCEV *getStoreSizeOfExpr(Type *IntTy, Type *StoreTy);

  /// Return an expression for offsetof on the given field with type IntTy
  ///
  /// \param IntTy Integer type of the result.
  /// \param STy Struct type containing the field.
  /// \param FieldNo Index of the field.
  /// @return An expression for offsetof on the given field with type IntTy.
  LLVM_ABI const SCEV *getOffsetOfExpr(Type *IntTy, StructType *STy,
                                       unsigned FieldNo);

  /// Return the SCEV object corresponding to -V.
  ///
  /// \param V SCEV to negate.
  /// \param Flags Optional no-wrap flags.
  /// @return The SCEV object corresponding to -V.
  LLVM_ABI const SCEV *
  getNegativeSCEV(const SCEV *V, SCEV::NoWrapFlags Flags = SCEV::FlagAnyWrap);

  /// Return the SCEV object corresponding to ~V.
  ///
  /// \param V SCEV to bitwise-not.
  /// @return The SCEV object corresponding to ~V.
  LLVM_ABI const SCEV *getNotSCEV(const SCEV *V);

  /// Return LHS-RHS.  Minus is represented in SCEV as A+B*-1.
  ///
  /// If the LHS and RHS are pointers which don't share a common base
  /// (according to getPointerBase()), this returns a SCEVCouldNotCompute.
  /// To compute the difference between two unrelated pointers, you can
  /// explicitly convert the arguments using getPtrToAddrExpr(), for pointer
  /// types that support it.
  ///
  /// \param LHS Left-hand SCEV.
  /// \param RHS Right-hand SCEV.
  /// \param Flags Optional no-wrap flags.
  /// \param Depth Recursion depth.
  /// @return LHS-RHS. Minus is represented in SCEV as A+B*-1.
  LLVM_ABI const SCEV *getMinusSCEV(SCEVUse LHS, SCEVUse RHS,
                                    SCEV::NoWrapFlags Flags = SCEV::FlagAnyWrap,
                                    unsigned Depth = 0);

  /// Compute ceil(N / D). N and D are treated as unsigned values.
  ///
  /// Since SCEV doesn't have native ceiling division, this generates a
  /// SCEV expression of the following form:
  ///
  /// umin(N, 1) + floor((N - umin(N, 1)) / D)
  ///
  /// A denominator of zero or poison is handled the same way as getUDivExpr().
  ///
  /// \param N Numerator.
  /// \param D Denominator.
  /// @return SCEV for ceil(N / D), treating N and D as unsigned.
  LLVM_ABI const SCEV *getUDivCeilSCEV(const SCEV *N, const SCEV *D);

  /// Return a SCEV corresponding to a conversion of the input value to the
  /// specified type.  If the type must be extended, it is zero extended.
  ///
  /// \param V SCEV to convert.
  /// \param Ty Destination type.
  /// \param Depth Recursion depth.
  /// @return A SCEV corresponding to a conversion of the input value to the specified type. If the
  /// type must be extended, it is zero extended.
  LLVM_ABI const SCEV *getTruncateOrZeroExtend(const SCEV *V, Type *Ty,
                                               unsigned Depth = 0);

  /// Return a SCEV corresponding to a conversion of the input value to the
  /// specified type.  If the type must be extended, it is sign extended.
  ///
  /// \param V SCEV to convert.
  /// \param Ty Destination type.
  /// \param Depth Recursion depth.
  /// @return A SCEV corresponding to a conversion of the input value to the specified type. If the
  /// type must be extended, it is sign extended.
  LLVM_ABI const SCEV *getTruncateOrSignExtend(const SCEV *V, Type *Ty,
                                               unsigned Depth = 0);

  /// Convert \p V to \p Ty by no-op or zero extension, never narrowing.
  ///
  /// If the type must be extended, it is zero extended.  The conversion must
  /// not be narrowing.
  ///
  /// \param V SCEV to convert.
  /// \param Ty Destination type, at least as wide as \p V.
  /// @return Result of converting \p V to \p Ty by no-op or zero extension, never narrowing.
  LLVM_ABI const SCEV *getNoopOrZeroExtend(const SCEV *V, Type *Ty);

  /// Convert \p V to \p Ty by no-op or sign extension, never narrowing.
  ///
  /// If the type must be extended, it is sign extended.  The conversion must
  /// not be narrowing.
  ///
  /// \param V SCEV to convert.
  /// \param Ty Destination type, at least as wide as \p V.
  /// @return Result of converting \p V to \p Ty by no-op or sign extension, never narrowing.
  LLVM_ABI const SCEV *getNoopOrSignExtend(const SCEV *V, Type *Ty);

  /// Convert \p V to \p Ty by no-op or any-extension, never narrowing.
  ///
  /// If the type must be extended, it is extended with unspecified bits. The
  /// conversion must not be narrowing.
  ///
  /// \param V SCEV to convert.
  /// \param Ty Destination type, at least as wide as \p V.
  /// @return Result of converting \p V to \p Ty by no-op or any-extension, never narrowing.
  LLVM_ABI const SCEV *getNoopOrAnyExtend(const SCEV *V, Type *Ty);

  /// Return a SCEV corresponding to a conversion of the input value to the
  /// specified type.  The conversion must not be widening.
  ///
  /// \param V SCEV to convert.
  /// \param Ty Destination type, no wider than \p V.
  /// @return A SCEV corresponding to a conversion of the input value to the specified type. The
  /// conversion must not be widening.
  LLVM_ABI const SCEV *getTruncateOrNoop(const SCEV *V, Type *Ty);

  /// Promote the operands to the wider of the types using zero-extension, and
  /// then perform a umax operation with them.
  ///
  /// \param LHS Left-hand SCEV.
  /// \param RHS Right-hand SCEV.
  /// @return Promote the operands to the wider of the types using zero-extension, and then perform
  /// a umax operation with them.
  LLVM_ABI const SCEV *getUMaxFromMismatchedTypes(const SCEV *LHS,
                                                  const SCEV *RHS);

  /// Promote the operands to the wider of the types using zero-extension, and
  /// then perform a umin operation with them.
  ///
  /// \param LHS Left-hand SCEV.
  /// \param RHS Right-hand SCEV.
  /// \param Sequential True to use sequential umin.
  /// @return Promote the operands to the wider of the types using zero-extension, and then perform
  /// a umin operation with them.
  LLVM_ABI const SCEV *getUMinFromMismatchedTypes(const SCEV *LHS,
                                                  const SCEV *RHS,
                                                  bool Sequential = false);

  /// Promote the operands to the wider of the types using zero-extension, and
  /// then perform a umin operation with them. N-ary function.
  ///
  /// \param Ops Operands to minimize.
  /// \param Sequential True to use sequential umin.
  /// @return Promote the operands to the wider of the types using zero-extension, and then perform
  /// a umin operation with them. N-ary function.
  LLVM_ABI const SCEV *getUMinFromMismatchedTypes(SmallVectorImpl<SCEVUse> &Ops,
                                                  bool Sequential = false);

  /// Return the pointer base of pointer-typed SCEV \p V.
  ///
  /// Transitively follow the chain of pointer-type operands until reaching a
  /// SCEV that does not have a single pointer operand. This returns a
  /// SCEVUnknown pointer for well-formed pointer-type expressions, but corner
  /// cases do exist.
  ///
  /// \param V Pointer-typed SCEV.
  /// @return The pointer base of pointer-typed SCEV \p V.
  LLVM_ABI const SCEV *getPointerBase(const SCEV *V);

  /// Compute an expression equivalent to S - getPointerBase(S).
  ///
  /// \param S Pointer-typed SCEV.
  /// @return An expression equivalent to S - getPointerBase(S).
  LLVM_ABI const SCEV *removePointerBase(const SCEV *S);

  /// Return a SCEV for \p S evaluated in the scope of loop \p L.
  ///
  /// The L value specifies a loop nest to evaluate the expression at, where
  /// null is the top-level or a specified loop is immediately inside of the
  /// loop.
  ///
  /// This method can be used to compute the exit value for a variable defined
  /// in a loop by querying what the value will hold in the parent loop.
  ///
  /// In the case that a relevant loop exit value cannot be computed, the
  /// original value V is returned.
  ///
  /// \param S SCEV to evaluate.
  /// \param L Loop scope, or null for the top-level scope.
  /// @return A SCEV for \p S evaluated in the scope of loop \p L.
  LLVM_ABI const SCEV *getSCEVAtScope(const SCEV *S, const Loop *L);

  /// This is a convenience function which does getSCEVAtScope(getSCEV(V), L).
  ///
  /// \param V Value whose SCEV to evaluate.
  /// \param L Loop scope, or null for the top-level scope.
  /// @return This is a convenience function which does getSCEVAtScope(getSCEV(V), L).
  LLVM_ABI const SCEV *getSCEVAtScope(Value *V, const Loop *L);

  /// Return true if entry to \p L is guarded by \p LHS \p Pred \p RHS.
  ///
  /// This is used to help avoid max expressions in loop trip counts, and to
  /// eliminate casts.
  ///
  /// \param L Loop whose entry to test.
  /// \param Pred Comparison predicate.
  /// \param LHS Left-hand SCEV.
  /// \param RHS Right-hand SCEV.
  /// @return True if entry to \p L is guarded by \p LHS \p Pred \p RHS.
  LLVM_ABI bool isLoopEntryGuardedByCond(const Loop *L, CmpPredicate Pred,
                                         const SCEV *LHS, const SCEV *RHS);

  /// Test whether entry to the basic block is protected by a conditional
  /// between LHS and RHS.
  ///
  /// \param BB Basic block whose entry to test.
  /// \param Pred Comparison predicate.
  /// \param LHS Left-hand SCEV.
  /// \param RHS Right-hand SCEV.
  /// @return Test whether entry to the basic block is protected by a conditional between LHS and
  /// RHS.
  LLVM_ABI bool isBasicBlockEntryGuardedByCond(const BasicBlock *BB,
                                               CmpPredicate Pred,
                                               const SCEV *LHS,
                                               const SCEV *RHS);

  /// Test whether the backedge of the loop is protected by a conditional
  /// between LHS and RHS.  This is used to eliminate casts.
  ///
  /// \param L Loop whose backedge to test.
  /// \param Pred Comparison predicate.
  /// \param LHS Left-hand SCEV.
  /// \param RHS Right-hand SCEV.
  /// @return Test whether the backedge of the loop is protected by a conditional between LHS and
  /// RHS. This is used to eliminate casts.
  LLVM_ABI bool isLoopBackedgeGuardedByCond(const Loop *L, CmpPredicate Pred,
                                            const SCEV *LHS, const SCEV *RHS);

  /// Convert exit count \p ExitCount to a trip count that cannot overflow.
  ///
  /// \param ExitCount Backedge-taken count to convert.
  /// @return Result of converting exit count \p ExitCount to a trip count that cannot overflow.
  LLVM_ABI const SCEV *getTripCountFromExitCount(const SCEV *ExitCount);

  /// Convert exit count \p ExitCount to a trip count of type \p EvalTy.
  ///
  /// A "trip count" is the number of times the header of the loop
  /// will execute if an exit is taken after the specified number of backedges
  /// have been taken.  (e.g. TripCount = ExitCount + 1).  Note that the
  /// expression can overflow if ExitCount = UINT_MAX.  If EvalTy is not wide
  /// enough to hold the result without overflow, result unsigned wraps with
  /// 2s-complement semantics.  ex: EC = 255 (i8), TC = 0 (i8)
  ///
  /// \param ExitCount Backedge-taken count to convert.
  /// \param EvalTy Type of the returned trip count.
  /// \param L Loop whose trip count is being computed.
  /// @return Result of converting exit count \p ExitCount to a trip count of type \p EvalTy.
  LLVM_ABI const SCEV *getTripCountFromExitCount(const SCEV *ExitCount,
                                                 Type *EvalTy, const Loop *L);

  /// Return \p L's exact trip count if it is a small constant, else 0.
  ///
  /// '0' is used to represent an unknown or non-constant trip count.  Note
  /// that a trip count is simply one more than the backedge taken count for
  /// the loop.
  ///
  /// \param L Loop whose trip count to compute.
  /// @return \p L's exact trip count if it is a small constant, else 0.
  LLVM_ABI unsigned getSmallConstantTripCount(const Loop *L);

  /// Return the exact trip count if \p L exits through \p ExitingBlock.
  ///
  /// '0' is used to represent an unknown or non-constant trip count.  Note
  /// that a trip count is simply one more than the backedge taken count for
  /// the same exit.
  /// This "trip count" assumes that control exits via ExitingBlock. More
  /// precisely, it is the number of times that control will reach ExitingBlock
  /// before taking the branch. For loops with multiple exits, it may not be
  /// the number times that the loop header executes if the loop exits
  /// prematurely via another branch.
  ///
  /// \param L Loop whose trip count to compute.
  /// \param ExitingBlock Exit through which the trip count is measured.
  /// @return The exact trip count if \p L exits through \p ExitingBlock.
  LLVM_ABI unsigned getSmallConstantTripCount(const Loop *L,
                                              const BasicBlock *ExitingBlock);

  /// Return a small constant upper bound on \p L's trip count, or 0.
  ///
  /// Returns 0 if the trip count is unknown, not constant or requires
  /// SCEV predicates and \p Predicates is nullptr.
  ///
  /// \param L Loop whose max trip count to compute.
  /// \param Predicates Optional predicates required for the bound.
  /// @return A small constant upper bound on \p L's trip count, or 0.
  LLVM_ABI unsigned getSmallConstantMaxTripCount(
      const Loop *L,
      SmallVectorImpl<const SCEVPredicate *> *Predicates = nullptr);

  /// Return the largest constant divisor of trip count \p ExitCount.
  ///
  /// This means that the actual trip count is always a multiple of the
  /// returned value. Returns 1 if the trip count is unknown or not guaranteed
  /// to be the multiple of a constant., Will also return 1 if the trip count
  /// is very large (>= 2^32).
  /// Note that the argument is an exit count for loop L, NOT a trip count.
  ///
  /// \param L Loop whose trip multiple to compute.
  /// \param ExitCount Exit count whose constant divisor to find.
  /// @return The largest constant divisor of trip count \p ExitCount.
  LLVM_ABI unsigned getSmallConstantTripMultiple(const Loop *L,
                                                 const SCEV *ExitCount);

  /// Return the largest constant divisor of \p L's trip count, or 1.
  ///
  /// Will return 1 if no trip count could be computed, or if a divisor could
  /// not be found.
  ///
  /// \param L Loop whose trip multiple to compute.
  /// @return The largest constant divisor of \p L's trip count, or 1.
  LLVM_ABI unsigned getSmallConstantTripMultiple(const Loop *L);

  /// Return the largest constant divisor of \p L's trip count via \p
  /// ExitingBlock.
  ///
  /// This means that the actual trip count is always a multiple of the
  /// returned value (don't forget the trip count could very well be zero as
  /// well!). As explained in the comments for getSmallConstantTripCount, this
  /// assumes that control exits the loop via ExitingBlock.
  ///
  /// \param L Loop whose trip multiple to compute.
  /// \param ExitingBlock Exit through which the trip count is measured.
  /// @return The largest constant divisor of \p L's trip count via \p ExitingBlock.
  LLVM_ABI unsigned
  getSmallConstantTripMultiple(const Loop *L, const BasicBlock *ExitingBlock);

  /// Kind of backedge-taken or exit count to compute.
  ///
  /// The terms "backedge taken count" and "exit count" are used
  /// interchangeably to refer to the number of times the backedge of a loop
  /// has executed before the loop is exited.
  enum ExitCountKind {
    /// An expression exactly describing the number of times the backedge has
    /// executed when a loop is exited.
    Exact,
    /// A constant which provides an upper bound on the exact trip count.
    ConstantMaximum,
    /// An expression which provides an upper bound on the exact trip count.
    SymbolicMaximum,
  };

  /// Return how many times \p L's backedge executes before \p ExitingBlock.
  ///
  /// If not exactly computable, return SCEVCouldNotCompute.
  /// For a single exit loop, this value is equivelent to the result of
  /// getBackedgeTakenCount.  The loop is guaranteed to exit (via *some* exit)
  /// before the backedge is executed (ExitCount + 1) times.  Note that there
  /// is no guarantee about *which* exit is taken on the exiting iteration.
  ///
  /// \param L Loop whose exit count to compute.
  /// \param ExitingBlock Exit whose taken count to compute.
  /// \param Kind Exact or maximum count kind.
  /// @return GetBackedgeTakenCount. The loop is guaranteed to exit (via *some* exit) before the
  /// backedge is executed (ExitCount + 1) times. Note that there is no guarantee about *which* exit
  /// is taken on the exiting iteration.
  LLVM_ABI const SCEV *getExitCount(const Loop *L,
                                    const BasicBlock *ExitingBlock,
                                    ExitCountKind Kind = Exact);

  /// Same as above except this uses the predicated backedge taken info and
  /// may require predicates.
  ///
  /// \param L Loop whose exit count to compute.
  /// \param ExitingBlock Exit whose taken count to compute.
  /// \param Predicates Predicates required for the answer to be correct.
  /// \param Kind Exact or maximum count kind.
  /// @return Same as above except this uses the predicated backedge taken info and may require
  /// predicates.
  LLVM_ABI const SCEV *
  getPredicatedExitCount(const Loop *L, const BasicBlock *ExitingBlock,
                         SmallVectorImpl<const SCEVPredicate *> *Predicates,
                         ExitCountKind Kind = Exact);

  /// Return \p L's backedge-taken count, or SCEVCouldNotCompute.
  ///
  /// The backedge-taken count is the number of times the loop header will be
  /// branched to from within the loop, assuming there are no abnormal exists
  /// like exception throws. This is one less than the trip count of the loop,
  /// since it doesn't count the first iteration, when the header is branched
  /// to from outside the loop.
  ///
  /// Note that it is not valid to call this method on a loop without a
  /// loop-invariant backedge-taken count (see
  /// hasLoopInvariantBackedgeTakenCount).
  ///
  /// \param L Loop whose backedge-taken count to compute.
  /// \param Kind Exact or maximum count kind.
  /// @return \p L's backedge-taken count, or SCEVCouldNotCompute.
  LLVM_ABI const SCEV *getBackedgeTakenCount(const Loop *L,
                                             ExitCountKind Kind = Exact);

  /// Return \p L's backedge-taken count, adding required predicates.
  ///
  /// Similar to getBackedgeTakenCount, except it will add a set of
  /// SCEV predicates to Predicates that are required to be true in order for
  /// the answer to be correct. Predicates can be checked with run-time
  /// checks and can be used to perform loop versioning.
  ///
  /// \param L Loop whose backedge-taken count to compute.
  /// \param Predicates Predicates required for the answer to be correct.
  /// @return \p L's backedge-taken count, adding required predicates.
  LLVM_ABI const SCEV *getPredicatedBackedgeTakenCount(
      const Loop *L, SmallVectorImpl<const SCEVPredicate *> &Predicates);

  /// Return a constant upper bound on \p L's backedge-taken count.
  ///
  /// When successful, this returns a SCEVConstant that is greater than or
  /// equal to (i.e. a "conservative over-approximation") of the value returend
  /// by getBackedgeTakenCount.  If such a value cannot be computed, it returns
  /// the SCEVCouldNotCompute object.
  ///
  /// \param L Loop whose constant-max backedge-taken count to compute.
  /// @return A constant upper bound on \p L's backedge-taken count.
  const SCEV *getConstantMaxBackedgeTakenCount(const Loop *L) {
    return getBackedgeTakenCount(L, ConstantMaximum);
  }

  /// Return a constant upper bound on \p L's backedge-taken count, with
  /// predicates.
  ///
  /// Similar to getConstantMaxBackedgeTakenCount, except it will add a set of
  /// SCEV predicates to Predicates that are required to be true in order for
  /// the answer to be correct. Predicates can be checked with run-time
  /// checks and can be used to perform loop versioning.
  ///
  /// \param L Loop whose constant-max backedge-taken count to compute.
  /// \param Predicates Predicates required for the answer to be correct.
  /// @return A constant upper bound on \p L's backedge-taken count, with predicates.
  LLVM_ABI const SCEV *getPredicatedConstantMaxBackedgeTakenCount(
      const Loop *L, SmallVectorImpl<const SCEVPredicate *> &Predicates);

  /// Return a symbolic upper bound on \p L's backedge-taken count.
  ///
  /// When successful, this returns a SCEV that is greater than or equal
  /// to (i.e. a "conservative over-approximation") of the value returend by
  /// getBackedgeTakenCount.  If such a value cannot be computed, it returns the
  /// SCEVCouldNotCompute object.
  ///
  /// \param L Loop whose symbolic-max backedge-taken count to compute.
  /// @return A symbolic upper bound on \p L's backedge-taken count.
  const SCEV *getSymbolicMaxBackedgeTakenCount(const Loop *L) {
    return getBackedgeTakenCount(L, SymbolicMaximum);
  }

  /// Return a symbolic upper bound on \p L's backedge-taken count, with
  /// predicates.
  ///
  /// Similar to getSymbolicMaxBackedgeTakenCount, except it will add a set of
  /// SCEV predicates to Predicates that are required to be true in order for
  /// the answer to be correct. Predicates can be checked with run-time
  /// checks and can be used to perform loop versioning.
  ///
  /// \param L Loop whose symbolic-max backedge-taken count to compute.
  /// \param Predicates Predicates required for the answer to be correct.
  /// @return A symbolic upper bound on \p L's backedge-taken count, with predicates.
  LLVM_ABI const SCEV *getPredicatedSymbolicMaxBackedgeTakenCount(
      const Loop *L, SmallVectorImpl<const SCEVPredicate *> &Predicates);

  /// Return true if the backedge taken count is either the value returned by
  /// getConstantMaxBackedgeTakenCount or zero.
  ///
  /// \param L Loop to query.
  /// @return True if the backedge taken count is either the value returned by
  /// getConstantMaxBackedgeTakenCount or zero.
  LLVM_ABI bool isBackedgeTakenCountMaxOrZero(const Loop *L);

  /// Return true if the specified loop has an analyzable loop-invariant
  /// backedge-taken count.
  ///
  /// \param L Loop to query.
  /// @return True if the specified loop has an analyzable loop-invariant backedge-taken count.
  LLVM_ABI bool hasLoopInvariantBackedgeTakenCount(const Loop *L);

  /// Forget all loop information held by ScalarEvolution.
  ///
  /// This method should be called by the client when it made any change that
  /// would invalidate SCEV's answers, and the client wants to remove all loop
  /// information held internally by ScalarEvolution. This is intended to be
  /// used when the alternative to forget a loop is too expensive (i.e. large
  /// loop bodies).
  LLVM_ABI void forgetAllLoops();

  /// Forget trip-count information for loop \p L.
  ///
  /// This method should be called by the client when it has changed a loop in
  /// a way that may effect ScalarEvolution's ability to compute a trip count,
  /// or if the loop is deleted.  This call is potentially expensive for large
  /// loop bodies.
  ///
  /// \param L Loop whose analysis to invalidate.
  LLVM_ABI void forgetLoop(const Loop *L);

  /// Forget the outermost loop containing \p L and its nested loops.
  ///
  /// This method invokes forgetLoop for the outermost loop of the given loop
  /// \p L, making ScalarEvolution forget about all this subtree. This needs to
  /// be done whenever we make a transform that may affect the parameters of
  /// the outer loop, such as exit counts for branches.
  ///
  /// \param L Loop whose topmost parent to forget.
  LLVM_ABI void forgetTopmostLoop(const Loop *L);

  /// Forget cached analysis for value \p V.
  ///
  /// This method should be called by the client when it has changed a value
  /// in a way that may effect its value, or which may disconnect it from a
  /// def-use chain linking it to a loop.
  ///
  /// \param V Value whose analysis to invalidate.
  LLVM_ABI void forgetValue(Value *V);

  /// Forget LCSSA phi node V of loop L to which a new predecessor was added,
  /// such that it may no longer be trivial.
  ///
  /// \param L Loop containing the LCSSA phi.
  /// \param V LCSSA phi that gained a predecessor.
  LLVM_ABI void forgetLcssaPhiWithNewPredecessor(Loop *L, PHINode *V);

  /// Called when the client has changed the disposition of values in
  /// this loop.
  ///
  /// We don't have a way to invalidate per-loop dispositions. Clear and
  /// recompute is simpler.
  LLVM_ABI void forgetLoopDispositions();

  /// Called when the client has changed the disposition of values in
  /// a loop or block.
  ///
  /// We don't have a way to invalidate per-loop/per-block dispositions. Clear
  /// and recompute is simpler.
  ///
  /// \param V Optional value whose dispositions to forget; all if null.
  LLVM_ABI void forgetBlockAndLoopDispositions(Value *V = nullptr);

  /// Determine the minimum number of trailing zero bits in \p S.
  ///
  /// It is, at the same time, the minimum number of times S is divisible by 2.
  /// For example, given {4,+,8} it returns 2. If S is guaranteed to be 0, it
  /// returns the bitwidth of S. If \p CtxI is not nullptr, return a constant
  /// multiple valid at \p CtxI.
  ///
  /// \param S SCEV whose trailing zeros to count.
  /// \param CtxI Optional context instruction.
  /// @return Determine the minimum number of trailing zero bits in \p S.
  LLVM_ABI uint32_t getMinTrailingZeros(const SCEV *S,
                                        const Instruction *CtxI = nullptr);

  /// Returns the max constant multiple of S. If \p CtxI is not nullptr, return
  /// a constant multiple valid at \p CtxI.
  ///
  /// \param S SCEV whose constant multiple to compute.
  /// \param CtxI Optional context instruction.
  /// @return The max constant multiple of S. If \p CtxI is not nullptr, return a constant multiple
  /// valid at \p CtxI.
  LLVM_ABI APInt getConstantMultiple(const SCEV *S,
                                     const Instruction *CtxI = nullptr);

  /// Return the max constant multiple of \p S, or 1 if \p S is exactly 0.
  ///
  /// \param S SCEV whose non-zero constant multiple to compute.
  /// @return The max constant multiple of \p S, or 1 if \p S is exactly 0.
  LLVM_ABI APInt getNonZeroConstantMultiple(const SCEV *S);

  /// Determine the unsigned range for a particular SCEV.
  /// NOTE: This returns a copy of the reference returned by getRangeRef.
  ///
  /// \param S SCEV whose unsigned range to compute.
  /// @return Determine the unsigned range for a particular SCEV. NOTE: This returns a copy of the
  /// reference returned by getRangeRef.
  ConstantRange getUnsignedRange(const SCEV *S) {
    if (const APInt *C = getConstantAPIntOrNull(S))
      return ConstantRange(*C);
    return getRangeRef(S, HINT_RANGE_UNSIGNED);
  }

  /// Determine the min of the unsigned range for a particular SCEV.
  ///
  /// \param S SCEV whose unsigned minimum to compute.
  /// @return Determine the min of the unsigned range for a particular SCEV.
  APInt getUnsignedRangeMin(const SCEV *S) {
    if (const APInt *C = getConstantAPIntOrNull(S))
      return *C;
    return getRangeRef(S, HINT_RANGE_UNSIGNED).getUnsignedMin();
  }

  /// Determine the max of the unsigned range for a particular SCEV.
  ///
  /// \param S SCEV whose unsigned maximum to compute.
  /// @return Determine the max of the unsigned range for a particular SCEV.
  APInt getUnsignedRangeMax(const SCEV *S) {
    if (const APInt *C = getConstantAPIntOrNull(S))
      return *C;
    return getRangeRef(S, HINT_RANGE_UNSIGNED).getUnsignedMax();
  }

  /// Determine the signed range for a particular SCEV.
  /// NOTE: This returns a copy of the reference returned by getRangeRef.
  ///
  /// \param S SCEV whose signed range to compute.
  /// @return Determine the signed range for a particular SCEV. NOTE: This returns a copy of the
  /// reference returned by getRangeRef.
  ConstantRange getSignedRange(const SCEV *S) {
    if (const APInt *C = getConstantAPIntOrNull(S))
      return ConstantRange(*C);
    return getRangeRef(S, HINT_RANGE_SIGNED);
  }

  /// Determine the min of the signed range for a particular SCEV.
  ///
  /// \param S SCEV whose signed minimum to compute.
  /// @return Determine the min of the signed range for a particular SCEV.
  APInt getSignedRangeMin(const SCEV *S) {
    if (const APInt *C = getConstantAPIntOrNull(S))
      return *C;
    return getRangeRef(S, HINT_RANGE_SIGNED).getSignedMin();
  }

  /// Determine the max of the signed range for a particular SCEV.
  ///
  /// \param S SCEV whose signed maximum to compute.
  /// @return Determine the max of the signed range for a particular SCEV.
  APInt getSignedRangeMax(const SCEV *S) {
    if (const APInt *C = getConstantAPIntOrNull(S))
      return *C;
    return getRangeRef(S, HINT_RANGE_SIGNED).getSignedMax();
  }

  /// Test if the given expression is known to be negative.
  ///
  /// \param S SCEV to test.
  /// @return Test if the given expression is known to be negative.
  LLVM_ABI bool isKnownNegative(const SCEV *S);

  /// Test if the given expression is known to be positive.
  ///
  /// \param S SCEV to test.
  /// @return Test if the given expression is known to be positive.
  LLVM_ABI bool isKnownPositive(const SCEV *S);

  /// Test if the given expression is known to be non-negative.
  ///
  /// \param S SCEV to test.
  /// @return Test if the given expression is known to be non-negative.
  LLVM_ABI bool isKnownNonNegative(const SCEV *S);

  /// Test if the given expression is known to be non-positive.
  ///
  /// \param S SCEV to test.
  /// @return Test if the given expression is known to be non-positive.
  LLVM_ABI bool isKnownNonPositive(const SCEV *S);

  /// Test if the given expression is known to be non-zero.
  ///
  /// \param S SCEV to test.
  /// @return Test if the given expression is known to be non-zero.
  LLVM_ABI bool isKnownNonZero(const SCEV *S);

  /// Returns true if \p Op is guaranteed to not be poison.
  ///
  /// \param Op SCEV to test.
  /// @return True if \p Op is guaranteed to not be poison.
  LLVM_ABI static bool isGuaranteedNotToBePoison(const SCEV *Op);

  /// Test if the given expression is known to be a power of 2.  OrNegative
  /// allows matching negative power of 2s, and OrZero allows matching 0.
  ///
  /// \param S SCEV to test.
  /// \param OrZero True to also match zero.
  /// \param OrNegative True to also match negative powers of two.
  /// @return Test if the given expression is known to be a power of 2. OrNegative allows matching
  /// negative power of 2s, and OrZero allows matching 0.
  LLVM_ABI bool isKnownToBeAPowerOfTwo(const SCEV *S, bool OrZero = false,
                                       bool OrNegative = false);

  /// Return true if \p S is a multiple of \p M.
  ///
  /// When \p S is an AddRecExpr, \p S is a multiple of \p M if \p S starts
  /// with a multiple of \p M and at every iteration step \p S only adds
  /// multiples of \p M. \p Assumptions records the runtime predicates under
  /// which \p S is a multiple of \p M.
  ///
  /// \param S SCEV to test.
  /// \param M Constant divisor.
  /// \param Assumptions Predicates under which the multiple holds.
  /// @return True if \p S is a multiple of \p M.
  LLVM_ABI bool
  isKnownMultipleOf(const SCEV *S, uint64_t M,
                    SmallVectorImpl<const SCEVPredicate *> &Assumptions);

  /// Return true if \p S1 and \p S2 are known to have the same sign.
  ///
  /// \param S1 First SCEV.
  /// \param S2 Second SCEV.
  /// @return True if \p S1 and \p S2 are known to have the same sign.
  LLVM_ABI bool haveSameSign(const SCEV *S1, const SCEV *S2);

  /// Split \p S into values at \p L's entry and after each iteration.
  ///
  /// One of them is obtained from \p S by substitution of all AddRec
  /// sub-expression related to loop \p L with initial value of that SCEV. The
  /// second is obtained from \p S by substitution of all AddRec
  /// sub-expressions related to loop \p L with post increment of this AddRec
  /// in the loop \p L. In both cases all other AddRec sub-expressions (not
  /// related to \p L) remain the same.
  /// If the \p S contains non-invariant unknown SCEV the function returns
  /// CouldNotCompute SCEV in both values of std::pair.
  /// For example, for SCEV S={0, +, 1}<L1> + {0, +, 1}<L2> and loop L=L1
  /// the function returns pair:
  /// first = {0, +, 1}<L2>
  /// second = {1, +, 1}<L1> + {0, +, 1}<L2>
  /// We can see that for the first AddRec sub-expression it was replaced with
  /// 0 (initial value) for the first element and to {1, +, 1}<L1> (post
  /// increment value) for the second one. In both cases AddRec expression
  /// related to L2 remains the same.
  ///
  /// \param L Loop whose add recurrences to split.
  /// \param S SCEV to split.
  /// @return Pair of (value at \p L entry, value after each iteration) for \p S.
  LLVM_ABI std::pair<const SCEV *, const SCEV *>
  SplitIntoInitAndPostInc(const Loop *L, const SCEV *S);

  /// Return true if \p Pred holds on every iteration via induction reasoning.
  ///
  /// We'd like to check the predicate on every iteration of the most dominated
  /// loop between loops used in LHS and RHS.
  /// To do this we use the following list of steps:
  /// 1. Collect set S all loops on which either LHS or RHS depend.
  /// 2. If S is non-empty
  /// a. Let PD be the element of S which is dominated by all other elements.
  /// b. Let E(LHS) be value of LHS on entry of PD.
  ///    To get E(LHS), we should just take LHS and replace all AddRecs that are
  ///    attached to PD on with their entry values.
  ///    Define E(RHS) in the same way.
  /// c. Let B(LHS) be value of L on backedge of PD.
  ///    To get B(LHS), we should just take LHS and replace all AddRecs that are
  ///    attached to PD on with their backedge values.
  ///    Define B(RHS) in the same way.
  /// d. Note that E(LHS) and E(RHS) are automatically available on entry of PD,
  ///    so we can assert on that.
  /// e. Return true if isLoopEntryGuardedByCond(Pred, E(LHS), E(RHS)) &&
  ///                   isLoopBackedgeGuardedByCond(Pred, B(LHS), B(RHS))
  ///
  /// \param Pred Comparison predicate.
  /// \param LHS Left-hand SCEV.
  /// \param RHS Right-hand SCEV.
  /// @return True if \p Pred holds on every iteration via induction reasoning.
  LLVM_ABI bool isKnownViaInduction(CmpPredicate Pred, SCEVUse LHS,
                                    SCEVUse RHS);

  /// Test if the given expression is known to satisfy the condition described
  /// by Pred, LHS, and RHS.
  ///
  /// \param Pred Comparison predicate.
  /// \param LHS Left-hand SCEV.
  /// \param RHS Right-hand SCEV.
  /// @return Test if the given expression is known to satisfy the condition described by Pred, LHS,
  /// and RHS.
  LLVM_ABI bool isKnownPredicate(CmpPredicate Pred, SCEVUse LHS, SCEVUse RHS);

  /// Evaluate whether \p LHS \p Pred \p RHS is known true or false.
  ///
  /// If we know it, return the evaluation of this condition. If neither
  /// is proved, return std::nullopt.
  ///
  /// \param Pred Comparison predicate.
  /// \param LHS Left-hand SCEV.
  /// \param RHS Right-hand SCEV.
  /// @return Whether \p LHS \p Pred \p RHS is known true or false, if decidable.
  LLVM_ABI std::optional<bool>
  evaluatePredicate(CmpPredicate Pred, const SCEV *LHS, const SCEV *RHS);

  /// Test if the given expression is known to satisfy the condition described
  /// by Pred, LHS, and RHS in the given Context.
  ///
  /// \param Pred Comparison predicate.
  /// \param LHS Left-hand SCEV.
  /// \param RHS Right-hand SCEV.
  /// \param CtxI Context instruction.
  /// @return Test if the given expression is known to satisfy the condition described by Pred, LHS,
  /// and RHS in the given Context.
  LLVM_ABI bool isKnownPredicateAt(CmpPredicate Pred, const SCEV *LHS,
                                   const SCEV *RHS, const Instruction *CtxI);

  /// Evaluate whether \p LHS \p Pred \p RHS is known at \p CtxI.
  ///
  /// If we know it, return the evaluation of this condition. If neither
  /// is proved, return std::nullopt.
  ///
  /// \param Pred Comparison predicate.
  /// \param LHS Left-hand SCEV.
  /// \param RHS Right-hand SCEV.
  /// \param CtxI Context instruction.
  /// @return Whether \p LHS \p Pred \p RHS is known at \p CtxI, if decidable.
  LLVM_ABI std::optional<bool> evaluatePredicateAt(CmpPredicate Pred,
                                                   const SCEV *LHS,
                                                   const SCEV *RHS,
                                                   const Instruction *CtxI);

  /// Test if the condition described by Pred, LHS, RHS is known to be true on
  /// every iteration of the loop of the recurrency LHS.
  ///
  /// \param Pred Comparison predicate.
  /// \param LHS Add recurrence on the left-hand side.
  /// \param RHS Right-hand SCEV.
  /// @return Test if the condition described by Pred, LHS, RHS is known to be true on every
  /// iteration of the loop of the recurrency LHS.
  LLVM_ABI bool isKnownOnEveryIteration(CmpPredicate Pred,
                                        const SCEVAddRecExpr *LHS,
                                        const SCEV *RHS);

  /// How many times a loop exit's not-taken path is taken.
  ///
  /// Information about the number of loop iterations for which a loop exit's
  /// branch condition evaluates to the not-taken path.  This is a temporary
  /// pair of exact and max expressions that are eventually summarized in
  /// ExitNotTakenInfo and BackedgeTakenInfo.
  struct ExitLimit {
    const SCEV *ExactNotTaken; ///< Exact not-taken count, or CouldNotCompute.
    const SCEV *ConstantMaxNotTaken; ///< Constant upper bound on not-taken.
    const SCEV *SymbolicMaxNotTaken; ///< Symbolic upper bound on not-taken.

    /// True if not-taken is either ConstantMaxNotTaken or zero.
    bool MaxOrZero = false;

    /// A vector of predicate guards for this ExitLimit. The result is only
    /// valid if all of the predicates in \c Predicates evaluate to 'true' at
    /// run-time.
    SmallVector<const SCEVPredicate *, 4> Predicates;

    /// Construct an exit limit from a constant or CouldNotCompute SCEV.
    ///
    /// No other types of SCEVs are allowed as arguments and asserts enforce
    /// that internally.
    ///
    /// \param E Exact not-taken count or SCEVCouldNotCompute.
    /*implicit*/ LLVM_ABI ExitLimit(const SCEV *E);
    /// Construct an exit limit from SCEVUse \p E.
    ///
    /// \param E Exact not-taken count or SCEVCouldNotCompute.
    /*implicit*/ ExitLimit(SCEVUse E) : ExitLimit((const SCEV *)E) {}

    /// Construct an exit limit from exact and maximum not-taken counts.
    ///
    /// \param E Exact not-taken count.
    /// \param ConstantMaxNotTaken Constant maximum not-taken count.
    /// \param SymbolicMaxNotTaken Symbolic maximum not-taken count.
    /// \param MaxOrZero True if the count is either the constant max or zero.
    /// \param PredLists Predicate lists that must all hold.
    LLVM_ABI
    ExitLimit(const SCEV *E, const SCEV *ConstantMaxNotTaken,
              const SCEV *SymbolicMaxNotTaken, bool MaxOrZero,
              ArrayRef<ArrayRef<const SCEVPredicate *>> PredLists = {});

    /// Construct an exit limit from exact and maximum not-taken counts.
    ///
    /// \param E Exact not-taken count.
    /// \param ConstantMaxNotTaken Constant maximum not-taken count.
    /// \param SymbolicMaxNotTaken Symbolic maximum not-taken count.
    /// \param MaxOrZero True if the count is either the constant max or zero.
    /// \param PredList Predicates that must hold.
    LLVM_ABI ExitLimit(const SCEV *E, const SCEV *ConstantMaxNotTaken,
                       const SCEV *SymbolicMaxNotTaken, bool MaxOrZero,
                       ArrayRef<const SCEVPredicate *> PredList);

    /// Test whether this ExitLimit contains any computed information, or
    /// whether it's all SCEVCouldNotCompute values.
    /// @return Test whether this ExitLimit contains any computed information, or whether it's all
    /// SCEVCouldNotCompute values.
    bool hasAnyInfo() const {
      return !isa<SCEVCouldNotCompute>(ExactNotTaken) ||
             !isa<SCEVCouldNotCompute>(ConstantMaxNotTaken);
    }

    /// Test whether this ExitLimit contains all information.
    /// @return Test whether this ExitLimit contains all information.
    bool hasFullInfo() const {
      return !isa<SCEVCouldNotCompute>(ExactNotTaken);
    }
  };

  /// Compute how many times \p L's backedge runs if its exit is \p ExitCond.
  ///
  /// \p ControlsOnlyExit is true if ExitCond directly controls the only exit
  /// branch. In this case, we can assume that the loop exits only if the
  /// condition is true and can infer that failing to meet the condition prior
  /// to integer wraparound results in undefined behavior.
  ///
  /// If \p AllowPredicates is set, this call will try to use a minimal set of
  /// SCEV predicates in order to return an exact answer.
  ///
  /// \param L Loop whose exit limit to compute.
  /// \param ExitCond Branch condition of the exit.
  /// \param ExitIfTrue True if the loop exits when \p ExitCond is true.
  /// \param ControlsOnlyExit True if \p ExitCond is the only exit condition.
  /// \param AllowPredicates True to allow adding SCEV predicates.
  /// @return How many times \p L's backedge runs when its exit is \p ExitCond.
  LLVM_ABI ExitLimit computeExitLimitFromCond(const Loop *L, Value *ExitCond,
                                              bool ExitIfTrue,
                                              bool ControlsOnlyExit,
                                              bool AllowPredicates = false);

  /// How an icmp predicate evolves as its loop iterates.
  ///
  /// A predicate is said to be monotonically increasing if may go from being
  /// false to being true as the loop iterates, but never the other way
  /// around.  A predicate is said to be monotonically decreasing if may go
  /// from being true to being false as the loop iterates, but never the other
  /// way around.
  enum MonotonicPredicateType {
    MonotonicallyIncreasing, ///< May become true, but never become false.
    MonotonicallyDecreasing  ///< May become false, but never become true.
  };

  /// Classify whether \p LHS \p Pred X is monotonic for invariant X.
  ///
  /// If, for all loop invariant X, the predicate "LHS `Pred` X" is
  /// monotonically increasing or decreasing, returns
  /// Some(MonotonicallyIncreasing) and Some(MonotonicallyDecreasing)
  /// respectively. If we could not prove either of these facts, returns
  /// std::nullopt.
  ///
  /// \param LHS Loop add recurrence on the left-hand side.
  /// \param Pred Comparison predicate.
  /// @return Classify whether \p LHS \p Pred X is monotonic for invariant X.
  LLVM_ABI std::optional<MonotonicPredicateType>
  getMonotonicPredicateType(const SCEVAddRecExpr *LHS,
                            ICmpInst::Predicate Pred);

  /// Comparison rewritten so both sides are invariant in a loop.
  struct LoopInvariantPredicate {
    CmpPredicate Pred; ///< Comparison predicate.
    const SCEV *LHS;   ///< Loop-invariant left-hand SCEV.
    const SCEV *RHS;   ///< Loop-invariant right-hand SCEV.

    /// Construct a loop-invariant comparison \p LHS \p Pred \p RHS.
    ///
    /// \param Pred Comparison predicate.
    /// \param LHS Left-hand SCEV.
    /// \param RHS Right-hand SCEV.
    LoopInvariantPredicate(CmpPredicate Pred, const SCEV *LHS, const SCEV *RHS)
        : Pred(Pred), LHS(LHS), RHS(RHS) {}
  };
  /// Rewrite \p LHS \p Pred \p RHS to a loop-invariant form in \p L if
  /// possible.
  ///
  /// If the result of the predicate LHS `Pred` RHS is loop invariant with
  /// respect to L, return a LoopInvariantPredicate with LHS and RHS being
  /// invariants, available at L's entry. Otherwise, return std::nullopt.
  ///
  /// \param Pred Comparison predicate.
  /// \param LHS Left-hand SCEV.
  /// \param RHS Right-hand SCEV.
  /// \param L Loop with respect to which invariance is tested.
  /// \param CtxI Optional context instruction.
  /// @return Rewrite \p LHS \p Pred \p RHS to a loop-invariant form in \p L if possible.
  LLVM_ABI std::optional<LoopInvariantPredicate>
  getLoopInvariantPredicate(CmpPredicate Pred, const SCEV *LHS, const SCEV *RHS,
                            const Loop *L, const Instruction *CtxI = nullptr);

  /// Rewrite an exit condition that is invariant for the first \p MaxIter
  /// iterations of \p L.
  ///
  /// If the result of the predicate LHS `Pred` RHS is loop invariant with
  /// respect to L at given Context during at least first MaxIter iterations,
  /// return a LoopInvariantPredicate with LHS and RHS being invariants,
  /// available at L's entry. Otherwise, return std::nullopt. The predicate
  /// should be the loop's exit condition.
  ///
  /// \param Pred Comparison predicate.
  /// \param LHS Left-hand SCEV.
  /// \param RHS Right-hand SCEV.
  /// \param L Loop whose exit condition is tested.
  /// \param CtxI Context instruction.
  /// \param MaxIter Number of leading iterations that must be invariant.
  /// @return Loop-invariant exit condition for the first \p MaxIter iterations, if possible.
  LLVM_ABI std::optional<LoopInvariantPredicate>
  getLoopInvariantExitCondDuringFirstIterations(CmpPredicate Pred,
                                                const SCEV *LHS,
                                                const SCEV *RHS, const Loop *L,
                                                const Instruction *CtxI,
                                                const SCEV *MaxIter);

  /// Implementation of getLoopInvariantExitCondDuringFirstIterations.
  ///
  /// \param Pred Comparison predicate.
  /// \param LHS Left-hand SCEV.
  /// \param RHS Right-hand SCEV.
  /// \param L Loop whose exit condition is tested.
  /// \param CtxI Context instruction.
  /// \param MaxIter Number of leading iterations that must be invariant.
  /// @return Implementation of getLoopInvariantExitCondDuringFirstIterations.
  LLVM_ABI std::optional<LoopInvariantPredicate>
  getLoopInvariantExitCondDuringFirstIterationsImpl(
      CmpPredicate Pred, const SCEV *LHS, const SCEV *RHS, const Loop *L,
      const Instruction *CtxI, const SCEV *MaxIter);

  /// Simplify \p LHS and \p RHS in a comparison with predicate \p Pred.
  ///
  /// Return true iff any changes were made. If the operands are provably equal
  /// or unequal, LHS and RHS are set to the same value and Pred is set to
  /// either ICMP_EQ or ICMP_NE.
  ///
  /// \param Pred Comparison predicate; may be updated.
  /// \param LHS Left-hand SCEV; may be updated.
  /// \param RHS Right-hand SCEV; may be updated.
  /// \param Depth Recursion depth.
  /// @return True if \p LHS and/or \p RHS were simplified.
  LLVM_ABI bool SimplifyICmpOperands(CmpPredicate &Pred, SCEVUse &LHS,
                                     SCEVUse &RHS, unsigned Depth = 0);

  /// Return the "disposition" of the given SCEV with respect to the given
  /// loop.
  ///
  /// \param S SCEV whose loop disposition to compute.
  /// \param L Loop relative to which disposition is computed.
  /// @return The "disposition" of the given SCEV with respect to the given loop.
  LLVM_ABI LoopDisposition getLoopDisposition(const SCEV *S, const Loop *L);

  /// Returns true if the given SCEV is loop-uniform with respect to the
  /// specified loop L.
  ///
  /// A SCEV is considered loop-uniform if its value is invariant across all
  /// iterations of L, meaning it does not depend on any induction variables
  /// or values that vary within L.
  ///
  /// This notion is particularly useful in nested loops, where a value may vary
  /// in an inner loop but remain invariant in an outer loop.
  ///
  /// Example:
  /// \code
  ///   for (i)
  ///     for (j)
  ///       dep(j);
  ///       dep(i, j);
  /// \endcode
  /// isLoopUniform(SCEV(dep(j)), loop_i) returns true, as `j` is independent of
  /// `i`.
  /// isLoopUniform(SCEV(dep(i, j)), loop_i) returns false, as the expression
  /// depends on `i`, which varies in loop_i.
  ///
  /// \param S SCEV to test.
  /// \param L Loop in which uniformity is tested.
  /// @return True if the given SCEV is loop-uniform with respect to the specified loop L.
  LLVM_ABI bool isLoopUniform(const SCEV *S, const Loop *L);

  /// Return true if \p S is unchanging in loop \p L.
  ///
  /// \param S SCEV to test.
  /// \param L Loop in which invariance is tested.
  /// @return True if \p S is unchanging in loop \p L.
  LLVM_ABI bool isLoopInvariant(const SCEV *S, const Loop *L);

  /// Return true if \p S can be evaluated at the entry of \p L.
  ///
  /// It is true if it doesn't depend on a SCEVUnknown of an instruction which
  /// is dominated by the header of loop L.
  ///
  /// \param S SCEV to test.
  /// \param L Loop whose entry is the evaluation point.
  /// @return True if \p S can be evaluated at the entry of \p L.
  LLVM_ABI bool isAvailableAtLoopEntry(const SCEV *S, const Loop *L);

  /// Return true if \p S varies in a known way in loop \p L.
  ///
  /// This property being true implies that the value is variant in the loop
  /// AND that we can emit an expression to compute the value of the expression
  /// at any particular loop iteration.
  ///
  /// \param S SCEV to test.
  /// \param L Loop in which evolution is tested.
  /// @return True if \p S varies in a known way in loop \p L.
  LLVM_ABI bool hasComputableLoopEvolution(const SCEV *S, const Loop *L);

  /// Return the "disposition" of the given SCEV with respect to the given
  /// block.
  ///
  /// \param S SCEV whose block disposition to compute.
  /// \param BB Basic block relative to which disposition is computed.
  /// @return The "disposition" of the given SCEV with respect to the given block.
  LLVM_ABI BlockDisposition getBlockDisposition(const SCEV *S,
                                                const BasicBlock *BB);

  /// Return true if elements that makes up the given SCEV dominate the
  /// specified basic block.
  ///
  /// \param S SCEV whose defining values to test.
  /// \param BB Basic block that must be dominated.
  /// @return True if elements that makes up the given SCEV dominate the specified basic block.
  LLVM_ABI bool dominates(const SCEV *S, const BasicBlock *BB);

  /// Return true if elements that makes up the given SCEV properly dominate
  /// the specified basic block.
  ///
  /// \param S SCEV whose defining values to test.
  /// \param BB Basic block that must be properly dominated.
  /// @return True if elements that makes up the given SCEV properly dominate the specified basic
  /// block.
  LLVM_ABI bool properlyDominates(const SCEV *S, const BasicBlock *BB);

  /// Test whether the given SCEV has Op as a direct or indirect operand.
  ///
  /// \param S SCEV to search.
  /// \param Op Operand to look for.
  /// @return Test whether the given SCEV has Op as a direct or indirect operand.
  LLVM_ABI bool hasOperand(const SCEV *S, const SCEV *Op) const;

  /// Return the size of an element read or written by Inst.
  ///
  /// \param Inst Memory instruction whose element size to return.
  /// @return The size of an element read or written by Inst.
  LLVM_ABI const SCEV *getElementSize(Instruction *Inst);

  /// Print the ScalarEvolution analysis to \p OS.
  ///
  /// \param OS Stream to print to.
  LLVM_ABI void print(raw_ostream &OS) const;
  /// Verify internal ScalarEvolution data structures.
  LLVM_ABI void verify() const;
  /// Invalidate cached results when \p F or preserved analyses change.
  ///
  /// \param F Function whose analysis may be stale.
  /// \param PA Analyses preserved by the last transformation.
  /// \param Inv Invalidator for dependent analyses.
  /// @return Invalidate cached results when \p F or preserved analyses change.
  LLVM_ABI bool invalidate(Function &F, const PreservedAnalyses &PA,
                           FunctionAnalysisManager::Invalidator &Inv);

  /// Return the DataLayout associated with the module this SCEV instance is
  /// operating on.
  /// @return The DataLayout associated with the module this SCEV instance is operating on.
  const DataLayout &getDataLayout() const { return DL; }

  /// Return a predicate that \p LHS equals \p RHS.
  ///
  /// \param LHS Left-hand SCEV.
  /// \param RHS Right-hand SCEV.
  /// @return A predicate that \p LHS equals \p RHS.
  LLVM_ABI const SCEVPredicate *getEqualPredicate(const SCEV *LHS,
                                                  const SCEV *RHS);
  /// Return a predicate that \p LHS \p Pred \p RHS holds.
  ///
  /// \param Pred Comparison predicate.
  /// \param LHS Left-hand SCEV.
  /// \param RHS Right-hand SCEV.
  /// @return A predicate that \p LHS \p Pred \p RHS holds.
  LLVM_ABI const SCEVPredicate *getComparePredicate(ICmpInst::Predicate Pred,
                                                    const SCEV *LHS,
                                                    const SCEV *RHS);

  /// Return a wrap predicate for add recurrence \p AR with \p AddedFlags.
  ///
  /// \param AR Add recurrence to constrain.
  /// \param AddedFlags Wrap flags assumed to hold.
  /// @return A wrap predicate for add recurrence \p AR with \p AddedFlags.
  LLVM_ABI const SCEVPredicate *
  getWrapPredicate(const SCEVAddRecExpr *AR,
                   SCEVWrapPredicate::IncrementWrapFlags AddedFlags);

  /// Re-writes the SCEV according to the Predicates in \p A.
  ///
  /// \param S SCEV to rewrite.
  /// \param L Loop in whose context to rewrite.
  /// \param A Predicate used to rewrite \p S.
  /// @return Re-writes the SCEV according to the Predicates in \p A.
  LLVM_ABI const SCEV *rewriteUsingPredicate(const SCEV *S, const Loop *L,
                                             const SCEVPredicate &A);
  /// Tries to convert the \p S expression to an AddRec expression,
  /// adding additional predicates to \p Preds as required.
  ///
  /// \param S SCEV to convert.
  /// \param L Loop of the desired add recurrence.
  /// \param Preds Predicates assumed or collected during conversion.
  /// @return Tries to convert the \p S expression to an AddRec expression, adding additional
  /// predicates to \p Preds as required.
  LLVM_ABI const SCEVAddRecExpr *convertSCEVToAddRecWithPredicates(
      const SCEV *S, const Loop *L,
      SmallVectorImpl<const SCEVPredicate *> &Preds);

  /// Compute \p LHS - \p RHS as a constant if possible.
  ///
  /// Returns the result as an APInt if it is a constant, and std::nullopt if
  /// it isn't.
  ///
  /// This is intended to be a cheaper version of getMinusSCEV.  We can be
  /// frugal here since we just bail out of actually constructing and
  /// canonicalizing an expression in the cases where the result isn't going
  /// to be a constant.
  ///
  /// \param LHS Left-hand SCEV.
  /// \param RHS Right-hand SCEV.
  /// @return \p LHS - \p RHS as a constant, if possible.
  LLVM_ABI std::optional<APInt> computeConstantDifference(const SCEV *LHS,
                                                          const SCEV *RHS);

  /// Update no-wrap flags of \p AddRec to \p Flags.
  ///
  /// This may drop the cached info about this AddRec (such as range info) in
  /// case if new flags may potentially sharpen it.
  ///
  /// \param AddRec Add recurrence whose flags to update.
  /// \param Flags New no-wrap flags.
  LLVM_ABI void setNoWrapFlags(SCEVAddRecExpr *AddRec, SCEV::NoWrapFlags Flags);

  /// Rewrite SCEVs using conditions that guard a loop.
  class LoopGuards {
    DenseMap<const SCEV *, const SCEV *> RewriteMap;
    SmallDenseSet<std::pair<const SCEV *, const SCEV *>> NotEqual;
    bool PreserveNUW = false;
    bool PreserveNSW = false;
    ScalarEvolution &SE;

    LoopGuards(ScalarEvolution &SE) : SE(SE) {}

    /// Recursively collect loop guards in \p Guards, starting from
    /// block \p Block with predecessor \p Pred. The intended starting point
    /// is to collect from a loop header and its predecessor.
    static void
    collectFromBlock(ScalarEvolution &SE, ScalarEvolution::LoopGuards &Guards,
                     const BasicBlock *Block, const BasicBlock *Pred,
                     SmallPtrSetImpl<const BasicBlock *> &VisitedBlocks,
                     unsigned Depth = 0);

    /// Collect loop guards in \p Guards, starting from PHINode \p
    /// Phi, by calling \p collectFromBlock on the incoming blocks of
    /// \Phi and trying to merge the found constraints into a single
    /// combined one for \p Phi.
    static void collectFromPHI(
        ScalarEvolution &SE, ScalarEvolution::LoopGuards &Guards,
        const PHINode &Phi, SmallPtrSetImpl<const BasicBlock *> &VisitedBlocks,
        SmallDenseMap<const BasicBlock *, LoopGuards> &IncomingGuards,
        unsigned Depth);

  public:
    /// Collect rewrite map for loop guards for loop \p L, together with flags
    /// indicating if NUW and NSW can be preserved during rewriting.
    ///
    /// \param L Loop whose guards to collect.
    /// \param SE ScalarEvolution used to rewrite expressions.
    /// @return Collect rewrite map for loop guards for loop \p L, together with flags indicating if
    /// NUW and NSW can be preserved during rewriting.
    LLVM_ABI static LoopGuards collect(const Loop *L, ScalarEvolution &SE);

    /// Try to apply the collected loop guards to \p Expr.
    ///
    /// \param Expr SCEV to rewrite using collected guards.
    /// @return Try to apply the collected loop guards to \p Expr.
    LLVM_ABI const SCEV *rewrite(const SCEV *Expr) const;
  };

  /// Apply loop-guard information from \p L to \p Expr.
  ///
  /// \param Expr SCEV to rewrite.
  /// \param L Loop whose entry guards to apply.
  /// @return \p Expr rewritten using loop-guard information from \p L.
  LLVM_ABI const SCEV *applyLoopGuards(const SCEV *Expr, const Loop *L);
  /// Apply collected loop-guard rewrites in \p Guards to \p Expr.
  ///
  /// \param Expr SCEV to rewrite.
  /// \param Guards Loop guards previously collected for a loop.
  /// @return \p Expr rewritten using the collected loop-guard rewrites in \p Guards.
  LLVM_ABI const SCEV *applyLoopGuards(const SCEV *Expr,
                                       const LoopGuards &Guards);

  /// Return true if \p L has no abnormal exits.
  ///
  /// That is, if the loop is not infinite, it must exit through an explicit
  /// edge in the CFG. (As opposed to either a) throwing out of the function or
  /// b) entering a well defined infinite loop in some callee.)
  ///
  /// \param L Loop to query.
  /// @return True if \p L has no abnormal exits.
  bool loopHasNoAbnormalExits(const Loop *L) {
    return getLoopProperties(L).HasNoAbnormalExits;
  }

  /// Return true if \p L is finite by assumption.
  ///
  /// That is, to be infinite, it must also be undefined.
  ///
  /// \param L Loop to query.
  /// @return True if \p L is finite by assumption.
  LLVM_ABI bool loopIsFiniteByAssumption(const Loop *L);

  /// Collect values that, if poison, make \p S poison.
  ///
  /// The returned set may be incomplete, i.e. there can
  /// be additional Values that also result in S being poison.
  ///
  /// \param Result Set filled with poison-generating values.
  /// \param S SCEV whose poison sources to collect.
  LLVM_ABI void
  getPoisonGeneratingValues(SmallPtrSetImpl<const Value *> &Result,
                            const SCEV *S);

  /// Return true if \p I can represent \p S without introducing poison.
  ///
  /// If such a replacement is performed, the poison flags of
  /// instructions in DropPoisonGeneratingInsts must be dropped.
  ///
  /// \param S SCEV being materialized.
  /// \param I Instruction proposed to represent \p S.
  /// \param DropPoisonGeneratingInsts Instructions whose poison flags must
  ///        be dropped if the reuse is performed.
  /// @return True if \p I can represent \p S without introducing poison.
  LLVM_ABI bool canReuseInstruction(
      const SCEV *S, Instruction *I,
      SmallVectorImpl<Instruction *> &DropPoisonGeneratingInsts);

  /// Identity used to memoize SCEV fold results.
  class FoldID {
    const SCEV *Op = nullptr;
    const Type *Ty = nullptr;
    unsigned short C;

  public:
    /// Construct a fold identity for opcode \p C, operand \p Op, and type \p
    /// Ty.
    ///
    /// \param C SCEV opcode of the folded expression.
    /// \param Op Operand of the folded expression.
    /// \param Ty Result type of the folded expression.
    FoldID(SCEVTypes C, const SCEV *Op, const Type *Ty) : Op(Op), Ty(Ty), C(C) {
      assert(Op);
      assert(Ty);
    }

    /// Construct a fold identity from opcode \p C alone.
    ///
    /// \param C Fold opcode or kind.
    FoldID(unsigned short C) : C(C) {}

    /// Return a hash of this fold identity.
    /// @return A hash of this fold identity.
    unsigned computeHash() const {
      return detail::combineHashValue(
          C, detail::combineHashValue(reinterpret_cast<uintptr_t>(Op),
                                      reinterpret_cast<uintptr_t>(Ty)));
    }

    /// Return true if this fold identity equals \p RHS.
    ///
    /// \param RHS Other fold identity.
    /// @return True if this fold identity equals \p RHS.
    bool operator==(const FoldID &RHS) const {
      return std::tie(Op, Ty, C) == std::tie(RHS.Op, RHS.Ty, RHS.C);
    }
  };

private:
  /// A CallbackVH to arrange for ScalarEvolution to be notified whenever a
  /// Value is deleted.
  class LLVM_ABI SCEVCallbackVH final : public CallbackVH {
    ScalarEvolution *SE;

    void deleted() override;
    void allUsesReplacedWith(Value *New) override;

  public:
    SCEVCallbackVH(Value *V, ScalarEvolution *SE = nullptr);
  };

  friend class SCEVCallbackVH;
  friend class SCEVExpander;
  friend class SCEVUnknown;
  friend class VPSCEVExpander;
  // Needs getWithOperands to rebuild a node from its canonical operands.
  friend void SCEV::computeAndSetCanonical(ScalarEvolution &SE);

  /// The function we are analyzing.
  Function &F;

  /// Data layout of the module.
  const DataLayout &DL;

  /// Does the module have any calls to the llvm.experimental.guard intrinsic
  /// at all?  If this is false, we avoid doing work that will only help if
  /// thare are guards present in the IR.
  bool HasGuards;

  /// The target library information for the target we are targeting.
  TargetLibraryInfo &TLI;

  /// The tracker for \@llvm.assume intrinsics in this function.
  AssumptionCache &AC;

  /// The dominator tree.
  DominatorTree &DT;

  /// The loop information for the function we are currently analyzing.
  LoopInfo &LI;

  /// This SCEV is used to represent unknown trip counts and things.
  std::unique_ptr<SCEVCouldNotCompute> CouldNotCompute;

  /// The type for HasRecMap.
  using HasRecMapType = DenseMap<const SCEV *, bool>;

  /// This is a cache to record whether a SCEV contains any scAddRecExpr.
  HasRecMapType HasRecMap;

  /// The type for ExprValueMap.
  using ValueSetVector = SmallSetVector<Value *, 4>;
  using ExprValueMapType = DenseMap<const SCEV *, ValueSetVector>;

  /// ExprValueMap -- This map records the original values from which
  /// the SCEV expr is generated from.
  ExprValueMapType ExprValueMap;

  /// The type for ValueExprMap.
  using ValueExprMapType =
      DenseMap<SCEVCallbackVH, const SCEV *, DenseMapInfo<Value *>>;

  /// This is a cache of the values we have analyzed so far.
  ValueExprMapType ValueExprMap;

  /// This is a cache for expressions that got folded to a different existing
  /// SCEV.
  DenseMap<FoldID, const SCEV *> FoldCache;
  DenseMap<const SCEV *, SmallVector<FoldID, 2>> FoldCacheUser;

  /// Mark predicate values currently being processed by isImpliedCond.
  SmallPtrSet<const Value *, 6> PendingLoopPredicates;

  // Mark SCEVUnknown Phis currently being processed by isImpliedViaMerge.
  SmallPtrSet<const PHINode *, 6> PendingMerges;

  /// Set to true by isLoopBackedgeGuardedByCond when we're walking the set of
  /// conditions dominating the backedge of a loop.
  bool WalkingBEDominatingConds = false;

  /// Set to true by isKnownPredicateViaSplitting when we're trying to prove a
  /// predicate by splitting it into a set of independent predicates.
  bool ProvingSplitPredicate = false;

  /// Memoized values for the getConstantMultiple
  DenseMap<const SCEV *, APInt> ConstantMultipleCache;

  /// Return the Value set from which the SCEV expr is generated.
  ArrayRef<Value *> getSCEVValues(const SCEV *S);

  /// Private helper method for the getConstantMultiple method. If \p CtxI is
  /// not nullptr, return a constant multiple valid at \p CtxI.
  APInt getConstantMultipleImpl(const SCEV *S,
                                const Instruction *Ctx = nullptr);

  /// Information about the number of times a particular loop exit may be
  /// reached before exiting the loop.
  struct ExitNotTakenInfo {
    PoisoningVH<BasicBlock> ExitingBlock;
    const SCEV *ExactNotTaken;
    const SCEV *ConstantMaxNotTaken;
    const SCEV *SymbolicMaxNotTaken;
    SmallVector<const SCEVPredicate *, 4> Predicates;

    explicit ExitNotTakenInfo(PoisoningVH<BasicBlock> ExitingBlock,
                              const SCEV *ExactNotTaken,
                              const SCEV *ConstantMaxNotTaken,
                              const SCEV *SymbolicMaxNotTaken,
                              ArrayRef<const SCEVPredicate *> Predicates)
        : ExitingBlock(ExitingBlock), ExactNotTaken(ExactNotTaken),
          ConstantMaxNotTaken(ConstantMaxNotTaken),
          SymbolicMaxNotTaken(SymbolicMaxNotTaken), Predicates(Predicates) {}

    bool hasAlwaysTruePredicate() const {
      return Predicates.empty();
    }
  };

  /// Information about the backedge-taken count of a loop. This currently
  /// includes an exact count and a maximum count.
  ///
  class BackedgeTakenInfo {
    friend class ScalarEvolution;

    /// A list of computable exits and their not-taken counts.  Loops almost
    /// never have more than one computable exit.
    SmallVector<ExitNotTakenInfo, 1> ExitNotTaken;

    /// Expression indicating the least constant maximum backedge-taken count of
    /// the loop that is known, or a SCEVCouldNotCompute. This expression is
    /// only valid if the predicates associated with all loop exits are true.
    const SCEV *ConstantMax = nullptr;

    /// Indicating if \c ExitNotTaken has an element for every exiting block in
    /// the loop.
    bool IsComplete = false;

    /// Expression indicating the least maximum backedge-taken count of the loop
    /// that is known, or a SCEVCouldNotCompute. Lazily computed on first query.
    const SCEV *SymbolicMax = nullptr;

    /// True iff the backedge is taken either exactly Max or zero times.
    bool MaxOrZero = false;

    bool isComplete() const { return IsComplete; }
    const SCEV *getConstantMax() const { return ConstantMax; }

    LLVM_ABI const ExitNotTakenInfo *getExitNotTaken(
        const BasicBlock *ExitingBlock,
        SmallVectorImpl<const SCEVPredicate *> *Predicates = nullptr) const;

  public:
    BackedgeTakenInfo() = default;
    BackedgeTakenInfo(BackedgeTakenInfo &&) = default;
    BackedgeTakenInfo &operator=(BackedgeTakenInfo &&) = default;

    using EdgeExitInfo = std::pair<BasicBlock *, ExitLimit>;

    /// Initialize BackedgeTakenInfo from a list of exact exit counts.
    LLVM_ABI BackedgeTakenInfo(ArrayRef<EdgeExitInfo> ExitCounts,
                               bool IsComplete, const SCEV *ConstantMax,
                               bool MaxOrZero);

    /// Test whether this BackedgeTakenInfo contains any computed information,
    /// or whether it's all SCEVCouldNotCompute values.
    /// @return Test whether this BackedgeTakenInfo contains any computed information, or whether
    /// it's all SCEVCouldNotCompute values.
    bool hasAnyInfo() const {
      return !ExitNotTaken.empty() ||
             !isa<SCEVCouldNotCompute>(getConstantMax());
    }

    /// Test whether this BackedgeTakenInfo contains complete information.
    /// @return Test whether this BackedgeTakenInfo contains complete information.
    bool hasFullInfo() const { return isComplete(); }

    /// Return an expression indicating the exact *backedge-taken*
    /// count of the loop if it is known or SCEVCouldNotCompute
    /// otherwise.  If execution makes it to the backedge on every
    /// iteration (i.e. there are no abnormal exists like exception
    /// throws and thread exits) then this is the number of times the
    /// loop header will execute minus one.
    ///
    /// If the SCEV predicate associated with the answer can be different
    /// from AlwaysTrue, we must add a (non null) Predicates argument.
    /// The SCEV predicate associated with the answer will be added to
    /// Predicates. A run-time check needs to be emitted for the SCEV
    /// predicate in order for the answer to be valid.
    ///
    /// Note that we should always know if we need to pass a predicate
    /// argument or not from the way the ExitCounts vector was computed.
    /// If we allowed SCEV predicates to be generated when populating this
    /// vector, this information can contain them and therefore a
    /// SCEVPredicate argument should be added to getExact.
    LLVM_ABI const SCEV *getExact(
        const Loop *L, ScalarEvolution *SE,
        SmallVectorImpl<const SCEVPredicate *> *Predicates = nullptr) const;

    /// Return the number of times this loop exit may fall through to the back
    /// edge, or SCEVCouldNotCompute. The loop is guaranteed not to exit via
    /// this block before this number of iterations, but may exit via another
    /// block. If \p Predicates is null the function returns CouldNotCompute if
    /// predicates are required, otherwise it fills in the required predicates.
    const SCEV *getExact(
        const BasicBlock *ExitingBlock, ScalarEvolution *SE,
        SmallVectorImpl<const SCEVPredicate *> *Predicates = nullptr) const {
      if (auto *ENT = getExitNotTaken(ExitingBlock, Predicates))
        return ENT->ExactNotTaken;
      else
        return SE->getCouldNotCompute();
    }

    /// Get the constant max backedge taken count for the loop.
    LLVM_ABI const SCEV *getConstantMax(
        ScalarEvolution *SE,
        SmallVectorImpl<const SCEVPredicate *> *Predicates = nullptr) const;

    /// Get the constant max backedge taken count for the particular loop exit.
    const SCEV *getConstantMax(
        const BasicBlock *ExitingBlock, ScalarEvolution *SE,
        SmallVectorImpl<const SCEVPredicate *> *Predicates = nullptr) const {
      if (auto *ENT = getExitNotTaken(ExitingBlock, Predicates))
        return ENT->ConstantMaxNotTaken;
      else
        return SE->getCouldNotCompute();
    }

    /// Get the symbolic max backedge taken count for the loop.
    LLVM_ABI const SCEV *getSymbolicMax(
        const Loop *L, ScalarEvolution *SE,
        SmallVectorImpl<const SCEVPredicate *> *Predicates = nullptr);

    /// Get the symbolic max backedge taken count for the particular loop exit.
    const SCEV *getSymbolicMax(
        const BasicBlock *ExitingBlock, ScalarEvolution *SE,
        SmallVectorImpl<const SCEVPredicate *> *Predicates = nullptr) const {
      if (auto *ENT = getExitNotTaken(ExitingBlock, Predicates))
        return ENT->SymbolicMaxNotTaken;
      else
        return SE->getCouldNotCompute();
    }

    /// Return true if the number of times this backedge is taken is either the
    /// value returned by getConstantMax or zero.
    LLVM_ABI bool isConstantMaxOrZero(ScalarEvolution *SE) const;
  };

  /// Cache the backedge-taken count of the loops for this function as they
  /// are computed.
  DenseMap<const Loop *, BackedgeTakenInfo> BackedgeTakenCounts;

  /// Cache the predicated backedge-taken count of the loops for this
  /// function as they are computed.
  DenseMap<const Loop *, BackedgeTakenInfo> PredicatedBackedgeTakenCounts;

  /// Loops whose backedge taken counts directly use this non-constant SCEV.
  DenseMap<const SCEV *, SmallPtrSet<PointerIntPair<const Loop *, 1, bool>, 4>>
      BECountUsers;

  /// This map contains entries for all of the PHI instructions that we
  /// attempt to compute constant evolutions for.  This allows us to avoid
  /// potentially expensive recomputation of these properties.  An instruction
  /// maps to null if we are unable to compute its exit value.
  DenseMap<PHINode *, Constant *> ConstantEvolutionLoopExitValue;

  /// This map contains entries for all the expressions that we attempt to
  /// compute getSCEVAtScope information for, which can be expensive in
  /// extreme cases.
  DenseMap<const SCEV *, SmallVector<std::pair<const Loop *, const SCEV *>, 2>>
      ValuesAtScopes;

  /// Reverse map for invalidation purposes: Stores of which SCEV and which
  /// loop this is the value-at-scope of.
  DenseMap<const SCEV *, SmallVector<std::pair<const Loop *, const SCEV *>, 2>>
      ValuesAtScopesUsers;

  /// Memoized computeLoopDisposition results.
  DenseMap<const SCEV *,
           SmallVector<PointerIntPair<const Loop *, 2, LoopDisposition>, 2>>
      LoopDispositions;

  struct LoopProperties {
    /// Set to true if the loop contains no instruction that can abnormally exit
    /// the loop (i.e. via throwing an exception, by terminating the thread
    /// cleanly or by infinite looping in a called function).  Strictly
    /// speaking, the last one is not leaving the loop, but is identical to
    /// leaving the loop for reasoning about undefined behavior.
    bool HasNoAbnormalExits;

    /// Set to true if the loop contains no instruction that can have side
    /// effects (i.e. via throwing an exception, volatile or atomic access).
    bool HasNoSideEffects;
  };

  /// Cache for \c getLoopProperties.
  DenseMap<const Loop *, LoopProperties> LoopPropertiesCache;

  /// Return a \c LoopProperties instance for \p L, creating one if necessary.
  LLVM_ABI LoopProperties getLoopProperties(const Loop *L);

  bool loopHasNoSideEffects(const Loop *L) {
    return getLoopProperties(L).HasNoSideEffects;
  }

  /// Compute a LoopDisposition value.
  LoopDisposition computeLoopDisposition(const SCEV *S, const Loop *L);

  /// Memoized computeBlockDisposition results.
  DenseMap<
      const SCEV *,
      SmallVector<PointerIntPair<const BasicBlock *, 2, BlockDisposition>, 2>>
      BlockDispositions;

  /// Compute a BlockDisposition value.
  BlockDisposition computeBlockDisposition(const SCEV *S, const BasicBlock *BB);

  /// Stores all SCEV that use a given SCEV as its direct operand.
  DenseMap<const SCEV *, SmallPtrSet<const SCEV *, 8> > SCEVUsers;

  /// Memoized results from getRange
  DenseMap<const SCEV *, ConstantRange> UnsignedRanges;

  /// Memoized results from getRange
  DenseMap<const SCEV *, ConstantRange> SignedRanges;

  /// Used to parameterize getRange
  enum RangeSignHint { HINT_RANGE_UNSIGNED, HINT_RANGE_SIGNED };

  /// Set the memoized range for the given SCEV.
  const ConstantRange &setRange(const SCEV *S, RangeSignHint Hint,
                                ConstantRange CR) {
    DenseMap<const SCEV *, ConstantRange> &Cache =
        Hint == HINT_RANGE_UNSIGNED ? UnsignedRanges : SignedRanges;

    auto Pair = Cache.insert_or_assign(S, std::move(CR));
    return Pair.first->second;
  }

  /// Determine the range for a particular SCEV.
  /// NOTE: This returns a reference to an entry in a cache. It must be
  /// copied if its needed for longer.
  LLVM_ABI const ConstantRange &getRangeRef(const SCEV *S, RangeSignHint Hint,
                                            unsigned Depth = 0);

  /// Determine the range for a particular SCEV, but evaluates ranges for
  /// operands iteratively first.
  const ConstantRange &getRangeRefIter(const SCEV *S, RangeSignHint Hint);

  /// Determines the range for the affine SCEVAddRecExpr {\p Start,+,\p Step},
  /// and whether it may wrap. Helper for \c getRange.
  std::pair<ConstantRange, SCEV::NoWrapFlags>
  getRangeForAffineAR(const SCEV *Start, const SCEV *Step,
                      const APInt &MaxBECount);
  /// If \p S is a SCEVConstant, return the wrapped constant or nullptr
  /// otherwise.
  LLVM_ABI static const APInt *getConstantAPIntOrNull(const SCEV *S);

  /// Determines the range for the affine non-self-wrapping SCEVAddRecExpr {\p
  /// Start,+,\p Step}<nw>.
  ConstantRange getRangeForAffineNoSelfWrappingAR(const SCEVAddRecExpr *AddRec,
                                                  const SCEV *MaxBECount,
                                                  unsigned BitWidth,
                                                  RangeSignHint SignHint);

  /// Try to compute a range for the affine SCEVAddRecExpr {\p Start,+,\p
  /// Step} by "factoring out" a ternary expression from the add recurrence.
  /// Helper called by \c getRange.
  ConstantRange getRangeViaFactoring(const SCEV *Start, const SCEV *Step,
                                     const APInt &MaxBECount);

  /// If the unknown expression U corresponds to a simple recurrence, return
  /// a constant range which represents the entire recurrence.  Note that
  /// *add* recurrences with loop invariant steps aren't represented by
  /// SCEVUnknowns and thus don't use this mechanism.
  ConstantRange getRangeForUnknownRecurrence(const SCEVUnknown *U);

  /// We know that there is no SCEV for the specified value.  Analyze the
  /// expression recursively.
  const SCEV *createSCEV(Value *V);

  /// We know that there is no SCEV for the specified value. Create a new SCEV
  /// for \p V iteratively.
  const SCEV *createSCEVIter(Value *V);
  /// Collect operands of \p V for which SCEV expressions should be constructed
  /// first. Returns a SCEV directly if it can be constructed trivially for \p
  /// V.
  const SCEV *getOperandsToCreate(Value *V, SmallVectorImpl<Value *> &Ops);

  /// Returns SCEV for the first operand of a phi if all phi operands have
  /// identical opcodes and operands.
  const SCEV *createNodeForPHIWithIdenticalOperands(PHINode *PN);

  /// Provide the special handling we need to analyze PHI SCEVs.
  const SCEV *createNodeForPHI(PHINode *PN);

  /// Helper function called from createNodeForPHI.
  const SCEV *createAddRecFromPHI(PHINode *PN);

  /// A helper function for createAddRecFromPHI to handle simple cases.
  const SCEV *createSimpleAffineAddRec(PHINode *PN, Value *BEValueV,
                                            Value *StartValueV);

  /// Helper function called from createNodeForPHI.
  const SCEV *createNodeFromSelectLikePHI(PHINode *PN);

  /// Provide special handling for a select-like instruction (currently this
  /// is either a select instruction or a phi node).  \p Ty is the type of the
  /// instruction being processed, that is assumed equivalent to
  /// "Cond ? TrueVal : FalseVal".
  std::optional<const SCEV *>
  createNodeForSelectOrPHIInstWithICmpInstCond(Type *Ty, ICmpInst *Cond,
                                               Value *TrueVal, Value *FalseVal);

  /// See if we can model this select-like instruction via umin_seq expression.
  const SCEV *createNodeForSelectOrPHIViaUMinSeq(Value *I, Value *Cond,
                                                 Value *TrueVal,
                                                 Value *FalseVal);

  /// Given a value \p V, which is a select-like instruction (currently this is
  /// either a select instruction or a phi node), which is assumed equivalent to
  ///   Cond ? TrueVal : FalseVal
  /// see if we can model it as a SCEV expression.
  const SCEV *createNodeForSelectOrPHI(Value *V, Value *Cond, Value *TrueVal,
                                       Value *FalseVal);

  /// Provide the special handling we need to analyze GEP SCEVs.
  const SCEV *createNodeForGEP(GEPOperator *GEP);

  /// Implementation code for getSCEVAtScope; called at most once for each
  /// SCEV+Loop pair.
  const SCEV *computeSCEVAtScope(const SCEV *S, const Loop *L);

  /// Return the BackedgeTakenInfo for the given loop, lazily computing new
  /// values if the loop hasn't been analyzed yet. The returned result is
  /// guaranteed not to be predicated.
  BackedgeTakenInfo &getBackedgeTakenInfo(const Loop *L);

  /// Similar to getBackedgeTakenInfo, but will add predicates as required
  /// with the purpose of returning complete information.
  BackedgeTakenInfo &getPredicatedBackedgeTakenInfo(const Loop *L);

  /// Compute the number of times the specified loop will iterate.
  /// If AllowPredicates is set, we will create new SCEV predicates as
  /// necessary in order to return an exact answer.
  BackedgeTakenInfo computeBackedgeTakenCount(const Loop *L,
                                              bool AllowPredicates = false);

  /// Compute the number of times the backedge of the specified loop will
  /// execute if it exits via the specified block. If AllowPredicates is set,
  /// this call will try to use a minimal set of SCEV predicates in order to
  /// return an exact answer.
  ExitLimit computeExitLimit(const Loop *L, BasicBlock *ExitingBlock,
                             bool IsOnlyExit, bool AllowPredicates = false);

  // Helper functions for computeExitLimitFromCond to avoid exponential time
  // complexity.

  class ExitLimitCache {
    // It may look like we need key on the whole (L, ExitIfTrue,
    // ControlsOnlyExit, AllowPredicates) tuple, but recursive calls to
    // computeExitLimitFromCondCached from computeExitLimitFromCondImpl only
    // vary the in \c ExitCond and \c ControlsOnlyExit parameters.  We remember
    // the initial values of the other values to assert our assumption.
    SmallDenseMap<PointerIntPair<Value *, 1>, ExitLimit> TripCountMap;

    const Loop *L;
    bool ExitIfTrue;
    bool AllowPredicates;

  public:
    ExitLimitCache(const Loop *L, bool ExitIfTrue, bool AllowPredicates)
        : L(L), ExitIfTrue(ExitIfTrue), AllowPredicates(AllowPredicates) {}

    LLVM_ABI std::optional<ExitLimit> find(const Loop *L, Value *ExitCond,
                                           bool ExitIfTrue,
                                           bool ControlsOnlyExit,
                                           bool AllowPredicates);

    LLVM_ABI void insert(const Loop *L, Value *ExitCond, bool ExitIfTrue,
                         bool ControlsOnlyExit, bool AllowPredicates,
                         const ExitLimit &EL);
  };

  using ExitLimitCacheTy = ExitLimitCache;

  ExitLimit computeExitLimitFromCondCached(ExitLimitCacheTy &Cache,
                                           const Loop *L, Value *ExitCond,
                                           bool ExitIfTrue,
                                           bool ControlsOnlyExit,
                                           bool AllowPredicates);
  ExitLimit computeExitLimitFromCondImpl(ExitLimitCacheTy &Cache, const Loop *L,
                                         Value *ExitCond, bool ExitIfTrue,
                                         bool ControlsOnlyExit,
                                         bool AllowPredicates);
  std::optional<ScalarEvolution::ExitLimit>
  computeExitLimitFromCondFromBinOp(ExitLimitCacheTy &Cache, const Loop *L,
                                    Value *ExitCond, bool ExitIfTrue,
                                    bool AllowPredicates);

  /// Compute the number of times the backedge of the specified loop will
  /// execute if its exit condition were a conditional branch of the ICmpInst
  /// ExitCond and ExitIfTrue. If AllowPredicates is set, this call will try
  /// to use a minimal set of SCEV predicates in order to return an exact
  /// answer.
  ExitLimit computeExitLimitFromICmp(const Loop *L, ICmpInst *ExitCond,
                                     bool ExitIfTrue,
                                     bool IsSubExpr,
                                     bool AllowPredicates = false);

  /// Variant of previous which takes the components representing an ICmp
  /// as opposed to the ICmpInst itself.  Note that the prior version can
  /// return more precise results in some cases and is preferred when caller
  /// has a materialized ICmp.
  ExitLimit computeExitLimitFromICmp(const Loop *L, CmpPredicate Pred,
                                     SCEVUse LHS, SCEVUse RHS, bool IsSubExpr,
                                     bool AllowPredicates = false);

  /// Compute the number of times the backedge of the specified loop will
  /// execute if its exit condition were a switch with a single exiting case
  /// to ExitingBB.
  ExitLimit computeExitLimitFromSingleExitSwitch(const Loop *L,
                                                 SwitchInst *Switch,
                                                 BasicBlock *ExitingBB,
                                                 bool IsSubExpr);

  /// Compute the exit limit of a loop that is controlled by a
  /// "(IV >> 1) != 0" type comparison.  We cannot compute the exact trip
  /// count in these cases (since SCEV has no way of expressing them), but we
  /// can still sometimes compute an upper bound.
  ///
  /// Return an ExitLimit for a loop whose backedge is guarded by `LHS Pred
  /// RHS`.
  ExitLimit computeShiftCompareExitLimit(Value *LHS, Value *RHS, const Loop *L,
                                         ICmpInst::Predicate Pred);

  /// If the loop is known to execute a constant number of times (the
  /// condition evolves only from constants), try to evaluate a few iterations
  /// of the loop until we get the exit condition gets a value of ExitWhen
  /// (true or false).  If we cannot evaluate the exit count of the loop,
  /// return CouldNotCompute.
  const SCEV *computeExitCountExhaustively(const Loop *L, Value *Cond,
                                           bool ExitWhen);

  /// Return the number of times an exit condition comparing the specified
  /// value to zero will execute.  If not computable, return CouldNotCompute.
  /// If AllowPredicates is set, this call will try to use a minimal set of
  /// SCEV predicates in order to return an exact answer.
  ExitLimit howFarToZero(const SCEV *V, const Loop *L, bool IsSubExpr,
                         bool AllowPredicates = false);

  /// Return the number of times an exit condition checking the specified
  /// value for nonzero will execute.  If not computable, return
  /// CouldNotCompute.
  ExitLimit howFarToNonZero(const SCEV *V, const Loop *L);

  /// Return the number of times an exit condition containing the specified
  /// less-than comparison will execute.  If not computable, return
  /// CouldNotCompute.
  ///
  /// \p isSigned specifies whether the less-than is signed.
  ///
  /// \p ControlsOnlyExit is true when the LHS < RHS condition directly controls
  /// the branch (loops exits only if condition is true). In this case, we can
  /// use NoWrapFlags to skip overflow checks.
  ///
  /// If \p AllowPredicates is set, this call will try to use a minimal set of
  /// SCEV predicates in order to return an exact answer.
  ExitLimit howManyLessThans(const SCEV *LHS, const SCEV *RHS, const Loop *L,
                             bool isSigned, bool ControlsOnlyExit,
                             bool AllowPredicates = false);

  ExitLimit howManyGreaterThans(const SCEV *LHS, const SCEV *RHS, const Loop *L,
                                bool isSigned, bool IsSubExpr,
                                bool AllowPredicates = false);

  /// Return a predecessor of BB (which may not be an immediate predecessor)
  /// which has exactly one successor from which BB is reachable, or null if
  /// no such block is found.
  std::pair<const BasicBlock *, const BasicBlock *>
  getPredecessorWithUniqueSuccessorForBB(const BasicBlock *BB) const;

  /// Test whether the condition described by Pred, LHS, and RHS is true
  /// whenever the given FoundCondValue value evaluates to true in given
  /// Context. If Context is nullptr, then the found predicate is true
  /// everywhere. LHS and FoundLHS may have different type width.
  LLVM_ABI bool isImpliedCond(CmpPredicate Pred, const SCEV *LHS,
                              const SCEV *RHS, const Value *FoundCondValue,
                              bool Inverse,
                              const Instruction *Context = nullptr);

  /// Test whether the condition described by Pred, LHS, and RHS is true
  /// whenever the given FoundCondValue value evaluates to true in given
  /// Context. If Context is nullptr, then the found predicate is true
  /// everywhere. LHS and FoundLHS must have same type width.
  LLVM_ABI bool isImpliedCondBalancedTypes(CmpPredicate Pred, SCEVUse LHS,
                                           SCEVUse RHS, CmpPredicate FoundPred,
                                           SCEVUse FoundLHS, SCEVUse FoundRHS,
                                           const Instruction *CtxI);

  /// Test whether the condition described by Pred, LHS, and RHS is true
  /// whenever the condition described by FoundPred, FoundLHS, FoundRHS is
  /// true in given Context. If Context is nullptr, then the found predicate is
  /// true everywhere.
  LLVM_ABI bool isImpliedCond(CmpPredicate Pred, const SCEV *LHS,
                              const SCEV *RHS, CmpPredicate FoundPred,
                              const SCEV *FoundLHS, const SCEV *FoundRHS,
                              const Instruction *Context = nullptr);

  /// Test whether the condition described by Pred, LHS, and RHS is true
  /// whenever the condition described by Pred, FoundLHS, and FoundRHS is
  /// true in given Context. If Context is nullptr, then the found predicate is
  /// true everywhere.
  bool isImpliedCondOperands(CmpPredicate Pred, const SCEV *LHS,
                             const SCEV *RHS, const SCEV *FoundLHS,
                             const SCEV *FoundRHS,
                             const Instruction *Context = nullptr);

  /// Test whether the condition described by Pred, LHS, and RHS is true
  /// whenever the condition described by Pred, FoundLHS, and FoundRHS is
  /// true. Here LHS is an operation that includes FoundLHS as one of its
  /// arguments.
  bool isImpliedViaOperations(CmpPredicate Pred, const SCEV *LHS,
                              const SCEV *RHS, const SCEV *FoundLHS,
                              const SCEV *FoundRHS, unsigned Depth = 0);

  /// Test whether the condition described by Pred, LHS, and RHS is true.
  /// Use only simple non-recursive types of checks, such as range analysis etc.
  bool isKnownViaNonRecursiveReasoning(CmpPredicate Pred, SCEVUse LHS,
                                       SCEVUse RHS);

  /// Test whether the condition described by Pred, LHS, and RHS is true
  /// whenever the condition described by Pred, FoundLHS, and FoundRHS is
  /// true.
  bool isImpliedCondOperandsHelper(CmpPredicate Pred, const SCEV *LHS,
                                   const SCEV *RHS, const SCEV *FoundLHS,
                                   const SCEV *FoundRHS);

  /// Test whether the condition described by Pred, LHS, and RHS is true
  /// whenever the condition described by Pred, FoundLHS, and FoundRHS is
  /// true.  Utility function used by isImpliedCondOperands.  Tries to get
  /// cases like "X `sgt` 0 => X - 1 `sgt` -1".
  bool isImpliedCondOperandsViaRanges(CmpPredicate Pred, const SCEV *LHS,
                                      const SCEV *RHS, CmpPredicate FoundPred,
                                      const SCEV *FoundLHS,
                                      const SCEV *FoundRHS);

  /// Return true if the condition denoted by \p LHS \p Pred \p RHS is implied
  /// by a call to @llvm.experimental.guard in \p BB.
  bool isImpliedViaGuard(const BasicBlock *BB, CmpPredicate Pred,
                         const SCEV *LHS, const SCEV *RHS);

  /// Test whether the condition described by Pred, LHS, and RHS is true
  /// whenever the condition described by Pred, FoundLHS, and FoundRHS is
  /// true.
  ///
  /// This routine tries to rule out certain kinds of integer overflow, and
  /// then tries to reason about arithmetic properties of the predicates.
  bool isImpliedCondOperandsViaNoOverflow(CmpPredicate Pred, const SCEV *LHS,
                                          const SCEV *RHS, const SCEV *FoundLHS,
                                          const SCEV *FoundRHS);

  /// Test whether the condition described by Pred, LHS, and RHS is true
  /// whenever the condition described by Pred, FoundLHS, and FoundRHS is
  /// true.
  ///
  /// This routine tries to weaken the known condition basing on fact that
  /// FoundLHS is an AddRec.
  bool isImpliedCondOperandsViaAddRecStart(CmpPredicate Pred, const SCEV *LHS,
                                           const SCEV *RHS,
                                           const SCEV *FoundLHS,
                                           const SCEV *FoundRHS,
                                           const Instruction *CtxI);

  /// Test whether the condition described by Pred, LHS, and RHS is true
  /// whenever the condition described by Pred, FoundLHS, and FoundRHS is
  /// true.
  ///
  /// This routine tries to figure out predicate for Phis which are SCEVUnknown
  /// if it is true for every possible incoming value from their respective
  /// basic blocks.
  bool isImpliedViaMerge(CmpPredicate Pred, const SCEV *LHS, const SCEV *RHS,
                         const SCEV *FoundLHS, const SCEV *FoundRHS,
                         unsigned Depth);

  /// Test whether the condition described by Pred, LHS, and RHS is true
  /// whenever the condition described by Pred, FoundLHS, and FoundRHS is
  /// true.
  ///
  /// This routine tries to reason about shifts.
  bool isImpliedCondOperandsViaShift(CmpPredicate Pred, const SCEV *LHS,
                                     const SCEV *RHS, const SCEV *FoundLHS,
                                     const SCEV *FoundRHS);

  /// Test whether the condition described by Pred, LHS, and RHS is true
  /// whenever the condition described by Pred, FoundLHS, and FoundRHS is
  /// true.
  ///
  /// This routine tries to analyze if the SCEV differences match.
  bool isImpliedCondOperandsViaMatchingDiff(CmpPredicate Pred, const SCEV *LHS,
                                            const SCEV *RHS,
                                            const SCEV *FoundLHS,
                                            const SCEV *FoundRHS);

  /// If we know that the specified Phi is in the header of its containing
  /// loop, we know the loop executes a constant number of times, and the PHI
  /// node is just a recurrence involving constants, fold it.
  Constant *getConstantEvolutionLoopExitValue(PHINode *PN, const APInt &BEs,
                                              const Loop *L);

  /// Test if the given expression is known to satisfy the condition described
  /// by Pred and the known constant ranges of LHS and RHS.
  bool isKnownPredicateViaConstantRanges(CmpPredicate Pred, SCEVUse LHS,
                                         SCEVUse RHS);

  /// Try to prove the condition described by "LHS Pred RHS" by ruling out
  /// integer overflow.
  ///
  /// For instance, this will return true for "A s< (A + C)<nsw>" if C is
  /// positive.
  bool isKnownPredicateViaNoOverflow(CmpPredicate Pred, SCEVUse LHS,
                                     SCEVUse RHS);

  /// Try to split Pred LHS RHS into logical conjunctions (and's) and try to
  /// prove them individually.
  bool isKnownPredicateViaSplitting(CmpPredicate Pred, SCEVUse LHS,
                                    SCEVUse RHS);

  /// Try to match the Expr as "(L + R)<Flags>".
  bool splitBinaryAdd(SCEVUse Expr, SCEVUse &L, SCEVUse &R,
                      SCEV::NoWrapFlags &Flags);

  /// Forget predicated/non-predicated backedge taken counts for the given loop.
  void forgetBackedgeTakenCounts(const Loop *L, bool Predicated);

  /// Drop memoized information for all \p SCEVs.
  void forgetMemoizedResults(ArrayRef<SCEVUse> SCEVs);

  /// Helper for forgetMemoizedResults.
  void forgetMemoizedResultsImpl(const SCEV *S);

  /// Iterate over instructions in \p Worklist and their users. Erase entries
  /// from ValueExprMap and collect SCEV expressions in \p ToForget
  void visitAndClearUsers(SmallVectorImpl<Instruction *> &Worklist,
                          SmallPtrSetImpl<Instruction *> &Visited,
                          SmallVectorImpl<SCEVUse> &ToForget);

  /// Erase Value from ValueExprMap and ExprValueMap.
  void eraseValueFromMap(Value *V);

  /// Insert V to S mapping into ValueExprMap and ExprValueMap.
  void insertValueToMap(Value *V, const SCEV *S);

  /// Return false iff given SCEV contains a SCEVUnknown with NULL value-
  /// pointer.
  bool checkValidity(const SCEV *S) const;

  /// Return true if `ExtendOpTy`({`Start`,+,`Step`}) can be proved to be
  /// equal to {`ExtendOpTy`(`Start`),+,`ExtendOpTy`(`Step`)}.  This is
  /// equivalent to proving no signed (resp. unsigned) wrap in
  /// {`Start`,+,`Step`} if `ExtendOpTy` is `SCEVSignExtendExpr`
  /// (resp. `SCEVZeroExtendExpr`).
  template <typename ExtendOpTy>
  bool proveNoWrapByVaryingStart(const SCEV *Start, const SCEV *Step,
                                 const Loop *L);

  /// Try to infer NSW or NUW on \p AR relying on ConstantRange manipulation.
  void inferNoWrapViaConstantRanges(const SCEVAddRecExpr *AR);

  /// Try to prove NSW on \p AR by proving facts about conditions known  on
  /// entry and backedge.
  SCEV::NoWrapFlags proveNoSignedWrapViaInduction(const SCEVAddRecExpr *AR);

  /// Try to prove NUW on \p AR by proving facts about conditions known on
  /// entry and backedge.
  SCEV::NoWrapFlags proveNoUnsignedWrapViaInduction(const SCEVAddRecExpr *AR);

  std::optional<MonotonicPredicateType>
  getMonotonicPredicateTypeImpl(const SCEVAddRecExpr *LHS,
                                ICmpInst::Predicate Pred);

  /// Return SCEV no-wrap flags that can be proven based on reasoning about
  /// how poison produced from no-wrap flags on this value (e.g. a nuw add)
  /// would trigger undefined behavior on overflow.
  SCEV::NoWrapFlags getNoWrapFlagsFromUB(const Value *V);

  /// Return a scope which provides an upper bound on the defining scope of
  /// 'S'. Specifically, return the first instruction in said bounding scope.
  /// Return nullptr if the scope is trivial (function entry).
  /// (See scope definition rules associated with flag discussion above)
  const Instruction *getNonTrivialDefiningScopeBound(const SCEV *S);

  /// Return a scope which provides an upper bound on the defining scope for
  /// a SCEV with the operands in Ops.  The outparam Precise is set if the
  /// bound found is a precise bound (i.e. must be the defining scope.)
  const Instruction *getDefiningScopeBound(ArrayRef<SCEVUse> Ops,
                                           bool &Precise);

  /// Wrapper around the above for cases which don't care if the bound
  /// is precise.
  const Instruction *getDefiningScopeBound(ArrayRef<SCEVUse> Ops);

  /// Given two instructions in the same function, return true if we can
  /// prove B must execute given A executes.
  bool isGuaranteedToTransferExecutionTo(const Instruction *A,
                                         const Instruction *B);

  /// Returns true if \p Op is guaranteed not to cause immediate UB.
  bool isGuaranteedNotToCauseUB(const SCEV *Op);

  /// Return true if the SCEV corresponding to \p I is never poison.  Proving
  /// this is more complex than proving that just \p I is never poison, since
  /// SCEV commons expressions across control flow, and you can have cases
  /// like:
  ///
  ///   idx0 = a + b;
  ///   ptr[idx0] = 100;
  ///   if (<condition>) {
  ///     idx1 = a +nsw b;
  ///     ptr[idx1] = 200;
  ///   }
  ///
  /// where the SCEV expression (+ a b) is guaranteed to not be poison (and
  /// hence not sign-overflow) only if "<condition>" is true.  Since both
  /// `idx0` and `idx1` will be mapped to the same SCEV expression, (+ a b),
  /// it is not okay to annotate (+ a b) with <nsw> in the above example.
  bool isSCEVExprNeverPoison(const Instruction *I);

  /// This is like \c isSCEVExprNeverPoison but it specifically works for
  /// instructions that will get mapped to SCEV add recurrences.  Return true
  /// if \p I will never generate poison under the assumption that \p I is an
  /// add recurrence on the loop \p L.
  bool isAddRecNeverPoison(const Instruction *I, const Loop *L);

  /// Similar to createAddRecFromPHI, but with the additional flexibility of
  /// suggesting runtime overflow checks in case casts are encountered.
  /// If successful, the analysis records that for this loop, \p SymbolicPHI,
  /// which is the UnknownSCEV currently representing the PHI, can be rewritten
  /// into an AddRec, assuming some predicates; The function then returns the
  /// AddRec and the predicates as a pair, and caches this pair in
  /// PredicatedSCEVRewrites.
  /// If the analysis is not successful, a mapping from the \p SymbolicPHI to
  /// itself (with no predicates) is recorded, and a nullptr with an empty
  /// predicates vector is returned as a pair.
  std::optional<std::pair<const SCEV *, SmallVector<const SCEVPredicate *, 3>>>
  createAddRecFromPHIWithCastsImpl(const SCEVUnknown *SymbolicPHI);

  /// Compute the maximum backedge count based on the range of values
  /// permitted by Start, End, and Stride. This is for loops of the form
  /// {Start, +, Stride} LT End.
  ///
  /// Preconditions:
  /// * the induction variable is known to be positive.
  /// * the induction variable is assumed not to overflow (i.e. either it
  ///   actually doesn't, or we'd have to immediately execute UB)
  /// We *don't* assert these preconditions so please be careful.
  const SCEV *computeMaxBECountForLT(const SCEV *Start, const SCEV *Stride,
                                     const SCEV *End, unsigned BitWidth,
                                     bool IsSigned);

  /// Verify if an linear IV with positive stride can overflow when in a
  /// less-than comparison, knowing the invariant term of the comparison,
  /// the stride.
  bool canIVOverflowOnLT(const SCEV *RHS, const SCEV *Stride, bool IsSigned);

  /// Verify if an linear IV with negative stride can overflow when in a
  /// greater-than comparison, knowing the invariant term of the comparison,
  /// the stride.
  bool canIVOverflowOnGT(const SCEV *RHS, const SCEV *Stride, bool IsSigned);

  /// Get add expr already created or create a new one.
  const SCEV *getOrCreateAddExpr(ArrayRef<SCEVUse> Ops,
                                 SCEV::NoWrapFlags Flags);

  /// Get mul expr already created or create a new one.
  const SCEV *getOrCreateMulExpr(ArrayRef<SCEVUse> Ops,
                                 SCEV::NoWrapFlags Flags);

  // Get addrec expr already created or create a new one.
  const SCEV *getOrCreateAddRecExpr(ArrayRef<SCEVUse> Ops, const Loop *L,
                                    SCEV::NoWrapFlags Flags);

  // Get UDiv expression already created or create a new one.
  const SCEV *getOrCreateUDivExpr(SCEVUse LHS, SCEVUse RHS);

  /// Return x if \p Val is f(x) where f is a 1-1 function.
  const SCEV *stripInjectiveFunctions(const SCEV *Val) const;

  /// Find all of the loops transitively used in \p S, and fill \p LoopsUsed.
  /// A loop is considered "used" by an expression if it contains
  /// an add rec on said loop.
  void getUsedLoops(const SCEV *S, SmallPtrSetImpl<const Loop *> &LoopsUsed);

  /// Look for a SCEV expression with type `SCEVType` and operands `Ops` in
  /// `UniqueSCEVs`.  Return if found, else nullptr.
  SCEV *findExistingSCEVInCache(SCEVTypes SCEVType, ArrayRef<SCEVUse> Ops);

  /// Get reachable blocks in this function, making limited use of SCEV
  /// reasoning about conditions.
  void getReachableBlocks(SmallPtrSetImpl<BasicBlock *> &Reachable,
                          Function &F);

  /// Return the given SCEV expression with a new set of operands.
  /// This preserves the origial nowrap flags.
  const SCEV *getWithOperands(const SCEV *S, SmallVectorImpl<SCEVUse> &NewOps);

  FoldingSet<SCEV> UniqueSCEVs;
  FoldingSet<SCEVPredicate> UniquePreds;
  BumpPtrAllocator SCEVAllocator;

  /// Fast lookup cache for SCEVConstant nodes, using the fact that IR constants
  /// are already uniqued.
  DenseMap<ConstantInt *, SCEVConstant *> ConstantSCEVs;

  /// This maps loops to a list of addrecs that directly use said loop.
  DenseMap<const Loop *, SmallVector<const SCEVAddRecExpr *, 4>> LoopUsers;

  /// Cache tentative mappings from UnknownSCEVs in a Loop, to a SCEV expression
  /// they can be rewritten into under certain predicates.
  DenseMap<std::pair<const SCEVUnknown *, const Loop *>,
           std::pair<const SCEV *, SmallVector<const SCEVPredicate *, 3>>>
      PredicatedSCEVRewrites;

  /// Set of AddRecs for which proving NUW via an induction has already been
  /// tried.
  SmallPtrSet<const SCEVAddRecExpr *, 16> UnsignedWrapViaInductionTried;

  /// Set of AddRecs for which proving NSW via an induction has already been
  /// tried.
  SmallPtrSet<const SCEVAddRecExpr *, 16> SignedWrapViaInductionTried;

  /// The head of a linked list of all SCEVUnknown values that have been
  /// allocated. This is used by releaseMemory to locate them all and call
  /// their destructors.
  SCEVUnknown *FirstUnknown = nullptr;
};

/// Analysis pass that exposes the \c ScalarEvolution for a function.
class ScalarEvolutionAnalysis
    : public AnalysisInfoMixin<ScalarEvolutionAnalysis> {
  friend AnalysisInfoMixin<ScalarEvolutionAnalysis>;

  LLVM_ABI static AnalysisKey Key;

public:
  /// ScalarEvolution result computed for a function.
  using Result = ScalarEvolution;

  /// Run ScalarEvolution on \p F using analyses from \p AM.
  ///
  /// \param F Function to analyze.
  /// \param AM Function analysis manager providing dependencies.
  /// @return ScalarEvolution analysis result for \p F.
  LLVM_ABI ScalarEvolution run(Function &F, FunctionAnalysisManager &AM);
};

/// Verifier pass for the \c ScalarEvolutionAnalysis results.
class ScalarEvolutionVerifierPass
    : public RequiredPassInfoMixin<ScalarEvolutionVerifierPass> {
public:
  /// Verify ScalarEvolution results for \p F.
  ///
  /// \param F Function whose SCEV analysis to verify.
  /// \param AM Function analysis manager providing ScalarEvolution.
  /// @return All analyses preserved (verification does not mutate IR).
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Printer pass for the \c ScalarEvolutionAnalysis results.
class ScalarEvolutionPrinterPass
    : public RequiredPassInfoMixin<ScalarEvolutionPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes SCEV results to \p OS.
  ///
  /// \param OS Stream to print to.
  explicit ScalarEvolutionPrinterPass(raw_ostream &OS) : OS(OS) {}

  /// Print ScalarEvolution results for \p F to the configured stream.
  ///
  /// \param F Function whose SCEV analysis to print.
  /// \param AM Function analysis manager providing ScalarEvolution.
  /// @return All analyses preserved (printing does not mutate IR).
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Legacy FunctionPass wrapper around ScalarEvolution.
class LLVM_ABI ScalarEvolutionWrapperPass : public FunctionPass {
  std::unique_ptr<ScalarEvolution> SE;

public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct a ScalarEvolution wrapper pass.
  ScalarEvolutionWrapperPass();

  /// Return the ScalarEvolution instance for the current function.
  /// @return The ScalarEvolution instance for the current function.
  ScalarEvolution &getSE() { return *SE; }
  /// Return the ScalarEvolution instance for the current function.
  /// @return The ScalarEvolution instance for the current function.
  const ScalarEvolution &getSE() const { return *SE; }

  /// Run ScalarEvolution on \p F.
  ///
  /// \param F Function to analyze.
  /// @return True if the pass succeeded on \p F.
  bool runOnFunction(Function &F) override;
  /// Release the ScalarEvolution instance computed for the last function.
  void releaseMemory() override;
  /// Declare the analyses this pass requires.
  ///
  /// \param AU Analysis usage to populate.
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  /// Print the ScalarEvolution analysis for the current function.
  ///
  /// \param OS Stream to print to.
  /// \param M Optional module (unused).
  void print(raw_ostream &OS, const Module *M = nullptr) const override;
  /// Verify the ScalarEvolution analysis for the current function.
  void verifyAnalysis() const override;
};

/// Interface that views SCEV expressions under a growing set of predicates.
///
/// An interface layer with SCEV used to manage how we see SCEV expressions
/// for values in the context of existing predicates. We can add new
/// predicates, but we cannot remove them.
///
/// This layer has multiple purposes:
///   - provides a simple interface for SCEV versioning.
///   - guarantees that the order of transformations applied on a SCEV
///     expression for a single Value is consistent across two different
///     getSCEV calls. This means that, for example, once we've obtained
///     an AddRec expression for a certain value through expression
///     rewriting, we will continue to get an AddRec expression for that
///     Value.
///   - lowers the number of expression rewrites.
class PredicatedScalarEvolution {
public:
  /// Construct a predicated SCEV analysis for loop \p L using \p SE.
  ///
  /// \param SE ScalarEvolution instance to wrap.
  /// \param L Loop whose values are rewritten under predicates.
  LLVM_ABI PredicatedScalarEvolution(ScalarEvolution &SE, Loop &L);

  /// Return the current union of SCEV predicates.
  /// @return The current union of SCEV predicates.
  LLVM_ABI const SCEVPredicate &getPredicate() const;

  /// Return the SCEV for \p V under the current predicate.
  ///
  /// The order of transformations applied on the expression of V
  /// returned by ScalarEvolution is guaranteed to be preserved, even when
  /// adding new predicates.
  ///
  /// \param V Value whose SCEV to rewrite.
  /// @return The SCEV for \p V under the current predicate.
  LLVM_ABI const SCEV *getSCEV(Value *V);

  /// Return the rewritten SCEV for \p Expr under the current predicate.
  ///
  /// The order of transformations applied on the expression of \p
  /// Expr returned by ScalarEvolution is guaranteed to be preserved, even when
  /// adding new predicates.
  ///
  /// \param Expr SCEV to rewrite under the current predicates.
  /// @return The rewritten SCEV for \p Expr under the current predicate.
  LLVM_ABI const SCEV *getPredicatedSCEV(const SCEV *Expr);

  /// Get the (predicated) backedge count for the analyzed loop.
  /// @return The (predicated) backedge count for the analyzed loop.
  LLVM_ABI const SCEV *getBackedgeTakenCount();

  /// Get the (predicated) symbolic max backedge count for the analyzed loop.
  /// @return The (predicated) symbolic max backedge count for the analyzed loop.
  LLVM_ABI const SCEV *getSymbolicMaxBackedgeTakenCount();

  /// Returns the upper bound of the loop trip count as a normal unsigned
  /// value, or 0 if the trip count is unknown.
  /// @return The upper bound of the loop trip count as a normal unsigned value, or 0 if the trip
  /// count is unknown.
  LLVM_ABI unsigned getSmallConstantMaxTripCount();

  /// Add \p Pred to the current predicate set.
  ///
  /// \param Pred Predicate to assume.
  LLVM_ABI void addPredicate(const SCEVPredicate &Pred);

  /// Add every predicate in \p Preds to the current predicate set.
  ///
  /// \param Preds Predicates to assume.
  LLVM_ABI void addPredicates(ArrayRef<const SCEVPredicate *> Preds);

  /// Attempt to rewrite \p V as an AddRecExpr by adding SCEV predicates.
  ///
  /// If we can't transform the expression into an AddRecExpr we
  /// return nullptr and not add additional SCEV predicates to the current
  /// context. If \p WrapPredsAdded is non-null, the required predicates are
  /// collected there instead of being added to this context.
  ///
  /// \param V Value to rewrite as an add recurrence.
  /// \param WrapPredsAdded Optional list that receives wrap predicates
  ///        instead of adding them to this context.
  /// @return Result of attempting to rewrite \p V as an AddRecExpr by adding SCEV predicates.
  LLVM_ABI const SCEVAddRecExpr *
  getAsAddRec(Value *V,
              SmallVectorImpl<const SCEVPredicate *> *WrapPredsAdded = nullptr);

  /// Return true if \p V is known not to wrap under \p Flags.
  ///
  /// \param V Value to test.
  /// \param Flags Wrap flags that must hold.
  /// @return True if \p V is known not to wrap under \p Flags.
  LLVM_ABI bool hasNoOverflow(Value *V,
                              SCEVWrapPredicate::IncrementWrapFlags Flags);

  /// Returns the ScalarEvolution analysis used.
  /// @return The ScalarEvolution analysis used.
  ScalarEvolution *getSE() const { return &SE; }

  /// Copy \p Other, including ownership of its SCEVUnionPredicate.
  ///
  /// We need to explicitly define the copy constructor due to the ownership of
  /// the SCEVUnionPredicate Preds.
  ///
  /// \param Other PredicatedScalarEvolution to copy.
  LLVM_ABI PredicatedScalarEvolution(const PredicatedScalarEvolution &Other);

  /// Print the SCEV mappings done by this analysis to \p OS.
  ///
  /// The printed text is indented by \p Depth.
  ///
  /// \param OS Stream to print to.
  /// \param Depth Indentation depth.
  LLVM_ABI void print(raw_ostream &OS, unsigned Depth) const;

  /// Return true if \p AR1 and \p AR2 are equal under current and extra
  /// predicates.
  ///
  /// Equal predicates in Preds and \p ExtraPreds are taken into account.
  ///
  /// \param AR1 First add recurrence.
  /// \param AR2 Second add recurrence.
  /// \param ExtraPreds Additional predicates to assume.
  /// @return True if \p AR1 and \p AR2 are equal under current and extra predicates.
  LLVM_ABI bool areAddRecsEqualWithPreds(
      const SCEVAddRecExpr *AR1, const SCEVAddRecExpr *AR2,
      ArrayRef<const SCEVPredicate *> ExtraPreds = {}) const;

private:
  /// Increments the version number of the predicate.  This needs to be called
  /// every time the SCEV predicate changes.
  void updateGeneration();

  /// Holds a SCEV and the version number of the SCEV predicate used to
  /// perform the rewrite of the expression.
  using RewriteEntry = std::pair<unsigned, const SCEV *>;

  /// Maps a SCEV to the rewrite result of that SCEV at a certain version
  /// number. If this number doesn't match the current Generation, we will
  /// need to do a rewrite. To preserve the transformation order of previous
  /// rewrites, we will rewrite the previous result instead of the original
  /// SCEV.
  DenseMap<const SCEV *, RewriteEntry> RewriteMap;

  /// The ScalarEvolution analysis.
  ScalarEvolution &SE;

  /// The analyzed Loop.
  const Loop &L;

  /// The SCEVPredicate that forms our context. We will rewrite all
  /// expressions assuming that this predicate true.
  std::unique_ptr<SCEVUnionPredicate> Preds;

  /// Marks the version of the SCEV predicate used. When rewriting a SCEV
  /// expression we mark it with the version of the predicate. We use this to
  /// figure out if the predicate has changed from the last rewrite of the
  /// SCEV. If so, we need to perform a new rewrite.
  unsigned Generation = 0;

  /// The backedge taken count.
  const SCEV *BackedgeCount = nullptr;

  /// The symbolic backedge taken count.
  const SCEV *SymbolicMaxBackedgeCount = nullptr;

  /// The constant max trip count for the loop.
  std::optional<unsigned> SmallConstantMaxTripCount;
};

/// DenseMapInfo specialization for ScalarEvolution::FoldID.
template <> struct DenseMapInfo<ScalarEvolution::FoldID> {
  /// Return the hash of \p Val.
  ///
  /// \param Val Fold identity to hash.
  /// @return Hash of \p Val.
  static unsigned getHashValue(const ScalarEvolution::FoldID &Val) {
    return Val.computeHash();
  }

  /// Return true if \p LHS and \p RHS represent the same fold identity.
  ///
  /// \param LHS Left-hand fold identity.
  /// \param RHS Right-hand fold identity.
  /// @return True if \p LHS and \p RHS represent the same fold identity.
  static bool isEqual(const ScalarEvolution::FoldID &LHS,
                      const ScalarEvolution::FoldID &RHS) {
    return LHS == RHS;
  }
};

template <> inline const SCEV *SCEVUseT<const SCEV *>::getCanonical() const {
  return getPointer()->getCanonical();
}

/// Print this SCEVUse to \p OS, including any use-specific flags.
///
/// \param OS Stream to print to.
template <typename SCEVPtrT>
void SCEVUseT<SCEVPtrT>::print(raw_ostream &OS) const {
  getPointer()->print(OS);
  SCEV::NoWrapFlags Flags = getUseNoWrapFlags();
  if (any(Flags & SCEV::FlagNUW))
    OS << "<u nuw>";
  if (any(Flags & SCEV::FlagNSW))
    OS << "<u nsw>";
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
template <typename SCEVPtrT>
LLVM_DUMP_METHOD void SCEVUseT<SCEVPtrT>::dump() const {
  print(dbgs());
  dbgs() << '\n';
}
#endif

} // end namespace llvm

#endif // LLVM_ANALYSIS_SCALAREVOLUTION_H
