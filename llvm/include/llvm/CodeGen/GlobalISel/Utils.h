//==-- llvm/CodeGen/GlobalISel/Utils.h ---------------------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file This file declares the API of helper functions used throughout the
/// GlobalISel pipeline.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALISEL_UTILS_H
#define LLVM_CODEGEN_GLOBALISEL_UTILS_H

#include "GISelWorkList.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/CodeGenTypes/LowLevelType.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"

#include <cstdint>
#include <vector>

namespace llvm {

class AnalysisUsage;
class LostDebugLocObserver;
class MachineBasicBlock;
class BlockFrequencyInfo;
class GISelValueTracking;
class MachineFunction;
class MachineInstr;
class MachineIRBuilder;
class MachineOperand;
class MachineOptimizationRemarkEmitter;
class MachineOptimizationRemarkMissed;
struct MachinePointerInfo;
class MachineRegisterInfo;
class MCInstrDesc;
class ProfileSummaryInfo;
class RegisterBankInfo;
class TargetInstrInfo;
class TargetLowering;
class TargetPassConfig;
class TargetRegisterInfo;
class MCRegisterClass;
using TargetRegisterClass = MCRegisterClass;
class ConstantFP;
class APFloat;

// Convenience macros for dealing with vector reduction opcodes.
#define GISEL_VECREDUCE_CASES_ALL                                              \
  case TargetOpcode::G_VECREDUCE_SEQ_FADD:                                     \
  case TargetOpcode::G_VECREDUCE_SEQ_FMUL:                                     \
  case TargetOpcode::G_VECREDUCE_FADD:                                         \
  case TargetOpcode::G_VECREDUCE_FMUL:                                         \
  case TargetOpcode::G_VECREDUCE_FMAX:                                         \
  case TargetOpcode::G_VECREDUCE_FMIN:                                         \
  case TargetOpcode::G_VECREDUCE_FMAXIMUM:                                     \
  case TargetOpcode::G_VECREDUCE_FMINIMUM:                                     \
  case TargetOpcode::G_VECREDUCE_FMAXIMUMNUM:                                  \
  case TargetOpcode::G_VECREDUCE_FMINIMUMNUM:                                  \
  case TargetOpcode::G_VECREDUCE_ADD:                                          \
  case TargetOpcode::G_VECREDUCE_MUL:                                          \
  case TargetOpcode::G_VECREDUCE_AND:                                          \
  case TargetOpcode::G_VECREDUCE_OR:                                           \
  case TargetOpcode::G_VECREDUCE_XOR:                                          \
  case TargetOpcode::G_VECREDUCE_SMAX:                                         \
  case TargetOpcode::G_VECREDUCE_SMIN:                                         \
  case TargetOpcode::G_VECREDUCE_UMAX:                                         \
  case TargetOpcode::G_VECREDUCE_UMIN:

#define GISEL_VECREDUCE_CASES_NONSEQ                                           \
  case TargetOpcode::G_VECREDUCE_FADD:                                         \
  case TargetOpcode::G_VECREDUCE_FMUL:                                         \
  case TargetOpcode::G_VECREDUCE_FMAX:                                         \
  case TargetOpcode::G_VECREDUCE_FMIN:                                         \
  case TargetOpcode::G_VECREDUCE_FMAXIMUM:                                     \
  case TargetOpcode::G_VECREDUCE_FMINIMUM:                                     \
  case TargetOpcode::G_VECREDUCE_FMAXIMUMNUM:                                  \
  case TargetOpcode::G_VECREDUCE_FMINIMUMNUM:                                  \
  case TargetOpcode::G_VECREDUCE_ADD:                                          \
  case TargetOpcode::G_VECREDUCE_MUL:                                          \
  case TargetOpcode::G_VECREDUCE_AND:                                          \
  case TargetOpcode::G_VECREDUCE_OR:                                           \
  case TargetOpcode::G_VECREDUCE_XOR:                                          \
  case TargetOpcode::G_VECREDUCE_SMAX:                                         \
  case TargetOpcode::G_VECREDUCE_SMIN:                                         \
  case TargetOpcode::G_VECREDUCE_UMAX:                                         \
  case TargetOpcode::G_VECREDUCE_UMIN:

/// Try to constrain Reg to the specified register class.
///
/// If this fails, create a new virtual register in the correct class.
///
/// \return The virtual register constrained to the right register class.
/// \param MRI Register information for the function.
/// \param TII Target instruction info.
/// \param RBI Register bank info.
/// \param Reg Virtual register to constrain.
/// \param RegClass Target register class to constrain to.
LLVM_ABI Register constrainRegToClass(MachineRegisterInfo &MRI,
                                      const TargetInstrInfo &TII,
                                      const RegisterBankInfo &RBI, Register Reg,
                                      const TargetRegisterClass &RegClass);

/// Constrain a register operand to a given target register class.
///
/// Constrain the Register operand OpIdx so that it is now constrained to the
/// TargetRegisterClass passed as an argument (RegClass). If this fails, create
/// a new virtual register in the correct class and insert a COPY before \p
/// InsertPt if it is a use or after if it is a definition. In both cases, the
/// function also updates the register of RegMo. The debug location of \p
/// InsertPt is used for the new copy.
///
/// \return The virtual register constrained to the right register class.
/// \param MF Machine function being transformed.
/// \param TRI Target register info.
/// \param MRI Register information for the function.
/// \param TII Target instruction info.
/// \param RBI Register bank info.
/// \param InsertPt Instruction used as the copy insertion point.
/// \param RegClass Target register class to constrain to.
/// \param RegMO Register operand to constrain and possibly rewrite.
LLVM_ABI Register constrainOperandRegClass(
    const MachineFunction &MF, const TargetRegisterInfo &TRI,
    MachineRegisterInfo &MRI, const TargetInstrInfo &TII,
    const RegisterBankInfo &RBI, MachineInstr &InsertPt,
    const TargetRegisterClass &RegClass, MachineOperand &RegMO);

/// Constrain a register operand for a given instruction description.
///
/// Try to constrain Reg so that it is usable by argument OpIdx of the provided
/// MCInstrDesc \p II. If this fails, create a new virtual register in the
/// correct class and insert a COPY before \p InsertPt if it is a use or after
/// if it is a definition. In both cases, the function also updates the register
/// of RegMo. This is equivalent to constrainOperandRegClass(..., RegClass, ...)
/// with RegClass obtained from the MCInstrDesc. The debug location of \p
/// InsertPt is used for the new copy.
///
/// \return The virtual register constrained to the right register class.
/// \param MF Machine function being transformed.
/// \param TRI Target register info.
/// \param MRI Register information for the function.
/// \param TII Target instruction info.
/// \param RBI Register bank info.
/// \param InsertPt Instruction used as the copy insertion point.
/// \param II Machine instruction description providing the operand class.
/// \param RegMO Register operand to constrain and possibly rewrite.
/// \param OpIdx Operand index in \p II that determines the register class.
LLVM_ABI Register constrainOperandRegClass(
    const MachineFunction &MF, const TargetRegisterInfo &TRI,
    MachineRegisterInfo &MRI, const TargetInstrInfo &TII,
    const RegisterBankInfo &RBI, MachineInstr &InsertPt, const MCInstrDesc &II,
    MachineOperand &RegMO, unsigned OpIdx);

/// Constrain selected-instruction operands to the instruction register classes.
///
/// Mutate the newly-selected instruction \p I to constrain its (possibly
/// generic) virtual register operands to the instruction's register class.
/// This could involve inserting COPYs before (for uses) or after (for defs).
/// This requires the number of operands to match the instruction description.
/// \param I Newly selected instruction whose operands are constrained.
/// \param TII Target instruction info.
/// \param TRI Target register info.
/// \param RBI Register bank info.
// FIXME: Not all instructions have the same number of operands. We should
// probably expose a constrain helper per operand and let the target selector
// constrain individual registers, like fast-isel.
LLVM_ABI void constrainSelectedInstRegOperands(MachineInstr &I,
                                               const TargetInstrInfo &TII,
                                               const TargetRegisterInfo &TRI,
                                               const RegisterBankInfo &RBI);

/// Check if DstReg can be replaced with SrcReg depending on the register
/// constraints.
/// \param DstReg Destination register that would be replaced.
/// \param SrcReg Source register proposed as the replacement.
/// \param MRI Register information for the function.
/// \return True if \p DstReg can be replaced with \p SrcReg.
LLVM_ABI bool canReplaceReg(Register DstReg, Register SrcReg,
                            MachineRegisterInfo &MRI);

/// Check whether an instruction \p MI is dead: it only defines dead virtual
/// registers, and doesn't have other side effects.
/// \param MI Instruction to test for trivial deadness.
/// \param MRI Register information for the function.
/// \return True if \p MI is trivially dead.
LLVM_ABI bool isTriviallyDead(const MachineInstr &MI,
                              const MachineRegisterInfo &MRI);

/// Report an ISel error as a missed optimization remark to the LLVMContext's
/// diagnostic stream.  Set the FailedISel MachineFunction property.
/// \param MF Machine function for which selection failed.
/// \param MORE Remark emitter used to report the failure.
/// \param R Missed-optimization remark describing the failure.
LLVM_ABI void reportGISelFailure(MachineFunction &MF,
                                 MachineOptimizationRemarkEmitter &MORE,
                                 MachineOptimizationRemarkMissed &R);

/// Report an ISel failure for a specific instruction with a custom message.
///
/// Sets the FailedISel MachineFunction property.
/// \param MF Machine function for which selection failed.
/// \param MORE Remark emitter used to report the failure.
/// \param PassName Name of the pass reporting the failure.
/// \param Msg Failure message to include in the remark.
/// \param MI Instruction associated with the failure.
LLVM_ABI void reportGISelFailure(MachineFunction &MF,
                                 MachineOptimizationRemarkEmitter &MORE,
                                 const char *PassName, StringRef Msg,
                                 const MachineInstr &MI);

/// Report an ISel warning as a missed optimization remark to the LLVMContext's
/// diagnostic stream.
/// \param MF Machine function associated with the warning.
/// \param MORE Remark emitter used to report the warning.
/// \param R Missed-optimization remark describing the warning.
LLVM_ABI void reportGISelWarning(MachineFunction &MF,
                                 MachineOptimizationRemarkEmitter &MORE,
                                 MachineOptimizationRemarkMissed &R);

/// Returns the inverse opcode of \p MinMaxOpc, which is a generic min/max
/// opcode like G_SMIN.
/// \param MinMaxOpc Generic min/max opcode whose inverse is requested.
/// \return The inverse generic min/max opcode.
LLVM_ABI unsigned getInverseGMinMaxOpcode(unsigned MinMaxOpc);

/// If \p VReg is defined by a G_CONSTANT, return the corresponding value.
/// \param VReg Virtual register that may be defined by a G_CONSTANT.
/// \param MRI Register information for the function.
/// \return The constant value, or std::nullopt if \p VReg is not a G_CONSTANT.
LLVM_ABI std::optional<APInt>
getIConstantVRegVal(Register VReg, const MachineRegisterInfo &MRI);

/// If \p VReg is defined by a G_CONSTANT fits in int64_t returns it.
/// \param VReg Virtual register that may be defined by a G_CONSTANT.
/// \param MRI Register information for the function.
/// \return The sign-extended constant as int64_t, or std::nullopt.
LLVM_ABI std::optional<int64_t>
getIConstantVRegSExtVal(Register VReg, const MachineRegisterInfo &MRI);

/// \p VReg is defined by a G_CONSTANT, return the corresponding value.
/// \param VReg Virtual register defined by a G_CONSTANT.
/// \param MRI Register information for the function.
/// \return The constant APInt defining \p VReg.
LLVM_ABI const APInt &getIConstantFromReg(Register VReg,
                                          const MachineRegisterInfo &MRI);

/// Simple struct used to hold a constant integer value and a virtual
/// register.
struct ValueAndVReg {
  /// Constant integer value.
  APInt Value;
  /// Virtual register that defines or carries the constant.
  Register VReg;
};

/// If \p VReg is defined by a statically evaluable chain of instructions rooted
/// on a G_CONSTANT returns its APInt value and def register.
/// \param VReg Virtual register to look through for a constant.
/// \param MRI Register information for the function.
/// \param LookThroughInstrs Whether to fold through trivial defining ops.
/// \return The constant value and def register, or std::nullopt.
LLVM_ABI std::optional<ValueAndVReg>
getIConstantVRegValWithLookThrough(Register VReg,
                                   const MachineRegisterInfo &MRI,
                                   bool LookThroughInstrs = true);

/// If \p VReg is defined by a statically evaluable chain of instructions rooted
/// on a G_CONSTANT or G_FCONSTANT returns its value as APInt and def register.
/// \param VReg Virtual register to look through for a constant.
/// \param MRI Register information for the function.
/// \param LookThroughInstrs Whether to fold through trivial defining ops.
/// \param LookThroughAnyExt Whether to look through any-extends as well.
/// \return The constant value and def register, or std::nullopt.
LLVM_ABI std::optional<ValueAndVReg> getAnyConstantVRegValWithLookThrough(
    Register VReg, const MachineRegisterInfo &MRI,
    bool LookThroughInstrs = true, bool LookThroughAnyExt = false);

/// Lowering info for a memcpy-family intrinsic: dst, src, length, align, and
/// whether dst alignment can change, plus per-chunk load/store types.
using MemCpyFamilyLoweringInfo =
    std::tuple<Register, Register, uint64_t, Align, bool, std::vector<LLT>>;

/// Matcher for memcpy-like instructions. For non-zero lengths, \p MemOps
/// contains the load/store types to emit.
/// \param MI Memcpy-family instruction to match.
/// \param MRI Register information for the function.
/// \param MaxLen Maximum length for which lowering is attempted.
/// \param Dst Set to the destination pointer register on success.
/// \param Src Set to the source pointer register on success.
/// \param KnownLen Set to the known copy length on success.
/// \param Alignment Set to the inferred access alignment on success.
/// \param DstAlignCanChange Set when destination alignment may change.
/// \param MemOps Filled with load/store types to emit for the copy.
/// \return True if the memcpy-family intrinsic can be lowered.
LLVM_ABI bool canLowerMemCpyFamily(const MachineInstr &MI,
                                   const MachineRegisterInfo &MRI,
                                   unsigned MaxLen, Register &Dst,
                                   Register &Src, uint64_t &KnownLen,
                                   Align &Alignment, bool &DstAlignCanChange,
                                   std::vector<LLT> &MemOps);

/// Simple struct used to hold a constant floating-point value and a virtual
/// register.
struct FPValueAndVReg {
  /// Constant floating-point value.
  APFloat Value;
  /// Virtual register that defines or carries the constant.
  Register VReg;
};

/// If \p VReg is defined by a statically evaluable chain of instructions rooted
/// on a G_FCONSTANT returns its APFloat value and def register.
/// \param VReg Virtual register to look through for a float constant.
/// \param MRI Register information for the function.
/// \param LookThroughInstrs Whether to fold through trivial defining ops.
/// \return The float constant and def register, or std::nullopt.
LLVM_ABI std::optional<FPValueAndVReg>
getFConstantVRegValWithLookThrough(Register VReg,
                                   const MachineRegisterInfo &MRI,
                                   bool LookThroughInstrs = true);

/// Return the ConstantFP defining \p VReg, or nullptr if it is not a
/// G_FCONSTANT.
/// \param VReg Virtual register that may be defined by a G_FCONSTANT.
/// \param MRI Register information for the function.
/// \return The ConstantFP, or nullptr if \p VReg is not a G_FCONSTANT.
LLVM_ABI const ConstantFP *getConstantFPVRegVal(Register VReg,
                                                const MachineRegisterInfo &MRI);

/// See if Reg is defined by an single def instruction that is
/// Opcode. Also try to do trivial folding if it's a COPY with
/// same types. Returns null otherwise.
/// \param Opcode Opcode that the defining instruction must match.
/// \param Reg Virtual register whose defining instruction is inspected.
/// \param MRI Register information for the function.
/// \return The defining instruction matching \p Opcode, or nullptr.
LLVM_ABI MachineInstr *getOpcodeDef(unsigned Opcode, Register Reg,
                                    const MachineRegisterInfo &MRI);

/// Simple struct used to hold a Register value and the instruction which
/// defines it.
struct DefinitionAndSourceRegister {
  /// Instruction that defines the underlying value.
  MachineInstr *MI;
  /// Underlying source register after folding copies.
  Register Reg;
};

/// Find the def instruction for \p Reg, and underlying value Register folding
/// away any copies.
///
/// Also walks through hints such as G_ASSERT_ZEXT.
/// \param Reg Virtual register whose definition is sought.
/// \param MRI Register information for the function.
/// \return The def instruction and source register, or std::nullopt.
LLVM_ABI std::optional<DefinitionAndSourceRegister>
getDefSrcRegIgnoringCopies(Register Reg, const MachineRegisterInfo &MRI);

/// Find the def instruction for \p Reg, folding away any trivial copies. May
/// return nullptr if \p Reg is not a generic virtual register.
///
/// Also walks through hints such as G_ASSERT_ZEXT.
/// \param Reg Virtual register whose definition is sought.
/// \param MRI Register information for the function.
/// \return The def instruction, or nullptr.
LLVM_ABI MachineInstr *getDefIgnoringCopies(Register Reg,
                                            const MachineRegisterInfo &MRI);

/// Find the source register for \p Reg, folding away any trivial copies.
///
/// It will be an output register of the instruction that getDefIgnoringCopies
/// returns. May return an invalid register if \p Reg is not a generic virtual
/// register.
///
/// Also walks through hints such as G_ASSERT_ZEXT.
/// \param Reg Virtual register whose source is sought.
/// \param MRI Register information for the function.
/// \return The underlying source register, or an invalid register.
LLVM_ABI Register getSrcRegIgnoringCopies(Register Reg,
                                          const MachineRegisterInfo &MRI);

/// Split a wide generic register into bitwise blocks of type \p Ty.
///
/// Helper function to split a wide generic register into bitwise blocks with
/// the given Type (which implies the number of blocks needed). The generic
/// registers created are appended to Ops, starting at bit 0 of Reg.
/// \param Reg Wide register to split.
/// \param Ty Type of each extracted part.
/// \param NumParts Number of parts to extract.
/// \param VRegs Receives the registers for each extracted part.
/// \param MIRBuilder Builder used to emit extract operations.
/// \param MRI Register information for the function.
LLVM_ABI void extractParts(Register Reg, LLT Ty, int NumParts,
                           SmallVectorImpl<Register> &VRegs,
                           MachineIRBuilder &MIRBuilder,
                           MachineRegisterInfo &MRI);

/// Version which handles irregular splits.
/// \param Reg Wide register to split.
/// \param RegTy Type of \p Reg.
/// \param MainTy Type used for the evenly sized main parts.
/// \param LeftoverTy Set to the type of any leftover part.
/// \param VRegs Receives the registers for the main parts.
/// \param LeftoverVRegs Receives the registers for leftover parts.
/// \param MIRBuilder Builder used to emit extract operations.
/// \param MRI Register information for the function.
/// \return True if the irregular split succeeded.
LLVM_ABI bool extractParts(Register Reg, LLT RegTy, LLT MainTy, LLT &LeftoverTy,
                           SmallVectorImpl<Register> &VRegs,
                           SmallVectorImpl<Register> &LeftoverVRegs,
                           MachineIRBuilder &MIRBuilder,
                           MachineRegisterInfo &MRI);

/// Version which handles irregular sub-vector splits.
/// \param Reg Vector register to split.
/// \param NumElts Number of elements in each extracted subvector.
/// \param VRegs Receives the registers for each subvector part.
/// \param MIRBuilder Builder used to emit extract operations.
/// \param MRI Register information for the function.
LLVM_ABI void extractVectorParts(Register Reg, unsigned NumElts,
                                 SmallVectorImpl<Register> &VRegs,
                                 MachineIRBuilder &MIRBuilder,
                                 MachineRegisterInfo &MRI);

// Templated variant of getOpcodeDef returning a MachineInstr derived T.
/// See if Reg is defined by an single def instruction of type T
/// Also try to do trivial folding if it's a COPY with
/// same types. Returns null otherwise.
/// \param Reg Virtual register whose defining instruction is inspected.
/// \param MRI Register information for the function.
/// \return The defining instruction cast to \p T, or nullptr.
template <class T>
T *getOpcodeDef(Register Reg, const MachineRegisterInfo &MRI) {
  MachineInstr *DefMI = getDefIgnoringCopies(Reg, MRI);
  return dyn_cast_or_null<T>(DefMI);
}

/// Modify analysis usage so it preserves passes required for the SelectionDAG
/// fallback.
/// \param AU Analysis usage to update for SelectionDAG fallback.
LLVM_ABI void getSelectionDAGFallbackAnalysisUsage(AnalysisUsage &AU);

/// Constant-fold a generic integer binary opcode on two register operands.
/// \param Opcode Generic integer binary opcode to fold.
/// \param Op1 First operand register.
/// \param Op2 Second operand register.
/// \param MRI Register information for the function.
/// \return The folded APInt, or std::nullopt on failure.
LLVM_ABI std::optional<APInt> ConstantFoldBinOp(unsigned Opcode,
                                                const Register Op1,
                                                const Register Op2,
                                                const MachineRegisterInfo &MRI);
/// Constant-fold a generic floating-point binary opcode on two registers.
/// \param Opcode Generic floating-point binary opcode to fold.
/// \param Op1 First operand register.
/// \param Op2 Second operand register.
/// \param MRI Register information for the function.
/// \return The folded APFloat, or std::nullopt on failure.
LLVM_ABI std::optional<APFloat>
ConstantFoldFPBinOp(unsigned Opcode, const Register Op1, const Register Op2,
                    const MachineRegisterInfo &MRI);

/// Tries to constant fold a vector binop with sources \p Op1 and \p Op2.
/// Returns an empty vector on failure.
/// \param Opcode Generic vector binary opcode to fold.
/// \param Op1 First operand register.
/// \param Op2 Second operand register.
/// \param MRI Register information for the function.
/// \return The folded element values, or an empty vector on failure.
LLVM_ABI SmallVector<APInt>
ConstantFoldVectorBinop(unsigned Opcode, const Register Op1, const Register Op2,
                        const MachineRegisterInfo &MRI);

/// Constant-fold a generic cast opcode producing an integer result.
/// \param Opcode Generic cast opcode to fold.
/// \param DstTy Destination type of the cast.
/// \param Op0 Source operand register.
/// \param MRI Register information for the function.
/// \return The folded APInt, or std::nullopt on failure.
LLVM_ABI std::optional<APInt>
ConstantFoldCastOp(unsigned Opcode, LLT DstTy, const Register Op0,
                   const MachineRegisterInfo &MRI);

/// Constant-fold a generic extend opcode with an immediate second operand.
/// \param Opcode Generic extend opcode to fold.
/// \param Op1 Source operand register.
/// \param Imm Immediate second operand.
/// \param MRI Register information for the function.
/// \return The folded APInt, or std::nullopt on failure.
LLVM_ABI std::optional<APInt> ConstantFoldExtOp(unsigned Opcode,
                                                const Register Op1,
                                                uint64_t Imm,
                                                const MachineRegisterInfo &MRI);

/// Constant-fold an integer-to-float conversion opcode.
/// \param Opcode Integer-to-float conversion opcode to fold.
/// \param DstTy Destination floating-point type.
/// \param Src Source integer register.
/// \param MRI Register information for the function.
/// \return The folded APFloat, or std::nullopt on failure.
LLVM_ABI std::optional<APFloat>
ConstantFoldIntToFloat(unsigned Opcode, LLT DstTy, Register Src,
                       const MachineRegisterInfo &MRI);

/// Constant-fold a unary integer operation on \p Src.
///
/// Tries to constant fold a unary integer operation (G_CTLZ, G_CTTZ, G_CTPOP
/// and their _ZERO_POISON variants, G_ABS, G_BSWAP, G_BITREVERSE) on \p Src.
/// If \p Src is a vector then it tries to do an element-wise constant fold.
/// \param Opcode Unary integer opcode to fold.
/// \param DstTy Destination type of the operation.
/// \param Src Source operand register.
/// \param MRI Register information for the function.
/// \return The folded element values.
LLVM_ABI SmallVector<APInt>
ConstantFoldUnaryIntOp(unsigned Opcode, LLT DstTy, Register Src,
                       const MachineRegisterInfo &MRI);

/// Constant-fold an integer compare of two register operands.
/// \param Pred Integer compare predicate.
/// \param Op1 First operand register.
/// \param Op2 Second operand register.
/// \param DstScalarSizeInBits Scalar bit width of the compare result.
/// \param ExtOp Extend opcode applied to the operands, if any.
/// \param MRI Register information for the function.
/// \return The compare result vector, or std::nullopt on failure.
LLVM_ABI std::optional<SmallVector<APInt>>
ConstantFoldICmp(unsigned Pred, const Register Op1, const Register Op2,
                 unsigned DstScalarSizeInBits, unsigned ExtOp,
                 const MachineRegisterInfo &MRI);

/// Test if the given value is known to have exactly one bit set.
///
/// This differs from computeKnownBits in that it doesn't necessarily determine
/// which bit is set. When \p OrNegative is true, the value is also considered a
/// power of two if its negation is a power of two (i.e. its absolute value is a
/// power of two).
/// \param Val Register whose value is tested.
/// \param MRI Register information for the function.
/// \param ValueTracking Optional value-tracking analysis to consult.
/// \param OrNegative Also accept values whose negation is a power of two.
/// \return True if \p Val is known to be a power of two.
LLVM_ABI bool
isKnownToBeAPowerOfTwo(Register Val, const MachineRegisterInfo &MRI,
                       GISelValueTracking *ValueTracking = nullptr,
                       bool OrNegative = false);

/// Infer an alignment from machine pointer info for a memory access.
/// \param MF Machine function providing data layout context.
/// \param MPO Pointer info describing the memory location.
/// \return The inferred alignment.
LLVM_ABI Align inferAlignFromPtrInfo(MachineFunction &MF,
                                     const MachinePointerInfo &MPO);

/// Return a virtual register for an incoming physical argument register.
///
/// Return a virtual register corresponding to the incoming argument register \p
/// PhysReg. This register is expected to have class \p RC, and optional type \p
/// RegTy. This assumes all references to the register will use the same type.
///
/// If there is an existing live-in argument register, it will be returned.
/// This will also ensure there is a valid copy.
/// \param MF Machine function receiving the live-in.
/// \param TII Target instruction info used to build copies.
/// \param PhysReg Incoming physical argument register.
/// \param RC Register class expected for the virtual register.
/// \param DL Debug location for any inserted copy.
/// \param RegTy Optional LLT assigned to the virtual register.
/// \return The virtual register for the live-in physical register.
LLVM_ABI Register getFunctionLiveInPhysReg(
    MachineFunction &MF, const TargetInstrInfo &TII, MCRegister PhysReg,
    const TargetRegisterClass &RC, const DebugLoc &DL, LLT RegTy = LLT());

/// Return the least common multiple type of \p OrigTy and \p TargetTy.
///
/// Changes the number of vector elements or scalar bitwidth. The intent is a
/// G_MERGE_VALUES, G_BUILD_VECTOR, or G_CONCAT_VECTORS can be constructed from
/// \p OrigTy elements, and unmerged into \p TargetTy. It is an error to call
/// this function where one argument is a fixed vector and the other is a
/// scalable vector, since it is illegal to build a G_{MERGE|UNMERGE}_VALUES
/// between fixed and scalable vectors.
/// \param OrigTy Original type being widened or reshaped.
/// \param TargetTy Target type that must be covered by the LCM type.
/// \return The least common multiple type of \p OrigTy and \p TargetTy.
LLVM_ABI LLVM_READNONE LLT getLCMType(LLT OrigTy, LLT TargetTy);

LLVM_ABI LLVM_READNONE
    /// Return the smallest type that covers both types and is a multiple of
    /// TargetTy.
    /// \param OrigTy Original type being covered.
    /// \param TargetTy Target type that the result must be a multiple of.
    /// \return The smallest covering type that is a multiple of \p TargetTy.
    LLT
    getCoverTy(LLT OrigTy, LLT TargetTy);

/// Return a type whose total size is the GCD of \p OrigTy and \p TargetTy.
///
/// This will try to either change the number of vector elements, or bitwidth of
/// scalars. The intent is the result type can be used as the result of a
/// G_UNMERGE_VALUES from \p OrigTy, and then some combination of
/// G_MERGE_VALUES, G_BUILD_VECTOR and G_CONCAT_VECTORS (possibly with
/// intermediate casts) can re-form \p TargetTy.
///
/// If these are vectors with different element types, this will try to produce
/// a vector with a compatible total size, but the element type of \p OrigTy. If
/// this can't be satisfied, this will produce a scalar smaller than the
/// original vector elements. It is an error to call this function where
/// one argument is a fixed vector and the other is a scalable vector, since it
/// is illegal to build a G_{MERGE|UNMERGE}_VALUES between fixed and scalable
/// vectors.
///
/// In the worst case, this returns LLT::scalar(1).
/// \param OrigTy Original type being decomposed.
/// \param TargetTy Target type that must be rebuildable from GCD parts.
/// \return The GCD type of \p OrigTy and \p TargetTy.
LLVM_ABI LLVM_READNONE LLT getGCDType(LLT OrigTy, LLT TargetTy);

/// Represents a value which can be a Register or a constant.
///
/// This is useful in situations where an instruction may have an interesting
/// register operand or interesting constant operand. For a concrete example,
/// \see getVectorSplat.
class RegOrConstant {
  int64_t Cst;
  Register Reg;
  bool IsReg;

public:
  /// Construct a RegOrConstant that holds a register.
  /// \param Reg Register value to store.
  explicit RegOrConstant(Register Reg) : Reg(Reg), IsReg(true) {}
  /// Construct a RegOrConstant that holds an integer constant.
  /// \param Cst Integer constant value to store.
  explicit RegOrConstant(int64_t Cst) : Cst(Cst), IsReg(false) {}
  /// Return true if this holds a register.
  /// \return True if this holds a register.
  bool isReg() const { return IsReg; }
  /// Return true if this holds a constant.
  /// \return True if this holds a constant.
  bool isCst() const { return !IsReg; }
  /// Return the held register; asserts if this holds a constant.
  /// \return The held register.
  Register getReg() const {
    assert(isReg() && "Expected a register!");
    return Reg;
  }
  /// Return the held constant; asserts if this holds a register.
  /// \return The held constant.
  int64_t getCst() const {
    assert(isCst() && "Expected a constant!");
    return Cst;
  }
};

/// Return the splat index of a G_SHUFFLE_VECTOR when it is a splat.
///
/// \returns The splat index when \p MI is a splat, or std::nullopt otherwise.
/// \param MI Shuffle instruction to inspect.
LLVM_ABI std::optional<int> getSplatIndex(MachineInstr &MI);

/// Return the scalar integral splat value of \p Reg if possible.
/// \param Reg Virtual register that may define a splat constant.
/// \param MRI Register information for the function.
/// \return The splat APInt, or std::nullopt.
LLVM_ABI std::optional<APInt>
getIConstantSplatVal(const Register Reg, const MachineRegisterInfo &MRI);

/// Return the scalar integral splat value defined by \p MI if possible.
/// \param MI Instruction that may define a splat constant.
/// \param MRI Register information for the function.
/// \return The splat APInt, or std::nullopt.
LLVM_ABI std::optional<APInt>
getIConstantSplatVal(const MachineInstr &MI, const MachineRegisterInfo &MRI);

/// Return the scalar sign-extended integral splat value of \p Reg if possible.
/// \param Reg Virtual register that may define a splat constant.
/// \param MRI Register information for the function.
/// \return The sign-extended splat value, or std::nullopt.
LLVM_ABI std::optional<int64_t>
getIConstantSplatSExtVal(const Register Reg, const MachineRegisterInfo &MRI);

/// Return the scalar sign-extended integral splat value defined by \p MI if
/// possible.
/// \param MI Instruction that may define a splat constant.
/// \param MRI Register information for the function.
/// \return The sign-extended splat value, or std::nullopt.
LLVM_ABI std::optional<int64_t>
getIConstantSplatSExtVal(const MachineInstr &MI,
                         const MachineRegisterInfo &MRI);

/// Returns a floating point scalar constant of a build vector splat if it
/// exists. When \p AllowUndef == true some elements can be undef but not all.
/// \param VReg Virtual register that may define a float splat.
/// \param MRI Register information for the function.
/// \param AllowUndef Whether some undef elements are permitted.
/// \return The float splat value and register, or std::nullopt.
LLVM_ABI std::optional<FPValueAndVReg>
getFConstantSplat(Register VReg, const MachineRegisterInfo &MRI,
                  bool AllowUndef = true);

/// Return true if the specified register is defined by G_BUILD_VECTOR or
/// G_BUILD_VECTOR_TRUNC where all of the elements are \p SplatValue or undef.
/// \param Reg Virtual register defined by a build vector.
/// \param MRI Register information for the function.
/// \param SplatValue Expected splat integer value.
/// \param AllowUndef Whether undef elements are permitted.
/// \return True if all elements are \p SplatValue or undef.
LLVM_ABI bool isBuildVectorConstantSplat(const Register Reg,
                                         const MachineRegisterInfo &MRI,
                                         int64_t SplatValue, bool AllowUndef);

/// Return true if the specified register is defined by G_BUILD_VECTOR or
/// G_BUILD_VECTOR_TRUNC where all of the elements are \p SplatValue or undef.
/// \param Reg Virtual register defined by a build vector.
/// \param MRI Register information for the function.
/// \param SplatValue Expected splat APInt value.
/// \param AllowUndef Whether undef elements are permitted.
/// \return True if all elements are \p SplatValue or undef.
LLVM_ABI bool isBuildVectorConstantSplat(const Register Reg,
                                         const MachineRegisterInfo &MRI,
                                         const APInt &SplatValue,
                                         bool AllowUndef);

/// Return true if the specified instruction is a G_BUILD_VECTOR or
/// G_BUILD_VECTOR_TRUNC where all of the elements are \p SplatValue or undef.
/// \param MI Build-vector instruction to inspect.
/// \param MRI Register information for the function.
/// \param SplatValue Expected splat integer value.
/// \param AllowUndef Whether undef elements are permitted.
/// \return True if all elements are \p SplatValue or undef.
LLVM_ABI bool isBuildVectorConstantSplat(const MachineInstr &MI,
                                         const MachineRegisterInfo &MRI,
                                         int64_t SplatValue, bool AllowUndef);

/// Return true if the specified instruction is a G_BUILD_VECTOR or
/// G_BUILD_VECTOR_TRUNC where all of the elements are \p SplatValue or undef.
/// \param MI Build-vector instruction to inspect.
/// \param MRI Register information for the function.
/// \param SplatValue Expected splat APInt value.
/// \param AllowUndef Whether undef elements are permitted.
/// \return True if all elements are \p SplatValue or undef.
LLVM_ABI bool isBuildVectorConstantSplat(const MachineInstr &MI,
                                         const MachineRegisterInfo &MRI,
                                         const APInt &SplatValue,
                                         bool AllowUndef);

/// Return true if the specified instruction is a G_BUILD_VECTOR or
/// G_BUILD_VECTOR_TRUNC where all of the elements are 0 or undef.
/// \param MI Build-vector instruction to inspect.
/// \param MRI Register information for the function.
/// \param AllowUndef Whether undef elements are permitted.
/// \return True if all elements are zero or undef.
LLVM_ABI bool isBuildVectorAllZeros(const MachineInstr &MI,
                                    const MachineRegisterInfo &MRI,
                                    bool AllowUndef = false);

/// Return true if the specified instruction is a G_BUILD_VECTOR or
/// G_BUILD_VECTOR_TRUNC where all of the elements are ~0 or undef.
/// \param MI Build-vector instruction to inspect.
/// \param MRI Register information for the function.
/// \param AllowUndef Whether undef elements are permitted.
/// \return True if all elements are all-ones or undef.
LLVM_ABI bool isBuildVectorAllOnes(const MachineInstr &MI,
                                   const MachineRegisterInfo &MRI,
                                   bool AllowUndef = false);

/// Return true if the specified instruction is known to be a constant, or a
/// vector of constants.
///
/// If \p AllowFP is true, this will consider G_FCONSTANT in addition to
/// G_CONSTANT. If \p AllowOpaqueConstants is true, constant-like instructions
/// such as G_GLOBAL_VALUE will also be considered.
/// \param MI Instruction to inspect.
/// \param MRI Register information for the function.
/// \param AllowFP Whether floating-point constants are accepted.
/// \param AllowOpaqueConstants Whether opaque constants like globals count.
/// \return True if \p MI is a constant or vector of constants.
LLVM_ABI bool isConstantOrConstantVector(const MachineInstr &MI,
                                         const MachineRegisterInfo &MRI,
                                         bool AllowFP = true,
                                         bool AllowOpaqueConstants = true);

/// Return true if the value is a constant 0 integer or a null splat.
///
/// Accepts a splatted vector of a constant 0 integer (with no undefs if \p
/// AllowUndefs is false). This will handle G_BUILD_VECTOR and
/// G_BUILD_VECTOR_TRUNC as truncation is not an issue for null values.
/// \param MI Instruction defining the value to test.
/// \param MRI Register information for the function.
/// \param AllowUndefs Whether undef elements are permitted in a splat.
/// \return True if the value is a null constant or null splat.
LLVM_ABI bool isNullOrNullSplat(const MachineInstr &MI,
                                const MachineRegisterInfo &MRI,
                                bool AllowUndefs = false);

/// Return true if the value is a constant -1 integer or a splatted vector of a
/// constant -1 integer (with no undefs if \p AllowUndefs is false).
/// \param MI Instruction defining the value to test.
/// \param MRI Register information for the function.
/// \param AllowUndefs Whether undef elements are permitted in a splat.
/// \return True if the value is an all-ones constant or splat.
LLVM_ABI bool isAllOnesOrAllOnesSplat(const MachineInstr &MI,
                                      const MachineRegisterInfo &MRI,
                                      bool AllowUndefs = false);

/// \returns a value when \p MI is a vector splat. The splat can be either a
/// Register or a constant.
///
/// Examples:
///
/// \code
///   %reg = COPY $physreg
///   %reg_splat = G_BUILD_VECTOR %reg, %reg, ..., %reg
/// \endcode
///
/// If called on the G_BUILD_VECTOR above, this will return a RegOrConstant
/// containing %reg.
///
/// \code
///   %cst = G_CONSTANT iN 4
///   %constant_splat = G_BUILD_VECTOR %cst, %cst, ..., %cst
/// \endcode
///
/// In the above case, this will return a RegOrConstant containing 4.
/// \param MI Build-vector or similar instruction that may be a splat.
/// \param MRI Register information for the function.
LLVM_ABI std::optional<RegOrConstant>
getVectorSplat(const MachineInstr &MI, const MachineRegisterInfo &MRI);

/// Determines if \p MI defines a constant integer or a build vector of
/// constant integers. Treats undef values as constants.
/// \param MI Instruction to inspect.
/// \param MRI Register information for the function.
/// \return True if \p MI defines a constant or build vector of constants.
LLVM_ABI bool isConstantOrConstantVector(MachineInstr &MI,
                                         const MachineRegisterInfo &MRI);

/// Determines if \p Def defines a constant integer or a splat vector of
/// constant integers.
/// \returns the scalar constant or std::nullopt.
/// \param Def Virtual register that may define a constant or splat.
/// \param MRI Register information for the function.
LLVM_ABI std::optional<APInt>
isConstantOrConstantSplatVector(Register Def, const MachineRegisterInfo &MRI);

/// Determines if \p Def defines a float constant integer or a splat vector of
/// float constant integers.
/// \returns the float constant or std::nullopt.
/// \param Def Virtual register that may define a float constant or splat.
/// \param MRI Register information for the function.
LLVM_ABI std::optional<APFloat>
isConstantOrConstantSplatVectorFP(Register Def, const MachineRegisterInfo &MRI);

/// Match a unary predicate against a scalar/splat constant or build vector.
///
/// Attempt to match a unary predicate against a scalar/splat constant or every
/// element of a constant G_BUILD_VECTOR. If \p ConstVal is null, the source
/// value was undef.
/// \param MRI Register information for the function.
/// \param Reg Virtual register whose constant definition is matched.
/// \param Match Predicate invoked for each constant element (null if undef).
/// \param AllowUndefs Whether undef elements are permitted.
/// \return True if the predicate matches all elements.
LLVM_ABI bool
matchUnaryPredicate(const MachineRegisterInfo &MRI, Register Reg,
                    std::function<bool(const Constant *ConstVal)> Match,
                    bool AllowUndefs = false);

/// Returns true if given the TargetLowering's boolean contents information,
/// the value \p Val contains a true value.
/// \param TLI Target lowering used for boolean contents.
/// \param Val Integer value to interpret as a boolean.
/// \param IsVector Whether the compare result is a vector.
/// \param IsFP Whether the compare is floating-point.
/// \return True if \p Val represents true for the given boolean contents.
LLVM_ABI bool isConstTrueVal(const TargetLowering &TLI, int64_t Val,
                             bool IsVector, bool IsFP);
/// Returns true if given the TargetLowering's boolean contents information,
/// the value \p Val contains a false value.
/// \param TLI Target lowering used for boolean contents.
/// \param Val Integer value to interpret as a boolean.
/// \param IsVector Whether the compare result is a vector.
/// \param IsFP Whether the compare is floating-point.
/// \return True if \p Val represents false for the given boolean contents.
LLVM_ABI bool isConstFalseVal(const TargetLowering &TLI, int64_t Val,
                              bool IsVector, bool IsFP);

/// Returns an integer representing true, as defined by the
/// TargetBooleanContents.
/// \param TLI Target lowering used for boolean contents.
/// \param IsVector Whether the compare result is a vector.
/// \param IsFP Whether the compare is floating-point.
/// \return The integer encoding of true for icmp results.
LLVM_ABI int64_t getICmpTrueVal(const TargetLowering &TLI, bool IsVector,
                                bool IsFP);

/// Small work list of machine instructions used while erasing dead chains.
using SmallInstListTy = GISelWorkList<4>;
/// Record uses of \p MI and erase it, appending newly dead users to the chain.
/// \param MI Instruction being erased.
/// \param MRI Register information for the function.
/// \param LocObserver Optional observer for lost debug locations.
/// \param DeadInstChain Work list that receives newly dead instructions.
LLVM_ABI void saveUsesAndErase(MachineInstr &MI, MachineRegisterInfo &MRI,
                               LostDebugLocObserver *LocObserver,
                               SmallInstListTy &DeadInstChain);
/// Erase a list of dead instructions, optionally notifying a debug observer.
/// \param DeadInstrs Instructions to erase.
/// \param MRI Register information for the function.
/// \param LocObserver Optional observer for lost debug locations.
LLVM_ABI void eraseInstrs(ArrayRef<MachineInstr *> DeadInstrs,
                          MachineRegisterInfo &MRI,
                          LostDebugLocObserver *LocObserver = nullptr);
/// Erase a single dead instruction, optionally notifying a debug observer.
/// \param MI Instruction to erase.
/// \param MRI Register information for the function.
/// \param LocObserver Optional observer for lost debug locations.
LLVM_ABI void eraseInstr(MachineInstr &MI, MachineRegisterInfo &MRI,
                         LostDebugLocObserver *LocObserver = nullptr);

/// Assuming the instruction \p MI is going to be deleted, attempt to salvage
/// debug users of \p MI by writing the effect of \p MI in a DIExpression.
/// \param MRI Register information for the function.
/// \param MI Instruction whose debug users may be salvaged.
LLVM_ABI void salvageDebugInfo(const MachineRegisterInfo &MRI,
                               MachineInstr &MI);

/// Returns whether opcode \p Opc is a pre-isel generic floating-point opcode,
/// having only floating-point operands.
/// \param Opc Opcode to classify.
/// \return True if \p Opc is a pre-isel generic floating-point opcode.
LLVM_ABI bool isPreISelGenericFloatingPointOpcode(unsigned Opc);

/// Returns true if \p Reg can create undef or poison from safe operands.
///
/// \p ConsiderFlagsAndMetadata controls whether poison producing flags and
/// metadata on the instruction are considered. This can be used to see if the
/// instruction could still introduce undef or poison even without poison
/// generating flags and metadata which might be on the instruction.
/// \param Reg Virtual register whose defining instruction is tested.
/// \param MRI Register information for the function.
/// \param ConsiderFlagsAndMetadata Whether poison flags/metadata count.
/// \return True if \p Reg can create undef or poison from safe operands.
LLVM_ABI bool canCreateUndefOrPoison(Register Reg,
                                     const MachineRegisterInfo &MRI,
                                     bool ConsiderFlagsAndMetadata = true);

/// Returns true if \p Reg can create poison from non-poison operands.
/// \param Reg Virtual register whose defining instruction is tested.
/// \param MRI Register information for the function.
/// \param ConsiderFlagsAndMetadata Whether poison flags/metadata count.
/// \return True if \p Reg can create poison from non-poison operands.
LLVM_ABI bool canCreatePoison(Register Reg, const MachineRegisterInfo &MRI,
                              bool ConsiderFlagsAndMetadata = true);

/// Returns true if \p Reg cannot be poison and undef.
/// \param Reg Virtual register to analyze.
/// \param MRI Register information for the function.
/// \param Depth Recursion depth limit for the analysis.
/// \return True if \p Reg cannot be undef or poison.
LLVM_ABI bool isGuaranteedNotToBeUndefOrPoison(Register Reg,
                                               const MachineRegisterInfo &MRI,
                                               unsigned Depth = 0);

/// Returns true if \p Reg cannot be poison, but may be undef.
/// \param Reg Virtual register to analyze.
/// \param MRI Register information for the function.
/// \param Depth Recursion depth limit for the analysis.
/// \return True if \p Reg cannot be poison.
LLVM_ABI bool isGuaranteedNotToBePoison(Register Reg,
                                        const MachineRegisterInfo &MRI,
                                        unsigned Depth = 0);

/// Returns true if \p Reg cannot be undef, but may be poison.
/// \param Reg Virtual register to analyze.
/// \param MRI Register information for the function.
/// \param Depth Recursion depth limit for the analysis.
/// \return True if \p Reg cannot be undef.
LLVM_ABI bool isGuaranteedNotToBeUndef(Register Reg,
                                       const MachineRegisterInfo &MRI,
                                       unsigned Depth = 0);

/// Get the type back from LLT. It won't be 100 percent accurate but returns an
/// estimate of the type.
/// \param Ty Low-level type to convert.
/// \param C LLVM context used to build the IR type.
/// \return An estimated IR Type for \p Ty.
LLVM_ABI Type *getTypeForLLT(LLT Ty, LLVMContext &C);

/// Returns true if the instruction \p MI is one of the assert
/// instructions.
/// \param MI Instruction to classify.
/// \return True if \p MI is an assert instruction.
LLVM_ABI bool isAssertMI(const MachineInstr &MI);

/// An integer-like constant.
///
/// It abstracts over scalar, fixed-length vectors, and scalable vectors.
/// In the common case, it provides a common API and feels like an APInt,
/// while still providing low-level access.
/// It can be used for constant-folding.
///
/// bool isZero()
/// abstracts over the kind.
///
/// switch(const.getKind())
/// {
/// }
/// provides low-level access.
class GIConstant {
public:
  /// Shape of the integer-like constant.
  enum class GIConstantKind {
    /// Scalar integer constant.
    Scalar,
    /// Fixed-length vector of integer constants.
    FixedVector,
    /// Scalable vector of integer constants.
    ScalableVector
  };

private:
  GIConstantKind Kind;
  SmallVector<APInt> Values;
  APInt Value;

public:
  /// Construct a fixed-vector GIConstant from element values.
  /// \param Values Element constants of the fixed vector.
  GIConstant(ArrayRef<APInt> Values)
      : Kind(GIConstantKind::FixedVector), Values(Values) {};
  /// Construct a scalar or scalable-vector GIConstant from a single value.
  /// \param Value Scalar or splat element value.
  /// \param Kind Whether the constant is scalar or a scalable vector.
  GIConstant(const APInt &Value, GIConstantKind Kind)
      : Kind(Kind), Value(Value) {};

