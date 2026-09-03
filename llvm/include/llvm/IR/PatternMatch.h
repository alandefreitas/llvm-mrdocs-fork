//===- PatternMatch.h - Match on the LLVM IR --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides a simple and efficient mechanism for performing general
// tree-based pattern matches on the LLVM IR. The power of these routines is
// that it allows you to write concise patterns that are expressive and easy to
// understand. The other major advantage of this is that it allows you to
// trivially capture/bind elements in the pattern to variables. For example,
// you can do something like this:
//
//  Value *Exp = ...
//  Value *X, *Y;  ConstantInt *C1, *C2;      // (X & C1) | (Y & C2)
//  if (match(Exp, m_Or(m_And(m_Value(X), m_ConstantInt(C1)),
//                      m_And(m_Value(Y), m_ConstantInt(C2))))) {
//    ... Pattern is matched and variables are bound ...
//  }
//
// This is primarily useful to things like the instruction combiner, but can
// also be useful for static analysis tools or code generators.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_PATTERNMATCH_H
#define LLVM_IR_PATTERNMATCH_H

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/FMF.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/PatternMatchHelpers.h"
#include <cstdint>
#include <utility>

namespace llvm {
/// Pattern-matching helpers for LLVM IR values and instructions.
namespace PatternMatch {

using namespace llvm::PatternMatchHelpers;

/// Match value \p V against pattern \p P.
/// \param V The value to match.
/// \param P The pattern matcher to apply.
/// \return True if the match succeeds.
template <typename Val, typename Pattern> bool match(Val *V, const Pattern &P) {
  return P.match(V);
}

/// A match functor that can be used as a UnaryPredicate in functional
/// algorithms like all_of.
/// \param P The pattern matcher to bind.
/// \return A unary predicate that applies the pattern matcher \p P.
template <typename Val = const Value, typename Pattern>
auto match_fn(const Pattern &P) {
  return bind_back<match<Val, Pattern>>(P);
}

/// Match shuffle mask \p Mask against pattern \p P.
/// \param Mask The shuffle mask to match.
/// \param P The pattern matcher to apply.
/// \return True if the match succeeds.
template <typename Pattern> bool match(ArrayRef<int> Mask, const Pattern &P) {
  return P.match(Mask);
}

/// Matcher that requires the value to have exactly one use.
template <typename SubPattern_t> struct OneUse_match {
  /// Nested pattern that must also match.
  SubPattern_t SubPattern;

  /// Construct a one-use matcher wrapping \p SP.
  /// \param SP The nested pattern to match.
  OneUse_match(const SubPattern_t &SP) : SubPattern(SP) {}

  /// Match if \p V has one use and matches the nested pattern.
  /// \param V The value to match.
  /// \return True if the match succeeds.
  template <typename OpTy> bool match(OpTy *V) const {
    return V->hasOneUse() && SubPattern.match(V);
  }
};

/// Match a value with exactly one use that also matches \p SubPattern.
/// \param SubPattern The nested pattern to match.
/// \return A matcher for a value with exactly one use that also matches \p SubPattern.
template <typename T> inline OneUse_match<T> m_OneUse(const T &SubPattern) {
  return SubPattern;
}

/// Matcher that requires a fast-math flag set on an FPMathOperator.
template <typename SubPattern_t, int Flag> struct AllowFmf_match {
  /// Nested pattern that must also match.
  SubPattern_t SubPattern;
  /// Fast-math flags that must be present on the matched operator.
  FastMathFlags FMF;

  /// Construct a fast-math-flag matcher wrapping \p SP.
  /// \param SP The nested pattern to match.
  AllowFmf_match(const SubPattern_t &SP) : SubPattern(SP), FMF(Flag) {}

  /// Match if \p V is an FPMathOperator with the required flags.
  /// \param V The value to match.
  /// \return True if the match succeeds.
  template <typename OpTy> bool match(OpTy *V) const {
    auto *I = dyn_cast<FPMathOperator>(V);
    return I && ((I->getFastMathFlags() & FMF) == FMF) && SubPattern.match(I);
  }
};

/// Match an FP operator that allows reassociation and matches \p SubPattern.
/// \param SubPattern The nested pattern to match.
/// \return A matcher for an FP operator that allows reassociation and matches \p SubPattern.
template <typename T>
inline AllowFmf_match<T, FastMathFlags::AllowReassoc>
m_AllowReassoc(const T &SubPattern) {
  return SubPattern;
}

/// Match an FP operator that allows reciprocal and matches \p SubPattern.
/// \param SubPattern The nested pattern to match.
/// \return A matcher for an FP operator that allows reciprocal and matches \p SubPattern.
template <typename T>
inline AllowFmf_match<T, FastMathFlags::AllowReciprocal>
m_AllowReciprocal(const T &SubPattern) {
  return SubPattern;
}

/// Match an FP operator that allows contraction and matches \p SubPattern.
/// \param SubPattern The nested pattern to match.
/// \return A matcher for an FP operator that allows contraction and matches \p SubPattern.
template <typename T>
inline AllowFmf_match<T, FastMathFlags::AllowContract>
m_AllowContract(const T &SubPattern) {
  return SubPattern;
}

/// Match an FP operator that allows approximate functions and matches \p SubPattern.
/// \param SubPattern The nested pattern to match.
/// \return A matcher for an FP operator that allows approximate functions and matches \p SubPattern.
template <typename T>
inline AllowFmf_match<T, FastMathFlags::ApproxFunc>
m_ApproxFunc(const T &SubPattern) {
  return SubPattern;
}

/// Match an FP operator with nnan and matches \p SubPattern.
/// \param SubPattern The nested pattern to match.
/// \return A matcher for an FP operator with nnan and matches \p SubPattern.
template <typename T>
inline AllowFmf_match<T, FastMathFlags::NoNaNs> m_NoNaNs(const T &SubPattern) {
  return SubPattern;
}

/// Match an FP operator with ninf and matches \p SubPattern.
/// \param SubPattern The nested pattern to match.
/// \return A matcher for an FP operator with ninf and matches \p SubPattern.
template <typename T>
inline AllowFmf_match<T, FastMathFlags::NoInfs> m_NoInfs(const T &SubPattern) {
  return SubPattern;
}

/// Match an FP operator with nsz and matches \p SubPattern.
/// \param SubPattern The nested pattern to match.
/// \return A matcher for an FP operator with nsz and matches \p SubPattern.
template <typename T>
inline AllowFmf_match<T, FastMathFlags::NoSignedZeros>
m_NoSignedZeros(const T &SubPattern) {
  return SubPattern;
}

/// Match an arbitrary value and ignore it.
/// \return A matcher for an arbitrary value and ignore it.
inline auto m_Value() { return m_Isa<Value>(); }

/// Match an arbitrary unary operation and ignore it.
/// \return A matcher for an arbitrary unary operation and ignore it.
inline auto m_UnOp() { return m_Isa<UnaryOperator>(); }

/// Match an arbitrary binary operation and ignore it.
/// \return A matcher for an arbitrary binary operation and ignore it.
inline auto m_BinOp() { return m_Isa<BinaryOperator>(); }

/// Matches any compare instruction and ignore it.
/// \return A matcher for any compare instruction and ignore it.
inline auto m_Cmp() { return m_Isa<CmpInst>(); }

/// Matches any intrinsic call and ignore it.
/// \return A matcher for any intrinsic call and ignore it.
inline auto m_AnyIntrinsic() { return m_Isa<IntrinsicInst>(); }

/// Matcher for undef and poison constants, including all-undef aggregates.
struct undef_match {
private:
  LLVM_ABI static bool checkAggregate(const ConstantAggregate *CA);

public:
  /// Return true if \p V is undef, poison, or an all-undef/poison aggregate.
  /// \param V The value to check.
  /// \return True if the value is undef, poison, or an all-undef/poison aggregate.
  static bool check(const Value *V) {
    if (isa<UndefValue>(V))
      return true;
    if (const auto *CA = dyn_cast<ConstantAggregate>(V))
      return checkAggregate(CA);
    return false;
  }
  /// Match if \p V is an undef or poison constant.
  /// \param V The value to match.
  /// \return True if the match succeeds.
  template <typename ITy> bool match(ITy *V) const { return check(V); }
};

/// Match an arbitrary undef constant.
///
/// This matches poison as well. If this is an aggregate and contains a
/// non-aggregate element that is neither undef nor poison, the aggregate is
/// not matched.
/// \return A matcher for an arbitrary undef constant.
inline auto m_Undef() { return undef_match(); }

/// Match an arbitrary UndefValue constant.
/// \return A matcher for an arbitrary UndefValue constant.
inline auto m_UndefValue() { return m_Isa<UndefValue>(); }

/// Match an arbitrary poison constant.
/// \return A matcher for an arbitrary poison constant.
inline auto m_Poison() { return m_Isa<PoisonValue>(); }

/// Match an arbitrary Constant and ignore it.
/// \return A matcher for an arbitrary Constant and ignore it.
inline auto m_Constant() { return m_Isa<Constant>(); }

/// Match an arbitrary ConstantInt and ignore it.
/// \return A matcher for an arbitrary ConstantInt and ignore it.
inline auto m_ConstantInt() { return m_Isa<ConstantInt>(); }

/// Match an arbitrary ConstantFP and ignore it.
/// \return A matcher for an arbitrary ConstantFP and ignore it.
inline auto m_ConstantFP() { return m_Isa<ConstantFP>(); }

/// Matcher for a vector constant with at least one matching element.
template <typename SPTy> struct ContainsMatchingVectorElement_match {
  /// Nested pattern matched against vector elements.
  SPTy SubPattern;
  /// Construct a matcher wrapping \p SP.
  /// \param SP The nested pattern to match against elements.
  ContainsMatchingVectorElement_match(const SPTy &SP) : SubPattern(SP) {}

  /// Match if any fixed-vector element of \p V matches the nested pattern.
  /// \param V The value to match.
  /// \return True if the match succeeds.
  template <typename ITy> bool match(ITy *V) const {
    auto *C = dyn_cast<Constant>(V);
    return C && C->containsMatchingVectorElement(
                    [&](Constant *E) { return SubPattern.match(E); });
  }
};

/// Match a vector constant with an element matching \p SubPattern.
///
/// Scalable vector constants are not matched. Any bindings in the subpattern
/// will be bound to the first match.
/// \param SubPattern The nested pattern to match against elements.
/// \return A matcher for a vector constant with an element matching \p SubPattern.
template <typename SPTy>
inline ContainsMatchingVectorElement_match<SPTy>
m_ContainsMatchingVectorElement(const SPTy &SubPattern) {
  return SubPattern;
}

/// Match a constant expression or a constant that contains a constant
/// expression.
/// \return A matcher for a constant expression or a constant that contains a constant expression.
inline auto m_ConstantExpr() {
  return m_CombineOr(m_Isa<ConstantExpr>(),
                     m_ContainsMatchingVectorElement(m_Isa<ConstantExpr>()));
}

/// Matcher for a splat constant whose splat value matches a nested pattern.
template <typename SubPattern_t> struct Splat_match {
  /// Nested pattern matched against the splat value.
  SubPattern_t SubPattern;
  /// Construct a splat matcher wrapping \p SP.
  /// \param SP The nested pattern to match.
  Splat_match(const SubPattern_t &SP) : SubPattern(SP) {}

  /// Match if \p V is a splat constant whose value matches the nested pattern.
  /// \param V The value to match.
  /// \return True if the match succeeds.
  template <typename OpTy> bool match(OpTy *V) const {
    if (auto *C = dyn_cast<Constant>(V)) {
      auto *Splat = C->getSplatValue();
      return Splat ? SubPattern.match(Splat) : false;
    }
    // TODO: Extend to other cases (e.g. shufflevectors).
    return false;
  }
};

/// Match a constant splat.
/// TODO: Extend this to non-constant splats.
/// \param SubPattern The nested pattern to match against the splat value.
/// \return A matcher for a constant splat. TODO: Extend this to non-constant splats.
template <typename T>
inline Splat_match<T> m_ConstantSplat(const T &SubPattern) {
  return SubPattern;
}

/// Match an arbitrary basic block value and ignore it.
/// \return A matcher for an arbitrary basic block value and ignore it.
inline auto m_BasicBlock() { return m_Isa<BasicBlock>(); }

/// Matcher that binds an APInt or APFloat from a constant or splat.
template <typename APTy> struct ap_match {
  static_assert(std::is_same_v<APTy, APInt> || std::is_same_v<APTy, APFloat>);
  /// Constant type corresponding to \c APTy (ConstantInt or ConstantFP).
  using ConstantTy =
      std::conditional_t<std::is_same_v<APTy, APInt>, ConstantInt, ConstantFP>;

  /// Bound pointer to the matched APInt or APFloat.
  const APTy *&Res;
  /// Whether poison is allowed in splat vector constants.
  bool AllowPoison;

  /// Construct a matcher that binds into \p Res.
  /// \param Res Reference to the pointer that receives the matched value.
  /// \param AllowPoison Whether poison is allowed in splat vectors.
  ap_match(const APTy *&Res, bool AllowPoison)
      : Res(Res), AllowPoison(AllowPoison) {}

  /// Match a scalar or splat constant and bind its AP value.
  /// \param V The value to match.
  /// \return True if the match succeeds.
  template <typename ITy> bool match(ITy *V) const {
    if (auto *CI = dyn_cast<ConstantTy>(V)) {
      Res = &CI->getValue();
      return true;
    }
    if (V->getType()->isVectorTy())
      if (const auto *C = dyn_cast<Constant>(V))
        if (auto *CI =
                dyn_cast_or_null<ConstantTy>(C->getSplatValue(AllowPoison))) {
          Res = &CI->getValue();
          return true;
        }
    return false;
  }
};

/// Match a ConstantInt or splatted ConstantVector, binding the
/// specified pointer to the contained APInt.
/// \param Res Reference to the pointer that receives the matched APInt.
/// \return A matcher for a ConstantInt or splatted ConstantVector, binding the specified pointer to the contained APInt.
inline ap_match<APInt> m_APInt(const APInt *&Res) {
  // Forbid poison by default to maintain previous behavior.
  return ap_match<APInt>(Res, /* AllowPoison */ false);
}

/// Match APInt while allowing poison in splat vector constants.
/// \param Res Reference to the pointer that receives the matched APInt.
/// \return A matcher for APInt while allowing poison in splat vector constants.
inline ap_match<APInt> m_APIntAllowPoison(const APInt *&Res) {
  return ap_match<APInt>(Res, /* AllowPoison */ true);
}

/// Match APInt while forbidding poison in splat vector constants.
/// \param Res Reference to the pointer that receives the matched APInt.
/// \return A matcher for APInt while forbidding poison in splat vector constants.
inline ap_match<APInt> m_APIntForbidPoison(const APInt *&Res) {
  return ap_match<APInt>(Res, /* AllowPoison */ false);
}

/// Match a ConstantFP or splatted ConstantVector, binding the
/// specified pointer to the contained APFloat.
/// \param Res Reference to the pointer that receives the matched APFloat.
/// \return A matcher for a ConstantFP or splatted ConstantVector, binding the specified pointer to the contained APFloat.
inline ap_match<APFloat> m_APFloat(const APFloat *&Res) {
  // Forbid undefs by default to maintain previous behavior.
  return ap_match<APFloat>(Res, /* AllowPoison */ false);
}

/// Match APFloat while allowing poison in splat vector constants.
/// \param Res Reference to the pointer that receives the matched APFloat.
/// \return A matcher for APFloat while allowing poison in splat vector constants.
inline ap_match<APFloat> m_APFloatAllowPoison(const APFloat *&Res) {
  return ap_match<APFloat>(Res, /* AllowPoison */ true);
}

/// Match APFloat while forbidding poison in splat vector constants.
/// \param Res Reference to the pointer that receives the matched APFloat.
/// \return A matcher for APFloat while forbidding poison in splat vector constants.
inline ap_match<APFloat> m_APFloatForbidPoison(const APFloat *&Res) {
  return ap_match<APFloat>(Res, /* AllowPoison */ false);
}

/// Matcher for a ConstantInt with a compile-time integer value.
template <int64_t Val> struct constantint_match {
  /// Match if \p V is a ConstantInt equal to \c Val.
  /// \param V The value to match.
  /// \return True if the match succeeds.
  template <typename ITy> bool match(ITy *V) const {
    if (const auto *CI = dyn_cast<ConstantInt>(V)) {
      const APInt &CIV = CI->getValue();
      if (Val >= 0)
        return CIV == static_cast<uint64_t>(Val);
      // If Val is negative, and CI is shorter than it, truncate to the right
      // number of bits.  If it is larger, then we have to sign extend.  Just
      // compare their negated values.
      return -CIV == -Val;
    }
    return false;
  }
};

/// Match a ConstantInt with a specific value.
/// \return A matcher for a ConstantInt with a specific value.
template <int64_t Val> inline constantint_match<Val> m_ConstantInt() {
  return constantint_match<Val>();
}

/// Match scalar, splat, or fixed-vector constants that satisfy a predicate.
/// For fixed width vector constants, poison elements are ignored if
/// AllowPoison is true.
template <typename Predicate, typename ConstantVal, bool AllowPoison>
struct cstval_pred_ty : public Predicate {
private:
  bool matchVector(const Value *V) const {
    if (const auto *C = dyn_cast<Constant>(V)) {
      if (const auto *CV = dyn_cast_or_null<ConstantVal>(C->getSplatValue()))
        return this->isValue(CV->getValue());

      // Number of elements of a scalable vector unknown at compile time
      auto *FVTy = dyn_cast<FixedVectorType>(V->getType());
      if (!FVTy)
        return false;

      // Non-splat vector constant: check each element for a match.
      unsigned NumElts = FVTy->getNumElements();
      assert(NumElts != 0 && "Constant vector with no elements?");
      bool HasNonPoisonElements = false;
      for (unsigned i = 0; i != NumElts; ++i) {
        Constant *Elt = C->getAggregateElement(i);
        if (!Elt)
          return false;
        if (AllowPoison && isa<PoisonValue>(Elt))
          continue;
        auto *CV = dyn_cast<ConstantVal>(Elt);
        if (!CV || !this->isValue(CV->getValue()))
          return false;
        HasNonPoisonElements = true;
      }
      return HasNonPoisonElements;
    }
    return false;
  }

public:
  /// Optional output that receives the matched Constant.
  const Constant **Res = nullptr;
  /// Match \p V against the predicate without binding \c Res.
  /// \param V The value to match.
  /// \return True if the constant matches the predicate.
  template <typename ITy> bool match_impl(ITy *V) const {
    if (const auto *CV = dyn_cast<ConstantVal>(V))
      return this->isValue(CV->getValue());
    if (isa<VectorType>(V->getType()))
      return matchVector(V);
    return false;
  }

