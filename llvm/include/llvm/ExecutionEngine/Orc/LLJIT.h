//===----- LLJIT.h -- An ORC-based JIT for compiling LLVM IR ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// An ORC-based JIT for compiling LLVM IR.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_LLJIT_H
#define LLVM_EXECUTIONENGINE_ORC_LLJIT_H

#include "llvm/ADT/SmallSet.h"
#include "llvm/ExecutionEngine/Orc/AbsoluteSymbols.h"
#include "llvm/ExecutionEngine/Orc/CompileOnDemandLayer.h"
#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/DylibManager.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
#include "llvm/ExecutionEngine/Orc/IRPartitionLayer.h"
#include "llvm/ExecutionEngine/Orc/IRTransformLayer.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ThreadPool.h"
#include <variant>

namespace llvm {

namespace orc {

class LLJITBuilderState;
class LLLazyJITBuilderState;
class ObjectTransformLayer;
class ExecutorProcessControl;

/// A pre-fabricated ORC JIT stack that can serve as an alternative to MCJIT.
///
/// Create instances using LLJITBuilder.
class LLVM_ABI LLJIT {
  template <typename, typename, typename> friend class LLJITBuilderSetters;

  LLVM_ABI friend Expected<JITDylibSP> setUpGenericLLVMIRPlatform(LLJIT &J);

public:
  /// Initializer support for LLJIT.
  class LLVM_ABI PlatformSupport {
  public:
    /// Destroy this PlatformSupport instance.
    virtual ~PlatformSupport();

    /// Run platform-specific initializers for the given JITDylib.
    /// \param JD JITDylib to initialize.
    /// \return Success, or an error if initialization fails.
    virtual Error initialize(JITDylib &JD) = 0;

    /// Run platform-specific deinitializers for the given JITDylib.
    /// \param JD JITDylib to deinitialize.
    /// \return Success, or an error if deinitialization fails.
    virtual Error deinitialize(JITDylib &JD) = 0;

  protected:
    /// Install an IR transform used to generate initializer helpers.
    /// \param J LLJIT instance whose transform layer will be updated.
    /// \param T Transform that rewrites modules to include init helpers.
    static void setInitTransform(LLJIT &J,
                                 IRTransformLayer::TransformFunction T);
  };

  /// Destruct this instance. If a multi-threaded instance, waits for all
  /// compile threads to complete.
  virtual ~LLJIT();

  /// Returns the ExecutionSession for this instance.
  /// \return Reference to the ExecutionSession for this instance.
  ExecutionSession &getExecutionSession() { return *ES; }

  /// Returns a reference to the triple for this instance.
  /// \return Reference to the target triple for this instance.
  const Triple &getTargetTriple() const { return TT; }

  /// Returns a reference to the DataLayout for this instance.
  /// \return Reference to the DataLayout for this instance.
  const DataLayout &getDataLayout() const { return DL; }

  /// Returns a reference to the JITDylib representing the JIT'd main program.
  /// \return Reference to the main JITDylib.
  JITDylib &getMainJITDylib() { return *Main; }

  /// Returns the ProcessSymbols JITDylib, which by default reflects non-JIT'd
  /// symbols in the host process.
  ///
  /// Note: JIT'd code should not be added to the ProcessSymbols JITDylib. Use
  /// the main JITDylib or a custom JITDylib instead.
  /// \return Shared pointer to the ProcessSymbols JITDylib, or nullptr if
  ///         none is set.
  JITDylibSP getProcessSymbolsJITDylib();

  /// Returns the Platform JITDylib, which will contain the ORC runtime (if
  /// given) and any platform symbols.
  ///
  /// Note: JIT'd code should not be added to the Platform JITDylib. Use the
  /// main JITDylib or a custom JITDylib instead.
  /// \return Shared pointer to the Platform JITDylib, or nullptr if none is
  ///         set.
  JITDylibSP getPlatformJITDylib();

  /// Returns the JITDylib with the given name, or nullptr if no JITDylib with
  /// that name exists.
  /// \param Name Name of the JITDylib to look up.
  /// \return Pointer to the JITDylib with the given name, or nullptr if none
  ///         exists.
  JITDylib *getJITDylibByName(StringRef Name) {
    return ES->getJITDylibByName(Name);
  }

  /// Load a (real) dynamic library and make its symbols available through a
  /// new JITDylib with the same name.
  ///
  /// If the given *executor* path contains a valid platform dynamic library
  /// then that library will be loaded, and a new bare JITDylib whose name is
  /// the given path will be created to make the library's symbols available to
  /// JIT'd code.
  /// \param Path Executor-side path to the dynamic library to load.
  /// \return Reference to the new JITDylib, or an error if loading fails.
  Expected<JITDylib &> loadPlatformDynamicLibrary(const char *Path);

