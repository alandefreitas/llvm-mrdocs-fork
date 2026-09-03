//===- llvm/ModuleSummaryIndex.h - Module Summary Index ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// @file
/// ModuleSummaryIndex.h This file contains the declarations the classes that
///  hold the module index and summary for function importing.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_MODULESUMMARYINDEX_H
#define LLVM_IR_MODULESUMMARYINDEX_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/InterleavedRange.h"
#include "llvm/Support/ScaledNumber.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace llvm {

template <class GraphType> struct GraphTraits;

namespace yaml {

template <typename T> struct MappingTraits;

} // end namespace yaml

/// Class to accumulate and hold information about a callee.
struct CalleeInfo {
  /// Relative hotness of calls to this callee.
  enum class HotnessType : uint8_t {
    Unknown = 0,   ///< Hotness is not known.
    Cold = 1,      ///< Call is cold.
    None = 2,      ///< Call has no special hotness.
    Hot = 3,       ///< Call is hot.
    Critical = 4   ///< Call is critically hot.
  };

  // The size of the bit-field might need to be adjusted if more values are
  // added to HotnessType enum.
  /// Encoded HotnessType for calls to this callee.
  uint32_t Hotness : 3;

  // True if at least one of the calls to the callee is a tail call.
  LLVM_PREFERRED_TYPE(bool)
  /// True if at least one call to this callee is a tail call.
  uint32_t HasTailCall : 1;

  /// Construct CalleeInfo with unknown hotness and no tail call.
  CalleeInfo()
      : Hotness(static_cast<uint32_t>(HotnessType::Unknown)),
        HasTailCall(false) {}
  /// Construct CalleeInfo with the given hotness and tail-call flag.
  /// \param Hotness Relative hotness of calls to this callee.
  /// \param HasTC Whether at least one call is a tail call.
  explicit CalleeInfo(HotnessType Hotness, bool HasTC)
      : Hotness(static_cast<uint32_t>(Hotness)), HasTailCall(HasTC) {}

  /// Raise stored hotness to at least \p OtherHotness.
  /// \param OtherHotness Hotness observed for another call edge.
  void updateHotness(const HotnessType OtherHotness) {
    Hotness = std::max(Hotness, static_cast<uint32_t>(OtherHotness));
  }

  /// Return true if at least one call to this callee is a tail call.
  /// @return True if at least one call to this callee is a tail call.
  bool hasTailCall() const { return HasTailCall; }

  /// Set whether at least one call to this callee is a tail call.
  /// \param HasTC New tail-call flag.
  void setHasTailCall(const bool HasTC) { HasTailCall = HasTC; }

  /// Return the relative hotness of calls to this callee.
  /// @return The relative hotness of calls to this callee.
  HotnessType getHotness() const { return HotnessType(Hotness); }
};

/// Return a printable name for hotness type \p HT.
/// \param HT Hotness enumeration value to name.
/// @return A printable name for hotness type \p HT.
inline const char *getHotnessName(CalleeInfo::HotnessType HT) {
  switch (HT) {
  case CalleeInfo::HotnessType::Unknown:
    return "unknown";
  case CalleeInfo::HotnessType::Cold:
    return "cold";
  case CalleeInfo::HotnessType::None:
    return "none";
  case CalleeInfo::HotnessType::Hot:
    return "hot";
  case CalleeInfo::HotnessType::Critical:
    return "critical";
  }
  llvm_unreachable("invalid hotness");
}

class GlobalValueSummary;

/// List of owned global value summary instances for one GUID.
using GlobalValueSummaryList = std::vector<std::unique_ptr<GlobalValueSummary>>;

/// Summary information for one GUID in the global value summary map.
struct alignas(8) GlobalValueSummaryInfo {
  /// Union of either a GlobalValue pointer or a summary name string.
  union NameOrGV {
    /// Construct an empty NameOrGV for the given HaveGVs mode.
    /// \param HaveGVs Whether summaries hold GlobalValue pointers vs names.
    NameOrGV(bool HaveGVs) {
      if (HaveGVs)
        GV = nullptr;
      else
        Name = "";
    }

    /// The GlobalValue corresponding to this summary, when IR is available.
    ///
    /// This is only used in per-module summaries and when the IR is available.
    /// E.g. when module analysis is being run, or when parsing both the IR and
    /// the summary from assembly.
    const GlobalValue *GV;

    /// Summary string representation owned by the bitcode module string table.
    ///
    /// This StringRef points to BC module string table and is valid until
    /// module data is stored in memory. This is guaranteed to happen until
    /// runThinLTOBackend function is called, so it is safe to use this field
    /// during thin link. This field is only valid if summary index was loaded
    /// from BC file.
    StringRef Name;
  } U; ///< Either a GlobalValue pointer or a name StringRef.

  /// Construct GlobalValueSummaryInfo for the given HaveGVs mode.
  /// \param HaveGVs Whether summaries hold GlobalValue pointers vs names.
  inline GlobalValueSummaryInfo(bool HaveGVs);

  /// Access a read-only list of global value summary structures for a
  /// particular value held in the GlobalValueMap.
  /// @return A read-only list of global value summaries for this GUID.
  ArrayRef<std::unique_ptr<GlobalValueSummary>> getSummaryList() const {
    return SummaryList;
  }

  /// Add a summary corresponding to a global value definition in a module with
  /// the corresponding GUID.
  /// \param Summary Summary instance to append for this GUID.
  inline void addSummary(std::unique_ptr<GlobalValueSummary> Summary);

  /// Verify that the HasLocal flag is consistent with the SummaryList. Should
  /// only be called prior to index-based internalization and promotion.
  inline void verifyLocal() const;

  /// Return true if any summary for this GUID has local linkage.
  /// @return True if any summary for this GUID has local linkage.
  bool hasLocal() const { return HasLocal; }

private:
  /// List of global value summary structures for a particular value held
  /// in the GlobalValueMap. Requires a vector in the case of multiple
  /// COMDAT values of the same name, weak symbols, locals of the same name when
  /// compiling without sufficient distinguishing path, or (theoretically) hash
  /// collisions. Each summary is from a different module.
  GlobalValueSummaryList SummaryList;

  /// True if the SummaryList contains at least one summary with local linkage.
  /// In most cases there should be only one, unless translation units with
  /// same-named locals were compiled without distinguishing path. And generally
  /// there should not be a mix of local and non-local summaries, because the
  /// GUID for a local is computed with the path prepended and a ';' delimiter.
  /// In extremely rare cases there could be a GUID hash collision. Having the
  /// flag saves having to walk through all summaries to prove the existence or
  /// not of any locals.
  /// NOTE: this flag is set when the index is built. It does not reflect
  /// index-based internalization and promotion decisions. Generally most
  /// index-based analysis occurs before then, but any users should assert that
  /// the withInternalizeAndPromote() flag is not set on the index.
  /// TODO: Replace checks in various ThinLTO analyses that loop through all
  /// summaries to handle the local case with a check of the flag.
  bool HasLocal : 1;
};

/// Map from global value GUID to corresponding summary structures.
///
/// Use a DenseMap for O(1) lookup and a std::deque for storage. std::deque
/// guarantees that pointers to elements are not invalidated by push_back,
/// which is required because ValueInfo stores a raw pointer to elements of
/// this container.
class GlobalValueSummaryMap {
public:
  /// GUID key type for the map.
  using key_type = GlobalValue::GUID;
  /// Mapped summary-info type for each GUID.
  using mapped_type = GlobalValueSummaryInfo;
  /// Key/value pair type stored in the map.
  using value_type = std::pair<key_type, mapped_type>;
  /// Mutable iterator over map entries.
  using iterator = std::deque<value_type>::iterator;
  /// Const iterator over map entries.
  using const_iterator = std::deque<value_type>::const_iterator;
  /// Size type for the map.
  using size_type = std::deque<value_type>::size_type;

private:
  /// Vector of pointers into Storage, used for key-sorted iteration.
  using SortedEntriesVec = SmallVector<const value_type *, 0>;

  DenseMap<key_type, unsigned> Map;
  std::deque<value_type> Storage;

public:
  /// Insert a new entry for \p Key if absent, constructing the mapped value.
  /// \param Key GUID to insert or look up.
  /// \param Args Constructor arguments forwarded to GlobalValueSummaryInfo.
  /// @return A pair of an iterator to the entry and whether a new entry was inserted.
  template <typename... Ts>
  std::pair<iterator, bool> try_emplace(key_type Key, Ts &&...Args) {
    auto Res = Map.try_emplace(Key, Storage.size());
    if (Res.second) {
      Storage.emplace_back(std::piecewise_construct, std::forward_as_tuple(Key),
                           std::forward_as_tuple(std::forward<Ts>(Args)...));
      return {std::prev(Storage.end()), true};
    }
    return {Storage.begin() + Res.first->second, false};
  }

  /// Return an iterator to the entry for \p Key, or end() if not found.
  /// \param Key GUID to look up.
  /// @return An iterator to the entry for \p Key, or end() if not found.
  iterator find(key_type Key) {
    auto It = Map.find(Key);
    return It == Map.end() ? Storage.end() : Storage.begin() + It->second;
  }

  /// Return a const iterator to the entry for \p Key, or end() if not found.
  /// \param Key GUID to look up.
  /// @return A const iterator to the entry for \p Key, or end() if not found.
  const_iterator find(key_type Key) const {
    auto It = Map.find(Key);
    return It == Map.end() ? Storage.end() : Storage.begin() + It->second;
  }

  /// Return an iterator to the beginning of the map.
  /// @return An iterator to the beginning of the map.
  iterator begin() { return Storage.begin(); }
  /// Return a const iterator to the beginning of the map.
  /// @return A const iterator to the beginning of the map.
  const_iterator begin() const { return Storage.begin(); }
  /// Return an iterator past the end of the map.
  /// @return An iterator past the end of the map.
  iterator end() { return Storage.end(); }
  /// Return a const iterator past the end of the map.
  /// @return A const iterator past the end of the map.
  const_iterator end() const { return Storage.end(); }
  /// Return the number of entries in the map.
  /// @return The number of entries in the map.
  size_type size() const { return Storage.size(); }
  /// Return true if the map contains no entries.
  /// @return True if the map contains no entries.
  bool empty() const { return Storage.empty(); }

  /// An owning range over the entries sorted by key, yielding each entry by
  /// reference.
  class SortedEntriesRange {
    SortedEntriesVec Entries;

  public:
    /// Iterator that yields map entries by reference via pointee_iterator.
    using iterator = pointee_iterator<SortedEntriesVec::const_iterator>;

    /// Construct a sorted range that owns the given entry pointers.
    /// \param Entries Pointers into the map storage, sorted by key.
    explicit SortedEntriesRange(SortedEntriesVec Entries)
        : Entries(std::move(Entries)) {}

    /// Return an iterator to the first sorted entry.
    /// @return An iterator to the first sorted entry.
    iterator begin() const { return iterator(Entries.begin()); }
    /// Return an iterator past the last sorted entry.
    /// @return An iterator past the last sorted entry.
    iterator end() const { return iterator(Entries.end()); }
    /// Return the number of entries in the sorted range.
    /// @return The number of entries in the sorted range.
    size_t size() const { return Entries.size(); }
    /// Return true if the sorted range is empty.
    /// @return True if the sorted range is empty.
    bool empty() const { return Entries.empty(); }
  };

  /// Return an owning range over the entries sorted by key. Storage is in
  /// insertion order; some serialization paths and tests rely on key-sorted
  /// iteration.
  /// @return An owning range over the entries sorted by key.
  SortedEntriesRange sortedRange() const {
    return SortedEntriesRange(getSortedEntries());
  }

private:
  SortedEntriesVec getSortedEntries() const {
    SortedEntriesVec Sorted;
    Sorted.reserve(Storage.size());
    for (const auto &E : Storage)
      Sorted.push_back(&E);
    llvm::sort(Sorted, [](const auto *A, const auto *B) {
      return A->first < B->first;
    });
    return Sorted;
  }
};

/// Type alias for the GUID-to-summary map used throughout the index.
using GlobalValueSummaryMapTy = GlobalValueSummaryMap;

/// Struct that holds a reference to a particular GUID in a global value
/// summary.
struct ValueInfo {
  /// Flag bits packed into RefAndFlags.
  enum Flags {
    HaveGV = 1,    ///< Ref points at a summary that stores a GlobalValue*.
    ReadOnly = 2,  ///< Value is known read-only.
    WriteOnly = 4  ///< Value is known write-only.
  };
  /// Pointer to the map entry plus HaveGV/ReadOnly/WriteOnly flag bits.
  PointerIntPair<const GlobalValueSummaryMapTy::value_type *, 3, int>
      RefAndFlags;

  /// Construct a null ValueInfo.
  ValueInfo() = default;
  /// Construct a ValueInfo referring to map entry \p R.
  /// \param HaveGVs Whether the index stores GlobalValue pointers.
  /// \param R Pointer to the GUID map entry, or null.
  ValueInfo(bool HaveGVs, const GlobalValueSummaryMapTy::value_type *R) {
    RefAndFlags.setPointer(R);
    RefAndFlags.setInt(HaveGVs);
  }

  /// Return true if this ValueInfo refers to a map entry.
  /// @return True if this ValueInfo refers to a map entry.
  explicit operator bool() const { return getRef(); }

  /// Return the GUID for this value.
  /// @return The GUID for this value.
  GlobalValue::GUID getGUID() const { return getRef()->first; }
  /// Return the GlobalValue pointer when HaveGVs is set.
  /// @return The GlobalValue pointer when HaveGVs is set.
  const GlobalValue *getValue() const {
    assert(haveGVs());
    return getRef()->second.U.GV;
  }

  /// Return the summary list for this value.
  /// @return The summary list for this value.
  ArrayRef<std::unique_ptr<GlobalValueSummary>> getSummaryList() const {
    return getRef()->second.getSummaryList();
  }

  /// Verify that the HasLocal flag matches the summary list.
  void verifyLocal() const { getRef()->second.verifyLocal(); }

  /// Return true if any summary for this value has local linkage.
  /// @return True if any summary for this value has local linkage.
  bool hasLocal() const { return getRef()->second.hasLocal(); }

  // Even if the index is built with GVs available, we may not have one for
  // summary entries synthesized for profiled indirect call targets.
  /// Return true if a name string is available for this value.
  /// @return True if a name string is available for this value.
  bool hasName() const { return !haveGVs() || getValue(); }

  /// Return the name of this value (from GV or the summary string).
  /// @return The name of this value (from GV or the summary string).
  StringRef name() const {
    assert(!haveGVs() || getRef()->second.U.GV);
    return haveGVs() ? getRef()->second.U.GV->getName()
                     : getRef()->second.U.Name;
  }

