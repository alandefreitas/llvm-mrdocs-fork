//===- MCInstPrinter.h - MCInst to target assembly syntax -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCINSTPRINTER_H
#define LLVM_MC_MCINSTPRINTER_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>

namespace llvm {

class MCAsmInfo;
class MCInst;
class MCInstrAnalysis;
class MCInstrInfo;
class MCOperand;
class MCRegister;
class MCRegisterInfo;
class MCSubtargetInfo;
class StringRef;

/// Convert `Bytes' to a hex string and output to `OS'
/// @param Bytes Bytes to format as hexadecimal text.
/// @param OS Stream to write the hex string to.
LLVM_ABI void dumpBytes(ArrayRef<uint8_t> Bytes, raw_ostream &OS);

/// Styles for printing hexadecimal immediate values.
namespace HexStyle {

/// Hexadecimal number printing styles.
enum Style {
  C,  ///< 0xff
  Asm ///< 0ffh
};

} // end namespace HexStyle

struct AliasMatchingData;

/// This is an instance of a target assembly language printer that
/// converts an MCInst to valid target assembly syntax.
class LLVM_ABI MCInstPrinter {
protected:
  /// A stream that comments can be emitted to if desired.  Each comment
  /// must end with a newline.  This will be null if verbose assembly emission
  /// is disabled.
  raw_ostream *CommentStream = nullptr;
  /// Target assembly information used when printing instructions.
  const MCAsmInfo &MAI;
  /// Target instruction information used when printing instructions.
  const MCInstrInfo &MII;
  /// Target register information used when printing register names.
  const MCRegisterInfo &MRI;
  /// Optional instruction analysis helper used when symbolizing operands.
  const MCInstrAnalysis *MIA = nullptr;

  /// True if we are printing marked up assembly.
  bool UseMarkup = false;

  /// True if we are printing colored assembly.
  bool UseColor = false;

  /// True if we prefer aliases (e.g. nop) to raw mnemonics.
  bool PrintAliases = true;

  /// True if we are printing immediates as hex.
  bool PrintImmHex = false;

  /// Which style to use for printing hexadecimal values.
  HexStyle::Style PrintHexStyle = HexStyle::C;

  /// If true, a branch immediate (e.g. bl 4) will be printed as a hexadecimal
  /// address (e.g. bl 0x20004). This is useful for a stream disassembler
  /// (llvm-objdump -d).
  bool PrintBranchImmAsAddress = false;

  /// If true, symbolize branch target and memory reference operands.
  bool SymbolizeOperands = false;

  /// Stack of active markup colors for nested colored output regions.
  SmallVector<raw_ostream::Colors, 4> ColorStack{raw_ostream::Colors::RESET};

  /// Utility function for printing annotations.
  /// @param OS Stream to write the annotation to.
  /// @param Annot Annotation text to print.
  void printAnnotation(raw_ostream &OS, StringRef Annot);

  /// Helper for matching MCInsts to alias patterns when printing instructions.
  /// @param MI Instruction to match against alias patterns.
  /// @param STI Subtarget info used to evaluate feature conditions.
  /// @param M Tablegen-generated alias matching tables.
  /// @return Alias assembly string on match, or nullptr if none matches.
  const char *matchAliasPatterns(const MCInst *MI, const MCSubtargetInfo *STI,
                                 const AliasMatchingData &M);

public:
  /// Construct an instruction printer for the given target descriptions.
  /// @param mai Target assembly information.
  /// @param mii Target instruction information.
  /// @param mri Target register information.
  MCInstPrinter(const MCAsmInfo &mai, const MCInstrInfo &mii,
                const MCRegisterInfo &mri) : MAI(mai), MII(mii), MRI(mri) {}

  /// Destroy the instruction printer.
  virtual ~MCInstPrinter();

  /// Markup categories for colored or annotated assembly operands.
  enum class Markup {
    /// Immediate operand markup.
    Immediate,
    /// Register operand markup.
    Register,
    /// Branch or call target markup.
    Target,
    /// Memory operand markup.
    Memory,
  };

  /// RAII helper that wraps streamed output in markup and optional color.
  class WithMarkup {
  public:
    /// Begin a markup region on \p OS for category \p M.
    /// @param IP Printer whose color stack is updated when color is enabled.
    /// @param OS Stream receiving the marked-up output.
    /// @param M Markup category for this region.
    /// @param EnableMarkup Whether to emit markup delimiters.
    /// @param EnableColor Whether to emit ANSI color codes.
    LLVM_CTOR_NODISCARD LLVM_ABI WithMarkup(MCInstPrinter &IP, raw_ostream &OS,
                                            Markup M, bool EnableMarkup,
                                            bool EnableColor);
    /// End the markup region and restore prior color state.
    LLVM_ABI ~WithMarkup();

