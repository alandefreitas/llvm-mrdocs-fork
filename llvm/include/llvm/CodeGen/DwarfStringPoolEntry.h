//===- llvm/CodeGen/DwarfStringPoolEntry.h - String pool entry --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_DWARFSTRINGPOOLENTRY_H
#define LLVM_CODEGEN_DWARFSTRINGPOOLENTRY_H

#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/StringMap.h"

namespace llvm {

class MCSymbol;

/// Data for a string pool entry.
struct DwarfStringPoolEntry {
  /// Sentinel Index value meaning the string is not indexed in the string pool.
  static constexpr unsigned NotIndexed = -1;

  /// Optional MCSymbol associated with this string pool entry.
  MCSymbol *Symbol = nullptr;
  /// Byte offset of this string within the string pool.
  uint64_t Offset = 0;
  /// Index of this string in the string pool, or NotIndexed if unindexed.
  unsigned Index = 0;

  /// Returns true if this entry has a valid string pool index.
  ///
  /// \returns True if this entry has a valid string pool index.
  bool isIndexed() const { return Index != NotIndexed; }
};

/// DwarfStringPoolEntry with string keeping externally.
struct DwarfStringPoolEntryWithExtString : public DwarfStringPoolEntry {
  /// The externally-owned string value for this pool entry.
  StringRef String;
};

/// DwarfStringPoolEntryRef: Dwarf string pool entry reference.
///
/// Dwarf string pool entry keeps string value and its data.
/// There are two variants how data are represented:
///
///   1. String data in pool  - StringMapEntry<DwarfStringPoolEntry>.
///   2. External string data - DwarfStringPoolEntryWithExtString.
///
/// The external data variant allows reducing memory usage for the case
/// when string pool entry does not have data: string entry does not
/// keep any data and so no need to waste space for the full
/// DwarfStringPoolEntry. It is recommended to use external variant if not all
/// entries of dwarf string pool have corresponding DwarfStringPoolEntry.

class DwarfStringPoolEntryRef {
  /// Pointer type for "By value" string entry.
  using ByValStringEntryPtr = const StringMapEntry<DwarfStringPoolEntry> *;

  /// Pointer type for external string entry.
  using ExtStringEntryPtr = const DwarfStringPoolEntryWithExtString *;

  /// Pointer to the dwarf string pool Entry.
  PointerUnion<ByValStringEntryPtr, ExtStringEntryPtr> MapEntry = nullptr;

public:
  /// Construct an empty dwarf string pool entry reference.
  DwarfStringPoolEntryRef() = default;

  /// Construct a reference to a by-value string map entry.
  ///
  /// ASSUMPTION: DwarfStringPoolEntryRef keeps pointer to \p Entry,
  /// thus specified entry mustn`t be reallocated.
  /// \param Entry The string map entry to reference.
  DwarfStringPoolEntryRef(const StringMapEntry<DwarfStringPoolEntry> &Entry)
      : MapEntry(&Entry) {}

  /// Construct a reference to an externally-owned string pool entry.
  ///
  /// ASSUMPTION: DwarfStringPoolEntryRef keeps pointer to \p Entry,
  /// thus specified entry mustn`t be reallocated.
  /// \param Entry The externally-owned string pool entry to reference.
  DwarfStringPoolEntryRef(const DwarfStringPoolEntryWithExtString &Entry)
      : MapEntry(&Entry) {}

  /// Returns true if this reference refers to a valid string pool entry.
  ///
  /// \returns True if this reference refers to a valid string pool entry.
  explicit operator bool() const { return !MapEntry.isNull(); }

  /// Returns the symbol for the dwarf string.
  ///
  /// \returns Symbol for the dwarf string.
  MCSymbol *getSymbol() const {
    assert(getEntry().Symbol && "No symbol available!");
    return getEntry().Symbol;
  }

  /// Returns the offset for the dwarf string.
  ///
  /// \returns Offset for the dwarf string.
  uint64_t getOffset() const { return getEntry().Offset; }

  /// Returns the index for the dwarf string.
  ///
  /// \returns Index for the dwarf string.
  unsigned getIndex() const {
    assert(getEntry().isIndexed() && "Index is not set!");
    return getEntry().Index;
  }

  /// Returns the string value for this pool entry.
  ///
  /// \returns The string value.
  StringRef getString() const {
    if (isa<ByValStringEntryPtr>(MapEntry))
      return cast<ByValStringEntryPtr>(MapEntry)->first();

    return cast<ExtStringEntryPtr>(MapEntry)->String;
  }

  /// Returns the entire string pool entry for convenience.
  ///
  /// \returns The entire string pool entry.
  const DwarfStringPoolEntry &getEntry() const {
    if (isa<ByValStringEntryPtr>(MapEntry))
      return cast<ByValStringEntryPtr>(MapEntry)->second;

    return *cast<ExtStringEntryPtr>(MapEntry);
  }

  /// Returns true if this reference and \p X refer to the same pool entry.
  ///
  /// \param X The other entry reference to compare against.
  /// \returns True if the references refer to the same pool entry.
  bool operator==(const DwarfStringPoolEntryRef &X) const {
    return MapEntry.getOpaqueValue() == X.MapEntry.getOpaqueValue();
  }

  /// Returns true if this reference and \p X refer to different pool entries.
  ///
  /// \param X The other entry reference to compare against.
  /// \returns True if the references refer to different pool entries.
  bool operator!=(const DwarfStringPoolEntryRef &X) const {
    return MapEntry.getOpaqueValue() != X.MapEntry.getOpaqueValue();
  }
};

} // end namespace llvm

#endif
