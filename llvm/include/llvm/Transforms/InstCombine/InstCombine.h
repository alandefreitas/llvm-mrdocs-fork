//===- InstCombine.h - InstCombine pass -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file provides the primary interface to the instcombine pass. This pass
/// is suitable for use in the new pass manager. For a pass that works with the
/// legacy pass manager, use \c createInstructionCombiningPass().
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_INSTCOMBINE_INSTCOMBINE_H
#define LLVM_TRANSFORMS_INSTCOMBINE_INSTCOMBINE_H

#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/Compiler.h"

#define DEBUG_TYPE "instcombine"
#include "llvm/Transforms/Utils/InstructionWorklist.h"

namespace llvm {

static constexpr unsigned InstCombineDefaultMaxIterations = 1;

/// Options that control how InstCombine iterates and verifies its result.
struct InstCombineOptions {
  /// Whether to verify that a fixpoint has been reached after MaxIterations.
  bool VerifyFixpoint = false;
  /// Maximum number of InstCombine iterations to run.
  unsigned MaxIterations = InstCombineDefaultMaxIterations;

  /// Construct InstCombine options with default field values.
  InstCombineOptions() = default;

  /// Enable or disable fixpoint verification after MaxIterations.
  /// @param Value Whether to verify that a fixpoint was reached.
  /// @return A reference to this options object for chaining.
  InstCombineOptions &setVerifyFixpoint(bool Value) {
    VerifyFixpoint = Value;
    return *this;
  }

  /// Set the maximum number of InstCombine iterations.
  /// @param Value Maximum iterations to run.
  /// @return A reference to this options object for chaining.
  InstCombineOptions &setMaxIterations(unsigned Value) {
    MaxIterations = Value;
    return *this;
  }
};

/// A pass that combines instructions to form fewer, simpler instructions.
///
/// This pass does not modify the CFG, and has a tendency to make instructions
/// dead, so a subsequent DCE pass is useful. For example, it combines:
///    %Y = add int 1, %X
///    %Z = add int 1, %Y
/// into:
///    %Z = add int 2, %X
class InstCombinePass : public OptionalPassInfoMixin<InstCombinePass> {
private:
  InstructionWorklist Worklist;
  InstCombineOptions Options;
  static char ID;

public:
  /// Construct an InstCombine pass with the given options.
  /// @param Opts Options controlling iteration count and fixpoint verification.
  LLVM_ABI explicit InstCombinePass(InstCombineOptions Opts = {});
  /// Print this pass's pipeline representation to \p OS.
  /// @param OS Stream to write the pipeline string to.
  /// @param MapClassName2PassName Maps class names to pass names.
  LLVM_ABI void
  printPipeline(raw_ostream &OS,
                function_ref<StringRef(StringRef)> MapClassName2PassName);

  /// Run instruction combining over the function.
  /// @param F Function to combine instructions in.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// The legacy pass manager's instcombine pass.
///
/// This is a basic whole-function wrapper around the instcombine utility. It
/// will try to combine all instructions in the function.
class LLVM_ABI InstructionCombiningPass : public FunctionPass {
  InstructionWorklist Worklist;

public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct the legacy instruction combining pass.
  explicit InstructionCombiningPass();

  /// Declare required and preserved analyses for this pass.
  /// @param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  /// Run instruction combining over function \p F.
  /// @param F Function whose instructions may be combined.
  /// @return True if the function was modified.
  bool runOnFunction(Function &F) override;
};

/// Create a legacy pass that combines instructions into fewer, simpler forms.
///
/// This pass does not modify the CFG, and has a tendency to make instructions
/// dead, so a subsequent DCE pass is useful. For example, it combines:
///    %Y = add int 1, %X
///    %Z = add int 1, %Y
/// into:
///    %Z = add int 2, %X
///
/// @return An owning pointer to the newly created FunctionPass.
LLVM_ABI FunctionPass *createInstructionCombiningPass();
}

#undef DEBUG_TYPE

#endif
