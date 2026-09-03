//===- PrintPasses.h - Determining whether/when to print IR ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_PRINTPASSES_H
#define LLVM_IR_PRINTPASSES_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLine.h"
#include <vector>

namespace llvm {

/// How `-print-changed` presents IR that a pass modified.
enum class ChangePrinter {
  None,              ///< Disabled; do not print changed IR.
  Verbose,           ///< Full after-IR dump, with unchanged and filtered notes.
  Quiet,             ///< Full after-IR dump, without unchanged or filtered notes.
  DiffVerbose,       ///< Patch-like diff, with unchanged and filtered notes.
  DiffQuiet,         ///< Patch-like diff, without unchanged or filtered notes.
  ColourDiffVerbose, ///< Colored patch-like diff, with unchanged and filtered notes.
  ColourDiffQuiet,   ///< Colored patch-like diff, without unchanged or filtered notes.
  DotCfgVerbose,     ///< Graphical CFG website, with unchanged and filtered notes.
  DotCfgQuiet        ///< Graphical CFG website, without unchanged or filtered notes.
};

/// Storage for the `-print-changed` command-line option.
extern LLVM_ABI cl::opt<ChangePrinter> PrintChanged;

/// Returns true if printing IR before at least one pass is enabled.
/// @return True if printing IR before at least one pass is enabled.
LLVM_ABI bool shouldPrintBeforeSomePass();
/// Returns true if printing IR after at least one pass is enabled.
/// @return True if printing IR after at least one pass is enabled.
LLVM_ABI bool shouldPrintAfterSomePass();

/// Returns true if IR should be printed before the pass named \p PassID.
/// \param PassID Pass identifier, such as "instcombine".
/// @return True if IR should be printed before the named pass.
LLVM_ABI bool shouldPrintBeforePass(StringRef PassID);
/// Returns true if IR should be printed after the pass named \p PassID.
/// \param PassID Pass identifier, such as "instcombine".
/// @return True if IR should be printed after the named pass.
LLVM_ABI bool shouldPrintAfterPass(StringRef PassID);

/// Returns true if IR should be printed before every pass.
/// @return True if IR should be printed before every pass.
LLVM_ABI bool shouldPrintBeforeAll();
/// Returns true if IR should be printed after every pass.
/// @return True if IR should be printed after every pass.
LLVM_ABI bool shouldPrintAfterAll();

/// Returns the names of passes whose IR should be printed before they run.
/// @return The names of passes whose IR should be printed before they run.
LLVM_ABI std::vector<std::string> printBeforePasses();
/// Returns the names of passes whose IR should be printed after they run.
/// @return The names of passes whose IR should be printed after they run.
LLVM_ABI std::vector<std::string> printAfterPasses();

/// Returns true if function-level IR dumps should print the entire module.
/// @return True if function-level IR dumps should print the entire module.
LLVM_ABI bool forcePrintModuleIR();

/// Returns true if loop-pass IR dumps should print the entire function.
/// @return True if loop-pass IR dumps should print the entire function.
LLVM_ABI bool forcePrintFuncIR();

/// Returns true if `-filter-passes` is empty or contains \p PassName.
/// \param PassName Pass name to test against `-filter-passes`.
/// @return True if `-filter-passes` is empty or contains \p PassName.
LLVM_ABI bool isPassInPrintList(StringRef PassName);
/// Returns true if the `-filter-passes` list is empty.
/// @return True if the `-filter-passes` list is empty.
LLVM_ABI bool isFilterPassesEmpty();

/// Returns true if IR for \p FunctionName should be printed.
/// \param FunctionName Function name to test against `-filter-print-funcs`.
/// @return True if IR for \p FunctionName should be printed.
LLVM_ABI bool isFunctionInPrintList(StringRef FunctionName);

/// Ensures temporary files exist, creating or re-using them.
///
/// \p FD contains file descriptors (-1 indicates that the file should be
/// created) and \p SR contains the corresponding initial content.  \p FileName
/// will have the filenames filled in when creating files.
/// \param FD File descriptors to reuse, or -1 to create a new temp file.
/// \param SR Initial content written to the first \c SR.size() files.
/// \param FileName On return, paths of created or reused files.
/// @return The first error code encountered, or success if all files were prepared.
LLVM_ABI std::error_code prepareTempFiles(SmallVector<int> &FD,
                                          ArrayRef<StringRef> SR,
                                          SmallVector<std::string> &FileName);

/// Removes the temporary files named in \p FileName.
///
/// Typically used in conjunction with prepareTempFiles.
/// \param FileName Paths of temporary files to delete.
/// @return The first error code encountered, or success if all files were removed.
LLVM_ABI std::error_code cleanUpTempFiles(ArrayRef<std::string> FileName);

/// Performs a system-based diff between \p Before and \p After.
///
/// Uses \p OldLineFormat, \p NewLineFormat, and \p UnchangedLineFormat to
/// control the formatting of the output.
/// \param Before Text of the IR (or file body) before the change.
/// \param After Text of the IR (or file body) after the change.
/// \param OldLineFormat `diff --old-line-format` string.
/// \param NewLineFormat `diff --new-line-format` string.
/// \param UnchangedLineFormat `diff --unchanged-line-format` string.
/// @return The formatted diff, or an error message on failure.
LLVM_ABI std::string doSystemDiff(StringRef Before, StringRef After,
                                  StringRef OldLineFormat,
                                  StringRef NewLineFormat,
                                  StringRef UnchangedLineFormat);

/// Reports a `-print-changed` dump for one pass over one IR unit.
///
/// IsInteresting is isPassInPrintList(PassID); ShouldReport is whether
/// the unit passed all filters (Before/After are only set then).
/// \param Before IR text before the pass; meaningful when \p ShouldReport.
/// \param After IR text after the pass; meaningful when \p ShouldReport.
/// \param PassName Display name of the pass.
/// \param PassID Pass identifier used in filter checks.
/// \param IRName Name of the function or module being reported.
/// \param IsInteresting True when the pass is selected by `-filter-passes`.
/// \param ShouldReport True when the IR unit passed all print filters.
LLVM_ABI void reportChangedIR(StringRef Before, StringRef After,
                              StringRef PassName, StringRef PassID,
                              StringRef IRName, bool IsInteresting,
                              bool ShouldReport);

} // namespace llvm

#endif // LLVM_IR_PRINTPASSES_H
