//===- llvm/MC/MCTargetAsmParser.h - Target Assembly Parser -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCPARSER_MCTARGETASMPARSER_H
#define LLVM_MC_MCPARSER_MCTARGETASMPARSER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCParser/MCAsmParserExtension.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCRegister.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include <cstdint>
#include <memory>

namespace llvm {

class MCContext;
class MCInst;
class MCInstrInfo;
class MCStreamer;
class MCSubtargetInfo;
class MCSymbol;
template <typename T> class SmallVectorImpl;

/// Vector of owned parsed assembly operands.
using OperandVector = SmallVectorImpl<std::unique_ptr<MCParsedAsmOperand>>;

/// Kind of rewrite applied when converting MS-style inline assembly.
enum AsmRewriteKind {
  /// Rewrite \c align as \c .align.
  AOK_Align,
  /// Rewrite \c even as \c .even.
  AOK_EVEN,
  /// Rewrite \c _emit as \c .byte.
  AOK_Emit,
  /// Rewrite in terms of \c ${N:P}.
  AOK_CallInput,
  /// Rewrite in terms of \c $N.
  AOK_Input,
  /// Rewrite in terms of \c $N.
  AOK_Output,
  /// Add a sizing directive (e.g., \c dword ptr).
  AOK_SizeDirective,
  /// Rewrite local labels.
  AOK_Label,
  /// Add an end-of-statement (e.g., newline and tab).
  AOK_EndOfStatement,
  /// Skip emission (e.g., offset/type operators).
  AOK_Skip,
  /// Compound Intel expression rewrite.
  ///
  /// SizeDirective SymDisp [BaseReg + IndexReg * Scale + ImmDisp].
  AOK_IntelExpr
};

/// Precedence for each \c AsmRewriteKind when applying rewrites.
const char AsmRewritePrecedence [] = {
  2, // AOK_Align
  2, // AOK_EVEN
  2, // AOK_Emit
  3, // AOK_Input
  3, // AOK_CallInput
  3, // AOK_Output
  5, // AOK_SizeDirective
  1, // AOK_Label
  5, // AOK_EndOfStatement
  2, // AOK_Skip
  2  // AOK_IntelExpr
};

/// Parts of an Intel-style memory expression used when emitting compound forms.
struct IntelExpr {
  /// Whether the expression should be emitted inside brackets.
  bool NeedBracs = false;
  /// Immediate displacement component.
  int64_t Imm = 0;
  /// Base register name, if any.
  StringRef BaseReg;
  /// Index register name, if any.
  StringRef IndexReg;
  /// Symbolic offset name, if any.
  StringRef OffsetName;
  /// Scale factor for the index register (1, 2, 4, or 8).
  unsigned Scale = 1;

