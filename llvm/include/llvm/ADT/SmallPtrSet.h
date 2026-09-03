//===- llvm/ADT/SmallPtrSet.h - 'Normally small' pointer set ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the SmallPtrSet class.  See the doxygen comment for
/// SmallPtrSetImplBase for more details on the algorithm used.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_SMALLPTRSET_H
#define LLVM_ADT_SMALLPTRSET_H

#include "llvm/ADT/ADL.h"
#include "llvm/ADT/EpochTracker.h"
#include "llvm/ADT/STLForwardCompat.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/ReverseIteration.h"
#include "llvm/Support/type_traits.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <utility>

namespace llvm {

/// Common base implementation shared by all \c SmallPtrSet specializations.
///
/// SmallPtrSet has two modes, one for small and one for large sets.
///
/// Small sets use an array of pointers allocated in the SmallPtrSet object,
/// which is treated as a simple array of pointers.  When a pointer is added to
/// the set, the array is scanned to see if the element already exists, if not
/// the element is 'pushed back' onto the array.  If we run out of space in the
/// array, we grow into the 'large set' case.  SmallSet should be used when the
/// sets are often small.  In this case, no memory allocation is used, and only
/// light-weight and cache-efficient scanning is used.
///
/// Large sets use a linear-probed hash table with deletion implemented using
/// Knuth TAOCP 6.4 Algorithm R: `erase` opens a hole, walks forward sliding
/// each following entry whose probe path crosses the hole back into it (the
/// hole moves with each slide), and stops at the next empty slot.  Empty
/// buckets are represented with an illegal pointer value (-1) to allow null
/// pointers to be inserted; no tombstone state is needed.  The hash table is
/// resized when the table is 2/3 or more.  When this happens, the table is
/// doubled in size.
class SmallPtrSetImplBase : public DebugEpochBase {
  friend class SmallPtrSetIteratorImpl;

protected:
  /// The current set of buckets, in either small or big representation.
  const void **CurArray;
  /// CurArraySize - The allocated size of CurArray, always a power of two.
  unsigned CurArraySize;

  /// Number of elements in CurArray that contain a value.
  /// If small, all these elements are at the beginning of CurArray and the rest
  /// is uninitialized.
  unsigned NumEntries;
  /// Whether the set is in small representation.
  bool IsSmall;

  /// Copy-construct from \p that into inline storage \p SmallStorage.
  ///
  /// Duplicates the source set's contents, allocating a heap table if \p that
  /// has grown beyond its small representation.
  ///
  /// \param SmallStorage Inline storage for the new set.
  /// \param that Set to copy from.
  LLVM_ABI SmallPtrSetImplBase(const void **SmallStorage,
                               const SmallPtrSetImplBase &that);
  /// Move-construct from \p that into inline storage \p SmallStorage.
  ///
  /// Transfers ownership of a heap-backed table when \p that is large; leaves
  /// \p that empty in small mode using \p RHSSmallStorage.
  ///
  /// \param SmallStorage Inline storage for the new set.
  /// \param SmallSize Capacity of \p SmallStorage (power of two).
  /// \param RHSSmallStorage Inline storage used when leaving \p that small.
  /// \param that Set to move from.
  LLVM_ABI SmallPtrSetImplBase(const void **SmallStorage, unsigned SmallSize,
                               const void **RHSSmallStorage,
                               SmallPtrSetImplBase &&that);

  /// Construct an empty set using inline storage \p SmallStorage.
  ///
  /// \p SmallSize must be a power of two and sets the initial small-mode
  /// capacity before any heap allocation occurs.
  ///
  /// \param SmallStorage Inline storage for the new set.
  /// \param SmallSize Initial small-mode capacity (power of two).
  explicit SmallPtrSetImplBase(const void **SmallStorage, unsigned SmallSize)
      : CurArray(SmallStorage), CurArraySize(SmallSize), NumEntries(0),
        IsSmall(true) {
    assert(llvm::has_single_bit(SmallSize) &&
           "Initial size must be a power of two!");
  }

