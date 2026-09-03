//===--------- Definition of the AddressSanitizer class ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares common infrastructure for AddressSanitizer and
// HWAddressSanitizer.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_ADDRESSSANITIZERCOMMON_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_ADDRESSSANITIZERCOMMON_H

#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/InterestingMemoryOperand.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
/// Fill AddressSanitizer shadow-mapping parameters for a target.
/// @param TargetTriple Target triple that selects the shadow mapping.
/// @param LongSize Pointer size in bits (32 or 64).
/// @param IsKasan Whether to use KernelAddressSanitizer mapping.
/// @param ShadowBase Filled with the shadow base offset.
/// @param MappingScale Filled with the shadow mapping scale.
/// @param OrShadowOffset Filled with whether the shadow offset is applied
/// with OR rather than ADD.
LLVM_ABI void getAddressSanitizerParams(const Triple &TargetTriple,
                                        int LongSize, bool IsKasan,
                                        uint64_t *ShadowBase, int *MappingScale,
                                        bool *OrShadowOffset);

/// Remove memory attributes incompatible with ASan/HWASan instrumentation.
///
/// Sanitizer checks invalidate previously inferred memory attributes on
/// instrumented or intercepted functions. When \p ReadsArgMem is true,
/// argument memory may be read by instrumentation and `writeonly`
/// attributes must be removed.
/// @param F Function whose memory attributes may be updated.
/// @param ReadsArgMem Whether instrumentation may read argument memory.
LLVM_ABI void removeASanIncompatibleFnAttributes(Function &F, bool ReadsArgMem);

} // namespace llvm

#endif
