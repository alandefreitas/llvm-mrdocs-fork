//===- llvm/CodeGen/TargetSubtargetInfo.h - Target Information --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the subtarget options of a Target machine.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_TARGETSUBTARGETINFO_H
#define LLVM_CODEGEN_TARGETSUBTARGETINFO_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringTable.h"
#include "llvm/CodeGen/MacroFusion.h"
#include "llvm/CodeGen/PBQPRAConstraint.h"
#include "llvm/CodeGen/SchedulerRegistry.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Compiler.h"
#include <memory>
#include <vector>

namespace llvm {

class APInt;
class BitVector;
class MachineFunction;
class ScheduleDAGMutation;
class CallLowering;
class GlobalValue;
class InlineAsmLowering;
class InstrItineraryData;
struct InstrStage;
class InstructionSelector;
class LegalizerInfo;
class LibcallLoweringInfo;
class MachineInstr;
struct MachinePipelinerPolicy;
struct MachineSchedPolicy;
struct MCReadAdvanceEntry;
struct MCSchedModel;
struct MCWriteLatencyEntry;
struct MCWriteProcResEntry;
class RegisterBankInfo;
class SDep;
class SelectionDAGTargetInfo;
class SUnit;
class TargetFrameLowering;
class TargetInstrInfo;
class TargetLowering;
class MCRegisterClass;
using TargetRegisterClass = MCRegisterClass;
class TargetRegisterInfo;
class TargetSchedModel;
class Triple;
struct SchedRegion;

//===----------------------------------------------------------------------===//
///
/// TargetSubtargetInfo - Generic base class for all target subtargets.  All
/// Target-specific options that control code generation and printing should
/// be exposed through a TargetSubtargetInfo-derived class.
///
class LLVM_ABI TargetSubtargetInfo : public MCSubtargetInfo {
protected: // Can only create subclasses...
  /// Construct target subtarget info from the MC tables for this target.
  ///
  /// \param TT Target triple for this subtarget.
  /// \param CPU CPU name being targeted.
  /// \param TuneCPU CPU name being tuned for.
  /// \param FS Feature string of plus/minus feature flags.
  /// \param PN Processor name string table, including aliases.
  /// \param PF Processor feature key-value table.
  /// \param PD Processor/subtype description table.
  /// \param PA CPU alias to processor index map.
  /// \param PSM Array of processor scheduling models.
  /// \param WPR Write processor-resource table.
  /// \param WL Write latency table.
  /// \param RA Read advance table.
  /// \param IS Instruction itinerary stage table.
  /// \param OC Itinerary operand cycle table.
  /// \param FP Forwarding path table.
  TargetSubtargetInfo(const Triple &TT, StringRef CPU, StringRef TuneCPU,
                      StringRef FS, StringTable PN,
                      ArrayRef<SubtargetFeatureKV> PF,
                      ArrayRef<SubtargetSubTypeKV> PD,
                      ArrayRef<SubtargetSubTypeAliasKV> PA,
                      const MCSchedModel *PSM, const MCWriteProcResEntry *WPR,
                      const MCWriteLatencyEntry *WL,
                      const MCReadAdvanceEntry *RA, const InstrStage *IS,
                      const unsigned *OC, const unsigned *FP);

public:
  /// Type of anti-dependence breaking performed before post-RA scheduling.
  using AntiDepBreakMode = enum {
    /// Do not break anti-dependencies.
    ANTIDEP_NONE,
    /// Break only critical-path anti-dependencies.
    ANTIDEP_CRITICAL,
    /// Break all anti-dependencies.
    ANTIDEP_ALL
  };
  /// Vector of target register classes used by anti-dependence breaking.
  using RegClassVector = SmallVectorImpl<const TargetRegisterClass *>;