  /// Construct an empty Intel expression.
  IntelExpr() = default;
  /// Construct an Intel expression from its components.
  ///
  /// Forms \c [BaseReg + IndexReg * Scale + OFFSET name + Imm].
  /// \param baseReg Base register name.
  /// \param indexReg Index register name.
  /// \param scale Index scale; non-zero values replace the default of 1.
  /// \param offsetName Symbolic offset name.
  /// \param imm Immediate displacement.
  /// \param needBracs Whether to emit brackets around the expression.
  IntelExpr(StringRef baseReg, StringRef indexReg, unsigned scale,
            StringRef offsetName, int64_t imm, bool needBracs)
      : NeedBracs(needBracs), Imm(imm), BaseReg(baseReg), IndexReg(indexReg),
        OffsetName(offsetName), Scale(1) {
    if (scale)
      Scale = scale;
  }
  /// Return true if a base register is present.
  ///
  /// \return True if a base register is present.
  bool hasBaseReg() const { return !BaseReg.empty(); }
  /// Return true if an index register is present.
  ///
  /// \return True if an index register is present.
  bool hasIndexReg() const { return !IndexReg.empty(); }
  /// Return true if a base or index register is present.
  ///
  /// \return True if a base or index register is present.
  bool hasRegs() const { return hasBaseReg() || hasIndexReg(); }
  /// Return true if a symbolic offset name is present.
  ///
  /// \return True if a symbolic offset name is present.
  bool hasOffset() const { return !OffsetName.empty(); }
  /// Return true if the immediate should be emitted alone.
  ///
  /// Normally immediates are not emitted unconditionally unless there are no
  /// other components.
  /// \return True if the immediate should be emitted alone.
  bool emitImm() const { return !(hasRegs() || hasOffset()); }
  /// Return true if the scale is valid for this expression.
  ///
  /// \return True if the scale is valid for this expression.
  bool isValid() const {
    return (Scale == 1) ||
           (hasIndexReg() && (Scale == 2 || Scale == 4 || Scale == 8));
  }
};

/// Description of a rewrite to apply when converting MS-style inline assembly.
struct AsmRewrite {
  /// Kind of rewrite to perform.
  AsmRewriteKind Kind;
  /// Source location of the text to rewrite.
  SMLoc Loc;
  /// Length of the text span to rewrite.
  unsigned Len;
  /// Whether this rewrite has already been applied.
  bool Done;
  /// Immediate or numeric value associated with the rewrite, if any.
  int64_t Val;
  /// Label text for label rewrites.
  StringRef Label;
  /// Intel expression payload for \c AOK_IntelExpr rewrites.
  IntelExpr IntelExp;
  /// Whether the Intel expression is subject to restricted forms.
  bool IntelExpRestricted;

public:
  /// Construct a rewrite of the given kind at \p loc.
  /// \param kind Rewrite kind.
  /// \param loc Source location of the rewrite.
  /// \param len Length of the rewritten span.
  /// \param val Optional numeric value for the rewrite.
  /// \param Restricted Whether Intel-expression rewrites are restricted.
  AsmRewrite(AsmRewriteKind kind, SMLoc loc, unsigned len = 0, int64_t val = 0,
             bool Restricted = false)
      : Kind(kind), Loc(loc), Len(len), Done(false), Val(val) {
    IntelExpRestricted = Restricted;
  }
  /// Construct a label rewrite at \p loc.
  /// \param kind Rewrite kind.
  /// \param loc Source location of the rewrite.
  /// \param len Length of the rewritten span.
  /// \param label Label text to use.
  AsmRewrite(AsmRewriteKind kind, SMLoc loc, unsigned len, StringRef label)
    : AsmRewrite(kind, loc, len) { Label = label; }
  /// Construct an Intel-expression rewrite at \p loc.
  /// \param loc Source location of the rewrite.
  /// \param len Length of the rewritten span.
  /// \param exp Intel expression to emit.
  AsmRewrite(SMLoc loc, unsigned len, IntelExpr exp)
    : AsmRewrite(AOK_IntelExpr, loc, len) { IntelExp = exp; }
};

/// Contextual information passed while parsing a single instruction.
struct ParseInstructionInfo {
  /// Optional list of MS inline-assembly rewrites collected during parsing.
  SmallVectorImpl<AsmRewrite> *AsmRewrites = nullptr;

  /// Construct with no rewrite list.
  ParseInstructionInfo() = default;
  /// Construct with an optional rewrite list.
  /// \param rewrites Rewrite list to populate, or null.
  ParseInstructionInfo(SmallVectorImpl<AsmRewrite> *rewrites)
    : AsmRewrites(rewrites) {}
};

/// Ternary parse status returned by various parse* methods.
class ParseStatus {
  enum class StatusTy {
    Success, // Parsing Succeeded
    Failure, // Parsing Failed after consuming some tokens
    NoMatch, // Parsing Failed without consuming any tokens
  } Status;

public:
#if __cplusplus >= 202002L
  using enum StatusTy;
#else
  static constexpr StatusTy Success = StatusTy::Success;
  static constexpr StatusTy Failure = StatusTy::Failure;
  static constexpr StatusTy NoMatch = StatusTy::NoMatch;
#endif

