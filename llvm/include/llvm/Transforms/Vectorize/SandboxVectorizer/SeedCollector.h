//===- SeedCollector.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This file contains the mechanism for collecting the seed instructions that
// are used as starting points for forming the vectorization graph.
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_SEEDCOLLECTOR_H
#define LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_SEEDCOLLECTOR_H

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/SandboxIR/Instruction.h"
#include "llvm/SandboxIR/Utils.h"
#include "llvm/SandboxIR/Value.h"
#include "llvm/Support/Compiler.h"
#include <iterator>
#include <memory>

namespace llvm::sandboxir {

/// A set of candidate Instructions for vectorizing together.
class SeedBundle {
public:
  /// Initialize a bundle with \p I.
  ///
  /// \param I First seed instruction to place in the bundle.
  explicit SeedBundle(Instruction *I) { insertAt(begin(), I); }
  /// Initialize a bundle from a list of seed instructions.
  ///
  /// \param L Seed instructions moved into this bundle.
  explicit SeedBundle(SmallVector<Instruction *> &&L) : Seeds(std::move(L)) {
    for (auto &S : Seeds)
      NumUnusedBits += Utils::getNumBits(S);
  }
  /// No need to allow copies.
  ///
  /// \param Other Bundle that would be copied.
  SeedBundle(const SeedBundle &Other) = delete;
  /// Deleted copy assignment.
  ///
  /// \param Other Bundle that would be assigned from.
  SeedBundle &operator=(const SeedBundle &Other) = delete;
  /// Destroy this seed bundle.
  virtual ~SeedBundle() = default;

  /// Iterator over the seed instructions.
  using iterator = SmallVector<Instruction *>::iterator;
  /// Const iterator over the seed instructions.
  using const_iterator = SmallVector<Instruction *>::const_iterator;
  /// Return an iterator to the first seed.
  /// \return An iterator to the first seed.
  iterator begin() { return Seeds.begin(); }
  /// Return an iterator past the last seed.
  /// \return An iterator past the last seed.
  iterator end() { return Seeds.end(); }
  /// Return a const iterator to the first seed.
  /// \return A const iterator to the first seed.
  const_iterator begin() const { return Seeds.begin(); }
  /// Return a const iterator past the last seed.
  /// \return A const iterator past the last seed.
  const_iterator end() const { return Seeds.end(); }

  /// Return the seed instruction at index \p Idx.
  ///
  /// \param Idx Index of the seed to access.
  /// \return The seed instruction at \p Idx.
  Instruction *operator[](unsigned Idx) const { return Seeds[Idx]; }

  /// Insert \p I into position \p Pos.
  ///
  /// Clients should choose Pos by symbol, symbol-offset, and program order
  /// (which depends if scheduling bottom-up or top-down).
  ///
  /// \param Pos Insertion position within the seed list.
  /// \param I Instruction to insert.
  void insertAt(iterator Pos, Instruction *I) {
    Seeds.insert(Pos, I);
    NumUnusedBits += Utils::getNumBits(I);
  }

  /// Try to insert \p I into this bundle if it is compatible.
  ///
  /// \param I Candidate seed instruction.
  /// \param SE Scalar evolution analysis used for ordering checks.
  /// \return True if \p I was inserted.
  virtual bool tryInsert(Instruction *I, ScalarEvolution &SE) = 0;

  /// Return the index of the first unused seed element.
  /// \return The index of the first unused seed, or \c Seeds.size() if none.
  unsigned getFirstUnusedElementIdx() const {
    for (unsigned ElmIdx : seq<unsigned>(0, Seeds.size()))
      if (!isUsed(ElmIdx))
        return ElmIdx;
    return Seeds.size();
  }
  /// Mark instruction \p I as used within the bundle.
  ///
  /// Clients use this property when assembling a vectorized instruction from
  /// the seeds in a bundle. This allows constant time evaluation and "removal"
  /// from the list.
  ///
  /// \param I Seed instruction to mark as used.
  void setUsed(Instruction *I) {
    auto It = llvm::find(*this, I);
    assert(It != end() && "Instruction not in the bundle!");
    auto Idx = It - begin();
    setUsed(Idx, 1, /*VerifyUnused=*/false);
  }

