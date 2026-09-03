//===- DeltaTree.h - B-Tree for Rewrite Delta tracking ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the DeltaTree class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_DELTATREE_H
#define LLVM_ADT_DELTATREE_H

#include "llvm/Support/Compiler.h"

namespace llvm {

/// B-tree mapping file indices to rewrite deltas with fast accumulated lookup.
///
/// B-Trees are generally more memory and cache efficient than binary trees,
/// because they store multiple keys/values in each node. This implements a
/// key/value mapping from index to delta, and allows fast lookup on index. An
/// added bonus is that it can also efficiently tell us the full accumulated
/// delta for a specific file offset without traversing the whole tree.
class DeltaTree {
  void *Root; // "DeltaTreeNode *"

public:
  /// Construct an empty delta tree.
  LLVM_ABI DeltaTree();

  /// Copy another tree. Currently only supported when \p RHS is empty.
  ///
  /// \param RHS Tree to copy; must currently be empty.
  LLVM_ABI DeltaTree(const DeltaTree &RHS);

  /// Assignment is deleted; DeltaTree is not copy-assignable.
  ///
  /// \param RHS Unused; this assignment is deleted.
  DeltaTree &operator=(const DeltaTree &RHS) = delete;
  /// Destroy the tree and free its nodes.
  LLVM_ABI ~DeltaTree();

  /// Return the accumulated delta at the specified file offset.
  /// This includes all insertions or delections that occurred *before* the
  /// specified file index.
  ///
  /// \param FileIndex File offset whose accumulated delta is requested.
  /// \return Accumulated rewrite delta at \p FileIndex.
  LLVM_ABI int getDeltaAt(unsigned FileIndex) const;

  /// Record a rewrite delta at the given file offset.
  ///
  /// When a change is made that shifts around the text buffer, this method is
  /// used to record that info.  It inserts a delta of 'Delta' into the current
  /// DeltaTree at offset FileIndex.
  ///
  /// \param FileIndex File offset at which to insert the delta.
  /// \param Delta Nonzero signed length of the insertion or deletion.
  LLVM_ABI void AddDelta(unsigned FileIndex, int Delta);
};

} // namespace llvm

#endif // LLVM_ADT_DELTATREE_H
