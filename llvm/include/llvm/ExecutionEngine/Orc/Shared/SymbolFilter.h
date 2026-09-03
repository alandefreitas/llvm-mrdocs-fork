//===- SymbolFilter.h - Utilities for Symbol Filtering ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_SHARED_SYMBOLFILTER_H
#define LLVM_EXECUTIONENGINE_ORC_SHARED_SYMBOLFILTER_H

#include "llvm/ExecutionEngine/Orc/Shared/SimplePackedSerialization.h"

#include <cmath>
#include <vector>

namespace llvm {
namespace orc {

namespace shared {
/// SPS tag type for BloomFilter as an initialized flag, counts, shift, and
/// table.
using SPSBloomFilter =
    SPSTuple<bool, uint32_t, uint32_t, uint32_t, SPSSequence<uint64_t>>;
}

/// Probabilistic set of symbol names for cheap membership tests.
class BloomFilter {
public:
  /// Hash function type mapping a symbol name to a 32-bit hash.
  using HashFunc = std::function<uint32_t(StringRef)>;

  /// Construct an uninitialized bloom filter.
  BloomFilter() = default;

  /// Move-construct a bloom filter.
  /// @param Other Filter to move from.
  BloomFilter(BloomFilter &&Other) noexcept = default;

  /// Move-assign a bloom filter.
  /// @param Other Filter to move from.
  /// @return Reference to this filter.
  BloomFilter &operator=(BloomFilter &&Other) noexcept = default;

  /// Copy construction is deleted.
  /// @param Other Filter that would be copied.
  BloomFilter(const BloomFilter &Other) = delete;

  /// Copy assignment is deleted.
  /// @param Other Filter that would be copied.
  BloomFilter &operator=(const BloomFilter &Other) = delete;

  /// Construct and initialize a filter sized for \p SymbolCount symbols.
  /// @param SymbolCount Expected number of symbols to store.
  /// @param FalsePositiveRate Target false-positive rate in (0, 1).
  /// @param hashFn Hash function used for symbol names.
  BloomFilter(uint32_t SymbolCount, float FalsePositiveRate, HashFunc hashFn)
      : HashFn(std::move(hashFn)) {
    initialize(SymbolCount, FalsePositiveRate);
  }

  /// Return true if this filter has been initialized.
  /// @return True if the filter has been initialized.
  bool isInitialized() const { return Initialized; }

  /// Insert \p Sym into the filter.
  /// @param Sym Symbol name to add.
  void add(StringRef Sym) {
    assert(Initialized);
    addHash(HashFn(Sym));
  }

  /// Return true if \p Sym may be present in the filter.
  /// @param Sym Symbol name to test.
  /// @return True if \p Sym may be present; false if it is definitely absent.
  bool mayContain(StringRef Sym) const {
    return !isEmpty() && testHash(HashFn(Sym));
  }

  /// Return true if no symbols have been recorded in the filter.
  /// @return True if the filter contains no symbols.
  bool isEmpty() const { return SymbolCount == 0; }

private:
  friend class shared::SPSSerializationTraits<shared::SPSBloomFilter,
                                              BloomFilter>;
  static constexpr uint32_t BitsPerEntry = 64;

  bool Initialized = false;
  uint32_t SymbolCount = 0;
  uint32_t BloomSize = 0;
  uint32_t BloomShift = 0;
  std::vector<uint64_t> BloomTable;
  HashFunc HashFn;

  void initialize(uint32_t SymCount, float FalsePositiveRate) {
    assert(SymCount > 0);
    SymbolCount = SymCount;
    Initialized = true;

    float ln2 = std::log(2.0f);
    float M = -1.0f * SymbolCount * std::log(FalsePositiveRate) / (ln2 * ln2);
    BloomSize = static_cast<uint32_t>(std::ceil(M / BitsPerEntry));
    BloomShift = std::min(6u, log2ceil(SymbolCount));
    BloomTable.resize(BloomSize, 0);
  }

  void addHash(uint32_t Hash) {
    uint32_t Hash2 = Hash >> BloomShift;
    uint32_t N = (Hash / BitsPerEntry) % BloomSize;
    uint64_t Mask =
        (1ULL << (Hash % BitsPerEntry)) | (1ULL << (Hash2 % BitsPerEntry));
    BloomTable[N] |= Mask;
  }

