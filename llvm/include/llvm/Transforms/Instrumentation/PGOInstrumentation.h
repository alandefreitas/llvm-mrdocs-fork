//===- Transforms/Instrumentation/PGOInstrumentation.h ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file provides the interface for IR based instrumentation passes (
/// (profile-gen, and profile-use).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_PGOINSTRUMENTATION_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_PGOINSTRUMENTATION_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <cstdint>
#include <string>

namespace llvm {

class Function;
class Instruction;
class Module;

/// The instrumentation (profile-instr-gen) pass for IR based PGO.
// We use this pass to create COMDAT profile variables for context
// sensitive PGO (CSPGO). The reason to have a pass for this is CSPGO
// can be run after LTO/ThinLTO linking. Lld linker needs to see
// all the COMDAT variables before linking. So we have this pass
// always run before linking for CSPGO.
class PGOInstrumentationGenCreateVar
    : public OptionalPassInfoMixin<PGOInstrumentationGenCreateVar> {
public:
  /// Construct a pass that creates COMDAT profile variables for CSPGO.
  /// @param CSInstrName Name used for context-sensitive instrumented counters.
  /// @param Sampling Whether profile sampling is enabled.
  PGOInstrumentationGenCreateVar(std::string CSInstrName = "",
                                 bool Sampling = false)
      : CSInstrName(CSInstrName), ProfileSampling(Sampling) {}
  /// Create COMDAT profile variables in \p M for context-sensitive PGO.
  /// @param M Module in which to create the profile variables.
  /// @param MAM Module analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);

private:
  std::string CSInstrName;
  bool ProfileSampling;
};

/// Kind of IR-based PGO instrumentation to generate.
enum class PGOInstrumentationType {
  Invalid = 0, ///< Not a valid instrumentation type.
  FDO,         ///< Standard feedback-directed optimization instrumentation.
  CSFDO,       ///< Context-sensitive FDO instrumentation.
  CTXPROF,     ///< Contextual profiling instrumentation.
};
/// The instrumentation (profile-instr-gen) pass for IR based PGO.
class PGOInstrumentationGen
    : public OptionalPassInfoMixin<PGOInstrumentationGen> {
public:
  /// Construct an IR-based PGO instrumentation generation pass.
  /// @param InstrumentationType Kind of PGO instrumentation to emit.
  PGOInstrumentationGen(
      PGOInstrumentationType InstrumentationType = PGOInstrumentationType ::FDO)
      : InstrumentationType(InstrumentationType) {}
  /// Instrument \p M to collect profile data for the configured PGO kind.
  /// @param M Module to instrument.
  /// @param MAM Module analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);

private:
  // If this is a context sensitive instrumentation.
  const PGOInstrumentationType InstrumentationType;
};

/// The profile annotation (profile-instr-use) pass for IR based PGO.
class PGOInstrumentationUse
    : public OptionalPassInfoMixin<PGOInstrumentationUse> {
public:
  /// Construct a PGO profile-use pass that annotates IR from a profile file.
  /// @param Filename Path to the profile data file to apply.
  /// @param RemappingFilename Optional path to a profile remapping file.
  /// @param IsCS Whether the profile is for context-sensitive instrumentation.
  /// @param FS Optional virtual file system used to read the profile files.
  LLVM_ABI
  PGOInstrumentationUse(std::string Filename = "",
                        std::string RemappingFilename = "", bool IsCS = false,
                        IntrusiveRefCntPtr<vfs::FileSystem> FS = nullptr);

  /// Apply profile data to annotate \p M for PGO.
  /// @param M Module to annotate with profile counts.
  /// @param MAM Module analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);

private:
  std::string ProfileFileName;
  std::string ProfileRemappingFileName;
  // If this is a context sensitive instrumentation.
  bool IsCS;
  IntrusiveRefCntPtr<vfs::FileSystem> FS;
};

/// The indirect function call promotion pass.
class PGOIndirectCallPromotion
    : public OptionalPassInfoMixin<PGOIndirectCallPromotion> {
public:
  /// Construct an indirect call promotion pass.
  /// @param IsInLTO Whether this pass runs in an LTO post-link pipeline.
  /// @param SamplePGO Whether promotion is driven by SamplePGO profiles.
  PGOIndirectCallPromotion(bool IsInLTO = false, bool SamplePGO = false)
      : InLTO(IsInLTO), SamplePGO(SamplePGO) {}

  /// Promote hot indirect calls in \p M using profile data.
  /// @param M Module whose indirect calls may be promoted.
  /// @param MAM Module analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);

private:
  bool InLTO;
  bool SamplePGO;
};

/// The profile size based optimization pass for memory intrinsics.
class PGOMemOPSizeOpt : public OptionalPassInfoMixin<PGOMemOPSizeOpt> {
public:
  /// Construct a memory-intrinsic size specialization pass.
  PGOMemOPSizeOpt() = default;

  /// Specialize memory intrinsics in \p F by profiled size.
  /// @param F Function whose memory intrinsics may be specialized.
  /// @param MAM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &MAM);
};

/// Attach branch-weight profile metadata to a terminator or select.
/// @param TI Instruction to annotate with branch weights.
/// @param EdgeCounts Per-successor (or per-arm) edge counts from the profile.
/// @param MaxCount Maximum count among \p EdgeCounts, used to scale weights.
LLVM_ABI void setProfMetadata(Instruction *TI, ArrayRef<uint64_t> EdgeCounts,
                              uint64_t MaxCount);

/// Attach irreducible-loop header weight metadata to an instruction.
/// @param M Module providing the LLVM context for metadata construction.
/// @param TI Instruction (typically a loop header terminator) to annotate.
/// @param Count Profile weight for the irreducible loop header.
LLVM_ABI void setIrrLoopHeaderMetadata(Module *M, Instruction *TI,
                                       uint64_t Count);

} // end namespace llvm

#endif // LLVM_TRANSFORMS_INSTRUMENTATION_PGOINSTRUMENTATION_H
