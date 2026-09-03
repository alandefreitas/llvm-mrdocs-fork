//===----- PerfSupportPlugin.h ----- Utils for perf support -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Handles support for registering code with perf
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_PERFSUPPORTPLUGIN_H
#define LLVM_EXECUTIONENGINE_ORC_PERFSUPPORTPLUGIN_H

#include "llvm/ExecutionEngine/Orc/Shared/PerfSharedStructs.h"
#include "llvm/Support/Compiler.h"

#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/ObjectLinkingLayer.h"

namespace llvm {
namespace orc {

/// ObjectLinkingLayer plugin that logs perf jitdump events for each object.
///
/// See
/// https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/tools/perf/Documentation/jitdump-specification.txt.
/// Currently has support for dumping code load records and unwind info records.
class LLVM_ABI PerfSupportPlugin : public ObjectLinkingLayer::Plugin {
public:
  /// Construct a plugin that logs perf jitdump events via the given executor
  /// addresses.
  /// @param EPC Executor process control used to run registration calls.
  /// @param RegisterPerfStartAddr Executor address of the perf start action.
  /// @param RegisterPerfEndAddr Executor address of the perf end action.
  /// @param RegisterPerfImplAddr Executor address of the perf impl action.
  /// @param EmitDebugInfo If true, emit debug info records in the jitdump.
  /// @param EmitUnwindInfo If true, emit unwind info records in the jitdump.
  PerfSupportPlugin(ExecutorProcessControl &EPC,
                    ExecutorAddr RegisterPerfStartAddr,
                    ExecutorAddr RegisterPerfEndAddr,
                    ExecutorAddr RegisterPerfImplAddr, bool EmitDebugInfo,
                    bool EmitUnwindInfo);
  /// Destroy the plugin.
  ~PerfSupportPlugin() override;

  /// Install JITLink passes that emit perf jitdump records for the graph.
  /// @param MR Materialization responsibility for the graph being linked.
  /// @param G Link graph whose pass configuration may be modified.
  /// @param Config Pass configuration to update.
  void modifyPassConfig(MaterializationResponsibility &MR,
                        jitlink::LinkGraph &G,
                        jitlink::PassConfiguration &Config) override;

  /// No-op failure notification.
  /// @param MR Materialization responsibility for the failed graph.
  /// @return Success always.
  Error notifyFailed(MaterializationResponsibility &MR) override {
    return Error::success();
  }

  /// No-op resource removal notification.
  /// @param JD JITDylib whose resources are being removed.
  /// @param K Key identifying the resources to remove.
  /// @return Success always.
  Error notifyRemovingResources(JITDylib &JD, ResourceKey K) override {
    return Error::success();
  }

  /// No-op resource transfer notification.
  /// @param JD JITDylib whose resources are being transferred.
  /// @param DstKey Destination resource key.
  /// @param SrcKey Source resource key.
  void notifyTransferringResources(JITDylib &JD, ResourceKey DstKey,
                                   ResourceKey SrcKey) override {}

  /// Create a plugin by looking up perf registration actions in JD.
  /// @param EPC Executor process control used to run registration calls.
  /// @param JD JITDylib holding the perf registration bootstrap symbols.
  /// @param EmitDebugInfo If true, emit debug info records in the jitdump.
  /// @param EmitUnwindInfo If true, emit unwind info records in the jitdump.
  /// @return A new plugin, or an error if a registration symbol cannot be
  ///         looked up.
  static Expected<std::unique_ptr<PerfSupportPlugin>>
  Create(ExecutorProcessControl &EPC, JITDylib &JD, bool EmitDebugInfo,
         bool EmitUnwindInfo);

private:
  ExecutorProcessControl &EPC;
  ExecutorAddr RegisterPerfStartAddr;
  ExecutorAddr RegisterPerfEndAddr;
  ExecutorAddr RegisterPerfImplAddr;
  std::atomic<uint64_t> CodeIndex;
  bool EmitDebugInfo;
  bool EmitUnwindInfo;
};

} // namespace orc
} // namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_PERFSUPPORTPLUGIN_H
