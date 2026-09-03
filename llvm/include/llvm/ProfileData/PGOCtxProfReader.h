//===--- PGOCtxProfReader.h - Contextual profile reader ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
///
/// Reader for contextual iFDO profile, which comes in bitstream format.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_PROFILEDATA_CTXINSTRPROFILEREADER_H
#define LLVM_PROFILEDATA_CTXINSTRPROFILEREADER_H

#include "llvm/Bitstream/BitstreamReader.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/ProfileData/PGOCtxProfWriter.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <map>

namespace llvm {
class PGOContextualProfile;
class PGOCtxProfContext;

namespace internal {
// When we traverse the contextual profile, we typically want to visit contexts
// pertaining to a specific function. To avoid traversing the whole tree, we
// want to keep a per-function list - which will be in preorder - of that
// function's contexts. This happens in PGOContextualProfile. For memory use
// efficiency, we want to make PGOCtxProfContext an intrusive double-linked list
// node. We need to handle the cases where PGOCtxProfContext nodes are moved and
// deleted: in both cases, we need to update the index (==list). We can do that
// directly from the node in the list, without knowing who the "parent" of the
// list is. That makes the ADT ilist overkill here. Finally, IndexNode is meant
// to be an implementation detail of PGOCtxProfContext, and the only reason it's
// factored out is to avoid implementing move semantics for all its members.
class IndexNode {
  // This class' members are intentionally private - it's a convenience
  // implementation detail.
  friend class ::llvm::PGOCtxProfContext;
  friend class ::llvm::PGOContextualProfile;

  IndexNode *Previous = nullptr;
  IndexNode *Next = nullptr;

  ~IndexNode() {
    if (Next)
      Next->Previous = Previous;
    if (Previous)
      Previous->Next = Next;
  }

  IndexNode(const IndexNode &Other) = delete;

  IndexNode(IndexNode &&Other) {
    // Copy the neighbor info
    Next = Other.Next;
    Previous = Other.Previous;

    // Update the neighbors to point to this object
    if (Other.Next)
      Other.Next->Previous = this;
    if (Other.Previous)
      Other.Previous->Next = this;

    // Make sure the dtor is a noop
    Other.Next = nullptr;
    Other.Previous = nullptr;
  }
  IndexNode() = default;
};
} // namespace internal

// Setting initial capacity to 1 because all contexts must have at least 1
// counter, and then, because all contexts belonging to a function have the same
// size, there'll be at most one other heap allocation.
/// Flat profile: per-GUID counter vectors without contextual nesting.
using CtxProfFlatProfile =
    std::map<GlobalValue::GUID, SmallVector<uint64_t, 1>>;

/// A mutable node in a loaded contextual profile.
///
/// Suitable for mutation during IPO passes. We generally expect a fraction of
/// counters and callsites to be populated. We continue to model counters as
/// vectors, but callsites are modeled as a map of a map. The expectation is
/// that, typically, there is a small number of indirect targets (usually, 1 for
/// direct calls); but potentially a large number of callsites, and, as inlining
/// progresses, the callsite count of a caller will grow.
class PGOCtxProfContext final : public internal::IndexNode {
public:
  /// Map from callee GUID to the callee's context under one callsite.
  using CallTargetMapTy = std::map<GlobalValue::GUID, PGOCtxProfContext>;
  /// Map from callsite index to the set of call targets at that site.
  using CallsiteMapTy = std::map<uint32_t, CallTargetMapTy>;

private:
  friend class PGOCtxProfileReader;
  friend class PGOContextualProfile;

  GlobalValue::GUID GUID = 0;
  SmallVector<uint64_t, 16> Counters;
  const std::optional<uint64_t> RootEntryCount{};
  std::optional<CtxProfFlatProfile> Unhandled{};
  CallsiteMapTy Callsites;

  PGOCtxProfContext(
      GlobalValue::GUID G, SmallVectorImpl<uint64_t> &&Counters,
      std::optional<uint64_t> RootEntryCount = std::nullopt,
      std::optional<CtxProfFlatProfile> &&Unhandled = std::nullopt)
      : GUID(G), Counters(std::move(Counters)), RootEntryCount(RootEntryCount),
        Unhandled(std::move(Unhandled)) {
    assert(RootEntryCount.has_value() == Unhandled.has_value());
  }

