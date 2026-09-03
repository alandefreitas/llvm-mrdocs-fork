//===- llvm/ADT/PostOrderIterator.h - PostOrder iterator --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file builds on the ADT/GraphTraits.h file to build a generic graph
/// post order iterator.  This should work over any graph type that has a
/// GraphTraits specialization.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_POSTORDERITERATOR_H
#define LLVM_ADT_POSTORDERITERATOR_H

#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include <iterator>
#include <optional>
#include <type_traits>
#include <utility>

namespace llvm {
/// Implementation details for post-order graph iterators.
namespace po_detail {

/// Dense visited set keyed by GraphTraits node numbers.
template <typename NodeRef> class NumberSet {
  SmallVector<bool> Data;

public:
  /// Ensure capacity for nodes with numbers less than \p Size.
  ///
  /// \param Size Exclusive upper bound on node numbers that may be inserted.
  void reserve(size_t Size) {
    if (Size < Data.size())
      Data.resize(Size, false);
  }

  /// Mark \p Node visited; second is true if it was not already present.
  ///
  /// \param Node Graph node identified by GraphTraits::getNumber.
  /// @return Pair of nullopt and whether \p Node was newly marked visited.
  std::pair<std::nullopt_t, bool> insert(NodeRef Node) {
    unsigned Idx = GraphTraits<NodeRef>::getNumber(Node);
    if (Idx >= Data.size())
      Data.resize(Idx + 1);
    bool Inserted = !Data[Idx];
    Data[Idx] = true;
    return {std::nullopt, Inserted};
  }
};

/// Default visited-set type for post-order traversal of \c GraphT.
template <typename GraphT>
using DefaultSet =
    std::conditional_t<GraphHasNodeNumbers<GraphT>,
                       NumberSet<typename GraphTraits<GraphT>::NodeRef>,
                       SmallPtrSet<typename GraphTraits<GraphT>::NodeRef, 8>>;

} // namespace po_detail

/// CRTP base for a single post-order graph walk.
///
/// Subclasses provide visited storage via insertEdge(). Call init() with the
/// start node before iterating. finishPostorder() observes each node just
/// before the iterator advances; insertEdge() can skip unwanted nodes by
/// returning false.
template <typename DerivedT, typename GraphTraits>
class PostOrderTraversalBase {
  using NodeRef = typename GraphTraits::NodeRef;
  using ChildItTy = typename GraphTraits::ChildIteratorType;

  struct StackEntry {
    NodeRef Node;  ///< Node.
    ChildItTy It;  ///< Iterator for next child.
    ChildItTy End; ///< End iterator for children.

    // This constructor is carefully designed so that there is no store between
    // the calls to child_begin() and child_end(). LLVM IR successors() is a
    // pure function called for begin and end, but the second call can't be
    // removed if there's a potentially visible store (here: store to the
    // member It in the VisitStack) in between.
    StackEntry(NodeRef Node, iterator_range<ChildItTy> Children)
        : Node(Node), It(Children.begin()), End(Children.end()) {}
  };
  SmallVector<StackEntry, 8> VisitStack;

public:
  /// Input iterator yielding nodes in post-order during a single traversal.
  class iterator {
    friend class PostOrderTraversalBase;

  public:
    /// Category identifying this as an input iterator.
    using iterator_category = std::input_iterator_tag;
    /// Node reference type yielded by the iterator.
    using value_type = NodeRef;
    /// Signed distance type required by the iterator concept.
    using difference_type = std::ptrdiff_t;
    /// Pointer type for operator->.
    using pointer = value_type *;
    /// Reference type for operator*.
    using reference = NodeRef;

  private:
    DerivedT *POT = nullptr;
    NodeRef V = nullptr;

  public:
    /// Construct an end/singular iterator.
    iterator() = default;

  private:
    iterator(DerivedT &POT, value_type V) : POT(&POT), V(V) {}

  public:
    /// Return true if both iterators refer to the same node.
    ///
    /// \param X Iterator to compare with.
    /// @return True if both iterators refer to the same node.
    bool operator==(const iterator &X) const { return V == X.V; }
    /// Return true if the iterators refer to different nodes.
    ///
    /// \param X Iterator to compare with.
    /// @return True if the iterators refer to different nodes.
    bool operator!=(const iterator &X) const { return !(*this == X); }

    /// Return the current node.
    /// @return Reference to the current node.
    reference operator*() const { return V; }
    /// Return a pointer to the current node storage.
    /// @return Pointer to the current node storage.
    pointer operator->() const { return &V; }

    /// Advance to the next node in post-order.
    /// @return Reference to this iterator after advancing.
    iterator &operator++() { // Preincrement
      V = POT->next();
      return *this;
    }

