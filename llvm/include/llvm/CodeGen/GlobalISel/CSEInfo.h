//===- llvm/CodeGen/GlobalISel/CSEInfo.h ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// Provides analysis for continuously CSEing during GISel passes.
///
//===----------------------------------------------------------------------===//
#ifndef LLVM_CODEGEN_GLOBALISEL_CSEINFO_H
#define LLVM_CODEGEN_GLOBALISEL_CSEINFO_H

#include "llvm/ADT/FoldingSet.h"
#include "llvm/CodeGen/CSEConfigBase.h"
#include "llvm/CodeGen/GlobalISel/GISelChangeObserver.h"
#include "llvm/CodeGen/GlobalISel/GISelWorkList.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {
class MachineBasicBlock;

/// Wrap a MachineInstr for uniquing in a CSE FoldingSet.
///
/// The tradeoff here is extra memory allocations for UniqueMachineInstr vs
/// making MachineInstr bigger.
class UniqueMachineInstr : public FoldingSetNode {
  friend class GISelCSEInfo;
  const MachineInstr *MI;
  explicit UniqueMachineInstr(const MachineInstr *MI) : MI(MI) {}

public:
  /// Add this instruction's profile bits to \p ID.
  ///
  /// \param ID Folding-set node ID that receives the profile bits.
  LLVM_ABI void Profile(FoldingSetNodeID &ID);
};

/// CSE configuration that enables CSE for a full set of generic opcodes.
class LLVM_ABI CSEConfigFull : public CSEConfigBase {
public:
  /// Virtual destructor.
  ~CSEConfigFull() override = default;
  /// Return whether \p Opc should be CSEd under a full optimization config.
  ///
  /// \param Opc Opcode to check for CSE eligibility.
  /// \return true if \p Opc should be CSEd under a full optimization config.
  bool shouldCSEOpc(unsigned Opc) override;
};

/// CSE configuration that only CSEs constant-like generic opcodes.
class LLVM_ABI CSEConfigConstantOnly : public CSEConfigBase {
public:
  /// Virtual destructor.
  ~CSEConfigConstantOnly() override = default;
  /// Return whether \p Opc should be CSEd under a constant-only config.
  ///
  /// \param Opc Opcode to check for CSE eligibility.
  /// \return true if \p Opc should be CSEd under a constant-only config.
  bool shouldCSEOpc(unsigned Opc) override;
};

/// Return the standard CSEConfig for the given optimization level.
///
/// We have this logic here so targets can make use of it from their derived
/// TargetPassConfig, but can't put this logic into TargetPassConfig directly
/// because the CodeGen library can't depend on GlobalISel.
///
/// \param Level Optimization level used to select the CSE configuration.
/// \return Owned CSE configuration appropriate for \p Level.
LLVM_ABI std::unique_ptr<CSEConfigBase>
getStandardCSEConfigForOpt(CodeGenOptLevel Level);

/// CSE analysis that tracks instructions for common-subexpression elimination.
///
/// This installs itself as a delegate to the MachineFunction to track new
/// instructions as well as deletions. It however will not be able to track
/// instruction mutations. In such cases, recordNewInstruction should be called
/// (for eg inside MachineIRBuilder::recordInsertion). Also because of how just
/// the instruction can be inserted without adding any operands to the
/// instruction, instructions are uniqued and inserted lazily. CSEInfo should
/// assert when trying to enter an incomplete instruction into the CSEMap. There
/// is Opcode level granularity on which instructions can be CSE'd and for now,
/// only Generic instructions are CSEable.
class LLVM_ABI GISelCSEInfo : public GISelChangeObserver {
  // Make it accessible only to CSEMIRBuilder.
  friend class CSEMIRBuilder;

  BumpPtrAllocator UniqueInstrAllocator;
  FoldingSet<UniqueMachineInstr> CSEMap;
  MachineRegisterInfo *MRI = nullptr;
  MachineFunction *MF = nullptr;
  std::unique_ptr<CSEConfigBase> CSEOpt;
  /// Keep a cache of UniqueInstrs for each MachineInstr. In GISel,
  /// often instructions are mutated (while their ID has completely changed).
  /// Whenever mutation happens, invalidate the UniqueMachineInstr for the
  /// MachineInstr
  DenseMap<const MachineInstr *, UniqueMachineInstr *> InstrMapping;