  /// Construct a no-match status.
  constexpr ParseStatus() : Status(NoMatch) {}

  /// Construct from an explicit status value.
  /// \param Status Status to store.
  constexpr ParseStatus(StatusTy Status) : Status(Status) {}

  /// Construct success or failure from a boolean error flag.
  /// \param Error True for failure, false for success.
  constexpr ParseStatus(bool Error) : Status(Error ? Failure : Success) {}

  /// Deleted: reject accidental construction from unrelated types.
  /// \param Value Unused; construction from unrelated types is deleted.
  template <typename T> constexpr ParseStatus(T Value) = delete;

  /// Return true if parsing succeeded.
  ///
  /// \return True if parsing succeeded.
  constexpr bool isSuccess() const { return Status == StatusTy::Success; }
  /// Return true if parsing failed after consuming tokens.
  ///
  /// \return True if parsing failed after consuming tokens.
  constexpr bool isFailure() const { return Status == StatusTy::Failure; }
  /// Return true if parsing failed without consuming tokens.
  ///
  /// \return True if parsing failed without consuming tokens.
  constexpr bool isNoMatch() const { return Status == StatusTy::NoMatch; }
};

/// Lightweight match / near-match / no-match result for operand predicates.
///
/// When an operand is parsed, the assembler will try to iterate through a set
/// of possible operand classes that the operand might match and call the
/// corresponding PredicateMethod to determine that.
///
/// If there are two AsmOperands that would give a specific diagnostic if there
/// is no match, there is currently no mechanism to distinguish which operand is
/// a closer match. The DiagnosticPredicate distinguishes between 'completely
/// no match' and 'near match', so the assembler can decide whether to give a
/// specific diagnostic, or use 'InvalidOperand' and continue to find a
/// 'better matching' diagnostic.
///
/// For example:
///    opcode opnd0, onpd1, opnd2
///
/// where:
///    opnd2 could be an 'immediate of range [-8, 7]'
///    opnd2 could be a  'register + shift/extend'.
///
/// If opnd2 is a valid register, but with a wrong shift/extend suffix, it makes
/// little sense to give a diagnostic that the operand should be an immediate
/// in range [-8, 7].
///
/// This is a light-weight alternative to the 'NearMissInfo' approach
/// below which collects *all* possible diagnostics. This alternative
/// is optional and fully backward compatible with existing
/// PredicateMethods that return a 'bool' (match or near match).
struct DiagnosticPredicate {
  /// Match closeness reported by a target operand predicate.
  enum PredicateTy {
    /// Operand matches the expected class.
    Match,
    /// Close match: prefer a specific diagnostic.
    NearMatch,
    /// No match: use \c InvalidOperand.
    NoMatch,
  } Predicate;

  /// Construct from an explicit predicate result.
  /// \param T Predicate result to store.
  constexpr DiagnosticPredicate(PredicateTy T) : Predicate(T) {}

  /// Construct match or near-match from a boolean.
  ///
  /// True becomes \c Match; false becomes \c NearMatch (not \c NoMatch), for
  /// compatibility with predicates that previously returned \c bool.
  /// \param Matches True if the operand matched.
  explicit constexpr DiagnosticPredicate(bool Matches)
      : Predicate(Matches ? Match : NearMatch) {}

  /// Return true if the predicate reported a full match.
  ///
  /// \return True if the predicate reported a full match.
  explicit operator bool() const { return Predicate == Match; }

