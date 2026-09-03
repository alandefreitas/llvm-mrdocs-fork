//===- MemProfRadixTree.h - MemProf format support ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A custom Radix Tree builder for memprof data to optimize for space.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_PROFILEDATA_MEMPROFRADIXTREE_H
#define LLVM_PROFILEDATA_MEMPROFRADIXTREE_H

#include "llvm/ADT/BitVector.h"
#include "llvm/ProfileData/IndexedMemProfData.h"
#include "llvm/ProfileData/MemProf.h"
#include "llvm/Support/Compiler.h"

#include <optional>

namespace llvm {
namespace memprof {
namespace detail {
// "Dereference" the iterator from DenseMap or OnDiskChainedHashTable.  We have
// to do so in one of two different ways depending on the type of the hash
// table.
template <typename value_type, typename IterTy>
value_type DerefIterator(IterTy Iter) {
  using deref_type = llvm::remove_cvref_t<decltype(*Iter)>;
  if constexpr (std::is_same_v<deref_type, value_type>)
    return *Iter;
  else
    return Iter->second;
}
} // namespace detail

/// Function object that returns a Frame for a given FrameId.
template <typename MapTy> struct FrameIdConverter {
  /// Most recent FrameId that was not found in Map, if any.
  std::optional<FrameId> LastUnmappedId;
  /// Map from FrameId to Frame used for lookups.
  MapTy &Map;

  /// Deleted; a map reference is required.
  FrameIdConverter() = delete;
  /// Construct a converter that looks up frames in \p Map.
  /// @param Map FrameId-to-Frame map to query.
  FrameIdConverter(MapTy &Map) : Map(Map) {}

  /// Deleted so copies cannot diverge on LastUnmappedId.
  /// @param Other Unused; copy construction is deleted.
  FrameIdConverter(const FrameIdConverter &Other) = delete;
  /// Deleted so copies cannot diverge on LastUnmappedId.
  /// @param Other Unused; copy assignment is deleted.
  FrameIdConverter &operator=(const FrameIdConverter &Other) = delete;

  /// Return the Frame for \p Id, or an empty Frame if unmapped.
  /// @param Id Frame identifier to look up.
  /// @return Frame for \p Id, or an empty Frame if unmapped.
  Frame operator()(FrameId Id) {
    auto Iter = Map.find(Id);
    if (Iter == Map.end()) {
      LastUnmappedId = Id;
      return Frame();
    }
    return detail::DerefIterator<Frame>(Iter);
  }
};

/// Function object that returns a call stack for a given CallStackId.
template <typename MapTy> struct CallStackIdConverter {
  /// Most recent CallStackId that was not found in Map, if any.
  std::optional<CallStackId> LastUnmappedId;
  /// Map from CallStackId to a sequence of FrameIds.
  MapTy &Map;
  /// Functor that converts a FrameId to a Frame.
  llvm::function_ref<Frame(FrameId)> FrameIdToFrame;

  /// Deleted; a map and frame converter are required.
  CallStackIdConverter() = delete;
  /// Construct a converter that looks up call stacks in \p Map.
  /// @param Map CallStackId-to-FrameId-sequence map to query.
  /// @param FrameIdToFrame Functor that converts each FrameId to a Frame.
  CallStackIdConverter(MapTy &Map,
                       llvm::function_ref<Frame(FrameId)> FrameIdToFrame)
      : Map(Map), FrameIdToFrame(FrameIdToFrame) {}

  /// Deleted so copies cannot diverge on LastUnmappedId.
  /// @param Other Unused; copy construction is deleted.
  CallStackIdConverter(const CallStackIdConverter &Other) = delete;
  /// Deleted so copies cannot diverge on LastUnmappedId.
  /// @param Other Unused; copy assignment is deleted.
  CallStackIdConverter &operator=(const CallStackIdConverter &Other) = delete;

  /// Return the Frames for \p CSId, or an empty vector if unmapped.
  /// @param CSId Call stack identifier to look up.
  /// @return Frames for \p CSId, or an empty vector if unmapped.
  std::vector<Frame> operator()(CallStackId CSId) {
    std::vector<Frame> Frames;
    auto CSIter = Map.find(CSId);
    if (CSIter == Map.end()) {
      LastUnmappedId = CSId;
    } else {
      llvm::SmallVector<FrameId> CS =
          detail::DerefIterator<llvm::SmallVector<FrameId>>(CSIter);
      Frames.reserve(CS.size());
      for (FrameId Id : CS)
        Frames.push_back(FrameIdToFrame(Id));
    }
    return Frames;
  }
};

/// Function object that returns a Frame stored at an index into the profile.
struct LinearFrameIdConverter {
  /// Base address of the serialized Frame array in the profile.
  const unsigned char *FrameBase;

