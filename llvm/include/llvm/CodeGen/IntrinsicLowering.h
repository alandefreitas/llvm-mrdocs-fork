//===-- IntrinsicLowering.h - Intrinsic Function Lowering -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the IntrinsicLowering interface.  This interface allows
// addition of domain-specific or front-end specific intrinsics to LLVM without
// having to modify all of the C backend or interpreter.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_INTRINSICLOWERING_H
#define LLVM_CODEGEN_INTRINSICLOWERING_H

#include "llvm/Support/Compiler.h"

namespace llvm {
class CallInst;
class DataLayout;

/// Lowers intrinsic function calls into non-intrinsic IR when possible.
class IntrinsicLowering {
  const DataLayout &DL;

  bool Warned = false;

public:
  /// Construct an intrinsic lowerer using data layout \p DL.
  ///
  /// \param DL Data layout used when lowering intrinsic calls.
  explicit IntrinsicLowering(const DataLayout &DL) : DL(DL) {}

  /// Replace a call to the specified intrinsic function.
  ///
  /// If an intrinsic function must be implemented by the code generator
  /// (such as va_start), this function should print a message and abort.
  ///
  /// Otherwise, if an intrinsic function call can be lowered, the code to
  /// implement it (often a call to a non-intrinsic function) is inserted
  /// _after_ the call instruction and the call is deleted. The caller must
  /// be capable of handling this kind of change.
  ///
  /// \param CI Call instruction invoking the intrinsic to lower.
  LLVM_ABI void LowerIntrinsicCall(CallInst *CI);

  /// Try to replace a call instruction with a call to a bswap intrinsic.
  ///
  /// \param CI Call instruction that may be a simple integer bswap.
  /// \return True if the call was replaced with a bswap intrinsic; false if
  /// the call is not a simple integer bswap.
  LLVM_ABI static bool LowerToByteSwap(CallInst *CI);
};
}

#endif
