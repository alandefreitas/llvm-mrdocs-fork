//===- DIContext.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines DIContext, an abstract data structure that holds
// debug information data.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DICONTEXT_H
#define LLVM_DEBUGINFO_DICONTEXT_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

namespace llvm {

/// A format-neutral container for source line information.
struct DILineInfo {
  /// Marker appended when line information is approximate.
  static constexpr const char *const ApproxString = "(approximate)";
  /// Placeholder used when a file or function name cannot be resolved.
  static constexpr const char *const BadString = "<invalid>";
  /// addr2line-style placeholder used instead of `BadString`.
  static constexpr const char *const Addr2LineBadString = "??";
  /// Source file name for this location, or `BadString` if unknown.
  std::string FileName;
  /// Demangled or raw name of the function containing this location.
  std::string FunctionName;
  /// File name for the start of the enclosing function or inlined frame.
  std::string StartFileName;
  /// Full source text of the file named by `FileName`, when available.
  std::optional<StringRef> Source;
  /// Source text for this particular line when full-file `Source` is unavailable.
  std::optional<StringRef> LineSource;
  /// Source line number (1-based), or 0 if unknown.
  uint32_t Line = 0;
  /// Source column number (1-based), or 0 if unknown.
  uint32_t Column = 0;
  /// Starting line of the enclosing function or inlined frame, or 0.
  uint32_t StartLine = 0;
  /// Absolute start address of the enclosing function or inlined frame, if known.
  std::optional<uint64_t> StartAddress;

  // DWARF-specific.
  /// DWARF discriminator for this location within the line table.
  uint32_t Discriminator = 0;

  /// True when the reported line is an approximation, not an exact match.
  bool IsApproximateLine = false;
  /// Construct with invalid file/function names (`BadString`).
  DILineInfo()
      : FileName(BadString), FunctionName(BadString), StartFileName(BadString) {
  }

  /// True if line, column, names, start line, and discriminator match.
  /// \param RHS Line info to compare against.
  /// \return True if the compared fields are equal.
  bool operator==(const DILineInfo &RHS) const {
    return Line == RHS.Line && Column == RHS.Column &&
           FileName == RHS.FileName && FunctionName == RHS.FunctionName &&
           StartFileName == RHS.StartFileName && StartLine == RHS.StartLine &&
           Discriminator == RHS.Discriminator;
  }

  /// True if this line info differs from \p RHS under `operator==`.
  /// \param RHS Line info to compare against.
  /// \return True if the line infos differ.
  bool operator!=(const DILineInfo &RHS) const { return !(*this == RHS); }

  /// Lexicographically compare file, function, start file, line, column, start line, and discriminator.
  /// \param RHS Line info to compare against.
  /// \return True if this line info is ordered before \p RHS.
  bool operator<(const DILineInfo &RHS) const {
    return std::tie(FileName, FunctionName, StartFileName, Line, Column,
                    StartLine, Discriminator) <
           std::tie(RHS.FileName, RHS.FunctionName, RHS.StartFileName, RHS.Line,
                    RHS.Column, RHS.StartLine, RHS.Discriminator);
  }

  /// True if this is not a default-constructed invalid line info.
  /// \return True if this is not the default invalid line info.
  explicit operator bool() const { return *this != DILineInfo(); }

  /// Print this line info to \p OS in a human-readable form.
  /// \param OS Stream that receives the dump.
  void dump(raw_ostream &OS) {
    OS << "Line info: ";
    if (FileName != BadString)
      OS << "file '" << FileName << "', ";
    if (FunctionName != BadString)
      OS << "function '" << FunctionName << "', ";
    OS << "line " << Line << ", ";
    OS << "column " << Column << ", ";
    if (StartFileName != BadString)
      OS << "start file '" << StartFileName << "', ";
    OS << "start line " << StartLine << '\n';
  }
};

/// Table of absolute addresses paired with their DILineInfo.
using DILineInfoTable = SmallVector<std::pair<uint64_t, DILineInfo>, 16>;

/// A format-neutral container for inlined code description.
class DIInliningInfo {
  SmallVector<DILineInfo, 4> Frames;

public:
  /// Construct an empty inlining-info table with no frames.
  DIInliningInfo() = default;

