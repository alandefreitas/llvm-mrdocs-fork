//===- CGPassBuilderOption.h - Options for pass builder ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the CCState and CCValAssign classes, used for lowering
// and implementing calling conventions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGET_CGPASSBUILDEROPTION_H
#define LLVM_TARGET_CGPASSBUILDEROPTION_H

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Target/TargetOptions.h"
#include <optional>

namespace llvm {

/// Controls when the machine outliner runs during code generation.
enum class RunOutliner {
  /// Use the target's default outlining policy.
  TargetDefault,
  /// Outline on all functions guaranteed to be beneficial.
  AlwaysOutline,
  /// Outline cold code, treating missing profile data as cold.
  OptimisticPGO,
  /// Outline cold code, treating missing profile data as hot.
  ConservativePGO,
  /// Disable all machine outlining.
  NeverOutline
};
/// Identifies which register allocator the codegen pipeline should use.
enum class RegAllocType {
  /// No allocator was specified.
  Unset,
  /// Pick an allocator from the optimization level.
  Default,
  /// The basic register allocator.
  Basic,
  /// The fast register allocator.
  Fast,
  /// The greedy register allocator.
  Greedy,
  /// The Partitioned Boolean Quadratic Programming allocator.
  PBQP
};

/// Command-line parser mapping allocator names to \c RegAllocType.
class RegAllocTypeParser : public cl::parser<RegAllocType> {
public:
  /// Construct a parser bound to command-line option \p O.
  /// \param O The option whose value this parser interprets.
  RegAllocTypeParser(cl::Option &O) : cl::parser<RegAllocType>(O) {}
  /// Register supported \c RegAllocType names as option literals.
  void initialize() {
    cl::parser<RegAllocType>::initialize();
    addLiteralOption("default", RegAllocType::Default,
                     "Default register allocator");
    addLiteralOption("pbqp", RegAllocType::PBQP, "PBQP register allocator");
    addLiteralOption("fast", RegAllocType::Fast, "Fast register allocator");
    addLiteralOption("basic", RegAllocType::Basic, "Basic register allocator");
    addLiteralOption("greedy", RegAllocType::Greedy,
                     "Greedy register allocator");
  }
};

/// Options that configure the code-generation pass pipeline.
///
/// Not one-on-one but mostly corresponding to commandline options in
/// TargetPassConfig.cpp.
struct CGPassBuilderOption {
  /// Whether to use the optimized register-allocation pipeline.
  cl::boolOrDefault OptimizeRegAlloc = cl::boolOrDefault::BOU_UNSET;
  /// Enable interprocedural register allocation when set.
  std::optional<bool> EnableIPRA;
  /// Print pass-manager debugging information.
  bool DebugPM = false;
  /// Skip verifying IR and machine code in the codegen pipeline.
  bool DisableVerify = false;
  /// Fold null checks into faulting memory operations.
  bool EnableImplicitNullChecks = false;
  /// Collect probability-driven block placement statistics.
  bool EnableBlockPlacementStats = false;
  /// Enable hash-based global function merging.
  bool EnableGlobalMergeFunc = false;
  /// Split cold blocks out of machine functions using profile data.
  bool EnableMachineFunctionSplitter = false;
  /// Sink and fold computations into addressing modes or copies.
  bool EnableSinkAndFold = false;
  /// Merge identical tails of successor blocks.
  bool EnableTailMerge = true;
  /// Enable LoopTermFold immediately after LSR.
  bool EnableLoopTermFold = false;
  /// Run MachineScheduler after register allocation.
  bool MISchedPostRA = false;
  /// Run live-interval analysis earlier in the pipeline.
  bool EarlyLiveIntervals = false;
  /// Garbage-collect empty basic blocks.
  bool EnableGCEmptyBlocks = false;

  /// Disable the loop strength reduction pass.
  bool DisableLSR = false;
  /// Disable the CodeGenPrepare pass.
  bool DisableCGP = false;
  /// Disable partial inlining of library calls.
  bool DisablePartialLibcallInlining = false;
  /// Disable the ConstantHoisting pass.
  bool DisableConstantHoisting = false;
  /// Disable the select-optimization pass.
  bool DisableSelectOptimize = true;
  /// Disable atexit-based global destructor lowering on MachO.
  bool DisableAtExitBasedGlobalDtorLowering = false;
  /// Disable expansion of reduction intrinsics.
  bool DisableExpandReductions = false;
  /// Disable loading flow-sensitive profiles before register allocation.
  bool DisableRAFSProfileLoader = false;
  /// Disable the CFI fixup pass.
  bool DisableCFIFixup = false;
  /// Print machine instructions after instruction selection.
  bool PrintAfterISel = false;
  /// Print the LLVM IR input to instruction selection.
  bool PrintISelInput = false;
  /// Print register-usage details collected for IPRA.
  bool PrintRegUsage = false;
  /// Require that callees are generated before callers.
  bool RequiresCodeGenSCCOrder = false;

  /// Policy controlling when the machine outliner runs.
  RunOutliner EnableMachineOutliner = RunOutliner::TargetDefault;
  /// Register allocator used by the new pass manager pipeline.
  RegAllocType RegAlloc = RegAllocType::Unset;
  /// Override the GlobalISel abort mode when set.
  std::optional<GlobalISelAbortMode> EnableGlobalISelAbort;
  /// Path to a flow-sensitive profile file.
  std::string FSProfileFile;
  /// Path to a remapping file for the flow-sensitive profile.
  std::string FSRemappingFile;

  /// Verify generated machine instructions when set.
  cl::boolOrDefault VerifyMachineCode = cl::boolOrDefault::BOU_UNSET;
  /// Enable the fast instruction selector when set.
  cl::boolOrDefault EnableFastISelOption = cl::boolOrDefault::BOU_UNSET;
  /// Enable the global instruction selector when set.
  cl::boolOrDefault EnableGlobalISelOption = cl::boolOrDefault::BOU_UNSET;
  /// Debugify MIR before each safe pass and strip debug info after.
  cl::boolOrDefault DebugifyAndStripAll = cl::boolOrDefault::BOU_UNSET;
  /// Debugify MIR before each safe pass, then check and strip debug info.
  cl::boolOrDefault DebugifyCheckAndStripAll = cl::boolOrDefault::BOU_UNSET;
};

/// Return codegen pass-builder options populated from command-line flags.
/// @return Codegen pass-builder options populated from command-line flags.
LLVM_ABI CGPassBuilderOption getCGPassBuilderOption();

} // namespace llvm

#endif // LLVM_TARGET_CGPASSBUILDEROPTION_H
