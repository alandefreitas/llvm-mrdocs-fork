//===- Utils.h --------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DWARFLINKER_UTILS_H
#define LLVM_DWARFLINKER_UTILS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Twine.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

namespace llvm {
namespace dwarf_linker {

/// Map each DW_AT_LLVM_stmt_sequence offset to its first line-table row.
///
/// Seeds the map from \p LT.Sequences (the DWARF parser's discovered
/// sequences), then augments it by walking row boundaries
/// (DW_LNE_end_sequence markers) and matching them against the sorted
/// input offsets in \p SortedStmtSeqOffsets, using the parser's results
/// as ground-truth anchors. This recovers sequences the parser may not
/// have registered and keeps the classic and parallel DWARFLinkers in
/// lockstep. Caller passes \p SortedStmtSeqOffsets sorted ascending and
/// deduplicated.
///
/// \param LT Line table whose sequences and rows are consulted.
/// \param SortedStmtSeqOffsets Ascending, deduplicated stmt_sequence byte
///        offsets from the input.
/// \param SeqOffToFirstRow Output map from stmt_sequence offset to the
///        first-row index in \p LT.Rows.
LLVM_ABI void buildStmtSeqOffsetToFirstRowIndex(
    const DWARFDebugLine::LineTable &LT,
    ArrayRef<uint64_t> SortedStmtSeqOffsets,
    DenseMap<uint64_t, uint64_t> &SeqOffToFirstRow);

/// Call \p Iteration until it returns false, or fail if the limit is hit.
///
/// If the number of iterations exceeds \p MaxCounter then an Error is
/// returned. This function should be used for loops which are assumed to
/// have a number of iterations significantly smaller than \p MaxCounter to
/// avoid infinite looping in error cases.
///
/// \param Iteration Callback that returns true to continue, false to stop,
///        or an Error on failure.
/// \param MaxCounter Maximum allowed iterations before treating the loop as
///        infinite.
/// \return Success when \p Iteration returns false, or an Error if
///         \p Iteration fails or \p MaxCounter is exceeded.
inline Error finiteLoop(function_ref<Expected<bool>()> Iteration,
                        size_t MaxCounter = 100000) {
  size_t iterationsCounter = 0;
  while (iterationsCounter++ < MaxCounter) {
    Expected<bool> IterationResultOrError = Iteration();
    if (!IterationResultOrError)
      return IterationResultOrError.takeError();
    if (!IterationResultOrError.get())
      return Error::success();
  }
  return createStringError(std::errc::invalid_argument, "Infinite recursion");
}

/// Make a best effort to guess the
/// Xcode.app/Contents/Developer path from an SDK path.
///
/// \param SysRoot SDK or sysroot path to inspect.
/// \return The inferred Developer directory, or an empty string on failure.
inline StringRef guessDeveloperDir(StringRef SysRoot) {
  // Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
  auto it = sys::path::rbegin(SysRoot);
  auto end = sys::path::rend(SysRoot);
  if (it == end || !it->ends_with(".sdk"))
    return {};
  ++it;
  // Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs
  if (it == end || *it != "SDKs")
    return {};
  auto developerEnd = it;
  ++it;
  while (it != end) {
    // Contents/Developer/Platforms/MacOSX.platform/Developer
    if (*it != "Developer")
      return {};
    ++it;
    if (it == end)
      return {};
    if (*it == "Contents")
      return StringRef(SysRoot.data(),
                       developerEnd - sys::path::rend(SysRoot) - 1);
    // Contents/Developer/Platforms/MacOSX.platform
    if (!it->ends_with(".platform"))
      return {};
    ++it;
    // Contents/Developer/Platforms
    if (it == end || *it != "Platforms")
      return {};
    developerEnd = it;
    ++it;
  }
  return {};
}

/// Make a best effort to determine whether \p Path is inside a toolchain.
///
/// \param Path Path string to inspect.
/// \return True if \p Path appears to lie under a toolchain directory.
inline bool isInToolchainDir(StringRef Path) {
  // Library/Developer/Toolchains/swift-DEVELOPMENT-SNAPSHOT-2024-05-15-a.xctoolchain/usr/lib/swift/macosx/_StringProcessing.swiftmodule/arm64-apple-macos.private.swiftinterface
  for (auto it = sys::path::rbegin(Path), end = sys::path::rend(Path);
       it != end; ++it) {
    if (it->ends_with(".xctoolchain")) {
      ++it;
      if (it == end)
        return false;
      if (*it != "Toolchains")
        return false;
      ++it;
      if (it == end)
        return false;
      if (*it != "Developer")
        return false;
      return true;
    }
  }
  return false;
}

/// True if \p Path is absolute under POSIX or Windows path rules.
///
/// Debug info can contain paths from any OS, not necessarily the one
/// currently running. Different compilation units may also have been
/// built on different operating systems and linked together later.
///
/// \param Path Path string to test.
/// \return True if absolute for either path style.
inline bool isPathAbsoluteOnWindowsOrPosix(const Twine &Path) {
  return sys::path::is_absolute(Path, sys::path::Style::posix) ||
         sys::path::is_absolute(Path, sys::path::Style::windows);
}

} // end of namespace dwarf_linker
} // end of namespace llvm

#endif // LLVM_DWARFLINKER_UTILS_H
