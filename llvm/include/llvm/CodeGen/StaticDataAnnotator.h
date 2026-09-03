//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_STATICDATAANNOTATOR_H
#define LLVM_CODEGEN_STATICDATAANNOTATOR_H

#include "llvm/IR/Analysis.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

/// New PM pass that annotates static data section prefixes from profiles.
///
/// Iterates global variables in the module, looks up hotness counters from
/// StaticDataProfileInfo, and sets each variable's section prefix based on
/// profile summary analysis. Coordinates with StaticDataSplitter, which
/// gathers the per-data profile information this pass consumes.
class StaticDataAnnoatorPass
    : public OptionalPassInfoMixin<StaticDataAnnoatorPass> {
public:
  /// Annotate section prefixes of static data in \p M from profile info.
  /// \param M Module whose global variables receive section prefixes.
  /// \param MAM Module analysis manager providing required analyses.
  /// \return PreservedAnalyses reflecting which analyses remain valid after
  ///         annotation.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_STATICDATAANNOTATOR_H
