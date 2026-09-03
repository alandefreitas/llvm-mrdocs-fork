//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file contains data-structure definitions and constants to support
/// unwinding based on .sframe sections.  This only supports SFRAME_VERSION_2
/// as described at https://sourceware.org/binutils/docs/sframe-spec.html
//===----------------------------------------------------------------------===//

#ifndef LLVM_BINARYFORMAT_SFRAME_H
#define LLVM_BINARYFORMAT_SFRAME_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/Endian.h"

namespace llvm {

template <typename, unsigned> class EnumStrings;

/// Constants and structures for the SFrame (.sframe) unwind format.
namespace sframe {

// Expanded (instead of the macro) so MrDocs can attach docs to each using.
/// Bring bitmask enum bitwise NOT into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator~;
/// Bring bitmask enum bitwise OR into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator|;
/// Bring bitmask enum bitwise AND into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator&;
/// Bring bitmask enum bitwise XOR into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator^;
/// Bring bitmask enum left-shift into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator<<;
/// Bring bitmask enum right-shift into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator>>;
/// Bring bitmask enum in-place OR into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator|=;
/// Bring bitmask enum in-place AND into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator&=;
/// Bring bitmask enum in-place XOR into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator^=;
/// Bring bitmask enum in-place left-shift into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator<<=;
/// Bring bitmask enum in-place right-shift into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator>>=;
/// Bring bitmask enum logical-not into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator!;
/// Bring bitmask enum any-bits-set test into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::any;

/// Magic number identifying an SFrame section (0xdee2).
constexpr uint16_t Magic = 0xdee2;

/// SFrame format version stored in the preamble.
enum class Version : uint8_t {
  /// SFrame version 1 (obsolete).
  V1 = 0x01,
  /// SFrame version 2.
  V2 = 0x02,
};

/// Section-wide flags stored in the SFrame preamble.
enum class Flags : uint8_t {
  /// Function descriptor entries are sorted by PC.
  FDESorted = 0x01,
  /// All functions in the object preserve the frame pointer.
  FramePointer = 0x02,
  /// FDE start addresses are PC-relative to the StartAddress field itself.
  FDEFuncStartPCRel = 0x04,
  /// Combination of all flags defined for SFrame version 2.
  V2AllFlags = FDESorted | FramePointer | FDEFuncStartPCRel,
  LLVM_MARK_AS_BITMASK_ENUM(/*LargestValue=*/0xff),
};

/// ABI and architecture identifier stored in the SFrame header.
enum class ABI : uint8_t {
  /// AArch64 big-endian.
  AArch64EndianBig = 0x01,
  /// AArch64 little-endian.
  AArch64EndianLittle = 0x02,
  /// AMD64 little-endian.
  AMD64EndianLittle = 0x03,
};

/// SFrame FRE Types. Bits 0-3 of FuncDescEntry.Info.
enum class FREType : uint8_t {
#define HANDLE_SFRAME_FRE_TYPE(CODE, NAME) NAME = CODE,
#include "llvm/BinaryFormat/SFrameConstants.def"
};

/// SFrame FDE Types. Bit 4 of FuncDescEntry.Info.
enum class FDEType : uint8_t {
#define HANDLE_SFRAME_FDE_TYPE(CODE, NAME) NAME = CODE,
#include "llvm/BinaryFormat/SFrameConstants.def"
};

/// Speficies key used for signing return addresses. Bit 5 of
/// FuncDescEntry.Info.
enum class AArch64PAuthKey : uint8_t {
#define HANDLE_SFRAME_AARCH64_PAUTH_KEY(CODE, NAME) NAME = CODE,
#include "llvm/BinaryFormat/SFrameConstants.def"
};

/// Size of stack offsets. Bits 6-7 of FREInfo.Info.
enum class FREOffset : uint8_t {
#define HANDLE_SFRAME_FRE_OFFSET(CODE, NAME) NAME = CODE,
#include "llvm/BinaryFormat/SFrameConstants.def"
};

/// Stack frame base register. Bit 0 of FREInfo.Info.
enum class BaseReg : uint8_t {
  /// Canonical frame address is based on the frame pointer.
  FP = 0,
  /// Canonical frame address is based on the stack pointer.
  SP = 1,
};

namespace detail {
template <typename T, endianness E>
using packed =
    support::detail::packed_endian_specific_integral<T, E, support::unaligned>;
}

/// Fixed-size SFrame preamble that begins every SFrame section.
template <endianness E> struct Preamble {
  /// Magic number; should equal \c Magic in target endianness.
  detail::packed<uint16_t, E> Magic;
  /// SFrame format version.
  detail::packed<enum Version, E> Version;
  /// Section-wide flags.
  detail::packed<enum Flags, E> Flags;
};

/// SFrame section header following the preamble.
template <endianness E> struct Header {
  /// Section preamble (magic, version, and flags).
  struct Preamble<E> Preamble;
  /// ABI/architecture identifier for this section.
  detail::packed<ABI, E> ABIArch;
  /// Fixed CFA-relative offset used to recover the frame pointer, if any.
  detail::packed<int8_t, E> CFAFixedFPOffset;
  /// Fixed CFA-relative offset used to recover the return address, if any.
  detail::packed<int8_t, E> CFAFixedRAOffset;
  /// Size in bytes of the optional auxiliary header that follows this header.
  detail::packed<uint8_t, E> AuxHdrLen;
  /// Number of function descriptor entries in the FDE sub-section.
  detail::packed<uint32_t, E> NumFDEs;
  /// Number of frame row entries in the FRE sub-section.
  detail::packed<uint32_t, E> NumFREs;
  /// Length in bytes of the FRE sub-section.
  detail::packed<uint32_t, E> FRELen;
  /// Offset in bytes from the end of the header to the FDE sub-section.
  detail::packed<uint32_t, E> FDEOff;
  /// Offset in bytes from the end of the header to the FRE sub-section.
  detail::packed<uint32_t, E> FREOff;
};

/// Packed FDE info byte describing FRE encoding and related attributes.
template <endianness E> struct FDEInfo {
  /// Raw FDE info byte.
  detail::packed<uint8_t, E> Info;

  /// Return the AArch64 pointer-authentication key bit from the info byte.
  /// \returns The AArch64 pointer-authentication key bit (0 or 1).
  uint8_t getPAuthKey() const { return (Info >> 5) & 1; }
  /// Return the FDE type encoded in the info byte.
  /// \returns The FDE type encoded in the info byte.
  FDEType getFDEType() const { return static_cast<FDEType>((Info >> 4) & 1); }
  /// Return the FRE type encoded in the info byte.
  /// \returns The FRE type encoded in the info byte.
  FREType getFREType() const { return static_cast<FREType>(Info & 0xf); }
  /// Set the AArch64 pointer-authentication key bit in the info byte.
  ///
  /// \param P Pointer-authentication key bit (0 or 1).
  void setPAuthKey(uint8_t P) { setFuncInfo(P, getFDEType(), getFREType()); }
  /// Set the FDE type in the info byte.
  ///
  /// \param D FDE type to encode.
  void setFDEType(FDEType D) { setFuncInfo(getPAuthKey(), D, getFREType()); }
  /// Set the FRE type in the info byte.
  ///
  /// \param R FRE type to encode.
  void setFREType(FREType R) { setFuncInfo(getPAuthKey(), getFDEType(), R); }
  /// Set all fields of the FDE info byte at once.
  ///
  /// \param PAuthKey Pointer-authentication key bit (0 or 1).
  /// \param FDE FDE type to encode.
  /// \param FRE FRE type to encode.
  void setFuncInfo(uint8_t PAuthKey, FDEType FDE, FREType FRE) {
    Info = ((PAuthKey & 1) << 5) | ((static_cast<uint8_t>(FDE) & 1) << 4) |
           (static_cast<uint8_t>(FRE) & 0xf);
  }
  /// Return the raw FDE info byte.
  /// \returns The raw FDE info byte.
  uint8_t getFuncInfo() const { return Info; }
};

/// Function descriptor entry describing one function in the SFrame section.
template <endianness E> struct FuncDescEntry {
  /// Offset to the start of the described function.
  detail::packed<int32_t, E> StartAddress;
  /// Size of the function in bytes.
  detail::packed<uint32_t, E> Size;
  /// Offset from the start of the FRE sub-section to this function's FREs.
  detail::packed<uint32_t, E> StartFREOff;
  /// Number of frame row entries for this function.
  detail::packed<uint32_t, E> NumFREs;
  /// Packed FDE info for this function.
  FDEInfo<E> Info;
  /// Size in bytes of the repeating code block when the FDE uses PC masks.
  detail::packed<uint8_t, E> RepSize;
  /// Reserved padding to keep the FDE naturally aligned.
  detail::packed<uint16_t, E> Padding2;
};

/// Packed FRE info byte describing offsets and CFA base register.
template <endianness E> struct FREInfo {
  /// Raw FRE info byte.
  detail::packed<uint8_t, E> Info;

  /// Return whether the return address is signed (mangled with PAC bits).
  /// \returns True if the return address is signed.
  bool isReturnAddressSigned() const { return Info >> 7; }
  /// Return the encoded size of each trailing stack offset.
  /// \returns The encoded size of each trailing stack offset.
  FREOffset getOffsetSize() const {
    return static_cast<FREOffset>((Info >> 5) & 3);
  }
  /// Return the number of trailing stack offsets.
  /// \returns The number of trailing stack offsets.
  uint8_t getOffsetCount() const { return (Info >> 1) & 0xf; }
  /// Return the CFA base register encoded in the info byte.
  /// \returns The CFA base register encoded in the info byte.
  BaseReg getBaseRegister() const { return static_cast<BaseReg>(Info & 1); }
  /// Set whether the return address is signed.
  ///
  /// \param RA True if the return address is signed.
  void setReturnAddressSigned(bool RA) {
    setFREInfo(RA, getOffsetSize(), getOffsetCount(), getBaseRegister());
  }
  /// Set the size of each trailing stack offset.
  ///
  /// \param Sz Offset size to encode.
  void setOffsetSize(FREOffset Sz) {
    setFREInfo(isReturnAddressSigned(), Sz, getOffsetCount(),
               getBaseRegister());
  }
  /// Set the number of trailing stack offsets.
  ///
  /// \param N Offset count to encode (0–15).
  void setOffsetCount(uint8_t N) {
    setFREInfo(isReturnAddressSigned(), getOffsetSize(), N, getBaseRegister());
  }
  /// Set the CFA base register.
  ///
  /// \param Reg Base register to encode.
  void setBaseRegister(BaseReg Reg) {
    setFREInfo(isReturnAddressSigned(), getOffsetSize(), getOffsetCount(), Reg);
  }
  /// Set all fields of the FRE info byte at once.
  ///
  /// \param RA True if the return address is signed.
  /// \param Sz Size of each trailing stack offset.
  /// \param N Number of trailing stack offsets.
  /// \param Reg CFA base register.
  void setFREInfo(bool RA, FREOffset Sz, uint8_t N, BaseReg Reg) {
    Info = ((RA & 1) << 7) | ((static_cast<uint8_t>(Sz) & 3) << 5) |
           ((N & 0xf) << 1) | (static_cast<uint8_t>(Reg) & 1);
  }
  /// Return the raw FRE info byte.
  /// \returns The raw FRE info byte.
  uint8_t getFREInfo() const { return Info; }
};

/// Frame row entry giving unwind info for a PC range within a function.
template <typename T, endianness E> struct FrameRowEntry {
  /// Start address offset of this FRE relative to the function start.
  detail::packed<T, E> StartAddress;
  /// Packed FRE info for this row.
  FREInfo<E> Info;
};

/// Frame row entry whose start address is an 8-bit offset.
template <endianness E> using FrameRowEntryAddr1 = FrameRowEntry<uint8_t, E>;
/// Frame row entry whose start address is a 16-bit offset.
template <endianness E> using FrameRowEntryAddr2 = FrameRowEntry<uint16_t, E>;
/// Frame row entry whose start address is a 32-bit offset.
template <endianness E> using FrameRowEntryAddr4 = FrameRowEntry<uint32_t, E>;

/// Return the enumerator name table for \c Version.
/// \returns The enumerator name table for \c Version.
LLVM_ABI EnumStrings<Version, 1> getVersions();
/// Return the enumerator name table for \c Flags.
/// \returns The enumerator name table for \c Flags.
LLVM_ABI EnumStrings<Flags, 1> getFlags();
/// Return the enumerator name table for \c ABI.
/// \returns The enumerator name table for \c ABI.
LLVM_ABI EnumStrings<ABI, 1> getABIs();
/// Return the enumerator name table for \c FREType.
/// \returns The enumerator name table for \c FREType.
LLVM_ABI EnumStrings<FREType, 1> getFRETypes();
/// Return the enumerator name table for \c FDEType.
/// \returns The enumerator name table for \c FDEType.
LLVM_ABI EnumStrings<FDEType, 1> getFDETypes();
/// Return the enumerator name table for \c AArch64PAuthKey.
/// \returns The enumerator name table for \c AArch64PAuthKey.
LLVM_ABI EnumStrings<AArch64PAuthKey, 1> getAArch64PAuthKeys();
/// Return the enumerator name table for \c FREOffset.
/// \returns The enumerator name table for \c FREOffset.
LLVM_ABI EnumStrings<FREOffset, 1> getFREOffsets();
/// Return the enumerator name table for \c BaseReg.
/// \returns The enumerator name table for \c BaseReg.
LLVM_ABI EnumStrings<BaseReg, 1> getBaseRegisters();

} // namespace sframe
} // namespace llvm

#endif // LLVM_BINARYFORMAT_SFRAME_H
