//===-- CodeGen/MachineJumpTableInfo.h - Abstract Jump Tables  --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The MachineJumpTableInfo class keeps track of jump tables referenced by
// lowered switch instructions in the MachineFunction.
//
// Instructions reference the address of these jump tables through the use of
// MO_JumpTableIndex values.  When emitting assembly or machine code, these
// virtual address references are converted to refer to the address of the
// function jump tables.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEJUMPTABLEINFO_H
#define LLVM_CODEGEN_MACHINEJUMPTABLEINFO_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/Printable.h"
#include <cassert>
#include <vector>

namespace llvm {

class MachineBasicBlock;
class DataLayout;
class raw_ostream;
enum class MachineFunctionDataHotness;

/// MachineJumpTableEntry - One jump table in the jump table info.
///
struct MachineJumpTableEntry {
  /// MBBs - The vector of basic blocks from which to create the jump table.
  std::vector<MachineBasicBlock*> MBBs;

  /// The hotness of MJTE is inferred from the hotness of the source basic
  /// block(s) that reference it.
  MachineFunctionDataHotness Hotness;

  /// Construct a jump table entry from the given destination blocks.
  ///
  /// \param M Basic blocks that form the jump table destinations.
  LLVM_ABI explicit MachineJumpTableEntry(
      const std::vector<MachineBasicBlock *> &M);
};

/// Keeps track of jump tables referenced by lowered switch instructions.
///
/// Instructions reference the address of these jump tables through
/// MO_JumpTableIndex values.  When emitting assembly or machine code, these
/// virtual address references are converted to refer to the address of the
/// function jump tables.
class MachineJumpTableInfo {
public:
  /// JTEntryKind - This enum indicates how each entry of the jump table is
  /// represented and emitted.
  enum JTEntryKind {
    /// EK_BlockAddress - Each entry is a plain address of block, e.g.:
    ///     .word LBB123
    EK_BlockAddress,

    /// EK_GPRel64BlockAddress - Each entry is an address of block, encoded
    /// with a relocation as gp-relative, e.g.:
    ///     .gpdword LBB123
    EK_GPRel64BlockAddress,

    /// EK_GPRel32BlockAddress - Each entry is an address of block, encoded
    /// with a relocation as gp-relative, e.g.:
    ///     .gprel32 LBB123
    EK_GPRel32BlockAddress,

    /// EK_LabelDifference32 - Each entry is the address of the block minus
    /// the address of the jump table.  This is used for PIC jump tables where
    /// gprel32 is not supported.  e.g.:
    ///      .word LBB123 - LJTI1_2
    /// If the .set directive is supported, this is emitted as:
    ///      .set L4_5_set_123, LBB123 - LJTI1_2
    ///      .word L4_5_set_123
    EK_LabelDifference32,

    /// EK_LabelDifference64 - Each entry is the address of the block minus
    /// the address of the jump table.  This is used for PIC jump tables where
    /// gprel64 is not supported.  e.g.:
    ///      .quad LBB123 - LJTI1_2
    EK_LabelDifference64,

    /// EK_Inline - Jump table entries are emitted inline at their point of
    /// use. It is the responsibility of the target to emit the entries.
    EK_Inline,

    /// EK_Custom32 - Each entry is a 32-bit value that is custom lowered by the
    /// TargetLowering::LowerCustomJumpTableEntry hook.
    EK_Custom32
  };

private:
  JTEntryKind EntryKind;
  std::vector<MachineJumpTableEntry> JumpTables;
public:
  /// Construct jump table info with the given entry encoding kind.
  ///
  /// \param Kind How each jump table entry is represented and emitted.
  explicit MachineJumpTableInfo(JTEntryKind Kind): EntryKind(Kind) {}

  /// Return how each jump table entry is represented and emitted.
  ///
  /// \return How each jump table entry is represented and emitted.
  JTEntryKind getEntryKind() const { return EntryKind; }

