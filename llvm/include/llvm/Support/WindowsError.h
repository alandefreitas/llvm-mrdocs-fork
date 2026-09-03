//===-- WindowsError.h - Support for mapping windows errors to posix-------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_WINDOWSERROR_H
#define LLVM_SUPPORT_WINDOWSERROR_H

#include "llvm/Support/Compiler.h"
#include <system_error>

namespace llvm {
/// Map the last Windows error to a portable \c std::error_code.
///
/// \return A portable \c std::error_code for the value of GetLastError().
LLVM_ABI std::error_code mapLastWindowsError();

/// Map a Windows error value to a portable \c std::error_code.
///
/// \param EV Windows error value to convert (typically from GetLastError()).
/// \return A portable \c std::error_code corresponding to \p EV.
LLVM_ABI std::error_code mapWindowsError(unsigned EV);
}

#endif