  /// Return true if this ValueInfo was built with GlobalValue pointers.
  /// @return True if this ValueInfo was built with GlobalValue pointers.
  bool haveGVs() const { return RefAndFlags.getInt() & HaveGV; }
  /// Return true if this value is known read-only.
  /// @return True if this value is known read-only.
  bool isReadOnly() const {
    assert(isValidAccessSpecifier());
    return RefAndFlags.getInt() & ReadOnly;
  }
  /// Return true if this value is known write-only.
  /// @return True if this value is known write-only.
  bool isWriteOnly() const {
    assert(isValidAccessSpecifier());
    return RefAndFlags.getInt() & WriteOnly;
  }
  /// Return the ReadOnly/WriteOnly access specifier bits.
  /// @return The ReadOnly/WriteOnly access specifier bits.
  unsigned getAccessSpecifier() const {
    assert(isValidAccessSpecifier());
    return RefAndFlags.getInt() & (ReadOnly | WriteOnly);
  }
  /// Return true if the access specifier bits are not both set.
  /// @return True if the access specifier bits are not both set.
  bool isValidAccessSpecifier() const {
    unsigned BadAccessMask = ReadOnly | WriteOnly;
    return (RefAndFlags.getInt() & BadAccessMask) != BadAccessMask;
  }
  /// Mark this value as read-only.
  void setReadOnly() {
    // We expect ro/wo attribute to set only once during
    // ValueInfo lifetime.
    assert(getAccessSpecifier() == 0);
    RefAndFlags.setInt(RefAndFlags.getInt() | ReadOnly);
  }
  /// Mark this value as write-only.
  void setWriteOnly() {
    assert(getAccessSpecifier() == 0);
    RefAndFlags.setInt(RefAndFlags.getInt() | WriteOnly);
  }

  /// Return the underlying GUID map entry pointer, or null.
  /// @return The underlying GUID map entry pointer, or null.
  const GlobalValueSummaryMapTy::value_type *getRef() const {
    return RefAndFlags.getPointer();
  }

  /// Returns the most constraining visibility among summaries. The
  /// visibilities, ordered from least to most constraining, are: default,
  /// protected and hidden.
  /// @return The most constraining visibility among summaries.
  LLVM_ABI GlobalValue::VisibilityTypes getELFVisibility() const;

  /// Checks if all summaries are DSO local (have the flag set). When DSOLocal
  /// propagation has been done, set the parameter to enable fast check.
  /// \param WithDSOLocalPropagation If true, use the post-propagation fast path.
  /// @return True if all summaries are DSO local (have the flag set).
  LLVM_ABI bool isDSOLocal(bool WithDSOLocalPropagation = false) const;

  /// Checks if all copies are eligible for auto-hiding (have flag set).
  /// @return True if all copies are eligible for auto-hiding (have flag set).
  LLVM_ABI bool canAutoHide() const;
};

/// Print \p VI to \p OS for debugging.
/// \param OS Stream to write to.
/// \param VI ValueInfo to print.
/// @return The output stream after printing.
inline raw_ostream &operator<<(raw_ostream &OS, const ValueInfo &VI) {
  OS << VI.getGUID();
  if (!VI.name().empty())
    OS << " (" << VI.name() << ")";
  return OS;
}

/// Return true if \p A and \p B refer to the same map entry.
/// \param A Left-hand operand.
/// \param B Right-hand operand.
/// @return True if the operands refer to the same map entry.
inline bool operator==(const ValueInfo &A, const ValueInfo &B) {
  assert(A.getRef() && B.getRef() &&
         "Need ValueInfo with non-null Ref for comparison");
  return A.getRef() == B.getRef();
}

/// Return true if \p A and \p B refer to different map entries.
/// \param A Left-hand operand.
/// \param B Right-hand operand.
/// @return True if the operands refer to different map entries.
inline bool operator!=(const ValueInfo &A, const ValueInfo &B) {
  assert(A.getRef() && B.getRef() &&
         "Need ValueInfo with non-null Ref for comparison");
  return A.getRef() != B.getRef();
}

/// Order ValueInfos by GUID.
/// \param A Left-hand operand.
/// \param B Right-hand operand.
/// @return True if A's GUID is less than B's GUID.
inline bool operator<(const ValueInfo &A, const ValueInfo &B) {
  assert(A.getRef() && B.getRef() &&
         "Need ValueInfo with non-null Ref to compare GUIDs");
  return A.getGUID() < B.getGUID();
}

/// DenseMapInfo specialization for ValueInfo.
template <> struct DenseMapInfo<ValueInfo> {
  /// Return true if \p L and \p R refer to the same map entry.
  /// \param L Left-hand operand.
  /// \param R Right-hand operand.
  /// @return True if \p L and \p R refer to the same map entry.
  static bool isEqual(ValueInfo L, ValueInfo R) {
    // We are not supposed to mix ValueInfo(s) with different HaveGVs flag
    // in a same container.
    assert(L.haveGVs() == R.haveGVs());
    return L.getRef() == R.getRef();
  }
  /// Return a hash for ValueInfo \p I based on its map entry pointer.
  /// \param I ValueInfo to hash.
  /// @return A hash for ValueInfo \p I based on its map entry pointer.
  static unsigned getHashValue(ValueInfo I) { return hash_value(I.getRef()); }
};

/// Full stack id and total profiled size for optional hinted size reporting.
///
/// Holds a pair of the full stack id (pre-trimming, from the full context in
/// the profile), and the associated total profiled size.
struct ContextTotalSize {
  /// Full (pre-trimmed) stack id from the profile context.
  uint64_t FullStackId;
  /// Total profiled size associated with this stack context.
  uint64_t TotalSize;
};

/// Summary of memprof callsite metadata.
struct CallsiteInfo {
  // Actual callee function.
  /// Value info for the actual callee function.
  ValueInfo Callee;

  // Used to record whole program analysis cloning decisions.
  // The ThinLTO backend will need to create as many clones as there are entries
  // in the vector (it is expected and should be confirmed that all such
  // summaries in the same FunctionSummary have the same number of entries).
  // Each index records version info for the corresponding clone of this
  // function. The value is the callee clone it calls (becomes the appended
  // suffix id). Index 0 is the original version, and a value of 0 calls the
  // original callee.
  /// Clone version ids for whole-program analysis cloning decisions.
  SmallVector<unsigned> Clones{0};

  // Represents stack ids in this context, recorded as indices into the
  // StackIds vector in the summary index, which in turn holds the full 64-bit
  // stack ids. This reduces memory as there are in practice far fewer unique
  // stack ids than stack id references.
  /// Indices into the index StackIds vector for this callsite context.
  SmallVector<unsigned> StackIdIndices;

  /// Construct callsite info with a callee and stack id indices.
  /// \param Callee Callee value info.
  /// \param StackIdIndices Indices into the index StackIds vector.
  CallsiteInfo(ValueInfo Callee, SmallVector<unsigned> StackIdIndices)
      : Callee(Callee), StackIdIndices(std::move(StackIdIndices)) {}
  /// Construct callsite info with callee, clones, and stack id indices.
  /// \param Callee Callee value info.
  /// \param Clones Clone version ids for this callsite.
  /// \param StackIdIndices Indices into the index StackIds vector.
  CallsiteInfo(ValueInfo Callee, SmallVector<unsigned> Clones,
               SmallVector<unsigned> StackIdIndices)
      : Callee(Callee), Clones(std::move(Clones)),
        StackIdIndices(std::move(StackIdIndices)) {}
};

/// Print memprof callsite summary \p SNI to \p OS.
/// \param OS Stream to write to.
/// \param SNI Callsite info to print.
/// @return The output stream after printing.
inline raw_ostream &operator<<(raw_ostream &OS, const CallsiteInfo &SNI) {
  OS << "Callee: " << SNI.Callee;
  OS << " Clones: " << llvm::interleaved(SNI.Clones);
  OS << " StackIds: " << llvm::interleaved(SNI.StackIdIndices);
  return OS;
}

/// Allocation type assigned to an allocation reached by a given context.
///
/// More can be added, now this is cold, notcold and hot.
/// Values should be powers of two so that they can be ORed, in particular to
/// track allocations that have different behavior with different calling
/// contexts.
enum class AllocationType : uint8_t {
  None = 0,     ///< No allocation type assigned.
  NotCold = 1,  ///< Allocation is not cold.
  Cold = 2,     ///< Allocation is cold.
  Hot = 4,      ///< Allocation is hot.
  All = 7       ///< OR of all allocation type bits; keep updated.
};

/// Summary of a single MIB in a memprof metadata on allocations.
struct MIBInfo {
  // The allocation type for this profiled context.
  /// Allocation type for this profiled context.
  AllocationType AllocType;

  // Represents stack ids in this context, recorded as indices into the
  // StackIds vector in the summary index, which in turn holds the full 64-bit
  // stack ids. This reduces memory as there are in practice far fewer unique
  // stack ids than stack id references.
  /// Indices into the index StackIds vector for this MIB context.
  SmallVector<unsigned> StackIdIndices;

  /// Construct MIB info from an allocation type and stack id indices.
  /// \param AllocType Allocation type for this profiled context.
  /// \param StackIdIndices Indices into the index StackIds vector.
  MIBInfo(AllocationType AllocType, SmallVector<unsigned> StackIdIndices)
      : AllocType(AllocType), StackIdIndices(std::move(StackIdIndices)) {}
};

/// Print MIB summary \p MIB to \p OS.
/// \param OS Stream to write to.
/// \param MIB MIB info to print.
/// @return The output stream after printing.
inline raw_ostream &operator<<(raw_ostream &OS, const MIBInfo &MIB) {
  OS << "AllocType " << (unsigned)MIB.AllocType;
  OS << " StackIds: " << llvm::interleaved(MIB.StackIdIndices);
  return OS;
}

/// Summary of memprof metadata on allocations.
struct AllocInfo {
  // Used to record whole program analysis cloning decisions.
  // The ThinLTO backend will need to create as many clones as there are entries
  // in the vector (it is expected and should be confirmed that all such
  // summaries in the same FunctionSummary have the same number of entries).
  // Each index records version info for the corresponding clone of this
  // function. The value is the allocation type of the corresponding allocation.
  // Index 0 is the original version. Before cloning, index 0 may have more than
  // one allocation type.
  /// Per-clone allocation type versions for whole-program cloning decisions.
  SmallVector<uint8_t> Versions;

  // Vector of MIBs in this memprof metadata.
  /// MIBs in this memprof metadata.
  std::vector<MIBInfo> MIBs;

  // If requested, keep track of full stack contexts and total profiled sizes
  // for each MIB. This will be a vector of the same length and order as the
  // MIBs vector, if non-empty. Note that each MIB in the summary can have
  // multiple of these as we trim the contexts when possible during matching.
  // For hinted size reporting we, however, want the original pre-trimmed full
  // stack context id for better correlation with the profile.
  /// Optional full stack contexts and sizes parallel to MIBs.
  std::vector<std::vector<ContextTotalSize>> ContextSizeInfos;

  /// Construct allocation info from a list of MIBs.
  /// \param MIBs Memprof MIB summaries for this allocation.
  AllocInfo(std::vector<MIBInfo> MIBs) : MIBs(std::move(MIBs)) {
    Versions.push_back(0);
  }
  /// Construct allocation info from versions and MIBs.
  /// \param Versions Per-clone allocation type versions.
  /// \param MIBs Memprof MIB summaries for this allocation.
  AllocInfo(SmallVector<uint8_t> Versions, std::vector<MIBInfo> MIBs)
      : Versions(std::move(Versions)), MIBs(std::move(MIBs)) {}
};

/// Print allocation summary \p AE to \p OS.
/// \param OS Stream to write to.
/// \param AE Allocation info to print.
/// @return The output stream after printing.
inline raw_ostream &operator<<(raw_ostream &OS, const AllocInfo &AE) {
  OS << "Versions: "
     << interleaved(map_range(AE.Versions, StaticCastTo<unsigned>));

  OS << " MIB:\n";
  for (auto &M : AE.MIBs)
    OS << "\t\t" << M << "\n";
  if (!AE.ContextSizeInfos.empty()) {
    OS << "\tContextSizeInfo per MIB:\n";
    for (auto Infos : AE.ContextSizeInfos) {
      OS << "\t\t";
      ListSeparator InfoLS;
      for (auto [FullStackId, TotalSize] : Infos)
        OS << InfoLS << "{ " << FullStackId << ", " << TotalSize << " }";
      OS << "\n";
    }
  }
  return OS;
}

/// Function and variable summary information to aid decisions and
/// implementation of importing.
class GlobalValueSummary {
public:
  /// Subclass discriminator (for dyn_cast<> et al.)
  enum SummaryKind : unsigned {
    AliasKind,     ///< Summary describes an alias.
    FunctionKind,  ///< Summary describes a function.
    GlobalVarKind  ///< Summary describes a global variable.
  };

  /// Whether a value should be imported as a definition or a declaration.
  enum ImportKind : unsigned {
    /// The global value definition corresponding to the summary should be
    /// imported from source module
    Definition = 0,

    /// When its definition doesn't exist in the destination module and not
    /// imported (e.g., function is too large to be inlined), the global value
    /// declaration corresponding to the summary should be imported, or the
    /// attributes from summary should be annotated on the function declaration.
    Declaration = 1,
  };

  /// Group flags (Linkage, NotEligibleToImport, etc.) as a bitfield.
  struct GVFlags {
    /// The linkage type of the associated global value.
    ///
    /// One use is to flag values that have local linkage types and need to
    /// have module identifier appended before placing into the combined
    /// index, to disambiguate from other values with the same name.
    /// In the future this will be used to update and optimize linkage
    /// types based on global summary-based analysis.
    unsigned Linkage : 4;

    /// Indicates the visibility.
    unsigned Visibility : 2;

    /// Indicate if the global value cannot be imported (e.g. it cannot
    /// be renamed or references something that can't be renamed).
    unsigned NotEligibleToImport : 1;

    /// Live-root / liveness flag for index-based analysis.
    ///
    /// In per-module summary, indicate that the global value must be considered
    /// a live root for index-based liveness analysis. Used for special LLVM
    /// values such as llvm.global_ctors that the linker does not know about.
    ///
    /// In combined summary, indicate that the global value is live.
    unsigned Live : 1;

    /// Indicates that the linker resolved the symbol to a definition from
    /// within the same linkage unit.
    unsigned DSOLocal : 1;

    /// Whether the value is eligible for auto-hiding via hidden visibility.
    ///
    /// In the per-module summary, indicates that the global value is
    /// linkonce_odr and global unnamed addr (so eligible for auto-hiding
    /// via hidden visibility). In the combined summary, indicates that the
    /// prevailing linkonce_odr copy can be auto-hidden via hidden visibility
    /// when it is upgraded to weak_odr in the backend. This is legal when
    /// all copies are eligible for auto-hiding (i.e. all copies were
    /// linkonce_odr global unnamed addr. If any copy is not (e.g. it was
    /// originally weak_odr, we cannot auto-hide the prevailing copy as it
    /// means the symbol was externally visible.
    unsigned CanAutoHide : 1;

    /// This field is written by the ThinLTO indexing step to postlink combined
    /// summary. The value is interpreted as 'ImportKind' enum defined above.
    unsigned ImportType : 1;

    /// This symbol was promoted. Thinlink stages need to be aware of this
    /// transition
    unsigned Promoted : 1;

    /// This field is written by the ThinLTO prelink stage to decide whether
    /// a particular static global value should be promoted or not.
    unsigned NoRenameOnPromotion : 1;

