//===- CodeGen/Analysis.h - CodeGen LLVM IR Analysis Utilities --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares several CodeGen-specific LLVM IR analysis utilities.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_ANALYSIS_H
#define LLVM_CODEGEN_ANALYSIS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/IR/Instructions.h"

namespace llvm {
template <typename T> class SmallVectorImpl;
class GlobalValue;
class LLT;
class MachineBasicBlock;
class MachineFunction;
class TargetLoweringBase;
class TargetLowering;
class TargetMachine;
struct EVT;

/// Compute the linearized index of a member in a nested
/// aggregate/struct/array.
///
/// Given an LLVM IR aggregate type and a sequence of insertvalue or
/// extractvalue indices that identify a member, return the linearized index of
/// the start of the member, i.e the number of element in memory before the
/// sought one. This is disconnected from the number of bytes.
///
/// \param Ty is the type indexed by \p Indices.
/// \param Indices is an optional pointer in the indices list to the current
/// index.
/// \param IndicesEnd is the end of the indices list.
/// \param CurIndex is the current index in the recursion.
///
/// \returns \p CurIndex plus the linear index in \p Ty  the indices list.
LLVM_ABI unsigned ComputeLinearIndex(Type *Ty, const unsigned *Indices,
                                     const unsigned *IndicesEnd,
                                     unsigned CurIndex = 0);

/// Compute the linearized index of a member in a nested aggregate.
///
/// \param Ty Aggregate type indexed by \p Indices.
/// \param Indices Sequence of insertvalue or extractvalue indices.
/// \param CurIndex Current index used as the recursion base.
///
/// \returns The linearized index of the member identified by \p Indices.
inline unsigned ComputeLinearIndex(Type *Ty,
                                   ArrayRef<unsigned> Indices,
                                   unsigned CurIndex = 0) {
  return ComputeLinearIndex(Ty, Indices.begin(), Indices.end(), CurIndex);
}

/// Given an LLVM IR type, compute non-aggregate subtypes. Optionally also
/// compute their offsets.
///
/// \param DL Data layout used to determine type sizes and offsets.
/// \param Ty LLVM IR type to decompose.
/// \param Types Filled with the non-aggregate subtypes of \p Ty.
/// \param Offsets Optional vector filled with the in-memory offset of each
///        subtype.
/// \param StartingOffset Base offset applied to each recorded offset.
LLVM_ABI void ComputeValueTypes(const DataLayout &DL, Type *Ty,
                                SmallVectorImpl<Type *> &Types,
                                SmallVectorImpl<TypeSize> *Offsets = nullptr,
                                TypeSize StartingOffset = TypeSize::getZero());

/// Compute a sequence of EVTs for the non-aggregate parts of an LLVM IR type.
///
/// Given an LLVM IR type, compute a sequence of EVTs that represent all the
/// individual underlying non-aggregate types that comprise it.
///
/// If Offsets is non-null, it points to a vector to be filled in
/// with the in-memory offsets of each of the individual values.
///
/// \param TLI Target lowering used to map IR types to EVTs.
/// \param DL Data layout used to determine type sizes and offsets.
/// \param Ty LLVM IR type to decompose.
/// \param ValueVTs Filled with the EVTs of the non-aggregate parts of \p Ty.
/// \param MemVTs Optional vector filled with the corresponding memory EVTs.
/// \param Offsets Optional vector filled with the in-memory offset of each
///        value.
/// \param StartingOffset Base offset applied to each recorded offset.
LLVM_ABI void ComputeValueVTs(const TargetLowering &TLI, const DataLayout &DL,
                              Type *Ty, SmallVectorImpl<EVT> &ValueVTs,
                              SmallVectorImpl<EVT> *MemVTs = nullptr,
                              SmallVectorImpl<TypeSize> *Offsets = nullptr,
                              TypeSize StartingOffset = TypeSize::getZero());

/// Compute a sequence of EVTs for the non-aggregate parts of an LLVM IR type.
///
/// This overload records fixed (byte) offsets instead of TypeSize offsets.
///
/// \param TLI Target lowering used to map IR types to EVTs.
/// \param DL Data layout used to determine type sizes and offsets.
/// \param Ty LLVM IR type to decompose.
/// \param ValueVTs Filled with the EVTs of the non-aggregate parts of \p Ty.
/// \param MemVTs Vector filled with the corresponding memory EVTs.
/// \param FixedOffsets Optional vector filled with the fixed in-memory offset
///        of each value.
/// \param StartingOffset Base offset applied to each recorded offset.
LLVM_ABI void ComputeValueVTs(const TargetLowering &TLI, const DataLayout &DL,
                              Type *Ty, SmallVectorImpl<EVT> &ValueVTs,
                              SmallVectorImpl<EVT> *MemVTs,
                              SmallVectorImpl<uint64_t> *FixedOffsets,
                              uint64_t StartingOffset);

/// Compute a sequence of LLTs for the non-aggregate parts of an LLVM IR type.
///
/// Given an LLVM IR type, compute a sequence of LLTs that represent all the
/// individual underlying non-aggregate types that comprise it.
///
/// If Offsets is non-null, it points to a vector to be filled in
/// with the in-memory offsets of each of the individual values.
///
/// \param DL Data layout used to determine type sizes and offsets.
/// \param Ty LLVM IR type to decompose.
/// \param ValueLLTs Filled with the LLTs of the non-aggregate parts of \p Ty.
/// \param Offsets Optional vector filled with the in-memory offset of each
///        value.
/// \param StartingOffset Base offset applied to each recorded offset.
LLVM_ABI void computeValueLLTs(const DataLayout &DL, Type &Ty,
                               SmallVectorImpl<LLT> &ValueLLTs,
                               SmallVectorImpl<TypeSize> *Offsets = nullptr,
                               TypeSize StartingOffset = TypeSize::getZero());

/// Compute a sequence of LLTs for the non-aggregate parts of an LLVM IR type.
///
/// This overload records fixed (byte) offsets instead of TypeSize offsets.
///
/// \param DL Data layout used to determine type sizes and offsets.
/// \param Ty LLVM IR type to decompose.
/// \param ValueLLTs Filled with the LLTs of the non-aggregate parts of \p Ty.
/// \param FixedOffsets Vector filled with the fixed in-memory offset of each
///        value.
/// \param FixedStartingOffset Base offset applied to each recorded offset.
LLVM_ABI void computeValueLLTs(const DataLayout &DL, Type &Ty,
                               SmallVectorImpl<LLT> &ValueLLTs,
                               SmallVectorImpl<uint64_t> *FixedOffsets,
                               uint64_t FixedStartingOffset = 0);

/// ExtractTypeInfo - Returns the type info, possibly bitcast, encoded in V.
///
/// \param V Value that encodes a type-info global, possibly via a bitcast.
///
/// \returns The type-info global encoded in \p V.
LLVM_ABI GlobalValue *ExtractTypeInfo(Value *V);

/// Return the ISD condition code for an LLVM IR floating-point predicate.
///
/// This includes consideration of global floating-point math flags.
///
/// \param Pred LLVM IR floating-point comparison predicate.
///
/// \returns The ISD condition code for \p Pred.
LLVM_ABI ISD::CondCode getFCmpCondCode(FCmpInst::Predicate Pred);

/// getFCmpCodeWithoutNaN - Given an ISD condition code comparing floats,
/// return the equivalent code if we're allowed to assume that NaNs won't occur.
///
/// \param CC ISD floating-point condition code to rewrite.
///
/// \returns The NaN-free equivalent of \p CC.
LLVM_ABI ISD::CondCode getFCmpCodeWithoutNaN(ISD::CondCode CC);

/// getICmpCondCode - Return the ISD condition code corresponding to
/// the given LLVM IR integer condition code.
///
/// \param Pred LLVM IR integer comparison predicate.
///
/// \returns The ISD condition code for \p Pred.
LLVM_ABI ISD::CondCode getICmpCondCode(ICmpInst::Predicate Pred);

/// getICmpCondCode - Return the LLVM IR integer condition code
/// corresponding to the given ISD integer condition code.
///
/// \param Pred ISD integer condition code to convert.
///
/// \returns The LLVM IR integer comparison predicate for \p Pred.
LLVM_ABI ICmpInst::Predicate getICmpCondCode(ISD::CondCode Pred);

/// Test whether an instruction is in a position for tail-call optimization.
///
/// This roughly means that it's in a block with a return and there's nothing
/// that needs to be scheduled between it and the return.
///
/// This function only tests target-independent requirements.
///
/// \param Call Call instruction to test.
/// \param TM Target machine providing target-specific constraints.
/// \param ReturnsFirstArg Whether the caller returns the call's first
///        argument.
///
/// \returns True if \p Call is in a tail-call position.
LLVM_ABI bool isInTailCallPosition(const CallBase &Call,
                                   const TargetMachine &TM,
                                   bool ReturnsFirstArg = false);

/// Check whether caller/callee attribute mismatches inhibit a tail call.
///
/// Assumes the input instruction is already in the tail call position.
/// \p AllowDifferingSizes is an output parameter which, if forming a tail call
/// is permitted, determines whether it's permitted only if the size of the
/// caller's and callee's return types match exactly.
///
/// \param F Caller function.
/// \param I Call instruction in the tail-call position.
/// \param Ret Return instruction that follows the call.
/// \param TLI Target lowering info used for attribute checks.
/// \param AllowDifferingSizes Optional output; if non-null and a tail call is
///        permitted, set to whether differing return sizes are allowed.
///
/// \returns True if attributes permit forming a tail call.
LLVM_ABI bool attributesPermitTailCall(const Function *F, const Instruction *I,
                                       const ReturnInst *Ret,
                                       const TargetLoweringBase &TLI,
                                       bool *AllowDifferingSizes = nullptr);

/// Test if given that the input instruction is in the tail call position if the
/// return type or any attributes of the function will inhibit tail call
/// optimization.
///
/// \param F Caller function.
/// \param I Call instruction in the tail-call position.
/// \param Ret Return instruction that follows the call.
/// \param TLI Target lowering info used for eligibility checks.
/// \param ReturnsFirstArg Whether the caller returns the call's first
///        argument.
///
/// \returns True if the return type and attributes allow a tail call.
LLVM_ABI bool returnTypeIsEligibleForTailCall(const Function *F,
                                              const Instruction *I,
                                              const ReturnInst *Ret,
                                              const TargetLoweringBase &TLI,
                                              bool ReturnsFirstArg = false);

/// Returns true if the parent of \p CI returns CI's first argument after
/// calling \p CI.
///
/// \param CI Call whose first argument may be returned by the caller.
///
/// \returns True if the caller returns \p CI's first argument.
LLVM_ABI bool funcReturnsFirstArgOfCall(const CallInst &CI);

/// Compute EH scope membership for each basic block in a machine function.
///
/// \param MF Machine function whose EH scopes are analyzed.
///
/// \returns A map from each basic block to the number of its enclosing EH
///          scope entry block.
LLVM_ABI DenseMap<const MachineBasicBlock *, int>
getEHScopeMembership(const MachineFunction &MF);

} // End llvm namespace

#endif
