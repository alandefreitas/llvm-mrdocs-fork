//===- MergedLoadStoreMotion.h - merge and hoist/sink load/stores ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//! \file
//! This pass performs merges of loads and stores on both sides of a
//  diamond (hammock). It hoists the loads and sinks the stores.
//
// The algorithm iteratively hoists two loads to the same address out of a
// diamond (hammock) and merges them into a single load in the header. Similar
// it sinks and merges two stores to the tail block (footer). The algorithm
// iterates over the instructions of one side of the diamond and attempts to
// find a matching load/store on the other side. It hoists / sinks when it
// thinks it safe to do so.  This optimization helps with eg. hiding load
// latencies, triggering if-conversion, and reducing static code size.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_MERGEDLOADSTOREMOTION_H
#define LLVM_TRANSFORMS_SCALAR_MERGEDLOADSTOREMOTION_H

#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/IR/PassManager.h"

namespace llvm {
class Function;

/// Options controlling merged load/store motion behavior.
struct MergedLoadStoreMotionOptions {
  /// Whether to split the diamond footer when it has more than two predecessors.
  bool SplitFooterBB;
  /// Construct merged load/store motion options.
  /// @param SplitFooterBB When true, allow splitting the footer BB to sink
  /// stores when it has more than two predecessors.
  MergedLoadStoreMotionOptions(bool SplitFooterBB = false)
      : SplitFooterBB(SplitFooterBB) {}

  /// Set whether the diamond footer may be split when sinking stores.
  /// @param SFBB True to allow splitting the footer BB, false to disable it.
  /// @return Reference to this options object for chaining.
  MergedLoadStoreMotionOptions &splitFooterBB(bool SFBB) {
    SplitFooterBB = SFBB;
    return *this;
  }
};

/// Pass that merges loads and stores across diamond (hammock) control flow.
///
/// Hoists matching loads into the diamond header and sinks matching stores
/// into the footer, which can hide load latency, enable if-conversion, and
/// reduce static code size.
class MergedLoadStoreMotionPass
    : public OptionalPassInfoMixin<MergedLoadStoreMotionPass> {
  MergedLoadStoreMotionOptions Options;

public:
  /// Construct a merged load/store motion pass with default options.
  MergedLoadStoreMotionPass()
      : MergedLoadStoreMotionPass(MergedLoadStoreMotionOptions()) {}
  /// Construct a merged load/store motion pass with the given options.
  /// @param PassOptions Configuration controlling footer splitting.
  MergedLoadStoreMotionPass(const MergedLoadStoreMotionOptions &PassOptions)
      : Options(PassOptions) {}
  /// Run merged load/store motion over the function.
  /// @param F Function to transform.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  /// Print this pass's pipeline representation to \p OS.
  /// @param OS Stream to write the pipeline string to.
  /// @param MapClassName2PassName Maps class names to pass names.
  LLVM_ABI void
  printPipeline(raw_ostream &OS,
                function_ref<StringRef(StringRef)> MapClassName2PassName);
};
}

#endif // LLVM_TRANSFORMS_SCALAR_MERGEDLOADSTOREMOTION_H
