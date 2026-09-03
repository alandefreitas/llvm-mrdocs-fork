//===- DWARFSection.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFSECTION_H
#define LLVM_DEBUGINFO_DWARF_DWARFSECTION_H

#include "llvm/ADT/StringRef.h"

namespace llvm {

/// Contents and load address of a DWARF debug section.
struct DWARFSection {
  /// Raw bytes of the section.
  StringRef Data;
  /// Section load address, or 0 if not applicable.
  uint64_t Address = 0;
};

/// Object-file section name and whether that name is unique in the object.
struct SectionName {
  /// Section name as reported by the object file.
  StringRef Name;
  bool IsNameUnique; ///< True if this section name is unique in the object.
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFSECTION_H
