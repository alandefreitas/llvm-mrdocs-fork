//===- llvm/ADT/DirectedGraph.h - Directed Graph ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the interface and a base class implementation for a
/// directed graph.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_DIRECTEDGRAPH_H
#define LLVM_ADT_DIRECTEDGRAPH_H

#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

/// Represent an edge in the directed graph.
/// The edge contains the target node it connects to.
template <class NodeType, class EdgeType> class DGEdge {
public:
  /// Edges must be constructed with an explicit target node.
  DGEdge() = delete;
  /// Create an edge pointing to the given node \p N.
  explicit DGEdge(NodeType &N) : TargetNode(N) {}
  /// Copy an edge, sharing the same target node reference.
  /// @param E Edge to copy from.
  explicit DGEdge(const DGEdge<NodeType, EdgeType> &E)
      : TargetNode(E.TargetNode) {}
  /// Assign this edge to point at the same target as \p E.
  /// @param E Edge whose target is copied.
  DGEdge<NodeType, EdgeType> &operator=(const DGEdge<NodeType, EdgeType> &E) {
    TargetNode = E.TargetNode;
    return *this;
  }

  /// Static polymorphism: delegate implementation (via isEqualTo) to the
  /// derived class.
  bool operator==(const DGEdge &E) const {
    return getDerived().isEqualTo(E.getDerived());
  }
  /// Return true if this edge is not equal to \p E.
  /// @param E Edge to compare with.
  bool operator!=(const DGEdge &E) const { return !operator==(E); }

  /// Retrieve the target node this edge connects to.
  const NodeType &getTargetNode() const { return TargetNode; }
  /// Retrieve a mutable reference to the target node this edge connects to.
  NodeType &getTargetNode() {
    return const_cast<NodeType &>(
        static_cast<const DGEdge<NodeType, EdgeType> &>(*this).getTargetNode());
  }

  /// Set the target node this edge connects to.
  void setTargetNode(const NodeType &N) { TargetNode = N; }

protected:
  /// Default equality compares edge object identity by address.
  /// @param E Edge to compare with.
  bool isEqualTo(const EdgeType &E) const { return this == &E; }

  /// Cast this edge to the derived \c EdgeType.
  EdgeType &getDerived() { return *static_cast<EdgeType *>(this); }
  /// Cast this edge to the derived \c EdgeType (const).
  const EdgeType &getDerived() const {
    return *static_cast<const EdgeType *>(this);
  }

  /// The target node this edge connects to.
  NodeType &TargetNode;
};

/// Represent a node in the directed graph.
/// The node has a (possibly empty) list of outgoing edges.
template <class NodeType, class EdgeType> class DGNode {
public:
  /// Ordered unique container of outgoing edge pointers.
  using EdgeListTy = SetVector<EdgeType *>;
  /// Mutable iterator over outgoing edges.
  using iterator = typename EdgeListTy::iterator;
  /// Const iterator over outgoing edges.
  using const_iterator = typename EdgeListTy::const_iterator;

  /// Create a node with a single outgoing edge \p E.
  explicit DGNode(EdgeType &E) : Edges() { Edges.insert(&E); }
  /// Create a node with no outgoing edges.
  DGNode() = default;

  /// Static polymorphism: delegate implementation (via isEqualTo) to the
  /// derived class.
  friend bool operator==(const NodeType &M, const NodeType &N) {
    return M.isEqualTo(N);
  }
  /// Return true if nodes \p M and \p N are not equal.
  friend bool operator!=(const NodeType &M, const NodeType &N) {
    return !(M == N);
  }

  /// Return an iterator to the first outgoing edge.
  const_iterator begin() const { return Edges.begin(); }
  /// Return a past-the-end iterator over outgoing edges.
  const_iterator end() const { return Edges.end(); }
  /// Return a mutable iterator to the first outgoing edge.
  iterator begin() { return Edges.begin(); }
  /// Return a mutable past-the-end iterator over outgoing edges.
  iterator end() { return Edges.end(); }
  /// Return the first outgoing edge.
  const EdgeType &front() const { return *Edges.front(); }
  /// Return a mutable reference to the first outgoing edge.
  EdgeType &front() { return *Edges.front(); }
  /// Return the last outgoing edge.
  const EdgeType &back() const { return *Edges.back(); }
  /// Return a mutable reference to the last outgoing edge.
  EdgeType &back() { return *Edges.back(); }

  /// Collect in \p EL, all the edges from this node to \p N.
  /// Return true if at least one edge was found, and false otherwise.
  /// Note that this implementation allows more than one edge to connect
  /// a given pair of nodes.
  bool findEdgesTo(const NodeType &N, SmallVectorImpl<EdgeType *> &EL) const {
    assert(EL.empty() && "Expected the list of edges to be empty.");
    for (auto *E : Edges)
      if (E->getTargetNode() == N)
        EL.push_back(E);
    return !EL.empty();
  }

  /// Add the given edge \p E to this node, if it doesn't exist already. Returns
  /// true if the edge is added and false otherwise.
  bool addEdge(EdgeType &E) { return Edges.insert(&E); }

  /// Remove the given edge \p E from this node, if it exists.
  void removeEdge(EdgeType &E) { Edges.remove(&E); }

  /// Test whether there is an edge that goes from this node to \p N.
  bool hasEdgeTo(const NodeType &N) const {
    return (findEdgeTo(N) != Edges.end());
  }

  /// Retrieve the outgoing edges for the node.
  const EdgeListTy &getEdges() const { return Edges; }
  /// Retrieve a mutable list of outgoing edges for the node.
  EdgeListTy &getEdges() {
    return const_cast<EdgeListTy &>(
        static_cast<const DGNode<NodeType, EdgeType> &>(*this).Edges);
  }

  /// Clear the outgoing edges.
  void clear() { Edges.clear(); }

protected:
  /// Default equality compares node object identity by address.
  /// @param N Node to compare with.
  bool isEqualTo(const NodeType &N) const { return this == &N; }

  /// Cast this node to the derived \c NodeType.
  NodeType &getDerived() { return *static_cast<NodeType *>(this); }
  /// Cast this node to the derived \c NodeType (const).
  const NodeType &getDerived() const {
    return *static_cast<const NodeType *>(this);
  }

  /// Find an edge to \p N. If more than one edge exists, this will return
  /// the first one in the list of edges.
  const_iterator findEdgeTo(const NodeType &N) const {
    return llvm::find_if(
        Edges, [&N](const EdgeType *E) { return E->getTargetNode() == N; });
  }

  /// The list of outgoing edges.
  EdgeListTy Edges;
};

