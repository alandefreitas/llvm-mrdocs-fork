//===--- ConvertEBCDIC.h - UTF8/EBCDIC CharSet Conversion -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file provides utility functions for converting between EBCDIC-1047 and
/// UTF-8.
///
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_CONVERTEBCDIC_H
#define LLVM_SUPPORT_CONVERTEBCDIC_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include <system_error>

namespace llvm {
/// Utilities for converting between EBCDIC-1047 and UTF-8.
namespace ConverterEBCDIC {
/// Converts a UTF-8 string to EBCDIC-1047.
///
/// \param Source UTF-8 input text to convert.
/// \param Result Empty buffer that receives the EBCDIC-1047 output.
/// \return An error code if \p Source contains an illegal or truncated
/// UTF-8 sequence; a default-constructed error code on success.
LLVM_ABI std::error_code convertToEBCDIC(StringRef Source,
                                         SmallVectorImpl<char> &Result);

/// Converts an EBCDIC-1047 string to UTF-8.
///
/// \param Source EBCDIC-1047 input text to convert.
/// \param Result Empty buffer that receives the UTF-8 output.
LLVM_ABI void convertToUTF8(StringRef Source, SmallVectorImpl<char> &Result);

} // namespace ConverterEBCDIC
} // namespace llvm

#endif // LLVM_SUPPORT_CONVERTEBCDIC_H
