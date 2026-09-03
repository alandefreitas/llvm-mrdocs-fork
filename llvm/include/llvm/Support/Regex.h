//===-- Regex.h - Regular Expression matcher implementation -*- C++ -*-----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a POSIX regular expression matcher.  Both Basic and
// Extended POSIX regular expressions (ERE) are supported.  EREs were extended
// to support backreferences in matches.
// This implementation also supports matching strings with embedded NUL chars.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_REGEX_H
#define LLVM_SUPPORT_REGEX_H

#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/Support/Compiler.h"
#include <string>

struct llvm_regex;

namespace llvm {
  class StringRef;
  template<typename T> class SmallVectorImpl;

  /// POSIX regular expression matcher.
  ///
  /// Supports Basic and Extended POSIX regular expressions (ERE), including
  /// backreferences in matches and strings with embedded NUL characters.
  class Regex {
  public:
    /// Compilation and matching option flags.
    enum RegexFlags : unsigned {
      /// No special options; use default Extended POSIX regex matching.
      NoFlags = 0,
      /// Compile for matching that ignores upper/lower case distinctions.
      IgnoreCase = 1,
      /// Compile for newline-sensitive matching. With this flag '[^' bracket
      /// expressions and '.' never match newline. A ^ anchor matches the
      /// null string after any newline in the string in addition to its normal
      /// function, and the $ anchor matches the null string before any
      /// newline in the string in addition to its normal function.
      Newline = 2,
      /// By default, the POSIX extended regular expression (ERE) syntax is
      /// assumed. Pass this flag to turn on basic regular expressions (BRE)
      /// instead.
      BasicRegex = 4,

      LLVM_MARK_AS_BITMASK_ENUM(BasicRegex)
    };

    /// Construct an empty, invalid regex.
    LLVM_ABI Regex();
    /// Compiles the given regular expression \p Regex.
    ///
    /// \param Regex - referenced string is no longer needed after this
    /// constructor does finish.  Only its compiled form is kept stored.
    /// \param Flags - compilation options controlling matching behavior.
    LLVM_ABI Regex(StringRef Regex, RegexFlags Flags = NoFlags);
    /// Compiles the given regular expression \p Regex with unsigned flags.
    ///
    /// \param Regex - pattern string to compile; only the compiled form is kept.
    /// \param Flags - bitfield of RegexFlags values.
    LLVM_ABI Regex(StringRef Regex, unsigned Flags);
    /// Copy construction is deleted; Regex is move-only.
    ///
    /// \param regex Unused; copy construction is deleted.
    Regex(const Regex &regex) = delete;
    /// Move-assign from \p regex, swapping internal state.
    ///
    /// \param regex Regex to take ownership from via swap.
    /// \return A reference to this Regex after the swap.
    Regex &operator=(Regex regex) {
      std::swap(preg, regex.preg);
      std::swap(error, regex.error);
      return *this;
    }
    /// Move-construct from \p regex.
    ///
    /// \param regex Regex to move from.
    LLVM_ABI Regex(Regex &&regex);
    /// Destroy the regex and free compiled state.
    LLVM_ABI ~Regex();

    /// Fill \p Error with any compilation error and return true if valid.
    ///
    /// \param Error - receives the error message from regex compilation, if any.
    /// \return True if the regex compiled successfully.
    LLVM_ABI bool isValid(std::string &Error) const;
    /// Return true if the regex compiled successfully.
    ///
    /// \return True if the regex compiled successfully.
    bool isValid() const { return !error; }

    /// Return the number of parenthesized capture groups in a valid regex.
    ///
    /// The number filled in by match will include this many entries plus one
    /// for the whole regex (as element 0).
    ///
    /// \return The number of parenthesized subgroups in the pattern.
    LLVM_ABI unsigned getNumMatches() const;

    /// Match the regex against a given \p String.
    ///
    /// \param String - the input to match against the compiled regex.
    ///
    /// \param Matches - If given, on a successful match this will be filled in
    /// with references to the matched group expressions (inside \p String),
    /// the first group is always the entire pattern.
    ///
    /// \param Error - If non-null, any errors in the matching will be recorded
    /// as a non-empty string. If there is no error, it will be an empty string.
    ///
    /// \return True on a successful match.
    LLVM_ABI bool match(StringRef String,
                        SmallVectorImpl<StringRef> *Matches = nullptr,
                        std::string *Error = nullptr) const;

    /// Replace the first regex match in \p String with \p Repl.
    ///
    /// Backreferences like "\0" and "\g<1>" in the replacement string are
    /// replaced with the appropriate match substring.
    ///
    /// Note that the replacement string has backslash escaping performed on
    /// it. Invalid backreferences are ignored (replaced by empty strings).
    ///
    /// \param Repl - replacement text, which may contain backreferences.
    /// \param String - input in which to replace the first match.
    /// \param Error If non-null, any errors in the substitution (invalid
    /// backreferences, trailing backslashes) will be recorded as a non-empty
    /// string. If there is no error, it will be an empty string.
    /// \return The string after replacing the first match, or \p String if none.
    LLVM_ABI std::string sub(StringRef Repl, StringRef String,
                             std::string *Error = nullptr) const;

    /// If this function returns true, ^Str$ is an extended regular
    /// expression that matches Str and only Str.
    ///
    /// \param Str - candidate string to test for literal ERE safety.
    /// \return True if \p Str is safe to use as a literal ERE pattern.
    LLVM_ABI static bool isLiteralERE(StringRef Str);

    /// Turn String into a regex by escaping its special characters.
    ///
    /// \param String - literal text to escape for use in a regex pattern.
    /// \return The escaped string, safe for use as a regex pattern.
    LLVM_ABI static std::string escape(StringRef String);

  private:
    struct llvm_regex *preg;
    int error;
  };
}

#endif // LLVM_SUPPORT_REGEX_H
