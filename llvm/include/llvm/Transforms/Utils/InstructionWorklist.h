//=== InstructionWorklist.h - Worklist for InstCombine & others -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_INSTRUCTIONWORKLIST_H
#define LLVM_TRANSFORMS_UTILS_INSTRUCTIONWORKLIST_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Instruction.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

/// InstructionWorklist - This is the worklist management logic for
/// InstCombine and other simplification passes.
class InstructionWorklist {
  SmallVector<Instruction *, 256> Worklist;
  DenseMap<Instruction *, unsigned> WorklistMap;
  /// These instructions will be added in reverse order after the current
  /// combine has finished. This means that these instructions will be visited
  /// in the order they have been added.
  SmallSetVector<Instruction *, 16> Deferred;

public:
  /// Construct an empty instruction worklist.
  InstructionWorklist() = default;

  /// Move-construct an instruction worklist.
  ///
  /// \param Other Worklist to move from.
  InstructionWorklist(InstructionWorklist &&Other) = default;

  /// Move-assign an instruction worklist.
  ///
  /// \param Other Worklist to move from.
  /// \return Reference to this worklist after the move.
  InstructionWorklist &operator=(InstructionWorklist &&Other) = default;

  /// Return true if both the worklist and deferred set are empty.
  ///
  /// \return True if both the worklist and deferred set are empty.
  bool isEmpty() const { return Worklist.empty() && Deferred.empty(); }

  /// Add instruction to the worklist.
  ///
  /// Instructions will be visited in the order they are added.
  /// You likely want to use this method.
  ///
  /// \param I Instruction to schedule for a later visit.
  void add(Instruction *I) {
    if (Deferred.insert(I))
      LLVM_DEBUG(dbgs() << "ADD DEFERRED: " << *I << '\n');
  }

  /// Add value to the worklist if it is an instruction.
  ///
  /// Instructions will be visited in the order they are added.
  ///
  /// \param V Value to enqueue when it is an instruction.
  void addValue(Value *V) {
    if (Instruction *I = dyn_cast<Instruction>(V))
      add(I);
  }

  /// Push the instruction onto the worklist stack.
  ///
  /// Instructions that have been added first will be visited last.
  ///
  /// \param I Instruction to push onto the worklist stack.
  void push(Instruction *I) {
    assert(I);
    assert(I->getParent() && "Instruction not inserted yet?");

    if (WorklistMap.insert(std::make_pair(I, Worklist.size())).second) {
      LLVM_DEBUG(dbgs() << "ADD: " << *I << '\n');
      Worklist.push_back(I);
    }
  }

  /// Push \p V onto the worklist stack if it is an instruction.
  ///
  /// \param V Value to push when it is an instruction.
  void pushValue(Value *V) {
    if (Instruction *I = dyn_cast<Instruction>(V))
      push(I);
  }

  /// Pop one deferred instruction, or return null if none remain.
  ///
  /// \return A deferred instruction, or null if none remain.
  Instruction *popDeferred() {
    if (Deferred.empty())
      return nullptr;
    return Deferred.pop_back_val();
  }

  /// Reserve capacity for about \p Size instructions in the worklist.
  ///
  /// \param Size Expected number of instructions to hold.
  void reserve(size_t Size) {
    Worklist.reserve(Size + 16);
    WorklistMap.reserve(Size);
  }

  /// Remove \p I from the worklist if it exists.
  ///
  /// \param I Instruction to remove from the worklist and deferred set.
  void remove(Instruction *I) {
    auto It = WorklistMap.find(I);
    if (It != WorklistMap.end()) {
      // Don't bother moving everything down, just null out the slot.
      Worklist[It->second] = nullptr;
      WorklistMap.erase(It);
    }

    Deferred.remove(I);
  }

  /// Remove and return one instruction from the worklist, or null if empty.
  ///
  /// \return An instruction from the worklist, or null if empty.
  Instruction *removeOne() {
    if (Worklist.empty())
      return nullptr;
    Instruction *I = Worklist.pop_back_val();
    WorklistMap.erase(I);
    return I;
  }

  /// When an instruction is simplified, add all users of the instruction
  /// to the work lists because they might get more simplified now.
  ///
  /// \param I Instruction whose users should be pushed onto the worklist.
  void pushUsersToWorkList(Instruction &I) {
    for (User *U : I.users())
      push(cast<Instruction>(U));
  }

  /// Revisit \p V after its use-count has been decremented.
  ///
  /// Should be called *after* decrementing the use-count on V.
  ///
  /// \param V Value whose remaining uses may now simplify further.
  void handleUseCountDecrement(Value *V) {
    if (auto *I = dyn_cast<Instruction>(V)) {
      add(I);
      // Many folds have one-use limitations. If there's only one use left,
      // revisit that use.
      if (I->hasOneUse())
        add(cast<Instruction>(*I->user_begin()));
    }
  }

  /// Check that the worklist is empty and nuke the backing store for the map.
  void zap() {
    assert(WorklistMap.empty() && "Worklist empty, but map not?");
    assert(Deferred.empty() && "Deferred instructions left over");

    // Do an explicit clear, this shrinks the map if needed.
    WorklistMap.clear();
  }
};

} // end namespace llvm.

#endif
