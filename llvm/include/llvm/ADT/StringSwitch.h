//===--- StringSwitch.h - Switch-on-literal-string Construct --------------===/
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===/
///
/// \file
///  This file implements the StringSwitch template, which mimics a switch()
///  statement whose cases are string literals.
///
//===----------------------------------------------------------------------===/
#ifndef LLVM_ADT_STRINGSWITCH_H
#define LLVM_ADT_STRINGSWITCH_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <cstring>
#include <initializer_list>
#include <optional>

namespace llvm {

/// A switch()-like statement whose cases are string literals.
///
/// The StringSwitch class is a simple form of a switch() statement that
/// determines whether the given string matches one of the given string
/// literals. The template type parameter \p T is the type of the value that
/// will be returned from the string-switch expression. For example,
/// the following code switches on the name of a color in \c argv[i]:
///
/// \code
/// Color color = StringSwitch<Color>(argv[i])
///   .Case("red", Red)
///   .Case("orange", Orange)
///   .Case("yellow", Yellow)
///   .Case("green", Green)
///   .Case("blue", Blue)
///   .Case("indigo", Indigo)
///   .Cases({"violet", "purple"}, Violet)
///   .Default(UnknownColor);
/// \endcode
///
/// When multiple matches are found, the value of the first match is returned.
template<typename T, typename R = T>
class StringSwitch {
  /// The string we are matching.
  const StringRef Str;

  /// The pointer to the result of this switch statement, once known,
  /// null before that.
  std::optional<T> Result;

public:
  /// Construct a StringSwitch that matches against subject string \p S.
  /// @param S Subject string to match against.
  explicit StringSwitch(StringRef S)
  : Str(S), Result() { }

  /// Move-construct a StringSwitch, transferring any matched result.
  /// @param Other Switch to move from.
  StringSwitch(StringSwitch &&Other) = default;

  /// Copy construction is deleted; StringSwitch is move-only.
  /// @param Other Unused; copy construction is deleted.
  StringSwitch(const StringSwitch &Other) = delete;

  /// Copy-assignment is deleted because the subject string is const.
  /// @param Other Unused; copy assignment is deleted.
  void operator=(const StringSwitch &Other) = delete;
  /// Move-assignment is deleted because the subject string is const.
  /// @param Other Unused; move assignment is deleted.
  void operator=(StringSwitch &&Other) = delete;

  /// Match if the subject equals \p S (case-sensitive) and bind \p Value.
  /// @param S Case string to compare against the subject.
  /// @param Value Result to bind when \p S matches.
  /// @return This switch for further chaining.
  StringSwitch &Case(StringLiteral S, T Value) {
    CaseImpl(S, Value);
    return *this;
  }

  /// Match if the subject ends with \p S (case-sensitive) and bind \p Value.
  /// @param S Suffix string to compare against the subject.
  /// @param Value Result to bind when the subject ends with \p S.
  /// @return This switch for further chaining.
  StringSwitch& EndsWith(StringLiteral S, T Value) {
    if (!Result && Str.ends_with(S)) {
      Result = std::move(Value);
    }
    return *this;
  }

  /// Match if the subject starts with \p S (case-sensitive) and bind \p Value.
  /// @param S Prefix string to compare against the subject.
  /// @param Value Result to bind when the subject starts with \p S.
  /// @return This switch for further chaining.
  StringSwitch& StartsWith(StringLiteral S, T Value) {
    if (!Result && Str.starts_with(S)) {
      Result = std::move(Value);
    }
    return *this;
  }

  /// Match if the subject equals any of \p CaseStrings and bind \p Value.
  /// @param CaseStrings Case strings to compare against the subject.
  /// @param Value Result to bind when any case string matches.
  /// @return This switch for further chaining.
  StringSwitch &Cases(std::initializer_list<StringLiteral> CaseStrings,
                      T Value) {
    // Stop matching after the string is found.
    for (StringLiteral S : CaseStrings)
      if (CaseImpl(S, Value))
        break;
    return *this;
  }

  /// Match if the subject equals \p S (case-insensitive) and bind \p Value.
  /// @param S Case string to compare against the subject.
  /// @param Value Result to bind when \p S matches.
  /// @return This switch for further chaining.
  StringSwitch &CaseLower(StringLiteral S, T Value) {
    CaseLowerImpl(S, Value);
    return *this;
  }

  /// Match if the subject ends with \p S (case-insensitive) and bind \p Value.
  /// @param S Suffix string to compare against the subject.
  /// @param Value Result to bind when the subject ends with \p S.
  /// @return This switch for further chaining.
  StringSwitch &EndsWithLower(StringLiteral S, T Value) {
    if (!Result && Str.ends_with_insensitive(S))
      Result = std::move(Value);

    return *this;
  }

  /// Match if the subject starts with \p S (case-insensitive) and bind \p Value.
  /// @param S Prefix string to compare against the subject.
  /// @param Value Result to bind when the subject starts with \p S.
  /// @return This switch for further chaining.
  StringSwitch &StartsWithLower(StringLiteral S, T Value) {
    if (!Result && Str.starts_with_insensitive(S))
      Result = std::move(Value);

    return *this;
  }

  /// Match if the subject equals any of \p CaseStrings case-insensitively.
  /// @param CaseStrings Case strings to compare against the subject.
  /// @param Value Result to bind when any case string matches.
  /// @return This switch for further chaining.
  StringSwitch &CasesLower(std::initializer_list<StringLiteral> CaseStrings,
                           T Value) {
    // Stop matching after the string is found.
    for (StringLiteral S : CaseStrings)
      if (CaseLowerImpl(S, Value))
        break;
    return *this;
  }

  /// Match if predicate \p Pred returns true for the subject and bind \p Value.
  /// @param Pred Predicate invoked with the subject string.
  /// @param Value Result to bind when \p Pred returns true.
  /// @return This switch for further chaining.
  StringSwitch &Predicate(function_ref<bool(StringRef)> Pred, T Value) {
    if (!Result && Pred(Str))
      Result = std::move(Value);
    return *this;
  }

  /// Return the matched value, or \p Value if no case matched.
  /// @param Value Fallback result when no case matched.
  /// @return The matched result, or \p Value if no case matched.
  [[nodiscard]] R Default(T Value) {
    if (Result)
      return std::move(*Result);
    return Value;
  }

  /// Declare default as unreachable, making sure that all cases were handled.
  /// @param Message Diagnostic text if no case matched.
  /// @return The matched result; aborts if no case matched.
  [[nodiscard]] R DefaultUnreachable(
      const char *Message = "Fell off the end of a string-switch") {
    if (Result)
      return std::move(*Result);
    llvm_unreachable(Message);
  }

  /// Convert to \c R by returning the match, or abort if none matched.
  /// @return The matched result; aborts if no case matched.
  [[nodiscard]] operator R() { return DefaultUnreachable(); }

private:
  // Returns true when a match is found. If `Str` matches the `S` argument,
  // stores the result.
  bool CaseImpl(StringLiteral S, T &Value) {
    if (Result)
      return true;

    if (Str != S)
      return false;

    Result = std::move(Value);
    return true;
  }

  // Returns true when a match is found. If `Str` matches the `S` argument
  // (case-insensitive), stores the result.
  bool CaseLowerImpl(StringLiteral S, T &Value) {
    if (Result)
      return true;

    if (!Str.equals_insensitive(S))
      return false;

    Result = std::move(Value);
    return true;
  }
};

} // end namespace llvm

#endif // LLVM_ADT_STRINGSWITCH_H
