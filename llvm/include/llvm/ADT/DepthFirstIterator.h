//===- llvm/ADT/DepthFirstIterator.h - Depth First iterator -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file builds on the ADT/GraphTraits.h file to build generic depth
/// first graph iterator.  This file exposes the following functions/types:
///
/// df_begin/df_end/df_iterator
///   * Normal depth-first iteration - visit a node and then all of its
///     children.
///
/// idf_begin/idf_end/idf_iterator
///   * Depth-first iteration on the 'inverse' graph.
///
/// df_ext_begin/df_ext_end/df_ext_iterator
///   * Normal depth-first iteration - visit a node and then all of its
///     children. This iterator stores the 'visited' set in an external set,
///     which allows it to be more efficient, and allows external clients to
///     use the set for other purposes.
///
/// idf_ext_begin/idf_ext_end/idf_ext_iterator
///   * Depth-first iteration on the 'inverse' graph.
///     This iterator stores the 'visited' set in an external set, which
///     allows it to be more efficient, and allows external clients to use
///     the set for other purposes.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_DEPTHFIRSTITERATOR_H
#define LLVM_ADT_DEPTHFIRSTITERATOR_H

#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/iterator_range.h"
#include <iterator>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace llvm {

/// Storage for the visited-node set used by depth-first iterators.
///
/// When \p External is false, the set is owned by the iterator. The
/// specialization for \p External true holds a reference to a caller-owned set.
template <class SetType, bool External> // Non-external set
class df_iterator_storage {
public:
  /// Set of nodes already visited during depth-first traversal.
  SetType Visited;
};

/// Depth-first iterator storage that references an external visited set.
template <class SetType> class df_iterator_storage<SetType, true> {
public:
  /// Construct storage that references the caller-owned visited set \p VSet.
  /// @param VSet External set of visited nodes.
  df_iterator_storage(SetType &VSet) : Visited(VSet) {}

  /// Copy construct, sharing the same external visited set reference.
  /// @param S Storage whose visited-set reference is reused.
  df_iterator_storage(const df_iterator_storage &S) : Visited(S.Visited) {}

  /// Reference to the caller-owned set of nodes already visited.
  SetType &Visited;
};

/// Default visited-node set for depth-first iteration.
///
/// The visited state for the iteration is a simple set augmented with one more
/// method, completed, which is invoked when all children of a node have been
/// processed. It is intended to distinguish back and cross edges in the
/// spanning tree but is not used in the common case.
template <typename NodeRef, unsigned SmallSize = 8>
struct df_iterator_default_set : SmallPtrSet<NodeRef, SmallSize> {
  /// Underlying SmallPtrSet used to track visited nodes.
  using BaseSet = SmallPtrSet<NodeRef, SmallSize>;
  /// Iterator over elements in the visited set.
  using iterator = typename BaseSet::iterator;

  /// Insert \p N into the visited set.
  /// @param N Node to mark visited.
  /// @return Pair of iterator to the element and whether it was newly inserted.
  std::pair<iterator, bool> insert(NodeRef N) { return BaseSet::insert(N); }

  /// Insert every node in the half-open range [\p Begin, \p End).
  /// @param Begin Start of the node range.
  /// @param End Past-the-end of the node range.
  template <typename IterT>
  void insert(IterT Begin, IterT End) {
    BaseSet::insert(Begin, End);
  }

  /// Called when depth-first descent finishes all children of a node.
  ///
  /// The default implementation is a no-op; custom sets may override to
  /// observe finishing edges in the DFS spanning tree.
  /// @param Node Node whose children have all been processed.
  void completed(NodeRef Node) { (void)Node; }
};

/// Generic depth-first search iterator over a graph.
///
/// Visits each reachable node once, descending into children before siblings.
/// When \p ExtStorage is true, the visited set is provided externally.
template <class GraphT,
          class SetType =
              df_iterator_default_set<typename GraphTraits<GraphT>::NodeRef>,
          bool ExtStorage = false, class GT = GraphTraits<GraphT>>
