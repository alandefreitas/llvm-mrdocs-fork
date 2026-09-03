//===- MCObjectStreamer.h - MCStreamer Object File Interface ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCOBJECTSTREAMER_H
#define LLVM_MC_MCOBJECTSTREAMER_H

#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class MCContext;
class MCInst;
class MCObjectWriter;
class MCSymbol;
struct MCDwarfFrameInfo;
class MCAssembler;
class MCCodeEmitter;
class MCSubtargetInfo;
class MCExpr;
class MCAsmBackend;
class raw_ostream;
class raw_pwrite_stream;

/// Streaming object file generation interface.
///
/// This class provides an implementation of the MCStreamer interface which is
/// suitable for use with the assembler backend. Specific object file formats
/// are expected to subclass this interface to implement directives specific
/// to that file format or custom semantics expected by the object writer
/// implementation.
class LLVM_ABI MCObjectStreamer : public MCStreamer {
  std::unique_ptr<MCAssembler> Assembler;
  bool EmitEHFrame;
  bool EmitDebugFrame;
  bool EmitSFrame;

  struct PendingAssignment {
    MCSymbol *Symbol;
    const MCExpr *Value;
  };

  /// A list of conditional assignments we may need to emit if the target
  /// symbol is later emitted.
  DenseMap<const MCSymbol *, SmallVector<PendingAssignment, 1>>
      pendingAssignments;

  SmallVector<std::unique_ptr<uint8_t[]>, 0> FragStorage;
  // Available bytes in the current block for trailing data or new fragments.
  size_t FragSpace = 0;
  // Used to allocate special fragments that do not use MCFragment's fixed-size
  // part.
  BumpPtrAllocator SpecialFragAllocator;

  void addSpecialFragment(MCFragment *F);
  void emitInstToData(const MCInst &Inst, const MCSubtargetInfo &);
  void emitCFIStartProcImpl(MCDwarfFrameInfo &Frame) override;
  void emitCFIEndProcImpl(MCDwarfFrameInfo &Frame) override;

protected:
  /// True while inside a `.bundle_lock` / `.bundle_unlock` group.
  ///
  /// A section cannot be switched while a group is open, so no per-section
  /// state is needed.
  bool BundleLocked = false;

  /// Construct an object streamer.
  ///
  /// \param Context - MC context that owns symbols and sections.
  /// \param TAB - Assembler backend used for relaxation and object writing.
  /// \param OW - Object writer that emits the object file.
  /// \param Emitter - Code emitter used to encode instructions.
  MCObjectStreamer(MCContext &Context, std::unique_ptr<MCAsmBackend> TAB,
                   std::unique_ptr<MCObjectWriter> OW,
                   std::unique_ptr<MCCodeEmitter> Emitter);
  /// Destroy the object streamer.
  ~MCObjectStreamer() override;

public:
  /// state management
  void reset() override;

  /// Object streamers require the integrated assembler.
  ///
  /// \return Always true.
  bool isIntegratedAssemblerRequired() const override { return true; }

  /// Emit collected EH, debug, and/or SFrame unwind information.
  void emitFrames();
  /// Generate compact-unwind encodings for collected DWARF frames.
  void generateCompactUnwindEncodings();
  /// Emit a real CFI label into the object when emitting object files.
  ///
  /// \return The CFI label symbol that was emitted.
  MCSymbol *emitCFILabel() override;
  /// Select which CFI sections to emit.
  ///
  /// \param EH - True to emit `.eh_frame`.
  /// \param Debug - True to emit `.debug_frame`.
  /// \param SFrame - True to emit `.sframe`.
  void emitCFISections(bool EH, bool Debug, bool SFrame) override;

public:
  /// Register \p Sym with the assembler when it is used.
  ///
  /// \param Sym - Symbol that was used.
  void visitUsedSymbol(const MCSymbol &Sym) override;

  /// Return the assembler owned by this streamer.
  ///
  /// \return Reference to the assembler owned by this streamer.
  MCAssembler &getAssembler() { return *Assembler; }
  /// Return a pointer to the assembler owned by this streamer.
  ///
  /// \return Pointer to the assembler owned by this streamer.
  MCAssembler *getAssemblerPtr() override;

