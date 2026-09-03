//===- llvm/User.h - User class definition ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class defines the interface that one who uses a Value must implement.
// Each instance of the Value class keeps track of what User's have handles
// to it.
//
//  * Instructions are the largest class of Users.
//  * Constants may be users of other constants (think arrays and stuff)
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_USER_H
#define LLVM_IR_USER_H

#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>

namespace llvm {

template <typename T> class ArrayRef;
template <typename T> class MutableArrayRef;

/// Compile-time customization of User operands.
///
/// Customizes operand-related allocators and accessors.
template <class>
struct OperandTraits;

/// A Value that uses other Values as operands.
class User : public Value {
  friend struct HungoffOperandTraits;
  template <class ConstantClass> friend struct ConstantAggrKeyType;

  LLVM_ATTRIBUTE_ALWAYS_INLINE static void *
  allocateFixedOperandUser(size_t, unsigned, unsigned);

protected:
  // Disable the default operator new, as all subclasses must use one of the
  // custom operators below depending on how they store their operands.
  /// Deleted; subclasses must use a custom operand-aware allocator.
  /// \param Size Unused size argument required by the language.
  void *operator new(size_t Size) = delete;

  /// Indicates this User has operands "hung off" in another allocation.
  struct HungOffOperandsAllocMarker {};

  /// Indicates this User has operands co-allocated.
  struct IntrusiveOperandsAllocMarker {
    /// The number of operands for this User.
    const unsigned NumOps;
  };

  /// Indicates this User has operands and a descriptor co-allocated .
  struct IntrusiveOperandsAndDescriptorAllocMarker {
    /// The number of operands for this User.
    const unsigned NumOps;
    /// The number of bytes to allocate for the descriptor. Must be divisible by
    /// `sizeof(void *)`.
    const unsigned DescBytes;
  };

  /// Information about how a User object was allocated, to be passed into the
  /// User constructor.
  ///
  /// DO NOT USE DIRECTLY. Use one of the `AllocMarker` structs instead, they
  /// call all be implicitly converted to `AllocInfo`.
  struct AllocInfo {
  public:
    /// Number of operands allocated for this user.
    const unsigned NumOps : NumUserOperandsBits;
    LLVM_PREFERRED_TYPE(bool)
    const unsigned HasHungOffUses : 1; ///< True if operands are hung off separately.
    LLVM_PREFERRED_TYPE(bool)
    const unsigned HasDescriptor : 1; ///< True if a descriptor is co-allocated.

    /// Deleted; allocation info must be constructed from an AllocMarker.
    AllocInfo() = delete;

    /// Construct allocation info for a user with hung-off operands.
    /// \param Marker Marker selecting hung-off operand allocation.
    constexpr AllocInfo(const HungOffOperandsAllocMarker Marker)
        : NumOps(0), HasHungOffUses(true), HasDescriptor(false) {}

    /// Construct allocation info for a user with co-allocated operands.
    /// \param Alloc Marker specifying the number of co-allocated operands.
    constexpr AllocInfo(const IntrusiveOperandsAllocMarker Alloc)
        : NumOps(Alloc.NumOps), HasHungOffUses(false), HasDescriptor(false) {}

    /// Construct from an intrusive-operands-and-descriptor allocation marker.
    /// \param Alloc Marker specifying operand count and descriptor size.
    constexpr AllocInfo(const IntrusiveOperandsAndDescriptorAllocMarker Alloc)
        : NumOps(Alloc.NumOps), HasHungOffUses(false),
          HasDescriptor(Alloc.DescBytes != 0) {}
  };

  /// Allocate a User with an operand pointer co-allocated.
  ///
  /// This is used for subclasses which need to allocate a variable number
  /// of operands, ie, 'hung off uses'.
  /// \param Size Size of the User subclass in bytes.
  /// \param Marker Marker selecting hung-off operand allocation.
  /// \return Pointer to the allocated User storage.
  LLVM_ABI void *operator new(size_t Size, HungOffOperandsAllocMarker Marker);

  /// Allocate a User with the operands co-allocated.
  ///
  /// This is used for subclasses which have a fixed number of operands.
  /// \param Size Size of the User subclass in bytes.
  /// \param allocTrait Marker specifying the number of co-allocated operands.
  /// \return Pointer to the allocated User storage.
  LLVM_ABI void *operator new(size_t Size,
                              IntrusiveOperandsAllocMarker allocTrait);