/// Directed graph
///
/// The graph is represented by a table of nodes.
/// Each node contains a (possibly empty) list of outgoing edges.
/// Each edge contains the target node it connects to.
template <class NodeType, class EdgeType> class DirectedGraph {
protected:
  /// Container of node pointers owned by the graph table.
  using NodeListTy = SmallVector<NodeType *, 10>;
  /// Scratch container of edge pointers used by graph queries.
  using EdgeListTy = SmallVector<EdgeType *, 10>;
public:
  /// Mutable iterator over nodes in the graph.
  using iterator = typename NodeListTy::iterator;
  /// Const iterator over nodes in the graph.
  using const_iterator = typename NodeListTy::const_iterator;
  /// Self type alias used by const-to-mutable helpers.
  using DGraphType = DirectedGraph<NodeType, EdgeType>;

  /// Create an empty directed graph.
  DirectedGraph() = default;
  /// Create a graph containing the single node \p N.
  /// @param N Node to insert.
  explicit DirectedGraph(NodeType &N) : Nodes() { addNode(N); }

  /// Return an iterator to the first node.
  const_iterator begin() const { return Nodes.begin(); }
  /// Return a past-the-end iterator over nodes.
  const_iterator end() const { return Nodes.end(); }
  /// Return a mutable iterator to the first node.
  iterator begin() { return Nodes.begin(); }
  /// Return a mutable past-the-end iterator over nodes.
  iterator end() { return Nodes.end(); }
  /// Return the first node in the graph table.
  const NodeType &front() const { return *Nodes.front(); }
  /// Return a mutable reference to the first node.
  NodeType &front() { return *Nodes.front(); }
  /// Return the last node in the graph table.
  const NodeType &back() const { return *Nodes.back(); }
  /// Return a mutable reference to the last node.
  NodeType &back() { return *Nodes.back(); }

  /// Return the number of nodes in the graph.
  size_t size() const { return Nodes.size(); }

  /// Find the given node \p N in the table.
  const_iterator findNode(const NodeType &N) const {
    return llvm::find_if(Nodes,
                         [&N](const NodeType *Node) { return *Node == N; });
  }
  /// Find the given node \p N and return a mutable iterator.
  /// @param N Node to look up.
  iterator findNode(const NodeType &N) {
    return const_cast<iterator>(
        static_cast<const DGraphType &>(*this).findNode(N));
  }

  /// Add the given node \p N to the graph if it is not already present.
  bool addNode(NodeType &N) {
    if (findNode(N) != Nodes.end())
      return false;
    Nodes.push_back(&N);
    return true;
  }

  /// Collect in \p EL all edges that are coming into node \p N. Return true
  /// if at least one edge was found, and false otherwise.
  bool findIncomingEdgesToNode(const NodeType &N, SmallVectorImpl<EdgeType*> &EL) const {
    assert(EL.empty() && "Expected the list of edges to be empty.");
    EdgeListTy TempList;
    for (auto *Node : Nodes) {
      if (*Node == N)
        continue;
      Node->findEdgesTo(N, TempList);
      llvm::append_range(EL, TempList);
      TempList.clear();
    }
    return !EL.empty();
  }

  /// Remove the given node \p N from the graph. If the node has incoming or
  /// outgoing edges, they are also removed. Return true if the node was found
  /// and then removed, and false if the node was not found in the graph to
  /// begin with.
  bool removeNode(NodeType &N) {
    iterator IT = findNode(N);
    if (IT == Nodes.end())
      return false;
    // Remove incoming edges.
    EdgeListTy EL;
    for (auto *Node : Nodes) {
      if (*Node == N)
        continue;
      Node->findEdgesTo(N, EL);
      for (auto *E : EL)
        Node->removeEdge(*E);
      EL.clear();
    }
    N.clear();
    Nodes.erase(IT);
    return true;
  }

  /// Assuming nodes \p Src and \p Dst are already in the graph, connect node \p
  /// Src to node \p Dst using the provided edge \p E. Return true if \p Src is
  /// not already connected to \p Dst via \p E, and false otherwise.
  bool connect(NodeType &Src, NodeType &Dst, EdgeType &E) {
    assert(findNode(Src) != Nodes.end() && "Src node should be present.");
    assert(findNode(Dst) != Nodes.end() && "Dst node should be present.");
    assert((E.getTargetNode() == Dst) &&
           "Target of the given edge does not match Dst.");
    return Src.addEdge(E);
  }

protected:
  /// The list of nodes in the graph.
  NodeListTy Nodes;
};

} // namespace llvm

#endif // LLVM_ADT_DIRECTEDGRAPH_H
