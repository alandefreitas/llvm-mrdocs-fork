//===- InlineModelFeatureMaps.h - common model runner defs ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//

#ifndef LLVM_ANALYSIS_INLINEMODELFEATUREMAPS_H
#define LLVM_ANALYSIS_INLINEMODELFEATUREMAPS_H

#include "llvm/Analysis/TensorSpec.h"
#include "llvm/Support/Compiler.h"

#include <array>
#include <vector>

namespace llvm {

// List of cost features. A "cost" feature is a summand of the heuristic-based
// inline cost, and we define them separately to preserve the original heuristic
// behavior.
#define INLINE_COST_FEATURE_ITERATOR(M)                                        \
  M(int64_t, {1}, sroa_savings,                                                \
    "Savings from SROA (scalar replacement of aggregates)")                    \
  M(int64_t, {1}, sroa_losses,                                                 \
    "Losses from SROA (scalar replacement of aggregates)")                     \
  M(int64_t, {1}, load_elimination, "Cost of load elimination in the call")    \
  M(int64_t, {1}, call_penalty,                                                \
    "Accumulation of penalty applied to call sites when inlining")             \
  M(int64_t, {1}, call_argument_setup,                                         \
    "Accumulation of call argument setup costs")                               \
  M(int64_t, {1}, load_relative_intrinsic,                                     \
    "Accumulation of costs of loading relative intrinsics")                    \
  M(int64_t, {1}, lowered_call_arg_setup,                                      \
    "Accumulation of cost of lowered call argument setups")                    \
  M(int64_t, {1}, indirect_call_penalty,                                       \
    "Accumulation of costs for indirect calls")                                \
  M(int64_t, {1}, jump_table_penalty, "Accumulation of costs for jump tables") \
  M(int64_t, {1}, case_cluster_penalty,                                        \
    "Accumulation of costs for case clusters")                                 \
  M(int64_t, {1}, switch_default_dest_penalty,                                 \
    "Accumulation of costs for switch default destination")                    \
  M(int64_t, {1}, switch_penalty,                                              \
    "Accumulation of costs for switch statements")                             \
  M(int64_t, {1}, unsimplified_common_instructions,                            \
    "Costs from unsimplified common instructions")                             \
  M(int64_t, {1}, num_loops, "Number of loops in the caller")                  \
  M(int64_t, {1}, dead_blocks, "Number of dead blocks in the caller")          \
  M(int64_t, {1}, simplified_instructions,                                     \
    "Number of simplified instructions")                                       \
  M(int64_t, {1}, constant_args,                                               \
    "Number of constant arguments in the call site")                           \
  M(int64_t, {1}, constant_offset_ptr_args,                                    \
    "Number of constant offset pointer args in the call site")                 \
  M(int64_t, {1}, callsite_cost, "Estimated cost of the call site")            \
  M(int64_t, {1}, cold_cc_penalty, "Penalty for a cold calling convention")    \
  M(int64_t, {1}, last_call_to_static_bonus,                                   \
    "Bonus for being the last call to static")                                 \
  M(int64_t, {1}, is_multiple_blocks,                                          \
    "Boolean; is the Callee multiple blocks")                                  \
  M(int64_t, {1}, nested_inlines,                                              \
    "Would the default inliner perfom nested inlining")                        \
  M(int64_t, {1}, nested_inline_cost_estimate,                                 \
    "Estimate of the accumulated cost of nested inlines")                      \
  M(int64_t, {1}, threshold, "Threshold for the heuristic inliner")

/// Indices of heuristic inline-cost feature summands.
///
/// A "cost" feature is a summand of the heuristic-based inline cost. These
/// indices are defined separately from other ML features to preserve the
/// original heuristic behavior. Values match \c INLINE_COST_FEATURE_ITERATOR
/// order (comments cannot live inside that X-macro).
// clang-format off
enum class InlineCostFeatureIndex : size_t {
  sroa_savings, ///< Savings from SROA (scalar replacement of aggregates).
  sroa_losses, ///< Losses from SROA (scalar replacement of aggregates).
  load_elimination, ///< Cost of load elimination in the call.
  call_penalty, ///< Accumulation of penalty applied to call sites when inlining.
  call_argument_setup, ///< Accumulation of call argument setup costs.
  load_relative_intrinsic, ///< Accumulation of costs of loading relative intrinsics.
  lowered_call_arg_setup, ///< Accumulation of cost of lowered call argument setups.
  indirect_call_penalty, ///< Accumulation of costs for indirect calls.
  jump_table_penalty, ///< Accumulation of costs for jump tables.
  case_cluster_penalty, ///< Accumulation of costs for case clusters.
  switch_default_dest_penalty, ///< Accumulation of costs for switch default destination.
  switch_penalty, ///< Accumulation of costs for switch statements.
  unsimplified_common_instructions, ///< Costs from unsimplified common instructions.
  num_loops, ///< Number of loops in the caller.
  dead_blocks, ///< Number of dead blocks in the caller.
  simplified_instructions, ///< Number of simplified instructions.
  constant_args, ///< Number of constant arguments in the call site.
  constant_offset_ptr_args, ///< Number of constant offset pointer args in the call site.
  callsite_cost, ///< Estimated cost of the call site.
  cold_cc_penalty, ///< Penalty for a cold calling convention.
  last_call_to_static_bonus, ///< Bonus for being the last call to static.
  is_multiple_blocks, ///< Boolean; is the callee multiple blocks.
  nested_inlines, ///< Would the default inliner perform nested inlining.
  nested_inline_cost_estimate, ///< Estimate of the accumulated cost of nested inlines.
  threshold, ///< Threshold for the heuristic inliner.