  /// Deleted; a frame array base address is required.
  LinearFrameIdConverter() = delete;
  /// Construct a converter for frames starting at \p FrameBase.
  /// @param FrameBase Base of the serialized Frame array.
  LinearFrameIdConverter(const unsigned char *FrameBase)
      : FrameBase(FrameBase) {}

  /// Deserialize and return the Frame at linear index \p LinearId.
  /// @param LinearId Index into the Frame array.
  /// @return Frame deserialized from the array at \p LinearId.
  Frame operator()(LinearFrameId LinearId) {
    uint64_t Offset = static_cast<uint64_t>(LinearId) * Frame::serializedSize();
    return Frame::deserialize(FrameBase + Offset);
  }
};

/// Function object that returns a call stack stored in the profile array.
struct LinearCallStackIdConverter {
  /// Base address of the serialized call stack (radix tree) array.
  const unsigned char *CallStackBase;
  /// Functor that converts a LinearFrameId to a Frame.
  llvm::function_ref<Frame(LinearFrameId)> FrameIdToFrame;

  /// Deleted; a call stack base and frame converter are required.
  LinearCallStackIdConverter() = delete;
  /// Construct a converter for call stacks starting at \p CallStackBase.
  /// @param CallStackBase Base of the serialized call stack array.
  /// @param FrameIdToFrame Functor that converts each LinearFrameId to a Frame.
  LinearCallStackIdConverter(
      const unsigned char *CallStackBase,
      llvm::function_ref<Frame(LinearFrameId)> FrameIdToFrame)
      : CallStackBase(CallStackBase), FrameIdToFrame(FrameIdToFrame) {}

  /// Return the Frames for the call stack at linear index \p LinearCSId.
  /// @param LinearCSId Index into the call stack radix tree array.
  /// @return Frames for the call stack at \p LinearCSId.
  std::vector<Frame> operator()(LinearCallStackId LinearCSId) {
    std::vector<Frame> Frames;

    const unsigned char *Ptr =
        CallStackBase +
        static_cast<uint64_t>(LinearCSId) * sizeof(LinearFrameId);
    uint32_t NumFrames =
        support::endian::readNext<uint32_t, llvm::endianness::little>(Ptr);
    Frames.reserve(NumFrames);
    for (; NumFrames; --NumFrames) {
      LinearFrameId Elem =
          support::endian::read<LinearFrameId, llvm::endianness::little>(Ptr);
      // Follow a pointer to the parent, if any.  See comments below on
      // CallStackRadixTreeBuilder for the description of the radix tree format.
      if (static_cast<std::make_signed_t<LinearFrameId>>(Elem) < 0) {
        Ptr += (-Elem) * sizeof(LinearFrameId);
        Elem =
            support::endian::read<LinearFrameId, llvm::endianness::little>(Ptr);
      }
      // We shouldn't encounter another pointer.
      assert(static_cast<std::make_signed_t<LinearFrameId>>(Elem) >= 0);
      Frames.push_back(FrameIdToFrame(Elem));
      Ptr += sizeof(LinearFrameId);
    }

    return Frames;
  }
};

/// Extracts caller-callee pairs from the call stack radix tree array.
///
/// The leaf frame is assumed to call a heap allocation function with GUID 0.
/// The resulting pairs are accumulated in CallerCalleePairs. Users can take it
/// with:
///
///   auto Pairs = std::move(Extractor.CallerCalleePairs);
struct CallerCalleePairExtractor {
  /// The base address of the radix tree array.
  const unsigned char *CallStackBase;
  /// A functor to convert a linear FrameId to a Frame.
  llvm::function_ref<Frame(LinearFrameId)> FrameIdToFrame;
  /// A map from caller GUIDs to lists of call sites in respective callers.
  DenseMap<uint64_t, SmallVector<CallEdgeTy, 0>> CallerCalleePairs;

