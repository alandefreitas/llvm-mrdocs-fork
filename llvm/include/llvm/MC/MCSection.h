//===- MCSection.h - Machine Code Sections ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the MCSection class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCSECTION_H
#define LLVM_MC_MCSECTION_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/SectionKind.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Compiler.h"
#include <cassert>
#include <utility>

namespace llvm {

class MCAsmInfo;
class MCAssembler;
class MCContext;
class MCExpr;
class MCFragment;
class MCObjectStreamer;
class MCSymbol;
class MCSection;
class MCSubtargetInfo;
class raw_ostream;
class Triple;

/// Contiguous piece of code or data within a section.
///
/// Its size is determined by \c MCAssembler::layout. All subclasses must have
/// trivial destructors.
class MCFragment {
  friend class MCAssembler;
  friend class MCStreamer;
  friend class MCObjectStreamer;
  friend class MCSection;

public:
  /// Discriminator for the kind of data or metadata this fragment encodes.
  enum FragmentType : uint8_t {
    FT_Data,          ///< Fixed-size data or instruction bytes.
    FT_Relaxable, ///< Instruction fragment that may be relaxed during layout.
    FT_Align,         ///< Alignment padding with optional fill or NOPs.
    FT_PrefAlign,     ///< Preferred-alignment padding for a following region.
    FT_Fill,          ///< Repeated fill value (\c .fill / \c .space).
    FT_LEB,           ///< SLEB128 or ULEB128 encoding of an expression.
    FT_Nops,          ///< Explicit NOP bytes from a \c .nops directive.
    FT_Org,           ///< Absolute org offset advance (\c .org).
    FT_Dwarf, ///< DWARF line-table address/line delta fragment.
    FT_DwarfFrame,    ///< DWARF \c .debug_frame address-delta encoding.
    FT_SFrame,        ///< SFrame FRE address-delta encoding.
    FT_BoundaryAlign, ///< Padding so a fragment set does not cross an alignment boundary.
    FT_SymbolId,      ///< Symbol table index reference.
    FT_CVInlineLines, ///< CodeView \c .cv_inline_linetable annotations.
    FT_CVDefRange,    ///< CodeView \c .cv_def_range record.
  };

private:
  // The next fragment within the section.
  MCFragment *Next = nullptr;

  /// The data for the section this fragment is in.
  MCSection *Parent = nullptr;

  /// The offset of this fragment in its section.
  uint64_t Offset = 0;

  /// The layout order of this fragment.
  unsigned LayoutOrder = 0;

  FragmentType Kind;

  //== Used by certain fragment types for better packing.

  // The number of fixups for the optional variable-size tail must be small.
  uint8_t VarFixupSize = 0;

  bool LinkerRelaxable : 1;

  /// FT_Data, FT_Relaxable
  bool HasInstructions : 1;
  /// FT_Relaxable, x86-specific
  bool AllowAutoPadding : 1;

  // Track content and fixups for the fixed-size part as fragments are
  // appended to the section. The content is stored as trailing data of the
  // MCFragment. The content remains immutable, except when modified by
  // applyFixup.
  uint32_t FixedSize = 0;
  uint32_t FixupStart = 0;
  uint32_t FixupEnd = 0;

  // Track content and fixups for the optional variable-size tail part,
  // typically modified during relaxation.
  uint32_t VarContentStart = 0;
  uint32_t VarContentEnd = 0;
  uint32_t VarFixupStart = 0;

protected:
  /// Subtarget info in effect when instructions in this fragment were encoded.
  const MCSubtargetInfo *STI = nullptr;

private:
  // Optional variable-size tail used by various fragment types.
  union Tail {
    struct {
      uint32_t Opcode;
      uint32_t Flags;
      uint32_t OperandStart;
      uint32_t OperandSize;
    } relax;
    struct {
      // The alignment to ensure, in bytes.
      Align Alignment;
      // The size of the integer (in bytes) of \p Value.
      uint8_t FillLen;
      // If true, fill with target-specific nop instructions.
      bool EmitNops;
      // The maximum number of bytes to emit; if the alignment
      // cannot be satisfied in this width then this fragment is ignored.
      unsigned MaxBytesToEmit;
      // Value to use for filling padding bytes.
      int64_t Fill;
    } align;
    struct {
      // Symbol denoting the end of the region; always non-null.
      const MCSymbol *End;
      // The preferred (maximum) alignment.
      Align PreferredAlign;
      // The alignment computed during relaxation.
      Align ComputedAlign;
      // If true, fill padding with target NOPs via writeNopData; the STI field
      // holds the subtarget info needed.  If false, fill with Fill byte.
      bool EmitNops;
      // Fill byte used when !EmitNops.
      uint8_t Fill;
    } prefalign;
    struct {
      // True if this is a sleb128, false if uleb128.
      bool IsSigned;
      // The value this fragment should contain.
      const MCExpr *Value;
    } leb;
    // Used by .debug_frame and .debug_line to encode an address difference.
    struct {
      // The address difference between two labels.
      const MCExpr *AddrDelta;
      // The value of the difference between the two line numbers between two
      // .loc dwarf directives.
      int64_t LineDelta;
    } dwarf;
    struct {
      // This FRE describes unwind info at AddrDelta from function start.
      const MCExpr *AddrDelta;
      // Fragment that records how many bytes of AddrDelta to emit.
      MCFragment *FDEFragment;
    } sframe;
  } u{};

public:
  /// Construct a fragment of \p Kind.
  ///
  /// When \p HasInstructions is true, the fragment is marked as containing
  /// emitted machine instructions (for example \c FT_Relaxable fragments).
  ///
  /// \param Kind - Fragment kind; defaults to \c FT_Data.
  /// \param HasInstructions - True if the fragment contains machine
  /// instructions.
  LLVM_ABI MCFragment(FragmentType Kind = MCFragment::FT_Data,
                      bool HasInstructions = false);
  /// Deleted copy constructor.
  ///
  /// \param Other - Unused; copy construction is deleted.
  MCFragment(const MCFragment &Other) = delete;
  /// Deleted copy assignment.
  ///
  /// \param Other - Unused; copy assignment is deleted.
  MCFragment &operator=(const MCFragment &Other) = delete;

