//===- RegAllocEvictionAdvisor.h - Interference resolution ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_REGALLOCEVICTIONADVISOR_H
#define LLVM_CODEGEN_REGALLOCEVICTIONADVISOR_H

#include "llvm/ADT/Any.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/PassManager.h"
#include "llvm/MC/MCRegister.h"
#include "llvm/Pass.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
/// Preferred order of physical registers for allocating a virtual register.
class AllocationOrder;
class LiveInterval;
class LiveIntervals;
class LiveRegMatrix;
class MachineFunction;
class MachineRegisterInfo;
class RegisterClassInfo;
class TargetRegisterInfo;
class VirtRegMap;

/// Small set of virtual registers used to mark fixed interference.
using SmallVirtRegSet = SmallSet<Register, 16>;

/// Stages a live range passes through during greedy register allocation.
///
/// Live ranges pass through a number of stages as we try to allocate them.
/// Some of the stages may also create new live ranges:
///
/// - Region splitting.
/// - Per-block splitting.
/// - Local splitting.
/// - Spilling.
///
/// Ranges produced by one of the stages skip the previous stages when they are
/// dequeued. This improves performance because we can skip interference checks
/// that are unlikely to give any results. It also guarantees that the live
/// range splitting algorithm terminates, something that is otherwise hard to
/// ensure.
enum LiveRangeStage {
  /// Newly created live range that has never been queued.
  RS_New,

  /// Only attempt assignment and eviction. Then requeue as RS_Split.
  RS_Assign,

  /// Attempt live range splitting if assignment is impossible.
  RS_Split,

  /// Attempt more aggressive live range splitting that is guaranteed to make
  /// progress.  This is used for split products that may not be making
  /// progress.
  RS_Split2,

  /// Live range will be spilled.  No more splitting will be attempted.
  RS_Spill,

  /// There is nothing more we can do to this live range.  Abort compilation
  /// if it can't be assigned.
  RS_Done
};

/// Cost of evicting interference - used by default advisor, and the eviction
/// chain heuristic in RegAllocGreedy.
// FIXME: this can be probably made an implementation detail of the default
// advisor, if the eviction chain logic can be refactored.
struct EvictionCost {
  unsigned BrokenHints = 0; ///< Total number of broken hints.
  float MaxWeight = 0;      ///< Maximum spill weight evicted.

  /// Construct a zero eviction cost.
  EvictionCost() = default;

  /// Return true if this cost is the maximum (unbeatable) value.
  ///
  /// \return True if this cost is the maximum (unbeatable) value.
  bool isMax() const { return BrokenHints == ~0u; }

  /// Set this cost to the maximum (unbeatable) value.
  void setMax() { BrokenHints = ~0u; }

  /// Set the number of broken allocation hints.
  ///
  /// \param NHints Number of broken hints to record.
  void setBrokenHints(unsigned NHints) { BrokenHints = NHints; }

  /// Return true if this cost is strictly less than \p O.
  ///
  /// \param O Other eviction cost to compare against.
  /// \return True if this cost is strictly less than \p O.
  bool operator<(const EvictionCost &O) const {
    return std::tie(BrokenHints, MaxWeight) <
           std::tie(O.BrokenHints, O.MaxWeight);
  }

  /// Return true if this cost is greater than or equal to \p O.
  ///
  /// \param O Other eviction cost to compare against.
  /// \return True if this cost is greater than or equal to \p O.
  bool operator>=(const EvictionCost &O) const { return !(*this < O); }
};

/// Greedy register allocator that provides analysis state to advisors.
class RAGreedy;
/// Interface to the eviction advisor for choosing live ranges to evict.
class RegAllocEvictionAdvisor {
public:
  /// Deleted copy constructor; eviction advisors are not copyable.
  ///
  /// \param Other Unused; copy construction is deleted.
  RegAllocEvictionAdvisor(const RegAllocEvictionAdvisor &Other) = delete;
  /// Deleted move constructor; eviction advisors are not movable.
  ///
  /// \param Other Unused; move construction is deleted.
  RegAllocEvictionAdvisor(RegAllocEvictionAdvisor &&Other) = delete;
  /// Destroy this eviction advisor.
  virtual ~RegAllocEvictionAdvisor() = default;

  /// Find a physical register that can be freed by eviction, or NoRegister.
  ///
  /// The eviction decision is assumed to be correct (i.e. no fixed live ranges
  /// are evicted) and profitable.
  ///
  /// \param VirtReg Virtual register being allocated.
  /// \param Order Preferred physical register allocation order.
  /// \param CostPerUseLimit Maximum allowed cost-per-use of a candidate.
  /// \param FixedRegisters Virtual registers that must not be evicted.
  /// \return Physical register freed by eviction, or NoRegister if none.
  virtual MCRegister tryFindEvictionCandidate(
      const LiveInterval &VirtReg, const AllocationOrder &Order,
      uint8_t CostPerUseLimit, const SmallVirtRegSet &FixedRegisters) const = 0;

