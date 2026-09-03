//===- RegAllocPriorityAdvisor.h - live ranges priority advisor -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_REGALLOCPRIORITYADVISOR_H
#define LLVM_CODEGEN_REGALLOCPRIORITYADVISOR_H

#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/RegAllocEvictionAdvisor.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

namespace llvm {

class MachineFunction;
class VirtRegMap;
class RAGreedy;

/// Interface to the priority advisor, which is responsible for prioritizing
/// live ranges.
class RegAllocPriorityAdvisor {
public:
  /// Deleted copy constructor; priority advisors are not copyable.
  ///
  /// \param Other Unused; copy construction is deleted.
  RegAllocPriorityAdvisor(const RegAllocPriorityAdvisor &Other) = delete;
  /// Deleted move constructor; priority advisors are not movable.
  ///
  /// \param Other Unused; move construction is deleted.
  RegAllocPriorityAdvisor(RegAllocPriorityAdvisor &&Other) = delete;
  /// Destroy this priority advisor.
  virtual ~RegAllocPriorityAdvisor() = default;

  /// Find the priority value for a live range. A float value is used since ML
  /// prefers it.
  ///
  /// \param LI Live interval whose allocation priority is requested.
  /// \return Priority value for the live interval.
  virtual unsigned getPriority(const LiveInterval &LI) const = 0;

  /// Construct an advisor for \p MF backed by greedy allocator \p RA.
  ///
  /// \param MF Machine function being allocated.
  /// \param RA Greedy register allocator providing analysis state.
  /// \param Indexes Slot indexes used when computing priorities.
  LLVM_ABI RegAllocPriorityAdvisor(const MachineFunction &MF,
                                   const RAGreedy &RA,
                                   SlotIndexes *const Indexes);

protected:
  /// Greedy register allocator that owns related analyses.
  const RAGreedy &RA;
  /// Live interval analysis for the current function.
  LiveIntervals *const LIS;
  /// Mapping from virtual to physical registers.
  VirtRegMap *const VRM;
  /// Machine register info for the current function.
  MachineRegisterInfo *const MRI;
  /// Target register info for the current subtarget.
  const TargetRegisterInfo *const TRI;
  /// Cached register class information for the current function.
  const RegisterClassInfo &RegClassInfo;
  /// Slot indexes for the current function.
  SlotIndexes *const Indexes;
  /// Whether register-class priority should outweigh globalness.
  const bool RegClassPriorityTrumpsGlobalness;
  /// Whether local live ranges should be assigned in reverse order.
  const bool ReverseLocalAssignment;
};

/// Default heuristic priority advisor used by greedy register allocation.
class LLVM_ABI DefaultPriorityAdvisor : public RegAllocPriorityAdvisor {
public:
  /// Construct the default advisor for \p MF and allocator \p RA.
  ///
  /// \param MF Machine function being allocated.
  /// \param RA Greedy register allocator providing analysis state.
  /// \param Indexes Slot indexes used when computing priorities.
  DefaultPriorityAdvisor(const MachineFunction &MF, const RAGreedy &RA,
                         SlotIndexes *const Indexes)
      : RegAllocPriorityAdvisor(MF, RA, Indexes) {}

private:
  unsigned getPriority(const LiveInterval &LI) const override;
};

/// Stupid priority advisor which just enqueues in virtual register number
/// order, for debug purposes only.
class LLVM_ABI DummyPriorityAdvisor : public RegAllocPriorityAdvisor {
public:
  /// Construct the dummy advisor for \p MF and allocator \p RA.
  ///
  /// \param MF Machine function being allocated.
  /// \param RA Greedy register allocator providing analysis state.
  /// \param Indexes Slot indexes used when computing priorities.
  DummyPriorityAdvisor(const MachineFunction &MF, const RAGreedy &RA,
                       SlotIndexes *const Indexes)
      : RegAllocPriorityAdvisor(MF, RA, Indexes) {}

private:
  unsigned getPriority(const LiveInterval &LI) const override;
};

/// Common provider for getting the priority advisor and logging rewards.
///
/// Legacy analysis forwards all calls to this provider. New analysis serves the
/// provider as the analysis result. Expensive setup is done in the constructor,
/// so that the advisor can be created quickly for every machine function.
/// TODO: Remove once legacy PM support is dropped.
class RegAllocPriorityAdvisorProvider {
public:
  /// Which priority advisor implementation to construct.
  enum class AdvisorMode : int {
    /// Heuristic default priority advisor.
    Default,
    /// Release-mode ML priority advisor.
    Release,
    /// Development-mode ML priority advisor with training logs.
    Development,
    /// Debug priority advisor ordered by virtual register number.
    Dummy
  };

  /// Construct a provider for advisor mode \p Mode.
  ///
  /// \param Mode Advisor implementation mode to use.
  RegAllocPriorityAdvisorProvider(AdvisorMode Mode) : Mode(Mode) {}

  /// Destroy this provider.
  virtual ~RegAllocPriorityAdvisorProvider() = default;

  /// Log a reward for \p MF when the advisor mode requires it.
  ///
  /// \param MF Machine function whose allocation outcome is rewarded.
  /// \param GetReward Callback that computes the reward value when needed.
  virtual void logRewardIfNeeded(const MachineFunction &MF,
                                 function_ref<float()> GetReward) {};

  /// Create a priority advisor for \p MF and allocator \p RA.
  ///
  /// \param MF Machine function being allocated.
  /// \param RA Greedy register allocator requesting advice.
  /// \param SI Slot indexes used when computing priorities.
  /// \return Priority advisor for the given machine function and allocator.
  virtual std::unique_ptr<RegAllocPriorityAdvisor>
  getAdvisor(const MachineFunction &MF, const RAGreedy &RA,
             SlotIndexes &SI) = 0;