  /// Allocate a User with co-allocated operands and an optional descriptor.
  ///
  /// If DescBytes is non-zero then allocate an additional DescBytes bytes
  /// before the operands. These bytes can be accessed by calling getDescriptor.
  /// \param Size Size of the User subclass in bytes.
  /// \param allocTrait Marker specifying operand count and descriptor size.
  /// \return Pointer to the allocated User storage.
  LLVM_ABI void *
  operator new(size_t Size,
               IntrusiveOperandsAndDescriptorAllocMarker allocTrait);

  /// Construct a User with the given type, subclass ID, and allocation info.
  /// \param ty Type of this value.
  /// \param vty Subclass identifier stored in SubclassID.
  /// \param AllocInfo How this User was allocated.
  User(Type *ty, unsigned vty, AllocInfo AllocInfo) : Value(ty, vty) {
    assert(AllocInfo.NumOps < (1u << NumUserOperandsBits) &&
           "Too many operands");
    NumUserOperands = AllocInfo.NumOps;
    assert((!AllocInfo.HasDescriptor || !AllocInfo.HasHungOffUses) &&
           "Cannot have both hung off uses and a descriptor");
    HasHungOffUses = AllocInfo.HasHungOffUses;
    HasDescriptor = AllocInfo.HasDescriptor;
    // If we have hung off uses, then the operand list should initially be
    // null.
    assert((!AllocInfo.HasHungOffUses || !getOperandList()) &&
           "Error in initializing hung off uses for User");

    Use *Operands = reinterpret_cast<Use *>(this) - NumUserOperands;
    for (unsigned I = 0; I < NumUserOperands; ++I)
      new (&Operands[I]) Use(this);
  }

  /// Allocate the array of Uses, followed by a pointer
  /// (with bottom bit set) to the User.
  /// \param N Number of hung-off uses to allocate.
  /// \param WithExtraValues identifies callers which need N Value* allocated
  /// along the N operands.
  LLVM_ABI void allocHungoffUses(unsigned N, bool WithExtraValues = false);

  /// Grow the number of hung off uses.  Note that allocHungoffUses
  /// should be called if there are no uses.
  /// \param N New number of hung-off uses.
  /// \param WithExtraValues Whether to also allocate N trailing Value* slots.
  LLVM_ABI void growHungoffUses(unsigned N, bool WithExtraValues = false);

protected:
  // Use deleteValue() to delete a generic User.
  /// Destroy this User; callers should use deleteValue() for generic Values.
  LLVM_ABI ~User();

public:
  /// Copy construction is deleted; Users are not copyable.
  /// \param Other Unused source User (deleted).
  User(const User &Other) = delete;

  /// Free memory allocated for User and Use objects.
  /// \param Usr Memory returned by a User operator new.
  LLVM_ABI void operator delete(void *Usr);
  /// Placement delete - required by std, called if the ctor throws.
  /// \param Usr Memory returned by the matching placement new.
  /// \param Marker Allocation marker matching the placement new.
  LLVM_ABI void operator delete(void *Usr, HungOffOperandsAllocMarker Marker);
  /// Placement delete - required by std, called if the ctor throws.
  /// \param Usr Memory returned by the matching placement new.
  /// \param Marker Allocation marker matching the placement new.
  LLVM_ABI void operator delete(void *Usr,
                                IntrusiveOperandsAndDescriptorAllocMarker Marker);
  /// Placement delete - required by std, called if the ctor throws.
  /// \param Usr Memory returned by the matching placement new.
  /// \param Marker Allocation marker matching the placement new.
  LLVM_ABI void operator delete(void *Usr, IntrusiveOperandsAllocMarker Marker);

protected:
  /// Return the operand Use at compile-time index \p Idx from \p that.
  /// \param that The User (or subclass) to read the operand from.
  /// \return Reference to the operand Use.
  template <int Idx, typename U> static Use &OpFrom(const U *that) {
    return Idx < 0
      ? OperandTraits<U>::op_end(const_cast<U*>(that))[Idx]
      : OperandTraits<U>::op_begin(const_cast<U*>(that))[Idx];
  }

  /// Return the operand Use at compile-time index \p Idx.
  /// \return Reference to the operand Use.
  template <int Idx> Use &Op() {
    return OpFrom<Idx>(this);
  }
  /// Return the const operand Use at compile-time index \p Idx.
  /// \return Const reference to the operand Use.
  template <int Idx> const Use &Op() const {
    return OpFrom<Idx>(this);
  }

private:
  const Use *getHungOffOperands() const {
    return *(reinterpret_cast<const Use *const *>(this) - 1);
  }

  Use *&getHungOffOperands() { return *(reinterpret_cast<Use **>(this) - 1); }

