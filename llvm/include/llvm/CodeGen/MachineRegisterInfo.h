//===- llvm/CodeGen/MachineRegisterInfo.h -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the MachineRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEREGISTERINFO_H
#define LLVM_CODEGEN_MACHINEREGISTERINFO_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/IndexedMap.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/RegisterBank.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/MC/LaneBitmask.h"
#include "llvm/Support/Compiler.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

namespace llvm {

class PSetIterator;

/// Convenient type to represent either a register class or a register bank.
using RegClassOrRegBank =
    PointerUnion<const TargetRegisterClass *, const RegisterBank *>;

/// MachineRegisterInfo - Keep track of information for virtual and physical
/// registers, including vreg register classes, use/def chains for registers,
/// etc.
class MachineRegisterInfo {
public:
  /// Observer notified when virtual registers are created or cloned.
  class LLVM_ABI Delegate {
    virtual void anchor();

  public:
    /// Destroy this delegate.
    virtual ~Delegate() = default;

    /// Called when a new virtual register \p Reg is created.
    ///
    /// \param Reg Newly created virtual register.
    virtual void MRI_NoteNewVirtualRegister(Register Reg) = 0;
    /// Called when virtual register \p NewReg is cloned from \p SrcReg.
    ///
    /// \param NewReg Newly created clone register.
    /// \param SrcReg Source register that was cloned.
    virtual void MRI_NoteCloneVirtualRegister(Register NewReg,
                                              Register SrcReg) {
      MRI_NoteNewVirtualRegister(NewReg);
    }
  };

  /// Pending VirtRegMap entry parsed from MIR for later initialization.
  ///
  /// Waiting to be consumed by VirtRegMap::init().
  struct PendingVirtRegMapEntry {
    /// Virtual register this pending mapping describes.
    Register VReg;
    /// Virtual register this was split from, or NoReg if absent.
    Register SplitFrom;      // NoReg if absent.
    /// Assigned physical register, or NoReg if absent.
    MCRegister AssignedPhys; // NoReg if absent.
  };

private:
  MachineFunction *MF;
  SmallPtrSet<Delegate *, 1> TheDelegates;

  /// True if subregister liveness is tracked.
  const bool TracksSubRegLiveness;

  /// VRegInfo - Information we keep for each virtual register.
  ///
  /// Each element in this list contains the register class of the vreg and the
  /// start of the use/def list for the register.
  IndexedMap<std::pair<RegClassOrRegBank, MachineOperand *>,
             VirtReg2IndexFunctor>
      VRegInfo;

  /// Map for recovering vreg name from vreg number.
  /// This map is used by the MIR Printer.
  IndexedMap<std::string, VirtReg2IndexFunctor> VReg2Name;

  /// StringSet that is used to unique vreg names.
  StringSet<> VRegNames;

  /// The flag is true upon \p UpdatedCSRs initialization
  /// and false otherwise.
  bool IsUpdatedCSRsInitialized = false;

  /// Contains the updated callee saved register list.
  /// As opposed to the static list defined in register info,
  /// all registers that were disabled are removed from the list.
  SmallVector<MCPhysReg, 16> UpdatedCSRs;

  /// RegAllocHints - This vector records register allocation hints for
  /// virtual registers. For each virtual register, it keeps a pair of hint
  /// type and hints vector making up the allocation hints. Only the first
  /// hint may be target specific, and in that case this is reflected by the
  /// first member of the pair being non-zero. If the hinted register is
  /// virtual, it means the allocator should prefer the physical register
  /// allocated to it if any.
  IndexedMap<std::pair<unsigned, SmallVector<Register, 4>>,
             VirtReg2IndexFunctor>
      RegAllocHints;

  /// Hold the register properties that are used to populate the VirtRegMap
  /// pass when deserializing from .mir files.
  SmallVector<PendingVirtRegMapEntry, 0> PendingVirtRegMapEntries;

  /// PhysRegUseDefLists - This is an array of the head of the use/def list for
  /// physical registers.
  std::unique_ptr<MachineOperand *[]> PhysRegUseDefLists;

  /// getRegUseDefListHead - Return the head pointer for the register use/def
  /// list for the specified virtual or physical register.
  MachineOperand *&getRegUseDefListHead(Register RegNo) {
    if (RegNo.isVirtual())
      return VRegInfo[RegNo.id()].second;
    return PhysRegUseDefLists[RegNo.id()];
  }

  MachineOperand *getRegUseDefListHead(Register RegNo) const {
    if (RegNo.isVirtual())
      return VRegInfo[RegNo.id()].second;
    return PhysRegUseDefLists[RegNo.id()];
  }

  /// Get the next element in the use-def chain.
  static MachineOperand *getNextOperandForReg(const MachineOperand *MO) {
    assert(MO && MO->isReg() && "This is not a register operand!");
    return MO->Contents.Reg.Next;
  }

  /// UsedPhysRegMask - Additional used physregs including aliases.
  /// This bit vector represents all the registers clobbered by function calls.
  BitVector UsedPhysRegMask;

  /// ReservedRegs - This is a bit vector of reserved registers.  The target
  /// may change its mind about which registers should be reserved.  This
  /// vector is the frozen set of reserved registers when register allocation
  /// started.
  BitVector ReservedRegs;

  using VRegToTypeMap = IndexedMap<LLT, VirtReg2IndexFunctor>;
  /// Map generic virtual registers to their low-level type.
  VRegToTypeMap VRegToType;

  /// Keep track of the physical registers that are live in to the function.
  /// Live in values are typically arguments in registers.  LiveIn values are
  /// allowed to have virtual registers associated with them, stored in the
  /// second element.
  std::vector<std::pair<MCRegister, Register>> LiveIns;

public:
  /// Construct register info for machine function \p MF.
  ///
  /// \param MF Machine function whose registers are tracked.
  LLVM_ABI explicit MachineRegisterInfo(MachineFunction *MF);
  /// MachineRegisterInfo is non-copyable.
  ///
  /// \param RHS Unused; copy construction is deleted.
  MachineRegisterInfo(const MachineRegisterInfo &RHS) = delete;
  /// MachineRegisterInfo is non-assignable.
  ///
  /// \param RHS Unused; copy assignment is deleted.
  MachineRegisterInfo &operator=(const MachineRegisterInfo &RHS) = delete;

  /// Return the target register info for this machine function.
  ///
  /// \return Target register info for this machine function.
  const TargetRegisterInfo *getTargetRegisterInfo() const {
    return MF->getSubtarget().getRegisterInfo();
  }

  /// Remove \p delegate from the set of attached observers.
  ///
  /// \param delegate Existing delegate to detach.
  void resetDelegate(Delegate *delegate) {
    // Ensure another delegate does not take over unless the current
    // delegate first unattaches itself.
    assert(TheDelegates.count(delegate) &&
           "Only an existing delegate can perform reset!");
    TheDelegates.erase(delegate);
  }

  /// Attach \p delegate as an observer of virtual-register events.
  ///
  /// \param delegate Non-null delegate that is not already attached.
  void addDelegate(Delegate *delegate) {
    assert(delegate && !TheDelegates.count(delegate) &&
           "Attempted to add null delegate, or to change it without "
           "first resetting it!");

    TheDelegates.insert(delegate);
  }

  /// Notify all delegates that virtual register \p Reg was created.
  ///
  /// \param Reg Newly created virtual register.
  void noteNewVirtualRegister(Register Reg) {
    for (auto *TheDelegate : TheDelegates)
      TheDelegate->MRI_NoteNewVirtualRegister(Reg);
  }

  /// Notify all delegates that \p NewReg was cloned from \p SrcReg.
  ///
  /// \param NewReg Newly created clone register.
  /// \param SrcReg Source register that was cloned.
  void noteCloneVirtualRegister(Register NewReg, Register SrcReg) {
    for (auto *TheDelegate : TheDelegates)
      TheDelegate->MRI_NoteCloneVirtualRegister(NewReg, SrcReg);
  }

  /// Return the machine function associated with this register info.
  ///
  /// \return Machine function associated with this register info.
  const MachineFunction &getMF() const { return *MF; }

  //===--------------------------------------------------------------------===//
  // Function State
  //===--------------------------------------------------------------------===//

  /// Return true when the machine function is in SSA form.
  ///
  /// Early passes require the machine function to be in SSA form where every
  /// virtual register has a single defining instruction.
  ///
  /// The TwoAddressInstructionPass and PHIElimination passes take the machine
  /// function out of SSA form when they introduce multiple defs per virtual
  /// register.
  ///
  /// \return True when the machine function is in SSA form.
  bool isSSA() const { return MF->getProperties().hasIsSSA(); }

  /// Indicate that the machine function is no longer in SSA form.
  void leaveSSA() { MF->getProperties().resetIsSSA(); }

  /// tracksLiveness - Returns true when tracking register liveness accurately.
  /// (see MachineFUnctionProperties::Property description for details)
  ///
  /// \return True when register liveness is tracked accurately.
  bool tracksLiveness() const {
    return MF->getProperties().hasTracksLiveness();
  }

  /// invalidateLiveness - Indicates that register liveness is no longer being
  /// tracked accurately.
  ///
  /// This should be called by late passes that invalidate the liveness
  /// information.
  void invalidateLiveness() { MF->getProperties().resetTracksLiveness(); }

