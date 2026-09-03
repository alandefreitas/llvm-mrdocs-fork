//===-- ELFNixPlatform.h -- Utilities for executing ELF in Orc --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Linux/BSD support for executing JIT'd ELF in Orc.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_ELFNIXPLATFORM_H
#define LLVM_EXECUTIONENGINE_ORC_ELFNIXPLATFORM_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/ExecutorProcessControl.h"
#include "llvm/ExecutionEngine/Orc/ObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"
#include "llvm/Support/Compiler.h"

#include <future>
#include <thread>
#include <vector>

namespace llvm {
namespace orc {

/// Per-object ELF sections that must be registered with the ORC runtime.
struct ELFPerObjectSectionsToRegister {
  /// Range of the object's EH frame section, if present.
  ExecutorAddrRange EHFrameSection;
  /// Range of the object's thread-local data section, if present.
  ExecutorAddrRange ThreadDataSection;
};

/// List of JITDylib header addresses that a JITDylib depends on.
using ELFNixJITDylibDepInfo = std::vector<ExecutorAddr>;
/// Map from JITDylib header address to its dependency info.
using ELFNixJITDylibDepInfoMap =
    std::vector<std::pair<ExecutorAddr, ELFNixJITDylibDepInfo>>;

/// Named ORC runtime function whose address is resolved during bootstrap.
struct RuntimeFunction {
  /// Construct a runtime function entry for the given symbol name.
  /// @param Name Interned name of the runtime function.
  RuntimeFunction(SymbolStringPtr Name) : Name(std::move(Name)) {}
  /// Interned name of the runtime function.
  SymbolStringPtr Name;
  /// Resolved address of the runtime function in the executor.
  ExecutorAddr Addr;
};

/// Map of deferred runtime function call argument pairs pending bootstrap.
using DeferredRuntimeFnMap = DenseMap<
    std::pair<RuntimeFunction *, RuntimeFunction *>,
    SmallVector<std::pair<shared::WrapperFunctionCall::ArgDataBufferType,
                          shared::WrapperFunctionCall::ArgDataBufferType>>>;

/// Mediates between ELFNix initialization and ExecutionSession state.
class LLVM_ABI ELFNixPlatform : public Platform {
public:
  /// Try to create a ELFNixPlatform instance, adding the ORC runtime to the
  /// given JITDylib.
  ///
  /// The ORC runtime requires access to a number of symbols in
  /// libc++. It is up to the caller to ensure that the required
  /// symbols can be referenced by code added to PlatformJD. The
  /// standard way to achieve this is to first attach dynamic library
  /// search generators for either the given process, or for the
  /// specific required libraries, to PlatformJD, then to create the
  /// platform instance:
  ///
  /// \code{.cpp}
  ///   auto &PlatformJD = ES.createBareJITDylib("stdlib");
  ///   PlatformJD.addGenerator(
  ///     ExitOnErr(EPCDynamicLibrarySearchGenerator
  ///                 ::GetForTargetProcess(EPC)));
  ///   ES.setPlatform(
  ///     ExitOnErr(ELFNixPlatform::Create(ES, ObjLayer, EPC, PlatformJD,
  ///                                     "/path/to/orc/runtime")));
  /// \endcode
  ///
  /// Alternatively, these symbols could be added to another JITDylib that
  /// PlatformJD links against.
  ///
  /// Clients are also responsible for ensuring that any JIT'd code that
  /// depends on runtime functions (including any code using TLV or static
  /// destructors) can reference the runtime symbols. This is usually achieved
  /// by linking any JITDylibs containing regular code against
  /// PlatformJD.
  ///
  /// By default, ELFNixPlatform will add the set of aliases returned by the
  /// standardPlatformAliases function. This includes both required aliases
  /// (e.g. __cxa_atexit -> __orc_rt_elf_cxa_atexit for static destructor
  /// support), and optional aliases that provide JIT versions of common
  /// functions (e.g. dlopen -> __orc_rt_elf_jit_dlopen). Clients can
  /// override these defaults by passing a non-None value for the
  /// RuntimeAliases function, in which case the client is responsible for
  /// setting up all aliases (including the required ones).
  /// @param ObjLinkingLayer Object linking layer used to load the runtime and
  ///        JIT'd objects.
  /// @param PlatformJD JITDylib that will hold the ORC runtime and platform
  ///        aliases.
  /// @param OrcRuntime Definition generator that supplies the ORC runtime.
  /// @param RuntimeAliases Optional alias map; if unset,
  ///        \c standardPlatformAliases is used.
  /// @return An ELFNixPlatform instance, or an error if creation fails.
  static Expected<std::unique_ptr<ELFNixPlatform>>
  Create(ObjectLinkingLayer &ObjLinkingLayer, JITDylib &PlatformJD,
         std::unique_ptr<DefinitionGenerator> OrcRuntime,
         std::optional<SymbolAliasMap> RuntimeAliases = std::nullopt);

