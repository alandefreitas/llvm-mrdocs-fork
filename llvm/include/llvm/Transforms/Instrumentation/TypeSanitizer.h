//===- Transforms/Instrumentation/TypeSanitizer.h - TySan Pass -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the type sanitizer pass.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_TYPESANITIZER_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_TYPESANITIZER_H

#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class Function;
class FunctionPass;
class Module;

/// A module pass for tysan instrumentation.
///
/// Instruments the code in a module to find type-based aliasing violations.
/// This pass inserts calls to runtime library functions. If the functions
/// aren't declared yet, the pass inserts the declarations.
struct TypeSanitizerPass : public RequiredPassInfoMixin<TypeSanitizerPass> {
  /// Run TypeSanitizer instrumentation over the module.
  /// @param M Module to instrument.
  /// @param AM Module analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // namespace llvm

#endif /* LLVM_TRANSFORMS_INSTRUMENTATION_TYPESANITIZER_H */