    /// Convenience constructor for the common global-value flag bits.
    /// \param Linkage Linkage type of the global value.
    /// \param Visibility Visibility of the global value.
    /// \param NotEligibleToImport Whether the value cannot be imported.
    /// \param Live Whether the value is a live root / is live.
    /// \param IsLocal Whether the linker resolved the symbol DSO-locally.
    /// \param CanAutoHide Whether the value can be auto-hidden.
    /// \param ImportType Whether to import as definition or declaration.
    /// \param NoRenameOnPromotion Whether promotion must not rename the value.
    explicit GVFlags(GlobalValue::LinkageTypes Linkage,
                     GlobalValue::VisibilityTypes Visibility,
                     bool NotEligibleToImport, bool Live, bool IsLocal,
                     bool CanAutoHide, ImportKind ImportType,
                     bool NoRenameOnPromotion)
        : Linkage(Linkage), Visibility(Visibility),
          NotEligibleToImport(NotEligibleToImport), Live(Live),
          DSOLocal(IsLocal), CanAutoHide(CanAutoHide),
          ImportType(static_cast<unsigned>(ImportType)), Promoted(false),
          NoRenameOnPromotion(NoRenameOnPromotion) {}
  };

private:
  /// Kind of summary for use in dyn_cast<> et al.
  SummaryKind Kind;

  GVFlags Flags;

  /// This is the hash of the name of the symbol in the original file. It is
  /// identical to the GUID for global symbols, but differs for local since the
  /// GUID includes the module level id in the hash.
  GlobalValue::GUID OriginalName = 0;

  /// Path of module IR containing value's definition, used to locate
  /// module during importing.
  ///
  /// This is only used during parsing of the combined index, or when
  /// parsing the per-module index for creation of the combined summary index,
  /// not during writing of the per-module index which doesn't contain a
  /// module path string table.
  StringRef ModulePath;

  /// List of values referenced by this global value's definition
  /// (either by the initializer of a global variable, or referenced
  /// from within a function). This does not include functions called, which
  /// are listed in the derived FunctionSummary object.
  /// We use SmallVector<ValueInfo, 0> instead of std::vector<ValueInfo> for its
  /// smaller memory footprint.
  SmallVector<ValueInfo, 0> RefEdgeList;

protected:
  /// Construct a summary of kind \p K with the given flags and references.
  /// \param K Subclass discriminator for this summary.
  /// \param Flags Common global-value flags.
  /// \param Refs Referenced value infos from the definition.
  GlobalValueSummary(SummaryKind K, GVFlags Flags,
                     SmallVectorImpl<ValueInfo> &&Refs)
      : Kind(K), Flags(Flags), RefEdgeList(std::move(Refs)) {
    assert((K != AliasKind || Refs.empty()) &&
           "Expect no references for AliasSummary");
  }

public:
  /// Destroy this global value summary.
  virtual ~GlobalValueSummary() = default;

  /// Returns the hash of the original name, it is identical to the GUID for
  /// externally visible symbols, but not for local ones.
  /// @return The hash of the original name, it is identical to the GUID for externally visible symbols, but not for local ones.
  GlobalValue::GUID getOriginalName() const { return OriginalName; }

  /// Initialize the original name hash in this summary.
  /// \param Name Original-name GUID hash to store.
  void setOriginalName(GlobalValue::GUID Name) { OriginalName = Name; }

  /// Which kind of summary subclass this is.
  /// @return Which kind of summary subclass this is.
  SummaryKind getSummaryKind() const { return Kind; }

  /// Set the path to the module containing this function, for use in
  /// the combined index.
  /// \param ModPath Path of the module that defines this value.
  void setModulePath(StringRef ModPath) { ModulePath = ModPath; }

  /// Get the path to the module containing this function.
  /// @return The path to the module containing this function.
  StringRef modulePath() const { return ModulePath; }

  /// Get the flags for this GlobalValue (see \p struct GVFlags).
  /// @return The flags for this GlobalValue (see \p struct GVFlags).
  GVFlags flags() const { return Flags; }

  /// Return linkage type recorded for this global value.
  /// @return Linkage type recorded for this global value.
  GlobalValue::LinkageTypes linkage() const {
    return static_cast<GlobalValue::LinkageTypes>(Flags.Linkage);
  }

  /// Return true if this symbol was promoted from local linkage.
  /// @return True if this symbol was promoted from local linkage.
  bool wasPromoted() const { return Flags.Promoted; }

  /// Promote this local symbol to external linkage and mark it promoted.
  void promote() {
    assert(GlobalValue::isLocalLinkage(linkage()) &&
           "unexpected (re-)promotion of non-local symbol");
    assert(!Flags.Promoted);
    Flags.Promoted = true;
    Flags.Linkage = GlobalValue::LinkageTypes::ExternalLinkage;
  }

  /// Sets the linkage to the value determined by global summary-based
  /// optimization. Will be applied in the ThinLTO backends.
  /// \param Linkage New linkage type to record.
  void setLinkage(GlobalValue::LinkageTypes Linkage) {
    assert(!wasPromoted());
    assert(!GlobalValue::isExternalLinkage(Linkage) && "use `promote` instead");
    Flags.Linkage = Linkage;
  }

  /// Set external linkage for testing without going through promote().
  void setExternalLinkageForTest() {
    Flags.Linkage = GlobalValue::LinkageTypes::ExternalLinkage;
  }

  /// Return true if this global value can't be imported.
  /// @return True if this global value can't be imported.
  bool notEligibleToImport() const { return Flags.NotEligibleToImport; }

  /// Return true if this global value is live.
  /// @return True if this global value is live.
  bool isLive() const { return Flags.Live; }

  /// Set whether this global value is live.
  /// \param Live New liveness flag.
  void setLive(bool Live) { Flags.Live = Live; }

  /// Set whether this symbol was resolved DSO-locally.
  /// \param Local New DSO-local flag.
  void setDSOLocal(bool Local) { Flags.DSOLocal = Local; }

  /// Return true if this symbol was resolved DSO-locally.
  /// @return True if this symbol was resolved DSO-locally.
  bool isDSOLocal() const { return Flags.DSOLocal; }

  /// Set whether this value can be auto-hidden.
  /// \param CanAutoHide New auto-hide eligibility flag.
  void setCanAutoHide(bool CanAutoHide) { Flags.CanAutoHide = CanAutoHide; }

  /// Return true if this value can be auto-hidden.
  /// @return True if this value can be auto-hidden.
  bool canAutoHide() const { return Flags.CanAutoHide; }

  /// Return true if this value should be imported as a declaration.
  /// @return True if this value should be imported as a declaration.
  bool shouldImportAsDecl() const {
    return Flags.ImportType == GlobalValueSummary::ImportKind::Declaration;
  }

  /// Set whether to import this value as a definition or declaration.
  /// \param IK Import kind to record.
  void setImportKind(ImportKind IK) { Flags.ImportType = IK; }

  /// Set whether promotion must not rename this static global.
  /// \param NoRenameOnPromotion New no-rename-on-promotion flag.
  void setNoRenameOnPromotion(bool NoRenameOnPromotion) {
    Flags.NoRenameOnPromotion = NoRenameOnPromotion;
  }

  /// Return true if promotion must not rename this static global.
  /// @return True if promotion must not rename this static global.
  bool noRenameOnPromotion() const { return Flags.NoRenameOnPromotion; }

  /// Return whether this value should be imported as definition or declaration.
  /// @return Whether this value should be imported as definition or declaration.
  GlobalValueSummary::ImportKind importType() const {
    return static_cast<ImportKind>(Flags.ImportType);
  }

  /// Return the visibility recorded for this global value.
  /// @return The visibility recorded for this global value.
  GlobalValue::VisibilityTypes getVisibility() const {
    return (GlobalValue::VisibilityTypes)Flags.Visibility;
  }
  /// Set the visibility recorded for this global value.
  /// \param Vis New visibility to record.
  void setVisibility(GlobalValue::VisibilityTypes Vis) {
    Flags.Visibility = (unsigned)Vis;
  }

  /// Flag that this global value cannot be imported.
  void setNotEligibleToImport() { Flags.NotEligibleToImport = true; }

  /// Return the list of values referenced by this global value definition.
  /// @return The list of values referenced by this global value definition.
  ArrayRef<ValueInfo> refs() const { return RefEdgeList; }

  /// If this is an alias summary, returns the summary of the aliased object (a
  /// global variable or function), otherwise returns itself.
  /// @return The aliasee summary if this is an alias; otherwise this summary.
  GlobalValueSummary *getBaseObject();
  const GlobalValueSummary *getBaseObject() const;

  friend class ModuleSummaryIndex;
};

/// Construct GlobalValueSummaryInfo for the given HaveGVs mode.
/// \param HaveGVs Whether summaries hold GlobalValue pointers vs names.
GlobalValueSummaryInfo::GlobalValueSummaryInfo(bool HaveGVs)
    : U(HaveGVs), HasLocal(false) {}

/// Append \p Summary and update HasLocal if it has local linkage.
/// \param Summary Summary instance to append for this GUID.
void GlobalValueSummaryInfo::addSummary(
    std::unique_ptr<GlobalValueSummary> Summary) {
  if (GlobalValue::isLocalLinkage(Summary->linkage()))
    HasLocal = true;
  return SummaryList.push_back(std::move(Summary));
}

void GlobalValueSummaryInfo::verifyLocal() const {
  assert(HasLocal ==
         llvm::any_of(SummaryList,
                      [](const std::unique_ptr<GlobalValueSummary> &Summary) {
                        return GlobalValue::isLocalLinkage(Summary->linkage());
                      }));
}

/// Alias summary information.
class AliasSummary : public GlobalValueSummary {
  ValueInfo AliaseeValueInfo;

  /// This is the Aliasee in the same module as alias (could get from VI, trades
  /// memory for time). Note that this pointer may be null (and the value info
  /// empty) when we have a distributed index where the alias is being imported
  /// (as a copy of the aliasee), but the aliasee is not.
  GlobalValueSummary *AliaseeSummary = nullptr;

public:
  /// Construct an alias summary with the given flags.
  /// \param Flags Common global-value flags for the alias.
  AliasSummary(GVFlags Flags)
      : GlobalValueSummary(AliasKind, Flags, SmallVector<ValueInfo, 0>{}) {}

  /// Check if this is an alias summary.
  /// \param GVS Summary to test.
  /// @return Check if this is an alias summary.
  static bool classof(const GlobalValueSummary *GVS) {
    return GVS->getSummaryKind() == AliasKind;
  }

  /// Set the aliasee ValueInfo and summary for this alias.
  /// \param AliaseeVI Value info for the aliasee.
  /// \param Aliasee Summary of the aliasee in the same module, if available.
  void setAliasee(ValueInfo &AliaseeVI, GlobalValueSummary *Aliasee) {
    AliaseeValueInfo = AliaseeVI;
    AliaseeSummary = Aliasee;
  }

  /// Return true if this alias has an associated aliasee summary.
  /// @return True if this alias has an associated aliasee summary.
  bool hasAliasee() const {
    assert(!!AliaseeSummary == (AliaseeValueInfo &&
                                !AliaseeValueInfo.getSummaryList().empty()) &&
           "Expect to have both aliasee summary and summary list or neither");
    return !!AliaseeSummary;
  }

  /// Return the const summary of the aliasee.
  /// @return The const summary of the aliasee.
  const GlobalValueSummary &getAliasee() const {
    assert(AliaseeSummary && "Unexpected missing aliasee summary");
    return *AliaseeSummary;
  }

  /// Return the mutable summary of the aliasee.
  /// @return The mutable summary of the aliasee.
  GlobalValueSummary &getAliasee() {
    return const_cast<GlobalValueSummary &>(
                         static_cast<const AliasSummary *>(this)->getAliasee());
  }
  /// Return the ValueInfo for the aliasee.
  /// @return The ValueInfo for the aliasee.
  ValueInfo getAliaseeVI() const {
    assert(AliaseeValueInfo && "Unexpected missing aliasee");
    return AliaseeValueInfo;
  }
  /// Return the GUID of the aliasee.
  /// @return The GUID of the aliasee.
  GlobalValue::GUID getAliaseeGUID() const {
    assert(AliaseeValueInfo && "Unexpected missing aliasee");
    return AliaseeValueInfo.getGUID();
  }
};

/// Return the base object summary for this value (aliasee if this is an alias).
/// @return The base object summary for this value (aliasee if this is an alias).
const inline GlobalValueSummary *GlobalValueSummary::getBaseObject() const {
  if (auto *AS = dyn_cast<AliasSummary>(this))
    return &AS->getAliasee();
  return this;
}

/// Return the mutable base object summary for this value.
/// @return The mutable base object summary for this value.
inline GlobalValueSummary *GlobalValueSummary::getBaseObject() {
  if (auto *AS = dyn_cast<AliasSummary>(this))
    return &AS->getAliasee();
  return this;
}

/// Function summary information to aid decisions and implementation of
/// importing.
class FunctionSummary : public GlobalValueSummary {
public:
  /// <CalleeValueInfo, CalleeInfo> call edge pair.
  using EdgeTy = std::pair<ValueInfo, CalleeInfo>;

  /// Types for -force-summary-edges-cold debugging option.
  enum ForceSummaryHotnessType : unsigned {
    FSHT_None,          ///< Do not force edge hotness.
    FSHT_AllNonCritical, ///< Force all non-critical edges cold.
    FSHT_All            ///< Force all edges cold.
  };

  /// An identifier for a virtual function (type GUID plus vtable offset).
  ///
  /// This contains the type identifier represented as a GUID and the offset
  /// from the address point to the virtual function pointer, where "address
  /// point" is as defined in the Itanium ABI:
  /// https://itanium-cxx-abi.github.io/cxx-abi/abi.html#vtable-general
  struct VFuncId {
    /// GUID of the type identifier for this virtual function.
    GlobalValue::GUID GUID;
    /// Offset from the address point to the virtual function pointer.
    uint64_t Offset;
  };

  /// A specification for a virtual function call with all constant integer
  /// arguments. This is used to perform virtual constant propagation on the
  /// summary.
  struct ConstVCall {
    /// Virtual function identifier for the call.
    VFuncId VFunc;
    /// Constant integer arguments passed to the virtual call.
    std::vector<uint64_t> Args;
  };

  /// All type identifier related information. Because these fields are
  /// relatively uncommon we only allocate space for them if necessary.
  struct TypeIdInfo {
    /// List of type identifiers used by this function in llvm.type.test
    /// intrinsics referenced by something other than an llvm.assume intrinsic,
    /// represented as GUIDs.
    std::vector<GlobalValue::GUID> TypeTests;

    /// Virtual calls via llvm.assume(llvm.type.test) without all-constant args.
    std::vector<VFuncId> TypeTestAssumeVCalls;
    /// Virtual calls via llvm.type.checked.load without all-constant args.
    std::vector<VFuncId> TypeCheckedLoadVCalls;

    /// Virtual calls via llvm.assume(llvm.type.test) with all-constant args.
    std::vector<ConstVCall> TypeTestAssumeConstVCalls;
    /// Virtual calls via llvm.type.checked.load with all-constant args.
    std::vector<ConstVCall> TypeCheckedLoadConstVCalls;
  };

  /// Flags specific to function summaries.
  struct FFlags {
    // Function attribute flags. Used to track if a function accesses memory,
    // recurses or aliases.
    unsigned ReadNone : 1; ///< Function does not access memory.
    unsigned ReadOnly : 1; ///< Function only reads memory.
    unsigned NoRecurse : 1; ///< Function does not recurse.
    unsigned ReturnDoesNotAlias : 1; ///< Function return does not alias.

