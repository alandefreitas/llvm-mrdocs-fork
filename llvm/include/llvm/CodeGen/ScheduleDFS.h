//===- ScheduleDFS.h - ILP metric for ScheduleDAGInstrs ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Definition of an ILP metric for machine level instruction scheduling.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SCHEDULEDFS_H
#define LLVM_CODEGEN_SCHEDULEDFS_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include <cassert>
#include <cstdint>
#include <vector>

namespace llvm {

template <typename T> class ArrayRef;
class raw_ostream;

/// Represent the ILP of the subDAG rooted at a DAG node.
///
/// ILPValues summarize the DAG subtree rooted at each node. ILPValues are
/// valid for all nodes regardless of their subtree membership.
///
/// When computed using bottom-up DFS, this metric assumes that the DAG is a
/// forest of trees with roots at the bottom of the schedule branching upward.
struct ILPValue {
  /// Number of instructions in the subDAG rooted at this node.
  unsigned InstrCount;
  /// Length may either correspond to depth or height, depending on direction,
  /// and cycles or nodes depending on context.
  unsigned Length;

  /// Construct an ILP metric from an instruction count and a path length.
  /// \param count Number of instructions in the subDAG.
  /// \param length Path length (depth or height) of the subDAG.
  ILPValue(unsigned count, unsigned length):
    InstrCount(count), Length(length) {}

  /// Return true if this ILP is strictly less than \p RHS.
  /// \param RHS ILP value to compare against.
  /// \return True if this ILP is strictly less than \p RHS.
  bool operator<(ILPValue RHS) const {
    return (uint64_t)InstrCount * RHS.Length
      < (uint64_t)Length * RHS.InstrCount;
  }
  /// Return true if this ILP is strictly greater than \p RHS.
  /// \param RHS ILP value to compare against.
  /// \return True if this ILP is strictly greater than \p RHS.
  bool operator>(ILPValue RHS) const {
    return RHS < *this;
  }
  /// Return true if this ILP is less than or equal to \p RHS.
  /// \param RHS ILP value to compare against.
  /// \return True if this ILP is less than or equal to \p RHS.
  bool operator<=(ILPValue RHS) const {
    return (uint64_t)InstrCount * RHS.Length
      <= (uint64_t)Length * RHS.InstrCount;
  }
  /// Return true if this ILP is greater than or equal to \p RHS.
  /// \param RHS ILP value to compare against.
  /// \return True if this ILP is greater than or equal to \p RHS.
  bool operator>=(ILPValue RHS) const {
    return RHS <= *this;
  }

  /// Print this ILP value to \p OS.
  /// \param OS Output stream receiving the printed representation.
  LLVM_ABI void print(raw_ostream &OS) const;

  /// Dump this ILP value to the debug stream.
  LLVM_ABI void dump() const;
};

/// Compute the values of each DAG node for various metrics during DFS.
class SchedDFSResult {
  /// Internal DFS state used to compute SchedDFSResult metrics.
  friend class SchedDFSImpl;

  static const unsigned InvalidSubtreeID = ~0u;

  /// Per-SUnit data computed during DFS for various metrics.
  ///
  /// A node's SubtreeID is set to itself when it is visited to indicate that it
  /// is the root of a subtree. Later it is set to its parent to indicate an
  /// interior node. Finally, it is set to a representative subtree ID during
  /// finalization.
  struct NodeData {
    unsigned InstrCount = 0;
    unsigned SubtreeID = InvalidSubtreeID;

    NodeData() = default;
  };

  /// Per-Subtree data computed during DFS.
  struct TreeData {
    unsigned ParentTreeID = InvalidSubtreeID;
    unsigned SubInstrCount = 0;

    TreeData() = default;
  };

  /// Record a connection between subtrees and the connection level.
  struct Connection {
    unsigned TreeID;
    unsigned Level;

    Connection(unsigned tree, unsigned level): TreeID(tree), Level(level) {}
  };

  bool IsBottomUp;
  unsigned SubtreeLimit;
  /// DFS results for each SUnit in this DAG.
  std::vector<NodeData> DFSNodeData;

  // Store per-tree data indexed on tree ID,
  SmallVector<TreeData, 16> DFSTreeData;

  // For each subtree discovered during DFS, record its connections to other
  // subtrees.
  std::vector<SmallVector<Connection, 4>> SubtreeConnections;

