//===- llvm/Support/Program.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the llvm::sys::Program class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_PROGRAM_H
#define LLVM_SUPPORT_PROGRAM_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FileSystem.h"
#include <chrono>
#include <optional>
#include <system_error>

namespace llvm {
class BitVector;
namespace sys {

#if defined(LLVM_ON_UNIX)
/// OS-specific separator for PATH-like environment variables.
///
/// A colon on Unix or a semicolon on Windows.
const char EnvPathSeparator = ':';
#elif defined(_WIN32)
/// OS-specific separator for PATH-like environment variables.
///
/// A colon on Unix or a semicolon on Windows.
const char EnvPathSeparator = ';';
#endif

#if defined(_WIN32)
/// Process identifier type (must match DWORD on Windows).
typedef unsigned long procid_t;
/// Platform-native process handle type (must match HANDLE on Windows).
typedef void *process_t;
#else
/// Process identifier type.
using procid_t = ::pid_t;
/// Platform-native process handle type.
using process_t = procid_t;
#endif

/// This struct encapsulates information about a process.
struct ProcessInfo {
  /// Sentinel value for an invalid process identifier.
  static constexpr procid_t InvalidPid = 0;

  /// The process identifier.
  procid_t Pid;
  /// Platform-dependent process object.
  process_t Process;

  /// The return code, set after execution.
  int ReturnCode;

