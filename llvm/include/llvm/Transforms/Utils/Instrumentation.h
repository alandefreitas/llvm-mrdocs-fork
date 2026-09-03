//===- Transforms/Instrumentation.h - Instrumentation passes ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines constructor functions for instrumentation passes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_H

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/Support/Compiler.h"
#include <cassert>
#include <cstdint>
#include <limits>
#include <string>

namespace llvm {

class Triple;
class OptimizationRemarkEmitter;
class Comdat;
class CallBase;
class Module;

/// Check if module has flag attached, if not add the flag.
///
/// \param M Module to inspect and possibly update with \p Flag.
/// \param Flag Module flag name that marks prior instrumentation.
/// \return True if \p Flag was already present; false if it was added.
LLVM_ABI bool checkIfAlreadyInstrumented(Module &M, StringRef Flag);

/// Move entry-block-only instructions before a planned split point.
///
/// Instrumentation passes often insert conditional checks into entry blocks.
/// Call this function before splitting the entry block to move instructions
/// that must remain in the entry block up before the split point. Static
/// allocas and llvm.localescape calls, for example, must remain in the entry
/// block.
/// \param BB Entry basic block that will be split.
/// \param IP Iterator at the intended split point within \p BB.
/// \return Updated iterator to the intended split point after moves.
LLVM_ABI BasicBlock::iterator PrepareToSplitEntryBlock(BasicBlock &BB,
                                                       BasicBlock::iterator IP);

/// Create a private global holding the bytes of \p Str for runtime use.
///
/// \param M Module that will own the new global.
/// \param Str String contents to place in the global.
/// \param AllowMerging Whether identical string globals may be merged.
/// \param NamePrefix Optional name prefix for the created global.
/// \return Newly created private global variable for \p Str.
LLVM_ABI GlobalVariable *createPrivateGlobalForString(Module &M, StringRef Str,
                                                      bool AllowMerging,
                                                      Twine NamePrefix = "");

/// Return \p F's comdat, creating and attaching one when missing.
///
/// \param F Function whose comdat is returned or created.
/// \param T Target triple used when choosing a comdat strategy.
/// \return \p F's comdat, or nullptr on failure.
LLVM_ABI Comdat *getOrCreateFunctionComdat(Function &F, Triple &T);

/// Place \p GV in a large section on x86-64 ELF to ease relocation pressure.
///
/// This can be used for metadata globals that aren't directly accessed by
/// code, which has no performance impact.
/// \param TargetTriple Triple that decides whether large sections apply.
/// \param GV Global variable to assign a large section.
LLVM_ABI void setGlobalVariableLargeSection(const Triple &TargetTriple,
                                            GlobalVariable &GV);

/// Options controlling GCOV-style profiling instrumentation.
struct GCOVOptions {
  /// Return GCOV options with the usual frontend defaults.
  ///
  /// \return Default GCOVOptions values used by the frontend.
  LLVM_ABI static GCOVOptions getDefault();

  /// Whether to emit .gcno note files.
  bool EmitNotes;

  /// Whether to emit runtime instrumentation that writes .gcda data files.
  bool EmitData;

  /// Four-byte GCOV format version string (see gcc's gcov-io.h).
  char Version[4];

  /// Whether to add the 'noredzone' attribute to added runtime library calls.
  bool NoRedZone;

  /// Whether to use atomic profile counter increments.
  bool Atomic = false;

  /// Semicolon-separated regexes selecting files to instrument.
  std::string Filter;

  /// Semicolon-separated regexes selecting files to skip.
  std::string Exclude;
};

/// Helpers shared by PGO-driven and sample-profile indirect-call promotion.
///
/// The pgo-specific indirect call promotion function declared below is used by
/// the pgo-driven indirect call promotion and sample profile passes. It's a
/// wrapper around llvm::promoteCall, et al. that additionally computes !prof
/// metadata. We place it in a pgo namespace so it's not confused with the
/// generic utilities.
namespace pgo {

/// Promote an indirect call or invoke \p CB to a conditional direct call to \p F.
///
/// Transforms \p CB (an indirect-call or invoke) into the equivalent of:
///     if (Inst.CalledValue == F)
///        F(...);
///     else
///        Inst(...);
/// \p TotalCount is the profile count value that the instruction executes.
/// \p Count is the profile count value that \p F is the target function.
/// These two values are used to update the branch weight.
/// If \p AttachProfToDirectCall is true, a prof metadata is attached to the
/// new direct call to contain \p Count.
/// \param CB Indirect call or invoke to promote.
/// \param F Hot callee to test for and call directly.
/// \param Count Profile count of calls from \p CB that target \p F.
/// \param TotalCount Profile count of all executions of \p CB.
/// \param AttachProfToDirectCall Whether to attach `!prof` with \p Count to
///        the new direct call.
/// \param ORE Optional remark emitter for promotion diagnostics.
/// \return The promoted direct call instruction.
LLVM_ABI CallBase &promoteIndirectCall(CallBase &CB, Function *F,
                                       uint64_t Count, uint64_t TotalCount,
                                       bool AttachProfToDirectCall,
                                       OptimizationRemarkEmitter *ORE);
} // namespace pgo

/// Options for the frontend instrumentation based profiling pass.
struct InstrProfOptions {
  /// Whether to add the 'noredzone' attribute to added runtime library calls.
  bool NoRedZone = false;

