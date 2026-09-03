//===- AsmLexer.h - Lexer for Assembly Files --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class declares the lexer for assembly files.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCPARSER_ASMLEXER_H
#define LLVM_MC_MCPARSER_ASMLEXER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCAsmMacro.h"
#include "llvm/Support/Compiler.h"
#include <cassert>
#include <cstddef>
#include <string>

namespace llvm {

class MCAsmInfo;

/// A callback class which is notified of each comment in an assembly file as
/// it is lexed.
class AsmCommentConsumer {
public:
  /// Destroy this comment consumer.
  virtual ~AsmCommentConsumer() = default;

  /// Callback invoked when a comment is lexed.
  ///
  /// Loc is the start of the comment text (excluding the comment-start marker).
  /// CommentText is the text of the comment, excluding the comment start and
  /// end markers, and the newline for single-line comments.
  ///
  /// \param Loc Start of the comment text, excluding the comment-start marker.
  /// \param CommentText Comment body, excluding start/end markers and newline.
  virtual void HandleComment(SMLoc Loc, StringRef CommentText) = 0;
};

/// Lexer that tokenizes assembly source for the MC assembler parser.
class AsmLexer {
  /// The current token, stored in the base class for faster access.
  SmallVector<AsmToken, 1> CurTok;

  const char *CurPtr = nullptr;
  /// NULL-terminated buffer. NULL terminator must reside at `CurBuf.end()`.
  StringRef CurBuf;

  /// The location and description of the current error
  SMLoc ErrLoc;
  std::string Err;

  const MCAsmInfo &MAI;

  bool IsAtStartOfLine = true;
  bool JustConsumedEOL = true;
  bool IsPeeking = false;
  bool EndStatementAtEOF = true;

  const char *TokStart = nullptr;
  bool SkipSpace = true;
  bool AllowAtInIdentifier = false;
  bool AllowHashInIdentifier = false;
  bool IsAtStartOfStatement = true;
  bool LexMasmHexFloats = false;
  bool LexMasmIntegers = false;
  bool LexMasmStrings = false;
  bool LexMotorolaIntegers = false;
  bool UseMasmDefaultRadix = false;
  unsigned DefaultRadix = 10;
  bool LexHLASMIntegers = false;
  bool LexHLASMStrings = false;
  AsmCommentConsumer *CommentConsumer = nullptr;

  LLVM_ABI AsmToken LexToken();

  void SetError(SMLoc errLoc, const std::string &err) {
    ErrLoc = errLoc;
    Err = err;
  }

public:
  /// Construct a lexer using target assembly information \p MAI.
  ///
  /// \param MAI Target-specific comment, separator, and integer-literal rules.
  LLVM_ABI AsmLexer(const MCAsmInfo &MAI);
  /// Copy construction is deleted; AsmLexer is not copyable.
  ///
  /// \param Other Unused; copy construction is deleted.
  AsmLexer(const AsmLexer &Other) = delete;
  /// Copy assignment is deleted; AsmLexer is not copyable.
  ///
  /// \param Other Unused; copy assignment is deleted.
  AsmLexer &operator=(const AsmLexer &Other) = delete;

  /// Consume the next token from the input stream and return it.
  ///
  /// The lexer will continuously return the end-of-file token once the end of
  /// the main input file has been reached.
  ///
  /// \return The next token from the input stream.
  const AsmToken &Lex() {
    assert(!CurTok.empty());
    // Mark if we parsing out a EndOfStatement.
    JustConsumedEOL = CurTok.front().getKind() == AsmToken::EndOfStatement;
    CurTok.erase(CurTok.begin());
    // LexToken may generate multiple tokens via UnLex but will always return
    // the first one. Place returned value at head of CurTok vector.
    if (CurTok.empty()) {
      AsmToken T = LexToken();
      CurTok.insert(CurTok.begin(), T);
    }
    return CurTok.front();
  }

