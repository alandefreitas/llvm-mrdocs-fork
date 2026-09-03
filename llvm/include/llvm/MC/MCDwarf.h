//===- MCDwarf.h - Machine Code Dwarf support -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the MCDwarfFile to support the dwarf
// .file directive and the .loc directive.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCDWARF_H
#define LLVM_MC_MCDWARF_H

#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/StringTableBuilder.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MD5.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/Support/StringSaver.h"
#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace llvm {

template <typename T> class ArrayRef;
class MCAsmBackend;
class MCContext;
class MCObjectStreamer;
class MCSection;
class MCStreamer;
class MCSymbol;
class raw_ostream;
class SourceMgr;

/// Internal helpers for emitting DWARF list table headers.
namespace mcdwarf {
/// Emit the common part of the DWARF 5 range/locations list tables header.
///
/// \param S - Streamer used to emit the list table header.
/// \return The symbol marking the start of the lists table header.
LLVM_ABI MCSymbol *emitListsTableHeaderStart(MCStreamer &S);
} // namespace mcdwarf

/// Manage the .debug_line_str section contents, if we use it.
class MCDwarfLineStr {
  BumpPtrAllocator Alloc;
  StringSaver Saver{Alloc};
  MCSymbol *LineStrLabel = nullptr;
  StringTableBuilder LineStrings{StringTableBuilder::DWARF};
  bool UseRelocs = false;

public:
  /// Construct an instance that can emit .debug_line_str (for use in a normal
  /// v5 line table).
  ///
  /// \param Ctx - Assembler context used for symbols and section setup.
  LLVM_ABI explicit MCDwarfLineStr(MCContext &Ctx);

  /// Return the string saver used for line-string paths.
  ///
  /// \return The string saver used for line-string paths.
  StringSaver &getSaver() { return Saver; }

  /// Emit a reference to the string.
  ///
  /// \param MCOS - Streamer that receives the string reference.
  /// \param Path - Path string to reference in .debug_line_str.
  LLVM_ABI void emitRef(MCStreamer *MCOS, StringRef Path);

  /// Emit the .debug_line_str section if appropriate.
  ///
  /// \param MCOS - Streamer that emits the .debug_line_str section.
  LLVM_ABI void emitSection(MCStreamer *MCOS);

  /// Returns finalized section.
  ///
  /// \return The finalized .debug_line_str section contents.
  LLVM_ABI SmallString<0> getFinalizedData();

  /// Adds path \p Path to the line string. Returns offset in the
  /// .debug_line_str section.
  ///
  /// \param Path - Path string to add to the line string table.
  /// \return Offset of the path in the .debug_line_str section.
  LLVM_ABI size_t addString(StringRef Path);
};

/// Represents a DWARF .file directive name and its file number.
///
/// Instances of this class represent the name of the dwarf .file directive and
/// its associated dwarf file number in the MC file. MCDwarfFile's are created
/// and uniqued by the MCContext class. In Dwarf 4 file numbers start from 1;
/// i.e. the entry with file number 1 is the first element in the vector of
/// DwarfFiles and there is no MCDwarfFile with file number 0. In Dwarf 5 file
/// numbers start from 0, with the MCDwarfFile with file number 0 being the
/// primary source file, and file numbers correspond to their index in the
/// vector.
struct MCDwarfFile {
  /// The base name of the file without its directory path.
  std::string Name;

  /// The index into the list of directory names for this file name.
  unsigned DirIndex = 0;

  /// The MD5 checksum, if there is one. Non-owning pointer to data allocated
  /// in MCContext.
  std::optional<MD5::MD5Result> Checksum;

  /// The source code of the file. Non-owning reference to data allocated in
  /// MCContext.
  std::optional<StringRef> Source;
};

/// Instances of this class represent the information from a
/// dwarf .loc directive.
class MCDwarfLoc {
  uint32_t FileNum;
  uint32_t Line;
  uint16_t Column;
  // Flags (see #define's below)
  uint8_t Flags;
  uint8_t Isa;
  uint32_t Discriminator;

// Flag that indicates the initial value of the is_stmt_start flag.
#define DWARF2_LINE_DEFAULT_IS_STMT 1

#define DWARF2_FLAG_IS_STMT (1 << 0)
#define DWARF2_FLAG_BASIC_BLOCK (1 << 1)
#define DWARF2_FLAG_PROLOGUE_END (1 << 2)
#define DWARF2_FLAG_EPILOGUE_BEGIN (1 << 3)

private: // MCContext manages these
  friend class MCContext;
  friend class MCDwarfLineEntry;

  MCDwarfLoc(unsigned fileNum, unsigned line, unsigned column, unsigned flags,
             unsigned isa, unsigned discriminator)
      : FileNum(fileNum), Line(line), Column(column), Flags(flags), Isa(isa),
        Discriminator(discriminator) {}

  // Allow the default copy constructor and assignment operator to be used
  // for an MCDwarfLoc object.

public:
  /// Get the FileNum of this MCDwarfLoc.
  ///
  /// \return The FileNum of this MCDwarfLoc.
  unsigned getFileNum() const { return FileNum; }

  /// Get the Line of this MCDwarfLoc.
  ///
  /// \return The Line of this MCDwarfLoc.
  unsigned getLine() const { return Line; }

  /// Get the Column of this MCDwarfLoc.
  ///
  /// \return The Column of this MCDwarfLoc.
  unsigned getColumn() const { return Column; }

  /// Get the Flags of this MCDwarfLoc.
  ///
  /// \return The Flags of this MCDwarfLoc.
  unsigned getFlags() const { return Flags; }

  /// Get the Isa of this MCDwarfLoc.
  ///
  /// \return The Isa of this MCDwarfLoc.
  unsigned getIsa() const { return Isa; }

  /// Get the Discriminator of this MCDwarfLoc.
  ///
  /// \return The Discriminator of this MCDwarfLoc.
  unsigned getDiscriminator() const { return Discriminator; }

  /// Set the FileNum of this MCDwarfLoc.
  ///
  /// \param fileNum - DWARF file number for this location.
  void setFileNum(unsigned fileNum) { FileNum = fileNum; }

  /// Set the Line of this MCDwarfLoc.
  ///
  /// \param line - Source line number for this location.
  void setLine(unsigned line) { Line = line; }

  /// Set the Column of this MCDwarfLoc.
  ///
  /// \param column - Source column number for this location.
  void setColumn(unsigned column) {
    assert(column <= UINT16_MAX);
    Column = column;
  }

  /// Set the Flags of this MCDwarfLoc.
  ///
  /// \param flags - DWARF line-table flags bitmask for this location.
  void setFlags(unsigned flags) {
    assert(flags <= UINT8_MAX);
    Flags = flags;
  }

  /// Set the Isa of this MCDwarfLoc.
  ///
  /// \param isa - Instruction set architecture identifier for this location.
  void setIsa(unsigned isa) {
    assert(isa <= UINT8_MAX);
    Isa = isa;
  }

  /// Set the Discriminator of this MCDwarfLoc.
  ///
  /// \param discriminator - DWARF discriminator value for this location.
  void setDiscriminator(unsigned discriminator) {
    Discriminator = discriminator;
  }
};

/// Represents one DWARF line-table entry for an assembled instruction.
///
/// Instances of this class represent the line information for the dwarf line
/// table entries. Which is created after a machine instruction is assembled and
/// uses an address from a temporary label created at the current address in the
/// current section and the info from the last .loc directive seen as stored in
/// the context.
class MCDwarfLineEntry : public MCDwarfLoc {
  MCSymbol *Label;

private:
  // Allow the default copy constructor and assignment operator to be used
  // for an MCDwarfLineEntry object.

public:
  /// Construct a line entry from a symbol and DWARF location.
  ///
  /// \param label - Temporary label at the instruction address.
  /// \param loc - Source location from the last .loc directive.
  /// \param lineStreamLabel - Optional label emitted into the line stream.
  /// \param streamLabelDefLoc - Source location where \p lineStreamLabel was defined.
  MCDwarfLineEntry(MCSymbol *label, const MCDwarfLoc loc,
                   MCSymbol *lineStreamLabel = nullptr,
                   SMLoc streamLabelDefLoc = {})
      : MCDwarfLoc(loc), Label(label), LineStreamLabel(lineStreamLabel),
        StreamLabelDefLoc(streamLabelDefLoc) {}

