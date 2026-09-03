//===- LiveIntervalUnion.h - Live interval union data struct ---*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// LiveIntervalUnion is a union of live segments across multiple live virtual
// registers. This may be used during coalescing to represent a congruence
// class, or during register allocation to model liveness of a physical
// register.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_LIVEINTERVALUNION_H
#define LLVM_CODEGEN_LIVEINTERVALUNION_H

#include "llvm/ADT/IntervalMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/LiveInterval.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include <cassert>
#include <limits>

namespace llvm {

class raw_ostream;
class TargetRegisterInfo;

#ifndef NDEBUG
// forward declaration
template <unsigned Element> class SparseBitVector;

/// Bit set of live virtual registers used when verifying unions.
using LiveVirtRegBitSet = SparseBitVector<128>;
#endif

/// Union of live intervals that are strong candidates for coalescing.
///
/// The intervals may be coalesced into a single register (either physical or
/// virtual depending on the context). We expect the constituent live intervals
/// to be disjoint, although we may eventually make exceptions to handle
/// value-based interference.
class LiveIntervalUnion {
  // A set of live virtual register segments that supports fast insertion,
  // intersection, and removal.
  // Mapping SlotIndex intervals to virtual register numbers.
  using LiveSegments = IntervalMap<SlotIndex, const LiveInterval *>;

public:
  /// Iterator over segments ordered by starting position.
  ///
  /// SegmentIter can advance to the next segment which may belong to a
  /// different live virtual register. We also must be able to reach the current
  /// segment's containing virtual register.
  using SegmentIter = LiveSegments::iterator;

  /// Const version of SegmentIter.
  using ConstSegmentIter = LiveSegments::const_iterator;

  /// Allocator type shared by LiveIntervalUnion instances.
  ///
  /// LiveIntervalUnions share an external allocator.
  using Allocator = LiveSegments::Allocator;

private:
  unsigned Tag = 0;       // unique tag for current contents.
  LiveSegments Segments;  // union of virtual reg segments

public:
  /// Construct an empty union that allocates segments from \p a.
  ///
  /// \param a Shared segment allocator.
  explicit LiveIntervalUnion(Allocator &a) : Segments(a) {}

  /// Return an iterator to the first segment in the union.
  ///
  /// \return Iterator to the first segment.
  SegmentIter begin() { return Segments.begin(); }
  /// Return an iterator past the last segment in the union.
  ///
  /// \return Iterator past the last segment.
  SegmentIter end() { return Segments.end(); }
  /// Return an iterator to the first segment that ends after \p x.
  ///
  /// \param x Slot index to search for.
  /// \return Iterator to the first segment that ends after \p x.
  SegmentIter find(SlotIndex x) { return Segments.find(x); }
  /// Return a const iterator to the first segment in the union.
  ///
  /// \return Const iterator to the first segment.
  ConstSegmentIter begin() const { return Segments.begin(); }
  /// Return a const iterator past the last segment in the union.
  ///
  /// \return Const iterator past the last segment.
  ConstSegmentIter end() const { return Segments.end(); }
  /// Return a const iterator to the first segment that ends after \p x.
  ///
  /// \param x Slot index to search for.
  /// \return Const iterator to the first segment that ends after \p x.
  ConstSegmentIter find(SlotIndex x) const { return Segments.find(x); }

  /// Return true if the union contains no segments.
  ///
  /// \return True if the union contains no segments.
  bool empty() const { return Segments.empty(); }
  /// Return the start index of the first segment in the union.
  ///
  /// \return Start index of the first segment.
  SlotIndex startIndex() const { return Segments.start(); }
  /// Return the end index of the last segment in the union.
  ///
  /// \return End index of the last segment.
  SlotIndex endIndex() const { return Segments.stop(); }

  /// Underlying interval map type used by the union.
  using Map = LiveSegments;
  /// Return the underlying map to allow overlap iteration.
  ///
  /// \return Const reference to the underlying segment map.
  const Map &getMap() const { return Segments; }

  /// getTag - Return an opaque tag representing the current state of the union.
  ///
  /// \return Opaque tag representing the current state of the union.
  unsigned getTag() const { return Tag; }

