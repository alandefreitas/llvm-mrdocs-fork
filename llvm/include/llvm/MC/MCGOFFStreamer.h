//===- MCGOFFStreamer.h - MCStreamer GOFF Object File Interface--*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCGOFFSTREAMER_H
#define LLVM_MC_MCGOFFSTREAMER_H

#include "llvm/MC/MCObjectStreamer.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class GOFFObjectWriter;
class MCSymbolGOFF;

/// Streaming GOFF object file generation interface.
class LLVM_ABI MCGOFFStreamer : public MCObjectStreamer {

public:
  /// Construct a GOFF object streamer.
  ///
  /// \param Context - MC context that owns symbols and sections.
  /// \param MAB - Assembler backend used for relaxation and object writing.
  /// \param OW - Object writer that emits the GOFF object.
  /// \param Emitter - Code emitter used to encode instructions.
  MCGOFFStreamer(MCContext &Context, std::unique_ptr<MCAsmBackend> MAB,
                 std::unique_ptr<MCObjectWriter> OW,
                 std::unique_ptr<MCCodeEmitter> Emitter);

  /// Destroy the GOFF object streamer.
  ~MCGOFFStreamer() override;

  /// Perform streamer-specific finalization before writing the object.
  void finishImpl() override;

  /// Update streamer state for a new active section.
  ///
  /// \param Section - Section being switched to.
  /// \param Subsection - Subsection index within \p Section.
  void changeSection(MCSection *Section, uint32_t Subsection = 0) override;

  /// Return the GOFF object writer used by this streamer.
  ///
  /// \return The GOFF object writer for this streamer.
  GOFFObjectWriter &getWriter();

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

  /// Emit a common symbol.
  ///
  /// \param Symbol - Common symbol to emit.
  /// \param Size - Size of the common symbol in bytes.
  /// \param ByteAlignment - Alignment of the symbol.
  void emitCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                        Align ByteAlignment) override;
};

} // end namespace llvm

#endif
