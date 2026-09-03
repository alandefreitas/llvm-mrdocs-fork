//===- StackProtector.h - Stack Protector Insertion -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass inserts stack protectors into functions which need them. A variable
// with a random value in it is stored onto the stack before the local variables
// are allocated. Upon exiting the block, the stored value is checked. If it's
// changed, then there was some sort of violation and the program aborts.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_STACKPROTECTOR_H
#define LLVM_CODEGEN_STACKPROTECTOR_H

#include "llvm/Analysis/DomTreeUpdater.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/TargetParser/Triple.h"

namespace llvm {

class BasicBlock;
class Function;
class Module;
class TargetLoweringBase;
class TargetMachine;

/// Per-function layout information used when deciding and applying stack
/// protectors.
class SSPLayoutInfo {
  friend class StackProtectorPass;
  friend class SSPLayoutAnalysis;
  friend class StackProtector;
  static constexpr unsigned DefaultSSPBufferSize = 8;

  /// A mapping of AllocaInsts to their required SSP layout.
  using SSPLayoutMap =
      DenseMap<const AllocaInst *, MachineFrameInfo::SSPLayoutKind>;

  /// Layout - Mapping of allocations to the required SSPLayoutKind.
  /// StackProtector analysis will update this map when determining if an
  /// AllocaInst triggers a stack protector.
  SSPLayoutMap Layout;

  /// The minimum size of buffers that will receive stack smashing
  /// protection when -fstack-protection is used.
  unsigned SSPBufferSize = DefaultSSPBufferSize;

  bool RequireStackProtector = false;

  // A prologue is generated.
  bool HasPrologue = false;

  // IR checking code is generated.
  bool HasIRCheck = false;

public:
  /// Return true if StackProtector should be handled by SelectionDAG.
  ///
  /// \param BB Basic block used to decide whether an SD check is needed.
  /// \return True if SelectionDAG should emit the stack-protector check.
  LLVM_ABI bool shouldEmitSDCheck(const BasicBlock &BB) const;

  /// Copy SSP layout decisions for allocas into machine frame info.
  ///
  /// \param MFI Machine frame info to update with SSP layout kinds.
  LLVM_ABI void copyToMachineFrameInfo(MachineFrameInfo &MFI) const;
};

/// Analysis that computes stack-protector layout information for a function.
class SSPLayoutAnalysis : public AnalysisInfoMixin<SSPLayoutAnalysis> {
  friend AnalysisInfoMixin<SSPLayoutAnalysis>;
  using SSPLayoutMap = SSPLayoutInfo::SSPLayoutMap;

  static AnalysisKey Key;

public:
  /// Result type produced by this analysis.
  using Result = SSPLayoutInfo;

  /// Compute stack-protector layout information for \p F.
  ///
  /// \param F Function to analyze.
  /// \param FAM Function analysis manager providing required analyses.
  /// \return Layout information describing whether and how SSP applies.
  LLVM_ABI Result run(Function &F, FunctionAnalysisManager &FAM);

  /// Check whether or not \p F needs a stack protector based upon the stack
  /// protector level.
  ///
  /// \param F Function to inspect for stack-protector requirements.
  /// \param Layout Optional map filled with per-alloca SSP layout kinds.
  /// \return True if \p F requires a stack protector.
  LLVM_ABI static bool requiresStackProtector(Function *F,
                                              SSPLayoutMap *Layout = nullptr);
};

/// New PM pass that inserts stack protectors into functions that need them.
class StackProtectorPass : public RequiredPassInfoMixin<StackProtectorPass> {
  const TargetMachine *TM;

public:
  /// Construct a StackProtector pass for target machine \p TM.
  ///
  /// \param TM Target machine used when inserting stack protectors.
  explicit StackProtectorPass(const TargetMachine &TM) : TM(&TM) {}
  /// Insert stack protectors into \p F when required.
  ///
  /// \param F Function to transform.
  /// \param FAM Function analysis manager providing required analyses.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
};

/// Legacy FunctionPass that inserts stack protectors into functions that need
/// them.
class LLVM_ABI StackProtector : public FunctionPass {
private:
  /// A mapping of AllocaInsts to their required SSP layout.
  using SSPLayoutMap = SSPLayoutInfo::SSPLayoutMap;

  const TargetMachine *TM = nullptr;

  Function *F = nullptr;
  Module *M = nullptr;

  std::optional<DomTreeUpdater> DTU;

  SSPLayoutInfo LayoutInfo;

public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct the legacy StackProtector pass.
  StackProtector();

  /// Return the computed stack-protector layout information.
  ///
  /// \return Mutable reference to this pass's SSP layout info.
  SSPLayoutInfo &getLayoutInfo() { return LayoutInfo; }

  /// Declare required and preserved analyses for this pass.
  ///
  /// \param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// Return true if StackProtector should be handled by SelectionDAG.
  ///
  /// \param BB Basic block used to decide whether an SD check is needed.
  /// \return True if SelectionDAG should emit the stack-protector check.
  bool shouldEmitSDCheck(const BasicBlock &BB) const {
    return LayoutInfo.shouldEmitSDCheck(BB);
  }

  /// Insert stack protectors into \p Fn when required.
  ///
  /// \param Fn Function to transform.
  /// \return True if the function was modified.
  bool runOnFunction(Function &Fn) override;

  /// Copy SSP layout decisions for allocas into machine frame info.
  ///
  /// \param MFI Machine frame info to update with SSP layout kinds.
  void copyToMachineFrameInfo(MachineFrameInfo &MFI) const {
    LayoutInfo.copyToMachineFrameInfo(MFI);
  }

  /// Check whether or not \p F needs a stack protector based upon the stack
  /// protector level.
  ///
  /// \param F Function to inspect for stack-protector requirements.
  /// \param Layout Optional map filled with per-alloca SSP layout kinds.
  /// \return True if \p F requires a stack protector.
  static bool requiresStackProtector(Function *F,
                                     SSPLayoutMap *Layout = nullptr) {
    return SSPLayoutAnalysis::requiresStackProtector(F, Layout);
  }
};

} // end namespace llvm

#endif // LLVM_CODEGEN_STACKPROTECTOR_H
