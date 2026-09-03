//===- llvm/Analysis/ScalarEvolutionDivision.h - See below ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the class that knows how to divide SCEV's.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_SCALAREVOLUTIONDIVISION_H
#define LLVM_ANALYSIS_SCALAREVOLUTIONDIVISION_H

#include "llvm/Analysis/ScalarEvolutionExpressions.h"

namespace llvm {

class SCEV;

class ScalarEvolution;

struct SCEVCouldNotCompute;

/// SCEV visitor that divides a numerator expression by a fixed denominator.
struct SCEVDivision : public SCEVVisitor<SCEVDivision, void> {
public:
  /// Compute quotient and remainder SCEVs for dividing Numerator by Denominator.
  ///
  /// We are not actually performing the division here. Instead, we are trying
  /// to find SCEV expressions Quotient and Remainder that satisfy:
  ///
  /// Numerator = Denominator * Quotient + Remainder
  ///
  /// There may be multiple valid answers for Quotient and Remainder. This
  /// function finds one of them. Especially, there is always a trivial
  /// solution: (Quotient, Remainder) = (0, Numerator).
  ///
  /// Note the following:
  /// * The condition Remainder < Denominator is NOT necessarily required.
  /// * Division of constants is performed as signed.
  /// * The multiplication of Quotient and Denominator may wrap.
  /// * The addition of Quotient*Denominator and Remainder may wrap.
  /// @param SE ScalarEvolution used to build and simplify result expressions.
  /// @param Numerator Dividend SCEV expression.
  /// @param Denominator Divisor SCEV expression.
  /// @param Quotient Output set to the computed quotient SCEV.
  /// @param Remainder Output set to the computed remainder SCEV.
  LLVM_ABI static void divide(ScalarEvolution &SE, const SCEV *Numerator,
                              const SCEV *Denominator, const SCEV **Quotient,
                              const SCEV **Remainder);

  // Except in the trivial case described above, we do not know how to divide
  // Expr by Denominator for the following functions with empty implementation.
  /// Leave the trivial quotient/remainder for a pointer-to-address numerator.
  /// @param Numerator Pointer-to-address expression used as the dividend.
  void visitPtrToAddrExpr(const SCEVPtrToAddrExpr *Numerator) {}
  /// Leave the trivial quotient/remainder for a truncate numerator.
  /// @param Numerator Truncate expression used as the dividend.
  void visitTruncateExpr(const SCEVTruncateExpr *Numerator) {}
  /// Leave the trivial quotient/remainder for a zero-extend numerator.
  /// @param Numerator Zero-extend expression used as the dividend.
  void visitZeroExtendExpr(const SCEVZeroExtendExpr *Numerator) {}
  /// Leave the trivial quotient/remainder for a sign-extend numerator.
  /// @param Numerator Sign-extend expression used as the dividend.
  void visitSignExtendExpr(const SCEVSignExtendExpr *Numerator) {}
  /// Leave the trivial quotient/remainder for an unsigned-divide numerator.
  /// @param Numerator Unsigned-divide expression used as the dividend.
  void visitUDivExpr(const SCEVUDivExpr *Numerator) {}
  /// Leave the trivial quotient/remainder for a signed-max numerator.
  /// @param Numerator Signed-max expression used as the dividend.
  void visitSMaxExpr(const SCEVSMaxExpr *Numerator) {}
  /// Leave the trivial quotient/remainder for an unsigned-max numerator.
  /// @param Numerator Unsigned-max expression used as the dividend.
  void visitUMaxExpr(const SCEVUMaxExpr *Numerator) {}
  /// Leave the trivial quotient/remainder for a signed-min numerator.
  /// @param Numerator Signed-min expression used as the dividend.
  void visitSMinExpr(const SCEVSMinExpr *Numerator) {}
  /// Leave the trivial quotient/remainder for an unsigned-min numerator.
  /// @param Numerator Unsigned-min expression used as the dividend.
  void visitUMinExpr(const SCEVUMinExpr *Numerator) {}
  /// Leave the trivial quotient/remainder for a sequential unsigned-min.
  /// @param Numerator Sequential unsigned-min expression used as the dividend.
  void visitSequentialUMinExpr(const SCEVSequentialUMinExpr *Numerator) {}
  /// Leave the trivial quotient/remainder for an unknown SCEV numerator.
  /// @param Numerator Unknown SCEV expression used as the dividend.
  void visitUnknown(const SCEVUnknown *Numerator) {}
  /// Leave the trivial quotient/remainder for a could-not-compute numerator.
  /// @param Numerator Could-not-compute marker used as the dividend.
  void visitCouldNotCompute(const SCEVCouldNotCompute *Numerator) {}

  /// Divide a constant SCEV by the denominator when possible.
  /// @param Numerator Constant expression used as the dividend.
  LLVM_ABI void visitConstant(const SCEVConstant *Numerator);

  /// Divide a vscale SCEV by the denominator when possible.
  /// @param Numerator VScale expression used as the dividend.
  LLVM_ABI void visitVScale(const SCEVVScale *Numerator);

  /// Divide an add-recurrence SCEV by the denominator when possible.
  /// @param Numerator Add-recurrence expression used as the dividend.
  LLVM_ABI void visitAddRecExpr(const SCEVAddRecExpr *Numerator);

  /// Divide an add SCEV by the denominator when possible.
  /// @param Numerator Add expression used as the dividend.
  LLVM_ABI void visitAddExpr(const SCEVAddExpr *Numerator);

  /// Divide a multiply SCEV by the denominator when possible.
  /// @param Numerator Multiply expression used as the dividend.
  LLVM_ABI void visitMulExpr(const SCEVMulExpr *Numerator);

private:
  SCEVDivision(ScalarEvolution &S, const SCEV *Numerator,
               const SCEV *Denominator);

  // Convenience function for giving up on the division. We set the quotient to
  // be equal to zero and the remainder to be equal to the numerator.
  void cannotDivide(const SCEV *Numerator);

  ScalarEvolution &SE;
  const SCEV *Denominator, *Quotient, *Remainder, *Zero, *One;
};

/// Printer pass that shows SCEVDivision results for signed divides in a function.
class SCEVDivisionPrinterPass
    : public RequiredPassInfoMixin<SCEVDivisionPrinterPass> {
  raw_ostream &OS;
  void runImpl(Function &F, ScalarEvolution &SE);

public:
  /// Construct a SCEV division printer that writes to \p OS.
  /// @param OS Output stream for the printed division results.
  explicit SCEVDivisionPrinterPass(raw_ostream &OS) : OS(OS) {}
  /// Print SCEVDivision results for signed divides in \p F.
  /// @param F Function whose signed divides are analyzed.
  /// @param AM Function analysis manager providing ScalarEvolution.
  /// @return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_SCALAREVOLUTIONDIVISION_H
