//===-- IndirectCallVisitor.h - indirect call visitor ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements defines a visitor class and a helper function that find
// all indirect call-sites in a function.

#ifndef LLVM_ANALYSIS_INDIRECTCALLVISITOR_H
#define LLVM_ANALYSIS_INDIRECTCALLVISITOR_H

#include "llvm/IR/InstVisitor.h"
#include <vector>

namespace llvm {
/// InstVisitor that collects indirect calls or vtable address instructions.
///
/// Finds indirect calls or instructions that give a vtable value, depending on
/// Type.
struct PGOIndirectCallVisitor : public InstVisitor<PGOIndirectCallVisitor> {
  /// Kind of instruction this visitor collects while walking a function.
  enum class InstructionType {
    /// Collect indirect call sites.
    kIndirectCall = 0,
    /// Collect instructions that produce a vtable address.
    kVTableVal = 1,
  };
  /// Indirect call sites discovered during the visit.
  std::vector<CallBase *> IndirectCalls;
  /// Vtable address instructions discovered when collecting \c kVTableVal.
  std::vector<Instruction *> ProfiledAddresses;
  /// Construct a visitor that collects instructions of the given \p Type.
  /// @param Type Whether to collect indirect calls or vtable addresses.
  PGOIndirectCallVisitor(InstructionType Type) : Type(Type) {}

  /// Try to recover the vtable load feeding an indirect call.
  ///
  /// Given an indirect call instruction, try to find the following pattern:
  ///
  /// %vtable = load ptr, ptr %obj
  /// %vfn = getelementptr inbounds ptr, ptr %vtable, i64 1
  /// %2 = load ptr, ptr %vfn
  /// $call = tail call i32 %2
  ///
  /// A heuristic is used to find the address feeding instructions.
  /// @param CB Indirect call whose called operand may load from a vtable.
  /// @return The vtable address instruction, or nullptr if not recognized.
  static Instruction *tryGetVTableInstruction(CallBase *CB) {
    assert(CB != nullptr && "Caller guaranteed");
    if (!CB->isIndirectCall())
      return nullptr;

    LoadInst *LI = dyn_cast<LoadInst>(CB->getCalledOperand());
    if (LI != nullptr) {
      Value *FuncPtr = LI->getPointerOperand(); // GEP (or bitcast)
      Value *VTablePtr = FuncPtr->stripInBoundsConstantOffsets();
      // FIXME: Add support in the frontend so LLVM type intrinsics are
      // emitted without LTO. This way, added intrinsics could filter
      // non-vtable instructions and reduce instrumentation overhead.
      // Since a non-vtable profiled address is not within the address
      // range of vtable objects, it's stored as zero in indexed profiles.
      // A pass that looks up symbol with an zero hash will (almost) always
      // find nullptr and skip the actual transformation (e.g., comparison
      // of symbols). So the performance overhead from non-vtable profiled
      // address is negligible if exists at all. Comparing loaded address
      // with symbol address guarantees correctness.
      if (VTablePtr != nullptr && isa<Instruction>(VTablePtr))
        return cast<Instruction>(VTablePtr);
    }
    return nullptr;
  }

  /// Record an indirect call, and optionally its vtable address.
  /// @param Call Call site to inspect for an indirect callee.
  void visitCallBase(CallBase &Call) {
    if (Call.isIndirectCall()) {
      IndirectCalls.push_back(&Call);

      if (Type != InstructionType::kVTableVal)
        return;

      Instruction *VPtr =
          PGOIndirectCallVisitor::tryGetVTableInstruction(&Call);
      if (VPtr)
        ProfiledAddresses.push_back(VPtr);
    }
  }

private:
  InstructionType Type;
};

/// Return all indirect call sites in function \p F.
/// @param F Function whose instructions are scanned.
/// @return Indirect call sites found in \p F.
inline std::vector<CallBase *> findIndirectCalls(Function &F) {
  PGOIndirectCallVisitor ICV(
      PGOIndirectCallVisitor::InstructionType::kIndirectCall);
  ICV.visit(F);
  return ICV.IndirectCalls;
}

/// Return vtable address instructions feeding indirect calls in \p F.
/// @param F Function whose instructions are scanned.
/// @return Vtable address instructions found in \p F.
inline std::vector<Instruction *> findVTableAddrs(Function &F) {
  PGOIndirectCallVisitor ICV(
      PGOIndirectCallVisitor::InstructionType::kVTableVal);
  ICV.visit(F);
  return ICV.ProfiledAddresses;
}

} // namespace llvm

#endif
