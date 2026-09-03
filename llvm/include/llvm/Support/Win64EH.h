//===-- llvm/Support/Win64EH.h ---Win64 EH Constants-------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains constants and structures used for implementing
// exception handling on Win64 platforms. For more information, see
// http://msdn.microsoft.com/en-us/library/1eyas8tf.aspx
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_WIN64EH_H
#define LLVM_SUPPORT_WIN64EH_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace Win64EH {

/// UnwindOpcodes - Enumeration whose values specify a single operation in
/// the prolog of a function.
enum UnwindOpcodes {
  // The following set of unwind opcodes is for x86_64.  They are documented at
  // https://docs.microsoft.com/en-us/cpp/build/exception-handling-x64.
  // Some generic values from this set are used for other architectures too.
  UOP_PushNonVol = 0, ///< Push a nonvolatile integer register.
  UOP_AllocLarge,     ///< Allocate a large-sized area on the stack.
  UOP_AllocSmall,     ///< Allocate a small-sized area on the stack.
  UOP_SetFPReg,       ///< Establish the frame pointer register.
  UOP_SaveNonVol,     ///< Save a nonvolatile integer register on the stack.
  UOP_SaveNonVolBig,  ///< Save a nonvolatile integer register with a large offset.
  UOP_Epilog,         ///< Describe an epilog region.
  UOP_SpareCode,      ///< Reserved spare opcode.
  UOP_SaveXMM128,     ///< Save a 128-bit XMM register on the stack.
  UOP_SaveXMM128Big,  ///< Save a 128-bit XMM register with a large offset.
  UOP_PushMachFrame,  ///< Push a machine frame (interrupt/exception frame).
  // The following set of unwind opcodes is for ARM64.  They are documented at
  // https://docs.microsoft.com/en-us/cpp/build/arm64-exception-handling
  UOP_AllocMedium,    ///< Allocate a medium-sized area on the stack (ARM64).
  UOP_SaveR19R20X,    ///< Save r19/r20 with pre-indexed addressing (ARM64).
  UOP_SaveFPLRX,      ///< Save FP/LR with pre-indexed addressing (ARM64).
  UOP_SaveFPLR,       ///< Save FP/LR on the stack (ARM64).
  UOP_SaveReg,        ///< Save a general-purpose register on the stack (ARM64).
  UOP_SaveRegX,       ///< Save a GPR with pre-indexed addressing (ARM64).
  UOP_SaveRegP,       ///< Save a pair of GPRs on the stack (ARM64).
  UOP_SaveRegPX,      ///< Save a pair of GPRs with pre-indexed addressing (ARM64).
  UOP_SaveLRPair,     ///< Save LR paired with another register (ARM64).
  UOP_SaveFReg,       ///< Save a floating-point register on the stack (ARM64).
  UOP_SaveFRegX,      ///< Save an FP register with pre-indexed addressing (ARM64).
  UOP_SaveFRegP,      ///< Save a pair of FP registers on the stack (ARM64).
  UOP_SaveFRegPX,     ///< Save a pair of FP registers with pre-indexed addressing (ARM64).
  UOP_SetFP,          ///< Set the frame pointer from SP (ARM64).
  UOP_AddFP,          ///< Add an offset to the frame pointer (ARM64).
  UOP_Nop,            ///< No operation; placeholder unwind code.
  UOP_End,            ///< End of the unwind code sequence.
  UOP_SaveNext,       ///< Save the next register in sequence (ARM64).
  UOP_TrapFrame,      ///< Indicate a trap/exception frame (ARM64).
  UOP_Context,        ///< Indicate a context record (ARM64).
  UOP_ECContext,      ///< Indicate an EC context record (ARM64).
  UOP_ClearUnwoundToCall, ///< Clear the unwound-to-call state (ARM64).
  UOP_PACSignLR,      ///< PAC-sign the link register (ARM64).
  UOP_SaveAnyRegI,    ///< Save any integer register (ARM64).
  UOP_SaveAnyRegIP,   ///< Save any integer register paired (ARM64).
  UOP_SaveAnyRegD,    ///< Save any D-register (ARM64).
  UOP_SaveAnyRegDP,   ///< Save any D-register paired (ARM64).
  UOP_SaveAnyRegQ,    ///< Save any Q-register (ARM64).
  UOP_SaveAnyRegQP,   ///< Save any Q-register paired (ARM64).
  UOP_SaveAnyRegIX,   ///< Save any integer register with pre-index (ARM64).
  UOP_SaveAnyRegIPX,  ///< Save any integer register pair with pre-index (ARM64).
  UOP_SaveAnyRegDX,   ///< Save any D-register with pre-index (ARM64).
  UOP_SaveAnyRegDPX,  ///< Save any D-register pair with pre-index (ARM64).
  UOP_SaveAnyRegQX,   ///< Save any Q-register with pre-index (ARM64).
  UOP_SaveAnyRegQPX,  ///< Save any Q-register pair with pre-index (ARM64).
  UOP_AllocZ,         ///< Allocate Z-register save area (ARM64).
  UOP_SaveZReg,       ///< Save a Z-register (ARM64).
  UOP_SavePReg,       ///< Save a P-register (ARM64).

