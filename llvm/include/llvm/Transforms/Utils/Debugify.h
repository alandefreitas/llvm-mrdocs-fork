//===- Debugify.h - Check debug info preservation in optimizations --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file Interface to the `debugify` synthetic/original debug info testing
/// utility.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_DEBUGIFY_H
#define LLVM_TRANSFORMS_UTILS_DEBUGIFY_H

#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Bitcode/BitcodeWriterPass.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassInstrumentation.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Pass.h"
#include "llvm/Support/Compiler.h"

using DebugFnMap =
    llvm::MapVector<const llvm::Function *, const llvm::DISubprogram *>;
using DebugInstMap = llvm::MapVector<const llvm::Instruction *, bool>;
using DebugVarMap = llvm::MapVector<const llvm::DILocalVariable *, unsigned>;
using WeakInstValueMap =
    llvm::MapVector<const llvm::Instruction *, llvm::WeakVH>;

/// Used to track the Debug Info Metadata information.
struct DebugInfoPerPass {
  // This maps a function name to its associated DISubprogram.
  DebugFnMap DIFunctions;
  // This tracks value (instruction) deletion. If an instruction gets deleted,
  // WeakVH nulls itself.
  WeakInstValueMap InstToDelete;
  // Maps variable into dbg users (#dbg values/declares for this variable).
  DebugVarMap DIVariables;
};

namespace llvm {
class DIBuilder;

/// Add synthesized debug information to a module.
///
/// \param M The module to add debug information to.
/// \param Functions A range of functions to add debug information to.
/// \param Banner A prefix string to add to debug/error messages.
/// \param ApplyToMF A call back that will add debug information to the
///                  MachineFunction for a Function. If nullptr, then the
///                  MachineFunction (if any) will not be modified.
/// \return True if synthetic debug info was applied; false if skipped because
///         the module already had debug info.
LLVM_ABI bool
applyDebugifyMetadata(Module &M, iterator_range<Module::iterator> Functions,
                      StringRef Banner,
                      std::function<bool(DIBuilder &, Function &)> ApplyToMF);

/// Strip out all of the metadata and debug info inserted by debugify.
///
/// If no llvm.debugify module-level named metadata is present, this is a
/// no-op.
///
/// \param M The module to strip debugify metadata from.
/// \return True if any change was made.
LLVM_ABI bool stripDebugifyMetadata(Module &M);

/// Collect original debug information before a pass.
///
/// \param M The module to collect debug information from.
/// \param Functions A range of functions to collect debug information from.
/// \param DebugInfoBeforePass DI metadata before a pass.
/// \param Banner A prefix string to add to debug/error messages.
/// \param NameOfWrappedPass A name of a pass to add to debug/error messages.
/// \return True if original debug info was collected; false if the module has
///         no debug info.
LLVM_ABI bool
collectDebugInfoMetadata(Module &M, iterator_range<Module::iterator> Functions,
                         DebugInfoPerPass &DebugInfoBeforePass,
                         StringRef Banner, StringRef NameOfWrappedPass);

/// Check original debug information after a pass.
///
/// \param M The module to collect debug information from.
/// \param Functions A range of functions to collect debug information from.
/// \param DebugInfoBeforePass DI metadata before a pass.
/// \param Banner A prefix string to add to debug/error messages.
/// \param NameOfWrappedPass A name of a pass to add to debug/error messages.
/// \param OrigDIVerifyBugsReportFilePath Path to write original-DI
///        verification bug reports to, or empty to skip writing a report.
/// \return True if original debug info was preserved; false if bugs were found
///         or the module has no debug info.
LLVM_ABI bool checkDebugInfoMetadata(Module &M,
                                     iterator_range<Module::iterator> Functions,
                                     DebugInfoPerPass &DebugInfoBeforePass,
                                     StringRef Banner,
                                     StringRef NameOfWrappedPass,
                                     StringRef OrigDIVerifyBugsReportFilePath);
} // namespace llvm

/// Used to check whether we track synthetic or original debug info.
enum class DebugifyMode { NoDebugify, SyntheticDebugInfo, OriginalDebugInfo };

using DebugifyApplyToMFCallback = llvm::function_ref<bool(
    llvm::DIBuilder &, llvm::Function &, llvm::ModuleAnalysisManager &)>;