  /// Push \p Token onto the front of the token queue.
  ///
  /// \param Token Token to restore or inject as the current token.
  void UnLex(AsmToken const &Token) {
    CurTok.insert(CurTok.begin(), Token);
  }

  /// Return true if the last consumed token was an end-of-statement.
  ///
  /// \return True if the previously consumed token was EndOfStatement.
  bool justConsumedEOL() { return JustConsumedEOL; }

  /// Lex raw text until a comment, statement separator, or newline.
  ///
  /// \return The consumed text, not including the terminator.
  LLVM_ABI StringRef LexUntilEndOfStatement();

  /// Get the current source location.
  ///
  /// \return The source location of the start of the current token.
  SMLoc getLoc() const { return SMLoc::getFromPointer(TokStart); }

  /// Get the current (last) lexed token.
  ///
  /// \return The current token at the front of the token queue.
  const AsmToken &getTok() const { return CurTok[0]; }

  /// Look ahead at the next token to be lexed.
  ///
  /// \param ShouldSkipSpace If true, skip whitespace while peeking.
  /// \return The next token that would be lexed, without consuming it.
  const AsmToken peekTok(bool ShouldSkipSpace = true) {
    AsmToken Tok;

    MutableArrayRef<AsmToken> Buf(Tok);
    size_t ReadCount = peekTokens(Buf, ShouldSkipSpace);

    assert(ReadCount == 1);
    (void)ReadCount;

    return Tok;
  }

  /// Look ahead an arbitrary number of tokens.
  ///
  /// \param Buf Output buffer filled with the peeked tokens.
  /// \param ShouldSkipSpace If true, skip whitespace while peeking.
  /// \return Number of tokens written into \p Buf.
  LLVM_ABI size_t peekTokens(MutableArrayRef<AsmToken> Buf,
                             bool ShouldSkipSpace = true);

  /// Get the current error location.
  ///
  /// \return The source location of the current error.
  SMLoc getErrLoc() { return ErrLoc; }

  /// Get the current error string.
  ///
  /// \return The description of the current error.
  const std::string &getErr() { return Err; }

  /// Get the kind of current token.
  ///
  /// \return The token kind of the current (last) lexed token.
  AsmToken::TokenKind getKind() const { return getTok().getKind(); }

  /// Check if the current token has kind \p K.
  ///
  /// \param K Token kind to compare against.
  /// \return True if the current token's kind is \p K.
  bool is(AsmToken::TokenKind K) const { return getTok().is(K); }

  /// Check if the current token does not have kind \p K.
  ///
  /// \param K Token kind to compare against.
  /// \return True if the current token's kind is not \p K.
  bool isNot(AsmToken::TokenKind K) const { return getTok().isNot(K); }

  /// Set whether spaces should be ignored by the lexer.
  ///
  /// \param val If true, the lexer skips whitespace tokens.
  void setSkipSpace(bool val) { SkipSpace = val; }

  /// Return whether '@' is allowed in identifiers.
  ///
  /// \return True if '@' may appear in identifier tokens.
  bool getAllowAtInIdentifier() { return AllowAtInIdentifier; }
  /// Set whether '@' is allowed in identifiers.
  ///
  /// \param v If true, '@' may appear in identifier tokens.
  void setAllowAtInIdentifier(bool v) { AllowAtInIdentifier = v; }

  /// Set whether '#' is allowed in identifiers.
  ///
  /// \param V If true, '#' may appear in identifier tokens.
  void setAllowHashInIdentifier(bool V) { AllowHashInIdentifier = V; }

  /// Set the callback invoked when a comment is lexed.
  ///
  /// \param CommentConsumer Callback, or nullptr to disable comment handling.
  void setCommentConsumer(AsmCommentConsumer *CommentConsumer) {
    this->CommentConsumer = CommentConsumer;
  }

  /// Set whether to lex masm-style binary (e.g., 0b1101) and radix-specified
  /// literals (e.g., 0ABCh [hex], 576t [decimal], 77o [octal], 1101y [binary]).
  ///
  /// \param V If true, recognize MASM-style integer literals.
  void setLexMasmIntegers(bool V) { LexMasmIntegers = V; }

