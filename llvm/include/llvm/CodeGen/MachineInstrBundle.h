//===- llvm/CodeGen/MachineInstrBundle.h - MI bundle utilities --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provide utility functions to manipulate machine instruction
// bundles.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEINSTRBUNDLE_H
#define LLVM_CODEGEN_MACHINEINSTRBUNDLE_H

#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

/// Finalize a machine instruction bundle from FirstMI to LastMI (exclusive).
///
/// This routine adds a BUNDLE instruction to represent the bundle, it adds
/// IsInternalRead markers to MachineOperands which are defined inside the
/// bundle, and it copies externally visible defs and uses to the BUNDLE
/// instruction.
/// @param MBB The basic block containing the instructions to finalize.
/// @param FirstMI Iterator to the first instruction in the bundle.
/// @param LastMI End iterator (exclusive) of the bundle.
LLVM_ABI void finalizeBundle(MachineBasicBlock &MBB,
                             MachineBasicBlock::instr_iterator FirstMI,
                             MachineBasicBlock::instr_iterator LastMI);

/// Finalize a machine instruction bundle whose end is marked by InsideBundle.
///
/// Same functionality as the previous finalizeBundle except the last
/// instruction in the bundle is not provided as an input. This is used in
/// cases where bundles are pre-determined by marking instructions with
/// 'InsideBundle' marker. It returns the MBB instruction iterator that
/// points to the end of the bundle.
/// @param MBB The basic block containing the instructions to finalize.
/// @param FirstMI Iterator to the first instruction in the bundle.
/// @return An instruction iterator pointing to the end of the bundle.
LLVM_ABI MachineBasicBlock::instr_iterator
finalizeBundle(MachineBasicBlock &MBB,
               MachineBasicBlock::instr_iterator FirstMI);

/// Finalize instruction bundles in the specified MachineFunction.
///
/// Return true if any bundles are finalized.
/// @param MF The machine function whose bundles should be finalized.
/// @return True if any bundles were finalized.
LLVM_ABI bool finalizeBundles(MachineFunction &MF);

/// Returns an iterator to the first instruction in the bundle containing \p I.
/// @param I An instruction iterator into a bundle.
/// @return An iterator to the first instruction in the bundle.
inline MachineBasicBlock::instr_iterator getBundleStart(
    MachineBasicBlock::instr_iterator I) {
  while (I->isBundledWithPred())
    --I;
  return I;
}

/// Returns an iterator to the first instruction in the bundle containing \p I.
/// @param I An instruction iterator into a bundle.
/// @return A const iterator to the first instruction in the bundle.
inline MachineBasicBlock::const_instr_iterator getBundleStart(
    MachineBasicBlock::const_instr_iterator I) {
  while (I->isBundledWithPred())
    --I;
  return I;
}

/// Returns an iterator pointing beyond the bundle containing \p I.
/// @param I An instruction iterator into a bundle.
/// @return An iterator one past the last instruction in the bundle.
inline MachineBasicBlock::instr_iterator getBundleEnd(
    MachineBasicBlock::instr_iterator I) {
  while (I->isBundledWithSucc())
    ++I;
  ++I;
  return I;
}

/// Returns an iterator pointing beyond the bundle containing \p I.
/// @param I An instruction iterator into a bundle.
/// @return A const iterator one past the last instruction in the bundle.
inline MachineBasicBlock::const_instr_iterator getBundleEnd(
    MachineBasicBlock::const_instr_iterator I) {
  while (I->isBundledWithSucc())
    ++I;
  ++I;
  return I;
}

//===----------------------------------------------------------------------===//
// MachineBundleOperand iterator
//

