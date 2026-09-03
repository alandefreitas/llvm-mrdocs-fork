//===- DWARFAbbreviationDeclaration.h ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFABBREVIATIONDECLARATION_H
#define LLVM_DEBUGINFO_DWARF_DWARFABBREVIATIONDECLARATION_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DWARF/DWARFFormValue.h"
#include "llvm/Support/Compiler.h"
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace llvm {

class DataExtractor;
class DWARFUnit;
class raw_ostream;

/// A single DWARF abbreviation declaration (code, tag, children, attributes).
class DWARFAbbreviationDeclaration {
public:
  /// Outcome of parsing one abbreviation declaration from .debug_abbrev.
  enum class ExtractState {
    Complete, ///< Reached the terminating null abbreviation code (end of set).
    MoreItems ///< Parsed one declaration; further abbreviations may follow.
  };
  /// One attribute/form pair (and optional size or implicit value) in an abbreviation.
  struct AttributeSpec {
    /// Build an attribute spec for DW_FORM_implicit_const with fixed \p Value.
    ///
    /// \param A DWARF attribute name for this spec.
    /// \param F Must be DW_FORM_implicit_const.
    /// \param Value Implicit-constant attribute value stored in the abbreviation.
    AttributeSpec(dwarf::Attribute A, dwarf::Form F, int64_t Value)
        : Attr(A), Form(F), Value(Value) {
      assert(isImplicitConst());
    }
    /// Build an attribute spec with optional fixed \p ByteSize for non-const forms.
    ///
    /// \param A DWARF attribute name for this spec.
    /// \param F DWARF form encoding (must not be DW_FORM_implicit_const).
    /// \param ByteSize Fixed byte size of the form if known; empty if variable.
    AttributeSpec(dwarf::Attribute A, dwarf::Form F,
                  std::optional<uint8_t> ByteSize)
        : Attr(A), Form(F) {
      assert(!isImplicitConst());
      this->ByteSize.HasByteSize = ByteSize.has_value();
      if (this->ByteSize.HasByteSize)
        this->ByteSize.ByteSize = *ByteSize;
    }

    /// Build a DWARFFormValue for this attribute's form (with implicit-const value if any).
    ///
    /// \returns A DWARFFormValue for Form, including the implicit-const value when applicable.
    DWARFFormValue getFormValue() const {
      if (Form == dwarf::DW_FORM_implicit_const)
        return DWARFFormValue::createFromSValue(Form, getImplicitConstValue());

      return DWARFFormValue(Form);
    }

    /// DWARF attribute name for this spec (e.g. DW_AT_name).
    dwarf::Attribute Attr;
    /// DWARF form that encodes this attribute's value (e.g. DW_FORM_strp).
    dwarf::Form Form;

  private:
    /// The following field is used for ByteSize for non-implicit_const
    /// attributes and as value for implicit_const ones, indicated by
    /// Form == DW_FORM_implicit_const.
    /// The following cases are distinguished:
    /// * Form != DW_FORM_implicit_const and HasByteSize is true:
    ///     ByteSize contains the fixed size in bytes for the Form in this
    ///     object.
    /// * Form != DW_FORM_implicit_const and HasByteSize is false:
    ///     byte size of Form either varies according to the DWARFUnit
    ///     that it is contained in or the value size varies and must be
    ///     decoded from the debug information in order to determine its size.
    /// * Form == DW_FORM_implicit_const:
    ///     Value contains value for the implicit_const attribute.
    struct ByteSizeStorage {
      bool HasByteSize;
      uint8_t ByteSize;
    };
    union {
      /// Fixed form size storage when Form is not DW_FORM_implicit_const.
      ByteSizeStorage ByteSize;
      /// Implicit-const attribute value when Form is DW_FORM_implicit_const.
      int64_t Value;
    };

  public:
    /// True if this attribute uses DW_FORM_implicit_const (value stored in the abbrev).
    ///
    /// \returns True if Form is DW_FORM_implicit_const; false otherwise.
    bool isImplicitConst() const {
      return Form == dwarf::DW_FORM_implicit_const;
    }

    /// Return the implicit-constant attribute value (DW_FORM_implicit_const).
    ///
    /// \returns The signed value stored in the abbreviation for this attribute.
    int64_t getImplicitConstValue() const {
      assert(isImplicitConst());
      return Value;
    }

    /// Get the fixed byte size of this Form if possible.
    ///
    /// This function might use the DWARFUnit to calculate the size of the
    /// Form, like for DW_AT_address and DW_AT_ref_addr, so this isn't just an
    /// accessor for the ByteSize member.
    ///
    /// \param U the DWARFUnit to use when determining form sizes that depend
    /// on the unit.
    /// \returns Fixed form size in bytes if known; std::nullopt if the size varies.
    LLVM_ABI std::optional<int64_t> getByteSize(const DWARFUnit &U) const;
  };
  /// Ordered list of attribute specs that make up this abbreviation.
  using AttributeSpecVector = SmallVector<AttributeSpec, 8>;

  /// Construct an empty abbreviation declaration.
  LLVM_ABI DWARFAbbreviationDeclaration();

  /// Return this abbreviation's DWARF abbreviation code.
  ///
  /// \returns The ULEB128 abbreviation code identifying this declaration.
  uint32_t getCode() const { return Code; }
  /// Byte size of the ULEB128 encoding of this abbreviation's code.
  ///
  /// \returns The number of bytes used to encode \c Code as a ULEB128.
  uint8_t getCodeByteSize() const { return CodeByteSize; }
  /// Return the DWARF tag for DIEs that use this abbreviation.
  ///
  /// \returns The DWARF tag (e.g. DW_TAG_subprogram) for matching DIEs.
  dwarf::Tag getTag() const { return Tag; }
  /// True if DIEs using this abbreviation have child DIEs.
  ///
  /// \returns True if matching DIEs have children; false if they are leaves.
  bool hasChildren() const { return HasChildren; }

  /// Const iterator range over this abbreviation's AttributeSpec entries.
  using attr_iterator_range =
      iterator_range<AttributeSpecVector::const_iterator>;

  /// Return a const range over this abbreviation's attribute/form specs.
  ///
  /// \returns A const iterator range over this abbreviation's AttributeSpec entries.
  attr_iterator_range attributes() const { return AttributeSpecs; }

  /// Return the DWARF form of the attribute at index \p idx.
  ///
  /// \param idx Zero-based index into this abbreviation's attribute specs.
  /// \returns The DWARF form encoding at the given index.
  dwarf::Form getFormByIndex(uint32_t idx) const {
    assert(idx < AttributeSpecs.size());
    return AttributeSpecs[idx].Form;
  }

  /// Number of attribute/form pairs in this abbreviation declaration.
  ///
  /// \returns The number of AttributeSpec entries in this abbreviation.
  size_t getNumAttributes() const {
    return AttributeSpecs.size();
  }

  /// Return the DWARF attribute at index \p idx in this abbreviation.
  ///
  /// \param idx Zero-based index into this abbreviation's attribute specs.
  /// \returns The DWARF attribute name at the given index.
  dwarf::Attribute getAttrByIndex(uint32_t idx) const {
    assert(idx < AttributeSpecs.size());
    return AttributeSpecs[idx].Attr;
  }

  /// Whether the attribute at index \p idx uses DW_FORM_implicit_const.
  ///
  /// \param idx Zero-based index into this abbreviation's attribute specs.
  /// \returns True if the attribute at \p idx uses DW_FORM_implicit_const.
  bool getAttrIsImplicitConstByIndex(uint32_t idx) const {
    assert(idx < AttributeSpecs.size());
    return AttributeSpecs[idx].isImplicitConst();
  }

  /// Implicit-const value of the attribute at index \p idx (must be DW_FORM_implicit_const).
  ///
  /// \param idx Zero-based index into this abbreviation's attribute specs.
  /// \returns The implicit-constant value stored in the abbreviation for that attribute.
  int64_t getAttrImplicitConstValueByIndex(uint32_t idx) const {
    assert(idx < AttributeSpecs.size());
    return AttributeSpecs[idx].getImplicitConstValue();
  }

  /// Get the index of the specified attribute.
  ///
  /// Searches the this abbreviation declaration for the index of the specified
  /// attribute.
  ///
  /// \param attr DWARF attribute to search for.
  /// \returns Optional index of the attribute if found, std::nullopt otherwise.
  LLVM_ABI std::optional<uint32_t>
  findAttributeIndex(dwarf::Attribute attr) const;

  /// Extract a DWARF form value from a DIE specified by DIE offset.
  ///
  /// Extract an attribute value for a DWARFUnit given the DIE offset and the
  /// attribute.
  ///
  /// \param DIEOffset the DIE offset that points to the ULEB128 abbreviation
  /// code in the .debug_info data.
  /// \param Attr DWARF attribute to search for.
  /// \param U the DWARFUnit the contains the DIE.
  /// \returns Optional DWARF form value if the attribute was extracted.
  LLVM_ABI std::optional<DWARFFormValue>
  getAttributeValue(const uint64_t DIEOffset, const dwarf::Attribute Attr,
                    const DWARFUnit &U) const;

  /// Compute an offset from a DIE specified by DIE offset and attribute index.
  ///
  /// \param AttrIndex an index of DWARF attribute.
  /// \param DIEOffset the DIE offset that points to the ULEB128 abbreviation
  /// code in the .debug_info data.
  /// \param U the DWARFUnit the contains the DIE.
  /// \returns an offset of the attribute.
  LLVM_ABI uint64_t getAttributeOffsetFromIndex(uint32_t AttrIndex,
                                                uint64_t DIEOffset,
                                                const DWARFUnit &U) const;

  /// Extract a DWARF form value from a DIE speccified by attribute index and
  /// its offset.
  ///
  /// \param AttrIndex an index of DWARF attribute.
  /// \param Offset offset of the attribute.
  /// \param U the DWARFUnit the contains the DIE.
  /// \returns Optional DWARF form value if the attribute was extracted.
  LLVM_ABI std::optional<DWARFFormValue>
  getAttributeValueFromOffset(uint32_t AttrIndex, uint64_t Offset,
                              const DWARFUnit &U) const;

  /// Parse one abbreviation from \p Data at \p OffsetPtr; returns Complete or MoreItems.
  ///
  /// \param Data .debug_abbrev section contents to parse from.
  /// \param OffsetPtr Byte offset into \p Data; advanced past the parsed declaration.
  /// \returns ExtractState::Complete or MoreItems on success, or an Error on failure.
  LLVM_ABI llvm::Expected<ExtractState> extract(DataExtractor Data,
                                                uint64_t *OffsetPtr);
  /// Print this abbreviation's code, tag, children flag, and attributes to \p OS.
  ///
  /// \param OS Output stream to write the dump to.
  LLVM_ABI void dump(raw_ostream &OS) const;

  /// Fixed byte size of all attribute payloads for unit \p U, if every form has a fixed size.
  ///
  /// \param U the DWARFUnit used to resolve sizes that depend on address or offset size.
  /// \returns Fixed total attribute payload size in bytes, or std::nullopt if any form varies.
  LLVM_ABI std::optional<size_t>
  getFixedAttributesByteSize(const DWARFUnit &U) const;

private:
  void clear();

  /// A helper structure that can quickly determine the size in bytes of an
  /// abbreviation declaration.
  struct FixedSizeInfo {
    /// The fixed byte size for fixed size forms.
    uint16_t NumBytes = 0;
    /// Number of DW_FORM_address forms in this abbrevation declaration.
    uint8_t NumAddrs = 0;
    /// Number of DW_FORM_ref_addr forms in this abbrevation declaration.
    uint8_t NumRefAddrs = 0;
    /// Number of 4 byte in DWARF32 and 8 byte in DWARF64 forms.
    uint8_t NumDwarfOffsets = 0;

    FixedSizeInfo() = default;

    /// Calculate the fixed size in bytes given a DWARFUnit.
    ///
    /// \param U the DWARFUnit to use when determing the byte size.
    /// \returns the size in bytes for all attribute data in this abbreviation.
    /// The returned size does not include bytes for the  ULEB128 abbreviation
    /// code
    LLVM_ABI size_t getByteSize(const DWARFUnit &U) const;
  };

  uint32_t Code;
  dwarf::Tag Tag;
  uint8_t CodeByteSize;
  bool HasChildren;
  AttributeSpecVector AttributeSpecs;
  /// If this abbreviation has a fixed byte size then FixedAttributeSize member
  /// variable below will have a value.
  std::optional<FixedSizeInfo> FixedAttributeSize;
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFABBREVIATIONDECLARATION_H
