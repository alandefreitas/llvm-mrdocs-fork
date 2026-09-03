//===- llvm/IR/TypedPointerType.h - Typed Pointer Type --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains typed pointer type information. It is separated out into
// a separate file to make it less likely to accidentally use this type.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_TYPEDPOINTERTYPE_H
#define LLVM_IR_TYPEDPOINTERTYPE_H

#include "llvm/IR/Type.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

/// A typed pointer type used by some GPU targets.
///
/// A few GPU targets, such as DXIL and SPIR-V, have typed pointers. This
/// pointer type abstraction is used for tracking the types of these pointers.
/// It is not legal to use this type, or derived types containing this type, in
/// LLVM IR.
class TypedPointerType : public Type {
  explicit TypedPointerType(Type *ElType, unsigned AddrSpace);

  Type *PointeeTy;

public:
  /// Copy construction is deleted; TypedPointerType is uniqued.
  /// \param Unused Unused copy source (deleted).
  TypedPointerType(const TypedPointerType &Unused) = delete;
  /// Copy assignment is deleted; TypedPointerType is uniqued.
  /// \param Unused Unused copy source (deleted).
  TypedPointerType &operator=(const TypedPointerType &Unused) = delete;

  /// This constructs a pointer to an object of the specified type in a numbered
  /// address space.
  /// \param ElementType Pointee type of the typed pointer.
  /// \param AddressSpace Address space number for the pointer.
  /// \return The uniqued TypedPointerType for the given element type and address
  /// space.
  LLVM_ABI static TypedPointerType *get(Type *ElementType,
                                        unsigned AddressSpace);

  /// Return true if the specified type is valid as a element type.
  /// \param ElemTy Candidate pointee type.
  /// \return true if \p ElemTy is a valid pointee type for a typed pointer.
  LLVM_ABI static bool isValidElementType(Type *ElemTy);

  /// Return the address space of the Pointer type.
  /// \return The address space number of this typed pointer.
  unsigned getAddressSpace() const { return getSubclassData(); }

  /// Return the element type of this typed pointer.
  /// \return The pointee type of this typed pointer.
  Type *getElementType() const { return PointeeTy; }

  /// Implement support type inquiry through isa, cast, and dyn_cast.
  /// \param T Type to test.
  /// \return true if \p T is a TypedPointerType.
  static bool classof(const Type *T) {
    return T->getTypeID() == TypedPointerTyID;
  }
};

} // namespace llvm

#endif // LLVM_IR_TYPEDPOINTERTYPE_H