    // Indicate if the global value cannot be inlined.
    unsigned NoInline : 1; ///< Function must not be inlined.
    // Indicate if function should be always inlined.
    unsigned AlwaysInline : 1; ///< Function should always be inlined.
    // Indicate if function never raises an exception. Can be modified during
    // thinlink function attribute propagation
    unsigned NoUnwind : 1; ///< Function never raises an exception.
    // Indicate if function contains instructions that mayThrow
    unsigned MayThrow : 1; ///< Function may throw an exception.

    // If there are calls to unknown targets (e.g. indirect)
    unsigned HasUnknownCall : 1; ///< Function has calls to unknown targets.

    // Indicate if a function must be an unreachable function.
    //
    // This bit is sufficient but not necessary;
    // if this bit is on, the function must be regarded as unreachable;
    // if this bit is off, the function might be reachable or unreachable.
    unsigned MustBeUnreachable : 1; ///< Function must be unreachable.

    /// Intersect these flags with \p RHS in place.
    /// \param RHS Flags to AND with this set.
    /// @return A reference to this FFlags after the bitwise AND.
    FFlags &operator&=(const FFlags &RHS) {
      this->ReadNone &= RHS.ReadNone;
      this->ReadOnly &= RHS.ReadOnly;
      this->NoRecurse &= RHS.NoRecurse;
      this->ReturnDoesNotAlias &= RHS.ReturnDoesNotAlias;
      this->NoInline &= RHS.NoInline;
      this->AlwaysInline &= RHS.AlwaysInline;
      this->NoUnwind &= RHS.NoUnwind;
      this->MayThrow &= RHS.MayThrow;
      this->HasUnknownCall &= RHS.HasUnknownCall;
      this->MustBeUnreachable &= RHS.MustBeUnreachable;
      return *this;
    }

    /// Return true if any flag bit is set.
    /// @return True if any flag bit is set.
    bool anyFlagSet() {
      return this->ReadNone | this->ReadOnly | this->NoRecurse |
             this->ReturnDoesNotAlias | this->NoInline | this->AlwaysInline |
             this->NoUnwind | this->MayThrow | this->HasUnknownCall |
             this->MustBeUnreachable;
    }

    /// Format these flags as a human-readable string.
    /// @return A string representation of the function flags.
    operator std::string() {
      std::string Output;
      raw_string_ostream OS(Output);
      OS << "funcFlags: (";
      OS << "readNone: " << this->ReadNone;
      OS << ", readOnly: " << this->ReadOnly;
      OS << ", noRecurse: " << this->NoRecurse;
      OS << ", returnDoesNotAlias: " << this->ReturnDoesNotAlias;
      OS << ", noInline: " << this->NoInline;
      OS << ", alwaysInline: " << this->AlwaysInline;
      OS << ", noUnwind: " << this->NoUnwind;
      OS << ", mayThrow: " << this->MayThrow;
      OS << ", hasUnknownCall: " << this->HasUnknownCall;
      OS << ", mustBeUnreachable: " << this->MustBeUnreachable;
      OS << ")";
      return Output;
    }
  };

  /// Describes the uses of a parameter by the function.
  struct ParamAccess {
    /// Bit width used for ConstantRange offset sets.
    static constexpr uint32_t RangeWidth = 64;

    /// Describes how a parameter value is passed to a callee.
    ///
    /// Specifies the call's target, the value's parameter number, and the
    /// possible range of offsets from the beginning of the value that are
    /// passed.
    struct Call {
      /// Parameter number of the value in the callee.
      uint64_t ParamNo = 0;
      /// Callee that receives the value.
      ValueInfo Callee;
      /// Possible byte-offset range of the value passed to the callee.
      ConstantRange Offsets{/*BitWidth=*/RangeWidth, /*isFullSet=*/true};

      /// Construct an empty call-parameter access record.
      Call() = default;
      /// Construct a call-parameter access record.
      /// \param ParamNo Parameter number in the callee.
      /// \param Callee Callee ValueInfo.
      /// \param Offsets Possible offsets of the passed value.
      Call(uint64_t ParamNo, ValueInfo Callee, const ConstantRange &Offsets)
          : ParamNo(ParamNo), Callee(Callee), Offsets(Offsets) {}
    };

    /// Parameter number in this function.
    uint64_t ParamNo = 0;
    /// Byte offsets from the parameter pointer accessed by the function.
    ///
    /// In the per-module summary, it only includes accesses made by the
    /// function instructions. In the combined summary, it also includes
    /// accesses by nested function calls.
    ConstantRange Use{/*BitWidth=*/RangeWidth, /*isFullSet=*/true};
    /// Per-callee byte offsets applied to this pointer parameter before calls.
    ///
    /// In the per-module summary, it summarizes the byte offset applied to each
    /// pointer parameter before passing to each corresponding callee.
    /// In the combined summary, it's empty and information is propagated by
    /// inter-procedural analysis and applied to the Use field.
    std::vector<Call> Calls;

    /// Construct an empty parameter access record.
    ParamAccess() = default;
    /// Construct a parameter access record.
    /// \param ParamNo Parameter number in this function.
    /// \param Use Accessed byte-offset range from the parameter pointer.
    ParamAccess(uint64_t ParamNo, const ConstantRange &Use)
        : ParamNo(ParamNo), Use(Use) {}
  };

  /// Create an empty FunctionSummary (with specified call edges).
  /// Used to represent external nodes and the dummy root node.
  /// \param Edges Call-graph edges for the dummy summary.
  /// @return Create an empty FunctionSummary (with specified call edges).
  static FunctionSummary
  makeDummyFunctionSummary(SmallVectorImpl<FunctionSummary::EdgeTy> &&Edges) {
    return FunctionSummary(
        FunctionSummary::GVFlags(
            GlobalValue::LinkageTypes::AvailableExternallyLinkage,
            GlobalValue::DefaultVisibility,
            /*NotEligibleToImport=*/true, /*Live=*/true, /*IsLocal=*/false,
            /*CanAutoHide=*/false, GlobalValueSummary::ImportKind::Definition,
            /*NoRenameOnPromotion=*/false),
        /*NumInsts=*/0, FunctionSummary::FFlags{}, SmallVector<ValueInfo, 0>(),
        std::move(Edges), std::vector<GlobalValue::GUID>(),
        std::vector<FunctionSummary::VFuncId>(),
        std::vector<FunctionSummary::VFuncId>(),
        std::vector<FunctionSummary::ConstVCall>(),
        std::vector<FunctionSummary::ConstVCall>(),
        std::vector<FunctionSummary::ParamAccess>(),
        std::vector<CallsiteInfo>(), std::vector<AllocInfo>());
  }

  /// A dummy node to reference external functions that aren't in the index
  LLVM_ABI static FunctionSummary ExternalNode;

private:
  /// Number of instructions (ignoring debug instructions, e.g.) computed
  /// during the initial compile step when the summary index is first built.
  unsigned InstCount;

  /// Function summary specific flags.
  FFlags FunFlags;

  /// List of <CalleeValueInfo, CalleeInfo> call edge pairs from this function.
  /// We use SmallVector<ValueInfo, 0> instead of std::vector<ValueInfo> for its
  /// smaller memory footprint.
  SmallVector<EdgeTy, 0> CallGraphEdgeList;

  std::unique_ptr<TypeIdInfo> TIdInfo;

  /// Uses for every parameter to this function.
  using ParamAccessesTy = std::vector<ParamAccess>;
  std::unique_ptr<ParamAccessesTy> ParamAccesses;

  /// Optional list of memprof callsite metadata summaries. The correspondence
  /// between the callsite summary and the callsites in the function is implied
  /// by the order in the vector (and can be validated by comparing the stack
  /// ids in the CallsiteInfo to those in the instruction callsite metadata).
  /// As a memory savings optimization, we only create these for the prevailing
  /// copy of a symbol when creating the combined index during LTO.
  using CallsitesTy = std::vector<CallsiteInfo>;
  std::unique_ptr<CallsitesTy> Callsites;

  /// Optional list of allocation memprof metadata summaries. The correspondence
  /// between the alloc memprof summary and the allocation callsites in the
  /// function is implied by the order in the vector (and can be validated by
  /// comparing the stack ids in the AllocInfo to those in the instruction
  /// memprof metadata).
  /// As a memory savings optimization, we only create these for the prevailing
  /// copy of a symbol when creating the combined index during LTO.
  using AllocsTy = std::vector<AllocInfo>;
  std::unique_ptr<AllocsTy> Allocs;

public:
  /// Construct a function summary from the given analysis results.
  /// \param Flags Common global-value flags for the function.
  /// \param NumInsts Instruction count recorded for this function.
  /// \param FunFlags Function-specific summary flags.
  /// \param Refs Non-call references from this function.
  /// \param CGEdges Call-graph edges from this function.
  /// \param TypeTests Type identifier GUIDs used by llvm.type.test.
  /// \param TypeTestAssumeVCalls Virtual calls via assume(type.test).
  /// \param TypeCheckedLoadVCalls Virtual calls via type.checked.load.
  /// \param TypeTestAssumeConstVCalls Assume(type.test) calls with const args.
  /// \param TypeCheckedLoadConstVCalls Type.checked.load calls with const args.
  /// \param Params Per-parameter access summaries.
  /// \param CallsiteList Memprof callsite metadata summaries.
  /// \param AllocList Memprof allocation metadata summaries.
  FunctionSummary(GVFlags Flags, unsigned NumInsts, FFlags FunFlags,
                  SmallVectorImpl<ValueInfo> &&Refs,
                  SmallVectorImpl<EdgeTy> &&CGEdges,
                  std::vector<GlobalValue::GUID> TypeTests,
                  std::vector<VFuncId> TypeTestAssumeVCalls,
                  std::vector<VFuncId> TypeCheckedLoadVCalls,
                  std::vector<ConstVCall> TypeTestAssumeConstVCalls,
                  std::vector<ConstVCall> TypeCheckedLoadConstVCalls,
                  std::vector<ParamAccess> Params, CallsitesTy CallsiteList,
                  AllocsTy AllocList)
      : GlobalValueSummary(FunctionKind, Flags, std::move(Refs)),
        InstCount(NumInsts), FunFlags(FunFlags),
        CallGraphEdgeList(std::move(CGEdges)) {
    if (!TypeTests.empty() || !TypeTestAssumeVCalls.empty() ||
        !TypeCheckedLoadVCalls.empty() || !TypeTestAssumeConstVCalls.empty() ||
        !TypeCheckedLoadConstVCalls.empty())
      TIdInfo = std::make_unique<TypeIdInfo>(
          TypeIdInfo{std::move(TypeTests), std::move(TypeTestAssumeVCalls),
                     std::move(TypeCheckedLoadVCalls),
                     std::move(TypeTestAssumeConstVCalls),
                     std::move(TypeCheckedLoadConstVCalls)});
    if (!Params.empty())
      ParamAccesses = std::make_unique<ParamAccessesTy>(std::move(Params));
    if (!CallsiteList.empty())
      Callsites = std::make_unique<CallsitesTy>(std::move(CallsiteList));
    if (!AllocList.empty())
      Allocs = std::make_unique<AllocsTy>(std::move(AllocList));
  }
  /// Return counts of readonly and writeonly references in RefEdgeList.
  /// @return Counts of readonly and writeonly references in RefEdgeList.
  LLVM_ABI std::pair<unsigned, unsigned> specialRefCounts() const;

  /// Check if this is a function summary.
  /// \param GVS Summary to test.
  /// @return Check if this is a function summary.
  static bool classof(const GlobalValueSummary *GVS) {
    return GVS->getSummaryKind() == FunctionKind;
  }

  /// Get function summary flags.
  /// @return Function summary flags.
  FFlags fflags() const { return FunFlags; }

  /// Mark this function as non-recursive.
  void setNoRecurse() { FunFlags.NoRecurse = true; }

  /// Mark this function as never unwinding.
  void setNoUnwind() { FunFlags.NoUnwind = true; }

  /// Get the instruction count recorded for this function.
  /// @return The instruction count recorded for this function.
  unsigned instCount() const { return InstCount; }

  /// Return the list of <CalleeValueInfo, CalleeInfo> pairs.
  /// @return The list of <CalleeValueInfo, CalleeInfo> pairs.
  ArrayRef<EdgeTy> calls() const { return CallGraphEdgeList; }

  /// Return a mutable reference to the call-graph edge list.
  /// @return A mutable reference to the call-graph edge list.
  SmallVector<EdgeTy, 0> &mutableCalls() { return CallGraphEdgeList; }

  /// Append call-graph edge \p E to this function summary.
  /// \param E Callee ValueInfo and CalleeInfo pair to add.
  void addCall(EdgeTy E) { CallGraphEdgeList.push_back(E); }

  /// Returns the list of type identifiers used by this function in
  /// llvm.type.test intrinsics other than by an llvm.assume intrinsic,
  /// represented as GUIDs.
  /// @return The list of type identifiers used by this function in llvm.type.test intrinsics other than by an llvm.assume intrinsic, represented as GUIDs.
  ArrayRef<GlobalValue::GUID> type_tests() const {
    if (TIdInfo)
      return TIdInfo->TypeTests;
    return {};
  }

  /// Returns the list of virtual calls made by this function using
  /// llvm.assume(llvm.type.test) intrinsics that do not have all constant
  /// integer arguments.
  /// @return The list of virtual calls made by this function using llvm.assume(llvm.type.test) intrinsics that do not have all constant integer arguments.
  ArrayRef<VFuncId> type_test_assume_vcalls() const {
    if (TIdInfo)
      return TIdInfo->TypeTestAssumeVCalls;
    return {};
  }

  /// Returns the list of virtual calls made by this function using
  /// llvm.type.checked.load intrinsics that do not have all constant integer
  /// arguments.
  /// @return The list of virtual calls made by this function using llvm.type.checked.load intrinsics that do not have all constant integer arguments.
  ArrayRef<VFuncId> type_checked_load_vcalls() const {
    if (TIdInfo)
      return TIdInfo->TypeCheckedLoadVCalls;
    return {};
  }

  /// Returns the list of virtual calls made by this function using
  /// llvm.assume(llvm.type.test) intrinsics with all constant integer
  /// arguments.
  /// @return The list of virtual calls made by this function using llvm.assume(llvm.type.test) intrinsics with all constant integer arguments.
  ArrayRef<ConstVCall> type_test_assume_const_vcalls() const {
    if (TIdInfo)
      return TIdInfo->TypeTestAssumeConstVCalls;
    return {};
  }

  /// Returns the list of virtual calls made by this function using
  /// llvm.type.checked.load intrinsics with all constant integer arguments.
  /// @return The list of virtual calls made by this function using llvm.type.checked.load intrinsics with all constant integer arguments.
  ArrayRef<ConstVCall> type_checked_load_const_vcalls() const {
    if (TIdInfo)
      return TIdInfo->TypeCheckedLoadConstVCalls;
    return {};
  }

  /// Returns the list of known uses of pointer parameters.
  /// @return The list of known uses of pointer parameters.
  ArrayRef<ParamAccess> paramAccesses() const {
    if (ParamAccesses)
      return *ParamAccesses;
    return {};
  }

