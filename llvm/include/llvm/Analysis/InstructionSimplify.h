//===-- InstructionSimplify.h - Fold instrs into simpler forms --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares routines for folding instructions into simpler forms
// that do not require creating new instructions.  This does constant folding
// ("add i32 1, 1" -> "2") but can also handle non-constant operands, either
// returning a constant ("and i32 %x, 0" -> "0") or an already existing value
// ("and i32 %x, %x" -> "%x").  If the simplification is also an instruction
// then it dominates the original instruction.
//
// These routines implicitly resolve undef uses. The easiest way to be safe when
// using these routines to obtain simplified values for existing instructions is
// to always replace all uses of the instructions with the resulting simplified
// values. This will prevent other code from seeing the same undef uses and
// resolving them to different values.
//
// They require that all the IR that they encounter be valid and inserted into a
// parent function.
//
// Additionally, these routines can't simplify to the instructions that are not
// def-reachable, meaning we can't just scan the basic block for instructions
// to simplify to.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_INSTRUCTIONSIMPLIFY_H
#define LLVM_ANALYSIS_INSTRUCTIONSIMPLIFY_H

#include "llvm/Analysis/SimplifyQuery.h"
#include "llvm/IR/FPEnv.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

template <typename T, typename... TArgs> class AnalysisManager;
template <class T> class ArrayRef;
class AssumptionCache;
class CallBase;
class DataLayout;
class DominatorTree;
class Function;
class Instruction;
class CmpPredicate;
class LoadInst;
struct LoopStandardAnalysisResults;
class Pass;
template <class T, unsigned n> class SmallSetVector;
class TargetLibraryInfo;
class Type;
class Value;

// NOTE: the explicit multiple argument versions of these functions are
// deprecated.
// Please use the SimplifyQuery versions in new code.

/// Given operands for an Add, fold the result or return null.
/// @param LHS Left-hand side operand.
/// @param RHS Right-hand side operand.
/// @param IsNSW Whether the add has the nsw flag.
/// @param IsNUW Whether the add has the nuw flag.
/// @param Q Simplification query providing analyses and flags.
/// @return The folded value, or null if no simplification was found.
LLVM_ABI Value *simplifyAddInst(Value *LHS, Value *RHS, bool IsNSW, bool IsNUW,
                                const SimplifyQuery &Q);

/// Given operands for a Sub, fold the result or return null.
/// @param LHS Left-hand side operand.
/// @param RHS Right-hand side operand.
/// @param IsNSW Whether the sub has the nsw flag.
/// @param IsNUW Whether the sub has the nuw flag.
/// @param Q Simplification query providing analyses and flags.
/// @return The folded value, or null if no simplification was found.
LLVM_ABI Value *simplifySubInst(Value *LHS, Value *RHS, bool IsNSW, bool IsNUW,
                                const SimplifyQuery &Q);

/// Given operands for a Mul, fold the result or return null.
/// @param LHS Left-hand side operand.
/// @param RHS Right-hand side operand.
/// @param IsNSW Whether the mul has the nsw flag.
/// @param IsNUW Whether the mul has the nuw flag.
/// @param Q Simplification query providing analyses and flags.
/// @return The folded value, or null if no simplification was found.
LLVM_ABI Value *simplifyMulInst(Value *LHS, Value *RHS, bool IsNSW, bool IsNUW,
                                const SimplifyQuery &Q);

/// Given operands for an SDiv, fold the result or return null.
/// @param LHS Left-hand side operand.
/// @param RHS Right-hand side operand.
/// @param IsExact Whether the sdiv has the exact flag.
/// @param Q Simplification query providing analyses and flags.
/// @return The folded value, or null if no simplification was found.
LLVM_ABI Value *simplifySDivInst(Value *LHS, Value *RHS, bool IsExact,
                                 const SimplifyQuery &Q);

