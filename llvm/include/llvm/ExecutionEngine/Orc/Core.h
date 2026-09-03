//===------ Core.h -- Core ORC APIs (Layer, JITDylib, etc.) -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Contains core ORC APIs.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_CORE_H
#define LLVM_EXECUTIONENGINE_ORC_CORE_H

#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/FunctionExtras.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ExecutionEngine/JITLink/JITLinkDylib.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/CoreContainers.h"
#include "llvm/ExecutionEngine/Orc/ExecutorProcessControl.h"
#include "llvm/ExecutionEngine/Orc/MaterializationUnit.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorSymbolDef.h"
#include "llvm/ExecutionEngine/Orc/Shared/WrapperFunctionUtils.h"
#include "llvm/ExecutionEngine/Orc/SymbolLookupSet.h"
#include "llvm/ExecutionEngine/Orc/TaskDispatch.h"
#include "llvm/ExecutionEngine/Orc/WaitingOnGraph.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ExtensibleRTTI.h"

#include <atomic>
#include <deque>
#include <future>
#include <memory>
#include <vector>

namespace llvm {
namespace orc {

// Forward declare some classes.
class AsynchronousSymbolQuery;
class ExecutionSession;
class MaterializationResponsibility;
class JITDylib;
class ResourceTracker;
/// Opaque state for a lookup that may be suspended by a DefinitionGenerator.
class InProgressLookupState;

enum class SymbolState : uint8_t;

/// Waiting-on dependence graph keyed by JITDylib and non-owning symbol name.
using WaitingOnGraph =
    detail::WaitingOnGraph<JITDylib *, NonOwningSymbolStringPtr>;

/// Intrusive shared pointer to a ResourceTracker.
using ResourceTrackerSP = IntrusiveRefCntPtr<ResourceTracker>;
/// Intrusive shared pointer to a JITDylib.
using JITDylibSP = IntrusiveRefCntPtr<JITDylib>;

/// Opaque key identifying resources associated with a ResourceTracker.
using ResourceKey = uintptr_t;

/// API to remove / transfer ownership of JIT resources.
class ResourceTracker : public ThreadSafeRefCountedBase<ResourceTracker> {
private:
  friend class ExecutionSession;
  friend class JITDylib;
  friend class MaterializationResponsibility;

public:
  /// ResourceTracker is not copy-constructible.
  /// @param Other Instance that would be copied.
  ResourceTracker(const ResourceTracker &Other) = delete;
  /// ResourceTracker is not copy-assignable.
  /// @param Other Instance that would be copied.
  ResourceTracker &operator=(const ResourceTracker &Other) = delete;
  /// ResourceTracker is not move-constructible.
  /// @param Other Instance that would be moved.
  ResourceTracker(ResourceTracker &&Other) = delete;
  /// ResourceTracker is not move-assignable.
  /// @param Other Instance that would be moved.
  ResourceTracker &operator=(ResourceTracker &&Other) = delete;

  /// Destroy this tracker and release its resources.
  LLVM_ABI ~ResourceTracker();

  /// Return the JITDylib targeted by this tracker.
  /// @return JITDylib associated with this tracker.
  JITDylib &getJITDylib() const {
    return *reinterpret_cast<JITDylib *>(JDAndFlag.load() &
                                         ~static_cast<uintptr_t>(1));
  }

  /// Run \p F under the session lock with this tracker's ResourceKey.
  ///
  /// This is the safe way to associate resources with trackers.
  /// @param F Callback invoked with the associated ResourceKey.
  /// @return Success, or an error if this tracker is defunct.
  template <typename Func> Error withResourceKeyDo(Func &&F);

  /// Remove all resources associated with this key.
  /// @return Success, or an error if removal fails.
  LLVM_ABI Error remove();

  /// Transfer all resources associated with this key to \p DstRT.
  ///
  /// \p DstRT must target the same JITDylib as this tracker.
  /// @param DstRT Destination tracker that receives the resources.
  LLVM_ABI void transferTo(ResourceTracker &DstRT);

  /// Return true if this tracker has become defunct.
  /// @return True if this tracker is defunct.
  bool isDefunct() const { return JDAndFlag.load() & 0x1; }

  /// Return the key associated with this tracker for debug logging only.
  ///
  /// This method should not be used except for debug logging: there is no
  /// guarantee that the returned value will remain valid.
  /// @return Resource key for this tracker (debug use only).
  ResourceKey getKeyUnsafe() const { return reinterpret_cast<uintptr_t>(this); }

private:
  ResourceTracker(JITDylibSP JD);

  void makeDefunct();

  std::atomic_uintptr_t JDAndFlag;
};

/// Listens for ResourceTracker operations.
class LLVM_ABI ResourceManager {
public:
  /// Destroy this ResourceManager.
  virtual ~ResourceManager();

  /// Handle removal of resources for key \p K outside the session lock.
  ///
  /// ResourceManagers should perform book-keeping under the session lock, and
  /// any expensive cleanup outside the session lock.
  /// @param JD JITDylib whose resources are being removed.
  /// @param K Resource key identifying the resources to remove.
  /// @return Success, or an error if resource removal fails.
  virtual Error handleRemoveResources(JITDylib &JD, ResourceKey K) = 0;

  /// Handle transfer of resources from \p SrcK to \p DstK under the session
  /// lock.
  ///
  /// ResourceManagers DO NOT need to re-lock the session.
  /// @param JD JITDylib whose resources are being transferred.
  /// @param DstK Destination resource key.
  /// @param SrcK Source resource key.
  virtual void handleTransferResources(JITDylib &JD, ResourceKey DstK,
                                       ResourceKey SrcK) = 0;
};

/// Lookup flags that apply to each dylib in the search order for a lookup.
///
/// If MatchHiddenSymbolsOnly is used (the default) for a given dylib, then
/// only symbols in that Dylib's interface will be searched. If
/// MatchHiddenSymbols is used then symbols with hidden visibility will match
/// as well.
enum class JITDylibLookupFlags {
  /// Match only symbols exported from the JITDylib's interface.
  MatchExportedSymbolsOnly,
  /// Match both exported and hidden (non-exported) symbols.
  MatchAllSymbols
};

/// Describes the kind of lookup being performed.
///
/// The lookup kind is passed to symbol generators (if they're invoked) to help
/// them determine what definitions to generate.
///
/// Static -- Lookup is being performed as-if at static link time (e.g.
///           generators representing static archives should pull in new
///           definitions).
///
/// DLSym -- Lookup is being performed as-if at runtime (e.g. generators
///          representing static archives should not pull in new definitions).
enum class LookupKind {
  /// Lookup as at static link time; generators may pull in new definitions.
  Static,
  /// Lookup as at runtime (dlsym-style); generators should not pull archives.
  DLSym
};

/// A list of (JITDylib*, JITDylibLookupFlags) pairs to be used as a search
/// order during symbol lookup.
using JITDylibSearchOrder =
    std::vector<std::pair<JITDylib *, JITDylibLookupFlags>>;

/// Convenience function for creating a search order from an ArrayRef of
/// JITDylib*, all with the same flags.
/// @param JDs JITDylibs to include in the search order.
/// @param Flags Lookup flags applied to every entry in the order.
/// @return Search order pairing each JITDylib with \p Flags.
inline JITDylibSearchOrder makeJITDylibSearchOrder(
    ArrayRef<JITDylib *> JDs,
    JITDylibLookupFlags Flags = JITDylibLookupFlags::MatchExportedSymbolsOnly) {
  JITDylibSearchOrder O;
  O.reserve(JDs.size());
  for (auto *JD : JDs)
    O.push_back(std::make_pair(JD, Flags));
  return O;
}

/// Entry describing a symbol alias and the flags for the alias name.
struct SymbolAliasMapEntry {
  /// Construct an empty alias map entry.
  SymbolAliasMapEntry() = default;
  /// Construct an alias entry for \p Aliasee with flags \p AliasFlags.
  /// @param Aliasee Symbol that the alias refers to.
  /// @param AliasFlags JIT symbol flags for the alias name.
  SymbolAliasMapEntry(SymbolStringPtr Aliasee, JITSymbolFlags AliasFlags)
      : Aliasee(std::move(Aliasee)), AliasFlags(AliasFlags) {}

  /// Symbol name that this alias refers to.
  SymbolStringPtr Aliasee;
  /// Flags associated with the alias symbol itself.
  JITSymbolFlags AliasFlags;
};

/// A map of Symbols to (Symbol, Flags) pairs.
using SymbolAliasMap = DenseMap<SymbolStringPtr, SymbolAliasMapEntry>;

/// Callback to notify client that symbols have been resolved.
using SymbolsResolvedCallback = unique_function<void(Expected<SymbolMap>)>;

/// Callback to register the dependencies for a given query.
using RegisterDependenciesFunction =
    std::function<void(const SymbolDependenceMap &)>;

/// This can be used as the value for a RegisterDependenciesFunction if there
/// are no dependants to register with.
LLVM_ABI extern RegisterDependenciesFunction NoDependenciesToRegister;

/// Error returned when an operation uses a defunct ResourceTracker.
class LLVM_ABI ResourceTrackerDefunct
    : public ErrorInfo<ResourceTrackerDefunct> {
public:
  /// Unique ErrorInfo RTTI key for ResourceTrackerDefunct.
  static char ID;

  /// Construct an error for defunct tracker \p RT.
  /// @param RT Resource tracker that has become defunct.
  ResourceTrackerDefunct(ResourceTrackerSP RT);
  /// Convert this error to a \c std::error_code.
  /// @return std::error_code corresponding to this error.
  std::error_code convertToErrorCode() const override;
  /// Write a description of this error to \p OS.
  /// @param OS Stream to receive the error message.
  void log(raw_ostream &OS) const override;

private:
  ResourceTrackerSP RT;
};

/// Returned by operations that fail because a JITDylib has been closed.
class LLVM_ABI JITDylibDefunct : public ErrorInfo<JITDylibDefunct> {
public:
  /// Unique ErrorInfo RTTI key for JITDylibDefunct.
  static char ID;

  /// Construct an error for closed JITDylib \p JD.
  /// @param JD JITDylib that has become defunct.
  JITDylibDefunct(JITDylibSP JD) : JD(std::move(JD)) {}
  /// Convert this error to a \c std::error_code.
  /// @return std::error_code corresponding to this error.
  std::error_code convertToErrorCode() const override;
  /// Write a description of this error to \p OS.
  /// @param OS Stream to receive the error message.
  void log(raw_ostream &OS) const override;

private:
  JITDylibSP JD;
};

/// Used to notify a JITDylib that the given set of symbols failed to
/// materialize.
class LLVM_ABI FailedToMaterialize : public ErrorInfo<FailedToMaterialize> {
public:
  /// Unique ErrorInfo RTTI key for FailedToMaterialize.
  static char ID;

  /// Construct an error for the failed symbols in \p Symbols.
  /// @param SSP Symbol string pool for names in \p Symbols.
  /// @param Symbols Map of JITDylibs to symbols that failed to materialize.
  FailedToMaterialize(std::shared_ptr<SymbolStringPool> SSP,
                      std::shared_ptr<SymbolDependenceMap> Symbols);
  /// Destroy this error and release owned symbol data.
  ~FailedToMaterialize() override;
  /// Convert this error to a \c std::error_code.
  /// @return std::error_code corresponding to this error.
  std::error_code convertToErrorCode() const override;
  /// Write a description of this error to \p OS.
  /// @param OS Stream to receive the error message.
  void log(raw_ostream &OS) const override;
  /// Return the map of symbols that failed to materialize.
  /// @return Map of JITDylibs to symbols that failed to materialize.
  const SymbolDependenceMap &getSymbols() const { return *Symbols; }

private:
  std::shared_ptr<SymbolStringPool> SSP;
  std::shared_ptr<SymbolDependenceMap> Symbols;
};

/// Used to report failure due to unsatisfiable symbol dependencies.
class LLVM_ABI UnsatisfiedSymbolDependencies
    : public ErrorInfo<UnsatisfiedSymbolDependencies> {
public:
  /// Unique ErrorInfo RTTI key for UnsatisfiedSymbolDependencies.
  static char ID;

  /// Construct an error describing unsatisfiable dependencies in \p JD.
  /// @param SSP Symbol string pool for names in this error.
  /// @param JD JITDylib containing the failed symbols.
  /// @param FailedSymbols Symbols that could not be satisfied.
  /// @param BadDeps Dependence map explaining the failure.
  /// @param Explanation Human-readable explanation of the failure.
  UnsatisfiedSymbolDependencies(std::shared_ptr<SymbolStringPool> SSP,
                                JITDylibSP JD, SymbolNameSet FailedSymbols,
                                SymbolDependenceMap BadDeps,
                                std::string Explanation);
  /// Convert this error to a \c std::error_code.
  /// @return std::error_code corresponding to this error.
  std::error_code convertToErrorCode() const override;
  /// Write a description of this error to \p OS.
  /// @param OS Stream to receive the error message.
  void log(raw_ostream &OS) const override;

private:
  std::shared_ptr<SymbolStringPool> SSP;
  JITDylibSP JD;
  SymbolNameSet FailedSymbols;
  SymbolDependenceMap BadDeps;
  std::string Explanation;
};

/// Used to notify clients when symbols can not be found during a lookup.
class LLVM_ABI SymbolsNotFound : public ErrorInfo<SymbolsNotFound> {
public:
  /// Unique ErrorInfo RTTI key for SymbolsNotFound.
  static char ID;

