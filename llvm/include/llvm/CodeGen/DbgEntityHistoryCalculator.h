//===- llvm/CodeGen/DbgEntityHistoryCalculator.h ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_DBGENTITYHISTORYCALCULATOR_H
#define LLVM_CODEGEN_DBGENTITYHISTORYCALCULATOR_H

#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineInstr.h"
#include <utility>

namespace llvm {

class DILocation;
class LexicalScopes;
class DINode;
class MachineFunction;
class TargetRegisterInfo;

/// Tracks relative instruction order within a machine function.
///
/// Meta instructions are given the same ordinal as the preceding non-meta
/// instruction. Class state is invalid if MF is modified after calling
/// initialize.
class InstructionOrdering {
public:
  /// Assign ordinals to every instruction in \p MF.
  ///
  /// \param MF Function whose instructions are numbered.
  LLVM_ABI void initialize(const MachineFunction &MF);

  /// Drop all recorded instruction ordinals.
  void clear() { InstNumberMap.clear(); }

  /// Check if instruction \p A comes before \p B, where \p A and \p B both
  /// belong to the MachineFunction passed to initialize().
  ///
  /// \param A First instruction to compare.
  /// \param B Second instruction to compare.
  /// \returns True if \p A precedes \p B in the recorded instruction order.
  LLVM_ABI bool isBefore(const MachineInstr *A, const MachineInstr *B) const;

private:
  /// Each instruction is assigned an order number.
  DenseMap<const MachineInstr *, unsigned> InstNumberMap;
};

/// For each user variable, keep a list of instruction ranges where this
/// variable is accessible. The variables are listed in order of appearance.
class DbgValueHistoryMap {
public:
  /// Index in the entry vector.
  typedef size_t EntryIndex;

  /// Special value to indicate that an entry is valid until the end of the
  /// function.
  static const EntryIndex NoEntry = std::numeric_limits<EntryIndex>::max();

  /// Specifies a change in a variable's debug value history.
  ///
  /// There exist two types of entries:
  ///
  /// * Debug value entry:
  ///
  ///   A new debug value becomes live. If the entry's \p EndIndex is \p NoEntry,
  ///   the value is valid until the end of the function. For other values, the
  ///   index points to the entry in the entry vector that ends this debug
  ///   value. The ending entry can either be an overlapping debug value, or
  ///   an instruction that clobbers the value.
  ///
  /// * Clobbering entry:
  ///
  ///   This entry's instruction clobbers one or more preceding
  ///   register-described debug values that have their end index
  ///   set to this entry's position in the entry vector.
  class Entry {
    friend DbgValueHistoryMap;

  public:
    /// Kind of history entry: a new debug value or a register clobber.
    enum EntryKind {
      /// A DBG_VALUE (or equivalent) that makes a new location live.
      DbgValue,
      /// An instruction that clobbers one or more preceding debug values.
      Clobber
    };

    /// Create a history entry for \p Instr of the given \p Kind.
    ///
    /// \param Instr Instruction associated with this history entry.
    /// \param Kind Whether this entry is a debug value or a clobber.
    Entry(const MachineInstr *Instr, EntryKind Kind)
        : Instr(Instr, Kind), EndIndex(NoEntry) {}

    /// Return the instruction associated with this entry.
    ///
    /// \returns The instruction associated with this entry.
    const MachineInstr *getInstr() const { return Instr.getPointer(); }

    /// Return the index of the entry that ends this debug value, or NoEntry.
    ///
    /// \returns The end-entry index, or NoEntry if still open.
    EntryIndex getEndIndex() const { return EndIndex; }

    /// Return whether this entry is a debug value or a clobber.
    ///
    /// \returns The kind of this history entry.
    EntryKind getEntryKind() const { return Instr.getInt(); }

    /// Return true if this entry is a clobbering instruction.
    ///
    /// \returns True if this entry is a clobber.
    bool isClobber() const { return getEntryKind() == Clobber; }

    /// Return true if this entry is a debug value.
    ///
    /// \returns True if this entry is a debug value.
    bool isDbgValue() const { return getEntryKind() == DbgValue; }

    /// Return true if this debug value has been closed by an end index.
    ///
    /// \returns True if an end index has been set.
    bool isClosed() const { return EndIndex != NoEntry; }

    /// Mark this debug value as ending at entry \p EndIndex.
    ///
    /// \param EndIndex Index of the overlapping debug value or clobber that
    ///        ends this entry.
    LLVM_ABI void endEntry(EntryIndex EndIndex);

  private:
    PointerIntPair<const MachineInstr *, 1, EntryKind> Instr;
    EntryIndex EndIndex;
  };

  /// Ordered list of history entries for one inlined entity.
  using Entries = SmallVector<Entry, 4>;

  /// Variable or label paired with its inlined-at location.
  using InlinedEntity = std::pair<const DINode *, const DILocation *>;