  /// Match \p V against the predicate and optionally bind \c Res.
  /// \param V The value to match.
  /// \return True if the match succeeds.
  template <typename ITy> bool match(ITy *V) const {
    if (this->match_impl(V)) {
      if (Res)
        *Res = cast<Constant>(V);
      return true;
    }
    return false;
  }
};

/// specialization of cstval_pred_ty for ConstantInt
template <typename Predicate, bool AllowPoison = true>
using cst_pred_ty = cstval_pred_ty<Predicate, ConstantInt, AllowPoison>;

/// specialization of cstval_pred_ty for ConstantFP
template <typename Predicate>
using cstfp_pred_ty = cstval_pred_ty<Predicate, ConstantFP,
                                     /*AllowPoison=*/true>;

/// This helper class is used to match scalar and vector constants that
/// satisfy a specified predicate, and bind them to an APInt.
template <typename Predicate> struct api_pred_ty : public Predicate {
  /// Bound pointer to the matched APInt.
  const APInt *&Res;

  /// Construct a matcher that binds into \p R.
  /// \param R Reference to the pointer that receives the matched APInt.
  api_pred_ty(const APInt *&R) : Res(R) {}

  /// Match a scalar or splat constant satisfying the predicate and bind it.
  /// \param V The value to match.
  /// \return True if the match succeeds.
  template <typename ITy> bool match(ITy *V) const {
    if (const auto *CI = dyn_cast<ConstantInt>(V))
      if (this->isValue(CI->getValue())) {
        Res = &CI->getValue();
        return true;
      }
    if (V->getType()->isVectorTy())
      if (const auto *C = dyn_cast<Constant>(V))
        if (auto *CI = dyn_cast_or_null<ConstantInt>(
                C->getSplatValue(/*AllowPoison=*/true)))
          if (this->isValue(CI->getValue())) {
            Res = &CI->getValue();
            return true;
          }

    return false;
  }
};

/// Match scalar and vector constants satisfying a predicate, binding an APFloat.
/// Poison is allowed in splat vector constants.
template <typename Predicate> struct apf_pred_ty : public Predicate {
  /// Bound pointer to the matched APFloat.
  const APFloat *&Res;

  /// Construct a matcher that binds into \p R.
  /// \param R Reference to the pointer that receives the matched APFloat.
  apf_pred_ty(const APFloat *&R) : Res(R) {}

  /// Match a scalar or splat constant satisfying the predicate and bind it.
  /// \param V The value to match.
  /// \return True if the match succeeds.
  template <typename ITy> bool match(ITy *V) const {
    if (const auto *CI = dyn_cast<ConstantFP>(V))
      if (this->isValue(CI->getValue())) {
        Res = &CI->getValue();
        return true;
      }
    if (V->getType()->isVectorTy())
      if (const auto *C = dyn_cast<Constant>(V))
        if (auto *CI = dyn_cast_or_null<ConstantFP>(
                C->getSplatValue(/* AllowPoison */ true)))
          if (this->isValue(CI->getValue())) {
            Res = &CI->getValue();
            return true;
          }

    return false;
  }
};

// Encapsulate constant value queries for use in templated predicate matchers.
// This allows checking if constants match using compound predicates and works
// with vector constants, possibly with relaxed constraints. For example, ignore
// undef values.

/// Predicate that applies a user-provided check function to AP values.
template <typename APTy> struct custom_checkfn {
  /// User-provided predicate over the AP value.
  function_ref<bool(const APTy &)> CheckFn;
  /// Return true if \p C satisfies \c CheckFn.
  /// \param C The AP value to test.
  /// \return True if the value satisfies the predicate.
  bool isValue(const APTy &C) const { return CheckFn(C); }
};

/// Match an integer or vector where CheckFn(ele) for each element is true.
/// For vectors, poison elements are assumed to match.
/// \param CheckFn Predicate applied to each integer element.
/// \return A matcher for an integer or vector where CheckFn(ele) for each element is true. For vectors, poison elements are assumed to match.
inline cst_pred_ty<custom_checkfn<APInt>>
m_CheckedInt(function_ref<bool(const APInt &)> CheckFn) {
  return cst_pred_ty<custom_checkfn<APInt>>{{CheckFn}};
}

/// Match an integer or vector where CheckFn(ele) is true, binding the constant.
/// For vectors, poison elements are assumed to match.
/// \param V Reference that receives the matched Constant.
/// \param CheckFn Predicate applied to each integer element.
/// \return A matcher for an integer or vector where CheckFn(ele) is true, binding the constant. For vectors, poison elements are assumed to match.
inline cst_pred_ty<custom_checkfn<APInt>>
m_CheckedInt(const Constant *&V, function_ref<bool(const APInt &)> CheckFn) {
  return cst_pred_ty<custom_checkfn<APInt>>{{CheckFn}, &V};
}

/// Match a float or vector where CheckFn(ele) for each element is true.
/// For vectors, poison elements are assumed to match.
/// \param CheckFn Predicate applied to each floating-point element.
/// \return A matcher for a float or vector where CheckFn(ele) for each element is true. For vectors, poison elements are assumed to match.
inline cstfp_pred_ty<custom_checkfn<APFloat>>
m_CheckedFp(function_ref<bool(const APFloat &)> CheckFn) {
  return cstfp_pred_ty<custom_checkfn<APFloat>>{{CheckFn}};
}

/// Match a float or vector where CheckFn(ele) is true, binding the constant.
/// For vectors, poison elements are assumed to match.
/// \param V Reference that receives the matched Constant.
/// \param CheckFn Predicate applied to each floating-point element.
/// \return A matcher for a float or vector where CheckFn(ele) is true, binding the constant. For vectors, poison elements are assumed to match.
inline cstfp_pred_ty<custom_checkfn<APFloat>>
m_CheckedFp(const Constant *&V, function_ref<bool(const APFloat &)> CheckFn) {
  return cstfp_pred_ty<custom_checkfn<APFloat>>{{CheckFn}, &V};
}

/// Predicate that matches any APInt value.
struct is_any_apint {
  /// Always return true for any APInt \p C.
  /// \param C The APInt to test.
  /// \return True if the value satisfies the predicate.
  bool isValue(const APInt &C) const { return true; }
};
/// Match an integer or vector with any integral constant.
/// For vectors, this includes constants with undefined elements.
/// \return A matcher for an integer or vector with any integral constant. For vectors, this includes constants with undefined elements.
inline cst_pred_ty<is_any_apint> m_AnyIntegralConstant() {
  return cst_pred_ty<is_any_apint>();
}

/// Predicate that matches shifted-mask APInt values.
struct is_shifted_mask {
  /// Return true if \p C is a shifted mask.
  /// \param C The APInt to test.
  /// \return True if the value satisfies the predicate.
  bool isValue(const APInt &C) const { return C.isShiftedMask(); }
};

/// Match an integer or vector shifted-mask constant.
/// \return A matcher for an integer or vector shifted-mask constant.
inline cst_pred_ty<is_shifted_mask> m_ShiftedMask() {
  return cst_pred_ty<is_shifted_mask>();
}

/// Predicate that matches all-ones APInt values.
struct is_all_ones {
  /// Return true if \p C has all bits set.
  /// \param C The APInt to test.
  /// \return True if the value satisfies the predicate.
  bool isValue(const APInt &C) const { return C.isAllOnes(); }
};
/// Match an integer or vector with all bits set.
/// For vectors, this includes constants with undefined elements.
/// \return A matcher for an integer or vector with all bits set. For vectors, this includes constants with undefined elements.
inline cst_pred_ty<is_all_ones> m_AllOnes() {
  return cst_pred_ty<is_all_ones>();
}

/// Match an all-ones integer or vector, forbidding poison elements.
/// \return A matcher for an all-ones integer or vector, forbidding poison elements.
inline cst_pred_ty<is_all_ones, false> m_AllOnesForbidPoison() {
  return cst_pred_ty<is_all_ones, false>();
}

/// Match an all-ones constant or poison.
/// \return A matcher for an all-ones constant or poison.
inline auto m_AllOnesOrPoison() { return m_CombineOr(m_AllOnes(), m_Poison()); }

/// Predicate that matches the maximum signed APInt value.
struct is_maxsignedvalue {
  /// Return true if \p C is the maximum signed value.
  /// \param C The APInt to test.
  /// \return True if the value satisfies the predicate.
  bool isValue(const APInt &C) const { return C.isMaxSignedValue(); }
};
/// Match an integer or vector with values having all bits except for the high
/// bit set (0x7f...).
/// For vectors, this includes constants with undefined elements.
/// \return A matcher for a max-signed-value integer or vector constant.
inline cst_pred_ty<is_maxsignedvalue> m_MaxSignedValue() {
  return cst_pred_ty<is_maxsignedvalue>();
}
/// Match a max-signed-value constant and bind the APInt.
/// \param V Reference to the pointer that receives the matched APInt.
/// \return A matcher for a max-signed-value constant and bind the APInt.
inline api_pred_ty<is_maxsignedvalue> m_MaxSignedValue(const APInt *&V) {
  return V;
}

/// Predicate that matches negative APInt values.
struct is_negative {
  /// Return true if \p C is negative.
  /// \param C The APInt to test.
  /// \return True if the value satisfies the predicate.
  bool isValue(const APInt &C) const { return C.isNegative(); }
};
/// Match an integer or vector of negative values.
/// For vectors, this includes constants with undefined elements.
/// \return A matcher for an integer or vector of negative values. For vectors, this includes constants with undefined elements.
inline cst_pred_ty<is_negative> m_Negative() {
  return cst_pred_ty<is_negative>();
}
/// Match a negative constant and bind the APInt.
/// \param V Reference to the pointer that receives the matched APInt.
/// \return A matcher for a negative constant and bind the APInt.
inline api_pred_ty<is_negative> m_Negative(const APInt *&V) { return V; }

/// Predicate that matches non-negative APInt values.
struct is_nonnegative {
  /// Return true if \p C is non-negative.
  /// \param C The APInt to test.
  /// \return True if the value satisfies the predicate.
  bool isValue(const APInt &C) const { return C.isNonNegative(); }
};
/// Match an integer or vector of non-negative values.
/// For vectors, this includes constants with undefined elements.
/// \return A matcher for an integer or vector of non-negative values. For vectors, this includes constants with undefined elements.
inline cst_pred_ty<is_nonnegative> m_NonNegative() {
  return cst_pred_ty<is_nonnegative>();
}
/// Match a non-negative constant and bind the APInt.
/// \param V Reference to the pointer that receives the matched APInt.
/// \return A matcher for a non-negative constant and bind the APInt.
inline api_pred_ty<is_nonnegative> m_NonNegative(const APInt *&V) { return V; }

/// Predicate that matches strictly positive APInt values.
struct is_strictlypositive {
  /// Return true if \p C is strictly positive.
  /// \param C The APInt to test.
  /// \return True if the value satisfies the predicate.
  bool isValue(const APInt &C) const { return C.isStrictlyPositive(); }
};
/// Match an integer or vector of strictly positive values.
/// For vectors, this includes constants with undefined elements.
/// \return A matcher for an integer or vector of strictly positive values. For vectors, this includes constants with undefined elements.
inline cst_pred_ty<is_strictlypositive> m_StrictlyPositive() {
  return cst_pred_ty<is_strictlypositive>();
}
/// Match a strictly positive constant and bind the APInt.
/// \param V Reference to the pointer that receives the matched APInt.
/// \return A matcher for a strictly positive constant and bind the APInt.
inline api_pred_ty<is_strictlypositive> m_StrictlyPositive(const APInt *&V) {
  return V;
}

/// Predicate that matches non-positive APInt values.
struct is_nonpositive {
  /// Return true if \p C is non-positive.
  /// \param C The APInt to test.
  /// \return True if the value satisfies the predicate.
  bool isValue(const APInt &C) const { return C.isNonPositive(); }
};
/// Match an integer or vector of non-positive values.
/// For vectors, this includes constants with undefined elements.
/// \return A matcher for an integer or vector of non-positive values. For vectors, this includes constants with undefined elements.
inline cst_pred_ty<is_nonpositive> m_NonPositive() {
  return cst_pred_ty<is_nonpositive>();
}
/// Match a non-positive constant and bind the APInt.
/// \param V Reference to the pointer that receives the matched APInt.
/// \return A matcher for a non-positive constant and bind the APInt.
inline api_pred_ty<is_nonpositive> m_NonPositive(const APInt *&V) { return V; }

/// Predicate that matches APInt value one.
struct is_one {
  /// Return true if \p C equals one.
  /// \param C The APInt to test.
  /// \return True if the value satisfies the predicate.
  bool isValue(const APInt &C) const { return C.isOne(); }
};
/// Match an integer 1 or a vector with all elements equal to 1.
/// For vectors, this includes constants with undefined elements.
/// \return A matcher for an integer 1 or a vector with all elements equal to 1. For vectors, this includes constants with undefined elements.
inline cst_pred_ty<is_one> m_One() { return cst_pred_ty<is_one>(); }

/// Predicate that matches zero APInt values.
struct is_zero_int {
  /// Return true if \p C is zero.
  /// \param C The APInt to test.
  /// \return True if the value satisfies the predicate.
  bool isValue(const APInt &C) const { return C.isZero(); }
};
/// Match an integer 0 or a vector with all elements equal to 0.
/// For vectors, this includes constants with undefined elements.
/// \return A matcher for an integer 0 or a vector with all elements equal to 0. For vectors, this includes constants with undefined elements.
inline cst_pred_ty<is_zero_int> m_ZeroInt() {
  return cst_pred_ty<is_zero_int>();
}

/// Predicate that matches non-zero APInt values.
struct is_non_zero_int {
  /// Return true if \p C is non-zero.
  /// \param C The APInt to test.
  /// \return True if the value satisfies the predicate.
  bool isValue(const APInt &C) const { return !C.isZero(); }
};
/// Match a non-zero integer or a vector with all non-zero elements.
/// For vectors, this includes constants with undefined elements.
/// \return A matcher for a non-zero integer or a vector with all non-zero elements. For vectors, this includes constants with undefined elements.
inline cst_pred_ty<is_non_zero_int> m_NonZeroInt() {
  return cst_pred_ty<is_non_zero_int>();
}

/// Matcher for null constants or integer zero vectors.
struct is_zero {
  /// Match if \p V is a null constant or an all-zero integer vector.
  /// \param V The value to match.
  /// \return True if the match succeeds.
  template <typename ITy> bool match(ITy *V) const {
    auto *C = dyn_cast<Constant>(V);
    // FIXME: this should be able to do something for scalable vectors
    return C && (C->isNullValue() || cst_pred_ty<is_zero_int>().match(C));
  }
};
/// Match any null constant or a vector with all elements equal to 0.
/// For vectors, this includes constants with undefined elements.
/// \return A matcher for any null constant or a vector with all elements equal to 0. For vectors, this includes constants with undefined elements.
inline is_zero m_Zero() { return is_zero(); }

/// Match a zero constant or poison.
/// \return A matcher for a zero constant or poison.
inline auto m_ZeroOrPoison() { return m_CombineOr(m_Zero(), m_Poison()); }

/// Predicate that matches power-of-two APInt values.
struct is_power2 {
  /// Return true if \p C is a power of two.
  /// \param C The APInt to test.
  /// \return True if the value satisfies the predicate.
  bool isValue(const APInt &C) const { return C.isPowerOf2(); }
};
/// Match an integer or vector power-of-2.
/// For vectors, this includes constants with undefined elements.
/// \return A matcher for an integer or vector power-of-2. For vectors, this includes constants with undefined elements.
inline cst_pred_ty<is_power2> m_Power2() { return cst_pred_ty<is_power2>(); }
/// Match a power-of-two constant and bind the APInt.
/// \param V Reference to the pointer that receives the matched APInt.
/// \return A matcher for a power-of-two constant and bind the APInt.
inline api_pred_ty<is_power2> m_Power2(const APInt *&V) { return V; }

/// Predicate that matches negated power-of-two APInt values.
struct is_negated_power2 {
  /// Return true if \p C is a negated power of two.
  /// \param C The APInt to test.
  /// \return True if the value satisfies the predicate.
  bool isValue(const APInt &C) const { return C.isNegatedPowerOf2(); }
};
/// Match a integer or vector negated power-of-2.
/// For vectors, this includes constants with undefined elements.
/// \return A matcher for a integer or vector negated power-of-2. For vectors, this includes constants with undefined elements.
inline cst_pred_ty<is_negated_power2> m_NegatedPower2() {
  return cst_pred_ty<is_negated_power2>();
}
/// Match a negated power-of-two constant and bind the APInt.
/// \param V Reference to the pointer that receives the matched APInt.
/// \return A matcher for a negated power-of-two constant and bind the APInt.
inline api_pred_ty<is_negated_power2> m_NegatedPower2(const APInt *&V) {
  return V;
}

/// Predicate that matches zero or negated power-of-two APInt values.
struct is_negated_power2_or_zero {
  /// Return true if \p C is zero or a negated power of two.
  /// \param C The APInt to test.
  /// \return True if the value satisfies the predicate.
  bool isValue(const APInt &C) const { return !C || C.isNegatedPowerOf2(); }
};
/// Match a integer or vector negated power-of-2.
/// For vectors, this includes constants with undefined elements.
/// \return A matcher for a integer or vector negated power-of-2. For vectors, this includes constants with undefined elements.
inline cst_pred_ty<is_negated_power2_or_zero> m_NegatedPower2OrZero() {
  return cst_pred_ty<is_negated_power2_or_zero>();
}
/// Match a zero or negated power-of-two constant and bind the APInt.
/// \param V Reference to the pointer that receives the matched APInt.
/// \return A matcher for a zero or negated power-of-two constant and bind the APInt.
inline api_pred_ty<is_negated_power2_or_zero>
m_NegatedPower2OrZero(const APInt *&V) {
  return V;
}

/// Predicate that matches zero or power-of-two APInt values.
struct is_power2_or_zero {
  /// Return true if \p C is zero or a power of two.
  /// \param C The APInt to test.
  /// \return True if the value satisfies the predicate.
  bool isValue(const APInt &C) const { return !C || C.isPowerOf2(); }
};
/// Match an integer or vector of 0 or power-of-2 values.
/// For vectors, this includes constants with undefined elements.
/// \return A matcher for an integer or vector of 0 or power-of-2 values. For vectors, this includes constants with undefined elements.
inline cst_pred_ty<is_power2_or_zero> m_Power2OrZero() {
  return cst_pred_ty<is_power2_or_zero>();
}
/// Match a zero or power-of-two constant and bind the APInt.
/// \param V Reference to the pointer that receives the matched APInt.
/// \return A matcher for a zero or power-of-two constant and bind the APInt.
inline api_pred_ty<is_power2_or_zero> m_Power2OrZero(const APInt *&V) {
  return V;
}

/// Predicate that matches sign-mask APInt values.
struct is_sign_mask {
  /// Return true if \p C is a sign mask.
  /// \param C The APInt to test.
  /// \return True if the value satisfies the predicate.
  bool isValue(const APInt &C) const { return C.isSignMask(); }
};
/// Match an integer or vector with only the sign bit(s) set.
/// For vectors, this includes constants with undefined elements.
/// \return A matcher for an integer or vector with only the sign bit(s) set. For vectors, this includes constants with undefined elements.
inline cst_pred_ty<is_sign_mask> m_SignMask() {
  return cst_pred_ty<is_sign_mask>();
}

/// Predicate that matches low-bit-mask APInt values.
struct is_lowbit_mask {
  /// Return true if \p C is a low-bit mask.
  /// \param C The APInt to test.
  /// \return True if the value satisfies the predicate.
  bool isValue(const APInt &C) const { return C.isMask(); }
};
/// Match an integer or vector with only the low bit(s) set.
/// For vectors, this includes constants with undefined elements.
/// \return A matcher for an integer or vector with only the low bit(s) set. For vectors, this includes constants with undefined elements.
inline cst_pred_ty<is_lowbit_mask> m_LowBitMask() {
  return cst_pred_ty<is_lowbit_mask>();
}
/// Match a low-bit-mask constant and bind the APInt.
/// \param V Reference to the pointer that receives the matched APInt.
/// \return A matcher for a low-bit-mask constant and bind the APInt.
inline api_pred_ty<is_lowbit_mask> m_LowBitMask(const APInt *&V) { return V; }

/// Predicate that matches zero or low-bit-mask APInt values.
struct is_lowbit_mask_or_zero {
  /// Return true if \p C is zero or a low-bit mask.
  /// \param C The APInt to test.
  /// \return True if the value satisfies the predicate.
  bool isValue(const APInt &C) const { return !C || C.isMask(); }
};
/// Match an integer or vector with only the low bit(s) set.
/// For vectors, this includes constants with undefined elements.
/// \return A matcher for an integer or vector with only the low bit(s) set. For vectors, this includes constants with undefined elements.
inline cst_pred_ty<is_lowbit_mask_or_zero> m_LowBitMaskOrZero() {
  return cst_pred_ty<is_lowbit_mask_or_zero>();
}
/// Match a zero or low-bit-mask constant and bind the APInt.
/// \param V Reference to the pointer that receives the matched APInt.
/// \return A matcher for a zero or low-bit-mask constant and bind the APInt.
inline api_pred_ty<is_lowbit_mask_or_zero> m_LowBitMaskOrZero(const APInt *&V) {
  return V;
}

/// Predicate that compares an APInt against a threshold with an icmp predicate.
struct icmp_pred_with_threshold {
  /// Integer compare predicate to apply.
  CmpPredicate Pred;
  /// Threshold compared against each element.
  const APInt *Thr;
  /// Return true if \p C compares against \c Thr under \c Pred.
  /// \param C The APInt to test.
  /// \return True if the value satisfies the predicate.
  bool isValue(const APInt &C) const {
    return ICmpInst::compare(C, *Thr, Pred);
  }
};
/// Match an integer or vector with every element comparing 'pred' (eg/ne/...)
/// to Threshold. For vectors, this includes constants with undefined elements.
/// \param Predicate The icmp predicate to apply.
/// \param Threshold The threshold each element is compared against.
/// \return A matcher for an integer or vector with every element comparing 'pred' (eg/ne/...) to Threshold. For vectors, this includes constants with undefined elements.
inline cst_pred_ty<icmp_pred_with_threshold>
m_SpecificInt_ICMP(ICmpInst::Predicate Predicate, const APInt &Threshold) {
  cst_pred_ty<icmp_pred_with_threshold> P;
  P.Pred = Predicate;
  P.Thr = &Threshold;
  return P;
}

/// Predicate that matches NaN APFloat values.
struct is_nan {
  /// Return true if \p C is a NaN.
  /// \param C The APFloat to test.
  /// \return True if the value satisfies the predicate.
  bool isValue(const APFloat &C) const { return C.isNaN(); }
};
/// Match an arbitrary NaN constant. This includes quiet and signalling nans.
/// For vectors, this includes constants with undefined elements.
/// \return A matcher for an arbitrary NaN constant. This includes quiet and signalling nans. For vectors, this includes constants with undefined elements.
inline cstfp_pred_ty<is_nan> m_NaN() { return cstfp_pred_ty<is_nan>(); }

/// Predicate that matches non-NaN APFloat values.
struct is_nonnan {
  /// Return true if \p C is not a NaN.
  /// \param C The APFloat to test.
  /// \return True if the value satisfies the predicate.
  bool isValue(const APFloat &C) const { return !C.isNaN(); }
};
/// Match a non-NaN FP constant.
/// For vectors, this includes constants with undefined elements.
/// \return A matcher for a non-NaN FP constant. For vectors, this includes constants with undefined elements.
inline cstfp_pred_ty<is_nonnan> m_NonNaN() {
  return cstfp_pred_ty<is_nonnan>();
}

/// Predicate that matches infinity APFloat values.
struct is_inf {
  /// Return true if \p C is infinity.
  /// \param C The APFloat to test.
  /// \return True if the value satisfies the predicate.
  bool isValue(const APFloat &C) const { return C.isInfinity(); }
};
/// Match a positive or negative infinity FP constant.
/// For vectors, this includes constants with undefined elements.
/// \return A matcher for a positive or negative infinity FP constant. For vectors, this includes constants with undefined elements.
inline cstfp_pred_ty<is_inf> m_Inf() { return cstfp_pred_ty<is_inf>(); }

/// Predicate that matches signed infinity APFloat values.
template <bool IsNegative> struct is_signed_inf {
  /// Return true if \p C is infinity with the configured sign.
  /// \param C The APFloat to test.
  /// \return True if the value satisfies the predicate.
  bool isValue(const APFloat &C) const {
    return C.isInfinity() && IsNegative == C.isNegative();
  }
};

/// Match a positive infinity FP constant.
/// For vectors, this includes constants with undefined elements.
/// \return A matcher for a positive infinity FP constant. For vectors, this includes constants with undefined elements.
inline cstfp_pred_ty<is_signed_inf<false>> m_PosInf() {
  return cstfp_pred_ty<is_signed_inf<false>>();
}

/// Match a negative infinity FP constant.
/// For vectors, this includes constants with undefined elements.
/// \return A matcher for a negative infinity FP constant. For vectors, this includes constants with undefined elements.
inline cstfp_pred_ty<is_signed_inf<true>> m_NegInf() {
  return cstfp_pred_ty<is_signed_inf<true>>();
}

/// Predicate that matches non-infinity APFloat values.
struct is_noninf {
  /// Return true if \p C is not infinity.
  /// \param C The APFloat to test.
  /// \return True if the value satisfies the predicate.
  bool isValue(const APFloat &C) const { return !C.isInfinity(); }
};
/// Match a non-infinity FP constant, i.e. finite or NaN.
/// For vectors, this includes constants with undefined elements.
/// \return A matcher for a non-infinity FP constant, i.e. finite or NaN. For vectors, this includes constants with undefined elements.
inline cstfp_pred_ty<is_noninf> m_NonInf() {
  return cstfp_pred_ty<is_noninf>();
}

/// Predicate that matches finite APFloat values.
struct is_finite {
  /// Return true if \p C is finite.
  /// \param C The APFloat to test.
  /// \return True if the value satisfies the predicate.
  bool isValue(const APFloat &C) const { return C.isFinite(); }
};
/// Match a finite FP constant, i.e. not infinity or NaN.
/// For vectors, this includes constants with undefined elements.
/// \return A matcher for a finite FP constant, i.e. not infinity or NaN. For vectors, this includes constants with undefined elements.
inline cstfp_pred_ty<is_finite> m_Finite() {
  return cstfp_pred_ty<is_finite>();
}
/// Match a finite FP constant and bind the APFloat.
/// \param V Reference to the pointer that receives the matched APFloat.
/// \return A matcher for a finite FP constant and bind the APFloat.
inline apf_pred_ty<is_finite> m_Finite(const APFloat *&V) { return V; }

/// Predicate that matches finite non-zero APFloat values.
struct is_finitenonzero {
  /// Return true if \p C is finite and non-zero.
  /// \param C The APFloat to test.
  /// \return True if the value satisfies the predicate.
  bool isValue(const APFloat &C) const { return C.isFiniteNonZero(); }
};
/// Match a finite non-zero FP constant.
/// For vectors, this includes constants with undefined elements.
/// \return A matcher for a finite non-zero FP constant. For vectors, this includes constants with undefined elements.
inline cstfp_pred_ty<is_finitenonzero> m_FiniteNonZero() {
  return cstfp_pred_ty<is_finitenonzero>();
}
/// Match a finite non-zero FP constant and bind the APFloat.
/// \param V Reference to the pointer that receives the matched APFloat.
/// \return A matcher for a finite non-zero FP constant and bind the APFloat.
inline apf_pred_ty<is_finitenonzero> m_FiniteNonZero(const APFloat *&V) {
  return V;
}

/// Predicate that matches any zero APFloat value.
struct is_any_zero_fp {
  /// Return true if \p C is positive or negative zero.
  /// \param C The APFloat to test.
  /// \return True if the value satisfies the predicate.
  bool isValue(const APFloat &C) const { return C.isZero(); }
};
/// Match a floating-point negative zero or positive zero.
/// For vectors, this includes constants with undefined elements.
/// \return A matcher for a floating-point negative zero or positive zero. For vectors, this includes constants with undefined elements.
inline cstfp_pred_ty<is_any_zero_fp> m_AnyZeroFP() {
  return cstfp_pred_ty<is_any_zero_fp>();
}

/// Predicate that matches positive-zero APFloat values.
struct is_pos_zero_fp {
  /// Return true if \p C is positive zero.
  /// \param C The APFloat to test.
  /// \return True if the value satisfies the predicate.
  bool isValue(const APFloat &C) const { return C.isPosZero(); }
};
/// Match a floating-point positive zero.
/// For vectors, this includes constants with undefined elements.
/// \return A matcher for a floating-point positive zero. For vectors, this includes constants with undefined elements.
inline cstfp_pred_ty<is_pos_zero_fp> m_PosZeroFP() {
  return cstfp_pred_ty<is_pos_zero_fp>();
}

/// Predicate that matches negative-zero APFloat values.
struct is_neg_zero_fp {
  /// Return true if \p C is negative zero.
  /// \param C The APFloat to test.
  /// \return True if the value satisfies the predicate.
  bool isValue(const APFloat &C) const { return C.isNegZero(); }
};
/// Match a floating-point negative zero.
/// For vectors, this includes constants with undefined elements.
/// \return A matcher for a floating-point negative zero. For vectors, this includes constants with undefined elements.
inline cstfp_pred_ty<is_neg_zero_fp> m_NegZeroFP() {
  return cstfp_pred_ty<is_neg_zero_fp>();
}

/// Predicate that matches non-zero APFloat values.
struct is_non_zero_fp {
  /// Return true if \p C is non-zero.
  /// \param C The APFloat to test.
  /// \return True if the value satisfies the predicate.
  bool isValue(const APFloat &C) const { return C.isNonZero(); }
};
/// Match a floating-point non-zero.
/// For vectors, this includes constants with undefined elements.
/// \return A matcher for a floating-point non-zero. For vectors, this includes constants with undefined elements.
inline cstfp_pred_ty<is_non_zero_fp> m_NonZeroFP() {
  return cstfp_pred_ty<is_non_zero_fp>();
}

/// Predicate that matches non-zero non-denormal APFloat values.
struct is_non_zero_not_denormal_fp {
  /// Return true if \p C is non-zero and not a denormal.
  /// \param C The APFloat to test.
  /// \return True if the value satisfies the predicate.
  bool isValue(const APFloat &C) const {
    return !C.isDenormal() && C.isNonZero();
  }
};

/// Match a floating-point non-zero that is not a denormal.
/// For vectors, this includes constants with undefined elements.
/// \return A matcher for a floating-point non-zero that is not a denormal. For vectors, this includes constants with undefined elements.
inline cstfp_pred_ty<is_non_zero_not_denormal_fp> m_NonZeroNotDenormalFP() {
  return cstfp_pred_ty<is_non_zero_not_denormal_fp>();
}

///////////////////////////////////////////////////////////////////////////////

/// Match a value, capturing it if we match.
/// \param V Reference that receives the matched Value.
/// \return A matcher for a value, capturing it if we match.
inline match_bind<Value> m_Value(Value *&V) { return V; }
/// Match a const value, capturing it if we match.
/// \param V Reference that receives the matched Value.
/// \return A matcher for a const value, capturing it if we match.
inline match_bind<const Value> m_Value(const Value *&V) { return V; }

/// Match against the nested pattern, and capture the value if we match.
/// \param V Reference that receives the matched Value.
/// \param P The nested pattern to match.
/// \return A matcher for against the nested pattern, and capture the value if we match.
template <typename Pattern> inline auto m_Value(Value *&V, const Pattern &P) {
  return m_CombineAnd(P, match_bind<Value>(V));
}

/// Match against the nested pattern, and capture the value if we match.
/// \param V Reference that receives the matched Value.
/// \param P The nested pattern to match.
/// \return A matcher for against the nested pattern, and capture the value if we match.
template <typename Pattern>
inline auto m_Value(const Value *&V, const Pattern &P) {
  return m_CombineAnd(P, match_bind<const Value>(V));
}

/// Match an instruction, capturing it if we match.
/// \param I Reference that receives the matched Instruction.
/// \return A matcher for an instruction, capturing it if we match.
inline match_bind<Instruction> m_Instruction(Instruction *&I) { return I; }
/// Match a const instruction, capturing it if we match.
/// \param I Reference that receives the matched Instruction.
/// \return A matcher for a const instruction, capturing it if we match.
inline match_bind<const Instruction> m_Instruction(const Instruction *&I) {
  return I;
}

/// Match against the nested pattern, and capture the instruction if we match.
/// \param I Reference that receives the matched Instruction.
/// \param P The nested pattern to match.
/// \return A matcher for against the nested pattern, and capture the instruction if we match.
template <typename Pattern>
inline auto m_Instruction(Instruction *&I, const Pattern &P) {
  return m_CombineAnd(P, match_bind<Instruction>(I));
}
/// Match against the nested pattern, and capture the instruction if we match.
/// \param I Reference that receives the matched Instruction.
/// \param P The nested pattern to match.
/// \return A matcher for against the nested pattern, and capture the instruction if we match.
template <typename Pattern>
inline auto m_Instruction(const Instruction *&I, const Pattern &P) {
  return m_CombineAnd(P, match_bind<const Instruction>(I));
}

/// Match a unary operator, capturing it if we match.
/// \param I Reference that receives the matched UnaryOperator.
/// \return A matcher for a unary operator, capturing it if we match.
inline match_bind<UnaryOperator> m_UnOp(UnaryOperator *&I) { return I; }
/// Match a const unary operator, capturing it if we match.
/// \param I Reference that receives the matched UnaryOperator.
/// \return A matcher for a const unary operator, capturing it if we match.
inline match_bind<const UnaryOperator> m_UnOp(const UnaryOperator *&I) {
  return I;
}
/// Match a binary operator, capturing it if we match.
/// \param I Reference that receives the matched BinaryOperator.
/// \return A matcher for a binary operator, capturing it if we match.
inline match_bind<BinaryOperator> m_BinOp(BinaryOperator *&I) { return I; }
/// Match a const binary operator, capturing it if we match.
/// \param I Reference that receives the matched BinaryOperator.
/// \return A matcher for a const binary operator, capturing it if we match.
inline match_bind<const BinaryOperator> m_BinOp(const BinaryOperator *&I) {
  return I;
}
/// Match any intrinsic call, capturing it if we match.
/// \param I Reference that receives the matched IntrinsicInst.
/// \return A matcher for any intrinsic call, capturing it if we match.
inline match_bind<IntrinsicInst> m_AnyIntrinsic(IntrinsicInst *&I) { return I; }
/// Match any const intrinsic call, capturing it if we match.
/// \param I Reference that receives the matched IntrinsicInst.
/// \return A matcher for any const intrinsic call, capturing it if we match.
inline match_bind<const IntrinsicInst> m_AnyIntrinsic(const IntrinsicInst *&I) {
  return I;
}
/// Match a with overflow intrinsic, capturing it if we match.
/// \param I Reference that receives the matched WithOverflowInst.
/// \return A matcher for a with overflow intrinsic, capturing it if we match.
inline match_bind<WithOverflowInst> m_WithOverflowInst(WithOverflowInst *&I) {
  return I;
}
/// Match a const with overflow intrinsic, capturing it if we match.
/// \param I Reference that receives the matched WithOverflowInst.
/// \return A matcher for a const with overflow intrinsic, capturing it if we match.
inline match_bind<const WithOverflowInst>
m_WithOverflowInst(const WithOverflowInst *&I) {
  return I;
}

/// Match a PHI node, capturing it if we match.
/// \param PN Reference that receives the matched PHINode.
/// \return A matcher for a PHI node, capturing it if we match.
inline match_bind<PHINode> m_Phi(PHINode *&PN) { return PN; }

/// Match an UndefValue, capturing the value if we match.
/// \param U Reference that receives the matched UndefValue.
/// \return A matcher for an UndefValue, capturing the value if we match.
inline match_bind<UndefValue> m_UndefValue(UndefValue *&U) { return U; }

/// Match a Constant, capturing the value if we match.
/// \param C Reference that receives the matched Constant.
/// \return A matcher for a Constant, capturing the value if we match.
inline match_bind<Constant> m_Constant(Constant *&C) { return C; }

/// Match a ConstantInt, capturing the value if we match.
/// \param CI Reference that receives the matched ConstantInt.
/// \return A matcher for a ConstantInt, capturing the value if we match.
inline match_bind<ConstantInt> m_ConstantInt(ConstantInt *&CI) { return CI; }

/// Match a ConstantFP, capturing the value if we match.
/// \param C Reference that receives the matched ConstantFP.
/// \return A matcher for a ConstantFP, capturing the value if we match.
inline match_bind<ConstantFP> m_ConstantFP(ConstantFP *&C) { return C; }

/// Match a ConstantExpr, capturing the value if we match.
/// \param C Reference that receives the matched ConstantExpr.
/// \return A matcher for a ConstantExpr, capturing the value if we match.
inline match_bind<ConstantExpr> m_ConstantExpr(ConstantExpr *&C) { return C; }

/// Match a basic block value, capturing it if we match.
/// \param V Reference that receives the matched BasicBlock.
/// \return A matcher for a basic block value, capturing it if we match.
inline match_bind<BasicBlock> m_BasicBlock(BasicBlock *&V) { return V; }
/// Match a const basic block value, capturing it if we match.
/// \param V Reference that receives the matched BasicBlock.
/// \return A matcher for a const basic block value, capturing it if we match.
inline match_bind<const BasicBlock> m_BasicBlock(const BasicBlock *&V) {
  return V;
}

// TODO: Remove once UseConstant{Int,FP}ForScalableSplat is enabled by default,
// and use m_Unless(m_ConstantExpr).
/// Helper that recognizes immediate (non-ConstantExpr) constants.
struct immconstant_ty {
  /// Return true if \p V is an immediate constant.
  /// \param V The value to test.
  /// \return True if the value satisfies the predicate.
  template <typename ITy> static bool isImmConstant(ITy *V) {
    if (auto *CV = dyn_cast<Constant>(V)) {
      if (!match(CV, m_ConstantExpr()))
        return true;

      if (CV->getType()->isVectorTy()) {
        if (auto *Splat = CV->getSplatValue(/*AllowPoison=*/true)) {
          if (!match(Splat, m_ConstantExpr())) {
            return true;
          }
        }
      }
    }
    return false;
  }
};

/// Matcher for an immediate constant that does not bind.
struct match_immconstant_ty : immconstant_ty {
  /// Match if \p V is an immediate constant.
  /// \param V The value to match.
  /// \return True if the match succeeds.
  template <typename ITy> bool match(ITy *V) const { return isImmConstant(V); }
};

/// Match an arbitrary immediate Constant and ignore it.
/// \return A matcher for an arbitrary immediate Constant and ignore it.
inline match_immconstant_ty m_ImmConstant() { return match_immconstant_ty(); }

/// Matcher that binds an immediate Constant.
struct bind_immconstant_ty : immconstant_ty {
  /// Bound reference to the matched Constant.
  Constant *&VR;

