//===- MCSFrame.h - Machine Code SFrame support ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of MCSFrameEmitter to support emitting
// sframe unwinding info from .cfi_* directives. It relies on FDEs and CIEs
// created for Dwarf frame info, but emits that info in a different format.
//
// See https://sourceware.org/binutils/docs-2.41/sframe-spec.html
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCSFRAME_H
#define LLVM_MC_MCSFRAME_H

#include "llvm/ADT/SmallVector.h"
#include <cstdint>

namespace llvm {

class MCContext;
class MCObjectStreamer;
class MCFragment;

/// Emits sframe unwinding information from CFI directives.
///
/// Relies on FDEs and CIEs created for DWARF frame info, but emits that info
/// in the sframe format.
class MCSFrameEmitter {
public:
  /// Emit the sframe section for all recorded frames.
  ///
  /// \param Streamer - Object streamer that receives the sframe section.
  LLVM_ABI static void emit(MCObjectStreamer &Streamer);

  /// Encode an FRE function offset into the given buffer.
  ///
  /// \param C - Assembler context used for the encoding.
  /// \param Offset - Function offset to encode.
  /// \param Out - Destination buffer that receives the encoded bytes.
  /// \param FDEFrag - Fragment that specifies the encoding format.
  LLVM_ABI static void encodeFuncOffset(MCContext &C, uint64_t Offset,
                                        SmallVectorImpl<char> &Out,
                                        MCFragment *FDEFrag);
};

} // end namespace llvm
#endif // LLVM_MC_MCSFRAME_H
