//===- DebugStringTableSubsection.h - CodeView String Table -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_DEBUGSTRINGTABLESUBSECTION_H
#define LLVM_DEBUGINFO_CODEVIEW_DEBUGSTRINGTABLESUBSECTION_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/DebugSubsection.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <cstdint>

namespace llvm {

class BinaryStreamReader;

namespace codeview {

/// Read-only view of a CodeView string table.
///
/// This is a very simple flat buffer consisting of null-terminated strings,
/// where strings are retrieved by their offset in the buffer.
/// DebugStringTableSubsectionRef does not own the underlying storage for the
/// buffer.
class DebugStringTableSubsectionRef : public DebugSubsectionRef {
public:
  /// Construct an empty, uninitialized string table subsection reference.
  LLVM_ABI DebugStringTableSubsectionRef();

  /// Return true if \p S is a StringTable subsection reference.
  ///
  /// \param S Subsection reference to test.
  ///
  /// \returns True if \p S is a StringTable subsection reference.
  static bool classof(const DebugSubsectionRef *S) {
    return S->kind() == DebugSubsectionKind::StringTable;
  }

  /// Initialize this view from the string table bytes in \p Contents.
  ///
  /// \param Contents Stream containing the serialized string table.
  ///
  /// \returns An Error on failure, or success if initialization succeeded.
  LLVM_ABI Error initialize(BinaryStreamRef Contents);
  /// Initialize this view from string table bytes read via \p Reader.
  ///
  /// \param Reader Reader positioned at the start of the string table data.
  ///
  /// \returns An Error on failure, or success if initialization succeeded.
  LLVM_ABI Error initialize(BinaryStreamReader &Reader);

  /// Return the null-terminated string at byte offset \p Offset.
  ///
  /// \param Offset Byte offset of the string within the table buffer.
  ///
  /// \returns The string at \p Offset, or an Error if the offset is invalid.
  LLVM_ABI Expected<StringRef> getString(uint32_t Offset) const;

  /// Return true if the underlying string table stream has been initialized.
  ///
  /// \returns True if the string table stream has been initialized.
  bool valid() const { return Stream.valid(); }

  /// Return the underlying buffer of serialized string table bytes.
  ///
  /// \returns The binary stream containing the serialized string table.
  BinaryStreamRef getBuffer() const { return Stream; }

private:
  BinaryStreamRef Stream;
};

/// Writable CodeView string table debug subsection.
///
/// DebugStringTableSubsection owns the underlying storage for the table, and is
/// capable of serializing the string table into a format understood by
/// DebugStringTableSubsectionRef.
class LLVM_ABI DebugStringTableSubsection : public DebugSubsection {
public:
  /// Construct an empty writable string table subsection.
  DebugStringTableSubsection();

  /// Return true if \p S is a StringTable subsection.
  ///
  /// \param S Subsection to test.
  ///
  /// \returns True if \p S is a StringTable subsection.
  static bool classof(const DebugSubsection *S) {
    return S->kind() == DebugSubsectionKind::StringTable;
  }

  /// Insert \p S into the string table if it is not already present.
  ///
  /// \param S String to intern in the table.
  ///
  /// \returns The ID (byte offset) for \p S.
  uint32_t insert(StringRef S);

  /// Return the ID for string \p S.
  ///
  /// Assumes \p S already exists in the table.
  ///
  /// \param S String whose ID is requested.
  ///
  /// \returns The ID (byte offset) for \p S.
  uint32_t getIdForString(StringRef S) const;

  /// Return the string associated with ID \p Id.
  ///
  /// \param Id String ID (byte offset) previously returned by insert or
  /// getIdForString.
  ///
  /// \returns The string for \p Id.
  StringRef getStringForId(uint32_t Id) const;

  /// Return the serialized size of this subsection in bytes.
  ///
  /// \returns The number of bytes required to serialize this subsection.
  uint32_t calculateSerializedSize() const override;
  /// Write this subsection's serialized form to \p Writer.
  ///
  /// \param Writer Destination stream writer.
  ///
  /// \returns An Error on failure, or success if the write completed.
  Error commit(BinaryStreamWriter &Writer) const override;

  /// Return the number of distinct strings in the table.
  ///
  /// \returns The number of distinct strings currently in the table.
  uint32_t size() const;

  /// Return an iterator to the first string-to-ID mapping.
  ///
  /// \returns A const iterator to the beginning of the string-to-ID map.
  StringMap<uint32_t>::const_iterator begin() const {
    return StringToId.begin();
  }

  /// Return an iterator past the last string-to-ID mapping.
  ///
  /// \returns A const iterator to the end of the string-to-ID map.
  StringMap<uint32_t>::const_iterator end() const { return StringToId.end(); }

  /// Return all string IDs sorted in ascending order.
  ///
  /// \returns A vector of string IDs in ascending order.
  std::vector<uint32_t> sortedIds() const;

private:
  DenseMap<uint32_t, StringRef> IdToString;
  StringMap<uint32_t> StringToId;
  uint32_t StringSize = 1;
};

} // end namespace codeview

} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_DEBUGSTRINGTABLESUBSECTION_H
