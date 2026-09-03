//===-- llvm/MC/MCAsmInfo.h - Asm info --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a class to be used as the basis for target specific
// asm writers.  This class primarily takes care of global printing constants,
// which are used in very similar ways across all targets.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCASMINFO_H
#define LLVM_MC_MCASMINFO_H

#include "llvm/ADT/CachedHashString.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/Support/Compiler.h"
#include <vector>

namespace llvm {

template <typename, unsigned> class EnumStrings;
class MCAssembler;
class MCContext;
class MCCFIInstruction;
class MCExpr;
class MCSpecifierExpr;
class MCSection;
class MCStreamer;
class MCSubtargetInfo;
class MCSymbol;
class MCValue;
class Triple;
class raw_ostream;

/// Windows exception-handling encoding helpers.
namespace WinEH {

/// Encoding used for Windows EH data (`.pdata`).
enum class EncodingType {
  /// Invalid encoding.
  Invalid,
  /// Windows Alpha.
  Alpha,
  /// Windows AXP64.
  Alpha64,
  /// Windows NT (Windows on ARM).
  ARM,
  /// Windows CE ARM, PowerPC, SH3, SH4.
  CE,
  /// Windows x64, Windows Itanium (IA-64).
  Itanium,
  /// Windows x86; uses no CFI, just EH tables.
  X86,
  /// Alias for Alpha (historical MIPS Windows EH).
  MIPS = Alpha,
};

} // end namespace WinEH

/// Local common (`.lcomm`) directive alignment helpers.
namespace LCOMM {

/// How `.lcomm` interprets an optional alignment argument.
enum LCOMMType {
  /// `.lcomm` does not take an alignment argument.
  NoAlignment,
  /// Alignment is specified in bytes.
  ByteAlignment,
  /// Alignment is specified as log2(N).
  Log2Alignment
};

} // end namespace LCOMM

/// This class is intended to be used as a base class for asm
/// properties and features specific to the target.
class LLVM_ABI MCAsmInfo {
public:
  /// Assembly character literal syntax types.
  enum AsmCharLiteralSyntax {
    /// Unknown; character literals are not used by LLVM for this target.
    ACLS_Unknown,
    /// The desired character is prefixed by a single quote, e.g., `'A`.
    ACLS_SingleQuotePrefix,
  };

  /// Type for at specifiers. Currently, 16 bits is enough.
  using AtSpecifierKind = uint16_t;

protected:
  //===------------------------------------------------------------------===//
  // Properties to be set by the target writer, used to configure asm printer.
  //

  /// Code pointer size in bytes.  Default is 4.
  unsigned CodePointerSize = 4;

  /// Size of the stack slot reserved for callee-saved registers, in bytes.
  /// Default is same as pointer size.
  unsigned CalleeSaveStackSlotSize = 4;

  /// True if target is little endian.  Default is true.
  bool IsLittleEndian = true;

  /// True if target stack grow up.  Default is false.
  bool StackGrowsUp = false;

  /// True if this target has the MachO .subsections_via_symbols directive.
  /// Default is false.
  bool HasSubsectionsViaSymbols = false;

  /// True if this is a non-GNU COFF target. The COFF port of the GNU linker
  /// doesn't handle associative comdats in the way that we would like to use
  /// them.
  bool HasCOFFAssociativeComdats = false;

  /// True if this is a non-GNU COFF target. For GNU targets, we don't generate
  /// constants into comdat sections.
  bool HasCOFFComdatConstants = false;

  /// True if this target is AIX.
  bool IsAIX = false;

  /// True if using the HLASM dialect on z/OS.
  bool IsHLASM = false;

  /// This is the maximum possible length of an instruction, which is needed to
  /// compute the size of an inline asm.  Defaults to 4.
  unsigned MaxInstLength = 4;

  /// Every possible instruction length is a multiple of this value.  Factored
  /// out in .debug_frame and .debug_line.  Defaults to 1.
  unsigned MinInstAlignment = 1;

  /// The '$' token, when not referencing an identifier or constant, refers to
  /// the current PC.  Defaults to false.
  bool DollarIsPC = false;

  /// This string, if specified, is used to separate instructions from each
  /// other when on the same line.  Defaults to ';'
  const char *SeparatorString = ";";

  /// This indicates the comment string used by the assembler.  Defaults to
  /// "#"
  StringRef CommentString = "#";

  /// Whether additional comment forms are lexed as comments.
  ///
  /// When true, C-style line comments (`// ..`), C-style block comments
  /// (`/* .. */`), and `#` are all treated as comments in addition to the
  /// string specified by the CommentString attribute.
  /// Default is true.
  bool AllowAdditionalComments = true;

  /// This is appended to emitted labels.  Defaults to ":"
  const char *LabelSuffix = ":";

  /// Use .set instead of = to equate a symbol to an expression.
  bool UsesSetToEquateSymbol = false;

  /// Print the EH begin symbol with an assignment. Defaults to false.
  bool UseAssignmentForEHBegin = false;

  /// True if a local symbol must be created for `.size`.
  bool NeedsLocalForSize = false;

  /// Prefix for compiler/assembler-internal symbols.
  ///
  /// For internal use by compiler and assembler, not meant to be visible
  /// externally. They are usually not emitted to the symbol table in the
  /// object file. This is also used for labels for basic blocks.
  StringRef InternalSymbolPrefix = "L";

  /// Prefix for symbols stripped by the linker after assembly.
  ///
  /// This prefix is used for symbols that should be passed through the
  /// assembler but be removed by the linker.  This is 'l' on Darwin, currently
  /// used for some ObjC metadata.  The default of "" means that for this system
  /// a plain private symbol should be used.  Defaults to "".
  StringRef LinkerPrivateGlobalPrefix = "";

