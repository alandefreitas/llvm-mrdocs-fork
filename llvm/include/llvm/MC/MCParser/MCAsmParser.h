//===- llvm/MC/MCAsmParser.h - Abstract Asm Parser Interface ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCPARSER_MCASMPARSER_H
#define LLVM_MC_MCPARSER_MCASMPARSER_H

#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/MC/MCAsmMacro.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCParser/AsmLexer.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/SMLoc.h"
#include <cstdint>
#include <string>
#include <utility>

namespace llvm {

class MCAsmInfo;
class MCAsmParserExtension;
class MCExpr;
class MCInstPrinter;
class MCInstrInfo;
class MCStreamer;
class MCTargetAsmParser;
class SourceMgr;

/// Parsed identity of an identifier in MS-style inline assembly.
struct InlineAsmIdentifierInfo {
  /// Kind of identifier resolved by the Sema callback.
  enum IdKind {
    IK_Invalid, ///< Initial state; unexpected after successful parsing.
    IK_Label,   ///< Function or label reference.
    IK_EnumVal, ///< Value of an enumeration type.
    IK_Var      ///< Variable.
  };
  /// Enum-constant form of an inline-asm identifier.
  struct EnumIdentifier {
    int64_t EnumVal; ///< Numeric value of the enumeration constant.
  };
  /// Label or function reference form of an inline-asm identifier.
  struct LabelIdentifier {
    void *Decl; ///< Opaque declaration for the label or function.
  };
  /// Variable form of an inline-asm identifier.
  struct VariableIdentifier {
    void *Decl;      ///< Opaque declaration for the variable.
    bool IsGlobalLV; ///< True if the variable is a global LLVM value.
    unsigned Length; ///< Number of elements (Size / Type).
    unsigned Size;   ///< Total size in bytes.
    unsigned Type;   ///< Element size in bytes.
  };
  /// Active arm of the identifier; discriminated by \c Kind.
  union {
    EnumIdentifier Enum;     ///< Valid when \c Kind is \c IK_EnumVal.
    LabelIdentifier Label;   ///< Valid when \c Kind is \c IK_Label.
    VariableIdentifier Var;  ///< Valid when \c Kind is \c IK_Var.
  };
  /// Return true if this identifier currently has kind \p kind.
  ///
  /// \param kind Identifier kind to compare against.
  /// \return True if \c Kind equals \p kind.
  bool isKind(IdKind kind) const { return Kind == kind; }
  /// Initialize this identifier as an enumeration constant.
  ///
  /// \param enumVal The enumeration value to store.
  void setEnum(int64_t enumVal) {
    assert(isKind(IK_Invalid) && "should be initialized only once");
    Kind = IK_EnumVal;
    Enum.EnumVal = enumVal;
  }
  /// Initialize this identifier as a label or function reference.
  ///
  /// \param decl Opaque declaration for the label or function.
  void setLabel(void *decl) {
    assert(isKind(IK_Invalid) && "should be initialized only once");
    Kind = IK_Label;
    Label.Decl = decl;
  }
  /// Initialize this identifier as a variable.
  ///
  /// \param decl Opaque declaration for the variable.
  /// \param isGlobalLV True if the variable is a global LLVM value.
  /// \param size Total size of the variable in bytes.
  /// \param type Element size in bytes.
  void setVar(void *decl, bool isGlobalLV, unsigned size, unsigned type) {
    assert(isKind(IK_Invalid) && "should be initialized only once");
    Kind = IK_Var;
    Var.Decl = decl;
    Var.IsGlobalLV = isGlobalLV;
    Var.Size = size;
    Var.Type = type;
    Var.Length = size / type;
  }
  /// Construct an invalid (uninitialized) identifier info.
  InlineAsmIdentifierInfo() = default;

private:
  // Discriminate using the current kind.
  IdKind Kind = IK_Invalid;
};

/// Generic type information for an assembly object.
///
/// All sizes are measured in bytes.
struct AsmTypeInfo {
  StringRef Name;          ///< Type name as spelled in the assembly.
  unsigned Size = 0;       ///< Total size of the object in bytes.
  unsigned ElementSize = 0; ///< Size of one element in bytes.
  unsigned Length = 0;     ///< Number of elements (Size / ElementSize).
};

/// Field within an assembly structure or type.
struct AsmFieldInfo {
  AsmTypeInfo Type;    ///< Type information for the field.
  unsigned Offset = 0; ///< Byte offset of the field within its parent.
};

/// Generic Sema callback for assembly parser.
class LLVM_ABI MCAsmParserSemaCallback {
public:
  /// Destroy this Sema callback.
  virtual ~MCAsmParserSemaCallback();

