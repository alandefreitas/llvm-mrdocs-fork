//===- ReOptimizeLayer.h - Re-optimization layer interface ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Re-optimization layer interface.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_EXECUTIONENGINE_ORC_REOPTIMIZELAYER_H
#define LLVM_EXECUTIONENGINE_ORC_REOPTIMIZELAYER_H

#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/Layer.h"
#include "llvm/ExecutionEngine/Orc/Mangling.h"
#include "llvm/ExecutionEngine/Orc/RedirectionManager.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"

namespace llvm {
namespace orc {

/// IR layer that supports profile-guided reoptimization of materialization
/// units.
class LLVM_ABI ReOptimizeLayer : public IRLayer, public ResourceManager {
public:
  /// Unique identifier for a reoptimizable materialization unit.
  using ReOptMaterializationUnitID = uint64_t;

  /// Callback that injects profiling and reoptimization request code.
  ///
  /// AddProfilerFunc will be called when ReOptimizeLayer emits the first
  /// version of a materialization unit in order to inject profiling code and
  /// reoptimization request code.
  using AddProfilerFunc = unique_function<Error(
      ReOptimizeLayer &Parent, ReOptMaterializationUnitID MUID,
      unsigned CurVersion, ThreadSafeModule &TSM)>;

  /// Callback that reoptimizes a module from collected profile data.
  ///
  /// ReOptimizeFunc will be called when ReOptimizeLayer reoptimization of a
  /// materialization unit was requested in order to reoptimize the IR module
  /// based on profile data. OldRT is the ResourceTracker that tracks the old
  /// function definitions. The OldRT must be kept alive until it can be
  /// guaranteed that every invocation of the old function definitions has been
  /// terminated.
  using ReOptimizeFunc = unique_function<Error(
      ReOptimizeLayer &Parent, ReOptMaterializationUnitID MUID,
      unsigned CurVersion, ResourceTrackerSP OldRT, ThreadSafeModule &TSM)>;

  /// Construct a ReOptimizeLayer that emits to a base IR layer.
  /// \param ES Execution session for this layer.
  /// \param DL Data layout used for mangling.
  /// \param BaseLayer IR layer to emit modules into.
  /// \param RM Manager used to redirect symbols to reoptimized definitions.
  ReOptimizeLayer(ExecutionSession &ES, DataLayout &DL, IRLayer &BaseLayer,
                  RedirectableSymbolManager &RM)
      : IRLayer(ES, BaseLayer.getManglingOptions()), ES(ES), Mangle(ES, DL),
        BaseLayer(BaseLayer), RSManager(RM), ReOptFunc(identity),
        ProfilerFunc(reoptimizeIfCallFrequent) {}

  /// Set the callback used when reoptimization is requested.
  /// \param ReOptFunc New reoptimization function to use.
  void setReoptimizeFunc(ReOptimizeFunc ReOptFunc) {
    this->ReOptFunc = std::move(ReOptFunc);
  }

  /// Set the callback used to inject profiling code on first emit.
  /// \param ProfilerFunc New profiler injection function to use.
  void setAddProfilerFunc(AddProfilerFunc ProfilerFunc) {
    this->ProfilerFunc = std::move(ProfilerFunc);
  }

  /// Add ORC Runtime-lite support for reoptimization to PlatformJD.
  ///
  /// This allows reoptimization to be used without the ORC runtime.
  ///
  /// WARNING: For use with in-process JITs only.
  /// WARNING: Do not use if the ORC runtime is loaded, as this will introduce
  ///          duplicate definitions.
  /// \param PlatformJD JITDylib that receives the lite runtime support.
  /// \param DL Data layout used when defining support symbols.
  /// \return Success, or an error if lite support cannot be added.
  Error addOrcRTLiteSupport(JITDylib &PlatformJD, const DataLayout &DL);

  /// Register reoptimize runtime dispatch handlers on PlatformJD.
  ///
  /// Registers reoptimize runtime dispatch handlers to given PlatformJD. The
  /// reoptimization request will not be handled if dispatch handler is not
  /// registered by using this function.
  /// \param PlatformJD JITDylib that receives the dispatch handlers.
  /// \return Success, or an error if the handlers cannot be registered.
  Error registerRuntimeFunctions(JITDylib &PlatformJD);

  /// Emits the given module. This should not be called by clients: it will be
  /// called by the JIT when a definition added via the add method is requested.
  /// \param R Materialization responsibility for the definitions being emitted.
  /// \param TSM Thread-safe module to emit, possibly with profiling injected.
  void emit(std::unique_ptr<MaterializationResponsibility> R,
            ThreadSafeModule TSM) override;

  /// Call-count threshold that triggers reoptimization in the default profiler.
  static const uint64_t CallCountThreshold = 10;