  /// Return true if a `.bundle_lock` group is currently open.
  ///
  /// \return True if a `.bundle_lock` group is currently open.
  bool isBundleLocked() const { return BundleLocked; }

  /// \name MCStreamer Interface
  /// @{

  /// Return a pointer to the end of the fixed-size contents of the current
  /// fragment.
  ///
  /// \return Pointer to the end of the fixed-size contents of the current
  /// fragment.
  uint8_t *getCurFragEnd() const {
    return reinterpret_cast<uint8_t *>(CurFrag + 1) + CurFrag->getFixedSize();
  }
  /// Allocate a new fragment storage block with room for \p Headroom bytes.
  ///
  /// \param Headroom - Minimum free bytes required after the fragment header.
  /// \return Pointer to the newly allocated fragment storage.
  MCFragment *allocFragSpace(size_t Headroom);
  /// Add a new fragment to the current section without a variable-size tail.
  void newFragment();

  /// Add a new special fragment of type \p FT to the current section and start
  /// a new empty fragment afterward.
  ///
  /// \param args - Constructor arguments forwarded to \p FT.
  /// \return Pointer to the constructed special fragment.
  template <typename FT, typename... Args>
  FT *newSpecialFragment(Args &&...args) {
    auto *F = new (SpecialFragAllocator.Allocate(sizeof(FT), alignof(FT)))
        FT(std::forward<Args>(args)...);
    addSpecialFragment(F);
    return F;
  }

  /// Ensure the current fragment has at least \p Headroom free bytes.
  ///
  /// Starts a new fragment if the remaining space is insufficient.
  ///
  /// \param Headroom - Minimum free bytes required in the current fragment.
  void ensureHeadroom(size_t Headroom);
  /// Append \p Contents to the fixed-size part of the current fragment.
  ///
  /// \param Contents - Bytes to append.
  void appendContents(ArrayRef<char> Contents);
  /// Append \p Num copies of \p Elt to the current fragment.
  ///
  /// \param Num - Number of bytes to append.
  /// \param Elt - Byte value repeated \p Num times.
  void appendContents(size_t Num, uint8_t Elt);
  /// Add a fixup to the current fragment.
  ///
  /// Call ensureHeadroom beforehand to ensure the fixup and appended content
  /// apply to the same fragment.
  ///
  /// \param Value - Expression relocated by the fixup.
  /// \param Kind - Fixup kind describing the relocation encoding.
  void addFixup(const MCExpr *Value, MCFixupKind Kind);

  /// Emit \p Symbol as a label at the current position.
  ///
  /// \param Symbol - Symbol to define as a label.
  /// \param Loc - Source location for diagnostics.
  void emitLabel(MCSymbol *Symbol, SMLoc Loc = SMLoc()) override;
  /// Emit \p Symbol as a label at \p Offset within fragment \p F.
  ///
  /// \param Symbol - Symbol to define as a label.
  /// \param Loc - Source location for diagnostics.
  /// \param F - Fragment that contains the label.
  /// \param Offset - Byte offset of the label within \p F.
  virtual void emitLabelAtPos(MCSymbol *Symbol, SMLoc Loc, MCFragment &F,
                              uint64_t Offset);
  /// Emit an assignment of \p Value to \p Symbol.
  ///
  /// \param Symbol - Symbol being assigned to.
  /// \param Value - Value assigned to \p Symbol.
  void emitAssignment(MCSymbol *Symbol, const MCExpr *Value) override;
  /// Emit an assignment of \p Value to \p Symbol only if \p Value is also
  /// emitted.
  ///
  /// \param Symbol - Symbol being assigned to.
  /// \param Value - Value assigned to \p Symbol.
  void emitConditionalAssignment(MCSymbol *Symbol,
                                 const MCExpr *Value) override;
  /// Emit the expression \p Value as a native integer of \p Size bytes.
  ///
  /// \param Value - Expression to emit.
  /// \param Size - Size of the integer (in bytes) to emit.
  /// \param Loc - Source location for diagnostics.
  void emitValueImpl(const MCExpr *Value, unsigned Size,
                     SMLoc Loc = SMLoc()) override;
  /// Emit \p Value encoded as unsigned LEB128.
  ///
  /// \param Value - Expression to encode.
  void emitULEB128Value(const MCExpr *Value) override;
  /// Emit \p Value encoded as signed LEB128.
  ///
  /// \param Value - Expression to encode.
  void emitSLEB128Value(const MCExpr *Value) override;
  /// Emit a weak reference from \p Alias to \p Target.
  ///
  /// \param Alias - Alias symbol being created.
  /// \param Target - Symbol being weakly referenced.
  void emitWeakReference(MCSymbol *Alias, const MCSymbol *Target) override;
  /// Update streamer state for a new active section.
  ///
  /// \param Section - Section being switched to.
  /// \param Subsection - Subsection index within \p Section.
  void changeSection(MCSection *Section, uint32_t Subsection = 0) override;
  /// Emit the given instruction into the current section.
  ///
  /// \param Inst - Instruction to emit.
  /// \param STI - Subtarget info in effect for \p Inst.
  void emitInstruction(const MCInst &Inst, const MCSubtargetInfo &STI) override;