  /// TargetSubtargetInfo is not default-constructible.
  TargetSubtargetInfo() = delete;
  /// TargetSubtargetInfo is not copyable.
  ///
  /// \param Other Unused source object; copying is deleted.
  TargetSubtargetInfo(const TargetSubtargetInfo &Other) = delete;
  /// TargetSubtargetInfo is not copyable.
  ///
  /// \param Other Unused source object; assignment is deleted.
  TargetSubtargetInfo &operator=(const TargetSubtargetInfo &Other) = delete;
  /// Destroy the target subtarget info.
  ~TargetSubtargetInfo() override;

  /// Return true if XRay instrumentation is supported on this subtarget.
  ///
  /// \return True if XRay instrumentation is supported on this subtarget.
  virtual bool isXRaySupported() const { return false; }

  /// Return true if the given target intrinsic is supported by this subtarget.
  ///
  /// \param IntrinsicID Intrinsic ID to test.
  /// \return True if the given target intrinsic is supported by this subtarget.
  bool isIntrinsicSupported(unsigned IntrinsicID) const;

  // Interfaces to the major aspects of target machine information:
  //
  // -- Instruction opcode and operand information
  // -- Pipelines and scheduling information
  // -- Stack frame information
  // -- Selection DAG lowering information
  // -- Call lowering information
  //
  // N.B. These objects may change during compilation. It's not safe to cache
  // them between functions.
  /// Return the target instruction info, or nullptr if unavailable.
  ///
  /// \return The target instruction info, or nullptr if unavailable.
  virtual const TargetInstrInfo *getInstrInfo() const { return nullptr; }
  /// Return the target frame-lowering info, or nullptr if unavailable.
  ///
  /// \return The target frame-lowering info, or nullptr if unavailable.
  virtual const TargetFrameLowering *getFrameLowering() const {
    return nullptr;
  }
  /// Return the target lowering info, or nullptr if unavailable.
  ///
  /// \return The target lowering info, or nullptr if unavailable.
  virtual const TargetLowering *getTargetLowering() const { return nullptr; }
  /// Return the SelectionDAG target info, or nullptr if unavailable.
  ///
  /// \return The SelectionDAG target info, or nullptr if unavailable.
  virtual const SelectionDAGTargetInfo *getSelectionDAGInfo() const {
    return nullptr;
  }
  /// Return the call-lowering info, or nullptr if unavailable.
  ///
  /// \return The call-lowering info, or nullptr if unavailable.
  virtual const CallLowering *getCallLowering() const { return nullptr; }

  /// Return the inline-asm lowering info, or nullptr if unavailable.
  ///
  /// \return The inline-asm lowering info, or nullptr if unavailable.
  virtual const InlineAsmLowering *getInlineAsmLowering() const {
    return nullptr;
  }

  // FIXME: This lets targets specialize the selector by subtarget (which lets
  // us do things like a dedicated avx512 selector).  However, we might want
  // to also specialize selectors by MachineFunction, which would let us be
  // aware of optsize/optnone and such.
  /// Return the GlobalISel instruction selector, or nullptr if unavailable.
  ///
  /// \return The GlobalISel instruction selector, or nullptr if unavailable.
  virtual InstructionSelector *getInstructionSelector() const {
    return nullptr;
  }

  /// Return a constructor for an alternate DAG scheduler, or nullptr.
  ///
  /// Targets can subclass this hook to select a different DAG scheduler.
  ///
  /// \param OptLevel Code generation optimization level.
  /// \return A constructor for an alternate DAG scheduler, or nullptr.
  virtual RegisterScheduler::FunctionPassCtor
  getDAGScheduler(CodeGenOptLevel OptLevel) const {
    return nullptr;
  }

  /// Return the GlobalISel legalizer info, or nullptr if unavailable.
  ///
  /// \return The GlobalISel legalizer info, or nullptr if unavailable.
  virtual const LegalizerInfo *getLegalizerInfo() const { return nullptr; }

  /// Return the target's register information.
  ///
  /// \return The target's register information.
  virtual const TargetRegisterInfo *getRegisterInfo() const = 0;