  /// Construct a binder for immediate constant \p V.
  /// \param V Reference that receives the matched Constant.
  bind_immconstant_ty(Constant *&V) : VR(V) {}

  /// Match an immediate constant and bind it into \c VR.
  /// \param V The value to match.
  /// \return True if the match succeeds.
  template <typename ITy> bool match(ITy *V) const {
    if (isImmConstant(V)) {
      VR = cast<Constant>(V);
      return true;
    }
    return false;
  }
};

/// Match an immediate Constant, capturing the value if we match.
/// \param C Reference that receives the matched Constant.
/// \return A matcher for an immediate Constant, capturing the value if we match.
inline bind_immconstant_ty m_ImmConstant(Constant *&C) {
  return bind_immconstant_ty(C);
}

/// Matcher for a specific Value*.
struct specificval_ty {
  /// The specific value that must be matched.
  const Value *Val;

  /// Construct a matcher for specific value \p V.
  /// \param V The value that must be matched.
  specificval_ty(const Value *V) : Val(V) {}

  /// Match if \p V is exactly \c Val.
  /// \param V The value to match.
  /// \return True if the match succeeds.
  template <typename ITy> bool match(ITy *V) const { return V == Val; }
};

/// Match if we have a specific specified value.
/// \param V The specific value that must be matched.
/// \return A matcher for the specific value \p V.
inline specificval_ty m_Specific(const Value *V) { return V; }

/// Match a deferred Value* determined later in the same match expression.
///
/// Like m_Specific(), but works if the specific value to match is determined
/// as part of the same match() expression. For example:
/// m_Add(m_Value(X), m_Specific(X)) is incorrect, because m_Specific() will
/// bind X before the pattern match starts.
/// m_Add(m_Value(X), m_Deferred(X)) is correct, and will check against
/// whichever value m_Value(X) populated.
/// \param V Reference to the Value* populated during matching.
/// \return A matcher for a deferred Value* determined later in the same match expression.
inline match_deferred<Value> m_Deferred(Value *const &V) { return V; }
/// Match a deferred const Value* determined later in the same match expression.
/// \param V Reference to the Value* populated during matching.
/// \return A matcher for a deferred const Value* determined later in the same match expression.
inline match_deferred<const Value> m_Deferred(const Value *const &V) {
  return V;
}

/// Match a specified floating point value or vector of all elements of
/// that value.
struct specific_fpval {
  /// The floating-point value that must be matched.
  double Val;

  /// Construct a matcher for floating-point value \p V.
  /// \param V The floating-point value that must be matched.
  specific_fpval(double V) : Val(V) {}

  /// Match a scalar or splat FP constant equal to \c Val.
  /// \param V The value to match.
  /// \return True if the match succeeds.
  template <typename ITy> bool match(ITy *V) const {
    if (const auto *CFP = dyn_cast<ConstantFP>(V))
      return CFP->isExactlyValue(Val);
    if (V->getType()->isVectorTy())
      if (const auto *C = dyn_cast<Constant>(V))
        if (auto *CFP = dyn_cast_or_null<ConstantFP>(C->getSplatValue()))
          return CFP->isExactlyValue(Val);
    return false;
  }
};

/// Match a specific floating point value or vector with all elements
/// equal to the value.
/// \param V The floating-point value that must be matched.
/// \return A matcher for a specific floating point value or vector with all elements equal to the value.
inline specific_fpval m_SpecificFP(double V) { return specific_fpval(V); }

/// Match a float 1.0 or vector with all elements equal to 1.0.
/// \return A matcher for a float 1.0 or vector with all elements equal to 1.0.
inline specific_fpval m_FPOne() { return m_SpecificFP(1.0); }

/// Matcher that binds a ConstantInt value into a uint64_t.
struct bind_const_intval_ty {
  /// Bound reference to the matched zero-extended integer value.
  uint64_t &VR;

  /// Construct a binder for integer value \p V.
  /// \param V Reference that receives the matched zero-extended value.
  bind_const_intval_ty(uint64_t &V) : VR(V) {}

  /// Match a ConstantInt fitting in 64 bits and bind its value.
  /// \param V The value to match.
  /// \return True if the match succeeds.
  template <typename ITy> bool match(ITy *V) const {
    const APInt *ConstInt;
    if (!ap_match<APInt>(ConstInt, /*AllowPoison=*/false).match(V))
      return false;
    std::optional<uint64_t> ZExtVal = ConstInt->tryZExtValue();
    if (!ZExtVal)
      return false;
    VR = *ZExtVal;
    return true;
  }
};

/// Match a specified integer value or vector of all elements of that
/// value.
template <bool AllowPoison> struct specific_intval {
  /// The APInt value that must be matched.
  const APInt &Val;

  /// Construct a matcher for integer value \p V.
  /// \param V The APInt that must be matched.
  specific_intval(const APInt &V) : Val(V) {}

  /// Match a scalar or splat ConstantInt equal to \c Val.
  /// \param V The value to match.
  /// \return True if the match succeeds.
  template <typename ITy> bool match(ITy *V) const {
    const auto *CI = dyn_cast<ConstantInt>(V);
    if (!CI && V->getType()->isVectorTy())
      if (const auto *C = dyn_cast<Constant>(V))
        CI = dyn_cast_or_null<ConstantInt>(C->getSplatValue(AllowPoison));

    return CI && APInt::isSameValue(CI->getValue(), Val);
  }
};

/// Match a specified 64-bit integer value or vector of all elements of that
/// value.
template <bool AllowPoison> struct specific_intval64 {
  /// The integer value that must be matched.
  uint64_t Val;

  /// Construct a matcher for integer value \p V.
  /// \param V The integer value that must be matched.
  specific_intval64(uint64_t V) : Val(V) {}

  /// Match a scalar or splat ConstantInt equal to \c Val.
  /// \param V The value to match.
  /// \return True if the match succeeds.
  template <typename ITy> bool match(ITy *V) const {
    const auto *CI = dyn_cast<ConstantInt>(V);
    if (!CI && V->getType()->isVectorTy())
      if (const auto *C = dyn_cast<Constant>(V))
        CI = dyn_cast_or_null<ConstantInt>(C->getSplatValue(AllowPoison));

    return CI && CI->getValue() == Val;
  }
};

/// Match a specific integer value or vector with all elements equal to
/// the value.
/// \param V The APInt that must be matched.
/// \return A matcher for a specific integer value or vector with all elements equal to the value.
inline specific_intval<false> m_SpecificInt(const APInt &V) {
  return specific_intval<false>(V);
}

/// Match a specific 64-bit integer value or vector with all elements equal.
/// \param V The integer value that must be matched.
/// \return A matcher for a specific 64-bit integer value or vector with all elements equal.
inline specific_intval64<false> m_SpecificInt(uint64_t V) {
  return specific_intval64<false>(V);
}

/// Match a specific integer allowing poison in splat vectors.
/// \param V The APInt that must be matched.
/// \return A matcher for a specific integer allowing poison in splat vectors.
inline specific_intval<true> m_SpecificIntAllowPoison(const APInt &V) {
  return specific_intval<true>(V);
}

/// Match a specific 64-bit integer allowing poison in splat vectors.
/// \param V The integer value that must be matched.
/// \return A matcher for a specific 64-bit integer allowing poison in splat vectors.
inline specific_intval64<true> m_SpecificIntAllowPoison(uint64_t V) {
  return specific_intval64<true>(V);
}

/// Match a ConstantInt and bind to its value.  This does not match
/// ConstantInts wider than 64-bits.
/// \param V Reference that receives the matched zero-extended value.
/// \return A matcher for a ConstantInt and bind to its value.  This does not match ConstantInts wider than 64-bits.
inline bind_const_intval_ty m_ConstantInt(uint64_t &V) { return V; }

/// Match a specified basic block value.
struct specific_bbval {
  /// The basic block that must be matched.
  BasicBlock *Val;

  /// Construct a matcher for basic block \p Val.
  /// \param Val The basic block that must be matched.
  specific_bbval(BasicBlock *Val) : Val(Val) {}

  /// Match if \p V is exactly the configured basic block.
  /// \param V The value to match.
  /// \return True if the match succeeds.
  template <typename ITy> bool match(ITy *V) const {
    const auto *BB = dyn_cast<BasicBlock>(V);
    return BB && BB == Val;
  }
};

/// Match a specific basic block value.
/// \param BB The basic block that must be matched.
/// \return A matcher for a specific basic block value.
inline specific_bbval m_SpecificBB(BasicBlock *BB) {
  return specific_bbval(BB);
}

/// A commutative-friendly version of m_Specific().
/// \param BB Reference to the BasicBlock* populated during matching.
/// \return A deferred basic-block matcher bound to \p BB.
inline match_deferred<BasicBlock> m_Deferred(BasicBlock *const &BB) {
  return BB;
}
/// Match a deferred const BasicBlock* determined later in the same expression.
/// \param BB Reference to the BasicBlock* populated during matching.
/// \return A matcher for a deferred const BasicBlock* determined later in the same expression.
inline match_deferred<const BasicBlock>
m_Deferred(const BasicBlock *const &BB) {
  return BB;
}

/// Matcher that requires a value to have a specific type and match a pattern.
template <typename Pattern> struct SpecificType_match {
  /// The required type of the matched value.
  Type *RefTy;
  /// Nested pattern that must also match.
  Pattern P;

  /// Construct a typed matcher for type \p RefTy wrapping \p P.
  /// \param RefTy The required type.
  /// \param P The nested pattern to match.
  SpecificType_match(Type *RefTy, const Pattern &P) : RefTy(RefTy), P(P) {}

  /// Match if \p V has type \c RefTy and matches the nested pattern.
  /// \param V The value to match.
  /// \return True if the match succeeds.
  template <typename ITy> bool match(ITy *V) const {
    return V->getType() == RefTy && P.match(V);
  }
};

/// Explicit deduction guide for SpecificType_match.
template <typename Pattern>
SpecificType_match(const Type *, const Pattern &)
    -> SpecificType_match<Pattern>;

/// Match a value of a specific type.
/// \param RefTy The required type.
/// \param P The nested pattern to match.
/// \return A matcher for a value of a specific type.
template <typename Pattern>
inline auto m_SpecificType(Type *RefTy, const Pattern &P) {
  return SpecificType_match<Pattern>(RefTy, P);
}
/// Match any value of a specific type.
/// \param RefTy The required type.
/// \return A matcher for any value of a specific type.
inline auto m_SpecificType(Type *RefTy) {
  return m_SpecificType(RefTy, m_Value());
}

/// Match a value of a specific type, capturing it if we match.
/// \param RefTy The required type.
/// \param V Reference that receives the matched Value.
/// \return A matcher for a value of a specific type, capturing it if we match.
inline auto m_SpecificType(Type *RefTy, Value *&V) {
  return m_SpecificType(RefTy, m_Value(V));
}
/// Match a const value of a specific type, capturing it if we match.
/// \param RefTy The required type.
/// \param V Reference that receives the matched Value.
/// \return A matcher for a const value of a specific type, capturing it if we match.
inline auto m_SpecificType(Type *RefTy, const Value *&V) {
  return m_SpecificType(RefTy, m_Value(V));
}

//===----------------------------------------------------------------------===//
// Matcher for any binary operator.
//
/// Matches any BinaryOperator against left and right operand patterns.
template <typename LHS_t, typename RHS_t, bool Commutable = false>
struct AnyBinaryOp_match {
  /// Sub-pattern for the left-hand operand.
  LHS_t L;
  /// Sub-pattern for the right-hand operand.
  RHS_t R;

  // The evaluation order is always stable, regardless of Commutability.
  // The LHS is always matched first.
  /// Construct a matcher for both operands.
  ///
  /// \param LHS Sub-pattern for the left-hand operand.
  /// \param RHS Sub-pattern for the right-hand operand.
  AnyBinaryOp_match(const LHS_t &LHS, const RHS_t &RHS) : L(LHS), R(RHS) {}

  /// Match \p V as a BinaryOperator against L and R.
  ///
  /// \param V Value to match.
  /// \return True if the match succeeds.
  template <typename OpTy> bool match(OpTy *V) const {
    if (auto *I = dyn_cast<BinaryOperator>(V))
      return (L.match(I->getOperand(0)) && R.match(I->getOperand(1))) ||
             (Commutable && L.match(I->getOperand(1)) &&
              R.match(I->getOperand(0)));
    return false;
  }
};

/// Matches any binary operator with operands \p L and \p R.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for any binary operator with operands \p L and \p R.
template <typename LHS, typename RHS>
inline AnyBinaryOp_match<LHS, RHS> m_BinOp(const LHS &L, const RHS &R) {
  return AnyBinaryOp_match<LHS, RHS>(L, R);
}

//===----------------------------------------------------------------------===//
// Matcher for any unary operator.
// TODO fuse unary, binary matcher into n-ary matcher
//
/// Matches any UnaryOperator against an operand pattern.
template <typename OP_t> struct AnyUnaryOp_match {
  /// Sub-pattern for the unary operand.
  OP_t X;

  /// Construct a matcher for the unary operand.
  ///
  /// \param X Sub-pattern for the operand.
  AnyUnaryOp_match(const OP_t &X) : X(X) {}

  /// Match \p V as a UnaryOperator against X.
  ///
  /// \param V Value to match.
  /// \return True if the match succeeds.
  template <typename OpTy> bool match(OpTy *V) const {
    if (auto *I = dyn_cast<UnaryOperator>(V))
      return X.match(I->getOperand(0));
    return false;
  }
};

/// Matches any unary operator with operand \p X.
///
/// \param X Sub-pattern for the operand.
/// \return A matcher for any unary operator with operand \p X.
template <typename OP_t> inline AnyUnaryOp_match<OP_t> m_UnOp(const OP_t &X) {
  return AnyUnaryOp_match<OP_t>(X);
}

//===----------------------------------------------------------------------===//
// Matchers for specific binary operators.
//

/// Matches a BinaryOperator with a fixed Opcode against operand patterns.
template <typename LHS_t, typename RHS_t, unsigned Opcode,
          bool Commutable = false>
struct BinaryOp_match {
  /// Sub-pattern for the left-hand operand.
  LHS_t L;
  /// Sub-pattern for the right-hand operand.
  RHS_t R;

  // The evaluation order is always stable, regardless of Commutability.
  // The LHS is always matched first.
  /// Construct a matcher for both operands.
  ///
  /// \param LHS Sub-pattern for the left-hand operand.
  /// \param RHS Sub-pattern for the right-hand operand.
  BinaryOp_match(const LHS_t &LHS, const RHS_t &RHS) : L(LHS), R(RHS) {}

  /// Match \p V if its opcode is \p Opc and operands match L and R.
  ///
  /// \param Opc Instruction opcode to require.
  /// \param V Value to match.
  /// \return True if the match succeeds.
  template <typename OpTy> inline bool match(unsigned Opc, OpTy *V) const {
    if (V->getValueID() == Value::InstructionVal + Opc) {
      auto *I = cast<BinaryOperator>(V);
      return (L.match(I->getOperand(0)) && R.match(I->getOperand(1))) ||
             (Commutable && L.match(I->getOperand(1)) &&
              R.match(I->getOperand(0)));
    }
    return false;
  }

  /// Match \p V as a BinaryOperator with the template Opcode.
  ///
  /// \param V Value to match.
  /// \return True if the match succeeds.
  template <typename OpTy> bool match(OpTy *V) const {
    return match(Opcode, V);
  }
};

/// Matches an add of \p L and \p R.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for an add of \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::Add> m_Add(const LHS &L,
                                                        const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::Add>(L, R);
}

/// Matches an fadd of \p L and \p R.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for an fadd of \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::FAdd> m_FAdd(const LHS &L,
                                                          const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::FAdd>(L, R);
}

/// Matches a sub of \p L and \p R.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for a sub of \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::Sub> m_Sub(const LHS &L,
                                                        const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::Sub>(L, R);
}

/// Matches an fsub of \p L and \p R.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for an fsub of \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::FSub> m_FSub(const LHS &L,
                                                          const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::FSub>(L, R);
}

/// Matches fneg, including the fsub -0.0, X form.
template <typename Op_t> struct FNeg_match {
  /// Sub-pattern for the negated operand.
  Op_t X;

  /// Construct a matcher for the negated operand.
  ///
  /// \param Op Sub-pattern for the operand.
  FNeg_match(const Op_t &Op) : X(Op) {}
  /// Match \p V as fneg or an equivalent fsub of zero.
  ///
  /// \param V Value to match.
  /// \return True if the match succeeds.
  template <typename OpTy> bool match(OpTy *V) const {
    auto *FPMO = dyn_cast<FPMathOperator>(V);
    if (!FPMO)
      return false;

    if (FPMO->getOpcode() == Instruction::FNeg)
      return X.match(FPMO->getOperand(0));

    if (FPMO->getOpcode() == Instruction::FSub) {
      if (FPMO->hasNoSignedZeros()) {
        // With 'nsz', any zero goes.
        if (!cstfp_pred_ty<is_any_zero_fp>().match(FPMO->getOperand(0)))
          return false;
      } else {
        // Without 'nsz', we need fsub -0.0, X exactly.
        if (!cstfp_pred_ty<is_neg_zero_fp>().match(FPMO->getOperand(0)))
          return false;
      }

      return X.match(FPMO->getOperand(1));
    }

    return false;
  }
};

/// Match 'fneg X' as 'fsub -0.0, X'.
///
/// \param X Sub-pattern for the negated operand.
/// \return A matcher for 'fneg X' as 'fsub -0.0, X'.
template <typename OpTy> inline FNeg_match<OpTy> m_FNeg(const OpTy &X) {
  return FNeg_match<OpTy>(X);
}