/// Given operands for a UDiv, fold the result or return null.
/// @param LHS Left-hand side operand.
/// @param RHS Right-hand side operand.
/// @param IsExact Whether the udiv has the exact flag.
/// @param Q Simplification query providing analyses and flags.
/// @return The folded value, or null if no simplification was found.
LLVM_ABI Value *simplifyUDivInst(Value *LHS, Value *RHS, bool IsExact,
                                 const SimplifyQuery &Q);

/// Given operands for an SRem, fold the result or return null.
/// @param LHS Left-hand side operand.
/// @param RHS Right-hand side operand.
/// @param Q Simplification query providing analyses and flags.
/// @return The folded value, or null if no simplification was found.
LLVM_ABI Value *simplifySRemInst(Value *LHS, Value *RHS,
                                 const SimplifyQuery &Q);

/// Given operands for a URem, fold the result or return null.
/// @param LHS Left-hand side operand.
/// @param RHS Right-hand side operand.
/// @param Q Simplification query providing analyses and flags.
/// @return The folded value, or null if no simplification was found.
LLVM_ABI Value *simplifyURemInst(Value *LHS, Value *RHS,
                                 const SimplifyQuery &Q);

/// Given operand for an FNeg, fold the result or return null.
/// @param Op Operand to negate.
/// @param FMF Fast-math flags to honor while folding.
/// @param Q Simplification query providing analyses and flags.
/// @return The folded value, or null if no simplification was found.
LLVM_ABI Value *simplifyFNegInst(Value *Op, FastMathFlags FMF,
                                 const SimplifyQuery &Q);

/// Given operands for an FAdd, fold the result or return null.
/// @param LHS Left-hand side operand.
/// @param RHS Right-hand side operand.
/// @param FMF Fast-math flags to honor while folding.
/// @param Q Simplification query providing analyses and flags.
/// @param ExBehavior Floating-point exception behavior to assume.
/// @param Rounding Rounding mode to assume.
/// @return The folded value, or null if no simplification was found.
LLVM_ABI Value *
simplifyFAddInst(Value *LHS, Value *RHS, FastMathFlags FMF,
                 const SimplifyQuery &Q,
                 fp::ExceptionBehavior ExBehavior = fp::ebIgnore,
                 RoundingMode Rounding = RoundingMode::NearestTiesToEven);

/// Given operands for an FSub, fold the result or return null.
/// @param LHS Left-hand side operand.
/// @param RHS Right-hand side operand.
/// @param FMF Fast-math flags to honor while folding.
/// @param Q Simplification query providing analyses and flags.
/// @param ExBehavior Floating-point exception behavior to assume.
/// @param Rounding Rounding mode to assume.
/// @return The folded value, or null if no simplification was found.
LLVM_ABI Value *
simplifyFSubInst(Value *LHS, Value *RHS, FastMathFlags FMF,
                 const SimplifyQuery &Q,
                 fp::ExceptionBehavior ExBehavior = fp::ebIgnore,
                 RoundingMode Rounding = RoundingMode::NearestTiesToEven);

/// Given operands for an FMul, fold the result or return null.
/// @param LHS Left-hand side operand.
/// @param RHS Right-hand side operand.
/// @param FMF Fast-math flags to honor while folding.
/// @param Q Simplification query providing analyses and flags.
/// @param ExBehavior Floating-point exception behavior to assume.
/// @param Rounding Rounding mode to assume.
/// @return The folded value, or null if no simplification was found.
LLVM_ABI Value *
simplifyFMulInst(Value *LHS, Value *RHS, FastMathFlags FMF,
                 const SimplifyQuery &Q,
                 fp::ExceptionBehavior ExBehavior = fp::ebIgnore,
                 RoundingMode Rounding = RoundingMode::NearestTiesToEven);

