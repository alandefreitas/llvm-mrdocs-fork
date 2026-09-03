//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares common types and utilities for basic-block address maps.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_BBADDRMAP_H
#define LLVM_OBJECT_BBADDRMAP_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/BlockFrequency.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/UniqueBBID.h"

namespace llvm {
namespace object {

/// BB address map for one function, including ranges and optional PGO data.
struct BBAddrMap {

  /// Optional feature flags controlling extra data encoded in the section.
  ///
  /// The feature list lives in BBAddrMap.def.
  struct Features {
    /// Bit indices for each optional feature flag.
    enum {
/// Expand a feature name into a bit-index enumerator.
/// \param Name Feature flag identifier from BBAddrMap.def.
#define HANDLE_BB_ADDR_MAP_FEATURE(Name) Name##Bit,
#include "llvm/Object/BBAddrMap.def"
      NumBits, ///< Number of defined feature bits.
    };
    static_assert(NumBits <= 16,
                  "BBAddrMap::Features is encoded as a uint16_t");

#define HANDLE_BB_ADDR_MAP_FEATURE(Name) bool Name : 1;
#include "llvm/Object/BBAddrMap.def"

    /// Mask of all defined feature bits in the encoded uint16_t value.
    static constexpr uint16_t KnownMask =
        (static_cast<uint16_t>(1) << NumBits) - 1;

    /// Return true if any PGO analysis feature is enabled.
    /// \return True if any PGO analysis feature is enabled.
    bool hasPGOAnalysis() const { return FuncEntryCount || BBFreq || BrProb; }

    /// Return true if any per-basic-block PGO analysis feature is enabled.
    /// \return True if any per-basic-block PGO analysis feature is enabled.
    bool hasPGOAnalysisBBData() const { return BBFreq || BrProb; }

    /// Encode the feature flags into a packed uint16_t value.
    /// \return Packed feature flags as a uint16_t.
    uint16_t encode() const {
      uint16_t V = 0;
#define HANDLE_BB_ADDR_MAP_FEATURE(Name)                                       \
  V |= static_cast<uint16_t>(Name) << Name##Bit;
#include "llvm/Object/BBAddrMap.def"
      return V;
    }

