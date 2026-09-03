//===- IPO/OpenMPOpt.h - Collection of OpenMP optimizations -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_OPENMPOPT_H
#define LLVM_TRANSFORMS_IPO_OPENMPOPT_H

#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

namespace omp {

/// Summary of a kernel (=entry point for target offloading).
using Kernel = Function *;

/// Set of kernels in the module
using KernelSet = SetVector<Kernel>;

/// Helper to determine if \p M contains OpenMP.
///
/// \param M Module to inspect for OpenMP usage.
/// \return True if \p M contains OpenMP.
LLVM_ABI bool containsOpenMP(Module &M);

/// Helper to determine if \p M is a OpenMP target offloading device module.
///
/// \param M Module to test for OpenMP device offloading.
/// \return True if \p M is an OpenMP target offloading device module.
LLVM_ABI bool isOpenMPDevice(Module &M);

/// Return true iff \p Fn is an OpenMP GPU kernel; \p Fn has the "kernel"
/// attribute.
///
/// \param Fn Function to test for the OpenMP kernel attribute.
/// \return True if \p Fn is an OpenMP GPU kernel.
LLVM_ABI bool isOpenMPKernel(Function &Fn);

/// Get OpenMP device kernels in \p M.
///
/// \param M Module from which OpenMP device kernels are collected.
/// \return The set of OpenMP device kernels found in \p M.
LLVM_ABI KernelSet getDeviceKernels(Module &M);

} // namespace omp

/// OpenMP optimizations pass.
class OpenMPOptPass : public OptionalPassInfoMixin<OpenMPOptPass> {
public:
  /// Construct an OpenMP optimizations pass.
  OpenMPOptPass() = default;
  /// Construct an OpenMP optimizations pass for the given LTO phase.
  ///
  /// \param LTOPhase Thin/full LTO phase in which this pass runs.
  OpenMPOptPass(ThinOrFullLTOPhase LTOPhase) : LTOPhase(LTOPhase) {}

  /// Run OpenMP optimizations over the given module.
  ///
  /// \param M Module whose OpenMP constructs are optimized.
  /// \param AM Module analysis manager providing analyses for the pass.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

private:
  const ThinOrFullLTOPhase LTOPhase = ThinOrFullLTOPhase::None;
};

/// OpenMP optimizations pass operating on a call-graph SCC.
class OpenMPOptCGSCCPass : public OptionalPassInfoMixin<OpenMPOptCGSCCPass> {
public:
  /// Construct an OpenMP CGSCC optimizations pass.
  OpenMPOptCGSCCPass() = default;
  /// Construct an OpenMP CGSCC optimizations pass for the given LTO phase.
  ///
  /// \param LTOPhase Thin/full LTO phase in which this pass runs.
  OpenMPOptCGSCCPass(ThinOrFullLTOPhase LTOPhase) : LTOPhase(LTOPhase) {}

  /// Run OpenMP optimizations over SCC \p C.
  ///
  /// \param C The SCC whose functions are considered for OpenMP optimizations.
  /// \param AM The CGSCC analysis manager.
  /// \param CG The lazy call graph.
  /// \param UR The CGSCC update result.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(LazyCallGraph::SCC &C,
                                 CGSCCAnalysisManager &AM, LazyCallGraph &CG,
                                 CGSCCUpdateResult &UR);

private:
  const ThinOrFullLTOPhase LTOPhase = ThinOrFullLTOPhase::None;
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_OPENMPOPT_H
