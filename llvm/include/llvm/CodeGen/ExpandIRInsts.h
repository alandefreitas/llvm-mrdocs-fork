//===- ExpandIRInsts.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_EXPANDIRINSTS_H
#define LLVM_CODEGEN_EXPANDIRINSTS_H

#include "llvm/IR/PassManager.h"
#include "llvm/Support/CodeGen.h"

namespace llvm {

class TargetMachine;

/// New PM pass that expands certain IR instructions for the target.
///
/// Expands wide fp conversions, frem, and wide div/rem into forms that the
/// backend can lower, scalarizing vector operands when needed.
class ExpandIRInstsPass : public RequiredPassInfoMixin<ExpandIRInstsPass> {
private:
  const TargetMachine *TM;
  CodeGenOptLevel OptLevel;

public:
  /// Construct a pass using target information from \p TM.
  /// \param TM Target machine used to decide how instructions are expanded.
  /// \param OptLevel CodeGen optimization level that controls expansions.
  LLVM_ABI explicit ExpandIRInstsPass(const TargetMachine &TM,
                                      CodeGenOptLevel OptLevel);

  /// Expand IR instructions in \p F for the configured target.
  /// \param F Function whose IR instructions are expanded.
  /// \param AM Function analysis manager providing required analyses.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  /// Print this pass and its options as a pipeline string.
  /// \param OS Stream to write the pipeline string to.
  /// \param MapClassName2PassName Maps class names to pass names.
  LLVM_ABI void
  printPipeline(raw_ostream &OS,
                function_ref<StringRef(StringRef)> MapClassName2PassName);
};

} // end namespace llvm

#endif // LLVM_CODEGEN_EXPANDIRINSTS_H
