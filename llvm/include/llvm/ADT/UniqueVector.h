//===- llvm/ADT/UniqueVector.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_UNIQUEVECTOR_H
#define LLVM_ADT_UNIQUEVECTOR_H

#include <cassert>
#include <cstddef>
#include <map>
#include <vector>

namespace llvm {

//===----------------------------------------------------------------------===//
/// Vector that assigns a sequential base-1 ID to each unique entry.
///
/// T is the type of entries in the vector. T should have implementations of
/// operator== and of operator<. Entries can be fetched using operator[] with
/// the entry ID.
template<class T> class UniqueVector {
public:
  /// Underlying contiguous storage of unique entries in insertion order.
  using VectorType = typename std::vector<T>;
  /// Mutable iterator over the stored entries.
  using iterator = typename VectorType::iterator;
  /// Const iterator over the stored entries.
  using const_iterator = typename VectorType::const_iterator;

private:
  // Map - Used to handle the correspondence of entry to ID.
  std::map<T, unsigned> Map;

  // Vector - ID ordered vector of entries. Entries can be indexed by ID - 1.
  VectorType Vector;

public:
  /// insert - Append entry to the vector if it doesn't already exist.  Returns
  /// the entry's index + 1 to be used as a unique ID.
  /// @param Entry The entry to insert.
  /// @return The entry's 1-based ID (existing or newly assigned).
  unsigned insert(const T &Entry) {
    // Check if the entry is already in the map.
    unsigned &Val = Map[Entry];

    // See if entry exists, if so return prior ID.
    if (Val) return Val;

    // Compute ID for entry.
    Val = static_cast<unsigned>(Vector.size()) + 1;

    // Insert in vector.
    Vector.push_back(Entry);
    return Val;
  }

  /// idFor - return the ID for an existing entry.  Returns 0 if the entry is
  /// not found.
  /// @param Entry The entry to look up.
  /// @return The entry's 1-based ID, or 0 if the entry is not found.
  unsigned idFor(const T &Entry) const {
    // Search for entry in the map.
    typename std::map<T, unsigned>::const_iterator MI = Map.find(Entry);

    // See if entry exists, if so return ID.
    if (MI != Map.end()) return MI->second;

    // No luck.
    return 0;
  }

  /// operator[] - Returns a reference to the entry with the specified ID.
  /// @param ID The 1-based ID of the entry to retrieve.
  /// @return A const reference to the entry with the given ID.
  const T &operator[](unsigned ID) const {
    assert(ID-1 < size() && "ID is 0 or out of range!");
    return Vector[ID - 1];
  }

  /// Return an iterator to the start of the vector.
  /// @return An iterator to the first entry.
  iterator begin() { return Vector.begin(); }

  /// Return an iterator to the start of the vector.
  /// @return A const iterator to the first entry.
  const_iterator begin() const { return Vector.begin(); }

  /// Return an iterator to the end of the vector.
  /// @return An iterator past the last entry.
  iterator end() { return Vector.end(); }

  /// Return an iterator to the end of the vector.
  /// @return A const iterator past the last entry.
  const_iterator end() const { return Vector.end(); }

  /// size - Returns the number of entries in the vector.
  /// @return The number of unique entries stored.
  size_t size() const { return Vector.size(); }

  /// empty - Returns true if the vector is empty.
  /// @return True if the vector contains no entries.
  bool empty() const { return Vector.empty(); }

  /// reset - Clears all the entries.
  void reset() {
    Map.clear();
    Vector.resize(0, 0);
  }
};

} // end namespace llvm

#endif // LLVM_ADT_UNIQUEVECTOR_H
