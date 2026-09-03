//===-- OverflowInstAnalysis.h - Utils to fold overflow insts ----*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file holds routines to help analyse overflow instructions
// and fold them into constants or other overflow instructions
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_OVERFLOWINSTANALYSIS_H
#define LLVM_ANALYSIS_OVERFLOWINSTANALYSIS_H

#include "llvm/Support/Compiler.h"

namespace llvm {
class Use;
class Value;

/// Return true if \p Op0 and \p Op1 match a zero check with mul-with-overflow.
///
/// Match one of the patterns up to the select/logic op:
///   %Op0 = icmp ne i4 %X, 0
///   %Agg = call { i4, i1 } @llvm.[us]mul.with.overflow.i4(i4 %X, i4 %Y)
///   %Op1 = extractvalue { i4, i1 } %Agg, 1
///   %ret = select i1 %Op0, i1 %Op1, i1 false / %ret = and i1 %Op0, %Op1
///
///   %Op0 = icmp eq i4 %X, 0
///   %Agg = call { i4, i1 } @llvm.[us]mul.with.overflow.i4(i4 %X, i4 %Y)
///   %NotOp1 = extractvalue { i4, i1 } %Agg, 1
///   %Op1 = xor i1 %NotOp1, true
///   %ret = select i1 %Op0, i1 true, i1 %Op1 / %ret = or i1 %Op0, %Op1
///
/// Callers are expected to align that with the operands of the select/logic.
/// \p IsAnd should be true when matching the first (and/select-false) pattern.
/// If \p Op0 and \p Op1 match one of the patterns above, return true and fill
/// \p Y's use.
/// @param Op0 Zero-comparison operand of the select or logic operation.
/// @param Op1 Overflow-bit (or inverted overflow-bit) operand.
/// @param IsAnd True for the and/select-false pattern; false for or/select-true.
/// @param Y Set to the use of the non-zero multiplicand when a match is found.
/// @return True if \p Op0 and \p Op1 match one of the patterns above.
LLVM_ABI bool isCheckForZeroAndMulWithOverflow(Value *Op0, Value *Op1,
                                               bool IsAnd, Use *&Y);
/// Return true if \p Op0 and \p Op1 match a zero check with mul-with-overflow.
///
/// Convenience overload that ignores the matched multiplicand use.
/// @param Op0 Zero-comparison operand of the select or logic operation.
/// @param Op1 Overflow-bit (or inverted overflow-bit) operand.
/// @param IsAnd True for the and/select-false pattern; false for or/select-true.
/// @return True if \p Op0 and \p Op1 match a zero check with mul-with-overflow.
LLVM_ABI bool isCheckForZeroAndMulWithOverflow(Value *Op0, Value *Op1,
                                               bool IsAnd);
} // end namespace llvm

#endif
