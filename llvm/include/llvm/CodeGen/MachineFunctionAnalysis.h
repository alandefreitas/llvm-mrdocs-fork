//===- llvm/CodeGen/MachineFunctionAnalysis.h -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the MachineFunctionAnalysis class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEFUNCTIONANALYSIS
#define LLVM_CODEGEN_MACHINEFUNCTIONANALYSIS

#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class MachineFunction;
class TargetMachine;

/// This analysis create MachineFunction for given Function.
/// To release the MachineFunction, users should invalidate it explicitly.
class MachineFunctionAnalysis
    : public AnalysisInfoMixin<MachineFunctionAnalysis> {
  friend AnalysisInfoMixin<MachineFunctionAnalysis>;

  LLVM_ABI static AnalysisKey Key;

  const TargetMachine *TM;

public:
  /// Cached analysis result owning a MachineFunction.
  class Result {
    std::unique_ptr<MachineFunction> MF;

  public:
    /// Construct a result that takes ownership of \p MF.
    ///
    /// \param MF MachineFunction to own.
    LLVM_ABI Result(std::unique_ptr<MachineFunction> MF);
    /// Return the owned MachineFunction.
    ///
    /// \return Reference to the owned MachineFunction.
    MachineFunction &getMF() { return *MF; };
    /// Check whether this result should be invalidated.
    ///
    /// Remains preserved unless MachineFunctionAnalysis is invalidated
    /// explicitly.
    ///
    /// \param F Function for which invalidation is queried (unused).
    /// \param PA Set of analyses preserved by the last transformation.
    /// \param Inv Invalidator for other function analyses (unused).
    /// \return True if this result should be discarded.
    LLVM_ABI bool invalidate(Function &F, const PreservedAnalyses &PA,
                             FunctionAnalysisManager::Invalidator &Inv);
  };

  /// Construct an analysis that builds MachineFunctions for \p TM.
  ///
  /// \param TM Target machine used when creating each MachineFunction.
  MachineFunctionAnalysis(const TargetMachine &TM) : TM(&TM) {};
  /// Build and return a MachineFunction for \p F.
  ///
  /// \param F Function to lower into a MachineFunction.
  /// \param FAM Function analysis manager used to obtain MachineModuleInfo.
  /// \return Result owning the newly created MachineFunction.
  LLVM_ABI Result run(Function &F, FunctionAnalysisManager &FAM);
};

/// Pass that explicitly frees the MachineFunction for a function.
class FreeMachineFunctionPass
    : public RequiredPassInfoMixin<FreeMachineFunctionPass> {
public:
  /// Clear MachineFunctionAnalysis for \p F and preserve all analyses.
  ///
  /// \param F Function whose MachineFunction should be released.
  /// \param FAM Function analysis manager holding the cached result.
  /// \return All analyses preserved.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_MachineFunctionAnalysis
