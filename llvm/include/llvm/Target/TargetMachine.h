//===-- llvm/Target/TargetMachine.h - Target Information --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// This file defines the TargetMachine class.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGET_TARGETMACHINE_H
#define LLVM_TARGET_TARGETMACHINE_H

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/PGOOptions.h"
#include "llvm/Target/CGPassBuilderOption.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Triple.h"
#include <optional>
#include <string>
#include <utility>

namespace llvm {

/// Remove the kernel-info pass at the end of the full LTO pipeline when set.
LLVM_ABI extern llvm::cl::opt<bool> NoKernelInfoEndLTO;

class AAManager;
using ModulePassManager = PassManager<Module>;

class Function;
class GlobalValue;
class MachineInstr;
class MachineModuleInfoWrapperPass;
struct MachineSchedContext;
class Mangler;
class MCAsmInfo;
class MCContext;
class MCInstrInfo;
class MCRegisterInfo;
class MCStreamer;
class MCSubtargetInfo;
class MCSymbol;
class raw_pwrite_stream;
class PassBuilder;
class PassInstrumentationCallbacks;
struct PerFunctionMIParsingState;
class ScheduleDAGInstrs;
class SMDiagnostic;
class SMRange;
class Target;
class TargetIRAnalysis;
class TargetTransformInfo;
class TargetLoweringObjectFile;
class TargetPassConfig;
class TargetSubtargetInfo;

// The old pass manager infrastructure is hidden in a legacy namespace now.
namespace legacy {
class PassManagerBase;
} // namespace legacy
using legacy::PassManagerBase;

struct MachineFunctionInfo;
namespace yaml {
struct MachineFunctionInfo;
} // namespace yaml

//===----------------------------------------------------------------------===//
///
/// Primary interface to the complete machine description for the target
/// machine.  All target-specific information should be accessible through this
/// interface.
///
class LLVM_ABI TargetMachine {
protected: // Can only create subclasses.
  /// Construct a TargetMachine for the given target and configuration.
  ///
  /// \param T Target description this machine is created for.
  /// \param DataLayoutString String representation of the target data layout.
  /// \param TargetTriple Triple describing the target architecture and OS.
  /// \param CPU Target CPU name.
  /// \param FS Target feature string.
  /// \param Options Target-specific code generation options.
  TargetMachine(const Target &T, StringRef DataLayoutString,
                const Triple &TargetTriple, StringRef CPU, StringRef FS,
                const TargetOptions &Options);

  /// The Target that this machine was created for.
  const Target &TheTarget;

  /// DataLayout for the target: keep ABI type size and alignment.
  ///
  /// The DataLayout is created based on the string representation provided
  /// during construction. It is kept here only to avoid reparsing the string
  /// but should not really be used during compilation, because it has an
  /// internal cache that is context specific.
  const DataLayout DL;

  /// Triple string, CPU name, and target feature strings the TargetMachine
  /// instance is created with.
  Triple TargetTriple;
  /// Target CPU name this machine was created with.
  std::string TargetCPU;
  /// Target feature string this machine was created with.
  std::string TargetFS;

  /// Relocation model used for code generation.
  Reloc::Model RM = Reloc::Static;
  /// Code model used for code generation.
  CodeModel::Model CMModel = CodeModel::Small;
  /// Threshold above which data is considered "large" under the code model.
  uint64_t LargeDataThreshold = 0;
  /// Optimization level used for code generation.
  CodeGenOptLevel OptLevel = CodeGenOptLevel::Default;

  /// Contains target specific asm information.
  std::unique_ptr<const MCAsmInfo> AsmInfo;
  /// Target-specific MC register information.
  std::unique_ptr<const MCRegisterInfo> MRI;
  /// Target-specific MC instruction information.
  std::unique_ptr<const MCInstrInfo> MII;
  /// Default MC subtarget information for this machine.
  std::unique_ptr<const MCSubtargetInfo> STI;

  /// MC subtarget keyed by target features and target CPU.
  StringMap<std::unique_ptr<const MCSubtargetInfo>> MCSubtargetMap;

  /// Whether the target requires a structured control-flow graph.
  unsigned RequireStructuredCFG : 1;
  /// Whether -O0 should prefer FastISel when available.
  unsigned O0WantsFastISel : 1;

  /// Profile-guided optimization options for this target machine.
  std::optional<PGOOptions> PGOOption;

public:
  /// Target-specific code generation options for this machine.
  TargetOptions Options;