  /// Returns the frame at `Index`. Frames are stored in bottom-up
  /// (leaf-to-root) order with increasing index.
  /// \param Index Frame index in leaf-to-root order.
  /// \return The frame at \p Index.
  const DILineInfo &getFrame(unsigned Index) const {
    assert(Index < Frames.size());
    return Frames[Index];
  }

  /// Return a mutable pointer to the frame at \p Index (leaf-to-root order).
  /// \param Index Frame index in leaf-to-root order.
  /// \return A mutable pointer to the frame at \p Index.
  DILineInfo *getMutableFrame(unsigned Index) {
    assert(Index < Frames.size());
    return &Frames[Index];
  }

  /// Number of inlining frames stored in bottom-up (leaf-to-root) order.
  /// \return The number of stored inlining frames.
  uint32_t getNumberOfFrames() const { return Frames.size(); }

  /// Append an inlining frame in bottom-up (leaf-to-root) order.
  /// \param Frame Line info for the frame to append.
  void addFrame(const DILineInfo &Frame) { Frames.push_back(Frame); }

  /// Resize the frame list to hold exactly \p i frames.
  /// \param i New number of frames to retain.
  void resize(unsigned i) { Frames.resize(i); }
};

/// Container for description of a global variable.
struct DIGlobal {
  /// Name of the global variable, or `DILineInfo::BadString` if unknown.
  std::string Name;
  /// Absolute start address of the global variable.
  uint64_t Start = 0;
  /// Size in bytes of the global variable.
  uint64_t Size = 0;
  /// Source file of the global's declaration.
  std::string DeclFile;
  /// Source line of the global's declaration, or 0 if unknown.
  uint64_t DeclLine = 0;

  /// Construct with an invalid name (`DILineInfo::BadString`).
  DIGlobal() : Name(DILineInfo::BadString) {}
};

/// Container for description of a local variable at an address.
struct DILocal {
  /// Name of the function that owns this local variable.
  std::string FunctionName;
  /// Name of the local variable.
  std::string Name;
  /// Source file of the local's declaration.
  std::string DeclFile;
  /// Source line of the local's declaration, or 0 if unknown.
  uint64_t DeclLine = 0;
  /// Offset from the frame base to this local, if known.
  std::optional<int64_t> FrameOffset;
  /// Size in bytes of the local variable, if known.
  std::optional<uint64_t> Size;
  /// DWARF tag offset associated with this local, if present.
  std::optional<uint64_t> TagOffset;
};

/// A DINameKind is passed to name search methods to specify a
/// preference regarding the type of name resolution the caller wants.
enum class DINameKind {
  None,        ///< Do not resolve a name.
  ShortName,   ///< Prefer the short (unmangled) name.
  LinkageName  ///< Prefer the linkage (mangled) name.
};

/// Controls which fields of DILineInfo container should be filled
/// with data.
struct DILineInfoSpecifier {
  /// How much path information to include for file/line lookups.
  enum class FileLineInfoKind {
    None, ///< Do not fill file or line path fields.
    /// Raw filename string as stored by the compiler (path form unspecified).
    RawValue,
    BaseNameOnly, ///< Only the base file name, without directories.
    /// Path relative to the compilation directory.
    RelativeFilePath,
    AbsoluteFilePath ///< Absolute path to the source file.
  };
  /// Alias for the preferred function-name resolution kind.
  using FunctionNameKind = DINameKind;
  /// Preference for how much file-path detail to report.
  FileLineInfoKind FLIKind;
  /// Preference for how to resolve function names.
  FunctionNameKind FNKind;
  /// Prefer approximate line info when exact line data is unavailable.
  bool ApproximateLine;