/// Match 'fneg X' as 'fsub +-0.0, X'.
///
/// \param X Sub-pattern for the negated operand.
/// \return A matcher for 'fneg X' as 'fsub +-0.0, X'.
template <typename RHS>
inline BinaryOp_match<cstfp_pred_ty<is_any_zero_fp>, RHS, Instruction::FSub>
m_FNegNSZ(const RHS &X) {
  return m_FSub(m_AnyZeroFP(), X);
}

/// Matches a mul of \p L and \p R.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for a mul of \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::Mul> m_Mul(const LHS &L,
                                                        const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::Mul>(L, R);
}

/// Matches an fmul of \p L and \p R.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for an fmul of \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::FMul> m_FMul(const LHS &L,
                                                          const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::FMul>(L, R);
}

/// Matches a udiv of \p L and \p R.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for a udiv of \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::UDiv> m_UDiv(const LHS &L,
                                                          const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::UDiv>(L, R);
}

/// Matches an sdiv of \p L and \p R.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for an sdiv of \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::SDiv> m_SDiv(const LHS &L,
                                                          const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::SDiv>(L, R);
}

/// Matches an fdiv of \p L and \p R.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for an fdiv of \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::FDiv> m_FDiv(const LHS &L,
                                                          const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::FDiv>(L, R);
}

/// Matches a urem of \p L and \p R.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for a urem of \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::URem> m_URem(const LHS &L,
                                                          const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::URem>(L, R);
}

/// Matches an srem of \p L and \p R.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for an srem of \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::SRem> m_SRem(const LHS &L,
                                                          const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::SRem>(L, R);
}

/// Matches an frem of \p L and \p R.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for an frem of \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::FRem> m_FRem(const LHS &L,
                                                          const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::FRem>(L, R);
}

/// Matches an and of \p L and \p R.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for an and of \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::And> m_And(const LHS &L,
                                                        const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::And>(L, R);
}

/// Matches an or of \p L and \p R.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for an or of \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::Or> m_Or(const LHS &L,
                                                      const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::Or>(L, R);
}

/// Matches an xor of \p L and \p R.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for an xor of \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::Xor> m_Xor(const LHS &L,
                                                        const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::Xor>(L, R);
}

/// Matches a shl of \p L and \p R.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for a shl of \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::Shl> m_Shl(const LHS &L,
                                                        const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::Shl>(L, R);
}

/// Matches an lshr of \p L and \p R.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for an lshr of \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::LShr> m_LShr(const LHS &L,
                                                          const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::LShr>(L, R);
}

/// Matches an ashr of \p L and \p R.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for an ashr of \p L and \p R.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::AShr> m_AShr(const LHS &L,
                                                          const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::AShr>(L, R);
}

/// Matches a shift by a constant, or the value itself with amount zero.
template <typename LHS_t, unsigned Opcode> struct ShiftLike_match {
  /// Sub-pattern for the shifted value.
  LHS_t L;
  /// Bound constant shift amount; set to zero when matching the value itself.
  uint64_t &R;

  /// Construct a shift-or-self matcher.
  ///
  /// \param LHS Sub-pattern for the shifted value.
  /// \param RHS Output that receives the matched shift amount.
  ShiftLike_match(const LHS_t &LHS, uint64_t &RHS) : L(LHS), R(RHS) {}

  /// Match \p V as Opcode with a constant amount, or as L with R set to 0.
  ///
  /// \param V Value to match.
  /// \return True if the match succeeds.
  template <typename OpTy> bool match(OpTy *V) const {
    if (auto *Op = dyn_cast<BinaryOperator>(V)) {
      if (Op->getOpcode() == Opcode)
        return m_ConstantInt(R).match(Op->getOperand(1)) &&
               L.match(Op->getOperand(0));
    }
    // Interpreted as shiftop V, 0
    R = 0;
    return L.match(V);
  }
};

/// Matches shl L, ConstShAmt or L itself (R will be set to zero in this case).
///
/// \param L Sub-pattern for the shifted value.
/// \param R Output that receives the matched shift amount.
/// \return A matcher for shl L, ConstShAmt or L itself (R will be set to zero in this case).
template <typename LHS>
inline ShiftLike_match<LHS, Instruction::Shl> m_ShlOrSelf(const LHS &L,
                                                          uint64_t &R) {
  return ShiftLike_match<LHS, Instruction::Shl>(L, R);
}

/// Matches lshr L, ConstShAmt or L itself (R will be set to zero in this case).
///
/// \param L Sub-pattern for the shifted value.
/// \param R Output that receives the matched shift amount.
/// \return A matcher for lshr L, ConstShAmt or L itself (R will be set to zero in this case).
template <typename LHS>
inline ShiftLike_match<LHS, Instruction::LShr> m_LShrOrSelf(const LHS &L,
                                                            uint64_t &R) {
  return ShiftLike_match<LHS, Instruction::LShr>(L, R);
}

/// Matches ashr L, ConstShAmt or L itself (R will be set to zero in this case).
///
/// \param L Sub-pattern for the shifted value.
/// \param R Output that receives the matched shift amount.
/// \return A matcher for ashr L, ConstShAmt or L itself (R will be set to zero in this case).
template <typename LHS>
inline ShiftLike_match<LHS, Instruction::AShr> m_AShrOrSelf(const LHS &L,
                                                            uint64_t &R) {
  return ShiftLike_match<LHS, Instruction::AShr>(L, R);
}

/// Matches an overflowing binary op with required no-wrap flags.
template <typename LHS_t, typename RHS_t, unsigned Opcode,
          unsigned WrapFlags = 0, bool Commutable = false>
struct OverflowingBinaryOp_match {
  /// Sub-pattern for the left-hand operand.
  LHS_t L;
  /// Sub-pattern for the right-hand operand.
  RHS_t R;

  /// Construct a matcher for both operands.
  ///
  /// \param LHS Sub-pattern for the left-hand operand.
  /// \param RHS Sub-pattern for the right-hand operand.
  OverflowingBinaryOp_match(const LHS_t &LHS, const RHS_t &RHS)
      : L(LHS), R(RHS) {}

  /// Match \p V if opcode, wrap flags, and operands succeed.
  ///
  /// \param V Value to match.
  /// \return True if the match succeeds.
  template <typename OpTy> bool match(OpTy *V) const {
    if (auto *Op = dyn_cast<OverflowingBinaryOperator>(V)) {
      if (Op->getOpcode() != Opcode)
        return false;
      if ((WrapFlags & OverflowingBinaryOperator::NoUnsignedWrap) &&
          !Op->hasNoUnsignedWrap())
        return false;
      if ((WrapFlags & OverflowingBinaryOperator::NoSignedWrap) &&
          !Op->hasNoSignedWrap())
        return false;
      return (L.match(Op->getOperand(0)) && R.match(Op->getOperand(1))) ||
             (Commutable && L.match(Op->getOperand(1)) &&
              R.match(Op->getOperand(0)));
    }
    return false;
  }
};

/// Matches an add nsw of \p L and \p R.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for an add nsw of \p L and \p R.
template <typename LHS, typename RHS>
inline OverflowingBinaryOp_match<LHS, RHS, Instruction::Add,
                                 OverflowingBinaryOperator::NoSignedWrap>
m_NSWAdd(const LHS &L, const RHS &R) {
  return OverflowingBinaryOp_match<LHS, RHS, Instruction::Add,
                                   OverflowingBinaryOperator::NoSignedWrap>(L,
                                                                            R);
}
/// Matches an add nsw of \p L and \p R in either order.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for an add nsw of \p L and \p R in either order.
template <typename LHS, typename RHS>
inline OverflowingBinaryOp_match<LHS, RHS, Instruction::Add,
                                 OverflowingBinaryOperator::NoSignedWrap, true>
m_c_NSWAdd(const LHS &L, const RHS &R) {
  return OverflowingBinaryOp_match<LHS, RHS, Instruction::Add,
                                   OverflowingBinaryOperator::NoSignedWrap,
                                   true>(L, R);
}
/// Matches a sub nsw of \p L and \p R.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for a sub nsw of \p L and \p R.
template <typename LHS, typename RHS>
inline OverflowingBinaryOp_match<LHS, RHS, Instruction::Sub,
                                 OverflowingBinaryOperator::NoSignedWrap>
m_NSWSub(const LHS &L, const RHS &R) {
  return OverflowingBinaryOp_match<LHS, RHS, Instruction::Sub,
                                   OverflowingBinaryOperator::NoSignedWrap>(L,
                                                                            R);
}
/// Matches a mul nsw of \p L and \p R.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for a mul nsw of \p L and \p R.
template <typename LHS, typename RHS>
inline OverflowingBinaryOp_match<LHS, RHS, Instruction::Mul,
                                 OverflowingBinaryOperator::NoSignedWrap>
m_NSWMul(const LHS &L, const RHS &R) {
  return OverflowingBinaryOp_match<LHS, RHS, Instruction::Mul,
                                   OverflowingBinaryOperator::NoSignedWrap>(L,
                                                                            R);
}
/// Matches a shl nsw of \p L and \p R.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for a shl nsw of \p L and \p R.
template <typename LHS, typename RHS>
inline OverflowingBinaryOp_match<LHS, RHS, Instruction::Shl,
                                 OverflowingBinaryOperator::NoSignedWrap>
m_NSWShl(const LHS &L, const RHS &R) {
  return OverflowingBinaryOp_match<LHS, RHS, Instruction::Shl,
                                   OverflowingBinaryOperator::NoSignedWrap>(L,
                                                                            R);
}

/// Matches an add nuw of \p L and \p R.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for an add nuw of \p L and \p R.
template <typename LHS, typename RHS>
inline OverflowingBinaryOp_match<LHS, RHS, Instruction::Add,
                                 OverflowingBinaryOperator::NoUnsignedWrap>
m_NUWAdd(const LHS &L, const RHS &R) {
  return OverflowingBinaryOp_match<LHS, RHS, Instruction::Add,
                                   OverflowingBinaryOperator::NoUnsignedWrap>(
      L, R);
}

/// Matches an add nuw of \p L and \p R in either order.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for an add nuw of \p L and \p R in either order.
template <typename LHS, typename RHS>
inline OverflowingBinaryOp_match<
    LHS, RHS, Instruction::Add, OverflowingBinaryOperator::NoUnsignedWrap, true>
m_c_NUWAdd(const LHS &L, const RHS &R) {
  return OverflowingBinaryOp_match<LHS, RHS, Instruction::Add,
                                   OverflowingBinaryOperator::NoUnsignedWrap,
                                   true>(L, R);
}

/// Matches a sub nuw of \p L and \p R.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for a sub nuw of \p L and \p R.
template <typename LHS, typename RHS>
inline OverflowingBinaryOp_match<LHS, RHS, Instruction::Sub,
                                 OverflowingBinaryOperator::NoUnsignedWrap>
m_NUWSub(const LHS &L, const RHS &R) {
  return OverflowingBinaryOp_match<LHS, RHS, Instruction::Sub,
                                   OverflowingBinaryOperator::NoUnsignedWrap>(
      L, R);
}
/// Matches a mul nuw of \p L and \p R.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for a mul nuw of \p L and \p R.
template <typename LHS, typename RHS>
inline OverflowingBinaryOp_match<LHS, RHS, Instruction::Mul,
                                 OverflowingBinaryOperator::NoUnsignedWrap>
m_NUWMul(const LHS &L, const RHS &R) {
  return OverflowingBinaryOp_match<LHS, RHS, Instruction::Mul,
                                   OverflowingBinaryOperator::NoUnsignedWrap>(
      L, R);
}
/// Matches a shl nuw of \p L and \p R.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for a shl nuw of \p L and \p R.
template <typename LHS, typename RHS>
inline OverflowingBinaryOp_match<LHS, RHS, Instruction::Shl,
                                 OverflowingBinaryOperator::NoUnsignedWrap>
m_NUWShl(const LHS &L, const RHS &R) {
  return OverflowingBinaryOp_match<LHS, RHS, Instruction::Shl,
                                   OverflowingBinaryOperator::NoUnsignedWrap>(
      L, R);
}

/// Matches a BinaryOperator whose opcode is supplied at construction time.
template <typename LHS_t, typename RHS_t, bool Commutable = false>
struct SpecificBinaryOp_match
    : public BinaryOp_match<LHS_t, RHS_t, 0, Commutable> {
  /// Opcode that must be matched.
  unsigned Opcode;

  /// Construct a matcher for \p Opcode and both operands.
  ///
  /// \param Opcode Instruction opcode to require.
  /// \param LHS Sub-pattern for the left-hand operand.
  /// \param RHS Sub-pattern for the right-hand operand.
  SpecificBinaryOp_match(unsigned Opcode, const LHS_t &LHS, const RHS_t &RHS)
      : BinaryOp_match<LHS_t, RHS_t, 0, Commutable>(LHS, RHS), Opcode(Opcode) {}

  /// Match \p V as a BinaryOperator with Opcode and matching operands.
  ///
  /// \param V Value to match.
  /// \return True if the match succeeds.
  template <typename OpTy> bool match(OpTy *V) const {
    return BinaryOp_match<LHS_t, RHS_t, 0, Commutable>::match(Opcode, V);
  }
};

/// Matches a specific opcode.
///
/// \param Opcode Instruction opcode to require.
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for a specific opcode.
template <typename LHS, typename RHS>
inline SpecificBinaryOp_match<LHS, RHS> m_BinOp(unsigned Opcode, const LHS &L,
                                                const RHS &R) {
  return SpecificBinaryOp_match<LHS, RHS>(Opcode, L, R);
}

/// Matches an or marked disjoint against operand patterns.
template <typename LHS, typename RHS, bool Commutable = false>
struct DisjointOr_match {
  /// Sub-pattern for the left-hand operand.
  LHS L;
  /// Sub-pattern for the right-hand operand.
  RHS R;

  /// Construct a matcher for both operands.
  ///
  /// \param L Sub-pattern for the left-hand operand.
  /// \param R Sub-pattern for the right-hand operand.
  DisjointOr_match(const LHS &L, const RHS &R) : L(L), R(R) {}

  /// Match \p V as a disjoint or with matching operands.
  ///
  /// \param V Value to match.
  /// \return True if the match succeeds.
  template <typename OpTy> bool match(OpTy *V) const {
    if (auto *PDI = dyn_cast<PossiblyDisjointInst>(V)) {
      assert(PDI->getOpcode() == Instruction::Or && "Only or can be disjoint");
      if (!PDI->isDisjoint())
        return false;
      return (L.match(PDI->getOperand(0)) && R.match(PDI->getOperand(1))) ||
             (Commutable && L.match(PDI->getOperand(1)) &&
              R.match(PDI->getOperand(0)));
    }
    return false;
  }
};

/// Matches a disjoint or of \p L and \p R.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for a disjoint or of \p L and \p R.
template <typename LHS, typename RHS>
inline DisjointOr_match<LHS, RHS> m_DisjointOr(const LHS &L, const RHS &R) {
  return DisjointOr_match<LHS, RHS>(L, R);
}

/// Matches a disjoint or of \p L and \p R in either order.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for a disjoint or of \p L and \p R in either order.
template <typename LHS, typename RHS>
inline DisjointOr_match<LHS, RHS, true> m_c_DisjointOr(const LHS &L,
                                                       const RHS &R) {
  return DisjointOr_match<LHS, RHS, true>(L, R);
}

/// Match either "add" or "or disjoint".
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for either "add" or "or disjoint".
template <typename LHS, typename RHS>
inline match_combine_or<BinaryOp_match<LHS, RHS, Instruction::Add>,
                        DisjointOr_match<LHS, RHS>>
m_AddLike(const LHS &L, const RHS &R) {
  return m_CombineOr(m_Add(L, R), m_DisjointOr(L, R));
}

/// Match either "add nsw" or "or disjoint".
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for either "add nsw" or "or disjoint".
template <typename LHS, typename RHS>
inline match_combine_or<
    OverflowingBinaryOp_match<LHS, RHS, Instruction::Add,
                              OverflowingBinaryOperator::NoSignedWrap>,
    DisjointOr_match<LHS, RHS>>
m_NSWAddLike(const LHS &L, const RHS &R) {
  return m_CombineOr(m_NSWAdd(L, R), m_DisjointOr(L, R));
}

/// Match either "add nuw" or "or disjoint".
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for either "add nuw" or "or disjoint".
template <typename LHS, typename RHS>
inline match_combine_or<
    OverflowingBinaryOp_match<LHS, RHS, Instruction::Add,
                              OverflowingBinaryOperator::NoUnsignedWrap>,
    DisjointOr_match<LHS, RHS>>
m_NUWAddLike(const LHS &L, const RHS &R) {
  return m_CombineOr(m_NUWAdd(L, R), m_DisjointOr(L, R));
}

/// Matches xor or a nuw sub of a low-bit mask that behaves like xor.
template <typename LHS, typename RHS>
struct XorLike_match {
  /// Sub-pattern for one operand.
  LHS L;
  /// Sub-pattern for the other operand.
  RHS R;

  /// Construct a matcher for both operands.
  ///
  /// \param L Sub-pattern for one operand.
  /// \param R Sub-pattern for the other operand.
  XorLike_match(const LHS &L, const RHS &R) : L(L), R(R) {}

  /// Match \p V as xor-like with matching operands.
  ///
  /// \param V Value to match.
  /// \return True if the match succeeds.
  template <typename OpTy> bool match(OpTy *V) const {
    if (auto *Op = dyn_cast<BinaryOperator>(V)) {
      if (Op->getOpcode() == Instruction::Sub && Op->hasNoUnsignedWrap() &&
          PatternMatch::match(Op->getOperand(0), m_LowBitMask()))
		  ; // Pass
      else if (Op->getOpcode() != Instruction::Xor)
        return false;
      return (L.match(Op->getOperand(0)) && R.match(Op->getOperand(1))) ||
             (L.match(Op->getOperand(1)) && R.match(Op->getOperand(0)));
    }
    return false;
  }
};

/// Match either `(xor L, R)`, `(xor R, L)` or `(sub nuw R, L)` iff `R.isMask()`.
///
/// Only commutative matcher as the `sub` will need to swap the L and R.
///
/// \param L Sub-pattern for one operand.
/// \param R Sub-pattern for the other operand.
/// \return A matcher for either `(xor L, R)`, `(xor R, L)` or `(sub nuw R, L)` iff `R.isMask()`.
template <typename LHS, typename RHS>
inline auto m_c_XorLike(const LHS &L, const RHS &R) {
  return XorLike_match<LHS, RHS>(L, R);
}

//===----------------------------------------------------------------------===//
// Class that matches a group of binary opcodes.
//
/// Matches a binary instruction whose opcode satisfies Predicate.
template <typename LHS_t, typename RHS_t, typename Predicate,
          bool Commutable = false>
struct BinOpPred_match : Predicate {
  /// Sub-pattern for the left-hand operand.
  LHS_t L;
  /// Sub-pattern for the right-hand operand.
  RHS_t R;

  /// Construct a matcher for both operands.
  ///
  /// \param LHS Sub-pattern for the left-hand operand.
  /// \param RHS Sub-pattern for the right-hand operand.
  BinOpPred_match(const LHS_t &LHS, const RHS_t &RHS) : L(LHS), R(RHS) {}

  /// Match \p V if its opcode passes Predicate and operands match.
  ///
  /// \param V Value to match.
  /// \return True if the match succeeds.
  template <typename OpTy> bool match(OpTy *V) const {
    if (auto *I = dyn_cast<Instruction>(V))
      return this->isOpType(I->getOpcode()) &&
             ((L.match(I->getOperand(0)) && R.match(I->getOperand(1))) ||
              (Commutable && L.match(I->getOperand(1)) &&
               R.match(I->getOperand(0))));
    return false;
  }
};

/// Predicate true for shift opcodes.
struct is_shift_op {
  /// Return whether \p Opcode is a shift.
  ///
  /// \param Opcode Instruction opcode to test.
  /// \return True if the value satisfies the predicate.
  bool isOpType(unsigned Opcode) const { return Instruction::isShift(Opcode); }
};

/// Predicate true for logical or arithmetic right shifts.
struct is_right_shift_op {
  /// Return whether \p Opcode is lshr or ashr.
  ///
  /// \param Opcode Instruction opcode to test.
  /// \return True if the value satisfies the predicate.
  bool isOpType(unsigned Opcode) const {
    return Opcode == Instruction::LShr || Opcode == Instruction::AShr;
  }
};

/// Predicate true for logical shifts (shl or lshr).
struct is_logical_shift_op {
  /// Return whether \p Opcode is shl or lshr.
  ///
  /// \param Opcode Instruction opcode to test.
  /// \return True if the value satisfies the predicate.
  bool isOpType(unsigned Opcode) const {
    return Opcode == Instruction::LShr || Opcode == Instruction::Shl;
  }
};

/// Predicate true for bitwise logic opcodes (and/or/xor).
struct is_bitwiselogic_op {
  /// Return whether \p Opcode is a bitwise logic op.
  ///
  /// \param Opcode Instruction opcode to test.
  /// \return True if the value satisfies the predicate.
  bool isOpType(unsigned Opcode) const {
    return Instruction::isBitwiseLogicOp(Opcode);
  }
};

/// Predicate true for integer division opcodes.
struct is_idiv_op {
  /// Return whether \p Opcode is sdiv or udiv.
  ///
  /// \param Opcode Instruction opcode to test.
  /// \return True if the value satisfies the predicate.
  bool isOpType(unsigned Opcode) const {
    return Opcode == Instruction::SDiv || Opcode == Instruction::UDiv;
  }
};

/// Predicate true for integer remainder opcodes.
struct is_irem_op {
  /// Return whether \p Opcode is srem or urem.
  ///
  /// \param Opcode Instruction opcode to test.
  /// \return True if the value satisfies the predicate.
  bool isOpType(unsigned Opcode) const {
    return Opcode == Instruction::SRem || Opcode == Instruction::URem;
  }
};

/// Matches shift operations.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for shift operations.
template <typename LHS, typename RHS>
inline BinOpPred_match<LHS, RHS, is_shift_op> m_Shift(const LHS &L,
                                                      const RHS &R) {
  return BinOpPred_match<LHS, RHS, is_shift_op>(L, R);
}

/// Matches right-shift operations.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for right-shift operations.
template <typename LHS, typename RHS>
inline BinOpPred_match<LHS, RHS, is_right_shift_op> m_Shr(const LHS &L,
                                                          const RHS &R) {
  return BinOpPred_match<LHS, RHS, is_right_shift_op>(L, R);
}

/// Matches logical shift operations.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for logical shift operations.
template <typename LHS, typename RHS>
inline BinOpPred_match<LHS, RHS, is_logical_shift_op>
m_LogicalShift(const LHS &L, const RHS &R) {
  return BinOpPred_match<LHS, RHS, is_logical_shift_op>(L, R);
}

/// Matches bitwise logic operations.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for bitwise logic operations.
template <typename LHS, typename RHS>
inline BinOpPred_match<LHS, RHS, is_bitwiselogic_op>
m_BitwiseLogic(const LHS &L, const RHS &R) {
  return BinOpPred_match<LHS, RHS, is_bitwiselogic_op>(L, R);
}

/// Matches bitwise logic operations in either order.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for bitwise logic operations in either order.
template <typename LHS, typename RHS>
inline BinOpPred_match<LHS, RHS, is_bitwiselogic_op, true>
m_c_BitwiseLogic(const LHS &L, const RHS &R) {
  return BinOpPred_match<LHS, RHS, is_bitwiselogic_op, true>(L, R);
}

/// Matches integer division operations.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for integer division operations.
template <typename LHS, typename RHS>
inline BinOpPred_match<LHS, RHS, is_idiv_op> m_IDiv(const LHS &L,
                                                    const RHS &R) {
  return BinOpPred_match<LHS, RHS, is_idiv_op>(L, R);
}

/// Matches integer remainder operations.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for integer remainder operations.
template <typename LHS, typename RHS>
inline BinOpPred_match<LHS, RHS, is_irem_op> m_IRem(const LHS &L,
                                                    const RHS &R) {
  return BinOpPred_match<LHS, RHS, is_irem_op>(L, R);
}

//===----------------------------------------------------------------------===//
// Class that matches exact binary ops.
//
/// Matches a possibly-exact operator that is marked exact.
template <typename SubPattern_t> struct Exact_match {
  /// Nested sub-pattern applied after the exactness check.
  SubPattern_t SubPattern;

  /// Construct an exactness wrapper around \p SP.
  ///
  /// \param SP Nested sub-pattern to apply after the exact check.
  Exact_match(const SubPattern_t &SP) : SubPattern(SP) {}

  /// Match \p V if it is exact and SubPattern matches.
  ///
  /// \param V Value to match.
  /// \return True if the match succeeds.
  template <typename OpTy> bool match(OpTy *V) const {
    if (auto *PEO = dyn_cast<PossiblyExactOperator>(V))
      return PEO->isExact() && SubPattern.match(V);
    return false;
  }
};

/// Matches \p SubPattern only when the operator is marked exact.
///
/// \param SubPattern Nested sub-pattern to apply after the exact check.
/// \return A matcher for \p SubPattern only when the operator is marked exact.
template <typename T> inline Exact_match<T> m_Exact(const T &SubPattern) {
  return SubPattern;
}

//===----------------------------------------------------------------------===//
// Matchers for CmpInst classes
//

