//===- RegAllocPBQP.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the PBQPBuilder interface, for classes which build PBQP
// instances to represent register allocation problems, and the RegAllocPBQP
// interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_REGALLOCPBQP_H
#define LLVM_CODEGEN_REGALLOCPBQP_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/CodeGen/PBQP/CostAllocator.h"
#include "llvm/CodeGen/PBQP/Graph.h"
#include "llvm/CodeGen/PBQP/Math.h"
#include "llvm/CodeGen/PBQP/ReductionRules.h"
#include "llvm/CodeGen/PBQP/Solution.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/MC/MCRegister.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <limits>
#include <memory>
#include <set>
#include <vector>

namespace llvm {

class FunctionPass;
class LiveIntervals;
class MachineBlockFrequencyInfo;
class MachineFunction;
class raw_ostream;

namespace PBQP {
namespace RegAlloc {

/// Spill option index.
/// @return The index of the spill option (always 0).
inline unsigned getSpillOptionIdx() { return 0; }

/// Metadata to speed allocatability test.
///
/// Keeps track of the number of infinities in each row and column.
class MatrixMetadata {
public:
  /// Construct matrix metadata from a cost matrix \p M.
  ///
  /// @param M Cost matrix whose infinite entries are summarized.
  MatrixMetadata(const Matrix& M)
    : UnsafeRows(new bool[M.getRows() - 1]()),
      UnsafeCols(new bool[M.getCols() - 1]()) {
    unsigned* ColCounts = new unsigned[M.getCols() - 1]();

    for (unsigned i = 1; i < M.getRows(); ++i) {
      unsigned RowCount = 0;
      for (unsigned j = 1; j < M.getCols(); ++j) {
        if (M[i][j] == std::numeric_limits<PBQPNum>::infinity()) {
          ++RowCount;
          ++ColCounts[j - 1];
          UnsafeRows[i - 1] = true;
          UnsafeCols[j - 1] = true;
        }
      }
      WorstRow = std::max(WorstRow, RowCount);
    }
    unsigned WorstColCountForCurRow =
      *std::max_element(ColCounts, ColCounts + M.getCols() - 1);
    WorstCol = std::max(WorstCol, WorstColCountForCurRow);
    delete[] ColCounts;
  }

  /// Deleted copy constructor; matrix metadata is not copyable.
  /// @param Unused Ignored; copy construction is not supported.
  MatrixMetadata(const MatrixMetadata &Unused) = delete;
  /// Deleted copy assignment; matrix metadata is not copyable.
  /// @param Unused Ignored; copy assignment is not supported.
  MatrixMetadata &operator=(const MatrixMetadata &Unused) = delete;

  /// Return the maximum number of infinite entries in any data row.
  /// @return The maximum number of infinite entries in any data row.
  unsigned getWorstRow() const { return WorstRow; }
  /// Return the maximum number of infinite entries in any data column.
  /// @return The maximum number of infinite entries in any data column.
  unsigned getWorstCol() const { return WorstCol; }
  /// Return a bit array marking rows that contain at least one infinity.
  /// @return A bit array marking rows that contain at least one infinity.
  const bool* getUnsafeRows() const { return UnsafeRows.get(); }
  /// Return a bit array marking columns that contain at least one infinity.
  /// @return A bit array marking columns that contain at least one infinity.
  const bool* getUnsafeCols() const { return UnsafeCols.get(); }

private:
  unsigned WorstRow = 0;
  unsigned WorstCol = 0;
  std::unique_ptr<bool[]> UnsafeRows;
  std::unique_ptr<bool[]> UnsafeCols;
};

/// Holds a vector of the allowed physical regs for a vreg.
class AllowedRegVector {
  friend hash_code hash_value(const AllowedRegVector &);

public:
  /// Construct an empty allowed-register vector.
  AllowedRegVector() = default;
  /// Move-construct an allowed-register vector.
  /// @param Other Allowed-register vector to move from.
  AllowedRegVector(AllowedRegVector &&Other) = default;