  /// Construct an error for the missing symbols in \p Symbols.
  /// @param SSP Symbol string pool for names in \p Symbols.
  /// @param Symbols Set of symbol names that were not found.
  SymbolsNotFound(std::shared_ptr<SymbolStringPool> SSP, SymbolNameSet Symbols);
  /// Construct an error for the missing symbols in \p Symbols.
  /// @param SSP Symbol string pool for names in \p Symbols.
  /// @param Symbols Vector of symbol names that were not found.
  SymbolsNotFound(std::shared_ptr<SymbolStringPool> SSP,
                  SymbolNameVector Symbols);
  /// Convert this error to a \c std::error_code.
  /// @return std::error_code corresponding to this error.
  std::error_code convertToErrorCode() const override;
  /// Write a description of this error to \p OS.
  /// @param OS Stream to receive the error message.
  void log(raw_ostream &OS) const override;
  /// Return the symbol string pool associated with this error.
  /// @return Shared pointer to the symbol string pool.
  std::shared_ptr<SymbolStringPool> getSymbolStringPool() { return SSP; }
  /// Return the vector of symbols that were not found.
  /// @return Vector of symbol names that were not found.
  const SymbolNameVector &getSymbols() const { return Symbols; }

private:
  std::shared_ptr<SymbolStringPool> SSP;
  SymbolNameVector Symbols;
};

/// Used to notify clients that a set of symbols could not be removed.
class LLVM_ABI SymbolsCouldNotBeRemoved
    : public ErrorInfo<SymbolsCouldNotBeRemoved> {
public:
  /// Unique ErrorInfo RTTI key for SymbolsCouldNotBeRemoved.
  static char ID;

  /// Construct an error for symbols that could not be removed.
  /// @param SSP Symbol string pool for names in \p Symbols.
  /// @param Symbols Set of symbol names that could not be removed.
  SymbolsCouldNotBeRemoved(std::shared_ptr<SymbolStringPool> SSP,
                           SymbolNameSet Symbols);
  /// Convert this error to a \c std::error_code.
  /// @return std::error_code corresponding to this error.
  std::error_code convertToErrorCode() const override;
  /// Write a description of this error to \p OS.
  /// @param OS Stream to receive the error message.
  void log(raw_ostream &OS) const override;
  /// Return the symbol string pool associated with this error.
  /// @return Shared pointer to the symbol string pool.
  std::shared_ptr<SymbolStringPool> getSymbolStringPool() { return SSP; }
  /// Return the set of symbols that could not be removed.
  /// @return Set of symbol names that could not be removed.
  const SymbolNameSet &getSymbols() const { return Symbols; }

private:
  std::shared_ptr<SymbolStringPool> SSP;
  SymbolNameSet Symbols;
};

/// Error for modules missing definitions claimed by MaterializationResponsibility.
///
/// Errors of this type should be returned if a module fails to include
/// definitions that are claimed by the module's associated
/// MaterializationResponsibility. If this error is returned it is indicative of
/// a broken transformation / compiler / object cache.
class LLVM_ABI MissingSymbolDefinitions
    : public ErrorInfo<MissingSymbolDefinitions> {
public:
  /// Unique ErrorInfo RTTI key for MissingSymbolDefinitions.
  static char ID;

  /// Construct an error for missing definitions in module \p ModuleName.
  /// @param SSP Symbol string pool for names in \p Symbols.
  /// @param ModuleName Name of the module with missing definitions.
  /// @param Symbols Symbols claimed but not defined by the module.
  MissingSymbolDefinitions(std::shared_ptr<SymbolStringPool> SSP,
                           std::string ModuleName, SymbolNameVector Symbols)
      : SSP(std::move(SSP)), ModuleName(std::move(ModuleName)),
        Symbols(std::move(Symbols)) {}
  /// Convert this error to a \c std::error_code.
  /// @return std::error_code corresponding to this error.
  std::error_code convertToErrorCode() const override;
  /// Write a description of this error to \p OS.
  /// @param OS Stream to receive the error message.
  void log(raw_ostream &OS) const override;
  /// Return the symbol string pool associated with this error.
  /// @return Shared pointer to the symbol string pool.
  std::shared_ptr<SymbolStringPool> getSymbolStringPool() { return SSP; }
  /// Return the name of the module with missing definitions.
  /// @return Name of the module with missing definitions.
  const std::string &getModuleName() const { return ModuleName; }
  /// Return the symbols claimed but not defined by the module.
  /// @return Symbols claimed but not defined by the module.
  const SymbolNameVector &getSymbols() const { return Symbols; }
private:
  std::shared_ptr<SymbolStringPool> SSP;
  std::string ModuleName;
  SymbolNameVector Symbols;
};

/// Error for modules defining symbols not claimed by MaterializationResponsibility.
///
/// Errors of this type should be returned if a module contains definitions for
/// symbols that are not claimed by the module's associated
/// MaterializationResponsibility. If this error is returned it is indicative of
/// a broken transformation / compiler / object cache.
class LLVM_ABI UnexpectedSymbolDefinitions
    : public ErrorInfo<UnexpectedSymbolDefinitions> {
public:
  /// Unique ErrorInfo RTTI key for UnexpectedSymbolDefinitions.
  static char ID;

  /// Construct an error for unexpected definitions in module \p ModuleName.
  /// @param SSP Symbol string pool for names in \p Symbols.
  /// @param ModuleName Name of the module with unexpected definitions.
  /// @param Symbols Symbols defined but not claimed by the responsibility.
  UnexpectedSymbolDefinitions(std::shared_ptr<SymbolStringPool> SSP,
                              std::string ModuleName, SymbolNameVector Symbols)
      : SSP(std::move(SSP)), ModuleName(std::move(ModuleName)),
        Symbols(std::move(Symbols)) {}
  /// Convert this error to a \c std::error_code.
  /// @return std::error_code corresponding to this error.
  std::error_code convertToErrorCode() const override;
  /// Write a description of this error to \p OS.
  /// @param OS Stream to receive the error message.
  void log(raw_ostream &OS) const override;
  /// Return the symbol string pool associated with this error.
  /// @return Shared pointer to the symbol string pool.
  std::shared_ptr<SymbolStringPool> getSymbolStringPool() { return SSP; }
  /// Return the name of the module with unexpected definitions.
  /// @return Name of the module with unexpected definitions.
  const std::string &getModuleName() const { return ModuleName; }
  /// Return the symbols defined but not claimed by the responsibility.
  /// @return Symbols defined but not claimed by the responsibility.
  const SymbolNameVector &getSymbols() const { return Symbols; }
private:
  std::shared_ptr<SymbolStringPool> SSP;
  std::string ModuleName;
  SymbolNameVector Symbols;
};

/// A set of symbols and the their dependencies. Used to describe dependencies
/// for the MaterializationResponsibility::notifyEmitted operation.
struct SymbolDependenceGroup {
  /// Symbols in this group that share the dependence map below.
  SymbolNameSet Symbols;
  /// Dependencies of \c Symbols on symbols outside this responsibility.
  SymbolDependenceMap Dependencies;
};

/// Tracks responsibility for materialization, and mediates interactions between
/// MaterializationUnits and JDs.
///
/// An instance of this class is passed to MaterializationUnits when their
/// materialize method is called. It allows MaterializationUnits to resolve and
/// emit symbols, or abandon materialization by notifying any unmaterialized
/// symbols of an error.
class MaterializationResponsibility {
  friend class ExecutionSession;
  friend class JITDylib;

public:
  /// MaterializationResponsibility is not move-constructible.
  /// @param Other Instance that would be moved.
  MaterializationResponsibility(MaterializationResponsibility &&Other) = delete;
  /// MaterializationResponsibility is not move-assignable.
  /// @param Other Instance that would be moved.
  MaterializationResponsibility &
  operator=(MaterializationResponsibility &&Other) = delete;

  /// Destruct a MaterializationResponsibility instance. In debug mode
  ///        this asserts that all symbols being tracked have been either
  ///        emitted or notified of an error.
  ~MaterializationResponsibility();

  /// Return the ResourceTracker associated with this instance.
  /// @return Resource tracker for this responsibility.
  const ResourceTrackerSP &getResourceTracker() const { return RT; }

  /// Run \p F under the session lock with this responsibility's ResourceKey.
  ///
  /// This is the safe way to associate resources with trackers.
  /// @param F Callback invoked with the associated ResourceKey.
  /// @return Success, or an error if the tracker is defunct.
  template <typename Func> Error withResourceKeyDo(Func &&F) const {
    return RT->withResourceKeyDo(std::forward<Func>(F));
  }

  /// Returns the target JITDylib that these symbols are being materialized
  ///        into.
  /// @return JITDylib that these symbols are being materialized into.
  JITDylib &getTargetJITDylib() const { return JD; }

  /// Returns the ExecutionSession for this instance.
  /// @return ExecutionSession that owns the target JITDylib.
  ExecutionSession &getExecutionSession() const;

  /// Return the symbol flags map for this responsibility instance.
  ///
  /// Note: The returned flags may have transient flags (Lazy, Materializing)
  /// set. These should be stripped with JITSymbolFlags::stripTransientFlags
  /// before using.
  /// @return Symbol flags map covered by this responsibility.
  const SymbolFlagsMap &getSymbols() const { return SymbolFlags; }

  /// Returns the initialization pseudo-symbol, if any. This symbol will also
  /// be present in the SymbolFlagsMap for this MaterializationResponsibility
  /// object.
  /// @return Initialization pseudo-symbol, or a null pointer if none.
  const SymbolStringPtr &getInitializerSymbol() const { return InitSymbol; }

  /// Return names of covered symbols that still have queries pending.
  ///
  /// This information can be used to return responsibility for unrequested
  /// symbols back to the JITDylib via the delegate method.
  /// @return Set of covered symbol names with pending queries.
  SymbolNameSet getRequestedSymbols() const;

  /// Notify the target JITDylib that the given symbols have been resolved.
  ///
  /// This will update the given symbols' addresses in the JITDylib, and notify
  /// any pending queries on the given symbols of their resolution. The given
  /// symbols must be ones covered by this MaterializationResponsibility
  /// instance. Individual calls to this method may resolve a subset of the
  /// symbols, but all symbols must have been resolved prior to calling emit.
  ///
  /// This method will return an error if any symbols being resolved have been
  /// moved to the error state due to the failure of a dependency. If this
  /// method returns an error then clients should log it and call
  /// failMaterialize. If no dependencies have been registered for the
  /// symbols covered by this MaterializationResponsibility then this method
  /// is guaranteed to return Error::success() and can be wrapped with cantFail.
  /// @param Symbols Map of resolved symbol names to their definitions.
  /// @return Success, or an error if any symbol is in the error state.
  Error notifyResolved(const SymbolMap &Symbols);