  NumberOfFeatures ///< Count of cost features; not itself a feature index.
};
// clang-format on

/// Array of per-call-site heuristic inline cost feature values.
using InlineCostFeatures =
    std::array<int,
               static_cast<size_t>(InlineCostFeatureIndex::NumberOfFeatures)>;

/// Return true if \p Feature contributes to the heuristic inline cost.
/// @param Feature Cost-feature index to classify.
/// @return False for bookkeeping features that are not heuristic cost summands.
constexpr bool isHeuristicInlineCostFeature(InlineCostFeatureIndex Feature) {
  return Feature != InlineCostFeatureIndex::sroa_savings &&
         Feature != InlineCostFeatureIndex::is_multiple_blocks &&
         Feature != InlineCostFeatureIndex::dead_blocks &&
         Feature != InlineCostFeatureIndex::simplified_instructions &&
         Feature != InlineCostFeatureIndex::constant_args &&
         Feature != InlineCostFeatureIndex::constant_offset_ptr_args &&
         Feature != InlineCostFeatureIndex::nested_inlines &&
         Feature != InlineCostFeatureIndex::nested_inline_cost_estimate &&
         Feature != InlineCostFeatureIndex::threshold;
}

// List of features. Each feature is defined through a triple:
// - the name of an enum member, which will be the feature index
// - a textual name, used for ML model binding (so it needs to match the
// names used by the ML model).
// - a documentation description. Currently, that is not used anywhere
// programmatically, and serves as workaround to inability of inserting comments
// in macros.
#define INLINE_FEATURE_ITERATOR(M)                                             \
  M(int64_t, {1}, callee_basic_block_count,                                    \
    "number of basic blocks of the callee")                                    \
  M(int64_t, {1}, callsite_height,                                             \
    "position of the call site in the original call graph - measured from "    \
    "the farthest SCC")                                                        \
  M(int64_t, {1}, node_count,                                                  \
    "total current number of defined functions in the module")                 \
  M(int64_t, {1}, nr_ctant_params,                                             \
    "number of parameters in the call site that are constants")                \
  M(int64_t, {1}, cost_estimate, "total cost estimate (threshold - free)")     \
  M(int64_t, {1}, edge_count, "total number of calls in the module")           \
  M(int64_t, {1}, caller_users,                                                \
    "number of module-internal users of the caller, +1 if the caller is "      \
    "exposed externally")                                                      \
  M(int64_t, {1}, caller_conditionally_executed_blocks,                        \
    "number of blocks reached from a conditional instruction, in the caller")  \
  M(int64_t, {1}, caller_basic_block_count,                                    \
    "number of basic blocks in the caller")                                    \
  M(int64_t, {1}, callee_conditionally_executed_blocks,                        \
    "number of blocks reached from a conditional instruction, in the callee")  \
  M(int64_t, {1}, callee_users,                                                \
    "number of module-internal users of the callee, +1 if the callee is "      \
    "exposed externally")                                                      \
  M(int64_t, {1}, is_callee_avail_external,                                    \
    "Is callee an available-externally linkage type (i.e. could be DCEd if "   \
    "not "                                                                     \
    "fully inlined by ElimAvailExtern)")                                       \
  M(int64_t, {1}, is_caller_avail_external,                                    \
    "Is caller an available-externally linkage type (i.e. could be DCEd if "   \
    "not "                                                                     \
    "fully inlined by ElimAvailExtern)")

/// Indices of ML inliner input features (cost features, context, embeddings).
///
/// Not all features listed in FeatureIndex are used by the ML model.
/// Specifically, callee_embedding and caller_embedding are used only when the
/// usage of IR2Vec embeddings is explicitly enabled. Meaning, the size/number of
/// features is not static. So, we cannot determine number of features based on
/// the number of elements in this enum. Cost-feature indices come first and
/// match \c INLINE_COST_FEATURE_ITERATOR; remaining values match
/// \c INLINE_FEATURE_ITERATOR (comments cannot live inside those X-macros).
// clang-format off
enum class FeatureIndex : size_t {
  // InlineCost features - these must come first
  sroa_savings, ///< Savings from SROA (scalar replacement of aggregates).
  sroa_losses, ///< Losses from SROA (scalar replacement of aggregates).
  load_elimination, ///< Cost of load elimination in the call.
  call_penalty, ///< Accumulation of penalty applied to call sites when inlining.
  call_argument_setup, ///< Accumulation of call argument setup costs.
  load_relative_intrinsic, ///< Accumulation of costs of loading relative intrinsics.
  lowered_call_arg_setup, ///< Accumulation of cost of lowered call argument setups.
  indirect_call_penalty, ///< Accumulation of costs for indirect calls.
  jump_table_penalty, ///< Accumulation of costs for jump tables.
  case_cluster_penalty, ///< Accumulation of costs for case clusters.
  switch_default_dest_penalty, ///< Accumulation of costs for switch default destination.
  switch_penalty, ///< Accumulation of costs for switch statements.
  unsimplified_common_instructions, ///< Costs from unsimplified common instructions.
  num_loops, ///< Number of loops in the caller.
  dead_blocks, ///< Number of dead blocks in the caller.
  simplified_instructions, ///< Number of simplified instructions.
  constant_args, ///< Number of constant arguments in the call site.
  constant_offset_ptr_args, ///< Number of constant offset pointer args in the call site.
  callsite_cost, ///< Estimated cost of the call site.
  cold_cc_penalty, ///< Penalty for a cold calling convention.
  last_call_to_static_bonus, ///< Bonus for being the last call to static.
  is_multiple_blocks, ///< Boolean; is the callee multiple blocks.
  nested_inlines, ///< Would the default inliner perform nested inlining.
  nested_inline_cost_estimate, ///< Estimate of the accumulated cost of nested inlines.
  threshold, ///< Threshold for the heuristic inliner.

