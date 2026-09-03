//===- DWARFDie.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFDIE_H
#define LLVM_DEBUGINFO_DWARF_DWARFDIE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/DebugInfo/DWARF/DWARFAddressRange.h"
#include "llvm/DebugInfo/DWARF/DWARFAttribute.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugInfoEntry.h"
#include "llvm/DebugInfo/DWARF/DWARFLocationExpression.h"
#include "llvm/Support/Compiler.h"
#include <cassert>
#include <cstdint>
#include <iterator>

namespace llvm {

class DWARFUnit;
class raw_ostream;

//===----------------------------------------------------------------------===//
/// Utility class that carries the DWARF compile/type unit and the debug info
/// entry in an object.
///
/// When accessing information from a debug info entry we always need to DWARF
/// compile/type unit in order to extract the info correctly as some information
/// is relative to the compile/type unit. Prior to this class the DWARFUnit and
/// the DWARFDebugInfoEntry was passed around separately and there was the
/// possibility for error if the wrong DWARFUnit was used to extract a unit
/// relative offset. This class helps to ensure that this doesn't happen and
/// also simplifies the attribute extraction calls by not having to specify the
/// DWARFUnit for each call.
class DWARFDie {
  DWARFUnit *U = nullptr;
  const DWARFDebugInfoEntry *Die = nullptr;

public:
  /// Alias for the DWARF form/value type used by attribute accessors.
  using DWARFFormValue = llvm::DWARFFormValue;
  /// Construct an invalid DIE (no unit or debug-info entry).
  DWARFDie() = default;
  /// Bind this DIE to \p Unit and debug-info entry \p D.
  ///
  /// \param Unit Compile or type unit that owns \p D.
  /// \param D Debug-info entry to bind to this DIE.
  DWARFDie(DWARFUnit *Unit, const DWARFDebugInfoEntry *D) : U(Unit), Die(D) {}

  /// True if this DIE is bound to both a unit and a debug-info entry.
  ///
  /// \returns True if this DIE is bound to both a unit and a debug-info entry.
  bool isValid() const { return U && Die; }
  /// True if this DIE refers to a valid unit and debug-info entry.
  ///
  /// \returns True if this DIE refers to a valid unit and debug-info entry.
  explicit operator bool() const { return isValid(); }
  /// Return the underlying debug-info entry, or nullptr if invalid.
  ///
  /// \returns The underlying debug-info entry, or nullptr if invalid.
  const DWARFDebugInfoEntry *getDebugInfoEntry() const { return Die; }
  /// Return the DWARF compile/type unit that owns this DIE, or nullptr.
  ///
  /// \returns The DWARF compile/type unit that owns this DIE, or nullptr.
  DWARFUnit *getDwarfUnit() const { return U; }

  /// Get the abbreviation declaration for this DIE.
  ///
  /// \returns the abbreviation declaration or NULL for null tags.
  const DWARFAbbreviationDeclaration *getAbbreviationDeclarationPtr() const {
    assert(isValid() && "must check validity prior to calling");
    return Die->getAbbreviationDeclarationPtr();
  }

  /// Get the absolute offset into the debug info or types section.
  ///
  /// \returns the DIE offset or -1U if invalid.
  uint64_t getOffset() const {
    assert(isValid() && "must check validity prior to calling");
    return Die->getOffset();
  }

  /// Return this DIE's DWARF tag, or DW_TAG_null for a null DIE.
  ///
  /// \returns This DIE's DWARF tag, or DW_TAG_null for a null DIE.
  dwarf::Tag getTag() const {
    auto AbbrevDecl = getAbbreviationDeclarationPtr();
    if (AbbrevDecl)
      return AbbrevDecl->getTag();
    return dwarf::DW_TAG_null;
  }

  /// True if this DIE has child DIEs.
  ///
  /// \returns True if this DIE has child DIEs.
  bool hasChildren() const {
    assert(isValid() && "must check validity prior to calling");
    return Die->hasChildren();
  }