  /// Notify that all symbols covered by this responsibility have been emitted.
  ///
  /// The DepGroups array describes the dependencies of symbols being emitted on
  /// symbols that are outside this MaterializationResponsibility object. Each
  /// group consists of a pair of a set of symbols and a SymbolDependenceMap
  /// that describes the dependencies for the symbols in the first set. The
  /// elements of DepGroups must be non-overlapping (no symbol should appear in
  /// more than one of hte symbol sets), but do not have to be exhaustive. Any
  /// symbol in this MaterializationResponsibility object that is not covered
  /// by an entry will be treated as having no dependencies.
  ///
  /// This method will return an error if any symbols being resolved have been
  /// moved to the error state due to the failure of a dependency. If this
  /// method returns an error then clients should log it and call
  /// failMaterialize. If no dependencies have been registered for the
  /// symbols covered by this MaterializationResponsibility then this method
  /// is guaranteed to return Error::success() and can be wrapped with cantFail.
  /// @param DepGroups Dependence groups for symbols outside this responsibility.
  /// @return Success, or an error if any symbol is in the error state.
  Error notifyEmitted(ArrayRef<SymbolDependenceGroup> DepGroups);

  /// Attempt to claim responsibility for new definitions.
  ///
  /// This method can be used to claim responsibility for symbols that are added
  /// to a materialization unit during the compilation process (e.g. literal pool
  /// symbols). Symbol linkage rules are the same as for symbols that are
  /// defined up front: duplicate strong definitions will result in errors.
  /// Duplicate weak definitions will be discarded (in which case they will
  /// not be added to this responsibility instance).
  ///
  ///   This method can be used by materialization units that want to add
  /// additional symbols at materialization time (e.g. stubs, compile
  /// callbacks, metadata).
  /// @param SymbolFlags Flags for the new symbols to claim responsibility for.
  /// @return Success, or an error if strong definitions conflict.
  Error defineMaterializing(SymbolFlagsMap SymbolFlags);

  /// Notify pending queries that materialization of covered symbols failed.
  ///
  /// This will remove all symbols covered by this MaterializationResponsibility
  /// from the target JITDylib, and send an error to any queries waiting on
  /// these symbols.
  void failMaterialization();

  /// Transfer responsibility for \p MU's symbols to that materialization unit.
  ///
  /// This allows materializers to break up work based on run-time information
  /// (e.g. by introspecting which symbols have actually been looked up and
  /// materializing only those).
  /// @param MU Materialization unit that takes responsibility for its symbols.
  /// @return Success, or an error if replacement fails.
  Error replace(std::unique_ptr<MaterializationUnit> MU);

  /// Delegate responsibility for \p Symbols to a new responsibility instance.
  ///
  /// Useful for breaking up work between threads, or different kinds of
  /// materialization processes.
  /// @param Symbols Symbols to transfer to the new responsibility instance.
  /// @return New responsibility for \p Symbols, or an error on failure.
  Expected<std::unique_ptr<MaterializationResponsibility>>
  delegate(const SymbolNameSet &Symbols);

private:
  /// Create a MaterializationResponsibility for the given JITDylib and
  ///        initial symbols.
  MaterializationResponsibility(ResourceTrackerSP RT,
                                SymbolFlagsMap SymbolFlags,
                                SymbolStringPtr InitSymbol)
      : JD(RT->getJITDylib()), RT(std::move(RT)),
        SymbolFlags(std::move(SymbolFlags)), InitSymbol(std::move(InitSymbol)) {
    assert(!this->SymbolFlags.empty() && "Materializing nothing?");
  }

  JITDylib &JD;
  ResourceTrackerSP RT;
  SymbolFlagsMap SymbolFlags;
  SymbolStringPtr InitSymbol;
};

/// A materialization unit for symbol aliases. Allows existing symbols to be
/// aliased with alternate flags.
class LLVM_ABI ReExportsMaterializationUnit : public MaterializationUnit {
public:
  /// Construct a re-exports unit from \p SourceJD with the given aliases.
  ///
  /// SourceJD is allowed to be nullptr, in which case the source JITDylib is
  /// taken to be whatever JITDylib these definitions are materialized in (and
  /// MatchNonExported has no effect). This is useful for defining aliases
  /// within a JITDylib.
  ///
  /// Note: Care must be taken that no sets of aliases form a cycle, as such
  ///       a cycle will result in a deadlock when any symbol in the cycle is
  ///       resolved.
  /// @param SourceJD Source JITDylib for aliasees; nullptr means the target JD.
  /// @param SourceJDLookupFlags Lookup flags used when searching \p SourceJD.
  /// @param Aliases Map from alias names to aliasee entries.
  ReExportsMaterializationUnit(JITDylib *SourceJD,
                               JITDylibLookupFlags SourceJDLookupFlags,
                               SymbolAliasMap Aliases);

  /// Return the name of this materialization unit.
  /// @return Name of this re-exports materialization unit.
  StringRef getName() const override;

private:
  void materialize(std::unique_ptr<MaterializationResponsibility> R) override;
  void discard(const JITDylib &JD, const SymbolStringPtr &Name) override;
  static MaterializationUnit::Interface
  extractFlags(const SymbolAliasMap &Aliases);

  JITDylib *SourceJD = nullptr;
  JITDylibLookupFlags SourceJDLookupFlags;
  SymbolAliasMap Aliases;
};

/// Create a ReExportsMaterializationUnit with the given aliases.
///
/// Useful for defining symbol aliases.: E.g., given a JITDylib JD containing
/// symbols "foo" and "bar", we can define aliases "baz" (for "foo") and "qux"
/// (for "bar") with: \code{.cpp}
///   SymbolStringPtr Baz = ...;
///   SymbolStringPtr Qux = ...;
///   if (auto Err = JD.define(symbolAliases({
///       {Baz, { Foo, JITSymbolFlags::Exported }},
///       {Qux, { Bar, JITSymbolFlags::Weak }}}))
///     return Err;
/// \endcode
/// @param Aliases Map from alias names to aliasee entries.
/// @return Materialization unit that defines the given aliases.
inline std::unique_ptr<ReExportsMaterializationUnit>
symbolAliases(SymbolAliasMap Aliases) {
  return std::make_unique<ReExportsMaterializationUnit>(
      nullptr, JITDylibLookupFlags::MatchAllSymbols, std::move(Aliases));
}

/// Create a materialization unit for re-exporting symbols from another JITDylib.
///
/// SourceJD will be searched using the given JITDylibLookupFlags.
/// @param SourceJD JITDylib that provides the underlying symbol definitions.
/// @param Aliases Map from exported names to aliasee entries in \p SourceJD.
/// @param SourceJDLookupFlags Lookup flags used when searching \p SourceJD.
/// @return Materialization unit that reexports symbols from \p SourceJD.
inline std::unique_ptr<ReExportsMaterializationUnit>
reexports(JITDylib &SourceJD, SymbolAliasMap Aliases,
          JITDylibLookupFlags SourceJDLookupFlags =
              JITDylibLookupFlags::MatchExportedSymbolsOnly) {
  return std::make_unique<ReExportsMaterializationUnit>(
      &SourceJD, SourceJDLookupFlags, std::move(Aliases));
}

/// Build a SymbolAliasMap for the common case where you want to re-export
/// symbols from another JITDylib with the same linkage/flags.
/// @param SourceJD JITDylib that defines the symbols to re-export.
/// @param Symbols Set of symbol names to include in the alias map.
/// @return Alias map with matching flags, or an error if lookup fails.
LLVM_ABI Expected<SymbolAliasMap>
buildSimpleReexportsAliasMap(JITDylib &SourceJD, const SymbolNameSet &Symbols);

/// Represents the state that a symbol has reached during materialization.
enum class SymbolState : uint8_t {
  /// No symbol should be in this state.
  Invalid,
  /// Added to the symbol table, never queried.
  NeverSearched,
  /// Queried, materialization begun.
  Materializing,
  /// Assigned address, still materializing.
  Resolved,
  /// Emitted to memory, but waiting on transitive dependencies.
  Emitted,
  /// Ready and safe for clients to access.
  Ready = 0x3f
};

/// A symbol query that returns results via a callback when results are
///        ready.
///
/// makes a callback when all symbols are available.
class AsynchronousSymbolQuery {
  friend class ExecutionSession;
  /// Full lookup state that drives asynchronous symbol queries.
  friend class InProgressFullLookupState;
  friend class JITDylib;
  /// Adapter that presents AsynchronousSymbolQuery as a JITSymbolResolver.
  friend class JITSymbolResolverAdapter;
  friend class MaterializationResponsibility;

public:
  /// Create a query for the given symbols. The NotifyComplete
  /// callback will be called once all queried symbols reach the given
  /// minimum state.
  /// @param Symbols Symbols to include in this query.
  /// @param RequiredState Minimum symbol state required before completion.
  /// @param NotifyComplete Callback invoked when the query completes.
  LLVM_ABI AsynchronousSymbolQuery(const SymbolLookupSet &Symbols,
                                   SymbolState RequiredState,
                                   SymbolsResolvedCallback NotifyComplete);

  /// Notify the query that a requested symbol has reached the required state.
  /// @param Name Symbol that reached the required state.
  /// @param Sym Resolved definition for \p Name.
  LLVM_ABI void notifySymbolMetRequiredState(const SymbolStringPtr &Name,
                                             ExecutorSymbolDef Sym);

  /// Returns true if all symbols covered by this query have been
  ///        resolved.
  /// @return True if every queried symbol has met the required state.
  bool isComplete() const { return OutstandingSymbolsCount == 0; }


private:
  void handleComplete(ExecutionSession &ES);

  SymbolState getRequiredState() { return RequiredState; }

  void addQueryDependence(JITDylib &JD, SymbolStringPtr Name);

  void removeQueryDependence(JITDylib &JD, const SymbolStringPtr &Name);

  void dropSymbol(const SymbolStringPtr &Name);

  void handleFailed(Error Err);

  void detach();

  SymbolsResolvedCallback NotifyComplete;
  SymbolDependenceMap QueryRegistrations;
  SymbolMap ResolvedSymbols;
  size_t OutstandingSymbolsCount;
  SymbolState RequiredState;
};

/// Wraps state for a lookup-in-progress.
///
/// DefinitionGenerators can optionally take ownership of a LookupState object
/// to suspend a lookup-in-progress while they search for definitions.
class LookupState {
  /// Helper granting the OrcV2 C API access to LookupState internals.
  friend class OrcV2CAPIHelper;
  friend class ExecutionSession;

public:
  /// Construct an empty LookupState.
  LLVM_ABI LookupState();
  /// Move-construct a LookupState from \p Other.
  /// @param Other LookupState to move from.
  LLVM_ABI LookupState(LookupState &&Other);
  /// Move-assign from \p Other.
  /// @param Other LookupState to move from.
  /// @return Reference to this LookupState.
  LLVM_ABI LookupState &operator=(LookupState &&Other);
  /// Destroy this LookupState and abandon any suspended lookup.
  LLVM_ABI ~LookupState();

  /// Continue the lookup. This can be called by DefinitionGenerators
  /// to re-start a captured query-application operation.
  /// @param Err Error from the suspended generation step, if any.
  LLVM_ABI void continueLookup(Error Err);

private:
  LookupState(std::unique_ptr<InProgressLookupState> IPLS);

  // For C API.
  void reset(InProgressLookupState *IPLS);

  std::unique_ptr<InProgressLookupState> IPLS;
};

/// Definition generators can be attached to JITDylibs to generate new
/// definitions for otherwise unresolved symbols during lookup.
class LLVM_ABI DefinitionGenerator {
  friend class ExecutionSession;

public:
  /// Destroy this DefinitionGenerator.
  virtual ~DefinitionGenerator();

