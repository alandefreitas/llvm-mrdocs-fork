//===- llvm/CodeGen/LiveInterval.h - Interval representation ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the LiveRange and LiveInterval classes.  Given some
// numbering of each the machine instructions an interval [i, j) is said to be a
// live range for register v if there is no instruction with number j' >= j
// such that v is live at j' and there is no instruction with number i' < i such
// that v is live at i'. In this implementation ranges can have holes,
// i.e. a range might look like [1,20), [50,65), [1000,1001).  Each
// individual segment is represented as an instance of LiveRange::Segment,
// and the whole range is represented as an instance of LiveRange.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_LIVEINTERVAL_H
#define LLVM_CODEGEN_LIVEINTERVAL_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/IntEqClasses.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/MC/LaneBitmask.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MathExtras.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <functional>
#include <memory>
#include <set>
#include <tuple>
#include <utility>

namespace llvm {

  /// Describes a potential register coalescing opportunity.
  class CoalescerPair;
  class LiveIntervals;
  class MachineRegisterInfo;
  class raw_ostream;

  /// Value Number Information for a live range definition.
  ///
  /// This class holds information about a machine level value, including
  /// definition and use points.
  class VNInfo {
  public:
    /// Bump-pointer allocator used to allocate VNInfo objects.
    using Allocator = BumpPtrAllocator;

    /// The ID number of this value.
    unsigned id;

    /// The index of the defining instruction.
    SlotIndex def;

    /// Construct a value number \p i defined at slot \p d.
    ///
    /// \param i Value number identifier.
    /// \param d Slot index of the defining instruction.
    VNInfo(unsigned i, SlotIndex d) : id(i), def(d) {}

    /// Construct value number \p i by copying fields from \p orig.
    ///
    /// \param i Value number identifier for the copy.
    /// \param orig Existing value whose def index is copied.
    VNInfo(unsigned i, const VNInfo &orig) : id(i), def(orig.def) {}

    /// Copy definition information from \p src into this VNInfo.
    ///
    /// \param src Source value number to copy from.
    void copyFrom(VNInfo &src) {
      def = src.def;
    }

    /// Return true if this value is (or was) defined by a PHI.
    ///
    /// PHI instructions may have been eliminated. PHI-defs begin at a block
    /// boundary, all other defs begin at register or EC slots.
    ///
    /// \return True if this value is (or was) defined by a PHI.
    bool isPHIDef() const { return def.isBlock(); }

    /// Returns true if this value is unused.
    ///
    /// \return True if this value is unused.
    bool isUnused() const { return !def.isValid(); }

    /// Mark this value as unused.
    void markUnused() { def = SlotIndex(); }

    /// Print this value number to \p OS.
    ///
    /// \param OS Output stream.
    LLVM_ABI void print(raw_ostream &OS) const;
    /// Dump this value number to the debug stream.
    LLVM_ABI void dump() const;
  };

  /// Write value-number info \p VNI to stream \p OS.
  ///
  /// \param OS Output stream.
  /// \param VNI Value number to print.
  /// \return Reference to \p OS.
  inline raw_ostream &operator<<(raw_ostream &OS, const VNInfo &VNI) {
    VNI.print(OS);
    return OS;
  }

  /// Result of querying a LiveRange around a single instruction.
  ///
  /// This class hides the implementation details of live ranges, and it should
  /// be used as the primary interface for examining live ranges around
  /// instructions.
  class LiveQueryResult {
    VNInfo *const EarlyVal;
    VNInfo *const LateVal;
    const SlotIndex EndPoint;
    const bool Kill;

  public:
    /// Construct a query result from the live-in/out values around an instr.
    ///
    /// \param EarlyVal Value live into the instruction, if any.
    /// \param LateVal Value live out of or defined by the instruction, if any.
    /// \param EndPoint End point of the interacting live segment.
    /// \param Kill True if the live-in value is killed by the instruction.
    LiveQueryResult(VNInfo *EarlyVal, VNInfo *LateVal, SlotIndex EndPoint,
                    bool Kill)
      : EarlyVal(EarlyVal), LateVal(LateVal), EndPoint(EndPoint), Kill(Kill)
    {}

    /// Return the value that is live-in to the instruction. This is the value
    /// that will be read by the instruction's use operands. Return NULL if no
    /// value is live-in.
    ///
    /// \return The value live-in to the instruction, or null.
    VNInfo *valueIn() const {
      return EarlyVal;
    }

    /// Return true if the live-in value is killed by this instruction. This
    /// means that either the live range ends at the instruction, or it changes
    /// value.
    ///
    /// \return True if the live-in value is killed by this instruction.
    bool isKill() const {
      return Kill;
    }

    /// Return true if this instruction has a dead def.
    ///
    /// \return True if this instruction has a dead def.
    bool isDeadDef() const {
      return EndPoint.isDead();
    }

    /// Return the value leaving the instruction, if any. This can be a
    /// live-through value, or a live def. A dead def returns NULL.
    ///
    /// \return The value leaving the instruction, or null for a dead def.
    VNInfo *valueOut() const {
      return isDeadDef() ? nullptr : LateVal;
    }

    /// Returns the value alive at the end of the instruction, if any. This can
    /// be a live-through value, a live def or a dead def.
    ///
    /// \return The value alive at the end of the instruction, or null.
    VNInfo *valueOutOrDead() const {
      return LateVal;
    }

    /// Return the value defined by this instruction, if any. This includes
    /// dead defs, it is the value created by the instruction's def operands.
    ///
    /// \return The value defined by this instruction, or null.
    VNInfo *valueDefined() const {
      return EarlyVal == LateVal ? nullptr : LateVal;
    }

    /// Return the end point of the last live range segment to interact with
    /// the instruction, if any.
    ///
    /// The end point is an invalid SlotIndex only if the live range doesn't
    /// intersect the instruction at all.
    ///
    /// The end point may be at or past the end of the instruction's basic
    /// block. That means the value was live out of the block.
    ///
    /// \return The end point of the last interacting live segment.
    SlotIndex endPoint() const {
      return EndPoint;
    }
  };

  /// Represents the liveness of a register, stack slot, or similar entity.
  ///
  /// It manages an ordered list of Segment objects. The Segments are organized
  /// in a static single assignment form: At places where a new value is defined
  /// or different values reach a CFG join a new segment with a new value number
  /// is used.
  class LiveRange {
  public:
    /// A single continuous liveness interval for one value number.
    ///
    /// The start point is inclusive, the end point exclusive. These intervals
    /// are rendered as [start,end).
    struct Segment {
      /// Inclusive start of the live segment.
      SlotIndex start;
      /// Exclusive end of the live segment.
      SlotIndex end;
      /// Value number live throughout this segment.
      VNInfo *valno = nullptr;

      /// Construct an uninitialized segment.
      Segment() = default;

      /// Construct a segment [\p S, \p E) carrying value \p V.
      ///
      /// \param S Inclusive start index.
      /// \param E Exclusive end index.
      /// \param V Value number for this segment.
      Segment(SlotIndex S, SlotIndex E, VNInfo *V)
        : start(S), end(E), valno(V) {
        assert(S < E && "Cannot create empty or backwards segment");
      }

      /// Return true if index \p I is covered by this segment.
      ///
      /// \param I Slot index tested for membership.
      /// \return True if index \p I is covered by this segment.
      bool contains(SlotIndex I) const {
        return start <= I && I < end;
      }

