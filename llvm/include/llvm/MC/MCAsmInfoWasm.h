//===-- llvm/MC/MCAsmInfoWasm.h - Wasm Asm info -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCASMINFOWASM_H
#define LLVM_MC_MCASMINFOWASM_H

#include "llvm/MC/MCAsmInfo.h"

namespace llvm {
/// Target asm properties for Wasm-based targets.
class LLVM_ABI MCAsmInfoWasm : public MCAsmInfo {
  void printSwitchToSection(const MCSection &, uint32_t, const Triple &,
                            raw_ostream &) const final;

protected:
  /// Construct Wasm asm info using the given target options.
  /// @param Options Target MC options to retain for later queries.
  MCAsmInfoWasm(const MCTargetOptions &Options);
};
} // namespace llvm

#endif
