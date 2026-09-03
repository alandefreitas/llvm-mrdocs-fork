//===---------------------- CustomBehaviour.h -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines the base class CustomBehaviour which can be inherited from
/// by specific targets (ex. llvm/tools/llvm-mca/lib/X86CustomBehaviour.h).
/// CustomBehaviour is designed to enforce custom behaviour and dependencies
/// within the llvm-mca pipeline simulation that llvm-mca isn't already capable
/// of extracting from the Scheduling Models.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_MCA_CUSTOMBEHAVIOUR_H
#define LLVM_MCA_CUSTOMBEHAVIOUR_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MCA/SourceMgr.h"
#include "llvm/MCA/View.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
namespace mca {

/// Target hook to adjust mca::Instruction objects before the pipeline runs.
///
/// Class which can be overriden by targets to modify the
/// mca::Instruction objects before the pipeline starts.
/// A common usage of this class is to add immediate operands to certain
/// instructions or to remove Defs/Uses from an instruction where the
/// schedulinng model is incorrect.
class InstrPostProcess {
protected:
  /// Subtarget information for the target being simulated.
  const MCSubtargetInfo &STI;
  /// Instruction info used when post-processing instructions.
  const MCInstrInfo &MCII;

public:
  /// Construct an instruction post-processor for the given subtarget.
  /// \param STI Subtarget information for the target being simulated.
  /// \param MCII Instruction info used when post-processing instructions.
  InstrPostProcess(const MCSubtargetInfo &STI, const MCInstrInfo &MCII)
      : STI(STI), MCII(MCII) {}

  /// Destroy this instruction post-processor.
  virtual ~InstrPostProcess() = default;

  /// Modify an mca::Instruction after it has been lowered from an MCInst.
  ///
  /// This method can be overriden by targets to modify the mca::Instruction
  /// object after it has been lowered from the MCInst.
  /// This is generally a less disruptive alternative to modifying the
  /// scheduling model.
  /// \param Inst The mca instruction to modify.
  /// \param MCI The original MCInst that \p Inst was lowered from.
  virtual void postProcessInstruction(Instruction &Inst, const MCInst &MCI) {}

  /// Clear target-specific state at the start of a new code region.
  ///
  /// The resetState() method gets invoked at the beginning of each code region
  /// so that targets that override this function can clear any state that they
  /// have left from the previous code region.
  virtual void resetState() {}
};

/// Target hook for dependencies not captured by the scheduling model.
///
/// Class which can be overriden by targets to enforce instruction
/// dependencies and behaviours that aren't expressed well enough
/// within the scheduling model for mca to automatically simulate
/// them properly.
/// If you implement this class for your target, make sure to also implement
/// a target specific InstrPostProcess class as well.
class LLVM_ABI CustomBehaviour {
protected:
  /// Subtarget information for the target being simulated.
  const MCSubtargetInfo &STI;
  /// Source manager providing the instruction stream being analyzed.
  const mca::SourceMgr &SrcMgr;
  /// Instruction info used when evaluating custom behaviour.
  const MCInstrInfo &MCII;

public:
  /// Construct a custom-behaviour hook for the given subtarget and source.
  /// \param STI Subtarget information for the target being simulated.
  /// \param SrcMgr Source manager providing the instruction stream.
  /// \param MCII Instruction info used when evaluating custom behaviour.
  CustomBehaviour(const MCSubtargetInfo &STI, const mca::SourceMgr &SrcMgr,
                  const MCInstrInfo &MCII)
      : STI(STI), SrcMgr(SrcMgr), MCII(MCII) {}

  /// Destroy this custom-behaviour hook.
  virtual ~CustomBehaviour();

  /// Return how many cycles \p IR must wait for custom hazards.
  ///
  /// Before the llvm-mca pipeline dispatches an instruction, it first checks
  /// for any register or resource dependencies / hazards. If it doesn't find
  /// any, this method will be invoked to determine if there are any custom
  /// hazards that the instruction needs to wait for.
  /// The return value of this method is the number of cycles that the
  /// instruction needs to wait for.
  /// It's safe to underestimate the number of cycles to wait for since these
  /// checks will be invoked again before the intruction gets dispatched.
  /// However, it's not safe (accurate) to overestimate the number of cycles
  /// to wait for since the instruction will wait for AT LEAST that number of
  /// cycles before attempting to be dispatched again.
  /// \param IssuedInst Instructions already issued that may create hazards.
  /// \param IR Instruction about to be dispatched.
  /// \return Number of cycles \p IR must wait before dispatch.
  virtual unsigned checkCustomHazard(ArrayRef<InstRef> IssuedInst,
                                     const InstRef &IR);

