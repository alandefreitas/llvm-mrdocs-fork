//===- llvm/CodeGen/RegUsageInfoPropagate.h ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_REGUSAGEINFOPROPAGATE_H
#define LLVM_CODEGEN_REGUSAGEINFOPROPAGATE_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

/// New PM pass that propagates callee register usage masks onto call sites.
///
/// At each callsite, queries PhysicalRegisterUsageInfo for the callee's
/// RegMask (from actual register allocation) and updates the call instruction
/// when that detail is available, so the register allocator can use it.
class RegUsageInfoPropagationPass
    : public RequiredPassInfoMixin<RegUsageInfoPropagationPass> {
public:
  /// Propagate register usage masks onto call instructions in \p MF.
  /// \param MF Machine function whose call sites are updated.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after propagating register usage
  ///         masks.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_REGUSAGEINFOPROPAGATE_H
