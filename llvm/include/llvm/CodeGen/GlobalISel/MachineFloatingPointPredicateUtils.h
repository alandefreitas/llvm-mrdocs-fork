//===-- MachineFloatingPointModeUtils.h -----*- C++ ---------------------*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEFLOATINGPOINTPREDICATEUTILS_H
#define LLVM_CODEGEN_MACHINEFLOATINGPOINTPREDICATEUTILS_H

#include "llvm/CodeGen/MachineSSAContext.h"
#include "llvm/IR/GenericFloatingPointPredicateUtils.h"

namespace llvm {

/// Floating-point predicate utilities specialized for Machine IR.
using MachineFloatingPointPredicateUtils =
    GenericFloatingPointPredicateUtils<MachineSSAContext>;

template <>
DenormalMode
MachineFloatingPointPredicateUtils::queryDenormalMode(const MachineFunction &MF,
                                                      Register Val);

template <>
bool MachineFloatingPointPredicateUtils::lookThroughFAbs(
    const MachineFunction &MF, Register LHS, Register &Src);

template <>
std::optional<APFloat> MachineFloatingPointPredicateUtils::matchConstantFloat(
    const MachineFunction &MF, Register Val);

/// Compute the possible floating-point classes that \p LHS could be based on
/// fcmp \Pred \p LHS, \p RHS.
///
/// \returns { TestedValue, ClassesIfTrue, ClassesIfFalse }
///
/// If the compare returns an exact class test, ClassesIfTrue ==
/// ~ClassesIfFalse
///
/// This is a less exact version of fcmpToClassTest (e.g. fcmpToClassTest will
/// only succeed for a test of x > 0 implies positive, but not x > 1).
///
/// If \p LookThroughSrc is true, consider the input value when computing the
/// mask. This may look through sign bit operations.
///
/// If \p LookThroughSrc is false, ignore the source value (i.e. the first
/// pair element will always be LHS.
///
/// \param Pred Floating-point compare predicate.
/// \param MF Machine function providing context for the compare.
/// \param LHS Left-hand side register of the compare.
/// \param RHS Right-hand side register of the compare.
/// \param LookThroughSrc Whether to look through source value operations such
///        as sign-bit manipulations when computing the class mask.
inline std::tuple<Register, FPClassTest, FPClassTest>
fcmpImpliesClass(CmpInst::Predicate Pred, const MachineFunction &MF,
                 Register LHS, Register RHS, bool LookThroughSrc = true) {
  return MachineFloatingPointPredicateUtils::fcmpImpliesClass(
      Pred, MF, LHS, RHS, LookThroughSrc);
}

} // namespace llvm

#endif // LLVM_CODEGEN_MACHINEFLOATINGPOINTPREDICATEUTILS_H
