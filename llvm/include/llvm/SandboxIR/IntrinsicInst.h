//===- IntrinsicInst.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SANDBOXIR_INTRINSICINST_H
#define LLVM_SANDBOXIR_INTRINSICINST_H

#include "llvm/IR/IntrinsicInst.h"
#include "llvm/SandboxIR/Instruction.h"

namespace llvm::sandboxir {

/// SandboxIR wrapper around an LLVM IntrinsicInst.
class IntrinsicInst : public CallInst {
  IntrinsicInst(llvm::IntrinsicInst *I, Context &Ctx) : CallInst(I, Ctx) {}

public:
  /// Return the intrinsic ID of this intrinsic.
  /// @return The Intrinsic::ID for this intrinsic.
  Intrinsic::ID getIntrinsicID() const {
    return cast<llvm::IntrinsicInst>(Val)->getIntrinsicID();
  }
  /// Return true if this intrinsic is an associative operation.
  /// @return True if this intrinsic is associative.
  bool isAssociative() const {
    return cast<llvm::IntrinsicInst>(Val)->isAssociative();
  }
  /// Return true if swapping the first two arguments produces the same result.
  /// @return True if swapping the first two arguments produces the same result.
  bool isCommutative() const {
    return cast<llvm::IntrinsicInst>(Val)->isCommutative();
  }
  /// Return true if this intrinsic is an annotation.
  /// @return True if this intrinsic is an annotation.
  bool isAssumeLikeIntrinsic() const {
    return cast<llvm::IntrinsicInst>(Val)->isAssumeLikeIntrinsic();
  }
  /// Return true if the intrinsic might lower into a regular function call.
  /// \param IID Intrinsic identifier to test.
  /// @return True if the intrinsic might lower into a regular function call.
  static bool mayLowerToFunctionCall(Intrinsic::ID IID) {
    return llvm::IntrinsicInst::mayLowerToFunctionCall(IID);
  }
  /// For isa/dyn_cast.
  /// \param V Value to test for IntrinsicInst.
  /// @return True if \p V is an IntrinsicInst.
  static bool classof(const Value *V) {
    auto *LLVMV = V->Val;
    return isa<llvm::IntrinsicInst>(LLVMV);
  }
};

} // namespace llvm::sandboxir

#endif // LLVM_SANDBOXIR_INTRINSICINST_H
