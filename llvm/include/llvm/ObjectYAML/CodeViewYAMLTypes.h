//==- CodeViewYAMLTypes.h - CodeView YAMLIO Type implementation --*- C++ -*-==//
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

#ifndef LLVM_OBJECTYAML_CODEVIEWYAMLTYPES_H
#define LLVM_OBJECTYAML_CODEVIEWYAMLTYPES_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/YAMLTraits.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace llvm {

namespace codeview {
class AppendingTypeTableBuilder;
}

namespace CodeViewYAML {

namespace detail {

struct LeafRecordBase;
struct MemberRecordBase;

} // end namespace detail

/// YAML representation of a CodeView field-list member record.
struct MemberRecord {
  /// Type-erased implementation of the concrete member record kind.
  std::shared_ptr<detail::MemberRecordBase> Member;
};

/// YAML representation of a CodeView type leaf record.
struct LeafRecord {
  /// Type-erased implementation of the concrete leaf record kind.
  std::shared_ptr<detail::LeafRecordBase> Leaf;

  /// Serialize this YAML leaf record into a CodeView type record.
  /// \param Serializer Type table builder that owns serialized record storage.
  /// \return The serialized CodeView type record.
  LLVM_ABI codeview::CVType
  toCodeViewRecord(codeview::AppendingTypeTableBuilder &Serializer) const;
  /// Build a YAML leaf record from a CodeView type record.
  /// \param Type CodeView type record to convert.
  /// \return The YAML leaf record, or an error on failure.
  LLVM_ABI static Expected<LeafRecord>
  fromCodeViewRecord(codeview::CVType Type);
};

/// Parse a \c .debug$T or \c .debug$P section blob into YAML leaf records.
/// \param DebugTorP Raw bytes of the type or ID section.
/// \param SectionName Name of the section being parsed, for diagnostics.
/// \return Parsed YAML leaf records.
LLVM_ABI std::vector<LeafRecord> fromDebugT(ArrayRef<uint8_t> DebugTorP,
                                            StringRef SectionName);
/// Serialize YAML leaf records into a \c .debug$T or \c .debug$P section blob.
/// \param LeafRecords YAML leaf records to serialize.
/// \param Alloc Allocator used for the serialized section bytes.
/// \param SectionName Name of the section being written, for diagnostics.
/// \return Serialized section bytes owned by \p Alloc.
LLVM_ABI ArrayRef<uint8_t>
toDebugT(ArrayRef<LeafRecord> LeafRecords, BumpPtrAllocator &Alloc,
         StringRef SectionName);

} // end namespace CodeViewYAML

} // end namespace llvm

namespace llvm {
namespace yaml {

/// YAMLIO scalar traits for \c codeview::GUID.
template <> struct LLVM_ABI ScalarTraits<codeview::GUID> {
  /// Write \p Value as a YAML scalar to \p Out.
  /// \param Value GUID value to write.
  /// \param Ctx Optional YAML context pointer.
  /// \param Out Output stream.
  static void output(const codeview::GUID &Value, void *Ctx, raw_ostream &Out);
  /// Parse YAML scalar \p Scalar into \p Value.
  /// \param Scalar YAML scalar text.
  /// \param Ctx Optional YAML context pointer.
  /// \param Value Destination GUID value.
  /// \return Empty string on success, or an error message.
  static StringRef input(StringRef Scalar, void *Ctx, codeview::GUID &Value);
  /// Return whether \p S must be quoted in YAML.
  /// \param S Scalar text being considered.
  /// \return Quoting requirement for \p S.
  static QuotingType mustQuote(StringRef S) { return QuotingType::Single; }
};

/// YAMLIO mapping traits for \c CodeViewYAML::LeafRecord.
template <> struct LLVM_ABI MappingTraits<CodeViewYAML::LeafRecord> {
  /// Map YAML leaf record fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Obj YAML leaf record being mapped.
  static void mapping(IO &IO, CodeViewYAML::LeafRecord &Obj);
};

/// YAMLIO mapping traits for \c CodeViewYAML::MemberRecord.
template <> struct LLVM_ABI MappingTraits<CodeViewYAML::MemberRecord> {
  /// Map YAML member record fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Obj YAML member record being mapped.
  static void mapping(IO &IO, CodeViewYAML::MemberRecord &Obj);
};

/// Sequences of YAML leaf records use block formatting.
template <> struct SequenceElementTraits<CodeViewYAML::LeafRecord> {
  /// Emit sequences of YAML leaf records in block style.
  static const bool flow = false;
};

/// Sequences of YAML member records use block formatting.
template <> struct SequenceElementTraits<CodeViewYAML::MemberRecord> {
  /// Emit sequences of YAML member records in block style.
  static const bool flow = false;
};

} // end namespace yaml
} // end namespace llvm

#endif // LLVM_OBJECTYAML_CODEVIEWYAMLTYPES_H
