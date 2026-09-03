//===--- COFFPlatform.h -- Utilities for executing COFF in Orc --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Utilities for executing JIT'd COFF in Orc.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_COFFPLATFORM_H
#define LLVM_EXECUTIONENGINE_ORC_COFFPLATFORM_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/Orc/COFFVCRuntimeSupport.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/ExecutorProcessControl.h"
#include "llvm/ExecutionEngine/Orc/ObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"
#include "llvm/Support/Compiler.h"

#include <future>
#include <list>
#include <memory>
#include <thread>
#include <vector>

namespace llvm {
namespace orc {

/// Mediates between COFF initialization and ExecutionSession state.
class LLVM_ABI COFFPlatform : public Platform {
public:
  /// A function that will be called with the name of dll file that must be
  /// loaded.
  using LoadDynamicLibrary =
      unique_function<Error(JITDylib &JD, StringRef DLLFileName)>;

  /// Try to create a COFFPlatform instance, adding the ORC runtime to the
  /// given JITDylib.
  /// @param ObjLinkingLayer Object linking layer used to load the runtime and
  ///        JIT'd objects.
  /// @param PlatformJD JITDylib that will hold the ORC runtime and platform
  ///        aliases.
  /// @param OrcRuntimeArchiveBuffer Memory buffer containing the ORC runtime
  ///        static archive.
  /// @param LoadDynLibrary Callback used to load dependent DLLs into a
  ///        JITDylib.
  /// @param StaticVCRuntime Whether to bootstrap the static MSVC VC runtime.
  /// @param VCRuntimePath Optional directory of VC runtime libraries; if null,
  ///        search the MSVC toolchain and Windows SDK.
  /// @param RuntimeAliases Optional alias map; if unset,
  ///        \c standardPlatformAliases is used.
  /// @return A COFFPlatform instance, or an error if creation fails.
  static Expected<std::unique_ptr<COFFPlatform>>
  Create(ObjectLinkingLayer &ObjLinkingLayer, JITDylib &PlatformJD,
         std::unique_ptr<MemoryBuffer> OrcRuntimeArchiveBuffer,
         LoadDynamicLibrary LoadDynLibrary, bool StaticVCRuntime = false,
         const char *VCRuntimePath = nullptr,
         std::optional<SymbolAliasMap> RuntimeAliases = std::nullopt);

  /// Construct a COFFPlatform using a path to the ORC runtime archive.
  /// @param ObjLinkingLayer Object linking layer used to load the runtime and
  ///        JIT'd objects.
  /// @param PlatformJD JITDylib that will hold the ORC runtime and platform
  ///        aliases.
  /// @param OrcRuntimePath Filesystem path to the ORC runtime static archive.
  /// @param LoadDynLibrary Callback used to load dependent DLLs into a
  ///        JITDylib.
  /// @param StaticVCRuntime Whether to bootstrap the static MSVC VC runtime.
  /// @param VCRuntimePath Optional directory of VC runtime libraries; if null,
  ///        search the MSVC toolchain and Windows SDK.
  /// @param RuntimeAliases Optional alias map; if unset,
  ///        \c standardPlatformAliases is used.
  /// @return A COFFPlatform instance, or an error if creation fails.
  static Expected<std::unique_ptr<COFFPlatform>>
  Create(ObjectLinkingLayer &ObjLinkingLayer, JITDylib &PlatformJD,
         const char *OrcRuntimePath, LoadDynamicLibrary LoadDynLibrary,
         bool StaticVCRuntime = false, const char *VCRuntimePath = nullptr,
         std::optional<SymbolAliasMap> RuntimeAliases = std::nullopt);

  /// Returns the ExecutionSession for this platform.
  /// @return The ExecutionSession for this platform.
  ExecutionSession &getExecutionSession() const { return ES; }

  /// Returns the ObjectLinkingLayer for this platform.
  /// @return The ObjectLinkingLayer for this platform.
  ObjectLinkingLayer &getObjectLinkingLayer() const { return ObjLinkingLayer; }

