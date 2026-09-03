//===- DebugUnknownSubsection.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_DEBUGUNKNOWNSUBSECTION_H
#define LLVM_DEBUGINFO_CODEVIEW_DEBUGUNKNOWNSUBSECTION_H

#include "llvm/DebugInfo/CodeView/DebugSubsection.h"
#include "llvm/Support/BinaryStreamRef.h"

namespace llvm {
namespace codeview {

/// Read-only view of an unrecognized CodeView debug subsection.
class DebugUnknownSubsectionRef final : public DebugSubsectionRef {
public:
  /// Construct a reference to an unknown subsection of kind \p Kind.
  ///
  /// \param Kind The subsection kind from the CodeView header.
  /// \param Data Raw bytes of the subsection contents.
  DebugUnknownSubsectionRef(DebugSubsectionKind Kind, BinaryStreamRef Data)
      : DebugSubsectionRef(Kind), Data(Data) {}

  /// Return the raw bytes of this unknown subsection.
  ///
  /// \returns The raw bytes of this unknown subsection.
  BinaryStreamRef getData() const { return Data; }

private:
  BinaryStreamRef Data;
};
}
}

#endif
