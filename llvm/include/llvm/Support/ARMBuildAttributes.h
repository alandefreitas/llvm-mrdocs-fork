//===-- ARMBuildAttributes.h - ARM Build Attributes -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains enumerations and support routines for ARM build attributes
// as defined in ARM ABI addenda document (ABI release 2.08).
//
// ELF for the ARM Architecture r2.09 - November 30, 2012
//
// http://infocenter.arm.com/help/topic/com.arm.doc.ihi0044e/IHI0044E_aaelf.pdf
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_ARMBUILDATTRIBUTES_H
#define LLVM_SUPPORT_ARMBUILDATTRIBUTES_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/ELFAttributes.h"

namespace llvm {
/// Enumerations and helpers for ARM ELF build attributes.
namespace ARMBuildAttrs {

/// Return the map from ARM build-attribute tags to their \c Tag_* names.
/// @return The ARM attribute tag-name map.
LLVM_ABI const TagNameMap &getARMAttributeTags();

/// Assembler-only attributes that are not stored as a single ELF tag.
///
/// These translate into one or more AttrType entries in the \c .ARM.attributes
/// section.
enum SpecialAttr {
  /// \c .cpu assembler directive; expands to one or more AttrType tags.
  SEL_CPU
};

/// Public tags in the ARM ELF \c .ARM.attributes section.
enum AttrType : unsigned {
  File = 1,                    ///< File-scope subsection (Tag_File).
  CPU_raw_name = 4,            ///< Raw CPU name string (Tag_CPU_raw_name).
  CPU_name = 5,                ///< Official CPU or architecture name (Tag_CPU_name).
  CPU_arch = 6,                ///< Permitted architecture version (Tag_CPU_arch).
  CPU_arch_profile = 7,        ///< Architecture profile (Tag_CPU_arch_profile).
  ARM_ISA_use = 8,             ///< Use of the A32/ARM instruction set (Tag_ARM_ISA_use).
  THUMB_ISA_use = 9,           ///< Use of the T32/Thumb instruction set (Tag_THUMB_ISA_use).
  FP_arch = 10,                ///< Permitted floating-point ISA (Tag_FP_arch).
  WMMX_arch = 11,              ///< Permitted Wireless MMX ISA (Tag_WMMX_arch).
  Advanced_SIMD_arch = 12,     ///< Permitted Advanced SIMD/NEON ISA (Tag_Advanced_SIMD_arch).
  PCS_config = 13,             ///< Procedure-call standard configuration (Tag_PCS_config).
  ABI_PCS_R9_use = 14,         ///< Role of register R9 (Tag_ABI_PCS_R9_use).
  ABI_PCS_RW_data = 15,        ///< How RW static data is addressed (Tag_ABI_PCS_RW_data).
  ABI_PCS_RO_data = 16,        ///< How RO static data is addressed (Tag_ABI_PCS_RO_data).
  ABI_PCS_GOT_use = 17,        ///< How imported data is addressed (Tag_ABI_PCS_GOT_use).
  ABI_PCS_wchar_t = 18,        ///< Size of wchar_t (Tag_ABI_PCS_wchar_t).
  ABI_FP_rounding = 19,        ///< Floating-point rounding mode (Tag_ABI_FP_rounding).
  ABI_FP_denormal = 20,        ///< Floating-point denormal handling (Tag_ABI_FP_denormal).
  ABI_FP_exceptions = 21,      ///< IEEE inexact-exception checking (Tag_ABI_FP_exceptions).
  ABI_FP_user_exceptions = 22, ///< IEEE user-exception support (Tag_ABI_FP_user_exceptions).
  ABI_FP_number_model = 23,    ///< Permitted floating-point number model (Tag_ABI_FP_number_model).
  ABI_align_needed = 24,       ///< Required data alignment (Tag_ABI_align_needed).
  ABI_align_preserved = 25,    ///< Alignment that must be preserved (Tag_ABI_align_preserved).
  ABI_enum_size = 26,          ///< Enum container size (Tag_ABI_enum_size).
  ABI_HardFP_use = 27,         ///< Hard-float unit usage (Tag_ABI_HardFP_use).
  ABI_VFP_args = 28,           ///< How FP arguments are passed (Tag_ABI_VFP_args).
  ABI_WMMX_args = 29,          ///< How WMMX arguments are passed (Tag_ABI_WMMX_args).
  ABI_optimization_goals = 30, ///< Speed/size/debug optimization goals (Tag_ABI_optimization_goals).
  ABI_FP_optimization_goals = 31, ///< FP-specific optimization goals (Tag_ABI_FP_optimization_goals).
  compatibility = 32,          ///< Vendor compatibility claim (Tag_compatibility).
  CPU_unaligned_access = 34,   ///< v6-style unaligned data access (Tag_CPU_unaligned_access).
  FP_HP_extension = 36,        ///< Half-precision FP extension (Tag_FP_HP_extension).
  ABI_FP_16bit_format = 38,    ///< 16-bit floating-point format (Tag_ABI_FP_16bit_format).
  MPextension_use = 42, ///< ARMv7 MP extension; recoded from tag 70 (ABI r2.08).
  DIV_use = 44,                ///< Hardware SDIV/UDIV usage (Tag_DIV_use).
  DSP_extension = 46,          ///< Thumb DSP extension (Tag_DSP_extension).
  MVE_arch = 48,               ///< M-profile Vector Extension (Tag_MVE_arch).
  PAC_extension = 50,          ///< Pointer-authentication instructions (Tag_PAC_extension).
  BTI_extension = 52,          ///< Branch Target Identification (Tag_BTI_extension).
  also_compatible_with = 65,   ///< Secondary compatibility tag (Tag_also_compatible_with).
  conformance = 67,            ///< ABI version claimed (Tag_conformance).
  Virtualization_use = 68,     ///< TrustZone and virtualization (Tag_Virtualization_use).
  BTI_use = 74,                ///< Code compiled with BTI enforcement (Tag_BTI_use).
  PACRET_use = 76,             ///< Return-address signing (Tag_PACRET_use).

