//==- llvm/Analysis/MemoryBuiltins.h - Calls to memory builtins --*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This family of functions identifies calls to builtin functions that allocate
// or free memory.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_MEMORYBUILTINS_H
#define LLVM_ANALYSIS_MEMORYBUILTINS_H

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/TargetFolder.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>
#include <optional>
#include <utility>

namespace llvm {

class AllocaInst;
class AAResults;
class Argument;
class ConstantPointerNull;
class DataLayout;
class ExtractElementInst;
class ExtractValueInst;
class GEPOperator;
class GlobalAlias;
class GlobalVariable;
class Instruction;
class IntegerType;
class IntrinsicInst;
class IntToPtrInst;
class LLVMContext;
class LoadInst;
class PHINode;
class SelectInst;
class Type;
class UndefValue;
class Value;

/// Tests if a value is a call or invoke to a library function that
/// allocates or reallocates memory (either malloc, calloc, realloc, or strdup
/// like).
/// @param V Value that may be a call or invoke.
/// @param TLI Target library info used to identify allocation functions.
/// @return True if \p V is a call or invoke to such an allocation function.
LLVM_ABI bool isAllocationFn(const Value *V, const TargetLibraryInfo *TLI);
/// Tests if a value is a call or invoke to a library function that
/// allocates or reallocates memory (either malloc, calloc, realloc, or strdup
/// like).
/// @param V Value that may be a call or invoke.
/// @param GetTLI Callback that returns TargetLibraryInfo for a Function.
/// @return True if \p V is a call or invoke to such an allocation function.
LLVM_ABI bool
isAllocationFn(const Value *V,
               function_ref<const TargetLibraryInfo &(Function &)> GetTLI);

/// Tests if a value is a call or invoke to a library function that
/// allocates memory (either malloc, calloc, or strdup like).
/// @param V Value that may be a call or invoke.
/// @param TLI Target library info used to identify allocation functions.
/// @return True if \p V is a call or invoke to such an allocation function.
LLVM_ABI bool isAllocLikeFn(const Value *V, const TargetLibraryInfo *TLI);

/// Tests if a function is a call or invoke to a library function that
/// reallocates memory (e.g., realloc).
/// @param F Function that may be a realloc-like library function.
/// @return True if \p F is a realloc-like library function.
LLVM_ABI bool isReallocLikeFn(const Function *F);

/// If this is a call to a realloc function, return the reallocated operand.
/// @param CB Call that may be a realloc-like function.
/// @return The reallocated operand, or nullptr if \p CB is not a realloc.
LLVM_ABI Value *getReallocatedOperand(const CallBase *CB);

//===----------------------------------------------------------------------===//
//  free Call Utility Functions.
//

/// isLibFreeFunction - Returns true if the function is a builtin free()
/// @param F Function that may be a free-like library function.
/// @param TLIFn Expected LibFunc identifier for the free-like function.
/// @return True if \p F is a builtin free() matching \p TLIFn.
LLVM_ABI bool isLibFreeFunction(const Function *F, const LibFunc TLIFn);

/// If this if a call to a free function, return the freed operand.
/// @param CB Call that may be a free-like function.
/// @param TLI Target library info used to identify free functions.
/// @return The freed operand, or nullptr if \p CB is not a free.
LLVM_ABI Value *getFreedOperand(const CallBase *CB,
                                const TargetLibraryInfo *TLI);

//===----------------------------------------------------------------------===//
//  Properties of allocation functions
//

/// Return true if this is a call to a removable allocation function.
///
/// Removable means the call does not have side effects that we are required to
/// preserve beyond the effect of allocating a new object.
/// Ex: If our allocation routine has a counter for the number of objects
/// allocated, and the program prints it on exit, can the value change due
/// to optimization? Answer is highly language dependent.
/// Note: *Removable* really does mean removable; it does not mean observable.
/// A language (e.g. C++) can allow removing allocations without allowing
/// insertion or speculative execution of allocation routines.
/// @param V Call that may be an allocation function.
/// @param TLI Target library info used to identify allocation functions.
/// @return True if \p V is a call to a removable allocation function.
LLVM_ABI bool isRemovableAlloc(const CallBase *V, const TargetLibraryInfo *TLI);

/// Gets the alignment argument for an aligned_alloc-like function.
///
/// Uses either built-in knowledge based on fuction names/signatures or
/// allocalign attributes. Note: the Value returned may not indicate a valid
/// alignment, per the definition of the allocalign attribute.
/// @param V Call that may be an aligned allocation function.
/// @param TLI Target library info used to identify allocation functions.
/// @return The alignment argument, or nullptr if none is found.
LLVM_ABI Value *getAllocAlignment(const CallBase *V,
                                  const TargetLibraryInfo *TLI);

/// Return the size of the requested allocation.
///
/// With a trivial mapper, this is similar to calling getObjectSize(..., Exact),
/// but without looking through calls that return their argument. A mapper
/// function can be used to replace one Value* (operand to the allocation) with
/// another. This is useful when doing abstract interpretation.
/// @param CB Call to an allocation function.
/// @param TLI Target library info used to identify allocation functions.
/// @param Mapper Optional mapper that rewrites allocation operands.
/// @return The requested allocation size, or std::nullopt if unknown.
LLVM_ABI std::optional<APInt> getAllocSize(
    const CallBase *CB, const TargetLibraryInfo *TLI,
    function_ref<const Value *(const Value *)> Mapper = [](const Value *V) {
      return V;
    });

/// If this is a call to an allocation function that initializes memory to a
/// fixed value, return said value in the requested type.  Otherwise, return
/// nullptr.
/// @param V Value that may be an initializing allocation call.
/// @param TLI Target library info used to identify allocation functions.
/// @param Ty Type in which to materialize the initial value.
/// @return The initial value in \p Ty, or nullptr if not an initializing
/// allocation.
LLVM_ABI Constant *getInitialValueOfAllocation(const Value *V,
                                               const TargetLibraryInfo *TLI,
                                               Type *Ty);

/// If a function is part of an allocation family (e.g.
/// malloc/realloc/calloc/free), return the identifier for its family
/// of functions.
/// @param I Value that may be a call in an allocation family.
/// @param TLI Target library info used to identify allocation functions.
/// @return The family identifier, or std::nullopt if not in a family.
LLVM_ABI std::optional<StringRef>
getAllocationFamily(const Value *I, const TargetLibraryInfo *TLI);

//===----------------------------------------------------------------------===//
//  Utility functions to compute size of objects.
//

/// Various options to control the behavior of getObjectSize.
struct ObjectSizeOpts {
  /// Controls how we handle conditional statements with unknown conditions.
  enum class Mode : uint8_t {
    /// All branches must be known and have the same size, starting from the
    /// offset, to be merged.
    ExactSizeFromOffset,
    /// All branches must be known and have the same underlying size and offset
    /// to be merged.
    ExactUnderlyingSizeAndOffset,
    /// Evaluate all branches of an unknown condition. If all evaluations
    /// succeed, pick the minimum size.
    Min,
    /// Same as Min, except we pick the maximum size of all of the branches.
    Max,
  };

