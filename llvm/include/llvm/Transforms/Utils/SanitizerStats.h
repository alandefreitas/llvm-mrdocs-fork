//===- SanitizerStats.h - Sanitizer statistics gathering  -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Declares functions and data structures for sanitizer statistics gathering.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_SANITIZERSTATS_H
#define LLVM_TRANSFORMS_UTILS_SANITIZERSTATS_H

#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

/// Number of bits reserved to encode a sanitizer statistic kind.
///
/// Needs to match \c __sanitizer::kKindBits in
/// compiler-rt/lib/stats/stats.h.
enum {
  kSanitizerStatKindBits = 3 ///< Bit width of the sanitizer-kind tag field.
};

/// Kinds of control-flow integrity checks counted by sanitizer stats.
enum SanitizerStatKind {
  SanStat_CFI_VCall,         ///< CFI check on a virtual call.
  SanStat_CFI_NVCall,        ///< CFI check on a non-virtual member call.
  SanStat_CFI_DerivedCast,   ///< CFI check on a cast to a derived type.
  SanStat_CFI_UnrelatedCast, ///< CFI check on a cast to an unrelated type.
  SanStat_CFI_ICall,         ///< CFI check on an indirect call.
};

/// Emits and finalizes per-module sanitizer statistic counters.
struct SanitizerStatReport {
  /// Construct a sanitizer statistics report for module \p M.
  /// @param M Module that will own the generated statistics globals.
  LLVM_ABI SanitizerStatReport(Module *M);

  /// Generates code into B that increments a location-specific counter tagged
  /// with the given sanitizer kind SK.
  /// @param B IR builder used to emit the counter increment.
  /// @param SK Sanitizer statistic kind that tags this counter.
  LLVM_ABI void create(IRBuilder<> &B, SanitizerStatKind SK);

  /// Finalize module stats array and add global constructor to register it.
  LLVM_ABI void finish();

private:
  Module *M;
  GlobalVariable *ModuleStatsGV;
  ArrayType *StatTy;
  StructType *EmptyModuleStatsTy;

  std::vector<Constant *> Inits;
  ArrayType *makeModuleStatsArrayTy();
  StructType *makeModuleStatsTy();
};

}

#endif
