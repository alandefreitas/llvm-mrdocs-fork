//===-- CFGPrinter.h - CFG printer external interface -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a 'dot-cfg' analysis pass, which emits the
// cfg.<fnname>.dot file for each function in the program, with a graph of the
// CFG for that function.
//
// This file defines external functions that can be called to explicitly
// instantiate the CFG printer.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_CFGPRINTER_H
#define LLVM_ANALYSIS_CFGPRINTER_H

#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/HeatUtils.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ProfDataUtils.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DOTGraphTraits.h"
#include "llvm/Support/FormatVariadic.h"

#include <functional>
#include <sstream>

namespace llvm {
class ModuleSlotTracker;

template <class GraphType> struct GraphTraits;

/// Pass that displays a function CFG in a Graphviz viewer.
class CFGViewerPass : public RequiredPassInfoMixin<CFGViewerPass> {
public:
  /// Display the function CFG in a viewer, including basic-block contents.
  /// @param F Function whose CFG is viewed.
  /// @param AM Function analysis manager providing supporting analyses.
  /// @return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Pass that displays a function CFG in a Graphviz viewer without block bodies.
class CFGOnlyViewerPass : public RequiredPassInfoMixin<CFGOnlyViewerPass> {
public:
  /// Display the function CFG in a viewer, showing only block labels.
  /// @param F Function whose CFG is viewed.
  /// @param AM Function analysis manager providing supporting analyses.
  /// @return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Pass that writes a function CFG to a DOT file.
class CFGPrinterPass : public RequiredPassInfoMixin<CFGPrinterPass> {
public:
  /// Write the function CFG to a DOT file, including basic-block contents.
  /// @param F Function whose CFG is printed.
  /// @param AM Function analysis manager providing supporting analyses.
  /// @return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Pass that writes a function CFG to a DOT file without block bodies.
class CFGOnlyPrinterPass : public RequiredPassInfoMixin<CFGOnlyPrinterPass> {
public:
  /// Write the function CFG to a DOT file, showing only block labels.
  /// @param F Function whose CFG is printed.
  /// @param AM Function analysis manager providing supporting analyses.
  /// @return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Context for rendering a function CFG as a DOT graph.
class DOTFuncInfo {
private:
  const Function *F;
  const BlockFrequencyInfo *BFI;
  const BranchProbabilityInfo *BPI;
  std::unique_ptr<ModuleSlotTracker> MSTStorage;
  uint64_t MaxFreq;
  bool ShowHeat;
  bool EdgeWeights;
  bool RawWeights;
  using NodeIdFormatterTy =
      std::function<std::optional<std::string>(const BasicBlock *)>;
  std::optional<NodeIdFormatterTy> NodeIdFormatter;

public:
  /// Construct DOT CFG info for \p F without profile analyses.
  /// @param F Function whose CFG will be rendered.
  DOTFuncInfo(const Function *F) : DOTFuncInfo(F, nullptr, nullptr, 0) {}

  /// Destroy DOT CFG info and release owned resources.
  LLVM_ABI ~DOTFuncInfo();

  /// Construct DOT CFG info with optional profile data and node ID formatting.
  /// @param F Function whose CFG will be rendered.
  /// @param BFI Optional block frequency info used for heat colors and weights.
  /// @param BPI Optional branch probability info used for edge attributes.
  /// @param MaxFreq Maximum block frequency used to scale heat colors.
  /// @param NodeIdFormatter Optional callback that supplies custom DOT node IDs.
  LLVM_ABI
  DOTFuncInfo(const Function *F, const BlockFrequencyInfo *BFI,
              const BranchProbabilityInfo *BPI, uint64_t MaxFreq,
              std::optional<NodeIdFormatterTy> NodeIdFormatter = std::nullopt);

  /// Return the block frequency info associated with this CFG, if any.
  /// @return Block frequency analysis, or nullptr when unavailable.
  const BlockFrequencyInfo *getBFI() const { return BFI; }

