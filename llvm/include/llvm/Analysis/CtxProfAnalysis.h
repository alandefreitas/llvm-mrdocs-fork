//===- CtxProfAnalysis.h - maintain contextual profile info   -*- C++ ---*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
#ifndef LLVM_ANALYSIS_CTXPROFANALYSIS_H
#define LLVM_ANALYSIS_CTXPROFANALYSIS_H

#include "llvm/ADT/SetVector.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/PassManager.h"
#include "llvm/ProfileData/PGOCtxProfReader.h"
#include "llvm/Support/Compiler.h"
#include <optional>

namespace llvm {

class CtxProfAnalysis;

/// Flat map from callee GUID to an indirect-call target count.
using FlatIndirectTargets = DenseMap<GlobalValue::GUID, uint64_t>;
/// Flat contextual profile of indirect-call targets keyed by caller and site.
using CtxProfFlatIndirectCallProfile =
    DenseMap<GlobalValue::GUID, DenseMap<uint32_t, FlatIndirectTargets>>;

/// The instrumented contextual profile, produced by the CtxProfAnalysis.
class PGOContextualProfile {
  friend class CtxProfAnalysis;
  friend class CtxProfAnalysisPrinterPass;
  struct FunctionInfo {
    uint32_t NextCounterIndex = 0;
    uint32_t NextCallsiteIndex = 0;
    const std::string Name;
    PGOCtxProfContext Index;
    FunctionInfo(StringRef Name) : Name(Name) {}
  };
  PGOCtxProfile Profiles;

  // True if this module is a post-thinlto module containing just functions
  // participating in one or more contextual profiles.
  bool IsInSpecializedModule = false;

  // For the GUIDs in this module, associate metadata about each function which
  // we'll need when we maintain the profiles during IPO transformations.
  std::map<GlobalValue::GUID, FunctionInfo> FuncInfo;

  // This is meant to be constructed from CtxProfAnalysis, which will also set
  // its state piecemeal.
  PGOContextualProfile() = default;

  void initIndex();

public:
  /// Deleted copy constructor; contextual profiles are not copyable.
  /// @param Unused Unused copy source (deleted).
  PGOContextualProfile(const PGOContextualProfile &Unused) = delete;
  /// Move-construct a contextual profile from \p Arg.
  /// @param Arg Contextual profile to move from.
  PGOContextualProfile(PGOContextualProfile &&Arg) = default;

  /// Return the contextual profiles keyed by root GUID.
  /// @return Contextual profiles keyed by root GUID.
  const CtxProfContextualProfiles &contexts() const {
    return Profiles.Contexts;
  }

  /// Return the full contextual profile, including flat and contextual data.
  /// @return Full contextual profile, including flat and contextual data.
  const PGOCtxProfile &profiles() const { return Profiles; }

  /// Return whether this module only contains functions in contextual profiles.
  /// @return True if this module only contains functions in contextual profiles.
  LLVM_ABI bool isInSpecializedModule() const;

  /// Return whether \p F is defined in this module and known to the profile.
  /// @param F Function to look up.
  /// @return True if \p F is defined in this module and known to the profile.
  bool isFunctionKnown(const Function &F) const { return !F.isDeclaration(); }

  /// Return the name recorded for function GUID \p GUID, or empty if unknown.
  /// @param GUID Function GUID to look up.
  /// @return Recorded name for \p GUID, or empty if unknown.
  StringRef getFunctionName(GlobalValue::GUID GUID) const {
    auto It = FuncInfo.find(GUID);
    if (It == FuncInfo.end())
      return "";
    return It->second.Name;
  }

  /// Return the number of counter indices allocated for function \p F.
  /// @param F Function whose counter count is requested.
  /// @return Number of counter indices allocated for \p F.
  uint32_t getNumCounters(const Function &F) const {
    assert(isFunctionKnown(F));
    return FuncInfo.find(F.getGUID())->second.NextCounterIndex;
  }

  /// Return the number of callsite indices allocated for function \p F.
  /// @param F Function whose callsite count is requested.
  /// @return Number of callsite indices allocated for \p F.
  uint32_t getNumCallsites(const Function &F) const {
    assert(isFunctionKnown(F));
    return FuncInfo.find(F.getGUID())->second.NextCallsiteIndex;
  }

