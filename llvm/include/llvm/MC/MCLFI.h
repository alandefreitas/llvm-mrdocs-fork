//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// LFI-specific code for MC.
///
//===----------------------------------------------------------------------===//

#include "llvm/Support/Compiler.h"

namespace llvm {

class MCContext;
class MCStreamer;
class Triple;

/// Initialize \p Streamer with LFI rewriting support for \p TheTriple.
///
/// \param Streamer - Streamer to attach the LFI rewriter to.
/// \param Ctx - Assembler context used to construct the rewriter.
/// \param TheTriple - Target triple; must be an LFI triple.
LLVM_ABI void initializeLFIMCStreamer(MCStreamer &Streamer, MCContext &Ctx,
                                      const Triple &TheTriple);

/// Emit LFI bundle-alignment mode for the current target, if required.
///
/// \param Streamer - Streamer used to emit the bundle-align directive.
/// \param Ctx - Assembler context providing the target triple.
LLVM_ABI void emitLFIBundleAlign(MCStreamer &Streamer, MCContext &Ctx);

/// Emit the ELF note section that identifies an LFI object file.
///
/// \param Streamer - Streamer used to emit the note section.
/// \param Ctx - Assembler context used to create the note section.
LLVM_ABI void emitLFINoteSection(MCStreamer &Streamer, MCContext &Ctx);

} // namespace llvm