  /// Release heap storage when the set has grown beyond small mode.
  ~SmallPtrSetImplBase() {
    if (!isSmall())
      free(CurArray);
  }

public:
  /// Unsigned type used to express set size and capacity.
  using size_type = unsigned;

  /// Assignment is not allowed; derived classes provide typed assignment.
  ///
  /// \param Unused Ignored; copy assignment is not supported.
  SmallPtrSetImplBase &operator=(const SmallPtrSetImplBase &Unused) = delete;

  /// Return true if the set contains no elements.
  ///
  /// @return True if the set is empty.
  [[nodiscard]] bool empty() const { return size() == 0; }
  /// Return the number of pointers stored in the set.
  ///
  /// @return The number of pointers stored in the set.
  [[nodiscard]] size_type size() const { return NumEntries; }
  /// Return the allocated bucket count of the current representation.
  ///
  /// In small mode this is the inline array length; in large mode it is the
  /// hash table size (always a power of two).
  ///
  /// @return The allocated bucket count.
  [[nodiscard]] size_type capacity() const { return CurArraySize; }

  /// Remove all elements from the set.
  ///
  /// Large sets may shrink and reallocate when capacity greatly exceeds size.
  void clear() {
    incrementEpoch();
    // If the capacity of the array is huge, and the # elements used is small,
    // shrink the array.
    if (!isSmall()) {
      if (size() * 4 < CurArraySize && CurArraySize > 32)
        return shrink_and_clear();
      // Fill the array with empty markers.
      memset(CurArray, -1, CurArraySize * sizeof(void *));
    }

    NumEntries = 0;
  }

  /// Ensure the set can hold at least \p NewNumEntries elements without
  /// rehashing on subsequent inserts.
  ///
  /// No-op when \p NewNumEntries is zero or the current storage already
  /// satisfies the load factor bound used by the large-set representation.
  ///
  /// \param NewNumEntries Minimum number of elements to reserve capacity for.
  void reserve(size_type NewNumEntries) {
    incrementEpoch();
    // Do nothing if we're given zero as a reservation size.
    if (NewNumEntries == 0)
      return;
    // No need to expand if we're small and NewNumEntries will fit in the space.
    if (isSmall() && NewNumEntries <= CurArraySize)
      return;
    // insert_imp_big will reallocate if stores is more than 2/3 full, on the
    // /final/ insertion.
    if (!isSmall() && ((NewNumEntries - 1) * 3) < (CurArraySize * 2))
      return;
    // We must Grow -- find the size where we'd be 2/3 full, then round up to
    // the next power of two.
    size_type NewSize = NewNumEntries + (NewNumEntries / 2);
    NewSize = llvm::bit_ceil(NewSize);
    // Like insert_imp_big, always allocate at least 128 elements.
    NewSize = std::max(128u, NewSize);
    Grow(NewSize);
  }

protected:
  /// Sentinel pointer value marking unused hash-table slots.
  ///
  /// Chosen so that \c clear() can memset buckets efficiently and so that null
  /// pointers remain valid set elements.
  ///
  /// @return Sentinel pointer value used for unused hash-table slots.
  static void *getEmptyMarker() {
    // Note that -1 is chosen to make clear() efficiently implementable with
    // memset and because it's not a valid pointer value.
    return reinterpret_cast<void *>(-1);
  }

  /// Return a pointer one past the last valid bucket for iteration.
  ///
  /// In small mode this is the first unused slot after \c NumEntries; in large
  /// mode it is one past the full hash-table array.
  ///
  /// @return Pointer one past the last valid bucket.
  const void **EndPointer() const {
    return isSmall() ? CurArray + NumEntries : CurArray + CurArraySize;
  }

  /// Iterate over occupied slots in small-mode storage.
  ///
  /// @return Range over occupied slots in small-mode storage.
  iterator_range<const void **> small_buckets() {
    return make_range(CurArray, CurArray + NumEntries);
  }

  /// Iterate over occupied slots in small-mode storage.
  ///
  /// @return Range over occupied slots in small-mode storage.
  iterator_range<const void *const *> small_buckets() const {
    return {CurArray, CurArray + NumEntries};
  }

