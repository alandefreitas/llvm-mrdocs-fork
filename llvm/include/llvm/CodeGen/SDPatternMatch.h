//==--------------- llvm/CodeGen/SDPatternMatch.h ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// Contains matchers for matching SelectionDAG nodes and values.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SDPATTERNMATCH_H
#define LLVM_CODEGEN_SDPATTERNMATCH_H

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/bit.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/Support/KnownBits.h"

#include <type_traits>

namespace llvm {
/// Matchers for SelectionDAG SDValue and SDNode patterns.
namespace SDPatternMatch {

/// Default match context for SelectionDAG pattern matching.
///
/// MatchContext can repurpose existing patterns to behave differently under
/// a certain context. For instance, `m_SpecificOpc(ISD::ADD)` matches plain ADD
/// nodes in normal circumstances, but matches VP_ADD nodes under a custom
/// VPMatchContext. This design is meant to facilitate code / pattern reusing.
/// TODO: Remove now that we don't need to match over VP nodes.
class BasicMatchContext {
  const SelectionDAG *DAG;
  const TargetLowering *TLI;

public:
  /// Construct a match context from a SelectionDAG.
  ///
  /// \param DAG SelectionDAG providing TargetLowering, or null.
  explicit BasicMatchContext(const SelectionDAG *DAG)
      : DAG(DAG), TLI(DAG ? &DAG->getTargetLoweringInfo() : nullptr) {}

  /// Construct a match context from TargetLowering only.
  ///
  /// \param TLI TargetLowering used for legality queries, or null.
  explicit BasicMatchContext(const TargetLowering *TLI)
      : DAG(nullptr), TLI(TLI) {}

  // A valid MatchContext has to implement the following functions.

  /// Return the SelectionDAG associated with this context, or null.
  ///
  /// \return The associated SelectionDAG, or null.
  const SelectionDAG *getDAG() const { return DAG; }

  /// Return the TargetLowering associated with this context, or null.
  ///
  /// \return The associated TargetLowering, or null.
  const TargetLowering *getTLI() const { return TLI; }

  /// Return true if N effectively has opcode Opcode.
  ///
  /// \param N Value whose opcode is checked.
  /// \param Opcode Opcode to compare against.
  /// \return True if N effectively has opcode Opcode.
  bool match(SDValue N, unsigned Opcode) const {
    return N->getOpcode() == Opcode;
  }

  /// Return the number of operands considered by this context for \p N.
  ///
  /// \param N Value whose operand count is queried.
  /// \return The number of operands considered for N.
  unsigned getNumOperands(SDValue N) const { return N->getNumOperands(); }
};

/// Match SDValue \p N against pattern \p P under match context \p Ctx.
///
/// \param N Value to match.
/// \param Ctx Match context controlling opcode and operand interpretation.
/// \param P Pattern object providing match().
/// \return True if the pattern matches.
template <typename Pattern, typename MatchContext>
[[nodiscard]] bool sd_context_match(SDValue N, const MatchContext &Ctx,
                                    Pattern &&P) {
  return P.match(Ctx, N);
}

/// Match SDNode \p N (result 0) against pattern \p P under context \p Ctx.
///
/// \param N Node to match as SDValue(N, 0).
/// \param Ctx Match context controlling opcode and operand interpretation.
/// \param P Pattern object providing match().
/// \return True if the pattern matches.
template <typename Pattern, typename MatchContext>
[[nodiscard]] bool sd_context_match(SDNode *N, const MatchContext &Ctx,
                                    Pattern &&P) {
  return sd_context_match(SDValue(N, 0), Ctx, P);
}

/// Match SDNode \p N against pattern \p P using a BasicMatchContext for \p DAG.
///
/// \param N Node to match.
/// \param DAG SelectionDAG used to build the match context, or null.
/// \param P Pattern object providing match().
/// \return True if the pattern matches.
template <typename Pattern>
[[nodiscard]] bool sd_match(SDNode *N, const SelectionDAG *DAG, Pattern &&P) {
  return sd_context_match(N, BasicMatchContext(DAG), P);
}

/// Match SDValue \p N against pattern \p P using a BasicMatchContext for \p DAG.
///
/// \param N Value to match.
/// \param DAG SelectionDAG used to build the match context, or null.
/// \param P Pattern object providing match().
/// \return True if the pattern matches.
template <typename Pattern>
[[nodiscard]] bool sd_match(SDValue N, const SelectionDAG *DAG, Pattern &&P) {
  return sd_context_match(N, BasicMatchContext(DAG), P);
}

/// Match SDNode \p N against pattern \p P without a SelectionDAG.
///
/// \param N Node to match.
/// \param P Pattern object providing match().
/// \return True if the pattern matches.
template <typename Pattern>
[[nodiscard]] bool sd_match(SDNode *N, Pattern &&P) {
  return sd_match(N, nullptr, P);
}

/// Match SDValue \p N against pattern \p P without a SelectionDAG.
///
/// \param N Value to match.
/// \param P Pattern object providing match().
/// \return True if the pattern matches.
template <typename Pattern>
[[nodiscard]] bool sd_match(SDValue N, Pattern &&P) {
  return sd_match(N, nullptr, P);
}

// === Utilities ===
/// Matcher for a specific SDValue, or any valid SDValue if unbound.
struct Value_match {
  /// Specific value to match, or null to accept any valid SDValue.
  SDValue MatchVal;

  /// Construct a matcher that accepts any valid SDValue.
  Value_match() = default;

  /// Construct a matcher for a specific SDValue.
  ///
  /// \param Match Specific value that must match.
  explicit Value_match(SDValue Match) : MatchVal(Match) {}

  /// Match \p N as MatchVal, or as any valid SDValue if MatchVal is null.
  ///
  /// \param Ctx Unused match context.
  /// \param N Value to match.
  /// \return True if the value matches.
  template <typename MatchContext>
  bool match(const MatchContext &Ctx, SDValue N) {
    (void)Ctx;
    if (MatchVal)
      return MatchVal == N;
    return N.getNode();
  }
};

/// Match any valid SDValue.
///
/// \return A matcher for any valid SDValue.
inline Value_match m_Value() { return Value_match(); }

/// Match the specific SDValue \p N.
///
/// \param N Specific value that must match.
/// \return A matcher for the specific SDValue \p N.
inline Value_match m_Specific(SDValue N) {
  assert(N);
  return Value_match(N);
}

/// Matcher that requires a specific result number before applying a pattern.
template <unsigned ResNo, typename Pattern> struct Result_match {
  /// Nested pattern applied after the result-number check.
  Pattern P;

  /// Construct a result-number matcher around \p P.
  ///
  /// \param P Nested pattern to apply when ResNo matches.
  explicit Result_match(const Pattern &P) : P(P) {}

  /// Match \p N if its result number is ResNo and \c P matches.
  ///
  /// \param Ctx Match context passed to the nested pattern.
  /// \param N Value to match.
  /// \return True if the result number and nested pattern succeed.
  template <typename MatchContext>
  bool match(const MatchContext &Ctx, SDValue N) {
    return N.getResNo() == ResNo && P.match(Ctx, N);
  }
};

/// Match only if the SDValue is a certain result at ResNo.
///
/// \param P Nested pattern to apply when the result number matches.
/// \return A matcher that applies the nested pattern only at result ResNo.
template <unsigned ResNo, typename Pattern>
inline Result_match<ResNo, Pattern> m_Result(const Pattern &P) {
  return Result_match<ResNo, Pattern>(P);
}

/// Matcher that compares against an SDValue bound by another sub-pattern.
struct DeferredValue_match {
  /// Reference to the SDValue bound by another sub-pattern.
  SDValue &MatchVal;

  /// Construct a deferred-value matcher bound to \p Match.
  ///
  /// \param Match Reference updated by another sub-pattern before matching.
  explicit DeferredValue_match(SDValue &Match) : MatchVal(Match) {}

  /// Match \p N if it equals the deferred MatchVal.
  ///
  /// \param Ctx Unused match context.
  /// \param N Value to match.
  /// \return True if N equals MatchVal.
  template <typename MatchContext>
  bool match(const MatchContext &Ctx, SDValue N) {
    (void)Ctx;
    return N == MatchVal;
  }
};

/// Match the SDValue previously bound into \p V by another sub-pattern.
///
/// Similar to m_Specific, but the specific value to match is determined by
/// another sub-pattern in the same sd_match() expression. For instance,
/// We cannot match `(add V, V)` with `m_Add(m_Value(X), m_Specific(X))` since
/// `X` is not initialized at the time it got copied into `m_Specific`. Instead,
/// we should use `m_Add(m_Value(X), m_Deferred(X))`.
///
/// \param V Reference bound by another sub-pattern in the same expression.
/// \return A matcher for the SDValue previously bound into \p V by another sub-pattern.
inline DeferredValue_match m_Deferred(SDValue &V) {
  return DeferredValue_match(V);
}

/// Matcher for a specific SelectionDAG opcode.
struct Opcode_match {
  /// Opcode that must match.
  unsigned Opcode;

  /// Construct an opcode matcher for \p Opc.
  ///
  /// \param Opc Opcode that must match.
  explicit Opcode_match(unsigned Opc) : Opcode(Opc) {}

  /// Match \p N if the context reports opcode Opcode.
  ///
  /// \param Ctx Match context used to interpret the opcode.
  /// \param N Value to match.
  /// \return True if the opcode matches.
  template <typename MatchContext>
  bool match(const MatchContext &Ctx, SDValue N) {
    return Ctx.match(N, Opcode);
  }
};

// === Patterns combinators ===
/// Conjunction matcher that always succeeds (empty predicate pack).
template <typename... Preds> struct And {
  /// Match any value; used as the base case of And.
  ///
  /// \param Ctx Unused match context.
  /// \param N Value to match.
  /// \return Always true.
  template <typename MatchContext>
  bool match(const MatchContext &Ctx, SDValue N) {
    (void)Ctx;
    (void)N;
    return true;
  }
};

/// Conjunction matcher that requires Pred and the remaining Preds to match.
template <typename Pred, typename... Preds>
struct And<Pred, Preds...> : And<Preds...> {
  /// Head predicate in the conjunction.
  Pred P;
  /// Construct a conjunction from \p p and \p preds.
  ///
  /// \param p Head predicate.
  /// \param preds Remaining predicates in the conjunction.
  And(const Pred &p, const Preds &...preds) : And<Preds...>(preds...), P(p) {}

  /// Match \p N if P and the remaining predicates all match.
  ///
  /// \param Ctx Match context passed to nested predicates.
  /// \param N Value to match.
  /// \return True if every predicate matches.
  template <typename MatchContext>
  bool match(const MatchContext &Ctx, SDValue N) {
    return P.match(Ctx, N) && And<Preds...>::match(Ctx, N);
  }
};

/// Disjunction matcher that always fails (empty predicate pack).
template <typename... Preds> struct Or {
  /// Match any value; used as the base case of Or.
  ///
  /// \param Ctx Unused match context.
  /// \param N Value to match.
  /// \return Always false.
  template <typename MatchContext>
  bool match(const MatchContext &Ctx, SDValue N) {
    (void)Ctx;
    (void)N;
    return false;
  }
};

/// Disjunction matcher that succeeds if Pred or any remaining Preds match.
template <typename Pred, typename... Preds>
struct Or<Pred, Preds...> : Or<Preds...> {
  /// Head predicate in the disjunction.
  Pred P;
  /// Construct a disjunction from \p p and \p preds.
  ///
  /// \param p Head predicate.
  /// \param preds Remaining predicates in the disjunction.
  Or(const Pred &p, const Preds &...preds) : Or<Preds...>(preds...), P(p) {}