  /// Return the temporary label for this line entry's address.
  ///
  /// \return The temporary label for this line entry's address.
  MCSymbol *getLabel() const { return Label; }

  /// Label to emit into the line stream, restarting the current sequence if set.
  MCSymbol *LineStreamLabel;

  /// Location where LineStreamLabel was defined, for error reporting.
  SMLoc StreamLabelDefLoc;

  /// True when this line entry is synthesized as an end-of-sequence entry.
  bool IsEndEntry = false;

  /// Override the label with \p EndLabel and mark this as an end-of-sequence entry.
  ///
  /// \param EndLabel - Label used as the end-of-sequence address.
  void setEndLabel(MCSymbol *EndLabel) {
    // If we're setting this to be an end entry, make sure we don't have
    // LineStreamLabel set.
    assert(LineStreamLabel == nullptr);
    Label = EndLabel;
    IsEndEntry = true;
  }

  /// Create a line entry if pending .loc info has not yet been recorded.
  ///
  /// Called when an instruction is assembled into \p Section; if information
  /// from the last .loc directive has yet to have a line entry made for it, one
  /// is made.
  ///
  /// \param MCOS - Streamer assembling the instruction.
  /// \param Section - Section receiving the assembled instruction.
  LLVM_ABI static void make(MCStreamer *MCOS, MCSection *Section);
};

/// Holds per-section DWARF line entries for a compile unit.
///
/// Instances of this class represent the line information for a compile unit
/// where machine instructions have been assembled after seeing .loc directives.
/// This is the information used to build the dwarf line table for a section.
class MCLineSection {
public:
  /// Add a line entry for \p Sec to this section's collections.
  ///
  /// \param LineEntry - Line entry to record.
  /// \param Sec - Section that owns this line entry.
  void addLineEntry(const MCDwarfLineEntry &LineEntry, MCSection *Sec) {
    MCLineDivisions[Sec].push_back(LineEntry);
  }

  /// Add an end-of-sequence entry cloned from the last entry for EndLabel's section.
  ///
  /// \param EndLabel - Label that ends the line sequence for its section.
  LLVM_ABI void addEndEntry(MCSymbol *EndLabel);

  /// Collection type for DWARF line entries in one section.
  using MCDwarfLineEntryCollection = std::vector<MCDwarfLineEntry>;
  /// Iterator over DWARF line entries in one section.
  using iterator = MCDwarfLineEntryCollection::iterator;
  /// Const iterator over DWARF line entries in one section.
  using const_iterator = MCDwarfLineEntryCollection::const_iterator;
  /// Per-section collections of DWARF line entries, in section order.
  using MCLineDivisionMap = MapVector<MCSection *, MCDwarfLineEntryCollection>;

private:
  // A collection of MCDwarfLineEntry for each section.
  MCLineDivisionMap MCLineDivisions;

public:
  /// Return the per-section map of DWARF line entries.
  ///
  /// \return The per-section map of DWARF line entries.
  const MCLineDivisionMap &getMCLineEntries() const {
    return MCLineDivisions;
  }
};

/// Parameters controlling special opcodes in the DWARF line table.
struct MCDwarfLineTableParams {
  /// First special line opcode value after the standard opcodes.
  ///
  /// First special line opcode - leave room for the standard opcodes.
  /// Note: If you want to change this, you'll have to update the
  /// "StandardOpcodeLengths" table that is emitted in
  /// \c Emit().
  uint8_t DWARF2LineOpcodeBase = 13;
  /// Minimum line offset in a special line info. opcode.  The value
  /// -5 was chosen to give a reasonable range of values.
  int8_t DWARF2LineBase = -5;
  /// Range of line offsets in a special line info. opcode.
  uint8_t DWARF2LineRange = 14;
};

/// Header metadata for a DWARF line table (directories, files, and root file).
struct MCDwarfLineTableHeader {
  /// Symbol marking the start of this line table.
  MCSymbol *Label = nullptr;
  /// Directory names referenced by file entries.
  SmallVector<std::string, 3> MCDwarfDirs;
  /// File entries in DWARF file-number order.
  SmallVector<MCDwarfFile, 3> MCDwarfFiles;
  /// Map from source path identity to allocated file number.
  StringMap<unsigned> SourceIdMap;
  /// Compilation directory string for this line table.
  std::string CompilationDir;
  /// Primary source file used as file index 0 (DWARF 5) / root file.
  MCDwarfFile RootFile;
  /// True if any file entry recorded embedded source text.
  bool HasAnySource = false;

private:
  bool HasAllMD5 = true;
  bool HasAnyMD5 = false;

public:
  /// Construct an empty DWARF line table header.
  MCDwarfLineTableHeader() = default;

  /// Look up or allocate a DWARF file number for \p FileName under \p Directory.
  ///
  /// When \p FileNumber is zero, reuses an existing entry if present; otherwise
  /// allocates the next free number. Optional \p Checksum and embedded \p Source
  /// are recorded when provided. Returns an error if \p FileNumber is already
  /// allocated to a different file.
  ///
  /// \param Directory - Directory component of the file path (may be updated).
  /// \param FileName - File name component of the path (may be updated).
  /// \param Checksum - Optional MD5 checksum for the file.
  /// \param Source - Optional embedded source text for the file.
  /// \param DwarfVersion - DWARF version controlling file-numbering rules.
  /// \param FileNumber - Requested file number, or 0 to allocate/reuse.
  /// \return The allocated or reused DWARF file number, or an error.
  LLVM_ABI Expected<unsigned> tryGetFile(StringRef &Directory,
                                         StringRef &FileName,
                                         std::optional<MD5::MD5Result> Checksum,
                                         std::optional<StringRef> Source,
                                         uint16_t DwarfVersion,
                                         unsigned FileNumber = 0);
  /// Emit the line table header and return start/end symbols.
  ///
  /// \param MCOS - Streamer that emits the line table header.
  /// \param Params - Special-opcode parameters for the line table.
  /// \param LineStr - Optional .debug_line_str helper for DWARF 5 strings.
  /// \return A pair of start and end symbols for the emitted header.
  LLVM_ABI std::pair<MCSymbol *, MCSymbol *>
  Emit(MCStreamer *MCOS, MCDwarfLineTableParams Params,
       std::optional<MCDwarfLineStr> &LineStr) const;
  /// Emit the line table header with custom special-opcode lengths.
  ///
  /// \param MCOS - Streamer that emits the line table header.
  /// \param Params - Special-opcode parameters for the line table.
  /// \param SpecialOpcodeLengths - Custom standard-opcode length table.
  /// \param LineStr - Optional .debug_line_str helper for DWARF 5 strings.
  /// \return A pair of start and end symbols for the emitted header.
  LLVM_ABI std::pair<MCSymbol *, MCSymbol *>
  Emit(MCStreamer *MCOS, MCDwarfLineTableParams Params,
       ArrayRef<char> SpecialOpcodeLengths,
       std::optional<MCDwarfLineStr> &LineStr) const;
  /// Reset MD5 usage tracking to the empty-table state.
  void resetMD5Usage() {
    HasAllMD5 = true;
    HasAnyMD5 = false;
  }
  /// Record whether the latest file entry provided an MD5 checksum.
  ///
  /// \param MD5Used - True if the file entry included an MD5 checksum.
  void trackMD5Usage(bool MD5Used) {
    HasAllMD5 &= MD5Used;
    HasAnyMD5 |= MD5Used;
  }
  /// Return true if MD5 usage is consistent (all-or-none) across file entries.
  ///
  /// \return True if MD5 usage is consistent (all-or-none) across file entries.
  bool isMD5UsageConsistent() const {
    return MCDwarfFiles.empty() || (HasAllMD5 == HasAnyMD5);
  }