  /// Install COFF platform symbols and per-JITDylib runtime support in \p JD.
  /// @param JD JITDylib to set up for COFF execution.
  /// @return Success, or an error if setup fails.
  Error setupJITDylib(JITDylib &JD) override;

  /// Remove COFF platform state associated with \p JD.
  /// @param JD JITDylib being torn down.
  /// @return Success, or an error if teardown fails.
  Error teardownJITDylib(JITDylib &JD) override;

  /// Record initializer symbols when a MaterializationUnit is added.
  /// @param RT Resource tracker for the JITDylib receiving \p MU.
  /// @param MU Materialization unit being added.
  /// @return Success, or an error if recording initializers fails.
  Error notifyAdding(ResourceTracker &RT,
                     const MaterializationUnit &MU) override;

  /// Handle removal of a ResourceTracker (not supported yet).
  /// @param RT Resource tracker being removed.
  /// @return Success, or an error if removal is not supported.
  Error notifyRemoving(ResourceTracker &RT) override;

  /// Returns the default aliases for the COFFPlatform.
  ///
  /// This can be modified by clients when constructing the platform to add
  /// or remove aliases.
  /// @param ES Execution session used to intern alias symbol names.
  /// @return The default symbol alias map for COFFPlatform.
  static SymbolAliasMap standardPlatformAliases(ExecutionSession &ES);

  /// Returns the array of required CXX aliases.
  /// @return The array of required CXX aliases.
  static ArrayRef<std::pair<const char *, const char *>> requiredCXXAliases();

  /// Returns the array of standard runtime utility aliases for COFF.
  /// @return The array of standard runtime utility aliases for COFF.
  static ArrayRef<std::pair<const char *, const char *>>
  standardRuntimeUtilityAliases();

  /// Returns the COFF section name used for SEH unwind info.
  /// @return The COFF section name used for SEH unwind info.
  static StringRef getSEHFrameSectionName() { return ".pdata"; }

private:
  using COFFJITDylibDepInfo = std::vector<ExecutorAddr>;
  using COFFJITDylibDepInfoMap =
      std::vector<std::pair<ExecutorAddr, COFFJITDylibDepInfo>>;
  using COFFObjectSectionsMap =
      SmallVector<std::pair<std::string, ExecutorAddrRange>>;
  using PushInitializersSendResultFn =
      unique_function<void(Expected<COFFJITDylibDepInfoMap>)>;
  using SendSymbolAddressFn = unique_function<void(Expected<ExecutorAddr>)>;
  using JITDylibDepMap = DenseMap<JITDylib *, SmallVector<JITDylib *>>;

  // The COFFPlatformPlugin scans/modifies LinkGraphs to support COFF
  // platform features including initializers, exceptions, and language
  // runtime registration.
  class LLVM_ABI COFFPlatformPlugin : public ObjectLinkingLayer::Plugin {
  public:
    COFFPlatformPlugin(COFFPlatform &CP) : CP(CP) {}

    void modifyPassConfig(MaterializationResponsibility &MR,
                          jitlink::LinkGraph &G,
                          jitlink::PassConfiguration &Config) override;

    // FIXME: We should be tentatively tracking scraped sections and discarding
    // if the MR fails.
    Error notifyFailed(MaterializationResponsibility &MR) override {
      return Error::success();
    }

    Error notifyRemovingResources(JITDylib &JD, ResourceKey K) override {
      return Error::success();
    }

    void notifyTransferringResources(JITDylib &JD, ResourceKey DstKey,
                                     ResourceKey SrcKey) override {}

  private:
    Error associateJITDylibHeaderSymbol(jitlink::LinkGraph &G,
                                        MaterializationResponsibility &MR,
                                        bool Bootstrap);

    Error preserveInitializerSections(jitlink::LinkGraph &G,
                                      MaterializationResponsibility &MR);
    Error registerObjectPlatformSections(jitlink::LinkGraph &G, JITDylib &JD);
    Error registerObjectPlatformSectionsInBootstrap(jitlink::LinkGraph &G,
                                                    JITDylib &JD);