  /// If these are nonempty, they contain a directive to emit before and after
  /// an inline assembly statement.  Defaults to "APP", "NO_APP"
  const char *InlineAsmStart = "APP";
  /// Directive emitted after an inline assembly statement. Defaults to "NO_APP".
  const char *InlineAsmEnd = "NO_APP";

  /// Which dialect of an assembler variant to use.  Defaults to 0
  unsigned AssemblerDialect = 0;

  /// This is true if the assembler allows @ characters in symbol names.
  /// Defaults to false.
  bool AllowAtInName = false;

  /// True if '?' may start an identifier token.
  ///
  /// This is true if the assembler allows the "?" character at the start of
  /// of a string to be lexed as an AsmToken::Identifier.
  /// If the AsmLexer determines that the string can be lexed as a possible
  /// comment, setting this option will have no effect, and the string will
  /// still be lexed as a comment.
  bool AllowQuestionAtStartOfIdentifier = false;

  /// True if '$' may start an identifier token.
  ///
  /// This is true if the assembler allows the "$" character at the start of
  /// of a string to be lexed as an AsmToken::Identifier.
  /// If the AsmLexer determines that the string can be lexed as a possible
  /// comment, setting this option will have no effect, and the string will
  /// still be lexed as a comment.
  bool AllowDollarAtStartOfIdentifier = false;

  /// True if '@' may start an identifier token.
  ///
  /// This is true if the assembler allows the "@" character at the start of
  /// a string to be lexed as an AsmToken::Identifier.
  /// If the AsmLexer determines that the string can be lexed as a possible
  /// comment, setting this option will have no effect, and the string will
  /// still be lexed as a comment.
  bool AllowAtAtStartOfIdentifier = false;

  /// If this is true, symbol names with invalid characters will be printed in
  /// quotes.
  bool SupportsQuotedNames = true;

  /// This is true if data region markers should be printed as
  /// ".data_region/.end_data_region" directives. If false, use "$d/$a" labels
  /// instead.
  bool UseDataRegionDirectives = false;

  /// True if the target supports LEB128 directives.
  bool HasLEB128Directives = true;

  /// True if full register names are printed.
  bool PPCUseFullRegisterNames = false;

  //===--- Data Emission Directives -------------------------------------===//

  /// Directive that emits a run of zero (or fill) bytes.
  ///
  /// This should be set to the directive used to get some number of zero (and
  /// non-zero if supported by the directive) bytes emitted to the current
  /// section. Common cases are "\t.zero\t" and "\t.space\t". Defaults to
  /// "\t.zero\t"
  const char *ZeroDirective = "\t.zero\t";

  /// Directive that emits an ASCII string with C escapes.
  ///
  /// This directive allows emission of an ascii string with the standard C
  /// escape characters embedded into it.  If a target doesn't support this, it
  /// can be set to null. Defaults to "\t.ascii\t"
  const char *AsciiDirective = "\t.ascii\t";

  /// Directive that emits a NUL-terminated ASCII string.
  ///
  /// If not null, this allows for special handling of zero terminated strings
  /// on this target.  This is commonly supported as ".asciz".  If a target
  /// doesn't support this, it can be set to null.  Defaults to "\t.asciz\t"
  const char *AscizDirective = "\t.asciz\t";

  /// Character-literal syntax used when emitting byte lists.
  ///
  /// Form used for character literals in the assembly syntax.  Useful for
  /// producing strings as byte lists.  If a target does not use or support
  /// this, it shall be set to ACLS_Unknown.  Defaults to ACLS_Unknown.
  AsmCharLiteralSyntax CharacterLiteralSyntax = ACLS_Unknown;

  /// Directive that emits 8-bit integer data.
  ///
  /// These directives are used to output some unit of integer data to the
  /// current section.  If a data directive is set to null, smaller data
  /// directives will be used to emit the large sizes.  Defaults to "\t.byte\t",
  /// "\t.short\t", "\t.long\t", "\t.quad\t"
  const char *Data8bitsDirective = "\t.byte\t";
  /// Directive that emits 16-bit integer data. Defaults to "\t.short\t".
  const char *Data16bitsDirective = "\t.short\t";
  /// Directive that emits 32-bit integer data. Defaults to "\t.long\t".
  const char *Data32bitsDirective = "\t.long\t";
  /// Directive that emits 64-bit integer data. Defaults to "\t.quad\t".
  const char *Data64bitsDirective = "\t.quad\t";

  /// True if data directives support signed values
  bool SupportsSignedData = true;

  /// True if section switching uses Sun-style `#alloc,#write` flags.
  ///
  /// This is true if this target uses "Sun Style" syntax for section switching
  /// ("#alloc,#write" etc) instead of the normal ELF syntax (,"a,w") in
  /// .section directives.  Defaults to false.
  bool SunStyleELFSectionSwitchSyntax = false;

  /// True if `.section` must precede `.bss` on this target.
  ///
  /// This is true if this target uses ELF '.section' directive before the
  /// '.bss' one. It's used for PPC/Linux which doesn't support the '.bss'
  /// directive only.  Defaults to false.
  bool UsesELFSectionDirectiveForBSS = false;

  /// True if DWARF section offsets need a dedicated directive.
  bool NeedsDwarfSectionOffsetDirective = false;

  //===--- Alignment Information ----------------------------------------===//