  /// Look up an inline-asm identifier starting at \p LineBuf.
  ///
  /// \param LineBuf Input text; updated to the remainder after the identifier.
  /// \param Info Filled with the resolved identifier kind and payload.
  /// \param IsUnevaluatedContext True when the identifier appears in an
  /// unevaluated operand context.
  virtual void LookupInlineAsmIdentifier(StringRef &LineBuf,
                                         InlineAsmIdentifierInfo &Info,
                                         bool IsUnevaluatedContext) = 0;
  /// Look up or create an inline-asm label named \p Identifier.
  ///
  /// \param Identifier Label name to resolve.
  /// \param SM Source manager for the enclosing translation unit.
  /// \param Location Source location of the label reference.
  /// \param Create If true, create the label when it does not already exist.
  /// \return The canonical label name after lookup or creation.
  virtual StringRef LookupInlineAsmLabel(StringRef Identifier, SourceMgr &SM,
                                         SMLoc Location, bool Create) = 0;
  /// Look up the byte offset of structure member \p Member of \p Base.
  ///
  /// \param Base Base type or variable name.
  /// \param Member Field name within \p Base.
  /// \param Offset Filled with the field's byte offset on success.
  /// \return False on success.
  virtual bool LookupInlineAsmField(StringRef Base, StringRef Member,
                                    unsigned &Offset) = 0;
};

/// Generic assembler parser interface, for use by target specific
/// assembly parsers.
class LLVM_ABI MCAsmParser {
public:
  /// Function pointer type for extension directive handlers.
  using DirectiveHandler = bool (*)(MCAsmParserExtension*, StringRef, SMLoc);
  /// Pair of an extension instance and its directive handler.
  using ExtensionDirectiveHandler =
      std::pair<MCAsmParserExtension*, DirectiveHandler>;

  /// Deferred parse error waiting to be printed.
  struct MCPendingError {
    SMLoc Loc;             ///< Source location of the error.
    SmallString<64> Msg;   ///< Error message text.
    SMRange Range;         ///< Optional source range to highlight.
  };

private:
  MCTargetAsmParser *TargetParser = nullptr;

protected: // Can only create subclasses.
  /// Construct a parser bound to the given context, streamer, and sources.
  ///
  /// \param Ctx Assembly context used by this parser.
  /// \param Out Streamer that receives parsed assembly.
  /// \param SM Source manager for the input buffers.
  /// \param MAI Target assembly information.
  MCAsmParser(MCContext &Ctx, MCStreamer &Out, SourceMgr &SM,
              const MCAsmInfo &MAI);

  MCContext &Ctx;       ///< Assembly context used by this parser.
  MCStreamer &Out;      ///< Streamer that receives parsed assembly.
  SourceMgr &SrcMgr;    ///< Source manager for the input buffers.
  const MCAsmInfo &MAI; ///< Target assembly information.
  AsmLexer Lexer;       ///< Lexer over the current input buffer.
  SmallVector<MCPendingError, 0> PendingErrors; ///< Deferred errors not yet printed.

  /// Flag tracking whether any errors have been encountered.
  bool HadError = false;

  /// True when parsed operands should be printed for debugging.
  bool ShowParsedOperands = false;

  /// Flag tracking whether we're only interested in symbols, which allows us to
  /// avoid some work (e.g. resolving .incbin directives).
  // TODO: Adopt this in more places.
  bool SymbolScanningMode = false;

public:
  /// Copy construction is deleted; parsers are not copyable.
  ///
  /// \param Other Unused; copy construction is deleted.
  MCAsmParser(const MCAsmParser &Other) = delete;
  /// Copy assignment is deleted; parsers are not copyable.
  ///
  /// \param Other Unused; copy assignment is deleted.
  MCAsmParser &operator=(const MCAsmParser &Other) = delete;
  /// Destroy this assembly parser.
  virtual ~MCAsmParser();