  /// Returns the kind of of this constant, e.g, Scalar.
  /// \return The kind of this constant.
  GIConstantKind getKind() const { return Kind; }

  /// Returns the value, if this constant is a scalar.
  /// \return The scalar APInt value.
  LLVM_ABI APInt getScalarValue() const;

  /// Build a GIConstant from a register if it is a known integer constant.
  /// \param Const Virtual register that may define the constant.
  /// \param MRI Register information for the function.
  /// \return The GIConstant, or std::nullopt if \p Const is not a known
  /// integer constant.
  LLVM_ABI static std::optional<GIConstant>
  getConstant(Register Const, const MachineRegisterInfo &MRI);
};

/// An floating-point-like constant.
///
/// It abstracts over scalar, fixed-length vectors, and scalable vectors.
/// In the common case, it provides a common API and feels like an APFloat,
/// while still providing low-level access.
/// It can be used for constant-folding.
///
/// bool isZero()
/// abstracts over the kind.
///
/// switch(const.getKind())
/// {
/// }
/// provides low-level access.
class GFConstant {
  using VecTy = SmallVector<APFloat>;
  using const_iterator = VecTy::const_iterator;

public:
  /// Shape of the floating-point-like constant.
  enum class GFConstantKind {
    /// Scalar floating-point constant.
    Scalar,
    /// Fixed-length vector of floating-point constants.
    FixedVector,
    /// Scalable vector of floating-point constants.
    ScalableVector
  };

private:
  GFConstantKind Kind;
  SmallVector<APFloat> Values;

public:
  /// Construct a fixed-vector GFConstant from element values.
  /// \param Values Element constants of the fixed vector.
  GFConstant(ArrayRef<APFloat> Values)
      : Kind(GFConstantKind::FixedVector), Values(Values) {};
  /// Construct a scalar or scalable-vector GFConstant from a single value.
  /// \param Value Scalar or splat element value.
  /// \param Kind Whether the constant is scalar or a scalable vector.
  GFConstant(const APFloat &Value, GFConstantKind Kind) : Kind(Kind) {
    Values.push_back(Value);
  }

