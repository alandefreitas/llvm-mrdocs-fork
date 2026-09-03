//== llvm/CodeGen/LowLevelTypeUtils.h -------------------------- -*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// Implement a low-level type suitable for MachineInstr level instruction
/// selection.
///
/// This provides the CodeGen aspects of LowLevelType, such as Type conversion.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_LOWLEVELTYPEUTILS_H
#define LLVM_CODEGEN_LOWLEVELTYPEUTILS_H

#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/CodeGenTypes/LowLevelType.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class DataLayout;
class Type;
struct fltSemantics;

/// Construct a low-level type based on an LLVM type.
///
/// \param Ty LLVM IR type to convert.
/// \param DL Data layout used for pointer sizes and type bit widths.
/// \return Low-level type corresponding to \p Ty.
LLVM_ABI LLT getLLTForType(Type &Ty, const DataLayout &DL);

/// Get a rough equivalent of an MVT for a given LLT. MVT can't distinguish
/// pointers, so these will convert to a plain integer.
///
/// \param Ty Low-level type to convert.
/// \return Machine value type roughly equivalent to \p Ty.
LLVM_ABI MVT getMVTForLLT(LLT Ty);

/// Get a rough equivalent of an EVT for a given LLT.
///
/// Always converts to a plain integer EVT of the same bit width.
///
/// \param Ty Low-level type to convert.
/// \param Ctx LLVM context used to create extended value types.
/// \return Integer EVT with the same bit width as \p Ty.
LLVM_ABI EVT getApproximateEVTForLLT(LLT Ty, LLVMContext &Ctx);

/// Get a rough equivalent of an LLT for a given MVT. LLT does not yet support
/// scalarable vector types, and will assert if used.
///
/// \param Ty Machine value type to convert.
/// \return Low-level type roughly equivalent to \p Ty.
LLVM_ABI LLT getLLTForMVT(MVT Ty);

/// Get the appropriate floating point arithmetic semantic based on the bit size
/// of the given scalar LLT.
///
/// \param Ty Scalar LLT whose bit size selects the floating-point semantics.
/// \return Floating-point semantics for the bit size of \p Ty.
LLVM_ABI const llvm::fltSemantics &getFltSemanticForLLT(LLT Ty);
} // namespace llvm

#endif // LLVM_CODEGEN_LOWLEVELTYPEUTILS_H
