//===- NonRelocatableStringpool.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_NONRELOCATABLESTRINGPOOL_H
#define LLVM_CODEGEN_NONRELOCATABLESTRINGPOOL_H

#include "llvm/CodeGen/DwarfStringPoolEntry.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>
#include <vector>

namespace llvm {

/// A string table that doesn't need relocations.
///
/// Use this class when a string table doesn't need relocations.
/// This class provides this ability by just associating offsets with strings.
class NonRelocatableStringpool {
public:
  /// Entries are stored into the StringMap and simply linked together through
  /// the second element of this pair in order to keep track of insertion
  /// order.
  using MapTy = StringMap<DwarfStringPoolEntry, BumpPtrAllocator>;

  /// Construct a string pool, optionally inserting the empty string first.
  ///
  /// \param PutEmptyString If true, insert the empty string as the first entry.
  NonRelocatableStringpool(bool PutEmptyString = false) {
    if (PutEmptyString)
      getEntry("");
  }

  /// Get or create a pool entry for string \p S.
  ///
  /// \param S The string to look up or insert.
  /// \returns A reference to the corresponding string pool entry.
  LLVM_ABI DwarfStringPoolEntryRef getEntry(StringRef S);

  /// Get the offset of string \p S in the string table. This can insert a new
  /// element or return the offset of a pre-existing one.
  ///
  /// \param S The string whose offset is requested.
  /// \returns The offset of \p S in the string table.
  uint64_t getStringOffset(StringRef S) { return getEntry(S).getOffset(); }

  /// Get permanent storage for \p S (but do not necessarily emit \p S in the
  /// output section). A latter call to getStringOffset() with the same string
  /// will chain it though.
  ///
  /// \param S The string to intern into permanent storage.
  /// \returns The StringRef that points to permanent storage to use
  /// in place of \p S.
  LLVM_ABI StringRef internString(StringRef S);

  /// Return the current size of the string pool in bytes.
  ///
  /// \returns The current size of the string pool in bytes.
  uint64_t getSize() { return CurrentEndOffset; }

  /// Return the list of strings to be emitted. This does not contain the
  /// strings which were added via internString only.
  ///
  /// \returns The list of strings to be emitted.
  LLVM_ABI std::vector<DwarfStringPoolEntryRef> getEntriesForEmission() const;

private:
  MapTy Strings;
  uint64_t CurrentEndOffset = 0;
  unsigned NumEntries = 0;
};

/// Helper for making strong types.
template <typename T, typename S> class StrongType : public T {
public:
  /// Construct a StrongType by forwarding arguments to the base type.
  ///
  /// \param A Arguments forwarded to the constructor of \c T.
  template <typename... Args>
  explicit StrongType(Args... A) : T(std::forward<Args>(A)...) {}
};

/// It's very easy to introduce bugs by passing the wrong string pool.
/// By using strong types the interface enforces that the right
/// kind of pool is used.
struct UniqueTag {};
/// Tag type that distinguishes offset-based string pools from uniquing pools.
struct OffsetsTag {};
/// Strongly typed non-relocatable string pool for uniquing strings.
using UniquingStringPool = StrongType<NonRelocatableStringpool, UniqueTag>;
/// Strongly typed non-relocatable string pool for offset lookup.
using OffsetsStringPool = StrongType<NonRelocatableStringpool, OffsetsTag>;

} // end namespace llvm

#endif // LLVM_CODEGEN_NONRELOCATABLESTRINGPOOL_H
