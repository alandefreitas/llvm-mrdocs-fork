//===- llvm/Analysis/FloatingPointPredicateUtils.h ------------*- C++ -*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_FLOATINGPOINTPREDICATEUTILS_H
#define LLVM_ANALYSIS_FLOATINGPOINTPREDICATEUTILS_H

#include "llvm/IR/GenericFloatingPointPredicateUtils.h"
#include "llvm/IR/SSAContext.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

/// Floating-point predicate utilities specialized for LLVM IR.
using FloatingPointPredicateUtils =
    GenericFloatingPointPredicateUtils<SSAContext>;

template <>
LLVM_ABI DenormalMode
FloatingPointPredicateUtils::queryDenormalMode(const Function &F, Value *Val);

template <>
LLVM_ABI bool FloatingPointPredicateUtils::lookThroughFAbs(const Function &F,
                                                           Value *LHS,
                                                           Value *&Src);

template <>
LLVM_ABI std::optional<APFloat>
FloatingPointPredicateUtils::matchConstantFloat(const Function &F, Value *Val);

/// Returns a pair of values, which if passed to llvm.is.fpclass, returns the
/// same result as an fcmp with the given operands.
///
/// If \p LookThroughSrc is true, consider the input value when computing the
/// mask.
///
/// If \p LookThroughSrc is false, ignore the source value (i.e. the first pair
/// element will always be LHS.
/// @param Pred FCmp predicate of the compare to convert.
/// @param F Function providing denormal mode for the operands.
/// @param LHS Left-hand operand of the compare.
/// @param RHS Right-hand operand of the compare.
/// @param LookThroughSrc Whether to look through sign-bit operations on \p LHS.
/// @return Pair of (value, class mask) equivalent to the fcmp when passed to
/// llvm.is.fpclass.
inline std::pair<Value *, FPClassTest>
fcmpToClassTest(FCmpInst::Predicate Pred, const Function &F, Value *LHS,
                Value *RHS, bool LookThroughSrc = true) {
  return FloatingPointPredicateUtils::fcmpToClassTest(Pred, F, LHS, RHS,
                                                      LookThroughSrc = true);
}

/// Returns a pair of values, which if passed to llvm.is.fpclass, returns the
/// same result as an fcmp with the given operands.
///
/// If \p LookThroughSrc is true, consider the input value when computing the
/// mask.
///
/// If \p LookThroughSrc is false, ignore the source value (i.e. the first pair
/// element will always be LHS.
/// @param Pred FCmp predicate of the compare to convert.
/// @param F Function providing denormal mode for the operands.
/// @param LHS Left-hand operand of the compare.
/// @param ConstRHS Constant right-hand operand of the compare.
/// @param LookThroughSrc Whether to look through sign-bit operations on \p LHS.
/// @return Pair of (value, class mask) equivalent to the fcmp when passed to
/// llvm.is.fpclass.
inline std::pair<Value *, FPClassTest>
fcmpToClassTest(FCmpInst::Predicate Pred, const Function &F, Value *LHS,
                const APFloat *ConstRHS, bool LookThroughSrc = true) {
  return FloatingPointPredicateUtils::fcmpToClassTest(Pred, F, LHS, *ConstRHS,
                                                      LookThroughSrc);
}

/// Compute the floating-point classes implied for \p LHS by an fcmp against a
/// class mask.
///
/// Returns {TestedValue, ClassesIfTrue, ClassesIfFalse}. If the compare is an
/// exact class test, ClassesIfTrue == ~ClassesIfFalse. This is a less exact
/// version of fcmpToClassTest (e.g. fcmpToClassTest will only succeed for a
/// test of x > 0 implies positive, but not x > 1).
///
/// If \p LookThroughSrc is true, consider the input value when computing the
/// mask. This may look through sign bit operations. If \p LookThroughSrc is
/// false, ignore the source value (i.e. the first tuple element will always be
/// LHS).
/// @param Pred FCmp predicate of the compare.
/// @param F Function providing denormal mode for the operands.
/// @param LHS Left-hand operand of the compare.
/// @param RHSClass Known floating-point class of the right-hand operand.
/// @param LookThroughSrc Whether to look through sign-bit operations on \p LHS.
/// @return Tuple of (tested value, classes if true, classes if false) implied by
/// the fcmp.
inline std::tuple<Value *, FPClassTest, FPClassTest>
fcmpImpliesClass(CmpInst::Predicate Pred, const Function &F, Value *LHS,
                 FPClassTest RHSClass, bool LookThroughSrc = true) {
  return FloatingPointPredicateUtils::fcmpImpliesClass(Pred, F, LHS, RHSClass,
                                                       LookThroughSrc);
}

/// Compute the floating-point classes implied for \p LHS by an fcmp against a
/// constant.
///
/// Returns {TestedValue, ClassesIfTrue, ClassesIfFalse}. If the compare is an
/// exact class test, ClassesIfTrue == ~ClassesIfFalse. This is a less exact
/// version of fcmpToClassTest (e.g. fcmpToClassTest will only succeed for a
/// test of x > 0 implies positive, but not x > 1).
///
/// If \p LookThroughSrc is true, consider the input value when computing the
/// mask. This may look through sign bit operations. If \p LookThroughSrc is
/// false, ignore the source value (i.e. the first tuple element will always be
/// LHS).
/// @param Pred FCmp predicate of the compare.
/// @param F Function providing denormal mode for the operands.
/// @param LHS Left-hand operand of the compare.
/// @param ConstRHS Constant right-hand operand of the compare.
/// @param LookThroughSrc Whether to look through sign-bit operations on \p LHS.
/// @return Tuple of (tested value, classes if true, classes if false) implied by
/// the fcmp.
inline std::tuple<Value *, FPClassTest, FPClassTest>
fcmpImpliesClass(CmpInst::Predicate Pred, const Function &F, Value *LHS,
                 const APFloat &ConstRHS, bool LookThroughSrc = true) {
  return FloatingPointPredicateUtils::fcmpImpliesClass(Pred, F, LHS, ConstRHS,
                                                       LookThroughSrc);
}

/// Compute the floating-point classes implied for \p LHS by an fcmp against a
/// value.
///
/// Returns {TestedValue, ClassesIfTrue, ClassesIfFalse}. If the compare is an
/// exact class test, ClassesIfTrue == ~ClassesIfFalse. This is a less exact
/// version of fcmpToClassTest (e.g. fcmpToClassTest will only succeed for a
/// test of x > 0 implies positive, but not x > 1).
///
/// If \p LookThroughSrc is true, consider the input value when computing the
/// mask. This may look through sign bit operations. If \p LookThroughSrc is
/// false, ignore the source value (i.e. the first tuple element will always be
/// LHS).
/// @param Pred FCmp predicate of the compare.
/// @param F Function providing denormal mode for the operands.
/// @param LHS Left-hand operand of the compare.
/// @param RHS Right-hand operand of the compare.
/// @param LookThroughSrc Whether to look through sign-bit operations on \p LHS.
/// @return Tuple of (tested value, classes if true, classes if false) implied by
/// the fcmp.
inline std::tuple<Value *, FPClassTest, FPClassTest>
fcmpImpliesClass(CmpInst::Predicate Pred, const Function &F, Value *LHS,
                 Value *RHS, bool LookThroughSrc = true) {
  return FloatingPointPredicateUtils::fcmpImpliesClass(Pred, F, LHS, RHS,
                                                       LookThroughSrc);
}

} // namespace llvm

#endif // LLVM_ANALYSIS_FLOATINGPOINTPREDICATEUTILS_H
