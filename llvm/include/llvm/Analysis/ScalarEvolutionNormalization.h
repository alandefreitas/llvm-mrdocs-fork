//===- llvm/Analysis/ScalarEvolutionNormalization.h - See below -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines utilities for working with "normalized" ScalarEvolution
// expressions.
//
// The following example illustrates post-increment uses and how normalized
// expressions help.
//
//   for (i=0; i!=n; ++i) {
//     ...
//   }
//   use(i);
//
// While the expression for most uses of i inside the loop is {0,+,1}<%L>, the
// expression for the use of i outside the loop is {1,+,1}<%L>, since i is
// incremented at the end of the loop body. This is inconveient, since it
// suggests that we need two different induction variables, one that starts
// at 0 and one that starts at 1. We'd prefer to be able to think of these as
// the same induction variable, with uses inside the loop using the
// "pre-incremented" value, and uses after the loop using the
// "post-incremented" value.
//
// Expressions for post-incremented uses are represented as an expression
// paired with a set of loops for which the expression is in "post-increment"
// mode (there may be multiple loops).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_SCALAREVOLUTIONNORMALIZATION_H
#define LLVM_ANALYSIS_SCALAREVOLUTIONNORMALIZATION_H

#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class Loop;
class ScalarEvolution;
class SCEV;
class SCEVAddRecExpr;

/// Set of loops for which a SCEV is in post-increment mode.
typedef SmallPtrSet<const Loop *, 2> PostIncLoopSet;

/// Predicate selecting add-recurrence SCEVs to normalize for post-increment use.
typedef function_ref<bool(const SCEVAddRecExpr *)> NormalizePredTy;

/// Normalize \p S to be post-increment for all loops present in \p Loops.
/// @param S SCEV expression to normalize.
/// @param Loops Loops for which \p S should be treated as post-increment.
/// @param SE Scalar evolution used to rebuild expressions.
/// @param CheckInvertible When true, return nullptr if the result is not
/// invertible.
/// @return The normalized SCEV, or nullptr if \p CheckInvertible is true and
/// the result is not invertible.
LLVM_ABI const SCEV *normalizeForPostIncUse(const SCEV *S,
                                            const PostIncLoopSet &Loops,
                                            ScalarEvolution &SE,
                                            bool CheckInvertible = true);

/// Normalize \p S for all add recurrence sub-expressions for which \p Pred
/// returns true.
/// @param S SCEV expression to normalize.
/// @param Pred Predicate selecting which add-recurrence sub-expressions to
/// normalize.
/// @param SE Scalar evolution used to rebuild expressions.
/// @return The normalized SCEV.
LLVM_ABI const SCEV *normalizeForPostIncUseIf(const SCEV *S,
                                              NormalizePredTy Pred,
                                              ScalarEvolution &SE);

/// Denormalize \p S to be post-increment for all loops present in \p Loops.
/// @param S SCEV expression to denormalize.
/// @param Loops Loops for which \p S should be treated as post-increment.
/// @param SE Scalar evolution used to rebuild expressions.
/// @return The denormalized SCEV.
LLVM_ABI const SCEV *denormalizeForPostIncUse(const SCEV *S,
                                              const PostIncLoopSet &Loops,
                                              ScalarEvolution &SE);
} // namespace llvm

#endif
