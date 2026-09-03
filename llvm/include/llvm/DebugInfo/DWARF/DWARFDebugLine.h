//===- DWARFDebugLine.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFDEBUGLINE_H
#define LLVM_DEBUGINFO_DWARF_DWARFDEBUGLINE_H

#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/DebugInfo/DWARF/DWARFFormValue.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MD5.h"
#include "llvm/Support/Path.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace llvm {

class raw_ostream;

/// Parser and cache for DWARF .debug_line line-number program tables.
class DWARFDebugLine {
public:
  /// One filename entry from a DWARF line-table prologue file_names list.
  struct FileNameEntry {
    /// Construct an empty file-name entry.
    FileNameEntry() = default;

    /// File name as stored in the line-table prologue (form value).
    DWARFFormValue Name;
    /// Index into the prologue's include_directories list.
    uint64_t DirIdx = 0;
    /// Modification timestamp of the source file, when present in the entry.
    uint64_t ModTime = 0;
    /// File length in bytes, when present in the file_names entry.
    uint64_t Length = 0;
    /// MD5 checksum of the file contents (DWARF v5).
    MD5::MD5Result Checksum;
    /// Optional embedded source text for this file (DWARF v5).
    DWARFFormValue Source;
  };

  /// Tracks which optional content types are present in a DWARF file name
  /// entry format.
  struct ContentTypeTracker {
    /// Construct with no optional content types marked present.
    ContentTypeTracker() = default;

    /// Whether filename entries provide a modification timestamp.
    bool HasModTime = false;
    /// Whether filename entries provide a file size.
    bool HasLength = false;
    /// For v5, whether filename entries provide an MD5 checksum.
    bool HasMD5 = false;
    /// For v5, whether filename entries provide source text.
    bool HasSource = false;

    /// Update tracked content types with \p ContentType.
    ///
    /// \param ContentType Line-number entry format content type to record.
    LLVM_ABI void trackContentType(dwarf::LineNumberEntryFormat ContentType);
  };

  /// Parsed DWARF line table prologue (lengths, versions, directories/files).
  struct Prologue {
    /// Construct an empty line-table prologue with default fields.
    LLVM_ABI Prologue();

    /// The size in bytes of the statement information for this compilation unit
    /// (not including the total_length field itself).
    uint64_t TotalLength;
    /// Version, address size, and DWARF32/64 format for this prologue.
    ///
    /// Version, address size (starting in v5), and DWARF32/64 format; these
    /// parameters affect interpretation of forms (used in the directory and
    /// file tables starting with v5).
    dwarf::FormParams FormParams;
    /// The number of bytes following the prologue_length field to the beginning
    /// of the first byte of the statement program itself.
    uint64_t PrologueLength;
    /// In v5, size in bytes of a segment selector.
    uint8_t SegSelectorSize;
    /// Size in bytes of the smallest target machine instruction.
    ///
    /// The size in bytes of the smallest target machine instruction. Statement
    /// program opcodes that alter the address register first multiply their
    /// operands by this value.
    uint8_t MinInstLength;
    /// The maximum number of individual operations that may be encoded in an
    /// instruction.
    uint8_t MaxOpsPerInst;
    /// The initial value of theis_stmtregister.
    uint8_t DefaultIsStmt;
    /// This parameter affects the meaning of the special opcodes. See below.
    int8_t LineBase;
    /// This parameter affects the meaning of the special opcodes. See below.
    uint8_t LineRange;
    /// The number assigned to the first special opcode.
    uint8_t OpcodeBase;
    /// This tracks which optional file format content types are present.
    ContentTypeTracker ContentTypes;
    /// Operand counts for standard opcodes 1..OpcodeBase-1 from the prologue.
    std::vector<uint8_t> StandardOpcodeLengths;
    /// Include-directory paths from the line-table prologue (form values).
    std::vector<DWARFFormValue> IncludeDirectories;
    /// Source file entries from the line-table prologue's file_names list.
    std::vector<FileNameEntry> FileNames;

    /// Return DWARF version/address-size/format parameters for this prologue.
    ///
    /// \returns FormParams describing version, address size, and format.
    const dwarf::FormParams getFormParams() const { return FormParams; }
    /// DWARF version number from the line-table prologue.
    ///
    /// \returns The DWARF version from FormParams.
    uint16_t getVersion() const { return FormParams.Version; }
    /// Address size in bytes from the line-table prologue.
    ///
    /// \returns Address size in bytes from FormParams.
    uint8_t getAddressSize() const { return FormParams.AddrSize; }
    /// True if this line-table prologue uses the DWARF64 format.
    ///
    /// \returns True if FormParams.Format is dwarf::DWARF64.
    bool isDWARF64() const { return FormParams.Format == dwarf::DWARF64; }

    /// Byte size of the unit_length field including the DWARF64 escape, if any.
    ///
    /// \returns Size in bytes of the total_length / unit_length field.
    uint32_t sizeofTotalLength() const { return isDWARF64() ? 12 : 4; }

    /// Byte size of the prologue_length field (8 for DWARF64, otherwise 4).
    ///
    /// \returns Size in bytes of the prologue_length field.
    uint32_t sizeofPrologueLength() const { return isDWARF64() ? 8 : 4; }

    /// True if TotalLength is a plausible unit_length for this DWARF format.
    ///
    /// \returns True if TotalLength is valid for the prologue's DWARF format.
    LLVM_ABI bool totalLengthIsValid() const;

    /// Length of the prologue in bytes.
    ///
    /// \returns Total byte length of this prologue including length fields.
    LLVM_ABI uint64_t getLength() const;

    /// Get DWARF-version aware access to the file name entry at the provided
    /// index.
    ///
    /// \param Index Index into the prologue file_names table.
    /// \returns Const reference to the file name entry at \p Index.
    LLVM_ABI const llvm::DWARFDebugLine::FileNameEntry &
    getFileNameEntry(uint64_t Index) const;

    /// True if the prologue's file_names table contains \p FileIndex.
    ///
    /// \param FileIndex Index into the prologue file_names table.
    /// \returns True if \p FileIndex is present in the file_names table.
    LLVM_ABI bool hasFileAtIndex(uint64_t FileIndex) const;

    /// Highest valid file index in the prologue file_names table, if any.
    ///
    /// \returns The highest valid file index, or std::nullopt if none.
    LLVM_ABI std::optional<uint64_t> getLastValidFileIndex() const;

    /// Resolve file index \p FileIndex to a path string using \p Kind.
    ///
    /// \param FileIndex Index into the prologue file_names table.
    /// \param CompDir Compilation directory used when building absolute paths.
    /// \param Kind How much path information to include in \p Result.
    /// \param Result Receives the resolved file path on success.
    /// \param Style Path style used when joining directory and file components.
    ///
    /// \returns true on success and writes the path into \p Result.
    LLVM_ABI bool
    getFileNameByIndex(uint64_t FileIndex, StringRef CompDir,
                       DILineInfoSpecifier::FileLineInfoKind Kind,
                       std::string &Result,
                       sys::path::Style Style = sys::path::Style::native) const;

    /// Reset prologue fields and clear directory/file tables.
    LLVM_ABI void clear();
    /// Print prologue fields, directories, and file names to \p OS.
    ///
    /// \param OS Output stream to write the dump to.
    /// \param DumpOptions Options controlling dump formatting.
    LLVM_ABI void dump(raw_ostream &OS, DIDumpOptions DumpOptions) const;
    /// Parse a line-table prologue from \p Data at \p *OffsetPtr.
    ///
    /// \param Data Extractor for the .debug_line section bytes.
    /// \param OffsetPtr Byte offset of the prologue; advanced past it.
    /// \param RecoverableErrorHandler Callback for non-fatal parse issues.
    /// \param Ctx DWARF context used while parsing.
    /// \param U Optional compilation/type unit associated with this prologue.
    /// \returns Success, or an error if the prologue could not be parsed.
    LLVM_ABI Error parse(DWARFDataExtractor Data, uint64_t *OffsetPtr,
                         function_ref<void(Error)> RecoverableErrorHandler,
                         const DWARFContext &Ctx, const DWARFUnit *U = nullptr);
  };

  /// Standard .debug_line state machine structure.
  struct Row {
    /// Construct a row with the given default \c is_stmt value.
    ///
    /// \param DefaultIsStmt Initial value for the is_stmt register.
    LLVM_ABI explicit Row(bool DefaultIsStmt = false);

    /// Called after a row is appended to the matrix.
    LLVM_ABI void postAppend();
    /// Reset registers to initial state machine values (\p DefaultIsStmt for is_stmt).
    ///
    /// \param DefaultIsStmt Value restored into the is_stmt register.
    LLVM_ABI void reset(bool DefaultIsStmt);
    /// Print this row's address, line/file info, and flags to \p OS.
    ///
    /// \param OS Output stream to write the dump to.
    LLVM_ABI void dump(raw_ostream &OS) const;

    /// Print column headers for a dumped line-table row matrix.
    ///
    /// \param OS Output stream to write the header to.
    /// \param Indent Number of spaces to indent before the header.
    LLVM_ABI static void dumpTableHeader(raw_ostream &OS, unsigned Indent);

    /// True if \p LHS precedes \p RHS by section index then address.
    ///
    /// \param LHS Left-hand row for ordering.
    /// \param RHS Right-hand row for ordering.
    /// \returns True if \p LHS sorts before \p RHS by section then address.
    static bool orderByAddress(const Row &LHS, const Row &RHS) {
      return std::tie(LHS.Address.SectionIndex, LHS.Address.Address) <
             std::tie(RHS.Address.SectionIndex, RHS.Address.Address);
    }

    /// Program-counter address and section for this matrix row.
    ///
    /// The program-counter value corresponding to a machine instruction
    /// generated by the compiler and section index pointing to the section
    /// containg this PC. If relocation information is present then section
    /// index is the index of the section which contains above address.
    /// Otherwise this is object::SectionedAddress::Undef value.
    object::SectionedAddress Address;
    /// Source line number for this instruction (1-based; 0 if unknown).
    ///
    /// An unsigned integer indicating a source line number. Lines are numbered
    /// beginning at 1. The compiler may emit the value 0 in cases where an
    /// instruction cannot be attributed to any source line.
    uint32_t Line;
    /// Source column within the line (1-based; 0 means left edge).
    ///
    /// An unsigned integer indicating a column number within a source line.
    /// Columns are numbered beginning at 1. The value 0 is reserved to indicate
    /// that a statement begins at the 'left edge' of the line.
    uint16_t Column;
    /// An unsigned integer indicating the identity of the source file
    /// corresponding to a machine instruction.
    uint16_t File;
    /// An unsigned integer representing the DWARF path discriminator value
    /// for this location.
    uint32_t Discriminator;
    /// An unsigned integer whose value encodes the applicable instruction set
    /// architecture for the current instruction.
    uint8_t Isa;
    /// Operation index within a VLIW instruction (always 0 if non-VLIW).
    ///
    /// An unsigned integer representing the index of an operation within a
    /// VLIW instruction. The index of the first operation is 0.
    /// For non-VLIW architectures, this register will always be 0.
    uint8_t OpIndex;
    /// A boolean indicating that the current instruction is the beginning of a
    /// statement.
    uint8_t IsStmt : 1,
        /// A boolean indicating that the current instruction is the
        /// beginning of a basic block.
        BasicBlock : 1,
        /// A boolean indicating that the current address is that of the
        /// first byte after the end of a sequence of target machine
        /// instructions.
        EndSequence : 1,
        /// A boolean indicating that the current address is one (of possibly
        /// many) where execution should be suspended for an entry breakpoint
        /// of a function.
        PrologueEnd : 1,
        /// A boolean indicating that the current address is one (of possibly
        /// many) where execution should be suspended for an exit breakpoint
        /// of a function.
        EpilogueBegin : 1;
  };

  /// Contiguous instruction range covered by a run of line-table rows.
  ///
  /// A compilation unit's line table may contain multiple sequences, not
  /// necessarily ordered by ascending instruction address.
  struct Sequence {
    /// Construct an empty, invalid sequence.
    LLVM_ABI Sequence();

    /// Inclusive start address of this sequence's instruction range.
    ///
    /// Sequence describes instructions at address range [LowPC, HighPC)
    /// and is described by line table rows [FirstRowIndex, LastRowIndex).
    uint64_t LowPC;
    /// Exclusive end address of this sequence's instruction range.
    uint64_t HighPC;
    /// Section index for this sequence's addresses, if relocations exist.
    ///
    /// If relocation information is present then this is the index of the
    /// section which contains above addresses. Otherwise this is
    /// object::SectionedAddress::Undef value.
    uint64_t SectionIndex;
    unsigned FirstRowIndex; ///< Inclusive start index into the row matrix.
    unsigned LastRowIndex; ///< Exclusive end index into the row matrix.
    bool Empty; ///< True if this sequence has not been finalized.

    /// The offset into the line table where this sequence begins
    uint64_t StmtSeqOffset = UINT64_MAX;

    /// Clear addresses, row indices, and mark this sequence empty/invalid.
    LLVM_ABI void reset();

    /// True if \p LHS precedes \p RHS by section index then high PC.
    ///
    /// \param LHS Left-hand sequence for ordering.
    /// \param RHS Right-hand sequence for ordering.
    /// \returns True if \p LHS sorts before \p RHS by section then high PC.
    static bool orderByHighPC(const Sequence &LHS, const Sequence &RHS) {
      return std::tie(LHS.SectionIndex, LHS.HighPC) <
             std::tie(RHS.SectionIndex, RHS.HighPC);
    }

    /// True if the sequence is non-empty with a valid address and row range.
    ///
    /// \returns True if this sequence has a valid address and row range.
    bool isValid() const {
      return !Empty && (LowPC < HighPC) && (FirstRowIndex < LastRowIndex);
    }

    /// True if \p PC lies in this sequence's [LowPC, HighPC) address range.
    ///
    /// \param PC Sectioned address to test for containment.
    /// \returns True if \p PC is in this sequence's section and address range.
    bool containsPC(object::SectionedAddress PC) const {
      return SectionIndex == PC.SectionIndex &&
             (LowPC <= PC.Address && PC.Address < HighPC);
    }
  };

  /// Parsed DWARF line-number program: prologue, row matrix, and sequences.
  struct LineTable {
    /// Construct an empty line table with a default prologue.
    LLVM_ABI LineTable();

    /// Represents an invalid row
    const uint32_t UnknownRowIndex = UINT32_MAX;

    /// Append matrix row \p R to this line table.
    ///
    /// \param R Line-table matrix row to append.
    void appendRow(const DWARFDebugLine::Row &R) { Rows.push_back(R); }

    /// Append contiguous sequence \p S described by a run of line-table rows.
    ///
    /// \param S Sequence describing a contiguous address range of rows.
    void appendSequence(const DWARFDebugLine::Sequence &S) {
      Sequences.push_back(S);
    }

    /// Returns the index of the row with file/line info for a given address,
    /// or UnknownRowIndex if there is no such row.
    ///
    /// \param Address Sectioned program-counter address to look up.
    /// \param IsApproximateLine If non-null, set when the matched line is
    /// approximate rather than exact.
    /// \returns Row index with file/line info, or UnknownRowIndex if none.
    LLVM_ABI uint32_t lookupAddress(object::SectionedAddress Address,
                                    bool *IsApproximateLine = nullptr) const;

    /// Fills the Result argument with the indices of the rows that correspond
    /// to the address range specified by \p Address and \p Size.
    ///
    /// \param Address - The starting address of the range.
    /// \param Size - The size of the address range.
    /// \param Result - The vector to fill with row indices.
    /// \param StmtSequenceOffset - if provided, only rows from the sequence
    /// starting at the matching offset will be added to the result.
    /// \returns True if any rows were found for the address range.
    LLVM_ABI bool lookupAddressRange(
        object::SectionedAddress Address, uint64_t Size,
        std::vector<uint32_t> &Result,
        std::optional<uint64_t> StmtSequenceOffset = std::nullopt) const;

    /// True if the line table prologue contains a file at \p FileIndex.
    ///
    /// \param FileIndex Index into the prologue file_names table.
    /// \returns True if \p FileIndex is present in the prologue file_names table.
    bool hasFileAtIndex(uint64_t FileIndex) const {
      return Prologue.hasFileAtIndex(FileIndex);
    }

    /// Highest valid file index in the prologue file_names table, if any.
    ///
    /// \returns The highest valid file index, or std::nullopt if none.
    std::optional<uint64_t> getLastValidFileIndex() const {
      return Prologue.getLastValidFileIndex();
    }

    /// Resolve a prologue file_names index to a path string.
    ///
    /// In Dwarf 4, the files are 1-indexed and the current compilation file
    /// name is not represented in the list. In DWARF v5, the files are
    /// 0-indexed and the primary source file has the index 0.
    ///
    /// \param FileIndex Index into the prologue file_names table.
    /// \param CompDir Compilation directory used when building absolute paths.
    /// \param Kind How much path information to include in \p Result.
    /// \param Result Receives the resolved file path on success.
    /// \returns True on success and writes the path into \p Result.
    bool getFileNameByIndex(uint64_t FileIndex, StringRef CompDir,
                            DILineInfoSpecifier::FileLineInfoKind Kind,
                            std::string &Result) const {
      return Prologue.getFileNameByIndex(FileIndex, CompDir, Kind, Result);
    }

    /// Fills the Result argument with the file and line information
    /// corresponding to Address.
    ///
    /// \param Address Sectioned program-counter address to resolve.
    /// \param Approximate Allow approximate line info when no exact match.
    /// \param CompDir Compilation directory used when building file paths.
    /// \param Kind How much file/path information to include in \p Result.
    /// \param Result Receives file and line information on success.
    /// \returns True on success and writes file/line info into \p Result.
    LLVM_ABI bool getFileLineInfoForAddress(
        object::SectionedAddress Address, bool Approximate, const char *CompDir,
        DILineInfoSpecifier::FileLineInfoKind Kind, DILineInfo &Result) const;

    /// Extracts directory name by its Entry in include directories table
    /// in prologue.
    ///
    /// \param Entry File-name entry whose directory index is resolved.
    /// \param Directory Receives the resolved include-directory path.
    /// \returns True on success and writes the path into \p Directory.
    LLVM_ABI bool getDirectoryForEntry(const FileNameEntry &Entry,
                                       std::string &Directory) const;

    /// Print this line table (prologue, rows, sequences) to \p OS.
    ///
    /// \param OS Output stream to write the dump to.
    /// \param DumpOptions Options controlling dump formatting.
    LLVM_ABI void dump(raw_ostream &OS, DIDumpOptions DumpOptions) const;
    /// Clear parsed rows, sequences, and prologue state.
    LLVM_ABI void clear();

    /// Parse prologue and all rows.
    ///
    /// \param DebugLineData Extractor for the .debug_line section.
    /// \param OffsetPtr Byte offset of this table; advanced past it on success.
    /// \param Ctx DWARF context used while parsing.
    /// \param U Optional compilation/type unit associated with this table.
    /// \param RecoverableErrorHandler Callback for non-fatal parse issues.
    /// \param OS Optional stream for parse progress output.
    /// \param Verbose Print verbose details when dumping to \p OS.
    /// \returns Success, or an error if the line table could not be parsed.
    LLVM_ABI Error parse(DWARFDataExtractor &DebugLineData, uint64_t *OffsetPtr,
                         const DWARFContext &Ctx, const DWARFUnit *U,
                         function_ref<void(Error)> RecoverableErrorHandler,
                         raw_ostream *OS = nullptr, bool Verbose = false);

