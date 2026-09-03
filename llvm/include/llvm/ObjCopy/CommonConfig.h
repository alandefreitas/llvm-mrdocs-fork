//===- CommonConfig.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJCOPY_COMMONCONFIG_H
#define LLVM_OBJCOPY_COMMONCONFIG_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/CachedHashString.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/ELFTypes.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/GlobPattern.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Regex.h"
#include <optional>

namespace llvm {
namespace objcopy {

/// Object-file format selected for input or output.
enum class FileFormat {
  /// Format has not been specified by the user.
  Unspecified,
  /// ELF object format.
  ELF,
  /// Raw binary image.
  Binary,
  /// Intel HEX text format.
  IHex,
  /// Motorola S-record text format.
  SREC
};

/// Machine architecture description used when selecting an ELF output target.
///
/// Maps architecture names to ELF types and the \c e_machine value of the ELF
/// file.
struct MachineInfo {
  /// Construct from an ELF machine id, OS ABI, and word-size/endianness flags.
  /// \param EM ELF \c e_machine value.
  /// \param ABI ELF OS/ABI identifier.
  /// \param Is64 True when the target is 64-bit.
  /// \param IsLittle True when the target is little-endian.
  MachineInfo(uint16_t EM, uint8_t ABI, bool Is64, bool IsLittle)
      : EMachine(EM), OSABI(ABI), Is64Bit(Is64), IsLittleEndian(IsLittle) {}
  /// Construct from an ELF machine id and word-size/endianness, with OS ABI
  /// defaulted to \c ELFOSABI_NONE.
  /// \param EM ELF \c e_machine value.
  /// \param Is64 True when the target is 64-bit.
  /// \param IsLittle True when the target is little-endian.
  MachineInfo(uint16_t EM, bool Is64, bool IsLittle)
      : MachineInfo(EM, ELF::ELFOSABI_NONE, Is64, IsLittle) {}
  /// Construct with all fields unset (zero / false).
  MachineInfo() : MachineInfo(0, 0, false, false) {}
  /// ELF \c e_machine identifier for the target architecture.
  uint16_t EMachine;
  /// ELF OS/ABI identifier for the target.
  uint8_t OSABI;
  /// True when the target uses a 64-bit ELF class.
  bool Is64Bit;
  /// True when the target is little-endian.
  bool IsLittleEndian;
};

/// Section attribute flags set by \c --set-section-flags or \c --rename-section.
///
/// Interpretation of these is format-specific and not all flags are meaningful
/// for all object file formats. This is a bitmask; many section flags may be
/// set.
enum SectionFlag {
  /// No section flags are set.
  SecNone = 0,
  /// Section occupies memory during process execution.
  SecAlloc = 1 << 0,
  /// Section contents are loaded from the file image.
  SecLoad = 1 << 1,
  /// Section is not loaded from the file image.
  SecNoload = 1 << 2,
  /// Section is read-only at run time.
  SecReadonly = 1 << 3,
  /// Section holds debugging information.
  SecDebug = 1 << 4,
  /// Section contains executable machine code.
  SecCode = 1 << 5,
  /// Section contains initialized data.
  SecData = 1 << 6,
  /// Section resides in ROM.
  SecRom = 1 << 7,
  /// Section may be merged with compatible sections.
  SecMerge = 1 << 8,
  /// Section contains null-terminated strings.
  SecStrings = 1 << 9,
  /// Section has non-empty contents.
  SecContents = 1 << 10,
  /// Section may be shared across processes.
  SecShare = 1 << 11,
  /// Section is excluded from the final link output.
  SecExclude = 1 << 12,
  /// Section is marked as large (architecture-specific).
  SecLarge = 1 << 13,
  LLVM_MARK_AS_BITMASK_ENUM(/*LargestValue=*/SecLarge)
};

/// Describes a section rename, optionally with replacement flags.
struct SectionRename {
  /// Current name of the section to rename.
  StringRef OriginalName;
  /// Replacement name for the section.
  StringRef NewName;
  /// Optional replacement section flags applied with the rename.
  std::optional<SectionFlag> NewFlags;
};

/// Describes a request to replace the flags of a named section.
struct SectionFlagsUpdate {
  /// Name of the section whose flags are updated.
  StringRef Name;
  /// Replacement section flags to apply.
  SectionFlag NewFlags;
};

/// Local-symbol discard policy selected by \c --discard-all / \c --discard-locals.
enum class DiscardType {
  /// Do not discard local symbols.
  None,
  /// Discard all local symbols (\c --discard-all / \c -x).
  All,
  /// Discard temporary local symbols (\c --discard-locals / \c -X).
  Locals,
};

/// Matching style used when interpreting user-supplied name patterns.
enum class MatchStyle {
  /// Match names as exact literal strings (default for symbols).
  Literal,
  /// Match names as glob wildcards (default for sections, or \c --wildcard).
  Wildcard,
  /// Match names as regular expressions (\c --regex).
  Regex,
};

/// A single literal name, regex, or glob pattern used to match symbol/section
/// names.
class NameOrPattern {
  StringRef Name;
  // Regex is shared between multiple CommonConfig instances.
  std::shared_ptr<Regex> R;
  std::shared_ptr<GlobPattern> G;
  bool IsPositiveMatch = true;