  /// Return true if the operand matched.
  ///
  /// \return True if the operand matched.
  constexpr bool isMatch() const { return Predicate == Match; }
  /// Return true if the operand nearly matched.
  ///
  /// \return True if the operand nearly matched.
  constexpr bool isNearMatch() const { return Predicate == NearMatch; }
  /// Return true if the operand did not match.
  ///
  /// \return True if the operand did not match.
  constexpr bool isNoMatch() const { return Predicate == NoMatch; }
};

/// Information about one near-miss encoding when instruction matching fails.
///
/// When matching of an assembly instruction fails, there may be multiple
/// encodings that are close to being a match. It's often ambiguous which one
/// the programmer intended to use, so we want to report an error which mentions
/// each of these "near-miss" encodings. This struct contains information about
/// one such encoding, and why it did not match the parsed instruction.
class NearMissInfo {
public:
  /// Classification of why an encoding was a near miss.
  enum NearMissKind {
    /// Not a near miss (successful match sentinel).
    NoNearMiss,
    /// One parsed operand had the wrong class.
    NearMissOperand,
    /// Required target features are not enabled.
    NearMissFeature,
    /// Target-specific predicate rejected the encoding.
    NearMissPredicate,
    /// Fewer operands were parsed than the encoding expects.
    NearMissTooFewOperands,
  };

  /// Return a success sentinel used internally by the table-generated matcher.
  ///
  /// The encoding is valid for the parsed assembly string.
  /// \return A success sentinel indicating a valid encoding.
  static NearMissInfo getSuccess() { return NearMissInfo(); }

  /// Return a near miss for missing target features.
  ///
  /// The instruction encoding is not valid because it requires some target
  /// features that are not currently enabled. MissingFeatures has a bit set for
  /// each feature that the encoding needs but which is not enabled.
  /// \param MissingFeatures Features required but not enabled.
  /// \return A near miss for missing target features.
  static NearMissInfo getMissedFeature(const FeatureBitset &MissingFeatures) {
    NearMissInfo Result;
    Result.Kind = NearMissFeature;
    Result.Features = MissingFeatures;
    return Result;
  }

  /// Return a near miss from a failed target predicate.
  ///
  /// The instruction encoding is not valid because the target-specific
  /// predicate function returned an error code. FailureCode is the
  /// target-specific error code returned by the predicate.
  /// \param FailureCode Target-specific predicate error code.
  /// \return A near miss from a failed target predicate.
  static NearMissInfo getMissedPredicate(unsigned FailureCode) {
    NearMissInfo Result;
    Result.Kind = NearMissPredicate;
    Result.PredicateError = FailureCode;
    return Result;
  }

  /// Return a near miss for a single incorrect operand.
  ///
  /// The instruction encoding is not valid because one (and only one) parsed
  /// operand is not of the correct type. OperandError is the error code
  /// relating to the operand class expected by the encoding. OperandClass is
  /// the type of the expected operand. Opcode is the opcode of the encoding.
  /// OperandIndex is the index into the parsed operand list.
  /// \param OperandError Error code for the expected operand class.
  /// \param OperandClass Expected operand class.
  /// \param Opcode Opcode of the encoding being matched.
  /// \param OperandIndex Index of the mismatched parsed operand.
  /// \return A near miss describing a single incorrect operand.
  static NearMissInfo getMissedOperand(unsigned OperandError,
                                       unsigned OperandClass, unsigned Opcode,
                                       unsigned OperandIndex) {
    NearMissInfo Result;
    Result.Kind = NearMissOperand;
    Result.MissedOperand.Error = OperandError;
    Result.MissedOperand.Class = OperandClass;
    Result.MissedOperand.Opcode = Opcode;
    Result.MissedOperand.Index = OperandIndex;
    return Result;
  }

  /// Return a near miss when too few operands were parsed.
  ///
  /// The instruction encoding is not valid because it expects more operands
  /// than were parsed. OperandClass is the class of the expected operand that
  /// was not provided. Opcode is the instruction encoding.
  /// \param OperandClass Class of the missing expected operand.
  /// \param Opcode Opcode of the encoding being matched.
  /// \return A near miss describing too few operands for the encoding.
  static NearMissInfo getTooFewOperands(unsigned OperandClass,
                                        unsigned Opcode) {
    NearMissInfo Result;
    Result.Kind = NearMissTooFewOperands;
    Result.TooFewOperands.Class = OperandClass;
    Result.TooFewOperands.Opcode = Opcode;
    return Result;
  }

