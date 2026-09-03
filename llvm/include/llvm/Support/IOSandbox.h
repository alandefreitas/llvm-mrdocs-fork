//===- IOSandbox.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_IOSANDBOX_H
#define LLVM_SUPPORT_IOSANDBOX_H

#if defined(LLVM_ENABLE_IO_SANDBOX) && LLVM_ENABLE_IO_SANDBOX

#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/SaveAndRestore.h"

namespace llvm {
/// Host system utilities.
namespace sys {
/// Thread-local controls for detecting unexpected host filesystem IO.
namespace sandbox {
inline LLVM_THREAD_LOCAL bool Enabled = false;
/// RAII guard that temporarily changes whether the IO sandbox is enabled.
struct [[nodiscard, maybe_unused]] ScopedSetting {
  SaveAndRestore<bool> Impl;
};
/// Return an RAII guard that enables the IO sandbox on this thread.
///
/// \return An RAII guard that enables the sandbox for this thread.
inline ScopedSetting scopedEnable() { return {{Enabled, true}}; }
/// Return an RAII guard that disables the IO sandbox on this thread.
///
/// \return An RAII guard that disables the sandbox for this thread.
inline ScopedSetting scopedDisable() { return {{Enabled, false}}; }
/// Report a fatal error if the IO sandbox is currently enabled.
inline void violationIfEnabled() {
  if (Enabled)
    reportFatalInternalError("IO sandbox violation");
}
} // namespace sandbox
} // namespace sys
} // namespace llvm

#else

namespace llvm {
/// Host system utilities.
namespace sys {
/// Thread-local controls for detecting unexpected host filesystem IO.
namespace sandbox {
/// RAII guard that temporarily changes whether the IO sandbox is enabled.
struct [[nodiscard, maybe_unused]] ScopedSetting {};
/// Return an RAII guard that enables the IO sandbox on this thread.
///
/// \return An RAII guard that enables the sandbox for this thread.
inline ScopedSetting scopedEnable() { return {}; }
/// Return an RAII guard that disables the IO sandbox on this thread.
///
/// \return An RAII guard that disables the sandbox for this thread.
inline ScopedSetting scopedDisable() { return {}; }
/// Report a fatal error if the IO sandbox is currently enabled.
inline void violationIfEnabled() {}
} // namespace sandbox
} // namespace sys
} // namespace llvm

#endif

#endif
