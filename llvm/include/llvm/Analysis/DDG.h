//===- llvm/Analysis/DDG.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the Data-Dependence Graph (DDG).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_DDG_H
#define LLVM_ANALYSIS_DDG_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DirectedGraph.h"
#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Analysis/DependenceGraphBuilder.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class Function;
class Loop;
class LoopInfo;
class DDGNode;
class DDGEdge;
/// Directed-graph node base type used by DDG nodes.
using DDGNodeBase = DGNode<DDGNode, DDGEdge>;
/// Directed-graph edge base type used by DDG edges.
using DDGEdgeBase = DGEdge<DDGNode, DDGEdge>;
/// Directed-graph base type for a data dependence graph.
using DDGBase = DirectedGraph<DDGNode, DDGEdge>;
class LPMUpdater;

/// Node in a data dependence graph.
///
/// The graph can represent the following types of nodes:
/// 1. Single instruction node containing just one instruction.
/// 2. Multiple instruction node where two or more instructions from
///    the same basic block are merged into one node.
/// 3. Pi-block node which is a group of other DDG nodes that are part of a
///    strongly-connected component of the graph.
///    A pi-block node contains more than one single or multiple instruction
///    nodes. The root node cannot be part of a pi-block.
/// 4. Root node is a special node that connects to all components such that
///    there is always a path from it to any node in the graph.
class LLVM_ABI DDGNode : public DDGNodeBase {
public:
  /// List of instruction pointers associated with a DDG node.
  using InstructionListType = SmallVectorImpl<Instruction *>;

  /// Kind of node stored in a DDG.
  enum class NodeKind {
    /// Uninitialized or invalid node kind.
    Unknown,
    /// Node that contains a single instruction.
    SingleInstruction,
    /// Node that contains two or more instructions from the same basic block.
    MultiInstruction,
    /// Compound node wrapping an SCC of other DDG nodes.
    PiBlock,
    /// Special node with edges to every connected component of the graph.
    Root,
  };

  /// Deleted default constructor; every DDG node has an explicit kind.
  DDGNode() = delete;
  /// Construct a DDG node of kind \p K.
  /// @param K Kind of node to create.
  DDGNode(const NodeKind K) : Kind(K) {}
  /// Copy-construct a DDG node from \p N.
  /// @param N Node to copy.
  DDGNode(const DDGNode &N) = default;
  /// Move-construct a DDG node from \p N.
  /// @param N Node to move from.
  DDGNode(DDGNode &&N) : DDGNodeBase(std::move(N)), Kind(N.Kind) {}
  /// Destroy this DDG node.
  virtual ~DDGNode() = 0;

  /// Copy-assign this DDG node from \p N.
  /// @param N Node to copy from.
  /// @return Reference to this node.
  DDGNode &operator=(const DDGNode &N) = default;

  /// Move-assign this DDG node from \p N.
  /// @param N Node to move from.
  /// @return Reference to this node.
  DDGNode &operator=(DDGNode &&N) {
    DGNode::operator=(std::move(N));
    Kind = N.Kind;
    return *this;
  }

  /// Getter for the kind of this node.
  /// @return Kind of this node.
  NodeKind getKind() const { return Kind; }

  /// Collect this node's instructions that satisfy \p Pred into \p IList.
  ///
  /// Iterates over instructions of this node and appends those for which
  /// \p Pred evaluates to true. Returns true if at least one instruction was
  /// collected, and false otherwise.
  /// @param Pred Predicate that selects which instructions to collect.
  /// @param IList Output list that receives matching instructions.
  /// @return True if at least one instruction was collected.
  bool collectInstructions(llvm::function_ref<bool(Instruction *)> const &Pred,
                           InstructionListType &IList) const;

protected:
  /// Setter for the kind of this node.
  /// @param K New kind for this node.
  void setKind(NodeKind K) { Kind = K; }

private:
  NodeKind Kind;
};

/// Subclass of DDGNode representing the root node of the graph.
/// There should only be one such node in a given graph.
class RootDDGNode : public DDGNode {
public:
  /// Construct a root DDG node.
  RootDDGNode() : DDGNode(NodeKind::Root) {}
  /// Deleted copy constructor; root nodes are not copyable.
  /// @param N Unused copy source (deleted).
  RootDDGNode(const RootDDGNode &N) = delete;
  /// Move-construct a root DDG node from \p N.
  /// @param N Root node to move from.
  RootDDGNode(RootDDGNode &&N) : DDGNode(std::move(N)) {}
  /// Destroy this root DDG node.
  ~RootDDGNode() override = default;

