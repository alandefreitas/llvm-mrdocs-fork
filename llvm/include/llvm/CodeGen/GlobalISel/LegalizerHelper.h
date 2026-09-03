//== llvm/CodeGen/GlobalISel/LegalizerHelper.h ---------------- -*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file A pass to convert the target-illegal operations created by IR -> MIR
/// translation into ones the target expects to be able to select. This may
/// occur in multiple phases, for example G_ADD <2 x i8> -> G_ADD <2 x i16> ->
/// G_ADD <4 x i16>.
///
/// The LegalizerHelper class is where most of the work happens, and is
/// designed to be callable from other passes that find themselves with an
/// illegal instruction.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALISEL_LEGALIZERHELPER_H
#define LLVM_CODEGEN_GLOBALISEL_LEGALIZERHELPER_H

#include "llvm/CodeGen/GlobalISel/CallLowering.h"
#include "llvm/CodeGen/GlobalISel/GISelValueTracking.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/RuntimeLibcallUtil.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
// Forward declarations.
class APInt;
class GAnyLoad;
class GLoadStore;
class GStore;
class GenericMachineInstr;
class MachineFunction;
class MachineIRBuilder;
class MachineInstr;
class MachineInstrBuilder;
struct MachinePointerInfo;
template <typename T> class SmallVectorImpl;
class LegalizerInfo;
class MachineRegisterInfo;
class GISelChangeObserver;
class LostDebugLocObserver;
class TargetLowering;

/// Helper that rewrites illegal Generic MIR into legal sequences.
class LegalizerHelper {
public:
  /// Expose MIRBuilder so clients can set their own RecordInsertInstruction
  /// functions
  MachineIRBuilder &MIRBuilder;

  /// To keep track of changes made by the LegalizerHelper.
  GISelChangeObserver &Observer;

private:
  MachineRegisterInfo &MRI;
  const LegalizerInfo &LI;
  const TargetLowering &TLI;

  const LibcallLoweringInfo *Libcalls = nullptr;
  GISelValueTracking *VT = nullptr;

public:
  /// Result of attempting to legalize a single instruction.
  enum LegalizeResult {
    /// Instruction was already legal and no change was made to the
    /// MachineFunction.
    AlreadyLegal,

    /// Instruction has been legalized and the MachineFunction changed.
    Legalized,

    /// Some kind of error has occurred and we could not legalize this
    /// instruction.
    UnableToLegalize,
  };

  /// Expose LegalizerInfo so the clients can re-use.
  ///
  /// \return Legalizer info used by this helper.
  const LegalizerInfo &getLegalizerInfo() const { return LI; }
  /// Return the TargetLowering used for target-specific hooks.
  ///
  /// \return Target lowering for the current function.
  const TargetLowering &getTargetLowering() const { return TLI; }
  /// Return the optional libcall lowering info, if configured.
  ///
  /// \return Libcall lowering info, or null when none was provided.
  const LibcallLoweringInfo *getLibcallLoweringInfo() { return Libcalls; }
  /// Return the optional value-tracking analysis, if configured.
  ///
  /// \return Value tracking analysis, or null when none was provided.
  GISelValueTracking *getValueTracking() const { return VT; }

  // FIXME: Should probably make Libcalls mandatory
  /// Construct a helper that uses the function's default LegalizerInfo.
  ///
  /// \param MF Machine function being legalized.
  /// \param Observer Observer notified of instruction changes.
  /// \param B Builder used to insert replacement instructions.
  /// \param Libcalls Optional libcall lowering info.
  LLVM_ABI LegalizerHelper(MachineFunction &MF, GISelChangeObserver &Observer,
                           MachineIRBuilder &B,
                           const LibcallLoweringInfo *Libcalls = nullptr);
  /// Construct a helper with an explicit LegalizerInfo and optional analyses.
  ///
  /// \param MF Machine function being legalized.
  /// \param LI Legalizer info describing legal operations and actions.
  /// \param Observer Observer notified of instruction changes.
  /// \param B Builder used to insert replacement instructions.
  /// \param Libcalls Optional libcall lowering info.
  /// \param VT Optional value-tracking analysis.
  LLVM_ABI LegalizerHelper(MachineFunction &MF, const LegalizerInfo &LI,
                           GISelChangeObserver &Observer, MachineIRBuilder &B,
                           const LibcallLoweringInfo *Libcalls = nullptr,
                           GISelValueTracking *VT = nullptr);

  /// Replace \p MI by a sequence of legal instructions.
  ///
  /// Note that this means \p MI may be deleted, so any iterator steps should be
  /// performed before calling this function. \p Helper should be initialized to
  /// the MachineFunction containing \p MI.
  ///
  /// Considered as an opaque blob, the legal code will use and define the same
  /// registers as \p MI.
  ///
  /// \param MI Instruction to legalize.
  /// \param LocObserver Observer that tracks lost debug locations.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult legalizeInstrStep(MachineInstr &MI,
                                            LostDebugLocObserver &LocObserver);

  /// Legalize an instruction by emitting a runtime library call instead.
  ///
  /// \param MI Instruction to replace with a libcall.
  /// \param LocObserver Observer that tracks lost debug locations.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult libcall(MachineInstr &MI,
                                  LostDebugLocObserver &LocObserver);

  /// Legalize an instruction by reducing the width of the underlying scalar
  /// type.
  ///
  /// \param MI Instruction being legalized.
  /// \param TypeIdx Type index to narrow.
  /// \param NarrowTy Narrower scalar type to use.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult narrowScalar(MachineInstr &MI, unsigned TypeIdx,
                                       LLT NarrowTy);

  /// Legalize an instruction by performing it on a wider scalar type.
  ///
  /// For example a 16-bit addition can be safely performed at 32-bits
  /// precision, ignoring the unused bits.
  ///
  /// \param MI Instruction being legalized.
  /// \param TypeIdx Type index to widen.
  /// \param WideTy Wider scalar type to use.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult widenScalar(MachineInstr &MI, unsigned TypeIdx,
                                      LLT WideTy);

  /// Legalize an instruction by replacing the value type.
  ///
  /// \param MI Instruction being legalized.
  /// \param TypeIdx Type index to bitcast.
  /// \param Ty Replacement type with the same bit width.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult bitcast(MachineInstr &MI, unsigned TypeIdx, LLT Ty);

  /// Legalize an instruction by splitting it into simpler parts.
  ///
  /// \param MI Instruction being legalized.
  /// \param TypeIdx Type index that triggered the lower action.
  /// \param Ty Type associated with the lower action.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lower(MachineInstr &MI, unsigned TypeIdx, LLT Ty);

  /// Legalize a vector by splitting it into fewer-element components.
  ///
  /// \param MI Instruction being legalized.
  /// \param TypeIdx Type index to split.
  /// \param NarrowTy Vector type with fewer elements.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult fewerElementsVector(MachineInstr &MI,
                                              unsigned TypeIdx, LLT NarrowTy);

  /// Legalize a vector by increasing the number of elements.
  ///
  /// \param MI Instruction being legalized.
  /// \param TypeIdx Type index to widen in element count.
  /// \param MoreTy Vector type with more elements.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult moreElementsVector(MachineInstr &MI, unsigned TypeIdx,
                                             LLT MoreTy);

  /// Cast \p Val to an integer LLT of the same size when needed.
  ///
  /// Returns the register to use if an instruction was inserted. Returns the
  /// original register if no coercion was necessary.
  ///
  /// This may also fail and return Register() if there is no legal way to cast.
  ///
  /// \param Val Register whose type may need integer coercion.
  /// \return Coerced integer register, the original register, or an empty register on failure.
  LLVM_ABI Register coerceToInteger(Register Val);

  /// Extend a use operand of \p MI to \p WideTy in place.
  ///
  /// Legalize a single operand \p OpIdx of the machine instruction \p MI as a
  /// Use by extending the operand's type to \p WideTy using the specified \p
  /// ExtOpcode for the extension instruction, and replacing the vreg of the
  /// operand in place.
  ///
  /// \param MI Instruction whose use operand is widened.
  /// \param WideTy Wider scalar type for the operand.
  /// \param OpIdx Operand index to legalize as a use.
  /// \param ExtOpcode Extension opcode used to widen the operand.
  LLVM_ABI void widenScalarSrc(MachineInstr &MI, LLT WideTy, unsigned OpIdx,
                               unsigned ExtOpcode);

  /// Extend a use operand of \p MI with G_FPEXT in place.
  ///
  /// Legalize a single operand \p OpIdx of the machine instruction \p MI as a
  /// Use by extending the operand's type to \p WideTy using the G_FPEXT for the
  /// extension instruction, and replacing the vreg of the operand in place.
  /// Flags are copied from MI to the new extend.
  ///
  /// \param MI Instruction whose use operand is widened.
  /// \param WideTy Wider floating-point type for the operand.
  /// \param OpIdx Operand index to legalize as a use.
  LLVM_ABI void widenScalarSrcUsingFPExt(MachineInstr &MI, LLT WideTy,
                                         unsigned OpIdx);

  /// Truncate a use operand of \p MI to \p NarrowTy in place.
  ///
  /// Legalize a single operand \p OpIdx of the machine instruction \p MI as a
  /// Use by truncating the operand's type to \p NarrowTy using G_TRUNC, and
  /// replacing the vreg of the operand in place.
  ///
  /// \param MI Instruction whose use operand is narrowed.
  /// \param NarrowTy Narrower scalar type for the operand.
  /// \param OpIdx Operand index to legalize as a use.
  LLVM_ABI void narrowScalarSrc(MachineInstr &MI, LLT NarrowTy, unsigned OpIdx);

  /// Widen a def operand of \p MI and truncate the result back in place.
  ///
  /// Legalize a single operand \p OpIdx of the machine instruction \p MI as a
  /// Def by extending the operand's type to \p WideTy and truncating it back
  /// with the \p TruncOpcode, and replacing the vreg of the operand in place.
  ///
  /// \param MI Instruction whose def operand is widened.
  /// \param WideTy Wider scalar type used for the operation.
  /// \param OpIdx Operand index to legalize as a def.
  /// \param TruncOpcode Truncation opcode used to restore the original type.
  LLVM_ABI void widenScalarDst(MachineInstr &MI, LLT WideTy, unsigned OpIdx = 0,
                               unsigned TruncOpcode = TargetOpcode::G_TRUNC);

  /// Widen a def operand of \p MI and truncate it with G_FPTRUNC.
  ///
  /// Legalize a single operand \p OpIdx of the machine instruction \p MI as a
  /// Def by extending the operand's type to \p WideTy and truncating it back
  /// with G_FPTRUNC, and replacing the vreg of the operand in place. Flags are
  /// copied from MI to the new trunc.
  ///
  /// \param MI Instruction whose def operand is widened.
  /// \param WideTy Wider floating-point type used for the operation.
  /// \param OpIdx Operand index to legalize as a def.
  LLVM_ABI void widenScalarDstUsingFPTrunc(MachineInstr &MI, LLT WideTy,
                                           unsigned OpIdx = 0);

  /// Narrow a def operand of \p MI and extend the result back in place.
  ///
  /// Legalize a single operand \p OpIdx of the machine instruction \p MI as a
  /// Def by truncating the operand's type to \p NarrowTy, replacing in place
  /// and extending back with \p ExtOpcode.
  ///
  /// \param MI Instruction whose def operand is narrowed.
  /// \param NarrowTy Narrower scalar type used for the operation.
  /// \param OpIdx Operand index to legalize as a def.
  /// \param ExtOpcode Extension opcode used to restore the original type.
  LLVM_ABI void narrowScalarDst(MachineInstr &MI, LLT NarrowTy, unsigned OpIdx,
                                unsigned ExtOpcode);
  /// Produce a wider-vector def and extract the original elements in place.
  ///
  /// Legalize a single operand \p OpIdx of the machine instruction \p MI as a
  /// Def by performing it with additional vector elements and extracting the
  /// result elements, and replacing the vreg of the operand in place.
  ///
  /// \param MI Instruction whose def operand gains more elements.
  /// \param MoreTy Vector type with additional elements.
  /// \param OpIdx Operand index to legalize as a def.
  LLVM_ABI void moreElementsVectorDst(MachineInstr &MI, LLT MoreTy,
                                      unsigned OpIdx);

  /// Pad a use operand of \p MI with undefined high vector elements.
  ///
  /// Legalize a single operand \p OpIdx of the machine instruction \p MI as a
  /// Use by producing a vector with undefined high elements, extracting the
  /// original vector type, and replacing the vreg of the operand in place.
  ///
  /// \param MI Instruction whose use operand gains more elements.
  /// \param MoreTy Vector type with additional elements.
  /// \param OpIdx Operand index to legalize as a use.
  LLVM_ABI void moreElementsVectorSrc(MachineInstr &MI, LLT MoreTy,
                                      unsigned OpIdx);

  /// Insert a G_BITCAST before a use operand of \p MI.
  ///
  /// \param MI Instruction whose use operand is bitcast.
  /// \param CastTy Type of the bitcast result used by the operand.
  /// \param OpIdx Operand index to legalize as a use.
  LLVM_ABI void bitcastSrc(MachineInstr &MI, LLT CastTy, unsigned OpIdx);

  /// Insert a G_BITCAST after a def operand of \p MI.
  ///
  /// \param MI Instruction whose def operand is bitcast.
  /// \param CastTy Type produced by the instruction before bitcasting back.
  /// \param OpIdx Operand index to legalize as a def.
  LLVM_ABI void bitcastDst(MachineInstr &MI, LLT CastTy, unsigned OpIdx);

  /// Emit a simple libcall where all operands share the same type.
  ///
  /// \param MI Instruction being replaced by a libcall.
  /// \param MIRBuilder Builder used to insert the call sequence.
  /// \param Size Operand size in bits used to select the libcall.
  /// \param OpType IR type shared by the libcall operands.
  /// \param LocObserver Observer that tracks lost debug locations.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult
  simpleLibcall(MachineInstr &MI, MachineIRBuilder &MIRBuilder, unsigned Size,
                Type *OpType, LostDebugLocObserver &LocObserver) const;

  /// Create a libcall to the given \p Name using calling convention \p CC.
  ///
  /// \param Name Mangled libcall name to emit.
  /// \param Result Call result argument info.
  /// \param Args Call argument infos.
  /// \param CC Calling convention for the libcall.
  /// \param LocObserver Observer that tracks lost debug locations.
  /// \param MI Optional original instruction providing debug location.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult createLibcall(const char *Name,
                                        const CallLowering::ArgInfo &Result,
                                        ArrayRef<CallLowering::ArgInfo> Args,
                                        CallingConv::ID CC,
                                        LostDebugLocObserver &LocObserver,
                                        MachineInstr *MI = nullptr) const;

  /// Create a libcall for the given RTLIB identifier.
  ///
  /// \param Libcall Runtime libcall identifier to emit.
  /// \param Result Call result argument info.
  /// \param Args Call argument infos.
  /// \param LocObserver Observer that tracks lost debug locations.
  /// \param MI Optional original instruction providing debug location.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult createLibcall(RTLIB::Libcall Libcall,
                                        const CallLowering::ArgInfo &Result,
                                        ArrayRef<CallLowering::ArgInfo> Args,
                                        LostDebugLocObserver &LocObserver,
                                        MachineInstr *MI = nullptr) const;

  /// Lower a conversion instruction by emitting a conversion libcall.
  ///
  /// \param MI Conversion instruction to replace.
  /// \param ToType Destination IR type of the conversion.
  /// \param FromType Source IR type of the conversion.
  /// \param LocObserver Observer that tracks lost debug locations.
  /// \param IsSigned True for a signed conversion libcall.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult conversionLibcall(MachineInstr &MI, Type *ToType,
                                            Type *FromType,
                                            LostDebugLocObserver &LocObserver,
                                            bool IsSigned = false) const;
  /// Lower an atomic instruction by emitting an atomic libcall.
  ///
  /// \param MI Atomic instruction to replace.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizerHelper::LegalizeResult
  createAtomicLibcall(MachineInstr &MI) const;

  /// Create a libcall to memcpy et al.
  ///
  /// \param MRI Register info for the function being legalized.
  /// \param MI Memory intrinsic instruction to replace.
  /// \param LocObserver Observer that tracks lost debug locations.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult
  createMemLibcall(MachineRegisterInfo &MRI, MachineInstr &MI,
                   LostDebugLocObserver &LocObserver) const;

