//===-- MachOPlatform.h - Utilities for executing MachO in Orc --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Utilities for executing JIT'd MachO in Orc.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_MACHOPLATFORM_H
#define LLVM_EXECUTIONENGINE_ORC_MACHOPLATFORM_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/ExecutorProcessControl.h"
#include "llvm/ExecutionEngine/Orc/ObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"
#include "llvm/Support/Compiler.h"

#include <array>
#include <future>
#include <optional>
#include <thread>
#include <vector>

namespace llvm {
namespace orc {

/// Mediates between MachO initialization and ExecutionSession state.
class LLVM_ABI MachOPlatform : public Platform {
public:
  /// Dependency info for a MachO JITDylib used during runtime registration.
  ///
  /// Used internally by MachOPlatform, but made public to enable serialization.
  struct MachOJITDylibDepInfo {
    /// True if the JITDylib has been sealed and can no longer gain dependencies.
    bool Sealed = false;
    /// Addresses of MachO headers for JITDylibs that this one depends on.
    std::vector<ExecutorAddr> DepHeaders;
  };

  /// Map from JITDylib header address to MachO dependency info.
  ///
  /// Used internally by MachOPlatform, but made public to enable serialization.
  using MachOJITDylibDepInfoMap =
      std::vector<std::pair<ExecutorAddr, MachOJITDylibDepInfo>>;

  /// Flags describing a symbol exported to the MachO executor runtime.
  ///
  /// Used internally by MachOPlatform, but made public to enable serialization.
  enum class MachOExecutorSymbolFlags : uint8_t {
    /// No special flags.
    None = 0,
    /// Symbol is weak.
    Weak = 1U << 0,
    /// Symbol is callable (e.g. a function).
    Callable = 1U << 1,
    LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */ Callable)
  };

  /// Configuration for the mach-o header of a JITDylib. Specify common load
  /// commands that should be added to the header.
  struct HeaderOptions {
    /// A dylib for use with a dylib command (e.g. LC_ID_DYLIB, LC_LOAD_DYLIB).
    struct Dylib {
      /// Install name of the dylib.
      std::string Name;
      /// Timestamp recorded in the dylib command.
      uint32_t Timestamp;
      /// Current version encoded as X.Y.Z in nibbles.
      uint32_t CurrentVersion;
      /// Compatibility version encoded as X.Y.Z in nibbles.
      uint32_t CompatibilityVersion;
    };

    /// An LC_LOAD_DYLIB or LC_LOAD_WEAK_DYLIB command to embed in the header.
    struct LoadDylibCmd {
      /// Kind of load-dylib command to emit.
      enum class LoadKind {
        /// Emit LC_LOAD_DYLIB.
        Default,
        /// Emit LC_LOAD_WEAK_DYLIB.
        Weak
      };

      /// Dylib identity described by this load command.
      Dylib D;
      /// Whether the load is strong or weak.
      LoadKind K;
    };

    /// Options for an LC_BUILD_VERSION load command.
    struct BuildVersionOpts {

      /// Derive build-version options from a target triple if possible.
      /// @param TT Target triple used to select the platform identifier.
      /// @param MinOS Minimum OS version encoded as X.Y.Z in nibbles.
      /// @param SDK SDK version encoded as X.Y.Z in nibbles.
      /// @return Build version options, or nullopt if the platform is unknown.
      LLVM_ABI static std::optional<BuildVersionOpts>
      fromTriple(const Triple &TT, uint32_t MinOS, uint32_t SDK);

      /// Platform identifier for the build-version command.
      uint32_t Platform;
      /// Minimum OS version; X.Y.Z is encoded in nibbles xxxx.yy.zz.
      uint32_t MinOS;
      /// SDK version; X.Y.Z is encoded in nibbles xxxx.yy.zz.
      uint32_t SDK;
    };

    /// Override for LC_IC_DYLIB. If this is nullopt, {JD.getName(), 0, 0, 0}
    /// will be used.
    std::optional<Dylib> IDDylib;

