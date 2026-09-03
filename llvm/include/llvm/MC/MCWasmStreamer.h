//===- MCWasmStreamer.h - MCStreamer Wasm Object File Interface -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCWASMSTREAMER_H
#define LLVM_MC_MCWASMSTREAMER_H

#include "MCAsmBackend.h"
#include "MCCodeEmitter.h"
#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCObjectStreamer.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/Support/DataTypes.h"

namespace llvm {
class MCExpr;
class MCInst;

/// Streaming Wasm object file generation interface.
class LLVM_ABI MCWasmStreamer : public MCObjectStreamer {
public:
  /// Construct a Wasm object streamer.
  ///
  /// \param Context - MC context that owns symbols and sections.
  /// \param TAB - Assembler backend used for relaxation and object writing.
  /// \param OW - Object writer that emits the Wasm object.
  /// \param Emitter - Code emitter used to encode instructions.
  MCWasmStreamer(MCContext &Context, std::unique_ptr<MCAsmBackend> TAB,
                 std::unique_ptr<MCObjectWriter> OW,
                 std::unique_ptr<MCCodeEmitter> Emitter)
      : MCObjectStreamer(Context, std::move(TAB), std::move(OW),
                         std::move(Emitter)),
        SeenIdent(false) {}

  /// Destroy the Wasm object streamer.
  ~MCWasmStreamer() override;

  /// state management
  void reset() override {
    SeenIdent = false;
    MCObjectStreamer::reset();
  }

  /// \name MCStreamer Interface
  /// @{

  /// Update streamer state for a new active section.
  ///
  /// \param Section - Section being switched to.
  /// \param Subsection - Subsection index within \p Section.
  void changeSection(MCSection *Section, uint32_t Subsection) override;
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
  void emitLabelAtPos(MCSymbol *Symbol, SMLoc Loc, MCFragment &F,
                      uint64_t Offset) override;
  /// Add the given \p Attribute to \p Symbol.
  ///
  /// \param Symbol - Symbol to attribute.
  /// \param Attribute - Attribute to add.
  /// \return True if the attribute was applied.
  bool emitSymbolAttribute(MCSymbol *Symbol, MCSymbolAttr Attribute) override;
  /// Emit a common symbol.
  ///
  /// \param Symbol - Common symbol to emit.
  /// \param Size - Size of the common symbol in bytes.
  /// \param ByteAlignment - Alignment of the symbol.
  void emitCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                        Align ByteAlignment) override;

  /// Emit an ELF `.size` directive.
  ///
  /// \param Symbol - Symbol whose size is set.
  /// \param Value - Expression giving the symbol size.
  void emitELFSize(MCSymbol *Symbol, const MCExpr *Value) override;

  /// Emit a local common (`.lcomm`) symbol.
  ///
  /// \param Symbol - Local common symbol to emit.
  /// \param Size - Size of the symbol in bytes.
  /// \param ByteAlignment - Alignment of the symbol.
  void emitLocalCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                             Align ByteAlignment) override;

  /// Emit an `.ident` directive.
  ///
  /// \param IdentString - Identification string to emit.
  void emitIdent(StringRef IdentString) override;

  /// Perform Wasm-specific finalization before finishing the object.
  void finishImpl() override;

private:
  bool SeenIdent;
};

} // end namespace llvm

#endif
