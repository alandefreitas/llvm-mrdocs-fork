//===-- RISCVAttributes.h - RISCV Attributes --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains enumerations for RISCV attributes as defined in RISC-V
// ELF psABI specification.
//
// RISC-V ELF psABI specification
//
// https://github.com/riscv/riscv-elf-psabi-doc/blob/master/riscv-elf.md
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_SUPPORT_RISCVATTRIBUTES_H
#define LLVM_SUPPORT_RISCVATTRIBUTES_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/ELFAttributes.h"

namespace llvm {
/// Enumerations and helpers for RISC-V ELF build attributes.
namespace RISCVAttrs {

/// Return the map from RISC-V build-attribute tags to their \c Tag_* names.
/// @return The RISC-V attribute tag-name map.
LLVM_ABI const TagNameMap &getRISCVAttributeTags();

/// Public tags in the RISC-V ELF \c .riscv.attributes section.
enum AttrType : unsigned {
  STACK_ALIGN = 4,          ///< Stack alignment in bytes (Tag_RISCV_stack_align).
  ARCH = 5,                 ///< Architecture string (Tag_RISCV_arch).
  UNALIGNED_ACCESS = 6,     ///< Unaligned access policy (Tag_RISCV_unaligned_access).
  PRIV_SPEC = 8,            ///< Privilege-spec major version (Tag_RISCV_priv_spec).
  PRIV_SPEC_MINOR = 10,     ///< Privilege-spec minor version (Tag_RISCV_priv_spec_minor).
  PRIV_SPEC_REVISION = 12,  ///< Privilege-spec revision (Tag_RISCV_priv_spec_revision).
  ATOMIC_ABI = 14,          ///< Atomic ABI version (Tag_RISCV_atomic_abi).
};

/// Legal Tag_RISCV_atomic_abi (tag 14) values.
///
/// Defined at
/// https://github.com/riscv-non-isa/riscv-elf-psabi-doc/blob/master/riscv-elf.adoc#tag_riscv_atomic_abi-14-uleb128version
enum class RISCVAtomicAbiTag : unsigned {
  UNKNOWN = 0, ///< Atomic ABI is unknown or unspecified.
  A6C = 1,     ///< Atomic ABI 6.0 compatible (no nested atomics).
  A6S = 2,     ///< Atomic ABI 6.0 with safe nested atomics.
  A7 = 3,      ///< Atomic ABI 7.0.
};

/// Legal Tag_RISCV_unaligned_access (tag 6) values.
enum {
  NOT_ALLOWED = 0, ///< Unaligned memory accesses are not permitted.
  ALLOWED = 1      ///< Unaligned memory accesses are permitted.
};

} // namespace RISCVAttrs
} // namespace llvm

#endif
