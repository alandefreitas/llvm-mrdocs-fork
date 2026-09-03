//===- DlltoolDriver.h - dlltool.exe-compatible driver ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines an interface to a dlltool.exe-compatible driver.
// Used by llvm-dlltool.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLDRIVERS_LLVM_DLLTOOL_DLLTOOLDRIVER_H
#define LLVM_TOOLDRIVERS_LLVM_DLLTOOL_DLLTOOLDRIVER_H

#include "llvm/Support/Compiler.h"

namespace llvm {
template <typename T> class ArrayRef;

/// Entry point for the dlltool.exe-compatible driver used by llvm-dlltool.
///
/// \param ArgsArr Command-line arguments, including the program name as the
///        first element.
/// \return Zero on success; a non-zero exit status on failure.
LLVM_ABI int dlltoolDriverMain(ArrayRef<const char *> ArgsArr);
} // namespace llvm

#endif
