//===- DebugSubsection.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_DEBUGSUBSECTION_H
#define LLVM_DEBUGINFO_CODEVIEW_DEBUGSUBSECTION_H

#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"

#include <cstdint>

namespace llvm {
class BinaryStreamWriter;
namespace codeview {

/// Read-only base class for a CodeView debug subsection view.
class LLVM_ABI DebugSubsectionRef {
public:
  /// Construct a subsection reference of the given kind.
  ///
  /// \param Kind Subsection kind for this reference.
  explicit DebugSubsectionRef(DebugSubsectionKind Kind) : Kind(Kind) {}
  /// Destroy a debug subsection reference.
  virtual ~DebugSubsectionRef();

  /// Return true if \p S is a debug subsection reference.
  ///
  /// \param S Subsection reference to test.
  ///
  /// \returns Always true for any debug subsection reference.
  static bool classof(const DebugSubsectionRef *S) { return true; }

  /// Return the subsection kind of this reference.
  ///
  /// \returns The subsection kind.
  DebugSubsectionKind kind() const { return Kind; }

protected:
  /// Subsection kind for this reference.
  DebugSubsectionKind Kind;
};

/// Writable base class for a CodeView debug subsection.
class LLVM_ABI DebugSubsection {
public:
  /// Construct a debug subsection of the given kind.
  ///
  /// \param Kind Subsection kind for this subsection.
  explicit DebugSubsection(DebugSubsectionKind Kind) : Kind(Kind) {}
  /// Destroy a debug subsection.
  virtual ~DebugSubsection();

  /// Return true if \p S is a debug subsection.
  ///
  /// \param S Subsection to test.
  ///
  /// \returns Always true for any debug subsection.
  static bool classof(const DebugSubsection *S) { return true; }

  /// Return the subsection kind of this subsection.
  ///
  /// \returns The subsection kind.
  DebugSubsectionKind kind() const { return Kind; }

  /// Write this subsection's serialized form to \p Writer.
  ///
  /// \param Writer Destination stream writer.
  ///
  /// \returns An Error on failure, or success if the write completed.
  virtual Error commit(BinaryStreamWriter &Writer) const = 0;
  /// Return the serialized size of this subsection in bytes.
  ///
  /// \returns The serialized size in bytes.
  virtual uint32_t calculateSerializedSize() const = 0;

protected:
  /// Subsection kind for this subsection.
  DebugSubsectionKind Kind;
};

} // namespace codeview
} // namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_DEBUGSUBSECTION_H
