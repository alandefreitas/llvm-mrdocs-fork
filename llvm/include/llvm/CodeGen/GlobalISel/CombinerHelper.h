//===-- llvm/CodeGen/GlobalISel/CombinerHelper.h --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===--------------------------------------------------------------------===//
/// \file
/// This contains common combine transformations that may be used in a combine
/// pass,or by the target elsewhere.
/// Targets can pick individual opcode transformations from the helper or use
/// tryCombine which invokes all transformations. All of the transformations
/// return true if the MachineInstruction changed and false otherwise.
///
//===--------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALISEL_COMBINERHELPER_H
#define LLVM_CODEGEN_GLOBALISEL_COMBINERHELPER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/GlobalISel/GenericMachineInstrs.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/CodeGenTypes/LowLevelType.h"
#include "llvm/IR/InstrTypes.h"
#include <functional>

namespace llvm {

class GISelChangeObserver;
class APInt;
class ConstantFP;
class GPtrAdd;
class GZExtLoad;
class MachineIRBuilder;
class MachineInstrBuilder;
class MachineRegisterInfo;
class MachineInstr;
class MachineOperand;
class GISelValueTracking;
class MachineDominatorTree;
class LegalizerInfo;
struct LegalityQuery;
class RegisterBank;
class RegisterBankInfo;
class TargetInstrInfo;
class TargetLowering;
class TargetRegisterInfo;

/// Preferred candidate for an extending-load combine.
struct PreferredTuple {
  /// Result type of the extend.
  LLT Ty;
  /// Extend opcode: G_ANYEXT, G_SEXT, or G_ZEXT.
  unsigned ExtendOpcode;
  /// Machine instruction that should perform the extend.
  MachineInstr *MI;
};

/// Match info for rewriting a load/store into an indexed form.
struct IndexedLoadStoreMatchInfo {
  /// Address register used by the original load/store.
  Register Addr;
  /// Base pointer register for the indexed operation.
  Register Base;
  /// Offset register (or materializable constant) added to the base.
  Register Offset;
  /// True if Offset is a constant that must be rematerialized before the new
  /// load/store.
  bool RematOffset = false;
  /// True for a pre-indexed form; false for post-indexed.
  bool IsPre = false;
};

/// Folded immediate G_PTR_ADD chain match info.
struct PtrAddChain {
  /// Combined immediate offset from the folded chain.
  int64_t Imm;
  /// Base pointer register at the root of the chain.
  Register Base;
  /// Register bank of the pointer, if assigned.
  const RegisterBank *Bank;
  /// Accumulated G_PTR_ADD flags from the chain.
  unsigned Flags;
};

/// Pair of a register and an immediate used by shift/ptradd combines.
struct RegisterImmPair {
  /// Non-immediate register operand.
  Register Reg;
  /// Immediate operand associated with \p Reg.
  int64_t Imm;
};

/// Match info for folding a shift of a shifted bitwise logic operation.
struct ShiftOfShiftedLogic {
  /// Bitwise logic instruction (AND/OR/XOR) feeding the outer shift.
  MachineInstr *Logic;
  /// Inner shift-by-constant instruction under the logic op.
  MachineInstr *Shift2;
  /// Logic operand that is not the shifted value.
  Register LogicNonShiftReg;
  /// Sum of the inner and outer shift amounts.
  uint64_t ValSum;
};

/// Match info for folding lshr(trunc(lshr x, C1), C2).
struct LshrOfTruncOfLshr {
  /// True when an AND mask must be applied after the combined shift.
  bool Mask = false;
  /// Mask value to apply when \p Mask is true.
  APInt MaskVal;
  /// Source register of the inner logical shift.
  Register Src;
  /// Combined shift amount (C1 + C2).
  APInt ShiftAmt;
  /// Type of the combined shift amount.
  LLT ShiftAmtTy;
  /// Type used for the inner shift before truncation.
  LLT InnerShiftTy;
};

/// Callback that builds replacement MIR using a MachineIRBuilder.
using BuildFnTy = std::function<void(MachineIRBuilder &)>;

/// Ordered callbacks that append operands to a MachineInstrBuilder.
using OperandBuildSteps =
    SmallVector<std::function<void(MachineInstrBuilder &)>, 4>;
/// Description of one instruction to emit during a multi-step combine.
struct InstructionBuildSteps {
  /// Opcode for the produced instruction.
  unsigned Opcode = 0;
  /// Operand builders invoked in order on the new instruction.
  OperandBuildSteps OperandFns;
  /// Construct empty build steps.
  InstructionBuildSteps() = default;
  /// Construct build steps for \p Opcode with operand builders \p OperandFns.
  ///
  /// \param Opcode - Opcode of the instruction to build.
  /// \param OperandFns - Callbacks that append operands to the new instruction.
  InstructionBuildSteps(unsigned Opcode, const OperandBuildSteps &OperandFns)
      : Opcode(Opcode), OperandFns(OperandFns) {}
};

/// Match info describing a sequence of instructions to build for a combine.
struct InstructionStepsMatchInfo {
  /// Instructions to be built during the combine apply step.
  SmallVector<InstructionBuildSteps, 2> InstrsToBuild;
  /// Construct empty match info.
  InstructionStepsMatchInfo() = default;
  /// Construct match info from an initializer list of build steps.
  ///
  /// \param InstrsToBuild - Instructions that should be emitted on apply.
  InstructionStepsMatchInfo(
      std::initializer_list<InstructionBuildSteps> InstrsToBuild)
      : InstrsToBuild(InstrsToBuild) {}
};

/// Helper that implements common GlobalISel combine transformations.
class CombinerHelper {
protected:
  /// IR builder used to construct replacement instructions.
  MachineIRBuilder &Builder;
  /// Register info for the function being combined.
  MachineRegisterInfo &MRI;
  /// Observer notified when instructions or registers change.
  GISelChangeObserver &Observer;
  /// Optional value-tracking analysis used by known-bits combines.
  GISelValueTracking *VT;
  /// Optional dominator tree used by dominance queries.
  MachineDominatorTree *MDT;
  /// True when the combiner is running before the legalizer.
  bool IsPreLegalize;
  /// Optional legalizer info used for legality queries.
  const LegalizerInfo *LI;
  /// Target instruction info for the current subtarget.
  const TargetInstrInfo *TII;
  /// Register bank info for the current subtarget.
  const RegisterBankInfo *RBI;
  /// Target register info for the current subtarget.
  const TargetRegisterInfo *TRI;

public:
  /// Construct a combiner helper for the current function.
  ///
  /// \param Observer - Change observer notified of MIR mutations.
  /// \param B - Machine IR builder used to emit replacements.
  /// \param IsPreLegalize - True when running before legalization.
  /// \param VT - Optional value-tracking analysis.
  /// \param MDT - Optional machine dominator tree.
  /// \param LI - Optional legalizer info for legality checks.
  LLVM_ABI CombinerHelper(GISelChangeObserver &Observer, MachineIRBuilder &B,
                          bool IsPreLegalize, GISelValueTracking *VT = nullptr,
                          MachineDominatorTree *MDT = nullptr,
                          const LegalizerInfo *LI = nullptr);

  /// Return the optional GISel value-tracking analysis.
  ///
  /// \return The optional GISel value-tracking analysis, or nullptr.
  GISelValueTracking *getValueTracking() const { return VT; }

  /// Return the MachineIRBuilder used by this helper.
  ///
  /// \return The MachineIRBuilder used by this helper.
  MachineIRBuilder &getBuilder() const {
    return Builder;
  }

  /// Return the target instruction info.
  ///
  /// \return The target instruction info.
  const TargetInstrInfo &getTII() const { return *TII; }

  /// Return the target register info.
  ///
  /// \return The target register info.
  const TargetRegisterInfo &getTRI() const { return *TRI; }

  /// Return the register bank info.
  ///
  /// \return The register bank info.
  const RegisterBankInfo &getRBI() const { return *RBI; }

  /// Return the target lowering info for the current function.
  ///
  /// \return The target lowering info for the current function.
  LLVM_ABI const TargetLowering &getTargetLowering() const;

  /// Return the machine function being combined.
  ///
  /// \return The machine function being combined.
  LLVM_ABI const MachineFunction &getMachineFunction() const;

  /// Return the data layout for the current module.
  ///
  /// \return The data layout for the current module.
  LLVM_ABI const DataLayout &getDataLayout() const;

  /// Return the LLVMContext for the current module.
  ///
  /// \return The LLVMContext for the current module.
  LLVM_ABI LLVMContext &getContext() const;

  /// Return true if the combiner is running pre-legalization.
  ///
  /// \return true if the combiner is running pre-legalization.
  LLVM_ABI bool isPreLegalize() const;

  /// Return true if \p Query is legal on the target.
  ///
  /// \param Query - Legality query to evaluate.
  /// \return true if \p Query is legal on the target.
  LLVM_ABI bool isLegal(const LegalityQuery &Query) const;

  /// Return true if combining before legalization, or if \p Query is legal.
  ///
  /// \param Query - Legality query to evaluate.
  /// \return true if combining before legalization, or if \p Query is legal.
  LLVM_ABI bool isLegalOrBeforeLegalizer(const LegalityQuery &Query) const;

  /// Return true if \p Query is legal or will WidenScalar on the target.
  ///
  /// \param Query - Legality query to evaluate.
  /// \return true if \p Query is legal or will WidenScalar on the target.
  LLVM_ABI bool isLegalOrHasWidenScalar(const LegalityQuery &Query) const;

  /// Return true if \p Query is legal or will FewerElements on the target.
  ///
  /// \param Query - Legality query to evaluate.
  /// \return true if \p Query is legal or will FewerElements on the target.
  LLVM_ABI bool isLegalOrHasFewerElements(const LegalityQuery &Query) const;

  /// Return true if combining before legalization, or if \p Ty is legal.
  ///
  /// \param Ty - Integer constant type to check.
  /// \return true if combining before legalization, or if \p Ty is legal.
  LLVM_ABI bool isConstantLegalOrBeforeLegalizer(const LLT Ty) const;

  /// Replace all uses of \p FromReg with \p ToReg and notify the observer.
  ///
  /// \param MRI - Register info owning the registers.
  /// \param FromReg - Register being replaced.
  /// \param ToReg - Replacement register.
  LLVM_ABI void replaceRegWith(MachineRegisterInfo &MRI, Register FromReg,
                               Register ToReg) const;

  /// Replace a single register operand and notify the observer.
  ///
  /// \param MRI - Register info owning the registers.
  /// \param FromRegOp - Register operand to rewrite.
  /// \param ToReg - Replacement register.
  LLVM_ABI void replaceRegOpWith(MachineRegisterInfo &MRI,
                                 MachineOperand &FromRegOp,
                                 Register ToReg) const;

  /// Replace the opcode of \p FromMI and notify the observer.
  ///
  /// \param FromMI - Instruction whose opcode is replaced.
  /// \param ToOpcode - New opcode to install.
  LLVM_ABI void replaceOpcodeWith(MachineInstr &FromMI,
                                  unsigned ToOpcode) const;

  /// Return the register bank of \p Reg, or nullptr if none is assigned.
  ///
  /// Returns nullptr when Reg has not been assigned a register, register class,
  /// or register bank.
  ///
  /// \pre Reg.isValid()
  ///
  /// \param Reg - Register whose bank is queried.
  /// \return The register bank of \p Reg, or nullptr if none is assigned.
  LLVM_ABI const RegisterBank *getRegBank(Register Reg) const;

  /// Set the register bank of \p Reg.
  ///
  /// Does nothing if \p RegBank is null. Counterpart to getRegBank.
  ///
  /// \param Reg - Register to assign.
  /// \param RegBank - Bank to assign, or null to leave unchanged.
  LLVM_ABI void setRegBank(Register Reg, const RegisterBank *RegBank) const;

  /// Try to combine \p MI when it is a COPY.
  ///
  /// \param MI - Potential COPY instruction.
  /// \return true if \p MI changed.
  LLVM_ABI bool tryCombineCopy(MachineInstr &MI) const;
  /// Match a COPY that can be combined away.
  ///
  /// \param MI - Potential COPY instruction.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchCombineCopy(MachineInstr &MI) const;
  /// Apply a matched COPY combine on \p MI.
  ///
  /// \param MI - COPY instruction to rewrite.
  LLVM_ABI void applyCombineCopy(MachineInstr &MI) const;

  /// Return true if \p DefMI precedes or is the same as \p UseMI.
  ///
  /// Both instructions must be in the same basic block.
  ///
  /// \param DefMI - Definition instruction.
  /// \param UseMI - Use instruction in the same block.
  /// \return true if \p DefMI precedes or is the same as \p UseMI.
  LLVM_ABI bool isPredecessor(const MachineInstr &DefMI,
                              const MachineInstr &UseMI) const;

