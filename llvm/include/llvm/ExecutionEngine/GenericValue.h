//===- GenericValue.h - Represent any type of LLVM value --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The GenericValue class is used to represent an LLVM value of arbitrary type.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_GENERICVALUE_H
#define LLVM_EXECUTIONENGINE_GENERICVALUE_H

#include "llvm/ADT/APInt.h"
#include <vector>

namespace llvm {

/// Opaque pointer type used to store pointer values in a GenericValue.
using PointerTy = void *;

/// Holds an LLVM value of arbitrary type for the interpreter and related tools.
struct GenericValue {
  /// Pair of unsigned integers stored in the untyped union of GenericValue.
  struct IntPair {
    /// First element of the integer pair.
    unsigned int first;
    /// Second element of the integer pair.
    unsigned int second;
  };
  union {
    /// Value when the GenericValue holds a double.
    double DoubleVal;
    /// Value when the GenericValue holds a float.
    float FloatVal;
    /// Value when the GenericValue holds a pointer.
    PointerTy PointerVal;
    /// Value when the GenericValue holds a pair of unsigned integers.
    struct IntPair UIntPairVal;
    /// Raw bytes for values that do not fit the other union members.
    unsigned char Untyped[8];
  };
  /// Arbitrary-precision integer value; also used for long doubles.
  APInt IntVal;
  /// Nested values for aggregate data types such as structs and arrays.
  std::vector<GenericValue> AggregateVal;

  /// Construct a zero-initialized GenericValue.
  ///
  /// Zeroing could be omitted for speed, but leaving garbage in the union is
  /// potentially problematic, so the value is cleared.
  GenericValue() : IntVal(1, 0) {
    UIntPairVal.first = 0;
    UIntPairVal.second = 0;
  }
  /// Construct a GenericValue that holds the given pointer.
  /// @param V Pointer value to store.
  explicit GenericValue(void *V) : PointerVal(V), IntVal(1, 0) {}
};

/// Convert a pointer to a GenericValue that holds it.
/// @param P Pointer to wrap in a GenericValue.
/// @return GenericValue whose PointerVal is \p P.
inline GenericValue PTOGV(void *P) { return GenericValue(P); }
/// Extract the pointer stored in a GenericValue.
/// @param GV GenericValue whose PointerVal is returned.
/// @return The pointer stored in \p GV.
inline void *GVTOP(const GenericValue &GV) { return GV.PointerVal; }

} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_GENERICVALUE_H