  /// Find out if we can evict the live ranges occupying the given PhysReg,
  /// which is a hint (preferred register) for VirtReg.
  ///
  /// \param VirtReg Virtual register whose hint is being considered.
  /// \param PhysReg Hint physical register whose interference may be evicted.
  /// \param FixedRegisters Virtual registers that must not be evicted.
  /// \return True if the interference on \p PhysReg can be evicted.
  virtual bool
  canEvictHintInterference(const LiveInterval &VirtReg, MCRegister PhysReg,
                           const SmallVirtRegSet &FixedRegisters) const = 0;

  /// Returns true if the given \p PhysReg is a callee saved register and has
  /// not been used for allocation yet.
  ///
  /// \param PhysReg Physical register to query.
  /// \return True if \p PhysReg is an unused callee-saved register.
  LLVM_ABI bool isUnusedCalleeSavedReg(MCRegister PhysReg) const;

  /// Returns true if this is an urgent eviction.
  ///
  /// \param VirtReg Virtual register being allocated.
  /// \param Intf Interfering live interval considered for eviction.
  /// \return True if evicting \p Intf for \p VirtReg is urgent.
  LLVM_ABI bool isUrgentEviction(const LiveInterval &VirtReg,
                                 const LiveInterval &Intf) const;

protected:
  /// Construct an advisor for \p MF backed by greedy allocator \p RA.
  ///
  /// \param MF Machine function being allocated.
  /// \param RA Greedy register allocator providing analysis state.
  LLVM_ABI RegAllocEvictionAdvisor(const MachineFunction &MF,
                                   const RAGreedy &RA);

  /// Return true if \p VirtReg can be reassigned away from \p FromReg.
  ///
  /// \param VirtReg Virtual register considered for reassignment.
  /// \param FromReg Physical register currently assigned to \p VirtReg.
  /// \return True if \p VirtReg can be reassigned away from \p FromReg.
  LLVM_ABI bool canReassign(const LiveInterval &VirtReg,
                            MCRegister FromReg) const;

  /// Get the upper limit of elements in \p Order that need to be analyzed.
  ///
  /// TODO: is this heuristic,  we could consider learning it.
  ///
  /// \param VirtReg Virtual register being allocated.
  /// \param Order Preferred physical register allocation order.
  /// \param CostPerUseLimit Maximum allowed cost-per-use of a candidate.
  /// \return Limit on candidates to consider, or nullopt if none apply.
  LLVM_ABI std::optional<unsigned>
  getOrderLimit(const LiveInterval &VirtReg, const AllocationOrder &Order,
                unsigned CostPerUseLimit) const;

  /// Return true if allocating \p PhysReg is worthwhile under the cost limit.
  ///
  /// TODO: this is a heuristic component we could consider learning, too.
  ///
  /// \param CostPerUseLimit Maximum allowed cost-per-use of a candidate.
  /// \param PhysReg Physical register considered for allocation.
  /// \return True if allocating \p PhysReg is worthwhile under the limit.
  LLVM_ABI bool canAllocatePhysReg(unsigned CostPerUseLimit,
                                   MCRegister PhysReg) const;

  /// Machine function currently being allocated.
  const MachineFunction &MF;
  /// Greedy register allocator that owns related analyses.
  const RAGreedy &RA;
  /// Live register matrix used to query interference.
  LiveRegMatrix *const Matrix;
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
  /// Per-physical-register cost-per-use values.
  const ArrayRef<uint8_t> RegCosts;

  /// Run or not the local reassignment heuristic. This information is
  /// obtained from the TargetSubtargetInfo.
  const bool EnableLocalReassign;
};

/// Common provider for legacy and new pass managers.
///
/// This keeps the state for logging, and sets up and holds the provider.
/// The legacy pass itself used to keep the logging state and provider,
/// so this extraction helps the NPM analysis to reuse the logic.
/// TODO: Coalesce this with the NPM analysis when legacy PM is removed.
class RegAllocEvictionAdvisorProvider {
public:
  /// Which eviction advisor implementation to construct.
  enum class AdvisorMode : int {
    /// Heuristic default eviction advisor.
    Default,
    /// Release-mode ML eviction advisor.
    Release,
    /// Development-mode ML eviction advisor with training logs.
    Development
  };