  /// Return true if \p DefMI dominates \p UseMI.
  ///
  /// An instruction dominates itself. Without a MachineDominatorTree this uses
  /// a conservative same-block check.
  ///
  /// \param DefMI - Potential dominator instruction.
  /// \param UseMI - Instruction that may be dominated.
  /// \return true if \p DefMI dominates \p UseMI.
  LLVM_ABI bool dominates(const MachineInstr &DefMI,
                          const MachineInstr &UseMI) const;

  /// Try to combine an extend that consumes a load result.
  ///
  /// \param MI - Extend instruction.
  /// \return true if \p MI changed.
  LLVM_ABI bool tryCombineExtendingLoads(MachineInstr &MI) const;
  /// Match an extend of a load that can become an extending load.
  ///
  /// \param MI - Extend instruction.
  /// \param MatchInfo - Preferred extending-load candidate.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchCombineExtendingLoads(MachineInstr &MI,
                                           PreferredTuple &MatchInfo) const;
  /// Apply a matched extending-load combine.
  ///
  /// \param MI - Extend instruction to rewrite.
  /// \param MatchInfo - Preferred extending-load candidate.
  LLVM_ABI void applyCombineExtendingLoads(MachineInstr &MI,
                                           PreferredTuple &MatchInfo) const;

  /// Match (and (load x), mask) as a zero-extending load.
  ///
  /// \param MI - And of a load and mask.
  /// \param MatchInfo - Builder callback that emits the zextload.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchCombineLoadWithAndMask(MachineInstr &MI,
                                            BuildFnTy &MatchInfo) const;

  /// Match extract-vector-element of a load as a narrowed load.
  ///
  /// \param MI - Extract-element of a load.
  /// \param MatchInfo - Builder callback that emits the narrowed load.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchCombineExtractedVectorLoad(MachineInstr &MI,
                                                BuildFnTy &MatchInfo) const;

  /// Match a load/store that can become an indexed load/store.
  ///
  /// \param MI - Load or store instruction.
  /// \param MatchInfo - Indexed addressing operands and form.
  /// \return true if the pattern matches.
  LLVM_ABI bool
  matchCombineIndexedLoadStore(MachineInstr &MI,
                               IndexedLoadStoreMatchInfo &MatchInfo) const;
  /// Apply a matched indexed load/store combine.
  ///
  /// \param MI - Load or store instruction to rewrite.
  /// \param MatchInfo - Indexed addressing operands and form.
  LLVM_ABI void
  applyCombineIndexedLoadStore(MachineInstr &MI,
                               IndexedLoadStoreMatchInfo &MatchInfo) const;

  /// Match sext(trunc(sextload)) that can be simplified.
  ///
  /// \param MI - Sext of a trunc of a sextload.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchSextTruncSextLoad(MachineInstr &MI) const;
  /// Apply a matched sext-trunc-sextload combine.
  ///
  /// \param MI - Instruction to rewrite.
  LLVM_ABI void applySextTruncSextLoad(MachineInstr &MI) const;

  /// Match sext_inreg(load p), imm as a sextload.
  ///
  /// \param MI - Sext-in-reg of a load.
  /// \param MatchInfo - Load register and immediate width.
  /// \return true if the pattern matches.
  LLVM_ABI bool
  matchSextInRegOfLoad(MachineInstr &MI,
                       std::tuple<Register, unsigned> &MatchInfo) const;
  /// Apply a matched sext_inreg-of-load combine.
  ///
  /// \param MI - Sext-in-reg instruction to rewrite.
  /// \param MatchInfo - Load register and immediate width.
  LLVM_ABI void
  applySextInRegOfLoad(MachineInstr &MI,
                       std::tuple<Register, unsigned> &MatchInfo) const;

  /// Match G_[SU]DIV and G_[SU]REM with identical sources as G_[SU]DIVREM.
  ///
  /// \param MI - Divide or remainder instruction.
  /// \param OtherMI - Matching sibling divide/remainder instruction.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchCombineDivRem(MachineInstr &MI,
                                   MachineInstr *&OtherMI) const;
  /// Apply a matched div/rem-to-divrem combine.
  ///
  /// \param MI - Divide or remainder instruction to rewrite.
  /// \param OtherMI - Matching sibling divide/remainder instruction.
  LLVM_ABI void applyCombineDivRem(MachineInstr &MI,
                                   MachineInstr *&OtherMI) const;

  /// Match a brcond whose true block is not the fallthrough and invert it.
  ///
  /// \param MI - Branch instruction.
  /// \param BrCond - Condition-producing instruction to invert.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchOptBrCondByInvertingCond(MachineInstr &MI,
                                              MachineInstr *&BrCond) const;
  /// Invert a matched brcond so the true block is the fallthrough.
  ///
  /// \param MI - Branch instruction to rewrite.
  /// \param BrCond - Condition-producing instruction to invert.
  LLVM_ABI void applyOptBrCondByInvertingCond(MachineInstr &MI,
                                              MachineInstr *&BrCond) const;

  /// Match a G_CONCAT_VECTORS that is undef or can flatten to build_vector.
  ///
  /// Supports:
  /// - concat_vector(undef, undef) => undef
  /// - concat_vector(build_vector(A, B), build_vector(C, D)) =>
  ///   build_vector(A, B, C, D)
  ///
  /// On success, \p Ops is empty for the undef case, otherwise it holds the
  /// flattened build_vector operands.
  ///
  /// \pre MI.getOpcode() == G_CONCAT_VECTORS
  ///
  /// \param MI - Concat-vectors instruction.
  /// \param Ops - Flattened operands, or empty for undef.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchCombineConcatVectors(MachineInstr &MI,
                                          SmallVector<Register> &Ops) const;
  /// Replace \p MI with a flattened build_vector or implicit_def.
  ///
  /// \param MI - Concat-vectors instruction to rewrite.
  /// \param Ops - Flattened operands, or empty for undef.
  LLVM_ABI void applyCombineConcatVectors(MachineInstr &MI,
                                          SmallVector<Register> &Ops) const;

  /// Match a shuffle that is concatenating vectors and can be flattened.
  ///
  /// \param MI - Shuffle instruction.
  /// \param Ops - Flattened operands for the replacement build_vector.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchCombineShuffleConcat(MachineInstr &MI,
                                          SmallVector<Register> &Ops) const;
  /// Replace shuffle-concat \p MI with a flattened build_vector or implicit_def.
  ///
  /// \param MI - Shuffle instruction to rewrite.
  /// \param Ops - Flattened operands, or empty for undef.
  LLVM_ABI void applyCombineShuffleConcat(MachineInstr &MI,
                                          SmallVector<Register> &Ops) const;

  /// Replace shuffle \p MI with a build_vector.
  ///
  /// \param MI - Shuffle instruction to rewrite.
  LLVM_ABI void applyCombineShuffleToBuildVector(MachineInstr &MI) const;

  /// Match G_BUILD_VECTOR(G_UNMERGE(G_BITCAST), Undef) as a bitcast of a build.
  ///
  /// \param MI - Build-vector instruction.
  /// \param Ops - Operands for the rewritten bitcast/build form.
  /// \return true if the pattern matches.
  LLVM_ABI bool
  matchCombineBuildVectorOfBitcast(MachineInstr &MI,
                                   SmallVector<Register> &Ops) const;
  /// Apply a matched build_vector-of-bitcast combine.
  ///
  /// \param MI - Build-vector instruction to rewrite.
  /// \param Ops - Operands for the rewritten bitcast/build form.
  LLVM_ABI void
  applyCombineBuildVectorOfBitcast(MachineInstr &MI,
                                   SmallVector<Register> &Ops) const;

  /// Match a G_SHUFFLE_VECTOR that can be replaced by concat_vectors.
  ///
  /// \p Ops receives the operands needed to produce the flattened concat.
  ///
  /// \pre MI.getOpcode() == G_SHUFFLE_VECTOR
  ///
  /// \param MI - Shuffle instruction.
  /// \param Ops - Operands for the replacement concat_vectors.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchCombineShuffleVector(MachineInstr &MI,
                                          SmallVectorImpl<Register> &Ops) const;
  /// Replace shuffle \p MI with concat_vectors of \p Ops.
  ///
  /// \param MI - Shuffle instruction to rewrite.
  /// \param Ops - Operands for the replacement concat_vectors.
  LLVM_ABI void applyCombineShuffleVector(MachineInstr &MI,
                                          ArrayRef<Register> Ops) const;

  /// Optimize memcpy-family intrinsics such as constant-length calls.
  ///
  /// \p MaxLen, when non-zero, caps the length of a mem libcall to inline.
  ///
  /// For example (pre-indexed):
  ///
  ///     $addr = G_PTR_ADD $base, $offset
  ///     [...]
  ///     $val = G_LOAD $addr
  ///     [...]
  ///     $whatever = COPY $addr
  ///
  /// -->
  ///
  ///     $val, $addr = G_INDEXED_LOAD $base, $offset, 1 (IsPre)
  ///     [...]
  ///     $whatever = COPY $addr
  ///
  /// or (post-indexed):
  ///
  ///     G_STORE $val, $base
  ///     [...]
  ///     $addr = G_PTR_ADD $base, $offset
  ///     [...]
  ///     $whatever = COPY $addr
  ///
  /// -->
  ///
  ///     $addr = G_INDEXED_STORE $val, $base, $offset
  ///     [...]
  ///     $whatever = COPY $addr
  ///
  /// \param MI - Memcpy-family intrinsic call.
  /// \param MaxLen - Max inline length, or 0 for no limit.
  /// \return true if the memcpy-family intrinsic was combined.
  LLVM_ABI bool tryCombineMemCpyFamily(MachineInstr &MI,
                                       unsigned MaxLen = 0) const;
  /// Match a memcpy-family intrinsic that can be lowered or inlined.
  ///
  /// \param MI - Memcpy-family intrinsic call.
  /// \param MatchInfo - Lowering description for the apply step.
  /// \param MaxLen - Max inline length, or 0 for no limit.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchCombineMemCpyFamily(MachineInstr &MI,
                                         MemCpyFamilyLoweringInfo &MatchInfo,
                                         unsigned MaxLen = 0) const;
  /// Apply a matched memcpy-family combine.
  ///
  /// \param MI - Memcpy-family intrinsic call to rewrite.
  /// \param MatchInfo - Lowering description for the replacement.
  LLVM_ABI void
  applyCombineMemCpyFamily(MachineInstr &MI,
                           MemCpyFamilyLoweringInfo &MatchInfo) const;

  /// Match a chain of immediate G_PTR_ADDs that can be folded together.
  ///
  /// \param MI - Pointer-add instruction root.
  /// \param MatchInfo - Folded immediate chain description.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchPtrAddImmedChain(MachineInstr &MI,
                                      PtrAddChain &MatchInfo) const;
  /// Apply a folded immediate G_PTR_ADD chain.
  ///
  /// \param MI - Pointer-add instruction to rewrite.
  /// \param MatchInfo - Folded immediate chain description.
  LLVM_ABI void applyPtrAddImmedChain(MachineInstr &MI,
                                      PtrAddChain &MatchInfo) const;

  /// Fold (shift (shift base, x), y) into (shift base, (x+y)).
  ///
  /// \param MI - Outer shift instruction.
  /// \param MatchInfo - Base register and combined immediate shift amount.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchShiftImmedChain(MachineInstr &MI,
                                     RegisterImmPair &MatchInfo) const;
  /// Apply a matched shift-immediate-chain fold.
  ///
  /// \param MI - Outer shift instruction to rewrite.
  /// \param MatchInfo - Base register and combined immediate shift amount.
  LLVM_ABI void applyShiftImmedChain(MachineInstr &MI,
                                     RegisterImmPair &MatchInfo) const;

  /// Match shift-of-(logic-of-shift) that can become independent shifts.
  ///
  /// If a shift-by-constant of a bitwise logic op itself has a shift-by-constant
  /// operand with the same opcode, it may become two independent shifts followed
  /// by the logic op.
  ///
  /// \param MI - Outer shift instruction.
  /// \param MatchInfo - Matched logic/shift operands and combined amount.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchShiftOfShiftedLogic(MachineInstr &MI,
                                         ShiftOfShiftedLogic &MatchInfo) const;
  /// Apply a matched shift-of-shifted-logic combine.
  ///
  /// \param MI - Outer shift instruction to rewrite.
  /// \param MatchInfo - Matched logic/shift operands and combined amount.
  LLVM_ABI void applyShiftOfShiftedLogic(MachineInstr &MI,
                                         ShiftOfShiftedLogic &MatchInfo) const;

  /// Match a shift whose operands should be commuted.
  ///
  /// \param MI - Shift instruction.
  /// \param MatchInfo - Builder callback that emits the commuted form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchCommuteShift(MachineInstr &MI, BuildFnTy &MatchInfo) const;

  /// Fold (lshr (trunc (lshr x, C1)), C2) into trunc(shift x, (C1 + C2)).
  ///
  /// \param MI - Outer logical right shift.
  /// \param MatchInfo - Combined shift/mask description.
  /// \param ShiftMI - Inner shift instruction.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchLshrOfTruncOfLshr(MachineInstr &MI,
                                       LshrOfTruncOfLshr &MatchInfo,
                                       MachineInstr &ShiftMI) const;
  /// Apply a matched lshr-of-trunc-of-lshr fold.
  ///
  /// \param MI - Outer logical right shift to rewrite.
  /// \param MatchInfo - Combined shift/mask description.
  LLVM_ABI void applyLshrOfTruncOfLshr(MachineInstr &MI,
                                       LshrOfTruncOfLshr &MatchInfo) const;

  /// Transform a multiply by a power of two into a left shift.
  ///
  /// \param MI - Multiply instruction.
  /// \param ShiftVal - Power-of-two shift amount.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchCombineMulToShl(MachineInstr &MI,
                                     unsigned &ShiftVal) const;
  /// Apply a matched multiply-to-shift combine.
  ///
  /// \param MI - Multiply instruction to rewrite.
  /// \param ShiftVal - Power-of-two shift amount.
  LLVM_ABI void applyCombineMulToShl(MachineInstr &MI,
                                     unsigned &ShiftVal) const;

  /// Transform a G_SUB with a constant RHS into a G_ADD.
  ///
  /// \param MI - Subtract instruction.
  /// \param MatchInfo - Builder callback that emits the add form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchCombineSubToAdd(MachineInstr &MI,
                                     BuildFnTy &MatchInfo) const;

  /// Transform G_SHL of an extended source into a narrower shift when possible.
  ///
  /// \param MI - Shift-left instruction.
  /// \param MatchData - Narrow source register and shift immediate.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchCombineShlOfExtend(MachineInstr &MI,
                                        RegisterImmPair &MatchData) const;
  /// Apply a matched shl-of-extend combine.
  ///
  /// \param MI - Shift-left instruction to rewrite.
  /// \param MatchData - Narrow source register and shift immediate.
  LLVM_ABI void applyCombineShlOfExtend(MachineInstr &MI,
                                        const RegisterImmPair &MatchData) const;

  /// Fold away a merge of an unmerge of the corresponding values.
  ///
  /// \param MI - Merge instruction.
  /// \param MatchInfo - Register that replaces the merge.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchCombineMergeUnmerge(MachineInstr &MI,
                                         Register &MatchInfo) const;

  /// Reduce a constant shift to an unmerge plus a half-width shift.
  ///
  /// Does not produce a shift smaller than \p TargetShiftSize.
  ///
  /// \param MI - Shift instruction.
  /// \param TargetShiftSize - Minimum shift width to leave in place.
  /// \param ShiftVal - Matched constant shift amount.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchCombineShiftToUnmerge(MachineInstr &MI,
                                           unsigned TargetShiftSize,
                                           unsigned &ShiftVal) const;
  /// Apply a matched shift-to-unmerge combine.
  ///
  /// \param MI - Shift instruction to rewrite.
  /// \param ShiftVal - Matched constant shift amount.
  LLVM_ABI void applyCombineShiftToUnmerge(MachineInstr &MI,
                                           const unsigned &ShiftVal) const;
  /// Try the shift-to-unmerge combine on \p MI.
  ///
  /// \param MI - Shift instruction.
  /// \param TargetShiftAmount - Minimum shift width to leave in place.
  /// \return true if the shift was rewritten as an unmerge.
  LLVM_ABI bool tryCombineShiftToUnmerge(MachineInstr &MI,
                                         unsigned TargetShiftAmount) const;

  /// Transform <ty,...> G_UNMERGE(G_MERGE ty X, Y, Z) into ty X, Y, Z.
  ///
  /// \param MI - Unmerge instruction.
  /// \param Operands - Plain values that replace the unmerge defs.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchCombineUnmergeMergeToPlainValues(
      MachineInstr &MI, SmallVectorImpl<Register> &Operands) const;
  /// Apply an unmerge-of-merge-to-plain-values combine.
  ///
  /// \param MI - Unmerge instruction to rewrite.
  /// \param Operands - Plain values that replace the unmerge defs.
  LLVM_ABI void applyCombineUnmergeMergeToPlainValues(
      MachineInstr &MI, SmallVectorImpl<Register> &Operands) const;

  /// Transform G_UNMERGE Constant into Constant1, Constant2, ...
  ///
  /// \param MI - Unmerge of a constant.
  /// \param Csts - Split constant pieces for each unmerge destination.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchCombineUnmergeConstant(MachineInstr &MI,
                                            SmallVectorImpl<APInt> &Csts) const;
  /// Apply an unmerge-of-constant combine.
  ///
  /// \param MI - Unmerge instruction to rewrite.
  /// \param Csts - Split constant pieces for each unmerge destination.
  LLVM_ABI void applyCombineUnmergeConstant(MachineInstr &MI,
                                            SmallVectorImpl<APInt> &Csts) const;

  /// Transform G_UNMERGE G_IMPLICIT_DEF into G_IMPLICIT_DEF results.
  ///
  /// \param MI - Unmerge of an undef value.
  /// \param MatchInfo - Builder callback that emits the undef results.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchCombineUnmergeUndef(
      MachineInstr &MI,
      std::function<void(MachineIRBuilder &)> &MatchInfo) const;

  /// Transform X, Y<dead> = G_UNMERGE Z into X = G_TRUNC Z.
  ///
  /// \param MI - Unmerge with dead high lanes.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchCombineUnmergeWithDeadLanesToTrunc(MachineInstr &MI) const;
  /// Apply an unmerge-with-dead-lanes-to-trunc combine.
  ///
  /// \param MI - Unmerge instruction to rewrite.
  LLVM_ABI void applyCombineUnmergeWithDeadLanesToTrunc(MachineInstr &MI) const;

  /// Transform X, Y = G_UNMERGE(G_ZEXT(Z)) into X = G_ZEXT(Z); Y = 0.
  ///
  /// \param MI - Unmerge of a zero-extend.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchCombineUnmergeZExtToZExt(MachineInstr &MI) const;
  /// Apply an unmerge-of-zext combine.
  ///
  /// \param MI - Unmerge instruction to rewrite.
  LLVM_ABI void applyCombineUnmergeZExtToZExt(MachineInstr &MI) const;

  /// Fold fp_instr(cst) to the constant result of the FP operation.
  ///
  /// \param MI - Unary floating-point instruction.
  /// \param Cst - Folded floating-point constant result.
  LLVM_ABI void applyCombineConstantFoldFpUnary(MachineInstr &MI,
                                                const ConstantFP *Cst) const;

  /// Constant-fold a unary integer op over a scalar or build_vector constant.
  ///
  /// Supports G_CTLZ, G_CTTZ, G_CTPOP and their _ZERO_POISON variants, G_ABS,
  /// G_BSWAP, and G_BITREVERSE.
  ///
  /// \param MI - Unary integer operation.
  /// \param MatchInfo - Builder callback that emits the folded constant.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchConstantFoldUnaryIntOp(MachineInstr &MI,
                                            BuildFnTy &MatchInfo) const;

  /// Transform IntToPtr(PtrToInt(x)) to x when the cast stays in-address-space.
  ///
  /// \param MI - IntToPtr instruction.
  /// \param Reg - Original pointer register that replaces the cast pair.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchCombineI2PToP2I(MachineInstr &MI, Register &Reg) const;
  /// Apply an IntToPtr(PtrToInt(x)) -> x combine.
  ///
  /// \param MI - IntToPtr instruction to rewrite.
  /// \param Reg - Original pointer register that replaces the cast pair.
  LLVM_ABI void applyCombineI2PToP2I(MachineInstr &MI, Register &Reg) const;

  /// Transform PtrToInt(IntToPtr(x)) to x.
  ///
  /// \param MI - PtrToInt instruction to rewrite.
  /// \param Reg - Original integer register that replaces the cast pair.
  LLVM_ABI void applyCombineP2IToI2P(MachineInstr &MI, Register &Reg) const;

  /// Transform G_ADD (G_PTRTOINT x), y into G_PTRTOINT (G_PTR_ADD x, y).
  ///
  /// Also matches the commuted form G_ADD y, (G_PTRTOINT x).
  ///
  /// \param MI - Add of a ptrtoint.
  /// \param PtrRegAndCommute - Pointer register and whether operands were commuted.
  /// \return true if the pattern matches.
  LLVM_ABI bool
  matchCombineAddP2IToPtrAdd(MachineInstr &MI,
                             std::pair<Register, bool> &PtrRegAndCommute) const;
  /// Apply an add-of-ptrtoint to ptradd combine.
  ///
  /// \param MI - Add instruction to rewrite.
  /// \param PtrRegAndCommute - Pointer register and whether operands were commuted.
  LLVM_ABI void
  applyCombineAddP2IToPtrAdd(MachineInstr &MI,
                             std::pair<Register, bool> &PtrRegAndCommute) const;

  /// Transform G_PTR_ADD (G_PTRTOINT C1), C2 into the constant C1 + C2.
  ///
  /// \param MI - Pointer-add of constants.
  /// \param NewCst - Folded integer constant result.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchCombineConstPtrAddToI2P(MachineInstr &MI,
                                             APInt &NewCst) const;
  /// Apply a constant ptradd-to-integer combine.
  ///
  /// \param MI - Pointer-add instruction to rewrite.
  /// \param NewCst - Folded integer constant result.
  LLVM_ABI void applyCombineConstPtrAddToI2P(MachineInstr &MI,
                                             APInt &NewCst) const;

  /// Transform anyext(trunc(x)) to x.
  ///
  /// \param MI - Any-extend of a truncate.
  /// \param Reg - Original register that replaces the cast pair.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchCombineAnyExtTrunc(MachineInstr &MI, Register &Reg) const;

  /// Transform zext(trunc(x)) to x.
  ///
  /// \param MI - Zero-extend of a truncate.
  /// \param Reg - Original register that replaces the cast pair.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchCombineZextTrunc(MachineInstr &MI, Register &Reg) const;

  /// Transform trunc(shl x, K) / trunc([al]shr x, K) into narrower shifts.
  ///
  /// Specifically:
  ///   trunc (shl x, K) -> shl (trunc x), K
  ///     if K < VT.getScalarSizeInBits().
  ///   trunc ([al]shr x, K) -> trunc ([al]shr (MidVT (trunc x)), K)
  ///     if K <= (MidVT.getScalarSizeInBits() - VT.getScalarSizeInBits())
  /// MidVT is a legal type between the trunc source and destination types.
  ///
  /// \param MI - Truncate of a shift.
  /// \param MatchInfo - Shift instruction and intermediate type for the rewrite.
  /// \return true if the pattern matches.
  LLVM_ABI bool
  matchCombineTruncOfShift(MachineInstr &MI,
                           std::pair<MachineInstr *, LLT> &MatchInfo) const;
  /// Apply a matched trunc-of-shift combine.
  ///
  /// \param MI - Truncate instruction to rewrite.
  /// \param MatchInfo - Shift instruction and intermediate type for the rewrite.
  LLVM_ABI void
  applyCombineTruncOfShift(MachineInstr &MI,
                           std::pair<MachineInstr *, LLT> &MatchInfo) const;

  /// Return true if any explicit use operand on \p MI is defined by G_IMPLICIT_DEF.
  ///
  /// \param MI - Instruction whose uses are inspected.
  /// \return true if any explicit use operand on \p MI is defined by G_IMPLICIT_DEF.
  LLVM_ABI bool matchAnyExplicitUseIsUndef(MachineInstr &MI) const;

  /// Return true if all explicit register uses on \p MI are G_IMPLICIT_DEF.
  ///
  /// \param MI - Instruction whose uses are inspected.
  /// \return true if all explicit register uses on \p MI are G_IMPLICIT_DEF.
  LLVM_ABI bool matchAllExplicitUsesAreUndef(MachineInstr &MI) const;

  /// Return true if shuffle \p MI has an undef mask.
  ///
  /// \param MI - G_SHUFFLE_VECTOR instruction.
  /// \return true if shuffle \p MI has an undef mask.
  LLVM_ABI bool matchUndefShuffleVectorMask(MachineInstr &MI) const;

  /// Return true if store \p MI stores an undef value.
  ///
  /// \param MI - G_STORE instruction.
  /// \return true if store \p MI stores an undef value.
  LLVM_ABI bool matchUndefStore(MachineInstr &MI) const;

  /// Return true if select \p MI has an undef comparison.
  ///
  /// \param MI - G_SELECT instruction.
  /// \return true if select \p MI has an undef comparison.
  LLVM_ABI bool matchUndefSelectCmp(MachineInstr &MI) const;

  /// Return true if insert/extract vector element \p MI has an OOB index.
  ///
  /// \param MI - Insert or extract vector element instruction.
  /// \return true if insert/extract vector element \p MI has an OOB index.
  LLVM_ABI bool matchInsertExtractVecEltOutOfBounds(MachineInstr &MI) const;

  /// Return true if select \p MI has a constant comparison.
  ///
  /// On success, \p OpIdx stores the operand index of the known selected value.
  ///
  /// \param MI - G_SELECT instruction.
  /// \param OpIdx - Set to the selected operand index on success.
  /// \return true if select \p MI has a constant comparison.
  LLVM_ABI bool matchConstantSelectCmp(MachineInstr &MI, unsigned &OpIdx) const;

  /// Replace an instruction with a G_FCONSTANT of value \p C.
  ///
  /// \param MI - Instruction to replace.
  /// \param C - Floating-point value for the new constant.
  LLVM_ABI void replaceInstWithFConstant(MachineInstr &MI, double C) const;

  /// Replace an instruction with a G_FCONSTANT of value \p CFP.
  ///
  /// \param MI - Instruction to replace.
  /// \param CFP - Floating-point constant value.
  LLVM_ABI void replaceInstWithFConstant(MachineInstr &MI,
                                         ConstantFP *CFP) const;

  /// Replace an instruction with a G_CONSTANT of value \p C.
  ///
  /// \param MI - Instruction to replace.
  /// \param C - Integer value for the new constant.
  LLVM_ABI void replaceInstWithConstant(MachineInstr &MI, int64_t C) const;

  /// Replace an instruction with a G_CONSTANT of value \p C.
  ///
  /// \param MI - Instruction to replace.
  /// \param C - Integer APInt value for the new constant.
  LLVM_ABI void replaceInstWithConstant(MachineInstr &MI, APInt C) const;

  /// Replace an instruction with a G_IMPLICIT_DEF.
  ///
  /// \param MI - Instruction to replace.
  LLVM_ABI void replaceInstWithUndef(MachineInstr &MI) const;

  /// Delete \p MI and replace all uses with its \p OpIdx-th operand.
  ///
  /// \param MI - Single-def instruction to delete.
  /// \param OpIdx - Operand index that replaces all uses.
  LLVM_ABI void replaceSingleDefInstWithOperand(MachineInstr &MI,
                                                unsigned OpIdx) const;

  /// Delete \p MI and replace all uses with \p Replacement.
  ///
  /// \param MI - Single-def instruction to delete.
  /// \param Replacement - Register that replaces all uses.
  LLVM_ABI void replaceSingleDefInstWithReg(MachineInstr &MI,
                                            Register Replacement) const;

  /// Replace the funnel-shift amount in \p MI with ShiftAmt % bitwidth.
  ///
  /// \param MI - Funnel-shift instruction whose amount is reduced modulo width.
  LLVM_ABI void applyFunnelShiftConstantModulo(MachineInstr &MI) const;

  /// Return true if \p MOP1 and \p MOP2 are defined by equivalent instructions.
  ///
  /// \param MOP1 - First register operand.
  /// \param MOP2 - Second register operand.
  /// \return true if \p MOP1 and \p MOP2 are defined by equivalent instructions.
  LLVM_ABI bool matchEqualDefs(const MachineOperand &MOP1,
                               const MachineOperand &MOP2) const;

  /// Return true if \p MOP is a G_CONSTANT or splat equal to \p C.
  ///
  /// \param MOP - Operand to test.
  /// \param C - Expected integer constant value.
  /// \return true if \p MOP is a G_CONSTANT or splat equal to \p C.
  LLVM_ABI bool matchConstantOp(const MachineOperand &MOP, int64_t C) const;

  /// Return true if \p MOP is a G_FCONSTANT or splat exactly equal to \p C.
  ///
  /// \param MOP - Operand to test.
  /// \param C - Expected floating-point constant value.
  /// \return true if \p MOP is a G_FCONSTANT or splat exactly equal to \p C.
  LLVM_ABI bool matchConstantFPOp(const MachineOperand &MOP, double C) const;

  /// Return true if the constant at \p ConstIdx is larger than \p MI's bitwidth.
  ///
  /// \param MI - Instruction owning the constant operand.
  /// \param ConstIdx - Index of the constant operand.
  /// \return true if the constant at \p ConstIdx is larger than \p MI's bitwidth.
  LLVM_ABI bool matchConstantLargerBitWidth(MachineInstr &MI,
                                            unsigned ConstIdx) const;

  /// Optimize (cond ? x : x) into x.
  ///
  /// \param MI - Select instruction.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchSelectSameVal(MachineInstr &MI) const;

  /// Optimize (x op x) into x.
  ///
  /// \param MI - Binary operation with identical operands.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchBinOpSameVal(MachineInstr &MI) const;

  /// Check if operand \p OpIdx is undef.
  ///
  /// \param MI - Instruction being inspected.
  /// \param OpIdx - Operand index to test.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchOperandIsUndef(MachineInstr &MI, unsigned OpIdx) const;

  /// Return true if operand \p MO is known to be a power of two.
  ///
  /// When \p OrNegative is true, also match operands whose absolute value is a
  /// power of two.
  ///
  /// \param MO - Operand to test.
  /// \param OrNegative - Also accept negated powers of two.
  /// \return true if \p MO is known to be a power of two.
  LLVM_ABI bool
  matchOperandIsKnownToBeAPowerOfTwo(const MachineOperand &MO,
                                     bool OrNegative = false) const;

  /// Erase \p MI from the function.
  ///
  /// \param MI - Instruction to erase.
  LLVM_ABI void eraseInst(MachineInstr &MI) const;

  /// Match a G_ADD that can be simplified to a G_SUB.
  ///
  /// \param MI - Add instruction.
  /// \param MatchInfo - Operand registers for the rewritten subtract.
  /// \return true if the pattern matches.
  LLVM_ABI bool
  matchSimplifyAddToSub(MachineInstr &MI,
                        std::tuple<Register, Register> &MatchInfo) const;
  /// Rewrite a matched G_ADD into a G_SUB.
  ///
  /// \param MI - Add instruction to rewrite.
  /// \param MatchInfo - Operand registers for the rewritten subtract.
  LLVM_ABI void
  applySimplifyAddToSub(MachineInstr &MI,
                        std::tuple<Register, Register> &MatchInfo) const;

  /// Fold `a bitwiseop (~b +/- c)` into `a bitwiseop ~(b -/+ c)`.
  ///
  /// \param MI - Bitwise operation over a negated add/sub.
  /// \param MatchInfo - Builder callback that emits the rewritten form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchBinopWithNeg(MachineInstr &MI, BuildFnTy &MatchInfo) const;

  /// Match (logic_op (op x...), (op y...)) into (op (logic_op x, y)).
  ///
  /// \param MI - Logic operation with matching hand opcodes.
  /// \param MatchInfo - Multi-step build info for the hoisted form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchHoistLogicOpWithSameOpcodeHands(
      MachineInstr &MI, InstructionStepsMatchInfo &MatchInfo) const;

  /// Replace \p MI with the instruction sequence described by \p MatchInfo.
  ///
  /// \param MI - Instruction being replaced.
  /// \param MatchInfo - Steps that describe the replacement instructions.
  LLVM_ABI void
  applyBuildInstructionSteps(MachineInstr &MI,
                             InstructionStepsMatchInfo &MatchInfo) const;

  /// Match ashr(shl x, C), C as sext_inreg with width C.
  ///
  /// \param MI - Arithmetic right shift instruction.
  /// \param MatchInfo - Source register and sext_inreg immediate width.
  /// \return true if the pattern matches.
  LLVM_ABI bool
  matchAshrShlToSextInreg(MachineInstr &MI,
                          std::tuple<Register, int64_t> &MatchInfo) const;
  /// Rewrite matched ashr(shl x, C), C into sext_inreg.
  ///
  /// \param MI - Arithmetic right shift instruction to rewrite.
  /// \param MatchInfo - Source register and sext_inreg immediate width.
  LLVM_ABI void
  applyAshShlToSextInreg(MachineInstr &MI,
                         std::tuple<Register, int64_t> &MatchInfo) const;

  /// Fold and(and(x, C1), C2) into C1&C2 ? and(x, C1&C2) : 0.
  ///
  /// \param MI - And instruction root.
  /// \param MatchInfo - Builder callback that emits the folded form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchOverlappingAnd(MachineInstr &MI,
                                    BuildFnTy &MatchInfo) const;

  /// Match a G_AND that is redundant with one of its operands.
  ///
  /// True when operands x and y satisfy x & y == x or x & y == y (for example
  /// when one operand is all-ones).
  ///
  /// \param MI - The G_AND instruction.
  /// \param Replacement - Register that should replace the G_AND on success.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchRedundantAnd(MachineInstr &MI,
                                  Register &Replacement) const;

  /// Match a G_OR that is redundant with one of its operands.
  ///
  /// True when operands x and y satisfy x | y == x or x | y == y (for example
  /// when one operand is all-zeros).
  ///
  /// \param MI - The G_OR instruction.
  /// \param Replacement - Register that should replace the G_OR on success.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchRedundantOr(MachineInstr &MI, Register &Replacement) const;

  /// Match a G_SEXT_INREG that can be erased as redundant.
  ///
  /// \param MI - Sext-in-reg instruction.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchRedundantSExtInReg(MachineInstr &MI) const;

  /// Combine inverting a compare result into the opposite condition code.
  ///
  /// \param MI - Not of a compare instruction.
  /// \param RegsToNegate - Registers whose compares should flip condition codes.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchNotCmp(MachineInstr &MI,
                            SmallVectorImpl<Register> &RegsToNegate) const;
  /// Apply a matched not-of-compare combine.
  ///
  /// \param MI - Not of a compare instruction to rewrite.
  /// \param RegsToNegate - Registers whose compares should flip condition codes.
  LLVM_ABI void applyNotCmp(MachineInstr &MI,
                            SmallVectorImpl<Register> &RegsToNegate) const;

  /// Fold (xor (and x, y), y) into (and (not x), y).
  ///
  /// \param MI - Xor instruction root.
  /// \param MatchInfo - Pair of registers used by the rewritten and/not form.
  /// \return true if the pattern matches.
  LLVM_ABI bool
  matchXorOfAndWithSameReg(MachineInstr &MI,
                           std::pair<Register, Register> &MatchInfo) const;
  /// Apply a matched xor-of-and-with-same-reg combine.
  ///
  /// \param MI - Xor instruction to rewrite.
  /// \param MatchInfo - Pair of registers used by the rewritten and/not form.
  LLVM_ABI void
  applyXorOfAndWithSameReg(MachineInstr &MI,
                           std::pair<Register, Register> &MatchInfo) const;

  /// Match G_PTR_ADD with a null pointer that can become G_INTTOPTR.
  ///
  /// \param MI - Pointer-add instruction.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchPtrAddZero(MachineInstr &MI) const;
  /// Rewrite G_PTR_ADD with nullptr into G_INTTOPTR.
  ///
  /// \param MI - Pointer-add instruction to rewrite.
  LLVM_ABI void applyPtrAddZero(MachineInstr &MI) const;

  /// Combine G_UREM x, (known power of 2) into an add and bitmasking.
  ///
  /// \param MI - Unsigned remainder instruction to rewrite.
  LLVM_ABI void applySimplifyURemByPow2(MachineInstr &MI) const;

  /// Push a binary operator through a select on constants.
  ///
  /// binop (select cond, K0, K1), K2 ->
  ///   select cond, (binop K0, K2), (binop K1, K2)
  ///
  /// \param MI - Binary operation over a select.
  /// \param SelectOpNo - Operand index of the select in \p MI.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchFoldBinOpIntoSelect(MachineInstr &MI,
                                         unsigned &SelectOpNo) const;
  /// Apply a matched binop-into-select fold.
  ///
  /// \param MI - Binary operation to rewrite.
  /// \param SelectOpNo - Operand index of the select in \p MI.
  LLVM_ABI void applyFoldBinOpIntoSelect(MachineInstr &MI,
                                         const unsigned &SelectOpNo) const;

  /// Match a sequence of inserts that can become a build_vector.
  ///
  /// \param MI - Insert-element instruction root.
  /// \param MatchInfo - Element registers for the build_vector.
  /// \return true if the pattern matches.
  LLVM_ABI bool
  matchCombineInsertVecElts(MachineInstr &MI,
                            SmallVectorImpl<Register> &MatchInfo) const;

  /// Rewrite matched inserts into a build_vector.
  ///
  /// \param MI - Insert-element instruction root.
  /// \param MatchInfo - Element registers for the build_vector.
  LLVM_ABI void
  applyCombineInsertVecElts(MachineInstr &MI,
                            SmallVectorImpl<Register> &MatchInfo) const;

  /// Match expression trees of the form
  ///
  /// \code
  ///  sN *a = ...
  ///  sM val = a[0] | (a[1] << N) | (a[2] << 2N) | (a[3] << 3N) ...
  /// \endcode
  ///
  /// and check whether they can be replaced with an M-bit load plus maybe a
  /// bswap.
  ///
  /// \param MI - Or-tree root being matched.
  /// \param MatchInfo - Builder callback that emits the load/bswap form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchLoadOrCombine(MachineInstr &MI,
                                   BuildFnTy &MatchInfo) const;

  /// Match an extend that can be pushed through PHI nodes.
  ///
  /// \param MI - Extend instruction.
  /// \param ExtMI - Matched extend to rewrite through PHIs.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchExtendThroughPhis(MachineInstr &MI,
                                       MachineInstr *&ExtMI) const;
  /// Push a matched extend through PHI nodes.
  ///
  /// \param MI - Extend instruction root.
  /// \param ExtMI - Matched extend to rewrite through PHIs.
  LLVM_ABI void applyExtendThroughPhis(MachineInstr &MI,
                                       MachineInstr *&ExtMI) const;

  /// Match extract-element of a build_vector that yields a known element.
  ///
  /// \param MI - Extract-element instruction.
  /// \param Reg - Register of the extracted element.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchExtractVecEltBuildVec(MachineInstr &MI,
                                           Register &Reg) const;
  /// Replace extract-element of a build_vector with the selected element.
  ///
  /// \param MI - Extract-element instruction to rewrite.
  /// \param Reg - Register of the extracted element.
  LLVM_ABI void applyExtractVecEltBuildVec(MachineInstr &MI,
                                           Register &Reg) const;

  /// Match extracting every element of a build_vector.
  ///
  /// \param MI - Build-vector whose elements are all extracted.
  /// \param MatchInfo - Pairs of extract destinations and extract instructions.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchExtractAllEltsFromBuildVector(
      MachineInstr &MI,
      SmallVectorImpl<std::pair<Register, MachineInstr *>> &MatchInfo) const;
  /// Replace extracts of every build_vector element with the source elements.
  ///
  /// \param MI - Build-vector whose elements are all extracted.
  /// \param MatchInfo - Pairs of extract destinations and extract instructions.
  LLVM_ABI void applyExtractAllEltsFromBuildVector(
      MachineInstr &MI,
      SmallVectorImpl<std::pair<Register, MachineInstr *>> &MatchInfo) const;

  /// Apply a MachineIRBuilder callback and erase \p MI by default.
  ///
  /// \param MI - Instruction being combined.
  /// \param MatchInfo - Builder callback that emits the replacement.
  LLVM_ABI void applyBuildFn(MachineInstr &MI, BuildFnTy &MatchInfo) const;
  /// Apply a MachineIRBuilder callback without erasing \p MI.
  ///
  /// \param MI - Instruction being combined.
  /// \param MatchInfo - Builder callback that emits the replacement.
  LLVM_ABI void applyBuildFnNoErase(MachineInstr &MI,
                                    BuildFnTy &MatchInfo) const;

  /// Match or-of-shifts that can become a funnel shift.
  ///
  /// \param MI - Or instruction root.
  /// \param AllowScalarConstants - Whether scalar constant shift amounts are allowed.
  /// \param MatchInfo - Builder callback that emits the funnel shift.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchOrShiftToFunnelShift(MachineInstr &MI,
                                          bool AllowScalarConstants,
                                          BuildFnTy &MatchInfo) const;
  /// Match a funnel shift that is actually a rotate.
  ///
  /// \param MI - Funnel-shift instruction.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchFunnelShiftToRotate(MachineInstr &MI) const;
  /// Rewrite a matched funnel shift into a rotate.
  ///
  /// \param MI - Funnel-shift instruction to rewrite.
  LLVM_ABI void applyFunnelShiftToRotate(MachineInstr &MI) const;
  /// Match a rotate whose amount is out of range and can be reduced.
  ///
  /// \param MI - Rotate instruction.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchRotateOutOfRange(MachineInstr &MI) const;
  /// Reduce a matched out-of-range rotate amount.
  ///
  /// \param MI - Rotate instruction to rewrite.
  LLVM_ABI void applyRotateOutOfRange(MachineInstr &MI) const;

  /// Match build_vector of an unmerge that can be replaced by the unmerge source.
  ///
  /// \param MI - Build-vector instruction.
  /// \param MRI - Register info for examining definitions.
  /// \param UnmergeSrc - Source register of the matched unmerge.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchCombineBuildUnmerge(MachineInstr &MI,
                                         MachineRegisterInfo &MRI,
                                         Register &UnmergeSrc) const;
  /// Replace build_vector(unmerge(x)) with x.
  ///
  /// \param MI - Build-vector instruction to rewrite.
  /// \param MRI - Register info for examining definitions.
  /// \param B - Builder used to emit replacements.
  /// \param UnmergeSrc - Source register of the matched unmerge.
  LLVM_ABI void applyCombineBuildUnmerge(MachineInstr &MI,
                                         MachineRegisterInfo &MRI,
                                         MachineIRBuilder &B,
                                         Register &UnmergeSrc) const;

  /// Match a truncate used only by vector ops that can become a vector truncate.
  ///
  /// \param MI - Scalar truncate instruction.
  /// \param MatchInfo - Register that replaces the truncate use.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchUseVectorTruncate(MachineInstr &MI,
                                       Register &MatchInfo) const;
  /// Rewrite a matched truncate into a vector truncate form.
  ///
  /// \param MI - Scalar truncate instruction to rewrite.
  /// \param MatchInfo - Register that replaces the truncate use.
  LLVM_ABI void applyUseVectorTruncate(MachineInstr &MI,
                                       Register &MatchInfo) const;

  /// Match a G_ICMP that folds to true or false from known bits.
  ///
  /// \param MI - Integer compare instruction.
  /// \param MatchInfo - Constant 0/1 result that replaces the compare.
  /// \return true if the compare can be replaced with a constant.
  LLVM_ABI bool matchICmpToTrueFalseKnownBits(MachineInstr &MI,
                                              int64_t &MatchInfo) const;

  /// Match a G_ICMP that can be replaced with its LHS from known bits.
  ///
  /// \param MI - Integer compare instruction.
  /// \param MatchInfo - Builder callback that emits the replacement.
  /// \return true if the compare can be replaced with its LHS.
  LLVM_ABI bool matchICmpToLHSKnownBits(MachineInstr &MI,
                                        BuildFnTy &MatchInfo) const;

  /// Match (and (or x, c1), c2) that simplifies to (and x, c2).
  ///
  /// \param MI - And instruction root.
  /// \param MatchInfo - Builder callback that emits the simplified form.
  /// \return true if the and/or mask pair is disjoint and can be folded.
  LLVM_ABI bool matchAndOrDisjointMask(MachineInstr &MI,
                                       BuildFnTy &MatchInfo) const;

  /// Match bitfield extract from sext_inreg.
  ///
  /// \param MI - Sext-in-reg instruction root.
  /// \param MatchInfo - Builder callback that emits the bitfield extract.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchBitfieldExtractFromSExtInReg(MachineInstr &MI,
                                                  BuildFnTy &MatchInfo) const;
  /// Match and(lshr x, cst), mask as an unsigned bitfield extract.
  ///
  /// \param MI - And instruction root.
  /// \param MatchInfo - Builder callback that emits the bitfield extract.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchBitfieldExtractFromAnd(MachineInstr &MI,
                                            BuildFnTy &MatchInfo) const;

  /// Match shr(shl x, n), k as a signed/unsigned bitfield extract.
  ///
  /// \param MI - Shift instruction root.
  /// \param MatchInfo - Builder callback that emits the bitfield extract.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchBitfieldExtractFromShr(MachineInstr &MI,
                                            BuildFnTy &MatchInfo) const;

  /// Match shr(and x, n), k as an unsigned bitfield extract.
  ///
  /// \param MI - Shift instruction root.
  /// \param MatchInfo - Builder callback that emits the bitfield extract.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchBitfieldExtractFromShrAnd(MachineInstr &MI,
                                               BuildFnTy &MatchInfo) const;

  /// Match reassociation when a constant sits on the inner RHS of a G_PTR_ADD.
  ///
  /// \param MI - Pointer-add being reassociated.
  /// \param RHS - RHS defining instruction.
  /// \param MatchInfo - Builder callback that emits the reassociated form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchReassocConstantInnerRHS(GPtrAdd &MI, MachineInstr *RHS,
                                             BuildFnTy &MatchInfo) const;
  /// Match folding of constants across a G_PTR_ADD subtree.
  ///
  /// \param MI - Pointer-add being reassociated.
  /// \param LHS - LHS defining instruction.
  /// \param RHS - RHS defining instruction.
  /// \param MatchInfo - Builder callback that emits the folded form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchReassocFoldConstantsInSubTree(GPtrAdd &MI,
                                                   MachineInstr *LHS,
                                                   MachineInstr *RHS,
                                                   BuildFnTy &MatchInfo) const;
  /// Match reassociation when a constant sits on the inner LHS of a G_PTR_ADD.
  ///
  /// \param MI - Pointer-add being reassociated.
  /// \param LHS - LHS defining instruction.
  /// \param RHS - RHS defining instruction.
  /// \param MatchInfo - Builder callback that emits the reassociated form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchReassocConstantInnerLHS(GPtrAdd &MI, MachineInstr *LHS,
                                             MachineInstr *RHS,
                                             BuildFnTy &MatchInfo) const;
  /// Reassociate pointer calculations involving G_ADD for better addressing.
  ///
  /// \param MI - Pointer-add instruction.
  /// \param MatchInfo - Builder callback that emits the reassociated form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchReassocPtrAdd(MachineInstr &MI,
                                   BuildFnTy &MatchInfo) const;

  /// Try to reassociate operands of a commutative binary operation.
  ///
  /// \param Opc - Opcode of the commutative binop.
  /// \param DstReg - Destination register of the binop.
  /// \param Op0 - First operand register.
  /// \param Op1 - Second operand register.
  /// \param MatchInfo - Builder callback that emits the reassociated form.
  /// \return true if the binary operation was reassociated.
  LLVM_ABI bool tryReassocBinOp(unsigned Opc, Register DstReg, Register Op0,
                                Register Op1, BuildFnTy &MatchInfo) const;
  /// Reassociate commutative binary operations such as G_ADD.
  ///
  /// \param MI - Commutative binary operation.
  /// \param MatchInfo - Builder callback that emits the reassociated form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchReassocCommBinOp(MachineInstr &MI,
                                      BuildFnTy &MatchInfo) const;

  /// Constant-fold a cast when opportunities appear after MIR building.
  ///
  /// \param MI - Cast instruction.
  /// \param MatchInfo - Folded integer constant result.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchConstantFoldCastOp(MachineInstr &MI,
                                        APInt &MatchInfo) const;

  /// Constant-fold a binary integer op when opportunities appear after MIR building.
  ///
  /// \param MI - Binary integer operation.
  /// \param MatchInfo - Folded integer constant result.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchConstantFoldBinOp(MachineInstr &MI,
                                       APInt &MatchInfo) const;

  /// Constant-fold a binary FP op when opportunities appear after MIR building.
  ///
  /// \param MI - Binary floating-point operation.
  /// \param MatchInfo - Folded floating-point constant result.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchConstantFoldFPBinOp(MachineInstr &MI,
                                         ConstantFP *&MatchInfo) const;

  /// Constant-fold G_FMA/G_FMAD.
  ///
  /// \param MI - FMA/FMAD instruction.
  /// \param MatchInfo - Folded floating-point constant result.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchConstantFoldFMA(MachineInstr &MI,
                                     ConstantFP *&MatchInfo) const;

  /// Match narrowing a scalar binop that feeds a G_AND.
  ///
  /// \param MI - And instruction fed by a wide binop.
  /// \param MatchInfo - Builder callback that emits the narrowed form.
  /// \return true if the feeding binop can be narrowed.
  LLVM_ABI bool matchNarrowBinopFeedingAnd(MachineInstr &MI,
                                           BuildFnTy &MatchInfo) const;

  /// Build an unsigned divide/remainder-by-constant using a magic multiply.
  ///
  /// Given G_UDIV or G_UREM by a constant, return an expression that implements
  /// it by multiplying by a magic number. See "Hacker's Delight" or "The
  /// PowerPC Compiler Writer's Guide".
  ///
  /// \param MI - Unsigned divide or remainder instruction.
  /// \return The newly built multiply-based expansion instruction.
  LLVM_ABI MachineInstr *buildUDivOrURemUsingMul(MachineInstr &MI) const;
  /// Combine G_UDIV or G_UREM by constant into a multiply by a magic constant.
  ///
  /// \param MI - Unsigned divide or remainder instruction.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchUDivOrURemByConst(MachineInstr &MI) const;
  /// Apply an unsigned divide/remainder-by-constant magic-multiply combine.
  ///
  /// \param MI - Unsigned divide or remainder instruction to rewrite.
  LLVM_ABI void applyUDivOrURemByConst(MachineInstr &MI) const;

  /// Build a signed divide/remainder-by-constant using a magic multiply.
  ///
  /// Given G_SDIV or G_SREM by a constant, return an expression that implements
  /// it by multiplying by a magic number. See "Hacker's Delight" or "The
  /// PowerPC Compiler Writer's Guide".
  ///
  /// \param MI - Signed divide or remainder instruction.
  /// \return The newly built multiply-based expansion instruction.
  LLVM_ABI MachineInstr *buildSDivOrSRemUsingMul(MachineInstr &MI) const;
  /// Combine G_SDIV or G_SREM by constant into a multiply by a magic constant.
  ///
  /// \param MI - Signed divide or remainder instruction.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchSDivOrSRemByConst(MachineInstr &MI) const;
  /// Apply a signed divide/remainder-by-constant magic-multiply combine.
  ///
  /// \param MI - Signed divide or remainder instruction to rewrite.
  LLVM_ABI void applySDivOrSRemByConst(MachineInstr &MI) const;

  /// Match signed or unsigned division by a power of two.
  ///
  /// \param MI - Divide instruction.
  /// \param IsSigned - True when matching signed division.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchDivByPow2(MachineInstr &MI, bool IsSigned) const;
  /// Rewrite signed division by a power of two into shifts.
  ///
  /// \param MI - Signed divide instruction to rewrite.
  LLVM_ABI void applySDivByPow2(MachineInstr &MI) const;
  /// Rewrite unsigned division by a power of two into a shift.
  ///
  /// \param MI - Unsigned divide instruction to rewrite.
  LLVM_ABI void applyUDivByPow2(MachineInstr &MI) const;

  /// Combine G_SREM x, (+/-2^k) into a bias-and-mask sequence.
  ///
  /// \param MI - Signed remainder instruction to rewrite.
  LLVM_ABI void applySimplifySRemByPow2(MachineInstr &MI) const;

  /// Match G_UMULH by a power of two as a logical right shift.
  ///
  /// Pattern: G_UMULH x, (1 << c) -> x >> (bitwidth - c).
  ///
  /// \param MI - G_UMULH instruction.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchUMulHToLShr(MachineInstr &MI) const;
  /// Rewrite matched G_UMULH-by-power-of-two into a logical right shift.
  ///
  /// \param MI - G_UMULH instruction to rewrite.
  LLVM_ABI void applyUMulHToLShr(MachineInstr &MI) const;

  /// Match trunc(smin(smax(x, C1), C2)) as signed saturating truncate.
  ///
  /// Also matches trunc(smax(smin(x, C2), C1)) -> truncssat_s(x).
  ///
  /// \param MI - Truncate instruction root.
  /// \param MatchInfo - Source register for the saturating truncate.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchTruncSSatS(MachineInstr &MI, Register &MatchInfo) const;
  /// Apply a matched signed saturating truncate.
  ///
  /// \param MI - Truncate instruction to rewrite.
  /// \param MatchInfo - Source register for the saturating truncate.
  LLVM_ABI void applyTruncSSatS(MachineInstr &MI, Register &MatchInfo) const;

  /// Match trunc(smin(smax(x, 0), C)) as unsigned saturating truncate from signed.
  ///
  /// Also matches trunc(smax(smin(x, C), 0)) and trunc(umin(smax(x, 0), C)).
  ///
  /// \param MI - Truncate instruction root.
  /// \param MatchInfo - Source register for the saturating truncate.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchTruncSSatU(MachineInstr &MI, Register &MatchInfo) const;
  /// Apply a matched unsigned saturating truncate from a signed clamp.
  ///
  /// \param MI - Truncate instruction to rewrite.
  /// \param MatchInfo - Source register for the saturating truncate.
  LLVM_ABI void applyTruncSSatU(MachineInstr &MI, Register &MatchInfo) const;

  /// Match trunc(umin(x, C)) as unsigned saturating truncate.
  ///
  /// \param MI - Truncate instruction root.
  /// \param MinMI - Unsigned min feeding the truncate.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchTruncUSatU(MachineInstr &MI, MachineInstr &MinMI) const;

  /// Match truncusat_u(fptoui(x)) as fptoui_sat(x).
  ///
  /// \param MI - Saturating truncate instruction.
  /// \param SrcMI - FP-to-UI conversion feeding the truncate.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchTruncUSatUToFPTOUISat(MachineInstr &MI,
                                           MachineInstr &SrcMI) const;

  /// Try every combine implemented by this helper on \p MI.
  ///
  /// \param MI - Instruction to attempt to combine.
  /// \return true if the instruction changed.
  LLVM_ABI bool tryCombine(MachineInstr &MI) const;

  /// Match multiply-with-overflow by two as an add-with-overflow of the value.
  ///
  /// Patterns:
  ///   (G_UMULO x, 2) -> (G_UADDO x, x)
  ///   (G_SMULO x, 2) -> (G_SADDO x, x)
  ///
  /// \param MI - Multiply-with-overflow instruction.
  /// \param MatchInfo - Builder callback that emits the add-with-overflow.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchMulOBy2(MachineInstr &MI, BuildFnTy &MatchInfo) const;

  /// Match multiply-with-overflow by zero to a zero result with no carry.
  ///
  /// Pattern: (G_*MULO x, 0) -> 0 + no carry out.
  ///
  /// \param MI - Multiply-with-overflow instruction.
  /// \param MatchInfo - Builder callback that emits the zero result.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchMulOBy0(MachineInstr &MI, BuildFnTy &MatchInfo) const;

  /// Match add/sub-with-carry with a zero carry-in as add/sub-with-overflow.
  ///
  /// Patterns:
  ///   (G_*ADDE x, y, 0) -> (G_*ADDO x, y)
  ///   (G_*SUBE x, y, 0) -> (G_*SUBO x, y)
  ///
  /// \param MI - Add/sub-with-carry instruction.
  /// \param MatchInfo - Builder callback that emits the overflow form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchAddEToAddO(MachineInstr &MI, BuildFnTy &MatchInfo) const;

  /// Fold redundant floating-point negations into simpler arithmetic.
  ///
  /// Transforms:
  ///   (fadd x, fneg(y)) -> (fsub x, y)
  ///   (fadd fneg(x), y) -> (fsub y, x)
  ///   (fsub x, fneg(y)) -> (fadd x, y)
  ///   (fmul fneg(x), fneg(y)) -> (fmul x, y)
  ///   (fdiv fneg(x), fneg(y)) -> (fdiv x, y)
  ///   (fmad fneg(x), fneg(y), z) -> (fmad x, y, z)
  ///   (fma fneg(x), fneg(y), z) -> (fma x, y, z)
  ///
  /// \param MI - FP instruction with redundant negations.
  /// \param MatchInfo - Builder callback that emits the rewritten form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchRedundantNegOperands(MachineInstr &MI,
                                          BuildFnTy &MatchInfo) const;

  /// Match fsub of zero that can become fneg.
  ///
  /// \param MI - Fsub instruction.
  /// \param MatchInfo - Source register to negate.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchFsubToFneg(MachineInstr &MI, Register &MatchInfo) const;
  /// Rewrite a matched fsub-of-zero into fneg.
  ///
  /// \param MI - Fsub instruction to rewrite.
  /// \param MatchInfo - Source register to negate.
  LLVM_ABI void applyFsubToFneg(MachineInstr &MI, Register &MatchInfo) const;

  /// Return whether \p MI can legally combine into FMA or FMAD.
  ///
  /// \param MI - Candidate fused-multiply instruction root.
  /// \param AllowFusionGlobally - Set when global FP contraction is allowed.
  /// \param HasFMAD - Set when the target provides FMAD.
  /// \param Aggressive - Set when aggressive FMA formation is enabled.
  /// \param CanReassociate - Whether reassociation is allowed for this match.
  /// \return true if an FMad or FMA combine is profitable and legal.
  LLVM_ABI bool canCombineFMadOrFMA(MachineInstr &MI, bool &AllowFusionGlobally,
                                    bool &HasFMAD, bool &Aggressive,
                                    bool CanReassociate = false) const;

  /// Match fadd(fmul x, y), z into fma/fmad.
  ///
  /// \param MI - Fadd instruction root.
  /// \param MatchInfo - Builder callback that emits the fused form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchCombineFAddFMulToFMadOrFMA(MachineInstr &MI,
                                                BuildFnTy &MatchInfo) const;

  /// Match fadd(fpext(fmul x, y)), z into fma/fmad of extended operands.
  ///
  /// \param MI - Fadd instruction root.
  /// \param MatchInfo - Builder callback that emits the fused form.
  /// \return true if the pattern matches.
  LLVM_ABI bool
  matchCombineFAddFpExtFMulToFMadOrFMA(MachineInstr &MI,
                                       BuildFnTy &MatchInfo) const;

  /// Match fadd of fma/fmad and fmul into a nested fused multiply-add.
  ///
  /// \param MI - Fadd instruction root.
  /// \param MatchInfo - Builder callback that emits the fused form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchCombineFAddFMAFMulToFMadOrFMA(MachineInstr &MI,
                                                   BuildFnTy &MatchInfo) const;

  /// Match aggressive fadd(fma ..., fpext(fmul ...)) into nested fma/fmad.
  ///
  /// Transforms:
  ///   (fadd (fma x, y, (fpext (fmul u, v))), z)
  ///     -> (fma x, y, (fma (fpext u), (fpext v), z))
  ///   (fadd (fmad x, y, (fpext (fmul u, v))), z)
  ///     -> (fmad x, y, (fmad (fpext u), (fpext v), z))
  ///
  /// \param MI - Fadd instruction root.
  /// \param MatchInfo - Builder callback that emits the fused form.
  /// \return true if the pattern matches.
  LLVM_ABI bool
  matchCombineFAddFpExtFMulToFMadOrFMAAggressive(MachineInstr &MI,
                                                 BuildFnTy &MatchInfo) const;

  /// Match fsub(fmul x, y), z into fma/fmad with a negated addend.
  ///
  /// \param MI - Fsub instruction root.
  /// \param MatchInfo - Builder callback that emits the fused form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchCombineFSubFMulToFMadOrFMA(MachineInstr &MI,
                                                BuildFnTy &MatchInfo) const;

  /// Match fsub(fneg(fmul x, y)), z into fma/fmad with negated operands.
  ///
  /// \param MI - Fsub instruction root.
  /// \param MatchInfo - Builder callback that emits the fused form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchCombineFSubFNegFMulToFMadOrFMA(MachineInstr &MI,
                                                    BuildFnTy &MatchInfo) const;

  /// Match fsub(fpext(fmul x, y)), z into fma/fmad of extended operands.
  ///
  /// \param MI - Fsub instruction root.
  /// \param MatchInfo - Builder callback that emits the fused form.
  /// \return true if the pattern matches.
  LLVM_ABI bool
  matchCombineFSubFpExtFMulToFMadOrFMA(MachineInstr &MI,
                                       BuildFnTy &MatchInfo) const;

  /// Match fsub(fpext(fneg(fmul x, y))), z into a negated fma/fmad.
  ///
  /// Transforms:
  ///   (fsub (fpext (fneg (fmul x, y))), z)
  ///     -> (fneg (fma (fpext x), (fpext y), z))
  ///   (fsub (fpext (fneg (fmul x, y))), z)
  ///     -> (fneg (fmad (fpext x), (fpext y), z))
  ///
  /// \param MI - Fsub instruction root.
  /// \param MatchInfo - Builder callback that emits the fused form.
  /// \return true if the pattern matches.
  LLVM_ABI bool
  matchCombineFSubFpExtFNegFMulToFMadOrFMA(MachineInstr &MI,
                                           BuildFnTy &MatchInfo) const;

  /// Match fmin/fmax with a NaN operand that can be simplified.
  ///
  /// \param MI - Fmin/fmax instruction.
  /// \param Info - Opcode or operand index used by the apply step.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchCombineFMinMaxNaN(MachineInstr &MI, unsigned &Info) const;

  /// Match repeated FP divisors that can share a reciprocal.
  ///
  /// \param MI - FP divide instruction.
  /// \param MatchInfo - Related divide instructions that share the divisor.
  /// \return true if the pattern matches.
  LLVM_ABI bool
  matchRepeatedFPDivisor(MachineInstr &MI,
                         SmallVector<MachineInstr *> &MatchInfo) const;
  /// Rewrite matched FP divides to reuse a shared reciprocal.
  ///
  /// \param MatchInfo - Related divide instructions that share the divisor.
  LLVM_ABI void
  applyRepeatedFPDivisor(SmallVector<MachineInstr *> &MatchInfo) const;

  /// Match G_ADD(x, G_SUB(y, x)) or G_ADD(G_SUB(y, x), x) folding to y.
  ///
  /// \param MI - Add instruction.
  /// \param Src - Register that replaces the add on success.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchAddSubSameReg(MachineInstr &MI, Register &Src) const;

  /// Match a build_vector that is an identity fold of an existing value.
  ///
  /// \param MI - Build-vector instruction.
  /// \param MatchInfo - Register that replaces the build_vector.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchBuildVectorIdentityFold(MachineInstr &MI,
                                             Register &MatchInfo) const;
  /// Match trunc(build_vector(...)) that can be folded.
  ///
  /// \param MI - Truncate of a build_vector.
  /// \param MatchInfo - Register that replaces the truncate.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchTruncBuildVectorFold(MachineInstr &MI,
                                          Register &MatchInfo) const;
  /// Match trunc(lshr(build_vector(...))) that can be folded.
  ///
  /// \param MI - Truncate of a logical shift of a build_vector.
  /// \param MatchInfo - Register that replaces the truncate.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchTruncLshrBuildVectorFold(MachineInstr &MI,
                                              Register &MatchInfo) const;

  /// Fold add/sub pairs that cancel a shared register operand.
  ///
  /// Transforms:
  ///   (x + y) - y -> x
  ///   (x + y) - x -> y
  ///   x - (y + x) -> 0 - y
  ///   x - (x + z) -> 0 - z
  ///
  /// \param MI - Subtract instruction root.
  /// \param MatchInfo - Builder callback that emits the folded form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchSubAddSameReg(MachineInstr &MI,
                                   BuildFnTy &MatchInfo) const;

  /// Match a select that can be rewritten as a min/max.
  ///
  /// \param MI - Select instruction.
  /// \param MatchInfo - Builder callback that emits the min/max form.
  /// \return true if the select can be simplified to a min/max.
  LLVM_ABI bool matchSimplifySelectToMinMax(MachineInstr &MI,
                                            BuildFnTy &MatchInfo) const;

  /// Fold redundant binops inside equality compares against an operand.
  ///
  /// Transforms:
  ///   (X + Y) == X -> Y == 0
  ///   (X - Y) == X -> Y == 0
  ///   (X ^ Y) == X -> Y == 0
  ///   (X + Y) != X -> Y != 0
  ///   (X - Y) != X -> Y != 0
  ///   (X ^ Y) != X -> Y != 0
  ///
  /// \param MI - Equality compare instruction.
  /// \param MatchInfo - Builder callback that emits the simplified compare.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchRedundantBinOpInEquality(MachineInstr &MI,
                                              BuildFnTy &MatchInfo) const;

  /// Match shifts greater or equal to the value bitwidth.
  ///
  /// \param MI - Shift instruction.
  /// \param MatchInfo - Optional constant result when the shift folds away.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchShiftsTooBig(MachineInstr &MI,
                                  std::optional<int64_t> &MatchInfo) const;

  /// Match constant LHS ops that should be commuted to the RHS.
  ///
  /// \param MI - Binary operation with a constant on the left.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchCommuteConstantToRHS(MachineInstr &MI) const;

  /// Combine sext of trunc.
  ///
  /// \param MO - Operand referring to the sext(trunc) expression.
  /// \param MatchInfo - Builder callback that emits the combined form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchSextOfTrunc(const MachineOperand &MO,
                                 BuildFnTy &MatchInfo) const;

  /// Combine zext of trunc.
  ///
  /// \param MO - Operand referring to the zext(trunc) expression.
  /// \param MatchInfo - Builder callback that emits the combined form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchZextOfTrunc(const MachineOperand &MO,
                                 BuildFnTy &MatchInfo) const;

  /// Combine zext nneg into sext.
  ///
  /// \param MO - Operand referring to the non-negative zext.
  /// \param MatchInfo - Builder callback that emits the sext form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchNonNegZext(const MachineOperand &MO,
                                BuildFnTy &MatchInfo) const;

  /// Match constant LHS FP ops that should be commuted to the RHS.
  ///
  /// \param MI - FP binary operation with a constant on the left.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchCommuteFPConstantToRHS(MachineInstr &MI) const;

  /// Commute operands 1 and 2 of binary operation \p MI.
  ///
  /// \param MI - Binary operation whose operands are swapped.
  LLVM_ABI void applyCommuteBinOpOperands(MachineInstr &MI) const;

  /// Combine select to integer min/max.
  ///
  /// \param MO - Operand referring to the select expression.
  /// \param MatchInfo - Builder callback that emits the min/max form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchSelectIMinMax(const MachineOperand &MO,
                                   BuildFnTy &MatchInfo) const;

  /// Transform (neg (min/max x, (neg x))) into (max/min x, (neg x)).
  ///
  /// \param MI - Negate of a min/max instruction.
  /// \param MatchInfo - Builder callback that emits the rewritten form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchSimplifyNegMinMax(MachineInstr &MI,
                                       BuildFnTy &MatchInfo) const;

  /// Combine selects.
  ///
  /// \param MI - Select instruction.
  /// \param MatchInfo - Builder callback that emits the combined form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchSelect(MachineInstr &MI, BuildFnTy &MatchInfo) const;

  /// Combine ands.
  ///
  /// \param MI - And instruction.
  /// \param MatchInfo - Builder callback that emits the combined form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchAnd(MachineInstr &MI, BuildFnTy &MatchInfo) const;

  /// Combine ors.
  ///
  /// \param MI - Or instruction.
  /// \param MatchInfo - Builder callback that emits the combined form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchOr(MachineInstr &MI, BuildFnTy &MatchInfo) const;

  /// Narrow trunc(binop X, C) to binop(trunc X, trunc C).
  ///
  /// \param TruncMI - Truncate of the binop result.
  /// \param BinopMI - Binary operation feeding the truncate.
  /// \param MatchInfo - Builder callback that emits the narrowed binop.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchNarrowBinop(const MachineInstr &TruncMI,
                                 const MachineInstr &BinopMI,
                                 BuildFnTy &MatchInfo) const;

  /// Match a cast of an integer constant that can be folded.
  ///
  /// \param CastMI - Cast instruction over a constant.
  /// \param MatchInfo - Folded integer constant result.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchCastOfInteger(const MachineInstr &CastMI,
                                   APInt &MatchInfo) const;

  /// Combine add-with-overflow operations.
  ///
  /// \param MI - Add-overflow instruction.
  /// \param MatchInfo - Builder callback that emits the combined form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchAddOverflow(MachineInstr &MI, BuildFnTy &MatchInfo) const;

  /// Combine extract vector element.
  ///
  /// \param MI - Extract-element instruction.
  /// \param MatchInfo - Builder callback that emits the combined form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchExtractVectorElement(MachineInstr &MI,
                                          BuildFnTy &MatchInfo) const;

  /// Combine extract-element with a build_vector on the vector register.
  ///
  /// \param MI - Extract-element instruction.
  /// \param MI2 - Build-vector defining the vector operand.
  /// \param MatchInfo - Builder callback that emits the combined form.
  /// \return true if the pattern matches.
  LLVM_ABI bool
  matchExtractVectorElementWithBuildVector(const MachineInstr &MI,
                                           const MachineInstr &MI2,
                                           BuildFnTy &MatchInfo) const;

  /// Combine extract-element with a truncating build_vector.
  ///
  /// \param MO - Operand referring to the extract of the truncating build.
  /// \param MatchInfo - Builder callback that emits the combined form.
  /// \return true if the pattern matches.
  LLVM_ABI bool
  matchExtractVectorElementWithBuildVectorTrunc(const MachineOperand &MO,
                                                BuildFnTy &MatchInfo) const;

  /// Combine extract-element with a shuffle on the vector register.
  ///
  /// \param MI - Extract-element instruction.
  /// \param MI2 - Shuffle defining the vector operand.
  /// \param MatchInfo - Builder callback that emits the combined form.
  /// \return true if the pattern matches.
  LLVM_ABI bool
  matchExtractVectorElementWithShuffleVector(const MachineInstr &MI,
                                             const MachineInstr &MI2,
                                             BuildFnTy &MatchInfo) const;

  /// Combine extract/insert element pairs with different indices.
  ///
  /// \param MO - Operand referring to the extract of an insert.
  /// \param MatchInfo - Builder callback that emits the combined form.
  /// \return true if the pattern matches.
  LLVM_ABI bool
  matchExtractVectorElementWithDifferentIndices(const MachineOperand &MO,
                                                BuildFnTy &MatchInfo) const;

  /// Remove shuffle references to an undef RHS.
  ///
  /// \param MI - Shuffle instruction.
  /// \param MatchInfo - Builder callback that emits the rewritten shuffle.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchShuffleUndefRHS(MachineInstr &MI,
                                     BuildFnTy &MatchInfo) const;

  /// Rewrite shuffle a, b, mask to shuffle undef, b, mask when mask ignores a.
  ///
  /// \param MI - Shuffle instruction.
  /// \param MatchInfo - Builder callback that emits the rewritten shuffle.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchShuffleDisjointMask(MachineInstr &MI,
                                         BuildFnTy &MatchInfo) const;

  /// Use a build function to combine the instruction defined by \p MO.
  ///
  /// By default, erases the instruction defined on \p MO from the function.
  ///
  /// \param MO - Operand whose defining instruction is rewritten.
  /// \param MatchInfo - Builder callback that emits the replacement.
  LLVM_ABI void applyBuildFnMO(const MachineOperand &MO,
                               BuildFnTy &MatchInfo) const;

  /// Match FPOWI when it is safe to expand into multiplies.
  ///
  /// \param MI - G_FPOWI instruction.
  /// \param Exponent - Constant exponent used for the expansion.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchFPowIExpansion(MachineInstr &MI, int64_t Exponent) const;

  /// Expand FPOWI into multiplies, and a divide when the exponent is negative.
  ///
  /// \param MI - G_FPOWI instruction to expand.
  /// \param Exponent - Constant exponent used for the expansion.
  LLVM_ABI void applyExpandFPowI(MachineInstr &MI, int64_t Exponent) const;

  /// Match an out-of-bounds G_INSERT_VECTOR_ELT.
  ///
  /// \param MI - Insert-element instruction.
  /// \param MatchInfo - Builder callback that emits the replacement.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchInsertVectorElementOOB(MachineInstr &MI,
                                            BuildFnTy &MatchInfo) const;

  /// Match freeze of a single maybe-poison operand that can be simplified.
  ///
  /// \param MI - G_FREEZE instruction.
  /// \param MatchInfo - Builder callback that emits the simplified form.
  /// \return true if the pattern matches.
  LLVM_ABI bool
  matchFreezeOfSingleMaybePoisonOperand(MachineInstr &MI,
                                        BuildFnTy &MatchInfo) const;

  /// Match an add of a vscale that can be folded.
  ///
  /// \param MO - Operand referring to the add of vscale.
  /// \param MatchInfo - Builder callback that emits the folded form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchAddOfVScale(const MachineOperand &MO,
                                 BuildFnTy &MatchInfo) const;

  /// Match a multiply of a vscale that can be folded.
  ///
  /// \param MO - Operand referring to the multiply of vscale.
  /// \param MatchInfo - Builder callback that emits the folded form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchMulOfVScale(const MachineOperand &MO,
                                 BuildFnTy &MatchInfo) const;

  /// Match a subtract of a vscale that can be folded.
  ///
  /// \param MO - Operand referring to the subtract of vscale.
  /// \param MatchInfo - Builder callback that emits the folded form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchSubOfVScale(const MachineOperand &MO,
                                 BuildFnTy &MatchInfo) const;

  /// Match a shift-left of a vscale that can be folded.
  ///
  /// \param MO - Operand referring to the shift of vscale.
  /// \param MatchInfo - Builder callback that emits the folded form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchShlOfVScale(const MachineOperand &MO,
                                 BuildFnTy &MatchInfo) const;

  /// Transform trunc ([asz]ext x) to x, ([asz]ext x), or (trunc x).
  ///
  /// \param Root - Truncate instruction being matched.
  /// \param ExtMI - Extend feeding the truncate.
  /// \param MatchInfo - Builder callback that emits the simplified form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchTruncateOfExt(const MachineInstr &Root,
                                   const MachineInstr &ExtMI,
                                   BuildFnTy &MatchInfo) const;

  /// Match a cast of a select that can be rewritten more simply.
  ///
  /// \param Cast - Cast instruction over the select.
  /// \param SelectMI - Select feeding the cast.
  /// \param MatchInfo - Builder callback that emits the rewritten form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchCastOfSelect(const MachineInstr &Cast,
                                  const MachineInstr &SelectMI,
                                  BuildFnTy &MatchInfo) const;
  /// Fold (A + C1) - C2 into A + (C1 - C2).
  ///
  /// \param MI - Subtract instruction root.
  /// \param MatchInfo - Builder callback that emits the folded form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchFoldAPlusC1MinusC2(const MachineInstr &MI,
                                        BuildFnTy &MatchInfo) const;

  /// Fold C2 - (A + C1) into (C2 - C1) - A.
  ///
  /// \param MI - Subtract instruction root.
  /// \param MatchInfo - Builder callback that emits the folded form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchFoldC2MinusAPlusC1(const MachineInstr &MI,
                                        BuildFnTy &MatchInfo) const;

  /// Fold (A - C1) - C2 into A - (C1 + C2).
  ///
  /// \param MI - Subtract instruction root.
  /// \param MatchInfo - Builder callback that emits the folded form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchFoldAMinusC1MinusC2(const MachineInstr &MI,
                                         BuildFnTy &MatchInfo) const;

  /// Fold C1 - (A - C2) into (C1 + C2) - A.
  ///
  /// \param MI - Subtract instruction root.
  /// \param MatchInfo - Builder callback that emits the folded form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchFoldC1Minus2MinusC2(const MachineInstr &MI,
                                         BuildFnTy &MatchInfo) const;

  /// Fold ((A - C1) + C2) into (A + (C2 - C1)).
  ///
  /// \param MI - Add instruction root.
  /// \param MatchInfo - Builder callback that emits the folded form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchFoldAMinusC1PlusC2(const MachineInstr &MI,
                                        BuildFnTy &MatchInfo) const;

  /// Match nested extends that can be merged into a single extend.
  ///
  /// \param FirstMI - Outer extend instruction.
  /// \param SecondMI - Inner extend instruction.
  /// \param MatchInfo - Builder callback that emits the merged extend.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchExtOfExt(const MachineInstr &FirstMI,
                              const MachineInstr &SecondMI,
                              BuildFnTy &MatchInfo) const;

  /// Match a cast of a build_vector that can be pushed into the elements.
  ///
  /// \param CastMI - Cast of the build_vector.
  /// \param BVMI - Build-vector feeding the cast.
  /// \param MatchInfo - Builder callback that emits the rewritten form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchCastOfBuildVector(const MachineInstr &CastMI,
                                       const MachineInstr &BVMI,
                                       BuildFnTy &MatchInfo) const;

  /// Canonicalize a G_ICMP for simpler matching and codegen.
  ///
  /// \param MI - Integer compare instruction.
  /// \param MatchInfo - Builder callback that emits the canonical form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchCanonicalizeICmp(const MachineInstr &MI,
                                      BuildFnTy &MatchInfo) const;
  /// Canonicalize a G_FCMP for simpler matching and codegen.
  ///
  /// \param MI - Floating-point compare instruction.
  /// \param MatchInfo - Builder callback that emits the canonical form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchCanonicalizeFCmp(const MachineInstr &MI,
                                      BuildFnTy &MatchInfo) const;

  /// Match unmerge_values(anyext(build_vector)) to build_vector(anyext).
  ///
  /// \param MI - Unmerge instruction root.
  /// \param MatchInfo - Builder callback that emits the rewritten form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchUnmergeValuesAnyExtBuildVector(const MachineInstr &MI,
                                                    BuildFnTy &MatchInfo) const;

  /// Match merge_values(x, undef) into an any-extend of x.
  ///
  /// \param MI - Merge instruction root.
  /// \param MatchInfo - Builder callback that emits the any-extend.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchMergeXAndUndef(const MachineInstr &MI,
                                    BuildFnTy &MatchInfo) const;

  /// Match merge_values(x, zero) into a zero-extend of x.
  ///
  /// \param MI - Merge instruction root.
  /// \param MatchInfo - Builder callback that emits the zero-extend.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchMergeXAndZero(const MachineInstr &MI,
                                   BuildFnTy &MatchInfo) const;

  /// Match an overflowing subtract whose carry-out can be simplified.
  ///
  /// \param MI - Overflowing subtract instruction.
  /// \param MatchInfo - Builder callback that emits the simplified form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchSuboCarryOut(const MachineInstr &MI,
                                  BuildFnTy &MatchInfo) const;

  /// Match redundant nested G_SEXT_INREG with widths K0 and K1.
  ///
  /// Pattern: (sext_inreg (sext_inreg x, K0), K1).
  ///
  /// \param Root - Outer G_SEXT_INREG instruction.
  /// \param Other - Inner G_SEXT_INREG instruction.
  /// \param MatchInfo - Builder callback that emits the combined form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchRedundantSextInReg(MachineInstr &Root, MachineInstr &Other,
                                        BuildFnTy &MatchInfo) const;

  /// Match count-leading-sign-bits idioms expressed with G_CTLZ.
  ///
  /// Patterns:
  /// (ctlz (xor x, (sra x, bitwidth-1))) -> (add (ctls x), 1) or
  /// (ctlz (or (shl (xor x, (sra x, bitwidth-1)), 1), 1) -> (ctls x).
  ///
  /// \param CtlzMI - G_CTLZ instruction to match.
  /// \param MatchInfo - Builder callback that emits the CTLS form.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchCtls(MachineInstr &CtlzMI, BuildFnTy &MatchInfo) const;

  /// Match an average idiom and rewrite it to \p TargetOpc.
  ///
  /// \param MI - Instruction root of the average pattern.
  /// \param MRI - Register info for examining defining instructions.
  /// \param X - First averaged register operand.
  /// \param Y - Second averaged register operand.
  /// \param TargetOpc - Average opcode to emit on a successful match.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchAVG(MachineInstr &MI, MachineRegisterInfo &MRI, Register X,
                         Register Y, unsigned TargetOpc) const;

  /// Match a count-zeros op that can use the zero-poison variant.
  ///
  /// \param MI - Count-zeros instruction to match.
  /// \return true if the pattern matches.
  LLVM_ABI bool matchCountZeroToZeroPoison(MachineInstr &MI) const;
  /// Rewrite a matched count-zeros op to its zero-poison variant.
  ///
  /// \param MI - Count-zeros instruction to rewrite.
  LLVM_ABI void applyCountZeroToZeroPoison(MachineInstr &MI) const;