  // Functions that target CBs can override to return a list of
  // target specific Views that need to live within /lib/Target/ so that
  // they can benefit from the target CB or from backend functionality that is
  // not already exposed through MC-layer classes. Keep in mind that how this
  // function is used is that the function is called within llvm-mca.cpp and
  // then each unique_ptr<View> is passed into the PipelinePrinter::addView()
  // function. This function will then std::move the View into its own vector of
  // Views. So any CB that overrides this function needs to make sure that they
  // are not relying on the current address or reference of the View
  // unique_ptrs. If you do need the CB and View to be able to communicate with
  // each other, consider giving the View a reference or pointer to the CB when
  // the View is constructed. Then the View can query the CB for information
  // when it needs it.
  /// Return a vector of Views that will be added before all other Views.
  /// \param IP Instruction printer used by the returned views.
  /// \param Insts Instructions in the region being analyzed.
  /// \return Target-specific Views to insert at the start of the view list.
  virtual std::vector<std::unique_ptr<View>>
  getStartViews(llvm::MCInstPrinter &IP, llvm::ArrayRef<llvm::MCInst> Insts);
  /// Return a vector of Views that will be added after the InstructionInfoView.
  /// \param IP Instruction printer used by the returned views.
  /// \param Insts Instructions in the region being analyzed.
  /// \return Target-specific Views to insert after InstructionInfoView.
  virtual std::vector<std::unique_ptr<View>>
  getPostInstrInfoViews(llvm::MCInstPrinter &IP,
                        llvm::ArrayRef<llvm::MCInst> Insts);
  /// Return a vector of Views that will be added after all other Views.
  /// \param IP Instruction printer used by the returned views.
  /// \param Insts Instructions in the region being analyzed.
  /// \return Target-specific Views to insert at the end of the view list.
  virtual std::vector<std::unique_ptr<View>>
  getEndViews(llvm::MCInstPrinter &IP, llvm::ArrayRef<llvm::MCInst> Insts);
};

/// Target-specific instrumentation data attached to instructions.
class Instrument {
  /// The description of Instrument kind
  const StringRef Desc;

  /// The instrumentation data
  const StringRef Data;

public:
  /// Construct an instrument with the given kind and data.
  /// \param Desc Instrument kind description.
  /// \param Data Instrument-specific data payload.
  Instrument(StringRef Desc, StringRef Data) : Desc(Desc), Data(Data) {}

  /// Construct an empty instrument with no description or data.
  Instrument() : Instrument("", "") {}

  /// Destroy this instrument.
  virtual ~Instrument() = default;

  /// Return the instrument kind description.
  /// \return The instrument kind description string.
  StringRef getDesc() const { return Desc; }
  /// Return the instrument-specific data payload.
  /// \return The instrument-specific data payload string.
  StringRef getData() const { return Data; }
};

/// Instrument that encodes an explicit instruction latency override.
class LatencyInstrument : public Instrument {
  std::optional<unsigned> Latency;

public:
  /// Instrument kind name used for latency overrides.
  LLVM_ABI static const StringRef DESC_NAME;
  /// Construct a latency instrument by parsing \p Data as an unsigned value.
  /// \param Data Decimal latency string; empty or invalid leaves no value set.
  LatencyInstrument(StringRef Data) : Instrument(DESC_NAME, Data) {
    // Skip spaces and tabs.
    Data = Data.trim();
    if (Data.empty()) // Empty description. Bail out.
      return;
    unsigned L = 0;
    if (!Data.getAsInteger(10, L))
      Latency = L;
  }

