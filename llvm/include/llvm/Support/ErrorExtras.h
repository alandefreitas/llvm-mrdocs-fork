//===- llvm/Support/ErrorExtras.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_ERROREXTRAS_H
#define LLVM_SUPPORT_ERROREXTRAS_H

#include "llvm/Support/Error.h"
#include "llvm/Support/FormatVariadic.h"

namespace llvm {

// LLVM formatv versions of llvm::createStringError

/// Create a StringError from error code \p EC and formatv-style \p Fmt.
///
/// Like \c createStringError, but formats the message with \c formatv.
///
/// \param EC Error code stored in the StringError.
/// \param Fmt Formatv-style format string for the message.
/// \param Vals Format arguments for \p Fmt.
/// \return A StringError with code \p EC and the formatv-formatted message.
template <typename... Ts>
inline Error createStringErrorV(std::error_code EC, char const *Fmt,
                                Ts &&...Vals) {
  return make_error<StringError>(formatv(Fmt, std::forward<Ts>(Vals)...).str(),
                                 EC, true);
}

/// Create a StringError with an inconvertible error code and formatv-style \p Fmt.
///
/// \param Fmt Formatv-style format string for the message.
/// \param Vals Format arguments for \p Fmt.
/// \return A StringError with an inconvertible error code and the formatv-formatted message.
template <typename... Ts>
inline Error createStringErrorV(char const *Fmt, Ts &&...Vals) {
  return createStringErrorV(llvm::inconvertibleErrorCode(), Fmt,
                            std::forward<Ts>(Vals)...);
}

} // namespace llvm

#endif // LLVM_SUPPORT_ERROREXTRAS_H
