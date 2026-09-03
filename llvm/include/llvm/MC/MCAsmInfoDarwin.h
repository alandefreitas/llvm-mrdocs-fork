//===- MCAsmInfoDarwin.h - Darwin asm properties ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines target asm properties related what form asm statements
// should take in general on Darwin-based targets
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCASMINFODARWIN_H
#define LLVM_MC_MCASMINFODARWIN_H

#include "llvm/MC/MCAsmInfo.h"

namespace llvm {

/// Target asm properties for Darwin-based targets.
class LLVM_ABI MCAsmInfoDarwin : public MCAsmInfo {
public:
  /// Construct Darwin asm info using the given target options.
  /// @param Options Target MC options to retain for later queries.
  explicit MCAsmInfoDarwin(const MCTargetOptions &Options);
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
  /// @return True if alignment should fill with optimized nops; false to use zeros.
  bool useCodeAlign(const MCSection &Sec) const final;

  /// True if the section is atomized using the symbols in it.
  /// This is false if the section is atomized based on its contents (MachO' __TEXT,__cstring for
  /// example).
  /// @param Section Section whose atomization policy is queried.
  /// @return True if the section is atomized by symbols; false if by contents.
  static bool isSectionAtomizableBySymbols(const MCSection &Section);
};

} // end namespace llvm

#endif // LLVM_MC_MCASMINFODARWIN_H