  /// Try to generate definitions for unresolved symbols in \p LookupSet.
  ///
  /// DefinitionGenerators should override this method to insert new
  /// definitions into the parent JITDylib. K specifies the kind of this
  /// lookup. JD specifies the target JITDylib being searched, and
  /// JDLookupFlags specifies whether the search should match against
  /// hidden symbols. Finally, Symbols describes the set of unresolved
  /// symbols and their associated lookup flags.
  /// @param LS Lookup state that may be retained to suspend the lookup.
  /// @param K Kind of lookup being performed.
  /// @param JD Target JITDylib being searched.
  /// @param JDLookupFlags Whether hidden symbols in \p JD should match.
  /// @param LookupSet Unresolved symbols and their lookup flags.
  /// @return Success, or an error if definition generation fails.
  virtual Error tryToGenerate(LookupState &LS, LookupKind K, JITDylib &JD,
                              JITDylibLookupFlags JDLookupFlags,
                              const SymbolLookupSet &LookupSet) = 0;

private:
  std::mutex M;
  bool InUse = false;
  std::deque<LookupState> PendingLookups;
};

/// Represents a JIT'd dynamic library.
///
/// This class aims to mimic the behavior of a regular dylib or shared object,
/// but without requiring the contained program representations to be compiled
/// up-front. The JITDylib's content is defined by adding MaterializationUnits,
/// and contained MaterializationUnits will typically rely on the JITDylib's
/// links-against order to resolve external references (similar to a regular
/// dylib).
///
/// The JITDylib object is a thin wrapper that references state held by the
/// ExecutionSession. JITDylibs can be removed, clearing this underlying state
/// and leaving the JITDylib object in a defunct state. In this state the
/// JITDylib's name is guaranteed to remain accessible. If the ExecutionSession
/// is still alive then other operations are callable but will return an Error
/// or null result (depending on the API). It is illegal to call any operation
/// other than getName on a JITDylib after the ExecutionSession has been torn
/// down.
///
/// JITDylibs cannot be moved or copied. Their address is stable, and useful as
/// a key in some JIT data structures.
class JITDylib : public ThreadSafeRefCountedBase<JITDylib>,
                 public jitlink::JITLinkDylib {
  friend class AsynchronousSymbolQuery;
  friend class ExecutionSession;
  friend class Platform;
  friend class MaterializationResponsibility;
public:

  /// JITDylib is not copy-constructible.
  /// @param Other Instance that would be copied.
  JITDylib(const JITDylib &Other) = delete;
  /// JITDylib is not copy-assignable.
  /// @param Other Instance that would be copied.
  JITDylib &operator=(const JITDylib &Other) = delete;
  /// JITDylib is not move-constructible.
  /// @param Other Instance that would be moved.
  JITDylib(JITDylib &&Other) = delete;
  /// JITDylib is not move-assignable.
  /// @param Other Instance that would be moved.
  JITDylib &operator=(JITDylib &&Other) = delete;
  /// Destroy this JITDylib and release its resources.
  LLVM_ABI ~JITDylib();

  /// Get a reference to the ExecutionSession for this JITDylib.
  ///
  /// It is legal to call this method on a defunct JITDylib, however the result
  /// will only usable if the ExecutionSession is still alive. If this JITDylib
  /// is held by an error that may have torn down the JIT then the result
  /// should not be used.
  /// @return ExecutionSession that owns this JITDylib.
  ExecutionSession &getExecutionSession() const { return ES; }

  /// Dump current JITDylib state to OS.
  ///
  /// It is legal to call this method on a defunct JITDylib.
  /// @param OS Stream to receive the dump.
  LLVM_ABI void dump(raw_ostream &OS);

  /// Calls remove on all trackers currently associated with this JITDylib.
  /// Does not run static deinits.
  ///
  /// Note that removal happens outside the session lock, so new code may be
  /// added concurrently while the clear is underway, and the newly added
  /// code will *not* be cleared. Adding new code concurrently with a clear
  /// is usually a bug and should be avoided.
  ///
  /// It is illegal to call this method on a defunct JITDylib and the client
  /// is responsible for ensuring that they do not do so.
  /// @return Success, or an error if clearing fails.
  LLVM_ABI Error clear();

  /// Get the default resource tracker for this JITDylib.
  ///
  /// It is illegal to call this method on a defunct JITDylib and the client
  /// is responsible for ensuring that they do not do so.
  /// @return Default resource tracker for this JITDylib.
  LLVM_ABI ResourceTrackerSP getDefaultResourceTracker();

  /// Create a resource tracker for this JITDylib.
  ///
  /// It is illegal to call this method on a defunct JITDylib and the client
  /// is responsible for ensuring that they do not do so.
  /// @return New resource tracker associated with this JITDylib.
  LLVM_ABI ResourceTrackerSP createResourceTracker();

  /// Adds a definition generator to this JITDylib and returns a referenece to
  /// it.
  ///
  /// When JITDylibs are searched during lookup, if no existing definition of
  /// a symbol is found, then any generators that have been added are run (in
  /// the order that they were added) to potentially generate a definition.
  ///
  /// It is illegal to call this method on a defunct JITDylib and the client
  /// is responsible for ensuring that they do not do so.
  /// @param DefGenerator Generator to attach; ownership is transferred.
  /// @return Reference to the installed generator.
  template <typename GeneratorT>
  GeneratorT &addGenerator(std::unique_ptr<GeneratorT> DefGenerator);

  /// Remove a definition generator from this JITDylib.
  ///
  /// The given generator must exist in this JITDylib's generators list (i.e.
  /// have been added and not yet removed).
  ///
  /// It is illegal to call this method on a defunct JITDylib and the client
  /// is responsible for ensuring that they do not do so.
  /// @param G Generator previously added with addGenerator.
  LLVM_ABI void removeGenerator(DefinitionGenerator &G);

  /// Set the link order used when fixing up definitions in this JITDylib.
  ///
  /// This will replace the previous link order, and apply to any symbol
  /// resolutions made for definitions in this JITDylib after the call to
  /// setLinkOrder (even if the definition itself was added before the
  /// call).
  ///
  /// If LinkAgainstThisJITDylibFirst is true (the default) then this JITDylib
  /// will add itself to the beginning of the LinkOrder (Clients should not
  /// put this JITDylib in the list in this case, to avoid redundant lookups).
  ///
  /// If LinkAgainstThisJITDylibFirst is false then the link order will be used
  /// as-is. The primary motivation for this feature is to support deliberate
  /// shadowing of symbols in this JITDylib by a facade JITDylib. For example,
  /// the facade may resolve function names to stubs, and the stubs may compile
  /// lazily by looking up symbols in this dylib. Adding the facade dylib
  /// as the first in the link order (instead of this dylib) ensures that
  /// definitions within this dylib resolve to the lazy-compiling stubs,
  /// rather than immediately materializing the definitions in this dylib.
  ///
  /// It is illegal to call this method on a defunct JITDylib and the client
  /// is responsible for ensuring that they do not do so.
  /// @param NewSearchOrder Replacement search order for this JITDylib.
  /// @param LinkAgainstThisJITDylibFirst If true, prepend this JITDylib.
  LLVM_ABI void setLinkOrder(JITDylibSearchOrder NewSearchOrder,
                             bool LinkAgainstThisJITDylibFirst = true);

  /// Append the given JITDylibSearchOrder to the link order for this
  /// JITDylib (discarding any elements already present in this JITDylib's
  /// link order).
  /// @param NewLinks Search-order entries to append.
  LLVM_ABI void addToLinkOrder(const JITDylibSearchOrder &NewLinks);

  /// Add the given JITDylib to the link order for definitions in this
  /// JITDylib.
  ///
  /// It is illegal to call this method on a defunct JITDylib and the client
  /// is responsible for ensuring that they do not do so.
  /// @param JD JITDylib to append to the link order.
  /// @param JDLookupFlags Lookup flags to use when searching \p JD.
  LLVM_ABI void
  addToLinkOrder(JITDylib &JD,
                 JITDylibLookupFlags JDLookupFlags =
                     JITDylibLookupFlags::MatchExportedSymbolsOnly);

  /// Replace OldJD with NewJD in the link order if OldJD is present.
  /// Otherwise this operation is a no-op.
  ///
  /// It is illegal to call this method on a defunct JITDylib and the client
  /// is responsible for ensuring that they do not do so.
  /// @param OldJD JITDylib entry to replace.
  /// @param NewJD JITDylib that replaces \p OldJD.
  /// @param JDLookupFlags Lookup flags to use for \p NewJD.
  LLVM_ABI void
  replaceInLinkOrder(JITDylib &OldJD, JITDylib &NewJD,
                     JITDylibLookupFlags JDLookupFlags =
                         JITDylibLookupFlags::MatchExportedSymbolsOnly);

  /// Remove the given JITDylib from the link order for this JITDylib if it is
  /// present. Otherwise this operation is a no-op.
  ///
  /// It is illegal to call this method on a defunct JITDylib and the client
  /// is responsible for ensuring that they do not do so.
  /// @param JD JITDylib to remove from the link order.
  LLVM_ABI void removeFromLinkOrder(JITDylib &JD);

  /// Do something with the link order (run under the session lock).
  ///
  /// It is illegal to call this method on a defunct JITDylib and the client
  /// is responsible for ensuring that they do not do so.
  /// @param F Callback invoked with the current link order.
  /// @return Result of invoking \p F with the current link order.
  template <typename Func>
  auto withLinkOrderDo(Func &&F)
      -> decltype(F(std::declval<const JITDylibSearchOrder &>()));

  /// Define all symbols provided by the materialization unit to be part of this
  /// JITDylib.
  ///
  /// If RT is not specified then the default resource tracker will be used.
  ///
  /// This overload always takes ownership of the MaterializationUnit. If any
  /// errors occur, the MaterializationUnit consumed.
  ///
  /// It is illegal to call this method on a defunct JITDylib and the client
  /// is responsible for ensuring that they do not do so.
  /// @param MU Materialization unit whose symbols are defined in this JITDylib.
  /// @param RT Resource tracker for the new definitions; default if null.
  /// @return Success, or an error if definition fails (\p MU is consumed).
  template <typename MaterializationUnitType>
  Error define(std::unique_ptr<MaterializationUnitType> &&MU,
               ResourceTrackerSP RT = nullptr);

  /// Define all symbols provided by the materialization unit to be part of this
  /// JITDylib.
  ///
  /// This overload only takes ownership of the MaterializationUnit no error is
  /// generated. If an error occurs, ownership remains with the caller. This
  /// may allow the caller to modify the MaterializationUnit to correct the
  /// issue, then re-call define.
  ///
  /// It is illegal to call this method on a defunct JITDylib and the client
  /// is responsible for ensuring that they do not do so.
  /// @param MU Materialization unit whose symbols are defined in this JITDylib.
  /// @param RT Resource tracker for the new definitions; default if null.
  /// @return Success, or an error if definition fails (\p MU retained on error).
  template <typename MaterializationUnitType>
  Error define(std::unique_ptr<MaterializationUnitType> &MU,
               ResourceTrackerSP RT = nullptr);

  /// Tries to remove the given symbols.
  ///
  /// If any symbols are not defined in this JITDylib this method will return
  /// a SymbolsNotFound error covering the missing symbols.
  ///
  /// If all symbols are found but some symbols are in the process of being
  /// materialized this method will return a SymbolsCouldNotBeRemoved error.
  ///
  /// On success, all symbols are removed. On failure, the JITDylib state is
  /// left unmodified (no symbols are removed).
  ///
  /// It is illegal to call this method on a defunct JITDylib and the client
  /// is responsible for ensuring that they do not do so.
  /// @param Names Symbols to remove from this JITDylib.
  /// @return Success, or an error if symbols are missing or still materializing.
  LLVM_ABI Error remove(const SymbolNameSet &Names);

  /// Returns the given JITDylibs and all of their transitive dependencies in
  /// DFS order (based on linkage relationships). Each JITDylib will appear
  /// only once.
  ///
  /// If any JITDylib in the order is defunct then this method will return an
  /// error, otherwise returns the order.
  /// @param JDs Root JITDylibs to include in the traversal.
  /// @return DFS link order, or an error if any JITDylib is defunct.
  LLVM_ABI static Expected<std::vector<JITDylibSP>>
  getDFSLinkOrder(ArrayRef<JITDylibSP> JDs);

  /// Returns the given JITDylibs and all of their transitive dependencies in
  /// reverse DFS order (based on linkage relationships). Each JITDylib will
  /// appear only once.
  ///
  /// If any JITDylib in the order is defunct then this method will return an
  /// error, otherwise returns the order.
  /// @param JDs Root JITDylibs to include in the traversal.
  /// @return Reverse DFS link order, or an error if any JITDylib is defunct.
  LLVM_ABI static Expected<std::vector<JITDylibSP>>
  getReverseDFSLinkOrder(ArrayRef<JITDylibSP> JDs);

  /// Return this JITDylib and its transitive dependencies in DFS order
  /// based on linkage relationships.
  ///
  /// If any JITDylib in the order is defunct then this method will return an
  /// error, otherwise returns the order.
  /// @return DFS link order from this JITDylib, or an error if any is defunct.
  LLVM_ABI Expected<std::vector<JITDylibSP>> getDFSLinkOrder();

  /// Rteurn this JITDylib and its transitive dependencies in reverse DFS order
  /// based on linkage relationships.
  ///
  /// If any JITDylib in the order is defunct then this method will return an
  /// error, otherwise returns the order.
  /// @return Reverse DFS link order from this JITDylib, or an error if defunct.
  LLVM_ABI Expected<std::vector<JITDylibSP>> getReverseDFSLinkOrder();

private:
  using AsynchronousSymbolQuerySet =
    std::set<std::shared_ptr<AsynchronousSymbolQuery>>;

  using AsynchronousSymbolQueryList =
      std::vector<std::shared_ptr<AsynchronousSymbolQuery>>;

  struct UnmaterializedInfo {
    UnmaterializedInfo(std::unique_ptr<MaterializationUnit> MU,
                       ResourceTracker *RT)
        : MU(std::move(MU)), RT(RT) {}

    std::unique_ptr<MaterializationUnit> MU;
    ResourceTracker *RT;
  };

  using UnmaterializedInfosMap =
      DenseMap<SymbolStringPtr, std::shared_ptr<UnmaterializedInfo>>;

  using UnmaterializedInfosList =
      std::vector<std::shared_ptr<UnmaterializedInfo>>;

  // Information about not-yet-ready symbol.
  // * DefiningEDU will point to the EmissionDepUnit that defines the symbol.
  // * DependantEDUs will hold pointers to any EmissionDepUnits currently
  //   waiting on this symbol.
  // * Pending queries holds any not-yet-completed queries that include this
  //   symbol.
  struct MaterializingInfo {
    friend class ExecutionSession;

    LLVM_ABI void addQuery(std::shared_ptr<AsynchronousSymbolQuery> Q);
    LLVM_ABI void removeQuery(const AsynchronousSymbolQuery &Q);
    LLVM_ABI AsynchronousSymbolQueryList
    takeQueriesMeeting(SymbolState RequiredState);
    AsynchronousSymbolQueryList takeAllPendingQueries() {
      return std::move(PendingQueries);
    }
    bool hasQueriesPending() const { return !PendingQueries.empty(); }
    const AsynchronousSymbolQueryList &pendingQueries() const {
      return PendingQueries;
    }
  private:
    AsynchronousSymbolQueryList PendingQueries;
  };

  using MaterializingInfosMap = DenseMap<SymbolStringPtr, MaterializingInfo>;

  class SymbolTableEntry {
  public:
    SymbolTableEntry() = default;
    SymbolTableEntry(JITSymbolFlags Flags)
        : Flags(Flags), State(static_cast<uint8_t>(SymbolState::NeverSearched)),
          MaterializerAttached(false) {}

    ExecutorAddr getAddress() const { return Addr; }
    JITSymbolFlags getFlags() const { return Flags; }
    SymbolState getState() const { return static_cast<SymbolState>(State); }

    bool hasMaterializerAttached() const { return MaterializerAttached; }

    void setAddress(ExecutorAddr Addr) { this->Addr = Addr; }
    void setFlags(JITSymbolFlags Flags) { this->Flags = Flags; }
    void setState(SymbolState State) {
      assert(static_cast<uint8_t>(State) < (1 << 6) &&
             "State does not fit in bitfield");
      this->State = static_cast<uint8_t>(State);
    }

    void setMaterializerAttached(bool MaterializerAttached) {
      this->MaterializerAttached = MaterializerAttached;
    }

    ExecutorSymbolDef getSymbol() const { return {Addr, Flags}; }

  private:
    ExecutorAddr Addr;
    JITSymbolFlags Flags;
    uint8_t State : 7;
    uint8_t MaterializerAttached : 1;
  };

  using SymbolTable = DenseMap<SymbolStringPtr, SymbolTableEntry>;

  JITDylib(ExecutionSession &ES, std::string Name);

  struct RemoveTrackerResult {
    AsynchronousSymbolQuerySet QueriesToFail;
    std::shared_ptr<SymbolDependenceMap> FailedSymbols;
    std::vector<std::unique_ptr<MaterializationUnit>> DefunctMUs;
  };

  RemoveTrackerResult IL_removeTracker(ResourceTracker &RT);

  void transferTracker(ResourceTracker &DstRT, ResourceTracker &SrcRT);

  LLVM_ABI Error defineImpl(MaterializationUnit &MU);

  LLVM_ABI void
  installMaterializationUnit(std::unique_ptr<MaterializationUnit> MU,
                             ResourceTracker &RT);

  void detachQueryHelper(AsynchronousSymbolQuery &Q,
                         const SymbolNameSet &QuerySymbols);

  void transferEmittedNodeDependencies(MaterializingInfo &DependantMI,
                                       const SymbolStringPtr &DependantName,
                                       MaterializingInfo &EmittedMI);

  Expected<SymbolFlagsMap>
  defineMaterializing(MaterializationResponsibility &FromMR,
                      SymbolFlagsMap SymbolFlags);

  Error replace(MaterializationResponsibility &FromMR,
                std::unique_ptr<MaterializationUnit> MU);

  Expected<std::unique_ptr<MaterializationResponsibility>>
  delegate(MaterializationResponsibility &FromMR, SymbolFlagsMap SymbolFlags,
           SymbolStringPtr InitSymbol);

  SymbolNameSet getRequestedSymbols(const SymbolFlagsMap &SymbolFlags) const;

  void addDependencies(const SymbolStringPtr &Name,
                       const SymbolDependenceMap &Dependants);

  Error resolve(MaterializationResponsibility &MR, const SymbolMap &Resolved);

  void unlinkMaterializationResponsibility(MaterializationResponsibility &MR);

  /// Attempt to reduce memory usage from empty \c UnmaterializedInfos and
  /// \c MaterializingInfos tables.
  void shrinkMaterializationInfoMemory();

  ExecutionSession &ES;
  enum { Open, Closing, Closed } State = Open;
  std::mutex GeneratorsMutex;
  SymbolTable Symbols;
  UnmaterializedInfosMap UnmaterializedInfos;
  MaterializingInfosMap MaterializingInfos;
  std::vector<std::shared_ptr<DefinitionGenerator>> DefGenerators;
  JITDylibSearchOrder LinkOrder;
  ResourceTrackerSP DefaultTracker;

  // Map trackers to sets of symbols tracked.
  DenseMap<ResourceTracker *, SymbolNameVector> TrackerSymbols;
  DenseMap<ResourceTracker *, DenseSet<MaterializationResponsibility *>>
      TrackerMRs;
};

/// Platforms set up standard symbols and mediate dynamic initializer state.
///
/// Platforms set up standard symbols and mediate interactions between dynamic
/// initializers (e.g. C++ static constructors) and ExecutionSession state.
/// Note that Platforms do not automatically run initializers: clients are still
/// responsible for doing this.
class LLVM_ABI Platform {
public:
  /// Destroy this Platform.
  virtual ~Platform();

