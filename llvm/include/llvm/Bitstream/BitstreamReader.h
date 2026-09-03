//===- BitstreamReader.h - Low-level bitstream reader interface -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header defines the BitstreamReader class.  This class can be used to
// read an arbitrary bitstream, regardless of its contents.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BITSTREAM_BITSTREAMREADER_H
#define LLVM_BITSTREAM_BITSTREAMREADER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Bitstream/BitCodes.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBufferRef.h"
#include <algorithm>
#include <cassert>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace llvm {

/// This class maintains the abbreviations read from a block info block.
class BitstreamBlockInfo {
public:
  /// This contains information emitted to BLOCKINFO_BLOCK blocks. These
  /// describe abbreviations that all blocks of the specified ID inherit.
  struct BlockInfo {
    unsigned BlockID = 0; ///< Block ID these BLOCKINFO records describe.
    std::vector<std::shared_ptr<BitCodeAbbrev>> Abbrevs; ///< Abbrevs inherited by blocks of this ID.
    std::string Name; ///< Optional human-readable name for this block ID.
    std::vector<std::pair<unsigned, std::string>> RecordNames; ///< Optional names for record codes.
  };

private:
  std::vector<BlockInfo> BlockInfoRecords;

public:
  /// If there is block info for the specified ID, return it, otherwise return
  /// null.
  ///
  /// \param BlockID Block ID to look up in the BLOCKINFO records.
  ///
  /// \returns Pointer to the BlockInfo for \p BlockID, or null if none exists.
  const BlockInfo *getBlockInfo(unsigned BlockID) const {
    // Common case, the most recent entry matches BlockID.
    if (!BlockInfoRecords.empty() && BlockInfoRecords.back().BlockID == BlockID)
      return &BlockInfoRecords.back();

    for (const BlockInfo &BI : BlockInfoRecords)
      if (BI.BlockID == BlockID)
        return &BI;
    return nullptr;
  }

  /// Return existing block info for \p BlockID, or create and return a new empty one.
  ///
  /// \param BlockID Block ID to look up or create BLOCKINFO for.
  ///
  /// \returns A mutable reference to the BlockInfo for \p BlockID.
  BlockInfo &getOrCreateBlockInfo(unsigned BlockID) {
    if (const BlockInfo *BI = getBlockInfo(BlockID))
      return *const_cast<BlockInfo*>(BI);

    // Otherwise, add a new record.
    BlockInfoRecords.emplace_back();
    BlockInfoRecords.back().BlockID = BlockID;
    return BlockInfoRecords.back();
  }
};

/// This represents a position within a bitstream. There may be multiple
/// independent cursors reading within one bitstream, each maintaining their
/// own local state.
class SimpleBitstreamCursor {
  ArrayRef<uint8_t> BitcodeBytes;
  size_t NextChar = 0;

public:
  /// Host-sized word type used to buffer unread bitstream data.
  ///
  /// This is the current data we have pulled from the stream but have not
  /// returned to the client. This is specifically and intentionally defined to
  /// follow the word size of the host machine for efficiency. We use word_t in
  /// places that are aware of this to make it perfectly explicit what is going
  /// on.
  using word_t = size_t;

private:
  word_t CurWord = 0;

  /// This is the number of bits in CurWord that are valid. This is always from
  /// [0...bits_of(size_t)-1] inclusive.
  unsigned BitsInCurWord = 0;

public:
  /// Construct an empty cursor with no bitcode buffer.
  SimpleBitstreamCursor() = default;
  /// Construct a cursor over the given bitcode byte array.
  ///
  /// \param BitcodeBytes Bitcode buffer to read from.
  explicit SimpleBitstreamCursor(ArrayRef<uint8_t> BitcodeBytes)
      : BitcodeBytes(BitcodeBytes) {}
  /// Construct a cursor over the given bitcode string bytes.
  ///
  /// \param BitcodeBytes Bitcode bytes as a string reference.
  explicit SimpleBitstreamCursor(StringRef BitcodeBytes)
      : BitcodeBytes(arrayRefFromStringRef(BitcodeBytes)) {}
  /// Construct a cursor over the bytes of \p BitcodeBytes.
  ///
  /// \param BitcodeBytes Memory buffer whose contents are the bitcode bytes.
  explicit SimpleBitstreamCursor(MemoryBufferRef BitcodeBytes)
      : SimpleBitstreamCursor(BitcodeBytes.getBuffer()) {}

