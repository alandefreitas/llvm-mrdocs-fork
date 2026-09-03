//===-- ResourceScriptToken.h -----------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
//
// This declares the .rc script tokens.
// The list of available tokens is located at ResourceScriptTokenList.h.
//
// Ref: msdn.microsoft.com/en-us/library/windows/desktop/aa380599(v=vs.85).aspx
//
//===---------------------------------------------------------------------===//

#ifndef LLVM_INCLUDE_LLVM_SUPPORT_WINDOWS_RESOURCE_SCRIPTTOKEN_H
#define LLVM_INCLUDE_LLVM_SUPPORT_WINDOWS_RESOURCE_SCRIPTTOKEN_H

#include "llvm/ADT/StringRef.h"

namespace llvm {

/// A single token from a Windows resource (.rc) script.
///
/// Each token has a \c Kind and holds a value - a reference to the token's
/// spelling in the input buffer. RCToken does not own that value; the memory
/// buffer containing it must remain valid and must not be freed or reallocated
/// while the token is in use.
class RCToken {
public:
  /// Classification of a resource-script token.
  enum class Kind {
    Invalid,    ///< Invalid token; should not occur in a valid script.
    Int,        ///< Integer (decimal, octal, or hexadecimal).
    String,     ///< String literal value.
    Identifier, ///< Script identifier (resource name or type).
    BlockBegin, ///< Start of a script block; can also be BEGIN.
    BlockEnd,   ///< End of a script block; can also be END.
    Comma,      ///< Resource-argument separator.
    Plus,       ///< Addition operator.
    Minus,      ///< Subtraction operator.
    Pipe,       ///< Bitwise-OR operator.
    Amp,        ///< Bitwise-AND operator.
    Tilde,      ///< Bitwise-NOT operator.
    LeftParen,  ///< Left parenthesis in script expressions.
    RightParen, ///< Right parenthesis in script expressions.
  };

  /// Construct a token with the given kind and spelling.
  /// @param RCTokenKind Kind of this token.
  /// @param Value Spelling of the token in the input buffer (not owned).
  RCToken(RCToken::Kind RCTokenKind, StringRef Value);

  /// Return the numeric value of an integer token.
  ///
  /// The token kind must be \c Kind::Int.
  /// @return The integer value represented by this token.
  uint32_t intValue() const;
  /// Return true if this integer token has a trailing L long-integer suffix.
  /// @return True if the token has a long-integer suffix.
  bool isLongInt() const;

  /// Return the token's spelling in the input buffer.
  /// @return The token's spelling (not owned).
  StringRef value() const;
  /// Return the kind of this token.
  /// @return The classification of this token.
  Kind kind() const;

  /// Return true if this token is a binary operator.
  /// @return True if this token is a binary operator.
  bool isBinaryOp() const;

private:
  Kind TokenKind;
  StringRef TokenValue;
};

} // namespace llvm

#endif