  /// Emit an instruction to a special fragment, because this instruction
  /// can change its size during relaxation.
  ///
  /// \param Inst - Instruction to emit.
  /// \param STI - Subtarget info in effect for \p Inst.
  void emitInstToFragment(const MCInst &Inst, const MCSubtargetInfo &STI);

  /// Emit the bytes in \p Data into the current section.
  ///
  /// \param Data - Bytes to emit.
  void emitBytes(StringRef Data) override;
  /// Emit fill bytes until alignment \p Alignment is reached.
  ///
  /// \param Alignment - Alignment to reach.
  /// \param Fill - Value used when filling bytes.
  /// \param FillLen - Size in bytes of each fill unit.
  /// \param MaxBytesToEmit - Maximum bytes to emit, or 0 for unlimited.
  void emitValueToAlignment(Align Alignment, int64_t Fill = 0,
                            uint8_t FillLen = 1,
                            unsigned MaxBytesToEmit = 0) override;
  /// Emit NOPs until alignment \p ByteAlignment is reached.
  ///
  /// \param ByteAlignment - Alignment to reach.
  /// \param STI - Subtarget info used when emitting NOP encodings.
  /// \param MaxBytesToEmit - Maximum bytes to emit, or 0 for unlimited.
  void emitCodeAlignment(Align ByteAlignment, const MCSubtargetInfo &STI,
                         unsigned MaxBytesToEmit = 0) override;
  /// Align up to \p Alignment using NOPs or fill, not past symbol \p End.
  ///
  /// \param Alignment - Preferred alignment to reach.
  /// \param End - Symbol that must not be passed while padding.
  /// \param EmitNops - True to pad with NOPs rather than \p Fill.
  /// \param Fill - Fill byte used when \p EmitNops is false.
  /// \param STI - Subtarget info used when emitting NOPs.
  void emitPrefAlign(Align Alignment, const MCSymbol &End, bool EmitNops,
                     uint8_t Fill, const MCSubtargetInfo &STI) override;
  /// Emit copies of \p Value until byte offset \p Offset is reached.
  ///
  /// \param Offset - Offset expression to reach in the current section.
  /// \param Value - Fill byte value.
  /// \param Loc - Source location for diagnostics.
  void emitValueToOffset(const MCExpr *Offset, unsigned char Value,
                         SMLoc Loc) override;
  /// Emit a DWARF `.loc` directive.
  ///
  /// \param FileNo - Logical file number.
  /// \param Line - Source line number.
  /// \param Column - Source column number.
  /// \param Flags - DWARF location flags.
  /// \param Isa - Instruction-set architecture identifier.
  /// \param Discriminator - DWARF discriminator.
  /// \param FileName - File name associated with this location.
  /// \param Comment - Optional comment attached to the directive.
  void emitDwarfLocDirective(unsigned FileNo, unsigned Line, unsigned Column,
                             unsigned Flags, unsigned Isa,
                             unsigned Discriminator, StringRef FileName,
                             StringRef Comment = {}) override;
  /// Emit a raw DWARF line-address advance from \p LastLabel to \p Label.
  ///
  /// \param LineDelta - Change in line number since the previous entry.
  /// \param LastLabel - Previous address label, or null at the start.
  /// \param Label - Current address label.
  /// \param PointerSize - Size of a target address in bytes.
  void emitDwarfAdvanceLineAddr(int64_t LineDelta, const MCSymbol *LastLabel,
                                const MCSymbol *Label,
                                unsigned PointerSize) override;
  /// Emit the debug line end entry for \p Section.
  ///
  /// \param Section - Section whose line-table contribution is being ended.
  /// \param LastLabel - Last line-table label emitted for \p Section.
  /// \param EndLabel - Optional explicit end label; created if null.
  void emitDwarfLineEndEntry(MCSection *Section, MCSymbol *LastLabel,
                             MCSymbol *EndLabel = nullptr) override;
  /// Emit a DWARF frame address advance from \p LastLabel to \p Label.
  ///
  /// \param LastLabel - Previous frame address label.
  /// \param Label - Current frame address label.
  /// \param Loc - Source location for diagnostics.
  void emitDwarfAdvanceFrameAddr(const MCSymbol *LastLabel,
                                 const MCSymbol *Label, SMLoc Loc);
  /// Emit an SFrame function-offset delta from \p FunCabsel to \p FREBegin.
  ///
  /// \param FunCabsel - Function base address symbol.
  /// \param FREBegin - Frame-row entry begin symbol.
  /// \param FDEFrag - Fragment owning the related FDE.
  /// \param Loc - Source location for diagnostics.
  void emitSFrameCalculateFuncOffset(const MCSymbol *FunCabsel,
                                     const MCSymbol *FREBegin,
                                     MCFragment *FDEFrag, SMLoc Loc);
  /// Emit a CodeView `.cv_loc` directive.
  ///
  /// \param FunctionId - Function id of this location.
  /// \param FileNo - Logical file number.
  /// \param Line - Source line number.
  /// \param Column - Source column number.
  /// \param PrologueEnd - True if this location is a prologue end.
  /// \param IsStmt - True if this location is a recommended breakpoint.
  /// \param FileName - File name associated with this location.
  /// \param Loc - Source location for diagnostics.
  void emitCVLocDirective(unsigned FunctionId, unsigned FileNo, unsigned Line,
                          unsigned Column, bool PrologueEnd, bool IsStmt,
                          StringRef FileName, SMLoc Loc) override;
  /// Emit a CodeView `.cv_linetable` directive.
  ///
  /// \param FunctionId - Function id whose line table is emitted.
  /// \param Begin - Function start symbol.
  /// \param End - Function end symbol.
  void emitCVLinetableDirective(unsigned FunctionId, const MCSymbol *Begin,
                                const MCSymbol *End) override;
  /// Emit a CodeView `.cv_inline_linetable` directive.
  ///
  /// \param PrimaryFunctionId - Function id of the outermost function.
  /// \param SourceFileId - File id of the inline site.
  /// \param SourceLineNum - Line number of the inline site.
  /// \param FnStartSym - Start symbol of the inlined range.
  /// \param FnEndSym - End symbol of the inlined range.
  void emitCVInlineLinetableDirective(unsigned PrimaryFunctionId,
                                      unsigned SourceFileId,
                                      unsigned SourceLineNum,
                                      const MCSymbol *FnStartSym,
                                      const MCSymbol *FnEndSym) override;
  /// Emit a CodeView `.cv_def_range` directive.
  ///
  /// \param Ranges - Code ranges this definition covers.
  /// \param FixedSizePortion - Fixed-size def-range header bytes.
  void emitCVDefRangeDirective(
      ArrayRef<std::pair<const MCSymbol *, const MCSymbol *>> Ranges,
      StringRef FixedSizePortion) override;
  /// Emit a CodeView `.cv_stringtable` directive.
  void emitCVStringTableDirective() override;
  /// Emit a CodeView `.cv_filechecksums` directive.
  void emitCVFileChecksumsDirective() override;
  /// Emit a CodeView `.cv_filechecksumoffset` directive.
  ///
  /// \param FileNo - Logical file number whose checksum offset is emitted.
  void emitCVFileChecksumOffsetDirective(unsigned FileNo) override;
  /// Record a relocation described by the `.reloc` directive.
  ///
  /// \param Offset - Offset in the current section of the relocation.
  /// \param Name - Relocation-kind name.
  /// \param Expr - Optional relocation addend.
  /// \param Loc - Source location for diagnostics.
  void emitRelocDirective(const MCExpr &Offset, StringRef Name,
                          const MCExpr *Expr, SMLoc Loc = {}) override;
  /// Bring MCStreamer::emitFill overloads into scope.
  using MCStreamer::emitFill;
  /// Emit \p NumBytes bytes filled with \p FillValue.
  ///
  /// \param NumBytes - Number of bytes to emit.
  /// \param FillValue - Value used when filling bytes.
  /// \param Loc - Source location for diagnostics.
  void emitFill(const MCExpr &NumBytes, uint64_t FillValue,
                SMLoc Loc = SMLoc()) override;
  /// Emit \p NumValues copies of \p Size bytes taken from \p Expr.
  ///
  /// \param NumValues - Number of copies of \p Size bytes to emit.
  /// \param Size - Size in bytes of each repeated value.
  /// \param Expr - Expression from which \p Size bytes are taken.
  /// \param Loc - Source location for diagnostics.
  void emitFill(const MCExpr &NumValues, int64_t Size, int64_t Expr,
                SMLoc Loc = SMLoc()) override;
  /// Emit \p NumBytes bytes of NOP instructions.
  ///
  /// \param NumBytes - Number of bytes of NOPs to emit.
  /// \param ControlledNopLength - Preferred NOP length, or 0 for the default.
  /// \param Loc - Source location for diagnostics.
  /// \param STI - Subtarget info used to choose NOP encodings.
  void emitNops(int64_t NumBytes, int64_t ControlledNopLength, SMLoc Loc,
                const MCSubtargetInfo &STI) override;
  /// Switch to a new logical file via a `.file` directive.
  ///
  /// \param Filename - Logical source file name.
  void emitFileDirective(StringRef Filename) override;
  /// Emit a `.file` directive with additional metadata.
  ///
  /// \param Filename - Logical source file name.
  /// \param CompilerVersion - Compiler version string.
  /// \param TimeStamp - Timestamp string.
  /// \param Description - Additional description string.
  void emitFileDirective(StringRef Filename, StringRef CompilerVersion,
                         StringRef TimeStamp, StringRef Description) override;

