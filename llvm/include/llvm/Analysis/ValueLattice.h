//===- ValueLattice.h - Value constraint analysis ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_VALUELATTICE_H
#define LLVM_ANALYSIS_VALUELATTICE_H

#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/Compiler.h"

//===----------------------------------------------------------------------===//
//                               ValueLatticeElement
//===----------------------------------------------------------------------===//

namespace llvm {

/// This class represents lattice values for constants.
///
/// FIXME: This is basically just for bringup, this can be made a lot more rich
/// in the future.
///
class ValueLatticeElement {
  enum ValueLatticeElementTy {
    /// This Value has no known value yet.  As a result, this implies the
    /// producing instruction is dead.  Caution: We use this as the starting
    /// state in our local meet rules.  In this usage, it's taken to mean
    /// "nothing known yet".
    /// Transition to any other state allowed.
    unknown,

    /// This Value is an UndefValue constant or produces undef. Undefined values
    /// can be merged with constants (or single element constant ranges),
    /// assuming all uses of the result will be replaced.
    /// Transition allowed to the following states:
    ///  constant
    ///  constantrange_including_undef
    ///  overdefined
    undef,

    /// This Value has a specific constant value.  The constant cannot be undef.
    /// (For constant integers, constantrange is used instead. Integer typed
    /// constantexprs can appear as constant.) Note that the constant state
    /// can be reached by merging undef & constant states.
    /// Transition allowed to the following states:
    ///  overdefined
    constant,

    /// This Value is known to not have the specified value. (For constant
    /// integers, constantrange is used instead.  As above, integer typed
    /// constantexprs can appear here.)
    /// Transition allowed to the following states:
    ///  overdefined
    notconstant,

    /// The Value falls within this range. (Used only for integer typed values.)
    /// Transition allowed to the following states:
    ///  constantrange (new range must be a superset of the existing range)
    ///  constantrange_including_undef
    ///  overdefined
    constantrange,

    /// This Value falls within this range, but also may be undef.
    /// Merging it with other constant ranges results in
    /// constantrange_including_undef.
    /// Transition allowed to the following states:
    ///  overdefined
    constantrange_including_undef,

    /// We can not precisely model the dynamic values this value might take.
    /// No transitions are allowed after reaching overdefined.
    overdefined,
  };

  ValueLatticeElementTy Tag : 8;
  /// Number of times a constant range has been extended with widening enabled.
  unsigned NumRangeExtensions : 8;

  // Pointer constants derived from equality predicates may have different
  // provenance than the original value. Limit constant propagation if this
  // happens to be the case.
  bool MayHaveDifferentProvenance = false;

  /// The union either stores a pointer to a constant or a constant range,
  /// associated to the lattice element. We have to ensure that Range is
  /// initialized or destroyed when changing state to or from constantrange.
  union {
    /// Pointer to the known constant or not-constant value.
    Constant *ConstVal;
    /// Known integer constant range for this lattice element.
    ConstantRange Range;
  };

  /// Destroy contents of lattice value, without destructing the object.
  void destroy() {
    switch (Tag) {
    case overdefined:
    case unknown:
    case undef:
    case constant:
    case notconstant:
      break;
    case constantrange_including_undef:
    case constantrange:
      Range.~ConstantRange();
      break;
    };
  }

public:
  /// Struct to control some aspects related to merging constant ranges.
  struct MergeOptions {
    /// The merge value may include undef.
    bool MayIncludeUndef;

    /// Handle repeatedly extending a range by going to overdefined after a
    /// number of steps.
    bool CheckWiden;

    /// The number of allowed widening steps (including setting the range
    /// initially).
    unsigned MaxWidenSteps;

    /// Construct MergeOptions with defaults (no undef, no widening).
    MergeOptions() : MergeOptions(false, false) {}

    /// Construct MergeOptions with the given merge behavior.
    /// @param MayIncludeUndef Whether the merge result may include undef.
    /// @param CheckWiden Whether to widen to overdefined after MaxWidenSteps.
    /// @param MaxWidenSteps Maximum widening steps before going overdefined.
    MergeOptions(bool MayIncludeUndef, bool CheckWiden,
                 unsigned MaxWidenSteps = 1)
        : MayIncludeUndef(MayIncludeUndef), CheckWiden(CheckWiden),
          MaxWidenSteps(MaxWidenSteps) {}

    /// Set whether the merge value may include undef.
    /// @param V Whether undef may be included.
    /// @return Reference to this options object.
    MergeOptions &setMayIncludeUndef(bool V = true) {
      MayIncludeUndef = V;
      return *this;
    }