  /// Legacy tags from earlier ABI revisions.
  Section = 2,               ///< Section-scope subsection; deprecated in ABI r2.09.
  Symbol = 3,                ///< Symbol-scope subsection; deprecated in ABI r2.09.
  ABI_align8_needed = 24,    ///< Old name for ABI_align_needed (ABI r2.09).
  ABI_align8_preserved = 25, ///< Old name for ABI_align_preserved (ABI r2.09).
  nodefaults = 64,           ///< Inherited tags are undefined; deprecated in ABI r2.09.
  T2EE_use = 66,             ///< Thumb-2EE (ENTERX/LEAVEX); deprecated in ABI r2.09.
  MPextension_use_old = 70   ///< Pre-r2.08 encoding of MPextension_use.
};

/// Legal Tag_CPU_arch (tag 6) values, encoded as ULEB128.
enum CPUArch {
  Pre_v4 = 0,       ///< Architecture older than ARMv4.
  v4 = 1,           ///< ARMv4 (for example SA-110).
  v4T = 2,          ///< ARMv4T (for example ARM7TDMI).
  v5T = 3,          ///< ARMv5T (for example ARM9TDMI).
  v5TE = 4,         ///< ARMv5TE (for example ARM946E-S).
  v5TEJ = 5,        ///< ARMv5TEJ (for example ARM926EJ-S).
  v6 = 6,           ///< ARMv6 (for example ARM1136J-S).
  v6KZ = 7,         ///< ARMv6KZ (for example ARM1176JZ-S).
  v6T2 = 8,         ///< ARMv6T2 (for example ARM1156T2-S).
  v6K = 9,          ///< ARMv6K (for example ARM1176JZ-S).
  v7 = 10,          ///< ARMv7 (for example Cortex-A8, Cortex-M3).
  v6_M = 11,        ///< ARMv6-M (for example Cortex-M1).
  v6S_M = 12,       ///< ARMv6-M with the system extensions.
  v7E_M = 13,       ///< ARMv7-M with DSP extensions.
  v8_A = 14,        ///< ARMv8-A AArch32.
  v8_R = 15,        ///< ARMv8-R (for example Cortex-R52).
  v8_M_Base = 16,   ///< ARMv8-M Baseline AArch32.
  v8_M_Main = 17,   ///< ARMv8-M Mainline AArch32.
  v8_1_M_Main = 21, ///< ARMv8.1-M Mainline AArch32.
  v9_A = 22,        ///< ARMv9-A AArch32.
};

/// Legal Tag_CPU_arch_profile (tag 7) values, encoded as ULEB128.
enum CPUArchProfile {
  Not_Applicable          = 0,      ///< Pre-v7, or cross-profile code.
  ApplicationProfile      = (0x41), ///< 'A' application profile (for example Cortex-A8).
  RealTimeProfile         = (0x52), ///< 'R' real-time profile (for example Cortex-R4).
  MicroControllerProfile  = (0x4D), ///< 'M' microcontroller profile (for example Cortex-M3).
  SystemProfile           = (0x53)  ///< 'S' classic A or R programmer's model.
};

/// Shared legal values for many ARM build-attribute tags.
///
/// Several tags reuse Not_Allowed and Allowed, then add tag-specific encodings.
enum {
  Not_Allowed = 0, ///< Feature or ISA not permitted for this entity.
  Allowed = 1,     ///< Feature or ISA permitted (v1 or the generic "yes").

