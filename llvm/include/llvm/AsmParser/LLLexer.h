//===- LLLexer.h - Lexer for LLVM Assembly Files ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class represents the Lexer for .ll files.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ASMPARSER_LLLEXER_H
#define LLVM_ASMPARSER_LLLEXER_H

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/AsmParser/LLToken.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/Support/SourceMgr.h"
#include <string>

namespace llvm {
  class Type;
  class SMDiagnostic;
  class LLVMContext;

  /// Lexer for LLVM IR textual assembly (`.ll` files).
  class LLLexer {
    const char *CurPtr;
    StringRef CurBuf;

    /// The end (exclusive) of the previous token.
    const char *PrevTokEnd = nullptr;

    enum class ErrorPriority {
      None,   // No error message present.
      Parser, // Errors issued by parser.
      Lexer,  // Errors issued by lexer.
    };

    struct ErrorInfo {
      ErrorPriority Priority = ErrorPriority::None;
      SMDiagnostic &Error;

      explicit ErrorInfo(SMDiagnostic &Error) : Error(Error) {}
    } ErrorInfo;

    SourceMgr &SM;
    LLVMContext &Context;

    // Information about the current token.
    const char *TokStart;
    lltok::Kind CurKind;
    std::string StrVal;
    unsigned UIntVal = 0;
    Type *TyVal = nullptr;
    APFloat APFloatVal{0.0};
    APSInt APSIntVal{0};

    // When false (default), an identifier ending in ':' is a label token.
    // When true, the ':' is treated as a separate token.
    bool IgnoreColonInIdentifiers = false;

  public:
    /// Construct a lexer over assembly buffer \p StartBuf.
    ///
    /// \param StartBuf Assembly source text to tokenize.
    /// \param SM Source manager owning \p StartBuf's buffer.
    /// \param Err Diagnostic sink for lexer and parser errors.
    /// \param C Context used when materializing types from tokens.
    LLVM_ABI explicit LLLexer(StringRef StartBuf, SourceMgr &SM,
                              SMDiagnostic &Err, LLVMContext &C);

    /// Advance to the next token and return its kind.
    ///
    /// \return The kind of the newly lexed token.
    lltok::Kind Lex() { return CurKind = LexToken(); }

    /// Source location type used for lexer and parser diagnostics.
    typedef SMLoc LocTy;
    /// Return the source location of the start of the current token.
    ///
    /// \return Source location of the start of the current token.
    LocTy getLoc() const { return SMLoc::getFromPointer(TokStart); }
    /// Return the kind of the current token.
    ///
    /// \return Kind of the current token.
    lltok::Kind getKind() const { return CurKind; }
    /// Return the string value associated with the current token.
    ///
    /// \return String value of the current token.
    const std::string &getStrVal() const { return StrVal; }
    /// Return the type value associated with the current token.
    ///
    /// \return Type associated with the current token.
    Type *getTyVal() const { return TyVal; }
    /// Return the unsigned integer value associated with the current token.
    ///
    /// \return Unsigned integer value of the current token.
    unsigned getUIntVal() const { return UIntVal; }
    /// Return the arbitrary-precision integer value of the current token.
    ///
    /// \return Arbitrary-precision integer value of the current token.
    const APSInt &getAPSIntVal() const { return APSIntVal; }
    /// Return the floating-point value associated with the current token.
    ///
    /// \return Floating-point value of the current token.
    const APFloat &getAPFloatVal() const { return APFloatVal; }

    /// Control whether a trailing ':' is part of an identifier or a label.
    ///
    /// When \p val is false (the default), an identifier ending in ':' is a
    /// label token. When true, the ':' is treated as a separate token.
    ///
    /// \param val Whether to ignore colon characters in identifiers.
    void setIgnoreColonInIdentifiers(bool val) {
      IgnoreColonInIdentifiers = val;
    }

    /// Get the line, column position of the start of the current token,
    /// zero-indexed.
    ///
    /// \return Zero-indexed line and column of the start of the current token.
    std::pair<unsigned, unsigned> getTokLineColumnPos() {
      auto LC = SM.getLineAndColumn(SMLoc::getFromPointer(TokStart));
      return {LC.first - 1, LC.second - 1};
    }
    /// Get the line, column position of the end of the previous token,
    /// zero-indexed exclusive.
    ///
    /// \return Zero-indexed exclusive line and column of the previous token end.
    std::pair<unsigned, unsigned> getPrevTokEndLineColumnPos() {
      auto LC = SM.getLineAndColumn(SMLoc::getFromPointer(PrevTokEnd));
      return {LC.first - 1, LC.second - 1};
    }

    /// Record a parser error at \p ErrorLoc and return true.
    ///
    /// Always returns true as a convenience for parser functions that return
    /// true on error.
    ///
    /// \param ErrorLoc Source location of the error.
    /// \param Msg Diagnostic message to report.
    /// \return Always true.
    bool ParseError(LocTy ErrorLoc, const Twine &Msg) {
      Error(ErrorLoc, Msg, ErrorPriority::Parser);
      return true;
    }
    /// Record a parser error at the current token and return true.
    ///
    /// Always returns true as a convenience for parser functions that return
    /// true on error.
    ///
    /// \param Msg Diagnostic message to report.
    /// \return Always true.
    bool ParseError(const Twine &Msg) { return ParseError(getLoc(), Msg); }

    /// Emit a warning diagnostic at \p WarningLoc.
    ///
    /// \param WarningLoc Source location of the warning.
    /// \param Msg Diagnostic message to report.
    LLVM_ABI void Warning(LocTy WarningLoc, const Twine &Msg) const;
    /// Emit a warning diagnostic at the current token location.
    ///
    /// \param Msg Diagnostic message to report.
    void Warning(const Twine &Msg) const { return Warning(getLoc(), Msg); }

  private:
    LLVM_ABI lltok::Kind LexToken();

    int getNextChar();
    void SkipLineComment();
    bool SkipCComment();
    lltok::Kind ReadString(lltok::Kind kind);
    bool ReadVarName();

    lltok::Kind LexIdentifier();
    lltok::Kind LexDigitOrNegative();
    lltok::Kind LexPositive();
    lltok::Kind LexAt();
    lltok::Kind LexDollar();
    lltok::Kind LexExclaim();
    lltok::Kind LexPercent();
    lltok::Kind LexUIntID(lltok::Kind Token);
    lltok::Kind LexVar(lltok::Kind Var, lltok::Kind VarID);
    lltok::Kind LexQuote();
    lltok::Kind Lex0x();
    lltok::Kind LexHash();
    lltok::Kind LexCaret();
    lltok::Kind LexFloatStr();

    uint64_t atoull(const char *Buffer, const char *End);
    uint64_t HexIntToVal(const char *Buffer, const char *End);
    void HexToIntPair(const char *Buffer, const char *End, uint64_t Pair[2]);
    void FP80HexToIntPair(const char *Buffer, const char *End,
                          uint64_t Pair[2]);

    LLVM_ABI void Error(LocTy ErrorLoc, const Twine &Msg, ErrorPriority Origin);

    void LexError(LocTy ErrorLoc, const Twine &Msg) {
      Error(ErrorLoc, Msg, ErrorPriority::Lexer);
    }
    void LexError(const Twine &Msg) { LexError(getLoc(), Msg); }
  };
} // end namespace llvm

#endif
