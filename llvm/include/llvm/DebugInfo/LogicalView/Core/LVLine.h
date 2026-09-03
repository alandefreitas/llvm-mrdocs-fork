//===-- LVLine.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the LVLine class, which is used to describe a debug
// information line.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVLINE_H
#define LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVLINE_H

#include "llvm/DebugInfo/LogicalView/Core/LVElement.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
namespace logicalview {

/// Kind flags that describe a logical-view line record.
enum class LVLineKind {
  /// Line marks the start of a basic block.
  IsBasicBlock,
  /// Line carries a DWARF or CodeView discriminator.
  IsDiscriminator,
  /// Line marks the end of a line-number sequence.
  IsEndSequence,
  /// Line marks the beginning of the function epilogue.
  IsEpilogueBegin,
  /// Line comes from DWARF debug line information.
  IsLineDebug,
  /// Line comes from assembler text extracted from the binary.
  IsLineAssembler,
  /// Line starts a new source statement.
  ///
  /// Shared with the CodeView 'IsStatement' flag.
  IsNewStatement,
  /// Line marks the end of the function prologue.
  IsPrologueEnd,
  /// Line is a CodeView always-step-into location.
  IsAlwaysStepInto,
  /// Line is a CodeView never-step-into location.
  IsNeverStepInto,
  /// Sentinel past the last valid kind.
  LastEntry
};
/// Set of selected LVLineKind values.
using LVLineKindSet = std::set<LVLineKind>;
/// Map from LVLineKind to the corresponding getter member function.
using LVLineDispatch = std::map<LVLineKind, LVLineGetFunction>;
/// Ordered list of LVLine getter member functions used for requests.
using LVLineRequest = std::vector<LVLineGetFunction>;

/// Logical-view element that represents a debug or assembler source line.
class LLVM_ABI LVLine : public LVElement {
  // Typed bitvector with kinds for this line.
  LVProperties<LVLineKind> Kinds;
  static LVLineDispatch Dispatch;

  // Find the current line in the given 'Targets'.
  LVLine *findIn(const LVLines *Targets) const;

public:
  /// Construct a logical line element and mark it for printing.
  LVLine() : LVElement(LVSubclassID::LV_LINE) {
    setIsLine();
    setIncludeInPrint();
  }
  /// Copy construction is not allowed.
  /// \param Other Unused source logical line.
  LVLine(const LVLine &Other) = delete;
  /// Copy assignment is not allowed.
  /// \param Other Unused source logical line.
  LVLine &operator=(const LVLine &Other) = delete;
  /// Destroy the logical line.
  ~LVLine() override = default;

  /// Return true when \p Element is an LVLine.
  /// \param Element Element to test for the LVLine subclass.
  /// \returns True if \p Element has subclass ID LV_LINE.
  static bool classof(const LVElement *Element) {
    return Element->getSubclassID() == LVSubclassID::LV_LINE;
  }

