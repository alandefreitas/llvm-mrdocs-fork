//===- CallGraph.h - Build a Module's call graph ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file provides interfaces used to build and manipulate a call graph,
/// which is a very useful tool for interprocedural optimization.
///
/// Every function in a module is represented as a node in the call graph.  The
/// callgraph node keeps track of which functions are called by the function
/// corresponding to the node.
///
/// A call graph may contain nodes where the function that they correspond to
/// is null.  These 'external' nodes are used to represent control flow that is
/// not represented (or analyzable) in the module.  In particular, this
/// analysis builds one external node such that:
///   1. All functions in the module without internal linkage will have edges
///      from this external node, indicating that they could be called by
///      functions outside of the module.
///   2. All functions whose address is used for something more than a direct
///      call, for example being stored into a memory location will also have
///      an edge from this external node.  Since they may be called by an
///      unknown caller later, they must be tracked as such.
///
/// There is a second external node added for calls that leave this module.
/// Functions have a call edge to the external node iff:
///   1. The function is external, reflecting the fact that they could call
///      anything without internal linkage or that has its address taken.
///   2. The function contains an indirect function call.
///
/// As an extension in the future, there may be multiple nodes with a null
/// function.  These will be used when we can prove (through pointer analysis)
/// that an indirect call site can call only a specific set of functions.
///
/// Because of these properties, the CallGraph captures a conservative superset
/// of all of the caller-callee relationships, which is useful for
/// transformations.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_CALLGRAPH_H
#define LLVM_ANALYSIS_CALLGRAPH_H

#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Pass.h"
#include "llvm/Support/Compiler.h"
#include <cassert>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace llvm {

template <class GraphType> struct GraphTraits;
class CallGraphNode;
class Function;
class Module;
class raw_ostream;

/// The basic data container for the call graph of a \c Module of IR.
///
/// This class exposes both the interface to the call graph for a module of IR.
///
/// The core call graph itself can also be updated to reflect changes to the IR.
class CallGraph {
  Module &M;

  using FunctionMapTy =
      std::map<const Function *, std::unique_ptr<CallGraphNode>>;

  /// A map from \c Function* to \c CallGraphNode*.
  FunctionMapTy FunctionMap;

  /// This node has edges to all external functions and those internal
  /// functions that have their address taken.
  CallGraphNode *ExternalCallingNode;

  /// This node has edges to it from all functions making indirect calls
  /// or calling an external function.
  std::unique_ptr<CallGraphNode> CallsExternalNode;

public:
  /// Construct a call graph for module \p M.
  /// @param M Module whose call graph is built.
  LLVM_ABI explicit CallGraph(Module &M);
  /// Move-construct a call graph from \p Arg.
  /// @param Arg Call graph to move from.
  LLVM_ABI CallGraph(CallGraph &&Arg);
  /// Destroy the call graph and its nodes.
  LLVM_ABI ~CallGraph();

  /// Print the call graph to \p OS.
  /// @param OS Output stream.
  LLVM_ABI void print(raw_ostream &OS) const;
  /// Dump the call graph to stderr for debugging.
  LLVM_ABI void dump() const;

  /// Mutable iterator over function-to-node map entries.
  using iterator = FunctionMapTy::iterator;
  /// Const iterator over function-to-node map entries.
  using const_iterator = FunctionMapTy::const_iterator;

  /// Returns the module the call graph corresponds to.
  /// @return The module associated with this call graph.
  Module &getModule() const { return M; }

  /// Invalidate cached analyses when the module changes.
  /// @param M Module being invalidated.
  /// @param PA Set of analyses preserved by the transform.
  /// @param Inv Invalidator for resolving analysis dependencies.
  /// @return True if this analysis result should be invalidated.
  LLVM_ABI bool invalidate(Module &M, const PreservedAnalyses &PA,
                           ModuleAnalysisManager::Invalidator &Inv);