  Expected<PGOCtxProfContext &>
  getOrEmplace(uint32_t Index, GlobalValue::GUID G,
               SmallVectorImpl<uint64_t> &&Counters);

  // Create a bogus context object, used for anchoring the index double linked
  // list - see IndexNode
  PGOCtxProfContext() = default;

public:
  /// Copy construction is deleted; contexts are moved, not copied.
  /// @param Other Unused; copy construction is deleted.
  PGOCtxProfContext(const PGOCtxProfContext &Other) = delete;
  /// Copy assignment is deleted; contexts are moved, not copied.
  /// @param Other Unused; copy assignment is deleted.
  PGOCtxProfContext &operator=(const PGOCtxProfContext &Other) = delete;
  /// Move-construct a context, transferring counters and callsites.
  /// @param Other Context to move from.
  PGOCtxProfContext(PGOCtxProfContext &&Other) = default;
  /// Move assignment is deleted; use move construction instead.
  /// @param Other Unused; move assignment is deleted.
  PGOCtxProfContext &operator=(PGOCtxProfContext &&Other) = delete;

  /// Return the GUID of the function this context belongs to.
  /// @return GUID of the function this context belongs to.
  GlobalValue::GUID guid() const { return GUID; }
  /// Return the counter vector for this context.
  /// @return Const reference to the counter vector.
  const SmallVectorImpl<uint64_t> &counters() const { return Counters; }
  /// Return the mutable counter vector for this context.
  /// @return Mutable reference to the counter vector.
  SmallVectorImpl<uint64_t> &counters() { return Counters; }

  /// Return true if this context is a profile root.
  /// @return True if this context is a profile root.
  bool isRoot() const { return RootEntryCount.has_value(); }
  /// Return the total entry count recorded for this root context.
  /// @return Total entry count recorded for this root context.
  uint64_t getTotalRootEntryCount() const { return RootEntryCount.value(); }

  /// Return flat profiles for contexts not attached under this root.
  /// @return Flat profiles for contexts not attached under this root.
  const CtxProfFlatProfile &getUnhandled() const { return Unhandled.value(); }

  /// Return the entry basic-block counter for this context.
  /// @return Entry basic-block counter for this context.
  uint64_t getEntrycount() const {
    assert(!Counters.empty() &&
           "Functions are expected to have at their entry BB instrumented, so "
           "there should always be at least 1 counter.");
    return Counters[0];
  }

  /// Return the map of callsites under this context.
  /// @return Const reference to the callsite map.
  const CallsiteMapTy &callsites() const { return Callsites; }
  /// Return the mutable map of callsites under this context.
  /// @return Mutable reference to the callsite map.
  CallsiteMapTy &callsites() { return Callsites; }

  /// Attach \p Other as a callee context under callsite \p CSId.
  /// @param CSId Callsite index that receives the ingested context.
  /// @param Other Context to move under this callsite.
  void ingestContext(uint32_t CSId, PGOCtxProfContext &&Other) {
    callsites()[CSId].emplace(Other.guid(), std::move(Other));
  }

  /// Attach all contexts in \p Other under callsite \p CSId.
  /// @param CSId Callsite index expected to be newly created (e.g. by inlining).
  /// @param Other Map of callee GUID to context to move under this callsite.
  void ingestAllContexts(uint32_t CSId, CallTargetMapTy &&Other) {
    auto [_, Inserted] = callsites().try_emplace(CSId, std::move(Other));
    (void)Inserted;
    assert(Inserted &&
           "CSId was expected to be newly created as result of e.g. inlining");
  }

  /// Grow or shrink the counter vector to \p Size elements.
  /// @param Size New number of counter slots.
  void resizeCounters(uint32_t Size) { Counters.resize(Size); }

  /// Return true if callsite index \p I is present.
  /// @param I Callsite index to query.
  /// @return True if callsite index \p I is present.
  bool hasCallsite(uint32_t I) const {
    return Callsites.find(I) != Callsites.end();
  }

