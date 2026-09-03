//===-- MSP430Attributes.h - MSP430 Attributes ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===-----------------------------------------------------------------------===//
///
/// \file
/// This file contains enumerations for MSP430 ELF build attributes as
/// defined in the MSP430 ELF psABI specification.
///
/// MSP430 ELF psABI specification
///
/// https://www.ti.com/lit/pdf/slaa534
///
//===----------------------------------------------------------------------===//
#ifndef LLVM_SUPPORT_MSP430ATTRIBUTES_H
#define LLVM_SUPPORT_MSP430ATTRIBUTES_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/ELFAttributes.h"

namespace llvm {
/// Enumerations and helpers for MSP430 ELF build attributes.
namespace MSP430Attrs {

/// Return the map from MSP430 build-attribute tags to their \c Tag_* names.
/// @return The MSP430 attribute tag-name map.
LLVM_ABI const TagNameMap &getMSP430AttributeTags();

/// Public tags in the MSP430 ELF \c .MSP430.attributes section.
enum AttrType : unsigned {
  TagISA = 4,       ///< Instruction-set architecture (Tag_ISA).
  TagCodeModel = 6, ///< Code model (Tag_Code_Model).
  TagDataModel = 8, ///< Data model (Tag_Data_Model).
  TagEnumSize = 10  ///< Enumeration type size (Tag_Enum_Size).
};

/// Legal Tag_ISA values.
enum ISA {
  ISAMSP430 = 1,  ///< MSP430 (16-bit) instruction set.
  ISAMSP430X = 2  ///< MSP430X (20-bit) instruction set.
};

/// Legal Tag_Code_Model values.
enum CodeModel {
  CMSmall = 1, ///< Small code model (16-bit addresses).
  CMLarge = 2  ///< Large code model (20-bit addresses).
};

/// Legal Tag_Data_Model values.
enum DataModel {
  DMSmall = 1,      ///< Small data model.
  DMLarge = 2,      ///< Large data model.
  DMRestricted = 3  ///< Restricted data model.
};

/// Legal Tag_Enum_Size values.
enum EnumSize {
  ESSmall = 1,    ///< Smallest fitting enum size.
  ESInteger = 2,  ///< Enums are int-sized.
  ESDontCare = 3  ///< Enum size is unspecified.
};

} // namespace MSP430Attrs
} // namespace llvm

#endif