/// Given operands for the multiplication of a FMA, fold the result or return
/// null.
///
/// In contrast to simplifyFMulInst, this function will not perform
/// simplifications whose unrounded results differ when rounded to the argument
/// type.
/// @param LHS Left-hand side operand.
/// @param RHS Right-hand side operand.
/// @param FMF Fast-math flags to honor while folding.
/// @param Q Simplification query providing analyses and flags.
/// @param ExBehavior Floating-point exception behavior to assume.
/// @param Rounding Rounding mode to assume.
/// @return The folded value, or null if no simplification was found.
LLVM_ABI Value *
simplifyFMAFMul(Value *LHS, Value *RHS, FastMathFlags FMF,
                const SimplifyQuery &Q,
                fp::ExceptionBehavior ExBehavior = fp::ebIgnore,
                RoundingMode Rounding = RoundingMode::NearestTiesToEven);

/// Given operands for an FDiv, fold the result or return null.
/// @param LHS Left-hand side operand.
/// @param RHS Right-hand side operand.
/// @param FMF Fast-math flags to honor while folding.
/// @param Q Simplification query providing analyses and flags.
/// @param ExBehavior Floating-point exception behavior to assume.
/// @param Rounding Rounding mode to assume.
/// @return The folded value, or null if no simplification was found.
LLVM_ABI Value *
simplifyFDivInst(Value *LHS, Value *RHS, FastMathFlags FMF,
                 const SimplifyQuery &Q,
                 fp::ExceptionBehavior ExBehavior = fp::ebIgnore,
                 RoundingMode Rounding = RoundingMode::NearestTiesToEven);

/// Given operands for an FRem, fold the result or return null.
/// @param LHS Left-hand side operand.
/// @param RHS Right-hand side operand.
/// @param FMF Fast-math flags to honor while folding.
/// @param Q Simplification query providing analyses and flags.
/// @param ExBehavior Floating-point exception behavior to assume.
/// @param Rounding Rounding mode to assume.
/// @return The folded value, or null if no simplification was found.
LLVM_ABI Value *
simplifyFRemInst(Value *LHS, Value *RHS, FastMathFlags FMF,
                 const SimplifyQuery &Q,
                 fp::ExceptionBehavior ExBehavior = fp::ebIgnore,
                 RoundingMode Rounding = RoundingMode::NearestTiesToEven);

/// Given operands for a Shl, fold the result or return null.
/// @param Op0 Value being shifted.
/// @param Op1 Shift amount.
/// @param IsNSW Whether the shl has the nsw flag.
/// @param IsNUW Whether the shl has the nuw flag.
/// @param Q Simplification query providing analyses and flags.
/// @return The folded value, or null if no simplification was found.
LLVM_ABI Value *simplifyShlInst(Value *Op0, Value *Op1, bool IsNSW, bool IsNUW,
                                const SimplifyQuery &Q);

/// Given operands for a LShr, fold the result or return null.
/// @param Op0 Value being shifted.
/// @param Op1 Shift amount.
/// @param IsExact Whether the lshr has the exact flag.
/// @param Q Simplification query providing analyses and flags.
/// @return The folded value, or null if no simplification was found.
LLVM_ABI Value *simplifyLShrInst(Value *Op0, Value *Op1, bool IsExact,
                                 const SimplifyQuery &Q);

/// Given operands for a AShr, fold the result or return nulll.
/// @param Op0 Value being shifted.
/// @param Op1 Shift amount.
/// @param IsExact Whether the ashr has the exact flag.
/// @param Q Simplification query providing analyses and flags.
/// @return The folded value, or null if no simplification was found.
LLVM_ABI Value *simplifyAShrInst(Value *Op0, Value *Op1, bool IsExact,
                                 const SimplifyQuery &Q);

/// Given operands for an And, fold the result or return null.
/// @param LHS Left-hand side operand.
/// @param RHS Right-hand side operand.
/// @param Q Simplification query providing analyses and flags.
/// @return The folded value, or null if no simplification was found.
LLVM_ABI Value *simplifyAndInst(Value *LHS, Value *RHS, const SimplifyQuery &Q);

