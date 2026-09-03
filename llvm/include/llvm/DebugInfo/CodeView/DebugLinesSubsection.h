//===- DebugLinesSubsection.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_DEBUGLINESSUBSECTION_H
#define LLVM_DEBUGINFO_CODEVIEW_DEBUGLINESSUBSECTION_H

#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/DebugSubsection.h"
#include "llvm/DebugInfo/CodeView/Line.h"
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

class DebugChecksumsSubsection;
class DebugStringTableSubsection;

/// Header for a CodeView Lines subsection fragment (\c CV_DebugSLinesHeader_t).
struct LineFragmentHeader {
  support::ulittle32_t RelocOffset;  ///< Code offset of the line contribution.
  support::ulittle16_t RelocSegment; ///< Code segment of the line contribution.
  support::ulittle16_t Flags;        ///< See the \c LineFlags enumeration.
  support::ulittle32_t CodeSize;     ///< Code size of this line contribution.
};

/// Header for one file's line block within a Lines subsection
/// (\c CV_DebugSLinesFileBlockHeader_t).
///
/// The following two variable-length arrays appear immediately after the
/// header:
/// \code
///   LineNumberEntry   Lines[NumLines];
///   ColumnNumberEntry Columns[NumLines];
/// \endcode
struct LineBlockFragmentHeader {
  /// Offset of the FileChecksum entry in the file checksums buffer.
  ///
  /// The checksum entry then contains another offset into the string table of
  /// the actual file name.
  support::ulittle32_t NameIndex;
  support::ulittle32_t NumLines;  ///< Number of line entries in this block.
  support::ulittle32_t BlockSize; ///< Size of this block in bytes.
};

/// One source line mapping entry (\c CV_Line_t).
struct LineNumberEntry {
  support::ulittle32_t Offset; ///< Offset to the start of code bytes for this line.
  support::ulittle32_t Flags;  ///< Packed Start:24, End:7, IsStatement:1 fields.
};

/// Optional start and end column numbers for a line entry (\c CV_Column_t).
struct ColumnNumberEntry {
  support::ulittle16_t StartColumn; ///< Starting column number.
  support::ulittle16_t EndColumn;   ///< Ending column number.
};

/// One file block of line (and optional column) entries from a Lines subsection.
struct LineColumnEntry {
  support::ulittle32_t NameIndex; ///< File checksum offset for this block's source file.
  FixedStreamArray<LineNumberEntry> LineNumbers; ///< Line number entries for this block.
  FixedStreamArray<ColumnNumberEntry> Columns; ///< Column entries, if column info is present.
};

/// Extracts \c LineColumnEntry records from a CodeView Lines subsection stream.
class LineColumnExtractor {
public:
  /// Extract one \c LineColumnEntry from \p Stream into \p Item.
  ///
  /// \param Stream Stream positioned at the start of the next line block.
  /// \param Len Set to the number of bytes occupied by the extracted entry.
  /// \param Item Set to the extracted line/column block.
  ///
  /// \returns An Error on failure, or success if an entry was extracted.
  LLVM_ABI Error operator()(BinaryStreamRef Stream, uint32_t &Len,
                            LineColumnEntry &Item);

  /// Line fragment header that provides flags while extracting blocks.
  const LineFragmentHeader *Header = nullptr;
};

/// Read-only view of a CodeView Lines debug subsection.
class DebugLinesSubsectionRef final : public DebugSubsectionRef {
  friend class LineColumnExtractor;

  using LineInfoArray = VarStreamArray<LineColumnEntry, LineColumnExtractor>;
  using Iterator = LineInfoArray::Iterator;

public:
  /// Construct an empty, uninitialized Lines subsection reference.
  LLVM_ABI DebugLinesSubsectionRef();

  /// Return true if \p S is a Lines subsection reference.
  ///
  /// \param S Subsection reference to test.
  ///
  /// \returns True if \p S is a Lines subsection reference.
  static bool classof(const DebugSubsectionRef *S) {
    return S->kind() == DebugSubsectionKind::Lines;
  }

  /// Initialize this view from the bytes readable via \p Reader.
  ///
  /// \param Reader Reader positioned at the start of the Lines subsection data.
  ///
  /// \returns An Error on failure, or success if initialization succeeded.
  LLVM_ABI Error initialize(BinaryStreamReader Reader);