  /// Returns true if liveness for register class @p RC should be tracked at
  /// the subregister level.
  ///
  /// \param RC Register class whose subregister liveness policy is queried.
  /// \return True if subregister liveness should be tracked for \p RC.
  bool shouldTrackSubRegLiveness(const TargetRegisterClass &RC) const {
    return subRegLivenessEnabled() && RC.HasDisjunctSubRegs;
  }
  /// Return true if subregister liveness should be tracked for \p VReg.
  ///
  /// \param VReg Virtual register whose subregister liveness policy is queried.
  /// \return True if subregister liveness should be tracked for \p VReg.
  bool shouldTrackSubRegLiveness(Register VReg) const {
    assert(VReg.isVirtual() && "Must pass a VReg");
    const TargetRegisterClass *RC = getRegClassOrNull(VReg);
    return LLVM_LIKELY(RC) ? shouldTrackSubRegLiveness(*RC) : false;
  }
  /// Return true if subregister liveness tracking is enabled for this function.
  ///
  /// \return True if subregister liveness tracking is enabled.
  bool subRegLivenessEnabled() const {
    return TracksSubRegLiveness;
  }

  //===--------------------------------------------------------------------===//
  // Register Info
  //===--------------------------------------------------------------------===//

  /// Returns true if the updated CSR list was initialized and false otherwise.
  ///
  /// \return True if the updated CSR list was initialized.
  bool isUpdatedCSRsInitialized() const { return IsUpdatedCSRsInitialized; }

  /// Disables the register from the list of CSRs.
  /// I.e. the register will not appear as part of the CSR mask.
  /// \see UpdatedCalleeSavedRegs.
  ///
  /// \param Reg Callee-saved register to disable.
  LLVM_ABI void disableCalleeSavedRegister(MCRegister Reg);

  /// Returns list of callee saved registers.
  /// The function returns the updated CSR list (after taking into account
  /// registers that are disabled from the CSR list).
  ///
  /// \return Pointer to the null-terminated list of callee-saved registers.
  LLVM_ABI const MCPhysReg *getCalleeSavedRegs() const;

  /// Sets the updated Callee Saved Registers list.
  /// Notice that it will override ant previously disabled/saved CSRs.
  ///
  /// \param CSRs New callee-saved register list.
  LLVM_ABI void setCalleeSavedRegs(ArrayRef<MCPhysReg> CSRs);

  /// Add register operand \p MO to its register's use/def list.
  ///
  /// Strictly for use by MachineInstr.cpp.
  ///
  /// \param MO Register operand being linked into a use/def chain.
  LLVM_ABI void addRegOperandToUseList(MachineOperand *MO);

  /// Remove register operand \p MO from its register's use/def list.
  ///
  /// Strictly for use by MachineInstr.cpp.
  ///
  /// \param MO Register operand being unlinked from a use/def chain.
  LLVM_ABI void removeRegOperandFromUseList(MachineOperand *MO);

  /// Move \p NumOps operands from \p Src to \p Dst, updating use/def lists.
  ///
  /// Strictly for use by MachineInstr.cpp.
  ///
  /// \param Dst Destination operand storage.
  /// \param Src Source operand storage.
  /// \param NumOps Number of operands to move.
  LLVM_ABI void moveOperands(MachineOperand *Dst, MachineOperand *Src,
                             unsigned NumOps);

  /// Verify the sanity of the use list for Reg.
  ///
  /// \param Reg Register whose use/def list is verified.
  LLVM_ABI void verifyUseList(Register Reg) const;

  /// Verify the use list of all registers.
  LLVM_ABI void verifyUseLists() const;

  /// Forward declaration of the operand def/use-chain iterator.
  template <bool Uses, bool Defs, bool SkipDebug, bool ByOperand, bool ByInstr>
  class defusechain_iterator;
  /// Forward declaration of the instruction def/use-chain iterator.
  template <bool Uses, bool Defs, bool SkipDebug, bool ByInstr>
  class defusechain_instr_iterator;

  // Make it a friend so it can access getNextOperandForReg().
  template <bool, bool, bool, bool, bool> friend class defusechain_iterator;
  template <bool, bool, bool, bool> friend class defusechain_instr_iterator;

  /// reg_iterator/reg_begin/reg_end - Walk all defs and uses of the specified
  /// register.
  using reg_iterator = defusechain_iterator<true, true, false, true, false>;
  /// Return an iterator to the first def or use operand of \p RegNo.
  ///
  /// \param RegNo Register whose def/use operands are iterated.
  /// \return Iterator to the first def or use operand of \p RegNo.
  reg_iterator reg_begin(Register RegNo) const {
    return reg_iterator(getRegUseDefListHead(RegNo));
  }
  /// Return the end iterator for register def/use operand walks.
  ///
  /// \return End iterator for register def/use operand walks.
  static reg_iterator reg_end() { return reg_iterator(nullptr); }

  /// Return a range over all def and use operands of \p Reg.
  ///
  /// \param Reg Register whose def/use operands are iterated.
  /// \return Range over all def and use operands of \p Reg.
  inline iterator_range<reg_iterator> reg_operands(Register Reg) const {
    return make_range(reg_begin(Reg), reg_end());
  }

  /// reg_instr_iterator/reg_instr_begin/reg_instr_end - Walk all defs and uses
  /// of the specified register, stepping by MachineInstr.
  using reg_instr_iterator =
      defusechain_instr_iterator<true, true, false, /*ByInstr=*/true>;
  /// Return an iterator to the first instruction that defs or uses \p RegNo.
  ///
  /// \param RegNo Register whose def/use instructions are iterated.
  /// \return Iterator to the first instruction that defs or uses \p RegNo.
  reg_instr_iterator reg_instr_begin(Register RegNo) const {
    return reg_instr_iterator(getRegUseDefListHead(RegNo));
  }
  /// Return the end iterator for register def/use instruction walks.
  ///
  /// \return End iterator for register def/use instruction walks.
  static reg_instr_iterator reg_instr_end() {
    return reg_instr_iterator(nullptr);
  }

  /// Return a range over all instructions that def or use \p Reg.
  ///
  /// \param Reg Register whose def/use instructions are iterated.
  /// \return Range over all instructions that def or use \p Reg.
  inline iterator_range<reg_instr_iterator>
  reg_instructions(Register Reg) const {
    return make_range(reg_instr_begin(Reg), reg_instr_end());
  }

  /// reg_bundle_iterator/reg_bundle_begin/reg_bundle_end - Walk all defs and uses
  /// of the specified register, stepping by bundle.
  using reg_bundle_iterator =
      defusechain_instr_iterator<true, true, false, /*ByInstr=*/false>;
  /// Return an iterator to the first bundle that defs or uses \p RegNo.
  ///
  /// \param RegNo Register whose def/use bundles are iterated.
  /// \return Iterator to the first bundle that defs or uses \p RegNo.
  reg_bundle_iterator reg_bundle_begin(Register RegNo) const {
    return reg_bundle_iterator(getRegUseDefListHead(RegNo));
  }
  /// Return the end iterator for register def/use bundle walks.
  ///
  /// \return End iterator for register def/use bundle walks.
  static reg_bundle_iterator reg_bundle_end() {
    return reg_bundle_iterator(nullptr);
  }

  /// Return a range over all bundles that def or use \p Reg.
  ///
  /// \param Reg Register whose def/use bundles are iterated.
  /// \return Range over all bundles that def or use \p Reg.
  inline iterator_range<reg_bundle_iterator> reg_bundles(Register Reg) const {
    return make_range(reg_bundle_begin(Reg), reg_bundle_end());
  }

  /// reg_empty - Return true if there are no instructions using or defining the
  /// specified register (it may be live-in).
  ///
  /// \param RegNo Register to test for any defs or uses.
  /// \return True if there are no instructions using or defining \p RegNo.
  bool reg_empty(Register RegNo) const { return reg_begin(RegNo) == reg_end(); }

  /// reg_nodbg_iterator/reg_nodbg_begin/reg_nodbg_end - Walk all defs and uses
  /// of the specified register, skipping those marked as Debug.
  using reg_nodbg_iterator =
      defusechain_iterator<true, true, true, true, false>;
  /// Return an iterator to the first non-debug def or use operand of \p RegNo.
  ///
  /// \param RegNo Register whose non-debug def/use operands are iterated.
  /// \return Iterator to the first non-debug def or use operand of \p RegNo.
  reg_nodbg_iterator reg_nodbg_begin(Register RegNo) const {
    return reg_nodbg_iterator(getRegUseDefListHead(RegNo));
  }
  /// Return the end iterator for non-debug register def/use operand walks.
  ///
  /// \return End iterator for non-debug register def/use operand walks.
  static reg_nodbg_iterator reg_nodbg_end() {
    return reg_nodbg_iterator(nullptr);
  }

  /// Return a range over all non-debug def and use operands of \p Reg.
  ///
  /// \param Reg Register whose non-debug def/use operands are iterated.
  /// \return Range over all non-debug def and use operands of \p Reg.
  inline iterator_range<reg_nodbg_iterator>
  reg_nodbg_operands(Register Reg) const {
    return make_range(reg_nodbg_begin(Reg), reg_nodbg_end());
  }

  /// Iterator over non-debug defs and uses of a register, stepping by
  /// MachineInstr.
  ///
  /// Walk all defs and uses of the specified register, stepping by
  /// MachineInstr, skipping those marked as Debug.
  using reg_instr_nodbg_iterator =
      defusechain_instr_iterator<true, true, true, /*ByInstr=*/true>;
  /// Return an iterator to the first non-debug instruction that defs or uses
  /// \p RegNo.
  ///
  /// \param RegNo Register whose non-debug def/use instructions are iterated.
  /// \return Iterator to the first non-debug instruction that defs or uses \p RegNo.
  reg_instr_nodbg_iterator reg_instr_nodbg_begin(Register RegNo) const {
    return reg_instr_nodbg_iterator(getRegUseDefListHead(RegNo));
  }
  /// Return the end iterator for non-debug register def/use instruction walks.
  ///
  /// \return End iterator for non-debug register def/use instruction walks.
  static reg_instr_nodbg_iterator reg_instr_nodbg_end() {
    return reg_instr_nodbg_iterator(nullptr);
  }