  /// Construct an allowed-register vector from \p OptVec.
  ///
  /// @param OptVec Physical registers that may be assigned.
  AllowedRegVector(const std::vector<MCRegister> &OptVec)
      : NumOpts(OptVec.size()), Opts(new MCRegister[NumOpts]) {
    llvm::copy(OptVec, Opts.get());
  }

  /// Return the number of allowed physical registers.
  /// @return The number of allowed physical registers.
  unsigned size() const { return NumOpts; }
  /// Return the allowed physical register at index \p I.
  ///
  /// @param I Zero-based index into the allowed-register list.
  /// @return The allowed physical register at index \p I.
  MCRegister operator[](size_t I) const { return Opts[I]; }

  /// Return true if this vector equals \p Other.
  ///
  /// @param Other Other allowed-register vector to compare against.
  /// @return True if both vectors have the same length and register values.
  bool operator==(const AllowedRegVector &Other) const {
    if (NumOpts != Other.NumOpts)
      return false;
    return std::equal(Opts.get(), Opts.get() + NumOpts, Other.Opts.get());
  }

  /// Return true if this vector differs from \p Other.
  ///
  /// @param Other Other allowed-register vector to compare against.
  /// @return True if the vectors differ in length or contents.
  bool operator!=(const AllowedRegVector &Other) const {
    return !(*this == Other);
  }

private:
  unsigned NumOpts = 0;
  std::unique_ptr<MCRegister[]> Opts;
};

/// Return a hash code for the allowed-register vector \p OptRegs.
///
/// @param OptRegs Allowed-register vector to hash.
/// @return A hash code combining the length and register values of \p OptRegs.
inline hash_code hash_value(const AllowedRegVector &OptRegs) {
  MCRegister *OStart = OptRegs.Opts.get();
  MCRegister *OEnd = OptRegs.Opts.get() + OptRegs.NumOpts;
  return hash_combine(OptRegs.NumOpts,
                      hash_combine_range(OStart, OEnd));
}

/// Holds graph-level metadata relevant to PBQP RA problems.
class GraphMetadata {
private:
  using AllowedRegVecPool = ValuePool<AllowedRegVector>;

public:
  /// Pooled reference to an allowed-register vector.
  using AllowedRegVecRef = AllowedRegVecPool::PoolRef;

  /// Construct graph metadata for a machine function.
  ///
  /// @param MF Machine function being allocated.
  /// @param LIS Live-interval information for \p MF.
  /// @param MBFI Block-frequency information for \p MF.
  GraphMetadata(MachineFunction &MF,
                LiveIntervals &LIS,
                MachineBlockFrequencyInfo &MBFI)
    : MF(MF), LIS(LIS), MBFI(MBFI) {}

  /// Machine function whose registers are being allocated.
  MachineFunction &MF;
  /// Live-interval information for the machine function.
  LiveIntervals &LIS;
  /// Block-frequency information for the machine function.
  MachineBlockFrequencyInfo &MBFI;

  /// Record that virtual register \p VReg maps to node \p NId.
  ///
  /// @param VReg Virtual register to associate with a graph node.
  /// @param NId Graph node identifier for \p VReg.
  void setNodeIdForVReg(Register VReg, GraphBase::NodeId NId) {
    VRegToNodeId[VReg.id()] = NId;
  }

  /// Return the graph node id for virtual register \p VReg.
  ///
  /// @param VReg Virtual register to look up.
  /// @return The node id for \p VReg, or an invalid id if none is recorded.
  GraphBase::NodeId getNodeIdForVReg(Register VReg) const {
    auto VRegItr = VRegToNodeId.find(VReg);
    if (VRegItr == VRegToNodeId.end())
      return GraphBase::invalidNodeId();
    return VRegItr->second;
  }

