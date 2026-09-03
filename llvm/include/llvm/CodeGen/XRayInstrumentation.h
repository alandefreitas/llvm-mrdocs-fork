//===- llvm/CodeGen/XRayInstrumentation.h -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_XRAYINSTRUMENTATION_H
#define LLVM_CODEGEN_XRAYINSTRUMENTATION_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

/// New PM pass that inserts XRay instrumentation into machine functions.
///
/// Looks for XRay-specific function attributes to decide whether to insert
/// the replacement operations that enable runtime patching of entry and exit
/// sleds.
class XRayInstrumentationPass
    : public RequiredPassInfoMixin<XRayInstrumentationPass> {
public:
  /// Insert XRay instrumentation instructions into \p MF when requested.
  /// \param MF Machine function to instrument.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after inserting XRay instrumentation.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_XRAYINSTRUMENTATION_H