  /// Return the call-target map for callsite index \p I.
  /// @param I Callsite index that must already exist.
  /// @return Const reference to the call-target map at callsite \p I.
  const CallTargetMapTy &callsite(uint32_t I) const {
    assert(hasCallsite(I) && "Callsite not found");
    return Callsites.find(I)->second;
  }

  /// Return the mutable call-target map for callsite index \p I.
  /// @param I Callsite index that must already exist.
  /// @return Mutable reference to the call-target map at callsite \p I.
  CallTargetMapTy &callsite(uint32_t I) {
    assert(hasCallsite(I) && "Callsite not found");
    return Callsites.find(I)->second;
  }

  /// Insert this node's GUID and those of its transitive children into \p Guids.
  ///
  /// Technically, all that is required of `TSetOfGUIDs` is to have an
  /// `insert(GUID)` member.
  /// @param Guids Set that receives this GUID and all transitive child GUIDs.
  template <class TSetOfGUIDs>
  void getContainedGuids(TSetOfGUIDs &Guids) const {
    Guids.insert(GUID);
    for (const auto &[_, Callsite] : Callsites)
      for (const auto &[_, Callee] : Callsite)
        Callee.getContainedGuids(Guids);
  }
};

/// Map from root GUID to its contextual profile tree.
using CtxProfContextualProfiles =
    std::map<GlobalValue::GUID, PGOCtxProfContext>;
/// Loaded contextual profile: rooted contexts plus flat profiles.
struct PGOCtxProfile {
  /// Contextual profile trees keyed by root function GUID.
  CtxProfContextualProfiles Contexts;
  /// Non-contextual (flat) per-function counter profiles.
  CtxProfFlatProfile FlatProfiles;

  /// Construct an empty profile with no contexts or flat data.
  PGOCtxProfile() = default;
  /// Copy construction is deleted; profiles are moved, not copied.
  /// @param Other Unused; copy construction is deleted.
  PGOCtxProfile(const PGOCtxProfile &Other) = delete;
  /// Move-construct a profile, transferring contexts and flat data.
  /// @param Other Profile to move from.
  PGOCtxProfile(PGOCtxProfile &&Other) = default;
  /// Move-assign a profile, transferring contexts and flat data.
  /// @param Other Profile to move from.
  /// @return Reference to this profile after the move assignment.
  PGOCtxProfile &operator=(PGOCtxProfile &&Other) = default;
};

/// Reader for contextual PGO profiles stored in bitstream format.
class PGOCtxProfileReader final {
  StringRef Magic;
  BitstreamCursor Cursor;
  Expected<BitstreamEntry> advance();
  Error readMetadata();
  Error wrongValue(const Twine &Msg);
  Error unsupported(const Twine &Msg);

  Expected<std::pair<std::optional<uint32_t>, PGOCtxProfContext>>
  readProfile(PGOCtxProfileBlockIDs Kind);

  bool tryGetNextKnownBlockID(PGOCtxProfileBlockIDs &ID);
  bool canEnterBlockWithID(PGOCtxProfileBlockIDs ID);
  Error enterBlockWithID(PGOCtxProfileBlockIDs ID);

  Error loadContexts(CtxProfContextualProfiles &P);
  Error loadFlatProfiles(CtxProfFlatProfile &P);
  Error loadFlatProfileList(CtxProfFlatProfile &P);

public:
  /// Construct a reader over the bitstream in \p Buffer.
  /// @param Buffer Profile bitstream, including the container magic prefix.
  PGOCtxProfileReader(StringRef Buffer)
      : Magic(Buffer.substr(0, PGOCtxProfileWriter::ContainerMagic.size())),
        Cursor(Buffer.substr(PGOCtxProfileWriter::ContainerMagic.size())) {}

  /// Load contextual and flat profiles from the bitstream.
  /// @return The loaded profile, or an error if reading fails.
  LLVM_ABI Expected<PGOCtxProfile> loadProfiles();
};

/// Write \p Profile to \p OS as YAML.
/// @param OS Destination stream for the YAML serialization.
/// @param Profile Contextual profile to serialize.
LLVM_ABI void convertCtxProfToYaml(raw_ostream &OS,
                                   const PGOCtxProfile &Profile);
} // namespace llvm
#endif