  /// Iterate over all buckets in the current representation.
  ///
  /// In small mode only occupied entries are visited; in large mode every
  /// table slot up to \c capacity() is included (empty slots hold the empty
  /// marker).
  ///
  /// @return Range over all buckets in the current representation.
  iterator_range<const void **> buckets() {
    return make_range(CurArray, EndPointer());
  }

  /// Iterate over all buckets in the current representation.
  ///
  /// @return Range over all buckets in the current representation.
  iterator_range<const void *const *> buckets() const {
    return make_range(CurArray, EndPointer());
  }

  /// Insert \p Ptr into the set if absent.
  ///
  /// Returns true if the pointer was new to the set, false if it was already
  /// present. Hidden from the client so the derived class can check that the
  /// right type of pointer is passed in.
  ///
  /// \param Ptr Void pointer value to insert.
  /// @return Pair of a bucket pointer and whether the pointer was newly
  ///         inserted.
  std::pair<const void *const *, bool> insert_imp(const void *Ptr) {
    if (isSmall()) {
      // Check to see if it is already in the set.
      for (const void *&Bucket : small_buckets()) {
        if (Bucket == Ptr)
          return {&Bucket, false};
      }

      // Nope, there isn't.  If we stay small, just 'pushback' now.
      if (NumEntries < CurArraySize) {
        CurArray[NumEntries++] = Ptr;
        incrementEpoch();
        return {CurArray + (NumEntries - 1), true};
      }
      // Otherwise, hit the big set case, which will call grow.
    }
    return insert_imp_big(Ptr);
  }

  /// Erase \p Ptr from the set if present.
  ///
  /// Returns true if the pointer was removed, false otherwise. Hidden from the
  /// client so the derived class can check that the right type of pointer is
  /// passed in.
  ///
  /// \param Ptr Void pointer value to erase.
  /// @return True if the pointer was removed; false otherwise.
  bool erase_imp(const void *Ptr) {
    if (isSmall()) {
      for (const void *&Bucket : small_buckets()) {
        if (Bucket == Ptr) {
          Bucket = CurArray[--NumEntries];
          incrementEpoch();
          return true;
        }
      }
      return false;
    }

    auto *Bucket = doFind(Ptr);
    if (!Bucket)
      return false;

    eraseFromBucket(const_cast<const void **>(Bucket));
    --NumEntries;
    incrementEpoch();
    return true;
  }

  /// Return a raw bucket pointer for constructing an iterator to \p Ptr.
  ///
  /// If the element is not found, this will be \c EndPointer. Otherwise, it
  /// will be a pointer to the slot which stores \p Ptr.
  ///
  /// \param Ptr Void pointer value to look up.
  /// @return Bucket pointer for \p Ptr, or \c EndPointer if not found.
  const void *const *find_imp(const void *Ptr) const {
    if (isSmall()) {
      // Linear search for the item.
      for (const void *const &Bucket : small_buckets())
        if (Bucket == Ptr)
          return &Bucket;
      return EndPointer();
    }

    // Big set case.
    if (auto *Bucket = doFind(Ptr))
      return Bucket;
    return EndPointer();
  }

  /// Return true if \p Ptr is in the set.
  ///
  /// Uses linear search in small mode and hash lookup in large mode.
  ///
  /// \param Ptr Void pointer value to test for membership.
  /// @return True if \p Ptr is in the set.
  bool contains_imp(const void *Ptr) const {
    if (isSmall()) {
      // Linear search for the item.
      for (const void *const &Bucket : small_buckets())
        if (Bucket == Ptr)
          return true;
      return false;
    }

    return doFind(Ptr) != nullptr;
  }

  /// Return true when the set uses inline small-mode storage.
  ///
  /// @return True when the set uses inline small-mode storage.
  bool isSmall() const { return IsSmall; }

private:
  LLVM_ABI std::pair<const void *const *, bool> insert_imp_big(const void *Ptr);