/// Matches a compare instruction of Class against operand patterns.
template <typename LHS_t, typename RHS_t, typename Class,
          bool Commutable = false>
struct CmpClass_match {
  /// Optional output that receives the matched compare predicate.
  CmpPredicate *Predicate;
  /// Sub-pattern for the left-hand operand.
  LHS_t L;
  /// Sub-pattern for the right-hand operand.
  RHS_t R;

  // The evaluation order is always stable, regardless of Commutability.
  // The LHS is always matched first.
  /// Construct a matcher that also binds the compare predicate.
  ///
  /// \param Pred Output that receives the matched predicate.
  /// \param LHS Sub-pattern for the left-hand operand.
  /// \param RHS Sub-pattern for the right-hand operand.
  CmpClass_match(CmpPredicate &Pred, const LHS_t &LHS, const RHS_t &RHS)
      : Predicate(&Pred), L(LHS), R(RHS) {}
  /// Construct a matcher that ignores the compare predicate.
  ///
  /// \param LHS Sub-pattern for the left-hand operand.
  /// \param RHS Sub-pattern for the right-hand operand.
  CmpClass_match(const LHS_t &LHS, const RHS_t &RHS)
      : Predicate(nullptr), L(LHS), R(RHS) {}

  /// Match \p V as Class with matching operands, optionally binding Predicate.
  ///
  /// \param V Value to match.
  /// \return True if the match succeeds.
  template <typename OpTy> bool match(OpTy *V) const {
    if (auto *I = dyn_cast<Class>(V)) {
      if (L.match(I->getOperand(0)) && R.match(I->getOperand(1))) {
        if (Predicate)
          *Predicate = CmpPredicate::get(I);
        return true;
      }
      if (Commutable && L.match(I->getOperand(1)) &&
          R.match(I->getOperand(0))) {
        if (Predicate)
          *Predicate = CmpPredicate::getSwapped(I);
        return true;
      }
    }
    return false;
  }
};

/// Matches any compare of \p L and \p R, binding the predicate to \p Pred.
///
/// \param Pred Output that receives the matched predicate.
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for any compare of \p L and \p R, binding the predicate to \p Pred.
template <typename LHS, typename RHS>
inline CmpClass_match<LHS, RHS, CmpInst> m_Cmp(CmpPredicate &Pred, const LHS &L,
                                               const RHS &R) {
  return CmpClass_match<LHS, RHS, CmpInst>(Pred, L, R);
}

/// Matches an icmp of \p L and \p R, binding the predicate to \p Pred.
///
/// \param Pred Output that receives the matched predicate.
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for an icmp of \p L and \p R, binding the predicate to \p Pred.
template <typename LHS, typename RHS>
inline CmpClass_match<LHS, RHS, ICmpInst> m_ICmp(CmpPredicate &Pred,
                                                 const LHS &L, const RHS &R) {
  return CmpClass_match<LHS, RHS, ICmpInst>(Pred, L, R);
}

/// Matches an fcmp of \p L and \p R, binding the predicate to \p Pred.
///
/// \param Pred Output that receives the matched predicate.
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for an fcmp of \p L and \p R, binding the predicate to \p Pred.
template <typename LHS, typename RHS>
inline CmpClass_match<LHS, RHS, FCmpInst> m_FCmp(CmpPredicate &Pred,
                                                 const LHS &L, const RHS &R) {
  return CmpClass_match<LHS, RHS, FCmpInst>(Pred, L, R);
}

/// Matches any compare of \p L and \p R.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for any compare of \p L and \p R.
template <typename LHS, typename RHS>
inline CmpClass_match<LHS, RHS, CmpInst> m_Cmp(const LHS &L, const RHS &R) {
  return CmpClass_match<LHS, RHS, CmpInst>(L, R);
}

/// Matches an icmp of \p L and \p R.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for an icmp of \p L and \p R.
template <typename LHS, typename RHS>
inline CmpClass_match<LHS, RHS, ICmpInst> m_ICmp(const LHS &L, const RHS &R) {
  return CmpClass_match<LHS, RHS, ICmpInst>(L, R);
}

/// Matches an fcmp of \p L and \p R.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for an fcmp of \p L and \p R.
template <typename LHS, typename RHS>
inline CmpClass_match<LHS, RHS, FCmpInst> m_FCmp(const LHS &L, const RHS &R) {
  return CmpClass_match<LHS, RHS, FCmpInst>(L, R);
}

// Same as CmpClass, but instead of saving Pred as out output variable, match a
// specific input pred for equality.
/// Matches a compare of Class with a required predicate and operands.
template <typename LHS_t, typename RHS_t, typename Class,
          bool Commutable = false>
struct SpecificCmpClass_match {
  /// Predicate that must match.
  const CmpPredicate Predicate;
  /// Sub-pattern for the left-hand operand.
  LHS_t L;
  /// Sub-pattern for the right-hand operand.
  RHS_t R;

  /// Construct a matcher for \p Pred and both operands.
  ///
  /// \param Pred Compare predicate that must match.
  /// \param LHS Sub-pattern for the left-hand operand.
  /// \param RHS Sub-pattern for the right-hand operand.
  SpecificCmpClass_match(CmpPredicate Pred, const LHS_t &LHS, const RHS_t &RHS)
      : Predicate(Pred), L(LHS), R(RHS) {}

  /// Match \p V if predicate and operands succeed.
  ///
  /// \param V Value to match.
  /// \return True if the match succeeds.
  template <typename OpTy> bool match(OpTy *V) const {
    if (auto *I = dyn_cast<Class>(V)) {
      if (CmpPredicate::getMatching(CmpPredicate::get(I), Predicate) &&
          L.match(I->getOperand(0)) && R.match(I->getOperand(1)))
        return true;
      if constexpr (Commutable) {
        if (CmpPredicate::getMatching(CmpPredicate::get(I),
                                      CmpPredicate::getSwapped(Predicate)) &&
            L.match(I->getOperand(1)) && R.match(I->getOperand(0)))
          return true;
      }
    }

    return false;
  }
};

/// Matches any compare of \p L and \p R with predicate \p MatchPred.
///
/// \param MatchPred Compare predicate that must match.
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for any compare of \p L and \p R with predicate \p MatchPred.
template <typename LHS, typename RHS>
inline SpecificCmpClass_match<LHS, RHS, CmpInst>
m_SpecificCmp(CmpPredicate MatchPred, const LHS &L, const RHS &R) {
  return SpecificCmpClass_match<LHS, RHS, CmpInst>(MatchPred, L, R);
}

/// Matches an icmp of \p L and \p R with predicate \p MatchPred.
///
/// \param MatchPred Compare predicate that must match.
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for an icmp of \p L and \p R with predicate \p MatchPred.
template <typename LHS, typename RHS>
inline SpecificCmpClass_match<LHS, RHS, ICmpInst>
m_SpecificICmp(CmpPredicate MatchPred, const LHS &L, const RHS &R) {
  return SpecificCmpClass_match<LHS, RHS, ICmpInst>(MatchPred, L, R);
}

/// Matches an icmp of \p L and \p R with \p MatchPred in either order.
///
/// \param MatchPred Compare predicate that must match.
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for an icmp of \p L and \p R with \p MatchPred in either order.
template <typename LHS, typename RHS>
inline SpecificCmpClass_match<LHS, RHS, ICmpInst, true>
m_c_SpecificICmp(CmpPredicate MatchPred, const LHS &L, const RHS &R) {
  return SpecificCmpClass_match<LHS, RHS, ICmpInst, true>(MatchPred, L, R);
}

/// Matches an fcmp of \p L and \p R with predicate \p MatchPred.
///
/// \param MatchPred Compare predicate that must match.
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for an fcmp of \p L and \p R with predicate \p MatchPred.
template <typename LHS, typename RHS>
inline SpecificCmpClass_match<LHS, RHS, FCmpInst>
m_SpecificFCmp(CmpPredicate MatchPred, const LHS &L, const RHS &R) {
  return SpecificCmpClass_match<LHS, RHS, FCmpInst>(MatchPred, L, R);
}

//===----------------------------------------------------------------------===//
// Matchers for instructions with a given opcode and number of operands.
//

/// Matches instructions with Opcode and one operand.
template <typename T0, unsigned Opcode> struct OneOps_match {
  /// Sub-pattern for the first operand.
  T0 Op1;

  /// Construct a matcher for the single operand.
  ///
  /// \param Op1 Sub-pattern for the first operand.
  OneOps_match(const T0 &Op1) : Op1(Op1) {}

  /// Match \p V if its opcode is Opcode and operand 0 matches Op1.
  ///
  /// \param V Value to match.
  /// \return True if the match succeeds.
  template <typename OpTy> bool match(OpTy *V) const {
    if (V->getValueID() == Value::InstructionVal + Opcode) {
      auto *I = cast<Instruction>(V);
      return Op1.match(I->getOperand(0));
    }
    return false;
  }
};

/// Matches instructions with Opcode and two operands.
template <typename T0, typename T1, unsigned Opcode> struct TwoOps_match {
  /// Sub-pattern for the first operand.
  T0 Op1;
  /// Sub-pattern for the second operand.
  T1 Op2;

  /// Construct a matcher for both operands.
  ///
  /// \param Op1 Sub-pattern for the first operand.
  /// \param Op2 Sub-pattern for the second operand.
  TwoOps_match(const T0 &Op1, const T1 &Op2) : Op1(Op1), Op2(Op2) {}

  /// Match \p V if its opcode is Opcode and both operands match.
  ///
  /// \param V Value to match.
  /// \return True if the match succeeds.
  template <typename OpTy> bool match(OpTy *V) const {
    if (V->getValueID() == Value::InstructionVal + Opcode) {
      auto *I = cast<Instruction>(V);
      return Op1.match(I->getOperand(0)) && Op2.match(I->getOperand(1));
    }
    return false;
  }
};

/// Matches instructions with Opcode and three operands.
template <typename T0, typename T1, typename T2, unsigned Opcode,
          bool CommutableOp2Op3 = false>
struct ThreeOps_match {
  /// Sub-pattern for the first operand.
  T0 Op1;
  /// Sub-pattern for the second operand.
  T1 Op2;
  /// Sub-pattern for the third operand.
  T2 Op3;

  /// Construct a matcher for all three operands.
  ///
  /// \param Op1 Sub-pattern for the first operand.
  /// \param Op2 Sub-pattern for the second operand.
  /// \param Op3 Sub-pattern for the third operand.
  ThreeOps_match(const T0 &Op1, const T1 &Op2, const T2 &Op3)
      : Op1(Op1), Op2(Op2), Op3(Op3) {}

  /// Match \p V if its opcode is Opcode and all operands match.
  ///
  /// \param V Value to match.
  /// \return True if the match succeeds.
  template <typename OpTy> bool match(OpTy *V) const {
    if (V->getValueID() == Value::InstructionVal + Opcode) {
      auto *I = cast<Instruction>(V);
      if (!Op1.match(I->getOperand(0)))
        return false;
      if (Op2.match(I->getOperand(1)) && Op3.match(I->getOperand(2)))
        return true;
      return CommutableOp2Op3 && Op2.match(I->getOperand(2)) &&
             Op3.match(I->getOperand(1));
    }
    return false;
  }
};

/// Matches instructions with Opcode and any number of operands.
template <unsigned Opcode, typename... OperandTypes> struct AnyOps_match {
  /// Tuple of sub-patterns for each operand.
  std::tuple<OperandTypes...> Operands;

  /// Construct a matcher from one sub-pattern per operand.
  ///
  /// \param Ops Sub-patterns for each operand, in order.
  AnyOps_match(const OperandTypes &...Ops) : Operands(Ops...) {}

  // Operand matching works by recursively calling match_operands, matching the
  // operands left to right. The first version is called for each operand but
  // the last, for which the second version is called. The second version of
  // match_operands is also used to match each individual operand.
  /// Recursively match operands from Idx through Last-1, then Last.
  ///
  /// \param I Instruction whose operands are matched.
  /// \return True if the operands match.
  template <int Idx, int Last>
  std::enable_if_t<Idx != Last, bool>
  match_operands(const Instruction *I) const {
    return match_operands<Idx, Idx>(I) && match_operands<Idx + 1, Last>(I);
  }

  /// Match operand Idx of \p I against the corresponding sub-pattern.
  ///
  /// \param I Instruction whose operands are matched.
  /// \return True if the operands match.
  template <int Idx, int Last>
  std::enable_if_t<Idx == Last, bool>
  match_operands(const Instruction *I) const {
    return std::get<Idx>(Operands).match(I->getOperand(Idx));
  }

  /// Match \p V if its opcode is Opcode and all operand patterns succeed.
  ///
  /// \param V Value to match.
  /// \return True if the match succeeds.
  template <typename OpTy> bool match(OpTy *V) const {
    if (V->getValueID() == Value::InstructionVal + Opcode) {
      auto *I = cast<Instruction>(V);
      return I->getNumOperands() == sizeof...(OperandTypes) &&
             match_operands<0, sizeof...(OperandTypes) - 1>(I);
    }
    return false;
  }
};

/// Matches SelectInst.
///
/// \param C Sub-pattern for the condition.
/// \param L Sub-pattern for the true value.
/// \param R Sub-pattern for the false value.
/// \return A matcher for SelectInst.
template <typename Cond, typename LHS, typename RHS>
inline ThreeOps_match<Cond, LHS, RHS, Instruction::Select>
m_Select(const Cond &C, const LHS &L, const RHS &R) {
  return ThreeOps_match<Cond, LHS, RHS, Instruction::Select>(C, L, R);
}

/// Matches a select of two integer constants.
///
/// For example: m_SelectCst<-1, 0>(m_Value(V)).
///
/// \param C Sub-pattern for the condition.
/// \return A matcher for a select of two integer constants.
template <int64_t L, int64_t R, typename Cond>
inline ThreeOps_match<Cond, constantint_match<L>, constantint_match<R>,
                      Instruction::Select>
m_SelectCst(const Cond &C) {
  return m_Select(C, m_ConstantInt<L>(), m_ConstantInt<R>());
}

/// Match Select(C, LHS, RHS) or Select(C, RHS, LHS).
///
/// \param L Sub-pattern for one selected value.
/// \param R Sub-pattern for the other selected value.
/// \return A matcher for Select(C, LHS, RHS) or Select(C, RHS, LHS).
template <typename LHS, typename RHS>
inline ThreeOps_match<decltype(m_Value()), LHS, RHS, Instruction::Select, true>
m_c_Select(const LHS &L, const RHS &R) {
  return ThreeOps_match<decltype(m_Value()), LHS, RHS, Instruction::Select,
                        true>(m_Value(), L, R);
}

/// Matches FreezeInst.
///
/// \param Op Sub-pattern for the frozen operand.
/// \return A matcher for FreezeInst.
template <typename OpTy>
inline OneOps_match<OpTy, Instruction::Freeze> m_Freeze(const OpTy &Op) {
  return OneOps_match<OpTy, Instruction::Freeze>(Op);
}

/// Matches InsertElementInst.
///
/// \param Val Sub-pattern for the vector value.
/// \param Elt Sub-pattern for the inserted element.
/// \param Idx Sub-pattern for the insert index.
/// \return A matcher for InsertElementInst.
template <typename Val_t, typename Elt_t, typename Idx_t>
inline ThreeOps_match<Val_t, Elt_t, Idx_t, Instruction::InsertElement>
m_InsertElt(const Val_t &Val, const Elt_t &Elt, const Idx_t &Idx) {
  return ThreeOps_match<Val_t, Elt_t, Idx_t, Instruction::InsertElement>(
      Val, Elt, Idx);
}

/// Matches ExtractElementInst.
///
/// \param Val Sub-pattern for the vector value.
/// \param Idx Sub-pattern for the extract index.
/// \return A matcher for ExtractElementInst.
template <typename Val_t, typename Idx_t>
inline TwoOps_match<Val_t, Idx_t, Instruction::ExtractElement>
m_ExtractElt(const Val_t &Val, const Idx_t &Idx) {
  return TwoOps_match<Val_t, Idx_t, Instruction::ExtractElement>(Val, Idx);
}

/// Matches a shufflevector with operand and mask patterns.
template <typename T0, typename T1, typename T2> struct Shuffle_match {
  /// Sub-pattern for the first vector operand.
  T0 Op1;
  /// Sub-pattern for the second vector operand.
  T1 Op2;
  /// Sub-pattern for the shuffle mask.
  T2 Mask;

  /// Construct a matcher for both vectors and the mask.
  ///
  /// \param Op1 Sub-pattern for the first vector operand.
  /// \param Op2 Sub-pattern for the second vector operand.
  /// \param Mask Sub-pattern for the shuffle mask.
  Shuffle_match(const T0 &Op1, const T1 &Op2, const T2 &Mask)
      : Op1(Op1), Op2(Op2), Mask(Mask) {}

  /// Match \p V as ShuffleVectorInst against Op1, Op2, and Mask.
  ///
  /// \param V Value to match.
  /// \return True if the match succeeds.
  template <typename OpTy> bool match(OpTy *V) const {
    if (auto *I = dyn_cast<ShuffleVectorInst>(V)) {
      return Op1.match(I->getOperand(0)) && Op2.match(I->getOperand(1)) &&
             Mask.match(I->getShuffleMask());
    }
    return false;
  }
};

/// Binds any shuffle mask to an ArrayRef output.
struct m_Mask {
  /// Output that receives the matched mask.
  ArrayRef<int> &MaskRef;
  /// Construct a binder for the shuffle mask.
  ///
  /// \param MaskRef Output that receives the matched mask.
  m_Mask(ArrayRef<int> &MaskRef) : MaskRef(MaskRef) {}
  /// Bind \p Mask into MaskRef and succeed.
  ///
  /// \param Mask Shuffle mask to bind.
  /// \return True if the match succeeds.
  bool match(ArrayRef<int> Mask) const {
    MaskRef = Mask;
    return true;
  }
};

/// Matches a shuffle mask whose elements are all 0 or poison (-1).
struct m_ZeroMask {
  /// Return whether every element of \p Mask is 0 or -1.
  ///
  /// \param Mask Shuffle mask to test.
  /// \return True if the match succeeds.
  bool match(ArrayRef<int> Mask) const {
    return all_of(Mask, [](int Elem) { return Elem == 0 || Elem == -1; });
  }
};

/// Matches a shuffle mask equal to a specific ArrayRef.
struct m_SpecificMask {
  /// Expected shuffle mask.
  ArrayRef<int> Val;
  /// Construct a matcher for a specific mask.
  ///
  /// \param Val Expected shuffle mask.
  m_SpecificMask(ArrayRef<int> Val) : Val(Val) {}
  /// Return whether \p Mask equals Val.
  ///
  /// \param Mask Shuffle mask to test.
  /// \return True if the match succeeds.
  bool match(ArrayRef<int> Mask) const { return Val == Mask; }
};

/// Matches a shuffle mask where every element is equal.
struct m_SplatMask {
  /// Return whether every element of \p Mask is equal.
  ///
  /// \param Mask Shuffle mask to test.
  /// \return True if the match succeeds.
  bool match(ArrayRef<int> Mask) const { return all_equal(Mask); }
};

/// Matches a splat mask, allowing poison, and binds the splat index.
struct m_SplatOrPoisonMask {
  /// Output that receives the non-poison splat index.
  int &SplatIndex;
  /// Construct a matcher that binds the splat index.
  ///
  /// \param SplatIndex Output that receives the matched splat index.
  m_SplatOrPoisonMask(int &SplatIndex) : SplatIndex(SplatIndex) {}
  /// Match \p Mask if non-poison elements are equal, binding SplatIndex.
  ///
  /// \param Mask Shuffle mask to test.
  /// \return True if the match succeeds.
  bool match(ArrayRef<int> Mask) const {
    const auto *First = find_if(Mask, [](int Elem) { return Elem != -1; });
    if (First == Mask.end())
      return false;
    SplatIndex = *First;
    return all_of(Mask,
                  [First](int Elem) { return Elem == *First || Elem == -1; });
  }
};

/// Matches a GEP of i8 (ptradd) against pointer and offset patterns.
template <typename PointerOpTy, typename OffsetOpTy> struct PtrAdd_match {
  /// Sub-pattern for the pointer operand.
  PointerOpTy PointerOp;
  /// Sub-pattern for the byte offset.
  OffsetOpTy OffsetOp;

  /// Construct a matcher for the pointer and offset.
  ///
  /// \param PointerOp Sub-pattern for the pointer operand.
  /// \param OffsetOp Sub-pattern for the byte offset.
  PtrAdd_match(const PointerOpTy &PointerOp, const OffsetOpTy &OffsetOp)
      : PointerOp(PointerOp), OffsetOp(OffsetOp) {}

  /// Match \p V as an i8 GEP with matching pointer and offset.
  ///
  /// \param V Value to match.
  /// \return True if the match succeeds.
  template <typename OpTy> bool match(OpTy *V) const {
    auto *GEP = dyn_cast<GEPOperator>(V);
    return GEP && GEP->getSourceElementType()->isIntegerTy(8) &&
           PointerOp.match(GEP->getPointerOperand()) &&
           OffsetOp.match(GEP->idx_begin()->get());
  }
};

/// Matches ShuffleVectorInst independently of mask value.
///
/// \param v1 Sub-pattern for the first vector operand.
/// \param v2 Sub-pattern for the second vector operand.
/// \return A matcher for ShuffleVectorInst independently of mask value.
template <typename V1_t, typename V2_t>
inline TwoOps_match<V1_t, V2_t, Instruction::ShuffleVector>
m_Shuffle(const V1_t &v1, const V2_t &v2) {
  return TwoOps_match<V1_t, V2_t, Instruction::ShuffleVector>(v1, v2);
}

/// Matches a shufflevector of \p v1 and \p v2 with mask \p mask.
///
/// \param v1 Sub-pattern for the first vector operand.
/// \param v2 Sub-pattern for the second vector operand.
/// \param mask Sub-pattern for the shuffle mask.
/// \return A matcher for a shufflevector of \p v1 and \p v2 with mask \p mask.
template <typename V1_t, typename V2_t, typename Mask_t>
inline Shuffle_match<V1_t, V2_t, Mask_t>
m_Shuffle(const V1_t &v1, const V2_t &v2, const Mask_t &mask) {
  return Shuffle_match<V1_t, V2_t, Mask_t>(v1, v2, mask);
}

/// Matches LoadInst.
///
/// \param Op Sub-pattern for the pointer operand.
/// \return A matcher for LoadInst.
template <typename OpTy>
inline OneOps_match<OpTy, Instruction::Load> m_Load(const OpTy &Op) {
  return OneOps_match<OpTy, Instruction::Load>(Op);
}

/// Matches a simple (non-volatile, non-atomic) LoadInst.
template <typename OpTy> struct LoadSimple_match {
  /// Underlying load matcher for the pointer operand.
  OneOps_match<OpTy, Instruction::Load> Base;

  /// Construct a simple-load matcher for the pointer operand.
  ///
  /// \param Op Sub-pattern for the pointer operand.
  LoadSimple_match(const OpTy &Op) : Base(Op) {}

  /// Match \p V as a simple load whose pointer matches Base.
  ///
  /// \param V Value to match.
  /// \return True if the match succeeds.
  template <typename ITy> bool match(ITy *V) const {
    return Base.match(V) && cast<LoadInst>(V)->isSimple();
  }
};

/// Matches a simple (non-volatile, non-atomic) LoadInst.
///
/// \param Op Sub-pattern for the pointer operand.
/// \return A matcher for a simple (non-volatile, non-atomic) LoadInst.
template <typename OpTy>
inline LoadSimple_match<OpTy> m_LoadSimple(const OpTy &Op) {
  return LoadSimple_match<OpTy>(Op);
}

/// Matches StoreInst.
///
/// \param ValueOp Sub-pattern for the stored value.
/// \param PointerOp Sub-pattern for the pointer operand.
/// \return A matcher for StoreInst.
template <typename ValueOpTy, typename PointerOpTy>
inline TwoOps_match<ValueOpTy, PointerOpTy, Instruction::Store>
m_Store(const ValueOpTy &ValueOp, const PointerOpTy &PointerOp) {
  return TwoOps_match<ValueOpTy, PointerOpTy, Instruction::Store>(ValueOp,
                                                                  PointerOp);
}

/// Matches GetElementPtrInst.
///
/// \param Ops Sub-patterns for the GEP operands, in order.
/// \return A matcher for GetElementPtrInst.
template <typename... OperandTypes>
inline auto m_GEP(const OperandTypes &...Ops) {
  return AnyOps_match<Instruction::GetElementPtr, OperandTypes...>(Ops...);
}

/// Matches GEP with i8 source element type.
///
/// \param PointerOp Sub-pattern for the pointer operand.
/// \param OffsetOp Sub-pattern for the byte offset.
/// \return A matcher for GEP with i8 source element type.
template <typename PointerOpTy, typename OffsetOpTy>
inline PtrAdd_match<PointerOpTy, OffsetOpTy>
m_PtrAdd(const PointerOpTy &PointerOp, const OffsetOpTy &OffsetOp) {
  return PtrAdd_match<PointerOpTy, OffsetOpTy>(PointerOp, OffsetOp);
}

