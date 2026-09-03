//===- ValueHandle.h - Value Smart Pointer classes --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the ValueHandle class and its sub-classes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_VALUEHANDLE_H
#define LLVM_IR_VALUEHANDLE_H

#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include <cassert>

namespace llvm {

/// This is the common base class of value handles.
///
/// ValueHandle's are smart pointers to Value's that have special behavior when
/// the value is deleted or ReplaceAllUsesWith'd.  See the specific handles
/// below for details.
class ValueHandleBase {
  friend class Value;
  template <typename ValueTy> friend class PoisoningVH;

protected:
  /// This indicates what sub class the handle actually is.
  ///
  /// This is to avoid having a vtable for the light-weight handle pointers. The
  /// fully general Callback version does have a vtable.
  enum HandleBaseKind {
    /// Asserting handle that traps if the Value is deleted.
    Assert,
    /// Callback handle that invokes virtual methods on delete/RAUW.
    Callback,
    /// Weak handle that nulls itself when the Value is deleted.
    Weak,
    /// Weak tracking handle that follows RAUW and nulls on delete.
    WeakTracking
  };

  /// Copy-construct a handle of the same kind as \p RHS.
  /// \param RHS Handle whose Value pointer and kind are copied.
  ValueHandleBase(const ValueHandleBase &RHS)
      : ValueHandleBase(RHS.PrevPair.getInt(), RHS) {}

  /// Construct a handle of kind \p Kind that tracks the same Value as \p RHS.
  /// \param Kind Subclass kind for this handle.
  /// \param RHS Handle whose Value pointer is shared.
  ValueHandleBase(HandleBaseKind Kind, const ValueHandleBase &RHS)
      : PrevPair(nullptr, Kind), Val(RHS.getValPtr()) {
    if (isValid(getValPtr()))
      AddToExistingUseList(RHS.getPrevPtr());
  }

  /// Move-construct a handle of kind \p Kind from \p RHS.
  /// \param Kind Subclass kind for this handle.
  /// \param RHS Handle whose Value pointer is taken.
  ValueHandleBase(HandleBaseKind Kind, ValueHandleBase &&RHS)
      : PrevPair(nullptr, Kind), Val(RHS.getValPtr()) {
    if (isValid(getValPtr())) {
      AddToExistingUseList(RHS.getPrevPtr());
      RHS.RemoveFromUseList();
      RHS.clearValPtr();
    }
  }

private:
  PointerIntPair<ValueHandleBase**, 2, HandleBaseKind> PrevPair;
  ValueHandleBase *Next = nullptr;
  Value *Val = nullptr;

  void setValPtr(Value *V) { Val = V; }

public:
  /// Construct an empty handle of the given subclass kind.
  /// \param Kind Subclass kind for this handle.
  explicit ValueHandleBase(HandleBaseKind Kind)
      : PrevPair(nullptr, Kind) {}
  /// Construct a handle of kind \p Kind that points to \p V.
  /// \param Kind Subclass kind for this handle.
  /// \param V Value to track, or null.
  ValueHandleBase(HandleBaseKind Kind, Value *V)
      : PrevPair(nullptr, Kind), Val(V) {
    if (isValid(getValPtr()))
      AddToUseList();
  }

  /// Destroy the handle and remove it from its Value's use list.
  ~ValueHandleBase() {
    if (isValid(getValPtr()))
      RemoveFromUseList();
  }

  /// Assign this handle to point to \p RHS.
  /// \param RHS Value to track, or null.
  /// \return The assigned Value pointer.
  Value *operator=(Value *RHS) {
    if (getValPtr() == RHS)
      return RHS;
    if (isValid(getValPtr()))
      RemoveFromUseList();
    setValPtr(RHS);
    if (isValid(getValPtr()))
      AddToUseList();
    return RHS;
  }

