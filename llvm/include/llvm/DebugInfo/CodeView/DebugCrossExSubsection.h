//===- DebugCrossExSubsection.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_DEBUGCROSSEXSUBSECTION_H
#define LLVM_DEBUGINFO_CODEVIEW_DEBUGCROSSEXSUBSECTION_H

#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/DebugSubsection.h"
#include "llvm/Support/BinaryStreamArray.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <map>

namespace llvm {
class BinaryStreamReader;
class BinaryStreamWriter;
namespace codeview {

/// Read-only view of a CodeView cross-module exports subsection.
class DebugCrossModuleExportsSubsectionRef final : public DebugSubsectionRef {
  using ReferenceArray = FixedStreamArray<CrossModuleExport>;
  using Iterator = ReferenceArray::Iterator;

public:
  /// Construct an empty cross-module exports subsection reference.
  DebugCrossModuleExportsSubsectionRef()
      : DebugSubsectionRef(DebugSubsectionKind::CrossScopeExports) {}

  /// Return true if \p S is a cross-module exports subsection reference.
  ///
  /// \param S Subsection reference to test.
  ///
  /// \returns True if \p S is a cross-module exports subsection reference.
  static bool classof(const DebugSubsectionRef *S) {
    return S->kind() == DebugSubsectionKind::CrossScopeExports;
  }

  /// Initialize this view by reading export items from \p Reader.
  ///
  /// \param Reader Stream reader positioned at the subsection contents.
  ///
  /// \returns An Error if the subsection could not be parsed.
  LLVM_ABI Error initialize(BinaryStreamReader Reader);
  /// Initialize this view from the raw subsection bytes in \p Stream.
  ///
  /// \param Stream Binary stream containing the subsection contents.
  ///
  /// \returns An Error if the subsection could not be parsed.
  LLVM_ABI Error initialize(BinaryStreamRef Stream);

  /// Return an iterator to the first cross-module export item.
  ///
  /// \returns An iterator to the first cross-module export item.
  Iterator begin() const { return References.begin(); }
  /// Return an iterator past the last cross-module export item.
  ///
  /// \returns An iterator past the last cross-module export item.
  Iterator end() const { return References.end(); }

private:
  FixedStreamArray<CrossModuleExport> References;
};

/// Writable CodeView cross-module exports subsection.
class LLVM_ABI DebugCrossModuleExportsSubsection final
    : public DebugSubsection {
public:
  /// Construct an empty cross-module exports subsection.
  DebugCrossModuleExportsSubsection()
      : DebugSubsection(DebugSubsectionKind::CrossScopeExports) {}

  /// Return true if \p S is a cross-module exports subsection.
  ///
  /// \param S Subsection to test.
  ///
  /// \returns True if \p S is a cross-module exports subsection.
  static bool classof(const DebugSubsection *S) {
    return S->kind() == DebugSubsectionKind::CrossScopeExports;
  }

  /// Record a mapping from local ID \p Local to global ID \p Global.
  ///
  /// \param Local Local type or item ID in this module.
  /// \param Global Global type or item ID exported for other modules.
  void addMapping(uint32_t Local, uint32_t Global);

  /// Return the number of bytes needed to serialize this subsection.
  ///
  /// \returns The number of bytes needed to serialize this subsection.
  uint32_t calculateSerializedSize() const override;
  /// Write the serialized cross-module exports subsection to \p Writer.
  ///
  /// \param Writer Destination stream writer.
  ///
  /// \returns An Error if writing fails.
  Error commit(BinaryStreamWriter &Writer) const override;

private:
  std::map<uint32_t, uint32_t> Mappings;
};

} // end namespace codeview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_DEBUGCROSSEXSUBSECTION_H