  /// Intern \p Allowed in the allowed-register pool and return a reference.
  ///
  /// @param Allowed Allowed-register vector to pool.
  /// @return A pooled reference to the interned allowed-register vector.
  AllowedRegVecRef getAllowedRegs(AllowedRegVector Allowed) {
    return AllowedRegVecs.getValue(std::move(Allowed));
  }

private:
  DenseMap<Register, GraphBase::NodeId> VRegToNodeId;
  AllowedRegVecPool AllowedRegVecs;
};

/// Holds solver state and other metadata relevant to each PBQP RA node.
class NodeMetadata {
public:
  /// Allowed physical registers for this node's virtual register.
  using AllowedRegVector = RegAlloc::AllowedRegVector;

  /// Reduction progress of a PBQP RA node.
  ///
  /// The order in this enum is important, as it is assumed nodes can only
  /// progress up (i.e. towards being optimally reducible) when reducing the
  /// graph.
  using ReductionState = enum {
    /// Node has not yet been classified for reduction.
    Unprocessed,
    /// Node is not known to be allocatable without spilling.
    NotProvablyAllocatable,
    /// Node is conservatively known to be allocatable.
    ConservativelyAllocatable,
    /// Node can be reduced with a locally optimal PBQP rule.
    OptimallyReducible
  };

  /// Construct default node metadata in the unprocessed state.
  NodeMetadata() = default;

  /// Copy-construct node metadata from \p Other.
  ///
  /// @param Other Node metadata to copy.
  NodeMetadata(const NodeMetadata &Other)
      : RS(Other.RS), NumOpts(Other.NumOpts), DeniedOpts(Other.DeniedOpts),
        OptUnsafeEdges(new unsigned[NumOpts]), VReg(Other.VReg),
        AllowedRegs(Other.AllowedRegs)
#if LLVM_ENABLE_ABI_BREAKING_CHECKS
        ,
        everConservativelyAllocatable(Other.everConservativelyAllocatable)
#endif
  {
    if (NumOpts > 0) {
      std::copy(&Other.OptUnsafeEdges[0], &Other.OptUnsafeEdges[NumOpts],
                &OptUnsafeEdges[0]);
    }
  }

  /// Move-construct node metadata.
  /// @param Other Node metadata to move from.
  NodeMetadata(NodeMetadata &&Other) = default;
  /// Move-assign node metadata.
  /// @param Other Node metadata to move from.
  /// @return Reference to this node metadata.
  NodeMetadata& operator=(NodeMetadata &&Other) = default;

  /// Set the virtual register represented by this node.
  ///
  /// @param VReg Virtual register associated with this node.
  void setVReg(Register VReg) { this->VReg = VReg; }
  /// Return the virtual register represented by this node.
  /// @return The virtual register associated with this node.
  Register getVReg() const { return VReg; }

  /// Set the pooled allowed-register vector for this node.
  ///
  /// @param AllowedRegs Pooled reference to allowed physical registers.
  void setAllowedRegs(GraphMetadata::AllowedRegVecRef AllowedRegs) {
    this->AllowedRegs = std::move(AllowedRegs);
  }
  /// Return the allowed physical registers for this node.
  /// @return The allowed physical registers for this node's virtual register.
  const AllowedRegVector& getAllowedRegs() const { return *AllowedRegs; }

  /// Initialize option counts from node cost vector \p Costs.
  ///
  /// @param Costs Node cost vector; length minus one is the option count.
  void setup(const Vector& Costs) {
    NumOpts = Costs.getLength() - 1;
    OptUnsafeEdges = std::unique_ptr<unsigned[]>(new unsigned[NumOpts]());
  }

