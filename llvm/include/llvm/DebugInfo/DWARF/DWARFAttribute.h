//===- DWARFAttribute.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFATTRIBUTE_H
#define LLVM_DEBUGINFO_DWARF_DWARFATTRIBUTE_H

#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DWARF/DWARFFormValue.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>

namespace llvm {

//===----------------------------------------------------------------------===//
/// Encapsulates a DWARF attribute value and all of the data required to
/// describe the attribute value.
///
/// This class is designed to be used by clients that want to iterate across all
/// attributes in a DWARFDie.
struct DWARFAttribute {
  /// The debug info/types offset for this attribute.
  uint64_t Offset = 0;
  /// The debug info/types section byte size of the data for this attribute.
  uint32_t ByteSize = 0;
  /// The attribute enumeration of this attribute.
  dwarf::Attribute Attr = dwarf::Attribute(0);
  /// The form and value for this attribute.
  DWARFFormValue Value;

  /// True if this attribute has a non-zero offset and a set attribute code.
  ///
  /// \returns True if Offset is non-zero and Attr is set.
  bool isValid() const {
    return Offset != 0 && Attr != dwarf::Attribute(0);
  }

  /// True if this attribute is valid (non-zero offset and attribute code).
  ///
  /// \returns True if this attribute is valid.
  explicit operator bool() const {
    return isValid();
  }

  /// Identify DWARF attributes that may contain a pointer to a location list.
  ///
  /// \param Attr the DWARF attribute to check.
  /// \returns True if \p Attr may contain a pointer to a location list.
  LLVM_ABI static bool mayHaveLocationList(dwarf::Attribute Attr);

  /// Identifies DWARF attributes that may contain a reference to a
  /// DWARF expression.
  ///
  /// \param Attr the DWARF attribute to check.
  /// \returns True if \p Attr may contain a reference to a DWARF expression.
  LLVM_ABI static bool mayHaveLocationExpr(dwarf::Attribute Attr);
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFATTRIBUTE_H