LLVM_ABI llvm::ModulePass *createDebugifyModulePass(
    enum DebugifyMode Mode = DebugifyMode::SyntheticDebugInfo,
    llvm::StringRef NameOfWrappedPass = "",
    DebugInfoPerPass *DebugInfoBeforePass = nullptr);
LLVM_ABI llvm::FunctionPass *createDebugifyFunctionPass(
    enum DebugifyMode Mode = DebugifyMode::SyntheticDebugInfo,
    llvm::StringRef NameOfWrappedPass = "",
    DebugInfoPerPass *DebugInfoBeforePass = nullptr);

class NewPMDebugifyPass
    : public llvm::OptionalPassInfoMixin<NewPMDebugifyPass> {
  DebugifyApplyToMFCallback ApplyToMF = nullptr;
  llvm::StringRef NameOfWrappedPass;
  DebugInfoPerPass *DebugInfoBeforePass = nullptr;
  enum DebugifyMode Mode = DebugifyMode::NoDebugify;
public:
  NewPMDebugifyPass(
      enum DebugifyMode Mode = DebugifyMode::SyntheticDebugInfo,
      llvm::StringRef NameOfWrappedPass = "",
      DebugInfoPerPass *DebugInfoBeforePass = nullptr)
      : NameOfWrappedPass(NameOfWrappedPass),
        DebugInfoBeforePass(DebugInfoBeforePass), Mode(Mode) {}
  NewPMDebugifyPass(DebugifyApplyToMFCallback ApplyToMF)
      : ApplyToMF(ApplyToMF), Mode(DebugifyMode::SyntheticDebugInfo) {}

  LLVM_ABI llvm::PreservedAnalyses run(llvm::Module &M,
                                       llvm::ModuleAnalysisManager &AM);
};

/// Track how much `debugify` information (in the `synthetic` mode only)
/// has been lost.
struct DebugifyStatistics {
  /// Number of missing dbg.values.
  unsigned NumDbgValuesMissing = 0;

  /// Number of dbg.values expected.
  unsigned NumDbgValuesExpected = 0;

  /// Number of instructions with empty debug locations.
  unsigned NumDbgLocsMissing = 0;

  /// Number of instructions expected to have debug locations.
  unsigned NumDbgLocsExpected = 0;

  /// Get the ratio of missing/expected dbg.values.
  float getMissingValueRatio() const {
    return float(NumDbgValuesMissing) / float(NumDbgLocsExpected);
  }

  /// Get the ratio of missing/expected instructions with locations.
  float getEmptyLocationRatio() const {
    return float(NumDbgLocsMissing) / float(NumDbgLocsExpected);
  }
};

/// Map pass names to a per-pass DebugifyStatistics instance.
using DebugifyStatsMap = llvm::MapVector<llvm::StringRef, DebugifyStatistics>;

LLVM_ABI llvm::ModulePass *createCheckDebugifyModulePass(
    bool Strip = false, llvm::StringRef NameOfWrappedPass = "",
    DebugifyStatsMap *StatsMap = nullptr,
    enum DebugifyMode Mode = DebugifyMode::SyntheticDebugInfo,
    DebugInfoPerPass *DebugInfoBeforePass = nullptr,
    llvm::StringRef OrigDIVerifyBugsReportFilePath = "");

LLVM_ABI llvm::FunctionPass *createCheckDebugifyFunctionPass(
    bool Strip = false, llvm::StringRef NameOfWrappedPass = "",
    DebugifyStatsMap *StatsMap = nullptr,
    enum DebugifyMode Mode = DebugifyMode::SyntheticDebugInfo,
    DebugInfoPerPass *DebugInfoBeforePass = nullptr,
    llvm::StringRef OrigDIVerifyBugsReportFilePath = "");

class NewPMCheckDebugifyPass
    : public llvm::OptionalPassInfoMixin<NewPMCheckDebugifyPass> {
  llvm::StringRef NameOfWrappedPass;
  llvm::StringRef OrigDIVerifyBugsReportFilePath;
  DebugifyStatsMap *StatsMap;
  DebugInfoPerPass *DebugInfoBeforePass;
  enum DebugifyMode Mode;
  bool Strip;
public:
  NewPMCheckDebugifyPass(
      bool Strip = false, llvm::StringRef NameOfWrappedPass = "",
      DebugifyStatsMap *StatsMap = nullptr,
      enum DebugifyMode Mode = DebugifyMode::SyntheticDebugInfo,
      DebugInfoPerPass *DebugInfoBeforePass = nullptr,
      llvm::StringRef OrigDIVerifyBugsReportFilePath = "")
      : NameOfWrappedPass(NameOfWrappedPass),
        OrigDIVerifyBugsReportFilePath(OrigDIVerifyBugsReportFilePath),
        StatsMap(StatsMap), DebugInfoBeforePass(DebugInfoBeforePass), Mode(Mode),
        Strip(Strip) {}

  LLVM_ABI llvm::PreservedAnalyses run(llvm::Module &M,
                                       llvm::ModuleAnalysisManager &AM);
};

namespace llvm {
/// Write debugify statistics from \p Map to the file at \p Path.
///
/// \param Path Output file path for the exported statistics.
/// \param Map Per-pass debugify statistics to export.
LLVM_ABI void exportDebugifyStats(StringRef Path, const DebugifyStatsMap &Map);

/// Registers NewPM callbacks that run debugify around each pass.
class DebugifyEachInstrumentation {
  llvm::StringRef OrigDIVerifyBugsReportFilePath = "";
  DebugInfoPerPass *DebugInfoBeforePass = nullptr;
  enum DebugifyMode Mode = DebugifyMode::NoDebugify;
  DebugifyStatsMap *DIStatsMap = nullptr;

public:
  /// Register before/after callbacks that apply debugify around each pass.
  ///
  /// \param PIC Pass instrumentation callbacks to register with.
  /// \param MAM Module analysis manager used by the registered callbacks.
  LLVM_ABI void registerCallbacks(PassInstrumentationCallbacks &PIC,
                                  ModuleAnalysisManager &MAM);

  /// Set the map used to record synthetic debugify statistics.
  ///
  /// Used within DebugifyMode::SyntheticDebugInfo mode.
  ///
  /// \param StatMap Statistics map to update as passes run.
  void setDIStatsMap(DebugifyStatsMap &StatMap) { DIStatsMap = &StatMap; }

  /// Return the map of synthetic debugify statistics.
  ///
  /// \return Const reference to the synthetic debugify statistics map.
  const DebugifyStatsMap &getDebugifyStatsMap() const { return *DIStatsMap; }

  /// Set the per-pass original debug-info snapshot used for verification.
  ///
  /// Used within DebugifyMode::OriginalDebugInfo mode.
  ///
  /// \param PerPassMap Snapshot of debug info collected before each pass.
  void setDebugInfoBeforePass(DebugInfoPerPass &PerPassMap) {
    DebugInfoBeforePass = &PerPassMap;
  }

  /// Return the per-pass original debug-info snapshot.
  ///
  /// \return Reference to the debug-info snapshot collected before each pass.
  DebugInfoPerPass &getDebugInfoPerPass() { return *DebugInfoBeforePass; }

  /// Set the path used to write original-DI verification bug reports.
  ///
  /// \param BugsReportFilePath Output path for verification bug reports.
  void setOrigDIVerifyBugsReportFilePath(StringRef BugsReportFilePath) {
    OrigDIVerifyBugsReportFilePath = BugsReportFilePath;
  }

  /// Return the path used to write original-DI verification bug reports.
  ///
  /// \return Path for original-DI verification bug reports, or empty if unset.
  StringRef getOrigDIVerifyBugsReportFilePath() const {
    return OrigDIVerifyBugsReportFilePath;
  }

  /// Set whether debugify tracks synthetic or original debug info.
  ///
  /// \param M Debugify operating mode to use.
  void setDebugifyMode(enum DebugifyMode M) { Mode = M; }

  /// Return true if debugify is in synthetic debug-info mode.
  ///
  /// \return True when Mode is DebugifyMode::SyntheticDebugInfo.
  bool isSyntheticDebugInfo() const {
    return Mode == DebugifyMode::SyntheticDebugInfo;
  }

  /// Return true if debugify is in original debug-info mode.
  ///
  /// \return True when Mode is DebugifyMode::OriginalDebugInfo.
  bool isOriginalDebugInfoMode() const {
    return Mode == DebugifyMode::OriginalDebugInfo;
  }
};

/// Legacy pass manager that wraps each pass with debugify when enabled.
///
/// NOTE: We support legacy custom pass manager only.
/// TODO: Add New PM support for custom pass manager.
class DebugifyCustomPassManager : public legacy::PassManager {
  StringRef OrigDIVerifyBugsReportFilePath;
  DebugifyStatsMap *DIStatsMap = nullptr;
  DebugInfoPerPass *DebugInfoBeforePass = nullptr;
  enum DebugifyMode Mode = DebugifyMode::NoDebugify;

public:
  /// Base legacy pass manager type.
  using super = legacy::PassManager;