  /// Define classof to be able to use isa<>, cast<>, dyn_cast<>, etc.
  /// @param N Node to test for the root kind.
  /// @return True if \p N is a root node.
  static bool classof(const DDGNode *N) {
    return N->getKind() == NodeKind::Root;
  }
  /// Return true; \p N is already a RootDDGNode.
  /// @param N Root node to test.
  /// @return True; \p N is already a RootDDGNode.
  static bool classof(const RootDDGNode *N) { return true; }
};

/// Subclass of DDGNode representing single or multi-instruction nodes.
class LLVM_ABI SimpleDDGNode : public DDGNode {
  friend class DDGBuilder;

public:
  /// Deleted default constructor; a simple node must contain an instruction.
  SimpleDDGNode() = delete;
  /// Construct a simple node containing instruction \p I.
  /// @param I Instruction stored in this node.
  SimpleDDGNode(Instruction &I);
  /// Copy-construct a simple DDG node from \p N.
  /// @param N Simple node to copy.
  SimpleDDGNode(const SimpleDDGNode &N);
  /// Move-construct a simple DDG node from \p N.
  /// @param N Simple node to move from.
  SimpleDDGNode(SimpleDDGNode &&N);
  /// Destroy this simple DDG node.
  ~SimpleDDGNode() override;

  /// Copy-assign this simple DDG node from \p N.
  /// @param N Simple node to copy from.
  /// @return Reference to this node.
  SimpleDDGNode &operator=(const SimpleDDGNode &N) = default;

  /// Move-assign this simple DDG node from \p N.
  /// @param N Simple node to move from.
  /// @return Reference to this node.
  SimpleDDGNode &operator=(SimpleDDGNode &&N) {
    DDGNode::operator=(std::move(N));
    InstList = std::move(N.InstList);
    return *this;
  }

  /// Get the list of instructions in this node.
  /// @return Const list of instructions in this node.
  const InstructionListType &getInstructions() const {
    assert(!InstList.empty() && "Instruction List is empty.");
    return InstList;
  }
  /// Get a mutable list of instructions in this node.
  /// @return Mutable list of instructions in this node.
  InstructionListType &getInstructions() {
    return const_cast<InstructionListType &>(
        static_cast<const SimpleDDGNode *>(this)->getInstructions());
  }

  /// Get the first instruction in the node.
  /// @return First instruction in this node.
  Instruction *getFirstInstruction() const { return getInstructions().front(); }
  /// Get the last instruction in the node.
  /// @return Last instruction in this node.
  Instruction *getLastInstruction() const { return getInstructions().back(); }

  /// Define classof to be able to use isa<>, cast<>, dyn_cast<>, etc.
  /// @param N Node to test for a simple (single- or multi-instruction) kind.
  /// @return True if \p N is a single- or multi-instruction node.
  static bool classof(const DDGNode *N) {
    return N->getKind() == NodeKind::SingleInstruction ||
           N->getKind() == NodeKind::MultiInstruction;
  }
  /// Return true; \p N is already a SimpleDDGNode.
  /// @param N Simple node to test.
  /// @return True; \p N is already a SimpleDDGNode.
  static bool classof(const SimpleDDGNode *N) { return true; }

private:
  /// Append the list of instructions in \p Input to this node.
  void appendInstructions(const InstructionListType &Input) {
    setKind((InstList.size() == 0 && Input.size() == 1)
                ? NodeKind::SingleInstruction
                : NodeKind::MultiInstruction);
    llvm::append_range(InstList, Input);
  }
  void appendInstructions(const SimpleDDGNode &Input) {
    appendInstructions(Input.getInstructions());
  }

  /// List of instructions associated with a single or multi-instruction node.
  SmallVector<Instruction *, 2> InstList;
};

/// DDG node that abstracts a strongly-connected component as a pi-block.
///
/// A pi-block represents a group of DDG nodes that are part of a
/// strongly-connected component of the graph. Replacing all the SCCs with
/// pi-blocks results in an acyclic representation of the DDG. For example if
/// we have:
/// {a -> b}, {b -> c, d}, {c -> a}
/// the cycle a -> b -> c -> a is abstracted into a pi-block "p" as follows:
/// {p -> d} with "p" containing: {a -> b}, {b -> c}, {c -> a}
class LLVM_ABI PiBlockDDGNode : public DDGNode {
public:
  /// List of DDG node pointers contained in a pi-block.
  using PiNodeList = SmallVector<DDGNode *, 4>;

