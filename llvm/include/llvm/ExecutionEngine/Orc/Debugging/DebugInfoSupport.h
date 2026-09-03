//===--- DebugInfoSupport.h ---- Utils for debug info support ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Utilities to preserve and parse debug info from LinkGraphs.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_DEBUGINFOSUPPORT_H
#define LLVM_EXECUTIONENGINE_ORC_DEBUGINFOSUPPORT_H

#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/ObjectLinkingLayer.h"
#include "llvm/Support/Compiler.h"

#include "llvm/DebugInfo/DWARF/DWARFContext.h"

namespace llvm {

namespace orc {

/// Preserve DWARF debug sections in \p G so they are not pruned by JITLink.
///
/// Only ELF LinkGraphs are supported.
/// @param G Link graph whose DWARF sections should be preserved.
/// @return Success, or an error if preservation fails.
LLVM_ABI Error preserveDebugSections(jitlink::LinkGraph &G);

/// Build a DWARFContext from the DWARF sections in \p G.
///
/// The backing string map is also returned, for memory lifetime management.
/// Only ELF LinkGraphs are supported.
/// @param G Link graph to extract DWARF section data from.
/// @return A DWARFContext and backing buffers, or an error on failure.
LLVM_ABI Expected<std::pair<std::unique_ptr<DWARFContext>,
                            StringMap<std::unique_ptr<MemoryBuffer>>>>
createDWARFContext(jitlink::LinkGraph &G);

/// ObjectLinkingLayer plugin that preserves DWARF debug sections during linking.
///
/// Thin wrapper around preserveDebugSections to be used as a standalone plugin.
class DebugInfoPreservationPlugin : public ObjectLinkingLayer::Plugin {
public:
  /// Install a pre-prune pass that preserves DWARF debug sections.
  /// @param MR Materialization responsibility for the graph being linked.
  /// @param LG Link graph whose pass configuration may be modified.
  /// @param PassConfig Pass configuration to update.
  void modifyPassConfig(MaterializationResponsibility &MR,
                        jitlink::LinkGraph &LG,
                        jitlink::PassConfiguration &PassConfig) override {
    PassConfig.PrePrunePasses.push_back(preserveDebugSections);
  }

  /// No-op resource removal notification.
  /// @param JD JITDylib whose resources are being removed.
  /// @param K Key identifying the resources to remove.
  /// @return Success always.
  Error notifyRemovingResources(JITDylib &JD, ResourceKey K) override {
    // Do nothing.
    return Error::success();
  }

  /// No-op failure notification.
  /// @param MR Materialization responsibility for the failed graph.
  /// @return Success always.
  Error notifyFailed(MaterializationResponsibility &MR) override {
    // Do nothing.
    return Error::success();
  }

  /// No-op resource transfer notification.
  /// @param JD JITDylib whose resources are being transferred.
  /// @param DstKey Destination resource key.
  /// @param SrcKey Source resource key.
  void notifyTransferringResources(JITDylib &JD, ResourceKey DstKey,
                                   ResourceKey SrcKey) override {
    // Do nothing.
  }

  /// Create a DebugInfoPreservationPlugin instance.
  /// @return A new DebugInfoPreservationPlugin, or an error if creation fails.
  static Expected<std::unique_ptr<DebugInfoPreservationPlugin>> Create() {
    return std::make_unique<DebugInfoPreservationPlugin>();
  }
};

} // namespace orc

} // namespace llvm

#endif