  /// Return an iterator to the first function map entry.
  /// @return Iterator to the first function-to-node map entry.
  inline iterator begin() { return FunctionMap.begin(); }
  /// Return an iterator past the last function map entry.
  /// @return Iterator past the last function-to-node map entry.
  inline iterator end() { return FunctionMap.end(); }
  /// Return a const iterator to the first function map entry.
  /// @return Const iterator to the first function-to-node map entry.
  inline const_iterator begin() const { return FunctionMap.begin(); }
  /// Return a const iterator past the last function map entry.
  /// @return Const iterator past the last function-to-node map entry.
  inline const_iterator end() const { return FunctionMap.end(); }

  /// Returns the call graph node for the provided function.
  /// @param F Function whose call graph node is requested.
  /// @return Const pointer to the call graph node for \p F.
  inline const CallGraphNode *operator[](const Function *F) const {
    const_iterator I = FunctionMap.find(F);
    assert(I != FunctionMap.end() && "Function not in callgraph!");
    return I->second.get();
  }

  /// Returns the call graph node for the provided function.
  /// @param F Function whose call graph node is requested.
  /// @return Pointer to the call graph node for \p F.
  inline CallGraphNode *operator[](const Function *F) {
    const_iterator I = FunctionMap.find(F);
    assert(I != FunctionMap.end() && "Function not in callgraph!");
    return I->second.get();
  }

  /// Returns the \c CallGraphNode which is used to represent
  /// undetermined calls into the callgraph.
  /// @return The external-calling node for undetermined callers.
  CallGraphNode *getExternalCallingNode() const { return ExternalCallingNode; }

  /// Return the node representing calls that leave this module.
  /// @return The node that represents calls leaving this module.
  CallGraphNode *getCallsExternalNode() const {
    return CallsExternalNode.get();
  }

  //===---------------------------------------------------------------------
  // Functions to keep a call graph up to date with a function that has been
  // modified.
  //

  /// Unlink the function from this module, returning it.
  ///
  /// Because this removes the function from the module, the call graph node is
  /// destroyed.  This is only valid if the function does not call any other
  /// functions (ie, there are no edges in it's CGN).  The easiest way to do
  /// this is to dropAllReferences before calling this.
  /// @param CGN Call graph node whose function is unlinked and returned.
  /// @return The function that was unlinked from the module.
  LLVM_ABI Function *removeFunctionFromModule(CallGraphNode *CGN);

  /// Similar to operator[], but this will insert a new CallGraphNode for
  /// \c F if one does not already exist.
  /// @param F Function to look up or insert into the call graph.
  /// @return The existing or newly inserted call graph node for \p F.
  LLVM_ABI CallGraphNode *getOrInsertFunction(const Function *F);

  /// Populate \p CGN based on the calls inside the associated function.
  /// @param CGN Call graph node to populate from its function body.
  LLVM_ABI void populateCallGraphNode(CallGraphNode *CGN);

  /// Add a function to the call graph, and link the node to all of the
  /// functions that it calls.
  /// @param F Function to add and connect in the call graph.
  LLVM_ABI void addToCallGraph(Function *F);
};

/// A node in the call graph for a module.
///
/// Typically represents a function in the call graph. There are also special
/// "null" nodes used to represent theoretical entries in the call graph.
class CallGraphNode {
public:
  /// A call-site edge from this node to a callee.
  ///
  /// A pair of the calling instruction (a call or invoke) and the call graph
  /// node being called. Call graph node may have two types of call records
  /// which represent an edge in the call graph - reference or a call edge.
  /// Reference edges are not associated with any call instruction and are
  /// created with the first field set to `None`, while real call edges have
  /// instruction address in this field. Therefore, all real call edges are
  /// expected to have a value in the first field and it is not supposed to be
  /// `nullptr`. Reference edges, for example, are used for connecting broker
  /// function caller to the callback function for callback call sites.
  using CallRecord = std::pair<std::optional<WeakTrackingVH>, CallGraphNode *>;

public:
  /// Vector of call records representing outgoing edges from this node.
  using CalledFunctionsVector = std::vector<CallRecord>;