  /// Return true if this describes an actual near miss.
  ///
  /// \return True if this describes an actual near miss.
  operator bool() const { return Kind != NoNearMiss; }

  /// Return the near-miss kind.
  ///
  /// \return The near-miss kind.
  NearMissKind getKind() const { return Kind; }

  /// Return features required by the instruction but missing on the target.
  ///
  /// \return Features required by the instruction but missing on the target.
  const FeatureBitset& getFeatures() const {
    assert(Kind == NearMissFeature);
    return Features;
  }
  /// Return the target predicate error code for this encoding.
  ///
  /// \return The target predicate error code for this encoding.
  unsigned getPredicateError() const {
    assert(Kind == NearMissPredicate);
    return PredicateError;
  }
  /// Return the expected operand class that did not match.
  ///
  /// \return The expected operand class that did not match.
  unsigned getOperandClass() const {
    assert(Kind == NearMissOperand || Kind == NearMissTooFewOperands);
    return MissedOperand.Class;
  }
  /// Return the opcode of the encoding that was nearly matched.
  ///
  /// \return The opcode of the encoding that was nearly matched.
  unsigned getOpcode() const {
    assert(Kind == NearMissOperand || Kind == NearMissTooFewOperands);
    return MissedOperand.Opcode;
  }
  /// Return the operand-validation error code.
  ///
  /// \return The operand-validation error code.
  unsigned getOperandError() const {
    assert(Kind == NearMissOperand);
    return MissedOperand.Error;
  }
  /// Return the index of the mismatched operand in the parsed list.
  ///
  /// \return The index of the mismatched operand in the parsed list.
  unsigned getOperandIndex() const {
    assert(Kind == NearMissOperand);
    return MissedOperand.Index;
  }

private:
  NearMissKind Kind;

  // These two structs share a common prefix, so we can safely rely on the fact
  // that they overlap in the union.
  struct MissedOpInfo {
    unsigned Class;
    unsigned Opcode;
    unsigned Error;
    unsigned Index;
  };

  struct TooFewOperandsInfo {
    unsigned Class;
    unsigned Opcode;
  };

  union {
    /// Features required but not enabled for a feature near miss.
    FeatureBitset Features;
    /// Target predicate error code for a predicate near miss.
    unsigned PredicateError;
    /// Details of a single mismatched operand.
    MissedOpInfo MissedOperand;
    /// Details when too few operands were provided.
    TooFewOperandsInfo TooFewOperands;
  };

  NearMissInfo() : Kind(NoNearMiss) {}
};

/// MCTargetAsmParser - Generic interface to target specific assembly parsers.
class LLVM_ABI MCTargetAsmParser : public MCAsmParserExtension {
public:
  /// Result codes returned by instruction matching.
  enum MatchResultTy {
    /// Operand did not match the expected class.
    Match_InvalidOperand,
    /// Tied operand constraint was not satisfied.
    Match_InvalidTiedOperand,
    /// Required target features are not available.
    Match_MissingFeature,
    /// Mnemonic did not match any instruction.
    Match_MnemonicFail,
    /// Instruction matched successfully.
    Match_Success,
    /// Matching produced near-miss encodings.
    Match_NearMisses,
    /// First value available for target-specific match results.
    FIRST_TARGET_MATCH_RESULT_TY
  };

protected: // Can only create subclasses.
  /// Construct a target asm parser for \p STI using \p MII.
  /// \param STI Subtarget info for the current target.
  /// \param MII Instruction info table.
  MCTargetAsmParser(const MCSubtargetInfo &STI, const MCInstrInfo &MII);

  /// Create a copy of STI and return a non-const reference to it.
  ///
  /// \return A non-const reference to the copied subtarget info.
  MCSubtargetInfo &copySTI();

  /// AvailableFeatures - The current set of available features.
  FeatureBitset AvailableFeatures;