      /// Return true if interval [\p S, \p E) is covered by this segment.
      ///
      /// \param S Inclusive start of the query interval.
      /// \param E Exclusive end of the query interval.
      /// \return True if interval [\p S, \p E) is covered by this segment.
      bool containsInterval(SlotIndex S, SlotIndex E) const {
        assert((S < E) && "Backwards interval?");
        return (start <= S && S < end) && (start < E && E <= end);
      }

      /// Compare segments by start, then end.
      ///
      /// \param Other Segment to compare against.
      /// \return True if this segment sorts before \p Other.
      bool operator<(const Segment &Other) const {
        return std::tie(start, end) < std::tie(Other.start, Other.end);
      }
      /// Return true if this segment has the same bounds as \p Other.
      ///
      /// \param Other Segment to compare against.
      /// \return True if this segment has the same bounds as \p Other.
      bool operator==(const Segment &Other) const {
        return start == Other.start && end == Other.end;
      }

      /// Return true if this segment's bounds differ from \p Other.
      ///
      /// \param Other Segment to compare against.
      /// \return True if this segment's bounds differ from \p Other.
      bool operator!=(const Segment &Other) const {
        return !(*this == Other);
      }

      /// Dump this segment to the debug stream.
      LLVM_ABI void dump() const;
    };

    /// Ordered vector of live segments.
    using Segments = SmallVector<Segment, 2>;
    /// Vector of value numbers owned by this live range.
    using VNInfoList = SmallVector<VNInfo *, 2>;

    /// Liveness segments in ascending start order.
    Segments segments;
    /// Value numbers referenced by segments.
    VNInfoList valnos;

    // The segment set is used temporarily to accelerate initial computation
    // of live ranges of physical registers in computeRegUnitRange.
    // After that the set is flushed to the segment vector and deleted.
    /// Ordered set used temporarily while building a live range.
    using SegmentSet = std::set<Segment>;
    /// Optional segment set used during initial live-range construction.
    std::unique_ptr<SegmentSet> segmentSet;

    /// Mutable iterator over live segments.
    using iterator = Segments::iterator;
    /// Const iterator over live segments.
    using const_iterator = Segments::const_iterator;

    /// Return an iterator to the first segment.
    ///
    /// \return Iterator to the first segment.
    iterator begin() { return segments.begin(); }
    /// Return an iterator past the last segment.
    ///
    /// \return Iterator past the last segment.
    iterator end()   { return segments.end(); }

    /// Return a const iterator to the first segment.
    ///
    /// \return Const iterator to the first segment.
    const_iterator begin() const { return segments.begin(); }
    /// Return a const iterator past the last segment.
    ///
    /// \return Const iterator past the last segment.
    const_iterator end() const  { return segments.end(); }

    /// Mutable iterator over value numbers.
    using vni_iterator = VNInfoList::iterator;
    /// Const iterator over value numbers.
    using const_vni_iterator = VNInfoList::const_iterator;

    /// Return an iterator to the first value number.
    ///
    /// \return Iterator to the first value number.
    vni_iterator vni_begin() { return valnos.begin(); }
    /// Return an iterator past the last value number.
    ///
    /// \return Iterator past the last value number.
    vni_iterator vni_end()   { return valnos.end(); }

    /// Return a const iterator to the first value number.
    ///
    /// \return Const iterator to the first value number.
    const_vni_iterator vni_begin() const { return valnos.begin(); }
    /// Return a const iterator past the last value number.
    ///
    /// \return Const iterator past the last value number.
    const_vni_iterator vni_end() const   { return valnos.end(); }

    /// Return a range covering all mutable value numbers.
    ///
    /// \return Range covering all mutable value numbers.
    iterator_range<vni_iterator> vnis() {
      return make_range(vni_begin(), vni_end());
    }

    /// Return a range covering all const value numbers.
    ///
    /// \return Range covering all const value numbers.
    iterator_range<const_vni_iterator> vnis() const {
      return make_range(vni_begin(), vni_end());
    }

    /// Construct an empty LiveRange.
    ///
    /// \param UseSegmentSet If true, accelerate initial construction with a
    ///        temporary segment set.
    LiveRange(bool UseSegmentSet = false)
        : segmentSet(UseSegmentSet ? std::make_unique<SegmentSet>()
                                   : nullptr) {}

    /// Construct a LiveRange by copying segments and valnos from \p Other.
    ///
    /// \param Other Live range to copy from.
    /// \param Allocator Allocator used to duplicate value numbers.
    LiveRange(const LiveRange &Other, BumpPtrAllocator &Allocator) {
      assert(Other.segmentSet == nullptr &&
             "Copying of LiveRanges with active SegmentSets is not supported");
      assign(Other, Allocator);
    }

    /// Copy value numbers and live segments from \p Other into this range.
    ///
    /// \param Other Live range to copy from.
    /// \param Allocator Allocator used to duplicate value numbers.
    void assign(const LiveRange &Other, BumpPtrAllocator &Allocator) {
      if (this == &Other)
        return;

      assert(Other.segmentSet == nullptr &&
             "Copying of LiveRanges with active SegmentSets is not supported");
      // Duplicate valnos.
      for (const VNInfo *VNI : Other.valnos)
        createValueCopy(VNI, Allocator);
      // Now we can copy segments and remap their valnos.
      for (const Segment &S : Other.segments)
        segments.push_back(Segment(S.start, S.end, valnos[S.valno->id]));
    }

    /// Advance iterator \p I to the segment at \p Pos, or end().
    ///
    /// If no Segment contains this position, but the position is in a hole,
    /// this method returns an iterator pointing to the Segment immediately
    /// after the hole.
    ///
    /// \param I Iterator that already points at or before the target segment.
    /// \param Pos Slot index to advance toward.
    /// \return Iterator to the segment at \p Pos, or end().
    iterator advanceTo(iterator I, SlotIndex Pos) {
      assert(I != end());
      if (Pos >= endIndex())
        return end();
      while (I->end <= Pos) ++I;
      return I;
    }

    /// Const overload of advanceTo(\p I, \p Pos).
    ///
    /// \param I Iterator that already points at or before the target segment.
    /// \param Pos Slot index to advance toward.
    /// \return Const iterator to the segment at \p Pos, or end().
    const_iterator advanceTo(const_iterator I, SlotIndex Pos) const {
      assert(I != end());
      if (Pos >= endIndex())
        return end();
      while (I->end <= Pos) ++I;
      return I;
    }

    /// Return an iterator to the first segment that ends after \p Pos.
    ///
    /// This is the same as advanceTo(begin(), Pos), but faster when searching
    /// large ranges.
    ///
    /// If Pos is contained in a Segment, that segment is returned.
    /// If Pos is in a hole, the following Segment is returned.
    /// If Pos is beyond endIndex, end() is returned.
    ///
    /// \param Pos Slot index used as the search key.
    /// \return Iterator to the first segment that ends after \p Pos.
    LLVM_ABI iterator find(SlotIndex Pos);

    /// Const overload of find(\p Pos).
    ///
    /// \param Pos Slot index used as the search key.
    /// \return Const iterator to the first segment that ends after \p Pos.
    const_iterator find(SlotIndex Pos) const {
      return const_cast<LiveRange*>(this)->find(Pos);
    }

