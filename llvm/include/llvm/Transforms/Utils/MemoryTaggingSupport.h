//===- MemoryTaggingSupport.h - helpers for memory tagging implementations ===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares common infrastructure for HWAddressSanitizer and
// Aarch64StackTagging.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TRANSFORMS_UTILS_MEMORYTAGGINGSUPPORT_H
#define LLVM_TRANSFORMS_UTILS_MEMORYTAGGINGSUPPORT_H

#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/StackSafetyAnalysis.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/Support/Alignment.h"

namespace llvm {
class DominatorTree;
class IntrinsicInst;
class PostDominatorTree;
class AllocaInst;
class Instruction;
/// Helpers shared by HWAddressSanitizer and AArch64 stack tagging.
namespace memtag {
/// Per-alloca lifetime and debug info collected for memory tagging.
struct AllocaInfo {
  /// First and last lifetime intrinsic kinds seen in one basic block.
  struct BBInfo {
    /// Kind of the first lifetime intrinsic in the block, if any.
    Intrinsic::ID First = Intrinsic::not_intrinsic;
    /// Kind of the last lifetime intrinsic in the block, if any.
    Intrinsic::ID Last = Intrinsic::not_intrinsic;
  };
  /// Alloca whose lifetime and debug uses are tracked.
  AllocaInst *AI;
  /// Lifetime start intrinsics that reference this alloca.
  SmallVector<IntrinsicInst *, 2> LifetimeStart;
  /// Lifetime end intrinsics that reference this alloca.
  SmallVector<IntrinsicInst *, 2> LifetimeEnd;
  /// Debug variable records that refer to this alloca.
  SmallVector<DbgVariableRecord *, 2> DbgVariableRecords;
  /// Per-block summary of the first and last lifetime intrinsic kinds.
  MapVector<BasicBlock *, struct BBInfo> BBInfos;
};

/// Invoke a callback for every reachable exit from an alloca's lifetime.
///
/// For an alloca valid between its lifetime markers, calls \p Callback for all
/// possible exits out of the lifetime in the containing function. Exits include
/// the lifetime-end markers themselves and any function exits in \p RetVec that
/// can be reached without first passing through a lifetime end. If lifetime
/// ends do not cover every exit, the caller should remove those ends so work
/// done at the other exits does not run outside the lifetime.
/// @param DT Dominator tree for reachability queries.
/// @param PDT Post-dominator tree used for the single start/end fast path.
/// @param LI Loop info for reachability queries.
/// @param AInfo Alloca lifetime markers and per-block lifetime summary.
/// @param RetVec Function-exit instructions that may leave the lifetime.
/// @param Callback Invoked for each lifetime end and uncovered function exit.
LLVM_ABI void
forAllReachableExits(const DominatorTree &DT, const PostDominatorTree &PDT,
                     const LoopInfo &LI, const AllocaInfo &AInfo,
                     const SmallVectorImpl<Instruction *> &RetVec,
                     llvm::function_ref<void(Instruction *)> Callback);

/// Return true when the alloca's lifetime markers form a supported pattern.
/// @param AInfo Alloca lifetime markers and per-block lifetime summary.
/// @param DT Dominator tree for reachability queries, or nullptr.
/// @param LI Loop info for reachability queries, or nullptr.
/// @return True when the lifetime pattern can be instrumented safely.
LLVM_ABI bool isSupportedLifetime(const AllocaInfo &AInfo,
                                  const DominatorTree *DT, const LoopInfo *LI);

/// Return the instruction at which stack untagging should run for a function
/// exit, or null if \p Inst is not such an exit.
/// @param Inst Instruction that may be a return, resume, or cleanup return.
/// @return Untag insertion point, or nullptr when \p Inst is not a function
/// exit.
LLVM_ABI Instruction *getUntagLocationIfFunctionExit(Instruction &Inst);

/// Aggregated stack tagging information for a function.
struct StackInfo {
  /// Allocas selected for instrumentation, keyed by the alloca instruction.
  MapVector<AllocaInst *, AllocaInfo> AllocasToInstrument;
  /// Function-exit instructions where stack tags should be cleared.
  SmallVector<Instruction *, 8> RetVec;
  /// True when the function may return twice (e.g. via setjmp).
  bool CallsReturnTwice = false;
};

/// Classification of whether an alloca should be memory-tagged.
enum class AllocaInterestingness {
  kUninteresting, ///< Uninteresting because of the nature of the alloca.
  kSafe,          ///< Uninteresting because proven safe.
  kInteresting    ///< Interesting and should be instrumented.
};

/// Collects stack tagging information by visiting function instructions.
class StackInfoBuilder {
public:
  /// Construct a builder optionally backed by stack-safety analysis.
  /// @param SSI Stack safety info used to skip proven-safe allocas, or
  /// nullptr.
  /// @param DebugType Pass name used when emitting optimization remarks.
  StackInfoBuilder(const StackSafetyGlobalInfo *SSI, const char *DebugType)
      : SSI(SSI), DebugType(DebugType) {}