  /// Return the current reduction state of this node.
  /// @return The node's current reduction state.
  ReductionState getReductionState() const { return RS; }
  /// Set the reduction state of this node to \p RS.
  ///
  /// The new state must not be a downgrade from the current state.
  ///
  /// @param RS New reduction state.
  void setReductionState(ReductionState RS) {
    assert(RS >= this->RS && "A node's reduction state can not be downgraded");
    this->RS = RS;

#if LLVM_ENABLE_ABI_BREAKING_CHECKS
    // Remember this state to assert later that a non-infinite register
    // option was available.
    if (RS == ConservativelyAllocatable)
      everConservativelyAllocatable = true;
#endif
  }

  /// Update metadata for an edge that was connected to this node.
  ///
  /// @param MD Metadata of the edge cost matrix.
  /// @param Transpose True if this node is the matrix column side.
  void handleAddEdge(const MatrixMetadata& MD, bool Transpose) {
    DeniedOpts += Transpose ? MD.getWorstRow() : MD.getWorstCol();
    const bool* UnsafeOpts =
      Transpose ? MD.getUnsafeCols() : MD.getUnsafeRows();
    for (unsigned i = 0; i < NumOpts; ++i)
      OptUnsafeEdges[i] += UnsafeOpts[i];
  }

  /// Update metadata for an edge that was disconnected from this node.
  ///
  /// @param MD Metadata of the edge cost matrix.
  /// @param Transpose True if this node is the matrix column side.
  void handleRemoveEdge(const MatrixMetadata& MD, bool Transpose) {
    DeniedOpts -= Transpose ? MD.getWorstRow() : MD.getWorstCol();
    const bool* UnsafeOpts =
      Transpose ? MD.getUnsafeCols() : MD.getUnsafeRows();
    for (unsigned i = 0; i < NumOpts; ++i)
      OptUnsafeEdges[i] -= UnsafeOpts[i];
  }

  /// Return true if this node is conservatively allocatable.
  /// @return True if the node is conservatively known to be allocatable.
  bool isConservativelyAllocatable() const {
    return (DeniedOpts < NumOpts) ||
      (std::find(&OptUnsafeEdges[0], &OptUnsafeEdges[NumOpts], 0) !=
       &OptUnsafeEdges[NumOpts]);
  }

#if LLVM_ENABLE_ABI_BREAKING_CHECKS
  bool wasConservativelyAllocatable() const {
    return everConservativelyAllocatable;
  }
#endif

private:
  ReductionState RS = Unprocessed;
  unsigned NumOpts = 0;
  unsigned DeniedOpts = 0;
  std::unique_ptr<unsigned[]> OptUnsafeEdges;
  Register VReg;
  GraphMetadata::AllowedRegVecRef AllowedRegs;

#if LLVM_ENABLE_ABI_BREAKING_CHECKS
  bool everConservativelyAllocatable = false;
#endif
};

/// PBQP solver specialized for register-allocation graphs.
class RegAllocSolverImpl {
private:
  using RAMatrix = MDMatrix<MatrixMetadata>;

public:
  /// Raw (unmetadata'd) PBQP cost vector type.
  using RawVector = PBQP::Vector;
  /// Raw (unmetadata'd) PBQP cost matrix type.
  using RawMatrix = PBQP::Matrix;
  /// PBQP cost vector type used by this solver.
  using Vector = PBQP::Vector;
  /// PBQP cost matrix type with matrix metadata.
  using Matrix = RAMatrix;
  /// Allocator for pooled cost vectors and matrices.
  using CostAllocator = PBQP::PoolCostAllocator<Vector, Matrix>;

  /// Identifier type for graph nodes.
  using NodeId = GraphBase::NodeId;
  /// Identifier type for graph edges.
  using EdgeId = GraphBase::EdgeId;

  /// Per-node metadata type for register allocation.
  using NodeMetadata = RegAlloc::NodeMetadata;
  /// Per-edge metadata type for register allocation.
  struct EdgeMetadata {};
  /// Graph-level metadata type for register allocation.
  using GraphMetadata = RegAlloc::GraphMetadata;

