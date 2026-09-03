#ifndef LLVM_ANALYSIS_STATICDATAPROFILEINFO_H
#define LLVM_ANALYSIS_STATICDATAPROFILEINFO_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/IR/Analysis.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/Compiler.h"
#include <memory>

namespace llvm {

namespace memprof {
/// Eligibility of a global variable for section-prefix annotation.
///
/// Other than AnnotationOK, each enum value indicates a specific reason for
/// ineligibility.
enum class AnnotationKind : uint8_t {
  /// The global is eligible for section-prefix annotation.
  AnnotationOK,
  /// The global is a declaration for the linker and must not be annotated.
  DeclForLinker,
  /// The global already has an explicit section and must not be annotated.
  ExplicitSection,
  /// The global uses a reserved name (e.g. llvm.*) and must not be annotated.
  ReservedName,
};
/// Returns the annotation kind of the global variable \p GV.
/// @param GV Global variable whose annotation eligibility is queried.
/// @return Annotation eligibility of \p GV.
LLVM_ABI AnnotationKind getAnnotationKind(const GlobalVariable &GV);

/// Returns true if the annotation kind of the global variable \p GV is
/// AnnotationOK.
/// @param GV Global variable whose annotation eligibility is queried.
/// @return True if \p GV is eligible for section-prefix annotation.
LLVM_ABI bool IsAnnotationOK(const GlobalVariable &GV);
} // namespace memprof

/// A class that holds the constants that represent static data and their
/// profile information and provides methods to operate on them.
class StaticDataProfileInfo {
public:
  /// Maps tracked constants to their accumulated profile counts.
  ///
  /// A constant is tracked only if the following conditions are met.
  ///   1) It has local (i.e., private or internal) linkage.
  ///   2) Its data kind is one of {.rodata, .data, .bss, .data.rel.ro}.
  ///   3) It's eligible for section prefix annotation. See `AnnotationKind`
  ///      above for ineligible reasons.
  DenseMap<const Constant *, uint64_t> ConstantProfileCounts;

  /// Keeps track of the constants that are seen at least once without profile
  /// counts.
  DenseSet<const Constant *> ConstantWithoutCounts;

  /// If \p C has a count, return it. Otherwise, return std::nullopt.
  /// @param C Constant whose profile count is requested.
  /// @return Profile count for \p C, or std::nullopt if none is recorded.
  LLVM_ABI std::optional<uint64_t>
  getConstantProfileCount(const Constant *C) const;

  /// Use signed enums for enum value comparison, and make 'LukewarmOrUnknown'
  /// as 0 so any accidentally uninitialized value will default to unknown.
  enum class StaticDataHotness : int8_t {
    /// Profile data indicates the constant is cold.
    Cold = -1,
    /// Lukewarm or unknown hotness; value 0 so uninitialized defaults are unknown.
    LukewarmOrUnknown = 0,
    Hot = 1,
  };

  /// Return the hotness of the constant \p C based on its profile count \p
  /// Count.
  /// @param C Constant whose hotness is classified.
  /// @param PSI Profile summary used to interpret \p Count.
  /// @param Count Profile count associated with \p C.
  /// @return Hotness of \p C classified from \p Count using \p PSI.
  LLVM_ABI StaticDataHotness getConstantHotnessUsingProfileCount(
      const Constant *C, const ProfileSummaryInfo *PSI, uint64_t Count) const;

  /// Return the hotness based on section prefix \p SectionPrefix.
  /// @param SectionPrefix Optional existing section prefix to classify.
  /// @return Hotness classified from \p SectionPrefix via data-access profile.
  LLVM_ABI StaticDataHotness getSectionHotnessUsingDataAccessProfile(
      std::optional<StringRef> SectionPrefix) const;

  /// Return the string representation of the hotness enum \p Hotness.
  /// @param Hotness Hotness value to convert to a section-prefix string.
  /// @return Section-prefix string corresponding to \p Hotness.
  LLVM_ABI StringRef hotnessToStr(StaticDataHotness Hotness) const;

  /// When true, combine data-access profile hotness with PGO counts for section
  /// prefixes.
  bool EnableDataAccessProf = false;

public:
  /// Construct StaticDataProfileInfo with data-access profile combining \p
  /// EnableDataAccessProf.
  /// @param EnableDataAccessProf Whether to combine data-access profile
  /// hotness with PGO counts.
  StaticDataProfileInfo(bool EnableDataAccessProf)
      : EnableDataAccessProf(EnableDataAccessProf) {}