    /// Remove all segments and value numbers from this range.
    void clear() {
      valnos.clear();
      segments.clear();
    }

    /// Return the number of segments in this live range.
    ///
    /// \return The number of segments in this live range.
    size_t size() const {
      return segments.size();
    }

    /// Return true if this range has at least one value number.
    ///
    /// \return True if this range has at least one value number.
    bool hasAtLeastOneValue() const { return !valnos.empty(); }

    /// Return true if this range has exactly one value number.
    ///
    /// \return True if this range has exactly one value number.
    bool containsOneValue() const { return valnos.size() == 1; }

    /// Return the number of value numbers in this range.
    ///
    /// \return The number of value numbers in this range.
    unsigned getNumValNums() const { return (unsigned)valnos.size(); }

    /// Return the VNInfo for value number \p ValNo.
    ///
    /// \param ValNo Value number index into valnos.
    /// \return The VNInfo for value number \p ValNo.
    inline VNInfo *getValNumInfo(unsigned ValNo) {
      return valnos[ValNo];
    }
    /// Return the const VNInfo for value number \p ValNo.
    ///
    /// \param ValNo Value number index into valnos.
    /// \return The const VNInfo for value number \p ValNo.
    inline const VNInfo *getValNumInfo(unsigned ValNo) const {
      return valnos[ValNo];
    }

    /// Return true if \p VNI belongs to this range.
    ///
    /// \param VNI Value number tested for membership.
    /// \return True if \p VNI belongs to this range.
    bool containsValue(const VNInfo *VNI) const {
      return VNI && VNI->id < getNumValNums() && VNI == getValNumInfo(VNI->id);
    }

    /// Create and return a new value number defined at \p Def.
    ///
    /// @p Def is the index of instruction that defines the value number.
    ///
    /// \param Def Slot index of the defining instruction.
    /// \param VNInfoAllocator Allocator used to construct the new VNInfo.
    /// \return Newly created value number defined at \p Def.
    VNInfo *getNextValue(SlotIndex Def, VNInfo::Allocator &VNInfoAllocator) {
      VNInfo *VNI =
        new (VNInfoAllocator) VNInfo((unsigned)valnos.size(), Def);
      valnos.push_back(VNI);
      return VNI;
    }

    /// Ensure a value is defined at \p Def, creating a dead def if needed.
    ///
    /// If one already exists, return it. Otherwise allocate a new value and
    /// add liveness for a dead def.
    ///
    /// \param Def Slot index of the dead definition.
    /// \param VNIAlloc Allocator used to construct a new VNInfo if needed.
    /// \return Existing or newly created dead def at \p Def.
    LLVM_ABI VNInfo *createDeadDef(SlotIndex Def, VNInfo::Allocator &VNIAlloc);

    /// Create a dead def of existing value \p VNI.
    ///
    /// Return @p VNI. If there already exists a definition at VNI->def, the
    /// value defined there must be @p VNI.
    ///
    /// \param VNI Value number to define as a dead def.
    /// \return The value number \p VNI.
    LLVM_ABI VNInfo *createDeadDef(VNInfo *VNI);

    /// Create a copy of \p orig with a fresh value number.
    ///
    /// The new value will be identical except for the Value number.
    ///
    /// \param orig Value number to copy.
    /// \param VNInfoAllocator Allocator used to construct the new VNInfo.
    /// \return Fresh copy of \p orig with a new value number.
    VNInfo *createValueCopy(const VNInfo *orig,
                            VNInfo::Allocator &VNInfoAllocator) {
      VNInfo *VNI =
        new (VNInfoAllocator) VNInfo((unsigned)valnos.size(), *orig);
      valnos.push_back(VNI);
      return VNI;
    }

    /// RenumberValues - Renumber all values in order of appearance and remove
    /// unused values.
    LLVM_ABI void RenumberValues();

    /// Merge equivalent value number \p V1 into \p V2.
    ///
    /// This eliminates V1, replacing all segments with the V1 value number with
    /// the V2 value number. This can cause merging of V1/V2 values numbers and
    /// compaction of the value space.
    ///
    /// \param V1 Value number being eliminated.
    /// \param V2 Value number that replaces V1.
    /// \return The surviving value number after the merge.
    LLVM_ABI VNInfo *MergeValueNumberInto(VNInfo *V1, VNInfo *V2);

    /// Merge all segments from \p RHS into this range as \p LHSValNo.
    ///
    /// The segments in RHS are allowed to overlap with segments in the current
    /// range; overlapping live segments are reassigned to the specified value
    /// number.
    ///
    /// \param RHS Live range providing segments to merge.
    /// \param LHSValNo Value number assigned to the merged segments here.
    LLVM_ABI void MergeSegmentsInAsValue(const LiveRange &RHS,
                                         VNInfo *LHSValNo);

    /// Merge segments of \p RHSValNo from \p RHS as \p LHSValNo.
    ///
    /// The segments in RHS are allowed to overlap with segments in the
    /// current range, but only if the overlapping segments have the
    /// specified value number.
    ///
    /// \param RHS Live range providing segments to merge.
    /// \param RHSValNo Value number in RHS whose segments are taken.
    /// \param LHSValNo Value number assigned to the merged segments here.
    LLVM_ABI void MergeValueInAsValue(const LiveRange &RHS,
                                      const VNInfo *RHSValNo, VNInfo *LHSValNo);

    /// Return true if this live range contains no segments.
    ///
    /// \return True if this live range contains no segments.
    bool empty() const { return segments.empty(); }

    /// beginIndex - Return the lowest numbered slot covered.
    ///
    /// \return The lowest numbered slot covered.
    SlotIndex beginIndex() const {
      assert(!empty() && "Call to beginIndex() on empty range.");
      return segments.front().start;
    }

    /// endNumber - return the maximum point of the range of the whole,
    /// exclusive.
    ///
    /// \return The exclusive maximum point of the whole range.
    SlotIndex endIndex() const {
      assert(!empty() && "Call to endIndex() on empty range.");
      return segments.back().end;
    }

    /// Return true if liveness has ended by slot index \p index.
    ///
    /// \param index Slot index tested against endIndex().
    /// \return True if liveness has ended by slot index \p index.
    bool expiredAt(SlotIndex index) const {
      return index >= endIndex();
    }

    /// Return true if this range is live at slot index \p index.
    ///
    /// \param index Slot index tested for liveness.
    /// \return True if this range is live at slot index \p index.
    bool liveAt(SlotIndex index) const {
      const_iterator r = find(index);
      return r != end() && r->start <= index;
    }

    /// Return the const segment containing \p Idx, or null.
    ///
    /// \param Idx Slot index to locate within a segment.
    /// \return The const segment containing \p Idx, or null.
    const Segment *getSegmentContaining(SlotIndex Idx) const {
      const_iterator I = FindSegmentContaining(Idx);
      return I == end() ? nullptr : &*I;
    }

    /// Return the mutable segment containing \p Idx, or null.
    ///
    /// \param Idx Slot index to locate within a segment.
    /// \return The mutable segment containing \p Idx, or null.
    Segment *getSegmentContaining(SlotIndex Idx) {
      iterator I = FindSegmentContaining(Idx);
      return I == end() ? nullptr : &*I;
    }

