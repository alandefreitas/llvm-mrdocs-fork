//===- TpiHashing.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_TPIHASHING_H
#define LLVM_DEBUGINFO_PDB_NATIVE_TPIHASHING_H

#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace pdb {

/// Compute the TPI hash for a CodeView type record.
///
/// \param Type The CodeView type record whose TPI hash is requested.
///
/// \returns The 32-bit TPI hash of \p Type, or an error on failure.
LLVM_ABI Expected<uint32_t> hashTypeRecord(const llvm::codeview::CVType &Type);

/// Holds the full-declaration and forward-declaration hashes of a tag record.
struct TagRecordHash {
  /// Construct a hash pair for a class, structure, or interface record.
  ///
  /// \param CR The deserialized class/structure/interface record.
  /// \param Full The hash of the full declaration for this tag.
  /// \param Forward The hash of the forward declaration for this tag.
  explicit TagRecordHash(codeview::ClassRecord CR, uint32_t Full,
                         uint32_t Forward)
      : FullRecordHash(Full), ForwardDeclHash(Forward), Class(std::move(CR)) {
    State = 0;
  }

  /// Construct a hash pair for an enumeration record.
  ///
  /// \param ER The deserialized enumeration record.
  /// \param Full The hash of the full declaration for this tag.
  /// \param Forward The hash of the forward declaration for this tag.
  explicit TagRecordHash(codeview::EnumRecord ER, uint32_t Full,
                         uint32_t Forward)
      : FullRecordHash(Full), ForwardDeclHash(Forward), Enum(std::move(ER)) {
    State = 1;
  }

  /// Construct a hash pair for a union record.
  ///
  /// \param UR The deserialized union record.
  /// \param Full The hash of the full declaration for this tag.
  /// \param Forward The hash of the forward declaration for this tag.
  explicit TagRecordHash(codeview::UnionRecord UR, uint32_t Full,
                         uint32_t Forward)
      : FullRecordHash(Full), ForwardDeclHash(Forward), Union(std::move(UR)) {
    State = 2;
  }

  /// Hash of the full (non-forward) declaration of this tag record.
  uint32_t FullRecordHash;
  /// Hash of the forward declaration of this tag record.
  uint32_t ForwardDeclHash;

  /// Return the stored class, enum, or union record as a TagRecord.
  ///
  /// \returns A reference to the active class, enum, or union TagRecord.
  codeview::TagRecord &getRecord() {
    switch (State) {
    case 0:
      return Class;
    case 1:
      return Enum;
    case 2:
      return Union;
    }
    llvm_unreachable("unreachable!");
  }

private:
  union {
    /// Class, structure, or interface record when \c State is 0.
    codeview::ClassRecord Class;
    /// Enumeration record when \c State is 1.
    codeview::EnumRecord Enum;
    /// Union record when \c State is 2.
    codeview::UnionRecord Union;
  };

  uint8_t State = 0;
};

/// Given a CVType referring to a class, structure, union, or enum, compute
/// the hash of its forward decl and full decl.
///
/// \param Type The class, structure, union, or enum type record to hash.
///
/// \returns The forward-decl and full-decl hashes for the tag record.
LLVM_ABI Expected<TagRecordHash> hashTagRecord(const codeview::CVType &Type);

} // end namespace pdb
} // end namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_TPIHASHING_H
