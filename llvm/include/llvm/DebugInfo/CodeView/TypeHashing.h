//===- TypeHashing.h ---------------------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_TYPEHASHING_H
#define LLVM_DEBUGINFO_CODEVIEW_TYPEHASHING_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"

#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/TypeCollection.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"

#include "llvm/Support/FormatProviders.h"

#include <type_traits>

namespace llvm {
class raw_ostream;
namespace codeview {

/// A hash of a serialized type record for de-duplication within one stream.
///
/// The record is simply serialized, and then the bytes are hashed by a
/// standard algorithm. This is sufficient for de-duplicating records within a
/// single sequence of types, because if two records both have a back-reference
/// to the same type in the same stream, they will both have the same numeric
/// value for the TypeIndex of the back reference.
struct LocallyHashedType {
  /// Hash of the serialized record bytes.
  hash_code Hash;
  /// Serialized type record bytes that were hashed.
  ArrayRef<uint8_t> RecordData;

  /// Given a type, compute its local hash.
  ///
  /// \param RecordData Serialized type record bytes to hash.
  /// \returns The locally hashed type for \p RecordData.
  LLVM_ABI static LocallyHashedType hashType(ArrayRef<uint8_t> RecordData);

  /// Given a sequence of types, compute all of the local hashes.
  ///
  /// \param Records Range of serialized type records to hash.
  /// \returns A vector of local hashes, one per record in \p Records.
  template <typename Range>
  static std::vector<LocallyHashedType> hashTypes(Range &&Records) {
    std::vector<LocallyHashedType> Hashes;
    Hashes.reserve(std::distance(std::begin(Records), std::end(Records)));
    for (const auto &R : Records)
      Hashes.push_back(hashType(R));

    return Hashes;
  }

  /// Compute local hashes for every record in \p Types.
  ///
  /// \param Types Type collection whose records are hashed.
  /// \returns A vector of local hashes, one per record in \p Types.
  static std::vector<LocallyHashedType>
  hashTypeCollection(TypeCollection &Types) {
    std::vector<LocallyHashedType> Hashes;
    Types.ForEachRecord([&Hashes](TypeIndex TI, const CVType &Type) {
      Hashes.push_back(hashType(Type.RecordData));
    });
    return Hashes;
  }
};

/// Algorithm used to compute a globally unique type hash.
enum class GlobalTypeHashAlg : uint16_t {
  SHA1 = 0, ///< Standard 20-byte SHA-1 hash.
  SHA1_8,   ///< Last 8 bytes of a standard SHA-1 hash.
  BLAKE3,   ///< Truncated 8-byte BLAKE3 hash.
};

/// A hash that uniquely identifies a type record across multiple type streams.
///
/// For any given record A which references B, the TypeIndex that refers to B
/// is replaced with a previously-computed global hash for B. As this is a
/// recursive algorithm (e.g. the global hash of B also depends on the global
/// hashes of the types that B refers to), a global hash can uniquely identify
/// that A occurs in another stream that has a completely different graph
/// structure. Although the hash itself is slower to compute, probing is much
/// faster with a globally hashed type, because the hash itself is considered
/// "as good as" the original type. Since type records can be quite large, this
/// makes the equality comparison of the hash much faster than equality
/// comparison of a full record.
struct GloballyHashedType {
  /// Construct an empty globally hashed type.
  GloballyHashedType() = default;
  /// Construct a globally hashed type from the 8-byte hash in \p H.
  ///
  /// \param H Hash bytes as a string; must be 8 bytes.
  GloballyHashedType(StringRef H)
      : GloballyHashedType(ArrayRef<uint8_t>(H.bytes_begin(), H.bytes_end())) {}
  /// Construct a globally hashed type from the 8-byte hash in \p H.
  ///
  /// \param H Hash bytes; must be exactly 8 bytes.
  GloballyHashedType(ArrayRef<uint8_t> H) {
    assert(H.size() == 8);
    ::memcpy(Hash.data(), H.data(), 8);
  }
  /// Eight-byte global hash of the type record.
  std::array<uint8_t, 8> Hash;

  /// Return true if this hash is all zeros.
  ///
  /// \returns True if the hash is all zeros.
  bool empty() const { return *(const uint64_t*)Hash.data() == 0; }

