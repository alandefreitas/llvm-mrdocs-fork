//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides a simple and efficient mechanism for performing general
// tree-based pattern matches on SCEVs, based on LLVM's IR pattern matchers.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_SCALAREVOLUTIONPATTERNMATCH_H
#define LLVM_ANALYSIS_SCALAREVOLUTIONPATTERNMATCH_H

#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Support/PatternMatchHelpers.h"

namespace llvm {
/// Pattern-match helpers specialized for SCEV use wrappers.
namespace PatternMatchHelpers {
/// Bind matcher specialization for \c SCEVUseT.
template <typename SCEVPtrT> struct match_bind<SCEVUseT<SCEVPtrT>> {
  /// Reference that receives the matched SCEV use.
  SCEVUseT<SCEVPtrT> &VR;

  /// Construct a binder that writes the matched use into \p V.
  /// @param V SCEV use reference to bind on a successful match.
  match_bind(SCEVUseT<SCEVPtrT> &V) : VR(V) {}

  /// Match any value and bind it as a SCEV use into \c VR.
  /// @param V Value to bind as a SCEV use.
  /// @return Always true.
  template <typename ITy> bool match(ITy *V) const {
    VR = V;
    return true;
  }
};
} // namespace PatternMatchHelpers

/// Pattern matchers for Scalar Evolution (SCEV) expressions.
namespace SCEVPatternMatch {

using namespace llvm::PatternMatchHelpers;

/// Match SCEV \p S against pattern \p P.
/// @param S SCEV to test.
/// @param P Pattern matcher to apply.
/// @return True if \p P matches \p S.
template <typename Pattern> bool match(const SCEV *S, const Pattern &P) {
  return P.match(S);
}

/// Match the SCEV pointed to by use \p U against pattern \p P.
/// @param U SCEV use whose pointer is matched.
/// @param P Pattern matcher to apply.
/// @return True if \p P matches the SCEV referenced by \p U.
template <typename SCEVPtrT, typename Pattern>
bool match(const SCEVUseT<SCEVPtrT> U, const Pattern &P) {
  return P.match(U.getPointer());
}

/// Predicate matcher that accepts SCEV constants satisfying \c Predicate.
template <typename Predicate> struct cst_pred_ty : public Predicate {
  /// Construct a constant-predicate matcher with a default predicate.
  cst_pred_ty() = default;
  /// Construct a constant-predicate matcher initialized with value \p V.
  /// @param V Value forwarded to the underlying predicate.
  cst_pred_ty(uint64_t V) : Predicate(V) {}
  /// Match a non-vector SCEV constant whose APInt satisfies the predicate.
  /// @param S SCEV to test.
  /// @return True if \p S is a matching SCEV constant.
  bool match(const SCEV *S) const {
    assert((isa<SCEVCouldNotCompute>(S) || !S->getType()->isVectorTy()) &&
           "no vector types expected from SCEVs");
    auto *C = dyn_cast<SCEVConstant>(S);
    return C && this->isValue(C->getAPInt());
  }
};

/// Predicate that is true for the integer zero.
struct is_zero {
  /// Return true if \p C is zero.
  /// @param C Integer value to test.
  /// @return True if \p C is zero.
  bool isValue(const APInt &C) const { return C.isZero(); }
};

/// Match an integer 0.
/// @return Matcher for the integer constant 0.
inline cst_pred_ty<is_zero> m_scev_Zero() { return cst_pred_ty<is_zero>(); }

/// Predicate that is true for the integer one.
struct is_one {
  /// Return true if \p C is one.
  /// @param C Integer value to test.
  /// @return True if \p C is one.
  bool isValue(const APInt &C) const { return C.isOne(); }
};

/// Match an integer 1.
/// @return Matcher for the integer constant 1.
inline cst_pred_ty<is_one> m_scev_One() { return cst_pred_ty<is_one>(); }

/// Predicate that is true for an all-ones integer.
struct is_all_ones {
  /// Return true if \p C has all bits set.
  /// @param C Integer value to test.
  /// @return True if \p C is all-ones.
  bool isValue(const APInt &C) const { return C.isAllOnes(); }
};

/// Match an integer with all bits set.
/// @return Matcher for an all-ones integer constant.
inline cst_pred_ty<is_all_ones> m_scev_AllOnes() {
  return cst_pred_ty<is_all_ones>();
}

/// Match any SCEV.
/// @return Matcher that accepts any SCEV.
inline auto m_SCEV() { return m_Isa<const SCEV>(); }
/// Match any SCEVConstant.
/// @return Matcher that accepts any SCEVConstant.
inline auto m_SCEVConstant() { return m_Isa<const SCEVConstant>(); }
/// Match any SCEVVScale.
/// @return Matcher that accepts any SCEVVScale.
inline auto m_SCEVVScale() { return m_Isa<const SCEVVScale>(); }

/// Match a SCEV, capturing it if we match.
/// @param V Reference that receives the matched SCEV.
/// @return Binder that captures the matched SCEV into \p V.
inline match_bind<const SCEV> m_SCEV(const SCEV *&V) { return V; }

/// Match a SCEV use, capturing it if we match.
/// @param V SCEV use reference that receives the match.
/// @return Binder that captures the matched SCEV use into \p V.
template <typename SCEVPtrT>
inline match_bind<SCEVUseT<SCEVPtrT>> m_SCEV(SCEVUseT<SCEVPtrT> &V) {
  return V;
}
/// Match a SCEVConstant, capturing it if we match.
/// @param V Reference that receives the matched SCEVConstant.
/// @return Binder that captures the matched SCEVConstant into \p V.
inline match_bind<const SCEVConstant> m_SCEVConstant(const SCEVConstant *&V) {
  return V;
}
/// Match a SCEVUnknown, capturing it if we match.
/// @param V Reference that receives the matched SCEVUnknown.
/// @return Binder that captures the matched SCEVUnknown into \p V.
inline match_bind<const SCEVUnknown> m_SCEVUnknown(const SCEVUnknown *&V) {
  return V;
}

/// Match a SCEVAddExpr, capturing it if we match.
/// @param V Reference that receives the matched SCEVAddExpr.
/// @return Binder that captures the matched SCEVAddExpr into \p V.
inline match_bind<const SCEVAddExpr> m_scev_Add(const SCEVAddExpr *&V) {
  return V;
}

/// Match a SCEVMulExpr, capturing it if we match.
/// @param V Reference that receives the matched SCEVMulExpr.
/// @return Binder that captures the matched SCEVMulExpr into \p V.
inline match_bind<const SCEVMulExpr> m_scev_Mul(const SCEVMulExpr *&V) {
  return V;
}

/// Match a specified const SCEV *.
struct specificscev_ty {
  /// The exact SCEV that must be matched.
  const SCEV *Expr;

