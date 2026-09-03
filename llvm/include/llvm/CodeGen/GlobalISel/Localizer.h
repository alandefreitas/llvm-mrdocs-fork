//== llvm/CodeGen/GlobalISel/Localizer.h - Localizer -------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file This file describes the interface of the Localizer pass.
/// This pass moves/duplicates constant-like instructions close to their uses.
/// Its primarily goal is to workaround the deficiencies of the fast register
/// allocator.
/// With GlobalISel constants are all materialized in the entry block of
/// a function. However, the fast allocator cannot rematerialize constants and
/// has a lot more live-ranges to deal with and will most likely end up
/// spilling a lot.
/// By pushing the constants close to their use, we only create small
/// live-ranges.
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALISEL_LOCALIZER_H
#define LLVM_CODEGEN_GLOBALISEL_LOCALIZER_H

#include "llvm/CodeGen/MachineFunctionAnalysisManager.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/IR/Analysis.h"
#include "llvm/IR/PassManager.h"

namespace llvm {
// Forward declarations.
class AnalysisUsage;

/// Legacy pass that localizes constant-like instructions near their uses.
///
/// This pass implements the localization mechanism described at the
/// top of this file. One specificity of the implementation is that
/// it will materialize one and only one instance of a constant per
/// basic block, thus enabling reuse of that constant within that block.
/// Moreover, it only materializes constants in blocks where they
/// are used. PHI uses are considered happening at the end of the
/// related predecessor.
class LLVM_ABI LocalizerLegacy : public MachineFunctionPass {
public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct the legacy GlobalISel localizer pass.
  LocalizerLegacy();

  /// Return the name of this pass.
  ///
  /// \return A string identifying this pass as "Localizer".
  StringRef getPassName() const override { return "Localizer"; }

  /// Return the properties this pass requires of the machine function.
  ///
  /// Localization expects the function to be in SSA form.
  ///
  /// \return Properties requiring the function to be in SSA form.
  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setIsSSA();
  }

  /// Declare required and preserved analyses for this pass.
  ///
  /// \param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// Localize constant-like instructions in \p MF near their uses.
  ///
  /// \param MF Machine function whose constants are localized.
  /// \return True if the machine function was modified.
  bool runOnMachineFunction(MachineFunction &MF) override;
};

/// New PM pass that localizes constant-like instructions near their uses.
class LocalizerPass : public RequiredPassInfoMixin<LocalizerPass> {
public:
  /// Localize constant-like instructions in \p MF near their uses.
  ///
  /// \param MF Machine function whose constants are localized.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved by this pass.
  PreservedAnalyses run(MachineFunction &MF,
                        MachineFunctionAnalysisManager &MFAM);

  /// Return the properties this pass requires of the machine function.
  ///
  /// Localization expects the function to be in SSA form.
  ///
  /// \return Properties requiring the function to be in SSA form.
  MachineFunctionProperties getRequiredProperties() const {
    return MachineFunctionProperties().setIsSSA();
  }
};

} // End namespace llvm.

#endif
