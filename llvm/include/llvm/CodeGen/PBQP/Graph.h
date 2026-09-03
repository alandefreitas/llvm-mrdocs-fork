//===- Graph.h - PBQP Graph -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// PBQP Graph class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_PBQP_GRAPH_H
#define LLVM_CODEGEN_PBQP_GRAPH_H

#include "llvm/ADT/STLExtras.h"
#include <algorithm>
#include <cassert>
#include <iterator>
#include <limits>
#include <vector>

namespace llvm {
namespace PBQP {

  /// Base class providing shared node and edge identifier types for PBQP graphs.
  class GraphBase {
  public:
    /// Identifier type for nodes in a PBQP graph.
    using NodeId = unsigned;
    /// Identifier type for edges in a PBQP graph.
    using EdgeId = unsigned;

    /// Returns a value representing an invalid (non-existent) node.
    /// @return An invalid node id.
    static NodeId invalidNodeId() {
      return std::numeric_limits<NodeId>::max();
    }

    /// Returns a value representing an invalid (non-existent) edge.
    /// @return An invalid edge id.
    static EdgeId invalidEdgeId() {
      return std::numeric_limits<EdgeId>::max();
    }
  };

  /// PBQP Graph class.
  /// Instances of this class describe PBQP problems.
  ///
  template <typename SolverT>
  class Graph : public GraphBase {
  private:
    using CostAllocator = typename SolverT::CostAllocator;

  public:
    /// Unallocated cost vector type from the solver.
    using RawVector = typename SolverT::RawVector;
    /// Unallocated cost matrix type from the solver.
    using RawMatrix = typename SolverT::RawMatrix;
    /// Allocated cost vector type from the solver.
    using Vector = typename SolverT::Vector;
    /// Allocated cost matrix type from the solver.
    using Matrix = typename SolverT::Matrix;
    /// Shared pointer type for cost vectors from the cost allocator.
    using VectorPtr = typename CostAllocator::VectorPtr;
    /// Shared pointer type for cost matrices from the cost allocator.
    using MatrixPtr = typename CostAllocator::MatrixPtr;
    /// Per-node metadata type from the solver.
    using NodeMetadata = typename SolverT::NodeMetadata;
    /// Per-edge metadata type from the solver.
    using EdgeMetadata = typename SolverT::EdgeMetadata;
    /// Graph-level metadata type from the solver.
    using GraphMetadata = typename SolverT::GraphMetadata;

  private:
    class NodeEntry {
    public:
      using AdjEdgeList = std::vector<EdgeId>;
      using AdjEdgeIdx = AdjEdgeList::size_type;
      using AdjEdgeItr = AdjEdgeList::const_iterator;

      NodeEntry(VectorPtr Costs) : Costs(std::move(Costs)) {}

      static AdjEdgeIdx getInvalidAdjEdgeIdx() {
        return std::numeric_limits<AdjEdgeIdx>::max();
      }

      AdjEdgeIdx addAdjEdgeId(EdgeId EId) {
        AdjEdgeIdx Idx = AdjEdgeIds.size();
        AdjEdgeIds.push_back(EId);
        return Idx;
      }

      void removeAdjEdgeId(Graph &G, NodeId ThisNId, AdjEdgeIdx Idx) {
        // Swap-and-pop for fast removal.
        //   1) Update the adj index of the edge currently at back().
        //   2) Move last Edge down to Idx.
        //   3) pop_back()
        // If Idx == size() - 1 then the setAdjEdgeIdx and swap are
        // redundant, but both operations are cheap.
        G.getEdge(AdjEdgeIds.back()).setAdjEdgeIdx(ThisNId, Idx);
        AdjEdgeIds[Idx] = AdjEdgeIds.back();
        AdjEdgeIds.pop_back();
      }

      const AdjEdgeList& getAdjEdgeIds() const { return AdjEdgeIds; }

      VectorPtr Costs;
      NodeMetadata Metadata;

    private:
      AdjEdgeList AdjEdgeIds;
    };

    class EdgeEntry {
    public:
      EdgeEntry(NodeId N1Id, NodeId N2Id, MatrixPtr Costs)
          : Costs(std::move(Costs)) {
        NIds[0] = N1Id;
        NIds[1] = N2Id;
        ThisEdgeAdjIdxs[0] = NodeEntry::getInvalidAdjEdgeIdx();
        ThisEdgeAdjIdxs[1] = NodeEntry::getInvalidAdjEdgeIdx();
      }