  /// ParsingMSInlineAsm - Are we parsing ms-style inline assembly?
  bool ParsingMSInlineAsm = false;

  /// SemaCallback - The Sema callback implementation.  Must be set when parsing
  /// ms-style inline assembly.
  MCAsmParserSemaCallback *SemaCallback = nullptr;

  /// Current STI.
  const MCSubtargetInfo *STI;

  /// Instruction info table for the target.
  const MCInstrInfo &MII;

public:
  /// Deleted copy constructor.
  /// \param Other Unused; copy construction is deleted.
  MCTargetAsmParser(const MCTargetAsmParser &Other) = delete;
  /// Deleted copy assignment.
  /// \param Other Unused; copy assignment is deleted.
  MCTargetAsmParser &operator=(const MCTargetAsmParser &Other) = delete;

  /// Destroy the target asm parser.
  ~MCTargetAsmParser() override;

  /// Return the current subtarget info.
  ///
  /// \return The current subtarget info.
  const MCSubtargetInfo &getSTI() const;

  /// Return the set of currently available target features.
  ///
  /// \return The set of currently available target features.
  const FeatureBitset& getAvailableFeatures() const {
    return AvailableFeatures;
  }
  /// Set the available target features.
  /// \param Value Feature bitset to install.
  void setAvailableFeatures(const FeatureBitset& Value) {
    AvailableFeatures = Value;
  }

  /// Return true if MS-style inline assembly is being parsed.
  ///
  /// \return True if MS-style inline assembly is being parsed.
  bool isParsingMSInlineAsm () { return ParsingMSInlineAsm; }
  /// Set whether MS-style inline assembly is being parsed.
  /// \param Value True to enable MS inline-asm mode.
  void setParsingMSInlineAsm (bool Value) { ParsingMSInlineAsm = Value; }

  /// Return the MC target options from the parser context.
  ///
  /// \return The MC target options from the parser context.
  const MCTargetOptions &getTargetOptions() const {
    return const_cast<MCTargetAsmParser *>(this)
        ->getParser()
        .getContext()
        .getTargetOptions();
  }

  /// Install the Sema callback used when parsing MS inline assembly.
  /// \param Callback Callback implementation to use.
  void setSemaCallback(MCAsmParserSemaCallback *Callback) {
    SemaCallback = Callback;
  }

  /// Parse a target-specific primary expression.
  /// \param Res [out] Parsed expression on success.
  /// \param EndLoc [out] Location of the end of the expression.
  /// \return True on failure.
  virtual bool parsePrimaryExpr(const MCExpr *&Res, SMLoc &EndLoc) {
    return getParser().parsePrimaryExpr(Res, EndLoc, nullptr);
  }
  /// Parse an expression in a data directive, possibly with a relocation.
  /// \param Res [out] Parsed expression on success.
  /// \return True on failure.
  virtual bool parseDataExpr(const MCExpr *&Res) {
    SMLoc EndLoc;
    return getParser().parseExpression(Res, EndLoc);
  }

  /// Parse a register operand.
  /// \param Reg [out] Parsed register.
  /// \param StartLoc [out] Location of the first token.
  /// \param EndLoc [out] Location of the last token.
  /// \return True on failure.
  virtual bool parseRegister(MCRegister &Reg, SMLoc &StartLoc,
                             SMLoc &EndLoc) = 0;

  /// tryParseRegister - parse one register if possible
  ///
  /// Check whether a register specification can be parsed at the current
  /// location, without failing the entire parse if it can't. Must not consume
  /// tokens if the parse fails.
  /// \param Reg [out] Parsed register on success.
  /// \param StartLoc [out] Location of the first token.
  /// \param EndLoc [out] Location of the last token.
  /// \return Parse status indicating success, failure, or no match.
  virtual ParseStatus tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                       SMLoc &EndLoc) = 0;