    /// Return the VNInfo live at \p Idx, or null.
    ///
    /// \param Idx Slot index whose live value is requested.
    /// \return The VNInfo live at \p Idx, or null.
    VNInfo *getVNInfoAt(SlotIndex Idx) const {
      const_iterator I = FindSegmentContaining(Idx);
      return I == end() ? nullptr : I->valno;
    }

    /// Return the VNInfo live up to but not necessarily including \p Idx.
    ///
    /// Use this to find the reaching def used by an instruction at this
    /// SlotIndex position.
    ///
    /// \param Idx Slot index whose preceding live value is requested.
    /// \return The VNInfo live before \p Idx, or null.
    VNInfo *getVNInfoBefore(SlotIndex Idx) const {
      const_iterator I = FindSegmentContaining(Idx.getPrevSlot());
      return I == end() ? nullptr : I->valno;
    }

    /// Return an iterator to the segment containing \p Idx, or end().
    ///
    /// \param Idx Slot index to locate within a segment.
    /// \return Iterator to the segment containing \p Idx, or end().
    iterator FindSegmentContaining(SlotIndex Idx) {
      iterator I = find(Idx);
      return I != end() && I->start <= Idx ? I : end();
    }

    /// Return a const iterator to the segment containing \p Idx, or end().
    ///
    /// \param Idx Slot index to locate within a segment.
    /// \return Const iterator to the segment containing \p Idx, or end().
    const_iterator FindSegmentContaining(SlotIndex Idx) const {
      const_iterator I = find(Idx);
      return I != end() && I->start <= Idx ? I : end();
    }

    /// Return true if this live range intersects \p other.
    ///
    /// \param other Live range tested for intersection.
    /// \return True if this live range intersects \p other.
    bool overlaps(const LiveRange &other) const {
      if (empty() || other.empty())
        return false;
      return overlapsFrom(other, other.begin());
    }

    /// Return true if overlapping segments are not coalescable per \p CP.
    ///
    /// Overlapping segments where one range is defined by a coalescable
    /// copy are allowed.
    ///
    /// \param Other Live range tested for conflicting overlap.
    /// \param CP Coalescer pair describing an allowed copy relationship.
    /// \param Indexes Slot indexes for the function.
    /// \return True if overlapping segments are not coalescable per \p CP.
    LLVM_ABI bool overlaps(const LiveRange &Other, const CoalescerPair &CP,
                           const SlotIndexes &Indexes) const;

    /// Return true if this live range overlaps [\p Start, \p End).
    ///
    /// \param Start Inclusive start of the query interval.
    /// \param End Exclusive end of the query interval.
    /// \return True if this live range overlaps [\p Start, \p End).
    LLVM_ABI bool overlaps(SlotIndex Start, SlotIndex End) const;

    /// Return true if this range overlaps \p Other from \p StartPos onward.
    ///
    /// The specified iterator is a hint that we can begin scanning the Other
    /// range starting at I.
    ///
    /// \param Other Live range tested for intersection.
    /// \param StartPos Iterator hint into Other where scanning may begin.
    /// \return True if this range overlaps \p Other from \p StartPos onward.
    LLVM_ABI bool overlapsFrom(const LiveRange &Other,
                               const_iterator StartPos) const;

    /// Return true if every segment of \p Other is covered by this range.
    ///
    /// Adjacent live ranges do not affect the covering: the liverange
    /// [1,5](5,10] covers (3,7].
    ///
    /// \param Other Live range whose segments must be covered.
    /// \return True if every segment of \p Other is covered by this range.
    LLVM_ABI bool covers(const LiveRange &Other) const;

    /// Add segment \p S to this range, merging as appropriate.
    ///
    /// Returns an iterator to the inserted segment (which may have grown since
    /// it was inserted).
    ///
    /// \param S Segment to insert into this live range.
    /// \return Iterator to the inserted (possibly grown) segment.
    LLVM_ABI iterator addSegment(Segment S);

    /// Merge the segment at \p I with adjacent same-value neighbors.
    ///
    /// @p I must be a valid iterator into this live range. Returns an iterator
    /// to the merged segment, which may be @p I or the previous segment if
    /// @p I was merged into it.
    ///
    /// \param I Iterator to the segment that may be merged with neighbors.
    /// \return Iterator to the merged segment.
    LLVM_ABI iterator mergeAdjacentSegments(iterator I);

    /// Attempt to extend a value defined after \p StartIdx up to \p Kill.
    ///
    /// Both @p StartIdx and @p Use should be in the same basic block. In case
    /// of subranges, an extension could be prevented by an explicit "undef"
    /// caused by a <def,read-undef> on a non-overlapping lane. The list of
    /// location of such "undefs" should be provided in @p Undefs.
    /// The return value is a pair: the first element is VNInfo of the value
    /// that was extended (possibly nullptr), the second is a boolean value
    /// indicating whether an "undef" was encountered.
    /// If this range is live before @p Use in the basic block that starts at
    /// @p StartIdx, and there is no intervening "undef", extend it to be live
    /// up to @p Use, and return the pair {value, false}. If there is no
    /// segment before @p Use and there is no "undef" between @p StartIdx and
    /// @p Use, return {nullptr, false}. If there is an "undef" before @p Use,
    /// return {nullptr, true}.
    ///
    /// \param Undefs Slot indexes of explicit undefs that can block extension.
    /// \param StartIdx Start of the basic block containing the use.
    /// \param Kill Slot index up to which liveness should be extended.
    /// \return Pair of the extended value (or null) and whether an undef was
    ///         encountered.
    LLVM_ABI std::pair<VNInfo *, bool> extendInBlock(ArrayRef<SlotIndex> Undefs,
                                                     SlotIndex StartIdx,
                                                     SlotIndex Kill);

    /// Extend a value in-block to \p Kill, ignoring read-undef lanes.
    ///
    /// Simplified version of the above "extendInBlock", which assumes that
    /// no register lanes are undefined by <def,read-undef> operands.
    /// If this range is live before @p Use in the basic block that starts
    /// at @p StartIdx, extend it to be live up to @p Use, and return the
    /// value. If there is no segment before @p Use, return nullptr.
    ///
    /// \param StartIdx Start of the basic block containing the use.
    /// \param Kill Slot index up to which liveness should be extended.
    /// \return The extended value, or null if none existed before \p Kill.
    LLVM_ABI VNInfo *extendInBlock(SlotIndex StartIdx, SlotIndex Kill);

    /// Join live range \p Other into this range using value-number mappings.
    ///
    /// This applies mappings to the value numbers in the LHS/RHS ranges as
    /// specified. If the ranges are not joinable, this aborts.
    ///
    /// \param Other Live range joined into this one.
    /// \param ValNoAssignments Mapping from this range's value numbers.
    /// \param RHSValNoAssignments Mapping from Other's value numbers.
    /// \param NewVNInfo Combined value-number table after the join.
    LLVM_ABI void join(LiveRange &Other, const int *ValNoAssignments,
                       const int *RHSValNoAssignments,
                       SmallVectorImpl<VNInfo *> &NewVNInfo);

    /// Return true if this range is a single local segment between the bounds.
    ///
    /// True iff this segment is a single segment that lies between the
    /// specified boundaries, exclusively. Vregs live across a backedge are not
    /// considered local. The boundaries are expected to lie within an extended
    /// basic block, so vregs that are not live out should contain no holes.
    ///
    /// \param Start Exclusive lower bound of the local region.
    /// \param End Exclusive upper bound of the local region.
    /// \return True if this range is a single local segment between the bounds.
    bool isLocal(SlotIndex Start, SlotIndex End) const {
      return beginIndex() > Start.getBaseIndex() &&
        endIndex() < End.getBoundaryIndex();
    }

