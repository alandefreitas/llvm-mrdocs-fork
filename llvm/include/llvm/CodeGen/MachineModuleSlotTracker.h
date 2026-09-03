//===-- llvm/CodeGen/MachineModuleInfo.h ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEMODULESLOTTRACKER_H
#define LLVM_CODEGEN_MACHINEMODULESLOTTRACKER_H

#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/IR/ModuleSlotTracker.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class AbstractSlotTrackerStorage;
class Function;
class MachineModuleInfo;
class MachineFunction;
class Module;

/// Callback that returns the MachineFunction for a given IR Function.
using MFGetterFnT = function_ref<MachineFunction *(const Function &)>;

/// Module slot tracker that also numbers MachineFunction metadata.
///
/// Extends \a ModuleSlotTracker with hooks that incorporate metadata created
/// in the backend (for example on machine memory operands) so MIR printing
/// can share consistent metadata slot numbers with IR.
class LLVM_ABI MachineModuleSlotTracker : public ModuleSlotTracker {
  const Function &TheFunction;
  const MachineFunction *TheMF;
  unsigned MDNStartSlot = 0, MDNEndSlot = 0;

  void processMachineFunctionMetadata(AbstractSlotTrackerStorage *AST,
                                      const MachineFunction &MF);
  void processMachineModule(AbstractSlotTrackerStorage *AST, const Module *M,
                            bool ShouldInitializeAllMetadata);
  void processMachineFunction(AbstractSlotTrackerStorage *AST,
                              const Function *F,
                              bool ShouldInitializeAllMetadata);

public:
  /// Construct a slot tracker for a machine function.
  ///
  /// Initializes the base \a ModuleSlotTracker from the IR module of \p MF,
  /// then installs process hooks that number machine-level metadata for the
  /// function returned by \p Fn.
  ///
  /// \param Fn Callback used to obtain the MachineFunction for an IR Function.
  /// \param MF Machine function whose IR parent module is tracked.
  /// \param ShouldInitializeAllMetadata If true, number all metadata in the
  ///        module up front so references stay consistent across callers.
  MachineModuleSlotTracker(MFGetterFnT Fn, const MachineFunction *MF,
                           bool ShouldInitializeAllMetadata = true);
  /// Destructor.
  ~MachineModuleSlotTracker() override;

  /// Collect machine-created metadata nodes and their slot numbers.
  ///
  /// Appends (slot, MDNode) pairs for metadata incorporated from the machine
  /// function into \p L.
  ///
  /// \param L Output list of (slot, MDNode) pairs for machine metadata.
  void collectMachineMDNodes(MachineMDNodeListType &L) const;
};

} // namespace llvm

#endif // LLVM_CODEGEN_MACHINEMODULESLOTTRACKER_H