  /// Construct a provider for advisor mode \p Mode using context \p Ctx.
  ///
  /// \param Mode Advisor implementation mode to use.
  /// \param Ctx LLVM context used by ML and logging setups.
  RegAllocEvictionAdvisorProvider(AdvisorMode Mode, LLVMContext &Ctx)
      : Ctx(Ctx), Mode(Mode) {}

  /// Destroy this provider.
  virtual ~RegAllocEvictionAdvisorProvider() = default;

  /// Log a reward for \p MF when the advisor mode requires it.
  ///
  /// \param MF Machine function whose allocation outcome is rewarded.
  /// \param GetReward Callback that computes the reward value when needed.
  virtual void logRewardIfNeeded(const MachineFunction &MF,
                                 llvm::function_ref<float()> GetReward) {}

  /// Create an eviction advisor for \p MF and allocator \p RA.
  ///
  /// \param MF Machine function being allocated.
  /// \param RA Greedy register allocator requesting advice.
  /// \param MBFI Optional block frequency info for heuristics or ML features.
  /// \param Loops Optional loop info for heuristics or ML features.
  /// \return Newly created eviction advisor for \p MF.
  virtual std::unique_ptr<RegAllocEvictionAdvisor>
  getAdvisor(const MachineFunction &MF, const RAGreedy &RA,
             MachineBlockFrequencyInfo *MBFI, MachineLoopInfo *Loops) = 0;

  /// Return the advisor mode this provider was constructed with.
  ///
  /// \return Advisor implementation mode this provider was constructed with.
  AdvisorMode getAdvisorMode() const { return Mode; }

protected:
  /// LLVM context used when constructing advisors and loggers.
  LLVMContext &Ctx;

private:
  const AdvisorMode Mode;
};

/// Immutable analysis that provides the eviction advisor.
///
/// We model it as an analysis to decouple the user from the implementation
/// insofar as dependencies on other analyses goes. The motivation for it being
/// an immutable pass is twofold:
/// - in the ML implementation case, the evaluator is stateless but (especially
/// in the development mode) expensive to set up. With an immutable pass, we set
/// it up once.
/// - in the 'development' mode ML case, we want to capture the training log
/// during allocation (this is a log of features encountered and decisions
/// made), and then measure a score, potentially a few steps after allocation
/// completes. So we need the properties of an immutable pass to keep the logger
/// state around until we can make that measurement.
///
/// Because we need to offer additional services in 'development' mode, the
/// implementations of this analysis need to implement RTTI support.
class LLVM_ABI RegAllocEvictionAdvisorAnalysisLegacy : public ImmutablePass {
public:
  /// Which eviction advisor implementation this analysis provides.
  enum class AdvisorMode : int {
    /// Heuristic default eviction advisor.
    Default,
    /// Release-mode ML eviction advisor.
    Release,
    /// Development-mode ML eviction advisor with training logs.
    Development
  };

  /// Construct the analysis for advisor mode \p Mode.
  ///
  /// \param Mode Advisor implementation mode to provide.
  RegAllocEvictionAdvisorAnalysisLegacy(AdvisorMode Mode)
      : ImmutablePass(ID), Mode(Mode) {};
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Get an advisor for the given context (i.e. machine function, etc)
  ///
  /// \return Provider that constructs per-function eviction advisors.
  RegAllocEvictionAdvisorProvider &getProvider() { return *Provider; }

  /// Return the advisor mode this analysis was constructed with.
  ///
  /// \return Advisor implementation mode this analysis was constructed with.
  AdvisorMode getAdvisorMode() const { return Mode; }
  /// Log a reward for \p MF when the advisor mode requires it.
  ///
  /// \param MF Machine function whose allocation outcome is rewarded.
  /// \param GetReward Callback that computes the reward value when needed.
  virtual void logRewardIfNeeded(const MachineFunction &MF,
                                 function_ref<float()> GetReward) {};

protected:
  /// Preserve all analyses; subclasses may declare extra requirements.
  ///
  /// \param AU Analysis usage to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }
  /// Owned provider that constructs per-function advisors.
  std::unique_ptr<RegAllocEvictionAdvisorProvider> Provider;

private:
  StringRef getPassName() const override;
  const AdvisorMode Mode;
};

