//===- MCAsmInfoGOFF.h - GOFF Asm Info Fields -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines certain target specific asm properties for GOFF (z/OS)
/// based targets.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCASMINFOGOFF_H
#define LLVM_MC_MCASMINFOGOFF_H

#include "llvm/MC/MCAsmInfo.h"

namespace llvm {
/// Target-specific assembly properties for GOFF (z/OS) based targets.
class LLVM_ABI MCAsmInfoGOFF : public MCAsmInfo {
  void printSwitchToSection(const MCSection &, uint32_t, const Triple &,
                            raw_ostream &) const final;

protected:
  /// Construct GOFF assembly info from target options.
  ///
  /// \param Options - Target options used to configure asm emission.
  MCAsmInfoGOFF(const MCTargetOptions &Options);
};
} // end namespace llvm

#endif // LLVM_MC_MCASMINFOGOFF_H