  /// Return the next fragment in the section, or null if this is the last.
  ///
  /// \return The next fragment, or null if this is the last.
  MCFragment *getNext() const { return Next; }

  /// Return the kind discriminator for this fragment.
  ///
  /// \return The fragment kind discriminator.
  FragmentType getKind() const { return Kind; }

  /// Return the section that owns this fragment, or null if unattached.
  ///
  /// \return The parent section, or null if unattached.
  MCSection *getParent() const { return Parent; }
  /// Set the section that owns this fragment.
  ///
  /// \param Value - Parent section, or null to detach.
  void setParent(MCSection *Value) { Parent = Value; }

  /// Return the atom (defining symbol) for this fragment, if any.
  ///
  /// \return The defining symbol for this fragment, or null if none.
  LLVM_ABI const MCSymbol *getAtom() const;

  /// Return the layout order of this fragment within its section.
  ///
  /// \return The layout order index within the parent section.
  unsigned getLayoutOrder() const { return LayoutOrder; }
  /// Set the layout order of this fragment within its section.
  ///
  /// \param Value - New layout order index.
  void setLayoutOrder(unsigned Value) { LayoutOrder = Value; }

  /// Does this fragment have instructions emitted into it? By default
  /// this is false, but specific fragment types may set it to true.
  ///
  /// \return True if this fragment contains machine instructions.
  bool hasInstructions() const { return HasInstructions; }

  /// Print a human-readable description of this fragment to stderr.
  LLVM_ABI void dump() const;

  /// Retrieve the MCSubTargetInfo in effect when the instruction was encoded.
  /// Guaranteed to be non-null if hasInstructions() == true
  ///
  /// \return The subtarget info active when instructions were encoded.
  const MCSubtargetInfo *getSubtargetInfo() const { return STI; }

  /// Record that the fragment contains instructions with the MCSubtargetInfo in
  /// effect when the instruction was encoded.
  ///
  /// \param STI - Subtarget info active when the instructions were encoded.
  void setHasInstructions(const MCSubtargetInfo &STI) {
    HasInstructions = true;
    this->STI = &STI;
  }

  /// Return true if this fragment may be resized by linker relaxation.
  ///
  /// \return True if this fragment may be resized by linker relaxation.
  bool isLinkerRelaxable() const { return LinkerRelaxable; }
  /// Mark this fragment as subject to linker relaxation.
  void setLinkerRelaxable() { LinkerRelaxable = true; }

  /// Return true if x86 auto-padding is allowed for this relaxable fragment.
  ///
  /// \return True if x86 auto-padding is allowed for this relaxable fragment.
  bool getAllowAutoPadding() const { return AllowAutoPadding; }
  /// Set whether x86 auto-padding is allowed for this relaxable fragment.
  ///
  /// \param V - True to allow automatic padding.
  void setAllowAutoPadding(bool V) { AllowAutoPadding = V; }

  //== Content-related functions manage parent's storage using ContentStart and
  // ContentSize.

  /// Return a mutable view of this fragment's fixed-size contents.
  ///
  /// \return A mutable view of the fixed-size content bytes.
  MutableArrayRef<char> getContents();
  /// Return a read-only view of this fragment's fixed-size contents.
  ///
  /// \return A read-only view of the fixed-size content bytes.
  ArrayRef<char> getContents() const;

  /// Replace the variable-size content tail of this fragment.
  ///
  /// \param Contents - Bytes to store in the variable-size region.
  LLVM_ABI void setVarContents(ArrayRef<char> Contents);
  /// Clear the variable-size content tail of this fragment.
  void clearVarContents() { setVarContents({}); }
  /// Return a mutable view of this fragment's variable-size contents.
  ///
  /// \return A mutable view of the variable-size content bytes.
  MutableArrayRef<char> getVarContents();
  /// Return a read-only view of this fragment's variable-size contents.
  ///
  /// \return A read-only view of the variable-size content bytes.
  ArrayRef<char> getVarContents() const;

  /// Return the size in bytes of the fixed-size content region.
  ///
  /// \return The fixed-size content length in bytes.
  size_t getFixedSize() const { return FixedSize; }
  /// Return the size in bytes of the variable-size content region.
  ///
  /// \return The variable-size content length in bytes.
  size_t getVarSize() const { return VarContentEnd - VarContentStart; }
  /// Return the total size in bytes of fixed and variable content.
  ///
  /// \return The combined fixed and variable content length in bytes.
  size_t getSize() const {
    return FixedSize + (VarContentEnd - VarContentStart);
  }

  //== Fixup-related functions manage parent's storage using FixupStart and
  // FixupSize.
  /// Clear all fixups stored for this fragment.
  void clearFixups() { FixupEnd = FixupStart; }
  /// Append a single fixup for the fixed-size part of this fragment.
  ///
  /// \param Fixup - Fixup to append.
  LLVM_ABI void addFixup(MCFixup Fixup);
  /// Insert \c .reloc fixups using assembler layout ordering rules.
  ///
  /// Ordering follows \c MCAssembler::layout().
  ///
  /// \param Fixups - Reloc fixups to insert.
  LLVM_ABI void insertRelocFixups(ArrayRef<MCFixup> Fixups);
  /// Append fixups for the fixed-size part of this fragment.
  ///
  /// \param Fixups - Fixups to append.
  LLVM_ABI void appendFixups(ArrayRef<MCFixup> Fixups);
  /// Move this fragment's fixups to the end of the parent's fixup storage.
  LLVM_ABI void moveFixupsToEnd();
  /// Return a mutable view of fixups for the fixed-size content.
  ///
  /// \return A mutable view of fixups for the fixed-size content.
  MutableArrayRef<MCFixup> getFixups();
  /// Return a read-only view of fixups for the fixed-size content.
  ///
  /// \return A read-only view of fixups for the fixed-size content.
  ArrayRef<MCFixup> getFixups() const;