  /// If the information for the register banks is available, return it.
  /// Otherwise return nullptr.
  ///
  /// \return The register bank info, or nullptr if unavailable.
  virtual const RegisterBankInfo *getRegBankInfo() const { return nullptr; }

  /// getInstrItineraryData - Returns instruction itinerary data for the target
  /// or specific subtarget.
  ///
  /// \return Instruction itinerary data for the target or specific subtarget.
  virtual const InstrItineraryData *getInstrItineraryData() const {
    return nullptr;
  }

  /// Return the number of extra cycles the processor takes to recover from a
  /// branch misprediction. Defaults to the value in the scheduling model.
  ///
  /// \return The number of extra cycles to recover from a branch misprediction.
  virtual unsigned getMispredictionPenalty() const {
    return getSchedModel().MispredictPenalty;
  }

  /// Return the expected latency of load instructions. Defaults to the value
  /// in the scheduling model.
  ///
  /// \return The expected latency of load instructions.
  virtual unsigned getLoadLatency() const {
    return getSchedModel().LoadLatency;
  }

  /// Configure the LibcallLoweringInfo for this subtarget.
  ///
  /// The libcalls will be pre-configured with defaults based on
  /// RuntimeLibcallsInfo. This may be used to override those decisions, such as
  /// disambiguating alternative implementations.
  ///
  /// \param Info Libcall lowering info to configure for this subtarget.
  virtual void initLibcallLoweringInfo(LibcallLoweringInfo &Info) const {}

  /// Resolve a variant SchedClass to a concrete scheduling class ID.
  ///
  /// \p SchedClass identifies an MCSchedClassDesc with the isVariant property.
  /// This may return the ID of another variant SchedClass, but repeated
  /// invocation must quickly terminate in a nonvariant SchedClass.
  ///
  /// \param SchedClass Variant scheduling class ID to resolve.
  /// \param MI Machine instruction whose class is being resolved.
  /// \param SchedModel Scheduling model for the current subtarget.
  /// \return A concrete scheduling class ID.
  virtual unsigned resolveSchedClass(unsigned SchedClass,
                                     const MachineInstr *MI,
                                     const TargetSchedModel *SchedModel) const {
    return 0;
  }

  /// Returns true if MI is a dependency breaking zero-idiom instruction for the
  /// subtarget.
  ///
  /// This function also sets bits in Mask related to input operands that
  /// are not in a data dependency relationship.  There is one bit for each
  /// machine operand; implicit operands follow explicit operands in the bit
  /// representation used for Mask.  An empty (i.e. a mask with all bits
  /// cleared) means: data dependencies are "broken" for all the explicit input
  /// machine operands of MI.
  ///
  /// \param MI Machine instruction being tested.
  /// \param Mask Bit mask of operands whose data dependencies are broken.
  /// \return True if MI is a dependency breaking zero-idiom instruction.
  virtual bool isZeroIdiom(const MachineInstr *MI, APInt &Mask) const {
    return false;
  }

  /// Returns true if MI is a dependency breaking instruction for the subtarget.
  ///
  /// Similar in behavior to `isZeroIdiom`. However, it knows how to identify
  /// all dependency breaking instructions (i.e. not just zero-idioms).
  ///
  /// As for `isZeroIdiom`, this method returns a mask of "broken" dependencies.
  /// (See method `isZeroIdiom` for a detailed description of Mask).
  ///
  /// \param MI Machine instruction being tested.
  /// \param Mask Bit mask of operands whose data dependencies are broken.
  /// \return True if MI is a dependency breaking instruction for the subtarget.
  virtual bool isDependencyBreaking(const MachineInstr *MI, APInt &Mask) const {
    return isZeroIdiom(MI, Mask);
  }

