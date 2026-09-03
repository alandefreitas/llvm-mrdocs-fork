//==- CodeViewYAMLTypeHashing.h - CodeView YAMLIO Type hashing ----*- C++-*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines classes for handling the YAML representation of CodeView
// Debug Info.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECTYAML_CODEVIEWYAMLTYPEHASHING_H
#define LLVM_OBJECTYAML_CODEVIEWYAMLTYPEHASHING_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/DebugInfo/CodeView/TypeHashing.h"
#include "llvm/ObjectYAML/YAML.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/YAMLTraits.h"
#include <cstdint>
#include <vector>

namespace llvm {

namespace CodeViewYAML {

/// YAML representation of an 8-byte CodeView global type hash.
struct GlobalHash {
  /// Construct an empty global hash.
  GlobalHash() = default;
  /// Construct a global hash from the 8-byte string \p S.
  /// \param S Hash bytes as a string; must be exactly 8 bytes.
  explicit GlobalHash(StringRef S) : Hash(S) {
    assert(S.size() == 8 && "Invalid hash size!");
  }
  /// Construct a global hash from the 8-byte array \p S.
  /// \param S Hash bytes; must be exactly 8 bytes.
  explicit GlobalHash(ArrayRef<uint8_t> S) : Hash(S) {
    assert(S.size() == 8 && "Invalid hash size!");
  }
  /// Eight-byte global type hash rendered as binary in YAML.
  yaml::BinaryRef Hash;
};

/// YAML representation of a CodeView \c .debug$H section.
struct DebugHSection {
  /// Magic number identifying a \c .debug$H section.
  uint32_t Magic;
  /// Version of the \c .debug$H section format.
  uint16_t Version;
  /// Algorithm used to compute the global type hashes.
  uint16_t HashAlgorithm;
  /// Global type hashes stored in the section.
  std::vector<GlobalHash> Hashes;
};

/// Parse a \c .debug$H section blob into a YAML \c DebugHSection.
/// \param DebugH Raw bytes of the \c .debug$H section.
/// \return Parsed YAML debug hash section.
LLVM_ABI DebugHSection fromDebugH(ArrayRef<uint8_t> DebugH);
/// Serialize a YAML \c DebugHSection into a \c .debug$H section blob.
/// \param DebugH YAML debug hash section to serialize.
/// \param Alloc Allocator used for the returned blob storage.
/// \return Serialized \c .debug$H section bytes owned by \p Alloc.
LLVM_ABI ArrayRef<uint8_t> toDebugH(const DebugHSection &DebugH,
                                    BumpPtrAllocator &Alloc);

} // end namespace CodeViewYAML

} // end namespace llvm

namespace llvm {
namespace yaml {

/// YAMLIO mapping traits for \c CodeViewYAML::DebugHSection.
template <> struct LLVM_ABI MappingTraits<CodeViewYAML::DebugHSection> {
  /// Map debug hash section fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Obj Debug hash section being mapped.
  static void mapping(IO &IO, CodeViewYAML::DebugHSection &Obj);
};

/// YAMLIO scalar traits for \c CodeViewYAML::GlobalHash.
template <> struct LLVM_ABI ScalarTraits<CodeViewYAML::GlobalHash> {
  /// Write \p Value as a YAML scalar to \p Out.
  /// \param Value Global hash to write.
  /// \param Ctx Optional YAML context pointer.
  /// \param Out Output stream.
  static void output(const CodeViewYAML::GlobalHash &Value, void *Ctx,
                     raw_ostream &Out);
  /// Parse YAML scalar \p Scalar into \p Value.
  /// \param Scalar YAML scalar text.
  /// \param Ctx Optional YAML context pointer.
  /// \param Value Destination global hash.
  /// \return Empty string on success, or an error message.
  static StringRef input(StringRef Scalar, void *Ctx,
                         CodeViewYAML::GlobalHash &Value);
  /// Return whether \p S must be quoted in YAML.
  /// \param S Scalar text being considered.
  /// \return Quoting requirement for \p S.
  static QuotingType mustQuote(StringRef S) { return QuotingType::None; }
};

/// Sequences of CodeView global hashes use block formatting.
template <> struct SequenceElementTraits<CodeViewYAML::GlobalHash> {
  /// Emit sequences of CodeView global hashes in block style.
  static const bool flow = false;
};

} // end namespace yaml
} // end namespace llvm

#endif // LLVM_OBJECTYAML_CODEVIEWYAMLTYPEHASHING_H