  /// Match \p N if P or any remaining predicate matches.
  ///
  /// \param Ctx Match context passed to nested predicates.
  /// \param N Value to match.
  /// \return True if any predicate matches.
  template <typename MatchContext>
  bool match(const MatchContext &Ctx, SDValue N) {
    return P.match(Ctx, N) || Or<Preds...>::match(Ctx, N);
  }
};

/// Negation matcher that succeeds when the nested predicate fails.
template <typename Pred> struct Not {
  /// Nested predicate whose failure is required.
  Pred P;

  /// Construct a negation matcher around \p P.
  ///
  /// \param P Nested predicate to negate.
  explicit Not(const Pred &P) : P(P) {}

  /// Match \p N if P does not match.
  ///
  /// \param Ctx Match context passed to the nested predicate.
  /// \param N Value to match.
  /// \return True if the nested predicate fails.
  template <typename MatchContext>
  bool match(const MatchContext &Ctx, SDValue N) {
    return !P.match(Ctx, N);
  }
};
// Explicit deduction guide.
/// Deduce Not from a predicate argument.
template <typename Pred> Not(const Pred &P) -> Not<Pred>;

/// Match if the inner pattern does NOT match.
///
/// \param P Nested pattern whose failure is required.
/// \return A matcher that succeeds when the inner pattern fails.
template <typename Pred> inline Not<Pred> m_Unless(const Pred &P) {
  return Not{P};
}

/// Match if every nested pattern in \p preds matches.
///
/// \param preds Nested patterns that must all succeed.
/// \return A matcher that requires every nested pattern to succeed.
template <typename... Preds> And<Preds...> m_AllOf(const Preds &...preds) {
  return And<Preds...>(preds...);
}

/// Match if any nested pattern in \p preds matches.
///
/// \param preds Nested patterns, any one of which may succeed.
/// \return A matcher that succeeds if any nested pattern matches.
template <typename... Preds> Or<Preds...> m_AnyOf(const Preds &...preds) {
  return Or<Preds...>(preds...);
}

/// Match if none of the nested patterns in \p preds match.
///
/// \param preds Nested patterns that must all fail.
/// \return A matcher that succeeds only if every nested pattern fails.
template <typename... Preds> auto m_NoneOf(const Preds &...preds) {
  return m_Unless(m_AnyOf(preds...));
}

/// Match a node with the specific opcode \p Opcode.
///
/// \param Opcode Opcode that must match.
/// \return A matcher for a node with the specific opcode \p Opcode.
inline Opcode_match m_SpecificOpc(unsigned Opcode) {
  return Opcode_match(Opcode);
}

/// Match an UNDEF or POISON node.
///
/// \return A matcher for an UNDEF or POISON node.
inline auto m_Undef() {
  return m_AnyOf(Opcode_match(ISD::UNDEF), Opcode_match(ISD::POISON));
}

/// Match a POISON node.
///
/// \return A matcher for a POISON node.
inline Opcode_match m_Poison() { return Opcode_match(ISD::POISON); }

/// Matcher that requires a value to have exactly NumUses uses.
template <unsigned NumUses, typename Pattern> struct NUses_match {
  /// Nested pattern applied before the use-count check.
  Pattern P;

  /// Construct a use-count matcher around \p P.
  ///
  /// \param P Nested pattern to apply before checking uses.
  explicit NUses_match(const Pattern &P) : P(P) {}

  /// Match \p N if P matches and N has exactly NumUses uses.
  ///
  /// \param Ctx Match context passed to the nested pattern.
  /// \param N Value to match.
  /// \return True if the nested pattern and use count succeed.
  template <typename MatchContext>
  bool match(const MatchContext &Ctx, SDValue N) {
    // SDNode::hasNUsesOfValue is pretty expensive when the SDNode produces
    // multiple results, hence we check the subsequent pattern here before
    // checking the number of value users.
    return P.match(Ctx, N) && N->hasNUsesOfValue(NumUses, N.getResNo());
  }
};

/// Match \p P only when the value has exactly one use.
///
/// \param P Nested pattern to apply after the use check.
/// \return A matcher for \p P only when the value has exactly one use.
template <typename Pattern>
inline NUses_match<1, Pattern> m_OneUse(const Pattern &P) {
  return NUses_match<1, Pattern>(P);
}
/// Match \p P only when the value has exactly N uses.
///
/// \param P Nested pattern to apply after the use check.
/// \return A matcher for \p P only when the value has exactly N uses.
template <unsigned N, typename Pattern>
inline NUses_match<N, Pattern> m_NUses(const Pattern &P) {
  return NUses_match<N, Pattern>(P);
}

/// Match any value that has exactly one use.
///
/// \return A matcher for any value that has exactly one use.
inline NUses_match<1, Value_match> m_OneUse() {
  return NUses_match<1, Value_match>(m_Value());
}
/// Match any value that has exactly N uses.
///
/// \return A matcher for any value that has exactly N uses.
template <unsigned N> inline NUses_match<N, Value_match> m_NUses() {
  return NUses_match<N, Value_match>(m_Value());
}

/// Matcher that binds a matching SDValue into a reference.
template <typename PredPattern> struct Value_bind {
  /// Output that receives the matched SDValue.
  SDValue &BindVal;
  /// Nested predicate that must succeed before binding.
  PredPattern Pred;

  /// Construct a binder for \p N guarded by \p P.
  ///
  /// \param N Output that receives the matched value.
  /// \param P Nested predicate that must match.
  Value_bind(SDValue &N, const PredPattern &P) : BindVal(N), Pred(P) {}

  /// Match \p N with Pred and bind it into BindVal.
  ///
  /// \param Ctx Match context passed to Pred.
  /// \param N Value to match and bind.
  /// \return True if Pred matches.
  template <typename MatchContext>
  bool match(const MatchContext &Ctx, SDValue N) {
    if (!Pred.match(Ctx, N))
      return false;

    BindVal = N;
    return true;
  }
};

/// Match any SDValue and bind it into \p N.
///
/// \param N Output that receives the matched value.
/// \return A matcher that binds any SDValue into \p N.
inline auto m_Value(SDValue &N) {
  return Value_bind<Value_match>(N, m_Value());
}
/// Conditionally bind an SDValue based on the predicate.
///
/// \param N Output that receives the matched value.
/// \param P Nested predicate that must match before binding.
/// \return A matcher that binds the value when the predicate matches.
template <typename PredPattern>
inline auto m_Value(SDValue &N, const PredPattern &P) {
  return Value_bind<PredPattern>(N, P);
}

/// Matcher that applies a TargetLowering predicate before a nested pattern.
template <typename Pattern, typename PredFuncT> struct TLI_pred_match {
  /// Nested pattern applied after the TargetLowering predicate.
  Pattern P;
  /// Predicate invoked with TargetLowering and the candidate value.
  PredFuncT PredFunc;

  /// Construct a TargetLowering-predicate matcher.
  ///
  /// \param Pred Predicate over TargetLowering and SDValue.
  /// \param P Nested pattern to apply when Pred succeeds.
  TLI_pred_match(const PredFuncT &Pred, const Pattern &P)
      : P(P), PredFunc(Pred) {}

  /// Match \p N if PredFunc and P both succeed.
  ///
  /// \param Ctx Match context providing TargetLowering.
  /// \param N Value to match.
  /// \return True if both predicates succeed.
  template <typename MatchContext>
  bool match(const MatchContext &Ctx, SDValue N) {
    assert(Ctx.getTLI() && "TargetLowering is required for this pattern.");
    return PredFunc(*Ctx.getTLI(), N) && P.match(Ctx, N);
  }
};

// Explicit deduction guide.
/// Deduce TLI_pred_match from predicate and pattern arguments.
template <typename PredFuncT, typename Pattern>
TLI_pred_match(const PredFuncT &Pred, const Pattern &P)
    -> TLI_pred_match<Pattern, PredFuncT>;

/// Match legal SDNodes based on the information provided by TargetLowering.
///
/// \param P Nested pattern applied after the legality check.
/// \return A matcher for legal SDNodes based on the information provided by TargetLowering.
template <typename Pattern> inline auto m_LegalOp(const Pattern &P) {
  return TLI_pred_match{[](const TargetLowering &TLI, SDValue N) {
                          return TLI.isOperationLegal(N->getOpcode(),
                                                      N.getValueType());
                        },
                        P};
}

/// Switch to a different MatchContext for subsequent patterns.
template <typename NewMatchContext, typename Pattern> struct SwitchContext {
  /// Replacement match context used for nested matching.
  const NewMatchContext &Ctx;
  /// Nested pattern evaluated under Ctx.
  Pattern P;

  /// Match \p N under Ctx instead of the original match context.
  ///
  /// \param OrigCtx Unused original match context.
  /// \param N Value to match.
  /// \return True if P matches under Ctx.
  template <typename OrigMatchContext>
  bool match(const OrigMatchContext &OrigCtx, SDValue N) {
    (void)OrigCtx;
    return P.match(Ctx, N);
  }
};

/// Evaluate nested pattern \p P under an alternate match context \p Ctx.
///
/// \param Ctx Alternate match context for nested matching.
/// \param P Nested pattern to evaluate under Ctx.
/// \return A matcher that evaluates the nested pattern under Ctx.
template <typename MatchContext, typename Pattern>
inline SwitchContext<MatchContext, Pattern> m_Context(const MatchContext &Ctx,
                                                      Pattern &&P) {
  return SwitchContext<MatchContext, Pattern>{Ctx, std::move(P)};
}

// === Value type ===

/// Matcher that binds the value type of a matching SDValue.
template <typename Pattern> struct ValueType_bind {
  /// Output that receives the matched value type.
  EVT &BindVT;
  /// Nested pattern applied after binding the value type.
  Pattern P;

  /// Construct a value-type binder for \p Bind guarded by \p P.
  ///
  /// \param Bind Output that receives the value type.
  /// \param P Nested pattern that must match.
  explicit ValueType_bind(EVT &Bind, const Pattern &P) : BindVT(Bind), P(P) {}

  /// Bind N's value type and match with P.
  ///
  /// \param Ctx Match context passed to P.
  /// \param N Value whose type is bound.
  /// \return True if P matches.
  template <typename MatchContext>
  bool match(const MatchContext &Ctx, SDValue N) {
    BindVT = N.getValueType();
    return P.match(Ctx, N);
  }
};

/// Deduce ValueType_bind from a pattern argument.
template <typename Pattern>
ValueType_bind(const Pattern &P) -> ValueType_bind<Pattern>;

/// Retreive the ValueType of the current SDValue.
///
/// \param VT Output that receives the matched value type.
/// \return A matcher that binds the value type.
inline auto m_VT(EVT &VT) { return ValueType_bind(VT, m_Value()); }

/// Bind the value type into \p VT and match nested pattern \p P.
///
/// \param VT Output that receives the matched value type.
/// \param P Nested pattern that must match.
/// \return A matcher that binds the value type and applies the nested pattern.
template <typename Pattern> inline auto m_VT(EVT &VT, const Pattern &P) {
  return ValueType_bind(VT, P);
}

/// Matcher that checks a value-type predicate before a nested pattern.
template <typename Pattern, typename PredFuncT> struct ValueType_match {
  /// Predicate over the candidate value type.
  PredFuncT PredFunc;
  /// Nested pattern applied after the type predicate.
  Pattern P;

  /// Construct a value-type predicate matcher.
  ///
  /// \param Pred Predicate over EVT.
  /// \param P Nested pattern to apply when Pred succeeds.
  ValueType_match(const PredFuncT &Pred, const Pattern &P)
      : PredFunc(Pred), P(P) {}

  /// Match \p N if PredFunc accepts its type and P matches.
  ///
  /// \param Ctx Match context passed to P.
  /// \param N Value to match.
  /// \return True if both checks succeed.
  template <typename MatchContext>
  bool match(const MatchContext &Ctx, SDValue N) {
    return PredFunc(N.getValueType()) && P.match(Ctx, N);
  }
};

// Explicit deduction guide.
/// Deduce ValueType_match from predicate and pattern arguments.
template <typename PredFuncT, typename Pattern>
ValueType_match(const PredFuncT &Pred, const Pattern &P)
    -> ValueType_match<Pattern, PredFuncT>;

/// Match a specific ValueType.
///
/// \param RefVT Exact value type that must match.
/// \param P Nested pattern applied after the type check.
/// \return A matcher for a specific ValueType.
template <typename Pattern>
inline auto m_SpecificVT(EVT RefVT, const Pattern &P) {
  return ValueType_match{[=](EVT VT) { return VT == RefVT; }, P};
}
/// Match a specific ValueType.
///
/// \param RefVT Exact value type that must match.
/// \return A matcher for a specific ValueType.
inline auto m_SpecificVT(EVT RefVT) {
  return ValueType_match{[=](EVT VT) { return VT == RefVT; }, m_Value()};
}

/// Match the Glue value type.
///
/// \return A matcher for the Glue value type.
inline auto m_Glue() { return m_SpecificVT(MVT::Glue); }
/// Match the Other (chain) value type.
///
/// \return A matcher for the Other (chain) value type.
inline auto m_OtherVT() { return m_SpecificVT(MVT::Other); }

/// Match a scalar ValueType.
///
/// \param RefVT Scalar type that VT.getScalarType() must equal.
/// \param P Nested pattern applied after the type check.
/// \return A matcher for a scalar ValueType.
template <typename Pattern>
inline auto m_SpecificScalarVT(EVT RefVT, const Pattern &P) {
  return ValueType_match{[=](EVT VT) { return VT.getScalarType() == RefVT; },
                         P};
}
/// Match a scalar ValueType.
///
/// \param RefVT Scalar type that VT.getScalarType() must equal.
/// \return A matcher for a scalar ValueType.
inline auto m_SpecificScalarVT(EVT RefVT) {
  return ValueType_match{[=](EVT VT) { return VT.getScalarType() == RefVT; },
                         m_Value()};
}

/// Match a vector ValueType.
///
/// \param RefVT Element type that a vector VT must have.
/// \param P Nested pattern applied after the type check.
/// \return A matcher for a vector ValueType.
template <typename Pattern>
inline auto m_SpecificVectorElementVT(EVT RefVT, const Pattern &P) {
  return ValueType_match{[=](EVT VT) {
                           return VT.isVector() &&
                                  VT.getVectorElementType() == RefVT;
                         },
                         P};
}
/// Match a vector ValueType.
///
/// \param RefVT Element type that a vector VT must have.
/// \return A matcher for a vector ValueType.
inline auto m_SpecificVectorElementVT(EVT RefVT) {
  return ValueType_match{[=](EVT VT) {
                           return VT.isVector() &&
                                  VT.getVectorElementType() == RefVT;
                         },
                         m_Value()};
}

/// Match any integer ValueTypes.
///
/// \param P Nested pattern applied after the type check.
/// \return A matcher for any integer ValueTypes.
template <typename Pattern> inline auto m_IntegerVT(const Pattern &P) {
  return ValueType_match{[](EVT VT) { return VT.isInteger(); }, P};
}
/// Match any integer ValueTypes.
///
/// \return A matcher for any integer ValueTypes.
inline auto m_IntegerVT() {
  return ValueType_match{[](EVT VT) { return VT.isInteger(); }, m_Value()};
}

/// Match any floating point ValueTypes.
///
/// \param P Nested pattern applied after the type check.
/// \return A matcher for any floating point ValueTypes.
template <typename Pattern> inline auto m_FloatingPointVT(const Pattern &P) {
  return ValueType_match{[](EVT VT) { return VT.isFloatingPoint(); }, P};
}
/// Match any floating point ValueTypes.
///
/// \return A matcher for any floating point ValueTypes.
inline auto m_FloatingPointVT() {
  return ValueType_match{[](EVT VT) { return VT.isFloatingPoint(); },
                         m_Value()};
}

/// Match any vector ValueTypes.
///
/// \param P Nested pattern applied after the type check.
/// \return A matcher for any vector ValueTypes.
template <typename Pattern> inline auto m_VectorVT(const Pattern &P) {
  return ValueType_match{[](EVT VT) { return VT.isVector(); }, P};
}
/// Match any vector ValueTypes.
///
/// \return A matcher for any vector ValueTypes.
inline auto m_VectorVT() {
  return ValueType_match{[](EVT VT) { return VT.isVector(); }, m_Value()};
}

/// Match fixed-length vector ValueTypes.
///
/// \param P Nested pattern applied after the type check.
/// \return A matcher for fixed-length vector ValueTypes.
template <typename Pattern> inline auto m_FixedVectorVT(const Pattern &P) {
  return ValueType_match{[](EVT VT) { return VT.isFixedLengthVector(); }, P};
}
/// Match fixed-length vector ValueTypes.
///
/// \return A matcher for fixed-length vector ValueTypes.
inline auto m_FixedVectorVT() {
  return ValueType_match{[](EVT VT) { return VT.isFixedLengthVector(); },
                         m_Value()};
}

/// Match scalable vector ValueTypes.
///
/// \param P Nested pattern applied after the type check.
/// \return A matcher for scalable vector ValueTypes.
template <typename Pattern> inline auto m_ScalableVectorVT(const Pattern &P) {
  return ValueType_match{[](EVT VT) { return VT.isScalableVector(); }, P};
}
/// Match scalable vector ValueTypes.
///
/// \return A matcher for scalable vector ValueTypes.
inline auto m_ScalableVectorVT() {
  return ValueType_match{[](EVT VT) { return VT.isScalableVector(); },
                         m_Value()};
}

/// Match legal ValueTypes based on the information provided by TargetLowering.
///
/// \param P Nested pattern applied after the legality check.
/// \return A matcher for legal ValueTypes based on the information provided by TargetLowering.
template <typename Pattern> inline auto m_LegalType(const Pattern &P) {
  return TLI_pred_match{[](const TargetLowering &TLI, SDValue N) {
                          return TLI.isTypeLegal(N.getValueType());
                        },
                        P};
}

// === Generic node matching ===
/// Base operand matcher that succeeds when the operand count equals OpIdx.
template <unsigned OpIdx, typename... OpndPreds> struct Operands_match {
  /// Match \p N when it has exactly OpIdx operands under \p Ctx.
  ///
  /// \param Ctx Match context used for operand counting.
  /// \param N Value whose operands are counted.
  /// \return True if the operand count equals OpIdx.
  template <typename MatchContext>
  bool match(const MatchContext &Ctx, SDValue N) {
    // Returns false if there are more operands than predicates;
    // Ignores the last two operands if both the Context and the Node are VP
    return Ctx.getNumOperands(N) == OpIdx;
  }
};

/// Operand matcher that checks operand OpIdx then the remaining predicates.
template <unsigned OpIdx, typename OpndPred, typename... OpndPreds>
struct Operands_match<OpIdx, OpndPred, OpndPreds...>
    : Operands_match<OpIdx + 1, OpndPreds...> {
  /// Predicate applied to operand OpIdx.
  OpndPred P;

  /// Construct an operand matcher from \p p and \p preds.
  ///
  /// \param p Predicate for operand OpIdx.
  /// \param preds Predicates for subsequent operands.
  Operands_match(const OpndPred &p, const OpndPreds &...preds)
      : Operands_match<OpIdx + 1, OpndPreds...>(preds...), P(p) {}

  /// Match operand OpIdx with P and the remaining operands recursively.
  ///
  /// \param Ctx Match context passed to nested predicates.
  /// \param N Value whose operands are matched.
  /// \return True if every operand predicate succeeds.
  template <typename MatchContext>
  bool match(const MatchContext &Ctx, SDValue N) {
    if (OpIdx < N->getNumOperands())
      return P.match(Ctx, N->getOperand(OpIdx)) &&
             Operands_match<OpIdx + 1, OpndPreds...>::match(Ctx, N);

    // This is the case where there are more predicates than operands.
    return false;
  }
};

/// Match a node with opcode \p Opcode and operands matching \p preds.
///
/// \param Opcode Opcode that must match.
/// \param preds Operand predicates applied in order.
/// \return A matcher for a node with opcode \p Opcode and operands matching \p preds.
template <typename... OpndPreds>
auto m_Node(unsigned Opcode, const OpndPreds &...preds) {
  return m_AllOf(m_SpecificOpc(Opcode),
                 Operands_match<0, OpndPreds...>(preds...));
}

/// Provide number of operands that are not chain or glue, as well as the first
/// index of such operand.
template <bool ExcludeChain> struct EffectiveOperands {
  /// Number of effective (non-chain/non-glue) operands.
  unsigned Size = 0;
  /// Index of the first effective operand.
  unsigned FirstIndex = 0;

  /// Count effective operands of \p N under \p Ctx.
  ///
  /// \param N Value whose operands are inspected.
  /// \param Ctx Match context used for operand counting.
  template <typename MatchContext>
  explicit EffectiveOperands(SDValue N, const MatchContext &Ctx) {
    const unsigned TotalNumOps = Ctx.getNumOperands(N);
    FirstIndex = TotalNumOps;
    for (unsigned I = 0; I < TotalNumOps; ++I) {
      // Count the number of non-chain and non-glue nodes (we ignore chain
      // and glue by default) and retreive the operand index offset.
      EVT VT = N->getOperand(I).getValueType();
      if (VT != MVT::Glue && VT != MVT::Other) {
        ++Size;
        if (FirstIndex == TotalNumOps)
          FirstIndex = I;
      }
    }
  }
};

/// EffectiveOperands specialization that counts every operand.
template <> struct EffectiveOperands<false> {
  /// Number of operands reported by the match context.
  unsigned Size = 0;
  /// Always zero when chains are not excluded.
  unsigned FirstIndex = 0;

  /// Record the full operand count of \p N under \p Ctx.
  ///
  /// \param N Value whose operands are counted.
  /// \param Ctx Match context used for operand counting.
  template <typename MatchContext>
  explicit EffectiveOperands(SDValue N, const MatchContext &Ctx)
      : Size(Ctx.getNumOperands(N)) {}
};

// === Ternary operations ===
/// Matcher for a ternary opcode with three operand patterns.
template <typename T0_P, typename T1_P, typename T2_P, bool Commutable = false,
          bool ExcludeChain = false>
struct TernaryOpc_match {
  /// Opcode that must match.
  unsigned Opcode;
  /// Sub-pattern for the first effective operand.
  T0_P Op0;
  /// Sub-pattern for the second effective operand.
  T1_P Op1;
  /// Sub-pattern for the third effective operand.
  T2_P Op2;