  /// Returns true if MI is a candidate for move elimination.
  ///
  /// A candidate for move elimination may be optimized out at register renaming
  /// stage. Subtargets can specify the set of optimizable moves by
  /// instantiating tablegen class `IsOptimizableRegisterMove` (see
  /// llvm/Target/TargetInstrPredicate.td).
  ///
  /// SubtargetEmitter is responsible for processing all the definitions of class
  /// IsOptimizableRegisterMove, and auto-generate an override for this method.
  ///
  /// \param MI Machine instruction being tested.
  /// \return True if MI is a candidate for move elimination.
  virtual bool isOptimizableRegisterMove(const MachineInstr *MI) const {
    return false;
  }

  /// True if the subtarget should run MachineScheduler after aggressive
  /// coalescing.
  ///
  /// This currently replaces the SelectionDAG scheduler with the "source" order
  /// scheduler (though see below for an option to turn this off and use the
  /// TargetLowering preference). It does not yet disable the postRA scheduler.
  ///
  /// \return True if the subtarget should run MachineScheduler after aggressive
  /// coalescing.
  virtual bool enableMachineScheduler() const;

  /// True if the machine scheduler should disable the TLI preference
  /// for preRA scheduling with the source level scheduler.
  ///
  /// \return True if the machine scheduler should disable the TLI preference
  /// for preRA scheduling with the source level scheduler.
  virtual bool enableMachineSchedDefaultSched() const { return true; }

  /// True if the subtarget should run MachinePipeliner
  ///
  /// \return True if the subtarget should run MachinePipeliner.
  virtual bool enableMachinePipeliner() const { return true; };

  /// True if the subtarget should run WindowScheduler.
  ///
  /// \return True if the subtarget should run WindowScheduler.
  virtual bool enableWindowScheduler() const { return true; }

  /// True if the subtarget should enable joining global copies.
  ///
  /// By default this is enabled if the machine scheduler is enabled, but
  /// can be overridden.
  ///
  /// \return True if the subtarget should enable joining global copies.
  virtual bool enableJoinGlobalCopies() const;

  /// Hack to bring up option. This should be unconditionally true, all targets
  /// should enable it and delete this.
  ///
  /// \return True if the terminal rule should be enabled.
  virtual bool enableTerminalRule() const { return false; }

  /// True if the subtarget should run a scheduler after register allocation.
  ///
  /// By default this queries the PostRAScheduling bit in the scheduling model
  /// which is the preferred way to influence this.
  ///
  /// \return True if the subtarget should run a scheduler after register
  /// allocation.
  virtual bool enablePostRAScheduler() const;

  /// True if the subtarget should run a machine scheduler after register
  /// allocation.
  ///
  /// \return True if the subtarget should run a machine scheduler after
  /// register allocation.
  virtual bool enablePostRAMachineScheduler() const;

  /// True if the subtarget should run the atomic expansion pass.
  ///
  /// \return True if the subtarget should run the atomic expansion pass.
  virtual bool enableAtomicExpand() const;

  /// True if the subtarget should run the indirectbr expansion pass.
  ///
  /// \return True if the subtarget should run the indirectbr expansion pass.
  virtual bool enableIndirectBrExpand() const;

  /// Override generic scheduling policy within a region.
  ///
  /// This is a convenient way for targets that don't provide any custom
  /// scheduling heuristics (no custom MachineSchedStrategy) to make
  /// changes to the generic scheduling policy.
  ///
  /// \param Policy Scheduling policy to adjust.
  /// \param Region Scheduling region the policy applies to.
  virtual void overrideSchedPolicy(MachineSchedPolicy &Policy,
                                   const SchedRegion &Region) const {}

  /// Override generic post-ra scheduling policy within a region.
  ///
  /// This is a convenient way for targets that don't provide any custom
  /// scheduling heuristics (no custom MachineSchedStrategy) to make
  /// changes to the generic  post-ra scheduling policy.
  /// Note that some options like tracking register pressure won't take effect
  /// in post-ra scheduling.
  ///
  /// \param Policy Post-RA scheduling policy to adjust.
  /// \param Region Scheduling region the policy applies to.
  virtual void overridePostRASchedPolicy(MachineSchedPolicy &Policy,
                                         const SchedRegion &Region) const {}

