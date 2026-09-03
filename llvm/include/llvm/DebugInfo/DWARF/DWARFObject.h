//===- DWARFObject.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===-----------------------------------------------------------------------===/

#ifndef LLVM_DEBUGINFO_DWARF_DWARFOBJECT_H
#define LLVM_DEBUGINFO_DWARF_DWARFOBJECT_H

#include "llvm/DebugInfo/DWARF/DWARFRelocMap.h"
#include "llvm/DebugInfo/DWARF/DWARFSection.h"
#include "llvm/Object/ObjectFile.h"
#include <optional>

namespace llvm {
/// Low-level access to DWARF sections in an object file.
///
/// Abstract view of DWARF sections in an object file.
///
/// Finds required sections and computes relocated values. Default get*Section
/// methods return dummy/empty values so clients can override only what they
/// need. The parser may call unused accessors eagerly, so defaults must not
/// abort.
class DWARFObject {
  DWARFSection Dummy;

public:
  /// Destroy this DWARF object view.
  virtual ~DWARFObject() = default;
  /// Return the path or identifier of the object file being inspected.
  ///
  /// \returns Path or identifier of the object file.
  virtual StringRef getFileName() const { llvm_unreachable("unimplemented"); }
  /// Return the underlying object file, or nullptr if none.
  ///
  /// \returns Pointer to the object file, or nullptr if none.
  virtual const object::ObjectFile *getFile() const { return nullptr; }
  /// Return the list of section names known to this DWARF object.
  ///
  /// \returns Array of section names known to this object.
  virtual ArrayRef<SectionName> getSectionNames() const { return {}; }
  /// True if this object's DWARF sections are little-endian.
  ///
  /// \returns True if this object's DWARF sections are little-endian.
  virtual bool isLittleEndian() const = 0;
  /// Return the default address size in bytes for this object.
  ///
  /// \returns Default address size in bytes for this object.
  virtual uint8_t getAddressSize() const { llvm_unreachable("unimplemented"); }
  /// Invoke \p F for each .debug_info section in this object.
  ///
  /// \param F Callback invoked with each .debug_info section.
  virtual void
  forEachInfoSections(function_ref<void(const DWARFSection &)> F) const {}
  /// Invoke \p F for each .debug_types section in this object.
  ///
  /// \param F Callback invoked with each .debug_types section.
  virtual void
  forEachTypesSections(function_ref<void(const DWARFSection &)> F) const {}
  /// Return the contents of the .debug_abbrev section, or empty if absent.
  ///
  /// \returns Contents of the .debug_abbrev section, or empty if absent.
  virtual StringRef getAbbrevSection() const { return ""; }
  /// Return the .debug_loc section, or a dummy empty section.
  ///
  /// \returns The .debug_loc section, or a dummy empty section.
  virtual const DWARFSection &getLocSection() const { return Dummy; }
  /// Return the .debug_loclists section, or a dummy empty section.
  ///
  /// \returns The .debug_loclists section, or a dummy empty section.
  virtual const DWARFSection &getLoclistsSection() const { return Dummy; }
  /// Return the contents of the .debug_aranges section, or empty if absent.
  ///
  /// \returns Contents of the .debug_aranges section, or empty if absent.
  virtual StringRef getArangesSection() const { return ""; }
  /// Return the .debug_frame section, or a dummy empty section.
  ///
  /// \returns The .debug_frame section, or a dummy empty section.
  virtual const DWARFSection &getFrameSection() const { return Dummy; }
  /// Return the .eh_frame section, or a dummy empty section.
  ///
  /// \returns The .eh_frame section, or a dummy empty section.
  virtual const DWARFSection &getEHFrameSection() const { return Dummy; }
  /// Return the .debug_line section, or a dummy empty section.
  ///
  /// \returns The .debug_line section, or a dummy empty section.
  virtual const DWARFSection &getLineSection() const { return Dummy; }
  /// Return the .debug_line_str section contents, or an empty string.
  ///
  /// \returns Contents of the .debug_line_str section, or an empty string.
  virtual StringRef getLineStrSection() const { return ""; }
  /// Return the .debug_str section contents, or an empty string.
  ///
  /// \returns Contents of the .debug_str section, or an empty string.
  virtual StringRef getStrSection() const { return ""; }
  /// Return the .debug_ranges section, or a dummy empty section.
  ///
  /// \returns The .debug_ranges section, or a dummy empty section.
  virtual const DWARFSection &getRangesSection() const { return Dummy; }
  /// Return the .debug_rnglists section, or a dummy empty section.
  ///
  /// \returns The .debug_rnglists section, or a dummy empty section.
  virtual const DWARFSection &getRnglistsSection() const { return Dummy; }
  /// Return the .debug_macro section, or a dummy empty section.
  ///
  /// \returns The .debug_macro section, or a dummy empty section.
  virtual const DWARFSection &getMacroSection() const { return Dummy; }
  /// Return the .debug_macro.dwo section contents, or an empty string.
  ///
  /// \returns Contents of the .debug_macro.dwo section, or an empty string.
  virtual StringRef getMacroDWOSection() const { return ""; }
  /// Return the .debug_macinfo section contents, or an empty string.
  ///
  /// \returns Contents of the .debug_macinfo section, or an empty string.
  virtual StringRef getMacinfoSection() const { return ""; }
  /// Return the contents of the .debug_macinfo.dwo section, or empty if absent.
  ///
  /// \returns Contents of the .debug_macinfo.dwo section, or empty if absent.
  virtual StringRef getMacinfoDWOSection() const { return ""; }
  /// Return the .debug_pubnames section, or a dummy empty section.
  ///
  /// \returns The .debug_pubnames section, or a dummy empty section.
  virtual const DWARFSection &getPubnamesSection() const { return Dummy; }
  /// Return the .debug_pubtypes section, or a dummy empty section.
  ///
  /// \returns The .debug_pubtypes section, or a dummy empty section.
  virtual const DWARFSection &getPubtypesSection() const { return Dummy; }
  /// Return the .debug_gnu_pubnames section, or a dummy empty section.
  ///
  /// \returns The .debug_gnu_pubnames section, or a dummy empty section.
  virtual const DWARFSection &getGnuPubnamesSection() const { return Dummy; }
  /// Return the .debug_gnu_pubtypes section, or a dummy empty section.
  ///
  /// \returns The .debug_gnu_pubtypes section, or a dummy empty section.
  virtual const DWARFSection &getGnuPubtypesSection() const { return Dummy; }
  /// Return the .debug_str_offsets section, or a dummy empty section.
  ///
  /// \returns The .debug_str_offsets section, or a dummy empty section.
  virtual const DWARFSection &getStrOffsetsSection() const { return Dummy; }
  /// Invoke \p F for each .debug_info.dwo section in this object.
  ///
  /// \param F Callback invoked with each .debug_info.dwo section.
  virtual void
  forEachInfoDWOSections(function_ref<void(const DWARFSection &)> F) const {}
  /// Invoke \p F for each .debug_types.dwo section in this object.
  ///
  /// \param F Callback invoked with each .debug_types.dwo section.
  virtual void
  forEachTypesDWOSections(function_ref<void(const DWARFSection &)> F) const {}
  /// Return the .debug_abbrev.dwo section contents, or an empty string.
  ///
  /// \returns Contents of the .debug_abbrev.dwo section, or an empty string.
  virtual StringRef getAbbrevDWOSection() const { return ""; }
  /// Return the .debug_line.dwo section, or a dummy empty section.
  ///
  /// \returns The .debug_line.dwo section, or a dummy empty section.
  virtual const DWARFSection &getLineDWOSection() const { return Dummy; }
  /// Return the .debug_loc.dwo section, or a dummy empty section.
  ///
  /// \returns The .debug_loc.dwo section, or a dummy empty section.
  virtual const DWARFSection &getLocDWOSection() const { return Dummy; }
  /// Return the .debug_loclists.dwo section, or a dummy empty section.
  ///
  /// \returns The .debug_loclists.dwo section, or a dummy empty section.
  virtual const DWARFSection &getLoclistsDWOSection() const { return Dummy; }
  /// Return the contents of the .debug_str.dwo section, or empty if absent.
  ///
  /// \returns Contents of the .debug_str.dwo section, or empty if absent.
  virtual StringRef getStrDWOSection() const { return ""; }
  /// Return the .debug_str_offsets.dwo section, or a dummy empty section.
  ///
  /// \returns The .debug_str_offsets.dwo section, or a dummy empty section.
  virtual const DWARFSection &getStrOffsetsDWOSection() const {
    return Dummy;
  }
  /// Return the .debug_ranges.dwo section, or a dummy empty section.
  ///
  /// \returns The .debug_ranges.dwo section, or a dummy empty section.
  virtual const DWARFSection &getRangesDWOSection() const { return Dummy; }
  /// Return the .debug_rnglists.dwo section, or a dummy empty section.
  ///
  /// \returns The .debug_rnglists.dwo section, or a dummy empty section.
  virtual const DWARFSection &getRnglistsDWOSection() const { return Dummy; }
  /// Return the .debug_addr section, or a dummy empty section.
  ///
  /// \returns The .debug_addr section, or a dummy empty section.
  virtual const DWARFSection &getAddrSection() const { return Dummy; }
  /// Return the .apple_names accelerator section, or a dummy empty section.
  ///
  /// \returns The .apple_names section, or a dummy empty section.
  virtual const DWARFSection &getAppleNamesSection() const { return Dummy; }
  /// Return the .apple_types accelerator section, or a dummy empty section.
  ///
  /// \returns The .apple_types section, or a dummy empty section.
  virtual const DWARFSection &getAppleTypesSection() const { return Dummy; }
  /// Return the .apple_namespaces accelerator section, or a dummy empty section.
  ///
  /// \returns The .apple_namespaces section, or a dummy empty section.
  virtual const DWARFSection &getAppleNamespacesSection() const {
    return Dummy;
  }
  /// Return the .debug_names accelerator section, or a dummy empty section.
  ///
  /// \returns The .debug_names section, or a dummy empty section.
  virtual const DWARFSection &getNamesSection() const { return Dummy; }
  /// Return the .apple_objc accelerator section, or a dummy empty section.
  ///
  /// \returns The .apple_objc section, or a dummy empty section.
  virtual const DWARFSection &getAppleObjCSection() const { return Dummy; }
  /// Return the .debug_cu_index section contents, or an empty string.
  ///
  /// \returns Contents of the .debug_cu_index section, or an empty string.
  virtual StringRef getCUIndexSection() const { return ""; }
  /// Return the .gdb_index section contents, or an empty string.
  ///
  /// \returns Contents of the .gdb_index section, or an empty string.
  virtual StringRef getGdbIndexSection() const { return ""; }
  /// Return the .debug_tu_index section contents, or an empty string.
  ///
  /// \returns Contents of the .debug_tu_index section, or an empty string.
  virtual StringRef getTUIndexSection() const { return ""; }
  /// Look up a relocation applied at offset \p Pos within section \p Sec.
  ///
  /// \param Sec DWARF section in which to look up the relocation.
  /// \param Pos Byte offset within \p Sec of the relocated value.
  /// \returns The relocation entry at \p Pos, or std::nullopt if none.
  virtual std::optional<RelocAddrEntry> find(const DWARFSection &Sec,
                                             uint64_t Pos) const = 0;
};

} // namespace llvm
#endif
