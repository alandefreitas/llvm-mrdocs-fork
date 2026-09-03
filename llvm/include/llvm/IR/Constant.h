//===-- llvm/Constant.h - Constant class definition -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the Constant class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_CONSTANT_H
#define LLVM_IR_CONSTANT_H

#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class ConstantRange;
class APInt;

/// LLVM Constant Representation
///
/// This is an important base class in LLVM. It provides the common facilities
/// of all constant values in an LLVM program. A constant is a value that is
/// immutable at runtime. Functions are constants because their address is
/// immutable. Same with global variables.
///
/// All constants share the capabilities provided in this class. All constants
/// can have a null value. They can have an operand list. Constants can be
/// simple (integer and floating point values), complex (arrays and structures),
/// or expression based (computations yielding a constant value composed of
/// only certain operators and other constant values).
///
/// Note that Constants are immutable (once created they never change)
/// and are fully shared by structural equivalence.  This means that two
/// structurally equivalent constants will always have the same address.
/// Constants are created on demand as needed and never deleted: thus clients
/// don't have to worry about the lifetime of the objects.
class Constant : public User {
protected:
  /// SubclassOptionalData bits. Low bits are used by ConstantExpr.
  enum {
    /// Bit indicating this constant is a null value.
    IsNullValue = (1 << 6),
  };

  /// Bits reserved in SubclassOptionalData, not to be used for ConstantExpr
  /// flags.
  static constexpr unsigned ConstantSubclassBits = IsNullValue;

  /// Construct a constant of type \p ty with value ID \p vty.
  /// \param ty The type of the constant.
  /// \param vty The ValueTy subclass identifier.
  /// \param AllocInfo Operand allocation information for User.
  Constant(Type *ty, ValueTy vty, AllocInfo AllocInfo)
      : User(ty, vty, AllocInfo) {}

  /// Destroy this constant.
  ~Constant() = default;

public:
  /// Constants are uniqued and cannot be assigned.
  /// \param Other The constant that would be assigned (deleted).
  void operator=(const Constant &Other) = delete;
  /// Constants are uniqued and cannot be copied.
  /// \param Other The constant that would be copied (deleted).
  Constant(const Constant &Other) = delete;

  /// Return true if this is the value that would be returned by getNullValue.
  /// \return True if this is the null value for its type.
  bool isNullValue() const { return SubclassOptionalData & IsNullValue; }

  /// Returns true if the value is one.
  /// \return True if the value is one.
  LLVM_ABI bool isOneValue() const;

  /// Return true if the value is not the one value, or,
  /// for vectors, does not contain one value elements.
  /// \return True if the value is not one (and for vectors, has no one
  /// elements).
  LLVM_ABI bool isNotOneValue() const;

  /// Return true if this is the value that would be returned by
  /// getAllOnesValue.
  /// \return True if this is the all-ones value for its type.
  LLVM_ABI bool isAllOnesValue() const;

  /// Return true if the value is what would be returned by
  /// getZeroValueForNegation.
  /// \return True if the value is the zero value for negation.
  LLVM_ABI bool isNegativeZeroValue() const;

  /// Return true if the value is not the smallest signed value, or,
  /// for vectors, does not contain smallest signed value elements.
  /// \return True if the value is not the smallest signed value (and for
  /// vectors, has no such elements).
  LLVM_ABI bool isNotMinSignedValue() const;

  /// Return true if the value is the smallest signed value.
  /// \return True if the value is the smallest signed value.
  LLVM_ABI bool isMinSignedValue() const;

  /// Return true if the value is the largest signed value.
  /// \return True if the value is the largest signed value.
  LLVM_ABI bool isMaxSignedValue() const;

  /// Return true if this is a finite and non-zero floating-point scalar
  /// constant or a fixed width vector constant with all finite and non-zero
  /// elements.
  /// \return True if this is a finite, non-zero floating-point constant.
  LLVM_ABI bool isFiniteNonZeroFP() const;

  /// Return true if this is a normal floating-point constant.
  ///
  /// True for a normal (as opposed to denormal, infinity, nan, or zero)
  /// floating-point scalar constant or a vector constant with all normal
  /// elements. See APFloat::isNormal.
  /// \return True if this is a normal floating-point constant.
  LLVM_ABI bool isNormalFP() const;

  /// Return true if this scalar has an exact multiplicative inverse or this
  /// vector has an exact multiplicative inverse for each element in the vector.
  /// \return True if this scalar or each vector element has an exact
  /// multiplicative inverse.
  LLVM_ABI bool hasExactInverseFP() const;

  /// Return true if this is a floating-point NaN constant or a vector
  /// floating-point constant with all NaN elements.
  /// \return True if this is a floating-point NaN constant.
  LLVM_ABI bool isNaN() const;

