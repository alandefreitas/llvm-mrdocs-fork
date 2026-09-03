//===- DWARFDataExtractorSimple.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_LOWLEVEL_DWARFDATAEXTRACTORSIMPLE_H
#define LLVM_DEBUGINFO_DWARF_LOWLEVEL_DWARFDATAEXTRACTORSIMPLE_H

#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/MathExtras.h"

namespace llvm {

/// A DataExtractor suitable use for parsing dwarf from memory.  Clients use
/// Relocator::getRelocatedValueImpl to relocate values as appropriate.

template <typename Relocator>
class DWARFDataExtractorBase : public DataExtractor {
  unsigned AddressSize;

public:
  /// Construct from string data with endianness and address size.
  ///
  /// \param Data String data to extract from.
  /// \param IsLittleEndian True if the data is little-endian.
  /// \param AddressSize Size in bytes of an address.
  DWARFDataExtractorBase(StringRef Data, bool IsLittleEndian,
                         unsigned AddressSize)
      : DataExtractor(Data, IsLittleEndian), AddressSize(AddressSize) {}

  /// Construct from a byte array with endianness and address size.
  ///
  /// \param Data Byte array to extract from.
  /// \param IsLittleEndian True if the data is little-endian.
  /// \param AddressSize Size in bytes of an address.
  DWARFDataExtractorBase(ArrayRef<uint8_t> Data, bool IsLittleEndian,
                         unsigned AddressSize)
      : DataExtractor(Data, IsLittleEndian), AddressSize(AddressSize) {}

  /// Get the address size for this extractor.
  ///
  /// \return Address size in bytes.
  unsigned getAddressSize() const { return AddressSize; }

  /// Set the address size for this extractor.
  ///
  /// \param Size New address size in bytes.
  void setAddressSize(unsigned Size) { AddressSize = Size; }

  //------------------------------------------------------------------
  /// Extract an address from \a *OffsetPtr.
  ///
  /// Extract a single address from the data and update the offset
  /// pointed to by \a OffsetPtr. The size of the extracted address
  /// is \a getAddressSize(), so the address size has to be
  /// set correctly prior to extracting any address values.
  ///
  /// @param[in,out] OffsetPtr
  ///     A pointer to an offset within the data that will be advanced
  ///     by the appropriate number of bytes if the value is extracted
  ///     correctly. If the offset is out of bounds or there are not
  ///     enough bytes to extract this value, the offset will be left
  ///     unmodified.
  ///
  /// @return
  ///     The extracted address value as a 64 integer.
  uint64_t getAddress(uint64_t *OffsetPtr) const {
    return getUnsigned(OffsetPtr, AddressSize);
  }

  /// Extract an address-sized unsigned integer from the cursor.
  ///
  /// In case of an extraction error, or if the cursor is already in an
  /// error state, zero is returned.
  ///
  /// \param C Cursor providing the offset and receiving any extraction error.
  /// \return The extracted address value, or zero on error.
  uint64_t getAddress(Cursor &C) const { return getUnsigned(C, AddressSize); }

  /// Test the availability of enough bytes of data for an address from
  /// \a Offset. The size of an address is \a getAddressSize().
  ///
  /// \param Offset Byte offset at which to test for an address.
  ///
  /// @return
  ///     \b true if \a Offset is a valid offset and there are enough
  ///     bytes for an address available at that offset, \b false
  ///     otherwise.
  bool isValidOffsetForAddress(uint64_t Offset) const {
    return isValidOffsetForDataOfSize(Offset, AddressSize);
  }

  /// Extracts a value and returns it as adjusted by the Relocator.
  ///
  /// \param Size Number of bytes to extract.
  /// \param Off Offset into the data; advanced past the value on success.
  /// \param SectionIndex If non-null, set to the section index of the
  ///        relocated value.
  /// \param Err Optional error out-parameter.
  /// \return The extracted value after relocation adjustment.
  uint64_t getRelocatedValue(uint32_t Size, uint64_t *Off,
                             uint64_t *SectionIndex = nullptr,
                             Error *Err = nullptr) const {
    return static_cast<const Relocator *>(this)->getRelocatedValueImpl(
        Size, Off, SectionIndex, Err);
  }