  /// How we want to evaluate this object's size.
  Mode EvalMode = Mode::ExactSizeFromOffset;
  /// Whether to round the result up to the alignment of allocas, byval
  /// arguments, and global variables.
  bool RoundToAlign = false;
  /// If true, treat null pointers in address space 0 as unknown size.
  ///
  /// Otherwise, null is always considered to point to a 0 byte region of
  /// memory.
  bool NullIsUnknownSize = false;
  /// If set, used for more accurate evaluation
  AAResults *AA = nullptr;
};

/// Compute the size of the object pointed by Ptr.
///
/// Returns true and the object size in Size if successful, and false otherwise.
/// In this context, by object we mean the region of memory starting at Ptr to
/// the end of the underlying object pointed to by Ptr.
///
/// WARNING: The object size returned is the allocation size.  This does not
/// imply dereferenceability at site of use since the object may be freeed in
/// between.
/// @param Ptr Pointer to the object whose size is requested.
/// @param Size On success, receives the computed object size in bytes.
/// @param DL Data layout used for type and pointer sizing.
/// @param TLI Target library info used to identify allocation functions.
/// @param Opts Options controlling how object size is evaluated.
/// @return True on success with \p Size filled; false otherwise.
LLVM_ABI bool getObjectSize(const Value *Ptr, uint64_t &Size,
                            const DataLayout &DL, const TargetLibraryInfo *TLI,
                            ObjectSizeOpts Opts = {});

/// Like getObjectSize(), but only for base objects.
///
/// Returns the size of base objects (like allocas, global variables and
/// allocator calls) and std::nullopt otherwise. Requires ExactSizeFromOffset
/// mode.
/// @param Ptr Pointer that may refer to a base object.
/// @param DL Data layout used for type and pointer sizing.
/// @param TLI Target library info used to identify allocation functions.
/// @param Opts Options controlling how object size is evaluated.
/// @return The base object size, or std::nullopt if \p Ptr is not a base
/// object.
LLVM_ABI std::optional<TypeSize> getBaseObjectSize(const Value *Ptr,
                                                   const DataLayout &DL,
                                                   const TargetLibraryInfo *TLI,
                                                   ObjectSizeOpts Opts = {});

/// Try to turn a call to \@llvm.objectsize into an integer value.
///
/// Returns null on failure. If MustSucceed is true, this function will not
/// return null, and may return conservative values governed by the second
/// argument of the call to objectsize.
/// @param ObjectSize Intrinsic call to lower.
/// @param DL Data layout used for type and pointer sizing.
/// @param TLI Target library info used to identify allocation functions.
/// @param MustSucceed When true, always produce a value, possibly conservative.
/// @return An integer Value for the object size, or nullptr on failure.
LLVM_ABI Value *lowerObjectSizeCall(IntrinsicInst *ObjectSize,
                                    const DataLayout &DL,
                                    const TargetLibraryInfo *TLI,
                                    bool MustSucceed);
/// Try to turn a call to \@llvm.objectsize into an integer value.
///
/// Returns null on failure. If MustSucceed is true, this function will not
/// return null, and may return conservative values governed by the second
/// argument of the call to objectsize.
/// @param ObjectSize Intrinsic call to lower.
/// @param DL Data layout used for type and pointer sizing.
/// @param TLI Target library info used to identify allocation functions.
/// @param AA Optional alias analysis results for more precise evaluation.
/// @param MustSucceed When true, always produce a value, possibly conservative.
/// @param InsertedInstructions Optional list that receives any new instructions.
/// @return An integer Value for the object size, or nullptr on failure.
LLVM_ABI Value *lowerObjectSizeCall(
    IntrinsicInst *ObjectSize, const DataLayout &DL,
    const TargetLibraryInfo *TLI, AAResults *AA, bool MustSucceed,
    SmallVectorImpl<Instruction *> *InsertedInstructions = nullptr);

/// SizeOffsetType - A base template class for the object size visitors. Used
/// here as a self-documenting way to handle the values rather than using a
/// \p std::pair.
template <typename T, class C> struct SizeOffsetType {
public:
  /// Size of the underlying object, or an unknown sentinel.
  T Size;
  /// Offset from the start of the underlying object, or an unknown sentinel.
  T Offset;

  /// Construct a size/offset pair with unknown values.
  SizeOffsetType() = default;
  /// Construct a size/offset pair from known values.
  /// @param Size Size of the underlying object.
  /// @param Offset Offset from the start of the underlying object.
  SizeOffsetType(T Size, T Offset)
      : Size(std::move(Size)), Offset(std::move(Offset)) {}

  /// Return true if Size is known.
  /// @return True if Size is known.
  bool knownSize() const { return C::known(Size); }
  /// Return true if Offset is known.
  /// @return True if Offset is known.
  bool knownOffset() const { return C::known(Offset); }
  /// Return true if either Size or Offset is known.
  /// @return True if either Size or Offset is known.
  bool anyKnown() const { return knownSize() || knownOffset(); }
  /// Return true if both Size and Offset are known.
  /// @return True if both Size and Offset are known.
  bool bothKnown() const { return knownSize() && knownOffset(); }

  /// Return true if this size/offset pair equals \p RHS.
  /// @param RHS Other size/offset pair to compare against.
  /// @return True if this size/offset pair equals \p RHS.
  bool operator==(const SizeOffsetType<T, C> &RHS) const {
    return Size == RHS.Size && Offset == RHS.Offset;
  }
  /// Return true if this size/offset pair differs from \p RHS.
  /// @param RHS Other size/offset pair to compare against.
  /// @return True if this size/offset pair differs from \p RHS.
  bool operator!=(const SizeOffsetType<T, C> &RHS) const {
    return !(*this == RHS);
  }
};

/// SizeOffsetAPInt - Used by \p ObjectSizeOffsetVisitor, which works with
/// \p APInts.
struct SizeOffsetAPInt : public SizeOffsetType<APInt, SizeOffsetAPInt> {
  /// Construct a size/offset pair with unknown APInt values.
  SizeOffsetAPInt() = default;
  /// Construct a size/offset pair from known APInt values.
  /// @param Size Size of the underlying object.
  /// @param Offset Offset from the start of the underlying object.
  SizeOffsetAPInt(APInt Size, APInt Offset)
      : SizeOffsetType(std::move(Size), std::move(Offset)) {}