  /// True if \p pos is within the buffer or exactly one byte past the end.
  ///
  /// \param pos Byte offset to test against the buffer bounds.
  ///
  /// \returns True if \p pos is a valid skip destination in the buffer.
  bool canSkipToPos(size_t pos) const {
    // pos can be skipped to if it is a valid address or one byte past the end.
    return pos <= BitcodeBytes.size();
  }

  /// True if no unread bits remain in the stream.
  ///
  /// \returns True when the cursor has consumed the entire bitstream.
  bool AtEndOfStream() {
    return BitsInCurWord == 0 && BitcodeBytes.size() <= NextChar;
  }

  /// Return the bit # of the bit we are reading.
  ///
  /// \returns The absolute bit offset of the next unread bit.
  uint64_t GetCurrentBitNo() const {
    return uint64_t(NextChar)*CHAR_BIT - BitsInCurWord;
  }

  /// Return the byte index of the current bit position.
  ///
  /// \returns The current bit position divided by eight.
  uint64_t getCurrentByteNo() const { return GetCurrentBitNo() / 8; }

  /// Return the underlying bitcode byte buffer.
  ///
  /// \returns The bitcode bytes as an ArrayRef.
  ArrayRef<uint8_t> getBitcodeBytes() const { return BitcodeBytes; }

  /// Reset the stream to the specified bit number.
  ///
  /// \param BitNo Absolute bit offset to seek to.
  ///
  /// \returns Error::success() on success, or an Error on failure.
  Error JumpToBit(uint64_t BitNo) {
    size_t ByteNo = size_t(BitNo/8) & ~(sizeof(word_t)-1);
    unsigned WordBitNo = unsigned(BitNo & (sizeof(word_t)*8-1));
    assert(canSkipToPos(ByteNo) && "Invalid location");

    // Move the cursor to the right word.
    NextChar = ByteNo;
    BitsInCurWord = 0;

    // Skip over any bits that are already consumed.
    if (WordBitNo) {
      if (Expected<word_t> Res = Read(WordBitNo))
        return Error::success();
      else
        return Res.takeError();
    }

    return Error::success();
  }

  /// Get a pointer into the bitstream at the specified byte offset.
  ///
  /// \param ByteNo Byte offset into the bitcode buffer.
  /// \param NumBytes Number of bytes expected to be readable from the pointer.
  ///
  /// \returns Pointer to byte \p ByteNo in the bitcode buffer.
  const uint8_t *getPointerToByte(uint64_t ByteNo, uint64_t NumBytes) {
    return BitcodeBytes.data() + ByteNo;
  }

  /// Get a pointer into the bitstream at the specified bit offset.
  ///
  /// The bit offset must be on a byte boundary.
  ///
  /// \param BitNo Bit offset into the stream; must be byte-aligned.
  /// \param NumBytes Number of bytes expected to be readable from the pointer.
  ///
  /// \returns Pointer to the byte containing bit \p BitNo.
  const uint8_t *getPointerToBit(uint64_t BitNo, uint64_t NumBytes) {
    assert(!(BitNo % 8) && "Expected bit on byte boundary");
    return getPointerToByte(BitNo / 8, NumBytes);
  }

  /// Load the next word from the buffer into CurWord.
  ///
  /// \returns Error::success() on success, or an Error if the stream ends
  /// unexpectedly.
  Error fillCurWord() {
    if (NextChar >= BitcodeBytes.size())
      return createStringError(std::errc::io_error,
                               "Unexpected end of file reading %u of %u bytes",
                               NextChar, BitcodeBytes.size());

    // Read the next word from the stream.
    const uint8_t *NextCharPtr = BitcodeBytes.data() + NextChar;
    unsigned BytesRead;
    if (BitcodeBytes.size() >= NextChar + sizeof(word_t)) {
      BytesRead = sizeof(word_t);
      CurWord =
          support::endian::read<word_t, llvm::endianness::little>(NextCharPtr);
    } else {
      // Short read.
      BytesRead = BitcodeBytes.size() - NextChar;
      CurWord = 0;
      for (unsigned B = 0; B != BytesRead; ++B)
        CurWord |= uint64_t(NextCharPtr[B]) << (B * 8);
    }
    NextChar += BytesRead;
    BitsInCurWord = BytesRead * 8;
    return Error::success();
  }

  /// Read \p NumBits bits from the stream.
  ///
  /// \param NumBits Number of bits to read; must be in [1, bits_of(word_t)].
  ///
  /// \returns The bits read as a word_t on success, or an Error on failure.
  Expected<word_t> Read(unsigned NumBits) {
    static const unsigned BitsInWord = sizeof(word_t) * 8;

    assert(NumBits && NumBits <= BitsInWord &&
           "Cannot return zero or more than BitsInWord bits!");

    static const unsigned Mask = sizeof(word_t) > 4 ? 0x3f : 0x1f;

    // If the field is fully contained by CurWord, return it quickly.
    if (BitsInCurWord >= NumBits) {
      word_t R = CurWord & (~word_t(0) >> (BitsInWord - NumBits));

      // Use a mask to avoid undefined behavior.
      CurWord >>= (NumBits & Mask);

      BitsInCurWord -= NumBits;
      return R;
    }

    word_t R = BitsInCurWord ? CurWord : 0;
    unsigned BitsLeft = NumBits - BitsInCurWord;

    if (Error fillResult = fillCurWord())
      return std::move(fillResult);

    // If we run out of data, abort.
    if (BitsLeft > BitsInCurWord)
      return createStringError(std::errc::io_error,
                               "Unexpected end of file reading %u of %u bits",
                               BitsInCurWord, BitsLeft);

    word_t R2 = CurWord & (~word_t(0) >> (BitsInWord - BitsLeft));

    // Use a mask to avoid undefined behavior.
    CurWord >>= (BitsLeft & Mask);

    BitsInCurWord -= BitsLeft;

    R |= R2 << (NumBits - BitsLeft);

    return R;
  }

  /// Read a variable-bit-rate value using \p NumBits-wide chunks.
  ///
  /// \param NumBits Width in bits of each VBR chunk; must be in [1, 32].
  ///
  /// \returns The decoded 32-bit value on success, or an Error on failure.
  Expected<uint32_t> ReadVBR(const unsigned NumBits) {
    Expected<unsigned> MaybeRead = Read(NumBits);
    if (!MaybeRead)
      return MaybeRead;
    uint32_t Piece = MaybeRead.get();

    assert(NumBits <= 32 && NumBits >= 1 && "Invalid NumBits value");
    const uint32_t MaskBitOrder = (NumBits - 1);
    const uint32_t Mask = 1UL << MaskBitOrder;

    if ((Piece & Mask) == 0)
      return Piece;

    uint32_t Result = 0;
    unsigned NextBit = 0;
    while (true) {
      Result |= (Piece & (Mask - 1)) << NextBit;

      if ((Piece & Mask) == 0)
        return Result;

      NextBit += NumBits-1;
      if (NextBit >= 32)
        return createStringError(std::errc::illegal_byte_sequence,
                                 "Unterminated VBR");

      MaybeRead = Read(NumBits);
      if (!MaybeRead)
        return MaybeRead;
      Piece = MaybeRead.get();
    }
  }

