//===- ASanStackFrameLayout.h - ComputeASanStackFrameLayout -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header defines ComputeASanStackFrameLayout and auxiliary data structs.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TRANSFORMS_UTILS_ASANSTACKFRAMELAYOUT_H
#define LLVM_TRANSFORMS_UTILS_ASANSTACKFRAMELAYOUT_H
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class AllocaInst;

// These magic constants should be the same as in
// in asan_internal.h from ASan runtime in compiler-rt.
static const int kAsanStackLeftRedzoneMagic = 0xf1;
static const int kAsanStackMidRedzoneMagic = 0xf2;
static const int kAsanStackRightRedzoneMagic = 0xf3;
static const int kAsanStackUseAfterReturnMagic = 0xf5;
static const int kAsanStackUseAfterScopeMagic = 0xf8;

/// Describes one stack variable for AddressSanitizer frame layout.
///
/// Input/output data struct for ComputeASanStackFrameLayout.
struct ASanStackVariableDescription {
  /// Name of the variable displayed in AddressSanitizer reports.
  const char *Name;
  /// Size of the variable in bytes.
  uint64_t Size;
  /// Size in bytes used for lifetime analysis.
  ///
  /// Will be rounded up to Granularity.
  size_t LifetimeSize;
  /// Alignment of the variable, which must be a power of two.
  uint64_t Alignment;
  /// Alloca instruction that allocates this variable.
  AllocaInst *AI;
  /// Offset from the beginning of the frame.
  ///
  /// Set by ComputeASanStackFrameLayout.
  size_t Offset;
  /// Source line number of the variable.
  unsigned Line;
};

/// Computed AddressSanitizer layout of an instrumented stack frame.
///
/// Output data struct for ComputeASanStackFrameLayout.
struct ASanStackFrameLayout {
  /// Shadow granularity in bytes.
  uint64_t Granularity;
  /// Alignment of the entire stack frame.
  uint64_t FrameAlignment;
  /// Size of the frame in bytes.
  uint64_t FrameSize;
};

/// Compute the AddressSanitizer layout of a stack frame.
///
/// Reorders \p Vars by alignment and writes each variable's \c Offset. The
/// resulting \c FrameSize is a multiple of \p MinHeaderSize.
/// @param Vars Stack variables to place; may be reordered and updated.
/// @param Granularity AddressSanitizer shadow granularity (usually 8; also
/// 16, 32, or 64).
/// @param MinHeaderSize Minimal size of the left-most redzone (header). Must
/// be a power of two, at least four pointer sizes, and >= \p Granularity.
/// @return Layout with granularity, frame alignment, and frame size.
LLVM_ABI ASanStackFrameLayout ComputeASanStackFrameLayout(
    SmallVectorImpl<ASanStackVariableDescription> &Vars, uint64_t Granularity,
    uint64_t MinHeaderSize);

/// Compute a frame description string for AddressSanitizer reports.
///
/// See DescribeAddressIfStack in the ASan runtime.
/// @param Vars Stack variables after layout has been computed.
/// @return Compact description consumed by the AddressSanitizer runtime.
LLVM_ABI SmallString<64> ComputeASanStackFrameDescription(
    const SmallVectorImpl<ASanStackVariableDescription> &Vars);

/// Return shadow bytes for a stack frame with all locals in scope.
///
/// Redzones are marked with AddressSanitizer magic values. This shadow
/// represents the state of the stack frame when all local variables are inside
/// their own scope.
/// @param Vars Stack variables after layout has been computed.
/// @param Layout Computed frame layout that supplies granularity and size.
/// @return Shadow bytes with redzones marked and in-scope locals as valid.
LLVM_ABI SmallVector<uint8_t, 64>
GetShadowBytes(const SmallVectorImpl<ASanStackVariableDescription> &Vars,
               const ASanStackFrameLayout &Layout);

/// Return shadow bytes after all locals have gone out of scope.
///
/// Redzones are marked, and each variable's lifetime region is filled with
/// use-after-scope magic. This shadow represents the state of the stack frame
/// when all local variables are outside their own scope.
/// @param Vars Stack variables after layout has been computed.
/// @param Layout Computed frame layout that supplies granularity and size.
/// @return Shadow bytes with redzones and use-after-scope bytes marked.
LLVM_ABI SmallVector<uint8_t, 64> GetShadowBytesAfterScope(
    const SmallVectorImpl<ASanStackVariableDescription> &Vars,
    const ASanStackFrameLayout &Layout);

} // llvm namespace

#endif  // LLVM_TRANSFORMS_UTILS_ASANSTACKFRAMELAYOUT_H
