//===- DWARFFormValue.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFFORMVALUE_H
#define LLVM_DEBUGINFO_DWARF_DWARFFORMVALUE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataExtractor.h"
#include <cstdint>

namespace llvm {

class DWARFContext;
class DWARFObject;
class DWARFDataExtractor;
class DWARFUnit;
class raw_ostream;

/// A parsed DWARF attribute form and its extracted value.
class DWARFFormValue {
public:
  /// High-level classification of a DWARF form's value representation.
  enum FormClass {
    FC_Unknown,       ///< Form class could not be determined.
    FC_Address,       ///< Address forms (e.g. DW_FORM_addr).
    FC_Block,         ///< Block of bytes (e.g. DW_FORM_block).
    FC_Constant,      ///< Integer constant forms (e.g. DW_FORM_data*).
    FC_String,        ///< String forms (e.g. DW_FORM_string, DW_FORM_strp).
    FC_Flag,          ///< Flag forms (e.g. DW_FORM_flag).
    FC_Reference,     ///< Reference forms (e.g. DW_FORM_ref*).
    FC_Indirect,      ///< Indirect form (DW_FORM_indirect).
    FC_SectionOffset, ///< Section offset forms (e.g. DW_FORM_sec_offset).
    FC_Exprloc        ///< Expression location forms (DW_FORM_exprloc).
  };

  /// Storage for the extracted form value (integer, string, or block data).
  struct ValueType {
    /// Construct with an unsigned value of zero.
    ValueType() { uval = 0; }
    /// Construct holding signed integer \p V.
    ///
    /// \param V Signed integer to store.
    ValueType(int64_t V) : sval(V) {}
    /// Construct holding unsigned integer \p V.
    ///
    /// \param V Unsigned integer to store.
    ValueType(uint64_t V) : uval(V) {}
    /// Construct holding C string pointer \p V.
    ///
    /// \param V C string pointer to store.
    ValueType(const char *V) : cstr(V) {}

    union {
      uint64_t uval; ///< Unsigned integer form value.
      int64_t sval;  ///< Signed integer form value.
      const char *cstr; ///< In-memory C string for string form values.
    };
    /// Pointer into section data for block or indirect form values.
    const uint8_t *data = nullptr;
    /// Section index for reference forms.
    uint64_t SectionIndex;
  };

private:
  dwarf::Form Form; /// Form for this value.
  dwarf::DwarfFormat Format =
      dwarf::DWARF32;           /// Remember the DWARF format at extract time.
  ValueType Value;              /// Contains all data for the form.
  const DWARFUnit *U = nullptr; /// Remember the DWARFUnit at extract time.
  const DWARFContext *C = nullptr; /// Context for extract time.

  DWARFFormValue(dwarf::Form F, const ValueType &V) : Form(F), Value(V) {}

public:
  /// Construct a form value with form \p F and an empty value.
  ///
  /// \param F DWARF form to associate with this value.
  DWARFFormValue(dwarf::Form F = dwarf::Form(0)) : Form(F) {}

