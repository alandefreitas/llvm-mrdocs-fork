//===- MachineIDFSSAUpdater.h - Unstructured SSA Update Tool ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the MachineIDFSSAUpdater class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEIDFSSAUPDATER_H
#define LLVM_CODEGEN_MACHINEIDFSSAUPDATER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Register.h"

namespace llvm {

class MachineDominatorTree;
class MachineInstrBuilder;
class MachineBasicBlock;
class TargetInstrInfo;

/// Efficient SSA form updater for machine IR using iterated dominance frontiers.
///
/// Places PHI nodes via the IDF algorithm after all definitions and uses have
/// been registered, offering better performance than the incremental
/// \c MachineSSAUpdater approach for batch updates.
class MachineIDFSSAUpdater {
  struct BBValueInfo {
    Register LiveInValue;
    Register LiveOutValue;
  };

  MachineDominatorTree &DT;
  MachineRegisterInfo &MRI;
  const TargetInstrInfo &TII;
  MachineRegisterInfo::VRegAttrs RegAttrs;

  SmallVector<std::pair<MachineBasicBlock *, Register>, 4> Defines;
  SmallVector<MachineBasicBlock *, 4> UseBlocks;
  DenseMap<MachineBasicBlock *, BBValueInfo> BBInfos;

  MachineInstrBuilder createInst(unsigned Opc, MachineBasicBlock *BB,
                                 MachineBasicBlock::iterator I);

  // IsLiveOut indicates whether we are computing live-out values (true) or
  // live-in values (false).
  Register computeValue(MachineBasicBlock *BB, bool IsLiveOut);

public:
  /// Construct an updater for a virtual register with the given attributes.
  ///
  /// \param DT Dominator tree for the machine function.
  /// \param MF Machine function being rewritten.
  /// \param RegAttr Register class/bank and type attributes for new values.
  MachineIDFSSAUpdater(MachineDominatorTree &DT, MachineFunction &MF,
                       const MachineRegisterInfo::VRegAttrs &RegAttr)
      : DT(DT), MRI(MF.getRegInfo()), TII(*MF.getSubtarget().getInstrInfo()),
        RegAttrs(RegAttr) {}

  /// Construct an updater that copies attributes from an existing register.
  ///
  /// \param DT Dominator tree for the machine function.
  /// \param MF Machine function being rewritten.
  /// \param Reg Virtual register whose attributes are used for new values.
  MachineIDFSSAUpdater(MachineDominatorTree &DT, MachineFunction &MF,
                       Register Reg)
      : MachineIDFSSAUpdater(DT, MF, MF.getRegInfo().getVRegAttrs(Reg)) {}

  /// Indicate that a rewritten value is available in the specified block
  /// with the specified value.
  ///
  /// Must be called before invoking Calculate().
  ///
  /// \param BB Block in which the rewritten value is available.
  /// \param V Value available in \p BB.
  void addAvailableValue(MachineBasicBlock *BB, Register V) {
    Defines.emplace_back(BB, V);
  }

  /// Record a basic block that uses the value.
  ///
  /// This method should be called for every basic block where the value will be
  /// used. Must be called before invoking Calculate().
  ///
  /// \param BB Block that contains a use of the rewritten value.
  void addUseBlock(MachineBasicBlock *BB) { UseBlocks.push_back(BB); }

  /// Calculate and insert necessary PHI nodes for SSA form.
  /// Must be called after registering all definitions and uses.
  LLVM_ABI void calculate();

  /// See SSAUpdater::GetValueInMiddleOfBlock description.
  ///
  /// \param BB Block in whose middle the live value is materialized.
  /// \return The register live in the middle of \p BB.
  LLVM_ABI Register getValueInMiddleOfBlock(MachineBasicBlock *BB);
};

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINEIDFSSAUPDATER_H
