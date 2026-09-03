//===- Line.h ---------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_LINE_H
#define LLVM_DEBUGINFO_CODEVIEW_LINE_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"
#include <cinttypes>

namespace llvm {
namespace codeview {

/// Little-endian 32-bit unsigned integer type from \c llvm::support.
using llvm::support::ulittle32_t;

/// Encoded CodeView start/end line numbers and statement flag for a line entry.
class LineInfo {
public:
  /// Special start-line values that control debugger step-into behavior.
  enum : uint32_t {
    AlwaysStepIntoLineNumber = 0xfeefee, ///< Line number meaning always step into.
    NeverStepIntoLineNumber = 0xf00f00   ///< Line number meaning never step into.
  };

  /// Bit shift applied to the end-line delta field in the packed encoding.
  enum : int {
    EndLineDeltaShift = 24 ///< Shift count for the end-line delta field.
  };

  /// Bit masks for the packed 32-bit CodeView line encoding.
  enum : uint32_t {
    StartLineMask = 0x00ffffff,   ///< Mask selecting the start line number.
    EndLineDeltaMask = 0x7f000000, ///< Mask selecting the end-line delta.
    StatementFlag = 0x80000000u   ///< Flag bit marking a statement boundary.
  };

  /// Construct line info from an explicit start line, end line, and statement flag.
  ///
  /// \param StartLine Starting source line number.
  /// \param EndLine Ending source line number.
  /// \param IsStatement True if this entry marks a statement boundary.
  LLVM_ABI LineInfo(uint32_t StartLine, uint32_t EndLine, bool IsStatement);
  /// Construct line info from a packed 32-bit CodeView line encoding.
  ///
  /// \param LineData Raw packed line data (start, delta, statement flag).
  LineInfo(uint32_t LineData) : LineData(LineData) {}

  /// Return the start source line number.
  ///
  /// \returns The start source line number.
  uint32_t getStartLine() const { return LineData & StartLineMask; }

  /// Return the delta from the start line to the end line.
  ///
  /// \returns The end-line delta encoded in the packed line data.
  uint32_t getLineDelta() const {
    return (LineData & EndLineDeltaMask) >> EndLineDeltaShift;
  }

  /// Return the end source line number.
  ///
  /// \returns The end source line number.
  uint32_t getEndLine() const { return getStartLine() + getLineDelta(); }

  /// Return true if this entry marks a statement boundary.
  ///
  /// \returns True if this entry marks a statement boundary.
  bool isStatement() const { return (LineData & StatementFlag) != 0; }

  /// Return the packed 32-bit CodeView line encoding.
  ///
  /// \returns The packed 32-bit CodeView line encoding.
  uint32_t getRawData() const { return LineData; }

  /// Return true if the start line is the always-step-into sentinel.
  ///
  /// \returns True if the start line is the always-step-into sentinel.
  bool isAlwaysStepInto() const {
    return getStartLine() == AlwaysStepIntoLineNumber;
  }

  /// Return true if the start line is the never-step-into sentinel.
  ///
  /// \returns True if the start line is the never-step-into sentinel.
  bool isNeverStepInto() const {
    return getStartLine() == NeverStepIntoLineNumber;
  }

private:
  uint32_t LineData;
};

/// Encoded start and end column numbers for a CodeView line entry.
class ColumnInfo {
private:
  static const uint32_t StartColumnMask = 0x0000ffffu;
  static const uint32_t EndColumnMask = 0xffff0000u;
  static const int EndColumnShift = 16;

public:
  /// Construct column info from explicit start and end column numbers.
  ///
  /// \param StartColumn Starting source column.
  /// \param EndColumn Ending source column.
  ColumnInfo(uint16_t StartColumn, uint16_t EndColumn) {
    ColumnData =
        (static_cast<uint32_t>(StartColumn) & StartColumnMask) |
        ((static_cast<uint32_t>(EndColumn) << EndColumnShift) & EndColumnMask);
  }