  /// Override generic software pipelining policy.
  ///
  /// \param Policy Machine pipeliner policy to adjust.
  virtual void overridePipelinerPolicy(MachinePipelinerPolicy &Policy) const {}

  /// Adjust the latency of a schedule dependency for this target.
  ///
  /// If a pair of operands is associated with the schedule dependency,
  /// \p DefOpIdx and \p UseOpIdx are the indices of the operands in \p Def and
  /// \p Use, respectively. Otherwise, either may be -1.
  ///
  /// \param Def Defining scheduling unit of the dependency.
  /// \param DefOpIdx Operand index in \p Def, or -1 if none.
  /// \param Use Using scheduling unit of the dependency.
  /// \param UseOpIdx Operand index in \p Use, or -1 if none.
  /// \param Dep Schedule dependency whose latency may be adjusted.
  /// \param SchedModel Scheduling model for the current subtarget.
  virtual void adjustSchedDependency(SUnit *Def, int DefOpIdx, SUnit *Use,
                                     int UseOpIdx, SDep &Dep,
                                     const TargetSchedModel *SchedModel) const {
  }

  /// Return the anti-dependence breaking mode used before post-RA scheduling.
  ///
  /// \return The anti-dependence breaking mode used before post-RA scheduling.
  virtual AntiDepBreakMode getAntiDepBreakMode() const { return ANTIDEP_NONE; }

  /// Collect register classes considered for critical-path anti-dep breaking.
  ///
  /// For use with PostRAScheduling: fills \p CriticalPathRCs with register
  /// classes that should only be considered for anti-dependence breaking if
  /// they are on the critical path.
  ///
  /// \param CriticalPathRCs Output vector of critical-path register classes.
  virtual void getCriticalPathRCs(RegClassVector &CriticalPathRCs) const {
    return CriticalPathRCs.clear();
  }

  /// Append schedule DAG mutations for the post-RA scheduler.
  ///
  /// \param Mutations Ordered list of schedule DAG mutations to extend.
  virtual void getPostRAMutations(
      std::vector<std::unique_ptr<ScheduleDAGMutation>> &Mutations) const {
  }

  /// Append schedule DAG mutations for the machine pipeliner (SMS).
  ///
  /// \param Mutations Ordered list of schedule DAG mutations to extend.
  virtual void getSMSMutations(
      std::vector<std::unique_ptr<ScheduleDAGMutation>> &Mutations) const {
  }

  /// Default to DFA for resource management, return false when target will use
  /// ProcResource in InstrSchedModel instead.
  ///
  /// \return True if DFA should be used for SMS resource management.
  virtual bool useDFAforSMS() const { return true; }

  /// Return the minimum optimization level that enables post-RA scheduling.
  ///
  /// For use with PostRAScheduling.
  ///
  /// \return The minimum optimization level that enables post-RA scheduling.
  virtual CodeGenOptLevel getOptLevelToEnablePostRAScheduler() const {
    return CodeGenOptLevel::Default;
  }

  /// True if the subtarget should run the register allocator's local reassignment.
  ///
  /// This heuristic may be compile-time intensive; \p OptLevel provides a
  /// finer grain to tune the register allocator.
  ///
  /// \param OptLevel Code generation optimization level.
  /// \return True if the register allocator's local reassignment should run.
  virtual bool enableRALocalReassignment(CodeGenOptLevel OptLevel) const;

  /// Enable use of alias analysis during code generation (during MI
  /// scheduling, DAGCombine, etc.).
  ///
  /// \return True if alias analysis should be used during code generation.
  virtual bool useAA() const;

  /// \brief Sink addresses into blocks using GEP instructions rather than
  /// pointer casts and arithmetic.
  ///
  /// \return True if addresses should be sunk using GEP instructions.
  virtual bool addrSinkUsingGEPs() const {
    return useAA();
  }