  /// Read a variable-bit-rate integer with \p NumBits chunk width.
  ///
  /// The value may be up to 64 bits; the VBR chunk size must still be <= 32.
  ///
  /// \param NumBits Width in bits of each VBR chunk; must be in [1, 32].
  ///
  /// \returns The decoded 64-bit value on success, or an Error on failure.
  Expected<uint64_t> ReadVBR64(const unsigned NumBits) {
    Expected<uint64_t> MaybeRead = Read(NumBits);
    if (!MaybeRead)
      return MaybeRead;
    uint32_t Piece = MaybeRead.get();
    assert(NumBits <= 32 && NumBits >= 1 && "Invalid NumBits value");
    const uint32_t MaskBitOrder = (NumBits - 1);
    const uint32_t Mask = 1UL << MaskBitOrder;

    if ((Piece & Mask) == 0)
      return uint64_t(Piece);

    uint64_t Result = 0;
    unsigned NextBit = 0;
    while (true) {
      Result |= uint64_t(Piece & (Mask - 1)) << NextBit;

      if ((Piece & Mask) == 0)
        return Result;

      NextBit += NumBits-1;
      if (NextBit >= 64)
        return createStringError(std::errc::illegal_byte_sequence,
                                 "Unterminated VBR");

      MaybeRead = Read(NumBits);
      if (!MaybeRead)
        return MaybeRead;
      Piece = MaybeRead.get();
    }
  }

  /// Advance the cursor to the next 32-bit alignment boundary.
  void SkipToFourByteBoundary() {
    // If word_t is 64-bits and if we've read less than 32 bits, just dump
    // the bits we have up to the next 32-bit boundary.
    if (sizeof(word_t) > 4 &&
        BitsInCurWord >= 32) {
      CurWord >>= BitsInCurWord-32;
      BitsInCurWord = 32;
      return;
    }

    BitsInCurWord = 0;
  }

  /// Return the size of the stream in bytes.
  ///
  /// \returns The number of bytes in the underlying bitcode buffer.
  size_t SizeInBytes() const { return BitcodeBytes.size(); }

  /// Skip to the end of the file.
  void skipToEnd() { NextChar = BitcodeBytes.size(); }

  /// Check whether a reservation of Size elements is plausible.
  ///
  /// \param Size Proposed element count to validate against the stream size.
  ///
  /// \returns True if \p Size is smaller than the number of bits in the stream.
  bool isSizePlausible(size_t Size) const {
    // Don't allow reserving more elements than the number of bits, assuming
    // at least one bit is needed to encode an element.
    return Size < BitcodeBytes.size() * 8;
  }
};

/// When advancing through a bitstream cursor, each advance can discover a few
/// different kinds of entries:
struct BitstreamEntry {
  /// Kinds of entries discovered while advancing through a bitstream.
  enum EntryKind {
    Error,    ///< Malformed bitcode was found.
    EndBlock, ///< End of the current block (or end of file, treated as EndBlock).
    SubBlock, ///< Start of a new subblock with a specific block ID.
    Record    ///< A record with a specific AbbrevID.
  } Kind;

  unsigned ID; ///< Block ID for SubBlock, or AbbrevID for Record.

  /// Construct an Error entry for malformed bitcode.
  ///
  /// \returns A BitstreamEntry with Kind set to Error.
  static BitstreamEntry getError() {
    BitstreamEntry E; E.Kind = Error; return E;
  }

  /// Construct an EndBlock entry.
  ///
  /// \returns A BitstreamEntry with Kind set to EndBlock.
  static BitstreamEntry getEndBlock() {
    BitstreamEntry E; E.Kind = EndBlock; return E;
  }

  /// Construct a SubBlock entry for the given block \p ID.
  ///
  /// \param ID Block ID of the subblock being entered.
  ///
  /// \returns A BitstreamEntry with Kind set to SubBlock.
  static BitstreamEntry getSubBlock(unsigned ID) {
    BitstreamEntry E; E.Kind = SubBlock; E.ID = ID; return E;
  }

  /// Construct a Record entry for the given abbreviation ID.
  ///
  /// \param AbbrevID Abbreviation ID of the record.
  ///
  /// \returns A BitstreamEntry with Kind set to Record.
  static BitstreamEntry getRecord(unsigned AbbrevID) {
    BitstreamEntry E; E.Kind = Record; E.ID = AbbrevID; return E;
  }
};