  /// Creates a node for the specified function.
  /// @param CG Owning call graph.
  /// @param F Function represented by this node, or null for a special node.
  inline CallGraphNode(CallGraph *CG, Function *F) : CG(CG), F(F) {}

  /// Deleted copy constructor; nodes are owned by the call graph.
  /// @param Other Unused; copy construction is deleted.
  CallGraphNode(const CallGraphNode &Other) = delete;
  /// Deleted copy assignment; nodes are owned by the call graph.
  /// @param Other Unused; copy assignment is deleted.
  CallGraphNode &operator=(const CallGraphNode &Other) = delete;

  /// Destroy this node; asserts that no references remain.
  ~CallGraphNode() {
    assert(NumReferences == 0 && "Node deleted while references remain");
  }

  /// Mutable iterator over outgoing call records.
  using iterator = std::vector<CallRecord>::iterator;
  /// Const iterator over outgoing call records.
  using const_iterator = std::vector<CallRecord>::const_iterator;

  /// Returns the function that this call graph node represents.
  /// @return The function for this node, or null for a special node.
  Function *getFunction() const { return F; }

  /// Return an iterator to the first called-function record.
  /// @return Iterator to the first outgoing call record.
  inline iterator begin() { return CalledFunctions.begin(); }
  /// Return an iterator past the last called-function record.
  /// @return Iterator past the last outgoing call record.
  inline iterator end() { return CalledFunctions.end(); }
  /// Return a const iterator to the first called-function record.
  /// @return Const iterator to the first outgoing call record.
  inline const_iterator begin() const { return CalledFunctions.begin(); }
  /// Return a const iterator past the last called-function record.
  /// @return Const iterator past the last outgoing call record.
  inline const_iterator end() const { return CalledFunctions.end(); }
  /// Return true if this node has no outgoing call edges.
  /// @return True if this node has no outgoing call edges.
  inline bool empty() const { return CalledFunctions.empty(); }
  /// Return the number of outgoing call edges.
  /// @return The number of outgoing call edges from this node.
  inline unsigned size() const { return (unsigned)CalledFunctions.size(); }

  /// Returns the number of other CallGraphNodes in this CallGraph that
  /// reference this node in their callee list.
  /// @return The number of incoming references to this node.
  unsigned getNumReferences() const { return NumReferences; }

  /// Returns the i'th called function.
  /// @param i Zero-based index into the called-functions list.
  /// @return The callee call graph node at index \p i.
  CallGraphNode *operator[](unsigned i) const {
    assert(i < CalledFunctions.size() && "Invalid index");
    return CalledFunctions[i].second;
  }

  /// Print out this call graph node.
  LLVM_ABI void dump() const;
  /// Print this call graph node to \p OS.
  /// @param OS Output stream.
  LLVM_ABI void print(raw_ostream &OS) const;

  //===---------------------------------------------------------------------
  // Methods to keep a call graph up to date with a function that has been
  // modified
  //

  /// Removes all edges from this CallGraphNode to any functions it
  /// calls.
  void removeAllCalledFunctions() {
    while (!CalledFunctions.empty()) {
      CalledFunctions.back().second->DropRef();
      CalledFunctions.pop_back();
    }
  }

  /// Moves all the callee information from N to this node.
  /// @param N Node whose called-function list is stolen.
  void stealCalledFunctionsFrom(CallGraphNode *N) {
    assert(CalledFunctions.empty() &&
           "Cannot steal callsite information if I already have some");
    std::swap(CalledFunctions, N->CalledFunctions);
  }

  /// Adds a function to the list of functions called by this one.
  /// @param Call Call instruction for the edge, or null for a reference edge.
  /// @param M Callee call graph node.
  void addCalledFunction(CallBase *Call, CallGraphNode *M) {
    CalledFunctions.emplace_back(Call ? std::optional<WeakTrackingVH>(Call)
                                      : std::optional<WeakTrackingVH>(),
                                 M);
    M->AddRef();
  }

