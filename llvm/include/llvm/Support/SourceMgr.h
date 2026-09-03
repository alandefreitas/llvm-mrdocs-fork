//===- SourceMgr.h - Manager for Source Buffers & Diagnostics ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the SMDiagnostic and SourceMgr classes.  This
// provides a simple substrate for diagnostics, #include handling, and other low
// level things for simple parsers.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_SOURCEMGR_H
#define LLVM_SUPPORT_SOURCEMGR_H

#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SMLoc.h"
#include <vector>

namespace llvm {

namespace vfs {
class FileSystem;
} // end namespace vfs

class raw_ostream;
class SMDiagnostic;
class SMFixIt;

/// This owns the files read by a parser, handles include stacks,
/// and handles diagnostic wrangling.
class SourceMgr {
public:
  /// Severity of a diagnostic message.
  enum DiagKind {
    DK_Error,   ///< Unrecoverable error.
    DK_Warning, ///< Warning that does not prevent further processing.
    DK_Remark,  ///< Informational remark.
    DK_Note,    ///< Note attached to a previous diagnostic.
  };

  /// Function pointer type for custom diagnostic handlers.
  ///
  /// Clients that want to handle their own diagnostics in a custom way can
  /// register a function pointer+context as a diagnostic handler. It gets
  /// called each time PrintMessage is invoked.
  using DiagHandlerTy = void (*)(const SMDiagnostic &, void *Context);

private:
  struct SrcBuffer {
    /// The memory buffer for the file.
    std::unique_ptr<MemoryBuffer> Buffer;

    /// Vector of offsets into Buffer at which there are line-endings
    /// (lazily populated). Once populated, the '\n' that marks the end of
    /// line number N from [1..] is at Buffer[OffsetCache[N-1]]. Since
    /// these offsets are in sorted (ascending) order, they can be
    /// binary-searched for the first one after any given offset (eg. an
    /// offset corresponding to a particular SMLoc).
    ///
    /// Since we're storing offsets into relatively small files (often smaller
    /// than 2^8 or 2^16 bytes), we select the offset vector element type
    /// dynamically based on the size of Buffer.
    mutable void *OffsetCache = nullptr;

    /// Look up a given \p Ptr in the buffer, determining which line and column
    /// it came from. This method has O(log n) complexity, where n is the number
    /// of lines in the buffer.
    LLVM_ABI std::pair<unsigned, unsigned>
    getLineAndColumn(const char *Ptr) const;
    template <typename T>
    std::pair<unsigned, unsigned>
    getLineAndColumnSpecialized(const char *Ptr) const;

    /// Return a pointer to the first character of the specified line number or
    /// null if the line number is invalid.
    LLVM_ABI const char *getPointerForLineNumber(unsigned LineNo) const;
    template <typename T>
    const char *getPointerForLineNumberSpecialized(unsigned LineNo) const;

    /// This is the location of the parent include, or null if at the top level.
    SMLoc IncludeLoc;

    SrcBuffer() = default;
    LLVM_ABI SrcBuffer(SrcBuffer &&);
    SrcBuffer(const SrcBuffer &) = delete;
    SrcBuffer &operator=(const SrcBuffer &) = delete;
    LLVM_ABI ~SrcBuffer();
  };

  /// This is all of the buffers that we are reading from.
  std::vector<SrcBuffer> Buffers;

  // This is the list of directories we should search for include files in.
  std::vector<std::string> IncludeDirectories;

  DiagHandlerTy DiagHandler = nullptr;
  void *DiagContext = nullptr;

  // Optional file system for finding include files.
  IntrusiveRefCntPtr<vfs::FileSystem> FS;

  bool isValidBufferID(unsigned i) const { return i && i <= Buffers.size(); }

public:
  /// Create new source manager without support for include files.
  LLVM_ABI SourceMgr();
  /// Create new source manager with the capability of finding include files
  /// via the provided file system.
  ///
  /// \param FS Virtual file system used to locate include files.
  LLVM_ABI explicit SourceMgr(IntrusiveRefCntPtr<vfs::FileSystem> FS);
  /// Deleted copy constructor.
  ///
  /// \param Other Unused; copy construction is deleted.
  SourceMgr(const SourceMgr &Other) = delete;
  /// Deleted copy assignment.
  ///
  /// \param Other Unused; copy assignment is deleted.
  SourceMgr &operator=(const SourceMgr &Other) = delete;
  /// Move-construct a source manager from \p Other.
  ///
  /// \param Other Source manager to move from.
  LLVM_ABI SourceMgr(SourceMgr &&Other);
  /// Move-assign a source manager from \p Other.
  ///
  /// \param Other Source manager to move from.
  /// @return A reference to this source manager.
  LLVM_ABI SourceMgr &operator=(SourceMgr &&Other);
  /// Destroy this source manager and its owned buffers.
  LLVM_ABI ~SourceMgr();