/// This represents a position within a bitcode file, implemented on top of a
/// SimpleBitstreamCursor.
///
/// Unlike iterators, BitstreamCursors are heavy-weight objects that should not
/// be passed by value.
class BitstreamCursor : SimpleBitstreamCursor {
  // This is the declared size of code values used for the current block, in
  // bits.
  unsigned CurCodeSize = 2;

  /// Abbrevs installed at in this block.
  std::vector<std::shared_ptr<BitCodeAbbrev>> CurAbbrevs;

  struct Block {
    unsigned PrevCodeSize;
    std::vector<std::shared_ptr<BitCodeAbbrev>> PrevAbbrevs;

    explicit Block(unsigned PCS) : PrevCodeSize(PCS) {}
  };

  /// This tracks the codesize of parent blocks.
  SmallVector<Block, 8> BlockScope;

  BitstreamBlockInfo *BlockInfo = nullptr;

public:
  /// Maximum VBR chunk size supported when reading abbreviated fields.
  static const size_t MaxChunkSize = 32;

  /// Construct an empty bitstream cursor.
  BitstreamCursor() = default;
  /// Construct a cursor over the given bitcode byte buffer.
  ///
  /// \param BitcodeBytes Bitcode buffer to read from.
  explicit BitstreamCursor(ArrayRef<uint8_t> BitcodeBytes)
      : SimpleBitstreamCursor(BitcodeBytes) {}
  /// Construct a cursor over the given bitcode string bytes.
  ///
  /// \param BitcodeBytes Bitcode bytes as a string reference.
  explicit BitstreamCursor(StringRef BitcodeBytes)
      : SimpleBitstreamCursor(BitcodeBytes) {}
  /// Construct a cursor over the bytes of \p BitcodeBytes.
  ///
  /// \param BitcodeBytes Memory buffer whose contents are the bitcode bytes.
  explicit BitstreamCursor(MemoryBufferRef BitcodeBytes)
      : SimpleBitstreamCursor(BitcodeBytes) {}

  /// Inherit AtEndOfStream from SimpleBitstreamCursor.
  using SimpleBitstreamCursor::AtEndOfStream;
  /// Inherit canSkipToPos from SimpleBitstreamCursor.
  using SimpleBitstreamCursor::canSkipToPos;
  /// Inherit fillCurWord from SimpleBitstreamCursor.
  using SimpleBitstreamCursor::fillCurWord;
  /// Inherit getBitcodeBytes from SimpleBitstreamCursor.
  using SimpleBitstreamCursor::getBitcodeBytes;
  /// Inherit GetCurrentBitNo from SimpleBitstreamCursor.
  using SimpleBitstreamCursor::GetCurrentBitNo;
  /// Inherit getCurrentByteNo from SimpleBitstreamCursor.
  using SimpleBitstreamCursor::getCurrentByteNo;
  /// Inherit getPointerToByte from SimpleBitstreamCursor.
  using SimpleBitstreamCursor::getPointerToByte;
  /// Inherit JumpToBit from SimpleBitstreamCursor.
  using SimpleBitstreamCursor::JumpToBit;
  /// Inherit Read from SimpleBitstreamCursor.
  using SimpleBitstreamCursor::Read;
  /// Inherit ReadVBR from SimpleBitstreamCursor.
  using SimpleBitstreamCursor::ReadVBR;
  /// Inherit ReadVBR64 from SimpleBitstreamCursor.
  using SimpleBitstreamCursor::ReadVBR64;
  /// Inherit SizeInBytes from SimpleBitstreamCursor.
  using SimpleBitstreamCursor::SizeInBytes;
  /// Skip to the end of the bitcode buffer.
  using SimpleBitstreamCursor::skipToEnd;

  /// Return the number of bits used to encode an abbrev #.
  ///
  /// \returns The current block's abbrev/code bit width.
  unsigned getAbbrevIDWidth() const { return CurCodeSize; }

  /// Flags that modify the behavior of advance().
  enum {
    /// If this flag is used, the advance() method does not automatically pop
    /// the block scope when the end of a block is reached.
    AF_DontPopBlockAtEnd = 1,

    /// If this flag is used, abbrev entries are returned just like normal
    /// records.
    AF_DontAutoprocessAbbrevs = 2
  };