  /// Store instructions that are not fully formed in TemporaryInsts.
  /// Also because CSE insertion happens lazily, we can remove insts from this
  /// list and avoid inserting and then removing from the CSEMap.
  GISelWorkList<8> TemporaryInsts;

  // Only used in asserts.
  DenseMap<unsigned, unsigned> OpcodeHitTable;

  bool isUniqueMachineInstValid(const UniqueMachineInstr &UMI) const;

  void invalidateUniqueMachineInstr(UniqueMachineInstr *UMI);

  UniqueMachineInstr *getNodeIfExists(FoldingSetNodeID &ID,
                                      MachineBasicBlock *MBB, void *&InsertPos);

  /// Allocate and construct a new UniqueMachineInstr for MI and return.
  UniqueMachineInstr *getUniqueInstrForMI(const MachineInstr *MI);

  void insertNode(UniqueMachineInstr *UMI, void *InsertPos = nullptr);

  /// Get the MachineInstr(Unique) if it exists already in the CSEMap and the
  /// same MachineBasicBlock.
  MachineInstr *getMachineInstrIfExists(FoldingSetNodeID &ID,
                                        MachineBasicBlock *MBB,
                                        void *&InsertPos);

  /// Use this method to allocate a new UniqueMachineInstr for MI and insert it
  /// into the CSEMap. MI should return true for shouldCSE(MI->getOpcode())
  void insertInstr(MachineInstr *MI, void *InsertPos = nullptr);

  bool HandlingRecordedInstrs = false;

public:
  /// Construct an empty CSE analysis object.
  GISelCSEInfo() = default;

  /// Destroy the CSE analysis object and release owned memory.
  ~GISelCSEInfo() override;

  /// Bind this analysis to machine function \p MF.
  ///
  /// \param MF Machine function whose instructions are tracked for CSE.
  void setMF(MachineFunction &MF);

  /// Verify that the CSE map is consistent with the profiled instructions.
  ///
  /// \return Success, or an Error describing a CSE map inconsistency.
  Error verify();

  /// Records a newly created inst in a list and lazily insert it to the CSEMap.
  ///
  /// Sometimes, this method might be called with a partially constructed
  /// MachineInstr (right after BuildMI without adding any operands) - and in
  /// such cases, defer the hashing of the instruction to a later stage.
  ///
  /// \param MI Newly created instruction to record for later CSE insertion.
  void recordNewInstruction(MachineInstr *MI);

  /// Use this callback to inform CSE about a newly fully created instruction.
  ///
  /// \param MI Fully constructed instruction to insert into the CSE map.
  void handleRecordedInst(MachineInstr *MI);

  /// Insert all recorded instructions into the CSE map.
  ///
  /// At this point, all of these insts need to be fully constructed and should
  /// not be missing any operands.
  void handleRecordedInsts();

  /// Remove this inst from the CSE map. If this inst has not been inserted yet,
  /// it will be removed from the Tempinsts list if it exists.
  ///
  /// \param MI Instruction to remove from the CSE map or temporary list.
  void handleRemoveInst(MachineInstr *MI);

  /// Release memory owned by the CSE map and temporary instruction lists.
  void releaseMemory();

  /// Install the CSE configuration that selects which opcodes are CSEd.
  ///
  /// \param Opt Ownership of the CSE configuration to use.
  void setCSEConfig(std::unique_ptr<CSEConfigBase> Opt) {
    CSEOpt = std::move(Opt);
  }

  /// Return whether opcode \p Opc is eligible for CSE under the current config.
  ///
  /// \param Opc Opcode to check.
  /// \return true if \p Opc should be CSEd under the current config.
  bool shouldCSE(unsigned Opc) const;

  /// Analyze \p MF and populate the CSE map with CSEable instructions.
  ///
  /// \param MF Machine function to analyze.
  void analyze(MachineFunction &MF);

  /// Record a CSE hit for opcode \p Opc (assert builds only).
  ///
  /// \param Opc Opcode whose hit count is incremented.
  void countOpcodeHit(unsigned Opc);

  /// Print CSE hit statistics to the debug stream.
  void print();

