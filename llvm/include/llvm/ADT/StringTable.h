//===- StringTable.h - Table of strings tracked by offset ----------C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_STRING_TABLE_H
#define LLVM_ADT_STRING_TABLE_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"
#include <cassert>
#include <iterator>
#include <limits>

namespace llvm {

/// A table of densely packed, null-terminated strings indexed by offset.
///
/// This table abstracts a densely concatenated list of null-terminated strings,
/// each of which can be referenced using an offset into the table.
///
/// This requires and ensures that the string at offset 0 is also the empty
/// string. This helps allow zero-initialized offsets form empty strings and
/// avoids non-zero initialization when using a string literal pointer would
/// allow a null pointer.
///
/// The primary use case is having a single global string literal for the table
/// contents, and offsets into it in other global data structures to avoid
/// dynamic relocations of individual string literal pointers in those global
/// data structures.
class StringTable {
  StringRef Table;

public:
  /// Byte offset of a null-terminated string within the packed table.
  ///
  /// Typically produced by TableGen or another generator. Default construction
  /// yields offset zero, which always names the empty string.
  class Offset {
    // Note that we ensure the empty string is at offset zero.
    unsigned Value = 0;

  public:
    /// Construct an offset of zero (the empty string).
    constexpr Offset() = default;
    /// Construct an offset with the given byte index \p Value.
    constexpr Offset(unsigned Value) : Value(Value) {}

    /// Return true if \p LHS and \p RHS name the same table offset.
    friend constexpr bool operator==(const Offset &LHS, const Offset &RHS) {
      return LHS.Value == RHS.Value;
    }

    /// Return true if \p LHS and \p RHS name different table offsets.
    friend constexpr bool operator!=(const Offset &LHS, const Offset &RHS) {
      return LHS.Value != RHS.Value;
    }

    /// Return the raw byte offset into the table.
    constexpr unsigned value() const { return Value; }
  };

  /// Construct a table from a string literal or character array \p RawTable.
  ///
  /// Unlike \c StringLiteral, null bytes inside the payload are preserved; the
  /// array length \p N is the table size. Offset zero must be the empty string
  /// and the final byte must be null.
  template <size_t N>
  constexpr StringTable(const char (&RawTable)[N]) : Table(RawTable, N) {
    static_assert(N <= std::numeric_limits<unsigned>::max(),
                  "We only support table sizes that can be indexed by an "
                  "`unsigned` offset.");

    // Note that we can only use `empty`, `data`, and `size` in these asserts to
    // support `constexpr`.
    assert(!Table.empty() && "Requires at least a valid empty string.");
    assert(Table.data()[0] == '\0' && "Offset zero must be the empty string.");
    // Regardless of how many strings are in the table, the last one should also
    // be null terminated. This also ensures that computing `strlen` on the
    // strings can't accidentally run past the end of the table.
    assert(Table.data()[Table.size() - 1] == '\0' &&
           "Last byte must be a null byte.");
  }

  /// Return a pointer to the null-terminated C string at offset \p O.
  constexpr const char *getCString(Offset O) const {
    assert(O.value() < Table.size() && "Out of bounds offset!");
    return Table.data() + O.value();
  }

  /// Return the string at offset \p O as a \c StringRef (also null-terminated).
  constexpr StringRef operator[](Offset O) const { return getCString(O); }

  /// Returns the byte size of the table.
  constexpr size_t size() const { return Table.size(); }

  /// Forward iterator over the non-empty strings stored in the table.
  class Iterator
      : public iterator_facade_base<Iterator, std::forward_iterator_tag,
                                    const StringRef> {
    friend StringTable;

    const StringTable *Table;
    Offset O;

    // A cache of one value to allow `*` to return a reference.
    mutable StringRef S;

    explicit constexpr Iterator(const StringTable &Table, Offset O)
        : Table(&Table), O(O) {}

  public:
    /// Copy-construct an iterator.
    constexpr Iterator(const Iterator &RHS) = default;
    /// Move-construct an iterator.
    constexpr Iterator(Iterator &&RHS) = default;

    /// Copy-assign an iterator.
    constexpr Iterator &operator=(const Iterator &RHS) = default;
    /// Move-assign an iterator.
    constexpr Iterator &operator=(Iterator &&RHS) = default;

    /// Return true if both iterators point at the same offset in the same table.
    bool operator==(const Iterator &RHS) const {
      assert(Table == RHS.Table && "Compared iterators for unrelated tables!");
      return O == RHS.O;
    }

    /// Return a reference to the string at the current offset.
    const StringRef &operator*() const {
      S = (*Table)[O];
      return S;
    }

    /// Advance to the next null-terminated string in the table.
    Iterator &operator++() {
      O = O.value() + (*Table)[O].size() + 1;
      return *this;
    }

    /// Return the table offset of the string this iterator currently names.
    Offset offset() const { return O; }
  };

  /// Return an iterator to the first non-empty string (skipping offset zero).
  constexpr Iterator begin() const { return Iterator(*this, 1); }
  /// Return an iterator past the last string in the table.
  constexpr Iterator end() const { return Iterator(*this, size() - 1); }
};

} // namespace llvm

#endif // LLVM_ADT_STRING_TABLE_H
