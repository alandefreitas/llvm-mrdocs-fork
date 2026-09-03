//==- RegAllocFast.h ----------- fast register allocator  ----------*-C++-*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_REGALLOCFAST_H
#define LLVM_CODEGEN_REGALLOCFAST_H

#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/CodeGen/RegAllocCommon.h"

namespace llvm {

/// New PM pass that performs fast (local) register allocation.
class RegAllocFastPass : public RequiredPassInfoMixin<RegAllocFastPass> {
public:
  /// Configuration options for the fast register allocator pass.
  struct Options {
    /// Optional filter selecting which virtual registers to allocate.
    RegAllocFilterFunc Filter;
    /// Name of the filter for pipeline printing; defaults to "all".
    StringRef FilterName;
    /// Whether to clear virtual registers after allocation.
    bool ClearVRegs;
    /// Construct options for the fast register allocator.
    /// \param F Optional filter for which virtual registers to allocate.
    /// \param FN Name of the filter shown in pipeline strings.
    /// \param CV If true, clear virtual registers after allocation.
    Options(RegAllocFilterFunc F = nullptr, StringRef FN = "all",
            bool CV = true)
        : Filter(std::move(F)), FilterName(FN), ClearVRegs(CV) {}
  };

  /// Construct a fast register allocator pass.
  /// \param Opts Configuration options for filtering and clearing vregs.
  RegAllocFastPass(Options Opts = Options()) : Opts(std::move(Opts)) {}

  /// Return the properties this pass requires of the machine function.
  ///
  /// Fast register allocation expects the function to contain no PHI nodes.
  /// \return Required properties, with NoPHIs set.
  MachineFunctionProperties getRequiredProperties() const {
    return MachineFunctionProperties().setNoPHIs();
  }

  /// Return the properties this pass sets on the machine function.
  ///
  /// When clearing virtual registers, marks the function as having no vregs.
  /// \return Properties set by this pass, including \c NoVRegs when clearing.
  MachineFunctionProperties getSetProperties() const {
    if (Opts.ClearVRegs) {
      return MachineFunctionProperties().setNoVRegs();
    }

    return MachineFunctionProperties();
  }

  /// Return the properties this pass clears on the machine function.
  ///
  /// Allocation may leave the function no longer in SSA form.
  /// \return Properties clearing IsSSA.
  MachineFunctionProperties getClearedProperties() const {
    return MachineFunctionProperties().setIsSSA();
  }

  /// Allocate registers in \p MF using the fast local allocator.
  /// \param MF Machine function whose virtual registers are allocated.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after fast register allocation.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);

  /// Print this pass and its options as a pipeline string.
  /// \param OS Stream to write the pipeline string to.
  /// \param MapClassName2PassName Maps class names to pass names.
  LLVM_ABI void
  printPipeline(raw_ostream &OS,
                function_ref<StringRef(StringRef)> MapClassName2PassName);

private:
  Options Opts;
};

} // namespace llvm

#endif // LLVM_CODEGEN_REGALLOCFAST_H