  /// Allocate and return the next counter index for function \p F.
  /// @param F Function that needs a new counter index.
  /// @return Newly allocated counter index for \p F.
  uint32_t allocateNextCounterIndex(const Function &F) {
    assert(isFunctionKnown(F));
    return FuncInfo.find(F.getGUID())->second.NextCounterIndex++;
  }

  /// Allocate and return the next callsite index for function \p F.
  /// @param F Function that needs a new callsite index.
  /// @return Newly allocated callsite index for \p F.
  uint32_t allocateNextCallsiteIndex(const Function &F) {
    assert(isFunctionKnown(F));
    return FuncInfo.find(F.getGUID())->second.NextCallsiteIndex++;
  }

  /// Callback type for read-only walks over contextual profile nodes.
  using ConstVisitor = function_ref<void(const PGOCtxProfContext &)>;
  /// Callback type for mutable walks over contextual profile nodes.
  using Visitor = function_ref<void(PGOCtxProfContext &)>;

  /// Apply \p V to the contextual profile node for function \p F.
  /// @param V Visitor invoked on the matching profile context.
  /// @param F Function whose profile context is updated.
  LLVM_ABI void update(Visitor V, const Function &F);
  /// Visit contextual profile nodes, optionally limited to function \p F.
  /// @param V Const visitor invoked on each visited profile context.
  /// @param F Optional function to restrict the walk; null visits all.
  LLVM_ABI void visit(ConstVisitor V, const Function *F = nullptr) const;

  /// Flatten contextual profiles into a GUID-to-counter-vector map.
  /// @return Flattened GUID-to-counter-vector profile.
  LLVM_ABI const CtxProfFlatProfile flatten() const;
  /// Flatten virtual/indirect call targets from the contextual profile.
  /// @return Flattened virtual/indirect call target profile.
  LLVM_ABI const CtxProfFlatIndirectCallProfile flattenVirtCalls() const;

  /// Handle invalidation of this analysis result.
  ///
  /// Checks whether the analysis has been explicitly invalidated. Otherwise,
  /// it's stateless and remains preserved.
  /// @param M Module being invalidated (unused).
  /// @param PA Set of preserved analyses.
  /// @param Inv Invalidator for dependent analyses (unused).
  /// @return True if the analysis should be discarded.
  bool invalidate(Module &M, const PreservedAnalyses &PA,
                  ModuleAnalysisManager::Invalidator &Inv) {
    auto PAC = PA.getChecker<CtxProfAnalysis>();
    return !PAC.preservedWhenStateless();
  }
};

/// Analysis that loads and maintains contextual PGO profiles for a module.
class CtxProfAnalysis : public AnalysisInfoMixin<CtxProfAnalysis> {
  const std::optional<StringRef> Profile;

public:
  /// Analysis key for \c CtxProfAnalysis.
  LLVM_ABI static AnalysisKey Key;
  /// Construct the analysis, optionally loading a profile from \p Profile.
  /// @param Profile Optional path or profile contents; nullopt uses defaults.
  LLVM_ABI explicit CtxProfAnalysis(
      std::optional<StringRef> Profile = std::nullopt);

  /// The analysis result type; an instrumented contextual profile.
  using Result = PGOContextualProfile;

  /// Run the contextual profile analysis on module \p M.
  /// @param M Module to analyze.
  /// @param MAM Module analysis manager providing dependencies.
  /// @return Contextual profile for \p M.
  LLVM_ABI PGOContextualProfile run(Module &M, ModuleAnalysisManager &MAM);

  /// Get the instruction instrumenting a callsite, or nullptr if that cannot be
  /// found.
  /// @param CB Call whose callsite instrumentation is requested.
  /// @return Callsite instrumentation for \p CB, or nullptr if not found.
  LLVM_ABI static InstrProfCallsite *getCallsiteInstrumentation(CallBase &CB);

  /// Get the instruction instrumenting a BB, or nullptr if not present.
  /// @param BB Basic block whose counter instrumentation is requested.
  /// @return BB counter instrumentation for \p BB, or nullptr if not present.
  LLVM_ABI static InstrProfIncrementInst *getBBInstrumentation(BasicBlock &BB);