  /// Link a static library into the given JITDylib.
  ///
  /// If the given MemoryBuffer contains a valid static archive (or a universal
  /// binary with an archive slice that fits the LLJIT instance's platform /
  /// architecture) then it will be added to the given JITDylib using a
  /// StaticLibraryDefinitionGenerator.
  /// \param JD JITDylib that will receive the archive definitions.
  /// \param LibBuffer Buffer containing the static archive (or universal
  ///        binary) to link.
  /// \return Success, or an error if the library cannot be linked.
  Error linkStaticLibraryInto(JITDylib &JD,
                              std::unique_ptr<MemoryBuffer> LibBuffer);

  /// Link a static library into the given JITDylib.
  ///
  /// If the given *host* path contains a valid static archive (or a universal
  /// binary with an archive slice that fits the LLJIT instance's platform /
  /// architecture) then it will be added to the given JITDylib using a
  /// StaticLibraryDefinitionGenerator.
  /// \param JD JITDylib that will receive the archive definitions.
  /// \param Path Host-side path to the static archive to link.
  /// \return Success, or an error if the library cannot be linked.
  Error linkStaticLibraryInto(JITDylib &JD, const char *Path);

  /// Create a new JITDylib with the given name and return a reference to it.
  ///
  /// JITDylib names must be unique. If the given name is derived from user
  /// input or elsewhere in the environment then the client should check
  /// (e.g. by calling getJITDylibByName) that the given name is not already in
  /// use.
  /// \param Name Unique name for the new JITDylib.
  /// \return Reference to the new JITDylib, or an error if creation fails.
  Expected<JITDylib &> createJITDylib(std::string Name);

  /// Returns the default link order for this LLJIT instance.
  ///
  /// This link order will be appended to the link order of JITDylibs created by
  /// LLJIT's createJITDylib method.
  /// \return The default link order for this LLJIT instance.
  JITDylibSearchOrder defaultLinkOrder() { return DefaultLinks; }

  /// Adds an IR module with the given ResourceTracker.
  /// \param RT Resource tracker that owns the module's definitions.
  /// \param TSM Thread-safe module to add.
  /// \return Success, or an error if the module cannot be added.
  Error addIRModule(ResourceTrackerSP RT, ThreadSafeModule TSM);

  /// Adds an IR module to the given JITDylib.
  /// \param JD JITDylib that will own the module's definitions.
  /// \param TSM Thread-safe module to add.
  /// \return Success, or an error if the module cannot be added.
  Error addIRModule(JITDylib &JD, ThreadSafeModule TSM);

  /// Adds an IR module to the Main JITDylib.
  /// \param TSM Thread-safe module to add.
  /// \return Success, or an error if the module cannot be added.
  Error addIRModule(ThreadSafeModule TSM) {
    return addIRModule(*Main, std::move(TSM));
  }

  /// Adds an object file to the given JITDylib.
  /// \param RT Resource tracker that owns the object file's definitions.
  /// \param Obj Buffer containing the object file to add.
  /// \return Success, or an error if the object file cannot be added.
  Error addObjectFile(ResourceTrackerSP RT, std::unique_ptr<MemoryBuffer> Obj);

  /// Adds an object file to the given JITDylib.
  /// \param JD JITDylib that will own the object file's definitions.
  /// \param Obj Buffer containing the object file to add.
  /// \return Success, or an error if the object file cannot be added.
  Error addObjectFile(JITDylib &JD, std::unique_ptr<MemoryBuffer> Obj);

  /// Adds an object file to the given JITDylib.
  /// \param Obj Buffer containing the object file to add.
  /// \return Success, or an error if the object file cannot be added.
  Error addObjectFile(std::unique_ptr<MemoryBuffer> Obj) {
    return addObjectFile(*Main, std::move(Obj));
  }

  /// Look up a symbol in JITDylib JD by the symbol's linker-mangled name (to
  /// look up symbols based on their IR name use the lookup function instead).
  /// \param JD JITDylib to search.
  /// \param Name Linker-mangled symbol name to look up.
  /// \return Address of the symbol, or an error if lookup fails.
  Expected<ExecutorAddr> lookupLinkerMangled(JITDylib &JD,
                                             SymbolStringPtr Name);

  /// Look up a symbol in JITDylib JD by the symbol's linker-mangled name (to
  /// look up symbols based on their IR name use the lookup function instead).
  /// \param JD JITDylib to search.
  /// \param Name Linker-mangled symbol name to look up.
  /// \return Address of the symbol, or an error if lookup fails.
  Expected<ExecutorAddr> lookupLinkerMangled(JITDylib &JD,
                                             StringRef Name) {
    return lookupLinkerMangled(JD, ES->intern(Name));
  }

