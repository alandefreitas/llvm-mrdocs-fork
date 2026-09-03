//===------ LazyReexports.h -- Utilities for lazy reexports -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Lazy re-exports are similar to normal re-exports, except that for callable
// symbols the definitions are replaced with trampolines that will look up and
// call through to the re-exported symbol at runtime. This can be used to
// enable lazy compilation.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_LAZYREEXPORTS_H
#define LLVM_EXECUTIONENGINE_ORC_LAZYREEXPORTS_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/IndirectionUtils.h"
#include "llvm/ExecutionEngine/Orc/RedirectionManager.h"
#include "llvm/ExecutionEngine/Orc/Speculation.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class Triple;

namespace orc {

/// Manages a set of lazy call-through trampolines for deferred symbol lookup.
///
/// These are compiler re-entry trampolines that are pre-bound to look up a
/// given symbol in a given JITDylib, then jump to that address. Since
/// compilation of symbols is triggered on first lookup, these call-through
/// trampolines can be used to implement lazy compilation.
///
/// The easiest way to construct these call-throughs is using the lazyReexport
/// function.
class LazyCallThroughManager {
public:
  /// Callback invoked when a call-through trampoline resolves to a landing
  /// address.
  using NotifyResolvedFunction =
      unique_function<Error(ExecutorAddr ResolvedAddr)>;

  /// Construct a LazyCallThroughManager.
  /// \param ES Execution session that owns this manager.
  /// \param ErrorHandlerAddr Address of the error-handler function.
  /// \param TP Optional trampoline pool, or nullptr if set later.
  LLVM_ABI LazyCallThroughManager(ExecutionSession &ES,
                                  ExecutorAddr ErrorHandlerAddr,
                                  TrampolinePool *TP);

  /// Return a free call-through trampoline bound to look up the given symbol.
  /// \param SourceJD JITDylib in which to look up the symbol.
  /// \param SymbolName Symbol to look up and call through to.
  /// \param NotifyResolved Callback invoked when the landing address is
  ///        resolved.
  /// \return Address of the call-through trampoline, or an error on failure.
  LLVM_ABI Expected<ExecutorAddr>
  getCallThroughTrampoline(JITDylib &SourceJD, SymbolStringPtr SymbolName,
                           NotifyResolvedFunction NotifyResolved);

  /// Resolve the landing address for the trampoline at the given address.
  /// \param TrampolineAddr Address of the trampoline to resolve.
  /// \param NotifyLandingResolved Callback invoked with the resolved landing
  ///        address.
  LLVM_ABI void resolveTrampolineLandingAddress(
      ExecutorAddr TrampolineAddr,
      TrampolinePool::NotifyLandingResolvedFunction NotifyLandingResolved);

  /// Destroy the lazy call-through manager.
  virtual ~LazyCallThroughManager() = default;

protected:
  /// Callback type for reporting a resolved trampoline landing address.
  using NotifyLandingResolvedFunction =
      TrampolinePool::NotifyLandingResolvedFunction;

  /// Binding from a trampoline to the JITDylib and symbol it re-exports.
  struct ReexportsEntry {
    /// JITDylib that defines the re-exported symbol.
    JITDylib *SourceJD;
    /// Name of the re-exported symbol.
    SymbolStringPtr SymbolName;
  };

  /// Report a call-through error and return the error-handler address.
  /// \param Err Error to report.
  /// \return Address of the error-handler function.
  LLVM_ABI ExecutorAddr reportCallThroughError(Error Err);
  /// Look up the re-export entry associated with the given trampoline.
  /// \param TrampolineAddr Address of the trampoline.
  /// \return The re-export entry, or an error if none is registered.
  LLVM_ABI Expected<ReexportsEntry> findReexport(ExecutorAddr TrampolineAddr);
  /// Notify that the trampoline at the given address has resolved.
  /// \param TrampolineAddr Address of the trampoline.
  /// \param ResolvedAddr Resolved landing address.
  /// \return Success, or an error if notification fails.
  LLVM_ABI Error notifyResolved(ExecutorAddr TrampolineAddr,
                                ExecutorAddr ResolvedAddr);
  /// Set the trampoline pool used by this manager.
  /// \param TP Trampoline pool to use.
  void setTrampolinePool(TrampolinePool &TP) { this->TP = &TP; }

private:
  using ReexportsMap = std::map<ExecutorAddr, ReexportsEntry>;

  using NotifiersMap = std::map<ExecutorAddr, NotifyResolvedFunction>;

  std::mutex LCTMMutex;
  ExecutionSession &ES;
  ExecutorAddr ErrorHandlerAddr;
  TrampolinePool *TP = nullptr;
  ReexportsMap Reexports;
  NotifiersMap Notifiers;
};

/// A lazy call-through manager that builds trampolines in the current process.
class LocalLazyCallThroughManager : public LazyCallThroughManager {
private:
  using NotifyTargetResolved = unique_function<void(ExecutorAddr)>;

  LocalLazyCallThroughManager(ExecutionSession &ES,
                              ExecutorAddr ErrorHandlerAddr)
      : LazyCallThroughManager(ES, ErrorHandlerAddr, nullptr) {}

  template <typename ORCABI> Error init() {
    auto TP = LocalTrampolinePool<ORCABI>::Create(
        [this](ExecutorAddr TrampolineAddr,
               TrampolinePool::NotifyLandingResolvedFunction
                   NotifyLandingResolved) {
          resolveTrampolineLandingAddress(TrampolineAddr,
                                          std::move(NotifyLandingResolved));
        });

    if (!TP)
      return TP.takeError();

    this->TP = std::move(*TP);
    setTrampolinePool(*this->TP);
    return Error::success();
  }

  std::unique_ptr<TrampolinePool> TP;

public:
  /// Create a LocalLazyCallThroughManager using the given ABI. See
  /// createLocalLazyCallThroughManager.
  /// \param ES Execution session that owns the manager.
  /// \param ErrorHandlerAddr Address of the error-handler function.
  /// \return A new LocalLazyCallThroughManager, or an error if setup fails.
  template <typename ORCABI>
  static Expected<std::unique_ptr<LocalLazyCallThroughManager>>
  Create(ExecutionSession &ES, ExecutorAddr ErrorHandlerAddr) {
    auto LLCTM = std::unique_ptr<LocalLazyCallThroughManager>(
        new LocalLazyCallThroughManager(ES, ErrorHandlerAddr));

    if (auto Err = LLCTM->init<ORCABI>())
      return std::move(Err);

    return std::move(LLCTM);
  }
};

/// Create a LocalLazyCallThroughManager from the given triple and execution
/// session.
/// \param T Target triple selecting the ABI for trampolines.
/// \param ES Execution session that owns the manager.
/// \param ErrorHandlerAddr Address of the error-handler function.
/// \return A new LazyCallThroughManager, or an error if creation fails.
LLVM_ABI Expected<std::unique_ptr<LazyCallThroughManager>>
createLocalLazyCallThroughManager(const Triple &T, ExecutionSession &ES,
                                  ExecutorAddr ErrorHandlerAddr);

/// Materialization unit that builds lazy re-exports for callable symbols.
///
/// These are callable entry points that call through to the given symbols.
/// Unlike a 'true' re-export, the address of the lazy re-export will not
/// match the address of the re-exported symbol, but calling it will behave
/// the same as calling the re-exported symbol.
class LLVM_ABI LazyReexportsMaterializationUnit : public MaterializationUnit {
public:
  /// Construct a materialization unit for the given callable aliases.
  /// \param LCTManager Manager providing call-through trampolines.
  /// \param RSManager Manager for redirectable symbols.
  /// \param SourceJD JITDylib that defines the aliasee symbols.
  /// \param CallableAliases Map from lazy re-export names to aliasees.
  /// \param SrcJDLoc Optional map recording implementation symbol locations.
  LazyReexportsMaterializationUnit(LazyCallThroughManager &LCTManager,
                                   RedirectableSymbolManager &RSManager,
                                   JITDylib &SourceJD,
                                   SymbolAliasMap CallableAliases,
                                   ImplSymbolMap *SrcJDLoc);