  /// Assign this handle to track the same Value as \p RHS.
  /// \param RHS Handle whose Value pointer is copied.
  /// \return The Value pointer now tracked by this handle.
  Value *operator=(const ValueHandleBase &RHS) {
    if (getValPtr() == RHS.getValPtr())
      return RHS.getValPtr();
    if (isValid(getValPtr()))
      RemoveFromUseList();
    setValPtr(RHS.getValPtr());
    if (isValid(getValPtr()))
      AddToExistingUseList(RHS.getPrevPtr());
    return getValPtr();
  }

  /// Move-assign this handle from \p RHS.
  /// \param RHS Handle whose Value pointer is taken.
  /// \return The Value pointer now tracked by this handle.
  Value *operator=(ValueHandleBase &&RHS) {
    if (getValPtr() == RHS.getValPtr()) {
      if (this != &RHS) {
        if (isValid(RHS.getValPtr()))
          RHS.RemoveFromUseList();
        RHS.clearValPtr();
      }
      return getValPtr();
    }
    if (isValid(getValPtr()))
      RemoveFromUseList();
    setValPtr(RHS.getValPtr());
    if (isValid(getValPtr())) {
      AddToExistingUseList(RHS.getPrevPtr());
      RHS.RemoveFromUseList();
      RHS.clearValPtr();
    }
    return getValPtr();
  }

  /// Return a pointer to the tracked Value.
  /// \return A pointer to the tracked Value.
  Value *operator->() const { return getValPtr(); }
  /// Return a reference to the tracked Value.
  /// \return A reference to the tracked Value.
  Value &operator*() const {
    Value *V = getValPtr();
    assert(V && "Dereferencing deleted ValueHandle");
    return *V;
  }

protected:
  /// Return the raw Value pointer held by this handle.
  /// \return The raw Value pointer held by this handle.
  Value *getValPtr() const { return Val; }

  /// Return true if \p V is a non-null Value pointer.
  /// \param V Pointer to test.
  /// \return True if \p V is non-null.
  static bool isValid(Value *V) { return V; }

  /// Remove this ValueHandle from its current use list.
  LLVM_ABI void RemoveFromUseList();

  /// Clear the underlying pointer without clearing the use list.
  ///
  /// This should only be used if a derived class has manually removed the
  /// handle from the use list.
  void clearValPtr() { setValPtr(nullptr); }

public:
  // Callbacks made from Value.
  /// Notify all handles that \p V is being destroyed.
  /// \param V Value that is being deleted.
  LLVM_ABI static void ValueIsDeleted(Value *V);
  /// Notify all handles that \p Old is being RAUW'd to \p New.
  /// \param Old Value being replaced.
  /// \param New Replacement value.
  LLVM_ABI static void ValueIsRAUWd(Value *Old, Value *New);

private:
  // Internal implementation details.
  ValueHandleBase **getPrevPtr() const { return PrevPair.getPointer(); }
  HandleBaseKind getKind() const { return PrevPair.getInt(); }
  void setPrevPtr(ValueHandleBase **Ptr) { PrevPair.setPointer(Ptr); }

  /// Add this ValueHandle to the use list for V.
  ///
  /// List is the address of either the head of the list or a Next node within
  /// the existing use list.
  LLVM_ABI void AddToExistingUseList(ValueHandleBase **List);

  /// Add this ValueHandle to the use list after Node.
  void AddToExistingUseListAfter(ValueHandleBase *Node);

  /// Add this ValueHandle to the use list for V.
  LLVM_ABI void AddToUseList();
};

/// A nullable Value handle that is nullable.
///
/// This is a value handle that points to a value, and nulls itself
/// out if that value is deleted.
class WeakVH : public ValueHandleBase {
public:
  /// Construct a null weak value handle.
  WeakVH() : ValueHandleBase(Weak) {}
  /// Construct a weak handle that points to \p P.
  /// \param P Value to track, or null.
  WeakVH(Value *P) : ValueHandleBase(Weak, P) {}
  /// Copy-construct a weak handle from \p RHS.
  /// \param RHS Handle whose Value pointer is copied.
  WeakVH(const WeakVH &RHS)
      : ValueHandleBase(Weak, RHS) {}