  NameOrPattern(StringRef N) : Name(N) {}
  NameOrPattern(std::shared_ptr<Regex> R) : R(R) {}
  NameOrPattern(std::shared_ptr<GlobPattern> G, bool IsPositiveMatch)
      : G(G), IsPositiveMatch(IsPositiveMatch) {}

public:
  /// Create a matcher from \p Pattern using the given match style.
  ///
  /// \p ErrorCallback is used to handle recoverable errors. An Error returned
  /// by the callback aborts the parsing and is then returned by this function.
  /// \param Pattern User-supplied name or pattern string.
  /// \param MS Matching style that interprets \p Pattern.
  /// \param ErrorCallback Handler invoked for recoverable parse errors.
  /// \returns The constructed matcher, or an Error on failure.
  LLVM_ABI static Expected<NameOrPattern>
  create(StringRef Pattern, MatchStyle MS,
         llvm::function_ref<Error(Error)> ErrorCallback);

  /// Return true when this matcher selects matching names (not exclusions).
  /// \returns True for positive matchers; false for exclusion patterns.
  bool isPositiveMatch() const { return IsPositiveMatch; }
  /// Return the literal name when this matcher is not a regex or glob.
  /// \returns The literal name, or \c std::nullopt for pattern matchers.
  std::optional<StringRef> getName() const {
    if (!R && !G)
      return Name;
    return std::nullopt;
  }
  /// Return true when \p S matches this name or pattern.
  /// \param S Candidate symbol or section name.
  /// \returns True if \p S matches; false otherwise.
  bool operator==(StringRef S) const {
    return R ? R->match(S) : G ? G->match(S) : Name == S;
  }
  /// Return true when \p S does not match this name or pattern.
  /// \param S Candidate symbol or section name.
  /// \returns True if \p S does not match; false otherwise.
  bool operator!=(StringRef S) const { return !operator==(S); }
};

/// Matcher that checks symbol or section names against command-line patterns.
///
/// Accumulates positive and negative \c NameOrPattern entries provided for a
/// single option.
class NameMatcher {
  DenseSet<CachedHashStringRef> PosNames;
  SmallVector<NameOrPattern, 0> PosPatterns;
  SmallVector<NameOrPattern, 0> NegMatchers;

public:
  /// Add a positive or negative matcher produced by \ref NameOrPattern::create.
  /// \param Matcher Expected matcher to incorporate, or an Error to propagate.
  /// \returns Success, or the Error contained in \p Matcher.
  Error addMatcher(Expected<NameOrPattern> Matcher) {
    if (!Matcher)
      return Matcher.takeError();
    if (Matcher->isPositiveMatch()) {
      if (std::optional<StringRef> MaybeName = Matcher->getName())
        PosNames.insert(CachedHashStringRef(*MaybeName));
      else
        PosPatterns.push_back(std::move(*Matcher));
    } else {
      NegMatchers.push_back(std::move(*Matcher));
    }
    return Error::success();
  }
  /// Return true when \p S matches a positive entry and no negative entry.
  /// \param S Candidate symbol or section name.
  /// \returns True if \p S is selected by this matcher; false otherwise.
  bool matches(StringRef S) const {
    return (PosNames.contains(CachedHashStringRef(S)) ||
            is_contained(PosPatterns, S)) &&
           !is_contained(NegMatchers, S);
  }
  /// Return true when no positive or negative matchers have been added.
  /// \returns True if this matcher has no entries; false otherwise.
  bool empty() const {
    return PosNames.empty() && PosPatterns.empty() && NegMatchers.empty();
  }
};

/// How an address or LMA adjustment should be applied to a value.
enum class AdjustKind {
  /// Replace the existing value with the update.
  Set,
  /// Add the update to the existing value.
  Add,
  /// Subtract the update from the existing value.
  Subtract
};

/// Absolute or relative adjustment applied to a section address.
struct AddressUpdate {
  /// Magnitude of the address adjustment.
  uint64_t Value = 0;
  /// Whether \ref Value replaces, adds to, or subtracts from the address.
  AdjustKind Kind = AdjustKind::Add;
};

/// Address update applied to every section whose name matches a pattern.
struct SectionPatternAddressUpdate {
  /// Section-name matcher selecting which sections are updated.
  NameMatcher SectionPattern;
  /// Address adjustment applied to matching sections.
  AddressUpdate Update;
};

/// Symbol attribute flags accepted by \c --add-symbol.
enum class SymbolFlag {
  /// Symbol has global binding.
  Global,
  /// Symbol has local binding.
  Local,
  /// Symbol has weak binding.
  Weak,
  /// Symbol has default visibility.
  Default,
  /// Symbol has hidden visibility.
  Hidden,
  /// Symbol has protected visibility.
  Protected,
  /// Symbol refers to a source file.
  File,
  /// Symbol refers to a section.
  Section,
  /// Symbol refers to a data object.
  Object,
  /// Symbol refers to a function.
  Function,
  /// Symbol refers to an indirect function (IFUNC).
  IndirectFunction,
  /// Symbol is a debugging symbol.
  Debug,
  /// Symbol marks a constructor.
  Constructor,
  /// Symbol is a warning symbol.
  Warning,
  /// Symbol is an indirect reference.
  Indirect,
  /// Symbol is a linker-synthesized symbol.
  Synthetic,
  /// Symbol is a unique-object marker.
  UniqueObject,
};

/// Symbol description supplied by the \c --add-symbol option.
///
/// Symbol flags not supported by a concrete format should be ignored.
struct NewSymbolInfo {
  /// Name of the symbol to create.
  StringRef SymbolName;
  /// Section in which the new symbol is defined.
  StringRef SectionName;
  /// Value (typically offset) assigned to the new symbol.
  uint64_t Value = 0;
  /// Attribute flags applied to the new symbol.
  SmallVector<SymbolFlag, 0> Flags;
  /// Symbols before which the new symbol should be ordered, when supported.
  SmallVector<StringRef, 0> BeforeSyms;
};

/// Section name and body for a newly added or replaced section.
struct NewSectionInfo {
  /// Construct with an empty name and no section data.
  NewSectionInfo() = default;
  /// Construct from a section name and ownership of its contents.
  /// \param Name Name of the section.
  /// \param Buffer Memory buffer holding the section contents.
  NewSectionInfo(StringRef Name, std::unique_ptr<MemoryBuffer> &&Buffer)
      : SectionName(Name), SectionData(std::move(Buffer)) {}