  /// Return true if \p L and \p R have the same hash bytes.
  ///
  /// \param L Left-hand globally hashed type.
  /// \param R Right-hand globally hashed type.
  /// \returns True if the hash bytes are equal.
  friend inline bool operator==(const GloballyHashedType &L,
                                const GloballyHashedType &R) {
    return L.Hash == R.Hash;
  }

  /// Return true if \p L and \p R have different hash bytes.
  ///
  /// \param L Left-hand globally hashed type.
  /// \param R Right-hand globally hashed type.
  /// \returns True if the hash bytes differ.
  friend inline bool operator!=(const GloballyHashedType &L,
                                const GloballyHashedType &R) {
    return !(L.Hash == R.Hash);
  }

  /// Compute a global hash for the serialized record in \p RecordData.
  ///
  /// Due to the nature of global hashes incorporating the hashes of referenced
  /// records, this function requires a list of types and ids that RecordData
  /// might reference, indexable by TypeIndex.
  ///
  /// \param RecordData Serialized type or ID record bytes.
  /// \param PreviousTypes Global hashes of type records, indexed by TypeIndex.
  /// \param PreviousIds Global hashes of ID records, indexed by TypeIndex.
  /// \returns The computed global hash for \p RecordData.
  LLVM_ABI static GloballyHashedType
  hashType(ArrayRef<uint8_t> RecordData,
           ArrayRef<GloballyHashedType> PreviousTypes,
           ArrayRef<GloballyHashedType> PreviousIds);

  /// Compute a global hash for the CodeView type record \p Type.
  ///
  /// Due to the nature of global hashes incorporating the hashes of referenced
  /// records, this function requires a list of types and ids that the record
  /// might reference, indexable by TypeIndex.
  ///
  /// \param Type CodeView type record to hash.
  /// \param PreviousTypes Global hashes of type records, indexed by TypeIndex.
  /// \param PreviousIds Global hashes of ID records, indexed by TypeIndex.
  /// \returns The computed global hash for \p Type.
  static GloballyHashedType hashType(CVType Type,
                                     ArrayRef<GloballyHashedType> PreviousTypes,
                                     ArrayRef<GloballyHashedType> PreviousIds) {
    return hashType(Type.RecordData, PreviousTypes, PreviousIds);
  }

  /// Given a sequence of combined type and ID records, compute global hashes
  /// for each of them, returning the results in a vector of hashed types.
  ///
  /// \param Records Range of combined type and ID records to hash.
  /// \returns A vector of global hashes, one per record in \p Records.
  template <typename Range>
  static std::vector<GloballyHashedType> hashTypes(Range &&Records) {
    std::vector<GloballyHashedType> Hashes;
    bool UnresolvedRecords = false;
    for (const auto &R : Records) {
      GloballyHashedType H = hashType(R, Hashes, Hashes);
      if (H.empty())
        UnresolvedRecords = true;
      Hashes.push_back(H);
    }

    // In some rare cases, there might be records with forward references in the
    // stream. Several passes might be needed to fully hash each record in the
    // Type stream. However this occurs on very small OBJs generated by MASM,
    // with a dozen records at most. Therefore this codepath isn't
    // time-critical, as it isn't taken in 99% of cases.
    while (UnresolvedRecords) {
      UnresolvedRecords = false;
      auto HashIt = Hashes.begin();
      for (const auto &R : Records) {
        if (HashIt->empty()) {
          GloballyHashedType H = hashType(R, Hashes, Hashes);
          if (H.empty())
            UnresolvedRecords = true;
          else
            *HashIt = H;
        }
        ++HashIt;
      }
    }

    return Hashes;
  }

  /// Given a sequence of combined type and ID records, compute global hashes
  /// for each of them, returning the results in a vector of hashed types.
  ///
  /// \param Records Range of ID records to hash.
  /// \param TypeHashes Global hashes of type records that IDs may reference.
  /// \returns A vector of global hashes, one per ID record in \p Records.
  template <typename Range>
  static std::vector<GloballyHashedType>
  hashIds(Range &&Records, ArrayRef<GloballyHashedType> TypeHashes) {
    std::vector<GloballyHashedType> IdHashes;
    for (const auto &R : Records)
      IdHashes.push_back(hashType(R, TypeHashes, IdHashes));

    return IdHashes;
  }