  /// Return a range over all non-debug instructions that def or use \p Reg.
  ///
  /// \param Reg Register whose non-debug def/use instructions are iterated.
  /// \return Range over all non-debug instructions that def or use \p Reg.
  inline iterator_range<reg_instr_nodbg_iterator>
  reg_nodbg_instructions(Register Reg) const {
    return make_range(reg_instr_nodbg_begin(Reg), reg_instr_nodbg_end());
  }

  /// Iterator over non-debug defs and uses of a register, stepping by bundle.
  ///
  /// Walk all defs and uses of the specified register, stepping by bundle,
  /// skipping those marked as Debug.
  using reg_bundle_nodbg_iterator =
      defusechain_instr_iterator<true, true, true, /*ByInstr=*/false>;
  /// Return an iterator to the first non-debug bundle that defs or uses
  /// \p RegNo.
  ///
  /// \param RegNo Register whose non-debug def/use bundles are iterated.
  /// \return Iterator to the first non-debug bundle that defs or uses \p RegNo.
  reg_bundle_nodbg_iterator reg_bundle_nodbg_begin(Register RegNo) const {
    return reg_bundle_nodbg_iterator(getRegUseDefListHead(RegNo));
  }
  /// Return the end iterator for non-debug register def/use bundle walks.
  ///
  /// \return End iterator for non-debug register def/use bundle walks.
  static reg_bundle_nodbg_iterator reg_bundle_nodbg_end() {
    return reg_bundle_nodbg_iterator(nullptr);
  }

  /// Return a range over all non-debug bundles that def or use \p Reg.
  ///
  /// \param Reg Register whose non-debug def/use bundles are iterated.
  /// \return Range over all non-debug bundles that def or use \p Reg.
  inline iterator_range<reg_bundle_nodbg_iterator>
  reg_nodbg_bundles(Register Reg) const {
    return make_range(reg_bundle_nodbg_begin(Reg), reg_bundle_nodbg_end());
  }

  /// reg_nodbg_empty - Return true if the only instructions using or defining
  /// Reg are Debug instructions.
  ///
  /// \param RegNo Register to test for non-debug defs or uses.
  /// \return True if \p RegNo has no non-debug defs or uses.
  bool reg_nodbg_empty(Register RegNo) const {
    return reg_nodbg_begin(RegNo) == reg_nodbg_end();
  }

  /// def_iterator/def_begin/def_end - Walk all defs of the specified register.
  using def_iterator = defusechain_iterator<false, true, false, true, false>;
  /// Return an iterator to the first defining operand of \p RegNo.
  ///
  /// \param RegNo Register whose defining operands are iterated.
  /// \return Iterator to the first defining operand of \p RegNo.
  def_iterator def_begin(Register RegNo) const {
    return def_iterator(getRegUseDefListHead(RegNo));
  }
  /// Return the end iterator for register defining-operand walks.
  ///
  /// \return End iterator for register defining-operand walks.
  static def_iterator def_end() { return def_iterator(nullptr); }

  /// Return a range over all defining operands of \p Reg.
  ///
  /// \param Reg Register whose defining operands are iterated.
  /// \return Range over all defining operands of \p Reg.
  inline iterator_range<def_iterator> def_operands(Register Reg) const {
    return make_range(def_begin(Reg), def_end());
  }

  /// def_instr_iterator/def_instr_begin/def_instr_end - Walk all defs of the
  /// specified register, stepping by MachineInst.
  using def_instr_iterator =
      defusechain_instr_iterator<false, true, false, /*ByInstr=*/true>;
  /// Return an iterator to the first instruction that defines \p RegNo.
  ///
  /// \param RegNo Register whose defining instructions are iterated.
  /// \return Iterator to the first instruction that defines \p RegNo.
  def_instr_iterator def_instr_begin(Register RegNo) const {
    return def_instr_iterator(getRegUseDefListHead(RegNo));
  }
  /// Return the end iterator for register defining-instruction walks.
  ///
  /// \return End iterator for register defining-instruction walks.
  static def_instr_iterator def_instr_end() {
    return def_instr_iterator(nullptr);
  }

  /// Return a range over all instructions that define \p Reg.
  ///
  /// \param Reg Register whose defining instructions are iterated.
  /// \return Range over all instructions that define \p Reg.
  inline iterator_range<def_instr_iterator>
  def_instructions(Register Reg) const {
    return make_range(def_instr_begin(Reg), def_instr_end());
  }

  /// def_bundle_iterator/def_bundle_begin/def_bundle_end - Walk all defs of the
  /// specified register, stepping by bundle.
  using def_bundle_iterator =
      defusechain_instr_iterator<false, true, false, /*ByInstr=*/false>;
  /// Return an iterator to the first bundle that defines \p RegNo.
  ///
  /// \param RegNo Register whose defining bundles are iterated.
  /// \return Iterator to the first bundle that defines \p RegNo.
  def_bundle_iterator def_bundle_begin(Register RegNo) const {
    return def_bundle_iterator(getRegUseDefListHead(RegNo));
  }
  /// Return the end iterator for register defining-bundle walks.
  ///
  /// \return End iterator for register defining-bundle walks.
  static def_bundle_iterator def_bundle_end() {
    return def_bundle_iterator(nullptr);
  }

  /// Return a range over all bundles that define \p Reg.
  ///
  /// \param Reg Register whose defining bundles are iterated.
  /// \return Range over all bundles that define \p Reg.
  inline iterator_range<def_bundle_iterator> def_bundles(Register Reg) const {
    return make_range(def_bundle_begin(Reg), def_bundle_end());
  }

  /// def_empty - Return true if there are no instructions defining the
  /// specified register (it may be live-in).
  ///
  /// \param RegNo Register to test for any definitions.
  /// \return True if there are no instructions defining \p RegNo.
  bool def_empty(Register RegNo) const { return def_begin(RegNo) == def_end(); }

  /// Return the debug name of virtual register \p Reg, or an empty string.
  ///
  /// \param Reg Virtual register whose name is requested.
  /// \return Debug name of \p Reg, or an empty string if unnamed.
  StringRef getVRegName(Register Reg) const {
    return VReg2Name.inBounds(Reg) ? StringRef(VReg2Name[Reg]) : "";
  }

  /// Associate debug name \p Name with virtual register \p Reg.
  ///
  /// \param Name Debug name to assign; empty names are ignored.
  /// \param Reg Virtual register receiving the name.
  void insertVRegByName(StringRef Name, Register Reg) {
    assert((Name.empty() || !VRegNames.contains(Name)) &&
           "Named VRegs Must be Unique.");
    if (!Name.empty()) {
      VRegNames.insert(Name);
      VReg2Name.grow(Reg);
      VReg2Name[Reg] = Name.str();
    }
  }

  /// Return true if there is exactly one operand defining the specified
  /// register.
  ///
  /// \param RegNo Register to test for a unique defining operand.
  /// \return True if exactly one operand defines \p RegNo.
  bool hasOneDef(Register RegNo) const {
    return hasSingleElement(def_operands(RegNo));
  }

  /// Returns the defining operand if there is exactly one operand defining the
  /// specified register, otherwise nullptr.
  ///
  /// \param Reg Register whose unique defining operand is requested.
  /// \return The unique defining operand, or nullptr if none or multiple.
  MachineOperand *getOneDef(Register Reg) const {
    def_iterator DI = def_begin(Reg);
    if (DI == def_end()) // No defs.
      return nullptr;

    def_iterator OneDef = DI;
    if (++DI == def_end())
      return &*OneDef;
    return nullptr; // Multiple defs.
  }

  /// use_iterator/use_begin/use_end - Walk all uses of the specified register.
  using use_iterator = defusechain_iterator<true, false, false, true, false>;
  /// Return an iterator to the first use operand of \p RegNo.
  ///
  /// \param RegNo Register whose use operands are iterated.
  /// \return Iterator to the first use operand of \p RegNo.
  use_iterator use_begin(Register RegNo) const {
    return use_iterator(getRegUseDefListHead(RegNo));
  }
  /// Return the end iterator for register use-operand walks.
  ///
  /// \return End iterator for register use-operand walks.
  static use_iterator use_end() { return use_iterator(nullptr); }

  /// Return a range over all use operands of \p Reg.
  ///
  /// \param Reg Register whose use operands are iterated.
  /// \return Range over all use operands of \p Reg.
  inline iterator_range<use_iterator> use_operands(Register Reg) const {
    return make_range(use_begin(Reg), use_end());
  }

  /// use_instr_iterator/use_instr_begin/use_instr_end - Walk all uses of the
  /// specified register, stepping by MachineInstr.
  using use_instr_iterator =
      defusechain_instr_iterator<true, false, false, /*ByInstr=*/true>;
  /// Return an iterator to the first instruction that uses \p RegNo.
  ///
  /// \param RegNo Register whose using instructions are iterated.
  /// \return Iterator to the first instruction that uses \p RegNo.
  use_instr_iterator use_instr_begin(Register RegNo) const {
    return use_instr_iterator(getRegUseDefListHead(RegNo));
  }
  /// Return the end iterator for register using-instruction walks.
  ///
  /// \return End iterator for register using-instruction walks.
  static use_instr_iterator use_instr_end() {
    return use_instr_iterator(nullptr);
  }

