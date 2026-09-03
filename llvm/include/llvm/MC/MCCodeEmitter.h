//===- llvm/MC/MCCodeEmitter.h - Instruction Encoding -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCCODEEMITTER_H
#define LLVM_MC_MCCODEEMITTER_H

#include "llvm/Support/Compiler.h"

namespace llvm {

class MCFixup;
class MCInst;
class MCSubtargetInfo;
template<typename T> class SmallVectorImpl;

/// MCCodeEmitter - Generic instruction encoding interface.
class LLVM_ABI MCCodeEmitter {
protected: // Can only create subclasses.
  /// Construct a code emitter. Only subclasses may create instances.
  MCCodeEmitter();

public:
  /// Deleted copy constructor.
  ///
  /// \param Other - Unused; copy construction is deleted.
  MCCodeEmitter(const MCCodeEmitter &Other) = delete;
  /// Deleted copy assignment.
  ///
  /// \param Other - Unused; copy assignment is deleted.
  MCCodeEmitter &operator=(const MCCodeEmitter &Other) = delete;
  /// Destroy the code emitter.
  virtual ~MCCodeEmitter();

  /// Lifetime management
  virtual void reset() {}

  /// Encode the given \p Inst to bytes and append to \p CB.
  ///
  /// \param Inst - Instruction to encode.
  /// \param CB - Buffer that receives the encoded instruction bytes.
  /// \param Fixups - Fixups collected for relocatable operands.
  /// \param STI - Subtarget information for the encoding.
  virtual void encodeInstruction(const MCInst &Inst, SmallVectorImpl<char> &CB,
                                 SmallVectorImpl<MCFixup> &Fixups,
                                 const MCSubtargetInfo &STI) const = 0;

protected:
  /// Report that instruction \p Inst is unsupported and abort.
  ///
  /// Helper used by CodeEmitterGen for error reporting.
  ///
  /// \param Inst - Unsupported instruction being encoded.
  [[noreturn]] static void reportUnsupportedInst(const MCInst &Inst);
  /// Report that operand \p OpNum of \p Inst is unsupported and abort.
  ///
  /// Helper used by CodeEmitterGen for error reporting.
  ///
  /// \param Inst - Instruction containing the unsupported operand.
  /// \param OpNum - Index of the unsupported operand.
  [[noreturn]] static void reportUnsupportedOperand(const MCInst &Inst,
                                                    unsigned OpNum);
};

} // end namespace llvm

#endif // LLVM_MC_MCCODEEMITTER_H
