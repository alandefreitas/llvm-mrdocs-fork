//===- llvm/CodeGen/MachineLICM.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINELICM_H
#define LLVM_CODEGEN_MACHINELICM_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

/// CRTP base for new-PM machine loop-invariant code motion passes.
///
/// Hoists simple loop-invariant machine instructions out of loops. Not a
/// replacement for IR-level LICM; it targets constructs exposed only after
/// lowering and instruction selection.
///
/// \tparam DerivedT Concrete pass type that inherits from this base.
/// \tparam PreRegAlloc True to run before register allocation; false after.
template <typename DerivedT, bool PreRegAlloc>
class MachineLICMBasePass : public OptionalPassInfoMixin<DerivedT> {
public:
  /// Run machine LICM on \p MF.
  /// \param MF Machine function to optimize.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after machine LICM.
  PreservedAnalyses run(MachineFunction &MF,
                        MachineFunctionAnalysisManager &MFAM);
};

/// New PM pass that performs machine LICM before register allocation.
class EarlyMachineLICMPass
    : public MachineLICMBasePass<EarlyMachineLICMPass, true> {};

/// New PM pass that performs machine LICM after register allocation.
class MachineLICMPass : public MachineLICMBasePass<MachineLICMPass, false> {};

} // namespace llvm

extern template class llvm::MachineLICMBasePass<llvm::EarlyMachineLICMPass,
                                                true>;
extern template class llvm::MachineLICMBasePass<llvm::MachineLICMPass, false>;

#endif // LLVM_CODEGEN_MACHINELICM_H
