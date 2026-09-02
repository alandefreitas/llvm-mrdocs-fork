//===- llvm/ADT/ilist_base.h - Intrusive List Base --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_ILIST_BASE_H
#define LLVM_ADT_ILIST_BASE_H

#include "llvm/ADT/ilist_node_base.h"
#include <cassert>

namespace llvm {

/// Implementations of list algorithms using ilist_node_base.
template <bool EnableSentinelTracking, class ParentTy> class ilist_base {
public:
  /// Node base type this algorithm operates on.
  using node_base_type = ilist_node_base<EnableSentinelTracking, ParentTy>;

  /// Insert \p N immediately before \p Next in the circular list.
  static void insertBeforeImpl(node_base_type &Next, node_base_type &N) {
    node_base_type &Prev = *Next.getPrev();
    N.setNext(&Next);
    N.setPrev(&Prev);
    Prev.setNext(&N);
    Next.setPrev(&N);
  }

  /// Unlink \p N from its neighbors without deleting it.
  static void removeImpl(node_base_type &N) {
    node_base_type *Prev = N.getPrev();
    node_base_type *Next = N.getNext();
    Next->setPrev(Prev);
    Prev->setNext(Next);

    // Not strictly necessary, but helps catch a class of bugs.
    N.setPrev(nullptr);
    N.setNext(nullptr);
  }

  /// Unlink the half-open node range [\p First, \p Last) from the list.
  static void removeRangeImpl(node_base_type &First, node_base_type &Last) {
    node_base_type *Prev = First.getPrev();
    node_base_type *Final = Last.getPrev();
    Last.setPrev(Prev);
    Prev->setNext(&Last);

    // Not strictly necessary, but helps catch a class of bugs.
    First.setPrev(nullptr);
    Final->setNext(nullptr);
  }

  /// Move [\p First, \p Last) so it appears immediately before \p Next.
  static void transferBeforeImpl(node_base_type &Next, node_base_type &First,
                                 node_base_type &Last) {
    if (&Next == &Last || &First == &Last)
      return;

    // Position cannot be contained in the range to be transferred.
    assert(&Next != &First &&
           // Check for the most common mistake.
           "Insertion point can't be one of the transferred nodes");

    node_base_type &Final = *Last.getPrev();

    // Detach from old list/position.
    First.getPrev()->setNext(&Last);
    Last.setPrev(First.getPrev());

    // Splice [First, Final] into its new list/position.
    node_base_type &Prev = *Next.getPrev();
    Final.setNext(&Next);
    First.setPrev(&Prev);
    Prev.setNext(&First);
    Next.setPrev(&Final);
  }

  /// Insert typed node \p N immediately before \p Next.
  /// @param Next Node currently at the insertion point.
  /// @param N Node to insert.
  template <class T> static void insertBefore(T &Next, T &N) {
    insertBeforeImpl(Next, N);
  }

  /// Unlink typed node \p N from its list.
  template <class T> static void remove(T &N) { removeImpl(N); }
  /// Unlink the half-open typed range [\p First, \p Last) from its list.
  /// @param First First node to remove.
  /// @param Last Node after the last removed node.
  template <class T> static void removeRange(T &First, T &Last) {
    removeRangeImpl(First, Last);
  }

  /// Move typed range [\p First, \p Last) before \p Next.
  template <class T> static void transferBefore(T &Next, T &First, T &Last) {
    transferBeforeImpl(Next, First, Last);
  }
};

} // end namespace llvm

#endif // LLVM_ADT_ILIST_BASE_H