  /// Deleted default constructor; a pi-block must contain member nodes.
  PiBlockDDGNode() = delete;
  /// Construct a pi-block containing the nodes in \p List.
  /// @param List Member nodes of this pi-block.
  PiBlockDDGNode(const PiNodeList &List);
  /// Copy-construct a pi-block DDG node from \p N.
  /// @param N Pi-block node to copy.
  PiBlockDDGNode(const PiBlockDDGNode &N);
  /// Move-construct a pi-block DDG node from \p N.
  /// @param N Pi-block node to move from.
  PiBlockDDGNode(PiBlockDDGNode &&N);
  /// Destroy this pi-block DDG node.
  ~PiBlockDDGNode() override;

  /// Copy-assign this pi-block DDG node from \p N.
  /// @param N Pi-block node to copy from.
  /// @return Reference to this node.
  PiBlockDDGNode &operator=(const PiBlockDDGNode &N) = default;

  /// Move-assign this pi-block DDG node from \p N.
  /// @param N Pi-block node to move from.
  /// @return Reference to this node.
  PiBlockDDGNode &operator=(PiBlockDDGNode &&N) {
    DDGNode::operator=(std::move(N));
    NodeList = std::move(N.NodeList);
    return *this;
  }

  /// Get the list of nodes in this pi-block.
  /// @return Const list of nodes in this pi-block.
  const PiNodeList &getNodes() const {
    assert(!NodeList.empty() && "Node list is empty.");
    return NodeList;
  }
  /// Get a mutable list of nodes in this pi-block.
  /// @return Mutable list of nodes in this pi-block.
  PiNodeList &getNodes() {
    return const_cast<PiNodeList &>(
        static_cast<const PiBlockDDGNode *>(this)->getNodes());
  }

  /// Define classof to be able to use isa<>, cast<>, dyn_cast<>, etc.
  /// @param N Node to test for the pi-block kind.
  /// @return True if \p N is a pi-block node.
  static bool classof(const DDGNode *N) {
    return N->getKind() == NodeKind::PiBlock;
  }

private:
  /// List of nodes in this pi-block.
  PiNodeList NodeList;
};

/// Edge in a data dependence graph.
///
/// An edge in the DDG can represent a def-use relationship or a memory
/// dependence based on the result of DependenceAnalysis. A rooted edge
/// connects the root node to one of the components of the graph.
class DDGEdge : public DDGEdgeBase {
public:
  /// The kind of edge in the DDG
  enum class EdgeKind {
    /// Uninitialized or invalid edge kind.
    Unknown,
    /// Def-use dependence through a register.
    RegisterDefUse,
    /// Memory dependence identified by DependenceAnalysis.
    MemoryDependence,
    /// Edge from the root node to a connected component.
    Rooted,
    /// Sentinel equal to the largest enumerator's value.
    Last = Rooted // Must be equal to the largest enum value.
  };

  /// Deleted constructor; every edge must specify a kind.
  /// @param N Unused target node (deleted).
  explicit DDGEdge(DDGNode &N) = delete;
  /// Construct an edge to \p N of kind \p K.
  /// @param N Target node of this edge.
  /// @param K Kind of dependence this edge represents.
  DDGEdge(DDGNode &N, EdgeKind K) : DDGEdgeBase(N), Kind(K) {}
  /// Copy-construct a DDG edge from \p E.
  /// @param E Edge to copy.
  DDGEdge(const DDGEdge &E) : DDGEdgeBase(E), Kind(E.getKind()) {}
  /// Move-construct a DDG edge from \p E.
  /// @param E Edge to move from.
  DDGEdge(DDGEdge &&E) : DDGEdgeBase(std::move(E)), Kind(E.Kind) {}
  /// Copy-assign this DDG edge from \p E.
  /// @param E Edge to copy from.
  /// @return Reference to this edge.
  DDGEdge &operator=(const DDGEdge &E) = default;

  /// Move-assign this DDG edge from \p E.
  /// @param E Edge to move from.
  /// @return Reference to this edge.
  DDGEdge &operator=(DDGEdge &&E) {
    DDGEdgeBase::operator=(std::move(E));
    Kind = E.Kind;
    return *this;
  }

  /// Get the edge kind.
  /// @return Kind of this edge.
  EdgeKind getKind() const { return Kind; };

  /// Return true if this is a def-use edge, and false otherwise.
  /// @return True if this is a def-use edge.
  bool isDefUse() const { return Kind == EdgeKind::RegisterDefUse; }