  /// Sets the list of known uses of pointer parameters.
  /// \param NewParams Replacement parameter-access summaries.
  void setParamAccesses(std::vector<ParamAccess> NewParams) {
    if (NewParams.empty())
      ParamAccesses.reset();
    else if (ParamAccesses)
      *ParamAccesses = std::move(NewParams);
    else
      ParamAccesses = std::make_unique<ParamAccessesTy>(std::move(NewParams));
  }

  /// Add a type test to the summary. This is used by WholeProgramDevirt if we
  /// were unable to devirtualize a checked call.
  /// \param Guid GUID of the type identifier referenced by the type test.
  void addTypeTest(GlobalValue::GUID Guid) {
    if (!TIdInfo)
      TIdInfo = std::make_unique<TypeIdInfo>();
    TIdInfo->TypeTests.push_back(Guid);
  }

  /// Return type-identifier info for this function, or null if none.
  /// @return Type-identifier info for this function, or null if none.
  const TypeIdInfo *getTypeIdInfo() const { return TIdInfo.get(); };

  /// Return memprof callsite summaries for this function, if any.
  /// @return Memprof callsite summaries for this function, if any.
  ArrayRef<CallsiteInfo> callsites() const {
    if (Callsites)
      return *Callsites;
    return {};
  }

  /// Return a mutable reference to the memprof callsite summaries.
  /// @return A mutable reference to the memprof callsite summaries.
  CallsitesTy &mutableCallsites() {
    assert(Callsites);
    return *Callsites;
  }

  /// Append memprof callsite summary \p Callsite.
  /// \param Callsite Callsite info to add.
  void addCallsite(CallsiteInfo &&Callsite) {
    if (!Callsites)
      Callsites = std::make_unique<CallsitesTy>();
    Callsites->push_back(std::move(Callsite));
  }

  /// Return memprof allocation summaries for this function, if any.
  /// @return Memprof allocation summaries for this function, if any.
  ArrayRef<AllocInfo> allocs() const {
    if (Allocs)
      return *Allocs;
    return {};
  }

  /// Append memprof allocation summary \p Alloc.
  /// \param Alloc Allocation info to add.
  void addAlloc(AllocInfo &&Alloc) {
    if (!Allocs)
      Allocs = std::make_unique<AllocsTy>();
    Allocs->push_back(std::move(Alloc));
  }

  /// Return a mutable reference to the memprof allocation summaries.
  /// @return A mutable reference to the memprof allocation summaries.
  AllocsTy &mutableAllocs() {
    assert(Allocs);
    return *Allocs;
  }

  friend struct GraphTraits<ValueInfo>;
};

/// DenseMapInfo specialization for FunctionSummary::VFuncId.
template <> struct DenseMapInfo<FunctionSummary::VFuncId> {
  /// Return true if \p L and \p R identify the same virtual function.
  /// \param L Left-hand operand.
  /// \param R Right-hand operand.
  /// @return True if \p L and \p R identify the same virtual function.
  static bool isEqual(FunctionSummary::VFuncId L, FunctionSummary::VFuncId R) {
    return L.GUID == R.GUID && L.Offset == R.Offset;
  }

  /// Return a hash for virtual function id \p I.
  /// \param I Virtual function id to hash.
  /// @return A hash for virtual function id \p I.
  static unsigned getHashValue(FunctionSummary::VFuncId I) { return I.GUID; }
};

/// DenseMapInfo specialization for FunctionSummary::ConstVCall.
template <> struct DenseMapInfo<FunctionSummary::ConstVCall> {
  /// Return true if \p L and \p R describe the same constant virtual call.
  /// \param L Left-hand operand.
  /// \param R Right-hand operand.
  /// @return True if \p L and \p R describe the same constant virtual call.
  static bool isEqual(FunctionSummary::ConstVCall L,
                      FunctionSummary::ConstVCall R) {
    return DenseMapInfo<FunctionSummary::VFuncId>::isEqual(L.VFunc, R.VFunc) &&
           L.Args == R.Args;
  }

  /// Return a hash for constant virtual call \p I.
  /// \param I Constant virtual call to hash.
  /// @return A hash for constant virtual call \p I.
  static unsigned getHashValue(FunctionSummary::ConstVCall I) {
    return I.VFunc.GUID;
  }
};

/// The ValueInfo and offset for a function within a vtable definition
/// initializer array.
struct VirtFuncOffset {
  /// Construct from a function ValueInfo and its offset in the vtable.
  /// \param VI Value info for the virtual function.
  /// \param Offset Byte offset of the function pointer in the vtable.
  VirtFuncOffset(ValueInfo VI, uint64_t Offset)
      : FuncVI(VI), VTableOffset(Offset) {}

  /// Value info for the virtual function referenced by the vtable.
  ValueInfo FuncVI;
  /// Byte offset of the function pointer within the vtable initializer.
  uint64_t VTableOffset;
};
/// List of functions referenced by a particular vtable definition.
using VTableFuncList = std::vector<VirtFuncOffset>;

/// Global variable summary information to aid decisions and
/// implementation of importing.
///
/// Global variable summary has two extra flag, telling if it is
/// readonly or writeonly. Both readonly and writeonly variables
/// can be optimized in the backed: readonly variables can be
/// const-folded, while writeonly vars can be completely eliminated
/// together with corresponding stores. We let both things happen
/// by means of internalizing such variables after ThinLTO import.
class GlobalVarSummary : public GlobalValueSummary {
private:
  /// For vtable definitions this holds the list of functions and
  /// their corresponding offsets within the initializer array.
  std::unique_ptr<VTableFuncList> VTableFuncs;

public:
  /// Global-variable-specific flags.
  struct GVarFlags {
    /// Construct global-variable summary flags.
    /// \param ReadOnly Whether the variable may be read-only.
    /// \param WriteOnly Whether the variable may be write-only.
    /// \param Constant Whether the variable is a compile-time constant.
    /// \param Vis VCall visibility metadata for vtable definitions.
    GVarFlags(bool ReadOnly, bool WriteOnly, bool Constant,
              GlobalObject::VCallVisibility Vis)
        : MaybeReadOnly(ReadOnly), MaybeWriteOnly(WriteOnly),
          Constant(Constant), VCallVisibility(Vis) {}

    /// True if this global might be accessed only by non-volatile loads.
    ///
    /// This in turn means it can be internalized in source and destination
    /// modules during thin LTO import because it neither modified nor its
    /// address is taken.
    unsigned MaybeReadOnly : 1;
    /// True if the variable is possibly only written to.
    ///
    /// Its value isn't loaded and its address isn't taken anywhere. False when
    /// the Constant attribute is set.
    unsigned MaybeWriteOnly : 1;
    /// True if the value is a compile-time constant.
    ///
    /// Global variable can be Constant while not being ReadOnly on several
    /// occasions:
    /// - it is volatile, (e.g mapped device address)
    /// - its address is taken, meaning that unlike ReadOnly vars we can't
    ///   internalize it.
    /// Constant variables are always imported thus giving compiler an
    /// opportunity to make some extra optimizations. Readonly constants
    /// are also internalized.
    unsigned Constant : 1;
    /// VCall visibility set from metadata on vtable definitions.
    unsigned VCallVisibility : 2;
  } VarFlags; ///< Per-variable flags for this global variable summary.

  /// Construct a global variable summary.
  /// \param Flags Common global-value flags.
  /// \param VarFlags Global-variable-specific flags.
  /// \param Refs Referenced value infos from the initializer.
  GlobalVarSummary(GVFlags Flags, GVarFlags VarFlags,
                   SmallVectorImpl<ValueInfo> &&Refs)
      : GlobalValueSummary(GlobalVarKind, Flags, std::move(Refs)),
        VarFlags(VarFlags) {}

  /// Check if this is a global variable summary.
  /// \param GVS Summary to test.
  /// @return Check if this is a global variable summary.
  static bool classof(const GlobalValueSummary *GVS) {
    return GVS->getSummaryKind() == GlobalVarKind;
  }

  /// Return the global-variable-specific flags.
  /// @return The global-variable-specific flags.
  GVarFlags varflags() const { return VarFlags; }
  /// Set whether this variable may be read-only.
  /// \param RO New maybe-read-only flag.
  void setReadOnly(bool RO) { VarFlags.MaybeReadOnly = RO; }
  /// Set whether this variable may be write-only.
  /// \param WO New maybe-write-only flag.
  void setWriteOnly(bool WO) { VarFlags.MaybeWriteOnly = WO; }
  /// Return true if this variable may be read-only.
  /// @return True if this variable may be read-only.
  bool maybeReadOnly() const { return VarFlags.MaybeReadOnly; }
  /// Return true if this variable may be write-only.
  /// @return True if this variable may be write-only.
  bool maybeWriteOnly() const { return VarFlags.MaybeWriteOnly; }
  /// Return true if this variable is a compile-time constant.
  /// @return True if this variable is a compile-time constant.
  bool isConstant() const { return VarFlags.Constant; }
  /// Set the vcall visibility for a vtable definition.
  /// \param Vis New vcall visibility.
  void setVCallVisibility(GlobalObject::VCallVisibility Vis) {
    VarFlags.VCallVisibility = Vis;
  }
  /// Return the vcall visibility for a vtable definition.
  /// @return The vcall visibility for a vtable definition.
  GlobalObject::VCallVisibility getVCallVisibility() const {
    return (GlobalObject::VCallVisibility)VarFlags.VCallVisibility;
  }

  /// Record the list of functions referenced by this vtable definition.
  /// \param Funcs Functions and their offsets within the vtable.
  void setVTableFuncs(VTableFuncList Funcs) {
    assert(!VTableFuncs);
    VTableFuncs = std::make_unique<VTableFuncList>(std::move(Funcs));
  }

  /// Return the functions referenced by this vtable definition, if any.
  /// @return The functions referenced by this vtable definition, if any.
  ArrayRef<VirtFuncOffset> vTableFuncs() const {
    if (VTableFuncs)
      return *VTableFuncs;
    return {};
  }
};

/// Summary of type identifier metadata used for CFI and whole-program
/// devirtualization.
struct TypeTestResolution {
  /// Specifies which kind of type check we should emit for this byte array.
  ///
  /// See http://clang.llvm.org/docs/ControlFlowIntegrityDesign.html for full
  /// details on each kind of check; the enumerators are described with
  /// reference to that document.
  enum Kind {
    Unsat,     ///< Unsatisfiable type (i.e. no global has this type metadata)
    ByteArray, ///< Test a byte array (first example)
    Inline,    ///< Inlined bit vector ("Short Inline Bit Vectors")
    Single,    ///< Single element (last example in "Short Inline Bit Vectors")
    AllOnes,   ///< All-ones bit vector ("Eliminating Bit Vector Checks for
               ///  All-Ones Bit Vectors")
    Unknown,   ///< Unknown (analysis not performed, don't lower)
  } TheKind = Unknown; ///< Selected type-check lowering kind.

  /// Bit width of the size-minus-one range used to emit compact checks.
  ///
  /// For example, if the size is in range [1,256], this number will be 8. This
  /// helps generate the most compact instruction sequences.
  unsigned SizeM1BitWidth = 0;

  // The following fields are only used if the target does not support the use
  // of absolute symbols to store constants. Their meanings are the same as the
  // corresponding fields in LowerTypeTestsModule::TypeIdLowering in
  // LowerTypeTests.cpp.

  /// Log2 of the alignment of the type identifier's byte array.
  uint64_t AlignLog2 = 0;
  /// Size of the type identifier's byte array minus one.
  uint64_t SizeM1 = 0;
  /// Bit mask used when lowering the type test.
  uint8_t BitMask = 0;
  /// Inline bit vector payload when Kind is Inline.
  uint64_t InlineBits = 0;
};

/// Whole-program virtual call resolution for a (typeid, offset) pair.
struct WholeProgramDevirtResolution {
  /// Kind of whole-program virtual call resolution to apply.
  enum Kind {
    Indir,        ///< Just do a regular virtual call
    SingleImpl,   ///< Single implementation devirtualization
    BranchFunnel, ///< When retpoline mitigation is enabled, use a branch funnel
                  ///< that is defined in the merged module. Otherwise same as
                  ///< Indir.
  } TheKind = Indir; ///< Selected whole-program virtual call resolution kind.

  /// Name of the single implementation when TheKind is SingleImpl.
  std::string SingleImplName;

  /// Resolution for a virtual call with a specific constant-argument vector.
  struct ByArg {
    /// Kind of by-argument virtual call resolution.
    enum Kind {
      Indir,            ///< Just do a regular virtual call
      UniformRetVal,    ///< Uniform return value optimization
      UniqueRetVal,     ///< Unique return value optimization
      VirtualConstProp, ///< Virtual constant propagation
    } TheKind = Indir; ///< Selected by-argument resolution kind.

    /// Extra integer payload for UniformRetVal or UniqueRetVal resolutions.
    ///
    /// Additional information for the resolution:
    /// - UniformRetVal: the uniform return value.
    /// - UniqueRetVal: the return value associated with the unique vtable (0 or
    ///   1).
    uint64_t Info = 0;

    // The following fields are only used if the target does not support the use
    // of absolute symbols to store constants.

    /// Byte offset used when absolute symbols are unavailable.
    uint32_t Byte = 0;
    /// Bit offset used when absolute symbols are unavailable.
    uint32_t Bit = 0;
  };

  /// Resolutions for calls with all constant integer arguments (excluding the
  /// first argument, "this"), where the key is the argument vector.
  std::map<std::vector<uint64_t>, ByArg> ResByArg;
};

/// Combined type-test and whole-program-devirt summary for one type id.
struct TypeIdSummary {
  /// Type-test resolution information for this type identifier.
  TypeTestResolution TTRes;

  /// Mapping from byte offset to whole-program devirt resolution for that
  /// (typeid, byte offset) pair.
  std::map<uint64_t, WholeProgramDevirtResolution> WPDRes;
};

/// Encapsulate the names of CFI target functions for ThinLTO export.
///
/// It interfaces with ThinLTO to determine efficiently which of the names need
/// to be exported for a particular module.
class CfiFunctionIndex {
  // `Names` is the authoritative source of data. `ThinLTOToNamesIndex` is there
  // just to efficiently retrieve which names in this index need exporting for
  // a particular module index. We cannot guarantee the ThinLTO GUIDs are
  // collision - free, so we associate a collection to a guid. Functions with
  // the same name may have different GUIDs, too. So we index a list of names
  // with the same GUID under that GUID key. We don't need the reverse because
  // the queries from ThinLTO use GUIDs as key.
  // Note that StringSet rehashing doesn't move keys, so we can safely store the
  // StringRef value inserted in `Names` in ThinLTOToNamesIndex, and avoid
  // copies.
  // Design note: we could do away with Names and use ThinLTOToNamesIndex as
  // index and data source, but opted against, for a small heap penalty, to
  // avoid confusion wrt the role GUIDs play in this case: they are an artifact
  // of the need to interface with ThinLTO, not otherwise necessary to CFI.
  StringSet<> Names;

  using InternalIndexGroup = SetVector<StringRef>;
  DenseMap<GlobalValue::GUID, InternalIndexGroup> ThinLTOToNamesIndex;

