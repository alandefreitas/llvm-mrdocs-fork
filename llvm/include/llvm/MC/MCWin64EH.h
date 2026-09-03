//===- MCWin64EH.h - Machine Code Win64 EH support --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains declarations to support the Win64 Exception Handling
// scheme in MC.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCWIN64EH_H
#define LLVM_MC_MCWIN64EH_H

#include "llvm/MC/MCWinEH.h"
#include "llvm/Support/Win64EH.h"

namespace llvm {
class MCStreamer;
class MCSymbol;

/// Helpers for encoding Windows x64, ARM, and ARM64 unwind information.
namespace Win64EH {
/// Factory helpers that build WinEH::Instruction opcodes for Win64 unwind.
struct Instruction {
  /// Create a PushNonVol unwind opcode for \p Reg at label \p L.
  ///
  /// \param L - Label marking where the push occurs.
  /// \param Reg - Non-volatile register being pushed.
  /// \return A WinEH::Instruction for the PushNonVol opcode.
  static WinEH::Instruction PushNonVol(MCSymbol *L, unsigned Reg) {
    return WinEH::Instruction(Win64EH::UOP_PushNonVol, L, Reg, -1);
  }
  /// Create a Push2 unwind opcode for registers \p Reg1 and \p Reg2.
  ///
  /// \param L - Label marking where the paired push occurs.
  /// \param Reg1 - First register being pushed.
  /// \param Reg2 - Second register being pushed.
  /// \return A WinEH::Instruction for the Push2 opcode.
  static WinEH::Instruction Push2(MCSymbol *L, unsigned Reg1, unsigned Reg2) {
    return WinEH::Instruction(Win64EH::UOP_Push2, L, Reg1, Reg2, -1);
  }
  /// Create an AllocSmall or AllocLarge unwind opcode for \p Size bytes.
  ///
  /// \param L - Label marking where the allocation occurs.
  /// \param Size - Number of bytes allocated on the stack.
  /// \return A WinEH::Instruction for the Alloc opcode.
  static WinEH::Instruction Alloc(MCSymbol *L, unsigned Size) {
    return WinEH::Instruction(Size > 128 ? UOP_AllocLarge : UOP_AllocSmall, L,
                              -1, Size);
  }
  /// Create a PushMachFrame unwind opcode, optionally including a code slot.
  ///
  /// \param L - Label marking where the machine frame is pushed.
  /// \param Code - True if the machine frame includes an error code.
  /// \return A WinEH::Instruction for the PushMachFrame opcode.
  static WinEH::Instruction PushMachFrame(MCSymbol *L, bool Code) {
    return WinEH::Instruction(UOP_PushMachFrame, L, -1, Code ? 1 : 0);
  }
  /// Create a SaveNonVol or SaveNonVolBig opcode storing \p Reg at \p Offset.
  ///
  /// \param L - Label marking where the save occurs.
  /// \param Reg - Non-volatile register being saved.
  /// \param Offset - Stack offset of the saved register.
  /// \return A WinEH::Instruction for the SaveNonVol opcode.
  static WinEH::Instruction SaveNonVol(MCSymbol *L, unsigned Reg,
                                       unsigned Offset) {
    return WinEH::Instruction(Offset > 512 * 1024 - 8 ? UOP_SaveNonVolBig
                                                      : UOP_SaveNonVol,
                              L, Reg, Offset);
  }
  /// Create a SaveXMM128 or SaveXMM128Big opcode storing \p Reg at \p Offset.
  ///
  /// \param L - Label marking where the save occurs.
  /// \param Reg - XMM register being saved.
  /// \param Offset - Stack offset of the saved XMM register.
  /// \return A WinEH::Instruction for the SaveXMM opcode.
  static WinEH::Instruction SaveXMM(MCSymbol *L, unsigned Reg,
                                    unsigned Offset) {
    return WinEH::Instruction(Offset > 512 * 1024 - 8 ? UOP_SaveXMM128Big
                                                      : UOP_SaveXMM128,
                              L, Reg, Offset);
  }
  /// Create a SetFPReg unwind opcode establishing a frame pointer.
  ///
  /// \param L - Label marking where the frame pointer is set.
  /// \param Reg - Register used as the frame pointer.
  /// \param Off - Offset from RSP applied when setting the frame pointer.
  /// \return A WinEH::Instruction for the SetFPReg opcode.
  static WinEH::Instruction SetFPReg(MCSymbol *L, unsigned Reg, unsigned Off) {
    return WinEH::Instruction(UOP_SetFPReg, L, Reg, Off);
  }
};

/// Emits Windows x64 unwind info sections (.pdata and .xdata).
class LLVM_ABI UnwindEmitter : public WinEH::UnwindEmitter {
public:
  /// Emit the unwind info sections for all recorded frames.
  ///
  /// \param Streamer - Streamer that receives the unwind sections.
  void Emit(MCStreamer &Streamer) const override;
  /// Emit unwind info for a single frame.
  ///
  /// \param Streamer - Streamer that receives the unwind info.
  /// \param FI - Frame whose unwind info should be emitted.
  /// \param HandlerData - True when handler data should also be emitted.
  void EmitUnwindInfo(MCStreamer &Streamer, WinEH::FrameInfo *FI,
                      bool HandlerData) const override;
};

/// Emits Windows ARM unwind info sections (.pdata and .xdata).
class LLVM_ABI ARMUnwindEmitter : public WinEH::UnwindEmitter {
public:
  /// Emit the unwind info sections for all recorded frames.
  ///
  /// \param Streamer - Streamer that receives the unwind sections.
  void Emit(MCStreamer &Streamer) const override;
  /// Emit unwind info for a single frame.
  ///
  /// \param Streamer - Streamer that receives the unwind info.
  /// \param FI - Frame whose unwind info should be emitted.
  /// \param HandlerData - True when handler data should also be emitted.
  void EmitUnwindInfo(MCStreamer &Streamer, WinEH::FrameInfo *FI,
                      bool HandlerData) const override;
};

/// Emits Windows ARM64 unwind info sections (.pdata and .xdata).
class LLVM_ABI ARM64UnwindEmitter : public WinEH::UnwindEmitter {
public:
  /// Emit the unwind info sections for all recorded frames.
  ///
  /// \param Streamer - Streamer that receives the unwind sections.
  void Emit(MCStreamer &Streamer) const override;
  /// Emit unwind info for a single frame.
  ///
  /// \param Streamer - Streamer that receives the unwind info.
  /// \param FI - Frame whose unwind info should be emitted.
  /// \param HandlerData - True when handler data should also be emitted.
  void EmitUnwindInfo(MCStreamer &Streamer, WinEH::FrameInfo *FI,
                      bool HandlerData) const override;
};
/// Encode a single WinEH::Instruction as V3 WOD bytes.
///
/// Appends encoded bytes to Out.
///
/// \param Inst - Instruction to encode as WOD bytes.
/// \param Out - Destination buffer that receives the encoded bytes.
LLVM_ABI void EncodeWOD(const WinEH::Instruction &Inst,
                        SmallVectorImpl<uint8_t> &Out);
} // namespace Win64EH
} // namespace llvm

#endif