  /// Remove the call edge at iterator \p I.
  /// @param I Iterator identifying the call record to remove.
  void removeCallEdge(iterator I) {
    I->second->DropRef();
    *I = CalledFunctions.back();
    CalledFunctions.pop_back();
  }

  /// Removes one edge associated with a null callsite from this node to
  /// the specified callee function.
  /// @param Callee Callee node for the abstract edge to remove.
  LLVM_ABI void removeOneAbstractEdgeTo(CallGraphNode *Callee);

  /// Replaces the edge in the node for the specified call site with a
  /// new one.
  ///
  /// Note that this method takes linear time, so it should be used sparingly.
  /// @param Call Existing call site whose edge is replaced.
  /// @param NewCall Replacement call instruction for the edge.
  /// @param NewNode Replacement callee call graph node.
  LLVM_ABI void replaceCallEdge(CallBase &Call, CallBase &NewCall,
                                CallGraphNode *NewNode);

private:
  friend class CallGraph;

  CallGraph *CG;
  Function *F;

  std::vector<CallRecord> CalledFunctions;

  /// The number of times that this CallGraphNode occurs in the
  /// CalledFunctions array of this or other CallGraphNodes.
  unsigned NumReferences = 0;

  void DropRef() { --NumReferences; }
  void AddRef() { ++NumReferences; }

  /// A special function that should only be used by the CallGraph class.
  void allReferencesDropped() { NumReferences = 0; }
};

/// An analysis pass to compute the \c CallGraph for a \c Module.
///
/// This class implements the concept of an analysis pass used by the \c
/// ModuleAnalysisManager to run an analysis over a module and cache the
/// resulting data.
class CallGraphAnalysis : public AnalysisInfoMixin<CallGraphAnalysis> {
  friend AnalysisInfoMixin<CallGraphAnalysis>;

  LLVM_ABI static AnalysisKey Key;

public:
  /// A formulaic type to inform clients of the result type.
  using Result = CallGraph;

  /// Compute the \c CallGraph for the module \c M.
  ///
  /// The real work here is done in the \c CallGraph constructor.
  /// @param M Module whose call graph is computed.
  /// @param AM Module analysis manager (unused).
  /// @return The call graph computed for \p M.
  CallGraph run(Module &M, ModuleAnalysisManager &AM) { return CallGraph(M); }
};

/// Printer pass for the \c CallGraphAnalysis results.
class CallGraphPrinterPass
    : public RequiredPassInfoMixin<CallGraphPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes to \p OS.
  /// @param OS Output stream for the call graph.
  explicit CallGraphPrinterPass(raw_ostream &OS) : OS(OS) {}

  /// Print the call graph for module \p M.
  /// @param M Module whose call graph is printed.
  /// @param AM Module analysis manager providing the call graph.
  /// @return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

/// Printer pass for the summarized \c CallGraphAnalysis results.
class CallGraphSCCsPrinterPass
    : public RequiredPassInfoMixin<CallGraphSCCsPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct an SCC printer that writes to \p OS.
  /// @param OS Output stream for the SCC summary.
  explicit CallGraphSCCsPrinterPass(raw_ostream &OS) : OS(OS) {}

  /// Print the call-graph SCCs for module \p M.
  /// @param M Module whose call-graph SCCs are printed.
  /// @param AM Module analysis manager providing the call graph.
  /// @return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

/// The \c ModulePass which wraps up a \c CallGraph and the logic to
/// build it.
///
/// This class exposes both the interface to the call graph container and the
/// module pass which runs over a module of IR and produces the call graph. The
/// call graph interface is entirelly a wrapper around a \c CallGraph object
/// which is stored internally for each module.
class LLVM_ABI CallGraphWrapperPass : public ModulePass {
  std::unique_ptr<CallGraph> G;

public:
  /// Pass identification, replacement for typeinfo.
  static char ID;

