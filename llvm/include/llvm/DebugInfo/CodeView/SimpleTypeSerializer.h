//===- SimpleTypeSerializer.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_SIMPLETYPESERIALIZER_H
#define LLVM_DEBUGINFO_CODEVIEW_SIMPLETYPESERIALIZER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Compiler.h"
#include <vector>

namespace llvm {
namespace codeview {
class FieldListRecord;

/// Serializes non-continuation CodeView leaf type records into a byte buffer.
class SimpleTypeSerializer {
  std::vector<uint8_t> ScratchBuffer;

public:
  /// Construct an empty simple type serializer.
  LLVM_ABI SimpleTypeSerializer();
  /// Destroy the simple type serializer.
  LLVM_ABI ~SimpleTypeSerializer();

  /// Serialize leaf type \p Record into a scratch buffer and return its bytes.
  ///
  /// This template is explicitly instantiated in the implementation file for
  /// all supported types. The method itself is ugly, so inlining it into the
  /// header file clutters an otherwise straightforward interface.
  ///
  /// \param Record Leaf type record to serialize.
  /// \returns Bytes of the serialized record; valid until the next serialize.
  template <typename T> ArrayRef<uint8_t> serialize(T &Record);

  /// Deleted overload; field lists must not be serialized through this class.
  ///
  /// \param Record Field list record (unsupported by this serializer).
  ArrayRef<uint8_t> serialize(const FieldListRecord &Record) = delete;
};

} // end namespace codeview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_SIMPLETYPESERIALIZER_H