  /// changedSince - Return true if the union change since getTag returned tag.
  ///
  /// \param tag Tag previously returned by getTag().
  /// \return True if the union has changed since \p tag was obtained.
  bool changedSince(unsigned tag) const { return tag != Tag; }

  /// Add a live virtual register to this union and merge its segments.
  ///
  /// \param VirtReg Live interval whose identity is recorded in the union.
  /// \param Range Live range segments to merge into the union.
  LLVM_ABI void unify(const LiveInterval &VirtReg, const LiveRange &Range);

  /// Remove a live virtual register's segments from this union.
  ///
  /// \param VirtReg Live interval whose identity is recorded in the union.
  /// \param Range Live range segments to remove from the union.
  LLVM_ABI void extract(const LiveInterval &VirtReg, const LiveRange &Range);

  /// Remove all segments referencing \p VirtRegLI.
  ///
  /// This may be used if the register isn't used anymore. The interval should
  /// have a valid register number but can have empty live ranges.
  ///
  /// \param VirtRegLI Live interval whose segments should be cleared.
  LLVM_ABI void clearAllSegmentsReferencing(const LiveInterval &VirtRegLI);

  /// Remove all inserted virtual registers.
  void clear() { Segments.clear(); ++Tag; }

  /// Print the union, using \p TRI to translate register names.
  ///
  /// \param OS Output stream.
  /// \param TRI Target register info used to print register names, or nullptr.
  LLVM_ABI void print(raw_ostream &OS, const TargetRegisterInfo *TRI) const;

#ifndef NDEBUG
  /// Verify the live intervals in this union and add them to \p VisitedVRegs.
  ///
  /// \param VisitedVRegs Set of visited virtual registers to update.
  void verify(LiveVirtRegBitSet& VisitedVRegs);
#endif

  /// Return any virtual register assigned to this physical unit, or nullptr.
  ///
  /// \return A live interval in the union, or nullptr if empty.
  LLVM_ABI const LiveInterval *getOneVReg() const;

  /// Query interferences between a single live virtual register and a live
  /// interval union.
  class Query {
    const LiveIntervalUnion *LiveUnion = nullptr;
    const LiveRange *LR = nullptr;
    LiveRange::const_iterator LRI;  ///< current position in LR
    ConstSegmentIter LiveUnionI;    ///< current position in LiveUnion
    SmallVector<const LiveInterval *, 4> InterferingVRegs;
    bool CheckedFirstInterference = false;
    bool SeenAllInterferences = false;
    unsigned Tag = 0;
    unsigned UserTag = 0;

    // Count the virtual registers in this union that interfere with this
    // query's live virtual register, up to maxInterferingRegs.
    LLVM_ABI unsigned collectInterferingVRegs(unsigned MaxInterferingRegs);

    // Was this virtual register visited during collectInterferingVRegs?
    bool isSeenInterference(const LiveInterval *VirtReg) const;

  public:
    /// Construct an uninitialized interference query.
    Query() = default;
    /// Construct a query for interference between \p LR and \p LIU.
    ///
    /// \param LR Live range to test for interference.
    /// \param LIU Live interval union to test against.
    Query(const LiveRange &LR, const LiveIntervalUnion &LIU)
        : LiveUnion(&LIU), LR(&LR) {}
    /// Deleted copy constructor; Query is not copyable.
    ///
    /// \param Other Unused; copy construction is deleted.
    Query(const Query &Other) = delete;
    /// Deleted copy assignment; Query is not copyable.
    ///
    /// \param Other Unused; copy assignment is deleted.
    Query &operator=(const Query &Other) = delete;

    /// Reset cached state and bind this query to \p NewLR and \p NewLiveUnion.
    ///
    /// \param NewUserTag Client tag identifying the current query configuration.
    /// \param NewLR Live range to test for interference.
    /// \param NewLiveUnion Live interval union to test against.
    void reset(unsigned NewUserTag, const LiveRange &NewLR,
               const LiveIntervalUnion &NewLiveUnion) {
      LiveUnion = &NewLiveUnion;
      LR = &NewLR;
      InterferingVRegs.clear();
      CheckedFirstInterference = false;
      SeenAllInterferences = false;
      Tag = NewLiveUnion.getTag();
      UserTag = NewUserTag;
    }