  /// Construct an empty call graph wrapper pass.
  CallGraphWrapperPass();
  /// Destroy the wrapper and its owned call graph.
  ~CallGraphWrapperPass() override;

  /// The internal \c CallGraph around which the rest of this interface
  /// is wrapped.
  /// @return Const reference to the wrapped call graph.
  const CallGraph &getCallGraph() const { return *G; }
  /// Return a mutable reference to the wrapped call graph.
  /// @return Mutable reference to the wrapped call graph.
  CallGraph &getCallGraph() { return *G; }

  /// Mutable iterator over function-to-node map entries.
  using iterator = CallGraph::iterator;
  /// Const iterator over function-to-node map entries.
  using const_iterator = CallGraph::const_iterator;

  /// Returns the module the call graph corresponds to.
  /// @return The module associated with the wrapped call graph.
  Module &getModule() const { return G->getModule(); }

  /// Return an iterator to the first function map entry.
  /// @return Iterator to the first function-to-node map entry.
  inline iterator begin() { return G->begin(); }
  /// Return an iterator past the last function map entry.
  /// @return Iterator past the last function-to-node map entry.
  inline iterator end() { return G->end(); }
  /// Return a const iterator to the first function map entry.
  /// @return Const iterator to the first function-to-node map entry.
  inline const_iterator begin() const { return G->begin(); }
  /// Return a const iterator past the last function map entry.
  /// @return Const iterator past the last function-to-node map entry.
  inline const_iterator end() const { return G->end(); }

  /// Returns the call graph node for the provided function.
  /// @param F Function whose call graph node is requested.
  /// @return Const pointer to the call graph node for \p F.
  inline const CallGraphNode *operator[](const Function *F) const {
    return (*G)[F];
  }

  /// Returns the call graph node for the provided function.
  /// @param F Function whose call graph node is requested.
  /// @return Pointer to the call graph node for \p F.
  inline CallGraphNode *operator[](const Function *F) { return (*G)[F]; }

  /// Returns the \c CallGraphNode which is used to represent
  /// undetermined calls into the callgraph.
  /// @return The external-calling node for undetermined callers.
  CallGraphNode *getExternalCallingNode() const {
    return G->getExternalCallingNode();
  }

  /// Return the node representing calls that leave this module.
  /// @return The node that represents calls leaving this module.
  CallGraphNode *getCallsExternalNode() const {
    return G->getCallsExternalNode();
  }

  //===---------------------------------------------------------------------
  // Functions to keep a call graph up to date with a function that has been
  // modified.
  //

  /// Unlink the function from this module, returning it.
  ///
  /// Because this removes the function from the module, the call graph node is
  /// destroyed.  This is only valid if the function does not call any other
  /// functions (ie, there are no edges in it's CGN).  The easiest way to do
  /// this is to dropAllReferences before calling this.
  /// @param CGN Call graph node whose function is unlinked and returned.
  /// @return The function that was unlinked from the module.
  Function *removeFunctionFromModule(CallGraphNode *CGN) {
    return G->removeFunctionFromModule(CGN);
  }

  /// Similar to operator[], but this will insert a new CallGraphNode for
  /// \c F if one does not already exist.
  /// @param F Function to look up or insert into the call graph.
  /// @return The existing or newly inserted call graph node for \p F.
  CallGraphNode *getOrInsertFunction(const Function *F) {
    return G->getOrInsertFunction(F);
  }

  //===---------------------------------------------------------------------
  // Implementation of the ModulePass interface needed here.
  //

  /// Report analysis dependencies; this pass requires no prior analyses.
  /// @param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  /// Build the call graph for module \p M.
  /// @param M Module to analyze.
  /// @return False; this analysis does not modify the module.
  bool runOnModule(Module &M) override;
  /// Release the owned call graph to free memory.
  void releaseMemory() override;