  /// Deleted copy constructor; TargetMachine is not copyable.
  ///
  /// \param Other Unused; copy construction is deleted.
  TargetMachine(const TargetMachine &Other) = delete;
  /// Deleted copy assignment; TargetMachine is not copyable.
  ///
  /// \param Other Unused; copy assignment is deleted.
  void operator=(const TargetMachine &Other) = delete;
  /// Destroy this TargetMachine and release owned MC objects.
  virtual ~TargetMachine();

  /// Return the Target this machine was created for.
  ///
  /// \return Target description this machine was created for.
  const Target &getTarget() const { return TheTarget; }

  /// Return the target triple this machine was created with.
  ///
  /// \return Target triple used to create this machine.
  const Triple &getTargetTriple() const { return TargetTriple; }
  /// Return the target CPU name this machine was created with.
  ///
  /// \return Target CPU name used to create this machine.
  StringRef getTargetCPU() const { return TargetCPU; }
  /// Return the target feature string this machine was created with.
  ///
  /// \return Target feature string used to create this machine.
  StringRef getTargetFeatureString() const { return TargetFS; }
  /// Set the target feature string used by this machine.
  ///
  /// \param FS New target feature string.
  void setTargetFeatureString(StringRef FS) { TargetFS = std::string(FS); }

  /// Return the effective target ABI name for the module.
  ///
  /// Returns the "target-abi" module flag if present, otherwise the
  /// -target-abi option. This is a pure query; call verifyOptionsConsistency
  /// once per module to diagnose a conflict.
  /// \param M Module whose ABI name is queried.
  /// \return Effective target ABI name for \p M.
  StringRef getTargetABIName(const Module &M) const;

  /// Diagnose conflicting command-line codegen options and module flags.
  ///
  /// Diagnoses command-line codegen options that conflict with the
  /// corresponding module flags (e.g. -target-abi vs the "target-abi" module
  /// flag). Intended to be called once per module.
  /// \param M Module to check for option consistency.
  void verifyOptionsConsistency(const Module &M) const;

  /// Virtual method implemented by subclasses that returns a reference to that
  /// target's TargetSubtargetInfo-derived member variable.
  ///
  /// \param F Function whose subtarget is requested.
  /// \return TargetSubtargetInfo for \p F, or nullptr if unavailable.
  virtual const TargetSubtargetInfo *getSubtargetImpl(const Function &F) const {
    return nullptr;
  }
  /// Return the target object-file lowering helper, if any.
  ///
  /// \return TargetLoweringObjectFile for this target, or nullptr if none.
  virtual TargetLoweringObjectFile *getObjFileLowering() const {
    return nullptr;
  }

  /// Create the target's instance of MachineFunctionInfo.
  ///
  /// \param Allocator Allocator used to construct the info object.
  /// \param F Function the info describes.
  /// \param STI Subtarget information for \p F.
  /// \return New MachineFunctionInfo, or nullptr if unsupported.
  virtual MachineFunctionInfo *
  createMachineFunctionInfo(BumpPtrAllocator &Allocator, const Function &F,
                            const TargetSubtargetInfo *STI) const {
    return nullptr;
  }

  /// Create an instance of ScheduleDAGInstrs to be run within the standard
  /// MachineScheduler pass for this function and target at the current
  /// optimization level.
  ///
  /// This can also be used to plug a new MachineSchedStrategy into an instance
  /// of the standard ScheduleDAGMI:
  ///   return new ScheduleDAGMI(C, std::make_unique<MyStrategy>(C),
  ///   /*RemoveKillFlags=*/false)
  ///
  /// Return NULL to select the default (generic) machine scheduler.
  /// \param C Scheduling context for the DAG to create.
  /// \return ScheduleDAGInstrs for this target, or nullptr for the default.
  virtual ScheduleDAGInstrs *
  createMachineScheduler(MachineSchedContext *C) const {
    return nullptr;
  }

  /// Similar to createMachineScheduler but used when postRA machine scheduling
  /// is enabled.
  ///
  /// \param C Scheduling context for the DAG to create.
  /// \return Post-RA ScheduleDAGInstrs, or nullptr to use the default.
  virtual ScheduleDAGInstrs *
  createPostMachineScheduler(MachineSchedContext *C) const {
    return nullptr;
  }