  /// Extract a relocated value using a cursor for offset and error state.
  ///
  /// \param C Cursor providing the offset and receiving any extraction error.
  /// \param Size Number of bytes to extract.
  /// \param SectionIndex If non-null, set to the section index of the
  ///        relocated value.
  /// \return The extracted value after relocation adjustment.
  uint64_t getRelocatedValue(Cursor &C, uint32_t Size,
                             uint64_t *SectionIndex = nullptr) const {
    return getRelocatedValue(Size, &getOffset(C), SectionIndex, &getError(C));
  }

  /// Extracts an address-sized relocated value.
  ///
  /// \param Off Offset into the data; advanced past the address on success.
  /// \param SecIx If non-null, set to the section index of the relocated
  ///        address.
  /// \return The relocated address value.
  uint64_t getRelocatedAddress(uint64_t *Off, uint64_t *SecIx = nullptr) const {
    return getRelocatedValue(getAddressSize(), Off, SecIx);
  }

  /// Extract an address-sized relocated value using a cursor.
  ///
  /// \param C Cursor providing the offset and receiving any extraction error.
  /// \param SecIx If non-null, set to the section index of the relocated
  ///        address.
  /// \return The relocated address value.
  uint64_t getRelocatedAddress(Cursor &C, uint64_t *SecIx = nullptr) const {
    return getRelocatedValue(getAddressSize(), &getOffset(C), SecIx,
                             &getError(C));
  }

  /// Read a DWARF initial-length field and its implied format.
  ///
  /// The field is either a 32-bit value smaller than 0xfffffff0, or
  /// 0xffffffff followed by a 64-bit length. Returns the length and the
  /// encoded DWARF format. On error returns {0, DWARF32} and leaves the
  /// offset unchanged.
  ///
  /// \param Off Offset into the extractor; advanced past the field on success.
  /// \param Err Optional error out-parameter.
  /// \return Pair of (length, DWARF32/DWARF64 format).
  std::pair<uint64_t, dwarf::DwarfFormat>
  getInitialLength(uint64_t *Off, Error *Err = nullptr) const {
    ErrorAsOutParameter ErrAsOut(Err);
    if (Err && *Err)
      return {0, dwarf::DWARF32};

    Cursor C(*Off);
    uint64_t Length = getRelocatedValue(C, 4);
    dwarf::DwarfFormat Format = dwarf::DWARF32;
    if (Length == dwarf::DW_LENGTH_DWARF64) {
      Length = getRelocatedValue(C, 8);
      Format = dwarf::DWARF64;
    } else if (Length >= dwarf::DW_LENGTH_lo_reserved) {
      cantFail(C.takeError());
      if (Err)
        *Err = createStringError(
            std::errc::invalid_argument,
            "unsupported reserved unit length of value 0x%8.8" PRIx64, Length);
      return {0, dwarf::DWARF32};
    }

    if (C) {
      *Off = C.tell();
      return {Length, Format};
    }
    if (Err)
      *Err = C.takeError();
    else
      consumeError(C.takeError());
    return {0, dwarf::DWARF32};
  }

  /// Read a DWARF initial-length field using a Cursor for offset and error.
  ///
  /// \param C Cursor providing the offset and receiving any extraction error.
  /// \return Pair of (length, DWARF32/DWARF64 format).
  std::pair<uint64_t, dwarf::DwarfFormat> getInitialLength(Cursor &C) const {
    return getInitialLength(&getOffset(C), &getError(C));
  }