  /// Set the compilation root file used as file index 0 in the DWARF line table.
  ///
  /// \param Directory - Compilation directory / root file directory.
  /// \param FileName - Root source file name.
  /// \param Checksum - Optional MD5 checksum for the root file.
  /// \param Source - Optional embedded source text for the root file.
  void setRootFile(StringRef Directory, StringRef FileName,
                   std::optional<MD5::MD5Result> Checksum,
                   std::optional<StringRef> Source) {
    CompilationDir = std::string(Directory);
    RootFile.Name = std::string(FileName);
    RootFile.DirIndex = 0;
    RootFile.Checksum = Checksum;
    RootFile.Source = Source;
    trackMD5Usage(Checksum.has_value());
    HasAnySource |= Source.has_value();
  }

  /// Clear the DWARF file and directory tables and reset root-file metadata.
  void resetFileTable() {
    MCDwarfDirs.clear();
    MCDwarfFiles.clear();
    RootFile.Name.clear();
    resetMD5Usage();
    HasAnySource = false;
  }

private:
  void emitV2FileDirTables(MCStreamer *MCOS) const;
  void emitV5FileDirTables(MCStreamer *MCOS,
                           std::optional<MCDwarfLineStr> &LineStr) const;
};

/// Split DWARF (DWO) line table for type units and skeleton CUs.
class MCDwarfDwoLineTable {
  MCDwarfLineTableHeader Header;
  bool HasSplitLineTable = false;

public:
  /// Set the root file if one has not already been recorded.
  ///
  /// \param Directory - Directory of the root source file.
  /// \param FileName - Name of the root source file.
  /// \param Checksum - Optional MD5 checksum for the root file.
  /// \param Source - Optional embedded source text for the root file.
  void maybeSetRootFile(StringRef Directory, StringRef FileName,
                        std::optional<MD5::MD5Result> Checksum,
                        std::optional<StringRef> Source) {
    if (!Header.RootFile.Name.empty())
      return;
    Header.setRootFile(Directory, FileName, Checksum, Source);
  }

  /// Look up or allocate a DWARF file number in the split DWO line table.
  ///
  /// Marks the line table as split and delegates to the header's \c tryGetFile.
  ///
  /// \param Directory - Directory component of the file path.
  /// \param FileName - File name component of the path.
  /// \param Checksum - Optional MD5 checksum for the file.
  /// \param DwarfVersion - DWARF version controlling file-numbering rules.
  /// \param Source - Optional embedded source text for the file.
  /// \return The allocated or reused DWARF file number.
  unsigned getFile(StringRef Directory, StringRef FileName,
                   std::optional<MD5::MD5Result> Checksum,
                   uint16_t DwarfVersion, std::optional<StringRef> Source) {
    HasSplitLineTable = true;
    return cantFail(Header.tryGetFile(Directory, FileName, Checksum, Source,
                                      DwarfVersion));
  }

  /// Emit the split DWO line table into \p Section.
  ///
  /// \param MCOS - Streamer that emits the DWO line table.
  /// \param Params - Special-opcode parameters for the line table.
  /// \param Section - Section that receives the line table.
  LLVM_ABI void Emit(MCStreamer &MCOS, MCDwarfLineTableParams Params,
                     MCSection *Section) const;
};

/// DWARF line table for a compile unit, including header and per-section entries.
class MCDwarfLineTable {
  MCDwarfLineTableHeader Header;
  MCLineSection MCLineSections;

public:
  /// Emit DWARF file and line tables for all compile units via \p MCOS.
  ///
  /// \param MCOS - Streamer that emits the line tables.
  /// \param Params - Special-opcode parameters for the line tables.
  LLVM_ABI static void emit(MCStreamer *MCOS, MCDwarfLineTableParams Params);

  /// Emit the DWARF file and line tables for this compile unit.
  ///
  /// \param MCOS - Streamer that emits the line table.
  /// \param Params - Special-opcode parameters for the line table.
  /// \param LineStr - Optional .debug_line_str helper for DWARF 5 strings.
  LLVM_ABI void emitCU(MCStreamer *MCOS, MCDwarfLineTableParams Params,
                       std::optional<MCDwarfLineStr> &LineStr) const;

  /// Emit a single line table associated with a given section.
  ///
  /// \param MCOS - Streamer that emits the line table.
  /// \param Section - Section whose line entries are emitted.
  /// \param LineEntries - Line entries for \p Section.
  LLVM_ABI static void
  emitOne(MCStreamer *MCOS, MCSection *Section,
          const MCLineSection::MCDwarfLineEntryCollection &LineEntries);

  /// End the current line sequence and emit a named line-stream label.
  ///
  /// \param MCOS - Streamer that emits the label.
  /// \param DefLoc - Source location of the label definition.
  /// \param Name - Name of the line-stream label to emit.
  LLVM_ABI void endCurrentSeqAndEmitLineStreamLabel(MCStreamer *MCOS,
                                                    SMLoc DefLoc,
                                                    StringRef Name);

  /// Look up or allocate a DWARF file number for this line table.
  ///
  /// \param Directory - Directory component of the file path (may be updated).
  /// \param FileName - File name component of the path (may be updated).
  /// \param Checksum - Optional MD5 checksum for the file.
  /// \param Source - Optional embedded source text for the file.
  /// \param DwarfVersion - DWARF version controlling file-numbering rules.
  /// \param FileNumber - Requested file number, or 0 to allocate/reuse.
  /// \return The allocated or reused DWARF file number, or an error.
  LLVM_ABI Expected<unsigned> tryGetFile(StringRef &Directory,
                                         StringRef &FileName,
                                         std::optional<MD5::MD5Result> Checksum,
                                         std::optional<StringRef> Source,
                                         uint16_t DwarfVersion,
                                         unsigned FileNumber = 0);
  /// Look up or allocate a DWARF file number, asserting on failure.
  ///
  /// \param Directory - Directory component of the file path (may be updated).
  /// \param FileName - File name component of the path (may be updated).
  /// \param Checksum - Optional MD5 checksum for the file.
  /// \param Source - Optional embedded source text for the file.
  /// \param DwarfVersion - DWARF version controlling file-numbering rules.
  /// \param FileNumber - Requested file number, or 0 to allocate/reuse.
  /// \return The allocated or reused DWARF file number.
  unsigned getFile(StringRef &Directory, StringRef &FileName,
                   std::optional<MD5::MD5Result> Checksum,
                   std::optional<StringRef> Source, uint16_t DwarfVersion,
                   unsigned FileNumber = 0) {
    return cantFail(tryGetFile(Directory, FileName, Checksum, Source,
                               DwarfVersion, FileNumber));
  }

  /// Record the compilation directory and primary source file for this line table.
  ///
  /// Sets \c Header.CompilationDir and \c Header.RootFile, including optional
  /// MD5 checksum and embedded source text when provided.
  ///
  /// \param Directory - Compilation directory / root file directory.
  /// \param FileName - Root source file name.
  /// \param Checksum - Optional MD5 checksum for the root file.
  /// \param Source - Optional embedded source text for the root file.
  void setRootFile(StringRef Directory, StringRef FileName,
                   std::optional<MD5::MD5Result> Checksum,
                   std::optional<StringRef> Source) {
    Header.CompilationDir = std::string(Directory);
    Header.RootFile.Name = std::string(FileName);
    Header.RootFile.DirIndex = 0;
    Header.RootFile.Checksum = Checksum;
    Header.RootFile.Source = Source;
    Header.trackMD5Usage(Checksum.has_value());
    Header.HasAnySource |= Source.has_value();
  }

  /// Clear the DWARF file and directory tables and reset root-file metadata.
  void resetFileTable() { Header.resetFileTable(); }

  /// Return true if a root source file name has been set for this line table.
  ///
  /// \return True if a root source file name has been set for this line table.
  bool hasRootFile() const { return !Header.RootFile.Name.empty(); }