  /// Tag_THUMB_ISA_use (tag 9) values, encoded as ULEB128.
  AllowThumb32 = 2, ///< 32-bit Thumb (implies 16-bit instructions).
  AllowThumbDerived = 3, ///< Thumb allowed; instruction set follows arch/profile.

  /// Tag_FP_arch (tag 10) values, encoded as ULEB128 (formerly Tag_VFP_arch).
  AllowFPv2  = 2,     ///< v2 FP ISA permitted (implies use of the v1 FP ISA).
  AllowFPv3A = 3,     ///< v3 FP ISA permitted (implies use of the v2 FP ISA).
  AllowFPv3B = 4,     ///< v3 FP ISA permitted, but only D0-D15, S0-S31.
  AllowFPv4A = 5,     ///< v4 FP ISA permitted (implies use of v3 FP ISA).
  AllowFPv4B = 6,     ///< v4 FP ISA was permitted, but only D0-D15, S0-S31.
  AllowFPARMv8A = 7,  ///< Use of the ARM v8-A FP ISA was permitted.
  /// Use of the ARM v8-A FP ISA was permitted, but only D0-D15, S0-S31.
  AllowFPARMv8B = 8,

  /// Tag_WMMX_arch (tag 11) values, encoded as ULEB128.
  AllowWMMXv1 = 1,  ///< The user permitted this entity to use WMMX v1.
  AllowWMMXv2 = 2,  ///< The user permitted this entity to use WMMX v2.

  /// Tag_Advanced_SIMD_arch (tag 12) values, encoded as ULEB128.
  AllowNeon = 1,      ///< SIMDv1 was permitted.
  AllowNeon2 = 2,     ///< SIMDv2 was permitted (half-precision FP, MAC operations).
  AllowNeonARMv8 = 3, ///< ARM v8-A SIMD was permitted.
  AllowNeonARMv8_1a = 4, ///< ARM v8.1-A SIMD was permitted (RDMA).

  /// Tag_MVE_arch (tag 48) values, encoded as ULEB128.
  AllowMVEInteger = 1, ///< Integer-only MVE was permitted.
  AllowMVEIntegerAndFloat = 2, ///< Integer and floating-point MVE were permitted.

  /// Tag_ABI_PCS_R9_use (tag 14) values, encoded as ULEB128.
  R9IsGPR = 0,        ///< R9 used as v6 (just another callee-saved register).
  R9IsSB = 1,         ///< R9 used as a global static base register.
  R9IsTLSPointer = 2, ///< R9 used as a thread-local storage pointer.
  R9Reserved = 3,     ///< R9 not used by code associated with the attributed entity.

  /// Tag_ABI_PCS_RW_data (tag 15) values, encoded as ULEB128.
  AddressRWPCRel = 1, ///< Address RW static data PC-relative.
  AddressRWSBRel = 2, ///< Address RW static data SB-relative.
  AddressRWNone = 3, ///< No RW static data permitted.

  /// Tag_ABI_PCS_RO_data (tag 16) values, encoded as ULEB128.
  AddressROPCRel = 1, ///< Address RO static data PC-relative.
  AddressRONone = 2, ///< No RO static data permitted.

  /// Tag_ABI_PCS_GOT_use (tag 17) values, encoded as ULEB128.
  AddressDirect = 1, ///< Address imported data directly.
  AddressGOT = 2, ///< Address imported data indirectly (via GOT).

  /// Tag_ABI_PCS_wchar_t (tag 18) values, encoded as ULEB128.
  WCharProhibited = 0,  ///< wchar_t is not used.
  WCharWidth2Bytes = 2, ///< sizeof(wchar_t) == 2.
  WCharWidth4Bytes = 4, ///< sizeof(wchar_t) == 4.

  /// Tag_ABI_align_needed (tag 24) values, encoded as ULEB128.
  Align8Byte = 1,     ///< Code may depend on 8-byte alignment of 8-byte data.
  Align4Byte = 2,     ///< Code may depend on 4-byte alignment of 8-byte data.
  AlignReserved = 3,  ///< Reserved alignment encoding.

  /// Tag_ABI_align_preserved (tag 25) values, encoded as ULEB128.
  AlignNotPreserved = 0,  ///< 8-byte alignment need not be preserved.
  AlignPreserve8Byte = 1, ///< Preserve 8-byte alignment of 8-byte data objects.
  AlignPreserveAll = 2,   ///< Preserve 8-byte data alignment and SP mod 8 == 0.