  /// Construct a ternary opcode matcher.
  ///
  /// \param Opc Opcode that must match.
  /// \param Op0 Sub-pattern for the first operand.
  /// \param Op1 Sub-pattern for the second operand.
  /// \param Op2 Sub-pattern for the third operand.
  TernaryOpc_match(unsigned Opc, const T0_P &Op0, const T1_P &Op1,
                   const T2_P &Op2)
      : Opcode(Opc), Op0(Op0), Op1(Op1), Op2(Op2) {}

  /// Match \p N as Opcode with operands Op0, Op1, and Op2.
  ///
  /// \param Ctx Match context used for opcode and operand interpretation.
  /// \param N Value to match.
  /// \return True if the opcode and operands match.
  template <typename MatchContext>
  bool match(const MatchContext &Ctx, SDValue N) {
    if (sd_context_match(N, Ctx, m_SpecificOpc(Opcode))) {
      EffectiveOperands<ExcludeChain> EO(N, Ctx);
      assert(EO.Size == 3);
      return ((Op0.match(Ctx, N->getOperand(EO.FirstIndex)) &&
               Op1.match(Ctx, N->getOperand(EO.FirstIndex + 1))) ||
              (Commutable && Op0.match(Ctx, N->getOperand(EO.FirstIndex + 1)) &&
               Op1.match(Ctx, N->getOperand(EO.FirstIndex)))) &&
             Op2.match(Ctx, N->getOperand(EO.FirstIndex + 2));
    }

    return false;
  }
};

/// Match a SETCC with operands \p LHS, \p RHS, and condition code \p CC.
///
/// \param LHS Sub-pattern for the left comparison operand.
/// \param RHS Sub-pattern for the right comparison operand.
/// \param CC Sub-pattern for the condition code.
/// \return A matcher for a SETCC with operands \p LHS, \p RHS, and condition code \p CC.
template <typename T0_P, typename T1_P, typename T2_P>
inline TernaryOpc_match<T0_P, T1_P, T2_P>
m_SetCC(const T0_P &LHS, const T1_P &RHS, const T2_P &CC) {
  return TernaryOpc_match<T0_P, T1_P, T2_P>(ISD::SETCC, LHS, RHS, CC);
}

/// Match a commutative SETCC with operands \p LHS, \p RHS, and \p CC.
///
/// \param LHS Sub-pattern for one comparison operand.
/// \param RHS Sub-pattern for the other comparison operand.
/// \param CC Sub-pattern for the condition code.
/// \return A matcher for a commutative SETCC with operands \p LHS, \p RHS, and \p CC.
template <typename T0_P, typename T1_P, typename T2_P>
inline TernaryOpc_match<T0_P, T1_P, T2_P, true, false>
m_c_SetCC(const T0_P &LHS, const T1_P &RHS, const T2_P &CC) {
  return TernaryOpc_match<T0_P, T1_P, T2_P, true, false>(ISD::SETCC, LHS, RHS,
                                                         CC);
}

/// Match a SELECT with condition \p Cond and values \p T and \p F.
///
/// \param Cond Sub-pattern for the condition operand.
/// \param T Sub-pattern for the true value.
/// \param F Sub-pattern for the false value.
/// \return A matcher for a SELECT with condition \p Cond and values \p T and \p F.
template <typename T0_P, typename T1_P, typename T2_P>
inline TernaryOpc_match<T0_P, T1_P, T2_P>
m_Select(const T0_P &Cond, const T1_P &T, const T2_P &F) {
  return TernaryOpc_match<T0_P, T1_P, T2_P>(ISD::SELECT, Cond, T, F);
}

/// Match a VSELECT with condition \p Cond and values \p T and \p F.
///
/// \param Cond Sub-pattern for the condition operand.
/// \param T Sub-pattern for the true value.
/// \param F Sub-pattern for the false value.
/// \return A matcher for a VSELECT with condition \p Cond and values \p T and \p F.
template <typename T0_P, typename T1_P, typename T2_P>
inline TernaryOpc_match<T0_P, T1_P, T2_P>
m_VSelect(const T0_P &Cond, const T1_P &T, const T2_P &F) {
  return TernaryOpc_match<T0_P, T1_P, T2_P>(ISD::VSELECT, Cond, T, F);
}

/// Match either SELECT or VSELECT with \p Cond, \p T, and \p F.
///
/// \param Cond Sub-pattern for the condition operand.
/// \param T Sub-pattern for the true value.
/// \param F Sub-pattern for the false value.
/// \return A matcher for either SELECT or VSELECT with \p Cond, \p T, and \p F.
template <typename T0_P, typename T1_P, typename T2_P>
inline auto m_SelectLike(const T0_P &Cond, const T1_P &T, const T2_P &F) {
  return m_AnyOf(m_Select(Cond, T, F), m_VSelect(Cond, T, F));
}

/// Match a LOAD node's data result with chain \p Ch, pointer \p Ptr, and
/// offset \p Offset.
///
/// \param Ch Sub-pattern for the chain operand.
/// \param Ptr Sub-pattern for the pointer operand.
/// \param Offset Sub-pattern for the offset operand.
/// \return A matcher for a LOAD node's data result with chain \p Ch, pointer \p Ptr, and.
template <typename T0_P, typename T1_P, typename T2_P>
inline Result_match<0, TernaryOpc_match<T0_P, T1_P, T2_P>>
m_Load(const T0_P &Ch, const T1_P &Ptr, const T2_P &Offset) {
  return m_Result<0>(
      TernaryOpc_match<T0_P, T1_P, T2_P>(ISD::LOAD, Ch, Ptr, Offset));
}

/// Match INSERT_VECTOR_ELT with vector \p Vec, value \p Val, and index \p Idx.
///
/// \param Vec Sub-pattern for the destination vector.
/// \param Val Sub-pattern for the inserted element.
/// \param Idx Sub-pattern for the element index.
/// \return A matcher for INSERT_VECTOR_ELT with vector \p Vec, value \p Val, and index \p Idx.
template <typename T0_P, typename T1_P, typename T2_P>
inline TernaryOpc_match<T0_P, T1_P, T2_P>
m_InsertElt(const T0_P &Vec, const T1_P &Val, const T2_P &Idx) {
  return TernaryOpc_match<T0_P, T1_P, T2_P>(ISD::INSERT_VECTOR_ELT, Vec, Val,
                                            Idx);
}

/// Match INSERT_SUBVECTOR with base \p Base, subvector \p Sub, and index \p Idx.
///
/// \param Base Sub-pattern for the destination vector.
/// \param Sub Sub-pattern for the inserted subvector.
/// \param Idx Sub-pattern for the insertion index.
/// \return A matcher for INSERT_SUBVECTOR with base \p Base, subvector \p Sub, and index \p Idx.
template <typename LHS, typename RHS, typename IDX>
inline TernaryOpc_match<LHS, RHS, IDX>
m_InsertSubvector(const LHS &Base, const RHS &Sub, const IDX &Idx) {
  return TernaryOpc_match<LHS, RHS, IDX>(ISD::INSERT_SUBVECTOR, Base, Sub, Idx);
}

/// Match VECTOR_SPLICE_RIGHT with vectors \p V1 and \p V2 and offset \p Offset.
///
/// \param V1 Sub-pattern for the first vector operand.
/// \param V2 Sub-pattern for the second vector operand.
/// \param Offset Sub-pattern for the splice offset.
/// \return A matcher for VECTOR_SPLICE_RIGHT with vectors \p V1 and \p V2 and offset \p Offset.
template <typename T0_P, typename T1_P, typename T2_P>
inline TernaryOpc_match<T0_P, T1_P, T2_P>
m_SpliceRight(const T0_P &V1, const T1_P &V2, const T2_P &Offset) {
  return TernaryOpc_match<T0_P, T1_P, T2_P>(ISD::VECTOR_SPLICE_RIGHT, V1, V2,
                                            Offset);
}

/// Match a ternary opcode \p Opc with operands \p Op0, \p Op1, and \p Op2.
///
/// \param Opc Opcode that must match.
/// \param Op0 Sub-pattern for the first operand.
/// \param Op1 Sub-pattern for the second operand.
/// \param Op2 Sub-pattern for the third operand.
/// \return A matcher for a ternary opcode \p Opc with operands \p Op0, \p Op1, and \p Op2.
template <typename T0_P, typename T1_P, typename T2_P>
inline TernaryOpc_match<T0_P, T1_P, T2_P>
m_TernaryOp(unsigned Opc, const T0_P &Op0, const T1_P &Op1, const T2_P &Op2) {
  return TernaryOpc_match<T0_P, T1_P, T2_P>(Opc, Op0, Op1, Op2);
}

/// Match a commutative ternary opcode \p Opc with \p Op0, \p Op1, and \p Op2.
///
/// \param Opc Opcode that must match.
/// \param Op0 Sub-pattern for the first operand.
/// \param Op1 Sub-pattern for the second operand.
/// \param Op2 Sub-pattern for the third operand.
/// \return A matcher for a commutative ternary opcode \p Opc with \p Op0, \p Op1, and \p Op2.
template <typename T0_P, typename T1_P, typename T2_P>
inline TernaryOpc_match<T0_P, T1_P, T2_P, true>
m_c_TernaryOp(unsigned Opc, const T0_P &Op0, const T1_P &Op1, const T2_P &Op2) {
  return TernaryOpc_match<T0_P, T1_P, T2_P, true>(Opc, Op0, Op1, Op2);
}

/// Match SELECT_CC with compared values, result values, and condition code.
///
/// \param L Sub-pattern for the left comparison operand.
/// \param R Sub-pattern for the right comparison operand.
/// \param T Sub-pattern for the true value.
/// \param F Sub-pattern for the false value.
/// \param CC Sub-pattern for the condition code.
/// \return A matcher for SELECT_CC with compared values, result values, and condition code.
template <typename LTy, typename RTy, typename TTy, typename FTy, typename CCTy>
inline auto m_SelectCC(const LTy &L, const RTy &R, const TTy &T, const FTy &F,
                       const CCTy &CC) {
  return m_Node(ISD::SELECT_CC, L, R, T, F, CC);
}

/// Match SELECT(SETCC(...)) or SELECT_CC with the same operands.
///
/// \param L Sub-pattern for the left comparison operand.
/// \param R Sub-pattern for the right comparison operand.
/// \param T Sub-pattern for the true value.
/// \param F Sub-pattern for the false value.
/// \param CC Sub-pattern for the condition code.
/// \return A matcher for SELECT(SETCC(...)) or SELECT_CC with the same operands.
template <typename LTy, typename RTy, typename TTy, typename FTy, typename CCTy>
inline auto m_SelectCCLike(const LTy &L, const RTy &R, const TTy &T,
                           const FTy &F, const CCTy &CC) {
  return m_AnyOf(m_Select(m_SetCC(L, R, CC), T, F), m_SelectCC(L, R, T, F, CC));
}

// === Binary operations ===
/// Matcher for a binary opcode with left and right operand patterns.
template <typename LHS_P, typename RHS_P, bool Commutable = false,
          bool ExcludeChain = false>
struct BinaryOpc_match {
  /// Opcode that must match.
  unsigned Opcode;
  /// Sub-pattern for the left-hand operand.
  LHS_P LHS;
  /// Sub-pattern for the right-hand operand.
  RHS_P RHS;
  /// Node flags that must be present on a match.
  SDNodeFlags Flags;
  /// Construct a binary opcode matcher.
  ///
  /// \param Opc Opcode that must match.
  /// \param L Sub-pattern for the left-hand operand.
  /// \param R Sub-pattern for the right-hand operand.
  /// \param Flgs Required SDNodeFlags, or empty for no flag requirements.
  BinaryOpc_match(unsigned Opc, const LHS_P &L, const RHS_P &R,
                  SDNodeFlags Flgs = SDNodeFlags())
      : Opcode(Opc), LHS(L), RHS(R), Flags(Flgs) {}