  /// Map from each inlined entity to its debug-value history entries.
  using EntriesMap = MapVector<InlinedEntity, Entries>;

private:
  EntriesMap VarEntries;

public:
  /// Record a new DBG_VALUE for \p Var at \p MI.
  ///
  /// \param Var Inlined entity whose history is updated.
  /// \param MI DBG_VALUE instruction that starts the new location.
  /// \param NewIndex Set to the index of the created entry when one is added.
  ///
  /// \returns true if a new entry was added; false if \p MI was coalesced with
  /// the preceding identical open DBG_VALUE.
  LLVM_ABI bool startDbgValue(InlinedEntity Var, const MachineInstr &MI,
                              EntryIndex &NewIndex);

  /// Record that \p MI clobbers register-described locations for \p Var.
  ///
  /// \param Var Inlined entity whose history is updated.
  /// \param MI Instruction that clobbers one or more of \p Var's locations.
  ///
  /// \returns Index of the clobber entry (reusing an existing one if \p MI was
  /// already recorded as the latest clobber).
  LLVM_ABI EntryIndex startClobber(InlinedEntity Var, const MachineInstr &MI);

  /// Return the history entry for \p Var at \p Index.
  ///
  /// \param Var Inlined entity whose entry vector is accessed.
  /// \param Index Position within that entity's entry vector.
  /// \returns Reference to the history entry at \p Index.
  Entry &getEntry(InlinedEntity Var, EntryIndex Index) {
    auto &Entries = VarEntries[Var];
    return Entries[Index];
  }

  /// Test whether a vector of entries features any non-empty locations. It
  /// could have no entries, or only DBG_VALUE $noreg entries.
  ///
  /// \param Entries History entries to inspect for a real location.
  /// \returns True if any entry describes a non-empty location.
  LLVM_ABI bool hasNonEmptyLocation(const Entries &Entries) const;

  /// Drop location ranges which exist entirely outside each variable's scope.
  ///
  /// \param MF Function providing instruction context for the histories.
  /// \param LScopes Lexical scopes used to decide which ranges are in-scope.
  /// \param Ordering Instruction order used to compare range endpoints.
  LLVM_ABI void trimLocationRanges(const MachineFunction &MF,
                                   LexicalScopes &LScopes,
                                   const InstructionOrdering &Ordering);

  /// Return true if no variable histories have been recorded.
  ///
  /// \returns True if no variable histories have been recorded.
  bool empty() const { return VarEntries.empty(); }

  /// Remove all recorded variable histories.
  void clear() { VarEntries.clear(); }

  /// Return an iterator to the first variable history.
  ///
  /// \returns Const iterator to the first variable history.
  EntriesMap::const_iterator begin() const { return VarEntries.begin(); }

  /// Return an iterator past the last variable history.
  ///
  /// \returns Const iterator past the last variable history.
  EntriesMap::const_iterator end() const { return VarEntries.end(); }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Dump the debug-value histories for function \p FuncName.
  ///
  /// \param FuncName Name printed in the dump header.
  LLVM_ABI LLVM_DUMP_METHOD void dump(StringRef FuncName) const;
#endif
};

/// Maps each inlined source-level label to its DBG_LABEL instruction.
///
/// The DBG_LABEL instruction could be used to generate a temporary (assembler)
/// label before it.
class DbgLabelInstrMap {
public:
  /// Label metadata paired with its inlined-at location.
  using InlinedEntity = std::pair<const DINode *, const DILocation *>;

  /// Map from each inlined label to the corresponding DBG_LABEL instruction.
  using InstrMap = MapVector<InlinedEntity, const MachineInstr *>;

private:
  InstrMap LabelInstr;

public:
  /// Record DBG_LABEL \p MI for inlined label \p Label.
  ///
  /// \param Label Inlined label entity being recorded.
  /// \param MI DBG_LABEL instruction associated with \p Label.
  LLVM_ABI void addInstr(InlinedEntity Label, const MachineInstr &MI);

  /// Return true if no label instructions have been recorded.
  ///
  /// \returns True if no label instructions have been recorded.
  bool empty() const { return LabelInstr.empty(); }

  /// Remove all recorded label instructions.
  void clear() { LabelInstr.clear(); }

  /// Return an iterator to the first label instruction mapping.
  ///
  /// \returns Const iterator to the first label instruction mapping.
  InstrMap::const_iterator begin() const { return LabelInstr.begin(); }

  /// Return an iterator past the last label instruction mapping.
  ///
  /// \returns Const iterator past the last label instruction mapping.
  InstrMap::const_iterator end() const { return LabelInstr.end(); }
};

/// Compute debug-value and debug-label histories for \p MF.
///
/// \param MF Function whose debug entities are analyzed.
/// \param TRI Target register info used to interpret register locations.
/// \param DbgValues Filled with per-variable debug-value history ranges.
/// \param DbgLabels Filled with the DBG_LABEL instruction for each label.
LLVM_ABI void calculateDbgEntityHistory(const MachineFunction *MF,
                                        const TargetRegisterInfo *TRI,
                                        DbgValueHistoryMap &DbgValues,
                                        DbgLabelInstrMap &DbgLabels);

} // end namespace llvm

#endif // LLVM_CODEGEN_DBGENTITYHISTORYCALCULATOR_H
