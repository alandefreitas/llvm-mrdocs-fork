//===- llvm/Support/Process.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// Provides a library for accessing information about this process and other
/// processes on the operating system. Also provides means of spawning
/// subprocess for commands. The design of this library is modeled after the
/// proposed design of the Boost.Process library, and is design specifically to
/// follow the style of standard libraries and potentially become a proposal
/// for a standard library.
///
/// This file declares the llvm::sys::Process class which contains a collection
/// of legacy static interfaces for extracting various information about the
/// current process. The goal is to migrate users of this API over to the new
/// interfaces.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_PROCESS_H
#define LLVM_SUPPORT_PROCESS_H

#include "llvm/Support/Chrono.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Program.h"
#include <optional>
#include <system_error>

namespace llvm {
template <typename T> class ArrayRef;
class StringRef;

namespace sys {


/// A collection of legacy interfaces for querying information about the
/// current executing process.
class Process {
public:
  /// Process identifier type used by this interface.
  using Pid = int32_t;

  /// Get the process's identifier.
  ///
  /// \return The identifier of the current process.
  LLVM_ABI static Pid getProcessId();

  /// Get the process's page size.
  ///
  /// This may fail if the underlying syscall returns an error. In most cases,
  /// page size information is used for optimization, and this error can be
  /// safely discarded by calling consumeError, and an estimated page size
  /// substituted instead.
  ///
  /// \return The page size in bytes, or an error if the underlying syscall
  /// fails.
  LLVM_ABI static Expected<unsigned> getPageSize();

  /// Get the process's estimated page size.
  ///
  /// This function always succeeds, but if the underlying syscall to determine
  /// the page size fails then this will silently return an estimated page size.
  /// The estimated page size is guaranteed to be a power of 2.
  ///
  /// \return The page size in bytes, or an estimated power-of-two size if the
  /// page size cannot be determined.
  static unsigned getPageSizeEstimate() {
    if (auto PageSize = getPageSize())
      return *PageSize;
    else {
      consumeError(PageSize.takeError());
      return 4096;
    }
  }

  /// Return process memory usage.
  ///
  /// This static function will return the total amount of memory allocated
  /// by the process. This only counts the memory allocated via the malloc,
  /// calloc and realloc functions and includes any "free" holes in the
  /// allocated space.
  ///
  /// \return The total amount of memory allocated via malloc, calloc, and
  /// realloc, including free holes in the allocated space.
  LLVM_ABI static size_t GetMallocUsage();

  /// Get the process's elapsed, user, and system CPU time.
  ///
  /// This static function will set \p user_time to the amount of CPU time
  /// spent in user (non-kernel) mode and \p sys_time to the amount of CPU
  /// time spent in system (kernel) mode.  If the operating system does not
  /// support collection of these metrics, a zero duration will be for both
  /// values.
  /// \param elapsed Returns the system_clock::now() giving current time
  /// \param user_time Returns the current amount of user time for the process
  /// \param sys_time Returns the current amount of system time for the process
  LLVM_ABI static void GetTimeUsage(TimePoint<> &elapsed,
                                    std::chrono::nanoseconds &user_time,
                                    std::chrono::nanoseconds &sys_time);

  /// Prevent core file generation.
  ///
  /// This function makes the necessary calls to the operating system to
  /// prevent core files or any other kind of large memory dumps that can
  /// occur when a program fails.
  LLVM_ABI static void PreventCoreFiles();

  /// true if PreventCoreFiles has been called, false otherwise.
  ///
  /// \return True if PreventCoreFiles has been called, false otherwise.
  LLVM_ABI static bool AreCoreFilesPrevented();

  /// Get the value of an environment variable as a UTF-8 string.
  ///
  /// \param name Environment variable name, assumed to be UTF-8 encoded.
  /// \return The UTF-8 value of the named environment variable, or
  /// std::nullopt if it is not set.
  LLVM_ABI static std::optional<std::string> GetEnv(StringRef name);

