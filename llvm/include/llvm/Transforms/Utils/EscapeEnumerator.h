//===-- EscapeEnumerator.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines a helper class that enumerates all possible exits from a function,
// including exception handling.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_ESCAPEENUMERATOR_H
#define LLVM_TRANSFORMS_UTILS_ESCAPEENUMERATOR_H

#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"

namespace llvm {

class DomTreeUpdater;

/// Find all escape points from a function for finally-style insertion.
///
/// In addition to finding the existing return and unwind instructions, it also
/// (if necessary) transforms any call instructions into invokes and sends them
/// to a landing pad.
class EscapeEnumerator {
  Function &F;
  const char *CleanupBBName;

  Function::iterator StateBB, StateE;
  IRBuilder<> Builder;
  bool Done = false;
  bool HandleExceptions;

  DomTreeUpdater *DTU;

public:
  /// Construct an escape enumerator for a function.
  ///
  /// \param F Function whose escape points are enumerated.
  /// \param N Name for the cleanup basic block created for exception handling.
  /// \param HandleExceptions Whether to transform throwing calls into invokes.
  /// \param DTU Optional dominator tree updater for IR transforms.
  EscapeEnumerator(Function &F, const char *N = "cleanup",
                   bool HandleExceptions = true, DomTreeUpdater *DTU = nullptr)
      : F(F), CleanupBBName(N), StateBB(F.begin()), StateE(F.end()),
        Builder(F.getContext()), HandleExceptions(HandleExceptions), DTU(DTU) {}

  /// Get an IR builder at the next escape point.
  ///
  /// \return An IR builder positioned at the next escape point, or null when
  /// exhausted.
  LLVM_ABI IRBuilder<> *Next();
};

}

#endif // LLVM_TRANSFORMS_UTILS_ESCAPEENUMERATOR_H