  /// Return true if this constant and \p Y are element-wise equal.
  ///
  /// This is identical to just comparing the pointers, with the exception that
  /// for vectors, if only one of the constants has an `undef` element in some
  /// lane, the constants still match.
  /// \param Y The other value to compare against.
  /// \return True if this constant and \p Y are element-wise equal.
  LLVM_ABI bool isElementWiseEqual(Value *Y) const;

  /// Return true if this vector constant includes undef or poison elements.
  ///
  /// Since it is impossible to inspect a scalable vector element-wise at
  /// compile time, this function returns true only if the entire vector is
  /// undef or poison.
  /// \return True if this vector constant includes undef or poison elements.
  LLVM_ABI bool containsUndefOrPoisonElement() const;

  /// Return true if this is a vector constant that includes any poison
  /// elements.
  /// \return True if this vector constant includes any poison elements.
  LLVM_ABI bool containsPoisonElement() const;

  /// Return true if this is a vector constant that includes any strictly undef
  /// (not poison) elements.
  /// \return True if this vector constant includes any strictly undef elements.
  LLVM_ABI bool containsUndefElement() const;

  /// Return true if this is a fixed width vector constant that includes
  /// any constant expressions.
  /// \return True if this fixed-width vector includes any constant expressions.
  LLVM_ABI bool containsConstantExpression() const;

  /// Return true if this is a vector constant where at least one element
  /// satisfies the given predicate. Scalable vectors are not checked.
  /// \param PredFn Predicate applied to each vector element constant.
  /// \return True if at least one vector element satisfies \p PredFn.
  LLVM_ABI bool
  containsMatchingVectorElement(function_ref<bool(Constant *)> PredFn) const;

  /// Return true if the value can vary between threads.
  /// \return True if the value can vary between threads.
  LLVM_ABI bool isThreadDependent() const;

  /// Return true if the value is dependent on a dllimport variable.
  /// \return True if the value is dependent on a dllimport variable.
  LLVM_ABI bool isDLLImportDependent() const;

  /// Return true if the constant has users other than constant expressions and
  /// other dangling things.
  /// \return True if the constant has non-dangling, non-ConstantExpr users.
  LLVM_ABI bool isConstantUsed() const;

  /// Return true if this constant may generate a relocation entry.
  ///
  /// Classifies the entry according to whether or not it may generate a
  /// relocation entry (either static or dynamic). This must be conservative, so
  /// if it might codegen to a relocatable entry, it should say so.
  ///
  /// FIXME: This really should not be in IR.
  /// \return True if this constant may generate a relocation entry.
  LLVM_ABI bool needsRelocation() const;
  /// Return true if this constant may generate a dynamic relocation entry.
  /// \return True if this constant may generate a dynamic relocation entry.
  LLVM_ABI bool needsDynamicRelocation() const;

  /// Return the constant for the specified aggregate element, or null.
  ///
  /// For aggregates (struct/array/vector) return the constant that corresponds
  /// to the specified element if possible, or null if not. This can return null
  /// if the element index is a ConstantExpr, if 'this' is a constant expr or
  /// if the constant does not fit into an uint64_t.
  /// \param Elt Zero-based element index.
  /// \return The constant for the element, or null if unavailable.
  LLVM_ABI Constant *getAggregateElement(unsigned Elt) const;
  /// Return the constant for the specified aggregate element, or null.
  /// \param Elt Constant integer index of the element.
  /// \return The constant for the element, or null if unavailable.
  LLVM_ABI Constant *getAggregateElement(Constant *Elt) const;

  /// If all elements of the vector constant have the same value, return that
  /// value. Otherwise, return nullptr. Ignore poison elements by setting
  /// AllowPoison to true.
  /// \param AllowPoison If true, poison elements are ignored when checking for
  /// a splat.
  /// \return The common element value, or nullptr if not a splat.
  LLVM_ABI Constant *getSplatValue(bool AllowPoison = false) const;

  /// If C is a constant integer then return its value, otherwise C must be a
  /// vector of constant integers, all equal, and the common value is returned.
  /// \return The unique integer value of this constant.
  LLVM_ABI const APInt &getUniqueInteger() const;

  /// Convert constant to an approximate constant range. For vectors, the
  /// range is the union over the element ranges. Poison elements are ignored.
  /// \return An approximate constant range for this constant.
  LLVM_ABI ConstantRange toConstantRange() const;

  /// Destroy this constant and any constant users that depend on it.
  ///
  /// Called if some element of this constant is no longer valid.
  /// At this point only other constants may be on the use_list for this
  /// constant.  Any constants on our Use list must also be destroy'd.  The
  /// implementation must be sure to remove the constant from the list of
  /// available cached constants.  Implementations should implement
  /// destroyConstantImpl to remove constants from any pools/maps they are
  /// contained it.
  LLVM_ABI void destroyConstant();

  //// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// Return true if \p V is a Constant.
  /// \param V The value to test.
  /// \return True if \p V is a Constant.
  static bool classof(const Value *V) {
    static_assert(ConstantFirstVal == 0, "V->getValueID() >= ConstantFirstVal always succeeds");
    return V->getValueID() <= ConstantLastVal;
  }

