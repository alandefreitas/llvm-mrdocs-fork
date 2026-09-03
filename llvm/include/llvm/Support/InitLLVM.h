//===- InitLLVM.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_INITLLVM_H
#define LLVM_SUPPORT_INITLLVM_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/PrettyStackTrace.h"
#include <optional>

namespace llvm {
/// RAII helper that performs one-time LLVM tool process initialization.
///
/// The \c main() functions in typical LLVM tools start with InitLLVM which does
/// the following one-time initializations:
///
///  1. Setting up a signal handler so that pretty stack trace is printed out
///     if a process crashes. A signal handler that exits when a failed write to
///     a pipe occurs may optionally be installed: this is on-by-default.
///
///  2. Set up the global new-handler which is called when a memory allocation
///     attempt fails.
///
///  3. If running on Windows, obtain command line arguments using a
///     multibyte character-aware API and convert arguments into UTF-8
///     encoding, so that you can assume that command line arguments are
///     always encoded in UTF-8 on any platform.
///
/// InitLLVM calls llvm_shutdown() on destruction, which cleans up
/// ManagedStatic objects.
class InitLLVM {
public:
  /// Initialize LLVM process support using \p Argc and \p Argv.
  ///
  /// \param Argc Argument count; may be updated on Windows when arguments are
  /// re-encoded to UTF-8.
  /// \param Argv Argument vector; may be replaced with UTF-8 encoded arguments
  /// on Windows.
  /// \param InstallPipeSignalExitHandler If true (default), install a handler
  /// that exits on a failed write to a pipe (SIGPIPE).
  /// \param NeedsPOSIXUtilitySignalHandling If true, register POSIX utility
  /// signal handling when installing signal handlers (non-Windows).
  LLVM_ABI InitLLVM(int &Argc, const char **&Argv,
                    bool InstallPipeSignalExitHandler = true,
                    bool NeedsPOSIXUtilitySignalHandling = false);
  /// Initialize LLVM process support using a non-const \p Argv.
  ///
  /// Forwards to the \c const char ** overload.
  ///
  /// \param Argc Argument count; may be updated on Windows when arguments are
  /// re-encoded to UTF-8.
  /// \param Argv Argument vector; may be replaced with UTF-8 encoded arguments
  /// on Windows.
  /// \param InstallPipeSignalExitHandler If true (default), install a handler
  /// that exits on a failed write to a pipe (SIGPIPE).
  /// \param NeedsPOSIXUtilitySignalHandling If true, register POSIX utility
  /// signal handling when installing signal handlers (non-Windows).
  InitLLVM(int &Argc, char **&Argv, bool InstallPipeSignalExitHandler = true,
           bool NeedsPOSIXUtilitySignalHandling = false)
      : InitLLVM(Argc, const_cast<const char **&>(Argv),
                 InstallPipeSignalExitHandler,
                 NeedsPOSIXUtilitySignalHandling) {}

  /// Shut down LLVM and clean up ManagedStatic objects.
  LLVM_ABI ~InitLLVM();

private:
  BumpPtrAllocator Alloc;
  SmallVector<const char *, 0> Args;
  std::optional<PrettyStackTraceProgram> StackPrinter;
};
} // namespace llvm

#endif
