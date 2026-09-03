//===-- DOTGraphTraitsPass.h - Print/View dotty graphs-----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Templates to create dotty viewer and printer passes for GraphTraits graphs.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_DOTGRAPHTRAITSPASS_H
#define LLVM_ANALYSIS_DOTGRAPHTRAITSPASS_H

#include "llvm/ADT/StringSet.h"
#include "llvm/Analysis/CFGPrinter.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/GraphWriter.h"

static llvm::StringSet<> nameObj;

namespace llvm {

/// Default traits class for extracting a graph from an analysis pass.
///
/// This assumes that 'GraphT' is 'AnalysisT::Result *', and pass it through
template <typename Result, typename GraphT = Result *>
struct DefaultAnalysisGraphTraits {
  /// Extract a graph pointer from an analysis result.
  /// @param R Analysis result to wrap as a graph.
  /// @return Pointer to \p R used as the graph.
  static GraphT getGraph(Result R) { return &R; }
};

/// Open a Graphviz viewer for a function's analysis graph.
/// @param F Function the graph belongs to.
/// @param Graph Graph extracted from the analysis result.
/// @param Name Base name for the viewer window.
/// @param IsSimple True to render simplified node labels.
template <typename GraphT>
void viewGraphForFunction(Function &F, GraphT Graph, StringRef Name,
                          bool IsSimple) {
  std::string GraphName = DOTGraphTraits<GraphT *>::getGraphName(&Graph);

  ViewGraph(Graph, Name, IsSimple,
            GraphName + " for '" + F.getName() + "' function");
}

/// Pass mixin that displays an analysis graph for a function in a viewer.
template <typename AnalysisT, bool IsSimple,
          typename GraphT = typename AnalysisT::Result *,
          typename AnalysisGraphTraitsT =
              DefaultAnalysisGraphTraits<typename AnalysisT::Result &, GraphT>>
struct DOTGraphTraitsViewer
    : RequiredPassInfoMixin<DOTGraphTraitsViewer<AnalysisT, IsSimple, GraphT,
                                                 AnalysisGraphTraitsT>> {
  /// Construct a viewer pass with the given graph name.
  /// @param GraphName Name used when displaying the graph.
  DOTGraphTraitsViewer(StringRef GraphName) : Name(GraphName) {}

  /// Return true if this function should be processed.
  ///
  /// An implementation of this class my override this function to indicate that
  /// only certain functions should be viewed.
  ///
  /// @param F Function under consideration.
  /// @param Result The current analysis result for this function.
  /// @return True if this function should be processed.
  virtual bool processFunction(Function &F,
                               const typename AnalysisT::Result &Result) {
    return true;
  }

  /// Display the analysis graph for \p F in a Graphviz viewer.
  /// @param F Function whose analysis graph is viewed.
  /// @param FAM Function analysis manager providing AnalysisT.
  /// @return Preserved analyses; this pass preserves all.
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
    auto &Result = FAM.getResult<AnalysisT>(F);
    if (!processFunction(F, Result))
      return PreservedAnalyses::all();

    GraphT Graph = AnalysisGraphTraitsT::getGraph(Result);
    viewGraphForFunction(F, Graph, Name, IsSimple);

    return PreservedAnalyses::all();
  };

protected:
  /// Avoid compiler warning "has virtual functions but non-virtual destructor
  /// [-Wnon-virtual-dtor]" in derived classes.
  ///
  /// DOTGraphTraitsViewer is also used as a mixin for avoiding repeated
  /// implementation of viewer passes, ie there should be no
  /// runtime-polymorphisms/downcasting involving this class and hence no
  /// virtual destructor needed. Making this dtor protected stops accidental
  /// invocation when the derived class destructor should have been called.
  /// Those derived classes sould be marked final to avoid the warning.
  ~DOTGraphTraitsViewer() = default;

private:
  StringRef Name;
};