  /// Construct using a path to the ORC runtime.
  /// @param ObjLinkingLayer Object linking layer used to load the runtime and
  ///        JIT'd objects.
  /// @param PlatformJD JITDylib that will hold the ORC runtime and platform
  ///        aliases.
  /// @param OrcRuntimePath Filesystem path to the ORC runtime static archive.
  /// @param RuntimeAliases Optional alias map; if unset,
  ///        \c standardPlatformAliases is used.
  /// @return An ELFNixPlatform instance, or an error if creation fails.
  static Expected<std::unique_ptr<ELFNixPlatform>>
  Create(ObjectLinkingLayer &ObjLinkingLayer, JITDylib &PlatformJD,
         const char *OrcRuntimePath,
         std::optional<SymbolAliasMap> RuntimeAliases = std::nullopt);

  /// Returns the ExecutionSession for this platform.
  /// @return The ExecutionSession for this platform.
  ExecutionSession &getExecutionSession() const { return ES; }
  /// Returns the ObjectLinkingLayer for this platform.
  /// @return The ObjectLinkingLayer for this platform.
  ObjectLinkingLayer &getObjectLinkingLayer() const { return ObjLinkingLayer; }

  /// Install ELFNix platform symbols and per-JITDylib runtime support in \p JD.
  /// @param JD JITDylib to set up for ELFNix execution.
  /// @return Success, or an error if setup fails.
  Error setupJITDylib(JITDylib &JD) override;
  /// Remove ELFNix platform state associated with \p JD.
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

  /// Returns the default aliases for the ELFNixPlatform.
  ///
  /// This can be modified by clients when constructing the platform to add
  /// or remove aliases.
  /// @param ES Execution session used to intern alias symbol names.
  /// @param PlatformJD JITDylib that will hold the platform aliases.
  /// @return The default symbol alias map for ELFNixPlatform, or an error.
  static Expected<SymbolAliasMap> standardPlatformAliases(ExecutionSession &ES,
                                                          JITDylib &PlatformJD);

  /// Returns the array of required CXX aliases.
  /// @return The array of required CXX aliases.
  static ArrayRef<std::pair<const char *, const char *>> requiredCXXAliases();

  /// Returns the array of standard runtime utility aliases for ELF.
  /// @return The array of standard runtime utility aliases for ELF.
  static ArrayRef<std::pair<const char *, const char *>>
  standardRuntimeUtilityAliases();

  /// Returns a list of aliases required to enable lazy compilation via the
  /// ORC runtime.
  /// @return The list of aliases required for lazy compilation via the ORC
  ///         runtime.
  static ArrayRef<std::pair<const char *, const char *>>
  standardLazyCompilationAliases();

private:
  // Data needed for bootstrap only.
  struct BootstrapInfo {
    std::mutex Mutex;
    std::condition_variable CV;
    size_t ActiveGraphs = 0;
    ExecutorAddr ELFNixHeaderAddr;
    DeferredRuntimeFnMap DeferredRTFnMap;

