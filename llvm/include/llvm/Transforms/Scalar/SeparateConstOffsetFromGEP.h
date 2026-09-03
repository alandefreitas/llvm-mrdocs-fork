//===- SeparateConstOffsetFromGEP.h ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_SEPARATECONSTOFFSETFROMGEP_H
#define LLVM_TRANSFORMS_SCALAR_SEPARATECONSTOFFSETFROMGEP_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// Pass that splits GEPs into a variadic base and a constant offset.
///
/// Improves CSE and reg+immediate addressing by separating constant offsets
/// from GEP address calculations. When LowerGEP is enabled, also lowers
/// multi-index GEPs to single-index GEPs and extracts struct-field offsets.
class SeparateConstOffsetFromGEPPass
    : public OptionalPassInfoMixin<SeparateConstOffsetFromGEPPass> {
  bool LowerGEP;

public:
  /// Construct a SeparateConstOffsetFromGEP pass.
  /// @param LowerGEP When true, also lower multi-index GEPs to single-index
  /// GEPs and extract struct-field offsets.
  SeparateConstOffsetFromGEPPass(bool LowerGEP = false) : LowerGEP(LowerGEP) {}
  /// Print this pass's pipeline representation to \p OS.
  /// @param OS Stream to write the pipeline string to.
  /// @param MapClassName2PassName Maps class names to pass names.
  LLVM_ABI void
  printPipeline(raw_ostream &OS,
                function_ref<StringRef(StringRef)> MapClassName2PassName);
  /// Run SeparateConstOffsetFromGEP over the function.
  /// @param F Function whose GEPs may be split.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_SEPARATECONSTOFFSETFROMGEP_H