  /// Construct with the given file/line and function-name preferences.
  /// \param FLIKind Preference for how much file-path detail to report.
  /// \param FNKind Preference for how to resolve function names.
  /// \param ApproximateLine Prefer approximate line info when exact data is missing.
  DILineInfoSpecifier(FileLineInfoKind FLIKind = FileLineInfoKind::RawValue,
                      FunctionNameKind FNKind = FunctionNameKind::None,
                      bool ApproximateLine = false)
      : FLIKind(FLIKind), FNKind(FNKind), ApproximateLine(ApproximateLine) {}

  /// True if file/line and function-name preferences match \p RHS.
  /// \param RHS Specifier to compare against.
  /// \return True if the preferences match.
  inline bool operator==(const DILineInfoSpecifier &RHS) const {
    return FLIKind == RHS.FLIKind && FNKind == RHS.FNKind;
  }
};

/// This is just a helper to programmatically construct DIDumpType.
enum DIDumpTypeCounter {
#define HANDLE_DWARF_SECTION(ENUM_NAME, ELF_NAME, CMDLINE_NAME, OPTION)        \
  DIDT_ID_##ENUM_NAME,
#include "llvm/BinaryFormat/Dwarf.def"
#undef HANDLE_DWARF_SECTION
  DIDT_ID_UUID,  ///< Counter for the UUID dump-type bit.
  DIDT_ID_Count  ///< Number of dump-type counter entries.
};
static_assert(DIDT_ID_Count <= 32, "section types overflow storage");

/// Selects which debug sections get dumped.
enum DIDumpType : unsigned {
  DIDT_Null, ///< Dump no debug sections.
  DIDT_All = ~0U, ///< Dump every recognized debug section.
#define HANDLE_DWARF_SECTION(ENUM_NAME, ELF_NAME, CMDLINE_NAME, OPTION)        \
  DIDT_##ENUM_NAME = 1U << DIDT_ID_##ENUM_NAME,
#include "llvm/BinaryFormat/Dwarf.def"
#undef HANDLE_DWARF_SECTION
  DIDT_UUID = 1 << DIDT_ID_UUID, ///< Dump UUID / build-ID debug info.
};

/// Container for dump options that control which debug information will be
/// dumped.
struct DIDumpOptions {
  /// Bitmask of DIDumpType sections to dump; defaults to DIDT_All.
  unsigned DumpType = DIDT_All;
  /// Max child DIE dump recursion depth; \c -1U means unlimited.
  unsigned ChildRecurseDepth = -1U;
  /// Max parent DIE dump recursion depth; \c -1U means unlimited.
  unsigned ParentRecurseDepth = -1U;
  /// DWARF version to assume when extracting.
  uint16_t Version = 0;
  /// Address byte size to assume when extracting DWARF without a CU header.
  uint8_t AddrSize = 4; // Address byte size to assume when extracting.
  /// Include addresses in dump output when true.
  bool ShowAddresses = true;
  /// Recursively dump child DIEs when dumping a DIE.
  bool ShowChildren = false;
  /// Dump parent DIEs when dumping a DIE.
  bool ShowParents = false;
  /// When true, print each attribute's DWARF form alongside its value.
  bool ShowForm = false;
  /// Print a short summary of types instead of full type dumps.
  bool SummarizeTypes = false;
  /// Emit extra detail in dump output.
  bool Verbose = false;
  /// Dump raw section bytes in addition to interpreted content.
  bool DisplayRawContents = false;
  /// True when dumping exception-handling (.eh_frame) rather than .debug_frame.
  bool IsEH = false;
  /// Also dump non-skeleton (full) compile units linked from skeletons.
  bool DumpNonSkeleton = false;
  /// Print aggregated recoverable errors at the end of a dump.
  bool ShowAggregateErrors = false;
  /// Print only register names, omitting location-expression details.
  bool PrintRegisterOnly = false;
  /// Path to write a JSON summary of aggregated dump errors, if requested.
  std::string JsonErrSummaryFile;
  /// List of DWARF tags to filter children by.
  llvm::SmallVector<unsigned, 0> FilterChildTag;
  /// Optional callback that names a DWARF register number for dumping.
  std::function<llvm::StringRef(uint64_t DwarfRegNum, bool IsEH)>
      GetNameForDWARFReg;