  // Source fixup offsets are relative to the variable part's start.
  // Stored fixup offsets are relative to the fixed part's start.
  /// Replace fixups for the variable-size content tail.
  ///
  /// Source fixup offsets are relative to the variable part's start; stored
  /// offsets are relative to the fixed part's start.
  ///
  /// \param Fixups - Fixups to store for the variable-size region.
  LLVM_ABI void setVarFixups(ArrayRef<MCFixup> Fixups);
  /// Clear fixups for the variable-size content tail.
  void clearVarFixups() { setVarFixups({}); }
  /// Return a mutable view of fixups for the variable-size content.
  ///
  /// \return A mutable view of fixups for the variable-size content.
  MutableArrayRef<MCFixup> getVarFixups();
  /// Return a read-only view of fixups for the variable-size content.
  ///
  /// \return A read-only view of fixups for the variable-size content.
  ArrayRef<MCFixup> getVarFixups() const;

  //== FT_Relaxable functions
  /// Return the opcode of the relaxable instruction in this fragment.
  ///
  /// \return The opcode of the relaxable instruction.
  unsigned getOpcode() const {
    assert(Kind == FT_Relaxable);
    return u.relax.Opcode;
  }
  /// Return the operands of the relaxable instruction in this fragment.
  ///
  /// \return The operands of the relaxable instruction.
  ArrayRef<MCOperand> getOperands() const;
  /// Return the relaxable instruction stored in this fragment.
  ///
  /// \return The relaxable instruction stored in this fragment.
  MCInst getInst() const;
  /// Replace the relaxable instruction stored in this fragment.
  ///
  /// \param Inst - Instruction to store (opcode, flags, and operands).
  void setInst(const MCInst &Inst);

  //== FT_Align functions
  /// Initialize this fragment as alignment padding with the given fill policy.
  ///
  /// \param Alignment - Alignment to ensure, in bytes.
  /// \param Fill - Value used to fill padding bytes.
  /// \param FillLen - Size in bytes of the integer \p Fill.
  /// \param MaxBytesToEmit - Maximum padding width; the fragment is ignored if
  /// alignment cannot be satisfied within this limit.
  void makeAlign(Align Alignment, int64_t Fill, uint8_t FillLen,
                 unsigned MaxBytesToEmit) {
    Kind = FT_Align;
    u.align.EmitNops = false;
    u.align.Alignment = Alignment;
    u.align.Fill = Fill;
    u.align.FillLen = FillLen;
    u.align.MaxBytesToEmit = MaxBytesToEmit;
  }

  /// Return the alignment this \c FT_Align fragment ensures.
  ///
  /// \return The alignment this \c FT_Align fragment ensures.
  Align getAlignment() const {
    assert(Kind == FT_Align);
    return u.align.Alignment;
  }
  /// Return the fill value used for alignment padding.
  ///
  /// \return The fill value used for alignment padding.
  int64_t getAlignFill() const {
    assert(Kind == FT_Align);
    return u.align.Fill;
  }
  /// Return the size in bytes of the alignment fill value.
  ///
  /// \return The size in bytes of the alignment fill value.
  uint8_t getAlignFillLen() const {
    assert(Kind == FT_Align);
    return u.align.FillLen;
  }
  /// Return the maximum number of padding bytes this align fragment may emit.
  ///
  /// \return The maximum number of padding bytes this align fragment may emit.
  unsigned getAlignMaxBytesToEmit() const {
    assert(Kind == FT_Align);
    return u.align.MaxBytesToEmit;
  }
  /// Return true if alignment padding is filled with target NOP instructions.
  ///
  /// \return True if alignment padding is filled with target NOP instructions.
  bool hasAlignEmitNops() const {
    assert(Kind == FT_Align);
    return u.align.EmitNops;
  }

  //== FT_PrefAlign functions
  /// Initialize this fragment as preferred-alignment padding ending at \p End.
  ///
  /// ComputedAlign is set during relaxation:
  ///   body_size < PrefAlign  => ComputedAlign = std::bit_ceil(body_size)
  ///   body_size >= PrefAlign => ComputedAlign = PrefAlign
  ///
  /// \param PrefAlign - Preferred (maximum) alignment for the region.
  /// \param End - Symbol denoting the end of the aligned region.
  /// \param EmitNops - True to fill with target NOPs; false to use \p Fill.
  /// \param Fill - Fill byte used when \p EmitNops is false.
  void makePrefAlign(Align PrefAlign, const MCSymbol &End, bool EmitNops,
                     uint8_t Fill) {
    Kind = FT_PrefAlign;
    u.prefalign.End = &End;
    u.prefalign.PreferredAlign = PrefAlign;
    u.prefalign.ComputedAlign = Align();
    u.prefalign.EmitNops = EmitNops;
    u.prefalign.Fill = Fill;
  }
  /// Return the end symbol of this preferred-alignment region.
  ///
  /// \return The end symbol of this preferred-alignment region.
  const MCSymbol &getPrefAlignEnd() const {
    assert(Kind == FT_PrefAlign);
    return *u.prefalign.End;
  }
  /// Return the preferred alignment for this \c FT_PrefAlign fragment.
  ///
  /// \return The preferred alignment for this \c FT_PrefAlign fragment.
  Align getPrefAlignPreferred() const {
    assert(Kind == FT_PrefAlign);
    return u.prefalign.PreferredAlign;
  }
  /// Return the alignment computed during relaxation for this fragment.
  ///
  /// \return The alignment computed during relaxation for this fragment.
  Align getPrefAlignComputed() const {
    assert(Kind == FT_PrefAlign);
    return u.prefalign.ComputedAlign;
  }
  /// Set the computed alignment for this \c FT_PrefAlign fragment.
  ///
  /// \param A - Alignment computed during relaxation.
  void setPrefAlignComputed(Align A) {
    assert(Kind == FT_PrefAlign);
    u.prefalign.ComputedAlign = A;
  }
  /// Return true if preferred-alignment padding is filled with target NOPs.
  ///
  /// \return True if preferred-alignment padding is filled with target NOPs.
  bool getPrefAlignEmitNops() const {
    assert(Kind == FT_PrefAlign);
    return u.prefalign.EmitNops;
  }
  /// Return the fill byte used when preferred-alignment padding is not NOPs.
  ///
  /// \return The fill byte used when preferred-alignment padding is not NOPs.
  uint8_t getPrefAlignFill() const {
    assert(Kind == FT_PrefAlign);
    return u.prefalign.Fill;
  }