  /// Return true if this is a memory dependence edge, and false otherwise.
  /// @return True if this is a memory dependence edge.
  bool isMemoryDependence() const { return Kind == EdgeKind::MemoryDependence; }

  /// Return true if this is an edge stemming from the root node, and false
  /// otherwise.
  /// @return True if this edge stems from the root node.
  bool isRooted() const { return Kind == EdgeKind::Rooted; }

private:
  EdgeKind Kind;
};

/// Encapsulate some common data and functionality needed for different
/// variations of data dependence graphs.
template <typename NodeType> class DependenceGraphInfo {
public:
  /// Vector of unique pointers to Dependence objects.
  using DependenceList = SmallVector<std::unique_ptr<Dependence>, 1>;

  /// Deleted default constructor; a graph must have a name and dependence info.
  DependenceGraphInfo() = delete;
  /// Deleted copy constructor; dependence graphs are not copyable.
  /// @param G Unused copy source (deleted).
  DependenceGraphInfo(const DependenceGraphInfo &G) = delete;
  /// Construct a graph named \p N that uses dependence info \p DepInfo.
  /// @param N Name used to label this graph.
  /// @param DepInfo Dependence analysis used to query memory dependences.
  DependenceGraphInfo(const std::string &N, const DependenceInfo &DepInfo)
      : Name(N), DI(DepInfo), Root(nullptr) {}
  /// Move-construct a dependence graph from \p G.
  /// @param G Graph to move from.
  DependenceGraphInfo(DependenceGraphInfo &&G)
      : Name(std::move(G.Name)), DI(std::move(G.DI)), Root(G.Root) {}
  /// Destroy this dependence graph.
  virtual ~DependenceGraphInfo() = default;

  /// Return the label that is used to name this graph.
  /// @return Name of this graph.
  StringRef getName() const { return Name; }

  /// Return the root node of the graph.
  /// @return Root node of this graph.
  NodeType &getRoot() const {
    assert(Root && "Root node is not available yet. Graph construction may "
                   "still be in progress\n");
    return *Root;
  }

  /// Collect memory dependences from \p Src to \p Dst into \p Deps.
  ///
  /// Collect all the data dependency infos coming from any pair of memory
  /// accesses from \p Src to \p Dst, and store them into \p Deps. Return true
  /// if a dependence exists, and false otherwise.
  /// @param Src Source node of the queried dependences.
  /// @param Dst Destination node of the queried dependences.
  /// @param Deps Output list that receives identified dependences.
  /// @return True if a dependence exists between \p Src and \p Dst.
  bool getDependencies(const NodeType &Src, const NodeType &Dst,
                       DependenceList &Deps) const;

  /// Return a string describing the dependence between \p Src and \p Dst.
  ///
  /// Return a string representing the type of dependence that the dependence
  /// analysis identified between the two given nodes. This function assumes
  /// that there is a memory dependence between the given two nodes.
  /// @param Src Source node of the dependence.
  /// @param Dst Destination node of the dependence.
  /// @return String describing the dependence between \p Src and \p Dst.
  std::string getDependenceString(const NodeType &Src,
                                  const NodeType &Dst) const;

protected:
  /// Name of the graph.
  std::string Name;

  /// Dependence information used to recompute memory dependencies on demand.
  ///
  /// Store a copy of DependenceInfo in the graph, so that individual memory
  /// dependencies don't need to be stored. Instead when the dependence is
  /// queried it is recomputed using DI.
  const DependenceInfo DI;

  /// A special node in the graph that has an edge to every connected component
  /// of the graph, to ensure all nodes are reachable in a graph walk.
  NodeType *Root = nullptr;
};

/// Dependence-graph info specialized for DDG nodes.
using DDGInfo = DependenceGraphInfo<DDGNode>;

/// Data Dependency Graph
class LLVM_ABI DataDependenceGraph : public DDGBase, public DDGInfo {
  friend AbstractDependenceGraphBuilder<DataDependenceGraph>;
  friend class DDGBuilder;

public:
  /// Node type stored in this graph.
  using NodeType = DDGNode;
  /// Edge type stored in this graph.
  using EdgeType = DDGEdge;