    /// Advance to the next node, returning the prior iterator value.
    ///
    /// \param Unused Unused postfix-discriminator parameter.
    /// @return Copy of the iterator before advancing.
    iterator operator++(int Unused) { // Postincrement
      iterator tmp = *this;
      ++*this;
      return tmp;
    }
  };

protected:
  /// Construct an uninitialized traversal base.
  PostOrderTraversalBase() = default;

  /// Return this object cast to the CRTP derived type.
  /// @return Pointer to this object as the derived CRTP type.
  DerivedT *derived() { return static_cast<DerivedT *>(this); }

  /// Initialize post-order traversal at given start node.
  ///
  /// \param Start Root node where the walk begins.
  void init(NodeRef Start) {
    if (derived()->insertEdge(std::optional<NodeRef>(), Start)) {
      VisitStack.emplace_back(Start, make_range(GraphTraits::child_begin(Start),
                                                GraphTraits::child_end(Start)));
      traverseChild();
    }
  }

private:
  void traverseChild() {
    while (true) {
      auto &Entry = VisitStack.back();
      if (Entry.It == Entry.End)
        break;
      NodeRef BB = *Entry.It++;
      // If the block is not visited...
      if (derived()->insertEdge(std::optional<NodeRef>(Entry.Node), BB))
        VisitStack.emplace_back(BB, make_range(GraphTraits::child_begin(BB),
                                               GraphTraits::child_end(BB)));
    }
  }

  NodeRef next() {
    derived()->finishPostorder(VisitStack.back().Node);
    VisitStack.pop_back();
    if (VisitStack.empty())
      return nullptr;
    traverseChild();
    return VisitStack.back().Node;
  }

public:
  /// Iterator to the current post-order node, or end if none remain.
  /// @return Iterator to the current node, or end if the stack is empty.
  iterator begin() {
    if (VisitStack.empty())
      return iterator(); // We don't even want to see the start node.
    return iterator(*derived(), VisitStack.back().Node);
  }
  /// Past-the-end iterator for this traversal.
  /// @return Singular end iterator for this traversal.
  iterator end() { return iterator(); }

  // Methods that are intended to be overridden by sub-classes.

  /// Add edge and return whether To should be visited. From is nullopt for the
  /// root node.
  ///
  /// \param From Predecessor node, or nullopt for the traversal root.
  /// \param To Successor being considered.
  /// @return True if \p To should be visited.
  bool insertEdge(std::optional<NodeRef> From, NodeRef To);

