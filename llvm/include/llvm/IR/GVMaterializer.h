//===- GVMaterializer.h - Interface for GV materializers --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides an abstract interface for loading a module from some
// place.  This interface allows incremental or random access loading of
// functions from the file.  This is useful for applications like JIT compilers
// or interprocedural optimizers that do not need the entire program in memory
// at the same time.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_GVMATERIALIZER_H
#define LLVM_IR_GVMATERIALIZER_H

#include "llvm/Support/Compiler.h"
#include <vector>

namespace llvm {

class Error;
class GlobalValue;
class StructType;

/// Abstract interface for lazily loading global values from an external source.
///
/// Implementations support incremental or random-access loading of functions,
/// which is useful for JIT compilers and interprocedural optimizers that do not
/// need the entire program in memory at once.
class LLVM_ABI GVMaterializer {
protected:
  /// Default-construct an abstract materializer.
  GVMaterializer() = default;

public:
  /// Destroy the materializer.
  virtual ~GVMaterializer();

  /// Make sure the given GlobalValue is fully read.
  ///
  /// \param GV Global value to materialize.
  /// \return Success, or an error if materialization fails.
  virtual Error materialize(GlobalValue *GV) = 0;

  /// Make sure the entire Module has been completely read.
  ///
  /// \return Success, or an error if materialization fails.
  virtual Error materializeModule() = 0;

  /// Make sure module-level metadata has been completely read.
  /// \return Success, or an error if materialization fails.
  virtual Error materializeMetadata() = 0;

  /// Request that debug info be stripped during materialization.
  virtual void setStripDebugInfo() = 0;

  /// Return the identified (non-literal) struct types known to this
  /// materializer.
  /// \return The identified struct types known to this materializer.
  virtual std::vector<StructType *> getIdentifiedStructTypes() const = 0;
};

} // end namespace llvm

#endif // LLVM_IR_GVMATERIALIZER_H
