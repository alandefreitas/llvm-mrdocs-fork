//===-- CommandFlags.h - Command Line Flags Interface -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains codegen-specific flags that are shared between different
// command line tools. The tools "llc" and "opt" both use this file to prevent
// flag duplication.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_COMMANDFLAGS_H
#define LLVM_CODEGEN_COMMANDFLAGS_H

#include "llvm/ADT/FloatingPointMode.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Target/TargetOptions.h"
#include <optional>
#include <string>
#include <vector>

namespace llvm {

class Module;
class AttrBuilder;
class Function;
class Triple;
class TargetMachine;

/// Shared codegen command-line flag accessors used by tools such as llc and opt.
namespace codegen {

/// Return the value of the -march command-line option.
///
/// \return Value of the -march command-line option.
LLVM_ABI std::string getMArch();

/// Return the value of the -mcpu command-line option.
///
/// \return Value of the -mcpu command-line option.
LLVM_ABI std::string getMCPU();

/// Return the value of the -mtune command-line option.
///
/// \return Value of the -mtune command-line option.
LLVM_ABI std::string getMTune();

/// Return the list of -mattr values from the command line.
///
/// \return List of -mattr values from the command line.
LLVM_ABI std::vector<std::string> getMAttrs();

/// Return the value of the -relocation-model command-line option.
///
/// \return Value of the -relocation-model command-line option.
LLVM_ABI Reloc::Model getRelocModel();
/// Return the -relocation-model value if it was explicitly set.
///
/// \return Explicit -relocation-model value, or std::nullopt if unset.
LLVM_ABI std::optional<Reloc::Model> getExplicitRelocModel();

/// Return the value of the -thread-model command-line option.
///
/// \return Value of the -thread-model command-line option.
LLVM_ABI ThreadModel::Model getThreadModel();

/// Return the value of the -code-model command-line option.
///
/// \return Value of the -code-model command-line option.
LLVM_ABI CodeModel::Model getCodeModel();
/// Return the -code-model value if it was explicitly set.
///
/// \return Explicit -code-model value, or std::nullopt if unset.
LLVM_ABI std::optional<CodeModel::Model> getExplicitCodeModel();

/// Return the value of the -large-data-threshold command-line option.
///
/// \return Value of the -large-data-threshold command-line option.
LLVM_ABI uint64_t getLargeDataThreshold();
/// Return the -large-data-threshold value if it was explicitly set.
///
/// \return Explicit -large-data-threshold value, or std::nullopt if unset.
LLVM_ABI std::optional<uint64_t> getExplicitLargeDataThreshold();

/// Return the value of the -exception-model command-line option.
///
/// \return Value of the -exception-model command-line option.
LLVM_ABI llvm::ExceptionHandling getExceptionModel();

/// Return the -filetype value if it was explicitly set.
///
/// \return Explicit -filetype value, or std::nullopt if unset.
LLVM_ABI std::optional<CodeGenFileType> getExplicitFileType();

/// Return the value of the -filetype command-line option.
///
/// \return Value of the -filetype command-line option.
LLVM_ABI CodeGenFileType getFileType();

/// Return the value of the -frame-pointer command-line option.
///
/// \return Value of the -frame-pointer command-line option.
LLVM_ABI FramePointerKind getFramePointerUsage();

/// Return whether -enable-no-trapping-fp-math is enabled.
///
/// \return True if -enable-no-trapping-fp-math is enabled.
LLVM_ABI bool getEnableNoTrappingFPMath();

/// Return the value of the -denormal-fp-math command-line option.
///
/// \return Value of the -denormal-fp-math command-line option.
LLVM_ABI DenormalMode::DenormalModeKind getDenormalFPMath();
/// Return the value of the -denormal-fp-math-f32 command-line option.
///
/// \return Value of the -denormal-fp-math-f32 command-line option.
LLVM_ABI DenormalMode::DenormalModeKind getDenormalFP32Math();

/// Return whether -enable-sign-dependent-rounding-fp-math is enabled.
///
/// \return True if -enable-sign-dependent-rounding-fp-math is enabled.
LLVM_ABI bool getEnableHonorSignDependentRoundingFPMath();

/// Return the value of the -float-abi command-line option.
///
/// \return Value of the -float-abi command-line option.
LLVM_ABI llvm::FloatABI::ABIType getFloatABIForCalls();

/// Return the value of the -fp-contract command-line option.
///
/// \return Value of the -fp-contract command-line option.
LLVM_ABI llvm::FPOpFusion::FPOpFusionMode getFuseFPOps();

/// Return the value of the -swift-async-fp command-line option.
///
/// \return Value of the -swift-async-fp command-line option.
LLVM_ABI SwiftAsyncFramePointerMode getSwiftAsyncFramePointer();

/// Return whether -nozero-initialized-in-bss is enabled.
///
/// \return True if -nozero-initialized-in-bss is enabled.
LLVM_ABI bool getDontPlaceZerosInBSS();

/// Return whether -tailcallopt is enabled.
///
/// \return True if -tailcallopt is enabled.
LLVM_ABI bool getEnableGuaranteedTailCallOpt();

/// Return whether -vec-extabi is enabled.
///
/// \return True if -vec-extabi is enabled.
LLVM_ABI bool getEnableAIXExtendedAltivecABI();

/// Return whether -disable-tail-calls is enabled.
///
/// \return True if -disable-tail-calls is enabled.
LLVM_ABI bool getDisableTailCalls();

/// Return whether -stack-symbol-ordering is enabled.
///
/// \return True if -stack-symbol-ordering is enabled.
LLVM_ABI bool getStackSymbolOrdering();

/// Return whether -stackrealign is enabled.
///
/// \return True if -stackrealign is enabled.
LLVM_ABI bool getStackRealign();

/// Return the value of the -trap-func command-line option.
///
/// \return Value of the -trap-func command-line option.
LLVM_ABI std::string getTrapFuncName();

/// Return whether -use-ctors is enabled.
///
/// \return True if -use-ctors is enabled.
LLVM_ABI bool getUseCtors();

/// Return whether -no-integrated-as is enabled.
///
/// \return True if -no-integrated-as is enabled.
LLVM_ABI bool getDisableIntegratedAS();

/// Return whether -data-sections is enabled.
///
/// \return True if -data-sections is enabled.
LLVM_ABI bool getDataSections();
/// Return the -data-sections value if it was explicitly set.
///
/// \return Explicit -data-sections value, or std::nullopt if unset.
LLVM_ABI std::optional<bool> getExplicitDataSections();

/// Return whether -function-sections is enabled.
///
/// \return True if -function-sections is enabled.
LLVM_ABI bool getFunctionSections();
/// Return the -function-sections value if it was explicitly set.
///
/// \return Explicit -function-sections value, or std::nullopt if unset.
LLVM_ABI std::optional<bool> getExplicitFunctionSections();

/// Return whether -ignore-xcoff-visibility is enabled.
///
/// \return True if -ignore-xcoff-visibility is enabled.
LLVM_ABI bool getIgnoreXCOFFVisibility();

/// Return whether -xcoff-traceback-table is enabled.
///
/// \return True if -xcoff-traceback-table is enabled.
LLVM_ABI bool getXCOFFTracebackTable();

/// Return the value of the -basic-block-sections command-line option.
///
/// \return Value of the -basic-block-sections command-line option.
LLVM_ABI std::string getBBSections();

/// Return the value of the -tls-size command-line option.
///
/// \return Value of the -tls-size command-line option.
LLVM_ABI unsigned getTLSSize();

/// Return whether -emulated-tls is enabled.
///
/// \return True if -emulated-tls is enabled.
LLVM_ABI bool getEmulatedTLS();
/// Return the -emulated-tls value if it was explicitly set.
///
/// \return Explicit -emulated-tls value, or std::nullopt if unset.
LLVM_ABI std::optional<bool> getExplicitEmulatedTLS();

/// Return whether -enable-tlsdesc is enabled.
///
/// \return True if -enable-tlsdesc is enabled.
LLVM_ABI bool getEnableTLSDESC();
/// Return the -enable-tlsdesc value if it was explicitly set.
///
/// \return Explicit -enable-tlsdesc value, or std::nullopt if unset.
LLVM_ABI std::optional<bool> getExplicitEnableTLSDESC();

/// Return whether -unique-section-names is enabled.
///
/// \return True if -unique-section-names is enabled.
LLVM_ABI bool getUniqueSectionNames();

/// Return whether -unique-basic-block-section-names is enabled.
///
/// \return True if -unique-basic-block-section-names is enabled.
LLVM_ABI bool getUniqueBasicBlockSectionNames();

/// Return whether -separate-named-sections is enabled.
///
/// \return True if -separate-named-sections is enabled.
LLVM_ABI bool getSeparateNamedSections();

/// Return the value of the -meabi command-line option.
///
/// \return Value of the -meabi command-line option.
LLVM_ABI llvm::EABI getEABIVersion();

/// Return the value of the -debugger-tune command-line option.
///
/// \return Value of the -debugger-tune command-line option.
LLVM_ABI llvm::DebuggerKind getDebuggerTuningOpt();

/// Return the value of the -vector-library command-line option.
///
/// \return Value of the -vector-library command-line option.
LLVM_ABI llvm::VectorLibrary getVectorLibrary();

/// Return whether -stack-size-section is enabled.
///
/// \return True if -stack-size-section is enabled.
LLVM_ABI bool getEnableStackSizeSection();

/// Return whether -addrsig is enabled.
///
/// \return True if -addrsig is enabled.
LLVM_ABI bool getEnableAddrsig();

/// Return whether -call-graph-section is enabled.
///
/// \return True if -call-graph-section is enabled.
LLVM_ABI bool getEnableCallGraphSection();

/// Return whether -emit-call-site-info is enabled.
///
/// \return True if -emit-call-site-info is enabled.
LLVM_ABI bool getEmitCallSiteInfo();

/// Return whether -split-machine-functions is enabled.
///
/// \return True if -split-machine-functions is enabled.
LLVM_ABI bool getEnableMachineFunctionSplitter();

/// Return whether -partition-static-data-sections is enabled.
///
/// \return True if -partition-static-data-sections is enabled.
LLVM_ABI bool getEnableStaticDataPartitioning();

/// Return whether -debug-entry-values is enabled.
///
/// \return True if -debug-entry-values is enabled.
LLVM_ABI bool getEnableDebugEntryValues();

/// Return whether -force-dwarf-frame-section is enabled.
///
/// \return True if -force-dwarf-frame-section is enabled.
LLVM_ABI bool getForceDwarfFrameSection();

/// Return whether -xray-function-index is enabled.
///
/// \return True if -xray-function-index is enabled.
LLVM_ABI bool getXRayFunctionIndex();

/// Return whether -strict-dwarf is enabled.
///
/// \return True if -strict-dwarf is enabled.
LLVM_ABI bool getDebugStrictDwarf();

/// Return the value of the -align-loops command-line option.
///
/// \return Value of the -align-loops command-line option.
LLVM_ABI unsigned getAlignLoops();

/// Return whether -enable-jmc-instrument is enabled.
///
/// \return True if -enable-jmc-instrument is enabled.
LLVM_ABI bool getJMCInstrument();

/// Return whether -mxcoff-roptr is enabled.
///
/// \return True if -mxcoff-roptr is enabled.
LLVM_ABI bool getXCOFFReadOnlyPointers();

/// Destination for --save-stats output.
enum SaveStatsMode {
  None, ///< Do not save statistics.
  Cwd,  ///< Save statistics under the current working directory.
  Obj,  ///< Save statistics under the output file's directory.
};

/// Return the value of the --save-stats command-line option.
///
/// \return Value of the --save-stats command-line option.
LLVM_ABI SaveStatsMode getSaveStats();

/// Create this object with static storage to register codegen-related command
/// line options.
struct RegisterCodeGenFlags {
  /// Register the shared codegen command-line options.
  LLVM_ABI RegisterCodeGenFlags();
};

/// Tools that support subtarget tuning should create this object with static
/// storage to register the -mtune command line option.
struct RegisterMTuneFlag {
  /// Register the -mtune command-line option.
  LLVM_ABI RegisterMTuneFlag();
};

/// Tools that support stats saving should create this object with static
/// storage to register the --save-stats command line option.
struct RegisterSaveStatsFlag {
  /// Register the --save-stats command-line option.
  LLVM_ABI RegisterSaveStatsFlag();
};

/// Return whether -basic-block-address-map is enabled.
///
/// \return True if -basic-block-address-map is enabled.
LLVM_ABI bool getEnableBBAddrMap();

/// Translate the -basic-block-sections flag into a BasicBlockSection mode.
///
/// \param Options Target options that may receive the BB sections function
///        list buffer when a list file is provided.
/// \return BasicBlockSection mode corresponding to -basic-block-sections.
LLVM_ABI llvm::BasicBlockSection
getBBSectionsMode(llvm::TargetOptions &Options);

/// Initialize a TargetOptions object from the registered CodeGen flags.
///
/// Common utility function tightly tied to the options listed here. TheTriple
/// is used to determine the default value for options if options are not
/// explicitly specified. If those triple dependant options value do not have
/// effect for your component, a default Triple() could be passed in.
///
/// \param TheTriple Triple used to choose default option values when flags
///        are not set explicitly.
/// \return TargetOptions initialized from the registered CodeGen flags.
LLVM_ABI TargetOptions
InitTargetOptionsFromCodeGenFlags(const llvm::Triple &TheTriple);

/// Return the CPU name to use, resolving -mcpu=native if needed.
///
/// \return CPU name to use, with -mcpu=native resolved to the host CPU.
LLVM_ABI std::string getCPUStr();

/// Return the tune CPU name to use, resolving -mtune=native if needed.
///
/// \return Tune CPU name to use, with -mtune=native resolved to the host CPU.
LLVM_ABI std::string getTuneCPUStr();

/// Return the subtarget feature string derived from -mcpu and -mattr.
///
/// \return Subtarget feature string derived from -mcpu and -mattr.
LLVM_ABI std::string getFeaturesStr();

/// Return the list of subtarget features derived from -mcpu and -mattr.
///
/// \return List of subtarget features derived from -mcpu and -mattr.
LLVM_ABI std::vector<std::string> getFeatureList();

/// Add a boolean string attribute named \p Name with value \p Val to \p B.
///
/// \param B Attribute builder that receives the attribute.
/// \param Name Attribute name to set.
/// \param Val Boolean value rendered as "true" or "false".
LLVM_ABI void renderBoolStringAttr(AttrBuilder &B, StringRef Name, bool Val);

/// Set function attributes of function \p F based on CPU, TuneCPU, Features,
/// and command line flags.
///
/// \param F Function whose attributes are updated.
/// \param CPU Target CPU name to apply, if not already present.
/// \param Features Target feature string to apply or append.
/// \param TuneCPU Tune CPU name to apply, if not already present.
LLVM_ABI void setFunctionAttributes(Function &F, StringRef CPU,
                                    StringRef Features, StringRef TuneCPU = "");

/// Set function attributes of functions in Module M based on CPU,
/// TuneCPU, Features, and command line flags.
///
/// \param M Module whose functions and float-abi flag are updated.
/// \param CPU Target CPU name to apply, if not already present.
/// \param Features Target feature string to apply or append.
/// \param TuneCPU Tune CPU name to apply, if not already present.
LLVM_ABI void setFunctionAttributes(Module &M, StringRef CPU,
                                    StringRef Features, StringRef TuneCPU = "");

/// Create a TargetMachine using the options defined on the command line.
///
/// This can be used for tools that do not need further customization of the
/// TargetOptions.
///
/// \param TargetTriple Triple identifying the target architecture and OS.
/// \param OptLevel Optimization level for the created TargetMachine.
/// \return Expected TargetMachine, or an error if creation fails.
LLVM_ABI Expected<std::unique_ptr<TargetMachine>> createTargetMachineForTriple(
    const Triple &TargetTriple,
    CodeGenOptLevel OptLevel = CodeGenOptLevel::Default);

/// Conditionally enable LLVM statistics collection based on --save-stats.
///
/// Must be called before the tool run to actually collect data.
LLVM_ABI void MaybeEnableStatistics();

/// Conditionally save collected LLVM statistics based on --save-stats.
///
/// Should be called after the tool run, and must follow a call to
/// `MaybeEnableStatistics()` to actually have data to write.
///
/// \param OutputFilename Path of the tool's primary output file.
/// \param ToolName Name of the invoking tool, used in error messages.
/// \return Zero on success, or a non-zero status if saving failed.
LLVM_ABI int MaybeSaveStatistics(StringRef OutputFilename, StringRef ToolName);

} // namespace codegen
} // namespace llvm

#endif // LLVM_CODEGEN_COMMANDFLAGS_H
