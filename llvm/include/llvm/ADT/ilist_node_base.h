//===- llvm/ADT/ilist_node_base.h - Intrusive List Node Base -----*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_ILIST_NODE_BASE_H
#define LLVM_ADT_ILIST_NODE_BASE_H

#include "llvm/ADT/PointerIntPair.h"

namespace llvm {

/// Implementation details for intrusive list node prev/next storage.
namespace ilist_detail {

/// Prev/next link helper; specialized on whether sentinel tracking is enabled.
template <class NodeBase, bool EnableSentinelTracking> class node_base_prevnext;

/// Prev/next storage without sentinel tracking.
template <class NodeBase> class node_base_prevnext<NodeBase, false> {
  NodeBase *Prev = nullptr;
  NodeBase *Next = nullptr;

public:
  /// Set the previous-node pointer.
  /// @param Prev Node that should precede this one.
  void setPrev(NodeBase *Prev) { this->Prev = Prev; }
  /// Set the next-node pointer.
  /// @param Next Node that should follow this one.
  void setNext(NodeBase *Next) { this->Next = Next; }
  /// Return the previous-node pointer.
  /// @return Pointer to the previous node, or \c nullptr if unset.
  NodeBase *getPrev() const { return Prev; }
  /// Return the next-node pointer.
  /// @return Pointer to the next node, or \c nullptr if unset.
  NodeBase *getNext() const { return Next; }

  /// Return false; sentinel tracking is disabled in this specialization.
  /// @return Always \c false in this specialization.
  bool isKnownSentinel() const { return false; }
  /// No-op; sentinel tracking is disabled in this specialization.
  void initializeSentinel() {}
};

/// Prev/next storage that tracks whether the node is a sentinel.
template <class NodeBase> class node_base_prevnext<NodeBase, true> {
  PointerIntPair<NodeBase *, 1> PrevAndSentinel;
  NodeBase *Next = nullptr;

public:
  /// Set the previous-node pointer.
  /// @param Prev Node that should precede this one.
  void setPrev(NodeBase *Prev) { PrevAndSentinel.setPointer(Prev); }
  /// Set the next-node pointer.
  /// @param Next Node that should follow this one.
  void setNext(NodeBase *Next) { this->Next = Next; }
  /// Return the previous-node pointer.
  /// @return Pointer to the previous node, or \c nullptr if unset.
  NodeBase *getPrev() const { return PrevAndSentinel.getPointer(); }
  /// Return the next-node pointer.
  /// @return Pointer to the next node, or \c nullptr if unset.
  NodeBase *getNext() const { return Next; }

  /// Return whether this node is marked as a sentinel.
  /// @return \c true if this node is marked as a sentinel.
  bool isSentinel() const { return PrevAndSentinel.getInt(); }
  /// Return whether this node is known to be a sentinel.
  /// @return \c true if this node is known to be a sentinel.
  bool isKnownSentinel() const { return isSentinel(); }
  /// Mark this node as a sentinel.
  void initializeSentinel() { PrevAndSentinel.setInt(true); }
};

/// Storage for a pointer to the owning parent object of an ilist node.
template <class ParentTy> class node_base_parent {
  ParentTy *Parent = nullptr;

public:
  /// Record \p Parent as the owner of this node.
  /// @param Parent Pointer to the owning parent object.
  void setNodeBaseParent(ParentTy *Parent) { this->Parent = Parent; }
  /// Return the const parent pointer recorded on this node.
  /// @return Const pointer to the owning parent object.
  inline const ParentTy *getNodeBaseParent() const { return Parent; }
  /// Return the parent pointer recorded on this node.
  /// @return Mutable pointer to the owning parent object.
  inline ParentTy *getNodeBaseParent() { return Parent; }
};
/// Empty specialization when the node does not store a parent pointer.
template <> class node_base_parent<void> {};

} // end namespace ilist_detail

/// Base class for ilist nodes.
///
/// Optionally tracks whether this node is the sentinel.
template <bool EnableSentinelTracking, class ParentTy>
class ilist_node_base : public ilist_detail::node_base_prevnext<
                            ilist_node_base<EnableSentinelTracking, ParentTy>,
                            EnableSentinelTracking>,
                        public ilist_detail::node_base_parent<ParentTy> {};

/// Explicitly instantiated sentinel-tracking ilist node base with no parent.
///
/// Implemented in the core LLVM library for stable ABI export.
template class LLVM_ABI ilist_node_base<true, void>;

} // end namespace llvm

#endif // LLVM_ADT_ILIST_NODE_BASE_H