  /// PBQP graph type solved by this implementation.
  using Graph = PBQP::Graph<RegAllocSolverImpl>;

  /// Construct a solver for register-allocation graph \p G.
  ///
  /// @param G PBQP register-allocation graph to solve.
  RegAllocSolverImpl(Graph &G) : G(G) {}

  /// Solve the register-allocation PBQP instance and return a solution.
  /// @return A solution assigning options to each node in the graph.
  Solution solve() {
    G.setSolver(*this);
    Solution S;
    setup();
    S = backpropagate(G, reduce());
    G.unsetSolver();
    return S;
  }

  /// Handle addition of node \p NId to the graph.
  ///
  /// @param NId Identifier of the newly added node.
  void handleAddNode(NodeId NId) {
    assert(G.getNodeCosts(NId).getLength() > 1 &&
           "PBQP Graph should not contain single or zero-option nodes");
    G.getNodeMetadata(NId).setup(G.getNodeCosts(NId));
  }

  /// Handle removal of node \p NId from the graph.
  ///
  /// @param NId Identifier of the removed node.
  void handleRemoveNode(NodeId NId) {}
  /// Handle replacement of the cost vector for node \p NId.
  ///
  /// @param NId Identifier of the node whose costs changed.
  /// @param newCosts New cost vector for the node.
  void handleSetNodeCosts(NodeId NId, const Vector& newCosts) {}

  /// Handle addition of edge \p EId to the graph.
  ///
  /// @param EId Identifier of the newly added edge.
  void handleAddEdge(EdgeId EId) {
    handleReconnectEdge(EId, G.getEdgeNode1Id(EId));
    handleReconnectEdge(EId, G.getEdgeNode2Id(EId));
  }

  /// Handle disconnection of edge \p EId from node \p NId.
  ///
  /// @param EId Identifier of the disconnected edge.
  /// @param NId Identifier of the node that lost the edge.
  void handleDisconnectEdge(EdgeId EId, NodeId NId) {
    NodeMetadata& NMd = G.getNodeMetadata(NId);
    const MatrixMetadata& MMd = G.getEdgeCosts(EId).getMetadata();
    NMd.handleRemoveEdge(MMd, NId == G.getEdgeNode2Id(EId));
    promote(NId, NMd);
  }

  /// Handle reconnection of edge \p EId to node \p NId.
  ///
  /// @param EId Identifier of the reconnected edge.
  /// @param NId Identifier of the node that gained the edge.
  void handleReconnectEdge(EdgeId EId, NodeId NId) {
    NodeMetadata& NMd = G.getNodeMetadata(NId);
    const MatrixMetadata& MMd = G.getEdgeCosts(EId).getMetadata();
    NMd.handleAddEdge(MMd, NId == G.getEdgeNode2Id(EId));
  }

  /// Handle replacement of the cost matrix for edge \p EId.
  ///
  /// @param EId Identifier of the edge whose costs changed.
  /// @param NewCosts New cost matrix for the edge.
  void handleUpdateCosts(EdgeId EId, const Matrix& NewCosts) {
    NodeId N1Id = G.getEdgeNode1Id(EId);
    NodeId N2Id = G.getEdgeNode2Id(EId);
    NodeMetadata& N1Md = G.getNodeMetadata(N1Id);
    NodeMetadata& N2Md = G.getNodeMetadata(N2Id);
    bool Transpose = N1Id != G.getEdgeNode1Id(EId);

    // Metadata are computed incrementally. First, update them
    // by removing the old cost.
    const MatrixMetadata& OldMMd = G.getEdgeCosts(EId).getMetadata();
    N1Md.handleRemoveEdge(OldMMd, Transpose);
    N2Md.handleRemoveEdge(OldMMd, !Transpose);

    // And update now the metadata with the new cost.
    const MatrixMetadata& MMd = NewCosts.getMetadata();
    N1Md.handleAddEdge(MMd, Transpose);
    N2Md.handleAddEdge(MMd, !Transpose);

    // As the metadata may have changed with the update, the nodes may have
    // become ConservativelyAllocatable or OptimallyReducible.
    promote(N1Id, N1Md);
    promote(N2Id, N2Md);
  }

private:
  void promote(NodeId NId, NodeMetadata& NMd) {
    if (G.getNodeDegree(NId) == 3) {
      // This node is becoming optimally reducible.
      moveToOptimallyReducibleNodes(NId);
    } else if (NMd.getReductionState() ==
               NodeMetadata::NotProvablyAllocatable &&
               NMd.isConservativelyAllocatable()) {
      // This node just became conservatively allocatable.
      moveToConservativelyAllocatableNodes(NId);
    }
  }

