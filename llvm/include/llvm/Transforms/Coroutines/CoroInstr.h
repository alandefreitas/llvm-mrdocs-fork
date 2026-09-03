//===-- CoroInstr.h - Coroutine Intrinsics Instruction Wrappers -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This file defines classes that make it really easy to deal with intrinsic
// functions with the isa/dyncast family of functions.  In particular, this
// allows you to do things like:
//
//     if (auto *SF = dyn_cast<CoroSubFnInst>(Inst))
//        ... SF->getFrame() ...
//
// All intrinsic function calls are instances of the call instruction, so these
// are all subclasses of the CallInst class.  Note that none of these classes
// has state or virtual methods, which is an important part of this gross/neat
// hack working.
//
// The helpful comment above is borrowed from llvm/IntrinsicInst.h, we keep
// coroutine intrinsic wrappers here since they are only used by the passes in
// the Coroutine library.
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_COROUTINES_COROINSTR_H
#define LLVM_TRANSFORMS_COROUTINES_COROINSTR_H

#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

/// This class represents the llvm.coro.subfn.addr instruction.
class CoroSubFnInst : public IntrinsicInst {
  enum { FrameArg, IndexArg };

public:
  /// Identifies which coroutine subfunction address to retrieve.
  enum ResumeKind {
    /// Sentinel used as a restart trigger rather than a real index.
    RestartTrigger = -1,
    /// Index of the resume function.
    ResumeIndex,
    /// Index of the destroy function.
    DestroyIndex,
    /// Index of the cleanup function.
    CleanupIndex,
    /// One past the last valid resume-kind index.
    IndexLast,
    /// First valid resume-kind value (same as RestartTrigger).
    IndexFirst = RestartTrigger
  };

  /// Return the coroutine frame pointer operand.
  /// \return The coroutine frame pointer operand.
  Value *getFrame() const { return getArgOperand(FrameArg); }
  /// Return which subfunction address this intrinsic requests.
  /// \return The requested subfunction address index.
  ResumeKind getIndex() const {
    int64_t Index = getRawIndex()->getValue().getSExtValue();
    assert(Index >= IndexFirst && Index < IndexLast &&
           "unexpected CoroSubFnInst index argument");
    return static_cast<ResumeKind>(Index);
  }