  /// Construct a matcher for the specific SCEV \p Expr.
  /// @param Expr SCEV that a successful match must equal.
  specificscev_ty(const SCEV *Expr) : Expr(Expr) {}

  /// Return true if \p S is the same pointer as \c Expr.
  /// @param S Value to compare against \c Expr.
  /// @return True if \p S equals \c Expr.
  template <typename ITy> bool match(ITy *S) const { return S == Expr; }
};

/// Match if we have a specific specified SCEV.
/// @param S Exact SCEV that must be matched.
/// @return Matcher for the exact SCEV \p S.
inline specificscev_ty m_scev_Specific(const SCEV *S) { return S; }

/// Predicate that matches a specific unsigned constant value.
struct is_specific_cst {
  /// The unsigned constant value to match.
  uint64_t CV;
  /// Construct a predicate for unsigned constant \p C.
  /// @param C Unsigned constant value to match.
  is_specific_cst(uint64_t C) : CV(C) {}
  /// Return true if \p C equals the stored unsigned constant.
  /// @param C Integer value to test.
  /// @return True if \p C equals \c CV.
  bool isValue(const APInt &C) const { return C == CV; }
};

/// Match an SCEV constant with a plain unsigned integer.
/// @param V Unsigned integer value that the SCEV constant must equal.
/// @return Matcher for an SCEV constant equal to \p V.
inline cst_pred_ty<is_specific_cst> m_scev_SpecificInt(uint64_t V) { return V; }

/// Predicate that matches a specific signed constant value.
struct is_specific_signed_cst {
  /// The signed constant value to match.
  int64_t CV;
  /// Construct a predicate for signed constant \p C.
  /// @param C Signed constant value to match.
  is_specific_signed_cst(int64_t C) : CV(C) {}
  /// Return true if the sign-extended value of \p C equals \c CV.
  /// @param C Integer value to test.
  /// @return True if \p C sign-extends to \c CV.
  bool isValue(const APInt &C) const { return C.trySExtValue() == CV; }
};

/// Match an SCEV constant with a plain signed integer (sign-extended value will
/// be matched)
/// @param V Signed integer value that the SCEV constant must equal.
/// @return Matcher for an SCEV constant whose sign-extended value equals \p V.
inline cst_pred_ty<is_specific_signed_cst> m_scev_SpecificSInt(int64_t V) {
  return V;
}

/// Matcher that binds a SCEV constant's APInt.
struct bind_cst_ty {
  /// Reference that receives the matched constant APInt.
  const APInt *&CR;