  void removeFromCurrentSet(NodeId NId) {
    switch (G.getNodeMetadata(NId).getReductionState()) {
    case NodeMetadata::Unprocessed: break;
    case NodeMetadata::OptimallyReducible:
      assert(OptimallyReducibleNodes.find(NId) !=
             OptimallyReducibleNodes.end() &&
             "Node not in optimally reducible set.");
      OptimallyReducibleNodes.erase(NId);
      break;
    case NodeMetadata::ConservativelyAllocatable:
      assert(ConservativelyAllocatableNodes.find(NId) !=
             ConservativelyAllocatableNodes.end() &&
             "Node not in conservatively allocatable set.");
      ConservativelyAllocatableNodes.erase(NId);
      break;
    case NodeMetadata::NotProvablyAllocatable:
      assert(NotProvablyAllocatableNodes.find(NId) !=
             NotProvablyAllocatableNodes.end() &&
             "Node not in not-provably-allocatable set.");
      NotProvablyAllocatableNodes.erase(NId);
      break;
    }
  }

  void moveToOptimallyReducibleNodes(NodeId NId) {
    removeFromCurrentSet(NId);
    OptimallyReducibleNodes.insert(NId);
    G.getNodeMetadata(NId).setReductionState(
      NodeMetadata::OptimallyReducible);
  }

  void moveToConservativelyAllocatableNodes(NodeId NId) {
    removeFromCurrentSet(NId);
    ConservativelyAllocatableNodes.insert(NId);
    G.getNodeMetadata(NId).setReductionState(
      NodeMetadata::ConservativelyAllocatable);
  }

  void moveToNotProvablyAllocatableNodes(NodeId NId) {
    removeFromCurrentSet(NId);
    NotProvablyAllocatableNodes.insert(NId);
    G.getNodeMetadata(NId).setReductionState(
      NodeMetadata::NotProvablyAllocatable);
  }

  void setup() {
    // Set up worklists.
    for (auto NId : G.nodeIds()) {
      if (G.getNodeDegree(NId) < 3)
        moveToOptimallyReducibleNodes(NId);
      else if (G.getNodeMetadata(NId).isConservativelyAllocatable())
        moveToConservativelyAllocatableNodes(NId);
      else
        moveToNotProvablyAllocatableNodes(NId);
    }
  }