    /// Remove the interval [\p Start, \p End) from this live range.
    ///
    /// Does nothing if the interval is not part of this live range. Note that
    /// the interval must be within a single Segment in its entirety.
    ///
    /// \param Start Inclusive start of the interval to remove.
    /// \param End Exclusive end of the interval to remove.
    /// \param RemoveDeadValNo If true, delete value numbers left unused.
    LLVM_ABI void removeSegment(SlotIndex Start, SlotIndex End,
                                bool RemoveDeadValNo = false);

    /// Remove segment \p S from this live range.
    ///
    /// \param S Segment describing the interval to remove.
    /// \param RemoveDeadValNo If true, delete value numbers left unused.
    void removeSegment(Segment S, bool RemoveDeadValNo = false) {
      removeSegment(S.start, S.end, RemoveDeadValNo);
    }

    /// Remove the segment pointed to by iterator \p I.
    ///
    /// \param I Iterator to the segment to remove.
    /// \param RemoveDeadValNo If true, delete value numbers left unused.
    /// \return Iterator to the segment following the removed one.
    LLVM_ABI iterator removeSegment(iterator I, bool RemoveDeadValNo = false);

    /// Mark \p ValNo for deletion if no segments in this range use it.
    ///
    /// \param ValNo Value number that may be unused after segment edits.
    LLVM_ABI void removeValNoIfDead(VNInfo *ValNo);

    /// Query liveness around instruction \p Idx.
    ///
    /// The sub-instruction slot of Idx doesn't matter, only the instruction
    /// it refers to is considered.
    ///
    /// \param Idx Slot index of the instruction to query.
    /// \return Query result describing liveness around instruction \p Idx.
    LiveQueryResult Query(SlotIndex Idx) const {
      // Find the segment that enters the instruction.
      const_iterator I = find(Idx.getBaseIndex());
      const_iterator E = end();
      if (I == E)
        return LiveQueryResult(nullptr, nullptr, SlotIndex(), false);

      // Is this an instruction live-in segment?
      // If Idx is the start index of a basic block, include live-in segments
      // that start at Idx.getBaseIndex().
      VNInfo *EarlyVal = nullptr;
      VNInfo *LateVal  = nullptr;
      SlotIndex EndPoint;
      bool Kill = false;
      if (I->start <= Idx.getBaseIndex()) {
        EarlyVal = I->valno;
        EndPoint = I->end;
        // Move to the potentially live-out segment.
        if (SlotIndex::isSameInstr(Idx, I->end)) {
          Kill = true;
          if (++I == E)
            return LiveQueryResult(EarlyVal, LateVal, EndPoint, Kill);
        }
        // Special case: A PHIDef value can have its def in the middle of a
        // segment if the value happens to be live out of the layout
        // predecessor.
        // Such a value is not live-in.
        if (EarlyVal->def == Idx.getBaseIndex())
          EarlyVal = nullptr;
      }
      // I now points to the segment that may be live-through, or defined by
      // this instr. Ignore segments starting after the current instr.
      if (!SlotIndex::isEarlierInstr(Idx, I->start)) {
        LateVal = I->valno;
        EndPoint = I->end;
      }
      return LiveQueryResult(EarlyVal, LateVal, EndPoint, Kill);
    }

    /// Remove all segments defined by \p ValNo and drop the value number.
    ///
    /// \param ValNo Value number whose segments are removed.
    LLVM_ABI void removeValNo(VNInfo *ValNo);

    /// Return true if no live segment spans an instruction.
    ///
    /// It doesn't pay to spill such a range.
    ///
    /// \param Indexes Slot index mapping used to detect instruction-spanning
    ///        segments.
    /// \return True if no live segment spans an instruction.
    bool isZeroLength(SlotIndexes *Indexes) const {
      for (const Segment &S : segments)
        if (Indexes->getNextNonNullIndex(S.start).getBaseIndex() <
            S.end.getBaseIndex())
          return false;
      return true;
    }

    /// Return true if any segment contains one of the provided slot indexes.
    ///
    /// Slots which occur in holes between segments will not cause the function
    /// to return true.
    ///
    /// \param Slots Slot indexes tested for membership in this live range.
    /// \return True if any segment contains one of the provided slot indexes.
    LLVM_ABI bool isLiveAtIndexes(ArrayRef<SlotIndex> Slots) const;

    /// Compare live ranges by their begin index.
    ///
    /// \param other Live range to compare against.
    /// \return True if this live range begins before \p other.
    bool operator<(const LiveRange& other) const {
      const SlotIndex &thisIndex = beginIndex();
      const SlotIndex &otherIndex = other.beginIndex();
      return thisIndex < otherIndex;
    }

    /// Return true if there is an explicit undef in [\p Begin, \p End).
    ///
    /// \param Undefs Slot indexes marked undef by read-undef definitions.
    /// \param Begin Inclusive start of the query interval.
    /// \param End Exclusive end of the query interval.
    /// \return True if there is an explicit undef in [\p Begin, \p End).
    bool isUndefIn(ArrayRef<SlotIndex> Undefs, SlotIndex Begin,
                   SlotIndex End) const {
      return llvm::any_of(Undefs, [Begin, End](SlotIndex Idx) -> bool {
        return Begin <= Idx && Idx < End;
      });
    }

    /// Flush the temporary segment set into the segment vector.
    ///
    /// The method is to be called after the live range has been created, if use
    /// of the segment set was activated in the constructor of the live range.
    LLVM_ABI void flushSegmentSet();

    /// Store indexes from \p R at which this live range is live into \p O.
    ///
    /// R is a range of ascending sorted random access iterators to the input
    /// indexes. Indexes stored at O are ascending sorted so it can be used
    /// directly in the subsequent search (for example for subranges). Returns
    /// true if found at least one index.
    ///
    /// \param R Ascending sorted range of slot indexes to test.
    /// \param O Output iterator that receives live indexes from R.
    /// \return True if at least one index from \p R was live.
    template <typename Range, typename OutputIt>
    bool findIndexesLiveAt(Range &&R, OutputIt O) const {
      assert(llvm::is_sorted(R));
      auto Idx = R.begin(), EndIdx = R.end();
      auto Seg = segments.begin(), EndSeg = segments.end();
      bool Found = false;
      while (Idx != EndIdx && Seg != EndSeg) {
        // if the Seg is lower find first segment that is above Idx using binary
        // search
        if (Seg->end <= *Idx) {
          Seg =
              std::upper_bound(++Seg, EndSeg, *Idx, [=](auto V, const auto &S) {
                return V < S.end;
              });
          if (Seg == EndSeg)
            break;
        }
        auto NotLessStart = std::lower_bound(Idx, EndIdx, Seg->start);
        if (NotLessStart == EndIdx)
          break;
        auto NotLessEnd = std::lower_bound(NotLessStart, EndIdx, Seg->end);
        if (NotLessEnd != NotLessStart) {
          Found = true;
          O = std::copy(NotLessStart, NotLessEnd, O);
        }
        Idx = NotLessEnd;
        ++Seg;
      }
      return Found;
    }

