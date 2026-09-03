//===- llvm/Analysis/Trace.h - Represent one trace of LLVM code -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class represents a single trace of LLVM basic blocks.  A trace is a
// single entry, multiple exit, region of code that is often hot.  Trace-based
// optimizations treat traces almost like they are a large, strange, basic
// block: because the trace path is assumed to be hot, optimizations for the
// fall-through path are made at the expense of the non-fall-through paths.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_TRACE_H
#define LLVM_ANALYSIS_TRACE_H

#include "llvm/Support/Compiler.h"
#include <cassert>
#include <vector>

namespace llvm {

class BasicBlock;
class Function;
class Module;
class raw_ostream;

/// A single-entry, multiple-exit sequence of basic blocks on a hot path.
///
/// Trace-based optimizations treat a trace almost like a large basic block:
/// because the path is assumed to be hot, fall-through optimizations are made
/// at the expense of the non-fall-through paths.
class Trace {
  using BasicBlockListType = std::vector<BasicBlock *>;

  BasicBlockListType BasicBlocks;

public:
  /// Construct a trace from an ordered vector of basic blocks.
  ///
  /// The blocks reside in the function which is the parent of the first basic
  /// block in the vector.
  /// @param vBB Basic blocks that form this trace, in order.
  Trace(const std::vector<BasicBlock *> &vBB) : BasicBlocks (vBB) {}

  /// Return the entry basic block (first block) of the trace.
  /// @return The first basic block in this trace.
  BasicBlock *getEntryBasicBlock () const { return BasicBlocks[0]; }

  /// Return the basic block at index \p i in the trace.
  /// @param i Zero-based index of the basic block.
  /// @return The basic block at index \p i.
  BasicBlock *operator[](unsigned i) const { return BasicBlocks[i]; }
  /// Return the basic block at index \p i in the trace.
  /// @param i Zero-based index of the basic block.
  /// @return The basic block at index \p i.
  BasicBlock *getBlock(unsigned i)   const { return BasicBlocks[i]; }

  /// Return this trace's parent function.
  /// @return The function that contains this trace.
  LLVM_ABI Function *getFunction() const;

  /// Return the module that contains this trace's parent function.
  /// @return The module that contains this trace's parent function.
  LLVM_ABI Module *getModule() const;

  /// Return the index of the specified basic block in the trace, or -1 if it
  /// is not in the trace.
  /// @param X Basic block to look up in this trace.
  /// @return Zero-based index of \p X, or -1 if \p X is not in the trace.
  int getBlockIndex(const BasicBlock *X) const {
    for (unsigned i = 0, e = BasicBlocks.size(); i != e; ++i)
      if (BasicBlocks[i] == X)
        return i;
    return -1;
  }

  /// Return true if this trace contains the given basic block.
  /// @param X Basic block to test for membership.
  /// @return True if \p X is in this trace.
  bool contains(const BasicBlock *X) const {
    return getBlockIndex(X) != -1;
  }

  /// Return true if \p B1 occurs before \p B2 in the trace, or if they are the
  /// same block.
  ///
  /// Both blocks must be in the trace.
  /// @param B1 Basic block that should appear first, or equal \p B2.
  /// @param B2 Basic block that should appear later, or equal \p B1.
  /// @return True if \p B1 occurs at or before \p B2 in this trace.
  bool dominates(const BasicBlock *B1, const BasicBlock *B2) const {
    int B1Idx = getBlockIndex(B1), B2Idx = getBlockIndex(B2);
    assert(B1Idx != -1 && B2Idx != -1 && "Block is not in the trace!");
    return B1Idx <= B2Idx;
  }

  /// Mutable iterator over the basic blocks in this trace.
  using iterator = BasicBlockListType::iterator;
  /// Const iterator over the basic blocks in this trace.
  using const_iterator = BasicBlockListType::const_iterator;
  /// Mutable reverse iterator over the basic blocks in this trace.
  using reverse_iterator = std::reverse_iterator<iterator>;
  /// Const reverse iterator over the basic blocks in this trace.
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  /// Return an iterator to the first basic block in the trace.
  /// @return Iterator to the first basic block.
  iterator                begin()       { return BasicBlocks.begin(); }
  /// Return a const iterator to the first basic block in the trace.
  /// @return Const iterator to the first basic block.
  const_iterator          begin() const { return BasicBlocks.begin(); }
  /// Return an iterator past the last basic block in the trace.
  /// @return Iterator past the last basic block.
  iterator                end  ()       { return BasicBlocks.end();   }
  /// Return a const iterator past the last basic block in the trace.
  /// @return Const iterator past the last basic block.
  const_iterator          end  () const { return BasicBlocks.end();   }

  /// Return a reverse iterator to the last basic block in the trace.
  /// @return Reverse iterator to the last basic block.
  reverse_iterator       rbegin()       { return BasicBlocks.rbegin(); }
  /// Return a const reverse iterator to the last basic block in the trace.
  /// @return Const reverse iterator to the last basic block.
  const_reverse_iterator rbegin() const { return BasicBlocks.rbegin(); }
  /// Return a reverse iterator past the first basic block in the trace.
  /// @return Reverse iterator past the first basic block.
  reverse_iterator       rend  ()       { return BasicBlocks.rend();   }
  /// Return a const reverse iterator past the first basic block in the trace.
  /// @return Const reverse iterator past the first basic block.
  const_reverse_iterator rend  () const { return BasicBlocks.rend();   }

  /// Return the number of basic blocks in the trace.
  /// @return The number of basic blocks in this trace.
  unsigned                 size() const { return BasicBlocks.size(); }
  /// Return true if the trace contains no basic blocks.
  /// @return True if this trace contains no basic blocks.
  bool                    empty() const { return BasicBlocks.empty(); }

  /// Erase the basic block at iterator \p q from the trace.
  /// @param q Iterator to the basic block to erase.
  /// @return Iterator to the basic block following the erased one.
  iterator erase(iterator q)               { return BasicBlocks.erase (q); }
  /// Erase the basic blocks in the half-open range [\p q1, \p q2).
  /// @param q1 Iterator to the first basic block to erase.
  /// @param q2 Iterator past the last basic block to erase.
  /// @return Iterator to the basic block following the last erased one.
  iterator erase(iterator q1, iterator q2) { return BasicBlocks.erase (q1, q2); }

  /// Write this trace to the given output stream.
  /// @param O Output stream.
  LLVM_ABI void print(raw_ostream &O) const;

  /// dump - Debugger convenience method; writes trace to standard error
  /// output stream.
  LLVM_ABI void dump() const;
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_TRACE_H
