//===- BoundsChecking.h - Bounds checking instrumentation -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_BOUNDSCHECKING_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_BOUNDSCHECKING_H

#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"
#include <optional>

namespace llvm {
class Function;

/// A pass to instrument code and perform run-time bounds checking on loads,
/// stores, and other memory intrinsics.
class BoundsCheckingPass : public RequiredPassInfoMixin<BoundsCheckingPass> {

public:
  /// Configuration options for bounds-checking instrumentation.
  struct Options {
    /// Runtime-handler settings used when reporting out-of-bounds accesses.
    struct Runtime {
      /// Construct runtime-handler options.
      /// @param MinRuntime Whether to use the minimal UBSan handler.
      /// @param MayReturn Whether the handler may return to the caller.
      /// @param HandlerPreserveAllRegs Whether the handler preserves all
      /// registers (PreserveAll calling convention).
      Runtime(bool MinRuntime, bool MayReturn, bool HandlerPreserveAllRegs)
          : MinRuntime(MinRuntime), MayReturn(MayReturn),
            HandlerPreserveAllRegs(HandlerPreserveAllRegs) {}
      /// Whether to call the minimal UBSan local-out-of-bounds handler.
      bool MinRuntime;
      /// Whether the runtime handler is allowed to return.
      bool MayReturn;
      /// Whether the handler uses the PreserveAll calling convention.
      bool HandlerPreserveAllRegs;
    };
    /// Runtime handler options; if empty, traps instead of calling a handler.
    std::optional<Runtime> Rt;
    /// Whether to allow merging of trap calls across bounds checks.
    bool Merge = false;
    /// Optional `allow_ubsan_check` kind argument; omitted if empty.
    std::optional<int8_t> GuardKind;
  };

  /// Construct a bounds-checking pass with the given options.
  /// @param Opts Instrumentation options for the pass.
  BoundsCheckingPass(Options Opts) : Opts(Opts) {}
  /// Run bounds-checking instrumentation over the function.
  /// @param F Function to instrument.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  /// Print this pass's pipeline representation to \p OS.
  /// @param OS Stream to write the pipeline string to.
  /// @param MapClassName2PassName Maps class names to pass names.
  LLVM_ABI void
  printPipeline(raw_ostream &OS,
                function_ref<StringRef(StringRef)> MapClassName2PassName);

private:
  Options Opts;
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_INSTRUMENTATION_BOUNDSCHECKING_H