  /// Construct a binder that writes the matched APInt into \p Op0.
  /// @param Op0 Reference that receives the constant's APInt.
  bind_cst_ty(const APInt *&Op0) : CR(Op0) {}

  /// Match a non-vector SCEV constant and bind its APInt to \c CR.
  /// @param S SCEV to test.
  /// @return True if \p S is a SCEVConstant.
  bool match(const SCEV *S) const {
    assert((isa<SCEVCouldNotCompute>(S) || !S->getType()->isVectorTy()) &&
           "no vector types expected from SCEVs");
    auto *C = dyn_cast<SCEVConstant>(S);
    if (!C)
      return false;
    CR = &C->getAPInt();
    return true;
  }
};

/// Match an SCEV constant and bind it to an APInt.
/// @param C Reference that receives the matched constant's APInt.
/// @return Binder that captures the constant's APInt into \p C.
inline bind_cst_ty m_scev_APInt(const APInt *&C) { return C; }

/// Match a unary SCEV.
template <typename SCEVTy, typename Op0_t> struct SCEVUnaryExpr_match {
  /// Matcher for the single operand.
  Op0_t Op0;

  /// Construct a unary matcher with operand pattern \p Op0.
  /// @param Op0 Pattern that must match the unary operand.
  SCEVUnaryExpr_match(Op0_t Op0) : Op0(Op0) {}

  /// Match a unary SCEV of type \c SCEVTy whose operand matches \c Op0.
  /// @param S SCEV to test.
  /// @return True if \p S is a matching unary expression.
  bool match(const SCEV *S) const {
    auto *E = dyn_cast<SCEVTy>(S);
    return E && E->getNumOperands() == 1 &&
           Op0.match(E->getOperand(0).getPointer());
  }
};

/// Match a unary SCEV of type \c SCEVTy whose operand matches \p Op0.
/// @param Op0 Pattern that must match the unary operand.
/// @return Matcher for a unary SCEVTy with operand matching \p Op0.
template <typename SCEVTy, typename Op0_t>
inline SCEVUnaryExpr_match<SCEVTy, Op0_t> m_scev_Unary(const Op0_t &Op0) {
  return SCEVUnaryExpr_match<SCEVTy, Op0_t>(Op0);
}

/// Match a sign-extend SCEV whose operand matches \p Op0.
/// @param Op0 Pattern that must match the sign-extend operand.
/// @return Matcher for a sign-extend SCEV with operand matching \p Op0.
template <typename Op0_t>
inline SCEVUnaryExpr_match<SCEVSignExtendExpr, Op0_t>
m_scev_SExt(const Op0_t &Op0) {
  return m_scev_Unary<SCEVSignExtendExpr>(Op0);
}

/// Match a zero-extend SCEV whose operand matches \p Op0.
/// @param Op0 Pattern that must match the zero-extend operand.
/// @return Matcher for a zero-extend SCEV with operand matching \p Op0.
template <typename Op0_t>
inline SCEVUnaryExpr_match<SCEVZeroExtendExpr, Op0_t>
m_scev_ZExt(const Op0_t &Op0) {
  return m_scev_Unary<SCEVZeroExtendExpr>(Op0);
}

/// Match a pointer-to-address SCEV whose operand matches \p Op0.
/// @param Op0 Pattern that must match the ptrtoaddr operand.
/// @return Matcher for a ptrtoaddr SCEV with operand matching \p Op0.
template <typename Op0_t>
inline SCEVUnaryExpr_match<SCEVPtrToAddrExpr, Op0_t>
m_scev_PtrToAddr(const Op0_t &Op0) {
  return SCEVUnaryExpr_match<SCEVPtrToAddrExpr, Op0_t>(Op0);
}

/// Match a truncate SCEV whose operand matches \p Op0.
/// @param Op0 Pattern that must match the truncate operand.
/// @return Matcher for a truncate SCEV with operand matching \p Op0.
template <typename Op0_t>
inline SCEVUnaryExpr_match<SCEVTruncateExpr, Op0_t>
m_scev_Trunc(const Op0_t &Op0) {
  return m_scev_Unary<SCEVTruncateExpr>(Op0);
}

/// Match a binary SCEV.
template <typename SCEVTy, typename Op0_t, typename Op1_t,
          SCEV::NoWrapFlags WrapFlags = SCEV::FlagAnyWrap,
          bool Commutable = false>
struct SCEVBinaryExpr_match {
  /// Matcher for the first operand.
  Op0_t Op0;
  /// Matcher for the second operand.
  Op1_t Op1;