  //== FT_LEB functions
  /// Convert this data fragment into an LEB128 encoding of \p Value.
  ///
  /// \param IsSigned - True for SLEB128; false for ULEB128.
  /// \param Value - Expression whose value is encoded as LEB128.
  void makeLEB(bool IsSigned, const MCExpr *Value) {
    assert(Kind == FT_Data);
    Kind = MCFragment::FT_LEB;
    u.leb.IsSigned = IsSigned;
    u.leb.Value = Value;
  }
  /// Return the expression encoded by this LEB fragment.
  ///
  /// \return The expression encoded by this LEB fragment.
  const MCExpr &getLEBValue() const {
    assert(Kind == FT_LEB);
    return *u.leb.Value;
  }
  /// Set the expression encoded by this LEB fragment.
  ///
  /// \param Expr - Expression whose value is encoded as LEB128.
  void setLEBValue(const MCExpr *Expr) {
    assert(Kind == FT_LEB);
    u.leb.Value = Expr;
  }
  /// Return true if this LEB fragment uses signed (SLEB128) encoding.
  ///
  /// \return True if this LEB fragment uses signed (SLEB128) encoding.
  bool isLEBSigned() const {
    assert(Kind == FT_LEB);
    return u.leb.IsSigned;
  }

  //== FT_DwarfFrame functions
  /// Return the address-delta expression for this DWARF fragment.
  ///
  /// \return The address-delta expression for this DWARF fragment.
  const MCExpr &getDwarfAddrDelta() const {
    assert(Kind == FT_Dwarf || Kind == FT_DwarfFrame);
    return *u.dwarf.AddrDelta;
  }
  /// Set the address-delta expression for this DWARF fragment.
  ///
  /// \param E - Expression giving the address difference between two labels.
  void setDwarfAddrDelta(const MCExpr *E) {
    assert(Kind == FT_Dwarf || Kind == FT_DwarfFrame);
    u.dwarf.AddrDelta = E;
  }
  /// Return the line-number delta encoded in this \c FT_Dwarf fragment.
  ///
  /// \return The line-number delta encoded in this \c FT_Dwarf fragment.
  int64_t getDwarfLineDelta() const {
    assert(Kind == FT_Dwarf);
    return u.dwarf.LineDelta;
  }
  /// Set the line-number delta encoded in this \c FT_Dwarf fragment.
  ///
  /// \param LineDelta - Difference between consecutive \c .loc line numbers.
  void setDwarfLineDelta(int64_t LineDelta) {
    assert(Kind == FT_Dwarf);
    u.dwarf.LineDelta = LineDelta;
  }

  //== FT_SFrame functions
  /// Return the address-delta expression for this \c FT_SFrame fragment.
  ///
  /// \return The address-delta expression for this \c FT_SFrame fragment.
  const MCExpr &getSFrameAddrDelta() const {
    assert(Kind == FT_SFrame);
    return *u.sframe.AddrDelta;
  }
  /// Set the address-delta expression for this \c FT_SFrame fragment.
  ///
  /// \param E - Expression giving the FRE address delta from function start.
  void setSFrameAddrDelta(const MCExpr *E) {
    assert(Kind == FT_SFrame);
    u.sframe.AddrDelta = E;
  }
  /// Return the FDE fragment that records how many AddrDelta bytes to emit.
  ///
  /// \return The FDE fragment that records how many AddrDelta bytes to emit.
  MCFragment *getSFrameFDE() const {
    assert(Kind == FT_SFrame);
    return u.sframe.FDEFragment;
  }
  /// Set the FDE fragment that records how many AddrDelta bytes to emit.
  ///
  /// \param F - FDE fragment associated with this SFrame FRE.
  void setSFrameFDE(MCFragment *F) {
    assert(Kind == FT_SFrame);
    u.sframe.FDEFragment = F;
  }
};

// MCFragment subclasses do not use the fixed-size part or variable-size tail of
// MCFragment. Instead, they encode content in a specialized way.

/// Fragment that fills a region with a repeated value (\c .fill / \c .space).
class MCFillFragment : public MCFragment {
  uint8_t ValueSize;
  /// Value to use for filling bytes.
  uint64_t Value;
  /// The number of bytes to insert.
  const MCExpr &NumValues;

  /// Source location of the directive that this fragment was created for.
  SMLoc Loc;

public:
  /// Construct a fill fragment repeating \p Value for \p NumValues units.
  ///
  /// \param Value - Fill pattern value.
  /// \param VSize - Size in bytes of each fill value unit.
  /// \param NumValues - Expression for how many value units to emit.
  /// \param Loc - Source location of the directive.
  MCFillFragment(uint64_t Value, uint8_t VSize, const MCExpr &NumValues,
                 SMLoc Loc)
      : MCFragment(FT_Fill), ValueSize(VSize), Value(Value),
        NumValues(NumValues), Loc(Loc) {}