  /// Return the mutable root source file for this line table.
  ///
  /// \return The mutable root source file for this line table.
  MCDwarfFile &getRootFile() { return Header.RootFile; }
  /// Return the root source file for this line table.
  ///
  /// \return The root source file for this line table.
  const MCDwarfFile &getRootFile() const { return Header.RootFile; }

  /// Report whether MD5 usage has been consistent (all-or-none).
  ///
  /// \return True if MD5 usage has been consistent (all-or-none).
  bool isMD5UsageConsistent() const { return Header.isMD5UsageConsistent(); }

  /// Return the label that marks the start of this DWARF line table.
  ///
  /// \return The label that marks the start of this DWARF line table.
  MCSymbol *getLabel() const {
    return Header.Label;
  }

  /// Set the label that marks the start of this DWARF line table.
  ///
  /// \param Label - Symbol marking the start of the line table.
  void setLabel(MCSymbol *Label) {
    Header.Label = Label;
  }

  /// Return the directory table for this line table.
  ///
  /// \return The directory table for this line table.
  const SmallVectorImpl<std::string> &getMCDwarfDirs() const {
    return Header.MCDwarfDirs;
  }

  /// Return the mutable directory table for this line table.
  ///
  /// \return The mutable directory table for this line table.
  SmallVectorImpl<std::string> &getMCDwarfDirs() {
    return Header.MCDwarfDirs;
  }

  /// Return the file table for this line table.
  ///
  /// \return The file table for this line table.
  const SmallVectorImpl<MCDwarfFile> &getMCDwarfFiles() const {
    return Header.MCDwarfFiles;
  }

  /// Return the mutable file table for this line table.
  ///
  /// \return The mutable file table for this line table.
  SmallVectorImpl<MCDwarfFile> &getMCDwarfFiles() {
    return Header.MCDwarfFiles;
  }

  /// Return the per-section line entry collections.
  ///
  /// \return The per-section line entry collections.
  const MCLineSection &getMCLineSections() const {
    return MCLineSections;
  }
  /// Return the mutable per-section line entry collections.
  ///
  /// \return The mutable per-section line entry collections.
  MCLineSection &getMCLineSections() {
    return MCLineSections;
  }
};

/// Helpers for encoding DWARF line/address advance pairs.
class MCDwarfLineAddr {
public:
  /// Encode a DWARF pair of LineDelta and AddrDelta into \p OS.
  ///
  /// \param Context - Assembler context providing DWARF emit parameters.
  /// \param Params - Special-opcode parameters used for encoding.
  /// \param LineDelta - Change in line number to encode.
  /// \param AddrDelta - Change in address to encode.
  /// \param OS - Output buffer that receives the encoded bytes.
  LLVM_ABI static void encode(MCContext &Context, MCDwarfLineTableParams Params,
                              int64_t LineDelta, uint64_t AddrDelta,
                              SmallVectorImpl<char> &OS);

  /// Emit the encoding of a line/address delta pair to a streamer.
  ///
  /// \param MCOS - Streamer that receives the encoded bytes.
  /// \param Params - Special-opcode parameters used for encoding.
  /// \param LineDelta - Change in line number to emit.
  /// \param AddrDelta - Change in address to emit.
  LLVM_ABI static void Emit(MCStreamer *MCOS, MCDwarfLineTableParams Params,
                            int64_t LineDelta, uint64_t AddrDelta);
};

/// Emitter for DWARF generated from assembly source files.
class MCGenDwarfInfo {
public:
  /// Emit DWARF sections for assembly source files into \p MCOS.
  ///
  /// \param MCOS - Streamer that receives the generated DWARF sections.
  LLVM_ABI static void Emit(MCStreamer *MCOS);
};

/// Per-symbol info gathered when generating DWARF labels for assembly sources.
class MCGenDwarfLabelEntry {
private:
  // Name of the symbol without a leading underbar, if any.
  StringRef Name;
  // The dwarf file number this symbol is in.
  unsigned FileNumber;
  // The line number this symbol is at.
  unsigned LineNumber;
  // The low_pc for the dwarf label is taken from this symbol.
  MCSymbol *Label;

public:
  /// Construct a DWARF label entry for an assembly symbol.
  ///
  /// \param name - Symbol name without a leading underscore, if any.
  /// \param fileNumber - DWARF file number for this symbol.
  /// \param lineNumber - Source line number for this symbol.
  /// \param label - Symbol providing the low_pc for the DWARF label.
  MCGenDwarfLabelEntry(StringRef name, unsigned fileNumber, unsigned lineNumber,
                       MCSymbol *label)
      : Name(name), FileNumber(fileNumber), LineNumber(lineNumber),
        Label(label) {}

  /// Return the symbol name without a leading underscore, if any.
  ///
  /// \return The symbol name without a leading underscore, if any.
  StringRef getName() const { return Name; }
  /// Return the DWARF file number for this label.
  ///
  /// \return The DWARF file number for this label.
  unsigned getFileNumber() const { return FileNumber; }
  /// Return the source line number associated with this label.
  ///
  /// \return The source line number associated with this label.
  unsigned getLineNumber() const { return LineNumber; }
  /// Return the symbol that provides low_pc for this DWARF label.
  ///
  /// \return The symbol that provides low_pc for this DWARF label.
  MCSymbol *getLabel() const { return Label; }

  /// Record a DWARF label entry when a symbol is created for assembly sources.
  ///
  /// \param Symbol - Newly created symbol to record.
  /// \param MCOS - Streamer associated with the symbol.
  /// \param SrcMgr - Source manager used to resolve \p Loc.
  /// \param Loc - Source location of the label definition.
  LLVM_ABI static void Make(MCSymbol *Symbol, MCStreamer *MCOS,
                            SourceMgr &SrcMgr, SMLoc &Loc);
};

/// One Call Frame Information instruction for DWARF unwind data.
class MCCFIInstruction {
public:
  /// Kind of CFI operation encoded by this instruction.
  enum OpType : uint8_t {
    /// Current value of a register is unchanged from the previous frame.
    OpSameValue,
    /// Save all current CFI rules for all registers (.cfi_remember_state).
    OpRememberState,
    /// Restore the previously saved CFI rules (.cfi_restore_state).
    OpRestoreState,
    /// Previous register value is saved at an offset from the CFA.
    OpOffset,
    /// Define CFA using a register, offset, and address space.
    OpLLVMDefAspaceCfa,
    /// Change the register used to compute the CFA.
    OpDefCfaRegister,
    /// Change the absolute offset used to compute the CFA.
    OpDefCfaOffset,
    /// Define CFA as register plus offset.
    OpDefCfa,
    /// Previous register value is saved at an offset from the CFA register.
    OpRelOffset,
    /// Adjust the CFA offset by a relative amount.
    OpAdjustCfaOffset,
    /// Escape bytes written verbatim into the unwind info.
    OpEscape,
    /// Restore a register rule to the .cfi_startproc initial state.
    OpRestore,
    /// Previous value of a register can no longer be restored.
    OpUndefined,
    /// Previous value of a register is saved in another register.
    OpRegister,
    /// SPARC register window save.
    OpWindowSave,
    /// AArch64 negate return-address sign state.
    OpNegateRAState,
    /// AArch64 negate return-address sign state with PC.
    OpNegateRAStateWithPC,
    /// AArch64 set return-address sign state.
    OpLLVMSetRAState,
    /// GNU args-size escape wrapper.
    OpGnuArgsSize,
    /// Named CFI label.
    OpLabel,
    /// Previous register value equals CFA plus offset.
    OpValOffset,
    /// Previous register value is saved in a register pair.
    OpLLVMRegisterPair,
    /// Previous register value is saved in vector-register lanes.
    OpLLVMVectorRegisters,
    /// Previous register value is saved at an offset with a vector mask.
    OpLLVMVectorOffset,
    /// Previous register value is saved in a spill register under a mask.
    OpLLVMVectorRegisterMask,
  };