    /// List of LC_LOAD_DYLIBs.
    std::vector<LoadDylibCmd> LoadDylibs;
    /// List of LC_RPATHs.
    std::vector<std::string> RPaths;
    /// List of LC_BUILD_VERSIONs.
    std::vector<BuildVersionOpts> BuildVersions;
    /// Optional LC_TARGET_TRIPLE.
    std::optional<std::string> TargetTriple;

    /// Optional UUID. If set, this will be used to add an LC_UUID command.
    std::optional<std::array<uint8_t, 16>> UUID;

    /// Construct HeaderOptions with no ID dylib override.
    HeaderOptions() = default;
    /// Construct HeaderOptions with the given ID dylib.
    /// @param D Dylib identity used for LC_ID_DYLIB.
    HeaderOptions(Dylib D) : IDDylib(std::move(D)) {}
  };

  /// Callback for generating HeaderOptions structs for new JITDylibs.
  using HeaderOptionsBuilder = unique_function<HeaderOptions(JITDylib &JD)>;

  /// Used by setupJITDylib to create MachO header MaterializationUnits for
  /// JITDylibs.
  using MachOHeaderMUBuilder =
      unique_function<std::unique_ptr<MaterializationUnit>(MachOPlatform &MOP,
                                                           HeaderOptions Opts)>;

  /// Simple MachO header graph builder.
  /// @param MOP MachO platform that owns header state for the JITDylib.
  /// @param Opts Header load-command options to embed in the MachO header.
  /// @return A MaterializationUnit that synthesizes a simple MachO header.
  static inline std::unique_ptr<MaterializationUnit>
  buildSimpleMachOHeaderMU(MachOPlatform &MOP, HeaderOptions Opts);

  /// Try to create a MachOPlatform instance, adding the ORC runtime to the
  /// given JITDylib.
  ///
  /// The ORC runtime requires access to a number of symbols in libc++, and
  /// requires access to symbols in libobjc, and libswiftCore to support
  /// Objective-C and Swift code. It is up to the caller to ensure that the
  /// required symbols can be referenced by code added to PlatformJD. The
  /// standard way to achieve this is to first attach dynamic library search
  /// generators for either the given process, or for the specific required
  /// libraries, to PlatformJD, then to create the platform instance:
  ///
  /// \code{.cpp}
  ///   auto &PlatformJD = ES.createBareJITDylib("stdlib");
  ///   PlatformJD.addGenerator(
  ///     ExitOnErr(EPCDynamicLibrarySearchGenerator
  ///                 ::GetForTargetProcess(EPC)));
  ///   ES.setPlatform(
  ///     ExitOnErr(MachOPlatform::Create(ES, ObjLayer, EPC, PlatformJD,
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
  /// By default, MachOPlatform will add the set of aliases returned by the
  /// standardPlatformAliases function. This includes both required aliases
  /// (e.g. __cxa_atexit -> __orc_rt_macho_cxa_atexit for static destructor
  /// support), and optional aliases that provide JIT versions of common
  /// functions (e.g. dlopen -> __orc_rt_macho_jit_dlopen). Clients can
  /// override these defaults by passing a non-None value for the
  /// RuntimeAliases function, in which case the client is responsible for
  /// setting up all aliases (including the required ones).
  /// @param ObjLinkingLayer Object linking layer used to load the runtime and
  ///        JIT'd objects.
  /// @param PlatformJD JITDylib that will hold the ORC runtime and platform
  ///        aliases.
  /// @param OrcRuntime Definition generator that supplies the ORC runtime.
  /// @param BuildHeaderOpts Callback that builds HeaderOptions for new
  ///        JITDylibs.
  /// @param PlatformJDOpts Header options for PlatformJD itself.
  /// @param BuildMachOHeaderMU Builder that creates MachO header
  ///        MaterializationUnits.
  /// @param RuntimeAliases Optional alias map; if unset,
  ///        \c standardPlatformAliases is used.
  /// @return A MachOPlatform instance, or an error if creation fails.
  static Expected<std::unique_ptr<MachOPlatform>>
  Create(ObjectLinkingLayer &ObjLinkingLayer, JITDylib &PlatformJD,
         std::unique_ptr<DefinitionGenerator> OrcRuntime,
         HeaderOptionsBuilder BuildHeaderOpts = defaultHeaderOpts,
         HeaderOptions PlatformJDOpts = {},
         MachOHeaderMUBuilder BuildMachOHeaderMU = buildSimpleMachOHeaderMU,
         std::optional<SymbolAliasMap> RuntimeAliases = std::nullopt);

