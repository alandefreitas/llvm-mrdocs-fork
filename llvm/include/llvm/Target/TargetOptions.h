//===-- llvm/Target/TargetOptions.h - Target Options ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines command line option flags that are shared across various
// targets.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGET_TARGETOPTIONS_H
#define LLVM_TARGET_TARGETOPTIONS_H

#include "llvm/ADT/FloatingPointMode.h"
#include "llvm/IR/SystemLibraries.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Compiler.h"

#include <memory>

namespace llvm {
struct fltSemantics;
class MachineFunction;
class MemoryBuffer;

/// Controls when floating-point operations may be fused during code generation.
namespace FPOpFusion {
/// Modes controlling fusion of floating-point operations.
enum FPOpFusionMode {
  Fast,     ///< Enable fusion of FP ops wherever it's profitable.
  Standard, ///< Only allow fusion of 'blessed' ops (currently just fmuladd).
  Strict    ///< Never fuse FP-ops.
};
}

/// Options for partitioning indirect call jump tables.
namespace JumpTable {
/// Strategy for partitioning indirect call jump tables during code generation.
enum JumpTableType {
  Single,     ///< Use a single table for all indirect jumptable calls.
  Arity,      ///< Use one table per number of function parameters.
  /// Use one table per function type after projecting types into four kinds.
  ///
  /// Types are projected into: pointer to non-function, struct, primitive,
  /// and function pointer.
  Simplified,
  Full        ///< Use one table per unique function type.
};
}

/// Threading model assumed for atomics and related code generation.
namespace ThreadModel {
/// Threading model for atomics and related code generation.
enum Model {
  POSIX, ///< POSIX Threads
  Single ///< Single Threaded Environment
};
}

/// Controls whether basic blocks are emitted into separate ELF sections.
enum class BasicBlockSection {
  /// Use Basic Block Sections for all basic blocks.
  ///
  /// A section for every basic block can significantly bloat object file
  /// sizes.
  All,
  /// Selectively enable basic block sections from a function/BB list file.
  ///
  /// Used to control object size bloat from creating sections.
  List,
  /// Blocks identified by passes (e.g. MachineFunctionSplitter); not CLI-settable.
  Preset,
  /// Do not use Basic Block Sections.
  None
};

/// Identify a debugger for "tuning" the debug info.
///
/// The "debugger tuning" concept allows us to present a more intuitive
/// interface that unpacks into different sets of defaults for the various
/// individual feature-flag settings, that suit the preferences of the
/// various debuggers.  However, it's worth remembering that debuggers are
/// not the only consumers of debug info, and some variations in DWARF might
/// better be treated as target/platform issues. Fundamentally,
/// o if the feature is useful (or not) to a particular debugger, regardless
///   of the target, that's a tuning decision;
/// o if the feature is useful (or not) on a particular platform, regardless
///   of the debugger, that's a target decision.
/// It's not impossible to see both factors in some specific case.
enum class DebuggerKind {
  Default, ///< No specific tuning requested.
  GDB,     ///< Tune debug info for gdb.
  LLDB,    ///< Tune debug info for lldb.
  SCE,     ///< Tune debug info for SCE targets (e.g. PS4).
  DBX      ///< Tune debug info for dbx.
};

/// Enable abort calls when global instruction selection fails to lower/select
/// an instruction.
enum class GlobalISelAbortMode {
  Disable,        ///< Disable the abort.
  Enable,         ///< Enable the abort.
  DisableWithDiag ///< Disable the abort but emit a diagnostic on failure.
};

/// Indicates when and how the Swift async frame pointer bit should be set.
enum class SwiftAsyncFramePointerMode {
  /// Determine whether to set the bit statically or dynamically based
  /// on the deployment target.
  DeploymentBased,
  /// Always set the bit.
  Always,
  /// Never set the bit.
  Never,
};

/// \brief Enumeration value for AMDGPU code object version, which is the
/// code object version times 100.
enum CodeObjectVersionKind {
  /// No AMDGPU code object version selected.
  COV_None,
  COV_2 = 200, // Unsupported.
  COV_3 = 300, ///< Unsupported AMDGPU code object version 3.00.
  COV_4 = 400, ///< AMDGPU code object version 4.00.
  COV_5 = 500, ///< AMDGPU code object version 5.00.
  COV_6 = 600, ///< AMDGPU code object version 6.00.
};

/// Shared command-line option flags used across LLVM code generation targets.
class TargetOptions {
public:
  /// Initialize target-lowering options to their conservative defaults.
  TargetOptions()
      : NoTrappingFPMath(true), EnableAIXExtendedAltivecABI(false),
        HonorSignDependentRoundingFPMathOption(false), NoZerosInBSS(false),
        GuaranteedTailCallOpt(false), StackSymbolOrdering(true),
        EnableFastISel(false), EnableGlobalISel(false), UseInitArray(false),
        DisableIntegratedAS(false), FunctionSections(false),
        DataSections(false), IgnoreXCOFFVisibility(false),
        XCOFFTracebackTable(true), UniqueSectionNames(true),
        UniqueBasicBlockSectionNames(false), SeparateNamedSections(false),
        TrapUnreachable(false), NoTrapAfterNoreturn(false), TLSSize(0),
        EmulatedTLS(false), EnableTLSDESC(false), EnableIPRA(false),
        EmitStackSizeSection(false), EnableMachineOutliner(false),
        EnableMachineFunctionSplitter(false),
        EnableStaticDataPartitioning(false), SupportsDefaultOutlining(false),
        EnableDefaultMachineVerifier(true), EmitAddrsig(false),
        BBAddrMap(false), EmitCallGraphSection(false), EmitCallSiteInfo(false),
        SupportsDebugEntryValues(false), EnableDebugEntryValues(false),
        ValueTrackingVariableLocations(false), ForceDwarfFrameSection(false),
        XRayFunctionIndex(true), DebugStrictDwarf(false), Hotpatch(false),
        PPCGenScalarMASSEntries(false), JMCInstrument(false),
        EnableCFIFixup(false), MisExpect(false), XCOFFReadOnlyPointers(false),
        VerifyArgABICompliance(true) {}

