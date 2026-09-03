//===- PHITransAddr.h - PHI Translation for Addresses -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the PHITransAddr class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_PHITRANSADDR_H
#define LLVM_ANALYSIS_PHITRANSADDR_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Instruction.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class AssumptionCache;
class DominatorTree;
class DataLayout;
class TargetLibraryInfo;

/// Holds a normal address or a select-dependent address pair.
///
/// Storage of either a normal Value address, or a select condition together
/// with a pair of addresses for the "true" and "false" variant of a
/// select-dependent address.  If the addresses are not present (both null), V
/// is a normal address; otherwise V is a select condition and the pair holds
/// the "true" and "false" addresses.
class SelectAddr {
public:
  /// Pair of "true" and "false" addresses for a select-dependent expression.
  using SelectAddrs = std::pair<Value *, Value *>;

  /// Construct a normal (non-select) address.
  /// @param Addr Address value to store.
  SelectAddr(Value *Addr) : V(Addr), Addrs(nullptr, nullptr) {}
  /// Construct a select-dependent address from a condition and address pair.
  /// @param Cond Select condition; must be non-null.
  /// @param Addrs True/false address pair; both must be non-null.
  SelectAddr(Value *Cond, SelectAddrs Addrs) : V(Cond), Addrs(Addrs) {
    assert(Cond && "Condition must be present");
    assert(hasSelectAddrs() && "Addrs must be present");
  }

  /// Return true if this stores a select condition with true/false addresses.
  /// @return True if both true and false select addresses are present.
  bool hasSelectAddrs() const { return Addrs.first && Addrs.second; }

  /// Return the normal address value.
  /// @return The stored non-select address value.
  Value *getAddr() const {
    assert(!hasSelectAddrs() && "this is a select address");
    return V;
  }

  /// Return the select condition and the true/false address pair.
  /// @return Pair of the select condition and the true/false address pair.
  std::pair<Value *, SelectAddrs> getSelectCondAndAddrs() const {
    assert(hasSelectAddrs() && "this is not a select address");
    return {V, Addrs};
  }

private:
  Value *V;
  SelectAddrs Addrs;
};

/// An address value that tracks and handles PHI translation.
///
/// As we walk "up" the CFG through predecessors, we need to ensure that the
/// address we're tracking is kept up to date.  For example, if we're analyzing
/// an address of "&A[i]" and walk through the definition of 'i' which is a PHI
/// node, we *must* phi translate i to get "&A[j]" or else we will analyze an
/// incorrect pointer in the predecessor block.
///
/// This is designed to be a relatively small object that lives on the stack and
/// is copyable.
class PHITransAddr {
  /// Addr - The actual address we're analyzing.
  Value *Addr;

  /// The DataLayout we are playing with.
  const DataLayout &DL;

  /// TLI - The target library info if known, otherwise null.
  const TargetLibraryInfo *TLI = nullptr;

  /// A cache of \@llvm.assume calls used by SimplifyInstruction.
  AssumptionCache *AC;

  /// InstInputs - The inputs for our symbolic address.
  SmallVector<Instruction*, 4> InstInputs;

public:
  /// Construct a PHI-translatable address from \p Addr.
  /// @param Addr Address value to track.
  /// @param DL Data layout used for address analysis.
  /// @param AC Optional assumption cache used by SimplifyInstruction.
  PHITransAddr(Value *Addr, const DataLayout &DL, AssumptionCache *AC)
      : Addr(Addr), DL(DL), AC(AC) {
    // If the address is an instruction, the whole thing is considered an input.
    addAsInput(Addr);
  }

  /// Return the current address being tracked.
  /// @return The address value currently being analyzed.
  Value *getAddr() const { return Addr; }

  /// Return the select condition this address depends on, if any.
  ///
  /// If the address expression depends on a select instruction (possibly
  /// through casts or GEPs), return that select's condition.  Otherwise return
  /// nullptr.  This is used to drive translation of both sides of a
  /// select-dependent address (see the \p Cond overload of translateValue).
  /// @return The select condition, or nullptr if the address is not
  /// select-dependent.
  LLVM_ABI Value *getSelectCondition() const;