  /// Advance the current bitstream, returning the next entry in the stream.
  ///
  /// \param Flags Bitmask of AF_* flags controlling advance behavior.
  ///
  /// \returns The next bitstream entry on success, or an Error on failure.
  Expected<BitstreamEntry> advance(unsigned Flags = 0) {
    while (true) {
      if (AtEndOfStream())
        return BitstreamEntry::getError();

      Expected<unsigned> MaybeCode = ReadCode();
      if (!MaybeCode)
        return MaybeCode.takeError();
      unsigned Code = MaybeCode.get();

      if (Code == bitc::END_BLOCK) {
        // Pop the end of the block unless Flags tells us not to.
        if (!(Flags & AF_DontPopBlockAtEnd) && ReadBlockEnd())
          return BitstreamEntry::getError();
        return BitstreamEntry::getEndBlock();
      }

      if (Code == bitc::ENTER_SUBBLOCK) {
        if (Expected<unsigned> MaybeSubBlock = ReadSubBlockID())
          return BitstreamEntry::getSubBlock(MaybeSubBlock.get());
        else
          return MaybeSubBlock.takeError();
      }

      if (Code == bitc::DEFINE_ABBREV &&
          !(Flags & AF_DontAutoprocessAbbrevs)) {
        // We read and accumulate abbrev's, the client can't do anything with
        // them anyway.
        if (Error Err = ReadAbbrevRecord())
          return std::move(Err);
        continue;
      }

      return BitstreamEntry::getRecord(Code);
    }
  }

  /// This is a convenience function for clients that don't expect any
  /// subblocks. This just skips over them automatically.
  ///
  /// \param Flags Bitmask of AF_* flags forwarded to advance().
  ///
  /// \returns The next non-SubBlock entry on success, or an Error on failure.
  Expected<BitstreamEntry> advanceSkippingSubblocks(unsigned Flags = 0) {
    while (true) {
      // If we found a normal entry, return it.
      Expected<BitstreamEntry> MaybeEntry = advance(Flags);
      if (!MaybeEntry)
        return MaybeEntry;
      BitstreamEntry Entry = MaybeEntry.get();

      if (Entry.Kind != BitstreamEntry::SubBlock)
        return Entry;

      // If we found a sub-block, just skip over it and check the next entry.
      if (Error Err = SkipBlock())
        return std::move(Err);
    }
  }

  /// Read the next abbrev/record code using the current block's code width.
  ///
  /// \returns The code value on success, or an Error on failure.
  Expected<unsigned> ReadCode() { return Read(CurCodeSize); }

  // Block header:
  //    [ENTER_SUBBLOCK, blockid, newcodelen, <align4bytes>, blocklen]

  /// Having read the ENTER_SUBBLOCK code, read the BlockID for the block.
  ///
  /// \returns The subblock ID on success, or an Error on failure.
  Expected<unsigned> ReadSubBlockID() { return ReadVBR(bitc::BlockIDWidth); }

  /// Having read the ENTER_SUBBLOCK abbrevid and a BlockID, skip over the body
  /// of this block.
  ///
  /// \returns Error::success() on success, or an Error on failure.
  Error SkipBlock() {
    // Read and ignore the codelen value.
    if (Expected<uint32_t> Res = ReadVBR(bitc::CodeLenWidth))
      ; // Since we are skipping this block, we don't care what code widths are
        // used inside of it.
    else
      return Res.takeError();

    SkipToFourByteBoundary();
    Expected<unsigned> MaybeNum = Read(bitc::BlockSizeWidth);
    if (!MaybeNum)
      return MaybeNum.takeError();
    size_t NumFourBytes = MaybeNum.get();

    // Check that the block wasn't partially defined, and that the offset isn't
    // bogus.
    size_t SkipTo = GetCurrentBitNo() + NumFourBytes * 4 * 8;
    if (AtEndOfStream())
      return createStringError(std::errc::illegal_byte_sequence,
                               "can't skip block: already at end of stream");
    if (!canSkipToPos(SkipTo / 8))
      return createStringError(std::errc::illegal_byte_sequence,
                               "can't skip to bit %zu from %" PRIu64, SkipTo,
                               GetCurrentBitNo());

    if (Error Res = JumpToBit(SkipTo))
      return Res;

    return Error::success();
  }

