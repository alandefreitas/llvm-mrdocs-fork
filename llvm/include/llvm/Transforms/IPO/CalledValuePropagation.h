//===- CalledValuePropagation.h - Propagate called values -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a transformation that attaches !callees metadata to
// indirect call sites. For a given call site, the metadata, if present,
// indicates the set of functions the call site could possibly target at
// run-time. This metadata is added to indirect call sites when the set of
// possible targets can be determined by analysis and is known to be small. The
// analysis driving the transformation is similar to constant propagation and
// makes uses of the generic sparse propagation solver.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_CALLEDVALUEPROPAGATION_H
#define LLVM_TRANSFORMS_IPO_CALLEDVALUEPROPAGATION_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// Pass that attaches !callees metadata to indirect call sites.
///
/// For a given call site, the metadata, if present, indicates the set of
/// functions the call site could possibly target at run-time. This metadata is
/// added to indirect call sites when the set of possible targets can be
/// determined by analysis and is known to be small. The analysis driving the
/// transformation is similar to constant propagation and makes use of the
/// generic sparse propagation solver.
class CalledValuePropagationPass
    : public OptionalPassInfoMixin<CalledValuePropagationPass> {
public:
  /// Run called-value propagation over the given module.
  ///
  /// \param M Module whose indirect call sites may receive !callees metadata.
  /// \param AM Module analysis manager providing analyses for the pass.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};
} // namespace llvm

#endif // LLVM_TRANSFORMS_IPO_CALLEDVALUEPROPAGATION_H