  /// Match \p N as Opcode with operands LHS and RHS and required Flags.
  ///
  /// \param Ctx Match context used for opcode and operand interpretation.
  /// \param N Value to match.
  /// \return True if the opcode, operands, and flags match.
  template <typename MatchContext>
  bool match(const MatchContext &Ctx, SDValue N) {
    if (sd_context_match(N, Ctx, m_SpecificOpc(Opcode))) {
      EffectiveOperands<ExcludeChain> EO(N, Ctx);
      assert(EO.Size == 2);
      if (!((LHS.match(Ctx, N->getOperand(EO.FirstIndex)) &&
             RHS.match(Ctx, N->getOperand(EO.FirstIndex + 1))) ||
            (Commutable && LHS.match(Ctx, N->getOperand(EO.FirstIndex + 1)) &&
             RHS.match(Ctx, N->getOperand(EO.FirstIndex)))))
        return false;

      return (Flags & N->getFlags()) == Flags;
    }

    return false;
  }
};

/// Matcher for VECTOR_SHUFFLE that also matches the shuffle mask.
template <typename T0, typename T1, typename T2> struct SDShuffle_match {
  /// Sub-pattern for the first shuffle operand.
  T0 Op1;
  /// Sub-pattern for the second shuffle operand.
  T1 Op2;
  /// Sub-pattern for the shuffle mask.
  T2 Mask;

  /// Construct a shuffle matcher for operands and mask.
  ///
  /// \param Op1 Sub-pattern for the first operand.
  /// \param Op2 Sub-pattern for the second operand.
  /// \param Mask Sub-pattern for the shuffle mask.
  SDShuffle_match(const T0 &Op1, const T1 &Op2, const T2 &Mask)
      : Op1(Op1), Op2(Op2), Mask(Mask) {}

  /// Match \p N as a ShuffleVectorSDNode against Op1, Op2, and Mask.
  ///
  /// \param Ctx Match context passed to operand patterns.
  /// \param N Value to match.
  /// \return True if the shuffle operands and mask match.
  template <typename MatchContext>
  bool match(const MatchContext &Ctx, SDValue N) {
    if (auto *I = dyn_cast<ShuffleVectorSDNode>(N)) {
      return Op1.match(Ctx, I->getOperand(0)) &&
             Op2.match(Ctx, I->getOperand(1)) && Mask.match(I->getMask());
    }
    return false;
  }
};
/// Matcher that binds a shuffle mask into a reference.
struct m_Mask {
  /// Output that receives the matched shuffle mask.
  ArrayRef<int> &MaskRef;
  /// Construct a mask binder for \p MaskRef.
  ///
  /// \param MaskRef Output that receives the matched mask.
  m_Mask(ArrayRef<int> &MaskRef) : MaskRef(MaskRef) {}
  /// Bind \p Mask into MaskRef.
  ///
  /// \param Mask Shuffle mask to bind.
  /// \return Always true.
  bool match(ArrayRef<int> Mask) {
    MaskRef = Mask;
    return true;
  }
};

/// Matcher for an exact shuffle mask.
struct m_SpecificMask {
  /// Exact mask that must match.
  ArrayRef<int> MaskRef;
  /// Construct a matcher for the specific mask \p MaskRef.
  ///
  /// \param MaskRef Exact mask that must match.
  m_SpecificMask(ArrayRef<int> MaskRef) : MaskRef(MaskRef) {}
  /// Match \p Mask if it equals MaskRef.
  ///
  /// \param Mask Shuffle mask to compare.
  /// \return True if the masks are equal.
  bool match(ArrayRef<int> Mask) { return MaskRef == Mask; }
};

/// Matcher for min/max idioms expressed as SELECT/SETCC or SELECT_CC.
template <typename LHS_P, typename RHS_P, typename Pred_t,
          bool Commutable = false, bool ExcludeChain = false>
struct MaxMin_match {
  /// Predicate type that recognizes the min/max condition codes.
  using PredType = Pred_t;
  /// Sub-pattern for the left-hand operand.
  LHS_P LHS;
  /// Sub-pattern for the right-hand operand.
  RHS_P RHS;

  /// Construct a min/max matcher for operands \p L and \p R.
  ///
  /// \param L Sub-pattern for the left-hand operand.
  /// \param R Sub-pattern for the right-hand operand.
  MaxMin_match(const LHS_P &L, const RHS_P &R) : LHS(L), RHS(R) {}

  /// Match \p N as a SELECT/SETCC or SELECT_CC min/max idiom.
  ///
  /// \param Ctx Match context used for nested matching.
  /// \param N Value to match.
  /// \return True if the min/max idiom and operands match.
  template <typename MatchContext>
  bool match(const MatchContext &Ctx, SDValue N) {
    auto MatchMinMax = [&](SDValue L, SDValue R, SDValue TrueValue,
                           SDValue FalseValue, ISD::CondCode CC) {
      if ((TrueValue != L || FalseValue != R) &&
          (TrueValue != R || FalseValue != L))
        return false;

      ISD::CondCode Cond =
          TrueValue == L ? CC : getSetCCInverse(CC, L.getValueType());
      if (!Pred_t::match(Cond))
        return false;

      return (LHS.match(Ctx, L) && RHS.match(Ctx, R)) ||
             (Commutable && LHS.match(Ctx, R) && RHS.match(Ctx, L));
    };

    if (sd_context_match(N, Ctx, m_SpecificOpc(ISD::SELECT)) ||
        sd_context_match(N, Ctx, m_SpecificOpc(ISD::VSELECT))) {
      EffectiveOperands<ExcludeChain> EO_SELECT(N, Ctx);
      assert(EO_SELECT.Size == 3);
      SDValue Cond = N->getOperand(EO_SELECT.FirstIndex);
      SDValue TrueValue = N->getOperand(EO_SELECT.FirstIndex + 1);
      SDValue FalseValue = N->getOperand(EO_SELECT.FirstIndex + 2);

      if (sd_context_match(Cond, Ctx, m_SpecificOpc(ISD::SETCC))) {
        EffectiveOperands<ExcludeChain> EO_SETCC(Cond, Ctx);
        assert(EO_SETCC.Size == 3);
        SDValue L = Cond->getOperand(EO_SETCC.FirstIndex);
        SDValue R = Cond->getOperand(EO_SETCC.FirstIndex + 1);
        auto *CondNode =
            cast<CondCodeSDNode>(Cond->getOperand(EO_SETCC.FirstIndex + 2));
        return MatchMinMax(L, R, TrueValue, FalseValue, CondNode->get());
      }
    }

    if (sd_context_match(N, Ctx, m_SpecificOpc(ISD::SELECT_CC))) {
      EffectiveOperands<ExcludeChain> EO_SELECT(N, Ctx);
      assert(EO_SELECT.Size == 5);
      SDValue L = N->getOperand(EO_SELECT.FirstIndex);
      SDValue R = N->getOperand(EO_SELECT.FirstIndex + 1);
      SDValue TrueValue = N->getOperand(EO_SELECT.FirstIndex + 2);
      SDValue FalseValue = N->getOperand(EO_SELECT.FirstIndex + 3);
      auto *CondNode =
          cast<CondCodeSDNode>(N->getOperand(EO_SELECT.FirstIndex + 4));
      return MatchMinMax(L, R, TrueValue, FalseValue, CondNode->get());
    }

    return false;
  }
};

/// Helper that recognizes signed-max SETCC predicates.
struct smax_pred_ty {
  /// Return true if \p Cond is a signed-max comparison.
  ///
  /// \param Cond Condition code to classify.
  /// \return True for SETGT or SETGE.
  static bool match(ISD::CondCode Cond) {
    return Cond == ISD::CondCode::SETGT || Cond == ISD::CondCode::SETGE;
  }
};

/// Helper that recognizes unsigned-max SETCC predicates.
struct umax_pred_ty {
  /// Return true if \p Cond is an unsigned-max comparison.
  ///
  /// \param Cond Condition code to classify.
  /// \return True for SETUGT or SETUGE.
  static bool match(ISD::CondCode Cond) {
    return Cond == ISD::CondCode::SETUGT || Cond == ISD::CondCode::SETUGE;
  }
};

/// Helper that recognizes signed-min SETCC predicates.
struct smin_pred_ty {
  /// Return true if \p Cond is a signed-min comparison.
  ///
  /// \param Cond Condition code to classify.
  /// \return True for SETLT or SETLE.
  static bool match(ISD::CondCode Cond) {
    return Cond == ISD::CondCode::SETLT || Cond == ISD::CondCode::SETLE;
  }
};

/// Helper that recognizes unsigned-min SETCC predicates.
struct umin_pred_ty {
  /// Return true if \p Cond is an unsigned-min comparison.
  ///
  /// \param Cond Condition code to classify.
  /// \return True for SETULT or SETULE.
  static bool match(ISD::CondCode Cond) {
    return Cond == ISD::CondCode::SETULT || Cond == ISD::CondCode::SETULE;
  }
};

/// Match binary opcode \p Opc with operands \p L and \p R.
///
/// \param Opc Opcode that must match.
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \param Flgs Required SDNodeFlags, or empty for no flag requirements.
/// \return A matcher for binary opcode \p Opc with operands \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOpc_match<LHS, RHS> m_BinOp(unsigned Opc, const LHS &L,
                                         const RHS &R,
                                         SDNodeFlags Flgs = SDNodeFlags()) {
  return BinaryOpc_match<LHS, RHS>(Opc, L, R, Flgs);
}
/// Match commutative binary opcode \p Opc with operands \p L and \p R.
///
/// \param Opc Opcode that must match.
/// \param L Sub-pattern for one operand.
/// \param R Sub-pattern for the other operand.
/// \param Flgs Required SDNodeFlags, or empty for no flag requirements.
/// \return A matcher for commutative binary opcode \p Opc with operands \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOpc_match<LHS, RHS, true>
m_c_BinOp(unsigned Opc, const LHS &L, const RHS &R,
          SDNodeFlags Flgs = SDNodeFlags()) {
  return BinaryOpc_match<LHS, RHS, true>(Opc, L, R, Flgs);
}

/// Match binary opcode \p Opc while ignoring chain/glue operands.
///
/// \param Opc Opcode that must match.
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for binary opcode \p Opc while ignoring chain/glue operands.
template <typename LHS, typename RHS>
inline BinaryOpc_match<LHS, RHS, false, true>
m_ChainedBinOp(unsigned Opc, const LHS &L, const RHS &R) {
  return BinaryOpc_match<LHS, RHS, false, true>(Opc, L, R);
}
/// Match commutative binary opcode \p Opc while ignoring chain/glue operands.
///
/// \param Opc Opcode that must match.
/// \param L Sub-pattern for one operand.
/// \param R Sub-pattern for the other operand.
/// \return A matcher for commutative binary opcode \p Opc while ignoring chain/glue operands.
template <typename LHS, typename RHS>
inline BinaryOpc_match<LHS, RHS, true, true>
m_c_ChainedBinOp(unsigned Opc, const LHS &L, const RHS &R) {
  return BinaryOpc_match<LHS, RHS, true, true>(Opc, L, R);
}

// Common binary operations
/// Match commutative ADD with operands \p L and \p R.
///
/// \param L Sub-pattern for one addend.
/// \param R Sub-pattern for the other addend.
/// \return A matcher for commutative ADD with operands \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOpc_match<LHS, RHS, true> m_Add(const LHS &L, const RHS &R) {
  return BinaryOpc_match<LHS, RHS, true>(ISD::ADD, L, R);
}

/// Match NUW ADD with operands \p L and \p R.
///
/// \param L Sub-pattern for one addend.
/// \param R Sub-pattern for the other addend.
/// \return A matcher for NUW ADD with operands \p L and \p R.
template <typename LHS, typename RHS>
inline auto m_NUWAdd(const LHS &L, const RHS &R) {
  return BinaryOpc_match<LHS, RHS, true>(ISD::ADD, L, R,
                                         SDNodeFlags::NoUnsignedWrap);
}

/// Match NSW ADD with operands \p L and \p R.
///
/// \param L Sub-pattern for one addend.
/// \param R Sub-pattern for the other addend.
/// \return A matcher for NSW ADD with operands \p L and \p R.
template <typename LHS, typename RHS>
inline auto m_NSWAdd(const LHS &L, const RHS &R) {
  return BinaryOpc_match<LHS, RHS, true>(ISD::ADD, L, R,
                                         SDNodeFlags::NoSignedWrap);
}

/// Match SUB with operands \p L and \p R.
///
/// \param L Sub-pattern for the minuend.
/// \param R Sub-pattern for the subtrahend.
/// \return A matcher for SUB with operands \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOpc_match<LHS, RHS> m_Sub(const LHS &L, const RHS &R) {
  return BinaryOpc_match<LHS, RHS>(ISD::SUB, L, R);
}

/// Match commutative MUL with operands \p L and \p R.
///
/// \param L Sub-pattern for one factor.
/// \param R Sub-pattern for the other factor.
/// \return A matcher for commutative MUL with operands \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOpc_match<LHS, RHS, true> m_Mul(const LHS &L, const RHS &R) {
  return BinaryOpc_match<LHS, RHS, true>(ISD::MUL, L, R);
}

/// Match commutative AND with operands \p L and \p R.
///
/// \param L Sub-pattern for one operand.
/// \param R Sub-pattern for the other operand.
/// \return A matcher for commutative AND with operands \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOpc_match<LHS, RHS, true> m_And(const LHS &L, const RHS &R) {
  return BinaryOpc_match<LHS, RHS, true>(ISD::AND, L, R);
}

/// Match commutative OR with operands \p L and \p R.
///
/// \param L Sub-pattern for one operand.
/// \param R Sub-pattern for the other operand.
/// \return A matcher for commutative OR with operands \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOpc_match<LHS, RHS, true> m_Or(const LHS &L, const RHS &R) {
  return BinaryOpc_match<LHS, RHS, true>(ISD::OR, L, R);
}

/// Match disjoint OR with operands \p L and \p R.
///
/// \param L Sub-pattern for one operand.
/// \param R Sub-pattern for the other operand.
/// \return A matcher for disjoint OR with operands \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOpc_match<LHS, RHS, true> m_DisjointOr(const LHS &L,
                                                    const RHS &R) {
  return BinaryOpc_match<LHS, RHS, true>(ISD::OR, L, R, SDNodeFlags::Disjoint);
}

/// Match ADD or disjoint OR with operands \p L and \p R.
///
/// \param L Sub-pattern for one operand.
/// \param R Sub-pattern for the other operand.
/// \return A matcher for ADD or disjoint OR with operands \p L and \p R.
template <typename LHS, typename RHS>
inline auto m_AddLike(const LHS &L, const RHS &R) {
  return m_AnyOf(m_Add(L, R), m_DisjointOr(L, R));
}

/// Match NSW ADD or disjoint OR with operands \p L and \p R.
///
/// \param L Sub-pattern for one operand.
/// \param R Sub-pattern for the other operand.
/// \return A matcher for NSW ADD or disjoint OR with operands \p L and \p R.
template <typename LHS, typename RHS>
inline auto m_NSWAddLike(const LHS &L, const RHS &R) {
  return m_AnyOf(m_NSWAdd(L, R), m_DisjointOr(L, R));
}

/// Match NUW ADD or disjoint OR with operands \p L and \p R.
///
/// \param L Sub-pattern for one operand.
/// \param R Sub-pattern for the other operand.
/// \return A matcher for NUW ADD or disjoint OR with operands \p L and \p R.
template <typename LHS, typename RHS>
inline auto m_NUWAddLike(const LHS &L, const RHS &R) {
  return m_AnyOf(m_NUWAdd(L, R), m_DisjointOr(L, R));
}

/// Match commutative XOR with operands \p L and \p R.
///
/// \param L Sub-pattern for one operand.
/// \param R Sub-pattern for the other operand.
/// \return A matcher for commutative XOR with operands \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOpc_match<LHS, RHS, true> m_Xor(const LHS &L, const RHS &R) {
  return BinaryOpc_match<LHS, RHS, true>(ISD::XOR, L, R);
}

