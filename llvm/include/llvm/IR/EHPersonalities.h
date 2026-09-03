//===- EHPersonalities.h - Compute EH-related information -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_EHPERSONALITIES_H
#define LLVM_IR_EHPERSONALITIES_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/TinyPtrVector.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class BasicBlock;
class Function;
class Triple;
class Value;

/// Recognized exception-handling personality kinds.
enum class EHPersonality {
  Unknown, ///< Personality function not recognized by LLVM.
  GNU_Ada, ///< The GNAT Ada personality (__gnat_eh_personality).
  GNU_C, ///< The GCC C personality (__gcc_personality_v0).
  GNU_C_SjLj, ///< The GCC C SjLj personality (__gcc_personality_sj0).
  GNU_CXX, ///< The GCC C++ personality (__gxx_personality_v0).
  GNU_CXX_SjLj, ///< The GCC C++ SjLj personality (__gxx_personality_sj0).
  GNU_ObjC, ///< The GCC Objective-C personality (__objc_personality_v0).
  MSVC_X86SEH, ///< MSVC x86 SEH (_except_handler3 / _except_handler4).
  MSVC_TableSEH, ///< MSVC table-based SEH (__C_specific_handler).
  MSVC_CXX, ///< MSVC C++ EH (__CxxFrameHandler3).
  CoreCLR, ///< The .NET CoreCLR personality (ProcessCLRException).
  Rust, ///< The Rust personality (mangled name ending in rust_eh_personality).
  Wasm_CXX, ///< The WebAssembly C++ personality (__gxx_wasm_personality_v0).
  XL_CXX, ///< The IBM XL C++ personality (__xlcxx_personality_v1).
  ZOS_CXX, ///< The z/OS C++ personality (__zos_cxx_personality_v2).
};

/// See if the given exception handling personality function is one
/// that we understand.  If so, return a description of it; otherwise return
/// Unknown.
/// \param Pers Value naming the personality function to classify.
/// \return The matching \c EHPersonality, or \c EHPersonality::Unknown if
/// unrecognized.
LLVM_ABI EHPersonality classifyEHPersonality(const Value *Pers);

/// Return the canonical name string for personality \p Pers.
/// \param Pers Personality kind whose runtime name is requested.
/// \return The canonical runtime name of personality \p Pers.
LLVM_ABI StringRef getEHPersonalityName(EHPersonality Pers);

/// Return the default exception-handling personality for target \p T.
/// \param T Target triple used to select the default personality.
/// \return The default EH personality for target \p T.
LLVM_ABI EHPersonality getDefaultEHPersonality(const Triple &T);

/// Returns true if this personality function catches asynchronous
/// exceptions.
/// \param Pers Personality kind to query.
/// \return True if \p Pers catches asynchronous exceptions.
inline bool isAsynchronousEHPersonality(EHPersonality Pers) {
  // The two SEH personality functions can catch asynch exceptions. We assume
  // unknown personalities don't catch asynch exceptions.
  switch (Pers) {
  case EHPersonality::MSVC_X86SEH:
  case EHPersonality::MSVC_TableSEH:
    return true;
  default:
    return false;
  }
  llvm_unreachable("invalid enum");
}

/// Returns true if this is a personality function that invokes
/// handler funclets (which must return to it).
/// \param Pers Personality kind to query.
/// \return True if \p Pers invokes handler funclets.
inline bool isFuncletEHPersonality(EHPersonality Pers) {
  switch (Pers) {
  case EHPersonality::MSVC_CXX:
  case EHPersonality::MSVC_X86SEH:
  case EHPersonality::MSVC_TableSEH:
  case EHPersonality::CoreCLR:
    return true;
  default:
    return false;
  }
  llvm_unreachable("invalid enum");
}

/// Returns true if this personality uses scope-style EH IR instructions:
/// catchswitch, catchpad/ret, and cleanuppad/ret.
/// \param Pers Personality kind to query.
/// \return True if \p Pers uses scope-style EH IR instructions.
inline bool isScopedEHPersonality(EHPersonality Pers) {
  switch (Pers) {
  case EHPersonality::MSVC_CXX:
  case EHPersonality::MSVC_X86SEH:
  case EHPersonality::MSVC_TableSEH:
  case EHPersonality::CoreCLR:
  case EHPersonality::Wasm_CXX:
    return true;
  default:
    return false;
  }
  llvm_unreachable("invalid enum");
}

/// Return true if this personality may be safely removed if there
/// are no invoke instructions remaining in the current function.
/// \param Pers Personality kind to query.
/// \return True if the personality may be safely removed when no invokes
/// remain.
inline bool isNoOpWithoutInvoke(EHPersonality Pers) {
  switch (Pers) {
  case EHPersonality::Unknown:
    return false;
  // All known personalities currently have this behavior
  default:
    return true;
  }
  llvm_unreachable("invalid enum");
}

/// Return true if invokes of nounwind callees in \p F may be simplified.
///
/// Invokes cannot be simplified when the personality catches asynchronous
/// exceptions, or when the module enables asynchronous EH.
/// \param F Function whose personality and module flags are inspected.
/// \return True if invokes of nounwind callees in \p F may be simplified.
LLVM_ABI bool canSimplifyInvokeNoUnwind(const Function *F);

/// Vector of basic blocks sharing an EH "color" (funclet membership).
typedef TinyPtrVector<BasicBlock *> ColorVector;

/// Recompute which basic blocks belong to which EH funclets in \p F.
///
/// If an EH funclet personality is in use (see isFuncletEHPersonality),
/// this will recompute which blocks are in which funclet. It is possible that
/// some blocks are in multiple funclets. Consider this analysis to be
/// expensive.
/// \param F Function whose blocks are colored by funclet membership.
/// \return Map from each basic block to the funclets (colors) that contain it.
LLVM_ABI DenseMap<BasicBlock *, ColorVector> colorEHFunclets(Function &F);

} // end namespace llvm

#endif // LLVM_IR_EHPERSONALITIES_H