  /// Basic AddProfilerFunc that reoptimizes the function when the call count
  /// exceeds CallCountThreshold.
  /// \param Parent ReOptimizeLayer owning the materialization unit.
  /// \param MUID Identifier of the materialization unit being profiled.
  /// \param CurVersion Current version of the materialization unit.
  /// \param TSM Thread-safe module into which profiling code is injected.
  /// \return Success, or an error if profiling code cannot be injected.
  static Error reoptimizeIfCallFrequent(ReOptimizeLayer &Parent,
                                        ReOptMaterializationUnitID MUID,
                                        unsigned CurVersion,
                                        ThreadSafeModule &TSM);

  /// ReOptimizeFunc that leaves the module unchanged.
  /// \param Parent ReOptimizeLayer that requested reoptimization.
  /// \param MUID Identifier of the materialization unit being reoptimized.
  /// \param CurVersion Current version of the materialization unit.
  /// \param OldRT Resource tracker for the previous definitions.
  /// \param TSM Thread-safe module that would otherwise be reoptimized.
  /// \return Success always.
  static Error identity(ReOptimizeLayer &Parent,
                        ReOptMaterializationUnitID MUID, unsigned CurVersion,
                        ResourceTrackerSP OldRT, ThreadSafeModule &TSM) {
    return Error::success();
  }

  /// Insert an IR call that requests reoptimization at the given point.
  /// \param M Module that will contain the reoptimize call.
  /// \param IP Instruction before which the reoptimize call is inserted.
  /// \param MUID Identifier of the materialization unit to reoptimize.
  /// \param CurVersion Current version of the materialization unit.
  static void createReoptimizeCall(Module &M, Instruction &IP,
                                   ReOptMaterializationUnitID MUID,
                                   unsigned CurVersion);

  /// Remove resources associated with the given key.
  /// \param JD JITDylib that owns the resources.
  /// \param K Resource key to remove.
  /// \return Success, or an error if the resources cannot be removed.
  Error handleRemoveResources(JITDylib &JD, ResourceKey K) override;
  /// Transfer resources from one key to another.
  /// \param JD JITDylib that owns the resources.
  /// \param DstK Destination resource key.
  /// \param SrcK Source resource key.
  void handleTransferResources(JITDylib &JD, ResourceKey DstK,
                               ResourceKey SrcK) override;

private:
  class ReOptMaterializationUnitState {
  public:
    ReOptMaterializationUnitState() = default;
    ReOptMaterializationUnitState(ReOptMaterializationUnitID ID,
                                  ThreadSafeModule TSM)
        : ID(ID), TSM(std::move(TSM)) {}
    ReOptMaterializationUnitState(ReOptMaterializationUnitState &&Other)
        : ID(Other.ID), TSM(std::move(Other.TSM)), RT(std::move(Other.RT)),
          Reoptimizing(std::move(Other.Reoptimizing)),
          CurVersion(Other.CurVersion) {}

    ReOptMaterializationUnitID getID() { return ID; }

    const ThreadSafeModule &getThreadSafeModule() { return TSM; }

    ResourceTrackerSP getResourceTracker() {
      std::unique_lock<std::mutex> Lock(Mutex);
      return RT;
    }

    void setResourceTracker(ResourceTrackerSP RT) {
      std::unique_lock<std::mutex> Lock(Mutex);
      this->RT = RT;
    }

    uint32_t getCurVersion() {
      std::unique_lock<std::mutex> Lock(Mutex);
      return CurVersion;
    }

    LLVM_ABI bool tryStartReoptimize();
    LLVM_ABI void reoptimizeSucceeded();
    LLVM_ABI void reoptimizeFailed();

  private:
    std::mutex Mutex;
    ReOptMaterializationUnitID ID;
    ThreadSafeModule TSM;
    ResourceTrackerSP RT;
    bool Reoptimizing = false;
    uint32_t CurVersion = 0;
  };

  using SPSReoptimizeArgList =
      shared::SPSArgList<ReOptMaterializationUnitID, uint32_t>;
  using SendErrorFn = unique_function<void(Error)>;

  Expected<SymbolMap> emitMUImplSymbols(ReOptMaterializationUnitState &MUState,
                                        uint32_t Version, JITDylib &JD,
                                        ThreadSafeModule TSM);

  void rt_reoptimize(SendErrorFn SendResult, ReOptMaterializationUnitID MUID,
                     uint32_t CurVersion);

  ReOptMaterializationUnitState &
  createMaterializationUnitState(const ThreadSafeModule &TSM);

  void
  registerMaterializationUnitResource(ResourceKey Key,
                                      ReOptMaterializationUnitState &State);

  ReOptMaterializationUnitState &
  getMaterializationUnitState(ReOptMaterializationUnitID MUID);

  ExecutionSession &ES;
  MangleAndInterner Mangle;
  IRLayer &BaseLayer;
  RedirectableSymbolManager &RSManager;

  ReOptimizeFunc ReOptFunc;
  AddProfilerFunc ProfilerFunc;

  std::mutex Mutex;
  std::map<ReOptMaterializationUnitID, ReOptMaterializationUnitState> MUStates;
  DenseMap<ResourceKey, DenseSet<ReOptMaterializationUnitID>> MUResources;
  ReOptMaterializationUnitID NextID = 1;
};

} // namespace orc
} // namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_REOPTIMIZELAYER_H