/// Match AND, OR, or XOR with operands \p L and \p R.
///
/// \param L Sub-pattern for one operand.
/// \param R Sub-pattern for the other operand.
/// \return A matcher for AND, OR, or XOR with operands \p L and \p R.
template <typename LHS, typename RHS>
inline auto m_BitwiseLogic(const LHS &L, const RHS &R) {
  return m_AnyOf(m_And(L, R), m_Or(L, R), m_Xor(L, R));
}

/// Match intrinsic min/max opcode \p Opc or an equivalent SELECT/SETCC form.
///
/// \param L Sub-pattern for one operand.
/// \param R Sub-pattern for the other operand.
/// \return A matcher for intrinsic min/max opcode \p Opc or an equivalent SELECT/SETCC form.
template <unsigned Opc, typename Pred, typename LHS, typename RHS>
inline auto m_MaxMinLike(const LHS &L, const RHS &R) {
  return m_AnyOf(BinaryOpc_match<LHS, RHS, true>(Opc, L, R),
                 MaxMin_match<LHS, RHS, Pred, true>(L, R));
}

/// Match commutative SMIN with operands \p L and \p R.
///
/// \param L Sub-pattern for one operand.
/// \param R Sub-pattern for the other operand.
/// \return A matcher for commutative SMIN with operands \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOpc_match<LHS, RHS, true> m_SMin(const LHS &L, const RHS &R) {
  return BinaryOpc_match<LHS, RHS, true>(ISD::SMIN, L, R);
}

/// Match signed min, including equivalent unsigned forms with known signs.
///
/// \param L Sub-pattern for one operand.
/// \param R Sub-pattern for the other operand.
/// \return A matcher for signed min, including equivalent unsigned forms with known signs.
template <typename LHS, typename RHS>
inline auto m_SMinLike(const LHS &L, const RHS &R) {
  return m_AnyOf(
      m_MaxMinLike<ISD::SMIN, smin_pred_ty>(L, R),
      m_MaxMinLike<ISD::UMIN, umin_pred_ty>(m_NonNegative(L), m_NonNegative(R)),
      m_MaxMinLike<ISD::UMIN, umin_pred_ty>(m_Negative(L), m_Negative(R)));
}

/// Match commutative SMAX with operands \p L and \p R.
///
/// \param L Sub-pattern for one operand.
/// \param R Sub-pattern for the other operand.
/// \return A matcher for commutative SMAX with operands \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOpc_match<LHS, RHS, true> m_SMax(const LHS &L, const RHS &R) {
  return BinaryOpc_match<LHS, RHS, true>(ISD::SMAX, L, R);
}

/// Match signed max, including equivalent unsigned forms with known signs.
///
/// \param L Sub-pattern for one operand.
/// \param R Sub-pattern for the other operand.
/// \return A matcher for signed max, including equivalent unsigned forms with known signs.
template <typename LHS, typename RHS>
inline auto m_SMaxLike(const LHS &L, const RHS &R) {
  return m_AnyOf(
      m_MaxMinLike<ISD::SMAX, smax_pred_ty>(L, R),
      m_MaxMinLike<ISD::UMAX, umax_pred_ty>(m_NonNegative(L), m_NonNegative(R)),
      m_MaxMinLike<ISD::UMAX, umax_pred_ty>(m_Negative(L), m_Negative(R)));
}

/// Match commutative UMIN with operands \p L and \p R.
///
/// \param L Sub-pattern for one operand.
/// \param R Sub-pattern for the other operand.
/// \return A matcher for commutative UMIN with operands \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOpc_match<LHS, RHS, true> m_UMin(const LHS &L, const RHS &R) {
  return BinaryOpc_match<LHS, RHS, true>(ISD::UMIN, L, R);
}

/// Match unsigned min, including equivalent signed forms with known signs.
///
/// \param L Sub-pattern for one operand.
/// \param R Sub-pattern for the other operand.
/// \return A matcher for unsigned min, including equivalent signed forms with known signs.
template <typename LHS, typename RHS>
inline auto m_UMinLike(const LHS &L, const RHS &R) {
  return m_AnyOf(
      m_MaxMinLike<ISD::UMIN, umin_pred_ty>(L, R),
      m_MaxMinLike<ISD::SMIN, smin_pred_ty>(m_NonNegative(L), m_NonNegative(R)),
      m_MaxMinLike<ISD::SMIN, smin_pred_ty>(m_Negative(L), m_Negative(R)));
}

/// Match commutative UMAX with operands \p L and \p R.
///
/// \param L Sub-pattern for one operand.
/// \param R Sub-pattern for the other operand.
/// \return A matcher for commutative UMAX with operands \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOpc_match<LHS, RHS, true> m_UMax(const LHS &L, const RHS &R) {
  return BinaryOpc_match<LHS, RHS, true>(ISD::UMAX, L, R);
}

/// Match unsigned max, including equivalent signed forms with known signs.
///
/// \param L Sub-pattern for one operand.
/// \param R Sub-pattern for the other operand.
/// \return A matcher for unsigned max, including equivalent signed forms with known signs.
template <typename LHS, typename RHS>
inline auto m_UMaxLike(const LHS &L, const RHS &R) {
  return m_AnyOf(
      m_MaxMinLike<ISD::UMAX, umax_pred_ty>(L, R),
      m_MaxMinLike<ISD::SMAX, smax_pred_ty>(m_NonNegative(L), m_NonNegative(R)),
      m_MaxMinLike<ISD::SMAX, smax_pred_ty>(m_Negative(L), m_Negative(R)));
}

/// Match UDIV with operands \p L and \p R.
///
/// \param L Sub-pattern for the dividend.
/// \param R Sub-pattern for the divisor.
/// \return A matcher for UDIV with operands \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOpc_match<LHS, RHS> m_UDiv(const LHS &L, const RHS &R) {
  return BinaryOpc_match<LHS, RHS>(ISD::UDIV, L, R);
}
/// Match SDIV with operands \p L and \p R.
///
/// \param L Sub-pattern for the dividend.
/// \param R Sub-pattern for the divisor.
/// \return A matcher for SDIV with operands \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOpc_match<LHS, RHS> m_SDiv(const LHS &L, const RHS &R) {
  return BinaryOpc_match<LHS, RHS>(ISD::SDIV, L, R);
}

/// Match UREM with operands \p L and \p R.
///
/// \param L Sub-pattern for the dividend.
/// \param R Sub-pattern for the divisor.
/// \return A matcher for UREM with operands \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOpc_match<LHS, RHS> m_URem(const LHS &L, const RHS &R) {
  return BinaryOpc_match<LHS, RHS>(ISD::UREM, L, R);
}
/// Match SREM with operands \p L and \p R.
///
/// \param L Sub-pattern for the dividend.
/// \param R Sub-pattern for the divisor.
/// \return A matcher for SREM with operands \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOpc_match<LHS, RHS> m_SRem(const LHS &L, const RHS &R) {
  return BinaryOpc_match<LHS, RHS>(ISD::SREM, L, R);
}

/// Match SHL with operands \p L and \p R.
///
/// \param L Sub-pattern for the value being shifted.
/// \param R Sub-pattern for the shift amount.
/// \return A matcher for SHL with operands \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOpc_match<LHS, RHS> m_Shl(const LHS &L, const RHS &R) {
  return BinaryOpc_match<LHS, RHS>(ISD::SHL, L, R);
}

/// Match SRA with operands \p L and \p R.
///
/// \param L Sub-pattern for the value being shifted.
/// \param R Sub-pattern for the shift amount.
/// \return A matcher for SRA with operands \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOpc_match<LHS, RHS> m_Sra(const LHS &L, const RHS &R) {
  return BinaryOpc_match<LHS, RHS>(ISD::SRA, L, R);
}
/// Match SRL with operands \p L and \p R.
///
/// \param L Sub-pattern for the value being shifted.
/// \param R Sub-pattern for the shift amount.
/// \return A matcher for SRL with operands \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOpc_match<LHS, RHS> m_Srl(const LHS &L, const RHS &R) {
  return BinaryOpc_match<LHS, RHS>(ISD::SRL, L, R);
}
/// Match exact SRA or exact SRL with operands \p L and \p R.
///
/// \param L Sub-pattern for the value being shifted.
/// \param R Sub-pattern for the shift amount.
/// \return A matcher for exact SRA or exact SRL with operands \p L and \p R.
template <typename LHS, typename RHS>
inline auto m_ExactSr(const LHS &L, const RHS &R) {
  return m_AnyOf(BinaryOpc_match<LHS, RHS>(ISD::SRA, L, R, SDNodeFlags::Exact),
                 BinaryOpc_match<LHS, RHS>(ISD::SRL, L, R, SDNodeFlags::Exact));
}

/// Match ROTL with operands \p L and \p R.
///
/// \param L Sub-pattern for the value being rotated.
/// \param R Sub-pattern for the rotate amount.
/// \return A matcher for ROTL with operands \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOpc_match<LHS, RHS> m_Rotl(const LHS &L, const RHS &R) {
  return BinaryOpc_match<LHS, RHS>(ISD::ROTL, L, R);
}

/// Match ROTR with operands \p L and \p R.
///
/// \param L Sub-pattern for the value being rotated.
/// \param R Sub-pattern for the rotate amount.
/// \return A matcher for ROTR with operands \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOpc_match<LHS, RHS> m_Rotr(const LHS &L, const RHS &R) {
  return BinaryOpc_match<LHS, RHS>(ISD::ROTR, L, R);
}

/// Match FSHL with operands \p Op0, \p Op1, and shift amount \p Op2.
///
/// \param Op0 Sub-pattern for the high input.
/// \param Op1 Sub-pattern for the low input.
/// \param Op2 Sub-pattern for the shift amount.
/// \return A matcher for FSHL with operands \p Op0, \p Op1, and shift amount \p Op2.
template <typename T0_P, typename T1_P, typename T2_P>
inline TernaryOpc_match<T0_P, T1_P, T2_P>
m_FShL(const T0_P &Op0, const T1_P &Op1, const T2_P &Op2) {
  return m_TernaryOp(ISD::FSHL, Op0, Op1, Op2);
}

/// Match FSHR with operands \p Op0, \p Op1, and shift amount \p Op2.
///
/// \param Op0 Sub-pattern for the high input.
/// \param Op1 Sub-pattern for the low input.
/// \param Op2 Sub-pattern for the shift amount.
/// \return A matcher for FSHR with operands \p Op0, \p Op1, and shift amount \p Op2.
template <typename T0_P, typename T1_P, typename T2_P>
inline TernaryOpc_match<T0_P, T1_P, T2_P>
m_FShR(const T0_P &Op0, const T1_P &Op1, const T2_P &Op2) {
  return m_TernaryOp(ISD::FSHR, Op0, Op1, Op2);
}

/// Matcher for funnel-shift idioms, including rotates and or-of-shifts forms.
template <typename T0_P, typename T1_P, typename T2_P, bool Left>
struct FunnelShiftLike_match {
  /// Sub-pattern for the high funnel-shift input.
  T0_P Op0;
  /// Sub-pattern for the low funnel-shift input.
  T1_P Op1;
  /// Sub-pattern for the shift amount.
  T2_P Op2;

  /// Construct a funnel-shift-like matcher.
  ///
  /// \param Op0 Sub-pattern for the high input.
  /// \param Op1 Sub-pattern for the low input.
  /// \param Op2 Sub-pattern for the shift amount.
  FunnelShiftLike_match(const T0_P &Op0, const T1_P &Op1, const T2_P &Op2)
      : Op0(Op0), Op1(Op1), Op2(Op2) {}

  /// Return true if constant shifts \p ShlV and \p SrlV sum to \p BitWidth.
  ///
  /// \param ShlV Left-shift amount constant.
  /// \param SrlV Right-shift amount constant.
  /// \param BitWidth Scalar bit width of the shifted value.
  /// \return True if the constants are complementary funnel-shift amounts.
  static bool hasComplementaryConstantShifts(const APInt &ShlV,
                                             const APInt &SrlV,
                                             unsigned BitWidth) {
    unsigned SumWidth = std::max(ShlV.getBitWidth(), SrlV.getBitWidth()) + 1;
    unsigned BitWidthBits = llvm::bit_width(BitWidth);
    if (BitWidthBits > SumWidth)
      return false;

    return ShlV.zext(SumWidth) + SrlV.zext(SumWidth) ==
           APInt(SumWidth, BitWidth);
  }

  /// Match funnel-shift operands \p X, \p Y, and \p Z against Op0/Op1/Op2.
  ///
  /// \param Ctx Match context passed to nested patterns.
  /// \param X Candidate high input.
  /// \param Y Candidate low input.
  /// \param Z Candidate shift amount.
  /// \return True if all three operand patterns match.
  template <typename MatchContext>
  bool matchOperands(const MatchContext &Ctx, SDValue X, SDValue Y, SDValue Z) {
    return Op0.match(Ctx, X) && Op1.match(Ctx, Y) && Op2.match(Ctx, Z);
  }

  /// Match an or-of-shifts funnel-shift expansion of bit width \p BitWidth.
  ///
  /// \param Ctx Match context used for nested matching.
  /// \param N Value to match.
  /// \param BitWidth Scalar bit width used for complementary shift checks.
  /// \return True if the or-of-shifts form matches.
  template <typename MatchContext>
  bool matchShiftOr(const MatchContext &Ctx, SDValue N, unsigned BitWidth);

  /// Match \p N as a funnel shift, rotate, or or-of-shifts expansion.
  ///
  /// \param Ctx Match context used for nested matching.
  /// \param N Value to match.
  /// \return True if any recognized funnel-shift form matches.
  template <typename MatchContext>
  bool match(const MatchContext &Ctx, SDValue N) {
    if (sd_context_match(N, Ctx,
                         Left ? m_FShL(Op0, Op1, Op2) : m_FShR(Op0, Op1, Op2)))
      return true;

    SDValue X, Z;
    if (sd_context_match(N, Ctx,
                         Left ? m_Rotl(m_Value(X), m_Value(Z))
                              : m_Rotr(m_Value(X), m_Value(Z))))
      return matchOperands(Ctx, X, X, Z);

    return matchShiftOr(Ctx, N, N.getValueType().getScalarSizeInBits());
  }
};

/// Match left funnel-shift-like forms with operands \p Op0, \p Op1, and \p Op2.
///
/// \param Op0 Sub-pattern for the high input.
/// \param Op1 Sub-pattern for the low input.
/// \param Op2 Sub-pattern for the shift amount.
/// \return A matcher for left funnel-shift-like forms with operands \p Op0, \p Op1, and \p Op2.
template <typename T0_P, typename T1_P, typename T2_P>
inline FunnelShiftLike_match<T0_P, T1_P, T2_P, true>
m_FShLLike(const T0_P &Op0, const T1_P &Op1, const T2_P &Op2) {
  return FunnelShiftLike_match<T0_P, T1_P, T2_P, true>(Op0, Op1, Op2);
}

/// Match right funnel-shift-like forms with operands \p Op0, \p Op1, and \p Op2.
///
/// \param Op0 Sub-pattern for the high input.
/// \param Op1 Sub-pattern for the low input.
/// \param Op2 Sub-pattern for the shift amount.
/// \return A matcher for right funnel-shift-like forms with operands \p Op0, \p Op1, and \p Op2.
template <typename T0_P, typename T1_P, typename T2_P>
inline FunnelShiftLike_match<T0_P, T1_P, T2_P, false>
m_FShRLike(const T0_P &Op0, const T1_P &Op1, const T2_P &Op2) {
  return FunnelShiftLike_match<T0_P, T1_P, T2_P, false>(Op0, Op1, Op2);
}

