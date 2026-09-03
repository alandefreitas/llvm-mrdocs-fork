//===- MCAsmMacro.h - Assembly Macros ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCASMMACRO_H
#define LLVM_MC_MCASMMACRO_H

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/SMLoc.h"
#include <vector>

namespace llvm {

/// Target independent representation for an assembler token.
class AsmToken {
public:
  /// Kind of token recognized by the assembler lexer.
  enum TokenKind {
    // Markers
    Eof,   ///< End of input.
    Error, ///< Lexical error.

    // String values.
    Identifier, ///< Identifier token.
    String,     ///< Quoted string literal.

    // Integer values.
    Integer, ///< Integer constant that fits in 64 bits.
    BigNum,  ///< Integer constant larger than 64 bits.

    // Real values.
    Real, ///< Floating-point constant.

    // Comments
    Comment,       ///< Comment text.
    HashDirective, ///< `#` directive (e.g. line marker).

    // No-value.
    EndOfStatement, ///< End of a statement (typically newline).
    Colon,          ///< Colon `:` token.
    Space,          ///< Whitespace token.
    Plus,           ///< Plus `+` operator.
    Minus,          ///< Minus `-` operator.
    Tilde,          ///< Bitwise NOT `~` operator.
    Slash,          ///< Division `/` operator.
    BackSlash,      ///< Backslash `\` token.
    LParen,         ///< Left parenthesis `(`.
    RParen,         ///< Right parenthesis `)`.
    LBrac,          ///< Left bracket `[`.
    RBrac,          ///< Right bracket `]`.
    LCurly,         ///< Left brace `{`.
    RCurly,         ///< Right brace `}`.
    Question,       ///< Question mark `?`.
    Star,           ///< Multiplication `*` / star.
    Dot,            ///< Dot `.` token.
    Comma,          ///< Comma `,` token.
    Dollar,         ///< Dollar `$` token.
    Equal,          ///< Assignment / equality `=` token.
    EqualEqual,     ///< Equality `==` operator.
    Pipe,           ///< Bitwise OR `|` operator.
    PipePipe,       ///< Logical OR `||` operator.
    Caret,          ///< Bitwise XOR `^` operator.
    Amp,            ///< Bitwise AND `&` operator.
    AmpAmp,         ///< Logical AND `&&` operator.
    Exclaim,        ///< Logical NOT `!` operator.
    ExclaimEqual,   ///< Inequality `!=` operator.
    Percent,        ///< Percent `%` / modulo operator.
    Hash,           ///< Hash `#` token.
    Less,           ///< Less-than `<` operator.
    LessEqual,      ///< Less-or-equal `<=` operator.
    LessLess,       ///< Left shift `<<` operator.
    LessGreater,    ///< Not-equal `<>` operator.
    Greater,        ///< Greater-than `>` operator.
    GreaterEqual,   ///< Greater-or-equal `>=` operator.
    GreaterGreater, ///< Right shift `>>` operator.
    At,             ///< At `@` token.
    MinusGreater,   ///< Arrow `->` token.
  };

private:
  TokenKind Kind = TokenKind::Eof;

  /// A reference to the entire token contents; this is always a pointer into
  /// a memory buffer owned by the source manager.
  StringRef Str;

  APInt IntVal;

public:
  /// Construct a default end-of-file token.
  AsmToken() = default;
  /// Construct a token with kind \p Kind, spelling \p Str, and integer value
  /// \p IntVal.
  ///
  /// @param Kind Token kind.
  /// @param Str Spelling of the token in the source buffer.
  /// @param IntVal Arbitrary-precision integer value for integer tokens.
  AsmToken(TokenKind Kind, StringRef Str, APInt IntVal)
      : Kind(Kind), Str(Str), IntVal(std::move(IntVal)) {}
  /// Construct a token with kind \p Kind, spelling \p Str, and 64-bit integer
  /// value \p IntVal.
  ///
  /// @param Kind Token kind.
  /// @param Str Spelling of the token in the source buffer.
  /// @param IntVal Signed 64-bit integer value for integer tokens.
  AsmToken(TokenKind Kind, StringRef Str, int64_t IntVal = 0)
      : Kind(Kind), Str(Str), IntVal(64, IntVal, true) {}

  /// Return the kind of this token.
  ///
  /// @return The kind of this token.
  TokenKind getKind() const { return Kind; }
  /// Return true if this token has kind \p K.
  ///
  /// @param K Token kind to compare against.
  /// @return True if this token has kind \p K.
  bool is(TokenKind K) const { return Kind == K; }
  /// Return true if this token does not have kind \p K.
  ///
  /// @param K Token kind to compare against.
  /// @return True if this token does not have kind \p K.
  bool isNot(TokenKind K) const { return Kind != K; }

