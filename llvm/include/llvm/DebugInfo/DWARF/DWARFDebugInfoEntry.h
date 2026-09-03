//===- DWARFDebugInfoEntry.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFDEBUGINFOENTRY_H
#define LLVM_DEBUGINFO_DWARF_DWARFDEBUGINFOENTRY_H

#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DWARF/DWARFAbbreviationDeclaration.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>

namespace llvm {

class DWARFUnit;
class DWARFDataExtractor;

/// DWARFDebugInfoEntry - A DIE with only the minimum required data.
class DWARFDebugInfoEntry {
  /// Offset within the .debug_info of the start of this entry.
  uint64_t Offset = 0;

  /// Index of the parent die. UINT32_MAX if there is no parent.
  uint32_t ParentIdx = UINT32_MAX;

  /// Index of the sibling die. Zero if there is no sibling.
  uint32_t SiblingIdx = 0;

  const DWARFAbbreviationDeclaration *AbbrevDecl = nullptr;

public:
  /// Construct an empty, uninitialized debug info entry.
  DWARFDebugInfoEntry() = default;

  /// Extract a debug info entry that is a child of a given unit.
  ///
  /// Starts at a given offset. If DIE can't be extracted, returns false and
  /// doesn't change OffsetPtr. High performance extraction should use this
  /// call.
  ///
  /// \param U Unit that owns this debug info entry.
  /// \param OffsetPtr Byte offset into the debug info; advanced past the DIE
  ///        on success.
  /// \param DebugInfoData Extractor for the .debug_info section data.
  /// \param UEndOffset End offset of the unit within the debug info.
  /// \param ParentIdx Index of the parent die.
  /// \returns True if the DIE was extracted successfully; false otherwise.
  LLVM_ABI bool extractFast(const DWARFUnit &U, uint64_t *OffsetPtr,
                            const DWARFDataExtractor &DebugInfoData,
                            uint64_t UEndOffset, uint32_t ParentIdx);

  /// Return the offset of this entry within .debug_info.
  ///
  /// \returns Byte offset of this entry within the .debug_info section.
  uint64_t getOffset() const { return Offset; }

  /// Returns index of the parent die.
  ///
  /// \returns Parent die index, or std::nullopt if there is no parent.
  std::optional<uint32_t> getParentIdx() const {
    if (ParentIdx == UINT32_MAX)
      return std::nullopt;

    return ParentIdx;
  }

  /// Returns index of the sibling die.
  ///
  /// \returns Sibling die index, or std::nullopt if there is no sibling.
  std::optional<uint32_t> getSiblingIdx() const {
    if (SiblingIdx == 0)
      return std::nullopt;

    return SiblingIdx;
  }

  /// Set index of sibling.
  ///
  /// \param Idx Index of the sibling die, or zero if there is no sibling.
  void setSiblingIdx(uint32_t Idx) { SiblingIdx = Idx; }

  /// DWARF tag for this entry, or DW_TAG_null if the abbreviation is missing.
  ///
  /// \returns The DWARF tag, or DW_TAG_null if the abbreviation is missing.
  dwarf::Tag getTag() const {
    return AbbrevDecl ? AbbrevDecl->getTag() : dwarf::DW_TAG_null;
  }

  /// True if this entry's abbreviation indicates it has child DIEs.
  ///
  /// \returns True if this entry has child DIEs; false otherwise.
  bool hasChildren() const { return AbbrevDecl && AbbrevDecl->hasChildren(); }

  /// Return the abbreviation declaration for this entry, or nullptr if none.
  ///
  /// \returns Pointer to the abbreviation declaration, or nullptr if none.
  const DWARFAbbreviationDeclaration *getAbbreviationDeclarationPtr() const {
    return AbbrevDecl;
  }
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFDEBUGINFOENTRY_H
