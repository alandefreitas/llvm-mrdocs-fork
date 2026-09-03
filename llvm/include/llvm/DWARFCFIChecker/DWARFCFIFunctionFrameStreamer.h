//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares CFIFunctionFrameStreamer class.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_DWARFCFICHECKER_DWARFCFIFUNCTIONFRAMESTREAMER_H
#define LLVM_DWARFCFICHECKER_DWARFCFIFUNCTIONFRAMESTREAMER_H

#include "DWARFCFIFunctionFrameReceiver.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/Compiler.h"
#include <memory>
#include <optional>

namespace llvm {

/// MCStreamer that cuts CFI and instructions into frames for a receiver.
///
/// This class is an `MCStreamer` implementation that watches for machine
/// instructions and CFI directives. It cuts the stream into function frames and
/// channels them to `CFIFunctionFrameReceiver`. A function frame is the machine
/// instructions and CFI directives that are between `.cfi_startproc` and
/// `.cfi_endproc` directives.
class LLVM_ABI CFIFunctionFrameStreamer : public MCStreamer {
public:
  /// Construct a streamer that forwards function frames to \p Receiver.
  /// \param Context - MC context that owns symbols and sections.
  /// \param Receiver - Non-null receiver that consumes each function frame.
  CFIFunctionFrameStreamer(MCContext &Context,
                           std::unique_ptr<CFIFunctionFrameReceiver> Receiver)
      : MCStreamer(Context), Receiver(std::move(Receiver)) {
    assert(this->Receiver && "Receiver should not be null");
  }

  /// Always accept raw text; it is discarded without emission.
  /// \return Always true.
  bool hasRawTextSupport() const override { return true; }
  /// Discard \p String; this streamer does not emit assembly text.
  /// \param String - Raw text that would be written to a .s file.
  void emitRawTextImpl(StringRef String) override {}

  /// Accept \p Attribute on \p Symbol without recording it.
  /// \param Symbol - Symbol to attribute.
  /// \param Attribute - Attribute to add.
  /// \return Always true.
  bool emitSymbolAttribute(MCSymbol *Symbol, MCSymbolAttr Attribute) override {
    return true;
  }

  /// No-op common-symbol emission; this streamer ignores object symbols.
  /// \param Symbol - The common symbol to emit.
  /// \param Size - The size of the common symbol.
  /// \param ByteAlignment - The alignment of the symbol.
  void emitCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                        Align ByteAlignment) override {}
  /// No-op `.subsection_via_symbols` emission.
  void emitSubsectionsViaSymbols() override {};
  /// No-op start of a COFF symbol definition.
  /// \param Symbol - The symbol to have its External & Type fields set.
  void beginCOFFSymbolDef(const MCSymbol *Symbol) override {}
  /// No-op COFF storage-class emission.
  /// \param StorageClass - The storage class the symbol should have.
  void emitCOFFSymbolStorageClass(int StorageClass) override {}
  /// No-op COFF symbol-type emission.
  /// \param Type - A COFF type identifier (see COFF::SymbolType in X86COFF.h).
  void emitCOFFSymbolType(int Type) override {}
  /// No-op end of a COFF symbol definition.
  void endCOFFSymbolDef() override {}
  /// No-op XCOFF linkage and visibility emission.
  /// \param Symbol - The symbol to emit.
  /// \param Linkage - The linkage of the symbol to emit.
  /// \param Visibility - The visibility of the symbol to emit or MCSA_Invalid
  /// if the symbol does not have an explicit visibility.
  void emitXCOFFSymbolLinkageWithVisibility(MCSymbol *Symbol,
                                            MCSymbolAttr Linkage,
                                            MCSymbolAttr Visibility) override {}

  /// Forward \p Inst to the receiver when inside an unfinished DWARF frame.
  /// \param Inst - Instruction to emit.
  /// \param STI - Subtarget info in effect for \p Inst.
  void emitInstruction(const MCInst &Inst, const MCSubtargetInfo &STI) override;
  /// Push frame state and start tracking a new `.cfi_startproc` region.
  /// \param Frame - Frame info being started.
  void emitCFIStartProcImpl(MCDwarfFrameInfo &Frame) override;
  /// Flush the current frame to the receiver and end `.cfi_endproc`.
  /// \param CurFrame - Frame info being ended.
  void emitCFIEndProcImpl(MCDwarfFrameInfo &CurFrame) override;

private:
  /// This method sends the last instruction, along with its associated
  /// directives, to the receiver and then updates the internal state of the
  /// class. It moves the directive index to after the last directive and sets
  /// the last instruction to \p NewInst . This method assumes it is called in
  /// the middle of an unfinished DWARF debug frame; if not, an assertion will
  /// fail.
  void updateReceiver(const std::optional<MCInst> &NewInst);

private:
  /// The following fields are stacks that store the state of the stream sent to
  /// the receiver in each frame. This class, like `MCStreamer`, assumes that
  /// the debug frames are intertwined with each other only in stack form.

  /// The last instruction that is not sent to the receiver for each frame.
  SmallVector<std::optional<MCInst>> LastInstructions;
  /// The index of the last directive that is not sent to the receiver for each
  /// frame.
  SmallVector<unsigned> LastDirectiveIndices;
  /// The index of each frame in `DwarfFrameInfos` field in `MCStreamer`.
  SmallVector<unsigned> FrameIndices;

  std::unique_ptr<CFIFunctionFrameReceiver> Receiver;
};

} // namespace llvm

#endif