  /// Return the virtual file system used to find include files.
  ///
  /// @return The virtual file system used to find include files.
  LLVM_ABI IntrusiveRefCntPtr<vfs::FileSystem> getVirtualFileSystem() const;
  /// Set the virtual file system used to find include files.
  ///
  /// \param FS Virtual file system to use for include lookup.
  LLVM_ABI void setVirtualFileSystem(IntrusiveRefCntPtr<vfs::FileSystem> FS);

  /// Return the include directories of this source manager.
  ///
  /// @return The include directories of this source manager.
  ArrayRef<std::string> getIncludeDirs() const { return IncludeDirectories; }

  /// Replace the include search path with \p Dirs.
  ///
  /// \param Dirs Directories searched when opening include files.
  void setIncludeDirs(const std::vector<std::string> &Dirs) {
    IncludeDirectories = Dirs;
  }

  /// Specify a diagnostic handler to be invoked every time PrintMessage is
  /// called. \p Ctx is passed into the handler when it is invoked.
  ///
  /// \param DH Diagnostic handler to install, or null for the default.
  /// \param Ctx Opaque context pointer passed to \p DH.
  void setDiagHandler(DiagHandlerTy DH, void *Ctx = nullptr) {
    DiagHandler = DH;
    DiagContext = Ctx;
  }

  /// Return the currently installed diagnostic handler, if any.
  ///
  /// @return The currently installed diagnostic handler, or null.
  DiagHandlerTy getDiagHandler() const { return DiagHandler; }
  /// Return the opaque context pointer passed to the diagnostic handler.
  ///
  /// @return The opaque context pointer passed to the diagnostic handler.
  void *getDiagContext() const { return DiagContext; }

  /// Return buffer metadata for the buffer with ID \p i.
  ///
  /// \param i One-based buffer identifier.
  /// @return Buffer metadata for buffer ID \p i.
  const SrcBuffer &getBufferInfo(unsigned i) const {
    assert(isValidBufferID(i));
    return Buffers[i - 1];
  }

  /// Return the memory buffer for the buffer with ID \p i.
  ///
  /// \param i One-based buffer identifier.
  /// @return The memory buffer for buffer ID \p i.
  const MemoryBuffer *getMemoryBuffer(unsigned i) const {
    assert(isValidBufferID(i));
    return Buffers[i - 1].Buffer.get();
  }

  /// Return the number of source buffers owned by this manager.
  ///
  /// @return The number of source buffers owned by this manager.
  unsigned getNumBuffers() const { return Buffers.size(); }

  /// Return the one-based ID of the main (first) source buffer.
  ///
  /// @return The one-based ID of the main (first) source buffer.
  unsigned getMainFileID() const {
    assert(getNumBuffers());
    return 1;
  }

  /// Return the include location of the parent of buffer \p i.
  ///
  /// \param i One-based buffer identifier.
  /// @return The include location of the parent buffer, or a null location at
  /// the top level.
  SMLoc getParentIncludeLoc(unsigned i) const {
    assert(isValidBufferID(i));
    return Buffers[i - 1].IncludeLoc;
  }

  /// Add a new source buffer to this source manager. This takes ownership of
  /// the memory buffer.
  ///
  /// \param F Memory buffer to take ownership of.
  /// \param IncludeLoc Location of the include that introduced this buffer,
  /// or a null location for a top-level buffer.
  /// @return The one-based buffer ID assigned to the new buffer.
  unsigned AddNewSourceBuffer(std::unique_ptr<MemoryBuffer> F,
                              SMLoc IncludeLoc) {
    SrcBuffer NB;
    NB.Buffer = std::move(F);
    NB.IncludeLoc = IncludeLoc;
    Buffers.push_back(std::move(NB));
    return Buffers.size();
  }

  /// Append all source buffers from \p SrcMgr into this manager.
  ///
  /// `MainBufferIncludeLoc` is an optional include location to attach to the
  /// main buffer of `SrcMgr` after it gets moved to the current manager.
  ///
  /// \param SrcMgr Source manager whose buffers are moved into this one.
  /// \param MainBufferIncludeLoc Include location assigned to the first
  /// buffer taken from \p SrcMgr.
  void takeSourceBuffersFrom(SourceMgr &SrcMgr,
                             SMLoc MainBufferIncludeLoc = SMLoc()) {
    if (SrcMgr.Buffers.empty())
      return;

    size_t OldNumBuffers = getNumBuffers();
    std::move(SrcMgr.Buffers.begin(), SrcMgr.Buffers.end(),
              std::back_inserter(Buffers));
    SrcMgr.Buffers.clear();
    Buffers[OldNumBuffers].IncludeLoc = MainBufferIncludeLoc;
  }