  /// Returns true for a valid DIE that terminates a sibling chain.
  ///
  /// \returns True for a valid DIE that terminates a sibling chain.
  bool isNULL() const { return getAbbreviationDeclarationPtr() == nullptr; }

  /// Returns true if DIE represents a subprogram (not inlined).
  ///
  /// \returns True if this DIE represents a subprogram (not inlined).
  LLVM_ABI bool isSubprogramDIE() const;

  /// Returns true if DIE represents a subprogram or an inlined subroutine.
  ///
  /// \returns True if this DIE represents a subprogram or an inlined subroutine.
  LLVM_ABI bool isSubroutineDIE() const;

  /// Get the parent of this DIE object.
  ///
  /// \returns a valid DWARFDie instance if this object has a parent or an
  /// invalid DWARFDie instance if it doesn't.
  LLVM_ABI DWARFDie getParent() const;

  /// Get the sibling of this DIE object.
  ///
  /// \returns a valid DWARFDie instance if this object has a sibling or an
  /// invalid DWARFDie instance if it doesn't.
  LLVM_ABI DWARFDie getSibling() const;

  /// Get the previous sibling of this DIE object.
  ///
  /// \returns a valid DWARFDie instance if this object has a sibling or an
  /// invalid DWARFDie instance if it doesn't.
  LLVM_ABI DWARFDie getPreviousSibling() const;

  /// Get the first child of this DIE object.
  ///
  /// \returns a valid DWARFDie instance if this object has children or an
  /// invalid DWARFDie instance if it doesn't.
  LLVM_ABI DWARFDie getFirstChild() const;

  /// Get the last child of this DIE object.
  ///
  /// \returns a valid null DWARFDie instance if this object has children or an
  /// invalid DWARFDie instance if it doesn't.
  LLVM_ABI DWARFDie getLastChild() const;

  /// Dump the DIE and all of its attributes to the supplied stream.
  ///
  /// \param OS the stream to use for output.
  /// \param indent the number of characters to indent each line that is output.
  /// \param DumpOpts Options controlling what and how to dump.
  LLVM_ABI void dump(raw_ostream &OS, unsigned indent = 0,
                     DIDumpOptions DumpOpts = DIDumpOptions()) const;

  /// Convenience zero-argument overload for debugging.
  LLVM_ABI LLVM_DUMP_METHOD void dump() const;

  /// Extract the specified attribute from this DIE.
  ///
  /// Extract an attribute value from this DIE only. This call doesn't look
  /// for the attribute value in any DW_AT_specification or
  /// DW_AT_abstract_origin referenced DIEs.
  ///
  /// \param Attr the attribute to extract.
  /// \returns an optional DWARFFormValue that will have the form value if the
  /// attribute was successfully extracted.
  LLVM_ABI std::optional<DWARFFormValue> find(dwarf::Attribute Attr) const;

  /// Extract the first value of any attribute in Attrs from this DIE.
  ///
  /// Extract the first attribute that matches from this DIE only. This call
  /// doesn't look for the attribute value in any DW_AT_specification or
  /// DW_AT_abstract_origin referenced DIEs. The attributes will be searched
  /// linearly in the order they are specified within Attrs.
  ///
  /// \param Attrs an array of DWARF attribute to look for.
  /// \returns an optional that has a valid DWARFFormValue for the first
  /// matching attribute in Attrs, or std::nullopt if none of the attributes in
  /// Attrs exist in this DIE.
  LLVM_ABI std::optional<DWARFFormValue>
  find(ArrayRef<dwarf::Attribute> Attrs) const;

  /// Extract the first value of any attribute in Attrs from this DIE and
  /// recurse into any DW_AT_specification or DW_AT_abstract_origin referenced
  /// DIEs.
  ///
  /// \param Attrs an array of DWARF attribute to look for.
  /// \returns an optional that has a valid DWARFFormValue for the first
  /// matching attribute in Attrs, or std::nullopt if none of the attributes in
  /// Attrs exist in this DIE or in any DW_AT_specification or
  /// DW_AT_abstract_origin DIEs.
  LLVM_ABI std::optional<DWARFFormValue>
  findRecursively(ArrayRef<dwarf::Attribute> Attrs) const;

