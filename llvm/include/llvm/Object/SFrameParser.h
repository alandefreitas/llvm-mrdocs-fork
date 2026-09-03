//===- SFrameParser.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_SFRAME_H
#define LLVM_OBJECT_SFRAME_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/fallible_iterator.h"
#include "llvm/BinaryFormat/SFrame.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <cstdint>

namespace llvm {
namespace object {

/// Parser for an SFrame (.sframe) unwind information section.
template <endianness E> class SFrameParser {
  class FallibleFREIterator;

public:
  /// Create a parser over the SFrame section bytes in \p Contents.
  ///
  /// \param Contents Raw bytes of the SFrame section.
  /// \param SectionAddress Virtual address of the SFrame section.
  /// \return A parser for the section, or an error if the contents are invalid.
  static Expected<SFrameParser> create(ArrayRef<uint8_t> Contents,
                                       uint64_t SectionAddress);

  /// Return the SFrame preamble from the section header.
  ///
  /// \return The SFrame preamble from the section header.
  const sframe::Preamble<E> &getPreamble() const { return Header.Preamble; }
  /// Return the SFrame section header.
  ///
  /// \return The SFrame section header.
  const sframe::Header<E> &getHeader() const { return Header; }

  /// Return the optional auxiliary header bytes following the main header.
  ///
  /// \return The auxiliary header bytes, or an error if they are unavailable.
  Expected<ArrayRef<uint8_t>> getAuxHeader() const;

  /// Return true if this ABI stores a fixed CFA-relative RA offset in the
  /// header.
  ///
  /// \return True if this ABI stores a fixed CFA-relative RA offset in the
  /// header.
  bool usesFixedRAOffset() const {
    return getHeader().ABIArch == sframe::ABI::AMD64EndianLittle;
  }
  /// Return true if this ABI stores a fixed CFA-relative FP offset in the
  /// header.
  ///
  /// \return True if this ABI stores a fixed CFA-relative FP offset in the
  /// header.
  bool usesFixedFPOffset() const {
    return false; // Not used in any currently defined ABI.
  }

  /// Array of function descriptor entries in the SFrame section.
  using FDERange = ArrayRef<sframe::FuncDescEntry<E>>;
  /// Return the function descriptor entries in this SFrame section.
  ///
  /// \return The function descriptor entries, or an error on failure.
  Expected<FDERange> fdes() const;

  /// Return the absolute start address of the given FDE.
  ///
  /// Decodes the start address of the given FDE, which must be one of the
  /// objects returned by the `fdes()` function.
  ///
  /// \param FDE Iterator to a function descriptor entry from \c fdes().
  /// \return The absolute start address of \p FDE.
  uint64_t getAbsoluteStartAddress(typename FDERange::iterator FDE) const;

  /// Return the byte offset of the given FDE within the SFrame section.
  ///
  /// Returns the offset (in the SFrame section) of the given FDE, which must be
  /// one of the objects returned by the `fdes()` function.
  ///
  /// \param FDE Iterator to a function descriptor entry from \c fdes().
  /// \return The byte offset of \p FDE within the SFrame section.
  uint64_t offsetOf(typename FDERange::iterator FDE) const;

  /// Decoded frame row entry describing unwind state at a PC offset.
  struct FrameRowEntry {
    /// PC offset of this row relative to the function start address.
    uint32_t StartAddress;
    /// Packed FRE info describing offsets and CFA base register.
    sframe::FREInfo<endianness::native> Info;
    /// Trailing stack offsets associated with this FRE.
    SmallVector<int32_t, 3> Offsets;
  };

  /// Fallible iterator type over frame row entries for a function.
  using fre_iterator = fallible_iterator<FallibleFREIterator>;
  /// Return an iterator range over the frame row entries for \p FDE.
  ///
  /// \param FDE Function descriptor whose FREs should be iterated.
  /// \param Err Set on the first decode error encountered while iterating.
  /// \return An iterator range over the frame row entries for \p FDE.
  iterator_range<fre_iterator> fres(const sframe::FuncDescEntry<E> &FDE,
                                    Error &Err) const;

  /// Return the CFA offset from \p FRE, if present.
  ///
  /// \param FRE Frame row entry to query.
  /// \return The CFA offset from \p FRE, if present.
  std::optional<int32_t> getCFAOffset(const FrameRowEntry &FRE) const;
  /// Return the return-address offset from \p FRE or the fixed header value.
  ///
  /// \param FRE Frame row entry to query.
  /// \return The return-address offset from \p FRE or the fixed header value.
  std::optional<int32_t> getRAOffset(const FrameRowEntry &FRE) const;
  /// Return the frame-pointer offset from \p FRE or the fixed header value.
  ///
  /// \param FRE Frame row entry to query.
  /// \return The frame-pointer offset from \p FRE or the fixed header value.
  std::optional<int32_t> getFPOffset(const FrameRowEntry &FRE) const;
  /// Return any trailing offsets in \p FRE beyond CFA/RA/FP.
  ///
  /// \param FRE Frame row entry to query.
  /// \return Trailing offsets in \p FRE beyond CFA/RA/FP.
  ArrayRef<int32_t> getExtraOffsets(const FrameRowEntry &FRE) const;

private:
  ArrayRef<uint8_t> Data;
  uint64_t SectionAddress;
  const sframe::Header<E> &Header;

  SFrameParser(ArrayRef<uint8_t> Data, uint64_t SectionAddress,
               const sframe::Header<E> &Header)
      : Data(Data), SectionAddress(SectionAddress), Header(Header) {}

  uint64_t getFDEBase() const {
    return sizeof(Header) + Header.AuxHdrLen + Header.FDEOff;
  }

  uint64_t getFREBase() const {
    return getFDEBase() + Header.NumFDEs * sizeof(sframe::FuncDescEntry<E>);
  }
};

template <endianness E> class SFrameParser<E>::FallibleFREIterator {
public:
  // NB: This iterator starts out in the before_begin() state. It must be
  // ++'ed to reach the first element.
  FallibleFREIterator(ArrayRef<uint8_t> Data, sframe::FREType FREType,
                      uint32_t Idx, uint32_t Size, uint64_t Offset)
      : Data(Data), FREType(FREType), Idx(Idx), Size(Size), Offset(Offset) {}

  LLVM_ABI Error inc();
  const FrameRowEntry &operator*() const { return FRE; }

  /// Return true if \p LHS and \p RHS refer to the same FRE position.
  ///
  /// \param LHS Left-hand iterator to compare.
  /// \param RHS Right-hand iterator to compare.
  /// \return True if \p LHS and \p RHS refer to the same FRE position.
  friend bool operator==(const FallibleFREIterator &LHS,
                         const FallibleFREIterator &RHS) {
    assert(LHS.Data.data() == RHS.Data.data());
    assert(LHS.Data.size() == RHS.Data.size());
    assert(LHS.FREType == RHS.FREType);
    assert(LHS.Size == RHS.Size);
    return LHS.Idx == RHS.Idx;
  }

private:
  ArrayRef<uint8_t> Data;
  sframe::FREType FREType;
  uint32_t Idx;
  uint32_t Size;
  uint64_t Offset;
  FrameRowEntry FRE;
};

/// Explicit instantiation of SFrameParser for big-endian SFrame data.
extern template class LLVM_TEMPLATE_ABI SFrameParser<endianness::big>;
/// Explicit instantiation of SFrameParser for little-endian SFrame data.
extern template class LLVM_TEMPLATE_ABI SFrameParser<endianness::little>;

} // end namespace object
} // end namespace llvm

#endif // LLVM_OBJECT_SFRAME_H