  /// True if `.align` takes a byte count rather than log2(N).
  ///
  /// If this is true (the default) then the asmprinter emits ".align N"
  /// directives, where N is the number of bytes to align to.  Otherwise, it
  /// emits ".align log2(N)", e.g. 3 to align to an 8 byte boundary.  Defaults
  /// to true.
  bool AlignmentIsInBytes = true;

  /// If non-zero, this is used to fill the executable space created as the
  /// result of a alignment directive.  Defaults to 0
  unsigned TextAlignFillValue = 0;

  //===--- Global Variable Emission Directives --------------------------===//

  /// This is the directive used to declare a global entity. Defaults to
  /// ".globl".
  const char *GlobalDirective = "\t.globl\t";

  /// True if the expression
  ///   .long f - g
  /// uses a relocation but it can be suppressed by writing
  ///   a = f - g
  ///   .long a
  bool SetDirectiveSuppressesReloc = false;

  /// True is .comm's and .lcomms optional alignment is to be specified in bytes
  /// instead of log2(n).  Defaults to true.
  bool COMMDirectiveAlignmentIsInBytes = true;

  /// Describes if the .lcomm directive for the target supports an alignment
  /// argument and how it is interpreted.  Defaults to NoAlignment.
  LCOMM::LCOMMType LCOMMDirectiveAlignmentType = LCOMM::NoAlignment;

  /// True if the target allows `.align` directives on functions.
  ///
  /// This is true for most targets, so defaults to true.
  bool HasFunctionAlignment = true;

  /// True if the target respects `.prefalign` directives.
  bool HasPreferredAlignment = false;

  /// True if the target has .type and .size directives, this is true for most
  /// ELF targets.  Defaults to true.
  bool HasDotTypeDotSizeDirective = true;

  /// True if the target has a single parameter .file directive, this is true
  /// for ELF targets.  Defaults to true.
  bool HasSingleParameterDotFile = true;

  /// True if the target has a .ident directive, this is true for ELF targets.
  /// Defaults to false.
  bool HasIdentDirective = false;

  /// True if this target supports the MachO .no_dead_strip directive.  Defaults
  /// to false.
  bool HasNoDeadStrip = false;

  /// Used to declare a global as being a weak symbol. Defaults to ".weak".
  const char *WeakDirective = "\t.weak\t";

  /// This directive, if non-null, is used to declare a global as being a weak
  /// undefined symbol.  Defaults to nullptr.
  const char *WeakRefDirective = nullptr;

  /// True if we have a directive to declare a global as being a weak defined
  /// symbol that can be hidden (unexported).  Defaults to false.
  bool HasWeakDefCanBeHiddenDirective = false;

  /// True if we should mark symbols as global instead of weak, for
  /// weak*/linkonce*, if the symbol has a comdat.
  /// Defaults to false.
  bool AvoidWeakIfComdat = false;

  /// This attribute, if not MCSA_Invalid, is used to declare a symbol as having
  /// hidden visibility.  Defaults to MCSA_Hidden.
  MCSymbolAttr HiddenVisibilityAttr = MCSA_Hidden;

  /// This attribute, if not MCSA_Invalid, is used to declare a symbol as having
  /// exported visibility.  Defaults to MCSA_Exported.
  MCSymbolAttr ExportedVisibilityAttr = MCSA_Exported;

  /// This attribute, if not MCSA_Invalid, is used to declare an undefined
  /// symbol as having hidden visibility. Defaults to MCSA_Hidden.
  MCSymbolAttr HiddenDeclarationVisibilityAttr = MCSA_Hidden;

  /// This attribute, if not MCSA_Invalid, is used to declare a symbol as having
  /// protected visibility.  Defaults to MCSA_Protected
  MCSymbolAttr ProtectedVisibilityAttr = MCSA_Protected;

  //===--- Dwarf Emission Directives -----------------------------------===//

  /// True if target supports emission of debugging information.  Defaults to
  /// false.
  bool SupportsDebugInformation = false;

  /// Exception handling format for the target.  Defaults to None.
  ExceptionHandling ExceptionsType = ExceptionHandling::None;

  /// True if target uses CFI unwind information for other purposes than EH
  /// (debugging / sanitizers) when `ExceptionsType == ExceptionHandling::None`.
  bool UsesCFIWithoutEH = false;

  /// Windows exception handling data (.pdata) encoding.  Defaults to Invalid.
  WinEH::EncodingType WinEHEncodingType = WinEH::EncodingType::Invalid;

  /// True if Dwarf2 output generally uses relocations for references to other
  /// .debug_* sections.
  bool DwarfUsesRelocationsAcrossSections = true;

  /// True if DWARF FDE symbol reference relocations should be replaced by an
  /// absolute difference.
  bool DwarfFDESymbolsUseAbsDiff = false;

  /// The optional specifier to use for the relative FDE symbol references.
  uint16_t DwarfFDERelSymbolSpec = 0;

  /// True if DWARF `.file directory' directive syntax is used by
  /// default.
  bool EnableDwarfFileDirectoryDefault = true;

  /// True if dwarf register numbers are printed instead of symbolic register
  /// names in .cfi_* directives.  Defaults to false.
  bool DwarfRegNumForCFI = false;

  /// True if target uses @ (expr@specifier) for relocation specifiers.
  bool UseAtForSpecifier = false;

  /// (ARM-specific) Uses parens for relocation specifier in data
  /// directives, e.g. .word foo(got).
  bool UseParensForSpecifier = false;

  /// True if the target supports flags in ".loc" directive, false if only
  /// location is allowed.
  bool SupportsExtendedDwarfLocDirective = true;

  //===--- Prologue State ----------------------------------------------===//

  /// Initial CFI instructions describing the prologue frame state.
  std::vector<MCCFIInstruction> InitialFrameState;

