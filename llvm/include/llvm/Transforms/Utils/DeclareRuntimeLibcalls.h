//===- DeclareRuntimeLibcalls.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_DECLARERUNTIMELIBCALLS_H
#define LLVM_TRANSFORMS_UTILS_DECLARERUNTIMELIBCALLS_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// Pass that inserts declarations for all runtime library calls known for the
/// target.
class DeclareRuntimeLibcallsPass
    : public OptionalPassInfoMixin<DeclareRuntimeLibcallsPass> {
public:
  /// Run the declare-runtime-libcalls pass over the module.
  /// @param M Module into which runtime libcall declarations are inserted.
  /// @param MAM Module analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_DECLARERUNTIMELIBCALLS_H
