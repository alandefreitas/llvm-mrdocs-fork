//===-- HexagonAttributes.h - Qualcomm Hexagon Attributes -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_HEXAGONATTRIBUTES_H
#define LLVM_SUPPORT_HEXAGONATTRIBUTES_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/ELFAttributes.h"

namespace llvm {
/// Enumerations and helpers for Qualcomm Hexagon ELF build attributes.
namespace HexagonAttrs {

/// Return the map from Hexagon build-attribute tags to their \c Tag_* names.
/// @return The Hexagon attribute tag-name map.
LLVM_ABI const TagNameMap &getHexagonAttributeTags();

/// Public tags in the Hexagon ELF \c .hexagon.attributes section.
enum AttrType : unsigned {
  ARCH = 4,      ///< Architecture version (Tag_arch).
  HVXARCH = 5,   ///< HVX architecture version (Tag_hvx_arch).
  HVXIEEEFP = 6, ///< HVX IEEE floating-point support (Tag_hvx_ieeefp).
  HVXQFLOAT = 7, ///< HVX QFloat support (Tag_hvx_qfloat).
  ZREG = 8,      ///< Z-register support (Tag_zreg).
  AUDIO = 9,     ///< Audio extensions (Tag_audio).
  CABAC = 10     ///< CABAC codec support (Tag_cabac).
};

} // namespace HexagonAttrs
} // namespace llvm

#endif