    /// Set whether range widening is checked.
    /// @param V Whether to check widening.
    /// @return Reference to this options object.
    MergeOptions &setCheckWiden(bool V = true) {
      CheckWiden = V;
      return *this;
    }

    /// Set the maximum number of widening steps and enable widening checks.
    /// @param Steps Maximum allowed widening steps.
    /// @return Reference to this options object.
    MergeOptions &setMaxWidenSteps(unsigned Steps = 1) {
      CheckWiden = true;
      MaxWidenSteps = Steps;
      return *this;
    }
  };

  /// Construct an unknown lattice element.
  // ConstVal and Range are initialized on-demand.
  ValueLatticeElement() : Tag(unknown), NumRangeExtensions(0) {}

  /// Destroy the lattice element and any owned range.
  ~ValueLatticeElement() { destroy(); }

  /// Copy-construct a lattice element from \p Other.
  /// @param Other Lattice element to copy.
  ValueLatticeElement(const ValueLatticeElement &Other)
      : Tag(Other.Tag), NumRangeExtensions(0),
        MayHaveDifferentProvenance(Other.MayHaveDifferentProvenance) {
    switch (Other.Tag) {
    case constantrange:
    case constantrange_including_undef:
      new (&Range) ConstantRange(Other.Range);
      NumRangeExtensions = Other.NumRangeExtensions;
      break;
    case constant:
    case notconstant:
      ConstVal = Other.ConstVal;
      break;
    case overdefined:
    case unknown:
    case undef:
      break;
    }
  }

  /// Move-construct a lattice element from \p Other.
  /// @param Other Lattice element to move from.
  ValueLatticeElement(ValueLatticeElement &&Other)
      : Tag(Other.Tag), NumRangeExtensions(0),
        MayHaveDifferentProvenance(Other.MayHaveDifferentProvenance) {
    switch (Other.Tag) {
    case constantrange:
    case constantrange_including_undef:
      new (&Range) ConstantRange(std::move(Other.Range));
      NumRangeExtensions = Other.NumRangeExtensions;
      break;
    case constant:
    case notconstant:
      ConstVal = Other.ConstVal;
      break;
    case overdefined:
    case unknown:
    case undef:
      break;
    }
    Other.Tag = unknown;
  }

  /// Copy-assign from \p Other.
  /// @param Other Lattice element to copy.
  /// @return Reference to this lattice element.
  ValueLatticeElement &operator=(const ValueLatticeElement &Other) {
    destroy();
    new (this) ValueLatticeElement(Other);
    return *this;
  }

  /// Move-assign from \p Other.
  /// @param Other Lattice element to move from.
  /// @return Reference to this lattice element.
  ValueLatticeElement &operator=(ValueLatticeElement &&Other) {
    destroy();
    new (this) ValueLatticeElement(std::move(Other));
    return *this;
  }

  /// Create a lattice element for constant \p C.
  /// @param C Constant value to represent.
  /// @return Lattice element marked as constant \p C.
  static ValueLatticeElement get(Constant *C) {
    ValueLatticeElement Res;
    Res.markConstant(C);
    return Res;
  }
  /// Create a lattice element for a value known not equal to \p C.
  /// @param C Constant the value is known not to equal.
  /// @return Lattice element marked as not-constant \p C.
  static ValueLatticeElement getNot(Constant *C) {
    ValueLatticeElement Res;
    assert(!isa<UndefValue>(C) && "!= undef is not supported");
    Res.markNotConstant(C);
    return Res;
  }
  /// Create a lattice element for constant range \p CR.
  /// @param CR Constant range to represent.
  /// @param MayIncludeUndef Whether the range may also be undef.
  /// @return Lattice element for \p CR, or overdefined if \p CR is full.
  static ValueLatticeElement getRange(ConstantRange CR,
                                      bool MayIncludeUndef = false) {
    if (CR.isFullSet())
      return getOverdefined();

    if (CR.isEmptySet()) {
      ValueLatticeElement Res;
      if (MayIncludeUndef)
        Res.markUndef();
      return Res;
    }

    ValueLatticeElement Res;
    Res.markConstantRange(std::move(CR),
                          MergeOptions().setMayIncludeUndef(MayIncludeUndef));
    return Res;
  }
  /// Create an overdefined lattice element.
  /// @return An overdefined lattice element.
  static ValueLatticeElement getOverdefined() {
    ValueLatticeElement Res;
    Res.markOverdefined();
    return Res;
  }