  /// Return true if a latency value was successfully parsed.
  /// \return True if a latency value is available.
  bool hasValue() const { return bool(Latency); }
  /// Return the parsed latency value.
  /// \return The explicit latency override.
  unsigned getLatency() const { return *Latency; }
};

/// Unique ownership wrapper for a dynamically allocated Instrument.
using UniqueInstrument = std::unique_ptr<Instrument>;

/// Optional target customization of scheduling class ID resolution.
///
/// This class allows targets to optionally customize the logic that resolves
/// scheduling class IDs. Targets can use information encoded in Instrument
/// objects to make more informed scheduling decisions.
class LLVM_ABI InstrumentManager {
protected:
  /// Subtarget information for the target being simulated.
  const MCSubtargetInfo &STI;
  /// Instruction info used when resolving scheduling class IDs.
  const MCInstrInfo &MCII;
  /// When false, instrument processing is ignored.
  bool EnableInstruments;

public:
  /// Construct an instrument manager for the given subtarget.
  /// \param STI Subtarget information for the target being simulated.
  /// \param MCII Instruction info used when resolving scheduling class IDs.
  /// \param EnableInstruments Whether instrument processing is enabled.
  InstrumentManager(const MCSubtargetInfo &STI, const MCInstrInfo &MCII,
                    bool EnableInstruments = true)
      : STI(STI), MCII(MCII), EnableInstruments(EnableInstruments) {};

  /// Destroy this instrument manager.
  virtual ~InstrumentManager() = default;

  /// Returns true if llvm-mca should ignore instruments.
  /// \return True if instrument processing is disabled.
  virtual bool shouldIgnoreInstruments() const { return !EnableInstruments; }

  /// Return true if instruments of kind \p Type are supported.
  ///
  /// Returns true if this supports processing Instrument with
  /// Instrument.Desc equal to Type.
  /// \param Type Instrument kind description to check.
  /// \return True if instruments with description \p Type are supported.
  virtual bool supportsInstrumentType(StringRef Type) const;

  /// Allocate an Instrument and return ownership of it.
  ///
  /// Allocate an Instrument, and return a unique pointer to it. This function
  /// may be useful to create instruments coming from comments in the assembly.
  /// See createInstruments to create Instruments from MCInst.
  /// \param Desc Instrument kind description.
  /// \param Data Instrument-specific data payload.
  /// \return Ownership of the newly allocated Instrument.
  virtual UniqueInstrument createInstrument(StringRef Desc, StringRef Data);

  /// Create instruments associated with the given MCInst.
  ///
  /// Return a list of unique pointers to Instruments, where each Instrument
  /// is allocated by this function. See createInstrument to create Instrument
  /// from a description and data.
  /// \param Inst Instruction from which instruments may be derived.
  /// \return Unique pointers to instruments derived from \p Inst.
  virtual SmallVector<UniqueInstrument> createInstruments(const MCInst &Inst);

  /// Return a scheduling class ID for \p MCI given active instruments.
  ///
  /// Given an MCInst and a vector of Instrument, a target can
  /// return a SchedClassID. This can be used by a subtarget to return a
  /// PseudoInstruction SchedClassID instead of the one that belongs to the
  /// BaseInstruction This can be useful when a BaseInstruction does not convey
  /// the correct scheduling information without additional data. By default,
  /// it returns the SchedClassID that belongs to MCI.
  /// \param MCII Instruction info used to look up scheduling class IDs.
  /// \param MCI Instruction whose scheduling class is being resolved.
  /// \param IVec Active instruments that may influence the result.
  /// \return The resolved SchedClassID for \p MCI.
  virtual unsigned getSchedClassID(const MCInstrInfo &MCII, const MCInst &MCI,
                                   const SmallVector<Instrument *> &IVec) const;

  /// Return true if instruments can modify an instruction description.
  /// \param IVec Active instruments to inspect.
  /// \return True if any instrument in \p IVec can customize the description.
  virtual bool canCustomize(const ArrayRef<Instrument *> IVec) const;

  /// Customize an instruction description using active instruments.
  /// \param IVec Active instruments that may modify \p Desc.
  /// \param Desc Instruction description to customize in place.
  virtual void customize(const ArrayRef<Instrument *> IVec,
                         llvm::mca::InstrDesc &Desc) const;
};

} // namespace mca
} // namespace llvm

#endif /* LLVM_MCA_CUSTOMBEHAVIOUR_H */
