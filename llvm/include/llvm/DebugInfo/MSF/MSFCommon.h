//===- MSFCommon.h - Common types and functions for MSF files ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_MSF_MSFCOMMON_H
#define LLVM_DEBUGINFO_MSF_MSFCOMMON_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MathExtras.h"
#include <cstdint>
#include <vector>

namespace llvm {
namespace msf {

static const char Magic[] = {'M',  'i',  'c',    'r', 'o', 's',  'o',  'f',
                             't',  ' ',  'C',    '/', 'C', '+',  '+',  ' ',
                             'M',  'S',  'F',    ' ', '7', '.',  '0',  '0',
                             '\r', '\n', '\x1a', 'D', 'S', '\0', '\0', '\0'};

/// The superblock overlaid at the beginning of an MSF file (offset 0).
///
/// It starts with a magic header and is followed by information which
/// describes the layout of the file system.
struct SuperBlock {
  /// Magic header identifying the file as an MSF.
  char MagicBytes[sizeof(Magic)];
  /// Size in bytes of each fixed-size block in the file system.
  ///
  /// The file system is split into a variable number of fixed size elements.
  /// These elements are referred to as blocks.  The size of a block may vary
  /// from system to system.
  support::ulittle32_t BlockSize;
  /// Index of the free block map (1 or 2).
  support::ulittle32_t FreeBlockMapBlock;
  /// Number of blocks resident in the file system.
  ///
  /// In practice, NumBlocks * BlockSize is equivalent to the size of the MSF
  /// file.
  support::ulittle32_t NumBlocks;
  /// Number of bytes which make up the directory.
  support::ulittle32_t NumDirectoryBytes;
  /// Field whose purpose is not yet known.
  support::ulittle32_t Unknown1;
  /// Block number of the block map.
  support::ulittle32_t BlockMapAddr;
};

/// Describes the overall layout of an MSF file.
struct MSFLayout {
  /// Constructs an empty MSF layout.
  MSFLayout() = default;

  /// Returns the block index of the main free page map.
  ///
  /// \returns The block index of the main free page map (1 or 2).
  uint32_t mainFpmBlock() const {
    assert(SB->FreeBlockMapBlock == 1 || SB->FreeBlockMapBlock == 2);
    return SB->FreeBlockMapBlock;
  }

  /// Returns the block index of the alternate free page map.
  ///
  /// \returns The block index of the alternate free page map (1 or 2).
  uint32_t alternateFpmBlock() const {
    // If mainFpmBlock is 1, this is 2.  If mainFpmBlock is 2, this is 1.
    return 3U - mainFpmBlock();
  }

