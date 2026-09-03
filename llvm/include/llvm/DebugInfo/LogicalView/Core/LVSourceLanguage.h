//===-- LVSourceLanguage.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the LVSourceLanguage struct, a unified representation of
// the source language used in a compile unit.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVSOURCELANGUAGE_H
#define LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVSOURCELANGUAGE_H

#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
namespace logicalview {

/// A source language supported by any of the debug info representations.
struct LVSourceLanguage {
  /// Tag identifying a DWARF source-language encoding in the high 16 bits.
  static constexpr unsigned TagDwarf = 0x00;
  /// Tag identifying a CodeView source-language encoding in the high 16 bits.
  static constexpr unsigned TagCodeView = 0x01;

  /// Tagged source-language code combining a format tag with a language ID.
  enum TaggedLanguage : uint32_t {
    /// Sentinel for an unset or unsupported source language.
    Invalid = -1U,

  // DWARF
/// Expand one DWARF source-language enumerator from Dwarf.def.
///
/// \param ID DWARF language encoding value.
/// \param NAME Enumerator name suffix (forms \c DW_LANG_##NAME).
/// \param LOWER_BOUND Default array lower bound for the language.
/// \param VERSION DWARF version that introduced the encoding.
/// \param VENDOR Vendor that defined the encoding.
#define HANDLE_DW_LANG(ID, NAME, LOWER_BOUND, VERSION, VENDOR)                 \
  DW_LANG_##NAME = (TagDwarf << 16) | ID,
#include "llvm/BinaryFormat/Dwarf.def"
  // CodeView
#define CV_LANGUAGE(NAME, ID) CV_LANG_##NAME = (TagCodeView << 16) | ID,
#include "llvm/DebugInfo/CodeView/CodeViewLanguages.def"
  };

  /// Construct an invalid source language.
  LVSourceLanguage() = default;
  /// Construct a source language from the DWARF encoding \p SL.
  /// \param SL DWARF source-language encoding to store.
  LVSourceLanguage(llvm::dwarf::SourceLanguage SL)
      : LVSourceLanguage(TagDwarf, SL) {}
  /// Construct a source language from the CodeView encoding \p SL.
  /// \param SL CodeView source-language encoding to store.
  LVSourceLanguage(llvm::codeview::SourceLanguage SL)
      : LVSourceLanguage(TagCodeView, SL) {}
  /// Return whether this source language equals \p SL.
  /// \param SL Source language to compare against.
  /// \returns True when both store the same tagged language value.
  bool operator==(const LVSourceLanguage &SL) const {
    return get() == SL.get();
  }
  /// Return whether this source language equals the tagged value \p TL.
  /// \param TL Tagged language value to compare against.
  /// \returns True when this stores the same tagged language value.
  bool operator==(const LVSourceLanguage::TaggedLanguage &TL) const {
    return get() == TL;
  }

  /// Return whether this holds a recognized source-language encoding.
  /// \returns True when the stored language is not Invalid.
  bool isValid() const { return Language != Invalid; }
  /// Return the tagged source-language value stored in this object.
  /// \returns The tagged language enumerator value.
  TaggedLanguage get() const { return Language; }
  /// Return the printable name of the stored source language.
  /// \returns Language name string, or an empty string when invalid.
  LLVM_ABI StringRef getName() const;

private:
  TaggedLanguage Language = Invalid;

  LVSourceLanguage(unsigned Tag, unsigned Lang)
      : Language(static_cast<TaggedLanguage>((Tag << 16) | Lang)) {}
  unsigned getTag() const { return Language >> 16; }
  unsigned getLang() const { return Language & 0xffff; }
};

} // end namespace logicalview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVSOURCELANGUAGE_H
