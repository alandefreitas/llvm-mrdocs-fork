//===- IRTransformLayer.h - Run all IR through a functor --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Run all IR passed in through a user supplied functor.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_IRTRANSFORMLAYER_H
#define LLVM_EXECUTIONENGINE_ORC_IRTRANSFORMLAYER_H

#include "llvm/ADT/FunctionExtras.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/Layer.h"
#include "llvm/Support/Compiler.h"
#include <memory>

namespace llvm {
namespace orc {

/// A layer that applies a transform to emitted modules.
/// The transform function is responsible for locking the ThreadSafeContext
/// before operating on the module.
class LLVM_ABI IRTransformLayer : public IRLayer {
public:
  /// Functor that transforms a thread-safe module before it is emitted.
  using TransformFunction = unique_function<Expected<ThreadSafeModule>(
      ThreadSafeModule, MaterializationResponsibility &R)>;

  /// Construct an IRTransformLayer that transforms modules before emitting
  /// them to a base layer.
  /// \param ES Execution session for this layer.
  /// \param BaseLayer IR layer to emit transformed modules into.
  /// \param Transform Transform applied to each module; defaults to
  ///        identityTransform.
  IRTransformLayer(ExecutionSession &ES, IRLayer &BaseLayer,
                   TransformFunction Transform = identityTransform);

  /// Replace the transform applied to modules before emission.
  /// \param Transform New transform function to use.
  void setTransform(TransformFunction Transform) {
    this->Transform = std::move(Transform);
  }

  /// Apply the configured transform and emit the result to the base layer.
  /// \param R Materialization responsibility for the definitions being emitted.
  /// \param TSM Thread-safe module to transform and emit.
  void emit(std::unique_ptr<MaterializationResponsibility> R,
            ThreadSafeModule TSM) override;

  /// Return the given module unchanged.
  /// \param TSM Thread-safe module to pass through.
  /// \param R Materialization responsibility for the definitions being emitted.
  /// \return The given thread-safe module, unmodified.
  static ThreadSafeModule identityTransform(ThreadSafeModule TSM,
                                            MaterializationResponsibility &R) {
    return TSM;
  }

private:
  IRLayer &BaseLayer;
  TransformFunction Transform;
};

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_IRTRANSFORMLAYER_H
