//===- VNCoercion.h - Value Numbering Coercion Utilities --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file / This file provides routines used by LLVM's value numbering passes to
/// perform various forms of value extraction from memory when the types are not
/// identical.  For example, given
///
/// store i32 8, i32 *%foo
/// %a = bitcast i32 *%foo to i16
/// %val = load i16, i16 *%a
///
/// It possible to extract the value of the load of %a from the store to %foo.
/// These routines know how to tell whether they can do that (the analyze*
/// routines), and can also insert the necessary IR to do it (the get*
/// routines).
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_VNCOERCION_H
#define LLVM_TRANSFORMS_UTILS_VNCOERCION_H

#include "llvm/Support/Compiler.h"

namespace llvm {
class Constant;
class Function;
class StoreInst;
class LoadInst;
class MemIntrinsic;
class Instruction;
class IRBuilderBase;
class Value;
class Type;
class DataLayout;

/// Utilities for coercing values across mismatched memory types in VN.
namespace VNCoercion {
/// Return true if CoerceAvailableValueToLoadType would succeed if it was
/// called.
///
/// \param StoredVal Value that was stored to memory.
/// \param LoadTy Type of the load that would consume the coerced value.
/// \param F Function providing context for the coercion.
/// \return True if coerceAvailableValueToLoadType would succeed.
LLVM_ABI bool canCoerceMustAliasedValueToLoad(Value *StoredVal, Type *LoadTy,
                                              Function *F);

/// Try to coerce a stored value to a must-aliased load of a different type.
///
/// If we saw a store of a value to memory, and then a load from a must-aliased
/// pointer of a different type, try to coerce the stored value to the loaded
/// type.  LoadedTy is the type of the load we want to replace.  IRB is
/// IRBuilder used to insert new instructions.
///
/// If we can't do it, return null.
///
/// \param StoredVal Value previously stored to a must-aliased location.
/// \param LoadedTy Type of the load that should replace uses of the store.
/// \param IRB Builder used to insert any new coercion instructions.
/// \param F Function providing context for the coercion.
/// \return The coerced value of type \p LoadedTy, or null if coercion fails.
LLVM_ABI Value *coerceAvailableValueToLoadType(Value *StoredVal, Type *LoadedTy,
                                               IRBuilderBase &IRB, Function *F);

/// This function determines whether a value for the pointer LoadPtr can be
/// extracted from the store at DepSI.
///
/// On success, it returns the offset into DepSI that extraction would start.
/// On failure, it returns -1.
///
/// \param LoadTy Type of the load being analyzed.
/// \param LoadPtr Pointer operand of the load.
/// \param DepSI Store that may clobber the loaded location.
/// \param DL Data layout used for size and offset calculations.
/// \return Offset into \p DepSI at which extraction would start, or -1 on
/// failure.
LLVM_ABI int analyzeLoadFromClobberingStore(Type *LoadTy, Value *LoadPtr,
                                            StoreInst *DepSI,
                                            const DataLayout &DL);

/// This function determines whether a value for the pointer LoadPtr can be
/// extracted from the load at DepLI.
///
/// On success, it returns the offset into DepLI that extraction would start.
/// On failure, it returns -1.
///
/// \param LoadTy Type of the load being analyzed.
/// \param LoadPtr Pointer operand of the load.
/// \param DepLI Load that may clobber the loaded location.
/// \param DL Data layout used for size and offset calculations.
/// \return Offset into \p DepLI at which extraction would start, or -1 on
/// failure.
LLVM_ABI int analyzeLoadFromClobberingLoad(Type *LoadTy, Value *LoadPtr,
                                           LoadInst *DepLI,
                                           const DataLayout &DL);

/// This function determines whether a value for the pointer LoadPtr can be
/// extracted from the memory intrinsic at DepMI.
///
/// On success, it returns the offset into DepMI that extraction would start.
/// On failure, it returns -1.
///
/// \param LoadTy Type of the load being analyzed.
/// \param LoadPtr Pointer operand of the load.
/// \param DepMI Memory intrinsic that may clobber the loaded location.
/// \param DL Data layout used for size and offset calculations.
/// \return Offset into \p DepMI at which extraction would start, or -1 on
/// failure.
LLVM_ABI int analyzeLoadFromClobberingMemInst(Type *LoadTy, Value *LoadPtr,
                                              MemIntrinsic *DepMI,
                                              const DataLayout &DL);

/// Extract bits for a load from a clobbering store or load at a given offset.
///
/// If analyzeLoadFromClobberingStore/Load returned an offset, this function
/// can be used to actually perform the extraction of the bits from the store.
/// It inserts instructions to do so at InsertPt, and returns the extracted
/// value.
///
/// \param SrcVal Stored or loaded value from which bits are extracted.
/// \param Offset Byte offset into \p SrcVal at which extraction starts.
/// \param LoadTy Type of the load being materialized.
/// \param InsertPt Instruction before which extraction IR is inserted.
/// \param F Function providing context for the extraction.
/// \return The extracted value inserted at \p InsertPt.
LLVM_ABI Value *getValueForLoad(Value *SrcVal, unsigned Offset, Type *LoadTy,
                                Instruction *InsertPt, Function *F);
/// Extract a constant load value from a constant store or load at an offset.
///
/// This is the same as getValueForLoad, except it performs no insertion.
/// It only allows constant inputs.
///
/// \param SrcVal Constant value from which bits are extracted.
/// \param Offset Byte offset into \p SrcVal at which extraction starts.
/// \param LoadTy Type of the load being materialized.
/// \param DL Data layout used for size and offset calculations.
/// \return The extracted constant value of type \p LoadTy.
LLVM_ABI Constant *getConstantValueForLoad(Constant *SrcVal, unsigned Offset,
                                           Type *LoadTy, const DataLayout &DL);

/// Extract bits for a load from a clobbering memory intrinsic at an offset.
///
/// If analyzeLoadFromClobberingMemInst returned an offset, this function can be
/// used to actually perform the extraction of the bits from the memory
/// intrinsic.  It inserts instructions to do so at InsertPt, and returns the
/// extracted value.
///
/// \param SrcInst Memory intrinsic from which bits are extracted.
/// \param Offset Byte offset into \p SrcInst at which extraction starts.
/// \param LoadTy Type of the load being materialized.
/// \param InsertPt Instruction before which extraction IR is inserted.
/// \param DL Data layout used for size and offset calculations.
/// \return The extracted value inserted at \p InsertPt.
LLVM_ABI Value *getMemInstValueForLoad(MemIntrinsic *SrcInst, unsigned Offset,
                                       Type *LoadTy, Instruction *InsertPt,
                                       const DataLayout &DL);
/// Extract a constant load value from a memory intrinsic at a given offset.
///
/// This is the same as getMemInstValueForLoad, except it performs no insertion.
/// It returns nullptr if it cannot produce a constant.
///
/// \param SrcInst Memory intrinsic from which bits are extracted.
/// \param Offset Byte offset into \p SrcInst at which extraction starts.
/// \param LoadTy Type of the load being materialized.
/// \param DL Data layout used for size and offset calculations.
/// \return The extracted constant value, or nullptr if it cannot be produced.
LLVM_ABI Constant *getConstantMemInstValueForLoad(MemIntrinsic *SrcInst,
                                                  unsigned Offset, Type *LoadTy,
                                                  const DataLayout &DL);
} // namespace VNCoercion
} // namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_VNCOERCION_H
