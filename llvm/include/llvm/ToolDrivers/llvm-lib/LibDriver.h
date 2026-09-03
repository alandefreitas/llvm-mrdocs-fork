//===- llvm-lib/LibDriver.h - lib.exe-compatible driver ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines an interface to a lib.exe-compatible driver that also understands
// bitcode files. Used by llvm-lib and lld-link /lib.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLDRIVERS_LLVM_LIB_LIBDRIVER_H
#define LLVM_TOOLDRIVERS_LLVM_LIB_LIBDRIVER_H

#include "llvm/Support/Compiler.h"

namespace llvm {
template <typename T> class ArrayRef;

/// Entry point for the lib.exe-compatible driver used by llvm-lib and
/// lld-link /lib.
///
/// \param ARgs Command-line arguments, including the program name as the
///        first element.
/// \return Zero on success; a non-zero exit status on failure.
LLVM_ABI int libDriverMain(ArrayRef<const char *> ARgs);
}

#endif
