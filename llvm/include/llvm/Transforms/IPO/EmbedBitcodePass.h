//===-- EmbedBitcodePass.h - Embeds bitcode into global ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file provides a pass which clones the current module and runs the
/// provided pass pipeline on the clone. The optimized module is stored into a
/// global variable in the `.llvm.lto` section. Primarily, this pass is used
/// to support the FatLTO pipeline, but could be used to generate a bitcode
/// section for any arbitrary pass pipeline without changing the current module.
///
//===----------------------------------------------------------------------===//
//
#ifndef LLVM_TRANSFORMS_IPO_EMBEDBITCODEPASS_H
#define LLVM_TRANSFORMS_IPO_EMBEDBITCODEPASS_H

#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class Module;
class Pass;

/// Options controlling how bitcode is embedded into a module.
struct EmbedBitcodeOptions {
  /// Construct embed-bitcode options with ThinLTO and summary emission disabled.
  EmbedBitcodeOptions() : EmbedBitcodeOptions(false, false) {}

  /// Construct embed-bitcode options with the given ThinLTO and summary flags.
  ///
  /// \param IsThinLTO Whether embedded bitcode should be prepared for ThinLTO.
  /// \param EmitLTOSummary Whether an LTO module summary should be emitted.
  EmbedBitcodeOptions(bool IsThinLTO, bool EmitLTOSummary)
      : IsThinLTO(IsThinLTO), EmitLTOSummary(EmitLTOSummary) {}

  /// Whether embedded bitcode should be prepared for ThinLTO.
  bool IsThinLTO;

  /// Whether an LTO module summary should be emitted with the bitcode.
  bool EmitLTOSummary;
};

/// Pass embeds a copy of the module optimized with the provided pass pipeline
/// into a global variable.
class EmbedBitcodePass : public RequiredPassInfoMixin<EmbedBitcodePass> {
  bool IsThinLTO;
  bool EmitLTOSummary;

public:
  /// Construct an embed-bitcode pass from the given options.
  ///
  /// \param Opts Options controlling ThinLTO preparation and summary emission.
  EmbedBitcodePass(EmbedBitcodeOptions Opts)
      : EmbedBitcodePass(Opts.IsThinLTO, Opts.EmitLTOSummary) {}

  /// Construct an embed-bitcode pass with the given ThinLTO and summary flags.
  ///
  /// \param IsThinLTO Whether embedded bitcode should be prepared for ThinLTO.
  /// \param EmitLTOSummary Whether an LTO module summary should be emitted.
  EmbedBitcodePass(bool IsThinLTO, bool EmitLTOSummary)
      : IsThinLTO(IsThinLTO), EmitLTOSummary(EmitLTOSummary) {}

  /// Embed optimized bitcode for module \p M into a global variable.
  ///
  /// \param M Module whose optimized bitcode is embedded.
  /// \param AM Module analysis manager providing analyses for the pass.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // end namespace llvm.

#endif
