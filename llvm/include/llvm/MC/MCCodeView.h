//===- MCCodeView.h - Machine Code CodeView support -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Holds state from .cv_file and .cv_loc directives for later emission.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCCODEVIEW_H
#define LLVM_MC_MCCODEVIEW_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include <deque>
#include <map>
#include <vector>

namespace llvm {
class MCAssembler;
class MCCVDefRangeFragment;
class MCCVInlineLineTableFragment;
class MCFragment;
class MCSection;
class MCSymbol;
class MCContext;
class MCObjectStreamer;
class MCStreamer;

/// Instances of this class represent the information from a
/// .cv_loc directive.
class MCCVLoc {
  const MCSymbol *Label = nullptr;
  uint32_t FunctionId;
  uint32_t FileNum;
  uint32_t Line;
  uint16_t Column;
  uint16_t PrologueEnd : 1;
  uint16_t IsStmt : 1;

private: // CodeViewContext manages these
  friend class CodeViewContext;
  MCCVLoc(const MCSymbol *Label, unsigned functionid, unsigned fileNum,
          unsigned line, unsigned column, bool prologueend, bool isstmt)
      : Label(Label), FunctionId(functionid), FileNum(fileNum), Line(line),
        Column(column), PrologueEnd(prologueend), IsStmt(isstmt) {}

  // Allow the default copy constructor and assignment operator to be used
  // for an MCCVLoc object.

public:
  /// Get the Label of this MCCVLoc.
  ///
  /// \return The Label of this MCCVLoc.
  const MCSymbol *getLabel() const { return Label; }

  /// Get the FunctionId of this MCCVLoc.
  ///
  /// \return The FunctionId of this MCCVLoc.
  unsigned getFunctionId() const { return FunctionId; }

  /// Get the FileNum of this MCCVLoc.
  ///
  /// \return The FileNum of this MCCVLoc.
  unsigned getFileNum() const { return FileNum; }

  /// Get the Line of this MCCVLoc.
  ///
  /// \return The Line of this MCCVLoc.
  unsigned getLine() const { return Line; }

  /// Get the Column of this MCCVLoc.
  ///
  /// \return The Column of this MCCVLoc.
  unsigned getColumn() const { return Column; }

  /// Return true if this location marks a prologue end.
  ///
  /// \return True if this location marks a prologue end.
  bool isPrologueEnd() const { return PrologueEnd; }
  /// Return true if this location is a recommended breakpoint (is_stmt).
  ///
  /// \return True if this location is a recommended breakpoint (is_stmt).
  bool isStmt() const { return IsStmt; }

  /// Set the Label of this MCCVLoc.
  ///
  /// \param L - Symbol marking the code address for this location.
  void setLabel(const MCSymbol *L) { Label = L; }

  /// Set the FunctionId of this MCCVLoc.
  ///
  /// \param FID - Function or inlined call site id for this location.
  void setFunctionId(unsigned FID) { FunctionId = FID; }

  /// Set the FileNum of this MCCVLoc.
  ///
  /// \param fileNum - File number of the source location.
  void setFileNum(unsigned fileNum) { FileNum = fileNum; }

  /// Set the Line of this MCCVLoc.
  ///
  /// \param line - Line number of the source location.
  void setLine(unsigned line) { Line = line; }

  /// Set the Column of this MCCVLoc.
  ///
  /// \param column - Column number of the source location.
  void setColumn(unsigned column) {
    assert(column <= UINT16_MAX);
    Column = column;
  }

  /// Set whether this location marks a prologue end.
  ///
  /// \param PE - True if this is a prologue-end location.
  void setPrologueEnd(bool PE) { PrologueEnd = PE; }
  /// Set whether this location is a recommended breakpoint (is_stmt).
  ///
  /// \param IS - True if this is a statement boundary.
  void setIsStmt(bool IS) { IsStmt = IS; }
};

/// Information describing a function or inlined call site.
///
/// Introduced by .cv_func_id or .cv_inline_site_id. Accumulates information
/// from .cv_loc directives used with this function's id or the id of an
/// inlined call site within this function or inlined call site.
struct MCCVFunctionInfo {
  /// Parent function id encoding for inlined call sites.
  ///
  /// If this represents an inlined call site, then ParentFuncIdPlusOne will be
  /// the parent function id plus one. If this represents a normal function,
  /// then there is no parent, and ParentFuncIdPlusOne will be FunctionSentinel.
  /// If this struct is an unallocated slot in the function info vector, then
  /// ParentFuncIdPlusOne will be zero.
  unsigned ParentFuncIdPlusOne = 0;