  /// Look up a symbol in the main JITDylib by the symbol's linker-mangled name
  /// (to look up symbols based on their IR name use the lookup function
  /// instead).
  /// \param Name Linker-mangled symbol name to look up.
  /// \return Address of the symbol, or an error if lookup fails.
  Expected<ExecutorAddr> lookupLinkerMangled(StringRef Name) {
    return lookupLinkerMangled(*Main, Name);
  }

  /// Look up a symbol in JITDylib JD based on its IR symbol name.
  /// \param JD JITDylib to search.
  /// \param UnmangledName IR-level (unmangled) symbol name to look up.
  /// \return Address of the symbol, or an error if lookup fails.
  Expected<ExecutorAddr> lookup(JITDylib &JD, StringRef UnmangledName) {
    return lookupLinkerMangled(JD, mangle(UnmangledName));
  }

  /// Look up a symbol in the main JITDylib based on its IR symbol name.
  /// \param UnmangledName IR-level (unmangled) symbol name to look up.
  /// \return Address of the symbol, or an error if lookup fails.
  Expected<ExecutorAddr> lookup(StringRef UnmangledName) {
    return lookup(*Main, UnmangledName);
  }

  /// Set the PlatformSupport instance.
  /// \param PS Platform support implementation to install.
  void setPlatformSupport(std::unique_ptr<PlatformSupport> PS) {
    this->PS = std::move(PS);
  }

  /// Get the PlatformSupport instance.
  /// \return Pointer to the PlatformSupport instance, or nullptr if none is
  ///         set.
  PlatformSupport *getPlatformSupport() { return PS.get(); }

  /// Run the initializers for the given JITDylib.
  /// \param JD JITDylib whose initializers should be run.
  /// \return Success, or an error if initialization fails.
  Error initialize(JITDylib &JD) {
    DEBUG_WITH_TYPE("orc", {
      dbgs() << "LLJIT running initializers for JITDylib \"" << JD.getName()
             << "\"\n";
    });
    assert(PS && "PlatformSupport must be set to run initializers.");
    return PS->initialize(JD);
  }

  /// Run the deinitializers for the given JITDylib.
  /// \param JD JITDylib whose deinitializers should be run.
  /// \return Success, or an error if deinitialization fails.
  Error deinitialize(JITDylib &JD) {
    DEBUG_WITH_TYPE("orc", {
      dbgs() << "LLJIT running deinitializers for JITDylib \"" << JD.getName()
             << "\"\n";
    });
    assert(PS && "PlatformSupport must be set to run initializers.");
    return PS->deinitialize(JD);
  }

  /// Returns a reference to the DylibManager for the target process.
  /// \return Reference to the DylibManager for the target process.
  DylibManager &getDylibMgr() {
    assert(DylibMgr && "No DylibMgr set");
    return *DylibMgr;
  }

  /// Returns a reference to the JITLinkMemoryManager for this instance.
  /// \return Reference to the JITLink memory manager.
  jitlink::JITLinkMemoryManager &getMemoryManager() {
    assert(MemMgr && "No MemMgr set");
    return *MemMgr;
  }

  /// Returns a reference to the ObjLinkingLayer
  /// \return Reference to the object linking layer.
  ObjectLayer &getObjLinkingLayer() { return *ObjLinkingLayer; }

  /// Returns a reference to the object transform layer.
  /// \return Reference to the object transform layer.
  ObjectTransformLayer &getObjTransformLayer() { return *ObjTransformLayer; }

  /// Returns a reference to the IR transform layer.
  /// \return Reference to the IR transform layer.
  IRTransformLayer &getIRTransformLayer() { return *TransformLayer; }

  /// Returns a reference to the IR compile layer.
  /// \return Reference to the IR compile layer.
  IRCompileLayer &getIRCompileLayer() { return *CompileLayer; }

  /// Returns a linker-mangled version of UnmangledName.
  /// \param UnmangledName IR-level symbol name to mangle.
  /// \return Linker-mangled version of \p UnmangledName.
  std::string mangle(StringRef UnmangledName) const;

  /// Returns an interned, linker-mangled version of UnmangledName.
  /// \param UnmangledName IR-level symbol name to mangle and intern.
  /// \return Interned, linker-mangled version of \p UnmangledName.
  SymbolStringPtr mangleAndIntern(StringRef UnmangledName) const {
    return ES->intern(mangle(UnmangledName));
  }

protected:
  /// Create a JITLink memory manager for the given builder state.
  /// \param S Builder state supplying memory-manager creation options.
  /// \param ES Execution session that will own the memory manager.
  /// \return The memory manager, or an error if creation fails.
  static Expected<std::unique_ptr<jitlink::JITLinkMemoryManager>>
  createMemoryManager(LLJITBuilderState &S, ExecutionSession &ES);