    /// Print this live range to \p OS.
    ///
    /// \param OS Output stream.
    LLVM_ABI void print(raw_ostream &OS) const;
    /// Dump this live range to the debug stream.
    LLVM_ABI void dump() const;

#ifdef NDEBUG
    /// Walk the range and assert if any invariants fail to hold.
    ///
    /// Note that this is a no-op when asserts are disabled.
    ///
    /// \return True if all live-range invariants hold.
    [[nodiscard]] bool verify() const { return true; }
#else
    /// Walk the range and assert if any invariants fail to hold.
    ///
    /// Note that this is a no-op when asserts are disabled.
    ///
    /// \return True if all live-range invariants hold.
    [[nodiscard]] bool verify() const;
#endif

  protected:
    /// Append segment \p S to the list of segments.
    ///
    /// \param S Segment to append; must begin after existing segments.
    LLVM_ABI void append(const LiveRange::Segment S);

  private:
    friend class LiveRangeUpdater;
    void addSegmentToSet(Segment S);
    void markValNoForDeletion(VNInfo *V);
  };

  /// Write live range \p LR to stream \p OS.
  ///
  /// \param OS Output stream.
  /// \param LR Live range to print.
  /// \return Reference to \p OS.
  inline raw_ostream &operator<<(raw_ostream &OS, const LiveRange &LR) {
    LR.print(OS);
    return OS;
  }

  /// LiveInterval - This class represents the liveness of a register,
  /// or stack slot.
  class LiveInterval : public LiveRange {
  public:
    /// Base LiveRange type of this live interval.
    using super = LiveRange;

    /// A live range covering selected lanes of a super-register.
    ///
    /// The LaneMask specifies which parts of the super register are covered by
    /// the interval.
    /// (@sa TargetRegisterInfo::getSubRegIndexLaneMask()).
    class SubRange : public LiveRange {
    public:
      /// Next subrange in the owning LiveInterval's singly linked list.
      SubRange *Next = nullptr;
      /// Lane mask describing which parts of the super-register are covered.
      LaneBitmask LaneMask;

      /// Construct an empty subrange for \p LaneMask.
      ///
      /// \param LaneMask Lane mask covered by this subrange.
      SubRange(LaneBitmask LaneMask) : LaneMask(LaneMask) {}

      /// Construct a subrange for \p LaneMask by copying liveness from \p Other.
      ///
      /// \param LaneMask Lane mask covered by this subrange.
      /// \param Other Live range whose segments and values are copied.
      /// \param Allocator Allocator used to duplicate value numbers.
      SubRange(LaneBitmask LaneMask, const LiveRange &Other,
               BumpPtrAllocator &Allocator)
        : LiveRange(Other, Allocator), LaneMask(LaneMask) {}

      /// Print this subrange to \p OS.
      ///
      /// \param OS Output stream.
      LLVM_ABI void print(raw_ostream &OS) const;
      /// Dump this subrange to the debug stream.
      LLVM_ABI void dump() const;
    };

  private:
    SubRange *SubRanges = nullptr; ///< Single linked list of subregister live
                                   /// ranges.
    const Register Reg; // the register or stack slot of this interval.
    float Weight = 0.0; // weight of this interval

  public:
    /// Return the register or stack slot represented by this interval.
    ///
    /// \return The register or stack slot represented by this interval.
    Register reg() const { return Reg; }
    /// Return the spill weight of this interval.
    ///
    /// \return The spill weight of this interval.
    float weight() const { return Weight; }
    /// Increase the spill weight by \p Inc.
    ///
    /// \param Inc Amount added to the current weight.
    void incrementWeight(float Inc) { Weight += Inc; }
    /// Set the spill weight to \p Value.
    ///
    /// \param Value New spill weight.
    void setWeight(float Value) { Weight = Value; }

    /// Construct a live interval for \p Reg with initial weight \p Weight.
    ///
    /// \param Reg Register or stack slot represented by this interval.
    /// \param Weight Initial spill weight.
    LiveInterval(Register Reg, float Weight) : Reg(Reg), Weight(Weight) {}

    /// Destroy the live interval and free its subranges.
    ~LiveInterval() {
      clearSubRanges();
    }

    /// Forward iterator over a singly linked list of nodes with a Next pointer.
    template<typename T>
    class SingleLinkedListIterator {
      T *P;

    public:
      /// Signed distance type for the iterator.
      using difference_type = ptrdiff_t;
      /// Element type pointed to by the iterator.
      using value_type = T;
      /// Pointer type returned by operator->.
      using pointer = T *;
      /// Reference type returned by operator*.
      using reference = T &;
      /// Iterator category tag (forward iterator).
      using iterator_category = std::forward_iterator_tag;

      /// Construct an iterator pointing at node \p P.
      ///
      /// \param P Node to point to, or null for end.
      SingleLinkedListIterator(T *P) : P(P) {}

      /// Advance to the next node and return this iterator.
      ///
      /// \return Reference to this iterator after advancing.
      SingleLinkedListIterator<T> &operator++() {
        P = P->Next;
        return *this;
      }
      /// Advance to the next node and return the previous iterator value.
      ///
      /// \param Unused Unused postfix-discriminator parameter.
      /// \return Copy of the iterator before advancing.
      SingleLinkedListIterator<T> operator++(int Unused) {
        SingleLinkedListIterator res = *this;
        ++*this;
        return res;
      }
      /// Return true if this iterator does not point to the same node as \p Other.
      ///
      /// \param Other Iterator to compare against.
      /// \return True if this iterator does not point to the same node as \p Other.
      bool operator!=(const SingleLinkedListIterator<T> &Other) const {
        return P != Other.operator->();
      }
      /// Return true if this iterator points to the same node as \p Other.
      ///
      /// \param Other Iterator to compare against.
      /// \return True if this iterator points to the same node as \p Other.
      bool operator==(const SingleLinkedListIterator<T> &Other) const {
        return P == Other.operator->();
      }
      /// Return a reference to the current node.
      ///
      /// \return Reference to the current node.
      T &operator*() const {
        return *P;
      }
      /// Return a pointer to the current node.
      ///
      /// \return Pointer to the current node.
      T *operator->() const {
        return P;
      }
    };

    /// Mutable iterator over subregister live ranges.
    using subrange_iterator = SingleLinkedListIterator<SubRange>;
    /// Const iterator over subregister live ranges.
    using const_subrange_iterator = SingleLinkedListIterator<const SubRange>;

    /// Return an iterator to the first subrange.
    ///
    /// \return Iterator to the first subrange.
    subrange_iterator subrange_begin() {
      return subrange_iterator(SubRanges);
    }
    /// Return an iterator past the last subrange.
    ///
    /// \return Iterator past the last subrange.
    subrange_iterator subrange_end() {
      return subrange_iterator(nullptr);
    }

    /// Return a const iterator to the first subrange.
    ///
    /// \return Const iterator to the first subrange.
    const_subrange_iterator subrange_begin() const {
      return const_subrange_iterator(SubRanges);
    }
    /// Return a const iterator past the last subrange.
    ///
    /// \return Const iterator past the last subrange.
    const_subrange_iterator subrange_end() const {
      return const_subrange_iterator(nullptr);
    }