/// Match commutative CLMUL with operands \p L and \p R.
///
/// \param L Sub-pattern for one operand.
/// \param R Sub-pattern for the other operand.
/// \return A matcher for commutative CLMUL with operands \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOpc_match<LHS, RHS, true> m_Clmul(const LHS &L, const RHS &R) {
  return BinaryOpc_match<LHS, RHS, true>(ISD::CLMUL, L, R);
}

/// Match commutative FADD with operands \p L and \p R.
///
/// \param L Sub-pattern for one addend.
/// \param R Sub-pattern for the other addend.
/// \return A matcher for commutative FADD with operands \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOpc_match<LHS, RHS, true> m_FAdd(const LHS &L, const RHS &R) {
  return BinaryOpc_match<LHS, RHS, true>(ISD::FADD, L, R);
}

/// Match FSUB with operands \p L and \p R.
///
/// \param L Sub-pattern for the minuend.
/// \param R Sub-pattern for the subtrahend.
/// \return A matcher for FSUB with operands \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOpc_match<LHS, RHS> m_FSub(const LHS &L, const RHS &R) {
  return BinaryOpc_match<LHS, RHS>(ISD::FSUB, L, R);
}

/// Match commutative FMUL with operands \p L and \p R.
///
/// \param L Sub-pattern for one factor.
/// \param R Sub-pattern for the other factor.
/// \return A matcher for commutative FMUL with operands \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOpc_match<LHS, RHS, true> m_FMul(const LHS &L, const RHS &R) {
  return BinaryOpc_match<LHS, RHS, true>(ISD::FMUL, L, R);
}

/// Match FDIV with operands \p L and \p R.
///
/// \param L Sub-pattern for the dividend.
/// \param R Sub-pattern for the divisor.
/// \return A matcher for FDIV with operands \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOpc_match<LHS, RHS> m_FDiv(const LHS &L, const RHS &R) {
  return BinaryOpc_match<LHS, RHS>(ISD::FDIV, L, R);
}

/// Match FREM with operands \p L and \p R.
///
/// \param L Sub-pattern for the dividend.
/// \param R Sub-pattern for the divisor.
/// \return A matcher for FREM with operands \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOpc_match<LHS, RHS> m_FRem(const LHS &L, const RHS &R) {
  return BinaryOpc_match<LHS, RHS>(ISD::FREM, L, R);
}

/// Match VECTOR_SHUFFLE with operands \p v1 and \p v2.
///
/// \param v1 Sub-pattern for the first vector operand.
/// \param v2 Sub-pattern for the second vector operand.
/// \return A matcher for VECTOR_SHUFFLE with operands \p v1 and \p v2.
template <typename V1_t, typename V2_t>
inline BinaryOpc_match<V1_t, V2_t> m_Shuffle(const V1_t &v1, const V2_t &v2) {
  return BinaryOpc_match<V1_t, V2_t>(ISD::VECTOR_SHUFFLE, v1, v2);
}

/// Match VECTOR_SHUFFLE with operands \p v1, \p v2, and mask \p mask.
///
/// \param v1 Sub-pattern for the first vector operand.
/// \param v2 Sub-pattern for the second vector operand.
/// \param mask Sub-pattern for the shuffle mask.
/// \return A matcher for VECTOR_SHUFFLE with operands \p v1, \p v2, and mask \p mask.
template <typename V1_t, typename V2_t, typename Mask_t>
inline SDShuffle_match<V1_t, V2_t, Mask_t>
m_Shuffle(const V1_t &v1, const V2_t &v2, const Mask_t &mask) {
  return SDShuffle_match<V1_t, V2_t, Mask_t>(v1, v2, mask);
}

/// Match EXTRACT_VECTOR_ELT with vector \p Vec and index \p Idx.
///
/// \param Vec Sub-pattern for the source vector.
/// \param Idx Sub-pattern for the element index.
/// \return A matcher for EXTRACT_VECTOR_ELT with vector \p Vec and index \p Idx.
template <typename LHS, typename RHS>
inline BinaryOpc_match<LHS, RHS> m_ExtractElt(const LHS &Vec, const RHS &Idx) {
  return BinaryOpc_match<LHS, RHS>(ISD::EXTRACT_VECTOR_ELT, Vec, Idx);
}

/// Match EXTRACT_SUBVECTOR with vector \p Vec and index \p Idx.
///
/// \param Vec Sub-pattern for the source vector.
/// \param Idx Sub-pattern for the subvector index.
/// \return A matcher for EXTRACT_SUBVECTOR with vector \p Vec and index \p Idx.
template <typename LHS, typename RHS>
inline BinaryOpc_match<LHS, RHS> m_ExtractSubvector(const LHS &Vec,
                                                    const RHS &Idx) {
  return BinaryOpc_match<LHS, RHS>(ISD::EXTRACT_SUBVECTOR, Vec, Idx);
}

// === Unary operations ===
/// Matcher for a unary opcode with an operand pattern and optional flags.
template <typename Opnd_P, bool ExcludeChain = false> struct UnaryOpc_match {
  /// Opcode that must match.
  unsigned Opcode;
  /// Sub-pattern for the unary operand.
  Opnd_P Opnd;
  /// Node flags that must be present on a match.
  SDNodeFlags Flags;
  /// Construct a unary opcode matcher.
  ///
  /// \param Opc Opcode that must match.
  /// \param Op Sub-pattern for the unary operand.
  /// \param Flgs Required SDNodeFlags, or empty for no flag requirements.
  UnaryOpc_match(unsigned Opc, const Opnd_P &Op,
                 SDNodeFlags Flgs = SDNodeFlags())
      : Opcode(Opc), Opnd(Op), Flags(Flgs) {}

  /// Match \p N as Opcode with operand Opnd and required Flags.
  ///
  /// \param Ctx Match context used for opcode and operand interpretation.
  /// \param N Value to match.
  /// \return True if the opcode, operand, and flags match.
  template <typename MatchContext>
  bool match(const MatchContext &Ctx, SDValue N) {
    if (sd_context_match(N, Ctx, m_SpecificOpc(Opcode))) {
      EffectiveOperands<ExcludeChain> EO(N, Ctx);
      assert(EO.Size == 1);
      if (!Opnd.match(Ctx, N->getOperand(EO.FirstIndex)))
        return false;

      return (Flags & N->getFlags()) == Flags;
    }

    return false;
  }
};

/// Match unary opcode \p Opc with operand \p Op.
///
/// \param Opc Opcode that must match.
/// \param Op Sub-pattern for the unary operand.
/// \return A matcher for unary opcode \p Opc with operand \p Op.
template <typename Opnd>
inline UnaryOpc_match<Opnd> m_UnaryOp(unsigned Opc, const Opnd &Op) {
  return UnaryOpc_match<Opnd>(Opc, Op);
}
/// Match unary opcode \p Opc while ignoring chain/glue operands.
///
/// \param Opc Opcode that must match.
/// \param Op Sub-pattern for the unary operand.
/// \return A matcher for unary opcode \p Opc while ignoring chain/glue operands.
template <typename Opnd>
inline UnaryOpc_match<Opnd, true> m_ChainedUnaryOp(unsigned Opc,
                                                   const Opnd &Op) {
  return UnaryOpc_match<Opnd, true>(Opc, Op);
}

/// Match BITCAST of \p Op.
///
/// \param Op Sub-pattern for the cast operand.
/// \return A matcher for BITCAST of \p Op.
template <typename Opnd> inline UnaryOpc_match<Opnd> m_BitCast(const Opnd &Op) {
  return UnaryOpc_match<Opnd>(ISD::BITCAST, Op);
}

/// Match BSWAP of \p Op.
///
/// \param Op Sub-pattern for the byteswapped operand.
/// \return A matcher for BSWAP of \p Op.
template <typename Opnd>
inline UnaryOpc_match<Opnd> m_BSwap(const Opnd &Op) {
  return UnaryOpc_match<Opnd>(ISD::BSWAP, Op);
}

/// Match BITREVERSE of \p Op.
///
/// \param Op Sub-pattern for the bit-reversed operand.
/// \return A matcher for BITREVERSE of \p Op.
template <typename Opnd>
inline UnaryOpc_match<Opnd> m_BitReverse(const Opnd &Op) {
  return UnaryOpc_match<Opnd>(ISD::BITREVERSE, Op);
}

/// Match ZERO_EXTEND of \p Op.
///
/// \param Op Sub-pattern for the extended operand.
/// \return A matcher for ZERO_EXTEND of \p Op.
template <typename Opnd> inline UnaryOpc_match<Opnd> m_ZExt(const Opnd &Op) {
  return UnaryOpc_match<Opnd>(ISD::ZERO_EXTEND, Op);
}

/// Match non-negative ZERO_EXTEND of \p Op.
///
/// \param Op Sub-pattern for the extended operand.
/// \return A matcher for non-negative ZERO_EXTEND of \p Op.
template <typename Opnd>
inline UnaryOpc_match<Opnd> m_NNegZExt(const Opnd &Op) {
  return UnaryOpc_match<Opnd>(ISD::ZERO_EXTEND, Op, SDNodeFlags::NonNeg);
}

/// Match SIGN_EXTEND of \p Op.
///
/// \param Op Sub-pattern for the extended operand.
/// \return A matcher for SIGN_EXTEND of \p Op.
template <typename Opnd> inline auto m_SExt(const Opnd &Op) {
  return UnaryOpc_match<Opnd>(ISD::SIGN_EXTEND, Op);
}

/// Match ANY_EXTEND of \p Op.
///
/// \param Op Sub-pattern for the extended operand.
/// \return A matcher for ANY_EXTEND of \p Op.
template <typename Opnd> inline UnaryOpc_match<Opnd> m_AnyExt(const Opnd &Op) {
  return UnaryOpc_match<Opnd>(ISD::ANY_EXTEND, Op);
}

/// Match TRUNCATE of \p Op.
///
/// \param Op Sub-pattern for the truncated operand.
/// \return A matcher for TRUNCATE of \p Op.
template <typename Opnd> inline UnaryOpc_match<Opnd> m_Trunc(const Opnd &Op) {
  return UnaryOpc_match<Opnd>(ISD::TRUNCATE, Op);
}

/// Match ABS or ABS_MIN_POISON of \p Op.
///
/// \param Op Sub-pattern for the absolute-value operand.
/// \return A matcher for ABS or ABS_MIN_POISON of \p Op.
template <typename Opnd> inline auto m_Abs(const Opnd &Op) {
  return m_AnyOf(UnaryOpc_match<Opnd>(ISD::ABS, Op),
                 UnaryOpc_match<Opnd>(ISD::ABS_MIN_POISON, Op));
}

/// Match FABS of \p Op.
///
/// \param Op Sub-pattern for the absolute-value operand.
/// \return A matcher for FABS of \p Op.
template <typename Opnd> inline UnaryOpc_match<Opnd> m_FAbs(const Opnd &Op) {
  return UnaryOpc_match<Opnd>(ISD::FABS, Op);
}

/// Match a zext or identity.
///
/// Allows to peek through optional extensions.
///
/// \param Op Nested pattern matched directly or under ZERO_EXTEND.
/// \return A matcher for a zext or identity.
template <typename Opnd> inline auto m_ZExtOrSelf(const Opnd &Op) {
  return m_AnyOf(m_ZExt(Op), Op);
}

/// Match a sext or identity.
///
/// Allows to peek through optional extensions.
///
/// \param Op Nested pattern matched directly or under SIGN_EXTEND.
/// \return A matcher for a sext or identity.
template <typename Opnd> inline auto m_SExtOrSelf(const Opnd &Op) {
  return m_AnyOf(m_SExt(Op), Op);
}

/// Match SIGN_EXTEND or non-negative ZERO_EXTEND of \p Op.
///
/// \param Op Sub-pattern for the extended operand.
/// \return A matcher for SIGN_EXTEND or non-negative ZERO_EXTEND of \p Op.
template <typename Opnd> inline auto m_SExtLike(const Opnd &Op) {
  return m_AnyOf(m_SExt(Op), m_NNegZExt(Op));
}

/// Match a aext or identity.
///
/// Allows to peek through optional extensions.
///
/// \param Op Nested pattern matched directly or under ANY_EXTEND.
/// \return A matcher for a aext or identity.
template <typename Opnd>
inline Or<UnaryOpc_match<Opnd>, Opnd> m_AExtOrSelf(const Opnd &Op) {
  return Or<UnaryOpc_match<Opnd>, Opnd>(m_AnyExt(Op), Op);
}

/// Match a trunc or identity.
///
/// Allows to peek through optional truncations.
///
/// \param Op Nested pattern matched directly or under TRUNCATE.
/// \return A matcher for a trunc or identity.
template <typename Opnd>
inline Or<UnaryOpc_match<Opnd>, Opnd> m_TruncOrSelf(const Opnd &Op) {
  return Or<UnaryOpc_match<Opnd>, Opnd>(m_Trunc(Op), Op);
}

/// Match VSCALE of \p Op.
///
/// \param Op Sub-pattern for the vscale operand.
/// \return A matcher for VSCALE of \p Op.
template <typename Opnd> inline UnaryOpc_match<Opnd> m_VScale(const Opnd &Op) {
  return UnaryOpc_match<Opnd>(ISD::VSCALE, Op);
}

/// Match FP_TO_UINT of \p Op.
///
/// \param Op Sub-pattern for the converted operand.
/// \return A matcher for FP_TO_UINT of \p Op.
template <typename Opnd> inline UnaryOpc_match<Opnd> m_FPToUI(const Opnd &Op) {
  return UnaryOpc_match<Opnd>(ISD::FP_TO_UINT, Op);
}

/// Match FP_TO_SINT of \p Op.
///
/// \param Op Sub-pattern for the converted operand.
/// \return A matcher for FP_TO_SINT of \p Op.
template <typename Opnd> inline UnaryOpc_match<Opnd> m_FPToSI(const Opnd &Op) {
  return UnaryOpc_match<Opnd>(ISD::FP_TO_SINT, Op);
}

/// Match CTPOP of \p Op.
///
/// \param Op Sub-pattern for the population-count operand.
/// \return A matcher for CTPOP of \p Op.
template <typename Opnd> inline UnaryOpc_match<Opnd> m_Ctpop(const Opnd &Op) {
  return UnaryOpc_match<Opnd>(ISD::CTPOP, Op);
}

/// Match CTLZ of \p Op.
///
/// \param Op Sub-pattern for the leading-zero-count operand.
/// \return A matcher for CTLZ of \p Op.
template <typename Opnd> inline UnaryOpc_match<Opnd> m_Ctlz(const Opnd &Op) {
  return UnaryOpc_match<Opnd>(ISD::CTLZ, Op);
}

/// Match CTTZ of \p Op.
///
/// \param Op Sub-pattern for the trailing-zero-count operand.
/// \return A matcher for CTTZ of \p Op.
template <typename Opnd> inline UnaryOpc_match<Opnd> m_Cttz(const Opnd &Op) {
  return UnaryOpc_match<Opnd>(ISD::CTTZ, Op);
}