/// Iterator that visits all operands in a bundle of MachineInstrs.
///
/// This class is not intended to be used directly, use one of the sub-classes
/// instead.
///
/// Intended use:
///
///   for (MIBundleOperands MIO(MI); MIO.isValid(); ++MIO) {
///     if (!MIO->isReg())
///       continue;
///     ...
///   }
///
template <typename ValueT>
class MIBundleOperandIteratorBase
    : public iterator_facade_base<MIBundleOperandIteratorBase<ValueT>,
                                  std::forward_iterator_tag, ValueT> {
  MachineBasicBlock::instr_iterator InstrI, InstrE;
  MachineInstr::mop_iterator OpI, OpE;

  // If the operands on InstrI are exhausted, advance InstrI to the next
  // bundled instruction with operands.
  void advance() {
    while (OpI == OpE) {
      // Don't advance off the basic block, or into a new bundle.
      if (++InstrI == InstrE || !InstrI->isInsideBundle()) {
        InstrI = InstrE;
        break;
      }
      OpI = InstrI->operands_begin();
      OpE = InstrI->operands_end();
    }
  }

protected:
  /// MIBundleOperandIteratorBase - Create an iterator that visits all operands
  /// on MI, or all operands on every instruction in the bundle containing MI.
  ///
  /// @param MI The instruction to examine.
  ///
  explicit MIBundleOperandIteratorBase(MachineInstr &MI) {
    InstrI = getBundleStart(MI.getIterator());
    InstrE = MI.getParent()->instr_end();
    OpI = InstrI->operands_begin();
    OpE = InstrI->operands_end();
    advance();
  }

  /// Construct a past-the-end operand iterator for a basic block.
  ///
  /// Both instruction iterators point to the end of the BB and OpI == OpE.
  /// @param InstrE End instruction iterator for the basic block.
  /// @param OpE End operand iterator used to mark an exhausted operand range.
  explicit MIBundleOperandIteratorBase(MachineBasicBlock::instr_iterator InstrE,
                                       MachineInstr::mop_iterator OpE)
      : InstrI(InstrE), InstrE(InstrE), OpI(OpE), OpE(OpE) {}

public:
  /// isValid - Returns true until all the operands have been visited.
  /// @return True if there are remaining operands to visit.
  bool isValid() const { return OpI != OpE; }

  /// Preincrement.  Move to the next operand.
  void operator++() {
    assert(isValid() && "Cannot advance MIOperands beyond the last operand");
    ++OpI;
    advance();
  }

  /// Dereference to the current machine operand.
  /// @return A reference to the current machine operand.
  ValueT &operator*() const { return *OpI; }
  /// Member access for the current machine operand.
  /// @return A pointer to the current machine operand.
  ValueT *operator->() const { return &*OpI; }

  /// Return true if this iterator and \p Arg refer to the same operand.
  /// @param Arg The other bundle-operand iterator to compare against.
  /// @return True if both iterators refer to the same operand.
  bool operator==(const MIBundleOperandIteratorBase &Arg) const {
    // Iterators are equal, if InstrI matches and either OpIs match or OpI ==
    // OpE match for both. The second condition allows us to construct an 'end'
    // iterator, without finding the last instruction in a bundle up-front.
    return InstrI == Arg.InstrI &&
           (OpI == Arg.OpI || (OpI == OpE && Arg.OpI == Arg.OpE));
  }
  /// getOperandNo - Returns the number of the current operand relative to its
  /// instruction.
  ///
  /// @return The current operand index within its instruction.
  unsigned getOperandNo() const {
    return OpI - InstrI->operands_begin();
  }
};

/// MIBundleOperands - Iterate over all operands in a bundle of machine
/// instructions.
///
class MIBundleOperands : public MIBundleOperandIteratorBase<MachineOperand> {
  /// Constructor for an iterator past the last iteration.
  MIBundleOperands(MachineBasicBlock::instr_iterator InstrE,
                   MachineInstr::mop_iterator OpE)
      : MIBundleOperandIteratorBase(InstrE, OpE) {}

public:
  /// Construct an iterator over all operands in the bundle containing \p MI.
  /// @param MI An instruction in the bundle to iterate.
  MIBundleOperands(MachineInstr &MI) : MIBundleOperandIteratorBase(MI) {}

  /// Returns an iterator past the last iteration.
  /// @param MBB The basic block whose instr_end marks the past-the-end position.
  /// @return A past-the-end MIBundleOperands iterator for \p MBB.
  static MIBundleOperands end(const MachineBasicBlock &MBB) {
    return {const_cast<MachineBasicBlock &>(MBB).instr_end(),
            const_cast<MachineBasicBlock &>(MBB).instr_begin()->operands_end()};
  }
};

/// ConstMIBundleOperands - Iterate over all operands in a const bundle of
/// machine instructions.
///
class ConstMIBundleOperands
    : public MIBundleOperandIteratorBase<const MachineOperand> {

  /// Constructor for an iterator past the last iteration.
  ConstMIBundleOperands(MachineBasicBlock::instr_iterator InstrE,
                        MachineInstr::mop_iterator OpE)
      : MIBundleOperandIteratorBase(InstrE, OpE) {}

public:
  /// Construct an iterator over all operands in the bundle containing \p MI.
  /// @param MI An instruction in the bundle to iterate.
  ConstMIBundleOperands(const MachineInstr &MI)
      : MIBundleOperandIteratorBase(const_cast<MachineInstr &>(MI)) {}

  /// Returns an iterator past the last iteration.
  /// @param MBB The basic block whose instr_end marks the past-the-end position.
  /// @return A past-the-end ConstMIBundleOperands iterator for \p MBB.
  static ConstMIBundleOperands end(const MachineBasicBlock &MBB) {
    return {const_cast<MachineBasicBlock &>(MBB).instr_end(),
            const_cast<MachineBasicBlock &>(MBB).instr_begin()->operands_end()};
  }
};

/// Return a range over all operands in the const bundle containing \p MI.
/// @param MI An instruction in the bundle to iterate.
/// @return An iterator range covering every operand in the const bundle.
inline iterator_range<ConstMIBundleOperands>
const_mi_bundle_ops(const MachineInstr &MI) {
  return make_range(ConstMIBundleOperands(MI),
                    ConstMIBundleOperands::end(*MI.getParent()));
}