  /// Return true if \p V represents a known APInt value.
  /// @param V APInt to test for a known bit width.
  /// @return True if \p V has a known bit width greater than one.
  static bool known(const APInt &V) { return V.getBitWidth() > 1; }
};

/// OffsetSpan - Used internally by \p ObjectSizeOffsetVisitor. Represents a
/// point in memory as a pair of allocated bytes before and after it.
///
/// \c Before and \c After fields are signed values. It makes it possible to
/// represent out-of-bound access, e.g. as a result of a GEP, at the expense of
/// not being able to represent very large allocation.
struct OffsetSpan {
  /// Number of allocated bytes before this point.
  APInt Before;
  /// Number of allocated bytes after this point.
  APInt After;

  /// Construct an offset span with unknown before/after values.
  OffsetSpan() = default;
  /// Construct an offset span from known before/after values.
  /// @param Before Number of allocated bytes before this point.
  /// @param After Number of allocated bytes after this point.
  OffsetSpan(APInt Before, APInt After) : Before(Before), After(After) {}

  /// Return true if Before is known.
  /// @return True if Before is known.
  bool knownBefore() const { return known(Before); }
  /// Return true if After is known.
  /// @return True if After is known.
  bool knownAfter() const { return known(After); }
  /// Return true if either Before or After is known.
  /// @return True if either Before or After is known.
  bool anyKnown() const { return knownBefore() || knownAfter(); }
  /// Return true if both Before and After are known.
  /// @return True if both Before and After are known.
  bool bothKnown() const { return knownBefore() && knownAfter(); }