  /// Search a PATH-like environment variable for an existing file.
  ///
  /// This function searches for an existing file in the list of directories
  /// in a PATH like environment variable, and returns the first file found,
  /// according to the order of the entries in the PATH like environment
  /// variable.  If an ignore list is specified, then any folder which is in
  /// the PATH like environment variable but is also in IgnoreList is not
  /// considered.
  ///
  /// \param EnvName Name of the PATH-like environment variable to search.
  /// \param FileName File name to look for in each directory entry.
  /// \param IgnoreList Directories to skip even if present in the variable.
  /// \param Separator Character that separates directory entries.
  /// \return The first matching file path found, or std::nullopt if none
  /// exists.
  LLVM_ABI static std::optional<std::string>
  FindInEnvPath(StringRef EnvName, StringRef FileName,
                ArrayRef<std::string> IgnoreList,
                char Separator = EnvPathSeparator);

  /// Search a PATH-like environment variable for an existing file.
  ///
  /// Equivalent to the overload that takes an ignore list, with an empty
  /// ignore list.
  ///
  /// \param EnvName Name of the PATH-like environment variable to search.
  /// \param FileName File name to look for in each directory entry.
  /// \param Separator Character that separates directory entries.
  /// \return The first matching file path found, or std::nullopt if none
  /// exists.
  LLVM_ABI static std::optional<std::string>
  FindInEnvPath(StringRef EnvName, StringRef FileName,
                char Separator = EnvPathSeparator);

  /// Ensure standard input, output, and error file descriptors are mapped.
  ///
  /// This should only be called by standalone programs; library components
  /// should not call this.
  ///
  /// \return A std::error_code indicating success or failure.
  LLVM_ABI static std::error_code FixupStandardFileDescriptors();

  /// Close a file descriptor safely around EINTR and related edge cases.
  ///
  /// It is not safe to retry close(2) when it returns with errno equivalent to
  /// EINTR; this is because *nixen cannot agree if the file descriptor is, in
  /// fact, closed when this occurs.
  ///
  /// N.B. Some operating systems, due to thread cancellation, cannot properly
  /// guarantee that it will or will not be closed one way or the other!
  ///
  /// \param FD File descriptor to close.
  /// \return A std::error_code indicating success or failure.
  LLVM_ABI static std::error_code SafelyCloseFileDescriptor(int FD);

  /// This function determines if the standard input is connected directly
  /// to a user's input (keyboard probably), rather than coming from a file
  /// or pipe.
  ///
  /// \return True if standard input is connected to a user input device.
  LLVM_ABI static bool StandardInIsUserInput();

  /// Determine whether standard output is connected to a terminal.
  ///
  /// This function determines if the standard output is connected to a
  /// "tty" or "console" window. That is, the output would be displayed to
  /// the user rather than being put on a pipe or stored in a file.
  ///
  /// \return True if standard output is connected to a terminal.
  LLVM_ABI static bool StandardOutIsDisplayed();

  /// Determine whether standard error is connected to a terminal.
  ///
  /// This function determines if the standard error is connected to a
  /// "tty" or "console" window. That is, the output would be displayed to
  /// the user rather than being put on a pipe or stored in a file.
  ///
  /// \return True if standard error is connected to a terminal.
  LLVM_ABI static bool StandardErrIsDisplayed();

  /// Determine whether a file descriptor is connected to a terminal.
  ///
  /// This function determines if the given file descriptor is connected to
  /// a "tty" or "console" window. That is, the output would be displayed to
  /// the user rather than being put on a pipe or stored in a file.
  ///
  /// \param fd File descriptor to test.
  /// \return True if \p fd is connected to a terminal.
  LLVM_ABI static bool FileDescriptorIsDisplayed(int fd);

  /// Determine whether a file descriptor is a color-capable display.
  ///
  /// This function determines if the given file descriptor is displayd and
  /// supports colors.
  ///
  /// \param fd File descriptor to test.
  /// \return True if \p fd is a display that supports colors.
  LLVM_ABI static bool FileDescriptorHasColors(int fd);