  /// Return true if frame pointer elimination should be disabled for \p MF.
  /// \param MF Machine function to query.
  /// \return True if frame pointer elimination should be disabled for \p MF.
  LLVM_ABI bool DisableFramePointerElim(const MachineFunction &MF) const;

  /// Return true if the frame pointer is reserved in \p MF.
  ///
  /// The frame pointer must always either point to a new frame record or be
  /// un-modified in the given function.
  /// \param MF Machine function to query.
  /// \return True if the frame pointer is reserved in \p MF.
  LLVM_ABI bool FramePointerIsReserved(const MachineFunction &MF) const;

  /// If greater than 0, override the default value of
  /// MCAsmInfo::BinutilsVersion.
  std::pair<int, int> BinutilsVersion{0, 0};

  /// Assume there are no FP exception trap handlers when set.
  ///
  /// This flag is enabled when -enable-no-trapping-fp-math is specified on
  /// the command line. This specifies that there are no trap handlers to
  /// handle exceptions.
  unsigned NoTrappingFPMath : 1;

  /// Enable the AIX extended Altivec ABI for volatile and nonvolatile vectors.
  ///
  /// This flag is true when -vec-extabi is specified. The code generator is
  /// then able to use both volatile and nonvolitle vector registers. When
  /// false, the code generator only uses volatile vector registers which is
  /// the default setting on AIX.
  unsigned EnableAIXExtendedAltivecABI : 1;

  /// Assume floating-point rounding mode may change dynamically when set.
  ///
  /// This is set when -enable-sign-dependent-rounding-fp-math is specified.
  /// If this is false (the default), the code generator is allowed to assume
  /// that the rounding behavior is the default (round-to-zero for all
  /// floating point to integer conversions, and round-to-nearest for all
  /// other arithmetic truncations). If this is enabled (set to true), the
  /// code generator must assume that the rounding mode may dynamically
  /// change.
  unsigned HonorSignDependentRoundingFPMathOption : 1;
  /// Return true if sign-dependent rounding for FP math must be honored.
  /// \return True if sign-dependent rounding for FP math must be honored.
  LLVM_ABI bool HonorSignDependentRoundingFPMath() const;

  /// NoZerosInBSS - By default some codegens place zero-initialized data to
  /// .bss section. This flag disables such behaviour (necessary, e.g. for
  /// crt*.o compiling).
  unsigned NoZerosInBSS : 1;

  /// Perform guaranteed tail call optimization for eligible fastcc calls.
  ///
  /// This flag is enabled when -tailcallopt is specified on the commandline.
  /// When the flag is on, participating targets will perform tail call
  /// optimization on all calls which use the fastcc calling convention and
  /// which satisfy certain target-independent criteria (being at the end of
  /// a function, having the same return type as their parent function, etc.),
  /// using an alternate ABI if necessary.
  unsigned GuaranteedTailCallOpt : 1;