  /// Return the source location of the start of this token.
  ///
  /// @return The source location of the start of this token.
  LLVM_ABI SMLoc getLoc() const;
  /// Return the source location of the end of this token.
  ///
  /// @return The source location of the end of this token.
  LLVM_ABI SMLoc getEndLoc() const;
  /// Return the source range covering this token.
  ///
  /// @return The source range covering this token.
  LLVM_ABI SMRange getLocRange() const;

  /// Get the contents of a string token (without quotes).
  ///
  /// @return The string contents without surrounding quotes.
  StringRef getStringContents() const {
    assert(Kind == String && "This token isn't a string!");
    return Str.slice(1, Str.size() - 1);
  }

  /// Get the identifier string for an identifier or string token.
  ///
  /// This gets the portion of the string which should be used as the
  /// identifier, e.g., it does not include the quotes on strings.
  ///
  /// @return The identifier portion of the token.
  StringRef getIdentifier() const {
    if (Kind == Identifier)
      return getString();
    return getStringContents();
  }

  /// Get the string for the current token, this includes all characters (for
  /// example, the quotes on strings) in the token.
  ///
  /// The returned StringRef points into the source manager's memory buffer, and
  /// is safe to store across calls to Lex().
  ///
  /// @return The full token spelling in the source buffer.
  StringRef getString() const { return Str; }

  // FIXME: Don't compute this in advance, it makes every token larger, and is
  // also not generally what we want (it is nicer for recovery etc. to lex 123br
  // as a single token, then diagnose as an invalid number).
  /// Return the zero-extended 64-bit integer value of an integer token.
  ///
  /// @return The zero-extended 64-bit integer value.
  int64_t getIntVal() const {
    assert(Kind == Integer && "This token isn't an integer!");
    return IntVal.getZExtValue();
  }

  /// Return the arbitrary-precision integer value of an integer or big-num
  /// token.
  ///
  /// @return The arbitrary-precision integer value.
  APInt getAPIntVal() const {
    assert((Kind == Integer || Kind == BigNum) &&
           "This token isn't an integer!");
    return IntVal;
  }

  /// Dump a debug representation of this token to \p OS.
  ///
  /// @param OS Stream to write to.
  LLVM_ABI void dump(raw_ostream &OS) const;
};

/// A single formal parameter of an assembler macro.
struct MCAsmMacroParameter {
  /// Name of the macro parameter.
  StringRef Name;
  /// Default value of the parameter as a token sequence.
  std::vector<AsmToken> Value;
  /// True if the parameter must be supplied by the caller.
  bool Required = false;
  /// True if the parameter accepts a variable number of arguments.
  bool Vararg = false;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Dump a debug representation of this parameter to stderr.
  void dump() const { dump(dbgs()); }
  /// Dump a debug representation of this parameter to \p OS.
  ///
  /// @param OS Stream to write to.
  LLVM_DUMP_METHOD void dump(raw_ostream &OS) const;
#endif
};

/// List of formal parameters for an assembler macro.
typedef std::vector<MCAsmMacroParameter> MCAsmMacroParameters;
/// Definition of an assembler macro or macro-like function.
struct MCAsmMacro {
  /// Name of the macro.
  StringRef Name;
  /// Body text of the macro.
  StringRef Body;
  /// Formal parameters of the macro.
  MCAsmMacroParameters Parameters;
  /// Names of local symbols declared inside the macro.
  std::vector<std::string> Locals;
  /// True if this macro is defined as a function-like macro.
  bool IsFunction = false;
  /// Number of times this macro has been instantiated.
  unsigned Count = 0;

public:
  /// Construct a macro named \p N with body \p B and parameters \p P.
  ///
  /// @param N Macro name.
  /// @param B Macro body text.
  /// @param P Formal parameter list.
  MCAsmMacro(StringRef N, StringRef B, MCAsmMacroParameters P)
      : Name(N), Body(B), Parameters(std::move(P)) {}
  /// Construct a macro named \p N with body \p B, parameters \p P, locals
  /// \p L, and function flag \p F.
  ///
  /// @param N Macro name.
  /// @param B Macro body text.
  /// @param P Formal parameter list.
  /// @param L Local symbol names.
  /// @param F True if this is a function-like macro.
  MCAsmMacro(StringRef N, StringRef B, MCAsmMacroParameters P,
             std::vector<std::string> L, bool F)
      : Name(N), Body(B), Parameters(std::move(P)), Locals(std::move(L)),
        IsFunction(F) {}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Dump a debug representation of this macro to stderr.
  void dump() const { dump(dbgs()); }
  /// Dump a debug representation of this macro to \p OS.
  ///
  /// @param OS Stream to write to.
  LLVM_DUMP_METHOD void dump(raw_ostream &OS) const;
#endif
};
} // namespace llvm

#endif