  /// Mark \p Sz consecutive elements starting at \p ElementIdx as used.
  ///
  /// \param ElementIdx First element index to mark used.
  /// \param Sz Number of consecutive elements to mark used.
  /// \param VerifyUnused If true, assert that the elements were unused.
  void setUsed(unsigned ElementIdx, unsigned Sz = 1, bool VerifyUnused = true) {
    if (ElementIdx + Sz >= UsedLanes.size())
      UsedLanes.resize(ElementIdx + Sz);
    for (unsigned Idx : seq<unsigned>(ElementIdx, ElementIdx + Sz)) {
      assert((!VerifyUnused || !UsedLanes.test(Idx)) &&
             "Already marked as used!");
      UsedLanes.set(Idx);
      UsedLaneCount++;
    }
    NumUnusedBits -= Utils::getNumBits(Seeds[ElementIdx]);
  }
  /// Return true if seed element \p Element has been used.
  ///
  /// \param Element Index of the seed element to query.
  /// \return True if seed element \p Element has been used.
  bool isUsed(unsigned Element) const {
    return Element < UsedLanes.size() && UsedLanes.test(Element);
  }
  /// Return true if every seed element has been marked used.
  /// \return True if every seed element has been marked used.
  bool allUsed() const { return UsedLaneCount == Seeds.size(); }
  /// Return the total number of unused seed bits remaining.
  /// \return The total number of unused seed bits remaining.
  unsigned getNumUnusedBits() const { return NumUnusedBits; }

  /// Return a slice of seed elements starting at \p StartIdx.
  ///
  /// The slice has a total size <= \p MaxVecRegBits, or is empty if the
  /// requirements cannot be met. If \p ForcePowOf2 is true, then the returned
  /// slice will have a total number of bits that is a power of 2.
  ///
  /// \param StartIdx First unused-element index included in the slice.
  /// \param MaxVecRegBits Maximum total bit width of the returned slice.
  /// \param ForcePowOf2 If true, require a power-of-two total bit width.
  /// \return A slice of seed instructions, or empty if requirements are unmet.
  LLVM_ABI ArrayRef<Instruction *>
  getSlice(unsigned StartIdx, unsigned MaxVecRegBits, bool ForcePowOf2);