    /// Stream \p O into the marked-up output.
    /// @param O Value to write through the markup region.
    /// @return This markup helper for further streaming.
    template <typename T> WithMarkup &operator<<(T &O) {
      OS << O;
      return *this;
    }

    /// Stream const \p O into the marked-up output.
    /// @param O Const value to write through the markup region.
    /// @return This markup helper for further streaming.
    template <typename T> WithMarkup &operator<<(const T &O) {
      OS << O;
      return *this;
    }

  private:
    MCInstPrinter &IP;
    raw_ostream &OS;
    bool EnableMarkup;
    bool EnableColor;
  };

  /// Customize the printer according to a command line option.
  /// @param Opt Command-line option text to interpret.
  /// @return true if the option is recognized and applied.
  virtual bool applyTargetSpecificCLOption(StringRef Opt) { return false; }

  /// Specify a stream to emit comments to.
  /// @param OS Stream that will receive verbose assembly comments.
  void setCommentStream(raw_ostream &OS) { CommentStream = &OS; }

  /// Returns a pair containing the mnemonic for \p MI and the number of bits
  /// left for further processing by printInstruction (generated by tablegen).
  /// @param MI Instruction whose mnemonic should be returned.
  /// @return Mnemonic string and remaining bitflags for printInstruction.
  virtual std::pair<const char *, uint64_t>
  getMnemonic(const MCInst &MI) const = 0;

  /// Print the specified MCInst to the specified raw_ostream.
  ///
  /// \p Address the address of current instruction on most targets, used to
  /// print a PC relative immediate as the target address. On targets where a PC
  /// relative immediate is relative to the next instruction and the length of a
  /// MCInst is difficult to measure (e.g. x86), this is the address of the next
  /// instruction. If Address is 0, the immediate will be printed.
  /// @param MI Instruction to print.
  /// @param Address Instruction address used for PC-relative immediates.
  /// @param Annot Optional annotation text printed with the instruction.
  /// @param STI Subtarget info controlling available features and aliases.
  /// @param OS Stream to write the assembly to.
  virtual void printInst(const MCInst *MI, uint64_t Address, StringRef Annot,
                         const MCSubtargetInfo &STI, raw_ostream &OS) = 0;

  /// Return the name of the specified opcode enum (e.g. "MOV32ri") or
  /// empty if we can't resolve it.
  /// @param Opcode Opcode enumeration value to name.
  /// @return Opcode name string, or empty if the opcode is unresolved.
  StringRef getOpcodeName(unsigned Opcode) const;

  /// Print the assembler register name.
  /// @param OS Stream to write the register name to.
  /// @param Reg Register whose assembler name should be printed.
  virtual void printRegName(raw_ostream &OS, MCRegister Reg);

  /// Return whether markup is enabled for printed assembly.
  /// @return True if markup is enabled.
  bool getUseMarkup() const { return UseMarkup; }
  /// Enable or disable markup in printed assembly.
  /// @param Value True to enable markup.
  void setUseMarkup(bool Value) { UseMarkup = Value; }

  /// Return whether ANSI color is enabled for printed assembly.
  /// @return True if ANSI color is enabled.
  bool getUseColor() const { return UseColor; }
  /// Enable or disable ANSI color in printed assembly.
  /// @param Value True to enable color.
  void setUseColor(bool Value) { UseColor = Value; }

  /// Begin a markup region of category \p M on \p OS.
  /// @param OS Stream receiving the marked-up output.
  /// @param M Markup category for the region.
  /// @return RAII helper that applies markup until it is destroyed.
  WithMarkup markup(raw_ostream &OS, Markup M);

  /// Return whether immediates are printed in hexadecimal.
  /// @return True if immediates are printed as hex.
  bool getPrintImmHex() const { return PrintImmHex; }
  /// Enable or disable printing immediates as hexadecimal.
  /// @param Value True to print immediates as hex.
  void setPrintImmHex(bool Value) { PrintImmHex = Value; }

  /// Set the hexadecimal printing style.
  /// @param Value Style used when formatting hex values.
  void setPrintHexStyle(HexStyle::Style Value) { PrintHexStyle = Value; }

  /// Enable or disable printing branch immediates as addresses.
  /// @param Value True to print branch immediates as hex addresses.
  void setPrintBranchImmAsAddress(bool Value) {
    PrintBranchImmAsAddress = Value;
  }

  /// Enable or disable symbolizing branch and memory operands.
  /// @param Value True to symbolize branch target and memory operands.
  void setSymbolizeOperands(bool Value) { SymbolizeOperands = Value; }
  /// Set the optional instruction analysis helper used when printing.
  /// @param Value Analysis object, or nullptr to clear it.
  void setMCInstrAnalysis(const MCInstrAnalysis *Value) { MIA = Value; }

  /// Utility function to print immediates in decimal or hex.
  /// @param Value Immediate to format according to the current hex setting.
  /// @return Formatted immediate ready to stream to an output.
  format_object<int64_t> formatImm(int64_t Value) const {
    return PrintImmHex ? formatHex(Value) : formatDec(Value);
  }

  /// Utility functions to print decimal/hexadecimal values.
  /// @param Value Signed value to format as decimal text.
  /// @return Formatted decimal value ready to stream to an output.
  format_object<int64_t> formatDec(int64_t Value) const;
  /// Format a signed value as hexadecimal text.
  /// @param Value Signed value to format as hex.
  /// @return Formatted hex value ready to stream to an output.
  format_object<int64_t> formatHex(int64_t Value) const;
  /// Format an unsigned value as hexadecimal text.
  /// @param Value Unsigned value to format as hex.
  /// @return Formatted hex value ready to stream to an output.
  format_object<uint64_t> formatHex(uint64_t Value) const;
};

/// Map from opcode to pattern list by binary search.
struct PatternsForOpcode {
  /// Opcode whose alias patterns begin at \c PatternStart.
  uint32_t Opcode;
  /// Index of the first alias pattern for \c Opcode in the pattern table.
  uint16_t PatternStart;
  /// Number of consecutive alias patterns for \c Opcode.
  uint16_t NumPatterns;
};

/// Data for each alias pattern. Includes feature bits, string, number of
/// operands, and a variadic list of conditions to check.
struct AliasPattern {
  /// Byte offset of this pattern's assembly string in the string table.
  uint32_t AsmStrOffset;
  /// Index of the first condition for this pattern in the condition table.
  uint32_t AliasCondStart;
  /// Number of operands that participate in this alias match.
  uint8_t NumOperands;
  /// Number of consecutive conditions starting at \c AliasCondStart.
  uint8_t NumConds;
};

/// A single condition checked while matching an instruction alias pattern.
struct AliasPatternCond {
  /// Kinds of conditions that can appear in an alias pattern.
  enum CondKind : uint8_t {
    K_Feature,          ///< Match only if a feature is enabled.
    K_NegFeature,       ///< Match only if a feature is disabled.
    K_OrFeature,        ///< Match only if one of a set of features is enabled.
    K_OrNegFeature,     ///< Match only if one of a set of features is disabled.
    K_EndOrFeatures,    ///< Note end of list of K_Or(Neg)?Features.
    K_Ignore,           ///< Match any operand.
    K_Reg,              ///< Match a specific register.
    K_TiedReg,          ///< Match another already matched register.
    K_Imm,              ///< Match a specific immediate.
    K_RegClass,         ///< Match registers in a class.
    K_RegClassByHwMode, ///< Match registers in a class (by HwMode)
    K_Custom,           ///< Call custom matcher by index.
  };

  /// Kind of condition this entry encodes.
  CondKind Kind;
  /// Feature, register, immediate, class, or predicate index for \c Kind.
  uint32_t Value;
};

/// Tablegenerated data structures needed to match alias patterns.
struct AliasMatchingData {
  /// Opcode-to-pattern-list map sorted for binary search.
  ArrayRef<PatternsForOpcode> OpToPatterns;
  /// Flattened table of alias patterns.
  ArrayRef<AliasPattern> Patterns;
  /// Flattened table of alias pattern conditions.
  ArrayRef<AliasPatternCond> PatternConds;
  /// Packed assembly strings referenced by pattern offsets.
  StringRef AsmStrings;
  /// Optional predicate that validates a matched MCOperand.
  bool (*ValidateMCOperand)(const MCOperand &MCOp, const MCSubtargetInfo &STI,
                            unsigned PredicateIndex);
};

} // end namespace llvm

#endif // LLVM_MC_MCINSTPRINTER_H
