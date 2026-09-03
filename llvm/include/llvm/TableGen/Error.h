//===- llvm/TableGen/Error.h - tblgen error handling helpers ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains error handling helper routines to pretty-print diagnostic
// messages from tblgen.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TABLEGEN_ERROR_H
#define LLVM_TABLEGEN_ERROR_H

#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/Support/SourceMgr.h"

namespace llvm {
class Record;
class RecordVal;
class Init;

/// Print a note diagnostic with message \p Msg and no source location.
///
/// \param Msg Note text to print.
LLVM_ABI void PrintNote(const Twine &Msg);
/// Print a note diagnostic by writing into a colored note stream.
///
/// \param PrintMsg Callback that writes the note body to \p OS.
LLVM_ABI void PrintNote(function_ref<void(raw_ostream &OS)> PrintMsg);
/// Print a note diagnostic at source location(s) \p NoteLoc.
///
/// \param NoteLoc Source locations associated with the note.
/// \param Msg Note text to print.
LLVM_ABI void PrintNote(ArrayRef<SMLoc> NoteLoc, const Twine &Msg);

/// Print a note with message \p Msg and then exit the process.
///
/// \param Msg Note text to print before exiting.
[[noreturn]] LLVM_ABI void PrintFatalNote(const Twine &Msg);
/// Print a note at \p ErrorLoc and then exit the process.
///
/// \param ErrorLoc Source locations associated with the note.
/// \param Msg Note text to print before exiting.
[[noreturn]] LLVM_ABI void PrintFatalNote(ArrayRef<SMLoc> ErrorLoc,
                                          const Twine &Msg);
/// Print a note at the location of record \p Rec and then exit the process.
///
/// \param Rec Record whose source location is used for the note.
/// \param Msg Note text to print before exiting.
[[noreturn]] LLVM_ABI void PrintFatalNote(const Record *Rec, const Twine &Msg);
/// Print a note at the location of field \p RecVal and then exit the process.
///
/// \param RecVal Record field whose source location is used for the note.
/// \param Msg Note text to print before exiting.
[[noreturn]] LLVM_ABI void PrintFatalNote(const RecordVal *RecVal,
                                          const Twine &Msg);

/// Print a warning diagnostic with message \p Msg and no source location.
///
/// \param Msg Warning text to print.
LLVM_ABI void PrintWarning(const Twine &Msg);
/// Print a warning diagnostic at source location(s) \p WarningLoc.
///
/// \param WarningLoc Source locations associated with the warning.
/// \param Msg Warning text to print.
LLVM_ABI void PrintWarning(ArrayRef<SMLoc> WarningLoc, const Twine &Msg);
/// Print a warning diagnostic at pointer location \p Loc within the buffer.
///
/// \param Loc Pointer into a SourceMgr buffer identifying the warning site.
/// \param Msg Warning text to print.
LLVM_ABI void PrintWarning(const char *Loc, const Twine &Msg);

/// Print an error diagnostic with message \p Msg and no source location.
///
/// \param Msg Error text to print.
LLVM_ABI void PrintError(const Twine &Msg);
/// Print an error diagnostic by writing into a colored error stream.
///
/// \param PrintMsg Callback that writes the error body to \p OS.
LLVM_ABI void PrintError(function_ref<void(raw_ostream &OS)> PrintMsg);
/// Print an error diagnostic at source location(s) \p ErrorLoc.
///
/// Increments \c ErrorsPrinted.
///
/// \param ErrorLoc Source locations associated with the error.
/// \param Msg Error text to print.
LLVM_ABI void PrintError(ArrayRef<SMLoc> ErrorLoc, const Twine &Msg);
/// Print an error diagnostic at pointer location \p Loc within the buffer.
///
/// \param Loc Pointer into a SourceMgr buffer identifying the error site.
/// \param Msg Error text to print.
LLVM_ABI void PrintError(const char *Loc, const Twine &Msg);
/// Print an error diagnostic at the location of record \p Rec.
///
/// Increments \c ErrorsPrinted.
///
/// \param Rec Record whose source location is used for the error.
/// \param Msg Error text to print.
LLVM_ABI void PrintError(const Record *Rec, const Twine &Msg);
/// Print an error diagnostic at the location of field \p RecVal.
///
/// Increments \c ErrorsPrinted.
///
/// \param RecVal Record field whose source location is used for the error.
/// \param Msg Error text to print.
LLVM_ABI void PrintError(const RecordVal *RecVal, const Twine &Msg);

/// Print an error with message \p Msg and then exit the process.
///
/// \param Msg Error text to print before exiting.
[[noreturn]] LLVM_ABI void PrintFatalError(const Twine &Msg);
/// Print an error at \p ErrorLoc and then exit the process.
///
/// \param ErrorLoc Source locations associated with the error.
/// \param Msg Error text to print before exiting.
[[noreturn]] LLVM_ABI void PrintFatalError(ArrayRef<SMLoc> ErrorLoc,
                                           const Twine &Msg);
/// Print an error at the location of record \p Rec and then exit the process.
///
/// \param Rec Record whose source location is used for the error.
/// \param Msg Error text to print before exiting.
[[noreturn]] LLVM_ABI void PrintFatalError(const Record *Rec, const Twine &Msg);
/// Print an error at the location of field \p RecVal and then exit the process.
///
/// \param RecVal Record field whose source location is used for the error.
/// \param Msg Error text to print before exiting.
[[noreturn]] LLVM_ABI void PrintFatalError(const RecordVal *RecVal,
                                           const Twine &Msg);
/// Print an error via \p PrintMsg and then exit the process.
///
/// \param PrintMsg Callback that writes the error body to \p OS.
[[noreturn]] LLVM_ABI void
PrintFatalError(function_ref<void(raw_ostream &OS)> PrintMsg);

/// Check TableGen assert condition \p Condition at \p Loc.
///
/// If \p Condition is not an integer-like init or evaluates to false, prints a
/// nonfatal error (using \p Message when it is a string) and returns true.
///
/// \param Loc Source location of the assert.
/// \param Condition Init expected to convert to a bit/bits/int condition.
/// \param Message Init holding the assert failure message when a string.
/// \returns true if the assert failed or was ill-formed; false otherwise.
LLVM_ABI bool CheckAssert(SMLoc Loc, const Init *Condition,
                          const Init *Message);
/// Dump TableGen message \p Message as a note at \p Loc.
///
/// If \p Message is not a string init, prints an error instead.
///
/// \param Loc Source location for the dump.
/// \param Message Init expected to be a string to print.
LLVM_ABI void dumpMessage(SMLoc Loc, const Init *Message);

/// Global SourceMgr used for TableGen diagnostics.
extern LLVM_ABI SourceMgr SrcMgr;
/// Count of error diagnostics printed so far.
extern LLVM_ABI unsigned ErrorsPrinted;

} // end namespace llvm

#endif