  /// Return the number of seed elements in the bundle.
  /// \return The number of seed elements in the bundle.
  std::size_t size() const { return Seeds.size(); }

protected:
  /// Seed instructions collected in this bundle.
  SmallVector<Instruction *> Seeds;
  /// The lanes that we have already vectorized.
  BitVector UsedLanes;
  /// Tracks used lanes for constant-time accessor.
  unsigned UsedLaneCount = 0;
  /// Tracks the remaining bits available to vectorize
  unsigned NumUnusedBits = 0;

public:
#ifndef NDEBUG
  /// Print the seeds in this bundle to \p OS.
  ///
  /// \param OS Destination stream.
  void dump(raw_ostream &OS) const {
    for (auto [ElmIdx, I] : enumerate(*this)) {
      OS.indent(2) << ElmIdx << ". ";
      if (isUsed(ElmIdx))
        OS << "[USED]";
      else
        OS << *I;
      OS << "\n";
    }
  }
  /// Dump the seeds in this bundle to the debug stream.
  LLVM_DUMP_METHOD void dump() const {
    dump(dbgs());
    dbgs() << "\n";
  }
#endif // NDEBUG
};

/// Specialization of SeedBundle for memory-access instructions.
///
/// Keeps seeds sorted in ascending memory order, which is convenient for
/// slicing these bundles into vectorizable groups.
template <typename LoadOrStoreT> class MemSeedBundle : public SeedBundle {
public:
  /// Construct a memory seed bundle from \p SV, sorted by address using \p SE.
  ///
  /// \param SV Seed load or store instructions moved into this bundle.
  /// \param SE Scalar evolution analysis used to order memory accesses.
  explicit MemSeedBundle(SmallVector<Instruction *> &&SV, ScalarEvolution &SE)
      : SeedBundle(std::move(SV)) {
    static_assert(std::is_same<LoadOrStoreT, LoadInst>::value ||
                      std::is_same<LoadOrStoreT, StoreInst>::value,
                  "Expected LoadInst or StoreInst!");
    assert(all_of(Seeds, [](auto *S) { return isa<LoadOrStoreT>(S); }) &&
           "Expected Load or Store instructions!");
    auto Cmp = [&SE](Instruction *I0, Instruction *I1) {
      return *Utils::atLowerAddress(cast<LoadOrStoreT>(I0),
                                    cast<LoadOrStoreT>(I1), SE);
    };
    std::sort(Seeds.begin(), Seeds.end(), Cmp);
  }
  /// Construct a memory seed bundle containing only \p MemI.
  ///
  /// \param MemI Initial load or store instruction.
  explicit MemSeedBundle(LoadOrStoreT *MemI) : SeedBundle(MemI) {
    static_assert(std::is_same<LoadOrStoreT, LoadInst>::value ||
                      std::is_same<LoadOrStoreT, StoreInst>::value,
                  "Expected LoadInst or StoreInst!");
    assert(isa<LoadOrStoreT>(MemI) && "Expected Load or Store!");
  }
  /// Try to insert memory instruction \p I in address order.
  ///
  /// \param I Candidate load or store instruction.
  /// \param SE Scalar evolution analysis used for pointer diffs.
  /// \return True if \p I was inserted.
  bool tryInsert(sandboxir::Instruction *I, ScalarEvolution &SE) override {
    assert(isa<LoadOrStoreT>(I) && "Expected a Store or a Load!");
    // Early return if we can't determine the mem access ordering.
    auto DiffOpt = Utils::getPointerDiffInBytes(
        cast<LoadOrStoreT>(Seeds.back()), cast<LoadOrStoreT>(I), SE);
    if (!DiffOpt)
      return false;

    auto Cmp = [&SE](Instruction *I0, Instruction *I1) {
      return *Utils::atLowerAddress(cast<LoadOrStoreT>(I0),
                                    cast<LoadOrStoreT>(I1), SE);
    };
    // Find the first element after I in mem. Then insert I before it.
    insertAt(llvm::upper_bound(*this, I, Cmp), I);
    return true;
  }
};

/// Seed bundle specialized for store instructions.
using StoreSeedBundle = MemSeedBundle<sandboxir::StoreInst>;
/// Seed bundle specialized for load instructions.
using LoadSeedBundle = MemSeedBundle<sandboxir::LoadInst>;

/// Tracks seeds within SeedBundles keyed by pointer, type, and opcode.
///
/// Saves newly collected seeds in the proper bundle. Supports constant-time
/// removal, as seeds and entire bundles are vectorized and marked used to
/// signify removal. Iterators skip bundles that are completely used.
class SeedContainer {
  // Use the same key for different seeds if they are the same type and
  // reference the same pointer, even if at different offsets. This directs
  // potentially vectorizable seeds into the same bundle.
  using KeyT = std::tuple<Value *, Type *, Instruction::Opcode>;
  // Trying to vectorize too many seeds at once is expensive in
  // compilation-time. Use a vector of bundles (all with the same key) to
  // partition the candidate set into more manageable units. Each bundle is
  // size-limited by sbvec-seed-bundle-size-limit.  TODO: There might be a
  // better way to divide these than by simple insertion order.
  using ValT = SmallVector<std::unique_ptr<SeedBundle>>;
  using BundleMapT = MapVector<KeyT, ValT>;
  // Map from {pointer, Type, Opcode} to a vector of bundles.
  BundleMapT Bundles;
  // Allows finding a particular Instruction's bundle.
  DenseMap<Instruction *, SeedBundle *> SeedLookupMap;

  ScalarEvolution &SE;

  template <typename LoadOrStoreT>
  KeyT getKey(LoadOrStoreT *LSI, bool AllowDiffTypes) const;

public:
  /// Construct a seed container using scalar evolution analysis \p SE.
  ///
  /// \param SE Scalar evolution analysis used when inserting seeds.
  SeedContainer(ScalarEvolution &SE) : SE(SE) {}

