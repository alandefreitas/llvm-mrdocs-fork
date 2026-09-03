//===- MCWinCOFFStreamer.h - COFF Object File Interface ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCWINCOFFSTREAMER_H
#define LLVM_MC_MCWINCOFFSTREAMER_H

#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCObjectStreamer.h"

namespace llvm {

class MCAsmBackend;
class MCContext;
class MCCodeEmitter;
class MCInst;
class MCSection;
class MCSubtargetInfo;
class MCSymbol;
class StringRef;
class WinCOFFObjectWriter;
class raw_pwrite_stream;

/// Streaming Windows COFF object file generation interface.
class LLVM_ABI MCWinCOFFStreamer : public MCObjectStreamer {
public:
  /// Construct a Windows COFF object streamer.
  ///
  /// \param Context - MC context that owns symbols and sections.
  /// \param MAB - Assembler backend used for relaxation and object writing.
  /// \param CE - Code emitter used to encode instructions.
  /// \param OW - Object writer that emits the COFF object.
  MCWinCOFFStreamer(MCContext &Context, std::unique_ptr<MCAsmBackend> MAB,
                    std::unique_ptr<MCCodeEmitter> CE,
                    std::unique_ptr<MCObjectWriter> OW);

  /// state management
  void reset() override {
    CurSymbol = nullptr;
    MCObjectStreamer::reset();
  }

  /// Return the Windows COFF object writer used by this streamer.
  ///
  /// \return The Windows COFF object writer used by this streamer.
  WinCOFFObjectWriter &getWriter();

  /// \name MCStreamer interface
  /// \{

  /// Create the default sections and set the initial one.
  ///
  /// \param STI - Subtarget info used to initialize sections.
  void initSections(const MCSubtargetInfo &STI) override;
  /// Update streamer state for a new active section.
  ///
  /// \param Section - Section being switched to.
  /// \param Subsection - Subsection index within \p Section.
  void changeSection(MCSection *Section, uint32_t Subsection = 0) override;
  /// Emit \p Symbol as a label at the current position.
  ///
  /// \param Symbol - Symbol to define as a label.
  /// \param Loc - Source location for diagnostics.
  void emitLabel(MCSymbol *Symbol, SMLoc Loc = SMLoc()) override;
  /// Add the given \p Attribute to \p Symbol.
  ///
  /// \param Symbol - Symbol to attribute.
  /// \param Attribute - Attribute to add.
  /// \return True if the attribute was applied.
  bool emitSymbolAttribute(MCSymbol *Symbol, MCSymbolAttr Attribute) override;
  /// Set the \p DescValue for the \p Symbol.
  ///
  /// \param Symbol - The symbol to have its n_desc field set.
  /// \param DescValue - The value to set into the n_desc field.
  void emitSymbolDesc(MCSymbol *Symbol, unsigned DescValue) override;
  /// Start emitting a COFF symbol definition.
  ///
  /// \param Symbol - The symbol to have its External & Type fields set.
  void beginCOFFSymbolDef(MCSymbol const *Symbol) override;
  /// Emit the storage class of the symbol under definition.
  ///
  /// \param StorageClass - The storage class the symbol should have.
  void emitCOFFSymbolStorageClass(int StorageClass) override;
  /// Emit the type of the symbol under definition.
  ///
  /// \param Type - A COFF type identifier (see COFF::SymbolType in X86COFF.h)
  void emitCOFFSymbolType(int Type) override;
  /// Mark the end of the current COFF symbol definition.
  void endCOFFSymbolDef() override;
  /// Emit a COFF .safeseh directive for \p Symbol.
  ///
  /// \param Symbol - Symbol registered as SafeSEH-compatible.
  void emitCOFFSafeSEH(MCSymbol const *Symbol) override;
  /// Emit the symbol table index of \p Symbol into the current section.
  ///
  /// \param Symbol - Symbol whose table index is emitted.
  void emitCOFFSymbolIndex(MCSymbol const *Symbol) override;
  /// Emit a COFF section index relocation for \p Symbol.
  ///
  /// \param Symbol - Symbol the section number relocation should point to.
  void emitCOFFSectionIndex(MCSymbol const *Symbol) override;
  /// Emit a COFF section-relative relocation.
  ///
  /// \param Symbol - Symbol the section relative relocation should point to.
  /// \param Offset - Offset from \p Symbol to apply to the relocation.
  void emitCOFFSecRel32(MCSymbol const *Symbol, uint64_t Offset) override;
  /// Emit a COFF image-relative relocation.
  ///
  /// \param Symbol - Symbol the image relative relocation should point to.
  /// \param Offset - Offset from \p Symbol to apply to the relocation.
  void emitCOFFImgRel32(MCSymbol const *Symbol, int64_t Offset) override;
  /// Emit the physical section number containing \p Symbol.
  ///
  /// This is assigned during object writing (i.e., this is not a runtime
  /// relocation).
  ///
  /// \param Symbol - Symbol whose containing section number is emitted.
  void emitCOFFSecNumber(MCSymbol const *Symbol) override;
  /// Emit the section offset of \p Symbol.
  ///
  /// This is assigned during object writing (i.e., this is not a runtime
  /// relocation).
  ///
  /// \param Symbol - Symbol whose section offset is emitted.
  void emitCOFFSecOffset(MCSymbol const *Symbol) override;
  /// Emit a common symbol.
  ///
  /// \param Symbol - Common symbol to emit.
  /// \param Size - Size of the common symbol in bytes.
  /// \param ByteAlignment - Alignment of the symbol.
  void emitCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                        Align ByteAlignment) override;
  /// Emit a local common (`.lcomm`) symbol.
  ///
  /// \param Symbol - Local common symbol to emit.
  /// \param Size - Size of the symbol in bytes.
  /// \param ByteAlignment - Alignment of the symbol.
  void emitLocalCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                             Align ByteAlignment) override;
  /// Emit a weak reference from \p Alias to \p Symbol.
  ///
  /// \param Alias - Alias symbol being created.
  /// \param Symbol - Symbol being weakly referenced.
  void emitWeakReference(MCSymbol *Alias, const MCSymbol *Symbol) override;
  /// Emit an `.ident` directive.
  ///
  /// \param IdentString - Identification string to emit.
  void emitIdent(StringRef IdentString) override;
  /// Emit a `.seh_handlerdata` directive.
  ///
  /// \param Loc - Source location for diagnostics.
  void emitWinEHHandlerData(SMLoc Loc) override;
  /// Emit a call-graph profile edge.
  ///
  /// \param From - Caller symbol.
  /// \param To - Callee symbol.
  /// \param Count - Number of calls from \p From to \p To.
  void emitCGProfileEntry(const MCSymbolRefExpr *From,
                          const MCSymbolRefExpr *To, uint64_t Count) override;
  /// Perform Windows COFF-specific finalization before finishing the object.
  void finishImpl() override;

  /// \}

protected:
  /// Symbol currently being defined by a COFF symbol definition sequence.
  MCSymbol *CurSymbol;

  /// Finalize one endpoint of a call-graph profile edge.
  ///
  /// Registers \p S with the assembler and marks it external when needed.
  ///
  /// \param S - Symbol reference on one side of a CG profile edge.
  void finalizeCGProfileEntry(const MCSymbolRefExpr *&S);

private:
  void Error(const Twine &Msg) const;
};

} // end namespace llvm

#endif // LLVM_MC_MCWINCOFFSTREAMER_H
