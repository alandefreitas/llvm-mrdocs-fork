//===- ObjectTransformLayer.h - Run all objects through functor -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Run all objects passed in through a user supplied functor.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_OBJECTTRANSFORMLAYER_H
#define LLVM_EXECUTIONENGINE_ORC_OBJECTTRANSFORMLAYER_H

#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/Layer.h"
#include "llvm/Support/Compiler.h"
#include <algorithm>
#include <memory>

namespace llvm {
namespace orc {

/// A layer that applies a transform to emitted object files.
class LLVM_ABI ObjectTransformLayer
    : public RTTIExtends<ObjectTransformLayer, ObjectLayer> {
public:
  /// RTTI identifier for this ObjectTransformLayer type.
  static char ID;

  /// Functor that transforms an object buffer before it is emitted.
  using TransformFunction =
      std::function<Expected<std::unique_ptr<MemoryBuffer>>(
          std::unique_ptr<MemoryBuffer>)>;

  /// Construct an ObjectTransformLayer that transforms objects before emitting
  /// them to a base layer.
  /// \param ES Execution session for this layer.
  /// \param BaseLayer Object layer to emit transformed objects into.
  /// \param Transform Transform applied to each object; defaults to null
  ///        (pass-through).
  ObjectTransformLayer(ExecutionSession &ES, ObjectLayer &BaseLayer,
                       TransformFunction Transform = TransformFunction());

  /// Apply the configured transform and emit the result to the base layer.
  /// \param R Materialization responsibility for the definitions being emitted.
  /// \param O Object buffer to transform and emit.
  void emit(std::unique_ptr<MaterializationResponsibility> R,
            std::unique_ptr<MemoryBuffer> O) override;

  /// Replace the transform applied to objects before emission.
  /// \param Transform New transform function to use.
  void setTransform(TransformFunction Transform) {
    this->Transform = std::move(Transform);
  }

private:
  ObjectLayer &BaseLayer;
  TransformFunction Transform;
};

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_OBJECTTRANSFORMLAYER_H
