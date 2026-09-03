//===- TypeMetadataUtils.h - Utilities related to type metadata --*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains functions that make it easier to manipulate type metadata
// for devirtualization.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_TYPEMETADATAUTILS_H
#define LLVM_ANALYSIS_TYPEMETADATAUTILS_H

#include "llvm/Support/Compiler.h"
#include <cstdint>
#include <utility>

namespace llvm {

template <typename T> class SmallVectorImpl;
class CallBase;
class CallInst;
class Constant;
class Function;
class DominatorTree;
class GlobalVariable;
class Instruction;
class Module;

/// The type of CFI jumptable needed for a function.
enum CfiFunctionLinkage {
  /// Function definition with a canonical CFI jumptable entry.
  CFL_Definition = 0,
  /// Non-weak function declaration needing a CFI jumptable.
  CFL_Declaration = 1,
  /// Externally weak function declaration needing a CFI jumptable.
  CFL_WeakDeclaration = 2
};

/// A call site that could be devirtualized.
struct DevirtCallSite {
  /// The offset from the address point to the virtual function.
  uint64_t Offset;
  /// The call site itself.
  CallBase &CB;
};

/// Given a call to the intrinsic \@llvm.type.test, find all devirtualizable
/// call sites based on the call and return them in DevirtCalls.
/// @param DevirtCalls Output list of discovered devirtualizable call sites.
/// @param Assumes Output list of llvm.assume calls that use the type test.
/// @param CI The \@llvm.type.test (or \@llvm.public.type.test) intrinsic call.
/// @param DT Dominator tree used to filter dominating call sites.
LLVM_ABI void findDevirtualizableCallsForTypeTest(
    SmallVectorImpl<DevirtCallSite> &DevirtCalls,
    SmallVectorImpl<CallInst *> &Assumes, const CallInst *CI,
    DominatorTree &DT);

/// Given a call to the intrinsic \@llvm.type.checked.load, find all
/// devirtualizable call sites based on the call and return them in DevirtCalls.
/// @param DevirtCalls Output list of discovered devirtualizable call sites.
/// @param LoadedPtrs Output list of extractvalue instructions for loaded
/// pointers.
/// @param Preds Output list of extractvalue instructions for the type-check
/// predicates.
/// @param HasNonCallUses Set to true if the intrinsic has uses that are not
/// calls or the expected extracts.
/// @param CI The \@llvm.type.checked.load (or relative) intrinsic call.
/// @param DT Dominator tree used to filter dominating call sites.
LLVM_ABI void findDevirtualizableCallsForTypeCheckedLoad(
    SmallVectorImpl<DevirtCallSite> &DevirtCalls,
    SmallVectorImpl<Instruction *> &LoadedPtrs,
    SmallVectorImpl<Instruction *> &Preds, bool &HasNonCallUses,
    const CallInst *CI, DominatorTree &DT);

/// Find a trivial pointer located at a byte offset within a constant.
///
/// Processes a Constant recursively looking into elements of arrays, structs
/// and expressions to find a trivial pointer element that is located at the
/// given offset (relative to the beginning of the whole outer Constant).
///
/// Used for example from GlobalDCE to find an entry in a C++ vtable that
/// matches a vcall offset.
///
/// To support relative vtables, getPointerAtOffset can see through "relative
/// pointers", i.e. (sub-)expressions of the form of:
///
/// @symbol = ... {
///   i32 trunc (i64 sub (
///     i64 ptrtoint (<type> @target to i64), i64 ptrtoint (... @symbol to i64)
///   ) to i32)
/// }
///
/// For such (sub-)expressions, getPointerAtOffset returns the @target pointer.
/// @param I Constant to search for a pointer at \p Offset.
/// @param Offset Byte offset from the start of \p I at which to look.
/// @param M Module providing the data layout for layout calculations.
/// @param TopLevelGlobal Optional top-level global used to validate relative
/// pointer bases; may be nullptr.
/// @return The trivial pointer at \p Offset, or nullptr if none is found.
LLVM_ABI Constant *getPointerAtOffset(Constant *I, uint64_t Offset, Module &M,
                                      Constant *TopLevelGlobal = nullptr);

/// Return the function and pointer at a vtable offset, if they refer to a
/// function.
///
/// Given a vtable and a specified offset, returns the function and the trivial
/// pointer at the specified offset in pair iff the pointer at the specified
/// offset is a function or an alias to a function. Returns a pair of nullptr
/// otherwise.
/// @param GV Global variable whose initializer is treated as a vtable.
/// @param Offset Byte offset into the vtable at which to look up a function.
/// @param M Module providing the data layout for layout calculations.
/// @return A pair of the function and pointer at \p Offset, or a pair of
/// nullptrs if the entry is not a function.
LLVM_ABI std::pair<Function *, Constant *>
getFunctionAtVTableOffset(GlobalVariable *GV, uint64_t Offset, Module &M);

/// Finds the same "relative pointer" pattern as described above, where the
/// target is `C`, and replaces the entire pattern with a constant zero.
/// @param C Constant that is the target of relative-pointer expressions to
/// replace.
LLVM_ABI void replaceRelativePointerUsersWithZero(Constant *C);

} // namespace llvm

#endif
