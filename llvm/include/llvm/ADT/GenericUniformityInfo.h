//===- GenericUniformityInfo.h ---------------------------*- C++ -*--------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_GENERICUNIFORMITYINFO_H
#define LLVM_ADT_GENERICUNIFORMITYINFO_H

#include "llvm/ADT/GenericCycleInfo.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

/// Target-specific transform cost model used by uniformity analysis. @seebelow
class TargetTransformInfo;

/// Internal implementation of generic uniformity analysis. @seebelow
template <typename ContextT> class GenericUniformityAnalysisImpl;
/// Deleter for \c GenericUniformityAnalysisImpl stored in a unique_ptr.
template <typename ImplT> struct GenericUniformityAnalysisImplDeleter {
  // Ugly hack around the fact that recent (> 15.0) clang will run into an
  // is_invocable() check in some GNU libc++'s unique_ptr implementation
  // and reject this deleter if you just make it callable with an ImplT *,
  // whether or not the type of ImplT is spelled out.
  /// Pointer type expected by unique_ptr's deleter interface.
  using pointer = ImplT *;
  /// Destroy and free the analysis implementation pointed to by \p Impl.
  void operator()(ImplT *Impl);
};

/// Computes which values and terminators are uniform vs divergent in a function.
///
/// Templated on an IR context that supplies block, value, and cycle types so
/// the same algorithm works for LLVM IR and other IRs.
template <typename ContextT> class GenericUniformityInfo {
public:
  /// Basic-block type from the IR context.
  using BlockT = typename ContextT::BlockT;
  /// Function type from the IR context.
  using FunctionT = typename ContextT::FunctionT;
  /// Mutable value reference type from the IR context.
  using ValueRefT = typename ContextT::ValueRefT;
  /// Const value reference type from the IR context.
  using ConstValueRefT = typename ContextT::ConstValueRefT;
  /// Use type from the IR context.
  using UseT = typename ContextT::UseT;
  /// Instruction type from the IR context.
  using InstructionT = typename ContextT::InstructionT;
  /// Dominator-tree type from the IR context.
  using DominatorTreeT = typename ContextT::DominatorTreeT;
  /// Alias for this specialization of GenericUniformityInfo.
  using ThisT = GenericUniformityInfo<ContextT>;

  /// Cycle-nesting analysis used while computing uniformity.
  using CycleInfoT = GenericCycleInfo<ContextT>;

  /// Tuple describing a temporally divergent value: value, use site, and cycle.
  using TemporalDivergenceTuple =
      std::tuple<ConstValueRefT, InstructionT *, CycleRef>;

  /// Construct analysis for the function dominated by \p DT with cycles \p CI.
  ///
  /// \param DT Dominator tree of the function.
  /// \param CI Cycle information for the function.
  /// \param TTI Optional target transform info for target-specific divergence.
  GenericUniformityInfo(const DominatorTreeT &DT, const CycleInfoT &CI,
                        const TargetTransformInfo *TTI = nullptr);
  /// Construct an empty analysis that must be assigned or moved into later.
  GenericUniformityInfo() = default;
  /// Move-construct, taking ownership of the analysis implementation.
  GenericUniformityInfo(GenericUniformityInfo &&) = default;
  /// Move-assign, taking ownership of the analysis implementation.
  GenericUniformityInfo &operator=(GenericUniformityInfo &&) = default;

  /// Run the uniformity analysis, filling divergence results.
  void compute() {
    DA->initialize();
    DA->compute();
  }

  /// The GPU kernel this analysis result is for
  const FunctionT &getFunction() const;

  /// The cycle info this analysis was computed with.
  const CycleInfoT &getCycleInfo() const;

  /// Whether \p V is divergent at its definition.
  bool isDivergentAtDef(ConstValueRefT V) const;

  /// Whether \p V is uniform/non-divergent at its definition.
  bool isUniformAtDef(ConstValueRefT V) const { return !isDivergentAtDef(V); }

  // Whether the terminator instruction \p I is uniform/divergent, i.e. whether
  // the controlling condition of a conditional branch or switch is
  // uniform/divergent.
  // TODO: The comment below is now out of date:
  // These accept a pointer argument so that
  // in LLVM IR, they overload the equivalent queries for Value*. For example,
  // if querying whether a CondBrInst is divergent, it should not be treated as
  // a Value in LLVM IR.
  /// Return true if terminator \p I has a uniform controlling condition.
  bool isUniformTerminator(const InstructionT *I) const {
    return !isDivergentTerminator(I);
  };
  /// Return true if terminator \p I has a divergent controlling condition.
  bool isDivergentTerminator(const InstructionT *I) const;

  /// \brief Whether \p U is divergent at its use. Uses of a uniform value can
  /// be divergent.
  bool isDivergentAtUse(const UseT &U) const;

  /// \brief Whether \p U is uniform/non-divergent at its use.
  bool isUniformAtUse(const UseT &U) const { return !isDivergentAtUse(U); }

  /// Return true if block \p B's terminator is divergent.
  bool hasDivergentTerminator(const BlockT &B);

  /// Call before erasing \p V, or a later instruction reusing its address
  /// may be misclassified as uniform.
  void forgetValue(ConstValueRefT V);

  /// Print a textual dump of divergence results to \p Out.
  void print(raw_ostream &Out) const;

  /// Return the list of temporally divergent (value, use, cycle) tuples.
  iterator_range<TemporalDivergenceTuple *> getTemporalDivergenceList() const;

private:
  using ImplT = GenericUniformityAnalysisImpl<ContextT>;

  std::unique_ptr<ImplT, GenericUniformityAnalysisImplDeleter<ImplT>> DA;

  GenericUniformityInfo(const GenericUniformityInfo &) = delete;
  GenericUniformityInfo &operator=(const GenericUniformityInfo &) = delete;
};

} // namespace llvm

#endif // LLVM_ADT_GENERICUNIFORMITYINFO_H