static inline void shortenFileName(std::string &FN, unsigned char len = 250) {
  if (FN.length() > len)
    FN.resize(len);
  auto strLen = FN.length();
  while (strLen > 0) {
    if (nameObj.insert(FN).second)
      break;
    FN.resize(--len);
    strLen--;
  }
}

/// Write an analysis graph for a function to a DOT file.
/// @param F Function the graph belongs to.
/// @param Graph Graph extracted from the analysis result.
/// @param Name Base name used to build the output filename.
/// @param IsSimple True to render simplified node labels.
template <typename GraphT>
void printGraphForFunction(Function &F, GraphT Graph, StringRef Name,
                           bool IsSimple) {
  std::string Filename = Name.str() + "." + F.getName().str();
  shortenFileName(Filename);
  Filename = Filename + ".dot";
  std::error_code EC;

  errs() << "Writing '" << Filename << "'...";

  raw_fd_ostream File(Filename, EC, sys::fs::OF_TextWithCRLF);
  std::string GraphName = DOTGraphTraits<GraphT>::getGraphName(Graph);

  if (!EC)
    WriteGraph(File, Graph, IsSimple,
               GraphName + " for '" + F.getName() + "' function");
  else
    errs() << "  error opening file for writing!";
  errs() << "\n";
}

/// Pass mixin that writes an analysis graph for a function to a DOT file.
template <typename AnalysisT, bool IsSimple,
          typename GraphT = typename AnalysisT::Result *,
          typename AnalysisGraphTraitsT =
              DefaultAnalysisGraphTraits<typename AnalysisT::Result &, GraphT>>
struct DOTGraphTraitsPrinter
    : RequiredPassInfoMixin<DOTGraphTraitsPrinter<AnalysisT, IsSimple, GraphT,
                                                  AnalysisGraphTraitsT>> {
  /// Construct a printer pass with the given graph name.
  /// @param GraphName Name used when naming the DOT output file.
  DOTGraphTraitsPrinter(StringRef GraphName) : Name(GraphName) {}

  /// Return true if this function should be processed.
  ///
  /// An implementation of this class my override this function to indicate that
  /// only certain functions should be viewed.
  ///
  /// @param F Function under consideration.
  /// @param Result The current analysis result for this function.
  /// @return True if this function should be processed.
  virtual bool processFunction(Function &F,
                               const typename AnalysisT::Result &Result) {
    return true;
  }

  /// Write the analysis graph for \p F to a DOT file.
  /// @param F Function whose analysis graph is printed.
  /// @param FAM Function analysis manager providing AnalysisT.
  /// @return Preserved analyses; this pass preserves all.
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
    auto &Result = FAM.getResult<AnalysisT>(F);
    if (!processFunction(F, Result))
      return PreservedAnalyses::all();

    GraphT Graph = AnalysisGraphTraitsT::getGraph(Result);

    printGraphForFunction(F, Graph, Name, IsSimple);

    return PreservedAnalyses::all();
  };

protected:
  /// Avoid compiler warning "has virtual functions but non-virtual destructor
  /// [-Wnon-virtual-dtor]" in derived classes.
  ///
  /// DOTGraphTraitsPrinter is also used as a mixin for avoiding repeated
  /// implementation of printer passes, ie there should be no
  /// runtime-polymorphisms/downcasting involving this class and hence no
  /// virtual destructor needed. Making this dtor protected stops accidental
  /// invocation when the derived class destructor should have been called.
  /// Those derived classes sould be marked final to avoid the warning.
  ~DOTGraphTraitsPrinter() = default;

private:
  StringRef Name;
};

/// Default traits class for extracting a graph from an analysis pass.
///
/// This assumes that 'GraphT' is 'AnalysisT *' and so just passes it through.
template <typename AnalysisT, typename GraphT = AnalysisT *>
struct LegacyDefaultAnalysisGraphTraits {
  /// Extract a graph pointer from a legacy analysis pass.
  /// @param A Analysis pass instance to wrap as a graph.
  /// @return Pointer to \p A used as the graph.
  static GraphT getGraph(AnalysisT *A) { return A; }
};