  /// Create an object linking layer for the given builder state.
  /// \param S Builder state supplying object-linking-layer creation options.
  /// \param ES Execution session that will own the layer.
  /// \param MemMgr Memory manager used by the linking layer.
  /// \return The object linking layer, or an error if creation fails.
  static Expected<std::unique_ptr<ObjectLayer>>
  createObjectLinkingLayer(LLJITBuilderState &S, ExecutionSession &ES,
                           jitlink::JITLinkMemoryManager &MemMgr);

  /// Create an IR compiler for the given builder state and target.
  /// \param S Builder state supplying compile-function creation options.
  /// \param JTMB Target machine builder used to construct the compiler.
  /// \return The IR compiler, or an error if creation fails.
  static Expected<std::unique_ptr<IRCompileLayer::IRCompiler>>
  createCompileFunction(LLJITBuilderState &S, JITTargetMachineBuilder JTMB);

  /// Create an LLJIT instance with a single compile thread.
  /// \param S Builder state used to configure this instance.
  /// \param Err Error out-parameter set if construction fails.
  LLJIT(LLJITBuilderState &S, Error &Err);

  /// Apply target-specific configuration to the given module.
  /// \param M Module to configure for this LLJIT instance's target.
  /// \return Success, or an error if target configuration fails.
  Error applyTargetConfig(Module &M);

  /// Execution session owned by this LLJIT instance.
  std::unique_ptr<ExecutionSession> ES;
  /// Memory manager used by the object linking layer.
  std::unique_ptr<jitlink::JITLinkMemoryManager> MemMgr;
  /// Platform support implementation for initialize / deinitialize.
  std::unique_ptr<PlatformSupport> PS;
  /// Manager for loading process and platform dynamic libraries.
  std::unique_ptr<DylibManager> DylibMgr;

  /// JITDylib reflecting non-JIT'd symbols in the host process.
  JITDylib *ProcessSymbols = nullptr;
  /// JITDylib holding the ORC runtime and platform symbols.
  JITDylib *Platform = nullptr;
  /// JITDylib representing the JIT'd main program.
  JITDylib *Main = nullptr;

  /// Default link order appended to newly created JITDylibs.
  JITDylibSearchOrder DefaultLinks;

  /// Data layout used for mangling and IR compilation.
  DataLayout DL;
  /// Target triple for this LLJIT instance.
  Triple TT;

  /// Object linking layer for this LLJIT instance.
  std::unique_ptr<ObjectLayer> ObjLinkingLayer;
  /// Object transform layer wrapping the object linking layer.
  std::unique_ptr<ObjectTransformLayer> ObjTransformLayer;
  /// IR compile layer for this LLJIT instance.
  std::unique_ptr<IRCompileLayer> CompileLayer;
  /// IR transform layer applied before compilation.
  std::unique_ptr<IRTransformLayer> TransformLayer;
  /// IR transform layer used to generate initializer helpers.
  std::unique_ptr<IRTransformLayer> InitHelperTransformLayer;
};

/// An extended version of LLJIT that supports lazy function-at-a-time
/// compilation of LLVM IR.
class LLLazyJIT : public LLJIT {
  template <typename, typename, typename> friend class LLJITBuilderSetters;

public:

  /// Sets the partition function.
  /// \param Partition Function used to partition modules for lazy compilation.
  void setPartitionFunction(IRPartitionLayer::PartitionFunction Partition) {
    IPLayer->setPartitionFunction(std::move(Partition));
  }

  /// Destroy this LLLazyJIT instance.
  LLVM_ABI ~LLLazyJIT();

  /// Returns a reference to the on-demand layer.
  /// \return Reference to the compile-on-demand layer.
  CompileOnDemandLayer &getCompileOnDemandLayer() { return *CODLayer; }

  /// Add a module to be lazily compiled to JITDylib JD.
  /// \param JD JITDylib that will own the module's definitions.
  /// \param M Thread-safe module to add for lazy compilation.
  /// \return Success, or an error if the module cannot be added.
  LLVM_ABI Error addLazyIRModule(JITDylib &JD, ThreadSafeModule M);

  /// Add a module to be lazily compiled to the main JITDylib.
  /// \param M Thread-safe module to add for lazy compilation.
  /// \return Success, or an error if the module cannot be added.
  Error addLazyIRModule(ThreadSafeModule M) {
    return addLazyIRModule(*Main, std::move(M));
  }

private:

  // Create a single-threaded LLLazyJIT instance.
  LLVM_ABI LLLazyJIT(LLLazyJITBuilderState &S, Error &Err);

  std::unique_ptr<LazyCallThroughManager> LCTMgr;
  std::unique_ptr<IRPartitionLayer> IPLayer;
  std::unique_ptr<CompileOnDemandLayer> CODLayer;
};

/// Holds configuration state used to construct an LLJIT instance.
class LLJITBuilderState {
public:
  /// Functor that creates a JITLink memory manager for an execution session.
  using MemoryManagerCreator =
      std::function<Expected<std::unique_ptr<jitlink::JITLinkMemoryManager>>(
          ExecutionSession &)>;

