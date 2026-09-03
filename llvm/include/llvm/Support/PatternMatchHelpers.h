//===- PatternMatchHelpers.h - Helpers for PatternMatch -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides helpers that are used across the IR PatternMatch,
// ScalarEvolutionPatternMatch, and VPlanPatternMatch.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_PATTERNMATCHHELPERS_H
#define LLVM_SUPPORT_PATTERNMATCHHELPERS_H

#include "llvm/Support/Casting.h"

namespace llvm::PatternMatchHelpers {
/// Matching or combinator leaf case.
template <typename... Tys> struct match_combine_or { // NOLINT
  /// Always fails; terminates the or-combinator recursion.
  /// \param V Unused value under test.
  /// \return Always false.
  template <typename ITy> bool match(ITy *V) const { return false; }
};

/// Matching or combinator.
template <typename Ty, typename... Tys>
struct match_combine_or<Ty, Tys...> : match_combine_or<Tys...> {
  /// Pattern matcher for this combinator element.
  Ty P;
  /// Construct an or-combinator from \p P and the remaining patterns \p Ps.
  /// \param P First pattern matcher in the or combination.
  /// \param Ps Remaining pattern matchers.
  match_combine_or(const Ty &P, const Tys &...Ps)
      : match_combine_or<Tys...>(Ps...), P(P) {}

  /// Match if \p V matches this pattern or any remaining or-combinator pattern.
  /// \param V The value to match.
  /// \return True if \p V matches this pattern or any remaining pattern.
  template <typename ITy> bool match(ITy *V) const {
    return P.match(V) || match_combine_or<Tys...>::match(V);
  }
};

/// Matching and combinator leaf case.
template <typename... Tys> struct match_combine_and { // NOLINT
  /// Always succeeds; terminates the and-combinator recursion.
  /// \param V Unused value under test.
  /// \return Always true.
  template <typename ITy> bool match(ITy *V) const { return true; }
};

/// Matching and combinator.
template <typename Ty, typename... Tys>
struct match_combine_and<Ty, Tys...> : match_combine_and<Tys...> {
  /// Pattern matcher for this combinator element.
  Ty P;
  /// Construct an and-combinator from \p P and the remaining patterns \p Ps.
  /// \param P First pattern matcher in the and combination.
  /// \param Ps Remaining pattern matchers.
  match_combine_and(const Ty &P, const Tys &...Ps)
      : match_combine_and<Tys...>(Ps...), P(P) {}

  /// Match if \p V matches this pattern and every remaining and-combinator
  /// pattern.
  /// \param V The value to match.
  /// \return True if \p V matches this pattern and every remaining pattern.
  template <typename ITy> bool match(ITy *V) const {
    return P.match(V) && match_combine_and<Tys...>::match(V);
  }
};

/// Combine pattern matchers matching any of Ps patterns.
/// \param Ps Pattern matchers combined with logical or.
/// \return An or-combinator matcher over \p Ps.
template <typename... Ty>
inline match_combine_or<Ty...> m_CombineOr(const Ty &...Ps) { // NOLINT
  return {Ps...};
}

/// Combine pattern matchers matching all of Ps patterns.
/// \param Ps Pattern matchers combined with logical and.
/// \return An and-combinator matcher over \p Ps.
template <typename... Ty>
inline match_combine_and<Ty...> m_CombineAnd(const Ty &...Ps) { // NOLINT
  return {Ps...};
}

/// A match-wrapper around isa.
template <typename... To> struct match_isa { // NOLINT
  /// Match if \p V is an instance of any of the \c To types.
  /// \param V The value to test with \c isa.
  /// \return True if \p V is an instance of any of the \c To types.
  template <typename ArgTy> bool match(const ArgTy *V) const {
    return isa<To...>(V);
  }
};

/// Match a value that is an instance of any of the \c To types.
/// \return An isa matcher for the \c To types.
template <typename... To> inline match_isa<To...> m_Isa() { // NOLINT
  return match_isa<To...>();
}

/// A variant of m_Isa that also matches SubPattern.
/// \param P Nested pattern that must also match after the isa check.
/// \return An and-combinator of an isa matcher and \p P.
template <typename... To, typename SubPattern>
inline auto m_Isa(const SubPattern &P) { // NOLINT
  return m_CombineAnd(m_Isa<To...>(), P);
}

/// Matcher for a specific value, but stores a reference to the value, not the
/// value itself.
template <typename Ty> struct match_deferred { // NOLINT
  /// Reference to the deferred value that must match later.
  Ty *const &Val;
  /// Construct a deferred matcher bound to \p V.
  /// \param V Reference to the value pointer compared during matching.
  match_deferred(Ty *const &V) : Val(V) {}
  /// Match if \p V equals the deferred value.
  /// \param V The value to compare against \c Val.
  /// \return True if \p V equals the deferred value.
  template <typename ITy> bool match(ITy *const V) const { return V == Val; }
};

/// Matcher to bind the captured value.
template <typename Ty> struct match_bind { // NOLINT
  /// Reference that receives the bound value on a successful match.
  Ty *&VR;
  /// Construct a bind matcher that writes into \p V.
  /// \param V Reference updated with the matched value of type \c Ty.
  match_bind(Ty *&V) : VR(V) {}
  /// Match if \p V is a \c Ty, binding it into \c VR.
  /// \param V The value to cast and bind.
  /// \return True if \p V is a \c Ty and was bound into \c VR.
  template <typename ITy> bool match(ITy *V) const {
    if (auto *CV = dyn_cast<Ty>(V)) {
      VR = CV;
      return true;
    }
    return false;
  }
};

/// Inverting matcher that matches a value not matching P.
template <typename Ty> struct match_unless { // NOLINT
  /// Nested pattern whose failure is treated as a match.
  Ty P;
  /// Construct an inverting matcher wrapping \p P.
  /// \param P Pattern that must not match.
  match_unless(const Ty &P) : P(P) {}
  /// Match if \p V does not match the nested pattern.
  /// \param V The value to match.
  /// \return True if \p V does not match the nested pattern.
  template <typename ITy> bool match(ITy *V) const { return !P.match(V); }
};

/// Match if the inner matcher does *NOT* match.
/// \param P Pattern whose failure causes this matcher to succeed.
/// \return An inverting matcher wrapping \p P.
template <typename Pattern>
inline match_unless<Pattern> m_Unless(const Pattern &P) { // NOLINT
  return P;
}
} // namespace llvm::PatternMatchHelpers

#endif // LLVM_SUPPORT_PATTERNMATCHHELPERS_H
