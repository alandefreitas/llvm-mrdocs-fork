//===- SyntheticCountsUtils.h - utilities for count propagation--*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines utilities for synthetic counts propagation.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_SYNTHETICCOUNTSUTILS_H
#define LLVM_ANALYSIS_SYNTHETICCOUNTSUTILS_H

#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/Support/ScaledNumber.h"
#include <vector>

namespace llvm {

/// Class with methods to propagate synthetic entry counts.
///
/// This class is templated on the type of the call graph and designed to work
/// with the traditional per-module callgraph and the summary callgraphs used in
/// ThinLTO. This contains only static methods and alias templates.
template <typename CallGraphType> class SyntheticCountsUtils {
public:
  /// Scaled 64-bit count used during synthetic count propagation.
  using Scaled64 = ScaledNumber<uint64_t>;
  /// GraphTraits specialization for \c CallGraphType.
  using CGT = GraphTraits<CallGraphType>;
  /// Call-graph node type from \c CGT.
  using NodeRef = typename CGT::NodeRef;
  /// Call-graph edge type from \c CGT.
  using EdgeRef = typename CGT::EdgeRef;
  /// Strongly connected component represented as a vector of nodes.
  using SccTy = std::vector<NodeRef>;

  /// Callback that returns the profile count for a call-graph edge.
  ///
  /// Not all EdgeRef have information about the source of the edge. Hence
  /// NodeRef corresponding to the source of the EdgeRef is explicitly passed.
  using GetProfCountTy =
      function_ref<std::optional<Scaled64>(NodeRef, EdgeRef)>;
  /// Callback that adds a scaled count to a call-graph node.
  using AddCountTy = function_ref<void(NodeRef, Scaled64)>;

  /// Propagate synthetic entry counts through the call graph.
  ///
  /// Performs a reverse post-order traversal of the call-graph SCCs. For each
  /// SCC, first propagates entry counts to nodes within the SCC through call
  /// edges and updates them in one shot, then propagates counts to nodes
  /// outside the SCC. Requires a GraphTraits specialization for
  /// CallGraphType.
  /// @param CG Call graph to propagate counts over.
  /// @param GetProfCount Callback returning the profile count for an edge.
  /// @param AddCount Callback that adds a scaled count to a node.
  static void propagate(const CallGraphType &CG, GetProfCountTy GetProfCount,
                        AddCountTy AddCount);

private:
  static void propagateFromSCC(const SccTy &SCC, GetProfCountTy GetProfCount,
                               AddCountTy AddCount);
};
} // namespace llvm

#endif