  LLVM_ABI const void *const *doFind(const void *Ptr) const;
  LLVM_ABI void shrink_and_clear();

protected:
  /// Erase the entry at \p Bucket and close the resulting hole via Knuth
  /// TAOCP 6.4 Algorithm R. Caller must update \c NumEntries and the epoch.
  ///
  /// \param Bucket Pointer to the occupied bucket slot to erase.
  LLVM_ABI void eraseFromBucket(const void **Bucket);

  /// Allocate a larger backing store for the buckets and move entries over.
  ///
  /// Passing the current size triggers a same-size rehash, used by batch erase
  /// to compact away empty slots left by mark-then-rebuild.
  ///
  /// \param NewSize Desired bucket count after growth (or current size to
  ///        rehash in place).
  LLVM_ABI void Grow(unsigned NewSize);

  /// Swap the elements of this set with \p RHS.
  ///
  /// Note: This method assumes that both sets have the same small size.
  ///
  /// \param SmallStorage Inline storage for this set.
  /// \param RHSSmallStorage Inline storage for \p RHS.
  /// \param RHS Other set to exchange contents with.
  LLVM_ABI void swap(const void **SmallStorage, const void **RHSSmallStorage,
                     SmallPtrSetImplBase &RHS);

  /// Replace this set's contents with a copy of \p RHS.
  ///
  /// \p SmallStorage is the inline buffer to use when the result stays small.
  ///
  /// \param SmallStorage Inline storage for this set.
  /// \param RHS Set to copy from.
  LLVM_ABI void copyFrom(const void **SmallStorage,
                         const SmallPtrSetImplBase &RHS);
  /// Replace this set's contents by moving from \p RHS.
  ///
  /// Transfers heap storage when \p RHS is large and leaves \p RHS empty in
  /// small mode using \p RHSSmallStorage.
  ///
  /// \param SmallStorage Inline storage for this set.
  /// \param SmallSize Capacity of \p SmallStorage (power of two).
  /// \param RHSSmallStorage Inline storage used when leaving \p RHS small.
  /// \param RHS Set to move from.
  LLVM_ABI void moveFrom(const void **SmallStorage, unsigned SmallSize,
                         const void **RHSSmallStorage,
                         SmallPtrSetImplBase &&RHS);

private:
  /// Code shared by moveFrom() and move constructor.
  void moveHelper(const void **SmallStorage, unsigned SmallSize,
                  const void **RHSSmallStorage, SmallPtrSetImplBase &&RHS);
  /// Code shared by copyFrom() and copy constructor.
  void copyHelper(const SmallPtrSetImplBase &RHS);
};

/// SmallPtrSetIteratorImpl - This is the common base class shared between all
/// instances of SmallPtrSetIterator.
class LLVM_DEBUGEPOCHBASE_HANDLEBASE_EMPTYBASE SmallPtrSetIteratorImpl
    : public DebugEpochBase::HandleBase {
public:
  /// Construct an iterator over buckets from \p BP up to (but not including)
  /// \p E, tracking mutations via \p Epoch.
  ///
  /// \param BP Pointer to the first bucket to visit.
  /// \param E Pointer past the last bucket to visit.
  /// \param Epoch Epoch tracker used to detect invalidation.
  explicit SmallPtrSetIteratorImpl(const void *const *BP, const void *const *E,
                                   const DebugEpochBase &Epoch)
      : DebugEpochBase::HandleBase(&Epoch), Bucket(BP), End(E) {
    AdvanceIfNotValid();
  }

  /// Return true if both iterators refer to the same bucket position.
  ///
  /// \param RHS Iterator to compare with.
  /// @return True if both iterators refer to the same bucket position.
  bool operator==(const SmallPtrSetIteratorImpl &RHS) const {
    return Bucket == RHS.Bucket;
  }
  /// Return true if the iterators refer to different bucket positions.
  ///
  /// \param RHS Iterator to compare with.
  /// @return True if the iterators refer to different bucket positions.
  bool operator!=(const SmallPtrSetIteratorImpl &RHS) const {
    return Bucket != RHS.Bucket;
  }

protected:
  /// Return the pointer stored in the current bucket.
  ///
  /// @return The pointer stored in the current bucket.
  void *dereference() const {
    assert(isHandleInSync() && "invalid iterator access!");
    assert(Bucket < End);
    return const_cast<void *>(*Bucket);
  }
  /// Advance to the next occupied bucket, skipping empty hash-table slots.
  void increment() {
    assert(isHandleInSync() && "invalid iterator access!");
    ++Bucket;
    AdvanceIfNotValid();
  }

private:
  /// AdvanceIfNotValid - If the current bucket isn't valid, advance to a bucket
  /// that is.   This is guaranteed to stop because the end() bucket is marked
  /// valid.
  void AdvanceIfNotValid() {
    assert(Bucket <= End);
    while (Bucket != End && *Bucket == SmallPtrSetImplBase::getEmptyMarker())
      ++Bucket;
  }

  using BucketItTy =
      std::conditional_t<shouldReverseIterate(),
                         std::reverse_iterator<const void *const *>,
                         const void *const *>;

  BucketItTy Bucket;
  BucketItTy End;
};

/// SmallPtrSetIterator - This implements a const_iterator for SmallPtrSet.
template <typename PtrTy>
class SmallPtrSetIterator : public SmallPtrSetIteratorImpl {
  using PtrTraits = PointerLikeTypeTraits<PtrTy>;

public:
  /// Element type produced when the iterator is dereferenced.
  using value_type = PtrTy;
  /// Reference type returned by dereferencing the iterator.
  using reference = PtrTy;
  /// Pointer type equivalent to the iterator's value type.
  using pointer = PtrTy;
  /// Signed type used to express the distance between iterators.
  using difference_type = std::ptrdiff_t;
  /// Iterator category; supports single-pass forward traversal.
  using iterator_category = std::forward_iterator_tag;