  /// Allocate and return a default initialized instance of the YAML
  /// representation for the MachineFunctionInfo.
  ///
  /// \return Default YAML MachineFunctionInfo, or nullptr if unsupported.
  virtual yaml::MachineFunctionInfo *createDefaultFuncInfoYAML() const {
    return nullptr;
  }

  /// Allocate and initialize an instance of the YAML representation of the
  /// MachineFunctionInfo.
  ///
  /// \param MF Machine function whose info is converted to YAML.
  /// \return YAML MachineFunctionInfo for \p MF, or nullptr if unsupported.
  virtual yaml::MachineFunctionInfo *
  convertFuncInfoToYAML(const MachineFunction &MF) const {
    return nullptr;
  }

  /// Parse out the target's MachineFunctionInfo from the YAML reprsentation.
  ///
  /// \param MFI YAML representation of the machine function info.
  /// \param PFS Per-function MI parsing state being filled.
  /// \param Error Diagnostic populated on parse failure.
  /// \param SourceRange Source range associated with the YAML entry.
  /// \return True if parsing failed; false on success.
  virtual bool parseMachineFunctionInfo(const yaml::MachineFunctionInfo &MFI,
                                        PerFunctionMIParsingState &PFS,
                                        SMDiagnostic &Error,
                                        SMRange &SourceRange) const {
    return false;
  }

  /// This method returns a pointer to the specified type of
  /// TargetSubtargetInfo.  In debug builds, it verifies that the object being
  /// returned is of the correct type.
  ///
  /// \param F Function whose typed subtarget is requested.
  /// \return Reference to the typed TargetSubtargetInfo for \p F.
  template <typename STC> const STC &getSubtarget(const Function &F) const {
    return *static_cast<const STC*>(getSubtargetImpl(F));
  }

  /// Create a DataLayout.
  ///
  /// \return Copy of this target machine's data layout.
  const DataLayout createDataLayout() const { return DL; }

  /// Test if a DataLayout if compatible with the CodeGen for this target.
  ///
  /// The LLVM Module owns a DataLayout that is used for the target independent
  /// optimizations and code generation. This hook provides a target specific
  /// check on the validity of this DataLayout.
  /// \param Candidate Data layout to compare against this target's layout.
  /// \return True if \p Candidate matches this target's data layout.
  bool isCompatibleDataLayout(const DataLayout &Candidate) const {
    return DL == Candidate;
  }

  /// Get the pointer size for this target.
  ///
  /// This is the only time the DataLayout in the TargetMachine is used.
  /// \param AS Address space whose pointer size is requested.
  /// \return Pointer size in bytes for address space \p AS.
  unsigned getPointerSize(unsigned AS) const {
    return DL.getPointerSize(AS);
  }

  /// Return the pointer size in bits for the given address space.
  ///
  /// \param AS Address space whose pointer size in bits is requested.
  /// \return Pointer size in bits for address space \p AS.
  unsigned getPointerSizeInBits(unsigned AS) const {
    return DL.getPointerSizeInBits(AS);
  }

  /// Return the pointer size of the program address space.
  ///
  /// \return Pointer size in bytes of the program address space.
  unsigned getProgramPointerSize() const {
    return DL.getPointerSize(DL.getProgramAddressSpace());
  }

  /// Return the pointer size of the alloca address space.
  ///
  /// \return Pointer size in bytes of the alloca address space.
  unsigned getAllocaPointerSize() const {
    return DL.getPointerSize(DL.getAllocaAddrSpace());
  }

  /// Return target specific asm information.
  ///
  /// \return Target-specific MCAsmInfo.
  const MCAsmInfo &getMCAsmInfo() const { return *AsmInfo; }

  /// Return the target-specific MC register information.
  ///
  /// \return Target-specific MCRegisterInfo.
  const MCRegisterInfo &getMCRegisterInfo() const { return *MRI; }
  /// Return the target-specific MC instruction information.
  ///
  /// \return Target-specific MCInstrInfo, or nullptr if unavailable.
  const MCInstrInfo *getMCInstrInfo() const { return MII.get(); }
  /// Return the default MC subtarget information for this machine.
  ///
  /// \return Default MCSubtargetInfo for this machine.
  const MCSubtargetInfo &getMCSubtargetInfo() const { return *STI; }

