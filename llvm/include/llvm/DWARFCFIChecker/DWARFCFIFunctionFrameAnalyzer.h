//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares CFIFunctionFrameAnalyzer class.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_DWARFCFICHECKER_DWARFCFIFUNCTIONFRAMEANALYZER_H
#define LLVM_DWARFCFICHECKER_DWARFCFIFUNCTIONFRAMEANALYZER_H

#include "DWARFCFIAnalysis.h"
#include "DWARFCFIFunctionFrameReceiver.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

/// Validates Call Frame Information across a stream of function frames.
///
/// This class implements the `CFIFunctionFrameReceiver` interface to validate
/// Call Frame Information in a stream of function frames. For validation, it
/// instantiates a `DWARFCFIAnalysis` for each frame. The errors/warnings are
/// emitted through the `MCContext` instance to the constructor. If a frame
/// finishes without being started or if all the frames are not finished before
/// this classes is destructed, the program fails through an assertion.
class LLVM_ABI CFIFunctionFrameAnalyzer : public CFIFunctionFrameReceiver {
public:
  /// Construct an analyzer that reports CFI issues through \p Context.
  /// \param Context Context used to emit validation errors and warnings.
  /// \param MCII Instruction info used when analyzing each frame.
  CFIFunctionFrameAnalyzer(MCContext &Context, const MCInstrInfo &MCII)
      : CFIFunctionFrameReceiver(Context), MCII(MCII) {}
  /// Destroy the analyzer; asserts that every started frame was finished.
  ~CFIFunctionFrameAnalyzer() override;

  /// Begin analyzing a new function frame with the given prologue CFI.
  /// \param IsEH True when the frame uses EH CFI conventions.
  /// \param Prologue Prologue CFI directives that initialize frame analysis.
  void startFunctionFrame(bool IsEH,
                          ArrayRef<MCCFIInstruction> Prologue) override;
  /// Update the current frame analysis with an instruction and its CFI.
  /// \param Inst Machine instruction to analyze.
  /// \param Directives CFI directives associated with \p Inst.
  void
  emitInstructionAndDirectives(const MCInst &Inst,
                               ArrayRef<MCCFIInstruction> Directives) override;
  /// Finish analyzing the current function frame and pop its analysis state.
  void finishFunctionFrame() override;

private:
  MCInstrInfo const &MCII;
  SmallVector<DWARFCFIAnalysis> UIAs;
};

} // namespace llvm

#endif
