//===- llvm/MC/MCAsmParserExtension.h - Asm Parser Hooks --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCPARSER_MCASMPARSEREXTENSION_H
#define LLVM_MC_MCPARSER_MCASMPARSEREXTENSION_H

#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/SMLoc.h"

namespace llvm {

class MCLFIRewriter;
class Twine;

/// Generic interface for extending the MCAsmParser,
/// which is implemented by target and object file assembly parser
/// implementations.
class LLVM_ABI MCAsmParserExtension {
  MCAsmParser *Parser = nullptr;

protected:
  /// Construct an uninitialized assembly parser extension.
  MCAsmParserExtension();

  /// Dispatch a parsed directive to a member handler on \p Target.
  ///
  /// Helper template for implementing static dispatch functions.
  ///
  /// \tparam T Concrete extension type that defines \p Handler.
  /// \tparam Handler Member function to invoke for the directive.
  /// \param Target Extension instance that owns the handler.
  /// \param Directive Directive name, including the leading '.'.
  /// \param DirectiveLoc Source location of the directive token.
  /// \return The handler's result; typically true on failure.
  template<typename T, bool (T::*Handler)(StringRef, SMLoc)>
  static bool HandleDirective(MCAsmParserExtension *Target,
                              StringRef Directive,
                              SMLoc DirectiveLoc) {
    T *Obj = static_cast<T*>(Target);
    return (Obj->*Handler)(Directive, DirectiveLoc);
  }

  /// Whether this parser accepts bracketed expressions such as \c [expr].
  bool BracketExpressionsSupported = false;

public:
  /// Copy construction is deleted; extensions are not copyable.
  ///
  /// \param Other Unused; copy construction is deleted.
  MCAsmParserExtension(const MCAsmParserExtension &Other) = delete;
  /// Copy assignment is deleted; extensions are not copyable.
  ///
  /// \param Other Unused; copy assignment is deleted.
  MCAsmParserExtension &operator=(const MCAsmParserExtension &Other) = delete;
  /// Destroy this assembly parser extension.
  virtual ~MCAsmParserExtension();

  /// Initialize the extension for parsing using the given \p Parser.
  /// The extension should use the AsmParser interfaces to register its
  /// parsing routines.
  ///
  /// \param Parser Parser this extension will attach to and register with.
  virtual void Initialize(MCAsmParser &Parser);

  /// \name MCAsmParser Proxy Interfaces
  /// @{

  /// Return the assembly context used by the attached parser.
  ///
  /// \return The assembly context used by the attached parser.
  MCContext &getContext() { return getParser().getContext(); }

  /// Return the lexer over the current input buffer.
  ///
  /// \return The lexer over the current input buffer.
  AsmLexer &getLexer() { return getParser().getLexer(); }
  /// Return the lexer over the current input buffer.
  ///
  /// \return The lexer over the current input buffer.
  const AsmLexer &getLexer() const {
    return const_cast<MCAsmParserExtension *>(this)->getLexer();
  }

  /// Return the assembly parser this extension is attached to.
  ///
  /// \return The assembly parser this extension is attached to.
  MCAsmParser &getParser() { return *Parser; }
  /// Return the assembly parser this extension is attached to.
  ///
  /// \return The assembly parser this extension is attached to.
  const MCAsmParser &getParser() const {
    return const_cast<MCAsmParserExtension*>(this)->getParser();
  }

  /// Return the source manager for the input buffers.
  ///
  /// \return The source manager for the input buffers.
  SourceMgr &getSourceManager() { return getParser().getSourceManager(); }
  /// Return the streamer that receives parsed assembly.
  ///
  /// \return The streamer that receives parsed assembly.
  MCStreamer &getStreamer() { return getParser().getStreamer(); }

  /// Emit a warning at the location \p L, with the message \p Msg.
  ///
  /// \param L Source location of the warning.
  /// \param Msg Warning text.
  /// \return True if warnings are fatal.
  bool Warning(SMLoc L, const Twine &Msg) {
    return getParser().Warning(L, Msg);
  }

  /// Report an error at the location \p L, with the message \p Msg.
  ///
  /// \param L Source location of the error.
  /// \param Msg Error text.
  /// \param Range Optional source range to highlight.
  /// \return Always true, as an idiomatic convenience to clients.
  bool Error(SMLoc L, const Twine &Msg, SMRange Range = SMRange()) {
    return getParser().Error(L, Msg, Range);
  }

  /// Emit a note at the location \p L, with the message \p Msg.
  ///
  /// \param L Source location of the note.
  /// \param Msg Note text.
  void Note(SMLoc L, const Twine &Msg) {
    getParser().Note(L, Msg);
  }

  /// Report an error at the current lexer location.
  ///
  /// \param Msg Error text.
  /// \return Always true.
  bool TokError(const Twine &Msg) {
    return getParser().TokError(Msg);
  }