  /// Input iterator over unused SeedBundles in this container.
  class iterator {
    BundleMapT *Map = nullptr;
    BundleMapT::iterator MapIt;
    ValT *Vec = nullptr;
    size_t VecIdx;

  public:
    /// Difference type for iterator arithmetic.
    using difference_type = std::ptrdiff_t;
    /// Value type yielded by this iterator.
    using value_type = SeedBundle;
    /// Pointer type yielded by this iterator.
    using pointer = value_type *;
    /// Reference type yielded by this iterator.
    using reference = value_type &;
    /// Iterator category tag.
    using iterator_category = std::input_iterator_tag;

    // TODO: Range_size counts fully used-bundles. Further, iterating over
    // anything other than the Bundles in a SeedContainer includes used
    // seeds. Rework the iterator logic to clean this up.
    /// Construct an iterator into \p Map at \p MapIt, \p Vec, and \p VecIdx.
    ///
    /// Iterates over the Map of SeedBundle Vectors, starting at MapIt, and Vec
    /// at VecIdx, skipping vectors that are completely used. Iteration order
    /// over the keys {Pointer, Type, Opcode} follows DenseMap iteration order.
    /// For a given key, the vectors of SeedBundles will be returned in
    /// insertion order. As in the pseudo code below:
    ///
    /// for Key,Value in Bundles
    ///   for SeedBundleVector in Value
    ///     for SeedBundle in SeedBundleVector
    ///        if !SeedBundle.allUsed() ...
    ///
    /// Note that the bundles themselves may have additional ordering, created
    /// by the subclasses by insertAt. The bundles themselves may also have used
    /// instructions.
    ///
    /// \param Map Bundle map being iterated.
    /// \param MapIt Current position in \p Map.
    /// \param Vec Current vector of bundles for the selected key, or nullptr.
    /// \param VecIdx Index within \p Vec.
    iterator(BundleMapT &Map, BundleMapT::iterator MapIt, ValT *Vec, int VecIdx)
        : Map(&Map), MapIt(MapIt), Vec(Vec), VecIdx(VecIdx) {}
    /// Return a reference to the current unused SeedBundle.
    /// \return A reference to the current unused SeedBundle.
    value_type &operator*() {
      assert(Vec != nullptr && "Already at end!");
      return *(*Vec)[VecIdx];
    }
    /// Skip completely used bundles by repeatedly calling operator++().
    void skipUsed() {
      while (Vec && VecIdx < Vec->size() && this->operator*().allUsed())
        ++(*this);
    }
    /// Advance to the next unused SeedBundle.
    /// \return A reference to this iterator after advancement.
    iterator &operator++() {
      ++VecIdx;
      if (VecIdx >= Vec->size()) {
        assert(MapIt != Map->end() && "Already at end!");
        VecIdx = 0;
        ++MapIt;
        if (MapIt != Map->end())
          Vec = &MapIt->second;
        else {
          Vec = nullptr;
        }
      }
      skipUsed();
      return *this;
    }
    /// Advance to the next unused SeedBundle, returning the previous position.
    ///
    /// \param Unused Unused postfix-discriminator parameter.
    /// \return A copy of the iterator as it was before advancement.
    iterator operator++(int Unused) {
      (void)Unused;
      auto Copy = *this;
      ++(*this);
      return Copy;
    }
    /// Return true if this iterator equals \p Other.
    ///
    /// \param Other Iterator to compare against.
    /// \return True if both iterators refer to the same position.
    bool operator==(const iterator &Other) const {
      assert(Map == Other.Map && "Iterator of different objects!");
      return MapIt == Other.MapIt && VecIdx == Other.VecIdx;
    }
    /// Return true if this iterator differs from \p Other.
    ///
    /// \param Other Iterator to compare against.
    /// \return True if the iterators refer to different positions.
    bool operator!=(const iterator &Other) const { return !(*this == Other); }
  };
  /// Const iterator over the underlying bundle map entries.
  using const_iterator = BundleMapT::const_iterator;
  /// Insert load or store \p LSI into the matching seed bundle.
  ///
  /// \param LSI Load or store instruction to insert.
  /// \param AllowDiffTypes If true, allow grouping seeds of different types.
  template <typename LoadOrStoreT>
  void insert(LoadOrStoreT *LSI, bool AllowDiffTypes);
  // To support constant-time erase, these just mark the element used, rather
  // than actually removing them from the bundle.
  /// Mark seed instruction \p I as used, effectively erasing it.
  ///
  /// \param I Seed instruction to erase.
  /// \return True if \p I was found and marked used.
  LLVM_ABI bool erase(Instruction *I);
  /// Erase all bundles associated with \p Key.
  ///
  /// \param Key Bundle key identifying pointer, type, and opcode.
  /// \return True if a mapping for \p Key was removed.
  bool erase(const KeyT &Key) { return Bundles.erase(Key); }
  /// Return an iterator to the first unused SeedBundle.
  /// \return An iterator to the first unused SeedBundle.
  iterator begin() {
    if (Bundles.empty())
      return end();
    auto BeginIt =
        iterator(Bundles, Bundles.begin(), &Bundles.begin()->second, 0);
    BeginIt.skipUsed();
    return BeginIt;
  }
  /// Return an iterator past the last SeedBundle.
  /// \return An iterator past the last SeedBundle.
  iterator end() { return iterator(Bundles, Bundles.end(), nullptr, 0); }
  /// Return the number of keyed bundle groups.
  /// \return The number of keyed bundle groups.
  unsigned size() const { return Bundles.size(); }

#ifndef NDEBUG
  /// Print the contents of this container to \p OS.
  ///
  /// \param OS Destination stream.
  void print(raw_ostream &OS) const;
  /// Dump the contents of this container to the debug stream.
  LLVM_DUMP_METHOD void dump() const;
#endif // NDEBUG
};

// Explicit instantiations
extern template LLVM_TEMPLATE_ABI void
SeedContainer::insert<LoadInst>(LoadInst *, bool);
extern template LLVM_TEMPLATE_ABI void
SeedContainer::insert<StoreInst>(StoreInst *, bool);

/// Collects load and store seed instructions from a basic block.
class SeedCollector {
  SeedContainer StoreSeeds;
  SeedContainer LoadSeeds;
  Context &Ctx;
  Context::CallbackID EraseCallbackID;
  /// Return the number of SeedBundle groups for all seed types.
  /// This is to be used for limiting compilation time.
  unsigned totalNumSeedGroups() const {
    return StoreSeeds.size() + LoadSeeds.size();
  }

public:
  /// Collect seeds from \p BB according to the selected load/store options.
  ///
  /// \param BB Basic block to scan for seed instructions.
  /// \param SE Scalar evolution analysis used when grouping seeds.
  /// \param CollectStores If true, collect store seeds.
  /// \param CollectLoads If true, collect load seeds.
  /// \param AllowDiffTypes If true, allow grouping seeds of different types.
  LLVM_ABI SeedCollector(BasicBlock *BB, ScalarEvolution &SE,
                         bool CollectStores, bool CollectLoads,
                         bool AllowDiffTypes = false);
  /// Destroy this seed collector and unregister callbacks.
  LLVM_ABI ~SeedCollector();

  /// Return a range over unused store seed bundles.
  /// \return A range of unused store SeedBundles.
  iterator_range<SeedContainer::iterator> getStoreSeeds() { return StoreSeeds; }
  /// Return a range over unused load seed bundles.
  /// \return A range of unused load SeedBundles.
  iterator_range<SeedContainer::iterator> getLoadSeeds() { return LoadSeeds; }
#ifndef NDEBUG
  /// Print the collected seeds to \p OS.
  ///
  /// \param OS Destination stream.
  void print(raw_ostream &OS) const;
  /// Dump the collected seeds to the debug stream.
  LLVM_DUMP_METHOD void dump() const;
#endif
};

} // namespace llvm::sandboxir

#endif // LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_SEEDCOLLECTOR_H