  const Use *getIntrusiveOperands() const {
    return reinterpret_cast<const Use *>(this) - NumUserOperands;
  }

  Use *getIntrusiveOperands() {
    return reinterpret_cast<Use *>(this) - NumUserOperands;
  }

  void setOperandList(Use *NewList) {
    assert(HasHungOffUses &&
           "Setting operand list only required for hung off uses");
    getHungOffOperands() = NewList;
  }

public:
  /// Return a pointer to the array of operand Uses.
  /// \return Pointer to the first operand Use.
  const Use *getOperandList() const {
    return HasHungOffUses ? getHungOffOperands() : getIntrusiveOperands();
  }
  /// Return a mutable pointer to the array of operand Uses.
  /// \return Mutable pointer to the first operand Use.
  Use *getOperandList() {
    return const_cast<Use *>(static_cast<const User *>(this)->getOperandList());
  }

  /// Return operand \p i.
  /// \param i Operand index.
  /// \return The value of operand \p i.
  Value *getOperand(unsigned i) const {
    assert(i < NumUserOperands && "getOperand() out of range!");
    return getOperandList()[i];
  }

  /// Replace operand \p i with \p Val.
  /// \param i Operand index.
  /// \param Val The new operand value.
  void setOperand(unsigned i, Value *Val) {
    assert(i < NumUserOperands && "setOperand() out of range!");
    assert((!isa<Constant>((const Value*)this) ||
            isa<GlobalValue>((const Value*)this)) &&
           "Cannot mutate a constant with setOperand!");
    getOperandList()[i] = Val;
  }

  /// Return a const reference to the \c Use for operand \p i.
  /// \param i Operand index.
  /// \return Const reference to the \c Use for operand \p i.
  const Use &getOperandUse(unsigned i) const {
    assert(i < NumUserOperands && "getOperandUse() out of range!");
    return getOperandList()[i];
  }
  /// Return a mutable reference to the \c Use for operand \p i.
  /// \param i Operand index.
  /// \return Mutable reference to the \c Use for operand \p i.
  Use &getOperandUse(unsigned i) {
    assert(i < NumUserOperands && "getOperandUse() out of range!");
    return getOperandList()[i];
  }

  /// Return the number of operands.
  /// \return The number of operands.
  unsigned getNumOperands() const { return NumUserOperands; }

  /// Returns the descriptor co-allocated with this User instance.
  /// \return The descriptor bytes co-allocated with this User.
  LLVM_ABI ArrayRef<const uint8_t> getDescriptor() const;

  /// Returns the descriptor co-allocated with this User instance.
  /// \return A mutable view of the descriptor bytes co-allocated with this User.
  LLVM_ABI MutableArrayRef<uint8_t> getDescriptor();

  /// Set the operand count for a User with hung-off uses.
  ///
  /// Subclasses with hung off uses need to manage the operand count
  /// themselves.  In these instances, the operand count isn't used to find the
  /// OperandList, so there's no issue in having the operand count change.
  /// \param NumOps The new number of operands.
  void setNumHungOffUseOperands(unsigned NumOps) {
    assert(HasHungOffUses && "Must have hung off uses to use this method");
    assert(NumOps < (1u << NumUserOperandsBits) && "Too many operands");
    NumUserOperands = NumOps;
  }

  /// Return whether this user's uses may be dropped without affecting
  /// correctness.
  ///
  /// A droppable user is a user for which uses can be dropped without affecting
  /// correctness and should be dropped rather than preventing a transformation
  /// from happening.
  /// \return True if this user is droppable.
  LLVM_ABI bool isDroppable() const;

  // ---------------------------------------------------------------------------
  // Operand Iterator interface...
  //
  /// Iterator over this user's operand \c Use objects.
  using op_iterator = Use*;
  /// Const iterator over this user's operand \c Use objects.
  using const_op_iterator = const Use*;
  /// Range of mutable operand \c Use iterators.
  using op_range = iterator_range<op_iterator>;
  /// Range of const operand \c Use iterators.
  using const_op_range = iterator_range<const_op_iterator>;

  /// Return an iterator to the first operand \c Use.
  /// \return Iterator to the first operand \c Use.
  op_iterator       op_begin()       { return getOperandList(); }
  /// Return an iterator to the first operand \c Use.
  /// \return Const iterator to the first operand \c Use.
  const_op_iterator op_begin() const { return getOperandList(); }
  /// Return an iterator past the last operand \c Use.
  /// \return Iterator past the last operand \c Use.
  op_iterator       op_end()         {
    return getOperandList() + NumUserOperands;
  }
  /// Return a const iterator past the last operand \c Use.
  /// \return Const iterator past the last operand \c Use.
  const_op_iterator op_end()   const {
    return getOperandList() + NumUserOperands;
  }
  /// Return a range over this user's operand Uses.
  /// \return Range from op_begin() to op_end().
  op_range operands() {
    return op_range(op_begin(), op_end());
  }
  /// Return a const range over this user's operand Uses.
  /// \return Const range from op_begin() to op_end().
  const_op_range operands() const {
    return const_op_range(op_begin(), op_end());
  }