  /// Set whether to use masm-style default-radix integer literals. If disabled,
  /// assume decimal unless prefixed (e.g., 0x2c [hex], 077 [octal]).
  ///
  /// \param V If true, use the MASM default radix for unprefixed integers.
  void useMasmDefaultRadix(bool V) { UseMasmDefaultRadix = V; }

  /// Return the default radix used for MASM integer literals.
  ///
  /// \return The radix assumed for unprefixed MASM integers.
  unsigned getMasmDefaultRadix() const { return DefaultRadix; }
  /// Set the default radix used for MASM integer literals.
  ///
  /// \param Radix Radix to assume for unprefixed MASM integers.
  void setMasmDefaultRadix(unsigned Radix) { DefaultRadix = Radix; }

  /// Set whether to lex masm-style hex float literals, such as 3f800000r.
  ///
  /// \param V If true, recognize MASM-style hex float literals.
  void setLexMasmHexFloats(bool V) { LexMasmHexFloats = V; }

  /// Set whether to lex masm-style string literals, such as 'Can''t find file'
  /// and "This ""value"" not found".
  ///
  /// \param V If true, recognize MASM-style string literals.
  void setLexMasmStrings(bool V) { LexMasmStrings = V; }

  /// Set whether to lex Motorola-style integer literals, such as $deadbeef or
  /// %01010110.
  ///
  /// \param V If true, recognize Motorola-style integer literals.
  void setLexMotorolaIntegers(bool V) { LexMotorolaIntegers = V; }

  /// Set whether to lex HLASM-flavour integers. For now this is only [0-9]*
  ///
  /// \param V If true, lex HLASM-style decimal integers.
  void setLexHLASMIntegers(bool V) { LexHLASMIntegers = V; }

  /// Set whether to "lex" HLASM-flavour character and string literals. For now,
  /// setting this option to true, will disable lexing for character and string
  /// literals.
  ///
  /// \param V If true, do not lex character and string literals.
  void setLexHLASMStrings(bool V) { LexHLASMStrings = V; }

  /// Set the source buffer that this lexer will tokenize.
  ///
  /// `Buf` must be NULL-terminated. NULL terminator must reside at `Buf.end()`.
  /// `ptr` if provided must be in range [`Buf.begin()`, `buf.end()`] or NULL.
  /// Specifies where lexing of buffer should begin.
  /// `EndStatementAtEOF` specifies whether `AsmToken::EndOfStatement` should be
  /// returned upon reaching end of buffer.
  ///
  /// \param Buf NULL-terminated buffer; terminator must be at \p Buf.end().
  /// \param ptr Start of lexing, or nullptr to start at \p Buf.begin().
  /// \param EndStatementAtEOF If true, emit EndOfStatement at end of buffer.
  LLVM_ABI void setBuffer(StringRef Buf, const char *ptr = nullptr,
                          bool EndStatementAtEOF = true);

  /// Return the MCAsmInfo used by this lexer.
  ///
  /// \return The target assembly info associated with this lexer.
  const MCAsmInfo &getMAI() const { return MAI; }

private:
  bool isAtStartOfComment(const char *Ptr);
  bool isAtStatementSeparator(const char *Ptr);
  [[nodiscard]] int getNextChar();
  int peekNextChar();
  AsmToken ReturnError(const char *Loc, const std::string &Msg);

  AsmToken LexIdentifier();
  AsmToken LexSlash();
  AsmToken LexLineComment();
  AsmToken LexDigit();
  AsmToken LexSingleQuote();
  AsmToken LexQuote();
  AsmToken LexFloatLiteral();
  AsmToken LexHexFloatLiteral(bool NoIntDigits);

  StringRef LexUntilEndOfLine();
};

} // end namespace llvm

#endif // LLVM_MC_MCPARSER_ASMLEXER_H