  using NestedIterator = InternalIndexGroup::const_iterator;

public:
  /// Construct an empty CFI function name index.
  CfiFunctionIndex() = default;
  /// Copy construction is deleted; the index owns its string storage.
  /// \param Other Index that would be copied.
  CfiFunctionIndex(const CfiFunctionIndex &Other) = delete;
  /// Move-construct a CFI function name index.
  /// \param Other Index to move from.
  CfiFunctionIndex(CfiFunctionIndex &&Other) = default;

  /// API used for serialization, e.g. YAML.
  /// @return API used for serialization, e.g.
  std::vector<std::pair<StringRef, GlobalValue::GUID>>
  getSortedSymbols() const {
    std::vector<std::pair<StringRef, GlobalValue::GUID>> Symbols;
    for (auto &[GUID, Names] : ThinLTOToNamesIndex)
      for (auto Name : Names)
        Symbols.emplace_back(Name, GUID);
    llvm::sort(Symbols);
    return Symbols;
  }

  /// get the set of GUIDs that should also be exported because they are the
  /// GUIDs of the cfi functions encapsulated here.
  /// @return The set of GUIDs of the CFI functions encapsulated here that should also be exported.
  auto getExportedThinLTOGUIDs() const {
    return map_range(ThinLTOToNamesIndex, [](auto I) { return I.first; });
  }

  /// get the name(s) associated with a given ThinLTO GUID. This enables
  /// efficient identification of the subset of names that should be included in
  /// a module summary.
  /// \param GUID ThinLTO GUID whose associated names are requested.
  /// @return The name(s) associated with \p GUID, or an empty range if none.
  auto getNamesForGUID(GlobalValue::GUID GUID) const {
    auto I = ThinLTOToNamesIndex.find(GUID);
    if (I == ThinLTOToNamesIndex.end())
      return make_range(NestedIterator{}, NestedIterator{});
    return make_range(I->second.begin(), I->second.end());
  }

  /// Add the function name and the GUID that ThinLTO uses for it.
  /// \param Name CFI target function name.
  /// \param GUID ThinLTO GUID associated with \p Name.
  void addSymbolWithThinLTOGUID(StringRef Name, GlobalValue::GUID GUID) {
    auto [Iter, _] = Names.insert(Name);
    ThinLTOToNamesIndex[GUID].insert(Iter->first());
  }

  /// Return true if \p Name is recorded as a CFI target function.
  /// \param Name Function name to look up.
  /// @return True if \p Name is recorded as a CFI target function.
  bool contains(StringRef Name) const {
    return Names.find(Name) != Names.end();
  }

  /// Return true if no CFI target function names are recorded.
  /// @return True if no CFI target function names are recorded.
  bool empty() const {
    assert(Names.empty() == ThinLTOToNamesIndex.empty());
    return Names.empty();
  }
};

/// 160 bits SHA1
using ModuleHash = std::array<uint32_t, 5>;

/// Type used for iterating through the global value summary map.
using const_gvsummary_iterator = GlobalValueSummaryMapTy::const_iterator;
/// Mutable iterator type for the global value summary map.
using gvsummary_iterator = GlobalValueSummaryMapTy::iterator;

/// String table to hold/own module path strings, as well as a hash
/// of the module. The StringMap makes a copy of and owns inserted strings.
using ModulePathStringTableTy = StringMap<ModuleHash>;

/// Map of global value GUID to its summary, used to identify values defined in
/// a particular module, and provide efficient access to their summary.
using GVSummaryMapTy = DenseMap<GlobalValue::GUID, GlobalValueSummary *>;

/// Map of a module name to the GUIDs and summaries we will import from that
/// module.
using ModuleToSummariesForIndexTy =
    std::map<std::string, GVSummaryMapTy, std::less<>>;

/// A set of global value summary pointers.
using GVSummaryPtrSet = SmallPtrSet<GlobalValueSummary *, 0>;

/// Map of a type GUID to type id string and summary (multimap used
/// in case of GUID conflicts).
using TypeIdSummaryMapTy =
    std::multimap<GlobalValue::GUID, std::pair<StringRef, TypeIdSummary>>;

/// Holds a vtable ValueInfo and its address-point offset for a type id.
///
/// The following data structures summarize type metadata information.
/// For type metadata overview see https://llvm.org/docs/TypeMetadata.html.
/// Each type metadata includes both the type identifier and the offset of
/// the address point of the type (the address held by objects of that type
/// which may not be the beginning of the virtual table). Vtable definitions
/// are decorated with type metadata for the types they are compatible with.
///
/// Holds information about vtable definitions decorated with type metadata:
/// the vtable definition value and its address point offset in a type
/// identifier metadata it is decorated (compatible) with.
struct TypeIdOffsetVtableInfo {
  /// Construct from an address-point offset and vtable ValueInfo.
  /// \param Offset Address point offset within the type identifier metadata.
  /// \param VI Value info for the vtable definition.
  TypeIdOffsetVtableInfo(uint64_t Offset, ValueInfo VI)
      : AddressPointOffset(Offset), VTableVI(VI) {}

  /// Address point offset of the vtable within the type identifier metadata.
  uint64_t AddressPointOffset;
  /// Value info for the vtable definition compatible with the type identifier.
  ValueInfo VTableVI;
};
/// List of vtable definitions decorated by a particular type identifier.
///
/// Also records their corresponding offsets in that type identifier's
/// metadata. Note that each type identifier may be compatible with multiple
/// vtables, due to inheritance, which is why this is a vector.
using TypeIdCompatibleVtableInfo = std::vector<TypeIdOffsetVtableInfo>;

/// Class to hold module path string table and global value map,
/// and encapsulate methods for operating on them.
class ModuleSummaryIndex {
private:
  /// Map from value name to list of summary instances for values of that
  /// name (may be duplicates in the COMDAT case, e.g.).
  GlobalValueSummaryMapTy GlobalValueMap;

  /// Holds strings for combined index, mapping to the corresponding module ID.
  ModulePathStringTableTy ModulePathStringTable;

  BumpPtrAllocator TypeIdSaverAlloc;
  UniqueStringSaver TypeIdSaver;

  /// Mapping from type identifier GUIDs to type identifier and its summary
  /// information. Produced by thin link.
  TypeIdSummaryMapTy TypeIdMap;

  /// Mapping from type identifier to information about vtables decorated
  /// with that type identifier's metadata. Produced by per module summary
  /// analysis and consumed by thin link. For more information, see description
  /// above where TypeIdCompatibleVtableInfo is defined.
  std::map<StringRef, TypeIdCompatibleVtableInfo, std::less<>>
      TypeIdCompatibleVtableMap;

  /// Mapping from original ID to GUID. If original ID can map to multiple
  /// GUIDs, it will be mapped to 0.
  DenseMap<GlobalValue::GUID, GlobalValue::GUID> OidGuidMap;

  /// Indicates that summary-based GlobalValue GC has run, and values with
  /// GVFlags::Live==false are really dead. Otherwise, all values must be
  /// considered live.
  bool WithGlobalValueDeadStripping = false;

  /// Indicates that summary-based attribute propagation has run and
  /// GVarFlags::MaybeReadonly / GVarFlags::MaybeWriteonly are really
  /// read/write only.
  bool WithAttributePropagation = false;

  /// Indicates that summary-based DSOLocal propagation has run and the flag in
  /// every summary of a GV is synchronized.
  bool WithDSOLocalPropagation = false;

  /// Indicates that summary-based internalization and promotion has run.
  bool WithInternalizeAndPromote = false;

  /// Indicates that we have whole program visibility.
  bool WithWholeProgramVisibility = false;

  /// Indicates that summary-based synthetic entry count propagation has run
  bool HasSyntheticEntryCounts = false;

  /// Indicates that we linked with allocator supporting hot/cold new operators.
  bool WithSupportsHotColdNew = false;

  /// Indicates that distributed backend should skip compilation of the
  /// module. Flag is suppose to be set by distributed ThinLTO indexing
  /// when it detected that the module is not needed during the final
  /// linking. As result distributed backend should just output a minimal
  /// valid object file.
  bool SkipModuleByDistributedBackend = false;

  /// If true then we're performing analysis of IR module, or parsing along with
  /// the IR from assembly. The value of 'false' means we're reading summary
  /// from BC or YAML source. Affects the type of value stored in NameOrGV
  /// union.
  bool HaveGVs;

  // True if the index was created for a module compiled with -fsplit-lto-unit.
  bool EnableSplitLTOUnit;

  // True if the index was created for a module compiled with -funified-lto
  bool UnifiedLTO;

  // True if some of the modules were compiled with -fsplit-lto-unit and
  // some were not. Set when the combined index is created during the thin link.
  bool PartiallySplitLTOUnits = false;

  /// True if some of the FunctionSummary contains a ParamAccess.
  bool HasParamAccess = false;

  CfiFunctionIndex CfiFunctionDefs;
  CfiFunctionIndex CfiFunctionDecls;

  // Used in cases where we want to record the name of a global, but
  // don't have the string owned elsewhere (e.g. the Strtab on a module).
  BumpPtrAllocator Alloc;
  StringSaver Saver;

  // The total number of basic blocks in the module in the per-module summary or
  // the total number of basic blocks in the LTO unit in the combined index.
  // FIXME: Putting this in the distributed ThinLTO index files breaks LTO
  // backend caching on any BB change to any linked file. It is currently not
  // used except in the case of a SamplePGO partial profile, and should be
  // reevaluated/redesigned to allow more effective incremental builds in that
  // case.
  uint64_t BlockCount = 0;

  // List of unique stack ids (hashes). We use a 4B index of the id in the
  // stack id lists on the alloc and callsite summaries for memory savings,
  // since the number of unique ids is in practice much smaller than the
  // number of stack id references in the summaries.
  std::vector<uint64_t> StackIds;

  // Temporary map while building StackIds list. Clear when index is completely
  // built via releaseTemporaryMemory.
  DenseMap<uint64_t, unsigned> StackIdToIndex;

  // YAML I/O support.
  friend yaml::MappingTraits<ModuleSummaryIndex>;

  GlobalValueSummaryMapTy::value_type *
  getOrInsertValuePtr(GlobalValue::GUID GUID) {
    return &*GlobalValueMap.try_emplace(GUID, GlobalValueSummaryInfo(HaveGVs))
                 .first;
  }

public:
  /// Construct a module summary index.
  ///
  /// See HaveGVs variable comment.
  /// \param HaveGVs Whether summaries hold GlobalValue pointers vs names.
  /// \param EnableSplitLTOUnit Whether the index was built with split LTO units.
  /// \param UnifiedLTO Whether the index was built with unified LTO.
  ModuleSummaryIndex(bool HaveGVs, bool EnableSplitLTOUnit = false,
                     bool UnifiedLTO = false)
      : TypeIdSaver(TypeIdSaverAlloc), HaveGVs(HaveGVs),
        EnableSplitLTOUnit(EnableSplitLTOUnit), UnifiedLTO(UnifiedLTO),
        Saver(Alloc) {}

  /// Current version for the module summary in bitcode files.
  ///
  /// The BitcodeSummaryVersion should be bumped whenever we introduce changes
  /// in the way some record are interpreted, like flags for instance.
  /// Note that incrementing this may require changes in both BitcodeReader.cpp
  /// and BitcodeWriter.cpp.
  static constexpr uint64_t BitcodeSummaryVersion = 14;

  /// Return the conventional module name used for Regular LTO in ASM output.
  /// @return The conventional module name used for Regular LTO in ASM output.
  static constexpr const char *getRegularLTOModuleName() {
    return "[Regular LTO]";
  }

  /// Return true if this index stores GlobalValue pointers in summaries.
  /// @return True if this index stores GlobalValue pointers in summaries.
  bool haveGVs() const { return HaveGVs; }

  /// Return the combined bitcode flags for this index.
  /// @return The combined bitcode flags for this index.
  LLVM_ABI uint64_t getFlags() const;
  /// Set the combined bitcode flags for this index.
  /// \param Flags Encoded index flags to store.
  LLVM_ABI void setFlags(uint64_t Flags);

  /// Return the total basic-block count recorded in this index.
  /// @return The total basic-block count recorded in this index.
  uint64_t getBlockCount() const { return BlockCount; }
  /// Add \p C to the total basic-block count.
  /// \param C Number of basic blocks to add.
  void addBlockCount(uint64_t C) { BlockCount += C; }
  /// Set the total basic-block count to \p C.
  /// \param C New total basic-block count.
  void setBlockCount(uint64_t C) { BlockCount = C; }

  /// Return an iterator to the beginning of the global value summary map.
  /// @return An iterator to the beginning of the global value summary map.
  gvsummary_iterator begin() { return GlobalValueMap.begin(); }
  /// Return a const iterator to the beginning of the global value summary map.
  /// @return A const iterator to the beginning of the global value summary map.
  const_gvsummary_iterator begin() const { return GlobalValueMap.begin(); }
  /// Return an iterator past the end of the global value summary map.
  /// @return An iterator past the end of the global value summary map.
  gvsummary_iterator end() { return GlobalValueMap.end(); }
  /// Return a const iterator past the end of the global value summary map.
  /// @return A const iterator past the end of the global value summary map.
  const_gvsummary_iterator end() const { return GlobalValueMap.end(); }
  /// Return the number of GUID entries in the global value summary map.
  /// @return The number of GUID entries in the global value summary map.
  size_t size() const { return GlobalValueMap.size(); }

  /// Return an owning range over global value summaries sorted by GUID.
  /// @return An owning range over global value summaries sorted by GUID.
  GlobalValueSummaryMapTy::SortedEntriesRange
  sortedGlobalValueSummariesRange() const {
    return GlobalValueMap.sortedRange();
  }

  /// Return the list of unique stack ids recorded in this index.
  /// @return The list of unique stack ids recorded in this index.
  const std::vector<uint64_t> &stackIds() const { return StackIds; }

  /// Return the index of \p StackId, inserting it if it is not yet present.
  /// \param StackId Full 64-bit stack id hash to record.
  /// @return The index of \p StackId, inserting it if it is not yet present.
  unsigned addOrGetStackIdIndex(uint64_t StackId) {
    auto Inserted = StackIdToIndex.insert({StackId, StackIds.size()});
    if (Inserted.second)
      StackIds.push_back(StackId);
    return Inserted.first->second;
  }

  /// Return the full stack id stored at \p Index in the StackIds vector.
  /// \param Index Index previously returned by addOrGetStackIdIndex.
  /// @return The full stack id stored at \p Index in the StackIds vector.
  uint64_t getStackIdAtIndex(unsigned Index) const {
    assert(StackIds.size() > Index);
    return StackIds[Index];
  }

  /// Release memory used only while constructing the index.
  ///
  /// Facility to release memory from data structures only needed during index
  /// construction (including while building combined index). Currently this only
  /// releases the temporary map used while constructing a correspondence between
  /// stack ids and their index in the StackIds vector. Mostly impactful when
  /// building a large combined index.
  void releaseTemporaryMemory() {
    assert(StackIdToIndex.size() == StackIds.size());
    StackIdToIndex.clear();
    StackIds.shrink_to_fit();
  }