  /// Install JITDylib-specific standard symbols when a JITDylib is created.
  ///
  /// This method will be called outside the session lock each time a JITDylib
  /// is created (unless it is created with EmptyJITDylib set) to allow the
  /// Platform to install any JITDylib specific standard symbols (e.g
  /// __dso_handle).
  /// @param JD Newly created JITDylib to configure.
  /// @return Success, or an error if platform setup fails.
  virtual Error setupJITDylib(JITDylib &JD) = 0;

  /// This method will be called outside the session lock each time a JITDylib
  /// is removed to allow the Platform to remove any JITDylib-specific data.
  /// @param JD JITDylib being torn down.
  /// @return Success, or an error if platform teardown fails.
  virtual Error teardownJITDylib(JITDylib &JD) = 0;

  /// This method will be called under the ExecutionSession lock each time a
  /// MaterializationUnit is added to a JITDylib.
  /// @param RT Resource tracker associated with the added unit.
  /// @param MU Materialization unit being added.
  /// @return Success, or an error if the platform rejects the unit.
  virtual Error notifyAdding(ResourceTracker &RT,
                             const MaterializationUnit &MU) = 0;

  /// This method will be called under the ExecutionSession lock when a
  /// ResourceTracker is removed.
  /// @param RT Resource tracker being removed.
  /// @return Success, or an error if the platform cannot handle removal.
  virtual Error notifyRemoving(ResourceTracker &RT) = 0;

  /// A utility function for looking up initializer symbols. Performs a blocking
  /// lookup for the given symbols in each of the given JITDylibs.
  ///
  /// Note: This function is deprecated and will be removed in the near future.
  /// @param ES Execution session that performs the lookup.
  /// @param InitSyms Map from JITDylib to initializer symbols to look up.
  /// @return Per-JITDylib map of resolved initializer symbols, or an error.
  static Expected<DenseMap<JITDylib *, SymbolMap>>
  lookupInitSymbols(ExecutionSession &ES,
                    const DenseMap<JITDylib *, SymbolLookupSet> &InitSyms);

  /// Performs an async lookup for the given symbols in each of the given
  /// JITDylibs, calling the given handler once all lookups have completed.
  /// @param OnComplete Callback invoked when all initializer lookups finish.
  /// @param ES Execution session that performs the lookup.
  /// @param InitSyms Map from JITDylib to initializer symbols to look up.
  static void
  lookupInitSymbolsAsync(unique_function<void(Error)> OnComplete,
                         ExecutionSession &ES,
                         const DenseMap<JITDylib *, SymbolLookupSet> &InitSyms);
};

/// A materialization task.
class LLVM_ABI MaterializationTask
    : public RTTIExtends<MaterializationTask, Task> {
public:
  /// RTTI identifier for MaterializationTask.
  static char ID;

  /// Construct a task that materializes \p MU under responsibility \p MR.
  /// @param MU Materialization unit to materialize.
  /// @param MR Responsibility covering the symbols being materialized.
  MaterializationTask(std::unique_ptr<MaterializationUnit> MU,
                      std::unique_ptr<MaterializationResponsibility> MR)
      : MU(std::move(MU)), MR(std::move(MR)) {}
  /// Destroy this materialization task.
  ~MaterializationTask() override;
  /// Print a description of this task to \p OS.
  /// @param OS Stream to receive the description.
  void printDescription(raw_ostream &OS) override;
  /// Run materialization for the associated unit and responsibility.
  void run() override;

private:
  std::unique_ptr<MaterializationUnit> MU;
  std::unique_ptr<MaterializationResponsibility> MR;
};

/// Lookups are usually run on the current thread, but in some cases they may
/// be run as tasks, e.g. if the lookup has been continued from a suspended
/// state.
class LLVM_ABI LookupTask : public RTTIExtends<LookupTask, Task> {
public:
  /// RTTI identifier for LookupTask.
  static char ID;

  /// Construct a task that continues lookup state \p LS.
  /// @param LS Suspended lookup state to resume.
  LookupTask(LookupState LS) : LS(std::move(LS)) {}
  /// Print a description of this task to \p OS.
  /// @param OS Stream to receive the description.
  void printDescription(raw_ostream &OS) override;
  /// Continue the suspended lookup represented by this task.
  void run() override;

private:
  LookupState LS;
};

/// An ExecutionSession represents a running JIT program.
class ExecutionSession {
  /// Lookup-flags state used while resolving symbol flags asynchronously.
  friend class InProgressLookupFlagsState;
  /// Full lookup state used while resolving symbols asynchronously.
  friend class InProgressFullLookupState;
  friend class JITDylib;
  friend class LookupState;
  friend class MaterializationResponsibility;
  friend class ResourceTracker;

public:
  /// For reporting errors.
  using ErrorReporter = unique_function<void(Error)>;

  /// Send a result to the remote.
  using SendResultFunction = unique_function<void(shared::WrapperFunctionBuffer)>;

  /// An asynchronous wrapper-function callable from the executor via
  /// jit-dispatch.
  using JITDispatchHandlerFunction = unique_function<void(
      SendResultFunction SendResult,
      const char *ArgData, size_t ArgSize)>;