  // Non-cost features
  callee_basic_block_count, ///< Number of basic blocks of the callee.
  callsite_height, ///< Position of the call site in the original call graph, measured from the farthest SCC.
  node_count, ///< Total current number of defined functions in the module.
  nr_ctant_params, ///< Number of parameters in the call site that are constants.
  cost_estimate, ///< Total cost estimate (threshold - free).
  edge_count, ///< Total number of calls in the module.
  caller_users, ///< Number of module-internal users of the caller, +1 if exposed externally.
  caller_conditionally_executed_blocks, ///< Number of blocks reached from a conditional instruction in the caller.
  caller_basic_block_count, ///< Number of basic blocks in the caller.
  callee_conditionally_executed_blocks, ///< Number of blocks reached from a conditional instruction in the callee.
  callee_users, ///< Number of module-internal users of the callee, +1 if exposed externally.
  is_callee_avail_external, ///< Whether the callee has available-externally linkage.
  is_caller_avail_external, ///< Whether the caller has available-externally linkage.

  // IR2Vec embeddings
  // Dimensions of embeddings are not known in the compile time (until vocab is
  // read). Hence macros cannot be used here.
  callee_embedding, ///< IR2Vec embedding of the callee (optional).
  caller_embedding  ///< IR2Vec embedding of the caller (optional).
};
// clang-format on

/// Map an inline-cost feature index to the corresponding ML feature index.
/// @param Feature Cost-feature index to convert.
/// @return The ML \c FeatureIndex with the same ordinal as \p Feature.
constexpr FeatureIndex
inlineCostFeatureToMlFeature(InlineCostFeatureIndex Feature) {
  return static_cast<FeatureIndex>(static_cast<size_t>(Feature));
}

/// Tensor name for the ML model's inlining decision output.
LLVM_ABI extern const char *const DecisionName;
/// Spec for the ML model's inlining decision output tensor.
LLVM_ABI extern const TensorSpec InlineDecisionSpec;
/// Tensor name for the default (heuristic) inlining decision.
LLVM_ABI extern const char *const DefaultDecisionName;
/// Spec for the default (heuristic) inlining decision tensor.
LLVM_ABI extern const TensorSpec DefaultDecisionSpec;
/// Tensor name for the training reward (IR size delta).
LLVM_ABI extern const char *const RewardName;

/// Vector of ML inliner feature values for a call site.
using InlineFeatures = std::vector<int64_t>;

} // namespace llvm
#endif // LLVM_ANALYSIS_INLINEMODELFEATUREMAPS_H
