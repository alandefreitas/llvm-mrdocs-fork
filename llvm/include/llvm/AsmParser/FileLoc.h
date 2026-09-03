//===-- FileLoc.h ---------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ASMPARSER_FILELOC_H
#define LLVM_ASMPARSER_FILELOC_H

#include <cassert>
#include <utility>

namespace llvm {

/// Struct holding Line:Column location
struct FileLoc {
  /// 0-based line number
  unsigned Line;
  /// 0-based column number
  unsigned Col;

  /// Compare two locations for equality.
  ///
  /// \param RHS Location to compare against.
  /// \return True if both line and column match.
  bool operator==(const FileLoc &RHS) const {
    return Line == RHS.Line && Col == RHS.Col;
  }

  /// Compare whether this location is less than or equal to another.
  ///
  /// Ordering is by line, then by column within the same line.
  /// \param RHS Location to compare against.
  /// \return True if this location precedes or equals \p RHS.
  bool operator<=(const FileLoc &RHS) const {
    return Line < RHS.Line || (Line == RHS.Line && Col <= RHS.Col);
  }

  /// Compare whether this location is strictly less than another.
  ///
  /// Ordering is by line, then by column within the same line.
  /// \param RHS Location to compare against.
  /// \return True if this location strictly precedes \p RHS.
  bool operator<(const FileLoc &RHS) const {
    return Line < RHS.Line || (Line == RHS.Line && Col < RHS.Col);
  }

  /// Construct a location at line 0, column 0.
  FileLoc() : Line(0), Col(0) {}
  /// Construct a location from a line and column.
  ///
  /// \param L 0-based line number.
  /// \param C 0-based column number.
  FileLoc(unsigned L, unsigned C) : Line(L), Col(C) {}
  /// Construct a location from a line/column pair.
  ///
  /// \param LC Pair of (line, column), both 0-based.
  FileLoc(std::pair<unsigned, unsigned> LC) : Line(LC.first), Col(LC.second) {}
};

/// Struct holding a semiopen range [Start; End)
struct FileLocRange {
  /// Inclusive start of the range.
  FileLoc Start;
  /// Exclusive end of the range.
  FileLoc End;

  /// Construct an empty range at line 0, column 0.
  FileLocRange() : Start(0, 0), End(0, 0) {}

  /// Construct a half-open range from start to end.
  ///
  /// \p S must be less than or equal to \p E.
  /// \param S Inclusive start location.
  /// \param E Exclusive end location.
  FileLocRange(FileLoc S, FileLoc E) : Start(S), End(E) {
    assert(Start <= End);
  }

  /// Check whether a location lies in this half-open range.
  ///
  /// \param L Location to test.
  /// \return True if \p Start <= \p L < \p End.
  bool contains(FileLoc L) const { return Start <= L && L < End; }

  /// Check whether another range is fully contained in this range.
  ///
  /// \param LR Range to test.
  /// \return True if \p LR is entirely within [\p Start, \p End).
  bool contains(FileLocRange LR) const {
    return Start <= LR.Start && LR.End <= End;
  }
};

} // namespace llvm

#endif