class df_iterator : public df_iterator_storage<SetType, ExtStorage> {
public:
  // When External storage is used we are not multi-pass safe.
  /// Iterator category: input when using external storage, otherwise forward.
  using iterator_category =
      std::conditional_t<ExtStorage, std::input_iterator_tag,
                         std::forward_iterator_tag>;
  /// Node reference type yielded by this iterator.
  using value_type = typename GT::NodeRef;
  /// Distance between two depth-first iterators.
  using difference_type = std::ptrdiff_t;
  /// Pointer to a node reference.
  using pointer = value_type *;
  /// Const reference to the current node.
  using reference = const value_type &;

private:
  using NodeRef = typename GT::NodeRef;
  using ChildItTy = typename GT::ChildIteratorType;

  // First element is node reference, second is the 'next child' to visit.
  // The second child is initialized lazily to pick up graph changes during the
  // DFS.
  using StackElement = std::pair<NodeRef, std::optional<ChildItTy>>;

  // VisitStack - Used to maintain the ordering.  Top = current block
  std::vector<StackElement> VisitStack;

  inline df_iterator(NodeRef Node) {
    this->Visited.insert(Node);
    VisitStack.push_back(StackElement(Node, std::nullopt));
  }

  inline df_iterator() = default; // End is when stack is empty

  inline df_iterator(NodeRef Node, SetType &S)
      : df_iterator_storage<SetType, ExtStorage>(S) {
    if (this->Visited.insert(Node).second)
      VisitStack.push_back(StackElement(Node, std::nullopt));
  }

  inline df_iterator(SetType &S)
      : df_iterator_storage<SetType, ExtStorage>(S) {
    // End is when stack is empty
  }

  inline void toNext() {
    do {
      NodeRef Node = VisitStack.back().first;
      std::optional<ChildItTy> &Opt = VisitStack.back().second;

      if (!Opt)
        Opt.emplace(GT::child_begin(Node));

      // Notice that we directly mutate *Opt here, so that
      // VisitStack.back().second actually gets updated as the iterator
      // increases.
      while (*Opt != GT::child_end(Node)) {
        NodeRef Next = *(*Opt)++;
        // Has our next sibling been visited?
        if (this->Visited.insert(Next).second) {
          // No, do it now.
          VisitStack.push_back(StackElement(Next, std::nullopt));
          return;
        }
      }
      this->Visited.completed(Node);

      // Oops, ran out of successors... go up a level on the stack.
      VisitStack.pop_back();
    } while (!VisitStack.empty());
  }

public:
  /// Construct a depth-first iterator at the entry node of \p G.
  /// @param G Graph to traverse.
  /// @return Depth-first iterator positioned at the entry node.
  static df_iterator begin(const GraphT &G) {
    return df_iterator(GT::getEntryNode(G));
  }

  /// Construct a past-the-end depth-first iterator for \p G.
  /// @param G Graph being traversed (unused; end is empty).
  /// @return Past-the-end depth-first iterator.
  static df_iterator end(const GraphT &G) { return df_iterator(); }

  /// Construct a depth-first iterator at the entry of \p G using external set
  /// \p S.
  /// @param G Graph to traverse.
  /// @param S Caller-owned visited set shared with this iterator.
  /// @return Depth-first iterator positioned at the entry node.
  static df_iterator begin(const GraphT &G, SetType &S) {
    return df_iterator(GT::getEntryNode(G), S);
  }

  /// Construct a past-the-end depth-first iterator bound to external set \p S.
  /// @param G Graph being traversed (unused; end is empty).
  /// @param S Caller-owned visited set shared with this iterator.
  /// @return Past-the-end depth-first iterator.
  static df_iterator end(const GraphT &G, SetType &S) { return df_iterator(S); }

  /// Return true if both iterators have the same visit-stack state.
  /// @param x Iterator to compare with.
  /// @return True if both iterators have the same visit-stack state.
  bool operator==(const df_iterator &x) const {
    return VisitStack == x.VisitStack;
  }

  /// Return true if the iterators differ in visit-stack state.
  /// @param x Iterator to compare with.
  /// @return True if the iterators differ in visit-stack state.
  bool operator!=(const df_iterator &x) const { return !(*this == x); }

  /// Return a reference to the current node.
  /// @return Const reference to the current node.
  reference operator*() const { return VisitStack.back().first; }