/// Given operands for an Or, fold the result or return null.
/// @param LHS Left-hand side operand.
/// @param RHS Right-hand side operand.
/// @param Q Simplification query providing analyses and flags.
/// @return The folded value, or null if no simplification was found.
LLVM_ABI Value *simplifyOrInst(Value *LHS, Value *RHS, const SimplifyQuery &Q);

/// Given operands for an Xor, fold the result or return null.
/// @param LHS Left-hand side operand.
/// @param RHS Right-hand side operand.
/// @param Q Simplification query providing analyses and flags.
/// @return The folded value, or null if no simplification was found.
LLVM_ABI Value *simplifyXorInst(Value *LHS, Value *RHS, const SimplifyQuery &Q);

/// Given operands for an ICmpInst, fold the result or return null.
/// @param Pred Integer comparison predicate.
/// @param LHS Left-hand side operand.
/// @param RHS Right-hand side operand.
/// @param Q Simplification query providing analyses and flags.
/// @return The folded value, or null if no simplification was found.
LLVM_ABI Value *simplifyICmpInst(CmpPredicate Pred, Value *LHS, Value *RHS,
                                 const SimplifyQuery &Q);

/// Given operands for an FCmpInst, fold the result or return null.
/// @param Predicate Floating-point comparison predicate.
/// @param LHS Left-hand side operand.
/// @param RHS Right-hand side operand.
/// @param FMF Fast-math flags to honor while folding.
/// @param Q Simplification query providing analyses and flags.
/// @return The folded value, or null if no simplification was found.
LLVM_ABI Value *simplifyFCmpInst(CmpPredicate Predicate, Value *LHS, Value *RHS,
                                 FastMathFlags FMF, const SimplifyQuery &Q);

/// Given operands for a SelectInst, fold the result or return null.
/// @param Cond Select condition.
/// @param TrueVal Value selected when the condition is true.
/// @param FalseVal Value selected when the condition is false.
/// @param FMF Fast-math flags to honor while folding.
/// @param Q Simplification query providing analyses and flags.
/// @return The folded value, or null if no simplification was found.
LLVM_ABI Value *simplifySelectInst(Value *Cond, Value *TrueVal, Value *FalseVal,
                                   FastMathFlags FMF, const SimplifyQuery &Q);

/// Given operands for a GetElementPtrInst, fold the result or return null.
/// @param SrcTy Source element type of the GEP.
/// @param Ptr Base pointer operand.
/// @param Indices Index operands of the GEP.
/// @param NW No-wrap flags of the GEP.
/// @param Q Simplification query providing analyses and flags.
/// @return The folded value, or null if no simplification was found.
LLVM_ABI Value *simplifyGEPInst(Type *SrcTy, Value *Ptr,
                                ArrayRef<Value *> Indices, GEPNoWrapFlags NW,
                                const SimplifyQuery &Q);

/// Given operands for an InsertValueInst, fold the result or return null.
/// @param Agg Aggregate value being updated.
/// @param Val Value inserted into the aggregate.
/// @param Idxs Indices locating the insertion site.
/// @param Q Simplification query providing analyses and flags.
/// @return The folded value, or null if no simplification was found.
LLVM_ABI Value *simplifyInsertValueInst(Value *Agg, Value *Val,
                                        ArrayRef<unsigned> Idxs,
                                        const SimplifyQuery &Q);

/// Given operands for an InsertElement, fold the result or return null.
/// @param Vec Vector value being updated.
/// @param Elt Element value inserted into the vector.
/// @param Idx Index of the inserted element.
/// @param Q Simplification query providing analyses and flags.
/// @return The folded value, or null if no simplification was found.
LLVM_ABI Value *simplifyInsertElementInst(Value *Vec, Value *Elt, Value *Idx,
                                          const SimplifyQuery &Q);

