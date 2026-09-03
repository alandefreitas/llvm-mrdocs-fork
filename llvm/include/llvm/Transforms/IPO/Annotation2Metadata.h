//===- Annotation2Metadata.h - Add !annotation metadata. --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// New pass manager pass to convert @llvm.global.annotations to !annotation
// metadata.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_ANNOTATION2METADATA_H
#define LLVM_TRANSFORMS_IPO_ANNOTATION2METADATA_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Module;

/// Pass to convert @llvm.global.annotations to !annotation metadata.
struct Annotation2MetadataPass
    : public OptionalPassInfoMixin<Annotation2MetadataPass> {
  /// Convert @llvm.global.annotations in \p M into !annotation metadata.
  ///
  /// \param M The module whose annotations are converted.
  /// \param AM The module analysis manager.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_ANNOTATION2METADATA_H
