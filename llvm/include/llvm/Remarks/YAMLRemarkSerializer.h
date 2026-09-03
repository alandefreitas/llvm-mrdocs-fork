//===-- YAMLRemarkSerializer.h - YAML Remark serialization ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides an interface for serializing remarks to YAML.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_REMARKS_YAMLREMARKSERIALIZER_H
#define LLVM_REMARKS_YAMLREMARKSERIALIZER_H

#include "llvm/Remarks/RemarkSerializer.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/YAMLTraits.h"

namespace llvm {
namespace remarks {

/// Serialize remarks to YAML.
///
/// One remark entry looks like this:
/// --- !<TYPE>
/// Pass:            <PASSNAME>
/// Name:            <REMARKNAME>
/// DebugLoc:        { File: <SOURCEFILENAME>, Line: <SOURCELINE>,
///                    Column: <SOURCECOLUMN> }
/// Function:        <FUNCTIONNAME>
/// Args:
///   - <KEY>: <VALUE>
///     DebugLoc:        { File: <FILE>, Line: <LINE>, Column: <COL> }
/// ...
struct LLVM_ABI YAMLRemarkSerializer : public RemarkSerializer {
  /// The YAML streamer.
  yaml::Output YAMLOutput;

  /// Construct a serializer that will create its own string table.
  /// @param OS The stream to emit remarks to.
  YAMLRemarkSerializer(raw_ostream &OS);
  /// Construct a serializer with a pre-filled string table.
  /// @param OS The stream to emit remarks to.
  /// @param StrTabIn Pre-filled string table to use for emission.
  YAMLRemarkSerializer(raw_ostream &OS, StringTable StrTabIn);

  /// Emit a remark to the stream in YAML.
  /// @param Remark The remark to emit.
  void emit(const Remark &Remark) override;
  /// Return the corresponding metadata serializer.
  /// @param OS The stream to emit metadata to.
  /// @param ExternalFilename Path to an external remarks file, if any.
  /// @return A unique pointer to a YAML metadata serializer.
  std::unique_ptr<MetaSerializer>
  metaSerializer(raw_ostream &OS, StringRef ExternalFilename) override;

  /// Check whether \p S is a YAMLRemarkSerializer.
  /// @param S The serializer to test.
  /// @return true if \p S is a YAMLRemarkSerializer.
  static bool classof(const RemarkSerializer *S) {
    return S->SerializerFormat == Format::YAML;
  }
};

/// Serializer of metadata for YAML remarks.
struct LLVM_ABI YAMLMetaSerializer : public MetaSerializer {
  /// Path to an external remarks file, if any.
  StringRef ExternalFilename;

  /// Construct a metadata serializer that writes to \p OS.
  /// @param OS The stream to emit metadata to.
  /// @param ExternalFilename Path to an external remarks file, if any.
  YAMLMetaSerializer(raw_ostream &OS, StringRef ExternalFilename)
      : MetaSerializer(OS), ExternalFilename(ExternalFilename) {}

  /// Emit the metadata to the stream.
  void emit() override;
};

} // end namespace remarks
} // end namespace llvm

#endif // LLVM_REMARKS_YAMLREMARKSERIALIZER_H
