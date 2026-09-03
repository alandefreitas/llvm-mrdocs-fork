//===------ ELFDebugObjectPlugin.h - JITLink debug objects ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// ObjectLinkingLayer plugin for emitting debug objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_ELFDEBUGOBJECTPLUGIN_H
#define LLVM_EXECUTIONENGINE_ORC_ELFDEBUGOBJECTPLUGIN_H

#include "llvm/ExecutionEngine/JITLink/JITLink.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/ObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Memory.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/TargetParser/Triple.h"

#include <map>
#include <memory>
#include <mutex>

namespace llvm {
namespace orc {

/// In-memory ELF debug object managed during JIT linking.
class DebugObject;

/// Debugger support for ELF platforms with the GDB JIT Interface.
///
/// The plugin emits and manages a separate debug object allocation in addition
/// to the LinkGraph's own allocation and it notifies the debugger when
/// necessary.
class LLVM_ABI ELFDebugObjectPlugin : public ObjectLinkingLayer::Plugin {
public:
  /// Create the plugin for the given session and set additional options.
  ///
  /// @param ES Execution session used for bootstrap symbols and errors.
  /// @param RequireDebugSections If true, emit debug objects only when the
  /// LinkGraph contains debug info. Turning this off allows minimal debugging
  /// based on raw symbol names, but it comes with significant overhead for
  /// release configurations.
  /// @param Err Set on failure to resolve the GDB JIT registration bootstrap
  /// symbol.
  ELFDebugObjectPlugin(ExecutionSession &ES, bool RequireDebugSections,
                       Error &Err);
  /// Destroy the plugin.
  ~ELFDebugObjectPlugin() override;

  /// Begin preparing a debug object when an ELF LinkGraph is materialized.
  ///
  /// Copies the input object into a pending debug-object allocation when the
  /// graph is ELF and the buffer is non-empty.
  /// @param MR Materialization responsibility for the graph.
  /// @param G Link graph being materialized.
  /// @param Ctx JITLink context for the link.
  /// @param InputObj Buffer holding the input object, if any.
  void notifyMaterializing(MaterializationResponsibility &MR,
                           jitlink::LinkGraph &G, jitlink::JITLinkContext &Ctx,
                           MemoryBufferRef InputObj) override;

  /// Release pending debug resources after materialization fails.
  /// @param MR Materialization responsibility for the failed graph.
  /// @return Success always.
  Error notifyFailed(MaterializationResponsibility &MR) override;
  /// Drop registered debug objects for resources being removed.
  /// @param JD JITDylib whose resources are being removed.
  /// @param K Key identifying the resources to remove.
  /// @return Success always.
  Error notifyRemovingResources(JITDylib &JD, ResourceKey K) override;

  /// Move registered debug objects from one resource key to another.
  /// @param JD JITDylib whose resources are being transferred.
  /// @param DstKey Destination resource key.
  /// @param SrcKey Source resource key.
  void notifyTransferringResources(JITDylib &JD, ResourceKey DstKey,
                                   ResourceKey SrcKey) override;

  /// Install JITLink passes that patch, finalize, and register the debug object.
  /// @param MR Materialization responsibility for the graph being linked.
  /// @param LG Link graph whose pass configuration may be modified.
  /// @param PassConfig Pass configuration to update.
  void modifyPassConfig(MaterializationResponsibility &MR,
                        jitlink::LinkGraph &LG,
                        jitlink::PassConfiguration &PassConfig) override;

private:
  ExecutionSession &ES;

  using OwnedDebugObject = std::unique_ptr<DebugObject>;
  std::map<MaterializationResponsibility *, OwnedDebugObject> PendingObjs;
  std::map<ResourceKey, std::vector<OwnedDebugObject>> RegisteredObjs;

  std::mutex PendingObjsLock;
  std::mutex RegisteredObjsLock;

  ExecutorAddr RegistrationAction;
  bool RequireDebugSections;

  DebugObject *getPendingDebugObj(MaterializationResponsibility &MR);
};

} // namespace orc
} // namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_ELFDEBUGOBJECTPLUGIN_H