  /// Construct using a path to the ORC runtime.
  /// @param ObjLinkingLayer Object linking layer used to load the runtime and
  ///        JIT'd objects.
  /// @param PlatformJD JITDylib that will hold the ORC runtime and platform
  ///        aliases.
  /// @param OrcRuntimePath Filesystem path to the ORC runtime static archive.
  /// @param BuildHeaderOpts Callback that builds HeaderOptions for new
  ///        JITDylibs.
  /// @param PlatformJDOpts Header options for PlatformJD itself.
  /// @param BuildMachOHeaderMU Builder that creates MachO header
  ///        MaterializationUnits.
  /// @param RuntimeAliases Optional alias map; if unset,
  ///        \c standardPlatformAliases is used.
  /// @return A MachOPlatform instance, or an error if creation fails.
  static Expected<std::unique_ptr<MachOPlatform>>
  Create(ObjectLinkingLayer &ObjLinkingLayer, JITDylib &PlatformJD,
         const char *OrcRuntimePath,
         HeaderOptionsBuilder BuildHeaderOpts = defaultHeaderOpts,
         HeaderOptions PlatformJDOpts = {},
         MachOHeaderMUBuilder BuildMachOHeaderMU = buildSimpleMachOHeaderMU,
         std::optional<SymbolAliasMap> RuntimeAliases = std::nullopt);

  /// Returns the ExecutionSession for this platform.
  /// @return The ExecutionSession for this platform.
  ExecutionSession &getExecutionSession() const { return ES; }
  /// Returns the ObjectLinkingLayer for this platform.
  /// @return The ObjectLinkingLayer for this platform.
  ObjectLinkingLayer &getObjectLinkingLayer() const { return ObjLinkingLayer; }

  /// Returns the symbol naming the start of the MachO header for a JITDylib.
  /// @return The symbol naming the start of the MachO header for a JITDylib.
  NonOwningSymbolStringPtr getMachOHeaderStartSymbol() const {
    return NonOwningSymbolStringPtr(MachOHeaderStartSymbol);
  }

  /// Install MachO platform symbols and per-JITDylib runtime support in \p JD.
  /// @param JD JITDylib to set up for MachO execution.
  /// @return Success, or an error if setup fails.
  Error setupJITDylib(JITDylib &JD) override;

  /// Install any platform-specific symbols (e.g. `__dso_handle`) and create a
  /// mach-o header based on the given options.
  /// @param JD JITDylib to set up for MachO execution.
  /// @param Opts MachO header options for \p JD.
  /// @return Success, or an error if setup fails.
  Error setupJITDylib(JITDylib &JD, HeaderOptions Opts);

  /// Remove MachO platform state associated with \p JD.
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

  /// Returns the default aliases for the MachOPlatform.
  ///
  /// This can be modified by clients when constructing the platform to add
  /// or remove aliases.
  /// @param ES Execution session used to intern alias symbol names.
  /// @return The default symbol alias map for MachOPlatform.
  static SymbolAliasMap standardPlatformAliases(ExecutionSession &ES);

  /// Returns the array of required CXX aliases.
  /// @return The array of required CXX aliases.
  static ArrayRef<std::pair<const char *, const char *>> requiredCXXAliases();

  /// Returns the array of standard runtime utility aliases for MachO.
  /// @return The array of standard runtime utility aliases for MachO.
  static ArrayRef<std::pair<const char *, const char *>>
  standardRuntimeUtilityAliases();

  /// Returns a list of aliases required to enable lazy compilation via the
  /// ORC runtime.
  /// @return The list of aliases required for lazy compilation via the ORC
  ///         runtime.
  static ArrayRef<std::pair<const char *, const char *>>
  standardLazyCompilationAliases();

