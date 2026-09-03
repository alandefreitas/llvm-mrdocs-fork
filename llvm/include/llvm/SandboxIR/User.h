//===- User.h ---------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SANDBOXIR_USER_H
#define LLVM_SANDBOXIR_USER_H

#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/SandboxIR/Use.h"
#include "llvm/SandboxIR/Value.h"
#include "llvm/Support/Compiler.h"

namespace llvm::sandboxir {

class Context;

/// Iterator for the `Use` edges of a User's operands.
/// \Returns the operand `Use` when dereferenced.
class OperandUseIterator {
  sandboxir::Use Use;
  /// Don't let the user create a non-empty OperandUseIterator.
  OperandUseIterator(const class Use &Use) : Use(Use) {}
  friend class User;                                  // For constructor
#define DEF_INSTR(ID, OPC, CLASS) friend class CLASS; // For constructor
#include "llvm/SandboxIR/Values.def"

public:
  /// Signed distance between iterators.
  using difference_type = std::ptrdiff_t;
  /// Use type referred to by this iterator.
  using value_type = sandboxir::Use;
  /// Pointer to a Use.
  using pointer = value_type *;
  /// Reference to a Use.
  using reference = value_type &;
  /// Forward traversal category.
  using iterator_category = std::forward_iterator_tag;

  /// Construct a singular (empty) iterator.
  OperandUseIterator() = default;
  /// Dereference to the operand Use at this position.
  /// \Returns The operand Use at this position.
  LLVM_ABI value_type operator*() const;
  /// Advance to the next operand Use.
  /// \Returns A reference to this iterator after advancing.
  LLVM_ABI OperandUseIterator &operator++();
  /// Post-increment to the next operand Use.
  /// \param Unused Unused postfix-discriminator parameter.
  /// \Returns A copy of the iterator before advancing.
  OperandUseIterator operator++(int Unused) {
    auto Copy = *this;
    this->operator++();
    return Copy;
  }
  /// Return true if this iterator and \p Other refer to the same Use.
  /// \param Other Iterator to compare against.
  /// \Returns True if both iterators refer to the same Use.
  bool operator==(const OperandUseIterator &Other) const {
    return Use == Other.Use;
  }
  /// Return true if this iterator and \p Other refer to different Uses.
  /// \param Other Iterator to compare against.
  /// \Returns True if the iterators refer to different Uses.
  bool operator!=(const OperandUseIterator &Other) const {
    return !(*this == Other);
  }
  /// Return an iterator \p Num positions ahead.
  /// \param Num Number of positions to advance.
  /// \Returns An iterator \p Num positions ahead of this one.
  LLVM_ABI OperandUseIterator operator+(unsigned Num) const;
  /// Return an iterator \p Num positions behind.
  /// \param Num Number of positions to retreat.
  /// \Returns An iterator \p Num positions behind this one.
  LLVM_ABI OperandUseIterator operator-(unsigned Num) const;
  /// Return the signed distance from \p Other to this iterator.
  /// \param Other Iterator to measure from.
  /// \Returns The signed distance from \p Other to this iterator.
  LLVM_ABI int operator-(const OperandUseIterator &Other) const;
};

/// A sandboxir::User has operands.
class LLVM_ABI User : public Value {
protected:
  /// Construct a User wrapping \p V with subclass id \p ID.
  /// \param ID Subclass identifier.
  /// \param V Underlying LLVM value.
  /// \param Ctx SandboxIR context.
  User(ClassID ID, llvm::Value *V, Context &Ctx) : Value(ID, V, Ctx) {}

  /// Return the Use edge that corresponds to \p OpIdx.
  ///
  /// This is the default implementation that works for instructions that match
  /// the underlying LLVM instruction. All others should use a different
  /// implementation.
  /// \param OpIdx Operand index.
  /// \param Verify Whether to verify that \p OpIdx is in range.
  /// \Returns The Use edge for operand \p OpIdx.
  Use getOperandUseDefault(unsigned OpIdx, bool Verify) const;
  /// Return the Use for the \p OpIdx'th operand.
  ///
  /// This is virtual to allow instructions to deviate from the LLVM IR
  /// operands, which is a requirement for sandboxir Instructions that consist
  /// of more than one LLVM Instruction.
  /// \param OpIdx Operand index.
  /// \param Verify Whether to verify that \p OpIdx is in range.
  /// \Returns The Use for the \p OpIdx'th operand.
  virtual Use getOperandUseInternal(unsigned OpIdx, bool Verify) const = 0;
  friend class OperandUseIterator; // for getOperandUseInternal()

  /// Return the operand index of \p Use for single-LLVMIR-instruction Users.
  ///
  /// The default implementation works only for single-LLVMIR-instruction Users
  /// and only if they match exactly the LLVM instruction.
  /// \param Use Operand use whose index is requested.
  /// \Returns The operand index of \p Use.
  unsigned getUseOperandNoDefault(const Use &Use) const {
    return Use.LLVMUse->getOperandNo();
  }
  /// Return the operand index of \p Use.
  /// \param Use Operand use whose index is requested.
  /// \Returns The operand index of \p Use.
  virtual unsigned getUseOperandNo(const Use &Use) const = 0;
  friend unsigned Use::getOperandNo() const; // For getUseOperandNo()