  /// Inherit constructors from \c SmallPtrSetIteratorImpl.
  using SmallPtrSetIteratorImpl::SmallPtrSetIteratorImpl;

  // Most methods are provided by the base class.

  /// Return the pointer at the current bucket, converted to \c PtrTy.
  ///
  /// @return The pointer at the current bucket, converted to \c PtrTy.
  [[nodiscard]] const PtrTy operator*() const {
    return PtrTraits::getFromVoidPointer(dereference());
  }

  /// Pre-increment: advance to the next element and return the updated iterator.
  ///
  /// @return Reference to this iterator after advancing.
  inline SmallPtrSetIterator &operator++() { // Preincrement
    increment();
    return *this;
  }

  /// Post-increment: advance to the next element and return the previous value.
  ///
  /// \param Unused Unused postfix-discriminator parameter.
  /// @return A copy of the iterator before advancing.
  SmallPtrSetIterator operator++(int Unused) { // Postincrement
    SmallPtrSetIterator tmp = *this;
    increment();
    return tmp;
  }
};

/// A templated base class for \c SmallPtrSet which provides the
/// typesafe interface that is common across all small sizes.
///
/// This is particularly useful for passing around between interface boundaries
/// to avoid encoding a particular small size in the interface boundary.
template <typename PtrType> class SmallPtrSetImpl : public SmallPtrSetImplBase {
  using ConstPtrType = typename add_const_past_pointer<PtrType>::type;
  using PtrTraits = PointerLikeTypeTraits<PtrType>;
  using ConstPtrTraits = PointerLikeTypeTraits<ConstPtrType>;

protected:
  /// Forward base-class constructors to derived \c SmallPtrSet types.
  using SmallPtrSetImplBase::SmallPtrSetImplBase;

public:
  /// Const forward iterator over set elements.
  using iterator = SmallPtrSetIterator<PtrType>;
  /// Const forward iterator over set elements.
  using const_iterator = SmallPtrSetIterator<PtrType>;
  /// Key type used for lookup and comparison.
  using key_type = ConstPtrType;
  /// Stored element type.
  using value_type = PtrType;

  /// Copy construction is not allowed; use \c SmallPtrSet assignment instead.
  ///
  /// \param Unused Ignored; copy construction is not supported.
  SmallPtrSetImpl(const SmallPtrSetImpl &Unused) = delete;

  /// Insert \p Ptr if it is not already present.
  ///
  /// The bool component of the returned pair is true if and only if the
  /// insertion takes place, and the iterator component of the pair points to
  /// the element equal to \p Ptr.
  ///
  /// \param Ptr Pointer to insert if absent.
  /// @return Pair of an iterator to the element and whether insertion occurred.
  std::pair<iterator, bool> insert(PtrType Ptr) {
    auto p = insert_imp(PtrTraits::getAsVoidPointer(Ptr));
    return {makeIterator(p.first), p.second};
  }

  /// Insert \p Ptr, ignoring the iterator hint.
  ///
  /// Identical to calling \c insert(Ptr), but allows \c SmallPtrSet to be used
  /// by \c std::insert_iterator and \c std::inserter().
  ///
  /// \param Hint Ignored insertion hint (for STL inserter compatibility).
  /// \param Ptr Pointer to insert if absent.
  /// @return Iterator to the inserted or existing element.
  iterator insert(iterator Hint, PtrType Ptr) { return insert(Ptr).first; }

  /// Remove \p Ptr from the set.
  ///
  /// Returns whether the pointer was in the set. Invalidates iterators if
  /// true is returned. To remove elements while iterating over the set, use
  /// \c remove_if() instead.
  ///
  /// \param Ptr Pointer to erase.
  /// @return True if the pointer was removed; false otherwise.
  bool erase(PtrType Ptr) {
    return erase_imp(PtrTraits::getAsVoidPointer(Ptr));
  }

  /// Remove elements that match the given predicate.
  ///
  /// This method is a safe replacement for the following pattern, which is not
  /// valid, because the erase() calls would invalidate the iterator:
  ///
  ///     for (PtrType *Ptr : Set)
  ///       if (Pred(P))
  ///         Set.erase(P);
  ///
  /// Returns whether anything was removed. The predicate must not access the
  /// set being modified: it may inspect the element passed to it and return
  /// true to request removal, but must not read (e.g. count()/find()) or
  /// otherwise mutate the set. If anything is removed, all iterators and
  /// references into the set are invalidated.
  ///
  /// \param P Unary predicate returning true for elements to remove.
  /// @return True if any element was removed.
  template <typename UnaryPredicate> bool remove_if(UnaryPredicate P) {
    bool Removed = false;
    if (isSmall()) {
      auto Buckets = small_buckets();
      const void **APtr = Buckets.begin(), **E = Buckets.end();
      while (APtr != E) {
        PtrType Ptr = PtrTraits::getFromVoidPointer(const_cast<void *>(*APtr));
        if (P(Ptr)) {
          *APtr = *--E;
          --NumEntries;
          incrementEpoch();
          Removed = true;
        } else {
          ++APtr;
        }
      }
      return Removed;
    }

    // Mark-then-rebuild: one pass to clear matches without sliding (which
    // would re-walk the cluster on every erase), then a single rehash to
    // restore the linear-probe invariant.  O(N) total, vs O(N * cluster)
    // for repeated per-match Algorithm R erases.
    for (const void *&Bucket : buckets()) {
      if (Bucket == getEmptyMarker())
        continue;
      PtrType Ptr = PtrTraits::getFromVoidPointer(const_cast<void *>(Bucket));
      if (P(Ptr)) {
        Bucket = getEmptyMarker();
        --NumEntries;
        Removed = true;
      }
    }
    if (Removed) {
      incrementEpoch();
      Grow(CurArraySize);
    }
    return Removed;
  }

  /// count - Return 1 if the specified pointer is in the set, 0 otherwise.
  ///
  /// \param Ptr Pointer to test for membership.
  /// @return 1 if \p Ptr is in the set, 0 otherwise.
  [[nodiscard]] size_type count(ConstPtrType Ptr) const {
    return contains_imp(ConstPtrTraits::getAsVoidPointer(Ptr));
  }
  /// Return an iterator to \p Ptr, or \c end() if it is not in the set.
  ///
  /// \param Ptr Pointer to look up.
  /// @return Iterator to \p Ptr, or \c end() if not found.
  [[nodiscard]] iterator find(ConstPtrType Ptr) const {
    return makeIterator(find_imp(ConstPtrTraits::getAsVoidPointer(Ptr)));
  }
  /// Return true if \p Ptr is an element of the set.
  ///
  /// \param Ptr Pointer to test for membership.
  /// @return True if \p Ptr is an element of the set.
  [[nodiscard]] bool contains(ConstPtrType Ptr) const {
    return contains_imp(ConstPtrTraits::getAsVoidPointer(Ptr));
  }

  /// Insert each pointer in the half-open range [\p I, \p E).
  ///
  /// \param I Iterator to the first pointer to insert.
  /// \param E Iterator past the last pointer to insert.
  template <typename IterT> void insert(IterT I, IterT E) {
    for (; I != E; ++I)
      insert(*I);
  }

  /// Insert each pointer in \p IL.
  ///
  /// \param IL Initializer list of pointers to insert.
  void insert(std::initializer_list<PtrType> IL) {
    insert(IL.begin(), IL.end());
  }

  /// Insert each pointer from range \p R using ADL \c begin/end.
  ///
  /// \param R Range whose elements are inserted.
  template <typename Range> void insert_range(Range &&R) {
    insert(adl_begin(R), adl_end(R));
  }

  /// Return an iterator to the first element, skipping empty hash-table slots.
  ///
  /// @return Iterator to the first element.
  [[nodiscard]] iterator begin() const {
    if constexpr (shouldReverseIterate())
      return makeIterator(EndPointer() - 1);
    else
      return makeIterator(CurArray);
  }
  /// Return an iterator past the last element.
  ///
  /// @return Iterator past the last element.
  [[nodiscard]] iterator end() const { return makeIterator(EndPointer()); }

private:
  /// Create an iterator that dereferences to same place as the given pointer.
  iterator makeIterator(const void *const *P) const {
    if constexpr (shouldReverseIterate())
      return iterator(P == EndPointer() ? CurArray : P + 1, CurArray, *this);
    else
      return iterator(P, EndPointer(), *this);
  }
};

/// Return true if \p LHS and \p RHS contain the same pointers.
///
/// Iterates over elements of \p LHS confirming that each value is also in
/// \p RHS, and that no additional values are in \p RHS.
///
/// \param LHS Left-hand set to compare.
/// \param RHS Right-hand set to compare.
/// @return True if \p LHS and \p RHS contain the same pointers.
template <typename PtrType>
[[nodiscard]] bool operator==(const SmallPtrSetImpl<PtrType> &LHS,
                              const SmallPtrSetImpl<PtrType> &RHS) {
  if (LHS.size() != RHS.size())
    return false;

  for (const auto *KV : LHS)
    if (!RHS.count(KV))
      return false;

  return true;
}

/// Return true if \p LHS and \p RHS do not contain the same pointers.
///
/// Equivalent to \c !(LHS == RHS).
///
/// \param LHS Left-hand set to compare.
/// \param RHS Right-hand set to compare.
/// @return True if \p LHS and \p RHS do not contain the same pointers.
template <typename PtrType>
[[nodiscard]] bool operator!=(const SmallPtrSetImpl<PtrType> &LHS,
                              const SmallPtrSetImpl<PtrType> &RHS) {
  return !(LHS == RHS);
}

/// A set of pointers optimized for holding SmallSize or fewer elements.
///
/// Internally rounds SmallSize up to the next power of two if it is not
/// already a power of two. See the comments on \c SmallPtrSetImplBase for
/// details of the algorithm.
template <class PtrType, unsigned SmallSize>
class SmallPtrSet : public SmallPtrSetImpl<PtrType> {
  // In small mode SmallPtrSet uses linear search for the elements, so it is
  // not a good idea to choose this value too high. You may consider using a
  // DenseSet<> instead if you expect many elements in the set.
  static_assert(SmallSize <= 32, "SmallSize should be small");