  /// Return a range over all instructions that use \p Reg.
  ///
  /// \param Reg Register whose using instructions are iterated.
  /// \return Range over all instructions that use \p Reg.
  inline iterator_range<use_instr_iterator>
  use_instructions(Register Reg) const {
    return make_range(use_instr_begin(Reg), use_instr_end());
  }

  /// use_bundle_iterator/use_bundle_begin/use_bundle_end - Walk all uses of the
  /// specified register, stepping by bundle.
  using use_bundle_iterator =
      defusechain_instr_iterator<true, false, false, /*ByInstr=*/false>;
  /// Return an iterator to the first bundle that uses \p RegNo.
  ///
  /// \param RegNo Register whose using bundles are iterated.
  /// \return Iterator to the first bundle that uses \p RegNo.
  use_bundle_iterator use_bundle_begin(Register RegNo) const {
    return use_bundle_iterator(getRegUseDefListHead(RegNo));
  }
  /// Return the end iterator for register using-bundle walks.
  ///
  /// \return End iterator for register using-bundle walks.
  static use_bundle_iterator use_bundle_end() {
    return use_bundle_iterator(nullptr);
  }

  /// Return a range over all bundles that use \p Reg.
  ///
  /// \param Reg Register whose using bundles are iterated.
  /// \return Range over all bundles that use \p Reg.
  inline iterator_range<use_bundle_iterator> use_bundles(Register Reg) const {
    return make_range(use_bundle_begin(Reg), use_bundle_end());
  }

  /// use_empty - Return true if there are no instructions using the specified
  /// register.
  ///
  /// \param RegNo Register to test for any uses.
  /// \return True if there are no instructions using \p RegNo.
  bool use_empty(Register RegNo) const { return use_begin(RegNo) == use_end(); }

  /// hasOneUse - Return true if there is exactly one instruction using the
  /// specified register.
  ///
  /// \param RegNo Register to test for a unique use.
  /// \return True if exactly one instruction uses \p RegNo.
  bool hasOneUse(Register RegNo) const {
    MachineOperand *Head = getRegUseDefListHead(RegNo);
    if (!Head)
      return false;
    // Prev links are circular, and defs always precede uses.
    MachineOperand *Tail = Head->Contents.Reg.Prev;
    if (!Tail->isUse())
      return false;
    if (Tail == Head)
      return true;
    return Tail->Contents.Reg.Prev->isDef();
  }

  /// use_nodbg_iterator/use_nodbg_begin/use_nodbg_end - Walk all uses of the
  /// specified register, skipping those marked as Debug.
  using use_nodbg_iterator =
      defusechain_iterator<true, false, true, true, false>;
  /// Return an iterator to the first non-debug use operand of \p RegNo.
  ///
  /// \param RegNo Register whose non-debug use operands are iterated.
  /// \return Iterator to the first non-debug use operand of \p RegNo.
  use_nodbg_iterator use_nodbg_begin(Register RegNo) const {
    return use_nodbg_iterator(getRegUseDefListHead(RegNo));
  }
  /// Return the end iterator for non-debug register use-operand walks.
  ///
  /// \return End iterator for non-debug register use-operand walks.
  static use_nodbg_iterator use_nodbg_end() {
    return use_nodbg_iterator(nullptr);
  }

  /// Return a range over all non-debug use operands of \p Reg.
  ///
  /// \param Reg Register whose non-debug use operands are iterated.
  /// \return Range over all non-debug use operands of \p Reg.
  inline iterator_range<use_nodbg_iterator>
  use_nodbg_operands(Register Reg) const {
    return make_range(use_nodbg_begin(Reg), use_nodbg_end());
  }

  /// Iterator over non-debug uses of a register, stepping by MachineInstr.
  ///
  /// Walk all uses of the specified register, stepping by MachineInstr,
  /// skipping those marked as Debug.
  using use_instr_nodbg_iterator =
      defusechain_instr_iterator<true, false, true, /*ByInstr=*/true>;
  /// Return an iterator to the first non-debug instruction that uses \p RegNo.
  ///
  /// \param RegNo Register whose non-debug using instructions are iterated.
  /// \return Iterator to the first non-debug instruction that uses \p RegNo.
  use_instr_nodbg_iterator use_instr_nodbg_begin(Register RegNo) const {
    return use_instr_nodbg_iterator(getRegUseDefListHead(RegNo));
  }
  /// Return the end iterator for non-debug register using-instruction walks.
  ///
  /// \return End iterator for non-debug register using-instruction walks.
  static use_instr_nodbg_iterator use_instr_nodbg_end() {
    return use_instr_nodbg_iterator(nullptr);
  }

  /// Return a range over all non-debug instructions that use \p Reg.
  ///
  /// \param Reg Register whose non-debug using instructions are iterated.
  /// \return Range over all non-debug instructions that use \p Reg.
  inline iterator_range<use_instr_nodbg_iterator>
  use_nodbg_instructions(Register Reg) const {
    return make_range(use_instr_nodbg_begin(Reg), use_instr_nodbg_end());
  }

  /// Iterator over non-debug uses of a register, stepping by bundle.
  ///
  /// Walk all uses of the specified register, stepping by bundle, skipping
  /// those marked as Debug.
  using use_bundle_nodbg_iterator =
      defusechain_instr_iterator<true, false, true, /*ByInstr=*/false>;
  /// Return an iterator to the first non-debug bundle that uses \p RegNo.
  ///
  /// \param RegNo Register whose non-debug using bundles are iterated.
  /// \return Iterator to the first non-debug bundle that uses \p RegNo.
  use_bundle_nodbg_iterator use_bundle_nodbg_begin(Register RegNo) const {
    return use_bundle_nodbg_iterator(getRegUseDefListHead(RegNo));
  }
  /// Return the end iterator for non-debug register using-bundle walks.
  ///
  /// \return End iterator for non-debug register using-bundle walks.
  static use_bundle_nodbg_iterator use_bundle_nodbg_end() {
    return use_bundle_nodbg_iterator(nullptr);
  }

  /// Return a range over all non-debug bundles that use \p Reg.
  ///
  /// \param Reg Register whose non-debug using bundles are iterated.
  /// \return Range over all non-debug bundles that use \p Reg.
  inline iterator_range<use_bundle_nodbg_iterator>
  use_nodbg_bundles(Register Reg) const {
    return make_range(use_bundle_nodbg_begin(Reg), use_bundle_nodbg_end());
  }

  /// use_nodbg_empty - Return true if there are no non-Debug instructions
  /// using the specified register.
  ///
  /// \param RegNo Register to test for non-debug uses.
  /// \return True if there are no non-debug instructions using \p RegNo.
  bool use_nodbg_empty(Register RegNo) const {
    return use_nodbg_begin(RegNo) == use_nodbg_end();
  }

  /// hasOneNonDBGUse - Return true if there is exactly one non-Debug
  /// use of the specified register.
  ///
  /// \param RegNo Register to test for a unique non-debug use.
  /// \return True if exactly one non-debug use of \p RegNo exists.
  LLVM_ABI bool hasOneNonDBGUse(Register RegNo) const;

  /// hasOneNonDBGUse - Return true if there is exactly one non-Debug
  /// instruction using the specified register. Said instruction may have
  /// multiple uses.
  ///
  /// \param RegNo Register to test for a unique non-debug user instruction.
  /// \return True if exactly one non-debug instruction uses \p RegNo.
  LLVM_ABI bool hasOneNonDBGUser(Register RegNo) const;

  /// If the register has a single non-Debug use, returns it; otherwise returns
  /// nullptr.
  ///
  /// \param RegNo Register whose unique non-debug use is requested.
  /// \return The unique non-debug use operand, or nullptr if none or multiple.
  LLVM_ABI MachineOperand *getOneNonDBGUse(Register RegNo) const;

  /// If the register has a single non-Debug instruction using the specified
  /// register, returns it; otherwise returns nullptr.
  ///
  /// \param RegNo Register whose unique non-debug user instruction is
  ///              requested.
  /// \return The unique non-debug user instruction, or nullptr if none or
  ///         multiple.
  LLVM_ABI MachineInstr *getOneNonDBGUser(Register RegNo) const;

  /// hasAtMostUses - Return true if the given register has at most \p MaxUsers
  /// non-debug user instructions.
  ///
  /// \param Reg Register whose non-debug users are counted.
  /// \param MaxUsers Maximum number of non-debug user instructions allowed.
  /// \return True if \p Reg has at most \p MaxUsers non-debug user instructions.
  LLVM_ABI bool hasAtMostUserInstrs(Register Reg, unsigned MaxUsers) const;

  /// Replace all instances of FromReg with ToReg in the machine function.
  ///
  /// This is like llvm-level X->replaceAllUsesWith(Y), except that it also
  /// changes any definitions of the register as well.
  ///
  /// Note that it is usually necessary to first constrain ToReg's register
  /// class and register bank to match the FromReg constraints using one of the
  /// methods:
  ///
  ///   constrainRegClass(ToReg, getRegClass(FromReg))
  ///   constrainRegAttrs(ToReg, FromReg)
  ///   RegisterBankInfo::constrainGenericRegister(ToReg,
  ///       *MRI.getRegClass(FromReg), MRI)
  ///
  /// These functions will return a falsy result if the virtual registers have
  /// incompatible constraints.
  ///
  /// Note that if ToReg is a physical register the function will replace and
  /// apply sub registers to ToReg in order to obtain a final/proper physical
  /// register.
  ///
  /// \param FromReg Register being replaced.
  /// \param ToReg Register that replaces \p FromReg.
  LLVM_ABI void replaceRegWith(Register FromReg, Register ToReg);

