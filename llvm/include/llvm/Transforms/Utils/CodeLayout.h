//===- CodeLayout.h - Code layout/placement algorithms  ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Declares methods and data structures for code layout algorithms.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_CODELAYOUT_H
#define LLVM_TRANSFORMS_UTILS_CODELAYOUT_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Compiler.h"

#include <utility>
#include <vector>

namespace llvm {
/// Algorithms for laying out code to improve locality and I-cache utilization.
namespace codelayout {

/// Directed edge between two nodes identified by integer indices.
using EdgeT = std::pair<uint64_t, uint64_t>;

/// An edge in a CFG or call graph together with its execution count.
struct EdgeCount {
  /// Index of the source node.
  uint64_t src;
  /// Index of the destination node.
  uint64_t dst;
  /// Profile execution count of the edge.
  uint64_t count;
};

/// Find a CFG node layout that optimizes jump locality.
///
/// Improves processor I-cache utilization by increasing the number of
/// fall-through jumps and co-locating frequently executed nodes. The nodes are
/// assumed to be indexed by integers from [0, |V|) so that the current order is
/// the identity permutation.
/// \param NodeSizes The sizes of the nodes (in bytes).
/// \param NodeCounts The execution counts of the nodes in the profile.
/// \param EdgeCounts The execution counts of every edge (jump) in the profile.
///    The map also defines the edges in CFG and should include 0-count edges.
/// \returns The best block order found.
LLVM_ABI std::vector<uint64_t>
computeExtTspLayout(ArrayRef<uint64_t> NodeSizes, ArrayRef<uint64_t> NodeCounts,
                    ArrayRef<EdgeCount> EdgeCounts);

/// Estimate the quality of a given node order in a CFG.
///
/// The higher the score, the better the order is. The score is designed to
/// reflect the locality of the given order, which is anti-correlated with the
/// number of I-cache misses in a typical execution of the function.
/// \param Order The node order to evaluate.
/// \param NodeSizes The sizes of the nodes (in bytes).
/// \param EdgeCounts The execution counts of every edge (jump) in the profile.
/// \returns The estimated quality score of \p Order.
LLVM_ABI double calcExtTspScore(ArrayRef<uint64_t> Order,
                                ArrayRef<uint64_t> NodeSizes,
                                ArrayRef<EdgeCount> EdgeCounts);

/// Estimate the quality of the current node order in a CFG.
///
/// \param NodeSizes The sizes of the nodes (in bytes).
/// \param EdgeCounts The execution counts of every edge (jump) in the profile.
/// \returns The estimated quality score of the identity order.
LLVM_ABI double calcExtTspScore(ArrayRef<uint64_t> NodeSizes,
                                ArrayRef<EdgeCount> EdgeCounts);

/// Algorithm-specific params for Cache-Directed Sort. The values are tuned for
/// the best performance of large-scale front-end bound binaries.
struct CDSortConfig {
  /// The size of the cache.
  unsigned CacheEntries = 16;
  /// The size of a line in the cache.
  unsigned CacheSize = 2048;
  /// The maximum size of a chain to create.
  unsigned MaxChainSize = 128;
  /// The power exponent for the distance-based locality.
  double DistancePower = 0.25;
  /// The scale factor for the frequency-based locality.
  double FrequencyScale = 0.25;
};

/// Apply a Cache-Directed Sort to a call graph of functions.
///
/// The placement optimizes call locality by co-locating frequently executed
/// functions.
/// \param FuncSizes The sizes of the nodes (in bytes).
/// \param FuncCounts The execution counts of the nodes in the profile.
/// \param CallCounts The execution counts of every call edge in the profile.
///    The map also defines the edges in the call graph and should include
///    0-count edges.
/// \param CallOffsets The offsets of the calls from their source nodes.
/// \returns The best function order found.
LLVM_ABI std::vector<uint64_t> computeCacheDirectedLayout(
    ArrayRef<uint64_t> FuncSizes, ArrayRef<uint64_t> FuncCounts,
    ArrayRef<EdgeCount> CallCounts, ArrayRef<uint64_t> CallOffsets);

/// Apply a Cache-Directed Sort with a custom config.
///
/// \param Config Algorithm parameters controlling cache-directed sort.
/// \param FuncSizes The sizes of the nodes (in bytes).
/// \param FuncCounts The execution counts of the nodes in the profile.
/// \param CallCounts The execution counts of every call edge in the profile.
///    The map also defines the edges in the call graph and should include
///    0-count edges.
/// \param CallOffsets The offsets of the calls from their source nodes.
/// \returns The best function order found.
LLVM_ABI std::vector<uint64_t> computeCacheDirectedLayout(
    const CDSortConfig &Config, ArrayRef<uint64_t> FuncSizes,
    ArrayRef<uint64_t> FuncCounts, ArrayRef<EdgeCount> CallCounts,
    ArrayRef<uint64_t> CallOffsets);

} // namespace codelayout
} // namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_CODELAYOUT_H