  /// Search for a file with the specified name in the current directory or in
  /// one of the IncludeDirs.
  ///
  /// If no file is found, this returns 0, otherwise it returns the buffer ID
  /// of the stacked file. The full path to the included file can be found in
  /// \p IncludedFile.
  ///
  /// \param Filename Name of the include file to search for.
  /// \param IncludeLoc Location of the include directive.
  /// \param IncludedFile Set to the full path of the file that was opened.
  /// @return The one-based buffer ID of the included file, or 0 if not found.
  LLVM_ABI unsigned AddIncludeFile(const std::string &Filename,
                                   SMLoc IncludeLoc, std::string &IncludedFile);

  /// Open an include file without adding it to this SourceMgr.
  ///
  /// Search for a file with the specified name in the current directory or in
  /// one of the IncludeDirs, and try to open it **without** adding to the
  /// SourceMgr. If the opened file is intended to be added to the source
  /// manager, prefer `AddIncludeFile` instead.
  ///
  /// If no file is found, this returns an Error, otherwise it returns the
  /// buffer of the stacked file. The full path to the included file can be
  /// found in \p IncludedFile.
  ///
  /// \param Filename Name of the include file to search for.
  /// \param IncludedFile Set to the full path of the file that was opened.
  /// \param RequiresNullTerminator Whether the buffer must be null-terminated.
  /// @return The opened memory buffer, or an error if the file was not found.
  LLVM_ABI ErrorOr<std::unique_ptr<MemoryBuffer>>
  OpenIncludeFile(const std::string &Filename, std::string &IncludedFile,
                  bool RequiresNullTerminator = true);

  /// Return the ID of the buffer containing the specified location.
  ///
  /// 0 is returned if the buffer is not found.
  ///
  /// \param Loc Source location whose owning buffer is sought.
  /// @return The one-based buffer ID containing \p Loc, or 0 if not found.
  LLVM_ABI unsigned FindBufferContainingLoc(SMLoc Loc) const;

  /// Find the line number for the specified location in the specified file.
  /// This method has O(log n) complexity, where n is the number of lines in the
  /// buffer.
  ///
  /// \param Loc Source location to map to a line number.
  /// \param BufferID Buffer to search, or 0 to infer from \p Loc.
  /// @return The one-based line number for \p Loc.
  unsigned FindLineNumber(SMLoc Loc, unsigned BufferID = 0) const {
    return getLineAndColumn(Loc, BufferID).first;
  }

  /// Return the line and column for \p Loc in the given buffer.
  ///
  /// This method has O(log n) complexity, where n is the number of lines in the
  /// buffer.
  ///
  /// \param Loc Source location to map to line and column.
  /// \param BufferID Buffer to search, or 0 to infer from \p Loc.
  /// @return A pair of the one-based line and column for \p Loc.
  LLVM_ABI std::pair<unsigned, unsigned>
  getLineAndColumn(SMLoc Loc, unsigned BufferID = 0) const;

  /// Get a string with the \p SMLoc filename and line number
  /// formatted in the standard style.
  ///
  /// \param Loc Source location to format.
  /// \param IncludePath Whether to include the full file path in the result.
  /// @return A formatted location string with filename and line number.
  LLVM_ABI std::string
  getFormattedLocationNoOffset(SMLoc Loc, bool IncludePath = false) const;

  /// Given a line and column number in a mapped buffer, turn it into an SMLoc.
  /// This will return a null SMLoc if the line/column location is invalid.
  ///
  /// \param BufferID One-based buffer identifier.
  /// \param LineNo One-based line number within the buffer.
  /// \param ColNo One-based column number within the line.
  /// @return An SMLoc for the given line and column, or a null location if
  /// invalid.
  LLVM_ABI SMLoc FindLocForLineAndColumn(unsigned BufferID, unsigned LineNo,
                                         unsigned ColNo);