    /// Initialize this query, retaining cached results when still valid.
    ///
    /// \param NewUserTag Client tag identifying the current query configuration.
    /// \param NewLR Live range to test for interference.
    /// \param NewLiveUnion Live interval union to test against.
    void init(unsigned NewUserTag, const LiveRange &NewLR,
              const LiveIntervalUnion &NewLiveUnion) {
      if (UserTag == NewUserTag && LR == &NewLR && LiveUnion == &NewLiveUnion &&
          !NewLiveUnion.changedSince(Tag)) {
        // Retain cached results, e.g. firstInterference.
        return;
      }
      reset(NewUserTag, NewLR, NewLiveUnion);
    }

    /// Return true if this live virtual register interferes with the union.
    ///
    /// \return True if any interference was found.
    bool checkInterference() { return collectInterferingVRegs(1); }

    /// Return virtual registers that interfere with this query's live range.
    ///
    /// \param MaxInterferingRegs Maximum number of interfering registers to
    ///        collect; defaults to unlimited.
    /// \return List of interfering live intervals, capped at
    ///         \p MaxInterferingRegs.
    const SmallVectorImpl<const LiveInterval *> &interferingVRegs(
        unsigned MaxInterferingRegs = std::numeric_limits<unsigned>::max()) {
      if (!SeenAllInterferences || MaxInterferingRegs < InterferingVRegs.size())
        collectInterferingVRegs(MaxInterferingRegs);
      return InterferingVRegs;
    }
  };

  /// Fixed-size array of LiveIntervalUnion instances.
  class Array {
    unsigned Size = 0;
    LiveIntervalUnion *LIUs = nullptr;

  public:
    /// Construct an empty, uninitialized array.
    Array() = default;
    /// Destroy the array and free any allocated unions.
    ~Array() { clear(); }

    /// Move-construct this array from \p Other, leaving \p Other empty.
    ///
    /// \param Other Array to move from.
    Array(Array &&Other) : Size(Other.Size), LIUs(Other.LIUs) {
      Other.Size = 0;
      Other.LIUs = nullptr;
    }

    /// Deleted copy constructor; Array is not copyable.
    ///
    /// \param Other Unused; copy construction is deleted.
    Array(const Array &Other) = delete;

    /// Initialize the array to have \p Size entries.
    ///
    /// Reuse an existing allocation if the size matches.
    ///
    /// \param Alloc Shared segment allocator for each union entry.
    /// \param Size Number of LiveIntervalUnion entries to allocate.
    LLVM_ABI void init(LiveIntervalUnion::Allocator &Alloc, unsigned Size);

    /// Return the number of LiveIntervalUnion entries in the array.
    ///
    /// \return Number of LiveIntervalUnion entries.
    unsigned size() const { return Size; }

    /// Destroy and deallocate all LiveIntervalUnion entries.
    LLVM_ABI void clear();

    /// Return the LiveIntervalUnion for register unit \p Unit.
    ///
    /// \param Unit Register unit index into the array.
    /// \return Reference to the LiveIntervalUnion for \p Unit.
    LiveIntervalUnion &operator[](MCRegUnit Unit) {
      assert(static_cast<unsigned>(Unit) < Size && "Unit out of bounds");
      return LIUs[static_cast<unsigned>(Unit)];
    }

    /// Return the LiveIntervalUnion for register unit \p Unit.
    ///
    /// \param Unit Register unit index into the array.
    /// \return Const reference to the LiveIntervalUnion for \p Unit.
    const LiveIntervalUnion &operator[](MCRegUnit Unit) const {
      assert(static_cast<unsigned>(Unit) < Size && "Unit out of bounds");
      return LIUs[static_cast<unsigned>(Unit)];
    }
  };
};

} // end namespace llvm

#endif // LLVM_CODEGEN_LIVEINTERVALUNION_H