  /// Sentinel values used when encoding parent function ids.
  enum : unsigned {
    /// Value of ParentFuncIdPlusOne for a normal (non-inlined) function.
    FunctionSentinel = ~0U
  };

  /// Source location used as an "inlined at" site.
  struct LineInfo {
    /// File number of the inlined-at location.
    unsigned File;
    /// Line number of the inlined-at location.
    unsigned Line;
    /// Column number of the inlined-at location.
    unsigned Col;
  };

  /// "Inlined at" location when this is an inlined call site.
  LineInfo InlinedAt;

  /// The section of the first .cv_loc directive used for this function, or null
  /// if none has been seen yet.
  MCSection *Section = nullptr;

  /// Map from inlined call site id to its collapsed "inlined at" location.
  ///
  /// Call chains are collapsed, so for the call chain 'f -> g -> h', the
  /// InlinedAtMap of 'f' will contain entries for 'g' and 'h' that both list
  /// the line info for the 'g' call site.
  DenseMap<unsigned, LineInfo> InlinedAtMap;

  /// Returns true if this is function info has not yet been used in a
  /// .cv_func_id or .cv_inline_site_id directive.
  ///
  /// \return True if this function info has not yet been used.
  bool isUnallocatedFunctionInfo() const { return ParentFuncIdPlusOne == 0; }

  /// Returns true if this represents an inlined call site, meaning
  /// ParentFuncIdPlusOne is neither zero nor ~0U.
  ///
  /// \return True if this represents an inlined call site.
  bool isInlinedCallSite() const {
    return !isUnallocatedFunctionInfo() &&
           ParentFuncIdPlusOne != FunctionSentinel;
  }

  /// Return the parent function id of this inlined call site.
  ///
  /// \return The parent function id of this inlined call site.
  unsigned getParentFuncId() const {
    assert(isInlinedCallSite());
    return ParentFuncIdPlusOne - 1;
  }
};

/// Holds state from .cv_file and .cv_loc directives for later emission.
class CodeViewContext {
public:
  /// Construct a CodeView context bound to \p MCCtx.
  ///
  /// \param MCCtx - Assembler context used for symbols and allocation.
  CodeViewContext(MCContext *MCCtx) : MCCtx(MCCtx) {}

  /// Deleted copy assignment.
  ///
  /// \param other - Source context that would be copied from.
  CodeViewContext &operator=(const CodeViewContext &other) = delete;
  /// Deleted copy constructor.
  ///
  /// \param other - Source context that would be copied from.
  CodeViewContext(const CodeViewContext &other) = delete;

  /// Finalize CodeView state before object emission.
  LLVM_ABI void finish();

  /// Return true if \p FileNumber refers to a recorded .cv_file.
  ///
  /// \param FileNumber - File number to validate.
  /// \return True if \p FileNumber refers to a recorded .cv_file.
  LLVM_ABI bool isValidFileNumber(unsigned FileNumber) const;
  /// Record a .cv_file entry and return true on success.
  ///
  /// \param OS - Streamer used when creating related symbols.
  /// \param FileNumber - File number assigned by the directive.
  /// \param Filename - Path recorded for this file.
  /// \param ChecksumBytes - Optional checksum bytes for the file.
  /// \param ChecksumKind - CodeView checksum kind identifier.
  /// \return True on success; false if the file number is invalid or already
  /// used.
  LLVM_ABI bool addFile(MCStreamer &OS, unsigned FileNumber, StringRef Filename,
                        ArrayRef<uint8_t> ChecksumBytes, uint8_t ChecksumKind);

  /// Records the function id of a normal function. Returns false if the
  /// function id has already been used, and true otherwise.
  ///
  /// \param FuncId - Function id from a .cv_func_id directive.
  /// \return False if the function id has already been used; true otherwise.
  LLVM_ABI bool recordFunctionId(unsigned FuncId);

