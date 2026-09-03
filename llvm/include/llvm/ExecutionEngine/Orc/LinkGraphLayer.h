//===- LinkGraphLayer.h - Add LinkGraphs to an ExecutionSession -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// LinkGraphLayer and associated utilities.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_LINKGRAPHLAYER_H
#define LLVM_EXECUTIONENGINE_ORC_LINKGRAPHLAYER_H

#include "llvm/ExecutionEngine/JITLink/JITLink.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"

#include <atomic>
#include <memory>

namespace llvm::orc {

/// Interface for layers that accept JITLink LinkGraphs.
class LLVM_ABI LinkGraphLayer {
public:
  /// Construct a LinkGraphLayer for the given execution session.
  /// @param ES Execution session this layer belongs to.
  LinkGraphLayer(ExecutionSession &ES) : ES(ES) {}

  /// Destroy this LinkGraphLayer.
  virtual ~LinkGraphLayer();

  /// Returns the ExecutionSession for this layer.
  /// @return ExecutionSession for this layer.
  ExecutionSession &getExecutionSession() { return ES; }

  /// Adds a LinkGraph to the JITDylib for the given ResourceTracker.
  /// @param RT Resource tracker for the target JITDylib.
  /// @param G LinkGraph to add.
  /// @param I Interface describing the symbols provided by the graph.
  /// @return Success, or an error if the graph cannot be added.
  virtual Error add(ResourceTrackerSP RT, std::unique_ptr<jitlink::LinkGraph> G,
                    MaterializationUnit::Interface I);

  /// Adds a LinkGraph to the JITDylib for the given ResourceTracker. The
  /// interface for the graph will be built using getLinkGraphInterface.
  /// @param RT Resource tracker for the target JITDylib.
  /// @param G LinkGraph to add.
  /// @return Success, or an error if the graph cannot be added.
  Error add(ResourceTrackerSP RT, std::unique_ptr<jitlink::LinkGraph> G) {
    auto LGI = getInterface(*G);
    return add(std::move(RT), std::move(G), std::move(LGI));
  }

  /// Adds a LinkGraph to the given JITDylib.
  /// @param JD JITDylib to add the graph to.
  /// @param G LinkGraph to add.
  /// @param I Interface describing the symbols provided by the graph.
  /// @return Success, or an error if the graph cannot be added.
  Error add(JITDylib &JD, std::unique_ptr<jitlink::LinkGraph> G,
            MaterializationUnit::Interface I) {
    return add(JD.getDefaultResourceTracker(), std::move(G), std::move(I));
  }

  /// Adds a LinkGraph to the given JITDylib. The interface for the object will
  /// be built using getLinkGraphInterface.
  /// @param JD JITDylib to add the graph to.
  /// @param G LinkGraph to add.
  /// @return Success, or an error if the graph cannot be added.
  Error add(JITDylib &JD, std::unique_ptr<jitlink::LinkGraph> G) {
    return add(JD.getDefaultResourceTracker(), std::move(G));
  }

  /// Emit should materialize the given LinkGraph.
  /// @param R Materialization responsibility for the symbols being emitted.
  /// @param G LinkGraph to materialize.
  virtual void emit(std::unique_ptr<MaterializationResponsibility> R,
                    std::unique_ptr<jitlink::LinkGraph> G) = 0;

  /// Get the interface for the given LinkGraph.
  /// @param G LinkGraph to build an interface for.
  /// @return Interface describing the symbols provided by \p G.
  MaterializationUnit::Interface getInterface(jitlink::LinkGraph &G);

  /// Get the JITSymbolFlags for the given symbol.
  /// @param Sym JITLink symbol to derive flags from.
  /// @return JITSymbolFlags derived from \p Sym.
  static JITSymbolFlags getJITSymbolFlagsForSymbol(jitlink::Symbol &Sym);

private:
  ExecutionSession &ES;
  std::atomic<uint64_t> Counter{0};
};

/// MaterializationUnit for wrapping LinkGraphs.
class LLVM_ABI LinkGraphMaterializationUnit : public MaterializationUnit {
public:
  /// Create a LinkGraphMaterializationUnit from a graph and pre-built
  /// interface.
  /// @param LGLayer Layer whose emit method will materialize the graph.
  /// @param G LinkGraph to materialize.
  /// @param I Interface describing the symbols provided by the graph.
  LinkGraphMaterializationUnit(LinkGraphLayer &LGLayer,
                               std::unique_ptr<jitlink::LinkGraph> G,
                               Interface I)
      : MaterializationUnit(I), LGLayer(LGLayer), G(std::move(G)) {}

  /// Create a LinkGraphMaterializationUnit, building the interface from the
  /// graph.
  /// @param LGLayer Layer whose emit method will materialize the graph.
  /// @param G LinkGraph to materialize.
  LinkGraphMaterializationUnit(LinkGraphLayer &LGLayer,
                               std::unique_ptr<jitlink::LinkGraph> G)
      : MaterializationUnit(LGLayer.getInterface(*G)), LGLayer(LGLayer),
        G(std::move(G)) {}

  /// Return the name of the wrapped LinkGraph.
  /// @return Name of the wrapped LinkGraph.
  StringRef getName() const override;

  /// Materialize the wrapped LinkGraph by calling emit on the layer.
  /// @param MR Materialization responsibility for the symbols being emitted.
  void materialize(std::unique_ptr<MaterializationResponsibility> MR) override {
    LGLayer.emit(std::move(MR), std::move(G));
  }

private:
  void discard(const JITDylib &JD, const SymbolStringPtr &Name) override;

  LinkGraphLayer &LGLayer;
  std::unique_ptr<jitlink::LinkGraph> G;
};

inline Error LinkGraphLayer::add(ResourceTrackerSP RT,
                                 std::unique_ptr<jitlink::LinkGraph> G,
                                 MaterializationUnit::Interface I) {
  auto &JD = RT->getJITDylib();

  return JD.define(std::make_unique<LinkGraphMaterializationUnit>(
                       *this, std::move(G), std::move(I)),
                   std::move(RT));
}

} // end namespace llvm::orc

#endif // LLVM_EXECUTIONENGINE_ORC_LINKGRAPHLAYER_H