  /// Get the MCSubtargetInfo for the given CPU and features.
  ///
  /// For use in contexts where a feature-specific MC subtarget is needed,
  /// but no MachineFunction is available, such as for module-level inline
  /// assembly.
  /// \param CPU Target CPU name for the subtarget.
  /// \param FS Target feature string for the subtarget.
  /// \return MCSubtargetInfo for \p CPU and \p FS.
  const MCSubtargetInfo &getMCSubtargetInfo(StringRef CPU, StringRef FS);

  /// Return the ExceptionHandling to use, considering TargetOptions and the
  /// Triple's default.
  ///
  /// \return Exception-handling model selected for this target.
  ExceptionHandling getExceptionModel() const {
    // FIXME: This interface fails to distinguish default from not supported.
    return Options.ExceptionModel == ExceptionHandling::None
               ? TargetTriple.getDefaultExceptionHandling()
               : Options.ExceptionModel;
  }

  /// Return true if the target requires a structured CFG.
  ///
  /// \return True if the target requires a structured CFG.
  bool requiresStructuredCFG() const { return RequireStructuredCFG; }
  /// Set whether the target requires a structured CFG.
  ///
  /// \param Value True if a structured CFG is required.
  void setRequiresStructuredCFG(bool Value) { RequireStructuredCFG = Value; }

  /// Returns the code generation relocation model. The choices are static, PIC,
  /// and dynamic-no-pic, and target default.
  ///
  /// \return Relocation model used for code generation.
  Reloc::Model getRelocationModel() const;

  /// Returns the code model. The choices are small, kernel, medium, large, and
  /// target default.
  ///
  /// \return Code model used for code generation.
  CodeModel::Model getCodeModel() const { return CMModel; }

  /// Returns the maximum code size possible under the code model.
  ///
  /// \return Maximum code size allowed by the current code model.
  uint64_t getMaxCodeSize() const;

  /// Set the code model.
  ///
  /// \param CM Code model to use for code generation.
  void setCodeModel(CodeModel::Model CM) { CMModel = CM; }

  /// Set the large-data threshold used with the code model.
  ///
  /// \param LDT Size threshold above which data is treated as large.
  void setLargeDataThreshold(uint64_t LDT) { LargeDataThreshold = LDT; }
  /// Return true if \p GV should be treated as a large global value.
  ///
  /// \param GV Global value to classify.
  /// \return True if \p GV should be treated as a large global value.
  bool isLargeGlobalValue(const GlobalValue *GV) const;
  /// Return true if a data object of \p Size should be treated as large.
  ///
  /// \param Size Data size in bytes.
  /// \return True if a data object of \p Size is treated as large.
  bool isLargeDataSize(uint64_t Size) const;

  /// Return true if code generation uses a position-independent model.
  ///
  /// \return True if code generation uses a position-independent model.
  bool isPositionIndependent() const;

  /// Return true if references to \p GV may assume DSO-local binding.
  ///
  /// \param GV Global value to query.
  /// \return True if references to \p GV may assume DSO-local binding.
  bool shouldAssumeDSOLocal(const GlobalValue *GV) const;

  /// Returns true if this target uses emulated TLS.
  ///
  /// \return True if this target uses emulated TLS.
  bool useEmulatedTLS() const;

  /// Returns true if this target uses TLS Descriptors.
  ///
  /// \return True if this target uses TLS descriptors.
  bool useTLSDESC() const;

  /// Returns the TLS model which should be used for the given global variable.
  ///
  /// \param GV Global variable whose TLS model is requested.
  /// \return TLS model to use for \p GV.
  TLSModel::Model getTLSModel(const GlobalValue *GV) const;

  /// Returns the optimization level: None, Less, Default, or Aggressive.
  ///
  /// \return Current code generation optimization level.
  CodeGenOptLevel getOptLevel() const { return OptLevel; }

  /// Overrides the optimization level.
  ///
  /// \param Level New code generation optimization level.
  void setOptLevel(CodeGenOptLevel Level) { OptLevel = Level; }

