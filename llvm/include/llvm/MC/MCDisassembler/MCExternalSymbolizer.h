//===-- llvm/MC/MCExternalSymbolizer.h - ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the MCExternalSymbolizer class, which
// enables library users to provide callbacks (through the C API) to do the
// symbolization externally.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCDISASSEMBLER_MCEXTERNALSYMBOLIZER_H
#define LLVM_MC_MCDISASSEMBLER_MCEXTERNALSYMBOLIZER_H

#include "llvm-c/DisassemblerTypes.h"
#include "llvm/MC/MCDisassembler/MCSymbolizer.h"
#include <memory>

namespace llvm {

/// Symbolize using user-provided, C API, callbacks.
///
/// See llvm-c/Disassembler.h.
class LLVM_ABI MCExternalSymbolizer : public MCSymbolizer {
protected:
  /// \name Hooks for symbolic disassembly via the public 'C' interface.
  /// @{
  /// The function to get the symbolic information for operands.
  LLVMOpInfoCallback GetOpInfo;
  /// The function to lookup a symbol name.
  LLVMSymbolLookupCallback SymbolLookUp;
  /// The pointer to the block of symbolic information for above call back.
  void *DisInfo;
  /// @}

public:
  /// Construct an external symbolizer with C API callbacks.
  ///
  /// \param Ctx - Context used to create symbolic MCExprs.
  /// \param RelInfo - Relocation info; ownership is transferred to the base.
  /// \param getOpInfo - Callback that provides symbolic operand information.
  /// \param symbolLookUp - Callback that looks up a symbol name by value.
  /// \param disInfo - Opaque pointer passed through to the callbacks.
  MCExternalSymbolizer(MCContext &Ctx,
                       std::unique_ptr<MCRelocationInfo> RelInfo,
                       LLVMOpInfoCallback getOpInfo,
                       LLVMSymbolLookupCallback symbolLookUp, void *disInfo)
    : MCSymbolizer(Ctx, std::move(RelInfo)), GetOpInfo(getOpInfo),
      SymbolLookUp(symbolLookUp), DisInfo(disInfo) {}

  /// Try to add a symbolic operand instead of \p Value to the MCInst.
  ///
  /// Uses the user-provided C API callbacks to obtain symbolic information or
  /// look up a symbol name, then replaces the immediate with an MCExpr when
  /// possible.
  /// \param MI - The MCInst where to insert the symbolic operand.
  /// \param CommentStream - Stream to print comments and annotations on.
  /// \param Value - Operand value, pc-adjusted by the caller if necessary.
  /// \param Address - Load address of the instruction.
  /// \param IsBranch - Is the instruction a branch?
  /// \param Offset - Byte offset of the operand inside the inst.
  /// \param OpSize - Size of the operand in bytes.
  /// \param InstSize - Size of the instruction in bytes.
  /// \return Whether a symbolic operand was added.
  bool tryAddingSymbolicOperand(MCInst &MI, raw_ostream &CommentStream,
                                int64_t Value, uint64_t Address, bool IsBranch,
                                uint64_t Offset, uint64_t OpSize,
                                uint64_t InstSize) override;

  /// Try to add a comment on the PC-relative load.
  ///
  /// For instance, in Mach-O, this is used to add annotations to instructions
  /// that use C string literals, as found in __cstring.
  /// \param CommentStream - Stream to print comments and annotations on.
  /// \param Value - Immediate value used as a PC-relative load address.
  /// \param Address - Load address of the instruction.
  void tryAddingPcLoadReferenceComment(raw_ostream &CommentStream,
                                       int64_t Value,
                                       uint64_t Address) override;
};
}

#endif
