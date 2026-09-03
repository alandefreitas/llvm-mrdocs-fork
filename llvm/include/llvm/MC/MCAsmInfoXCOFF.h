//===- MCAsmInfoXCOFF.h - XCOFF asm properties ----------------- *- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCASMINFOXCOFF_H
#define LLVM_MC_MCASMINFOXCOFF_H

#include "llvm/MC/MCAsmInfo.h"

namespace llvm {

/// Target asm properties for XCOFF-based targets.
class LLVM_ABI MCAsmInfoXCOFF : public MCAsmInfo {
protected:
  /// Construct XCOFF asm info using the given target options.
  /// @param Options Target MC options to retain for later queries.
  MCAsmInfoXCOFF(const MCTargetOptions &Options);
  /// Print the assembler text that switches to \p Section.
  /// @param Section Section to switch to.
  /// @param Subsection Optional subsection number.
  /// @param T Target triple used for target-specific syntax.
  /// @param OS Output stream that receives the directive text.
  void printSwitchToSection(const MCSection &Section, uint32_t Subsection,
                            const Triple &T, raw_ostream &OS) const final;
  /// Return true if a `.align` directive should use optimized nops to fill
  /// instead of 0s.
  /// @param Sec Section whose alignment fill policy is queried.
  /// @return True if alignment padding should use optimized nops.
  bool useCodeAlign(const MCSection &Sec) const final;
};

} // end namespace llvm

#endif // LLVM_MC_MCASMINFOXCOFF_H