    void addArgumentsToRTFnMap(
        RuntimeFunction *func1, RuntimeFunction *func2,
        const shared::WrapperFunctionCall::ArgDataBufferType &arg1,
        const shared::WrapperFunctionCall::ArgDataBufferType &arg2) {
      std::lock_guard<std::mutex> Lock(Mutex);
      auto &argList = DeferredRTFnMap[std::make_pair(func1, func2)];
      argList.emplace_back(arg1, arg2);
    }
  };

  // The ELFNixPlatformPlugin scans/modifies LinkGraphs to support ELF
  // platform features including initializers, exceptions, TLV, and language
  // runtime registration.
  class LLVM_ABI ELFNixPlatformPlugin : public ObjectLinkingLayer::Plugin {
  public:
    ELFNixPlatformPlugin(ELFNixPlatform &MP) : MP(MP) {}

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
    Error bootstrapPipelineStart(jitlink::LinkGraph &G);
    Error bootstrapPipelineRecordRuntimeFunctions(jitlink::LinkGraph &G);
    Error bootstrapPipelineEnd(jitlink::LinkGraph &G);

    void addDSOHandleSupportPasses(MaterializationResponsibility &MR,
                                   jitlink::PassConfiguration &Config);

    void addEHAndTLVSupportPasses(MaterializationResponsibility &MR,
                                  jitlink::PassConfiguration &Config,
                                  bool IsBootstrapping);

    Error preserveInitSections(jitlink::LinkGraph &G,
                               MaterializationResponsibility &MR);

    Error registerInitSections(jitlink::LinkGraph &G, JITDylib &JD,
                               bool IsBootstrapping);

    Error registerFiniSections(jitlink::LinkGraph &G, JITDylib &JD,
                               bool IsBootstrapping);

    Error fixTLVSectionsAndEdges(jitlink::LinkGraph &G, JITDylib &JD);

    std::mutex PluginMutex;
    ELFNixPlatform &MP;
  };

  using PushInitializersSendResultFn =
      unique_function<void(Expected<ELFNixJITDylibDepInfoMap>)>;

  using SendSymbolAddressFn = unique_function<void(Expected<ExecutorAddr>)>;

  static bool supportedTarget(const Triple &TT);

  ELFNixPlatform(ObjectLinkingLayer &ObjLinkingLayer, JITDylib &PlatformJD,
                 std::unique_ptr<DefinitionGenerator> OrcRuntimeGenerator,
                 Error &Err);

  // Associate ELFNixPlatform JIT-side runtime support functions with handlers.
  Error associateRuntimeSupportFunctions(JITDylib &PlatformJD);

  void pushInitializersLoop(PushInitializersSendResultFn SendResult,
                            JITDylibSP JD);

  void rt_recordInitializers(PushInitializersSendResultFn SendResult,
                             ExecutorAddr JDHeader);

  void rt_lookupSymbol(SendSymbolAddressFn SendResult, ExecutorAddr Handle,
                       StringRef SymbolName);

  Error registerPerObjectSections(jitlink::LinkGraph &G,
                                  const ELFPerObjectSectionsToRegister &POSR,
                                  bool IsBootstrapping);

  Expected<uint64_t> createPThreadKey();

  ExecutionSession &ES;
  JITDylib &PlatformJD;
  ObjectLinkingLayer &ObjLinkingLayer;

  SymbolStringPtr DSOHandleSymbol;