  /// Allow CodeGen to reorder local stack symbols for size or locality.
  ///
  /// When true, this will allow CodeGen to order the local stack symbols
  /// (for code size, code locality, or any other heuristics). When false,
  /// the local symbols are left in whatever order they were generated.
  /// Default is true.
  unsigned StackSymbolOrdering : 1;

  /// EnableFastISel - This flag enables fast-path instruction selection
  /// which trades away generated code quality in favor of reducing
  /// compile time.
  unsigned EnableFastISel : 1;

  /// EnableGlobalISel - This flag enables global instruction selection.
  unsigned EnableGlobalISel : 1;

  /// EnableGlobalISelAbort - Control abort behaviour when global instruction
  /// selection fails to lower/select an instruction.
  GlobalISelAbortMode GlobalISelAbort = GlobalISelAbortMode::Enable;

  /// Control when and how the Swift async frame pointer bit should
  /// be set.
  SwiftAsyncFramePointerMode SwiftAsyncFramePointer =
      SwiftAsyncFramePointerMode::Always;

  /// UseInitArray - Use .init_array instead of .ctors for static
  /// constructors.
  unsigned UseInitArray : 1;

  /// Disable the integrated assembler.
  unsigned DisableIntegratedAS : 1;

  /// Emit functions into separate sections.
  unsigned FunctionSections : 1;

  /// Emit data into separate sections.
  unsigned DataSections : 1;

  /// Do not emit visibility attribute for xcoff.
  unsigned IgnoreXCOFFVisibility : 1;

  /// Emit XCOFF traceback table.
  unsigned XCOFFTracebackTable : 1;

  /// Use unique names for object file sections.
  unsigned UniqueSectionNames : 1;

  /// Use unique names for basic block sections.
  unsigned UniqueBasicBlockSectionNames : 1;

  /// Emit named sections with the same name into different sections.
  unsigned SeparateNamedSections : 1;

  /// Emit target-specific trap instruction for 'unreachable' IR instructions.
  unsigned TrapUnreachable : 1;

  /// Do not emit a trap instruction for 'unreachable' IR instructions behind
  /// noreturn calls, even if TrapUnreachable is true.
  unsigned NoTrapAfterNoreturn : 1;

  /// Bit size of immediate TLS offsets (0 == use the default).
  unsigned TLSSize : 8;

  /// EmulatedTLS - This flag enables emulated TLS model, using emutls
  /// function in the runtime library..
  unsigned EmulatedTLS : 1;

  /// EnableTLSDESC - This flag enables TLS Descriptors.
  unsigned EnableTLSDESC : 1;

  /// This flag enables InterProcedural Register Allocation (IPRA).
  unsigned EnableIPRA : 1;

  /// Emit section containing metadata on function stack sizes.
  unsigned EmitStackSizeSection : 1;

  /// Enables the MachineOutliner pass.
  unsigned EnableMachineOutliner : 1;

  /// Enables the MachineFunctionSplitter pass.
  unsigned EnableMachineFunctionSplitter : 1;

  /// Enables the StaticDataSplitter pass.
  unsigned EnableStaticDataPartitioning : 1;

  /// Set if the target supports default outlining behaviour.
  unsigned SupportsDefaultOutlining : 1;

  /// Enable Machine verifier at the end of default codegen pipelines. (Only
  /// used with NPM)
  unsigned EnableDefaultMachineVerifier : 1;

  /// Emit address-significance table.
  unsigned EmitAddrsig : 1;

  /// Emit the SHT_LLVM_BB_ADDR_MAP section for basic block addresses.
  ///
  /// The section contains basic block addresses which can be used to map
  /// virtual addresses to machine basic blocks.
  unsigned BBAddrMap : 1;

  /// Emit basic blocks into separate sections.
  BasicBlockSection BBSections = BasicBlockSection::None;

  /// Memory Buffer that contains information on sampled basic blocks and used
  /// to selectively generate basic block sections.
  std::shared_ptr<MemoryBuffer> BBSectionsFuncListBuf;

  /// Emit section containing call graph metadata.
  unsigned EmitCallGraphSection : 1;