  /// Return the fill pattern value.
  ///
  /// \return The fill pattern value.
  uint64_t getValue() const { return Value; }
  /// Return the size in bytes of each fill value unit.
  ///
  /// \return The size in bytes of each fill value unit.
  uint8_t getValueSize() const { return ValueSize; }
  /// Return the expression for how many value units to emit.
  ///
  /// \return The expression for how many value units to emit.
  const MCExpr &getNumValues() const { return NumValues; }

  /// Return the source location of the fill directive.
  ///
  /// \return The source location of the fill directive.
  SMLoc getLoc() const { return Loc; }

  /// Return true if \p F is an \c MCFillFragment.
  ///
  /// \param F - Fragment to test.
  /// \return True if \p F is an \c MCFillFragment.
  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_Fill;
  }
};

/// Fragment representing NOP bytes inserted by a .nops directive.
class MCNopsFragment : public MCFragment {
  /// The number of bytes to insert.
  int64_t Size;
  /// Maximum number of bytes allowed in each NOP instruction.
  int64_t ControlledNopLength;

  /// Source location of the directive that this fragment was created for.
  SMLoc Loc;

public:
  /// Construct a NOP fragment emitting \p NumBytes bytes using NOPs of at most
  /// \p ControlledNopLength bytes.
  ///
  /// \param NumBytes - Total number of NOP bytes to emit.
  /// \param ControlledNopLength - Maximum length of each NOP instruction.
  /// \param L - Source location of the \c .nops directive.
  /// \param STI - Subtarget info used when encoding NOP instructions.
  MCNopsFragment(int64_t NumBytes, int64_t ControlledNopLength, SMLoc L,
                 const MCSubtargetInfo &STI)
      : MCFragment(FT_Nops), Size(NumBytes),
        ControlledNopLength(ControlledNopLength), Loc(L) {
    this->STI = &STI;
  }

  /// Return the total number of NOP bytes to emit.
  ///
  /// \return The total number of NOP bytes to emit.
  int64_t getNumBytes() const { return Size; }
  /// Return the maximum length allowed for each NOP instruction.
  ///
  /// \return The maximum length allowed for each NOP instruction.
  int64_t getControlledNopLength() const { return ControlledNopLength; }

  /// Return the source location of the \c .nops directive.
  ///
  /// \return The source location of the \c .nops directive.
  SMLoc getLoc() const { return Loc; }

  /// Return true if \p F is an \c MCNopsFragment.
  ///
  /// \param F - Fragment to test.
  /// \return True if \p F is an \c MCNopsFragment.
  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_Nops;
  }
};

/// Fragment representing a \c .org directive that advances to an absolute
/// offset.
class MCOrgFragment : public MCFragment {
  /// Value to use for filling bytes.
  int8_t Value;

  /// The offset this fragment should start at.
  const MCExpr *Offset;

  /// Source location of the directive that this fragment was created for.
  SMLoc Loc;

public:
  /// Construct an \c .org fragment that fills to \p Offset with \p Value.
  ///
  /// \param Offset - Absolute offset expression for the fragment start.
  /// \param Value - Fill byte used while advancing to \p Offset.
  /// \param Loc - Source location of the directive.
  MCOrgFragment(const MCExpr &Offset, int8_t Value, SMLoc Loc)
      : MCFragment(FT_Org), Value(Value), Offset(&Offset), Loc(Loc) {}

  /// Return the fill offset expression for this \c .org fragment.
  ///
  /// \return The fill offset expression for this \c .org fragment.
  const MCExpr &getOffset() const { return *Offset; }
  /// Return the fill byte used while advancing to the org offset.
  ///
  /// \return The fill byte used while advancing to the org offset.
  uint8_t getValue() const { return Value; }

  /// Return the source location of the \c .org directive.
  ///
  /// \return The source location of the \c .org directive.
  SMLoc getLoc() const { return Loc; }

  /// Return true if \p F is an \c MCOrgFragment.
  ///
  /// \param F - Fragment to test.
  /// \return True if \p F is an \c MCOrgFragment.
  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_Org;
  }
};

/// Represents a symbol table index fragment.
class MCSymbolIdFragment : public MCFragment {
  const MCSymbol *Sym;

public:
  /// Construct a symbol-id fragment for \p Sym.
  ///
  /// \param Sym - Symbol whose table index is emitted.
  MCSymbolIdFragment(const MCSymbol *Sym) : MCFragment(FT_SymbolId), Sym(Sym) {}

  /// Return the symbol referenced by this fragment.
  ///
  /// \return The symbol referenced by this fragment.
  const MCSymbol *getSymbol() const { return Sym; }

  /// Return true if \p F is an \c MCSymbolIdFragment.
  ///
  /// \param F - Fragment to test.
  /// \return True if \p F is an \c MCSymbolIdFragment.
  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_SymbolId;
  }
};

/// Fragment representing the binary annotations produced by the
/// .cv_inline_linetable directive.
class MCCVInlineLineTableFragment : public MCFragment {
  unsigned SiteFuncId;
  unsigned StartFileId;
  unsigned StartLineNum;
  const MCSymbol *FnStartSym;
  const MCSymbol *FnEndSym;