  /// Return the current node so methods can be called through the iterator.
  ///
  /// This is a nonstandard operator-> that dereferences the pointer an extra
  /// time so that you can actually call methods on the node, because the
  /// contained type is a pointer. This allows expressions like
  /// `BBIt->getTerminator()`.
  /// @return The current node reference.
  NodeRef operator->() const { return **this; }

  /// Advance to the next node in depth-first order and return this iterator.
  /// @return Reference to this iterator after advancing.
  df_iterator &operator++() { // Preincrement
    toNext();
    return *this;
  }

  /// Skips all children of the current node and traverses to next node
  ///
  /// Note: This function takes care of incrementing the iterator. If you
  /// always increment and call this function, you risk walking off the end.
  /// @return Reference to this iterator after skipping children.
  df_iterator &skipChildren() {
    VisitStack.pop_back();
    if (!VisitStack.empty())
      toNext();
    return *this;
  }

  /// Advance to the next node and return the prior iterator position.
  /// @param Unused Unused postfix-discriminator parameter.
  /// @return Copy of the iterator before advancing.
  df_iterator operator++(int Unused) { // Postincrement
    df_iterator tmp = *this;
    ++*this;
    return tmp;
  }

  /// Return true if this iterator has already visited \p Node.
  ///
  /// Useful for finding nodes that a depth-first walk did not reach (for
  /// example unreachable nodes).
  /// @param Node Node to query.
  /// @return True if \p Node has already been visited.
  bool nodeVisited(NodeRef Node) const {
    return this->Visited.contains(Node);
  }

  /// Return the length of the path from the entry node to the current node,
  /// counting both nodes.
  /// @return Number of nodes on the path from entry to the current node.
  unsigned getPathLength() const { return VisitStack.size(); }

  /// Return the n'th node in the path from the entry node to the current node.
  /// @param n Zero-based index along the path (0 is the entry node).
  /// @return The node at path index \p n.
  NodeRef getPath(unsigned n) const { return VisitStack[n].first; }
};

/// Return a depth-first iterator at the entry of \p G.
/// @param G Graph to traverse.
/// @return Depth-first iterator positioned at the entry of \p G.
template <class T>
df_iterator<T> df_begin(const T &G) {
  return df_iterator<T>::begin(G);
}

/// Return a past-the-end depth-first iterator for \p G.
/// @param G Graph being traversed.
/// @return Past-the-end depth-first iterator for \p G.
template <class T>
df_iterator<T> df_end(const T &G) {
  return df_iterator<T>::end(G);
}

/// Return a range that visits \p G in depth-first order.
/// @param G Graph to traverse.
/// @return Iterator range covering a depth-first walk of \p G.
template <class T>
iterator_range<df_iterator<T>> depth_first(const T &G) {
  return make_range(df_begin(G), df_end(G));
}

/// Depth-first iterator that stores its visited set externally.
template <class T,
          class SetTy =
              df_iterator_default_set<typename GraphTraits<T>::NodeRef>>
struct df_ext_iterator : df_iterator<T, SetTy, true> {
  /// Construct from an underlying external-storage depth-first iterator.
  /// @param V Iterator state to wrap.
  df_ext_iterator(const df_iterator<T, SetTy, true> &V)
      : df_iterator<T, SetTy, true>(V) {}
};

/// Return an external-storage depth-first iterator at the entry of \p G.
/// @param G Graph to traverse.
/// @param S Caller-owned visited set.
/// @return External-storage depth-first iterator at the entry of \p G.
template <class T, class SetTy>
df_ext_iterator<T, SetTy> df_ext_begin(const T &G, SetTy &S) {
  return df_ext_iterator<T, SetTy>::begin(G, S);
}

/// Return a past-the-end external-storage depth-first iterator for \p G.
/// @param G Graph being traversed.
/// @param S Caller-owned visited set.
/// @return Past-the-end external-storage depth-first iterator for \p G.
template <class T, class SetTy>
df_ext_iterator<T, SetTy> df_ext_end(const T &G, SetTy &S) {
  return df_ext_iterator<T, SetTy>::end(G, S);
}