  /// The set of linear call stack IDs that we've visited.
  BitVector Visited;

  /// Deleted; a radix tree base, frame converter, and size are required.
  CallerCalleePairExtractor() = delete;
  /// Construct an extractor over a radix tree of size \p RadixTreeSize.
  /// @param CallStackBase Base of the serialized call stack radix tree array.
  /// @param FrameIdToFrame Functor that converts each LinearFrameId to a Frame.
  /// @param RadixTreeSize Number of LinearFrameId slots in the radix tree.
  CallerCalleePairExtractor(
      const unsigned char *CallStackBase,
      llvm::function_ref<Frame(LinearFrameId)> FrameIdToFrame,
      unsigned RadixTreeSize)
      : CallStackBase(CallStackBase), FrameIdToFrame(FrameIdToFrame),
        Visited(RadixTreeSize) {}

  /// Extract caller-callee pairs along the call stack at \p LinearCSId.
  /// @param LinearCSId Index into the call stack radix tree array.
  void operator()(LinearCallStackId LinearCSId) {
    const unsigned char *Ptr =
        CallStackBase +
        static_cast<uint64_t>(LinearCSId) * sizeof(LinearFrameId);
    uint32_t NumFrames =
        support::endian::readNext<uint32_t, llvm::endianness::little>(Ptr);
    // The leaf frame calls a function with GUID 0.
    uint64_t CalleeGUID = 0;
    for (; NumFrames; --NumFrames) {
      LinearFrameId Elem =
          support::endian::read<LinearFrameId, llvm::endianness::little>(Ptr);
      // Follow a pointer to the parent, if any.  See comments below on
      // CallStackRadixTreeBuilder for the description of the radix tree format.
      if (static_cast<std::make_signed_t<LinearFrameId>>(Elem) < 0) {
        Ptr += (-Elem) * sizeof(LinearFrameId);
        Elem =
            support::endian::read<LinearFrameId, llvm::endianness::little>(Ptr);
      }
      // We shouldn't encounter another pointer.
      assert(static_cast<std::make_signed_t<LinearFrameId>>(Elem) >= 0);

      // Add a new caller-callee pair.
      Frame F = FrameIdToFrame(Elem);
      uint64_t CallerGUID = F.Function;
      LineLocation Loc(F.LineOffset, F.Column);
      CallerCalleePairs[CallerGUID].emplace_back(Loc, CalleeGUID);

      // Keep track of the indices we've visited.  If we've already visited the
      // current one, terminate the traversal.  We will not discover any new
      // caller-callee pair by continuing the traversal.
      unsigned Offset =
          std::distance(CallStackBase, Ptr) / sizeof(LinearFrameId);
      if (Visited.test(Offset))
        break;
      Visited.set(Offset);

      Ptr += sizeof(LinearFrameId);
      CalleeGUID = CallerGUID;
    }
  }
};

/// Convenience wrapper around FrameIdConverter and CallStackIdConverter for
/// tests.
struct IndexedCallstackIdConverter {
  /// Deleted; IndexedMemProfData is required.
  IndexedCallstackIdConverter() = delete;
  /// Construct converters over the Frames and CallStacks in \p MemProfData.
  /// @param MemProfData Indexed MemProf frames and call stacks to wrap.
  IndexedCallstackIdConverter(IndexedMemProfData &MemProfData)
      : FrameIdConv(MemProfData.Frames),
        CSIdConv(MemProfData.CallStacks, FrameIdConv) {}