  /// Returns default HeaderOptions for the given JITDylib.
  /// @param JD JITDylib for which default header options are requested.
  /// @return Default HeaderOptions for \p JD.
  static HeaderOptions defaultHeaderOpts(JITDylib &JD);

private:
  using SymbolTableVector = SmallVector<
      std::tuple<ExecutorAddr, ExecutorAddr, MachOExecutorSymbolFlags>>;

  // Data needed for bootstrap only.
  struct BootstrapInfo {
    std::condition_variable CV;
    size_t ActiveGraphs = 0;
    shared::AllocActions DeferredAAs;
    ExecutorAddr MachOHeaderAddr;
    SymbolTableVector SymTab;
  };

  // The MachOPlatformPlugin scans/modifies LinkGraphs to support MachO
  // platform features including initializers, exceptions, TLV, and language
  // runtime registration.
  class LLVM_ABI MachOPlatformPlugin : public ObjectLinkingLayer::Plugin {
  public:
    MachOPlatformPlugin(MachOPlatform &MP) : MP(MP) {}

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
    struct UnwindSections {
      SmallVector<ExecutorAddrRange> CodeRanges;
      ExecutorAddrRange DwarfSection;
      ExecutorAddrRange CompactUnwindSection;
    };

    struct ObjCImageInfo {
      uint32_t Version = 0;
      uint32_t Flags = 0;
      /// Whether this image info can no longer be mutated, as it may have been
      /// registered with the objc runtime.
      bool Finalized = false;
    };

    struct SymbolTablePair {
      jitlink::Symbol *OriginalSym = nullptr;
      jitlink::Symbol *NameSym = nullptr;
    };
    using JITSymTabVector = SmallVector<SymbolTablePair>;

    Error bootstrapPipelineRecordRuntimeFunctions(jitlink::LinkGraph &G);
    Error bootstrapPipelineEnd(jitlink::LinkGraph &G);

    Error associateJITDylibHeaderSymbol(jitlink::LinkGraph &G,
                                        MaterializationResponsibility &MR);

    Error preserveImportantSections(jitlink::LinkGraph &G,
                                    MaterializationResponsibility &MR);

    Error processObjCImageInfo(jitlink::LinkGraph &G,
                               MaterializationResponsibility &MR);
    Error mergeImageInfoFlags(jitlink::LinkGraph &G,
                              MaterializationResponsibility &MR,
                              ObjCImageInfo &Info, uint32_t NewFlags);

    Error fixTLVSectionsAndEdges(jitlink::LinkGraph &G, JITDylib &JD);

    std::optional<UnwindSections> findUnwindSectionInfo(jitlink::LinkGraph &G);
    Error registerObjectPlatformSections(jitlink::LinkGraph &G, JITDylib &JD,
                                         ExecutorAddr HeaderAddr,
                                         bool InBootstrapPhase);

    Error createObjCRuntimeObject(jitlink::LinkGraph &G);
    Error populateObjCRuntimeObject(jitlink::LinkGraph &G,
                                    MaterializationResponsibility &MR);

    Error prepareSymbolTableRegistration(jitlink::LinkGraph &G,
                                         JITSymTabVector &JITSymTabInfo);
    Error addSymbolTableRegistration(jitlink::LinkGraph &G,
                                     MaterializationResponsibility &MR,
                                     JITSymTabVector &JITSymTabInfo,
                                     bool InBootstrapPhase);

    std::mutex PluginMutex;
    MachOPlatform &MP;

    // FIXME: ObjCImageInfos and HeaderAddrs need to be cleared when
    // JITDylibs are removed.
    DenseMap<JITDylib *, ObjCImageInfo> ObjCImageInfos;
  };

  using GetJITDylibHeaderSendResultFn =
      unique_function<void(Expected<ExecutorAddr>)>;
  using GetJITDylibNameSendResultFn =
      unique_function<void(Expected<StringRef>)>;
  using PushInitializersSendResultFn =
      unique_function<void(Expected<MachOJITDylibDepInfoMap>)>;
  using SendSymbolAddressFn = unique_function<void(Expected<ExecutorAddr>)>;
  using PushSymbolsInSendResultFn = unique_function<void(Error)>;