    /// Decode feature flags from a packed uint16_t, rejecting unknown bits.
    /// \param Val Encoded feature bitfield to decode.
    /// \return Decoded Features, or an error if unknown bits are set.
    static Expected<Features> decode(uint16_t Val) {
      Features Feat{
#define HANDLE_BB_ADDR_MAP_FEATURE(Name)                                       \
  static_cast<bool>(Val & (uint16_t{1} << Name##Bit)),
#include "llvm/Object/BBAddrMap.def"
      };
      if (Feat.encode() != Val)
        return createStringError(
            "invalid encoding for BBAddrMap::Features: 0x%x", Val);
      return Feat;
    }

    /// Return true if this equals \p Other.
    /// \param Other Features value to compare against.
    /// \return True if the feature flags are equal.
    bool operator==(const Features &Other) const {
      return encode() == Other.encode();
    }
  };

  /// Address-map entry describing one basic block.
  struct BBEntry {
    /// Compact metadata flags for a basic block.
    struct Metadata {
      /// Bit indices for each basic-block metadata flag.
      enum {
#define HANDLE_BB_ADDR_MAP_BB_METADATA(Name)                                   \
  Name##Bit, ///< Metadata flag bit index.
#include "llvm/Object/BBAddrMap.def"
        NumBits, ///< Number of defined metadata bits.
      };
      static_assert(NumBits <= 32,
                    "BBAddrMap::BBEntry::Metadata is encoded as a uint32_t");

#define HANDLE_BB_ADDR_MAP_BB_METADATA(Name) bool Name : 1;
#include "llvm/Object/BBAddrMap.def"

      /// Return true if this equals \p Other.
      /// \param Other Metadata value to compare against.
      /// \return True if the metadata flags are equal.
      bool operator==(const Metadata &Other) const {
        return encode() == Other.encode();
      }

      /// Encode this metadata as a packed uint32_t value.
      /// \return Packed metadata flags as a uint32_t.
      uint32_t encode() const {
        uint32_t V = 0;
#define HANDLE_BB_ADDR_MAP_BB_METADATA(Name)                                   \
  V |= static_cast<uint32_t>(Name) << Name##Bit;
#include "llvm/Object/BBAddrMap.def"
        return V;
      }

      /// Decode metadata flags from a packed uint32_t, rejecting unknown bits.
      /// \param V Encoded metadata bitfield to decode.
      /// \return Decoded Metadata, or an error if unknown bits are set.
      static Expected<Metadata> decode(uint32_t V) {
        Metadata MD{
#define HANDLE_BB_ADDR_MAP_BB_METADATA(Name)                                   \
  static_cast<bool>(V & (uint32_t{1} << Name##Bit)),
#include "llvm/Object/BBAddrMap.def"
        };
        if (MD.encode() != V)
          return createStringError(
              "invalid encoding for BBEntry::Metadata: 0x%x", V);
        return MD;
      }
    };

    /// Unique ID of this basic block.
    uint32_t ID = 0;
    /// Offset of the basic block relative to the range base address.
    uint32_t Offset = 0;
    /// Size of the basic block in bytes.
    uint32_t Size = 0;
    /// Metadata flags for this basic block.
    Metadata MD = {false, false, false, false, false};
    /// Offsets of call instruction ends, relative to the basic block start.
    SmallVector<uint32_t, 1> CallsiteEndOffsets;
    /// Hash of this basic block.
    uint64_t Hash = 0;

    /// Construct a basic-block entry with the given fields.
    /// \param ID Unique basic-block identifier.
    /// \param Offset Offset from the range base address.
    /// \param Size Size of the basic block in bytes.
    /// \param MD Metadata flags for the basic block.
    /// \param CallsiteEndOffsets Call-end offsets relative to the block start.
    /// \param Hash Hash of the basic block.
    BBEntry(uint32_t ID, uint32_t Offset, uint32_t Size, Metadata MD,
            SmallVector<uint32_t, 1> CallsiteEndOffsets, uint64_t Hash)
        : ID(ID), Offset(Offset), Size(Size), MD(MD),
          CallsiteEndOffsets(std::move(CallsiteEndOffsets)), Hash(Hash) {}

    /// Return this basic block's unique ID.
    /// \return Unique basic-block identifier.
    UniqueBBID getID() const { return {ID, 0}; }

    /// Return true if this equals \p Other.
    /// \param Other Basic-block entry to compare against.
    /// \return True if the entries are equal.
    bool operator==(const BBEntry &Other) const {
      return ID == Other.ID && Offset == Other.Offset && Size == Other.Size &&
             MD == Other.MD && CallsiteEndOffsets == Other.CallsiteEndOffsets &&
             Hash == Other.Hash;
    }

    /// Return true if this basic block ends with a return.
    /// \return True if the block ends with a return.
    bool hasReturn() const { return MD.HasReturn; }
    /// Return true if this basic block ends with a tail call.
    /// \return True if the block ends with a tail call.
    bool hasTailCall() const { return MD.HasTailCall; }
    /// Return true if this basic block is an exception-handling pad.
    /// \return True if the block is an exception-handling pad.
    bool isEHPad() const { return MD.IsEHPad; }
    /// Return true if this basic block can fall through to the next.
    /// \return True if the block can fall through to the next.
    bool canFallThrough() const { return MD.CanFallThrough; }
    /// Return true if this basic block ends with an indirect branch.
    /// \return True if the block ends with an indirect branch.
    bool hasIndirectBranch() const { return MD.HasIndirectBranch; }
  };

  /// Contiguous range of basic blocks (a function or a basic-block section).
  struct BBRangeEntry {
    /// Base address of this basic-block range.
    uint64_t BaseAddress = 0;
    /// Basic-block entries belonging to this range.
    std::vector<BBEntry> BBEntries;

    /// Return true if this equals \p Other.
    /// \param Other Range entry to compare against.
    /// \return True if the ranges are equal.
    bool operator==(const BBRangeEntry &Other) const {
      return BaseAddress == Other.BaseAddress && BBEntries == Other.BBEntries;
    }
  };

  /// All ranges for this function; the first is always the function entry.
  std::vector<BBRangeEntry> BBRanges;

  /// Return the function address stored as the first range's base address.
  /// \return Base address of the first range.
  uint64_t getFunctionAddress() const {
    assert(!BBRanges.empty());
    return BBRanges.front().BaseAddress;
  }

  /// Return the total number of basic-block entries across all ranges.
  /// \return Total number of basic-block entries.
  size_t getNumBBEntries() const {
    size_t NumBBEntries = 0;
    for (const auto &BBR : BBRanges)
      NumBBEntries += BBR.BBEntries.size();
    return NumBBEntries;
  }

  /// Return the index of the range with \p BaseAddress, if any.
  /// \param BaseAddress Base address of the range to look up.
  /// \return Index of the matching range, or std::nullopt if none.
  std::optional<size_t>
  getBBRangeIndexForBaseAddress(uint64_t BaseAddress) const {
    for (size_t I = 0; I < BBRanges.size(); ++I)
      if (BBRanges[I].BaseAddress == BaseAddress)
        return I;
    return {};
  }

  /// Return the basic-block entries in the first range.
  /// \return Basic-block entries in the first range.
  const std::vector<BBEntry> &getBBEntries() const {
    return BBRanges.front().BBEntries;
  }

  /// Return all basic-block ranges for this function.
  /// \return All basic-block ranges for this function.
  const std::vector<BBRangeEntry> &getBBRanges() const { return BBRanges; }

  /// Return true if this equals \p Other.
  /// \param Other BB address map to compare against.
  /// \return True if the maps are equal.
  bool operator==(const BBAddrMap &Other) const {
    return BBRanges == Other.BBRanges;
  }
};

/// A feature extension of BBAddrMap that holds information relevant to PGO.
struct PGOAnalysisMap {
  /// Extra basic block data with fields for block frequency and branch
  /// probability.
  struct PGOBBEntry {
    /// Single successor of a given basic block that contains the tag and branch
    /// probability associated with it.
    struct SuccessorEntry {
      /// Unique ID of this successor basic block.
      uint32_t ID = 0;
      /// Branch Probability of the edge to this successor taken from MBPI.
      BranchProbability Prob;
      /// Raw edge count from the post link profile (e.g., from bolt or
      /// propeller).
      uint64_t PostLinkFreq = 0;

      /// Return true if this equals \p Other.
      /// \param Other Successor entry to compare against.
      /// \return True if the successor entries are equal.
      bool operator==(const SuccessorEntry &Other) const {
        return std::tie(ID, Prob, PostLinkFreq) ==
               std::tie(Other.ID, Other.Prob, Other.PostLinkFreq);
      }
    };

    /// Block frequency taken from MBFI
    BlockFrequency BlockFreq;
    /// Raw block count taken from the post link profile (e.g., from bolt or
    /// propeller).
    uint64_t PostLinkBlockFreq = 0;
    /// List of successors of the current block
    llvm::SmallVector<SuccessorEntry, 2> Successors;

    /// Return true if this equals \p Other.
    /// \param Other PGO basic-block entry to compare against.
    /// \return True if the PGO entries are equal.
    bool operator==(const PGOBBEntry &Other) const {
      return std::tie(BlockFreq, PostLinkBlockFreq, Successors) ==
             std::tie(Other.BlockFreq, Other.PostLinkBlockFreq,
                      Other.Successors);
    }
  };

  /// Profile count for the IR function entry.
  uint64_t FuncEntryCount;
  /// Extended basic-block entries with PGO analysis data.
  std::vector<PGOBBEntry> BBEntries;

  /// Feature flags indicating which PGO fields were enabled for this function.
  BBAddrMap::Features FeatEnable;

  /// Return true if this equals \p Other.
  /// \param Other PGO analysis map to compare against.
  /// \return True if the maps are equal.
  bool operator==(const PGOAnalysisMap &Other) const {
    return std::tie(FuncEntryCount, BBEntries, FeatEnable) ==
           std::tie(Other.FuncEntryCount, Other.BBEntries, Other.FeatEnable);
  }
};

/// Extracts addresses from a data stream.
///
/// The base implementation reads the address directly. Subclasses can override
/// to handle format-specific details such as relocation resolution.
class AddressExtractor {
  const DataExtractor &Data;
  unsigned AddressSize;

public:
  /// Construct an extractor over \p Data using \p AddressSize-byte addresses.
  /// \param Data Data extractor providing the address stream.
  /// \param AddressSize Size in bytes of each address value.
  AddressExtractor(const DataExtractor &Data, unsigned AddressSize)
      : Data(Data), AddressSize(AddressSize) {}

  /// Destroy the address extractor.
  virtual ~AddressExtractor() = default;

  /// Return the underlying data extractor.
  /// \return Underlying data extractor.
  const DataExtractor &getDataExtractor() const { return Data; }

  /// Extract and resolve an address at the current cursor position.
  /// \param Cur Cursor positioned at the address to extract.
  /// \return Extracted address, or an error on failure.
  virtual Expected<uint64_t> extractAddress(DataExtractor::Cursor &Cur) {
    uint64_t Address = Data.getUnsigned(Cur, AddressSize);
    if (!Cur)
      return Cur.takeError();
    return Address;
  }
};

/// Decodes one BB address map section payload.
///
/// \param Extractor Address extractor and underlying DataExtractor to read from.
/// \param PGOAnalyses If non-null, receives the decoded PGO analysis data; may
///   be partially populated on error.
/// \return Decoded BB address maps, or an error on failure.
LLVM_ABI Expected<std::vector<BBAddrMap>>
decodeBBAddrMapPayload(AddressExtractor &Extractor,
                       std::vector<PGOAnalysisMap> *PGOAnalyses = nullptr);

} // end namespace object.
} // end namespace llvm.

#endif // LLVM_OBJECT_BBADDRMAP_H