  /// Enable the use of the early if conversion pass.
  ///
  /// \return True if the early if conversion pass should be used.
  virtual bool enableEarlyIfConversion() const { return false; }

  /// Return PBQPConstraint(s) for the target.
  ///
  /// Override to provide custom PBQP constraints.
  ///
  /// \return Custom PBQP constraints for the target, or nullptr if none.
  virtual std::unique_ptr<PBQPRAConstraint> getCustomPBQPConstraints() const {
    return nullptr;
  }

  /// Enable tracking of subregister liveness in register allocator.
  /// Please use MachineRegisterInfo::subRegLivenessEnabled() instead where
  /// possible.
  ///
  /// \return True if subregister liveness tracking should be enabled.
  virtual bool enableSubRegLiveness() const { return false; }

  /// Called after a .mir file has been loaded into \p MF.
  ///
  /// \param MF Machine function populated from the loaded .mir file.
  virtual void mirFileLoaded(MachineFunction &MF) const;

  /// Fill a mask of physical registers that keep their TableGen allocation order.
  ///
  /// Constructs a mask of physical registers whose allocation orders should be
  /// used exactly as written in the TableGen descriptions, rather than
  /// allocating them later if they are callee-saved. \p Mask is empty on entry
  /// and must either remain empty or cover all physical registers.
  ///
  /// \param MF Function whose callee-saved allocation order is being queried.
  /// \param Mask Bit mask of physical registers that keep their written order.
  virtual void getCSRAllocationOrderMask(const MachineFunction &MF,
                                         BitVector &Mask) const {}

  /// Classify a global function reference for address lowering.
  ///
  /// This is mainly used to fetch target-special flags when lowering a
  /// function address, for example to mark that a call should use PLT or
  /// PC-related addressing.
  ///
  /// \param GV Global function being classified.
  /// \return Target-special flags for lowering the function address.
  virtual unsigned char
  classifyGlobalFunctionReference(const GlobalValue *GV) const {
    return 0;
  }

  /// Enable spillage copy elimination in MachineCopyPropagation.
  ///
  /// This helps remove redundant copies generated by the register allocator
  /// when handling complex eviction chains.
  ///
  /// \return True if spillage copy elimination should be enabled.
  virtual bool enableSpillageCopyElimination() const { return false; }

  /// Get the list of MacroFusion predicates.
  ///
  /// \return The list of MacroFusion predicates for this subtarget.
  virtual std::vector<MacroFusionPredTy> getMacroFusions() const { return {}; };

  /// Whether the target has instructions where an early-clobber result
  /// operand cannot overlap with an undef input operand.
  ///
  /// \return True if early-clobber results cannot overlap with undef inputs.
  virtual bool requiresDisjointEarlyClobberAndUndef() const {
    // Conservatively assume such instructions exist by default.
    return true;
  }

  /// Return true if the user has reserved register \p R.
  ///
  /// \param R Register to test for a user reservation.
  /// \return True if the user has reserved register \p R.
  virtual bool isRegisterReservedByUser(Register R) const { return false; }

  /// Target features to ignore for inline compatibility check.
  ///
  /// \return Feature bitset of features to ignore for inline compatibility.
  virtual const FeatureBitset &getInlineIgnoreFeatures() const = 0;
  /// Target features where the callee may have an additional feature,
  /// instead of the caller.
  ///
  /// \return Feature bitset of inverse features for inline compatibility.
  virtual const FeatureBitset &getInlineInverseFeatures() const = 0;
  /// Target features where all mismatches prevent inlining.
  ///
  /// \return Feature bitset of features that must match for inlining.
  virtual const FeatureBitset &getInlineMustMatchFeatures() const = 0;

private:
  /// Lazy, incrementally-populated cache for isIntrinsicSupported().
  mutable DenseMap<unsigned, bool> IntrinsicSupportCache;
};
} // end namespace llvm

#endif // LLVM_CODEGEN_TARGETSUBTARGETINFO_H