  /// Return true if moving from \p BB to its predecessors needs PHI translation.
  /// @param BB Block whose definitions may require PHI translation.
  /// @return True if PHI translation is needed when leaving \p BB.
  bool needsPHITranslationFromBlock(BasicBlock *BB) const {
    // We do need translation if one of our input instructions is defined in
    // this block.
    return any_of(InstInputs, [BB](const auto &InstInput) {
      return InstInput->getParent() == BB;
    });
  }

  /// Return true if PHI translation of this address may succeed.
  ///
  /// If this needs PHI translation, return true if we have some hope of doing
  /// it.  This should be used as a filter to avoid calling PHITranslateValue in
  /// hopeless situations.
  /// @return True if PHI translation may succeed.
  LLVM_ABI bool isPotentiallyPHITranslatable() const;

  /// PHI-translate the current address from \p CurBB to \p PredBB.
  ///
  /// Updates our state to reflect any needed changes.  If \p MustDominate is
  /// true, the translated value must dominate \p PredBB.
  /// @param CurBB Block containing the current address.
  /// @param PredBB Predecessor block to translate into.
  /// @param DT Optional dominator tree used when \p MustDominate is true.
  /// @param MustDominate When true, require the result to dominate \p PredBB.
  /// @return The PHI-translated address value.
  LLVM_ABI Value *translateValue(BasicBlock *CurBB, BasicBlock *PredBB,
                                 const DominatorTree *DT, bool MustDominate);

  /// PHI-translate the address and both sides of a matching select.
  ///
  /// PHI translate the current address from \p CurBB to \p PredBB, and if the
  /// resulting address depends on a select instruction with condition \p Cond,
  /// translate both the "true" and the "false" side. Returns a pair of
  /// addresses (true, false); either may be null on failure.
  /// @param CurBB Block containing the current address.
  /// @param PredBB Predecessor block to translate into.
  /// @param DT Optional dominator tree used during translation.
  /// @param Cond Select condition that must match for dual-side translation.
  /// @return Pair of true/false translated addresses; either may be null on
  /// failure.
  LLVM_ABI SelectAddr::SelectAddrs translateValue(BasicBlock *CurBB,
                                                  BasicBlock *PredBB,
                                                  const DominatorTree *DT,
                                                  Value *Cond);

  /// PHI-translate this value into \p PredBB, inserting computation if needed.
  ///
  /// All newly created instructions are added to the NewInsts list.  This
  /// returns null on failure.
  /// @param CurBB Block containing the current address.
  /// @param PredBB Predecessor block to translate into.
  /// @param DT Dominator tree used during translation.
  /// @param NewInsts Receives any instructions inserted for the translation.
  /// @return The PHI-translated value, or null on failure.
  LLVM_ABI Value *
  translateWithInsertion(BasicBlock *CurBB, BasicBlock *PredBB,
                         const DominatorTree &DT,
                         SmallVectorImpl<Instruction *> &NewInsts);

  /// Dump this PHITransAddr to stderr for debugging.
  LLVM_ABI void dump() const;

  /// verify - Check internal consistency of this data structure.  If the
  /// structure is valid, it returns true.  If invalid, it prints errors and
  /// returns false.
  /// @return True if the structure is valid; false if invalid (with errors
  /// printed).
  LLVM_ABI bool verify() const;

private:
  Value *translateSubExpr(Value *V, BasicBlock *CurBB, BasicBlock *PredBB,
                          const DominatorTree *DT, Value *Cond = nullptr,
                          bool CondVal = false);

  /// insertTranslatedSubExpr - Insert a computation of the PHI translated
  /// version of 'V' for the edge PredBB->CurBB into the end of the PredBB
  /// block.  All newly created instructions are added to the NewInsts list.
  /// This returns null on failure.
  ///
  Value *insertTranslatedSubExpr(Value *InVal, BasicBlock *CurBB,
                                 BasicBlock *PredBB, const DominatorTree &DT,
                                 SmallVectorImpl<Instruction *> &NewInsts);

  /// addAsInput - If the specified value is an instruction, add it as an input.
  Value *addAsInput(Value *V) {
    // If V is an instruction, it is now an input.
    if (Instruction *VI = dyn_cast<Instruction>(V))
      InstInputs.push_back(VI);
    return V;
  }
};

} // end namespace llvm

#endif
