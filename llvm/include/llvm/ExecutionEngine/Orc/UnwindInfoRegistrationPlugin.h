//===- UnwindInfoRegistrationPlugin.h -- libunwind registration -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Register eh-frame and compact-unwind sections with libunwind
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_UNWINDINFOREGISTRATIONPLUGIN_H
#define LLVM_EXECUTIONENGINE_ORC_UNWINDINFOREGISTRATIONPLUGIN_H

#include "llvm/ExecutionEngine/Orc/LinkGraphLinkingLayer.h"
#include "llvm/ExecutionEngine/Orc/Shared/OrcRTBridge.h"
#include "llvm/Support/Compiler.h"

namespace llvm::orc {

/// Adds AllocationActions to register and deregister unwind-info sections with
/// libunwind.
class LLVM_ABI UnwindInfoRegistrationPlugin
    : public LinkGraphLinkingLayer::Plugin {
public:
  /// Construct a plugin that registers and deregisters unwind-info sections.
  /// @param ES Execution session used to intern the DSO base symbol name.
  /// @param RegisterSections Executor address of the unwind-info registration
  ///        allocation action.
  /// @param DeregisterSections Executor address of the unwind-info
  ///        deregistration allocation action.
  UnwindInfoRegistrationPlugin(ExecutionSession &ES,
                               ExecutorAddr RegisterSections,
                               ExecutorAddr DeregisterSections)
      : RegisterSections(RegisterSections),
        DeregisterSections(DeregisterSections) {
    DSOBaseName = ES.intern("__jitlink$libunwind_dso_base");
  }

  /// Create an UnwindInfoRegistrationPlugin using bootstrap registration
  /// symbols.
  /// @param ES Execution session used to look up unwind-info registration
  ///        symbols in the executor bootstrap map.
  /// @param SNs Bootstrap symbol names for the MachO unwind-info registrar.
  /// @return An UnwindInfoRegistrationPlugin, or an error if bootstrap symbols
  ///         cannot be looked up.
  static Expected<std::shared_ptr<UnwindInfoRegistrationPlugin>>
  Create(ExecutionSession &ES,
         rt::MachOUnwindInfoRegistrarSymbolNames SNs =
             rt::orc_rt_MachOUnwindInfoRegistrarSPSSymbols);

  /// Install passes that register unwind-info sections via allocation actions.
  /// @param MR Materialization responsibility for the graph being linked.
  /// @param G Link graph whose pass configuration may be modified.
  /// @param PassConfig Pass configuration to update.
  void modifyPassConfig(MaterializationResponsibility &MR,
                        jitlink::LinkGraph &G,
                        jitlink::PassConfiguration &PassConfig) override;

  /// No-op successful-emission notification.
  /// @param MR Materialization responsibility for the emitted graph.
  /// @return Success; this plugin has no emission-side effects.
  Error notifyEmitted(MaterializationResponsibility &MR) override {
    return Error::success();
  }

  /// No-op failure notification.
  /// @param MR Materialization responsibility for the failed graph.
  /// @return Success; this plugin has no failure-side effects.
  Error notifyFailed(MaterializationResponsibility &MR) override {
    return Error::success();
  }

  /// No-op resource removal notification.
  /// @param JD JITDylib whose resources are being removed.
  /// @param K Key identifying the resources to remove.
  /// @return Success; this plugin does not track resources to remove.
  Error notifyRemovingResources(JITDylib &JD, ResourceKey K) override {
    return Error::success();
  }

  /// No-op resource transfer notification.
  /// @param JD JITDylib whose resources are being transferred.
  /// @param DstKey Destination resource key.
  /// @param SrcKey Source resource key.
  void notifyTransferringResources(JITDylib &JD, ResourceKey DstKey,
                                   ResourceKey SrcKey) override {}

private:
  Error addUnwindInfoRegistrationActions(jitlink::LinkGraph &G);

  SymbolStringPtr DSOBaseName;
  ExecutorAddr RegisterSections, DeregisterSections;
};

} // namespace llvm::orc

#endif // LLVM_EXECUTIONENGINE_ORC_UNWINDINFOREGISTRATIONPLUGIN_H