    /// Vector of parsed line-table matrix rows.
    using RowVector = std::vector<Row>;
    /// Const iterator over line-table rows.
    using RowIter = RowVector::const_iterator;
    /// Vector of contiguous address sequences in this line table.
    using SequenceVector = std::vector<Sequence>;
    /// Const iterator over line-table sequences.
    using SequenceIter = SequenceVector::const_iterator;

    /// Line-table prologue (lengths, version, directories, and file names).
    struct Prologue Prologue;
    /// Parsed line-table matrix rows in program order.
    RowVector Rows;
    /// Contiguous address sequences described by runs of line-table rows.
    SequenceVector Sequences;

  private:
    uint32_t findRowInSeq(const DWARFDebugLine::Sequence &Seq,
                          object::SectionedAddress Address) const;
    std::optional<StringRef>
    getSourceByIndex(uint64_t FileIndex,
                     DILineInfoSpecifier::FileLineInfoKind Kind) const;

    uint32_t lookupAddressImpl(object::SectionedAddress Address,
                               bool *IsApproximateLine = nullptr) const;

    /// Fills the Result argument with the indices of the rows that correspond
    /// to the address range specified by \p Address and \p Size.
    ///
    /// \param Address - The starting address of the range.
    /// \param Size - The size of the address range.
    /// \param Result - The vector to fill with row indices.
    /// \param StmtSequenceOffset - if provided, only rows from the sequence
    /// starting at the matching offset will be added to the result.
    ///
    /// Returns true if any rows were found.
    bool
    lookupAddressRangeImpl(object::SectionedAddress Address, uint64_t Size,
                           std::vector<uint32_t> &Result,
                           std::optional<uint64_t> StmtSequenceOffset) const;
  };

  /// Return the cached line table at section \p Offset, or nullptr if unparsed.
  ///
  /// \param Offset Byte offset of the line table within .debug_line.
  /// \returns Pointer to the cached line table, or nullptr if not yet parsed.
  LLVM_ABI const LineTable *getLineTable(uint64_t Offset) const;
  /// Return the line table at \p Offset, parsing and caching it if needed.
  ///
  /// \param DebugLineData Extractor for the .debug_line section.
  /// \param Offset Byte offset of the line table within .debug_line.
  /// \param Ctx DWARF context used while parsing the table.
  /// \param U Optional compilation/type unit associated with this table.
  /// \param RecoverableErrorHandler Callback for non-fatal parse issues.
  /// \returns Pointer to the cached line table, or an error on parse failure.
  LLVM_ABI Expected<const LineTable *>
  getOrParseLineTable(DWARFDataExtractor &DebugLineData, uint64_t Offset,
                      const DWARFContext &Ctx, const DWARFUnit *U,
                      function_ref<void(Error)> RecoverableErrorHandler);
  /// Remove the cached line table at section \p Offset, if present.
  ///
  /// \param Offset Byte offset of the line table within .debug_line.
  LLVM_ABI void clearLineTable(uint64_t Offset);

  /// Helper to allow for parsing of an entire .debug_line section in sequence.
  class SectionParser {
  public:
    /// Map from line-table section offset to the owning DWARF unit.
    using LineToUnitMap = std::map<uint64_t, DWARFUnit *>;

    /// Construct a parser over the entire .debug_line section in \p Data.
    ///
    /// \param Data .debug_line section contents to parse.
    /// \param C DWARF context providing units and related sections.
    /// \param Units Compilation/type units used to map line-table offsets.
    LLVM_ABI SectionParser(DWARFDataExtractor &Data, const DWARFContext &C,
                           DWARFUnitVector::iterator_range Units);

