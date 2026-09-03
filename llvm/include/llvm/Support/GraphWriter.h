//===- llvm/Support/GraphWriter.h - Write graph to a .dot file --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a simple interface that can be used to print out generic
// LLVM graphs to ".dot" files.  "dot" is a tool that is part of the AT&T
// graphviz package (http://www.research.att.com/sw/tools/graphviz/) which can
// be used to turn the files output by this interface into a variety of
// different graphics formats.
//
// Graphs do not need to implement any interface past what is already required
// by the GraphTraits template, but they can choose to implement specializations
// of the DOTGraphTraits template if they want to customize the graphs output in
// any way.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_GRAPHWRITER_H
#define LLVM_SUPPORT_GRAPHWRITER_H

#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DOTGraphTraits.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include <iterator>
#include <string>
#include <type_traits>
#include <vector>

namespace llvm {

/// Helpers for writing Graphviz DOT text.
namespace DOT {

/// Escape special characters in \p Label for inclusion in a DOT string.
///
/// \param Label Text that may contain characters special to DOT.
/// \return A copy of \p Label with DOT-special characters escaped.
LLVM_ABI std::string EscapeString(const std::string &Label);

/// Get a color string for this node number.
///
/// Simply round-robin selects from a reasonable number of colors.
///
/// \param NodeNumber Index used to pick a color in round-robin order.
/// \return A DOT color name selected for \p NodeNumber.
LLVM_ABI StringRef getColorString(unsigned NodeNumber);

} // end namespace DOT

/// Identifiers for Graphviz layout programs used to render graphs.
namespace GraphProgram {

/// Graphviz program used to layout and render a DOT graph.
enum Name {
  /// The standard \c dot hierarchical layout program.
  DOT,
  /// The \c fdp force-directed layout program.
  FDP,
  /// The \c neato spring-model layout program.
  NEATO,
  /// The \c twopi radial layout program.
  TWOPI,
  /// The \c circo circular layout program.
  CIRCO
};

} // end namespace GraphProgram

/// Display a DOT graph file with the given Graphviz program.
///
/// \param Filename Path of the DOT file to display.
/// \param wait If true, wait for the viewer process to exit.
/// \param program Graphviz program used to render the graph.
/// \return True if the graph was displayed successfully.
LLVM_ABI bool DisplayGraph(StringRef Filename, bool wait = true,
                           GraphProgram::Name program = GraphProgram::DOT);

/// CRTP base that emits a Graphviz DOT representation of a graph.
///
/// \tparam GraphType Graph type modeled by \c GraphTraits.
/// \tparam Derived Concrete writer type that customizes emission hooks.
template <typename GraphType, typename Derived> class GraphWriterBase {
protected:
  /// Stream that receives the emitted DOT text.
  raw_ostream &O;
  /// Graph being written.
  const GraphType &G;
  /// Whether nodes are emitted with HTML-like labels.
  bool RenderUsingHTML = false;

  /// DOT customization traits for \c GraphType.
  using DOTTraits = DOTGraphTraits<GraphType>;
  /// Graph traversal traits for \c GraphType.
  using GTraits = GraphTraits<GraphType>;
  /// Reference type for a node in the graph.
  using NodeRef = typename GTraits::NodeRef;
  /// Iterator over all nodes in the graph.
  using node_iterator = typename GTraits::nodes_iterator;
  /// Iterator over children of a node.
  using child_iterator = typename GTraits::ChildIteratorType;
  /// Instance of the DOT traits used while writing.
  DOTTraits DTraits;

  static_assert(std::is_pointer_v<NodeRef>,
                "FIXME: Currently GraphWriterBase requires the NodeRef type to "
                "be a pointer.\nThe pointer usage should be moved to "
                "DOTGraphTraits, and removed from GraphWriterBase itself.");

  /// Cast \c this to the derived writer type.
  ///
  /// \return A reference to this writer as \c Derived.
  Derived &getDerived() { return *static_cast<Derived *>(this); }
  /// Cast \c this to the derived writer type.
  ///
  /// \return A const reference to this writer as \c Derived.
  const Derived &getDerived() const {
    return *static_cast<const Derived *>(this);
  }

  /// Write edge source labels for \p Node to \p O.
  ///
  /// \param O Stream that receives the edge source label DOT fragment.
  /// \param Node Node whose outgoing edge source labels are written.
  /// \return True if any non-empty edge source labels were written.
  bool getEdgeSourceLabels(raw_ostream &O, NodeRef Node) {
    child_iterator EI = GTraits::child_begin(Node);
    child_iterator EE = GTraits::child_end(Node);
    bool hasEdgeSourceLabels = false;

    if (RenderUsingHTML)
      O << "</tr><tr>";

    for (unsigned i = 0; EI != EE && i != 64; ++EI, ++i) {
      std::string label = DTraits.getEdgeSourceLabel(Node, EI);

      if (label.empty())
        continue;

      hasEdgeSourceLabels = true;

      if (RenderUsingHTML)
        O << "<td colspan=\"1\" port=\"s" << i << "\">" << label << "</td>";
      else {
        if (i)
          O << "|";

        O << "<s" << i << ">" << DOT::EscapeString(label);
      }
    }

    if (EI != EE && hasEdgeSourceLabels) {
      if (RenderUsingHTML)
        O << "<td colspan=\"1\" port=\"s64\">truncated...</td>";
      else
        O << "|<s64>truncated...";
    }

    return hasEdgeSourceLabels;
  }

public:
  /// Construct a graph writer for \p g that writes to \p o.
  ///
  /// \param o Stream that receives the DOT output.
  /// \param g Graph to write.
  /// \param SN If true, request short names from the DOT traits.
  GraphWriterBase(raw_ostream &o, const GraphType &g, bool SN) : O(o), G(g) {
    DTraits = DOTTraits(SN);
    RenderUsingHTML = DTraits.renderNodesUsingHTML();
  }
  /// Destroy the graph writer.
  virtual ~GraphWriterBase() = default;

  /// Write the complete DOT graph, including header, nodes, and footer.
  ///
  /// \param Title Optional title used as the graph name and label.
  void writeGraph(const std::string &Title = "") {
    // Output the header for the graph...
    getDerived().writeHeader(Title);

    // Emit all of the nodes in the graph...
    getDerived().writeNodes();

    // Output any customizations on the graph
    DOTGraphTraits<GraphType>::addCustomGraphFeatures(G, getDerived());

    // Output the end of the graph
    getDerived().writeFooter();
  }

  /// Write the opening DOT digraph header and graph properties.
  ///
  /// \param Title Optional title used as the digraph name and label.
  void writeHeader(const std::string &Title) {
    std::string GraphName(DTraits.getGraphName(G));

    if (!Title.empty())
      O << "digraph \"" << DOT::EscapeString(Title) << "\" {\n";
    else if (!GraphName.empty())
      O << "digraph \"" << DOT::EscapeString(GraphName) << "\" {\n";
    else
      O << "digraph unnamed {\n";

    if (DTraits.renderGraphFromBottomUp())
      O << "\trankdir=\"BT\";\n";

    if (!Title.empty())
      O << "\tlabel=\"" << DOT::EscapeString(Title) << "\";\n";
    else if (!GraphName.empty())
      O << "\tlabel=\"" << DOT::EscapeString(GraphName) << "\";\n";
    O << DTraits.getGraphProperties(G);
    O << "\n";
  }

  /// Write the closing brace that ends the DOT digraph.
  void writeFooter() {
    // Finish off the graph
    O << "}\n";
  }

  /// Write every non-hidden node in the graph.
  void writeNodes() {
    // Loop over the graph, printing it out...
    for (const auto Node : nodes<GraphType>(G))
      if (!getDerived().isNodeHidden(Node))
        getDerived().writeNode(Node);
  }

  /// Return true if \p Node should be omitted from the DOT output.
  ///
  /// \param Node Node to test for visibility.
  /// \return True if \p Node should be omitted from the DOT output.
  bool isNodeHidden(NodeRef Node) { return DTraits.isNodeHidden(Node, G); }

  /// Write the DOT record for \p Node and its outgoing edges.
  ///
  /// \param Node Node to emit.
  void writeNode(NodeRef Node) {
    std::string NodeAttributes = DTraits.getNodeAttributes(Node, G);

    O << "\tNode" << static_cast<const void *>(Node) << " [shape=";
    if (RenderUsingHTML)
      O << "none,";
    else
      O << "record,";

    if (!NodeAttributes.empty()) O << NodeAttributes << ",";
    O << "label=";

    if (RenderUsingHTML) {
      // Count the numbewr of edges out of the node to determine how
      // many columns to span (max 64)
      unsigned ColSpan = 0;
      child_iterator EI = GTraits::child_begin(Node);
      child_iterator EE = GTraits::child_end(Node);
      for (; EI != EE && ColSpan != 64; ++EI, ++ColSpan)
        ;
      if (ColSpan == 0)
        ColSpan = 1;
      // Include truncated messages when counting.
      if (EI != EE)
        ++ColSpan;
      O << "<<table border=\"0\" cellborder=\"1\" cellspacing=\"0\""
        << " cellpadding=\"0\"><tr><td align=\"text\" colspan=\"" << ColSpan
        << "\">";
    } else {
      O << "\"{";
    }

    if (!DTraits.renderGraphFromBottomUp()) {
      if (RenderUsingHTML)
        O << DTraits.getNodeLabel(Node, G) << "</td>";
      else
        O << DOT::EscapeString(DTraits.getNodeLabel(Node, G));

      // If we should include the address of the node in the label, do so now.
      std::string Id = DTraits.getNodeIdentifierLabel(Node, G);
      if (!Id.empty())
        O << "|" << DOT::EscapeString(Id);

      std::string NodeDesc = DTraits.getNodeDescription(Node, G);
      if (!NodeDesc.empty())
        O << "|" << DOT::EscapeString(NodeDesc);
    }

    std::string edgeSourceLabels;
    raw_string_ostream EdgeSourceLabels(edgeSourceLabels);
    bool hasEdgeSourceLabels = getEdgeSourceLabels(EdgeSourceLabels, Node);

    if (hasEdgeSourceLabels) {
      if (!DTraits.renderGraphFromBottomUp())
        if (!RenderUsingHTML)
          O << "|";

      if (RenderUsingHTML)
        O << edgeSourceLabels;
      else
        O << "{" << edgeSourceLabels << "}";

      if (DTraits.renderGraphFromBottomUp())
        if (!RenderUsingHTML)
          O << "|";
    }

    if (DTraits.renderGraphFromBottomUp()) {
      if (RenderUsingHTML)
        O << DTraits.getNodeLabel(Node, G);
      else
        O << DOT::EscapeString(DTraits.getNodeLabel(Node, G));

      // If we should include the address of the node in the label, do so now.
      std::string Id = DTraits.getNodeIdentifierLabel(Node, G);
      if (!Id.empty())
        O << "|" << DOT::EscapeString(Id);

      std::string NodeDesc = DTraits.getNodeDescription(Node, G);
      if (!NodeDesc.empty())
        O << "|" << DOT::EscapeString(NodeDesc);
    }

    if (DTraits.hasEdgeDestLabels()) {
      O << "|{";

      unsigned i = 0, e = DTraits.numEdgeDestLabels(Node);
      for (; i != e && i != 64; ++i) {
        if (i) O << "|";
        O << "<d" << i << ">"
          << DOT::EscapeString(DTraits.getEdgeDestLabel(Node, i));
      }

      if (i != e)
        O << "|<d64>truncated...";
      O << "}";
    }

    if (RenderUsingHTML)
      O << "</tr></table>>";
    else
      O << "}\"";
    O << "];\n"; // Finish printing the "node" line

    // Output all of the edges now
    child_iterator EI = GTraits::child_begin(Node);
    child_iterator EE = GTraits::child_end(Node);
    for (unsigned i = 0; EI != EE && i != 64; ++EI, ++i)
      if (!DTraits.isNodeHidden(*EI, G))
        writeEdge(Node, i, EI);
    for (; EI != EE; ++EI)
      if (!DTraits.isNodeHidden(*EI, G))
        writeEdge(Node, 64, EI);
  }

  /// Write the DOT edge from \p Node along child iterator \p EI.
  ///
  /// \param Node Source node of the edge.
  /// \param edgeidx Source-port index for the edge, or a truncated-port index.
  /// \param EI Child iterator identifying the destination.
  void writeEdge(NodeRef Node, unsigned edgeidx, child_iterator EI) {
    if (NodeRef TargetNode = *EI) {
      int DestPort = -1;
      if (DTraits.edgeTargetsEdgeSource(Node, EI)) {
        child_iterator TargetIt = DTraits.getEdgeTarget(Node, EI);

        // Figure out which edge this targets...
        unsigned Offset =
          (unsigned)std::distance(GTraits::child_begin(TargetNode), TargetIt);
        DestPort = static_cast<int>(Offset);
      }

      if (DTraits.getEdgeSourceLabel(Node, EI).empty())
        edgeidx = -1;

      getDerived().emitEdge(static_cast<const void *>(Node), edgeidx,
                            static_cast<const void *>(TargetNode), DestPort,
                            DTraits.getEdgeAttributes(Node, EI, G));
    }
  }

  /// Output a simple (non-record) node.
  ///
  /// \param ID Opaque pointer identity used in the DOT node name.
  /// \param Attr Optional DOT attribute string for the node.
  /// \param Label Text shown as the node label.
  /// \param NumEdgeSources Number of source ports to emit on the node.
  /// \param EdgeSourceLabels Optional labels for each source port.
  void emitSimpleNode(const void *ID, const std::string &Attr,
                   const std::string &Label, unsigned NumEdgeSources = 0,
                   const std::vector<std::string> *EdgeSourceLabels = nullptr) {
    O << "\tNode" << ID << "[ ";
    if (!Attr.empty())
      O << Attr << ",";
    O << " label =\"";
    if (NumEdgeSources) O << "{";
    O << DOT::EscapeString(Label);
    if (NumEdgeSources) {
      O << "|{";

      for (unsigned i = 0; i != NumEdgeSources; ++i) {
        if (i) O << "|";
        O << "<s" << i << ">";
        if (EdgeSourceLabels) O << DOT::EscapeString((*EdgeSourceLabels)[i]);
      }
      O << "}}";
    }
    O << "\"];\n";
  }

  /// Output an edge from a simple node into the graph.
  ///
  /// \param SrcNodeID Opaque pointer identity of the source node.
  /// \param SrcNodePort Source port index, or a negative value for none.
  /// \param DestNodeID Opaque pointer identity of the destination node.
  /// \param DestNodePort Destination port index, or a negative value for none.
  /// \param Attrs Optional DOT attribute string for the edge.
  void emitEdge(const void *SrcNodeID, int SrcNodePort,
                const void *DestNodeID, int DestNodePort,
                const std::string &Attrs) {
    if (SrcNodePort  > 64) return;             // Eminating from truncated part?
    DestNodePort = std::min(DestNodePort, 64); // Targeting the truncated part?

    O << "\tNode" << SrcNodeID;
    if (SrcNodePort >= 0)
      O << ":s" << SrcNodePort;
    O << " -> Node" << DestNodeID;
    if (DestNodePort >= 0 && DTraits.hasEdgeDestLabels())
      O << ":d" << DestNodePort;

    if (!Attrs.empty())
      O << "[" << Attrs << "]";
    O << ";\n";
  }

  /// Get the raw output stream into the graph file.
  ///
  /// Useful to write fancy things using \c addCustomGraphFeatures().
  ///
  /// \return The raw output stream receiving the DOT text.
  raw_ostream &getOStream() {
    return O;
  }
};

/// Default DOT graph writer for a graph type.
///
/// \tparam GraphType Graph type modeled by \c GraphTraits.
template <typename GraphType>
class GraphWriter : public GraphWriterBase<GraphType, GraphWriter<GraphType>> {
public:
  /// Construct a graph writer for \p g that writes to \p o.
  ///
  /// \param o Stream that receives the DOT output.
  /// \param g Graph to write.
  /// \param SN If true, request short names from the DOT traits.
  GraphWriter(raw_ostream &o, const GraphType &g, bool SN)
      : GraphWriterBase<GraphType, GraphWriter<GraphType>>(o, g, SN) {}
  /// Destroy the graph writer.
  ~GraphWriter() override = default;
};

/// Write graph \p G as DOT text to stream \p O.
///
/// \param O Stream that receives the DOT output.
/// \param G Graph to write.
/// \param ShortNames If true, request short names from the DOT traits.
/// \param Title Optional title used as the graph name and label.
/// \return The output stream \p O.
template <typename GraphType>
raw_ostream &WriteGraph(raw_ostream &O, const GraphType &G,
                        bool ShortNames = false, const Twine &Title = "") {
  // Start the graph emission process...
  GraphWriter<GraphType> W(O, G, ShortNames);

  // Emit the graph.
  W.writeGraph(Title.str());

  return O;
}

/// Create a temporary file name suitable for writing a DOT graph.
///
/// \param Name Twine used as a prefix when generating the file name.
/// \param FD Set to an open file descriptor for the created file, or -1 on
/// failure.
/// \return The generated file path.
LLVM_ABI std::string createGraphFilename(const Twine &Name, int &FD);

/// Write graph \p G into a DOT file.
///
/// If \p Filename is empty, generates a random one.
///
/// \param G Graph to write.
/// \param Name Twine used when generating a temporary file name.
/// \param ShortNames If true, request short names from the DOT traits.
/// \param Title Optional title used as the graph name and label.
/// \param Filename Destination path, or empty to create a temporary file.
/// \return The resulting filename, or an empty string if writing failed.
template <typename GraphType>
std::string WriteGraph(const GraphType &G, const Twine &Name,
                       bool ShortNames = false,
                       const Twine &Title = "",
                       std::string Filename = "") {
  int FD;
  if (Filename.empty()) {
    Filename = createGraphFilename(Name.str(), FD);
  } else {
    std::error_code EC = sys::fs::openFileForWrite(
        Filename, FD, sys::fs::CD_CreateAlways, sys::fs::OF_Text);

    // Writing over an existing file is not considered an error.
    if (EC == std::errc::file_exists) {
      errs() << "file exists, overwriting" << "\n";
    } else if (EC) {
      errs() << "error writing into file" << "\n";
      return "";
    } else {
      errs() << "writing to the newly created file " << Filename << "\n";
    }
  }
  raw_fd_ostream O(FD, /*shouldClose=*/ true);

  if (FD == -1) {
    errs() << "error opening file '" << Filename << "' for writing!\n";
    return "";
  }

  llvm::WriteGraph(O, G, ShortNames, Title);
  errs() << " done. \n";

  return Filename;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
/// Dump a DOT graph to the user-provided file name.
///
/// \param G Graph to write.
/// \param FileName Destination path for the DOT file.
/// \param Title Optional title used as the graph name and label.
/// \param ShortNames If true, request short names from the DOT traits.
/// \param Name Twine used when generating a temporary file name.
template <typename GraphType>
LLVM_DUMP_METHOD void
dumpDotGraphToFile(const GraphType &G, const Twine &FileName,
                   const Twine &Title, bool ShortNames = false,
                   const Twine &Name = "") {
  llvm::WriteGraph(G, Name, ShortNames, Title, FileName.str());
}
#endif

/// Emit a DOT graph and open it with a Graphviz viewer.
///
/// Useful from a debugger: writes the graph, runs the selected program, then
/// cleans up.
///
/// \param G Graph to write and display.
/// \param Name Twine used when generating a temporary file name.
/// \param ShortNames If true, request short names from the DOT traits.
/// \param Title Optional title used as the graph name and label.
/// \param Program Graphviz program used to render the graph.
template<typename GraphType>
void ViewGraph(const GraphType &G, const Twine &Name,
               bool ShortNames = false, const Twine &Title = "",
               GraphProgram::Name Program = GraphProgram::DOT) {
  std::string Filename = llvm::WriteGraph(G, Name, ShortNames, Title);

  if (Filename.empty())
    return;

  DisplayGraph(Filename, false, Program);
}

} // end namespace llvm

#endif // LLVM_SUPPORT_GRAPHWRITER_H