/// Given operands for an ExtractValueInst, fold the result or return null.
/// @param Agg Aggregate value being extracted from.
/// @param Idxs Indices locating the extracted value.
/// @param Q Simplification query providing analyses and flags.
/// @return The folded value, or null if no simplification was found.
LLVM_ABI Value *simplifyExtractValueInst(Value *Agg, ArrayRef<unsigned> Idxs,
                                         const SimplifyQuery &Q);

/// Given operands for an ExtractElementInst, fold the result or return null.
/// @param Vec Vector value being extracted from.
/// @param Idx Index of the extracted element.
/// @param Q Simplification query providing analyses and flags.
/// @return The folded value, or null if no simplification was found.
LLVM_ABI Value *simplifyExtractElementInst(Value *Vec, Value *Idx,
                                           const SimplifyQuery &Q);

/// Given operands for a CastInst, fold the result or return null.
/// @param CastOpc Cast opcode.
/// @param Op Value being cast.
/// @param Ty Destination type of the cast.
/// @param Q Simplification query providing analyses and flags.
/// @return The folded value, or null if no simplification was found.
LLVM_ABI Value *simplifyCastInst(unsigned CastOpc, Value *Op, Type *Ty,
                                 const SimplifyQuery &Q);

/// Given operands for an intrinsic, fold the result or return null.
///
/// Context Function is passed as \p CxtF. \p ExBehavior and \p Rounding only
/// apply to constrained FP intrinsics.
/// @param IID Intrinsic identifier.
/// @param ReturnType Return type of the intrinsic.
/// @param Args Operand values of the intrinsic.
/// @param FMF Fast-math flags to honor while folding.
/// @param Q Simplification query providing analyses and flags.
/// @param CxtF Optional context function for the intrinsic.
/// @param ExBehavior Floating-point exception behavior to assume.
/// @param Rounding Rounding mode to assume.
/// @return The folded value, or null if no simplification was found.
LLVM_ABI Value *
simplifyIntrinsic(Intrinsic::ID IID, Type *ReturnType, ArrayRef<Value *> Args,
                  FastMathFlags FMF, const SimplifyQuery &Q,
                  Function *CxtF = nullptr,
                  fp::ExceptionBehavior ExBehavior = fp::ebIgnore,
                  RoundingMode Rounding = RoundingMode::NearestTiesToEven);

/// Given operands for a ShuffleVectorInst, fold the result or return null.
///
/// See class ShuffleVectorInst for a description of the mask representation.
/// @param Op0 First vector operand.
/// @param Op1 Second vector operand.
/// @param Mask Shuffle mask.
/// @param RetTy Result type of the shuffle.
/// @param Q Simplification query providing analyses and flags.
/// @return The folded value, or null if no simplification was found.
LLVM_ABI Value *simplifyShuffleVectorInst(Value *Op0, Value *Op1,
                                          ArrayRef<int> Mask, Type *RetTy,
                                          const SimplifyQuery &Q);

//=== Helper functions for higher up the class hierarchy.

/// Given operands for a CmpInst, fold the result or return null.
/// @param Predicate Comparison predicate.
/// @param LHS Left-hand side operand.
/// @param RHS Right-hand side operand.
/// @param Q Simplification query providing analyses and flags.
/// @return The folded value, or null if no simplification was found.
LLVM_ABI Value *simplifyCmpInst(CmpPredicate Predicate, Value *LHS, Value *RHS,
                                const SimplifyQuery &Q);

/// Given operand for a UnaryOperator, fold the result or return null.
/// @param Opcode Unary operator opcode.
/// @param Op Operand of the unary operator.
/// @param Q Simplification query providing analyses and flags.
/// @return The folded value, or null if no simplification was found.
LLVM_ABI Value *simplifyUnOp(unsigned Opcode, Value *Op,
                             const SimplifyQuery &Q);

