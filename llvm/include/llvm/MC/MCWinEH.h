//===- MCWinEH.h - Windows Unwinding Support --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCWINEH_H
#define LLVM_MC_MCWINEH_H

#include "llvm/ADT/MapVector.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/SMLoc.h"
#include <vector>

namespace llvm {
class MCSection;
class MCStreamer;
class MCSymbol;

namespace WinEH {
/// A single Windows unwind opcode and its associated operands.
struct Instruction {
  /// Label marking where this unwind operation applies.
  const MCSymbol *Label;
  /// Stack offset or size operand for the operation.
  unsigned Offset;
  /// Primary register operand for the operation.
  uint16_t Register;
  /// Secondary register for two-register ops (e.g. PUSH2).
  uint16_t Register2;
  /// Unwind opcode identifying the operation kind.
  uint8_t Operation;

  /// Construct a one-register unwind instruction.
  ///
  /// \param Op - Unwind opcode for this instruction.
  /// \param L - Label marking where the operation applies.
  /// \param Reg - Register operand for the operation.
  /// \param Off - Stack offset or size operand.
  Instruction(unsigned Op, MCSymbol *L, unsigned Reg, unsigned Off)
      : Label(L), Offset(Off), Register(Reg), Register2(0), Operation(Op) {}

  /// Construct a two-register unwind instruction.
  ///
  /// \param Op - Unwind opcode for this instruction.
  /// \param L - Label marking where the operation applies.
  /// \param Reg1 - First register operand.
  /// \param Reg2 - Second register operand.
  /// \param Off - Stack offset or size operand.
  Instruction(unsigned Op, MCSymbol *L, unsigned Reg1, unsigned Reg2,
              unsigned Off)
      : Label(L), Offset(Off), Register(Reg1), Register2(Reg2), Operation(Op) {}

  /// Compare two instructions for equal opcode and operands, ignoring labels.
  ///
  /// Two instructions are equal when they refer to the same operation applied
  /// at a different spot (i.e. pointing at a different label).
  ///
  /// \param I - Other instruction to compare against.
  /// \return True if the opcode and operands match.
  bool operator==(const Instruction &I) const {
    return Offset == I.Offset && Register == I.Register &&
           Register2 == I.Register2 && Operation == I.Operation;
  }
  /// Return true if this instruction differs from \p I in opcode or operands.
  ///
  /// \param I - Other instruction to compare against.
  /// \return True if the instructions are not equal.
  bool operator!=(const Instruction &I) const { return !(*this == I); }
};

/// Unwind and exception-handling state for a Windows function or funclet.
struct FrameInfo {
  /// Symbol marking the start of the function or funclet.
  const MCSymbol *Begin = nullptr;
  /// Symbol marking the end of the function or funclet.
  const MCSymbol *End = nullptr;
  /// Symbol marking the end of the enclosing function or funclet.
  const MCSymbol *FuncletOrFuncEnd = nullptr;
  /// Symbol of the exception handler for this frame, if any.
  const MCSymbol *ExceptionHandler = nullptr;
  /// Symbol of the function this frame describes.
  const MCSymbol *Function = nullptr;
  /// Source location of the function for diagnostics.
  SMLoc FunctionLoc;
  /// Symbol marking the end of the prologue.
  const MCSymbol *PrologEnd = nullptr;
  /// Symbol associated with this frame's unwind info entry.
  const MCSymbol *Symbol = nullptr;
  /// Text section containing the function body.
  MCSection *TextSection = nullptr;
  /// Packed unwind info encoding when a compact form is used.
  uint32_t PackedInfo = 0;
  /// Number of code bytes occupied by the prologue.
  uint32_t PrologCodeBytes = 0;

  /// True if this frame participates in unwind handling.
  bool HandlesUnwind = false;
  /// True if this frame participates in exception handling.
  bool HandlesExceptions = false;
  /// True if emission of this frame's unwind info was already attempted.
  bool EmitAttempted = false;
  /// True if this frame is a fragment of a larger function's unwind info.
  bool Fragment = false;
  /// Default unwind info version used when none is specified.
  constexpr static uint8_t DefaultVersion = 1;
  /// Unwind info version for this frame.
  uint8_t Version = DefaultVersion;

