//===- SCCP.h - Sparse Conditional Constant Propagation ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass implements  interprocedural sparse conditional constant
// propagation and merging.
//
// Specifically, this:
//   * Assumes values are constant unless proven otherwise
//   * Assumes BasicBlocks are dead unless proven otherwise
//   * Proves values to be constant, and replaces them with constants
//   * Proves conditional branches to be unconditional
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_SCCP_H
#define LLVM_TRANSFORMS_IPO_SCCP_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Module;

/// A set of parameters to control various transforms performed by IPSCCP pass.
///
/// Each of the boolean parameters can be set to: true - enabling the
/// transformation. false - disabling the transformation. Intended use is to
/// create a default object, modify parameters with additional setters and then
/// pass it to IPSCCP.
struct IPSCCPOptions {
  /// Whether function specialization is allowed during IPSCCP.
  bool AllowFuncSpec;

  /// Construct IPSCCP options with the given function-specialization flag.
  ///
  /// \param AllowFuncSpec Whether function specialization is enabled.
  IPSCCPOptions(bool AllowFuncSpec = true) : AllowFuncSpec(AllowFuncSpec) {}

  /// Enables or disables Specialization of Functions.
  ///
  /// \param FuncSpec True to enable function specialization, false to disable
  /// it.
  /// \return A reference to these options for chaining.
  IPSCCPOptions &setFuncSpec(bool FuncSpec) {
    AllowFuncSpec = FuncSpec;
    return *this;
  }
};

/// Pass to perform interprocedural constant propagation.
class IPSCCPPass : public OptionalPassInfoMixin<IPSCCPPass> {
  IPSCCPOptions Options;

public:
  /// Construct an IPSCCP pass with default options.
  IPSCCPPass() = default;

  /// Construct an IPSCCP pass with the given options.
  ///
  /// \param Options Options controlling IPSCCP transforms such as function
  /// specialization.
  IPSCCPPass(IPSCCPOptions Options) : Options(Options) {}

  /// Run interprocedural sparse conditional constant propagation over \p M.
  ///
  /// \param M Module to optimize.
  /// \param AM Module analysis manager providing analyses for the pass.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

  /// Return whether function specialization is enabled for this pass.
  ///
  /// \return True if function specialization is enabled.
  bool isFuncSpecEnabled() const { return Options.AllowFuncSpec; }
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_SCCP_H
