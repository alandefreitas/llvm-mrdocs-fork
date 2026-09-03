//===- llvm/MC/MCAsmInfoELF.h - ELF Asm info --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCASMINFOELF_H
#define LLVM_MC_MCASMINFOELF_H

#include "llvm/MC/MCAsmInfo.h"

namespace llvm {

/// Target asm properties for ELF-based targets.
class LLVM_ABI MCAsmInfoELF : public MCAsmInfo {
  virtual void anchor();
  MCSection *getStackSection(MCContext &Ctx, bool Exec) const override;
  void printSwitchToSection(const MCSection &, uint32_t, const Triple &,
                            raw_ostream &) const final;
  bool useCodeAlign(const MCSection &Sec) const final;

protected:
  /// Construct ELF asm info using the given target options.
  /// @param Options Target MC options to retain for later queries.
  MCAsmInfoELF(const MCTargetOptions &Options);
};

} // end namespace llvm

#endif // LLVM_MC_MCASMINFOELF_H
