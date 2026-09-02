//===- llvm/ADT/EquivalenceClasses.h - Generic Equiv. Classes ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Generic implementation of equivalence classes through the use Tarjan's
/// efficient union-find algorithm.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_EQUIVALENCECLASSES_H
#define LLVM_ADT_EQUIVALENCECLASSES_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Allocator.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>

namespace llvm {

/// Collection of equivalence classes with insert, union, and find operations.
///
/// In addition to these modification methods, it is possible to iterate over
/// all of the equivalence classes and all of the elements in a class.
///
/// This implementation is an efficient implementation that only stores one copy
/// of the element being indexed per entry in the set, and allows any arbitrary
/// type to be indexed (as long as it can be implements DenseMapInfo).
///
/// Here is a simple example using integers:
///
/// \code
///  EquivalenceClasses<int> EC;
///  EC.unionSets(1, 2);                // insert 1, 2 into the same set
///  EC.insert(4); EC.insert(5);        // insert 4, 5 into own sets
///  EC.unionSets(5, 1);                // merge the set for 1 with 5's set.
///
///  for (EquivalenceClasses<int>::iterator I = EC.begin(), E = EC.end();
///       I != E; ++I) {           // Iterate over all of the equivalence sets.
///    if (!I->isLeader()) continue;   // Ignore non-leader sets.
///    for (EquivalenceClasses<int>::member_iterator MI = EC.member_begin(I);
///         MI != EC.member_end(); ++MI)   // Loop over members in this set.
///      cerr << *MI << " ";  // Print member.
///    cerr << "\n";   // Finish set.
///  }
/// \endcode
///
/// This example prints:
///   4
///   5 1 2
///
template <class ElemTy> class EquivalenceClasses {
public:
  /// One member of an equivalence class, with links to its leader and neighbors.
  ///
  /// Each node stores the value itself, a next pointer used to enumerate
  /// members of the unioned set, and either an end-of-list pointer or a leader
  /// pointer depending on whether the value itself is a leader. A leader
  /// pointer points to the leader for this element when the node is not a
  /// leader. An end-of-list pointer points to the last node in the member list.
  /// Whether a node is a leader is determined by a bit stolen from one of the
  /// pointers.
  class ECValue {
    friend class EquivalenceClasses;

    mutable const ECValue *Leader, *Next;
    ElemTy Data;

    // ECValue ctor - Start out with EndOfList pointing to this node, Next is
    // Null, isLeader = true.
    ECValue(const ElemTy &Elt)
        : Leader(this),
          Next(reinterpret_cast<ECValue *>(static_cast<intptr_t>(1))),
          Data(Elt) {}

    const ECValue *getLeader() const {
      if (isLeader())
        return this;
      if (Leader->isLeader())
        return Leader;
      // Path compression.
      return Leader = Leader->getLeader();
    }

    const ECValue *getEndOfList() const {
      assert(isLeader() && "Cannot get the end of a list for a non-leader!");
      return Leader;
    }

    void setNext(const ECValue *NewNext) const {
      assert(getNext() == nullptr && "Already has a next pointer!");
      Next = reinterpret_cast<const ECValue *>(
          reinterpret_cast<intptr_t>(NewNext) |
          static_cast<intptr_t>(isLeader()));
    }

  public:
    /// Copy a singleton node; the source must be a leader with no next member.
    ECValue(const ECValue &RHS)
        : Leader(this),
          Next(reinterpret_cast<ECValue *>(static_cast<intptr_t>(1))),
          Data(RHS.Data) {
      // Only support copying of singleton nodes.
      assert(RHS.isLeader() && RHS.getNext() == nullptr && "Not a singleton!");
    }

    /// True if this node is the leader of its equivalence class.
    bool isLeader() const { return (intptr_t)Next & 1; }
    /// Stored element value for this member.
    const ElemTy &getData() const { return Data; }

    /// Next member in the circular class list, or null at the end.
    const ECValue *getNext() const {
      return reinterpret_cast<ECValue *>(reinterpret_cast<intptr_t>(Next) &
                                         ~static_cast<intptr_t>(1));
    }
  };

private:
  /// This implicitly provides a mapping from ElemTy values to the ECValues, it
  /// just keeps the key as part of the value.
  DenseMap<ElemTy, ECValue *> TheMapping;

  /// List of all members, used to provide a deterministic iteration order.
  SmallVector<const ECValue *> Members;

  mutable BumpPtrAllocator ECValueAllocator;

public:
  /// Construct an empty collection of equivalence classes.
  EquivalenceClasses() = default;
  /// Deep-copy another collection of equivalence classes.
  EquivalenceClasses(const EquivalenceClasses &RHS) { operator=(RHS); }

  /// Replace this collection with a deep copy of \p RHS.
  EquivalenceClasses &operator=(const EquivalenceClasses &RHS) {
    TheMapping.clear();
    Members.clear();
    for (const auto &E : RHS)
      if (E->isLeader()) {
        member_iterator MI = RHS.member_begin(*E);
        member_iterator LeaderIt = member_begin(insert(*MI));
        for (++MI; MI != member_end(); ++MI)
          unionSets(LeaderIt, member_begin(insert(*MI)));
      }
    return *this;
  }

  //===--------------------------------------------------------------------===//
  // Inspection methods
  //

  /// iterator* - Provides a way to iterate over all values in the set.
  using iterator = typename SmallVector<const ECValue *>::const_iterator;

  /// Iterator to the first stored member pointer.
  iterator begin() const { return Members.begin(); }
  /// Iterator past the last stored member pointer.
  iterator end() const { return Members.end(); }

  /// True if no elements have been inserted.
  bool empty() const { return TheMapping.empty(); }

  /// member_* Iterate over the members of an equivalence class.
  class member_iterator;
  /// Begin iterating members of the class led by \p ECV (or end if not a leader).
  member_iterator member_begin(const ECValue &ECV) const {
    // Only leaders provide anything to iterate over.
    return member_iterator(ECV.isLeader() ? &ECV : nullptr);
  }

  /// Past-the-end iterator for member walks.
  member_iterator member_end() const { return member_iterator(nullptr); }

  /// Range over members of the equivalence class of \p ECV.
  iterator_range<member_iterator> members(const ECValue &ECV) const {
    return make_range(member_begin(ECV), member_end());
  }

  /// Range over members of the equivalence class containing \p V.
  iterator_range<member_iterator> members(const ElemTy &V) const {
    return make_range(findLeader(V), member_end());
  }

  /// Returns true if \p V is contained an equivalence class.
  [[nodiscard]] bool contains(const ElemTy &V) const {
    return TheMapping.contains(V);
  }

  /// Return the leader value for \p V, which must already be in the set.
  ///
  /// It is an error to call this for a value that is not yet in the set; use
  /// getOrInsertLeaderValue(V) instead.
  const ElemTy &getLeaderValue(const ElemTy &V) const {
    member_iterator MI = findLeader(V);
    assert(MI != member_end() && "Value is not in the set!");
    return *MI;
  }

  /// Return the leader for \p V, inserting \p V first if needed.
  const ElemTy &getOrInsertLeaderValue(const ElemTy &V) {
    member_iterator MI = findLeader(insert(V));
    assert(MI != member_end() && "Value is not in the set!");
    return *MI;
  }

  /// Return the number of equivalence classes (linear time).
  unsigned getNumClasses() const {
    unsigned NC = 0;
    for (const auto &E : *this)
      if (E->isLeader())
        ++NC;
    return NC;
  }

  //===--------------------------------------------------------------------===//
  // Mutation methods

  /// Insert a new value into the union/find set, ignoring the request if the
  /// value already exists.
  const ECValue &insert(const ElemTy &Data) {
    auto [I, Inserted] = TheMapping.try_emplace(Data);
    if (!Inserted)
      return *I->second;

    auto *ECV = new (ECValueAllocator) ECValue(Data);
    I->second = ECV;
    Members.push_back(ECV);
    return *ECV;
  }

  /// Erase a value from the union/find set, return true if erase succeeded, or
  /// false when the value was not found.
  bool erase(const ElemTy &V) {
    if (!TheMapping.contains(V))
      return false;
    const ECValue *Cur = TheMapping[V];
    const ECValue *Next = Cur->getNext();
    // If the current element is the leader and has a successor element,
    // update the successor element's 'Leader' field to be the last element,
    // set the successor element's stolen bit, and set the 'Leader' field of
    // all other elements in same class to be the successor element.
    if (Cur->isLeader() && Next) {
      Next->Leader = Cur->Leader;
      Next->Next = reinterpret_cast<const ECValue *>(
          reinterpret_cast<intptr_t>(Next->Next) | static_cast<intptr_t>(1));

      const ECValue *NewLeader = Next;
      while ((Next = Next->getNext())) {
        Next->Leader = NewLeader;
      }
    } else if (!Cur->isLeader()) {
      const ECValue *Leader = findLeader(V).Node;
      const ECValue *Pre = Leader;
      while (Pre->getNext() != Cur) {
        Pre = Pre->getNext();
      }
      if (!Next) {
        // If the current element is the last element(not leader), set the
        // successor of the current element's predecessor to null while
        // preserving the leader bit, and set the 'Leader' field of the class
        // leader to the predecessor element.
        Pre->Next = reinterpret_cast<const ECValue *>(
            static_cast<intptr_t>(Pre->isLeader()));
        Leader->Leader = Pre;
      } else {
        // If the current element is in the middle of class, then simply
        // connect the predecessor element and the successor element.
        Pre->Next = reinterpret_cast<const ECValue *>(
            reinterpret_cast<intptr_t>(Next) |
            static_cast<intptr_t>(Pre->isLeader()));
        Next->Leader = Pre;
      }
    }

    // Update 'TheMapping' and 'Members'.
    assert(TheMapping.contains(V) && "Can't find input in TheMapping!");
    TheMapping.erase(V);
    auto I = find(Members, Cur);
    assert(I != Members.end() && "Can't find input in members!");
    Members.erase(I);
    return true;
  }

  /// Find the leader iterator for the equivalence class containing \p V.
  ///
  /// Performs path compression. Returns member_end() if \p V is not in the set.
  member_iterator findLeader(const ElemTy &V) const {
    auto I = TheMapping.find(V);
    if (I == TheMapping.end())
      return member_iterator(nullptr);
    return findLeader(*I->second);
  }
  /// Return a member iterator for the leader of the class containing \p ECV.
  member_iterator findLeader(const ECValue &ECV) const {
    return member_iterator(ECV.getLeader());
  }

  /// Erase the class containing \p V, i.e. erase all members of the class from
  /// the set.
  void eraseClass(const ElemTy &V) {
    if (!TheMapping.contains(V))
      return;
    iterator_range<member_iterator> LeaderI = members(V);
    for (member_iterator MI = LeaderI.begin(), ME = LeaderI.end(); MI != ME;) {
      const ElemTy &ToErase = *MI;
      ++MI;
      const ECValue *Cur = TheMapping[ToErase];
      TheMapping.erase(ToErase);
      auto I = find(Members, Cur);
      assert(I != Members.end() && "Can't find input in members!");
      Members.erase(I);
    }
  }

  /// Merge the two equivalence sets for the specified values, inserting
  /// them if they do not already exist in the equivalence set.
  member_iterator unionSets(const ElemTy &V1, const ElemTy &V2) {
    const ECValue &V1I = insert(V1), &V2I = insert(V2);
    return unionSets(findLeader(V1I), findLeader(V2I));
  }
  /// Merge the equivalence classes whose leaders are \p L1 and \p L2.
  member_iterator unionSets(member_iterator L1, member_iterator L2) {
    assert(L1 != member_end() && L2 != member_end() && "Illegal inputs!");
    if (L1 == L2)
      return L1; // Unifying the same two sets, noop.

    // Otherwise, this is a real union operation.  Set the end of the L1 list to
    // point to the L2 leader node.
    const ECValue &L1LV = *L1.Node, &L2LV = *L2.Node;
    L1LV.getEndOfList()->setNext(&L2LV);

    // Update L1LV's end of list pointer.
    L1LV.Leader = L2LV.getEndOfList();

    // Clear L2's leader flag:
    L2LV.Next = L2LV.getNext();

    // L2's leader is now L1.
    L2LV.Leader = &L1LV;
    return L1;
  }

  /// Return true if \p V1 is equivalent to \p V2.
  ///
  /// This holds when the values compare equal or already share an equivalence
  /// class.
  bool isEquivalent(const ElemTy &V1, const ElemTy &V2) const {
    // Fast path: any element is equivalent to itself.
    if (V1 == V2)
      return true;
    auto It = findLeader(V1);
    return It != member_end() && It == findLeader(V2);
  }

  /// Forward iterator over members of a single equivalence class.
  class member_iterator {
    friend class EquivalenceClasses;

    const ECValue *Node;

  public:
    /// Category tag required by the iterator concept.
    using iterator_category = std::forward_iterator_tag;
    /// Type of element referred to by the iterator.
    using value_type = const ElemTy;
    /// Unsigned type used for iterator size expressions.
    using size_type = std::size_t;
    /// Signed type used for iterator differences.
    using difference_type = std::ptrdiff_t;
    /// Pointer to a const element.
    using pointer = value_type *;
    /// Reference to a const element.
    using reference = value_type &;

    /// Construct a past-the-end member iterator.
    explicit member_iterator() = default;
    /// Construct an iterator positioned at member node \p N.
    explicit member_iterator(const ECValue *N) : Node(N) {}

    /// Dereference to the stored element.
    reference operator*() const {
      assert(Node != nullptr && "Dereferencing end()!");
      return Node->getData();
    }
    /// Access the stored element through a pointer.
    pointer operator->() const { return &operator*(); }

    /// Advance to the next member in the class.
    member_iterator &operator++() {
      assert(Node != nullptr && "++'d off the end of the list!");
      Node = Node->getNext();
      return *this;
    }

    /// Post-increment; return the previous position.
    member_iterator operator++(int) { // postincrement operators.
      member_iterator tmp = *this;
      ++*this;
      return tmp;
    }

    /// True if both iterators refer to the same member node.
    bool operator==(const member_iterator &RHS) const {
      return Node == RHS.Node;
    }
    /// True if the iterators refer to different member nodes.
    bool operator!=(const member_iterator &RHS) const {
      return Node != RHS.Node;
    }
  };
};

} // end namespace llvm

#endif // LLVM_ADT_EQUIVALENCECLASSES_H
