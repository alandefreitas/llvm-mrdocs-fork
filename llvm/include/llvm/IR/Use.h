//===- llvm/Use.h - Definition of the Use class -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This defines the Use class.  The Use class represents the operand of an
/// instruction or some other User instance which refers to a Value.  The Use
/// class keeps the "use list" of the referenced value up to date.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_USE_H
#define LLVM_IR_USE_H

#include "llvm-c/Types.h"
#include "llvm/Support/CBindingWrapping.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

template <typename> struct simplify_type;
class User;
class Value;

/// A Use represents the edge between a Value definition and its users.
///
/// This is notionally a two-dimensional linked list. It supports traversing
/// all of the uses for a particular value definition. It also supports jumping
/// directly to the used value when we arrive from the User's operands, and
/// jumping directly to the User when we arrive from the Value's uses.
class Use {
public:
  /// Copy construction is deleted; Use is non-copyable.
  /// \param U Unused source Use (deleted).
  Use(const Use &U) = delete;

  /// Provide a fast substitute to std::swap<Use>
  /// that also works with less standard-compliant compilers
  /// \param RHS Use to swap with this one.
  LLVM_ABI void swap(Use &RHS);

private:
  /// Destructor - Only for zap()
  ~Use() { removeFromList(); }

  /// Constructor
  Use(User *Parent) : Parent(Parent) {}

public:
  friend class Value;
  friend class User;

  /// Implicitly convert this use to the referenced value.
  /// \return The referenced value.
  operator Value *() const { return Val; }
  /// Return the value this use refers to.
  /// \return The value this use refers to.
  Value *get() const { return Val; }

  /// Returns the User that contains this Use.
  ///
  /// For an instruction operand, for example, this will return the
  /// instruction.
  /// \return The User that contains this Use.
  User *getUser() const { return Parent; }

  LLVM_ABI inline void set(Value *Val);

  LLVM_ABI inline Value *operator=(Value *RHS);
  /// Assign from another use by setting this use to the same value.
  /// \param RHS Use whose referenced value is assigned to this use.
  /// \return This use.
  LLVM_ABI inline const Use &operator=(const Use &RHS);

  /// Access members of the referenced value.
  /// \return Pointer to the referenced value.
  Value *operator->() { return Val; }
  /// Access members of the referenced value.
  /// \return Pointer to the referenced value.
  const Value *operator->() const { return Val; }

  /// Return the next use in this value's use list.
  /// \return The next use, or null if this is the last use.
  Use *getNext() const { return Next; }

  /// Return the operand # of this use in its User.
  /// \return The zero-based operand index of this use in its User.
  LLVM_ABI unsigned getOperandNo() const;

  /// Destroys Use operands when the number of operands of
  /// a User changes.
  /// \param Start First use in the operand range to destroy.
  /// \param Stop One-past-the-end of the use range to destroy.
  /// \param del If true, delete the storage for the uses.
  LLVM_ABI static void zap(Use *Start, const Use *Stop, bool del = false);

private:

  Value *Val = nullptr;
  Use *Next = nullptr;
  Use **Prev = nullptr;
  User *Parent = nullptr;

  void addToList(Use **List) {
    Next = *List;
    if (Next)
      Next->Prev = &Next;
    Prev = List;
    *Prev = this;
  }

  void removeFromList() {
    if (Prev) {
      *Prev = Next;
      if (Next) {
        Next->Prev = Prev;
        Next = nullptr;
      }

      Prev = nullptr;
    }
  }
};

/// Allow clients to treat uses just like values when using casting operators.
template <> struct simplify_type<Use> {
  /// Simplified type used by casting operators.
  using SimpleType = Value *;

  /// Return the value referenced by \p Val.
  /// \param Val Use to unwrap.
  /// \return The value referenced by \p Val.
  static SimpleType getSimplifiedValue(Use &Val) { return Val.get(); }
};
/// Allow clients to treat const uses just like values when using casting
/// operators.
template <> struct simplify_type<const Use> {
  /// Simplified type used by casting operators.
  using SimpleType = /*const*/ Value *;

  /// Return the value referenced by \p Val.
  /// \param Val Const use to unwrap.
  /// \return The value referenced by \p Val.
  static SimpleType getSimplifiedValue(const Use &Val) { return Val.get(); }
};

/// Allow clients to treat use pointers just like values when using casting
/// operators.
template <> struct simplify_type<Use *> {
  /// Simplified type used by casting operators.
  using SimpleType = Value *;

  /// Return the value referenced by \p Val, or null if \p Val is null.
  /// \param Val Use pointer to unwrap.
  /// \return The value referenced by \p Val, or null if \p Val is null.
  static SimpleType getSimplifiedValue(Use *Val) {
    return Val ? Val->get() : nullptr;
  }
};
/// Allow clients to treat const use pointers just like values when using
/// casting operators.
template <> struct simplify_type<const Use *> {
  /// Simplified type used by casting operators.
  using SimpleType = /*const*/ Value *;

  /// Return the value referenced by \p Val, or null if \p Val is null.
  /// \param Val Const use pointer to unwrap.
  /// \return The value referenced by \p Val, or null if \p Val is null.
  static SimpleType getSimplifiedValue(const Use *Val) {
    return Val ? Val->get() : nullptr;
  }
};

// Create wrappers for C Binding types (see CBindingWrapping.h).
/// Convert an opaque \c LLVMUseRef to a \c Use pointer.
/// \param P Opaque C API use reference to unwrap.
/// \return The Use pointer corresponding to \p P.
inline Use *unwrap(LLVMUseRef P) {
  return reinterpret_cast<Use *>(P);
}

/// Convert a \c Use pointer to an opaque \c LLVMUseRef.
/// \param P Use to wrap for the C API.
/// \return Opaque C API use reference for \p P.
inline LLVMUseRef wrap(const Use *P) {
  return reinterpret_cast<LLVMUseRef>(const_cast<Use *>(P));
}

} // end namespace llvm

#endif // LLVM_IR_USE_H
