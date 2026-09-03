//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares CFIFunctionFrameReceiver class.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_DWARFCFICHECKER_DWARFCFIFUNCTIONFRAMERECEIVER_H
#define LLVM_DWARFCFICHECKER_DWARFCFIFUNCTIONFRAMERECEIVER_H

#include "llvm/ADT/ArrayRef.h"

namespace llvm {

class MCCFIInstruction;
class MCContext;
class MCInst;

/// Abstract interface for receiving DWARF Call Frame Information for function
/// frames.
///
/// `DWARFCFIFunctionFrameStreamer` channels the function frames information
/// gathered from an `MCStreamer` using a pointer to an instance of this class
/// for the whole program.
class CFIFunctionFrameReceiver {
public:
  /// Copy construction is deleted.
  ///
  /// \param Other Unused; copy construction is not supported.
  CFIFunctionFrameReceiver(const CFIFunctionFrameReceiver &Other) = delete;
  /// Copy assignment is deleted.
  ///
  /// \param Other Unused; copy assignment is not supported.
  CFIFunctionFrameReceiver &
  operator=(const CFIFunctionFrameReceiver &Other) = delete;
  /// Destroy the function-frame receiver.
  virtual ~CFIFunctionFrameReceiver() = default;

  /// Construct a receiver that uses \p Context for related MC state.
  ///
  /// \param Context MC context associated with this receiver.
  CFIFunctionFrameReceiver(MCContext &Context) : Context(Context) {}

  /// Get the MC context associated with this receiver.
  ///
  /// \return The MC context associated with this receiver.
  MCContext &getContext() const { return Context; }

  /// Begin a new DWARF function frame.
  ///
  /// \param IsEH Whether this frame is for exception handling, not debug.
  /// \param Prologue CFI instructions that form the frame prologue.
  virtual void startFunctionFrame(bool IsEH,
                                  ArrayRef<MCCFIInstruction> Prologue) {}
  /// Emit a machine instruction together with its associated CFI directives.
  ///
  /// Instructions are processed in the program order.
  /// \param Inst Machine instruction being emitted.
  /// \param Directives CFI directives associated with \p Inst.
  virtual void
  emitInstructionAndDirectives(const MCInst &Inst,
                               ArrayRef<MCCFIInstruction> Directives) {}
  /// Finish the current DWARF function frame.
  virtual void finishFunctionFrame() {}

private:
  MCContext &Context;
};

} // namespace llvm

#endif
