//===- CallGraphSCCPass.h - Pass that operates BU on call graph -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the CallGraphSCCPass class, which is used for passes which
// are implemented as bottom-up traversals on the call graph.  Because there may
// be cycles in the call graph, passes of this type operate on the call-graph in
// SCC order: that is, they process function bottom-up, except for recursive
// functions, which they process all at once.
//
// These passes are inherently interprocedural, and are required to keep the
// call graph up-to-date if they do anything which could modify it.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_CALLGRAPHSCCPASS_H
#define LLVM_ANALYSIS_CALLGRAPHSCCPASS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Pass.h"
#include "llvm/Support/Compiler.h"
#include <vector>

namespace llvm {

class CallGraph;
class CallGraphNode;
class CallGraphSCC;
class PMStack;

/// Base class for passes that run bottom-up over call-graph SCCs.
class LLVM_ABI CallGraphSCCPass : public Pass {
public:
  /// Construct a call-graph SCC pass with Pass ID \p pid.
  /// @param pid Static Pass ID for this pass instance.
  explicit CallGraphSCCPass(char &pid) : Pass(PT_CallGraphSCC, pid) {}

  /// createPrinterPass - Get a pass that prints the Module
  /// corresponding to a CallGraph.
  /// @param OS Stream to write the printed IR to.
  /// @param Banner Banner text printed before the IR.
  /// @return A pass that prints the Module corresponding to a CallGraph.
  Pass *createPrinterPass(raw_ostream &OS,
                          const std::string &Banner) const override;

  /// Bring \c Pass::doInitialization into scope for overload resolution.
  using llvm::Pass::doInitialization;
  /// Bring \c Pass::doFinalization into scope for overload resolution.
  using llvm::Pass::doFinalization;

  /// doInitialization - This method is called before the SCC's of the program
  /// has been processed, allowing the pass to do initialization as necessary.
  /// @param CG Call graph of the module being processed.
  /// @return True if the pass modifies the call graph during initialization.
  virtual bool doInitialization(CallGraph &CG) {
    return false;
  }

  /// Run this pass on the strongly connected component \p SCC.
  ///
  /// Note that non-recursive (or only self-recursive) functions will have an
  /// SCC size of 1, where recursive portions of the call graph will have SCC
  /// size > 1.
  ///
  /// SCC passes that add or delete functions to the SCC are required to update
  /// the SCC list, otherwise stale pointers may be dereferenced.
  /// @param SCC Strongly connected component to process.
  /// @return True if the pass modifies this SCC.
  virtual bool runOnSCC(CallGraphSCC &SCC) = 0;

  /// doFinalization - This method is called after the SCC's of the program has
  /// been processed, allowing the pass to do final cleanup as necessary.
  /// @param CG Call graph of the module being processed.
  /// @return True if the pass modifies the call graph during finalization.
  virtual bool doFinalization(CallGraph &CG) {
    return false;
  }

  /// Assign pass manager to manager this pass
  /// @param PMS Stack of pass managers to search or update.
  /// @param PMT Preferred pass manager type for this pass.
  void assignPassManager(PMStack &PMS, PassManagerType PMT) override;

  ///  Return what kind of Pass Manager can manage this pass.
  /// @return \c PMT_CallGraphPassManager.
  PassManagerType getPotentialPassManagerType() const override {
    return PMT_CallGraphPassManager;
  }

  /// Declare that this pass requires and preserves the call graph.
  ///
  /// If the derived class implements this method, it should always explicitly
  /// call the implementation here.
  /// @param Info Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &Info) const override;
};

/// CallGraphSCC - This is a single SCC that a CallGraphSCCPass is run on.
class CallGraphSCC {
  const CallGraph &CG; // The call graph for this SCC.
  void *Context; // The CGPassManager object that is vending this.
  std::vector<CallGraphNode *> Nodes;

public:
  /// Construct an SCC for call graph \p cg owned by pass-manager \p context.
  /// @param cg Call graph that contains this SCC.
  /// @param context Opaque CGPassManager that vends this SCC.
  CallGraphSCC(CallGraph &cg, void *context) : CG(cg), Context(context) {}

  /// Replace the nodes in this SCC with \p NewNodes.
  /// @param NewNodes Call-graph nodes that form this SCC.
  void initialize(ArrayRef<CallGraphNode *> NewNodes) {
    Nodes.assign(NewNodes.begin(), NewNodes.end());
  }

  /// Return true if this SCC contains exactly one node.
  /// @return True if this SCC contains exactly one node.
  bool isSingular() const { return Nodes.size() == 1; }
  /// Return the number of call-graph nodes in this SCC.
  /// @return The number of call-graph nodes in this SCC.
  unsigned size() const { return Nodes.size(); }

  /// ReplaceNode - This informs the SCC and the pass manager that the specified
  /// Old node has been deleted, and New is to be used in its place.
  /// @param Old Call-graph node being replaced.
  /// @param New Call-graph node that takes \p Old's place.
  LLVM_ABI void ReplaceNode(CallGraphNode *Old, CallGraphNode *New);

  /// DeleteNode - This informs the SCC and the pass manager that the specified
  /// Old node has been deleted.
  /// @param Old Call-graph node that has been deleted.
  LLVM_ABI void DeleteNode(CallGraphNode *Old);

  /// Const iterator over the call-graph nodes in this SCC.
  using iterator = std::vector<CallGraphNode *>::const_iterator;

  /// Return an iterator to the first call-graph node in this SCC.
  /// @return An iterator to the first call-graph node in this SCC.
  iterator begin() const { return Nodes.begin(); }
  /// Return an iterator past the last call-graph node in this SCC.
  /// @return An iterator past the last call-graph node in this SCC.
  iterator end() const { return Nodes.end(); }

  /// Return the call graph that contains this SCC.
  /// @return The call graph that contains this SCC.
  const CallGraph &getCallGraph() { return CG; }
};

/// This pass is required by interprocedural register allocation. It forces
/// codegen to follow bottom up order on call graph.
class DummyCGSCCPass : public CallGraphSCCPass {
public:
  /// Pass identification, replacement for typeid.
  LLVM_ABI static char ID;

  /// Construct a no-op CGSCC pass used to enforce bottom-up codegen order.
  DummyCGSCCPass() : CallGraphSCCPass(ID) {}

  /// Run this pass on \p SCC without modifying the call graph.
  /// @param SCC Strongly connected component to visit.
  /// @return False; this pass does not modify the call graph.
  bool runOnSCC(CallGraphSCC &SCC) override { return false; }

  /// Preserve all analyses; this pass makes no transformations.
  /// @param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_CALLGRAPHSCCPASS_H
