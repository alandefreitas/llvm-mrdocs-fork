//===-- InstructionPrecedenceTracking.h -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Implements a class that is able to define some instructions as "special"
// (e.g. as having implicit control flow, or writing memory, or having another
// interesting property) and then efficiently answers queries of the types:
// 1. Are there any special instructions in the block of interest?
// 2. Return first of the special instructions in the given block;
// 3. Check if the given instruction is preceeded by the first special
//    instruction in the same block.
// The class provides caching that allows to answer these queries quickly. The
// user must make sure that the cached data is invalidated properly whenever
// a content of some tracked block is changed.
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_INSTRUCTIONPRECEDENCETRACKING_H
#define LLVM_ANALYSIS_INSTRUCTIONPRECEDENCETRACKING_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class BasicBlock;
class Instruction;

/// Tracks "special" instructions and answers precedence queries efficiently.
///
/// Defines some instructions as special (e.g. having implicit control flow, or
/// writing memory, or having another interesting property) and then efficiently
/// answers queries of the types:
/// 1. Are there any special instructions in the block of interest?
/// 2. Return first of the special instructions in the given block;
/// 3. Check if the given instruction is preceeded by the first special
///    instruction in the same block.
/// The class provides caching that allows to answer these queries quickly. The
/// user must make sure that the cached data is invalidated properly whenever
/// a content of some tracked block is changed.
class InstructionPrecedenceTracking {
  // Maps a block to the topmost special instruction in it. If the value is
  // nullptr, it means that it is known that this block does not contain any
  // special instructions.
  DenseMap<const BasicBlock *, const Instruction *> FirstSpecialInsts;

#ifndef NDEBUG
  /// Asserts that the cached info for \p BB is up-to-date. This helps to catch
  /// the usage error of accessing a block without properly invalidating after a
  /// previous transform.
  void validate(const BasicBlock *BB) const;

  /// Asserts whether or not the contents of this tracking is up-to-date. This
  /// helps to catch the usage error of accessing a block without properly
  /// invalidating after a previous transform.
  void validateAll() const;
#endif

protected:
  /// Returns the topmost special instruction from the block \p BB. Returns
  /// nullptr if there is no special instructions in the block.
  /// \param BB Basic block to query.
  /// \return The first special instruction in \p BB, or nullptr if none.
  LLVM_ABI const Instruction *getFirstSpecialInstruction(const BasicBlock *BB);

  /// Returns true iff at least one instruction from the basic block \p BB is
  /// special.
  /// \param BB Basic block to query.
  /// \return True if \p BB contains at least one special instruction.
  LLVM_ABI bool hasSpecialInstructions(const BasicBlock *BB);

  /// Returns true iff the first special instruction of \p Insn's block exists
  /// and dominates \p Insn.
  /// \param Insn Instruction to check for a preceding special instruction.
  /// \return True if a special instruction from the same block precedes \p Insn.
  LLVM_ABI bool isPreceededBySpecialInstruction(const Instruction *Insn);

  /// Returns whether \p Insn is considered special and needs to be tracked.
  ///
  /// Implementing this method in children classes allows to implement tracking
  /// of implicit control flow, memory writing instructions or any other kinds
  /// of instructions we might be interested in.
  /// \param Insn Instruction to classify.
  /// \return True if \p Insn is considered special and needs to be tracked.
  virtual bool isSpecialInstruction(const Instruction *Insn) const = 0;

  /// Destroys this instruction precedence tracking.
  virtual ~InstructionPrecedenceTracking() = default;

public:
  /// Updates caches for an instruction about to be inserted into a block.
  ///
  /// Notifies this tracking that we are going to insert a new instruction \p
  /// Inst to the basic block \p BB. It makes all necessary updates to internal
  /// caches to keep them consistent.
  /// \param Inst Instruction that will be inserted.
  /// \param BB Basic block that will contain \p Inst.
  LLVM_ABI void insertInstructionTo(const Instruction *Inst,
                                    const BasicBlock *BB);