  /// Held in ExtraFields for most common OpTypes, exceptions follow.
  struct CommonFields {
    /// Primary register operand for this CFI instruction.
    unsigned Register;
    /// Offset or size operand for this CFI instruction.
    int64_t Offset;
    /// Second register for ops that take a register pair (e.g. \c .cfi_register).
    unsigned Register2;
    /// Address space for address-space CFA definitions.
    unsigned AddressSpace;
    // FIXME: Workaround for GCC7 bug with nested class used as std::variant
    // alternative where the compiler really wants a user-defined default
    // constructor. Once we no longer support GCC7 these constructors can be
    // replaced with default member initializers and aggregate initialization.
    /// Construct CFI common fields with primary register and optional operands.
    ///
    /// \param Reg - Primary DWARF register number.
    /// \param Off - Offset or size associated with this instruction.
    /// \param Reg2 - Optional second register, or max unsigned if unused.
    /// \param AddrSpace - Optional address space for aspace CFA definitions.
    CommonFields(unsigned Reg, int64_t Off = 0,
                 unsigned Reg2 = std::numeric_limits<unsigned>::max(),
                 unsigned AddrSpace = 0)
        : Register(Reg), Offset(Off), Register2(Reg2), AddressSpace(AddrSpace) {
    }
    /// Default-construct CFI common fields with no primary register.
    CommonFields() : CommonFields(std::numeric_limits<unsigned>::max()) {}
  };
  /// Held in ExtraFields when OpEscape.
  struct EscapeFields {
    /// Raw bytes written by .cfi_escape.
    std::vector<char> Values;
    /// Optional human-readable comment describing the escape bytes.
    std::string Comment;
  };
  /// Held in ExtraFields when OpLabel; stores the CFI label symbol.
  struct LabelFields {
    /// Symbol named by this CFI label instruction.
    MCSymbol *CfiLabel = nullptr;
  };
  /// Held in ExtraFields when OpLLVMRegisterPair.
  struct RegisterPairFields {
    /// DWARF register whose previous value is saved in \c Reg1:\c Reg2.
    unsigned Register;
    /// First register of the pair holding the previous value.
    unsigned Reg1;
    /// Second register of the pair holding the previous value.
    unsigned Reg2;
    /// Size in bits of Reg1 when encoding a register pair CFI expression.
    unsigned Reg1SizeInBits;
    /// Size in bits of Reg2 when encoding a register pair CFI expression.
    unsigned Reg2SizeInBits;
  };
  /// A vector register and the lane used to hold a spilled value.
  struct VectorRegisterWithLane {
    /// Vector register number.
    unsigned Register;
    /// Lane within the vector register.
    unsigned Lane;
    /// Size in bits of this vector register lane.
    unsigned SizeInBits;
  };
  /// Held in ExtraFields when OpLLVMVectorRegisters.
  struct VectorRegistersFields {
    /// DWARF register whose previous value is saved in vector lanes.
    unsigned Register;
    /// Vector registers and lanes that hold the previous value.
    std::vector<VectorRegisterWithLane> VectorRegisters;
  };
  /// Held in ExtraFields when OpLLVMVectorOffset.
  struct VectorOffsetFields {
    /// DWARF register whose previous value is saved at Offset.
    unsigned Register;
    /// Size in bits of the spilled register.
    unsigned RegisterSizeInBits;
    /// Offset from the CFA where the previous value is saved.
    int64_t Offset;
    /// Mask register identifying the active lanes.
    unsigned MaskRegister;
    /// Size in bits of the mask register.
    unsigned MaskRegisterSizeInBits;
  };
  /// Held in ExtraFields when OpLLVMVectorRegisterMask.
  struct VectorRegisterMaskFields {
    /// DWARF register whose previous value is spilled under a mask.
    unsigned Register;
    /// Register that holds the spilled previous value.
    unsigned SpillRegister;
    /// Size in bits of each lane of the spill register.
    unsigned SpillRegisterLaneSizeInBits;
    /// Mask register that predicates the spill.
    unsigned MaskRegister;
    /// Size in bits of the mask register.
    unsigned MaskRegisterSizeInBits;
  };
  /// Held in ExtraFields when OpLLVMSetRAState.
  struct LLVMSetRAStateFields {
    /// The ra_state value (DW_AARCH64_RA_NOT_SIGNED, DW_AARCH64_RA_SIGNED_SP,
    /// or DW_AARCH64_RA_SIGNED_SP_PC).
    unsigned State;
    /// Symbol pointing to the signing instruction.
    /// Precisely one of \p PACSym xor \p Offset should be set.
    MCSymbol *PACSym;
    /// Factored offset to the signing instruction.
    /// Precisely one of \p PACSym xor \p Offset should be set.
    int64_t Offset;
  };

private:
  MCSymbol *Label;
  std::variant<CommonFields, EscapeFields, LabelFields, RegisterPairFields,
               VectorRegistersFields, VectorOffsetFields,
               VectorRegisterMaskFields, LLVMSetRAStateFields>
      ExtraFields;
  OpType Operation;
  SMLoc Loc;

  template <class FieldsType>
  MCCFIInstruction(OpType Op, MCSymbol *L, FieldsType &&EF, SMLoc Loc)
      : Label(L), ExtraFields(std::forward<FieldsType>(EF)), Operation(Op),
        Loc(Loc) {}

public:
  /// Create a .cfi_def_cfa instruction.
  ///
  /// .cfi_def_cfa defines a rule for computing CFA as: take address from
  /// Register and add Offset to it.
  ///
  /// \param L - Label at which this CFI instruction applies.
  /// \param Register - Register used to compute the CFA.
  /// \param Offset - Offset added to \p Register to compute the CFA.
  /// \param Loc - Source location of the directive.
  /// \return A new MCCFIInstruction for .cfi_def_cfa.
  static MCCFIInstruction cfiDefCfa(MCSymbol *L, unsigned Register,
                                    int64_t Offset, SMLoc Loc = {}) {
    return {OpDefCfa, L, CommonFields{Register, Offset}, Loc};
  }

  /// Create a .cfi_def_cfa_register instruction.
  ///
  /// .cfi_def_cfa_register modifies a rule for computing CFA. From now
  /// on Register will be used instead of the old one. Offset remains the same.
  ///
  /// \param L - Label at which this CFI instruction applies.
  /// \param Register - New register used to compute the CFA.
  /// \param Loc - Source location of the directive.
  /// \return A new MCCFIInstruction for .cfi_def_cfa_register.
  static MCCFIInstruction createDefCfaRegister(MCSymbol *L, unsigned Register,
                                               SMLoc Loc = {}) {
    return {OpDefCfaRegister, L, CommonFields{Register}, Loc};
  }

  /// Create a .cfi_def_cfa_offset instruction.
  ///
  /// .cfi_def_cfa_offset modifies a rule for computing CFA. Register remains
  /// the same, but offset is new. Note that it is the absolute offset that will
  /// be added to a defined register to the compute CFA address.
  ///
  /// \param L - Label at which this CFI instruction applies.
  /// \param Offset - New absolute CFA offset.
  /// \param Loc - Source location of the directive.
  /// \return A new MCCFIInstruction for .cfi_def_cfa_offset.
  static MCCFIInstruction cfiDefCfaOffset(MCSymbol *L, int64_t Offset,
                                          SMLoc Loc = {}) {
    return {OpDefCfaOffset, L, CommonFields{0, Offset}, Loc};
  }

  /// Create a .cfi_adjust_cfa_offset instruction.
  ///
  /// .cfi_adjust_cfa_offset Same as .cfi_def_cfa_offset, but
  /// Offset is a relative value that is added/subtracted from the previous
  /// offset.
  ///
  /// \param L - Label at which this CFI instruction applies.
  /// \param Adjustment - Relative CFA offset adjustment.
  /// \param Loc - Source location of the directive.
  /// \return A new MCCFIInstruction for .cfi_adjust_cfa_offset.
  static MCCFIInstruction createAdjustCfaOffset(MCSymbol *L, int64_t Adjustment,
                                                SMLoc Loc = {}) {
    return {OpAdjustCfaOffset, L, CommonFields{0, Adjustment}, Loc};
  }

