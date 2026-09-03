//===- CoroShape.h - Coroutine info for lowering --------------*- C++ -*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This file declares the shape info struct that is required by many coroutine
// utility methods.
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_COROUTINES_COROSHAPE_H
#define LLVM_TRANSFORMS_COROUTINES_COROSHAPE_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Transforms/Coroutines/CoroInstr.h"

namespace llvm {

class CallGraph;

namespace coro {

/// Identifies which coroutine ABI lowering strategy is in use.
enum class ABI {
  /// The "resume-switch" lowering, where there are separate resume and
  /// destroy functions that are shared between all suspend points.  The
  /// coroutine frame implicitly stores the resume and destroy functions,
  /// the current index, and any promise value.
  Switch,

  /// The "returned-continuation" lowering, where each suspend point creates a
  /// single continuation function that is used for both resuming and
  /// destroying.  Does not support promises.
  Retcon,

  /// The "unique returned-continuation" lowering, where each suspend point
  /// creates a single continuation function that is used for both resuming
  /// and destroying.  Does not support promises.  The function is known to
  /// suspend at most once during its execution, and the return value of
  /// the continuation is void.
  RetconOnce,

  /// The "async continuation" lowering, where each suspend point creates a
  /// single continuation function. The continuation function is available as an
  /// intrinsic.
  Async,
};

/// Holds structural coroutine intrinsics for a function and related values
/// used during CoroSplit.
struct Shape {
  /// The llvm.coro.begin intrinsic that marks the coroutine.
  CoroBeginInst *CoroBegin = nullptr;
  /// All llvm.coro.end (and related) intrinsics in the function.
  SmallVector<AnyCoroEndInst *, 4> CoroEnds;
  /// All llvm.coro.is_in_ramp intrinsics in the function.
  SmallVector<CoroIsInRampInst *, 2> CoroIsInRampInsts;
  /// All llvm.coro.size intrinsics in the function.
  SmallVector<CoroSizeInst *, 2> CoroSizes;
  /// All llvm.coro.align intrinsics in the function.
  SmallVector<CoroAlignInst *, 2> CoroAligns;
  /// All llvm.coro.suspend (and related) intrinsics in the function.
  SmallVector<AnyCoroSuspendInst *, 4> CoroSuspends;
  /// Map from suspend instructions to their execution frequency.
  ///
  /// Used for branch weights in the resume function.
  SmallDenseMap<AnyCoroSuspendInst *, uint64_t, 4> SuspendFreqs;
  /// Estimated profile execution count for the resume function, if available.
  std::optional<uint64_t> ResumeEntryCount;
  /// All llvm.coro.await.suspend.* intrinsics in the function.
  SmallVector<CoroAwaitSuspendInst *, 4> CoroAwaitSuspends;
  /// Calls that perform symmetric transfers between coroutines.
  SmallVector<CallInst *, 2> SymmetricTransfers;

  /// Calls and related values invalidated by replaceSwiftErrorOps().
  SmallVector<CallInst *, 2> SwiftErrorOps;

  /// Reset collected intrinsics and frame-related fields to an empty state.
  void clear() {
    CoroBegin = nullptr;
    CoroEnds.clear();
    CoroIsInRampInsts.clear();
    CoroSizes.clear();
    CoroAligns.clear();
    CoroSuspends.clear();
    SuspendFreqs.clear();
    ResumeEntryCount = std::nullopt;
    CoroAwaitSuspends.clear();
    SymmetricTransfers.clear();

    SwiftErrorOps.clear();

    FramePtr = nullptr;
    AllocaSpillBlock = nullptr;
  }

  /// Scan \p F and collect coroutine intrinsics for later processing.
  ///
  /// \param F The function to analyze.
  /// \param CoroFrames Output list of llvm.coro.frame intrinsics found in \p F.
  /// \param UnusedCoroSaves Output list of llvm.coro.save intrinsics that are
  ///        unused and may be cleaned up later.
  LLVM_ABI void analyze(Function &F,
                        SmallVectorImpl<CoroFrameInst *> &CoroFrames,
                        SmallVectorImpl<CoroSaveInst *> &UnusedCoroSaves);
  /// Bail out when coro.begin could not be found for \p F.
  ///
  /// \param F The function whose coroutine lowering is being abandoned.
  /// \param CoroFrames Frame intrinsics collected earlier that must be cleaned
  ///        up on failure.
  LLVM_ABI void
  invalidateCoroutine(Function &F,
                      SmallVectorImpl<CoroFrameInst *> &CoroFrames);
  /// Remove orphaned and unnecessary coroutine intrinsics.
  ///
  /// \param CoroFrames Frame intrinsics to clean up after analysis.
  /// \param UnusedCoroSaves Unused coro.save intrinsics to remove.
  LLVM_ABI void
  cleanCoroutine(SmallVectorImpl<CoroFrameInst *> &CoroFrames,
                 SmallVectorImpl<CoroSaveInst *> &UnusedCoroSaves);

  /// The ABI lowering selected for this coroutine.
  coro::ABI ABI;