  /// Create a form value holding signed integer \p V for form \p F.
  ///
  /// \param F DWARF form to associate with the value.
  /// \param V Signed integer payload.
  /// \returns A form value containing \p V.
  LLVM_ABI static DWARFFormValue createFromSValue(dwarf::Form F, int64_t V);
  /// Create a form value holding unsigned integer \p V for form \p F.
  ///
  /// \param F DWARF form to associate with the value.
  /// \param V Unsigned integer payload.
  /// \returns A form value containing \p V.
  LLVM_ABI static DWARFFormValue createFromUValue(dwarf::Form F, uint64_t V);
  /// Create a form value holding C string pointer \p V for form \p F.
  ///
  /// \param F DWARF form to associate with the value.
  /// \param V C string pointer payload.
  /// \returns A form value containing \p V.
  LLVM_ABI static DWARFFormValue createFromPValue(dwarf::Form F, const char *V);
  /// Create a form value holding block bytes \p D for form \p F.
  ///
  /// \param F DWARF form to associate with the value.
  /// \param D Block bytes to store.
  /// \returns A form value containing \p D.
  LLVM_ABI static DWARFFormValue createFromBlockValue(dwarf::Form F,
                                                      ArrayRef<uint8_t> D);
  /// Extract a form value of form \p F from \p Unit at \p *OffsetPtr.
  ///
  /// \param F DWARF form to extract.
  /// \param Unit DWARF unit providing section and format context.
  /// \param OffsetPtr Offset into the unit data; advanced past the value.
  /// \returns The extracted form value.
  LLVM_ABI static DWARFFormValue
  createFromUnit(dwarf::Form F, const DWARFUnit *Unit, uint64_t *OffsetPtr);
  /// Interpret \p Val/\p Form as a sectioned address in \p U, if possible.
  ///
  /// \param Val Extracted value storage to interpret.
  /// \param Form DWARF form describing \p Val.
  /// \param U DWARF unit used to resolve section information.
  /// \returns The sectioned address, or std::nullopt if not an address form.
  LLVM_ABI static std::optional<object::SectionedAddress>
  getAsSectionedAddress(const ValueType &Val, const dwarf::Form Form,
                        const DWARFUnit *U);

  /// Return the DWARF form of this value.
  ///
  /// \returns The DWARF form of this value.
  dwarf::Form getForm() const { return Form; }
  /// Return the raw unsigned payload stored for this form value.
  ///
  /// \returns The raw unsigned payload.
  uint64_t getRawUValue() const { return Value.uval; }

  /// True if this value's form belongs to form class \p FC.
  ///
  /// \param FC Form class to test membership against.
  /// \returns true if this form belongs to \p FC.
  LLVM_ABI bool isFormClass(FormClass FC) const;
  /// Return the DWARF unit this value was extracted from, if any.
  ///
  /// \returns The DWARF unit, or nullptr if none was recorded.
  const DWARFUnit *getUnit() const { return U; }
  /// Print this form value to \p OS using \p DumpOpts.
  ///
  /// \param OS Output stream to write to.
  /// \param DumpOpts Options controlling dump formatting.
  LLVM_ABI void dump(raw_ostream &OS,
                     DIDumpOptions DumpOpts = DIDumpOptions()) const;
  /// Print sectioned address \p SA to \p OS using \p DumpOpts.
  ///
  /// \param OS Output stream to write to.
  /// \param DumpOpts Options controlling dump formatting.
  /// \param SA Sectioned address to print.
  LLVM_ABI void dumpSectionedAddress(raw_ostream &OS, DIDumpOptions DumpOpts,
                                     object::SectionedAddress SA) const;
  /// Print address \p Address to \p OS.
  ///
  /// \param OS Output stream to write to.
  /// \param Address Address value to print.
  LLVM_ABI void dumpAddress(raw_ostream &OS, uint64_t Address) const;
  /// Print \p Address using \p AddressSize bytes to \p OS.
  ///
  /// \param OS Output stream to write to.
  /// \param AddressSize Number of bytes used to represent the address.
  /// \param Address Address value to print.
  LLVM_ABI static void dumpAddress(raw_ostream &OS, uint8_t AddressSize,
                                   uint64_t Address);
  /// Print the name of section \p SectionIndex from \p Obj to \p OS.
  ///
  /// \param Obj DWARF object providing section names.
  /// \param OS Output stream to write to.
  /// \param DumpOpts Options controlling dump formatting.
  /// \param SectionIndex Index of the section whose name to print.
  LLVM_ABI static void dumpAddressSection(const DWARFObject &Obj,
                                          raw_ostream &OS,
                                          DIDumpOptions DumpOpts,
                                          uint64_t SectionIndex);

  /// Extract a form value from \p Data at \p *OffsetPtr.
  ///
  /// The information in \p FormParams is needed to interpret some forms. The
  /// optional \p Context and \p Unit allow extracting information if the form
  /// refers to other sections (e.g., .debug_str).
  ///
  /// \param Data DWARF data to extract from.
  /// \param OffsetPtr Offset into \p Data; advanced past the extracted value.
  /// \param FormParams DWARF parameters needed to interpret some forms.
  /// \param Context Optional context for resolving cross-section forms.
  /// \param Unit Optional unit for resolving unit-relative forms.
  /// \returns true on success, false on failure.
  LLVM_ABI bool extractValue(const DWARFDataExtractor &Data,
                             uint64_t *OffsetPtr, dwarf::FormParams FormParams,
                             const DWARFContext *Context = nullptr,
                             const DWARFUnit *Unit = nullptr);

