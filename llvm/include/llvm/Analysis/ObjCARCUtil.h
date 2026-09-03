//===- ObjCARCUtil.h - ObjC ARC Utility Functions ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file defines ARC utility functions which are used by various parts of
/// the compiler.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_OBJCARCUTIL_H
#define LLVM_ANALYSIS_OBJCARCUTIL_H

#include "llvm/Analysis/ObjCARCInstKind.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/LLVMContext.h"

namespace llvm {
namespace objcarc {

/// Return the module flag name for the retainAutoreleasedReturnValue marker.
/// @return The module flag name string for the retainRV marker.
inline const char *getRVMarkerModuleFlagStr() {
  return "clang.arc.retainAutoreleasedReturnValueMarker";
}

/// Return true if \p CB has a clang_arc_attachedcall operand bundle.
///
/// Ignores the bundle when the call's return type is void, which can happen
/// after global optimization turns the callee's return type to void.
/// @param CB Call to inspect for the attached-call operand bundle.
/// @return True if \p CB has a non-void clang_arc_attachedcall bundle.
inline bool hasAttachedCallOpBundle(const CallBase *CB) {
  // Ignore the bundle if the return type is void. Global optimization passes
  // can turn the called function's return type to void. That should happen only
  // if the call doesn't return and the call to @llvm.objc.clang.arc.noop.use
  // no longer consumes the function return or is deleted. In that case, it's
  // not necessary to emit the marker instruction or calls to the ARC runtime
  // functions.
  return !CB->getFunctionType()->getReturnType()->isVoidTy() &&
         CB->getOperandBundle(LLVMContext::OB_clang_arc_attachedcall)
             .has_value();
}

/// Return the ARC runtime function from a clang_arc_attachedcall bundle.
///
/// This is the address of the ARC runtime function passed as the operand
/// bundle's argument.
/// @param CB Call whose attached-call operand bundle is queried.
/// @return The attached ARC runtime function, or std::nullopt if none.
inline std::optional<Function *> getAttachedARCFunction(const CallBase *CB) {
  auto B = CB->getOperandBundle(LLVMContext::OB_clang_arc_attachedcall);
  if (!B)
    return std::nullopt;

  return cast<Function>(B->Inputs[0]);
}

/// Return whether clang_arc_attachedcall should be emitted with a marker.
///
/// Concretely, this is the difference between:
///   objc_retainAutoreleasedReturnValue
/// and
///  objc_claimAutoreleasedReturnValue
/// retainRV (and unsafeClaimRV) requires a marker, but claimRV does not.
/// @param CB Call whose attached ARC function determines the marker need.
/// @return True if the attached call needs a retainRV marker.
inline bool attachedCallOpBundleNeedsMarker(const CallBase *CB) {
  // FIXME: do this on ARCRuntimeEntryPoints, and do the todo above ARCInstKind
  if (std::optional<Function *> Fn = getAttachedARCFunction(CB))
    if ((*Fn)->getName() == "objc_claimAutoreleasedReturnValue")
      return false;
  return true;
}

/// Check whether the function is retainRV/unsafeClaimRV.
/// @param Kind ARC instruction kind to classify.
/// @return True if \p Kind is RetainRV or UnsafeClaimRV.
inline bool isRetainOrClaimRV(ARCInstKind Kind) {
  return Kind == ARCInstKind::RetainRV || Kind == ARCInstKind::UnsafeClaimRV;
}

/// Return the ARCInstKind of the function on clang_arc_attachedcall.
///
/// Returns ARCInstKind::None if the call doesn't have the operand bundle or
/// the operand is null. Otherwise it returns either RetainRV or UnsafeClaimRV.
/// @param CB Call whose attached ARC function kind is queried.
/// @return The ARCInstKind of the attached ARC function, or ARCInstKind::None.
inline ARCInstKind getAttachedARCFunctionKind(const CallBase *CB) {
  std::optional<Function *> Fn = getAttachedARCFunction(CB);
  if (!Fn)
    return ARCInstKind::None;
  auto FnClass = GetFunctionClass(*Fn);
  assert(isRetainOrClaimRV(FnClass) && "unexpected ARC runtime function");
  return FnClass;
}

} // end namespace objcarc
} // end namespace llvm

#endif
