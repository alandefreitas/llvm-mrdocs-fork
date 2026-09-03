//===--- VTuneSupportPlugin.h -- Support for VTune profiler ---*- C++ -*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Handles support for registering code with VIntel Tune's Amplifier JIT API.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_DEBUGGING_VTUNESUPPORT_H
#define LLVM_EXECUTIONENGINE_ORC_DEBUGGING_VTUNESUPPORT_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/ObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/Orc/Shared/SimplePackedSerialization.h"
#include "llvm/ExecutionEngine/Orc/Shared/VTuneSharedStructs.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

namespace orc {

/// ObjectLinkingLayer plugin that registers JIT code with Intel VTune.
class LLVM_ABI VTuneSupportPlugin : public ObjectLinkingLayer::Plugin {
public:
  /// Construct a plugin that registers methods via the given executor
  /// addresses.
  /// @param EPC Executor process control used to run registration calls.
  /// @param RegisterImplAddr Executor address of the VTune register action.
  /// @param UnregisterImplAddr Executor address of the VTune unregister action.
  /// @param EmitDebugInfo If true, include source line info in registrations.
  VTuneSupportPlugin(ExecutorProcessControl &EPC, ExecutorAddr RegisterImplAddr,
                     ExecutorAddr UnregisterImplAddr, bool EmitDebugInfo)
      : EPC(EPC), RegisterVTuneImplAddr(RegisterImplAddr),
        UnregisterVTuneImplAddr(UnregisterImplAddr),
        EmitDebugInfo(EmitDebugInfo) {}

  /// Install JITLink passes that register callable symbols with VTune.
  /// @param MR Materialization responsibility for the graph being linked.
  /// @param G Link graph whose pass configuration may be modified.
  /// @param Config Pass configuration to update.
  void modifyPassConfig(MaterializationResponsibility &MR,
                        jitlink::LinkGraph &G,
                        jitlink::PassConfiguration &Config) override;

  /// Record pending method IDs under the emitted resource key.
  /// @param MR Materialization responsibility for the emitted graph.
  /// @return Success, or an error if the resource key cannot be obtained.
  Error notifyEmitted(MaterializationResponsibility &MR) override;
  /// Drop pending method IDs after materialization fails.
  /// @param MR Materialization responsibility for the failed graph.
  /// @return Success always.
  Error notifyFailed(MaterializationResponsibility &MR) override;
  /// Unregister VTune method IDs associated with the removed resources.
  /// @param JD JITDylib whose resources are being removed.
  /// @param K Key identifying the resources to remove.
  /// @return Success, or an error if unregistration fails.
  Error notifyRemovingResources(JITDylib &JD, ResourceKey K) override;
  /// Move loaded method IDs from one resource key to another.
  /// @param JD JITDylib whose resources are being transferred.
  /// @param DstKey Destination resource key.
  /// @param SrcKey Source resource key.
  void notifyTransferringResources(JITDylib &JD, ResourceKey DstKey,
                                   ResourceKey SrcKey) override;

  /// Create a plugin by looking up VTune registration actions in JD.
  /// @param EPC Executor process control used to run registration calls.
  /// @param JD JITDylib holding the VTune registration bootstrap symbols.
  /// @param EmitDebugInfo If true, include source line info in registrations.
  /// @param TestMode If true, look up the test registration symbol instead.
  /// @return A new plugin, or an error if a registration symbol cannot be
  ///         looked up.
  static Expected<std::unique_ptr<VTuneSupportPlugin>>
  Create(ExecutorProcessControl &EPC, JITDylib &JD, bool EmitDebugInfo,
         bool TestMode = false);

private:
  ExecutorProcessControl &EPC;
  ExecutorAddr RegisterVTuneImplAddr;
  ExecutorAddr UnregisterVTuneImplAddr;
  std::mutex PluginMutex;
  uint64_t NextMethodID = 0;
  DenseMap<MaterializationResponsibility *, std::pair<uint64_t, uint64_t>>
      PendingMethodIDs;
  DenseMap<ResourceKey, SmallVector<std::pair<uint64_t, uint64_t>>>
      LoadedMethodIDs;
  bool EmitDebugInfo;
};

} // end namespace orc

} // end namespace llvm

#endif
