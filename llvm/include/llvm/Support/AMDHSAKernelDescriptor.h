//===--- AMDHSAKernelDescriptor.h -----------------------------*- C++ -*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// AMDHSA kernel descriptor definitions. For more information, visit
/// https://llvm.org/docs/AMDGPUUsage.html#kernel-descriptor
///
/// \warning
/// Any changes to this file should also be audited for corresponding changes
/// needed in both the assembler and disassembler, namely:
/// * AMDGPUAsmPrinter.{cpp,h}
/// * AMDGPUTargetStreamer.{cpp,h}
/// * AMDGPUDisassembler.{cpp,h}
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_AMDHSAKERNELDESCRIPTOR_H
#define LLVM_SUPPORT_AMDHSAKERNELDESCRIPTOR_H

#include <cstddef>
#include <cstdint>

// Creates enumeration entries used for packing bits into integers. Enumeration
// entries include bit shift amount, bit width, and bit mask.
#ifndef AMDHSA_BITS_ENUM_ENTRY
#define AMDHSA_BITS_ENUM_ENTRY(NAME, SHIFT, WIDTH) \
  NAME ## _SHIFT = (SHIFT),                        \
  NAME ## _WIDTH = (WIDTH),                        \
  NAME = (((1 << (WIDTH)) - 1) << (SHIFT))
#endif // AMDHSA_BITS_ENUM_ENTRY

// Gets bits for specified bit mask from specified source.
#ifndef AMDHSA_BITS_GET
#define AMDHSA_BITS_GET(SRC, MSK) ((SRC & MSK) >> MSK ## _SHIFT)
#endif // AMDHSA_BITS_GET

