//===- StringSet.h - An efficient set built on StringMap --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
///  StringSet - A set-like wrapper for the StringMap.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_STRINGSET_H
#define LLVM_ADT_STRINGSET_H

#include "llvm/ADT/ADL.h"
#include "llvm/ADT/STLForwardCompat.h"
#include "llvm/ADT/StringMap.h"

namespace llvm {

/// StringSet - A wrapper for StringMap that provides set-like functionality.
template <class AllocatorTy = MallocAllocator>
class StringSet : public StringMap<EmptyStringSetTag, AllocatorTy> {
  using Base = StringMap<EmptyStringSetTag, AllocatorTy>;

public:
  /// Construct an empty string set.
  StringSet() = default;
  /// Construct a set containing each string in \p initializer.
  /// @param initializer Strings to insert.
  StringSet(std::initializer_list<StringRef> initializer) {
    for (StringRef str : initializer)
      insert(str);
  }
  /// Construct a set from the elements of range \p R.
  /// @param R Range of StringRef-convertible values to insert.
  template <typename Range> StringSet(llvm::from_range_t, Range &&R) {
    insert(adl_begin(R), adl_end(R));
  }
  /// Construct an empty set that allocates with \p a.
  /// @param a Allocator used by the underlying StringMap.
  explicit StringSet(AllocatorTy a) : Base(a) {}

  /// Insert \p key if it is not already present.
  /// @param key String to insert.
  /// @return Pair of iterator to the element and whether insertion occurred.
  std::pair<typename Base::iterator, bool> insert(StringRef key) {
    return Base::try_emplace(key);
  }

  /// Insert each string in the half-open iterator range [\p begin, \p end).
  /// @param begin Iterator to the first string.
  /// @param end Iterator past the last string.
  template <typename InputIt>
  void insert(InputIt begin, InputIt end) {
    for (auto it = begin; it != end; ++it)
      insert(*it);
  }

  /// Insert each string from range \p R.
  /// @param R Range of StringRef-convertible values.
  template <typename Range> void insert_range(Range &&R) {
    insert(adl_begin(R), adl_end(R));
  }

  /// Insert the key from a StringMapEntry of any mapped type.
  /// @param mapEntry Entry whose key is inserted into this set.
  /// @return Pair of iterator to the element and whether insertion occurred.
  template <typename ValueTy>
  std::pair<typename Base::iterator, bool>
  insert(const StringMapEntry<ValueTy> &mapEntry) {
    return insert(mapEntry.getKey());
  }

  /// Check if the set contains the given \c key.
  [[nodiscard]] bool contains(StringRef key) const {
    return Base::contains(key);
  }
};

} // end namespace llvm

#endif // LLVM_ADT_STRINGSET_H