/// Return a range that visits \p G in depth-first order using visited set \p S.
/// @param G Graph to traverse.
/// @param S Caller-owned visited set.
/// @return Iterator range covering a depth-first walk of \p G using \p S.
template <class T, class SetTy>
iterator_range<df_ext_iterator<T, SetTy>> depth_first_ext(const T &G,
                                                          SetTy &S) {
  return make_range(df_ext_begin(G, S), df_ext_end(G, S));
}

/// Depth-first iterator over the inverse of graph \p T.
template <class T,
          class SetTy =
              df_iterator_default_set<typename GraphTraits<T>::NodeRef>,
          bool External = false>
struct idf_iterator : df_iterator<Inverse<T>, SetTy, External> {
  /// Construct from an underlying inverse depth-first iterator.
  /// @param V Iterator state to wrap.
  idf_iterator(const df_iterator<Inverse<T>, SetTy, External> &V)
      : df_iterator<Inverse<T>, SetTy, External>(V) {}
};

/// Return an inverse depth-first iterator at the entry of \p G.
/// @param G Graph whose inverse edges are traversed.
/// @return Inverse depth-first iterator at the entry of \p G.
template <class T>
idf_iterator<T> idf_begin(const T &G) {
  return idf_iterator<T>::begin(Inverse<T>(G));
}

/// Return a past-the-end inverse depth-first iterator for \p G.
/// @param G Graph whose inverse edges are traversed.
/// @return Past-the-end inverse depth-first iterator for \p G.
template <class T>
idf_iterator<T> idf_end(const T &G) {
  return idf_iterator<T>::end(Inverse<T>(G));
}

/// Return a range that visits the inverse of \p G in depth-first order.
/// @param G Graph whose inverse edges are traversed.
/// @return Iterator range covering an inverse depth-first walk of \p G.
template <class T>
iterator_range<idf_iterator<T>> inverse_depth_first(const T &G) {
  return make_range(idf_begin(G), idf_end(G));
}

/// Inverse depth-first iterator that stores its visited set externally.
template <class T,
          class SetTy =
              df_iterator_default_set<typename GraphTraits<T>::NodeRef>>
struct idf_ext_iterator : idf_iterator<T, SetTy, true> {
  /// Construct from an underlying external inverse depth-first iterator.
  /// @param V Iterator state to wrap.
  idf_ext_iterator(const idf_iterator<T, SetTy, true> &V)
      : idf_iterator<T, SetTy, true>(V) {}

  /// Construct from an underlying inverse depth-first iterator with external
  /// storage.
  /// @param V Iterator state to wrap.
  idf_ext_iterator(const df_iterator<Inverse<T>, SetTy, true> &V)
      : idf_iterator<T, SetTy, true>(V) {}
};

/// Return an external inverse depth-first iterator at the entry of \p G.
/// @param G Graph whose inverse edges are traversed.
/// @param S Caller-owned visited set.
/// @return External inverse depth-first iterator at the entry of \p G.
template <class T, class SetTy>
idf_ext_iterator<T, SetTy> idf_ext_begin(const T &G, SetTy &S) {
  return idf_ext_iterator<T, SetTy>::begin(Inverse<T>(G), S);
}

/// Return a past-the-end external inverse depth-first iterator for \p G.
/// @param G Graph whose inverse edges are traversed.
/// @param S Caller-owned visited set.
/// @return Past-the-end external inverse depth-first iterator for \p G.
template <class T, class SetTy>
idf_ext_iterator<T, SetTy> idf_ext_end(const T &G, SetTy &S) {
  return idf_ext_iterator<T, SetTy>::end(Inverse<T>(G), S);
}

/// Return a range that visits the inverse of \p G in depth-first order using
/// visited set \p S.
/// @param G Graph whose inverse edges are traversed.
/// @param S Caller-owned visited set.
/// @return Iterator range covering an inverse depth-first walk of \p G using \p S.
template <class T, class SetTy>
iterator_range<idf_ext_iterator<T, SetTy>>
inverse_depth_first_ext(const T &G, SetTy &S) {
  return make_range(idf_ext_begin(G, S), idf_ext_end(G, S));
}

} // end namespace llvm

#endif // LLVM_ADT_DEPTHFIRSTITERATOR_H
