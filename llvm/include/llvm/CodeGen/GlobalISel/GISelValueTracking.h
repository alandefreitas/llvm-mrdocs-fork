//===- llvm/CodeGen/GlobalISel/GISelValueTracking.h -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// Provides analysis for querying information about KnownBits during GISel
/// passes.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALISEL_GISELVALUETRACKING_H
#define LLVM_CODEGEN_GLOBALISEL_GISELVALUETRACKING_H

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/CodeGen/GlobalISel/GISelChangeObserver.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/PassManager.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/KnownFPClass.h"

namespace llvm {

class TargetLowering;
class DataLayout;

/// Analysis that computes KnownBits and related facts for GlobalISel.
///
/// Clients query known-zero/one bits, sign bits, alignment, shift amounts, and
/// floating-point class information for registers in a MachineFunction.
class LLVM_ABI GISelValueTracking : public GISelChangeObserver {
  MachineFunction &MF;
  MachineRegisterInfo &MRI;
  const TargetLowering &TL;
  const DataLayout &DL;
  unsigned MaxDepth;

  void computeKnownBitsMin(Register Src0, Register Src1, KnownBits &Known,
                           const APInt &DemandedElts, unsigned Depth = 0);

  unsigned computeNumSignBitsMin(Register Src0, Register Src1,
                                 const APInt &DemandedElts, unsigned Depth = 0);

  void computeKnownFPClass(Register R, KnownFPClass &Known,
                           FPClassTest InterestedClasses, unsigned Depth);

  void computeKnownFPClassForFPTrunc(const MachineInstr &MI,
                                     const APInt &DemandedElts,
                                     FPClassTest InterestedClasses,
                                     KnownFPClass &Known, unsigned Depth);

  void computeKnownFPClass(Register R, const APInt &DemandedElts,
                           FPClassTest InterestedClasses, KnownFPClass &Known,
                           unsigned Depth);

public:
  /// Construct value tracking for machine function \p MF.
  ///
  /// \param MF Machine function whose registers are analyzed.
  /// \param MaxDepth Maximum recursion depth for known-bits queries.
  GISelValueTracking(MachineFunction &MF, unsigned MaxDepth = 6);
  /// Destroy the value tracking analysis.
  ~GISelValueTracking() override = default;

  /// Return the machine function being analyzed.
  ///
  /// \return Machine function whose registers are analyzed.
  const MachineFunction &getMachineFunction() const { return MF; }

  /// Return the data layout for the machine function.
  ///
  /// \return Data layout associated with the machine function.
  const DataLayout &getDataLayout() const { return DL; }

  /// Compute known bits for register \p R into \p Known.
  ///
  /// \param R Register whose known bits are computed.
  /// \param Known Output known-bits result.
  /// \param DemandedElts Vector elements that must be considered.
  /// \param Depth Current recursion depth for the query.
  void computeKnownBitsImpl(Register R, KnownBits &Known,
                            const APInt &DemandedElts, unsigned Depth = 0);

  /// Compute the number of sign bits for register \p R.
  ///
  /// \param R Register whose sign bits are counted.
  /// \param DemandedElts Vector elements that must be considered.
  /// \param Depth Current recursion depth for the query.
  /// \return Number of replicated sign bits known for \p R.
  unsigned computeNumSignBits(Register R, const APInt &DemandedElts,
                              unsigned Depth = 0);
  /// Compute the number of sign bits for register \p R.
  ///
  /// Demands every fixed-vector element, or the scalar value.
  ///
  /// \param R Register whose sign bits are counted.
  /// \param Depth Current recursion depth for the query.
  /// \return Number of replicated sign bits known for \p R.
  unsigned computeNumSignBits(Register R, unsigned Depth = 0);

  /// Return the known bits for register \p R.
  ///
  /// \param R Register whose known bits are queried.
  /// \return Known zero and one bits for \p R.
  KnownBits getKnownBits(Register R);
  /// Return the known bits for register \p R under \p DemandedElts.
  ///
  /// \param R Register whose known bits are queried.
  /// \param DemandedElts Vector elements that must be considered.
  /// \param Depth Current recursion depth for the query.
  /// \return Known zero and one bits for \p R.
  KnownBits getKnownBits(Register R, const APInt &DemandedElts,
                         unsigned Depth = 0);