      void connectToN(Graph &G, EdgeId ThisEdgeId, unsigned NIdx) {
        assert(ThisEdgeAdjIdxs[NIdx] == NodeEntry::getInvalidAdjEdgeIdx() &&
               "Edge already connected to NIds[NIdx].");
        NodeEntry &N = G.getNode(NIds[NIdx]);
        ThisEdgeAdjIdxs[NIdx] = N.addAdjEdgeId(ThisEdgeId);
      }

      void connect(Graph &G, EdgeId ThisEdgeId) {
        connectToN(G, ThisEdgeId, 0);
        connectToN(G, ThisEdgeId, 1);
      }

      void setAdjEdgeIdx(NodeId NId, typename NodeEntry::AdjEdgeIdx NewIdx) {
        if (NId == NIds[0])
          ThisEdgeAdjIdxs[0] = NewIdx;
        else {
          assert(NId == NIds[1] && "Edge not connected to NId");
          ThisEdgeAdjIdxs[1] = NewIdx;
        }
      }

      void disconnectFromN(Graph &G, unsigned NIdx) {
        assert(ThisEdgeAdjIdxs[NIdx] != NodeEntry::getInvalidAdjEdgeIdx() &&
               "Edge not connected to NIds[NIdx].");
        NodeEntry &N = G.getNode(NIds[NIdx]);
        N.removeAdjEdgeId(G, NIds[NIdx], ThisEdgeAdjIdxs[NIdx]);
        ThisEdgeAdjIdxs[NIdx] = NodeEntry::getInvalidAdjEdgeIdx();
      }

      void disconnectFrom(Graph &G, NodeId NId) {
        if (NId == NIds[0])
          disconnectFromN(G, 0);
        else {
          assert(NId == NIds[1] && "Edge does not connect NId");
          disconnectFromN(G, 1);
        }
      }

      NodeId getN1Id() const { return NIds[0]; }
      NodeId getN2Id() const { return NIds[1]; }

      MatrixPtr Costs;
      EdgeMetadata Metadata;

    private:
      NodeId NIds[2];
      typename NodeEntry::AdjEdgeIdx ThisEdgeAdjIdxs[2];
    };

    // ----- MEMBERS -----

    GraphMetadata Metadata;
    CostAllocator CostAlloc;
    SolverT *Solver = nullptr;

    using NodeVector = std::vector<NodeEntry>;
    using FreeNodeVector = std::vector<NodeId>;
    NodeVector Nodes;
    FreeNodeVector FreeNodeIds;

    using EdgeVector = std::vector<EdgeEntry>;
    using FreeEdgeVector = std::vector<EdgeId>;
    EdgeVector Edges;
    FreeEdgeVector FreeEdgeIds;

    Graph(const Graph &Other) {}

    // ----- INTERNAL METHODS -----

    NodeEntry &getNode(NodeId NId) {
      assert(NId < Nodes.size() && "Out of bound NodeId");
      return Nodes[NId];
    }
    const NodeEntry &getNode(NodeId NId) const {
      assert(NId < Nodes.size() && "Out of bound NodeId");
      return Nodes[NId];
    }

    EdgeEntry& getEdge(EdgeId EId) { return Edges[EId]; }
    const EdgeEntry& getEdge(EdgeId EId) const { return Edges[EId]; }

    NodeId addConstructedNode(NodeEntry N) {
      NodeId NId = 0;
      if (!FreeNodeIds.empty()) {
        NId = FreeNodeIds.back();
        FreeNodeIds.pop_back();
        Nodes[NId] = std::move(N);
      } else {
        NId = Nodes.size();
        Nodes.push_back(std::move(N));
      }
      return NId;
    }

    EdgeId addConstructedEdge(EdgeEntry E) {
      assert(findEdge(E.getN1Id(), E.getN2Id()) == invalidEdgeId() &&
             "Attempt to add duplicate edge.");
      EdgeId EId = 0;
      if (!FreeEdgeIds.empty()) {
        EId = FreeEdgeIds.back();
        FreeEdgeIds.pop_back();
        Edges[EId] = std::move(E);
      } else {
        EId = Edges.size();
        Edges.push_back(std::move(E));
      }

      EdgeEntry &NE = getEdge(EId);

      // Add the edge to the adjacency sets of its nodes.
      NE.connect(*this, EId);
      return EId;
    }