//===----------------------------------------------------------------------===//
// Matchers for CastInst classes
//

/// Matches a cast Operator with a fixed Opcode and operand pattern.
template <typename Op_t, unsigned Opcode> struct CastOperator_match {
  /// Operand matcher.
  Op_t Op;

  /// Construct from an operand matcher.
  /// \param OpMatch Operand matcher.
  CastOperator_match(const Op_t &OpMatch) : Op(OpMatch) {}

  /// Match V if it is an Operator with Opcode whose operand matches Op.
  /// \param V Value to match.
  /// \return True if the match succeeds.
  template <typename OpTy> bool match(OpTy *V) const {
    if (auto *O = dyn_cast<Operator>(V))
      return O->getOpcode() == Opcode && Op.match(O->getOperand(0));
    return false;
  }
};

/// Matches a CastInst of a specific Class with an operand pattern.
template <typename Op_t, typename Class> struct CastInst_match {
  /// Operand matcher.
  Op_t Op;

  /// Construct from an operand matcher.
  /// \param OpMatch Operand matcher.
  CastInst_match(const Op_t &OpMatch) : Op(OpMatch) {}

  /// Match V if it is Class and its operand matches Op.
  /// \param V Value to match.
  /// \return True if the match succeeds.
  template <typename OpTy> bool match(OpTy *V) const {
    if (auto *I = dyn_cast<Class>(V))
      return Op.match(I->getOperand(0));
    return false;
  }
};

/// Matches ptrtoint where the integer and pointer have the same bit width.
template <typename Op_t> struct PtrToIntSameSize_match {
  /// Data layout used to compare bit widths.
  const DataLayout &DL;
  /// Operand matcher.
  Op_t Op;

  /// Construct from a data layout and operand matcher.
  /// \param DL Data layout for size checks.
  /// \param OpMatch Operand matcher.
  PtrToIntSameSize_match(const DataLayout &DL, const Op_t &OpMatch)
      : DL(DL), Op(OpMatch) {}

  /// Match V if it is a same-size ptrtoint whose operand matches Op.
  /// \param V Value to match.
  /// \return True if the match succeeds.
  template <typename OpTy> bool match(OpTy *V) const {
    if (auto *O = dyn_cast<Operator>(V))
      return O->getOpcode() == Instruction::PtrToInt &&
             DL.getTypeSizeInBits(O->getType()) ==
                 DL.getTypeSizeInBits(O->getOperand(0)->getType()) &&
             Op.match(O->getOperand(0));
    return false;
  }
};

/// Matches a zext with the nneg flag set.
template <typename Op_t> struct NNegZExt_match {
  /// Operand matcher.
  Op_t Op;

  /// Construct from an operand matcher.
  /// \param OpMatch Operand matcher.
  NNegZExt_match(const Op_t &OpMatch) : Op(OpMatch) {}

  /// Match V if it is a non-negative zext whose operand matches Op.
  /// \param V Value to match.
  /// \return True if the match succeeds.
  template <typename OpTy> bool match(OpTy *V) const {
    if (auto *I = dyn_cast<ZExtInst>(V))
      return I->hasNonNeg() && Op.match(I->getOperand(0));
    return false;
  }
};

/// Matches a trunc with required no-wrap flags.
template <typename Op_t, unsigned WrapFlags = 0> struct NoWrapTrunc_match {
  /// Operand matcher.
  Op_t Op;

  /// Construct from an operand matcher.
  /// \param OpMatch Operand matcher.
  NoWrapTrunc_match(const Op_t &OpMatch) : Op(OpMatch) {}

  /// Match V if it is a trunc with WrapFlags whose operand matches Op.
  /// \param V Value to match.
  /// \return True if the match succeeds.
  template <typename OpTy> bool match(OpTy *V) const {
    if (auto *I = dyn_cast<TruncInst>(V))
      return (I->getNoWrapKind() & WrapFlags) == WrapFlags &&
             Op.match(I->getOperand(0));
    return false;
  }
};

/// Matches BitCast.
/// \param Op Operand matcher.
/// \return A matcher for BitCast.
template <typename OpTy>
inline CastOperator_match<OpTy, Instruction::BitCast>
m_BitCast(const OpTy &Op) {
  return CastOperator_match<OpTy, Instruction::BitCast>(Op);
}

/// Matches a bitcast that does not change vector element count or scalar/vector kind.
template <typename Op_t> struct ElementWiseBitCast_match {
  /// Operand matcher.
  Op_t Op;

  /// Construct from an operand matcher.
  /// \param OpMatch Operand matcher.
  ElementWiseBitCast_match(const Op_t &OpMatch) : Op(OpMatch) {}

  /// Match V if it is an element-wise bitcast whose operand matches Op.
  /// \param V Value to match.
  /// \return True if the match succeeds.
  template <typename OpTy> bool match(OpTy *V) const {
    auto *I = dyn_cast<BitCastInst>(V);
    if (!I)
      return false;
    Type *SrcType = I->getSrcTy();
    Type *DstType = I->getType();
    // Make sure the bitcast doesn't change between scalar and vector and
    // doesn't change the number of vector elements.
    if (SrcType->isVectorTy() != DstType->isVectorTy())
      return false;
    if (VectorType *SrcVecTy = dyn_cast<VectorType>(SrcType);
        SrcVecTy && SrcVecTy->getElementCount() !=
                        cast<VectorType>(DstType)->getElementCount())
      return false;
    return Op.match(I->getOperand(0));
  }
};

/// Matches an element-wise BitCast.
/// \param Op Operand matcher.
/// \return A matcher for an element-wise BitCast.
template <typename OpTy>
inline ElementWiseBitCast_match<OpTy> m_ElementWiseBitCast(const OpTy &Op) {
  return ElementWiseBitCast_match<OpTy>(Op);
}

/// Matches PtrToInt.
/// \param Op Operand matcher.
/// \return A matcher for PtrToInt.
template <typename OpTy>
inline CastOperator_match<OpTy, Instruction::PtrToInt>
m_PtrToInt(const OpTy &Op) {
  return CastOperator_match<OpTy, Instruction::PtrToInt>(Op);
}

/// Matches ptrtoint when the integer and pointer have the same size.
/// \param DL Data layout for size checks.
/// \param Op Operand matcher.
/// \return A matcher for ptrtoint when the integer and pointer have the same size.
template <typename OpTy>
inline PtrToIntSameSize_match<OpTy> m_PtrToIntSameSize(const DataLayout &DL,
                                                       const OpTy &Op) {
  return PtrToIntSameSize_match<OpTy>(DL, Op);
}

/// Matches PtrToAddr.
/// \param Op Operand matcher.
/// \return A matcher for PtrToAddr.
template <typename OpTy>
inline CastOperator_match<OpTy, Instruction::PtrToAddr>
m_PtrToAddr(const OpTy &Op) {
  return CastOperator_match<OpTy, Instruction::PtrToAddr>(Op);
}

/// Matches PtrToInt or PtrToAddr.
/// \param Op Operand matcher.
/// \return A matcher for PtrToInt or PtrToAddr.
template <typename OpTy> inline auto m_PtrToIntOrAddr(const OpTy &Op) {
  return m_CombineOr(m_PtrToInt(Op), m_PtrToAddr(Op));
}

/// Matches IntToPtr.
/// \param Op Operand matcher.
/// \return A matcher for IntToPtr.
template <typename OpTy>
inline CastOperator_match<OpTy, Instruction::IntToPtr>
m_IntToPtr(const OpTy &Op) {
  return CastOperator_match<OpTy, Instruction::IntToPtr>(Op);
}

/// Matches any cast or self. Used to ignore casts.
/// \param Op Operand matcher.
/// \return A matcher for any cast or self. Used to ignore casts.
template <typename OpTy>
inline match_combine_or<CastInst_match<OpTy, CastInst>, OpTy>
m_CastOrSelf(const OpTy &Op) {
  return m_CombineOr(CastInst_match<OpTy, CastInst>(Op), Op);
}

/// Matches Trunc.
/// \param Op Operand matcher.
/// \return A matcher for Trunc.
template <typename OpTy>
inline CastInst_match<OpTy, TruncInst> m_Trunc(const OpTy &Op) {
  return CastInst_match<OpTy, TruncInst>(Op);
}

/// Matches trunc nuw.
/// \param Op Operand matcher.
/// \return A matcher for trunc nuw.
template <typename OpTy>
inline NoWrapTrunc_match<OpTy, TruncInst::NoUnsignedWrap>
m_NUWTrunc(const OpTy &Op) {
  return NoWrapTrunc_match<OpTy, TruncInst::NoUnsignedWrap>(Op);
}

/// Matches trunc nsw.
/// \param Op Operand matcher.
/// \return A matcher for trunc nsw.
template <typename OpTy>
inline NoWrapTrunc_match<OpTy, TruncInst::NoSignedWrap>
m_NSWTrunc(const OpTy &Op) {
  return NoWrapTrunc_match<OpTy, TruncInst::NoSignedWrap>(Op);
}

/// Matches Trunc or the value itself.
/// \param Op Operand matcher.
/// \return A matcher for Trunc or the value itself.
template <typename OpTy>
inline match_combine_or<CastInst_match<OpTy, TruncInst>, OpTy>
m_TruncOrSelf(const OpTy &Op) {
  return m_CombineOr(m_Trunc(Op), Op);
}

/// Matches SExt.
/// \param Op Operand matcher.
/// \return A matcher for SExt.
template <typename OpTy>
inline CastInst_match<OpTy, SExtInst> m_SExt(const OpTy &Op) {
  return CastInst_match<OpTy, SExtInst>(Op);
}

/// Matches ZExt.
/// \param Op Operand matcher.
/// \return A matcher for ZExt.
template <typename OpTy>
inline CastInst_match<OpTy, ZExtInst> m_ZExt(const OpTy &Op) {
  return CastInst_match<OpTy, ZExtInst>(Op);
}

/// Matches a zext with the nneg flag.
/// \param Op Operand matcher.
/// \return A matcher for a zext with the nneg flag.
template <typename OpTy>
inline NNegZExt_match<OpTy> m_NNegZExt(const OpTy &Op) {
  return NNegZExt_match<OpTy>(Op);
}

/// Matches ZExt or the value itself.
/// \param Op Operand matcher.
/// \return A matcher for ZExt or the value itself.
template <typename OpTy>
inline match_combine_or<CastInst_match<OpTy, ZExtInst>, OpTy>
m_ZExtOrSelf(const OpTy &Op) {
  return m_CombineOr(m_ZExt(Op), Op);
}

/// Matches SExt or the value itself.
/// \param Op Operand matcher.
/// \return A matcher for SExt or the value itself.
template <typename OpTy>
inline match_combine_or<CastInst_match<OpTy, SExtInst>, OpTy>
m_SExtOrSelf(const OpTy &Op) {
  return m_CombineOr(m_SExt(Op), Op);
}

/// Match either "sext" or "zext nneg".
/// \param Op Operand matcher.
/// \return A matcher for either "sext" or "zext nneg".
template <typename OpTy>
inline match_combine_or<CastInst_match<OpTy, SExtInst>, NNegZExt_match<OpTy>>
m_SExtLike(const OpTy &Op) {
  return m_CombineOr(m_SExt(Op), m_NNegZExt(Op));
}

/// Matches ZExt or SExt.
/// \param Op Operand matcher.
/// \return A matcher for ZExt or SExt.
template <typename OpTy>
inline match_combine_or<CastInst_match<OpTy, ZExtInst>,
                        CastInst_match<OpTy, SExtInst>>
m_ZExtOrSExt(const OpTy &Op) {
  return m_CombineOr(m_ZExt(Op), m_SExt(Op));
}

/// Matches ZExt, SExt, or the value itself.
/// \param Op Operand matcher.
/// \return A matcher for ZExt, SExt, or the value itself.
template <typename OpTy>
inline match_combine_or<match_combine_or<CastInst_match<OpTy, ZExtInst>,
                                         CastInst_match<OpTy, SExtInst>>,
                        OpTy>
m_ZExtOrSExtOrSelf(const OpTy &Op) {
  return m_CombineOr(m_ZExtOrSExt(Op), Op);
}

/// Matches ZExt, Trunc, or the value itself.
/// \param Op Operand matcher.
/// \return A matcher for ZExt, Trunc, or the value itself.
template <typename OpTy> inline auto m_ZExtOrTruncOrSelf(const OpTy &Op) {
  return m_CombineOr(m_ZExt(Op), m_Trunc(Op), Op);
}

/// Matches icmp or an equivalent trunc-nuw-to-i1 pattern.
template <typename LHS_t, typename RHS_t> struct ICmpLike_match {
  /// Captured compare predicate.
  CmpPredicate &Pred;
  /// Left-hand operand matcher.
  LHS_t L;
  /// Right-hand operand matcher.
  RHS_t R;

  /// Construct from a predicate reference and operand matchers.
  /// \param P Predicate to capture.
  /// \param Left Left-hand matcher.
  /// \param Right Right-hand matcher.
  ICmpLike_match(CmpPredicate &P, const LHS_t &Left, const RHS_t &Right)
      : Pred(P), L(Left), R(Right) {}

  /// Match V as icmp or as trunc nuw x to i1 (icmp ne x, 0).
  /// \param V Value to match.
  /// \return True if the match succeeds.
  template <typename OpTy> bool match(OpTy *V) const {
    if (PatternMatch::match(V, m_ICmp(Pred, L, R)))
      return true;
    Value *A;
    // trunc nuw x to i1 is equivalent to icmp ne x, 0
    if (V->getType()->isIntOrIntVectorTy(1) &&
        PatternMatch::match(V, m_NUWTrunc(m_Value(A))) && L.match(A) &&
        R.match(ConstantInt::getNullValue(A->getType()))) {
      Pred = ICmpInst::ICMP_NE;
      return true;
    }
    return false;
  }
};

/// Matches icmp or an equivalent trunc-nuw-to-i1 pattern.
/// \param Pred Predicate to capture.
/// \param L Left-hand matcher.
/// \param R Right-hand matcher.
/// \return A matcher for icmp or an equivalent trunc-nuw-to-i1 pattern.
template <typename LHS, typename RHS>
inline ICmpLike_match<LHS, RHS> m_ICmpLike(CmpPredicate &Pred, const LHS &L,
                                           const RHS &R) {
  return ICmpLike_match<LHS, RHS>(Pred, L, R);
}

/// Matches select or boolean zext/sext idioms equivalent to select.
template <typename CondTy, typename LTy, typename RTy> struct SelectLike_match {
  /// Condition matcher.
  CondTy Cond;
  /// True-arm constant matcher.
  LTy TrueC;
  /// False-arm constant matcher.
  RTy FalseC;

  /// Construct from condition and true/false constant matchers.
  /// \param C Condition matcher.
  /// \param TC True-arm matcher.
  /// \param FC False-arm matcher.
  SelectLike_match(const CondTy &C, const LTy &TC, const RTy &FC)
      : Cond(C), TrueC(TC), FalseC(FC) {}

  /// Match V as select or as zext/sext of an i1 condition.
  /// \param V Value to match.
  /// \return True if the match succeeds.
  template <typename OpTy> bool match(OpTy *V) const {
    // select(Cond, TrueC, FalseC) — captures both constants directly
    if (PatternMatch::match(V, m_Select(Cond, TrueC, FalseC)))
      return true;

    Type *Ty = V->getType();
    Value *CondV = nullptr;

    // zext(i1 Cond) is equivalent to select(Cond, 1, 0)
    if (PatternMatch::match(V, m_ZExt(m_Value(CondV))) &&
        CondV->getType()->isIntOrIntVectorTy(1) && Cond.match(CondV) &&
        TrueC.match(ConstantInt::get(Ty, 1)) &&
        FalseC.match(ConstantInt::get(Ty, 0)))
      return true;

    // sext(i1 Cond) is equivalent to select(Cond, -1, 0)
    if (PatternMatch::match(V, m_SExt(m_Value(CondV))) &&
        CondV->getType()->isIntOrIntVectorTy(1) && Cond.match(CondV) &&
        TrueC.match(Constant::getAllOnesValue(Ty)) &&
        FalseC.match(ConstantInt::get(Ty, 0)))
      return true;

    return false;
  }
};

/// Matches a boolean-controlled select or equivalent zext/sext.
///
/// Matches one of:
///   select i1 Cond, TrueC, FalseC
///   zext i1 Cond             (equivalent to select i1 Cond, 1, 0)
///   sext i1 Cond             (equivalent to select i1 Cond, -1, 0)
///
/// The condition is matched against \p Cond, and the true/false constants
/// against \p TrueC and \p FalseC respectively. For zext/sext, the synthetic
/// constants are bound to \p TrueC and \p FalseC via their matchers.
/// \param C Condition matcher.
/// \param TrueC True-arm constant matcher.
/// \param FalseC False-arm constant matcher.
/// \return A matcher for a boolean-controlled select or equivalent zext/sext.
template <typename CondTy, typename LTy, typename RTy>
inline SelectLike_match<CondTy, LTy, RTy>
m_SelectLike(const CondTy &C, const LTy &TrueC, const RTy &FalseC) {
  return SelectLike_match<CondTy, LTy, RTy>(C, TrueC, FalseC);
}

/// Matches UIToFP.
/// \param Op Operand matcher.
/// \return A matcher for UIToFP.
template <typename OpTy>
inline CastInst_match<OpTy, UIToFPInst> m_UIToFP(const OpTy &Op) {
  return CastInst_match<OpTy, UIToFPInst>(Op);
}

/// Matches SIToFP.
/// \param Op Operand matcher.
/// \return A matcher for SIToFP.
template <typename OpTy>
inline CastInst_match<OpTy, SIToFPInst> m_SIToFP(const OpTy &Op) {
  return CastInst_match<OpTy, SIToFPInst>(Op);
}

/// Matches UIToFP or SIToFP.
/// \param Op Operand matcher.
/// \return A matcher for UIToFP or SIToFP.
template <typename OpTy>
inline match_combine_or<CastInst_match<OpTy, UIToFPInst>,
                        CastInst_match<OpTy, SIToFPInst>>
m_IToFP(const OpTy &Op) {
  return m_CombineOr(m_UIToFP(Op), m_SIToFP(Op));
}

/// Matches FPToUI.
/// \param Op Operand matcher.
/// \return A matcher for FPToUI.
template <typename OpTy>
inline CastInst_match<OpTy, FPToUIInst> m_FPToUI(const OpTy &Op) {
  return CastInst_match<OpTy, FPToUIInst>(Op);
}

/// Matches FPToSI.
/// \param Op Operand matcher.
/// \return A matcher for FPToSI.
template <typename OpTy>
inline CastInst_match<OpTy, FPToSIInst> m_FPToSI(const OpTy &Op) {
  return CastInst_match<OpTy, FPToSIInst>(Op);
}

/// Matches FPToUI or FPToSI.
/// \param Op Operand matcher.
/// \return A matcher for FPToUI or FPToSI.
template <typename OpTy>
inline match_combine_or<CastInst_match<OpTy, FPToUIInst>,
                        CastInst_match<OpTy, FPToSIInst>>
m_FPToI(const OpTy &Op) {
  return m_CombineOr(m_FPToUI(Op), m_FPToSI(Op));
}

/// Matches FPTrunc.
/// \param Op Operand matcher.
/// \return A matcher for FPTrunc.
template <typename OpTy>
inline CastInst_match<OpTy, FPTruncInst> m_FPTrunc(const OpTy &Op) {
  return CastInst_match<OpTy, FPTruncInst>(Op);
}

/// Matches FPExt.
/// \param Op Operand matcher.
/// \return A matcher for FPExt.
template <typename OpTy>
inline CastInst_match<OpTy, FPExtInst> m_FPExt(const OpTy &Op) {
  return CastInst_match<OpTy, FPExtInst>(Op);
}

//===----------------------------------------------------------------------===//
// Matchers for control flow.
//

/// Matches an unconditional branch and captures its successor.
struct br_match {
  /// Captured successor basic block.
  BasicBlock *&Succ;

  /// Construct from a successor binding.
  /// \param Succ Successor to capture.
  br_match(BasicBlock *&Succ) : Succ(Succ) {}

  /// Match V if it is an unconditional branch.
  /// \param V Value to match.
  /// \return True if the match succeeds.
  template <typename OpTy> bool match(OpTy *V) const {
    if (auto *BI = dyn_cast<UncondBrInst>(V)) {
      Succ = BI->getSuccessor();
      return true;
    }
    return false;
  }
};

/// Matches an unconditional branch.
/// \param Succ Successor to capture.
/// \return A matcher for an unconditional branch.
inline br_match m_UnconditionalBr(BasicBlock *&Succ) { return br_match(Succ); }

/// Matches a conditional branch with condition and successor patterns.
template <typename Cond_t, typename TrueBlock_t, typename FalseBlock_t>
struct brc_match {
  /// Condition matcher.
  Cond_t Cond;
  /// True-successor matcher.
  TrueBlock_t T;
  /// False-successor matcher.
  FalseBlock_t F;

  /// Construct from condition and successor matchers.
  /// \param C Condition matcher.
  /// \param t True-successor matcher.
  /// \param f False-successor matcher.
  brc_match(const Cond_t &C, const TrueBlock_t &t, const FalseBlock_t &f)
      : Cond(C), T(t), F(f) {}

  /// Match V if it is a conditional branch matching Cond, T, and F.
  /// \param V Value to match.
  /// \return True if the match succeeds.
  template <typename OpTy> bool match(OpTy *V) const {
    if (auto *BI = dyn_cast<CondBrInst>(V))
      if (Cond.match(BI->getCondition()))
        return T.match(BI->getSuccessor(0)) && F.match(BI->getSuccessor(1));
    return false;
  }
};

/// Matches a conditional branch, binding true and false successors.
/// \param C Condition matcher.
/// \param T True successor to capture.
/// \param F False successor to capture.
/// \return A matcher for a conditional branch, binding true and false successors.
template <typename Cond_t>
inline brc_match<Cond_t, match_bind<BasicBlock>, match_bind<BasicBlock>>
m_Br(const Cond_t &C, BasicBlock *&T, BasicBlock *&F) {
  return brc_match<Cond_t, match_bind<BasicBlock>, match_bind<BasicBlock>>(
      C, m_BasicBlock(T), m_BasicBlock(F));
}

/// Matches a conditional branch with custom successor matchers.
/// \param C Condition matcher.
/// \param T True-successor matcher.
/// \param F False-successor matcher.
/// \return A matcher for a conditional branch with custom successor matchers.
template <typename Cond_t, typename TrueBlock_t, typename FalseBlock_t>
inline brc_match<Cond_t, TrueBlock_t, FalseBlock_t>
m_Br(const Cond_t &C, const TrueBlock_t &T, const FalseBlock_t &F) {
  return brc_match<Cond_t, TrueBlock_t, FalseBlock_t>(C, T, F);
}

//===----------------------------------------------------------------------===//
// Matchers for fmax/fmin idioms, eg: "select (sgt x, y), x, y" -> smax(x,y).
//

/// Matches select(fcmp(Pred, L, R), L, R) fmax/fmin idioms.
template <typename LHS_t, typename RHS_t, typename Pred_t>
struct FMaxMin_match {
  /// Predicate helper type for the desired max/min.
  using PredType = Pred_t;
  /// Left-hand operand matcher.
  LHS_t L;
  /// Right-hand operand matcher.
  RHS_t R;

  // The evaluation order is always stable, regardless of Commutability.
  // The LHS is always matched first.
  /// Construct from left and right operand matchers.
  /// \param LHS Left-hand matcher.
  /// \param RHS Right-hand matcher.
  FMaxMin_match(const LHS_t &LHS, const RHS_t &RHS) : L(LHS), R(RHS) {}

  /// Match V if it is a select/fcmp max or min idiom for Pred_t.
  /// \param V Value to match.
  /// \return True if the match succeeds.
  template <typename OpTy> bool match(OpTy *V) const {
    // Look for "(x pred y) ? x : y" or "(x pred y) ? y : x".
    auto *SI = dyn_cast<SelectInst>(V);
    if (!SI)
      return false;
    auto *Cmp = dyn_cast<FCmpInst>(SI->getCondition());
    if (!Cmp)
      return false;
    // At this point we have a select conditioned on a comparison.  Check that
    // it is the values returned by the select that are being compared.
    auto *TrueVal = SI->getTrueValue();
    auto *FalseVal = SI->getFalseValue();
    auto *LHS = Cmp->getOperand(0);
    auto *RHS = Cmp->getOperand(1);
    if ((TrueVal != LHS || FalseVal != RHS) &&
        (TrueVal != RHS || FalseVal != LHS))
      return false;
    FCmpInst::Predicate Pred =
        LHS == TrueVal ? Cmp->getPredicate() : Cmp->getInversePredicate();
    // Does "(x pred y) ? x : y" represent the desired max/min operation?
    if (!Pred_t::match(Pred))
      return false;
    // It does!  Bind the operands.
    return L.match(LHS) && R.match(RHS);
  }
};

/// Helper class for identifying ordered max predicates.
struct ofmax_pred_ty {
  /// Return true if Pred is an ordered greater-than or greater-or-equal.
  /// \param Pred Floating-point compare predicate.
  /// \return True if the match succeeds.
  static bool match(FCmpInst::Predicate Pred) {
    return Pred == CmpInst::FCMP_OGT || Pred == CmpInst::FCMP_OGE;
  }
};