  /// Parse one assembly instruction.
  ///
  /// The parser is positioned following the instruction name. The target
  /// specific instruction parser should parse the entire instruction and
  /// construct the appropriate MCInst, or emit an error. On success, the entire
  /// line should be parsed up to and including the end-of-statement token. On
  /// failure, the parser is not required to read to the end of the line.
  ///
  /// \param Info Parse context, including optional MS rewrite state.
  /// \param Name The instruction name.
  /// \param NameLoc The source location of the name.
  /// \param Operands [out] The list of parsed operands, this returns
  ///        ownership of them to the caller.
  /// \return True on failure.
  virtual bool parseInstruction(ParseInstructionInfo &Info, StringRef Name,
                                SMLoc NameLoc, OperandVector &Operands) = 0;
  /// Parse one assembly instruction using a name token for the location.
  /// \param Info Parse context, including optional MS rewrite state.
  /// \param Name The instruction name.
  /// \param Token Token whose location is used as the name location.
  /// \param Operands [out] Parsed operands; ownership returned to the caller.
  /// \return True on failure.
  virtual bool parseInstruction(ParseInstructionInfo &Info, StringRef Name,
                                AsmToken Token, OperandVector &Operands) {
    return parseInstruction(Info, Name, Token.getLoc(), Operands);
  }

  /// ParseDirective - Parse a target specific assembler directive
  /// This method is deprecated, use 'parseDirective' instead.
  ///
  /// The parser is positioned following the directive name.  The target
  /// specific directive parser should parse the entire directive doing or
  /// recording any target specific work, or return true and do nothing if the
  /// directive is not target specific. If the directive is specific for
  /// the target, the entire line is parsed up to and including the
  /// end-of-statement token and false is returned.
  ///
  /// \param DirectiveID - the identifier token of the directive.
  /// \return False if the directive was handled; true if it is not
  ///         target-specific.
  virtual bool ParseDirective(AsmToken DirectiveID) { return true; }

  /// Parses a target-specific assembler directive.
  ///
  /// The parser is positioned following the directive name. The target-specific
  /// directive parser should parse the entire directive doing or recording any
  /// target-specific work, or emit an error. On success, the entire line should
  /// be parsed up to and including the end-of-statement token. On failure, the
  /// parser is not required to read to the end of the line. If the directive is
  /// not target-specific, no tokens should be consumed and NoMatch is returned.
  ///
  /// \param DirectiveID - The token identifying the directive.
  /// \return Success or failure after consuming the directive, or NoMatch if
  ///         it is not target-specific.
  virtual ParseStatus parseDirective(AsmToken DirectiveID);

