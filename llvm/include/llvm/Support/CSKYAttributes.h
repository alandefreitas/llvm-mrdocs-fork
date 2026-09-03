//===---- CSKYAttributes.h - CSKY Attributes --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains enumerations for CSKY attributes.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_SUPPORT_CSKYATTRIBUTES_H
#define LLVM_SUPPORT_CSKYATTRIBUTES_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/ELFAttributes.h"

namespace llvm {
/// Enumerations and helpers for CSKY ELF build attributes.
namespace CSKYAttrs {

/// Return the map from CSKY build-attribute tags to their \c Tag_* names.
/// @return The CSKY attribute tag-name map.
LLVM_ABI const TagNameMap &getCSKYAttributeTags();

/// Public tags in the CSKY ELF \c .csky.attributes section.
enum AttrType {
  CSKY_ARCH_NAME = 4,         ///< Architecture name string (Tag_CSKY_ARCH_NAME).
  CSKY_CPU_NAME = 5,          ///< CPU name string (Tag_CSKY_CPU_NAME).
  CSKY_ISA_FLAGS = 6,         ///< Base ISA feature bitmask (Tag_CSKY_ISA_FLAGS).
  CSKY_ISA_EXT_FLAGS = 7,     ///< Extended ISA feature bitmask (Tag_CSKY_ISA_EXT_FLAGS).
  CSKY_DSP_VERSION = 8,       ///< DSP ISA version (Tag_CSKY_DSP_VERSION).
  CSKY_VDSP_VERSION = 9,      ///< Vector DSP ISA version (Tag_CSKY_VDSP_VERSION).
  CSKY_FPU_VERSION = 16,      ///< Floating-point unit version (Tag_CSKY_FPU_VERSION).
  CSKY_FPU_ABI = 17,          ///< Floating-point ABI variant (Tag_CSKY_FPU_ABI).
  CSKY_FPU_ROUNDING = 18,     ///< Non-default FP rounding needed (Tag_CSKY_FPU_ROUNDING).
  CSKY_FPU_DENORMAL = 19,     ///< Non-default FP denormal handling needed (Tag_CSKY_FPU_DENORMAL).
  CSKY_FPU_EXCEPTION = 20,    ///< Non-default FP exception behavior needed (Tag_CSKY_FPU_EXCEPTION).
  CSKY_FPU_NUMBER_MODULE = 21, ///< FP number-model string (Tag_CSKY_FPU_NUMBER_MODULE).
  CSKY_FPU_HARDFP = 22        ///< Hard-float precision bitmask (Tag_CSKY_FPU_HARDFP).
};

/// Bit flags for Tag_CSKY_ISA_FLAGS (tag 6).
enum ISA_FLAGS {
  V2_ISA_E1 = 1 << 1,       ///< CSKY V2 ISA E1 feature set.
  V2_ISA_1E2 = 1 << 2,      ///< CSKY V2 ISA 1E2 feature set.
  V2_ISA_2E3 = 1 << 3,      ///< CSKY V2 ISA 2E3 feature set.
  V2_ISA_3E7 = 1 << 4,      ///< CSKY V2 ISA 3E7 feature set.
  V2_ISA_7E10 = 1 << 5,     ///< CSKY V2 ISA 7E10 feature set.
  V2_ISA_3E3R1 = 1 << 6,    ///< CSKY V2 ISA 3E3 revision 1.
  V2_ISA_3E3R2 = 1 << 7,    ///< CSKY V2 ISA 3E3 revision 2.
  V2_ISA_10E60 = 1 << 8,    ///< CSKY V2 ISA 10E60 feature set.
  V2_ISA_3E3R3 = 1 << 9,    ///< CSKY V2 ISA 3E3 revision 3.
  ISA_TRUST = 1 << 11,      ///< TrustZone / secure execution extension.
  ISA_CACHE = 1 << 12,      ///< Cache control instructions.
  ISA_NVIC = 1 << 13,       ///< Nested vectored interrupt controller support.
  ISA_CP = 1 << 14,         ///< Coprocessor instructions.
  ISA_MP = 1 << 15,         ///< Multiprocessor extension.
  ISA_MP_1E2 = 1 << 16,     ///< Multiprocessor 1E2 extension.
  ISA_JAVA = 1 << 17,       ///< Java acceleration extension.
  ISA_MAC = 1 << 18,        ///< Multiply-accumulate extension.
  ISA_MAC_DSP = 1 << 19,    ///< DSP multiply-accumulate extension.
  ISA_DSP = 1 << 20,        ///< DSP extension.
  ISA_DSP_1E2 = 1 << 21,    ///< DSP 1E2 extension.
  ISA_DSP_ENHANCE = 1 << 22, ///< Enhanced DSP extension.
  ISA_DSP_SILAN = 1 << 23,  ///< Silan DSP extension.
  ISA_VDSP = 1 << 24,       ///< Vector DSP extension.
  ISA_VDSP_2 = 1 << 25,     ///< Vector DSP version 2.
  ISA_VDSP_2E3 = 1 << 26,   ///< Vector DSP 2E3 extension.
  V2_ISA_DSPE60 = 1 << 27,  ///< CSKY V2 DSP E60 extension.
  ISA_VDSP_2E60F = 1 << 28  ///< Vector DSP 2E60F extension.
};

/// Bit flags for Tag_CSKY_ISA_EXT_FLAGS (tag 7).
enum ISA_EXT_FLAGS {
  ISA_FLOAT_E1 = 1 << 0,   ///< Floating-point E1 extension.
  ISA_FLOAT_1E2 = 1 << 1,  ///< Floating-point 1E2 extension.
  ISA_FLOAT_1E3 = 1 << 2,  ///< Floating-point 1E3 extension.
  ISA_FLOAT_3E4 = 1 << 3,  ///< Floating-point 3E4 extension.
  ISA_FLOAT_7E60 = 1 << 4  ///< Floating-point 7E60 extension.
};

/// Shared values for Tag_CSKY_FPU_ROUNDING, Tag_CSKY_FPU_DENORMAL, and
/// Tag_CSKY_FPU_EXCEPTION.
enum {
  NONE = 0,   ///< Feature or non-default behavior is not required.
  NEEDED = 1  ///< Feature or non-default behavior is required.
};

/// Legal Tag_CSKY_DSP_VERSION (tag 8) values.
enum DSP_VERSION {
  DSP_VERSION_EXTENSION = 1, ///< DSP as an optional ISA extension.
  DSP_VERSION_2 = 2          ///< DSP version 2.
};

/// Legal Tag_CSKY_VDSP_VERSION (tag 9) values.
enum VDSP_VERSION {
  VDSP_VERSION_1 = 1, ///< Vector DSP version 1.
  VDSP_VERSION_2 = 2  ///< Vector DSP version 2.
};

/// Legal Tag_CSKY_FPU_VERSION (tag 16) values.
enum FPU_VERSION {
  FPU_VERSION_1 = 1, ///< Floating-point unit version 1.
  FPU_VERSION_2 = 2, ///< Floating-point unit version 2.
  FPU_VERSION_3 = 3  ///< Floating-point unit version 3.
};

/// Legal Tag_CSKY_FPU_ABI (tag 17) values.
enum FPU_ABI {
  FPU_ABI_SOFT = 1,   ///< Soft-float ABI (no FPU registers for arguments).
  FPU_ABI_SOFTFP = 2, ///< SoftFP ABI (FPU instructions, soft calling convention).
  FPU_ABI_HARD = 3    ///< Hard-float ABI (FPU registers for arguments).
};

/// Bit flags for Tag_CSKY_FPU_HARDFP (tag 22).
enum FPU_HARDFP {
  FPU_HARDFP_HALF = 1,   ///< Half-precision hard-float support.
  FPU_HARDFP_SINGLE = 2, ///< Single-precision hard-float support.
  FPU_HARDFP_DOUBLE = 4  ///< Double-precision hard-float support.
};

} // namespace CSKYAttrs
} // namespace llvm

#endif