  /// Return the start source column.
  ///
  /// \returns The start source column.
  uint16_t getStartColumn() const {
    return static_cast<uint16_t>(ColumnData & StartColumnMask);
  }

  /// Return the end source column.
  ///
  /// \returns The end source column.
  uint16_t getEndColumn() const {
    return static_cast<uint16_t>((ColumnData & EndColumnMask) >>
                                 EndColumnShift);
  }

  /// Return the packed 32-bit column encoding.
  ///
  /// \returns The packed 32-bit column encoding.
  uint32_t getRawData() const { return ColumnData; }

private:
  uint32_t ColumnData;
};

/// A CodeView line mapping from a code offset to source line and column info.
class Line {
private:
  int32_t CodeOffset;
  LineInfo LineInf;
  ColumnInfo ColumnInf;

public:
  /// Construct a line entry from a code offset and explicit line/column fields.
  ///
  /// \param CodeOffset Offset of the code bytes corresponding to this line.
  /// \param StartLine Starting source line number.
  /// \param EndLine Ending source line number.
  /// \param StartColumn Starting source column.
  /// \param EndColumn Ending source column.
  /// \param IsStatement True if this entry marks a statement boundary.
  Line(int32_t CodeOffset, uint32_t StartLine, uint32_t EndLine,
       uint16_t StartColumn, uint16_t EndColumn, bool IsStatement)
      : CodeOffset(CodeOffset), LineInf(StartLine, EndLine, IsStatement),
        ColumnInf(StartColumn, EndColumn) {}

  /// Construct a line entry from a code offset and prebuilt line/column info.
  ///
  /// \param CodeOffset Offset of the code bytes corresponding to this line.
  /// \param LineInf Packed line number and statement information.
  /// \param ColumnInf Packed start and end column information.
  Line(int32_t CodeOffset, LineInfo LineInf, ColumnInfo ColumnInf)
      : CodeOffset(CodeOffset), LineInf(LineInf), ColumnInf(ColumnInf) {}

  /// Return the packed line number and statement information.
  ///
  /// \returns The packed line number and statement information.
  LineInfo getLineInfo() const { return LineInf; }

  /// Return the packed start and end column information.
  ///
  /// \returns The packed start and end column information.
  ColumnInfo getColumnInfo() const { return ColumnInf; }

  /// Return the code offset for this line entry.
  ///
  /// \returns The code offset for this line entry.
  int32_t getCodeOffset() const { return CodeOffset; }

  /// Return the start source line number.
  ///
  /// \returns The start source line number.
  uint32_t getStartLine() const { return LineInf.getStartLine(); }

  /// Return the delta from the start line to the end line.
  ///
  /// \returns The end-line delta from the packed line information.
  uint32_t getLineDelta() const { return LineInf.getLineDelta(); }

  /// Return the end source line number.
  ///
  /// \returns The end source line number.
  uint32_t getEndLine() const { return LineInf.getEndLine(); }

  /// Return the start source column.
  ///
  /// \returns The start source column.
  uint16_t getStartColumn() const { return ColumnInf.getStartColumn(); }

  /// Return the end source column.
  ///
  /// \returns The end source column.
  uint16_t getEndColumn() const { return ColumnInf.getEndColumn(); }

  /// Return true if this entry marks a statement boundary.
  ///
  /// \returns True if this entry marks a statement boundary.
  bool isStatement() const { return LineInf.isStatement(); }

  /// Return true if the start line is the always-step-into sentinel.
  ///
  /// \returns True if the start line is the always-step-into sentinel.
  bool isAlwaysStepInto() const { return LineInf.isAlwaysStepInto(); }

  /// Return true if the start line is the never-step-into sentinel.
  ///
  /// \returns True if the start line is the never-step-into sentinel.
  bool isNeverStepInto() const { return LineInf.isNeverStepInto(); }
};

} // namespace codeview
} // namespace llvm

#endif