  /// Deleted so copies cannot diverge on LastUnmappedId.
  /// @param Other Unused; copy construction is deleted.
  IndexedCallstackIdConverter(const IndexedCallstackIdConverter &Other) =
      delete;
  /// Deleted so copies cannot diverge on LastUnmappedId.
  /// @param Other Unused; copy assignment is deleted.
  IndexedCallstackIdConverter &
  operator=(const IndexedCallstackIdConverter &Other) = delete;

  /// Return the Frames for call stack \p CSId.
  /// @param CSId Call stack identifier to look up.
  /// @return Frames for \p CSId.
  std::vector<Frame> operator()(CallStackId CSId) { return CSIdConv(CSId); }

  /// FrameId-to-Frame converter over MemProfData.Frames.
  FrameIdConverter<decltype(IndexedMemProfData::Frames)> FrameIdConv;
  /// CallStackId-to-Frames converter over MemProfData.CallStacks.
  CallStackIdConverter<decltype(IndexedMemProfData::CallStacks)> CSIdConv;
};

/// Aggregate occurrence statistics for a single FrameId across call stacks.
struct FrameStat {
  /// The number of occurrences of a given FrameId.
  uint64_t Count = 0;
  /// The sum of indexes where a given FrameId shows up.
  uint64_t PositionSum = 0;
};

/// Compute a histogram of Frames in call stacks.
/// @param MemProfCallStackData Mapping from CallStackId to frame id sequences.
/// @return Per-frame occurrence counts and position sums.
template <typename FrameIdTy>
llvm::DenseMap<FrameIdTy, FrameStat>
computeFrameHistogram(llvm::MapVector<CallStackId, llvm::SmallVector<FrameIdTy>>
                          &MemProfCallStackData);

/// Builder that compresses call stacks into a serialized radix tree array.
///
/// A set of call stacks might look like:
///
/// CallStackId 1:  f1 -> f2 -> f3
/// CallStackId 2:  f1 -> f2 -> f4 -> f5
/// CallStackId 3:  f1 -> f2 -> f4 -> f6
/// CallStackId 4:  f7 -> f8 -> f9
///
/// where each fn refers to a stack frame.
///
/// Since we expect a lot of common prefixes, we can compress the call stacks
/// into a radix tree like:
///
/// CallStackId 1:  f1 -> f2 -> f3
///                       |
/// CallStackId 2:        +---> f4 -> f5
///                             |
/// CallStackId 3:              +---> f6
///
/// CallStackId 4:  f7 -> f8 -> f9
///
/// Now, we are interested in retrieving call stacks for a given CallStackId, so
/// we just need a pointer from a given call stack to its parent.  For example,
/// CallStackId 2 would point to CallStackId 1 as a parent.
///
/// We serialize the radix tree above into a single array along with the length
/// of each call stack and pointers to the parent call stacks.
///
/// Index:              0  1  2  3  4  5  6  7  8  9 10 11 12 13 14
/// Array:             L3 f9 f8 f7 L4 f6 J3 L4 f5 f4 J3 L3 f3 f2 f1
///                     ^           ^        ^           ^
///                     |           |        |           |
/// CallStackId 4:  0 --+           |        |           |
/// CallStackId 3:  4 --------------+        |           |
/// CallStackId 2:  7 -----------------------+           |
/// CallStackId 1: 11 -----------------------------------+
///
/// - LN indicates the length of a call stack, encoded as ordinary integer N.
///
/// - JN indicates a pointer to the parent, encoded as -N.
///
/// The radix tree allows us to reconstruct call stacks in the leaf-to-root
/// order as we scan the array from left ro right while following pointers to
/// parents along the way.
///
/// For example, if we are decoding CallStackId 2, we start a forward traversal
/// at Index 7, noting the call stack length of 4 and obtaining f5 and f4.  When
/// we see J3 at Index 10, we resume a forward traversal at Index 13 = 10 + 3,
/// picking up f2 and f1.  We are done after collecting 4 frames as indicated at
/// the beginning of the traversal.
///
/// On-disk IndexedMemProfRecord will refer to call stacks by their indexes into
/// the radix tree array, so we do not explicitly encode mappings like:
/// "CallStackId 1 -> 11".
template <typename FrameIdTy> class CallStackRadixTreeBuilder {
  // The radix tree array.
  std::vector<LinearFrameId> RadixArray;

  // Mapping from CallStackIds to indexes into RadixArray.
  llvm::DenseMap<CallStackId, LinearCallStackId> CallStackPos;

  // In build, we partition a given call stack into two parts -- the prefix
  // that's common with the previously encoded call stack and the frames beyond
  // the common prefix -- the unique portion.  Then we want to find out where
  // the common prefix is stored in RadixArray so that we can link the unique
  // portion to the common prefix.  Indexes, declared below, helps with our
  // needs.  Intuitively, Indexes tells us where each of the previously encoded
  // call stack is stored in RadixArray.  More formally, Indexes satisfies:
  //
  //   RadixArray[Indexes[I]] == Prev[I]
  //
  // for every I, where Prev is the the call stack in the root-to-leaf order
  // previously encoded by build.  (Note that Prev, as passed to
  // encodeCallStack, is in the leaf-to-root order.)
  //
  // For example, if the call stack being encoded shares 5 frames at the root of
  // the call stack with the previously encoded call stack,
  // RadixArray[Indexes[0]] is the root frame of the common prefix.
  // RadixArray[Indexes[5 - 1]] is the last frame of the common prefix.
  std::vector<LinearCallStackId> Indexes;

  using CSIdPair = std::pair<CallStackId, llvm::SmallVector<FrameIdTy>>;

  // Encode a call stack into RadixArray.  Return the starting index within
  // RadixArray.
  LinearCallStackId encodeCallStack(
      const llvm::SmallVector<FrameIdTy> *CallStack,
      const llvm::SmallVector<FrameIdTy> *Prev,
      const llvm::DenseMap<FrameIdTy, LinearFrameId> *MemProfFrameIndexes);

public:
  /// Construct an empty radix tree builder.
  CallStackRadixTreeBuilder() = default;

  /// Build a radix tree array from \p MemProfCallStackData.
  /// @param MemProfCallStackData Call stacks to encode; contents are consumed.
  /// @param MemProfFrameIndexes Optional map from FrameIdTy to linear frame ids.
  /// @param FrameHistogram Frame occurrence histogram used for ordering.
  void
  build(llvm::MapVector<CallStackId, llvm::SmallVector<FrameIdTy>>
            &&MemProfCallStackData,
        const llvm::DenseMap<FrameIdTy, LinearFrameId> *MemProfFrameIndexes,
        llvm::DenseMap<FrameIdTy, FrameStat> &FrameHistogram);

  /// Return a view of the constructed radix tree array.
  /// @return View of the constructed radix tree array.
  ArrayRef<LinearFrameId> getRadixArray() const { return RadixArray; }

  /// Take ownership of the CallStackId-to-radix-index mapping.
  /// @return CallStackId-to-radix-index mapping, moved out of the builder.
  llvm::DenseMap<CallStackId, LinearCallStackId> takeCallStackPos() {
    return std::move(CallStackPos);
  }
};

/// Explicit instantiation of CallStackRadixTreeBuilder for FrameId.
extern template class LLVM_TEMPLATE_ABI CallStackRadixTreeBuilder<FrameId>;
/// Explicit instantiation of CallStackRadixTreeBuilder for LinearFrameId.
extern template class LLVM_TEMPLATE_ABI
    CallStackRadixTreeBuilder<LinearFrameId>;

} // namespace memprof
} // namespace llvm
#endif // LLVM_PROFILEDATA_MEMPROFRADIXTREE_H