  /// Functor that creates an object linking layer.
  using ObjectLinkingLayerCreator =
      std::function<Expected<std::unique_ptr<ObjectLayer>>(
          ExecutionSession &, jitlink::JITLinkMemoryManager &)>;

  /// Functor that creates an IR compiler for a target machine builder.
  using CompileFunctionCreator =
      std::function<Expected<std::unique_ptr<IRCompileLayer::IRCompiler>>(
          JITTargetMachineBuilder JTMB)>;

  /// Functor that sets up the process-symbols JITDylib for an LLJIT instance.
  using ProcessSymbolsJITDylibSetupFunction =
      unique_function<Expected<JITDylibSP>(LLJIT &J)>;

  /// Functor that configures platform support for an LLJIT instance.
  using PlatformSetupFunction = unique_function<Expected<JITDylibSP>(LLJIT &J)>;

  /// Callback invoked after a successful LLJIT construction.
  using NotifyCreatedFunction = std::function<Error(LLJIT &)>;

  /// Optional executor process control used when no ExecutionSession is set.
  std::unique_ptr<ExecutorProcessControl> EPC;
  /// Optional execution session; created from EPC if not provided.
  std::unique_ptr<ExecutionSession> ES;
  /// Optional JIT target machine builder; host is detected if unset.
  std::optional<JITTargetMachineBuilder> JTMB;
  /// Optional data layout; the target default is used if unset.
  std::optional<DataLayout> DL;
  /// Whether the process-symbols JITDylib is appended to the default link
  /// order.
  bool LinkProcessSymbolsByDefault = true;
  /// Optional setup function for the process-symbols JITDylib.
  ProcessSymbolsJITDylibSetupFunction SetupProcessSymbolsJITDylib;
  /// Optional creator for the JITLink memory manager.
  MemoryManagerCreator CreateMemoryManager;
  /// Optional creator for the object linking layer.
  ObjectLinkingLayerCreator CreateObjectLinkingLayer;
  /// Optional creator for the IR compile function.
  CompileFunctionCreator CreateCompileFunction;
  /// Optional setup run immediately before platform setup.
  unique_function<Error(LLJIT &)> PrePlatformSetup;
  /// Optional platform setup function.
  PlatformSetupFunction SetUpPlatform;
  /// Optional callback invoked after successful LLJIT construction.
  NotifyCreatedFunction NotifyCreated;
  /// Number of compile threads; zero means compile on the execution thread.
  unsigned NumCompileThreads = 0;
  /// Optional override for concurrent compilation support.
  std::optional<bool> SupportConcurrentCompilation;

  /// Called prior to JIT class construcion to fix up defaults.
  /// \return Success, or an error if defaults cannot be prepared.
  LLVM_ABI Error prepareForConstruction();
};

/// CRTP helper that provides setters for LLJIT builder configuration.
template <typename JITType, typename SetterImpl, typename State>
class LLJITBuilderSetters {
public:
  /// Set an ExecutorProcessControl for this instance.
  /// This should not be called if ExecutionSession has already been set.
  /// \param EPC Executor process control to install.
  /// \return Reference to this builder for chaining.
  SetterImpl &
  setExecutorProcessControl(std::unique_ptr<ExecutorProcessControl> EPC) {
    assert(
        !impl().ES &&
        "setExecutorProcessControl should not be called if an ExecutionSession "
        "has already been set");
    impl().EPC = std::move(EPC);
    return impl();
  }

  /// Set an ExecutionSession for this instance.
  /// \param ES Execution session to install.
  /// \return Reference to this builder for chaining.
  SetterImpl &setExecutionSession(std::unique_ptr<ExecutionSession> ES) {
    assert(
        !impl().EPC &&
        "setExecutionSession should not be called if an ExecutorProcessControl "
        "object has already been set");
    impl().ES = std::move(ES);
    return impl();
  }

  /// Set the JITTargetMachineBuilder for this instance.
  ///
  /// If this method is not called, JITTargetMachineBuilder::detectHost will be
  /// used to construct a default target machine builder for the host platform.
  /// \param JTMB Target machine builder to use.
  /// \return Reference to this builder for chaining.
  SetterImpl &setJITTargetMachineBuilder(JITTargetMachineBuilder JTMB) {
    impl().JTMB = std::move(JTMB);
    return impl();
  }

  /// Return a reference to the JITTargetMachineBuilder.
  ///
  /// \return Reference to the optional JITTargetMachineBuilder.
  std::optional<JITTargetMachineBuilder> &getJITTargetMachineBuilder() {
    return impl().JTMB;
  }