  /// Extract the specified attribute from this DIE as the referenced DIE.
  ///
  /// Regardless of the reference type, return the correct DWARFDie instance if
  /// the attribute exists. The returned DWARFDie object might be from another
  /// DWARFUnit, but that is all encapsulated in the new DWARFDie object.
  ///
  /// Extract an attribute value from this DIE only. This call doesn't look
  /// for the attribute value in any DW_AT_specification or
  /// DW_AT_abstract_origin referenced DIEs.
  ///
  /// \param Attr the attribute to extract.
  /// \returns a valid DWARFDie instance if the attribute exists, or an invalid
  /// DWARFDie object if it doesn't.
  LLVM_ABI DWARFDie
  getAttributeValueAsReferencedDie(dwarf::Attribute Attr) const;
  /// Resolve form value \p V as a DIE reference (any reference form).
  ///
  /// \param V Form value that encodes a DIE reference.
  /// \returns The referenced DIE, or an invalid DIE if resolution fails.
  LLVM_ABI DWARFDie
  getAttributeValueAsReferencedDie(const DWARFFormValue &V) const;

  /// Resolve this DIE's type-unit reference to the referenced type DIE.
  ///
  /// \returns The referenced type DIE, or an invalid DIE if none exists.
  LLVM_ABI DWARFDie resolveTypeUnitReference() const;

  /// Resolve attribute \p Attr to a referenced DIE, then follow any type-unit signature.
  ///
  /// \param Attr Attribute that encodes a type or DIE reference.
  /// \returns The referenced type DIE, or an invalid DIE if resolution fails.
  LLVM_ABI DWARFDie resolveReferencedType(dwarf::Attribute Attr) const;
  /// Resolve form value \p V to a referenced DIE, then follow any type-unit signature.
  ///
  /// \param V Form value that encodes a type or DIE reference.
  /// \returns The referenced type DIE, or an invalid DIE if resolution fails.
  LLVM_ABI DWARFDie resolveReferencedType(const DWARFFormValue &V) const;
  /// Extract the range base attribute from this DIE as absolute section offset.
  ///
  /// This is a utility function that checks for either the DW_AT_rnglists_base
  /// or DW_AT_GNU_ranges_base attribute.
  ///
  /// \returns anm optional absolute section offset value for the attribute.
  LLVM_ABI std::optional<uint64_t> getRangesBaseAttribute() const;
  /// Absolute section offset of DW_AT_loclists_base, if present on this DIE.
  ///
  /// \returns The absolute section offset, or std::nullopt if absent.
  LLVM_ABI std::optional<uint64_t> getLocBaseAttribute() const;

  /// Get the DW_AT_high_pc attribute value as an address.
  ///
  /// In DWARF version 4 and later the high PC can be encoded as an offset from
  /// the DW_AT_low_pc. This function takes care of extracting the value as an
  /// address or offset and adds it to the low PC if needed and returns the
  /// value as an optional in case the DIE doesn't have a DW_AT_high_pc
  /// attribute.
  ///
  /// \param LowPC the low PC that might be needed to calculate the high PC.
  /// \returns an optional address value for the attribute.
  LLVM_ABI std::optional<uint64_t> getHighPC(uint64_t LowPC) const;

  /// Retrieves DW_AT_low_pc and DW_AT_high_pc from CU.
  ///
  /// \param LowPC Filled with the low PC address on success.
  /// \param HighPC Filled with the high PC address on success.
  /// \param SectionIndex Filled with the section index for the low/high PC.
  /// \returns True if both attributes are present.
  LLVM_ABI bool getLowAndHighPC(uint64_t &LowPC, uint64_t &HighPC,
                                uint64_t &SectionIndex) const;

  /// Get the address ranges for this DIE.
  ///
  /// Get the hi/low PC range if both attributes are available or exrtracts the
  /// non-contiguous address ranges from the DW_AT_ranges attribute.
  ///
  /// Extracts the range information from this DIE only. This call doesn't look
  /// for the range in any DW_AT_specification or DW_AT_abstract_origin DIEs.
  ///
  /// \returns a address range vector that might be empty if no address range
  /// information is available.
  LLVM_ABI Expected<DWARFAddressRangesVector> getAddressRanges() const;

