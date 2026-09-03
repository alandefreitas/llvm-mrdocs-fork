//===- StackSafetyAnalysis.h - Stack memory safety analysis -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Stack Safety Analysis detects allocas and arguments with safe access.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_STACKSAFETYANALYSIS_H
#define LLVM_ANALYSIS_STACKSAFETYANALYSIS_H

#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

namespace llvm {

class AllocaInst;
class ScalarEvolution;

/// Interface to access stack safety analysis results for single function.
class StackSafetyInfo {
public:
  /// Opaque per-function stack safety analysis results.
  struct InfoTy;

private:
  Function *F = nullptr;
  std::function<ScalarEvolution &()> GetSE;
  mutable std::unique_ptr<InfoTy> Info;

public:
  /// Construct an empty StackSafetyInfo.
  LLVM_ABI StackSafetyInfo();
  /// Construct StackSafetyInfo for \p F using \p GetSE for ScalarEvolution.
  /// @param F Function to analyze.
  /// @param GetSE Callback that returns ScalarEvolution for \p F.
  LLVM_ABI StackSafetyInfo(Function *F,
                           std::function<ScalarEvolution &()> GetSE);
  /// Move-construct a StackSafetyInfo.
  /// @param Arg StackSafetyInfo to move from.
  LLVM_ABI StackSafetyInfo(StackSafetyInfo &&Arg);
  /// Move-assign a StackSafetyInfo.
  /// @param RHS StackSafetyInfo to move from.
  /// @return Reference to this object.
  LLVM_ABI StackSafetyInfo &operator=(StackSafetyInfo &&RHS);
  /// Destroy this StackSafetyInfo.
  LLVM_ABI ~StackSafetyInfo();

  /// Return the computed per-function stack safety results.
  /// @return The opaque per-function stack safety analysis results.
  LLVM_ABI const InfoTy &getInfo() const;

  // TODO: Add useful for client methods.
  /// Print the stack safety results to \p O.
  /// @param O Output stream.
  LLVM_ABI void print(raw_ostream &O) const;

  /// Collect pointer-parameter access ranges for a FunctionSummary.
  ///
  /// Function collects access information of all pointer parameters.
  /// Information includes a range of direct access of parameters by the
  /// functions and all call sites accepting the parameter.
  /// StackSafety assumes that missing parameter information means possibility
  /// of access to the parameter with any offset, so we can correctly link
  /// code without StackSafety information, e.g. non-ThinLTO.
  /// @param Index Module summary index used when recording parameter accesses.
  /// @return Parameter access ranges for the function's pointer parameters.
  LLVM_ABI std::vector<FunctionSummary::ParamAccess>
  getParamAccesses(ModuleSummaryIndex &Index) const;
};

/// Interprocedural stack safety analysis results for a module.
class StackSafetyGlobalInfo {
public:
  /// Opaque module-wide stack safety analysis results.
  struct InfoTy;

private:
  Module *M = nullptr;
  std::function<const StackSafetyInfo &(Function &F)> GetSSI;
  const ModuleSummaryIndex *Index = nullptr;
  mutable std::unique_ptr<InfoTy> Info;
  const InfoTy &getInfo() const;

public:
  /// Construct an empty StackSafetyGlobalInfo.
  LLVM_ABI StackSafetyGlobalInfo();
  /// Construct StackSafetyGlobalInfo for \p M.
  /// @param M Module to analyze.
  /// @param GetSSI Callback that returns StackSafetyInfo for a function.
  /// @param Index Optional module summary index for interprocedural info.
  LLVM_ABI StackSafetyGlobalInfo(
      Module *M, std::function<const StackSafetyInfo &(Function &F)> GetSSI,
      const ModuleSummaryIndex *Index);
  /// Move-construct a StackSafetyGlobalInfo.
  /// @param Arg StackSafetyGlobalInfo to move from.
  LLVM_ABI StackSafetyGlobalInfo(StackSafetyGlobalInfo &&Arg);
  /// Move-assign a StackSafetyGlobalInfo.
  /// @param RHS StackSafetyGlobalInfo to move from.
  /// @return Reference to this object.
  LLVM_ABI StackSafetyGlobalInfo &operator=(StackSafetyGlobalInfo &&RHS);
  /// Destroy this StackSafetyGlobalInfo.
  LLVM_ABI ~StackSafetyGlobalInfo();

  /// Return true if all accesses to \p AI are in-range and during its lifetime.
  /// @param AI Alloca to query.
  /// @return True if all accesses to \p AI are in-range and during its lifetime.
  LLVM_ABI bool isSafe(const AllocaInst &AI) const;

