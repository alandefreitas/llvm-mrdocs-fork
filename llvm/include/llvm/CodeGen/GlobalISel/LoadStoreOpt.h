//== llvm/CodeGen/GlobalISel/LoadStoreOpt.h - LoadStoreOpt -------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// This is an optimization pass for GlobalISel generic memory operations.
/// Specifically, it focuses on merging stores and loads to consecutive
/// addresses.
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALISEL_LOADSTOREOPT_H
#define LLVM_CODEGEN_GLOBALISEL_LOADSTOREOPT_H

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
// Forward declarations.
class AnalysisUsage;
class GStore;
class LegalizerInfo;
class MachineBasicBlock;
class MachineInstr;
class TargetLowering;
struct LegalityQuery;
class MachineRegisterInfo;
/// Helpers for decomposing and alias-checking GlobalISel memory addresses.
namespace GISelAddressing {
/// Helper struct to store a base, index and offset that forms an address
class BaseIndexOffset {
private:
  Register BaseReg;
  Register IndexReg;
  std::optional<int64_t> Offset;

public:
  /// Construct an empty address with no base, index, or offset.
  BaseIndexOffset() = default;
  /// Return the base register of this address.
  ///
  /// \return The base register of this address.
  Register getBase() { return BaseReg; }
  /// Return the base register of this address.
  ///
  /// \return The base register of this address.
  Register getBase() const { return BaseReg; }
  /// Return the index register of this address.
  ///
  /// \return The index register of this address.
  Register getIndex() { return IndexReg; }
  /// Return the index register of this address.
  ///
  /// \return The index register of this address.
  Register getIndex() const { return IndexReg; }
  /// Set the base register of this address.
  ///
  /// \param NewBase Register to use as the address base.
  void setBase(Register NewBase) { BaseReg = NewBase; }
  /// Set the index register of this address.
  ///
  /// \param NewIndex Register to use as the address index.
  void setIndex(Register NewIndex) { IndexReg = NewIndex; }
  /// Set the constant offset of this address.
  ///
  /// \param NewOff New offset, or std::nullopt if the offset is unknown.
  void setOffset(std::optional<int64_t> NewOff) { Offset = NewOff; }
  /// Return true if this address has a known constant offset.
  ///
  /// \return True if this address has a known constant offset.
  bool hasValidOffset() const { return Offset.has_value(); }
  /// Return the constant offset of this address.
  ///
  /// The offset is only valid when hasValidOffset() is true.
  ///
  /// \return The constant offset of this address.
  int64_t getOffset() const { return *Offset; }
};

/// Returns a BaseIndexOffset which describes the pointer in \p Ptr.
///
/// \param Ptr Pointer register to decompose.
/// \param MRI Register info used to inspect defining instructions.
/// \return Address decomposition for \p Ptr as base, index, and offset.
LLVM_ABI BaseIndexOffset getPointerInfo(Register Ptr, MachineRegisterInfo &MRI);

/// Compute whether or not a memory access at \p MI1 aliases with an access at
/// \p MI2 \returns true if either alias/no-alias is known. Sets \p IsAlias
/// accordingly.
///
/// \param MI1 First memory instruction.
/// \param MI2 Second memory instruction.
/// \param IsAlias Set to true when the accesses are known to alias.
/// \param MRI Register info used to inspect addressing operands.
LLVM_ABI bool aliasIsKnownForLoadStore(const MachineInstr &MI1,
                                       const MachineInstr &MI2, bool &IsAlias,
                                       MachineRegisterInfo &MRI);

/// Returns true if the instruction \p MI may alias \p Other.
///
/// This function uses multiple strategies to detect aliasing, whereas
/// aliasIsKnownForLoadStore just looks at the addresses of load/stores and
/// tries to reason about base/index/offsets.
///
/// \param MI First instruction to test for aliasing.
/// \param Other Second instruction to test for aliasing.
/// \param MRI Register info used to inspect addressing operands.
/// \param AA Optional alias analysis; may be null.
/// \return True if \p MI may alias \p Other.
LLVM_ABI bool instMayAlias(const MachineInstr &MI, const MachineInstr &Other,
                           MachineRegisterInfo &MRI, AliasAnalysis *AA);
} // namespace GISelAddressing

/// Legacy pass that merges consecutive GlobalISel loads and stores.
class LLVM_ABI LoadStoreOptLegacy : public MachineFunctionPass {
public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct the legacy GlobalISel load/store optimization pass.
  LoadStoreOptLegacy();

  /// Return the name of this pass.
  ///
  /// \return The name of this pass.
  StringRef getPassName() const override { return "LoadStoreOpt"; }

  /// Return the properties this pass requires of the machine function.
  ///
  /// Load/store merging expects the function to be in SSA form.
  ///
  /// \return Machine function properties required by this pass.
  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setIsSSA();
  }

  /// Declare required analyses and that this pass preserves all analyses.
  ///
  /// \param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// Merge consecutive loads and stores in \p MF.
  ///
  /// \param MF Machine function whose memory operations are optimized.
  /// \return True if the machine function was modified.
  bool runOnMachineFunction(MachineFunction &MF) override;
};

/// New PM pass that merges consecutive GlobalISel loads and stores.
class LoadStoreOptPass : public RequiredPassInfoMixin<LoadStoreOptPass> {
public:
  /// Merge consecutive loads and stores in \p MF.
  ///
  /// \param MF Machine function whose memory operations are optimized.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return Analyses preserved by this pass after running.
  PreservedAnalyses run(MachineFunction &MF,
                        MachineFunctionAnalysisManager &MFAM);

  /// Return the properties this pass requires of the machine function.
  ///
  /// Load/store merging expects the function to be in SSA form.
  ///
  /// \return Machine function properties required by this pass.
  MachineFunctionProperties getRequiredProperties() const {
    return MachineFunctionProperties().setIsSSA();
  }
};

} // End namespace llvm.

#endif