  /// Set a DataLayout for this instance. If no data layout is specified then
  /// the target's default data layout will be used.
  /// \param DL Data layout to use, or std::nullopt for the target default.
  /// \return Reference to this builder for chaining.
  SetterImpl &setDataLayout(std::optional<DataLayout> DL) {
    impl().DL = std::move(DL);
    return impl();
  }

  /// The LinkProcessSymbolsDyDefault flag determines whether the "Process" JITDylib will be.
  ///
  /// added to the default link order at LLJIT construction time. If true, the Process JITDylib will be added as the last item in the default link order. If false (or if the Process JITDylib is disabled via setProcessSymbolsJITDylibSetup) then the Process JITDylib will not appear in the default link order.
  /// \param LinkProcessSymbolsByDefault Whether to append the process-symbols
  ///        JITDylib to the default link order.
  /// \return Reference to this builder for chaining.
  SetterImpl &setLinkProcessSymbolsByDefault(bool LinkProcessSymbolsByDefault) {
    impl().LinkProcessSymbolsByDefault = LinkProcessSymbolsByDefault;
    return impl();
  }

  /// Set a memory manager creation function. If not provided then the
  /// ExecutorProcessControl's createDefaultMemoryManager method will be used.
  /// \param CreateMemoryManager Functor used to create the memory manager.
  /// \return Reference to this builder for chaining.
  SetterImpl &setMemoryManagerCreator(
      LLJITBuilderState::MemoryManagerCreator CreateMemoryManager) {
    impl().CreateMemoryManager = std::move(CreateMemoryManager);
    return impl();
  }

  /// Set a setup function for the process symbols dylib.
  ///
  /// If not provided, but LinkProcessSymbolsJITDylibByDefault is true, then the process-symbols JITDylib will be configured with a DynamicLibrarySearchGenerator with a default symbol filter.
  /// \param SetupProcessSymbolsJITDylib Functor used to set up the
  ///        process-symbols JITDylib.
  /// \return Reference to this builder for chaining.
  SetterImpl &setProcessSymbolsJITDylibSetup(
      LLJITBuilderState::ProcessSymbolsJITDylibSetupFunction
          SetupProcessSymbolsJITDylib) {
    impl().SetupProcessSymbolsJITDylib = std::move(SetupProcessSymbolsJITDylib);
    return impl();
  }

  /// Set an ObjectLinkingLayer creation function.
  ///
  /// If this method is not called, a default creation function will be used
  /// that will construct an RTDyldObjectLinkingLayer.
  /// \param CreateObjectLinkingLayer Functor used to create the object linking
  ///        layer.
  /// \return Reference to this builder for chaining.
  SetterImpl &setObjectLinkingLayerCreator(
      LLJITBuilderState::ObjectLinkingLayerCreator CreateObjectLinkingLayer) {
    impl().CreateObjectLinkingLayer = std::move(CreateObjectLinkingLayer);
    return impl();
  }

  /// Set a CompileFunctionCreator.
  ///
  /// If this method is not called, a default creation function wil be used
  /// that will construct a basic IR compile function that is compatible with
  /// the selected number of threads (SimpleCompiler for '0' compile threads,
  /// ConcurrentIRCompiler otherwise).
  /// \param CreateCompileFunction Functor used to create the IR compiler.
  /// \return Reference to this builder for chaining.
  SetterImpl &setCompileFunctionCreator(
      LLJITBuilderState::CompileFunctionCreator CreateCompileFunction) {
    impl().CreateCompileFunction = std::move(CreateCompileFunction);
    return impl();
  }

  /// Set a setup function to be run just before the PlatformSetupFunction is
  /// run.
  ///
  /// This can be used to customize the LLJIT instance before the platform is
  /// set up. E.g. By installing a debugger support plugin before the platform
  /// is set up (when the ORC runtime is loaded) we enable debugging of the
  /// runtime itself.
  /// \param PrePlatformSetup Functor run immediately before platform setup.
  /// \return Reference to this builder for chaining.
  SetterImpl &
  setPrePlatformSetup(unique_function<Error(LLJIT &)> PrePlatformSetup) {
    impl().PrePlatformSetup = std::move(PrePlatformSetup);
    return impl();
  }

  /// Set up an PlatformSetupFunction.
  ///
  /// If this method is not called then setUpGenericLLVMIRPlatform
  /// will be used to configure the JIT's platform support.
  /// \param SetUpPlatform Functor used to configure platform support.
  /// \return Reference to this builder for chaining.
  SetterImpl &
  setPlatformSetUp(LLJITBuilderState::PlatformSetupFunction SetUpPlatform) {
    impl().SetUpPlatform = std::move(SetUpPlatform);
    return impl();
  }