  /// Return the defining instruction of a virtual register in SSA form.
  ///
  /// Return the machine instr that defines the specified virtual register or
  /// null if none is found. This assumes that the code is in SSA form, so there
  /// should only be one definition.
  ///
  /// \param Reg Virtual register whose defining instruction is requested.
  /// \return Defining instruction of \p Reg, or null if none is found.
  LLVM_ABI LLVM_READONLY MachineInstr *getVRegDef(Register Reg) const;

  /// Return the unique defining instruction of a virtual register, or null.
  ///
  /// Return the unique machine instr that defines the specified virtual
  /// register or null if none is found. If there are multiple definitions or no
  /// definition, return null.
  ///
  /// \param Reg Virtual register whose unique defining instruction is
  ///            requested.
  /// \return Unique defining instruction of \p Reg, or null if none or multiple.
  LLVM_ABI LLVM_READONLY MachineInstr *getUniqueVRegDef(Register Reg) const;

  /// Return the machine basic block in which the specified virtual register is
  /// defined, or null if it has no definition. This assumes SSA form.
  ///
  /// \param Reg Virtual register whose defining block is requested.
  /// \return Defining basic block of \p Reg, or null if it has no definition.
  MachineBasicBlock *getDefBlock(Register Reg) const {
    MachineInstr *DefMI = getVRegDef(Reg);
    return DefMI ? DefMI->getParent() : nullptr;
  }

  /// Clear kill flags on all uses of the given register.
  ///
  /// Iterate over all the uses of the given register and clear the kill flag
  /// from the MachineOperand. This function is used by optimization passes
  /// which extend register lifetimes and need only preserve conservative kill
  /// flag information.
  ///
  /// \param Reg Register whose use kill flags are cleared.
  LLVM_ABI void clearKillFlags(Register Reg) const;

  /// Dump all uses of \p RegNo to the debug stream.
  ///
  /// \param RegNo Register whose uses are dumped.
  LLVM_ABI void dumpUses(Register RegNo) const;

  /// Returns true if PhysReg is unallocatable and constant throughout the
  /// function. Writing to a constant register has no effect.
  ///
  /// \param PhysReg Physical register to test for constant status.
  /// \return True if \p PhysReg is unallocatable and constant.
  LLVM_ABI bool isConstantPhysReg(MCRegister PhysReg) const;

  /// Get an iterator over the pressure sets affected by the virtual register
  /// or register unit.
  ///
  /// \param VRegOrUnit Virtual register or register unit whose pressure sets
  ///                   are iterated.
  /// \return Iterator over pressure sets affected by \p VRegOrUnit.
  PSetIterator getPressureSets(VirtRegOrUnit VRegOrUnit) const;

  //===--------------------------------------------------------------------===//
  // Virtual Register Info
  //===--------------------------------------------------------------------===//

  /// Return the register class of the specified virtual register.
  /// This shouldn't be used directly unless \p Reg has a register class.
  /// \see getRegClassOrNull when this might happen.
  ///
  /// \param Reg Virtual register whose class is requested.
  /// \return Register class of \p Reg.
  const TargetRegisterClass *getRegClass(Register Reg) const {
    assert(isa<const TargetRegisterClass *>(VRegInfo[Reg.id()].first) &&
           "Register class not set, wrong accessor");
    return cast<const TargetRegisterClass *>(VRegInfo[Reg.id()].first);
  }

  /// Return the register class of \p Reg, or null if Reg has not been assigned
  /// a register class yet.
  ///
  /// \note A null register class can only happen when these two
  /// conditions are met:
  /// 1. Generic virtual registers are created.
  /// 2. The machine function has not completely been through the
  ///    instruction selection process.
  /// None of this condition is possible without GlobalISel for now.
  /// In other words, if GlobalISel is not used or if the query happens after
  /// the select pass, using getRegClass is safe.
  ///
  /// \param Reg Virtual register whose class is requested.
  /// \return Register class of \p Reg, or null if none is assigned yet.
  const TargetRegisterClass *getRegClassOrNull(Register Reg) const {
    const RegClassOrRegBank &Val = VRegInfo[Reg].first;
    return dyn_cast_if_present<const TargetRegisterClass *>(Val);
  }

  /// Return the register bank of \p Reg.
  /// This shouldn't be used directly unless \p Reg has a register bank.
  ///
  /// \param Reg Virtual register whose bank is requested.
  /// \return Register bank of \p Reg.
  const RegisterBank *getRegBank(Register Reg) const {
    return cast<const RegisterBank *>(VRegInfo[Reg.id()].first);
  }

  /// Return the register bank of \p Reg, or null if Reg has not been assigned
  /// a register bank or has been assigned a register class.
  /// \note It is possible to get the register bank from the register class via
  /// RegisterBankInfo::getRegBankFromRegClass.
  ///
  /// \param Reg Virtual register whose bank is requested.
  /// \return Register bank of \p Reg, or null if unset or a class is assigned.
  const RegisterBank *getRegBankOrNull(Register Reg) const {
    const RegClassOrRegBank &Val = VRegInfo[Reg].first;
    return dyn_cast_if_present<const RegisterBank *>(Val);
  }

  /// Return the register bank or register class of \p Reg.
  /// \note Before the register bank gets assigned (i.e., before the
  /// RegBankSelect pass) \p Reg may not have either.
  ///
  /// \param Reg Virtual register whose class or bank is requested.
  /// \return Register class or bank currently assigned to \p Reg.
  const RegClassOrRegBank &getRegClassOrRegBank(Register Reg) const {
    return VRegInfo[Reg].first;
  }

  /// setRegClass - Set the register class of the specified virtual register.
  ///
  /// \param Reg Virtual register whose class is set.
  /// \param RC Register class to assign.
  LLVM_ABI void setRegClass(Register Reg, const TargetRegisterClass *RC);

  /// Set the register bank to \p RegBank for \p Reg.
  ///
  /// \param Reg Virtual register whose bank is set.
  /// \param RegBank Register bank to assign.
  LLVM_ABI void setRegBank(Register Reg, const RegisterBank &RegBank);

  /// Set the register class or register bank of \p Reg to \p RCOrRB.
  ///
  /// \param Reg Virtual register whose class or bank is set.
  /// \param RCOrRB Register class or bank to assign.
  void setRegClassOrRegBank(Register Reg,
                            const RegClassOrRegBank &RCOrRB){
    VRegInfo[Reg].first = RCOrRB;
  }

  /// Constrain a virtual register to a common subclass with \p RC.
  ///
  /// Constrain the register class of the specified virtual register to be a
  /// common subclass of RC and the current register class, but only if the new
  /// class has at least MinNumRegs registers. Return the new register class, or
  /// NULL if no such class exists. This should only be used when the constraint
  /// is known to be trivial, like GR32 -> GR32_NOSP. Beware of increasing
  /// register pressure.
  ///
  /// \note Assumes that the register has a register class assigned.
  /// Use RegisterBankInfo::constrainGenericRegister in GlobalISel's
  /// InstructionSelect pass and constrainRegAttrs in every other pass,
  /// including non-select passes of GlobalISel, instead.
  ///
  /// \param Reg Virtual register whose class is constrained.
  /// \param RC Register class to intersect with the current class.
  /// \param MinNumRegs Minimum number of registers the new class must have.
  /// \return New constrained register class, or null if none exists.
  LLVM_ABI const TargetRegisterClass *
  constrainRegClass(Register Reg, const TargetRegisterClass *RC,
                    unsigned MinNumRegs = 0);

  /// Constrain register class/bank and type of \p Reg to match another register.
  ///
  /// Constrain the register class or the register bank of the virtual register
  /// \p Reg (and low-level type) to be a common subclass or a common bank of
  /// both registers provided respectively (and a common low-level type). Do
  /// nothing if any of the attributes (classes, banks, or low-level types) of
  /// the registers are deemed incompatible, or if the resulting register will
  /// have a class smaller than before and of size less than \p MinNumRegs.
  /// Return true if such register attributes exist, false otherwise.
  ///
  /// \note Use this method instead of constrainRegClass and
  /// RegisterBankInfo::constrainGenericRegister everywhere but SelectionDAG
  /// ISel / FastISel and GlobalISel's InstructionSelect pass respectively.
  ///
  /// \param Reg Virtual register whose attributes are constrained.
  /// \param ConstrainingReg Register providing the constraining attributes.
  /// \param MinNumRegs Minimum number of registers the new class must have.
  /// \return True if compatible constrained attributes were applied.
  LLVM_ABI bool constrainRegAttrs(Register Reg, Register ConstrainingReg,
                                  unsigned MinNumRegs = 0);

  /// Recompute a legal super-class for \p Reg after constraints are relaxed.
  ///
  /// Try to find a legal super-class of Reg's register class that still
  /// satisfies the constraints from the instructions using Reg. Returns true
  /// if Reg was upgraded.
  ///
  /// This method can be used after constraints have been removed from a
  /// virtual register, for example after removing instructions or splitting
  /// the live range.
  ///
  /// \param Reg Virtual register whose class may be upgraded.
  /// \return True if \p Reg was upgraded to a larger legal class.
  LLVM_ABI bool recomputeRegClass(Register Reg);

  /// createVirtualRegister - Create and return a new virtual register in the
  /// function with the specified register class.
  ///
  /// \param RegClass Register class of the new virtual register.
  /// \param Name Optional debug name for the new virtual register.
  /// \return Newly created virtual register with class \p RegClass.
  LLVM_ABI Register createVirtualRegister(const TargetRegisterClass *RegClass,
                                          StringRef Name = "");

