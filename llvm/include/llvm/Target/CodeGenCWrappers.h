//===- llvm/Target/CodeGenCWrappers.h - CodeGen C Wrappers ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines C bindings wrappers for enums in llvm/Support/CodeGen.h
// that need them.  The wrappers are separated to avoid adding an indirect
// dependency on llvm/Config/Targets.def to CodeGen.h.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGET_CODEGENCWRAPPERS_H
#define LLVM_TARGET_CODEGENCWRAPPERS_H

#include "llvm-c/TargetMachine.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/ErrorHandling.h"
#include <optional>

namespace llvm {

/// Convert an LLVM C API code model to its C++ equivalent.
///
/// Default and JIT-default models yield \c std::nullopt. When \p Model is
/// \c LLVMCodeModelJITDefault, \p JIT is set to \c true; otherwise it is set
/// to \c false.
///
/// \param Model The C API \c LLVMCodeModel value to unwrap.
/// \param JIT Set to \c true when \p Model requests the JIT default model.
/// \return The matching \c CodeModel::Model, or \c std::nullopt for defaults.
inline std::optional<CodeModel::Model> unwrap(LLVMCodeModel Model, bool &JIT) {
  JIT = false;
  switch (Model) {
  case LLVMCodeModelJITDefault:
    JIT = true;
    [[fallthrough]];
  case LLVMCodeModelDefault:
    return std::nullopt;
  case LLVMCodeModelTiny:
    return CodeModel::Tiny;
  case LLVMCodeModelSmall:
    return CodeModel::Small;
  case LLVMCodeModelKernel:
    return CodeModel::Kernel;
  case LLVMCodeModelMedium:
    return CodeModel::Medium;
  case LLVMCodeModelLarge:
    return CodeModel::Large;
  }
  return CodeModel::Small;
}

/// Convert a C++ code model to its LLVM C API equivalent.
///
/// \param Model The C++ \c CodeModel::Model value to wrap.
/// \return The corresponding \c LLVMCodeModel enumerator.
inline LLVMCodeModel wrap(CodeModel::Model Model) {
  switch (Model) {
  case CodeModel::Tiny:
    return LLVMCodeModelTiny;
  case CodeModel::Small:
    return LLVMCodeModelSmall;
  case CodeModel::Kernel:
    return LLVMCodeModelKernel;
  case CodeModel::Medium:
    return LLVMCodeModelMedium;
  case CodeModel::Large:
    return LLVMCodeModelLarge;
  }
  llvm_unreachable("Bad CodeModel!");
}
} // namespace llvm

#endif