  /// Return true if this offset span equals \p RHS.
  /// @param RHS Other offset span to compare against.
  /// @return True if this offset span equals \p RHS.
  bool operator==(const OffsetSpan &RHS) const {
    return Before == RHS.Before && After == RHS.After;
  }
  /// Return true if this offset span differs from \p RHS.
  /// @param RHS Other offset span to compare against.
  /// @return True if this offset span differs from \p RHS.
  bool operator!=(const OffsetSpan &RHS) const { return !(*this == RHS); }

  /// Return true if \p V represents a known APInt value.
  /// @param V APInt to test for a known bit width.
  /// @return True if \p V has a known bit width greater than one.
  static bool known(const APInt &V) { return V.getBitWidth() > 1; }
};

/// Evaluate the size and offset of an object pointed to by a Value*
/// statically. Fails if size or offset are not known at compile time.
class ObjectSizeOffsetVisitor
    : public InstVisitor<ObjectSizeOffsetVisitor, OffsetSpan> {
  const DataLayout &DL;
  const TargetLibraryInfo *TLI;
  ObjectSizeOpts Options;
  unsigned IntTyBits;
  APInt Zero;
  SmallDenseMap<Instruction *, OffsetSpan, 8> SeenInsts;
  unsigned InstructionsVisited;

  APInt align(APInt Size, MaybeAlign Align);

  static OffsetSpan unknown() { return OffsetSpan(); }

public:
  /// Construct a visitor that evaluates object size and offset statically.
  /// @param DL Data layout used for type and pointer sizing.
  /// @param TLI Target library info used to identify allocation functions.
  /// @param Context LLVM context used for integer constants.
  /// @param Options Options controlling how object size is evaluated.
  LLVM_ABI ObjectSizeOffsetVisitor(const DataLayout &DL,
                                   const TargetLibraryInfo *TLI,
                                   LLVMContext &Context,
                                   ObjectSizeOpts Options = {});

  /// Compute the statically known size and offset of the object pointed to by
  /// \p V.
  /// @param V Pointer value to analyze.
  /// @return The statically known size and offset, or unknown values on failure.
  LLVM_ABI SizeOffsetAPInt compute(Value *V);

  // These are "private", except they can't actually be made private. Only
  // compute() should be used by external users.
  /// Visit an alloca and return its offset span.
  /// @param I Alloca instruction to analyze.
  /// @return The offset span of the alloca, or unknown if it cannot be computed.
  LLVM_ABI OffsetSpan visitAllocaInst(AllocaInst &I);
  /// Visit a function argument and return its offset span.
  /// @param A Argument to analyze.
  /// @return The offset span of the argument, or unknown if it cannot be
  /// computed.
  LLVM_ABI OffsetSpan visitArgument(Argument &A);
  /// Visit a call or invoke and return its offset span.
  /// @param CB Call to analyze.
  /// @return The offset span of the call result, or unknown if it cannot be
  /// computed.
  LLVM_ABI OffsetSpan visitCallBase(CallBase &CB);
  /// Visit a null pointer constant and return its offset span.
  /// @param CPN Null pointer constant to analyze.
  /// @return The offset span of the null pointer.
  LLVM_ABI OffsetSpan visitConstantPointerNull(ConstantPointerNull &CPN);
  /// Visit an extractelement and return its offset span.
  /// @param I ExtractElementInst to analyze.
  /// @return The offset span of the extracted element, or unknown if it cannot
  /// be computed.
  LLVM_ABI OffsetSpan visitExtractElementInst(ExtractElementInst &I);
  /// Visit an extractvalue and return its offset span.
  /// @param I ExtractValueInst to analyze.
  /// @return The offset span of the extracted value, or unknown if it cannot be
  /// computed.
  LLVM_ABI OffsetSpan visitExtractValueInst(ExtractValueInst &I);
  /// Visit a global alias and return its offset span.
  /// @param GA Global alias to analyze.
  /// @return The offset span of the alias target, or unknown if it cannot be
  /// computed.
  LLVM_ABI OffsetSpan visitGlobalAlias(GlobalAlias &GA);
  /// Visit a global variable and return its offset span.
  /// @param GV Global variable to analyze.
  /// @return The offset span of the global variable, or unknown if it cannot be
  /// computed.
  LLVM_ABI OffsetSpan visitGlobalVariable(GlobalVariable &GV);
  /// Visit an inttoptr and return its offset span.
  /// @param I IntToPtrInst to analyze.
  /// @return The offset span of the converted pointer, or unknown if it cannot
  /// be computed.
  LLVM_ABI OffsetSpan visitIntToPtrInst(IntToPtrInst &I);
  /// Visit a load and return its offset span.
  /// @param I LoadInst to analyze.
  /// @return The offset span of the loaded pointer, or unknown if it cannot be
  /// computed.
  LLVM_ABI OffsetSpan visitLoadInst(LoadInst &I);
  /// Visit a PHI and return its offset span.
  /// @param PN PHINode to analyze.
  /// @return The offset span of the PHI, or unknown if it cannot be computed.
  LLVM_ABI OffsetSpan visitPHINode(PHINode &PN);
  /// Visit a select and return its offset span.
  /// @param I SelectInst to analyze.
  /// @return The offset span of the select, or unknown if it cannot be computed.
  LLVM_ABI OffsetSpan visitSelectInst(SelectInst &I);
  /// Visit an undef value and return its offset span.
  /// @param UV UndefValue to analyze.
  /// @return An unknown offset span.
  LLVM_ABI OffsetSpan visitUndefValue(UndefValue &UV);
  /// Visit a generic instruction and return its offset span.
  /// @param I Instruction to analyze.
  /// @return An unknown offset span for unhandled instructions.
  LLVM_ABI OffsetSpan visitInstruction(Instruction &I);

private:
  OffsetSpan
  findLoadOffsetRange(LoadInst &LoadFrom, BasicBlock &BB,
                      BasicBlock::iterator From,
                      SmallDenseMap<BasicBlock *, OffsetSpan, 8> &VisitedBlocks,
                      unsigned &ScannedInstCount);
  OffsetSpan combineOffsetRange(OffsetSpan LHS, OffsetSpan RHS);
  OffsetSpan computeImpl(Value *V);
  OffsetSpan computeValue(Value *V);
  bool checkedZextOrTrunc(APInt &I);
};

struct SizeOffsetWeakTrackingVH;
/// Size and offset pair represented as IR Values.
///
/// Used by \p ObjectSizeOffsetEvaluator, which works with \p Values.
struct SizeOffsetValue : public SizeOffsetType<Value *, SizeOffsetValue> {
  /// Construct a size/offset pair with null Values.
  SizeOffsetValue() : SizeOffsetType(nullptr, nullptr) {}
  /// Construct a size/offset pair from Value pointers.
  /// @param Size Value representing the object size.
  /// @param Offset Value representing the offset from the object start.
  SizeOffsetValue(Value *Size, Value *Offset) : SizeOffsetType(Size, Offset) {}
  /// Construct a size/offset pair from weak tracking handles.
  /// @param SOT Weak tracking size/offset pair to copy from.
  LLVM_ABI SizeOffsetValue(const SizeOffsetWeakTrackingVH &SOT);