  /// Get the column width of the standard output terminal.
  ///
  /// This function determines the number of columns in the window
  /// if standard output is connected to a "tty" or "console"
  /// window. If standard output is not connected to a tty or
  /// console, or if the number of columns cannot be determined,
  /// this routine returns zero.
  ///
  /// \return The number of columns, or zero if unavailable.
  LLVM_ABI static unsigned StandardOutColumns();

  /// Get the column width of the standard error terminal.
  ///
  /// This function determines the number of columns in the window
  /// if standard error is connected to a "tty" or "console"
  /// window. If standard error is not connected to a tty or
  /// console, or if the number of columns cannot be determined,
  /// this routine returns zero.
  ///
  /// \return The number of columns, or zero if unavailable.
  LLVM_ABI static unsigned StandardErrColumns();

  /// Determine whether standard output supports colors.
  ///
  /// This function determines whether the terminal connected to standard
  /// output supports colors. If standard output is not connected to a
  /// terminal, this function returns false.
  ///
  /// \return True if standard output is a color-capable terminal.
  LLVM_ABI static bool StandardOutHasColors();

  /// Determine whether standard error supports colors.
  ///
  /// This function determines whether the terminal connected to standard
  /// error supports colors. If standard error is not connected to a
  /// terminal, this function returns false.
  ///
  /// \return True if standard error is a color-capable terminal.
  LLVM_ABI static bool StandardErrHasColors();

  /// Enable or disable ANSI escape sequences for color output.
  ///
  /// This only has an effect on Windows.
  /// Note: Setting this option is not thread-safe and should only be done
  /// during initialization.
  ///
  /// \param enable Whether ANSI escape sequences should be used.
  LLVM_ABI static void UseANSIEscapeCodes(bool enable);

  /// Whether changing colors requires the output to be flushed.
  /// This is needed on systems that don't support escape sequences for
  /// changing colors.
  ///
  /// \return True if color changes require flushing the output stream.
  LLVM_ABI static bool ColorNeedsFlush();

  /// Return the escape sequence for the requested output color.
  ///
  /// If ColorNeedsFlush() is true then this function will change the colors
  /// and return an empty escape sequence. In that case it is the
  /// responsibility of the client to flush the output stream prior to
  /// calling this function.
  ///
  /// \param c Color code character selecting the desired color.
  /// \param bold Whether to enable the bold attribute.
  /// \param bg Whether the color applies to the background.
  /// \return An escape sequence for the requested color, or an empty string
  /// if colors were applied directly.
  LLVM_ABI static const char *OutputColor(char c, bool bold, bool bg);

  /// Same as OutputColor, but only enables the bold attribute.
  ///
  /// \param bg Whether the bold attribute applies to the background.
  /// \return An escape sequence enabling bold, or an empty string if the
  /// attribute was applied directly.
  LLVM_ABI static const char *OutputBold(bool bg);

  /// This function returns the escape sequence to reverse forground and
  /// background colors.
  ///
  /// \return An escape sequence that reverses foreground and background
  /// colors, or an empty string if the reverse was applied directly.
  LLVM_ABI static const char *OutputReverse();

  /// Resets the terminals colors, or returns an escape sequence to do so.
  ///
  /// \return An escape sequence that resets terminal colors, or an empty
  /// string if colors were reset directly.
  LLVM_ABI static const char *ResetColor();

  /// Get the result of a process wide random number generator. The
  /// generator will be automatically seeded in non-deterministic fashion.
  ///
  /// \return A non-deterministically seeded process-wide random number.
  LLVM_ABI static unsigned GetRandomNumber();

  /// Exit the process, or recover when inside a CrashRecoveryContext.
  ///
  /// Equivalent to ::exit(), except when running inside a CrashRecoveryContext.
  /// In that case, the control flow will resume after RunSafely(), like for a
  /// crash, rather than exiting the current process.
  /// Use \arg NoCleanup for calling _exit() instead of exit().
  ///
  /// \param RetCode Process exit status code.
  /// \param NoCleanup If true, call _exit() instead of exit().
  [[noreturn]] LLVM_ABI static void Exit(int RetCode, bool NoCleanup = false);

private:
  [[noreturn]] static void ExitNoCleanup(int RetCode);
};

}
}

#endif
