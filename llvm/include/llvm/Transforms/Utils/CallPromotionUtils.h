//===- CallPromotionUtils.h - Utilities for call promotion ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares utilities useful for promoting indirect call sites to
// direct call sites.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_CALLPROMOTIONUTILS_H
#define LLVM_TRANSFORMS_UTILS_CALLPROMOTIONUTILS_H

#include "llvm/Analysis/CtxProfAnalysis.h"
#include "llvm/Support/Compiler.h"
namespace llvm {
template <typename T> class ArrayRef;
class Constant;
class CallBase;
class CastInst;
class Function;
class Instruction;
class MDNode;
class Value;

/// Return true if the given indirect call site can be made to call \p Callee.
///
/// This function ensures that the number and type of the call site's arguments
/// and return value match those of the given function. If the types do not
/// match exactly, they must at least be bitcast compatible. If \p FailureReason
/// is non-null and the indirect call cannot be promoted, the failure reason
/// will be stored in it.
///
/// \param CB Indirect call site to check for promotion eligibility.
/// \param Callee Function that would become the direct callee.
/// \param FailureReason Optional out-parameter that receives a failure reason.
/// \return True if \p CB can be promoted to call \p Callee.
LLVM_ABI bool isLegalToPromote(const CallBase &CB, Function *Callee,
                               const char **FailureReason = nullptr);

/// Promote the given indirect call site to unconditionally call \p Callee.
///
/// This function promotes the given call site, returning the direct call or
/// invoke instruction. If the function type of the call site doesn't match that
/// of the callee, bitcast instructions are inserted where appropriate. If \p
/// RetBitCast is non-null, it will be used to store the return value bitcast,
/// if created.
///
/// \param CB Indirect call site to promote.
/// \param Callee Function to call directly after promotion.
/// \param RetBitCast Optional out-parameter for a created return-value bitcast.
/// \return The promoted direct call or invoke instruction.
LLVM_ABI CallBase &promoteCall(CallBase &CB, Function *Callee,
                               CastInst **RetBitCast = nullptr);

/// Promote the given indirect call site to conditionally call \p Callee. The
/// promoted direct call instruction is predicated on `CB.getCalledOperand() ==
/// Callee`.
///
/// This function creates an if-then-else structure at the location of the call
/// site. The original call site is moved into the "else" block. A clone of the
/// indirect call site is promoted, placed in the "then" block, and returned. If
/// \p BranchWeights is non-null, it will be used to set !prof metadata on the
/// new conditional branch.
///
/// \param CB Indirect call site to promote conditionally.
/// \param Callee Function to call on the promoted path.
/// \param BranchWeights Optional !prof metadata for the new conditional branch.
/// \return The promoted direct call placed in the "then" block.
LLVM_ABI CallBase &promoteCallWithIfThenElse(CallBase &CB, Function *Callee,
                                             MDNode *BranchWeights = nullptr);

/// Promote an indirect call conditionally while updating contextual profiles.
///
/// Like the overload that takes branch weights, this creates an if-then-else
/// structure, promotes a clone of \p CB to call \p Callee directly on the then
/// path, and leaves the original call as the fallback. It also updates
/// \p CtxProf with instrumentation and counter metadata for the transformed
/// call sites and basic blocks. Returns the promoted direct call, or nullptr if
/// promotion is not possible given the contextual profile.
///
/// \param CB Indirect call site to promote conditionally.
/// \param Callee Function to call on the promoted path.
/// \param CtxProf Contextual profile used to validate and update instrumentation.
/// \return The promoted direct call, or nullptr if promotion is not possible.
LLVM_ABI CallBase *promoteCallWithIfThenElse(CallBase &CB, Function &Callee,
                                             PGOContextualProfile &CtxProf);

/// This is similar to `promoteCallWithIfThenElse` except that the condition to
/// promote a virtual call is that \p VPtr is the same as any of \p
/// AddressPoints.
///
/// This function is expected to be used on virtual calls (a subset of indirect
/// calls). \p VPtr is the virtual table address stored in the objects, and
/// \p AddressPoints contains vtable address points. A vtable address point is
/// a location inside the vtable that's referenced by vpointer in C++ objects.
///
/// TODO: sink the address-calculation instructions of indirect callee to the
/// indirect call fallback after transformation.
///
/// \param CB Virtual call site to promote conditionally.
/// \param VPtr Virtual table pointer compared against the address points.
/// \param Callee Function to call on the promoted path.
/// \param AddressPoints Vtable address points that identify the promoted type.
/// \param BranchWeights Optional !prof metadata for the new conditional branch.
/// \return The promoted direct call placed in the "then" block.
LLVM_ABI CallBase &promoteCallWithVTableCmp(CallBase &CB, Instruction *VPtr,
                                            Function *Callee,
                                            ArrayRef<Constant *> AddressPoints,
                                            MDNode *BranchWeights);

/// Try to promote (devirtualize) a virtual call on an Alloca. Return true on
/// success.
///
/// Look for a pattern like:
///
///  %o = alloca %class.Impl
///  %1 = getelementptr %class.Impl, %class.Impl* %o, i64 0, i32 0, i32 0
///  store i32 (...)** bitcast (i8** getelementptr inbounds
///      ({ [3 x i8*] }, { [3 x i8*] }* @_ZTV4Impl, i64 0, inrange i32 0, i64 2)
///      to i32 (...)**), i32 (...)*** %1
///  %2 = getelementptr inbounds %class.Impl, %class.Impl* %o, i64 0, i32 0
///  %3 = bitcast %class.Interface* %2 to void (%class.Interface*)***
///  %vtable.i = load void (%class.Interface*)**, void (%class.Interface*)*** %3
///  %4 = load void (%class.Interface*)*, void (%class.Interface*)** %vtable.i
///  call void %4(%class.Interface* nonnull %2)
///
/// @_ZTV4Impl = linkonce_odr dso_local unnamed_addr constant { [3 x i8*] }
///     { [3 x i8*]
///     [i8* null, i8* bitcast ({ i8*, i8*, i8* }* @_ZTI4Impl to i8*),
///     i8* bitcast (void (%class.Impl*)* @_ZN4Impl3RunEv to i8*)] }
///
/// \param CB Call site that may be a virtual call through an alloca.
/// \return True if the virtual call was successfully promoted.
LLVM_ABI bool tryPromoteCall(CallBase &CB);

/// Predicate and clone the given call site.
///
/// This function creates an if-then-else structure at the location of the
/// call site. The "if" condition compares the call site's called value to
/// the given callee. The original call site is moved into the "else" block,
/// and a clone of the call site is placed in the "then" block. The cloned
/// instruction is returned.
///
/// \param CB Call site to predicate and clone.
/// \param Callee Value compared against the call site's called operand.
/// \param BranchWeights Optional !prof metadata for the new conditional branch.
/// \return The cloned call site placed in the "then" block.
LLVM_ABI CallBase &versionCallSite(CallBase &CB, Value *Callee,
                                   MDNode *BranchWeights);

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_CALLPROMOTIONUTILS_H