  /// Enable production of call site info for debug info in optimized code.
  ///
  /// The flag enables call site info production. It is used only for debug
  /// info, and it is restricted only to optimized code. This can be used for
  /// something else, so that should be controlled in the frontend.
  unsigned EmitCallSiteInfo : 1;
  /// Set if the target supports the debug entry values by default.
  unsigned SupportsDebugEntryValues : 1;
  /// Force production of debug entry values even if the target lacks support.
  ///
  /// When set to true, the EnableDebugEntryValues option forces production
  /// of debug entry values even if the target does not officially support
  /// it. Useful for testing purposes only. This flag should never be checked
  /// directly, always use \ref ShouldEmitDebugEntryValues instead.
  unsigned EnableDebugEntryValues : 1;
  /// NOTE: There are targets that still do not support the debug entry values
  /// production.
  /// \return True if debug entry values should be emitted.
  LLVM_ABI bool ShouldEmitDebugEntryValues() const;

  /// Use experimental value-tracking debug variable location information.
  ///
  /// When set to true, use experimental new debug variable location tracking,
  /// which seeks to follow the values of variables rather than their location,
  /// post isel.
  unsigned ValueTrackingVariableLocations : 1;

  /// Emit DWARF debug frame section.
  unsigned ForceDwarfFrameSection : 1;

  /// Emit XRay Function Index section
  unsigned XRayFunctionIndex : 1;

  /// When set to true, don't use DWARF extensions in later DWARF versions.
  /// By default, it is set to false.
  unsigned DebugStrictDwarf : 1;

  /// Emit the hotpatch flag in CodeView debug.
  unsigned Hotpatch : 1;

  /// Enables scalar MASS conversions
  unsigned PPCGenScalarMASSEntries : 1;

  /// Enable JustMyCode instrumentation.
  unsigned JMCInstrument : 1;

  /// Enable the CFIFixup pass.
  unsigned EnableCFIFixup : 1;

  /// When set to true, enable MisExpect Diagnostics
  /// By default, it is set to false
  unsigned MisExpect : 1;

  /// When set to true, const objects with relocatable address values are put
  /// into the RO data section.
  unsigned XCOFFReadOnlyPointers : 1;

  /// Verify call/return extensions of narrow integers in the target backend.
  ///
  /// When set to true, call/return argument extensions of narrow integers
  /// are verified in the target backend if it cares about them. This is
  /// not done with internal tools like llc that run many tests that ignore
  /// (lack) these extensions.
  unsigned VerifyArgABICompliance : 1;

  /// Name of the stack usage file (i.e., .su file) if user passes
  /// -fstack-usage. If empty, it can be implied that -fstack-usage is not
  /// passed on the command line.
  std::string StackUsageFile;

  /// If greater than 0, override TargetLoweringBase::PrefLoopAlignment.
  unsigned LoopAlignment = 0;

  /// Controls formation of fused FP ops that keep excess intermediate precision.
  ///
  /// This flag is set by the -fp-contract=xxx option. Fast mode allows
  /// formation of fused FP ops whenever they're profitable. Standard mode
  /// allows fusion only for 'blessed' FP ops (currently just the fmuladd
  /// intrinsic; more may be added later). Strict mode allows fusion only
  /// if/when it can be proven that the excess precision won't affect the
  /// result.
  ///
  /// Note: This option only controls formation of fused ops by the
  /// optimizers.  Fused operations that are explicitly specified (e.g. FMA
  /// via the llvm.fma.* intrinsic) will always be honored, regardless of
  /// the value of this option.
  FPOpFusion::FPOpFusionMode AllowFPOpFusion = FPOpFusion::Standard;

  /// ThreadModel - This flag specifies the type of threading model to assume
  /// for things like atomics
  ThreadModel::Model ThreadModel = ThreadModel::POSIX;

  /// EABIVersion - This flag specifies the EABI version
  EABI EABIVersion = EABI::Default;

  /// Which debugger to tune for.
  DebuggerKind DebuggerTuning = DebuggerKind::Default;

  /// Vector math library to use.
  VectorLibrary VecLib = VectorLibrary::NoLibrary;

public:
  /// What exception model to use
  ExceptionHandling ExceptionModel = ExceptionHandling::None;

  /// Machine level options.
  MCTargetOptions MCOptions;

  /// Stores the filename/path of the final .o/.obj file, to be written in the
  /// debug information. This is used for emitting the CodeView S_OBJNAME
  /// record.
  std::string ObjectFilenameForDebug;
};

} // namespace llvm

#endif