  /// Emit a `.addrsig` directive starting an address-significance table.
  void emitAddrsig() override;
  /// Add \p Sym to the address-significance table.
  ///
  /// \param Sym - Symbol marked address-significant.
  void emitAddrsigSym(const MCSymbol *Sym) override;

  /// Perform object-streamer finalization before finishing the object.
  void finishImpl() override;

  /// Emit the absolute difference between two symbols if possible.
  ///
  /// Emit the absolute difference between \c Hi and \c Lo, as long as we can
  /// compute it.  Currently, that requires that both symbols are in the same
  /// data fragment and that the target has not specified that diff expressions
  /// require relocations to be emitted. Otherwise, do nothing and return
  /// \c false.
  ///
  /// \pre Offset of \c Hi is greater than the offset \c Lo.
  /// \param Hi - Symbol with the greater offset.
  /// \param Lo - Symbol with the lesser offset.
  /// \param Size - Size of the difference (in bytes) to emit.
  void emitAbsoluteSymbolDiff(const MCSymbol *Hi, const MCSymbol *Lo,
                              unsigned Size) override;

  /// Emit the absolute difference between two symbols encoded as ULEB128.
  ///
  /// \param Hi - Symbol with the greater offset.
  /// \param Lo - Symbol with the lesser offset.
  void emitAbsoluteSymbolDiffAsULEB128(const MCSymbol *Hi,
                                       const MCSymbol *Lo) override;

  /// Return true if \p Sec may contain instructions.
  ///
  /// \param Sec - Section to query.
  /// \return True if \p Sec may contain instructions.
  bool mayHaveInstructions(MCSection &Sec) const override;

  /// Emits pending conditional assignments that depend on \p Symbol
  /// being emitted.
  ///
  /// \param Symbol - Symbol whose pending conditional assignments are emitted.
  void emitPendingAssignments(MCSymbol *Symbol);
};

} // end namespace llvm

#endif