  /// Return default option set for printing a single DIE without children.
  /// \return Options configured for dumping a single DIE.
  static DIDumpOptions getForSingleDIE() {
    DIDumpOptions Opts;
    Opts.ChildRecurseDepth = 0;
    Opts.ParentRecurseDepth = 0;
    return Opts;
  }

  /// Return the options with RecurseDepth set to 0 unless explicitly required.
  /// \return A copy with unlimited recurse depths clamped when children/parents are not shown.
  DIDumpOptions noImplicitRecursion() const {
    DIDumpOptions Opts = *this;
    if (ChildRecurseDepth == -1U && !ShowChildren)
      Opts.ChildRecurseDepth = 0;
    if (ParentRecurseDepth == -1U && !ShowParents)
      Opts.ParentRecurseDepth = 0;
    return Opts;
  }

  /// Handler invoked for recoverable dump/parse errors.
  std::function<void(Error)> RecoverableErrorHandler =
      WithColor::defaultErrorHandler;
  /// Handler invoked for non-fatal dump/parse warnings.
  std::function<void(Error)> WarningHandler = WithColor::defaultWarningHandler;
};

/// Abstract interface for querying and dumping debug information.
class DIContext {
public:
  /// Concrete debug-info backend implemented by this context.
  enum DIContextKind {
    CK_DWARF, ///< DWARF debug information.
    CK_PDB,   ///< Microsoft PDB debug information.
    CK_BTF,   ///< BPF Type Format (BTF) debug information.
    CK_GSYM,  ///< GSYM debug information.
  };

  /// Construct a context of kind \p K.
  /// \param K Concrete debug-info backend kind for this context.
  DIContext(DIContextKind K) : Kind(K) {}
  /// Virtual destructor.
  virtual ~DIContext() = default;

  /// Return the concrete debug-info backend kind of this context.
  /// \return The concrete debug-info backend kind.
  DIContextKind getKind() const { return Kind; }

  /// Dump debug information from this context to \p OS using \p DumpOpts.
  /// \param OS Stream that receives the dump.
  /// \param DumpOpts Options controlling which sections and detail to dump.
  virtual void dump(raw_ostream &OS, DIDumpOptions DumpOpts) = 0;

  /// Verify debug information, writing diagnostics to \p OS; return true on success.
  /// \param OS Stream that receives verification diagnostics.
  /// \param DumpOpts Options controlling verification detail.
  /// \return True if verification succeeds.
  virtual bool verify(raw_ostream &OS, DIDumpOptions DumpOpts = {}) {
    // No verifier? Just say things went well.
    return true;
  }

  /// Look up source line info for the instruction at \p Address.
  ///
  /// Returns std::nullopt when debug info is missing for the given address.
  /// \param Address Instruction address to look up.
  /// \param Specifier Controls which DILineInfo fields are filled.
  /// \return Line info for the address, or std::nullopt if unavailable.
  virtual std::optional<DILineInfo> getLineInfoForAddress(
      object::SectionedAddress Address,
      DILineInfoSpecifier Specifier = DILineInfoSpecifier()) = 0;
  /// Look up source line info for a data (variable) address.
  ///
  /// Returns std::nullopt when debug info is missing for the given address.
  /// \param Address Data address to look up.
  /// \return Line info for the data address, or std::nullopt if unavailable.
  virtual std::optional<DILineInfo>
  getLineInfoForDataAddress(object::SectionedAddress Address) = 0;
  /// Return line info for each instruction address in [Address, Address+Size).
  /// \param Address Start of the address range to query.
  /// \param Size Length in bytes of the address range.
  /// \param Specifier Controls which DILineInfo fields are filled.
  /// \return A table of addresses paired with their line info.
  virtual DILineInfoTable getLineInfoForAddressRange(
      object::SectionedAddress Address, uint64_t Size,
      DILineInfoSpecifier Specifier = DILineInfoSpecifier()) = 0;
  /// Return the inlining stack for the instruction at \p Address.
  /// \param Address Instruction address to look up.
  /// \param Specifier Controls which DILineInfo fields are filled.
  /// \return The inlining stack for the address.
  virtual DIInliningInfo getInliningInfoForAddress(
      object::SectionedAddress Address,
      DILineInfoSpecifier Specifier = DILineInfoSpecifier()) = 0;

