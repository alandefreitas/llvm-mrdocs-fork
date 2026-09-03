//=- CodeViewYAMLDebugSections.h - CodeView YAMLIO debug sections -*- C++ -*-=//
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

#ifndef LLVM_OBJECTYAML_CODEVIEWYAMLDEBUGSECTIONS_H
#define LLVM_OBJECTYAML_CODEVIEWYAMLDEBUGSECTIONS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/DebugSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugSubsectionRecord.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/YAMLTraits.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace llvm {

namespace codeview {

class StringsAndChecksums;
class StringsAndChecksumsRef;

} // end namespace codeview

/// YAML representations of CodeView debug subsections.
namespace CodeViewYAML {

namespace detail {

struct YAMLSubsectionBase;

} // end namespace detail

/// YAML representation of a CodeView FrameData record.
struct YAMLFrameData {
  /// Relative virtual address of the start of the function code.
  uint32_t RvaStart;
  /// Size of the function code in bytes.
  uint32_t CodeSize;
  /// Size of local variables in bytes.
  uint32_t LocalSize;
  /// Size of parameters in bytes.
  uint32_t ParamsSize;
  /// Maximum stack depth reached by the function in bytes.
  uint32_t MaxStackSize;
  /// Frame procedure string describing how to unwind the frame.
  StringRef FrameFunc;
  /// Size of the function prologue in bytes.
  uint32_t PrologSize;
  /// Size of saved registers in bytes.
  uint32_t SavedRegsSize;
  /// Frame data flags bitfield.
  uint32_t Flags;
};

/// YAML representation of a CodeView cross-module import entry.
struct YAMLCrossModuleImport {
  /// Name of the module from which symbols are imported.
  StringRef ModuleName;
  /// Type or item IDs imported from the named module.
  std::vector<uint32_t> ImportIds;
};

/// YAML representation of a single source line mapping entry.
struct SourceLineEntry {
  /// Offset of the code address relative to the section base.
  uint32_t Offset;
  /// Starting source line number.
  uint32_t LineStart;
  /// Delta from \c LineStart to the ending source line.
  uint32_t EndDelta;
  /// True if this line is a statement boundary.
  bool IsStatement;
};

/// YAML representation of a source column range for a line entry.
struct SourceColumnEntry {
  /// Starting column number.
  uint16_t StartColumn;
  /// Ending column number.
  uint16_t EndColumn;
};

/// YAML representation of a contiguous block of source line mappings.
struct SourceLineBlock {
  /// Source file name for the lines in this block.
  StringRef FileName;
  /// Line entries belonging to this block.
  std::vector<SourceLineEntry> Lines;
  /// Optional column entries parallel to \c Lines.
  std::vector<SourceColumnEntry> Columns;
};

/// Byte sequence rendered as a hex-formatted string in YAML.
struct HexFormattedString {
  /// Raw checksum or other binary payload bytes.
  std::vector<uint8_t> Bytes;
};

/// YAML representation of a source file checksum entry.
struct SourceFileChecksumEntry {
  /// Source file name whose checksum is recorded.
  StringRef FileName;
  /// Algorithm used to compute the checksum.
  codeview::FileChecksumKind Kind;
  /// Checksum bytes for the named file.
  HexFormattedString ChecksumBytes;
};

/// YAML representation of a CodeView source line info subsection.
struct SourceLineInfo {
  /// Relocation offset of the code contribution.
  uint32_t RelocOffset;
  /// Relocation segment of the code contribution.
  uint32_t RelocSegment;
  /// Line flags controlling optional fields such as columns.
  codeview::LineFlags Flags;
  /// Size of the code contribution in bytes.
  uint32_t CodeSize;
  /// Per-file blocks of line and optional column mappings.
  std::vector<SourceLineBlock> Blocks;
};

/// YAML representation of an inlinee source site.
struct InlineeSite {
  /// Type index of the inlined function.
  uint32_t Inlinee;
  /// Source file containing the inlinee declaration.
  StringRef FileName;
  /// Source line number of the inlinee declaration.
  uint32_t SourceLineNum;
  /// Extra source files associated with the inlinee when present.
  std::vector<StringRef> ExtraFiles;
};

/// YAML representation of a CodeView inlinee lines subsection.
struct InlineeInfo {
  /// True when each site may list extra files.
  bool HasExtraFiles;
  /// Inlinee sites described by this subsection.
  std::vector<InlineeSite> Sites;
};

/// YAML representation of a single CodeView debug subsection.
struct YAMLDebugSubsection {
  /// Build a YAML subsection from a CodeView debug subsection record.
  /// \param SC String and checksum tables used to resolve file names.
  /// \param SS CodeView debug subsection record to convert.
  /// \return The YAML subsection, or an error on failure.
  LLVM_ABI static Expected<YAMLDebugSubsection>
  fromCodeViewSubection(const codeview::StringsAndChecksumsRef &SC,
                        const codeview::DebugSubsectionRecord &SS);

  /// Type-erased implementation of the concrete subsection kind.
  std::shared_ptr<detail::YAMLSubsectionBase> Subsection;
};

/// Convert YAML debug subsections into CodeView debug subsections.
/// \param Allocator Allocator used for CodeView string and record storage.
/// \param Subsections YAML subsections to convert.
/// \param SC String and checksum tables shared by the subsections.
/// \return The CodeView subsections, or an error on failure.
LLVM_ABI Expected<std::vector<std::shared_ptr<codeview::DebugSubsection>>>
toCodeViewSubsectionList(BumpPtrAllocator &Allocator,
                         ArrayRef<YAMLDebugSubsection> Subsections,
                         const codeview::StringsAndChecksums &SC);

/// Parse a \c .debug$S section blob into YAML debug subsections.
/// \param Data Raw bytes of the \c .debug$S section.
/// \param SC String and checksum tables used to resolve file names.
/// \return Parsed YAML debug subsections.
LLVM_ABI std::vector<YAMLDebugSubsection>
fromDebugS(ArrayRef<uint8_t> Data, const codeview::StringsAndChecksumsRef &SC);

/// Populate string and checksum tables from YAML debug subsections.
/// \param Sections YAML subsections that may contribute strings or checksums.
/// \param SC String and checksum tables to initialize.
LLVM_ABI void
initializeStringsAndChecksums(ArrayRef<YAMLDebugSubsection> Sections,
                              codeview::StringsAndChecksums &SC);

} // end namespace CodeViewYAML

} // end namespace llvm

namespace llvm {
namespace yaml {

/// YAMLIO mapping traits for \c CodeViewYAML::YAMLDebugSubsection.
template <> struct LLVM_ABI MappingTraits<CodeViewYAML::YAMLDebugSubsection> {
  /// Map YAML debug subsection fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Obj YAML debug subsection being mapped.
  static void mapping(IO &IO, CodeViewYAML::YAMLDebugSubsection &Obj);
};

/// Sequences of YAML debug subsections use block formatting.
template <> struct SequenceElementTraits<CodeViewYAML::YAMLDebugSubsection> {
  /// Emit sequences of YAML debug subsections in block style.
  static const bool flow = false;
};

} // end namespace yaml
} // end namespace llvm

#endif // LLVM_OBJECTYAML_CODEVIEWYAMLDEBUGSECTIONS_H
