//===- PGOCtxProfWriter.h - Contextual Profile Writer -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares a utility for writing a contextual profile to bitstream.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_PROFILEDATA_PGOCTXPROFWRITER_H_
#define LLVM_PROFILEDATA_PGOCTXPROFWRITER_H_

#include "llvm/ADT/StringExtras.h"
#include "llvm/Bitstream/BitCodeEnums.h"
#include "llvm/Bitstream/BitstreamWriter.h"
#include "llvm/ProfileData/CtxInstrContextNode.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
/// Bitstream record codes used in contextual PGO profiles.
enum PGOCtxProfileRecords {
  /// Sentinel; not a valid record code.
  Invalid = 0,
  /// Profile format version record.
  Version,
  /// Function GUID for a context or flat profile.
  Guid,
  /// Callsite index identifying the call edge into a context.
  CallsiteIndex,
  /// Counter vector for a context or flat profile.
  Counters,
  /// Aggregate entry count across all roots of a contextual profile.
  TotalRootEntryCount
};

/// Bitstream block IDs used in contextual PGO profiles.
enum PGOCtxProfileBlockIDs {
  /// First application block ID reserved for contextual profiles.
  FIRST_VALID = bitc::FIRST_APPLICATION_BLOCKID,
  /// Top-level metadata block (holds the version).
  ProfileMetadataBlockID = FIRST_VALID,
  /// Section containing contextual profile roots.
  ContextsSectionBlockID = ProfileMetadataBlockID + 1,
  /// One contextual profile root and its nested contexts.
  ContextRootBlockID = ContextsSectionBlockID + 1,
  /// One nested context node under a root or another context.
  ContextNodeBlockID = ContextRootBlockID + 1,
  /// Section containing non-contextual (flat) profiles.
  FlatProfilesSectionBlockID = ContextNodeBlockID + 1,
  /// One flat profile for a single GUID.
  FlatProfileBlockID = FlatProfilesSectionBlockID + 1,
  /// Flat profiles for contexts not attached under a root.
  UnhandledBlockID = FlatProfileBlockID + 1,
  /// Last valid contextual-profile block ID.
  LAST_VALID = UnhandledBlockID
};

/// Writer that serializes contextual PGO profiles to a bitstream.
///
/// Write one or more ContextNodes to the provided raw_ostream. The caller must
/// destroy the PGOCtxProfileWriter object before closing the stream.
/// The design allows serializing a bunch of contexts embedded in some other
/// file. The overall format is:
///
///  [... other data written to the stream...]
///  SubBlock(ProfileMetadataBlockID)
///   Version
///   SubBlock(ContextNodeBlockID)
///     [RECORDS]
///     SubBlock(ContextNodeBlockID)
///       [RECORDS]
///       [... more SubBlocks]
///     EndBlock
///   EndBlock
///
/// The "RECORDS" are bitsream records. The IDs are in CtxProfileCodes (except)
/// for Version, which is just for metadata). All contexts will have Guid and
/// Counters, and all but the roots have CalleeIndex. The order in which the
/// records appear does not matter, but they must precede any subcontexts,
/// because that helps keep the reader code simpler.
///
/// Subblock containment captures the context->subcontext relationship. The
/// "next()" relationship in the raw profile, between call targets of indirect
/// calls, are just modeled as peer subblocks where the callee index is the
/// same.
///
/// Versioning: the writer may produce additional records not known by the
/// reader. The version number indicates a more structural change.
/// The current version, in particular, is set up to expect optional extensions
/// like value profiling - which would appear as additional records. For
/// example, value profiling would produce a new record with a new record ID,
/// containing the profiled values (much like the counters)
class LLVM_ABI PGOCtxProfileWriter final : public ctx_profile::ProfileWriter {
  enum class EmptyContextCriteria { None, EntryIsZero, AllAreZero };

  BitstreamWriter Writer;
  const bool IncludeEmpty;

  void writeGuid(ctx_profile::GUID Guid);
  void writeCallsiteIndex(uint32_t Index);
  void writeRootEntryCount(uint64_t EntryCount);
  void writeCounters(ArrayRef<uint64_t> Counters);
  void writeNode(uint32_t CallerIndex, const ctx_profile::ContextNode &Node);
  void writeSubcontexts(const ctx_profile::ContextNode &Node);

public:
  /// Construct a writer that emits into \p Out.
  /// @param Out Destination stream; keep open until this writer is destroyed.
  /// @param VersionOverride Optional profile version to emit instead of
  ///        CurrentVersion.
  /// @param IncludeEmpty If true, write contexts whose counters are all zero.
  PGOCtxProfileWriter(raw_ostream &Out,
                      std::optional<unsigned> VersionOverride = std::nullopt,
                      bool IncludeEmpty = false);
  /// Destroy the writer and exit the open metadata block.
  ~PGOCtxProfileWriter() override { Writer.ExitBlock(); }

  /// Begin writing the contextual profile section.
  void startContextSection() override;
  /// Write one contextual profile root and optional unhandled contexts.
  /// @param RootNode Root context node to serialize.
  /// @param Unhandled Optional list of contexts not attached to a root.
  /// @param TotalRootEntryCount Aggregate entry count for the root.
  void writeContextual(const ctx_profile::ContextNode &RootNode,
                       const ctx_profile::ContextNode *Unhandled,
                       uint64_t TotalRootEntryCount) override;
  /// Finish writing the contextual profile section.
  void endContextSection() override;

  /// Begin writing the flat profile section.
  void startFlatSection() override;
  /// Write one flat profile buffer for \p Guid.
  /// @param Guid Function identifier for the flat profile.
  /// @param Buffer Counter values to write.
  /// @param BufferSize Number of elements in \p Buffer.
  void writeFlat(ctx_profile::GUID Guid, const uint64_t *Buffer,
                 size_t BufferSize) override;
  /// Finish writing the flat profile section.
  void endFlatSection() override;

  /// Bit width of abbreviated record codes written by this format.
  static constexpr unsigned CodeLen = 2;
  /// Default contextual profile format version emitted by the writer.
  static constexpr uint32_t CurrentVersion = 4;
  /// VBR chunk width used when encoding record operands.
  static constexpr unsigned VBREncodingBits = 6;
  /// Four-byte magic prefix identifying a contextual profile container.
  static constexpr StringRef ContainerMagic = "CTXP";
};

/// Convert a YAML contextual profile in \p Profile into bitstream form on \p Out.
/// @param Profile YAML text describing contextual and flat profiles.
/// @param Out Destination stream for the bitstream profile.
/// @return Success, or an error if YAML parsing or conversion fails.
LLVM_ABI Error createCtxProfFromYAML(StringRef Profile, raw_ostream &Out);
} // namespace llvm
#endif