  /// Required alignment of the coroutine frame.
  Align FrameAlign;
  /// Size in bytes of the coroutine frame.
  uint64_t FrameSize = 0;
  /// Pointer to the coroutine frame (instruction or argument).
  Value *FramePtr = nullptr;
  /// Block that holds spilled allocas for the coroutine frame.
  BasicBlock *AllocaSpillBlock = nullptr;

  /// Per-ABI state for the resume-switch lowering.
  struct SwitchLoweringStorage {
    /// Switch that dispatches to the correct resume point.
    SwitchInst *ResumeSwitch;
    /// Alloca holding the coroutine promise, if any.
    AllocaInst *PromiseAlloca;
    /// Entry block of the shared resume function.
    BasicBlock *ResumeEntryBlock;
    /// Integer type used for the suspend index stored in the frame.
    IntegerType *IndexType;
    /// Byte offset of the destroy function pointer in the frame.
    ///
    /// The resume function pointer always starts at offset 0.
    unsigned DestroyOffset;
    /// Alignment of the suspend index field in the frame.
    unsigned IndexAlign;
    /// Byte offset of the suspend index field in the frame.
    unsigned IndexOffset;
    /// Whether the coroutine has a final suspend point.
    bool HasFinalSuspend;
    /// Whether any coro.end is reached via an unwind path.
    bool HasUnwindCoroEnd;
    /// Whether a no-alloc elision variant of the coroutine exists.
    bool HasCoroElideNoAllocVariant;
  };

  /// Per-ABI state for returned-continuation lowerings.
  struct RetconLoweringStorage {
    /// Prototype function whose type and calling convention define resumes.
    Function *ResumePrototype;
    /// Allocator function used for the continuation storage.
    Function *Alloc;
    /// Deallocator function used for the continuation storage.
    Function *Dealloc;
    /// Block that returns the continuation result to the caller.
    BasicBlock *ReturnBlock;
    /// Whether the frame is stored inline in the continuation storage.
    bool IsFrameInlineInStorage;
  };

  /// Per-ABI state for the async-continuation lowering.
  struct AsyncLoweringStorage {
    /// Async context value passed to the coroutine.
    Value *Context;
    /// Calling convention used by async continuation functions.
    CallingConv::ID AsyncCC;
    /// Argument index of the async context parameter.
    unsigned ContextArgNo;
    /// Size in bytes of the async context header before the frame.
    uint64_t ContextHeaderSize;
    /// Required alignment of the async context.
    uint64_t ContextAlignment;
    /// Byte offset where the coroutine frame begins within the context.
    uint64_t FrameOffset;
    /// Total async context size in bytes, including the frame.
    uint64_t ContextSize;
    /// Global that holds the async function pointer descriptor.
    GlobalVariable *AsyncFuncPointer;

    /// Return the async context alignment as an Align value.
    /// \return The async context alignment.
    Align getContextAlignment() const { return Align(ContextAlignment); }
  };

  union {
    /// Switch-ABI-specific lowering fields.
    SwitchLoweringStorage SwitchLowering;
    /// Retcon-ABI-specific lowering fields.
    RetconLoweringStorage RetconLowering;
    /// Async-ABI-specific lowering fields.
    AsyncLoweringStorage AsyncLowering;
  };

  /// Return the switch-ABI coro.id intrinsic.
  /// \return The CoroIdInst associated with this coroutine.
  CoroIdInst *getSwitchCoroId() const {
    assert(ABI == coro::ABI::Switch);
    return cast<CoroIdInst>(CoroBegin->getId());
  }

  /// Return the retcon-ABI coro.id intrinsic.
  /// \return The AnyCoroIdRetconInst associated with this coroutine.
  AnyCoroIdRetconInst *getRetconCoroId() const {
    assert(ABI == coro::ABI::Retcon || ABI == coro::ABI::RetconOnce);
    return cast<AnyCoroIdRetconInst>(CoroBegin->getId());
  }

  /// Return the async-ABI coro.id intrinsic.
  /// \return The CoroIdAsyncInst associated with this coroutine.
  CoroIdAsyncInst *getAsyncCoroId() const {
    assert(ABI == coro::ABI::Async);
    return cast<CoroIdAsyncInst>(CoroBegin->getId());
  }

  /// Return the integer type used for the switch-ABI suspend index.
  /// \return The integer type of the suspend index field.
  IntegerType *getIndexType() const {
    assert(ABI == coro::ABI::Switch);
    assert(SwitchLowering.IndexType && "index type not assigned");
    return SwitchLowering.IndexType;
  }
  /// Return a constant suspend index of type getIndexType().
  ///
  /// \param Value The index value to materialize.
  /// \return A ConstantInt of the suspend index type for \p Value.
  ConstantInt *getIndex(uint64_t Value) const {
    return ConstantInt::get(getIndexType(), Value);
  }

  /// Return the pointer type of switch-ABI resume/destroy function pointers.
  /// \return The unqualified pointer type used for resume/destroy pointers.
  PointerType *getSwitchResumePointerType() const {
    assert(ABI == coro::ABI::Switch);
    assert(CoroBegin && "CoroBegin not assigned");
    return PointerType::getUnqual(CoroBegin->getContext());
  }