  /// All attributes(register class or bank and low-level type) a virtual
  /// register can have.
  struct VRegAttrs {
    /// Register class or register bank assigned to the virtual register.
    RegClassOrRegBank RCOrRB;
    /// Low-level type of the virtual register, or LLT{} if unset.
    LLT Ty;
  };

  /// Returns register class or bank and low level type of \p Reg. Always safe
  /// to use. Special values are returned when \p Reg does not have some of the
  /// attributes.
  ///
  /// \param Reg Virtual register whose attributes are requested.
  /// \return Class/bank and low-level type attributes of \p Reg.
  VRegAttrs getVRegAttrs(Register Reg) const {
    return {getRegClassOrRegBank(Reg), getType(Reg)};
  }

  /// Create and return a new virtual register in the function with the
  /// specified register attributes(register class or bank and low level type).
  ///
  /// \param RegAttr Class/bank and type attributes for the new register.
  /// \param Name Optional debug name for the new virtual register.
  /// \return Newly created virtual register with attributes \p RegAttr.
  LLVM_ABI Register createVirtualRegister(VRegAttrs RegAttr,
                                          StringRef Name = "");

  /// Create and return a new virtual register in the function with the same
  /// attributes as the given register.
  ///
  /// \param VReg Virtual register whose attributes are cloned.
  /// \param Name Optional debug name for the new virtual register.
  /// \return Newly created virtual register cloning attributes of \p VReg.
  LLVM_ABI Register cloneVirtualRegister(Register VReg, StringRef Name = "");

  /// Get the low-level type of \p Reg or LLT{} if Reg is not a generic
  /// (target independent) virtual register.
  ///
  /// \param Reg Register whose low-level type is requested.
  /// \return Low-level type of \p Reg, or LLT{} if unset or not generic.
  LLT getType(Register Reg) const {
    if (Reg.isVirtual() && VRegToType.inBounds(Reg))
      return VRegToType[Reg];
    return LLT{};
  }

  /// Set the low-level type of \p VReg to \p Ty.
  ///
  /// \param VReg Virtual register whose type is set.
  /// \param Ty Low-level type to assign.
  LLVM_ABI void setType(Register VReg, LLT Ty);

  /// Create and return a new generic virtual register with low-level
  /// type \p Ty.
  ///
  /// \param Ty Low-level type of the new generic virtual register.
  /// \param Name Optional debug name for the new virtual register.
  /// \return Newly created generic virtual register of type \p Ty.
  LLVM_ABI Register createGenericVirtualRegister(LLT Ty, StringRef Name = "");

  /// Remove all types associated to virtual registers (after instruction
  /// selection and constraining of all generic virtual registers).
  LLVM_ABI void clearVirtRegTypes();

  /// Create a virtual register with no class, bank, or size assigned yet.
  ///
  /// This is only allowed to be used temporarily while constructing machine
  /// instructions. Most operations are undefined on an incomplete register
  /// until one of setRegClass(), setRegBank() or setSize() has been called on
  /// it.
  ///
  /// \param Name Optional debug name for the new virtual register.
  /// \return Newly created incomplete virtual register.
  LLVM_ABI Register createIncompleteVirtualRegister(StringRef Name = "");

  /// getNumVirtRegs - Return the number of virtual registers created.
  ///
  /// \return Number of virtual registers created in this function.
  unsigned getNumVirtRegs() const { return VRegInfo.size(); }

  /// clearVirtRegs - Remove all virtual registers (after physreg assignment).
  LLVM_ABI void clearVirtRegs();

  /// Record a VirtRegMap entry to be applied when VirtRegMap is initialized.
  ///
  /// \param Entry Pending mapping from a virtual register to split source and
  ///              assigned physical register.
  void addPendingVirtRegMapEntry(PendingVirtRegMapEntry Entry) {
    assert(Entry.VReg.isVirtual());
    assert(!Entry.SplitFrom.isValid() || Entry.SplitFrom.isVirtual());
    assert(!Entry.AssignedPhys.isValid() || Entry.AssignedPhys.isPhysical());
    PendingVirtRegMapEntries.push_back(Entry);
  }

  /// Return the pending VirtRegMap entries waiting to be consumed.
  ///
  /// \return Pending VirtRegMap entries waiting to be consumed.
  ArrayRef<PendingVirtRegMapEntry> getPendingVirtRegMapEntries() const {
    return PendingVirtRegMapEntries;
  }

  /// Clear all pending VirtRegMap entries.
  void clearPendingVirtRegMapEntries() { PendingVirtRegMapEntries.clear(); }

  /// Copy pending VirtRegMap entries from another MachineRegisterInfo.
  ///
  /// \param Other Source register info whose pending entries are copied.
  void copyPendingVirtRegMapEntriesFrom(const MachineRegisterInfo &Other) {
    assert(getNumVirtRegs() == Other.getNumVirtRegs() &&
           "expected MachineFunction clone to preserve virtual registers");
    PendingVirtRegMapEntries = Other.PendingVirtRegMapEntries;
  }

  /// Specify a register allocation hint for the given virtual register.
  ///
  /// This is typically used by the target, and in case of an earlier hint it
  /// will be overwritten.
  ///
  /// \param VReg Virtual register receiving the hint.
  /// \param Type Hint type; non-zero values are target-specific.
  /// \param PrefReg Preferred register suggested by the hint.
  void setRegAllocationHint(Register VReg, unsigned Type, Register PrefReg) {
    assert(VReg.isVirtual());
    RegAllocHints.grow(Register::index2VirtReg(getNumVirtRegs()));
    auto &Hint = RegAllocHints[VReg];
    Hint.first = Type;
    Hint.second.clear();
    Hint.second.push_back(PrefReg);
  }

  /// addRegAllocationHint - Add a register allocation hint to the hints
  /// vector for VReg.
  ///
  /// \param VReg Virtual register receiving the hint.
  /// \param PrefReg Preferred register to append to the hint list.
  void addRegAllocationHint(Register VReg, Register PrefReg) {
    assert(VReg.isVirtual());
    RegAllocHints.grow(Register::index2VirtReg(getNumVirtRegs()));
    RegAllocHints[VReg].second.push_back(PrefReg);
  }

  /// Specify the preferred (target independent) register allocation hint for
  /// the specified virtual register.
  ///
  /// \param VReg Virtual register receiving the hint.
  /// \param PrefReg Preferred register suggested by the hint.
  void setSimpleHint(Register VReg, Register PrefReg) {
    setRegAllocationHint(VReg, /*Type=*/0, PrefReg);
  }

  /// Clear the target-independent register allocation hints for \p VReg.
  ///
  /// \param VReg Virtual register whose simple hints are cleared.
  void clearSimpleHint(Register VReg) {
    assert (!RegAllocHints[VReg].first &&
            "Expected to clear a non-target hint!");
    if (RegAllocHints.inBounds(VReg))
      RegAllocHints[VReg].second.clear();
  }

  /// Return the strongest register allocation hint for a virtual register.
  ///
  /// If there are many hints, this returns the one with the greatest weight.
  ///
  /// \param VReg Virtual register whose hint is requested.
  /// \return Hint type and preferred register for \p VReg.
  std::pair<unsigned, Register> getRegAllocationHint(Register VReg) const {
    assert(VReg.isVirtual());
    if (!RegAllocHints.inBounds(VReg))
      return {0, Register()};
    auto &Hint = RegAllocHints[VReg.id()];
    Register BestHint = (Hint.second.size() ? Hint.second[0] : Register());
    return {Hint.first, BestHint};
  }

  /// getSimpleHint - same as getRegAllocationHint except it will only return
  /// a target independent hint.
  ///
  /// \param VReg Virtual register whose simple hint is requested.
  /// \return Preferred register for a target-independent hint, or an invalid
  ///         register if the hint is target-specific or unset.
  Register getSimpleHint(Register VReg) const {
    assert(VReg.isVirtual());
    std::pair<unsigned, Register> Hint = getRegAllocationHint(VReg);
    return Hint.first ? Register() : Hint.second;
  }

  /// getRegAllocationHints - Return a reference to the vector of all
  /// register allocation hints for VReg.
  ///
  /// \param VReg Virtual register whose hints are requested.
  /// \return Pointer to the hint type and preferred-register list, or null.
  const std::pair<unsigned, SmallVector<Register, 4>> *
  getRegAllocationHints(Register VReg) const {
    assert(VReg.isVirtual());
    return RegAllocHints.inBounds(VReg) ? &RegAllocHints[VReg] : nullptr;
  }

  /// Mark DBG_VALUE uses of a register as undefined so they are deleted later.
  ///
  /// Mark every DBG_VALUE referencing the specified register as undefined
  /// which causes the DBG_VALUE to be deleted during LiveDebugVariables
  /// analysis.
  ///
  /// \param Reg Register whose debug uses are marked undefined.
  LLVM_ABI void markUsesInDebugValueAsUndef(Register Reg) const;

  /// updateDbgUsersToReg - Update a collection of debug instructions
  /// to refer to the designated register.
  ///
  /// \param OldReg Register currently referenced by the debug users.
  /// \param NewReg Register that should replace \p OldReg in the users.
  /// \param Users Debug instructions to update.
  LLVM_ABI void updateDbgUsersToReg(MCRegister OldReg, MCRegister NewReg,
                                    ArrayRef<MachineInstr *> Users) const;