/// Legacy function pass that displays an analysis graph in a viewer.
template <typename AnalysisT, bool IsSimple, typename GraphT = AnalysisT *,
          typename AnalysisGraphTraitsT =
              LegacyDefaultAnalysisGraphTraits<AnalysisT, GraphT>>
class DOTGraphTraitsViewerWrapperPass : public FunctionPass {
public:
  /// Construct a legacy viewer pass with the given graph name and ID.
  /// @param GraphName Name used when displaying the graph.
  /// @param ID Pass identifier assigned by LLVM.
  DOTGraphTraitsViewerWrapperPass(StringRef GraphName, char &ID)
      : FunctionPass(ID), Name(GraphName) {}

  /// Return true if this function should be processed.
  ///
  /// An implementation of this class my override this function to indicate that
  /// only certain functions should be viewed.
  ///
  /// @param F Function under consideration.
  /// @param Analysis The current analysis result for this function.
  /// @return True if this function should be processed.
  virtual bool processFunction(Function &F, AnalysisT &Analysis) {
    return true;
  }

  /// Display the analysis graph for \p F in a Graphviz viewer.
  /// @param F Function whose analysis graph is viewed.
  /// @return False; this pass does not modify the function.
  bool runOnFunction(Function &F) override {
    auto &Analysis = getAnalysis<AnalysisT>();

    if (!processFunction(F, Analysis))
      return false;

    GraphT Graph = AnalysisGraphTraitsT::getGraph(&Analysis);
    viewGraphForFunction(F, Graph, Name, IsSimple);

    return false;
  }

  /// Declare that this pass requires AnalysisT and preserves all analyses.
  /// @param AU Analysis usage to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    AU.addRequired<AnalysisT>();
  }

private:
  std::string Name;
};

/// Legacy function pass that writes an analysis graph to a DOT file.
template <typename AnalysisT, bool IsSimple, typename GraphT = AnalysisT *,
          typename AnalysisGraphTraitsT =
              LegacyDefaultAnalysisGraphTraits<AnalysisT, GraphT>>
class DOTGraphTraitsPrinterWrapperPass : public FunctionPass {
public:
  /// Construct a legacy printer pass with the given graph name and ID.
  /// @param GraphName Name used when naming the DOT output file.
  /// @param ID Pass identifier assigned by LLVM.
  DOTGraphTraitsPrinterWrapperPass(StringRef GraphName, char &ID)
      : FunctionPass(ID), Name(GraphName) {}

  /// Return true if this function should be processed.
  ///
  /// An implementation of this class my override this function to indicate that
  /// only certain functions should be printed.
  ///
  /// @param F Function under consideration.
  /// @param Analysis The current analysis result for this function.
  /// @return True if this function should be processed.
  virtual bool processFunction(Function &F, AnalysisT &Analysis) {
    return true;
  }

  /// Write the analysis graph for \p F to a DOT file.
  /// @param F Function whose analysis graph is printed.
  /// @return False; this pass does not modify the function.
  bool runOnFunction(Function &F) override {
    auto &Analysis = getAnalysis<AnalysisT>();

    if (!processFunction(F, Analysis))
      return false;

    GraphT Graph = AnalysisGraphTraitsT::getGraph(&Analysis);
    printGraphForFunction(F, Graph, Name, IsSimple);

    return false;
  }

  /// Declare that this pass requires AnalysisT and preserves all analyses.
  /// @param AU Analysis usage to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    AU.addRequired<AnalysisT>();
  }

private:
  std::string Name;
};

/// Legacy module pass that displays an analysis graph in a viewer.
template <typename AnalysisT, bool IsSimple, typename GraphT = AnalysisT *,
          typename AnalysisGraphTraitsT =
              LegacyDefaultAnalysisGraphTraits<AnalysisT, GraphT>>