  /// Return the name of this materialization unit.
  /// \return The name of this materialization unit.
  StringRef getName() const override;

private:
  void materialize(std::unique_ptr<MaterializationResponsibility> R) override;
  void discard(const JITDylib &JD, const SymbolStringPtr &Name) override;
  static MaterializationUnit::Interface
  extractFlags(const SymbolAliasMap &Aliases);

  LazyCallThroughManager &LCTManager;
  RedirectableSymbolManager &RSManager;
  JITDylib &SourceJD;
  SymbolAliasMap CallableAliases;
  ImplSymbolMap *AliaseeTable;
};

/// Create a materialization unit that defines lazy re-exports.
///
/// Each lazy re-export is a callable symbol that will look up and dispatch to
/// the given aliasee on first call. All subsequent calls will go directly to
/// the aliasee.
/// \param LCTManager Manager providing call-through trampolines.
/// \param RSManager Manager for redirectable symbols.
/// \param SourceJD JITDylib that defines the aliasee symbols.
/// \param CallableAliases Map from lazy re-export names to aliasees.
/// \param SrcJDLoc Optional map recording implementation symbol locations.
/// \return A materialization unit that defines the lazy re-exports.
inline std::unique_ptr<LazyReexportsMaterializationUnit>
lazyReexports(LazyCallThroughManager &LCTManager,
              RedirectableSymbolManager &RSManager, JITDylib &SourceJD,
              SymbolAliasMap CallableAliases,
              ImplSymbolMap *SrcJDLoc = nullptr) {
  return std::make_unique<LazyReexportsMaterializationUnit>(
      LCTManager, RSManager, SourceJD, std::move(CallableAliases), SrcJDLoc);
}

/// Manages lazy re-exports backed by ORC-runtime trampolines.
class LLVM_ABI LazyReexportsManager : public ResourceManager {

  friend std::unique_ptr<MaterializationUnit>
  lazyReexports(LazyReexportsManager &, SymbolAliasMap);

public:
  /// Identifies a lazy re-export and its underlying body symbol.
  struct CallThroughInfo {
    /// JITDylib that owns the lazy re-export.
    JITDylibSP JD;
    /// Name of the lazy re-export symbol.
    SymbolStringPtr Name;
    /// Name of the underlying body / aliasee symbol.
    SymbolStringPtr BodyName;
  };

  /// Observer notified when lazy re-exports are created, moved, removed, or
  /// called.
  class LLVM_ABI Listener {
  public:
    /// Call-through information for a lazy re-export.
    using CallThroughInfo = LazyReexportsManager::CallThroughInfo;

    /// Destroy the listener.
    virtual ~Listener();

    /// Called under the session lock when new lazy reexports are created.
    /// \param JD JITDylib that owns the re-exports.
    /// \param K Resource key associated with the re-exports.
    /// \param Reexports Map of newly created lazy re-exports.
    virtual void onLazyReexportsCreated(JITDylib &JD, ResourceKey K,
                                        const SymbolAliasMap &Reexports) = 0;

    /// Called under the session lock when lazy reexports have their ownership
    /// transferred to a new ResourceKey.
    /// \param JD JITDylib that owns the re-exports.
    /// \param DstK Destination resource key.
    /// \param SrcK Source resource key.
    virtual void onLazyReexportsTransfered(JITDylib &JD, ResourceKey DstK,
                                           ResourceKey SrcK) = 0;

    /// Called under the session lock when lazy reexports are removed.
    /// \param JD JITDylib that owns the re-exports.
    /// \param K Resource key associated with the re-exports.
    /// \return Success, or an error if removal fails.
    virtual Error onLazyReexportsRemoved(JITDylib &JD, ResourceKey K) = 0;

    /// Called outside the session lock when a lazy reexport is called.
    ///
    /// NOTE: Since this is called outside the session lock there is a chance
    /// that the reexport referred to has already been removed. Listeners must
    /// be prepared to handle requests for stale reexports.
    /// \param CTI Call-through information for the re-export that was called.
    virtual void onLazyReexportCalled(const CallThroughInfo &CTI) = 0;
  };