  //===--- Integrated Assembler Information ----------------------------===//

  /// Minimum GNU binutils version whose ELF features may be assumed.
  ///
  /// Generated object files can use all ELF features supported by GNU ld of
  /// this binutils version and later. INT_MAX means all features can be used,
  /// regardless of GNU ld support. The default value is referenced by
  /// clang/Options/Options.td.
  std::pair<int, int> BinutilsVersion = {2, 26};

  /// Whether the integrated assembler should be used.
  ///
  /// The integrated assembler should be enabled by default (by the
  /// constructors) when failing to parse a valid piece of assembly (inline
  /// or otherwise) is considered a bug. It may then be overridden after
  /// construction (see CodeGenTargetMachineImpl::initAsmInfo()).
  bool UseIntegratedAssembler = true;

  /// Use AsmParser to parse inlineAsm when UseIntegratedAssembler is not set.
  bool ParseInlineAsmUsingAsmParser = false;

  /// Preserve Comments in assembly
  bool PreserveAsmComments = true;

  /// The column (zero-based) at which asm comments should be printed.
  unsigned CommentColumn = 40;

  /// True if the integrated assembler should interpret 'a >> b' constant
  /// expressions as logical rather than arithmetic.
  bool UseLogicalShr = true;

  /// True if Motorola-style integer literals (e.g. `$0ac`) are used.
  bool UseMotorolaIntegers = false;

  /// Map from `@`-style relocation specifier kind to its printed name.
  ///
  /// This describes a @ style relocation specifier (expr@specifier) supported by
  /// AsmParser::parsePrimaryExpr.
  llvm::DenseMap<AtSpecifierKind, StringRef> AtSpecifierToName;
  /// Map from relocation specifier name to its kind.
  llvm::StringMap<AtSpecifierKind> NameToAtSpecifier;
  /// Populate AtSpecifierToName / NameToAtSpecifier from \p Descs.
  /// @param Descs Enumerated specifier names and kinds to register.
  void initializeAtSpecifiers(EnumStrings<AtSpecifierKind, 1> Descs);

  /// Lowercase identifiers that must be quoted when used as symbol names.
  ///
  /// Includes register names, dialect keywords, and similar reserved words.
  llvm::DenseSet<llvm::CachedHashStringRef> ReservedIdentifiers;

  /// Target-specific MC options used while configuring this asm info.
  const MCTargetOptions &TargetOptions;

public:
  /// Construct asm info using the given target options.
  /// @param Options Target MC options to retain for later queries.
  explicit MCAsmInfo(const MCTargetOptions &Options);
  /// Destroy this asm info.
  virtual ~MCAsmInfo();

  // Explicitly non-copyable.
  /// Deleted copy constructor.
  /// @param Other Unused; this overload is deleted.
  MCAsmInfo(MCAsmInfo const &Other) = delete;
  /// Deleted copy assignment.
  /// @param Other Unused; this overload is deleted.
  MCAsmInfo &operator=(MCAsmInfo const &Other) = delete;

  /// Return the retained target MC options.
  /// @return The retained target MC options.
  const MCTargetOptions &getTargetOptions() const { return TargetOptions; }

  /// Get the code pointer size in bytes.
  /// @return The code pointer size in bytes.
  unsigned getCodePointerSize() const { return CodePointerSize; }

  /// Get the callee-saved register stack slot
  /// size in bytes.
  /// @return The callee-saved register stack slot size in bytes.
  unsigned getCalleeSaveStackSlotSize() const {
    return CalleeSaveStackSlotSize;
  }

  /// True if the target is little endian.
  /// @return True if the target is little endian.
  bool isLittleEndian() const { return IsLittleEndian; }

  /// True if target stack grow up.
  /// @return True if the target stack grows up.
  bool isStackGrowthDirectionUp() const { return StackGrowsUp; }

  /// True if this target has the MachO `.subsections_via_symbols` directive.
  /// @return True if this target has the MachO `.subsections_via_symbols`
  /// directive.
  bool hasSubsectionsViaSymbols() const { return HasSubsectionsViaSymbols; }

  // Data directive accessors.

  /// Return the directive used to emit 8-bit data.
  /// @return The directive used to emit 8-bit data.
  const char *getData8bitsDirective() const { return Data8bitsDirective; }
  /// Return the directive used to emit 16-bit data.
  /// @return The directive used to emit 16-bit data.
  const char *getData16bitsDirective() const { return Data16bitsDirective; }
  /// Return the directive used to emit 32-bit data.
  /// @return The directive used to emit 32-bit data.
  const char *getData32bitsDirective() const { return Data32bitsDirective; }
  /// Return the directive used to emit 64-bit data.
  /// @return The directive used to emit 64-bit data.
  const char *getData64bitsDirective() const { return Data64bitsDirective; }
  /// True if data directives accept signed values.
  /// @return True if data directives accept signed values.
  bool supportsSignedData() const { return SupportsSignedData; }

  /// Return the section to use for the stack, or null if none.
  ///
  /// Targets can implement this method to specify a section to switch to
  /// depending on whether the translation unit has any trampolines that require
  /// an executable stack.
  /// @param Ctx Assembly context used to create or look up the section.
  /// @param Exec True if the stack must be executable.
  /// @return The stack section, or null if none.
  virtual MCSection *getStackSection(MCContext &Ctx, bool Exec) const {
    return nullptr;
  }

  /// Build an MCExpr referring to a personality symbol for EH tables.
  /// @param Sym Personality function symbol.
  /// @param Encoding DWARF personality encoding flags.
  /// @param Streamer Streamer used to create expressions.
  /// @return An MCExpr referring to the personality symbol.
  virtual const MCExpr *getExprForPersonalitySymbol(const MCSymbol *Sym,
                                                    unsigned Encoding,
                                                    MCStreamer &Streamer) const;