/// Helper class for identifying ordered min predicates.
struct ofmin_pred_ty {
  /// Return true if Pred is an ordered less-than or less-or-equal.
  /// \param Pred Floating-point compare predicate.
  /// \return True if the match succeeds.
  static bool match(FCmpInst::Predicate Pred) {
    return Pred == CmpInst::FCMP_OLT || Pred == CmpInst::FCMP_OLE;
  }
};

/// Helper class for identifying unordered max predicates.
struct ufmax_pred_ty {
  /// Return true if Pred is an unordered greater-than or greater-or-equal.
  /// \param Pred Floating-point compare predicate.
  /// \return True if the match succeeds.
  static bool match(FCmpInst::Predicate Pred) {
    return Pred == CmpInst::FCMP_UGT || Pred == CmpInst::FCMP_UGE;
  }
};

/// Helper class for identifying unordered min predicates.
struct ufmin_pred_ty {
  /// Return true if Pred is an unordered less-than or less-or-equal.
  /// \param Pred Floating-point compare predicate.
  /// \return True if the match succeeds.
  static bool match(FCmpInst::Predicate Pred) {
    return Pred == CmpInst::FCMP_ULT || Pred == CmpInst::FCMP_ULE;
  }
};

/// Match an ordered floating-point maximum.
///
/// Floating point has one special value 'NaN'. Therefore, there is no total
/// order. However, if we can ignore the 'NaN' value (for example, because of a
/// 'no-nans-float-math' flag) a combination of a fcmp and select has 'maximum'
/// semantics. In the presence of 'NaN' we have to preserve the original
/// select(fcmp(ogt/ge, L, R), L, R) semantics matched by this predicate.
///
///                         max(L, R)  iff L and R are not NaN
///  m_OrdFMax(L, R) =      R          iff L or R are NaN
/// \param L Left-hand matcher.
/// \param R Right-hand matcher.
/// \return A matcher for an ordered floating-point maximum.
template <typename LHS, typename RHS>
inline FMaxMin_match<LHS, RHS, ofmax_pred_ty> m_OrdFMax(const LHS &L,
                                                        const RHS &R) {
  return FMaxMin_match<LHS, RHS, ofmax_pred_ty>(L, R);
}

/// Match an ordered floating-point minimum.
///
/// Floating point has one special value 'NaN'. Therefore, there is no total
/// order. However, if we can ignore the 'NaN' value (for example, because of a
/// 'no-nans-float-math' flag) a combination of a fcmp and select has 'minimum'
/// semantics. In the presence of 'NaN' we have to preserve the original
/// select(fcmp(olt/le, L, R), L, R) semantics matched by this predicate.
///
///                         min(L, R)  iff L and R are not NaN
///  m_OrdFMin(L, R) =      R          iff L or R are NaN
/// \param L Left-hand matcher.
/// \param R Right-hand matcher.
/// \return A matcher for an ordered floating-point minimum.
template <typename LHS, typename RHS>
inline FMaxMin_match<LHS, RHS, ofmin_pred_ty> m_OrdFMin(const LHS &L,
                                                        const RHS &R) {
  return FMaxMin_match<LHS, RHS, ofmin_pred_ty>(L, R);
}

/// Match an unordered floating-point maximum.
///
/// Floating point has one special value 'NaN'. Therefore, there is no total
/// order. However, if we can ignore the 'NaN' value (for example, because of a
/// 'no-nans-float-math' flag) a combination of a fcmp and select has 'maximum'
/// semantics. In the presence of 'NaN' we have to preserve the original
/// select(fcmp(ugt/ge, L, R), L, R) semantics matched by this predicate.
///
///                         max(L, R)  iff L and R are not NaN
///  m_UnordFMax(L, R) =    L          iff L or R are NaN
/// \param L Left-hand matcher.
/// \param R Right-hand matcher.
/// \return A matcher for an unordered floating-point maximum.
template <typename LHS, typename RHS>
inline FMaxMin_match<LHS, RHS, ufmax_pred_ty> m_UnordFMax(const LHS &L,
                                                          const RHS &R) {
  return FMaxMin_match<LHS, RHS, ufmax_pred_ty>(L, R);
}

/// Match an unordered floating-point minimum.
///
/// Floating point has one special value 'NaN'. Therefore, there is no total
/// order. However, if we can ignore the 'NaN' value (for example, because of a
/// 'no-nans-float-math' flag) a combination of a fcmp and select has 'minimum'
/// semantics. In the presence of 'NaN' we have to preserve the original
/// select(fcmp(ult/le, L, R), L, R) semantics matched by this predicate.
///
///                          min(L, R)  iff L and R are not NaN
///  m_UnordFMin(L, R) =     L          iff L or R are NaN
/// \param L Left-hand matcher.
/// \param R Right-hand matcher.
/// \return A matcher for an unordered floating-point minimum.
template <typename LHS, typename RHS>
inline FMaxMin_match<LHS, RHS, ufmin_pred_ty> m_UnordFMin(const LHS &L,
                                                          const RHS &R) {
  return FMaxMin_match<LHS, RHS, ufmin_pred_ty>(L, R);
}

/// Match an ordered or unordered floating-point maximum.
///
/// Floating point has one special value 'NaN'. Therefore, there is no total
/// order. However, if we can ignore the 'NaN' value (for example, because of a
/// 'no-nans-float-math' flag) a combination of a fcmp and select has 'maximum'
/// semantics.
/// \param L Left-hand matcher.
/// \param R Right-hand matcher.
/// \return A matcher for an ordered or unordered floating-point maximum.
template <typename LHS, typename RHS>
inline match_combine_or<FMaxMin_match<LHS, RHS, ofmax_pred_ty>,
                        FMaxMin_match<LHS, RHS, ufmax_pred_ty>>
m_OrdOrUnordFMax(const LHS &L, const RHS &R) {
  return m_CombineOr(FMaxMin_match<LHS, RHS, ofmax_pred_ty>(L, R),
                     FMaxMin_match<LHS, RHS, ufmax_pred_ty>(L, R));
}

/// Match an ordered or unordered floating-point minimum.
///
/// Floating point has one special value 'NaN'. Therefore, there is no total
/// order. However, if we can ignore the 'NaN' value (for example, because of a
/// 'no-nans-float-math' flag) a combination of a fcmp and select has 'minimum'
/// semantics.
/// \param L Left-hand matcher.
/// \param R Right-hand matcher.
/// \return A matcher for an ordered or unordered floating-point minimum.
template <typename LHS, typename RHS>
inline match_combine_or<FMaxMin_match<LHS, RHS, ofmin_pred_ty>,
                        FMaxMin_match<LHS, RHS, ufmin_pred_ty>>
m_OrdOrUnordFMin(const LHS &L, const RHS &R) {
  return m_CombineOr(FMaxMin_match<LHS, RHS, ofmin_pred_ty>(L, R),
                     FMaxMin_match<LHS, RHS, ufmin_pred_ty>(L, R));
}

/// Matches a 'Not' as 'xor V, -1' or 'xor -1, V'.
/// NOTE: we first match the 'Not' (by matching '-1'),
/// and only then match the inner matcher!
/// \param V Matcher for the value being bitwise-not'ed.
/// \return A matcher for a 'Not' as 'xor V, -1' or 'xor -1, V'. NOTE: we first match the 'Not' (by matching '-1'), and only then match the inner matcher!.
template <typename ValTy>
inline BinaryOp_match<cst_pred_ty<is_all_ones>, ValTy, Instruction::Xor, true>
m_Not(const ValTy &V) {
  return m_c_Xor(m_AllOnes(), V);
}

/// Matches a 'Not' as 'xor V, -1' without allowing poison in the all-ones.
/// \param V Matcher for the value being bitwise-not'ed.
/// \return A matcher for a 'Not' as 'xor V, -1' without allowing poison in the all-ones.
template <typename ValTy>
inline BinaryOp_match<cst_pred_ty<is_all_ones, false>, ValTy, Instruction::Xor,
                      true>
m_NotForbidPoison(const ValTy &V) {
  return m_c_Xor(m_AllOnesForbidPoison(), V);
}

//===----------------------------------------------------------------------===//
// Matchers for overflow check patterns: e.g. (a + b) u< a, (a ^ -1) <u b
// Note that S might be matched to other instructions than AddInst.
//

/// Matcher for an icmp checking unsigned add overflow.
template <typename LHS_t, typename RHS_t, typename Sum_t>
struct UAddWithOverflow_match {
  /// Matcher for the add LHS.
  LHS_t L;
  /// Matcher for the add RHS.
  RHS_t R;
  /// Matcher for the add (or not) whose overflow is checked.
  Sum_t S;

  /// Construct an unsigned-add-overflow matcher.
  /// \param L Matcher for the add LHS.
  /// \param R Matcher for the add RHS.
  /// \param S Matcher for the add whose overflow is checked.
  UAddWithOverflow_match(const LHS_t &L, const RHS_t &R, const Sum_t &S)
      : L(L), R(R), S(S) {}

  /// Match if \p V is an icmp that checks unsigned overflow of L + R.
  /// \param V The value to match.
  /// \return True if the match succeeds.
  template <typename OpTy> bool match(OpTy *V) const {
    Value *ICmpLHS, *ICmpRHS;
    CmpPredicate Pred;
    if (!m_ICmp(Pred, m_Value(ICmpLHS), m_Value(ICmpRHS)).match(V))
      return false;

    Value *AddLHS, *AddRHS;
    auto AddExpr = m_Add(m_Value(AddLHS), m_Value(AddRHS));

    // (a + b) u< a, (a + b) u< b
    if (Pred == ICmpInst::ICMP_ULT)
      if (AddExpr.match(ICmpLHS) && (ICmpRHS == AddLHS || ICmpRHS == AddRHS))
        return L.match(AddLHS) && R.match(AddRHS) && S.match(ICmpLHS);

    // a >u (a + b), b >u (a + b)
    if (Pred == ICmpInst::ICMP_UGT)
      if (AddExpr.match(ICmpRHS) && (ICmpLHS == AddLHS || ICmpLHS == AddRHS))
        return L.match(AddLHS) && R.match(AddRHS) && S.match(ICmpRHS);

    Value *Op1;
    auto XorExpr = m_OneUse(m_Not(m_Value(Op1)));
    // (~a) <u b
    if (Pred == ICmpInst::ICMP_ULT) {
      if (XorExpr.match(ICmpLHS))
        return L.match(Op1) && R.match(ICmpRHS) && S.match(ICmpLHS);
    }
    //  b > u (~a)
    if (Pred == ICmpInst::ICMP_UGT) {
      if (XorExpr.match(ICmpRHS))
        return L.match(Op1) && R.match(ICmpLHS) && S.match(ICmpRHS);
    }

    // Match special-case for increment-by-1.
    if (Pred == ICmpInst::ICMP_EQ) {
      // (a + 1) == 0
      // (1 + a) == 0
      if (AddExpr.match(ICmpLHS) && m_ZeroInt().match(ICmpRHS) &&
          (m_One().match(AddLHS) || m_One().match(AddRHS)))
        return L.match(AddLHS) && R.match(AddRHS) && S.match(ICmpLHS);
      // 0 == (a + 1)
      // 0 == (1 + a)
      if (m_ZeroInt().match(ICmpLHS) && AddExpr.match(ICmpRHS) &&
          (m_One().match(AddLHS) || m_One().match(AddRHS)))
        return L.match(AddLHS) && R.match(AddRHS) && S.match(ICmpRHS);
    }

    return false;
  }
};

/// Match an icmp instruction checking for unsigned overflow on addition.
///
/// S is matched to the addition whose result is being checked for overflow, and
/// L and R are matched to the LHS and RHS of S.
/// \param L Matcher for the add LHS.
/// \param R Matcher for the add RHS.
/// \param S Matcher for the add whose overflow is checked.
/// \return A matcher for an icmp instruction checking for unsigned overflow on addition.
template <typename LHS_t, typename RHS_t, typename Sum_t>
UAddWithOverflow_match<LHS_t, RHS_t, Sum_t>
m_UAddWithOverflow(const LHS_t &L, const RHS_t &R, const Sum_t &S) {
  return UAddWithOverflow_match<LHS_t, RHS_t, Sum_t>(L, R, S);
}

/// Matcher for a specific call argument by index.
template <typename Opnd_t> struct Argument_match {
  /// Argument operand index to match.
  unsigned OpI;
  /// Nested matcher for the argument value.
  Opnd_t Val;

  /// Construct an argument matcher for operand \p OpIdx.
  /// \param OpIdx Zero-based argument index.
  /// \param V Nested matcher for that argument.
  Argument_match(unsigned OpIdx, const Opnd_t &V) : OpI(OpIdx), Val(V) {}

  /// Match if \p V is a call and argument OpI matches Val.
  /// \param V The value to match.
  /// \return True if the match succeeds.
  template <typename OpTy> bool match(OpTy *V) const {
    // FIXME: Should likely be switched to use `CallBase`.
    if (const auto *CI = dyn_cast<CallInst>(V))
      return Val.match(CI->getArgOperand(OpI));
    return false;
  }
};

/// Match an argument.
/// \param Op Matcher for the argument at index OpI.
/// \return A matcher for an argument.
template <unsigned OpI, typename Opnd_t>
inline Argument_match<Opnd_t> m_Argument(const Opnd_t &Op) {
  return Argument_match<Opnd_t>(OpI, Op);
}

/// Matcher for a call to a specific intrinsic ID.
struct IntrinsicID_match {
  /// Intrinsic ID that must match.
  unsigned ID;

  /// Construct a matcher for intrinsic \p IntrID.
  /// \param IntrID The intrinsic ID to require.
  IntrinsicID_match(Intrinsic::ID IntrID) : ID(IntrID) {}

  /// Match if \p V is a call to intrinsic ID.
  /// \param V The value to match.
  /// \return True if the match succeeds.
  template <typename OpTy> bool match(OpTy *V) const {
    if (const auto *CI = dyn_cast<CallInst>(V))
      if (const auto *F = dyn_cast_or_null<Function>(CI->getCalledOperand()))
        return F->getIntrinsicID() == ID;
    return false;
  }
};

/// Matcher for a call whose intrinsic ID is one of IntrIDs.
template <Intrinsic::ID... IntrIDs> struct IntrinsicIDs_match {
  /// Match if \p V is a call to any of IntrIDs.
  /// \param V The value to match.
  /// \return True if the match succeeds.
  template <typename OpTy> bool match(OpTy *V) const {
    if (const auto *CI = dyn_cast<CallInst>(V))
      if (const auto *F = dyn_cast_or_null<Function>(CI->getCalledOperand())) {
        Intrinsic::ID ID = F->getIntrinsicID();
        return ((ID == IntrIDs) || ...);
      }
    return false;
  }
};

/// Helper that builds an intrinsic ID matcher combined with argument matchers.
struct IntrinsicMatchImpl {
  /// Build a matcher for intrinsic \p IntrID with operand matchers \p Ops.
  /// \param Indices Index sequence pairing each operand matcher with its argument index.
  /// \param Ops Operand matchers paired with indices from the index sequence.
  /// \return A matcher for the intrinsic with the given operand matchers.
  template <Intrinsic::ID IntrID, typename... Ts, size_t... Is>
  static auto impl([[maybe_unused]] std::index_sequence<Is...> Indices,
                   const Ts &...Ops) {
    return m_CombineAnd(IntrinsicID_match(IntrID), m_Argument<Is>(Ops)...);
  }
};

/// Match intrinsic calls like this:
/// m_Intrinsic<Intrinsic::fabs>(m_Value(X))
/// \param Ops Operand matchers for the intrinsic arguments, in order.
/// \return A matcher for intrinsic calls like this: m_Intrinsic<Intrinsic::fabs>(m_Value(X)).
template <Intrinsic::ID IntrID, typename... Ts>
inline auto m_Intrinsic(const Ts &...Ops) {
  return IntrinsicMatchImpl::impl<IntrID>(
      std::make_index_sequence<sizeof...(Ts)>{}, Ops...);
}

/// Match a call to any of the listed intrinsic IDs.
///
/// Example: m_AnyIntrinsic<Intrinsic::fptosi_sat, Intrinsic::fptoui_sat>()
/// This is more efficient than using nested m_CombineOr with m_Intrinsic
/// because it performs the CallInst/Function cast only once.
/// \return A matcher for a call to any of the listed intrinsic IDs.
template <Intrinsic::ID... IntrIDs>
inline IntrinsicIDs_match<IntrIDs...> m_AnyIntrinsic() {
  return IntrinsicIDs_match<IntrIDs...>();
}

/// Matches MaskedLoad Intrinsic.
/// \param Op0 Operand matcher for the pointer.
/// \param Op1 Operand matcher for the alignment.
/// \param Op2 Operand matcher for the mask.
/// \return A matcher for MaskedLoad Intrinsic.
template <typename Opnd0, typename Opnd1, typename Opnd2>
inline auto m_MaskedLoad(const Opnd0 &Op0, const Opnd1 &Op1, const Opnd2 &Op2) {
  return m_Intrinsic<Intrinsic::masked_load>(Op0, Op1, Op2);
}

/// Matches MaskedStore Intrinsic.
/// \param Op0 Operand matcher for the value to store.
/// \param Op1 Operand matcher for the pointer.
/// \param Op2 Operand matcher for the alignment.
/// \return A matcher for MaskedStore Intrinsic.
template <typename Opnd0, typename Opnd1, typename Opnd2>
inline auto m_MaskedStore(const Opnd0 &Op0, const Opnd1 &Op1,
                          const Opnd2 &Op2) {
  return m_Intrinsic<Intrinsic::masked_store>(Op0, Op1, Op2);
}

/// Matches MaskedGather Intrinsic.
/// \param Op0 Operand matcher for the pointers.
/// \param Op1 Operand matcher for the alignment.
/// \param Op2 Operand matcher for the mask.
/// \return A matcher for MaskedGather Intrinsic.
template <typename Opnd0, typename Opnd1, typename Opnd2>
inline auto m_MaskedGather(const Opnd0 &Op0, const Opnd1 &Op1,
                           const Opnd2 &Op2) {
  return m_Intrinsic<Intrinsic::masked_gather>(Op0, Op1, Op2);
}

// Helper intrinsic matching specializations.
/// Matches a call to the llvm.bitreverse intrinsic.
/// \param Op0 Operand matcher for the value to reverse.
/// \return A matcher for a call to the llvm.bitreverse intrinsic.
template <typename Opnd0> inline auto m_BitReverse(const Opnd0 &Op0) {
  return m_Intrinsic<Intrinsic::bitreverse>(Op0);
}

/// Matches a call to the llvm.bswap intrinsic.
/// \param Op0 Operand matcher for the value to byte-swap.
/// \return A matcher for a call to the llvm.bswap intrinsic.
template <typename Opnd0> inline auto m_BSwap(const Opnd0 &Op0) {
  return m_Intrinsic<Intrinsic::bswap>(Op0);
}
/// Matches a call to the llvm.ctpop intrinsic.
/// \param Op0 Operand matcher for the value whose bits are counted.
/// \return A matcher for a call to the llvm.ctpop intrinsic.
template <typename Opnd0> inline auto m_Ctpop(const Opnd0 &Op0) {
  return m_Intrinsic<Intrinsic::ctpop>(Op0);
}

/// Matches a call to the llvm.fabs intrinsic.
/// \param Op0 Operand matcher for the floating-point value.
/// \return A matcher for a call to the llvm.fabs intrinsic.
template <typename Opnd0> inline auto m_FAbs(const Opnd0 &Op0) {
  return m_Intrinsic<Intrinsic::fabs>(Op0);
}

/// Matches a call to the llvm.canonicalize intrinsic.
/// \param Op0 Operand matcher for the floating-point value.
/// \return A matcher for a call to the llvm.canonicalize intrinsic.
template <typename Opnd0> inline auto m_FCanonicalize(const Opnd0 &Op0) {
  return m_Intrinsic<Intrinsic::canonicalize>(Op0);
}

/// Matches a call to the llvm.ctlz intrinsic.
/// \param Op0 Operand matcher for the value.
/// \param Op1 Operand matcher for the is_zero_poison flag.
/// \return A matcher for a call to the llvm.ctlz intrinsic.
template <typename Opnd0, typename Opnd1>
inline auto m_Ctlz(const Opnd0 &Op0, const Opnd1 &Op1) {
  return m_Intrinsic<Intrinsic::ctlz>(Op0, Op1);
}

/// Matches a call to the llvm.cttz intrinsic.
/// \param Op0 Operand matcher for the value.
/// \param Op1 Operand matcher for the is_zero_poison flag.
/// \return A matcher for a call to the llvm.cttz intrinsic.
template <typename Opnd0, typename Opnd1>
inline auto m_Cttz(const Opnd0 &Op0, const Opnd1 &Op1) {
  return m_Intrinsic<Intrinsic::cttz>(Op0, Op1);
}

/// Matches a call to the llvm.smax intrinsic.
/// \param Op0 Operand matcher for the first operand.
/// \param Op1 Operand matcher for the second operand.
/// \return A matcher for a call to the llvm.smax intrinsic.
template <typename Opnd0, typename Opnd1>
inline auto m_SMax(const Opnd0 &Op0, const Opnd1 &Op1) {
  return m_Intrinsic<Intrinsic::smax>(Op0, Op1);
}

/// Matches a call to the llvm.smin intrinsic.
/// \param Op0 Operand matcher for the first operand.
/// \param Op1 Operand matcher for the second operand.
/// \return A matcher for a call to the llvm.smin intrinsic.
template <typename Opnd0, typename Opnd1>
inline auto m_SMin(const Opnd0 &Op0, const Opnd1 &Op1) {
  return m_Intrinsic<Intrinsic::smin>(Op0, Op1);
}

/// Matches a call to the llvm.umax intrinsic.
/// \param Op0 Operand matcher for the first operand.
/// \param Op1 Operand matcher for the second operand.
/// \return A matcher for a call to the llvm.umax intrinsic.
template <typename Opnd0, typename Opnd1>
inline auto m_UMax(const Opnd0 &Op0, const Opnd1 &Op1) {
  return m_Intrinsic<Intrinsic::umax>(Op0, Op1);
}

/// Matches a call to the llvm.umin intrinsic.
/// \param Op0 Operand matcher for the first operand.
/// \param Op1 Operand matcher for the second operand.
/// \return A matcher for a call to the llvm.umin intrinsic.
template <typename Opnd0, typename Opnd1>
inline auto m_UMin(const Opnd0 &Op0, const Opnd1 &Op1) {
  return m_Intrinsic<Intrinsic::umin>(Op0, Op1);
}

/// Matches a call to llvm.smax, smin, umax, or umin.
/// \param Op0 Operand matcher for the first operand.
/// \param Op1 Operand matcher for the second operand.
/// \return A matcher for a call to llvm.smax, smin, umax, or umin.
template <typename Opnd0, typename Opnd1>
inline auto m_MaxOrMin(const Opnd0 &Op0, const Opnd1 &Op1) {
  return m_CombineOr(m_SMax(Op0, Op1), m_SMin(Op0, Op1), m_UMax(Op0, Op1),
                     m_UMin(Op0, Op1));
}

/// Matches a call to the llvm.minnum intrinsic.
/// \param Op0 Operand matcher for the first operand.
/// \param Op1 Operand matcher for the second operand.
/// \return A matcher for a call to the llvm.minnum intrinsic.
template <typename Opnd0, typename Opnd1>
inline auto m_FMinNum(const Opnd0 &Op0, const Opnd1 &Op1) {
  return m_Intrinsic<Intrinsic::minnum>(Op0, Op1);
}

/// Matches a call to the llvm.minimum intrinsic.
/// \param Op0 Operand matcher for the first operand.
/// \param Op1 Operand matcher for the second operand.
/// \return A matcher for a call to the llvm.minimum intrinsic.
template <typename Opnd0, typename Opnd1>
inline auto m_FMinimum(const Opnd0 &Op0, const Opnd1 &Op1) {
  return m_Intrinsic<Intrinsic::minimum>(Op0, Op1);
}

/// Matches a call to the llvm.minimumnum intrinsic.
/// \param Op0 Operand matcher for the first operand.
/// \param Op1 Operand matcher for the second operand.
/// \return A matcher for a call to the llvm.minimumnum intrinsic.
template <typename Opnd0, typename Opnd1>
inline auto m_FMinimumNum(const Opnd0 &Op0, const Opnd1 &Op1) {
  return m_Intrinsic<Intrinsic::minimumnum>(Op0, Op1);
}

/// Matches a call to the llvm.maxnum intrinsic.
/// \param Op0 Operand matcher for the first operand.
/// \param Op1 Operand matcher for the second operand.
/// \return A matcher for a call to the llvm.maxnum intrinsic.
template <typename Opnd0, typename Opnd1>
inline auto m_FMaxNum(const Opnd0 &Op0, const Opnd1 &Op1) {
  return m_Intrinsic<Intrinsic::maxnum>(Op0, Op1);
}

