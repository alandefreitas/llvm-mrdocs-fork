//===- MachineSSAUpdater.h - Unstructured SSA Update Tool -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the MachineSSAUpdater class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINESSAUPDATER_H
#define LLVM_CODEGEN_MACHINESSAUPDATER_H

#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Register.h"

namespace llvm {

class MachineBasicBlock;
class MachineFunction;
class MachineInstr;
class MachineOperand;
class MachineRegisterInfo;
class TargetInstrInfo;
class MCRegisterClass;
using TargetRegisterClass = MCRegisterClass;
template<typename T> class SmallVectorImpl;
/// Traits used by SSAUpdaterImpl to adapt an SSA updater implementation.
template<typename T> class SSAUpdaterTraits;

/// Helper class for SSA formation on virtual registers defined in multiple
/// blocks.
///
/// This is used when code duplication or another unstructured transformation
/// wants to rewrite a set of uses of one vreg with uses of a set of vregs.
class MachineSSAUpdater {
  friend class SSAUpdaterTraits<MachineSSAUpdater>;

private:
  /// AvailableVals - This keeps track of which value to use on a per-block
  /// basis.  When we insert PHI nodes, we keep track of them here.
  //typedef DenseMap<MachineBasicBlock*, Register> AvailableValsTy;
  void *AV = nullptr;

  /// Register class or bank and LLT of current virtual register.
  MachineRegisterInfo::VRegAttrs RegAttrs;

  /// InsertedPHIs - If this is non-null, the MachineSSAUpdater adds all PHI
  /// nodes that it creates to the vector.
  SmallVectorImpl<MachineInstr*> *InsertedPHIs;

  const TargetInstrInfo *TII = nullptr;
  MachineRegisterInfo *MRI = nullptr;

public:
  /// Construct an updater for \p MF.
  ///
  /// If \p NewPHI is specified, it will be filled in with all PHI Nodes created
  /// by rewriting.
  ///
  /// \param MF Machine function whose SSA form is updated.
  /// \param NewPHI Optional list that receives newly inserted PHI nodes.
  LLVM_ABI explicit MachineSSAUpdater(
      MachineFunction &MF, SmallVectorImpl<MachineInstr *> *NewPHI = nullptr);
  /// Deleted copy constructor; MachineSSAUpdater is not copyable.
  ///
  /// \param Other Unused; copy construction is deleted.
  MachineSSAUpdater(const MachineSSAUpdater &Other) = delete;
  /// Deleted copy assignment; MachineSSAUpdater cannot be copy-assigned.
  ///
  /// \param Other Unused; copy assignment is deleted.
  MachineSSAUpdater &operator=(const MachineSSAUpdater &Other) = delete;
  /// Destroy the MachineSSA updater.
  LLVM_ABI ~MachineSSAUpdater();

  /// Reset this object to get ready for a new set of SSA updates.
  ///
  /// \param V Virtual register whose uses will be rewritten.
  LLVM_ABI void Initialize(Register V);

  /// Indicate that a rewritten value is available at the end of the specified
  /// block with the specified value.
  ///
  /// \param BB Block in which the rewritten value is available.
  /// \param V Value available in \p BB.
  LLVM_ABI void AddAvailableValue(MachineBasicBlock *BB, Register V);

  /// Return true if the MachineSSAUpdater already has a value for the specified
  /// block.
  ///
  /// \param BB Block to query for an available value.
  /// \return True if a value is already available for \p BB.
  LLVM_ABI bool HasValueForBlock(MachineBasicBlock *BB) const;

  /// Construct SSA form, materializing a value that is live at the end of the
  /// specified block.
  ///
  /// \param BB Block at whose end the live value is materialized.
  /// \return The register that holds the live value at the end of \p BB.
  LLVM_ABI Register GetValueAtEndOfBlock(MachineBasicBlock *BB);

  /// Construct SSA form, materializing a value live in the middle of a block.
  ///
  /// If ExistingValueOnly is true then this will only return an existing value
  /// or $noreg; otherwise new instructions may be inserted to materialize a
  /// value.
  ///
  /// GetValueInMiddleOfBlock is the same as GetValueAtEndOfBlock except in one
  /// important case: if there is a definition of the rewritten value after the
  /// 'use' in BB.  Consider code like this:
  ///
  ///      X1 = ...
  ///   SomeBB:
  ///      use(X)
  ///      X2 = ...
  ///      br Cond, SomeBB, OutBB
  ///
  /// In this case, there are two values (X1 and X2) added to the AvailableVals
  /// set by the client of the rewriter, and those values are both live out of
  /// their respective blocks.  However, the use of X happens in the *middle* of
  /// a block.  Because of this, we need to insert a new PHI node in SomeBB to
  /// merge the appropriate values, and this value isn't live out of the block.
  ///
  /// \param BB Block in whose middle the live value is materialized.
  /// \param ExistingValueOnly If true, only return an existing value or $noreg;
  ///        do not insert new instructions.
  /// \return The register live in the middle of \p BB, or $noreg if
  ///         ExistingValueOnly is true and no suitable value exists.
  LLVM_ABI Register GetValueInMiddleOfBlock(MachineBasicBlock *BB,
                                            bool ExistingValueOnly = false);

  /// Rewrite a use of the symbolic value.
  ///
  /// This handles PHI nodes, which use their value in the corresponding
  /// predecessor. Note that this will not work if the use is supposed to be
  /// rewritten to a value defined in the same block as the use, but above it.
  /// Any 'AddAvailableValue's added for the use's block will be considered to
  /// be below it.
  ///
  /// \param U Use of the symbolic value to rewrite.
  LLVM_ABI void RewriteUse(MachineOperand &U);

private:
  // If ExistingValueOnly is true, will not create any new instructions. Used
  // for debug values, which cannot modify Codegen.
  Register GetValueAtEndOfBlockInternal(MachineBasicBlock *BB,
                                        bool ExistingValueOnly = false);
};

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINESSAUPDATER_H