  /// Construct a binary matcher with operand patterns \p Op0 and \p Op1.
  /// @param Op0 Pattern that must match the first operand.
  /// @param Op1 Pattern that must match the second operand.
  SCEVBinaryExpr_match(Op0_t Op0, Op1_t Op1) : Op0(Op0), Op1(Op1) {}

  /// Match a binary SCEV of type \c SCEVTy with the configured wrap flags.
  /// @param S SCEV to test.
  /// @return True if \p S is a matching binary expression.
  bool match(const SCEV *S) const {
    if (auto WrappingS = dyn_cast<SCEVNAryExpr>(S))
      if (WrappingS->getNoWrapFlags(WrapFlags) != WrapFlags)
        return false;

    auto *E = dyn_cast<SCEVTy>(S);
    return E && E->getNumOperands() == 2 &&
           ((Op0.match(E->getOperand(0).getPointer()) &&
             Op1.match(E->getOperand(1).getPointer())) ||
            (Commutable && Op0.match(E->getOperand(1).getPointer()) &&
             Op1.match(E->getOperand(0).getPointer())));
  }
};

/// Match a binary SCEV of type \c SCEVTy with operands matching \p Op0 and
/// \p Op1.
/// @param Op0 Pattern that must match the first operand.
/// @param Op1 Pattern that must match the second operand.
/// @return Matcher for a binary SCEVTy with the given operands.
template <typename SCEVTy, typename Op0_t, typename Op1_t,
          SCEV::NoWrapFlags WrapFlags = SCEV::FlagAnyWrap,
          bool Commutable = false>
inline SCEVBinaryExpr_match<SCEVTy, Op0_t, Op1_t, WrapFlags, Commutable>
m_scev_Binary(const Op0_t &Op0, const Op1_t &Op1) {
  return SCEVBinaryExpr_match<SCEVTy, Op0_t, Op1_t, WrapFlags, Commutable>(Op0,
                                                                           Op1);
}

/// Match a SCEVAddExpr with operands matching \p Op0 and \p Op1.
/// @param Op0 Pattern that must match the first addend.
/// @param Op1 Pattern that must match the second addend.
/// @return Matcher for an add SCEV with the given addends.
template <typename Op0_t, typename Op1_t>
inline SCEVBinaryExpr_match<SCEVAddExpr, Op0_t, Op1_t>
m_scev_Add(const Op0_t &Op0, const Op1_t &Op1) {
  return m_scev_Binary<SCEVAddExpr>(Op0, Op1);
}

/// Match a SCEVMulExpr with operands matching \p Op0 and \p Op1.
/// @param Op0 Pattern that must match the first factor.
/// @param Op1 Pattern that must match the second factor.
/// @return Matcher for a multiply SCEV with the given factors.
template <typename Op0_t, typename Op1_t>
inline SCEVBinaryExpr_match<SCEVMulExpr, Op0_t, Op1_t>
m_scev_Mul(const Op0_t &Op0, const Op1_t &Op1) {
  return m_scev_Binary<SCEVMulExpr>(Op0, Op1);
}

/// Match a commutative SCEVMulExpr with operands matching \p Op0 and \p Op1.
/// @param Op0 Pattern that must match one factor.
/// @param Op1 Pattern that must match the other factor.
/// @return Matcher for a commutative multiply SCEV with the given factors.
template <typename Op0_t, typename Op1_t>
inline SCEVBinaryExpr_match<SCEVMulExpr, Op0_t, Op1_t, SCEV::FlagAnyWrap, true>
m_scev_c_Mul(const Op0_t &Op0, const Op1_t &Op1) {
  return m_scev_Binary<SCEVMulExpr, Op0_t, Op1_t, SCEV::FlagAnyWrap, true>(Op0,
                                                                           Op1);
}

/// Match a commutative NUW SCEVMulExpr with operands matching \p Op0 and \p
/// Op1.
/// @param Op0 Pattern that must match one factor.
/// @param Op1 Pattern that must match the other factor.
/// @return Matcher for a commutative NUW multiply SCEV with the given factors.
template <typename Op0_t, typename Op1_t>
inline SCEVBinaryExpr_match<SCEVMulExpr, Op0_t, Op1_t, SCEV::FlagNUW, true>
m_scev_c_NUWMul(const Op0_t &Op0, const Op1_t &Op1) {
  return m_scev_Binary<SCEVMulExpr, Op0_t, Op1_t, SCEV::FlagNUW, true>(Op0,
                                                                       Op1);
}

/// Match a SCEVUDivExpr with operands matching \p Op0 and \p Op1.
/// @param Op0 Pattern that must match the dividend.
/// @param Op1 Pattern that must match the divisor.
/// @return Matcher for an unsigned-divide SCEV with the given operands.
template <typename Op0_t, typename Op1_t>
inline SCEVBinaryExpr_match<SCEVUDivExpr, Op0_t, Op1_t>
m_scev_UDiv(const Op0_t &Op0, const Op1_t &Op1) {
  return m_scev_Binary<SCEVUDivExpr>(Op0, Op1);
}

/// Match a commutative SCEVSMaxExpr with operands matching \p Op0 and \p Op1.
/// @param Op0 Pattern that must match one operand.
/// @param Op1 Pattern that must match the other operand.
/// @return Matcher for a commutative signed-max SCEV with the given operands.
template <typename Op0_t, typename Op1_t>
inline SCEVBinaryExpr_match<SCEVSMaxExpr, Op0_t, Op1_t, SCEV::FlagAnyWrap, true>
m_scev_SMax(const Op0_t &Op0, const Op1_t &Op1) {
  return m_scev_Binary<SCEVSMaxExpr, Op0_t, Op1_t, SCEV::FlagAnyWrap, true>(
      Op0, Op1);
}

/// Match a commutative SCEVUMaxExpr with operands matching \p Op0 and \p Op1.
/// @param Op0 Pattern that must match one operand.
/// @param Op1 Pattern that must match the other operand.
/// @return Matcher for a commutative unsigned-max SCEV with the given operands.
template <typename Op0_t, typename Op1_t>
inline SCEVBinaryExpr_match<SCEVUMaxExpr, Op0_t, Op1_t, SCEV::FlagAnyWrap, true>
m_scev_UMax(const Op0_t &Op0, const Op1_t &Op1) {
  return m_scev_Binary<SCEVUMaxExpr, Op0_t, Op1_t, SCEV::FlagAnyWrap, true>(
      Op0, Op1);
}

/// Match a SCEVMinMaxExpr with operands matching \p Op0 and \p Op1.
/// @param Op0 Pattern that must match the first operand.
/// @param Op1 Pattern that must match the second operand.
/// @return Matcher for a min/max SCEV with the given operands.
template <typename Op0_t, typename Op1_t>
inline SCEVBinaryExpr_match<SCEVMinMaxExpr, Op0_t, Op1_t>
m_scev_MinMax(const Op0_t &Op0, const Op1_t &Op1) {
  return m_scev_Binary<SCEVMinMaxExpr>(Op0, Op1);
}

/// Match unsigned remainder pattern.
/// Matches patterns generated by getURemExpr.
template <typename Op0_t, typename Op1_t> struct SCEVURem_match {
  /// Matcher for the dividend (left-hand side).
  Op0_t Op0;
  /// Matcher for the divisor (right-hand side).
  Op1_t Op1;
  /// ScalarEvolution instance used to rebuild URem forms.
  ScalarEvolution &SE;

  /// Construct a URem matcher with operand patterns and ScalarEvolution.
  /// @param Op0 Pattern that must match the dividend.
  /// @param Op1 Pattern that must match the divisor.
  /// @param SE ScalarEvolution used to recognize equivalent URem forms.
  SCEVURem_match(Op0_t Op0, Op1_t Op1, ScalarEvolution &SE)
      : Op0(Op0), Op1(Op1), SE(SE) {}

  /// Match an expression that represents unsigned remainder of Op0 by Op1.
  /// @param Expr SCEV to test.
  /// @return True if \p Expr matches a URem pattern for the operands.
  bool match(const SCEV *Expr) const {
    if (Expr->getType()->isPointerTy())
      return false;

    // Try to match 'zext (trunc A to iB) to iY', which is used
    // for URem with constant power-of-2 second operands. Make sure the size of
    // the operand A matches the size of the whole expressions.
    const SCEV *LHS;
    if (SCEVPatternMatch::match(Expr, m_scev_ZExt(m_scev_Trunc(m_SCEV(LHS))))) {
      Type *TruncTy = cast<SCEVZeroExtendExpr>(Expr)->getOperand()->getType();
      // Bail out if the type of the LHS is larger than the type of the
      // expression for now.
      if (SE.getTypeSizeInBits(LHS->getType()) >
          SE.getTypeSizeInBits(Expr->getType()))
        return false;
      if (LHS->getType() != Expr->getType())
        LHS = SE.getZeroExtendExpr(LHS, Expr->getType());
      const SCEV *RHS =
          SE.getConstant(APInt(SE.getTypeSizeInBits(Expr->getType()), 1)
                         << SE.getTypeSizeInBits(TruncTy));
      return Op0.match(LHS) && Op1.match(RHS);
    }

    const SCEV *A;
    const SCEVMulExpr *Mul;
    if (!SCEVPatternMatch::match(Expr, m_scev_Add(m_scev_Mul(Mul), m_SCEV(A))))
      return false;

    // URem is represented as `A - ((A udiv B) * B)`. Only construct the complex
    // SCEV expression, if the multiply of the expression to check has a UDiv
    // operand.
    if (none_of(Mul->operands(),
                [](const SCEV *Op) { return isa<SCEVUDivExpr>(Op); }))
      return false;

    const auto MatchURemWithDivisor = [&](const SCEV *B) {
      // (SomeExpr + (-(SomeExpr / B) * B)).
      if (Expr == SE.getURemExpr(A, B))
        return Op0.match(A) && Op1.match(B);
      return false;
    };

    // (SomeExpr + (-1 * (SomeExpr / B) * B)).
    if (Mul->getNumOperands() == 3 && isa<SCEVConstant>(Mul->getOperand(0)))
      return MatchURemWithDivisor(Mul->getOperand(1)) ||
             MatchURemWithDivisor(Mul->getOperand(2));

    // (SomeExpr + ((-SomeExpr / B) * B)) or (SomeExpr + ((SomeExpr / B) * -B)).
    if (Mul->getNumOperands() == 2)
      return MatchURemWithDivisor(Mul->getOperand(1)) ||
             MatchURemWithDivisor(Mul->getOperand(0)) ||
             MatchURemWithDivisor(SE.getNegativeSCEV(Mul->getOperand(1))) ||
             MatchURemWithDivisor(SE.getNegativeSCEV(Mul->getOperand(0)));
    return false;
  }
};

/// Match an unsigned-remainder SCEV pattern for dividend and divisor.
///
/// Match the mathematical pattern A - (A / B) * B, where A and B can be
/// arbitrary expressions. Also match zext (trunc A to iB) to iY, which is used
/// for URem with constant power-of-2 second operands. It's not always easy, as
/// A and B can be folded (imagine A is X / 2, and B is 4, A / B becomes X / 8).
/// @param LHS Pattern that must match the dividend.
/// @param RHS Pattern that must match the divisor.
/// @param SE ScalarEvolution used to recognize equivalent URem forms.
/// @return Matcher for an unsigned-remainder SCEV of \p LHS by \p RHS.
template <typename Op0_t, typename Op1_t>
inline SCEVURem_match<Op0_t, Op1_t> m_scev_URem(Op0_t LHS, Op1_t RHS,
                                                ScalarEvolution &SE) {
  return SCEVURem_match<Op0_t, Op1_t>(LHS, RHS, SE);
}

/// Match any Loop.
/// @return Matcher that accepts any Loop.
inline auto m_Loop() { return m_Isa<const Loop>(); }

/// Match an affine SCEVAddRecExpr.
template <typename Op0_t, typename Op1_t, typename Loop_t>
struct SCEVAffineAddRec_match {
  /// Binary matcher for the add-recurrence start and step.
  SCEVBinaryExpr_match<SCEVAddRecExpr, Op0_t, Op1_t> Ops;
  /// Matcher for the loop associated with the add recurrence.
  Loop_t Loop;

  /// Construct an affine add-recurrence matcher.
  /// @param Op0 Pattern that must match the recurrence start.
  /// @param Op1 Pattern that must match the recurrence step.
  /// @param Loop Pattern that must match the recurrence loop.
  SCEVAffineAddRec_match(Op0_t Op0, Op1_t Op1, Loop_t Loop)
      : Ops(Op0, Op1), Loop(Loop) {}

  /// Match an affine add recurrence whose operands and loop match.
  /// @param S SCEV to test.
  /// @return True if \p S is a matching affine SCEVAddRecExpr.
  bool match(const SCEV *S) const {
    return Ops.match(S) && Loop.match(cast<SCEVAddRecExpr>(S)->getLoop());
  }
};

/// Match a specified const Loop*.
struct specificloop_ty {
  /// The exact Loop that must be matched.
  const Loop *L;

  /// Construct a matcher for the specific loop \p L.
  /// @param L Loop that a successful match must equal.
  specificloop_ty(const Loop *L) : L(L) {}

  /// Return true if \p L is the same pointer as \c this->L.
  /// @param L Loop to compare against the stored loop.
  /// @return True if \p L equals the stored loop.
  bool match(const Loop *L) const { return L == this->L; }
};

/// Match the specific loop \p L.
/// @param L Exact Loop that must be matched.
/// @return Matcher for the exact Loop \p L.
inline specificloop_ty m_SpecificLoop(const Loop *L) { return L; }

/// Match a Loop, capturing it if we match.
/// @param L Reference that receives the matched Loop.
/// @return Binder that captures the matched Loop into \p L.
inline match_bind<const Loop> m_Loop(const Loop *&L) { return L; }

/// Match an affine SCEVAddRecExpr with any loop.
/// @param Op0 Pattern that must match the recurrence start.
/// @param Op1 Pattern that must match the recurrence step.
/// @return Matcher for an affine add recurrence over any loop.
template <typename Op0_t, typename Op1_t>
inline SCEVAffineAddRec_match<Op0_t, Op1_t, match_isa<const Loop>>
m_scev_AffineAddRec(const Op0_t &Op0, const Op1_t &Op1) {
  return SCEVAffineAddRec_match<Op0_t, Op1_t, match_isa<const Loop>>(Op0, Op1,
                                                                     m_Loop());
}

/// Match an affine SCEVAddRecExpr whose loop matches \p L.
/// @param Op0 Pattern that must match the recurrence start.
/// @param Op1 Pattern that must match the recurrence step.
/// @param L Pattern that must match the recurrence loop.
/// @return Matcher for an affine add recurrence with the given loop pattern.
template <typename Op0_t, typename Op1_t, typename Loop_t>
inline SCEVAffineAddRec_match<Op0_t, Op1_t, Loop_t>
m_scev_AffineAddRec(const Op0_t &Op0, const Op1_t &Op1, const Loop_t &L) {
  return SCEVAffineAddRec_match<Op0_t, Op1_t, Loop_t>(Op0, Op1, L);
}

/// Matcher for an SCEVUnknown wrapping undef or poison.
struct is_undef_or_poison {
  /// Match an SCEVUnknown whose underlying value is undef or poison.
  /// @param S SCEV to test.
  /// @return True if \p S wraps an undef or poison value.
  bool match(const SCEV *S) const {
    const SCEVUnknown *Unknown;
    return SCEVPatternMatch::match(S, m_SCEVUnknown(Unknown)) &&
           isa<UndefValue>(Unknown->getValue());
  }
};

/// Match an SCEVUnknown wrapping undef or poison.
/// @return Matcher for an SCEVUnknown wrapping undef or poison.
inline is_undef_or_poison m_scev_UndefOrPoison() {
  return is_undef_or_poison();
}

} // namespace SCEVPatternMatch
} // namespace llvm

#endif