  /// Build an MCExpr referring to an FDE symbol for EH tables.
  /// @param Sym FDE / function symbol.
  /// @param Encoding DWARF FDE encoding flags.
  /// @param Streamer Streamer used to create expressions.
  /// @return An MCExpr referring to the FDE symbol.
  const MCExpr *getExprForFDESymbol(const MCSymbol *Sym, unsigned Encoding,
                                    MCStreamer &Streamer) const;

  /// Return true if C is an acceptable character inside a symbol name.
  /// @param C Character to test.
  /// @return True if \p C is acceptable inside a symbol name.
  bool isAcceptableChar(char C) const;

  /// Return true if the identifier \p Name does not need quotes to be
  /// syntactically correct.
  /// @param Name Identifier to test.
  /// @return True if \p Name does not need quotes.
  bool isValidUnquotedName(StringRef Name) const;

  /// Return the mutable set of reserved identifiers.
  /// @return The mutable set of reserved identifiers.
  llvm::DenseSet<llvm::CachedHashStringRef> &getReservedIdentifiers() {
    return ReservedIdentifiers;
  }
  /// Return the const set of reserved identifiers.
  /// @return The const set of reserved identifiers.
  const llvm::DenseSet<llvm::CachedHashStringRef> &
  getReservedIdentifiers() const {
    return ReservedIdentifiers;
  }

  /// Print the assembler text that switches to \p Section.
  /// @param Section Section to switch to.
  /// @param Subsection Optional subsection number.
  /// @param T Target triple used for target-specific syntax.
  /// @param OS Output stream that receives the directive text.
  virtual void printSwitchToSection(const MCSection &Section,
                                    uint32_t Subsection, const Triple &T,
                                    raw_ostream &OS) const {}

  /// Return true if the .section directive should be omitted when
  /// emitting \p SectionName.  For example:
  ///
  /// shouldOmitSectionDirective(".text")
  ///
  /// returns false => .section .text,#alloc,#execinstr
  /// returns true  => .text
  /// @param SectionName Section name being considered for emission.
  /// @return True if the `.section` directive should be omitted.
  virtual bool shouldOmitSectionDirective(StringRef SectionName) const;

  /// Return true if a `.align` directive should use optimized nops to fill
  /// instead of 0s.
  /// @param Sec Section whose alignment fill policy is queried.
  /// @return True if optimized nops should fill alignment.
  virtual bool useCodeAlign(const MCSection &Sec) const { return false; }

  /// True if section switching uses Sun-style ELF flag syntax.
  /// @return True if section switching uses Sun-style ELF flag syntax.
  bool usesSunStyleELFSectionSwitchSyntax() const {
    return SunStyleELFSectionSwitchSyntax;
  }

  /// True if `.section` must precede `.bss` on this target.
  /// @return True if `.section` must precede `.bss` on this target.
  bool usesELFSectionDirectiveForBSS() const {
    return UsesELFSectionDirectiveForBSS;
  }

  /// True if DWARF section offsets need a dedicated directive.
  /// @return True if DWARF section offsets need a dedicated directive.
  bool needsDwarfSectionOffsetDirective() const {
    return NeedsDwarfSectionOffsetDirective;
  }

  // Accessors.

  /// True if this target is AIX.
  /// @return True if this target is AIX.
  bool isAIX() const { return IsAIX; }
  /// True if using the HLASM dialect on z/OS.
  /// @return True if using the HLASM dialect on z/OS.
  bool isHLASM() const { return IsHLASM; }
  /// True if this is a MachO-style target (via subsections-via-symbols).
  /// @return True if this is a MachO-style target.
  bool isMachO() const { return HasSubsectionsViaSymbols; }
  /// True if associative COFF comdats are supported.
  /// @return True if associative COFF comdats are supported.
  bool hasCOFFAssociativeComdats() const { return HasCOFFAssociativeComdats; }
  /// True if COFF comdat constants are supported.
  /// @return True if COFF comdat constants are supported.
  bool hasCOFFComdatConstants() const { return HasCOFFComdatConstants; }

  /// Returns the maximum possible encoded instruction size in bytes. If \p STI
  /// is null, this should be the maximum size for any subtarget.
  /// @param STI Optional subtarget; null means any subtarget.
  /// @return The maximum encoded instruction size in bytes.
  virtual unsigned getMaxInstLength(const MCSubtargetInfo *STI = nullptr) const {
    return MaxInstLength;
  }

  /// Return the minimum instruction alignment in bytes.
  /// @return The minimum instruction alignment in bytes.
  unsigned getMinInstAlignment() const { return MinInstAlignment; }
  /// True if a bare `$` token refers to the current PC.
  /// @return True if a bare `$` token refers to the current PC.
  bool getDollarIsPC() const { return DollarIsPC; }
  /// Return the string used to separate instructions on one line.
  /// @return The string used to separate instructions on one line.
  const char *getSeparatorString() const { return SeparatorString; }

  /// Return the zero-based column where asm comments are printed.
  /// @return The zero-based column where asm comments are printed.
  unsigned getCommentColumn() const { return CommentColumn; }
  /// Set the zero-based column where asm comments are printed.
  /// @param Col Comment column to use.
  void setCommentColumn(unsigned Col) { CommentColumn = Col; }