  /// Copy-assign from another weak handle.
  /// \param RHS Handle whose Value pointer is copied.
  /// \return A reference to this handle.
  WeakVH &operator=(const WeakVH &RHS) = default;

  /// Assign this handle to point to \p RHS.
  /// \param RHS Value to track, or null.
  /// \return The assigned Value pointer.
  Value *operator=(Value *RHS) {
    return ValueHandleBase::operator=(RHS);
  }
  /// Assign this handle to track the same Value as \p RHS.
  /// \param RHS Handle whose Value pointer is copied.
  /// \return The Value pointer now tracked by this handle.
  Value *operator=(const ValueHandleBase &RHS) {
    return ValueHandleBase::operator=(RHS);
  }

  /// Implicit conversion to the tracked Value pointer.
  /// \return The tracked Value pointer, or null.
  operator Value*() const {
    return getValPtr();
  }
};

/// simplify_type specialization so WeakVH participates in cast/isa.
template <> struct simplify_type<WeakVH> {
  /// Underlying type used by casting operators.
  using SimpleType = Value *;

  /// Return the Value pointer held by \p WVH.
  /// \param WVH Weak handle to unwrap.
  /// \return The Value pointer held by \p WVH.
  static SimpleType getSimplifiedValue(WeakVH &WVH) { return WVH; }
};
/// simplify_type specialization so const WeakVH participates in cast/isa.
template <> struct simplify_type<const WeakVH> {
  /// Underlying type used by casting operators.
  using SimpleType = Value *;

  /// Return the Value pointer held by \p WVH.
  /// \param WVH Const weak handle to unwrap.
  /// \return The Value pointer held by \p WVH.
  static SimpleType getSimplifiedValue(const WeakVH &WVH) { return WVH; }
};

/// DenseMapInfo specialization so WeakVH can be used as a DenseMap key.
template <> struct DenseMapInfo<WeakVH> {
  /// Return the hash of the Value pointer held by \p Val.
  /// \param Val Weak handle whose Value pointer is hashed.
  /// \return The hash of the Value pointer held by \p Val.
  static unsigned getHashValue(const WeakVH &Val) {
    return DenseMapInfo<Value *>::getHashValue(Val);
  }

  /// Return true if \p LHS and \p RHS hold the same Value pointer.
  /// \param LHS Left-hand weak handle.
  /// \param RHS Right-hand weak handle.
  /// \return True if \p LHS and \p RHS hold the same Value pointer.
  static bool isEqual(const WeakVH &LHS, const WeakVH &RHS) {
    return DenseMapInfo<Value *>::isEqual(LHS, RHS);
  }
};

/// Value handle that is nullable, but tries to track the Value.
///
/// This is a value handle that tries hard to point to a Value, even across
/// RAUW operations, but will null itself out if the value is destroyed.  this
/// is useful for advisory sorts of information, but should not be used as the
/// key of a map (since the map would have to rearrange itself when the pointer
/// changes).
class WeakTrackingVH : public ValueHandleBase {
public:
  /// Construct a null weak tracking value handle.
  WeakTrackingVH() : ValueHandleBase(WeakTracking) {}
  /// Construct a weak tracking handle that points to \p P.
  /// \param P Value to track, or null.
  WeakTrackingVH(Value *P) : ValueHandleBase(WeakTracking, P) {}
  /// Copy-construct a weak tracking handle from \p RHS.
  /// \param RHS Handle whose Value pointer is copied.
  WeakTrackingVH(const WeakTrackingVH &RHS)
      : ValueHandleBase(WeakTracking, RHS) {}

  /// Copy-assign from another weak tracking handle.
  /// \param RHS Handle whose Value pointer is copied.
  /// \return A reference to this handle.
  WeakTrackingVH &operator=(const WeakTrackingVH &RHS) = default;

  /// Assign this handle to point to \p RHS.
  /// \param RHS Value to track, or null.
  /// \return The assigned Value pointer.
  Value *operator=(Value *RHS) {
    return ValueHandleBase::operator=(RHS);
  }
  /// Assign this handle to track the same Value as \p RHS.
  /// \param RHS Handle whose Value pointer is copied.
  /// \return The Value pointer now tracked by this handle.
  Value *operator=(const ValueHandleBase &RHS) {
    return ValueHandleBase::operator=(RHS);
  }