  /// Deleted default constructor; a DDG is built from a function or a loop.
  DataDependenceGraph() = delete;
  /// Deleted copy constructor; data dependence graphs are not copyable.
  /// @param G Unused copy source (deleted).
  DataDependenceGraph(const DataDependenceGraph &G) = delete;
  /// Move-construct a data dependence graph from \p G.
  /// @param G Graph to move from.
  DataDependenceGraph(DataDependenceGraph &&G)
      : DDGBase(std::move(G)), DDGInfo(std::move(G)) {}
  /// Build a DDG for function \p F using dependence info \p DI.
  /// @param F Function whose instructions are represented in the graph.
  /// @param DI Dependence analysis used to create memory edges.
  DataDependenceGraph(Function &F, DependenceInfo &DI);
  /// Build a DDG for loop \p L using loop info \p LI and dependence info \p DI.
  /// @param L Loop whose instructions are represented in the graph.
  /// @param LI Loop information used while constructing the graph.
  /// @param DI Dependence analysis used to create memory edges.
  DataDependenceGraph(Loop &L, LoopInfo &LI, DependenceInfo &DI);
  /// Destroy this data dependence graph.
  ~DataDependenceGraph() override;

  /// If node \p N belongs to a pi-block return a pointer to the pi-block,
  /// otherwise return null.
  /// @param N Node whose containing pi-block is requested.
  /// @return Containing pi-block for \p N, or null if none.
  const PiBlockDDGNode *getPiBlock(const NodeType &N) const;

protected:
  /// Add node \p N to the graph if it is not already present.
  ///
  /// Keep track of the root node as well as pi-blocks and their members.
  /// Return true if node is successfully added.
  /// @param N Node to add to the graph.
  /// @return True if \p N was successfully added.
  bool addNode(NodeType &N);

private:
  using PiBlockMapType = DenseMap<const NodeType *, const PiBlockDDGNode *>;

  /// Mapping from graph nodes to their containing pi-blocks. If a node is not
  /// part of a pi-block, it will not appear in this map.
  PiBlockMapType PiBlockMap;
};

/// Builder that constructs a pure data dependence graph.
///
/// Concrete implementation of a pure data dependence graph builder. This class
/// provides custom implementation for the pure-virtual functions used in the
/// generic dependence graph build algorithm.
///
/// For information about time complexity of the build algorithm see the
/// comments near the declaration of AbstractDependenceGraphBuilder.
class LLVM_ABI DDGBuilder
    : public AbstractDependenceGraphBuilder<DataDependenceGraph> {
public:
  /// Construct a DDG builder for graph \p G using \p D and blocks \p BBs.
  /// @param G Graph being populated by this builder.
  /// @param D Dependence information used when creating memory edges.
  /// @param BBs Basic blocks considered when building the graph.
  DDGBuilder(DataDependenceGraph &G, DependenceInfo &D,
             const BasicBlockListType &BBs)
      : AbstractDependenceGraphBuilder(G, D, BBs) {}
  /// Create the root node of the graph.
  /// @return Newly created root node of the graph.
  DDGNode &createRootNode() final {
    auto *RN = new RootDDGNode();
    assert(RN && "Failed to allocate memory for DDG root node.");
    Graph.addNode(*RN);
    return *RN;
  }
  /// Create an atomic node in the graph given a single instruction.
  /// @param I Instruction represented by the new fine-grained node.
  /// @return Newly created fine-grained node for \p I.
  DDGNode &createFineGrainedNode(Instruction &I) final {
    auto *SN = new SimpleDDGNode(I);
    assert(SN && "Failed to allocate memory for simple DDG node.");
    Graph.addNode(*SN);
    return *SN;
  }
  /// Create a pi-block node in the graph representing a group of nodes in an
  /// SCC of the graph.
  /// @param L Nodes that belong to the SCC and become members of the pi-block.
  /// @return Newly created pi-block node containing the nodes in \p L.
  DDGNode &createPiBlock(const NodeListType &L) final {
    auto *Pi = new PiBlockDDGNode(L);
    assert(Pi && "Failed to allocate memory for pi-block node.");
    Graph.addNode(*Pi);
    return *Pi;
  }
  /// Create a def-use edge going from \p Src to \p Tgt.
  /// @param Src Source node of the def-use edge.
  /// @param Tgt Target node of the def-use edge.
  /// @return Newly created def-use edge from \p Src to \p Tgt.
  DDGEdge &createDefUseEdge(DDGNode &Src, DDGNode &Tgt) final {
    auto *E = new DDGEdge(Tgt, DDGEdge::EdgeKind::RegisterDefUse);
    assert(E && "Failed to allocate memory for edge");
    Graph.connect(Src, Tgt, *E);
    return *E;
  }
  /// Create a memory dependence edge going from \p Src to \p Tgt.
  /// @param Src Source node of the memory dependence edge.
  /// @param Tgt Target node of the memory dependence edge.
  /// @return Newly created memory dependence edge from \p Src to \p Tgt.
  DDGEdge &createMemoryEdge(DDGNode &Src, DDGNode &Tgt) final {
    auto *E = new DDGEdge(Tgt, DDGEdge::EdgeKind::MemoryDependence);
    assert(E && "Failed to allocate memory for edge");
    Graph.connect(Src, Tgt, *E);
    return *E;
  }
  /// Create a rooted edge going from \p Src to \p Tgt.
  /// @param Src Source node of the rooted edge.
  /// @param Tgt Target node of the rooted edge.
  /// @return Newly created rooted edge from \p Src to \p Tgt.
  DDGEdge &createRootedEdge(DDGNode &Src, DDGNode &Tgt) final {
    auto *E = new DDGEdge(Tgt, DDGEdge::EdgeKind::Rooted);
    assert(E && "Failed to allocate memory for edge");
    assert(isa<RootDDGNode>(Src) && "Expected root node");
    Graph.connect(Src, Tgt, *E);
    return *E;
  }

  /// Given a pi-block node, return a vector of all the nodes contained within
  /// it.
  /// @param N Pi-block node whose member nodes are requested.
  /// @return Nodes contained within pi-block \p N.
  const NodeListType &getNodesInPiBlock(const DDGNode &N) final {
    auto *PiNode = dyn_cast<const PiBlockDDGNode>(&N);
    assert(PiNode && "Expected a pi-block node.");
    return PiNode->getNodes();
  }

  /// Return true if the two nodes \p Src and \p Tgt are both simple nodes and
  /// the consecutive instructions after merging belong to the same basic block.
  /// @param Src First node to consider for merging.
  /// @param Tgt Second node to consider for merging.
  /// @return True if \p Src and \p Tgt can be merged.
  bool areNodesMergeable(const DDGNode &Src, const DDGNode &Tgt) const final;
  /// Append the contents of \p Tgt into \p Src and remove \p Tgt from the
  /// graph.
  /// @param Src Destination node that absorbs the contents of \p Tgt.
  /// @param Tgt Node whose contents are merged into \p Src.
  void mergeNodes(DDGNode &Src, DDGNode &Tgt) final;
  /// Return true if graph simplification step is requested, and false
  /// otherwise.
  /// @return True if graph simplification is requested.
  bool shouldSimplify() const final;
  /// Return true if creation of pi-blocks are supported and desired,
  /// and false otherwise.
  /// @return True if pi-block creation is supported and desired.
  bool shouldCreatePiBlocks() const final;
};