private:
  LegalizeResult
  widenScalarMergeValues(MachineInstr &MI, unsigned TypeIdx, LLT WideTy);
  LegalizeResult
  widenScalarUnmergeValues(MachineInstr &MI, unsigned TypeIdx, LLT WideTy);
  LegalizeResult
  widenScalarExtract(MachineInstr &MI, unsigned TypeIdx, LLT WideTy);
  LegalizeResult
  widenScalarInsert(MachineInstr &MI, unsigned TypeIdx, LLT WideTy);
  LegalizeResult widenScalarAddSubOverflow(MachineInstr &MI, unsigned TypeIdx,
                                           LLT WideTy);
  LegalizeResult widenScalarAddSubShlSat(MachineInstr &MI, unsigned TypeIdx,
                                         LLT WideTy);
  LegalizeResult widenScalarMulo(MachineInstr &MI, unsigned TypeIdx,
                                 LLT WideTy);

  /// Helper function to build a wide generic register \p DstReg of type \p
  /// RegTy from smaller parts. This will produce a G_MERGE_VALUES,
  /// G_BUILD_VECTOR, G_CONCAT_VECTORS, or sequence of G_INSERT as appropriate
  /// for the types.
  ///
  /// \p PartRegs must be registers of type \p PartTy.
  ///
  /// If \p ResultTy does not evenly break into \p PartTy sized pieces, the
  /// remainder must be specified with \p LeftoverRegs of type \p LeftoverTy.
  void insertParts(Register DstReg, LLT ResultTy,
                   LLT PartTy, ArrayRef<Register> PartRegs,
                   LLT LeftoverTy = LLT(), ArrayRef<Register> LeftoverRegs = {});

  /// Merge \p PartRegs with different types into \p DstReg.
  void mergeMixedSubvectors(Register DstReg, ArrayRef<Register> PartRegs);

  void appendVectorElts(SmallVectorImpl<Register> &Elts, Register Reg);

  /// Unmerge \p SrcReg into smaller sized values, and append them to \p
  /// Parts. The elements of \p Parts will be the greatest common divisor type
  /// of \p DstTy, \p NarrowTy and the type of \p SrcReg. This will compute and
  /// return the GCD type.
  LLT extractGCDType(SmallVectorImpl<Register> &Parts, LLT DstTy,
                     LLT NarrowTy, Register SrcReg);

  /// Unmerge \p SrcReg into \p GCDTy typed registers. This will append all of
  /// the unpacked registers to \p Parts. This version is if the common unmerge
  /// type is already known.
  void extractGCDType(SmallVectorImpl<Register> &Parts, LLT GCDTy,
                      Register SrcReg);

  /// Produce a merge of values in \p VRegs to define \p DstReg. Perform a merge
  /// from the least common multiple type, and convert as appropriate to \p
  /// DstReg.
  ///
  /// \p VRegs should each have type \p GCDTy. This type should be greatest
  /// common divisor type of \p DstReg, \p NarrowTy, and an undetermined source
  /// type.
  ///
  /// \p NarrowTy is the desired result merge source type. If the source value
  /// needs to be widened to evenly cover \p DstReg, inserts high bits
  /// corresponding to the extension opcode \p PadStrategy.
  ///
  /// \p VRegs will be cleared, and the result \p NarrowTy register pieces
  /// will replace it. Returns The complete LCMTy that \p VRegs will cover when
  /// merged.
  LLT buildLCMMergePieces(LLT DstTy, LLT NarrowTy, LLT GCDTy,
                          SmallVectorImpl<Register> &VRegs,
                          unsigned PadStrategy = TargetOpcode::G_ANYEXT);

  /// Merge the values in \p RemergeRegs to an \p LCMTy typed value. Extract the
  /// low bits into \p DstReg. This is intended to use the outputs from
  /// buildLCMMergePieces after processing.
  void buildWidenedRemergeToDst(Register DstReg, LLT LCMTy,
                                ArrayRef<Register> RemergeRegs);

  /// Perform generic multiplication of values held in multiple registers.
  /// Generated instructions use only types NarrowTy and i1.
  /// Destination can be same or two times size of the source.
  void multiplyRegisters(SmallVectorImpl<Register> &DstRegs,
                         ArrayRef<Register> Src1Regs,
                         ArrayRef<Register> Src2Regs, LLT NarrowTy);

  void changeOpcode(MachineInstr &MI, unsigned NewOpcode);

  LegalizeResult tryNarrowPow2Reduction(MachineInstr &MI, Register SrcReg,
                                        LLT SrcTy, LLT NarrowTy,
                                        unsigned ScalarOpc);

  // Memcpy family legalization helpers.
  LegalizeResult lowerMemset(MachineInstr &MI, Register Dst, Register Val,
                             uint64_t KnownLen, Align Alignment,
                             bool DstAlignCanChange, ArrayRef<LLT> MemOps);
  LegalizeResult lowerMemcpy(MachineInstr &MI, Register Dst, Register Src,
                             uint64_t KnownLen, Align Alignment,
                             bool DstAlignCanChange, ArrayRef<LLT> MemOps);
  LegalizeResult lowerMemmove(MachineInstr &MI, Register Dst, Register Src,
                              uint64_t KnownLen, Align Alignment,
                              bool DstAlignCanChange, ArrayRef<LLT> MemOps);

  // Implements floating-point environment read/write via library function call.
  LegalizeResult createGetStateLibcall(MachineInstr &MI,
                                       LostDebugLocObserver &LocObserver);
  LegalizeResult createSetStateLibcall(MachineInstr &MI,
                                       LostDebugLocObserver &LocObserver);
  LegalizeResult createResetStateLibcall(MachineInstr &MI,
                                         LostDebugLocObserver &LocObserver);
  LegalizeResult createFCMPLibcall(MachineInstr &MI,
                                   LostDebugLocObserver &LocObserver);

  MachineInstrBuilder
  getNeutralElementForVecReduce(unsigned Opcode, MachineIRBuilder &MIRBuilder,
                                LLT Ty);

  LegalizeResult emitSincosLibcall(MachineInstr &MI,
                                   MachineIRBuilder &MIRBuilder, unsigned Size,
                                   Type *OpType,
                                   LostDebugLocObserver &LocObserver);

  LegalizeResult emitModfLibcall(MachineInstr &MI, MachineIRBuilder &MIRBuilder,
                                 unsigned Size, Type *OpType,
                                 LostDebugLocObserver &LocObserver);

