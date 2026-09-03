//===--- NVVMIntrinsicUtils.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file contains the definitions of the enumerations and flags
/// associated with NVVM Intrinsics, along with some helper functions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_NVVMINTRINSICUTILS_H
#define LLVM_IR_NVVMINTRINSICUTILS_H

#include <stdint.h>

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsNVPTX.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
/// Helpers for NVVM / NVPTX intrinsic enums, printers, and FP semantics.
namespace nvvm {

/// Reduction ops for TMA shared-to-global bulk tensor copies.
///
/// These map to the \c cp.reduce.async.bulk.tensor.* family of PTX
/// instructions.
enum class TMAReductionOp : uint8_t {
  ADD = 0, ///< Element-wise add reduction.
  MIN = 1, ///< Element-wise minimum reduction.
  MAX = 2, ///< Element-wise maximum reduction.
  INC = 3, ///< Saturating increment reduction.
  DEC = 4, ///< Saturating decrement reduction.
  AND = 5, ///< Bitwise AND reduction.
  OR = 6,  ///< Bitwise OR reduction.
  XOR = 7, ///< Bitwise XOR reduction.
};

/// Return the PTX name for a TMA tensor reduction operation.
/// \param Op The TMA reduction operation to name.
/// \return The PTX name string for \p Op.
inline StringRef getTMATensorReductionOpName(TMAReductionOp Op) {
  switch (Op) {
  case TMAReductionOp::ADD:
    return "add";
  case TMAReductionOp::MIN:
    return "min";
  case TMAReductionOp::MAX:
    return "max";
  case TMAReductionOp::INC:
    return "inc";
  case TMAReductionOp::DEC:
    return "dec";
  case TMAReductionOp::AND:
    return "and";
  case TMAReductionOp::OR:
    return "or";
  case TMAReductionOp::XOR:
    return "xor";
  }
  llvm_unreachable("invalid TMA tensorreduction operation");
}

/// CTA group variants for TMA / TCGEN05 PTX instructions.
enum class CTAGroupKind : uint8_t {
  CG_NONE = 0, ///< Default with no \c cta_group modifier.
  CG_1 = 1,    ///< The \c cta_group::1 modifier.
  CG_2 = 2,    ///< The \c cta_group::2 modifier.
};

/// Eviction priorities for prefetch and applypriority intrinsics.
enum class EvictPolicyType : uint8_t {
  EVICT_NORMAL = 0, ///< Default L2 eviction policy.
  EVICT_LAST = 1,   ///< Prefer keeping the line until last use.
};

/// Return the PTX name for an L2 eviction policy.
/// \param Policy The eviction policy to name.
/// \return The PTX name string for \p Policy.
inline StringRef getEvictPolicyName(EvictPolicyType Policy) {
  switch (Policy) {
  case EvictPolicyType::EVICT_NORMAL:
    return "L2::evict_normal";
  case EvictPolicyType::EVICT_LAST:
    return "L2::evict_last";
  }
  llvm_unreachable("invalid evict policy");
}

/// Matrix-multiply-accumulate data kinds for TCGEN05 MMA.
enum class Tcgen05MMAKind : uint8_t {
  F16 = 0,    ///< Half-precision floating-point MMA.
  TF32 = 1,   ///< TensorFloat-32 MMA.
  F8F6F4 = 2, ///< Mixed 8/6/4-bit floating-point MMA.
  I8 = 3,     ///< 8-bit integer MMA.
  TI16 = 4,   ///< 16-bit integer tensor MMA.
};

/// Collector usage operations for TCGEN05 MMA.
enum class Tcgen05CollectorUsageOp : uint8_t {
  DISCARD = 0, ///< Discard the collector contents.
  LASTUSE = 1, ///< Mark the last use of the collector.
  FILL = 2,    ///< Fill the collector from memory.
  USE = 3,     ///< Use the current collector contents.
};

/// B-buffer selectors for TCGEN05 MMA collectors.
enum class Tcgen05MMACollectorBBuffer : uint8_t {
  B0 = 0, ///< Collector B-buffer 0.
  B1 = 1, ///< Collector B-buffer 1.
  B2 = 2, ///< Collector B-buffer 2.
  B3 = 3, ///< Collector B-buffer 3.
};

/// Element types encoded in a tensormap descriptor.
enum class TensormapElemType : uint8_t {
  U8 = 0,         ///< Unsigned 8-bit integer.
  U16 = 1,        ///< Unsigned 16-bit integer.
  U32 = 2,        ///< Unsigned 32-bit integer.
  S32 = 3,        ///< Signed 32-bit integer.
  U64 = 4,        ///< Unsigned 64-bit integer.
  S64 = 5,        ///< Signed 64-bit integer.
  F16 = 6,        ///< Half-precision floating-point.
  F32 = 7,        ///< Single-precision floating-point.
  F32_FTZ = 8,    ///< Single-precision with flush-to-zero.
  F64 = 9,        ///< Double-precision floating-point.
  BF16 = 10,      ///< Brain floating-point 16.
  TF32 = 11,      ///< TensorFloat-32.
  TF32_FTZ = 12,  ///< TensorFloat-32 with flush-to-zero.
  B4x16 = 13,     ///< Packed 4-bit elements in 16-bit containers.
  B4x16_p64 = 14, ///< Packed 4-bit elements with 64-bit packing.
  B6x16_p32 = 15, ///< Packed 6-bit elements with 32-bit packing.
};

/// Interleave layouts for tensormap descriptors.
enum class TensormapInterleaveLayout : uint8_t {
  NO_INTERLEAVE = 0,  ///< No interleaving.
  INTERLEAVE_16B = 1, ///< 16-byte interleave.
  INTERLEAVE_32B = 2, ///< 32-byte interleave.
};

/// Swizzle modes for tensormap descriptors.
enum class TensormapSwizzleMode : uint8_t {
  NO_SWIZZLE = 0,   ///< No swizzling.
  SWIZZLE_32B = 1,  ///< 32-byte swizzle.
  SWIZZLE_64B = 2,  ///< 64-byte swizzle.
  SWIZZLE_128B = 3, ///< 128-byte swizzle.
  SWIZZLE_96B = 4,  ///< 96-byte swizzle.
};

/// Swizzle atomicity widths for tensormap descriptors.
enum class TensormapSwizzleAtomicity : uint8_t {
  SWIZZLE_ATOMICITY_16B = 0,         ///< 16-byte atomicity.
  SWIZZLE_ATOMICITY_32B = 1,         ///< 32-byte atomicity.
  SWIZZLE_ATOMICITY_32B_FLIP_8B = 2, ///< 32-byte atomicity with 8-byte flip.
  SWIZZLE_ATOMICITY_64B = 3,         ///< 64-byte atomicity.
};

/// Out-of-bounds fill modes for tensormap descriptors.
enum class TensormapFillMode : uint8_t {
  ZERO_FILL = 0,    ///< Fill out-of-bounds with zero.
  OOB_NAN_FILL = 1, ///< Fill out-of-bounds with NaN.
};

/// Print a TCGEN05 MMA kind immediate to \p OS.
/// \param OS Stream to write the kind name to.
/// \param ImmArgVal Constant holding the \c Tcgen05MMAKind value.
LLVM_ABI void printTcgen05MMAKind(raw_ostream &OS, const Constant *ImmArgVal);

/// Print an eviction policy immediate to \p OS.
/// \param OS Stream to write the policy name to.
/// \param ImmArgVal Constant holding the \c EvictPolicyType value.
LLVM_ABI void printEvictPolicyType(raw_ostream &OS, const Constant *ImmArgVal);

/// Print a TMA reduction op immediate to \p OS.
/// \param OS Stream to write the reduction op name to.
/// \param ImmArgVal Constant holding the \c TMAReductionOp value.
LLVM_ABI void printTMAReductionOp(raw_ostream &OS, const Constant *ImmArgVal);

/// Print a TCGEN05 collector usage op immediate to \p OS.
/// \param OS Stream to write the usage op name to.
/// \param ImmArgVal Constant holding the \c Tcgen05CollectorUsageOp value.
LLVM_ABI void printTcgen05CollectorUsageOp(raw_ostream &OS,
                                           const Constant *ImmArgVal);

/// Print a TCGEN05 MMA collector B-buffer immediate to \p OS.
/// \param OS Stream to write the B-buffer name to.
/// \param ImmArgVal Constant holding the \c Tcgen05MMACollectorBBuffer value.
LLVM_ABI void printTcgen05MMACollectorBBuffer(raw_ostream &OS,
                                              const Constant *ImmArgVal);

/// Print a tensormap element type immediate to \p OS.
/// \param OS Stream to write the element type name to.
/// \param ImmArgVal Constant holding the \c TensormapElemType value.
LLVM_ABI void printTensormapElemType(raw_ostream &OS,
                                     const Constant *ImmArgVal);
/// Print a tensormap interleave layout immediate to \p OS.
/// \param OS Stream to write the layout name to.
/// \param ImmArgVal Constant holding the \c TensormapInterleaveLayout value.
LLVM_ABI void printTensormapInterleaveLayout(raw_ostream &OS,
                                             const Constant *ImmArgVal);
/// Print a tensormap swizzle mode immediate to \p OS.
/// \param OS Stream to write the swizzle mode name to.
/// \param ImmArgVal Constant holding the \c TensormapSwizzleMode value.
LLVM_ABI void printTensormapSwizzleMode(raw_ostream &OS,
                                        const Constant *ImmArgVal);
/// Print a tensormap swizzle atomicity immediate to \p OS.
/// \param OS Stream to write the atomicity name to.
/// \param ImmArgVal Constant holding the \c TensormapSwizzleAtomicity value.
LLVM_ABI void printTensormapSwizzleAtomicity(raw_ostream &OS,
                                             const Constant *ImmArgVal);
/// Print a tensormap fill mode immediate to \p OS.
/// \param OS Stream to write the fill mode name to.
/// \param ImmArgVal Constant holding the \c TensormapFillMode value.
LLVM_ABI void printTensormapFillMode(raw_ostream &OS,
                                     const Constant *ImmArgVal);

/// Return true if the FP-to-integer intrinsic flushes denormals to zero.
/// \param IntrinsicID NVVM f2i/d2i (and related) intrinsic to query.
/// \return True if \p IntrinsicID is an FTZ conversion variant; false otherwise.
inline bool FPToIntegerIntrinsicShouldFTZ(Intrinsic::ID IntrinsicID) {
  switch (IntrinsicID) {
  case Intrinsic::nvvm_f2i_rm_ftz:
  case Intrinsic::nvvm_f2i_rn_ftz:
  case Intrinsic::nvvm_f2i_rp_ftz:
  case Intrinsic::nvvm_f2i_rz_ftz:

  case Intrinsic::nvvm_f2ui_rm_ftz:
  case Intrinsic::nvvm_f2ui_rn_ftz:
  case Intrinsic::nvvm_f2ui_rp_ftz:
  case Intrinsic::nvvm_f2ui_rz_ftz:

  case Intrinsic::nvvm_f2ll_rm_ftz:
  case Intrinsic::nvvm_f2ll_rn_ftz:
  case Intrinsic::nvvm_f2ll_rp_ftz:
  case Intrinsic::nvvm_f2ll_rz_ftz:

  case Intrinsic::nvvm_f2ull_rm_ftz:
  case Intrinsic::nvvm_f2ull_rn_ftz:
  case Intrinsic::nvvm_f2ull_rp_ftz:
  case Intrinsic::nvvm_f2ull_rz_ftz:
    return true;

  case Intrinsic::nvvm_f2i_rm:
  case Intrinsic::nvvm_f2i_rn:
  case Intrinsic::nvvm_f2i_rp:
  case Intrinsic::nvvm_f2i_rz:

  case Intrinsic::nvvm_f2ui_rm:
  case Intrinsic::nvvm_f2ui_rn:
  case Intrinsic::nvvm_f2ui_rp:
  case Intrinsic::nvvm_f2ui_rz:

  case Intrinsic::nvvm_d2i_rm:
  case Intrinsic::nvvm_d2i_rn:
  case Intrinsic::nvvm_d2i_rp:
  case Intrinsic::nvvm_d2i_rz:

  case Intrinsic::nvvm_d2ui_rm:
  case Intrinsic::nvvm_d2ui_rn:
  case Intrinsic::nvvm_d2ui_rp:
  case Intrinsic::nvvm_d2ui_rz:

  case Intrinsic::nvvm_f2ll_rm:
  case Intrinsic::nvvm_f2ll_rn:
  case Intrinsic::nvvm_f2ll_rp:
  case Intrinsic::nvvm_f2ll_rz:

  case Intrinsic::nvvm_f2ull_rm:
  case Intrinsic::nvvm_f2ull_rn:
  case Intrinsic::nvvm_f2ull_rp:
  case Intrinsic::nvvm_f2ull_rz:

  case Intrinsic::nvvm_d2ll_rm:
  case Intrinsic::nvvm_d2ll_rn:
  case Intrinsic::nvvm_d2ll_rp:
  case Intrinsic::nvvm_d2ll_rz:

  case Intrinsic::nvvm_d2ull_rm:
  case Intrinsic::nvvm_d2ull_rn:
  case Intrinsic::nvvm_d2ull_rp:
  case Intrinsic::nvvm_d2ull_rz:
    return false;
  }
  llvm_unreachable("Checking FTZ flag for invalid f2i/d2i intrinsic");
}

/// Return true if the FP-to-integer intrinsic converts to a signed integer.
/// \param IntrinsicID NVVM f2i/d2i (and related) intrinsic to query.
/// \return True if \p IntrinsicID converts to a signed integer; false otherwise.
inline bool FPToIntegerIntrinsicResultIsSigned(Intrinsic::ID IntrinsicID) {
  switch (IntrinsicID) {
  // f2i
  case Intrinsic::nvvm_f2i_rm:
  case Intrinsic::nvvm_f2i_rm_ftz:
  case Intrinsic::nvvm_f2i_rn:
  case Intrinsic::nvvm_f2i_rn_ftz:
  case Intrinsic::nvvm_f2i_rp:
  case Intrinsic::nvvm_f2i_rp_ftz:
  case Intrinsic::nvvm_f2i_rz:
  case Intrinsic::nvvm_f2i_rz_ftz:
  // d2i
  case Intrinsic::nvvm_d2i_rm:
  case Intrinsic::nvvm_d2i_rn:
  case Intrinsic::nvvm_d2i_rp:
  case Intrinsic::nvvm_d2i_rz:
  // f2ll
  case Intrinsic::nvvm_f2ll_rm:
  case Intrinsic::nvvm_f2ll_rm_ftz:
  case Intrinsic::nvvm_f2ll_rn:
  case Intrinsic::nvvm_f2ll_rn_ftz:
  case Intrinsic::nvvm_f2ll_rp:
  case Intrinsic::nvvm_f2ll_rp_ftz:
  case Intrinsic::nvvm_f2ll_rz:
  case Intrinsic::nvvm_f2ll_rz_ftz:
  // d2ll
  case Intrinsic::nvvm_d2ll_rm:
  case Intrinsic::nvvm_d2ll_rn:
  case Intrinsic::nvvm_d2ll_rp:
  case Intrinsic::nvvm_d2ll_rz:
    return true;

  // f2ui
  case Intrinsic::nvvm_f2ui_rm:
  case Intrinsic::nvvm_f2ui_rm_ftz:
  case Intrinsic::nvvm_f2ui_rn:
  case Intrinsic::nvvm_f2ui_rn_ftz:
  case Intrinsic::nvvm_f2ui_rp:
  case Intrinsic::nvvm_f2ui_rp_ftz:
  case Intrinsic::nvvm_f2ui_rz:
  case Intrinsic::nvvm_f2ui_rz_ftz:
  // d2ui
  case Intrinsic::nvvm_d2ui_rm:
  case Intrinsic::nvvm_d2ui_rn:
  case Intrinsic::nvvm_d2ui_rp:
  case Intrinsic::nvvm_d2ui_rz:
  // f2ull
  case Intrinsic::nvvm_f2ull_rm:
  case Intrinsic::nvvm_f2ull_rm_ftz:
  case Intrinsic::nvvm_f2ull_rn:
  case Intrinsic::nvvm_f2ull_rn_ftz:
  case Intrinsic::nvvm_f2ull_rp:
  case Intrinsic::nvvm_f2ull_rp_ftz:
  case Intrinsic::nvvm_f2ull_rz:
  case Intrinsic::nvvm_f2ull_rz_ftz:
  // d2ull
  case Intrinsic::nvvm_d2ull_rm:
  case Intrinsic::nvvm_d2ull_rn:
  case Intrinsic::nvvm_d2ull_rp:
  case Intrinsic::nvvm_d2ull_rz:
    return false;
  }
  llvm_unreachable(
      "Checking invalid f2i/d2i intrinsic for signed int conversion");
}

/// Return true if the FP-to-integer intrinsic converts NaN inputs to zero.
/// \param IntrinsicID NVVM f2i/d2i (and related) intrinsic to query.
/// \return True if \p IntrinsicID maps NaN inputs to zero; false otherwise.
inline bool FPToIntegerIntrinsicNaNZero(Intrinsic::ID IntrinsicID) {
  switch (IntrinsicID) {
  // f2i
  case Intrinsic::nvvm_f2i_rm:
  case Intrinsic::nvvm_f2i_rn:
  case Intrinsic::nvvm_f2i_rp:
  case Intrinsic::nvvm_f2i_rz:
  case Intrinsic::nvvm_f2i_rm_ftz:
  case Intrinsic::nvvm_f2i_rn_ftz:
  case Intrinsic::nvvm_f2i_rp_ftz:
  case Intrinsic::nvvm_f2i_rz_ftz:
  // f2ui
  case Intrinsic::nvvm_f2ui_rm:
  case Intrinsic::nvvm_f2ui_rn:
  case Intrinsic::nvvm_f2ui_rp:
  case Intrinsic::nvvm_f2ui_rz:
  case Intrinsic::nvvm_f2ui_rm_ftz:
  case Intrinsic::nvvm_f2ui_rn_ftz:
  case Intrinsic::nvvm_f2ui_rp_ftz:
  case Intrinsic::nvvm_f2ui_rz_ftz:
    return true;
  // d2i
  case Intrinsic::nvvm_d2i_rm:
  case Intrinsic::nvvm_d2i_rn:
  case Intrinsic::nvvm_d2i_rp:
  case Intrinsic::nvvm_d2i_rz:
  // d2ui
  case Intrinsic::nvvm_d2ui_rm:
  case Intrinsic::nvvm_d2ui_rn:
  case Intrinsic::nvvm_d2ui_rp:
  case Intrinsic::nvvm_d2ui_rz:
  // f2ll
  case Intrinsic::nvvm_f2ll_rm:
  case Intrinsic::nvvm_f2ll_rn:
  case Intrinsic::nvvm_f2ll_rp:
  case Intrinsic::nvvm_f2ll_rz:
  case Intrinsic::nvvm_f2ll_rm_ftz:
  case Intrinsic::nvvm_f2ll_rn_ftz:
  case Intrinsic::nvvm_f2ll_rp_ftz:
  case Intrinsic::nvvm_f2ll_rz_ftz:
  // f2ull
  case Intrinsic::nvvm_f2ull_rm:
  case Intrinsic::nvvm_f2ull_rn:
  case Intrinsic::nvvm_f2ull_rp:
  case Intrinsic::nvvm_f2ull_rz:
  case Intrinsic::nvvm_f2ull_rm_ftz:
  case Intrinsic::nvvm_f2ull_rn_ftz:
  case Intrinsic::nvvm_f2ull_rp_ftz:
  case Intrinsic::nvvm_f2ull_rz_ftz:
  // d2ll
  case Intrinsic::nvvm_d2ll_rm:
  case Intrinsic::nvvm_d2ll_rn:
  case Intrinsic::nvvm_d2ll_rp:
  case Intrinsic::nvvm_d2ll_rz:
  // d2ull
  case Intrinsic::nvvm_d2ull_rm:
  case Intrinsic::nvvm_d2ull_rn:
  case Intrinsic::nvvm_d2ull_rp:
  case Intrinsic::nvvm_d2ull_rz:
    return false;
  }
  llvm_unreachable("Checking NaN result for invalid f2i/d2i intrinsic");
}

/// Return the rounding mode of an NVVM FP-to-integer conversion intrinsic.
/// \param IntrinsicID NVVM f2i/d2i (and related) intrinsic to query.
/// \return The \c APFloat rounding mode encoded by \p IntrinsicID.
inline APFloat::roundingMode
GetFPToIntegerRoundingMode(Intrinsic::ID IntrinsicID) {
  switch (IntrinsicID) {
  // RM:
  case Intrinsic::nvvm_f2i_rm:
  case Intrinsic::nvvm_f2ui_rm:
  case Intrinsic::nvvm_f2i_rm_ftz:
  case Intrinsic::nvvm_f2ui_rm_ftz:
  case Intrinsic::nvvm_d2i_rm:
  case Intrinsic::nvvm_d2ui_rm:

  case Intrinsic::nvvm_f2ll_rm:
  case Intrinsic::nvvm_f2ull_rm:
  case Intrinsic::nvvm_f2ll_rm_ftz:
  case Intrinsic::nvvm_f2ull_rm_ftz:
  case Intrinsic::nvvm_d2ll_rm:
  case Intrinsic::nvvm_d2ull_rm:
    return APFloat::rmTowardNegative;

  // RN:
  case Intrinsic::nvvm_f2i_rn:
  case Intrinsic::nvvm_f2ui_rn:
  case Intrinsic::nvvm_f2i_rn_ftz:
  case Intrinsic::nvvm_f2ui_rn_ftz:
  case Intrinsic::nvvm_d2i_rn:
  case Intrinsic::nvvm_d2ui_rn:

  case Intrinsic::nvvm_f2ll_rn:
  case Intrinsic::nvvm_f2ull_rn:
  case Intrinsic::nvvm_f2ll_rn_ftz:
  case Intrinsic::nvvm_f2ull_rn_ftz:
  case Intrinsic::nvvm_d2ll_rn:
  case Intrinsic::nvvm_d2ull_rn:
    return APFloat::rmNearestTiesToEven;

  // RP:
  case Intrinsic::nvvm_f2i_rp:
  case Intrinsic::nvvm_f2ui_rp:
  case Intrinsic::nvvm_f2i_rp_ftz:
  case Intrinsic::nvvm_f2ui_rp_ftz:
  case Intrinsic::nvvm_d2i_rp:
  case Intrinsic::nvvm_d2ui_rp:

  case Intrinsic::nvvm_f2ll_rp:
  case Intrinsic::nvvm_f2ull_rp:
  case Intrinsic::nvvm_f2ll_rp_ftz:
  case Intrinsic::nvvm_f2ull_rp_ftz:
  case Intrinsic::nvvm_d2ll_rp:
  case Intrinsic::nvvm_d2ull_rp:
    return APFloat::rmTowardPositive;

  // RZ:
  case Intrinsic::nvvm_f2i_rz:
  case Intrinsic::nvvm_f2ui_rz:
  case Intrinsic::nvvm_f2i_rz_ftz:
  case Intrinsic::nvvm_f2ui_rz_ftz:
  case Intrinsic::nvvm_d2i_rz:
  case Intrinsic::nvvm_d2ui_rz:

  case Intrinsic::nvvm_f2ll_rz:
  case Intrinsic::nvvm_f2ull_rz:
  case Intrinsic::nvvm_f2ll_rz_ftz:
  case Intrinsic::nvvm_f2ull_rz_ftz:
  case Intrinsic::nvvm_d2ll_rz:
  case Intrinsic::nvvm_d2ull_rz:
    return APFloat::rmTowardZero;
  }
  llvm_unreachable("Checking rounding mode for invalid f2i/d2i intrinsic");
}

/// Return true if the fmin/fmax intrinsic flushes denormals to zero.
/// \param IntrinsicID NVVM fmin/fmax intrinsic to query.
/// \return True if \p IntrinsicID is an FTZ fmin/fmax variant; false otherwise.
inline bool FMinFMaxShouldFTZ(Intrinsic::ID IntrinsicID) {
  switch (IntrinsicID) {
  case Intrinsic::nvvm_fmax_ftz_f:
  case Intrinsic::nvvm_fmax_ftz_nan_f:
  case Intrinsic::nvvm_fmax_ftz_nan_xorsign_abs_f:
  case Intrinsic::nvvm_fmax_ftz_xorsign_abs_f:

  case Intrinsic::nvvm_fmin_ftz_f:
  case Intrinsic::nvvm_fmin_ftz_nan_f:
  case Intrinsic::nvvm_fmin_ftz_nan_xorsign_abs_f:
  case Intrinsic::nvvm_fmin_ftz_xorsign_abs_f:
    return true;

  case Intrinsic::nvvm_fmax_d:
  case Intrinsic::nvvm_fmax_f:
  case Intrinsic::nvvm_fmax_nan_f:
  case Intrinsic::nvvm_fmax_nan_xorsign_abs_f:
  case Intrinsic::nvvm_fmax_xorsign_abs_f:

  case Intrinsic::nvvm_fmin_d:
  case Intrinsic::nvvm_fmin_f:
  case Intrinsic::nvvm_fmin_nan_f:
  case Intrinsic::nvvm_fmin_nan_xorsign_abs_f:
  case Intrinsic::nvvm_fmin_xorsign_abs_f:
    return false;
  }
  llvm_unreachable("Checking FTZ flag for invalid fmin/fmax intrinsic");
}

/// Return true if the fmin/fmax intrinsic propagates NaN operands.
/// \param IntrinsicID NVVM fmin/fmax intrinsic to query.
/// \return True if \p IntrinsicID propagates NaNs; false otherwise.
inline bool FMinFMaxPropagatesNaNs(Intrinsic::ID IntrinsicID) {
  switch (IntrinsicID) {
  case Intrinsic::nvvm_fmax_ftz_nan_f:
  case Intrinsic::nvvm_fmax_nan_f:
  case Intrinsic::nvvm_fmax_ftz_nan_xorsign_abs_f:
  case Intrinsic::nvvm_fmax_nan_xorsign_abs_f:

  case Intrinsic::nvvm_fmin_ftz_nan_f:
  case Intrinsic::nvvm_fmin_nan_f:
  case Intrinsic::nvvm_fmin_ftz_nan_xorsign_abs_f:
  case Intrinsic::nvvm_fmin_nan_xorsign_abs_f:
    return true;

  case Intrinsic::nvvm_fmax_d:
  case Intrinsic::nvvm_fmax_f:
  case Intrinsic::nvvm_fmax_ftz_f:
  case Intrinsic::nvvm_fmax_ftz_xorsign_abs_f:
  case Intrinsic::nvvm_fmax_xorsign_abs_f:

  case Intrinsic::nvvm_fmin_d:
  case Intrinsic::nvvm_fmin_f:
  case Intrinsic::nvvm_fmin_ftz_f:
  case Intrinsic::nvvm_fmin_ftz_xorsign_abs_f:
  case Intrinsic::nvvm_fmin_xorsign_abs_f:
    return false;
  }
  llvm_unreachable("Checking NaN flag for invalid fmin/fmax intrinsic");
}

/// Return true if the fmin/fmax intrinsic uses the xorsign-abs variant.
/// \param IntrinsicID NVVM fmin/fmax intrinsic to query.
/// \return True if \p IntrinsicID is an xorsign-abs variant; false otherwise.
inline bool FMinFMaxIsXorSignAbs(Intrinsic::ID IntrinsicID) {
  switch (IntrinsicID) {
  case Intrinsic::nvvm_fmax_ftz_nan_xorsign_abs_f:
  case Intrinsic::nvvm_fmax_ftz_xorsign_abs_f:
  case Intrinsic::nvvm_fmax_nan_xorsign_abs_f:
  case Intrinsic::nvvm_fmax_xorsign_abs_f:

  case Intrinsic::nvvm_fmin_ftz_nan_xorsign_abs_f:
  case Intrinsic::nvvm_fmin_ftz_xorsign_abs_f:
  case Intrinsic::nvvm_fmin_nan_xorsign_abs_f:
  case Intrinsic::nvvm_fmin_xorsign_abs_f:
    return true;

  case Intrinsic::nvvm_fmax_d:
  case Intrinsic::nvvm_fmax_f:
  case Intrinsic::nvvm_fmax_ftz_f:
  case Intrinsic::nvvm_fmax_ftz_nan_f:
  case Intrinsic::nvvm_fmax_nan_f:

  case Intrinsic::nvvm_fmin_d:
  case Intrinsic::nvvm_fmin_f:
  case Intrinsic::nvvm_fmin_ftz_f:
  case Intrinsic::nvvm_fmin_ftz_nan_f:
  case Intrinsic::nvvm_fmin_nan_f:
    return false;
  }
  llvm_unreachable("Checking XorSignAbs flag for invalid fmin/fmax intrinsic");
}

/// Return true if the unary math intrinsic flushes denormals to zero.
/// \param IntrinsicID NVVM unary math intrinsic to query.
/// \return True if \p IntrinsicID is an FTZ unary math variant; false otherwise.
inline bool UnaryMathIntrinsicShouldFTZ(Intrinsic::ID IntrinsicID) {
  switch (IntrinsicID) {
  case Intrinsic::nvvm_ceil_ftz_f:
  case Intrinsic::nvvm_fabs_ftz:
  case Intrinsic::nvvm_floor_ftz_f:
  case Intrinsic::nvvm_round_ftz_f:
  case Intrinsic::nvvm_saturate_ftz_f:
  case Intrinsic::nvvm_sqrt_rn_ftz_f:
    return true;
  case Intrinsic::nvvm_ceil_f:
  case Intrinsic::nvvm_ceil_d:
  case Intrinsic::nvvm_fabs:
  case Intrinsic::nvvm_floor_f:
  case Intrinsic::nvvm_floor_d:
  case Intrinsic::nvvm_round_f:
  case Intrinsic::nvvm_round_d:
  case Intrinsic::nvvm_saturate_d:
  case Intrinsic::nvvm_saturate_f:
  case Intrinsic::nvvm_sqrt_f:
  case Intrinsic::nvvm_sqrt_rn_d:
  case Intrinsic::nvvm_sqrt_rn_f:
    return false;
  }
  llvm_unreachable("Checking FTZ flag for invalid unary intrinsic");
}

/// Return true if the reciprocal intrinsic flushes denormals to zero.
/// \param IntrinsicID NVVM rcp intrinsic to query.
/// \return True if \p IntrinsicID is an FTZ rcp variant; false otherwise.
inline bool RCPShouldFTZ(Intrinsic::ID IntrinsicID) {
  switch (IntrinsicID) {
  case Intrinsic::nvvm_rcp_rm_ftz_f:
  case Intrinsic::nvvm_rcp_rn_ftz_f:
  case Intrinsic::nvvm_rcp_rp_ftz_f:
  case Intrinsic::nvvm_rcp_rz_ftz_f:
    return true;
  case Intrinsic::nvvm_rcp_rm_d:
  case Intrinsic::nvvm_rcp_rm_f:
  case Intrinsic::nvvm_rcp_rn_d:
  case Intrinsic::nvvm_rcp_rn_f:
  case Intrinsic::nvvm_rcp_rp_d:
  case Intrinsic::nvvm_rcp_rp_f:
  case Intrinsic::nvvm_rcp_rz_d:
  case Intrinsic::nvvm_rcp_rz_f:
    return false;
  }
  llvm_unreachable("Checking FTZ flag for invalid rcp intrinsic");
}

/// Return the rounding mode of an NVVM reciprocal intrinsic.
/// \param IntrinsicID NVVM rcp intrinsic to query.
/// \return The \c APFloat rounding mode encoded by \p IntrinsicID.
inline APFloat::roundingMode GetRCPRoundingMode(Intrinsic::ID IntrinsicID) {
  switch (IntrinsicID) {
  case Intrinsic::nvvm_rcp_rm_f:
  case Intrinsic::nvvm_rcp_rm_d:
  case Intrinsic::nvvm_rcp_rm_ftz_f:
    return APFloat::rmTowardNegative;

  case Intrinsic::nvvm_rcp_rn_f:
  case Intrinsic::nvvm_rcp_rn_d:
  case Intrinsic::nvvm_rcp_rn_ftz_f:
    return APFloat::rmNearestTiesToEven;

  case Intrinsic::nvvm_rcp_rp_f:
  case Intrinsic::nvvm_rcp_rp_d:
  case Intrinsic::nvvm_rcp_rp_ftz_f:
    return APFloat::rmTowardPositive;

  case Intrinsic::nvvm_rcp_rz_f:
  case Intrinsic::nvvm_rcp_rz_d:
  case Intrinsic::nvvm_rcp_rz_ftz_f:
    return APFloat::rmTowardZero;
  }
  llvm_unreachable("Checking rounding mode for invalid rcp intrinsic");
}

/// Map an FTZ preference to the corresponding LLVM denormal mode.
/// \param ShouldFTZ True to use preserve-sign FTZ; false for IEEE mode.
/// \return Preserve-sign denormal mode if \p ShouldFTZ is true; IEEE otherwise.
inline DenormalMode GetNVVMDenormMode(bool ShouldFTZ) {
  if (ShouldFTZ)
    return DenormalMode::getPreserveSign();
  return DenormalMode::getIEEE();
}

/// Return true if the NVVM add intrinsic flushes denormals to zero.
/// \param IntrinsicID NVVM add intrinsic to query.
/// \return True if \p IntrinsicID is an FTZ add variant; false otherwise.
inline bool FAddShouldFTZ(Intrinsic::ID IntrinsicID) {
  switch (IntrinsicID) {
  case Intrinsic::nvvm_add_rm_ftz_f:
  case Intrinsic::nvvm_add_rn_ftz_f:
  case Intrinsic::nvvm_add_rp_ftz_f:
  case Intrinsic::nvvm_add_rz_ftz_f:
    return true;

  case Intrinsic::nvvm_add_rm_f:
  case Intrinsic::nvvm_add_rn_f:
  case Intrinsic::nvvm_add_rp_f:
  case Intrinsic::nvvm_add_rz_f:
  case Intrinsic::nvvm_add_rm_d:
  case Intrinsic::nvvm_add_rn_d:
  case Intrinsic::nvvm_add_rp_d:
  case Intrinsic::nvvm_add_rz_d:
    return false;
  }
  llvm_unreachable("Checking FTZ flag for invalid NVVM add intrinsic");
}

/// Return the rounding mode of an NVVM floating-point add intrinsic.
/// \param IntrinsicID NVVM add intrinsic to query.
/// \return The \c APFloat rounding mode encoded by \p IntrinsicID.
inline APFloat::roundingMode GetFAddRoundingMode(Intrinsic::ID IntrinsicID) {
  switch (IntrinsicID) {
  case Intrinsic::nvvm_add_rm_f:
  case Intrinsic::nvvm_add_rm_d:
  case Intrinsic::nvvm_add_rm_ftz_f:
    return APFloat::rmTowardNegative;
  case Intrinsic::nvvm_add_rn_f:
  case Intrinsic::nvvm_add_rn_d:
  case Intrinsic::nvvm_add_rn_ftz_f:
    return APFloat::rmNearestTiesToEven;
  case Intrinsic::nvvm_add_rp_f:
  case Intrinsic::nvvm_add_rp_d:
  case Intrinsic::nvvm_add_rp_ftz_f:
    return APFloat::rmTowardPositive;
  case Intrinsic::nvvm_add_rz_f:
  case Intrinsic::nvvm_add_rz_d:
  case Intrinsic::nvvm_add_rz_ftz_f:
    return APFloat::rmTowardZero;
  }
  llvm_unreachable("Invalid FP instrinsic rounding mode for NVVM add");
}

/// Return true if the NVVM mul intrinsic flushes denormals to zero.
/// \param IntrinsicID NVVM mul intrinsic to query.
/// \return True if \p IntrinsicID is an FTZ mul variant; false otherwise.
inline bool FMulShouldFTZ(Intrinsic::ID IntrinsicID) {
  switch (IntrinsicID) {
  case Intrinsic::nvvm_mul_rm_ftz_f:
  case Intrinsic::nvvm_mul_rn_ftz_f:
  case Intrinsic::nvvm_mul_rp_ftz_f:
  case Intrinsic::nvvm_mul_rz_ftz_f:
    return true;

  case Intrinsic::nvvm_mul_rm_f:
  case Intrinsic::nvvm_mul_rn_f:
  case Intrinsic::nvvm_mul_rp_f:
  case Intrinsic::nvvm_mul_rz_f:
  case Intrinsic::nvvm_mul_rm_d:
  case Intrinsic::nvvm_mul_rn_d:
  case Intrinsic::nvvm_mul_rp_d:
  case Intrinsic::nvvm_mul_rz_d:
    return false;
  }
  llvm_unreachable("Checking FTZ flag for invalid NVVM mul intrinsic");
}

/// Return the rounding mode of an NVVM floating-point mul intrinsic.
/// \param IntrinsicID NVVM mul intrinsic to query.
/// \return The \c APFloat rounding mode encoded by \p IntrinsicID.
inline APFloat::roundingMode GetFMulRoundingMode(Intrinsic::ID IntrinsicID) {
  switch (IntrinsicID) {
  case Intrinsic::nvvm_mul_rm_f:
  case Intrinsic::nvvm_mul_rm_d:
  case Intrinsic::nvvm_mul_rm_ftz_f:
    return APFloat::rmTowardNegative;
  case Intrinsic::nvvm_mul_rn_f:
  case Intrinsic::nvvm_mul_rn_d:
  case Intrinsic::nvvm_mul_rn_ftz_f:
    return APFloat::rmNearestTiesToEven;
  case Intrinsic::nvvm_mul_rp_f:
  case Intrinsic::nvvm_mul_rp_d:
  case Intrinsic::nvvm_mul_rp_ftz_f:
    return APFloat::rmTowardPositive;
  case Intrinsic::nvvm_mul_rz_f:
  case Intrinsic::nvvm_mul_rz_d:
  case Intrinsic::nvvm_mul_rz_ftz_f:
    return APFloat::rmTowardZero;
  }
  llvm_unreachable("Invalid FP instrinsic rounding mode for NVVM mul");
}

/// Return true if the NVVM div intrinsic flushes denormals to zero.
/// \param IntrinsicID NVVM div intrinsic to query.
/// \return True if \p IntrinsicID is an FTZ div variant; false otherwise.
inline bool FDivShouldFTZ(Intrinsic::ID IntrinsicID) {
  switch (IntrinsicID) {
  case Intrinsic::nvvm_div_rm_ftz_f:
  case Intrinsic::nvvm_div_rn_ftz_f:
  case Intrinsic::nvvm_div_rp_ftz_f:
  case Intrinsic::nvvm_div_rz_ftz_f:
    return true;

  case Intrinsic::nvvm_div_rm_f:
  case Intrinsic::nvvm_div_rn_f:
  case Intrinsic::nvvm_div_rp_f:
  case Intrinsic::nvvm_div_rz_f:
  case Intrinsic::nvvm_div_rm_d:
  case Intrinsic::nvvm_div_rn_d:
  case Intrinsic::nvvm_div_rp_d:
  case Intrinsic::nvvm_div_rz_d:
    return false;
  }
  llvm_unreachable("Checking FTZ flag for invalid NVVM div intrinsic");
}

/// Return the rounding mode of an NVVM floating-point div intrinsic.
/// \param IntrinsicID NVVM div intrinsic to query.
/// \return The \c APFloat rounding mode encoded by \p IntrinsicID.
inline APFloat::roundingMode GetFDivRoundingMode(Intrinsic::ID IntrinsicID) {
  switch (IntrinsicID) {
  case Intrinsic::nvvm_div_rm_f:
  case Intrinsic::nvvm_div_rm_d:
  case Intrinsic::nvvm_div_rm_ftz_f:
    return APFloat::rmTowardNegative;
  case Intrinsic::nvvm_div_rn_f:
  case Intrinsic::nvvm_div_rn_d:
  case Intrinsic::nvvm_div_rn_ftz_f:
    return APFloat::rmNearestTiesToEven;
  case Intrinsic::nvvm_div_rp_f:
  case Intrinsic::nvvm_div_rp_d:
  case Intrinsic::nvvm_div_rp_ftz_f:
    return APFloat::rmTowardPositive;
  case Intrinsic::nvvm_div_rz_f:
  case Intrinsic::nvvm_div_rz_d:
  case Intrinsic::nvvm_div_rz_ftz_f:
    return APFloat::rmTowardZero;
  }
  llvm_unreachable("Invalid FP instrinsic rounding mode for NVVM div");
}

/// Return true if the NVVM FMA intrinsic flushes denormals to zero.
/// \param IntrinsicID NVVM fma intrinsic to query.
/// \return True if \p IntrinsicID is an FTZ FMA variant; false otherwise.
inline bool FMAShouldFTZ(Intrinsic::ID IntrinsicID) {
  switch (IntrinsicID) {
  case Intrinsic::nvvm_fma_rm_ftz_f:
  case Intrinsic::nvvm_fma_rn_ftz_f:
  case Intrinsic::nvvm_fma_rp_ftz_f:
  case Intrinsic::nvvm_fma_rz_ftz_f:
    return true;

  case Intrinsic::nvvm_fma_rm_f:
  case Intrinsic::nvvm_fma_rn_f:
  case Intrinsic::nvvm_fma_rp_f:
  case Intrinsic::nvvm_fma_rz_f:
  case Intrinsic::nvvm_fma_rm_d:
  case Intrinsic::nvvm_fma_rn_d:
  case Intrinsic::nvvm_fma_rp_d:
  case Intrinsic::nvvm_fma_rz_d:
    return false;
  }
  llvm_unreachable("Checking FTZ flag for invalid NVVM fma intrinsic");
}

/// Return the rounding mode of an NVVM fused multiply-add intrinsic.
/// \param IntrinsicID NVVM fma intrinsic to query.
/// \return The \c APFloat rounding mode encoded by \p IntrinsicID.
inline APFloat::roundingMode GetFMARoundingMode(Intrinsic::ID IntrinsicID) {
  switch (IntrinsicID) {
  case Intrinsic::nvvm_fma_rm_f:
  case Intrinsic::nvvm_fma_rm_d:
  case Intrinsic::nvvm_fma_rm_ftz_f:
    return APFloat::rmTowardNegative;
  case Intrinsic::nvvm_fma_rn_f:
  case Intrinsic::nvvm_fma_rn_d:
  case Intrinsic::nvvm_fma_rn_ftz_f:
    return APFloat::rmNearestTiesToEven;
  case Intrinsic::nvvm_fma_rp_f:
  case Intrinsic::nvvm_fma_rp_d:
  case Intrinsic::nvvm_fma_rp_ftz_f:
    return APFloat::rmTowardPositive;
  case Intrinsic::nvvm_fma_rz_f:
  case Intrinsic::nvvm_fma_rz_d:
  case Intrinsic::nvvm_fma_rz_ftz_f:
    return APFloat::rmTowardZero;
  }
  llvm_unreachable("Invalid FP instrinsic rounding mode for NVVM fma");
}

} // namespace nvvm
} // namespace llvm
#endif // LLVM_IR_NVVMINTRINSICUTILS_H
