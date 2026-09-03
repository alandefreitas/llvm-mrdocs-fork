//===- llvm/Analysis/DependenceGraphBuilder.h -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a builder interface that can be used to populate dependence
// graphs such as DDG and PDG.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_DEPENDENCEGRAPHBUILDER_H
#define LLVM_ANALYSIS_DEPENDENCEGRAPHBUILDER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/EquivalenceClasses.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class BasicBlock;
class DependenceInfo;
class Instruction;

/// Abstract builder defining high-level steps for creating DDG-like graphs.
///
/// This abstract builder class defines a set of high-level steps for creating
/// DDG-like graphs. The client code is expected to inherit from this class and
/// define concrete implementation for each of the pure virtual functions used
/// in the high-level algorithm.
template <class GraphType> class LLVM_ABI AbstractDependenceGraphBuilder {
protected:
  /// List of basic blocks considered when building the graph.
  using BasicBlockListType = SmallVectorImpl<BasicBlock *>;

private:
  using NodeType = typename GraphType::NodeType;
  using EdgeType = typename GraphType::EdgeType;

public:
  /// Equivalence classes of basic blocks.
  using ClassesType = EquivalenceClasses<BasicBlock *>;
  /// Small vector of graph node pointers.
  using NodeListType = SmallVector<NodeType *, 4>;

  /// Construct a builder for graph \p G using dependence info \p D and blocks
  /// \p BBs.
  /// @param G Graph being populated by this builder.
  /// @param D Dependence information used when creating memory edges.
  /// @param BBs Basic blocks considered when building the graph.
  AbstractDependenceGraphBuilder(GraphType &G, DependenceInfo &D,
                                 const BasicBlockListType &BBs)
      : Graph(G), DI(D), BBList(BBs) {}
  /// Destroy this dependence graph builder.
  virtual ~AbstractDependenceGraphBuilder() = default;

  /// The main entry to the graph construction algorithm.
  ///
  /// It starts by creating nodes in increasing order of granularity and then
  /// adds def-use and memory edges. As one of the final stages, it also creates
  /// pi-block nodes to facilitate codegen in transformations that use dependence
  /// graphs.
  void populate() {
    computeInstructionOrdinals();
    createFineGrainedNodes();
    createDefUseEdges();
    createMemoryDependencyEdges();
    simplify();
    createAndConnectRootNode();
    createPiBlocks();
    sortNodesTopologically();
  }

  /// Compute ordinal numbers for each instruction and store them in a map.
  ///
  /// These ordinals are used to compute node ordinals which are in turn used to
  /// order nodes that are part of a cycle. Instruction ordinals are assigned
  /// based on lexical program order.
  void computeInstructionOrdinals();

  /// Create fine grained nodes. These are typically atomic nodes that
  /// consist of a single instruction.
  void createFineGrainedNodes();

  /// Analyze the def-use chains and create edges from the nodes containing
  /// definitions to the nodes containing the uses.
  void createDefUseEdges();

  /// Analyze data dependencies that exist between memory loads or stores,
  /// in the graph nodes and create edges between them.
  void createMemoryDependencyEdges();

  /// Create a root node and add edges such that each node in the graph is
  /// reachable from the root.
  void createAndConnectRootNode();

  /// Create pi-block nodes from strongly connected components.
  ///
  /// Apply graph abstraction to groups of nodes that belong to a strongly
  /// connected component of the graph to create larger compound nodes
  /// called pi-blocks. The purpose of this abstraction is to isolate sets of
  /// program elements that need to stay together during codegen and turn
  /// the dependence graph into an acyclic graph.
  void createPiBlocks();

  /// Collapse adjacent def-use pairs in the same basic block.
  ///
  /// Go through all the nodes in the graph and collapse any two nodes 'a' and
  /// 'b' if all of the following are true:
  ///   - the only edge from 'a' is a def-use edge to 'b' and
  ///   - the only edge to 'b' is a def-use edge from 'a' and
  ///   - there is no cyclic edge from 'b' to 'a' and
  ///   - all instructions in 'a' and 'b' belong to the same basic block and
  ///   - both 'a' and 'b' are simple (single or multi instruction) nodes.
  void simplify();

  /// Topologically sort the graph nodes.
  void sortNodesTopologically();

protected:
  /// Create the root node of the graph.
  /// @return Newly created root node of the graph.
  virtual NodeType &createRootNode() = 0;

  /// Create an atomic node in the graph given a single instruction.
  /// @param I Instruction represented by the new fine-grained node.
  /// @return Newly created fine-grained node for \p I.
  virtual NodeType &createFineGrainedNode(Instruction &I) = 0;

  /// Create a pi-block node in the graph representing a group of nodes in an
  /// SCC of the graph.
  /// @param L Nodes that belong to the SCC and become members of the pi-block.
  /// @return Newly created pi-block node containing the nodes in \p L.
  virtual NodeType &createPiBlock(const NodeListType &L) = 0;

  /// Create a def-use edge going from \p Src to \p Tgt.
  /// @param Src Source node of the def-use edge.
  /// @param Tgt Target node of the def-use edge.
  /// @return Newly created def-use edge from \p Src to \p Tgt.
  virtual EdgeType &createDefUseEdge(NodeType &Src, NodeType &Tgt) = 0;

  /// Create a memory dependence edge going from \p Src to \p Tgt.
  /// @param Src Source node of the memory dependence edge.
  /// @param Tgt Target node of the memory dependence edge.
  /// @return Newly created memory dependence edge from \p Src to \p Tgt.
  virtual EdgeType &createMemoryEdge(NodeType &Src, NodeType &Tgt) = 0;

  /// Create a rooted edge going from \p Src to \p Tgt.
  /// @param Src Source node of the rooted edge.
  /// @param Tgt Target node of the rooted edge.
  /// @return Newly created rooted edge from \p Src to \p Tgt.
  virtual EdgeType &createRootedEdge(NodeType &Src, NodeType &Tgt) = 0;

  /// Given a pi-block node, return a vector of all the nodes contained within
  /// it.
  /// @param N Pi-block node whose member nodes are requested.
  /// @return Nodes contained within pi-block \p N.
  virtual const NodeListType &getNodesInPiBlock(const NodeType &N) = 0;

  /// Deallocate memory of edge \p E.
  /// @param E Edge to destroy.
  virtual void destroyEdge(EdgeType &E) { delete &E; }

  /// Deallocate memory of node \p N.
  /// @param N Node to destroy.
  virtual void destroyNode(NodeType &N) { delete &N; }

  /// Return true if creation of pi-blocks are supported and desired,
  /// and false otherwise.
  /// @return True if pi-block creation is supported and desired.
  virtual bool shouldCreatePiBlocks() const { return true; }

  /// Return true if graph simplification step is requested, and false
  /// otherwise.
  /// @return True if graph simplification is requested.
  virtual bool shouldSimplify() const { return true; }

  /// Return true if it's safe to merge the two nodes.
  /// @param A First node to consider for merging.
  /// @param B Second node to consider for merging.
  /// @return True if it is safe to merge \p A and \p B.
  virtual bool areNodesMergeable(const NodeType &A,
                                 const NodeType &B) const = 0;

  /// Append the content of node \p B into node \p A and remove \p B and
  /// the edge between \p A and \p B from the graph.
  /// @param A Destination node that absorbs the contents of \p B.
  /// @param B Source node whose contents are merged into \p A.
  virtual void mergeNodes(NodeType &A, NodeType &B) = 0;

  /// Given an instruction \p I return its associated ordinal number.
  /// @param I Instruction whose ordinal is requested.
  /// @return Ordinal number associated with \p I.
  size_t getOrdinal(Instruction &I) {
    assert(InstOrdinalMap.contains(&I) &&
           "No ordinal computed for this instruction.");
    return InstOrdinalMap[&I];
  }

  /// Given a node \p N return its associated ordinal number.
  /// @param N Node whose ordinal is requested.
  /// @return Ordinal number associated with \p N.
  size_t getOrdinal(NodeType &N) {
    assert(NodeOrdinalMap.contains(&N) && "No ordinal computed for this node.");
    return NodeOrdinalMap[&N];
  }

  /// Map types to map instructions to nodes used when populating the graph.
  using InstToNodeMap = DenseMap<Instruction *, NodeType *>;

  /// Map Types to map instruction/nodes to an ordinal number.
  using InstToOrdinalMap = DenseMap<Instruction *, size_t>;
  /// Map from graph nodes to ordinal numbers.
  using NodeToOrdinalMap = DenseMap<NodeType *, size_t>;

  /// Reference to the graph that gets built by a concrete implementation of
  /// this builder.
  GraphType &Graph;

  /// Dependence information used to create memory dependence edges in the
  /// graph.
  DependenceInfo &DI;

  /// The list of basic blocks to consider when building the graph.
  const BasicBlockListType &BBList;

  /// A mapping from instructions to the corresponding nodes in the graph.
  InstToNodeMap IMap;

  /// A mapping from each instruction to an ordinal number. This map is used to
  /// populate the \p NodeOrdinalMap.
  InstToOrdinalMap InstOrdinalMap;

  /// A mapping from nodes to an ordinal number. This map is used to sort nodes
  /// in a pi-block based on program order.
  NodeToOrdinalMap NodeOrdinalMap;
};

} // namespace llvm

#endif // LLVM_ANALYSIS_DEPENDENCEGRAPHBUILDER_H