  /// True if \p Address falls within this DIE's address ranges.
  ///
  /// \param Address Address to test against this DIE's ranges.
  /// \returns True if \p Address falls within this DIE's address ranges.
  LLVM_ABI bool addressRangeContainsAddress(const uint64_t Address) const;

  /// Returns the DW_LANG_ code for this DIE's DWARF unit, if it exists.
  ///
  /// \returns The DW_LANG_ code, or std::nullopt if it does not exist.
  LLVM_ABI std::optional<uint64_t> getLanguage() const;

  /// Return location expressions for attribute \p Attr (inline or from loclists).
  ///
  /// \param Attr Location attribute to extract (e.g. DW_AT_location).
  /// \returns Location expressions, or an error if the attribute is missing/invalid.
  LLVM_ABI Expected<DWARFLocationExpressionsVector>
  getLocations(dwarf::Attribute Attr) const;

  /// Return the mangled or short name of a subprogram or inlined subroutine DIE.
  ///
  /// If a DIE represents a subprogram (or inlined subroutine), returns its
  /// mangled name (or short name, if mangled is missing). This name may be
  /// fetched from specification or abstract origin for this subprogram.
  /// Returns null if no name is found.
  ///
  /// \param Kind Whether to prefer linkage name or short name.
  /// \returns The mangled or short name, or null if no name is found.
  LLVM_ABI const char *getSubroutineName(DINameKind Kind) const;

  /// Return the DIE name, resolving specification or abstract origin if needed.
  ///
  /// Return the DIE name resolving DW_AT_specification or DW_AT_abstract_origin
  /// references if necessary. For the LinkageName case it additionaly searches
  /// for ShortName if LinkageName is not found.
  /// Returns null if no name is found.
  ///
  /// \param Kind Whether to prefer linkage name or short name.
  /// \returns The DIE name, or null if no name is found.
  LLVM_ABI const char *getName(DINameKind Kind) const;
  /// Append this DIE's unqualified type name to \p OS (optionally keep original spelling).
  ///
  /// \param OS Stream to append the unqualified type name to.
  /// \param OriginalFullName If non-null, set to the original full spelling before
  /// any rewriting.
  LLVM_ABI void getFullName(raw_string_ostream &OS,
                            std::string *OriginalFullName = nullptr) const;

  /// Return the DIE short name resolving DW_AT_specification or
  /// DW_AT_abstract_origin references if necessary. Returns null if no name
  /// is found.
  ///
  /// \returns The DIE short name, or null if no name is found.
  LLVM_ABI const char *getShortName() const;

  /// Return the DIE linkage name resolving DW_AT_specification or
  /// DW_AT_abstract_origin references if necessary. Returns null if no name
  /// is found.
  ///
  /// \returns The DIE linkage name, or null if no name is found.
  LLVM_ABI const char *getLinkageName() const;

  /// Return the declaration line for a subprogram DIE.
  ///
  /// Returns the declaration line (start line) for a DIE, assuming it specifies
  /// a subprogram. This may be fetched from specification or abstract origin
  /// for this subprogram by resolving DW_AT_sepcification or
  /// DW_AT_abstract_origin references if necessary.
  ///
  /// \returns The declaration line number for this subprogram DIE.
  LLVM_ABI uint64_t getDeclLine() const;
  /// Declaration file path for this DIE (via DW_AT_decl_file), formatted per \p Kind.
  ///
  /// \param Kind How to format the declaration file path.
  /// \returns The declaration file path string.
  LLVM_ABI std::string
  getDeclFile(DILineInfoSpecifier::FileLineInfoKind Kind) const;