  /// Emit a message about the specified location with the specified string.
  ///
  /// \param OS Stream to write the diagnostic to.
  /// \param Loc Source location associated with the message.
  /// \param Kind Severity of the diagnostic.
  /// \param Msg Diagnostic message text.
  /// \param Ranges Optional underline ranges for the caret display.
  /// \param FixIts Optional fix-it hints to display.
  /// \param ShowColors Display colored messages if output is a terminal and
  /// the default error handler is used.
  LLVM_ABI void PrintMessage(raw_ostream &OS, SMLoc Loc, DiagKind Kind,
                             const Twine &Msg, ArrayRef<SMRange> Ranges = {},
                             ArrayRef<SMFixIt> FixIts = {},
                             bool ShowColors = true) const;

  /// Emits a diagnostic to llvm::errs().
  ///
  /// \param Loc Source location associated with the message.
  /// \param Kind Severity of the diagnostic.
  /// \param Msg Diagnostic message text.
  /// \param Ranges Optional underline ranges for the caret display.
  /// \param FixIts Optional fix-it hints to display.
  /// \param ShowColors Display colored messages if output is a terminal and
  /// the default error handler is used.
  LLVM_ABI void PrintMessage(SMLoc Loc, DiagKind Kind, const Twine &Msg,
                             ArrayRef<SMRange> Ranges = {},
                             ArrayRef<SMFixIt> FixIts = {},
                             bool ShowColors = true) const;

  /// Emits a manually-constructed diagnostic to the given output stream.
  ///
  /// \param OS Stream to write the diagnostic to.
  /// \param Diagnostic Diagnostic to print.
  /// \param ShowColors Display colored messages if output is a terminal and
  /// the default error handler is used.
  LLVM_ABI void PrintMessage(raw_ostream &OS, const SMDiagnostic &Diagnostic,
                             bool ShowColors = true) const;

  /// Return an SMDiagnostic at the specified location with the specified
  /// string.
  ///
  /// \param Loc Source location associated with the message.
  /// \param Kind Severity of the diagnostic.
  /// \param Msg If non-null, the kind of message (e.g., "error") which is
  /// prefixed to the message.
  /// \param Ranges Optional underline ranges for the caret display.
  /// \param FixIts Optional fix-it hints to attach.
  /// @return An SMDiagnostic describing the message at \p Loc.
  LLVM_ABI SMDiagnostic GetMessage(SMLoc Loc, DiagKind Kind, const Twine &Msg,
                                   ArrayRef<SMRange> Ranges = {},
                                   ArrayRef<SMFixIt> FixIts = {}) const;

  /// Print the include stack ending at \p IncludeLoc to \p OS.
  ///
  /// A diagnostic handler can use this before printing its custom formatted
  /// message.
  ///
  /// \param IncludeLoc The location of the include.
  /// \param OS the raw_ostream to print on.
  LLVM_ABI void PrintIncludeStack(SMLoc IncludeLoc, raw_ostream &OS) const;

  /// Prints the include stack of a buffer unless it is a macro instantiation
  /// buffer.
  ///
  /// \param Loc Source location whose include stack should be printed.
  /// \param OS Stream to write the include stack to.
  LLVM_ABI void printIncludeStackForDiagnostic(SMLoc Loc,
                                               raw_ostream &OS) const;
};

/// Represents a single fixit, a replacement of one range of text with another.
class SMFixIt {
  SMRange Range;

  std::string Text;

public:
  /// Construct a fix-it that replaces \p R with \p Replacement.
  ///
  /// \param R Source range to replace.
  /// \param Replacement Replacement text for \p R.
  LLVM_ABI SMFixIt(SMRange R, const Twine &Replacement);

  /// Construct a fix-it that inserts \p Replacement at \p Loc.
  ///
  /// \param Loc Insertion point (empty range at that location).
  /// \param Replacement Text to insert at \p Loc.
  SMFixIt(SMLoc Loc, const Twine &Replacement)
      : SMFixIt(SMRange(Loc, Loc), Replacement) {}

  /// Return the replacement text for this fix-it.
  ///
  /// @return The replacement text for this fix-it.
  StringRef getText() const { return Text; }
  /// Return the source range replaced by this fix-it.
  ///
  /// @return The source range replaced by this fix-it.
  SMRange getRange() const { return Range; }