    /// Return a range covering all mutable subranges.
    ///
    /// \return Range covering all mutable subranges.
    iterator_range<subrange_iterator> subranges() {
      return make_range(subrange_begin(), subrange_end());
    }

    /// Return a range covering all const subranges.
    ///
    /// \return Range covering all const subranges.
    iterator_range<const_subrange_iterator> subranges() const {
      return make_range(subrange_begin(), subrange_end());
    }

    /// Create a new empty subregister live range for \p LaneMask.
    ///
    /// The range is added at the beginning of the subrange list; subrange
    /// iterators stay valid.
    ///
    /// \param Allocator Allocator used to construct the subrange.
    /// \param LaneMask Lane mask covered by the new subrange.
    /// \return Newly created empty subrange for \p LaneMask.
    SubRange *createSubRange(BumpPtrAllocator &Allocator,
                             LaneBitmask LaneMask) {
      SubRange *Range = new (Allocator) SubRange(LaneMask);
      appendSubRange(Range);
      return Range;
    }

    /// Create a subrange for \p LaneMask copied from \p CopyFrom.
    ///
    /// Like createSubRange() but the new range is filled with a copy of the
    /// liveness information in @p CopyFrom.
    ///
    /// \param Allocator Allocator used to construct the subrange.
    /// \param LaneMask Lane mask covered by the new subrange.
    /// \param CopyFrom Live range whose segments and values are copied.
    /// \return Newly created subrange for \p LaneMask copied from \p CopyFrom.
    SubRange *createSubRangeFrom(BumpPtrAllocator &Allocator,
                                 LaneBitmask LaneMask,
                                 const LiveRange &CopyFrom) {
      SubRange *Range = new (Allocator) SubRange(LaneMask, CopyFrom, Allocator);
      appendSubRange(Range);
      return Range;
    }

    /// Returns true if subregister liveness information is available.
    ///
    /// \return True if subregister liveness information is available.
    bool hasSubRanges() const {
      return SubRanges != nullptr;
    }

    /// Removes all subregister liveness information.
    LLVM_ABI void clearSubRanges();

    /// Removes all subranges without any segments (subranges without segments
    /// are not considered valid and should only exist temporarily).
    LLVM_ABI void removeEmptySubRanges();

    /// getSize - Returns the sum of sizes of all the LiveRange's.
    ///
    /// \return The sum of sizes of all the LiveRange's segments.
    LLVM_ABI unsigned getSize() const;

    /// isSpillable - Can this interval be spilled?
    ///
    /// \return True if this interval can be spilled.
    bool isSpillable() const { return Weight != huge_valf; }

    /// markNotSpillable - Mark interval as not spillable
    void markNotSpillable() { Weight = huge_valf; }

    /// Compute indexes where \p LaneMask is undef due to read-undef defs.
    ///
    /// For a given lane mask @p LaneMask, compute indexes at which the
    /// lane is marked undefined by subregister <def,read-undef> definitions.
    ///
    /// \param Undefs Output vector that receives undef slot indexes.
    /// \param LaneMask Lane mask whose undef sites are collected.
    /// \param MRI Machine register info providing definitions.
    /// \param Indexes Slot index mapping for the function.
    LLVM_ABI void computeSubRangeUndefs(SmallVectorImpl<SlotIndex> &Undefs,
                                        LaneBitmask LaneMask,
                                        const MachineRegisterInfo &MRI,
                                        const SlotIndexes &Indexes) const;

    /// Refine subranges so that \p LaneMask matches an exact subrange set.
    ///
    /// This may only be called for LI.hasSubrange()==true. Subregister ranges
    /// are split or created until \p LaneMask can be matched exactly. \p Apply
    /// is executed on the matching subranges.
    ///
    /// Example:
    ///    Given an interval with subranges with lanemasks L0F00, L00F0 and
    ///    L000F, refining for mask L0018. Will split the L00F0 lane into
    ///    L00E0 and L0010 and the L000F lane into L0007 and L0008. The Apply
    ///    function will be applied to the L0010 and L0008 subranges.
    ///
    /// \p Indexes and \p TRI are required to clean up the VNIs that
    /// don't define the related lane masks after they get shrunk. E.g.,
    /// when L000F gets split into L0007 and L0008 maybe only a subset
    /// of the VNIs that defined L000F defines L0007.
    ///
    /// The clean up of the VNIs need to look at the actual instructions
    /// to decide what is or is not live at a definition point. If the
    /// update of the subranges occurs while the IR does not reflect these
    /// changes, \p ComposeSubRegIdx can be used to specify how the
    /// definition are going to be rewritten.
    /// E.g., let say we want to merge:
    ///     V1.sub1:<2 x s32> = COPY V2.sub3:<4 x s32>
    /// We do that by choosing a class where sub1:<2 x s32> and sub3:<4 x s32>
    /// overlap, i.e., by choosing a class where we can find "offset + 1 == 3".
    /// Put differently we align V2's sub3 with V1's sub1:
    /// V2: sub0 sub1 sub2 sub3
    /// V1: <offset>  sub0 sub1
    ///
    /// This offset will look like a composed subregidx in the class:
    ///     V1.(composed sub2 with sub1):<4 x s32> = COPY V2.sub3:<4 x s32>
    /// =>  V1.(composed sub2 with sub1):<4 x s32> = COPY V2.sub3:<4 x s32>
    ///
    /// Now if we didn't rewrite the uses and def of V1, all the checks for V1
    /// need to account for this offset.
    /// This happens during coalescing where we update the live-ranges while
    /// still having the old IR around because updating the IR on-the-fly
    /// would actually clobber some information on how the live-ranges that
    /// are being updated look like.
    ///
    /// \param Allocator Allocator used to create new subranges.
    /// \param LaneMask Lane mask that must be matched exactly after refining.
    /// \param Apply Callback invoked on each subrange that matches LaneMask.
    /// \param Indexes Slot indexes used when cleaning up VNIs.
    /// \param TRI Target register info used when cleaning up VNIs.
    /// \param ComposeSubRegIdx Optional composed subreg index describing a
    ///        pending rewrite of definitions.
    LLVM_ABI void
    refineSubRanges(BumpPtrAllocator &Allocator, LaneBitmask LaneMask,
                    std::function<void(LiveInterval::SubRange &)> Apply,
                    const SlotIndexes &Indexes, const TargetRegisterInfo &TRI,
                    unsigned ComposeSubRegIdx = 0);

    /// Compare live intervals by begin index, then by register number.
    ///
    /// \param other Live interval to compare against.
    /// \return True if this interval sorts before \p other.
    bool operator<(const LiveInterval& other) const {
      const SlotIndex &thisIndex = beginIndex();
      const SlotIndex &otherIndex = other.beginIndex();
      return std::tie(thisIndex, Reg) < std::tie(otherIndex, other.Reg);
    }

    /// Print this live interval to \p OS.
    ///
    /// \param OS Output stream.
    LLVM_ABI void print(raw_ostream &OS) const;
    /// Dump this live interval to the debug stream.
    LLVM_ABI void dump() const;

#ifdef NDEBUG
    /// Walk the interval and assert if any invariants fail to hold.
    ///
    /// Note that this is a no-op when asserts are disabled.
    ///
    /// \param MRI Optional machine register info for deeper checks.
    /// \return True if all live-interval invariants hold.
    [[nodiscard]] bool verify(const MachineRegisterInfo *MRI = nullptr) const {
      return true;
    }
#else
    /// Walk the interval and assert if any invariants fail to hold.
    ///
    /// Note that this is a no-op when asserts are disabled.
    ///
    /// \param MRI Optional machine register info for deeper checks.
    /// \return True if all live-interval invariants hold.
    [[nodiscard]] bool verify(const MachineRegisterInfo *MRI = nullptr) const;
#endif

