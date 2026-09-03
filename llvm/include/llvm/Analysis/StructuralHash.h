//=- StructuralHash.h - Structural Hash Printing --*- C++ -*-----------------=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_STRUCTURALHASH_H
#define LLVM_ANALYSIS_STRUCTURALHASH_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// Options that control how structural hashes are computed.
enum class StructuralHashOptions {
  /// Hash with opcode only.
  None,
  /// Hash with opcode and operands.
  Detailed,
  /// Ignore call target operand when computing hash.
  CallTargetIgnored,
};

/// Printer pass for  StructuralHashes
class StructuralHashPrinterPass
    : public RequiredPassInfoMixin<StructuralHashPrinterPass> {
  raw_ostream &OS;
  const StructuralHashOptions Options;

public:
  /// Construct a structural-hash printer that writes to \p OS.
  /// @param OS Output stream for the printed hashes.
  /// @param Options How structural hashes are computed for printing.
  explicit StructuralHashPrinterPass(raw_ostream &OS,
                                     StructuralHashOptions Options)
      : OS(OS), Options(Options) {}

  /// Print structural hashes for the functions in module \p M.
  /// @param M Module whose function hashes are printed.
  /// @param MAM Module analysis manager (unused).
  /// @return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
};

} // namespace llvm

#endif // LLVM_ANALYSIS_STRUCTURALHASH_H