  /// Enable or disable FastISel for this target machine.
  ///
  /// \param Enable True to enable FastISel.
  void setFastISel(bool Enable) { Options.EnableFastISel = Enable; }
  /// Return true if -O0 should prefer FastISel when available.
  ///
  /// \return True if -O0 should prefer FastISel when available.
  bool getO0WantsFastISel() { return O0WantsFastISel; }
  /// Set whether -O0 should prefer FastISel when available.
  ///
  /// \param Enable True if -O0 should want FastISel.
  void setO0WantsFastISel(bool Enable) { O0WantsFastISel = Enable; }
  /// Enable or disable GlobalISel for this target machine.
  ///
  /// \param Enable True to enable GlobalISel.
  void setGlobalISel(bool Enable) { Options.EnableGlobalISel = Enable; }
  /// Set how GlobalISel aborts on failure.
  ///
  /// \param Mode Abort mode for GlobalISel failures.
  void setGlobalISelAbort(GlobalISelAbortMode Mode) {
    Options.GlobalISelAbort = Mode;
  }
  /// Enable or disable the machine outliner.
  ///
  /// \param Enable True to enable the machine outliner.
  void setMachineOutliner(bool Enable) {
    Options.EnableMachineOutliner = Enable;
  }
  /// Set whether the target supports default outlining.
  ///
  /// \param Enable True if default outlining is supported.
  void setSupportsDefaultOutlining(bool Enable) {
    Options.SupportsDefaultOutlining = Enable;
  }
  /// Set whether the target supports debug entry values by default.
  ///
  /// \param Enable True if debug entry values are supported by default.
  void setSupportsDebugEntryValues(bool Enable) {
    Options.SupportsDebugEntryValues = Enable;
  }
  /// Enable or disable the default machine verifier.
  ///
  /// \param Enable True to enable the default machine verifier.
  void setEnableDefaultMachineVerifier(bool Enable) {
    Options.EnableDefaultMachineVerifier = Enable;
  }

  /// Enable or disable CFI fixup for this target machine.
  ///
  /// \param Enable True to enable CFI fixup.
  void setCFIFixup(bool Enable) { Options.EnableCFIFixup = Enable; }

  /// Return true if the AIX extended Altivec ABI is enabled.
  ///
  /// \return True if the AIX extended Altivec ABI is enabled.
  bool getAIXExtendedAltivecABI() const {
    return Options.EnableAIXExtendedAltivecABI;
  }

  /// Return true if unique section names should be used.
  ///
  /// \return True if unique section names should be used.
  bool getUniqueSectionNames() const { return Options.UniqueSectionNames; }

  /// Return true if unique basic block section names must be generated.
  ///
  /// \return True if unique basic-block section names are required.
  bool getUniqueBasicBlockSectionNames() const {
    return Options.UniqueBasicBlockSectionNames;
  }

  /// Return true if named sections should be emitted separately.
  ///
  /// \return True if named sections are emitted separately.
  bool getSeparateNamedSections() const {
    return Options.SeparateNamedSections;
  }

  /// Return true if data objects should be emitted into their own section,
  /// corresponds to -fdata-sections.
  ///
  /// \return True if each data object is emitted into its own section.
  bool getDataSections() const {
    return Options.DataSections;
  }

  /// Return true if functions should be emitted into their own section,
  /// corresponding to -ffunction-sections.
  ///
  /// \return True if each function is emitted into its own section.
  bool getFunctionSections() const {
    return Options.FunctionSections;
  }

  /// Return true if static data partitioning is enabled.
  ///
  /// \return True if static data partitioning is enabled.
  bool getEnableStaticDataPartitioning() const {
    return Options.EnableStaticDataPartitioning;
  }

  /// Return true if visibility attribute should not be emitted in XCOFF,
  /// corresponding to -mignore-xcoff-visibility.
  ///
  /// \return True if XCOFF visibility attributes should be ignored.
  bool getIgnoreXCOFFVisibility() const {
    return Options.IgnoreXCOFFVisibility;
  }

  /// Return true if XCOFF traceback table should be emitted,
  /// corresponding to -xcoff-traceback-table.
  ///
  /// \return True if XCOFF traceback tables should be emitted.
  bool getXCOFFTracebackTable() const { return Options.XCOFFTracebackTable; }

  /// Return how basic blocks should be emitted into their own sections.
  ///
  /// Corresponds to -fbasic-block-sections.
  /// \return Basic-block section emission mode from the target options.
  llvm::BasicBlockSection getBBSectionsType() const {
    return Options.BBSections;
  }

  /// Get the list of functions and basic block ids that need unique sections.
  ///
  /// \return Buffer listing functions and basic-block IDs for unique sections.
  const MemoryBuffer *getBBSectionsFuncListBuf() const {
    return Options.BBSectionsFuncListBuf.get();
  }

  /// Returns true if a cast between SrcAS and DestAS is a noop.
  ///
  /// \param SrcAS Source address space of the cast.
  /// \param DestAS Destination address space of the cast.
  /// \return True if casting from \p SrcAS to \p DestAS is a no-op.
  virtual bool isNoopAddrSpaceCast(unsigned SrcAS, unsigned DestAS) const {
    return false;
  }