  private:
    /// Appends @p Range to SubRanges list.
    void appendSubRange(SubRange *Range) {
      Range->Next = SubRanges;
      SubRanges = Range;
    }

    /// Free memory held by SubRange.
    void freeSubRange(SubRange *S);
  };

  /// Write subrange \p SR to stream \p OS.
  ///
  /// \param OS Output stream.
  /// \param SR Subrange to print.
  /// \return Reference to \p OS.
  inline raw_ostream &operator<<(raw_ostream &OS,
                                 const LiveInterval::SubRange &SR) {
    SR.print(OS);
    return OS;
  }

  /// Write live interval \p LI to stream \p OS.
  ///
  /// \param OS Output stream.
  /// \param LI Live interval to print.
  /// \return Reference to \p OS.
  inline raw_ostream &operator<<(raw_ostream &OS, const LiveInterval &LI) {
    LI.print(OS);
    return OS;
  }

  /// Write live-range segment \p S to stream \p OS.
  ///
  /// \param OS Output stream.
  /// \param S Segment to print.
  /// \return Reference to \p OS.
  LLVM_ABI raw_ostream &operator<<(raw_ostream &OS,
                                   const LiveRange::Segment &S);

  /// Return true if slot index \p V is before segment \p S.
  ///
  /// \param V Slot index on the left-hand side.
  /// \param S Segment on the right-hand side.
  /// \return True if slot index \p V is before segment \p S.
  inline bool operator<(SlotIndex V, const LiveRange::Segment &S) {
    return V < S.start;
  }

  /// Return true if segment \p S starts before slot index \p V.
  ///
  /// \param S Segment on the left-hand side.
  /// \param V Slot index on the right-hand side.
  /// \return True if segment \p S starts before slot index \p V.
  inline bool operator<(const LiveRange::Segment &S, SlotIndex V) {
    return S.start < V;
  }

  /// Helper class for performant LiveRange bulk updates.
  ///
  /// Calling LiveRange::addSegment() repeatedly can be expensive on large
  /// live ranges because segments after the insertion point may need to be
  /// shifted. The LiveRangeUpdater class can defer the shifting when adding
  /// many segments in order.
  ///
  /// The LiveRange will be in an invalid state until flush() is called.
  class LiveRangeUpdater {
    LiveRange *LR;
    SlotIndex LastStart;
    LiveRange::iterator WriteI;
    LiveRange::iterator ReadI;
    SmallVector<LiveRange::Segment, 16> Spills;
    void mergeSpills();

  public:
    /// Create a LiveRangeUpdater for adding segments to \p lr.
    ///
    /// LR will temporarily be in an invalid state until flush() is called.
    ///
    /// \param lr Destination live range to update, or null.
    LiveRangeUpdater(LiveRange *lr = nullptr) : LR(lr) {}

    /// Flush any pending updates on destruction.
    ~LiveRangeUpdater() { flush(); }

    /// Add segment \p S to LR, coalescing when possible.
    ///
    /// Behaves like LR.addSegment(). Segments should be added in increasing
    /// start order for best performance.
    ///
    /// \param S Segment to insert into the destination live range.
    LLVM_ABI void add(LiveRange::Segment S);

    /// Add a segment spanning [\p Start, \p End) with value \p VNI.
    ///
    /// \param Start Inclusive start index of the segment.
    /// \param End Exclusive end index of the segment.
    /// \param VNI Value number carried by the segment.
    void add(SlotIndex Start, SlotIndex End, VNInfo *VNI) {
      add(LiveRange::Segment(Start, End, VNI));
    }

    /// Return true if the LR is currently in an invalid state, and flush()
    /// needs to be called.
    ///
    /// \return True if LR is in an invalid state pending flush().
    bool isDirty() const { return LastStart.isValid(); }

    /// Flush the updater state to LR so it is valid and contains all added
    /// segments.
    LLVM_ABI void flush();

    /// Select a different destination live range.
    ///
    /// \param lr New destination live range.
    void setDest(LiveRange *lr) {
      if (LR != lr && isDirty())
        flush();
      LR = lr;
    }

    /// Get the current destination live range.
    ///
    /// \return The current destination live range.
    LiveRange *getDest() const { return LR; }

    /// Dump the updater state to the debug stream.
    LLVM_ABI void dump() const;
    /// Print the updater state to \p OS.
    ///
    /// \param OS Output stream.
    LLVM_ABI void print(raw_ostream &OS) const;
  };

  /// Write the LiveRangeUpdater \p X to stream \p OS.
  ///
  /// \param OS Output stream.
  /// \param X Updater whose pending state is printed.
  /// \return Reference to \p OS.
  inline raw_ostream &operator<<(raw_ostream &OS, const LiveRangeUpdater &X) {
    X.print(OS);
    return OS;
  }

  /// Helper that partitions VNInfos in a LiveInterval into connected components.
  ///
  /// ConnectedVNInfoEqClasses can divide VNInfos in a LiveInterval into
  /// equivalence classes of connected components. A LiveInterval that has
  /// multiple connected components can be broken into multiple LiveIntervals.
  ///
  /// Given a LiveInterval that may have multiple connected components, run:
  ///
  ///   unsigned numComps = ConEQ.Classify(LI);
  ///   if (numComps > 1) {
  ///     // allocate numComps-1 new LiveIntervals into LIS[1..]
  ///     ConEQ.Distribute(LIS);
  ///   }
  class ConnectedVNInfoEqClasses {
    LiveIntervals &LIS;
    IntEqClasses EqClass;

  public:
    /// Construct a classifier that uses live-interval analysis \p lis.
    ///
    /// \param lis LiveIntervals analysis used while classifying and distributing.
    explicit ConnectedVNInfoEqClasses(LiveIntervals &lis) : LIS(lis) {}

    /// Classify the values in \p LR into connected components.
    ///
    /// Returns the number of connected components.
    ///
    /// \param LR Live range whose value numbers are classified.
    /// \return The number of connected components.
    LLVM_ABI unsigned Classify(const LiveRange &LR);

    /// Return the equivalence class assigned to \p VNI.
    ///
    /// Classify creates equivalence classes numbered 0..N.
    ///
    /// \param VNI Value number whose equivalence class is requested.
    /// \return Equivalence class number assigned to \p VNI.
    unsigned getEqClass(const VNInfo *VNI) const { return EqClass[VNI->id]; }

    /// Distribute values in \p LI into a separate LiveInterval per component.
    ///
    /// LIV must have an empty LiveInterval for each additional connected
    /// component. The first connected component is left in \p LI.
    ///
    /// \param LI Live interval being split; retains the first component.
    /// \param LIV Array of empty LiveIntervals for the remaining components.
    /// \param MRI Machine register info updated for the distributed intervals.
    LLVM_ABI void Distribute(LiveInterval &LI, LiveInterval *LIV[],
                             MachineRegisterInfo &MRI);
  };

} // end namespace llvm

#endif // LLVM_CODEGEN_LIVEINTERVAL_H