  /// Convenience function for doing a DFS on a ValueInfo. Marks the function in
  /// the FunctionHasParent map.
  /// \param V Value info to discover and recurse into.
  /// \param FunctionHasParent Map updated with parenthood status for each node.
  static void discoverNodes(ValueInfo V,
                            std::map<ValueInfo, bool> &FunctionHasParent) {
    if (!V.getSummaryList().size())
      return; // skip external functions that don't have summaries

    // Mark discovered if we haven't yet
    auto S = FunctionHasParent.emplace(V, false);

    // Stop if we've already discovered this node
    if (!S.second)
      return;

    FunctionSummary *F =
        dyn_cast<FunctionSummary>(V.getSummaryList().front().get());
    assert(F != nullptr && "Expected FunctionSummary node");

    for (const auto &C : F->calls()) {
      // Insert node if necessary
      auto S = FunctionHasParent.emplace(C.first, true);

      // Skip nodes that we're sure have parents
      if (!S.second && S.first->second)
        continue;

      if (S.second)
        discoverNodes(C.first, FunctionHasParent);
      else
        S.first->second = true;
    }
  }

  /// Calculate a dummy FunctionSummary that edges to all call-graph roots.
  /// @return A dummy FunctionSummary with edges to all call-graph roots.
  FunctionSummary calculateCallGraphRoot() {
    // Functions that have a parent will be marked in FunctionHasParent pair.
    // Once we've marked all functions, the functions in the map that are false
    // have no parent (so they're the roots)
    std::map<ValueInfo, bool> FunctionHasParent;

    for (auto &S : *this) {
      // Skip external functions
      if (!S.second.getSummaryList().size() ||
          !isa<FunctionSummary>(S.second.getSummaryList().front().get()))
        continue;
      discoverNodes(ValueInfo(HaveGVs, &S), FunctionHasParent);
    }

    SmallVector<FunctionSummary::EdgeTy, 0> Edges;
    // create edges to all roots in the Index
    for (auto &P : FunctionHasParent) {
      if (P.second)
        continue; // skip over non-root nodes
      Edges.push_back(std::make_pair(P.first, CalleeInfo{}));
    }
    return FunctionSummary::makeDummyFunctionSummary(std::move(Edges));
  }

  /// Return true if summary-based GlobalValue dead stripping has run.
  /// @return True if summary-based GlobalValue dead stripping has run.
  bool withGlobalValueDeadStripping() const {
    return WithGlobalValueDeadStripping;
  }
  /// Mark that summary-based GlobalValue dead stripping has run.
  void setWithGlobalValueDeadStripping() {
    WithGlobalValueDeadStripping = true;
  }

  /// Return true if summary-based attribute propagation has run.
  /// @return True if summary-based attribute propagation has run.
  bool withAttributePropagation() const { return WithAttributePropagation; }
  /// Mark that summary-based attribute propagation has run.
  void setWithAttributePropagation() {
    WithAttributePropagation = true;
  }

  /// Return true if summary-based DSOLocal propagation has run.
  /// @return True if summary-based DSOLocal propagation has run.
  bool withDSOLocalPropagation() const { return WithDSOLocalPropagation; }
  /// Mark that summary-based DSOLocal propagation has run.
  void setWithDSOLocalPropagation() { WithDSOLocalPropagation = true; }

  /// Return true if summary-based internalization and promotion has run.
  /// @return True if summary-based internalization and promotion has run.
  bool withInternalizeAndPromote() const { return WithInternalizeAndPromote; }
  /// Mark that summary-based internalization and promotion has run.
  void setWithInternalizeAndPromote() { WithInternalizeAndPromote = true; }

  /// Return true if whole-program visibility is available.
  /// @return True if whole-program visibility is available.
  bool withWholeProgramVisibility() const { return WithWholeProgramVisibility; }
  /// Mark that whole-program visibility is available.
  void setWithWholeProgramVisibility() { WithWholeProgramVisibility = true; }

  /// Return true if \p GVS is known read-only after attribute propagation.
  /// \param GVS Global variable summary to query.
  /// @return True if \p GVS is known read-only after attribute propagation.
  bool isReadOnly(const GlobalVarSummary *GVS) const {
    return WithAttributePropagation && GVS->maybeReadOnly();
  }
  /// Return true if \p GVS is known write-only after attribute propagation.
  /// \param GVS Global variable summary to query.
  /// @return True if \p GVS is known write-only after attribute propagation.
  bool isWriteOnly(const GlobalVarSummary *GVS) const {
    return WithAttributePropagation && GVS->maybeWriteOnly();
  }

  /// Return true if linking used an allocator that supports hot/cold new.
  /// @return True if linking used an allocator that supports hot/cold new.
  bool withSupportsHotColdNew() const { return WithSupportsHotColdNew; }
  /// Mark that linking used an allocator that supports hot/cold new.
  void setWithSupportsHotColdNew() { WithSupportsHotColdNew = true; }

  /// Return true if the distributed backend should skip this module.
  /// @return True if the distributed backend should skip this module.
  bool skipModuleByDistributedBackend() const {
    return SkipModuleByDistributedBackend;
  }
  /// Mark that the distributed backend should skip compiling this module.
  void setSkipModuleByDistributedBackend() {
    SkipModuleByDistributedBackend = true;
  }

  /// Return true if the index was created with -fsplit-lto-unit.
  /// @return True if the index was created with -fsplit-lto-unit.
  bool enableSplitLTOUnit() const { return EnableSplitLTOUnit; }
  /// Mark that the index was created with -fsplit-lto-unit.
  void setEnableSplitLTOUnit() { EnableSplitLTOUnit = true; }

  /// Return true if the index was created with -funified-lto.
  /// @return True if the index was created with -funified-lto.
  bool hasUnifiedLTO() const { return UnifiedLTO; }
  /// Mark that the index was created with -funified-lto.
  void setUnifiedLTO() { UnifiedLTO = true; }

  /// Return true if some modules were split LTO units and some were not.
  /// @return True if some modules were split LTO units and some were not.
  bool partiallySplitLTOUnits() const { return PartiallySplitLTOUnits; }
  /// Mark that the combined index mixes split and non-split LTO units.
  void setPartiallySplitLTOUnits() { PartiallySplitLTOUnits = true; }

  /// Return true if any FunctionSummary in the index has ParamAccess data.
  /// @return True if any FunctionSummary in the index has ParamAccess data.
  bool hasParamAccess() const { return HasParamAccess; }

  /// Return true if \p GVS is live given whether dead stripping has run.
  /// \param GVS Global value summary to query.
  /// @return True if \p GVS is live given whether dead stripping has run.
  bool isGlobalValueLive(const GlobalValueSummary *GVS) const {
    return !WithGlobalValueDeadStripping || GVS->isLive();
  }
  /// Return true if any summary for \p GUID is live.
  /// \param GUID Global value GUID to query.
  /// @return True if any summary for \p GUID is live.
  LLVM_ABI bool isGUIDLive(GlobalValue::GUID GUID) const;

  /// Return a ValueInfo for the index value_type (convenient when iterating
  /// index).
  /// \param R Map entry whose GUID and summary info are wrapped.
  /// @return A ValueInfo for the index value_type (convenient when iterating index).
  ValueInfo getValueInfo(const GlobalValueSummaryMapTy::value_type &R) const {
    return ValueInfo(HaveGVs, &R);
  }

  /// Return a ValueInfo for GUID if it exists, otherwise return ValueInfo().
  /// \param GUID Global value GUID to look up.
  /// @return A ValueInfo for GUID if it exists, otherwise return ValueInfo().
  ValueInfo getValueInfo(GlobalValue::GUID GUID) const {
    auto I = GlobalValueMap.find(GUID);
    return ValueInfo(HaveGVs, I == GlobalValueMap.end() ? nullptr : &*I);
  }

  /// Return a ValueInfo for \p GUID.
  /// \param GUID Global value GUID to insert or look up.
  /// @return A ValueInfo for \p GUID.
  ValueInfo getOrInsertValueInfo(GlobalValue::GUID GUID) {
    return ValueInfo(HaveGVs, getOrInsertValuePtr(GUID));
  }

  /// Save a string in the Index for use as a value name.
  ///
  /// Use before passing Name to getOrInsertValueInfo when the string isn't
  /// owned elsewhere (e.g. on the module's Strtab).
  /// \param String The string to own in the index allocator.
  /// @return Save a string in the Index for use as a value name.
  StringRef saveString(StringRef String) { return Saver.save(String); }

  /// Return a ValueInfo for \p GUID setting value \p Name.
  /// \param GUID Global value GUID to insert or look up.
  /// \param Name Name string to associate with the value info.
  /// @return A ValueInfo for \p GUID setting value \p Name.
  ValueInfo getOrInsertValueInfo(GlobalValue::GUID GUID, StringRef Name) {
    assert(!HaveGVs);
    auto VP = getOrInsertValuePtr(GUID);
    VP->second.U.Name = Name;
    return ValueInfo(HaveGVs, VP);
  }

  /// Return a ValueInfo for \p GV with GUID \p GUID and mark it as belonging to
  /// GV.
  /// \param GV The global value whose IR is available.
  /// \param GUID GUID to associate with \p GV.
  /// @return A ValueInfo for \p GV with GUID \p GUID and mark it as belonging to GV.
  ValueInfo getOrInsertValueInfo(const GlobalValue *GV,
                                 GlobalValue::GUID GUID) {
    assert(HaveGVs);
    auto VP = getOrInsertValuePtr(GUID);
    VP->second.U.GV = GV;
    return ValueInfo(HaveGVs, VP);
  }

  /// Return a ValueInfo for \p GV and mark it as belonging to GV.
  /// \param GV The global value whose IR is available.
  /// @return A ValueInfo for \p GV and mark it as belonging to GV.
  ValueInfo getOrInsertValueInfo(const GlobalValue *GV) {
    return getOrInsertValueInfo(GV, GV->getGUID());
  }

  /// Return the GUID for \p OriginalId in the OidGuidMap.
  /// \param OriginalID Original (pre-promotion) GUID to look up.
  /// @return The GUID for \p OriginalId in the OidGuidMap.
  GlobalValue::GUID getGUIDFromOriginalID(GlobalValue::GUID OriginalID) const {
    const auto I = OidGuidMap.find(OriginalID);
    return I == OidGuidMap.end() ? 0 : I->second;
  }

  /// Return the mutable index of CFI function definitions.
  /// @return The mutable index of CFI function definitions.
  CfiFunctionIndex &cfiFunctionDefs() { return CfiFunctionDefs; }
  /// Return the const index of CFI function definitions.
  /// @return The const index of CFI function definitions.
  const CfiFunctionIndex &cfiFunctionDefs() const { return CfiFunctionDefs; }

  /// Return the mutable index of CFI function declarations.
  /// @return The mutable index of CFI function declarations.
  CfiFunctionIndex &cfiFunctionDecls() { return CfiFunctionDecls; }
  /// Return the const index of CFI function declarations.
  /// @return The const index of CFI function declarations.
  const CfiFunctionIndex &cfiFunctionDecls() const { return CfiFunctionDecls; }

  /// Add a global value summary for a value.
  /// \param GV Global value whose summary is being recorded.
  /// \param Summary Summary to associate with \p GV.
  void addGlobalValueSummary(const GlobalValue &GV,
                             std::unique_ptr<GlobalValueSummary> Summary) {
    addGlobalValueSummary(getOrInsertValueInfo(&GV), std::move(Summary));
  }

  /// Add a global value summary for a value of the given name.
  /// \param ValueName Name of the global value.
  /// \param Summary Summary to associate with the named value.
  void addGlobalValueSummary(StringRef ValueName,
                             std::unique_ptr<GlobalValueSummary> Summary) {
    addGlobalValueSummary(
        getOrInsertValueInfo(
            GlobalValue::getGUIDAssumingExternalLinkage(ValueName)),
        std::move(Summary));
  }

  /// Add a global value summary for the given ValueInfo.
  /// \param VI Value info that owns the summary list.
  /// \param Summary Summary to append for this value.
  void addGlobalValueSummary(ValueInfo VI,
                             std::unique_ptr<GlobalValueSummary> Summary) {
    if (const FunctionSummary *FS = dyn_cast<FunctionSummary>(Summary.get()))
      HasParamAccess |= !FS->paramAccesses().empty();
    addOriginalName(VI.getGUID(), Summary->getOriginalName());
    // Here we have a notionally const VI, but the value it points to is owned
    // by the non-const *this.
    const_cast<GlobalValueSummaryMapTy::value_type *>(VI.getRef())
        ->second.addSummary(std::move(Summary));
  }

  /// Add an original name for the value of the given GUID.
  /// \param ValueGUID Current GUID of the value.
  /// \param OrigGUID Original GUID before promotion or renaming.
  void addOriginalName(GlobalValue::GUID ValueGUID,
                       GlobalValue::GUID OrigGUID) {
    if (OrigGUID == 0 || ValueGUID == OrigGUID)
      return;
    auto [It, Inserted] = OidGuidMap.try_emplace(OrigGUID, ValueGUID);
    if (!Inserted && It->second != ValueGUID)
      It->second = 0;
  }

  /// Find the summary for ValueInfo \p VI in module \p ModuleId, or nullptr if
  /// not found.
  /// \param VI Value info whose summary list is searched.
  /// \param ModuleId Module path that must match the summary's module path.
  /// @return The summary for ValueInfo \p VI in module \p ModuleId, or nullptr if not found.
  GlobalValueSummary *findSummaryInModule(ValueInfo VI, StringRef ModuleId) const {
    auto SummaryList = VI.getSummaryList();
    auto Summary =
        llvm::find_if(SummaryList,
                      [&](const std::unique_ptr<GlobalValueSummary> &Summary) {
                        return Summary->modulePath() == ModuleId;
                      });
    if (Summary == SummaryList.end())
      return nullptr;
    return Summary->get();
  }

  /// Find the summary for global \p GUID in module \p ModuleId, or nullptr if
  /// not found.
  /// \param ValueGUID GUID of the global value to find.
  /// \param ModuleId Module path that must match the summary's module path.
  /// @return The summary for global \p GUID in module \p ModuleId, or nullptr if not found.
  GlobalValueSummary *findSummaryInModule(GlobalValue::GUID ValueGUID,
                                          StringRef ModuleId) const {
    auto CalleeInfo = getValueInfo(ValueGUID);
    if (!CalleeInfo)
      return nullptr; // This function does not have a summary
    return findSummaryInModule(CalleeInfo, ModuleId);
  }

  /// Returns the first GlobalValueSummary for \p GV, asserting that there
  /// is only one if \p PerModuleIndex.
  /// \param GV Global value whose summary is requested.
  /// \param PerModuleIndex If true, assert there is only one summary.
  /// @return The first GlobalValueSummary for \p GV, asserting that there is only one if \p PerModuleIndex.
  GlobalValueSummary *getGlobalValueSummary(const GlobalValue &GV,
                                            bool PerModuleIndex = true) const {
    assert(GV.hasName() && "Can't get GlobalValueSummary for GV with no name");
    return getGlobalValueSummary(GV.getGUID(), PerModuleIndex);
  }

  /// Returns the first GlobalValueSummary for \p ValueGUID, asserting that
  /// there is only one if \p PerModuleIndex.
  /// \param ValueGUID GUID of the global value whose summary is requested.
  /// \param PerModuleIndex If true, assert there is only one summary.
  /// @return The first GlobalValueSummary for \p ValueGUID, asserting that there is only one if \p PerModuleIndex.
  LLVM_ABI GlobalValueSummary *
  getGlobalValueSummary(GlobalValue::GUID ValueGUID,
                        bool PerModuleIndex = true) const;