  /// Return the branch probability info associated with this CFG, if any.
  /// @return Branch probability analysis, or nullptr when unavailable.
  const BranchProbabilityInfo *getBPI() const { return BPI; }

  /// Return the function whose CFG is being rendered.
  /// @return Function associated with this DOT CFG info.
  const Function *getFunction() const { return this->F; }

  /// Return a module slot tracker for printing values in this function.
  /// @return Module slot tracker owned by or cached in this object.
  LLVM_ABI ModuleSlotTracker *getModuleSlotTracker();

  /// Return the maximum block frequency used for heat-color scaling.
  /// @return Maximum frequency among blocks in the function.
  uint64_t getMaxFreq() const { return MaxFreq; }

  /// Return the profile frequency of basic block \p BB.
  /// @param BB Basic block whose frequency is requested.
  /// @return Frequency of \p BB from block frequency info.
  uint64_t getFreq(const BasicBlock *BB) const {
    return BFI->getBlockFreq(BB).getFrequency();
  }

  /// Enable or disable heat-color attributes on CFG nodes.
  /// @param ShowHeat True to color nodes by relative hotness.
  void setHeatColors(bool ShowHeat) { this->ShowHeat = ShowHeat; }

  /// Return whether heat-color attributes are enabled.
  /// @return True if heat colors should be emitted for nodes.
  bool showHeatColors() { return ShowHeat; }

  /// Enable or disable raw profile counts on CFG edges.
  /// @param RawWeights True to label edges with raw weights instead of
  /// percentages.
  void setRawEdgeWeights(bool RawWeights) { this->RawWeights = RawWeights; }

  /// Return whether edges should use raw profile weights.
  /// @return True if edge labels use raw weights rather than percentages.
  bool useRawEdgeWeights() { return RawWeights; }

  /// Enable or disable emission of edge weight attributes.
  /// @param EdgeWeights True to attach weight/probability attributes to edges.
  void setEdgeWeights(bool EdgeWeights) { this->EdgeWeights = EdgeWeights; }

  /// Return whether edge weight attributes are enabled.
  /// @return True if edge weights should be shown.
  bool showEdgeWeights() { return EdgeWeights; }

  /// Return the optional formatter used to assign custom DOT node IDs.
  /// @return Node ID formatter callback, or std::nullopt when unset.
  std::optional<NodeIdFormatterTy> getNodeIdFormatter() {
    return NodeIdFormatter;
  }
};

/// GraphTraits specialization that treats DOTFuncInfo as a CFG of basic blocks.
template <>
struct GraphTraits<DOTFuncInfo *> : public GraphTraits<const BasicBlock *> {
  /// Return the entry basic block of the CFG described by \p CFGInfo.
  /// @param CFGInfo DOT CFG info for the function being traversed.
  /// @return Entry basic block of the function.
  static NodeRef getEntryNode(DOTFuncInfo *CFGInfo) {
    return &(CFGInfo->getFunction()->getEntryBlock());
  }

  /// Iterator over the basic blocks of the function CFG.
  using nodes_iterator = pointer_iterator<Function::const_iterator>;

  /// Return an iterator to the first basic block in the CFG.
  /// @param CFGInfo DOT CFG info for the function being traversed.
  /// @return Begin iterator over the function's basic blocks.
  static nodes_iterator nodes_begin(DOTFuncInfo *CFGInfo) {
    return nodes_iterator(CFGInfo->getFunction()->begin());
  }

  /// Return the end iterator for the CFG's basic blocks.
  /// @param CFGInfo DOT CFG info for the function being traversed.
  /// @return End iterator over the function's basic blocks.
  static nodes_iterator nodes_end(DOTFuncInfo *CFGInfo) {
    return nodes_iterator(CFGInfo->getFunction()->end());
  }

