//===- IndexedValuesMap.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DWARFLINKER_INDEXEDVALUESMAP_H
#define LLVM_DWARFLINKER_INDEXEDVALUESMAP_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include <cstdint>

namespace llvm {
namespace dwarf_linker {

/// This class stores values sequentually and assigns index to the each value.
template <typename T> class IndexedValuesMap {
public:
  /// Inserts \p Value if new and returns its assigned index.
  ///
  /// \param Value Value to look up or insert.
  /// \returns Index of \p Value in the sequential storage.
  uint64_t getValueIndex(T Value) {
    auto [It, Inserted] = ValueToIndexMap.try_emplace(Value, Values.size());
    if (Inserted)
      Values.push_back(Value);
    return It->second;
  }

  /// Returns the sequential array of stored values.
  ///
  /// \returns Reference to the sequential array of stored values.
  const SmallVector<T> &getValues() const { return Values; }

  /// Erases all values and index mappings.
  void clear() {
    ValueToIndexMap.clear();
    Values.clear();
  }

  /// Returns true if no values are stored.
  ///
  /// \returns True if no values are stored.
  bool empty() { return Values.empty(); }

protected:
  /// Dense map from a value to its assigned index.
  using ValueToIndexMapTy = DenseMap<T, uint64_t>;
  /// Map from each stored value to its index.
  ValueToIndexMapTy ValueToIndexMap;
  /// Sequential storage of unique values in insertion order.
  SmallVector<T> Values;
};

} // end of namespace dwarf_linker
} // end of namespace llvm

#endif // LLVM_DWARFLINKER_INDEXEDVALUESMAP_H
