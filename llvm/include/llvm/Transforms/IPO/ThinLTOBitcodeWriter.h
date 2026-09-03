//===- ThinLTOBitcodeWriter.h - Bitcode writing pass for ThinLTO ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass prepares a module containing type metadata for ThinLTO by splitting
// it into regular and thin LTO parts if possible, and writing both parts to
// a multi-module bitcode file. Modules that do not contain type metadata are
// written unmodified as a single module.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_THINLTOBITCODEWRITER_H
#define LLVM_TRANSFORMS_IPO_THINLTOBITCODEWRITER_H

#include "llvm/Support/Compiler.h"
#include <llvm/IR/PassManager.h>

namespace llvm {
class Module;
class raw_ostream;

/// Pass that prepares a module for ThinLTO and writes bitcode.
///
/// Modules containing type metadata are split into regular and thin LTO parts
/// when possible and written to a multi-module bitcode file. Modules without
/// type metadata are written unmodified as a single module.
class ThinLTOBitcodeWriterPass
    : public RequiredPassInfoMixin<ThinLTOBitcodeWriterPass> {
  raw_ostream &OS;
  raw_ostream *ThinLinkOS;
  const bool ShouldPreserveUseListOrder;

public:
  /// Construct a ThinLTO bitcode writer that emits to \p OS.
  ///
  /// Also writes a thin link file to \p ThinLinkOS when it is not nullptr.
  ///
  /// \param OS Stream that receives the ThinLTO bitcode output.
  /// \param ThinLinkOS Optional stream for the thin link file, or nullptr.
  /// \param ShouldPreserveUseListOrder Whether to preserve use-list order in
  ///        the written bitcode.
  ThinLTOBitcodeWriterPass(raw_ostream &OS, raw_ostream *ThinLinkOS,
                           bool ShouldPreserveUseListOrder = false)
      : OS(OS), ThinLinkOS(ThinLinkOS),
        ShouldPreserveUseListOrder(ShouldPreserveUseListOrder) {}

  /// Prepare module \p M for ThinLTO and write its bitcode.
  ///
  /// \param M Module to prepare and write as ThinLTO bitcode.
  /// \param AM Module analysis manager providing analyses for the pass.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // namespace llvm

#endif