  /// Register \p Handler for assembly directive \p Directive.
  ///
  /// \param Directive Directive name (including the leading '.').
  /// \param Handler Extension instance and callback to invoke.
  virtual void addDirectiveHandler(StringRef Directive,
                                   ExtensionDirectiveHandler Handler) = 0;

  /// Treat \p Directive as an alias of \p Alias.
  ///
  /// \param Directive Directive name to accept.
  /// \param Alias Canonical directive name it maps to.
  virtual void addAliasForDirective(StringRef Directive, StringRef Alias) = 0;

  /// Return the assembly context used by this parser.
  ///
  /// \return The assembly context.
  MCContext &getContext() { return Ctx; }
  /// Return the streamer that receives parsed assembly.
  ///
  /// \return The output streamer.
  MCStreamer &getStreamer() { return Out; }
  /// Return the source manager for the input buffers.
  ///
  /// \return The source manager.
  SourceMgr &getSourceManager() { return SrcMgr; }
  /// Return the lexer over the current input buffer.
  ///
  /// \return The assembly lexer.
  AsmLexer &getLexer() { return Lexer; }
  /// Return the lexer over the current input buffer.
  ///
  /// \return The assembly lexer.
  const AsmLexer &getLexer() const { return Lexer; }

  /// Return the target-specific assembly parser.
  ///
  /// \return The attached target assembly parser.
  MCTargetAsmParser &getTargetParser() const { return *TargetParser; }
  /// Install the target-specific assembly parser \p P.
  ///
  /// \param P Target parser to initialize and attach.
  void setTargetParser(MCTargetAsmParser &P);

  /// Return the active assembler dialect identifier.
  ///
  /// \return The dialect identifier understood by the target parser.
  virtual unsigned getAssemblerDialect() { return 0;}
  /// Set the active assembler dialect to \p i.
  ///
  /// \param i Dialect identifier understood by the target parser.
  virtual void setAssemblerDialect(unsigned i) { }

  /// Return true if parsed operands are printed for debugging.
  ///
  /// \return True when parsed operands are printed.
  bool getShowParsedOperands() const { return ShowParsedOperands; }
  /// Enable or disable printing of parsed operands.
  ///
  /// \param Value True to print parsed operands.
  void setShowParsedOperands(bool Value) { ShowParsedOperands = Value; }

  /// Enable or disable symbol-scanning-only mode.
  ///
  /// \param Value True to skip work not needed for symbol discovery.
  void setSymbolScanningMode(bool Value) { SymbolScanningMode = Value; }

  /// Run the parser on the input source buffer.
  ///
  /// \param NoInitialTextSection If true, do not switch to an initial .text
  /// section before parsing.
  /// \param NoFinalize If true, skip finalization after parsing.
  /// \return True if any error was encountered.
  virtual bool Run(bool NoInitialTextSection, bool NoFinalize = false) = 0;

  /// Enable or disable MS-style inline assembly parsing.
  ///
  /// \param V True when parsing MS inline asm.
  virtual void setParsingMSInlineAsm(bool V) = 0;
  /// Return true when parsing MS-style inline assembly.
  ///
  /// \return True when parsing MS-style inline assembly.
  virtual bool isParsingMSInlineAsm() = 0;

  /// Return true if LTO should discard the symbol named by the argument.
  ///
  /// \param Name Symbol name to check against the LTO discard set.
  /// \return True if the symbol should be discarded for LTO.
  virtual bool discardLTOSymbol(StringRef Name) const { return false; }

  /// Return true when this parser is handling MASM-style assembly.
  ///
  /// \return True when parsing MASM-style assembly.
  virtual bool isParsingMasm() const { return false; }

  /// Define a text macro named \p Name with replacement \p Value.
  ///
  /// \param Name Macro name.
  /// \param Value Replacement text.
  /// \return True on failure.
  virtual bool defineMacro(StringRef Name, StringRef Value) { return true; }

