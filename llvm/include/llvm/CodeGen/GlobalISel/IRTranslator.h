//===- llvm/CodeGen/GlobalISel/IRTranslator.h - IRTranslator ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file declares the IRTranslator pass.
/// This pass is responsible for translating LLVM IR into MachineInstr.
/// It uses target hooks to lower the ABI but aside from that, the pass
/// generated code is generic. This is the default translator used for
/// GlobalISel.
///
/// \todo Replace the comments with actual doxygen comments.
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALISEL_IRTRANSLATOR_H
#define LLVM_CODEGEN_GLOBALISEL_IRTRANSLATOR_H

#include "llvm/CodeGen/MachineFunctionAnalysisManager.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/IR/Analysis.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/CodeGen.h"
#include <memory>

namespace llvm {

/// Shared implementation for legacy and new-PM IR translation passes.
class IRTranslatorImpl;

/// Legacy pass that translates LLVM IR into MachineInstr for GlobalISel.
///
/// Technically the pass should run on an hypothetical MachineModule,
/// since it should translate Global into some sort of MachineGlobal.
/// The MachineGlobal should ultimately just be a transfer of ownership of
/// the interesting bits that are relevant to represent a global value.
/// That being said, we could investigate what would it cost to just duplicate
/// the information from the LLVM IR.
/// The idea is that ultimately we would be able to free up the memory used
/// by the LLVM IR as soon as the translation is over.
class LLVM_ABI IRTranslatorLegacy : public MachineFunctionPass {
public:
  /// Pass identification, replacement for typeid.
  static char ID;
  /// Construct the legacy GlobalISel IR translator pass.
  ///
  /// \param OptLevel Optimization level controlling optional analyses.
  IRTranslatorLegacy(CodeGenOptLevel OptLevel = CodeGenOptLevel::None);
  /// Destroy the legacy IR translator pass.
  ~IRTranslatorLegacy() override;

  /// Return the name of this pass.
  ///
  /// \return The name of this pass.
  StringRef getPassName() const override { return "IRTranslator"; }

  /// Declare the analyses required and preserved by this pass.
  ///
  /// \param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// Translate LLVM IR in \p MF into MachineInstr.
  ///
  /// \param MF Machine function whose IR is translated.
  /// \return True if the machine function was modified.
  bool runOnMachineFunction(MachineFunction &MF) override;

private:
  CodeGenOptLevel OptLevel;
  std::unique_ptr<IRTranslatorImpl> Impl;
};

/// New PM pass that translates LLVM IR into MachineInstr for GlobalISel.
class IRTranslatorPass : public RequiredPassInfoMixin<IRTranslatorPass> {
  std::unique_ptr<IRTranslatorImpl> Impl;

public:
  /// Construct the new-PM GlobalISel IR translator pass.
  ///
  /// \param OptLevel Optimization level controlling optional analyses.
  IRTranslatorPass(CodeGenOptLevel OptLevel);
  /// Destroy the new-PM IR translator pass.
  ~IRTranslatorPass();
  /// Move-construct an IR translator pass.
  ///
  /// \param Other Pass to move from.
  IRTranslatorPass(IRTranslatorPass &&Other);

  /// Translate LLVM IR in \p MF into MachineInstr.
  ///
  /// \param MF Machine function whose IR is translated.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved by this pass.
  PreservedAnalyses run(MachineFunction &MF,
                        MachineFunctionAnalysisManager &MFAM);
};

} // end namespace llvm

#endif // LLVM_CODEGEN_GLOBALISEL_IRTRANSLATOR_H