  /// Observer API: remove \p MI from the CSE map as it is erased.
  ///
  /// \param MI Instruction that is about to be erased.
  void erasingInstr(MachineInstr &MI) override;
  /// Observer API: record \p MI when a new instruction is created.
  ///
  /// \param MI Instruction that was created and inserted.
  void createdInstr(MachineInstr &MI) override;
  /// Observer API: prepare for mutation of \p MI by removing it from the map.
  ///
  /// \param MI Instruction that is about to be mutated.
  void changingInstr(MachineInstr &MI) override;
  /// Observer API: reinsert \p MI after it was mutated.
  ///
  /// \param MI Instruction that was mutated.
  void changedInstr(MachineInstr &MI) override;
};

class MCRegisterClass;
using TargetRegisterClass = MCRegisterClass;
class RegisterBank;

/// Builder that profiles MachineInstr properties into a FoldingSetNodeID.
class GISelInstProfileBuilder {
  FoldingSetNodeID &ID;
  const MachineRegisterInfo &MRI;

public:
  /// Construct a profile builder for \p ID using register info \p MRI.
  ///
  /// \param ID Folding-set node ID that receives profile bits.
  /// \param MRI Register info used when profiling register operands.
  GISelInstProfileBuilder(FoldingSetNodeID &ID, const MachineRegisterInfo &MRI)
      : ID(ID), MRI(MRI) {}

  /// Profile opcode \p Opc into the node ID.
  ///
  /// \param Opc Opcode value to profile.
  /// \return Reference to this builder for chaining.
  LLVM_ABI const GISelInstProfileBuilder &addNodeIDOpcode(unsigned Opc) const;
  /// Profile LLT register type \p Ty into the node ID.
  ///
  /// \param Ty LLT type to profile.
  /// \return Reference to this builder for chaining.
  LLVM_ABI const GISelInstProfileBuilder &addNodeIDRegType(const LLT Ty) const;
  /// Profile the type of register \p Reg into the node ID.
  ///
  /// \param Reg Register whose type is profiled.
  /// \return Reference to this builder for chaining.
  LLVM_ABI const GISelInstProfileBuilder &
  addNodeIDRegType(const Register Reg) const;
  /// Profile virtual-register attributes \p Attrs into the node ID.
  ///
  /// \param Attrs Type and register-class/bank attributes to profile.
  /// \return Reference to this builder for chaining.
  LLVM_ABI const GISelInstProfileBuilder &
  addNodeIDRegType(MachineRegisterInfo::VRegAttrs Attrs) const;

  /// Profile target register class \p RC into the node ID.
  ///
  /// \param RC Target register class to profile.
  /// \return Reference to this builder for chaining.
  LLVM_ABI const GISelInstProfileBuilder &
  addNodeIDRegType(const TargetRegisterClass *RC) const;
  /// Profile register bank \p RB into the node ID.
  ///
  /// \param RB Register bank to profile.
  /// \return Reference to this builder for chaining.
  LLVM_ABI const GISelInstProfileBuilder &
  addNodeIDRegType(const RegisterBank *RB) const;

  /// Profile register number \p Reg into the node ID.
  ///
  /// \param Reg Register whose number is profiled.
  /// \return Reference to this builder for chaining.
  LLVM_ABI const GISelInstProfileBuilder &addNodeIDRegNum(Register Reg) const;

  /// Profile the type attributes of register \p Reg into the node ID.
  ///
  /// \param Reg Register whose type attributes are profiled.
  /// \return Reference to this builder for chaining.
  LLVM_ABI const GISelInstProfileBuilder &addNodeIDReg(Register Reg) const;

  /// Profile immediate value \p Imm into the node ID.
  ///
  /// \param Imm Immediate value to profile.
  /// \return Reference to this builder for chaining.
  LLVM_ABI const GISelInstProfileBuilder &addNodeIDImmediate(int64_t Imm) const;
  /// Profile machine basic block \p MBB into the node ID.
  ///
  /// \param MBB Machine basic block pointer to profile.
  /// \return Reference to this builder for chaining.
  LLVM_ABI const GISelInstProfileBuilder &
  addNodeIDMBB(const MachineBasicBlock *MBB) const;

  /// Profile machine operand \p MO into the node ID.
  ///
  /// \param MO Machine operand to profile.
  /// \return Reference to this builder for chaining.
  LLVM_ABI const GISelInstProfileBuilder &
  addNodeIDMachineOperand(const MachineOperand &MO) const;