  /// Return true if this lattice element is undef.
  /// @return True if this lattice element is undef.
  bool isUndef() const { return Tag == undef; }
  /// Return true if this lattice element is unknown.
  /// @return True if this lattice element is unknown.
  bool isUnknown() const { return Tag == unknown; }
  /// Return true if this lattice element is unknown or undef.
  /// @return True if this lattice element is unknown or undef.
  bool isUnknownOrUndef() const { return Tag == unknown || Tag == undef; }
  /// Return true if this lattice element is a specific constant.
  /// @return True if this lattice element is a specific constant.
  bool isConstant() const { return Tag == constant; }
  /// Return true if this lattice element is a not-constant value.
  /// @return True if this lattice element is a not-constant value.
  bool isNotConstant() const { return Tag == notconstant; }
  /// Return true if this is a constant range that may also be undef.
  /// @return True if this is a constant range that may also be undef.
  bool isConstantRangeIncludingUndef() const {
    return Tag == constantrange_including_undef;
  }
  /// Return true if this value is a constant range.
  ///
  /// Use \p UndefAllowed to exclude non-singleton constant ranges that may
  /// also be undef. Note that this function also returns true if the range may
  /// include undef, but only contains a single element. In that case, it can
  /// be replaced by a constant.
  /// @param UndefAllowed Whether ranges that may include undef count as ranges.
  /// @return True if this value is a constant range.
  bool isConstantRange(bool UndefAllowed = true) const {
    return Tag == constantrange || (Tag == constantrange_including_undef &&
                                    (UndefAllowed || Range.isSingleElement()));
  }
  /// Return true if this lattice element is overdefined.
  /// @return True if this lattice element is overdefined.
  bool isOverdefined() const { return Tag == overdefined; }

  /// Return the known constant value.
  /// @return The constant associated with this lattice element.
  Constant *getConstant() const {
    assert(isConstant() && "Cannot get the constant of a non-constant!");
    return ConstVal;
  }

  /// Return the constant this value is known not to equal.
  /// @return The not-constant associated with this lattice element.
  Constant *getNotConstant() const {
    assert(isNotConstant() && "Cannot get the constant of a non-notconstant!");
    return ConstVal;
  }

  /// Return the constant range for this value.
  ///
  /// Use \p UndefAllowed to exclude non-singleton constant ranges that may
  /// also be undef. Note that this function also returns a range if the range
  /// may include undef, but only contains a single element. In that case, it
  /// can be replaced by a constant.
  /// @param UndefAllowed Whether ranges that may include undef are allowed.
  /// @return The constant range associated with this lattice element.
  const ConstantRange &getConstantRange(bool UndefAllowed = true) const {
    assert(isConstantRange(UndefAllowed) &&
           "Cannot get the constant-range of a non-constant-range!");
    return Range;
  }

  /// Return this value as a constant integer if it is one.
  /// @return The integer value, or nullopt if not a constant integer.
  std::optional<APInt> asConstantInteger() const {
    if (isConstant() && isa<ConstantInt>(getConstant())) {
      return cast<ConstantInt>(getConstant())->getValue();
    } else if (isConstantRange() && getConstantRange().isSingleElement()) {
      return *getConstantRange().getSingleElement();
    }
    return std::nullopt;
  }

  /// Return this lattice element as a constant range of bit width \p BW.
  /// @param BW Bit width of the range to produce.
  /// @param UndefAllowed Whether ranges that may include undef are allowed.
  /// @return A constant range approximating this lattice element.
  ConstantRange asConstantRange(unsigned BW, bool UndefAllowed = false) const {
    if (isConstantRange(UndefAllowed))
      return getConstantRange();
    if (isConstant())
      return getConstant()->toConstantRange();
    if (isUnknown())
      return ConstantRange::getEmpty(BW);
    return ConstantRange::getFull(BW);
  }

  /// Return this lattice element as a constant range for type \p Ty.
  /// @param Ty Integer or integer-vector type providing the bit width.
  /// @param UndefAllowed Whether ranges that may include undef are allowed.
  /// @return A constant range approximating this lattice element.
  ConstantRange asConstantRange(Type *Ty, bool UndefAllowed = false) const {
    assert(Ty->isIntOrIntVectorTy() && "Must be integer type");
    return asConstantRange(Ty->getScalarSizeInBits(), UndefAllowed);
  }

  /// Mark this lattice element as overdefined.
  /// @return True if the state changed.
  bool markOverdefined() {
    if (isOverdefined())
      return false;
    destroy();
    Tag = overdefined;
    return true;
  }