  /// Return the function type of the resume (or continuation) function.
  /// \return The resume/continuation function type, or nullptr for async ABI.
  FunctionType *getResumeFunctionType() const {
    switch (ABI) {
    case coro::ABI::Switch:
      return FunctionType::get(Type::getVoidTy(CoroBegin->getContext()),
                               PointerType::getUnqual(CoroBegin->getContext()),
                               /*IsVarArg=*/false);
    case coro::ABI::Retcon:
    case coro::ABI::RetconOnce:
      return RetconLowering.ResumePrototype->getFunctionType();
    case coro::ABI::Async:
      // Not used. The function type depends on the active suspend.
      return nullptr;
    }

    llvm_unreachable("Unknown coro::ABI enum");
  }

  /// Return the result types yielded by a retcon continuation after the
  /// continuation pointer.
  /// \return The continuation result types excluding the continuation pointer.
  ArrayRef<Type *> getRetconResultTypes() const {
    assert(ABI == coro::ABI::Retcon || ABI == coro::ABI::RetconOnce);
    auto FTy = CoroBegin->getFunction()->getFunctionType();

    // The safety of all this is checked by checkWFRetconPrototype.
    if (auto STy = dyn_cast<StructType>(FTy->getReturnType())) {
      return STy->elements().slice(1);
    } else {
      return ArrayRef<Type *>();
    }
  }

  /// Return the parameter types of a retcon resume prototype after the frame
  /// pointer.
  /// \return The resume prototype parameter types excluding the frame pointer.
  ArrayRef<Type *> getRetconResumeTypes() const {
    assert(ABI == coro::ABI::Retcon || ABI == coro::ABI::RetconOnce);

    // The safety of all this is checked by checkWFRetconPrototype.
    auto FTy = RetconLowering.ResumePrototype->getFunctionType();
    return FTy->params().slice(1);
  }

  /// Return the calling convention used by resume/continuation functions.
  /// \return The calling convention for resume/continuation functions.
  CallingConv::ID getResumeFunctionCC() const {
    switch (ABI) {
    case coro::ABI::Switch:
      // Use the platform C calling convention so that resume/destroy
      // function pointers stored in the coroutine frame are
      // interoperable with other compilers.
      return CallingConv::C;

    case coro::ABI::Retcon:
    case coro::ABI::RetconOnce:
      return RetconLowering.ResumePrototype->getCallingConv();
    case coro::ABI::Async:
      return AsyncLowering.AsyncCC;
    }
    llvm_unreachable("Unknown coro::ABI enum");
  }

  /// Return the promise alloca for switch ABI, or null otherwise.
  /// \return The promise alloca, or nullptr for non-switch ABIs.
  AllocaInst *getPromiseAlloca() const {
    if (ABI == coro::ABI::Switch)
      return SwitchLowering.PromiseAlloca;
    return nullptr;
  }

  /// Return an insertion point immediately after the frame pointer value.
  /// \return An iterator positioned immediately after the frame pointer.
  BasicBlock::iterator getInsertPtAfterFramePtr() const {
    if (auto *I = dyn_cast<Instruction>(FramePtr)) {
      BasicBlock::iterator It = std::next(I->getIterator());
      It.setHeadBit(true); // Copy pre-RemoveDIs behaviour.
      return It;
    }
    return cast<Argument>(FramePtr)->getParent()->getEntryBlock().begin();
  }

  /// Allocate memory according to the rules of the active lowering.
  ///
  /// \param Builder IR builder used to emit the allocation.
  /// \param Size Number of bytes to allocate.
  /// \param CG - if non-null, will be updated for the new call
  /// \return A pointer to the allocated memory.
  LLVM_ABI Value *emitAlloc(IRBuilder<> &Builder, Value *Size,
                            CallGraph *CG) const;

  /// Deallocate memory according to the rules of the active lowering.
  ///
  /// \param Builder IR builder used to emit the deallocation.
  /// \param Ptr Pointer previously returned by emitAlloc (or equivalent).
  /// \param CG - if non-null, will be updated for the new call
  LLVM_ABI void emitDealloc(IRBuilder<> &Builder, Value *Ptr,
                            CallGraph *CG) const;

  /// Construct an empty shape with no analyzed intrinsics.
  Shape() = default;
  /// Analyze and clean coroutine intrinsics in \p F.
  ///
  /// \param F The coroutine function whose shape is collected.
  explicit Shape(Function &F) {
    SmallVector<CoroFrameInst *, 8> CoroFrames;
    SmallVector<CoroSaveInst *, 2> UnusedCoroSaves;

    analyze(F, CoroFrames, UnusedCoroSaves);
    if (!CoroBegin) {
      invalidateCoroutine(F, CoroFrames);
      return;
    }
    cleanCoroutine(CoroFrames, UnusedCoroSaves);
  }
};

} // end namespace coro

} // end namespace llvm

#endif // LLVM_TRANSFORMS_COROUTINES_COROSHAPE_H
