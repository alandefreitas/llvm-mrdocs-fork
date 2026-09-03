//===- llvm/ADT/SuffixTreeNode.h - Nodes for SuffixTrees --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines nodes for use within a SuffixTree.
//
// Each node has either no children or at least two children, with the root
// being a exception in the empty tree.
//
// Children are represented as a map between unsigned integers and nodes. If
// a node N has a child M on unsigned integer k, then the mapping represented
// by N is a proper prefix of the mapping represented by M. Note that this,
// although similar to a trie is somewhat different: each node stores a full
// substring of the full mapping rather than a single character state.
//
// Each internal node contains a pointer to the internal node representing
// the same string, but with the first character chopped off. This is stored
// in \p Link. Each leaf node stores the start index of its respective
// suffix in \p SuffixIdx.
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_SUFFIXTREE_NODE_H
#define LLVM_SUPPORT_SUFFIXTREE_NODE_H
#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

/// A node in a suffix tree which represents a substring or suffix.
struct SuffixTreeNode {
public:
  /// Represents an undefined index in the suffix tree.
  static const unsigned EmptyIdx = -1;
  /// Kind of suffix tree node for LLVM-style RTTI.
  enum class NodeKind {
    /// A leaf node that represents a suffix.
    ST_Leaf,
    /// An internal node with children, or the root.
    ST_Internal
  };

private:
  const NodeKind Kind;

  /// The start index of this node's substring in the main string.
  unsigned StartIdx = EmptyIdx;

  /// The length of the string formed by concatenating the edge labels from
  /// the root to this node.
  unsigned ConcatLen = 0;

  /// These two indices give a range of indices for its leaf descendants.
  /// Imagine drawing a tree on paper and assigning a unique index to each leaf
  /// node in monotonically increasing order from left to right. This way of
  /// numbering the leaf nodes allows us to associate a continuous range of
  /// indices with each internal node. For example, if a node has leaf
  /// descendants with indices i, i+1, ..., j, then its LeftLeafIdx is i and
  /// its RightLeafIdx is j. These indices are for LeafNodes in the SuffixTree
  /// class, which is constructed using post-order depth-first traversal.
  unsigned LeftLeafIdx = EmptyIdx;
  unsigned RightLeafIdx = EmptyIdx;

public:
  /// Return the kind of this node.
  ///
  /// \returns The kind of this node.
  NodeKind getKind() const { return Kind; }

  /// Return the start index of this node's substring in the entire string.
  ///
  /// \returns The start index of this node's substring.
  LLVM_ABI unsigned getStartIdx() const;

  /// Return the end index of this node.
  ///
  /// \returns The end index of this node's substring.
  virtual unsigned getEndIdx() const = 0;

  /// Return the index of this node's left most leaf node.
  ///
  /// \returns The left-most leaf index.
  LLVM_ABI unsigned getLeftLeafIdx() const;

  /// Return the index of this node's right most leaf node.
  ///
  /// \returns The right-most leaf index.
  LLVM_ABI unsigned getRightLeafIdx() const;

  /// Set the index of the left most leaf node of this node to \p Idx.
  ///
  /// \param Idx The left-most leaf index.
  LLVM_ABI void setLeftLeafIdx(unsigned Idx);

  /// Set the index of the right most leaf node of this node to \p Idx.
  ///
  /// \param Idx The right-most leaf index.
  LLVM_ABI void setRightLeafIdx(unsigned Idx);

  /// Advance this node's StartIdx by \p Inc.
  ///
  /// \param Inc The amount to add to the start index.
  LLVM_ABI void incrementStartIdx(unsigned Inc);

  /// Set the length of the string from the root to this node to \p Len.
  ///
  /// \param Len The concatenated length from the root to this node.
  LLVM_ABI void setConcatLen(unsigned Len);

  /// Return the length of the string from the root to this node.
  ///
  /// \returns The concatenated length from the root to this node.
  LLVM_ABI unsigned getConcatLen() const;

  /// Construct a suffix tree node of the given kind and start index.
  ///
  /// \param Kind The kind of node to create.
  /// \param StartIdx The start index of this node's substring.
  SuffixTreeNode(NodeKind Kind, unsigned StartIdx)
      : Kind(Kind), StartIdx(StartIdx) {}
  /// Destroy a suffix tree node.
  virtual ~SuffixTreeNode() = default;
};

/// A node with two or more children, or the root.
struct LLVM_ABI SuffixTreeInternalNode : SuffixTreeNode {
private:
  /// The end index of this node's substring in the main string.
  ///
  /// Every leaf node must have its \p EndIdx incremented at the end of every
  /// step in the construction algorithm. To avoid having to update O(N)
  /// nodes individually at the end of every step, the end index is stored
  /// as a pointer.
  unsigned EndIdx = EmptyIdx;

