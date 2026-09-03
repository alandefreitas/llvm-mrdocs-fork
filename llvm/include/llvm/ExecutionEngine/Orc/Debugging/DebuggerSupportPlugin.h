//===--- DebugerSupportPlugin.h -- Utils for debugger support ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generates debug objects and registers them using the jit-loader-gdb protocol.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_DEBUGGERSUPPORTPLUGIN_H
#define LLVM_EXECUTIONENGINE_ORC_DEBUGGERSUPPORTPLUGIN_H

#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/ObjectLinkingLayer.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
namespace orc {

/// For each object containing debug info, installs JITLink passes to synthesize
/// a debug object and then register it via the GDB JIT-registration interface.
///
/// Currently MachO only. For ELF use ELFDebugObjectPlugin. These two
/// plugins will be merged in the near future.
class LLVM_ABI GDBJITDebugInfoRegistrationPlugin
    : public ObjectLinkingLayer::Plugin {
public:
  /// Interface for synthesizing a debug object and registering it with GDB.
  class DebugSectionSynthesizer {
  public:
    /// Destroy the synthesizer.
    virtual ~DebugSectionSynthesizer() = default;
    /// Begin building the debug object for the current link graph.
    /// @return Success, or an error if synthesis cannot be started.
    virtual Error startSynthesis() = 0;
    /// Finish the debug object and register it via the GDB JIT interface.
    /// @return Success, or an error if registration fails.
    virtual Error completeSynthesisAndRegister() = 0;
  };

  /// Create a plugin by looking up the GDB registration action in BootstrapJD.
  /// @param ES Execution session used to look up the registration symbol.
  /// @param BootstrapJD JITDylib holding the GDB registration bootstrap symbol.
  /// @return A new plugin, or an error if the registration symbol cannot be
  ///         resolved.
  static Expected<std::unique_ptr<GDBJITDebugInfoRegistrationPlugin>>
  Create(ExecutionSession &ES, JITDylib &BootstrapJD);

  /// Construct a plugin that registers debug objects via the given action.
  /// @param RegisterActionAddr Executor address of the GDB JIT registration
  ///        allocation action.
  GDBJITDebugInfoRegistrationPlugin(ExecutorAddr RegisterActionAddr)
      : RegisterActionAddr(RegisterActionAddr) {}

  /// No-op failure notification.
  /// @param MR Materialization responsibility for the failed graph.
  /// @return Success always.
  Error notifyFailed(MaterializationResponsibility &MR) override;
  /// No-op resource removal notification.
  /// @param JD JITDylib whose resources are being removed.
  /// @param K Key identifying the resources to remove.
  /// @return Success always.
  Error notifyRemovingResources(JITDylib &JD, ResourceKey K) override;

  /// No-op resource transfer notification.
  /// @param JD JITDylib whose resources are being transferred.
  /// @param DstKey Destination resource key.
  /// @param SrcKey Source resource key.
  void notifyTransferringResources(JITDylib &JD, ResourceKey DstKey,
                                   ResourceKey SrcKey) override;

  /// Install JITLink passes that synthesize and register a debug object.
  /// @param MR Materialization responsibility for the graph being linked.
  /// @param LG Link graph whose pass configuration may be modified.
  /// @param PassConfig Pass configuration to update.
  void modifyPassConfig(MaterializationResponsibility &MR,
                        jitlink::LinkGraph &LG,
                        jitlink::PassConfiguration &PassConfig) override;

private:
  void modifyPassConfigForMachO(MaterializationResponsibility &MR,
                                jitlink::LinkGraph &LG,
                                jitlink::PassConfiguration &PassConfig);

  ExecutorAddr RegisterActionAddr;
};

} // namespace orc
} // namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_DEBUGGERSUPPORTPLUGIN_H
