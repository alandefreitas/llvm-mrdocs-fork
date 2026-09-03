//===- llvm/ADT/PriorityQueue.h - Priority queues ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the PriorityQueue class.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_PRIORITYQUEUE_H
#define LLVM_ADT_PRIORITYQUEUE_H

#include <algorithm>
#include <queue>

namespace llvm {

/// PriorityQueue - This class behaves like std::priority_queue and
/// provides a few additional convenience functions.
///
template<class T,
         class Sequence = std::vector<T>,
         class Compare = std::less<typename Sequence::value_type> >
class PriorityQueue : public std::priority_queue<T, Sequence, Compare> {
  using Base = std::priority_queue<T, Sequence, Compare>;

public:
  /// Type of elements stored in the queue.
  using value_type = typename Base::value_type;
  /// Mutable reference to an element.
  using reference = typename Base::reference;
  /// Const reference to an element.
  using const_reference = typename Base::const_reference;
  /// Unsigned type used for sizes.
  using size_type = typename Base::size_type;
  /// Underlying container type that holds the heap.
  using container_type = typename Base::container_type;
  /// Comparator type that defines heap ordering.
  using value_compare = typename Base::value_compare;

  /// Assign from another priority queue.
  ///
  /// @param Other Priority queue to copy from.
  /// @return A reference to this priority queue.
  PriorityQueue &operator=(Base const &Other) {
    Base::operator=(Other);
    return *this;
  }

  /// Assign from another priority queue.
  ///
  /// @param Other Priority queue to move from.
  /// @return A reference to this priority queue.
  PriorityQueue &operator=(Base &&Other) noexcept {
    Base::operator=(static_cast<Base &&>(Other));
    return *this;
  }

  /// Return true if the queue contains no elements.
  using Base::empty;
  /// Return the number of elements in the queue.
  using Base::size;
  /// Return a const reference to the greatest element.
  using Base::top;

  /// Insert a copy or moved-from value into the queue.
  ///
  /// @param Value Element to insert.
  void push(value_type const &Value) { Base::push(Value); }

  /// Insert a copy or moved-from value into the queue.
  ///
  /// @param Value Element to insert.
  void push(value_type &&Value) {
    Base::push(static_cast<value_type &&>(Value));
  }

  /// Construct an element in-place and insert it into the queue.
  using Base::emplace;
#if defined(_LIBCPP_STD_VER) ? (_LIBCPP_STD_VER >= 23) : (__cplusplus >= 202302L)
  /// Insert every element from a range into the queue.
  using Base::push_range;
#endif
  /// Remove the greatest element from the queue.
  using Base::pop;
  /// Exchange contents with another priority queue.
  using Base::swap;

#ifdef _LIBCPP_VERSION
  /// Return a const reference to the underlying container (libc++ extension).
  using Base::__get_container;
#endif

  /// Construct an empty queue with comparator \p compare and container
  /// \p sequence.
  /// @param compare Comparator used to order elements.
  /// @param sequence Initial underlying container (usually empty).
  explicit PriorityQueue(const Compare &compare = Compare(),
                         const Sequence &sequence = Sequence())
    : Base(compare, sequence)
  {}

  /// Construct a queue from the range [\p begin, \p end) with optional
  /// comparator and initial container.
  /// @param begin Iterator to the first element to insert.
  /// @param end Iterator past the last element to insert.
  /// @param compare Comparator used to order elements.
  /// @param sequence Initial underlying container to extend.
  template<class Iterator>
  PriorityQueue(Iterator begin, Iterator end,
                const Compare &compare = Compare(),
                const Sequence &sequence = Sequence())
    : Base(begin, end, compare, sequence)
  {}

  /// Erase one element from the queue, regardless of its position.
  ///
  /// This operation performs a linear search to find an element equal to t,
  /// but then uses all logarithmic-time algorithms to do the erase operation.
  /// @param t Element value to find and remove.
  void erase_one(const T &t) {
    // Linear-search to find the element.
    typename Sequence::size_type i = find(this->c, t) - this->c.begin();

    // Logarithmic-time heap bubble-up.
    while (i != 0) {
      typename Sequence::size_type parent = (i - 1) / 2;
      this->c[i] = this->c[parent];
      i = parent;
    }

    // The element we want to remove is now at the root, so we can use
    // priority_queue's plain pop to remove it.
    this->pop();
  }

  /// Rebuild the heap after an element's ordering relative to the comparator
  /// has changed.
  ///
  /// If an element in the queue has changed in a way that affects its standing
  /// in the comparison function, the queue's internal state becomes invalid.
  /// Calling reheapify() resets the queue's state, making it valid again. This
  /// operation has time complexity proportional to the number of elements in
  /// the queue, so don't plan to use it a lot.
  void reheapify() {
    std::make_heap(this->c.begin(), this->c.end(), this->comp);
  }

  /// clear - Erase all elements from the queue.
  ///
  void clear() {
    this->c.clear();
  }

protected:
  /// Underlying container that stores the heap elements.
  using Base::c;
  /// Comparator that defines the heap ordering.
  using Base::comp;
};

} // End llvm namespace

#endif