  /// Return the advisor mode this provider was constructed with.
  ///
  /// \return Advisor mode this provider was constructed with.
  AdvisorMode getAdvisorMode() const { return Mode; }

private:
  const AdvisorMode Mode;
};

/// MachineFunction analysis that lazily provides the priority advisor.
class RegAllocPriorityAdvisorAnalysis
    : public AnalysisInfoMixin<RegAllocPriorityAdvisorAnalysis> {
  static AnalysisKey Key;
  friend AnalysisInfoMixin<RegAllocPriorityAdvisorAnalysis>;

public:
  /// Cached analysis result holding the shared advisor provider.
  struct Result {
    // Owned by this analysis.
    /// Provider owned by the analysis and shared across functions.
    RegAllocPriorityAdvisorProvider *Provider;

    /// Return true when the provider result must be recomputed.
    ///
    /// \param MF Machine function for which invalidation is queried.
    /// \param PA Set of analyses preserved by the last transformation.
    /// \param Inv Invalidator for other machine-function analyses.
    /// \return True if the provider result must be recomputed.
    bool invalidate(MachineFunction &MF, const PreservedAnalyses &PA,
                    MachineFunctionAnalysisManager::Invalidator &Inv) {
      auto PAC = PA.getChecker<RegAllocPriorityAdvisorAnalysis>();
      return !PAC.preservedWhenStateless() ||
             Inv.invalidate<SlotIndexesAnalysis>(MF, PA);
    }
  };

  /// Run the analysis on \p MF and return the cached provider result.
  ///
  /// \param MF Machine function being analyzed.
  /// \param MFAM Manager used to query dependent analyses.
  /// \return Cached result holding the shared advisor provider.
  LLVM_ABI Result run(MachineFunction &MF,
                      MachineFunctionAnalysisManager &MFAM);

private:
  void initializeProvider(LLVMContext &Ctx);
  void initializeMLProvider(RegAllocPriorityAdvisorProvider::AdvisorMode Mode,
                            LLVMContext &Ctx);
  std::unique_ptr<RegAllocPriorityAdvisorProvider> Provider;
};

/// Immutable analysis that provides the priority advisor.
class LLVM_ABI RegAllocPriorityAdvisorAnalysisLegacy : public ImmutablePass {
public:
  /// Which priority advisor implementation this analysis provides.
  using AdvisorMode = RegAllocPriorityAdvisorProvider::AdvisorMode;
  /// Construct the analysis for advisor mode \p Mode.
  ///
  /// \param Mode Advisor implementation mode to provide.
  RegAllocPriorityAdvisorAnalysisLegacy(AdvisorMode Mode)
      : ImmutablePass(ID), Mode(Mode) {};
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Get an advisor for the given context (i.e. machine function, etc)
  ///
  /// \return Provider that constructs priority advisors.
  RegAllocPriorityAdvisorProvider &getProvider() { return *Provider; }
  /// Return the advisor mode this analysis was constructed with.
  ///
  /// \return Advisor mode this analysis was constructed with.
  AdvisorMode getAdvisorMode() const { return Mode; }
  /// Log a reward for \p MF when the advisor mode requires it.
  ///
  /// \param MF Machine function whose allocation outcome is rewarded.
  /// \param GetReward Callback that computes the reward value when needed.
  virtual void logRewardIfNeeded(const MachineFunction &MF,
                                 llvm::function_ref<float()> GetReward) {};

protected:
  /// Preserve all analyses; subclasses may declare extra requirements.
  ///
  /// \param AU Analysis usage to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }

  /// Owned provider that constructs per-function advisors.
  std::unique_ptr<RegAllocPriorityAdvisorProvider> Provider;

private:
  StringRef getPassName() const override;
  const AdvisorMode Mode;
};

/// Specialization for the API used by the analysis infrastructure to create
/// an instance of the priority advisor.
///
/// \return Newly constructed legacy priority advisor analysis pass.
template <>
LLVM_ABI Pass *callDefaultCtor<RegAllocPriorityAdvisorAnalysisLegacy>();

/// Create the legacy analysis for the release-mode ML priority advisor.
///
/// \return Newly created release-mode legacy analysis pass.
LLVM_ABI RegAllocPriorityAdvisorAnalysisLegacy *
createReleaseModePriorityAdvisorAnalysis();

/// Create the legacy analysis for the development-mode ML priority advisor.
///
/// \return Newly created development-mode legacy analysis pass.
LLVM_ABI RegAllocPriorityAdvisorAnalysisLegacy *
createDevelopmentModePriorityAdvisorAnalysis();

/// Create a release-mode ML priority advisor provider.
///
/// \return Newly created release-mode advisor provider.
LLVM_ATTRIBUTE_RETURNS_NONNULL LLVM_ABI RegAllocPriorityAdvisorProvider *
createReleaseModePriorityAdvisorProvider();

/// Create a development-mode ML priority advisor provider.
///
/// \param Ctx LLVM context used to set up logging and evaluation.
/// \return Newly created development-mode advisor provider.
LLVM_ATTRIBUTE_RETURNS_NONNULL LLVM_ABI RegAllocPriorityAdvisorProvider *
createDevelopmentModePriorityAdvisorProvider(LLVMContext &Ctx);

} // namespace llvm

#endif // LLVM_CODEGEN_REGALLOCPRIORITYADVISOR_H