    void operator=(const Graph &Other) {}

  public:
    /// Iterator over edge ids adjacent to a node.
    using AdjEdgeItr = typename NodeEntry::AdjEdgeItr;

    /// Forward iterator over in-use node ids in the graph.
    class NodeItr {
    public:
      /// Iterator category tag for this forward iterator.
      using iterator_category = std::forward_iterator_tag;
      /// Type of the value obtained by dereferencing this iterator.
      using value_type = NodeId;
      /// Type used to represent distances between iterators.
      using difference_type = int;
      /// Pointer type for the iterated value.
      using pointer = NodeId *;
      /// Reference type for the iterated value.
      using reference = NodeId &;

      /// Construct an iterator starting at \p CurNId within graph \p G.
      /// @param CurNId Candidate starting node id (advanced to the next in-use id).
      /// @param G Graph whose nodes are iterated.
      NodeItr(NodeId CurNId, const Graph &G)
        : CurNId(CurNId), EndNId(G.Nodes.size()), FreeNodeIds(G.FreeNodeIds) {
        this->CurNId = findNextInUse(CurNId); // Move to first in-use node id
      }

      /// Return true if this iterator refers to the same node as \p O.
      /// @param O Other iterator to compare.
      /// @return True if both iterators refer to the same node.
      bool operator==(const NodeItr &O) const { return CurNId == O.CurNId; }
      /// Return true if this iterator does not refer to the same node as \p O.
      /// @param O Other iterator to compare.
      /// @return True if the iterators refer to different nodes.
      bool operator!=(const NodeItr &O) const { return !(*this == O); }
      /// Advance to the next in-use node id.
      /// @return Reference to this iterator after advancing.
      NodeItr& operator++() { CurNId = findNextInUse(++CurNId); return *this; }
      /// Return the current node id.
      /// @return The node id currently referred to by this iterator.
      NodeId operator*() const { return CurNId; }

    private:
      NodeId findNextInUse(NodeId NId) const {
        while (NId < EndNId && is_contained(FreeNodeIds, NId)) {
          ++NId;
        }
        return NId;
      }

      NodeId CurNId, EndNId;
      const FreeNodeVector &FreeNodeIds;
    };

    /// Forward iterator over in-use edge ids in the graph.
    class EdgeItr {
    public:
      /// Construct an iterator starting at \p CurEId within graph \p G.
      /// @param CurEId Candidate starting edge id (advanced to the next in-use id).
      /// @param G Graph whose edges are iterated.
      EdgeItr(EdgeId CurEId, const Graph &G)
        : CurEId(CurEId), EndEId(G.Edges.size()), FreeEdgeIds(G.FreeEdgeIds) {
        this->CurEId = findNextInUse(CurEId); // Move to first in-use edge id
      }

      /// Return true if this iterator refers to the same edge as \p O.
      /// @param O Other iterator to compare.
      /// @return True if both iterators refer to the same edge.
      bool operator==(const EdgeItr &O) const { return CurEId == O.CurEId; }
      /// Return true if this iterator does not refer to the same edge as \p O.
      /// @param O Other iterator to compare.
      /// @return True if the iterators refer to different edges.
      bool operator!=(const EdgeItr &O) const { return !(*this == O); }
      /// Advance to the next in-use edge id.
      /// @return Reference to this iterator after advancing.
      EdgeItr& operator++() { CurEId = findNextInUse(++CurEId); return *this; }
      /// Return the current edge id.
      /// @return The edge id currently referred to by this iterator.
      EdgeId operator*() const { return CurEId; }

    private:
      EdgeId findNextInUse(EdgeId EId) const {
        while (EId < EndEId && is_contained(FreeEdgeIds, EId)) {
          ++EId;
        }
        return EId;
      }

      EdgeId CurEId, EndEId;
      const FreeEdgeVector &FreeEdgeIds;
    };

    /// Range of in-use node ids in a graph.
    class NodeIdSet {
    public:
      /// Construct a node-id set view over graph \p G.
      /// @param G Graph whose node ids are exposed.
      NodeIdSet(const Graph &G) : G(G) {}

