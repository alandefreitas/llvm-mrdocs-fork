//===- llvm/Support/Errno.h - Portable+convenient errno handling -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares some portable and convenient functions to deal with errno.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_ERRNO_H
#define LLVM_SUPPORT_ERRNO_H

#include "llvm/Support/Compiler.h"
#include <cerrno>
#include <string>

namespace llvm {
namespace sys {

/// Returns a string representation of the errno value.
///
/// Uses whatever thread-safe variant of strerror() is available. Be sure to
/// call this immediately after the function that set errno, or errno may have
/// been overwritten by an intervening call.
///
/// \returns A string describing the current errno value.
LLVM_ABI std::string StrError();

/// Like the no-argument version above, but uses \p errnum instead of errno.
///
/// \param errnum Error number to convert to a string.
/// \returns A string describing the given error number.
LLVM_ABI std::string StrError(int errnum);

/// Invokes \p F with \p As, retrying when interrupted by a signal.
///
/// Clears errno, calls \p F, and repeats while the result equals \p Fail and
/// errno is EINTR.
///
/// \param Fail Value that indicates the call failed.
/// \param F Callable to invoke.
/// \param As Arguments forwarded to \p F.
/// \returns The result of the last call to \p F that did not fail with EINTR.
template <typename FailT, typename Fun, typename... Args>
inline decltype(auto) RetryAfterSignal(const FailT &Fail, const Fun &F,
                                       const Args &... As) {
  decltype(F(As...)) Res;
  do {
    errno = 0;
    Res = F(As...);
  } while (Res == Fail && errno == EINTR);
  return Res;
}

}  // namespace sys
}  // namespace llvm

#endif // LLVM_SUPPORT_ERRNO_H