  /// Return the known bits for the first definition of \p MI.
  ///
  /// \param MI Instruction whose first defined register is queried.
  /// \return Known zero and one bits for the first definition.
  KnownBits getKnownBits(MachineInstr &MI);
  /// Return the known-zero bits for register \p R.
  ///
  /// \param R Register whose known-zero mask is queried.
  /// \return Bits of \p R that are known to be zero.
  APInt getKnownZeroes(Register R);
  /// Return the known-one bits for register \p R.
  ///
  /// \param R Register whose known-one mask is queried.
  /// \return Bits of \p R that are known to be one.
  APInt getKnownOnes(Register R);

  /// Return true if Val & Mask is known to be zero.
  ///
  /// We use this predicate to simplify operations downstream. Mask is known to
  /// be zero for bits that Val cannot have.
  ///
  /// \param Val Register whose value is masked.
  /// \param Mask Bits that must be proven zero in \p Val.
  /// \return True if every bit set in \p Mask is known zero in \p Val.
  bool maskedValueIsZero(Register Val, const APInt &Mask) {
    return Mask.isSubsetOf(getKnownBits(Val).Zero);
  }

  /// Return true if the sign bit of Op is known to be zero.
  ///
  /// We use this predicate to simplify operations downstream.
  ///
  /// \param Op Register whose sign bit is tested.
  /// \return True if the sign bit of \p Op is known zero.
  bool signBitIsZero(Register Op);

  /// Return true if the value defined by \p R is provably never zero.
  ///
  /// Demands every fixed-vector element, or the scalar value to be non-zero.
  ///
  /// \param R Register whose value is tested for non-zero.
  /// \param Depth Current recursion depth for the query.
  /// \return True if \p R is known never zero.
  bool isKnownNeverZero(Register R, unsigned Depth = 0);
  /// Return true if the value defined by \p R is provably never zero.
  ///
  /// \p DemandedElts selects the vector elements that must be proven nonzero.
  /// For scalar values this is a one-bit mask.
  ///
  /// \param R Register whose value is tested for non-zero.
  /// \param DemandedElts Vector elements that must be proven nonzero.
  /// \param Depth Current recursion depth for the query.
  /// \return True if the demanded elements of \p R are known never zero.
  bool isKnownNeverZero(Register R, const APInt &DemandedElts,
                        unsigned Depth = 0);

  /// Update \p Known with low zero bits implied by \p Alignment.
  ///
  /// \param Known Known-bits result to update with alignment zeroes.
  /// \param Alignment Pointer alignment that implies low zero bits.
  static void computeKnownBitsForAlignment(KnownBits &Known, Align Alignment) {
    // The low bits are known zero if the pointer is aligned.
    Known.Zero.setLowBits(Log2(Alignment));
  }

  /// Return the known alignment for the pointer-like value \p R.
  ///
  /// \param R Pointer-like register whose alignment is queried.
  /// \param Depth Current recursion depth for the query.
  /// \return Known alignment of \p R.
  Align computeKnownAlignment(Register R, unsigned Depth = 0);

  /// Return the valid constant shift-amount range for shift operand \p R.
  ///
  /// If a G_SHL/G_ASHR/G_LSHR node with shift operand \p R has shift amounts
  /// that are all less than the element bit-width of the shift node, return the
  /// valid constant range.
  ///
  /// \param R Shift-amount register of a G_SHL/G_ASHR/G_LSHR.
  /// \param DemandedElts Vector elements that must be considered.
  /// \param Depth Current recursion depth for the query.
  /// \return Constant range of valid shift amounts, or std::nullopt.
  std::optional<ConstantRange>
  getValidShiftAmountRange(Register R, const APInt &DemandedElts,
                           unsigned Depth);

