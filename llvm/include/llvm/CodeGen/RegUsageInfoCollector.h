//===- llvm/CodeGen/RegUsageInfoCollector.h ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_REGUSAGEINFOCOLLECTOR_H
#define LLVM_CODEGEN_REGUSAGEINFOCOLLECTOR_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

/// New PM pass that collects physical register usage for IPRA.
///
/// Iterates used physical registers in each function, builds a RegMask, and
/// stores it in PhysicalRegisterUsageInfo for interprocedural register
/// allocation.
class RegUsageInfoCollectorPass
    : public RequiredPassInfoMixin<RegUsageInfoCollectorPass> {
public:
  /// Collect and store register usage information for \p MF.
  /// \param MF Machine function whose physical register usage is collected.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after collecting register usage.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_REGUSAGEINFOCOLLECTOR_H
