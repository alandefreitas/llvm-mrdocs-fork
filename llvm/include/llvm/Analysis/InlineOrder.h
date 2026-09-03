//===- InlineOrder.h - Inlining order abstraction -*- C++ ---*-------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
#ifndef LLVM_ANALYSIS_INLINEORDER_H
#define LLVM_ANALYSIS_INLINEORDER_H

#include "llvm/Analysis/InlineCost.h"
#include "llvm/Support/Compiler.h"
#include <utility>

namespace llvm {
class CallBase;
template <typename Fn> class function_ref;

/// Priority queue of call sites that defines the order of inlining attempts.
class InlineOrder {
public:
  /// Destroy the inline order.
  virtual ~InlineOrder() = default;

  /// Return the number of call sites remaining in the order.
  /// @return Number of call sites remaining in the order.
  virtual size_t size() = 0;

  /// Add call site \p Elt to the order.
  /// @param Elt Call site to enqueue for later inlining consideration.
  virtual void push(CallBase *Elt) = 0;

  /// Remove and return the next call site to consider for inlining.
  /// @return Highest-priority remaining call site.
  virtual CallBase *pop() = 0;

  /// Remove every call site for which \p Pred returns true.
  /// @param Pred Predicate that selects call sites to erase.
  virtual void erase_if(function_ref<bool(CallBase *)> Pred) = 0;

  /// Return true if no call sites remain in the order.
  /// @return True if no call sites remain in the order.
  bool empty() { return !size(); }
};

/// Create the default heuristic-based inline order for module \p M.
/// @param FAM Function analysis manager providing per-function analyses.
/// @param Params Inline cost/heuristic parameters.
/// @param MAM Module analysis manager providing module-level analyses.
/// @param M Module whose call sites are ordered.
/// @return Newly created default InlineOrder.
LLVM_ABI std::unique_ptr<InlineOrder>
getDefaultInlineOrder(FunctionAnalysisManager &FAM, const InlineParams &Params,
                      ModuleAnalysisManager &MAM, Module &M);

/// Create an InlineOrder for module \p M, preferring a plugin factory if one
/// is registered.
/// @param FAM Function analysis manager providing per-function analyses.
/// @param Params Inline cost/heuristic parameters.
/// @param MAM Module analysis manager that may hold PluginInlineOrderAnalysis.
/// @param M Module whose call sites are ordered.
/// @return Plugin-provided InlineOrder when registered, otherwise the default.
LLVM_ABI std::unique_ptr<InlineOrder>
getInlineOrder(FunctionAnalysisManager &FAM, const InlineParams &Params,
               ModuleAnalysisManager &MAM, Module &M);

/// Used for dynamically loading instances of InlineOrder as plugins
///
/// Plugins must implement an InlineOrderFactory, for an example refer to:
/// llvm/unittests/Analysis/InlineOrderPlugin/InlineOrderPlugin.cpp
///
/// If a PluginInlineOrderAnalysis has been registered with the
/// current ModuleAnalysisManager, llvm::getInlineOrder returns an
/// InlineOrder created by the PluginInlineOrderAnalysis' Factory.
///
class PluginInlineOrderAnalysis
    : public AnalysisInfoMixin<PluginInlineOrderAnalysis> {
public:
  /// Analysis key used to identify PluginInlineOrderAnalysis.
  LLVM_ABI static AnalysisKey Key;

  /// Factory that constructs a plugin InlineOrder for a module.
  typedef std::unique_ptr<InlineOrder> (*InlineOrderFactory)(
      FunctionAnalysisManager &FAM, const InlineParams &Params,
      ModuleAnalysisManager &MAM, Module &M);

  /// Construct analysis that exposes plugin factory \p Factory.
  /// @param Factory Non-null factory used to construct the plugin InlineOrder.
  PluginInlineOrderAnalysis(InlineOrderFactory Factory) : Factory(Factory) {
    assert(Factory != nullptr &&
           "The plugin inline order factory should not be a null pointer.");
  }

  /// Result holding the registered plugin InlineOrder factory.
  struct Result {
    /// Factory used to construct the plugin InlineOrder.
    InlineOrderFactory Factory;
  };

  /// Run the analysis and return the registered factory.
  /// @param M Module being analyzed (unused).
  /// @param MAM Module analysis manager (unused).
  /// @return Result containing the plugin InlineOrder factory.
  Result run(Module &M, ModuleAnalysisManager &MAM) { return {Factory}; }
  /// Return the registered plugin InlineOrder factory.
  /// @return Result containing the plugin InlineOrder factory.
  Result getResult() { return {Factory}; }

private:
  InlineOrderFactory Factory;
};

} // namespace llvm
#endif // LLVM_ANALYSIS_INLINEORDER_H