  RuntimeFunction PlatformBootstrap{
      ES.intern("__orc_rt_elfnix_platform_bootstrap")};
  RuntimeFunction PlatformShutdown{
      ES.intern("__orc_rt_elfnix_platform_shutdown")};
  RuntimeFunction RegisterJITDylib{
      ES.intern("__orc_rt_elfnix_register_jitdylib")};
  RuntimeFunction DeregisterJITDylib{
      ES.intern("__orc_rt_elfnix_deregister_jitdylib")};
  RuntimeFunction RegisterObjectSections{
      ES.intern("__orc_rt_elfnix_register_object_sections")};
  RuntimeFunction DeregisterObjectSections{
      ES.intern("__orc_rt_elfnix_deregister_object_sections")};
  RuntimeFunction RegisterInitSections{
      ES.intern("__orc_rt_elfnix_register_init_sections")};
  RuntimeFunction DeregisterInitSections{
      ES.intern("__orc_rt_elfnix_deregister_init_sections")};
  RuntimeFunction RegisterFiniSections{
      ES.intern("__orc_rt_elfnix_register_fini_sections")};
  RuntimeFunction DeregisterFiniSections{
      ES.intern("__orc_rt_elfnix_deregister_fini_sections")};
  RuntimeFunction CreatePThreadKey{
      ES.intern("__orc_rt_elfnix_create_pthread_key")};

  DenseMap<JITDylib *, SymbolLookupSet> RegisteredInitSymbols;

  // InitSeqs gets its own mutex to avoid locking the whole session when
  // aggregating data from the jitlink.
  std::mutex PlatformMutex;
  std::vector<ELFPerObjectSectionsToRegister> BootstrapPOSRs;

  DenseMap<ExecutorAddr, JITDylib *> HandleAddrToJITDylib;
  DenseMap<JITDylib *, ExecutorAddr> JITDylibToHandleAddr;
  DenseMap<JITDylib *, uint64_t> JITDylibToPThreadKey;

  std::atomic<BootstrapInfo *> Bootstrap;
};

/// SPS serialization helpers for ELFNixPlatform types.
namespace shared {

/// SPS tag type for ELFPerObjectSectionsToRegister as an EH-frame/TLV pair.
using SPSELFPerObjectSectionsToRegister =
    SPSTuple<SPSExecutorAddrRange, SPSExecutorAddrRange>;

/// SPS serialization traits for ELFPerObjectSectionsToRegister.
template <>
class SPSSerializationTraits<SPSELFPerObjectSectionsToRegister,
                             ELFPerObjectSectionsToRegister> {

public:
  /// Return the serialized size of \p MOPOSR.
  /// @param MOPOSR Sections to measure.
  /// @return Number of bytes needed to serialize \p MOPOSR.
  static size_t size(const ELFPerObjectSectionsToRegister &MOPOSR) {
    return SPSELFPerObjectSectionsToRegister::AsArgList::size(
        MOPOSR.EHFrameSection, MOPOSR.ThreadDataSection);
  }

  /// Serialize \p MOPOSR into \p OB.
  /// @param OB Output buffer.
  /// @param MOPOSR Sections to serialize.
  /// @return True on success, false if serialization fails.
  static bool serialize(SPSOutputBuffer &OB,
                        const ELFPerObjectSectionsToRegister &MOPOSR) {
    return SPSELFPerObjectSectionsToRegister::AsArgList::serialize(
        OB, MOPOSR.EHFrameSection, MOPOSR.ThreadDataSection);
  }

  /// Deserialize an ELFPerObjectSectionsToRegister from \p IB into \p MOPOSR.
  /// @param IB Input buffer.
  /// @param MOPOSR Destination sections.
  /// @return True on success, false if deserialization fails.
  static bool deserialize(SPSInputBuffer &IB,
                          ELFPerObjectSectionsToRegister &MOPOSR) {
    return SPSELFPerObjectSectionsToRegister::AsArgList::deserialize(
        IB, MOPOSR.EHFrameSection, MOPOSR.ThreadDataSection);
  }
};

/// SPS tag type for ELFNixJITDylibDepInfoMap.
using SPSELFNixJITDylibDepInfoMap =
    SPSSequence<SPSTuple<SPSExecutorAddr, SPSSequence<SPSExecutorAddr>>>;

} // end namespace shared
} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_ELFNIXPLATFORM_H