  /// Look up field information for the dotted name \p Name.
  ///
  /// \param Name Field path to resolve.
  /// \param Info Filled with type and offset on success.
  /// \return True on failure.
  virtual bool lookUpField(StringRef Name, AsmFieldInfo &Info) const {
    return true;
  }
  /// Look up field \p Member of base type or variable \p Base.
  ///
  /// \param Base Base type or variable name.
  /// \param Member Field name within \p Base.
  /// \param Info Filled with type and offset on success.
  /// \return True on failure.
  virtual bool lookUpField(StringRef Base, StringRef Member,
                           AsmFieldInfo &Info) const {
    return true;
  }

  /// Look up type information for the type named \p Name.
  ///
  /// \param Name Type name to resolve.
  /// \param Info Filled with size and element information on success.
  /// \return True on failure.
  virtual bool lookUpType(StringRef Name, AsmTypeInfo &Info) const {
    return true;
  }

  /// Parse MS-style inline assembly.
  ///
  /// \param AsmString Filled with the rewritten assembly string.
  /// \param NumOutputs Number of output operands.
  /// \param NumInputs Number of input operands.
  /// \param OpDecls Declarations for each operand, with a flag indicating
  /// whether the operand is an address.
  /// \param Constraints Constraint strings for each operand.
  /// \param Clobbers Clobber list names.
  /// \param MII Instruction info used when rewriting the asm.
  /// \param IP Instruction printer used when rewriting the asm.
  /// \param SI Sema callback for identifier and field lookup.
  /// \return True on failure.
  virtual bool parseMSInlineAsm(
      std::string &AsmString, unsigned &NumOutputs, unsigned &NumInputs,
      SmallVectorImpl<std::pair<void *, bool>> &OpDecls,
      SmallVectorImpl<std::string> &Constraints,
      SmallVectorImpl<std::string> &Clobbers, const MCInstrInfo *MII,
      MCInstPrinter *IP, MCAsmParserSemaCallback &SI) = 0;

  /// Emit a note at the location \p L, with the message \p Msg.
  ///
  /// \param L Source location of the note.
  /// \param Msg Note text.
  /// \param Range Optional source range to highlight.
  virtual void Note(SMLoc L, const Twine &Msg, SMRange Range = {}) = 0;

  /// Emit a warning at the location \p L, with the message \p Msg.
  ///
  /// \param L Source location of the warning.
  /// \param Msg Warning text.
  /// \param Range Optional source range to highlight.
  /// \return True if warnings are fatal.
  virtual bool Warning(SMLoc L, const Twine &Msg, SMRange Range = {}) = 0;

  /// Return an error at the location \p L, with the message \p Msg. This
  /// may be modified before being emitted.
  ///
  /// \param L Source location of the error.
  /// \param Msg Error text.
  /// \param Range Optional source range to highlight.
  /// \return Always true, as an idiomatic convenience to clients.
  bool Error(SMLoc L, const Twine &Msg, SMRange Range = {});

  /// Emit an error at the location \p L, with the message \p Msg.
  ///
  /// \param L Source location of the error.
  /// \param Msg Error text.
  /// \param Range Optional source range to highlight.
  /// \return Always true, as an idiomatic convenience to clients.
  virtual bool printError(SMLoc L, const Twine &Msg, SMRange Range = {}) = 0;

  /// Return true if any deferred errors have not yet been printed.
  ///
  /// \return True if there are pending errors.
  bool hasPendingError() { return !PendingErrors.empty(); }

  /// Print and clear all deferred errors.
  ///
  /// \return True if any errors were pending.
  bool printPendingErrors() {
    bool rv = !PendingErrors.empty();
    for (auto &Err : PendingErrors) {
      printError(Err.Loc, Twine(Err.Msg), Err.Range);
    }
    PendingErrors.clear();
    return rv;
  }

  /// Discard all deferred errors without printing them.
  void clearPendingErrors() { PendingErrors.clear(); }

  /// Append \p Suffix to every pending error message.
  ///
  /// \param Suffix Text appended to each deferred error.
  /// \return True if any pending errors were updated.
  bool addErrorSuffix(const Twine &Suffix);

  /// Get the next AsmToken in the stream, possibly handling file
  /// inclusion first.
  ///
  /// \return The next token after advancing the lexer.
  virtual const AsmToken &Lex() = 0;

  /// Get the current AsmToken from the stream.
  ///
  /// \return The current token without advancing the lexer.
  const AsmToken &getTok() const;