  /// Return the minimum valid shift amount for shift operand \p R.
  ///
  /// If a G_SHL/G_ASHR/G_LSHR node with shift operand \p R has shift amounts
  /// that are all less than the element bit-width of the shift node, return the
  /// minimum possible value.
  ///
  /// \param R Shift-amount register of a G_SHL/G_ASHR/G_LSHR.
  /// \param DemandedElts Vector elements that must be considered.
  /// \param Depth Current recursion depth for the query.
  /// \return Minimum valid shift amount, or std::nullopt.
  std::optional<uint64_t> getValidMinimumShiftAmount(Register R,
                                                     const APInt &DemandedElts,
                                                     unsigned Depth = 0);

  /// Determine which floating-point classes are valid for \p R.
  ///
  /// This function is defined on values with floating-point type, values
  /// vectors of floating-point type, and arrays of floating-point type.
  ///
  /// \p InterestedClasses is a compile time optimization hint for which
  /// floating point classes should be queried. Queries not specified in \p
  /// InterestedClasses should be reliable if they are determined during the
  /// query.
  ///
  /// \param R Floating-point register whose class is queried.
  /// \param DemandedElts Vector elements that must be considered.
  /// \param InterestedClasses FP classes the caller cares about.
  /// \param Depth Current recursion depth for the query.
  /// \return Known floating-point class information for \p R.
  KnownFPClass computeKnownFPClass(Register R, const APInt &DemandedElts,
                                   FPClassTest InterestedClasses,
                                   unsigned Depth);

  /// Determine which floating-point classes are valid for \p R.
  ///
  /// Demands every fixed-vector element, or the scalar value.
  ///
  /// \param R Floating-point register whose class is queried.
  /// \param InterestedClasses FP classes the caller cares about.
  /// \param Depth Current recursion depth for the query.
  /// \return Known floating-point class information for \p R.
  KnownFPClass computeKnownFPClass(Register R,
                                   FPClassTest InterestedClasses = fcAllFlags,
                                   unsigned Depth = 0);

  /// Compute known FP class accounting for fast-math flags at the use.
  ///
  /// Wrapper to account for known fast math flags at the use instruction.
  ///
  /// \param R Floating-point register whose class is queried.
  /// \param DemandedElts Vector elements that must be considered.
  /// \param Flags Fast-math flags from the use instruction.
  /// \param InterestedClasses FP classes the caller cares about.
  /// \param Depth Current recursion depth for the query.
  /// \return Known floating-point class information for \p R.
  KnownFPClass computeKnownFPClass(Register R, const APInt &DemandedElts,
                                   uint32_t Flags,
                                   FPClassTest InterestedClasses,
                                   unsigned Depth);

  /// Compute known FP class accounting for fast-math flags at the use.
  ///
  /// Demands every fixed-vector element, or the scalar value.
  ///
  /// \param R Floating-point register whose class is queried.
  /// \param Flags Fast-math flags from the use instruction.
  /// \param InterestedClasses FP classes the caller cares about.
  /// \param Depth Current recursion depth for the query.
  /// \return Known floating-point class information for \p R.
  KnownFPClass computeKnownFPClass(Register R, uint32_t Flags,
                                   FPClassTest InterestedClasses,
                                   unsigned Depth);

  /// Return true if \p Val can be assumed to never be a NaN.
  ///
  /// If \p SNaN is true, this returns whether \p Val can be assumed to never be
  /// a signaling NaN.
  ///
  /// \param Val Floating-point register tested for NaN.
  /// \param SNaN When true, only signaling NaNs are excluded.
  /// \return True if \p Val is known never NaN (or never SNaN when requested).
  bool isKnownNeverNaN(Register Val, bool SNaN = false);

  /// Return true if \p Val can be assumed to never be a signaling NaN.
  ///
  /// \param Val Floating-point register tested for signaling NaN.
  /// \return True if \p Val is known never to be a signaling NaN.
  bool isKnownNeverSNaN(Register Val) { return isKnownNeverNaN(Val, true); }