  /// Consume and return the next assembly token, handling file inclusion.
  ///
  /// \return The next assembly token.
  const AsmToken &Lex() { return getParser().Lex(); }
  /// Return the current assembly token without consuming it.
  ///
  /// \return The current assembly token.
  const AsmToken &getTok() { return getParser().getTok(); }
  /// Parse and consume a token of kind \p T, or diagnose with \p Msg.
  ///
  /// \param T Expected token kind.
  /// \param Msg Diagnostic if the current token is not \p T.
  /// \return True on failure.
  bool parseToken(AsmToken::TokenKind T,
                  const Twine &Msg = "unexpected token") {
    return getParser().parseToken(T, Msg);
  }
  /// Parse and consume an end-of-statement token.
  ///
  /// \return True on failure.
  bool parseEOL() { return getParser().parseEOL(); }

  /// Repeatedly invoke \p parseOne until end-of-statement.
  ///
  /// \param parseOne Callback that parses one list element; returns true on
  /// failure.
  /// \param hasComma If true, require commas between elements.
  /// \return True on failure.
  bool parseMany(function_ref<bool()> parseOne, bool hasComma = true) {
    return getParser().parseMany(parseOne, hasComma);
  }

  /// Attempt to parse and consume a token of kind \p T.
  ///
  /// \param T Token kind to match and consume.
  /// \return True if the token was present and consumed.
  bool parseOptionalToken(AsmToken::TokenKind T) {
    return getParser().parseOptionalToken(T);
  }

  /// Parse a \c .cg_profile identifier, identifier, count directive.
  ///
  /// \param Directive Directive name, including the leading '.'.
  /// \param Loc Source location of the directive token.
  /// \return True on failure.
  bool parseDirectiveCGProfile(StringRef Directive, SMLoc Loc);

  /// Optionally parse a \c unique id suffix into \p UniqueID.
  ///
  /// Accepts \c , unique, <id> after a section directive. Does nothing if the
  /// next token is not a comma.
  ///
  /// \param UniqueID Filled with the unique identifier when present.
  /// \return True on failure; false if the suffix is absent or parsed.
  bool maybeParseUniqueID(int64_t &UniqueID);

  /// Report an error with \p Msg when predicate \p P is true.
  ///
  /// \param P Condition that indicates an error when true.
  /// \param Msg Error text.
  /// \return True if an error was reported.
  bool check(bool P, const Twine &Msg) {
    return getParser().check(P, Msg);
  }

  /// Report an error at \p Loc with \p Msg when predicate \p P is true.
  ///
  /// \param P Condition that indicates an error when true.
  /// \param Loc Source location for the diagnostic.
  /// \param Msg Error text.
  /// \return True if an error was reported.
  bool check(bool P, SMLoc Loc, const Twine &Msg) {
    return getParser().check(P, Loc, Msg);
  }

  /// Append \p Suffix to every pending error message.
  ///
  /// \param Suffix Text appended to each deferred error.
  /// \return True if any pending errors were updated.
  bool addErrorSuffix(const Twine &Suffix) {
    return getParser().addErrorSuffix(Suffix);
  }

  /// Return true if this parser accepts bracketed expressions.
  ///
  /// \return True if this parser accepts bracketed expressions.
  bool HasBracketExpressions() const { return BracketExpressionsSupported; }

  /// @}
};

/// Create a Darwin (Mach-O) object-file assembly parser extension.
///
/// \return A new Darwin assembly parser extension.
LLVM_ABI MCAsmParserExtension *createDarwinAsmParser();
/// Create an ELF object-file assembly parser extension.
///
/// \return A new ELF assembly parser extension.
LLVM_ABI MCAsmParserExtension *createELFAsmParser();
/// Create a COFF object-file assembly parser extension.
///
/// \return A new COFF assembly parser extension.
LLVM_ABI MCAsmParserExtension *createCOFFAsmParser();
/// Create a COFF MASM object-file assembly parser extension.
///
/// \return A new COFF MASM assembly parser extension.
LLVM_ABI MCAsmParserExtension *createCOFFMasmParser();
/// Create a GOFF object-file assembly parser extension.
///
/// \return A new GOFF assembly parser extension.
LLVM_ABI MCAsmParserExtension *createGOFFAsmParser();
/// Create an XCOFF object-file assembly parser extension.
///
/// \return A new XCOFF assembly parser extension.
LLVM_ABI MCAsmParserExtension *createXCOFFAsmParser();
/// Create a WebAssembly object-file assembly parser extension.
///
/// \return A new WebAssembly assembly parser extension.
LLVM_ABI MCAsmParserExtension *createWasmAsmParser();
/// Create an LFI assembly parser extension bound to rewriter \p Exp.
///
/// \param Exp LFI rewriter the parser uses to enable or disable rewriting.
/// \return A new LFI assembly parser extension.
LLVM_ABI MCAsmParserExtension *createLFIAsmParser(MCLFIRewriter *Exp);

} // end namespace llvm

#endif // LLVM_MC_MCPARSER_MCASMPARSEREXTENSION_H