  /// Returns the kind of of this constant, e.g, Scalar.
  /// \return The kind of this constant.
  GFConstantKind getKind() const { return Kind; }

  /// Iterator to the first element of a scalar or fixed-vector constant.
  /// \return Iterator to the first element.
  const_iterator begin() const {
    assert(Kind != GFConstantKind::ScalableVector &&
           "Expected fixed vector or scalar constant");
    return Values.begin();
  }

  /// Iterator past the last element of a scalar or fixed-vector constant.
  /// \return Iterator past the last element.
  const_iterator end() const {
    assert(Kind != GFConstantKind::ScalableVector &&
           "Expected fixed vector or scalar constant");
    return Values.end();
  }

  /// Number of elements in a fixed-vector constant.
  /// \return The number of elements.
  size_t size() const {
    assert(Kind == GFConstantKind::FixedVector && "Expected fixed vector");
    return Values.size();
  }

  /// Returns the value, if this constant is a scalar.
  /// \return The scalar APFloat value.
  LLVM_ABI APFloat getScalarValue() const;

  /// Build a GFConstant from a register if it is a known float constant.
  /// \param Const Virtual register that may define the constant.
  /// \param MRI Register information for the function.
  /// \return The GFConstant, or std::nullopt if \p Const is not a known float
  /// constant.
  LLVM_ABI static std::optional<GFConstant>
  getConstant(Register Const, const MachineRegisterInfo &MRI);
};

} // End namespace llvm.
#endif