  /// CodeViewContext has the real knowledge about this format, so let it access
  /// our members.
  friend class CodeViewContext;

public:
  /// Construct a CodeView inline line-table fragment.
  ///
  /// \param SiteFuncId - Function ID of the inlined call site.
  /// \param StartFileId - File ID for the start of the inline range.
  /// \param StartLineNum - Starting line number of the inline range.
  /// \param FnStartSym - Symbol at the start of the enclosing function.
  /// \param FnEndSym - Symbol at the end of the enclosing function.
  MCCVInlineLineTableFragment(unsigned SiteFuncId, unsigned StartFileId,
                              unsigned StartLineNum, const MCSymbol *FnStartSym,
                              const MCSymbol *FnEndSym)
      : MCFragment(FT_CVInlineLines), SiteFuncId(SiteFuncId),
        StartFileId(StartFileId), StartLineNum(StartLineNum),
        FnStartSym(FnStartSym), FnEndSym(FnEndSym) {}

  /// Return the symbol at the start of the enclosing function.
  ///
  /// \return The symbol at the start of the enclosing function.
  const MCSymbol *getFnStartSym() const { return FnStartSym; }
  /// Return the symbol at the end of the enclosing function.
  ///
  /// \return The symbol at the end of the enclosing function.
  const MCSymbol *getFnEndSym() const { return FnEndSym; }

  /// Return true if \p F is an \c MCCVInlineLineTableFragment.
  ///
  /// \param F - Fragment to test.
  /// \return True if \p F is an \c MCCVInlineLineTableFragment.
  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_CVInlineLines;
  }
};

/// Fragment representing the .cv_def_range directive.
class MCCVDefRangeFragment : public MCFragment {
  ArrayRef<std::pair<const MCSymbol *, const MCSymbol *>> Ranges;
  StringRef FixedSizePortion;

  /// CodeViewContext has the real knowledge about this format, so let it access
  /// our members.
  friend class CodeViewContext;

public:
  /// Construct a CodeView def-range fragment.
  ///
  /// \param Ranges - Inclusive symbol pairs describing address ranges.
  /// \param FixedSizePortion - Fixed-size trailing bytes of the record.
  MCCVDefRangeFragment(
      ArrayRef<std::pair<const MCSymbol *, const MCSymbol *>> Ranges,
      StringRef FixedSizePortion)
      : MCFragment(FT_CVDefRange), Ranges(Ranges.begin(), Ranges.end()),
        FixedSizePortion(FixedSizePortion) {}

  /// Return the address ranges covered by this def-range record.
  ///
  /// \return The address ranges covered by this def-range record.
  ArrayRef<std::pair<const MCSymbol *, const MCSymbol *>> getRanges() const {
    return Ranges;
  }

  /// Return the fixed-size trailing portion of this def-range record.
  ///
  /// \return The fixed-size trailing portion of this def-range record.
  StringRef getFixedSizePortion() const { return FixedSizePortion; }

  /// Return true if \p F is an \c MCCVDefRangeFragment.
  ///
  /// \param F - Fragment to test.
  /// \return True if \p F is an \c MCCVDefRangeFragment.
  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_CVDefRange;
  }
};

/// Padding fragment that keeps a following fragment set within one alignment
/// boundary.
///
/// Represents required padding such that a particular other set of fragments
/// does not cross a particular power-of-two boundary. The other fragments must
/// follow this one within the same section.
class MCBoundaryAlignFragment : public MCFragment {
  /// The alignment requirement of the branch to be aligned.
  Align AlignBoundary;
  /// The last fragment in the set of fragments to be aligned.
  const MCFragment *LastFragment = nullptr;
  /// The size of the fragment.  The size is lazily set during relaxation, and
  /// is not meaningful before that.
  uint64_t Size = 0;

  /// If true, align the last instruction in the fragment to the end of the
  /// fragment.
  bool AlignToEnd = false;

public:
  /// Construct a boundary-align fragment for \p AlignBoundary.
  ///
  /// \param AlignBoundary - Power-of-two boundary that must not be crossed.
  /// \param STI - Subtarget info used when emitting NOP padding.
  MCBoundaryAlignFragment(Align AlignBoundary, const MCSubtargetInfo &STI)
      : MCFragment(FT_BoundaryAlign), AlignBoundary(AlignBoundary) {
    this->STI = &STI;
  }

  /// Return the lazily computed size of this padding fragment.
  ///
  /// \return The lazily computed size of this padding fragment.
  uint64_t getSize() const { return Size; }
  /// Set the size of this padding fragment.
  ///
  /// \param Value - Size in bytes after relaxation.
  void setSize(uint64_t Value) { Size = Value; }

  /// Return the alignment boundary that must not be crossed.
  ///
  /// \return The alignment boundary that must not be crossed.
  Align getAlignment() const { return AlignBoundary; }
  /// Set the alignment boundary that must not be crossed.
  ///
  /// \param Value - New power-of-two alignment boundary.
  void setAlignment(Align Value) { AlignBoundary = Value; }

  /// Return whether the last instruction is aligned to the end of this fragment.
  ///
  /// \return True if the last instruction is aligned to the fragment end.
  bool isAlignToEnd() const { return AlignToEnd; }
  /// Set whether the last instruction is aligned to the end of this fragment.
  ///
  /// \param Value - True to align the last instruction to the fragment end.
  void setAlignToEnd(bool Value) { AlignToEnd = Value; }

  /// Return the last fragment in the aligned set, or null if unset.
  ///
  /// \return The last fragment in the aligned set, or null if unset.
  const MCFragment *getLastFragment() const { return LastFragment; }
  /// Set the last fragment in the aligned set.
  ///
  /// \param F - Last fragment to align, or null to clear; must share this
  /// section when non-null.
  void setLastFragment(const MCFragment *F) {
    assert(!F || getParent() == F->getParent());
    LastFragment = F;
  }

  /// Return true if \p F is an \c MCBoundaryAlignFragment.
  ///
  /// \param F - Fragment to test.
  /// \return True if \p F is an \c MCBoundaryAlignFragment.
  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_BoundaryAlign;
  }
};