  static bool supportedTarget(const Triple &TT);

  static jitlink::Edge::Kind getPointerEdgeKind(jitlink::LinkGraph &G);

  static MachOExecutorSymbolFlags flagsForSymbol(jitlink::Symbol &Sym);

  MachOPlatform(ObjectLinkingLayer &ObjLinkingLayer, JITDylib &PlatformJD,
                std::unique_ptr<DefinitionGenerator> OrcRuntimeGenerator,
                HeaderOptionsBuilder BuildHeaderOpts,
                HeaderOptions PlatformJDOpts,
                MachOHeaderMUBuilder BuildMachOHeaderMU, Error &Err);

  // Associate MachOPlatform JIT-side runtime support functions with handlers.
  Error associateRuntimeSupportFunctions();

  // Implements rt_pushInitializers by making repeat async lookups for
  // initializer symbols (each lookup may spawn more initializer symbols if
  // it pulls in new materializers, e.g. from objects in a static library).
  void pushInitializersLoop(PushInitializersSendResultFn SendResult,
                            JITDylibSP JD);

  // Handle requests from the ORC runtime to push MachO initializer info.
  void rt_pushInitializers(PushInitializersSendResultFn SendResult,
                           ExecutorAddr JDHeaderAddr);

  // Request that that the given symbols be materialized. The bool element of
  // each pair indicates whether the symbol must be initialized, or whether it
  // is optional. If any required symbol is not found then the pushSymbols
  // function will return an error.
  void rt_pushSymbols(PushSymbolsInSendResultFn SendResult, ExecutorAddr Handle,
                      const std::vector<std::pair<StringRef, bool>> &Symbols);

  // Call the ORC runtime to create a pthread key.
  Expected<uint64_t> createPThreadKey();

  ExecutionSession &ES;
  JITDylib &PlatformJD;
  ObjectLinkingLayer &ObjLinkingLayer;
  HeaderOptionsBuilder BuildHeaderOpts;
  MachOHeaderMUBuilder BuildMachOHeaderMU;

  SymbolStringPtr MachOHeaderStartSymbol = ES.intern("___dso_handle");

  struct RuntimeFunction {
    RuntimeFunction(SymbolStringPtr Name) : Name(std::move(Name)) {}
    SymbolStringPtr Name;
    ExecutorAddr Addr;
  };

  RuntimeFunction PlatformBootstrap{
      ES.intern("___orc_rt_macho_platform_bootstrap")};
  RuntimeFunction PlatformShutdown{
      ES.intern("___orc_rt_macho_platform_shutdown")};
  RuntimeFunction RegisterEHFrameSection{
      ES.intern("___orc_rt_macho_register_ehframe_section")};
  RuntimeFunction DeregisterEHFrameSection{
      ES.intern("___orc_rt_macho_deregister_ehframe_section")};
  RuntimeFunction RegisterJITDylib{
      ES.intern("___orc_rt_macho_register_jitdylib")};
  RuntimeFunction DeregisterJITDylib{
      ES.intern("___orc_rt_macho_deregister_jitdylib")};
  RuntimeFunction RegisterObjectSymbolTable{
      ES.intern("___orc_rt_macho_register_object_symbol_table")};
  RuntimeFunction DeregisterObjectSymbolTable{
      ES.intern("___orc_rt_macho_deregister_object_symbol_table")};
  RuntimeFunction RegisterObjectPlatformSections{
      ES.intern("___orc_rt_macho_register_object_platform_sections")};
  RuntimeFunction DeregisterObjectPlatformSections{
      ES.intern("___orc_rt_macho_deregister_object_platform_sections")};
  RuntimeFunction CreatePThreadKey{
      ES.intern("___orc_rt_macho_create_pthread_key")};
  RuntimeFunction RegisterObjCRuntimeObject{
      ES.intern("___orc_rt_macho_register_objc_runtime_object")};
  RuntimeFunction DeregisterObjCRuntimeObject{
      ES.intern("___orc_rt_macho_deregister_objc_runtime_object")};

