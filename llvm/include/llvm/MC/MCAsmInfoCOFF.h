//===- MCAsmInfoCOFF.h - COFF asm properties --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCASMINFOCOFF_H
#define LLVM_MC_MCASMINFOCOFF_H

#include "llvm/MC/MCAsmInfo.h"

namespace llvm {

/// Base class for asm properties and features on COFF-based targets.
class LLVM_ABI MCAsmInfoCOFF : public MCAsmInfo {
  virtual void anchor();
  void printSwitchToSection(const MCSection &, uint32_t, const Triple &,
                            raw_ostream &) const final;
  bool useCodeAlign(const MCSection &Sec) const final;

protected:
  /// Construct COFF asm info using the given target options.
  /// @param Options Target MC options to retain for later queries.
  explicit MCAsmInfoCOFF(const MCTargetOptions &Options);
};

/// Asm info for Microsoft COFF targets (MSVC-style assembly).
class LLVM_ABI MCAsmInfoMicrosoft : public MCAsmInfoCOFF {
  void anchor() override;

protected:
  /// Construct Microsoft COFF asm info using the given target options.
  /// @param Options Target MC options to retain for later queries.
  explicit MCAsmInfoMicrosoft(const MCTargetOptions &Options);
};

/// Asm info for GNU COFF targets (MinGW and Cygwin).
class LLVM_ABI MCAsmInfoGNUCOFF : public MCAsmInfoCOFF {
  void anchor() override;

protected:
  /// Construct GNU COFF asm info using the given target options.
  /// @param Options Target MC options to retain for later queries.
  explicit MCAsmInfoGNUCOFF(const MCTargetOptions &Options);
};

} // end namespace llvm

#endif // LLVM_MC_MCASMINFOCOFF_H