  /// Extract a value using form parameters and unit \p U (no separate context).
  ///
  /// \param Data DWARF data to extract from.
  /// \param OffsetPtr Offset into \p Data; advanced past the extracted value.
  /// \param FormParams DWARF parameters needed to interpret some forms.
  /// \param U DWARF unit used as extract context.
  /// \returns true on success, false on failure.
  bool extractValue(const DWARFDataExtractor &Data, uint64_t *OffsetPtr,
                    dwarf::FormParams FormParams, const DWARFUnit *U) {
    return extractValue(Data, OffsetPtr, FormParams, nullptr, U);
  }

  /// Return a unit-relative reference if the form is a relative ref form.
  ///
  /// \returns The relative reference, or std::nullopt if the form is unsuitable.
  LLVM_ABI std::optional<uint64_t> getAsRelativeReference() const;
  /// Return a .debug_info relative reference if the form is a ref form.
  ///
  /// \returns The .debug_info reference, or std::nullopt if the form is unsuitable.
  LLVM_ABI std::optional<uint64_t> getAsDebugInfoReference() const;
  /// Return the type-unit signature if the form is DW_FORM_ref_sig8.
  ///
  /// \returns The type-unit signature, or std::nullopt if the form is unsuitable.
  LLVM_ABI std::optional<uint64_t> getAsSignatureReference() const;
  /// Return a supplementary/alt reference if the form is ref_sup* or GNU_ref_alt.
  ///
  /// \returns The supplementary reference, or std::nullopt if the form is unsuitable.
  LLVM_ABI std::optional<uint64_t> getAsSupplementaryReference() const;
  /// Return the value as an unsigned constant if the form is FC_Constant or
  /// FC_Flag (except DW_FORM_sdata).
  ///
  /// \returns The unsigned constant, or std::nullopt if the form is unsuitable.
  LLVM_ABI std::optional<uint64_t> getAsUnsignedConstant() const;
  /// Return the value as a signed constant if the form is FC_Constant.
  ///
  /// \returns The signed constant, or std::nullopt if the form is unsuitable.
  LLVM_ABI std::optional<int64_t> getAsSignedConstant() const;
  /// Return this value as a C string if the form is a string form.
  ///
  /// \returns The C string on success, or an error if the form is unsuitable.
  LLVM_ABI Expected<const char *> getAsCString() const;
  /// Return this value as an address if the form is an address form.
  ///
  /// \returns The address, or std::nullopt if the form is unsuitable.
  LLVM_ABI std::optional<uint64_t> getAsAddress() const;
  /// Return this value as a sectioned address, if the form is an address.
  ///
  /// \returns The sectioned address, or std::nullopt if the form is unsuitable.
  LLVM_ABI std::optional<object::SectionedAddress>
  getAsSectionedAddress() const;
  /// Return a section offset if the form is FC_SectionOffset.
  ///
  /// \returns The section offset, or std::nullopt if the form is unsuitable.
  LLVM_ABI std::optional<uint64_t> getAsSectionOffset() const;
  /// Return the value as a byte block if the form is FC_Block or FC_Exprloc.
  ///
  /// \returns The byte block, or std::nullopt if the form is unsuitable.
  LLVM_ABI std::optional<ArrayRef<uint8_t>> getAsBlock() const;
  /// Return the string-table offset if the form is a string pointer form.
  ///
  /// \returns The string-table offset, or std::nullopt if the form is unsuitable.
  LLVM_ABI std::optional<uint64_t> getAsCStringOffset() const;
  /// Return the raw unsigned reference value without unit-relative adjustment.
  ///
  /// \returns The raw unsigned reference, or std::nullopt if the form is unsuitable.
  LLVM_ABI std::optional<uint64_t> getAsReferenceUVal() const;
  /// Correctly extract any file paths from a form value.
  ///
  /// These attributes can be in the from DW_AT_decl_file or DW_AT_call_file
  /// attributes. We need to use the file index in the correct DWARFUnit's line
  /// table prologue, and each DWARFFormValue has the DWARFUnit the form value
  /// was extracted from.
  ///
  /// \param Kind The kind of path to extract.
  ///
  /// \returns A valid string value on success, or std::nullopt if the form
  /// class is not FC_Constant, or if the file index is not valid.
  LLVM_ABI std::optional<std::string>
  getAsFile(DILineInfoSpecifier::FileLineInfoKind Kind) const;