  // The following set of unwind opcodes is for ARM.  They are documented at
  // https://docs.microsoft.com/en-us/cpp/build/arm-exception-handling

  // Stack allocations use UOP_AllocSmall, UOP_AllocLarge from above, plus
  // the following. AllocSmall, AllocLarge and AllocHuge represent a 16 bit
  // instruction, while the WideAlloc* opcodes represent a 32 bit instruction.
  // Small can represent a stack offset of 0x7f*4 (252) bytes, Medium can
  // represent up to 0x3ff*4 (4092) bytes, Large up to 0xffff*4 (262140) bytes,
  // and Huge up to 0xffffff*4 (67108860) bytes.
  UOP_AllocHuge,              ///< Allocate a huge-sized area on the stack (ARM).
  UOP_WideAllocMedium,        ///< Wide (32-bit) medium stack allocation (ARM).
  UOP_WideAllocLarge,         ///< Wide (32-bit) large stack allocation (ARM).
  UOP_WideAllocHuge,          ///< Wide (32-bit) huge stack allocation (ARM).

  UOP_WideSaveRegMask,        ///< Wide save of registers from a bitmask (ARM).
  UOP_SaveSP,                 ///< Save the stack pointer (ARM).
  UOP_SaveRegsR4R7LR,         ///< Save r4-r7 and LR (ARM).
  UOP_WideSaveRegsR4R11LR,    ///< Wide save of r4-r11 and LR (ARM).
  UOP_SaveFRegD8D15,          ///< Save floating-point registers d8-d15 (ARM).
  UOP_SaveRegMask,            ///< Save registers from a bitmask (ARM).
  UOP_SaveLR,                 ///< Save the link register (ARM).
  UOP_SaveFRegD0D15,          ///< Save floating-point registers d0-d15 (ARM).
  UOP_SaveFRegD16D31,         ///< Save floating-point registers d16-d31 (ARM).
  // Using UOP_Nop from above
  UOP_WideNop,                ///< Wide (32-bit) no-operation placeholder (ARM).
  // Using UOP_End from above
  UOP_EndNop,                 ///< End unwind codes with a narrow NOP (ARM).
  UOP_WideEndNop,             ///< End unwind codes with a wide NOP (ARM).
  // A custom unspecified opcode, consisting of one or more bytes. This
  // allows producing opcodes in the implementation defined/reserved range.
  UOP_Custom, ///< Custom/implementation-defined unwind opcode sequence.

  // V3-only x86_64 opcodes. They are documented at
  // https://learn.microsoft.com/en-us/cpp/build/x64-unwind-information-v3
  UOP_Push2, ///< PUSH2 — two registers in one instruction (x86_64 V3).
};

/// UnwindCode - This union describes a single operation in a function prolog,
/// or part thereof.
union UnwindCode {
  /// Byte-oriented view of a single unwind code slot.
  struct {
    /// Offset in the prolog of the instruction that performs this operation.
    uint8_t CodeOffset;
    /// Packed unwind opcode (low nibble) and operation info (high nibble).
    uint8_t UnwindOpAndOpInfo;
  } u; ///< Byte-oriented view of this unwind code.
  /// Frame offset when this slot encodes a scaled stack displacement.
  support::ulittle16_t FrameOffset;