  // FIXME: Update the remaining docs to use the new proposal wording.
  /// Create a .cfi_llvm_def_aspace_cfa instruction.
  ///
  /// .cfi_llvm_def_aspace_cfa defines the rule for computing the CFA to be the
  /// result of evaluating the DWARF operation expression
  /// `DW_OP_constu AS; DW_OP_aspace_bregx R, B` as a location description.
  ///
  /// \param L - Label at which this CFI instruction applies.
  /// \param Register - Register used in the aspace CFA expression.
  /// \param Offset - Offset used in the aspace CFA expression.
  /// \param AddressSpace - Address space used in the aspace CFA expression.
  /// \param Loc - Source location of the directive.
  /// \return A new MCCFIInstruction for .cfi_llvm_def_aspace_cfa.
  static MCCFIInstruction createLLVMDefAspaceCfa(MCSymbol *L, unsigned Register,
                                                 int64_t Offset,
                                                 unsigned AddressSpace,
                                                 SMLoc Loc) {
    return {OpLLVMDefAspaceCfa, L,
            CommonFields{Register, Offset, 0, AddressSpace}, Loc};
  }

  /// Create a .cfi_offset instruction.
  ///
  /// .cfi_offset Previous value of Register is saved at offset Offset
  /// from CFA.
  ///
  /// \param L - Label at which this CFI instruction applies.
  /// \param Register - Register whose previous value is saved.
  /// \param Offset - Offset from the CFA where the value is saved.
  /// \param Loc - Source location of the directive.
  /// \return A new MCCFIInstruction for .cfi_offset.
  static MCCFIInstruction createOffset(MCSymbol *L, unsigned Register,
                                       int64_t Offset, SMLoc Loc = {}) {
    return {OpOffset, L, CommonFields{Register, Offset}, Loc};
  }

  /// Create a .cfi_rel_offset instruction.
  ///
  /// .cfi_rel_offset Previous value of Register is saved at offset Offset from
  /// the current CFA register. This is transformed to .cfi_offset using the
  /// known displacement of the CFA register from the CFA.
  ///
  /// \param L - Label at which this CFI instruction applies.
  /// \param Register - Register whose previous value is saved.
  /// \param Offset - Offset from the current CFA register.
  /// \param Loc - Source location of the directive.
  /// \return A new MCCFIInstruction for .cfi_rel_offset.
  static MCCFIInstruction createRelOffset(MCSymbol *L, unsigned Register,
                                          int64_t Offset, SMLoc Loc = {}) {
    return {OpRelOffset, L, CommonFields{Register, Offset}, Loc};
  }

  /// Create a .cfi_register instruction.
  ///
  /// .cfi_register Previous value of Register1 is saved in
  /// register Register2.
  ///
  /// \param L - Label at which this CFI instruction applies.
  /// \param Register1 - Register whose previous value is saved.
  /// \param Register2 - Register that holds the previous value.
  /// \param Loc - Source location of the directive.
  /// \return A new MCCFIInstruction for .cfi_register.
  static MCCFIInstruction createRegister(MCSymbol *L, unsigned Register1,
                                         unsigned Register2, SMLoc Loc = {}) {
    return {OpRegister, L, CommonFields{Register1, 0, Register2}, Loc};
  }

  /// Create a .cfi_window_save instruction.
  ///
  /// .cfi_window_save SPARC register window is saved.
  ///
  /// \param L - Label at which this CFI instruction applies.
  /// \param Loc - Source location of the directive.
  /// \return A new MCCFIInstruction for .cfi_window_save.
  static MCCFIInstruction createWindowSave(MCSymbol *L, SMLoc Loc = {}) {
    return {OpWindowSave, L, CommonFields{}, Loc};
  }

  /// Create a .cfi_negate_ra_state instruction.
  ///
  /// .cfi_negate_ra_state AArch64 negate RA state.
  ///
  /// \param L - Label at which this CFI instruction applies.
  /// \param Loc - Source location of the directive.
  /// \return A new MCCFIInstruction for .cfi_negate_ra_state.
  static MCCFIInstruction createNegateRAState(MCSymbol *L, SMLoc Loc = {}) {
    return {OpNegateRAState, L, CommonFields{}, Loc};
  }

  /// Create a .cfi_negate_ra_state_with_pc instruction.
  ///
  /// .cfi_negate_ra_state_with_pc AArch64 negate RA state with PC.
  ///
  /// \param L - Label at which this CFI instruction applies.
  /// \param Loc - Source location of the directive.
  /// \return A new MCCFIInstruction for .cfi_negate_ra_state_with_pc.
  static MCCFIInstruction createNegateRAStateWithPC(MCSymbol *L,
                                                    SMLoc Loc = {}) {
    return {OpNegateRAStateWithPC, L, CommonFields{}, Loc};
  }

  /// Create a .cfi_set_ra_state instruction with a signing-instruction symbol.
  ///
  /// .cfi_set_ra_state AArch64 set RA sign state,
  /// with a symbolic offset to the signing instruction.
  ///
  /// \param L - Label at which this CFI instruction applies.
  /// \param State - AArch64 RA sign state value.
  /// \param PACSym - Symbol pointing to the signing instruction.
  /// \param Loc - Source location of the directive.
  /// \return A new MCCFIInstruction for .cfi_set_ra_state with a signing-instruction symbol.
  static MCCFIInstruction createSetRAState(MCSymbol *L, unsigned State,
                                           MCSymbol *PACSym = nullptr,
                                           SMLoc Loc = {}) {
    return {OpLLVMSetRAState, L, LLVMSetRAStateFields{State, PACSym, 0}, Loc};
  }

  /// Create a .cfi_set_ra_state instruction with a factored offset.
  ///
  /// .cfi_set_ra_state AArch64 set RA sign state,
  /// with a pre-computed factored offset to the signing instruction.
  ///
  /// \param L - Label at which this CFI instruction applies.
  /// \param State - AArch64 RA sign state value.
  /// \param Offset - Factored offset to the signing instruction.
  /// \param Loc - Source location of the directive.
  /// \return A new MCCFIInstruction for .cfi_set_ra_state with a factored offset.
  static MCCFIInstruction createSetRAState(MCSymbol *L, unsigned State,
                                           int64_t Offset, SMLoc Loc = {}) {
    return {OpLLVMSetRAState, L, LLVMSetRAStateFields{State, nullptr, Offset},
            Loc};
  }

  /// Create a .cfi_restore instruction.
  ///
  /// .cfi_restore says that the rule for Register is now the same as it was at
  /// the beginning of the function, after all initial instructions added by
  /// .cfi_startproc were executed.
  ///
  /// \param L - Label at which this CFI instruction applies.
  /// \param Register - Register whose rule is restored.
  /// \param Loc - Source location of the directive.
  /// \return A new MCCFIInstruction for .cfi_restore.
  static MCCFIInstruction createRestore(MCSymbol *L, unsigned Register,
                                        SMLoc Loc = {}) {
    return {OpRestore, L, CommonFields{Register}, Loc};
  }

  /// Create a .cfi_undefined instruction.
  ///
  /// .cfi_undefined From now on the previous value of Register can't be
  /// restored anymore.
  ///
  /// \param L - Label at which this CFI instruction applies.
  /// \param Register - Register whose previous value is undefined.
  /// \param Loc - Source location of the directive.
  /// \return A new MCCFIInstruction for .cfi_undefined.
  static MCCFIInstruction createUndefined(MCSymbol *L, unsigned Register,
                                          SMLoc Loc = {}) {
    return {OpUndefined, L, CommonFields{Register}, Loc};
  }