  /// Construct a ProcessInfo with an invalid process identifier.
  LLVM_ABI ProcessInfo();
};

/// This struct encapsulates information about a process execution.
struct ProcessStatistics {
  /// Total elapsed wall-clock time for the process.
  std::chrono::microseconds TotalTime;
  /// CPU time spent in user mode.
  std::chrono::microseconds UserTime;
  /// Maximum resident set size in KiB.
  uint64_t PeakMemory = 0;
};

/// Find the first executable file \p Name in \p Paths.
///
/// This does not perform hashing as a shell would but instead stats each PATH
/// entry individually so should generally be avoided. Core LLVM library
/// functions and options should instead require fully specified paths.
///
/// \param Name name of the executable to find. If it contains any system
///   slashes, it will be returned as is.
/// \param Paths optional list of paths to search for \p Name. If empty it
///   will use the system PATH environment instead.
///
/// \returns The fully qualified path to the first \p Name in \p Paths if it
///   exists. \p Name if \p Name has slashes in it. Otherwise an error.
LLVM_ABI ErrorOr<std::string> findProgramByName(StringRef Name,
                                                ArrayRef<StringRef> Paths = {});

/// Change stdin to the mode described by \p Flags.
///
/// \param Flags Open flags selecting the desired stdin mode.
///
/// \returns errc::success if stdin was changed; otherwise a platform-dependent
/// error.
LLVM_ABI std::error_code ChangeStdinMode(fs::OpenFlags Flags);

/// Change stdout to the mode described by \p Flags.
///
/// \param Flags Open flags selecting the desired stdout mode.
///
/// \returns errc::success if stdout was changed; otherwise a
/// platform-dependent error.
LLVM_ABI std::error_code ChangeStdoutMode(fs::OpenFlags Flags);

/// Change stdin to binary mode.
///
/// \returns errc::success if stdin was changed; otherwise a platform-dependent
/// error.
LLVM_ABI std::error_code ChangeStdinToBinary();

/// Change stdout to binary mode.
///
/// \returns errc::success if stdout was changed; otherwise a
/// platform-dependent error.
LLVM_ABI std::error_code ChangeStdoutToBinary();

/// Execute a program and wait for it to finish.
///
/// The invoked program will inherit the stdin, stdout, and stderr file
/// descriptors, the environment and other configuration settings of the
/// invoking program. This function waits for the program to finish, so should
/// be avoided in library functions that aren't expected to block. Consider
/// using ExecuteNoWait() instead.
///
/// \param Program Path of the program to be executed. It is presumed this is
///   the result of the findProgramByName method.
/// \param Args An array of strings that are passed to the program. The first
///   element should be the name of the program. The array should **not** be
///   terminated by an empty StringRef.
/// \param Env An optional vector of strings to use for the program's
///   environment. If not provided, the current program's environment will be
///   used. If specified, the vector should **not** be terminated by an empty
///   StringRef.
/// \param Redirects An array of optional paths. Should have a size of zero or
///   three. If the array is empty, no redirections are performed. Otherwise,
///   the inferior process's stdin(0), stdout(1), and stderr(2) will be
///   redirected to the corresponding paths, if the optional path is present
///   (not \c std::nullopt). When an empty path is passed in, the corresponding
///   file descriptor will be disconnected (ie, /dev/null'd) in a portable way.
/// \param SecondsToWait If non-zero, this specifies the amount of time to wait
///   for the child process to exit. If the time expires, the child is killed
///   and this call returns. If zero, this function will wait until the child
///   finishes or forever if it doesn't.
/// \param MemoryLimit If non-zero, this specifies max. amount of memory can be
///   allocated by process. If memory usage will be higher limit, the child is
///   killed and this call returns. If zero - no memory limit.
/// \param ErrMsg If non-zero, provides a pointer to a string instance in which
///   error messages will be returned. If the string is non-empty upon return
///   an error occurred while invoking the program.
/// \param ExecutionFailed If non-null, set to true when process creation
///   fails and to false otherwise.
/// \param ProcStat If non-zero, provides a pointer to a structure in which
///   process execution statistics will be stored.
/// \param AffinityMask CPUs or processors the new program shall run on.
///
/// \returns an integer result code indicating the status of the program.
/// A zero or positive value indicates the result code of the program.
/// -1 indicates failure to execute
/// -2 indicates a crash during execution or timeout
LLVM_ABI int ExecuteAndWait(
    StringRef Program, ArrayRef<StringRef> Args,
    std::optional<ArrayRef<StringRef>> Env = std::nullopt,
    ArrayRef<std::optional<StringRef>> Redirects = {},
    unsigned SecondsToWait = 0, unsigned MemoryLimit = 0,
    std::string *ErrMsg = nullptr, bool *ExecutionFailed = nullptr,
    std::optional<ProcessStatistics> *ProcStat = nullptr,
    BitVector *AffinityMask = nullptr);

/// Similar to \ref ExecuteAndWait, but returns immediately.
///
/// \param Program Path of the program to be executed. It is presumed this is
///   the result of the findProgramByName method.
/// \param Args An array of strings that are passed to the program. The first
///   element should be the name of the program. The array should **not** be
///   terminated by an empty StringRef.
/// \param Env An optional vector of strings to use for the program's
///   environment. If not provided, the current program's environment will be
///   used. If specified, the vector should **not** be terminated by an empty
///   StringRef.
/// \param Redirects An array of optional paths. Should have a size of zero or
///   three. If the array is empty, no redirections are performed. Otherwise,
///   the inferior process's stdin(0), stdout(1), and stderr(2) will be
///   redirected to the corresponding paths, if the optional path is present
///   (not \c std::nullopt). When an empty path is passed in, the corresponding
///   file descriptor will be disconnected (ie, /dev/null'd) in a portable way.
/// \param MemoryLimit If non-zero, this specifies max. amount of memory can be
///   allocated by process. If memory usage will be higher limit, the child is
///   killed and this call returns. If zero - no memory limit.
/// \param ErrMsg If non-zero, provides a pointer to a string instance in which
///   error messages will be returned. If the string is non-empty upon return
///   an error occurred while invoking the program.
/// \param ExecutionFailed If non-null, set to true when process creation
///   fails and to false otherwise.
/// \param AffinityMask CPUs or processors the new program shall run on.
/// \param DetachProcess If true the executed program detaches from the
///   controlling terminal. I/O streams such as llvm::outs, llvm::errs, and
///   stdin will be closed until redirected to another output location.
///
/// \returns The \ref ProcessInfo of the newly launched process.
/// \note On Microsoft Windows systems, users will need to either call
/// \ref Wait until the process has finished executing or win32's CloseHandle
/// API on ProcessInfo.ProcessHandle to avoid memory leaks.
LLVM_ABI ProcessInfo ExecuteNoWait(
    StringRef Program, ArrayRef<StringRef> Args,
    std::optional<ArrayRef<StringRef>> Env,
    ArrayRef<std::optional<StringRef>> Redirects = {}, unsigned MemoryLimit = 0,
    std::string *ErrMsg = nullptr, bool *ExecutionFailed = nullptr,
    BitVector *AffinityMask = nullptr, bool DetachProcess = false);

/// Return true if the given arguments fit within system-specific
/// argument length limits.
///
/// \param Program Path of the program that would be executed.
/// \param Args Arguments that would be passed to the program.
///
/// \returns true if the program path and arguments fit within the system
///   command-line length limits; false otherwise.
LLVM_ABI bool commandLineFitsWithinSystemLimits(StringRef Program,
                                                ArrayRef<StringRef> Args);

/// Return true if the given arguments fit within system-specific
/// argument length limits.
///
/// \param Program Path of the program that would be executed.
/// \param Args Null-terminated C-string arguments that would be passed to the
///   program.
///
/// \returns true if the program path and arguments fit within the system
///   command-line length limits; false otherwise.
LLVM_ABI bool commandLineFitsWithinSystemLimits(StringRef Program,
                                                ArrayRef<const char *> Args);

/// File encoding options when writing contents that a non-UTF8 tool will
/// read (on Windows systems). For UNIX, we always use UTF-8.
enum WindowsEncodingMethod {
  /// UTF-8 is the LLVM native encoding, being the same as "do not perform
  /// encoding conversion".
  WEM_UTF8,
  WEM_CurrentCodePage,
  WEM_UTF16
};

/// Saves the UTF8-encoded \p contents string into the file \p FileName
/// using a specific encoding.
///
/// This write file function adds the possibility to choose which encoding
/// to use when writing a text file. On Windows, this is important when
/// writing files with internationalization support with an encoding that is
/// different from the one used in LLVM (UTF-8). We use this when writing
/// response files, since GCC tools on MinGW only understand legacy code
/// pages, and VisualStudio tools only understand UTF-16.
/// For UNIX, using different encodings is silently ignored, since all tools
/// work well with UTF-8.
/// This function assumes that you only use UTF-8 *text* data and will convert
/// it to your desired encoding before writing to the file.
///
/// FIXME: We use EM_CurrentCodePage to write response files for GNU tools in
/// a MinGW/MinGW-w64 environment, which has serious flaws but currently is
/// our best shot to make gcc/ld understand international characters. This
/// should be changed as soon as binutils fix this to support UTF16 on mingw.
///
/// \param FileName Path of the file to write.
/// \param Contents UTF-8 text to encode and write.
/// \param Encoding Encoding to apply on Windows (ignored on UNIX).
///
/// \returns non-zero error_code if failed
LLVM_ABI std::error_code
writeFileWithEncoding(StringRef FileName, StringRef Contents,
                      WindowsEncodingMethod Encoding = WEM_UTF8);

/// This function waits for the process specified by \p PI to finish.
///
/// \param PI The child process that should be waited on.
/// \param SecondsToWait If std::nullopt, waits until child has terminated.
///   If a value, this specifies the amount of time to wait for the child
///   process. If the time expires, and \p Polling is false, the child is
///   killed and this function returns. If the time expires and \p Polling is
///   true, the child is resumed. If zero, this function will perform a
///   non-blocking wait on the child process.
/// \param ErrMsg If non-zero, provides a pointer to a string instance in which
///   error messages will be returned. If the string is non-empty upon return
///   an error occurred while invoking the program.
/// \param ProcStat If non-zero, provides a pointer to a structure in which
///   process execution statistics will be stored.
/// \param Polling If true, do not kill the process on timeout.
///
/// \returns A \see ProcessInfo struct with Pid set to:
/// \li The process id of the child process if the child process has changed
/// state.
/// \li 0 if the child process has not changed state.
/// \note Users of this function should always check the ReturnCode member of
/// the \see ProcessInfo returned from this function.
LLVM_ABI ProcessInfo Wait(const ProcessInfo &PI,
                          std::optional<unsigned> SecondsToWait,
                          std::string *ErrMsg = nullptr,
                          std::optional<ProcessStatistics> *ProcStat = nullptr,
                          bool Polling = false);

/// Print a command argument, and optionally quote it.
///
/// \param OS Stream to write the argument to.
/// \param Arg Argument text to print.
/// \param Quote Whether to wrap the argument in quotes when needed.
LLVM_ABI void printArg(llvm::raw_ostream &OS, StringRef Arg, bool Quote);

#if defined(_WIN32)
/// Given a list of command line arguments, quote and escape them as necessary
/// to build a single flat command line appropriate for calling CreateProcess
/// on
/// Windows.
LLVM_ABI ErrorOr<std::wstring>
flattenWindowsCommandLine(ArrayRef<StringRef> Args);
#endif
} // namespace sys
} // namespace llvm

#endif