  /// Print the internal call graph state to \p o.
  /// @param o Output stream.
  /// @param M Optional module context; unused by this pass.
  void print(raw_ostream &o, const Module *M) const override;
  /// Dump the wrapped call graph to stderr for debugging.
  void dump() const;
};

//===----------------------------------------------------------------------===//
// GraphTraits specializations for call graphs so that they can be treated as
// graphs by the generic graph algorithms.
//

// Provide graph traits for traversing call graphs using standard graph
// traversals.
/// GraphTraits specialization for mutable CallGraphNode pointers.
template <> struct GraphTraits<CallGraphNode *> {
  /// Graph node type for a CallGraphNode.
  using NodeRef = CallGraphNode *;
  /// Call-record pair used when mapping child iterators.
  using CGNPairTy = CallGraphNode::CallRecord;

  /// Return \p CGN as the graph entry node.
  /// @param CGN Call graph node used as the entry.
  /// @return \p CGN as the entry node for graph algorithms.
  static NodeRef getEntryNode(CallGraphNode *CGN) { return CGN; }
  /// Return the callee node from call-record pair \p P.
  /// @param P Call record whose callee is extracted.
  /// @return The callee call graph node from \p P.
  static CallGraphNode *CGNGetValue(CGNPairTy P) { return P.second; }

  /// Iterator over child CallGraphNode pointers.
  using ChildIteratorType =
      mapped_iterator<CallGraphNode::iterator, decltype(&CGNGetValue)>;

  /// Return the begin iterator over callees of \p N.
  /// @param N Parent call graph node.
  /// @return Begin iterator over the callees of \p N.
  static ChildIteratorType child_begin(NodeRef N) {
    return ChildIteratorType(N->begin(), &CGNGetValue);
  }

  /// Return the end iterator over callees of \p N.
  /// @param N Parent call graph node.
  /// @return End iterator over the callees of \p N.
  static ChildIteratorType child_end(NodeRef N) {
    return ChildIteratorType(N->end(), &CGNGetValue);
  }
};

/// GraphTraits specialization for const CallGraphNode pointers.
template <> struct GraphTraits<const CallGraphNode *> {
  /// Graph node type for a const CallGraphNode.
  using NodeRef = const CallGraphNode *;
  /// Call-record pair used when mapping child iterators.
  using CGNPairTy = CallGraphNode::CallRecord;
  /// Reference type for an outgoing call-graph edge.
  using EdgeRef = const CallGraphNode::CallRecord &;

  /// Return \p CGN as the graph entry node.
  /// @param CGN Call graph node used as the entry.
  /// @return \p CGN as the entry node for graph algorithms.
  static NodeRef getEntryNode(const CallGraphNode *CGN) { return CGN; }
  /// Return the callee node from call-record pair \p P.
  /// @param P Call record whose callee is extracted.
  /// @return The callee call graph node from \p P.
  static const CallGraphNode *CGNGetValue(CGNPairTy P) { return P.second; }

  /// Iterator over child const CallGraphNode pointers.
  using ChildIteratorType =
      mapped_iterator<CallGraphNode::const_iterator, decltype(&CGNGetValue)>;
  /// Iterator over outgoing call-record edges.
  using ChildEdgeIteratorType = CallGraphNode::const_iterator;

  /// Return the begin iterator over callees of \p N.
  /// @param N Parent call graph node.
  /// @return Begin iterator over the callees of \p N.
  static ChildIteratorType child_begin(NodeRef N) {
    return ChildIteratorType(N->begin(), &CGNGetValue);
  }

  /// Return the end iterator over callees of \p N.
  /// @param N Parent call graph node.
  /// @return End iterator over the callees of \p N.
  static ChildIteratorType child_end(NodeRef N) {
    return ChildIteratorType(N->end(), &CGNGetValue);
  }

  /// Return the begin iterator over outgoing edges of \p N.
  /// @param N Parent call graph node.
  /// @return Begin iterator over the outgoing edges of \p N.
  static ChildEdgeIteratorType child_edge_begin(NodeRef N) {
    return N->begin();
  }
  /// Return the end iterator over outgoing edges of \p N.
  /// @param N Parent call graph node.
  /// @return End iterator over the outgoing edges of \p N.
  static ChildEdgeIteratorType child_edge_end(NodeRef N) { return N->end(); }

  /// Return the destination node of edge \p E.
  /// @param E Call-record edge whose callee is the destination.
  /// @return The destination call graph node of \p E.
  static NodeRef edge_dest(EdgeRef E) { return E.second; }
};

