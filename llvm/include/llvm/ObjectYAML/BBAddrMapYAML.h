//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the YAML representation of BB address maps
/// (SHT_LLVM_BB_ADDR_MAP / .llvm_bb_addr_map). The types here are
/// format-agnostic so they can be reused by ELFYAML and COFFYAML.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECTYAML_BBADDRMAPYAML_H
#define LLVM_OBJECTYAML_BBADDRMAPYAML_H

#include "llvm/Support/YAMLTraits.h"

namespace llvm {

namespace yaml {
class ContiguousBlobAccumulator;
}

/// YAML representations of BB address map sections.
namespace BBAddrMapYAML {

/// YAML representation of one function's BB address map entry.
struct BBAddrMapEntry {
  /// YAML representation of a single basic block within a range.
  struct BBEntry {
    /// Unique ID of this basic block.
    uint32_t ID;
    /// Offset of the basic block relative to the range base address.
    llvm::yaml::Hex64 AddressOffset;
    /// Size of the basic block in bytes.
    llvm::yaml::Hex64 Size;
    /// Encoded basic-block metadata bitfield.
    llvm::yaml::Hex64 Metadata;
    /// Offsets of call instruction ends relative to the basic block start.
    std::optional<std::vector<llvm::yaml::Hex64>> CallsiteEndOffsets;
    /// Optional hash of this basic block.
    std::optional<llvm::yaml::Hex64> Hash;
  };
  /// BBAddrMap section version for this entry.
  uint8_t Version;
  /// Feature bitfield controlling optional BBAddrMap fields.
  llvm::yaml::Hex16 Feature;

  /// YAML representation of a contiguous range of basic blocks.
  struct BBRangeEntry {
    /// Base address of this basic-block range.
    llvm::yaml::Hex64 BaseAddress;
    /// Optional count of basic blocks in this range.
    std::optional<uint64_t> NumBlocks;
    /// Optional list of basic-block entries in this range.
    std::optional<std::vector<BBEntry>> BBEntries;
  };

  /// Optional count of basic-block ranges for this function.
  std::optional<uint64_t> NumBBRanges;
  /// Optional list of basic-block ranges for this function.
  std::optional<std::vector<BBRangeEntry>> BBRanges;

  /// Returns the function address from the first range's base address.
  /// \return The first range's base address, or 0 if there are no ranges.
  llvm::yaml::Hex64 getFunctionAddress() const {
    if (!BBRanges || BBRanges->empty())
      return 0;
    return BBRanges->front().BaseAddress;
  }

  /// Returns true if any BB entry has non-empty callsite end offsets.
  /// \return True if any BB entry has non-empty callsite end offsets.
  bool hasAnyCallsiteEndOffsets() const {
    if (!BBRanges)
      return false;
    for (const BBRangeEntry &BBR : *BBRanges) {
      if (!BBR.BBEntries)
        continue;
      for (const BBEntry &BBE : *BBR.BBEntries)
        if (BBE.CallsiteEndOffsets && !BBE.CallsiteEndOffsets->empty())
          return true;
    }
    return false;
  }
};

/// YAML representation of PGO analysis data paired with a BBAddrMap entry.
struct PGOAnalysisMapEntry {
  /// YAML representation of PGO data for one basic block.
  struct PGOBBEntry {
    /// YAML representation of a successor edge with branch probability.
    struct SuccessorEntry {
      /// Unique ID of the successor basic block.
      uint32_t ID;
      /// Branch probability of the edge to this successor.
      llvm::yaml::Hex32 BrProb;
      /// Optional post-link raw edge frequency for this successor.
      std::optional<uint32_t> PostLinkBrFreq;
    };
    /// Optional block frequency for this basic block.
    std::optional<uint64_t> BBFreq;
    /// Optional post-link raw block frequency for this basic block.
    std::optional<uint32_t> PostLinkBBFreq;
    /// Optional list of successor edges from this basic block.
    std::optional<std::vector<SuccessorEntry>> Successors;
  };
  /// Optional profile entry count for the function.
  std::optional<uint64_t> FuncEntryCount;
  /// Optional per-basic-block PGO analysis entries.
  std::optional<std::vector<PGOBBEntry>> PGOBBEntries;
};

/// Encodes the BBAddrMap payload into a contiguous blob.
/// \param Entries BB address map entries to encode.
/// \param PGOAnalyses Optional PGO analyses parallel to \p Entries; if
///        non-null, must have the same length as \p Entries.
/// \param CBA Accumulator that receives the encoded payload.
/// \param Endian Endianness used when writing multi-byte fields.
/// \param AddressSize Address width in bytes; must be 4 or 8.
LLVM_ABI void encodePayload(ArrayRef<BBAddrMapEntry> Entries,
                            const std::vector<PGOAnalysisMapEntry> *PGOAnalyses,
                            yaml::ContiguousBlobAccumulator &CBA,
                            llvm::endianness Endian, unsigned AddressSize);

} // end namespace BBAddrMapYAML
} // end namespace llvm

