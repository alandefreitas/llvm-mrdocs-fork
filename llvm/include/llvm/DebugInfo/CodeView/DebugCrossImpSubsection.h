//===- DebugCrossImpSubsection.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_DEBUGCROSSIMPSUBSECTION_H
#define LLVM_DEBUGINFO_CODEVIEW_DEBUGCROSSIMPSUBSECTION_H

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/DebugSubsection.h"
#include "llvm/Support/BinaryStreamArray.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <vector>

namespace llvm {
class BinaryStreamReader;
class BinaryStreamWriter;

namespace codeview {

/// One cross-module import entry: a header plus the imported ID list.
struct CrossModuleImportItem {
  /// Pointer to the fixed-size CrossModuleImport header for this entry.
  const CrossModuleImport *Header = nullptr;
  /// Array of imported type or item IDs from the referenced module.
  FixedStreamArray<support::ulittle32_t> Imports;
};

} // end namespace codeview

/// Extracts CrossModuleImportItem records from a variable-length stream array.
template <> struct VarStreamArrayExtractor<codeview::CrossModuleImportItem> {
public:
  /// Context type required by VarStreamArray; unused for this extractor.
  using ContextType = void;

  /// Extract one CrossModuleImportItem from \p Stream into \p Item.
  ///
  /// \param Stream Stream positioned at the start of the next import item.
  /// \param Len Set to the number of bytes occupied by the extracted item.
  /// \param Item Set to the extracted cross-module import item.
  ///
  /// \returns An Error on failure, or success if an item was extracted.
  LLVM_ABI Error operator()(BinaryStreamRef Stream, uint32_t &Len,
                            codeview::CrossModuleImportItem &Item);
};

namespace codeview {

class DebugStringTableSubsection;

/// Read-only view of a CodeView cross-module imports subsection.
class DebugCrossModuleImportsSubsectionRef final : public DebugSubsectionRef {
  using ReferenceArray = VarStreamArray<CrossModuleImportItem>;
  using Iterator = ReferenceArray::Iterator;

public:
  /// Construct an empty cross-module imports subsection reference.
  DebugCrossModuleImportsSubsectionRef()
      : DebugSubsectionRef(DebugSubsectionKind::CrossScopeImports) {}

  /// Return true if \p S is a cross-module imports subsection reference.
  ///
  /// \param S Subsection reference to test.
  ///
  /// \returns True if \p S is a cross-module imports subsection reference.
  static bool classof(const DebugSubsectionRef *S) {
    return S->kind() == DebugSubsectionKind::CrossScopeImports;
  }

  /// Initialize this view by reading import items from \p Reader.
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

  /// Return an iterator to the first cross-module import item.
  ///
  /// \returns An iterator to the first cross-module import item.
  Iterator begin() const { return References.begin(); }
  /// Return an iterator past the last cross-module import item.
  ///
  /// \returns An iterator past the last cross-module import item.
  Iterator end() const { return References.end(); }

private:
  ReferenceArray References;
};

/// Writable CodeView cross-module imports subsection.
class LLVM_ABI DebugCrossModuleImportsSubsection final
    : public DebugSubsection {
public:
  /// Construct a cross-module imports subsection backed by \p Strings.
  ///
  /// \param Strings String table used to store imported module names.
  explicit DebugCrossModuleImportsSubsection(
      DebugStringTableSubsection &Strings)
      : DebugSubsection(DebugSubsectionKind::CrossScopeImports),
        Strings(Strings) {}

  /// Return true if \p S is a cross-module imports subsection.
  ///
  /// \param S Subsection to test.
  ///
  /// \returns True if \p S is a cross-module imports subsection.
  static bool classof(const DebugSubsection *S) {
    return S->kind() == DebugSubsectionKind::CrossScopeImports;
  }

  /// Record an import of \p ImportId from the module named \p Module.
  ///
  /// \param Module Name of the module that defines the imported ID.
  /// \param ImportId Type or item ID to import from \p Module.
  void addImport(StringRef Module, uint32_t ImportId);

  /// Return the number of bytes needed to serialize this subsection.
  ///
  /// \returns The number of bytes needed to serialize this subsection.
  uint32_t calculateSerializedSize() const override;
  /// Write the serialized cross-module imports subsection to \p Writer.
  ///
  /// \param Writer Destination stream writer.
  ///
  /// \returns An Error if writing fails.
  Error commit(BinaryStreamWriter &Writer) const override;

private:
  DebugStringTableSubsection &Strings;
  StringMap<std::vector<support::ulittle32_t>> Mappings;
};

} // end namespace codeview

} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_DEBUGCROSSIMPSUBSECTION_H
