//===- CFG.h ----------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file provides various utilities for inspecting and working with the
/// control flow graph in LLVM IR. This includes generic facilities for
/// iterating successors and predecessors of basic blocks, the successors of
/// specific terminator instructions, etc. It also defines specializations of
/// GraphTraits that allow Function and BasicBlock graphs to be treated as
/// proper graphs for generic algorithms.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_CFG_H
#define LLVM_IR_CFG_H

#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Value.h"
#include <cassert>
#include <cstddef>
#include <iterator>

namespace llvm {

class Instruction;
class Use;

//===----------------------------------------------------------------------===//
// BasicBlock pred_iterator definition
//===----------------------------------------------------------------------===//

/// Forward iterator over the predecessor basic blocks of a block, discovered by
/// walking users of the block that are terminator instructions.
template <class Ptr, class USE_iterator> // Predecessor Iterator
class PredIterator {
public:
  /// Forward-iterator category tag for this predecessor iterator.
  using iterator_category = std::forward_iterator_tag;
  /// Type of the predecessor basic-block pointer yielded by this iterator.
  using value_type = Ptr *;
  /// Signed distance type between predecessor iterators.
  using difference_type = std::ptrdiff_t;
  /// Pointer-to-pointer type required by the iterator concept.
  using pointer = Ptr **;
  /// Reference type returned when dereferencing this iterator (a predecessor
  /// basic block pointer).
  using reference = Ptr *;

protected:
  /// Self-referential type alias for this PredIterator specialization.
  using Self = PredIterator<Ptr, USE_iterator>;
  /// Underlying use-list iterator over users of the basic block.
  USE_iterator It;

public:
  /// Construct a default (singular) predecessor iterator.
  PredIterator() = default;
  /// Construct an iterator to the first predecessor of \p bb.
  /// @param bb Basic block whose predecessors are iterated.
  explicit inline PredIterator(Ptr *bb) : It(bb->user_begin()) {}
  /// Construct the end iterator for predecessors of \p bb.
  /// @param bb Basic block whose predecessor end position is requested.
  /// @param AtEnd Unused tag distinguishing this from the begin constructor.
  inline PredIterator(Ptr *bb, bool AtEnd) : It(bb->user_end()) {}

  /// Return true if this iterator and \p x refer to the same use.
  /// @param x Other predecessor iterator to compare against.
  /// @return True if the iterators refer to the same use.
  inline bool operator==(const Self& x) const { return It == x.It; }
  /// Return true if this iterator and \p x refer to different uses.
  /// @param x Other predecessor iterator to compare against.
  /// @return True if the iterators refer to different uses.
  inline bool operator!=(const Self& x) const { return !operator==(x); }

  /// Return a pointer to the current predecessor basic block.
  /// @return Pointer to the current predecessor basic block.
  inline reference operator*() const {
    assert(!It.atEnd() && "pred_iterator out of range!");
    auto *I = cast<Instruction>(*It);
    assert(I->isTerminator() && "BasicBlock used in non-terminator");
    return I->getParent();
  }
  /// Member access for the current predecessor basic block.
  /// @return Address of the current predecessor basic-block pointer.
  inline pointer *operator->() const { return &operator*(); }

  /// Advance to the next predecessor and return this iterator.
  /// @return Reference to this iterator after advancing.
  inline Self& operator++() {   // Preincrement
    assert(!It.atEnd() && "pred_iterator out of range!");
    ++It;
    return *this;
  }

  /// Advance to the next predecessor, returning the previous position.
  /// @param Unused Unused post-increment tag required by the iterator concept.
  /// @return Copy of the iterator before advancing.
  inline Self operator++(int Unused) { // Postincrement
    Self tmp = *this; ++*this; return tmp;
  }

  /// getOperandNo - Return the operand number in the predecessor's
  /// terminator of the successor.
  /// @return Operand number of the successor in the predecessor terminator.
  unsigned getOperandNo() const {
    return It.getOperandNo();
  }