      /// Return an iterator to the first in-use node id.
      /// @return Iterator to the first in-use node id.
      NodeItr begin() const { return NodeItr(0, G); }
      /// Return an iterator past the last node id.
      /// @return Iterator past the last node id.
      NodeItr end() const { return NodeItr(G.Nodes.size(), G); }

      /// Return true if the graph has no nodes.
      /// @return True if the graph has no nodes.
      bool empty() const { return G.Nodes.empty(); }

      /// Return the number of in-use nodes.
      /// @return Number of in-use nodes in the graph.
      typename NodeVector::size_type size() const {
        return G.Nodes.size() - G.FreeNodeIds.size();
      }

    private:
      const Graph& G;
    };

    /// Range of in-use edge ids in a graph.
    class EdgeIdSet {
    public:
      /// Construct an edge-id set view over graph \p G.
      /// @param G Graph whose edge ids are exposed.
      EdgeIdSet(const Graph &G) : G(G) {}

      /// Return an iterator to the first in-use edge id.
      /// @return Iterator to the first in-use edge id.
      EdgeItr begin() const { return EdgeItr(0, G); }
      /// Return an iterator past the last edge id.
      /// @return Iterator past the last edge id.
      EdgeItr end() const { return EdgeItr(G.Edges.size(), G); }

      /// Return true if the graph has no edges.
      /// @return True if the graph has no edges.
      bool empty() const { return G.Edges.empty(); }

      /// Return the number of in-use edges.
      /// @return Number of in-use edges in the graph.
      typename NodeVector::size_type size() const {
        return G.Edges.size() - G.FreeEdgeIds.size();
      }

    private:
      const Graph& G;
    };

    /// Range of edge ids adjacent to a single node.
    class AdjEdgeIdSet {
    public:
      /// Construct an adjacent-edge set view for node entry \p NE.
      /// @param NE Node whose adjacent edge ids are exposed.
      AdjEdgeIdSet(const NodeEntry &NE) : NE(NE) {}

      /// Return an iterator to the first adjacent edge id.
      /// @return Iterator to the first adjacent edge id.
      typename NodeEntry::AdjEdgeItr begin() const {
        return NE.getAdjEdgeIds().begin();
      }

      /// Return an iterator past the last adjacent edge id.
      /// @return Iterator past the last adjacent edge id.
      typename NodeEntry::AdjEdgeItr end() const {
        return NE.getAdjEdgeIds().end();
      }

      /// Return true if the node has no adjacent edges.
      /// @return True if the node has no adjacent edges.
      bool empty() const { return NE.getAdjEdgeIds().empty(); }

      /// Return the number of adjacent edges.
      /// @return Number of edges adjacent to the node.
      typename NodeEntry::AdjEdgeList::size_type size() const {
        return NE.getAdjEdgeIds().size();
      }

    private:
      const NodeEntry &NE;
    };

    /// Construct an empty PBQP graph.
    Graph() = default;

    /// Construct an empty PBQP graph with the given graph metadata.
    /// @param Metadata Graph-level metadata to store.
    Graph(GraphMetadata Metadata) : Metadata(std::move(Metadata)) {}

    /// Get a reference to the graph metadata.
    /// @return Mutable reference to the graph metadata.
    GraphMetadata& getMetadata() { return Metadata; }

    /// Get a const-reference to the graph metadata.
    /// @return Const reference to the graph metadata.
    const GraphMetadata& getMetadata() const { return Metadata; }

    /// Lock this graph to the given solver instance.
    ///
    /// This method will call solver.handleAddNode for each node in the graph,
    /// and handleAddEdge for each edge, to give the solver an opportunity to
    /// set up any requried metadata.
    /// @param S Solver instance to attach to this graph.
    void setSolver(SolverT &S) {
      assert(!Solver && "Solver already set. Call unsetSolver().");
      Solver = &S;
      for (auto NId : nodeIds())
        Solver->handleAddNode(NId);
      for (auto EId : edgeIds())
        Solver->handleAddEdge(EId);
    }

    /// Release from solver instance.
    void unsetSolver() {
      assert(Solver && "Solver not set.");
      Solver = nullptr;
    }

    /// Add a node with the given costs.
    /// @param Costs Cost vector for the new node.
    /// @return Node iterator for the added node.
    template <typename OtherVectorT>
    NodeId addNode(OtherVectorT Costs) {
      // Get cost vector from the problem domain
      VectorPtr AllocatedCosts = CostAlloc.getVector(std::move(Costs));
      NodeId NId = addConstructedNode(NodeEntry(AllocatedCosts));
      if (Solver)
        Solver->handleAddNode(NId);
      return NId;
    }