  /// Order fix-its by range start, then end, then replacement text.
  ///
  /// \param Other Fix-it to compare against.
  /// @return True if this fix-it sorts before \p Other.
  bool operator<(const SMFixIt &Other) const {
    if (Range.Start.getPointer() != Other.Range.Start.getPointer())
      return Range.Start.getPointer() < Other.Range.Start.getPointer();
    if (Range.End.getPointer() != Other.Range.End.getPointer())
      return Range.End.getPointer() < Other.Range.End.getPointer();
    return Text < Other.Text;
  }
};

/// Instances of this class encapsulate one diagnostic report, allowing
/// printing to a raw_ostream as a caret diagnostic.
class SMDiagnostic {
  const SourceMgr *SM = nullptr;
  SMLoc Loc;
  std::string Filename;
  int LineNo = 0;
  int ColumnNo = 0;
  SourceMgr::DiagKind Kind = SourceMgr::DK_Error;
  std::string Message, LineContents;
  std::vector<std::pair<unsigned, unsigned>> Ranges;
  SmallVector<SMFixIt, 4> FixIts;

public:
  /// Construct an empty (null) diagnostic.
  SMDiagnostic() = default;
  /// Construct a diagnostic with no source location.
  ///
  /// Used for diagnostics such as file-not-found or command-line argument
  /// errors.
  ///
  /// \param filename Display name for the file associated with the diagnostic.
  /// \param Knd Severity of the diagnostic.
  /// \param Msg Diagnostic message text.
  SMDiagnostic(StringRef filename, SourceMgr::DiagKind Knd, StringRef Msg)
      : Filename(filename), LineNo(-1), ColumnNo(-1), Kind(Knd), Message(Msg) {}

  /// Construct a diagnostic with a source location and caret ranges.
  ///
  /// \param sm Source manager that owns the buffers for this diagnostic.
  /// \param L Source location of the diagnostic.
  /// \param FN Filename displayed in the diagnostic.
  /// \param Line One-based line number, or -1 if unknown.
  /// \param Col One-based column number, or -1 if unknown.
  /// \param Kind Severity of the diagnostic.
  /// \param Msg Diagnostic message text.
  /// \param LineStr Contents of the source line for caret display.
  /// \param Ranges Column ranges to underline on the source line.
  /// \param FixIts Optional fix-it hints to display.
  LLVM_ABI SMDiagnostic(const SourceMgr &sm, SMLoc L, StringRef FN, int Line,
                        int Col, SourceMgr::DiagKind Kind, StringRef Msg,
                        StringRef LineStr,
                        ArrayRef<std::pair<unsigned, unsigned>> Ranges,
                        ArrayRef<SMFixIt> FixIts = {});

  /// Return the source manager associated with this diagnostic, if any.
  ///
  /// @return The source manager associated with this diagnostic, or null.
  const SourceMgr *getSourceMgr() const { return SM; }
  /// Return the source location of this diagnostic.
  ///
  /// @return The source location of this diagnostic.
  SMLoc getLoc() const { return Loc; }
  /// Return the filename displayed for this diagnostic.
  ///
  /// @return The filename displayed for this diagnostic.
  StringRef getFilename() const { return Filename; }
  /// Return the one-based line number, or -1 if unknown.
  ///
  /// @return The one-based line number, or -1 if unknown.
  int getLineNo() const { return LineNo; }
  /// Return the one-based column number, or -1 if unknown.
  ///
  /// @return The one-based column number, or -1 if unknown.
  int getColumnNo() const { return ColumnNo; }
  /// Return the severity of this diagnostic.
  ///
  /// @return The severity of this diagnostic.
  SourceMgr::DiagKind getKind() const { return Kind; }
  /// Return the diagnostic message text.
  ///
  /// @return The diagnostic message text.
  StringRef getMessage() const { return Message; }
  /// Return the source line contents used for caret display.
  ///
  /// @return The source line contents used for caret display.
  StringRef getLineContents() const { return LineContents; }
  /// Return the column ranges to underline on the source line.
  ///
  /// @return Column ranges to underline on the source line.
  ArrayRef<std::pair<unsigned, unsigned>> getRanges() const { return Ranges; }

  /// Append a fix-it hint to this diagnostic.
  ///
  /// \param Hint Fix-it to attach.
  void addFixIt(const SMFixIt &Hint) { FixIts.push_back(Hint); }

  /// Return the fix-it hints attached to this diagnostic.
  ///
  /// @return The fix-it hints attached to this diagnostic.
  ArrayRef<SMFixIt> getFixIts() const { return FixIts; }

  /// Print this diagnostic as a caret diagnostic to \p S.
  ///
  /// \param ProgName Optional program name prefix, or null to omit it.
  /// \param S Stream to write the diagnostic to.
  /// \param ShowColors Whether to use ANSI colors when \p S is a terminal.
  /// \param ShowKindLabel Whether to print the error/warning/note label.
  /// \param ShowLocation Whether to print the file:line:column prefix.
  LLVM_ABI void print(const char *ProgName, raw_ostream &S,
                      bool ShowColors = true, bool ShowKindLabel = true,
                      bool ShowLocation = true) const;
};

} // end namespace llvm

#endif // LLVM_SUPPORT_SOURCEMGR_H