  /// getUse - Return the operand Use in the predecessor's terminator
  /// of the successor.
  /// @return Use of the successor in the predecessor terminator.
  Use &getUse() const {
    return It.getUse();
  }
};

/// Iterator over the predecessor basic blocks of a \c BasicBlock.
using pred_iterator = PredIterator<BasicBlock, Value::user_iterator>;
/// Read-only predecessor iterator over a \c const BasicBlock.
using const_pred_iterator =
    PredIterator<const BasicBlock, Value::const_user_iterator>;
/// Range over the predecessor basic blocks of a \c BasicBlock.
using pred_range = iterator_range<pred_iterator>;
/// Read-only range over the predecessor basic blocks of a \c const BasicBlock.
using const_pred_range = iterator_range<const_pred_iterator>;

/// Return an iterator to the first predecessor of \p BB.
/// @param BB Basic block whose predecessors are iterated.
/// @return Iterator to the first predecessor of \p BB.
inline pred_iterator pred_begin(BasicBlock *BB) { return pred_iterator(BB); }
/// Return a const iterator to the first predecessor of \p BB.
/// @param BB Basic block whose predecessors are iterated.
/// @return Const iterator to the first predecessor of \p BB.
inline const_pred_iterator pred_begin(const BasicBlock *BB) {
  return const_pred_iterator(BB);
}
/// Return the end iterator for predecessors of \p BB.
/// @param BB Basic block whose predecessor end position is requested.
/// @return Past-the-end predecessor iterator for \p BB.
inline pred_iterator pred_end(BasicBlock *BB) { return pred_iterator(BB, true);}
/// Return a const end iterator for predecessors of \p BB.
/// @param BB Basic block whose predecessor end position is requested.
/// @return Const past-the-end predecessor iterator for \p BB.
inline const_pred_iterator pred_end(const BasicBlock *BB) {
  return const_pred_iterator(BB, true);
}
/// Return true if \p BB has no predecessors.
/// @param BB Basic block to test for an empty predecessor list.
/// @return True if \p BB has no predecessors.
inline bool pred_empty(const BasicBlock *BB) {
  return pred_begin(BB) == pred_end(BB);
}
/// Get the number of predecessors of \p BB. This is a linear time operation.
/// Use \ref BasicBlock::hasNPredecessors() or hasNPredecessorsOrMore if able.
/// @param BB Basic block whose predecessor count is requested.
/// @return Number of predecessors of \p BB.
inline unsigned pred_size(const BasicBlock *BB) {
  return std::distance(pred_begin(BB), pred_end(BB));
}
/// Return a range of predecessor basic blocks of \p BB.
/// @param BB Basic block whose predecessors are returned.
/// @return Range covering the predecessors of \p BB.
inline pred_range predecessors(BasicBlock *BB) {
  return pred_range(pred_begin(BB), pred_end(BB));
}
/// Return a const range of predecessor basic blocks of \p BB.
/// @param BB Basic block whose predecessors are returned.
/// @return Const range covering the predecessors of \p BB.
inline const_pred_range predecessors(const BasicBlock *BB) {
  return const_pred_range(pred_begin(BB), pred_end(BB));
}

//===----------------------------------------------------------------------===//
// Instruction and BasicBlock succ_iterator helpers
//===----------------------------------------------------------------------===//

/// Iterator over successor basic blocks of a terminator instruction.
using succ_iterator = Instruction::succ_iterator;
/// Read-only iterator over successor basic blocks of a terminator instruction.
using const_succ_iterator = Instruction::const_succ_iterator;
/// Range over successor basic blocks of a terminator instruction.
using succ_range = iterator_range<succ_iterator>;
/// Read-only range over successor basic blocks of a terminator instruction.
using const_succ_range = iterator_range<const_succ_iterator>;

/// Return an iterator to the first successor of terminator \p I.
/// @param I Terminator instruction whose successors are iterated.
/// @return Iterator to the first successor of \p I.
inline succ_iterator succ_begin(Instruction *I) {
  return I->successors().begin();
}
/// Return a const iterator to the first successor of terminator \p I.
/// @param I Terminator instruction whose successors are iterated.
/// @return Const iterator to the first successor of \p I.
inline const_succ_iterator succ_begin(const Instruction *I) {
  return I->successors().begin();
}
/// Return the end iterator for successors of terminator \p I.
/// @param I Terminator instruction whose successor end position is requested.
/// @return Past-the-end successor iterator for \p I.
inline succ_iterator succ_end(Instruction *I) { return I->successors().end(); }
/// Return a const end iterator for successors of terminator \p I.
/// @param I Terminator instruction whose successor end position is requested.
/// @return Const past-the-end successor iterator for \p I.
inline const_succ_iterator succ_end(const Instruction *I) {
  return I->successors().end();
}
/// Return true if \p I's terminator has no successor blocks.
/// @param I Terminator instruction to test for an empty successor list.
/// @return True if \p I has no successor blocks.
inline bool succ_empty(const Instruction *I) {
  return succ_begin(I) == succ_end(I);
}
/// Return the number of successors of terminator \p I.
/// @param I Terminator instruction whose successor count is requested.
/// @return Number of successors of \p I.
inline unsigned succ_size(const Instruction *I) {
  return std::distance(succ_begin(I), succ_end(I));
}
/// Return a range of successor basic blocks of terminator \p I.
/// @param I Terminator instruction whose successors are returned.
/// @return Range covering the successors of \p I.
inline succ_range successors(Instruction *I) { return I->successors(); }
/// Return a const range of successor basic blocks of terminator \p I.
/// @param I Terminator instruction whose successors are returned.
/// @return Const range covering the successors of \p I.
inline const_succ_range successors(const Instruction *I) {
  return I->successors();
}

/// Return an iterator to the first successor of basic block \p BB.
/// @param BB Basic block whose terminator successors are iterated.
/// @return Iterator to the first successor of \p BB.
inline succ_iterator succ_begin(BasicBlock *BB) {
  return succ_begin(BB->getTerminator());
}
/// Return a const iterator to the first successor of basic block \p BB.
/// @param BB Basic block whose terminator successors are iterated.
/// @return Const iterator to the first successor of \p BB.
inline const_succ_iterator succ_begin(const BasicBlock *BB) {
  return succ_begin(BB->getTerminator());
}
/// Return the end iterator for successors of basic block \p BB.
/// @param BB Basic block whose terminator successor end is requested.
/// @return Past-the-end successor iterator for \p BB.
inline succ_iterator succ_end(BasicBlock *BB) {
  return succ_end(BB->getTerminator());
}
/// Return a const end iterator for successors of basic block \p BB.
/// @param BB Basic block whose terminator successor end is requested.
/// @return Const past-the-end successor iterator for \p BB.
inline const_succ_iterator succ_end(const BasicBlock *BB) {
  return succ_end(BB->getTerminator());
}
/// Return true if \p BB's terminator has no successor blocks.
/// @param BB Basic block to test for an empty successor list.
/// @return True if \p BB has no successor blocks.
inline bool succ_empty(const BasicBlock *BB) {
  return succ_begin(BB) == succ_end(BB);
}
/// Return the number of successors of basic block \p BB.
/// @param BB Basic block whose terminator successor count is requested.
/// @return Number of successors of \p BB.
inline unsigned succ_size(const BasicBlock *BB) {
  return std::distance(succ_begin(BB), succ_end(BB));
}
/// Return a range of successor basic blocks of basic block \p BB.
/// @param BB Basic block whose terminator successors are returned.
/// @return Range covering the successors of \p BB.
inline succ_range successors(BasicBlock *BB) {
  return successors(BB->getTerminator());
}
/// Return a const range of successor basic blocks of basic block \p BB.
/// @param BB Basic block whose terminator successors are returned.
/// @return Const range covering the successors of \p BB.
inline const_succ_range successors(const BasicBlock *BB) {
  return successors(BB->getTerminator());
}

//===--------------------------------------------------------------------===//
// GraphTraits specializations for basic block graphs (CFGs)
//===--------------------------------------------------------------------===//

// Provide specializations of GraphTraits to be able to treat a function as a
// graph of basic blocks...

/// GraphTraits specialization treating a BasicBlock as a CFG node via successors.
template <> struct GraphTraits<BasicBlock*> {
  /// Graph node type for a mutable basic block pointer.
  using NodeRef = BasicBlock *;
  /// Iterator over successor basic blocks of a CFG node.
  using ChildIteratorType = succ_iterator;

  /// Return \p BB as the graph entry node.
  /// @param BB Basic block used as the entry node.
  /// @return \p BB as the graph entry node.
  static NodeRef getEntryNode(BasicBlock *BB) { return BB; }
  /// Return the begin iterator over successors of \p N.
  /// @param N Basic block whose successors are walked.
  /// @return Begin iterator over successors of \p N.
  static ChildIteratorType child_begin(NodeRef N) { return succ_begin(N); }
  /// Return the end iterator over successors of \p N.
  /// @param N Basic block whose successors are walked.
  /// @return End iterator over successors of \p N.
  static ChildIteratorType child_end(NodeRef N) { return succ_end(N); }

  /// Return the dense number of basic block \p BB.
  /// @param BB Basic block whose number is requested.
  /// @return Dense number of \p BB.
  static unsigned getNumber(const BasicBlock *BB) { return BB->getNumber(); }
};

static_assert(GraphHasNodeNumbers<BasicBlock *>,
              "GraphTraits getNumber() not detected");

/// GraphTraits specialization treating a const BasicBlock as a CFG node via successors.
template <> struct GraphTraits<const BasicBlock*> {
  /// Graph node type for a const basic block pointer.
  using NodeRef = const BasicBlock *;
  /// Const iterator over successor basic blocks of a CFG node.
  using ChildIteratorType = const_succ_iterator;

  /// Return \p BB as the graph entry node.
  /// @param BB Basic block used as the entry node.
  /// @return \p BB as the graph entry node.
  static NodeRef getEntryNode(const BasicBlock *BB) { return BB; }

  /// Return the begin iterator over successors of \p N.
  /// @param N Basic block whose successors are walked.
  /// @return Begin iterator over successors of \p N.
  static ChildIteratorType child_begin(NodeRef N) { return succ_begin(N); }
  /// Return the end iterator over successors of \p N.
  /// @param N Basic block whose successors are walked.
  /// @return End iterator over successors of \p N.
  static ChildIteratorType child_end(NodeRef N) { return succ_end(N); }

  /// Return the dense number of basic block \p BB.
  /// @param BB Basic block whose number is requested.
  /// @return Dense number of \p BB.
  static unsigned getNumber(const BasicBlock *BB) { return BB->getNumber(); }
};

static_assert(GraphHasNodeNumbers<const BasicBlock *>,
              "GraphTraits getNumber() not detected");

// Provide specializations of GraphTraits to be able to treat a function as a
// graph of basic blocks... and to walk it in inverse order.  Inverse order for
// a function is considered to be when traversing the predecessor edges of a BB
// instead of the successor edges.
//
/// GraphTraits specialization walking a BasicBlock CFG in inverse (predecessor) order.
template <> struct GraphTraits<Inverse<BasicBlock*>> {
  /// Graph node type for a mutable basic block pointer.
  using NodeRef = BasicBlock *;
  /// Iterator over predecessor basic blocks of a CFG node.
  using ChildIteratorType = pred_iterator;

  /// Return the wrapped basic block as the inverse-graph entry node.
  /// @param G Inverse wrapper around the entry basic block.
  /// @return The basic block wrapped by \p G.
  static NodeRef getEntryNode(Inverse<BasicBlock *> G) { return G.Graph; }
  /// Return the begin iterator over predecessors of \p N.
  /// @param N Basic block whose predecessors are walked.
  /// @return Begin iterator over predecessors of \p N.
  static ChildIteratorType child_begin(NodeRef N) { return pred_begin(N); }
  /// Return the end iterator over predecessors of \p N.
  /// @param N Basic block whose predecessors are walked.
  /// @return End iterator over predecessors of \p N.
  static ChildIteratorType child_end(NodeRef N) { return pred_end(N); }

  /// Return the dense number of basic block \p BB.
  /// @param BB Basic block whose number is requested.
  /// @return Dense number of \p BB.
  static unsigned getNumber(const BasicBlock *BB) { return BB->getNumber(); }
};

static_assert(GraphHasNodeNumbers<Inverse<BasicBlock *>>,
              "GraphTraits getNumber() not detected");

/// GraphTraits specialization walking a const BasicBlock CFG in inverse order.
template <> struct GraphTraits<Inverse<const BasicBlock*>> {
  /// Graph node type for a const basic block pointer.
  using NodeRef = const BasicBlock *;
  /// Const iterator over predecessor basic blocks of a CFG node.
  using ChildIteratorType = const_pred_iterator;

  /// Return the wrapped basic block as the inverse-graph entry node.
  /// @param G Inverse wrapper around the entry basic block.
  /// @return The basic block wrapped by \p G.
  static NodeRef getEntryNode(Inverse<const BasicBlock *> G) { return G.Graph; }
  /// Return the begin iterator over predecessors of \p N.
  /// @param N Basic block whose predecessors are walked.
  /// @return Begin iterator over predecessors of \p N.
  static ChildIteratorType child_begin(NodeRef N) { return pred_begin(N); }
  /// Return the end iterator over predecessors of \p N.
  /// @param N Basic block whose predecessors are walked.
  /// @return End iterator over predecessors of \p N.
  static ChildIteratorType child_end(NodeRef N) { return pred_end(N); }

  /// Return the dense number of basic block \p BB.
  /// @param BB Basic block whose number is requested.
  /// @return Dense number of \p BB.
  static unsigned getNumber(const BasicBlock *BB) { return BB->getNumber(); }
};

static_assert(GraphHasNodeNumbers<Inverse<const BasicBlock *>>,
              "GraphTraits getNumber() not detected");

//===--------------------------------------------------------------------===//
// GraphTraits specializations for function basic block graphs (CFGs)
//===--------------------------------------------------------------------===//

// Provide specializations of GraphTraits to be able to treat a function as a
// graph of basic blocks... these are the same as the basic block iterators,
// except that the root node is implicitly the first node of the function.
//
/// GraphTraits specialization treating a Function as a CFG of basic blocks.
template <> struct GraphTraits<Function*> : public GraphTraits<BasicBlock*> {
  /// Return the entry basic block of function \p F.
  /// @param F Function whose entry block is the graph entry.
  /// @return Entry basic block of \p F.
  static NodeRef getEntryNode(Function *F) { return &F->getEntryBlock(); }

  // nodes_iterator/begin/end - Allow iteration over all nodes in the graph
  /// Iterator over all basic-block nodes in a function CFG.
  using nodes_iterator = pointer_iterator<Function::iterator>;

  /// Return the begin iterator over all basic blocks of \p F.
  /// @param F Function whose basic blocks are iterated.
  /// @return Begin iterator over basic blocks of \p F.
  static nodes_iterator nodes_begin(Function *F) {
    return nodes_iterator(F->begin());
  }

  /// Return the end iterator over all basic blocks of \p F.
  /// @param F Function whose basic blocks are iterated.
  /// @return End iterator over basic blocks of \p F.
  static nodes_iterator nodes_end(Function *F) {
    return nodes_iterator(F->end());
  }

  /// Return the number of basic blocks in function \p F.
  /// @param F Function whose block count is requested.
  /// @return Number of basic blocks in \p F.
  static size_t size(Function *F) { return F->size(); }

  /// Return the maximum basic-block number used in function \p F.
  /// @param F Function whose max block number is requested.
  /// @return Maximum basic-block number used in \p F.
  static unsigned getMaxNumber(const Function *F) {
    return F->getMaxBlockNumber();
  }
  /// Return the basic-block number epoch of function \p F.
  /// @param F Function whose block-number epoch is requested.
  /// @return Basic-block number epoch of \p F.
  static unsigned getNumberEpoch(const Function *F) {
    return F->getBlockNumberEpoch();
  }
};
/// GraphTraits specialization treating a const Function as a CFG of basic blocks.
template <> struct GraphTraits<const Function*> :
  public GraphTraits<const BasicBlock*> {
  /// Return the entry basic block of function \p F.
  /// @param F Function whose entry block is the graph entry.
  /// @return Entry basic block of \p F.
  static NodeRef getEntryNode(const Function *F) { return &F->getEntryBlock(); }

  // nodes_iterator/begin/end - Allow iteration over all nodes in the graph
  /// Const iterator over all basic-block nodes in a function CFG.
  using nodes_iterator = pointer_iterator<Function::const_iterator>;

  /// Return the begin iterator over all basic blocks of \p F.
  /// @param F Function whose basic blocks are iterated.
  /// @return Begin iterator over basic blocks of \p F.
  static nodes_iterator nodes_begin(const Function *F) {
    return nodes_iterator(F->begin());
  }

  /// Return the end iterator over all basic blocks of \p F.
  /// @param F Function whose basic blocks are iterated.
  /// @return End iterator over basic blocks of \p F.
  static nodes_iterator nodes_end(const Function *F) {
    return nodes_iterator(F->end());
  }

  /// Return the number of basic blocks in function \p F.
  /// @param F Function whose block count is requested.
  /// @return Number of basic blocks in \p F.
  static size_t size(const Function *F) { return F->size(); }

  /// Return the maximum basic-block number used in function \p F.
  /// @param F Function whose max block number is requested.
  /// @return Maximum basic-block number used in \p F.
  static unsigned getMaxNumber(const Function *F) {
    return F->getMaxBlockNumber();
  }
  /// Return the basic-block number epoch of function \p F.
  /// @param F Function whose block-number epoch is requested.
  /// @return Basic-block number epoch of \p F.
  static unsigned getNumberEpoch(const Function *F) {
    return F->getBlockNumberEpoch();
  }
};

// Provide specializations of GraphTraits to be able to treat a function as a
// graph of basic blocks... and to walk it in inverse order.  Inverse order for
// a function is considered to be when traversing the predecessor edges of a BB
// instead of the successor edges.
//
/// GraphTraits specialization walking a Function CFG in inverse (predecessor) order.
template <> struct GraphTraits<Inverse<Function*>> :
  public GraphTraits<Inverse<BasicBlock*>> {
  /// Return the entry basic block of the wrapped function as the inverse entry.
  /// @param G Inverse wrapper around the function whose entry is used.
  /// @return Entry basic block of the function wrapped by \p G.
  static NodeRef getEntryNode(Inverse<Function *> G) {
    return &G.Graph->getEntryBlock();
  }

  /// Return the maximum basic-block number used in function \p F.
  /// @param F Function whose max block number is requested.
  /// @return Maximum basic-block number used in \p F.
  static unsigned getMaxNumber(const Function *F) {
    return F->getMaxBlockNumber();
  }
  /// Return the basic-block number epoch of function \p F.
  /// @param F Function whose block-number epoch is requested.
  /// @return Basic-block number epoch of \p F.
  static unsigned getNumberEpoch(const Function *F) {
    return F->getBlockNumberEpoch();
  }
};
/// GraphTraits specialization walking a const Function CFG in inverse order.
template <> struct GraphTraits<Inverse<const Function*>> :
  public GraphTraits<Inverse<const BasicBlock*>> {
  /// Return the entry basic block of the wrapped function as the inverse entry.
  /// @param G Inverse wrapper around the function whose entry is used.
  /// @return Entry basic block of the function wrapped by \p G.
  static NodeRef getEntryNode(Inverse<const Function *> G) {
    return &G.Graph->getEntryBlock();
  }

  /// Return the maximum basic-block number used in function \p F.
  /// @param F Function whose max block number is requested.
  /// @return Maximum basic-block number used in \p F.
  static unsigned getMaxNumber(const Function *F) {
    return F->getMaxBlockNumber();
  }
  /// Return the basic-block number epoch of function \p F.
  /// @param F Function whose block-number epoch is requested.
  /// @return Basic-block number epoch of \p F.
  static unsigned getNumberEpoch(const Function *F) {
    return F->getBlockNumberEpoch();
  }
};

} // end namespace llvm

#endif // LLVM_IR_CFG_H