  /// Index of the last frame instruction, or -1 if none.
  int LastFrameInst = -1;
  /// Parent frame when this frame is part of a chained unwind sequence.
  FrameInfo *ChainedParent = nullptr;
  /// Prologue unwind instructions for this frame.
  std::vector<Instruction> Instructions;
  /// Description of a single epilogue within a Windows frame.
  struct Epilog {
    /// Unwind instructions that reverse the corresponding prologue.
    std::vector<Instruction> Instructions;
    /// Condition code associated with a conditional epilogue, if any.
    unsigned Condition;
    /// Symbol marking the start of the epilogue.
    const MCSymbol *Start = nullptr;
    /// Symbol marking the end of the epilogue.
    const MCSymbol *End = nullptr;
    /// Symbol marking the unwind-v2 start of the epilogue, if used.
    const MCSymbol *UnwindV2Start = nullptr;
    /// Source location of the epilogue for diagnostics.
    SMLoc Loc;
  };
  /// Map from each epilogue's start symbol to its epilogue description.
  MapVector<MCSymbol *, Epilog> EpilogMap;

  /// A contiguous fragment of unwind info for a large function.
  ///
  /// Used when splitting unwind info of large functions.
  struct Segment {
    /// Byte offset of this segment within the function.
    int64_t Offset;
    /// Byte length of this segment.
    int64_t Length;
    /// True if this segment includes the function prologue.
    bool HasProlog;
    /// Symbol associated with this segment's unwind info entry.
    MCSymbol *Symbol = nullptr;
    /// Map from an epilogue's symbol to its offset within the function.
    MapVector<MCSymbol *, int64_t> Epilogs;

    /// Construct a segment covering \p Length bytes starting at \p Offset.
    ///
    /// \param Offset - Byte offset of the segment within the function.
    /// \param Length - Byte length of the segment.
    /// \param HasProlog - True if the segment includes the prologue.
    Segment(int64_t Offset, int64_t Length, bool HasProlog = false)
        : Offset(Offset), Length(Length), HasProlog(HasProlog) {}
  };

  /// Segments used when splitting unwind info of large functions.
  std::vector<Segment> Segments;

  /// Construct an empty frame with default field values.
  FrameInfo() = default;
  /// Construct a frame for \p Function starting at \p BeginFuncEHLabel.
  ///
  /// \param Function - Symbol of the function this frame describes.
  /// \param BeginFuncEHLabel - Symbol marking the start of the EH region.
  FrameInfo(const MCSymbol *Function, const MCSymbol *BeginFuncEHLabel)
      : Begin(BeginFuncEHLabel), Function(Function) {}
  /// Construct a chained frame under \p ChainedParent.
  ///
  /// \param Function - Symbol of the function this frame describes.
  /// \param BeginFuncEHLabel - Symbol marking the start of the EH region.
  /// \param ChainedParent - Parent frame in the chained unwind sequence.
  FrameInfo(const MCSymbol *Function, const MCSymbol *BeginFuncEHLabel,
            FrameInfo *ChainedParent)
      : Begin(BeginFuncEHLabel), Function(Function),
        Version(ChainedParent->Version), ChainedParent(ChainedParent) {}

  /// Return true if this frame has no prologue or epilogue unwind instructions.
  ///
  /// \return True if there are no prologue or epilogue unwind instructions.
  bool empty() const {
    if (!Instructions.empty())
      return false;
    for (const auto &E : EpilogMap)
      if (!E.second.Instructions.empty())
        return false;
    return true;
  }
};

/// Abstract emitter for Windows unwind info sections.
class LLVM_ABI UnwindEmitter {
public:
  /// Destroy the unwind emitter.
  virtual ~UnwindEmitter();

  /// This emits the unwind info sections (.pdata and .xdata in PE/COFF).
  ///
  /// \param Streamer - Streamer that receives the unwind sections.
  virtual void Emit(MCStreamer &Streamer) const = 0;
  /// Emit unwind info for a single frame.
  ///
  /// \param Streamer - Streamer that receives the unwind info.
  /// \param FI - Frame whose unwind info should be emitted.
  /// \param HandlerData - True when handler data should also be emitted.
  virtual void EmitUnwindInfo(MCStreamer &Streamer, FrameInfo *FI,
                              bool HandlerData) const = 0;
};
} // namespace WinEH
} // namespace llvm

#endif