  /// Return the assembler comment string.
  /// @return The assembler comment string.
  StringRef getCommentString() const { return CommentString; }
  /// True if additional comment forms are lexed as comments.
  /// @return True if additional comment forms are lexed as comments.
  bool shouldAllowAdditionalComments() const { return AllowAdditionalComments; }
  /// Return the suffix appended to emitted labels.
  /// @return The suffix appended to emitted labels.
  const char *getLabelSuffix() const { return LabelSuffix; }

  /// True if `.set` is used instead of `=` to equate symbols.
  /// @return True if `.set` is used instead of `=` to equate symbols.
  bool usesSetToEquateSymbol() const { return UsesSetToEquateSymbol; }
  /// True if the EH begin symbol is printed with an assignment.
  /// @return True if the EH begin symbol is printed with an assignment.
  bool useAssignmentForEHBegin() const { return UseAssignmentForEHBegin; }
  /// True if a local symbol must be created for `.size`.
  /// @return True if a local symbol must be created for `.size`.
  bool needsLocalForSize() const { return NeedsLocalForSize; }
  /// Return the prefix used for compiler/assembler-internal symbols.
  /// @return The prefix used for compiler/assembler-internal symbols.
  StringRef getInternalSymbolPrefix() const { return InternalSymbolPrefix; }

  /// True if a non-empty linker-private global prefix is configured.
  /// @return True if a non-empty linker-private global prefix is configured.
  bool hasLinkerPrivateGlobalPrefix() const {
    return !LinkerPrivateGlobalPrefix.empty();
  }

  /// Return the linker-private prefix, or the internal prefix if none.
  /// @return The linker-private prefix, or the internal prefix if none.
  StringRef getLinkerPrivateGlobalPrefix() const {
    if (hasLinkerPrivateGlobalPrefix())
      return LinkerPrivateGlobalPrefix;
    return getInternalSymbolPrefix();
  }

  /// Return the directive emitted before inline assembly.
  /// @return The directive emitted before inline assembly.
  const char *getInlineAsmStart() const { return InlineAsmStart; }
  /// Return the directive emitted after inline assembly.
  /// @return The directive emitted after inline assembly.
  const char *getInlineAsmEnd() const { return InlineAsmEnd; }
  /// Return the default assembler dialect variant.
  /// @return The default assembler dialect variant.
  unsigned getAssemblerDialect() const { return AssemblerDialect; }
  /// Return the assembler dialect that output printing should use.
  ///
  /// Used by createMCInstPrinter.
  /// @return The assembler dialect that output printing should use.
  unsigned getOutputAssemblerDialect() const {
    return TargetOptions.OutputAsmVariant.value_or(AssemblerDialect);
  }
  /// True if `@` is allowed inside symbol names.
  /// @return True if `@` is allowed inside symbol names.
  bool doesAllowAtInName() const { return AllowAtInName; }
  /// Set whether `@` is allowed inside symbol names.
  /// @param V New AllowAtInName value.
  void setAllowAtInName(bool V) { AllowAtInName = V; }
  /// True if `?` may start an identifier token.
  /// @return True if `?` may start an identifier token.
  bool doesAllowQuestionAtStartOfIdentifier() const {
    return AllowQuestionAtStartOfIdentifier;
  }
  /// True if `@` may start an identifier token.
  /// @return True if `@` may start an identifier token.
  bool doesAllowAtAtStartOfIdentifier() const {
    return AllowAtAtStartOfIdentifier;
  }
  /// True if `$` may start an identifier token.
  /// @return True if `$` may start an identifier token.
  bool doesAllowDollarAtStartOfIdentifier() const {
    return AllowDollarAtStartOfIdentifier;
  }
  /// True if symbol names with invalid characters are printed in quotes.
  /// @return True if symbol names with invalid characters are printed in
  /// quotes.
  bool supportsNameQuoting() const { return SupportsQuotedNames; }

  /// True if data-region markers are printed as directives.
  /// @return True if data-region markers are printed as directives.
  bool doesSupportDataRegionDirectives() const {
    return UseDataRegionDirectives;
  }

  /// True if the target supports LEB128 directives.
  /// @return True if the target supports LEB128 directives.
  bool hasLEB128Directives() const { return HasLEB128Directives; }

  /// True if full register names are printed.
  /// @return True if full register names are printed.
  bool useFullRegisterNames() const { return PPCUseFullRegisterNames; }
  /// Set whether full register names are printed.
  /// @param V New full-register-names setting.
  void setFullRegisterNames(bool V) { PPCUseFullRegisterNames = V; }

  /// Return the zero/fill directive string.
  /// @return The zero/fill directive string.
  const char *getZeroDirective() const { return ZeroDirective; }
  /// Return the ASCII string directive.
  /// @return The ASCII string directive.
  const char *getAsciiDirective() const { return AsciiDirective; }
  /// Return the NUL-terminated ASCII string directive.
  /// @return The NUL-terminated ASCII string directive.
  const char *getAscizDirective() const { return AscizDirective; }
  /// Return the character-literal syntax used by this target.
  /// @return The character-literal syntax used by this target.
  AsmCharLiteralSyntax characterLiteralSyntax() const {
    return CharacterLiteralSyntax;
  }
  /// True if `.align` takes a byte count rather than log2(N).
  /// @return True if `.align` takes a byte count rather than log2(N).
  bool getAlignmentIsInBytes() const { return AlignmentIsInBytes; }
  /// Return the fill value used for executable alignment padding.
  /// @return The fill value used for executable alignment padding.
  unsigned getTextAlignFillValue() const { return TextAlignFillValue; }
  /// Return the directive used to declare a global entity.
  /// @return The directive used to declare a global entity.
  const char *getGlobalDirective() const { return GlobalDirective; }

