//===--- Capacity.h - Generic computation of ADT memory use -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the capacity function that computes the amount of
// memory used by an ADT.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_CAPACITY_H
#define LLVM_SUPPORT_CAPACITY_H

#include <cstddef>

namespace llvm {

/// Return the memory capacity of \p x in bytes.
///
/// This default definition works for containers like \c std::vector that
/// expose \c capacity() and \c value_type. More specialized overloads handle
/// other ADTs.
///
/// \tparam T Container type with \c capacity() and \c value_type.
/// \param x Container whose allocated capacity is measured.
/// \return Capacity of \p x in bytes.
template <typename T>
static inline size_t capacity_in_bytes(const T &x) {
  // This default definition of capacity should work for things like std::vector
  // and friends.  More specialized versions will work for others.
  return x.capacity() * sizeof(typename T::value_type);
}

} // end namespace llvm

#endif

