//===-- CmpInstAnalysis.h - Utils to help fold compare insts ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file holds routines to help analyse compare instructions
// and fold them into constants or other compare instructions
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_CMPINSTANALYSIS_H
#define LLVM_ANALYSIS_CMPINSTANALYSIS_H

#include "llvm/ADT/APInt.h"
#include "llvm/IR/InstrTypes.h"

namespace llvm {
  class Type;
  class Value;

  /// Encode a icmp predicate into a three bit mask. These bits are carefully
  /// arranged to allow folding of expressions such as:
  ///
  ///      (A < B) | (A > B) --> (A != B)
  ///
  /// Note that this is only valid if the first and second predicates have the
  /// same sign. It is illegal to do: (A u< B) | (A s> B)
  ///
  /// Three bits are used to represent the condition, as follows:
  ///   0  A > B
  ///   1  A == B
  ///   2  A < B
  ///
  /// <=>  Value  Definition
  /// 000     0   Always false
  /// 001     1   A >  B
  /// 010     2   A == B
  /// 011     3   A >= B
  /// 100     4   A <  B
  /// 101     5   A != B
  /// 110     6   A <= B
  /// 111     7   Always true
  ///
  /// @param Pred ICmp predicate to encode into the three-bit mask.
  /// @return Three-bit predicate encoding for \p Pred.
  LLVM_ABI unsigned getICmpCode(CmpInst::Predicate Pred);

  /// Convert a predicate code into a constant or an ICmp predicate.
  ///
  /// This is the complement of getICmpCode. It turns a predicate code into
  /// either a constant true or false or the predicate for a new ICmp.
  /// The sign is passed in to determine which kind of predicate to use in the
  /// new ICmp instruction.
  /// Non-NULL return value will be a true or false constant.
  /// NULL return means a new ICmp is needed. The predicate is output in Pred.
  /// @param Code Three-bit predicate encoding produced by getICmpCode.
  /// @param Sign True for signed predicates; false for unsigned.
  /// @param OpTy Operand type used when constructing a constant result.
  /// @param Pred Output predicate for a new ICmp when the result is not constant.
  /// @return True or false constant when \p Code is tautological; otherwise
  /// nullptr and \p Pred is set for a new ICmp.
  LLVM_ABI Constant *getPredForICmpCode(unsigned Code, bool Sign, Type *OpTy,
                                        CmpInst::Predicate &Pred);

  /// Return true if both predicates match sign or if at least one of them is an
  /// equality comparison (which is signless).
  /// @param P1 First compare predicate.
  /// @param P2 Second compare predicate.
  /// @return True if the predicates share a sign or either is an equality.
  LLVM_ABI bool predicatesFoldable(CmpInst::Predicate P1,
                                   CmpInst::Predicate P2);

  /// Similar to getICmpCode but for FCmpInst. This encodes a fcmp predicate
  /// into a four bit mask.
  /// @param CC FCmp predicate to encode into the four-bit mask.
  /// @return Four-bit predicate encoding for \p CC.
  inline unsigned getFCmpCode(CmpInst::Predicate CC) {
    assert(CmpInst::FCMP_FALSE <= CC && CC <= CmpInst::FCMP_TRUE &&
           "Unexpected FCmp predicate!");
    // Take advantage of the bit pattern of CmpInst::Predicate here.
    //                                          U L G E
    static_assert(CmpInst::FCMP_FALSE == 0); // 0 0 0 0
    static_assert(CmpInst::FCMP_OEQ == 1);   // 0 0 0 1
    static_assert(CmpInst::FCMP_OGT == 2);   // 0 0 1 0
    static_assert(CmpInst::FCMP_OGE == 3);   // 0 0 1 1
    static_assert(CmpInst::FCMP_OLT == 4);   // 0 1 0 0
    static_assert(CmpInst::FCMP_OLE == 5);   // 0 1 0 1
    static_assert(CmpInst::FCMP_ONE == 6);   // 0 1 1 0
    static_assert(CmpInst::FCMP_ORD == 7);   // 0 1 1 1
    static_assert(CmpInst::FCMP_UNO == 8);   // 1 0 0 0
    static_assert(CmpInst::FCMP_UEQ == 9);   // 1 0 0 1
    static_assert(CmpInst::FCMP_UGT == 10);  // 1 0 1 0
    static_assert(CmpInst::FCMP_UGE == 11);  // 1 0 1 1
    static_assert(CmpInst::FCMP_ULT == 12);  // 1 1 0 0
    static_assert(CmpInst::FCMP_ULE == 13);  // 1 1 0 1
    static_assert(CmpInst::FCMP_UNE == 14);  // 1 1 1 0
    static_assert(CmpInst::FCMP_TRUE == 15); // 1 1 1 1
    return CC;
  }

