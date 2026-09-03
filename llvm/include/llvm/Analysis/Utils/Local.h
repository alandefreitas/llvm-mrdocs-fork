//===- Local.h - Functions to perform local transformations -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This family of functions perform various local transformations to the
// program.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_UTILS_LOCAL_H
#define LLVM_ANALYSIS_UTILS_LOCAL_H

#include "llvm/Support/Compiler.h"

namespace llvm {

class DataLayout;
class IRBuilderBase;
class User;
class Value;

/// Emit code to compute a GEP's offset from its base pointer.
///
/// Given a getelementptr instruction or constant expression, emits the code
/// necessary to compute the offset from the base pointer (without adding in
/// the base pointer). Returns the result as a signed integer of intptr size.
/// When \p NoAssumptions is true, no assumptions about index computation not
/// overflowing are made.
/// \param Builder IR builder used to emit the offset computation.
/// \param DL Data layout used for pointer and type sizes.
/// \param GEP GetElementPtr instruction or constant expression to measure.
/// \param NoAssumptions If true, do not assume index computations do not
/// overflow.
/// \return The offset as a signed integer of intptr size.
LLVM_ABI Value *emitGEPOffset(IRBuilderBase *Builder, const DataLayout &DL,
                              User *GEP, bool NoAssumptions = false);

} // namespace llvm

#endif // LLVM_ANALYSIS_UTILS_LOCAL_H