  /// Return true if \p V is a known non-null Value.
  /// @param V Value pointer to test.
  /// @return True if \p V is non-null.
  static bool known(Value *V) { return V != nullptr; }
};

/// SizeOffsetWeakTrackingVH - Used by \p ObjectSizeOffsetEvaluator in a
/// \p DenseMap.
struct SizeOffsetWeakTrackingVH
    : public SizeOffsetType<WeakTrackingVH, SizeOffsetWeakTrackingVH> {
  /// Construct a size/offset pair with null weak handles.
  SizeOffsetWeakTrackingVH() : SizeOffsetType(nullptr, nullptr) {}
  /// Construct a size/offset pair from Value pointers held weakly.
  /// @param Size Value representing the object size.
  /// @param Offset Value representing the offset from the object start.
  SizeOffsetWeakTrackingVH(Value *Size, Value *Offset)
      : SizeOffsetType(Size, Offset) {}
  /// Construct a size/offset pair from a SizeOffsetValue.
  /// @param SOV Size/offset Values to wrap in weak handles.
  SizeOffsetWeakTrackingVH(const SizeOffsetValue &SOV)
      : SizeOffsetType(SOV.Size, SOV.Offset) {}

  /// Return true if \p V still points to an alive Value.
  /// @param V Weak handle to test.
  /// @return True if \p V still points to an alive Value.
  static bool known(WeakTrackingVH V) { return V.pointsToAliveValue(); }
};

/// Evaluate the size and offset of an object pointed to by a Value*.
/// May create code to compute the result at run-time.
class ObjectSizeOffsetEvaluator
    : public InstVisitor<ObjectSizeOffsetEvaluator, SizeOffsetValue> {
  using BuilderTy = IRBuilder<TargetFolder, IRBuilderCallbackInserter>;
  using WeakEvalType = SizeOffsetWeakTrackingVH;
  using CacheMapTy = DenseMap<const Value *, WeakEvalType>;
  using PtrSetTy = SmallPtrSet<const Value *, 8>;

  const DataLayout &DL;
  const TargetLibraryInfo *TLI;
  LLVMContext &Context;
  BuilderTy Builder;
  IntegerType *IntTy;
  Value *Zero;
  CacheMapTy CacheMap;
  PtrSetTy SeenVals;
  ObjectSizeOpts EvalOpts;
  SmallPtrSet<Instruction *, 8> InsertedInstructions;

  SizeOffsetValue compute_(Value *V);

public:
  /// Construct an evaluator that may emit runtime size/offset code.
  /// @param DL Data layout used for type and pointer sizing.
  /// @param TLI Target library info used to identify allocation functions.
  /// @param Context LLVM context used for IR builder and constants.
  /// @param EvalOpts Options controlling how object size is evaluated.
  LLVM_ABI ObjectSizeOffsetEvaluator(const DataLayout &DL,
                                     const TargetLibraryInfo *TLI,
                                     LLVMContext &Context,
                                     ObjectSizeOpts EvalOpts = {});

  /// Return an unknown size/offset pair.
  /// @return A size/offset pair with null Values.
  static SizeOffsetValue unknown() { return SizeOffsetValue(); }

  /// Compute the size and offset of the object pointed to by \p V.
  /// @param V Pointer value to analyze.
  /// @return The size and offset Values, or unknown values on failure.
  LLVM_ABI SizeOffsetValue compute(Value *V);

  // The individual instruction visitors should be treated as private.
  /// Visit an alloca and return its size and offset Values.
  /// @param I Alloca instruction to analyze.
  /// @return The size and offset Values of the alloca, or unknown if they cannot
  /// be computed.
  LLVM_ABI SizeOffsetValue visitAllocaInst(AllocaInst &I);
  /// Visit a call or invoke and return its size and offset Values.
  /// @param CB Call to analyze.
  /// @return The size and offset Values of the call result, or unknown if they
  /// cannot be computed.
  LLVM_ABI SizeOffsetValue visitCallBase(CallBase &CB);
  /// Visit an extractelement and return its size and offset Values.
  /// @param I ExtractElementInst to analyze.
  /// @return The size and offset Values of the extracted element, or unknown if
  /// they cannot be computed.
  LLVM_ABI SizeOffsetValue visitExtractElementInst(ExtractElementInst &I);
  /// Visit an extractvalue and return its size and offset Values.
  /// @param I ExtractValueInst to analyze.
  /// @return The size and offset Values of the extracted value, or unknown if
  /// they cannot be computed.
  LLVM_ABI SizeOffsetValue visitExtractValueInst(ExtractValueInst &I);
  /// Visit a GEP and return its size and offset Values.
  /// @param GEP GEPOperator to analyze.
  /// @return The size and offset Values after applying the GEP, or unknown if
  /// they cannot be computed.
  LLVM_ABI SizeOffsetValue visitGEPOperator(GEPOperator &GEP);
  /// Visit an inttoptr and return its size and offset Values.
  /// @param I IntToPtrInst to analyze.
  /// @return The size and offset Values of the converted pointer, or unknown if
  /// they cannot be computed.
  LLVM_ABI SizeOffsetValue visitIntToPtrInst(IntToPtrInst &I);
  /// Visit a load and return its size and offset Values.
  /// @param I LoadInst to analyze.
  /// @return The size and offset Values of the loaded pointer, or unknown if
  /// they cannot be computed.
  LLVM_ABI SizeOffsetValue visitLoadInst(LoadInst &I);
  /// Visit a PHI and return its size and offset Values.
  /// @param PHI PHINode to analyze.
  /// @return The size and offset Values of the PHI, or unknown if they cannot be
  /// computed.
  LLVM_ABI SizeOffsetValue visitPHINode(PHINode &PHI);
  /// Visit a select and return its size and offset Values.
  /// @param I SelectInst to analyze.
  /// @return The size and offset Values of the select, or unknown if they cannot
  /// be computed.
  LLVM_ABI SizeOffsetValue visitSelectInst(SelectInst &I);
  /// Visit a generic instruction and return its size and offset Values.
  /// @param I Instruction to analyze.
  /// @return An unknown size/offset pair for unhandled instructions.
  LLVM_ABI SizeOffsetValue visitInstruction(Instruction &I);
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_MEMORYBUILTINS_H