  /// Return local variables whose live ranges cover \p Address.
  /// \param Address Code address whose live locals are requested.
  /// \return Local variables live at \p Address.
  virtual std::vector<DILocal>
  getLocalsForAddress(object::SectionedAddress Address) = 0;

private:
  const DIContextKind Kind;
};

/// An inferface for inquiring the load address of a loaded object file
/// to be used by the DIContext implementations when applying relocations
/// on the fly.
class LoadedObjectInfo {
protected:
  /// Construct a base loaded-object info used by derived helpers.
  LoadedObjectInfo() = default;
  /// Copy-construct loaded-object info state.
  /// \param Other Loaded-object info to copy.
  LoadedObjectInfo(const LoadedObjectInfo &Other) = default;

public:
  /// Destroy the loaded-object info.
  virtual ~LoadedObjectInfo() = default;

  /// Obtain the Load Address of a section by SectionRef.
  ///
  /// Calculate the address of the given section.
  /// The section need not be present in the local address space. The addresses
  /// need to be consistent with the addresses used to query the DIContext and
  /// the output of this function should be deterministic, i.e. repeated calls
  /// with the same Sec should give the same address.
  /// \param Sec Section whose load address is requested.
  /// \return The load address of \p Sec, or 0 by default.
  virtual uint64_t getSectionLoadAddress(const object::SectionRef &Sec) const {
    return 0;
  }

  /// If conveniently available, return the content of the given Section.
  ///
  /// When the section is available in the local address space, in relocated
  /// (loaded) form, e.g. because it was relocated by a JIT for execution, this
  /// function should provide the contents of said section in `Data`. If the
  /// loaded section is not available, or the cost of retrieving it would be
  /// prohibitive, this function should return false. In that case, relocations
  /// will be read from the local (unrelocated) object file and applied on the
  /// fly. Note that this method is used purely for optimzation purposes in the
  /// common case of JITting in the local address space, so returning false
  /// should always be correct.
  /// \param Sec Section whose loaded contents are requested.
  /// \param Data Set to the relocated section bytes when available.
  /// \return True if relocated section contents were provided in \p Data.
  virtual bool getLoadedSectionContents(const object::SectionRef &Sec,
                                        StringRef &Data) const {
    return false;
  }

  // FIXME: This is untested and unused anywhere in the LLVM project, it's
  // used/needed by Julia (an external project). It should have some coverage
  // (at least tests, but ideally example functionality).
  /// Obtain a copy of this LoadedObjectInfo.
  /// \return A newly allocated copy of this loaded-object info.
  virtual std::unique_ptr<LoadedObjectInfo> clone() const = 0;
};

/// CRTP helper that implements LoadedObjectInfo::clone for Derived.
template <typename Derived, typename Base = LoadedObjectInfo>
struct LoadedObjectInfoHelper : Base {
protected:
  /// Copy-construct the CRTP helper from another instance.
  /// \param Other Helper instance to copy.
  LoadedObjectInfoHelper(const LoadedObjectInfoHelper &Other) = default;
  /// Default-construct the CRTP helper and its Base.
  LoadedObjectInfoHelper() = default;

public:
  /// Forward construction arguments to the Base LoadedObjectInfo.
  /// \param Args Arguments forwarded to the Base constructor.
  template <typename... Ts>
  LoadedObjectInfoHelper(Ts &&...Args) : Base(std::forward<Ts>(Args)...) {}

  /// Clone this loaded-object info as a Derived instance.
  /// \return A unique_ptr to a Derived copy of this instance.
  std::unique_ptr<llvm::LoadedObjectInfo> clone() const override {
    return std::make_unique<Derived>(static_cast<const Derived &>(*this));
  }
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DICONTEXT_H