  /// Return true if the specified register is modified in this function.
  ///
  /// This checks that no defining machine operands exist for the register or
  /// any of its aliases. Definitions found on functions marked noreturn are
  /// ignored, to consider them pass 'true' for optional parameter
  /// SkipNoReturnDef. The register is also considered modified when it is set
  /// in the UsedPhysRegMask.
  ///
  /// \param PhysReg Physical register to test for modification.
  /// \param SkipNoReturnDef If true, ignore definitions on noreturn calls.
  /// \return True if \p PhysReg is modified in this function.
  LLVM_ABI bool isPhysRegModified(MCRegister PhysReg,
                                  bool SkipNoReturnDef = false) const;

  /// Return true if the specified register is modified or read in this
  /// function.
  ///
  /// This checks that no machine operands exist for the register or any of its
  /// aliases. If SkipRegMaskTest is false, the register is considered used when
  /// it is set in the UsedPhysRegMask.
  ///
  /// \param PhysReg Physical register to test for use or modification.
  /// \param SkipRegMaskTest If true, ignore the UsedPhysRegMask bit test.
  /// \return True if \p PhysReg is modified or read in this function.
  LLVM_ABI bool isPhysRegUsed(MCRegister PhysReg,
                              bool SkipRegMaskTest = false) const;

  /// addPhysRegsUsedFromRegMask - Mark any registers not in RegMask as used.
  /// This corresponds to the bit mask attached to register mask operands.
  ///
  /// \param RegMask Register mask whose clobbered registers are marked used.
  void addPhysRegsUsedFromRegMask(const uint32_t *RegMask) {
    UsedPhysRegMask.setBitsNotInMask(RegMask);
  }

  /// Return the bit vector of physical registers marked used via regmasks.
  ///
  /// \return Bit vector of physical registers marked used via regmasks.
  const BitVector &getUsedPhysRegsMask() const { return UsedPhysRegMask; }

  //===--------------------------------------------------------------------===//
  // Reserved Register Info
  //===--------------------------------------------------------------------===//
  //
  // The set of reserved registers must be invariant during register
  // allocation.  For example, the target cannot suddenly decide it needs a
  // frame pointer when the register allocator has already used the frame
  // pointer register for something else.
  //
  // These methods can be used by target hooks like hasFP() to avoid changing
  // the reserved register set during register allocation.

  /// freezeReservedRegs - Called by the register allocator to freeze the set
  /// of reserved registers before allocation begins.
  LLVM_ABI void freezeReservedRegs();

  /// Mark a register as reserved so allocatable checks will reject it.
  ///
  /// This should not be used during the middle of a function walk, or when
  /// liveness info is available.
  ///
  /// \param PhysReg Physical register (and aliases) to reserve.
  /// \param TRI Target register info used to walk aliases of \p PhysReg.
  void reserveReg(MCRegister PhysReg, const TargetRegisterInfo *TRI) {
    assert(reservedRegsFrozen() &&
           "Reserved registers haven't been frozen yet. ");
    MCRegAliasIterator R(PhysReg, TRI, true);

    for (; R.isValid(); ++R)
      ReservedRegs.set((*R).id());
  }

  /// reservedRegsFrozen - Returns true after freezeReservedRegs() was called
  /// to ensure the set of reserved registers stays constant.
  ///
  /// \return True after freezeReservedRegs() has frozen the reserved set.
  bool reservedRegsFrozen() const {
    return !ReservedRegs.empty();
  }

  /// canReserveReg - Returns true if PhysReg can be used as a reserved
  /// register.  Any register can be reserved before freezeReservedRegs() is
  /// called.
  ///
  /// \param PhysReg Physical register to test for reservability.
  /// \return True if \p PhysReg can still be reserved.
  bool canReserveReg(MCRegister PhysReg) const {
    return !reservedRegsFrozen() || ReservedRegs.test(PhysReg.id());
  }

  /// Return a reference to the frozen set of reserved registers.
  ///
  /// This method should always be preferred to calling TRI::getReservedRegs()
  /// when possible.
  ///
  /// \return Frozen bit vector of reserved physical registers.
  const BitVector &getReservedRegs() const {
    assert(reservedRegsFrozen() &&
           "Reserved registers haven't been frozen yet. "
           "Use TRI::getReservedRegs().");
    return ReservedRegs;
  }

  /// isReserved - Returns true when PhysReg is a reserved register.
  ///
  /// Reserved registers may belong to an allocatable register class, but the
  /// target has explicitly requested that they are not used.
  ///
  /// \param PhysReg Physical register to test for reserved status.
  /// \return True when \p PhysReg is a reserved register.
  bool isReserved(MCRegister PhysReg) const {
    return getReservedRegs().test(PhysReg.id());
  }

  /// Returns true when the given register unit is considered reserved.
  ///
  /// Register units are considered reserved when for at least one of their
  /// root registers, the root register and all super registers are reserved.
  /// This currently iterates the register hierarchy and may be slower than
  /// expected.
  ///
  /// \param Unit Register unit to test for reserved status.
  /// \return True when \p Unit is considered reserved.
  LLVM_ABI bool isReservedRegUnit(MCRegUnit Unit) const;

  /// isAllocatable - Returns true when PhysReg belongs to an allocatable
  /// register class and it hasn't been reserved.
  ///
  /// Allocatable registers may show up in the allocation order of some virtual
  /// register, so a register allocator needs to track its liveness and
  /// availability.
  ///
  /// \param PhysReg Physical register to test for allocatability.
  /// \return True when \p PhysReg is allocatable and not reserved.
  bool isAllocatable(MCRegister PhysReg) const {
    return getTargetRegisterInfo()->isInAllocatableClass(PhysReg) &&
      !isReserved(PhysReg);
  }

  //===--------------------------------------------------------------------===//
  // LiveIn Management
  //===--------------------------------------------------------------------===//

  /// addLiveIn - Add the specified register as a live-in.  Note that it
  /// is an error to add the same register to the same set more than once.
  ///
  /// \param Reg Physical register that is live into the function.
  /// \param vreg Optional virtual register associated with the live-in.
  void addLiveIn(MCRegister Reg, Register vreg = Register()) {
    LiveIns.push_back(std::make_pair(Reg, vreg));
  }

  // Iteration support for the live-ins set.  It's kept in sorted order
  // by register number.
  /// Iterator over live-in physical/virtual register pairs.
  using livein_iterator =
      std::vector<std::pair<MCRegister,Register>>::const_iterator;
  /// Return an iterator to the beginning of the live-in set.
  ///
  /// \return Iterator to the beginning of the live-in set.
  livein_iterator livein_begin() const { return LiveIns.begin(); }
  /// Return an iterator to the end of the live-in set.
  ///
  /// \return Iterator to the end of the live-in set.
  livein_iterator livein_end()   const { return LiveIns.end(); }
  /// Return true if the live-in set is empty.
  ///
  /// \return True if the live-in set is empty.
  bool            livein_empty() const { return LiveIns.empty(); }

  /// Return the live-in physical/virtual register pairs for this function.
  ///
  /// \return Live-in physical/virtual register pairs for this function.
  ArrayRef<std::pair<MCRegister, Register>> liveins() const {
    return LiveIns;
  }

  /// Return true if \p Reg is a live-in register of this function.
  ///
  /// \param Reg Physical or virtual register to test.
  /// \return True if \p Reg is a live-in register of this function.
  LLVM_ABI bool isLiveIn(Register Reg) const;

  /// getLiveInPhysReg - If VReg is a live-in virtual register, return the
  /// corresponding live-in physical register.
  ///
  /// \param VReg Live-in virtual register to look up.
  /// \return Live-in physical register corresponding to \p VReg.
  LLVM_ABI MCRegister getLiveInPhysReg(Register VReg) const;

  /// getLiveInVirtReg - If PReg is a live-in physical register, return the
  /// corresponding live-in virtual register.
  ///
  /// \param PReg Live-in physical register to look up.
  /// \return Live-in virtual register corresponding to \p PReg.
  LLVM_ABI Register getLiveInVirtReg(MCRegister PReg) const;

  /// EmitLiveInCopies - Emit copies to initialize livein virtual registers
  /// into the given entry block.
  ///
  /// \param EntryMBB Entry block that receives the live-in copy instructions.
  /// \param TRI Target register info used while emitting copies.
  /// \param TII Target instruction info used while emitting copies.
  LLVM_ABI void EmitLiveInCopies(MachineBasicBlock *EntryMBB,
                                 const TargetRegisterInfo &TRI,
                                 const TargetInstrInfo &TII);

  /// Returns a mask covering all bits that can appear in lane masks of
  /// subregisters of the virtual register @p Reg.
  ///
  /// \param Reg Virtual register whose maximum lane mask is requested.
  /// \return Lane mask covering all possible subregister lanes of \p Reg.
  LLVM_ABI LaneBitmask getMaxLaneMaskForVReg(Register Reg) const;

  /// Iterator over machine operands that use or define a specific register.
  ///
  /// If ReturnUses is true it returns uses of registers, if ReturnDefs is true
  /// it returns defs. If neither are true then you are silly and it always
  /// returns end(). If SkipDebug is true it skips uses marked Debug when
  /// incrementing.
  template <bool ReturnUses, bool ReturnDefs, bool SkipDebug, bool ByOperand,
            bool ByInstr>
  class defusechain_iterator {
    friend class MachineRegisterInfo;
    static_assert(!ByOperand || !ByInstr,
                  "ByOperand and ByInstr are mutually exclusive");

  public:
    /// Category of this forward iterator.
    using iterator_category = std::forward_iterator_tag;
    /// Type of the value obtained by dereferencing this iterator.
    using value_type = MachineOperand;
    /// Type used to represent distances between iterators.
    using difference_type = std::ptrdiff_t;
    /// Pointer to the iterated value type.
    using pointer = value_type *;
    /// Reference to the iterated value type.
    using reference = value_type &;