  /// Callback just before the iterator moves to the next block.
  ///
  /// \param Node Node that has finished post-order visitation.
  void finishPostorder(NodeRef Node) {}
};

/// Post-order traversal of a graph.
///
/// Traversal state lives in this object, not in the iterators, so its lifetime
/// must outlive the iterators. Prefer:
/// \code
///   for (BasicBlock *BB : post_order(F)) { ... }
/// \endcode
/// Avoid binding a temporary traversal into \c make_filter_range; store the
/// traversal object first. Only a single traversal is supported.
template <typename GraphT, typename SetType = po_detail::DefaultSet<GraphT>>
class PostOrderTraversal
    : public PostOrderTraversalBase<PostOrderTraversal<GraphT, SetType>,
                                    GraphTraits<GraphT>> {
  using NodeRef = typename GraphTraits<GraphT>::NodeRef;

  SetType Visited;

public:
  /// Default constructor for an empty traversal.
  PostOrderTraversal() = default;

  /// Post-order traversal of the graph starting at the root node using an
  /// internal storage.
  ///
  /// \param G Graph whose entry node starts the walk.
  PostOrderTraversal(const GraphT &G) {
    this->init(GraphTraits<GraphT>::getEntryNode(G));
  }

  /// Record edge \p From -> \p To and return true if \p To should be visited.
  ///
  /// \param From Predecessor node, or nullopt for the traversal root.
  /// \param To Successor being considered.
  /// @return True if \p To was newly inserted and should be visited.
  bool insertEdge(std::optional<NodeRef> From, NodeRef To) {
    return Visited.insert(To).second;
  }
};

/// Post-order traversal using an external visited set.
///
/// The set can retain visited nodes after the walk and skip nodes already
/// present. See PostOrderTraversal for lifetime restrictions.
template <typename GraphT, typename SetType>
class PostOrderExtTraversal
    : public PostOrderTraversalBase<PostOrderExtTraversal<GraphT, SetType>,
                                    GraphTraits<GraphT>> {
  using NodeRef = typename GraphTraits<GraphT>::NodeRef;

  SetType &Visited;

public:
  /// Traverse \p G while recording visited nodes in external set \p S.
  ///
  /// \param G Graph whose entry node starts the walk.
  /// \param S Visited-set shared with the caller.
  PostOrderExtTraversal(const GraphT &G, SetType &S) : Visited(S) {
    this->init(GraphTraits<GraphT>::getEntryNode(G));
  }

  /// Record edge \p From -> \p To and return true if \p To should be visited.
  ///
  /// \param From Predecessor node, or nullopt for the traversal root.
  /// \param To Successor being considered.
  /// @return True if \p To was newly inserted and should be visited.
  bool insertEdge(std::optional<NodeRef> From, NodeRef To) {
    return Visited.insert(To).second;
  }
};

// Provide global constructors that automatically figure out correct types...
//
/// Post-order traversal of a graph. Note: this returns a PostOrderTraversal,
/// not an iterator range; \see PostOrderTraversal.
///
/// \param G Graph whose entry node starts the walk.
/// @return Post-order traversal object starting at the entry of \p G.
template <class T> auto post_order(const T &G) {
  return PostOrderTraversal<T>(G);
}
/// Post-order traversal of \p G that records visited nodes in external set \p S.
///
/// \param G Graph to traverse.
/// \param S Visited-set provided by the caller.
/// @return Post-order traversal that records visits in \p S.
template <class T, class SetType> auto post_order_ext(const T &G, SetType &S) {
  return PostOrderExtTraversal<T, SetType>(G, S);
}
/// Post-order traversal of the inverse of \p G using external visited set \p S.
///
/// \param G Graph whose inverse edges are walked.
/// \param S Visited-set provided by the caller.
/// @return Post-order traversal of the inverse of \p G using \p S.
template <class T, class SetType>
auto inverse_post_order_ext(const T &G, SetType &S) {
  return PostOrderExtTraversal<Inverse<T>, SetType>(G, S);
}

//===--------------------------------------------------------------------===//
// Reverse Post Order CFG iterator code
//===--------------------------------------------------------------------===//
//
// This is used to visit basic blocks in a method in reverse post order.  This
// class is awkward to use because I don't know a good incremental algorithm to
// computer RPO from a graph.  Because of this, the construction of the
// ReversePostOrderTraversal object is expensive (it must walk the entire graph
// with a postorder iterator to build the data structures).  The moral of this
// story is: Don't create more ReversePostOrderTraversal classes than necessary.
//
// Because it does the traversal in its constructor, it won't invalidate when
// BasicBlocks are removed, *but* it may contain erased blocks. Some places
// rely on this behavior (i.e. GVN).
//
// This class should be used like this:
// {
//   ReversePostOrderTraversal<Function*> RPOT(FuncPtr); // Expensive to create
//   for (rpo_iterator I = RPOT.begin(); I != RPOT.end(); ++I) {
//      ...
//   }
//   for (rpo_iterator I = RPOT.begin(); I != RPOT.end(); ++I) {
//      ...
//   }
// }
//

/// Precomputed reverse-post-order walk of a graph.
///
/// Construction walks the entire graph once with a post-order iterator and
/// stores the nodes so later iteration is cheap. Prefer reusing one instance
/// rather than creating many.
template<class GraphT, class GT = GraphTraits<GraphT>>
class ReversePostOrderTraversal {
  using NodeRef = typename GT::NodeRef;

  using VecTy = SmallVector<NodeRef, 8>;
  VecTy Blocks; // Block list in normal PO order

  void Initialize(const GraphT &G) {
    llvm::copy(post_order(G), std::back_inserter(Blocks));
  }

public:
  /// Mutable reverse iterator yielding nodes in reverse post-order.
  using rpo_iterator = typename VecTy::reverse_iterator;
  /// Const reverse iterator yielding nodes in reverse post-order.
  using const_rpo_iterator = typename VecTy::const_reverse_iterator;

  /// Build a reverse-post-order listing of all nodes reachable from \p G.
  ///
  /// \param G Graph whose entry node starts the post-order walk.
  ReversePostOrderTraversal(const GraphT &G) { Initialize(G); }

  // Because we want a reverse post order, use reverse iterators from the vector
  /// Iterator to the first node in reverse post-order.
  /// @return Mutable reverse iterator at the first node.
  rpo_iterator begin() { return Blocks.rbegin(); }
  /// Const iterator to the first node in reverse post-order.
  /// @return Const reverse iterator at the first node.
  const_rpo_iterator begin() const { return Blocks.rbegin(); }
  /// Past-the-end reverse-post-order iterator.
  /// @return Mutable past-the-end reverse iterator.
  rpo_iterator end() { return Blocks.rend(); }
  /// Const past-the-end reverse-post-order iterator.
  /// @return Const past-the-end reverse iterator.
  const_rpo_iterator end() const { return Blocks.rend(); }
};

} // end namespace llvm

#endif // LLVM_ADT_POSTORDERITERATOR_H