  /// Convert a predicate code into a constant or an FCmp predicate.
  ///
  /// This is the complement of getFCmpCode. It turns a predicate code into
  /// either a constant true or false or the predicate for a new FCmp.
  /// Non-NULL return value will be a true or false constant.
  /// NULL return means a new FCmp is needed. The predicate is output in Pred.
  /// @param Code Four-bit predicate encoding produced by getFCmpCode.
  /// @param OpTy Operand type used when constructing a constant result.
  /// @param Pred Output predicate for a new FCmp when the result is not constant.
  /// @return True or false constant when \p Code is tautological; otherwise
  /// nullptr and \p Pred is set for a new FCmp.
  LLVM_ABI Constant *getPredForFCmpCode(unsigned Code, Type *OpTy,
                                        CmpInst::Predicate &Pred);

  /// Represents the operation icmp (X & Mask) pred C, where pred can only be
  /// eq or ne.
  struct DecomposedBitTest {
    /// Value being tested after masking.
    Value *X;
    /// Equality predicate (\c eq or \c ne) for the bit test.
    CmpInst::Predicate Pred;
    /// Bit mask applied to \c X before the comparison.
    APInt Mask;
    /// Constant compared against the masked value.
    APInt C;
  };

  /// Decompose an icmp into the form ((X & Mask) pred C) if possible.
  ///
  /// Unless \p AllowNonZeroC is true, C will always be 0. If \p DecomposeAnd is
  /// specified, then, for equality predicates, this will decompose bitmasking
  /// via `and`.
  /// @param LHS Left-hand operand of the icmp.
  /// @param RHS Right-hand operand of the icmp.
  /// @param Pred Predicate of the icmp to decompose.
  /// @param LookThroughTrunc When true, look through truncates on the operands.
  /// @param AllowNonZeroC When true, allow a non-zero constant \c C.
  /// @param DecomposeAnd When true, decompose equality bitmasking via \c and.
  /// @return Decomposed bit-test form when possible; otherwise \c std::nullopt.
  LLVM_ABI std::optional<DecomposedBitTest>
  decomposeBitTestICmp(Value *LHS, Value *RHS, CmpInst::Predicate Pred,
                       bool LookThroughTrunc = true, bool AllowNonZeroC = false,
                       bool DecomposeAnd = false);

  /// Decompose an icmp into the form ((X & Mask) pred C) if possible.
  ///
  /// Unless \p AllowNonZeroC is true, C will always be 0. If \p DecomposeAnd is
  /// specified, then, for equality predicates, this will decompose bitmasking
  /// via `and`.
  /// @param Cond Condition value expected to be an icmp (possibly after casts).
  /// @param LookThroughTrunc When true, look through truncates on the operands.
  /// @param AllowNonZeroC When true, allow a non-zero constant \c C.
  /// @param DecomposeAnd When true, decompose equality bitmasking via \c and.
  /// @return Decomposed bit-test form when possible; otherwise \c std::nullopt.
  LLVM_ABI std::optional<DecomposedBitTest>
  decomposeBitTest(Value *Cond, bool LookThroughTrunc = true,
                   bool AllowNonZeroC = false, bool DecomposeAnd = false);

} // end namespace llvm

#endif