  /// Add \p P, wrapping it with debugify passes when requested.
  ///
  /// \param P Pass to schedule, optionally wrapped with (-check)-debugify.
  void add(Pass *P) override {
    // Wrap each pass with (-check)-debugify passes if requested, making
    // exceptions for passes which shouldn't see -debugify instrumentation.
    bool WrapWithDebugify = Mode != DebugifyMode::NoDebugify &&
                            !P->getAsImmutablePass() && !isIRPrintingPass(P) &&
                            !isBitcodeWriterPass(P);
    if (!WrapWithDebugify) {
      super::add(P);
      return;
    }

    // Either apply -debugify/-check-debugify before/after each pass and collect
    // debug info loss statistics, or collect and check original debug info in
    // the optimizations.
    PassKind Kind = P->getPassKind();
    StringRef Name = P->getPassName();

    // TODO: Implement Debugify for LoopPass.
    switch (Kind) {
    case PT_Function:
      super::add(createDebugifyFunctionPass(Mode, Name, DebugInfoBeforePass));
      super::add(P);
      super::add(createCheckDebugifyFunctionPass(
          isSyntheticDebugInfo(), Name, DIStatsMap, Mode, DebugInfoBeforePass,
          OrigDIVerifyBugsReportFilePath));
      break;
    case PT_Module:
      super::add(createDebugifyModulePass(Mode, Name, DebugInfoBeforePass));
      super::add(P);
      super::add(createCheckDebugifyModulePass(
          isSyntheticDebugInfo(), Name, DIStatsMap, Mode, DebugInfoBeforePass,
          OrigDIVerifyBugsReportFilePath));
      break;
    default:
      super::add(P);
      break;
    }
  }

  /// Set the map used to record synthetic debugify statistics.
  ///
  /// Used within DebugifyMode::SyntheticDebugInfo mode.
  ///
  /// \param StatMap Statistics map to update as passes run.
  void setDIStatsMap(DebugifyStatsMap &StatMap) { DIStatsMap = &StatMap; }

  /// Set the per-pass original debug-info snapshot used for verification.
  ///
  /// Used within DebugifyMode::OriginalDebugInfo mode.
  ///
  /// \param PerPassDI Snapshot of debug info collected before each pass.
  void setDebugInfoBeforePass(DebugInfoPerPass &PerPassDI) {
    DebugInfoBeforePass = &PerPassDI;
  }

  /// Set the path used to write original-DI verification bug reports.
  ///
  /// \param BugsReportFilePath Output path for verification bug reports.
  void setOrigDIVerifyBugsReportFilePath(StringRef BugsReportFilePath) {
    OrigDIVerifyBugsReportFilePath = BugsReportFilePath;
  }

  /// Return the path used to write original-DI verification bug reports.
  ///
  /// \return Path for original-DI verification bug reports, or empty if unset.
  StringRef getOrigDIVerifyBugsReportFilePath() const {
    return OrigDIVerifyBugsReportFilePath;
  }

  /// Set whether debugify tracks synthetic or original debug info.
  ///
  /// \param M Debugify operating mode to use.
  void setDebugifyMode(enum DebugifyMode M) { Mode = M; }

  /// Return true if debugify is in synthetic debug-info mode.
  ///
  /// \return True when Mode is DebugifyMode::SyntheticDebugInfo.
  bool isSyntheticDebugInfo() const {
    return Mode == DebugifyMode::SyntheticDebugInfo;
  }

  /// Return true if debugify is in original debug-info mode.
  ///
  /// \return True when Mode is DebugifyMode::OriginalDebugInfo.
  bool isOriginalDebugInfoMode() const {
    return Mode == DebugifyMode::OriginalDebugInfo;
  }

  /// Return the map of synthetic debugify statistics.
  ///
  /// \return Const reference to the synthetic debugify statistics map.
  const DebugifyStatsMap &getDebugifyStatsMap() const { return *DIStatsMap; }

  /// Return the per-pass original debug-info snapshot.
  ///
  /// \return Reference to the debug-info snapshot collected before each pass.
  DebugInfoPerPass &getDebugInfoPerPass() { return *DebugInfoBeforePass; }
};
} // namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_DEBUGIFY_H