  /// Table of modules, containing module hash and id.
  /// @return The const module path table mapping paths to hashes and ids.
  const StringMap<ModuleHash> &modulePaths() const {
    return ModulePathStringTable;
  }

  /// Table of modules, containing hash and id.
  /// @return The mutable module path table mapping paths to hashes and ids.
  StringMap<ModuleHash> &modulePaths() { return ModulePathStringTable; }

  /// Get the module SHA1 hash recorded for the given module path.
  /// \param ModPath Path of the module whose hash is requested.
  /// @return The module SHA1 hash recorded for the given module path.
  const ModuleHash &getModuleHash(const StringRef ModPath) const {
    auto It = ModulePathStringTable.find(ModPath);
    assert(It != ModulePathStringTable.end() && "Module not registered");
    return It->second;
  }

  /// Convenience method for creating a promoted global name
  /// for the given value name of a local, and its original module's ID.
  /// \param Name The local value name to promote.
  /// \param ModHash Hash of the defining module used to form the suffix.
  /// @return A promoted global name for the local \p Name and module \p ModHash.
  static std::string getGlobalNameForLocal(StringRef Name, ModuleHash ModHash) {
    std::string Suffix = utostr((uint64_t(ModHash[0]) << 32) |
                                ModHash[1]); // Take the first 64 bits
    return getGlobalNameForLocal(Name, Suffix);
  }

  /// Create a promoted global name by appending \p Suffix to \p Name.
  /// \param Name The local value name to promote.
  /// \param Suffix Module-unique suffix (typically derived from the hash).
  /// @return Create a promoted global name by appending \p Suffix to \p Name.
  static std::string getGlobalNameForLocal(StringRef Name, StringRef Suffix) {
    SmallString<256> NewName(Name);
    NewName += ".llvm.";
    NewName += Suffix;
    return std::string(NewName);
  }

  /// Return the unpromoted name for a global value, if it was promoted.
  ///
  /// Split off the rightmost ".llvm.${hash}" suffix, because it is possible in
  /// certain clients (not clang at the moment) for two rounds of ThinLTO
  /// optimization and therefore promotion to occur.
  /// \param Name Possibly promoted global value name.
  /// @return The unpromoted name for a global value, if it was promoted.
  static StringRef getOriginalNameBeforePromote(StringRef Name) {
    std::pair<StringRef, StringRef> Pair = Name.rsplit(".llvm.");
    return Pair.first;
  }

  /// Module path string table entry type (path mapped to module hash).
  typedef ModulePathStringTableTy::value_type ModuleInfo;

  /// Add a new module with the given \p Hash, mapped to the given \p
  /// ModID, and return a reference to the module.
  /// \param ModPath Path string identifying the module.
  /// \param Hash Module content hash (default all zeros).
  /// @return A pointer to the newly added module entry.
  ModuleInfo *addModule(StringRef ModPath, ModuleHash Hash = ModuleHash{{0}}) {
    return &*ModulePathStringTable.insert({ModPath, Hash}).first;
  }

  /// Return module entry for module with the given \p ModPath.
  /// \param ModPath Path of the module to look up.
  /// @return Module entry for module with the given \p ModPath.
  ModuleInfo *getModule(StringRef ModPath) {
    auto It = ModulePathStringTable.find(ModPath);
    assert(It != ModulePathStringTable.end() && "Module not registered");
    return &*It;
  }

  /// Return module entry for module with the given \p ModPath.
  /// \param ModPath Path of the module to look up.
  /// @return Module entry for module with the given \p ModPath.
  const ModuleInfo *getModule(StringRef ModPath) const {
    auto It = ModulePathStringTable.find(ModPath);
    assert(It != ModulePathStringTable.end() && "Module not registered");
    return &*It;
  }

  /// Check if the given Module has any functions available for exporting.
  ///
  /// We consider any module present in the ModulePathStringTable to have
  /// exported functions.
  /// \param M The module to query.
  /// @return Check if the given Module has any functions available for exporting.
  bool hasExportedFunctions(const Module &M) const {
    return ModulePathStringTable.count(M.getModuleIdentifier());
  }

  /// Return the map from type identifier GUIDs to type id summaries.
  /// @return The map from type identifier GUIDs to type id summaries.
  const TypeIdSummaryMapTy &typeIds() const { return TypeIdMap; }

  /// Return an existing or new TypeIdSummary entry for \p TypeId.
  /// This accessor can mutate the map and therefore should not be used in
  /// the ThinLTO backends.
  /// \param TypeId The type identifier string.
  /// @return An existing or new TypeIdSummary entry for \p TypeId.
  TypeIdSummary &getOrInsertTypeIdSummary(StringRef TypeId) {
    auto TidIter = TypeIdMap.equal_range(
        GlobalValue::getGUIDAssumingExternalLinkage(TypeId));
    for (auto &[GUID, TypeIdPair] : make_range(TidIter))
      if (TypeIdPair.first == TypeId)
        return TypeIdPair.second;
    auto It =
        TypeIdMap.insert({GlobalValue::getGUIDAssumingExternalLinkage(TypeId),
                          {TypeIdSaver.save(TypeId), TypeIdSummary()}});
    return It->second.second;
  }

  /// This returns either a pointer to the type id summary (if present in the
  /// summary map) or null (if not present). This may be used when importing.
  /// \param TypeId The type identifier string.
  /// @return Either a pointer to the type id summary (if present in the summary map) or null (if not present).
  const TypeIdSummary *getTypeIdSummary(StringRef TypeId) const {
    auto TidIter = TypeIdMap.equal_range(
        GlobalValue::getGUIDAssumingExternalLinkage(TypeId));
    for (const auto &[GUID, TypeIdPair] : make_range(TidIter))
      if (TypeIdPair.first == TypeId)
        return &TypeIdPair.second;
    return nullptr;
  }

  /// Return a mutable pointer to the type id summary for \p TypeId, or null.
  /// \param TypeId The type identifier string.
  /// @return A mutable pointer to the type id summary for \p TypeId, or null.
  TypeIdSummary *getTypeIdSummary(StringRef TypeId) {
    return const_cast<TypeIdSummary *>(
        static_cast<const ModuleSummaryIndex *>(this)->getTypeIdSummary(
            TypeId));
  }

  /// Return the map from type identifiers to compatible vtable summaries.
  /// @return The map from type identifiers to compatible vtable summaries.
  const auto &typeIdCompatibleVtableMap() const {
    return TypeIdCompatibleVtableMap;
  }

  /// Return an existing or new TypeIdCompatibleVtableMap entry for \p TypeId.
  /// This accessor can mutate the map and therefore should not be used in
  /// the ThinLTO backends.
  /// \param TypeId The type identifier string.
  /// @return An existing or new TypeIdCompatibleVtableMap entry for \p TypeId.
  TypeIdCompatibleVtableInfo &
  getOrInsertTypeIdCompatibleVtableSummary(StringRef TypeId) {
    return TypeIdCompatibleVtableMap[TypeIdSaver.save(TypeId)];
  }

  /// For the given \p TypeId, this returns the TypeIdCompatibleVtableMap
  /// entry if present in the summary map. This may be used when importing.
  /// \param TypeId The type identifier string.
  /// @return The TypeIdCompatibleVtableMap entry for \p TypeId if present.
  std::optional<TypeIdCompatibleVtableInfo>
  getTypeIdCompatibleVtableSummary(StringRef TypeId) const {
    auto I = TypeIdCompatibleVtableMap.find(TypeId);
    if (I == TypeIdCompatibleVtableMap.end())
      return std::nullopt;
    return I->second;
  }

  /// Collect for the given module the list of functions it defines
  /// (GUID -> Summary).
  /// \param ModulePath Path of the module whose defined functions are collected.
  /// \param GVSummaryMap Output map filled with GUID to summary pointers.
  LLVM_ABI void
  collectDefinedFunctionsForModule(StringRef ModulePath,
                                   GVSummaryMapTy &GVSummaryMap) const;

  /// Collect for each module the list of Summaries it defines (GUID ->
  /// Summary).
  /// \param ModuleToDefinedGVSummaries Output map from module path to
  ///        GUID-to-summary maps.
  template <class Map>
  void
  collectDefinedGVSummariesPerModule(Map &ModuleToDefinedGVSummaries) const {
    for (const auto &GlobalList : *this) {
      auto GUID = GlobalList.first;
      for (const auto &Summary : GlobalList.second.getSummaryList()) {
        ModuleToDefinedGVSummaries[Summary->modulePath()][GUID] = Summary.get();
      }
    }
  }

  /// Print to an output stream.
  /// \param OS Stream to print to.
  /// \param IsForDebug If true, include additional debug details.
  LLVM_ABI void print(raw_ostream &OS, bool IsForDebug = false) const;

  /// Dump to stderr (for debugging).
  LLVM_ABI void dump() const;

  /// Export summary to dot file for GraphViz.
  /// \param OS Stream to write the DOT graph to.
  /// \param GUIDPreservedSymbols GUIDs of symbols that must be preserved.
  LLVM_ABI void
  exportToDot(raw_ostream &OS,
              const DenseSet<GlobalValue::GUID> &GUIDPreservedSymbols) const;

  /// Print out strongly connected components for debugging.
  /// \param OS Stream to dump SCC information to.
  LLVM_ABI void dumpSCCs(raw_ostream &OS);

  /// Do the access attribute and DSOLocal propagation in combined index.
  /// \param PreservedSymbols GUIDs that must not be internalized or stripped.
  LLVM_ABI void
  propagateAttributes(const DenseSet<GlobalValue::GUID> &PreservedSymbols);

  /// Checks if we can import global variable from another module.
  /// \param S Summary of the global variable to consider for import.
  /// \param AnalyzeRefs If true, also analyze referenced values.
  /// @return True if we can import global variable from another module.
  LLVM_ABI bool canImportGlobalVar(const GlobalValueSummary *S,
                                   bool AnalyzeRefs) const;

  /// Same as above but checks whether the global var is importable as a
  /// declaration.
  /// \param S Summary of the global variable to consider for import.
  /// \param AnalyzeRefs If true, also analyze referenced values.
  /// \param CanImportDecl Set to true if import as a declaration is allowed.
  /// @return True if the global variable can be imported from another module.
  LLVM_ABI bool canImportGlobalVar(const GlobalValueSummary *S,
                                   bool AnalyzeRefs, bool &CanImportDecl) const;
};

/// GraphTraits specialization so ValueInfo can be used as a call-graph node.
template <> struct GraphTraits<ValueInfo> {
  /// Graph node type for a value in the module summary index.
  typedef ValueInfo NodeRef;
  /// Reference to a call-graph edge (callee ValueInfo plus CalleeInfo).
  using EdgeRef = FunctionSummary::EdgeTy &;

  /// Return the callee ValueInfo from a call-graph edge.
  /// \param P The call-graph edge.
  /// @return The callee ValueInfo from a call-graph edge.
  static NodeRef valueInfoFromEdge(FunctionSummary::EdgeTy &P) {
    return P.first;
  }
  /// Iterator over child ValueInfo nodes reached by call-graph edges.
  using ChildIteratorType =
      mapped_iterator<SmallVector<FunctionSummary::EdgeTy, 0>::iterator,
                      decltype(&valueInfoFromEdge)>;

  /// Iterator over the raw call-graph edges of a function summary.
  using ChildEdgeIteratorType =
      SmallVector<FunctionSummary::EdgeTy, 0>::iterator;

  /// Return \p V as the entry node for graph algorithms.
  /// \param V The value info used as the graph entry.
  /// @return \p V as the entry node for graph algorithms.
  static NodeRef getEntryNode(ValueInfo V) { return V; }

  /// Return an iterator to the first callee of \p N.
  /// \param N The call-graph node whose children are iterated.
  /// @return An iterator to the first callee of \p N.
  static ChildIteratorType child_begin(NodeRef N) {
    if (!N.getSummaryList().size()) // handle external function
      return ChildIteratorType(
          FunctionSummary::ExternalNode.CallGraphEdgeList.begin(),
          &valueInfoFromEdge);
    FunctionSummary *F =
        cast<FunctionSummary>(N.getSummaryList().front()->getBaseObject());
    return ChildIteratorType(F->CallGraphEdgeList.begin(), &valueInfoFromEdge);
  }

  /// Return an iterator past the last callee of \p N.
  /// \param N The call-graph node whose children are iterated.
  /// @return An iterator past the last callee of \p N.
  static ChildIteratorType child_end(NodeRef N) {
    if (!N.getSummaryList().size()) // handle external function
      return ChildIteratorType(
          FunctionSummary::ExternalNode.CallGraphEdgeList.end(),
          &valueInfoFromEdge);
    FunctionSummary *F =
        cast<FunctionSummary>(N.getSummaryList().front()->getBaseObject());
    return ChildIteratorType(F->CallGraphEdgeList.end(), &valueInfoFromEdge);
  }

  /// Return an iterator to the first call-graph edge of \p N.
  /// \param N The call-graph node whose edges are iterated.
  /// @return An iterator to the first call-graph edge of \p N.
  static ChildEdgeIteratorType child_edge_begin(NodeRef N) {
    if (!N.getSummaryList().size()) // handle external function
      return FunctionSummary::ExternalNode.CallGraphEdgeList.begin();

    FunctionSummary *F =
        cast<FunctionSummary>(N.getSummaryList().front()->getBaseObject());
    return F->CallGraphEdgeList.begin();
  }

  /// Return an iterator past the last call-graph edge of \p N.
  /// \param N The call-graph node whose edges are iterated.
  /// @return An iterator past the last call-graph edge of \p N.
  static ChildEdgeIteratorType child_edge_end(NodeRef N) {
    if (!N.getSummaryList().size()) // handle external function
      return FunctionSummary::ExternalNode.CallGraphEdgeList.end();

    FunctionSummary *F =
        cast<FunctionSummary>(N.getSummaryList().front()->getBaseObject());
    return F->CallGraphEdgeList.end();
  }

  /// Return the destination ValueInfo of call-graph edge \p E.
  /// \param E The call-graph edge.
  /// @return The destination ValueInfo of call-graph edge \p E.
  static NodeRef edge_dest(EdgeRef E) { return E.first; }
};

/// GraphTraits specialization so ModuleSummaryIndex can be traversed as a graph.
template <>
struct GraphTraits<ModuleSummaryIndex *> : public GraphTraits<ValueInfo> {
  /// Return a synthetic call-graph root that edges to all root functions.
  /// \param I The module summary index to traverse.
  /// @return A synthetic call-graph root that edges to all root functions.
  static NodeRef getEntryNode(ModuleSummaryIndex *I) {
    std::unique_ptr<GlobalValueSummary> Root =
        std::make_unique<FunctionSummary>(I->calculateCallGraphRoot());
    GlobalValueSummaryInfo G(I->haveGVs());
    G.addSummary(std::move(Root));
    static auto P =
        GlobalValueSummaryMapTy::value_type(GlobalValue::GUID(0), std::move(G));
    return ValueInfo(I->haveGVs(), &P);
  }
};
} // end namespace llvm

#endif // LLVM_IR_MODULESUMMARYINDEX_H
