//===- DWARFDebugAbbrev.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFDEBUGABBREV_H
#define LLVM_DEBUGINFO_DWARF_DWARFDEBUGABBREV_H

#include "llvm/DebugInfo/DWARF/DWARFAbbreviationDeclaration.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataExtractor.h"
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace llvm {

class raw_ostream;

/// Read the next attribute/form spec from an abbreviation declaration.
///
/// Read the next (attribute, form) specification from an abbreviation
/// declaration at \p Offset, advancing \p Offset past it. \p ImplicitConst is
/// set to the inline value of a DW_FORM_implicit_const attribute and to
/// std::nullopt otherwise.
///
/// \param AbbrevData .debug_abbrev bytes containing the abbreviation.
/// \param Offset Byte offset of the next attribute spec; advanced past it.
/// \param Name DWARF attribute name read from the spec.
/// \param Form DWARF form encoding read from the spec.
/// \param ImplicitConst Inline value if Form is DW_FORM_implicit_const;
/// std::nullopt otherwise.
/// \returns false on the terminating (0, 0) pair; true otherwise.
LLVM_ABI bool readAbbrevAttribute(const DataExtractor &AbbrevData,
                                  uint64_t *Offset, dwarf::Attribute &Name,
                                  dwarf::Form &Form,
                                  std::optional<int64_t> &ImplicitConst);

class DWARFAbbreviationDeclarationSet {
  uint64_t Offset;
  /// Code of the first abbreviation, if all abbreviations in the set have
  /// consecutive codes. UINT32_MAX otherwise.
  uint32_t FirstAbbrCode;
  std::vector<DWARFAbbreviationDeclaration> Decls;

  using const_iterator =
      std::vector<DWARFAbbreviationDeclaration>::const_iterator;

public:
  /// Construct an empty abbreviation declaration set.
  LLVM_ABI DWARFAbbreviationDeclarationSet();

  /// Byte offset of this set within the .debug_abbrev section.
  ///
  /// \returns Offset of this set within .debug_abbrev.
  uint64_t getOffset() const { return Offset; }
  /// Print each abbreviation declaration in this set to \p OS.
  ///
  /// \param OS Output stream to write the dump to.
  LLVM_ABI void dump(raw_ostream &OS) const;
  /// Parse this set's abbreviations from \p Data starting at \p OffsetPtr.
  ///
  /// \param Data .debug_abbrev bytes to parse from.
  /// \param OffsetPtr Byte offset of this set; advanced past the terminating
  /// null abbreviation.
  /// \returns Success, or an error if the abbreviations could not be parsed.
  LLVM_ABI Error extract(DataExtractor Data, uint64_t *OffsetPtr);

  /// Return the abbreviation declaration with code \p AbbrCode.
  ///
  /// \param AbbrCode DWARF abbreviation code to look up in this set.
  /// \returns The matching declaration, or nullptr if \p AbbrCode is not in
  /// this set.
  LLVM_ABI const DWARFAbbreviationDeclaration *
  getAbbreviationDeclaration(uint32_t AbbrCode) const;

  /// Iterator to the first abbreviation declaration in this set.
  ///
  /// \returns Const iterator to the first declaration.
  const_iterator begin() const {
    return Decls.begin();
  }

  /// Iterator past the last abbreviation declaration in this set.
  ///
  /// \returns Const iterator past the last declaration.
  const_iterator end() const {
    return Decls.end();
  }

  /// Format this set's abbreviation codes as a comma-separated range list.
  ///
  /// \returns Comma-separated string of abbreviation code ranges.
  LLVM_ABI std::string getCodeRange() const;

  /// First abbreviation code if codes are consecutive; UINT32_MAX otherwise.
  ///
  /// \returns FirstAbbrCode when codes are consecutive; UINT32_MAX otherwise.
  uint32_t getFirstAbbrCode() const { return FirstAbbrCode; }

private:
  void clear();
};

class DWARFDebugAbbrev {
  using DWARFAbbreviationDeclarationSetMap =
      std::map<uint64_t, DWARFAbbreviationDeclarationSet>;

  mutable DWARFAbbreviationDeclarationSetMap AbbrDeclSets;
  mutable DWARFAbbreviationDeclarationSetMap::const_iterator PrevAbbrOffsetPos;
  mutable std::optional<DataExtractor> Data;

public:
  /// Construct a parser for the .debug_abbrev contents in \p Data.
  ///
  /// \param Data Raw .debug_abbrev section bytes.
  LLVM_ABI DWARFDebugAbbrev(DataExtractor Data);

  /// Return the abbreviation set at compile-unit offset \p CUAbbrOffset.
  ///
  /// Parses that set on demand if it has not already been extracted.
  ///
  /// \param CUAbbrOffset Offset of a compilation unit's abbreviation table
  /// in .debug_abbrev.
  /// \returns A pointer to the set, or an error if \p CUAbbrOffset is not
  /// valid.
  LLVM_ABI Expected<const DWARFAbbreviationDeclarationSet *>
  getAbbreviationDeclarationSet(uint64_t CUAbbrOffset) const;

  /// Print every abbreviation set in this section to \p OS.
  ///
  /// \param OS Output stream to write the dump to.
  LLVM_ABI void dump(raw_ostream &OS) const;
  /// Parse all remaining abbreviation sets from the .debug_abbrev data.
  ///
  /// After a successful parse the raw extractor is released so the sets may
  /// be iterated with begin() and end().
  ///
  /// \returns Success, or an error if a set could not be parsed.
  LLVM_ABI Error parse() const;

  /// Iterator to the first abbreviation declaration set.
  ///
  /// parse() must have been called successfully before iterating.
  ///
  /// \returns Const iterator to the first abbreviation set.
  DWARFAbbreviationDeclarationSetMap::const_iterator begin() const {
    assert(!Data && "Must call parse before iterating over DWARFDebugAbbrev");
    return AbbrDeclSets.begin();
  }

  /// Iterator past the last abbreviation declaration set.
  ///
  /// \returns Const iterator past the last abbreviation set.
  DWARFAbbreviationDeclarationSetMap::const_iterator end() const {
    return AbbrDeclSets.end();
  }
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFDEBUGABBREV_H