  /// Fill call-site file, line, column, and discriminator from this DIE.
  ///
  /// Retrieves values of DW_AT_call_file, DW_AT_call_line and DW_AT_call_column
  /// from DIE (or zeroes if they are missing). This function looks for
  /// DW_AT_call attributes in this DIE only, it will not resolve the attribute
  /// values in any DW_AT_specification or DW_AT_abstract_origin DIEs.
  /// \param CallFile filled in with non-zero if successful, zero if there is no
  /// DW_AT_call_file attribute in this DIE.
  /// \param CallLine filled in with non-zero if successful, zero if there is no
  /// DW_AT_call_line attribute in this DIE.
  /// \param CallColumn filled in with non-zero if successful, zero if there is
  /// no DW_AT_call_column attribute in this DIE.
  /// \param CallDiscriminator filled in with non-zero if successful, zero if
  /// there is no DW_AT_GNU_discriminator attribute in this DIE.
  LLVM_ABI void getCallerFrame(uint32_t &CallFile, uint32_t &CallLine,
                               uint32_t &CallColumn,
                               uint32_t &CallDiscriminator) const;

  class attribute_iterator;

  /// Get an iterator range to all attributes in the current DIE only.
  ///
  /// \returns an iterator range for the attributes of the current DIE.
  LLVM_ABI iterator_range<attribute_iterator> attributes() const;

  /// Gets the type size (in bytes) for this DIE.
  ///
  /// \param PointerSize the pointer size of the containing CU.
  /// \returns if this is a type DIE, or this DIE contains a DW_AT_type, returns
  /// the size of the type.
  LLVM_ABI std::optional<uint64_t> getTypeSize(uint64_t PointerSize);

  class iterator;

  /// Iterator to the first child DIE of this DIE.
  ///
  /// \returns An iterator to the first child DIE.
  iterator begin() const;
  /// Past-the-end iterator over this DIE's children (the terminating null DIE).
  ///
  /// \returns The past-the-end child iterator.
  iterator end() const;

  /// Reverse iterator to the last child of this DIE.
  ///
  /// \returns A reverse iterator to the last child.
  std::reverse_iterator<iterator> rbegin() const;
  /// Past-the-end reverse iterator over this DIE's children.
  ///
  /// \returns The past-the-end reverse iterator.
  std::reverse_iterator<iterator> rend() const;