  /// Return an iterator to the first line/column block.
  ///
  /// \returns An iterator to the first line/column block.
  Iterator begin() const { return LinesAndColumns.begin(); }
  /// Return an iterator past the last line/column block.
  ///
  /// \returns An iterator past the last line/column block.
  Iterator end() const { return LinesAndColumns.end(); }

  /// Return a pointer to the line fragment header, or null if uninitialized.
  ///
  /// \returns A pointer to the line fragment header, or null if uninitialized.
  const LineFragmentHeader *header() const { return Header; }

  /// Return true if this subsection includes column number information.
  ///
  /// \returns True if this subsection includes column number information.
  LLVM_ABI bool hasColumnInfo() const;

private:
  const LineFragmentHeader *Header = nullptr;
  LineInfoArray LinesAndColumns;
};

/// Writable CodeView Lines debug subsection.
class LLVM_ABI DebugLinesSubsection final : public DebugSubsection {
  struct Block {
    Block(uint32_t ChecksumBufferOffset)
        : ChecksumBufferOffset(ChecksumBufferOffset) {}

    uint32_t ChecksumBufferOffset;
    std::vector<LineNumberEntry> Lines;
    std::vector<ColumnNumberEntry> Columns;
  };

public:
  /// Construct a Lines subsection that resolves files through \p Checksums.
  ///
  /// \param Checksums File checksums subsection used to map names to offsets.
  /// \param Strings String table subsection used to intern source file names.
  DebugLinesSubsection(DebugChecksumsSubsection &Checksums,
                       DebugStringTableSubsection &Strings);

  /// Return true if \p S is a Lines subsection.
  ///
  /// \param S Subsection to test.
  ///
  /// \returns True if \p S is a Lines subsection.
  static bool classof(const DebugSubsection *S) {
    return S->kind() == DebugSubsectionKind::Lines;
  }

  /// Start a new line block for the source file named \p FileName.
  ///
  /// \param FileName Source file whose lines will be recorded in the new block.
  void createBlock(StringRef FileName);
  /// Append a line mapping at code \p Offset for \p Line to the current block.
  ///
  /// \param Offset Code offset within the contribution for this line.
  /// \param Line Source line information to record.
  void addLineInfo(uint32_t Offset, const LineInfo &Line);
  /// Append a line and column mapping at code \p Offset to the current block.
  ///
  /// \param Offset Code offset within the contribution for this line.
  /// \param Line Source line information to record.
  /// \param ColStart Starting column number.
  /// \param ColEnd Ending column number.
  void addLineAndColumnInfo(uint32_t Offset, const LineInfo &Line,
                            uint32_t ColStart, uint32_t ColEnd);

  /// Return the serialized size of this subsection in bytes.
  ///
  /// \returns The serialized size of this subsection in bytes.
  uint32_t calculateSerializedSize() const override;
  /// Write this subsection's serialized form to \p Writer.
  ///
  /// \param Writer Destination stream writer.
  ///
  /// \returns An Error on failure, or success if the write completed.
  Error commit(BinaryStreamWriter &Writer) const override;

  /// Set the relocation segment and offset for this line contribution.
  ///
  /// \param Segment Code segment of the line contribution.
  /// \param Offset Code offset of the line contribution.
  void setRelocationAddress(uint16_t Segment, uint32_t Offset);
  /// Set the code size covered by this line contribution.
  ///
  /// \param Size Size in bytes of the contributed code range.
  void setCodeSize(uint32_t Size);
  /// Set the line-subsection flags (for example, whether columns are present).
  ///
  /// \param Flags Line subsection flags to store in the fragment header.
  void setFlags(LineFlags Flags);

  /// Return true if this subsection will emit column number information.
  ///
  /// \returns True if this subsection will emit column number information.
  bool hasColumnInfo() const;

private:
  DebugChecksumsSubsection &Checksums;
  uint32_t RelocOffset = 0;
  uint16_t RelocSegment = 0;
  uint32_t CodeSize = 0;
  LineFlags Flags = LF_None;
  std::vector<Block> Blocks;
};

} // end namespace codeview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_DEBUGLINESSUBSECTION_H