  /// Name of the section to add or update.
  StringRef SectionName;
  /// Buffer holding the section contents.
  std::shared_ptr<MemoryBuffer> SectionData;
};

/// Configuration for copying or stripping a single file.
struct CommonConfig {
  // Main input/output options
  /// Path of the input object file.
  StringRef InputFilename;
  /// Format of the input file, or \c Unspecified to detect it.
  FileFormat InputFormat = FileFormat::Unspecified;
  /// Path of the output object file.
  StringRef OutputFilename;
  /// Format of the output file, or \c Unspecified to keep the input format.
  FileFormat OutputFormat = FileFormat::Unspecified;

  /// Optional ELF machine info used when the output format is not binary.
  ///
  /// Only applicable when \c --output-format is not \c binary (e.g.
  /// \c elf64-x86-64).
  std::optional<MachineInfo> OutputArch;

  // Advanced options
  /// Path of the file linked via a \c .gnu_debuglink section.
  StringRef AddGnuDebugLink;
  /// Cached CRC32 of the \c gnu_debuglink target file.
  uint32_t GnuDebugLinkCRC32;
  /// Name of the partition to extract, when partition extraction is requested.
  std::optional<StringRef> ExtractPartition;
  /// Byte value used to fill gaps between sections.
  uint8_t GapFill = 0;
  /// Address to which the output binary is padded, or zero when disabled.
  uint64_t PadTo = 0;
  /// Output path for extracted DWO (split DWARF) contents.
  StringRef SplitDWO;
  /// Prefix prepended to symbol names.
  StringRef SymbolsPrefix;
  /// Prefix removed from symbol names when present.
  StringRef SymbolsPrefixRemove;
  /// Prefix prepended to the names of allocatable sections.
  StringRef AllocSectionsPrefix;
  /// Local-symbol discard mode.
  DiscardType DiscardMode = DiscardType::None;