  /// Create a .cfi_same_value instruction.
  ///
  /// .cfi_same_value Current value of Register is the same as in the
  /// previous frame. I.e., no restoration is needed.
  ///
  /// \param L - Label at which this CFI instruction applies.
  /// \param Register - Register whose value is unchanged across frames.
  /// \param Loc - Source location of the directive.
  /// \return A new MCCFIInstruction for .cfi_same_value.
  static MCCFIInstruction createSameValue(MCSymbol *L, unsigned Register,
                                          SMLoc Loc = {}) {
    return {OpSameValue, L, CommonFields{Register}, Loc};
  }

  /// Create a .cfi_remember_state instruction.
  ///
  /// .cfi_remember_state Save all current rules for all registers.
  ///
  /// \param L - Label at which this CFI instruction applies.
  /// \param Loc - Source location of the directive.
  /// \return A new MCCFIInstruction for .cfi_remember_state.
  static MCCFIInstruction createRememberState(MCSymbol *L, SMLoc Loc = {}) {
    return {OpRememberState, L, CommonFields{}, Loc};
  }

  /// Create a .cfi_restore_state instruction.
  ///
  /// .cfi_restore_state Restore the previously saved state.
  ///
  /// \param L - Label at which this CFI instruction applies.
  /// \param Loc - Source location of the directive.
  /// \return A new MCCFIInstruction for .cfi_restore_state.
  static MCCFIInstruction createRestoreState(MCSymbol *L, SMLoc Loc = {}) {
    return {OpRestoreState, L, CommonFields{}, Loc};
  }

  /// Create a .cfi_escape instruction.
  ///
  /// .cfi_escape Allows the user to add arbitrary bytes to the unwind
  /// info.
  ///
  /// \param L - Label at which this CFI instruction applies.
  /// \param Vals - Raw bytes to write into the unwind info.
  /// \param Loc - Source location of the directive.
  /// \param Comment - Optional human-readable comment for the escape bytes.
  /// \return A new MCCFIInstruction for .cfi_escape.
  static MCCFIInstruction createEscape(MCSymbol *L, StringRef Vals,
                                       SMLoc Loc = {}, StringRef Comment = "") {
    return {OpEscape, L,
            EscapeFields{std::vector<char>(Vals.begin(), Vals.end()),
                         Comment.str()},
            Loc};
  }

  /// Create a GNU_ARGS_SIZE wrapper around .cfi_escape.
  ///
  /// A special wrapper for .cfi_escape that indicates GNU_ARGS_SIZE
  ///
  /// \param L - Label at which this CFI instruction applies.
  /// \param Size - Argument-area size encoded by GNU_ARGS_SIZE.
  /// \param Loc - Source location of the directive.
  /// \return A new MCCFIInstruction for GNU_ARGS_SIZE.
  static MCCFIInstruction createGnuArgsSize(MCSymbol *L, int64_t Size,
                                            SMLoc Loc = {}) {
    return {OpGnuArgsSize, L, CommonFields{0, Size}, Loc};
  }

  /// Create a CFI label instruction.
  ///
  /// \param L - Label at which this CFI instruction applies.
  /// \param CfiLabel - Symbol named by this CFI label.
  /// \param Loc - Source location of the directive.
  /// \return A new MCCFIInstruction for a CFI label.
  static MCCFIInstruction createLabel(MCSymbol *L, MCSymbol *CfiLabel,
                                      SMLoc Loc) {
    return {OpLabel, L, LabelFields{CfiLabel}, Loc};
  }

  /// Create a .cfi_llvm_register_pair instruction.
  ///
  /// .cfi_llvm_register_pair Previous value of Register is saved in R1:R2.
  ///
  /// \param L - Label at which this CFI instruction applies.
  /// \param Register - Register whose previous value is saved.
  /// \param R1 - First register of the pair.
  /// \param R1SizeInBits - Size in bits of \p R1.
  /// \param R2 - Second register of the pair.
  /// \param R2SizeInBits - Size in bits of \p R2.
  /// \param Loc - Source location of the directive.
  /// \return A new MCCFIInstruction for .cfi_llvm_register_pair.
  static MCCFIInstruction
  createLLVMRegisterPair(MCSymbol *L, unsigned Register, unsigned R1,
                         unsigned R1SizeInBits, unsigned R2,
                         unsigned R2SizeInBits, SMLoc Loc = {}) {
    RegisterPairFields Extra{Register, R1, R2, R1SizeInBits, R2SizeInBits};
    return {OpLLVMRegisterPair, L, Extra, Loc};
  }

  /// Create a .cfi_llvm_vector_registers instruction.
  ///
  /// .cfi_llvm_vector_registers Previous value of Register is saved in lanes of
  /// vector registers.
  ///
  /// \param L - Label at which this CFI instruction applies.
  /// \param Register - Register whose previous value is saved.
  /// \param VectorRegisters - Vector registers and lanes holding the value.
  /// \param Loc - Source location of the directive.
  /// \return A new MCCFIInstruction for .cfi_llvm_vector_registers.
  static MCCFIInstruction
  createLLVMVectorRegisters(MCSymbol *L, unsigned Register,
                            ArrayRef<VectorRegisterWithLane> VectorRegisters,
                            SMLoc Loc = {}) {
    VectorRegistersFields Extra{Register, VectorRegisters};
    return {OpLLVMVectorRegisters, L, std::move(Extra), Loc};
  }

  /// Create a .cfi_llvm_vector_offset instruction.
  ///
  /// .cfi_llvm_vector_offset Previous value of Register is saved at Offset from
  /// CFA. MaskRegister specifies the active lanes of register.
  ///
  /// \param L - Label at which this CFI instruction applies.
  /// \param Register - Register whose previous value is saved.
  /// \param RegisterSizeInBits - Size in bits of \p Register.
  /// \param MaskRegister - Mask register identifying active lanes.
  /// \param MaskRegisterSizeInBits - Size in bits of \p MaskRegister.
  /// \param Offset - Offset from the CFA where the value is saved.
  /// \param Loc - Source location of the directive.
  /// \return A new MCCFIInstruction for .cfi_llvm_vector_offset.
  static MCCFIInstruction
  createLLVMVectorOffset(MCSymbol *L, unsigned Register,
                         unsigned RegisterSizeInBits, unsigned MaskRegister,
                         unsigned MaskRegisterSizeInBits, int64_t Offset,
                         SMLoc Loc = {}) {
    VectorOffsetFields Extra{Register, RegisterSizeInBits, Offset, MaskRegister,
                             MaskRegisterSizeInBits};
    return MCCFIInstruction(OpLLVMVectorOffset, L, Extra, Loc);
  }

  /// Create a .cfi_llvm_vector_register_mask instruction.
  ///
  /// .cfi_llvm_vector_register_mask Previous value of Register is saved in
  /// SpillRegister, predicated on the value of MaskRegister.
  ///
  /// \param L - Label at which this CFI instruction applies.
  /// \param Register - Register whose previous value is spilled.
  /// \param SpillRegister - Register that holds the spilled value.
  /// \param SpillRegisterLaneSizeInBits - Lane size of \p SpillRegister.
  /// \param MaskRegister - Mask register that predicates the spill.
  /// \param MaskRegisterSizeInBits - Size in bits of \p MaskRegister.
  /// \param Loc - Source location of the directive.
  /// \return A new MCCFIInstruction for .cfi_llvm_vector_register_mask.
  static MCCFIInstruction createLLVMVectorRegisterMask(
      MCSymbol *L, unsigned Register, unsigned SpillRegister,
      unsigned SpillRegisterLaneSizeInBits, unsigned MaskRegister,
      unsigned MaskRegisterSizeInBits, SMLoc Loc = {}) {
    VectorRegisterMaskFields Extra{
        Register,     SpillRegister,          SpillRegisterLaneSizeInBits,
        MaskRegister, MaskRegisterSizeInBits,
    };
    return MCCFIInstruction(OpLLVMVectorRegisterMask, L, Extra, Loc);
  }

