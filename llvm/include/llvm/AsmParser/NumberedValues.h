//===-- NumberedValues.h - --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ASMPARSER_NUMBEREDVALUES_H
#define LLVM_ASMPARSER_NUMBEREDVALUES_H

#include "llvm/ADT/DenseMap.h"

namespace llvm {

/// Mapping from value ID to value, which also remembers what the next unused
/// ID is.
template <class T> class NumberedValues {
  DenseMap<unsigned, T> Vals;
  unsigned NextUnusedID = 0;

public:
  /// Return the next unused value ID.
  ///
  /// \return The next unused value ID.
  unsigned getNext() const { return NextUnusedID; }
  /// Look up the value associated with \p ID.
  ///
  /// \param ID Value ID to look up.
  /// \return The mapped value, or a default-constructed \c T if \p ID is
  /// absent.
  T get(unsigned ID) const { return Vals.lookup(ID); }
  /// Insert a value under \p ID and advance the next unused ID past it.
  ///
  /// \p ID must be at least as large as the current next unused ID.
  /// \param ID Value ID under which to store \p V.
  /// \param V Value to associate with \p ID.
  void add(unsigned ID, T V) {
    assert(ID >= NextUnusedID && "Invalid value ID");
    Vals.insert({ID, V});
    NextUnusedID = ID + 1;
  }
};

} // end namespace llvm

#endif