/// Match FNEG of \p Op.
///
/// \param Op Sub-pattern for the negated operand.
/// \return A matcher for FNEG of \p Op.
template <typename Opnd> inline UnaryOpc_match<Opnd> m_FNeg(const Opnd &Op) {
  return UnaryOpc_match<Opnd>(ISD::FNEG, Op);
}

/// Match VECTOR_REVERSE of \p Op.
///
/// \param Op Sub-pattern for the reversed vector operand.
/// \return A matcher for VECTOR_REVERSE of \p Op.
template <typename Opnd>
inline UnaryOpc_match<Opnd> m_VectorReverse(const Opnd &Op) {
  return UnaryOpc_match<Opnd>(ISD::VECTOR_REVERSE, Op);
}

// === Constants ===
/// Matcher for an integer constant or constant splat, optionally binding it.
struct ConstantInt_match {
  /// Optional output that receives the matched APInt, or null to only test.
  APInt *BindVal;

  /// Construct a constant matcher that optionally binds into \p V.
  ///
  /// \param V Output for the matched APInt, or null to leave unbound.
  explicit ConstantInt_match(APInt *V) : BindVal(V) {}

  /// Match \p N as an integer constant or splat, optionally binding BindVal.
  ///
  /// \param Ctx Unused match context.
  /// \param N Value to match.
  /// \return True if N is a constant integer or constant splat.
  template <typename MatchContext>
  bool match(const MatchContext &Ctx, SDValue N) {
    (void)Ctx;
    // The logics here are similar to that in
    // SelectionDAG::isConstantIntBuildVectorOrConstantInt, but the latter also
    // treats GlobalAddressSDNode as a constant, which is difficult to turn into
    // APInt.
    if (auto *C = dyn_cast_or_null<ConstantSDNode>(N.getNode())) {
      if (BindVal)
        *BindVal = C->getAPIntValue();
      return true;
    }

    APInt Discard;
    return ISD::isConstantSplatVector(N.getNode(),
                                      BindVal ? *BindVal : Discard);
  }
};

/// Matcher for a 64-bit integer constant or splat bound into type T.
template <typename T> struct Constant64_match {
  static_assert(sizeof(T) == 8, "T must be 64 bits wide");

  /// Output that receives the matched 64-bit constant.
  T &BindVal;

  /// Construct a matcher that binds into \p V.
  ///
  /// \param V Output that receives the matched constant.
  explicit Constant64_match(T &V) : BindVal(V) {}

  /// Match \p N as a 64-bit-fitting integer constant or splat.
  ///
  /// \param Ctx Match context passed to ConstantInt_match.
  /// \param N Value to match.
  /// \return True if a fitting constant was bound into BindVal.
  template <typename MatchContext>
  bool match(const MatchContext &Ctx, SDValue N) {
    APInt V;
    if (!ConstantInt_match(&V).match(Ctx, N))
      return false;

    if constexpr (std::is_signed_v<T>) {
      if (std::optional<int64_t> TrySExt = V.trySExtValue()) {
        BindVal = *TrySExt;
        return true;
      }
    }

    if constexpr (std::is_unsigned_v<T>) {
      if (std::optional<uint64_t> TryZExt = V.tryZExtValue()) {
        BindVal = *TryZExt;
        return true;
      }
    }

    return false;
  }
};

/// Match any integer constants or splat of an integer constant.
///
/// \return A matcher for any integer constants or splat of an integer constant.
inline ConstantInt_match m_ConstInt() { return ConstantInt_match(nullptr); }
/// Match any integer constants or splat of an integer constant; return the
/// specific constant or constant splat value.
///
/// \param V Output that receives the matched APInt.
/// \return A matcher that binds a constant integer or splat into \p V.
inline ConstantInt_match m_ConstInt(APInt &V) { return ConstantInt_match(&V); }
/// Match a 64-bit-fitting integer constant or splat, zero-extended.
///
/// Match any integer constants or splat of an integer constant that can fit in
/// 64 bits; return the specific constant or constant splat value, zero-extended
/// to 64 bits.
///
/// \param V Output that receives the zero-extended constant.
/// \return A matcher for a 64-bit-fitting integer constant or splat, zero-extended.
inline Constant64_match<uint64_t> m_ConstInt(uint64_t &V) {
  return Constant64_match<uint64_t>(V);
}
/// Match a 64-bit-fitting integer constant or splat, sign-extended.
///
/// Match any integer constants or splat of an integer constant that can fit in
/// 64 bits; return the specific constant or constant splat value, sign-extended
/// to 64 bits.
///
/// \param V Output that receives the sign-extended constant.
/// \return A matcher for a 64-bit-fitting integer constant or splat, sign-extended.
inline Constant64_match<int64_t> m_ConstInt(int64_t &V) {
  return Constant64_match<int64_t>(V);
}

template <typename T0_P, typename T1_P, typename T2_P, bool Left>
template <typename MatchContext>
bool FunnelShiftLike_match<T0_P, T1_P, T2_P, Left>::matchShiftOr(
    const MatchContext &Ctx, SDValue N, unsigned BitWidth) {
  SDValue X, Y, ShlAmt, SrlAmt;
  APInt ShlConst, SrlConst;
  if (!sd_context_match(
          N, Ctx,
          m_Or(m_Shl(m_Value(X), m_Value(ShlAmt, m_ConstInt(ShlConst))),
               m_Srl(m_Value(Y), m_Value(SrlAmt, m_ConstInt(SrlConst))))) ||
      !hasComplementaryConstantShifts(ShlConst, SrlConst, BitWidth))
    return false;

  return matchOperands(Ctx, X, Y, Left ? ShlAmt : SrlAmt);
}

/// Matcher for a specific integer constant or constant splat value.
struct SpecificInt_match {
  /// Exact integer value that must match.
  APInt IntVal;

  /// Construct a matcher for the specific integer \p APV.
  ///
  /// \param APV Exact integer value that must match.
  explicit SpecificInt_match(APInt APV) : IntVal(std::move(APV)) {}

  /// Match \p N if it is the constant IntVal or an equivalent splat.
  ///
  /// \param Ctx Match context passed to m_ConstInt.
  /// \param N Value to match.
  /// \return True if the constant equals IntVal.
  template <typename MatchContext>
  bool match(const MatchContext &Ctx, SDValue N) {
    APInt ConstInt;
    if (sd_context_match(N, Ctx, m_ConstInt(ConstInt)))
      return APInt::isSameValue(IntVal, ConstInt);
    return false;
  }
};

/// Match a specific integer constant or constant splat value.
///
/// \param V Exact APInt value that must match.
/// \return A matcher for a specific integer constant or constant splat value.
inline SpecificInt_match m_SpecificInt(APInt V) {
  return SpecificInt_match(std::move(V));
}
/// Match a specific integer constant encoded as a 64-bit value.
///
/// \param V Exact integer value that must match.
/// \return A matcher for a specific integer constant encoded as a 64-bit value.
inline SpecificInt_match m_SpecificInt(uint64_t V) {
  return SpecificInt_match(APInt(64, V));
}

/// Matcher for a specific floating-point constant or splat.
struct SpecificFP_match {
  /// Exact floating-point value that must match.
  APFloat Val;

  /// Construct a matcher for the specific float \p V.
  ///
  /// \param V Exact floating-point value that must match.
  explicit SpecificFP_match(APFloat V) : Val(V) {}

  /// Match \p V if it is the floating-point constant Val or an equivalent splat.
  ///
  /// \param Ctx Unused match context.
  /// \param V Value to match.
  /// \return True if the floating-point constant equals Val.
  template <typename MatchContext>
  bool match(const MatchContext &Ctx, SDValue V) {
    (void)Ctx;
    if (const auto *CFP = dyn_cast<ConstantFPSDNode>(V.getNode()))
      return CFP->isExactlyValue(Val);
    if (ConstantFPSDNode *C = isConstOrConstSplatFP(V, /*AllowUndefs=*/true))
      return C->getValueAPF().compare(Val) == APFloat::cmpEqual;
    return false;
  }
};

/// Match a specific float constant.
///
/// \param V Exact floating-point value that must match.
/// \return A matcher for a specific float constant.
inline SpecificFP_match m_SpecificFP(APFloat V) { return SpecificFP_match(V); }

/// Match a specific float constant encoded as a double.
///
/// \param V Exact floating-point value that must match.
/// \return A matcher for a specific float constant encoded as a double.
inline SpecificFP_match m_SpecificFP(double V) {
  return SpecificFP_match(APFloat(V));
}

/// Matcher for a floating-point +0.0 or -0.0 constant or splat.
struct AnyZeroFP_match {
  /// Match \p N if it is a floating-point zero constant or splat.
  ///
  /// \param Ctx Unused match context.
  /// \param N Value to match.
  /// \return True if N is +0.0 or -0.0.
  template <typename MatchContext>
  bool match(const MatchContext &Ctx, SDValue N) {
    (void)Ctx;
    if (ConstantFPSDNode *C = isConstOrConstSplatFP(N))
      return C->isZero();
    return false;
  }
};

/// Match a floating-point +0.0 or -0.0 constant or splat.
///
/// \return A matcher for a floating-point +0.0 or -0.0 constant or splat.
inline AnyZeroFP_match m_AnyZeroFP() { return AnyZeroFP_match(); }

/// Matcher for values known to be negative.
struct Negative_match {
  /// Match \p N if known bits prove it is negative.
  ///
  /// \param Ctx Match context providing the SelectionDAG.
  /// \param N Value to match.
  /// \return True if N is known negative.
  template <typename MatchContext>
  bool match(const MatchContext &Ctx, SDValue N) {
    const SelectionDAG *DAG = Ctx.getDAG();
    return DAG && DAG->computeKnownBits(N).isNegative();
  }
};

/// Matcher for values known to be non-negative.
struct NonNegative_match {
  /// Match \p N if known bits prove it is non-negative.
  ///
  /// \param Ctx Match context providing the SelectionDAG.
  /// \param N Value to match.
  /// \return True if N is known non-negative.
  template <typename MatchContext>
  bool match(const MatchContext &Ctx, SDValue N) {
    const SelectionDAG *DAG = Ctx.getDAG();
    return DAG && DAG->computeKnownBits(N).isNonNegative();
  }
};

/// Matcher for values known to be strictly positive.
struct StrictlyPositive_match {
  /// Match \p N if known bits prove it is strictly positive.
  ///
  /// \param Ctx Match context providing the SelectionDAG.
  /// \param N Value to match.
  /// \return True if N is known strictly positive.
  template <typename MatchContext>
  bool match(const MatchContext &Ctx, SDValue N) {
    const SelectionDAG *DAG = Ctx.getDAG();
    return DAG && DAG->computeKnownBits(N).isStrictlyPositive();
  }
};

/// Matcher for values known to be non-positive.
struct NonPositive_match {
  /// Match \p N if known bits prove it is non-positive.
  ///
  /// \param Ctx Match context providing the SelectionDAG.
  /// \param N Value to match.
  /// \return True if N is known non-positive.
  template <typename MatchContext>
  bool match(const MatchContext &Ctx, SDValue N) {
    const SelectionDAG *DAG = Ctx.getDAG();
    return DAG && DAG->computeKnownBits(N).isNonPositive();
  }
};

/// Matcher for values known to be non-zero.
struct NonZero_match {
  /// Match \p N if known bits prove it is non-zero.
  ///
  /// \param Ctx Match context providing the SelectionDAG.
  /// \param N Value to match.
  /// \return True if N is known non-zero.
  template <typename MatchContext>
  bool match(const MatchContext &Ctx, SDValue N) {
    const SelectionDAG *DAG = Ctx.getDAG();
    return DAG && DAG->computeKnownBits(N).isNonZero();
  }
};

/// Matcher for an integer zero constant or zero splat.
struct Zero_match {
  /// Whether undef elements are allowed in a splat.
  bool AllowUndefs;

  /// Construct a zero matcher.
  ///
  /// \param AllowUndefs Whether undef splat lanes are allowed.
  explicit Zero_match(bool AllowUndefs) : AllowUndefs(AllowUndefs) {}

  /// Match \p N as zero or a zero splat.
  ///
  /// \param Ctx Unused match context.
  /// \param N Value to match.
  /// \return True if N is zero (optionally allowing undef lanes).
  template <typename MatchContext>
  bool match(const MatchContext &Ctx, SDValue N) const {
    (void)Ctx;
    return isZeroOrZeroSplat(N, AllowUndefs);
  }
};

/// Matcher for an integer ones constant or ones splat.
struct Ones_match {
  /// Whether undef elements are allowed in a splat.
  bool AllowUndefs;

  /// Construct a ones matcher.
  ///
  /// \param AllowUndefs Whether undef splat lanes are allowed.
  Ones_match(bool AllowUndefs) : AllowUndefs(AllowUndefs) {}

  /// Match \p N as ones or a ones splat.
  ///
  /// \param Ctx Unused match context.
  /// \param N Value to match.
  /// \return True if N is ones (optionally allowing undef lanes).
  template <typename MatchContext>
  bool match(const MatchContext &Ctx, SDValue N) {
    (void)Ctx;
    return isOnesOrOnesSplat(N, AllowUndefs);
  }
};

/// Matcher for an all-ones constant or all-ones splat.
struct AllOnes_match {
  /// Whether undef elements are allowed in a splat.
  bool AllowUndefs;

  /// Construct an all-ones matcher.
  ///
  /// \param AllowUndefs Whether undef splat lanes are allowed.
  AllOnes_match(bool AllowUndefs) : AllowUndefs(AllowUndefs) {}

  /// Match \p N as all-ones or an all-ones splat.
  ///
  /// \param Ctx Unused match context.
  /// \param N Value to match.
  /// \return True if N is all-ones (optionally allowing undef lanes).
  template <typename MatchContext>
  bool match(const MatchContext &Ctx, SDValue N) {
    (void)Ctx;
    return isAllOnesOrAllOnesSplat(N, AllowUndefs);
  }
};

/// Match a value known to be negative.
///
/// \return A matcher for a value known to be negative.
inline Negative_match m_Negative() { return Negative_match(); }
/// Match \p P only when the value is known negative.
///
/// \param P Nested pattern applied after the known-bits check.
/// \return A matcher for \p P only when the value is known negative.
template <typename Pattern> inline auto m_Negative(const Pattern &P) {
  return m_AllOf(m_Negative(), P);
}
/// Match a value known to be non-negative.
///
/// \return A matcher for a value known to be non-negative.
inline NonNegative_match m_NonNegative() { return NonNegative_match(); }
/// Match \p P only when the value is known non-negative.
///
/// \param P Nested pattern applied after the known-bits check.
/// \return A matcher for \p P only when the value is known non-negative.
template <typename Pattern> inline auto m_NonNegative(const Pattern &P) {
  return m_AllOf(m_NonNegative(), P);
}
/// Match a value known to be strictly positive.
///
/// \return A matcher for a value known to be strictly positive.
inline StrictlyPositive_match m_StrictlyPositive() {
  return StrictlyPositive_match();
}
/// Match \p P only when the value is known strictly positive.
///
/// \param P Nested pattern applied after the known-bits check.
/// \return A matcher for \p P only when the value is known strictly positive.
template <typename Pattern> inline auto m_StrictlyPositive(const Pattern &P) {
  return m_AllOf(m_StrictlyPositive(), P);
}
/// Match a value known to be non-positive.
///
/// \return A matcher for a value known to be non-positive.
inline NonPositive_match m_NonPositive() { return NonPositive_match(); }
/// Match \p P only when the value is known non-positive.
///
/// \param P Nested pattern applied after the known-bits check.
/// \return A matcher for \p P only when the value is known non-positive.
template <typename Pattern> inline auto m_NonPositive(const Pattern &P) {
  return m_AllOf(m_NonPositive(), P);
}
/// Match a value known to be non-zero.
///
/// \return A matcher for a value known to be non-zero.
inline NonZero_match m_NonZero() { return NonZero_match(); }
/// Match \p P only when the value is known non-zero.
///
/// \param P Nested pattern applied after the known-bits check.
/// \return A matcher for \p P only when the value is known non-zero.
template <typename Pattern> inline auto m_NonZero(const Pattern &P) {
  return m_AllOf(m_NonZero(), P);
}
/// Match an integer ones constant or ones splat.
///
/// \param AllowUndefs Whether undef splat lanes are allowed.
/// \return A matcher for an integer ones constant or ones splat.
inline Ones_match m_One(bool AllowUndefs = false) {
  return Ones_match(AllowUndefs);
}
/// Match an integer zero constant or zero splat.
///
/// \param AllowUndefs Whether undef splat lanes are allowed.
/// \return A matcher for an integer zero constant or zero splat.
inline Zero_match m_Zero(bool AllowUndefs = false) {
  return Zero_match(AllowUndefs);
}
/// Match an all-ones constant or all-ones splat.
///
/// \param AllowUndefs Whether undef splat lanes are allowed.
/// \return A matcher for an all-ones constant or all-ones splat.
inline AllOnes_match m_AllOnes(bool AllowUndefs = false) {
  return AllOnes_match(AllowUndefs);
}