  /// Return the raw constant integer index operand.
  /// \return The raw constant integer index operand.
  ConstantInt *getRawIndex() const {
    return cast<ConstantInt>(getArgOperand(IndexArg));
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param I Intrinsic to test.
  /// \return true if \p I is this kind of intrinsic.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::coro_subfn_addr;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param V Value to test.
  /// \return true if \p V is this kind of value.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.coro.alloc instruction.
class CoroAllocInst : public IntrinsicInst {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param I Intrinsic to test.
  /// \return true if \p I is this kind of intrinsic.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::coro_alloc;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param V Value to test.
  /// \return true if \p V is this kind of value.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.coro.await.suspend.{void,bool,handle} instructions.
// FIXME: add callback metadata
// FIXME: make a proper IntrinisicInst. Currently this is not possible,
// because llvm.coro.await.suspend.* can be invoked.
class CoroAwaitSuspendInst : public CallBase {
  enum { AwaiterArg, FrameArg, WrapperArg };

public:
  /// Return the awaiter object operand.
  /// \return The awaiter object operand.
  Value *getAwaiter() const { return getArgOperand(AwaiterArg); }

  /// Return the coroutine frame pointer operand.
  /// \return The coroutine frame pointer operand.
  Value *getFrame() const { return getArgOperand(FrameArg); }

  /// Return the wrapper function invoked for this await-suspend.
  /// \return The wrapper function invoked for this await-suspend.
  Function *getWrapperFunction() const {
    return cast<Function>(getArgOperand(WrapperArg));
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param CB Call to test.
  /// \return true if \p CB is this kind of call.
  static bool classof(const CallBase *CB) {
    if (const Function *CF = CB->getCalledFunction()) {
      auto IID = CF->getIntrinsicID();
      return IID == Intrinsic::coro_await_suspend_void ||
             IID == Intrinsic::coro_await_suspend_bool ||
             IID == Intrinsic::coro_await_suspend_handle;
    }

    return false;
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param V Value to test.
  /// \return true if \p V is this kind of value.
  static bool classof(const Value *V) {
    return isa<CallBase>(V) && classof(cast<CallBase>(V));
  }
};

/// This represents a common base class for llvm.coro.id instructions.
class AnyCoroIdInst : public IntrinsicInst {
public:
  /// Return the associated llvm.coro.alloc, if any.
  /// \return The associated llvm.coro.alloc, if any.
  CoroAllocInst *getCoroAlloc() {
    for (User *U : users())
      if (auto *CA = dyn_cast<CoroAllocInst>(U))
        return CA;
    return nullptr;
  }

  /// Return the associated llvm.coro.begin (or custom-abi variant).
  /// \return The associated llvm.coro.begin (or custom-abi variant).
  IntrinsicInst *getCoroBegin() {
    for (User *U : users())
      if (auto *II = dyn_cast<IntrinsicInst>(U))
        if (II->getIntrinsicID() == Intrinsic::coro_begin ||
            II->getIntrinsicID() == Intrinsic::coro_begin_custom_abi)
          return II;
    llvm_unreachable("no coro.begin associated with coro.id");
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param I Intrinsic to test.
  /// \return true if \p I is this kind of intrinsic.
  static bool classof(const IntrinsicInst *I) {
    auto ID = I->getIntrinsicID();
    return ID == Intrinsic::coro_id || ID == Intrinsic::coro_id_retcon ||
           ID == Intrinsic::coro_id_retcon_once ||
           ID == Intrinsic::coro_id_async;
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param V Value to test.
  /// \return true if \p V is this kind of value.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.coro.id instruction.
class CoroIdInst : public AnyCoroIdInst {
  enum { AlignArg, PromiseArg, CoroutineArg, InfoArg };

public:
  /// Return the promise alloca, or null if there is no promise.
  /// \return The promise alloca, or null if there is no promise.
  AllocaInst *getPromise() const {
    Value *Arg = getArgOperand(PromiseArg);
    return isa<ConstantPointerNull>(Arg)
               ? nullptr
               : cast<AllocaInst>(Arg->stripPointerCasts());
  }

  /// Clear the promise operand and clean up any cast instructions.
  void clearPromise() {
    Value *Arg = getArgOperand(PromiseArg);
    setArgOperand(PromiseArg, ConstantPointerNull::get(
                                  PointerType::getUnqual(getContext())));
    if (isa<AllocaInst>(Arg))
      return;
    assert((isa<BitCastInst>(Arg) || isa<GetElementPtrInst>(Arg)) &&
           "unexpected instruction designating the promise");
    // TODO: Add a check that any remaining users of Inst are after coro.begin
    // or add code to move the users after coro.begin.
    auto *Inst = cast<Instruction>(Arg);
    if (Inst->use_empty()) {
      Inst->eraseFromParent();
      return;
    }
    Inst->moveBefore(std::next(getCoroBegin()->getIterator()));
  }

  /// Describes the Info argument of llvm.coro.id across lowering stages.
  ///
  /// Fresh from the frontend the Info argument is null. If parts of the
  /// coroutine were outlined to protect against undesirable code motion, those
  /// functions are stored in a struct literal referred to by Info (pre-split
  /// only). After the coroutine is split, resume functions are stored in an
  /// array referred to by Info.
  struct Info {
    /// Outlined helper functions before the coroutine is split.
    ConstantStruct *OutlinedParts = nullptr;
    /// Resume/destroy/cleanup functions after the coroutine is split.
    ConstantArray *Resumers = nullptr;

    /// Return true if outlined parts are present.
    /// \return true if outlined parts are present.
    bool hasOutlinedParts() const { return OutlinedParts != nullptr; }
    /// Return true if this Info describes a post-split coroutine.
    /// \return true if this Info describes a post-split coroutine.
    bool isPostSplit() const { return Resumers != nullptr; }
    /// Return true if this Info describes a pre-split coroutine.
    /// \return true if this Info describes a pre-split coroutine.
    bool isPreSplit() const { return !isPostSplit(); }
  };
  /// Decode and return the Info argument of this coro.id.
  /// \return The decoded Info argument of this coro.id.
  Info getInfo() const {
    Info Result;
    auto *GV = dyn_cast<GlobalVariable>(getRawInfo());
    if (!GV)
      return Result;

    assert(GV->isConstant() && GV->hasDefinitiveInitializer());
    Constant *Initializer = GV->getInitializer();
    if ((Result.OutlinedParts = dyn_cast<ConstantStruct>(Initializer)))
      return Result;

    Result.Resumers = cast<ConstantArray>(Initializer);
    return Result;
  }
  /// Return the raw constant Info operand with pointer casts stripped.
  /// \return The raw constant Info operand with pointer casts stripped.
  Constant *getRawInfo() const {
    return cast<Constant>(getArgOperand(InfoArg)->stripPointerCasts());
  }

  /// Set the Info operand to \p C.
  /// \param C New Info constant.
  void setInfo(Constant *C) { setArgOperand(InfoArg, C); }

  /// Return the coroutine function this coro.id identifies.
  /// \return The coroutine function this coro.id identifies.
  Function *getCoroutine() const {
    return cast<Function>(
        getArgOperand(CoroutineArg)->stripPointerCastsAndAliases());
  }
  /// Set the coroutine operand to the enclosing function.
  void setCoroutineSelf() {
    if (!isa<ConstantPointerNull>(getArgOperand(CoroutineArg)))
      assert(getCoroutine() == getFunction() && "Don't change coroutine.");
    setArgOperand(CoroutineArg, getFunction());
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param I Intrinsic to test.
  /// \return true if \p I is this kind of intrinsic.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::coro_id;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param V Value to test.
  /// \return true if \p V is this kind of value.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents either the llvm.coro.id.retcon or
/// llvm.coro.id.retcon.once instruction.
class AnyCoroIdRetconInst : public AnyCoroIdInst {
  enum { SizeArg, AlignArg, StorageArg, PrototypeArg, AllocArg, DeallocArg };

public:
  /// Assert that this retcon id intrinsic is well formed.
  LLVM_ABI void checkWellFormed() const;

  /// Return the size of the continuation storage in bytes.
  /// \return The size of the continuation storage in bytes.
  uint64_t getStorageSize() const {
    return cast<ConstantInt>(getArgOperand(SizeArg))->getZExtValue();
  }

  /// Return the required alignment of the continuation storage.
  /// \return The required alignment of the continuation storage.
  Align getStorageAlignment() const {
    return cast<ConstantInt>(getArgOperand(AlignArg))->getAlignValue();
  }

  /// Return the continuation storage buffer operand.
  /// \return The continuation storage buffer operand.
  Value *getStorage() const { return getArgOperand(StorageArg); }

  /// Return the prototype for the continuation function.
  ///
  /// The type, attributes, and calling convention of the continuation
  /// function(s) are taken from this declaration.
  /// \return The prototype for the continuation function.
  Function *getPrototype() const {
    return cast<Function>(
        getArgOperand(PrototypeArg)->stripPointerCastsAndAliases());
  }

  /// Return the function to use for allocating memory.
  /// \return The function to use for allocating memory.
  Function *getAllocFunction() const {
    return cast<Function>(
        getArgOperand(AllocArg)->stripPointerCastsAndAliases());
  }

  /// Return the function to use for deallocating memory.
  /// \return The function to use for deallocating memory.
  Function *getDeallocFunction() const {
    return cast<Function>(
        getArgOperand(DeallocArg)->stripPointerCastsAndAliases());
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param I Intrinsic to test.
  /// \return true if \p I is this kind of intrinsic.
  static bool classof(const IntrinsicInst *I) {
    auto ID = I->getIntrinsicID();
    return ID == Intrinsic::coro_id_retcon ||
           ID == Intrinsic::coro_id_retcon_once;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param V Value to test.
  /// \return true if \p V is this kind of value.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.coro.id.retcon instruction.
class CoroIdRetconInst : public AnyCoroIdRetconInst {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param I Intrinsic to test.
  /// \return true if \p I is this kind of intrinsic.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::coro_id_retcon;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param V Value to test.
  /// \return true if \p V is this kind of value.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.coro.id.retcon.once instruction.
class CoroIdRetconOnceInst : public AnyCoroIdRetconInst {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param I Intrinsic to test.
  /// \return true if \p I is this kind of intrinsic.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::coro_id_retcon_once;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param V Value to test.
  /// \return true if \p V is this kind of value.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.coro.id.async instruction.
class CoroIdAsyncInst : public AnyCoroIdInst {
  enum { SizeArg, AlignArg, StorageArg, AsyncFuncPtrArg };

public:
  /// Assert that this async id intrinsic is well formed.
  LLVM_ABI void checkWellFormed() const;

  /// The initial async function context size. The fields of which are reserved
  /// for use by the frontend. The frame will be allocated as a tail of this
  /// context.
  /// \return The initial async function context size in bytes.
  uint64_t getStorageSize() const {
    return cast<ConstantInt>(getArgOperand(SizeArg))->getZExtValue();
  }

  /// The alignment of the initial async function context.
  /// \return The alignment of the initial async function context.
  Align getStorageAlignment() const {
    return cast<ConstantInt>(getArgOperand(AlignArg))->getAlignValue();
  }

  /// The async context parameter.
  /// \return The async context parameter.
  Value *getStorage() const {
    return getParent()->getParent()->getArg(getStorageArgumentIndex());
  }

  /// Return the function-argument index of the async context.
  /// \return The function-argument index of the async context.
  unsigned getStorageArgumentIndex() const {
    auto *Arg = cast<ConstantInt>(getArgOperand(StorageArg));
    return Arg->getZExtValue();
  }

  /// Return the async function pointer address.
  ///
  /// This should be the address of a async function pointer struct for the current async function. struct async_function_pointer { uint32_t context_size; uint32_t relative_async_function_pointer; };
  /// \return The async function pointer address.
  GlobalVariable *getAsyncFunctionPointer() const {
    return cast<GlobalVariable>(
        getArgOperand(AsyncFuncPtrArg)->stripPointerCasts());
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param I Intrinsic to test.
  /// \return true if \p I is this kind of intrinsic.
  static bool classof(const IntrinsicInst *I) {
    auto ID = I->getIntrinsicID();
    return ID == Intrinsic::coro_id_async;
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param V Value to test.
  /// \return true if \p V is this kind of value.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.coro.context.alloc instruction.
class CoroAsyncContextAllocInst : public IntrinsicInst {
  enum { AsyncFuncPtrArg };

public:
  /// Return the async function pointer global for this allocation.
  /// \return The async function pointer global for this allocation.
  GlobalVariable *getAsyncFunctionPointer() const {
    return cast<GlobalVariable>(
        getArgOperand(AsyncFuncPtrArg)->stripPointerCasts());
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param I Intrinsic to test.
  /// \return true if \p I is this kind of intrinsic.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::coro_async_context_alloc;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param V Value to test.
  /// \return true if \p V is this kind of value.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.coro.context.dealloc instruction.
class CoroAsyncContextDeallocInst : public IntrinsicInst {
  enum { AsyncContextArg };

public:
  /// Return the async context pointer being deallocated.
  /// \return The async context pointer being deallocated.
  Value *getAsyncContext() const {
    return getArgOperand(AsyncContextArg)->stripPointerCasts();
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param I Intrinsic to test.
  /// \return true if \p I is this kind of intrinsic.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::coro_async_context_dealloc;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param V Value to test.
  /// \return true if \p V is this kind of value.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.coro.async.resume instruction.
/// During lowering this is replaced by the resume function of a suspend point
/// (the continuation function).
class CoroAsyncResumeInst : public IntrinsicInst {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param I Intrinsic to test.
  /// \return true if \p I is this kind of intrinsic.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::coro_async_resume;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param V Value to test.
  /// \return true if \p V is this kind of value.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.coro.async.size.replace instruction.
class CoroAsyncSizeReplace : public IntrinsicInst {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param I Intrinsic to test.
  /// \return true if \p I is this kind of intrinsic.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::coro_async_size_replace;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param V Value to test.
  /// \return true if \p V is this kind of value.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.coro.frame instruction.
class CoroFrameInst : public IntrinsicInst {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param I Intrinsic to test.
  /// \return true if \p I is this kind of intrinsic.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::coro_frame;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param V Value to test.
  /// \return true if \p V is this kind of value.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.coro.is_in_ramp instruction.
class CoroIsInRampInst : public IntrinsicInst {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param I Intrinsic to test.
  /// \return true if \p I is this kind of intrinsic.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::coro_is_in_ramp;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param V Value to test.
  /// \return true if \p V is this kind of value.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.coro.free instruction.
class CoroFreeInst : public IntrinsicInst {
  enum { IdArg, FrameArg };

public:
  /// Return the coroutine frame pointer being freed.
  /// \return The coroutine frame pointer being freed.
  Value *getFrame() const { return getArgOperand(FrameArg); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param I Intrinsic to test.
  /// \return true if \p I is this kind of intrinsic.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::coro_free;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param V Value to test.
  /// \return true if \p V is this kind of value.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.coro.dead instruction.
class CoroDeadInst : public IntrinsicInst {
public:
  /// Return the coroutine frame marked dead.
  /// \return The coroutine frame marked dead.
  Value *getFrame() const { return getArgOperand(0); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param I Intrinsic to test.
  /// \return true if \p I is this kind of intrinsic.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::coro_dead;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param V Value to test.
  /// \return true if \p V is this kind of value.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This class represents the llvm.coro.begin or llvm.coro.begin.custom.abi
/// instructions.
class CoroBeginInst : public IntrinsicInst {
  enum { IdArg, MemArg, CustomABIArg };

public:
  /// Return the associated coro.id intrinsic.
  /// \return The associated coro.id intrinsic.
  AnyCoroIdInst *getId() const {
    return cast<AnyCoroIdInst>(getArgOperand(IdArg));
  }

  /// Return true if this is llvm.coro.begin.custom.abi.
  /// \return true if this is llvm.coro.begin.custom.abi.
  bool hasCustomABI() const {
    return getIntrinsicID() == Intrinsic::coro_begin_custom_abi;
  }

  /// Return the custom ABI tag for this coro.begin.
  /// \return The custom ABI tag for this coro.begin.
  int getCustomABI() const {
    return cast<ConstantInt>(getArgOperand(CustomABIArg))->getZExtValue();
  }

  /// Return the memory/frame pointer operand.
  /// \return The memory/frame pointer operand.
  Value *getMem() const { return getArgOperand(MemArg); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param I Intrinsic to test.
  /// \return true if \p I is this kind of intrinsic.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::coro_begin ||
           I->getIntrinsicID() == Intrinsic::coro_begin_custom_abi;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param V Value to test.
  /// \return true if \p V is this kind of value.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.coro.save instruction.
class CoroSaveInst : public IntrinsicInst {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param I Intrinsic to test.
  /// \return true if \p I is this kind of intrinsic.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::coro_save;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param V Value to test.
  /// \return true if \p V is this kind of value.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.coro.promise instruction.
class CoroPromiseInst : public IntrinsicInst {
  enum { FrameArg, AlignArg, FromArg };

public:
  /// Are we translating from the frame to the promise (false) or from
  /// the promise to the frame (true)?
  /// \return true if translating from the promise to the frame.
  bool isFromPromise() const {
    return cast<Constant>(getArgOperand(FromArg))->isOneValue();
  }

  /// The required alignment of the promise.  This must match the
  /// alignment of the promise alloca in the coroutine.
  /// \return The required alignment of the promise.
  Align getAlignment() const {
    return cast<ConstantInt>(getArgOperand(AlignArg))->getAlignValue();
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param I Intrinsic to test.
  /// \return true if \p I is this kind of intrinsic.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::coro_promise;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param V Value to test.
  /// \return true if \p V is this kind of value.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// Common base class for llvm.coro.suspend.* instructions.
class AnyCoroSuspendInst : public IntrinsicInst {
public:
  /// Return the associated coro.save, or null if none.
  /// \return The associated coro.save, or null if none.
  CoroSaveInst *getCoroSave() const;

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param I Intrinsic to test.
  /// \return true if \p I is this kind of intrinsic.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::coro_suspend ||
           I->getIntrinsicID() == Intrinsic::coro_suspend_async ||
           I->getIntrinsicID() == Intrinsic::coro_suspend_retcon;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param V Value to test.
  /// \return true if \p V is this kind of value.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.coro.suspend instruction.
class CoroSuspendInst : public AnyCoroSuspendInst {
  enum { SaveArg, FinalArg };

public:
  /// Return the associated coro.save, or null for none.
  /// \return The associated coro.save, or null for none.
  CoroSaveInst *getCoroSave() const {
    Value *Arg = getArgOperand(SaveArg);
    if (auto *SI = dyn_cast<CoroSaveInst>(Arg))
      return SI;
    assert(isa<ConstantTokenNone>(Arg));
    return nullptr;
  }

  /// Return true if this is the final suspend point.
  /// \return true if this is the final suspend point.
  bool isFinal() const {
    return cast<Constant>(getArgOperand(FinalArg))->isOneValue();
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param I Intrinsic to test.
  /// \return true if \p I is this kind of intrinsic.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::coro_suspend;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param V Value to test.
  /// \return true if \p V is this kind of value.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// Return the associated coro.save for switch-style suspends, else null.
/// \return The associated coro.save for switch-style suspends, else null.
inline CoroSaveInst *AnyCoroSuspendInst::getCoroSave() const {
  if (auto Suspend = dyn_cast<CoroSuspendInst>(this))
    return Suspend->getCoroSave();
  return nullptr;
}

/// This represents the llvm.coro.suspend.async instruction.
class CoroSuspendAsyncInst : public AnyCoroSuspendInst {
public:
  /// Operand indices for llvm.coro.suspend.async.
  enum {
    /// Operand holding the async context argument index.
    StorageArgNoArg,
    /// Operand holding the resume/continuation function.
    ResumeFunctionArg,
    /// Operand holding the async context projection function.
    AsyncContextProjectionArg,
    /// Operand holding the must-tail call function.
    MustTailCallFuncArg
  };

  /// Assert that this async suspend intrinsic is well formed.
  LLVM_ABI void checkWellFormed() const;

  /// Return the function-argument index of the async context.
  /// \return The function-argument index of the async context.
  unsigned getStorageArgumentIndex() const {
    auto *Arg = cast<ConstantInt>(getArgOperand(StorageArgNoArg));
    return Arg->getZExtValue();
  }

  /// Return the async context projection function.
  /// \return The async context projection function.
  Function *getAsyncContextProjectionFunction() const {
    return cast<Function>(
        getArgOperand(AsyncContextProjectionArg)->stripPointerCasts());
  }

  /// Return the resume function for this async suspend.
  /// \return The resume function for this async suspend.
  CoroAsyncResumeInst *getResumeFunction() const {
    return cast<CoroAsyncResumeInst>(
        getArgOperand(ResumeFunctionArg)->stripPointerCasts());
  }

  /// Return the function that must be tail-called at this suspend.
  /// \return The function that must be tail-called at this suspend.
  Function *getMustTailCallFunction() const {
    return cast<Function>(
        getArgOperand(MustTailCallFuncArg)->stripPointerCasts());
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param I Intrinsic to test.
  /// \return true if \p I is this kind of intrinsic.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::coro_suspend_async;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param V Value to test.
  /// \return true if \p V is this kind of value.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.coro.suspend.retcon instruction.
class CoroSuspendRetconInst : public AnyCoroSuspendInst {
public:
  /// Return an iterator to the first value operand.
  /// \return An iterator to the first value operand.
  op_iterator value_begin() { return arg_begin(); }
  /// Return an iterator to the first value operand.
  /// \return An iterator to the first value operand.
  const_op_iterator value_begin() const { return arg_begin(); }

  /// Return an iterator past the last value operand.
  /// \return An iterator past the last value operand.
  op_iterator value_end() { return arg_end(); }
  /// Return an iterator past the last value operand.
  /// \return An iterator past the last value operand.
  const_op_iterator value_end() const { return arg_end(); }

  /// Return the range of value operands yielded at this suspend.
  /// \return The range of value operands yielded at this suspend.
  iterator_range<op_iterator> value_operands() {
    return make_range(value_begin(), value_end());
  }
  /// Return the range of value operands yielded at this suspend.
  /// \return The range of value operands yielded at this suspend.
  iterator_range<const_op_iterator> value_operands() const {
    return make_range(value_begin(), value_end());
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param I Intrinsic to test.
  /// \return true if \p I is this kind of intrinsic.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::coro_suspend_retcon;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param V Value to test.
  /// \return true if \p V is this kind of value.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.coro.size instruction.
class CoroSizeInst : public IntrinsicInst {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param I Intrinsic to test.
  /// \return true if \p I is this kind of intrinsic.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::coro_size;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param V Value to test.
  /// \return true if \p V is this kind of value.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.coro.align instruction.
class CoroAlignInst : public IntrinsicInst {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param I Intrinsic to test.
  /// \return true if \p I is this kind of intrinsic.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::coro_align;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param V Value to test.
  /// \return true if \p V is this kind of value.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.end.results instruction.
class CoroEndResults : public IntrinsicInst {
public:
  /// Return an iterator to the first return-value operand.
  /// \return An iterator to the first return-value operand.
  op_iterator retval_begin() { return arg_begin(); }
  /// Return an iterator to the first return-value operand.
  /// \return An iterator to the first return-value operand.
  const_op_iterator retval_begin() const { return arg_begin(); }

  /// Return an iterator past the last return-value operand.
  /// \return An iterator past the last return-value operand.
  op_iterator retval_end() { return arg_end(); }
  /// Return an iterator past the last return-value operand.
  /// \return An iterator past the last return-value operand.
  const_op_iterator retval_end() const { return arg_end(); }

  /// Return the range of return-value operands.
  /// \return The range of return-value operands.
  iterator_range<op_iterator> return_values() {
    return make_range(retval_begin(), retval_end());
  }
  /// Return the range of return-value operands.
  /// \return The range of return-value operands.
  iterator_range<const_op_iterator> return_values() const {
    return make_range(retval_begin(), retval_end());
  }

  /// Return the number of return-value operands.
  /// \return The number of return-value operands.
  unsigned numReturns() const {
    return std::distance(retval_begin(), retval_end());
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param I Intrinsic to test.
  /// \return true if \p I is this kind of intrinsic.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::coro_end_results;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param V Value to test.
  /// \return true if \p V is this kind of value.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// Common base class for llvm.coro.end and llvm.coro.end.async.
class AnyCoroEndInst : public IntrinsicInst {
  enum { FrameArg, UnwindArg, TokenArg };

public:
  /// Return true if this end is a fallthrough (non-unwind) path.
  /// \return true if this end is a fallthrough (non-unwind) path.
  bool isFallthrough() const { return !isUnwind(); }
  /// Return true if this end is on an unwind path.
  /// \return true if this end is on an unwind path.
  bool isUnwind() const {
    return cast<Constant>(getArgOperand(UnwindArg))->isOneValue();
  }

  /// Return true if this end carries result values via a token.
  /// \return true if this end carries result values via a token.
  bool hasResults() const {
    return !isa<ConstantTokenNone>(getArgOperand(TokenArg));
  }

  /// Return the coro.end.results intrinsic providing return values.
  /// \return The coro.end.results intrinsic providing return values.
  CoroEndResults *getResults() const {
    assert(hasResults());
    return cast<CoroEndResults>(getArgOperand(TokenArg));
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param I Intrinsic to test.
  /// \return true if \p I is this kind of intrinsic.
  static bool classof(const IntrinsicInst *I) {
    auto ID = I->getIntrinsicID();
    return ID == Intrinsic::coro_end || ID == Intrinsic::coro_end_async;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param V Value to test.
  /// \return true if \p V is this kind of value.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.coro.end instruction.
class CoroEndInst : public AnyCoroEndInst {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param I Intrinsic to test.
  /// \return true if \p I is this kind of intrinsic.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::coro_end;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param V Value to test.
  /// \return true if \p V is this kind of value.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.coro.end instruction.
class CoroAsyncEndInst : public AnyCoroEndInst {
  enum { FrameArg, UnwindArg, MustTailCallFuncArg };

public:
  /// Assert that this async end intrinsic is well formed.
  LLVM_ABI void checkWellFormed() const;

  /// Return the must-tail call function, or null if absent.
  /// \return The must-tail call function, or null if absent.
  Function *getMustTailCallFunction() const {
    if (arg_size() < 3)
      return nullptr;

    return cast<Function>(
        getArgOperand(MustTailCallFuncArg)->stripPointerCasts());
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param I Intrinsic to test.
  /// \return true if \p I is this kind of intrinsic.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::coro_end_async;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param V Value to test.
  /// \return true if \p V is this kind of value.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.coro.alloca.alloc instruction.
class CoroAllocaAllocInst : public IntrinsicInst {
  enum { SizeArg, AlignArg };

public:
  /// Return the size operand of this dynamic alloca.
  /// \return The size operand of this dynamic alloca.
  Value *getSize() const { return getArgOperand(SizeArg); }
  /// Return the alignment of this dynamic alloca.
  /// \return The alignment of this dynamic alloca.
  Align getAlignment() const {
    return cast<ConstantInt>(getArgOperand(AlignArg))->getAlignValue();
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param I Intrinsic to test.
  /// \return true if \p I is this kind of intrinsic.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::coro_alloca_alloc;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param V Value to test.
  /// \return true if \p V is this kind of value.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.coro.alloca.get instruction.
class CoroAllocaGetInst : public IntrinsicInst {
  enum { AllocArg };

public:
  /// Return the corresponding coro.alloca.alloc intrinsic.
  /// \return The corresponding coro.alloca.alloc intrinsic.
  CoroAllocaAllocInst *getAlloc() const {
    return cast<CoroAllocaAllocInst>(getArgOperand(AllocArg));
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param I Intrinsic to test.
  /// \return true if \p I is this kind of intrinsic.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::coro_alloca_get;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param V Value to test.
  /// \return true if \p V is this kind of value.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.coro.alloca.free instruction.
class CoroAllocaFreeInst : public IntrinsicInst {
  enum { AllocArg };

public:
  /// Return the corresponding coro.alloca.alloc intrinsic.
  /// \return The corresponding coro.alloca.alloc intrinsic.
  CoroAllocaAllocInst *getAlloc() const {
    return cast<CoroAllocaAllocInst>(getArgOperand(AllocArg));
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param I Intrinsic to test.
  /// \return true if \p I is this kind of intrinsic.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::coro_alloca_free;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param V Value to test.
  /// \return true if \p V is this kind of value.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

} // End namespace llvm.

#endif // LLVM_TRANSFORMS_COROUTINES_COROINSTR_H