  using BaseT = SmallPtrSetImpl<PtrType>;

  // Make sure that SmallSize is a power of two, round up if not.
  static constexpr size_t SmallSizePowTwo = llvm::bit_ceil_constexpr(SmallSize);
  /// SmallStorage - Fixed size storage used in 'small mode'.
  const void *SmallStorage[SmallSizePowTwo];

public:
  /// Construct an empty set with inline capacity \c SmallSizePowTwo.
  SmallPtrSet() : BaseT(SmallStorage, SmallSizePowTwo) {}
  /// Copy-construct a set with the same elements as \p that.
  ///
  /// \param that Set to copy from.
  SmallPtrSet(const SmallPtrSet &that) : BaseT(SmallStorage, that) {}
  /// Move-construct a set, transferring storage from \p that.
  ///
  /// \param that Set to move from.
  SmallPtrSet(SmallPtrSet &&that)
      : BaseT(SmallStorage, SmallSizePowTwo, that.SmallStorage,
              std::move(that)) {}

  /// Construct a set and insert each pointer in [\p I, \p E).
  ///
  /// \param I Iterator to the first pointer to insert.
  /// \param E Iterator past the last pointer to insert.
  template <typename It>
  SmallPtrSet(It I, It E) : BaseT(SmallStorage, SmallSizePowTwo) {
    this->insert(I, E);
  }

