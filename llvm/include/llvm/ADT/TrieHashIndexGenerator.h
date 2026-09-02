//===- TrieHashIndexGenerator.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_TRIEHASHINDEXGENERATOR_H
#define LLVM_ADT_TRIEHASHINDEXGENERATOR_H

#include "llvm/ADT/ArrayRef.h"
#include <optional>

namespace llvm {

/// The utility class that helps computing the index of the object inside trie
/// from its hash. The generator can be configured with the number of bits
/// used for each level of trie structure with \c NumRootsBits and \c
/// NumSubtrieBits.
/// For example, try computing indexes for a 16-bit hash 0x1234 with 8-bit root
/// and 4-bit sub-trie:
///
///   IndexGenerator IndexGen{8, 4, Hash};
///   size_t index1 = IndexGen.next(); // index 18 in root node.
///   size_t index2 = IndexGen.next(); // index 3 in sub-trie level 1.
///   size_t index3 = IndexGen.next(); // index 4 in sub-tire level 2.
///
/// This is used by different trie implementation to figure out where to
/// insert/find the object in the data structure.
struct TrieHashIndexGenerator {
  /// Number of hash bits consumed for the root trie node index.
  size_t NumRootBits;
  /// Number of hash bits consumed for each subsequent subtrie level.
  size_t NumSubtrieBits;
  /// Hash bytes from which indexes are derived.
  ArrayRef<uint8_t> Bytes;
  /// Next bit offset into \ref Bytes; unset before the first \ref next call.
  std::optional<size_t> StartBit = std::nullopt;

  /// Return how many bits will be used for the current index.
  size_t getNumBits() const {
    assert(StartBit);
    size_t TotalNumBits = Bytes.size() * 8;
    assert(*StartBit <= TotalNumBits);
    return std::min(*StartBit ? NumSubtrieBits : NumRootBits,
                    TotalNumBits - *StartBit);
  }

  /// Advance to the next trie level and return its child index.
  size_t next() {
    if (!StartBit) {
      // Compute index for root when StartBit is not set.
      StartBit = 0;
      return getIndex(Bytes, *StartBit, NumRootBits);
    }
    if (*StartBit < Bytes.size() * 8) {
      // Compute index for sub-trie.
      *StartBit += *StartBit ? NumSubtrieBits : NumRootBits;
      assert((*StartBit - NumRootBits) % NumSubtrieBits == 0);
      return getIndex(Bytes, *StartBit, NumSubtrieBits);
    }
    // All the bits are consumed.
    return end();
  }

  /// Jump to a known level and return the provided index as a hint.
  ///
  /// For example, if the object is known to have \c Index on a level that
  /// already consumes the first \p Bit bits of the hash, start generation from
  /// that level.
  size_t hint(unsigned Index, unsigned Bit) {
    assert(Bit < Bytes.size() * 8);
    assert(Bit == 0 || (Bit - NumRootBits) % NumSubtrieBits == 0);
    StartBit = Bit;
    return Index;
  }

  /// Index at the current level for a hash that collides on leading bits.
  size_t getCollidingBits(ArrayRef<uint8_t> CollidingBits) const {
    assert(StartBit);
    return getIndex(CollidingBits, *StartBit, NumSubtrieBits);
  }

  /// Sentinel index returned when all hash bits have been consumed.
  size_t end() const { return SIZE_MAX; }

  /// Compute a trie child index from hash bytes at a bit offset.
  static size_t getIndex(ArrayRef<uint8_t> Bytes, size_t StartBit,
                         size_t NumBits) {
    assert(StartBit < Bytes.size() * 8);
    // Drop all the bits before StartBit.
    Bytes = Bytes.drop_front(StartBit / 8u);
    StartBit %= 8u;
    size_t Index = 0;
    // Compute the index using the bits in range [StartBit, StartBit + NumBits),
    // note the range can spread across few `uint8_t` in the array.
    for (uint8_t Byte : Bytes) {
      size_t ByteStart = 0, ByteEnd = 8;
      if (StartBit) {
        ByteStart = StartBit;
        Byte &= (1u << (8 - StartBit)) - 1u;
        StartBit = 0;
      }
      size_t CurrentNumBits = ByteEnd - ByteStart;
      if (CurrentNumBits > NumBits) {
        Byte >>= CurrentNumBits - NumBits;
        CurrentNumBits = NumBits;
      }
      Index <<= CurrentNumBits;
      Index |= Byte & ((1u << CurrentNumBits) - 1u);

      assert(NumBits >= CurrentNumBits);
      NumBits -= CurrentNumBits;
      if (!NumBits)
        break;
    }
    return Index;
  }
};

} // namespace llvm

#endif // LLVM_ADT_TRIEHASHINDEXGENERATOR_H