    /// Add a node bypassing the cost allocator.
    /// @param Costs Cost vector ptr for the new node (must be convertible to
    ///        VectorPtr).
    /// @return Node iterator for the added node.
    ///
    ///   This method allows for fast addition of a node whose costs don't need
    /// to be passed through the cost allocator. The most common use case for
    /// this is when duplicating costs from an existing node (when using a
    /// pooling allocator). These have already been uniqued, so we can avoid
    /// re-constructing and re-uniquing them by attaching them directly to the
    /// new node.
    template <typename OtherVectorPtrT>
    NodeId addNodeBypassingCostAllocator(OtherVectorPtrT Costs) {
      NodeId NId = addConstructedNode(NodeEntry(Costs));
      if (Solver)
        Solver->handleAddNode(NId);
      return NId;
    }

    /// Add an edge between the given nodes with the given costs.
    /// @param N1Id First node.
    /// @param N2Id Second node.
    /// @param Costs Cost matrix for new edge.
    /// @return Edge iterator for the added edge.
    template <typename OtherVectorT>
    EdgeId addEdge(NodeId N1Id, NodeId N2Id, OtherVectorT Costs) {
      assert(getNodeCosts(N1Id).getLength() == Costs.getRows() &&
             getNodeCosts(N2Id).getLength() == Costs.getCols() &&
             "Matrix dimensions mismatch.");
      // Get cost matrix from the problem domain.
      MatrixPtr AllocatedCosts = CostAlloc.getMatrix(std::move(Costs));
      EdgeId EId = addConstructedEdge(EdgeEntry(N1Id, N2Id, AllocatedCosts));
      if (Solver)
        Solver->handleAddEdge(EId);
      return EId;
    }

    /// Add an edge bypassing the cost allocator.
    /// @param N1Id First node.
    /// @param N2Id Second node.
    /// @param Costs Cost matrix for new edge.
    /// @return Edge iterator for the added edge.
    ///
    ///   This method allows for fast addition of an edge whose costs don't need
    /// to be passed through the cost allocator. The most common use case for
    /// this is when duplicating costs from an existing edge (when using a
    /// pooling allocator). These have already been uniqued, so we can avoid
    /// re-constructing and re-uniquing them by attaching them directly to the
    /// new edge.
    template <typename OtherMatrixPtrT>
    NodeId addEdgeBypassingCostAllocator(NodeId N1Id, NodeId N2Id,
                                         OtherMatrixPtrT Costs) {
      assert(getNodeCosts(N1Id).getLength() == Costs->getRows() &&
             getNodeCosts(N2Id).getLength() == Costs->getCols() &&
             "Matrix dimensions mismatch.");
      // Get cost matrix from the problem domain.
      EdgeId EId = addConstructedEdge(EdgeEntry(N1Id, N2Id, Costs));
      if (Solver)
        Solver->handleAddEdge(EId);
      return EId;
    }

    /// Returns true if the graph is empty.
    /// @return True if the graph has no nodes.
    bool empty() const { return NodeIdSet(*this).empty(); }

    /// Return the set of in-use node ids in this graph.
    /// @return Set of in-use node ids.
    NodeIdSet nodeIds() const { return NodeIdSet(*this); }
    /// Return the set of in-use edge ids in this graph.
    /// @return Set of in-use edge ids.
    EdgeIdSet edgeIds() const { return EdgeIdSet(*this); }

    /// Return the set of edge ids adjacent to node \p NId.
    /// @param NId Node whose adjacent edges are requested.
    /// @return Set of edge ids adjacent to \p NId.
    AdjEdgeIdSet adjEdgeIds(NodeId NId) { return AdjEdgeIdSet(getNode(NId)); }

    /// Get the number of nodes in the graph.
    /// @return Number of nodes in the graph.
    unsigned getNumNodes() const { return NodeIdSet(*this).size(); }

    /// Get the number of edges in the graph.
    /// @return Number of edges in the graph.
    unsigned getNumEdges() const { return EdgeIdSet(*this).size(); }

