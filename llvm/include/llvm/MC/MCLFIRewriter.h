//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the MCLFIRewriter class, an abstract class that
/// encapsulates the rewriting logic for MCInsts.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCLFIREWRITER_H
#define LLVM_MC_MCLFIREWRITER_H

#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class MCContext;
class MCInst;
class MCSubtargetInfo;
class MCStreamer;
class MCSymbol;
class Twine;

/// Abstract base class that rewrites MCInsts for LFI sandboxing.
class MCLFIRewriter {
private:
  MCContext &Ctx;

protected:
  /// True when LFI rewriting is currently enabled.
  bool Enabled = true;
  /// Instruction info used to classify and query MCInsts.
  std::unique_ptr<MCInstrInfo> InstInfo;
  /// Register info used for register-modification queries.
  std::unique_ptr<MCRegisterInfo> RegInfo;

public:
  /// Construct a rewriter with the given context and target desc tables.
  ///
  /// \param Ctx - Assembler context used for diagnostics.
  /// \param RI - Register info for this target.
  /// \param II - Instruction info for this target.
  MCLFIRewriter(MCContext &Ctx, std::unique_ptr<MCRegisterInfo> &&RI,
                std::unique_ptr<MCInstrInfo> &&II)
      : Ctx(Ctx), InstInfo(std::move(II)), RegInfo(std::move(RI)) {}

  /// Report an error at the location of \p Inst.
  ///
  /// \param Inst - Instruction whose source location is used.
  /// \param Msg - Error message to report.
  LLVM_ABI void error(const MCInst &Inst, const Twine &Msg);
  /// Report a warning at the location of \p Inst.
  ///
  /// \param Inst - Instruction whose source location is used.
  /// \param Msg - Warning message to report.
  LLVM_ABI void warning(const MCInst &Inst, const Twine &Msg);

  /// Disable LFI rewriting.
  void disable() { Enabled = false; }
  /// Enable LFI rewriting.
  void enable() { Enabled = true; }

  /// Return true if \p Inst is a call.
  ///
  /// \param Inst - Instruction to classify.
  /// \return - True if \p Inst is a call instruction.
  LLVM_ABI bool isCall(const MCInst &Inst) const;
  /// Return true if \p Inst is a branch.
  ///
  /// \param Inst - Instruction to classify.
  /// \return - True if \p Inst is a branch instruction.
  LLVM_ABI bool isBranch(const MCInst &Inst) const;
  /// Return true if \p Inst is an indirect branch.
  ///
  /// \param Inst - Instruction to classify.
  /// \return - True if \p Inst is an indirect branch instruction.
  LLVM_ABI bool isIndirectBranch(const MCInst &Inst) const;
  /// Return true if \p Inst is a return.
  ///
  /// \param Inst - Instruction to classify.
  /// \return - True if \p Inst is a return instruction.
  LLVM_ABI bool isReturn(const MCInst &Inst) const;

  /// Return true if \p Inst may load from memory.
  ///
  /// \param Inst - Instruction to query.
  /// \return - True if \p Inst may load from memory.
  LLVM_ABI bool mayLoad(const MCInst &Inst) const;
  /// Return true if \p Inst may store to memory.
  ///
  /// \param Inst - Instruction to query.
  /// \return - True if \p Inst may store to memory.
  LLVM_ABI bool mayStore(const MCInst &Inst) const;

  /// Return true if \p Inst may modify \p Reg, including implicitly.
  ///
  /// \param Inst - Instruction to query.
  /// \param Reg - Physical register to check for modification.
  /// \return - True if \p Inst may modify \p Reg, including implicitly.
  LLVM_ABI bool mayModifyRegister(const MCInst &Inst, MCRegister Reg) const;
  /// Return true if \p Inst explicitly modifies \p Reg.
  ///
  /// \param Inst - Instruction to query.
  /// \param Reg - Physical register to check for an explicit def.
  /// \return - True if \p Inst explicitly modifies \p Reg.
  LLVM_ABI bool explicitlyModifiesRegister(const MCInst &Inst,
                                           MCRegister Reg) const;

  /// Destroy the LFI rewriter.
  virtual ~MCLFIRewriter() = default;
  /// Rewrite \p Inst for LFI, emitting any replacement through \p Out.
  ///
  /// \param Inst - Instruction to rewrite.
  /// \param Out - Streamer that receives rewritten instructions.
  /// \param STI - Subtarget info in effect for \p Inst.
  /// \return - True if the instruction was handled and must not be emitted
  /// again by the caller.
  virtual bool rewriteInst(const MCInst &Inst, MCStreamer &Out,
                           const MCSubtargetInfo &STI) = 0;

  /// Handle emission of label \p Symbol.
  ///
  /// Called when a label is emitted. Used for optimizations that require
  /// information about jump targets, such as guard elimination, and to flush
  /// any rewriter state that must not cross a potential branch target.
  ///
  /// \param Symbol - Label being emitted.
  /// \param Out - Streamer associated with the label.
  virtual void onLabel(const MCSymbol *Symbol, MCStreamer &Out) {}

  /// Flush pending rewriter state when the stream is finalized.
  ///
  /// Called when the stream is finalized. Used to flush any pending rewriter
  /// state before the stream ends.
  ///
  /// \param Out - Streamer being finalized.
  virtual void finish(MCStreamer &Out) {}
};

} // namespace llvm
#endif
