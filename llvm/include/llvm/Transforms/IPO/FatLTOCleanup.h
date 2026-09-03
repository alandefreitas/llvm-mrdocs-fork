//===- FatLtoCleanup.h - clean up IR for the FatLTO pipeline ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines operations used to clean up IR for the FatLTO pipeline.
// Instrumentation that is beneficial for bitcode sections used in LTO may
// need to be cleaned up to finish non-LTO compilation. llvm.checked.load is
// an example of an instruction that we want to preserve for LTO, but is
// incorrect to leave unchanged during the per-TU compilation in FatLTO.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_FATLTOCLEANUP_H
#define LLVM_TRANSFORMS_IPO_FATLTOCLEANUP_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Module;
class ModuleSummaryIndex;

/// Pass that cleans up IR for the FatLTO pipeline.
///
/// Instrumentation beneficial for bitcode sections used in LTO may need to be
/// cleaned up to finish non-LTO compilation. For example, llvm.checked.load
/// should be preserved for LTO but must not be left unchanged during per-TU
/// compilation in FatLTO.
class FatLtoCleanup : public RequiredPassInfoMixin<FatLtoCleanup> {
public:
  /// Construct a FatLTO cleanup pass.
  FatLtoCleanup() = default;

  /// Clean up FatLTO-incompatible IR in module \p M.
  ///
  /// \param M Module whose IR is cleaned up for non-LTO FatLTO compilation.
  /// \param AM Module analysis manager providing analyses for the pass.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_FATLTOCLEANUP_H
