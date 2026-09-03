//===-- CopyProf.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the instrumentation passes for CopyProf that insert
// callbacks into special member functions, and add store instrumentation.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_COPYPROF_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_COPYPROF_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// Early-stage pass that instruments special member functions to call into the
/// CopyProf runtime.
class CopyProfPass : public RequiredPassInfoMixin<CopyProfPass> {
public:
  /// Construct a CopyProf function pass.
  CopyProfPass() = default;
  /// Run CopyProf instrumentation over the function.
  /// @param F Function to instrument.
  /// @param FAM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);

  /// Return true; this pass cannot be skipped.
  /// @return True.
  static bool isRequired() { return true; }
};

/// Module-level pass that inserts the CopyProf runtime initialization
/// constructor and hooks it into @llvm.global_ctors.
class ModuleCopyProfPass : public RequiredPassInfoMixin<ModuleCopyProfPass> {
public:
  /// Construct a CopyProf module pass.
  ModuleCopyProfPass() = default;
  /// Run CopyProf module instrumentation over \p M.
  /// @param M Module to instrument.
  /// @param AM Module analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

  /// Return true; this pass cannot be skipped.
  /// @return True.
  static bool isRequired() { return true; }
};

/// Late-stage pass that instruments store instructions to detect whether an
/// object copy has been modified before it is destructed.
class CopyProfStoresPass : public RequiredPassInfoMixin<CopyProfStoresPass> {
public:
  /// Construct a CopyProf store-instrumentation pass.
  CopyProfStoresPass() = default;
  /// Run CopyProf store instrumentation over the function.
  /// @param F Function to instrument.
  /// @param FAM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);

  /// Return true; this pass cannot be skipped.
  /// @return True.
  static bool isRequired() { return true; }
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_INSTRUMENTATION_COPYPROF_H