  /// Return the number of basic blocks in the CFG.
  /// @param CFGInfo DOT CFG info for the function being traversed.
  /// @return Number of basic blocks in the function.
  static size_t size(DOTFuncInfo *CFGInfo) {
    return CFGInfo->getFunction()->size();
  }
};

/// Build a simple DOT label for a basic block from its name or operand form.
/// @param Node Basic block to label.
/// @return Name of \p Node, or its printed operand form when unnamed.
template <typename BasicBlockT>
std::string SimpleNodeLabelString(const BasicBlockT *Node) {
  if (!Node->getName().empty())
    return Node->getName().str();

  std::string Str;
  raw_string_ostream OS(Str);

  Node->printAsOperand(OS, false);
  return Str;
}

/// Build a detailed DOT label for a basic block, wrapping and stripping comments.
/// @param Node Basic block to label.
/// @param HandleBasicBlock Callback that prints the block body into a stream.
/// @param HandleComment Callback that erases or rewrites a comment span.
/// @return Formatted multi-line DOT label string for \p Node.
template <typename BasicBlockT>
std::string CompleteNodeLabelString(
    const BasicBlockT *Node,
    function_ref<void(raw_string_ostream &, const BasicBlockT &)>
        HandleBasicBlock,
    function_ref<void(std::string &, unsigned &, unsigned)>
        HandleComment) {

  enum { MaxColumns = 80 };
  std::string OutStr;
  raw_string_ostream OS(OutStr);
  HandleBasicBlock(OS, *Node);
  // Remove "%" from BB name
  if (OutStr[0] == '%') {
    OutStr.erase(OutStr.begin());
  }
  // Place | after BB name to separate it into header
  OutStr.insert(OutStr.find_first_of('\n') + 1, "\\|");

  unsigned ColNum = 0;
  unsigned LastSpace = 0;
  for (unsigned i = 0; i != OutStr.length(); ++i) {
    if (OutStr[i] == '\n') { // Left justify
      OutStr[i] = '\\';
      OutStr.insert(OutStr.begin() + i + 1, 'l');
      ColNum = 0;
      LastSpace = 0;
    } else if (OutStr[i] == ';') {             // Delete comments!
      unsigned Idx = OutStr.find('\n', i + 1); // Find end of line
      HandleComment(OutStr, i, Idx);
    } else if (ColNum == MaxColumns) { // Wrap lines.
      // Wrap very long names even though we can't find a space.
      if (!LastSpace)
        LastSpace = i;
      OutStr.insert(LastSpace, "\\l...");
      ColNum = i - LastSpace;
      LastSpace = 0;
      i += 3; // The loop will advance 'i' again.
    } else
      ++ColNum;
    if (OutStr[i] == ' ')
      LastSpace = i;
  }
  return OutStr;
}

/// DOTGraphTraits specialization for rendering a function CFG via DOTFuncInfo.
template <>
struct DOTGraphTraits<DOTFuncInfo *> : public DefaultDOTGraphTraits {

  /// Cache of whether each block lies only on deopt or unreachable paths.
  DenseMap<const BasicBlock *, bool> isOnDeoptOrUnreachablePath;

  /// Construct DOT traits for a CFG, optionally in simple (label-only) mode.
  /// @param isSimple True to emit simple node labels without block bodies.
  DOTGraphTraits(bool isSimple = false) : DefaultDOTGraphTraits(isSimple) {}

  /// Erase a comment span from a DOT label string being formatted.
  /// @param OutStr Label string being edited.
  /// @param I Index of the comment start; updated to the character before it.
  /// @param Idx Index one past the end of the comment span.
  static void eraseComment(std::string &OutStr, unsigned &I, unsigned Idx) {
    OutStr.erase(OutStr.begin() + I, OutStr.begin() + Idx);
    --I;
  }

  /// Return the DOT graph title for the CFG of \p CFGInfo.
  /// @param CFGInfo DOT CFG info for the function being rendered.
  /// @return Graph title naming the function.
  static std::string getGraphName(DOTFuncInfo *CFGInfo) {
    return "CFG for '" + CFGInfo->getFunction()->getName().str() + "' function";
  }