/// Given operand for a UnaryOperator, fold the result or return null.
///
/// Try to use FastMathFlags when folding the result.
/// @param Opcode Unary operator opcode.
/// @param Op Operand of the unary operator.
/// @param FMF Fast-math flags to honor while folding.
/// @param Q Simplification query providing analyses and flags.
/// @return The folded value, or null if no simplification was found.
LLVM_ABI Value *simplifyUnOp(unsigned Opcode, Value *Op, FastMathFlags FMF,
                             const SimplifyQuery &Q);

/// Given operands for a BinaryOperator, fold the result or return null.
/// @param Opcode Binary operator opcode.
/// @param LHS Left-hand side operand.
/// @param RHS Right-hand side operand.
/// @param Q Simplification query providing analyses and flags.
/// @return The folded value, or null if no simplification was found.
LLVM_ABI Value *simplifyBinOp(unsigned Opcode, Value *LHS, Value *RHS,
                              const SimplifyQuery &Q);

/// Given operands for a BinaryOperator, fold the result or return null.
///
/// Try to use FastMathFlags when folding the result.
/// @param Opcode Binary operator opcode.
/// @param LHS Left-hand side operand.
/// @param RHS Right-hand side operand.
/// @param FMF Fast-math flags to honor while folding.
/// @param Q Simplification query providing analyses and flags.
/// @return The folded value, or null if no simplification was found.
LLVM_ABI Value *simplifyBinOp(unsigned Opcode, Value *LHS, Value *RHS,
                              FastMathFlags FMF, const SimplifyQuery &Q);

/// Given a callsite, callee, and arguments, fold the result or return null.
/// @param Call Callsite being simplified.
/// @param Callee Callee value of the call.
/// @param Args Argument values of the call.
/// @param Q Simplification query providing analyses and flags.
/// @return The folded value, or null if no simplification was found.
LLVM_ABI Value *simplifyCall(CallBase *Call, Value *Callee,
                             ArrayRef<Value *> Args, const SimplifyQuery &Q);

/// Given a constrained FP intrinsic call, tries to compute its simplified
/// version.
///
/// Returns a simplified result or null. This function provides an additional
/// contract: it guarantees that if simplification succeeds that the intrinsic
/// is side effect free. As a result, successful simplification can be used to
/// delete the intrinsic not just replace its result.
/// @param Call Constrained floating-point intrinsic call.
/// @param Q Simplification query providing analyses and flags.
/// @return The simplified result, or null if the call could not be simplified.
LLVM_ABI Value *simplifyConstrainedFPCall(CallBase *Call,
                                          const SimplifyQuery &Q);

/// Given an operand for a Freeze, see if we can fold the result.
///
/// If not, this returns null.
/// @param Op Operand of the freeze.
/// @param Q Simplification query providing analyses and flags.
/// @return The folded value, or null if no simplification was found.
LLVM_ABI Value *simplifyFreezeInst(Value *Op, const SimplifyQuery &Q);

/// Given a load instruction and its pointer operand, fold the result or return
/// null.
/// @param LI Load instruction being simplified.
/// @param PtrOp Pointer operand of the load.
/// @param Q Simplification query providing analyses and flags.
/// @return The folded value, or null if no simplification was found.
LLVM_ABI Value *simplifyLoadInst(LoadInst *LI, Value *PtrOp,
                                 const SimplifyQuery &Q);

/// See if we can compute a simplified version of this instruction.
///
/// If not, return null.
/// @param I Instruction to simplify.
/// @param Q Simplification query providing analyses and flags.
/// @return The simplified value, or null if the instruction could not be simplified.
LLVM_ABI Value *simplifyInstruction(Instruction *I, const SimplifyQuery &Q);

