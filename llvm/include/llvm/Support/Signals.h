//===- llvm/Support/Signals.h - Signal Handling support ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines some helpful functions for dealing with the possibility of
// unix signals occurring while your program is running.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_SIGNALS_H
#define LLVM_SUPPORT_SIGNALS_H

#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>
#include <string>

#if LLVM_ENABLE_DEBUGLOC_TRACKING_ORIGIN
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
namespace llvm {
// Typedefs that are convenient but only used by the stack-trace-collection code
// added if DebugLoc origin-tracking is enabled.
using AddressSet = DenseSet<void *>;
using SymbolizedAddressMap = DenseMap<void *, SmallVector<std::string, 0>>;
} // namespace llvm
#endif

namespace llvm {
class StringRef;
class raw_ostream;

namespace sys {

/// This function runs all the registered interrupt handlers, including the
/// removal of files registered by RemoveFileOnSignal.
LLVM_ABI void RunInterruptHandlers();

/// This function registers signal handlers to ensure that if a signal gets
/// delivered that the named file is removed.
/// Remove a file if a fatal signal occurs.
/// \param Filename Path of the file to remove if a fatal signal occurs.
/// \param ErrMsg Optional string populated with an error message on failure.
/// \return True if registration failed; false on success.
LLVM_ABI bool RemoveFileOnSignal(StringRef Filename,
                                 std::string *ErrMsg = nullptr);

/// This function removes a file from the list of files to be removed on
/// signal delivery.
/// \param Filename Path previously registered with RemoveFileOnSignal.
LLVM_ABI void DontRemoveFileOnSignal(StringRef Filename);

/// When an error signal (such as SIGABRT or SIGSEGV) is delivered to the
/// process, print a stack trace and then exit.
/// Print a stack trace if a fatal signal occurs.
/// \param Argv0 the current binary name, used to find the symbolizer
///        relative to the current binary before searching $PATH; can be
///        StringRef(), in which case we will only search $PATH.
/// \param DisableCrashReporting if \c true, disable the normal crash
///        reporting mechanisms on the underlying operating system.
LLVM_ABI void PrintStackTraceOnErrorSignal(StringRef Argv0,
                                           bool DisableCrashReporting = false);

/// Disable all system dialog boxes that appear when the process crashes.
LLVM_ABI void DisableSystemDialogsOnCrash();

/// Print the stack trace using the given \c raw_ostream object.
/// \param OS Stream to which the stack trace is written.
/// \param Depth refers to the number of stackframes to print. If not
///        specified, the entire frame is printed.
LLVM_ABI void PrintStackTrace(raw_ostream &OS, int Depth = 0);

#if LLVM_ENABLE_DEBUGLOC_TRACKING_ORIGIN
#ifdef NDEBUG
#error DebugLoc origin-tracking should not be enabled in Release builds.
#endif
/// Populates the given array with a stack trace of the current program, up to
/// MaxDepth frames. Returns the number of frames returned, which will be
/// inserted into \p StackTrace from index 0. All entries after the returned
/// depth will be unmodified. NB: This is only intended to be used for
/// introspection of LLVM by Debugify, will not be enabled in release builds,
/// and should not be relied on for other purposes.
template <unsigned long MaxDepth>
int getStackTrace(std::array<void *, MaxDepth> &StackTrace);

/// Takes a set of \p Addresses, symbolizes them and stores the result in the
/// provided \p SymbolizedAddresses map.
/// NB: This is only intended to be used for introspection of LLVM by
/// Debugify, will not be enabled in release builds, and should not be relied
/// on for other purposes.
void symbolizeAddresses(AddressSet &Addresses,
                        SymbolizedAddressMap &SymbolizedAddresses);
#endif

/// Run all registered signal handlers added via \ref AddSignalHandler.
LLVM_ABI void RunSignalHandlers();

/// Callback invoked by \ref AddSignalHandler when an abort/kill signal occurs.
///
/// @param Cookie Opaque user data registered with the handler.
using SignalHandlerCallback = void (*)(void *);

/// Add a function to be called when an abort/kill signal is delivered to the
/// process.
///
/// The handler can have a cookie passed to it to identify what instance of
/// the handler it is. The NeedsPOSIXUtilitySignalHandling argument indicates
/// whether POSIX signal handling semantics are followed, so that the signal
/// handler resignals itself to terminate after handling the signal.
/// \param FnPtr Callback invoked when an abort/kill signal is delivered.
/// \param Cookie Opaque user data passed through to \p FnPtr.
/// \param NeedsPOSIXUtilitySignalHandling If true, follow POSIX utility
///        signal handling so the process resignals itself after the handler.
LLVM_ABI void AddSignalHandler(SignalHandlerCallback FnPtr, void *Cookie,
                               bool NeedsPOSIXUtilitySignalHandling = false);

/// Register a function to be called when the user interrupts the program.
///
/// This function registers a function to be called when the user "interrupts"
/// the program (typically by pressing ctrl-c).  When the user interrupts the
/// program, the specified interrupt function is called instead of the program
/// being killed, and the interrupt function automatically disabled.
///
/// Note that interrupt functions are not allowed to call any non-reentrant
/// functions.  An null interrupt function pointer disables the current
/// installed function.  Note also that the handler may be executed on a
/// different thread on some platforms.
/// \param IF Interrupt function to install, or null to disable the current
///        handler.
LLVM_ABI void SetInterruptFunction(void (*IF)());

/// Registers a function to be called when an "info" signal is delivered to
/// the process.
///
/// On POSIX systems, this will be SIGUSR1; on systems that have it, SIGINFO
/// will also be used (typically ctrl-t).
///
/// Note that signal handlers are not allowed to call any non-reentrant
/// functions.  An null function pointer disables the current installed
/// function.  Note also that the handler may be executed on a different
/// thread on some platforms.
/// \param Handler Function to call on an info signal, or null to disable
///        the current handler.
LLVM_ABI void SetInfoSignalFunction(void (*Handler)());

/// Register a one-shot handler for a failed write to a pipe.
///
/// After the pipe signal is handled once, the handler is unregistered.
///
/// The LLVM signal handling code will not install any handler for the pipe
/// signal unless one is provided with this API (see \ref
/// DefaultOneShotPipeSignalHandler). This handler must be provided before
/// any other LLVM signal handlers are installed: the llvm::InitLLVM
/// constructor has a flag that can simplify this setup.
///
/// Note that the handler is not allowed to call any non-reentrant
/// functions.  A null handler pointer disables the current installed
/// function.  Note also that the handler may be executed on a
/// different thread on some platforms.
/// \param Handler Function to call on a pipe write failure, or null to
///        disable the current handler.
LLVM_ABI void SetOneShotPipeSignalFunction(void (*Handler)());

/// On Unix systems and Windows, this function exits with an "IO error" exit
/// code.
LLVM_ABI void DefaultOneShotPipeSignalHandler();

#ifdef _WIN32
/// Windows does not support signals and this handler must be called manually.
LLVM_ABI void CallOneShotPipeSignalHandler();
#endif

/// Clean up temporary files, dump the stack, run handlers, and create a crash
/// dump after a fatal signal.
///
/// This function does the following:
/// - clean up any temporary files registered with RemoveFileOnSignal()
/// - dump the callstack from the exception context
/// - call any relevant interrupt/signal handlers
/// - create a core/mini dump of the exception context whenever possible
/// \param Context System-specific failure context: the signal type on Unix;
///        the ExceptionContext on Windows.
LLVM_ABI void CleanupOnSignal(uintptr_t Context);

/// Unregister previously installed signal handlers, restoring prior behavior.
LLVM_ABI void unregisterHandlers();
} // namespace sys
} // namespace llvm

#endif