  /// Record the function id of an inlined call site.
  ///
  /// Records the "inlined at" location info of the call site, including what
  /// function or inlined call site it was inlined into. Returns false if the
  /// function id has already been used, and true otherwise.
  ///
  /// \param FuncId - Function id from a .cv_inline_site_id directive.
  /// \param IAFunc - Function or site id this call was inlined into.
  /// \param IAFile - File number of the inlined-at location.
  /// \param IALine - Line number of the inlined-at location.
  /// \param IACol - Column number of the inlined-at location.
  /// \return False if the function id has already been used; true otherwise.
  LLVM_ABI bool recordInlinedCallSiteId(unsigned FuncId, unsigned IAFunc,
                                        unsigned IAFile, unsigned IALine,
                                        unsigned IACol);

  /// Retreive the function info if this is a valid function id, or nullptr.
  ///
  /// \param FuncId - Function or inlined call site id to look up.
  /// \return The function info for \p FuncId, or nullptr if invalid.
  LLVM_ABI MCCVFunctionInfo *getCVFunctionInfo(unsigned FuncId);

  /// Record information from the currently parsed .cv_loc directive.
  ///
  /// Saves the information and sets CVLocSeen. When the next instruction is
  /// assembled an entry in the line number table with this information and the
  /// address of the instruction will be created.
  ///
  /// \param Ctx - Assembler context used when recording the location.
  /// \param Label - Symbol marking the code address for this location.
  /// \param FunctionId - Function or inlined call site id for this location.
  /// \param FileNo - File number of the source location.
  /// \param Line - Line number of the source location.
  /// \param Column - Column number of the source location.
  /// \param PrologueEnd - True if this marks a prologue end.
  /// \param IsStmt - True if this is a recommended breakpoint.
  LLVM_ABI void recordCVLoc(MCContext &Ctx, const MCSymbol *Label,
                            unsigned FunctionId, unsigned FileNo, unsigned Line,
                            unsigned Column, bool PrologueEnd, bool IsStmt);

  /// Add a line entry.
  ///
  /// \param LineEntry - Line location to append to the table.
  LLVM_ABI void addLineEntry(const MCCVLoc &LineEntry);

  /// Return the line entries for \p FuncId, including synthetic inlined-at
  /// sites.
  ///
  /// \param FuncId - Function id whose line entries are requested.
  /// \return The line entries for \p FuncId, including synthetic inlined-at
  /// sites.
  LLVM_ABI std::vector<MCCVLoc> getFunctionLineEntries(unsigned FuncId);

  /// Return the [begin, end) index range of .cv_loc entries for \p FuncId.
  ///
  /// \param FuncId - Function id whose line extent is requested.
  /// \return The [begin, end) index range of .cv_loc entries for \p FuncId.
  LLVM_ABI std::pair<size_t, size_t> getLineExtent(unsigned FuncId);
  /// Return the line extent of \p FuncId including inlined call sites.
  ///
  /// \param FuncId - Function id whose inclusive line extent is requested.
  /// \return The [begin, end) index range covering \p FuncId and its inlinees.
  LLVM_ABI std::pair<size_t, size_t>
  getLineExtentIncludingInlinees(unsigned FuncId);

  /// Return the .cv_loc entries in the half-open index range [\p L, \p R).
  ///
  /// \param L - Inclusive begin index into the line table.
  /// \param R - Exclusive end index into the line table.
  /// \return The .cv_loc entries in the requested index range.
  LLVM_ABI ArrayRef<MCCVLoc> getLinesForExtent(size_t L, size_t R);

  /// Emits a line table substream.
  ///
  /// \param OS - Object streamer to emit into.
  /// \param FuncId - Function id whose line table is emitted.
  /// \param FuncBegin - Symbol at the start of the function.
  /// \param FuncEnd - Symbol at the end of the function.
  LLVM_ABI void emitLineTableForFunction(MCObjectStreamer &OS, unsigned FuncId,
                                         const MCSymbol *FuncBegin,
                                         const MCSymbol *FuncEnd);

