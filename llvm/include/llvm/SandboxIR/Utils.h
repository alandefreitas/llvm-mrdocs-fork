//===- Utils.h --------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SANDBOXIR_UTILS_H
#define LLVM_SANDBOXIR_UTILS_H

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/LoopAccessAnalysis.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Verifier.h"
#include "llvm/SandboxIR/Function.h"
#include "llvm/SandboxIR/Instruction.h"
#include <optional>

namespace llvm::sandboxir {

/// Collector for SandboxIR-related convenience functions that don't belong in
/// other classes.
class Utils {
public:
  /// Return the expected type of \p V.
  ///
  /// For most Values this is equivalent to getType, but for stores returns the
  /// stored type, rather than void, and for ReturnInsts returns the returned
  /// type.
  /// \param V Value whose expected type is requested.
  /// \Returns The expected type of \p V.
  static Type *getExpectedType(const Value *V) {
    if (auto *I = dyn_cast<Instruction>(V)) {
      // A Return's value operand can be null if it returns void.
      if (auto *RI = dyn_cast<ReturnInst>(I)) {
        if (RI->getReturnValue() == nullptr)
          return RI->getType();
      }
      return getExpectedValue(I)->getType();
    }
    return V->getType();
  }

  /// Return the expected Value for this instruction.
  ///
  /// For most instructions, this is the instruction itself, but for stores
  /// returns the stored operand, and for ReturnInstructions returns the
  /// returned value.
  /// \param I Instruction whose expected value is requested.
  /// \Returns The expected Value for \p I.
  static Value *getExpectedValue(const Instruction *I) {
    if (auto *SI = dyn_cast<StoreInst>(I))
      return SI->getValueOperand();
    if (auto *RI = dyn_cast<ReturnInst>(I))
      return RI->getReturnValue();
    return const_cast<Instruction *>(I);
  }

  /// Return the base Value for load or store instruction \p LSI.
  /// \param LSI Load or store instruction.
  /// \Returns The base Value of the memory instruction.
  template <typename LoadOrStoreT>
  static Value *getMemInstructionBase(const LoadOrStoreT *LSI) {
    static_assert(std::is_same_v<LoadOrStoreT, LoadInst> ||
                      std::is_same_v<LoadOrStoreT, StoreInst>,
                  "Expected sandboxir::Load or sandboxir::Store!");
    return LSI->Ctx.getOrCreateValue(
        getUnderlyingObject(LSI->getPointerOperand()->Val));
  }

  /// Return the number of bits of \p Ty.
  /// \param Ty Type to measure.
  /// \param DL Data layout used for type size.
  /// \Returns The size of \p Ty in bits.
  static unsigned getNumBits(Type *Ty, const DataLayout &DL) {
    return DL.getTypeSizeInBits(Ty->LLVMTy);
  }

  /// Return the number of bits required to represent the operands or return
  /// value of \p V in \p DL.
  /// \param V Value whose expected type size is measured.
  /// \param DL Data layout used for type size.
  /// \Returns The size of \p V's expected type in bits.
  static unsigned getNumBits(Value *V, const DataLayout &DL) {
    Type *Ty = getExpectedType(V);
    return getNumBits(Ty, DL);
  }

  /// Return the number of bits required to represent the operands or return
  /// value of \p I.
  /// \param I Instruction whose expected type size is measured.
  /// \Returns The size of \p I's expected type in bits.
  static unsigned getNumBits(Instruction *I) {
    return I->getDataLayout().getTypeSizeInBits(getExpectedType(I)->LLVMTy);
  }

  /// Equivalent to MemoryLocation::getOrNone(I).
  /// \param I Instruction to query.
  /// \Returns The memory location for \p I, or nullopt if none.
  static std::optional<llvm::MemoryLocation>
  memoryLocationGetOrNone(const Instruction *I) {
    return llvm::MemoryLocation::getOrNone(cast<llvm::Instruction>(I->Val));
  }

  /// Return the gap between the memory locations accessed by \p I0 and \p I1
  /// in bytes.
  /// Returns nullopt if the gap can't be determined.
  /// \param I0 First load or store instruction.
  /// \param I1 Second load or store instruction.
  /// \param SE Scalar evolution analysis used for pointer difference.
  /// \Returns The byte gap between \p I0 and \p I1, or nullopt if unknown.
  template <typename LoadOrStoreT>
  static std::optional<int> getPointerDiffInBytes(LoadOrStoreT *I0,
                                                  LoadOrStoreT *I1,
                                                  ScalarEvolution &SE) {
    static_assert(std::is_same_v<LoadOrStoreT, LoadInst> ||
                      std::is_same_v<LoadOrStoreT, StoreInst>,
                  "Expected sandboxir::Load or sandboxir::Store!");
    llvm::Value *Opnd0 = I0->getPointerOperand()->Val;
    llvm::Value *Opnd1 = I1->getPointerOperand()->Val;
    llvm::Value *Ptr0 = getUnderlyingObject(Opnd0);
    llvm::Value *Ptr1 = getUnderlyingObject(Opnd1);
    if (Ptr0 != Ptr1)
      return std::nullopt;
    llvm::Type *ElemTy = llvm::Type::getInt8Ty(SE.getContext());
    return getPointersDiff(ElemTy, Opnd0, ElemTy, Opnd1, I0->getDataLayout(),
                           SE, /*StrictCheck=*/false, /*CheckType=*/false);
  }

  /// Return true if \p I0 accesses a memory location lower than \p I1.
  ///
  /// Returns false if the memory locations are equal, or if I1 accesses a
  /// memory location greater than I0. Returns nullopt if the difference cannot
  /// be determined.
  /// \param I0 First load or store instruction.
  /// \param I1 Second load or store instruction.
  /// \param SE Scalar evolution analysis used for pointer difference.
  /// \Returns True if \p I0 is at a lower address than \p I1, or nullopt.
  template <typename LoadOrStoreT>
  static std::optional<bool> atLowerAddress(LoadOrStoreT *I0, LoadOrStoreT *I1,
                                            ScalarEvolution &SE) {
    auto Diff = getPointerDiffInBytes(I0, I1, SE);
    if (!Diff)
      return std::nullopt;
    return *Diff > 0;
  }

  /// Equivalent to BatchAA::getModRefInfo().
  /// \param BatchAA Batched alias analysis results.
  /// \param I Instruction to query.
  /// \param OptLoc Optional memory location, or none.
  /// \Returns The mod/ref info for \p I relative to \p OptLoc.
  static ModRefInfo
  aliasAnalysisGetModRefInfo(BatchAAResults &BatchAA, const Instruction *I,
                             const std::optional<MemoryLocation> &OptLoc) {
    return BatchAA.getModRefInfo(cast<llvm::Instruction>(I->Val), OptLoc);
  }

  /// Equivalent to llvm::verifyFunction().
  /// \Returns true if the IR is broken.
  /// \param F Function to verify.
  /// \param OS Stream for verifier diagnostics.
  static bool verifyFunction(const Function *F, raw_ostream &OS) {
    const auto &LLVMF = *cast<llvm::Function>(F->Val);
    return llvm::verifyFunction(LLVMF, &OS);
  }
};

} // namespace llvm::sandboxir

#endif // LLVM_SANDBOXIR_UTILS_H