  /// Implicit conversion to the tracked Value pointer.
  /// \return The tracked Value pointer, or null.
  operator Value*() const {
    return getValPtr();
  }

  /// Return true if this handle points to a non-null, still-alive Value.
  /// \return True if this handle points to a non-null, still-alive Value.
  bool pointsToAliveValue() const {
    return ValueHandleBase::isValid(getValPtr());
  }
};

/// simplify_type specialization so WeakTrackingVH participates in cast/isa.
template <> struct simplify_type<WeakTrackingVH> {
  /// Underlying type used by casting operators.
  using SimpleType = Value *;

  /// Return the Value pointer held by \p WVH.
  /// \param WVH Weak tracking handle to unwrap.
  /// \return The Value pointer held by \p WVH.
  static SimpleType getSimplifiedValue(WeakTrackingVH &WVH) { return WVH; }
};
/// simplify_type specialization so const WeakTrackingVH participates in cast/isa.
template <> struct simplify_type<const WeakTrackingVH> {
  /// Underlying type used by casting operators.
  using SimpleType = Value *;

  /// Return the Value pointer held by \p WVH.
  /// \param WVH Const weak tracking handle to unwrap.
  /// \return The Value pointer held by \p WVH.
  static SimpleType getSimplifiedValue(const WeakTrackingVH &WVH) {
    return WVH;
  }
};

/// Value handle that asserts if the Value is deleted.
///
/// This is a Value Handle that points to a value and asserts out if the value
/// is destroyed while the handle is still live.  This is very useful for
/// catching dangling pointer bugs and other things which can be non-obvious.
/// One particularly useful place to use this is as the Key of a map.  Dangling
/// pointer bugs often lead to really subtle bugs that only occur if another
/// object happens to get allocated to the same address as the old one.  Using
/// an AssertingVH ensures that an assert is triggered as soon as the bad
/// delete occurs.
///
/// Note that an AssertingVH handle does *not* follow values across RAUW
/// operations.  This means that RAUW's need to explicitly update the
/// AssertingVH's as it moves.  This is required because in non-assert mode this
/// class turns into a trivial wrapper around a pointer.
template <typename ValueTy>
class AssertingVH
#if LLVM_ENABLE_ABI_BREAKING_CHECKS
    : public ValueHandleBase
#endif
{
  friend struct DenseMapInfo<AssertingVH<ValueTy>>;

#if LLVM_ENABLE_ABI_BREAKING_CHECKS
  Value *getRawValPtr() const { return ValueHandleBase::getValPtr(); }
  void setRawValPtr(Value *P) { ValueHandleBase::operator=(P); }
#else
  Value *ThePtr;
  Value *getRawValPtr() const { return ThePtr; }
  void setRawValPtr(Value *P) { ThePtr = P; }
#endif
  // Convert a ValueTy*, which may be const, to the raw Value*.
  static Value *GetAsValue(Value *V) { return V; }
  static Value *GetAsValue(const Value *V) { return const_cast<Value*>(V); }

  ValueTy *getValPtr() const { return static_cast<ValueTy *>(getRawValPtr()); }
  void setValPtr(ValueTy *P) { setRawValPtr(GetAsValue(P)); }

public:
#if LLVM_ENABLE_ABI_BREAKING_CHECKS
  /// Construct a null asserting value handle.
  AssertingVH() : ValueHandleBase(Assert) {}
  /// Construct an asserting handle that points to \p P.
  /// \param P Value to track, or null.
  AssertingVH(ValueTy *P) : ValueHandleBase(Assert, GetAsValue(P)) {}
  /// Copy-construct an asserting handle from \p RHS.
  /// \param RHS Handle whose Value pointer is copied.
  AssertingVH(const AssertingVH &RHS) : ValueHandleBase(Assert, RHS) {}
  /// Move-construct an asserting handle from \p RHS.
  /// \param RHS Handle whose Value pointer is taken.
  AssertingVH(AssertingVH &&RHS) : ValueHandleBase(Assert, std::move(RHS)) {}
#else
  /// Construct a null asserting value handle.
  AssertingVH() : ThePtr(nullptr) {}
  /// Construct an asserting handle that points to \p P.
  /// \param P Value to track, or null.
  AssertingVH(ValueTy *P) : ThePtr(GetAsValue(P)) {}
  /// Copy-construct an asserting handle from \p RHS.
  /// \param RHS Handle whose Value pointer is copied.
  AssertingVH(const AssertingVH &RHS) = default;
  /// Move-construct an asserting handle from \p RHS.
  /// \param RHS Handle whose Value pointer is taken.
  AssertingVH(AssertingVH &&RHS) : ThePtr(std::exchange(RHS.ThePtr, nullptr)) {}
#endif

  /// Implicit conversion to the tracked typed Value pointer.
  /// \return The tracked typed Value pointer, or null.
  operator ValueTy*() const {
    return getValPtr();
  }

  /// Assign this handle to point to \p RHS.
  /// \param RHS Typed value to track, or null.
  /// \return The assigned typed Value pointer.
  ValueTy *operator=(ValueTy *RHS) {
    setValPtr(RHS);
    return getValPtr();
  }
  /// Assign this handle to track the same Value as \p RHS.
  /// \param RHS Handle whose Value pointer is copied.
  /// \return The typed Value pointer now tracked by this handle.
  ValueTy *operator=(const AssertingVH<ValueTy> &RHS) {
    setValPtr(RHS.getValPtr());
    return getValPtr();
  }
#if LLVM_ENABLE_ABI_BREAKING_CHECKS
  /// Move-assign this handle from \p RHS.
  /// \param RHS Handle whose Value pointer is taken.
  /// \return The typed Value pointer now tracked by this handle.
  ValueTy *operator=(AssertingVH<ValueTy> &&RHS) {
    ValueHandleBase::operator=(std::move(RHS));
    return getValPtr();
  }
#else
  /// Move-assign this handle from \p RHS.
  /// \param RHS Handle whose Value pointer is taken.
  /// \return The typed Value pointer now tracked by this handle.
  ValueTy *operator=(AssertingVH<ValueTy> &&RHS) {
    ThePtr = std::exchange(RHS.ThePtr, nullptr);
    return getValPtr();
  }
#endif

  /// Return a pointer to the tracked typed Value.
  /// \return A pointer to the tracked typed Value.
  ValueTy *operator->() const { return getValPtr(); }
  /// Return a reference to the tracked typed Value.
  /// \return A reference to the tracked typed Value.
  ValueTy &operator*() const { return *getValPtr(); }
};

/// DenseMapInfo specialization treating AssertingVH like a raw T* key.
template<typename T>
struct DenseMapInfo<AssertingVH<T>> : DenseMapInfo<T *> {};

/// Value handle that tracks a Value across RAUW.
///
/// TrackingVH is designed for situations where a client needs to hold a handle
/// to a Value (or subclass) across some operations which may move that value,
/// but should never destroy it or replace it with some unacceptable type.
///
/// It is an error to attempt to replace a value with one of a type which is
/// incompatible with any of its outstanding TrackingVHs.
///
/// It is an error to read from a TrackingVH that does not point to a valid
/// value.  A TrackingVH is said to not point to a valid value if either it
/// hasn't yet been assigned a value yet or because the value it was tracking
/// has since been deleted.
///
/// Assigning a value to a TrackingVH is always allowed, even if said TrackingVH
/// no longer points to a valid value.
template <typename ValueTy> class TrackingVH {
  WeakTrackingVH InnerHandle;

public:
  /// Return the tracked typed Value, asserting it is alive and correctly typed.
  /// \return The tracked typed Value pointer.
  ValueTy *getValPtr() const {
    assert(InnerHandle.pointsToAliveValue() &&
           "TrackingVH must be non-null and valid on dereference!");

    // Check that the value is a member of the correct subclass. We would like
    // to check this property on assignment for better debugging, but we don't
    // want to require a virtual interface on this VH. Instead we allow RAUW to
    // replace this value with a value of an invalid type, and check it here.
    assert(isa<ValueTy>(InnerHandle) &&
           "Tracked Value was replaced by one with an invalid type!");
    return cast<ValueTy>(InnerHandle);
  }

  /// Point this handle at \p P.
  /// \param P Typed value to track, or null.
  void setValPtr(ValueTy *P) {
    // Assigning to non-valid TrackingVH's are fine so we just unconditionally
    // assign here.
    InnerHandle = GetAsValue(P);
  }

  // Convert a ValueTy*, which may be const, to the type the base
  // class expects.
  /// Convert a non-const Value pointer to the base Value* type.
  /// \param V Value pointer to convert.
  /// \return \p V as a Value*.
  static Value *GetAsValue(Value *V) { return V; }
  /// Convert a const Value pointer to the base Value* type.
  /// \param V Const value pointer to convert.
  /// \return \p V cast to a non-const Value*.
  static Value *GetAsValue(const Value *V) { return const_cast<Value*>(V); }

public:
  /// Construct a null tracking value handle.
  TrackingVH() = default;
  /// Construct a tracking handle that points to \p P.
  /// \param P Typed value to track, or null.
  TrackingVH(ValueTy *P) { setValPtr(P); }

  /// Implicit conversion to the tracked typed Value pointer.
  /// \return The tracked typed Value pointer.
  operator ValueTy*() const {
    return getValPtr();
  }

  /// Assign this handle to point to \p RHS.
  /// \param RHS Typed value to track, or null.
  /// \return The assigned typed Value pointer.
  ValueTy *operator=(ValueTy *RHS) {
    setValPtr(RHS);
    return getValPtr();
  }

  /// Return a pointer to the tracked typed Value.
  /// \return A pointer to the tracked typed Value.
  ValueTy *operator->() const { return getValPtr(); }
  /// Return a reference to the tracked typed Value.
  /// \return A reference to the tracked typed Value.
  ValueTy &operator*() const { return *getValPtr(); }
};

/// Value handle with callbacks on RAUW and destruction.
///
/// This is a value handle that allows subclasses to define callbacks that run
/// when the underlying Value has RAUW called on it or is destroyed.  This
/// class can be used as the key of a map, as long as the user takes it out of
/// the map before calling setValPtr() (since the map has to rearrange itself
/// when the pointer changes).  Unlike ValueHandleBase, this class has a vtable.
class LLVM_ABI CallbackVH : public ValueHandleBase {
  virtual void anchor();
protected:
  /// Destroy the callback handle.
  ~CallbackVH() = default;
  /// Copy-construct a callback handle from \p RHS.
  /// \param RHS Handle whose Value pointer is copied.
  CallbackVH(const CallbackVH &RHS) = default;
  /// Copy-assign from another callback handle.
  /// \param RHS Handle whose Value pointer is copied.
  /// \return A reference to this handle.
  CallbackVH &operator=(const CallbackVH &RHS) = default;

