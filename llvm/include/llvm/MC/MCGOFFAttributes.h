//===- MCGOFFAttributes.h - Attributes of GOFF symbols --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines the various attribute collections defining GOFF symbols.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCGOFFATTRIBUTES_H
#define LLVM_MC_MCGOFFATTRIBUTES_H

#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/GOFF.h"
#include <cstdint>

namespace llvm {
namespace GOFF {
// An "External Symbol Definition" in the GOFF file has a type, and depending on
// the type a different subset of the fields is used.
//
// Unlike other formats, a 2 dimensional structure is used to define the
// location of data. For example, the equivalent of the ELF .text section is
// made up of a Section Definition (SD) and a class (Element Definition; ED).
// The name of the SD symbol depends on the application, while the class has the
// predefined name C_CODE/C_CODE64 in AMODE31 and AMODE64 respectively.
//
// Data can be placed into this structure in 2 ways. First, the data (in a text
// record) can be associated with an ED symbol. To refer to data, a Label
// Definition (LD) is used to give an offset into the data a name. When binding,
// the whole data is pulled into the resulting executable, and the addresses
// given by the LD symbols are resolved.
//
// The alternative is to use a Part Definition (PR). In this case, the data (in
// a text record) is associated with the part. When binding, only the data of
// referenced PRs is pulled into the resulting binary.
//
// Both approaches are used. SD, ED, and PR elements are modelled by nested
// MCSectionGOFF instances, while LD elements are associated with MCSymbolGOFF
// instances.

/// Attributes for a GOFF section-definition (SD) symbol.
struct SDAttr {
  /// Tasking / reusability behavior of the section.
  GOFF::ESDTaskingBehavior TaskingBehavior = GOFF::ESD_TA_Unspecified;
  /// Binding scope of the section-definition symbol.
  GOFF::ESDBindingScope BindingScope = GOFF::ESD_BSC_Unspecified;
};

/// Attributes for a GOFF element-definition (ED) symbol.
struct EDAttr {
  /// True if the element is read-only.
  bool IsReadOnly = false;
  /// Residence mode (Rmode) of the element.
  GOFF::ESDRmode Rmode;
  /// Name-space identifier for the element name.
  GOFF::ESDNameSpaceId NameSpace = GOFF::ESD_NS_NormalName;
  /// Text style of the element's contents.
  GOFF::ESDTextStyle TextStyle = GOFF::ESD_TS_ByteOriented;
  /// Binding algorithm used when combining contributing elements.
  GOFF::ESDBindingAlgorithm BindAlgorithm = GOFF::ESD_BA_Concatenate;
  /// Loading behavior requested for the element.
  GOFF::ESDLoadingBehavior LoadBehavior = GOFF::ESD_LB_Initial;
  /// Number of reserved doublewords associated with the element.
  GOFF::ESDReserveQwords ReservedQwords = GOFF::ESD_RQ_0;
  /// Fill-byte value used to pad unused space in the element.
  uint8_t FillByteValue = 0;
};

/// Attributes for a GOFF label-definition (LD) symbol.
struct LDAttr {
  /// True if the label name may be renamed by the binder.
  bool IsRenamable = false;
  /// Whether the label describes code or data.
  GOFF::ESDExecutable Executable = GOFF::ESD_EXE_Unspecified;
  /// Binding strength of the label.
  GOFF::ESDBindingStrength BindingStrength = GOFF::ESD_BST_Strong;
  /// Linkage convention associated with the label.
  GOFF::ESDLinkageType Linkage = GOFF::ESD_LT_XPLink;
  /// Addressing mode (Amode) of the label.
  GOFF::ESDAmode Amode;
  /// Binding scope of the label.
  GOFF::ESDBindingScope BindingScope = GOFF::ESD_BSC_Unspecified;
};

/// Attributes for a GOFF part-reference (PR) symbol.
struct PRAttr {
  /// True if the part name may be renamed by the binder.
  bool IsRenamable = false;
  /// Whether the part describes code or data.
  GOFF::ESDExecutable Executable = GOFF::ESD_EXE_Unspecified;
  /// Linkage convention associated with the part.
  GOFF::ESDLinkageType Linkage = GOFF::ESD_LT_XPLink;
  /// Binding scope of the part.
  GOFF::ESDBindingScope BindingScope = GOFF::ESD_BSC_Unspecified;
  /// Sort key used to order the part among siblings.
  uint32_t SortKey = 0;
};

/// Attributes for a GOFF external-reference (ER) symbol.
struct ERAttr {
  /// True if the external name is referenced indirectly.
  bool IsIndirectReference = false;
  /// Whether the external reference describes code or data.
  GOFF::ESDExecutable Executable = GOFF::ESD_EXE_Unspecified;
  /// Binding strength of the external reference.
  GOFF::ESDBindingStrength BindingStrength = GOFF::ESD_BST_Strong;
  /// Linkage convention associated with the external reference.
  GOFF::ESDLinkageType Linkage = GOFF::ESD_LT_XPLink;
  /// Addressing mode (Amode) of the external reference.
  GOFF::ESDAmode Amode;
  /// Binding scope of the external reference.
  GOFF::ESDBindingScope BindingScope = GOFF::ESD_BSC_Unspecified;
};

/// Predefined GOFF class name for 64-bit executable code (`C_CODE64`).
constexpr StringLiteral CLASS_CODE = "C_CODE64";
/// Predefined GOFF class name for the 64-bit writable static area (`C_WSA64`).
constexpr StringLiteral CLASS_WSA = "C_WSA64";
/// Predefined GOFF class name for 64-bit data (`C_DATA64`).
constexpr StringLiteral CLASS_DATA = "C_DATA64";
/// Predefined GOFF class name for the PPA2 list (`C_@@QPPA2`).
constexpr StringLiteral CLASS_PPA2 = "C_@@QPPA2";
/// Predefined GOFF class name for static initialization (`C_@@SQINIT`).
constexpr StringLiteral CLASS_SINIT = "C_@@SQINIT";

} // namespace GOFF
} // namespace llvm

#endif
