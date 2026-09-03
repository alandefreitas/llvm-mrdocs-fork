//===- llvm/Analysis/DemandedBits.h - Determine demanded bits ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass implements a demanded bits analysis. A demanded bit is one that
// contributes to a result; bits that are not demanded can be either zero or
// one without affecting control or data flow. For example in this sequence:
//
//   %1 = add i32 %x, %y
//   %2 = trunc i32 %1 to i16
//
// Only the lowest 16 bits of %1 are demanded; the rest are removed by the
// trunc.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_DEMANDEDBITS_H
#define LLVM_ANALYSIS_DEMANDEDBITS_H

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class AssumptionCache;
class DominatorTree;
class Function;
class Instruction;
struct KnownBits;
class raw_ostream;
class Use;
class Value;

/// Analysis that computes which bits of each instruction result are demanded.
///
/// A demanded bit is one that contributes to a result; bits that are not
/// demanded can be either zero or one without affecting control or data flow.
class DemandedBits {
public:
  /// Construct demanded-bits analysis for \p F.
  /// @param F Function whose instructions are analyzed.
  /// @param AC Assumption cache used for known-bits queries.
  /// @param DT Dominator tree used for known-bits queries.
  DemandedBits(Function &F, AssumptionCache &AC, DominatorTree &DT) :
    F(F), AC(AC), DT(DT) {}

  /// Return the bits demanded from instruction I.
  ///
  /// For vector instructions individual vector elements are not distinguished:
  /// A bit is demanded if it is demanded for any of the vector elements. The
  /// size of the return value corresponds to the type size in bits of the
  /// scalar type.
  ///
  /// Instructions that do not have integer or vector of integer type are
  /// accepted, but will always produce a mask with all bits set.
  /// @param I Instruction whose demanded-bit mask is requested.
  /// @return Mask of bits demanded from \p I's result.
  LLVM_ABI APInt getDemandedBits(Instruction *I);

  /// Return the bits demanded from use U.
  /// @param U Use whose demanded-bit mask is requested.
  /// @return Mask of bits demanded from \p U.
  LLVM_ABI APInt getDemandedBits(Use *U);

  /// Return true if, during analysis, I could not be reached.
  /// @param I Instruction to test for liveness during analysis.
  /// @return True if \p I was unreachable during analysis.
  LLVM_ABI bool isInstructionDead(Instruction *I);

  /// Return whether this use is dead by means of not having any demanded bits.
  /// @param U Use to test for demanded-bit liveness.
  /// @return True if \p U has no demanded bits.
  LLVM_ABI bool isUseDead(Use *U);

  /// Print demanded-bits analysis results for the function to \p OS.
  /// @param OS Output stream for the printed analysis.
  LLVM_ABI void print(raw_ostream &OS);

  /// Compute alive bits of one addition operand from alive output and known
  /// operand bits
  /// @param OperandNo Operand index (0 for LHS, 1 for RHS).
  /// @param AOut Alive (demanded) bits of the addition result.
  /// @param LHS Known bits of the left-hand operand.
  /// @param RHS Known bits of the right-hand operand.
  /// @return Mask of alive bits for the selected operand.
  LLVM_ABI static APInt determineLiveOperandBitsAdd(unsigned OperandNo,
                                                    const APInt &AOut,
                                                    const KnownBits &LHS,
                                                    const KnownBits &RHS);

  /// Compute alive bits of one subtraction operand from alive output and known
  /// operand bits
  /// @param OperandNo Operand index (0 for LHS, 1 for RHS).
  /// @param AOut Alive (demanded) bits of the subtraction result.
  /// @param LHS Known bits of the left-hand operand.
  /// @param RHS Known bits of the right-hand operand.
  /// @return Mask of alive bits for the selected operand.
  LLVM_ABI static APInt determineLiveOperandBitsSub(unsigned OperandNo,
                                                    const APInt &AOut,
                                                    const KnownBits &LHS,
                                                    const KnownBits &RHS);

private:
  void performAnalysis();
  void determineLiveOperandBits(const Instruction *UserI,
    const Value *Val, unsigned OperandNo,
    const APInt &AOut, APInt &AB,
    KnownBits &Known, KnownBits &Known2, bool &KnownBitsComputed);

  Function &F;
  AssumptionCache &AC;
  DominatorTree &DT;

  bool Analyzed = false;

  // The set of visited instructions (non-integer-typed only).
  SmallPtrSet<Instruction*, 32> Visited;
  DenseMap<Instruction *, APInt> AliveBits;
  // Uses with no demanded bits. If the user also has no demanded bits, the use
  // might not be stored explicitly in this map, to save memory during analysis.
  SmallPtrSet<Use *, 16> DeadUses;
};

/// An analysis that produces \c DemandedBits for a function.
class DemandedBitsAnalysis : public AnalysisInfoMixin<DemandedBitsAnalysis> {
  friend AnalysisInfoMixin<DemandedBitsAnalysis>;

  LLVM_ABI static AnalysisKey Key;

public:
  /// Provide the result type for this analysis pass.
  using Result = DemandedBits;

  /// Run the analysis pass over a function and produce demanded bits
  /// information.
  /// @param F Function to analyze.
  /// @param AM Function analysis manager providing AssumptionAnalysis and
  /// DominatorTreeAnalysis.
  /// @return DemandedBits results for \p F.
  LLVM_ABI DemandedBits run(Function &F, FunctionAnalysisManager &AM);
};

/// Printer pass for DemandedBits
class DemandedBitsPrinterPass
    : public RequiredPassInfoMixin<DemandedBitsPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes demanded-bits results to \p OS.
  /// @param OS Output stream for the printed analysis.
  explicit DemandedBitsPrinterPass(raw_ostream &OS) : OS(OS) {}

  /// Print demanded-bits results for \p F.
  /// @param F Function whose demanded bits are printed.
  /// @param AM Function analysis manager providing DemandedBitsAnalysis.
  /// @return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_DEMANDEDBITS_H