  /// A map associating tag names with asynchronous wrapper function
  /// implementations in the JIT.
  using JITDispatchHandlerAssociationMap =
      DenseMap<SymbolStringPtr, JITDispatchHandlerFunction>;

  /// Construct an ExecutionSession with the given ExecutorProcessControl
  /// object.
  /// @param EPC Process-control object for the executor; ownership is taken.
  LLVM_ABI ExecutionSession(std::unique_ptr<ExecutorProcessControl> EPC);

  /// ExecutionSession is not copy-constructible.
  /// @param Other Instance that would be copied.
  ExecutionSession(const ExecutionSession &Other) = delete;
  /// ExecutionSession is not copy-assignable.
  /// @param Other Instance that would be copied.
  ExecutionSession &operator=(const ExecutionSession &Other) = delete;
  /// ExecutionSession is not move-constructible.
  /// @param Other Instance that would be moved.
  ExecutionSession(ExecutionSession &&Other) = delete;
  /// ExecutionSession is not move-assignable.
  /// @param Other Instance that would be moved.
  ExecutionSession &operator=(ExecutionSession &&Other) = delete;

  /// Destroy an ExecutionSession. Verifies that endSession was called prior to
  /// destruction.
  LLVM_ABI ~ExecutionSession();

  /// End the session. Closes all JITDylibs and disconnects from the
  /// executor. Clients must call this method before destroying the session.
  /// @return Success, or an error if teardown or disconnect fails.
  LLVM_ABI Error endSession();

  /// Get the ExecutorProcessControl object associated with this
  /// ExecutionSession.
  /// @return ExecutorProcessControl for this session.
  ExecutorProcessControl &getExecutorProcessControl() { return *EPC; }

  /// Return the triple for the executor.
  /// @return Target triple of the executor process.
  const Triple &getTargetTriple() const { return EPC->getTargetTriple(); }

  /// Return the page size for the executor.
  /// @return Page size of the executor process.
  size_t getPageSize() const { return EPC->getPageSize(); }

  /// Get the SymbolStringPool for this instance.
  /// @return Shared pointer to this session's symbol string pool.
  std::shared_ptr<SymbolStringPool> getSymbolStringPool() {
    return EPC->getSymbolStringPool();
  }

  /// Add a symbol name to the SymbolStringPool and return a pointer to it.
  /// @param SymName Symbol name to intern.
  /// @return Owning pointer to the interned symbol name.
  SymbolStringPtr intern(StringRef SymName) { return EPC->intern(SymName); }

  /// Returns a reference to the bootstrap JITDylib.
  ///
  /// This is a bare JITDylib that is created for each ExecutionSession and
  /// populated with the bootstrap symbol definitions provided by the
  /// ExecutorProcessControl object.
  /// @return Bootstrap JITDylib for this session.
  JITDylib &getBootstrapJITDylib() { return BootstrapJD; }

  /// Set a WaitingOnGraph::Recorder to capture WaitingOnGraph operations.
  ///
  /// This method can be called at most once. If called, it should be called
  /// before any symbols are materialized.
  /// @param R Recorder that receives WaitingOnGraph operations.
  void setWaitingOnGraphOpRecorder(WaitingOnGraph::OpRecorder &R) {
    assert(!GOpRecorder && "WaitingOnGraph recorder already set");
    GOpRecorder = &R;
  }

  /// Set the Platform for this ExecutionSession.
  /// @param P Platform instance to install; ownership is taken.
  void setPlatform(std::unique_ptr<Platform> P) { this->P = std::move(P); }

  /// Get the Platform for this session.
  /// Will return null if no Platform has been set for this ExecutionSession.
  /// @return Platform for this session, or null if none is set.
  Platform *getPlatform() { return P.get(); }

  /// Run the given lambda with the session mutex locked.
  /// @param F Callback invoked while the session mutex is held.
  /// @return Result of invoking \p F under the session lock.
  template <typename Func> decltype(auto) runSessionLocked(Func &&F) {
    std::lock_guard<std::recursive_mutex> Lock(SessionMutex);
    return F();
  }

  /// Register the given ResourceManager with this ExecutionSession.
  /// Managers will be notified of events in reverse order of registration.
  /// @param RM Resource manager to register.
  LLVM_ABI void registerResourceManager(ResourceManager &RM);

  /// Deregister the given ResourceManager with this ExecutionSession.
  /// Manager must have been previously registered.
  /// @param RM Resource manager to deregister.
  LLVM_ABI void deregisterResourceManager(ResourceManager &RM);

  /// Return a pointer to the "name" JITDylib.
  /// Ownership of JITDylib remains within Execution Session
  /// @param Name Name of the JITDylib to look up.
  /// @return Pointer to the named JITDylib, or null if not found.
  LLVM_ABI JITDylib *getJITDylibByName(StringRef Name);

  /// Add a new bare JITDylib to this ExecutionSession.
  ///
  /// The JITDylib Name is required to be unique. Clients should verify that
  /// names are not being re-used (E.g. by calling getJITDylibByName) if names
  /// are based on user input.
  ///
  /// This call does not install any library code or symbols into the newly
  /// created JITDylib. The client is responsible for all configuration.
  /// @param Name Unique name for the new bare JITDylib.
  /// @return Reference to the newly created bare JITDylib.
  LLVM_ABI JITDylib &createBareJITDylib(std::string Name);

  /// Add a new JITDylib to this ExecutionSession.
  ///
  /// The JITDylib Name is required to be unique. Clients should verify that
  /// names are not being re-used (e.g. by calling getJITDylibByName) if names
  /// are based on user input.
  ///
  /// If a Platform is attached then Platform::setupJITDylib will be called to
  /// install standard platform symbols (e.g. standard library interposes).
  /// If no Platform is attached this call is equivalent to createBareJITDylib.
  /// @param Name Unique name for the new JITDylib.
  /// @return Reference to the new JITDylib, or an error if setup fails.
  LLVM_ABI Expected<JITDylib &> createJITDylib(std::string Name);

  /// Removes the given JITDylibs from the ExecutionSession.
  ///
  /// This method clears all resources held for the JITDylibs, puts them in the
  /// closed state, and clears all references to them that are held by the
  /// ExecutionSession or other JITDylibs. No further code can be added to the
  /// removed JITDylibs, and the JITDylib objects will be freed once any
  /// remaining JITDylibSPs pointing to them are destroyed.
  ///
  /// This method does *not* run static destructors for code contained in the
  /// JITDylibs, and each JITDylib can only be removed once.
  ///
  /// JITDylibs will be removed in the order given. Teardown is usually
  /// independent for each JITDylib, but not always. In particular, where the
  /// ORC runtime is used it is expected that teardown off all JITDylibs will
  /// depend on it, so the JITDylib containing the ORC runtime must be removed
  /// last. If the client has introduced any other dependencies they should be
  /// accounted for in the removal order too.
  /// @param JDsToRemove JITDylibs to remove, in teardown order.
  /// @return Success, or an error if removal fails.
  LLVM_ABI Error removeJITDylibs(std::vector<JITDylibSP> JDsToRemove);

  /// Calls removeJTIDylibs on the gives JITDylib.
  /// @param JD JITDylib to remove.
  /// @return Success, or an error if removal fails.
  Error removeJITDylib(JITDylib &JD) {
    return removeJITDylibs(std::vector<JITDylibSP>({&JD}));
  }

  /// Set the error reporter function.
  /// @param ReportError Callback used to report session errors.
  /// @return Reference to this ExecutionSession.
  ExecutionSession &setErrorReporter(ErrorReporter ReportError) {
    this->ReportError = std::move(ReportError);
    return *this;
  }

  /// Report a error for this execution session.
  ///
  /// Unhandled errors can be sent here to log them.
  /// @param Err Error to report.
  void reportError(Error Err) { ReportError(std::move(Err)); }

  /// Search the given JITDylibs to find the flags associated with each of the
  /// given symbols.
  /// @param K Kind of lookup being performed.
  /// @param SearchOrder JITDylibs to search, with per-dylib lookup flags.
  /// @param Symbols Symbols whose flags should be looked up.
  /// @param OnComplete Callback invoked with the resulting flags map.
  LLVM_ABI void
  lookupFlags(LookupKind K, JITDylibSearchOrder SearchOrder,
              SymbolLookupSet Symbols,
              unique_function<void(Expected<SymbolFlagsMap>)> OnComplete);

  /// Blocking version of lookupFlags.
  /// @param K Kind of lookup being performed.
  /// @param SearchOrder JITDylibs to search, with per-dylib lookup flags.
  /// @param Symbols Symbols whose flags should be looked up.
  /// @return Flags map for the requested symbols, or an error on failure.
  LLVM_ABI Expected<SymbolFlagsMap> lookupFlags(LookupKind K,
                                                JITDylibSearchOrder SearchOrder,
                                                SymbolLookupSet Symbols);

  /// Search the given JITDylibs for the given symbols.
  ///
  /// SearchOrder lists the JITDylibs to search. For each dylib, the associated
  /// boolean indicates whether the search should match against non-exported
  /// (hidden visibility) symbols in that dylib (true means match against
  /// non-exported symbols, false means do not match).
  ///
  /// The NotifyComplete callback will be called once all requested symbols
  /// reach the required state.
  ///
  /// If all symbols are found, the RegisterDependencies function will be called
  /// while the session lock is held. This gives clients a chance to register
  /// dependencies for on the queried symbols for any symbols they are
  /// materializing (if a MaterializationResponsibility instance is present,
  /// this can be implemented by calling
  /// MaterializationResponsibility::addDependencies). If there are no
  /// dependenant symbols for this query (e.g. it is being made by a top level
  /// client to get an address to call) then the value NoDependenciesToRegister
  /// can be used.
  /// @param K Kind of lookup being performed.
  /// @param SearchOrder JITDylibs to search, with per-dylib lookup flags.
  /// @param Symbols Symbols to look up.
  /// @param RequiredState Minimum symbol state required before completion.
  /// @param NotifyComplete Callback invoked when the lookup completes.
  /// @param RegisterDependencies Callback to register query dependencies.
  LLVM_ABI void lookup(LookupKind K, const JITDylibSearchOrder &SearchOrder,
                       SymbolLookupSet Symbols, SymbolState RequiredState,
                       SymbolsResolvedCallback NotifyComplete,
                       RegisterDependenciesFunction RegisterDependencies);

  /// Blocking version of asynchronous lookup; returns the resolved symbol map.
  ///
  /// If WaitUntilReady is true (the default), will not return until all
  /// requested symbols are ready (or an error occurs). If WaitUntilReady is
  /// false, will return as soon as all requested symbols are resolved,
  /// or an error occurs. If WaitUntilReady is false and an error occurs
  /// after resolution, the function will return a success value, but the
  /// error will be reported via reportErrors.
  /// @param SearchOrder JITDylibs to search, with per-dylib lookup flags.
  /// @param Symbols Symbols to look up.
  /// @param K Kind of lookup being performed.
  /// @param RequiredState Minimum symbol state required before return.
  /// @param RegisterDependencies Callback to register query dependencies.
  /// @return Resolved symbol map, or an error if lookup fails.
  LLVM_ABI Expected<SymbolMap>
  lookup(const JITDylibSearchOrder &SearchOrder, SymbolLookupSet Symbols,
         LookupKind K = LookupKind::Static,
         SymbolState RequiredState = SymbolState::Ready,
         RegisterDependenciesFunction RegisterDependencies =
             NoDependenciesToRegister);

  /// Convenience blocking lookup for a single symbol in \p SearchOrder.
  ///
  /// Searches each of the JITDylibs in the search order in turn for the given
  /// symbol.
  /// @param SearchOrder JITDylibs to search, with per-dylib lookup flags.
  /// @param Symbol Symbol to look up.
  /// @param RequiredState Minimum symbol state required before return.
  /// @return Resolved symbol definition, or an error if lookup fails.
  LLVM_ABI Expected<ExecutorSymbolDef>
  lookup(const JITDylibSearchOrder &SearchOrder, SymbolStringPtr Symbol,
         SymbolState RequiredState = SymbolState::Ready);