/// Like \p simplifyInstruction but the operands of \p I are replaced with
/// \p NewOps.
///
/// Returns a simplified value, or null if none was found.
/// @param I Instruction to simplify.
/// @param NewOps Replacement operands for \p I.
/// @param Q Simplification query providing analyses and flags.
/// @return The simplified value, or null if none was found.
LLVM_ABI Value *simplifyInstructionWithOperands(Instruction *I,
                                                ArrayRef<Value *> NewOps,
                                                const SimplifyQuery &Q);

/// See if V simplifies when its operand Op is replaced with RepOp.
///
/// If not, return null. AllowRefinement specifies whether the simplification
/// can be a refinement (e.g. 0 instead of poison), or whether it needs to be
/// strictly identical. Op and RepOp can be assumed to not be poison when
/// determining refinement.
///
/// If DropFlags is passed, then the replacement result is only valid if
/// poison-generating flags/metadata on those instructions are dropped. This
/// is only useful in conjunction with AllowRefinement=false.
/// @param V Value being simplified.
/// @param Op Operand of \p V that is replaced.
/// @param RepOp Replacement for \p Op.
/// @param Q Simplification query providing analyses and flags.
/// @param AllowRefinement Whether a refined result is acceptable.
/// @param DropFlags Optional list of instructions whose flags must be dropped.
/// @return The simplified value, or null if no simplification was found.
LLVM_ABI Value *
simplifyWithOpReplaced(Value *V, Value *Op, Value *RepOp,
                       const SimplifyQuery &Q, bool AllowRefinement,
                       SmallVectorImpl<Instruction *> *DropFlags = nullptr);

/// Replace all uses of 'I' with 'SimpleV' and simplify the uses recursively.
///
/// This first performs a normal RAUW of I with SimpleV. It then recursively
/// attempts to simplify those users updated by the operation. The 'I'
/// instruction must not be equal to the simplified value 'SimpleV'.
/// If UnsimplifiedUsers is provided, instructions that could not be simplified
/// are added to it.
///
/// The function returns true if any simplifications were performed.
/// @param I Instruction whose uses are replaced.
/// @param SimpleV Simplified replacement for \p I.
/// @param TLI Optional target library info.
/// @param DT Optional dominator tree.
/// @param AC Optional assumption cache.
/// @param UnsimplifiedUsers Optional set of users that could not be simplified.
/// @return True if any simplifications were performed.
LLVM_ABI bool replaceAndRecursivelySimplify(
    Instruction *I, Value *SimpleV, const TargetLibraryInfo *TLI = nullptr,
    const DominatorTree *DT = nullptr, AssumptionCache *AC = nullptr,
    SmallSetVector<Instruction *, 8> *UnsimplifiedUsers = nullptr);

/// Build a SimplifyQuery from analyses currently valid in a pass.
///
/// This is the strongly preferred way of constructing SimplifyQuery in passes.
/// @param P Pass providing currently valid analyses.
/// @param F Function being simplified.
/// @return A SimplifyQuery built from the currently valid analyses.
LLVM_ABI const SimplifyQuery getBestSimplifyQuery(Pass &P, Function &F);

/// Build a SimplifyQuery from analyses currently valid in an analysis manager.
///
/// This is the strongly preferred way of constructing SimplifyQuery in passes.
/// @param AM Analysis manager providing currently valid analyses.
/// @param F Function being simplified.
/// @return A SimplifyQuery built from the currently valid analyses.
template <class T, class... TArgs>
const SimplifyQuery getBestSimplifyQuery(AnalysisManager<T, TArgs...> &AM,
                                         Function &F);

/// Build a SimplifyQuery from loop analyses and a data layout.
///
/// This is the strongly preferred way of constructing SimplifyQuery in passes.
/// @param AR Loop analyses providing currently valid results.
/// @param DL Data layout used for type sizes and pointer widths.
/// @return A SimplifyQuery built from the currently valid analyses.
LLVM_ABI const SimplifyQuery getBestSimplifyQuery(LoopStandardAnalysisResults &AR,
                                                  const DataLayout &DL);
} // end namespace llvm

#endif
