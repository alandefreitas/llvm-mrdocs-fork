//===- llvm/Transforms/Utils/BypassSlowDivision.h ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains an optimization for div and rem on architectures that
// execute short instructions significantly faster than longer instructions.
// For example, on Intel Atom 32-bit divides are slow enough that during
// runtime it is profitable to check the value of the operands, and if they are
// positive and less than 256 use an unsigned 8-bit divide.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_BYPASSSLOWDIVISION_H
#define LLVM_TRANSFORMS_UTILS_BYPASSSLOWDIVISION_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/IR/ValueHandle.h"
#include <cstdint>

namespace llvm {

class BasicBlock;
class BranchProbabilityInfo;
class DomTreeUpdater;
class LoopInfo;
class Value;

/// Key identifying a div/rem operation by signedness and operands.
struct DivRemMapKey {
  /// True when the corresponding division or remainder is signed.
  bool SignedOp;
  /// Dividend value of the division or remainder.
  AssertingVH<Value> Dividend;
  /// Divisor value of the division or remainder.
  AssertingVH<Value> Divisor;

  /// Construct an empty key with default-initialized members.
  DivRemMapKey() = default;

  /// Construct a key from signedness and dividend/divisor values.
  /// \param InSignedOp True when the operation is signed.
  /// \param InDividend Dividend operand of the operation.
  /// \param InDivisor Divisor operand of the operation.
  DivRemMapKey(bool InSignedOp, Value *InDividend, Value *InDivisor)
      : SignedOp(InSignedOp), Dividend(InDividend), Divisor(InDivisor) {}
};

/// DenseMapInfo specialization so DivRemMapKey can be a DenseMap key.
template <> struct DenseMapInfo<DivRemMapKey> {
  /// Return true if \p Val1 and \p Val2 have equal signedness and operands.
  /// \param Val1 Left-hand map key.
  /// \param Val2 Right-hand map key.
  /// \return True if \p Val1 and \p Val2 have equal signedness and operands.
  static bool isEqual(const DivRemMapKey &Val1, const DivRemMapKey &Val2) {
    return Val1.SignedOp == Val2.SignedOp && Val1.Dividend == Val2.Dividend &&
           Val1.Divisor == Val2.Divisor;
  }

  /// Return a hash of \p Val's signedness and operand pointers.
  /// \param Val Map key whose members are hashed.
  /// \return Hash combining \p Val's signedness and operand pointers.
  static unsigned getHashValue(const DivRemMapKey &Val) {
    return (unsigned)(reinterpret_cast<uintptr_t>(
                          static_cast<Value *>(Val.Dividend)) ^
                      reinterpret_cast<uintptr_t>(
                          static_cast<Value *>(Val.Divisor))) ^
           (unsigned)Val.SignedOp;
  }
};

/// This optimization identifies DIV instructions in a BB that can be
/// profitably bypassed and carried out with a shorter, faster divide.
///
/// This optimization may add basic blocks immediately after BB; for obvious
/// reasons, you shouldn't pass those blocks to bypassSlowDivision.
/// \param BB Basic block whose divisions may be bypassed.
/// \param BypassWidth Map from operand bit width to profitable bypass width.
/// \param DTU Optional dominator-tree updater for CFG changes.
/// \param LI Optional loop info to keep in sync with CFG changes.
/// \param BPI Optional branch probabilities for inserted bypass branches.
/// \return True if any division or remainder in \p BB was rewritten.
LLVM_ABI bool
bypassSlowDivision(BasicBlock *BB,
                   const DenseMap<unsigned int, unsigned int> &BypassWidth,
                   DomTreeUpdater *DTU = nullptr, LoopInfo *LI = nullptr,
                   BranchProbabilityInfo *BPI = nullptr);

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_BYPASSSLOWDIVISION_H
