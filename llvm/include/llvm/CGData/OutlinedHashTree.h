//===- OutlinedHashTree.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
//
// This defines the OutlinedHashTree class. It contains sequences of stable
// hash values of instructions that have been outlined. This OutlinedHashTree
// can be used to track the outlined instruction sequences across modules.
//
//===---------------------------------------------------------------------===//

#ifndef LLVM_CGDATA_OUTLINEDHASHTREE_H
#define LLVM_CGDATA_OUTLINEDHASHTREE_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StableHashing.h"
#include "llvm/ObjectYAML/YAML.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

/// An entry in an OutlinedHashTree.
///
/// A HashNode holds a hash value and a collection of Successors (other
/// HashNodes). If a HashNode has a positive terminal value (Terminals > 0),
/// it signifies the end of a hash sequence with that occurrence count.
struct HashNode {
  /// The hash value of the node.
  stable_hash Hash = 0;
  /// The number of terminals in the sequence ending at this node.
  std::optional<unsigned> Terminals;
  /// The successors of this node.
  DenseMap<stable_hash, std::unique_ptr<HashNode>> Successors;
};

/// A trie of stable hash sequences for outlined instruction sequences.
class OutlinedHashTree {

  using EdgeCallbackFn =
      std::function<void(const HashNode *, const HashNode *)>;
  using NodeCallbackFn = std::function<void(const HashNode *)>;

  using HashSequence = SmallVector<stable_hash>;
  using HashSequencePair = std::pair<HashSequence, unsigned>;

public:
  /// Walk every edge and node in the outlined hash tree.
  ///
  /// Calls \p CallbackEdge for the edges and \p CallbackNode for the nodes
  /// with the stable_hash for the source and the stable_hash of the sink for
  /// an edge. These generic callbacks can be used to traverse an
  /// OutlinedHashTree for the purpose of print debugging or serializing it.
  ///
  /// \param CallbackNode Called for each node visited during the walk.
  /// \param CallbackEdge Called for each edge visited during the walk; may be
  /// null.
  /// \param SortedWalk When true, walk successors in a deterministic sorted
  /// order.
  LLVM_ABI void walkGraph(NodeCallbackFn CallbackNode,
                          EdgeCallbackFn CallbackEdge = nullptr,
                          bool SortedWalk = false) const;

  /// Release all hash nodes except the root hash node.
  void clear() {
    assert(getRoot()->Hash == 0 && !getRoot()->Terminals);
    getRoot()->Successors.clear();
  }

  /// Returns true if the hash tree has only the root node.
  ///
  /// \return True if the hash tree has only the root node.
  bool empty() { return size() == 1; }

  /// Returns the size of the outlined hash tree by traversing it.
  ///
  /// \param GetTerminalCountOnly When true, only count terminal nodes (the
  /// number of hash sequences in the OutlinedHashTree).
  /// \return The number of nodes, or the number of terminal sequences when
  /// \p GetTerminalCountOnly is true.
  LLVM_ABI size_t size(bool GetTerminalCountOnly = false) const;

  /// Returns the depth of the outlined hash tree by traversing it.
  ///
  /// \return The maximum depth of the outlined hash tree.
  LLVM_ABI size_t depth() const;

  /// Returns the root hash node of the outlined hash tree.
  ///
  /// \return Pointer to the root hash node.
  const HashNode *getRoot() const { return &Root; }
  /// Returns the root hash node of the outlined hash tree.
  ///
  /// \return Pointer to the root hash node.
  HashNode *getRoot() { return &Root; }

  /// Insert a hash sequence into this tree.
  ///
  /// The last node in the sequence will increase Terminals.
  ///
  /// \param SequencePair Pair of the hash sequence and its occurrence count.
  LLVM_ABI void insert(const HashSequencePair &SequencePair);

  /// Merge another outlined hash tree into this tree.
  ///
  /// \param OtherTree Tree whose sequences are merged into this one.
  LLVM_ABI void merge(const OutlinedHashTree *OtherTree);

  /// Returns the matching count if \p Sequence exists in the outlined hash
  /// tree.
  ///
  /// \param Sequence Hash sequence to look up.
  /// \return The occurrence count of \p Sequence, or std::nullopt if not found.
  LLVM_ABI std::optional<unsigned> find(const HashSequence &Sequence) const;

private:
  HashNode Root;
};

} // namespace llvm

#endif