  /// getEntrySize - Return the size of each entry in the jump table.
  ///
  /// \param TD Data layout used to compute the entry size.
  /// \return Size of each entry in the jump table.
  LLVM_ABI unsigned getEntrySize(const DataLayout &TD) const;
  /// getEntryAlignment - Return the alignment of each entry in the jump table.
  ///
  /// \param TD Data layout used to compute the entry alignment.
  /// \return Alignment of each entry in the jump table.
  LLVM_ABI unsigned getEntryAlignment(const DataLayout &TD) const;

  /// createJumpTableIndex - Create a new jump table.
  ///
  /// \param DestBBs Destination basic blocks that form the jump table.
  /// \return Index of the newly created jump table.
  LLVM_ABI unsigned
  createJumpTableIndex(const std::vector<MachineBasicBlock *> &DestBBs);

  /// isEmpty - Return true if there are no jump tables.
  ///
  /// \return True if there are no jump tables.
  bool isEmpty() const { return JumpTables.empty(); }

  /// Return the jump tables owned by this function.
  ///
  /// \return The jump tables owned by this function.
  const std::vector<MachineJumpTableEntry> &getJumpTables() const {
    return JumpTables;
  }

  /// Update the hotness of the jump table entry at index \p JTI.
  ///
  /// \param JTI Index of the jump table entry to update.
  /// \param Hotness New hotness value for the entry.
  /// \return True if the hotness was updated.
  LLVM_ABI bool updateJumpTableEntryHotness(size_t JTI,
                                            MachineFunctionDataHotness Hotness);

  /// RemoveJumpTable - Mark the specific index as being dead.  This will
  /// prevent it from being emitted.
  ///
  /// \param Idx Index of the jump table to mark as dead.
  void RemoveJumpTable(unsigned Idx) {
    JumpTables[Idx].MBBs.clear();
  }

  /// RemoveMBBFromJumpTables - If MBB is present in any jump tables, remove it.
  ///
  /// \param MBB Basic block to remove from all jump tables.
  /// \return True if \p MBB was removed from at least one jump table.
  LLVM_ABI bool RemoveMBBFromJumpTables(MachineBasicBlock *MBB);

  /// ReplaceMBBInJumpTables - If Old is the target of any jump tables, update
  /// the jump tables to branch to New instead.
  ///
  /// \param Old Basic block currently targeted by jump tables.
  /// \param New Basic block that should replace \p Old as the target.
  /// \return True if any jump table was updated.
  LLVM_ABI bool ReplaceMBBInJumpTables(MachineBasicBlock *Old,
                                       MachineBasicBlock *New);

  /// ReplaceMBBInJumpTable - If Old is a target of the jump tables, update
  /// the jump table to branch to New instead.
  ///
  /// \param Idx Index of the jump table to update.
  /// \param Old Basic block currently targeted by the jump table.
  /// \param New Basic block that should replace \p Old as the target.
  /// \return True if the jump table was updated.
  LLVM_ABI bool ReplaceMBBInJumpTable(unsigned Idx, MachineBasicBlock *Old,
                                      MachineBasicBlock *New);

  /// print - Used by the MachineFunction printer to print information about
  /// jump tables.  Implemented in MachineFunction.cpp
  ///
  /// \param OS Stream to print to.
  LLVM_ABI void print(raw_ostream &OS) const;

  /// dump - Call to stderr.
  ///
  LLVM_ABI void dump() const;
};


/// Prints a jump table entry reference.
///
/// The format is:
///   %jump-table.5       - a jump table entry with index == 5.
///
/// Usage: OS << printJumpTableEntryReference(Idx) << '\n';
///
/// \param Idx Jump table index to format as a printable reference.
/// \return A Printable that formats the jump table entry reference.
LLVM_ABI Printable printJumpTableEntryReference(unsigned Idx);

} // End llvm namespace

#endif