  /// Add or clear the profile count for constant \p C.
  ///
  /// If \p Count is not nullopt, add it to the profile count of the constant \p
  /// C in a saturating way, and clamp the count to \p getInstrMaxCountValue if
  /// the result exceeds it. Otherwise, mark the constant as having no profile
  /// count.
  /// @param C Constant whose profile count is updated.
  /// @param Count Profile count to add, or nullopt to mark \p C as uncounted.
  LLVM_ABI void addConstantProfileCount(const Constant *C,
                                        std::optional<uint64_t> Count);

  /// Return a section prefix for constant \p C based on profile data.
  ///
  /// If \p C is a global variable, the section prefix is the bigger one
  /// between its existing section prefix and its use profile count. Otherwise,
  /// the section prefix is based on its use profile count.
  /// @param C Constant whose section prefix is requested.
  /// @param PSI Profile summary used to classify profile counts.
  /// @return Section prefix string for \p C derived from profile data.
  LLVM_ABI StringRef getConstantSectionPrefix(
      const Constant *C, const ProfileSummaryInfo *PSI) const;
};

/// This wraps the StaticDataProfileInfo object as an immutable pass, for a
/// backend pass to operate on.
class LLVM_ABI StaticDataProfileInfoWrapperPass : public ImmutablePass {
public:
  /// Pass identification, replacement for typeid.
  static char ID;
  /// Construct the legacy StaticDataProfileInfo wrapper pass.
  StaticDataProfileInfoWrapperPass();
  /// Build StaticDataProfileInfo for module \p M.
  /// @param M Module to analyze.
  /// @return False; this analysis does not modify the module.
  bool doInitialization(Module &M) override;
  /// Release the cached StaticDataProfileInfo after the module is processed.
  /// @param M Module whose analysis state is being finalized.
  /// @return False; this pass does not modify the module.
  bool doFinalization(Module &M) override;

  /// Return the cached StaticDataProfileInfo.
  /// @return Cached StaticDataProfileInfo for the module.
  StaticDataProfileInfo &getStaticDataProfileInfo() { return *Info; }
  /// Return the cached StaticDataProfileInfo.
  /// @return Cached StaticDataProfileInfo for the module.
  const StaticDataProfileInfo &getStaticDataProfileInfo() const {
    return *Info;
  }

  /// This pass provides StaticDataProfileInfo for reads/writes but does not
  /// modify \p M or other analysis. All analysis are preserved.
  /// @param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }

private:
  std::unique_ptr<StaticDataProfileInfo> Info;
};

/// NewPM analysis that computes and caches \c StaticDataProfileInfo for a
/// module.
class LLVM_ABI StaticDataProfileInfoAnalysis
    : public AnalysisInfoMixin<StaticDataProfileInfoAnalysis> {
public:
  /// Analysis key used to identify this analysis in the pass manager.
  static AnalysisKey Key;

  /// Cached StaticDataProfileInfo result for a module.
  class Result {
    std::unique_ptr<StaticDataProfileInfo> HeldInfo;
    Result(std::unique_ptr<StaticDataProfileInfo> &&Info)
        : HeldInfo(std::move(Info)) {}
    friend class StaticDataProfileInfoAnalysis;

  public:
    /// Return the cached StaticDataProfileInfo.
    /// @return Cached StaticDataProfileInfo for the module.
    StaticDataProfileInfo &getStaticDataProfileInfo() {
      return *HeldInfo;
    }

    /// Handle invalidation of this analysis result.
    ///
    /// Always returns false because this analysis is immutable after it is
    /// computed for the module.
    /// @param M Module being invalidated (unused).
    /// @param PA Set of preserved analyses (unused).
    /// @param Inv Invalidator for dependent analyses (unused).
    /// @return False; this analysis is never invalidated.
    bool invalidate(Module &M, const PreservedAnalyses &PA,
                    ModuleAnalysisManager::Invalidator &Inv) {
      return false;
    }
  };

  /// Compute StaticDataProfileInfo for module \p M.
  /// @param M Module to analyze.
  /// @param AM Module analysis manager providing dependencies.
  /// @return Cached StaticDataProfileInfo for \p M.
  Result run(Module &M, ModuleAnalysisManager &AM);
};

} // namespace llvm

#endif // LLVM_ANALYSIS_STATICDATAPROFILEINFO_H