  /// Compute global hashes for every record in \p Types.
  ///
  /// \param Types Type collection whose records are hashed.
  /// \returns A vector of global hashes, one per record in \p Types.
  static std::vector<GloballyHashedType>
  hashTypeCollection(TypeCollection &Types) {
    std::vector<GloballyHashedType> Hashes;
    Types.ForEachRecord([&Hashes](TypeIndex TI, const CVType &Type) {
      Hashes.push_back(hashType(Type.RecordData, Hashes, Hashes));
    });
    return Hashes;
  }
};
static_assert(std::is_trivially_copyable<GloballyHashedType>::value,
              "GloballyHashedType must be trivially copyable so that we can "
              "reinterpret_cast arrays of hash data to arrays of "
              "GloballyHashedType");
} // namespace codeview

/// DenseMapInfo specialization for locally hashed types.
template <> struct DenseMapInfo<codeview::LocallyHashedType> {
  /// Empty-key sentinel used by DenseMap.
  LLVM_ABI static codeview::LocallyHashedType Empty;

  /// Return the precomputed local hash of \p Val.
  ///
  /// \param Val Locally hashed type whose hash to return.
  /// \returns The precomputed local hash value.
  static unsigned getHashValue(codeview::LocallyHashedType Val) {
    return Val.Hash;
  }

  /// Return true if \p LHS and \p RHS have equal hashes and record bytes.
  ///
  /// \param LHS Left-hand locally hashed type.
  /// \param RHS Right-hand locally hashed type.
  /// \returns True if both the hashes and record bytes are equal.
  static bool isEqual(codeview::LocallyHashedType LHS,
                      codeview::LocallyHashedType RHS) {
    if (LHS.Hash != RHS.Hash)
      return false;
    return LHS.RecordData == RHS.RecordData;
  }
};

/// DenseMapInfo specialization for globally hashed types.
template <> struct DenseMapInfo<codeview::GloballyHashedType> {
  /// Empty-key sentinel used by DenseMap.
  LLVM_ABI static codeview::GloballyHashedType Empty;

  /// Return a hash of the global hash bytes in \p Val.
  ///
  /// \param Val Globally hashed type whose bytes to hash.
  /// \returns An unsigned hash derived from the global hash bytes.
  static unsigned getHashValue(codeview::GloballyHashedType Val) {
    return *reinterpret_cast<const unsigned *>(Val.Hash.data());
  }

  /// Return true if \p LHS and \p RHS have equal global hashes.
  ///
  /// \param LHS Left-hand globally hashed type.
  /// \param RHS Right-hand globally hashed type.
  /// \returns True if the global hashes are equal.
  static bool isEqual(codeview::GloballyHashedType LHS,
                      codeview::GloballyHashedType RHS) {
    return LHS == RHS;
  }
};

/// Format provider that prints a locally hashed type as an 8-digit hex hash.
template <> struct format_provider<codeview::LocallyHashedType> {
public:
  /// Write the local hash of \p V to \p Stream.
  ///
  /// \param V Locally hashed type to print.
  /// \param Stream Destination stream.
  /// \param Style Unused format-style specifier.
  static void format(const codeview::LocallyHashedType &V,
                     llvm::raw_ostream &Stream, StringRef Style) {
    write_hex(Stream, V.Hash, HexPrintStyle::Upper, 8);
  }
};

/// Format provider that prints a globally hashed type as uppercase hex bytes.
template <> struct format_provider<codeview::GloballyHashedType> {
public:
  /// Write the global hash bytes of \p V to \p Stream.
  ///
  /// \param V Globally hashed type to print.
  /// \param Stream Destination stream.
  /// \param Style Unused format-style specifier.
  static void format(const codeview::GloballyHashedType &V,
                     llvm::raw_ostream &Stream, StringRef Style) {
    for (uint8_t B : V.Hash) {
      write_hex(Stream, B, HexPrintStyle::Upper, 2);
    }
  }
};

} // namespace llvm

#endif