  /// Convenience blocking lookup for a single exported symbol in \p SearchOrder.
  ///
  /// Searches each of the JITDylibs in the search order in turn for the given
  /// symbol. The search will not find non-exported symbols.
  /// @param SearchOrder JITDylibs to search for exported symbols only.
  /// @param Symbol Symbol to look up.
  /// @param RequiredState Minimum symbol state required before return.
  /// @return Resolved exported symbol definition, or an error if lookup fails.
  LLVM_ABI Expected<ExecutorSymbolDef>
  lookup(ArrayRef<JITDylib *> SearchOrder, SymbolStringPtr Symbol,
         SymbolState RequiredState = SymbolState::Ready);

  /// Convenience blocking lookup for a single exported symbol by name string.
  ///
  /// Searches each of the JITDylibs in the search order in turn for the given
  /// symbol. The search will not find non-exported symbols.
  /// @param SearchOrder JITDylibs to search for exported symbols only.
  /// @param Symbol Symbol name to look up.
  /// @param RequiredState Minimum symbol state required before return.
  /// @return Resolved exported symbol definition, or an error if lookup fails.
  LLVM_ABI Expected<ExecutorSymbolDef>
  lookup(ArrayRef<JITDylib *> SearchOrder, StringRef Symbol,
         SymbolState RequiredState = SymbolState::Ready);

  /// Materialize the given unit.
  /// @param T Task to dispatch on the session's task dispatcher.
  void dispatchTask(std::unique_ptr<Task> T) {
    assert(T && "T must be non-null");
    DEBUG_WITH_TYPE("orc", dumpDispatchInfo(*T));
    EPC->getDispatcher().dispatch(std::move(T));
  }

  /// Returns the bootstrap map.
  /// @return Bootstrap map of serialized values from the executor.
  const StringMap<std::vector<char>> &getBootstrapMap() const {
    return EPC->getBootstrapMap();
  }

  /// Look up and SPS-deserialize a bootstrap map value.
  /// @param Key Bootstrap map key to look up.
  /// @param Val Optional set to the deserialized value when found.
  /// @return Success, or an error if lookup or deserialization fails.
  template <typename T, typename SPSTagT>
  Error getBootstrapMapValue(StringRef Key, std::optional<T> &Val) const {
    return EPC->getBootstrapMapValue<T, SPSTagT>(Key, Val);
  }

  /// Returns the bootstrap symbol map.
  /// @return Map from bootstrap symbol names to executor addresses.
  const StringMap<ExecutorAddr> &getBootstrapSymbolsMap() const {
    return EPC->getBootstrapSymbolsMap();
  }

  /// Look up bootstrap symbol addresses for each name in \p Pairs.
  ///
  /// For each (ExecutorAddr&, StringRef) pair, looks up the string in the
  /// bootstrap symbols map and writes its address to the ExecutorAddr if
  /// found. If any symbol is not found then the function returns an error.
  /// @param Pairs Address/name pairs to fill from the bootstrap symbol map.
  /// @return Success, or an error if any bootstrap symbol is missing.
  Error getBootstrapSymbols(
      ArrayRef<std::pair<ExecutorAddr &, StringRef>> Pairs) const {
    return EPC->getBootstrapSymbols(Pairs);
  }

  /// Run a wrapper function in the executor. The given WFRHandler will be
  /// called on the result when it is returned.
  ///
  /// The wrapper function should be callable as:
  ///
  /// \code{.cpp}
  ///   CWrapperFunctionBuffer fn(uint8_t *Data, uint64_t Size);
  /// \endcode{.cpp}
  /// @param WrapperFnAddr Address of the wrapper function in the executor.
  /// @param OnComplete Handler invoked with the wrapper function result.
  /// @param ArgBuffer Serialized argument bytes for the wrapper call.
  void callWrapperAsync(ExecutorAddr WrapperFnAddr,
                        ExecutorProcessControl::IncomingWFRHandler OnComplete,
                        ArrayRef<char> ArgBuffer) {
    EPC->callWrapperAsync(WrapperFnAddr, std::move(OnComplete), ArgBuffer);
  }

  /// Run a wrapper function in the executor using the given Runner to dispatch
  /// OnComplete when the result is ready.
  /// @param Runner Policy used to dispatch \p OnComplete.
  /// @param WrapperFnAddr Address of the wrapper function in the executor.
  /// @param OnComplete Callback invoked with the wrapper function result.
  /// @param ArgBuffer Serialized argument bytes for the wrapper call.
  template <typename RunPolicyT, typename FnT>
  void callWrapperAsync(RunPolicyT &&Runner, ExecutorAddr WrapperFnAddr,
                        FnT &&OnComplete, ArrayRef<char> ArgBuffer) {
    EPC->callWrapperAsync(std::forward<RunPolicyT>(Runner), WrapperFnAddr,
                          std::forward<FnT>(OnComplete), ArgBuffer);
  }

  /// Run a wrapper function in the executor. OnComplete will be dispatched
  /// as a GenericNamedTask using this instance's TaskDispatch object.
  /// @param WrapperFnAddr Address of the wrapper function in the executor.
  /// @param OnComplete Callback invoked with the wrapper function result.
  /// @param ArgBuffer Serialized argument bytes for the wrapper call.
  template <typename FnT>
  void callWrapperAsync(ExecutorAddr WrapperFnAddr, FnT &&OnComplete,
                        ArrayRef<char> ArgBuffer) {
    EPC->callWrapperAsync(WrapperFnAddr, std::forward<FnT>(OnComplete),
                          ArgBuffer);
  }

  /// Run a wrapper function in the executor. The wrapper function should be
  /// callable as:
  ///
  /// \code{.cpp}
  ///   CWrapperFunctionBuffer fn(uint8_t *Data, uint64_t Size);
  /// \endcode{.cpp}
  /// @param WrapperFnAddr Address of the wrapper function in the executor.
  /// @param ArgBuffer Serialized argument bytes for the wrapper call.
  /// @return Serialized result buffer from the wrapper function.
  shared::WrapperFunctionBuffer callWrapper(ExecutorAddr WrapperFnAddr,
                                            ArrayRef<char> ArgBuffer) {
    return EPC->callWrapper(WrapperFnAddr, ArgBuffer);
  }

  /// Run a wrapper function using SPS to serialize the arguments and
  /// deserialize the results.
  /// @param WrapperFnAddr Address of the SPS wrapper function in the executor.
  /// @param SendResult Callback that receives the deserialized result.
  /// @param Args Concrete arguments to serialize with SPS.
  template <typename SPSSignature, typename SendResultT, typename... ArgTs>
  void callSPSWrapperAsync(ExecutorAddr WrapperFnAddr, SendResultT &&SendResult,
                           const ArgTs &...Args) {
    EPC->callSPSWrapperAsync<SPSSignature, SendResultT, ArgTs...>(
        WrapperFnAddr, std::forward<SendResultT>(SendResult), Args...);
  }

  /// Run a wrapper function using SPS to serialize the arguments and
  /// deserialize the results.
  ///
  /// If SPSSignature is a non-void function signature then the second argument
  /// (the first in the Args list) should be a reference to a return value.
  /// @param WrapperFnAddr Address of the SPS wrapper function in the executor.
  /// @param WrapperCallArgs Return-value reference (if any) followed by args.
  /// @return Success, or an error if the SPS wrapper call fails.
  template <typename SPSSignature, typename... WrapperCallArgTs>
  Error callSPSWrapper(ExecutorAddr WrapperFnAddr,
                       WrapperCallArgTs &&...WrapperCallArgs) {
    return EPC->callSPSWrapper<SPSSignature, WrapperCallArgTs...>(
        WrapperFnAddr, std::forward<WrapperCallArgTs>(WrapperCallArgs)...);
  }

  /// Wrap a concrete async handler as an SPS AsyncHandlerWrapperFunction.
  ///
  /// This function is intended to support easy construction of
  /// AsyncHandlerWrapperFunctions that can be associated with a tag
  /// (using registerJITDispatchHandler) and called from the executor.
  /// @param H Handler taking a result sender and concrete argument types.
  /// @return JIT-dispatch handler that SPS-deserializes args for \p H.
  template <typename SPSSignature, typename HandlerT>
  static JITDispatchHandlerFunction wrapAsyncWithSPS(HandlerT &&H) {
    return [H = std::forward<HandlerT>(H)](SendResultFunction SendResult,
                                           const char *ArgData,
                                           size_t ArgSize) mutable {
      shared::WrapperFunction<SPSSignature>::handleAsync(
          ArgData, ArgSize, std::move(SendResult), H);
    };
  }

  /// Wrap a class method as an SPS AsyncHandlerWrapperFunction.
  ///
  /// This function is intended to support easy construction of
  /// AsyncHandlerWrapperFunctions that can be associated with a tag
  /// (using registerJITDispatchHandler) and called from the executor.
  /// @param Instance Object whose method will handle the dispatch.
  /// @param Method Member function pointer invoked for each request.
  /// @return JIT-dispatch handler that forwards SPS calls to \p Method.
  template <typename SPSSignature, typename ClassT, typename... MethodArgTs>
  static JITDispatchHandlerFunction
  wrapAsyncWithSPS(ClassT *Instance, void (ClassT::*Method)(MethodArgTs...)) {
    return wrapAsyncWithSPS<SPSSignature>(
        [Instance, Method](MethodArgTs &&...MethodArgs) {
          (Instance->*Method)(std::forward<MethodArgTs>(MethodArgs)...);
        });
  }

  /// Associate tag symbols in \p JD with jit-dispatch handler wrappers.
  ///
  /// For each tag symbol name, associate the corresponding
  /// AsyncHandlerWrapperFunction with the address of that symbol. The
  /// handler becomes callable from the executor using the ORC runtime
  /// __orc_rt_jit_dispatch function and the given tag.
  ///
  /// Tag symbols will be looked up in JD using LookupKind::Static,
  /// JITDylibLookupFlags::MatchAllSymbols (hidden tags will be found), and
  /// LookupFlags::WeaklyReferencedSymbol. Missing tag definitions will not
  /// cause an error, the handler will simply be dropped.
  /// @param JD JITDylib in which tag symbols are looked up.
  /// @param WFs Map from tag symbol names to handler wrappers.
  /// @return Success, or an error if handler registration fails.
  LLVM_ABI Error registerJITDispatchHandlers(
      JITDylib &JD, JITDispatchHandlerAssociationMap WFs);

  /// Run a registered jit-side wrapper function for an incoming dispatch.
  ///
  /// This should be called by the ExecutorProcessControl instance in response
  /// to incoming jit-dispatch requests from the executor.
  /// @param SendResult Callback used to return the wrapper result.
  /// @param HandlerFnTagAddr Address of the registered handler tag symbol.
  /// @param ArgBytes Serialized argument bytes for the handler.
  LLVM_ABI void runJITDispatchHandler(SendResultFunction SendResult,
                                      ExecutorAddr HandlerFnTagAddr,
                                      shared::WrapperFunctionBuffer ArgBytes);

  /// Dump the state of all the JITDylibs in this session.
  /// @param OS Stream to receive the dump.
  LLVM_ABI void dump(raw_ostream &OS);

  /// Check the internal consistency of ExecutionSession data structures.
#ifdef EXPENSIVE_CHECKS
  bool verifySessionState(Twine Phase);
#endif

private:
  static void logErrorsToStdErr(Error Err) {
    logAllUnhandledErrors(std::move(Err), errs(), "JIT session error: ");
  }

  void dispatchOutstandingMUs();

  static std::unique_ptr<MaterializationResponsibility>
  createMaterializationResponsibility(ResourceTracker &RT,
                                      SymbolFlagsMap Symbols,
                                      SymbolStringPtr InitSymbol) {
    auto &JD = RT.getJITDylib();
    std::unique_ptr<MaterializationResponsibility> MR(
        new MaterializationResponsibility(&RT, std::move(Symbols),
                                          std::move(InitSymbol)));
    JD.TrackerMRs[&RT].insert(MR.get());
    return MR;
  }

  Error removeResourceTracker(ResourceTracker &RT);
  void transferResourceTracker(ResourceTracker &DstRT, ResourceTracker &SrcRT);
  void destroyResourceTracker(ResourceTracker &RT);

  // State machine functions for query application..

