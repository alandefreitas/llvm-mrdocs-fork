//===- ReplayInlineAdvisor.h - Replay Inline Advisor interface -*- C++ --*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
#ifndef LLVM_ANALYSIS_REPLAYINLINEADVISOR_H
#define LLVM_ANALYSIS_REPLAYINLINEADVISOR_H

#include "llvm/ADT/StringSet.h"
#include "llvm/Analysis/InlineAdvisor.h"

namespace llvm {
class CallBase;
class LLVMContext;
class Module;

/// Controls how call-site locations are formatted as strings.
struct CallSiteFormat {
  /// Granularity of location information included in a call-site string.
  enum class Format : int {
    /// Line number only.
    Line,
    /// Line number and column.
    LineColumn,
    /// Line number and discriminator.
    LineDiscriminator,
    /// Line number, column, and discriminator.
    LineColumnDiscriminator
  };

  /// Whether the selected format includes a column number.
  /// @return True if \p OutputFormat includes column information.
  bool outputColumn() const {
    return OutputFormat == Format::LineColumn ||
           OutputFormat == Format::LineColumnDiscriminator;
  }

  /// Whether the selected format includes a discriminator.
  /// @return True if \p OutputFormat includes discriminator information.
  bool outputDiscriminator() const {
    return OutputFormat == Format::LineDiscriminator ||
           OutputFormat == Format::LineColumnDiscriminator;
  }

  /// Selected call-site location format.
  Format OutputFormat;
};

/// Replay Inliner Setup
struct ReplayInlinerSettings {
  /// Scope over which replay remarks apply.
  enum class Scope : int {
    /// Replay decisions within a single function.
    Function,
    /// Replay decisions across the whole module.
    Module
  };
  /// Policy when a call site has no matching replay remark.
  enum class Fallback : int {
    /// Defer to the original advisor's recommendation.
    Original,
    /// Always recommend inlining.
    AlwaysInline,
    /// Never recommend inlining.
    NeverInline
  };

  /// Path to the file containing replay inline remarks.
  StringRef ReplayFile;
  /// Whether replay applies per function or to the whole module.
  Scope ReplayScope;
  /// Fallback policy when no replay remark matches a call site.
  Fallback ReplayFallback;
  /// Format used to match call-site locations in the replay file.
  CallSiteFormat ReplayFormat;
};

/// Get call site location as a string with the given format
/// @param DLoc Debug location of the call site to format.
/// @param Format Controls which location fields are included in the string.
/// @return Formatted call-site location string according to \p Format.
LLVM_ABI std::string formatCallSiteLocation(DebugLoc DLoc,
                                            const CallSiteFormat &Format);

/// Create a replay inline advisor that consults prior inlining remarks.
/// @param M Module whose call sites are advised.
/// @param FAM Function analysis manager providing analyses for advice.
/// @param Context LLVM context used for diagnostics and remarks.
/// @param OriginalAdvisor Advisor to fall back to when replay has no match.
/// @param ReplaySettings Replay file, scope, fallback, and location format.
/// @param EmitRemarks Whether to emit optimization remarks for replay decisions.
/// @param IC Pipeline context identifying the inline pass using this advisor.
/// @return A ReplayInlineAdvisor, or null if remarks could not be loaded.
LLVM_ABI std::unique_ptr<InlineAdvisor>
getReplayInlineAdvisor(Module &M, FunctionAnalysisManager &FAM,
                       LLVMContext &Context,
                       std::unique_ptr<InlineAdvisor> OriginalAdvisor,
                       const ReplayInlinerSettings &ReplaySettings,
                       bool EmitRemarks, InlineContext IC);

/// Replay inline advisor that uses optimization remarks from inlining of
/// previous build to guide current inlining. This is useful for inliner tuning.
class LLVM_ABI ReplayInlineAdvisor : public InlineAdvisor {
public:
  /// Construct a replay advisor from prior inlining remarks.
  /// @param M Module whose call sites are advised.
  /// @param FAM Function analysis manager providing analyses for advice.
  /// @param Context LLVM context used for diagnostics and remarks.
  /// @param OriginalAdvisor Advisor to fall back to when replay has no match.
  /// @param ReplaySettings Replay file, scope, fallback, and location format.
  /// @param EmitRemarks Whether to emit optimization remarks for replay decisions.
  /// @param IC Pipeline context identifying the inline pass using this advisor.
  ReplayInlineAdvisor(Module &M, FunctionAnalysisManager &FAM,
                      LLVMContext &Context,
                      std::unique_ptr<InlineAdvisor> OriginalAdvisor,
                      const ReplayInlinerSettings &ReplaySettings,
                      bool EmitRemarks, InlineContext IC);
  /// Compute advice for call site \p CB using loaded replay remarks.
  /// @param CB Direct call site to advise on.
  /// @return Advice recommending whether to inline \p CB.
  std::unique_ptr<InlineAdvice> getAdviceImpl(CallBase &CB) override;
  /// Whether replay remarks were successfully loaded from the replay file.
  /// @return True if remarks are available to guide inlining.
  bool areReplayRemarksLoaded() const { return HasReplayRemarks; }

private:
  bool hasInlineAdvice(Function &F) const {
    return (ReplaySettings.ReplayScope ==
            ReplayInlinerSettings::Scope::Module) ||
           CallersToReplay.contains(F.getName());
  }
  std::unique_ptr<InlineAdvisor> OriginalAdvisor;
  bool HasReplayRemarks = false;
  const ReplayInlinerSettings ReplaySettings;
  bool EmitRemarks = false;

  StringMap<bool> InlineSitesFromRemarks;
  StringSet<> CallersToReplay;
};
} // namespace llvm
#endif // LLVM_ANALYSIS_REPLAYINLINEADVISOR_H