  /// Extract a DWARF-encoded pointer at \p Offset using \p Encoding.
  ///
  /// There is a DWARF encoding that uses a PC-relative adjustment.
  /// For these values, \p PCRelOffset is used to fix them, which should
  /// reflect the absolute address of this pointer.
  ///
  /// \param Offset Offset into the data; advanced past the pointer on success.
  /// \param Encoding DWARF pointer encoding (DW_EH_PE_*).
  /// \param PCRelOffset Absolute address used to fix up PC-relative encodings.
  /// \return The decoded pointer value, or std::nullopt if omitted or unsupported.
  std::optional<uint64_t> getEncodedPointer(uint64_t *Offset, uint8_t Encoding,
                                            uint64_t PCRelOffset) const {
    if (Encoding == dwarf::DW_EH_PE_omit)
      return std::nullopt;

    uint64_t Result = 0;
    uint64_t OldOffset = *Offset;
    // First get value
    switch (Encoding & 0x0F) {
    case dwarf::DW_EH_PE_absptr:
      switch (getAddressSize()) {
      case 2:
      case 4:
      case 8:
        Result = getUnsigned(Offset, getAddressSize());
        break;
      default:
        return std::nullopt;
      }
      break;
    case dwarf::DW_EH_PE_uleb128:
      Result = getULEB128(Offset);
      break;
    case dwarf::DW_EH_PE_sleb128:
      Result = getSLEB128(Offset);
      break;
    case dwarf::DW_EH_PE_udata2:
      Result = getUnsigned(Offset, 2);
      break;
    case dwarf::DW_EH_PE_udata4:
      Result = getUnsigned(Offset, 4);
      break;
    case dwarf::DW_EH_PE_udata8:
      Result = getUnsigned(Offset, 8);
      break;
    case dwarf::DW_EH_PE_sdata2:
      Result = getSigned(Offset, 2);
      break;
    case dwarf::DW_EH_PE_sdata4:
      Result = SignExtend64<32>(getRelocatedValue(4, Offset));
      break;
    case dwarf::DW_EH_PE_sdata8:
      Result = getRelocatedValue(8, Offset);
      break;
    default:
      return std::nullopt;
    }
    // Then add relative offset, if required
    switch (Encoding & 0x70) {
    case dwarf::DW_EH_PE_absptr:
      // do nothing
      break;
    case dwarf::DW_EH_PE_pcrel:
      Result += PCRelOffset;
      break;
    case dwarf::DW_EH_PE_datarel:
    case dwarf::DW_EH_PE_textrel:
    case dwarf::DW_EH_PE_funcrel:
    case dwarf::DW_EH_PE_aligned:
    default:
      *Offset = OldOffset;
      return std::nullopt;
    }

    return Result;
  }
};

/// Non-relocating DWARF data extractor for memory-backed debug info.
class DWARFDataExtractorSimple
    : public DWARFDataExtractorBase<DWARFDataExtractorSimple> {
public:
  /// Inherit constructors from DWARFDataExtractorBase.
  using DWARFDataExtractorBase::DWARFDataExtractorBase;

  /// Read an unsigned value of \p Size bytes without applying relocations.
  ///
  /// \param Size Number of bytes to extract.
  /// \param Off Offset into the data; advanced past the value on success.
  /// \param SectionIndex Must be null; section indices are unsupported.
  /// \param Err Optional error out-parameter.
  /// \return The extracted unsigned value with no relocation applied.
  uint64_t getRelocatedValueImpl(uint32_t Size, uint64_t *Off,
                                 uint64_t *SectionIndex = nullptr,
                                 Error *Err = nullptr) const {
    assert(SectionIndex == nullptr &&
           "DWARFDATAExtractorSimple cannot take section indices.");
    return getUnsigned(Off, Size, Err);
  }
};

} // end namespace llvm
#endif // LLVM_DEBUGINFO_DWARF_LOWLEVEL_DWARFDATAEXTRACTORSIMPLE_H