  /// Record alloca, lifetime, debug, and exit info from \p Inst.
  /// @param ORE Emitter for safe/missed-safe alloca remarks.
  /// @param Inst Instruction (or attached debug records) to inspect.
  LLVM_ABI void visit(OptimizationRemarkEmitter &ORE, Instruction &Inst);
  /// Classify whether \p AI should be instrumented for memory tagging.
  /// @param AI Alloca to classify.
  /// @return Interestingness of \p AI for stack tagging.
  LLVM_ABI AllocaInterestingness getAllocaInterestingness(const AllocaInst &AI);
  /// Return the stack info collected so far.
  /// @return Mutable reference to the accumulated \c StackInfo.
  StackInfo &get() { return Info; };

private:
  StackInfo Info;
  const StackSafetyGlobalInfo *SSI;
  const char *DebugType;
};

/// Return the size in bytes of the object allocated by \p AI.
/// @param AI Alloca whose allocation size is requested.
/// @return Allocation size in bytes.
LLVM_ABI uint64_t getAllocaSizeInBytes(const AllocaInst &AI);
/// Raise an alloca's alignment and pad its type up to \p Align.
/// @param Info Alloca info whose \c AI is replaced when padding is needed.
/// @param Align Required alignment; also used as the padding multiple.
LLVM_ABI void alignAndPadAlloca(memtag::AllocaInfo &Info, llvm::Align Align);

/// Emit an \c llvm.read_register intrinsic for the named register.
/// @param IRB Builder used to create the intrinsic.
/// @param Name Target register name metadata string.
/// @return Value of the named register.
LLVM_ABI Value *readRegister(IRBuilder<> &IRB, StringRef Name);
/// Return the current frame pointer as an integer.
/// @param IRB Builder used to create the frameaddress intrinsic.
/// @return Frame pointer converted to an integer pointer-sized value.
LLVM_ABI Value *getFP(IRBuilder<> &IRB);
/// Return the program counter value for the current insertion point.
/// @param TargetTriple Target triple selecting the PC read strategy.
/// @param IRB Builder used to create the PC value.
/// @return PC as a pointer-sized integer.
LLVM_ABI Value *getPC(const Triple &TargetTriple, IRBuilder<> &IRB);
/// Return a pointer to Android's sanitizer TLS slot at index \p Slot.
/// @param IRB Builder used to create the thread-pointer GEP.
/// @param Slot TLS slot index (byte offset is \c 8 * Slot).
/// @return Pointer into the Android sanitizer TLS area.
LLVM_ABI Value *getAndroidSlotPtr(IRBuilder<> &IRB, int Slot);
/// Return a pointer to Darwin's sanitizer TSD slot at index \p Slot.
/// @param IRB Builder used to create the TPIDRRO_EL0-based GEP.
/// @param Slot TSD slot index (byte offset is \c 8 * Slot).
/// @return Pointer into the Darwin sanitizer TSD area.
LLVM_ABI Value *getDarwinSlotPtr(IRBuilder<> &IRB, int Slot);

/// Prepend a DWARF tag-offset of \p Tag to debug records for the alloca.
/// @param Info Alloca whose debug variable records are annotated.
/// @param Tag Tag offset written into each matching DIExpression.
LLVM_ABI void annotateDebugRecords(AllocaInfo &Info, unsigned int Tag);
/// Advance a thread-local ring-buffer pointer with wraparound.
/// @param IRB Builder used to create the add-and-mask update.
/// @param ThreadLong Current ring-buffer pointer with size encoded in high
/// bits.
/// @param Inc Byte increment applied before wrap (must divide 4096).
/// @param IsMemtagDarwin When true, use Darwin's high-bit size encoding.
/// @return Updated ring-buffer pointer after increment and wrap.
LLVM_ABI Value *incrementThreadLong(IRBuilder<> &IRB, Value *ThreadLong,
                                    unsigned int Inc,
                                    bool IsMemtagDarwin = false);

} // namespace memtag
} // namespace llvm

#endif