/// Write DDG node \p N to stream \p OS.
/// @param OS Output stream.
/// @param N DDG node to print.
/// @return Reference to the output stream.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const DDGNode &N);
/// Write DDG node kind \p K to stream \p OS.
/// @param OS Output stream.
/// @param K Node kind to print.
/// @return Reference to the output stream.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const DDGNode::NodeKind K);
/// Write DDG edge \p E to stream \p OS.
/// @param OS Output stream.
/// @param E DDG edge to print.
/// @return Reference to the output stream.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const DDGEdge &E);
/// Write DDG edge kind \p K to stream \p OS.
/// @param OS Output stream.
/// @param K Edge kind to print.
/// @return Reference to the output stream.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const DDGEdge::EdgeKind K);
/// Write data dependence graph \p G to stream \p OS.
/// @param OS Output stream.
/// @param G Data dependence graph to print.
/// @return Reference to the output stream.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const DataDependenceGraph &G);

//===--------------------------------------------------------------------===//
// DDG Analysis Passes
//===--------------------------------------------------------------------===//

/// Analysis pass that builds the DDG for a loop.
class DDGAnalysis : public AnalysisInfoMixin<DDGAnalysis> {
public:
  /// Result of this analysis: an owning pointer to a DDG.
  using Result = std::unique_ptr<DataDependenceGraph>;
  /// Run the analysis pass over loop \p L and produce a DDG.
  /// @param L Loop whose data dependence graph is built.
  /// @param AM Loop analysis manager providing analyses.
  /// @param AR Standard loop analysis results used to compute dependences.
  /// @return Owning pointer to the DDG for \p L.
  LLVM_ABI Result run(Loop &L, LoopAnalysisManager &AM,
                      LoopStandardAnalysisResults &AR);

private:
  friend AnalysisInfoMixin<DDGAnalysis>;
  LLVM_ABI static AnalysisKey Key;
};