  /// Return the typed operand payload stored for this CFI instruction.
  ///
  /// \return The typed operand payload stored for this CFI instruction.
  template <class ExtraFieldsTy> ExtraFieldsTy &getExtraFields() {
    return std::get<ExtraFieldsTy>(ExtraFields);
  }

  /// Return the typed operand payload stored for this CFI instruction.
  ///
  /// \return The typed operand payload stored for this CFI instruction.
  template <class ExtraFieldsTy> const ExtraFieldsTy &getExtraFields() const {
    return std::get<ExtraFieldsTy>(ExtraFields);
  }
  /// Create a .cfi_val_offset instruction.
  ///
  /// .cfi_val_offset Previous value of Register is offset Offset from the
  /// current CFA register.
  ///
  /// \param L - Label at which this CFI instruction applies.
  /// \param Register - Register whose previous value is CFA-relative.
  /// \param Offset - Offset from the current CFA register.
  /// \param Loc - Source location of the directive.
  /// \return A new MCCFIInstruction for .cfi_val_offset.
  static MCCFIInstruction createValOffset(MCSymbol *L, unsigned Register,
                                          int64_t Offset, SMLoc Loc = {}) {
    return {OpValOffset, L, CommonFields{Register, Offset}, Loc};
  }

  /// Return the CFI operation kind.
  ///
  /// \return The CFI operation kind.
  OpType getOperation() const { return Operation; }
  /// Return the label associated with this CFI instruction.
  ///
  /// \return The label associated with this CFI instruction.
  MCSymbol *getLabel() const { return Label; }

  /// Return the primary register operand for this instruction.
  ///
  /// \return The primary register operand for this instruction.
  unsigned getRegister() const {
    assert(Operation == OpDefCfa || Operation == OpOffset ||
           Operation == OpRestore || Operation == OpUndefined ||
           Operation == OpSameValue || Operation == OpDefCfaRegister ||
           Operation == OpRelOffset || Operation == OpValOffset ||
           Operation == OpRegister || Operation == OpLLVMDefAspaceCfa);
    return std::get<CommonFields>(ExtraFields).Register;
  }

  /// Return the second register operand for register-pair ops.
  ///
  /// \return The second register operand for register-pair ops.
  unsigned getRegister2() const {
    assert(Operation == OpRegister);
    return std::get<CommonFields>(ExtraFields).Register2;
  }

  /// Return the address space for aspace CFA definitions.
  ///
  /// \return The address space for aspace CFA definitions.
  unsigned getAddressSpace() const {
    assert(Operation == OpLLVMDefAspaceCfa);
    return std::get<CommonFields>(ExtraFields).AddressSpace;
  }

  /// Return the offset or size operand for this instruction.
  ///
  /// \return The offset or size operand for this instruction.
  int64_t getOffset() const {
    assert(Operation == OpDefCfa || Operation == OpOffset ||
           Operation == OpRelOffset || Operation == OpDefCfaOffset ||
           Operation == OpAdjustCfaOffset || Operation == OpGnuArgsSize ||
           Operation == OpValOffset || Operation == OpLLVMDefAspaceCfa);
    return std::get<CommonFields>(ExtraFields).Offset;
  }

  /// Return the AArch64 RA sign state for OpLLVMSetRAState.
  ///
  /// \return The AArch64 RA sign state for OpLLVMSetRAState.
  unsigned getRASignState() const {
    assert(Operation == OpLLVMSetRAState);
    return std::get<LLVMSetRAStateFields>(ExtraFields).State;
  }

  /// Return the signing-instruction symbol for OpLLVMSetRAState.
  ///
  /// \return The signing-instruction symbol for OpLLVMSetRAState.
  MCSymbol *getRASignSymbol() const {
    assert(Operation == OpLLVMSetRAState);
    return std::get<LLVMSetRAStateFields>(ExtraFields).PACSym;
  }

  /// Return the factored signing-instruction offset for OpLLVMSetRAState.
  ///
  /// \return The factored signing-instruction offset for OpLLVMSetRAState.
  int64_t getRASignOffset() const {
    assert(Operation == OpLLVMSetRAState);
    return std::get<LLVMSetRAStateFields>(ExtraFields).Offset;
  }

  /// Return the CFI label symbol for OpLabel.
  ///
  /// \return The CFI label symbol for OpLabel.
  MCSymbol *getCfiLabel() const {
    assert(Operation == OpLabel);
    return std::get<LabelFields>(ExtraFields).CfiLabel;
  }

  /// Return the raw escape bytes for OpEscape.
  ///
  /// \return The raw escape bytes for OpEscape.
  StringRef getValues() const {
    assert(Operation == OpEscape);
    auto &Values = std::get<EscapeFields>(ExtraFields).Values;
    return StringRef(&Values[0], Values.size());
  }

  /// Return the optional comment for OpEscape.
  ///
  /// \return The optional comment for OpEscape.
  StringRef getComment() const {
    assert(Operation == OpEscape);
    return std::get<EscapeFields>(ExtraFields).Comment;
  }
  /// Return the source location associated with this CFI instruction.
  ///
  /// \return The source location associated with this CFI instruction.
  SMLoc getLoc() const { return Loc; }

  /// Replaces in place all references to FromReg with ToReg.
  ///
  /// \param FromReg - Register number to replace.
  /// \param ToReg - Register number that replaces \p FromReg.
  LLVM_ABI void replaceRegister(unsigned FromReg, unsigned ToReg);
};

/// Frame metadata and CFI instructions for one function or EH frame.
struct MCDwarfFrameInfo {
  /// Construct an empty frame info.
  MCDwarfFrameInfo() = default;

  /// Symbol marking the beginning of this frame.
  MCSymbol *Begin = nullptr;
  /// Symbol marking the end of this frame.
  MCSymbol *End = nullptr;
  /// Personality routine symbol for this frame, if any.
  const MCSymbol *Personality = nullptr;
  /// Language-specific data area symbol for this frame, if any.
  const MCSymbol *Lsda = nullptr;
  /// CFI instructions that describe this frame.
  std::vector<MCCFIInstruction> Instructions;
  /// Current CFA register while building this frame.
  unsigned CurrentCfaRegister = 0;
  /// DWARF encoding used for the personality pointer.
  unsigned PersonalityEncoding = 0;
  /// DWARF encoding used for the LSDA pointer.
  unsigned LsdaEncoding = 0;
  /// Compact unwind encoding for this frame, if available.
  uint64_t CompactUnwindEncoding = 0;
  /// True if this is a signal-handling frame.
  bool IsSignalFrame = false;
  /// True if this is a simple frame without full unwind info.
  bool IsSimple = false;
  /// Return-address register for this frame.
  unsigned RAReg = static_cast<unsigned>(INT_MAX);
  /// True if this frame uses the B key for pointer authentication.
  bool IsBKeyFrame = false;
  /// True if this frame is tagged for Memory Tagging Extension.
  bool IsMTETaggedFrame = false;
};

/// Emits DWARF call-frame and compact-unwind information.
///
/// Emit DWARF call frame information and, when available, compact unwind
/// information.
class MCDwarfFrameEmitter {
public:
  /// Emit call-frame information for all recorded frames.
  ///
  /// \param streamer - Object streamer that emits CIE/FDE data.
  /// \param isEH - True when emitting EH frame info rather than debug frame.
  LLVM_ABI static void emit(MCObjectStreamer &streamer, bool isEH);
  /// Encode a DWARF CFA advance location instruction for AddrDelta.
  ///
  /// The address delta is scaled by the minimum instruction length and
  /// written to OS using the most compact DW_CFA_advance_loc* encoding.
  ///
  /// \param Context - Assembler context providing DWARF emit parameters.
  /// \param AddrDelta - Address delta to encode.
  /// \param OS - Output buffer that receives the encoded bytes.
  LLVM_ABI static void encodeAdvanceLoc(MCContext &Context, uint64_t AddrDelta,
                                        SmallVectorImpl<char> &OS);
};

} // end namespace llvm

#endif // LLVM_MC_MCDWARF_H
