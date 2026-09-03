//===- llvm/Support/PrettyStackTrace.h - Pretty Crash Handling --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the PrettyStackTraceEntry class, which is used to make
// crashes give more contextual information about what the program was doing
// when it crashed.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_PRETTYSTACKTRACE_H
#define LLVM_SUPPORT_PRETTYSTACKTRACE_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
  class raw_ostream;

  /// Enables dumping a "pretty" stack trace when the program crashes.
  ///
  /// \see PrettyStackTraceEntry
  LLVM_ABI void EnablePrettyStackTrace();

  /// Enables (or disables) dumping a "pretty" stack trace when the user sends
  /// SIGINFO or SIGUSR1 to the current process.
  ///
  /// This is a per-thread decision so that a program can choose to print stack
  /// traces only on a primary thread, or on all threads that use
  /// PrettyStackTraceEntry.
  ///
  /// \param ShouldEnable If true, enable pretty stack traces for SIGINFO/SIGUSR1
  /// on this thread; if false, disable them.
  /// \see EnablePrettyStackTrace
  /// \see PrettyStackTraceEntry
  LLVM_ABI void
  EnablePrettyStackTraceOnSigInfoForThisThread(bool ShouldEnable = true);

  /// Replaces the generic bug report message that is output upon
  /// a crash.
  ///
  /// \param Msg Null-terminated bug report message to show on crash.
  LLVM_ABI void setBugReportMsg(const char *Msg);

  /// Get the bug report message that will be output upon a crash.
  ///
  /// \return Null-terminated bug report message shown on crash.
  LLVM_ABI const char *getBugReportMsg();

  /// A frame of the "pretty" stack trace dumped when a program crashes.
  ///
  /// You can define subclasses of this and declare them on the program stack:
  /// when they are constructed and destructed, they will add their symbolic
  /// frames to a virtual stack trace. This gets dumped out if the program
  /// crashes.
  class LLVM_ABI PrettyStackTraceEntry {
    /// Reverse the singly linked list of pretty stack-trace entries.
    ///
    /// \param Head Head of the pretty stack-trace entry list to reverse.
    /// \return New head of the reversed list.
    LLVM_ABI friend PrettyStackTraceEntry *
    ReverseStackTrace(PrettyStackTraceEntry *Head);

    PrettyStackTraceEntry *NextEntry;
    PrettyStackTraceEntry(const PrettyStackTraceEntry &) = delete;
    void operator=(const PrettyStackTraceEntry &) = delete;
  public:
    /// Construct a pretty stack-trace entry and push it onto the stack.
    PrettyStackTraceEntry();
    /// Destroy this entry and pop it from the pretty stack trace.
    virtual ~PrettyStackTraceEntry();

    /// Emit information about this stack frame to OS.
    ///
    /// \param OS Stream to write this frame's description to.
    virtual void print(raw_ostream &OS) const = 0;

    /// getNextEntry - Return the next entry in the list of frames.
    ///
    /// \return The next pretty stack-trace entry, or nullptr if none.
    const PrettyStackTraceEntry *getNextEntry() const { return NextEntry; }
  };

  /// PrettyStackTraceString - This object prints a specified string (which
  /// should not contain newlines) to the stream as the stack trace when a crash
  /// occurs.
  class LLVM_ABI PrettyStackTraceString : public PrettyStackTraceEntry {
    const char *Str;
  public:
    /// Construct an entry that prints \p str when a crash occurs.
    ///
    /// \param str Null-terminated message to print (should not contain newlines).
    PrettyStackTraceString(const char *str) : Str(str) {}
    /// Print the stored string to \p OS.
    ///
    /// \param OS Stream to write the string to.
    void print(raw_ostream &OS) const override;
  };

  /// Pretty stack-trace entry that prints a printf-style formatted string.
  ///
  /// The format string may use printf-style formatting but should not contain
  /// newlines. The formatted result is written to the stream as the stack trace
  /// when a crash occurs.
  class LLVM_ABI PrettyStackTraceFormat : public PrettyStackTraceEntry {
    llvm::SmallVector<char, 32> Str;
  public:
    /// Construct an entry from a printf-style format string and arguments.
    ///
    /// \param Format Printf-style format string (should not contain newlines).
    PrettyStackTraceFormat(const char *Format, ...);
    /// Print the formatted string to \p OS.
    ///
    /// \param OS Stream to write the formatted string to.
    void print(raw_ostream &OS) const override;
  };

  /// PrettyStackTraceProgram - This object prints a specified program arguments
  /// to the stream as the stack trace when a crash occurs.
  class LLVM_ABI PrettyStackTraceProgram : public PrettyStackTraceEntry {
    int ArgC;
    const char *const *ArgV;
  public:
    /// Construct an entry that prints the program arguments on crash.
    ///
    /// Also enables pretty stack traces for the process.
    ///
    /// \param argc Number of arguments in \p argv.
    /// \param argv Null-terminated array of argument strings.
    PrettyStackTraceProgram(int argc, const char * const*argv)
      : ArgC(argc), ArgV(argv) {
      EnablePrettyStackTrace();
    }
    /// Print the program arguments to \p OS.
    ///
    /// \param OS Stream to write the arguments to.
    void print(raw_ostream &OS) const override;
  };

  /// Returns the topmost element of the "pretty" stack state.
  ///
  /// \return Opaque pointer to the current pretty stack state.
  LLVM_ABI const void *SavePrettyStackState();

  /// Restore the pretty stack state saved by SavePrettyStackState.
  ///
  /// \p State should come from a previous call to SavePrettyStackState(). This
  /// is useful when using a CrashRecoveryContext in code that also uses
  /// PrettyStackTraceEntries, to make sure the stack that's printed if a crash
  /// happens after a crash that's been recovered by CrashRecoveryContext
  /// doesn't have frames on it that were added in code unwound by the
  /// CrashRecoveryContext.
  ///
  /// \param State Opaque stack state previously returned by
  /// SavePrettyStackState().
  LLVM_ABI void RestorePrettyStackState(const void *State);

} // end namespace llvm

#endif