  /// Return the unwind opcode from the low nibble of UnwindOpAndOpInfo.
  ///
  /// \returns The unwind opcode.
  uint8_t getUnwindOp() const {
    return u.UnwindOpAndOpInfo & 0x0F;
  }
  /// Return the operation info from the high nibble of UnwindOpAndOpInfo.
  ///
  /// \returns The operation info.
  uint8_t getOpInfo() const {
    return (u.UnwindOpAndOpInfo >> 4) & 0x0F;
  }
  /// Gets the offset for an UOP_Epilog unwind code.
  ///
  /// \returns The epilog offset encoded in this unwind code.
  uint32_t getEpilogOffset() const {
    assert(getUnwindOp() == UOP_Epilog);
    return (getOpInfo() << 8) | static_cast<uint32_t>(u.CodeOffset);
  }
};

/// Flags stored in the VersionAndFlags field of UnwindInfo.
enum {
  /// UNW_ExceptionHandler - Specifies that this function has an exception
  /// handler.
  UNW_ExceptionHandler = 0x01,
  /// UNW_TerminateHandler - Specifies that this function has a termination
  /// handler.
  UNW_TerminateHandler = 0x02,
  /// UNW_ChainInfo - Specifies that this UnwindInfo structure is chained to
  /// another one.
  UNW_ChainInfo = 0x04,
  /// UNW_FlagLarge - V3 only. When set, the header is 5 bytes (an extra
  /// UNWIND_INFO_LARGE_V3 byte follows), SizeOfProlog extends to 16 bits,
  /// and prolog IP offset entries are 16-bit.
  UNW_FlagLarge = 0x08
};

/// RuntimeFunction - An entry in the table of functions with unwind info.
struct RuntimeFunction {
  /// Image-relative start address of the function.
  support::ulittle32_t StartAddress;
  /// Image-relative end address of the function.
  support::ulittle32_t EndAddress;
  /// Image-relative offset of the associated UnwindInfo.
  support::ulittle32_t UnwindInfoOffset;
};

/// UnwindInfo - An entry in the exception table.
struct UnwindInfo {
  /// Packed version (low 3 bits) and flags (upper bits).
  uint8_t VersionAndFlags;
  /// Size of the function prolog in bytes.
  uint8_t PrologSize;
  /// Number of slots in the UnwindCodes array.
  uint8_t NumCodes;
  /// Packed frame register number (low nibble) and scaled frame offset (high).
  uint8_t FrameRegisterAndOffset;
  /// Variable-length array of unwind operation codes.
  UnwindCode UnwindCodes[1];

  /// Return the unwind info version from VersionAndFlags.
  ///
  /// \returns The unwind info version (low 3 bits of VersionAndFlags).
  uint8_t getVersion() const {
    return VersionAndFlags & 0x07;
  }
  /// Return the unwind info flags from VersionAndFlags.
  ///
  /// \returns The unwind info flags (upper bits of VersionAndFlags).
  uint8_t getFlags() const {
    return (VersionAndFlags >> 3) & 0x1f;
  }
  /// Return the frame pointer register number, or 0 if none.
  ///
  /// \returns The frame pointer register number, or 0 if none.
  uint8_t getFrameRegister() const {
    return FrameRegisterAndOffset & 0x0f;
  }
  /// Return the scaled frame register offset from FrameRegisterAndOffset.
  ///
  /// \returns The scaled frame register offset.
  uint8_t getFrameOffset() const {
    return (FrameRegisterAndOffset >> 4) & 0x0f;
  }

  // The data after unwindCodes depends on flags.
  // If UNW_ExceptionHandler or UNW_TerminateHandler is set then follows
  // the address of the language-specific exception handler.
  // If UNW_ChainInfo is set then follows a RuntimeFunction which defines
  // the chained unwind info.
  // For more information please see MSDN at:
  // http://msdn.microsoft.com/en-us/library/ddssxxy8.aspx