/// Instances of this class represent a uniqued identifier for a section in the
/// current translation unit.  The MCContext class uniques and creates these.
class LLVM_ABI MCSection {
public:
  friend MCAssembler;
  friend MCObjectStreamer;
  friend class MCFragment;
  /// Sentinel unique ID meaning the section is not uniquely identified.
  static constexpr unsigned NonUniqueID = ~0U;

  /// Forward iterator over fragments in a section.
  struct iterator {
    /// Current fragment, or null for the end iterator.
    MCFragment *F = nullptr;
    /// Construct an end iterator.
    iterator() = default;
    /// Construct an iterator pointing at \p F.
    ///
    /// \param F - Fragment to point at, or null for end.
    explicit iterator(MCFragment *F) : F(F) {}
    /// Return the fragment this iterator points to.
    ///
    /// \return The fragment this iterator points to.
    MCFragment &operator*() const { return *F; }
    /// Return true if both iterators point to the same fragment.
    ///
    /// \param O - Other iterator to compare.
    /// \return True if both iterators point to the same fragment.
    bool operator==(const iterator &O) const { return F == O.F; }
    /// Return true if the iterators point to different fragments.
    ///
    /// \param O - Other iterator to compare.
    /// \return True if the iterators point to different fragments.
    bool operator!=(const iterator &O) const { return F != O.F; }
    /// Advance this iterator to the next fragment in the section.
    ///
    /// \return A reference to this iterator after advancing.
    iterator &operator++();
  };

  /// Intrusive singly-linked list of fragments for a subsection.
  struct FragList {
    /// First fragment in the list, or null if empty.
    MCFragment *Head = nullptr;
    /// Last fragment in the list, or null if empty.
    MCFragment *Tail = nullptr;
  };

private:
  // At parse time, this holds the fragment list of the current subsection. At
  // layout time, this holds the concatenated fragment lists of all subsections.
  // Null until the first fragment is added to this section.
  FragList *CurFragList = nullptr;
  // In many object file formats, this denotes the section symbol. In Mach-O,
  // this denotes an optional temporary label at the section start.
  MCSymbol *Begin;
  MCSymbol *End = nullptr;
  /// The alignment requirement of this section.
  Align Alignment;
  /// The section index in the assemblers section list.
  unsigned Ordinal = 0;
  // If not -1u, the first linker-relaxable fragment's order within the
  // subsection. When present, the offset between two locations crossing this
  // fragment may not be fully resolved.
  unsigned FirstLinkerRelaxable = -1u;

  /// Whether this section has had instructions emitted into it.
  bool HasInstructions : 1;

  bool IsRegistered : 1;

  bool IsText : 1;
  bool IsBss : 1;

  MCFragment DummyFragment;

  // Mapping from subsection number to fragment list. At layout time, the
  // subsection 0 list is replaced with concatenated fragments from all
  // subsections.
  SmallVector<std::pair<unsigned, FragList>, 1> Subsections;

  // Content and fixup storage for fragments
  SmallVector<char, 0> ContentStorage;
  SmallVector<MCFixup, 0> FixupStorage;
  SmallVector<MCOperand, 0> MCOperandStorage;

protected:
  // TODO Make Name private when possible.
  /// Section name as it appears in the object file.
  StringRef Name;

  /// Construct a section with the given name and kind flags.
  ///
  /// \param Name - Section name.
  /// \param IsText - True if this is a text (code) section.
  /// \param IsBss - True if this is a BSS (no-content) section.
  /// \param Begin - Optional begin symbol for the section.
  MCSection(StringRef Name, bool IsText, bool IsBss, MCSymbol *Begin);

public:
  /// Deleted copy constructor.
  ///
  /// \param Other - Unused; copy construction is deleted.
  MCSection(const MCSection &Other) = delete;
  /// Deleted copy assignment.
  ///
  /// \param Other - Unused; copy assignment is deleted.
  MCSection &operator=(const MCSection &Other) = delete;

  /// Return the name of this section.
  ///
  /// \return The name of this section.
  StringRef getName() const { return Name; }
  /// Return true if this is a text (code) section.
  ///
  /// \return True if this is a text (code) section.
  bool isText() const { return IsText; }

  /// Return the section begin symbol, if any.
  ///
  /// \return The section begin symbol, or null if none.
  MCSymbol *getBeginSymbol() { return Begin; }
  /// Return the section begin symbol, if any.
  ///
  /// \return The section begin symbol, or null if none.
  const MCSymbol *getBeginSymbol() const {
    return const_cast<MCSection *>(this)->getBeginSymbol();
  }
  /// Set the section begin symbol; may be called only once.
  ///
  /// \param Sym - Symbol that labels the start of this section.
  void setBeginSymbol(MCSymbol *Sym) {
    assert(!Begin);
    Begin = Sym;
  }
  /// Return the section end symbol, creating it in \p Ctx if needed.
  ///
  /// \param Ctx - Context used to create the end symbol when absent.
  /// \return The section end symbol.
  MCSymbol *getEndSymbol(MCContext &Ctx);
  /// Return true if an end symbol has already been created for this section.
  ///
  /// \return True if an end symbol has already been created for this section.
  bool hasEnded() const;

  /// Return the required alignment of this section.
  ///
  /// \return The required alignment of this section.
  Align getAlign() const { return Alignment; }
  /// Set the required alignment of this section.
  ///
  /// \param Value - New section alignment.
  void setAlignment(Align Value) { Alignment = Value; }

  /// Makes sure that Alignment is at least MinAlignment.
  ///
  /// \param MinAlignment - Minimum alignment to enforce for this section.
  void ensureMinAlignment(Align MinAlignment) {
    if (Alignment < MinAlignment)
      Alignment = MinAlignment;
  }