  /// Whether to promote profile counters into registers.
  bool DoCounterPromotion = false;

  /// Whether to use atomic profile counter increments.
  bool Atomic = false;

  /// Whether to use Block Frequency Info to guide counter promotion.
  bool UseBFIInPromotion = false;

  /// Whether to sample instrumentation to reduce runtime overhead.
  bool Sampling = false;

  /// Name of the profile file to use as output.
  std::string InstrProfileOutput;

  /// Construct InstrProf options with default field values.
  InstrProfOptions() = default;
};

/// Create the module global used for profile sampling.
///
/// \param M Module that will own the sampling variable.
LLVM_ABI void createProfileSamplingVar(Module &M);

/// Options for SanitizerCoverage instrumentation.
struct SanitizerCoverageOptions {
  /// Granularity of coverage points to instrument.
  enum Type {
    SCK_None = 0, ///< No coverage instrumentation.
    SCK_Function, ///< Instrument at function granularity.
    SCK_BB,       ///< Instrument every basic block.
    SCK_Edge      ///< Instrument every control-flow edge.
  } CoverageType = SCK_None; ///< Selected coverage point granularity.
  /// Whether to collect coverage for indirect calls.
  bool IndirectCalls = false;
  /// Whether to emit basic-block tracing callbacks.
  bool TraceBB = false;
  /// Whether to trace comparison instructions.
  bool TraceCmp = false;
  /// Whether to trace division instructions.
  bool TraceDiv = false;
  /// Whether to trace GEP instructions.
  bool TraceGep = false;
  /// Whether to use 8-bit coverage counters.
  bool Use8bitCounters = false;
  /// Whether to emit PC tracing callbacks.
  bool TracePC = false;
  /// Whether to emit separate PC entry/exit tracing callbacks.
  bool TracePCEntryExit = false;
  /// Whether to emit PC tracing with a per-edge guard.
  bool TracePCGuard = false;
  /// Whether to increment inline 8-bit counters for every edge.
  bool Inline8bitCounters = false;
  /// Whether to set an inline boolean flag for every edge.
  bool InlineBoolFlag = false;
  /// Whether to create a static PC table of instrumented blocks.
  bool PCTable = false;
  /// Whether to skip pruning of instrumented blocks.
  bool NoPrune = false;
  /// Whether to trace maximum stack depth.
  bool StackDepth = false;
  /// Whether to trace load instructions.
  bool TraceLoads = false;
  /// Whether to trace store instructions.
  bool TraceStores = false;
  /// Whether to collect per-function control-flow graphs.
  bool CollectControlFlow = false;
  /// Whether to gate tracing callbacks behind a global enable flag.
  bool GatedCallbacks = false;
  /// Minimum stack depth before invoking the stack-depth callback.
  int StackDepthCallbackMin = 0;

  /// Construct SanitizerCoverage options with default field values.
  SanitizerCoverageOptions() = default;
};

/// IRBuilder that ensures inserted instrumentation has a DebugLocation.
///
/// If none is attached to the source instruction, try to use a DILocation with
/// offset 0 scoped to surrounding function (if it has a DebugLocation).
///
/// Some non-call instructions may be missing debug info, but when inserting
/// instrumentation calls, some builds (e.g. LTO) want calls to have debug info
/// if the enclosing function does.
struct InstrumentationIRBuilder : IRBuilder<> {
  /// Attach a fallback DebugLocation to \p IRB when it has none.
  ///
  /// \param IRB Builder whose current debug location may be set.
  /// \param F Function used as the scope for a synthetic DILocation.
  static void ensureDebugInfo(IRBuilder<> &IRB, const Function &F) {
    if (IRB.getCurrentDebugLocation())
      return;
    if (DISubprogram *SP = F.getSubprogram())
      IRB.SetCurrentDebugLocation(DILocation::get(SP->getContext(), 0, 0, SP));
  }

  /// Construct a builder at \p IP and ensure it has debug info.
  ///
  /// \param IP Insertion point whose parent function supplies debug scope.
  explicit InstrumentationIRBuilder(Instruction *IP) : IRBuilder<>(IP) {
    ensureDebugInfo(*this, *IP->getFunction());
  }

  /// Construct a builder at \p It in \p BB and ensure it has debug info.
  ///
  /// \param BB Basic block that owns the insertion point.
  /// \param It Iterator where new instructions will be inserted.
  explicit InstrumentationIRBuilder(BasicBlock *BB, BasicBlock::iterator It)
      : IRBuilder<>(BB, It) {
    ensureDebugInfo(*this, *BB->getParent());
  }
};
} // end namespace llvm

#endif // LLVM_TRANSFORMS_INSTRUMENTATION_H