  /// Report an error at the current lexer location.
  ///
  /// \param Msg Error text.
  /// \param Range Optional source range to highlight.
  /// \return Always true.
  bool TokError(const Twine &Msg, SMRange Range = {});

  /// Capture the location of the current token into \p Loc.
  ///
  /// \param Loc Filled with the current token's source location.
  /// \return False on success.
  bool parseTokenLoc(SMLoc &Loc);
  /// Parse and consume a token of kind \p T, or diagnose with \p Msg.
  ///
  /// \param T Expected token kind.
  /// \param Msg Diagnostic if the current token is not \p T.
  /// \return True on failure.
  bool parseToken(AsmToken::TokenKind T, const Twine &Msg = "unexpected token");
  /// Attempt to parse and consume token, returning true on
  /// success.
  ///
  /// \param T Token kind to match and consume.
  /// \return True if the token was present and consumed.
  bool parseOptionalToken(AsmToken::TokenKind T);

  /// Parse and consume a comma token.
  ///
  /// \return True on failure.
  bool parseComma() { return parseToken(AsmToken::Comma, "expected comma"); }
  /// Parse and consume a right-parenthesis token.
  ///
  /// \return True on failure.
  bool parseRParen() { return parseToken(AsmToken::RParen, "expected ')'"); }
  /// Parse and consume an end-of-statement token.
  ///
  /// \return True on failure.
  bool parseEOL();
  /// Parse and consume an end-of-statement token, diagnosing with \p ErrMsg.
  ///
  /// \param ErrMsg Diagnostic if the current token is not end-of-statement.
  /// \return True on failure.
  bool parseEOL(const Twine &ErrMsg);

  /// Repeatedly invoke \p parseOne until end-of-statement.
  ///
  /// \param parseOne Callback that parses one list element; returns true on
  /// failure.
  /// \param hasComma If true, require commas between elements.
  /// \return True on failure.
  bool parseMany(function_ref<bool()> parseOne, bool hasComma = true);

  /// Parse an integer token into \p V.
  ///
  /// \param V Filled with the integer value on success.
  /// \param ErrMsg Diagnostic if the current token is not an integer.
  /// \return True on failure.
  bool parseIntToken(int64_t &V, const Twine &ErrMsg = "expected integer");

  /// Report an error with \p Msg when predicate \p P is true.
  ///
  /// \param P Condition that indicates an error when true.
  /// \param Msg Error text.
  /// \return True if an error was reported.
  bool check(bool P, const Twine &Msg);
  /// Report an error at \p Loc with \p Msg when predicate \p P is true.
  ///
  /// \param P Condition that indicates an error when true.
  /// \param Loc Source location for the diagnostic.
  /// \param Msg Error text.
  /// \return True if an error was reported.
  bool check(bool P, SMLoc Loc, const Twine &Msg);

  /// Parse an identifier or string (as a quoted identifier) and set \p
  /// Res to the identifier contents.
  ///
  /// \param Res Filled with the identifier spelling on success.
  /// \return True on failure.
  virtual bool parseIdentifier(StringRef &Res) = 0;

  /// Parse identifier and get or create symbol for it.
  ///
  /// \param Res Filled with the symbol for the parsed identifier.
  /// \return True on failure.
  bool parseSymbol(MCSymbol *&Res);

  /// Parse and return the remainder of the current statement as a string.
  ///
  /// Returns the contents from the current token until the end of the
  /// statement; the current token on exit will be either the EndOfStatement
  /// or EOF.
  /// \return The statement text from the current token through end-of-statement.
  virtual StringRef parseStringToEndOfStatement() = 0;

  /// Parse the current token as a string which may include escaped
  /// characters and return the string contents.
  ///
  /// \param Data Filled with the unescaped string contents on success.
  /// \return True on failure.
  virtual bool parseEscapedString(std::string &Data) = 0;

  /// Parse an angle-bracket delimited string at the current position if one is
  /// present, returning the string contents.
  ///
  /// \param Data Filled with the string contents on success.
  /// \return True on failure.
  virtual bool parseAngleBracketString(std::string &Data) = 0;

  /// Skip to the end of the current statement, for error recovery.
  virtual void eatToEndOfStatement() = 0;

