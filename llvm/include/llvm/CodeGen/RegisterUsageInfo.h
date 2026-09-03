//==- RegisterUsageInfo.h - Register Usage Informartion Storage --*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This pass is required to take advantage of the interprocedural register
/// allocation infrastructure.
///
/// This pass is simple immutable pass which keeps RegMasks (calculated based on
/// actual register allocation) for functions in a module and provides simple
/// API to query this information.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_REGISTERUSAGEINFO_H
#define LLVM_CODEGEN_REGISTERUSAGEINFO_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/PassRegistry.h"
#include <cstdint>
#include <vector>

namespace llvm {

class Function;
class TargetMachine;

/// Stores physical register usage masks for functions in a module.
class PhysicalRegisterUsageInfo {
public:
  /// Set TargetMachine which is used to print analysis.
  ///
  /// \param TM Target machine used when printing register names.
  LLVM_ABI void setTargetMachine(const TargetMachine &TM);

  /// Initialize storage for the functions in module \p M.
  ///
  /// \param M Module whose functions may later receive register usage info.
  /// \return False; this analysis does not modify the module.
  LLVM_ABI bool doInitialization(Module &M);

  /// Optionally print usage info and clear stored RegMasks for module \p M.
  ///
  /// \param M Module whose analysis state is being finalized.
  /// \return False; this analysis does not modify the module.
  LLVM_ABI bool doFinalization(Module &M);

  /// To store RegMask for given Function *.
  ///
  /// \param FP Function whose register usage mask is stored or updated.
  /// \param RegMask Bitmask of clobbered physical registers for \p FP.
  LLVM_ABI void storeUpdateRegUsageInfo(const Function &FP,
                                        ArrayRef<uint32_t> RegMask);

  /// To query stored RegMask for given Function *, it will returns ane empty
  /// array if function is not known.
  ///
  /// \param FP Function whose stored register usage mask is requested.
  /// \return Stored RegMask for \p FP, or an empty array if unknown.
  LLVM_ABI ArrayRef<uint32_t> getRegUsageInfo(const Function &FP);

  /// Print stored register usage information to \p OS.
  ///
  /// \param OS Output stream for the dump.
  /// \param M Optional module providing additional context.
  LLVM_ABI void print(raw_ostream &OS, const Module *M = nullptr) const;

  /// Invalidate this result unless the analysis is preserved.
  ///
  /// \param M Module whose analysis result may be invalidated.
  /// \param PA Set of analyses preserved by the transform.
  /// \param Inv Invalidator for resolving analysis dependencies.
  /// \return True if this result should be discarded.
  LLVM_ABI bool invalidate(Module &M, const PreservedAnalyses &PA,
                           ModuleAnalysisManager::Invalidator &Inv);

private:
  /// A Dense map from Function * to RegMask.
  /// In RegMask 0 means register used (clobbered) by function.
  /// and 1 means content of register will be preserved around function call.
  DenseMap<const Function *, std::vector<uint32_t>> RegMasks;

  const TargetMachine *TM = nullptr;
};

/// Legacy immutable pass wrapping PhysicalRegisterUsageInfo.
class PhysicalRegisterUsageInfoWrapperLegacy : public ImmutablePass {
  std::unique_ptr<PhysicalRegisterUsageInfo> PRUI;

public:
  /// Pass identification, replacement for typeid.
  LLVM_ABI static char ID;
  /// Construct the legacy PhysicalRegisterUsageInfo wrapper pass.
  PhysicalRegisterUsageInfoWrapperLegacy() : ImmutablePass(ID) {}

  /// Return the PhysicalRegisterUsageInfo owned by this pass.
  ///
  /// \return Mutable reference to the owned PhysicalRegisterUsageInfo.
  PhysicalRegisterUsageInfo &getPRUI() { return *PRUI; }
  /// Return the PhysicalRegisterUsageInfo owned by this pass.
  ///
  /// \return Const reference to the owned PhysicalRegisterUsageInfo.
  const PhysicalRegisterUsageInfo &getPRUI() const { return *PRUI; }

  /// Create and initialize PhysicalRegisterUsageInfo for module \p M.
  ///
  /// \param M Module to analyze.
  /// \return False; this analysis does not modify the module.
  bool doInitialization(Module &M) override {
    PRUI.reset(new PhysicalRegisterUsageInfo());
    return PRUI->doInitialization(M);
  }

  /// Finalize and clear PhysicalRegisterUsageInfo for module \p M.
  ///
  /// \param M Module whose analysis state is being finalized.
  /// \return False; this analysis does not modify the module.
  bool doFinalization(Module &M) override { return PRUI->doFinalization(M); }

  /// Print the physical register usage info owned by this pass.
  ///
  /// \param OS Output stream for the dump.
  /// \param M Optional module providing additional context.
  void print(raw_ostream &OS, const Module *M = nullptr) const override {
    PRUI->print(OS, M);
  }
};

/// Analysis that provides PhysicalRegisterUsageInfo for a module.
class PhysicalRegisterUsageAnalysis
    : public AnalysisInfoMixin<PhysicalRegisterUsageAnalysis> {
  friend AnalysisInfoMixin<PhysicalRegisterUsageAnalysis>;
  static AnalysisKey Key;

public:
  /// Provide the result type for this analysis pass.
  using Result = PhysicalRegisterUsageInfo;

  /// Run the analysis over a module and produce PhysicalRegisterUsageInfo.
  ///
  /// \param M Module to analyze.
  /// \param AM Module analysis manager for this pass.
  /// \return Fresh PhysicalRegisterUsageInfo initialized for \p M.
  LLVM_ABI PhysicalRegisterUsageInfo run(Module &M, ModuleAnalysisManager &AM);
};

/// Printer pass for the PhysicalRegisterUsageAnalysis results.
class PhysicalRegisterUsageInfoPrinterPass
    : public RequiredPassInfoMixin<PhysicalRegisterUsageInfoPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes to \p OS.
  ///
  /// \param OS Output stream for the printed register usage info.
  explicit PhysicalRegisterUsageInfoPrinterPass(raw_ostream &OS) : OS(OS) {}
  /// Print PhysicalRegisterUsageAnalysis results for \p M.
  ///
  /// \param M Module whose register usage info is printed.
  /// \param AM Module analysis manager providing PhysicalRegisterUsageAnalysis.
  /// \return All analyses preserved; this pass does not transform \p M.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_CODEGEN_REGISTERUSAGEINFO_H