    std::mutex PluginMutex;
    COFFPlatform &CP;
  };

  struct JDBootstrapState {
    JITDylib *JD = nullptr;
    std::string JDName;
    ExecutorAddr HeaderAddr;
    std::list<COFFObjectSectionsMap> ObjectSectionsMaps;
    SmallVector<std::pair<std::string, ExecutorAddr>> Initializers;
  };

  static bool supportedTarget(const Triple &TT);

  COFFPlatform(
      ObjectLinkingLayer &ObjLinkingLayer, JITDylib &PlatformJD,
      std::unique_ptr<StaticLibraryDefinitionGenerator> OrcRuntimeGenerator,
      std::set<std::string> DylibsToPreload,
      std::unique_ptr<MemoryBuffer> OrcRuntimeArchiveBuffer,
      std::unique_ptr<object::Archive> OrcRuntimeArchive,
      LoadDynamicLibrary LoadDynLibrary, bool StaticVCRuntime,
      const char *VCRuntimePath, Error &Err);

  // Associate COFFPlatform JIT-side runtime support functions with handlers.
  Error associateRuntimeSupportFunctions(JITDylib &PlatformJD);

  // Records the addresses of runtime symbols used by the platform.
  Error bootstrapCOFFRuntime(JITDylib &PlatformJD);

  // Run a specific void function if it exists.
  Error runSymbolIfExists(JITDylib &PlatformJD, StringRef SymbolName);

  // Run collected initializers in boostrap stage.
  Error runBootstrapInitializers(JDBootstrapState &BState);
  Error runBootstrapSubsectionInitializers(JDBootstrapState &BState,
                                           StringRef Start, StringRef End);

  // Build dependency graph of a JITDylib
  Expected<JITDylibDepMap> buildJDDepMap(JITDylib &JD);

  Expected<MemoryBufferRef> getPerJDObjectFile();

  // Implements rt_pushInitializers by making repeat async lookups for
  // initializer symbols (each lookup may spawn more initializer symbols if
  // it pulls in new materializers, e.g. from objects in a static library).
  void pushInitializersLoop(PushInitializersSendResultFn SendResult,
                            JITDylibSP JD, JITDylibDepMap &JDDepMap);

  void rt_pushInitializers(PushInitializersSendResultFn SendResult,
                           ExecutorAddr JDHeaderAddr);

  void rt_lookupSymbol(SendSymbolAddressFn SendResult, ExecutorAddr Handle,
                       StringRef SymbolName);

  ExecutionSession &ES;
  ObjectLinkingLayer &ObjLinkingLayer;

  LoadDynamicLibrary LoadDynLibrary;
  std::unique_ptr<COFFVCRuntimeBootstrapper> VCRuntimeBootstrap;
  std::unique_ptr<MemoryBuffer> OrcRuntimeArchiveBuffer;
  std::unique_ptr<object::Archive> OrcRuntimeArchive;
  bool StaticVCRuntime;

  SymbolStringPtr COFFHeaderStartSymbol;

  // State of bootstrap in progress
  std::map<JITDylib *, JDBootstrapState> JDBootstrapStates;
  std::atomic<bool> Bootstrapping;

  ExecutorAddr orc_rt_coff_platform_bootstrap;
  ExecutorAddr orc_rt_coff_platform_shutdown;
  ExecutorAddr orc_rt_coff_register_object_sections;
  ExecutorAddr orc_rt_coff_deregister_object_sections;
  ExecutorAddr orc_rt_coff_register_jitdylib;
  ExecutorAddr orc_rt_coff_deregister_jitdylib;

  DenseMap<JITDylib *, ExecutorAddr> JITDylibToHeaderAddr;
  DenseMap<ExecutorAddr, JITDylib *> HeaderAddrToJITDylib;

  DenseMap<JITDylib *, SymbolLookupSet> RegisteredInitSymbols;

  std::mutex PlatformMutex;
};

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_COFFPLATFORM_H