  /// Tag_ABI_FP_denormal (tag 20) values, encoded as ULEB128.
  PositiveZero = 0,   ///< Denormals may be flushed to positive zero.
  IEEEDenormals = 1,  ///< IEEE 754 denormal numbers are required.
  PreserveFPSign = 2, ///< Sign when flushed-to-zero is preserved.

  /// Tag_ABI_FP_number_model (tag 23) values, encoded as ULEB128.
  AllowIEEENormal = 1, ///< IEEE 754 format normal numbers only.
  AllowRTABI = 2,  ///< Numbers, infinities, and one quiet NaN (see [RTABI]).
  AllowIEEE754 = 3, ///< This code may use all the IEEE 754-defined FP encodings.

  /// Tag_ABI_enum_size (tag 26) values, encoded as ULEB128.
  /// The user prohibited the use of enums when building this entity.
  EnumProhibited = 0,
  /// Enum is smallest container big enough to hold all values.
  EnumSmallest = 1,
  Enum32Bit = 2,      ///< Enum is at least 32 bits.
  /// Every enumeration visible across an ABI-complying interface contains a
  /// value needing 32 bits to encode it; other enums can be containerized.
  Enum32BitABI = 3,

  /// Tag_ABI_HardFP_use (tag 27) values, encoded as ULEB128.
  HardFPImplied = 0,          ///< FP use should be implied by Tag_FP_arch.
  HardFPSinglePrecision = 1,  ///< Single-precision only.

  /// Tag_ABI_VFP_args (tag 28) values, encoded as ULEB128.
  BaseAAPCS = 0,        ///< FP arguments follow the AAPCS base variant.
  HardFPAAPCS = 1,      ///< FP arguments follow the AAPCS VFP variant.
  ToolChainFPPCS = 2,   ///< FP arguments follow toolchain-specific conventions.
  CompatibleFPAAPCS = 3, ///< Compatible with both base and VFP AAPCS variants.

  /// Tag_FP_HP_extension (tag 36) values, encoded as ULEB128.
  AllowHPFP = 1, ///< Allow use of half-precision FP.

  /// Tag_ABI_FP_16bit_format (tag 38) values, encoded as ULEB128.
  FP16FormatIEEE = 1, ///< IEEE 754 binary16 format was permitted.
  FP16VFP3 = 2,       ///< VFPv3/Advanced SIMD alternative FP16 format.

  /// Tag_MPextension_use (tag 42) values, encoded as ULEB128.
  AllowMP = 1, ///< Allow use of MP extensions.

  /// Tag_DIV_use (tag 44) values, encoded as ULEB128.
  ///
  /// AllowDIVExt must be emitted if and only if the permission to use hardware
  /// divide cannot be conveyed using AllowDIVIfExists or DisallowDIV.
  AllowDIVIfExists = 0, ///< Allow hardware divide if available in arch, or no
                        ///< info exists.
  DisallowDIV = 1,      ///< Hardware divide explicitly disallowed.
  AllowDIVExt = 2,      ///< Allow hardware divide as optional architecture
                        ///< extension above the base arch specified by
                        ///< Tag_CPU_arch and Tag_CPU_arch_profile.

  /// Tag_Virtualization_use (tag 68) values, encoded as ULEB128.
  AllowTZ = 1,              ///< TrustZone (SMC) was permitted.
  AllowVirtualization = 2,  ///< Virtualization extensions (HVC, ERET) were permitted.
  AllowTZVirtualization = 3, ///< TrustZone and virtualization were permitted.

  /// Tag_PAC_extension (tag 50) values, encoded as ULEB128.
  DisallowPAC = 0,        ///< PAC/AUT instructions were not permitted.
  AllowPACInNOPSpace = 1, ///< PAC/AUT instructions permitted in the NOP space.
  AllowPAC = 2,           ///< PAC/AUT instructions permitted in NOP and non-NOP space.

  /// Tag_BTI_extension (tag 52) values, encoded as ULEB128.
  DisallowBTI = 0,        ///< BTI instructions were not permitted.
  AllowBTIInNOPSpace = 1, ///< BTI instructions permitted in the NOP space.
  AllowBTI = 2,           ///< BTI instructions permitted in NOP and non-NOP space.

  /// Tag_BTI_use (tag 74) values, encoded as ULEB128.
  BTINotUsed = 0, ///< Code is compiled without branch-target enforcement.
  BTIUsed = 1,    ///< Code is compiled with branch-target enforcement.

  /// Tag_PACRET_use (tag 76) values, encoded as ULEB128.
  PACRETNotUsed = 0, ///< Code is compiled without return-address signing.
  PACRETUsed = 1     ///< Code is compiled with return-address signing.
};

} // namespace ARMBuildAttrs
} // namespace llvm

#endif
