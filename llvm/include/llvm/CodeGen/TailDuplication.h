//===- llvm/CodeGen/TailDuplication.h ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_TAILDUPLICATIONPASS_H
#define LLVM_CODEGEN_TAILDUPLICATIONPASS_H

#include "llvm/CodeGen/MBFIWrapper.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

template <typename DerivedT, bool PreRegAlloc>
/// CRTP base for new-PM machine-function tail-duplication passes.
///
/// Duplicates basic blocks ending in unconditional branches into the tails of
/// their predecessors.
///
/// \tparam DerivedT Concrete pass type that inherits from this base.
/// \tparam PreRegAlloc True to run before register allocation; false after.
class TailDuplicatePassBase : public OptionalPassInfoMixin<DerivedT> {
private:
  std::unique_ptr<MBFIWrapper> MBFIW;

public:
  /// Duplicate eligible block tails in \p MF.
  /// \param MF Machine function whose tails are duplicated.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after tail duplication.
  PreservedAnalyses run(MachineFunction &MF,
                        MachineFunctionAnalysisManager &MFAM);
};

/// New PM pass that performs early (pre-regalloc) tail duplication.
class EarlyTailDuplicatePass
    : public TailDuplicatePassBase<EarlyTailDuplicatePass, true> {
public:
  /// Return the properties this pass clears on the machine function.
  ///
  /// Early tail duplication may introduce PHI instructions, so the NoPHIs
  /// property is cleared.
  /// \return The machine function properties cleared by this pass.
  MachineFunctionProperties getClearedProperties() const {
    return MachineFunctionProperties().setNoPHIs();
  }
};

/// New PM pass that performs late (post-regalloc) tail duplication.
class TailDuplicatePass
    : public TailDuplicatePassBase<TailDuplicatePass, false> {};

} // namespace llvm

extern template class llvm::TailDuplicatePassBase<llvm::EarlyTailDuplicatePass,
                                                  true>;
extern template class llvm::TailDuplicatePassBase<llvm::TailDuplicatePass,
                                                  false>;

#endif // LLVM_CODEGEN_TAILDUPLICATIONPASS_H