  /// Cache the current connection level of each subtree.
  /// This mutable array is updated during scheduling.
  std::vector<unsigned> SubtreeConnectLevels;

public:
  /// Construct a DFS result for bottom-up or top-down analysis.
  /// \param IsBU True to compute metrics bottom-up; false for top-down.
  /// \param lim Node-count cutoff before a subtree is considered significant.
  SchedDFSResult(bool IsBU, unsigned lim)
    : IsBottomUp(IsBU), SubtreeLimit(lim) {}

  /// Get the node cutoff before subtrees are considered significant.
  /// \return Node-count cutoff before a subtree is considered significant.
  unsigned getSubtreeLimit() const { return SubtreeLimit; }

  /// Return true if this DFSResult is uninitialized.
  ///
  /// resize() initializes DFSResult, while compute() populates it.
  /// \return True if this DFSResult has not been initialized.
  bool empty() const { return DFSNodeData.empty(); }

  /// Clear the results.
  void clear() {
    DFSNodeData.clear();
    DFSTreeData.clear();
    SubtreeConnections.clear();
    SubtreeConnectLevels.clear();
  }

  /// Initialize the result data with the size of the DAG.
  /// \param NumSUnits Number of SUnits in the DAG to allocate for.
  void resize(unsigned NumSUnits) {
    DFSNodeData.resize(NumSUnits);
  }

  /// Compute various metrics for the DAG with given roots.
  /// \param SUnits Scheduling units of the DAG to analyze.
  LLVM_ABI void compute(ArrayRef<SUnit> SUnits);

  /// Get the number of instructions in the given subtree and its
  /// children.
  /// \param SU DAG node whose subtree instruction count is returned.
  /// \return Number of instructions in \p SU's subtree including children.
  unsigned getNumInstrs(const SUnit *SU) const {
    return DFSNodeData[SU->NodeNum].InstrCount;
  }

  /// Get the number of instructions in the given subtree not including
  /// children.
  /// \param SubtreeID Identifier of the subtree to query.
  /// \return Number of instructions in the subtree excluding child subtrees.
  unsigned getNumSubInstrs(unsigned SubtreeID) const {
    return DFSTreeData[SubtreeID].SubInstrCount;
  }

  /// Get the ILP value for a DAG node.
  ///
  /// A leaf node has an ILP of 1/1.
  /// \param SU DAG node whose ILP is returned.
  /// \return ILP metric for the subDAG rooted at \p SU.
  ILPValue getILP(const SUnit *SU) const {
    return ILPValue(DFSNodeData[SU->NodeNum].InstrCount, 1 + SU->getDepth());
  }

  /// The number of subtrees detected in this DAG.
  /// \return Number of subtrees discovered in this DAG.
  unsigned getNumSubtrees() const { return SubtreeConnectLevels.size(); }

  /// Get the ID of the subtree the given DAG node belongs to.
  ///
  /// For convenience, if DFSResults have not been computed yet, give everything
  /// tree ID 0.
  /// \param SU DAG node whose subtree ID is returned.
  /// \return Subtree ID for \p SU, or 0 if DFS results are not yet computed.
  unsigned getSubtreeID(const SUnit *SU) const {
    if (empty())
      return 0;
    assert(SU->NodeNum < DFSNodeData.size() &&  "New Node");
    return DFSNodeData[SU->NodeNum].SubtreeID;
  }

  /// Get the connection level of a subtree.
  ///
  /// For bottom-up trees, the connection level is the latency depth (in cycles)
  /// of the deepest connection to another subtree.
  /// \param SubtreeID Identifier of the subtree to query.
  /// \return Connection level of the given subtree.
  unsigned getSubtreeLevel(unsigned SubtreeID) const {
    return SubtreeConnectLevels[SubtreeID];
  }

  /// Scheduler callback to update SubtreeConnectLevels when a tree is
  /// initially scheduled.
  /// \param SubtreeID Identifier of the subtree that was scheduled.
  LLVM_ABI void scheduleTree(unsigned SubtreeID);
};

/// Print an ILPValue to a raw_ostream.
/// \param OS Output stream receiving the printed representation.
/// \param Val ILP value to print.
/// \return Reference to the output stream.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const ILPValue &Val);

} // end namespace llvm

#endif // LLVM_CODEGEN_SCHEDULEDFS_H