  // Repeated options
  /// Sections to add to the output, each with a name and contents.
  SmallVector<NewSectionInfo, 0> AddSection;
  /// Section names whose contents should be dumped to files.
  SmallVector<StringRef, 0> DumpSection;
  /// Sections whose contents are replaced with new data.
  SmallVector<NewSectionInfo, 0> UpdateSection;
  /// Per-section address adjustments selected by name patterns.
  SmallVector<SectionPatternAddressUpdate, 0> ChangeSectionAddress;
  /// Section names to extract into separate output files.
  SmallVector<StringRef, 0> ExtractSection;

  // Section matchers
  /// Sections that must be kept even when other strip options would remove them.
  NameMatcher KeepSection;
  /// When non-empty, only matching sections are retained.
  NameMatcher OnlySection;
  /// Sections that should be removed from the output.
  NameMatcher ToRemove;

  // Symbol matchers
  /// Symbols that should be given global binding.
  NameMatcher SymbolsToGlobalize;
  /// Symbols that must be kept even when other strip options would remove them.
  NameMatcher SymbolsToKeep;
  /// Symbols that should be given local binding.
  NameMatcher SymbolsToLocalize;
  /// Symbols that should be removed from the symbol table.
  NameMatcher SymbolsToRemove;
  /// Unneeded symbols that should be removed when safe to do so.
  NameMatcher UnneededSymbolsToRemove;
  /// Symbols that should be weakened.
  NameMatcher SymbolsToWeaken;
  /// Symbols that should keep global binding when others are localized.
  NameMatcher SymbolsToKeepGlobal;
  /// Symbols that should be skipped by certain rename/prefix operations.
  NameMatcher SymbolsToSkip;

  // Map options
  /// Map from original section name to rename (and optional flag) requests.
  StringMap<SectionRename> SectionsToRename;
  /// Map from section name to the alignment that should be set.
  StringMap<uint64_t> SetSectionAlignment;
  /// Map from section name to replacement section flags.
  StringMap<SectionFlagsUpdate> SetSectionFlags;
  /// Map from section name to the replacement section type.
  StringMap<uint64_t> SetSectionType;
  /// Map from original symbol name to the replacement name.
  StringMap<StringRef> SymbolsToRename;

  /// Symbols to add, as specified by \c --add-symbol.
  SmallVector<NewSymbolInfo, 0> SymbolsToAdd;

  // Integer options
  /// Signed LMA adjustment applied to all loadable sections.
  int64_t ChangeSectionLMAValAll = 0;

  // Boolean options
  /// Prefer deterministic archive member metadata when rewriting archives.
  bool DeterministicArchives = true;
  /// Extract DWO sections into a separate file.
  bool ExtractDWO = false;
  /// Extract the main partition from a partitioned object.
  bool ExtractMainPartition = false;
  /// Keep only debugging information in the output.
  bool OnlyKeepDebug = false;
  /// Preserve input file timestamps on the output.
  bool PreserveDates = false;
  /// Strip all symbols and relocation information.
  bool StripAll = false;
  /// Apply GNU-compatible \c --strip-all behavior.
  bool StripAllGNU = false;
  /// Remove DWO (split DWARF) sections from the output.
  bool StripDWO = false;
  /// Remove debugging sections from the output.
  bool StripDebug = false;
  /// Remove non-allocatable sections from the output.
  bool StripNonAlloc = false;
  /// Remove section headers from the output.
  bool StripSections = false;
  /// Remove symbols that are not needed for relocation.
  bool StripUnneeded = false;
  /// Print verbose status messages while copying.
  bool Verbose = false;
  /// Weaken all global symbols in the output.
  bool Weaken = false;
  /// Decompress compressed DWARF debug sections.
  bool DecompressDebugSections = false;

  /// Compression algorithm used when compressing debug sections.
  DebugCompressionType CompressionType = DebugCompressionType::None;

  /// Per-section compression requests pairing a name matcher with a type.
  SmallVector<std::pair<NameMatcher, llvm::DebugCompressionType>, 0>
      compressSections;

  /// Callback used to handle recoverable errors during objcopy.
  ///
  /// An Error returned by the callback aborts the execution and is then
  /// returned to the caller. If the callback is not set, the errors are not
  /// issued.
  std::function<Error(Error)> ErrorCallback;
};

} // namespace objcopy
} // namespace llvm

#endif // LLVM_OBJCOPY_COMMONCONFIG_H
