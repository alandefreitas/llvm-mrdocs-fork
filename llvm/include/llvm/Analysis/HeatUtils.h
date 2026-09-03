//===-- HeatUtils.h - Utility for printing heat colors ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Utility for printing heat colors based on profiling information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_HEATUTILS_H
#define LLVM_ANALYSIS_HEATUTILS_H

#include "llvm/Support/Compiler.h"
#include <cstdint>
#include <string>

namespace llvm {

class BlockFrequencyInfo;
class Function;

/// Return the number of calls of \p CalledFunction by \p CallerFunction.
/// @param CallerFunction Function that may contain call sites.
/// @param CalledFunction Function whose call sites are counted.
/// @return Number of calls from \p CallerFunction to \p CalledFunction.
LLVM_ABI uint64_t getNumOfCalls(const Function &CallerFunction,
                                const Function &CalledFunction);

/// Return the maximum frequency of a basic block in a function.
/// @param F Function whose basic-block frequencies are examined.
/// @param BFI Block frequency info for \p F.
/// @return Maximum basic-block frequency in \p F.
LLVM_ABI uint64_t getMaxFreq(const Function &F, const BlockFrequencyInfo *BFI);

/// Calculate a heat color from a frequency relative to a maximum.
/// @param Freq Current frequency value.
/// @param MaxFreq Maximum frequency used for normalization.
/// @return Heat color string corresponding to \p Freq / \p MaxFreq.
LLVM_ABI std::string getHeatColor(uint64_t Freq, uint64_t MaxFreq);

/// Calculate a heat color from a percent of hotness.
/// @param Percent Hotness percent in the range [0.0, 1.0].
/// @return Heat color string corresponding to \p Percent.
LLVM_ABI std::string getHeatColor(double Percent);

} // namespace llvm

#endif