  /// Point this handle at \p P.
  /// \param P Value to track, or null.
  void setValPtr(Value *P) {
    ValueHandleBase::operator=(P);
  }

public:
  /// Construct a null callback value handle.
  CallbackVH() : ValueHandleBase(Callback) {}
  /// Construct a callback handle that points to \p P.
  /// \param P Value to track, or null.
  CallbackVH(Value *P) : ValueHandleBase(Callback, P) {}
  /// Construct a callback handle that points to const value \p P.
  /// \param P Const value to track, or null.
  CallbackVH(const Value *P) : CallbackVH(const_cast<Value *>(P)) {}

  /// Implicit conversion to the tracked Value pointer.
  /// \return The tracked Value pointer, or null.
  operator Value*() const {
    return getValPtr();
  }

  /// Callback for Value destruction.
  ///
  /// Called when this->getValPtr() is destroyed, inside ~Value(), so you
  /// may call any non-virtual Value method on getValPtr(), but no subclass
  /// methods.  If WeakTrackingVH were implemented as a CallbackVH, it would use
  /// this
  /// method to call setValPtr(NULL).  AssertingVH would use this method to
  /// cause an assertion failure.
  ///
  /// All implementations must remove the reference from this object to the
  /// Value that's being destroyed.
  virtual void deleted() { setValPtr(nullptr); }