  /// Return whether this line marks a basic-block start.
  /// \returns True when the basic-block kind is set.
  bool getIsBasicBlock() const { return Kinds.get(LVLineKind::IsBasicBlock); }
  /// Mark this line as a basic-block start.
  void setIsBasicBlock() { Kinds.set(LVLineKind::IsBasicBlock); }
  /// Clear the basic-block kind on this line.
  void resetIsBasicBlock() { Kinds.reset(LVLineKind::IsBasicBlock); }
  /// Return whether this line carries a discriminator.
  /// \returns True when the discriminator kind is set.
  bool getIsDiscriminator() const {
    return Kinds.get(LVLineKind::IsDiscriminator);
  }
  /// Mark this line as carrying a discriminator.
  void setIsDiscriminator() { Kinds.set(LVLineKind::IsDiscriminator); }
  /// Clear the discriminator kind on this line.
  void resetIsDiscriminator() { Kinds.reset(LVLineKind::IsDiscriminator); }
  /// Return whether this line marks the end of a sequence.
  /// \returns True when the end-sequence kind is set.
  bool getIsEndSequence() const { return Kinds.get(LVLineKind::IsEndSequence); }
  /// Mark this line as the end of a sequence.
  void setIsEndSequence() { Kinds.set(LVLineKind::IsEndSequence); }
  /// Clear the end-sequence kind on this line.
  void resetIsEndSequence() { Kinds.reset(LVLineKind::IsEndSequence); }
  /// Return whether this line marks the epilogue begin.
  /// \returns True when the epilogue-begin kind is set.
  bool getIsEpilogueBegin() const {
    return Kinds.get(LVLineKind::IsEpilogueBegin);
  }
  /// Mark this line as the epilogue begin.
  void setIsEpilogueBegin() { Kinds.set(LVLineKind::IsEpilogueBegin); }
  /// Clear the epilogue-begin kind on this line.
  void resetIsEpilogueBegin() { Kinds.reset(LVLineKind::IsEpilogueBegin); }
  /// Return whether this line comes from DWARF debug line info.
  /// \returns True when the debug-line kind is set.
  bool getIsLineDebug() const { return Kinds.get(LVLineKind::IsLineDebug); }
  /// Mark this line as coming from DWARF debug line info.
  void setIsLineDebug() { Kinds.set(LVLineKind::IsLineDebug); }
  /// Clear the debug-line kind on this line.
  void resetIsLineDebug() { Kinds.reset(LVLineKind::IsLineDebug); }
  /// Return whether this line comes from assembler text.
  /// \returns True when the assembler-line kind is set.
  bool getIsLineAssembler() const {
    return Kinds.get(LVLineKind::IsLineAssembler);
  }
  /// Mark this line as coming from assembler text.
  void setIsLineAssembler() { Kinds.set(LVLineKind::IsLineAssembler); }
  /// Clear the assembler-line kind on this line.
  void resetIsLineAssembler() { Kinds.reset(LVLineKind::IsLineAssembler); }
  /// Return whether this line starts a new statement.
  /// \returns True when the new-statement kind is set.
  bool getIsNewStatement() const {
    return Kinds.get(LVLineKind::IsNewStatement);
  }
  /// Mark this line as the start of a new statement.
  void setIsNewStatement() { Kinds.set(LVLineKind::IsNewStatement); }
  /// Clear the new-statement kind on this line.
  void resetIsNewStatement() { Kinds.reset(LVLineKind::IsNewStatement); }
  /// Return whether this line marks the prologue end.
  /// \returns True when the prologue-end kind is set.
  bool getIsPrologueEnd() const { return Kinds.get(LVLineKind::IsPrologueEnd); }
  /// Mark this line as the prologue end.
  void setIsPrologueEnd() { Kinds.set(LVLineKind::IsPrologueEnd); }
  /// Clear the prologue-end kind on this line.
  void resetIsPrologueEnd() { Kinds.reset(LVLineKind::IsPrologueEnd); }
  /// Return whether this line is a CodeView always-step-into location.
  /// \returns True when the always-step-into kind is set.
  bool getIsAlwaysStepInto() const {
    return Kinds.get(LVLineKind::IsAlwaysStepInto);
  }
  /// Mark this line as a CodeView always-step-into location.
  void setIsAlwaysStepInto() { Kinds.set(LVLineKind::IsAlwaysStepInto); }
  /// Clear the always-step-into kind on this line.
  void resetIsAlwaysStepInto() { Kinds.reset(LVLineKind::IsAlwaysStepInto); }
  /// Return whether this line is a CodeView never-step-into location.
  /// \returns True when the never-step-into kind is set.
  bool getIsNeverStepInto() const {
    return Kinds.get(LVLineKind::IsNeverStepInto);
  }
  /// Mark this line as a CodeView never-step-into location.
  void setIsNeverStepInto() { Kinds.set(LVLineKind::IsNeverStepInto); }
  /// Clear the never-step-into kind on this line.
  void resetIsNeverStepInto() { Kinds.reset(LVLineKind::IsNeverStepInto); }

  /// Return a string naming the kind of this line.
  /// \returns C string describing the line kind.
  const char *kind() const override;

  /// Return the code address stored for this line.
  ///
  /// Uses the offset field to store the line address.
  /// \returns Address associated with this line.
  uint64_t getAddress() const { return getOffset(); }
  /// Store \p address as the code address for this line.
  ///
  /// Uses the offset field to store the line address.
  /// \param address Code address to associate with this line.
  void setAddress(uint64_t address) { setOffset(address); }

  /// Return the placeholder string used when no line number is available.
  /// \param ShowZero Whether to show zero instead of a blank placeholder.
  /// \returns Display string for a missing line number.
  std::string noLineAsString(bool ShowZero = false) const override;

  /// Format this line's number for display.
  ///
  /// For inlined functions, uses the DW_AT_call_line attribute; otherwise
  /// uses the DW_AT_decl_line attribute.
  /// \param ShowZero Whether to show a zero line number instead of padding.
  /// \returns Formatted line-number string for display.
  std::string lineNumberAsString(bool ShowZero = false) const override {
    return lineAsString(getLineNumber(), getDiscriminator(), ShowZero);
  }

  /// Return the shared dispatch map from line kinds to getters.
  /// \returns Reference to the static LVLineDispatch table.
  static LVLineDispatch &getDispatch() { return Dispatch; }