  /// Set the profile-guided optimization options for this machine.
  ///
  /// \param PGOOpt Optional PGO options to apply, or empty to clear them.
  void setPGOOption(std::optional<PGOOptions> PGOOpt) { PGOOption = PGOOpt; }
  /// Return the profile-guided optimization options for this machine.
  ///
  /// \return Optional PGO options currently set on this machine.
  const std::optional<PGOOptions> &getPGOOption() const { return PGOOption; }

  /// If the specified generic pointer could be assumed as a pointer to a
  /// specific address space, return that address space.
  ///
  /// Under offloading programming, the offloading target may be passed with
  /// values only prepared on the host side and could assume certain
  /// properties.
  /// \param V Value whose assumed address space is queried.
  /// \return Assumed address space for \p V, or -1 if none.
  virtual unsigned getAssumedAddrSpace(const Value *V) const { return -1; }

  /// Return the generic pointer and address space queried by a predicate.
  ///
  /// If the specified predicate checks whether a generic pointer falls within
  /// a specified address space, return that generic pointer and the address
  /// space being queried.
  ///
  /// Such predicates could be specified in @llvm.assume intrinsics for the
  /// optimizer to assume that the given generic pointer always falls within
  /// the address space based on that predicate.
  /// \param V Predicate value that may encode an address-space check.
  /// \return Pair of the generic pointer and address space, or {nullptr, -1}.
  virtual std::pair<const Value *, unsigned>
  getPredicatedAddrSpace(const Value *V) const {
    return std::make_pair(nullptr, -1);
  }

  /// Get a \c TargetIRAnalysis appropriate for the target.
  ///
  /// This is used to construct the new pass manager's target IR analysis pass,
  /// set up appropriately for this target machine. Even the old pass manager
  /// uses this to answer queries about the IR.
  /// \return TargetIRAnalysis configured for this target machine.
  TargetIRAnalysis getTargetIRAnalysis() const;

  /// Return a TargetTransformInfo for a given function.
  ///
  /// The returned TargetTransformInfo is specialized to the subtarget
  /// corresponding to \p F.
  /// \param F Function whose target transform info is requested.
  /// \return TargetTransformInfo specialized for \p F.
  virtual TargetTransformInfo getTargetTransformInfo(const Function &F) const;

  /// Allow the target to modify the pass pipeline.
  ///
  /// \param PB Pass builder to register callbacks with.
  // TODO: Populate all pass names by using <Target>PassRegistry.def.
  virtual void registerPassBuilderCallbacks(PassBuilder &PB) {}

  /// Register early alias analyses with the default AAManager.
  ///
  /// Allow the target to register early alias analyses (AA before BasicAA) with
  /// the AAManager for use with the new pass manager. Only affects the
  /// "default" AAManager.
  /// \param AAM Alias analysis manager to register early analyses with.
  virtual void registerEarlyDefaultAliasAnalyses(AAManager &AAM) {}

  /// Allow the target to register alias analyses with the AAManager for use
  /// with the new pass manager. Only affects the "default" AAManager.
  ///
  /// \param AAM Alias analysis manager to register analyses with.
  virtual void registerDefaultAliasAnalyses(AAManager &AAM) {}

  /// Add passes to emit the specified file type from the pass manager.
  ///
  /// Typically this will involve several steps of code generation. This method
  /// should return true if emission of this file type is not supported, or
  /// false on success.
  /// \param PM Pass manager that receives the emission passes.
  /// \param Out Primary output stream for the emitted file.
  /// \param DwoOut Optional stream for split DWARF (.dwo) output.
  /// \param FileType Kind of file to emit.
  /// \param DisableVerify Whether to skip machine verification.
  /// \param MMIWP Optional wrapper used to set MachineModuleInfo for this PM.
  /// \return True if emission of \p FileType is unsupported; false on success.
  virtual bool
  addPassesToEmitFile(PassManagerBase &PM, raw_pwrite_stream &Out,
                      raw_pwrite_stream *DwoOut, CodeGenFileType FileType,
                      bool DisableVerify = true,
                      MachineModuleInfoWrapperPass *MMIWP = nullptr) {
    return true;
  }