    /// Get the next line table from the section. Report any issues via the
    /// handlers.
    ///
    /// \param RecoverableErrorHandler - any issues that don't prevent further
    /// parsing of the table will be reported through this handler.
    /// \param UnrecoverableErrorHandler - any issues that prevent further
    /// parsing of the table will be reported through this handler.
    /// \param OS - if not null, the parser will print information about the
    /// table as it parses it.
    /// \param Verbose - if true, the parser will print verbose information when
    /// printing to the output.
    /// \returns The next parsed line table from the section.
    LLVM_ABI LineTable
    parseNext(function_ref<void(Error)> RecoverableErrorHandler,
              function_ref<void(Error)> UnrecoverableErrorHandler,
              raw_ostream *OS = nullptr, bool Verbose = false);

    /// Skip the current line table and go to the following line table (if
    /// present) immediately.
    ///
    /// \param RecoverableErrorHandler - report any recoverable prologue
    /// parsing issues via this handler.
    /// \param UnrecoverableErrorHandler - report any unrecoverable prologue
    /// parsing issues via this handler.
    LLVM_ABI void skip(function_ref<void(Error)> RecoverableErrorHandler,
                       function_ref<void(Error)> UnrecoverableErrorHandler);

    /// Indicates if the parser has parsed as much as possible.
    ///
    /// \note Certain problems with the line table structure might mean that
    /// parsing stops before the end of the section is reached.
    ///
    /// \returns True if no further line tables remain to parse.
    bool done() const { return Done; }

    /// Get the offset the parser has reached.
    ///
    /// \returns Byte offset into .debug_line where parsing will continue.
    uint64_t getOffset() const { return Offset; }

  private:
    DWARFUnit *prepareToParse(uint64_t Offset);
    void moveToNextTable(uint64_t OldOffset, const Prologue &P);
    bool hasValidVersion(uint64_t Offset);

    LineToUnitMap LineToUnit;

    DWARFDataExtractor &DebugLineData;
    const DWARFContext &Context;
    uint64_t Offset = 0;
    bool Done = false;
  };

private:
  struct ParsingState {
    LLVM_ABI ParsingState(struct LineTable *LT, uint64_t TableOffset,
                          function_ref<void(Error)> ErrorHandler);

    LLVM_ABI void resetRowAndSequence(uint64_t Offset);
    LLVM_ABI void appendRowToMatrix();

    struct AddrOpIndexDelta {
      uint64_t AddrOffset;
      int16_t OpIndexDelta;
    };

    /// Advance the address and op-index by the \p OperationAdvance value.
    /// \returns the amount advanced by.
    LLVM_ABI AddrOpIndexDelta advanceAddrOpIndex(uint64_t OperationAdvance,
                                                 uint8_t Opcode,
                                                 uint64_t OpcodeOffset);

    struct OpcodeAdvanceResults {
      uint64_t AddrDelta;
      int16_t OpIndexDelta;
      uint8_t AdjustedOpcode;
    };

    /// Advance the address and op-index as required by the specified \p Opcode.
    /// \returns the amount advanced by and the calculated adjusted opcode.
    LLVM_ABI OpcodeAdvanceResults advanceForOpcode(uint8_t Opcode,
                                                   uint64_t OpcodeOffset);

    struct SpecialOpcodeDelta {
      uint64_t Address;
      int32_t Line;
      int16_t OpIndex;
    };

    /// Advance the line, address and op-index as required by the specified
    /// special \p Opcode. \returns the address, op-index and line delta.
    LLVM_ABI SpecialOpcodeDelta handleSpecialOpcode(uint8_t Opcode,
                                                    uint64_t OpcodeOffset);

    /// Line table we're currently parsing.
    struct LineTable *LineTable;
    struct Row Row;
    struct Sequence Sequence;

  private:
    uint64_t LineTableOffset;

    bool ReportAdvanceAddrProblem = true;
    bool ReportBadLineRange = true;
    function_ref<void(Error)> ErrorHandler;
  };

  using LineTableMapTy = std::map<uint64_t, LineTable>;
  using LineTableIter = LineTableMapTy::iterator;
  using LineTableConstIter = LineTableMapTy::const_iterator;

  LineTableMapTy LineTableMap;
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFDEBUGLINE_H