  /// Pointer to the file's superblock.
  const SuperBlock *SB = nullptr;
  /// Bit vector describing which blocks are free.
  BitVector FreePageMap;
  /// Block indices that store the MSF directory.
  ArrayRef<support::ulittle32_t> DirectoryBlocks;
  /// Byte sizes of each stream in the MSF.
  ArrayRef<support::ulittle32_t> StreamSizes;
  /// For each stream, the list of blocks that comprise that stream.
  std::vector<ArrayRef<support::ulittle32_t>> StreamMap;
};

/// Describes the layout of a stream in an MSF layout.
///
/// A "stream" here is defined as any logical unit of data which may be
/// arranged inside the MSF file as a sequence of (possibly discontiguous)
/// blocks.  When we want to read from a particular MSF Stream, we fill out a
/// stream layout structure and the reader uses it to determine which blocks in
/// the underlying MSF file contain the data, so that it can be pieced together
/// in the right order.
class MSFStreamLayout {
public:
  /// Length in bytes of the stream.
  uint32_t Length;
  /// Block indices that make up the stream, in order.
  std::vector<support::ulittle32_t> Blocks;
};

/// Determine the layout of the FPM stream, given the MSF layout.
///
/// An FPM stream spans 1 or more blocks, each at equally spaced intervals
/// throughout the file.
///
/// \param Msf The MSF layout describing the file.
/// \param IncludeUnusedFpmData If true, include FPM blocks that match the
///        form but are unused for describing allocation status.
/// \param AltFpm If true, use the alternate FPM; otherwise use the main FPM.
/// \returns The stream layout of the selected FPM.
LLVM_ABI MSFStreamLayout getFpmStreamLayout(const MSFLayout &Msf,
                                            bool IncludeUnusedFpmData = false,
                                            bool AltFpm = false);

/// Returns whether \p Size is a valid MSF block size.
///
/// \param Size The candidate block size in bytes.
/// \returns True if \p Size is a supported MSF block size, false otherwise.
inline bool isValidBlockSize(uint32_t Size) {
  switch (Size) {
  case 512:
  case 1024:
  case 2048:
  case 4096:
  case 8192:
  case 16384:
  case 32768:
    return true;
  }
  return false;
}

/// Returns the maximum possible file size for the given block size.
///
/// Block Size  |  Max File Size
/// <= 4096     |      4GB
///    8192     |      8GB
///   16384     |      16GB
///   32768     |      32GB
///
/// \param Size The block size of the MSF.
/// \returns The maximum file size in bytes for the given block size.
inline uint64_t getMaxFileSizeFromBlockSize(uint32_t Size) {
  switch (Size) {
  case 8192:
    return (uint64_t)UINT32_MAX * 2ULL;
  case 16384:
    return (uint64_t)UINT32_MAX * 3ULL;
  case 32768:
    return (uint64_t)UINT32_MAX * 4ULL;
  default:
    return (uint64_t)UINT32_MAX;
  }
}

/// Returns the minimum number of blocks in a valid MSF file.
///
/// Accounts for the Super Block, Fpm0, Fpm1, and Block Map.
///
/// \returns The minimum block count (4).
inline uint32_t getMinimumBlockCount() { return 4; }

/// Returns the index of the first block that is not reserved.
///
/// Super Block, Fpm0, and Fpm1 are reserved.  The Block Map, although
/// required, need not be at block 3.
///
/// \returns The index of the first unreserved block (3).
inline uint32_t getFirstUnreservedBlock() { return 3; }

/// Converts a byte count to the number of blocks needed to hold that many
/// bytes.
///
/// \param NumBytes The number of bytes.
/// \param BlockSize The size of each block in bytes.
/// \returns The number of blocks needed to hold \p NumBytes bytes.
inline uint64_t bytesToBlocks(uint64_t NumBytes, uint64_t BlockSize) {
  return divideCeil(NumBytes, BlockSize);
}

/// Converts a block number to a byte offset within the MSF file.
///
/// \param BlockNumber The zero-based block index.
/// \param BlockSize The size of each block in bytes.
/// \returns The byte offset of the start of the given block.
inline uint64_t blockToOffset(uint64_t BlockNumber, uint64_t BlockSize) {
  return BlockNumber * BlockSize;
}

/// Returns the spacing between consecutive FPM blocks for the given layout.
///
/// \param L The MSF layout.
/// \returns The interval length in blocks between consecutive FPM blocks.
inline uint32_t getFpmIntervalLength(const MSFLayout &L) {
  return L.SB->BlockSize;
}

/// Determines how many pieces the specified FPM is split into.
///
/// \param BlockSize The block size of the MSF.
/// \param NumBlocks The total number of blocks in the MSF.
/// \param IncludeUnusedFpmData When true, this will count every block that is
///        both in the file and matches the form of an FPM block, even if some
///        of those FPM blocks are unused (a single FPM block can describe the
///        allocation status of up to 32,767 blocks, although one appears only
///        every 4,096 blocks).  So there are 8x as many blocks that match the
///        form as there are blocks that are necessary to describe the
///        allocation status of the file.  When this parameter is false, these
///        extraneous trailing blocks are not counted.
/// \param FpmNumber The FPM to measure; must be 1 or 2.
/// \returns The number of FPM intervals for the given parameters.
inline uint32_t getNumFpmIntervals(uint32_t BlockSize, uint32_t NumBlocks,
                                   bool IncludeUnusedFpmData, int FpmNumber) {
  assert(FpmNumber == 1 || FpmNumber == 2);
  if (IncludeUnusedFpmData) {
    // This calculation determines how many times a number of the form
    // BlockSize * k + N appears in the range [0, NumBlocks).  We only need to
    // do this when unused data is included, since the number of blocks dwarfs
    // the number of fpm blocks.
    return divideCeil(NumBlocks - FpmNumber, BlockSize);
  }

  // We want the minimum number of intervals required, where each interval can
  // represent BlockSize * 8 blocks.
  return divideCeil(NumBlocks, 8 * BlockSize);
}

/// Determines how many pieces the FPM is split into for the given layout.
///
/// \param L The MSF layout.
/// \param IncludeUnusedFpmData When true, count unused FPM blocks that match
///        the form as well as those needed for allocation status.
/// \param AltFpm If true, use the alternate FPM; otherwise use the main FPM.
/// \returns The number of FPM intervals for the given layout.
inline uint32_t getNumFpmIntervals(const MSFLayout &L,
                                   bool IncludeUnusedFpmData = false,
                                   bool AltFpm = false) {
  return getNumFpmIntervals(L.SB->BlockSize, L.SB->NumBlocks,
                            IncludeUnusedFpmData,
                            AltFpm ? L.alternateFpmBlock() : L.mainFpmBlock());
}

/// Validates that a superblock describes a well-formed MSF file.
///
/// \param SB The superblock to validate.
/// \returns Success if the superblock is valid, or an error describing the
///          problem.
LLVM_ABI Error validateSuperBlock(const SuperBlock &SB);

} // end namespace msf
} // end namespace llvm

#endif // LLVM_DEBUGINFO_MSF_MSFCOMMON_H