  /// Skip a form's value in \p DebugInfoData at the offset specified by
  /// \p OffsetPtr.
  ///
  /// Skips the bytes for the current form and updates the offset.
  ///
  /// \param DebugInfoData The data where we want to skip the value.
  /// \param OffsetPtr A reference to the offset that will be updated.
  /// \param Params DWARF parameters to help interpret forms.
  /// \returns true on success, false if the form was not skipped.
  bool skipValue(DataExtractor DebugInfoData, uint64_t *OffsetPtr,
                 const dwarf::FormParams Params) const {
    return DWARFFormValue::skipValue(Form, DebugInfoData, OffsetPtr, Params);
  }

  /// Skip a form's value in \p DebugInfoData at the offset specified by
  /// \p OffsetPtr.
  ///
  /// Skips the bytes for the specified form and updates the offset.
  ///
  /// \param Form The DW_FORM enumeration that indicates the form to skip.
  /// \param DebugInfoData The data where we want to skip the value.
  /// \param OffsetPtr A reference to the offset that will be updated.
  /// \param FormParams DWARF parameters to help interpret forms.
  /// \returns true on success, false if the form was not skipped.
  LLVM_ABI static bool skipValue(dwarf::Form Form, DataExtractor DebugInfoData,
                                 uint64_t *OffsetPtr,
                                 const dwarf::FormParams FormParams);

private:
  void dumpString(raw_ostream &OS) const;
};

namespace dwarf {

/// Take an optional DWARFFormValue and try to extract a string value from it.
///
/// \param V and optional DWARFFormValue to attempt to extract the value from.
/// \returns an optional value that contains a value if the form value
/// was valid and was a string.
inline std::optional<const char *>
toString(const std::optional<DWARFFormValue> &V) {
  if (!V)
    return std::nullopt;
  Expected<const char*> E = V->getAsCString();
  if (!E) {
    consumeError(E.takeError());
    return std::nullopt;
  }
  return *E;
}

/// Take an optional DWARFFormValue and try to extract a string value from it.
///
/// \param V and optional DWARFFormValue to attempt to extract the value from.
/// \param Default the default value to return in case of failure.
/// \returns the string value or Default if the V doesn't have a value or the
/// form value's encoding wasn't a string.
inline StringRef toStringRef(const std::optional<DWARFFormValue> &V,
                             StringRef Default = {}) {
  if (!V)
    return Default;
  auto S = V->getAsCString();
  if (!S) {
    consumeError(S.takeError());
    return Default;
  }
  if (!*S)
    return Default;
  return *S;
}

/// Take an optional DWARFFormValue and extract a string value from it.
///
/// \param V and optional DWARFFormValue to attempt to extract the value from.
/// \param Default the default value to return in case of failure.
/// \returns the string value or Default if the V doesn't have a value or the
/// form value's encoding wasn't a string.
inline const char *toString(const std::optional<DWARFFormValue> &V,
                            const char *Default) {
  if (auto E = toString(V))
    return *E;
  return Default;
}

/// Take an optional DWARFFormValue and try to extract an unsigned constant.
///
/// \param V and optional DWARFFormValue to attempt to extract the value from.
/// \returns an optional value that contains a value if the form value
/// was valid and has a unsigned constant form.
inline std::optional<uint64_t>
toUnsigned(const std::optional<DWARFFormValue> &V) {
  if (V)
    return V->getAsUnsignedConstant();
  return std::nullopt;
}

/// Take an optional DWARFFormValue and extract a unsigned constant.
///
/// \param V and optional DWARFFormValue to attempt to extract the value from.
/// \param Default the default value to return in case of failure.
/// \returns the extracted unsigned value or Default if the V doesn't have a
/// value or the form value's encoding wasn't an unsigned constant form.
inline uint64_t toUnsigned(const std::optional<DWARFFormValue> &V,
                           uint64_t Default) {
  return toUnsigned(V).value_or(Default);
}

/// Take an optional DWARFFormValue and try to extract a relative offset
/// reference.
///
/// \param V an optional DWARFFormValue to attempt to extract the value from.
/// \returns an optional value that contains a value if the form value
/// was valid and has a relative reference form.
inline std::optional<uint64_t>
toRelativeReference(const std::optional<DWARFFormValue> &V) {
  if (V)
    return V->getAsRelativeReference();
  return std::nullopt;
}

/// Take an optional DWARFFormValue and extract a relative offset reference.
///
/// \param V an optional DWARFFormValue to attempt to extract the value from.
/// \param Default the default value to return in case of failure.
/// \returns the extracted reference value or Default if the V doesn't have a
/// value or the form value's encoding wasn't a relative offset reference form.
inline uint64_t toRelativeReference(const std::optional<DWARFFormValue> &V,
                                    uint64_t Default) {
  return toRelativeReference(V).value_or(Default);
}

/// Take an optional DWARFFormValue and try to extract an absolute debug info
/// offset reference.
///
/// \param V an optional DWARFFormValue to attempt to extract the value from.
/// \returns an optional value that contains a value if the form value
/// was valid and has an (absolute) debug info offset reference form.
inline std::optional<uint64_t>
toDebugInfoReference(const std::optional<DWARFFormValue> &V) {
  if (V)
    return V->getAsDebugInfoReference();
  return std::nullopt;
}

/// Take an optional DWARFFormValue and extract an absolute debug info offset
/// reference.
///
/// \param V an optional DWARFFormValue to attempt to extract the value from.
/// \param Default the default value to return in case of failure.
/// \returns the extracted reference value or Default if the V doesn't have a
/// value or the form value's encoding wasn't an absolute debug info offset
/// reference form.
inline uint64_t toDebugInfoReference(const std::optional<DWARFFormValue> &V,
                                     uint64_t Default) {
  return toDebugInfoReference(V).value_or(Default);
}

/// Take an optional DWARFFormValue and try to extract a signature reference.
///
/// \param V an optional DWARFFormValue to attempt to extract the value from.
/// \returns an optional value that contains a value if the form value
/// was valid and has a signature reference form.
inline std::optional<uint64_t>
toSignatureReference(const std::optional<DWARFFormValue> &V) {
  if (V)
    return V->getAsSignatureReference();
  return std::nullopt;
}

/// Take an optional DWARFFormValue and extract a signature reference.
///
/// \param V an optional DWARFFormValue to attempt to extract the value from.
/// \param Default the default value to return in case of failure.
/// \returns the extracted reference value or Default if the V doesn't have a
/// value or the form value's encoding wasn't a signature reference form.
inline uint64_t toSignatureReference(const std::optional<DWARFFormValue> &V,
                                     uint64_t Default) {
  return toSignatureReference(V).value_or(Default);
}

/// Take an optional DWARFFormValue and try to extract a supplementary debug
/// info reference.
///
/// \param V an optional DWARFFormValue to attempt to extract the value from.
/// \returns an optional value that contains a value if the form value
/// was valid and has a supplementary reference form.
inline std::optional<uint64_t>
toSupplementaryReference(const std::optional<DWARFFormValue> &V) {
  if (V)
    return V->getAsSupplementaryReference();
  return std::nullopt;
}

/// Take an optional DWARFFormValue and extract a supplementary debug info
/// reference.
///
/// \param V an optional DWARFFormValue to attempt to extract the value from.
/// \param Default the default value to return in case of failure.
/// \returns the extracted reference value or Default if the V doesn't have a
/// value or the form value's encoding wasn't a supplementary reference form.
inline uint64_t toSupplementaryReference(const std::optional<DWARFFormValue> &V,
                                         uint64_t Default) {
  return toSupplementaryReference(V).value_or(Default);
}

/// Take an optional DWARFFormValue and try to extract an signed constant.
///
/// \param V and optional DWARFFormValue to attempt to extract the value from.
/// \returns an optional value that contains a value if the form value
/// was valid and has a signed constant form.
inline std::optional<int64_t> toSigned(const std::optional<DWARFFormValue> &V) {
  if (V)
    return V->getAsSignedConstant();
  return std::nullopt;
}

/// Take an optional DWARFFormValue and extract a signed integer.
///
/// \param V and optional DWARFFormValue to attempt to extract the value from.
/// \param Default the default value to return in case of failure.
/// \returns the extracted signed integer value or Default if the V doesn't
/// have a value or the form value's encoding wasn't a signed integer form.
inline int64_t toSigned(const std::optional<DWARFFormValue> &V,
                        int64_t Default) {
  return toSigned(V).value_or(Default);
}

/// Take an optional DWARFFormValue and try to extract an address.
///
/// \param V and optional DWARFFormValue to attempt to extract the value from.
/// \returns an optional value that contains a value if the form value
/// was valid and has a address form.
inline std::optional<uint64_t>
toAddress(const std::optional<DWARFFormValue> &V) {
  if (V)
    return V->getAsAddress();
  return std::nullopt;
}

/// Extract a sectioned address from optional form value \p V, if present.
///
/// \param V and optional DWARFFormValue to attempt to extract the value from.
/// \returns an optional value that contains a value if the form value
/// was valid and has an address form.
inline std::optional<object::SectionedAddress>
toSectionedAddress(const std::optional<DWARFFormValue> &V) {
  if (V)
    return V->getAsSectionedAddress();
  return std::nullopt;
}

/// Take an optional DWARFFormValue and extract a address.
///
/// \param V and optional DWARFFormValue to attempt to extract the value from.
/// \param Default the default value to return in case of failure.
/// \returns the extracted address value or Default if the V doesn't have a
/// value or the form value's encoding wasn't an address form.
inline uint64_t toAddress(const std::optional<DWARFFormValue> &V,
                          uint64_t Default) {
  return toAddress(V).value_or(Default);
}

/// Take an optional DWARFFormValue and try to extract an section offset.
///
/// \param V and optional DWARFFormValue to attempt to extract the value from.
/// \returns an optional value that contains a value if the form value
/// was valid and has a section offset form.
inline std::optional<uint64_t>
toSectionOffset(const std::optional<DWARFFormValue> &V) {
  if (V)
    return V->getAsSectionOffset();
  return std::nullopt;
}

/// Take an optional DWARFFormValue and extract a section offset.
///
/// \param V and optional DWARFFormValue to attempt to extract the value from.
/// \param Default the default value to return in case of failure.
/// \returns the extracted section offset value or Default if the V doesn't
/// have a value or the form value's encoding wasn't a section offset form.
inline uint64_t toSectionOffset(const std::optional<DWARFFormValue> &V,
                                uint64_t Default) {
  return toSectionOffset(V).value_or(Default);
}

/// Take an optional DWARFFormValue and try to extract block data.
///
/// \param V and optional DWARFFormValue to attempt to extract the value from.
/// \returns an optional value that contains a value if the form value
/// was valid and has a block form.
inline std::optional<ArrayRef<uint8_t>>
toBlock(const std::optional<DWARFFormValue> &V) {
  if (V)
    return V->getAsBlock();
  return std::nullopt;
}

/// Check whether specified \p Form belongs to the \p FC class.
/// \param Form an attribute form.
/// \param FC an attribute form class to check.
/// \param DwarfVersion the version of DWARF debug info keeping the attribute.
/// \returns true if specified \p Form belongs to the \p FC class.
LLVM_ABI bool doesFormBelongToClass(dwarf::Form Form,
                                    DWARFFormValue::FormClass FC,
                                    uint16_t DwarfVersion);

} // end namespace dwarf

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFFORMVALUE_H