  /// Iterator range over this DIE's immediate children (excluding the null terminator).
  ///
  /// \returns An iterator range over this DIE's immediate children.
  iterator_range<iterator> children() const;
};

/// Forward iterator over the attributes of a single DWARFDie.
class DWARFDie::attribute_iterator
    : public iterator_facade_base<attribute_iterator, std::forward_iterator_tag,
                                  const DWARFAttribute> {
  /// The DWARF DIE we are extracting attributes from.
  DWARFDie Die;
  /// The value vended to clients via the operator*() or operator->().
  DWARFAttribute AttrValue;
  /// The attribute index within the abbreviation declaration in Die.
  uint32_t Index;

  /// True if both attribute iterators refer to the same DIE and attribute index.
  ///
  /// \returns True if both iterators refer to the same DIE and attribute index.
  friend bool operator==(const attribute_iterator &LHS,
                         const attribute_iterator &RHS);

  /// Update the attribute index and attempt to read the attribute value. If the
  /// attribute is able to be read, update AttrValue and the Index member
  /// variable. If the attribute value is not able to be read, an appropriate
  /// error will be set if the Err member variable is non-NULL and the iterator
  /// will be set to the end value so iteration stops.
  void updateForIndex(const DWARFAbbreviationDeclaration &AbbrDecl, uint32_t I);

public:
  /// Deleted; attribute iterators must be constructed from a DIE and end flag.
  attribute_iterator() = delete;
  /// Construct an attribute iterator for DIE \p D; \p End selects the end position.
  ///
  /// \param D DIE whose attributes will be iterated.
  /// \param End If true, construct an end iterator; otherwise start at the first
  /// attribute.
  LLVM_ABI explicit attribute_iterator(DWARFDie D, bool End);

  /// Advance to the next attribute in the DIE's abbreviation.
  ///
  /// \returns A reference to this iterator.
  LLVM_ABI attribute_iterator &operator++();
  /// Move to the previous attribute in the DIE's abbreviation.
  ///
  /// \returns A reference to this iterator.
  LLVM_ABI attribute_iterator &operator--();
  /// True if the current attribute value was successfully read.
  ///
  /// \returns True if the current attribute value was successfully read.
  explicit operator bool() const { return AttrValue.isValid(); }
  /// Access the current attribute value.
  ///
  /// \returns The current attribute value.
  const DWARFAttribute &operator*() const { return AttrValue; }
};

/// True if both attribute iterators refer to the same DIE and attribute index.
///
/// \param LHS First attribute iterator to compare.
/// \param RHS Second attribute iterator to compare.
/// \returns True if both iterators refer to the same DIE and attribute index.
inline bool operator==(const DWARFDie::attribute_iterator &LHS,
                       const DWARFDie::attribute_iterator &RHS) {
  return LHS.Index == RHS.Index;
}

/// True if the iterators refer to different attribute indices.
///
/// \param LHS First attribute iterator to compare.
/// \param RHS Second attribute iterator to compare.
/// \returns True if the iterators refer to different attribute indices.
inline bool operator!=(const DWARFDie::attribute_iterator &LHS,
                       const DWARFDie::attribute_iterator &RHS) {
  return !(LHS == RHS);
}

/// True if \p LHS and \p RHS refer to the same unit and debug-info entry.
///
/// \param LHS First DIE to compare.
/// \param RHS Second DIE to compare.
/// \returns True if both DIEs refer to the same unit and debug-info entry.
inline bool operator==(const DWARFDie &LHS, const DWARFDie &RHS) {
  return LHS.getDebugInfoEntry() == RHS.getDebugInfoEntry() &&
         LHS.getDwarfUnit() == RHS.getDwarfUnit();
}

/// True if \p LHS and \p RHS refer to different units or debug-info entries.
///
/// \param LHS First DIE to compare.
/// \param RHS Second DIE to compare.
/// \returns True if the DIEs refer to different units or debug-info entries.
inline bool operator!=(const DWARFDie &LHS, const DWARFDie &RHS) {
  return !(LHS == RHS);
}

/// True if \p LHS's section offset is less than \p RHS's.
///
/// \param LHS First DIE to compare by section offset.
/// \param RHS Second DIE to compare by section offset.
/// \returns True if \p LHS's section offset is less than \p RHS's.
inline bool operator<(const DWARFDie &LHS, const DWARFDie &RHS) {
  return LHS.getOffset() < RHS.getOffset();
}

/// Bidirectional iterator over the immediate children of a DWARFDie.
class DWARFDie::iterator
    : public iterator_facade_base<iterator, std::bidirectional_iterator_tag,
                                  const DWARFDie> {
  DWARFDie Die;

  friend std::reverse_iterator<llvm::DWARFDie::iterator>;
  /// True if both iterators refer to the same DIE.
  ///
  /// \returns True if both iterators refer to the same DIE.
  friend bool operator==(const DWARFDie::iterator &LHS,
                         const DWARFDie::iterator &RHS);

public:
  /// Default-construct an empty DIE iterator.
  iterator() = default;

  /// Construct an iterator positioned at DIE \p D.
  ///
  /// \param D DIE to position this iterator at.
  explicit iterator(DWARFDie D) : Die(D) {}

  /// Advance to this DIE's next sibling.
  ///
  /// \returns A reference to this iterator.
  iterator &operator++() {
    Die = Die.getSibling();
    return *this;
  }

  /// Move to this DIE's previous sibling.
  ///
  /// \returns A reference to this iterator.
  iterator &operator--() {
    Die = Die.getPreviousSibling();
    return *this;
  }

  /// Dereference to the DIE currently pointed to by this iterator.
  ///
  /// \returns The DIE currently pointed to by this iterator.
  const DWARFDie &operator*() const { return Die; }
};

/// True if both DIE child iterators refer to the same DIE.
///
/// \param LHS First iterator to compare.
/// \param RHS Second iterator to compare.
/// \returns True if both iterators refer to the same DIE.
inline bool operator==(const DWARFDie::iterator &LHS,
                       const DWARFDie::iterator &RHS) {
  return LHS.Die == RHS.Die;
}

// These inline functions must follow the DWARFDie::iterator definition above
// as they use functions from that class.
inline DWARFDie::iterator DWARFDie::begin() const {
  return iterator(getFirstChild());
}

inline DWARFDie::iterator DWARFDie::end() const {
  return iterator(getLastChild());
}

inline iterator_range<DWARFDie::iterator> DWARFDie::children() const {
  return make_range(begin(), end());
}

} // end namespace llvm

namespace std {

template <>
class reverse_iterator<llvm::DWARFDie::iterator>
    : public llvm::iterator_facade_base<
          reverse_iterator<llvm::DWARFDie::iterator>,
          bidirectional_iterator_tag, const llvm::DWARFDie> {

private:
  llvm::DWARFDie Die;
  bool AtEnd;

public:
  reverse_iterator(llvm::DWARFDie::iterator It)
      : Die(It.Die), AtEnd(!It.Die.getPreviousSibling()) {
    if (!AtEnd)
      Die = Die.getPreviousSibling();
  }

  llvm::DWARFDie::iterator base() const {
    return llvm::DWARFDie::iterator(AtEnd ? Die : Die.getSibling());
  }

  reverse_iterator<llvm::DWARFDie::iterator> &operator++() {
    assert(!AtEnd && "Incrementing rend");
    llvm::DWARFDie D = Die.getPreviousSibling();
    if (D)
      Die = D;
    else
      AtEnd = true;
    return *this;
  }

  reverse_iterator<llvm::DWARFDie::iterator> &operator--() {
    if (AtEnd) {
      AtEnd = false;
      return *this;
    }
    Die = Die.getSibling();
    assert(!Die.isNULL() && "Decrementing rbegin");
    return *this;
  }

  const llvm::DWARFDie &operator*() const {
    assert(Die.isValid());
    return Die;
  }

  // FIXME: We should be able to specify the equals operator as a friend, but
  //        that causes the compiler to think the operator overload is ambiguous
  //        with the friend declaration and the actual definition as candidates.
  bool equals(const reverse_iterator<llvm::DWARFDie::iterator> &RHS) const {
    return Die == RHS.Die && AtEnd == RHS.AtEnd;
  }
};

} // namespace std

namespace llvm {

/// True if both reverse DIE iterators refer to the same position.
///
/// \param LHS First reverse iterator to compare.
/// \param RHS Second reverse iterator to compare.
/// \returns True if both reverse iterators refer to the same position.
inline bool operator==(const std::reverse_iterator<DWARFDie::iterator> &LHS,
                       const std::reverse_iterator<DWARFDie::iterator> &RHS) {
  return LHS.equals(RHS);
}

/// True if the reverse DIE iterators do not refer to the same position.
///
/// \param LHS First reverse iterator to compare.
/// \param RHS Second reverse iterator to compare.
/// \returns True if the reverse iterators do not refer to the same position.
inline bool operator!=(const std::reverse_iterator<DWARFDie::iterator> &LHS,
                       const std::reverse_iterator<DWARFDie::iterator> &RHS) {
  return !(LHS == RHS);
}

inline std::reverse_iterator<DWARFDie::iterator> DWARFDie::rbegin() const {
  return std::make_reverse_iterator(end());
}

inline std::reverse_iterator<DWARFDie::iterator> DWARFDie::rend() const {
  return std::make_reverse_iterator(begin());
}

/// Dump the qualified type name of \p DIE to \p OS.
///
/// \param DIE DIE whose qualified type name to dump.
/// \param OS Output stream to write the type name to.
LLVM_ABI void dumpTypeQualifiedName(const DWARFDie &DIE, raw_ostream &OS);
/// Dump the unqualified type name of \p DIE to \p OS.
///
/// \param DIE DIE whose unqualified type name to dump.
/// \param OS Output stream to write the type name to.
/// \param OriginalFullName If non-null, set to the original full spelling before
/// any rewriting.
LLVM_ABI void dumpTypeUnqualifiedName(const DWARFDie &DIE, raw_ostream &OS,
                                      std::string *OriginalFullName = nullptr);

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFDIE_H