  /// Callback invoked when trampoline entry addresses are ready.
  using OnTrampolinesReadyFn = unique_function<void(
      Expected<std::vector<ExecutorSymbolDef>> EntryAddrs)>;
  /// Functor that emits trampolines into a resource tracker.
  using EmitTrampolinesFn =
      unique_function<void(ResourceTrackerSP RT, size_t NumTrampolines,
                           OnTrampolinesReadyFn OnTrampolinesReady)>;

  /// Create a LazyReexportsManager that uses the ORC runtime for reentry.
  /// This will work both in-process and out-of-process.
  /// \param EmitTrampolines Functor used to emit reentry trampolines.
  /// \param RSMgr Manager for redirectable symbols.
  /// \param PlatformJD JITDylib that provides platform support.
  /// \param L Optional listener for lazy re-export events.
  /// \return A new LazyReexportsManager, or an error if setup fails.
  static Expected<std::unique_ptr<LazyReexportsManager>>
  Create(EmitTrampolinesFn EmitTrampolines, RedirectableSymbolManager &RSMgr,
         JITDylib &PlatformJD, Listener *L = nullptr);

  /// LazyReexportsManager is not move-constructible.
  /// \param Other Instance that would be moved.
  LazyReexportsManager(LazyReexportsManager &&Other) = delete;
  /// LazyReexportsManager is not move-assignable.
  /// \param Other Instance that would be moved.
  LazyReexportsManager &operator=(LazyReexportsManager &&Other) = delete;

  /// Remove resources associated with the given key.
  /// \param JD JITDylib that owns the resources.
  /// \param K Resource key to remove.
  /// \return Success, or an error if removal fails.
  Error handleRemoveResources(JITDylib &JD, ResourceKey K) override;
  /// Transfer resources from one key to another.
  /// \param JD JITDylib that owns the resources.
  /// \param DstK Destination resource key.
  /// \param SrcK Source resource key.
  void handleTransferResources(JITDylib &JD, ResourceKey DstK,
                               ResourceKey SrcK) override;

private:
  class MU;
  class Plugin;

  using ResolveSendResultFn =
      unique_function<void(Expected<ExecutorSymbolDef>)>;

  LazyReexportsManager(EmitTrampolinesFn EmitTrampolines,
                       RedirectableSymbolManager &RSMgr, JITDylib &PlatformJD,
                       Listener *L, Error &Err);

  std::unique_ptr<MaterializationUnit>
  createLazyReexports(SymbolAliasMap Reexports);

  void emitReentryTrampolines(std::unique_ptr<MaterializationResponsibility> MR,
                              SymbolAliasMap Reexports);
  void emitRedirectableSymbols(
      std::unique_ptr<MaterializationResponsibility> MR,
      SymbolAliasMap Reexports,
      Expected<std::vector<ExecutorSymbolDef>> ReentryPoints);
  void resolve(ResolveSendResultFn SendResult, ExecutorAddr ReentryStubAddr);

  ExecutionSession &ES;
  EmitTrampolinesFn EmitTrampolines;
  RedirectableSymbolManager &RSMgr;
  Listener *L;