  /// Set up a callback after successful construction of the JIT.
  ///
  /// This is useful to attach generators to JITDylibs or inject initial symbol
  /// definitions.
  /// \param Callback Functor invoked after a successful create().
  /// \return Reference to this builder for chaining.
  SetterImpl &
  setNotifyCreatedCallback(LLJITBuilderState::NotifyCreatedFunction Callback) {
    impl().NotifyCreated = std::move(Callback);
    return impl();
  }

  /// Set the number of compile threads to use.
  ///
  /// If set to zero, compilation will be performed on the execution thread when
  /// JITing in-process. If set to any other number N, a thread pool of N
  /// threads will be created for compilation.
  ///
  /// If this method is not called, behavior will be as if it were called with
  /// a zero argument.
  ///
  /// This setting should not be used if a custom ExecutionSession or
  /// ExecutorProcessControl object is set: in those cases a custom
  /// TaskDispatcher should be used instead.
  /// \param NumCompileThreads Number of compile threads to create.
  /// \return Reference to this builder for chaining.
  SetterImpl &setNumCompileThreads(unsigned NumCompileThreads) {
    impl().NumCompileThreads = NumCompileThreads;
    return impl();
  }

  /// If set, this forces LLJIT concurrent compilation support to be either on or off.
  ///
  /// This controls the selection of compile function (concurrent vs single threaded) and whether or not sub-modules are cloned to new contexts for lazy emission. If not explicitly set then concurrency support will be turned on if NumCompileThreads is set to a non-zero value, or if a custom ExecutionSession or ExecutorProcessControl instance is provided.
  /// \param SupportConcurrentCompilation Optional override for concurrent
  ///        compilation support.
  /// \return Reference to this builder for chaining.
  SetterImpl &setSupportConcurrentCompilation(
      std::optional<bool> SupportConcurrentCompilation) {
    impl().SupportConcurrentCompilation = SupportConcurrentCompilation;
    return impl();
  }

  /// Create an instance of the JIT.
  /// \return The constructed JIT instance, or an error if construction fails.
  Expected<std::unique_ptr<JITType>> create() {
    if (auto Err = impl().prepareForConstruction())
      return std::move(Err);

    Error Err = Error::success();
    std::unique_ptr<JITType> J(new JITType(impl(), Err));
    if (Err)
      return std::move(Err);

    if (impl().NotifyCreated)
      if (Error Err = impl().NotifyCreated(*J))
        return std::move(Err);

    return std::move(J);
  }

protected:
  /// Return this builder as its most-derived setter type.
  /// \return Reference to this builder as its most-derived setter type.
  SetterImpl &impl() { return static_cast<SetterImpl &>(*this); }
};

/// Constructs LLJIT instances.
class LLJITBuilder
    : public LLJITBuilderState,
      public LLJITBuilderSetters<LLJIT, LLJITBuilder, LLJITBuilderState> {};

/// Holds configuration state used to construct an LLLazyJIT instance.
class LLLazyJITBuilderState : public LLJITBuilderState {
  friend class LLLazyJIT;

public:
  /// Functor that creates an IndirectStubsManager.
  using IndirectStubsManagerBuilderFunction =
      std::function<std::unique_ptr<IndirectStubsManager>()>;

  /// Target triple used when constructing lazy compilation support.
  Triple TT;
  /// Address called if a lazy compile fails; defaults to zero if unset.
  ExecutorAddr LazyCompileFailureAddr;
  /// Optional lazy call-through manager; a default is created if unset.
  std::unique_ptr<LazyCallThroughManager> LCTMgr;
  /// Optional IndirectStubsManager builder; a default is used if unset.
  IndirectStubsManagerBuilderFunction ISMBuilder;