  // Compute a reduction order for the graph by iteratively applying PBQP
  // reduction rules. Locally optimal rules are applied whenever possible (R0,
  // R1, R2). If no locally-optimal rules apply then any conservatively
  // allocatable node is reduced. Finally, if no conservatively allocatable
  // node exists then the node with the lowest spill-cost:degree ratio is
  // selected.
  std::vector<GraphBase::NodeId> reduce() {
    assert(!G.empty() && "Cannot reduce empty graph.");

    using NodeId = GraphBase::NodeId;
    std::vector<NodeId> NodeStack;

    // Consume worklists.
    while (true) {
      if (!OptimallyReducibleNodes.empty()) {
        NodeSet::iterator NItr = OptimallyReducibleNodes.begin();
        NodeId NId = *NItr;
        OptimallyReducibleNodes.erase(NItr);
        NodeStack.push_back(NId);
        switch (G.getNodeDegree(NId)) {
        case 0:
          break;
        case 1:
          applyR1(G, NId);
          break;
        case 2:
          applyR2(G, NId);
          break;
        default: llvm_unreachable("Not an optimally reducible node.");
        }
      } else if (!ConservativelyAllocatableNodes.empty()) {
        // Conservatively allocatable nodes will never spill. For now just
        // take the first node in the set and push it on the stack. When we
        // start optimizing more heavily for register preferencing, it may
        // would be better to push nodes with lower 'expected' or worst-case
        // register costs first (since early nodes are the most
        // constrained).
        NodeSet::iterator NItr = ConservativelyAllocatableNodes.begin();
        NodeId NId = *NItr;
        ConservativelyAllocatableNodes.erase(NItr);
        NodeStack.push_back(NId);
        G.disconnectAllNeighborsFromNode(NId);
      } else if (!NotProvablyAllocatableNodes.empty()) {
        NodeSet::iterator NItr = llvm::min_element(NotProvablyAllocatableNodes,
                                                   SpillCostComparator(G));
        NodeId NId = *NItr;
        NotProvablyAllocatableNodes.erase(NItr);
        NodeStack.push_back(NId);
        G.disconnectAllNeighborsFromNode(NId);
      } else
        break;
    }

    return NodeStack;
  }

  class SpillCostComparator {
  public:
    SpillCostComparator(const Graph& G) : G(G) {}

    bool operator()(NodeId N1Id, NodeId N2Id) {
      PBQPNum N1SC = G.getNodeCosts(N1Id)[0];
      PBQPNum N2SC = G.getNodeCosts(N2Id)[0];
      if (N1SC == N2SC)
        return G.getNodeDegree(N1Id) < G.getNodeDegree(N2Id);
      return N1SC < N2SC;
    }

  private:
    const Graph& G;
  };

  Graph& G;
  using NodeSet = std::set<NodeId>;
  NodeSet OptimallyReducibleNodes;
  NodeSet ConservativelyAllocatableNodes;
  NodeSet NotProvablyAllocatableNodes;
};

/// PBQP graph specialized for register-allocation problems.
class PBQPRAGraph : public PBQP::Graph<RegAllocSolverImpl> {
private:
  using BaseT = PBQP::Graph<RegAllocSolverImpl>;

public:
  /// Construct a PBQP register-allocation graph with \p Metadata.
  ///
  /// @param Metadata Graph-level metadata for the allocation problem.
  PBQPRAGraph(GraphMetadata Metadata) : BaseT(std::move(Metadata)) {}

  /// Dump this graph to dbgs().
  LLVM_ABI void dump() const;

  /// Dump this graph to an output stream.
  /// @param OS Output stream to print on.
  LLVM_ABI void dump(raw_ostream &OS) const;

  /// Print a representation of this graph in DOT format.
  /// @param OS Output stream to print on.
  LLVM_ABI void printDot(raw_ostream &OS) const;
};

/// Solve the PBQP register-allocation graph \p G.
///
/// @param G Register-allocation graph to solve.
/// @return A solution assigning options to each node, or empty if \p G is empty.
inline Solution solve(PBQPRAGraph& G) {
  if (G.empty())
    return Solution();
  RegAllocSolverImpl RegAllocSolver(G);
  return RegAllocSolver.solve();
}

} // end namespace RegAlloc
} // end namespace PBQP

/// Create a PBQP register allocator instance.
///
/// @param customPassID Optional custom pass identifier for the allocator.
/// @return A FunctionPass that performs PBQP register allocation.
LLVM_ABI FunctionPass *
createPBQPRegisterAllocator(char *customPassID = nullptr);

} // end namespace llvm

#endif // LLVM_CODEGEN_REGALLOCPBQP_H