/// Textual printer pass for the DDG of a loop.
class DDGAnalysisPrinterPass
    : public RequiredPassInfoMixin<DDGAnalysisPrinterPass> {
public:
  /// Construct a printer that writes DDG results to \p OS.
  /// @param OS Output stream for the printed analysis.
  explicit DDGAnalysisPrinterPass(raw_ostream &OS) : OS(OS) {}
  /// Print the DDG for loop \p L and return all analyses preserved.
  /// @param L Loop whose data dependence graph is printed.
  /// @param AM Loop analysis manager providing DDGAnalysis.
  /// @param AR Standard loop analysis results.
  /// @param U Loop pass manager updater (unused by the printer).
  /// @return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                                 LoopStandardAnalysisResults &AR,
                                 LPMUpdater &U);

private:
  raw_ostream &OS;
};

//===--------------------------------------------------------------------===//
// DependenceGraphInfo Implementation
//===--------------------------------------------------------------------===//

template <typename NodeType>
bool DependenceGraphInfo<NodeType>::getDependencies(
    const NodeType &Src, const NodeType &Dst, DependenceList &Deps) const {
  assert(Deps.empty() && "Expected empty output list at the start.");

  // List of memory access instructions from src and dst nodes.
  SmallVector<Instruction *, 8> SrcIList, DstIList;
  auto isMemoryAccess = [](const Instruction *I) {
    return I->mayReadOrWriteMemory();
  };
  Src.collectInstructions(isMemoryAccess, SrcIList);
  Dst.collectInstructions(isMemoryAccess, DstIList);

  for (auto *SrcI : SrcIList)
    for (auto *DstI : DstIList)
      if (auto Dep =
              const_cast<DependenceInfo *>(&DI)->depends(SrcI, DstI))
        Deps.push_back(std::move(Dep));

  return !Deps.empty();
}

template <typename NodeType>
std::string
DependenceGraphInfo<NodeType>::getDependenceString(const NodeType &Src,
                                                   const NodeType &Dst) const {
  std::string Str;
  raw_string_ostream OS(Str);
  DependenceList Deps;
  if (!getDependencies(Src, Dst, Deps))
    return Str;
  interleaveComma(Deps, OS, [&](const std::unique_ptr<Dependence> &D) {
    D->dump(OS);
    // Remove the extra new-line character printed by the dump
    // method
    if (Str.back() == '\n')
      Str.pop_back();
  });

  return Str;
}

//===--------------------------------------------------------------------===//
// GraphTraits specializations for the DDG
//===--------------------------------------------------------------------===//

/// GraphTraits specialization for mutable DDGNode pointers.
template <> struct GraphTraits<DDGNode *> {
  /// Graph node type for a DDGNode.
  using NodeRef = DDGNode *;

  /// Return the target node of edge \p P.
  /// @param P Edge whose target node is extracted.
  /// @return Target node of \p P.
  static DDGNode *DDGGetTargetNode(DGEdge<DDGNode, DDGEdge> *P) {
    return &P->getTargetNode();
  }

  /// Iterator over child DDGNode pointers.
  ///
  /// Provide a mapped iterator so that the GraphTrait-based implementations can
  /// find the target nodes without having to explicitly go through the edges.
  using ChildIteratorType =
      mapped_iterator<DDGNode::iterator, decltype(&DDGGetTargetNode)>;
  /// Iterator over outgoing DDG edges.
  using ChildEdgeIteratorType = DDGNode::iterator;

  /// Return \p N as the graph entry node.
  /// @param N DDG node used as the entry.
  /// @return Entry node \p N.
  static NodeRef getEntryNode(NodeRef N) { return N; }
  /// Return the begin iterator over children of \p N.
  /// @param N Parent DDG node.
  /// @return Begin iterator over child nodes of \p N.
  static ChildIteratorType child_begin(NodeRef N) {
    return ChildIteratorType(N->begin(), &DDGGetTargetNode);
  }
  /// Return the end iterator over children of \p N.
  /// @param N Parent DDG node.
  /// @return End iterator over child nodes of \p N.
  static ChildIteratorType child_end(NodeRef N) {
    return ChildIteratorType(N->end(), &DDGGetTargetNode);
  }

  /// Return the begin iterator over outgoing edges of \p N.
  /// @param N Parent DDG node.
  /// @return Begin iterator over outgoing edges of \p N.
  static ChildEdgeIteratorType child_edge_begin(NodeRef N) {
    return N->begin();
  }
  /// Return the end iterator over outgoing edges of \p N.
  /// @param N Parent DDG node.
  /// @return End iterator over outgoing edges of \p N.
  static ChildEdgeIteratorType child_edge_end(NodeRef N) { return N->end(); }
};

