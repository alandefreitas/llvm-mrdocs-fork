//===- DebugInlineeLinesSubsection.h ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_DEBUGINLINEELINESSUBSECTION_H
#define LLVM_DEBUGINFO_CODEVIEW_DEBUGINLINEELINESSUBSECTION_H

#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/DebugSubsection.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/Support/BinaryStreamArray.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <vector>

namespace llvm {

namespace codeview {

class DebugChecksumsSubsection;

/// Signature values that introduce an InlineeLines subsection.
enum class InlineeLinesSignature : uint32_t {
  Normal,    ///< Standard inlinee source lines (\c CV_INLINEE_SOURCE_LINE_SIGNATURE).
  ExtraFiles ///< Inlinee lines that also list extra files (\c CV_INLINEE_SOURCE_LINE_SIGNATURE_EX).
};

/// Fixed header for one inlinee source-line entry.
///
/// When extra files are present, the header is followed by
/// \c ExtraFileCount and an array of file checksum offsets.
struct InlineeSourceLineHeader {
  TypeIndex Inlinee;                  ///< Type index of the function that was inlined.
  support::ulittle32_t FileID;        ///< Offset into the FileChecksums subsection.
  support::ulittle32_t SourceLineNum; ///< First source line of the inlined code.
};

/// One decoded InlineeLines entry: header plus optional extra file IDs.
struct InlineeSourceLine {
  const InlineeSourceLineHeader *Header; ///< Pointer to the fixed header for this entry.
  FixedStreamArray<support::ulittle32_t> ExtraFiles; ///< Extra file checksum offsets, if any.
};

} // end namespace codeview

/// Extracts \c InlineeSourceLine records from a variable-length CodeView stream.
template <> struct VarStreamArrayExtractor<codeview::InlineeSourceLine> {
  /// Parse one \c InlineeSourceLine from \p Stream into \p Item.
  ///
  /// \param Stream Remaining bytes of the InlineeLines subsection.
  /// \param Len Set to the number of bytes consumed by this entry.
  /// \param Item Destination record to fill.
  /// \returns Success, or an error if the stream is truncated or malformed.
  LLVM_ABI Error operator()(BinaryStreamRef Stream, uint32_t &Len,
                            codeview::InlineeSourceLine &Item);

  /// True when each entry includes the extra-files count and file ID array.
  bool HasExtraFiles = false;
};

namespace codeview {

/// Read-only view of a CodeView InlineeLines debug subsection.
class DebugInlineeLinesSubsectionRef final : public DebugSubsectionRef {
  using LinesArray = VarStreamArray<InlineeSourceLine>;
  using Iterator = LinesArray::Iterator;

public:
  /// Construct an empty InlineeLines subsection reference.
  LLVM_ABI DebugInlineeLinesSubsectionRef();

  /// Return true if \p S is an InlineeLines subsection reference.
  ///
  /// \param S Subsection reference to test.
  ///
  /// \returns True if \p S is an InlineeLines subsection reference.
  static bool classof(const DebugSubsectionRef *S) {
    return S->kind() == DebugSubsectionKind::InlineeLines;
  }

  /// Initialize this view from the bytes readable via \p Reader.
  ///
  /// \param Reader Stream positioned at the start of the InlineeLines data.
  /// \returns Success, or an error if the subsection cannot be parsed.
  LLVM_ABI Error initialize(BinaryStreamReader Reader);
  /// Initialize this view from the raw subsection bytes in \p Section.
  ///
  /// \param Section Binary contents of the InlineeLines subsection.
  /// \returns Success, or an error if the subsection cannot be parsed.
  Error initialize(BinaryStreamRef Section) {
    return initialize(BinaryStreamReader(Section));
  }

  /// Return true if the underlying line array has been successfully initialized.
  ///
  /// \returns True if the underlying line array has been successfully initialized.
  bool valid() const { return Lines.valid(); }
  /// Return true if entries in this subsection include extra file lists.
  ///
  /// \returns True if entries in this subsection include extra file lists.
  LLVM_ABI bool hasExtraFiles() const;

  /// Return an iterator to the first inlinee source-line entry.
  ///
  /// \returns An iterator to the first inlinee source-line entry.
  Iterator begin() const { return Lines.begin(); }
  /// Return an iterator past the last inlinee source-line entry.
  ///
  /// \returns An iterator past the last inlinee source-line entry.
  Iterator end() const { return Lines.end(); }

private:
  InlineeLinesSignature Signature;
  LinesArray Lines;
};

/// Mutable builder for a CodeView InlineeLines debug subsection.
class LLVM_ABI DebugInlineeLinesSubsection final : public DebugSubsection {
public:
  /// One inline site to serialize, with optional extra file checksum offsets.
  struct Entry {
    std::vector<support::ulittle32_t> ExtraFiles; ///< Extra file checksum offsets for this site.
    InlineeSourceLineHeader Header; ///< Fixed header describing the inlinee and primary file.
  };

  /// Construct a builder that resolves file names through \p Checksums.
  ///
  /// \param Checksums File checksums subsection used to map names to offsets.
  /// \param HasExtraFiles Whether entries will include extra file lists.
  DebugInlineeLinesSubsection(DebugChecksumsSubsection &Checksums,
                              bool HasExtraFiles = false);

  /// Return true if \p S is an InlineeLines subsection.
  ///
  /// \param S Subsection to test.
  ///
  /// \returns True if \p S is an InlineeLines subsection.
  static bool classof(const DebugSubsection *S) {
    return S->kind() == DebugSubsectionKind::InlineeLines;
  }

  /// Serialize this subsection into \p Writer.
  ///
  /// \param Writer Destination binary stream writer.
  /// \returns Success, or an error if writing fails.
  Error commit(BinaryStreamWriter &Writer) const override;
  /// Return the number of bytes this subsection will occupy when serialized.
  ///
  /// \returns The number of bytes this subsection will occupy when serialized.
  uint32_t calculateSerializedSize() const override;

  /// Append an inline site for \p FuncId at \p SourceLine in \p FileName.
  ///
  /// \param FuncId Type index of the inlined function.
  /// \param FileName Source file containing the inline site.
  /// \param SourceLine First source line of the inlined code.
  void addInlineSite(TypeIndex FuncId, StringRef FileName, uint32_t SourceLine);
  /// Attach an extra source file to the most recently added inline site.
  ///
  /// \param FileName Extra source file name to associate with the site.
  void addExtraFile(StringRef FileName);

  /// Return true if this builder emits the extra-files signature and lists.
  ///
  /// \returns True if this builder emits the extra-files signature and lists.
  bool hasExtraFiles() const { return HasExtraFiles; }
  /// Set whether entries should include extra file lists.
  ///
  /// \param Has True to emit the extra-files form of the subsection.
  void setHasExtraFiles(bool Has) { HasExtraFiles = Has; }

  /// Return an iterator to the first pending inline-site entry.
  ///
  /// \returns An iterator to the first pending inline-site entry.
  std::vector<Entry>::const_iterator begin() const { return Entries.begin(); }
  /// Return an iterator past the last pending inline-site entry.
  ///
  /// \returns An iterator past the last pending inline-site entry.
  std::vector<Entry>::const_iterator end() const { return Entries.end(); }

private:
  DebugChecksumsSubsection &Checksums;
  bool HasExtraFiles = false;
  uint32_t ExtraFileCount = 0;
  std::vector<Entry> Entries;
};

} // end namespace codeview

} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_DEBUGINLINEELINESSUBSECTION_H