  /// Mark parents of reference lines that are missing from the targets.
  ///
  /// Iterate through the \p References set and check that all its elements
  /// are present in the \p Targets set. For a missing element, mark its
  /// parents as missing.
  /// \param References Lines expected to appear in the target set.
  /// \param Targets Lines available for matching.
  static void markMissingParents(const LVLines *References,
                                 const LVLines *Targets);

  /// Return true if this line is logically equal to \p Line.
  /// \param Line Line to compare against.
  /// \returns True when the lines are logically equal.
  virtual bool equals(const LVLine *Line) const;

  /// Return true if \p References are logically equal to \p Targets.
  /// \param References Reference line set.
  /// \param Targets Target line set.
  /// \returns True when both sets are logically equal.
  static bool equals(const LVLines *References, const LVLines *Targets);

  /// Report this line as missing or added during comparison.
  /// \param Pass Comparison pass that classifies the line.
  void report(LVComparePass Pass) override;

  /// Print this line to \p OS.
  /// \param OS Stream that receives the printed line.
  /// \param Full Whether to include full detail.
  void print(raw_ostream &OS, bool Full = true) const override;
  /// Print line-specific extra details to \p OS.
  /// \param OS Stream that receives the printed details.
  /// \param Full Whether to include full detail.
  void printExtra(raw_ostream &OS, bool Full = true) const override {}
};

/// Logical line representing a DWARF debug line record.
class LLVM_ABI LVLineDebug final : public LVLine {
  // Discriminator value (DW_LNE_set_discriminator). The DWARF standard
  // defines the discriminator as an unsigned LEB128 integer.
  uint32_t Discriminator = 0;

public:
  /// Construct a DWARF debug line and mark it as such.
  LVLineDebug() : LVLine() { setIsLineDebug(); }
  /// Copy construction is not allowed.
  /// \param Other Unused source debug line.
  LVLineDebug(const LVLineDebug &Other) = delete;
  /// Copy assignment is not allowed.
  /// \param Other Unused source debug line.
  LVLineDebug &operator=(const LVLineDebug &Other) = delete;
  /// Destroy the DWARF debug line.
  ~LVLineDebug() override = default;

  /// Format additional state attributes for this debug line.
  ///
  /// Includes attributes that describe states in the machine instructions
  /// (basic block, end prologue, and similar).
  /// \param Formatted Whether to apply display formatting.
  /// \returns String describing the line's state flags.
  std::string statesInfo(bool Formatted) const;

  /// Return the DW_LNE_set_discriminator value.
  /// \returns Discriminator associated with this line.
  uint32_t getDiscriminator() const override { return Discriminator; }
  /// Set the DW_LNE_set_discriminator value to \p Value.
  /// \param Value Discriminator to store on this line.
  void setDiscriminator(uint32_t Value) override {
    Discriminator = Value;
    setIsDiscriminator();
  }

  /// Return true if this line is logically equal to \p Line.
  /// \param Line Line to compare against.
  /// \returns True when the lines are logically equal.
  bool equals(const LVLine *Line) const override;

  /// Print DWARF debug line-specific details to \p OS.
  /// \param OS Stream that receives the printed details.
  /// \param Full Whether to include full detail.
  void printExtra(raw_ostream &OS, bool Full = true) const override;
};

/// Logical line representing assembler text extracted from a text section.
class LLVM_ABI LVLineAssembler final : public LVLine {
public:
  /// Construct an assembler line and mark it as such.
  LVLineAssembler() : LVLine() { setIsLineAssembler(); }
  /// Copy construction is not allowed.
  /// \param Other Unused source assembler line.
  LVLineAssembler(const LVLineAssembler &Other) = delete;
  /// Copy assignment is not allowed.
  /// \param Other Unused source assembler line.
  LVLineAssembler &operator=(const LVLineAssembler &Other) = delete;
  /// Destroy the assembler line.
  ~LVLineAssembler() override = default;

  /// Return blanks in place of a line number for assembler lines.
  /// \param ShowZero Unused; assembler lines always print blanks.
  /// \returns Fixed-width blank string used as the line-number field.
  std::string noLineAsString(bool ShowZero) const override {
    return std::string(8, ' ');
  };

  /// Return true if this line is logically equal to \p Line.
  /// \param Line Line to compare against.
  /// \returns True when the lines are logically equal.
  bool equals(const LVLine *Line) const override;

  /// Print assembler line-specific details to \p OS.
  /// \param OS Stream that receives the printed details.
  /// \param Full Whether to include full detail.
  void printExtra(raw_ostream &OS, bool Full = true) const override;
};

} // end namespace logicalview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVLINE_H