  /// Mark this lattice element as undef.
  /// @return True if the state changed.
  bool markUndef() {
    if (isUndef())
      return false;

    assert(isUnknown());
    Tag = undef;
    return true;
  }

  /// Mark this lattice element as constant \p V.
  /// @param V Constant value to set.
  /// @param MayIncludeUndef Whether the value may also be undef.
  /// @return True if the state changed.
  bool markConstant(Constant *V, bool MayIncludeUndef = false) {
    if (isa<UndefValue>(V))
      return markUndef();

    if (isConstant()) {
      assert(getConstant() == V && "Marking constant with different value");
      return false;
    }

    if (ConstantInt *CI = dyn_cast<ConstantInt>(V))
      return markConstantRange(
          ConstantRange(CI->getValue()),
          MergeOptions().setMayIncludeUndef(MayIncludeUndef));

    assert(isUnknown() || isUndef());
    Tag = constant;
    ConstVal = V;
    return true;
  }

  /// Mark this lattice element as not equal to constant \p V.
  /// @param V Constant the value is known not to equal.
  /// @return True if the state changed.
  bool markNotConstant(Constant *V) {
    assert(V && "Marking constant with NULL");
    if (ConstantInt *CI = dyn_cast<ConstantInt>(V))
      return markConstantRange(
          ConstantRange(CI->getValue() + 1, CI->getValue()));

    if (isa<UndefValue>(V))
      return false;

    if (isNotConstant()) {
      assert(getNotConstant() == V && "Marking !constant with different value");
      return false;
    }

    assert(isUnknown());
    Tag = notconstant;
    ConstVal = V;
    return true;
  }

  /// Mark this lattice element as constant range \p NewR.
  ///
  /// If the object is already a constant range, nothing changes if the
  /// existing range is equal to \p NewR and the tag. Otherwise \p NewR must be
  /// a superset of the existing range or the object must be undef. The tag is
  /// set to constant_range_including_undef if either the existing value or the
  /// new range may include undef.
  /// @param NewR Constant range to set.
  /// @param Opts Options controlling undef inclusion and widening.
  /// @return True if the state changed.
  bool markConstantRange(ConstantRange NewR,
                         MergeOptions Opts = MergeOptions()) {
    assert(!NewR.isEmptySet() && "should only be called for non-empty sets");

    if (NewR.isFullSet())
      return markOverdefined();

    ValueLatticeElementTy OldTag = Tag;
    ValueLatticeElementTy NewTag =
        (isUndef() || isConstantRangeIncludingUndef() || Opts.MayIncludeUndef)
            ? constantrange_including_undef
            : constantrange;
    if (isConstantRange()) {
      Tag = NewTag;
      if (getConstantRange() == NewR)
        return Tag != OldTag;

      // Simple form of widening. If a range is extended multiple times, go to
      // overdefined.
      if (Opts.CheckWiden && ++NumRangeExtensions > Opts.MaxWidenSteps)
        return markOverdefined();

      assert(NewR.contains(getConstantRange()) &&
             "Existing range must be a subset of NewR");
      Range = std::move(NewR);
      return true;
    }

    assert(isUnknown() || isUndef() || isConstant());
    assert((!isConstant() || NewR.contains(getConstant()->toConstantRange())) &&
           "Constant must be subset of new range");

    NumRangeExtensions = 0;
    Tag = NewTag;
    new (&Range) ConstantRange(std::move(NewR));
    return true;
  }

