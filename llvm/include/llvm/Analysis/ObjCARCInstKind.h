//===- ObjCARCInstKind.h - ARC instruction equivalence classes --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_OBJCARCINSTKIND_H
#define LLVM_ANALYSIS_OBJCARCINSTKIND_H

#include "llvm/IR/Instructions.h"

namespace llvm {
namespace objcarc {

/// \enum ARCInstKind
///
/// Equivalence classes of instructions in the ARC Model.
///
/// Since we do not have "instructions" to represent ARC concepts in LLVM IR,
/// we instead operate on equivalence classes of instructions.
///
/// TODO: This should be split into two enums: a runtime entry point enum
/// (possibly united with the ARCRuntimeEntrypoint class) and an enum that deals
/// with effects of instructions in the ARC model (which would handle the notion
/// of a User or CallOrUser).
enum class ARCInstKind {
  Retain,                   ///< objc_retain
  RetainRV,                 ///< objc_retainAutoreleasedReturnValue
  UnsafeClaimRV,            ///< objc_unsafeClaimAutoreleasedReturnValue
  RetainBlock,              ///< objc_retainBlock
  Release,                  ///< objc_release
  Autorelease,              ///< objc_autorelease
  AutoreleaseRV,            ///< objc_autoreleaseReturnValue
  AutoreleasepoolPush,      ///< objc_autoreleasePoolPush
  AutoreleasepoolPop,       ///< objc_autoreleasePoolPop
  NoopCast,                 ///< objc_retainedObject, etc.
  FusedRetainAutorelease,   ///< objc_retainAutorelease
  FusedRetainAutoreleaseRV, ///< objc_retainAutoreleaseReturnValue
  LoadWeakRetained,         ///< objc_loadWeakRetained (primitive)
  StoreWeak,                ///< objc_storeWeak (primitive)
  InitWeak,                 ///< objc_initWeak (derived)
  LoadWeak,                 ///< objc_loadWeak (derived)
  MoveWeak,                 ///< objc_moveWeak (derived)
  CopyWeak,                 ///< objc_copyWeak (derived)
  DestroyWeak,              ///< objc_destroyWeak (derived)
  StoreStrong,              ///< objc_storeStrong (derived)
  IntrinsicUser,            ///< llvm.objc.clang.arc.use
  CallOrUser,               ///< could call objc_release and/or "use" pointers
  Call,                     ///< could call objc_release
  User,                     ///< could "use" a pointer
  None                      ///< anything that is inert from an ARC perspective.
};

/// Print the ARCInstKind \p Class to stream \p OS.
/// @param OS Stream to write the ARCInstKind name to.
/// @param Class ARC instruction kind to print.
/// @return The stream \p OS after writing.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const ARCInstKind Class);

/// Test if the given class is a kind of user.
/// @param Class ARC instruction kind to test.
/// @return True if \p Class is a kind of user.
LLVM_ABI bool IsUser(ARCInstKind Class);

/// Test if the given class is objc_retain or equivalent.
/// @param Class ARC instruction kind to test.
/// @return True if \p Class is objc_retain or equivalent.
LLVM_ABI bool IsRetain(ARCInstKind Class);

/// Test if the given class is objc_autorelease or equivalent.
/// @param Class ARC instruction kind to test.
/// @return True if \p Class is objc_autorelease or equivalent.
LLVM_ABI bool IsAutorelease(ARCInstKind Class);

/// Test if the given class represents instructions which return their
/// argument verbatim.
/// @param Class ARC instruction kind to test.
/// @return True if \p Class represents forwarding instructions.
LLVM_ABI bool IsForwarding(ARCInstKind Class);

/// Test if the given class represents instructions which do nothing if
/// passed a null pointer.
/// @param Class ARC instruction kind to test.
/// @return True if \p Class is a no-op when passed a null pointer.
LLVM_ABI bool IsNoopOnNull(ARCInstKind Class);

/// Test if the given class represents instructions which do nothing if
/// passed a global variable.
/// @param Class ARC instruction kind to test.
/// @return True if \p Class is a no-op when passed a global variable.
LLVM_ABI bool IsNoopOnGlobal(ARCInstKind Class);

/// Test if the given class represents instructions which are always safe
/// to mark with the "tail" keyword.
/// @param Class ARC instruction kind to test.
/// @return True if \p Class is always safe to mark tail.
LLVM_ABI bool IsAlwaysTail(ARCInstKind Class);

/// Test if the given class represents instructions which are never safe
/// to mark with the "tail" keyword.
/// @param Class ARC instruction kind to test.
/// @return True if \p Class is never safe to mark tail.
LLVM_ABI bool IsNeverTail(ARCInstKind Class);

/// Test if the given class represents instructions which are always safe
/// to mark with the nounwind attribute.
/// @param Class ARC instruction kind to test.
/// @return True if \p Class is always safe to mark nounwind.
LLVM_ABI bool IsNoThrow(ARCInstKind Class);

/// Test whether the given instruction can autorelease any pointer or cause an
/// autoreleasepool pop.
/// @param Class ARC instruction kind to test.
/// @return True if \p Class can interrupt a retainAutoreleasedReturnValue.
LLVM_ABI bool CanInterruptRV(ARCInstKind Class);

/// Determine if F is one of the special known Functions.  If it isn't,
/// return ARCInstKind::CallOrUser.
/// @param F Function to classify as an ARC instruction kind.
/// @return The ARCInstKind for \p F, or ARCInstKind::CallOrUser if unknown.
LLVM_ABI ARCInstKind GetFunctionClass(const Function *F);

/// Determine which objc runtime call instruction class V belongs to.
///
/// This is similar to GetARCInstKind except that it only detects objc
/// runtime calls. This allows it to be faster.
/// @param V Value to classify as a basic ARC instruction kind.
/// @return The basic ARCInstKind classification for \p V.
inline ARCInstKind GetBasicARCInstKind(const Value *V) {
  if (const CallInst *CI = dyn_cast<CallInst>(V)) {
    if (const Function *F = CI->getCalledFunction())
      return GetFunctionClass(F);
    // Otherwise, be conservative.
    return ARCInstKind::CallOrUser;
  }

  // Otherwise, be conservative.
  return isa<InvokeInst>(V) ? ARCInstKind::CallOrUser : ARCInstKind::User;
}

/// Map V to its ARCInstKind equivalence class.
/// @param V Value to map to an ARCInstKind equivalence class.
/// @return The ARCInstKind equivalence class for \p V.
LLVM_ABI ARCInstKind GetARCInstKind(const Value *V);

/// Returns false if conservatively we can prove that any instruction mapped to
/// this kind can not decrement ref counts. Returns true otherwise.
/// @param Kind ARC instruction kind to test for ref-count decrements.
/// @return False if no instruction of \p Kind can decrement ref counts; true otherwise.
LLVM_ABI bool CanDecrementRefCount(ARCInstKind Kind);

} // end namespace objcarc
} // end namespace llvm

#endif