  /// Get the step instrumentation associated with a `select`
  /// @param SI Select instruction whose step instrumentation is requested.
  /// @return Step instrumentation for \p SI, or nullptr if not present.
  LLVM_ABI static InstrProfIncrementInstStep *
  getSelectInstrumentation(SelectInst &SI);

  // FIXME: refactor to an advisor model, and separate
  /// Collect candidate direct callees for promoting indirect call \p IC.
  /// @param IC Indirect call to consider for promotion.
  /// @param Profile Contextual profile providing target counts.
  /// @param Candidates Output set of (call, direct callee) promotion pairs.
  LLVM_ABI static void collectIndirectCallPromotionList(
      CallBase &IC, Result &Profile,
      SetVector<std::pair<CallBase *, Function *>> &Candidates);
};

/// Printer pass for the \c CtxProfAnalysis results.
class CtxProfAnalysisPrinterPass
    : public RequiredPassInfoMixin<CtxProfAnalysisPrinterPass> {
public:
  /// How much contextual profile detail to print.
  enum class PrintMode {
    /// Print the full contextual profile.
    Everything,
    /// Print the profile as YAML.
    YAML
  };
  /// Construct a printer that writes contextual profile info to \p OS.
  /// @param OS Output stream for the printed profile.
  LLVM_ABI explicit CtxProfAnalysisPrinterPass(raw_ostream &OS);

  /// Print contextual profile results for module \p M.
  /// @param M Module whose contextual profile is printed.
  /// @param MAM Module analysis manager providing CtxProfAnalysis.
  /// @return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);

private:
  raw_ostream &OS;
  const PrintMode Mode;
};

/// Implementation details for \c ProfileAnnotator.
///
/// Propagates counter values to each basic block and to each edge when a basic
/// block has more than one outgoing edge, using an adaptation of
/// PGOUseFunc::populateCounters.
// FIXME(mtrofin): look into factoring the code to share one implementation.
class ProfileAnnotatorImpl;
/// Propagates contextual profile counters onto basic blocks and CFG edges.
class ProfileAnnotator {
  std::unique_ptr<ProfileAnnotatorImpl> PImpl;

public:
  /// Construct an annotator for function \p F from \p RawCounters.
  /// @param F Function whose CFG is annotated.
  /// @param RawCounters Raw counter values for \p F.
  LLVM_ABI ProfileAnnotator(const Function &F, ArrayRef<uint64_t> RawCounters);
  /// Return the propagated counter value for basic block \p BB.
  /// @param BB Basic block whose count is requested.
  /// @return Propagated counter value for \p BB.
  LLVM_ABI uint64_t getBBCount(const BasicBlock &BB) const;

  /// Find the true and false counts for select instruction \p SI.
  ///
  /// Returns false if the select doesn't have instrumentation or if the count
  /// of the parent BB is 0.
  /// @param SI Select instruction whose branch counts are requested.
  /// @param TrueCount Output true-edge count.
  /// @param FalseCount Output false-edge count.
  /// @return True if both counts were successfully computed.
  LLVM_ABI bool getSelectInstrProfile(SelectInst &SI, uint64_t &TrueCount,
                                      uint64_t &FalseCount) const;
  /// Populate \p Profile with outgoing edge weights for \p BB.
  ///
  /// Clears \p Profile and fills it with the edge weights, in the same order as
  /// they need to appear in the MD_prof metadata. Also computes the max of
  /// those weights and returns it in \p MaxCount. Returns false if:
  ///   - the BB has less than 2 successors
  ///   - the counts are 0
  /// @param BB Basic block whose outgoing edges are weighted.
  /// @param Profile Output vector of successor edge weights.
  /// @param MaxCount Output maximum weight among the edges.
  /// @return True if weights were successfully computed.
  LLVM_ABI bool getOutgoingBranchWeights(BasicBlock &BB,
                                         SmallVectorImpl<uint64_t> &Profile,
                                         uint64_t &MaxCount) const;
  /// Destroy this annotator and release owned implementation state.
  LLVM_ABI ~ProfileAnnotator();
};

} // namespace llvm
#endif // LLVM_ANALYSIS_CTXPROFANALYSIS_H