public:
  /// Return the alignment to use for a stack temporary of type \p Type.
  ///
  /// \param Type Type of the stack temporary.
  /// \param MinAlign Minimum alignment required by the caller.
  /// \return Alignment to use for the stack temporary.
  LLVM_ABI Align getStackTemporaryAlignment(LLT Type,
                                            Align MinAlign = Align()) const;

  /// Create a stack temporary of \p Bytes bytes with \p Alignment.
  ///
  /// \param Bytes Size of the temporary in bytes.
  /// \param Alignment Required alignment of the temporary.
  /// \param PtrInfo Filled with pointer info for the created frame index.
  /// \return Builder for the frame-index address of the temporary.
  LLVM_ABI MachineInstrBuilder createStackTemporary(
      TypeSize Bytes, Align Alignment, MachinePointerInfo &PtrInfo);

  /// Store \p Val to a stack temporary and reload it as \p Res.
  ///
  /// \param Res Destination operand describing the reloaded value type.
  /// \param Val Source value stored through the temporary.
  /// \return Builder for the reload of the stored value.
  LLVM_ABI MachineInstrBuilder createStackStoreLoad(const DstOp &Res,
                                                    const SrcOp &Val);

  /// Scalarize a store of a boolean vector.
  ///
  /// \param MI Boolean vector store to scalarize.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult scalarizeVectorBooleanStore(GStore &MI);

  /// Get a pointer to vector element \p Index in memory.
  ///
  /// The vector has type \p VecTy and starts at base address \p VecPtr. If \p
  /// Index is out of bounds the returned pointer is unspecified, but will be
  /// within the vector bounds.
  ///
  /// \param VecPtr Base address of the vector in memory.
  /// \param VecTy Type of the vector being addressed.
  /// \param Index Element index within the vector.
  /// \return Pointer to the selected vector element.
  LLVM_ABI Register getVectorElementPointer(Register VecPtr, LLT VecTy,
                                            Register Index);

  /// Split \p MI into fewer-element operations of size \p NumElts.
  ///
  /// Handles most opcodes. Split \p MI into same instruction on sub-vectors or
  /// scalars with \p NumElts elements (1 for scalar). Supports uneven splits:
  /// there can be leftover sub-vector with fewer then \p NumElts or a leftover
  /// scalar. To avoid this use moreElements first and set MI number of elements
  /// to multiple of \p NumElts. Non-vector operands that should be used on all
  /// sub-instructions without split are listed in \p NonVecOpIndices.
  ///
  /// \param MI Instruction being split into fewer-element pieces.
  /// \param NumElts Element count of each split sub-vector or scalar.
  /// \param NonVecOpIndices Operand indices reused unchanged on every piece.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult fewerElementsVectorMultiEltType(
      GenericMachineInstr &MI, unsigned NumElts,
      std::initializer_list<unsigned> NonVecOpIndices = {});

  /// Split a vector PHI into fewer-element PHI pieces.
  ///
  /// \param MI PHI instruction being split.
  /// \param NumElts Element count of each split PHI piece.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult fewerElementsVectorPhi(GenericMachineInstr &MI,
                                                 unsigned NumElts);

  /// Widen a vector PHI to a type with more elements.
  ///
  /// \param MI PHI instruction being widened.
  /// \param TypeIdx Type index to widen.
  /// \param MoreTy Vector type with more elements.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult moreElementsVectorPhi(MachineInstr &MI,
                                                unsigned TypeIdx, LLT MoreTy);
  /// Widen a shuffle to operate on vectors with more elements.
  ///
  /// \param MI Shuffle instruction being widened.
  /// \param TypeIdx Type index to widen.
  /// \param MoreTy Vector type with more elements.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult moreElementsVectorShuffle(MachineInstr &MI,
                                                    unsigned TypeIdx,
                                                    LLT MoreTy);

  /// Split an unmerge into fewer-element unmerge pieces.
  ///
  /// \param MI Unmerge instruction being split.
  /// \param TypeIdx Type index to narrow.
  /// \param NarrowTy Vector type with fewer elements.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult fewerElementsVectorUnmergeValues(MachineInstr &MI,
                                                           unsigned TypeIdx,
                                                           LLT NarrowTy);
  /// Split a merge into fewer-element merge pieces.
  ///
  /// \param MI Merge instruction being split.
  /// \param TypeIdx Type index to narrow.
  /// \param NarrowTy Vector type with fewer elements.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult fewerElementsVectorMerge(MachineInstr &MI,
                                                   unsigned TypeIdx,
                                                   LLT NarrowTy);
  /// Split extract/insert vector element into fewer-element pieces.
  ///
  /// \param MI Extract or insert element instruction being split.
  /// \param TypeIdx Type index to narrow.
  /// \param NarrowTy Vector type with fewer elements.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult fewerElementsVectorExtractInsertVectorElt(
      MachineInstr &MI, unsigned TypeIdx, LLT NarrowTy);

  /// Equalize source and destination vector sizes of G_SHUFFLE_VECTOR.
  ///
  /// \param MI Shuffle instruction whose vector lengths are equalized.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult equalizeVectorShuffleLengths(MachineInstr &MI);

  /// Reduce the data width of a load or store instruction.
  ///
  /// \param MI Load or store being narrowed.
  /// \param TypeIdx Type index to narrow.
  /// \param NarrowTy Narrower memory or value type.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult reduceLoadStoreWidth(GLoadStore &MI, unsigned TypeIdx,
                                               LLT NarrowTy);

  /// Narrow a scalar shift by a known constant amount.
  ///
  /// \param MI Shift instruction being narrowed.
  /// \param Amt Constant shift amount.
  /// \param HalfTy Half-width type used for the split shift.
  /// \param ShiftAmtTy Type of the shift-amount operands.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult narrowScalarShiftByConstant(MachineInstr &MI,
                                                      const APInt &Amt,
                                                      LLT HalfTy,
                                                      LLT ShiftAmtTy);

  /// Split a wide shift into target-sized parts in one step.
  ///
  /// Multi-way shift legalization: directly split wide shifts into target-sized
  /// parts in a single step, avoiding recursive binary splitting.
  ///
  /// \param MI Shift instruction being narrowed.
  /// \param TargetTy Target-sized part type for the split.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult narrowScalarShiftMultiway(MachineInstr &MI,
                                                    LLT TargetTy);

  /// Narrow a constant shift with direct multi-way indexing.
  ///
  /// Optimized path for constant shift amounts using static indexing. Directly
  /// calculates which source parts contribute to each output part without
  /// generating runtime select chains.
  ///
  /// \param MI Shift instruction being narrowed.
  /// \param Amt Constant shift amount.
  /// \param TargetTy Target-sized part type for the split.
  /// \param ShiftAmtTy Type of the shift-amount operands.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult narrowScalarShiftByConstantMultiway(MachineInstr &MI,
                                                              const APInt &Amt,
                                                              LLT TargetTy,
                                                              LLT ShiftAmtTy);

  /// Cached registers used while emitting multi-way shift parts.
  struct ShiftParams {
    /// Number of complete words to shift.
    Register WordShift;
    /// Number of bits to shift within words.
    Register BitShift;
    /// Complement bit shift (TargetBits - BitShift).
    Register InvBitShift;
    /// Zero constant for SHL/LSHR fill.
    Register Zero;
    /// Sign extension value for ASHR fill.
    Register SignBit;
  };

  /// Generate one output part for a constant multi-way shift.
  ///
  /// Calculates which source parts contribute and how they're combined.
  ///
  /// \param Opcode Shift opcode being legalized.
  /// \param PartIdx Index of the output part being built.
  /// \param NumParts Total number of output parts.
  /// \param SrcParts Source value split into target-sized parts.
  /// \param Params Precomputed shift amounts and fill values.
  /// \param TargetTy Type of each shift part.
  /// \param ShiftAmtTy Type of the shift-amount operands.
  /// \return Register holding the computed output shift part.
  LLVM_ABI Register buildConstantShiftPart(unsigned Opcode, unsigned PartIdx,
                                           unsigned NumParts,
                                           ArrayRef<Register> SrcParts,
                                           const ShiftParams &Params,
                                           LLT TargetTy, LLT ShiftAmtTy);

  /// Generate one shift part with carry for a variable multi-way shift.
  ///
  /// Combines main operand shifted by BitShift with carry bits from adjacent
  /// operand.
  ///
  /// \param Opcode Shift opcode being legalized.
  /// \param MainOperand Primary source part being shifted.
  /// \param ShiftAmt Intra-word bit shift amount.
  /// \param TargetTy Type of each shift part.
  /// \param CarryOperand Adjacent part providing shifted-in bits, if any.
  /// \return Register holding the computed shift part with carry.
  LLVM_ABI Register buildVariableShiftPart(unsigned Opcode,
                                           Register MainOperand,
                                           Register ShiftAmt, LLT TargetTy,
                                           Register CarryOperand = Register());

  /// Split a vector reduction into fewer-element reductions.
  ///
  /// \param MI Reduction instruction being split.
  /// \param TypeIdx Type index to narrow.
  /// \param NarrowTy Vector type with fewer elements.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult fewerElementsVectorReductions(MachineInstr &MI,
                                                        unsigned TypeIdx,
                                                        LLT NarrowTy);
  /// Split a sequential vector reduction into fewer-element pieces.
  ///
  /// \param MI Sequential reduction instruction being split.
  /// \param TypeIdx Type index to narrow.
  /// \param NarrowTy Vector type with fewer elements.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult fewerElementsVectorSeqReductions(MachineInstr &MI,
                                                           unsigned TypeIdx,
                                                           LLT NarrowTy);

  /// Split a bitcast while keeping source and destination sizes equal.
  ///
  /// \param MI Bitcast instruction being split.
  /// \param TypeIdx Type index to narrow.
  /// \param NarrowTy Narrower type used for the split pieces.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult fewerElementsBitcast(MachineInstr &MI,
                                               unsigned TypeIdx, LLT NarrowTy);

  /// Split a shuffle into fewer-element shuffle pieces.
  ///
  /// \param MI Shuffle instruction being split.
  /// \param TypeIdx Type index to narrow.
  /// \param NarrowTy Vector type with fewer elements.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult fewerElementsVectorShuffle(MachineInstr &MI,
                                                     unsigned TypeIdx,
                                                     LLT NarrowTy);

  /// Narrow a scalar shift into operations on a smaller type.
  ///
  /// \param MI Shift instruction being narrowed.
  /// \param TypeIdx Type index to narrow.
  /// \param Ty Narrower scalar type.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult narrowScalarShift(MachineInstr &MI, unsigned TypeIdx,
                                            LLT Ty);
  /// Narrow a scalar add or subtract into smaller-type operations.
  ///
  /// \param MI Add or subtract instruction being narrowed.
  /// \param TypeIdx Type index to narrow.
  /// \param NarrowTy Narrower scalar type.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult narrowScalarAddSub(MachineInstr &MI, unsigned TypeIdx,
                                             LLT NarrowTy);
  /// Narrow a scalar multiply into smaller-type operations.
  ///
  /// \param MI Multiply instruction being narrowed.
  /// \param Ty Narrower scalar type.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult narrowScalarMul(MachineInstr &MI, LLT Ty);
  /// Narrow a floating-point to integer conversion.
  ///
  /// \param MI Conversion instruction being narrowed.
  /// \param TypeIdx Type index to narrow.
  /// \param Ty Narrower scalar type.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult narrowScalarFPTOI(MachineInstr &MI, unsigned TypeIdx,
                                            LLT Ty);
  /// Narrow a scalar extract into smaller-type operations.
  ///
  /// \param MI Extract instruction being narrowed.
  /// \param TypeIdx Type index to narrow.
  /// \param Ty Narrower scalar type.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult narrowScalarExtract(MachineInstr &MI,
                                              unsigned TypeIdx, LLT Ty);
  /// Narrow a scalar insert into smaller-type operations.
  ///
  /// \param MI Insert instruction being narrowed.
  /// \param TypeIdx Type index to narrow.
  /// \param Ty Narrower scalar type.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult narrowScalarInsert(MachineInstr &MI, unsigned TypeIdx,
                                             LLT Ty);

  /// Narrow a basic scalar arithmetic or bitwise instruction.
  ///
  /// \param MI Instruction being narrowed.
  /// \param TypeIdx Type index to narrow.
  /// \param Ty Narrower scalar type.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult narrowScalarBasic(MachineInstr &MI, unsigned TypeIdx,
                                            LLT Ty);
  /// Narrow a scalar extend into smaller-type operations.
  ///
  /// \param MI Extend instruction being narrowed.
  /// \param TypeIdx Type index to narrow.
  /// \param Ty Narrower scalar type.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult narrowScalarExt(MachineInstr &MI, unsigned TypeIdx,
                                          LLT Ty);
  /// Narrow a scalar select into smaller-type operations.
  ///
  /// \param MI Select instruction being narrowed.
  /// \param TypeIdx Type index to narrow.
  /// \param Ty Narrower scalar type.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult narrowScalarSelect(MachineInstr &MI, unsigned TypeIdx,
                                             LLT Ty);
  /// Narrow a scalar CTLZ into smaller-type operations.
  ///
  /// \param MI CTLZ instruction being narrowed.
  /// \param TypeIdx Type index to narrow.
  /// \param Ty Narrower scalar type.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult narrowScalarCTLZ(MachineInstr &MI, unsigned TypeIdx,
                                           LLT Ty);
  /// Narrow a scalar CTTZ into smaller-type operations.
  ///
  /// \param MI CTTZ instruction being narrowed.
  /// \param TypeIdx Type index to narrow.
  /// \param Ty Narrower scalar type.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult narrowScalarCTTZ(MachineInstr &MI, unsigned TypeIdx,
                                           LLT Ty);
  /// Narrow a scalar CTLZ/CTTZ-style count into smaller-type operations.
  ///
  /// \param MI Bit-count instruction being narrowed.
  /// \param TypeIdx Type index to narrow.
  /// \param Ty Narrower scalar type.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult narrowScalarCTLS(MachineInstr &MI, unsigned TypeIdx,
                                           LLT Ty);
  /// Narrow a scalar CTPOP into smaller-type operations.
  ///
  /// \param MI CTPOP instruction being narrowed.
  /// \param TypeIdx Type index to narrow.
  /// \param Ty Narrower scalar type.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult narrowScalarCTPOP(MachineInstr &MI, unsigned TypeIdx,
                                            LLT Ty);
  /// Narrow a scalar FLDEXP into smaller-type operations.
  ///
  /// \param MI FLDEXP instruction being narrowed.
  /// \param TypeIdx Type index to narrow.
  /// \param Ty Narrower scalar type.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult narrowScalarFLDEXP(MachineInstr &MI, unsigned TypeIdx,
                                             LLT Ty);

  /// Perform Bitcast legalize action on G_EXTRACT_VECTOR_ELT.
  ///
  /// \param MI Extract-element instruction being bitcast-legalized.
  /// \param TypeIdx Type index to bitcast.
  /// \param CastTy Type used for the bitcast legalization.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult bitcastExtractVectorElt(MachineInstr &MI,
                                                  unsigned TypeIdx, LLT CastTy);

  /// Perform Bitcast legalize action on G_INSERT_VECTOR_ELT.
  ///
  /// \param MI Insert-element instruction being bitcast-legalized.
  /// \param TypeIdx Type index to bitcast.
  /// \param CastTy Type used for the bitcast legalization.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult bitcastInsertVectorElt(MachineInstr &MI,
                                                 unsigned TypeIdx, LLT CastTy);
  /// Bitcast-legalize a vector concatenation.
  ///
  /// \param MI Concatenation instruction being bitcast-legalized.
  /// \param TypeIdx Type index to bitcast.
  /// \param CastTy Type used for the bitcast legalization.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult bitcastConcatVector(MachineInstr &MI,
                                              unsigned TypeIdx, LLT CastTy);
  /// Bitcast-legalize a vector shuffle.
  ///
  /// \param MI Shuffle instruction being bitcast-legalized.
  /// \param TypeIdx Type index to bitcast.
  /// \param CastTy Type used for the bitcast legalization.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult bitcastShuffleVector(MachineInstr &MI,
                                               unsigned TypeIdx, LLT CastTy);
  /// Bitcast-legalize an extract-subvector.
  ///
  /// \param MI Extract-subvector instruction being bitcast-legalized.
  /// \param TypeIdx Type index to bitcast.
  /// \param CastTy Type used for the bitcast legalization.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult bitcastExtractSubvector(MachineInstr &MI,
                                                  unsigned TypeIdx, LLT CastTy);
  /// Bitcast-legalize an insert-subvector.
  ///
  /// \param MI Insert-subvector instruction being bitcast-legalized.
  /// \param TypeIdx Type index to bitcast.
  /// \param CastTy Type used for the bitcast legalization.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult bitcastInsertSubvector(MachineInstr &MI,
                                                 unsigned TypeIdx, LLT CastTy);

  /// Lower G_CONSTANT into target-legal materialization.
  ///
  /// \param MI Constant instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerConstant(MachineInstr &MI);
  /// Lower G_FCONSTANT into target-legal materialization.
  ///
  /// \param MI Floating-point constant instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerFConstant(MachineInstr &MI);
  /// Lower G_BITCAST into equivalent legal operations.
  ///
  /// \param MI Bitcast instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerBitcast(MachineInstr &MI);
  /// Lower a generic load into legal memory operations.
  ///
  /// \param MI Load instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerLoad(GAnyLoad &MI);
  /// Lower a generic store into legal memory operations.
  ///
  /// \param MI Store instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerStore(GStore &MI);
  /// Lower a bit-counting instruction into simpler operations.
  ///
  /// \param MI Bit-count instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerBitCount(MachineInstr &MI);
  /// Lower a funnel shift using the inverse funnel-shift opcode.
  ///
  /// \param MI Funnel-shift instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerFunnelShiftWithInverse(MachineInstr &MI);
  /// Lower a funnel shift into a sequence of ordinary shifts.
  ///
  /// \param MI Funnel-shift instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerFunnelShiftAsShifts(MachineInstr &MI);
  /// Lower a funnel shift into target-legal operations.
  ///
  /// \param MI Funnel-shift instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerFunnelShift(MachineInstr &MI);
  /// Lower an integer extend into target-legal operations.
  ///
  /// \param MI Extend instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerEXT(MachineInstr &MI);
  /// Lower an integer truncate into target-legal operations.
  ///
  /// \param MI Truncate instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerTRUNC(MachineInstr &MI);
  /// Lower a rotate using the reverse rotate direction.
  ///
  /// \param MI Rotate instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerRotateWithReverseRotate(MachineInstr &MI);
  /// Lower a rotate into target-legal operations.
  ///
  /// \param MI Rotate instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerRotate(MachineInstr &MI);

  /// Lower uint64-to-float conversion using bit operations to f32.
  ///
  /// \param MI Conversion instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerU64ToF32BitOps(MachineInstr &MI);
  /// Lower uint64-to-float conversion via signed integer-to-float.
  ///
  /// \param MI Conversion instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerU64ToF32WithSITOFP(MachineInstr &MI);
  /// Lower uint64-to-double conversion using floating-point bit ops.
  ///
  /// \param MI Conversion instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerU64ToF64BitFloatOps(MachineInstr &MI);
  /// Lower G_UITOFP into target-legal operations.
  ///
  /// \param MI Unsigned integer-to-float conversion to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerUITOFP(MachineInstr &MI);
  /// Lower G_SITOFP into target-legal operations.
  ///
  /// \param MI Signed integer-to-float conversion to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerSITOFP(MachineInstr &MI);
  /// Lower G_FPTOUI into target-legal operations.
  ///
  /// \param MI Float-to-unsigned conversion to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerFPTOUI(MachineInstr &MI);
  /// Lower G_FPTOSI into target-legal operations.
  ///
  /// \param MI Float-to-signed conversion to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerFPTOSI(MachineInstr &MI);
  /// Lower a saturating float-to-integer conversion.
  ///
  /// \param MI Saturating conversion instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerFPTOINT_SAT(MachineInstr &MI);

  /// Lower float extend/truncate via memory round-trip when needed.
  ///
  /// \param MI Extend or truncate instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerFPExtAndTruncMem(MachineInstr &MI);
  /// Lower G_FPEXT into target-legal operations.
  ///
  /// \param MI Floating-point extend instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerFPEXT(MachineInstr &MI);
  /// Lower a bfloat16 floating-point extend.
  ///
  /// \param MI BF16 floating-point extend instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerFPEXT_BF16(MachineInstr &MI);
  /// Lower f64-to-f16 floating-point truncation.
  ///
  /// \param MI F64-to-F16 truncate instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerFPTRUNC_F64_TO_F16(MachineInstr &MI);
  /// Lower f32-to-bf16 floating-point truncation.
  ///
  /// \param MI F32-to-BF16 truncate instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerFPTRUNC_F32_TO_BF16(MachineInstr &MI);
  /// Round \p Op inexactly to odd and return a value of \p ResultTy.
  ///
  /// \param ResultTy Result type of the rounded value.
  /// \param Op Source floating-point value to round.
  /// \return Register holding the value rounded inexactly to odd.
  LLVM_ABI Register lowerRoundInexactToOdd(LLT ResultTy, Register Op);
  /// Lower f64-to-bf16 floating-point truncation.
  ///
  /// \param MI F64-to-BF16 truncate instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerFPTRUNC_F64_TO_BF16(MachineInstr &MI);
  /// Lower G_FPTRUNC into target-legal operations.
  ///
  /// \param MI Floating-point truncate instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerFPTRUNC(MachineInstr &MI);
  /// Lower G_FPOWI into target-legal operations.
  ///
  /// \param MI Floating-point power instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerFPOWI(MachineInstr &MI);
  /// Lower G_FMODF into target-legal operations.
  ///
  /// \param MI Floating-point modf instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerFMODF(MachineInstr &MI);

  /// Lower G_ISFPCLASS into target-legal comparisons.
  ///
  /// \param MI Floating-point class test to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerISFPCLASS(MachineInstr &MI);

  /// Lower a three-way compare into target-legal comparisons.
  ///
  /// \param MI Three-way compare instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerThreewayCompare(MachineInstr &MI);
  /// Lower integer min/max into compares and selects.
  ///
  /// \param MI Min or max instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerMinMax(MachineInstr &MI);
  /// Lower G_FCOPYSIGN into target-legal bit operations.
  ///
  /// \param MI Copysign instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerFCopySign(MachineInstr &MI);
  /// Lower G_FMINNUM/G_FMAXNUM into target-legal operations.
  ///
  /// \param MI Floating-point minnum/maxnum instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerFMinNumMaxNum(MachineInstr &MI);
  /// Lower G_FMINIMUM/G_FMAXIMUM into target-legal operations.
  ///
  /// \param MI Floating-point minimum/maximum instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerFMinimumMaximum(MachineInstr &MI);
  /// Lower G_FMAD into a multiply and add sequence.
  ///
  /// \param MI Fused multiply-add instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerFMad(MachineInstr &MI);
  /// Lower an intrinsic round into target-legal operations.
  ///
  /// \param MI Rounding intrinsic instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerIntrinsicRound(MachineInstr &MI);
  /// Lower G_FFLOOR into target-legal operations.
  ///
  /// \param MI Floor instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerFFloor(MachineInstr &MI);
  /// Lower G_MERGE_VALUES into target-legal operations.
  ///
  /// \param MI Merge instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerMergeValues(MachineInstr &MI);
  /// Lower G_UNMERGE_VALUES into target-legal operations.
  ///
  /// \param MI Unmerge instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerUnmergeValues(MachineInstr &MI);
  /// Lower extract/insert vector element into legal operations.
  ///
  /// \param MI Extract or insert element instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerExtractInsertVectorElt(MachineInstr &MI);
  /// Lower G_SHUFFLE_VECTOR into target-legal operations.
  ///
  /// \param MI Shuffle instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerShuffleVector(MachineInstr &MI);
  /// Lower G_VECTOR_COMPRESS into target-legal operations.
  ///
  /// \param MI Vector compress instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerVECTOR_COMPRESS(MachineInstr &MI);
  /// Compute the target pointer for a dynamic stack allocation.
  ///
  /// \param SPReg Current stack-pointer register.
  /// \param AllocSize Size of the allocation in bytes.
  /// \param Alignment Required alignment of the allocation.
  /// \param PtrTy Pointer type of the returned address.
  /// \return Pointer to the dynamically allocated stack region.
  LLVM_ABI Register getDynStackAllocTargetPtr(Register SPReg,
                                              Register AllocSize,
                                              Align Alignment, LLT PtrTy);
  /// Lower a dynamic stack allocation into stack-pointer updates.
  ///
  /// \param MI Dynamic stack allocation instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerDynStackAlloc(MachineInstr &MI);
  /// Lower G_STACKSAVE into a stack-pointer copy.
  ///
  /// \param MI Stack-save instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerStackSave(MachineInstr &MI);
  /// Lower G_STACKRESTORE into a stack-pointer restore.
  ///
  /// \param MI Stack-restore instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerStackRestore(MachineInstr &MI);
  /// Lower G_EXTRACT into target-legal operations.
  ///
  /// \param MI Extract instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerExtract(MachineInstr &MI);
  /// Lower G_INSERT into target-legal operations.
  ///
  /// \param MI Insert instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerInsert(MachineInstr &MI);
  /// Lower signed add/sub-with-overflow into legal operations.
  ///
  /// \param MI Signed overflow add or sub instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerSADDO_SSUBO(MachineInstr &MI);
  /// Lower signed add-with-carry-in into legal operations.
  ///
  /// \param MI Signed add-with-carry instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerSADDE(MachineInstr &MI);
  /// Lower signed sub-with-carry-in into legal operations.
  ///
  /// \param MI Signed sub-with-carry instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerSSUBE(MachineInstr &MI);
  /// Lower saturating add/sub using min/max clamping.
  ///
  /// \param MI Saturating add or sub instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerAddSubSatToMinMax(MachineInstr &MI);
  /// Lower saturating add/sub using overflow arithmetic.
  ///
  /// \param MI Saturating add or sub instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerAddSubSatToAddoSubo(MachineInstr &MI);
  /// Lower a saturating left shift into legal operations.
  ///
  /// \param MI Saturating shift instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerShlSat(MachineInstr &MI);
  /// Lower a saturating truncate into legal operations.
  ///
  /// \param MI Saturating truncate instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerTruncSat(MachineInstr &MI);
  /// Lower G_BSWAP into target-legal operations.
  ///
  /// \param MI Byte-swap instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerBswap(MachineInstr &MI);
  /// Lower G_BITREVERSE into target-legal operations.
  ///
  /// \param MI Bit-reverse instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerBitreverse(MachineInstr &MI);
  /// Lower read/write register intrinsics into legal operations.
  ///
  /// \param MI Read or write register instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerReadWriteRegister(MachineInstr &MI);
  /// Lower high-half multiply into wider multiply and extract.
  ///
  /// \param MI SMULH or UMULH instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerSMULH_UMULH(MachineInstr &MI);
  /// Lower G_SELECT into target-legal operations.
  ///
  /// \param MI Select instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerSelect(MachineInstr &MI);
  /// Lower a divrem into separate divide and remainder.
  ///
  /// \param MI Divrem instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerDIVREM(MachineInstr &MI);
  /// Lower absolute value using add and xor.
  ///
  /// \param MI Absolute-value instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerAbsToAddXor(MachineInstr &MI);
  /// Lower absolute value using max and negation.
  ///
  /// \param MI Absolute-value instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerAbsToMaxNeg(MachineInstr &MI);
  /// Lower absolute value using conditional negation.
  ///
  /// \param MI Absolute-value instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerAbsToCNeg(MachineInstr &MI);
  /// Lower absolute difference using a select sequence.
  ///
  /// \param MI Absolute-difference instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerAbsDiffToSelect(MachineInstr &MI);
  /// Lower absolute difference using min and max.
  ///
  /// \param MI Absolute-difference instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerAbsDiffToMinMax(MachineInstr &MI);
  /// Lower G_FABS into target-legal operations.
  ///
  /// \param MI Floating-point absolute-value instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerFAbs(MachineInstr &MI);
  /// Lower a vector reduction into target-legal operations.
  ///
  /// \param MI Vector reduction instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerVectorReduction(MachineInstr &MI);
  /// Lower a memcpy-family intrinsic with known operands and length.
  ///
  /// \param MI Memcpy-family instruction to lower.
  /// \param Dst Destination pointer.
  /// \param Src Source pointer.
  /// \param KnownLen Known copy length in bytes.
  /// \param Alignment Access alignment used for lowering.
  /// \param DstAlignCanChange Whether destination alignment may increase.
  /// \param MemOps Preferred memory operation types for expansion.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerMemCpyFamily(MachineInstr &MI, Register Dst,
                                            Register Src, uint64_t KnownLen,
                                            Align Alignment,
                                            bool DstAlignCanChange,
                                            ArrayRef<LLT> MemOps);
  /// Lower a memcpy-family intrinsic, optionally capped by \p MaxLen.
  ///
  /// \param MI Memcpy-family instruction to lower.
  /// \param MaxLen Maximum inline expansion length, or 0 for no limit.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerMemCpyFamily(MachineInstr &MI,
                                            unsigned MaxLen = 0);
  /// Lower G_VAARG into target-legal operations.
  ///
  /// \param MI VAArg instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerVAArg(MachineInstr &MI);
  /// Lower a fixed-point multiply into extend, multiply, and shift.
  ///
  /// \param MI Fixed-point multiply instruction to lower.
  /// \return Result of attempting to legalize the instruction.
  LLVM_ABI LegalizeResult lowerMulfix(MachineInstr &MI);
};

} // End namespace llvm.

#endif