  bool testHash(uint32_t Hash) const {
    uint32_t Hash2 = Hash >> BloomShift;
    uint32_t N = (Hash / BitsPerEntry) % BloomSize;
    uint64_t Mask =
        (1ULL << (Hash % BitsPerEntry)) | (1ULL << (Hash2 % BitsPerEntry));
    return (BloomTable[N] & Mask) == Mask;
  }

  static constexpr uint32_t log2ceil(uint32_t V) {
    return V <= 1 ? 0 : 32 - countl_zero(V - 1);
  }
};

/// Builder for BloomFilter instances from a list of symbol names.
class BloomFilterBuilder {
public:
  /// Hash function type used when constructing the filter.
  using HashFunc = BloomFilter::HashFunc;

  /// Construct a builder with default false-positive rate and hash function.
  BloomFilterBuilder() = default;

  /// Set the target false-positive rate for the built filter.
  /// @param Rate Desired false-positive rate in (0, 1).
  /// @return Reference to this builder.
  BloomFilterBuilder &setFalsePositiveRate(float Rate) {
    assert(Rate > 0.0f && Rate < 1.0f);
    FalsePositiveRate = Rate;
    return *this;
  }

  /// Set the hash function used when adding and querying symbols.
  /// @param Fn Hash function to use.
  /// @return Reference to this builder.
  BloomFilterBuilder &setHashFunction(HashFunc Fn) {
    HashFn = std::move(Fn);
    return *this;
  }

  /// Build a bloom filter containing all of \p Symbols.
  /// @param Symbols Non-empty list of symbol names to insert.
  /// @return A bloom filter containing every symbol in \p Symbols.
  BloomFilter build(ArrayRef<StringRef> Symbols) const {
    assert(!Symbols.empty() && "Cannot build filter from empty symbol list.");
    BloomFilter F(static_cast<uint32_t>(Symbols.size()), FalsePositiveRate,
                  HashFn);
    for (const auto &Sym : Symbols)
      F.add(Sym);

    return F;
  }

private:
  float FalsePositiveRate = 0.02f;
  HashFunc HashFn = [](StringRef S) -> uint32_t {
    uint32_t H = 5381;
    for (char C : S)
      H = ((H << 5) + H) + static_cast<uint8_t>(C); // H * 33 + C
    return H;
  };
};

namespace shared {

/// SPS serialization traits for BloomFilter.
template <> class SPSSerializationTraits<SPSBloomFilter, BloomFilter> {
public:
  /// Return the serialized size of \p Filter.
  /// @param Filter Filter to measure.
  /// @return Number of bytes needed to serialize \p Filter.
  static size_t size(const BloomFilter &Filter) {
    return SPSBloomFilter::AsArgList::size(
        Filter.Initialized, Filter.SymbolCount, Filter.BloomSize,
        Filter.BloomShift, Filter.BloomTable);
  }

  /// Serialize \p Filter into \p OB.
  /// @param OB Output buffer.
  /// @param Filter Filter to serialize.
  /// @return True on success, false if serialization fails.
  static bool serialize(SPSOutputBuffer &OB, const BloomFilter &Filter) {
    return SPSBloomFilter::AsArgList::serialize(
        OB, Filter.Initialized, Filter.SymbolCount, Filter.BloomSize,
        Filter.BloomShift, Filter.BloomTable);
  }

  /// Deserialize a BloomFilter from \p IB into \p Filter.
  /// @param IB Input buffer.
  /// @param Filter Destination filter.
  /// @return True on success, false if deserialization fails.
  static bool deserialize(SPSInputBuffer &IB, BloomFilter &Filter) {
    bool IsInitialized;
    uint32_t SymbolCount = 0, BloomSize = 0, BloomShift = 0;
    std::vector<uint64_t> BloomTable;

    if (!SPSBloomFilter::AsArgList::deserialize(
            IB, IsInitialized, SymbolCount, BloomSize, BloomShift, BloomTable))
      return false;

    Filter.Initialized = IsInitialized;
    Filter.SymbolCount = SymbolCount;
    Filter.BloomSize = BloomSize;
    Filter.BloomShift = BloomShift;
    Filter.BloomTable = std::move(BloomTable);

    return true;
  }
};

} // end namespace shared
} // end namespace orc
} // end namespace llvm
#endif // LLVM_EXECUTIONENGINE_ORC_SHARED_SYMBOLFILTER_H