/// Return a range over all operands in the bundle containing \p MI.
/// @param MI An instruction in the bundle to iterate.
/// @return An iterator range covering every operand in the bundle.
inline iterator_range<MIBundleOperands> mi_bundle_ops(MachineInstr &MI) {
  return make_range(MIBundleOperands(MI),
                    MIBundleOperands::end(*MI.getParent()));
}

/// VirtRegInfo - Information about a virtual register used by a set of
/// operands.
///
struct VirtRegInfo {
  /// Reads - One of the operands read the virtual register.  This does not
  /// include undef or internal use operands, see MO::readsReg().
  bool Reads;

  /// Writes - One of the operands writes the virtual register.
  bool Writes;

  /// Tied - Uses and defs must use the same register. This can be because of
  /// a two-address constraint, or there may be a partial redefinition of a
  /// sub-register.
  bool Tied;
};

/// Analyze how the current instruction or bundle uses a virtual register.
///
/// This function should not be called after operator++(), it expects a fresh
/// iterator.
///
/// @param MI The instruction or bundle header to analyze.
/// @param Reg The virtual register to analyze.
/// @param Ops When set, this vector will receive an (MI, OpNum) entry for
///            each operand referring to Reg.
/// @returns A filled-in RegInfo struct.
LLVM_ABI VirtRegInfo AnalyzeVirtRegInBundle(
    MachineInstr &MI, Register Reg,
    SmallVectorImpl<std::pair<MachineInstr *, unsigned>> *Ops = nullptr);

/// Return a pair of lane masks (reads, writes) indicating which lanes this
/// instruction uses with Reg.
/// @param MI The instruction or bundle header to analyze.
/// @param Reg The virtual register whose lanes are analyzed.
/// @param MRI Register info used to resolve register classes and aliases.
/// @param TRI Target register info used for subregister lane masks.
/// @return A pair of lane masks for reads and writes of \p Reg.
LLVM_ABI std::pair<LaneBitmask, LaneBitmask>
AnalyzeVirtRegLanesInBundle(const MachineInstr &MI, Register Reg,
                            const MachineRegisterInfo &MRI,
                            const TargetRegisterInfo &TRI);

/// Information about how a physical register Reg is used by a set of
/// operands.
struct PhysRegInfo {
  /// There is a regmask operand indicating Reg is clobbered.
  /// \see MachineOperand::CreateRegMask().
  bool Clobbered;

  /// Reg or one of its aliases is defined. The definition may only cover
  /// parts of the register.
  bool Defined;
  /// Reg or a super-register is defined. The definition covers the full
  /// register.
  bool FullyDefined;

  /// Reg or one of its aliases is read. The register may only be read
  /// partially.
  bool Read;
  /// Reg or a super-register is read. The full register is read.
  bool FullyRead;

  /// Either:
  /// - Reg is FullyDefined and all defs of reg or an overlapping
  ///   register are dead, or
  /// - Reg is completely dead because "defined" by a clobber.
  bool DeadDef;

  /// Reg is Defined and all defs of reg or an overlapping register are
  /// dead.
  bool PartialDeadDef;

  /// There is a use operand of reg or a super-register with kill flag set.
  bool Killed;
};

/// Analyze how the current instruction or bundle uses a physical register.
///
/// This function should not be called after operator++(), it expects a fresh
/// iterator.
///
/// @param MI The instruction or bundle header to analyze.
/// @param Reg The physical register to analyze.
/// @param TRI Target register info used to resolve aliases and super-regs.
/// @returns A filled-in PhysRegInfo struct.
LLVM_ABI PhysRegInfo AnalyzePhysRegInBundle(const MachineInstr &MI,
                                            Register Reg,
                                            const TargetRegisterInfo *TRI);

/// New PM test pass that finalizes a bundle for each basic block.
class FinalizeBundleTestPass
    : public RequiredPassInfoMixin<FinalizeBundleTestPass> {
public:
  /// Finalize test bundles in \p MF.
  /// @param MF Machine function to process.
  /// @param MFAM Analysis manager providing required analyses.
  /// @return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

/// New PM pass that unpacks machine instruction bundles.
class UnpackMachineBundlesPass
    : public RequiredPassInfoMixin<UnpackMachineBundlesPass> {

public:
  /// Construct a pass that optionally filters functions with \p Ftor.
  /// @param Ftor Optional predicate; when set, only matching functions are
  ///             unpacked.
  UnpackMachineBundlesPass(
      std::function<bool(const MachineFunction &)> Ftor = nullptr)
      : PredicateFtor(std::move(Ftor)) {}
  /// Unpack machine instruction bundles in \p MF.
  /// @param MF Machine function to process.
  /// @param MFAM Analysis manager providing required analyses.
  /// @return The set of analyses preserved by this pass.
  PreservedAnalyses LLVM_ABI run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);

private:
  std::function<bool(const MachineFunction &)> PredicateFtor;
};

} // End llvm namespace

#endif