  /// True if `.set` can suppress a reloc for symbol differences.
  /// @return True if `.set` can suppress a reloc for symbol differences.
  bool doesSetDirectiveSuppressReloc() const {
    return SetDirectiveSuppressesReloc;
  }

  /// True if `.comm` / `.lcomm` alignment is specified in bytes.
  /// @return True if `.comm` / `.lcomm` alignment is specified in bytes.
  bool getCOMMDirectiveAlignmentIsInBytes() const {
    return COMMDirectiveAlignmentIsInBytes;
  }

  /// Return how `.lcomm` interprets an optional alignment argument.
  /// @return How `.lcomm` interprets an optional alignment argument.
  LCOMM::LCOMMType getLCOMMDirectiveAlignmentType() const {
    return LCOMMDirectiveAlignmentType;
  }

  /// True if the target allows `.align` directives on functions.
  /// @return True if the target allows `.align` directives on functions.
  bool hasFunctionAlignment() const { return HasFunctionAlignment; }
  /// True if the target respects `.prefalign` directives.
  /// @return True if the target respects `.prefalign` directives.
  bool hasPreferredAlignment() const { return HasPreferredAlignment; }
  /// True if the target has `.type` and `.size` directives.
  /// @return True if the target has `.type` and `.size` directives.
  bool hasDotTypeDotSizeDirective() const { return HasDotTypeDotSizeDirective; }
  /// True if the target has a single-parameter `.file` directive.
  /// @return True if the target has a single-parameter `.file` directive.
  bool hasSingleParameterDotFile() const { return HasSingleParameterDotFile; }
  /// True if the target has a `.ident` directive.
  /// @return True if the target has a `.ident` directive.
  bool hasIdentDirective() const { return HasIdentDirective; }
  /// True if this target supports the MachO `.no_dead_strip` directive.
  /// @return True if this target supports the MachO `.no_dead_strip`
  /// directive.
  bool hasNoDeadStrip() const { return HasNoDeadStrip; }
  /// Return the directive used to declare a weak symbol.
  /// @return The directive used to declare a weak symbol.
  const char *getWeakDirective() const { return WeakDirective; }
  /// Return the directive used to declare a weak undefined symbol.
  /// @return The directive used to declare a weak undefined symbol.
  const char *getWeakRefDirective() const { return WeakRefDirective; }

  /// True if weak-defined symbols can be declared hidden.
  /// @return True if weak-defined symbols can be declared hidden.
  bool hasWeakDefCanBeHiddenDirective() const {
    return HasWeakDefCanBeHiddenDirective;
  }

  /// True if weak*/linkonce* symbols with a comdat should be marked global.
  /// @return True if weak*/linkonce* symbols with a comdat should be marked
  /// global.
  bool avoidWeakIfComdat() const { return AvoidWeakIfComdat; }

  /// Return the attribute used for hidden visibility.
  /// @return The attribute used for hidden visibility.
  MCSymbolAttr getHiddenVisibilityAttr() const { return HiddenVisibilityAttr; }

  /// Return the attribute used for exported visibility.
  /// @return The attribute used for exported visibility.
  MCSymbolAttr getExportedVisibilityAttr() const { return ExportedVisibilityAttr; }

  /// Return the attribute used for hidden visibility on undefined symbols.
  /// @return The attribute used for hidden visibility on undefined symbols.
  MCSymbolAttr getHiddenDeclarationVisibilityAttr() const {
    return HiddenDeclarationVisibilityAttr;
  }

  /// Return the attribute used for protected visibility.
  /// @return The attribute used for protected visibility.
  MCSymbolAttr getProtectedVisibilityAttr() const {
    return ProtectedVisibilityAttr;
  }

  /// True if the target supports emission of debugging information.
  /// @return True if the target supports emission of debugging information.
  bool doesSupportDebugInformation() const { return SupportsDebugInformation; }

  /// Return the exception-handling format for the target.
  /// @return The exception-handling format for the target.
  ExceptionHandling getExceptionHandlingType() const { return ExceptionsType; }
  /// Return the Windows EH data encoding type.
  /// @return The Windows EH data encoding type.
  WinEH::EncodingType getWinEHEncodingType() const { return WinEHEncodingType; }

  /// Set the exception-handling format for the target.
  /// @param EH New exception-handling kind.
  void setExceptionsType(ExceptionHandling EH) {
    ExceptionsType = EH;
  }

  /// True if CFI is used for non-EH purposes when exceptions are None.
  /// @return True if CFI is used for non-EH purposes when exceptions are None.
  bool usesCFIWithoutEH() const {
    return ExceptionsType == ExceptionHandling::None && UsesCFIWithoutEH;
  }

  /// Returns true if the exception handling method for the platform uses call
  /// frame information to unwind.
  /// @return True if the platform uses CFI for exception unwinding.
  bool usesCFIForEH() const {
    return (ExceptionsType == ExceptionHandling::DwarfCFI ||
            ExceptionsType == ExceptionHandling::ARM ||
            ExceptionsType == ExceptionHandling::ZOS || usesWindowsCFI());
  }

  /// True if Windows CFI-based EH is in use.
  /// @return True if Windows CFI-based EH is in use.
  bool usesWindowsCFI() const {
    return ExceptionsType == ExceptionHandling::WinEH &&
           (WinEHEncodingType != WinEH::EncodingType::Invalid &&
            WinEHEncodingType != WinEH::EncodingType::X86);
  }

  /// True if Dwarf2 output uses relocations across `.debug_*` sections.
  /// @return True if Dwarf2 output uses relocations across `.debug_*`
  /// sections.
  bool doesDwarfUseRelocationsAcrossSections() const {
    return DwarfUsesRelocationsAcrossSections;
  }

