//===- MCXCOFFObjectStreamer.h - MCStreamer XCOFF Object File Interface ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCXCOFFSTREAMER_H
#define LLVM_MC_MCXCOFFSTREAMER_H

#include "llvm/MC/MCObjectStreamer.h"

namespace llvm {
class XCOFFObjectWriter;

/// Streaming XCOFF object file generation interface.
class LLVM_ABI MCXCOFFStreamer : public MCObjectStreamer {
public:
  /// Construct an XCOFF object streamer.
  ///
  /// \param Context - MC context that owns symbols and sections.
  /// \param MAB - Assembler backend used for relaxation and object writing.
  /// \param OW - Object writer that emits the XCOFF object.
  /// \param Emitter - Code emitter used to encode instructions.
  MCXCOFFStreamer(MCContext &Context, std::unique_ptr<MCAsmBackend> MAB,
                  std::unique_ptr<MCObjectWriter> OW,
                  std::unique_ptr<MCCodeEmitter> Emitter);

  /// Return the XCOFF object writer used by this streamer.
  ///
  /// \return The XCOFF object writer used by this streamer.
  XCOFFObjectWriter &getWriter();

  /// Update streamer state for a new active section.
  ///
  /// \param Section - Section being switched to.
  /// \param Subsection - Subsection index within \p Section.
  void changeSection(MCSection *Section, uint32_t Subsection = 0) override;
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
  /// Emit an lcomm directive with XCOFF csect information.
  ///
  /// \param LabelSym - Label on the block of storage.
  /// \param Size - The size of the block of storage.
  /// \param CsectSym - Csect name for the block of storage.
  /// \param Alignment - The alignment of the symbol in bytes.
  void emitXCOFFLocalCommonSymbol(MCSymbol *LabelSym, uint64_t Size,
                                  MCSymbol *CsectSym, Align Alignment) override;
  /// Emit a symbol's linkage and visibility with a linkage directive for XCOFF.
  ///
  /// \param Symbol - The symbol to emit.
  /// \param Linkage - The linkage of the symbol to emit.
  /// \param Visibility - The visibility of the symbol to emit or MCSA_Invalid
  /// if the symbol does not have an explicit visibility.
  void emitXCOFFSymbolLinkageWithVisibility(MCSymbol *Symbol,
                                            MCSymbolAttr Linkage,
                                            MCSymbolAttr Visibility) override;
  /// Emit an XCOFF .ref directive which creates R_REF type entry in the
  /// relocation table for one or more symbols.
  ///
  /// \param Symbol - The symbol on the .ref directive.
  void emitXCOFFRefDirective(const MCSymbol *Symbol) override;
  /// Emit a XCOFF .rename directive which creates a synonym for an illegal or
  /// undesirable name.
  ///
  /// \param Name - The name used internally in the assembly for references to
  /// the symbol.
  /// \param Rename - The value to which the Name parameter is
  /// changed at the end of assembly.
  void emitXCOFFRenameDirective(const MCSymbol *Name,
                                StringRef Rename) override;
  /// Emit an XCOFF .except directive which adds information about
  /// a trap instruction to the object file exception section.
  ///
  /// \param Symbol - The function containing the trap.
  /// \param Trap - The trap-instruction symbol.
  /// \param Lang - The language code for the exception entry.
  /// \param Reason - The reason code for the exception entry.
  /// \param FunctionSize - Size of the function containing the trap.
  /// \param hasDebug - True if the function has debug information.
  void emitXCOFFExceptDirective(const MCSymbol *Symbol, const MCSymbol *Trap,
                                unsigned Lang, unsigned Reason,
                                unsigned FunctionSize, bool hasDebug) override;
  /// Emit a C_INFO symbol with XCOFF embedded metadata to the .info section.
  ///
  /// \param Name - The embedded metadata name.
  /// \param Metadata - The embedded metadata.
  void emitXCOFFCInfoSym(StringRef Name, StringRef Metadata) override;
};

} // end namespace llvm

#endif // LLVM_MC_MCXCOFFSTREAMER_H
