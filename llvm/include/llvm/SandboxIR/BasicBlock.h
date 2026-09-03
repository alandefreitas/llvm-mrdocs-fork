//===- BasicBlock.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SANDBOXIR_BASICBLOCK_H
#define LLVM_SANDBOXIR_BASICBLOCK_H

#include "llvm/IR/BasicBlock.h"
#include "llvm/SandboxIR/Value.h"
#include "llvm/Support/Compiler.h"

namespace llvm::sandboxir {

class BasicBlock;
class Function;
class Instruction;

/// Iterator for `Instruction`s in a `BasicBlock.
/// \Returns an sandboxir::Instruction & when derereferenced.
class BBIterator {
public:
  /// Signed distance between iterators.
  using difference_type = std::ptrdiff_t;
  /// Instruction type referred to by this iterator.
  using value_type = Instruction;
  /// Pointer to an instruction.
  using pointer = value_type *;
  /// Reference to an instruction.
  using reference = value_type &;
  /// Bidirectional traversal category.
  using iterator_category = std::bidirectional_iterator_tag;

private:
  llvm::BasicBlock *BB;
  llvm::BasicBlock::iterator It;
  Context *Ctx;
  LLVM_ABI pointer getInstr(llvm::BasicBlock::iterator It) const;

public:
  /// Construct a singular (empty) iterator.
  BBIterator() : BB(nullptr), Ctx(nullptr) {}
  /// Construct an iterator over instructions in \p BB at position \p It.
  /// \param BB Underlying LLVM basic block.
  /// \param It Position within \p BB.
  /// \param Ctx SandboxIR context used to map LLVM instructions.
  BBIterator(llvm::BasicBlock *BB, llvm::BasicBlock::iterator It, Context *Ctx)
      : BB(BB), It(It), Ctx(Ctx) {}
  /// Dereference to the SandboxIR instruction at this position.
  /// \Returns A reference to the SandboxIR instruction.
  reference operator*() const { return *getInstr(It); }
  /// Advance to the next instruction.
  /// \Returns A reference to this iterator after advancing.
  LLVM_ABI BBIterator &operator++();
  /// Post-increment to the next instruction.
  /// \param Unused Unused postfix-discriminator parameter.
  /// \Returns A copy of the iterator before advancing.
  BBIterator operator++(int Unused) {
    auto Copy = *this;
    ++*this;
    return Copy;
  }
  /// Retreat to the previous instruction.
  /// \Returns A reference to this iterator after retreating.
  LLVM_ABI BBIterator &operator--();
  /// Post-decrement to the previous instruction.
  /// \param Unused Unused postfix-discriminator parameter.
  /// \Returns A copy of the iterator before retreating.
  BBIterator operator--(int Unused) {
    auto Copy = *this;
    --*this;
    return Copy;
  }
  /// Return true if this iterator and \p Other refer to the same position.
  /// \param Other Iterator to compare against.
  /// \Returns True if both iterators refer to the same position.
  bool operator==(const BBIterator &Other) const {
    assert(Ctx == Other.Ctx && "BBIterators in different context!");
    return It == Other.It;
  }
  /// Return true if this iterator and \p Other refer to different positions.
  /// \param Other Iterator to compare against.
  /// \Returns True if the iterators refer to different positions.
  bool operator!=(const BBIterator &Other) const { return !(*this == Other); }
  /// Return the SandboxIR instruction at this iterator, or null if unmapped.
  /// \Returns the SBInstruction that corresponds to this iterator, or null if
  /// the instruction is not found in the IR-to-SandboxIR tables.
  pointer get() const { return getInstr(It); }
  /// Return the parent basic block of the instruction at this iterator.
  /// \Returns the parent BB.
  LLVM_ABI BasicBlock *getNodeParent() const;
};

/// Contains a list of sandboxir::Instruction's.
class BasicBlock : public Value {
  /// Builds a graph that contains all values in \p BB in their original form
  /// i.e., no vectorization is taking place here.
  LLVM_ABI void buildBasicBlockFromLLVMIR(llvm::BasicBlock *LLVMBB);
  friend class Context;     // For `buildBasicBlockFromIR`
  friend class Instruction; // For LLVM Val.

  BasicBlock(llvm::BasicBlock *BB, Context &SBCtx)
      : Value(ClassID::Block, BB, SBCtx) {
    buildBasicBlockFromLLVMIR(BB);
  }

public:
  /// Destroy this basic block wrapper.
  ~BasicBlock() override = default;
  /// For isa/dyn_cast.
  /// \param From Value to test for BasicBlock.
  /// \Returns True if \p From is a BasicBlock.
  static bool classof(const Value *From) {
    return From->getSubclassID() == Value::ClassID::Block;
  }
  /// Return the enclosing function, or null if none.
  /// \Returns The parent Function, or null if none.
  LLVM_ABI Function *getParent() const;
  /// Iterator over instructions in this block.
  using iterator = BBIterator;
  /// Return an iterator to the first instruction.
  /// \Returns An iterator to the first instruction.
  LLVM_ABI iterator begin() const;
  /// Return an iterator to the past-the-end position.
  /// \Returns An iterator to the past-the-end position.
  iterator end() const {
    auto *BB = cast<llvm::BasicBlock>(Val);
    return iterator(BB, BB->end(), &Ctx);
  }
  /// Return a reverse iterator to the last instruction.
  /// \Returns A reverse iterator to the last instruction.
  std::reverse_iterator<iterator> rbegin() const {
    return std::make_reverse_iterator(end());
  }
  /// Return a reverse iterator to the past-the-rend position.
  /// \Returns A reverse iterator to the past-the-rend position.
  std::reverse_iterator<iterator> rend() const {
    return std::make_reverse_iterator(begin());
  }
  /// Return the SandboxIR context for this block.
  /// \Returns The SandboxIR Context for this block.
  Context &getContext() const { return Ctx; }
  /// Return the terminator instruction of this block.
  /// \Returns The terminator Instruction of this block.
  LLVM_ABI Instruction *getTerminator() const;
  /// Return true if this block contains no instructions.
  /// \Returns True if this block contains no instructions.
  bool empty() const { return begin() == end(); }
  /// Return the first instruction in the block.
  /// \Returns A reference to the first instruction.
  LLVM_ABI Instruction &front() const;
  /// Return the last instruction in the block.
  /// \Returns A reference to the last instruction.
  LLVM_ABI Instruction &back() const;

#ifndef NDEBUG
  /// Verify that this wraps a well-formed LLVM basic block.
  void verify() const final;
  /// Dump this basic block to \p OS.
  /// \param OS Output stream.
  LLVM_ABI void dumpOS(raw_ostream &OS) const final;
#endif
};

} // namespace llvm::sandboxir

#endif // LLVM_SANDBOXIR_BASICBLOCK_H