/// GraphTraits specialization treating a DataDependenceGraph as a graph of
/// nodes.
template <>
struct GraphTraits<DataDependenceGraph *> : public GraphTraits<DDGNode *> {
  /// Iterator over all DDGNode pointers in the graph.
  using nodes_iterator = DataDependenceGraph::iterator;
  /// Return the root node as the graph entry.
  /// @param DG Data dependence graph whose root is the entry.
  /// @return Root node of \p DG.
  static NodeRef getEntryNode(DataDependenceGraph *DG) {
    return &DG->getRoot();
  }
  /// Return the begin iterator over all nodes in \p DG.
  /// @param DG Data dependence graph whose nodes are iterated.
  /// @return Begin iterator over nodes in \p DG.
  static nodes_iterator nodes_begin(DataDependenceGraph *DG) {
    return DG->begin();
  }
  /// Return the end iterator over all nodes in \p DG.
  /// @param DG Data dependence graph whose nodes are iterated.
  /// @return End iterator over nodes in \p DG.
  static nodes_iterator nodes_end(DataDependenceGraph *DG) { return DG->end(); }
};

/// GraphTraits specialization for const DDGNode pointers.
template <> struct GraphTraits<const DDGNode *> {
  /// Graph node type for a const DDGNode.
  using NodeRef = const DDGNode *;

  /// Return the target node of edge \p P.
  /// @param P Edge whose target node is extracted.
  /// @return Target node of \p P.
  static const DDGNode *DDGGetTargetNode(const DGEdge<DDGNode, DDGEdge> *P) {
    return &P->getTargetNode();
  }

  /// Iterator over child const DDGNode pointers.
  ///
  /// Provide a mapped iterator so that the GraphTrait-based implementations can
  /// find the target nodes without having to explicitly go through the edges.
  using ChildIteratorType =
      mapped_iterator<DDGNode::const_iterator, decltype(&DDGGetTargetNode)>;
  /// Iterator over outgoing const DDG edges.
  using ChildEdgeIteratorType = DDGNode::const_iterator;

  /// Return \p N as the graph entry node.
  /// @param N DDG node used as the entry.
  /// @return Entry node \p N.
  static NodeRef getEntryNode(NodeRef N) { return N; }
  /// Return the begin iterator over children of \p N.
  /// @param N Parent DDG node.
  /// @return Begin iterator over child nodes of \p N.
  static ChildIteratorType child_begin(NodeRef N) {
    return ChildIteratorType(N->begin(), &DDGGetTargetNode);
  }
  /// Return the end iterator over children of \p N.
  /// @param N Parent DDG node.
  /// @return End iterator over child nodes of \p N.
  static ChildIteratorType child_end(NodeRef N) {
    return ChildIteratorType(N->end(), &DDGGetTargetNode);
  }

  /// Return the begin iterator over outgoing edges of \p N.
  /// @param N Parent DDG node.
  /// @return Begin iterator over outgoing edges of \p N.
  static ChildEdgeIteratorType child_edge_begin(NodeRef N) {
    return N->begin();
  }
  /// Return the end iterator over outgoing edges of \p N.
  /// @param N Parent DDG node.
  /// @return End iterator over outgoing edges of \p N.
  static ChildEdgeIteratorType child_edge_end(NodeRef N) { return N->end(); }
};

/// GraphTraits specialization treating a const DataDependenceGraph as a graph
/// of nodes.
template <>
struct GraphTraits<const DataDependenceGraph *>
    : public GraphTraits<const DDGNode *> {
  /// Iterator over all const DDGNode pointers in the graph.
  using nodes_iterator = DataDependenceGraph::const_iterator;
  /// Return the root node as the graph entry.
  /// @param DG Data dependence graph whose root is the entry.
  /// @return Root node of \p DG.
  static NodeRef getEntryNode(const DataDependenceGraph *DG) {
    return &DG->getRoot();
  }
  /// Return the begin iterator over all nodes in \p DG.
  /// @param DG Data dependence graph whose nodes are iterated.
  /// @return Begin iterator over nodes in \p DG.
  static nodes_iterator nodes_begin(const DataDependenceGraph *DG) {
    return DG->begin();
  }
  /// Return the end iterator over all nodes in \p DG.
  /// @param DG Data dependence graph whose nodes are iterated.
  /// @return End iterator over nodes in \p DG.
  static nodes_iterator nodes_end(const DataDependenceGraph *DG) {
    return DG->end();
  }
};

} // namespace llvm

#endif // LLVM_ANALYSIS_DDG_H