  /// Return pointer to language specific data part of UnwindInfo.
  ///
  /// \returns Pointer to the language-specific data following the unwind codes.
  void *getLanguageSpecificData() {
    return reinterpret_cast<void *>(&UnwindCodes[(NumCodes+1) & ~1]);
  }

  /// Return pointer to language specific data part of UnwindInfo.
  ///
  /// \returns Pointer to the language-specific data following the unwind codes.
  const void *getLanguageSpecificData() const {
    return reinterpret_cast<const void *>(&UnwindCodes[(NumCodes + 1) & ~1]);
  }

  /// Return image-relative offset of language-specific exception handler.
  ///
  /// \returns Image-relative offset of the language-specific exception handler.
  uint32_t getLanguageSpecificHandlerOffset() const {
    return *reinterpret_cast<const support::ulittle32_t *>(
               getLanguageSpecificData());
  }

  /// Set image-relative offset of language-specific exception handler.
  ///
  /// \param offset Image-relative offset of the handler.
  void setLanguageSpecificHandlerOffset(uint32_t offset) {
    *reinterpret_cast<support::ulittle32_t *>(getLanguageSpecificData()) =
        offset;
  }

  /// Return pointer to exception-specific data.
  ///
  /// \returns Pointer to the exception-specific data following the handler.
  void *getExceptionData() {
    return reinterpret_cast<void *>(reinterpret_cast<uint32_t *>(
                                                  getLanguageSpecificData())+1);
  }

  /// Return pointer to chained unwind info.
  ///
  /// \returns Pointer to the chained RuntimeFunction entry.
  RuntimeFunction *getChainedFunctionEntry() {
    return reinterpret_cast<RuntimeFunction *>(getLanguageSpecificData());
  }

  /// Return pointer to chained unwind info.
  ///
  /// \returns Pointer to the chained RuntimeFunction entry.
  const RuntimeFunction *getChainedFunctionEntry() const {
    return reinterpret_cast<const RuntimeFunction *>(getLanguageSpecificData());
  }
};

//===----------------------------------------------------------------------===//
// V3 Unwind Information
//===----------------------------------------------------------------------===//

/// V3 Winding Operation Descriptor opcodes.
enum WODOpcode : uint8_t {
  WOD_SET_FPREG = 0,            ///< Set the frame pointer register (8-bit opcode, 2 bytes).
  WOD_ALLOC_HUGE = 1,           ///< Huge stack allocation (8-bit opcode, 5 bytes).
  WOD_ALLOC_LARGE = 2,          ///< Large stack allocation (8-bit opcode, 3 bytes).
  WOD_PUSH_CANONICAL_FRAME = 3, ///< Push a canonical frame (8-bit opcode, 2 bytes).
  WOD_PUSH = 4,                 ///< Push a register (3-bit opcode, 1 byte).
  WOD_SAVE_NONVOL_FAR = 5,      ///< Save a nonvolatile register far (3-bit opcode, 5 bytes).
  WOD_SAVE_NONVOL = 6,          ///< Save a nonvolatile register (3-bit opcode, 3 bytes).
  WOD_PUSH_CONSECUTIVE_2 = 7,   ///< Push two consecutive registers (3-bit opcode, 1 byte).
  WOD_ALLOC_SMALL = 8,          ///< Small stack allocation (4-bit opcode, 1 byte).
  WOD_SAVE_XMM128_FAR = 9,      ///< Save an XMM128 register far (4-bit opcode, 5 bytes).
  WOD_SAVE_XMM128 = 10,         ///< Save an XMM128 register (4-bit opcode, 3 bytes).
  WOD_PUSH2 = 32,               ///< Push two arbitrary registers (6-bit opcode, 2 bytes).
};

/// V3 EPILOG_INFO flags.
enum EpilogInfoFlagsV3 : uint8_t {
  /// Transfer control to a parent fragment's epilog.
  EPILOG_PARENT_FRAGMENT_TRANSFER = 0x01,
  /// When set, the extended descriptor uses EPILOG_INFO_LARGE_EX_V3 (16-bit
  /// IpOffsetOfLastInstruction) and the IP offset array uses 16-bit entries.
  EPILOG_INFO_LARGE = 0x02,
};