  /// A pointer to the internal node representing the same sequence with the
  /// first character chopped off.
  ///
  /// This acts as a shortcut in Ukkonen's algorithm. One of the things that
  /// Ukkonen's algorithm does to achieve linear-time construction is
  /// keep track of which node the next insert should be at. This makes each
  /// insert O(1), and there are a total of O(N) inserts. The suffix link
  /// helps with inserting children of internal nodes.
  ///
  /// Say we add a child to an internal node with associated mapping S. The
  /// next insertion must be at the node representing S - its first character.
  /// This is given by the way that we iteratively build the tree in Ukkonen's
  /// algorithm. The main idea is to look at the suffixes of each prefix in the
  /// string, starting with the longest suffix of the prefix, and ending with
  /// the shortest. Therefore, if we keep pointers between such nodes, we can
  /// move to the next insertion point in O(1) time. If we don't, then we'd
  /// have to query from the root, which takes O(N) time. This would make the
  /// construction algorithm O(N^2) rather than O(N).
  SuffixTreeInternalNode *Link = nullptr;

public:
  /// Check whether \p N is a \c SuffixTreeInternalNode.
  ///
  /// \param N The node to inspect.
  ///
  /// \returns true if \p N is an internal node.
  static bool classof(const SuffixTreeNode *N) {
    return N->getKind() == NodeKind::ST_Internal;
  }

  /// Return true if this node is the root of its owning \p SuffixTree.
  ///
  /// \returns True if this node is the root.
  bool isRoot() const;

  /// Return the end index of this node's substring in the entire string.
  ///
  /// \returns The end index of this node's substring.
  unsigned getEndIdx() const override;

  /// Sets \p Link to \p L. Assumes \p L is not null.
  ///
  /// \param L The suffix link to set.
  void setLink(SuffixTreeInternalNode *L);

  /// Return the pointer to the Link node.
  ///
  /// \returns The suffix link, or nullptr if unset.
  SuffixTreeInternalNode *getLink() const;

  /// The children of this node.
  ///
  /// A child existing on an unsigned integer implies that from the mapping
  /// represented by the current node, there is a way to reach another
  /// mapping by tacking that character on the end of the current string.
  DenseMap<unsigned, SuffixTreeNode *> Children;

  /// Construct an internal node with the given bounds and suffix link.
  ///
  /// \param StartIdx The start index of this node's substring.
  /// \param EndIdx The end index of this node's substring.
  /// \param Link The suffix link for this node, or null.
  SuffixTreeInternalNode(unsigned StartIdx, unsigned EndIdx,
                         SuffixTreeInternalNode *Link)
      : SuffixTreeNode(NodeKind::ST_Internal, StartIdx), EndIdx(EndIdx),
        Link(Link) {}

  /// Destroy an internal node.
  ~SuffixTreeInternalNode() override = default;
};

/// A node representing a suffix.
struct LLVM_ABI SuffixTreeLeafNode : SuffixTreeNode {
private:
  /// The start index of the suffix represented by this leaf.
  unsigned SuffixIdx = EmptyIdx;

  /// The end index of this node's substring in the main string.
  ///
  /// Every leaf node must have its \p EndIdx incremented at the end of every
  /// step in the construction algorithm. To avoid having to update O(N)
  /// nodes individually at the end of every step, the end index is stored
  /// as a pointer.
  unsigned *EndIdx = nullptr;

public:
  /// Check whether \p N is a \c SuffixTreeLeafNode.
  ///
  /// \param N The node to inspect.
  ///
  /// \returns true if \p N is a leaf node.
  static bool classof(const SuffixTreeNode *N) {
    return N->getKind() == NodeKind::ST_Leaf;
  }

  /// Return the end index of this node's substring in the entire string.
  ///
  /// \returns The end index of this node's substring.
  unsigned getEndIdx() const override;

  /// Return the start index of the suffix represented by this leaf.
  ///
  /// \returns The start index of the suffix.
  unsigned getSuffixIdx() const;

  /// Sets the start index of the suffix represented by this leaf to \p Idx.
  ///
  /// \param Idx The start index of the suffix.
  void setSuffixIdx(unsigned Idx);
  /// Construct a leaf node with the given start index and shared end index.
  ///
  /// \param StartIdx The start index of this node's substring.
  /// \param EndIdx Pointer to the shared end index for leaf nodes.
  SuffixTreeLeafNode(unsigned StartIdx, unsigned *EndIdx)
      : SuffixTreeNode(NodeKind::ST_Leaf, StartIdx), EndIdx(EndIdx) {}

  /// Destroy a leaf node.
  ~SuffixTreeLeafNode() override = default;
};
} // namespace llvm
#endif // LLVM_SUPPORT_SUFFIXTREE_NODE_H