  DenseMap<JITDylib *, SymbolLookupSet> RegisteredInitSymbols;

  std::mutex PlatformMutex;
  bool ForceEHFrames = false;
  BootstrapInfo *Bootstrap = nullptr;
  DenseMap<JITDylib *, ExecutorAddr> JITDylibToHeaderAddr;
  DenseMap<ExecutorAddr, JITDylib *> HeaderAddrToJITDylib;
  DenseMap<JITDylib *, uint64_t> JITDylibToPThreadKey;
};

/// MaterializationUnit that generates a simple MachO header for a JITDylib.
class LLVM_ABI SimpleMachOHeaderMU : public MaterializationUnit {
public:
  /// Construct a simple MachO header materialization unit.
  /// @param MOP MachO platform that owns header state for the JITDylib.
  /// @param HeaderStartSymbol Symbol naming the start of the generated header.
  /// @param Opts Header load-command options to embed in the MachO header.
  SimpleMachOHeaderMU(MachOPlatform &MOP, SymbolStringPtr HeaderStartSymbol,
                      MachOPlatform::HeaderOptions Opts);
  /// Return the name of this materialization unit.
  /// @return The name of this materialization unit.
  StringRef getName() const override { return "MachOHeaderMU"; }
  /// Materialize the MachO header symbols for the responsible JITDylib.
  /// @param R Materialization responsibility for the header symbols.
  void materialize(std::unique_ptr<MaterializationResponsibility> R) override;
  /// Discard an overridden header symbol from this materialization unit.
  /// @param JD JITDylib that discarded the symbol.
  /// @param Sym Symbol that was overridden.
  void discard(const JITDylib &JD, const SymbolStringPtr &Sym) override;

protected:
  /// Create the jitlink block that holds the synthesized MachO header.
  /// @param JD JITDylib whose header is being created.
  /// @param G Link graph that will own the header block.
  /// @param HeaderSection Section that will contain the header block.
  /// @return The jitlink block containing the synthesized MachO header.
  virtual jitlink::Block &createHeaderBlock(JITDylib &JD, jitlink::LinkGraph &G,
                                            jitlink::Section &HeaderSection);

  /// MachO platform associated with this header materialization unit.
  MachOPlatform &MOP;
  /// Header options used when synthesizing the MachO header.
  MachOPlatform::HeaderOptions Opts;

private:
  struct HeaderSymbol {
    const char *Name;
    uint64_t Offset;
  };

  static constexpr HeaderSymbol AdditionalHeaderSymbols[] = {
      {"___mh_executable_header", 0}};

  void addMachOHeader(JITDylib &JD, jitlink::LinkGraph &G,
                      const SymbolStringPtr &InitializerSymbol);
  static MaterializationUnit::Interface
  createHeaderInterface(MachOPlatform &MOP,
                        const SymbolStringPtr &HeaderStartSymbol);
};

/// Simple MachO header graph builder.
/// @param MOP MachO platform that owns header state for the JITDylib.
/// @param Opts Header load-command options to embed in the MachO header.
/// @return A MaterializationUnit that synthesizes a simple MachO header.
inline std::unique_ptr<MaterializationUnit>
MachOPlatform::buildSimpleMachOHeaderMU(MachOPlatform &MOP,
                                        HeaderOptions Opts) {
  return std::make_unique<SimpleMachOHeaderMU>(MOP, MOP.MachOHeaderStartSymbol,
                                               std::move(Opts));
}

/// MachO header page size and CPU type information for a target triple.
struct MachOHeaderInfo {
  /// Preferred page size for the target architecture.
  size_t PageSize = 0;
  /// MachO CPU type identifier.
  uint32_t CPUType = 0;
  /// MachO CPU subtype identifier.
  uint32_t CPUSubType = 0;
};
/// Return MachO header info appropriate for the given target triple.
/// @param TT Target triple used to select page size and CPU identifiers.
/// @return Page size and MachO CPU type/subtype for \p TT.
LLVM_ABI MachOHeaderInfo getMachOHeaderInfoFromTriple(const Triple &TT);

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_MACHOPLATFORM_H