  /// Parse an arbitrary expression.
  ///
  /// \param Res - The value of the expression. The result is undefined
  /// on error.
  /// \param EndLoc - Location of the last token consumed by the expression.
  /// \return - False on success.
  virtual bool parseExpression(const MCExpr *&Res, SMLoc &EndLoc) = 0;
  /// Parse an arbitrary expression, discarding the end location.
  ///
  /// \param Res The value of the expression. The result is undefined on error.
  /// \return False on success.
  bool parseExpression(const MCExpr *&Res);

  /// Parse a primary expression.
  ///
  /// \param Res - The value of the expression. The result is undefined
  /// on error.
  /// \param EndLoc - Location of the last token consumed by the expression.
  /// \param TypeInfo - Optional out-parameter for MASM type information.
  /// \return - False on success.
  virtual bool parsePrimaryExpr(const MCExpr *&Res, SMLoc &EndLoc,
                                AsmTypeInfo *TypeInfo = nullptr) = 0;

  /// Parse an arbitrary expression, assuming that an initial '(' has
  /// already been consumed.
  ///
  /// \param Res - The value of the expression. The result is undefined
  /// on error.
  /// \param EndLoc - Location of the last token consumed by the expression.
  /// \return - False on success.
  virtual bool parseParenExpression(const MCExpr *&Res, SMLoc &EndLoc) = 0;

  /// Parse an expression which must evaluate to an absolute value.
  ///
  /// \param Res - The value of the absolute expression. The result is undefined
  /// on error.
  /// \return - False on success.
  virtual bool parseAbsoluteExpression(int64_t &Res) = 0;

  /// Ensure that we have a valid section set in the streamer. Otherwise,
  /// report an error and switch to .text.
  /// \return - False on success.
  virtual bool checkForValidSection() = 0;

  /// Parse a .gnu_attribute.
  ///
  /// \param L Location of the directive (used when recovering the attribute
  /// spelling).
  /// \param Tag Filled with the attribute tag.
  /// \param IntegerValue Filled with the attribute's integer value.
  /// \return True if a tag and value were successfully parsed.
  bool parseGNUAttribute(SMLoc L, int64_t &Tag, int64_t &IntegerValue);

  /// Parse an optional '@' specifier and apply it to expression \p Res.
  ///
  /// \param Res Expression to modify; updated when a specifier is present.
  /// \param EndLoc Location of the last token consumed.
  /// \return True on failure.
  bool parseAtSpecifier(const MCExpr *&Res, SMLoc &EndLoc);
  /// Apply relocation specifier \p Variant to expression \p E.
  ///
  /// \param E Expression to rewrite.
  /// \param Variant Specifier / variant kind to apply.
  /// \return The rewritten expression, or null if the specifier does not
  /// apply.
  const MCExpr *applySpecifier(const MCExpr *E, uint32_t Variant);
};

/// Create an MCAsmParser instance for parsing assembly similar to gas syntax.
///
/// \param SM Source manager providing the input buffers.
/// \param Ctx Assembly context.
/// \param Out Streamer that receives parsed assembly.
/// \param MAI Target assembly information.
/// \param CB SourceMgr buffer ID to start from; 0 selects the main file.
/// \return A new gas-style assembly parser owned by the caller.
LLVM_ABI MCAsmParser *createMCAsmParser(SourceMgr &SM, MCContext &Ctx,
                                        MCStreamer &Out, const MCAsmInfo &MAI,
                                        unsigned CB = 0);

/// Create an MCAsmParser instance for parsing Microsoft MASM-style assembly.
///
/// \param SM Source manager providing the input buffers.
/// \param Ctx Assembly context.
/// \param Out Streamer that receives parsed assembly.
/// \param MAI Target assembly information.
/// \param TM Initial local time used by MASM time-related builtins.
/// \param CB SourceMgr buffer ID to start from; 0 selects the main file.
/// \return A new MASM-style assembly parser owned by the caller.
LLVM_ABI MCAsmParser *createMCMasmParser(SourceMgr &SM, MCContext &Ctx,
                                         MCStreamer &Out, const MCAsmInfo &MAI,
                                         struct tm TM, unsigned CB = 0);

} // end namespace llvm

#endif // LLVM_MC_MCPARSER_MCASMPARSER_H
