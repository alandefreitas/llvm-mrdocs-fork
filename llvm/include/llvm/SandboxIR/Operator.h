//===- Operator.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SANDBOXIR_OPERATOR_H
#define LLVM_SANDBOXIR_OPERATOR_H

#include "llvm/IR/Operator.h"
#include "llvm/SandboxIR/Instruction.h"
#include "llvm/SandboxIR/User.h"

namespace llvm::sandboxir {

/// Utility class that abstracts common functionality of Instructions and
/// ConstantExprs.
class Operator : public User {
public:
  // The Operator class is intended to be used as a utility, and is never itself
  // instantiated.
  /// Deleted default constructor; Operator is never instantiated.
  Operator() = delete;
  /// Deleted; Operator objects are never allocated.
  /// \param s Unused allocation size.
  void *operator new(size_t s) = delete;

  /// For isa/dyn_cast.
  /// \param From An instruction (always an Operator).
  /// @return True (every Instruction is an Operator).
  static bool classof(const Instruction *From) { return true; }
  /// For isa/dyn_cast.
  /// \param From A constant expression (always an Operator).
  /// @return True (every ConstantExpr is an Operator).
  static bool classof(const ConstantExpr *From) { return true; }
  /// For isa/dyn_cast.
  /// \param From The value to test.
  /// @return True if \p From is an Instruction or ConstantExpr.
  static bool classof(const Value *From) {
    return llvm::Operator::classof(From->Val);
  }
  /// Return true if this operator has flags which may cause this operator to
  /// evaluate to poison despite having non-poison inputs.
  /// @return True if this operator has poison-generating flags.
  bool hasPoisonGeneratingFlags() const {
    return cast<llvm::Operator>(Val)->hasPoisonGeneratingFlags();
  }
};

/// Utility class for integer operators that may exhibit overflow.
class OverflowingBinaryOperator : public Operator {
public:
  /// Test whether this operation is known to never undergo unsigned overflow.
  /// @return True if the operation has the nuw property.
  bool hasNoUnsignedWrap() const {
    return cast<llvm::OverflowingBinaryOperator>(Val)->hasNoUnsignedWrap();
  }
  /// Test whether this operation is known to never undergo signed overflow.
  /// @return True if the operation has the nsw property.
  bool hasNoSignedWrap() const {
    return cast<llvm::OverflowingBinaryOperator>(Val)->hasNoSignedWrap();
  }
  /// Returns the no-wrap kind of the operation.
  /// @return A bitmask of NoUnsignedWrap and/or NoSignedWrap flags.
  unsigned getNoWrapKind() const {
    return cast<llvm::OverflowingBinaryOperator>(Val)->getNoWrapKind();
  }
  /// For isa/dyn_cast.
  /// \param From The instruction to test.
  /// @return True if \p From is an overflowing binary operator.
  static bool classof(const Instruction *From) {
    return llvm::OverflowingBinaryOperator::classof(
        cast<llvm::Instruction>(From->Val));
  }
  /// For isa/dyn_cast.
  /// \param From The constant expression to test.
  /// @return True if \p From is an overflowing binary operator.
  static bool classof(const ConstantExpr *From) {
    return llvm::OverflowingBinaryOperator::classof(
        cast<llvm::ConstantExpr>(From->Val));
  }
  /// For isa/dyn_cast.
  /// \param From The value to test.
  /// @return True if \p From is an OverflowingBinaryOperator.
  static bool classof(const Value *From) {
    return llvm::OverflowingBinaryOperator::classof(From->Val);
  }
};

/// Utility class for floating-point operations with relaxed accuracy info.
class FPMathOperator : public Operator {
public:
  /// Test if this operation allows all non-strict floating-point transforms.
  /// @return True if all fast-math flags are set.
  bool isFast() const { return cast<llvm::FPMathOperator>(Val)->isFast(); }
  /// Test if this operation may be simplified with reassociative transforms.
  /// @return True if the allow-reassociation flag is set.
  bool hasAllowReassoc() const {
    return cast<llvm::FPMathOperator>(Val)->hasAllowReassoc();
  }
  /// Test if this operation's arguments and results are assumed not-NaN.
  /// @return True if the no-NaNs flag is set.
  bool hasNoNaNs() const {
    return cast<llvm::FPMathOperator>(Val)->hasNoNaNs();
  }
  /// Test if this operation's arguments and results are assumed not-infinite.
  /// @return True if the no-infs flag is set.
  bool hasNoInfs() const {
    return cast<llvm::FPMathOperator>(Val)->hasNoInfs();
  }
  /// Test if this operation can ignore the sign of zero.
  /// @return True if the no-signed-zeros flag is set.
  bool hasNoSignedZeros() const {
    return cast<llvm::FPMathOperator>(Val)->hasNoSignedZeros();
  }
  /// Test if this operation can use reciprocal multiply instead of division.
  /// @return True if the allow-reciprocal flag is set.
  bool hasAllowReciprocal() const {
    return cast<llvm::FPMathOperator>(Val)->hasAllowReciprocal();
  }
  /// Test if this operation can be floating-point contracted (FMA).
  /// @return True if the allow-contract flag is set.
  bool hasAllowContract() const {
    return cast<llvm::FPMathOperator>(Val)->hasAllowContract();
  }
  /// Test if this operation allows approximations of math library functions or
  /// intrinsics.
  /// @return True if the approximate-functions flag is set.
  bool hasApproxFunc() const {
    return cast<llvm::FPMathOperator>(Val)->hasApproxFunc();
  }
  /// Convenience function for getting all the fast-math flags.
  /// @return The fast-math flags attached to this operator.
  FastMathFlags getFastMathFlags() const {
    return cast<llvm::FPMathOperator>(Val)->getFastMathFlags();
  }
  /// Get the maximum error permitted by this operation in ULPs.
  ///
  /// An accuracy of 0.0 means that the operation should be performed with the
  /// default precision.
  /// @return The maximum error in ULPs, or 0.0 for default precision.
  float getFPAccuracy() const {
    return cast<llvm::FPMathOperator>(Val)->getFPAccuracy();
  }
  /// Returns true if \p Ty is a supported floating-point type for FPMathOperator.
  /// \param Ty The type to test for FPMathOperator support.
  /// @return True if \p Ty is a supported floating-point type.
  static bool isSupportedFloatingPointType(Type *Ty) {
    return llvm::FPMathOperator::isSupportedFloatingPointType(Ty->LLVMTy);
  }
  /// For isa/dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is an FPMathOperator.
  static bool classof(const Value *V) {
    return llvm::FPMathOperator::classof(V->Val);
  }
};

} // namespace llvm::sandboxir

#endif // LLVM_SANDBOXIR_OPERATOR_H