  /// Callback for Value RAUW.
  ///
  /// Called when this->getValPtr()->replaceAllUsesWith(new_value) is called,
  /// _before_ any of the uses have actually been replaced.  If WeakTrackingVH
  /// were
  /// implemented as a CallbackVH, it would use this method to call
  /// setValPtr(new_value).  AssertingVH would do nothing in this method.
  /// \param New Replacement value passed to replaceAllUsesWith.
  virtual void allUsesReplacedWith(Value *New) {}
};

/// Value handle that poisons itself if the Value is deleted.
///
/// This is a Value Handle that points to a value and poisons itself if the
/// value is destroyed while the handle is still live.  This is very useful for
/// catching dangling pointer bugs where an \c AssertingVH cannot be used
/// because the dangling handle needs to outlive the value without ever being
/// used.
///
/// One particularly useful place to use this is as the Key of a map. Dangling
/// pointer bugs often lead to really subtle bugs that only occur if another
/// object happens to get allocated to the same address as the old one. Using
/// a PoisoningVH ensures that an assert is triggered if looking up a new value
/// in the map finds a handle from the old value.
///
/// Note that a PoisoningVH handle does *not* follow values across RAUW
/// operations. This means that RAUW's need to explicitly update the
/// PoisoningVH's as it moves. This is required because in non-assert mode this
/// class turns into a trivial wrapper around a pointer.
template <typename ValueTy>
class PoisoningVH final
#if LLVM_ENABLE_ABI_BREAKING_CHECKS
    : public CallbackVH
#endif
{
  friend struct DenseMapInfo<PoisoningVH<ValueTy>>;

  // Convert a ValueTy*, which may be const, to the raw Value*.
  static Value *GetAsValue(Value *V) { return V; }
  static Value *GetAsValue(const Value *V) { return const_cast<Value *>(V); }

#if LLVM_ENABLE_ABI_BREAKING_CHECKS
  /// A flag tracking whether this value has been poisoned.
  ///
  /// On delete and RAUW, we leave the value pointer alone so that as a raw
  /// pointer it produces the same value (and we fit into the same key of
  /// a hash table, etc), but we poison the handle so that any top-level usage
  /// will fail.
  bool Poisoned = false;

  Value *getRawValPtr() const { return ValueHandleBase::getValPtr(); }
  void setRawValPtr(Value *P) { ValueHandleBase::operator=(P); }

  /// Handle deletion by poisoning the handle.
  void deleted() override {
    assert(!Poisoned && "Tried to delete an already poisoned handle!");
    Poisoned = true;
    RemoveFromUseList();
  }

  /// Handle RAUW by poisoning the handle.
  void allUsesReplacedWith(Value *) override {
    assert(!Poisoned && "Tried to RAUW an already poisoned handle!");
    Poisoned = true;
    RemoveFromUseList();
  }
#else // LLVM_ENABLE_ABI_BREAKING_CHECKS
  Value *ThePtr = nullptr;

  Value *getRawValPtr() const { return ThePtr; }
  void setRawValPtr(Value *P) { ThePtr = P; }
#endif

  ValueTy *getValPtr() const {
#if LLVM_ENABLE_ABI_BREAKING_CHECKS
    assert(!Poisoned && "Accessed a poisoned value handle!");
#endif
    return static_cast<ValueTy *>(getRawValPtr());
  }
  void setValPtr(ValueTy *P) { setRawValPtr(GetAsValue(P)); }

public:
  /// Construct a null poisoning value handle.
  PoisoningVH() = default;
#if LLVM_ENABLE_ABI_BREAKING_CHECKS
  /// Construct a poisoning handle that points to \p P.
  /// \param P Typed value to track, or null.
  PoisoningVH(ValueTy *P) : CallbackVH(GetAsValue(P)) {}
  /// Copy-construct a poisoning handle from \p RHS.
  ///
  /// A poisoned handle is detached from its use list but keeps its raw value
  /// pointer, so its use-list pointers are stale; a copy must not relink through
  /// them.
  /// \param RHS Handle whose Value pointer and poison state are copied.
  PoisoningVH(const PoisoningVH &RHS) : CallbackVH(), Poisoned(RHS.Poisoned) {
    if (Poisoned)
      ValueHandleBase::setValPtr(RHS.getRawValPtr());
    else
      setRawValPtr(RHS.getRawValPtr());
  }

  ~PoisoningVH() {
    if (Poisoned)
      clearValPtr();
  }

  PoisoningVH &operator=(const PoisoningVH &RHS) {
    if (Poisoned)
      clearValPtr();
    if (RHS.Poisoned) {
      // Detach *this and copy only the raw pointer; see the copy constructor.
      if (isValid(getRawValPtr()))
        RemoveFromUseList();
      ValueHandleBase::setValPtr(RHS.getRawValPtr());
    } else {
      CallbackVH::operator=(RHS);
    }
    Poisoned = RHS.Poisoned;
    return *this;
  }
#else
  /// Construct a poisoning handle that points to \p P.
  /// \param P Typed value to track, or null.
  PoisoningVH(ValueTy *P) : ThePtr(GetAsValue(P)) {}
#endif

  /// Implicit conversion to the tracked typed Value pointer.
  /// \return The tracked typed Value pointer, or null.
  operator ValueTy *() const { return getValPtr(); }

  /// Return a pointer to the tracked typed Value.
  /// \return A pointer to the tracked typed Value.
  ValueTy *operator->() const { return getValPtr(); }
  /// Return a reference to the tracked typed Value.
  /// \return A reference to the tracked typed Value.
  ValueTy &operator*() const { return *getValPtr(); }
};

/// DenseMapInfo specialization so PoisoningVH can be used as a DenseMap key.
template <typename T> struct DenseMapInfo<PoisoningVH<T>> {
  /// Return the hash of the raw Value pointer held by \p Val.
  /// \param Val Poisoning handle whose raw Value pointer is hashed.
  /// \return The hash of the raw Value pointer held by \p Val.
  static unsigned getHashValue(const PoisoningVH<T> &Val) {
    return DenseMapInfo<Value *>::getHashValue(Val.getRawValPtr());
  }

  /// Return true if \p LHS and \p RHS hold the same raw Value pointer.
  /// \param LHS Left-hand poisoning handle.
  /// \param RHS Right-hand poisoning handle.
  /// \return True if \p LHS and \p RHS hold the same raw Value pointer.
  static bool isEqual(const PoisoningVH<T> &LHS, const PoisoningVH<T> &RHS) {
    return DenseMapInfo<Value *>::isEqual(LHS.getRawValPtr(),
                                          RHS.getRawValPtr());
  }

  // Allow lookup by T* via find_as(), without constructing a temporary
  // value handle.

  /// Return the hash of typed pointer \p Val for find_as() lookup.
  /// \param Val Typed value pointer to hash.
  /// \return The hash of \p Val.
  static unsigned getHashValue(const T *Val) {
    return DenseMapInfo<Value *>::getHashValue(Val);
  }

  /// Return true if \p LHS equals the raw Value pointer held by \p RHS.
  /// \param LHS Typed value pointer to compare.
  /// \param RHS Poisoning handle whose raw Value pointer is compared.
  /// \return True if \p LHS equals the raw Value pointer held by \p RHS.
  static bool isEqual(const T *LHS, const PoisoningVH<T> &RHS) {
    return DenseMapInfo<Value *>::isEqual(LHS, RHS.getRawValPtr());
  }
};

} // end namespace llvm

#endif // LLVM_IR_VALUEHANDLE_H