  /// Updates this object to approximate both this object and RHS. Returns
  /// true if this object has been changed.
  /// @param RHS Lattice element to merge into this one.
  /// @param Opts Options controlling undef inclusion and widening.
  /// @return True if this object changed.
  bool mergeIn(const ValueLatticeElement &RHS,
               MergeOptions Opts = MergeOptions()) {
    if (RHS.isUnknown() || isOverdefined())
      return false;
    if (RHS.isOverdefined()) {
      markOverdefined();
      return true;
    }

    if (isUndef()) {
      assert(!RHS.isUnknown());
      if (RHS.isUndef())
        return false;
      if (RHS.isConstant())
        return markConstant(RHS.getConstant(), true);
      if (RHS.isConstantRange())
        return markConstantRange(RHS.getConstantRange(true),
                                 Opts.setMayIncludeUndef());
      return markOverdefined();
    }

    if (isUnknown()) {
      assert(!RHS.isUnknown() && "Unknow RHS should be handled earlier");
      *this = RHS;
      return true;
    }

    if (isConstant()) {
      if (RHS.isConstant() && getConstant() == RHS.getConstant()) {
        // Equal constants may still differ in provenance, propagate it when
        // merging values.
        bool Current = MayHaveDifferentProvenance;
        MayHaveDifferentProvenance |= RHS.mayHaveDifferentProvenance();
        return MayHaveDifferentProvenance != Current;
      }
      if (RHS.isUndef())
        return false;
      // If the constant is a vector of integers, try to treat it as a range.
      if (getConstant()->getType()->isVectorTy() &&
          getConstant()->getType()->getScalarType()->isIntegerTy()) {
        ConstantRange L = getConstant()->toConstantRange();
        ConstantRange NewR = L.unionWith(
            RHS.asConstantRange(L.getBitWidth(), /*UndefAllowed=*/true));
        return markConstantRange(
            std::move(NewR),
            Opts.setMayIncludeUndef(RHS.isConstantRangeIncludingUndef()));
      }
      markOverdefined();
      return true;
    }

    if (isNotConstant()) {
      if (RHS.isNotConstant() && getNotConstant() == RHS.getNotConstant())
        return false;
      markOverdefined();
      return true;
    }

    auto OldTag = Tag;
    assert(isConstantRange() && "New ValueLattice type?");
    if (RHS.isUndef()) {
      Tag = constantrange_including_undef;
      return OldTag != Tag;
    }

    const ConstantRange &L = getConstantRange();
    ConstantRange NewR = L.unionWith(
        RHS.asConstantRange(L.getBitWidth(), /*UndefAllowed=*/true));
    return markConstantRange(
        std::move(NewR),
        Opts.setMayIncludeUndef(RHS.isConstantRangeIncludingUndef()));
  }

  /// Compare this symbolic value with \p Other using \p Pred.
  ///
  /// Returns either true, false or undef constants, or nullptr if the
  /// comparison cannot be evaluated.
  /// @param Pred Comparison predicate to evaluate.
  /// @param Ty Result type of the comparison.
  /// @param Other Lattice element to compare against.
  /// @param DL Data layout used for the comparison.
  /// @return A true/false/undef constant, or nullptr if unevaluable.
  LLVM_ABI Constant *getCompare(CmpInst::Predicate Pred, Type *Ty,
                                const ValueLatticeElement &Other,
                                const DataLayout &DL) const;

  /// Combine two sets of facts about the same value into one.
  ///
  /// Note that this method is not suitable for merging facts along different
  /// paths in a CFG; that's what the mergeIn function is for.  This is for
  /// merging facts gathered about the same value at the same location through
  /// two independent means.
  /// Notes:
  /// * This method does not promise to return the most precise possible lattice
  ///   value implied by A and B.  It is allowed to return any lattice element
  ///   which is at least as strong as *either* A or B (unless our facts
  ///   conflict, see below).
  /// * Due to unreachable code, the intersection of two lattice values could be
  ///   contradictory.  If this happens, we return some valid lattice value so
  ///   as not confuse the rest of LVI.  Ideally, we'd always return Undefined,
  ///   but we do not make this guarantee.  TODO: This would be a useful
  ///   enhancement.
  /// @param Other Lattice element with facts to intersect with this one.
  /// @return Lattice element combining facts from this and \p Other.
  LLVM_ABI ValueLatticeElement
  intersect(const ValueLatticeElement &Other) const;

  /// Return the number of constant-range extensions performed.
  /// @return Number of range extensions with widening enabled.
  unsigned getNumRangeExtensions() const { return NumRangeExtensions; }
  /// Set the number of constant-range extensions.
  /// @param N New extension count.
  void setNumRangeExtensions(unsigned N) { NumRangeExtensions = N; }

  /// Return whether this constant may have different provenance.
  /// @return True if provenance may differ from the original value.
  bool mayHaveDifferentProvenance() const { return MayHaveDifferentProvenance; }
  /// Set whether this constant may have different provenance.
  /// @param V Whether provenance may differ.
  void setMayHaveDifferentProvenance(bool V) { MayHaveDifferentProvenance = V; }
};

static_assert(sizeof(ValueLatticeElement) <= 40,
              "size of ValueLatticeElement changed unexpectedly");

/// Print lattice element \p Val to stream \p OS.
/// @param OS Output stream.
/// @param Val Lattice element to print.
/// @return Reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS,
                                 const ValueLatticeElement &Val);
} // end namespace llvm
#endif