// Sets bits for specified bit mask in specified destination.
#ifndef AMDHSA_BITS_SET
#define AMDHSA_BITS_SET(DST, MSK, VAL)                                         \
  do {                                                                         \
    auto local = VAL;                                                          \
    DST &= ~MSK;                                                               \
    DST |= ((local << MSK##_SHIFT) & MSK);                                     \
  } while (0)
#endif // AMDHSA_BITS_SET

namespace llvm {
/// AMDHSA kernel descriptor layout, bitfield packs, and related constants.
///
/// Definitions match the hardware and must stay aligned with the assembler and
/// disassembler. See https://llvm.org/docs/AMDGPUUsage.html#kernel-descriptor.
namespace amdhsa {

/// Floating-point rounding modes matching the hardware definition.
enum : uint8_t {
  FLOAT_ROUND_MODE_NEAR_EVEN = 0,     ///< Round ties to even.
  FLOAT_ROUND_MODE_PLUS_INFINITY = 1, ///< Round toward +infinity.
  FLOAT_ROUND_MODE_MINUS_INFINITY = 2, ///< Round toward -infinity.
  FLOAT_ROUND_MODE_ZERO = 3,          ///< Round toward zero.
};

/// Floating-point denormal modes matching the hardware definition.
enum : uint8_t {
  FLOAT_DENORM_MODE_FLUSH_SRC_DST = 0, ///< Flush source and destination denorms.
  FLOAT_DENORM_MODE_FLUSH_DST = 1,     ///< Flush destination denorms only.
  FLOAT_DENORM_MODE_FLUSH_SRC = 2,     ///< Flush source denorms only.
  FLOAT_DENORM_MODE_FLUSH_NONE = 3,    ///< Do not flush denorms.
};

/// System VGPR work-item ID encodings matching the hardware definition.
enum : uint8_t {
  SYSTEM_VGPR_WORKITEM_ID_X = 0,       ///< Initialize work-item ID X only.
  SYSTEM_VGPR_WORKITEM_ID_X_Y = 1,     ///< Initialize work-item IDs X and Y.
  SYSTEM_VGPR_WORKITEM_ID_X_Y_Z = 2,   ///< Initialize work-item IDs X, Y, and Z.
  SYSTEM_VGPR_WORKITEM_ID_UNDEFINED = 3, ///< Undefined work-item ID encoding.
};

// Compute program resource register 1. Must match hardware definition.
// GFX6+.
#define COMPUTE_PGM_RSRC1(NAME, SHIFT, WIDTH)                                  \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC1_##NAME, SHIFT, WIDTH)
// [GFX6-GFX8].
#define COMPUTE_PGM_RSRC1_GFX6_GFX8(NAME, SHIFT, WIDTH)                        \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC1_GFX6_GFX8_##NAME, SHIFT, WIDTH)
// [GFX6-GFX9].
#define COMPUTE_PGM_RSRC1_GFX6_GFX9(NAME, SHIFT, WIDTH)                        \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC1_GFX6_GFX9_##NAME, SHIFT, WIDTH)
// [GFX6-GFX11].
#define COMPUTE_PGM_RSRC1_GFX6_GFX11(NAME, SHIFT, WIDTH)                       \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC1_GFX6_GFX11_##NAME, SHIFT, WIDTH)
// [GFX6-GFX120].
#define COMPUTE_PGM_RSRC1_GFX6_GFX120(NAME, SHIFT, WIDTH)                      \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC1_GFX6_GFX120_##NAME, SHIFT, WIDTH)
// GFX9+.
#define COMPUTE_PGM_RSRC1_GFX9_PLUS(NAME, SHIFT, WIDTH)                        \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC1_GFX9_PLUS_##NAME, SHIFT, WIDTH)
// GFX10+.
#define COMPUTE_PGM_RSRC1_GFX10_PLUS(NAME, SHIFT, WIDTH)                       \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC1_GFX10_PLUS_##NAME, SHIFT, WIDTH)
// GFX12+.
#define COMPUTE_PGM_RSRC1_GFX12_PLUS(NAME, SHIFT, WIDTH)                       \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC1_GFX12_PLUS_##NAME, SHIFT, WIDTH)
// [GFX125].
#define COMPUTE_PGM_RSRC1_GFX125(NAME, SHIFT, WIDTH)                           \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC1_GFX125_##NAME, SHIFT, WIDTH)
/// COMPUTE_PGM_RSRC1 bitfield packs matching the hardware (GFX6+).
///
/// Each macro expands to \c NAME_SHIFT, \c NAME_WIDTH, and \c NAME (bit mask).
enum : int32_t {
  COMPUTE_PGM_RSRC1(GRANULATED_WORKITEM_VGPR_COUNT, 0, 6), ///< Granulated VGPR count per work-item.
  COMPUTE_PGM_RSRC1(GRANULATED_WAVEFRONT_SGPR_COUNT, 6, 4), ///< Granulated SGPR count per wavefront.
  COMPUTE_PGM_RSRC1(PRIORITY, 10, 2), ///< Wavefront priority.
  COMPUTE_PGM_RSRC1(FLOAT_ROUND_MODE_32, 12, 2), ///< F32 floating-point rounding mode.
  COMPUTE_PGM_RSRC1(FLOAT_ROUND_MODE_16_64, 14, 2), ///< F16/F64 floating-point rounding mode.
  COMPUTE_PGM_RSRC1(FLOAT_DENORM_MODE_32, 16, 2), ///< F32 floating-point denormal mode.
  COMPUTE_PGM_RSRC1(FLOAT_DENORM_MODE_16_64, 18, 2), ///< F16/F64 floating-point denormal mode.
  COMPUTE_PGM_RSRC1(PRIV, 20, 1), ///< Privileged mode.
  COMPUTE_PGM_RSRC1_GFX6_GFX11(ENABLE_DX10_CLAMP, 21, 1), ///< Enable DX10 clamp (GFX6-GFX11).
  COMPUTE_PGM_RSRC1_GFX12_PLUS(ENABLE_WG_RR_EN, 21, 1), ///< Enable workgroup round-robin (GFX12+).
  COMPUTE_PGM_RSRC1(DEBUG_MODE, 22, 1), ///< Debug mode.
  COMPUTE_PGM_RSRC1_GFX6_GFX11(ENABLE_IEEE_MODE, 23, 1), ///< Enable IEEE floating-point mode (GFX6-GFX11).
  COMPUTE_PGM_RSRC1_GFX12_PLUS(DISABLE_PERF, 23, 1), ///< Disable performance counters (GFX12+).
  COMPUTE_PGM_RSRC1(BULKY, 24, 1), ///< Bulky shader.
  COMPUTE_PGM_RSRC1(CDBG_USER, 25, 1), ///< Compute debug user.
  COMPUTE_PGM_RSRC1_GFX6_GFX8(RESERVED0, 26, 1), ///< Reserved (GFX6-GFX8).
  COMPUTE_PGM_RSRC1_GFX9_PLUS(FP16_OVFL, 26, 1), ///< FP16 overflow mode (GFX9+).
  COMPUTE_PGM_RSRC1_GFX6_GFX120(RESERVED1, 27, 1), ///< Reserved (GFX6-GFX120).
  COMPUTE_PGM_RSRC1_GFX125(FLAT_SCRATCH_IS_NV, 27, 1), ///< Flat scratch is NV (GFX125).
  COMPUTE_PGM_RSRC1(RESERVED2, 28, 1), ///< Reserved.
  COMPUTE_PGM_RSRC1_GFX6_GFX9(RESERVED3, 29, 3), ///< Reserved (GFX6-GFX9).
  COMPUTE_PGM_RSRC1_GFX10_PLUS(WGP_MODE, 29, 1), ///< Workgroup processor mode (GFX10+).
  COMPUTE_PGM_RSRC1_GFX10_PLUS(MEM_ORDERED, 30, 1), ///< Memory ordered mode (GFX10+).
  COMPUTE_PGM_RSRC1_GFX10_PLUS(FWD_PROGRESS, 31, 1), ///< Forward progress (GFX10+).
};
#undef COMPUTE_PGM_RSRC1

// Compute program resource register 2. Must match hardware definition.
// GFX6+.
#define COMPUTE_PGM_RSRC2(NAME, SHIFT, WIDTH)                                  \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC2_##NAME, SHIFT, WIDTH)
// [GFX6-GFX11].
#define COMPUTE_PGM_RSRC2_GFX6_GFX11(NAME, SHIFT, WIDTH)                       \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC2_GFX6_GFX11_##NAME, SHIFT, WIDTH)
// [GFX6-GFX120].
#define COMPUTE_PGM_RSRC2_GFX6_GFX120(NAME, SHIFT, WIDTH)                      \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC2_GFX6_GFX120_##NAME, SHIFT, WIDTH)
// GFX12+.
#define COMPUTE_PGM_RSRC2_GFX12_PLUS(NAME, SHIFT, WIDTH)                       \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC2_GFX12_PLUS_##NAME, SHIFT, WIDTH)
// [GFX120].
#define COMPUTE_PGM_RSRC2_GFX120(NAME, SHIFT, WIDTH)                           \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC2_GFX120_##NAME, SHIFT, WIDTH)
// [GFX125].
#define COMPUTE_PGM_RSRC2_GFX125(NAME, SHIFT, WIDTH)                           \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC2_GFX125_##NAME, SHIFT, WIDTH)
/// COMPUTE_PGM_RSRC2 bitfield packs matching the hardware (GFX6+).
///
/// Each macro expands to \c NAME_SHIFT, \c NAME_WIDTH, and \c NAME (bit mask).
enum : int32_t {
  COMPUTE_PGM_RSRC2(ENABLE_PRIVATE_SEGMENT, 0, 1), ///< Enable private segment.
  COMPUTE_PGM_RSRC2_GFX6_GFX120(USER_SGPR_COUNT, 1, 5), ///< User SGPR count (GFX6-GFX120).
  COMPUTE_PGM_RSRC2_GFX6_GFX11(ENABLE_TRAP_HANDLER, 6, 1), ///< Enable trap handler (GFX6-GFX11).
  COMPUTE_PGM_RSRC2_GFX120(ENABLE_DYNAMIC_VGPR, 6, 1), ///< Enable dynamic VGPR (GFX120).
  COMPUTE_PGM_RSRC2_GFX125(USER_SGPR_COUNT, 1, 6), ///< User SGPR count (GFX125).
  COMPUTE_PGM_RSRC2(ENABLE_SGPR_WORKGROUP_ID_X, 7, 1), ///< Enable SGPR work-group ID X.
  COMPUTE_PGM_RSRC2(ENABLE_SGPR_WORKGROUP_ID_Y, 8, 1), ///< Enable SGPR work-group ID Y.
  COMPUTE_PGM_RSRC2(ENABLE_SGPR_WORKGROUP_ID_Z, 9, 1), ///< Enable SGPR work-group ID Z.
  COMPUTE_PGM_RSRC2(ENABLE_SGPR_WORKGROUP_INFO, 10, 1), ///< Enable SGPR work-group info.
  COMPUTE_PGM_RSRC2(ENABLE_VGPR_WORKITEM_ID, 11, 2), ///< System VGPR work-item ID encoding.
  COMPUTE_PGM_RSRC2(ENABLE_EXCEPTION_ADDRESS_WATCH, 13, 1), ///< Enable address watch exception.
  COMPUTE_PGM_RSRC2(ENABLE_EXCEPTION_MEMORY, 14, 1), ///< Enable memory violation exception.
  COMPUTE_PGM_RSRC2(GRANULATED_LDS_SIZE, 15, 9), ///< Granulated LDS size.
  COMPUTE_PGM_RSRC2(ENABLE_EXCEPTION_IEEE_754_FP_INVALID_OPERATION, 24, 1), ///< Enable IEEE-754 invalid operation exception.
  COMPUTE_PGM_RSRC2(ENABLE_EXCEPTION_FP_DENORMAL_SOURCE, 25, 1), ///< Enable FP denormal source exception.
  COMPUTE_PGM_RSRC2(ENABLE_EXCEPTION_IEEE_754_FP_DIVISION_BY_ZERO, 26, 1), ///< Enable IEEE-754 division-by-zero exception.
  COMPUTE_PGM_RSRC2(ENABLE_EXCEPTION_IEEE_754_FP_OVERFLOW, 27, 1), ///< Enable IEEE-754 FP overflow exception.
  COMPUTE_PGM_RSRC2(ENABLE_EXCEPTION_IEEE_754_FP_UNDERFLOW, 28, 1), ///< Enable IEEE-754 FP underflow exception.
  COMPUTE_PGM_RSRC2(ENABLE_EXCEPTION_IEEE_754_FP_INEXACT, 29, 1), ///< Enable IEEE-754 FP inexact exception.
  COMPUTE_PGM_RSRC2(ENABLE_EXCEPTION_INT_DIVIDE_BY_ZERO, 30, 1), ///< Enable integer divide-by-zero exception.
  COMPUTE_PGM_RSRC2(RESERVED0, 31, 1), ///< Reserved.
};
#undef COMPUTE_PGM_RSRC2

// Compute program resource register 3 for GFX90A+. Must match hardware
// definition.
#define COMPUTE_PGM_RSRC3_GFX90A(NAME, SHIFT, WIDTH) \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC3_GFX90A_ ## NAME, SHIFT, WIDTH)
/// COMPUTE_PGM_RSRC3 bitfield packs for GFX90A+ matching the hardware.
///
/// Each macro expands to \c NAME_SHIFT, \c NAME_WIDTH, and \c NAME (bit mask).
enum : int32_t {
  COMPUTE_PGM_RSRC3_GFX90A(ACCUM_OFFSET, 0, 6), ///< Accumulator VGPR offset.
  COMPUTE_PGM_RSRC3_GFX90A(RESERVED0, 6, 10), ///< Reserved.
  COMPUTE_PGM_RSRC3_GFX90A(TG_SPLIT, 16, 1), ///< Threadgroup split.
  COMPUTE_PGM_RSRC3_GFX90A(RESERVED1, 17, 15), ///< Reserved.
};
#undef COMPUTE_PGM_RSRC3_GFX90A

// Compute program resource register 3 for GFX10+. Must match hardware
// definition.
// GFX10+.
#define COMPUTE_PGM_RSRC3_GFX10_PLUS(NAME, SHIFT, WIDTH)                       \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC3_GFX10_PLUS_##NAME, SHIFT, WIDTH)
// [GFX10].
#define COMPUTE_PGM_RSRC3_GFX10(NAME, SHIFT, WIDTH)                            \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC3_GFX10_##NAME, SHIFT, WIDTH)
// [GFX10-GFX11].
#define COMPUTE_PGM_RSRC3_GFX10_GFX11(NAME, SHIFT, WIDTH)                      \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC3_GFX10_GFX11_##NAME, SHIFT, WIDTH)
// [GFX10-GFX120].
#define COMPUTE_PGM_RSRC3_GFX10_GFX120(NAME, SHIFT, WIDTH)                     \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC3_GFX10_GFX120_##NAME, SHIFT, WIDTH)
// GFX11+.
#define COMPUTE_PGM_RSRC3_GFX11_PLUS(NAME, SHIFT, WIDTH)                       \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC3_GFX11_PLUS_##NAME, SHIFT, WIDTH)
// [GFX11].
#define COMPUTE_PGM_RSRC3_GFX11(NAME, SHIFT, WIDTH)                            \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC3_GFX11_##NAME, SHIFT, WIDTH)
// GFX12+.
#define COMPUTE_PGM_RSRC3_GFX12_PLUS(NAME, SHIFT, WIDTH)                       \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC3_GFX12_PLUS_##NAME, SHIFT, WIDTH)
// [GFX125].
#define COMPUTE_PGM_RSRC3_GFX125(NAME, SHIFT, WIDTH)                           \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC3_GFX125_##NAME, SHIFT, WIDTH)
/// COMPUTE_PGM_RSRC3 bitfield packs for GFX10+ matching the hardware.
///
/// Each macro expands to \c NAME_SHIFT, \c NAME_WIDTH, and \c NAME (bit mask).
enum : int32_t {
  COMPUTE_PGM_RSRC3_GFX10_GFX11(SHARED_VGPR_COUNT, 0, 4), ///< Shared VGPR count (GFX10-GFX11).
  COMPUTE_PGM_RSRC3_GFX12_PLUS(RESERVED0, 0, 4), ///< Reserved (GFX12+).
  COMPUTE_PGM_RSRC3_GFX10(RESERVED1, 4, 8), ///< Reserved (GFX10).
  COMPUTE_PGM_RSRC3_GFX11(INST_PREF_SIZE, 4, 6), ///< Instruction prefetch size (GFX11).
  COMPUTE_PGM_RSRC3_GFX11(TRAP_ON_START, 10, 1), ///< Trap on start (GFX11).
  COMPUTE_PGM_RSRC3_GFX11(TRAP_ON_END, 11, 1), ///< Trap on end (GFX11).
  COMPUTE_PGM_RSRC3_GFX12_PLUS(INST_PREF_SIZE, 4, 8), ///< Instruction prefetch size (GFX12+).
  COMPUTE_PGM_RSRC3_GFX10_PLUS(RESERVED2, 12, 1), ///< Reserved (GFX10+).
  COMPUTE_PGM_RSRC3_GFX10_GFX11(RESERVED3, 13, 1), ///< Reserved (GFX10-GFX11).
  COMPUTE_PGM_RSRC3_GFX12_PLUS(GLG_EN, 13, 1), ///< GLG enable (GFX12+).
  COMPUTE_PGM_RSRC3_GFX10_GFX120(RESERVED4, 14, 8), ///< Reserved (GFX10-GFX120).
  COMPUTE_PGM_RSRC3_GFX125(NAMED_BAR_CNT, 14, 3), ///< Named barrier count (GFX125).
  COMPUTE_PGM_RSRC3_GFX125(ENABLE_DYNAMIC_VGPR, 17, 1), ///< Enable dynamic VGPR (GFX125).
  COMPUTE_PGM_RSRC3_GFX125(TCP_SPLIT, 18, 3), ///< TCP split (GFX125).
  COMPUTE_PGM_RSRC3_GFX125(ENABLE_DIDT_THROTTLE, 21, 1), ///< Enable DIDT throttle (GFX125).
  COMPUTE_PGM_RSRC3_GFX10_PLUS(RESERVED5, 22, 9), ///< Reserved (GFX10+).
  COMPUTE_PGM_RSRC3_GFX10(RESERVED6, 31, 1), ///< Reserved (GFX10).
  COMPUTE_PGM_RSRC3_GFX11_PLUS(IMAGE_OP, 31, 1), ///< Image operation (GFX11+).
};
#undef COMPUTE_PGM_RSRC3_GFX10_PLUS

// Kernel code properties. Must be kept backwards compatible.
#define KERNEL_CODE_PROPERTY(NAME, SHIFT, WIDTH) \
  AMDHSA_BITS_ENUM_ENTRY(KERNEL_CODE_PROPERTY_ ## NAME, SHIFT, WIDTH)
/// Kernel code property bitfield packs; layout is backwards compatible.
///
/// Each macro expands to \c NAME_SHIFT, \c NAME_WIDTH, and \c NAME (bit mask).
enum : int32_t {
  KERNEL_CODE_PROPERTY(ENABLE_SGPR_PRIVATE_SEGMENT_BUFFER, 0, 1), ///< Enable private segment buffer SGPR.
  KERNEL_CODE_PROPERTY(ENABLE_SGPR_DISPATCH_PTR, 1, 1), ///< Enable dispatch pointer SGPR.
  KERNEL_CODE_PROPERTY(ENABLE_SGPR_QUEUE_PTR, 2, 1), ///< Enable queue pointer SGPR.
  KERNEL_CODE_PROPERTY(ENABLE_SGPR_KERNARG_SEGMENT_PTR, 3, 1), ///< Enable kernarg segment pointer SGPR.
  KERNEL_CODE_PROPERTY(ENABLE_SGPR_DISPATCH_ID, 4, 1), ///< Enable dispatch ID SGPR.
  KERNEL_CODE_PROPERTY(ENABLE_SGPR_FLAT_SCRATCH_INIT, 5, 1), ///< Enable flat scratch init SGPR.
  KERNEL_CODE_PROPERTY(ENABLE_SGPR_PRIVATE_SEGMENT_SIZE, 6, 1), ///< Enable private segment size SGPR.
  KERNEL_CODE_PROPERTY(RESERVED0, 7, 3), ///< Reserved.
  KERNEL_CODE_PROPERTY(ENABLE_WAVEFRONT_SIZE32, 10, 1), ///< Enable wavefront size 32 (GFX10+).
  KERNEL_CODE_PROPERTY(USES_DYNAMIC_STACK, 11, 1), ///< Kernel uses a dynamically sized stack.
  KERNEL_CODE_PROPERTY(RESERVED1, 12, 4), ///< Reserved.
};
#undef KERNEL_CODE_PROPERTY

// Kernarg preload specification.
#define KERNARG_PRELOAD_SPEC(NAME, SHIFT, WIDTH)                               \
  AMDHSA_BITS_ENUM_ENTRY(KERNARG_PRELOAD_SPEC_##NAME, SHIFT, WIDTH)
/// Kernarg preload specification bitfield packs.
///
/// Each macro expands to \c NAME_SHIFT, \c NAME_WIDTH, and \c NAME (bit mask).
enum : int32_t {
  KERNARG_PRELOAD_SPEC(LENGTH, 0, 7), ///< Number of dwords to preload from kernarg.
  KERNARG_PRELOAD_SPEC(OFFSET, 7, 9), ///< Dword offset into kernarg to begin preload.
};
#undef KERNARG_PRELOAD_SPEC

/// AMDHSA kernel descriptor; layout must stay backwards compatible.
///
/// Contains the information CP needs to launch a kernel. Total size is 64 bytes
/// and must be 64-byte aligned.
struct kernel_descriptor_t {
  /// Fixed local (group) address space size in bytes for a work-group.
  uint32_t group_segment_fixed_size;
  /// Fixed private address space size in bytes for a work-item.
  uint32_t private_segment_fixed_size;
  /// Size in bytes of the kernarg memory pointed to by the AQL dispatch packet.
  uint32_t kernarg_size;
  /// Reserved; must be zero.
  uint8_t reserved0[4];
  /// Byte offset from this descriptor to the kernel entry point (256-byte aligned).
  int64_t kernel_code_entry_byte_offset;
  /// Reserved; must be zero.
  uint8_t reserved1[20];
  /// COMPUTE_PGM_RSRC3 program settings (GFX10+ and GFX90A+); reserved earlier.
  uint32_t compute_pgm_rsrc3; // GFX10+ and GFX90A+
  /// COMPUTE_PGM_RSRC1 compute shader program settings.
  uint32_t compute_pgm_rsrc1;
  /// COMPUTE_PGM_RSRC2 compute shader program settings.
  uint32_t compute_pgm_rsrc2;
  /// Kernel code properties controlling SGPR setup and related flags.
  uint16_t kernel_code_properties;
  /// Kernarg preload length and offset specification.
  uint16_t kernarg_preload;
  /// Reserved; must be zero.
  uint8_t reserved3[4];
};

/// Byte offsets of fields within \c kernel_descriptor_t.
enum : uint32_t {
  GROUP_SEGMENT_FIXED_SIZE_OFFSET = 0, ///< Offset of \c group_segment_fixed_size.
  PRIVATE_SEGMENT_FIXED_SIZE_OFFSET = 4, ///< Offset of \c private_segment_fixed_size.
  KERNARG_SIZE_OFFSET = 8, ///< Offset of \c kernarg_size.
  RESERVED0_OFFSET = 12, ///< Offset of \c reserved0.
  KERNEL_CODE_ENTRY_BYTE_OFFSET_OFFSET = 16, ///< Offset of \c kernel_code_entry_byte_offset.
  RESERVED1_OFFSET = 24, ///< Offset of \c reserved1.
  COMPUTE_PGM_RSRC3_OFFSET = 44, ///< Offset of \c compute_pgm_rsrc3.
  COMPUTE_PGM_RSRC1_OFFSET = 48, ///< Offset of \c compute_pgm_rsrc1.
  COMPUTE_PGM_RSRC2_OFFSET = 52, ///< Offset of \c compute_pgm_rsrc2.
  KERNEL_CODE_PROPERTIES_OFFSET = 56, ///< Offset of \c kernel_code_properties.
  KERNARG_PRELOAD_OFFSET = 58, ///< Offset of \c kernarg_preload.
  RESERVED3_OFFSET = 60 ///< Offset of \c reserved3.
};

static_assert(
    sizeof(kernel_descriptor_t) == 64,
    "invalid size for kernel_descriptor_t");
static_assert(offsetof(kernel_descriptor_t, group_segment_fixed_size) ==
                  GROUP_SEGMENT_FIXED_SIZE_OFFSET,
              "invalid offset for group_segment_fixed_size");
static_assert(offsetof(kernel_descriptor_t, private_segment_fixed_size) ==
                  PRIVATE_SEGMENT_FIXED_SIZE_OFFSET,
              "invalid offset for private_segment_fixed_size");
static_assert(offsetof(kernel_descriptor_t, kernarg_size) ==
                  KERNARG_SIZE_OFFSET,
              "invalid offset for kernarg_size");
static_assert(offsetof(kernel_descriptor_t, reserved0) == RESERVED0_OFFSET,
              "invalid offset for reserved0");
static_assert(offsetof(kernel_descriptor_t, kernel_code_entry_byte_offset) ==
                  KERNEL_CODE_ENTRY_BYTE_OFFSET_OFFSET,
              "invalid offset for kernel_code_entry_byte_offset");
static_assert(offsetof(kernel_descriptor_t, reserved1) == RESERVED1_OFFSET,
              "invalid offset for reserved1");
static_assert(offsetof(kernel_descriptor_t, compute_pgm_rsrc3) ==
                  COMPUTE_PGM_RSRC3_OFFSET,
              "invalid offset for compute_pgm_rsrc3");
static_assert(offsetof(kernel_descriptor_t, compute_pgm_rsrc1) ==
                  COMPUTE_PGM_RSRC1_OFFSET,
              "invalid offset for compute_pgm_rsrc1");
static_assert(offsetof(kernel_descriptor_t, compute_pgm_rsrc2) ==
                  COMPUTE_PGM_RSRC2_OFFSET,
              "invalid offset for compute_pgm_rsrc2");
static_assert(offsetof(kernel_descriptor_t, kernel_code_properties) ==
                  KERNEL_CODE_PROPERTIES_OFFSET,
              "invalid offset for kernel_code_properties");
static_assert(offsetof(kernel_descriptor_t, kernarg_preload) ==
                  KERNARG_PRELOAD_OFFSET,
              "invalid offset for kernarg_preload");
static_assert(offsetof(kernel_descriptor_t, reserved3) == RESERVED3_OFFSET,
              "invalid offset for reserved3");

} // end namespace amdhsa
} // end namespace llvm

#endif // LLVM_SUPPORT_AMDHSAKERNELDESCRIPTOR_H
