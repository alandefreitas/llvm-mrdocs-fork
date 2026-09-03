//===- BuildLibCalls.h - Utility builder for libcalls -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file exposes an interface to build some C language libcalls for
// optimization passes that need to call the various functions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_BUILDLIBCALLS_H
#define LLVM_TRANSFORMS_UTILS_BUILDLIBCALLS_H

#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
  class Value;
  class DataLayout;
  class IRBuilderBase;

  /// Infer and set non-mandatory attributes on a library function by name.
  ///
  /// Analyze the name and prototype of the given function and set any
  /// applicable attributes. Note that this merely helps optimizations on an
  /// already existing function but does not consider mandatory attributes.
  ///
  /// If the library function is unavailable, this doesn't modify it.
  ///
  /// Returns true if any attributes were set and false otherwise.
  ///
  /// \param M Module containing the function.
  /// \param Name Name of the library function to analyze.
  /// \param TLI Target library info describing available libcalls.
  /// \return True if any attributes were set, false otherwise.
  LLVM_ABI bool inferNonMandatoryLibFuncAttrs(Module *M, StringRef Name,
                                              const TargetLibraryInfo &TLI);

  /// Infer and set non-mandatory attributes on an existing function.
  ///
  /// Analyze the name and prototype of the given function and set any
  /// applicable attributes. Note that this merely helps optimizations on an
  /// already existing function but does not consider mandatory attributes.
  ///
  /// If the library function is unavailable, this doesn't modify it.
  ///
  /// Returns true if any attributes were set and false otherwise.
  ///
  /// \param F Function whose attributes may be updated.
  /// \param TLI Target library info describing available libcalls.
  /// \return True if any attributes were set, false otherwise.
  LLVM_ABI bool inferNonMandatoryLibFuncAttrs(Function &F,
                                              const TargetLibraryInfo &TLI);

  /// Get or insert a library function, applying mandatory argument attributes.
  ///
  /// Calls getOrInsertFunction() and then makes sure to add mandatory
  /// argument attributes.
  ///
  /// \param M Module in which to look up or insert the function.
  /// \param TLI Target library info describing available libcalls.
  /// \param TheLibFunc Library function identifier to emit.
  /// \param T Function type of the libcall.
  /// \param AttributeList Attributes to attach to the inserted function.
  /// \return The FunctionCallee for the looked-up or inserted libcall.
  LLVM_ABI FunctionCallee getOrInsertLibFunc(Module *M,
                                             const TargetLibraryInfo &TLI,
                                             LibFunc TheLibFunc,
                                             FunctionType *T,
                                             AttributeList AttributeList);

  /// Get or insert a library function with the given type and no extra attrs.
  ///
  /// \param M Module in which to look up or insert the function.
  /// \param TLI Target library info describing available libcalls.
  /// \param TheLibFunc Library function identifier to emit.
  /// \param T Function type of the libcall.
  /// \return The FunctionCallee for the looked-up or inserted libcall.
  LLVM_ABI FunctionCallee getOrInsertLibFunc(Module *M,
                                             const TargetLibraryInfo &TLI,
                                             LibFunc TheLibFunc,
                                             FunctionType *T);

  /// Get or insert a library function built from a return type and arg types.
  ///
  /// \param M Module in which to look up or insert the function.
  /// \param TLI Target library info describing available libcalls.
  /// \param TheLibFunc Library function identifier to emit.
  /// \param AttributeList Attributes to attach to the inserted function.
  /// \param RetTy Return type of the libcall.
  /// \param Args Parameter types of the libcall.
  /// \return The FunctionCallee for the looked-up or inserted libcall.
  template <typename... ArgsTy>
  FunctionCallee getOrInsertLibFunc(Module *M, const TargetLibraryInfo &TLI,
                               LibFunc TheLibFunc, AttributeList AttributeList,
                               Type *RetTy, ArgsTy... Args) {
    SmallVector<Type*, sizeof...(ArgsTy)> ArgTys{Args...};
    return getOrInsertLibFunc(M, TLI, TheLibFunc,
                              FunctionType::get(RetTy, ArgTys, false),
                              AttributeList);
  }

  /// Get or insert a library function without an explicit attribute list.
  ///
  /// Same as the ret/arg-type overload above, but without the attributes.
  ///
  /// \param M Module in which to look up or insert the function.
  /// \param TLI Target library info describing available libcalls.
  /// \param TheLibFunc Library function identifier to emit.
  /// \param RetTy Return type of the libcall.
  /// \param Args Parameter types of the libcall.
  /// \return The FunctionCallee for the looked-up or inserted libcall.
  template <typename... ArgsTy>
  FunctionCallee getOrInsertLibFunc(Module *M, const TargetLibraryInfo &TLI,
                             LibFunc TheLibFunc, Type *RetTy, ArgsTy... Args) {
    return getOrInsertLibFunc(M, TLI, TheLibFunc, AttributeList{}, RetTy,
                              Args...);
  }

  /// Deleted overload that would otherwise allow an incorrect argument order.
  ///
  /// Avoid an incorrect ordering that'd otherwise compile incorrectly.
  ///
  /// \param M Module in which to look up or insert the function.
  /// \param TLI Target library info describing available libcalls.
  /// \param TheLibFunc Library function identifier to emit.
  /// \param AttributeList Attributes that would have been attached.
  /// \param Invalid Mistaken FunctionType* that must not be accepted.
  /// \param Args Remaining argument types that would follow \p Invalid.
  template <typename... ArgsTy>
  FunctionCallee
  getOrInsertLibFunc(Module *M, const TargetLibraryInfo &TLI,
                     LibFunc TheLibFunc, AttributeList AttributeList,
                     FunctionType *Invalid, ArgsTy... Args) = delete;

  /// Apply -mregparm-style register parameter attributes to a function.
  ///
  /// Handle -mregparm for the given function.
  /// Note that this function is a rough approximation that only works for simple
  /// function signatures; it does not apply other relevant attributes for
  /// function signatures, including sign/zero-extension for arguments and return
  /// values.
  ///
  /// \param F Function whose register-parameter attributes may be set.
  LLVM_ABI void markRegisterParameterAttributes(Function *F);

  /// Return true if the library function is available and correctly typed.
  ///
  /// Check whether the library function is available on target and also that
  /// it in the current Module is a Function with the right type.
  ///
  /// \param M Module in which the function may already be declared.
  /// \param TLI Target library info describing available libcalls.
  /// \param TheLibFunc Library function identifier to check.
  /// \return True if the libcall is available and correctly typed.
  LLVM_ABI bool isLibFuncEmittable(const Module *M,
                                   const TargetLibraryInfo *TLI,
                                   LibFunc TheLibFunc);

  /// Return true if a named library function is available and correctly typed.
  ///
  /// \param M Module in which the function may already be declared.
  /// \param TLI Target library info describing available libcalls.
  /// \param Name Name of the library function to check.
  /// \return True if the libcall is available and correctly typed.
  LLVM_ABI bool isLibFuncEmittable(const Module *M,
                                   const TargetLibraryInfo *TLI,
                                   StringRef Name);

  /// Check whether the overloaded floating point function corresponding to Ty is available.
  ///
  /// \param M Module in which the function may already be declared.
  /// \param TLI Target library info describing available libcalls.
  /// \param Ty Floating-point type selecting double/float/long double.
  /// \param DoubleFn LibFunc for the double variant.
  /// \param FloatFn LibFunc for the float variant.
  /// \param LongDoubleFn LibFunc for the long double variant.
  /// \return True if the matching float libcall is available and typed.
  LLVM_ABI bool hasFloatFn(const Module *M, const TargetLibraryInfo *TLI,
                           Type *Ty, LibFunc DoubleFn, LibFunc FloatFn,
                           LibFunc LongDoubleFn);

  /// Get the name of the overloaded floating point function corresponding to Ty.
  ///
  /// Return the LibFunc in \a TheLibFunc.
  ///
  /// \param M Module in which the function may already be declared.
  /// \param TLI Target library info describing available libcalls.
  /// \param Ty Floating-point type selecting double/float/long double.
  /// \param DoubleFn LibFunc for the double variant.
  /// \param FloatFn LibFunc for the float variant.
  /// \param LongDoubleFn LibFunc for the long double variant.
  /// \param TheLibFunc Set to the selected LibFunc on success.
  /// \return The name of the selected floating-point libcall.
  LLVM_ABI StringRef getFloatFn(const Module *M, const TargetLibraryInfo *TLI,
                                Type *Ty, LibFunc DoubleFn, LibFunc FloatFn,
                                LibFunc LongDoubleFn, LibFunc &TheLibFunc);

  /// Emit a call to strlen for the specified pointer.
  ///
  /// Ptr is required to be some pointer type, and the return value has
  /// 'size_t' type.
  ///
  /// \param Ptr Pointer to the C string whose length is computed.
  /// \param B IR builder used to create the call.
  /// \param DL Data layout used for size_t typing.
  /// \param TLI Target library info describing available libcalls.
  /// \return The strlen call, or null if unavailable.
  LLVM_ABI Value *emitStrLen(Value *Ptr, IRBuilderBase &B, const DataLayout &DL,
                             const TargetLibraryInfo *TLI);

  /// Emit a call to wcslen for the specified pointer.
  ///
  /// Ptr is required to be some pointer type, and the return value has
  /// 'size_t' type.
  ///
  /// \param Ptr Pointer to the wide string whose length is computed.
  /// \param B IR builder used to create the call.
  /// \param DL Data layout used for size_t typing.
  /// \param TLI Target library info describing available libcalls.
  /// \return The wcslen call, or null if unavailable.
  LLVM_ABI Value *emitWcsLen(Value *Ptr, IRBuilderBase &B, const DataLayout &DL,
                             const TargetLibraryInfo *TLI);

  /// Emit a call to strdup for the specified pointer.
  ///
  /// Ptr is required to be some pointer type, and the return value has
  /// 'i8*' type.
  ///
  /// \param Ptr Pointer to the C string to duplicate.
  /// \param B IR builder used to create the call.
  /// \param TLI Target library info describing available libcalls.
  /// \return The strdup call, or null if unavailable.
  LLVM_ABI Value *emitStrDup(Value *Ptr, IRBuilderBase &B,
                             const TargetLibraryInfo *TLI);

  /// Emit a call to strchr for the specified pointer and character.
  ///
  /// Ptr is required to be some pointer type, and the return value has 'i8*'
  /// type.
  ///
  /// \param Ptr Pointer to the C string to search.
  /// \param C Character value to search for.
  /// \param B IR builder used to create the call.
  /// \param TLI Target library info describing available libcalls.
  /// \return The strchr call, or null if unavailable.
  LLVM_ABI Value *emitStrChr(Value *Ptr, char C, IRBuilderBase &B,
                             const TargetLibraryInfo *TLI);

  /// Emit a call to the strncmp function to the builder.
  ///
  /// \param Ptr1 First string pointer to compare.
  /// \param Ptr2 Second string pointer to compare.
  /// \param Len Maximum number of characters to compare.
  /// \param B IR builder used to create the call.
  /// \param DL Data layout used for size typing.
  /// \param TLI Target library info describing available libcalls.
  /// \return The strncmp call, or null if unavailable.
  LLVM_ABI Value *emitStrNCmp(Value *Ptr1, Value *Ptr2, Value *Len,
                              IRBuilderBase &B, const DataLayout &DL,
                              const TargetLibraryInfo *TLI);

  /// Emit a call to strcpy for the specified pointer arguments.
  ///
  /// \param Dst Destination buffer pointer.
  /// \param Src Source C string pointer.
  /// \param B IR builder used to create the call.
  /// \param TLI Target library info describing available libcalls.
  /// \return The strcpy call, or null if unavailable.
  LLVM_ABI Value *emitStrCpy(Value *Dst, Value *Src, IRBuilderBase &B,
                             const TargetLibraryInfo *TLI);

  /// Emit a call to stpcpy for the specified pointer arguments.
  ///
  /// \param Dst Destination buffer pointer.
  /// \param Src Source C string pointer.
  /// \param B IR builder used to create the call.
  /// \param TLI Target library info describing available libcalls.
  /// \return The stpcpy call, or null if unavailable.
  LLVM_ABI Value *emitStpCpy(Value *Dst, Value *Src, IRBuilderBase &B,
                             const TargetLibraryInfo *TLI);

  /// Emit a call to strncpy for the specified pointer arguments and length.
  ///
  /// \param Dst Destination buffer pointer.
  /// \param Src Source C string pointer.
  /// \param Len Maximum number of characters to copy.
  /// \param B IR builder used to create the call.
  /// \param TLI Target library info describing available libcalls.
  /// \return The strncpy call, or null if unavailable.
  LLVM_ABI Value *emitStrNCpy(Value *Dst, Value *Src, Value *Len,
                              IRBuilderBase &B, const TargetLibraryInfo *TLI);

  /// Emit a call to stpncpy for the specified pointer arguments and length.
  ///
  /// \param Dst Destination buffer pointer.
  /// \param Src Source C string pointer.
  /// \param Len Maximum number of characters to copy.
  /// \param B IR builder used to create the call.
  /// \param TLI Target library info describing available libcalls.
  /// \return The stpncpy call, or null if unavailable.
  LLVM_ABI Value *emitStpNCpy(Value *Dst, Value *Src, Value *Len,
                              IRBuilderBase &B, const TargetLibraryInfo *TLI);

  /// Emit a call to __memcpy_chk with size_t Len/ObjSize and pointer Dst/Src.
  ///
  /// This expects that the Len and ObjSize have type 'size_t' and Dst/Src are
  /// pointers.
  ///
  /// \param Dst Destination memory pointer.
  /// \param Src Source memory pointer.
  /// \param Len Number of bytes to copy.
  /// \param ObjSize Size of the destination object for checking.
  /// \param B IR builder used to create the call.
  /// \param DL Data layout used for size_t typing.
  /// \param TLI Target library info describing available libcalls.
  /// \return The __memcpy_chk call, or null if unavailable.
  LLVM_ABI Value *emitMemCpyChk(Value *Dst, Value *Src, Value *Len,
                                Value *ObjSize, IRBuilderBase &B,
                                const DataLayout &DL,
                                const TargetLibraryInfo *TLI);

  /// Emit a call to the mempcpy function.
  ///
  /// \param Dst Destination memory pointer.
  /// \param Src Source memory pointer.
  /// \param Len Number of bytes to copy.
  /// \param B IR builder used to create the call.
  /// \param DL Data layout used for size typing.
  /// \param TLI Target library info describing available libcalls.
  /// \return The mempcpy call, or null if unavailable.
  LLVM_ABI Value *emitMemPCpy(Value *Dst, Value *Src, Value *Len,
                              IRBuilderBase &B, const DataLayout &DL,
                              const TargetLibraryInfo *TLI);

  /// Emit a call to memchr with a pointer, int Val, and size_t Len.
  ///
  /// This assumes that Ptr is a pointer, Val is an 'int' value, and Len is an
  /// 'size_t' value.
  ///
  /// \param Ptr Memory region to search.
  /// \param Val Byte value to search for.
  /// \param Len Number of bytes to examine.
  /// \param B IR builder used to create the call.
  /// \param DL Data layout used for size_t typing.
  /// \param TLI Target library info describing available libcalls.
  /// \return The memchr call, or null if unavailable.
  LLVM_ABI Value *emitMemChr(Value *Ptr, Value *Val, Value *Len,
                             IRBuilderBase &B, const DataLayout &DL,
                             const TargetLibraryInfo *TLI);

  /// Emit a call to the memrchr function, analogously to emitMemChr.
  ///
  /// \param Ptr Memory region to search.
  /// \param Val Byte value to search for.
  /// \param Len Number of bytes to examine.
  /// \param B IR builder used to create the call.
  /// \param DL Data layout used for size_t typing.
  /// \param TLI Target library info describing available libcalls.
  /// \return The memrchr call, or null if unavailable.
  LLVM_ABI Value *emitMemRChr(Value *Ptr, Value *Val, Value *Len,
                              IRBuilderBase &B, const DataLayout &DL,
                              const TargetLibraryInfo *TLI);

  /// Emit a call to the memcmp function.
  ///
  /// \param Ptr1 First memory region to compare.
  /// \param Ptr2 Second memory region to compare.
  /// \param Len Number of bytes to compare.
  /// \param B IR builder used to create the call.
  /// \param DL Data layout used for size typing.
  /// \param TLI Target library info describing available libcalls.
  /// \return The memcmp call, or null if unavailable.
  LLVM_ABI Value *emitMemCmp(Value *Ptr1, Value *Ptr2, Value *Len,
                             IRBuilderBase &B, const DataLayout &DL,
                             const TargetLibraryInfo *TLI);

  /// Emit a call to the bcmp function.
  ///
  /// \param Ptr1 First memory region to compare.
  /// \param Ptr2 Second memory region to compare.
  /// \param Len Number of bytes to compare.
  /// \param B IR builder used to create the call.
  /// \param DL Data layout used for size typing.
  /// \param TLI Target library info describing available libcalls.
  /// \return The bcmp call, or null if unavailable.
  LLVM_ABI Value *emitBCmp(Value *Ptr1, Value *Ptr2, Value *Len,
                           IRBuilderBase &B, const DataLayout &DL,
                           const TargetLibraryInfo *TLI);

  /// Emit a call to the memccpy function.
  ///
  /// \param Ptr1 Destination memory pointer.
  /// \param Ptr2 Source memory pointer.
  /// \param Val Stop character value.
  /// \param Len Maximum number of bytes to copy.
  /// \param B IR builder used to create the call.
  /// \param TLI Target library info describing available libcalls.
  /// \return The memccpy call, or null if unavailable.
  LLVM_ABI Value *emitMemCCpy(Value *Ptr1, Value *Ptr2, Value *Val, Value *Len,
                              IRBuilderBase &B, const TargetLibraryInfo *TLI);

  /// Emit a call to the snprintf function.
  ///
  /// \param Dest Destination buffer pointer.
  /// \param Size Size of the destination buffer.
  /// \param Fmt Format string pointer.
  /// \param Args Additional variadic format arguments.
  /// \param B IR builder used to create the call.
  /// \param TLI Target library info describing available libcalls.
  /// \return The snprintf call, or null if unavailable.
  LLVM_ABI Value *emitSNPrintf(Value *Dest, Value *Size, Value *Fmt,
                               ArrayRef<Value *> Args, IRBuilderBase &B,
                               const TargetLibraryInfo *TLI);

  /// Emit a call to the sprintf function.
  ///
  /// \param Dest Destination buffer pointer.
  /// \param Fmt Format string pointer.
  /// \param VariadicArgs Additional variadic format arguments.
  /// \param B IR builder used to create the call.
  /// \param TLI Target library info describing available libcalls.
  /// \return The sprintf call, or null if unavailable.
  LLVM_ABI Value *emitSPrintf(Value *Dest, Value *Fmt,
                              ArrayRef<Value *> VariadicArgs, IRBuilderBase &B,
                              const TargetLibraryInfo *TLI);

  /// Emit a call to the strcat function.
  ///
  /// \param Dest Destination C string pointer.
  /// \param Src Source C string pointer to append.
  /// \param B IR builder used to create the call.
  /// \param TLI Target library info describing available libcalls.
  /// \return The strcat call, or null if unavailable.
  LLVM_ABI Value *emitStrCat(Value *Dest, Value *Src, IRBuilderBase &B,
                             const TargetLibraryInfo *TLI);

  /// Emit a call to the strlcpy function.
  ///
  /// \param Dest Destination buffer pointer.
  /// \param Src Source C string pointer.
  /// \param Size Size of the destination buffer.
  /// \param B IR builder used to create the call.
  /// \param TLI Target library info describing available libcalls.
  /// \return The strlcpy call, or null if unavailable.
  LLVM_ABI Value *emitStrLCpy(Value *Dest, Value *Src, Value *Size,
                              IRBuilderBase &B, const TargetLibraryInfo *TLI);

  /// Emit a call to the strlcat function.
  ///
  /// \param Dest Destination C string pointer.
  /// \param Src Source C string pointer to append.
  /// \param Size Size of the destination buffer.
  /// \param B IR builder used to create the call.
  /// \param TLI Target library info describing available libcalls.
  /// \return The strlcat call, or null if unavailable.
  LLVM_ABI Value *emitStrLCat(Value *Dest, Value *Src, Value *Size,
                              IRBuilderBase &B, const TargetLibraryInfo *TLI);

  /// Emit a call to the strncat function.
  ///
  /// \param Dest Destination C string pointer.
  /// \param Src Source C string pointer to append.
  /// \param Size Maximum number of characters to append.
  /// \param B IR builder used to create the call.
  /// \param TLI Target library info describing available libcalls.
  /// \return The strncat call, or null if unavailable.
  LLVM_ABI Value *emitStrNCat(Value *Dest, Value *Src, Value *Size,
                              IRBuilderBase &B, const TargetLibraryInfo *TLI);

  /// Emit a call to the vsnprintf function.
  ///
  /// \param Dest Destination buffer pointer.
  /// \param Size Size of the destination buffer.
  /// \param Fmt Format string pointer.
  /// \param VAList va_list argument value.
  /// \param B IR builder used to create the call.
  /// \param TLI Target library info describing available libcalls.
  /// \return The vsnprintf call, or null if unavailable.
  LLVM_ABI Value *emitVSNPrintf(Value *Dest, Value *Size, Value *Fmt,
                                Value *VAList, IRBuilderBase &B,
                                const TargetLibraryInfo *TLI);

  /// Emit a call to the vsprintf function.
  ///
  /// \param Dest Destination buffer pointer.
  /// \param Fmt Format string pointer.
  /// \param VAList va_list argument value.
  /// \param B IR builder used to create the call.
  /// \param TLI Target library info describing available libcalls.
  /// \return The vsprintf call, or null if unavailable.
  LLVM_ABI Value *emitVSPrintf(Value *Dest, Value *Fmt, Value *VAList,
                               IRBuilderBase &B, const TargetLibraryInfo *TLI);

  /// Emit a call to the unary floating-point libcall named Name.
  ///
  /// Emit a call to the unary function named 'Name' (e.g.  'floor'). This
  /// function is known to take a single of type matching 'Op' and returns one
  /// value with the same type. If 'Op' is a long double, 'l' is added as the
  /// suffix of name, if 'Op' is a float, we add a 'f' suffix.
  ///
  /// \param Op Operand whose type selects float/double/long double.
  /// \param TLI Target library info describing available libcalls.
  /// \param Name Base name of the unary math function (e.g. "floor").
  /// \param B IR builder used to create the call.
  /// \param Attrs Attributes to attach to the call.
  /// \return The unary float libcall, or null if unavailable.
  LLVM_ABI Value *emitUnaryFloatFnCall(Value *Op, const TargetLibraryInfo *TLI,
                                       StringRef Name, IRBuilderBase &B,
                                       const AttributeList &Attrs);

  /// Emit a call to the unary DoubleFn, FloatFn, or LongDoubleFn for Op's type.
  ///
  /// \param Op Operand whose type selects which LibFunc to call.
  /// \param TLI Target library info describing available libcalls.
  /// \param DoubleFn LibFunc for the double variant.
  /// \param FloatFn LibFunc for the float variant.
  /// \param LongDoubleFn LibFunc for the long double variant.
  /// \param B IR builder used to create the call.
  /// \param Attrs Attributes to attach to the call.
  /// \return The unary float libcall, or null if unavailable.
  LLVM_ABI Value *emitUnaryFloatFnCall(Value *Op, const TargetLibraryInfo *TLI,
                                       LibFunc DoubleFn, LibFunc FloatFn,
                                       LibFunc LongDoubleFn, IRBuilderBase &B,
                                       const AttributeList &Attrs);

  /// Emit a call to the binary floating-point libcall named Name.
  ///
  /// Emit a call to the binary function named 'Name' (e.g. 'fmin'). This
  /// function is known to take type matching 'Op1' and 'Op2' and return one
  /// value with the same type. If 'Op1/Op2' are long double, 'l' is added as
  /// the suffix of name, if 'Op1/Op2' are float, we add a 'f' suffix.
  ///
  /// \param Op1 First operand; type must match \p Op2.
  /// \param Op2 Second operand; type must match \p Op1.
  /// \param TLI Target library info describing available libcalls.
  /// \param Name Base name of the binary math function (e.g. "fmin").
  /// \param B IR builder used to create the call.
  /// \param Attrs Attributes to attach to the call.
  /// \return The binary float libcall, or null if unavailable.
  LLVM_ABI Value *emitBinaryFloatFnCall(Value *Op1, Value *Op2,
                                        const TargetLibraryInfo *TLI,
                                        StringRef Name, IRBuilderBase &B,
                                        const AttributeList &Attrs);

  /// Emit a call to the binary DoubleFn, FloatFn, or LongDoubleFn for Op1's type.
  ///
  /// \param Op1 First operand; type selects which LibFunc to call.
  /// \param Op2 Second operand; type must match \p Op1.
  /// \param TLI Target library info describing available libcalls.
  /// \param DoubleFn LibFunc for the double variant.
  /// \param FloatFn LibFunc for the float variant.
  /// \param LongDoubleFn LibFunc for the long double variant.
  /// \param B IR builder used to create the call.
  /// \param Attrs Attributes to attach to the call.
  /// \return The binary float libcall, or null if unavailable.
  LLVM_ABI Value *emitBinaryFloatFnCall(Value *Op1, Value *Op2,
                                        const TargetLibraryInfo *TLI,
                                        LibFunc DoubleFn, LibFunc FloatFn,
                                        LibFunc LongDoubleFn, IRBuilderBase &B,
                                        const AttributeList &Attrs);

  /// Emit a call to putchar, assuming Char is an int.
  ///
  /// \param Char Character value to write (int).
  /// \param B IR builder used to create the call.
  /// \param TLI Target library info describing available libcalls.
  /// \return The putchar call, or null if unavailable.
  LLVM_ABI Value *emitPutChar(Value *Char, IRBuilderBase &B,
                              const TargetLibraryInfo *TLI);

  /// Emit a call to puts, assuming Str is some pointer.
  ///
  /// \param Str C string pointer to write.
  /// \param B IR builder used to create the call.
  /// \param TLI Target library info describing available libcalls.
  /// \return The puts call, or null if unavailable.
  LLVM_ABI Value *emitPutS(Value *Str, IRBuilderBase &B,
                           const TargetLibraryInfo *TLI);

  /// Emit a call to fputc with an int Char and a FILE pointer.
  ///
  /// This assumes that Char is an 'int', and File is a pointer to FILE.
  ///
  /// \param Char Character value to write (int).
  /// \param File Pointer to the FILE stream.
  /// \param B IR builder used to create the call.
  /// \param TLI Target library info describing available libcalls.
  /// \return The fputc call, or null if unavailable.
  LLVM_ABI Value *emitFPutC(Value *Char, Value *File, IRBuilderBase &B,
                            const TargetLibraryInfo *TLI);

  /// Emit a call to fputs with a string pointer and a FILE pointer.
  ///
  /// Str is required to be a pointer and File is a pointer to FILE.
  ///
  /// \param Str C string pointer to write.
  /// \param File Pointer to the FILE stream.
  /// \param B IR builder used to create the call.
  /// \param TLI Target library info describing available libcalls.
  /// \return The fputs call, or null if unavailable.
  LLVM_ABI Value *emitFPutS(Value *Str, Value *File, IRBuilderBase &B,
                            const TargetLibraryInfo *TLI);

  /// Emit a call to fwrite with a pointer, size_t Size, and FILE pointer.
  ///
  /// This assumes that Ptr is a pointer, Size is an 'size_t', and File is a
  /// pointer to FILE.
  ///
  /// \param Ptr Pointer to the data to write.
  /// \param Size Number of bytes to write.
  /// \param File Pointer to the FILE stream.
  /// \param B IR builder used to create the call.
  /// \param DL Data layout used for size_t typing.
  /// \param TLI Target library info describing available libcalls.
  /// \return The fwrite call, or null if unavailable.
  LLVM_ABI Value *emitFWrite(Value *Ptr, Value *Size, Value *File,
                             IRBuilderBase &B, const DataLayout &DL,
                             const TargetLibraryInfo *TLI);

  /// Emit a call to the malloc function.
  ///
  /// \param Num Number of bytes to allocate.
  /// \param B IR builder used to create the call.
  /// \param DL Data layout used for size typing.
  /// \param TLI Target library info describing available libcalls.
  /// \return The malloc call, or null if unavailable.
  LLVM_ABI Value *emitMalloc(Value *Num, IRBuilderBase &B, const DataLayout &DL,
                             const TargetLibraryInfo *TLI);

  /// Emit a call to the calloc function.
  ///
  /// \param Num Number of elements to allocate.
  /// \param Size Size of each element in bytes.
  /// \param B IR builder used to create the call.
  /// \param TLI Target library info describing available libcalls.
  /// \param AddrSpace Address space of the returned pointer.
  /// \return The calloc call, or null if unavailable.
  LLVM_ABI Value *emitCalloc(Value *Num, Value *Size, IRBuilderBase &B,
                             const TargetLibraryInfo &TLI, unsigned AddrSpace);

  /// Emit a call to the hot/cold operator new function.
  ///
  /// \param Num Number of bytes to allocate.
  /// \param B IR builder used to create the call.
  /// \param TLI Target library info describing available libcalls.
  /// \param NewFunc LibFunc identifying which operator new to call.
  /// \param HotCold Hot/cold hint argument for the allocator.
  /// \return The hot/cold new call, or null if unavailable.
  LLVM_ABI Value *emitHotColdNew(Value *Num, IRBuilderBase &B,
                                 const TargetLibraryInfo *TLI, LibFunc NewFunc,
                                 Value *HotCold);

  /// Emit a call to the hot/cold nothrow operator new function.
  ///
  /// \param Num Number of bytes to allocate.
  /// \param NoThrow Nothrow tag argument for the allocator.
  /// \param B IR builder used to create the call.
  /// \param TLI Target library info describing available libcalls.
  /// \param NewFunc LibFunc identifying which operator new to call.
  /// \param HotCold Hot/cold hint argument for the allocator.
  /// \return The nothrow new call, or null if unavailable.
  LLVM_ABI Value *emitHotColdNewNoThrow(Value *Num, Value *NoThrow,
                                        IRBuilderBase &B,
                                        const TargetLibraryInfo *TLI,
                                        LibFunc NewFunc, Value *HotCold);

  /// Emit a call to the hot/cold aligned operator new function.
  ///
  /// \param Num Number of bytes to allocate.
  /// \param Align Alignment requested for the allocation.
  /// \param B IR builder used to create the call.
  /// \param TLI Target library info describing available libcalls.
  /// \param NewFunc LibFunc identifying which operator new to call.
  /// \param HotCold Hot/cold hint argument for the allocator.
  /// \return The aligned new call, or null if unavailable.
  LLVM_ABI Value *emitHotColdNewAligned(Value *Num, Value *Align,
                                        IRBuilderBase &B,
                                        const TargetLibraryInfo *TLI,
                                        LibFunc NewFunc, Value *HotCold);

  /// Emit a call to the hot/cold aligned nothrow operator new function.
  ///
  /// \param Num Number of bytes to allocate.
  /// \param Align Alignment requested for the allocation.
  /// \param NoThrow Nothrow tag argument for the allocator.
  /// \param B IR builder used to create the call.
  /// \param TLI Target library info describing available libcalls.
  /// \param NewFunc LibFunc identifying which operator new to call.
  /// \param HotCold Hot/cold hint argument for the allocator.
  /// \return The aligned nothrow new call, or null if unavailable.
  LLVM_ABI Value *emitHotColdNewAlignedNoThrow(Value *Num, Value *Align,
                                               Value *NoThrow, IRBuilderBase &B,
                                               const TargetLibraryInfo *TLI,
                                               LibFunc NewFunc, Value *HotCold);

  /// Emit a call to the hot/cold size-returning operator new function.
  ///
  /// \param Num Number of bytes to allocate.
  /// \param B IR builder used to create the call.
  /// \param TLI Target library info describing available libcalls.
  /// \param NewFunc LibFunc identifying which operator new to call.
  /// \param HotCold Hot/cold hint argument for the allocator.
  /// \return The size-returning new call, or null if unavailable.
  LLVM_ABI Value *emitHotColdSizeReturningNew(Value *Num, IRBuilderBase &B,
                                              const TargetLibraryInfo *TLI,
                                              LibFunc NewFunc, Value *HotCold);

  /// Emit a call to the hot/cold size-returning aligned operator new function.
  ///
  /// \param Num Number of bytes to allocate.
  /// \param Align Alignment requested for the allocation.
  /// \param B IR builder used to create the call.
  /// \param TLI Target library info describing available libcalls.
  /// \param NewFunc LibFunc identifying which operator new to call.
  /// \param HotCold Hot/cold hint argument for the allocator.
  /// \return The size-returning aligned new call, or null if unavailable.
  LLVM_ABI Value *
  emitHotColdSizeReturningNewAligned(Value *Num, Value *Align, IRBuilderBase &B,
                                     const TargetLibraryInfo *TLI,
                                     LibFunc NewFunc, Value *HotCold);
}

#endif