  /// Add passes to emit machine code for MCJIT.
  ///
  /// This method returns true if machine code is not supported. It fills the
  /// MCContext Ctx pointer which can be used to build custom MCStreamer.
  /// \param PM Pass manager that receives the emission passes.
  /// \param Ctx Filled with the MCContext used for emission.
  /// \param Out Output stream for the emitted machine code.
  /// \param DisableVerify Whether to skip machine verification.
  /// \return True if machine-code emission is not supported; false on success.
  virtual bool addPassesToEmitMC(PassManagerBase &PM, MCContext *&Ctx,
                                 raw_pwrite_stream &Out,
                                 bool DisableVerify = true) {
    return true;
  }

  /// True if subtarget inserts the final scheduling pass on its own.
  ///
  /// Branch relaxation, which must happen after block placement, can
  /// on some targets (e.g. SystemZ) expose additional post-RA
  /// scheduling opportunities.
  /// \return True if the target schedules its own post-RA pass.
  virtual bool targetSchedulesPostRAScheduling() const { return false; };

  /// Append the mangled name of \p GV to \p Name.
  ///
  /// \param Name Buffer that receives the mangled name prefix.
  /// \param GV Global value whose name is mangled.
  /// \param Mang Mangler used to produce the name.
  /// \param MayAlwaysUsePrivate Whether private linkage names may always be
  /// used.
  void getNameWithPrefix(SmallVectorImpl<char> &Name, const GlobalValue *GV,
                         Mangler &Mang, bool MayAlwaysUsePrivate = false) const;
  /// Return the MCSymbol for the given global value.
  ///
  /// \param GV Global value whose symbol is requested.
  /// \return MCSymbol corresponding to \p GV.
  MCSymbol *getSymbol(const GlobalValue *GV) const;

  /// The integer bit size to use for SjLj based exception handling.
  static constexpr unsigned DefaultSjLjDataSize = 32;
  /// Return the integer bit size used for SjLj-based exception handling.
  ///
  /// \return Bit size used for SjLj exception-handling data.
  virtual unsigned getSjLjDataSize() const { return DefaultSjLjDataSize; }

  /// Parse a binutils version string into a major/minor pair.
  ///
  /// \param Version Version string such as "2.36" or "none".
  /// \return Major and minor version numbers parsed from \p Version.
  static std::pair<int, int> parseBinutilsVersion(StringRef Version);

  /// Return the address space for a given pseudo source kind of memory.
  ///
  /// Given the kind of memory (e.g. stack) the target returns the corresponding
  /// address space.
  /// \param Kind Pseudo source kind identifying the memory class.
  /// \return Address space associated with \p Kind, or 0 if unspecified.
  virtual unsigned getAddressSpaceForPseudoSourceKind(unsigned Kind) const {
    return 0;
  }

  /// Entry point for module splitting. Targets can implement custom module
  /// splitting logic, mainly used by LTO for --lto-partitions.
  ///
  /// On success, this guarantees that between 1 and \p NumParts modules were
  /// created and passed to \p ModuleCallBack.
  ///
  /// \param M Module to split.
  /// \param NumParts Requested number of partitions.
  /// \param ModuleCallback Callback invoked with each produced module part.
  /// \returns `true` if the module was split, `false` otherwise. When  `false`
  /// is returned, it is assumed that \p ModuleCallback has never been called
  /// and \p M has not been modified.
  virtual bool splitModule(
      Module &M, unsigned NumParts,
      function_ref<void(std::unique_ptr<Module> MPart)> ModuleCallback) {
    return false;
  }

  /// Create a pass configuration object to be used by addPassToEmitX methods
  /// for generating a pipeline of CodeGen passes.
  ///
  /// \param PM Pass manager the configuration will build passes for.
  /// \return A new TargetPassConfig, or nullptr if unsupported.
  virtual TargetPassConfig *createPassConfig(PassManagerBase &PM) {
    return nullptr;
  }

  /// Build the new-pass-manager code generation pipeline for this target.
  ///
  /// \param MPM Module pass manager that receives the pipeline.
  /// \param MAM Module analysis manager used by the pipeline.
  /// \param Out Primary output stream for the emitted file.
  /// \param DwoOut Optional stream for split DWARF (.dwo) output.
  /// \param FileType Kind of file to emit.
  /// \param Opt CodeGen pass builder options.
  /// \param Ctx MC context used during emission.
  /// \param PIC Optional pass instrumentation callbacks.
  /// \return Success, or an error if the pipeline could not be built.
  virtual Error
  buildCodeGenPipeline(ModulePassManager &MPM, ModuleAnalysisManager &MAM,
                       raw_pwrite_stream &Out, raw_pwrite_stream *DwoOut,
                       CodeGenFileType FileType, const CGPassBuilderOption &Opt,
                       MCContext &Ctx, PassInstrumentationCallbacks *PIC) {
    return make_error<StringError>("buildCodeGenPipeline is not overridden",
                                   inconvertibleErrorCode());
  }

