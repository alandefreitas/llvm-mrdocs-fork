//===- GenericConvergenceVerifier.h ---------------------------*- C++ -*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
///
/// A verifier for the static rules of convergence control tokens that works
/// with both LLVM IR and MIR.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_GENERICCONVERGENCEVERIFIER_H
#define LLVM_ADT_GENERICCONVERGENCEVERIFIER_H

#include "llvm/ADT/GenericCycleInfo.h"

namespace llvm {

/// Verifier for static convergence-control token rules over IR or MIR.
///
/// Specializes on an SSA context so the same checks apply to LLVM IR and
/// Machine IR. Call \c initialize, visit each block and instruction, then
/// \c verify against a dominator tree.
template <typename ContextT> class GenericConvergenceVerifier {
public:
  /// Basic-block type from the SSA context.
  using BlockT = typename ContextT::BlockT;
  /// Function type from the SSA context.
  using FunctionT = typename ContextT::FunctionT;
  /// SSA value reference type from the SSA context.
  using ValueRefT = typename ContextT::ValueRefT;
  /// Instruction type from the SSA context.
  using InstructionT = typename ContextT::InstructionT;
  /// Dominator-tree type from the SSA context.
  using DominatorTreeT = typename ContextT::DominatorTreeT;
  /// Cycle-info analysis specialized for this context.
  using CycleInfoT = GenericCycleInfo<ContextT>;

  /// Bind failure reporting and the function under verification.
  /// @param OS Optional stream for diagnostic details.
  /// @param FailureCB Callback invoked when a rule is violated.
  /// @param F Function whose convergence tokens will be checked.
  void initialize(raw_ostream *OS,
                  function_ref<void(const Twine &Message)> FailureCB,
                  const FunctionT &F) {
    clear();
    this->OS = OS;
    this->FailureCB = FailureCB;
    Context = ContextT(&F);
  }

  /// Reset verifier state for a new function.
  void clear();
  /// Prepare to visit instructions in basic block \p BB (resets per-block flags).
  /// @param BB Basic block about to be walked.
  void visit(const BlockT &BB);
  /// Check convergence rules for instruction \p I and record any tokens it uses.
  /// @param I Instruction to examine.
  void visit(const InstructionT &I);
  /// Finish verification against dominator tree \p DT after all visits.
  /// @param DT Dominator tree for the function under verification.
  void verify(const DominatorTreeT &DT);

  /// Return true if any controlled convergence tokens were observed.
  bool sawTokens() const { return ConvergenceKind == ControlledConvergence; }

private:
  raw_ostream *OS;
  std::function<void(const Twine &Message)> FailureCB;
  DominatorTreeT *DT;
  CycleInfoT CI;
  ContextT Context;

  /// Whether the current function has convergencectrl operand bundles.
  enum {
    ControlledConvergence,
    UncontrolledConvergence,
    NoConvergence
  } ConvergenceKind = NoConvergence;

  /// The control token operation performed by a convergence control Intrinsic
  /// in LLVM IR, or by a CONVERGENCECTRL* instruction in MIR
  enum ConvOpKind { CONV_ANCHOR, CONV_ENTRY, CONV_LOOP, CONV_NONE };

  // Cache token uses found so far. Note that we track the unique definitions
  // and not the token values.
  DenseMap<const InstructionT *, const InstructionT *> Tokens;

  bool SeenFirstConvOp = false;

  static bool isInsideConvergentFunction(const InstructionT &I);
  static bool isConvergent(const InstructionT &I);
  static ConvOpKind getConvOp(const InstructionT &I);
  void checkConvergenceTokenProduced(const InstructionT &I);
  const InstructionT *findAndCheckConvergenceTokenUsed(const InstructionT &I);

  void reportFailure(const Twine &Message, ArrayRef<Printable> Values);
};

} // end namespace llvm

#endif // LLVM_ADT_GENERICCONVERGENCEVERIFIER_H