  /// Swap the operands at indices \p OpIdxA and \p OpIdxB.
  /// \param OpIdxA First operand index.
  /// \param OpIdxB Second operand index.
  void swapOperandsInternal(unsigned OpIdxA, unsigned OpIdxB) {
    assert(OpIdxA < getNumOperands() && "OpIdxA out of bounds!");
    assert(OpIdxB < getNumOperands() && "OpIdxB out of bounds!");
    auto UseA = getOperandUse(OpIdxA);
    auto UseB = getOperandUse(OpIdxB);
    UseA.swap(UseB);
  }

#ifndef NDEBUG
  /// Verify that \p Use belongs to this User's underlying LLVM value.
  /// \param Use LLVM use to check.
  void verifyUserOfLLVMUse(const llvm::Use &Use) const;
#endif // NDEBUG

public:
  /// For isa/dyn_cast.
  /// \param From Value to test for User.
  /// \Returns True if \p From is a User.
  static bool classof(const Value *From);
  /// Iterator over operand Uses.
  using op_iterator = OperandUseIterator;
  /// Const iterator over operand Uses.
  using const_op_iterator = OperandUseIterator;
  /// Range of operand Use iterators.
  using op_range = iterator_range<op_iterator>;
  /// Const range of operand Use iterators.
  using const_op_range = iterator_range<const_op_iterator>;

  /// Return an iterator to the first operand Use.
  /// \Returns An iterator to the first operand Use.
  virtual op_iterator op_begin() {
    assert(isa<llvm::User>(Val) && "Expect User value!");
    return op_iterator(getOperandUseInternal(0, /*Verify=*/false));
  }
  /// Return an iterator to the past-the-end operand Use.
  /// \Returns An iterator to the past-the-end operand Use.
  virtual op_iterator op_end() {
    assert(isa<llvm::User>(Val) && "Expect User value!");
    return op_iterator(
        getOperandUseInternal(getNumOperands(), /*Verify=*/false));
  }
  /// Return a const iterator to the first operand Use.
  /// \Returns A const iterator to the first operand Use.
  virtual const_op_iterator op_begin() const {
    return const_cast<User *>(this)->op_begin();
  }
  /// Return a const iterator to the past-the-end operand Use.
  /// \Returns A const iterator to the past-the-end operand Use.
  virtual const_op_iterator op_end() const {
    return const_cast<User *>(this)->op_end();
  }

  /// Return a range over this User's operand Uses.
  /// \Returns A range over this User's operand Uses.
  op_range operands() { return make_range<op_iterator>(op_begin(), op_end()); }
  /// Return a const range over this User's operand Uses.
  /// \Returns A const range over this User's operand Uses.
  const_op_range operands() const {
    return make_range<const_op_iterator>(op_begin(), op_end());
  }
  /// Return the value of the operand at \p OpIdx.
  /// \param OpIdx Operand index.
  /// \Returns The value of the operand at \p OpIdx.
  Value *getOperand(unsigned OpIdx) const { return getOperandUse(OpIdx).get(); }
  /// Return the operand use edge for \p OpIdx.
  ///
  /// NOTE: This should also work for OpIdx == getNumOperands(), which is used
  /// for op_end().
  /// \param OpIdx Operand index.
  /// \Returns The operand use edge for \p OpIdx.
  Use getOperandUse(unsigned OpIdx) const {
    return getOperandUseInternal(OpIdx, /*Verify=*/true);
  }
  /// Return the number of operands.
  /// \Returns The number of operands.
  virtual unsigned getNumOperands() const {
    return isa<llvm::User>(Val) ? cast<llvm::User>(Val)->getNumOperands() : 0;
  }

  /// Set the operand at \p OperandIdx to \p Operand.
  /// \param OperandIdx Operand index to update.
  /// \param Operand New operand value.
  virtual void setOperand(unsigned OperandIdx, Value *Operand);
  /// Replaces any operands that match \p FromV with \p ToV. Returns whether any
  /// operands were replaced.
  /// \param FromV Operand value to replace.
  /// \param ToV Replacement operand value.
  /// \Returns True if any operands were replaced.
  bool replaceUsesOfWith(Value *FromV, Value *ToV);

#ifndef NDEBUG
  /// Verify that this wraps an LLVM User.
  void verify() const override {
    assert(isa<llvm::User>(Val) && "Expected User!");
  }
  /// Dump the common header for this User to \p OS.
  /// \param OS Output stream.
  void dumpCommonHeader(raw_ostream &OS) const final;
  /// Dump this User to \p OS.
  /// \param OS Output stream.
  void dumpOS(raw_ostream &OS) const override {
    // TODO: Remove this tmp implementation once we get the Instruction classes.
  }
#endif
};

} // namespace llvm::sandboxir

#endif // LLVM_SANDBOXIR_USER_H