  /// True if DWARF FDE symbol refs should become absolute differences.
  /// @return True if DWARF FDE symbol refs should become absolute differences.
  bool doDwarfFDESymbolsUseAbsDiff() const { return DwarfFDESymbolsUseAbsDiff; }
  /// True if dwarf register numbers are printed in `.cfi_*` directives.
  /// @return True if dwarf register numbers are printed in `.cfi_*`
  /// directives.
  bool useDwarfRegNumForCFI() const { return DwarfRegNumForCFI; }
  /// True if relocation specifiers use `@` (expr@specifier).
  /// @return True if relocation specifiers use `@` (expr@specifier).
  bool useAtForSpecifier() const { return UseAtForSpecifier; }
  /// True if relocation specifiers use parentheses (ARM-style).
  /// @return True if relocation specifiers use parentheses (ARM-style).
  bool useParensForSpecifier() const { return UseParensForSpecifier; }
  /// True if `.loc` directives may include flags beyond location.
  /// @return True if `.loc` directives may include flags beyond location.
  bool supportsExtendedDwarfLocDirective() const {
    return SupportsExtendedDwarfLocDirective;
  }

  /// True if DWARF `.file directory` syntax is used by default.
  /// @return True if DWARF `.file directory` syntax is used by default.
  bool enableDwarfFileDirectoryDefault() const {
    return EnableDwarfFileDirectoryDefault;
  }

  /// Append an initial CFI instruction to the prologue frame state.
  /// @param Inst CFI instruction to record.
  void addInitialFrameState(const MCCFIInstruction &Inst);

  /// Return the initial CFI instructions for the prologue frame state.
  /// @return The initial CFI instructions for the prologue frame state.
  const std::vector<MCCFIInstruction> &getInitialFrameState() const {
    return InitialFrameState;
  }

  /// Set the assumed GNU binutils version for ELF feature selection.
  /// @param Value Major/minor pair to require.
  void setBinutilsVersion(std::pair<int, int> Value) {
    BinutilsVersion = Value;
  }

  /// Return true if assembly (inline or otherwise) should be parsed.
  /// @return True if assembly should be parsed with the integrated assembler.
  bool useIntegratedAssembler() const { return UseIntegratedAssembler; }

  /// Return true if target want to use AsmParser to parse inlineasm.
  /// @return True if AsmParser should parse inline assembly.
  bool parseInlineAsmUsingAsmParser() const {
    return ParseInlineAsmUsingAsmParser;
  }

  /// True if the configured binutils version is at least \p Major.\p Minor.
  /// @param Major Required major version.
  /// @param Minor Required minor version.
  /// @return True if the configured binutils version is at least
  /// \p Major.\p Minor.
  bool binutilsIsAtLeast(int Major, int Minor) const {
    return BinutilsVersion >= std::make_pair(Major, Minor);
  }

  /// Set whether assembly (inline or otherwise) should be parsed.
  /// @param Value Whether to use the integrated assembler.
  virtual void setUseIntegratedAssembler(bool Value) {
    UseIntegratedAssembler = Value;
  }

  /// Set whether target want to use AsmParser to parse inlineasm.
  /// @param Value Whether to parse inline asm with AsmParser.
  void setParseInlineAsmUsingAsmParser(bool Value) {
    ParseInlineAsmUsingAsmParser = Value;
  }

  /// Return true if assembly (inline or otherwise) should be parsed.
  /// @return True if assembly comments should be preserved.
  bool preserveAsmComments() const { return PreserveAsmComments; }

  /// Set whether assembly (inline or otherwise) should be parsed.
  /// @param Value Whether to preserve assembly comments.
  void setPreserveAsmComments(bool Value) { PreserveAsmComments = Value; }

  /// True if `>>` in constant expressions is a logical shift.
  /// @return True if `>>` in constant expressions is a logical shift.
  bool shouldUseLogicalShr() const { return UseLogicalShr; }

  /// True if Motorola-style integer literals are used.
  /// @return True if Motorola-style integer literals are used.
  bool shouldUseMotorolaIntegers() const { return UseMotorolaIntegers; }

  /// Return the printed name for relocation specifier kind \p S.
  /// @param S Specifier kind to look up.
  /// @return The printed name for relocation specifier kind \p S.
  StringRef getSpecifierName(uint32_t S) const;
  /// Return the specifier kind for \p Name, if known.
  /// @param Name Specifier name to look up (case-insensitive).
  /// @return The specifier kind for \p Name, or nullopt if unknown.
  std::optional<uint32_t> getSpecifierForName(StringRef Name) const;

  /// Print \p Expr to \p OS using target asm syntax.
  /// @param OS Output stream.
  /// @param Expr Expression to print.
  void printExpr(raw_ostream &OS, const MCExpr &Expr) const;
  /// Print a target specifier expression to \p OS.
  /// @param OS Output stream.
  /// @param Expr Specifier expression to print.
  virtual void printSpecifierExpr(raw_ostream &OS,
                                  const MCSpecifierExpr &Expr) const {
    llvm_unreachable("Need to implement hook if target uses MCSpecifierExpr");
  }
  /// Evaluate \p E as a relocatable MCValue.
  /// @param E Specifier expression to evaluate.
  /// @param Res Destination for the relocatable value.
  /// @param Asm Optional assembler providing layout; may be null.
  /// @returns true on success.
  virtual bool evaluateAsRelocatableImpl(const MCSpecifierExpr &E,
                                         MCValue &Res,
                                         const MCAssembler *Asm) const;
};

} // end namespace llvm

#endif // LLVM_MC_MCASMINFO_H
