//===----- EHFrameRegistrationPlugin.h - Register eh-frames -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Register eh-frame sections with a registrar.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_EHFRAMEREGISTRATIONPLUGIN_H
#define LLVM_EXECUTIONENGINE_ORC_EHFRAMEREGISTRATIONPLUGIN_H

#include "llvm/ExecutionEngine/Orc/LinkGraphLinkingLayer.h"
#include "llvm/Support/Compiler.h"

#include <memory>
#include <mutex>

namespace llvm::orc {

/// Adds AllocationActions to register and deregister eh-frame sections in the
/// absence of native Platform support.
class LLVM_ABI EHFrameRegistrationPlugin
    : public LinkGraphLinkingLayer::Plugin {
public:
  /// Create an EHFrameRegistrationPlugin using bootstrap registration symbols.
  /// @param ES Execution session used to look up eh-frame registration
  ///        symbols in the executor bootstrap map.
  /// @return An EHFrameRegistrationPlugin, or an error if bootstrap symbols
  ///         cannot be looked up.
  static Expected<std::unique_ptr<EHFrameRegistrationPlugin>>
  Create(ExecutionSession &ES);

  /// Construct a plugin that registers and deregisters eh-frame sections.
  /// @param RegisterEHFrame Executor address of the eh-frame registration
  ///        allocation action.
  /// @param DeregisterEHFrame Executor address of the eh-frame deregistration
  ///        allocation action.
  EHFrameRegistrationPlugin(ExecutorAddr RegisterEHFrame,
                            ExecutorAddr DeregisterEHFrame)
      : RegisterEHFrame(RegisterEHFrame), DeregisterEHFrame(DeregisterEHFrame) {
  }

  /// Install passes that register eh-frame sections via allocation actions.
  /// @param MR Materialization responsibility for the graph being linked.
  /// @param G Link graph whose pass configuration may be modified.
  /// @param PassConfig Pass configuration to update.
  void modifyPassConfig(MaterializationResponsibility &MR,
                        jitlink::LinkGraph &G,
                        jitlink::PassConfiguration &PassConfig) override;
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
  ExecutorAddr RegisterEHFrame;
  ExecutorAddr DeregisterEHFrame;
};

} // namespace llvm::orc

#endif // LLVM_EXECUTIONENGINE_ORC_EHFRAMEREGISTRATIONPLUGIN_H