  /// Returns true if frontends should default to using the NewPM for this
  /// specific target.
  ///
  /// \return True if the new pass manager should be the default for this target.
  virtual bool shouldDefaultToNewPM() const { return false; }

  /// Return true if the target is expected to pass machine verifier checks.
  ///
  /// This is a stopgap measure to fix targets one by one. We will remove this
  /// at some point and always enable the verifier when EXPENSIVE_CHECKS is
  /// enabled.
  /// \return True if the target is expected to pass the machine verifier.
  virtual bool isMachineVerifierClean() const { return true; }

  /// Adds an AsmPrinter pass to the pipeline that prints assembly or
  /// machine code from the MI representation.
  ///
  /// \param PM Pass manager that receives the AsmPrinter pass.
  /// \param Out Primary output stream for the printed file.
  /// \param DwoOut Optional stream for split DWARF (.dwo) output.
  /// \param FileType Kind of file to print.
  /// \param Context MC context used by the printer.
  /// \return True if an AsmPrinter was added successfully.
  virtual bool addAsmPrinter(PassManagerBase &PM, raw_pwrite_stream &Out,
                             raw_pwrite_stream *DwoOut,
                             CodeGenFileType FileType, MCContext &Context) {
    return false;
  }

  /// Create an MCStreamer for the given output streams and file type.
  ///
  /// \param Out Primary output stream for the streamer.
  /// \param DwoOut Optional stream for split DWARF (.dwo) output.
  /// \param FileType Kind of file the streamer should emit.
  /// \param Ctx MC context used by the streamer.
  /// \return An MCStreamer on success, or an error if creation fails.
  virtual Expected<std::unique_ptr<MCStreamer>>
  createMCStreamer(raw_pwrite_stream &Out, raw_pwrite_stream *DwoOut,
                   CodeGenFileType FileType, MCContext &Ctx);

  /// Return true if the target uses physical registers for values.
  ///
  /// True if the target uses physical regs (as nearly all targets do). False
  /// for stack machines such as WebAssembly and other virtual-register
  /// machines. If true, all vregs must be allocated before PEI. If false, then
  /// callee-save register spilling and scavenging are not needed or used. If
  /// false, implicitly defined registers will still be assumed to be physical
  /// registers, except that variadic defs will be allocated vregs.
  /// \return True if values are held in physical registers.
  virtual bool usesPhysRegsForValues() const { return true; }

  /// True if the target wants to use interprocedural register allocation by
  /// default.
  ///
  /// The -enable-ipra flag can be used to override this.
  /// \return True if IPRA is enabled by default for this target.
  virtual bool useIPRA() const { return false; }

  /// The default variant to use in unqualified `asm` instructions.
  ///
  /// If this returns 0, `asm "$(foo$|bar$)"` will evaluate to `asm "foo"`.
  /// \return The default inline-asm variant index.
  virtual int unqualifiedInlineAsmVariant() const { return 0; }

  /// Register a MachineRegisterInfo callback for the given machine function.
  ///
  /// \param MF Machine function whose register info receives the callback.
  virtual void registerMachineRegisterInfoCallback(MachineFunction &MF) const {}

  /// Remove all Linker Optimization Hints (LOH) associated with instructions in
  /// \p MIs and \return the number of hints removed. This is useful in
  /// transformations that cause these hints to be illegal, like in the machine
  /// outliner.
  ///
  /// \param MIs Instructions whose associated LOHs should be cleared.
  virtual size_t clearLinkerOptimizationHints(
      const SmallPtrSetImpl<MachineInstr *> &MIs) const {
    return 0;
  }

  /// Return whether the backend can lower the llvm.cond.loop intrinsic.
  ///
  /// If this function returns false, the intrinsic will be supported
  /// generically but without loop detection support.
  /// \return True if the backend can lower llvm.cond.loop.
  virtual bool canLowerCondLoop() const { return false; }
};

} // end namespace llvm

#endif // LLVM_TARGET_TARGETMACHINE_H