  /// Replace all uses of \p From with \p To within this constant.
  ///
  /// This method is a special form of User::replaceUsesOfWith (which does not
  /// work on constants) that does work on constants.  Basically this method
  /// goes through the trouble of building a new constant that is equivalent to
  /// the current one, with all uses of From replaced with uses of To.  After
  /// this construction is completed, all of the users of 'this' are replaced to
  /// use the new constant, and then 'this' is deleted.  In general, you should
  /// not call this method, instead, use Value::replaceAllUsesWith, which
  /// automatically dispatches to this method as needed.
  /// \param From The operand value to replace.
  /// \param To The replacement value.
  LLVM_ABI void handleOperandChange(Value *From, Value *To);

  /// Return the null constant of type \p Ty (zero for integers, null for
  /// pointers, and so on).
  /// \param Ty The type of the null constant to create.
  /// \return The null constant of type \p Ty.
  LLVM_ABI static Constant *getNullValue(Type *Ty);

  /// Return the all-ones constant of type \p Ty.
  ///
  /// Returns the value for an integer or vector of integer constant of the
  /// given type that has all its bits set to true.
  /// \param Ty The integer or vector-of-integer type.
  /// \return The all-ones constant of type \p Ty.
  LLVM_ABI static Constant *getAllOnesValue(Type *Ty);

  /// Return the value for an integer or pointer constant, or a vector thereof,
  /// with the given scalar value.
  /// \param Ty The integer, pointer, or vector type of the result.
  /// \param V The scalar integer value to splat or use.
  /// \return The constant with the given integer value.
  LLVM_ABI static Constant *getIntegerValue(Type *Ty, const APInt &V);

  /// Remove any dead constant users dangling off of this constant.
  ///
  /// This method is useful for clients that want to check to see if a global is
  /// unused, but don't want to deal with potentially dead constants hanging off
  /// of the globals.
  LLVM_ABI void removeDeadConstantUsers() const;

  /// Return true if the constant has exactly one live use.
  ///
  /// This returns the same result as calling Value::hasOneUse after
  /// Constant::removeDeadConstantUsers, but doesn't remove dead constants.
  /// \return True if the constant has exactly one live use.
  LLVM_ABI bool hasOneLiveUse() const;

  /// Return true if the constant has no live uses.
  ///
  /// This returns the same result as calling Value::use_empty after
  /// Constant::removeDeadConstantUsers, but doesn't remove dead constants.
  /// \return True if the constant has no live uses.
  LLVM_ABI bool hasZeroLiveUses() const;

  /// Strip pointer casts, all-zero GEPs, and address-space casts, returning a
  /// \c Constant.
  /// \return This constant with pointer casts stripped.
  const Constant *stripPointerCasts() const {
    return cast<Constant>(Value::stripPointerCasts());
  }

  /// Non-const overload of \c stripPointerCasts().
  /// \return This constant with pointer casts stripped.
  Constant *stripPointerCasts() {
    return const_cast<Constant*>(
                      static_cast<const Constant *>(this)->stripPointerCasts());
  }

  /// Try to replace undefined constant \p C or undefined elements in \p C with
  /// \p Replacement. If no changes are made, the constant \p C is returned.
  /// \param C The constant whose undefs may be replaced.
  /// \param Replacement The constant used in place of undefs.
  /// \return The constant with undefs replaced, or \p C if unchanged.
  LLVM_ABI static Constant *replaceUndefsWith(Constant *C,
                                              Constant *Replacement);

  /// Merge undef elements from \p Other into constant \p C.
  ///
  /// Merges undefs of a Constant with another Constant, along with the undefs
  /// already present. Other doesn't have to be the same type as C, but both
  /// must either be scalars or vectors with the same element count. If no
  /// changes are made, the constant C is returned.
  /// \param C The constant whose undefs are merged.
  /// \param Other The constant providing additional undef lanes.
  /// \return The merged constant, or \p C if unchanged.
  LLVM_ABI static Constant *mergeUndefsWith(Constant *C, Constant *Other);

  /// Return true if a constant is ConstantData or a ConstantAggregate or
  /// ConstantExpr that contain only ConstantData.
  /// \return True if this constant is or contains only ConstantData.
  LLVM_ABI bool isManifestConstant() const;

private:
  enum PossibleRelocationsTy {
    /// This constant requires no relocations. That is, it holds simple
    /// constants (like integrals).
    NoRelocation = 0,

    /// This constant holds static relocations that can be resolved by the
    /// static linker.
    LocalRelocation = 1,

    /// This constant holds dynamic relocations that the dynamic linker will
    /// need to resolve.
    GlobalRelocation = 2,
  };

  /// Determine what potential relocations may be needed by this constant.
  PossibleRelocationsTy getRelocationInfo() const;

  bool hasNLiveUses(unsigned N) const;
};

} // end namespace llvm

#endif // LLVM_IR_CONSTANT_H