/// Match true boolean value based on the information provided by
/// TargetLowering.
///
/// \return A matcher for a true boolean value per TargetLowering.
inline auto m_True() {
  return TLI_pred_match{
      [](const TargetLowering &TLI, SDValue N) {
        APInt ConstVal;
        if (sd_match(N, m_ConstInt(ConstVal)))
          switch (TLI.getBooleanContents(N.getValueType())) {
          case TargetLowering::ZeroOrOneBooleanContent:
            return ConstVal.isOne();
          case TargetLowering::ZeroOrNegativeOneBooleanContent:
            return ConstVal.isAllOnes();
          case TargetLowering::UndefinedBooleanContent:
            return (ConstVal & 0x01) == 1;
          }

        return false;
      },
      m_Value()};
}
/// Match false boolean value based on the information provided by
/// TargetLowering.
///
/// \return A matcher for a false boolean value per TargetLowering.
inline auto m_False() {
  return TLI_pred_match{
      [](const TargetLowering &TLI, SDValue N) {
        APInt ConstVal;
        if (sd_match(N, m_ConstInt(ConstVal)))
          switch (TLI.getBooleanContents(N.getValueType())) {
          case TargetLowering::ZeroOrOneBooleanContent:
          case TargetLowering::ZeroOrNegativeOneBooleanContent:
            return ConstVal.isZero();
          case TargetLowering::UndefinedBooleanContent:
            return (ConstVal & 0x01) == 0;
          }

        return false;
      },
      m_Value()};
}

/// Matcher for a CondCodeSDNode, optionally binding or constraining its code.
struct CondCode_match {
  /// Exact condition code required when set.
  std::optional<ISD::CondCode> CCToMatch;
  /// Optional output that receives the matched condition code.
  ISD::CondCode *BindCC = nullptr;

  /// Construct a matcher for the specific condition code \p CC.
  ///
  /// \param CC Exact condition code that must match.
  explicit CondCode_match(ISD::CondCode CC) : CCToMatch(CC) {}

  /// Construct a matcher that binds the condition code into \p CC.
  ///
  /// \param CC Output that receives the matched condition code, or null.
  explicit CondCode_match(ISD::CondCode *CC) : BindCC(CC) {}

  /// Match \p N as a CondCodeSDNode, optionally binding or checking CCToMatch.
  ///
  /// \param Ctx Unused match context.
  /// \param N Value to match.
  /// \return True if N is an acceptable CondCodeSDNode.
  template <typename MatchContext>
  bool match(const MatchContext &Ctx, SDValue N) {
    (void)Ctx;
    if (auto *CC = dyn_cast<CondCodeSDNode>(N.getNode())) {
      if (CCToMatch && *CCToMatch != CC->get())
        return false;

      if (BindCC)
        *BindCC = CC->get();
      return true;
    }

    return false;
  }
};

/// Match any conditional code SDNode.
///
/// \return A matcher for any conditional code SDNode.
inline CondCode_match m_CondCode() { return CondCode_match(nullptr); }
/// Match any conditional code SDNode and return its ISD::CondCode value.
///
/// \param CC Output that receives the matched condition code.
/// \return A matcher that binds a CondCodeSDNode's code into \p CC.
inline CondCode_match m_CondCode(ISD::CondCode &CC) {
  return CondCode_match(&CC);
}
/// Match a conditional code SDNode with a specific ISD::CondCode.
///
/// \param CC Exact condition code that must match.
/// \return A matcher for a conditional code SDNode with a specific ISD::CondCode.
inline CondCode_match m_SpecificCondCode(ISD::CondCode CC) {
  return CondCode_match(CC);
}

/// Match a negate as a sub(0, v)
///
/// \param V Sub-pattern for the negated operand.
/// \return A matcher for a negate as a sub(0, v).
template <typename ValTy>
inline BinaryOpc_match<Zero_match, ValTy, false> m_Neg(const ValTy &V) {
  return m_Sub(m_Zero(), V);
}

/// Match a Not as a xor(v, -1) or xor(-1, v)
///
/// \param V Sub-pattern for the bitwise-not operand.
/// \return A matcher for a Not as a xor(v, -1) or xor(-1, v).
template <typename ValTy>
inline BinaryOpc_match<ValTy, AllOnes_match, true> m_Not(const ValTy &V) {
  return m_Xor(V, m_AllOnes());
}

/// Match INTRINSIC_WO_CHAIN for IntrinsicId with operands \p Opnds.
///
/// \param Opnds Operand predicates following the intrinsic ID.
/// \return A matcher for INTRINSIC_WO_CHAIN for IntrinsicId with operands \p Opnds.
template <unsigned IntrinsicId, typename... OpndPreds>
inline auto m_IntrinsicWOChain(const OpndPreds &...Opnds) {
  return m_Node(ISD::INTRINSIC_WO_CHAIN, m_SpecificInt(IntrinsicId), Opnds...);
}

/// Matcher for the negation of a specific SDValue.
struct SpecificNeg_match {
  /// Specific value whose negation must match.
  SDValue V;

  /// Construct a matcher for the negation of \p V.
  ///
  /// \param V Specific value whose negation must match.
  explicit SpecificNeg_match(SDValue V) : V(V) {}

  /// Match \p N as sub(0, V) or as constants that negate V's constants.
  ///
  /// \param Ctx Match context used for nested matching.
  /// \param N Value to match.
  /// \return True if N is the negation of V.
  template <typename MatchContext>
  bool match(const MatchContext &Ctx, SDValue N) {
    if (sd_context_match(N, Ctx, m_Neg(m_Specific(V))))
      return true;

    return ISD::matchBinaryPredicate(
        V, N, [](ConstantSDNode *LHS, ConstantSDNode *RHS) {
          return LHS->getAPIntValue() == -RHS->getAPIntValue();
        });
  }
};

/// Match a negation of a specific value V, either as sub(0, V) or as
/// constant(s) that are the negation of V's constant(s).
///
/// \param V Specific value whose negation must match.
/// \return A matcher for a negation of a specific value V, either as sub(0, V) or as.
inline SpecificNeg_match m_SpecificNeg(SDValue V) {
  return SpecificNeg_match(V);
}

/// Matcher for a reassociatable opcode tree against an unordered leaf pattern pack.
template <typename... PatternTs> struct ReassociatableOpc_match {
  /// Opcode whose tree may be reassociated while collecting leaves.
  unsigned Opcode;
  /// Unordered leaf patterns that must each match one collected leaf.
  std::tuple<PatternTs...> Patterns;
  /// Number of leaf patterns in Patterns.
  constexpr static size_t NumPatterns =
      std::tuple_size_v<std::tuple<PatternTs...>>;

  /// Node flags required on every Opcode node in the tree.
  SDNodeFlags Flags;

  /// Construct a reassociatable matcher for \p Opcode and \p Patterns.
  ///
  /// \param Opcode Opcode whose tree may be flattened.
  /// \param Patterns Leaf patterns matched in any order.
  ReassociatableOpc_match(unsigned Opcode, const PatternTs &...Patterns)
      : Opcode(Opcode), Patterns(Patterns...) {}

  /// Construct a reassociatable matcher with required \p Flags.
  ///
  /// \param Opcode Opcode whose tree may be flattened.
  /// \param Flags Node flags required on Opcode nodes.
  /// \param Patterns Leaf patterns matched in any order.
  ReassociatableOpc_match(unsigned Opcode, SDNodeFlags Flags,
                          const PatternTs &...Patterns)
      : Opcode(Opcode), Patterns(Patterns...), Flags(Flags) {}

  /// Match \p N as a reassociatable Opcode tree whose leaves match Patterns.
  ///
  /// \param Ctx Match context passed to leaf patterns.
  /// \param N Value to match.
  /// \return True if leaves can be matched one-to-one with Patterns.
  template <typename MatchContext>
  bool match(const MatchContext &Ctx, SDValue N) {
    std::array<SDValue, NumPatterns> Leaves;
    size_t LeavesIdx = 0;
    if (!(collectLeaves(N, Leaves, LeavesIdx) && (LeavesIdx == NumPatterns)))
      return false;

    Bitset<NumPatterns> Used;
    return std::apply(
        [&](auto &...P) -> bool {
          return reassociatableMatchHelper(Ctx, Leaves, Used, P...);
        },
        Patterns);
  }

  /// Flatten Opcode nodes under \p V into \p Leaves starting at \p LeafIdx.
  ///
  /// \param V Root of the subtree to flatten.
  /// \param Leaves Output array that receives collected leaves.
  /// \param LeafIdx Next free index in Leaves.
  /// \return False if too many leaves are found.
  bool collectLeaves(SDValue V, std::array<SDValue, NumPatterns> &Leaves,
                     std::size_t &LeafIdx) {
    if (V->getOpcode() == Opcode && (Flags & V->getFlags()) == Flags) {
      for (size_t I = 0, N = V->getNumOperands(); I < N; I++)
        if ((LeafIdx == NumPatterns) ||
            !collectLeaves(V->getOperand(I), Leaves, LeafIdx))
          return false;
    } else {
      Leaves[LeafIdx] = V;
      LeafIdx++;
    }
    return true;
  }

  // Searchs for a matching leaf for every sub-pattern.
  /// Assign an unused leaf to \p HeadPattern, then recurse on \p TailPatterns.
  ///
  /// \param Ctx Match context passed to leaf patterns.
  /// \param Leaves Collected leaves from the Opcode tree.
  /// \param Used Bitset of leaves already assigned to a pattern.
  /// \param HeadPattern Next leaf pattern to assign.
  /// \param TailPatterns Remaining leaf patterns.
  /// \return True if every pattern can be assigned a distinct matching leaf.
  template <typename MatchContext, typename PatternHd, typename... PatternTl>
  [[nodiscard]] inline bool
  reassociatableMatchHelper(const MatchContext &Ctx, ArrayRef<SDValue> Leaves,
                            Bitset<NumPatterns> &Used, PatternHd &HeadPattern,
                            PatternTl &...TailPatterns) {
    for (size_t Match = 0, N = Used.size(); Match < N; Match++) {
      if (Used[Match] || !(sd_context_match(Leaves[Match], Ctx, HeadPattern)))
        continue;
      Used.set(Match);
      if (reassociatableMatchHelper(Ctx, Leaves, Used, TailPatterns...))
        return true;
      Used.reset(Match);
    }
    return false;
  }

  /// Base case: every leaf pattern has already been assigned.
  ///
  /// \param Ctx Unused match context.
  /// \param Leaves Collected leaves from the Opcode tree.
  /// \param Used Bitset of leaves already assigned to a pattern.
  /// \return Always true.
  template <typename MatchContext>
  [[nodiscard]] inline bool
  reassociatableMatchHelper(const MatchContext &Ctx, ArrayRef<SDValue> Leaves,
                            Bitset<NumPatterns> &Used) {
    (void)Ctx;
    (void)Leaves;
    (void)Used;
    return true;
  }
};

/// Match a reassociatable ADD tree whose leaves match \p Patterns in any order.
///
/// \param Patterns Leaf patterns matched against the flattened ADD tree.
/// \return A matcher for a reassociatable ADD tree whose leaves match \p Patterns in any order.
template <typename... PatternTs>
inline ReassociatableOpc_match<PatternTs...>
m_ReassociatableAdd(const PatternTs &...Patterns) {
  return ReassociatableOpc_match<PatternTs...>(ISD::ADD, Patterns...);
}

/// Match a reassociatable OR tree whose leaves match \p Patterns in any order.
///
/// \param Patterns Leaf patterns matched against the flattened OR tree.
/// \return A matcher for a reassociatable OR tree whose leaves match \p Patterns in any order.
template <typename... PatternTs>
inline ReassociatableOpc_match<PatternTs...>
m_ReassociatableOr(const PatternTs &...Patterns) {
  return ReassociatableOpc_match<PatternTs...>(ISD::OR, Patterns...);
}

/// Match a reassociatable AND tree whose leaves match \p Patterns in any order.
///
/// \param Patterns Leaf patterns matched against the flattened AND tree.
/// \return A matcher for a reassociatable AND tree whose leaves match \p Patterns in any order.
template <typename... PatternTs>
inline ReassociatableOpc_match<PatternTs...>
m_ReassociatableAnd(const PatternTs &...Patterns) {
  return ReassociatableOpc_match<PatternTs...>(ISD::AND, Patterns...);
}

/// Match a reassociatable MUL tree whose leaves match \p Patterns in any order.
///
/// \param Patterns Leaf patterns matched against the flattened MUL tree.
/// \return A matcher for a reassociatable MUL tree whose leaves match \p Patterns in any order.
template <typename... PatternTs>
inline ReassociatableOpc_match<PatternTs...>
m_ReassociatableMul(const PatternTs &...Patterns) {
  return ReassociatableOpc_match<PatternTs...>(ISD::MUL, Patterns...);
}

/// Match a reassociatable NSW ADD tree whose leaves match \p Patterns.
///
/// \param Patterns Leaf patterns matched against the flattened ADD tree.
/// \return A matcher for a reassociatable NSW ADD tree whose leaves match \p Patterns.
template <typename... PatternTs>
inline ReassociatableOpc_match<PatternTs...>
m_ReassociatableNSWAdd(const PatternTs &...Patterns) {
  return ReassociatableOpc_match<PatternTs...>(
      ISD::ADD, SDNodeFlags::NoSignedWrap, Patterns...);
}

/// Match a reassociatable NUW ADD tree whose leaves match \p Patterns.
///
/// \param Patterns Leaf patterns matched against the flattened ADD tree.
/// \return A matcher for a reassociatable NUW ADD tree whose leaves match \p Patterns.
template <typename... PatternTs>
inline ReassociatableOpc_match<PatternTs...>
m_ReassociatableNUWAdd(const PatternTs &...Patterns) {
  return ReassociatableOpc_match<PatternTs...>(
      ISD::ADD, SDNodeFlags::NoUnsignedWrap, Patterns...);
}

} // namespace SDPatternMatch
} // namespace llvm
#endif