private:
  /// Checks for legality of an indexed variant of \p LdSt.
  bool isIndexedLoadStoreLegal(GLoadStore &LdSt) const;

  /// Helper function for matchBinopWithNeg: tries to match one commuted form
  /// of `a bitwiseop (~b +/- c)` -> `a bitwiseop ~(b -/+ c)`.
  bool matchBinopWithNegInner(Register MInner, Register Other, unsigned RootOpc,
                              Register Dst, LLT Ty, BuildFnTy &MatchInfo) const;
  /// Given a non-indexed load or store instruction \p MI, find an offset that
  /// can be usefully and legally folded into it as a post-indexing operation.
  ///
  /// \returns true if a candidate is found.
  bool findPostIndexCandidate(GLoadStore &MI, Register &Addr, Register &Base,
                              Register &Offset, bool &RematOffset) const;

  /// Given a non-indexed load or store instruction \p MI, find an offset that
  /// can be usefully and legally folded into it as a pre-indexing operation.
  ///
  /// \returns true if a candidate is found.
  bool findPreIndexCandidate(GLoadStore &MI, Register &Addr, Register &Base,
                             Register &Offset) const;

  /// Helper function for matchLoadOrCombine. Searches for Registers
  /// which may have been produced by a load instruction + some arithmetic.
  ///
  /// \param [in] Root - The search root.
  ///
  /// \returns The Registers found during the search.
  std::optional<SmallVector<Register, 8>>
  findCandidatesForLoadOrCombine(const MachineInstr *Root) const;

  /// Helper function for matchLoadOrCombine.
  ///
  /// Checks if every register in \p RegsToVisit is defined by a load
  /// instruction + some arithmetic.
  ///
  /// \param [out] MemOffset2Idx - Maps the byte positions each load ends up
  /// at to the index of the load.
  /// \param [in] MemSizeInBits - The number of bits each load should produce.
  ///
  /// \returns On success, a 3-tuple containing lowest-index load found, the
  /// lowest index, and the last load in the sequence.
  std::optional<std::tuple<GZExtLoad *, int64_t, GZExtLoad *>>
  findLoadOffsetsForLoadOrCombine(
      SmallDenseMap<int64_t, int64_t, 8> &MemOffset2Idx,
      const SmallVector<Register, 8> &RegsToVisit,
      const unsigned MemSizeInBits) const;

  /// Examines the G_PTR_ADD instruction \p PtrAdd and determines if performing
  /// a re-association of its operands would break an existing legal addressing
  /// mode that the address computation currently represents.
  bool reassociationCanBreakAddressingModePattern(MachineInstr &PtrAdd) const;

  /// Behavior when a floating point min/max is given one NaN and one
  /// non-NaN as input.
  enum class SelectPatternNaNBehaviour {
    NOT_APPLICABLE = 0, /// NaN behavior not applicable.
    RETURNS_NAN,        /// Given one NaN input, returns the NaN.
    RETURNS_OTHER,      /// Given one NaN input, returns the non-NaN.
    RETURNS_ANY /// Given one NaN input, can return either (or both operands are
                /// known non-NaN.)
  };

  /// \returns which of \p LHS and \p RHS would be the result of a non-equality
  /// floating point comparison where one of \p LHS and \p RHS may be NaN.
  ///
  /// If both \p LHS and \p RHS may be NaN, returns
  /// SelectPatternNaNBehaviour::NOT_APPLICABLE.
  SelectPatternNaNBehaviour
  computeRetValAgainstNaN(Register LHS, Register RHS,
                          bool IsOrderedComparison) const;

  /// Determines the floating point min/max opcode which should be used for
  /// a G_SELECT fed by a G_FCMP with predicate \p Pred.
  ///
  /// \returns 0 if this G_SELECT should not be combined to a floating point
  /// min or max. If it should be combined, returns one of
  ///
  /// * G_FMAXNUM
  /// * G_FMAXIMUM
  /// * G_FMINNUM
  /// * G_FMINIMUM
  ///
  /// Helper function for matchFPSelectToMinMax.
  unsigned getFPMinMaxOpcForSelect(CmpInst::Predicate Pred, LLT DstTy,
                                   SelectPatternNaNBehaviour VsNaNRetVal) const;

  /// Handle floating point cases for matchSimplifySelectToMinMax.
  ///
  /// E.g.
  ///
  /// select (fcmp uge x, 1.0) x, 1.0 -> fmax x, 1.0
  /// select (fcmp uge x, 1.0) 1.0, x -> fminnm x, 1.0
  bool matchFPSelectToMinMax(Register Dst, Register Cond, Register TrueVal,
                             Register FalseVal, BuildFnTy &MatchInfo) const;

  /// Try to fold selects to logical operations.
  bool tryFoldBoolSelectToLogic(GSelect *Select, BuildFnTy &MatchInfo) const;

  bool tryFoldSelectOfConstants(GSelect *Select, BuildFnTy &MatchInfo) const;

  bool isOneOrOneSplat(Register Src, bool AllowUndefs) const;
  bool isZeroOrZeroSplat(Register Src, bool AllowUndefs) const;
  bool isConstantSplatVector(Register Src, int64_t SplatValue,
                             bool AllowUndefs) const;
  bool isConstantOrConstantVectorI(Register Src) const;

  std::optional<APInt> getConstantOrConstantSplatVector(Register Src) const;

  /// Fold (icmp Pred1 V1, C1) && (icmp Pred2 V2, C2)
  /// or   (icmp Pred1 V1, C1) || (icmp Pred2 V2, C2)
  /// into a single comparison using range-based reasoning.
  bool tryFoldAndOrOrICmpsUsingRanges(GLogicalBinOp *Logic,
                                      BuildFnTy &MatchInfo) const;

  // Simplify (cmp cc0 x, y) (&& or ||) (cmp cc1 x, y) -> cmp cc2 x, y.
  bool tryFoldLogicOfFCmps(GLogicalBinOp *Logic, BuildFnTy &MatchInfo) const;

  bool isCastFree(unsigned Opcode, LLT ToTy, LLT FromTy) const;

  bool constantFoldICmp(const GICmp &ICmp, const GIConstant &LHSCst,
                        const GIConstant &RHSCst, BuildFnTy &MatchInfo) const;
  bool constantFoldFCmp(const GFCmp &FCmp, const GFConstant &LHSCst,
                        const GFConstant &RHSCst, BuildFnTy &MatchInfo) const;
};
} // namespace llvm

#endif
