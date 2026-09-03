//===-- MachineCFGPrinter.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/CFGPrinter.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/Support/DOTGraphTraits.h"

namespace llvm {

template <class GraphType> struct GraphTraits;

/// Context for rendering a machine function CFG as a DOT graph.
class DOTMachineFuncInfo {
private:
  const MachineFunction *F;

public:
  /// Construct DOT machine CFG info for \p F.
  /// @param F Machine function whose CFG will be rendered.
  DOTMachineFuncInfo(const MachineFunction *F) : F(F) {}

  /// Return the machine function whose CFG is being rendered.
  /// @return Machine function associated with this DOT CFG info.
  const MachineFunction *getFunction() const { return this->F; }
};

/// GraphTraits specialization that treats DOTMachineFuncInfo as a CFG of
/// machine basic blocks.
template <>
struct GraphTraits<DOTMachineFuncInfo *>
    : public GraphTraits<const MachineBasicBlock *> {
  /// Return the entry machine basic block of the CFG described by \p CFGInfo.
  /// @param CFGInfo DOT machine CFG info for the function being traversed.
  /// @return Entry machine basic block of the function.
  static NodeRef getEntryNode(DOTMachineFuncInfo *CFGInfo) {
    return &(CFGInfo->getFunction()->front());
  }

  /// Iterator over the machine basic blocks of the function CFG.
  using nodes_iterator = pointer_iterator<MachineFunction::const_iterator>;

  /// Return an iterator to the first machine basic block in the CFG.
  /// @param CFGInfo DOT machine CFG info for the function being traversed.
  /// @return Begin iterator over the function's machine basic blocks.
  static nodes_iterator nodes_begin(DOTMachineFuncInfo *CFGInfo) {
    return nodes_iterator(CFGInfo->getFunction()->begin());
  }

  /// Return the end iterator for the CFG's machine basic blocks.
  /// @param CFGInfo DOT machine CFG info for the function being traversed.
  /// @return End iterator over the function's machine basic blocks.
  static nodes_iterator nodes_end(DOTMachineFuncInfo *CFGInfo) {
    return nodes_iterator(CFGInfo->getFunction()->end());
  }

  /// Return the number of machine basic blocks in the CFG.
  /// @param CFGInfo DOT machine CFG info for the function being traversed.
  /// @return Number of machine basic blocks in the function.
  static size_t size(DOTMachineFuncInfo *CFGInfo) {
    return CFGInfo->getFunction()->size();
  }
};

/// DOTGraphTraits specialization for rendering a machine CFG via
/// DOTMachineFuncInfo.
template <>
struct DOTGraphTraits<DOTMachineFuncInfo *> : public DefaultDOTGraphTraits {

  /// Construct DOT traits for a machine CFG, optionally in simple mode.
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

  /// Return a simple DOT label for \p Node using only its name.
  /// @param Node Machine basic block to label.
  /// @param CFGInfo DOT machine CFG info (unused for simple labels).
  /// @return Simple name-based label for \p Node.
  static std::string getSimpleNodeLabel(const MachineBasicBlock *Node,
                                        DOTMachineFuncInfo *CFGInfo) {
    return SimpleNodeLabelString(Node);
  }

  /// Return a detailed DOT label for \p Node, including instruction text.
  /// @param Node Machine basic block to label.
  /// @param CFGInfo DOT machine CFG info (unused by the default handlers).
  /// @param HandleBasicBlock Optional callback that prints the block body.
  /// @param HandleComment Optional callback that strips or rewrites comments.
  /// @return Complete multi-line DOT label for \p Node.
  static std::string getCompleteNodeLabel(
      const MachineBasicBlock *Node, DOTMachineFuncInfo *CFGInfo,
      function_ref<void(raw_string_ostream &, const MachineBasicBlock &)>
          HandleBasicBlock =
              [](raw_string_ostream &OS,
                 const MachineBasicBlock &Node) -> void { OS << Node; },
      function_ref<void(std::string &, unsigned &, unsigned)>
          HandleComment = eraseComment) {
    return CompleteNodeLabelString(Node, HandleBasicBlock, HandleComment);
  }

  /// Return the DOT node label for \p Node in simple or complete form.
  /// @param Node Machine basic block to label.
  /// @param CFGInfo DOT machine CFG info for the function being rendered.
  /// @return Simple or complete label depending on isSimple().
  std::string getNodeLabel(const MachineBasicBlock *Node,
                           DOTMachineFuncInfo *CFGInfo) {
    if (isSimple())
      return getSimpleNodeLabel(Node, CFGInfo);

    return getCompleteNodeLabel(Node, CFGInfo);
  }

  /// Return the DOT graph title for the machine CFG of \p CFGInfo.
  /// @param CFGInfo DOT machine CFG info for the function being rendered.
  /// @return Graph title naming the machine function.
  static std::string getGraphName(DOTMachineFuncInfo *CFGInfo) {
    return "Machine CFG for '" + CFGInfo->getFunction()->getName().str() +
           "' function";
  }
};

/// Pass that writes a machine function CFG to a DOT file.
class MachineCFGPrinterPass : public RequiredPassInfoMixin<MachineCFGPrinterPass> {
public:
  /// Write the machine function CFG to a DOT file.
  /// @param MF Machine function whose CFG is printed.
  /// @param MFAM Machine function analysis manager providing supporting
  /// analyses.
  /// @return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

} // namespace llvm