/// MachineFunction analysis that lazily provides the eviction advisor.
///
/// This sets up the Provider lazily and caches it.
/// - in the ML implementation case, the evaluator is stateless but (especially
/// in the development mode) expensive to set up. With a Module Analysis, we
/// `require` it and set it up once.
/// - in the 'development' mode ML case, we want to capture the training log
/// during allocation (this is a log of features encountered and decisions
/// made), and then measure a score, potentially a few steps after allocation
/// completes. So we need a Module analysis to keep the logger state around
/// until we can make that measurement.
class RegAllocEvictionAdvisorAnalysis
    : public AnalysisInfoMixin<RegAllocEvictionAdvisorAnalysis> {
  static AnalysisKey Key;
  friend AnalysisInfoMixin<RegAllocEvictionAdvisorAnalysis>;

public:
  /// Cached analysis result holding the shared advisor provider.
  struct Result {
    // owned by this analysis
    /// Provider owned by the analysis and shared across functions.
    RegAllocEvictionAdvisorProvider *Provider;

    /// Return false so the provider is never invalidated.
    ///
    /// Provider is stateless and constructed only once.
    ///
    /// \param MF Machine function for which invalidation is queried.
    /// \param PA Set of analyses preserved by the last transformation.
    /// \param Inv Invalidator for other machine-function analyses.
    /// \return Always false; the provider is never invalidated.
    bool invalidate(MachineFunction &MF, const PreservedAnalyses &PA,
                    MachineFunctionAnalysisManager::Invalidator &Inv) {
      // Provider is stateless and constructed only once. Do not get
      // invalidated.
      return false;
    }
  };

  /// Run the analysis on \p MF and return the cached provider result.
  ///
  /// \param MF Machine function being analyzed.
  /// \param MAM Manager used to query dependent analyses.
  /// \return Cached result holding the shared advisor provider.
  LLVM_ABI Result run(MachineFunction &MF, MachineFunctionAnalysisManager &MAM);

private:
  void
  initializeProvider(RegAllocEvictionAdvisorAnalysisLegacy::AdvisorMode Mode,
                     LLVMContext &Ctx);

  std::unique_ptr<RegAllocEvictionAdvisorProvider> Provider;
};

/// Specialization for the API used by the analysis infrastructure to create
/// an instance of the eviction advisor.
///
/// \return Newly constructed legacy eviction advisor analysis pass.
template <>
LLVM_ABI Pass *callDefaultCtor<RegAllocEvictionAdvisorAnalysisLegacy>();

/// Create the legacy analysis for the release-mode ML eviction advisor.
///
/// \return Newly created release-mode legacy analysis pass.
LLVM_ABI RegAllocEvictionAdvisorAnalysisLegacy *
createReleaseModeAdvisorAnalysisLegacy();

/// Create the legacy analysis for the development-mode ML eviction advisor.
///
/// \return Newly created development-mode legacy analysis pass.
LLVM_ABI RegAllocEvictionAdvisorAnalysisLegacy *
createDevelopmentModeAdvisorAnalysisLegacy();

/// Create a release-mode ML eviction advisor provider.
///
/// \param Ctx LLVM context used to set up the ML evaluator.
/// \return Newly created release-mode advisor provider.
LLVM_ATTRIBUTE_RETURNS_NONNULL LLVM_ABI RegAllocEvictionAdvisorProvider *
createReleaseModeAdvisorProvider(LLVMContext &Ctx);

/// Create a development-mode ML eviction advisor provider.
///
/// \param Ctx LLVM context used to set up logging and evaluation.
/// \return Newly created development-mode advisor provider.
LLVM_ABI RegAllocEvictionAdvisorProvider *
createDevelopmentModeAdvisorProvider(LLVMContext &Ctx);

// TODO: move to RegAllocEvictionAdvisor.cpp when we move implementation
// out of RegAllocGreedy.cpp
/// Default heuristic eviction advisor used by greedy register allocation.
class LLVM_ABI DefaultEvictionAdvisor : public RegAllocEvictionAdvisor {
public:
  /// Construct the default advisor for \p MF and allocator \p RA.
  ///
  /// \param MF Machine function being allocated.
  /// \param RA Greedy register allocator providing analysis state.
  DefaultEvictionAdvisor(const MachineFunction &MF, const RAGreedy &RA)
      : RegAllocEvictionAdvisor(MF, RA) {}

private:
  MCRegister tryFindEvictionCandidate(const LiveInterval &,
                                      const AllocationOrder &, uint8_t,
                                      const SmallVirtRegSet &) const override;
  bool canEvictHintInterference(const LiveInterval &, MCRegister,
                                const SmallVirtRegSet &) const override;
  bool canEvictInterferenceBasedOnCost(const LiveInterval &, MCRegister, bool,
                                       EvictionCost &,
                                       const SmallVirtRegSet &) const;
  bool shouldEvict(const LiveInterval &A, bool, const LiveInterval &B,
                   bool) const;
};
} // namespace llvm

#endif // LLVM_CODEGEN_REGALLOCEVICTIONADVISOR_H
