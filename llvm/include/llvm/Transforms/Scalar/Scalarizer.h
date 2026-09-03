//===- Scalarizer.h --- Scalarize vector operations -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This pass converts vector operations into scalar operations (or, optionally,
/// operations on smaller vector widths), in order to expose optimization
/// opportunities on the individual scalar operations.
/// It is mainly intended for targets that do not have vector units, but it
/// may also be useful for revectorizing code to different vector widths.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_SCALARIZER_H
#define LLVM_TRANSFORMS_SCALAR_SCALARIZER_H

#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class Function;
class FunctionPass;

/// Options controlling how the Scalarizer pass splits vector operations.
struct ScalarizerPassOptions {
  /// Split vectors larger than this size into fragments, where each fragment is
  /// either a vector no larger than this size or a scalar.
  ///
  /// Instructions with operands or results of different sizes that would be
  /// split into a different number of fragments are currently left as-is.
  unsigned ScalarizeMinBits = 0;

  /// Allow the scalarizer pass to scalarize insertelement/extractelement with
  /// variable index.
  bool ScalarizeVariableInsertExtract = true;

  /// Allow the scalarizer pass to scalarize loads and store
  ///
  /// This is disabled by default because having separate loads and stores makes
  /// it more likely that the -combiner-alias-analysis limits will be reached.
  bool ScalarizeLoadStore = false;
};

/// Pass that converts vector operations into scalar or smaller-vector forms.
class ScalarizerPass : public OptionalPassInfoMixin<ScalarizerPass> {
  ScalarizerPassOptions Options;

public:
  /// Construct a Scalarizer pass with default options.
  ScalarizerPass() = default;
  /// Construct a Scalarizer pass with the given options.
  /// @param Options Scalarizer options to use for this pass instance.
  ScalarizerPass(const ScalarizerPassOptions &Options) : Options(Options) {}

  /// Run the Scalarizer pass over the function.
  /// @param F Function whose vector operations may be scalarized.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  /// Set whether to scalarize insertelement/extractelement with a variable
  /// index.
  /// @param Value True to allow scalarizing variable-index insert/extract.
  void setScalarizeVariableInsertExtract(bool Value) {
    Options.ScalarizeVariableInsertExtract = Value;
  }
  /// Set whether to scalarize vector loads and stores.
  /// @param Value True to allow scalarizing loads and stores.
  void setScalarizeLoadStore(bool Value) { Options.ScalarizeLoadStore = Value; }
  /// Set the minimum fragment bit width used when splitting vectors.
  /// @param Value Minimum bits per fragment; zero requests full scalarization.
  void setScalarizeMinBits(unsigned Value) { Options.ScalarizeMinBits = Value; }
};

/// Create a legacy pass manager instance of the Scalarizer pass.
/// @param Options Scalarizer options for the created pass instance.
/// @return A new legacy FunctionPass that runs the Scalarizer.
LLVM_ABI FunctionPass *createScalarizerPass(
    const ScalarizerPassOptions &Options = ScalarizerPassOptions());
}

#endif /* LLVM_TRANSFORMS_SCALAR_SCALARIZER_H */