  /// Called prior to LLLazyJIT construction to fix up defaults.
  /// \return Success, or an error if defaults cannot be prepared.
  LLVM_ABI Error prepareForConstruction();
};

/// CRTP helper that provides setters for LLLazyJIT builder configuration.
template <typename JITType, typename SetterImpl, typename State>
class LLLazyJITBuilderSetters
    : public LLJITBuilderSetters<JITType, SetterImpl, State> {
public:
  /// Set the address in the target address to call if a lazy compile fails.
  ///
  /// If this method is not called then the value will default to 0.
  /// \param Addr Executor address to call on lazy-compile failure.
  /// \return Reference to this builder for chaining.
  SetterImpl &setLazyCompileFailureAddr(ExecutorAddr Addr) {
    this->impl().LazyCompileFailureAddr = Addr;
    return this->impl();
  }

  /// Set the lazy-callthrough manager.
  ///
  /// If this method is not called then a default, in-process lazy callthrough
  /// manager for the host platform will be used.
  /// \param LCTMgr Lazy call-through manager to install.
  /// \return Reference to this builder for chaining.
  SetterImpl &
  setLazyCallthroughManager(std::unique_ptr<LazyCallThroughManager> LCTMgr) {
    this->impl().LCTMgr = std::move(LCTMgr);
    return this->impl();
  }

  /// Set the IndirectStubsManager builder function.
  ///
  /// If this method is not called then a default, in-process
  /// IndirectStubsManager builder for the host platform will be used.
  /// \param ISMBuilder Functor used to create IndirectStubsManagers.
  /// \return Reference to this builder for chaining.
  SetterImpl &setIndirectStubsManagerBuilder(
      LLLazyJITBuilderState::IndirectStubsManagerBuilderFunction ISMBuilder) {
    this->impl().ISMBuilder = std::move(ISMBuilder);
    return this->impl();
  }
};

/// Constructs LLLazyJIT instances.
class LLLazyJITBuilder
    : public LLLazyJITBuilderState,
      public LLLazyJITBuilderSetters<LLLazyJIT, LLLazyJITBuilder,
                                     LLLazyJITBuilderState> {};

/// Configure the LLJIT instance to use orc runtime support. This overload
/// assumes that the client has manually configured a Platform object.
/// \param J LLJIT instance to configure.
/// \return Success, or an error if configuration fails.
LLVM_ABI Error setUpOrcPlatformManually(LLJIT &J);

/// Configure the LLJIT instance to use the ORC runtime and the detected
/// native target for the executor.
class ExecutorNativePlatform {
public:
  /// Set up using path to Orc runtime.
  /// \param OrcRuntimePath Host path to the ORC runtime library.
  ExecutorNativePlatform(std::string OrcRuntimePath)
      : OrcRuntime(std::move(OrcRuntimePath)) {}

  /// Set up using the given memory buffer.
  /// \param OrcRuntimeMB Buffer containing the ORC runtime library.
  ExecutorNativePlatform(std::unique_ptr<MemoryBuffer> OrcRuntimeMB)
      : OrcRuntime(std::move(OrcRuntimeMB)) {}

  // TODO: add compiler-rt.

  /// Add a path to the VC runtime.
  /// \param VCRuntimePath Host path to the Visual C++ runtime.
  /// \param StaticVCRuntime Whether to link the static VC runtime.
  /// \return Reference to this platform configuration for chaining.
  ExecutorNativePlatform &addVCRuntime(std::string VCRuntimePath,
                                       bool StaticVCRuntime) {
    VCRuntime = {std::move(VCRuntimePath), StaticVCRuntime};
    return *this;
  }

  /// Configure \p J to use the ORC runtime and detected native platform.
  /// \param J LLJIT instance to configure.
  /// \return The platform JITDylib, or an error if configuration fails.
  LLVM_ABI Expected<JITDylibSP> operator()(LLJIT &J);

private:
  std::variant<std::string, std::unique_ptr<MemoryBuffer>> OrcRuntime;
  std::optional<std::pair<std::string, bool>> VCRuntime;
};

/// Configure the LLJIT instance for generic LLVM IR platform support.
///
/// Scrapes modules for llvm.global_ctors and llvm.global_dtors variables and
/// (if present) builds initialization and deinitialization functions. Platform
/// specific initialization configurations should be preferred where available.
/// \param J LLJIT instance to configure.
/// \return The platform JITDylib, or an error if setup fails.
LLVM_ABI Expected<JITDylibSP> setUpGenericLLVMIRPlatform(LLJIT &J);

/// Configure the LLJIT instance to disable platform support explicitly.
///
/// This is useful in two cases: for platforms that don't have such requirements
/// and for platforms that we have no explicit support yet and that don't work
/// well with the generic IR platform.
/// \param J LLJIT instance to configure.
/// \return The platform JITDylib, or an error if setup fails.
LLVM_ABI Expected<JITDylibSP> setUpInactivePlatform(LLJIT &J);

/// A Platform-support class that implements initialize / deinitialize by
/// forwarding to ORC runtime dlopen / dlclose operations.
class LLVM_ABI ORCPlatformSupport : public LLJIT::PlatformSupport {
public:
  /// Construct platform support that forwards to the ORC runtime.
  /// \param J LLJIT instance whose runtime will be used.
  ORCPlatformSupport(orc::LLJIT &J) : J(J) {}
  /// Initialize \p JD by forwarding to the ORC runtime.
  /// \param JD JITDylib to initialize.
  /// \return Success, or an error if initialization fails.
  Error initialize(orc::JITDylib &JD) override;
  /// Deinitialize \p JD by forwarding to the ORC runtime.
  /// \param JD JITDylib to deinitialize.
  /// \return Success, or an error if deinitialization fails.
  Error deinitialize(orc::JITDylib &JD) override;

private:
  orc::LLJIT &J;
  DenseMap<orc::JITDylib *, orc::ExecutorAddr> DSOHandles;
  SmallPtrSet<JITDylib const *, 8> InitializedDylib;
};

} // End namespace orc
} // End namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_LLJIT_H
