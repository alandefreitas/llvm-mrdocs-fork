//===- llvm/CodeGen/MachineDomTreeUpdater.h -----------------------*- C++-*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file exposes interfaces to post dominance information for
// target-specific code.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEDOMTREEUPDATER_H
#define LLVM_CODEGEN_MACHINEDOMTREEUPDATER_H

#include "llvm/Analysis/GenericDomTreeUpdater.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class MachinePostDominatorTree;
class MachineDomTreeUpdater;

/// Explicit instantiation of GenericDomTreeUpdater for MachineDominatorTree
/// and MachinePostDominatorTree.
extern template class LLVM_TEMPLATE_ABI GenericDomTreeUpdater<
    MachineDomTreeUpdater, MachineDominatorTree, MachinePostDominatorTree>;

extern template LLVM_TEMPLATE_ABI void
GenericDomTreeUpdater<MachineDomTreeUpdater, MachineDominatorTree,
                      MachinePostDominatorTree>::recalculate(MachineFunction
                                                                 &MF);

extern template LLVM_TEMPLATE_ABI void GenericDomTreeUpdater<
    MachineDomTreeUpdater, MachineDominatorTree,
    MachinePostDominatorTree>::applyUpdatesImpl</*IsForward=*/true>();
extern template LLVM_TEMPLATE_ABI void GenericDomTreeUpdater<
    MachineDomTreeUpdater, MachineDominatorTree,
    MachinePostDominatorTree>::applyUpdatesImpl</*IsForward=*/false>();

/// Updates MachineDominatorTree and MachinePostDominatorTree.
///
/// Provides a uniform way to submit CFG updates and delete machine basic
/// blocks under either Eager or Lazy update strategies.
class MachineDomTreeUpdater
    : public GenericDomTreeUpdater<MachineDomTreeUpdater, MachineDominatorTree,
                                   MachinePostDominatorTree> {
  friend GenericDomTreeUpdater<MachineDomTreeUpdater, MachineDominatorTree,
                               MachinePostDominatorTree>;

public:
  /// Base class type specialized for MachineDominatorTree and
  /// MachinePostDominatorTree.
  using Base =
      GenericDomTreeUpdater<MachineDomTreeUpdater, MachineDominatorTree,
                            MachinePostDominatorTree>;
  /// Inherit constructors from the GenericDomTreeUpdater base.
  using Base::Base;

  /// Flush pending updates and destroy this updater.
  ~MachineDomTreeUpdater() { flush(); }

  ///@{
  /// \name Mutation APIs
  ///

  /// Delete machine basic block \p DelBB from its function and any available
  /// trees.
  ///
  /// DelBB will be removed from its Parent and erased from available trees if
  /// it exists and finally get deleted. Under Eager UpdateStrategy, DelBB will
  /// be processed immediately. Under Lazy UpdateStrategy, DelBB will be queued
  /// until a flush event and all available trees are up-to-date. Assert if any
  /// instruction of DelBB is modified while awaiting deletion. When both DT and
  /// PDT are nullptrs, DelBB will be queued until flush() is called.
  /// \param DelBB Machine basic block to remove and delete.
  LLVM_ABI void deleteBB(MachineBasicBlock *DelBB);

  ///@}

private:
  /// First remove all the instructions of DelBB and then make sure DelBB has a
  /// valid terminator instruction which is necessary to have when DelBB still
  /// has to be inside of its parent Function while awaiting deletion under Lazy
  /// UpdateStrategy to prevent other routines from asserting the state of the
  /// IR is inconsistent. Assert if DelBB is nullptr or has predecessors.
  void validateDeleteBB(MachineBasicBlock *DelBB);

  /// Returns true if at least one MachineBasicBlock is deleted.
  bool forceFlushDeletedBB();
};
} // namespace llvm
#endif // LLVM_CODEGEN_MACHINEDOMTREEUPDATER_H
