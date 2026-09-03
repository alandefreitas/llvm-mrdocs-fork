//===--------- HipStdPar.h - Standard Parallelism passes --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// AcceleratorCodeSelection - Identify all functions reachable from a kernel,
/// removing those that are unreachable.
///
/// AllocationInterposition - Forward calls to allocation / deallocation
//  functions to runtime provided equivalents that allocate memory that is
//  accessible for an accelerator
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_HIPSTDPAR_HIPSTDPAR_H
#define LLVM_TRANSFORMS_HIPSTDPAR_HIPSTDPAR_H

#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class Module;

/// Pass that keeps only functions reachable from HIPSTDPAR kernels.
class HipStdParAcceleratorCodeSelectionPass
    : public RequiredPassInfoMixin<HipStdParAcceleratorCodeSelectionPass> {
public:
  /// Select accelerator-reachable code in module \p M.
  ///
  /// \param M Module whose unreachable non-kernel functions are removed.
  /// \param MAM Module analysis manager providing analyses for the pass.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
};

/// Pass that redirects allocations to accelerator-accessible runtime APIs.
class HipStdParAllocationInterpositionPass
    : public RequiredPassInfoMixin<HipStdParAllocationInterpositionPass> {
public:
  /// Interpose allocation and deallocation calls in module \p M.
  ///
  /// \param M Module whose alloc/dealloc calls are forwarded to the runtime.
  /// \param MAM Module analysis manager providing analyses for the pass.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
};

/// Pass that rewrites math calls through the HIPSTDPAR forwarding layer.
class HipStdParMathFixupPass
    : public RequiredPassInfoMixin<HipStdParMathFixupPass> {
public:
  /// Replace problematic math intrinsics and libcalls in module \p M.
  ///
  /// \param M Module whose math calls are redirected to HIPSTDPAR forwards.
  /// \param MAM Module analysis manager providing analyses for the pass.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_HIPSTDPAR_HIPSTDPAR_H
