//===- RTDyldObjectLinkingLayer.h - RTDyld-based jit linking  ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Contains the definition for an RTDyld-based, in-process object linking layer.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_RTDYLDOBJECTLINKINGLAYER_H
#define LLVM_EXECUTIONENGINE_ORC_RTDYLDOBJECTLINKINGLAYER_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/JITEventListener.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/Layer.h"
#include "llvm/ExecutionEngine/RuntimeDyld.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <algorithm>
#include <cassert>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace llvm {
namespace orc {

/// An ObjectLayer implementation built on RuntimeDyld.
///
/// Clients can use this class to add relocatable object files to an
/// ExecutionSession. It typically serves as the base layer (underneath a
/// compiling layer like IRCompileLayer) for in-process JIT linking via
/// RuntimeDyld.
class LLVM_ABI RTDyldObjectLinkingLayer
    : public RTTIExtends<RTDyldObjectLinkingLayer, ObjectLayer>,
      private ResourceManager {
public:
  /// RTTI identifier for this RTDyldObjectLinkingLayer type.
  static char ID;

  /// Functor for receiving object-loaded notifications.
  using NotifyLoadedFunction = std::function<void(
      MaterializationResponsibility &R, const object::ObjectFile &Obj,
      const RuntimeDyld::LoadedObjectInfo &)>;

  /// Functor for receiving finalization notifications.
  using NotifyEmittedFunction = std::function<void(
      MaterializationResponsibility &R, std::unique_ptr<MemoryBuffer>)>;

  /// Functor that supplies a RuntimeDyld memory manager for an object buffer.
  using GetMemoryManagerFunction =
      unique_function<std::unique_ptr<RuntimeDyld::MemoryManager>(
          const MemoryBuffer &)>;

  /// Construct an RTDyldObjectLinkingLayer.
  /// \param ES Execution session this layer is attached to.
  /// \param GetMemoryManager Functor that creates a memory manager for each
  ///        object buffer.
  RTDyldObjectLinkingLayer(ExecutionSession &ES,
                           GetMemoryManagerFunction GetMemoryManager);

  /// Destroy this RTDyldObjectLinkingLayer.
  ~RTDyldObjectLinkingLayer() override;

  /// Emit the object.
  /// \param R Materialization responsibility for the symbols being emitted.
  /// \param O Memory buffer containing the object file to materialize.
  void emit(std::unique_ptr<MaterializationResponsibility> R,
            std::unique_ptr<MemoryBuffer> O) override;

  /// Set the NotifyLoaded callback.
  /// \param NotifyLoaded Callback invoked after an object has been loaded.
  /// \return Reference to this layer.
  RTDyldObjectLinkingLayer &setNotifyLoaded(NotifyLoadedFunction NotifyLoaded) {
    this->NotifyLoaded = std::move(NotifyLoaded);
    return *this;
  }

  /// Set the NotifyEmitted callback.
  /// \param NotifyEmitted Callback invoked after an object has been finalized.
  /// \return Reference to this layer.
  RTDyldObjectLinkingLayer &
  setNotifyEmitted(NotifyEmittedFunction NotifyEmitted) {
    this->NotifyEmitted = std::move(NotifyEmitted);
    return *this;
  }

  /// Set the 'ProcessAllSections' flag.
  ///
  /// If set to true, all sections in each object file will be allocated using
  /// the memory manager, rather than just the sections required for execution.
  ///
  /// This is kludgy, and may be removed in the future.
  /// \param ProcessAllSections Whether to allocate all object sections.
  /// \return Reference to this layer.
  RTDyldObjectLinkingLayer &setProcessAllSections(bool ProcessAllSections) {
    this->ProcessAllSections = ProcessAllSections;
    return *this;
  }

  /// Override RuntimeDyld symbol flags with responsibility flags.
  ///
  /// Instructs this RTDyldLinkingLayer2 instance to override the symbol flags
  /// returned by RuntimeDyld for any given object file with the flags supplied
  /// by the MaterializationResponsibility instance. This is a workaround to
  /// support symbol visibility in COFF, which does not use the libObject's
  /// SF_Exported flag. Use only when generating / adding COFF object files.
  ///
  /// FIXME: We should be able to remove this if/when COFF properly tracks
  /// exported symbols.
  /// \param OverrideObjectFlags Whether to replace RuntimeDyld flags with
  ///        MaterializationResponsibility flags.
  /// \return Reference to this layer.
  RTDyldObjectLinkingLayer &
  setOverrideObjectFlagsWithResponsibilityFlags(bool OverrideObjectFlags) {
    this->OverrideObjectFlags = OverrideObjectFlags;
    return *this;
  }

  /// Automatically claim responsibility for extra object symbols.
  ///
  /// If set, this RTDyldObjectLinkingLayer instance will claim responsibility
  /// for any symbols provided by a given object file that were not already in
  /// the MaterializationResponsibility instance. Setting this flag allows
  /// higher-level program representations (e.g. LLVM IR) to be added based on
  /// only a subset of the symbols they provide, without having to write
  /// intervening layers to scan and add the additional symbols. This trades
  /// diagnostic quality for convenience however: If all symbols are enumerated
  /// up-front then clashes can be detected and reported early (and usually
  /// deterministically). If this option is set, clashes for the additional
  /// symbols may not be detected until late, and detection may depend on
  /// the flow of control through JIT'd code. Use with care.
  /// \param AutoClaimObjectSymbols Whether to claim unlisted object symbols.
  /// \return Reference to this layer.
  RTDyldObjectLinkingLayer &
  setAutoClaimResponsibilityForObjectSymbols(bool AutoClaimObjectSymbols) {
    this->AutoClaimObjectSymbols = AutoClaimObjectSymbols;
    return *this;
  }

  /// Register a JITEventListener.
  /// \param L Listener to register for JIT events from this layer.
  void registerJITEventListener(JITEventListener &L);

  /// Unregister a JITEventListener.
  /// \param L Listener to unregister from this layer.
  void unregisterJITEventListener(JITEventListener &L);

private:
  using MemoryManagerUP = std::unique_ptr<RuntimeDyld::MemoryManager>;

  Error onObjLoad(MaterializationResponsibility &R,
                  const object::ObjectFile &Obj,
                  RuntimeDyld::MemoryManager &MemMgr,
                  RuntimeDyld::LoadedObjectInfo &LoadedObjInfo,
                  std::map<StringRef, JITEvaluatedSymbol> Resolved,
                  std::set<StringRef> &InternalSymbols);

  void onObjEmit(MaterializationResponsibility &R,
                 object::OwningBinary<object::ObjectFile> O,
                 std::unique_ptr<RuntimeDyld::MemoryManager> MemMgr,
                 std::unique_ptr<RuntimeDyld::LoadedObjectInfo> LoadedObjInfo,
                 std::unique_ptr<SymbolDependenceMap> Deps, Error Err);

  Error handleRemoveResources(JITDylib &JD, ResourceKey K) override;
  void handleTransferResources(JITDylib &JD, ResourceKey DstKey,
                               ResourceKey SrcKey) override;

  mutable std::mutex RTDyldLayerMutex;
  GetMemoryManagerFunction GetMemoryManager;
  NotifyLoadedFunction NotifyLoaded;
  NotifyEmittedFunction NotifyEmitted;
  bool ProcessAllSections = false;
  bool OverrideObjectFlags = false;
  bool AutoClaimObjectSymbols = false;
  DenseMap<ResourceKey, std::vector<MemoryManagerUP>> MemMgrs;
  std::vector<JITEventListener *> EventListeners;
};

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_RTDYLDOBJECTLINKINGLAYER_H