  /// IL_updateCandidatesFor is called to remove already-defined symbols that
  /// match a given query from the set of candidate symbols to generate
  /// definitions for (no need to generate a definition if one already exists).
  Error IL_updateCandidatesFor(JITDylib &JD, JITDylibLookupFlags JDLookupFlags,
                               SymbolLookupSet &Candidates,
                               SymbolLookupSet *NonCandidates);

  /// Handle resumption of a lookup after entering a generator.
  void OL_resumeLookupAfterGeneration(InProgressLookupState &IPLS);

  /// OL_applyQueryPhase1 is an optionally re-startable loop for triggering
  /// definition generation. It is called when a lookup is performed, and again
  /// each time that LookupState::continueLookup is called.
  void OL_applyQueryPhase1(std::unique_ptr<InProgressLookupState> IPLS,
                           Error Err);

  /// OL_completeLookup is run once phase 1 successfully completes for a lookup
  /// call. It attempts to attach the symbol to all symbol table entries and
  /// collect all MaterializationUnits to dispatch. If this method fails then
  /// all MaterializationUnits will be left un-materialized.
  void OL_completeLookup(std::unique_ptr<InProgressLookupState> IPLS,
                         std::shared_ptr<AsynchronousSymbolQuery> Q,
                         RegisterDependenciesFunction RegisterDependencies);

  /// OL_completeLookupFlags is run once phase 1 successfully completes for a
  /// lookupFlags call.
  void OL_completeLookupFlags(
      std::unique_ptr<InProgressLookupState> IPLS,
      unique_function<void(Expected<SymbolFlagsMap>)> OnComplete);

  // State machine functions for MaterializationResponsibility.
  LLVM_ABI void
  OL_destroyMaterializationResponsibility(MaterializationResponsibility &MR);
  LLVM_ABI SymbolNameSet
  OL_getRequestedSymbols(const MaterializationResponsibility &MR);
  LLVM_ABI Error OL_notifyResolved(MaterializationResponsibility &MR,
                                   const SymbolMap &Symbols);

  // FIXME: We should be able to derive FailedSymsForQuery from each query once
  //        we fix how the detach operation works.
  struct EmitQueries {
    JITDylib::AsynchronousSymbolQuerySet Completed;
    JITDylib::AsynchronousSymbolQuerySet Failed;
    DenseMap<AsynchronousSymbolQuery *, std::shared_ptr<SymbolDependenceMap>>
        FailedSymsForQuery;
  };

  WaitingOnGraph::ExternalState
  IL_getSymbolState(JITDylib *JD, NonOwningSymbolStringPtr Name);

  template <typename UpdateSymbolFn, typename UpdateQueryFn>
  void IL_collectQueries(JITDylib::AsynchronousSymbolQuerySet &Qs,
                         WaitingOnGraph::ContainerElementsMap &QualifiedSymbols,
                         UpdateSymbolFn &&UpdateSymbol,
                         UpdateQueryFn &&UpdateQuery);

  Expected<EmitQueries> IL_emit(MaterializationResponsibility &MR,
                                WaitingOnGraph::SimplifyResult SR);
  LLVM_ABI Error OL_notifyEmitted(MaterializationResponsibility &MR,
                                  ArrayRef<SymbolDependenceGroup> EmittedDeps);

  LLVM_ABI Error OL_defineMaterializing(MaterializationResponsibility &MR,
                                        SymbolFlagsMap SymbolFlags);

  std::pair<JITDylib::AsynchronousSymbolQuerySet,
            std::shared_ptr<SymbolDependenceMap>>
  IL_failSymbols(JITDylib &JD, const SymbolNameVector &SymbolsToFail);
  LLVM_ABI void OL_notifyFailed(MaterializationResponsibility &MR);
  LLVM_ABI Error OL_replace(MaterializationResponsibility &MR,
                            std::unique_ptr<MaterializationUnit> MU);
  LLVM_ABI Expected<std::unique_ptr<MaterializationResponsibility>>
  OL_delegate(MaterializationResponsibility &MR, const SymbolNameSet &Symbols);

#ifndef NDEBUG
  void dumpDispatchInfo(Task &T);
#endif // NDEBUG

  mutable std::recursive_mutex SessionMutex;
  bool SessionOpen = true;
  std::unique_ptr<ExecutorProcessControl> EPC;
  std::unique_ptr<Platform> P;
  ErrorReporter ReportError = logErrorsToStdErr;

  std::vector<ResourceManager *> ResourceManagers;

  std::vector<JITDylibSP> JDs;
  JITDylib &BootstrapJD;
  WaitingOnGraph G;
  WaitingOnGraph::OpRecorder *GOpRecorder = nullptr;

  // FIXME: Remove this (and runOutstandingMUs) once the linking layer works
  //        with callbacks from asynchronous queries.
  mutable std::recursive_mutex OutstandingMUsMutex;
  std::vector<std::pair<std::unique_ptr<MaterializationUnit>,
                        std::unique_ptr<MaterializationResponsibility>>>
      OutstandingMUs;

  mutable std::mutex JITDispatchHandlersMutex;
  DenseMap<ExecutorAddr, std::shared_ptr<JITDispatchHandlerFunction>>
      JITDispatchHandlers;
};

template <typename Func> Error ResourceTracker::withResourceKeyDo(Func &&F) {
  return getJITDylib().getExecutionSession().runSessionLocked([&]() -> Error {
    if (isDefunct())
      return make_error<ResourceTrackerDefunct>(this);
    F(getKeyUnsafe());
    return Error::success();
  });
}

inline ExecutionSession &
MaterializationResponsibility::getExecutionSession() const {
  return JD.getExecutionSession();
}

template <typename GeneratorT>
GeneratorT &JITDylib::addGenerator(std::unique_ptr<GeneratorT> DefGenerator) {
  auto &G = *DefGenerator;
  ES.runSessionLocked([&] {
    assert(State == Open && "Cannot add generator to closed JITDylib");
    DefGenerators.push_back(std::move(DefGenerator));
  });
  return G;
}

template <typename Func>
auto JITDylib::withLinkOrderDo(Func &&F)
    -> decltype(F(std::declval<const JITDylibSearchOrder &>())) {
  assert(State == Open && "Cannot use link order of closed JITDylib");
  return ES.runSessionLocked([&]() { return F(LinkOrder); });
}

template <typename MaterializationUnitType>
Error JITDylib::define(std::unique_ptr<MaterializationUnitType> &&MU,
                       ResourceTrackerSP RT) {
  assert(MU && "Can not define with a null MU");

  if (MU->getSymbols().empty()) {
    // Empty MUs are allowable but pathological, so issue a warning.
    DEBUG_WITH_TYPE("orc", {
      dbgs() << "Warning: Discarding empty MU " << MU->getName() << " for "
             << getName() << "\n";
    });
    return Error::success();
  } else
    DEBUG_WITH_TYPE("orc", {
      dbgs() << "Defining MU " << MU->getName() << " for " << getName()
             << " (tracker: ";
      if (RT == getDefaultResourceTracker())
        dbgs() << "default)";
      else if (RT)
        dbgs() << RT.get() << ")\n";
      else
        dbgs() << "0x0, default will be used)\n";
    });

  return ES.runSessionLocked([&, this]() -> Error {
    if (State != Open)
      return make_error<JITDylibDefunct>(this);

    if (auto Err = defineImpl(*MU))
      return Err;

    if (!RT)
      RT = getDefaultResourceTracker();

    if (auto *P = ES.getPlatform()) {
      if (auto Err = P->notifyAdding(*RT, *MU))
        return Err;
    }

    installMaterializationUnit(std::move(MU), *RT);
    return Error::success();
  });
}

template <typename MaterializationUnitType>
Error JITDylib::define(std::unique_ptr<MaterializationUnitType> &MU,
                       ResourceTrackerSP RT) {
  assert(MU && "Can not define with a null MU");

  if (MU->getSymbols().empty()) {
    // Empty MUs are allowable but pathological, so issue a warning.
    DEBUG_WITH_TYPE("orc", {
      dbgs() << "Warning: Discarding empty MU " << MU->getName() << getName()
             << "\n";
    });
    return Error::success();
  } else
    DEBUG_WITH_TYPE("orc", {
      dbgs() << "Defining MU " << MU->getName() << " for " << getName()
             << " (tracker: ";
      if (RT == getDefaultResourceTracker())
        dbgs() << "default)";
      else if (RT)
        dbgs() << RT.get() << ")\n";
      else
        dbgs() << "0x0, default will be used)\n";
    });

  return ES.runSessionLocked([&, this]() -> Error {
    assert(State == Open && "JD is defunct");

    if (auto Err = defineImpl(*MU))
      return Err;

    if (!RT)
      RT = getDefaultResourceTracker();

    if (auto *P = ES.getPlatform()) {
      if (auto Err = P->notifyAdding(*RT, *MU))
        return Err;
    }

    installMaterializationUnit(std::move(MU), *RT);
    return Error::success();
  });
}

/// ReexportsGenerator can be used with JITDylib::addGenerator to automatically
/// re-export a subset of the source JITDylib's symbols in the target.
class LLVM_ABI ReexportsGenerator : public DefinitionGenerator {
public:
  /// Predicate that selects which source symbols should be reexported.
  using SymbolPredicate = std::function<bool(SymbolStringPtr)>;

  /// Create a reexports generator from \p SourceJD with optional filter.
  ///
  /// If an Allow predicate is passed, only symbols for which the predicate
  /// returns true will be reexported. If no Allow predicate is passed, all
  /// symbols will be exported.
  /// @param SourceJD JITDylib whose symbols may be reexported.
  /// @param SourceJDLookupFlags Lookup flags used when searching \p SourceJD.
  /// @param Allow Optional predicate selecting symbols to reexport.
  ReexportsGenerator(JITDylib &SourceJD,
                     JITDylibLookupFlags SourceJDLookupFlags,
                     SymbolPredicate Allow = SymbolPredicate());

  /// Try to generate reexport definitions for unresolved symbols in \p LookupSet.
  /// @param LS Lookup state that may be retained to suspend the lookup.
  /// @param K Kind of lookup being performed.
  /// @param JD Target JITDylib being searched.
  /// @param JDLookupFlags Whether hidden symbols in \p JD should match.
  /// @param LookupSet Unresolved symbols and their lookup flags.
  /// @return Success, or an error if reexport generation fails.
  Error tryToGenerate(LookupState &LS, LookupKind K, JITDylib &JD,
                      JITDylibLookupFlags JDLookupFlags,
                      const SymbolLookupSet &LookupSet) override;

private:
  JITDylib &SourceJD;
  JITDylibLookupFlags SourceJDLookupFlags;
  SymbolPredicate Allow;
};

// --------------- IMPLEMENTATION --------------
// Implementations for inline functions/methods.
// ---------------------------------------------

inline MaterializationResponsibility::~MaterializationResponsibility() {
  getExecutionSession().OL_destroyMaterializationResponsibility(*this);
}

inline SymbolNameSet MaterializationResponsibility::getRequestedSymbols() const {
  return getExecutionSession().OL_getRequestedSymbols(*this);
}

inline Error MaterializationResponsibility::notifyResolved(
    const SymbolMap &Symbols) {
  return getExecutionSession().OL_notifyResolved(*this, Symbols);
}

inline Error MaterializationResponsibility::notifyEmitted(
    ArrayRef<SymbolDependenceGroup> EmittedDeps) {
  return getExecutionSession().OL_notifyEmitted(*this, EmittedDeps);
}

inline Error MaterializationResponsibility::defineMaterializing(
    SymbolFlagsMap SymbolFlags) {
  return getExecutionSession().OL_defineMaterializing(*this,
                                                      std::move(SymbolFlags));
}

inline void MaterializationResponsibility::failMaterialization() {
  getExecutionSession().OL_notifyFailed(*this);
}

inline Error MaterializationResponsibility::replace(
    std::unique_ptr<MaterializationUnit> MU) {
  return getExecutionSession().OL_replace(*this, std::move(MU));
}

inline Expected<std::unique_ptr<MaterializationResponsibility>>
MaterializationResponsibility::delegate(const SymbolNameSet &Symbols) {
  return getExecutionSession().OL_delegate(*this, Symbols);
}

} // End namespace orc
} // End namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_CORE_H