  /// Return true if \p I only accesses live in-bounds stack or non-stack memory.
  ///
  /// Returns true if the instruction can be proven to do only two types of
  /// memory accesses:
  ///  (1) live stack locations in-bounds or
  ///  (2) non-stack locations.
  /// @param I Instruction to query.
  /// @return True if \p I only accesses live in-bounds stack or non-stack memory.
  LLVM_ABI bool stackAccessIsSafe(const Instruction &I) const;
  /// Print the global stack safety results to \p O.
  /// @param O Output stream.
  LLVM_ABI void print(raw_ostream &O) const;
  /// Dump the global stack safety results to stderr.
  LLVM_ABI void dump() const;
};

/// StackSafetyInfo wrapper for the new pass manager.
class StackSafetyAnalysis : public AnalysisInfoMixin<StackSafetyAnalysis> {
  friend AnalysisInfoMixin<StackSafetyAnalysis>;
  static AnalysisKey Key;

public:
  /// Provide the result typedef for this analysis pass.
  using Result = StackSafetyInfo;
  /// Run the analysis over \p F and produce a StackSafetyInfo.
  /// @param F Function to analyze.
  /// @param AM Function analysis manager providing dependencies.
  /// @return StackSafetyInfo for \p F.
  LLVM_ABI StackSafetyInfo run(Function &F, FunctionAnalysisManager &AM);
};

/// Printer pass for the \c StackSafetyAnalysis results.
class StackSafetyPrinterPass
    : public RequiredPassInfoMixin<StackSafetyPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes to \p OS.
  /// @param OS Output stream for the printed results.
  explicit StackSafetyPrinterPass(raw_ostream &OS) : OS(OS) {}
  /// Print StackSafetyInfo for \p F and return all analyses preserved.
  /// @param F Function whose StackSafetyInfo is printed.
  /// @param AM Function analysis manager providing StackSafetyInfo.
  /// @return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// StackSafetyInfo wrapper for the legacy pass manager
class LLVM_ABI StackSafetyInfoWrapperPass : public FunctionPass {
  StackSafetyInfo SSI;

public:
  /// Pass identification, replacement for typeid.
  static char ID;
  /// Construct the legacy StackSafetyInfo wrapper pass.
  StackSafetyInfoWrapperPass();

  /// Return the StackSafetyInfo computed by this pass.
  /// @return The StackSafetyInfo computed by this pass.
  const StackSafetyInfo &getResult() const { return SSI; }

  /// Print the StackSafetyInfo computed by this pass.
  /// @param O Output stream.
  /// @param M Optional module (unused).
  void print(raw_ostream &O, const Module *M) const override;
  /// Declare required and preserved analyses for this pass.
  /// @param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// Compute StackSafetyInfo for \p F.
  /// @param F Function to analyze.
  /// @return False; this analysis does not modify the function.
  bool runOnFunction(Function &F) override;
};

/// This pass performs the global (interprocedural) stack safety analysis (new
/// pass manager).
class StackSafetyGlobalAnalysis
    : public AnalysisInfoMixin<StackSafetyGlobalAnalysis> {
  friend AnalysisInfoMixin<StackSafetyGlobalAnalysis>;
  static AnalysisKey Key;

public:
  /// Provide the result typedef for this analysis pass.
  using Result = StackSafetyGlobalInfo;
  /// Run the analysis over \p M and produce a StackSafetyGlobalInfo.
  /// @param M Module to analyze.
  /// @param AM Module analysis manager providing dependencies.
  /// @return StackSafetyGlobalInfo for \p M.
  LLVM_ABI Result run(Module &M, ModuleAnalysisManager &AM);
};

/// Printer pass for the \c StackSafetyGlobalAnalysis results.
class StackSafetyGlobalPrinterPass
    : public RequiredPassInfoMixin<StackSafetyGlobalPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes to \p OS.
  /// @param OS Output stream for the printed results.
  explicit StackSafetyGlobalPrinterPass(raw_ostream &OS) : OS(OS) {}
  /// Print StackSafetyGlobalInfo for \p M and return all analyses preserved.
  /// @param M Module whose StackSafetyGlobalInfo is printed.
  /// @param AM Module analysis manager providing StackSafetyGlobalInfo.
  /// @return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

/// This pass performs the global (interprocedural) stack safety analysis
/// (legacy pass manager).
class LLVM_ABI StackSafetyGlobalInfoWrapperPass : public ModulePass {
  StackSafetyGlobalInfo SSGI;

public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct the legacy StackSafetyGlobalInfo wrapper pass.
  StackSafetyGlobalInfoWrapperPass();
  /// Destroy this StackSafetyGlobalInfoWrapperPass.
  ~StackSafetyGlobalInfoWrapperPass() override;

  /// Return the StackSafetyGlobalInfo computed by this pass.
  /// @return The StackSafetyGlobalInfo computed by this pass.
  const StackSafetyGlobalInfo &getResult() const { return SSGI; }

  /// Print the StackSafetyGlobalInfo computed by this pass.
  /// @param O Output stream.
  /// @param M Optional module (unused).
  void print(raw_ostream &O, const Module *M) const override;
  /// Declare required and preserved analyses for this pass.
  /// @param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// Compute StackSafetyGlobalInfo for \p M.
  /// @param M Module to analyze.
  /// @return False; this analysis does not modify the module.
  bool runOnModule(Module &M) override;
};

/// Return true if \p M needs a parameter access summary for stack safety.
/// @param M Module to query.
/// @return True if \p M needs a parameter access summary for stack safety.
LLVM_ABI bool needsParamAccessSummary(const Module &M);

/// Generate parameter access summary information in \p Index.
/// @param Index Module summary index to update.
LLVM_ABI void generateParamAccessSummary(ModuleSummaryIndex &Index);

} // end namespace llvm

#endif // LLVM_ANALYSIS_STACKSAFETYANALYSIS_H
