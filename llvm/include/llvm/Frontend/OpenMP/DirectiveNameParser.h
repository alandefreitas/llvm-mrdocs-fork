//===- DirectiveNameParser.h  ------------------------------------- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FRONTEND_OPENMP_DIRECTIVENAMEPARSER_H
#define LLVM_FRONTEND_OPENMP_DIRECTIVENAMEPARSER_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Frontend/OpenMP/OMP.h"
#include "llvm/Support/Compiler.h"

#include <memory>

namespace llvm {
/// OpenMP front-end utilities for representing directives and descriptor metadata.
namespace omp {
/// Parser for OpenMP directive names.
///
/// It only recognizes names listed in OMP.td, in particular it does not
/// recognize Fortran's end-directives if they are not explicitly listed in
/// OMP.td.
///
/// The class itself may be a singleton, once it's constructed it never
/// changes.
///
/// Usage:
/// {
///   DirectiveNameParser Parser;   // Could be static const.
///
///   DirectiveNameParser::State *S = Parser.initial();
///   for (StringRef Token : Tokens)
///     S = Parser.consume(S, Token); // Passing nullptr is ok.
///
///   if (S == nullptr) {
///     // Error: ended up in a state from which there is no possible path
///     // to a successful parse.
///   } else if (S->Value == OMPD_unknown) {
///     // Parsed a sequence of tokens that are not a complete name, but
///     // parsing more tokens could lead to a successful parse.
///   } else {
///     // Success.
///     ParsedId = S->Value;
///   }
/// }
struct DirectiveNameParser {
  /// Construct a parser for directive names in source language \p L.
  /// \param L Source language whose directive names are recognized.
  LLVM_ABI DirectiveNameParser(SourceLanguage L = SourceLanguage::C);

  /// Intermediate state while incrementally parsing a directive name.
  struct State {
    /// Recognized directive, or \c OMPD_unknown if the name is incomplete.
    Directive Value = Directive::OMPD_unknown;

  private:
    using TransitionMapTy = StringMap<State>;
    std::unique_ptr<TransitionMapTy> Transition;

    State *next(StringRef Tok);
    const State *next(StringRef Tok) const;
    bool isValid() const {
      return Value != Directive::OMPD_unknown || !Transition->empty();
    }
    friend struct DirectiveNameParser;
  };

  /// Return the starting parse state before any tokens are consumed.
  /// \return The initial parse state.
  const State *initial() const { return &InitialState; }
  /// Advance from \p Current by consuming the next token \p Tok.
  ///
  /// Passing a null \p Current is allowed and returns null.
  /// \param Current Current parse state, or null.
  /// \param Tok Next whitespace-separated token of the directive name.
  /// \return The next state, or null if no valid transition exists.
  LLVM_ABI const State *consume(const State *Current, StringRef Tok) const;

  /// Split a directive name into whitespace-separated tokens.
  /// \param N Directive name string to tokenize.
  /// \return The whitespace-separated tokens of \p N.
  LLVM_ABI static SmallVector<StringRef> tokenize(StringRef N);

private:
  void insertName(StringRef Name, Directive D);
  State *insertTransition(State *From, StringRef Tok);

  State InitialState;
};
} // namespace omp
} // namespace llvm

#endif // LLVM_FRONTEND_OPENMP_DIRECTIVENAMEPARSER_H