  /// Return this section's index in the assembler's section list.
  ///
  /// \return This section's index in the assembler's section list.
  unsigned getOrdinal() const { return Ordinal; }
  /// Set this section's index in the assembler's section list.
  ///
  /// \param Value - New section ordinal.
  void setOrdinal(unsigned Value) { Ordinal = Value; }

  /// Return true if instructions have been emitted into this section.
  ///
  /// \return True if instructions have been emitted into this section.
  bool hasInstructions() const { return HasInstructions; }
  /// Set whether instructions have been emitted into this section.
  ///
  /// \param Value - True if the section contains instructions.
  void setHasInstructions(bool Value) { HasInstructions = Value; }

  /// Return true if this section has been registered with the assembler.
  ///
  /// \return True if this section has been registered with the assembler.
  bool isRegistered() const { return IsRegistered; }
  /// Set whether this section has been registered with the assembler.
  ///
  /// \param Value - True if the section is registered.
  void setIsRegistered(bool Value) { IsRegistered = Value; }

  /// Return the layout order of the first linker-relaxable fragment, or
  /// \c ~0u if none.
  ///
  /// \return The layout order of the first linker-relaxable fragment, or
  /// \c ~0u if none.
  unsigned firstLinkerRelaxable() const { return FirstLinkerRelaxable; }
  /// Return true if this section contains any linker-relaxable fragments.
  ///
  /// \return True if this section contains any linker-relaxable fragments.
  bool isLinkerRelaxable() const { return FirstLinkerRelaxable != -1u; }
  /// Record the layout order of the first linker-relaxable fragment.
  ///
  /// \param Order - Fragment layout order within the subsection.
  void setFirstLinkerRelaxable(unsigned Order) { FirstLinkerRelaxable = Order; }

  /// Return the dummy fragment used as a sentinel for this section.
  ///
  /// \return The dummy fragment used as a sentinel for this section.
  MCFragment &getDummyFragment() { return DummyFragment; }

  /// Return the active fragment list for this section.
  ///
  /// \return The active fragment list for this section.
  FragList *curFragList() const { return CurFragList; }
  /// Return an iterator to the first fragment in this section.
  ///
  /// \return An iterator to the first fragment in this section.
  iterator begin() const { return iterator(CurFragList->Head); }
  /// Return an end iterator past the last fragment in this section.
  ///
  /// \return An end iterator past the last fragment in this section.
  iterator end() const { return {}; }

  /// Print a human-readable description of this section to stderr.
  ///
  /// \param FragToSyms - Optional map from fragments to symbols defined at
  /// them, used to annotate the dump.
  void dump(DenseMap<const MCFragment *, SmallVector<const MCSymbol *, 0>>
                *FragToSyms = nullptr) const;

  /// Check whether this section is "virtual", that is has no actual object
  /// file contents.
  ///
  /// \return True if this section has no actual object file contents.
  bool isBssSection() const { return IsBss; }
};

inline MutableArrayRef<char> MCFragment::getContents() {
  return {reinterpret_cast<char *>(this + 1), FixedSize};
}
inline ArrayRef<char> MCFragment::getContents() const {
  return {reinterpret_cast<const char *>(this + 1), FixedSize};
}

inline MutableArrayRef<char> MCFragment::getVarContents() {
  return MutableArrayRef(getParent()->ContentStorage)
      .slice(VarContentStart, VarContentEnd - VarContentStart);
}
inline ArrayRef<char> MCFragment::getVarContents() const {
  return ArrayRef(getParent()->ContentStorage)
      .slice(VarContentStart, VarContentEnd - VarContentStart);
}

//== Fixup-related functions manage parent's storage using FixupStart and
// FixupSize.
inline MutableArrayRef<MCFixup> MCFragment::getFixups() {
  return MutableArrayRef(getParent()->FixupStorage)
      .slice(FixupStart, FixupEnd - FixupStart);
}
inline ArrayRef<MCFixup> MCFragment::getFixups() const {
  return ArrayRef(getParent()->FixupStorage)
      .slice(FixupStart, FixupEnd - FixupStart);
}

inline MutableArrayRef<MCFixup> MCFragment::getVarFixups() {
  return MutableArrayRef(getParent()->FixupStorage)
      .slice(VarFixupStart, VarFixupSize);
}
inline ArrayRef<MCFixup> MCFragment::getVarFixups() const {
  return ArrayRef(getParent()->FixupStorage).slice(VarFixupStart, VarFixupSize);
}

//== FT_Relaxable functions
inline ArrayRef<MCOperand> MCFragment::getOperands() const {
  assert(Kind == FT_Relaxable);
  return MutableArrayRef(getParent()->MCOperandStorage)
      .slice(u.relax.OperandStart, u.relax.OperandSize);
}
inline MCInst MCFragment::getInst() const {
  assert(Kind == FT_Relaxable);
  MCInst Inst;
  Inst.setOpcode(u.relax.Opcode);
  Inst.setFlags(u.relax.Flags);
  Inst.setOperands(ArrayRef(getParent()->MCOperandStorage)
                       .slice(u.relax.OperandStart, u.relax.OperandSize));
  return Inst;
}
inline void MCFragment::setInst(const MCInst &Inst) {
  assert(Kind == FT_Relaxable);
  u.relax.Opcode = Inst.getOpcode();
  u.relax.Flags = Inst.getFlags();
  auto &S = getParent()->MCOperandStorage;
  if (Inst.getNumOperands() > u.relax.OperandSize) {
    u.relax.OperandStart = S.size();
    S.resize_for_overwrite(S.size() + Inst.getNumOperands());
  }
  u.relax.OperandSize = Inst.getNumOperands();
  llvm::copy(Inst, S.begin() + u.relax.OperandStart);
}

inline MCSection::iterator &MCSection::iterator::operator++() {
  F = F->Next;
  return *this;
}

} // end namespace llvm

#endif // LLVM_MC_MCSECTION_H