  /// Iterator for directly iterating over the operand Values.
  struct value_op_iterator
      : iterator_adaptor_base<value_op_iterator, op_iterator,
                              std::random_access_iterator_tag, Value *,
                              ptrdiff_t, Value *, Value *> {
    /// Construct an iterator over operand values, starting at use \p U.
    /// \param U The starting Use, or null for a default-constructed iterator.
    explicit value_op_iterator(Use *U = nullptr) : iterator_adaptor_base(U) {}

    /// Dereference to the operand value.
    /// \return The operand value.
    Value *operator*() const { return *I; }
    /// Member access through the operand value.
    /// \return Pointer to the operand value.
    Value *operator->() const { return operator*(); }
  };

  /// Return an iterator to the first operand Value.
  /// \return Iterator to the first operand Value.
  value_op_iterator value_op_begin() {
    return value_op_iterator(op_begin());
  }
  /// Return an iterator past the last operand Value.
  /// \return Iterator past the last operand Value.
  value_op_iterator value_op_end() {
    return value_op_iterator(op_end());
  }
  /// Return a range over this user's operand Values.
  /// \return Range from value_op_begin() to value_op_end().
  iterator_range<value_op_iterator> operand_values() {
    return make_range(value_op_begin(), value_op_end());
  }

  /// Const iterator for directly iterating over the operand Values.
  struct const_value_op_iterator
      : iterator_adaptor_base<const_value_op_iterator, const_op_iterator,
                              std::random_access_iterator_tag, const Value *,
                              ptrdiff_t, const Value *, const Value *> {
    /// Construct a const iterator over operand values, starting at use \p U.
    /// \param U The starting Use, or null for a default-constructed iterator.
    explicit const_value_op_iterator(const Use *U = nullptr) :
      iterator_adaptor_base(U) {}

    /// Dereference to the const operand value.
    /// \return The const operand value.
    const Value *operator*() const { return *I; }
    /// Member access through the const operand value.
    /// \return Pointer to the const operand value.
    const Value *operator->() const { return operator*(); }
  };

  /// Return a const iterator to the first operand Value.
  /// \return Const iterator to the first operand Value.
  const_value_op_iterator value_op_begin() const {
    return const_value_op_iterator(op_begin());
  }
  /// Return a const iterator past the last operand Value.
  /// \return Const iterator past the last operand Value.
  const_value_op_iterator value_op_end() const {
    return const_value_op_iterator(op_end());
  }
  /// Return a const range over this user's operand Values.
  /// \return Const range from value_op_begin() to value_op_end().
  iterator_range<const_value_op_iterator> operand_values() const {
    return make_range(value_op_begin(), value_op_end());
  }

  /// Drop all references to operands.
  ///
  /// This function is in charge of "letting go" of all objects that this User
  /// refers to.  This allows one to 'delete' a whole class at a time, even
  /// though there may be circular references...  First all references are
  /// dropped, and all use counts go to zero.  Then everything is deleted for
  /// real.  Note that no operations are valid on an object that has "dropped
  /// all references", except operator delete.
  void dropAllReferences() {
    for (Use &U : operands())
      U.set(nullptr);
  }

  /// Replace uses of one Value with another.
  ///
  /// Replaces all references to the "From" definition with references to the
  /// "To" definition.
  /// \param From The value whose uses should be replaced.
  /// \param To The value to substitute for \p From.
  /// \return True if any uses were replaced.
  LLVM_ABI bool replaceUsesOfWith(Value *From, Value *To);

  /// Check whether \p V is a User.
  /// \param V The value to test.
  /// \return True if \p V is an Instruction or Constant.
  static bool classof(const Value *V) {
    return isa<Instruction>(V) || isa<Constant>(V);
  }
};

// Either Use objects, or a Use pointer can be prepended to User.
static_assert(alignof(Use) >= alignof(User),
              "Alignment is insufficient after objects prepended to User");
static_assert(alignof(Use *) >= alignof(User),
              "Alignment is insufficient after objects prepended to User");

} // end namespace llvm

#endif // LLVM_IR_USER_H