/// Matches a call to the llvm.maximum intrinsic.
/// \param Op0 Operand matcher for the first operand.
/// \param Op1 Operand matcher for the second operand.
/// \return A matcher for a call to the llvm.maximum intrinsic.
template <typename Opnd0, typename Opnd1>
inline auto m_FMaximum(const Opnd0 &Op0, const Opnd1 &Op1) {
  return m_Intrinsic<Intrinsic::maximum>(Op0, Op1);
}

/// Matches a call to the llvm.maximumnum intrinsic.
/// \param Op0 Operand matcher for the first operand.
/// \param Op1 Operand matcher for the second operand.
/// \return A matcher for a call to the llvm.maximumnum intrinsic.
template <typename Opnd0, typename Opnd1>
inline auto m_FMaximumNum(const Opnd0 &Op0, const Opnd1 &Op1) {
  return m_Intrinsic<Intrinsic::maximumnum>(Op0, Op1);
}

/// Matches a call to llvm.maxnum or llvm.maximumnum.
/// \param Op0 Operand matcher for the first operand.
/// \param Op1 Operand matcher for the second operand.
/// \return A matcher for a call to llvm.maxnum or llvm.maximumnum.
template <typename Opnd0, typename Opnd1>
inline auto m_FMaxNum_or_FMaximumNum(const Opnd0 &Op0, const Opnd1 &Op1) {
  return m_CombineOr(m_FMaxNum(Op0, Op1), m_FMaximumNum(Op0, Op1));
}

/// Matches a call to llvm.minnum or llvm.minimumnum.
/// \param Op0 Operand matcher for the first operand.
/// \param Op1 Operand matcher for the second operand.
/// \return A matcher for a call to llvm.minnum or llvm.minimumnum.
template <typename Opnd0, typename Opnd1>
inline auto m_FMinNum_or_FMinimumNum(const Opnd0 &Op0, const Opnd1 &Op1) {
  return m_CombineOr(m_FMinNum(Op0, Op1), m_FMinimumNum(Op0, Op1));
}

/// Matches a call to the llvm.fshl intrinsic.
/// \param Op0 Operand matcher for the first concatenated operand.
/// \param Op1 Operand matcher for the second concatenated operand.
/// \param Op2 Operand matcher for the shift amount.
/// \return A matcher for a call to the llvm.fshl intrinsic.
template <typename Opnd0, typename Opnd1, typename Opnd2>
inline auto m_FShl(const Opnd0 &Op0, const Opnd1 &Op1, const Opnd2 &Op2) {
  return m_Intrinsic<Intrinsic::fshl>(Op0, Op1, Op2);
}

/// Matches a call to the llvm.fshr intrinsic.
/// \param Op0 Operand matcher for the first concatenated operand.
/// \param Op1 Operand matcher for the second concatenated operand.
/// \param Op2 Operand matcher for the shift amount.
/// \return A matcher for a call to the llvm.fshr intrinsic.
template <typename Opnd0, typename Opnd1, typename Opnd2>
inline auto m_FShr(const Opnd0 &Op0, const Opnd1 &Op1, const Opnd2 &Op2) {
  return m_Intrinsic<Intrinsic::fshr>(Op0, Op1, Op2);
}

/// Matches a call to the llvm.sqrt intrinsic.
/// \param Op0 Operand matcher for the floating-point value.
/// \return A matcher for a call to the llvm.sqrt intrinsic.
template <typename Opnd0> inline auto m_Sqrt(const Opnd0 &Op0) {
  return m_Intrinsic<Intrinsic::sqrt>(Op0);
}

/// Matches a call to the llvm.copysign intrinsic.
/// \param Op0 Operand matcher for the magnitude operand.
/// \param Op1 Operand matcher for the sign operand.
/// \return A matcher for a call to the llvm.copysign intrinsic.
template <typename Opnd0, typename Opnd1>
inline auto m_CopySign(const Opnd0 &Op0, const Opnd1 &Op1) {
  return m_Intrinsic<Intrinsic::copysign>(Op0, Op1);
}

/// Matches a call to the llvm.vector.reverse intrinsic.
/// \param Op0 Operand matcher for the vector to reverse.
/// \return A matcher for a call to the llvm.vector.reverse intrinsic.
template <typename Opnd0> inline auto m_VecReverse(const Opnd0 &Op0) {
  return m_Intrinsic<Intrinsic::vector_reverse>(Op0);
}

/// Matches a call to the llvm.vector.insert intrinsic.
/// \param Op0 Operand matcher for the destination vector.
/// \param Op1 Operand matcher for the subvector to insert.
/// \param Op2 Operand matcher for the insertion index.
/// \return A matcher for a call to the llvm.vector.insert intrinsic.
template <typename Opnd0, typename Opnd1, typename Opnd2>
inline auto m_VectorInsert(const Opnd0 &Op0, const Opnd1 &Op1,
                           const Opnd2 &Op2) {
  return m_Intrinsic<Intrinsic::vector_insert>(Op0, Op1, Op2);
}

//===----------------------------------------------------------------------===//
// Matchers for two-operands operators with the operators in either order
//

/// Matches a BinaryOperator with LHS and RHS in either order.
/// \param L Matcher for one operand.
/// \param R Matcher for the other operand.
/// \return A matcher for a BinaryOperator with LHS and RHS in either order.
template <typename LHS, typename RHS>
inline AnyBinaryOp_match<LHS, RHS, true> m_c_BinOp(const LHS &L, const RHS &R) {
  return AnyBinaryOp_match<LHS, RHS, true>(L, R);
}

/// Matches an ICmp with a predicate over LHS and RHS in either order.
/// Swaps the predicate if operands are commuted.
/// \param Pred Bound to the matched (possibly swapped) predicate.
/// \param L Matcher for one operand.
/// \param R Matcher for the other operand.
/// \return A matcher for an ICmp with a predicate over LHS and RHS in either order. Swaps the predicate if operands are commuted.
template <typename LHS, typename RHS>
inline CmpClass_match<LHS, RHS, ICmpInst, true>
m_c_ICmp(CmpPredicate &Pred, const LHS &L, const RHS &R) {
  return CmpClass_match<LHS, RHS, ICmpInst, true>(Pred, L, R);
}

/// Matches an ICmp over LHS and RHS in either order without binding the pred.
/// \param L Matcher for one operand.
/// \param R Matcher for the other operand.
/// \return A matcher for an ICmp over LHS and RHS in either order without binding the pred.
template <typename LHS, typename RHS>
inline CmpClass_match<LHS, RHS, ICmpInst, true> m_c_ICmp(const LHS &L,
                                                         const RHS &R) {
  return CmpClass_match<LHS, RHS, ICmpInst, true>(L, R);
}

/// Matches a specific opcode with LHS and RHS in either order.
/// \param Opcode Binary operator opcode to match.
/// \param L Matcher for one operand.
/// \param R Matcher for the other operand.
/// \return A matcher for a specific opcode with LHS and RHS in either order.
template <typename LHS, typename RHS>
inline SpecificBinaryOp_match<LHS, RHS, true>
m_c_BinOp(unsigned Opcode, const LHS &L, const RHS &R) {
  return SpecificBinaryOp_match<LHS, RHS, true>(Opcode, L, R);
}

/// Matches a Add with LHS and RHS in either order.
/// \param L Matcher for one operand.
/// \param R Matcher for the other operand.
/// \return A matcher for a Add with LHS and RHS in either order.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::Add, true> m_c_Add(const LHS &L,
                                                                const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::Add, true>(L, R);
}

/// Matches a Mul with LHS and RHS in either order.
/// \param L Matcher for one operand.
/// \param R Matcher for the other operand.
/// \return A matcher for a Mul with LHS and RHS in either order.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::Mul, true> m_c_Mul(const LHS &L,
                                                                const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::Mul, true>(L, R);
}

/// Matches an And with LHS and RHS in either order.
/// \param L Matcher for one operand.
/// \param R Matcher for the other operand.
/// \return A matcher for an And with LHS and RHS in either order.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::And, true> m_c_And(const LHS &L,
                                                                const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::And, true>(L, R);
}

/// Matches an Or with LHS and RHS in either order.
/// \param L Matcher for one operand.
/// \param R Matcher for the other operand.
/// \return A matcher for an Or with LHS and RHS in either order.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::Or, true> m_c_Or(const LHS &L,
                                                              const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::Or, true>(L, R);
}

/// Matches an Xor with LHS and RHS in either order.
/// \param L Matcher for one operand.
/// \param R Matcher for the other operand.
/// \return A matcher for an Xor with LHS and RHS in either order.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::Xor, true> m_c_Xor(const LHS &L,
                                                                const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::Xor, true>(L, R);
}

/// Matches a 'Neg' as 'sub 0, V'.
/// \param V Matcher for the value being negated.
/// \return A matcher for a 'Neg' as 'sub 0, V'.
template <typename ValTy>
inline BinaryOp_match<cst_pred_ty<is_zero_int>, ValTy, Instruction::Sub>
m_Neg(const ValTy &V) {
  return m_Sub(m_ZeroInt(), V);
}

/// Matches a 'Neg' as 'sub nsw 0, V'.
/// \param V Matcher for the value being negated.
/// \return A matcher for a 'Neg' as 'sub nsw 0, V'.
template <typename ValTy>
inline OverflowingBinaryOp_match<cst_pred_ty<is_zero_int>, ValTy,
                                 Instruction::Sub,
                                 OverflowingBinaryOperator::NoSignedWrap>
m_NSWNeg(const ValTy &V) {
  return m_NSWSub(m_ZeroInt(), V);
}

/// Matcher for a binary intrinsic with operands in either order.
template <Intrinsic::ID IntrID, typename LHS, typename RHS>
struct CommutativeBinaryIntrinsic_match {
  /// Matcher for one intrinsic operand.
  LHS L;
  /// Matcher for the other intrinsic operand.
  RHS R;

  /// Construct a commutative binary intrinsic matcher.
  /// \param L Matcher for one operand.
  /// \param R Matcher for the other operand.
  CommutativeBinaryIntrinsic_match(const LHS &L, const RHS &R) : L(L), R(R) {}

  /// Match if \p V is IntrID with operands matching L and R in either order.
  /// \param V The value to match.
  /// \return True if the match succeeds.
  template <typename OpTy> bool match(OpTy *V) const {
    const auto *II = dyn_cast<IntrinsicInst>(V);
    if (!II || II->getIntrinsicID() != IntrID)
      return false;
    return (L.match(II->getArgOperand(0)) && R.match(II->getArgOperand(1))) ||
           (L.match(II->getArgOperand(1)) && R.match(II->getArgOperand(0)));
  }
};

/// Matches a binary intrinsic with operands in either order.
/// \param Op0 Matcher for one operand.
/// \param Op1 Matcher for the other operand.
/// \return A matcher for a binary intrinsic with operands in either order.
template <Intrinsic::ID IntrID, typename T0, typename T1>
inline CommutativeBinaryIntrinsic_match<IntrID, T0, T1>
m_c_Intrinsic(const T0 &Op0, const T1 &Op1) {
  return CommutativeBinaryIntrinsic_match<IntrID, T0, T1>(Op0, Op1);
}

/// Matches an SMin with LHS and RHS in either order.
/// \param L Matcher for one operand.
/// \param R Matcher for the other operand.
/// \return A matcher for an SMin with LHS and RHS in either order.
template <typename LHS, typename RHS>
inline auto m_c_SMin(const LHS &L, const RHS &R) {
  return m_c_Intrinsic<Intrinsic::smin>(L, R);
}
/// Matches an SMax with LHS and RHS in either order.
/// \param L Matcher for one operand.
/// \param R Matcher for the other operand.
/// \return A matcher for an SMax with LHS and RHS in either order.
template <typename LHS, typename RHS>
inline auto m_c_SMax(const LHS &L, const RHS &R) {
  return m_c_Intrinsic<Intrinsic::smax>(L, R);
}
/// Matches a UMin with LHS and RHS in either order.
/// \param L Matcher for one operand.
/// \param R Matcher for the other operand.
/// \return A matcher for a UMin with LHS and RHS in either order.
template <typename LHS, typename RHS>
inline auto m_c_UMin(const LHS &L, const RHS &R) {
  return m_c_Intrinsic<Intrinsic::umin>(L, R);
}
/// Matches a UMax with LHS and RHS in either order.
/// \param L Matcher for one operand.
/// \param R Matcher for the other operand.
/// \return A matcher for a UMax with LHS and RHS in either order.
template <typename LHS, typename RHS>
inline auto m_c_UMax(const LHS &L, const RHS &R) {
  return m_c_Intrinsic<Intrinsic::umax>(L, R);
}

/// Matches smin/smax/umin/umax with operands in either order.
/// \param L Matcher for one operand.
/// \param R Matcher for the other operand.
/// \return A matcher for smin/smax/umin/umax with operands in either order.
template <typename LHS, typename RHS>
inline auto m_c_MaxOrMin(const LHS &L, const RHS &R) {
  return m_CombineOr(m_c_SMax(L, R), m_c_SMin(L, R), m_c_UMax(L, R),
                     m_c_UMin(L, R));
}

/// Matches FAdd with LHS and RHS in either order.
/// \param L Matcher for one operand.
/// \param R Matcher for the other operand.
/// \return A matcher for FAdd with LHS and RHS in either order.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::FAdd, true>
m_c_FAdd(const LHS &L, const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::FAdd, true>(L, R);
}

/// Matches FMul with LHS and RHS in either order.
/// \param L Matcher for one operand.
/// \param R Matcher for the other operand.
/// \return A matcher for FMul with LHS and RHS in either order.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::FMul, true>
m_c_FMul(const LHS &L, const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::FMul, true>(L, R);
}

/// Matcher for the arithmetic signum idiom.
template <typename Opnd_t> struct Signum_match {
  /// Nested matcher for the signum input value.
  Opnd_t Val;
  /// Construct a signum matcher wrapping \p V.
  /// \param V Matcher for the signum input.
  Signum_match(const Opnd_t &V) : Val(V) {}

  /// Match if \p V is a signum pattern over a value matching Val.
  /// \param V The value to match.
  /// \return True if the match succeeds.
  template <typename OpTy> bool match(OpTy *V) const {
    unsigned TypeSize = V->getType()->getScalarSizeInBits();
    if (TypeSize == 0)
      return false;

    unsigned ShiftWidth = TypeSize - 1;
    Value *Op;

    // This is the representation of signum we match:
    //
    //  signum(x) == (x >> 63) | (-x >>u 63)
    //
    // An i1 value is its own signum, so it's correct to match
    //
    //  signum(x) == (x >> 0)  | (-x >>u 0)
    //
    // for i1 values.

    auto LHS = m_AShr(m_Value(Op), m_SpecificInt(ShiftWidth));
    auto RHS = m_LShr(m_Neg(m_Deferred(Op)), m_SpecificInt(ShiftWidth));
    auto Signum = m_c_Or(LHS, RHS);

    return Signum.match(V) && Val.match(Op);
  }
};

/// Matches a signum pattern.
///
/// signum(x) =
///      x >  0  ->  1
///      x == 0  ->  0
///      x <  0  -> -1
/// \param V Matcher for the signum input value.
/// \return A matcher for a signum pattern.
template <typename Val_t> inline Signum_match<Val_t> m_Signum(const Val_t &V) {
  return Signum_match<Val_t>(V);
}

/// Matcher for ExtractValue with a fixed or any index.
template <int Ind, typename Opnd_t> struct ExtractValue_match {
  /// Nested matcher for the aggregate operand.
  Opnd_t Val;
  /// Construct an ExtractValue matcher wrapping \p V.
  /// \param V Matcher for the aggregate operand.
  ExtractValue_match(const Opnd_t &V) : Val(V) {}

  /// Match if \p V is ExtractValue of an aggregate matching Val.
  /// \param V The value to match.
  /// \return True if the match succeeds.
  template <typename OpTy> bool match(OpTy *V) const {
    if (auto *I = dyn_cast<ExtractValueInst>(V)) {
      // If Ind is -1, don't inspect indices
      if (Ind != -1 &&
          !(I->getNumIndices() == 1 && I->getIndices()[0] == (unsigned)Ind))
        return false;
      return Val.match(I->getAggregateOperand());
    }
    return false;
  }
};

/// Match a single index ExtractValue instruction.
/// For example m_ExtractValue<1>(...)
/// \param V Matcher for the aggregate operand.
/// \return A matcher for a single index ExtractValue instruction. For example m_ExtractValue<1>(...).
template <int Ind, typename Val_t>
inline ExtractValue_match<Ind, Val_t> m_ExtractValue(const Val_t &V) {
  return ExtractValue_match<Ind, Val_t>(V);
}

/// Match an ExtractValue instruction with any index.
/// For example m_ExtractValue(...)
/// \param V Matcher for the aggregate operand.
/// \return A matcher for an ExtractValue instruction with any index. For example m_ExtractValue(...).
template <typename Val_t>
inline ExtractValue_match<-1, Val_t> m_ExtractValue(const Val_t &V) {
  return ExtractValue_match<-1, Val_t>(V);
}

/// Matcher for a single index InsertValue instruction.
template <int Ind, typename T0, typename T1> struct InsertValue_match {
  /// Nested matcher for the aggregate operand.
  T0 Op0;
  /// Nested matcher for the inserted element.
  T1 Op1;

  /// Construct an InsertValue matcher.
  /// \param Op0 Matcher for the aggregate operand.
  /// \param Op1 Matcher for the inserted element.
  InsertValue_match(const T0 &Op0, const T1 &Op1) : Op0(Op0), Op1(Op1) {}

  /// Match if \p V is InsertValue at Ind with matching operands.
  /// \param V The value to match.
  /// \return True if the match succeeds.
  template <typename OpTy> bool match(OpTy *V) const {
    if (auto *I = dyn_cast<InsertValueInst>(V)) {
      return Op0.match(I->getOperand(0)) && Op1.match(I->getOperand(1)) &&
             I->getNumIndices() == 1 && Ind == I->getIndices()[0];
    }
    return false;
  }
};

/// Matches a single index InsertValue instruction.
/// \param Val Matcher for the aggregate operand.
/// \param Elt Matcher for the inserted element.
/// \return A matcher for a single index InsertValue instruction.
template <int Ind, typename Val_t, typename Elt_t>
inline InsertValue_match<Ind, Val_t, Elt_t> m_InsertValue(const Val_t &Val,
                                                          const Elt_t &Elt) {
  return InsertValue_match<Ind, Val_t, Elt_t>(Val, Elt);
}

/// Matches a call to `llvm.vscale()`.
/// \return A matcher for a call to `llvm.vscale()`.
inline auto m_VScale() { return m_Intrinsic<Intrinsic::vscale>(); }

/// Matches a call to the llvm.vector.interleave2 intrinsic.
/// \param Op0 Operand matcher for the first vector.
/// \param Op1 Operand matcher for the second vector.
/// \return A matcher for a call to the llvm.vector.interleave2 intrinsic.
template <typename Opnd0, typename Opnd1>
inline auto m_Interleave2(const Opnd0 &Op0, const Opnd1 &Op1) {
  return m_Intrinsic<Intrinsic::vector_interleave2>(Op0, Op1);
}

/// Matches a call to the llvm.vector.deinterleave2 intrinsic.
/// \param Op Operand matcher for the interleaved vector.
/// \return A matcher for a call to the llvm.vector.deinterleave2 intrinsic.
template <typename Opnd> inline auto m_Deinterleave2(const Opnd &Op) {
  return m_Intrinsic<Intrinsic::vector_deinterleave2>(Op);
}

/// Matcher for a logical And/Or as a bitwise op or select idiom.
template <typename LHS, typename RHS, unsigned Opcode, bool Commutable = false>
struct LogicalOp_match {
  /// Matcher for the left-hand logical operand.
  LHS L;
  /// Matcher for the right-hand logical operand.
  RHS R;

  /// Construct a logical-op matcher.
  /// \param L Matcher for one operand.
  /// \param R Matcher for the other operand.
  LogicalOp_match(const LHS &L, const RHS &R) : L(L), R(R) {}

  /// Match if \p V is a logical And/Or in binary or select form.
  /// \param V The value to match.
  /// \return True if the match succeeds.
  template <typename T> bool match(T *V) const {
    auto *I = dyn_cast<Instruction>(V);
    if (!I || !I->getType()->isIntOrIntVectorTy(1))
      return false;

    if (I->getOpcode() == Opcode) {
      auto *Op0 = I->getOperand(0);
      auto *Op1 = I->getOperand(1);
      return (L.match(Op0) && R.match(Op1)) ||
             (Commutable && L.match(Op1) && R.match(Op0));
    }

    if (auto *Select = dyn_cast<SelectInst>(I)) {
      auto *Cond = Select->getCondition();
      auto *TVal = Select->getTrueValue();
      auto *FVal = Select->getFalseValue();

      // Don't match a scalar select of bool vectors.
      // Transforms expect a single type for operands if this matches.
      if (Cond->getType() != Select->getType())
        return false;

      if (Opcode == Instruction::And) {
        auto *C = dyn_cast<Constant>(FVal);
        if (C && C->isNullValue())
          return (L.match(Cond) && R.match(TVal)) ||
                 (Commutable && L.match(TVal) && R.match(Cond));
      } else {
        assert(Opcode == Instruction::Or);
        auto *C = dyn_cast<Constant>(TVal);
        if (C && C->isOneValue())
          return (L.match(Cond) && R.match(FVal)) ||
                 (Commutable && L.match(FVal) && R.match(Cond));
      }
    }

    return false;
  }
};

/// Matches L && R either in the form of L & R or L ? R : false.
/// Note that the latter form is poison-blocking.
/// \param L Matcher for one operand.
/// \param R Matcher for the other operand.
/// \return A matcher for L && R either in the form of L & R or L ? R : false. Note that the latter form is poison-blocking.
template <typename LHS, typename RHS>
inline LogicalOp_match<LHS, RHS, Instruction::And> m_LogicalAnd(const LHS &L,
                                                                const RHS &R) {
  return LogicalOp_match<LHS, RHS, Instruction::And>(L, R);
}

/// Matches L && R where L and R are arbitrary values.
/// \return A matcher for L && R where L and R are arbitrary values.
inline auto m_LogicalAnd() { return m_LogicalAnd(m_Value(), m_Value()); }

/// Matches L && R with LHS and RHS in either order.
/// \param L Matcher for one operand.
/// \param R Matcher for the other operand.
/// \return A matcher for L && R with LHS and RHS in either order.
template <typename LHS, typename RHS>
inline LogicalOp_match<LHS, RHS, Instruction::And, true>
m_c_LogicalAnd(const LHS &L, const RHS &R) {
  return LogicalOp_match<LHS, RHS, Instruction::And, true>(L, R);
}

/// Matches L || R either in the form of L | R or L ? true : R.
/// Note that the latter form is poison-blocking.
/// \param L Matcher for one operand.
/// \param R Matcher for the other operand.
/// \return A matcher for L || R either in the form of L | R or L ? true : R. Note that the latter form is poison-blocking.
template <typename LHS, typename RHS>
inline LogicalOp_match<LHS, RHS, Instruction::Or> m_LogicalOr(const LHS &L,
                                                              const RHS &R) {
  return LogicalOp_match<LHS, RHS, Instruction::Or>(L, R);
}

/// Matches L || R where L and R are arbitrary values.
/// \return A matcher for L || R where L and R are arbitrary values.
inline auto m_LogicalOr() { return m_LogicalOr(m_Value(), m_Value()); }

/// Matches L || R with LHS and RHS in either order.
/// \param L Matcher for one operand.
/// \param R Matcher for the other operand.
/// \return A matcher for L || R with LHS and RHS in either order.
template <typename LHS, typename RHS>
inline LogicalOp_match<LHS, RHS, Instruction::Or, true>
m_c_LogicalOr(const LHS &L, const RHS &R) {
  return LogicalOp_match<LHS, RHS, Instruction::Or, true>(L, R);
}

/// Matches either L && R or L || R,
/// either one being in the either binary or logical form.
/// Note that the latter form is poison-blocking.
/// \param L Matcher for one operand.
/// \param R Matcher for the other operand.
/// \return A matcher for either L && R or L || R, either one being in the either binary or logical form. Note that the latter form is poison-blocking.
template <typename LHS, typename RHS, bool Commutable = false>
inline auto m_LogicalOp(const LHS &L, const RHS &R) {
  return m_CombineOr(
      LogicalOp_match<LHS, RHS, Instruction::And, Commutable>(L, R),
      LogicalOp_match<LHS, RHS, Instruction::Or, Commutable>(L, R));
}

/// Matches either L && R or L || R where L and R are arbitrary values.
/// \return A matcher for either L && R or L || R where L and R are arbitrary values.
inline auto m_LogicalOp() { return m_LogicalOp(m_Value(), m_Value()); }

/// Matches either L && R or L || R with LHS and RHS in either order.
/// \param L Matcher for one operand.
/// \param R Matcher for the other operand.
/// \return A matcher for either L && R or L || R with LHS and RHS in either order.
template <typename LHS, typename RHS>
inline auto m_c_LogicalOp(const LHS &L, const RHS &R) {
  return m_LogicalOp<LHS, RHS, /*Commutable=*/true>(L, R);
}

} // end namespace PatternMatch
} // end namespace llvm

#endif // LLVM_IR_PATTERNMATCH_H