/// Decoded V3 Winding Operation Descriptor.
struct DecodedWOD {
  /// WOD opcode identifying the operation.
  WODOpcode Opcode;
  /// Primary register for applicable ops (5-bit for int, 4-bit for XMM).
  uint8_t Register;
  /// Second register for WOD_PUSH2.
  uint8_t Register2;
  // TODO: Define a named enum for WOD_PUSH_CANONICAL_FRAME Type values once
  // the Windows x64 Unwind V3 spec is finalized. The set of valid values is
  // defined by the OS (see the Windows SDK headers) but is not yet stable.
  /// Canonical frame type for WOD_PUSH_CANONICAL_FRAME.
  uint8_t Type;
  /// Number of bytes this WOD consumed in the pool (max 5).
  uint8_t ByteSize;
  /// Final computed allocation size for alloc ops.
  uint32_t Size;
  /// Final computed stack displacement for save ops.
  uint32_t Displacement;
};

/// Decoded V3 epilog descriptor.
struct DecodedEpilogV3 {
  /// Epilog info flags (see EpilogInfoFlagsV3).
  uint8_t Flags;
  /// Number of WOD operations in this epilog.
  uint8_t NumberOfOps;
  /// IP offset of the last instruction in the epilog.
  uint16_t IpOffsetOfLastInstruction;
  /// Index of the first WOD in the shared pool for this epilog.
  uint16_t FirstOp;
  /// Resolved absolute epilog offset (accumulated from deltas).
  int32_t EpilogOffset;
  /// Per-instruction IP offsets within the epilog.
  SmallVector<uint16_t, 8> IpOffsets;

  /// Whether the EPILOG_INFO_LARGE flag is set.
  ///
  /// \returns True if the EPILOG_INFO_LARGE flag is set.
  bool isLarge() const { return Flags & EPILOG_INFO_LARGE; }
};

/// Decoded V3 UNWIND_INFO.
struct DecodedUnwindInfoV3 {
  /// Unwind info version number.
  uint8_t Version;
  /// Unwind info flags (see UNW_* constants).
  uint8_t Flags;
  /// Number of payload words following the header.
  uint8_t PayloadWords;
  /// Number of WOD operations in the prolog.
  uint8_t NumberOfOps;
  /// Number of epilog descriptors.
  uint8_t NumberOfEpilogs;
  /// Size of the function prolog in bytes.
  uint16_t SizeOfProlog;
  /// Total bytes consumed by header + payload (used to locate handler/chain).
  uint16_t PayloadSize;
  /// IP offsets of prolog instructions.
  SmallVector<uint16_t, 8> PrologIpOffsets;
  /// Decoded epilog descriptors.
  SmallVector<DecodedEpilogV3, 4> Epilogs;
  /// Raw byte pool of winding operation descriptors.
  ArrayRef<uint8_t> WODPool;

  /// Whether the UNW_FlagLarge flag is set.
  ///
  /// \returns True if the UNW_FlagLarge flag is set.
  bool isLarge() const { return Flags & UNW_FlagLarge; }
};

/// Return the register name for a 5-bit AMD64 integer register number.
/// Covers 0-15 (RAX-R15) and 16-31 (R16-R31 for APX).
///
/// \param Reg 5-bit AMD64 integer register number.
/// \returns The name of the register.
LLVM_ABI StringRef getRegisterNameV3(unsigned Reg);

/// Decode one WOD from the pool at the given byte offset.
/// Returns an error on malformed data.
///
/// \param Pool Byte pool containing winding operation descriptors.
/// \param Offset Byte offset within \p Pool at which to decode.
/// \returns The decoded WOD, or an error on malformed data.
LLVM_ABI Expected<DecodedWOD> decodeWOD(ArrayRef<uint8_t> Pool,
                                        unsigned Offset);

/// Parse a V3 UNWIND_INFO from raw bytes.
/// Returns an error on malformed data.
///
/// \param Data Raw bytes of a V3 UNWIND_INFO record.
/// \returns The decoded unwind info, or an error on malformed data.
LLVM_ABI Expected<DecodedUnwindInfoV3>
decodeUnwindInfoV3(ArrayRef<uint8_t> Data);

} // End of namespace Win64EH
} // End of namespace llvm

#endif