namespace llvm {
namespace yaml {

/// Sequences of BBAddrMap entries use block formatting.
template <> struct SequenceElementTraits<llvm::BBAddrMapYAML::BBAddrMapEntry> {
  /// Emit sequences of BBAddrMap entries in block style.
  static const bool flow = false;
};

/// Sequences of BB entries use block formatting.
template <>
struct SequenceElementTraits<llvm::BBAddrMapYAML::BBAddrMapEntry::BBEntry> {
  /// Emit sequences of BB entries in block style.
  static const bool flow = false;
};

/// Sequences of BB range entries use block formatting.
template <>
struct SequenceElementTraits<
    llvm::BBAddrMapYAML::BBAddrMapEntry::BBRangeEntry> {
  /// Emit sequences of BB range entries in block style.
  static const bool flow = false;
};

/// Sequences of PGO analysis map entries use block formatting.
template <>
struct SequenceElementTraits<llvm::BBAddrMapYAML::PGOAnalysisMapEntry> {
  /// Emit sequences of PGO analysis map entries in block style.
  static const bool flow = false;
};

/// Sequences of PGO BB entries use block formatting.
template <>
struct SequenceElementTraits<
    llvm::BBAddrMapYAML::PGOAnalysisMapEntry::PGOBBEntry> {
  /// Emit sequences of PGO BB entries in block style.
  static const bool flow = false;
};

/// Sequences of PGO successor entries use block formatting.
template <>
struct SequenceElementTraits<
    llvm::BBAddrMapYAML::PGOAnalysisMapEntry::PGOBBEntry::SuccessorEntry> {
  /// Emit sequences of PGO successor entries in block style.
  static const bool flow = false;
};

/// YAMLIO mapping traits for \c BBAddrMapYAML::BBAddrMapEntry.
template <> struct MappingTraits<BBAddrMapYAML::BBAddrMapEntry> {
  /// Map BBAddrMap entry fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param E BBAddrMap entry being mapped.
  LLVM_ABI static void mapping(IO &IO, BBAddrMapYAML::BBAddrMapEntry &E);
};

/// YAMLIO mapping traits for \c BBAddrMapYAML::BBAddrMapEntry::BBRangeEntry.
template <> struct MappingTraits<BBAddrMapYAML::BBAddrMapEntry::BBRangeEntry> {
  /// Map BB range entry fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param E BB range entry being mapped.
  LLVM_ABI static void mapping(IO &IO,
                               BBAddrMapYAML::BBAddrMapEntry::BBRangeEntry &E);
};

/// YAMLIO mapping traits for \c BBAddrMapYAML::BBAddrMapEntry::BBEntry.
template <> struct MappingTraits<BBAddrMapYAML::BBAddrMapEntry::BBEntry> {
  /// Map BB entry fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param E BB entry being mapped.
  LLVM_ABI static void mapping(IO &IO,
                               BBAddrMapYAML::BBAddrMapEntry::BBEntry &E);
};

/// YAMLIO mapping traits for \c BBAddrMapYAML::PGOAnalysisMapEntry.
template <> struct MappingTraits<BBAddrMapYAML::PGOAnalysisMapEntry> {
  /// Map PGO analysis map entry fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param E PGO analysis map entry being mapped.
  LLVM_ABI static void mapping(IO &IO, BBAddrMapYAML::PGOAnalysisMapEntry &E);
};

/// YAMLIO mapping traits for \c BBAddrMapYAML::PGOAnalysisMapEntry::PGOBBEntry.
template <>
struct MappingTraits<BBAddrMapYAML::PGOAnalysisMapEntry::PGOBBEntry> {
  /// Map PGO BB entry fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param E PGO BB entry being mapped.
  LLVM_ABI static void
  mapping(IO &IO, BBAddrMapYAML::PGOAnalysisMapEntry::PGOBBEntry &E);
};

/// YAMLIO mapping traits for
/// \c BBAddrMapYAML::PGOAnalysisMapEntry::PGOBBEntry::SuccessorEntry.
template <>
struct MappingTraits<
    BBAddrMapYAML::PGOAnalysisMapEntry::PGOBBEntry::SuccessorEntry> {
  /// Map PGO successor entry fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param E PGO successor entry being mapped.
  LLVM_ABI static void
  mapping(IO &IO,
          BBAddrMapYAML::PGOAnalysisMapEntry::PGOBBEntry::SuccessorEntry &E);
};

} // end namespace yaml
} // end namespace llvm

#endif // LLVM_OBJECTYAML_BBADDRMAPYAML_H