  DenseMap<ResourceKey, std::vector<ExecutorAddr>> KeyToReentryAddrs;
  DenseMap<ExecutorAddr, CallThroughInfo> CallThroughs;
};

/// Create a materialization unit that defines lazy re-exports.
///
/// Each lazy re-export is a callable symbol that will look up and dispatch to
/// the given aliasee on first call. All subsequent calls will go directly to
/// the aliasee.
/// \param LRM Manager that owns and implements the lazy re-exports.
/// \param Reexports Map from lazy re-export names to aliasees.
/// \return A materialization unit that defines the lazy re-exports.
inline std::unique_ptr<MaterializationUnit>
lazyReexports(LazyReexportsManager &LRM, SymbolAliasMap Reexports) {
  return LRM.createLazyReexports(std::move(Reexports));
}

/// Listener that records lazy re-export calls and drives speculative lookups.
class LLVM_ABI SimpleLazyReexportsSpeculator
    : public LazyReexportsManager::Listener {
public:
  /// Callback invoked to record execution of a lazy re-export.
  using RecordExecutionFunction =
      unique_function<void(const CallThroughInfo &CTI)>;

  /// Create a SimpleLazyReexportsSpeculator for the given execution session.
  /// \param ES Execution session that owns the speculator.
  /// \param RecordExec Optional callback invoked when a re-export is called.
  /// \return Shared ownership of the new speculator.
  static std::shared_ptr<SimpleLazyReexportsSpeculator>
  Create(ExecutionSession &ES, RecordExecutionFunction RecordExec = {}) {
    class make_shared_helper : public SimpleLazyReexportsSpeculator {
    public:
      make_shared_helper(ExecutionSession &ES,
                         RecordExecutionFunction RecordExec)
          : SimpleLazyReexportsSpeculator(ES, std::move(RecordExec)) {}
    };

    auto Instance =
        std::make_shared<make_shared_helper>(ES, std::move(RecordExec));
    Instance->WeakThis = Instance;
    return Instance;
  }

  /// SimpleLazyReexportsSpeculator is not move-constructible.
  /// \param Other Instance that would be moved.
  SimpleLazyReexportsSpeculator(SimpleLazyReexportsSpeculator &&Other) = delete;
  /// SimpleLazyReexportsSpeculator is not move-assignable.
  /// \param Other Instance that would be moved.
  SimpleLazyReexportsSpeculator &
  operator=(SimpleLazyReexportsSpeculator &&Other) = delete;
  /// Destroy the simple lazy re-exports speculator.
  ~SimpleLazyReexportsSpeculator() override;

  /// Called under the session lock when new lazy reexports are created.
  /// \param JD JITDylib that owns the re-exports.
  /// \param K Resource key associated with the re-exports.
  /// \param Reexports Map of newly created lazy re-exports.
  void onLazyReexportsCreated(JITDylib &JD, ResourceKey K,
                              const SymbolAliasMap &Reexports) override;

  /// Called under the session lock when lazy reexports have their ownership
  /// transferred to a new ResourceKey.
  /// \param JD JITDylib that owns the re-exports.
  /// \param DstK Destination resource key.
  /// \param SrcK Source resource key.
  void onLazyReexportsTransfered(JITDylib &JD, ResourceKey DstK,
                                 ResourceKey SrcK) override;

  /// Called under the session lock when lazy reexports are removed.
  /// \param JD JITDylib that owns the re-exports.
  /// \param K Resource key associated with the re-exports.
  /// \return Success, or an error if removal fails.
  Error onLazyReexportsRemoved(JITDylib &JD, ResourceKey K) override;

  /// Called outside the session lock when a lazy reexport is called.
  /// \param CTI Call-through information for the re-export that was called.
  void onLazyReexportCalled(const CallThroughInfo &CTI) override;

  /// Add new symbols as candidates for speculative lookup.
  /// \param NewSuggestions Pairs of (JITDylib name, symbol) to speculate on.
  void addSpeculationSuggestions(
      std::vector<std::pair<std::string, SymbolStringPtr>> NewSuggestions);

private:
  SimpleLazyReexportsSpeculator(ExecutionSession &ES,
                                RecordExecutionFunction RecordExec)
      : ES(ES), RecordExec(std::move(RecordExec)) {}

  bool doNextSpeculativeLookup();

  class SpeculateTask;

  using KeyToFunctionBodiesMap =
      DenseMap<ResourceKey, std::vector<SymbolStringPtr>>;

  ExecutionSession &ES;
  RecordExecutionFunction RecordExec;
  std::weak_ptr<SimpleLazyReexportsSpeculator> WeakThis;
  DenseMap<JITDylib *, KeyToFunctionBodiesMap> LazyReexports;
  std::deque<std::pair<std::string, SymbolStringPtr>> SpeculateSuggestions;
  bool SpeculateTaskActive = false;
};

} // End namespace orc
} // End namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_LAZYREEXPORTS_H