  /// Return a simple DOT label for \p Node using only its name.
  /// @param Node Basic block to label.
  /// @param CFGInfo DOT CFG info (unused for simple labels).
  /// @return Simple name-based label for \p Node.
  static std::string getSimpleNodeLabel(const BasicBlock *Node,
                                        DOTFuncInfo *CFGInfo) {
    return SimpleNodeLabelString(Node);
  }

  /// Return a detailed DOT label for \p Node, including instruction text.
  /// @param Node Basic block to label.
  /// @param CFGInfo DOT CFG info providing a module slot tracker when available.
  /// @param HandleBasicBlock Optional callback that prints the block body.
  /// @param HandleComment Optional callback that strips or rewrites comments.
  /// @return Complete multi-line DOT label for \p Node.
  LLVM_ABI static std::string getCompleteNodeLabel(
      const BasicBlock *Node, DOTFuncInfo *CFGInfo,
      function_ref<void(raw_string_ostream &, const BasicBlock &)>
          HandleBasicBlock = {},
      function_ref<void(std::string &, unsigned &, unsigned)> HandleComment =
          eraseComment);

  /// Return the DOT node label for \p Node in simple or complete form.
  /// @param Node Basic block to label.
  /// @param CFGInfo DOT CFG info for the function being rendered.
  /// @return Simple or complete label depending on isSimple().
  std::string getNodeLabel(const BasicBlock *Node, DOTFuncInfo *CFGInfo) {

    if (isSimple())
      return getSimpleNodeLabel(Node, CFGInfo);
    else
      return getCompleteNodeLabel(Node, CFGInfo);
  }

  /// Return a label for the source of the successor edge pointed to by \p I.
  /// @param Node Basic block that owns the outgoing edge.
  /// @param I Successor iterator identifying the edge.
  /// @return "T"/"F" for conditional branches, case value or "def" for
  /// switches, or an empty string otherwise.
  static std::string getEdgeSourceLabel(const BasicBlock *Node,
                                        const_succ_iterator I) {
    // Label source of conditional branches with "T" or "F"
    if (isa<CondBrInst>(Node->getTerminator()))
      return (I == succ_begin(Node)) ? "T" : "F";

    // Label source of switch edges with the associated value.
    if (const SwitchInst *SI = dyn_cast<SwitchInst>(Node->getTerminator())) {
      unsigned SuccNo = std::distance(succ_begin(SI), I);

      if (SuccNo == 0)
        return "def";

      std::string Str;
      raw_string_ostream OS(Str);
      auto Case = *SwitchInst::ConstCaseIt::fromSuccessorIndex(SI, SuccNo);
      OS << Case.getCaseValue()->getValue();
      return Str;
    }
    return "";
  }

  /// Return a printable name for basic block \p Node.
  /// @param Node Basic block whose name is requested.
  /// @return Name of \p Node, or its printed operand form without a leading %.
  static std::string getBBName(const BasicBlock *Node) {
    std::string NodeName = Node->getName().str();
    if (NodeName.empty()) {
      raw_string_ostream NodeOS(NodeName);
      Node->printAsOperand(NodeOS, false);
      // Removing %
      NodeName.erase(NodeName.begin());
    }
    return NodeName;
  }