  private:
    MachineOperand *Op = nullptr;

    explicit defusechain_iterator(MachineOperand *op) : Op(op) {
      // If the first node isn't one we're interested in, advance to one that
      // we are interested in.
      if (op) {
        if ((!ReturnUses && op->isUse()) ||
            (!ReturnDefs && op->isDef()) ||
            (SkipDebug && op->isDebug()))
          advance();
      }
    }

    void advance() {
      assert(Op && "Cannot increment end iterator!");
      Op = getNextOperandForReg(Op);

      // All defs come before the uses, so stop def_iterator early.
      if (!ReturnUses) {
        if (Op) {
          if (Op->isUse())
            Op = nullptr;
          else
            assert(!Op->isDebug() && "Can't have debug defs");
        }
      } else {
        // If this is an operand we don't care about, skip it.
        while (Op && ((!ReturnDefs && Op->isDef()) ||
                      (SkipDebug && Op->isDebug())))
          Op = getNextOperandForReg(Op);
      }
    }

  public:
    /// Construct a default (end) def/use-chain operand iterator.
    defusechain_iterator() = default;

    /// Return true if this iterator equals \p x.
    ///
    /// \param x Iterator to compare against.
    /// \return True if this iterator equals \p x.
    bool operator==(const defusechain_iterator &x) const {
      return Op == x.Op;
    }
    /// Return true if this iterator differs from \p x.
    ///
    /// \param x Iterator to compare against.
    /// \return True if this iterator differs from \p x.
    bool operator!=(const defusechain_iterator &x) const {
      return !operator==(x);
    }

    /// Advance to the next matching operand, instruction, or bundle.
    ///
    /// Forward iteration only.
    ///
    /// \return Reference to this iterator after advancing.
    defusechain_iterator &operator++() {          // Preincrement
      assert(Op && "Cannot increment end iterator!");
      if (ByOperand)
        advance();
      else if (ByInstr) {
        MachineInstr *P = Op->getParent();
        do {
          advance();
        } while (Op && Op->getParent() == P);
      } else {
        MachineBasicBlock::instr_iterator P =
            getBundleStart(Op->getParent()->getIterator());
        do {
          advance();
        } while (Op && getBundleStart(Op->getParent()->getIterator()) == P);
      }

      return *this;
    }
    /// Advance to the next matching operand and return the previous position.
    ///
    /// \param Unused Unused postfix-discriminator parameter.
    /// \return Copy of the iterator before advancing.
    defusechain_iterator operator++(int Unused) {        // Postincrement
      defusechain_iterator tmp = *this; ++*this; return tmp;
    }

    /// getOperandNo - Return the operand # of this MachineOperand in its
    /// MachineInstr.
    ///
    /// \return Operand index of the current MachineOperand in its MachineInstr.
    unsigned getOperandNo() const {
      assert(Op && "Cannot dereference end iterator!");
      return Op - &Op->getParent()->getOperand(0);
    }

    /// Return a reference to the current machine operand.
    ///
    /// \return Reference to the current machine operand.
    MachineOperand &operator*() const {
      assert(Op && "Cannot dereference end iterator!");
      return *Op;
    }

    /// Return a pointer to the current machine operand.
    ///
    /// \return Pointer to the current machine operand.
    MachineOperand *operator->() const {
      assert(Op && "Cannot dereference end iterator!");
      return Op;
    }
  };

  /// Iterator over machine instructions that use or define a specific register.
  ///
  /// If ReturnUses is true it returns uses of registers, if ReturnDefs is true
  /// it returns defs. If neither are true then you are silly and it always
  /// returns end(). If SkipDebug is true it skips uses marked Debug when
  /// incrementing. Steps by MachineInstr when ByInstr is true, otherwise by
  /// bundle.
  template <bool ReturnUses, bool ReturnDefs, bool SkipDebug, bool ByInstr>
  class defusechain_instr_iterator {
    friend class MachineRegisterInfo;

  public:
    /// Category of this forward iterator.
    using iterator_category = std::forward_iterator_tag;
    /// Type of the value obtained by dereferencing this iterator.
    using value_type = MachineInstr;
    /// Type used to represent distances between iterators.
    using difference_type = std::ptrdiff_t;
    /// Pointer to the iterated value type.
    using pointer = value_type *;
    /// Reference to the iterated value type.
    using reference = value_type &;

  private:
    MachineOperand *Op = nullptr;

    explicit defusechain_instr_iterator(MachineOperand *op) : Op(op) {
      // If the first node isn't one we're interested in, advance to one that
      // we are interested in.
      if (op) {
        if ((!ReturnUses && op->isUse()) ||
            (!ReturnDefs && op->isDef()) ||
            (SkipDebug && op->isDebug()))
          advance();
      }
    }

    void advance() {
      assert(Op && "Cannot increment end iterator!");
      Op = getNextOperandForReg(Op);

      // All defs come before the uses, so stop def_iterator early.
      if (!ReturnUses) {
        if (Op) {
          if (Op->isUse())
            Op = nullptr;
          else
            assert(!Op->isDebug() && "Can't have debug defs");
        }
      } else {
        // If this is an operand we don't care about, skip it.
        while (Op && ((!ReturnDefs && Op->isDef()) ||
                      (SkipDebug && Op->isDebug())))
          Op = getNextOperandForReg(Op);
      }
    }

  public:
    /// Construct a default (end) def/use-chain instruction iterator.
    defusechain_instr_iterator() = default;

    /// Return true if this iterator equals \p x.
    ///
    /// \param x Iterator to compare against.
    /// \return True if this iterator equals \p x.
    bool operator==(const defusechain_instr_iterator &x) const {
      return Op == x.Op;
    }
    /// Return true if this iterator differs from \p x.
    ///
    /// \param x Iterator to compare against.
    /// \return True if this iterator differs from \p x.
    bool operator!=(const defusechain_instr_iterator &x) const {
      return !operator==(x);
    }

    /// Advance to the next matching instruction or bundle.
    ///
    /// Forward iteration only.
    ///
    /// \return Reference to this iterator after advancing.
    defusechain_instr_iterator &operator++() {          // Preincrement
      assert(Op && "Cannot increment end iterator!");
      if (ByInstr) {
        MachineInstr *P = Op->getParent();
        do {
          advance();
        } while (Op && Op->getParent() == P);
      } else {
        MachineBasicBlock::instr_iterator P =
            getBundleStart(Op->getParent()->getIterator());
        do {
          advance();
        } while (Op && getBundleStart(Op->getParent()->getIterator()) == P);
      }

      return *this;
    }
    /// Advance to the next matching instruction and return the previous
    /// position.
    ///
    /// \param Unused Unused postfix-discriminator parameter.
    /// \return Copy of the iterator before advancing.
    defusechain_instr_iterator operator++(int Unused) {        // Postincrement
      defusechain_instr_iterator tmp = *this; ++*this; return tmp;
    }

    /// Return a reference to the current machine instruction or bundle.
    ///
    /// \return Reference to the current machine instruction or bundle.
    MachineInstr &operator*() const {
      assert(Op && "Cannot dereference end iterator!");
      if (!ByInstr)
        return *getBundleStart(Op->getParent()->getIterator());
      return *Op->getParent();
    }

    /// Return a pointer to the current machine instruction or bundle.
    ///
    /// \return Pointer to the current machine instruction or bundle.
    MachineInstr *operator->() const { return &operator*(); }
  };
};

/// Iterate over the pressure sets affected by a physical or virtual register.
///
/// If Reg is physical, it must be a register unit (from MCRegUnitIterator).
class PSetIterator {
  const int *PSet = nullptr;
  unsigned Weight = 0;

public:
  /// Construct an empty, invalid pressure-set iterator.
  PSetIterator() = default;

  /// Construct an iterator over pressure sets affected by \p VRegOrUnit.
  ///
  /// \param VRegOrUnit Virtual register or register unit whose pressure sets
  ///                   are iterated.
  /// \param MRI Register info used to resolve classes, units, and weights.
  PSetIterator(VirtRegOrUnit VRegOrUnit, const MachineRegisterInfo *MRI) {
    const TargetRegisterInfo *TRI = MRI->getTargetRegisterInfo();
    if (VRegOrUnit.isVirtualReg()) {
      const TargetRegisterClass *RC =
          MRI->getRegClass(VRegOrUnit.asVirtualReg());
      PSet = TRI->getRegClassPressureSets(RC);
      Weight = TRI->getRegClassWeight(RC).RegWeight;
    } else {
      PSet = TRI->getRegUnitPressureSets(VRegOrUnit.asMCRegUnit());
      Weight = TRI->getRegUnitWeight(VRegOrUnit.asMCRegUnit());
    }
    if (*PSet == -1)
      PSet = nullptr;
  }

  /// Return true if this iterator still points at a valid pressure set.
  ///
  /// \return True if this iterator still points at a valid pressure set.
  bool isValid() const { return PSet; }

  /// Return the register weight contributed to the current pressure set.
  ///
  /// \return Register weight contributed to the current pressure set.
  unsigned getWeight() const { return Weight; }

  /// Return the current pressure set identifier.
  ///
  /// \return Current pressure set identifier.
  unsigned operator*() const { return *PSet; }

  /// Advance to the next pressure set, or become invalid at the end.
  void operator++() {
    assert(isValid() && "Invalid PSetIterator.");
    ++PSet;
    if (*PSet == -1)
      PSet = nullptr;
  }
};

inline PSetIterator
MachineRegisterInfo::getPressureSets(VirtRegOrUnit VRegOrUnit) const {
  return PSetIterator(VRegOrUnit, this);
}

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINEREGISTERINFO_H
