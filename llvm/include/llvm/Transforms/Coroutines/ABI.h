//===- ABI.h - Coroutine lowering class definitions (ABIs) ----*- C++ -*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This file defines coroutine lowering classes. The interface for coroutine
// lowering is defined by BaseABI. Each lowering method (ABI) implements the
// interface. Note that the enum class ABI, such as ABI::Switch, determines
// which ABI class, such as SwitchABI, is used to lower the coroutine. Both the
// ABI enum and ABI class are used by the Coroutine passes when lowering.
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_COROUTINES_ABI_H
#define LLVM_TRANSFORMS_COROUTINES_ABI_H

#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Transforms/Coroutines/CoroShape.h"
#include "llvm/Transforms/Coroutines/MaterializationUtils.h"
#include "llvm/Transforms/Coroutines/SuspendCrossingInfo.h"

namespace llvm {

class Function;

/// Coroutine ABI lowering utilities and class hierarchy.
///
/// Provides an object-oriented interface for ABI operations, intended to
/// replace use of the ABI enum alone. The common ABIs (Switch, Async,
/// Retcon{Once}) can be customized by inheriting and overriding methods.
/// Custom ABIs are selected via the \c coro.begin.custom.abi intrinsic, which
/// takes an additional i32 index into a SmallVector of ABI generators passed
/// to CoroSplitPass.
namespace coro {

/// Base class for coroutine ABI lowering implementations.
///
/// Provides a virtual interface for transforming coroutines according to
/// different calling conventions (ABIs). Subclasses implement specific
/// lowering strategies.
class LLVM_ABI BaseABI {
public:
  /// Construct a base ABI lowering for coroutine function \p F.
  ///
  /// \param F The coroutine function to lower.
  /// \param S Shape describing the coroutine's structure and intrinsics.
  /// \param IsMaterializable Callback that returns true for instructions that
  ///        may be rematerialized instead of spilled into the frame.
  BaseABI(Function &F, coro::Shape &S,
          std::function<bool(Instruction &)> IsMaterializable)
      : F(F), Shape(S), IsMaterializable(std::move(IsMaterializable)) {}
  /// Destroy the ABI lowering object.
  virtual ~BaseABI() = default;

  /// Initialize ABI-specific state before frame building and splitting.
  virtual void init() = 0;

  /// Allocate the coroutine frame and insert spill/reload code as needed.
  ///
  /// \param OptimizeFrame Whether to apply frame layout optimizations.
  virtual void buildCoroutineFrame(bool OptimizeFrame);

  /// Split the coroutine into ramp and resume/destroy clones per this ABI.
  ///
  /// \param F The coroutine function to split.
  /// \param Shape Shape describing the coroutine's structure.
  /// \param Clones Output vector filled with generated clone functions.
  /// \param TTI Target transform info for cost and materialization decisions.
  virtual void splitCoroutine(Function &F, coro::Shape &Shape,
                              SmallVectorImpl<Function *> &Clones,
                              TargetTransformInfo &TTI) = 0;

  /// The coroutine function being lowered.
  Function &F;
  /// Structural shape of the coroutine (intrinsics, frame layout, ABI kind).
  coro::Shape &Shape;

  /// Predicate identifying instructions eligible for rematerialization.
  ///
  /// Used by \c buildCoroutineFrame when calling \c coro::doMaterializations.
  std::function<bool(Instruction &I)> IsMaterializable;
};

/// Resume-switch coroutine lowering with shared resume/destroy functions.
class LLVM_ABI SwitchABI : public BaseABI {
public:
  /// Construct a switch-ABI lowering for coroutine function \p F.
  ///
  /// \param F The coroutine function to lower.
  /// \param S Shape describing the coroutine's structure and intrinsics.
  /// \param IsMaterializable Callback that returns true for instructions that
  ///        may be rematerialized instead of spilled into the frame.
  SwitchABI(Function &F, coro::Shape &S,
            std::function<bool(Instruction &)> IsMaterializable)
      : BaseABI(F, S, std::move(IsMaterializable)) {}

  /// Initialize switch-ABI-specific state before frame building and splitting.
  void init() override;

  /// Split the coroutine into shared resume and destroy functions.
  ///
  /// \param F The coroutine function to split.
  /// \param Shape Shape describing the coroutine's structure.
  /// \param Clones Output vector filled with generated clone functions.
  /// \param TTI Target transform info for cost and materialization decisions.
  void splitCoroutine(Function &F, coro::Shape &Shape,
                      SmallVectorImpl<Function *> &Clones,
                      TargetTransformInfo &TTI) override;
};

/// Async-continuation coroutine lowering.
///
/// Each suspend point creates a continuation function available via an
/// intrinsic.
class LLVM_ABI AsyncABI : public BaseABI {
public:
  /// Construct an async-ABI lowering for coroutine function \p F.
  ///
  /// \param F The coroutine function to lower.
  /// \param S Shape describing the coroutine's structure and intrinsics.
  /// \param IsMaterializable Callback that returns true for instructions that
  ///        may be rematerialized instead of spilled into the frame.
  AsyncABI(Function &F, coro::Shape &S,
           std::function<bool(Instruction &)> IsMaterializable)
      : BaseABI(F, S, std::move(IsMaterializable)) {}

  /// Initialize async-ABI-specific state before frame building and splitting.
  void init() override;

  /// Split the coroutine into async continuation functions.
  ///
  /// \param F The coroutine function to split.
  /// \param Shape Shape describing the coroutine's structure.
  /// \param Clones Output vector filled with generated clone functions.
  /// \param TTI Target transform info for cost and materialization decisions.
  void splitCoroutine(Function &F, coro::Shape &Shape,
                      SmallVectorImpl<Function *> &Clones,
                      TargetTransformInfo &TTI) override;
};

/// Returned-continuation coroutine lowering base class.
class LLVM_ABI AnyRetconABI : public BaseABI {
public:
  /// Construct a returned-continuation ABI lowering for coroutine function \p F.
  ///
  /// \param F The coroutine function to lower.
  /// \param S Shape describing the coroutine's structure and intrinsics.
  /// \param IsMaterializable Callback that returns true for instructions that
  ///        may be rematerialized instead of spilled into the frame.
  AnyRetconABI(Function &F, coro::Shape &S,
               std::function<bool(Instruction &)> IsMaterializable)
      : BaseABI(F, S, std::move(IsMaterializable)) {}

  /// Initialize retcon-ABI-specific state before frame building and splitting.
  void init() override;

  /// Split the coroutine into returned-continuation functions.
  ///
  /// \param F The coroutine function to split.
  /// \param Shape Shape describing the coroutine's structure.
  /// \param Clones Output vector filled with generated clone functions.
  /// \param TTI Target transform info for cost and materialization decisions.
  void splitCoroutine(Function &F, coro::Shape &Shape,
                      SmallVectorImpl<Function *> &Clones,
                      TargetTransformInfo &TTI) override;
};

} // end namespace coro

} // end namespace llvm

#endif // LLVM_TRANSFORMS_COROUTINES_ABI_H