  /// Construct a set from each pointer in range \p R.
  ///
  /// \param Tag Discriminator selecting the range constructor.
  /// \param R Range whose elements are inserted.
  template <typename Range>
  SmallPtrSet(llvm::from_range_t Tag, Range &&R)
      : SmallPtrSet(adl_begin(R), adl_end(R)) {}

  /// Construct a set containing each pointer in \p IL.
  ///
  /// \param IL Initializer list of pointers to insert.
  SmallPtrSet(std::initializer_list<PtrType> IL)
      : BaseT(SmallStorage, SmallSizePowTwo) {
    this->insert(IL.begin(), IL.end());
  }

  /// Replace contents with a copy of \p RHS.
  ///
  /// \param RHS Set to copy-assign from.
  /// @return Reference to this set.
  SmallPtrSet<PtrType, SmallSize> &
  operator=(const SmallPtrSet<PtrType, SmallSize> &RHS) {
    if (&RHS != this)
      this->copyFrom(SmallStorage, RHS);
    return *this;
  }

  /// Replace contents by moving from \p RHS.
  ///
  /// \param RHS Set to move-assign from.
  /// @return Reference to this set.
  SmallPtrSet<PtrType, SmallSize> &
  operator=(SmallPtrSet<PtrType, SmallSize> &&RHS) {
    if (&RHS != this)
      this->moveFrom(SmallStorage, SmallSizePowTwo, RHS.SmallStorage,
                     std::move(RHS));
    return *this;
  }

  /// Clear the set and insert each pointer in \p IL.
  ///
  /// \param IL Initializer list of pointers to assign.
  /// @return Reference to this set.
  SmallPtrSet<PtrType, SmallSize> &
  operator=(std::initializer_list<PtrType> IL) {
    this->clear();
    this->insert(IL.begin(), IL.end());
    return *this;
  }

  /// Swap the elements of this set with \p RHS.
  ///
  /// \param RHS Set to exchange contents with.
  void swap(SmallPtrSet<PtrType, SmallSize> &RHS) {
    SmallPtrSetImplBase::swap(SmallStorage, RHS.SmallStorage, RHS);
  }
};

} // namespace llvm

namespace std {

/// Implement std::swap in terms of SmallPtrSet swap.
template <class T, unsigned N>
inline void swap(llvm::SmallPtrSet<T, N> &LHS, llvm::SmallPtrSet<T, N> &RHS) {
  LHS.swap(RHS);
}

} // namespace std

#endif // LLVM_ADT_SMALLPTRSET_H
