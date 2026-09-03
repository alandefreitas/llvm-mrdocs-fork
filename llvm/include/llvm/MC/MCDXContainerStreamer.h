//===- MCDXContainerStreamer.h - MCDXContainerStreamer Interface ---*- C++ ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Overrides MCObjectStreamer to disable all unnecessary features with stubs.
// The DXContainer format isn't a fully featured object format. It doesn't
// support symbols, and initially it will not support instruction data since it
// is used as a bitcode container for DXIL.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCDXCONTAINERSTREAMER_H
#define LLVM_MC_MCDXCONTAINERSTREAMER_H

#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCObjectStreamer.h"
#include "llvm/MC/MCObjectWriter.h"

namespace llvm {
class MCInst;
class raw_ostream;

/// Streaming DXContainer object file generation interface.
///
/// Overrides MCObjectStreamer to disable all unnecessary features with stubs.
/// The DXContainer format isn't a fully featured object format. It doesn't
/// support symbols, and initially it will not support instruction data since it
/// is used as a bitcode container for DXIL.
class MCDXContainerStreamer : public MCObjectStreamer {
public:
  /// Construct a DXContainer object streamer.
  ///
  /// \param Context - MC context that owns symbols and sections.
  /// \param TAB - Assembler backend used for relaxation and object writing.
  /// \param OW - Object writer that emits the DXContainer object.
  /// \param Emitter - Code emitter used to encode instructions.
  MCDXContainerStreamer(MCContext &Context, std::unique_ptr<MCAsmBackend> TAB,
                        std::unique_ptr<MCObjectWriter> OW,
                        std::unique_ptr<MCCodeEmitter> Emitter)
      : MCObjectStreamer(Context, std::move(TAB), std::move(OW),
                         std::move(Emitter)) {}

  /// Ignore symbol attributes; DXContainer does not support symbols.
  ///
  /// \param Symbol - Symbol that would receive the attribute.
  /// \param Attribute - Attribute that would be applied.
  /// \return Always false.
  bool emitSymbolAttribute(MCSymbol *Symbol, MCSymbolAttr Attribute) override {
    return false;
  }
  /// Ignore common symbols; DXContainer does not support symbols.
  ///
  /// \param Symbol - Common symbol that would be emitted.
  /// \param Size - Size of the common symbol in bytes.
  /// \param ByteAlignment - Alignment of the symbol.
  void emitCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                        Align ByteAlignment) override {}
};

} // end namespace llvm

#endif // LLVM_MC_MCDXCONTAINERSTREAMER_H