    /// Set a node's cost vector.
    /// @param NId Node to update.
    /// @param Costs New costs to set.
    template <typename OtherVectorT>
    void setNodeCosts(NodeId NId, OtherVectorT Costs) {
      VectorPtr AllocatedCosts = CostAlloc.getVector(std::move(Costs));
      if (Solver)
        Solver->handleSetNodeCosts(NId, *AllocatedCosts);
      getNode(NId).Costs = AllocatedCosts;
    }

    /// Get a VectorPtr to a node's cost vector. Rarely useful - use
    ///        getNodeCosts where possible.
    /// @param NId Node id.
    /// @return VectorPtr to node cost vector.
    ///
    ///   This method is primarily useful for duplicating costs quickly by
    /// bypassing the cost allocator. See addNodeBypassingCostAllocator. Prefer
    /// getNodeCosts when dealing with node cost values.
    const VectorPtr& getNodeCostsPtr(NodeId NId) const {
      return getNode(NId).Costs;
    }

    /// Get a node's cost vector.
    /// @param NId Node id.
    /// @return Node cost vector.
    const Vector& getNodeCosts(NodeId NId) const {
      return *getNodeCostsPtr(NId);
    }

    /// Get a mutable reference to a node's metadata.
    /// @param NId Node id.
    /// @return Reference to the node's metadata.
    NodeMetadata& getNodeMetadata(NodeId NId) {
      return getNode(NId).Metadata;
    }

    /// Get a const reference to a node's metadata.
    /// @param NId Node id.
    /// @return Const reference to the node's metadata.
    const NodeMetadata& getNodeMetadata(NodeId NId) const {
      return getNode(NId).Metadata;
    }

    /// Get the number of edges adjacent to a node.
    /// @param NId Node id.
    /// @return Degree of the node.
    typename NodeEntry::AdjEdgeList::size_type getNodeDegree(NodeId NId) const {
      return getNode(NId).getAdjEdgeIds().size();
    }

    /// Update an edge's cost matrix.
    /// @param EId Edge id.
    /// @param Costs New cost matrix.
    template <typename OtherMatrixT>
    void updateEdgeCosts(EdgeId EId, OtherMatrixT Costs) {
      MatrixPtr AllocatedCosts = CostAlloc.getMatrix(std::move(Costs));
      if (Solver)
        Solver->handleUpdateCosts(EId, *AllocatedCosts);
      getEdge(EId).Costs = AllocatedCosts;
    }

    /// Get a MatrixPtr to a node's cost matrix. Rarely useful - use
    ///        getEdgeCosts where possible.
    /// @param EId Edge id.
    /// @return MatrixPtr to edge cost matrix.
    ///
    ///   This method is primarily useful for duplicating costs quickly by
    /// bypassing the cost allocator. See addNodeBypassingCostAllocator. Prefer
    /// getEdgeCosts when dealing with edge cost values.
    const MatrixPtr& getEdgeCostsPtr(EdgeId EId) const {
      return getEdge(EId).Costs;
    }

    /// Get an edge's cost matrix.
    /// @param EId Edge id.
    /// @return Edge cost matrix.
    const Matrix& getEdgeCosts(EdgeId EId) const {
      return *getEdge(EId).Costs;
    }

    /// Get a mutable reference to an edge's metadata.
    /// @param EId Edge id.
    /// @return Reference to the edge's metadata.
    EdgeMetadata& getEdgeMetadata(EdgeId EId) {
      return getEdge(EId).Metadata;
    }

    /// Get a const reference to an edge's metadata.
    /// @param EId Edge id.
    /// @return Const reference to the edge's metadata.
    const EdgeMetadata& getEdgeMetadata(EdgeId EId) const {
      return getEdge(EId).Metadata;
    }

    /// Get the first node connected to this edge.
    /// @param EId Edge id.
    /// @return The first node connected to the given edge.
    NodeId getEdgeNode1Id(EdgeId EId) const {
      return getEdge(EId).getN1Id();
    }

    /// Get the second node connected to this edge.
    /// @param EId Edge id.
    /// @return The second node connected to the given edge.
    NodeId getEdgeNode2Id(EdgeId EId) const {
      return getEdge(EId).getN2Id();
    }

    /// Get the "other" node connected to this edge.
    /// @param EId Edge id.
    /// @param NId Node id for the "given" node.
    /// @return The iterator for the "other" node connected to this edge.
    NodeId getEdgeOtherNodeId(EdgeId EId, NodeId NId) {
      EdgeEntry &E = getEdge(EId);
      if (E.getN1Id() == NId) {
        return E.getN2Id();
      } // else
      return E.getN1Id();
    }