  /// Display the raw branch weights from PGO.
  /// @param Node Source basic block of the edge.
  /// @param I Successor iterator identifying the edge.
  /// @param CFGInfo DOT CFG info controlling weight display options.
  /// @return DOT attribute string for the edge, or empty when weights are off.
  std::string getEdgeAttributes(const BasicBlock *Node, const_succ_iterator I,
                                DOTFuncInfo *CFGInfo) {
    // If BPI is not provided do not display any edge attributes
    if (!CFGInfo->showEdgeWeights())
      return "";

    const Instruction *TI = Node->getTerminator();
    unsigned OpNo = std::distance(succ_begin(TI), I);
    BasicBlock *SuccBB = TI->getSuccessor(OpNo);
    auto BranchProb = CFGInfo->getBPI()->getEdgeProbability(Node, SuccBB);
    double WeightPercent = ((double)BranchProb.getNumerator()) /
                           ((double)BranchProb.getDenominator());
    std::string TTAttr =
        formatv("tooltip=\"{0} -> {1}\\nProbability {2:P}\" ", getBBName(Node),
                getBBName(SuccBB), WeightPercent);

    if (TI->getNumSuccessors() == 1)
      return TTAttr + "penwidth=2";

    if (OpNo >= TI->getNumSuccessors())
      return TTAttr;

    double Width = 1 + WeightPercent;

    if (!CFGInfo->useRawEdgeWeights())
      return TTAttr +
             formatv("label=\"{0:P}\" penwidth={1}", WeightPercent, Width)
                 .str();

    // Prepend a 'W' to indicate that this is a weight rather than the actual
    // profile count (due to scaling).

    uint64_t Freq = CFGInfo->getFreq(Node);
    std::string Attrs =
        TTAttr + formatv("label=\"W:{0}\" penwidth={1}",
                         (uint64_t)(Freq * WeightPercent), Width)
                     .str();
    if (Attrs.size())
      return Attrs;

    MDNode *WeightsNode = getBranchWeightMDNode(*TI);
    if (!WeightsNode)
      return TTAttr;

    OpNo += 1;
    if (OpNo >= WeightsNode->getNumOperands())
      return TTAttr;
    ConstantInt *Weight =
        mdconst::dyn_extract<ConstantInt>(WeightsNode->getOperand(OpNo));
    if (!Weight)
      return TTAttr;
    return (TTAttr + "label=\"W:" + std::to_string(Weight->getZExtValue()) +
            "\" penwidth=" + std::to_string(Width));
  }

  /// Return DOT attributes for \p Node, such as custom IDs and heat colors.
  /// @param Node Basic block whose attributes are requested.
  /// @param CFGInfo DOT CFG info controlling heat colors and node IDs.
  /// @return Comma-separated DOT attribute string, possibly empty.
  std::string getNodeAttributes(const BasicBlock *Node, DOTFuncInfo *CFGInfo) {
    std::stringstream Attrs;

    if (auto NodeIdFmt = CFGInfo->getNodeIdFormatter())
      if (auto NodeId = (*NodeIdFmt)(Node))
        Attrs << "id=\"" << *NodeId << "\"";

    if (CFGInfo->showHeatColors()) {
      uint64_t Freq = CFGInfo->getFreq(Node);
      std::string Color = getHeatColor(Freq, CFGInfo->getMaxFreq());
      std::string EdgeColor = (Freq <= (CFGInfo->getMaxFreq() / 2))
                                  ? (getHeatColor(0))
                                  : (getHeatColor(1));
      if (!Attrs.str().empty())
        Attrs << ",";
      Attrs << "color=\"" << EdgeColor << "ff\", style=filled, "
            << "fillcolor=\"" << Color << "70\", " << "fontname=\"Courier\"";
    }

    return Attrs.str();
  }

  /// Return whether \p Node should be omitted from the rendered CFG.
  /// @param Node Basic block to test for hiding.
  /// @param CFGInfo DOT CFG info providing profile data for cold-path hiding.
  /// @return True if \p Node should not be displayed.
  LLVM_ABI bool isNodeHidden(const BasicBlock *Node,
                             const DOTFuncInfo *CFGInfo);

  /// Compute which blocks lie only on deoptimize or unreachable paths.
  /// @param F Function whose CFG paths are analyzed into the cache.
  LLVM_ABI void computeDeoptOrUnreachablePaths(const Function *F);
};
} // namespace llvm

#endif