  /// Observe that instruction \p MI is about to be erased.
  ///
  /// No-op for the non-caching implementation.
  ///
  /// \param MI Instruction that is about to be erased.
  void erasingInstr(MachineInstr &MI) override {}
  /// Observe that instruction \p MI was created and inserted.
  ///
  /// No-op for the non-caching implementation.
  ///
  /// \param MI Instruction that was created and inserted.
  void createdInstr(MachineInstr &MI) override {}
  /// Observe that instruction \p MI is about to be mutated.
  ///
  /// No-op for the non-caching implementation.
  ///
  /// \param MI Instruction that is about to be mutated.
  void changingInstr(MachineInstr &MI) override {}
  /// Observe that instruction \p MI was mutated.
  ///
  /// No-op for the non-caching implementation.
  ///
  /// \param MI Instruction that was mutated.
  void changedInstr(MachineInstr &MI) override {}

protected:
  /// Return the maximum recursion depth for known-bits queries.
  ///
  /// \return Maximum recursion depth used by known-bits queries.
  unsigned getMaxDepth() const { return MaxDepth; }
};

/// Legacy MachineFunctionPass that owns a GISelValueTracking instance.
///
/// To use KnownBitsInfo analysis in a pass:
/// KnownBitsInfo &Info = getAnalysis<GISelValueTrackingInfoAnalysis>().get(MF);
/// Add to observer if the Info is caching.
/// WrapperObserver.addObserver(Info);
///
/// Eventually add other features such as caching/ser/deserializing to MIR etc.
/// Those implementations can derive from GISelValueTracking and override
/// computeKnownBitsImpl.
class LLVM_ABI GISelValueTrackingAnalysisLegacy : public MachineFunctionPass {
  std::unique_ptr<GISelValueTracking> Info;

public:
  /// Pass identification, replacement for typeid.
  static char ID;
  /// Construct the legacy GISel value-tracking analysis pass.
  GISelValueTrackingAnalysisLegacy() : MachineFunctionPass(ID) {}
  /// Return the value-tracking analysis for machine function \p MF.
  ///
  /// \param MF Machine function whose GISelValueTracking is requested.
  /// \return Value-tracking analysis associated with \p MF.
  GISelValueTracking &get(MachineFunction &MF);
  /// Declare the analyses required and preserved by this pass.
  ///
  /// \param AU Analysis usage to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  /// Compute value-tracking info for machine function \p MF.
  ///
  /// \param MF Machine function to analyze.
  /// \return False; this analysis does not modify \p MF.
  bool runOnMachineFunction(MachineFunction &MF) override;
  /// Release memory held by the owned GISelValueTracking instance.
  void releaseMemory() override { Info.reset(); }
};

/// Analysis that computes GISelValueTracking for a MachineFunction.
class GISelValueTrackingAnalysis
    : public AnalysisInfoMixin<GISelValueTrackingAnalysis> {
  friend AnalysisInfoMixin<GISelValueTrackingAnalysis>;
  LLVM_ABI static AnalysisKey Key;

public:
  /// Result type produced by this analysis.
  using Result = GISelValueTracking;

  /// Run the value-tracking analysis on machine function \p MF.
  ///
  /// \param MF Machine function to analyze.
  /// \param MFAM Machine function analysis manager.
  /// \return Value-tracking analysis for \p MF.
  LLVM_ABI Result run(MachineFunction &MF,
                      MachineFunctionAnalysisManager &MFAM);
};

/// Printer pass for GISelValueTracking analysis results.
class GISelValueTrackingPrinterPass
    : public RequiredPassInfoMixin<GISelValueTrackingPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes to \p OS.
  ///
  /// \param OS Output stream for the value-tracking dump.
  GISelValueTrackingPrinterPass(raw_ostream &OS) : OS(OS) {}

  /// Print GISelValueTracking results for \p MF.
  ///
  /// \param MF Machine function whose value-tracking info is printed.
  /// \param MFAM Analysis manager providing GISelValueTrackingAnalysis.
  /// \return All analyses preserved; this pass does not transform \p MF.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};
} // namespace llvm

#endif // LLVM_CODEGEN_GLOBALISEL_GISELVALUETRACKING_H