    /// Get the edge connecting two nodes.
    /// @param N1Id First node id.
    /// @param N2Id Second node id.
    /// @return An id for edge (N1Id, N2Id) if such an edge exists,
    ///         otherwise returns an invalid edge id.
    EdgeId findEdge(NodeId N1Id, NodeId N2Id) {
      for (auto AEId : adjEdgeIds(N1Id)) {
        if ((getEdgeNode1Id(AEId) == N2Id) ||
            (getEdgeNode2Id(AEId) == N2Id)) {
          return AEId;
        }
      }
      return invalidEdgeId();
    }

    /// Remove a node from the graph.
    /// @param NId Node id.
    void removeNode(NodeId NId) {
      if (Solver)
        Solver->handleRemoveNode(NId);
      NodeEntry &N = getNode(NId);
      // TODO: Can this be for-each'd?
      for (AdjEdgeItr AEItr = N.adjEdgesBegin(),
             AEEnd = N.adjEdgesEnd();
           AEItr != AEEnd;) {
        EdgeId EId = *AEItr;
        ++AEItr;
        removeEdge(EId);
      }
      FreeNodeIds.push_back(NId);
    }

    /// Disconnect an edge from the given node.
    ///
    /// Removes the given edge from the adjacency list of the given node.
    /// This operation leaves the edge in an 'asymmetric' state: It will no
    /// longer appear in an iteration over the given node's (NId's) edges, but
    /// will appear in an iteration over the 'other', unnamed node's edges.
    ///
    /// This does not correspond to any normal graph operation, but exists to
    /// support efficient PBQP graph-reduction based solvers. It is used to
    /// 'effectively' remove the unnamed node from the graph while the solver
    /// is performing the reduction. The solver will later call reconnectNode
    /// to restore the edge in the named node's adjacency list.
    ///
    /// Since the degree of a node is the number of connected edges,
    /// disconnecting an edge from a node 'u' will cause the degree of 'u' to
    /// drop by 1.
    ///
    /// A disconnected edge WILL still appear in an iteration over the graph
    /// edges.
    ///
    /// A disconnected edge should not be removed from the graph, it should be
    /// reconnected first.
    ///
    /// A disconnected edge can be reconnected by calling the reconnectEdge
    /// method.
    /// @param EId Edge to disconnect.
    /// @param NId Node from whose adjacency list the edge is removed.
    void disconnectEdge(EdgeId EId, NodeId NId) {
      if (Solver)
        Solver->handleDisconnectEdge(EId, NId);

      EdgeEntry &E = getEdge(EId);
      E.disconnectFrom(*this, NId);
    }

    /// Convenience method to disconnect all neighbours from the given
    ///        node.
    /// @param NId Node whose neighbours should be disconnected.
    void disconnectAllNeighborsFromNode(NodeId NId) {
      for (auto AEId : adjEdgeIds(NId))
        disconnectEdge(AEId, getEdgeOtherNodeId(AEId, NId));
    }

    /// Re-attach an edge to its nodes.
    ///
    /// Adds an edge that had been previously disconnected back into the
    /// adjacency set of the nodes that the edge connects.
    /// @param EId Edge to reconnect.
    /// @param NId Node whose adjacency list should regain the edge.
    void reconnectEdge(EdgeId EId, NodeId NId) {
      EdgeEntry &E = getEdge(EId);
      E.connectTo(*this, EId, NId);
      if (Solver)
        Solver->handleReconnectEdge(EId, NId);
    }

    /// Remove an edge from the graph.
    /// @param EId Edge id.
    void removeEdge(EdgeId EId) {
      if (Solver)
        Solver->handleRemoveEdge(EId);
      EdgeEntry &E = getEdge(EId);
      E.disconnect();
      FreeEdgeIds.push_back(EId);
      Edges[EId].invalidate();
    }

    /// Remove all nodes and edges from the graph.
    void clear() {
      Nodes.clear();
      FreeNodeIds.clear();
      Edges.clear();
      FreeEdgeIds.clear();
    }
  };

} // end namespace PBQP
} // end namespace llvm

#endif // LLVM_CODEGEN_PBQP_GRAPH_H
