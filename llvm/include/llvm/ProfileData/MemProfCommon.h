//===- MemProfCommon.h - MemProf common utilities ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains MemProf common utilities.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_PROFILEDATA_MEMPROFCOMMON_H
#define LLVM_PROFILEDATA_MEMPROFCOMMON_H

#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
namespace memprof {

struct Frame;

/// Return the allocation type for a given set of memory profile values.
/// @param TotalLifetimeAccessDensity Sum of lifetime access densities (scaled
///        by 100 for two decimal places of precision).
/// @param AllocCount Number of allocations in the profile.
/// @param TotalLifetime Sum of allocation lifetimes, in milliseconds.
/// @return Hot, Cold, or NotCold based on average density and lifetime.
LLVM_ABI AllocationType getAllocType(uint64_t TotalLifetimeAccessDensity,
                                     uint64_t AllocCount,
                                     uint64_t TotalLifetime);

/// Generate a single hash id for a given callstack.
///
/// Used for emitting matching statistics and useful for uniquing such
/// statistics across modules. Also used to dedup contexts when computing the
/// summary.
/// @param CallStack Frames that form the callstack to hash.
/// @return 64-bit hash uniquely identifying the callstack.
LLVM_ABI uint64_t computeFullStackId(ArrayRef<Frame> CallStack);

} // namespace memprof
} // namespace llvm

#endif // LLVM_PROFILEDATA_MEMPROFCOMMON_H