  /// Emit an inline line table fragment for an inlined call site.
  ///
  /// \param OS - Object streamer to emit into.
  /// \param PrimaryFunctionId - Inlined call site function id.
  /// \param SourceFileId - File number of the inlinee source location.
  /// \param SourceLineNum - Line number of the inlinee source location.
  /// \param FnStartSym - Symbol at the start of the inlined range.
  /// \param FnEndSym - Symbol at the end of the inlined range.
  LLVM_ABI void emitInlineLineTableForFunction(MCObjectStreamer &OS,
                                               unsigned PrimaryFunctionId,
                                               unsigned SourceFileId,
                                               unsigned SourceLineNum,
                                               const MCSymbol *FnStartSym,
                                               const MCSymbol *FnEndSym);

  /// Encodes the binary annotations once we have a layout.
  ///
  /// \param Asm - Assembler providing layout information.
  /// \param F - Inline line table fragment to encode.
  LLVM_ABI void encodeInlineLineTable(const MCAssembler &Asm,
                                      MCCVInlineLineTableFragment &F);

  /// Emit a CodeView def-range fragment covering \p Ranges.
  ///
  /// \param OS - Object streamer to emit into.
  /// \param Ranges - Pairs of begin/end symbols describing address ranges.
  /// \param FixedSizePortion - Fixed-size header bytes for the def-range.
  LLVM_ABI void
  emitDefRange(MCObjectStreamer &OS,
               ArrayRef<std::pair<const MCSymbol *, const MCSymbol *>> Ranges,
               StringRef FixedSizePortion);

  /// Encode a previously emitted def-range fragment once layout is known.
  ///
  /// \param Asm - Assembler providing layout information.
  /// \param F - Def-range fragment to encode.
  LLVM_ABI void encodeDefRange(const MCAssembler &Asm, MCCVDefRangeFragment &F);

  /// Emits the string table substream.
  ///
  /// \param OS - Object streamer to emit into.
  LLVM_ABI void emitStringTable(MCObjectStreamer &OS);

  /// Emits the file checksum substream.
  ///
  /// \param OS - Object streamer to emit into.
  LLVM_ABI void emitFileChecksums(MCObjectStreamer &OS);

  /// Emits the offset into the checksum table of the given file number.
  ///
  /// \param OS - Object streamer to emit into.
  /// \param FileNo - File number whose checksum offset is emitted.
  LLVM_ABI void emitFileChecksumOffset(MCObjectStreamer &OS, unsigned FileNo);

  /// Add something to the string table.  Returns the final string as well as
  /// offset into the string table.
  ///
  /// \param S - String to insert or look up in the table.
  /// \return The canonical string and its offset in the string table.
  LLVM_ABI std::pair<StringRef, unsigned> addToStringTable(StringRef S);

private:
  MCContext *MCCtx;

  /// Map from string to string table offset.
  StringMap<unsigned> StringTable;

  /// The fragment that ultimately holds our strings.
  MCFragment *StrTabFragment = nullptr;
  SmallVector<char, 0> StrTab = {'\0'};

  /// Get a string table offset.
  unsigned getStringTableOffset(StringRef S);

  struct FileInfo {
    unsigned StringTableOffset;

    // Indicates if this FileInfo corresponds to an actual file, or hasn't been
    // set yet.
    bool Assigned = false;

    uint8_t ChecksumKind;

    ArrayRef<uint8_t> Checksum;

    // Checksum offset stored as a symbol because it might be requested
    // before it has been calculated, so a fixup may be needed.
    MCSymbol *ChecksumTableOffset;
  };

  /// Array storing added file information.
  SmallVector<FileInfo, 4> Files;

  /// The offset of the first and last .cv_loc directive for a given function
  /// id.
  std::map<unsigned, std::pair<size_t, size_t>> MCCVLineStartStop;

  /// A collection of MCCVLoc for each section.
  std::vector<MCCVLoc> MCCVLines;

  /// All known functions and inlined call sites, indexed by function id.
  std::vector<MCCVFunctionInfo> Functions;

  /// Indicate whether we have already laid out the checksum table addresses or
  /// not.
  bool ChecksumOffsetsAssigned = false;

  /// Append-only storage of MCCVDefRangeFragment::Ranges.
  std::deque<SmallVector<std::pair<const MCSymbol *, const MCSymbol *>, 0>>
      DefRangeStorage;
};

} // end namespace llvm
#endif