  /// Profile instruction flag \p Flag into the node ID when non-zero.
  ///
  /// \param Flag MachineInstr flags to profile.
  /// \return Reference to this builder for chaining.
  LLVM_ABI const GISelInstProfileBuilder &addNodeIDFlag(unsigned Flag) const;
  /// Profile all CSE-relevant properties of \p MI into the node ID.
  ///
  /// \param MI Instruction whose profile bits are added.
  /// \return Reference to this builder for chaining.
  LLVM_ABI const GISelInstProfileBuilder &
  addNodeID(const MachineInstr *MI) const;
};

/// Lazily computed wrapper around GISelCSEInfo with configurable CSE opcodes.
///
/// 1) Lazily evaluate the MachineFunction to compute CSEable instructions.
/// 2) Allows configuration of which instructions are CSEd through CSEConfig
/// object. Provides a method called get which takes a CSEConfig object.
class GISelCSEAnalysisWrapper {
  GISelCSEInfo Info;
  MachineFunction *MF = nullptr;
  bool AlreadyComputed = false;

public:
  /// Return the CSE info, computing it with \p CSEOpt when needed.
  ///
  /// Takes a CSEConfigBase object that defines what opcodes get CSEd. If
  /// CSEConfig is already set, and the CSE Analysis has been preserved, it will
  /// not use the new CSEOpt(use Recompute to force using the new CSEOpt).
  ///
  /// \param CSEOpt Configuration defining which opcodes are CSEd.
  /// \return Reference to the lazily computed GISelCSEInfo.
  LLVM_ABI GISelCSEInfo &get(std::unique_ptr<CSEConfigBase> CSEOpt);
  /// Bind this wrapper to machine function \p MFunc.
  ///
  /// \param MFunc Machine function analyzed by the wrapped CSE info.
  void setMF(MachineFunction &MFunc) { MF = &MFunc; }
  /// Mark whether the CSE analysis has already been computed.
  ///
  /// \param Computed True if the analysis result is currently valid.
  void setComputed(bool Computed) { AlreadyComputed = Computed; }
  /// Release memory held by the wrapped CSE info.
  void releaseMemory() { Info.releaseMemory(); }
};

/// Analysis that computes GISelCSEInfo for a MachineFunction.
class GISelCSEAnalysis : public AnalysisInfoMixin<GISelCSEAnalysis> {
  friend AnalysisInfoMixin<GISelCSEAnalysis>;
  LLVM_ABI static AnalysisKey Key;
  TargetMachine *TM;

public:
  /// Result type produced by this analysis.
  using Result = std::unique_ptr<GISelCSEInfo>;
  /// Construct the analysis using target machine \p TM for opt-level config.
  ///
  /// \param TM Target machine whose optimization level selects the CSE config.
  GISelCSEAnalysis(TargetMachine *TM) : TM(TM) {};

  /// Run the CSE analysis on machine function \p MF.
  ///
  /// \param MF Machine function to analyze.
  /// \param MFAM Machine function analysis manager.
  /// \return Owned GISelCSEInfo populated for \p MF.
  LLVM_ABI Result run(MachineFunction &MF,
                      MachineFunctionAnalysisManager &MFAM);
};

/// The actual analysis pass wrapper.
class LLVM_ABI GISelCSEAnalysisWrapperPass : public MachineFunctionPass {
  GISelCSEAnalysisWrapper Wrapper;

public:
  /// Pass identification, replacement for typeid.
  static char ID;
  /// Construct the legacy GISel CSE analysis wrapper pass.
  GISelCSEAnalysisWrapperPass();

  /// Specify that this pass preserves all analyses.
  ///
  /// \param AU Analysis usage to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// Return the const CSE analysis wrapper.
  ///
  /// \return Const reference to the CSE analysis wrapper.
  const GISelCSEAnalysisWrapper &getCSEWrapper() const { return Wrapper; }
  /// Return the CSE analysis wrapper.
  ///
  /// \return Reference to the CSE analysis wrapper.
  GISelCSEAnalysisWrapper &getCSEWrapper() { return Wrapper; }

  /// Bind the wrapper to \p MF for subsequent CSE queries.
  ///
  /// \param MF Machine function associated with this analysis run.
  /// \return false; this analysis never modifies the function.
  bool runOnMachineFunction(MachineFunction &MF) override;

  /// Release memory held by the wrapper and mark it as not computed.
  void releaseMemory() override {
    Wrapper.releaseMemory();
    Wrapper.setComputed(false);
  }
};

} // namespace llvm

#endif