  /// Having read the ENTER_SUBBLOCK abbrevid, and enter the block.
  ///
  /// \param BlockID Block ID of the subblock to enter.
  /// \param NumWordsP If non-null, receives the block length in 32-bit words.
  ///
  /// \returns Error::success() on success, or an Error on failure.
  LLVM_ABI Error EnterSubBlock(unsigned BlockID, unsigned *NumWordsP = nullptr);

  /// Handle END_BLOCK: pop block scope and realign; return true on imbalance.
  ///
  /// \returns True if the block scope was empty (imbalanced END_BLOCK), false
  /// on success.
  bool ReadBlockEnd() {
    if (BlockScope.empty()) return true;

    // Block tail:
    //    [END_BLOCK, <align4bytes>]
    SkipToFourByteBoundary();

    popBlockScope();
    return false;
  }

private:
  void popBlockScope() {
    CurCodeSize = BlockScope.back().PrevCodeSize;

    CurAbbrevs = std::move(BlockScope.back().PrevAbbrevs);
    BlockScope.pop_back();
  }

  //===--------------------------------------------------------------------===//
  // Record Processing
  //===--------------------------------------------------------------------===//

public:
  /// Return the abbreviation for the specified AbbrevId.
  ///
  /// \param AbbrevID Abbreviation ID to look up in the current block.
  ///
  /// \returns The abbreviation on success, or an Error if \p AbbrevID is
  /// invalid.
  Expected<const BitCodeAbbrev *> getAbbrev(unsigned AbbrevID) {
    unsigned AbbrevNo = AbbrevID - bitc::FIRST_APPLICATION_ABBREV;
    if (AbbrevNo >= CurAbbrevs.size())
      return createStringError(
          std::errc::illegal_byte_sequence, "Invalid abbrev number");
    return CurAbbrevs[AbbrevNo].get();
  }

  /// Read the current record and discard it, returning the code for the record.
  ///
  /// \param AbbrevID Abbreviation ID of the record to skip.
  ///
  /// \returns The record code on success, or an Error on failure.
  LLVM_ABI Expected<unsigned> skipRecord(unsigned AbbrevID);

  /// Read the record for \p AbbrevID into \p Vals (and optional \p Blob).
  ///
  /// \param AbbrevID Abbreviation ID of the record to read.
  /// \param Vals Output vector that receives the decoded record operands.
  /// \param Blob If non-null, receives the blob payload when present.
  ///
  /// \returns The record code on success.
  LLVM_ABI Expected<unsigned> readRecord(unsigned AbbrevID,
                                         SmallVectorImpl<uint64_t> &Vals,
                                         StringRef *Blob = nullptr);

  //===--------------------------------------------------------------------===//
  // Abbrev Processing
  //===--------------------------------------------------------------------===//
  /// Read a DEFINE_ABBREV record and install the new abbreviation.
  ///
  /// \returns Error::success() on success, or an Error on failure.
  LLVM_ABI Error ReadAbbrevRecord();

  /// Read and return a block info block from the bitstream. If an error was
  /// encountered, return std::nullopt.
  ///
  /// \param ReadBlockInfoNames Whether to read block/record name information in
  /// the BlockInfo block. Only llvm-bcanalyzer uses this.
  ///
  /// \returns The parsed block info on success, std::nullopt if none was found,
  /// or an Error on failure.
  LLVM_ABI Expected<std::optional<BitstreamBlockInfo>>
  ReadBlockInfoBlock(bool ReadBlockInfoNames = false);

  /// Set the block info to be used by this BitstreamCursor to interpret
  /// abbreviated records.
  ///
  /// \param BI Block info describing abbreviations inherited by blocks.
  void setBlockInfo(BitstreamBlockInfo *BI) { BlockInfo = BI; }
};

} // end llvm namespace

#endif // LLVM_BITSTREAM_BITSTREAMREADER_H