/// GraphTraits specialization treating a CallGraph as a graph of nodes.
template <>
struct GraphTraits<CallGraph *> : public GraphTraits<CallGraphNode *> {
  /// Function-map entry type used when iterating all nodes.
  using PairTy =
      std::pair<const Function *const, std::unique_ptr<CallGraphNode>>;

  /// Return the external-calling node as the graph entry.
  /// @param CGN Call graph whose external-calling node is the entry.
  /// @return The external-calling node of \p CGN.
  static NodeRef getEntryNode(CallGraph *CGN) {
    return CGN->getExternalCallingNode(); // Start at the external node!
  }

  /// Return the CallGraphNode stored in map entry \p P.
  /// @param P Function-map entry whose node is extracted.
  /// @return The call graph node stored in \p P.
  static CallGraphNode *CGGetValuePtr(const PairTy &P) {
    return P.second.get();
  }

  /// Iterator over all CallGraphNode pointers in the graph.
  using nodes_iterator =
      mapped_iterator<CallGraph::iterator, decltype(&CGGetValuePtr)>;

  /// Return the begin iterator over all nodes in \p CG.
  /// @param CG Call graph whose nodes are iterated.
  /// @return Begin iterator over all nodes in \p CG.
  static nodes_iterator nodes_begin(CallGraph *CG) {
    return nodes_iterator(CG->begin(), &CGGetValuePtr);
  }

  /// Return the end iterator over all nodes in \p CG.
  /// @param CG Call graph whose nodes are iterated.
  /// @return End iterator over all nodes in \p CG.
  static nodes_iterator nodes_end(CallGraph *CG) {
    return nodes_iterator(CG->end(), &CGGetValuePtr);
  }
};

/// GraphTraits specialization treating a const CallGraph as a graph of nodes.
template <>
struct GraphTraits<const CallGraph *> : public GraphTraits<
                                            const CallGraphNode *> {
  /// Function-map entry type used when iterating all nodes.
  using PairTy =
      std::pair<const Function *const, std::unique_ptr<CallGraphNode>>;

  /// Return the external-calling node as the graph entry.
  /// @param CGN Call graph whose external-calling node is the entry.
  /// @return The external-calling node of \p CGN.
  static NodeRef getEntryNode(const CallGraph *CGN) {
    return CGN->getExternalCallingNode(); // Start at the external node!
  }

  /// Return the CallGraphNode stored in map entry \p P.
  /// @param P Function-map entry whose node is extracted.
  /// @return The call graph node stored in \p P.
  static const CallGraphNode *CGGetValuePtr(const PairTy &P) {
    return P.second.get();
  }

  /// Iterator over all const CallGraphNode pointers in the graph.
  using nodes_iterator =
      mapped_iterator<CallGraph::const_iterator, decltype(&CGGetValuePtr)>;

  /// Return the begin iterator over all nodes in \p CG.
  /// @param CG Call graph whose nodes are iterated.
  /// @return Begin iterator over all nodes in \p CG.
  static nodes_iterator nodes_begin(const CallGraph *CG) {
    return nodes_iterator(CG->begin(), &CGGetValuePtr);
  }

  /// Return the end iterator over all nodes in \p CG.
  /// @param CG Call graph whose nodes are iterated.
  /// @return End iterator over all nodes in \p CG.
  static nodes_iterator nodes_end(const CallGraph *CG) {
    return nodes_iterator(CG->end(), &CGGetValuePtr);
  }
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_CALLGRAPH_H
