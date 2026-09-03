//===-- ConstantFolding.h - Fold instructions into constants ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares routines for folding instructions into constants when all
// operands are constants, for example "sub i32 1, 0" -> "1".
//
// Also, to supplement the basic VMCore ConstantExpr simplifications,
// this file declares some additional folding routines that can make use of
// DataLayout information. These functions cannot go in VMCore due to library
// dependency issues.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_CONSTANTFOLDING_H
#define LLVM_ANALYSIS_CONSTANTFOLDING_H

#include "llvm/Support/Compiler.h"
#include <stdint.h>

namespace llvm {

namespace Intrinsic {
using ID = unsigned;
}

class APInt;
template <typename T> class ArrayRef;
class CallBase;
class Constant;
class DSOLocalEquivalent;
class DataLayout;
class Function;
class GlobalValue;
class GlobalVariable;
class Instruction;
class TargetLibraryInfo;
class Type;

/// Return true if \p C is a constant offset from a global.
///
/// Because of constantexprs, this function is recursive. If the global is part
/// of a dso_local_equivalent constant, return it through \p DSOEquiv if it is
/// provided.
/// @param C Constant to inspect for a global-plus-offset form.
/// @param GV Set to the base global value on success.
/// @param Offset Set to the constant byte offset from \p GV on success.
/// @param DL Data layout used for type sizes and pointer widths.
/// @param DSOEquiv Optional out-parameter for a dso_local_equivalent wrapper.
/// @return True if \p C is a constant offset from a global.
LLVM_ABI bool
IsConstantOffsetFromGlobal(Constant *C, GlobalValue *&GV, APInt &Offset,
                           const DataLayout &DL,
                           DSOLocalEquivalent **DSOEquiv = nullptr);

/// Try to constant fold the specified instruction.
///
/// If successful, the constant result is returned, if not, null is returned.
/// Note that this fails if not all of the operands are constant. Otherwise,
/// this function can only fail when attempting to fold instructions like loads
/// and stores, which have no constant expression form.
/// @param I Instruction to attempt to fold.
/// @param DL Data layout used for type sizes and pointer widths.
/// @param TLI Optional target library info for libcall folding.
/// @return The folded constant on success, or null on failure.
LLVM_ABI Constant *
ConstantFoldInstruction(const Instruction *I, const DataLayout &DL,
                        const TargetLibraryInfo *TLI = nullptr);

/// Fold a constant using the specified DataLayout.
///
/// This function always returns a non-null constant: Either the folding result,
/// or the original constant if further folding is not possible.
/// @param C Constant to fold.
/// @param DL Data layout used for type sizes and pointer widths.
/// @param TLI Optional target library info for libcall folding.
/// @return The folded constant, or \p C if no further folding is possible.
LLVM_ABI Constant *ConstantFoldConstant(const Constant *C, const DataLayout &DL,
                                        const TargetLibraryInfo *TLI = nullptr);

/// Attempt to constant fold an instruction with the specified operands.
///
/// If successful, the constant result is returned, if not, null is returned.
/// Note that this function can fail when attempting to fold instructions like
/// loads and stores, which have no constant expression form.
///
/// In some cases, constant folding may return one value chosen from a set of
/// multiple legal return values. For example, the exact bit pattern of NaN
/// results is not guaranteed. Using such a result is usually only valid if
/// all uses of the original operation are replaced by the constant-folded
/// result. The \p AllowNonDeterministic parameter controls whether this is
/// allowed.
/// @param I Instruction whose opcode and attributes guide folding.
/// @param Ops Constant operands to fold with.
/// @param DL Data layout used for type sizes and pointer widths.
/// @param TLI Optional target library info for libcall folding.
/// @param AllowNonDeterministic Whether non-deterministic fold results are OK.
/// @return The folded constant on success, or null on failure.
LLVM_ABI Constant *ConstantFoldInstOperands(
    const Instruction *I, ArrayRef<Constant *> Ops, const DataLayout &DL,
    const TargetLibraryInfo *TLI = nullptr, bool AllowNonDeterministic = true);

/// Attempt to constant fold a compare instruction with the specified operands.
///
/// Returns null or a constant expression of the specified operands on failure.
/// Denormal inputs may be flushed based on the denormal handling mode.
/// @param Predicate ICmp or FCmp predicate to evaluate.
/// @param LHS Left-hand constant operand.
/// @param RHS Right-hand constant operand.
/// @param DL Data layout used for type sizes and pointer widths.
/// @param TLI Optional target library info for libcall folding.
/// @param I Optional compare instruction providing denormal-mode context.
/// @return The folded constant, or null or a constant expression on failure.
LLVM_ABI Constant *ConstantFoldCompareInstOperands(
    unsigned Predicate, Constant *LHS, Constant *RHS, const DataLayout &DL,
    const TargetLibraryInfo *TLI = nullptr, const Instruction *I = nullptr);

/// Attempt to constant fold a unary operation with the specified operand.
/// Returns null on failure.
/// @param Opcode Unary opcode to fold.
/// @param Op Constant operand of the unary operation.
/// @param DL Data layout used for type sizes and pointer widths.
/// @return The folded constant on success, or null on failure.
LLVM_ABI Constant *ConstantFoldUnaryOpOperand(unsigned Opcode, Constant *Op,
                                              const DataLayout &DL);

/// Attempt to constant fold a binary operation with the specified operands.
/// Returns null or a constant expression of the specified operands on failure.
/// @param Opcode Binary opcode to fold.
/// @param LHS Left-hand constant operand.
/// @param RHS Right-hand constant operand.
/// @param DL Data layout used for type sizes and pointer widths.
/// @return The folded constant, or null or a constant expression on failure.
LLVM_ABI Constant *ConstantFoldBinaryOpOperands(unsigned Opcode, Constant *LHS,
                                                Constant *RHS,
                                                const DataLayout &DL);

/// Attempt to constant fold a floating-point binary operation with denormal
/// handling.
///
/// Applies the denormal handling mode to the operands. Returns null or a
/// constant expression of the specified operands on failure.
/// @param Opcode Floating-point binary opcode to fold.
/// @param LHS Left-hand constant operand.
/// @param RHS Right-hand constant operand.
/// @param DL Data layout used for type sizes and pointer widths.
/// @param I Instruction providing denormal-mode and FP environment context.
/// @param AllowNonDeterministic Whether non-deterministic fold results are OK.
/// @return The folded constant, or null or a constant expression on failure.
LLVM_ABI Constant *
ConstantFoldFPInstOperands(unsigned Opcode, Constant *LHS, Constant *RHS,
                           const DataLayout &DL, const Instruction *I,
                           bool AllowNonDeterministic = true);

/// Flush a floating-point constant according to the parent function's denormal
/// mode.
///
/// If so, return a zero with the correct sign, otherwise return the original
/// constant. Inputs and outputs to floating point instructions can have their
/// mode set separately, so the direction is also needed.
///
/// If the calling function's denormal_fpenv input mode is dynamic for the
/// floating-point type, returns nullptr for denormal inputs.
/// @param Operand Floating-point constant to possibly flush.
/// @param I Instruction whose parent function supplies denormal mode.
/// @param IsOutput True if flushing an output; false if flushing an input.
/// @return A correctly signed zero if flushed, \p Operand otherwise, or null
///         for denormal inputs under a dynamic denormal_fpenv mode.
LLVM_ABI Constant *FlushFPConstant(Constant *Operand, const Instruction *I,
                                   bool IsOutput);

/// Attempt to constant fold a cast with the specified operand.  If it
/// fails, it returns a constant expression of the specified operand.
/// @param Opcode Cast opcode to apply.
/// @param C Constant value to cast.
/// @param DestTy Destination type of the cast.
/// @param DL Data layout used for type sizes and pointer widths.
/// @return The folded constant, or a constant expression of \p C on failure.
LLVM_ABI Constant *ConstantFoldCastOperand(unsigned Opcode, Constant *C,
                                           Type *DestTy, const DataLayout &DL);

/// Constant fold a zext, sext or trunc, depending on IsSigned and whether the
/// DestTy is wider or narrower than C. Returns nullptr on failure.
/// @param C Integer constant to cast.
/// @param DestTy Destination integer type.
/// @param IsSigned Whether to treat the value as signed (sext vs zext).
/// @param DL Data layout used for type sizes and pointer widths.
/// @return The folded integer cast result, or null on failure.
LLVM_ABI Constant *ConstantFoldIntegerCast(Constant *C, Type *DestTy,
                                           bool IsSigned, const DataLayout &DL);

/// Extract value of C at the given Offset reinterpreted as Ty. If bits past
/// the end of C are accessed, they are assumed to be poison.
/// @param C Constant whose bytes are read.
/// @param Ty Type to reinterpret the loaded bits as.
/// @param Offset Byte offset into \p C at which to start reading.
/// @param DL Data layout used for type sizes and pointer widths.
/// @return The constant value of type \p Ty at \p Offset in \p C.
LLVM_ABI Constant *ConstantFoldLoadFromConst(Constant *C, Type *Ty,
                                             const APInt &Offset,
                                             const DataLayout &DL);

/// Extract value of C reinterpreted as Ty. Same as previous API with zero
/// offset.
/// @param C Constant whose bytes are read.
/// @param Ty Type to reinterpret the loaded bits as.
/// @param DL Data layout used for type sizes and pointer widths.
/// @return The constant value of type \p Ty reinterpreted from \p C.
LLVM_ABI Constant *ConstantFoldLoadFromConst(Constant *C, Type *Ty,
                                             const DataLayout &DL);

/// Return the value that a load from C with offset Offset would produce if it
/// is constant and determinable. If this is not determinable, return null.
/// @param C Pointer constant being loaded from.
/// @param Ty Type of the load result.
/// @param Offset Byte offset from \p C at which to load.
/// @param DL Data layout used for type sizes and pointer widths.
/// @return The loaded constant of type \p Ty, or null if not determinable.
LLVM_ABI Constant *ConstantFoldLoadFromConstPtr(Constant *C, Type *Ty,
                                                APInt Offset,
                                                const DataLayout &DL);

/// Return the value that a load from C would produce if it is constant and
/// determinable. If this is not determinable, return null.
/// @param C Pointer constant being loaded from.
/// @param Ty Type of the load result.
/// @param DL Data layout used for type sizes and pointer widths.
/// @return The loaded constant of type \p Ty, or null if not determinable.
LLVM_ABI Constant *ConstantFoldLoadFromConstPtr(Constant *C, Type *Ty,
                                                const DataLayout &DL);

/// Return a uniform bit-pattern constant reinterpreted as \p Ty, or null.
///
/// If C is a uniform value where all bits are the same (either all zero, all
/// ones, all undef or all poison), return the corresponding uniform value in
/// the new type. If the value is not uniform or the result cannot be
/// represented, return null.
/// @param C Constant tested for a uniform bit pattern.
/// @param Ty Destination type for the uniform value.
/// @param DL Data layout used for type sizes and pointer widths.
/// @return The uniform value reinterpreted as \p Ty, or null if not possible.
LLVM_ABI Constant *ConstantFoldLoadFromUniformValue(Constant *C, Type *Ty,
                                                    const DataLayout &DL);

/// Return true if it is even possible to fold a call to the specified function.
/// @param Call Call site providing calling-convention and FP context.
/// @param F Callee function being considered for folding.
/// @return True if a call to \p F may be constant-folded.
LLVM_ABI bool canConstantFoldCallTo(const CallBase *Call, const Function *F);

/// Attempt to constant fold a call to the specified function with the specified
/// arguments, returning null if unsuccessful.
/// @param Call Call site providing attributes and FP environment context.
/// @param F Callee function to fold.
/// @param Operands Constant arguments to the call.
/// @param TLI Optional target library info for libcall recognition.
/// @param AllowNonDeterministic Whether non-deterministic fold results are OK.
/// @return The folded constant result, or null if unsuccessful.
LLVM_ABI Constant *ConstantFoldCall(const CallBase *Call, Function *F,
                                    ArrayRef<Constant *> Operands,
                                    const TargetLibraryInfo *TLI = nullptr,
                                    bool AllowNonDeterministic = true);

/// Attempt to constant fold a call to the specified intrinsic.
/// @param ID Intrinsic identifier to fold.
/// @param Ops Constant operands of the intrinsic.
/// @param Ty Result type of the intrinsic.
/// @param DL Data layout used for type sizes and pointer widths.
/// @param CxtF Optional context function for strictfp and related attributes.
/// @return The folded constant result, or null if unsuccessful.
LLVM_ABI Constant *ConstantFoldIntrinsic(Intrinsic::ID ID,
                                         ArrayRef<Constant *> Ops, Type *Ty,
                                         const DataLayout &DL,
                                         Function *CxtF = nullptr);

/// Try to cast a constant to a destination type through a bitcast-like load.
///
/// Returns null if unsuccessful. Can cast pointer to pointer or pointer to
/// integer and vice versa if their sizes are equal.
/// @param C Constant value to cast.
/// @param DestTy Destination type of the cast.
/// @param DL Data layout used for type sizes and pointer widths.
/// @return The cast constant of type \p DestTy, or null if unsuccessful.
LLVM_ABI Constant *ConstantFoldLoadThroughBitcast(Constant *C, Type *DestTy,
                                                  const DataLayout &DL);

/// Check whether the given call has no side-effects.
/// Specifically checks for math routimes which sometimes set errno.
/// @param Call Call to inspect for math-lib side effects.
/// @param TLI Target library info used to recognize math libcalls.
/// @return True if the math libcall is a no-op with no side effects.
LLVM_ABI bool isMathLibCallNoop(const CallBase *Call,
                                const TargetLibraryInfo *TLI);

/// Read the trailing bytes of a global's initializer starting at \p Offset.
/// @param GV Constant global whose initializer is read.
/// @param Offset Byte offset into the initializer at which to start.
/// @return A constant byte array for the trailing initializer bytes.
LLVM_ABI Constant *ReadByteArrayFromGlobal(const GlobalVariable *GV,
                                           uint64_t Offset);

/// Cast flags that can be preserved by a lossless inverse cast.
struct PreservedCastFlags {
  /// Whether a non-negative (nneg) flag can be preserved.
  bool NNeg = false;
  /// Whether a no-unsigned-wrap (nuw) flag can be preserved.
  bool NUW = false;
  /// Whether a no-signed-wrap (nsw) flag can be preserved.
  bool NSW = false;
};

/// Try to cast C to InvC losslessly, satisfying CastOp(InvC) equals C, or
/// CastOp(InvC) is a refined value of undefined C. Will try best to
/// preserve the flags.
/// @param C Constant to invert through \p CastOp.
/// @param InvCastTo Type of the inverse-cast result.
/// @param CastOp Cast opcode that maps the result back to \p C.
/// @param DL Data layout used for type sizes and pointer widths.
/// @param Flags Optional out-parameter for preserved cast flags.
/// @return The inverse-cast constant, or null if no lossless inverse exists.
LLVM_ABI Constant *getLosslessInvCast(Constant *C, Type *InvCastTo,
                                      unsigned CastOp, const DataLayout &DL,
                                      PreservedCastFlags *Flags = nullptr);

/// Compute a lossless inverse of an unsigned extend (zext) of \p C.
/// @param C Constant that is the result of a zext.
/// @param DestTy Narrower type of the pre-extend value.
/// @param DL Data layout used for type sizes and pointer widths.
/// @param Flags Optional out-parameter for preserved cast flags.
/// @return The pre-zext constant of type \p DestTy, or null on failure.
LLVM_ABI Constant *
getLosslessUnsignedTrunc(Constant *C, Type *DestTy, const DataLayout &DL,
                         PreservedCastFlags *Flags = nullptr);

/// Compute a lossless inverse of a signed extend (sext) of \p C.
/// @param C Constant that is the result of a sext.
/// @param DestTy Narrower type of the pre-extend value.
/// @param DL Data layout used for type sizes and pointer widths.
/// @param Flags Optional out-parameter for preserved cast flags.
/// @return The pre-sext constant of type \p DestTy, or null on failure.
LLVM_ABI Constant *getLosslessSignedTrunc(Constant *C, Type *DestTy,
                                          const DataLayout &DL,
                                          PreservedCastFlags *Flags = nullptr);
} // namespace llvm

#endif