class DOTGraphTraitsModuleViewerWrapperPass : public ModulePass {
public:
  /// Construct a legacy module viewer pass with the given graph name and ID.
  /// @param GraphName Name used when displaying the graph.
  /// @param ID Pass identifier assigned by LLVM.
  DOTGraphTraitsModuleViewerWrapperPass(StringRef GraphName, char &ID)
      : ModulePass(ID), Name(GraphName) {}

  /// Display the module analysis graph in a Graphviz viewer.
  /// @param M Module whose analysis graph is viewed.
  /// @return False; this pass does not modify the module.
  bool runOnModule(Module &M) override {
    GraphT Graph = AnalysisGraphTraitsT::getGraph(&getAnalysis<AnalysisT>());
    std::string Title = DOTGraphTraits<GraphT>::getGraphName(Graph);

    ViewGraph(Graph, Name, IsSimple, Title);

    return false;
  }

  /// Declare that this pass requires AnalysisT and preserves all analyses.
  /// @param AU Analysis usage to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    AU.addRequired<AnalysisT>();
  }

private:
  std::string Name;
};

/// Legacy module pass that writes an analysis graph to a DOT file.
template <typename AnalysisT, bool IsSimple, typename GraphT = AnalysisT *,
          typename AnalysisGraphTraitsT =
              LegacyDefaultAnalysisGraphTraits<AnalysisT, GraphT>>
class DOTGraphTraitsModulePrinterWrapperPass : public ModulePass {
public:
  /// Construct a legacy module printer pass with the given graph name and ID.
  /// @param GraphName Name used when naming the DOT output file.
  /// @param ID Pass identifier assigned by LLVM.
  DOTGraphTraitsModulePrinterWrapperPass(StringRef GraphName, char &ID)
      : ModulePass(ID), Name(GraphName) {}

  /// Write the module analysis graph to a DOT file.
  /// @param M Module whose analysis graph is printed.
  /// @return False; this pass does not modify the module.
  bool runOnModule(Module &M) override {
    GraphT Graph = AnalysisGraphTraitsT::getGraph(&getAnalysis<AnalysisT>());
    shortenFileName(Name);
    std::string Filename = Name + ".dot";
    std::error_code EC;

    errs() << "Writing '" << Filename << "'...";

    raw_fd_ostream File(Filename, EC, sys::fs::OF_TextWithCRLF);
    std::string Title = DOTGraphTraits<GraphT>::getGraphName(Graph);

    if (!EC)
      WriteGraph(File, Graph, IsSimple, Title);
    else
      errs() << "  error opening file for writing!";
    errs() << "\n";

    return false;
  }

  /// Declare that this pass requires AnalysisT and preserves all analyses.
  /// @param AU Analysis usage to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    AU.addRequired<AnalysisT>();
  }

private:
  std::string Name;
};

/// Write a function analysis graph to a DOT file with a custom name prefix.
/// @param F Function the graph belongs to.
/// @param Graph Graph to serialize.
/// @param FileNamePrefix Prefix used to build the output filename.
/// @param IsSimple True to render simplified node labels.
template <typename GraphT>
void WriteDOTGraphToFile(Function &F, GraphT &&Graph,
                         std::string FileNamePrefix, bool IsSimple) {
  std::string Filename = FileNamePrefix + "." + F.getName().str();
  shortenFileName(Filename);
  Filename = Filename + ".dot";
  std::error_code EC;

  errs() << "Writing '" << Filename << "'...";

  raw_fd_ostream File(Filename, EC, sys::fs::OF_TextWithCRLF);
  std::string GraphName = DOTGraphTraits<GraphT>::getGraphName(Graph);
  std::string Title = GraphName + " for '" + F.getName().str() + "' function";

  if (!EC)
    WriteGraph(File, Graph, IsSimple, Title);
  else
    errs() << "  error opening file for writing!";
  errs() << "\n";
}

} // end namespace llvm

#endif