  /// Notifies this tracking that we are going to remove the instruction \p Inst
  /// It makes all necessary updates to internal caches to keep them consistent.
  /// \param Inst Instruction that will be removed.
  LLVM_ABI void removeInstruction(const Instruction *Inst);

  /// Updates caches before replacing all uses of an instruction.
  ///
  /// Notifies this tracking that we are going to replace all uses of \p Inst.
  /// It makes all necessary updates to internal caches to keep them consistent.
  /// Should typically be called before a RAUW.
  /// \param Inst Instruction whose uses will be replaced.
  LLVM_ABI void removeUsersOf(const Instruction *Inst);

  /// Invalidates all information from this tracking.
  LLVM_ABI void clear();
};

/// Tracks instructions with implicit control flow within basic blocks.
///
/// These are instructions that may not pass execution to their successors. For
/// example, throwing calls and guards do not always do this. If we need to know
/// for sure that some instruction is guaranteed to execute if the given block
/// is reached, then we need to make sure that there is no implicit control flow
/// instruction (ICFI) preceding it. For example, this check is required if we
/// perform PRE moving non-speculable instruction to other place.
class LLVM_ABI ImplicitControlFlowTracking
    : public InstructionPrecedenceTracking {
public:
  /// Returns the topmost instruction with implicit control flow from the given
  /// basic block. Returns nullptr if there is no such instructions in the block.
  /// \param BB Basic block to query.
  /// \return The first ICFI in \p BB, or nullptr if none.
  const Instruction *getFirstICFI(const BasicBlock *BB) {
    return getFirstSpecialInstruction(BB);
  }

  /// Returns true if at least one instruction from the given basic block has
  /// implicit control flow.
  /// \param BB Basic block to query.
  /// \return True if \p BB contains an instruction with implicit control flow.
  bool hasICF(const BasicBlock *BB) {
    return hasSpecialInstructions(BB);
  }

  /// Returns true if the first ICFI of Insn's block exists and dominates Insn.
  /// \param Insn Instruction to check for a preceding ICFI.
  /// \return True if an ICFI from the same block dominates \p Insn.
  bool isDominatedByICFIFromSameBlock(const Instruction *Insn) {
    return isPreceededBySpecialInstruction(Insn);
  }

  /// Returns whether \p Insn has implicit control flow.
  /// \param Insn Instruction to classify.
  /// \return True if \p Insn has implicit control flow.
  bool isSpecialInstruction(const Instruction *Insn) const override;
};

/// Tracks instructions that may write to memory within basic blocks.
class LLVM_ABI MemoryWriteTracking : public InstructionPrecedenceTracking {
public:
  /// Returns the topmost instruction that may write memory from the given
  /// basic block. Returns nullptr if there is no such instructions in the block.
  /// \param BB Basic block to query.
  /// \return The first memory-writing instruction in \p BB, or nullptr if none.
  const Instruction *getFirstMemoryWrite(const BasicBlock *BB) {
    return getFirstSpecialInstruction(BB);
  }

  /// Returns true if at least one instruction from the given basic block may
  /// write memory.
  /// \param BB Basic block to query.
  /// \return True if \p BB contains an instruction that may write memory.
  bool mayWriteToMemory(const BasicBlock *BB) {
    return hasSpecialInstructions(BB);
  }

  /// Returns true if the first memory writing instruction of Insn's block
  /// exists and dominates Insn.
  /// \param Insn Instruction to check for a preceding memory write.
  /// \return True if a memory write from the same block dominates \p Insn.
  bool isDominatedByMemoryWriteFromSameBlock(const Instruction *Insn) {
    return isPreceededBySpecialInstruction(Insn);
  }

  /// Returns whether \p Insn may write to memory.
  /// \param Insn Instruction to classify.
  /// \return True if \p Insn may write to memory.
  bool isSpecialInstruction(const Instruction *Insn) const override;
};

} // llvm

#endif // LLVM_ANALYSIS_INSTRUCTIONPRECEDENCETRACKING_H