  /// Match parsed operands to an MCInst and emit it.
  ///
  /// Recognize a series of operands of a parsed instruction as an actual MCInst
  /// and emit it to the specified MCStreamer. This returns false on success and
  /// returns true on failure to match.
  ///
  /// On failure, the target parser is responsible for emitting a diagnostic
  /// explaining the match failure.
  /// \param IDLoc Location of the instruction mnemonic.
  /// \param Opcode [out] Matched instruction opcode.
  /// \param Operands Parsed operands to match.
  /// \param Out Streamer that receives the emitted instruction.
  /// \param ErrorInfo [out] Target-specific match error detail.
  /// \param MatchingInlineAsm True when matching MS inline assembly.
  /// \return True on failure to match.
  virtual bool matchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                                       OperandVector &Operands, MCStreamer &Out,
                                       uint64_t &ErrorInfo,
                                       bool MatchingInlineAsm) = 0;

  /// Allows targets to let registers opt out of clobber lists.
  /// \param Reg Register that may be omitted from clobber lists.
  /// \return True if \p Reg should be omitted.
  virtual bool omitRegisterFromClobberLists(MCRegister Reg) { return false; }

  /// Perform target-specific operand class validation.
  ///
  /// Allow a target to add special case operand matching for things that
  /// tblgen doesn't/can't handle effectively. For example, literal
  /// immediates on ARM. TableGen expects a token operand, but the parser
  /// will recognize them as immediates.
  /// \param Op Parsed operand to validate.
  /// \param Kind Expected match-class kind.
  /// \return A \c MatchResultTy code; default is \c Match_InvalidOperand.
  virtual unsigned validateTargetOperandClass(MCParsedAsmOperand &Op,
                                              unsigned Kind) {
    return Match_InvalidOperand;
  }

  /// Validate the instruction match against any complex target predicates
  /// before rendering any operands to it.
  /// \param Inst Instruction under construction.
  /// \param Operands Parsed operands being matched.
  /// \return A \c MatchResultTy code; default is \c Match_Success.
  virtual unsigned
  checkEarlyTargetMatchPredicate(MCInst &Inst, const OperandVector &Operands) {
    return Match_Success;
  }

  /// checkTargetMatchPredicate - Validate the instruction match against
  /// any complex target predicates not expressible via match classes.
  /// \param Inst Matched instruction to validate.
  /// \return A \c MatchResultTy code; default is \c Match_Success.
  virtual unsigned checkTargetMatchPredicate(MCInst &Inst) {
    return Match_Success;
  }

  /// Convert matched operands into inline-asm operand maps and constraints.
  /// \param Kind Match-class kind for the conversion.
  /// \param Operands Parsed operands to convert.
  virtual void convertToMapAndConstraints(unsigned Kind,
                                          const OperandVector &Operands) = 0;

  /// Return whether two operands are equal registers.
  ///
  /// Returns whether two operands are registers and are equal. This is used
  /// by the tied-operands checks in the AsmMatcher. This method can be
  /// overridden to allow e.g. a sub- or super-register as the tied operand.
  /// \param Op1 First parsed operand.
  /// \param Op2 Second parsed operand.
  /// \return True if both are registers and considered equal.
  virtual bool areEqualRegs(const MCParsedAsmOperand &Op1,
                            const MCParsedAsmOperand &Op2) const;

  /// Return whether this parser treats \c = as an asm assignment.
  ///
  /// \return True if \c = is treated as an asm assignment.
  virtual bool equalIsAsmAssignment() { return true; };
  /// Return whether the given start-of-statement token may be a label.
  /// \param Token Token at the start of the statement.
  /// \return True if \p Token may introduce a label.
  virtual bool isLabel(AsmToken &Token) { return true; };
  /// Return whether the given token kind may start a statement.
  /// \param Token Token kind to test.
  /// \return True if \p Token is accepted as start of statement.
  virtual bool tokenIsStartOfStatement(AsmToken::TokenKind Token) {
    return false;
  }

  /// Apply a relocation specifier to expression \p E, if supported.
  /// \param E Expression to modify.
  /// \param Spec Relocation specifier to apply.
  /// \param Ctx Assembler context used to build the result.
  /// \return Modified expression, or null if unsupported.
  virtual const MCExpr *applySpecifier(const MCExpr *E, uint32_t Spec,
                                       MCContext &Ctx) {
    return nullptr;
  }

  /// Perform target actions immediately before a label is emitted.
  /// \param Symbol Symbol about to be emitted as a label.
  /// \param IDLoc Source location of the label identifier.
  virtual void doBeforeLabelEmit(MCSymbol *Symbol, SMLoc IDLoc) {}

  /// Hook invoked after a label has been parsed.
  /// \param Symbol Parsed label symbol.
  virtual void onLabelParsed(MCSymbol *Symbol) {}

  /// Ensure that all previously parsed instructions have been emitted to the
  /// output streamer, if the target does not emit them immediately.
  /// \param Out Streamer that should receive any pending instructions.
  virtual void flushPendingInstructions(MCStreamer &Out) {}

  /// Perform target-specific initialization at the start of a file.
  virtual void onBeginOfFile() {}

  /// Perform target-specific checks or cleanup at the end of a file.
  virtual void onEndOfFile() {}
};

} // end namespace llvm

#endif // LLVM_MC_MCPARSER_MCTARGETASMPARSER_H
