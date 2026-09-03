//===- llvm/IRBuilder.h - Builder for LLVM Instructions ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the IRBuilder class, which is used as a convenient way
// to create LLVM instructions with a consistent and simplified interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_IRBUILDER_H
#define LLVM_IR_IRBUILDER_H

#include "llvm-c/Types.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/ConstantFolder.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/FPEnv.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/CBindingWrapping.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include <cassert>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

namespace llvm {

class APInt;
class Use;

/// Default helper that inserts IRBuilder-created instructions.
///
/// Called whenever an instruction is created by IRBuilder and needs to be
/// inserted. By default, this inserts the instruction at the insertion point.
class LLVM_ABI IRBuilderDefaultInserter {
public:
  /// Destroy the IRBuilderDefaultInserter.
  virtual ~IRBuilderDefaultInserter();

  /// Insert an instruction at the builder insert point.
  /// \param I Instruction to insert, act on, or take debug location from.
  /// \param Name Name of the new instruction or value.
  /// \param InsertPt Insertion-point iterator for the new instruction.
  virtual void InsertHelper(Instruction *I, const Twine &Name,
                            BasicBlock::iterator InsertPt) const {
    if (InsertPt.isValid())
      I->insertInto(InsertPt.getNodeParent(), InsertPt);
    I->setName(Name);
  }
};

/// Provides an 'InsertHelper' that calls a user-provided callback after
/// performing the default insertion.
class LLVM_ABI IRBuilderCallbackInserter : public IRBuilderDefaultInserter {
  /// D.
  std::function<void(Instruction *)> Callback;

public:
  /// Destroy the IRBuilderCallbackInserter.
  ~IRBuilderCallbackInserter() override;

  /// Construct an IRBuilderCallbackInserter.
  /// \param Callback Callback invoked after each inserted instruction.
  IRBuilderCallbackInserter(std::function<void(Instruction *)> Callback)
      : Callback(std::move(Callback)) {}

  /// Insert an instruction at the builder insert point.
  /// \param I Instruction to insert, act on, or take debug location from.
  /// \param Name Name of the new instruction or value.
  /// \param InsertPt Insertion-point iterator for the new instruction.
  void InsertHelper(Instruction *I, const Twine &Name,
                    BasicBlock::iterator InsertPt) const override {
    IRBuilderDefaultInserter::InsertHelper(I, Name, InsertPt);
    Callback(I);
  }
};

/// This provides a helper for copying FMF from an instruction or setting
/// specified flags.
class FMFSource {
  std::optional<FastMathFlags> FMF;
  /// Construct an FMFSource.
public:
  FMFSource() = default;
  /// Construct an FMFSource.
  /// \param Source Source instruction for FMF, or pointer passed to free.
  FMFSource(Instruction *Source) {
    if (Source)
      FMF = Source->getFastMathFlags();
  }
  /// Construct an FMFSource.
  /// \param FMF Fast-math flags to store.
  FMFSource(FastMathFlags FMF) : FMF(FMF) {}
  /// Return the stored fast-math flags, or the default.
  /// \param Default Default fast-math flags when none are stored.
  /// @return The stored fast-math flags, or \p Default when none are stored.
  FastMathFlags get(FastMathFlags Default) const {
    return FMF.value_or(Default);
  }
  /// Intersect the FMF from two instructions.
  /// \param A First value whose fast-math flags are intersected.
  /// \param B Builder being guarded, or second FMF value.
  /// @return An FMFSource holding the intersection of both values' flags.
  static FMFSource intersect(Value *A, Value *B) {
    return FMFSource(cast<FPMathOperator>(A)->getFastMathFlags() &
                     cast<FPMathOperator>(B)->getFastMathFlags());
  }
};

/// Common base class shared among various IRBuilders.
class IRBuilderBase {
  /// The DebugLoc that will be applied to instructions inserted by this
  /// builder.
  DebugLoc StoredDL;

protected:
  /// Basic block used as the current insert point.
  BasicBlock *BB;
  /// Iterator within BB where new instructions are inserted.
  BasicBlock::iterator InsertPt;
  /// LLVM context used to create IR objects.
  LLVMContext &Context;
  /// Constant folder used when creating instructions.
  const IRBuilderFolder &Folder;
  /// Helper that inserts newly created instructions.
  const IRBuilderDefaultInserter &Inserter;

  /// Default floating-point math metadata tag.
  MDNode *DefaultFPMathTag;
  /// Fast-math flags applied to created floating-point ops.
  FastMathFlags FMF;

  /// Whether CreateF* builds constrained FP intrinsics.
  bool IsFPConstrained = false;
  /// Default exception behavior for constrained FP.
  fp::ExceptionBehavior DefaultConstrainedExcept = fp::ebStrict;
  /// Default rounding mode for constrained FP.
  RoundingMode DefaultConstrainedRounding = RoundingMode::Dynamic;

  /// Default operand bundles attached to new calls.
  ArrayRef<OperandBundleDef> DefaultOperandBundles;
  /// Construct an IRBuilderBase.
  /// \param context LLVM context used to create IR objects.
  /// \param Folder Constant folder used by this builder.
  /// \param Inserter Custom instruction inserter.
  /// \param FPMathTag Floating-point math metadata tag.
  /// \param OpBundles Operand bundles attached to the call.
public:
  IRBuilderBase(LLVMContext &context, const IRBuilderFolder &Folder,
                const IRBuilderDefaultInserter &Inserter, MDNode *FPMathTag,
                ArrayRef<OperandBundleDef> OpBundles)
      : Context(context), Folder(Folder), Inserter(Inserter),
        DefaultFPMathTag(FPMathTag), DefaultOperandBundles(OpBundles) {
    ClearInsertionPoint();
  }

  /// Insert and return the specified instruction.
  /// \param I Instruction to insert at the current insertion point.
  /// \param Name Optional name to assign to the instruction.
  /// @return The inserted instruction.
  template<typename InstTy>
  InstTy *Insert(InstTy *I, const Twine &Name = "") const {
    Inserter.InsertHelper(I, Name, InsertPt);
    SetInstDebugLocation(I);
    return I;
  }

  /// No-op overload to handle constants.
  /// \param C LLVM context, or integer constant value.
  /// \param Name Name of the new instruction or value.
  /// @return The inserted instruction or value.
  Constant *Insert(Constant *C, const Twine &Name = "") const {
    return C;
  }

  /// Insert a value using the current inserter.
  /// \param V Input value.
  /// \param Name Name of the new instruction or value.
  /// @return The inserted instruction or value.
  Value *Insert(Value *V, const Twine &Name = "") const {
    if (Instruction *I = dyn_cast<Instruction>(V))
      return Insert(I, Name);
    assert(isa<Constant>(V));
    return V;
  }

  //===--------------------------------------------------------------------===//
  // Builder configuration methods
  //===--------------------------------------------------------------------===//

  /// Clear the insertion point: created instructions will not be
  /// inserted into a block.
  void ClearInsertionPoint() {
    BB = nullptr;
    InsertPt = BasicBlock::iterator();
  }

  /// Return the basic block of the current insert point.
  /// @return The basic block of the current insert point.
  BasicBlock *GetInsertBlock() const { return BB; }
  /// Return the iterator of the current insert point.
  /// @return The iterator of the current insert point.
  BasicBlock::iterator GetInsertPoint() const { return InsertPt; }
  /// Return the LLVM context used by this builder.
  /// @return The LLVM context used by this builder.
  LLVMContext &getContext() const { return Context; }

  /// This specifies that created instructions should be appended to the
  /// end of the specified block.
  /// \param TheBB Basic block used as the insert point.
  void SetInsertPoint(BasicBlock *TheBB) {
    BB = TheBB;
    InsertPt = BB->end();
  }

  /// This specifies that created instructions should be inserted before
  /// the specified instruction.
  /// \param I Instruction to insert, act on, or take debug location from.
  void SetInsertPoint(Instruction *I) {
    BB = I->getParent();
    InsertPt = I->getIterator();
    assert(InsertPt != BB->end() && "Can't read debug loc from end()");
    SetCurrentDebugLocation(I->getStableDebugLoc());
  }

  /// This specifies that created instructions should be inserted at the
  /// specified point.
  /// \param TheBB Basic block used as the insert point.
  /// \param IP Instruction or saved insert point.
  void SetInsertPoint(BasicBlock *TheBB, BasicBlock::iterator IP) {
    BB = TheBB;
    InsertPt = IP;
    if (IP != TheBB->end())
      SetCurrentDebugLocation(IP->getStableDebugLoc());
  }

  /// This specifies that created instructions should be inserted at
  /// the specified point, but also requires that \p IP is dereferencable.
  /// \param IP Instruction or saved insert point.
  void SetInsertPoint(BasicBlock::iterator IP) {
    BB = IP->getParent();
    InsertPt = IP;
    SetCurrentDebugLocation(IP->getStableDebugLoc());
  }

  /// Set the insert point past entry-block static allocas.
  ///
  /// Created instructions are inserted at the beginning of the specified
  /// function, after already existing static alloca instructions that are at
  /// the start.
  /// \param F Function whose entry block receives the insert point.
  void SetInsertPointPastAllocas(Function *F) {
    BB = &F->getEntryBlock();
    InsertPt = BB->getFirstNonPHIOrDbgOrAlloca();
  }

  /// Set location information used by debugging information.
  /// \param L Debug location to apply to created instructions.
  void SetCurrentDebugLocation(const DebugLoc &L) {
    // For !dbg metadata attachments, we use DebugLoc instead of the raw MDNode
    // to include optional introspection data for use in Debugify.
    StoredDL = L;
  }

  /// Set location information used by debugging information.
  /// \param L Debug location to apply to created instructions.
  void SetCurrentDebugLocation(DebugLoc &&L) {
    // For !dbg metadata attachments, we use DebugLoc instead of the raw MDNode
    // to include optional introspection data for use in Debugify.
    StoredDL = std::move(L);
  }

  /// Get location information used by debugging information.
  /// @return The current debug location.
  LLVM_ABI DebugLoc getCurrentDebugLocation() const;

  /// If this builder has a current debug location, set it on the
  /// specified instruction.
  /// \param I Instruction to insert, act on, or take debug location from.
  LLVM_ABI void SetInstDebugLocation(Instruction *I) const;

  /// Get the return type of the current function that we're emitting
  /// into.
  /// @return The return type of the function being built.
  LLVM_ABI Type *getCurrentFunctionReturnType() const;

  /// InsertPoint - A saved insertion point.
  class InsertPoint {
    BasicBlock *Block = nullptr;
    BasicBlock::iterator Point;
    /// Creates a new insertion point which doesn't point to anything.
  public:
    InsertPoint() = default;

    /// Creates a new insertion point at the given location.
    /// \param InsertBlock Basic block of the insert point.
    /// \param InsertPoint Iterator insert point within the block.
    InsertPoint(BasicBlock *InsertBlock, BasicBlock::iterator InsertPoint)
        : Block(InsertBlock), Point(InsertPoint) {}

    /// Returns true if this insert point is set.
    /// @return True if this insert point is set.
    bool isSet() const { return (Block != nullptr); }

    /// Return the basic block of this insert point.
    /// @return The basic block of this insert point.
    BasicBlock *getBlock() const { return Block; }
    /// Return the iterator of this insert point.
    /// @return The iterator of this insert point.
    BasicBlock::iterator getPoint() const { return Point; }
  };

  /// Returns the current insert point.
  /// @return The saved insertion point.
  InsertPoint saveIP() const {
    return InsertPoint(GetInsertBlock(), GetInsertPoint());
  }

  /// Returns the current insert point, clearing it in the process.
  /// @return The previously active insertion point.
  InsertPoint saveAndClearIP() {
    InsertPoint IP(GetInsertBlock(), GetInsertPoint());
    ClearInsertionPoint();
    return IP;
  }

  /// Sets the current insert point to a previously-saved location.
  /// \param IP Instruction or saved insert point.
  void restoreIP(InsertPoint IP) {
    if (IP.isSet())
      SetInsertPoint(IP.getBlock(), IP.getPoint());
    else
      ClearInsertionPoint();
  }

  /// Get the floating point math metadata being used.
  /// @return The default floating-point math metadata tag.
  MDNode *getDefaultFPMathTag() const { return DefaultFPMathTag; }

  /// Get the flags to be applied to created floating point ops
  /// @return The fast-math flags used by this builder.
  FastMathFlags getFastMathFlags() const { return FMF; }

  /// Return the fast-math flags used by this builder.
  /// @return The fast-math flags used by this builder.
  FastMathFlags &getFastMathFlags() { return FMF; }

  /// Clear the fast-math flags.
  void clearFastMathFlags() { FMF.clear(); }

  /// Set the floating point math metadata to be used.
  /// \param FPMathTag Floating-point math metadata tag.
  void setDefaultFPMathTag(MDNode *FPMathTag) { DefaultFPMathTag = FPMathTag; }

  /// Set the fast-math flags to be used with generated fp-math operators
  /// \param NewFMF Fast-math flags to apply to created FP ops.
  void setFastMathFlags(FastMathFlags NewFMF) { FMF = NewFMF; }

  /// Enable or disable constrained floating-point math.
  ///
  /// When enabled the CreateF<op>() calls instead create constrained floating
  /// point intrinsic calls. Fast math flags are unaffected by this setting.
  /// \param IsCon Whether constrained floating-point math is enabled.
  void setIsFPConstrained(bool IsCon) { IsFPConstrained = IsCon; }

  /// Query for the use of constrained floating point math
  /// @return True if constrained floating-point math is enabled.
  bool getIsFPConstrained() { return IsFPConstrained; }

  /// Set the exception handling to be used with constrained floating point
  /// \param NewExcept Default constrained-FP exception behavior.
  void setDefaultConstrainedExcept(fp::ExceptionBehavior NewExcept) {
#ifndef NDEBUG
    std::optional<StringRef> ExceptStr =
        convertExceptionBehaviorToStr(NewExcept);
    assert(ExceptStr && "Garbage strict exception behavior!");
#endif
    DefaultConstrainedExcept = NewExcept;
  }

  /// Set the rounding mode handling to be used with constrained floating point
  /// \param NewRounding Default constrained-FP rounding mode.
  void setDefaultConstrainedRounding(RoundingMode NewRounding) {
#ifndef NDEBUG
    std::optional<StringRef> RoundingStr =
        convertRoundingModeToStr(NewRounding);
    assert(RoundingStr && "Garbage strict rounding mode!");
#endif
    DefaultConstrainedRounding = NewRounding;
  }

  /// Get the exception handling used with constrained floating point
  /// @return The default constrained-FP exception behavior.
  fp::ExceptionBehavior getDefaultConstrainedExcept() {
    return DefaultConstrainedExcept;
  }

  /// Get the rounding mode handling used with constrained floating point
  /// @return The default constrained-FP rounding mode.
  RoundingMode getDefaultConstrainedRounding() {
    return DefaultConstrainedRounding;
  }

  /// Add the StrictFP attribute to the current function.
  void setConstrainedFPFunctionAttr() {
    assert(BB && "Must have a basic block to set any function attributes!");

    Function *F = BB->getParent();
    if (!F->hasFnAttribute(Attribute::StrictFP)) {
      F->addFnAttr(Attribute::StrictFP);
    }
  }

  /// Add the StrictFP attribute to a call instruction.
  /// \param I Instruction to insert, act on, or take debug location from.
  void setConstrainedFPCallAttr(CallBase *I) {
    I->addFnAttr(Attribute::StrictFP);
  }

  /// Set the default operand bundles for new calls.
  /// \param OpBundles Operand bundles attached to the call.
  void setDefaultOperandBundles(ArrayRef<OperandBundleDef> OpBundles) {
    DefaultOperandBundles = OpBundles;
  }

  //===--------------------------------------------------------------------===//
  // RAII helpers.
  //===--------------------------------------------------------------------===//

  // RAII object that stores the current insertion point and restores it
  // when the object is destroyed. This includes the debug location.
  /// RAII guard that restores the builder insert point.
  class InsertPointGuard {
    IRBuilderBase &Builder;
    AssertingVH<BasicBlock> Block;
    BasicBlock::iterator Point;
    DebugLoc DbgLoc;
    /// Construct an InsertPointGuard.
    /// \param B Builder being guarded, or second FMF value.
  public:
    InsertPointGuard(IRBuilderBase &B)
        : Builder(B), Block(B.GetInsertBlock()), Point(B.GetInsertPoint()),
          DbgLoc(B.getCurrentDebugLocation()) {}

    /// Construct an InsertPointGuard.
    /// \param Unused Unused copy source (deleted).
    InsertPointGuard(const InsertPointGuard &Unused) = delete;
    /// Copy assignment is deleted.
    /// \param Unused Unused copy source (deleted).
    InsertPointGuard &operator=(const InsertPointGuard &Unused) = delete;

    /// Destroy the InsertPointGuard.
    ~InsertPointGuard() {
      Builder.restoreIP(InsertPoint(Block, Point));
      Builder.SetCurrentDebugLocation(DbgLoc);
    }
  };

  // RAII object that stores the current fast math settings and restores
  // them when the object is destroyed.
  /// RAII guard that restores fast-math builder settings.
  class FastMathFlagGuard {
    IRBuilderBase &Builder;
    FastMathFlags FMF;
    MDNode *FPMathTag;
    bool IsFPConstrained;
    fp::ExceptionBehavior DefaultConstrainedExcept;
    RoundingMode DefaultConstrainedRounding;
    /// Construct a FastMathFlagGuard.
    /// \param B Builder being guarded, or second FMF value.
  public:
    FastMathFlagGuard(IRBuilderBase &B)
        : Builder(B), FMF(B.FMF), FPMathTag(B.DefaultFPMathTag),
          IsFPConstrained(B.IsFPConstrained),
          DefaultConstrainedExcept(B.DefaultConstrainedExcept),
          DefaultConstrainedRounding(B.DefaultConstrainedRounding) {}

    /// Construct a FastMathFlagGuard.
    /// \param Unused Unused copy source (deleted).
    FastMathFlagGuard(const FastMathFlagGuard &Unused) = delete;
    /// Copy assignment is deleted.
    /// \param Unused Unused copy source (deleted).
    FastMathFlagGuard &operator=(const FastMathFlagGuard &Unused) = delete;

    /// Destroy the FastMathFlagGuard.
    ~FastMathFlagGuard() {
      Builder.FMF = FMF;
      Builder.DefaultFPMathTag = FPMathTag;
      Builder.IsFPConstrained = IsFPConstrained;
      Builder.DefaultConstrainedExcept = DefaultConstrainedExcept;
      Builder.DefaultConstrainedRounding = DefaultConstrainedRounding;
    }
  };

  // RAII object that stores the current default operand bundles and restores
  // them when the object is destroyed.
  /// RAII guard that restores default operand bundles.
  class OperandBundlesGuard {
    IRBuilderBase &Builder;
    ArrayRef<OperandBundleDef> DefaultOperandBundles;
    /// Construct an OperandBundlesGuard.
    /// \param B Builder being guarded, or second FMF value.
  public:
    OperandBundlesGuard(IRBuilderBase &B)
        : Builder(B), DefaultOperandBundles(B.DefaultOperandBundles) {}

    /// Construct an OperandBundlesGuard.
    /// \param Unused Unused copy source (deleted).
    OperandBundlesGuard(const OperandBundlesGuard &Unused) = delete;
    /// Copy assignment is deleted.
    /// \param Unused Unused copy source (deleted).
    OperandBundlesGuard &operator=(const OperandBundlesGuard &Unused) = delete;

    /// Destroy the OperandBundlesGuard.
    ~OperandBundlesGuard() {
      Builder.DefaultOperandBundles = DefaultOperandBundles;
    }
  };


  //===--------------------------------------------------------------------===//
  // Miscellaneous creation methods.
  //===--------------------------------------------------------------------===//

  /// Make a new global variable with initializer type i8*
  ///
  /// Make a new global variable with an initializer that has array of i8 type
  /// filled in with the null terminated string value specified.  The new global
  /// variable will be marked mergable with any others of the same contents.  If
  /// Name is specified, it is the name of the global variable created.
  ///
  /// If no module is given via \p M, it is take from the insertion point basic
  /// block.
  /// \param Str String contents of the global.
  /// \param Name Name of the new instruction or value.
  /// \param AddressSpace The AddressSpace parameter.
  /// \param M Module that owns the global string, or nullptr.
  /// \param AddNull Whether to append a null terminator to the string.
  /// @return The created global string value.
  LLVM_ABI GlobalVariable *CreateGlobalString(StringRef Str,
                                              const Twine &Name = "",
                                              unsigned AddressSpace = 0,
                                              Module *M = nullptr,
                                              bool AddNull = true);

  /// Get a constant value representing either true or false.
  /// \param V Input value.
  /// @return A constant value representing either true or false.
  ConstantInt *getInt1(bool V) {
    return ConstantInt::get(getInt1Ty(), V);
  }

  /// Get the constant value for i1 true.
  /// @return The constant value for i1 true.
  ConstantInt *getTrue() {
    return ConstantInt::getTrue(Context);
  }

  /// Get the constant value for i1 false.
  /// @return The constant value for i1 false.
  ConstantInt *getFalse() {
    return ConstantInt::getFalse(Context);
  }

  /// Get a constant 8-bit value.
  /// \param C LLVM context, or integer constant value.
  /// @return A constant 8-bit value.
  ConstantInt *getInt8(uint8_t C) {
    return ConstantInt::get(getInt8Ty(), C);
  }

  /// Get a constant 16-bit value.
  /// \param C LLVM context, or integer constant value.
  /// @return A constant 16-bit value.
  ConstantInt *getInt16(uint16_t C) {
    return ConstantInt::get(getInt16Ty(), C);
  }

  /// Get a constant 32-bit value.
  /// \param C LLVM context, or integer constant value.
  /// @return A constant 32-bit value.
  ConstantInt *getInt32(uint32_t C) {
    return ConstantInt::get(getInt32Ty(), C);
  }

  /// Get a constant 64-bit value.
  /// \param C LLVM context, or integer constant value.
  /// @return A constant 64-bit value.
  ConstantInt *getInt64(uint64_t C) {
    return ConstantInt::get(getInt64Ty(), C);
  }

  /// Get a constant N-bit value, zero extended from a 64-bit value.
  /// \param N Bit width, count, or related size parameter.
  /// \param C LLVM context, or integer constant value.
  /// @return A constant N-bit value, zero extended from a 64-bit value.
  ConstantInt *getIntN(unsigned N, uint64_t C) {
    return ConstantInt::get(getIntNTy(N), C);
  }

  /// Get a constant integer value.
  /// \param AI Alloca instruction or APInt constant source.
  /// @return A constant integer value.
  ConstantInt *getInt(const APInt &AI) {
    return ConstantInt::get(Context, AI);
  }

  //===--------------------------------------------------------------------===//
  // Type creation methods
  //===--------------------------------------------------------------------===//

  /// Fetch the type representing an 8-bit byte.
  /// @return The type representing an 8-bit byte.
  ByteType *getByte8Ty() { return Type::getByte8Ty(Context); }

  /// Fetch the type representing a 16-bit byte.
  /// @return The type representing a 16-bit byte.
  ByteType *getByte16Ty() { return Type::getByte16Ty(Context); }

  /// Fetch the type representing a 32-bit byte.
  /// @return The type representing a 32-bit byte.
  ByteType *getByte32Ty() { return Type::getByte32Ty(Context); }

  /// Fetch the type representing a 64-bit byte.
  /// @return The type representing a 64-bit byte.
  ByteType *getByte64Ty() { return Type::getByte64Ty(Context); }

  /// Fetch the type representing a 128-bit byte.
  /// @return The type representing a 128-bit byte.
  ByteType *getByte128Ty() { return Type::getByte128Ty(Context); }

  /// Fetch the type representing an N-bit byte.
  /// \param N Bit width, count, or related size parameter.
  /// @return The type representing an N-bit byte.
  ByteType *getByteNTy(unsigned N) { return Type::getByteNTy(Context, N); }

  /// Fetch the type representing a single bit
  /// @return The type representing a single bit.
  IntegerType *getInt1Ty() {
    return Type::getInt1Ty(Context);
  }

  /// Fetch the type representing an 8-bit integer.
  /// @return The type representing an 8-bit integer.
  IntegerType *getInt8Ty() {
    return Type::getInt8Ty(Context);
  }

  /// Fetch the type representing a 16-bit integer.
  /// @return The type representing a 16-bit integer.
  IntegerType *getInt16Ty() {
    return Type::getInt16Ty(Context);
  }

  /// Fetch the type representing a 32-bit integer.
  /// @return The type representing a 32-bit integer.
  IntegerType *getInt32Ty() {
    return Type::getInt32Ty(Context);
  }

  /// Fetch the type representing a 64-bit integer.
  /// @return The type representing a 64-bit integer.
  IntegerType *getInt64Ty() {
    return Type::getInt64Ty(Context);
  }

  /// Fetch the type representing a 128-bit integer.
  /// @return The type representing a 128-bit integer.
  IntegerType *getInt128Ty() { return Type::getInt128Ty(Context); }

  /// Fetch the type representing an N-bit integer.
  /// \param N Bit width, count, or related size parameter.
  /// @return The type representing an N-bit integer.
  IntegerType *getIntNTy(unsigned N) {
    return Type::getIntNTy(Context, N);
  }

  /// Fetch the type representing a 16-bit floating point value.
  /// @return The type representing a 16-bit floating-point value.
  Type *getHalfTy() {
    return Type::getHalfTy(Context);
  }

  /// Fetch the type representing a 16-bit brain floating point value.
  /// @return The type representing a brain floating-point value.
  Type *getBFloatTy() {
    return Type::getBFloatTy(Context);
  }

  /// Fetch the type representing a 32-bit floating point value.
  /// @return The type representing a 32-bit floating-point value.
  Type *getFloatTy() {
    return Type::getFloatTy(Context);
  }

  /// Fetch the type representing a 64-bit floating point value.
  /// @return The type representing a 64-bit floating-point value.
  Type *getDoubleTy() {
    return Type::getDoubleTy(Context);
  }

  /// Fetch the type representing void.
  /// @return The void type.
  Type *getVoidTy() {
    return Type::getVoidTy(Context);
  }

  /// Fetch the type representing a pointer.
  /// \param AddrSpace Address space of the pointer or global.
  /// @return The pointer type in the given address space.
  PointerType *getPtrTy(unsigned AddrSpace = 0) {
    return PointerType::get(Context, AddrSpace);
  }

  /// Fetch the type of a byte with size at least as big as that of a
  /// pointer in the given address space.
  /// \param DL Data layout used for sizes, alignments, and casts.
  /// \param AddrSpace Address space of the pointer or global.
  /// @return The pointer type for an 8-bit byte in the given address space.
  ByteType *getBytePtrTy(const DataLayout &DL, unsigned AddrSpace = 0) {
    return DL.getBytePtrType(Context, AddrSpace);
  }

  /// Fetch the type of an integer with size at least as big as that of a
  /// pointer in the given address space.
  /// \param DL Data layout used for sizes, alignments, and casts.
  /// \param AddrSpace Address space of the pointer or global.
  /// @return An integer type at least as wide as a pointer in the given address space.
  IntegerType *getIntPtrTy(const DataLayout &DL, unsigned AddrSpace = 0) {
    return DL.getIntPtrType(Context, AddrSpace);
  }

  /// Fetch the type of an integer that should be used to index GEP operations
  /// within AddressSpace.
  /// \param DL Data layout used for sizes, alignments, and casts.
  /// \param AddrSpace Address space of the pointer or global.
  /// @return The integer type used to index GEP operations in the given address space.
  IntegerType *getIndexTy(const DataLayout &DL, unsigned AddrSpace) {
    return DL.getIndexType(Context, AddrSpace);
  }

  //===--------------------------------------------------------------------===//
  // Intrinsic creation methods
  //===--------------------------------------------------------------------===//

  /// Create and insert a memset to the specified pointer and the
  /// specified value.
  ///
  /// If the pointer isn't an i8*, it will be converted. If alias metadata is
  /// specified, it will be added to the instruction.
  /// \param Ptr Pointer operand.
  /// \param Val Stored or broadcast value.
  /// \param Size Number of bytes, or invariant region size.
  /// \param Align Required alignment of the memory access.
  /// \param isVolatile Whether the memory intrinsic is volatile.
  /// \param AAInfo Optional alias-analysis metadata nodes.
  /// @return The created memset intrinsic call.
  CallInst *CreateMemSet(Value *Ptr, Value *Val, uint64_t Size,
                         MaybeAlign Align, bool isVolatile = false,
                         const AAMDNodes &AAInfo = AAMDNodes()) {
    return CreateMemSet(Ptr, Val, getInt64(Size), Align, isVolatile, AAInfo);
  }

  /// Create a memset intrinsic.
  /// \param Ptr Pointer operand.
  /// \param Val Stored or broadcast value.
  /// \param Size Number of bytes, or invariant region size.
  /// \param Align Required alignment of the memory access.
  /// \param isVolatile Whether the memory intrinsic is volatile.
  /// \param AAInfo Optional alias-analysis metadata nodes.
  /// @return The created memset intrinsic call.
  LLVM_ABI CallInst *CreateMemSet(Value *Ptr, Value *Val, Value *Size,
                                  MaybeAlign Align, bool isVolatile = false,
                                  const AAMDNodes &AAInfo = AAMDNodes());

  /// Create an inline memset intrinsic.
  /// \param Dst Destination pointer.
  /// \param DstAlign Alignment of the destination pointer.
  /// \param Val Stored or broadcast value.
  /// \param Size Number of bytes, or invariant region size.
  /// \param IsVolatile Whether the memory intrinsic is volatile.
  /// \param AAInfo Optional alias-analysis metadata nodes.
  /// @return The created inline memset intrinsic call.
  LLVM_ABI CallInst *CreateMemSetInline(Value *Dst, MaybeAlign DstAlign,
                                        Value *Val, Value *Size,
                                        bool IsVolatile = false,
                                        const AAMDNodes &AAInfo = AAMDNodes());
  /// Create and insert an element unordered-atomic memset of the region of
  /// memory starting at the given pointer to the given value.
  /// \param Ptr Pointer operand.
  /// \param Val Stored or broadcast value.
  /// \param Size Number of bytes, or invariant region size.
  /// \param Alignment Required alignment of the memory access.
  /// \param ElementSize Atomic element size in bytes.
  /// \param AAInfo Optional alias-analysis metadata nodes.
  /// @return The created element-unordered-atomic memset intrinsic call.
  CallInst *
  CreateElementUnorderedAtomicMemSet(Value *Ptr, Value *Val, uint64_t Size,
                                     Align Alignment, uint32_t ElementSize,
                                     const AAMDNodes &AAInfo = AAMDNodes()) {
    return CreateElementUnorderedAtomicMemSet(
        Ptr, Val, getInt64(Size), Align(Alignment), ElementSize, AAInfo);
  }

  /// Generate IR for a call to malloc.
  ///
  /// Computes the malloc argument as the specified type's size, possibly
  /// multiplied by the array size when that size is not constant 1, then calls
  /// malloc with that argument.
  /// \param IntPtrTy Integer type wide enough for a pointer.
  /// \param AllocTy Element type being allocated.
  /// \param AllocSize Size in bytes of one allocated element.
  /// \param ArraySize Number of elements to allocate, or nullptr for one.
  /// \param OpB Operand bundles attached to the malloc call.
  /// \param MallocF Optional malloc function to call.
  /// \param Name Name of the new instruction or value.
  /// @return The created malloc call.
  LLVM_ABI CallInst *CreateMalloc(Type *IntPtrTy, Type *AllocTy,
                                  Value *AllocSize, Value *ArraySize,
                                  ArrayRef<OperandBundleDef> OpB,
                                  Function *MallocF = nullptr,
                                  const Twine &Name = "");

  /// Generate IR for a call to malloc.
  ///
  /// Computes the malloc argument as the specified type's size, possibly
  /// multiplied by the array size when that size is not constant 1, then calls
  /// malloc with that argument.
  /// \param IntPtrTy Integer type wide enough for a pointer.
  /// \param AllocTy Element type being allocated.
  /// \param AllocSize Size in bytes of one allocated element.
  /// \param ArraySize Number of elements to allocate, or nullptr for one.
  /// \param MallocF Optional malloc function to call.
  /// \param Name Name of the new instruction or value.
  /// @return The created malloc call.
  LLVM_ABI CallInst *CreateMalloc(Type *IntPtrTy, Type *AllocTy,
                                  Value *AllocSize, Value *ArraySize,
                                  Function *MallocF = nullptr,
                                  const Twine &Name = "");
  /// Generate the IR for a call to the builtin free function.
  /// \param Source Source instruction for FMF, or pointer passed to free.
  /// \param Bundles Operand bundles attached to the call.
  /// @return The created free call.
  LLVM_ABI CallInst *CreateFree(Value *Source,
                                ArrayRef<OperandBundleDef> Bundles = {});
  /// Create an element unordered-atomic memset.
  /// \param Ptr Pointer operand.
  /// \param Val Stored or broadcast value.
  /// \param Size Number of bytes, or invariant region size.
  /// \param Alignment Required alignment of the memory access.
  /// \param ElementSize Atomic element size in bytes.
  /// \param AAInfo Optional alias-analysis metadata nodes.
  /// @return The created element-unordered-atomic memset intrinsic call.
  LLVM_ABI CallInst *
  CreateElementUnorderedAtomicMemSet(Value *Ptr, Value *Val, Value *Size,
                                     Align Alignment, uint32_t ElementSize,
                                     const AAMDNodes &AAInfo = AAMDNodes());

  /// Create and insert a memcpy between the specified pointers.
  ///
  /// If the pointers aren't i8*, they will be converted.  If alias metadata is
  /// specified, it will be added to the instruction.
  /// and noalias tags.
  /// \param Dst Destination pointer.
  /// \param DstAlign Alignment of the destination pointer.
  /// \param Src Source vector or pointer.
  /// \param SrcAlign Alignment of the source pointer.
  /// \param Size Number of bytes, or invariant region size.
  /// \param isVolatile Whether the memory intrinsic is volatile.
  /// \param AAInfo Optional alias-analysis metadata nodes.
  /// @return The created memcpy intrinsic call.
  CallInst *CreateMemCpy(Value *Dst, MaybeAlign DstAlign, Value *Src,
                         MaybeAlign SrcAlign, uint64_t Size,
                         bool isVolatile = false,
                         const AAMDNodes &AAInfo = AAMDNodes()) {
    return CreateMemCpy(Dst, DstAlign, Src, SrcAlign, getInt64(Size),
                        isVolatile, AAInfo);
  }
  /// Create a memory-transfer intrinsic.
  /// \param IntrID Memory-transfer intrinsic identifier.
  /// \param Dst Destination pointer.
  /// \param DstAlign Alignment of the destination pointer.
  /// \param Src Source vector or pointer.
  /// \param SrcAlign Alignment of the source pointer.
  /// \param Size Number of bytes, or invariant region size.
  /// \param isVolatile Whether the memory intrinsic is volatile.
  /// \param AAInfo Optional alias-analysis metadata nodes.
  /// @return The created memory-transfer intrinsic call.
  LLVM_ABI CallInst *
  CreateMemTransferInst(Intrinsic::ID IntrID, Value *Dst, MaybeAlign DstAlign,
                        Value *Src, MaybeAlign SrcAlign, Value *Size,
                        bool isVolatile = false,
                        const AAMDNodes &AAInfo = AAMDNodes());

  /// Create a mem cpy.
  /// \param Dst Destination pointer.
  /// \param DstAlign Alignment of the destination pointer.
  /// \param Src Source vector or pointer.
  /// \param SrcAlign Alignment of the source pointer.
  /// \param Size Number of bytes, or invariant region size.
  /// \param isVolatile Whether the memory intrinsic is volatile.
  /// \param AAInfo Optional alias-analysis metadata nodes.
  /// @return The created memcpy intrinsic call.
  CallInst *CreateMemCpy(Value *Dst, MaybeAlign DstAlign, Value *Src,
                         MaybeAlign SrcAlign, Value *Size,
                         bool isVolatile = false,
                         const AAMDNodes &AAInfo = AAMDNodes()) {
    return CreateMemTransferInst(Intrinsic::memcpy, Dst, DstAlign, Src,
                                 SrcAlign, Size, isVolatile, AAInfo);
  }

  /// Create a mem cpy inline.
  /// \param Dst Destination pointer.
  /// \param DstAlign Alignment of the destination pointer.
  /// \param Src Source vector or pointer.
  /// \param SrcAlign Alignment of the source pointer.
  /// \param Size Number of bytes, or invariant region size.
  /// \param isVolatile Whether the memory intrinsic is volatile.
  /// \param AAInfo Optional alias-analysis metadata nodes.
  /// @return The created inline memcpy intrinsic call.
  CallInst *CreateMemCpyInline(Value *Dst, MaybeAlign DstAlign, Value *Src,
                               MaybeAlign SrcAlign, Value *Size,
                               bool isVolatile = false,
                               const AAMDNodes &AAInfo = AAMDNodes()) {
    return CreateMemTransferInst(Intrinsic::memcpy_inline, Dst, DstAlign, Src,
                                 SrcAlign, Size, isVolatile, AAInfo);
  }

  /// Create and insert an element unordered-atomic memcpy between the
  /// specified pointers.
  ///
  /// DstAlign/SrcAlign are the alignments of the Dst/Src pointers,
  /// respectively.
  ///
  /// If the pointers aren't i8*, they will be converted.  If alias metadata is
  /// specified, it will be added to the instruction.
  /// \param Dst Destination pointer.
  /// \param DstAlign Alignment of the destination pointer.
  /// \param Src Source vector or pointer.
  /// \param SrcAlign Alignment of the source pointer.
  /// \param Size Number of bytes, or invariant region size.
  /// \param ElementSize Atomic element size in bytes.
  /// \param AAInfo Optional alias-analysis metadata nodes.
  /// @return The created element-unordered-atomic memcpy intrinsic call.
  LLVM_ABI CallInst *CreateElementUnorderedAtomicMemCpy(
      Value *Dst, Align DstAlign, Value *Src, Align SrcAlign, Value *Size,
      uint32_t ElementSize, const AAMDNodes &AAInfo = AAMDNodes());

  /// Create a memmove intrinsic.
  /// \param Dst Destination pointer.
  /// \param DstAlign Alignment of the destination pointer.
  /// \param Src Source vector or pointer.
  /// \param SrcAlign Alignment of the source pointer.
  /// \param Size Number of bytes, or invariant region size.
  /// \param isVolatile Whether the memory intrinsic is volatile.
  /// \param AAInfo Optional alias-analysis metadata nodes.
  /// @return The created memmove intrinsic call.
  CallInst *CreateMemMove(Value *Dst, MaybeAlign DstAlign, Value *Src,
                          MaybeAlign SrcAlign, uint64_t Size,
                          bool isVolatile = false,
                          const AAMDNodes &AAInfo = AAMDNodes()) {
    return CreateMemMove(Dst, DstAlign, Src, SrcAlign, getInt64(Size),
                         isVolatile, AAInfo);
  }

  /// Create a memmove intrinsic.
  /// \param Dst Destination pointer.
  /// \param DstAlign Alignment of the destination pointer.
  /// \param Src Source vector or pointer.
  /// \param SrcAlign Alignment of the source pointer.
  /// \param Size Number of bytes, or invariant region size.
  /// \param isVolatile Whether the memory intrinsic is volatile.
  /// \param AAInfo Optional alias-analysis metadata nodes.
  /// @return The created memmove intrinsic call.
  CallInst *CreateMemMove(Value *Dst, MaybeAlign DstAlign, Value *Src,
                          MaybeAlign SrcAlign, Value *Size,
                          bool isVolatile = false,
                          const AAMDNodes &AAInfo = AAMDNodes()) {
    return CreateMemTransferInst(Intrinsic::memmove, Dst, DstAlign, Src,
                                 SrcAlign, Size, isVolatile, AAInfo);
  }

  /// \brief Create and insert an element unordered-atomic memmove between the
  /// specified pointers.
  ///
  /// DstAlign/SrcAlign are the alignments of the Dst/Src pointers,
  /// respectively.
  ///
  /// If the pointers aren't i8*, they will be converted.  If alias metadata is
  /// specified, it will be added to the instruction.
  /// \param Dst Destination pointer.
  /// \param DstAlign Alignment of the destination pointer.
  /// \param Src Source vector or pointer.
  /// \param SrcAlign Alignment of the source pointer.
  /// \param Size Number of bytes, or invariant region size.
  /// \param ElementSize Atomic element size in bytes.
  /// \param AAInfo Optional alias-analysis metadata nodes.
  /// @return The created element-unordered-atomic memmove intrinsic call.
  LLVM_ABI CallInst *CreateElementUnorderedAtomicMemMove(
      Value *Dst, Align DstAlign, Value *Src, Align SrcAlign, Value *Size,
      uint32_t ElementSize, const AAMDNodes &AAInfo = AAMDNodes());

private:
  /// Get the reduction intrinsic.
  /// \param ID Intrinsic or statepoint identifier.
  /// \param Src Source vector or pointer.
  Value *getReductionIntrinsic(Intrinsic::ID ID, Value *Src);

public:
  /// Create a sequential vector fadd reduction intrinsic.
  ///
  /// The first parameter is a scalar accumulator value. An unordered reduction
  /// can be created by adding the reassoc fast-math flag to the resulting
  /// sequential reduction.
  /// \param Acc Scalar accumulator value for the reduction.
  /// \param Src Source vector or pointer.
  /// @return The created sequential vector fadd reduction intrinsic.
  LLVM_ABI Value *CreateFAddReduce(Value *Acc, Value *Src);

  /// Create a sequential vector fmul reduction intrinsic.
  ///
  /// The first parameter is a scalar accumulator value. An unordered reduction
  /// can be created by adding the reassoc fast-math flag to the resulting
  /// sequential reduction.
  /// \param Acc Scalar accumulator value for the reduction.
  /// \param Src Source vector or pointer.
  /// @return The created sequential vector fmul reduction intrinsic.
  LLVM_ABI Value *CreateFMulReduce(Value *Acc, Value *Src);

  /// Create a vector int add reduction intrinsic of the source vector.
  /// \param Src Source vector or pointer.
  /// @return The created vector int add reduction intrinsic of the source vector.
  LLVM_ABI Value *CreateAddReduce(Value *Src);

  /// Create a vector int mul reduction intrinsic of the source vector.
  /// \param Src Source vector or pointer.
  /// @return The created vector int mul reduction intrinsic of the source vector.
  LLVM_ABI Value *CreateMulReduce(Value *Src);

  /// Create a vector int AND reduction intrinsic of the source vector.
  /// \param Src Source vector or pointer.
  /// @return The created vector int AND reduction intrinsic of the source vector.
  LLVM_ABI Value *CreateAndReduce(Value *Src);

  /// Create a vector int OR reduction intrinsic of the source vector.
  /// \param Src Source vector or pointer.
  /// @return The created vector int OR reduction intrinsic of the source vector.
  LLVM_ABI Value *CreateOrReduce(Value *Src);

  /// Create a vector int XOR reduction intrinsic of the source vector.
  /// \param Src Source vector or pointer.
  /// @return The created vector int XOR reduction intrinsic of the source vector.
  LLVM_ABI Value *CreateXorReduce(Value *Src);

  /// Create a vector integer max reduction intrinsic of the source
  /// vector.
  /// \param Src Source vector or pointer.
  /// \param IsSigned Whether to treat the reduction as signed.
  /// @return The created vector integer max reduction intrinsic of the source vector.
  LLVM_ABI Value *CreateIntMaxReduce(Value *Src, bool IsSigned = false);

  /// Create a vector integer min reduction intrinsic of the source
  /// vector.
  /// \param Src Source vector or pointer.
  /// \param IsSigned Whether to treat the reduction as signed.
  /// @return The created vector integer min reduction intrinsic of the source vector.
  LLVM_ABI Value *CreateIntMinReduce(Value *Src, bool IsSigned = false);

  /// Create a vector float max reduction intrinsic of the source
  /// vector.
  /// \param Src Source vector or pointer.
  /// @return The created vector float max reduction intrinsic of the source vector.
  LLVM_ABI Value *CreateFPMaxReduce(Value *Src);

  /// Create a vector float min reduction intrinsic of the source
  /// vector.
  /// \param Src Source vector or pointer.
  /// @return The created vector float min reduction intrinsic of the source vector.
  LLVM_ABI Value *CreateFPMinReduce(Value *Src);

  /// Create a vector float maximum reduction intrinsic of the source
  /// vector. This variant follows the NaN and signed zero semantic of
  /// llvm.maximum intrinsic.
  /// \param Src Source vector or pointer.
  /// @return The created vector float maximum reduction intrinsic of the source vector.
  LLVM_ABI Value *CreateFPMaximumReduce(Value *Src);

  /// Create a vector float minimum reduction intrinsic of the source
  /// vector. This variant follows the NaN and signed zero semantic of
  /// llvm.minimum intrinsic.
  /// \param Src Source vector or pointer.
  /// @return The created vector float minimum reduction intrinsic of the source vector.
  LLVM_ABI Value *CreateFPMinimumReduce(Value *Src);

  /// Create a vector float maximum reduction intrinsic of the source
  /// vector. This variant follows the NaN and signed zero semantic of
  /// llvm.maximumnum intrinsic.
  /// \param Src Source vector or pointer.
  /// @return The created vector float maximum reduction intrinsic of the source vector.
  LLVM_ABI Value *CreateFPMaximumNumReduce(Value *Src);

  /// Create a vector float minimum reduction intrinsic of the source
  /// vector. This variant follows the NaN and signed zero semantic of
  /// llvm.minimumnum intrinsic.
  /// \param Src Source vector or pointer.
  /// @return The created vector float minimum reduction intrinsic of the source vector.
  LLVM_ABI Value *CreateFPMinimumNumReduce(Value *Src);

  /// Create a lifetime.start intrinsic.
  /// \param Ptr Pointer operand.
  /// @return The created lifetime.start intrinsic call.
  LLVM_ABI CallInst *CreateLifetimeStart(Value *Ptr);

  /// Create a lifetime.end intrinsic.
  /// \param Ptr Pointer operand.
  /// @return The created lifetime.end intrinsic call.
  LLVM_ABI CallInst *CreateLifetimeEnd(Value *Ptr);

  /// Create a call to invariant.start intrinsic.
  ///
  /// If the pointer isn't i8* it will be converted.
  /// \param Ptr Pointer operand.
  /// \param Size Number of bytes, or invariant region size.
  /// @return The created invariant.start intrinsic call.
  LLVM_ABI CallInst *CreateInvariantStart(Value *Ptr,
                                          ConstantInt *Size = nullptr);

  /// Create a call to llvm.threadlocal.address intrinsic.
  /// \param Ptr Pointer operand.
  /// @return The created thread-local address intrinsic call.
  LLVM_ABI CallInst *CreateThreadLocalAddress(Value *Ptr);

  /// Create a call to Masked Load intrinsic
  /// \param Ty Type of the loaded, allocated, or created value.
  /// \param Ptr Pointer operand.
  /// \param Alignment Required alignment of the memory access.
  /// \param Mask Vector mask selecting active lanes.
  /// \param PassThru Passthrough value for inactive masked lanes.
  /// \param Name Name of the new instruction or value.
  /// @return The created masked load intrinsic call.
  LLVM_ABI CallInst *CreateMaskedLoad(Type *Ty, Value *Ptr, Align Alignment,
                                      Value *Mask, Value *PassThru = nullptr,
                                      const Twine &Name = "");

  /// Create a call to Masked Store intrinsic
  /// \param Val Stored or broadcast value.
  /// \param Ptr Pointer operand.
  /// \param Alignment Required alignment of the memory access.
  /// \param Mask Vector mask selecting active lanes.
  /// @return The created masked store intrinsic call.
  LLVM_ABI CallInst *CreateMaskedStore(Value *Val, Value *Ptr, Align Alignment,
                                       Value *Mask);

  /// Create a call to Masked Gather intrinsic
  /// \param Ty Type of the loaded, allocated, or created value.
  /// \param Ptrs Vector of pointers for a gather or scatter.
  /// \param Alignment Required alignment of the memory access.
  /// \param Mask Vector mask selecting active lanes.
  /// \param PassThru Passthrough value for inactive masked lanes.
  /// \param Name Name of the new instruction or value.
  /// @return The created masked gather intrinsic call.
  LLVM_ABI CallInst *CreateMaskedGather(Type *Ty, Value *Ptrs, Align Alignment,
                                        Value *Mask = nullptr,
                                        Value *PassThru = nullptr,
                                        const Twine &Name = "");

  /// Create a call to Masked Scatter intrinsic
  /// \param Val Stored or broadcast value.
  /// \param Ptrs Vector of pointers for a gather or scatter.
  /// \param Alignment Required alignment of the memory access.
  /// \param Mask Vector mask selecting active lanes.
  /// @return The created masked scatter intrinsic call.
  LLVM_ABI CallInst *CreateMaskedScatter(Value *Val, Value *Ptrs,
                                         Align Alignment,
                                         Value *Mask = nullptr);

  /// Create a call to Masked Expand Load intrinsic
  /// \param Ty Type of the loaded, allocated, or created value.
  /// \param Ptr Pointer operand.
  /// \param Align Required alignment of the memory access.
  /// \param Mask Vector mask selecting active lanes.
  /// \param PassThru Passthrough value for inactive masked lanes.
  /// \param Name Name of the new instruction or value.
  /// @return The created masked expand-load intrinsic call.
  LLVM_ABI CallInst *CreateMaskedExpandLoad(Type *Ty, Value *Ptr,
                                            MaybeAlign Align,
                                            Value *Mask = nullptr,
                                            Value *PassThru = nullptr,
                                            const Twine &Name = "");

  /// Create a call to Masked Compress Store intrinsic
  /// \param Val Stored or broadcast value.
  /// \param Ptr Pointer operand.
  /// \param Align Required alignment of the memory access.
  /// \param Mask Vector mask selecting active lanes.
  /// @return The created masked compress-store intrinsic call.
  LLVM_ABI CallInst *CreateMaskedCompressStore(Value *Val, Value *Ptr,
                                               MaybeAlign Align,
                                               Value *Mask = nullptr);

  /// Return an all true boolean vector (mask) with \p NumElts lanes.
  /// \param NumElts Number of vector elements.
  /// @return An all-ones vector mask of the requested type.
  Value *getAllOnesMask(ElementCount NumElts) {
    VectorType *VTy = VectorType::get(Type::getInt1Ty(Context), NumElts);
    return Constant::getAllOnesValue(VTy);
  }

  /// Create an assume intrinsic call that allows the optimizer to
  /// assume that the provided condition will be true.
  /// \param Cond Boolean condition value.
  /// @return The created llvm.assume intrinsic call.
  LLVM_ABI CallInst *CreateAssumption(Value *Cond);

  /// Create an assume intrinsic call that allows the optimizer to
  /// assume that the provided operand bundles hold.
  /// \param OpBundles Operand bundles attached to the call.
  /// @return The created llvm.assume intrinsic call.
  LLVM_ABI CallInst *CreateAssumption(ArrayRef<OperandBundleDef> OpBundles);

  /// Create a llvm.experimental.noalias.scope.decl intrinsic call.
  /// \param Scope Noalias scope metadata node.
  /// @return The created noalias.scope.declare intrinsic call.
  LLVM_ABI Instruction *CreateNoAliasScopeDeclaration(Value *Scope);
  /// Create a noalias.scope.decl intrinsic from a metadata tag.
  /// \param ScopeTag Noalias scope metadata tag.
  /// @return The created noalias.scope.declare intrinsic call.
  Instruction *CreateNoAliasScopeDeclaration(MDNode *ScopeTag) {
    return CreateNoAliasScopeDeclaration(
        MetadataAsValue::get(Context, ScopeTag));
  }

  /// Create a call to the experimental.gc.statepoint intrinsic.
  /// \param ID Intrinsic or statepoint identifier.
  /// \param NumPatchBytes Number of patchable nop bytes at the statepoint.
  /// \param ActualCallee Callee of the statepoint call.
  /// \param CallArgs Arguments passed to the statepoint callee.
  /// \param DeoptArgs Deoptimization arguments for the statepoint.
  /// \param GCArgs GC pointer arguments for the statepoint.
  /// \param Name Name of the new instruction or value.
  /// @return The created GC statepoint call.
  LLVM_ABI CallInst *CreateGCStatepointCall(
      uint64_t ID, uint32_t NumPatchBytes, FunctionCallee ActualCallee,
      ArrayRef<Value *> CallArgs, std::optional<ArrayRef<Value *>> DeoptArgs,
      ArrayRef<Value *> GCArgs, const Twine &Name = "");
  /// Create a call to the experimental.gc.statepoint intrinsic to
  /// start a new statepoint sequence.
  /// \param ID Intrinsic or statepoint identifier.
  /// \param NumPatchBytes Number of patchable nop bytes at the statepoint.
  /// \param ActualCallee Callee of the statepoint call.
  /// \param Flags Statepoint flags bitmask.
  /// \param CallArgs Arguments passed to the statepoint callee.
  /// \param TransitionArgs GC transition arguments for the statepoint.
  /// \param DeoptArgs Deoptimization arguments for the statepoint.
  /// \param GCArgs GC pointer arguments for the statepoint.
  /// \param Name Name of the new instruction or value.
  /// @return The created GC statepoint call.
  LLVM_ABI CallInst *
  CreateGCStatepointCall(uint64_t ID, uint32_t NumPatchBytes,
                         FunctionCallee ActualCallee, uint32_t Flags,
                         ArrayRef<Value *> CallArgs,
                         std::optional<ArrayRef<Use>> TransitionArgs,
                         std::optional<ArrayRef<Use>> DeoptArgs,
                         ArrayRef<Value *> GCArgs, const Twine &Name = "");
  /// Create a GC statepoint call.
  ///
  /// Convenience overload for the common case when CallArgs are filled from an
  /// existing call-site argument range. Use needs to be .get()'ed to get the
  /// Value pointer.
  /// \param ID Intrinsic or statepoint identifier.
  /// \param NumPatchBytes Number of patchable nop bytes at the statepoint.
  /// \param ActualCallee Callee of the statepoint call.
  /// \param CallArgs Arguments passed to the statepoint callee.
  /// \param DeoptArgs Deoptimization arguments for the statepoint.
  /// \param GCArgs GC pointer arguments for the statepoint.
  /// \param Name Name of the new instruction or value.
  /// @return The created GC statepoint call.
  LLVM_ABI CallInst *
  CreateGCStatepointCall(uint64_t ID, uint32_t NumPatchBytes,
                         FunctionCallee ActualCallee, ArrayRef<Use> CallArgs,
                         std::optional<ArrayRef<Value *>> DeoptArgs,
                         ArrayRef<Value *> GCArgs, const Twine &Name = "");
  /// Create an invoke to the experimental.gc.statepoint intrinsic to
  /// start a new statepoint sequence.
  /// \param ID Intrinsic or statepoint identifier.
  /// \param NumPatchBytes Number of patchable nop bytes at the statepoint.
  /// \param ActualInvokee Callee of the statepoint invoke.
  /// \param NormalDest Normal (non-unwind) destination block.
  /// \param UnwindDest Unwind destination basic block.
  /// \param InvokeArgs Arguments passed to the statepoint invokee.
  /// \param DeoptArgs Deoptimization arguments for the statepoint.
  /// \param GCArgs GC pointer arguments for the statepoint.
  /// \param Name Name of the new instruction or value.
  /// @return The created GC statepoint invoke.
  LLVM_ABI InvokeInst *
  CreateGCStatepointInvoke(uint64_t ID, uint32_t NumPatchBytes,
                           FunctionCallee ActualInvokee, BasicBlock *NormalDest,
                           BasicBlock *UnwindDest, ArrayRef<Value *> InvokeArgs,
                           std::optional<ArrayRef<Value *>> DeoptArgs,
                           ArrayRef<Value *> GCArgs, const Twine &Name = "");

  /// Create an invoke to the experimental.gc.statepoint intrinsic to
  /// start a new statepoint sequence.
  /// \param ID Intrinsic or statepoint identifier.
  /// \param NumPatchBytes Number of patchable nop bytes at the statepoint.
  /// \param ActualInvokee Callee of the statepoint invoke.
  /// \param NormalDest Normal (non-unwind) destination block.
  /// \param UnwindDest Unwind destination basic block.
  /// \param Flags Statepoint flags bitmask.
  /// \param InvokeArgs Arguments passed to the statepoint invokee.
  /// \param TransitionArgs GC transition arguments for the statepoint.
  /// \param DeoptArgs Deoptimization arguments for the statepoint.
  /// \param GCArgs GC pointer arguments for the statepoint.
  /// \param Name Name of the new instruction or value.
  /// @return The created GC statepoint invoke.
  LLVM_ABI InvokeInst *CreateGCStatepointInvoke(
      uint64_t ID, uint32_t NumPatchBytes, FunctionCallee ActualInvokee,
      BasicBlock *NormalDest, BasicBlock *UnwindDest, uint32_t Flags,
      ArrayRef<Value *> InvokeArgs, std::optional<ArrayRef<Use>> TransitionArgs,
      std::optional<ArrayRef<Use>> DeoptArgs, ArrayRef<Value *> GCArgs,
      const Twine &Name = "");

  // Convenience function for the common case when CallArgs are filled in using
  // ArrayRef(CS.arg_begin(), CS.arg_end()); Use needs to be .get()'ed to
  // get the Value *.
  /// Create a gcstatepoint invoke.
  /// \param ID Intrinsic or statepoint identifier.
  /// \param NumPatchBytes Number of patchable nop bytes at the statepoint.
  /// \param ActualInvokee Callee of the statepoint invoke.
  /// \param NormalDest Normal (non-unwind) destination block.
  /// \param UnwindDest Unwind destination basic block.
  /// \param InvokeArgs Arguments passed to the statepoint invokee.
  /// \param DeoptArgs Deoptimization arguments for the statepoint.
  /// \param GCArgs GC pointer arguments for the statepoint.
  /// \param Name Name of the new instruction or value.
  /// @return The created GC statepoint invoke.
  LLVM_ABI InvokeInst *
  CreateGCStatepointInvoke(uint64_t ID, uint32_t NumPatchBytes,
                           FunctionCallee ActualInvokee, BasicBlock *NormalDest,
                           BasicBlock *UnwindDest, ArrayRef<Use> InvokeArgs,
                           std::optional<ArrayRef<Value *>> DeoptArgs,
                           ArrayRef<Value *> GCArgs, const Twine &Name = "");

  /// Create a call to the experimental.gc.result intrinsic to extract
  /// the result from a call wrapped in a statepoint.
  /// \param Statepoint GC statepoint call or invoke.
  /// \param ResultType Result type of the GC relocate or result.
  /// \param Name Name of the new instruction or value.
  /// @return The created GC result value.
  LLVM_ABI CallInst *CreateGCResult(Instruction *Statepoint, Type *ResultType,
                                    const Twine &Name = "");

  /// Create a call to the experimental.gc.relocate intrinsics to
  /// project the relocated value of one pointer from the statepoint.
  /// \param Statepoint GC statepoint call or invoke.
  /// \param BaseOffset Byte offset of the base pointer in the statepoint.
  /// \param DerivedOffset Byte offset of the derived pointer in the statepoint.
  /// \param ResultType Result type of the GC relocate or result.
  /// \param Name Name of the new instruction or value.
  /// @return The created GC relocate value.
  LLVM_ABI CallInst *CreateGCRelocate(Instruction *Statepoint, int BaseOffset,
                                      int DerivedOffset, Type *ResultType,
                                      const Twine &Name = "");

  /// Create a call to the experimental.gc.pointer.base intrinsic to get the
  /// base pointer for the specified derived pointer.
  /// \param DerivedPtr Derived GC pointer.
  /// \param Name Name of the new instruction or value.
  /// @return The created GC pointer-base value.
  LLVM_ABI CallInst *CreateGCGetPointerBase(Value *DerivedPtr,
                                            const Twine &Name = "");

  /// Create a call to the experimental.gc.get.pointer.offset intrinsic to get
  /// the offset of the specified derived pointer from its base.
  /// \param DerivedPtr Derived GC pointer.
  /// \param Name Name of the new instruction or value.
  /// @return The created GC pointer-offset value.
  LLVM_ABI CallInst *CreateGCGetPointerOffset(Value *DerivedPtr,
                                              const Twine &Name = "");

  /// Create a call to llvm.vscale.<Ty>().
  /// \param Ty Type of the loaded, allocated, or created value.
  /// \param Name Name of the new instruction or value.
  /// @return The created call to llvm.vscale.<Ty>().
  Value *CreateVScale(Type *Ty, const Twine &Name = "") {
    return CreateIntrinsic(Intrinsic::vscale, {Ty}, {}, {}, Name);
  }

  /// Create an expression which evaluates to the number of elements in \p EC
  /// at runtime. This can result in poison if type \p Ty is not big enough to
  /// hold the value.
  /// \param Ty Type of the loaded, allocated, or created value.
  /// \param EC Element count of the splat result.
  /// @return The created expression which evaluates to the number of elements in \p EC at runtime.
  LLVM_ABI Value *CreateElementCount(Type *Ty, ElementCount EC);

  /// Create an expression for the runtime size of a type.
  ///
  /// Evaluates to the number of units in Size at runtime. Works for both units
  /// of bits and bytes. Can result in poison if type Ty is not big enough to
  /// hold the value.
  /// \param Ty Type of the loaded, allocated, or created value.
  /// \param Size Number of bytes, or invariant region size.
  /// @return The created expression for the runtime size of a type.
  LLVM_ABI Value *CreateTypeSize(Type *Ty, TypeSize Size);

  /// Get allocation size of an alloca as a runtime Value* (handles both static
  /// and dynamic allocas and vscale factor).
  /// \param DestTy Destination type of the cast.
  /// \param AI Alloca instruction or APInt constant source.
  /// @return The allocation size of an alloca as a runtime Value* (handles both static and dynamic allocas and vscale factor).
  LLVM_ABI Value *CreateAllocationSize(Type *DestTy, AllocaInst *AI);

  /// Creates a vector of type \p DstType with the linear sequence <0, 1, ...>
  /// \param DstType Destination vector or value type.
  /// \param Name Name of the new instruction or value.
  /// @return The created vector of type \p DstType with the linear sequence <0, 1, ...>.
  LLVM_ABI Value *CreateStepVector(Type *DstType, const Twine &Name = "");

  /// Create a call to intrinsic \p ID with 1 operand which is mangled on its
  /// type.
  /// \param ID Intrinsic or statepoint identifier.
  /// \param Op Unary operand.
  /// \param FMFSource Optional source of fast-math flags.
  /// \param Name Name of the new instruction or value.
  /// @return The created call to intrinsic \p ID with 1 operand which is mangled on its type.
  LLVM_ABI Value *CreateUnaryIntrinsic(Intrinsic::ID ID, Value *Op,
                                       FMFSource FMFSource = {},
                                       const Twine &Name = "");

  /// Create a call to intrinsic \p ID with 2 operands which is mangled on the
  /// first type.
  /// \param ID Intrinsic or statepoint identifier.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param FMFSource Optional source of fast-math flags.
  /// \param Name Name of the new instruction or value.
  /// @return The created call to intrinsic \p ID with 2 operands which is mangled on the first type.
  LLVM_ABI Value *CreateBinaryIntrinsic(Intrinsic::ID ID, Value *LHS,
                                        Value *RHS, FMFSource FMFSource = {},
                                        const Twine &Name = "");

  /// Create an intrinsic call that is never constant-folded.
  ///
  /// If FMFSource is provided, copies fast-math flags from that instruction to
  /// the intrinsic.
  /// \param ID Intrinsic or statepoint identifier.
  /// \param OverloadTypes Types used to mangle an overloaded intrinsic.
  /// \param Args Argument list for the call.
  /// \param FMFSource Optional source of fast-math flags.
  /// \param Name Name of the new instruction or value.
  /// \param OpBundles Operand bundles attached to the call.
  /// @return The created intrinsic call that is never constant-folded.
  LLVM_ABI CallInst *CreateIntrinsicWithoutFolding(
      Intrinsic::ID ID, ArrayRef<Type *> OverloadTypes, ArrayRef<Value *> Args,
      FMFSource FMFSource = {}, const Twine &Name = "",
      ArrayRef<OperandBundleDef> OpBundles = {});

  /// Create an intrinsic call that is never constant-folded.
  ///
  /// If FMFSource is provided, copies fast-math flags from that instruction to
  /// the intrinsic.
  /// \param RetTy Return type of the intrinsic.
  /// \param ID Intrinsic or statepoint identifier.
  /// \param Args Argument list for the call.
  /// \param FMFSource Optional source of fast-math flags.
  /// \param Name Name of the new instruction or value.
  /// @return The created intrinsic call that is never constant-folded.
  LLVM_ABI CallInst *CreateIntrinsicWithoutFolding(Type *RetTy,
                                                   Intrinsic::ID ID,
                                                   ArrayRef<Value *> Args,
                                                   FMFSource FMFSource = {},
                                                   const Twine &Name = "");

  /// Create an intrinsic call that is never constant-folded.
  ///
  /// If FMFSource is provided, copies fast-math flags from that instruction to
  /// the intrinsic.
  /// \param ID Intrinsic or statepoint identifier.
  /// \param Args Argument list for the call.
  /// \param FMFSource Optional source of fast-math flags.
  /// \param Name Name of the new instruction or value.
  /// @return The created intrinsic call that is never constant-folded.
  CallInst *CreateIntrinsicWithoutFolding(Intrinsic::ID ID,
                                          ArrayRef<Value *> Args,
                                          FMFSource FMFSource = {},
                                          const Twine &Name = "") {
    return CreateIntrinsicWithoutFolding(ID, /*Types=*/{}, Args, FMFSource,
                                         Name);
  }

  /// Create a possibly constant-folded intrinsic call.
  ///
  /// An optional SetFn is called if the intrinsic does not fold, and can be
  /// used to set attributes.
  /// \param ID Intrinsic or statepoint identifier.
  /// \param OverloadTypes Types used to mangle an overloaded intrinsic.
  /// \param Args Argument list for the call.
  /// \param FMFSource Optional source of fast-math flags.
  /// \param Name Name of the new instruction or value.
  /// \param OpBundles Operand bundles attached to the call.
  /// \param SetFn Optional callback applied when the intrinsic is not folded.
  /// @return The created possibly constant-folded intrinsic call.
  LLVM_ABI Value *CreateIntrinsic(
      Intrinsic::ID ID, ArrayRef<Type *> OverloadTypes, ArrayRef<Value *> Args,
      FMFSource FMFSource = {}, const Twine &Name = "",
      ArrayRef<OperandBundleDef> OpBundles = {},
      function_ref<void(CallInst *)> SetFn = [](CallInst *) {});

  /// Create a possibly constant-folded intrinsic call.
  ///
  /// An optional SetFn is called if the intrinsic does not fold, and can be
  /// used to set attributes.
  /// \param RetTy Return type of the intrinsic.
  /// \param ID Intrinsic or statepoint identifier.
  /// \param Args Argument list for the call.
  /// \param FMFSource Optional source of fast-math flags.
  /// \param Name Name of the new instruction or value.
  /// \param SetFn Optional callback applied when the intrinsic is not folded.
  /// @return The created possibly constant-folded intrinsic call.
  LLVM_ABI Value *CreateIntrinsic(
      Type *RetTy, Intrinsic::ID ID, ArrayRef<Value *> Args,
      FMFSource FMFSource = {}, const Twine &Name = "",
      function_ref<void(CallInst *)> SetFn = [](CallInst *) {});

  /// Create a possibly constant-folded intrinsic call.
  ///
  /// An optional SetFn is called if the intrinsic does not fold, and can be
  /// used to set attributes.
  /// \param ID Intrinsic or statepoint identifier.
  /// \param Args Argument list for the call.
  /// \param FMFSource Optional source of fast-math flags.
  /// \param Name Name of the new instruction or value.
  /// \param SetFn Optional callback applied when the intrinsic is not folded.
  /// @return The created possibly constant-folded intrinsic call.
  Value *CreateIntrinsic(
      Intrinsic::ID ID, ArrayRef<Value *> Args, FMFSource FMFSource = {},
      const Twine &Name = "",
      function_ref<void(CallInst *)> SetFn = [](CallInst *) {}) {
    return CreateIntrinsic(ID, /*Types=*/{}, Args, FMFSource, Name, {}, SetFn);
  }

  /// Create call to the fabs intrinsic.
  /// \param V Input value.
  /// \param FMFSource Optional source of fast-math flags.
  /// \param Name Name of the new instruction or value.
  /// @return The created call to the fabs intrinsic.
  Value *CreateFAbs(Value *V, FMFSource FMFSource = {},
                    const Twine &Name = "") {
    return CreateUnaryIntrinsic(Intrinsic::fabs, V, FMFSource, Name);
  }

  /// Create call to the minnum intrinsic.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param FMFSource Optional source of fast-math flags.
  /// \param Name Name of the new instruction or value.
  /// @return The created call to the minnum intrinsic.
  Value *CreateMinNum(Value *LHS, Value *RHS, FMFSource FMFSource = {},
                      const Twine &Name = "") {
    if (IsFPConstrained) {
      return CreateConstrainedFPUnroundedBinOp(
          Intrinsic::experimental_constrained_minnum, LHS, RHS, FMFSource,
          Name);
    }

    return CreateBinaryIntrinsic(Intrinsic::minnum, LHS, RHS, FMFSource, Name);
  }

  /// Create call to the maxnum intrinsic.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param FMFSource Optional source of fast-math flags.
  /// \param Name Name of the new instruction or value.
  /// @return The created call to the maxnum intrinsic.
  Value *CreateMaxNum(Value *LHS, Value *RHS, FMFSource FMFSource = {},
                      const Twine &Name = "") {
    if (IsFPConstrained) {
      return CreateConstrainedFPUnroundedBinOp(
          Intrinsic::experimental_constrained_maxnum, LHS, RHS, FMFSource,
          Name);
    }

    return CreateBinaryIntrinsic(Intrinsic::maxnum, LHS, RHS, FMFSource, Name);
  }

  /// Create call to the minimum intrinsic.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// @return The created call to the minimum intrinsic.
  Value *CreateMinimum(Value *LHS, Value *RHS, const Twine &Name = "") {
    return CreateBinaryIntrinsic(Intrinsic::minimum, LHS, RHS, nullptr, Name);
  }

  /// Create call to the maximum intrinsic.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// @return The created call to the maximum intrinsic.
  Value *CreateMaximum(Value *LHS, Value *RHS, const Twine &Name = "") {
    return CreateBinaryIntrinsic(Intrinsic::maximum, LHS, RHS, nullptr, Name);
  }

  /// Create call to the minimumnum intrinsic.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// @return The created call to the minimumnum intrinsic.
  Value *CreateMinimumNum(Value *LHS, Value *RHS, const Twine &Name = "") {
    return CreateBinaryIntrinsic(Intrinsic::minimumnum, LHS, RHS, nullptr,
                                 Name);
  }

  /// Create call to the maximum intrinsic.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// @return The created call to the maximum intrinsic.
  Value *CreateMaximumNum(Value *LHS, Value *RHS, const Twine &Name = "") {
    return CreateBinaryIntrinsic(Intrinsic::maximumnum, LHS, RHS, nullptr,
                                 Name);
  }

  /// Create call to the copysign intrinsic.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param FMFSource Optional source of fast-math flags.
  /// \param Name Name of the new instruction or value.
  /// @return The created call to the copysign intrinsic.
  Value *CreateCopySign(Value *LHS, Value *RHS, FMFSource FMFSource = {},
                        const Twine &Name = "") {
    return CreateBinaryIntrinsic(Intrinsic::copysign, LHS, RHS, FMFSource,
                                 Name);
  }

  /// Create call to the ldexp intrinsic.
  /// \param Src Source vector or pointer.
  /// \param Exp Binary exponent for ldexp.
  /// \param FMFSource Optional source of fast-math flags.
  /// \param Name Name of the new instruction or value.
  /// @return The created call to the ldexp intrinsic.
  Value *CreateLdexp(Value *Src, Value *Exp, FMFSource FMFSource = {},
                     const Twine &Name = "") {
    assert(!IsFPConstrained && "TODO: Support strictfp");
    return CreateIntrinsic(Intrinsic::ldexp, {Src->getType(), Exp->getType()},
                           {Src, Exp}, FMFSource, Name);
  }

  /// Create call to the fma intrinsic.
  /// \param Factor1 First multiply operand of an FMA.
  /// \param Factor2 Second multiply operand of an FMA.
  /// \param Summand Addend of an FMA.
  /// \param FMFSource Optional source of fast-math flags.
  /// \param Name Name of the new instruction or value.
  /// @return The created call to the fma intrinsic.
  Value *CreateFMA(Value *Factor1, Value *Factor2, Value *Summand,
                   FMFSource FMFSource = {}, const Twine &Name = "") {
    if (IsFPConstrained) {
      return CreateConstrainedFPIntrinsic(
          Intrinsic::experimental_constrained_fma, {Factor1->getType()},
          {Factor1, Factor2, Summand}, FMFSource, Name);
    }

    return CreateIntrinsic(Intrinsic::fma, {Factor1->getType()},
                           {Factor1, Factor2, Summand}, FMFSource, Name);
  }

  /// Create a call to the arithmetic_fence intrinsic.
  /// \param Val Stored or broadcast value.
  /// \param DstType Destination vector or value type.
  /// \param Name Name of the new instruction or value.
  /// @return The created call to the arithmetic_fence intrinsic.
  Value *CreateArithmeticFence(Value *Val, Type *DstType,
                               const Twine &Name = "") {
    return CreateIntrinsic(Intrinsic::arithmetic_fence, DstType, Val, nullptr,
                           Name);
  }

  /// Create a call to the vector.extract intrinsic.
  /// \param DstType Destination vector or value type.
  /// \param SrcVec Source vector.
  /// \param Idx Index at which to extract or insert.
  /// \param Name Name of the new instruction or value.
  /// @return The created call to the vector.extract intrinsic.
  Value *CreateExtractVector(Type *DstType, Value *SrcVec, Value *Idx,
                             const Twine &Name = "") {
    return CreateIntrinsic(Intrinsic::vector_extract,
                           {DstType, SrcVec->getType()}, {SrcVec, Idx}, nullptr,
                           Name);
  }

  /// Create a call to the vector.extract intrinsic.
  /// \param DstType Destination vector or value type.
  /// \param SrcVec Source vector.
  /// \param Idx Index at which to extract or insert.
  /// \param Name Name of the new instruction or value.
  /// @return The created call to the vector.extract intrinsic.
  Value *CreateExtractVector(Type *DstType, Value *SrcVec, uint64_t Idx,
                             const Twine &Name = "") {
    return CreateExtractVector(DstType, SrcVec, getInt64(Idx), Name);
  }

  /// Create a call to the vector.insert intrinsic.
  /// \param DstType Destination vector or value type.
  /// \param SrcVec Source vector.
  /// \param SubVec Subvector to insert.
  /// \param Idx Index at which to extract or insert.
  /// \param Name Name of the new instruction or value.
  /// @return The created call to the vector.insert intrinsic.
  Value *CreateInsertVector(Type *DstType, Value *SrcVec, Value *SubVec,
                            Value *Idx, const Twine &Name = "") {
    return CreateIntrinsic(Intrinsic::vector_insert,
                           {DstType, SubVec->getType()}, {SrcVec, SubVec, Idx},
                           nullptr, Name);
  }

  /// Create a call to the vector.extract intrinsic.
  /// \param DstType Destination vector or value type.
  /// \param SrcVec Source vector.
  /// \param SubVec Subvector to insert.
  /// \param Idx Index at which to extract or insert.
  /// \param Name Name of the new instruction or value.
  /// @return The created call to the vector.extract intrinsic.
  Value *CreateInsertVector(Type *DstType, Value *SrcVec, Value *SubVec,
                            uint64_t Idx, const Twine &Name = "") {
    return CreateInsertVector(DstType, SrcVec, SubVec, getInt64(Idx), Name);
  }

  /// Create a call to llvm.stacksave
  /// \param Name Name of the new instruction or value.
  /// @return The created call to llvm.stacksave.
  CallInst *CreateStackSave(const Twine &Name = "") {
    const DataLayout &DL = BB->getDataLayout();
    return CreateIntrinsicWithoutFolding(Intrinsic::stacksave,
                                         {DL.getAllocaPtrType(Context)}, {},
                                         nullptr, Name);
  }

  /// Create a call to llvm.stackrestore
  /// \param Ptr Pointer operand.
  /// \param Name Name of the new instruction or value.
  /// @return The created call to llvm.stackrestore.
  CallInst *CreateStackRestore(Value *Ptr, const Twine &Name = "") {
    return CreateIntrinsicWithoutFolding(
        Intrinsic::stackrestore, {Ptr->getType()}, {Ptr}, nullptr, Name);
  }

  /// Create a call to llvm.experimental_cttz_elts
  /// \param ResTy Result integer type.
  /// \param Mask Vector mask selecting active lanes.
  /// \param ZeroIsPoison Whether a zero mask is poison.
  /// \param Name Name of the new instruction or value.
  /// @return The created call to llvm.experimental_cttz_elts.
  Value *CreateCountTrailingZeroElems(Type *ResTy, Value *Mask,
                                      bool ZeroIsPoison = true,
                                      const Twine &Name = "") {
    return CreateIntrinsic(Intrinsic::experimental_cttz_elts,
                           {ResTy, Mask->getType()},
                           {Mask, getInt1(ZeroIsPoison)}, nullptr, Name);
  }

private:
  /// Create a call to a masked intrinsic with given Id.
  /// \param Id The Id parameter.
  /// \param Ops Operands of the N-ary operation.
  /// \param OverloadedTypes The OverloadedTypes parameter.
  /// \param Name Name of the new instruction or value.
  CallInst *CreateMaskedIntrinsic(Intrinsic::ID Id, ArrayRef<Value *> Ops,
                                  ArrayRef<Type *> OverloadedTypes,
                                  const Twine &Name = "");

  //===--------------------------------------------------------------------===//
  // Instruction creation methods: Terminators
  //===--------------------------------------------------------------------===//

private:
  /// Helper to add branch weight and unpredictable metadata onto an
  /// instruction.
  /// \returns The annotated instruction.
  template <typename InstTy>
  /// Add branch metadata.
  /// \param I Instruction to insert, act on, or take debug location from.
  /// \param Weights Branch-weight metadata node.
  /// \param Unpredictable Optional unpredictable-branch metadata.
  InstTy *addBranchMetadata(InstTy *I, MDNode *Weights, MDNode *Unpredictable) {
    if (Weights)
      I->setMetadata(LLVMContext::MD_prof, Weights);
    if (Unpredictable)
      I->setMetadata(LLVMContext::MD_unpredictable, Unpredictable);
    return I;
  }

public:
  /// Create a 'ret void' instruction.
  /// @return The created return instruction.
  ReturnInst *CreateRetVoid() {
    return Insert(ReturnInst::Create(Context));
  }

  /// Create a 'ret <val>' instruction.
  /// \param V Input value.
  /// @return The created return instruction.
  ReturnInst *CreateRet(Value *V) {
    return Insert(ReturnInst::Create(Context, V));
  }

  /// Create an aggregate return from multiple values.
  ///
  /// Builds N insertvalue instructions from RetVals, then a ret of the
  /// resulting aggregate. Convenience for code that uses aggregate returns as a
  /// vehicle for multiple return values.
  /// \param RetVals Values aggregated into the return value.
  /// @return The created aggregate return instruction.
  ReturnInst *CreateAggregateRet(ArrayRef<Value *> RetVals) {
    Value *V = PoisonValue::get(getCurrentFunctionReturnType());
    for (size_t i = 0, N = RetVals.size(); i != N; ++i)
      V = CreateInsertValue(V, RetVals[i], i, "mrv");
    return Insert(ReturnInst::Create(Context, V));
  }

  /// Create an unconditional 'br label X' instruction.
  /// \param Dest Destination basic block.
  /// @return The created branch instruction.
  UncondBrInst *CreateBr(BasicBlock *Dest) {
    return Insert(UncondBrInst::Create(Dest));
  }

  /// Create a conditional 'br Cond, TrueDest, FalseDest'
  /// instruction.
  /// \param Cond Boolean condition value.
  /// \param True True successor basic block.
  /// \param False False successor basic block.
  /// \param BranchWeights Optional branch-weight profile metadata.
  /// \param Unpredictable Optional unpredictable-branch metadata.
  /// @return The created conditional branch instruction.
  CondBrInst *CreateCondBr(Value *Cond, BasicBlock *True, BasicBlock *False,
                           MDNode *BranchWeights = nullptr,
                           MDNode *Unpredictable = nullptr) {
    return Insert(addBranchMetadata(CondBrInst::Create(Cond, True, False),
                                    BranchWeights, Unpredictable));
  }

  /// Create a conditional 'br Cond, TrueDest, FalseDest'
  /// instruction. Copy branch meta data if available.
  /// \param Cond Boolean condition value.
  /// \param True True successor basic block.
  /// \param False False successor basic block.
  /// \param MDSrc Instruction whose branch metadata is copied.
  /// @return The created conditional branch instruction.
  CondBrInst *CreateCondBr(Value *Cond, BasicBlock *True, BasicBlock *False,
                           Instruction *MDSrc) {
    CondBrInst *Br = CondBrInst::Create(Cond, True, False);
    if (MDSrc) {
      unsigned WL[4] = {LLVMContext::MD_prof, LLVMContext::MD_unpredictable,
                        LLVMContext::MD_make_implicit, LLVMContext::MD_dbg};
      Br->copyMetadata(*MDSrc, WL);
    }
    return Insert(Br);
  }

  /// Create a switch instruction with the specified value, default dest,
  /// and with a hint for the number of cases that will be added (for efficient
  /// allocation).
  /// \param V Input value.
  /// \param Dest Destination basic block.
  /// \param NumCases Hint for how many switch cases will be added.
  /// \param BranchWeights Optional branch-weight profile metadata.
  /// \param Unpredictable Optional unpredictable-branch metadata.
  /// @return The created switch instruction.
  SwitchInst *CreateSwitch(Value *V, BasicBlock *Dest, unsigned NumCases = 10,
                           MDNode *BranchWeights = nullptr,
                           MDNode *Unpredictable = nullptr) {
    return Insert(addBranchMetadata(SwitchInst::Create(V, Dest, NumCases),
                                    BranchWeights, Unpredictable));
  }

  /// Create an indirect branch instruction.
  ///
  /// Uses the specified address operand, with an optional hint for the number
  /// of destinations that will be added (for efficient allocation).
  /// \param Addr Indirect branch target address.
  /// \param NumDests Hint for how many indirect destinations will be added.
  /// @return The created indirect branch instruction.
  IndirectBrInst *CreateIndirectBr(Value *Addr, unsigned NumDests = 10) {
    return Insert(IndirectBrInst::Create(Addr, NumDests));
  }

  /// Create an invoke instruction.
  /// \param Ty Type of the loaded, allocated, or created value.
  /// \param Callee Function or function pointer being called.
  /// \param NormalDest Normal (non-unwind) destination block.
  /// \param UnwindDest Unwind destination basic block.
  /// \param Args Argument list for the call.
  /// \param OpBundles Operand bundles attached to the call.
  /// \param Name Name of the new instruction or value.
  /// @return The created invoke instruction.
  InvokeInst *CreateInvoke(FunctionType *Ty, Value *Callee,
                           BasicBlock *NormalDest, BasicBlock *UnwindDest,
                           ArrayRef<Value *> Args,
                           ArrayRef<OperandBundleDef> OpBundles,
                           const Twine &Name = "") {
    InvokeInst *II =
        InvokeInst::Create(Ty, Callee, NormalDest, UnwindDest, Args, OpBundles);
    if (IsFPConstrained)
      setConstrainedFPCallAttr(II);
    return Insert(II, Name);
  }
  /// Create an invoke instruction.
  /// \param Ty Type of the loaded, allocated, or created value.
  /// \param Callee Function or function pointer being called.
  /// \param NormalDest Normal (non-unwind) destination block.
  /// \param UnwindDest Unwind destination basic block.
  /// \param Args Argument list for the call.
  /// \param Name Name of the new instruction or value.
  /// @return The created invoke instruction.
  InvokeInst *CreateInvoke(FunctionType *Ty, Value *Callee,
                           BasicBlock *NormalDest, BasicBlock *UnwindDest,
                           ArrayRef<Value *> Args = {},
                           const Twine &Name = "") {
    InvokeInst *II =
        InvokeInst::Create(Ty, Callee, NormalDest, UnwindDest, Args);
    if (IsFPConstrained)
      setConstrainedFPCallAttr(II);
    return Insert(II, Name);
  }

  /// Create an invoke instruction.
  /// \param Callee Function or function pointer being called.
  /// \param NormalDest Normal (non-unwind) destination block.
  /// \param UnwindDest Unwind destination basic block.
  /// \param Args Argument list for the call.
  /// \param OpBundles Operand bundles attached to the call.
  /// \param Name Name of the new instruction or value.
  /// @return The created invoke instruction.
  InvokeInst *CreateInvoke(FunctionCallee Callee, BasicBlock *NormalDest,
                           BasicBlock *UnwindDest, ArrayRef<Value *> Args,
                           ArrayRef<OperandBundleDef> OpBundles,
                           const Twine &Name = "") {
    return CreateInvoke(Callee.getFunctionType(), Callee.getCallee(),
                        NormalDest, UnwindDest, Args, OpBundles, Name);
  }

  /// Create an invoke instruction.
  /// \param Callee Function or function pointer being called.
  /// \param NormalDest Normal (non-unwind) destination block.
  /// \param UnwindDest Unwind destination basic block.
  /// \param Args Argument list for the call.
  /// \param Name Name of the new instruction or value.
  /// @return The created invoke instruction.
  InvokeInst *CreateInvoke(FunctionCallee Callee, BasicBlock *NormalDest,
                           BasicBlock *UnwindDest, ArrayRef<Value *> Args = {},
                           const Twine &Name = "") {
    return CreateInvoke(Callee.getFunctionType(), Callee.getCallee(),
                        NormalDest, UnwindDest, Args, Name);
  }

  /// \brief Create a callbr instruction.
  /// \param Ty Type of the loaded, allocated, or created value.
  /// \param Callee Function or function pointer being called.
  /// \param DefaultDest Default destination of a callbr.
  /// \param IndirectDests Indirect destinations of a callbr.
  /// \param Args Argument list for the call.
  /// \param Name Name of the new instruction or value.
  /// @return The created callbr instruction.
  CallBrInst *CreateCallBr(FunctionType *Ty, Value *Callee,
                           BasicBlock *DefaultDest,
                           ArrayRef<BasicBlock *> IndirectDests,
                           ArrayRef<Value *> Args = {},
                           const Twine &Name = "") {
    return Insert(CallBrInst::Create(Ty, Callee, DefaultDest, IndirectDests,
                                     Args), Name);
  }
  /// Create a call br.
  /// \param Ty Type of the loaded, allocated, or created value.
  /// \param Callee Function or function pointer being called.
  /// \param DefaultDest Default destination of a callbr.
  /// \param IndirectDests Indirect destinations of a callbr.
  /// \param Args Argument list for the call.
  /// \param OpBundles Operand bundles attached to the call.
  /// \param Name Name of the new instruction or value.
  /// @return The created callbr instruction.
  CallBrInst *CreateCallBr(FunctionType *Ty, Value *Callee,
                           BasicBlock *DefaultDest,
                           ArrayRef<BasicBlock *> IndirectDests,
                           ArrayRef<Value *> Args,
                           ArrayRef<OperandBundleDef> OpBundles,
                           const Twine &Name = "") {
    return Insert(
        CallBrInst::Create(Ty, Callee, DefaultDest, IndirectDests, Args,
                           OpBundles), Name);
  }

  /// Create a call br.
  /// \param Callee Function or function pointer being called.
  /// \param DefaultDest Default destination of a callbr.
  /// \param IndirectDests Indirect destinations of a callbr.
  /// \param Args Argument list for the call.
  /// \param Name Name of the new instruction or value.
  /// @return The created callbr instruction.
  CallBrInst *CreateCallBr(FunctionCallee Callee, BasicBlock *DefaultDest,
                           ArrayRef<BasicBlock *> IndirectDests,
                           ArrayRef<Value *> Args = {},
                           const Twine &Name = "") {
    return CreateCallBr(Callee.getFunctionType(), Callee.getCallee(),
                        DefaultDest, IndirectDests, Args, Name);
  }
  /// Create a call br.
  /// \param Callee Function or function pointer being called.
  /// \param DefaultDest Default destination of a callbr.
  /// \param IndirectDests Indirect destinations of a callbr.
  /// \param Args Argument list for the call.
  /// \param OpBundles Operand bundles attached to the call.
  /// \param Name Name of the new instruction or value.
  /// @return The created callbr instruction.
  CallBrInst *CreateCallBr(FunctionCallee Callee, BasicBlock *DefaultDest,
                           ArrayRef<BasicBlock *> IndirectDests,
                           ArrayRef<Value *> Args,
                           ArrayRef<OperandBundleDef> OpBundles,
                           const Twine &Name = "") {
    return CreateCallBr(Callee.getFunctionType(), Callee.getCallee(),
                        DefaultDest, IndirectDests, Args, Name);
  }

  /// Create a resume.
  /// \param Exn Exception value resumed with.
  /// @return The created resume instruction.
  ResumeInst *CreateResume(Value *Exn) {
    return Insert(ResumeInst::Create(Exn));
  }

  /// Create a cleanup ret.
  /// \param CleanupPad Cleanup pad token for the cleanupret.
  /// \param UnwindBB Unwind destination basic block.
  /// @return The created cleanupret instruction.
  CleanupReturnInst *CreateCleanupRet(CleanupPadInst *CleanupPad,
                                      BasicBlock *UnwindBB = nullptr) {
    return Insert(CleanupReturnInst::Create(CleanupPad, UnwindBB));
  }

  /// Create a catch switch.
  /// \param ParentPad Parent pad token for the EH instruction.
  /// \param UnwindBB Unwind destination basic block.
  /// \param NumHandlers Hint for how many catch handlers will be added.
  /// \param Name Name of the new instruction or value.
  /// @return The created catchswitch instruction.
  CatchSwitchInst *CreateCatchSwitch(Value *ParentPad, BasicBlock *UnwindBB,
                                     unsigned NumHandlers,
                                     const Twine &Name = "") {
    return Insert(CatchSwitchInst::Create(ParentPad, UnwindBB, NumHandlers),
                  Name);
  }

  /// Create a catch pad.
  /// \param ParentPad Parent pad token for the EH instruction.
  /// \param Args Argument list for the call.
  /// \param Name Name of the new instruction or value.
  /// @return The created catchpad instruction.
  CatchPadInst *CreateCatchPad(Value *ParentPad, ArrayRef<Value *> Args,
                               const Twine &Name = "") {
    return Insert(CatchPadInst::Create(ParentPad, Args), Name);
  }

  /// Create a cleanup pad.
  /// \param ParentPad Parent pad token for the EH instruction.
  /// \param Args Argument list for the call.
  /// \param Name Name of the new instruction or value.
  /// @return The created cleanuppad instruction.
  CleanupPadInst *CreateCleanupPad(Value *ParentPad,
                                   ArrayRef<Value *> Args = {},
                                   const Twine &Name = "") {
    return Insert(CleanupPadInst::Create(ParentPad, Args), Name);
  }

  /// Create a catch ret.
  /// \param CatchPad Catch pad token for the catchret.
  /// \param BB The BB parameter.
  /// @return The created catchret instruction.
  CatchReturnInst *CreateCatchRet(CatchPadInst *CatchPad, BasicBlock *BB) {
    return Insert(CatchReturnInst::Create(CatchPad, BB));
  }

  /// Create an unreachable.
  /// @return The created unreachable instruction.
  UnreachableInst *CreateUnreachable() {
    return Insert(new UnreachableInst(Context));
  }

  //===--------------------------------------------------------------------===//
  // Instruction creation methods: Binary Operators
  //===--------------------------------------------------------------------===//
private:
  /// Create an insert nuwnsw bin op.
  /// \param Opc Opcode of the instruction to create.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// \param HasNUW Whether to set the nuw (no unsigned wrap) flag.
  /// \param HasNSW Whether to set the nsw (no signed wrap) flag.
  BinaryOperator *CreateInsertNUWNSWBinOp(BinaryOperator::BinaryOps Opc,
                                          Value *LHS, Value *RHS,
                                          const Twine &Name,
                                          bool HasNUW, bool HasNSW) {
    BinaryOperator *BO = Insert(BinaryOperator::Create(Opc, LHS, RHS), Name);
    if (HasNUW) BO->setHasNoUnsignedWrap();
    if (HasNSW) BO->setHasNoSignedWrap();
    return BO;
  }

  /// Set the fpattrs.
  /// \param I Instruction to insert, act on, or take debug location from.
  /// \param FPMD Floating-point math metadata node.
  /// \param FMF Fast-math flags to store.
  Instruction *setFPAttrs(Instruction *I, MDNode *FPMD,
                          FastMathFlags FMF) const {
    if (!FPMD)
      FPMD = DefaultFPMathTag;
    if (FPMD)
      I->setMetadata(LLVMContext::MD_fpmath, FPMD);
    I->setFastMathFlags(FMF);
    return I;
  }

  /// Get the constrained fprounding.
  /// \param Rounding Rounding mode for a constrained FP intrinsic.
  Value *getConstrainedFPRounding(std::optional<RoundingMode> Rounding) {
    RoundingMode UseRounding = DefaultConstrainedRounding;

    if (Rounding)
      UseRounding = *Rounding;

    std::optional<StringRef> RoundingStr =
        convertRoundingModeToStr(UseRounding);
    assert(RoundingStr && "Garbage strict rounding mode!");
    auto *RoundingMDS = MDString::get(Context, *RoundingStr);

    return MetadataAsValue::get(Context, RoundingMDS);
  }

  /// Get the constrained fpexcept.
  /// \param Except Exception behavior for a constrained FP intrinsic.
  Value *getConstrainedFPExcept(std::optional<fp::ExceptionBehavior> Except) {
    std::optional<StringRef> ExceptStr = convertExceptionBehaviorToStr(
        Except.value_or(DefaultConstrainedExcept));
    assert(ExceptStr && "Garbage strict exception behavior!");
    auto *ExceptMDS = MDString::get(Context, *ExceptStr);

    return MetadataAsValue::get(Context, ExceptMDS);
  }

  /// Get the constrained fppredicate.
  /// \param Predicate Comparison predicate.
  Value *getConstrainedFPPredicate(CmpInst::Predicate Predicate) {
    assert(CmpInst::isFPPredicate(Predicate) &&
           Predicate != CmpInst::FCMP_FALSE &&
           Predicate != CmpInst::FCMP_TRUE &&
           "Invalid constrained FP comparison predicate!");

    StringRef PredicateStr = CmpInst::getPredicateName(Predicate);
    auto *PredicateMDS = MDString::get(Context, PredicateStr);

    return MetadataAsValue::get(Context, PredicateMDS);
  }

public:
  /// Create an add instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// \param HasNUW Whether to set the nuw (no unsigned wrap) flag.
  /// \param HasNSW Whether to set the nsw (no signed wrap) flag.
  /// @return The created add instruction.
  Value *CreateAdd(Value *LHS, Value *RHS, const Twine &Name = "",
                   bool HasNUW = false, bool HasNSW = false) {
    if (Value *V =
            Folder.FoldNoWrapBinOp(Instruction::Add, LHS, RHS, HasNUW, HasNSW))
      return V;
    return CreateInsertNUWNSWBinOp(Instruction::Add, LHS, RHS, Name, HasNUW,
                                   HasNSW);
  }

  /// Create an nsw add instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// @return The created nsw add instruction.
  Value *CreateNSWAdd(Value *LHS, Value *RHS, const Twine &Name = "") {
    return CreateAdd(LHS, RHS, Name, false, true);
  }

  /// Create an nuw add instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// @return The created nuw add instruction.
  Value *CreateNUWAdd(Value *LHS, Value *RHS, const Twine &Name = "") {
    return CreateAdd(LHS, RHS, Name, true, false);
  }

  /// Create a sub instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// \param HasNUW Whether to set the nuw (no unsigned wrap) flag.
  /// \param HasNSW Whether to set the nsw (no signed wrap) flag.
  /// @return The created sub instruction.
  Value *CreateSub(Value *LHS, Value *RHS, const Twine &Name = "",
                   bool HasNUW = false, bool HasNSW = false) {
    if (Value *V =
            Folder.FoldNoWrapBinOp(Instruction::Sub, LHS, RHS, HasNUW, HasNSW))
      return V;
    return CreateInsertNUWNSWBinOp(Instruction::Sub, LHS, RHS, Name, HasNUW,
                                   HasNSW);
  }

  /// Create an nsw sub instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// @return The created nsw sub instruction.
  Value *CreateNSWSub(Value *LHS, Value *RHS, const Twine &Name = "") {
    return CreateSub(LHS, RHS, Name, false, true);
  }

  /// Create an nuw sub instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// @return The created nuw sub instruction.
  Value *CreateNUWSub(Value *LHS, Value *RHS, const Twine &Name = "") {
    return CreateSub(LHS, RHS, Name, true, false);
  }

  /// Create a mul instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// \param HasNUW Whether to set the nuw (no unsigned wrap) flag.
  /// \param HasNSW Whether to set the nsw (no signed wrap) flag.
  /// @return The created mul instruction.
  Value *CreateMul(Value *LHS, Value *RHS, const Twine &Name = "",
                   bool HasNUW = false, bool HasNSW = false) {
    if (Value *V =
            Folder.FoldNoWrapBinOp(Instruction::Mul, LHS, RHS, HasNUW, HasNSW))
      return V;
    return CreateInsertNUWNSWBinOp(Instruction::Mul, LHS, RHS, Name, HasNUW,
                                   HasNSW);
  }

  /// Create an nsw mul instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// @return The created nsw mul instruction.
  Value *CreateNSWMul(Value *LHS, Value *RHS, const Twine &Name = "") {
    return CreateMul(LHS, RHS, Name, false, true);
  }

  /// Create an nuw mul instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// @return The created nuw mul instruction.
  Value *CreateNUWMul(Value *LHS, Value *RHS, const Twine &Name = "") {
    return CreateMul(LHS, RHS, Name, true, false);
  }

  /// Create an u div instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// \param isExact Whether to set the exact flag.
  /// @return The created u div instruction.
  Value *CreateUDiv(Value *LHS, Value *RHS, const Twine &Name = "",
                    bool isExact = false) {
    if (Value *V = Folder.FoldExactBinOp(Instruction::UDiv, LHS, RHS, isExact))
      return V;
    if (!isExact)
      return Insert(BinaryOperator::CreateUDiv(LHS, RHS), Name);
    return Insert(BinaryOperator::CreateExactUDiv(LHS, RHS), Name);
  }

  /// Create an exact udiv instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// @return The created exact udiv instruction.
  Value *CreateExactUDiv(Value *LHS, Value *RHS, const Twine &Name = "") {
    return CreateUDiv(LHS, RHS, Name, true);
  }

  /// Create a s div instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// \param isExact Whether to set the exact flag.
  /// @return The created s div instruction.
  Value *CreateSDiv(Value *LHS, Value *RHS, const Twine &Name = "",
                    bool isExact = false) {
    if (Value *V = Folder.FoldExactBinOp(Instruction::SDiv, LHS, RHS, isExact))
      return V;
    if (!isExact)
      return Insert(BinaryOperator::CreateSDiv(LHS, RHS), Name);
    return Insert(BinaryOperator::CreateExactSDiv(LHS, RHS), Name);
  }

  /// Create an exact sdiv instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// @return The created exact sdiv instruction.
  Value *CreateExactSDiv(Value *LHS, Value *RHS, const Twine &Name = "") {
    return CreateSDiv(LHS, RHS, Name, true);
  }

  /// Create an u rem instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// @return The created u rem instruction.
  Value *CreateURem(Value *LHS, Value *RHS, const Twine &Name = "") {
    if (Value *V = Folder.FoldBinOp(Instruction::URem, LHS, RHS))
      return V;
    return Insert(BinaryOperator::CreateURem(LHS, RHS), Name);
  }

  /// Create a s rem instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// @return The created s rem instruction.
  Value *CreateSRem(Value *LHS, Value *RHS, const Twine &Name = "") {
    if (Value *V = Folder.FoldBinOp(Instruction::SRem, LHS, RHS))
      return V;
    return Insert(BinaryOperator::CreateSRem(LHS, RHS), Name);
  }

  /// Create a shl instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// \param HasNUW Whether to set the nuw (no unsigned wrap) flag.
  /// \param HasNSW Whether to set the nsw (no signed wrap) flag.
  /// @return The created shl instruction.
  Value *CreateShl(Value *LHS, Value *RHS, const Twine &Name = "",
                   bool HasNUW = false, bool HasNSW = false) {
    if (Value *V =
            Folder.FoldNoWrapBinOp(Instruction::Shl, LHS, RHS, HasNUW, HasNSW))
      return V;
    return CreateInsertNUWNSWBinOp(Instruction::Shl, LHS, RHS, Name,
                                   HasNUW, HasNSW);
  }

  /// Create a shl instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// \param HasNUW Whether to set the nuw (no unsigned wrap) flag.
  /// \param HasNSW Whether to set the nsw (no signed wrap) flag.
  /// @return The created shl instruction.
  Value *CreateShl(Value *LHS, const APInt &RHS, const Twine &Name = "",
                   bool HasNUW = false, bool HasNSW = false) {
    return CreateShl(LHS, ConstantInt::get(LHS->getType(), RHS), Name,
                     HasNUW, HasNSW);
  }

  /// Create a shl instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// \param HasNUW Whether to set the nuw (no unsigned wrap) flag.
  /// \param HasNSW Whether to set the nsw (no signed wrap) flag.
  /// @return The created shl instruction.
  Value *CreateShl(Value *LHS, uint64_t RHS, const Twine &Name = "",
                   bool HasNUW = false, bool HasNSW = false) {
    return CreateShl(LHS, ConstantInt::get(LHS->getType(), RHS), Name,
                     HasNUW, HasNSW);
  }

  /// Create an l shr instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// \param isExact Whether to set the exact flag.
  /// @return The created l shr instruction.
  Value *CreateLShr(Value *LHS, Value *RHS, const Twine &Name = "",
                    bool isExact = false) {
    if (Value *V = Folder.FoldExactBinOp(Instruction::LShr, LHS, RHS, isExact))
      return V;
    if (!isExact)
      return Insert(BinaryOperator::CreateLShr(LHS, RHS), Name);
    return Insert(BinaryOperator::CreateExactLShr(LHS, RHS), Name);
  }

  /// Create an l shr instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// \param isExact Whether to set the exact flag.
  /// @return The created l shr instruction.
  Value *CreateLShr(Value *LHS, const APInt &RHS, const Twine &Name = "",
                    bool isExact = false) {
    return CreateLShr(LHS, ConstantInt::get(LHS->getType(), RHS), Name,isExact);
  }

  /// Create an l shr instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// \param isExact Whether to set the exact flag.
  /// @return The created l shr instruction.
  Value *CreateLShr(Value *LHS, uint64_t RHS, const Twine &Name = "",
                    bool isExact = false) {
    return CreateLShr(LHS, ConstantInt::get(LHS->getType(), RHS), Name,isExact);
  }

  /// Create an a shr instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// \param isExact Whether to set the exact flag.
  /// @return The created a shr instruction.
  Value *CreateAShr(Value *LHS, Value *RHS, const Twine &Name = "",
                    bool isExact = false) {
    if (Value *V = Folder.FoldExactBinOp(Instruction::AShr, LHS, RHS, isExact))
      return V;
    if (!isExact)
      return Insert(BinaryOperator::CreateAShr(LHS, RHS), Name);
    return Insert(BinaryOperator::CreateExactAShr(LHS, RHS), Name);
  }

  /// Create an a shr instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// \param isExact Whether to set the exact flag.
  /// @return The created a shr instruction.
  Value *CreateAShr(Value *LHS, const APInt &RHS, const Twine &Name = "",
                    bool isExact = false) {
    return CreateAShr(LHS, ConstantInt::get(LHS->getType(), RHS), Name,isExact);
  }

  /// Create an a shr instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// \param isExact Whether to set the exact flag.
  /// @return The created a shr instruction.
  Value *CreateAShr(Value *LHS, uint64_t RHS, const Twine &Name = "",
                    bool isExact = false) {
    return CreateAShr(LHS, ConstantInt::get(LHS->getType(), RHS), Name,isExact);
  }

  /// Create an and instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// @return The created and instruction.
  Value *CreateAnd(Value *LHS, Value *RHS, const Twine &Name = "") {
    if (auto *V = Folder.FoldBinOp(Instruction::And, LHS, RHS))
      return V;
    return Insert(BinaryOperator::CreateAnd(LHS, RHS), Name);
  }

  /// Create an and instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// @return The created and instruction.
  Value *CreateAnd(Value *LHS, const APInt &RHS, const Twine &Name = "") {
    return CreateAnd(LHS, ConstantInt::get(LHS->getType(), RHS), Name);
  }

  /// Create an and instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// @return The created and instruction.
  Value *CreateAnd(Value *LHS, uint64_t RHS, const Twine &Name = "") {
    return CreateAnd(LHS, ConstantInt::get(LHS->getType(), RHS), Name);
  }

  /// Create an and instruction.
  /// \param Ops Operands of the N-ary operation.
  /// @return The created and instruction.
  Value *CreateAnd(ArrayRef<Value*> Ops) {
    assert(!Ops.empty());
    Value *Accum = Ops[0];
    for (unsigned i = 1; i < Ops.size(); i++)
      Accum = CreateAnd(Accum, Ops[i]);
    return Accum;
  }

  /// Create an or instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// \param IsDisjoint Whether to set the disjoint flag on an or.
  /// @return The created or instruction.
  Value *CreateOr(Value *LHS, Value *RHS, const Twine &Name = "",
                  bool IsDisjoint = false) {
    if (auto *V = Folder.FoldBinOp(Instruction::Or, LHS, RHS))
      return V;
    return Insert(
        IsDisjoint ? BinaryOperator::CreateDisjoint(Instruction::Or, LHS, RHS)
                   : BinaryOperator::CreateOr(LHS, RHS),
        Name);
  }

  /// Create an or instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// @return The created or instruction.
  Value *CreateOr(Value *LHS, const APInt &RHS, const Twine &Name = "") {
    return CreateOr(LHS, ConstantInt::get(LHS->getType(), RHS), Name);
  }

  /// Create an or instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// @return The created or instruction.
  Value *CreateOr(Value *LHS, uint64_t RHS, const Twine &Name = "") {
    return CreateOr(LHS, ConstantInt::get(LHS->getType(), RHS), Name);
  }

  /// Create an or instruction.
  /// \param Ops Operands of the N-ary operation.
  /// @return The created or instruction.
  Value *CreateOr(ArrayRef<Value*> Ops) {
    assert(!Ops.empty());
    Value *Accum = Ops[0];
    for (unsigned i = 1; i < Ops.size(); i++)
      Accum = CreateOr(Accum, Ops[i]);
    return Accum;
  }

  /// Create a disjoint or.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// @return The created disjoint or.
  Value *CreateDisjointOr(Value *LHS, Value *RHS, const Twine &Name = "") {
    return CreateOr(LHS, RHS, Name, true);
  }

  /// Create a xor instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// @return The created xor instruction.
  Value *CreateXor(Value *LHS, Value *RHS, const Twine &Name = "") {
    if (Value *V = Folder.FoldBinOp(Instruction::Xor, LHS, RHS))
      return V;
    return Insert(BinaryOperator::CreateXor(LHS, RHS), Name);
  }

  /// Create a xor instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// @return The created xor instruction.
  Value *CreateXor(Value *LHS, const APInt &RHS, const Twine &Name = "") {
    return CreateXor(LHS, ConstantInt::get(LHS->getType(), RHS), Name);
  }

  /// Create a xor instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// @return The created xor instruction.
  Value *CreateXor(Value *LHS, uint64_t RHS, const Twine &Name = "") {
    return CreateXor(LHS, ConstantInt::get(LHS->getType(), RHS), Name);
  }

  /// Create a f add instruction.
  /// \param L Debug location to apply to created instructions.
  /// \param R Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// \param FPMD Floating-point math metadata node.
  /// @return The created f add instruction.
  Value *CreateFAdd(Value *L, Value *R, const Twine &Name = "",
                    MDNode *FPMD = nullptr) {
    return CreateFAddFMF(L, R, {}, Name, FPMD);
  }

  /// Create a fadd with fast-math flags.
  /// \param L Debug location to apply to created instructions.
  /// \param R Right-hand side operand.
  /// \param FMFSource Optional source of fast-math flags.
  /// \param Name Name of the new instruction or value.
  /// \param FPMD Floating-point math metadata node.
  /// @return The created fadd with fast-math flags.
  Value *CreateFAddFMF(Value *L, Value *R, FMFSource FMFSource,
                       const Twine &Name = "", MDNode *FPMD = nullptr) {
    if (IsFPConstrained)
      return CreateConstrainedFPBinOp(Intrinsic::experimental_constrained_fadd,
                                      L, R, FMFSource, Name, FPMD);

    if (Value *V =
            Folder.FoldBinOpFMF(Instruction::FAdd, L, R, FMFSource.get(FMF)))
      return V;
    Instruction *I =
        setFPAttrs(BinaryOperator::CreateFAdd(L, R), FPMD, FMFSource.get(FMF));
    return Insert(I, Name);
  }

  /// Create a f sub instruction.
  /// \param L Debug location to apply to created instructions.
  /// \param R Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// \param FPMD Floating-point math metadata node.
  /// @return The created f sub instruction.
  Value *CreateFSub(Value *L, Value *R, const Twine &Name = "",
                    MDNode *FPMD = nullptr) {
    return CreateFSubFMF(L, R, {}, Name, FPMD);
  }

  /// Create a fsub with fast-math flags.
  /// \param L Debug location to apply to created instructions.
  /// \param R Right-hand side operand.
  /// \param FMFSource Optional source of fast-math flags.
  /// \param Name Name of the new instruction or value.
  /// \param FPMD Floating-point math metadata node.
  /// @return The created fsub with fast-math flags.
  Value *CreateFSubFMF(Value *L, Value *R, FMFSource FMFSource,
                       const Twine &Name = "", MDNode *FPMD = nullptr) {
    if (IsFPConstrained)
      return CreateConstrainedFPBinOp(Intrinsic::experimental_constrained_fsub,
                                      L, R, FMFSource, Name, FPMD);

    if (Value *V =
            Folder.FoldBinOpFMF(Instruction::FSub, L, R, FMFSource.get(FMF)))
      return V;
    Instruction *I =
        setFPAttrs(BinaryOperator::CreateFSub(L, R), FPMD, FMFSource.get(FMF));
    return Insert(I, Name);
  }

  /// Create a f mul instruction.
  /// \param L Debug location to apply to created instructions.
  /// \param R Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// \param FPMD Floating-point math metadata node.
  /// @return The created f mul instruction.
  Value *CreateFMul(Value *L, Value *R, const Twine &Name = "",
                    MDNode *FPMD = nullptr) {
    return CreateFMulFMF(L, R, {}, Name, FPMD);
  }

  /// Create a fmul with fast-math flags.
  /// \param L Debug location to apply to created instructions.
  /// \param R Right-hand side operand.
  /// \param FMFSource Optional source of fast-math flags.
  /// \param Name Name of the new instruction or value.
  /// \param FPMD Floating-point math metadata node.
  /// @return The created fmul with fast-math flags.
  Value *CreateFMulFMF(Value *L, Value *R, FMFSource FMFSource,
                       const Twine &Name = "", MDNode *FPMD = nullptr) {
    if (IsFPConstrained)
      return CreateConstrainedFPBinOp(Intrinsic::experimental_constrained_fmul,
                                      L, R, FMFSource, Name, FPMD);

    if (Value *V =
            Folder.FoldBinOpFMF(Instruction::FMul, L, R, FMFSource.get(FMF)))
      return V;
    Instruction *I =
        setFPAttrs(BinaryOperator::CreateFMul(L, R), FPMD, FMFSource.get(FMF));
    return Insert(I, Name);
  }

  /// Create a f div instruction.
  /// \param L Debug location to apply to created instructions.
  /// \param R Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// \param FPMD Floating-point math metadata node.
  /// @return The created f div instruction.
  Value *CreateFDiv(Value *L, Value *R, const Twine &Name = "",
                    MDNode *FPMD = nullptr) {
    return CreateFDivFMF(L, R, {}, Name, FPMD);
  }

  /// Create a fdiv with fast-math flags.
  /// \param L Debug location to apply to created instructions.
  /// \param R Right-hand side operand.
  /// \param FMFSource Optional source of fast-math flags.
  /// \param Name Name of the new instruction or value.
  /// \param FPMD Floating-point math metadata node.
  /// @return The created fdiv with fast-math flags.
  Value *CreateFDivFMF(Value *L, Value *R, FMFSource FMFSource,
                       const Twine &Name = "", MDNode *FPMD = nullptr) {
    if (IsFPConstrained)
      return CreateConstrainedFPBinOp(Intrinsic::experimental_constrained_fdiv,
                                      L, R, FMFSource, Name, FPMD);

    if (Value *V =
            Folder.FoldBinOpFMF(Instruction::FDiv, L, R, FMFSource.get(FMF)))
      return V;
    Instruction *I =
        setFPAttrs(BinaryOperator::CreateFDiv(L, R), FPMD, FMFSource.get(FMF));
    return Insert(I, Name);
  }

  /// Create a f rem instruction.
  /// \param L Debug location to apply to created instructions.
  /// \param R Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// \param FPMD Floating-point math metadata node.
  /// @return The created f rem instruction.
  Value *CreateFRem(Value *L, Value *R, const Twine &Name = "",
                    MDNode *FPMD = nullptr) {
    return CreateFRemFMF(L, R, {}, Name, FPMD);
  }

  /// Create a frem with fast-math flags.
  /// \param L Debug location to apply to created instructions.
  /// \param R Right-hand side operand.
  /// \param FMFSource Optional source of fast-math flags.
  /// \param Name Name of the new instruction or value.
  /// \param FPMD Floating-point math metadata node.
  /// @return The created frem with fast-math flags.
  Value *CreateFRemFMF(Value *L, Value *R, FMFSource FMFSource,
                       const Twine &Name = "", MDNode *FPMD = nullptr) {
    if (IsFPConstrained)
      return CreateConstrainedFPBinOp(Intrinsic::experimental_constrained_frem,
                                      L, R, FMFSource, Name, FPMD);

    if (Value *V =
            Folder.FoldBinOpFMF(Instruction::FRem, L, R, FMFSource.get(FMF)))
      return V;
    Instruction *I =
        setFPAttrs(BinaryOperator::CreateFRem(L, R), FPMD, FMFSource.get(FMF));
    return Insert(I, Name);
  }

  /// Create a bin op.
  /// \param Opc Opcode of the instruction to create.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// @return The created bin op.
  Value *CreateBinOp(Instruction::BinaryOps Opc,
                     Value *LHS, Value *RHS, const Twine &Name = "",
                     MDNode *FPMathTag = nullptr) {
    return CreateBinOpFMF(Opc, LHS, RHS, {}, Name, FPMathTag);
  }

  /// Create a binop with fast-math flags.
  /// \param Opc Opcode of the instruction to create.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param FMFSource Optional source of fast-math flags.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// @return The created binop with fast-math flags.
  Value *CreateBinOpFMF(Instruction::BinaryOps Opc, Value *LHS, Value *RHS,
                        FMFSource FMFSource, const Twine &Name = "",
                        MDNode *FPMathTag = nullptr) {
    if (Value *V = Folder.FoldBinOp(Opc, LHS, RHS))
      return V;
    Instruction *BinOp = BinaryOperator::Create(Opc, LHS, RHS);
    if (isa<FPMathOperator>(BinOp))
      setFPAttrs(BinOp, FPMathTag, FMFSource.get(FMF));
    return Insert(BinOp, Name);
  }

  /// Create a no wrap bin op.
  /// \param Opc Opcode of the instruction to create.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param IsNUW Whether the pointer difference is nuw.
  /// \param IsNSW Whether to set the nsw flag.
  /// \param Name Name of the new instruction or value.
  /// @return The created no wrap bin op.
  Value *CreateNoWrapBinOp(Instruction::BinaryOps Opc, Value *LHS, Value *RHS,
                           bool IsNUW, bool IsNSW, const Twine &Name = "") {
    if (Value *V = Folder.FoldNoWrapBinOp(Opc, LHS, RHS, IsNUW, IsNSW))
      return V;
    Instruction *BinOp = BinaryOperator::Create(Opc, LHS, RHS);
    if (IsNUW)
      BinOp->setHasNoUnsignedWrap(IsNUW);
    if (IsNSW)
      BinOp->setHasNoSignedWrap(IsNSW);
    return Insert(BinOp, Name);
  }

  /// Create an exact binop instruction.
  /// \param Opc Opcode of the instruction to create.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param IsExact The IsExact parameter.
  /// \param Name Name of the new instruction or value.
  /// @return The created exact binop instruction.
  Value *CreateExactBinOp(Instruction::BinaryOps Opc, Value *LHS, Value *RHS,
                          bool IsExact, const Twine &Name = "") {
    if (Value *V = Folder.FoldExactBinOp(Opc, LHS, RHS, IsExact))
      return V;
    Instruction *BinOp = BinaryOperator::Create(Opc, LHS, RHS);
    if (IsExact)
      BinOp->setIsExact(IsExact);
    return Insert(BinOp, Name);
  }

  /// Create a logical and.
  /// \param Cond1 The Cond1 parameter.
  /// \param Cond2 The Cond2 parameter.
  /// \param Name Name of the new instruction or value.
  /// \param MDFrom The MDFrom parameter.
  /// @return The created logical and.
  Value *CreateLogicalAnd(Value *Cond1, Value *Cond2, const Twine &Name = "",
                          Instruction *MDFrom = nullptr) {
    assert(Cond2->getType()->isIntOrIntVectorTy(1));
    return CreateSelect(Cond1, Cond2,
                        ConstantInt::getNullValue(Cond2->getType()), Name,
                        MDFrom);
  }

  /// Create a logical or.
  /// \param Cond1 The Cond1 parameter.
  /// \param Cond2 The Cond2 parameter.
  /// \param Name Name of the new instruction or value.
  /// \param MDFrom The MDFrom parameter.
  /// @return The created logical or.
  Value *CreateLogicalOr(Value *Cond1, Value *Cond2, const Twine &Name = "",
                         Instruction *MDFrom = nullptr) {
    assert(Cond2->getType()->isIntOrIntVectorTy(1));
    return CreateSelect(Cond1, ConstantInt::getAllOnesValue(Cond2->getType()),
                        Cond2, Name, MDFrom);
  }

  /// Create a logical op.
  /// \param Opc Opcode of the instruction to create.
  /// \param Cond1 The Cond1 parameter.
  /// \param Cond2 The Cond2 parameter.
  /// \param Name Name of the new instruction or value.
  /// \param MDFrom The MDFrom parameter.
  /// @return The created logical op.
  Value *CreateLogicalOp(Instruction::BinaryOps Opc, Value *Cond1, Value *Cond2,
                         const Twine &Name = "",
                         Instruction *MDFrom = nullptr) {
    switch (Opc) {
    case Instruction::And:
      return CreateLogicalAnd(Cond1, Cond2, Name, MDFrom);
    case Instruction::Or:
      return CreateLogicalOr(Cond1, Cond2, Name, MDFrom);
    default:
      break;
    }
    llvm_unreachable("Not a logical operation.");
  }

  // NOTE: this is sequential, non-commutative, ordered reduction!
  /// Create a logical or.
  /// \param Ops Operands of the N-ary operation.
  /// @return The created logical or.
  Value *CreateLogicalOr(ArrayRef<Value *> Ops) {
    assert(!Ops.empty());
    Value *Accum = Ops[0];
    for (unsigned i = 1; i < Ops.size(); i++)
      Accum = CreateLogicalOr(Accum, Ops[i]);
    return Accum;
  }

  /// Create a constrained floating-point intrinsic call.
  ///
  /// Like CreateIntrinsic for constrained FP intrinsics. Sets rounding mode and
  /// exception behavior from Rounding and Except, and applies FPMathTag /
  /// fast-math flags from FMFSource when provided.
  /// \param ID Intrinsic or statepoint identifier.
  /// \param Types Overload types for a constrained FP intrinsic.
  /// \param Args Argument list for the call.
  /// \param FMFSource Optional source of fast-math flags.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// \param Rounding Rounding mode for a constrained FP intrinsic.
  /// \param Except Exception behavior for a constrained FP intrinsic.
  /// @return The created constrained floating-point intrinsic call.
  LLVM_ABI CallInst *CreateConstrainedFPIntrinsic(
      Intrinsic::ID ID, ArrayRef<Type *> Types, ArrayRef<Value *> Args,
      FMFSource FMFSource, const Twine &Name, MDNode *FPMathTag = nullptr,
      std::optional<RoundingMode> Rounding = std::nullopt,
      std::optional<fp::ExceptionBehavior> Except = std::nullopt);

  /// Create a constrained floating-point binop.
  /// \param ID Intrinsic or statepoint identifier.
  /// \param L Debug location to apply to created instructions.
  /// \param R Right-hand side operand.
  /// \param FMFSource Optional source of fast-math flags.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// \param Rounding Rounding mode for a constrained FP intrinsic.
  /// \param Except Exception behavior for a constrained FP intrinsic.
  /// @return The created constrained floating-point binary op.
  LLVM_ABI CallInst *CreateConstrainedFPBinOp(
      Intrinsic::ID ID, Value *L, Value *R, FMFSource FMFSource = {},
      const Twine &Name = "", MDNode *FPMathTag = nullptr,
      std::optional<RoundingMode> Rounding = std::nullopt,
      std::optional<fp::ExceptionBehavior> Except = std::nullopt);

  /// Create a constrained FP binop without rounding metadata.
  /// \param ID Intrinsic or statepoint identifier.
  /// \param L Debug location to apply to created instructions.
  /// \param R Right-hand side operand.
  /// \param FMFSource Optional source of fast-math flags.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// \param Except Exception behavior for a constrained FP intrinsic.
  /// @return The created constrained floating-point binary op.
  LLVM_ABI CallInst *CreateConstrainedFPUnroundedBinOp(
      Intrinsic::ID ID, Value *L, Value *R, FMFSource FMFSource = {},
      const Twine &Name = "", MDNode *FPMathTag = nullptr,
      std::optional<fp::ExceptionBehavior> Except = std::nullopt);

  /// Create a neg instruction.
  /// \param V Input value.
  /// \param Name Name of the new instruction or value.
  /// \param HasNSW Whether to set the nsw (no signed wrap) flag.
  /// @return The created neg instruction.
  Value *CreateNeg(Value *V, const Twine &Name = "", bool HasNSW = false) {
    return CreateSub(Constant::getNullValue(V->getType()), V, Name,
                     /*HasNUW=*/0, HasNSW);
  }

  /// Create an nsw neg instruction.
  /// \param V Input value.
  /// \param Name Name of the new instruction or value.
  /// @return The created nsw neg instruction.
  Value *CreateNSWNeg(Value *V, const Twine &Name = "") {
    return CreateNeg(V, Name, /*HasNSW=*/true);
  }

  /// Create a f neg.
  /// \param V Input value.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// @return The created f neg.
  Value *CreateFNeg(Value *V, const Twine &Name = "",
                    MDNode *FPMathTag = nullptr) {
    return CreateFNegFMF(V, {}, Name, FPMathTag);
  }

  /// Create a fneg with fast-math flags.
  /// \param V Input value.
  /// \param FMFSource Optional source of fast-math flags.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// @return The created fneg with fast-math flags.
  Value *CreateFNegFMF(Value *V, FMFSource FMFSource, const Twine &Name = "",
                       MDNode *FPMathTag = nullptr) {
    if (Value *Res =
            Folder.FoldUnOpFMF(Instruction::FNeg, V, FMFSource.get(FMF)))
      return Res;
    return Insert(
        setFPAttrs(UnaryOperator::CreateFNeg(V), FPMathTag, FMFSource.get(FMF)),
        Name);
  }

  /// Create a not instruction.
  /// \param V Input value.
  /// \param Name Name of the new instruction or value.
  /// @return The created not instruction.
  Value *CreateNot(Value *V, const Twine &Name = "") {
    return CreateXor(V, Constant::getAllOnesValue(V->getType()), Name);
  }

  /// Create an un op.
  /// \param Opc Opcode of the instruction to create.
  /// \param V Input value.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// @return The created un op.
  Value *CreateUnOp(Instruction::UnaryOps Opc,
                    Value *V, const Twine &Name = "",
                    MDNode *FPMathTag = nullptr) {
    if (Value *Res = Folder.FoldUnOpFMF(Opc, V, FMF))
      return Res;
    Instruction *UnOp = UnaryOperator::Create(Opc, V);
    if (isa<FPMathOperator>(UnOp))
      setFPAttrs(UnOp, FPMathTag, FMF);
    return Insert(UnOp, Name);
  }

  /// Create either a UnaryOperator or BinaryOperator depending on \p Opc.
  /// Correct number of operands must be passed accordingly.
  /// \param Opc Opcode of the instruction to create.
  /// \param Ops Operands of the N-ary operation.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// @return The created either a UnaryOperator or BinaryOperator depending on \p Opc.
  LLVM_ABI Value *CreateNAryOp(unsigned Opc, ArrayRef<Value *> Ops,
                               const Twine &Name = "",
                               MDNode *FPMathTag = nullptr);

  //===--------------------------------------------------------------------===//
  // Instruction creation methods: Memory Instructions
  //===--------------------------------------------------------------------===//

  /// Create an alloca instruction.
  /// \param Ty Type of the loaded, allocated, or created value.
  /// \param AddrSpace Address space of the pointer or global.
  /// \param ArraySize Number of elements to allocate, or nullptr for one.
  /// \param Name Name of the new instruction or value.
  /// @return The created alloca instruction.
  AllocaInst *CreateAlloca(Type *Ty, unsigned AddrSpace,
                           Value *ArraySize = nullptr, const Twine &Name = "") {
    const DataLayout &DL = BB->getDataLayout();
    Align AllocaAlign = DL.getPrefTypeAlign(Ty);
    return Insert(new AllocaInst(Ty, AddrSpace, ArraySize, AllocaAlign), Name);
  }

  /// Create an alloca instruction.
  /// \param Ty Type of the loaded, allocated, or created value.
  /// \param ArraySize Number of elements to allocate, or nullptr for one.
  /// \param Name Name of the new instruction or value.
  /// @return The created alloca instruction.
  AllocaInst *CreateAlloca(Type *Ty, Value *ArraySize = nullptr,
                           const Twine &Name = "") {
    const DataLayout &DL = BB->getDataLayout();
    Align AllocaAlign = DL.getPrefTypeAlign(Ty);
    unsigned AddrSpace = DL.getAllocaAddrSpace();
    return Insert(new AllocaInst(Ty, AddrSpace, ArraySize, AllocaAlign), Name);
  }

  /// Create a structured alloca.
  /// \param BaseType Element type attached as an attribute.
  /// \param Name Name of the new instruction or value.
  /// @return The created structured alloca instruction.
  CallInst *CreateStructuredAlloca(Type *BaseType, const Twine &Name = "") {
    const DataLayout &DL = BB->getDataLayout();
    PointerType *PtrTy = DL.getAllocaPtrType(Context);
    auto *Output = CreateIntrinsicWithoutFolding(Intrinsic::structured_alloca,
                                                 {PtrTy}, {}, {}, Name);
    Output->addRetAttr(
        Attribute::get(getContext(), Attribute::ElementType, BaseType));
    return Output;
  }

  /// Provided to resolve 'CreateLoad(Ty, Ptr, "...")' correctly, instead of
  /// converting the string to 'bool' for the isVolatile parameter.
  /// \param Ty Type of the loaded, allocated, or created value.
  /// \param Ptr Pointer operand.
  /// \param Name Name of the new instruction or value.
  /// @return The created load instruction.
  LoadInst *CreateLoad(Type *Ty, Value *Ptr, const char *Name) {
    return CreateAlignedLoad(Ty, Ptr, MaybeAlign(), Name);
  }

  /// Create a load instruction.
  /// \param Ty Type of the loaded, allocated, or created value.
  /// \param Ptr Pointer operand.
  /// \param Name Name of the new instruction or value.
  /// @return The created load instruction.
  LoadInst *CreateLoad(Type *Ty, Value *Ptr, const Twine &Name = "") {
    return CreateAlignedLoad(Ty, Ptr, MaybeAlign(), Name);
  }

  /// Create a load instruction.
  /// \param Ty Type of the loaded, allocated, or created value.
  /// \param Ptr Pointer operand.
  /// \param isVolatile Whether the memory intrinsic is volatile.
  /// \param Name Name of the new instruction or value.
  /// @return The created load instruction.
  LoadInst *CreateLoad(Type *Ty, Value *Ptr, bool isVolatile,
                       const Twine &Name = "") {
    return CreateAlignedLoad(Ty, Ptr, MaybeAlign(), isVolatile, Name);
  }

  /// Create a load instruction.
  /// \param Ty Type of the loaded, allocated, or created value.
  /// \param Ptr Pointer operand.
  /// \param Props Load/store properties (volatile, align, ordering, etc.).
  /// \param Name Name of the new instruction or value.
  /// @return The created load instruction.
  LoadInst *CreateLoad(Type *Ty, Value *Ptr,
                       const LoadStoreInstProperties &Props,
                       const Twine &Name = "") {
    return Insert(new LoadInst(Ty, Ptr, Twine(), Props), Name);
  }

  /// Create a store instruction.
  /// \param Val Stored or broadcast value.
  /// \param Ptr Pointer operand.
  /// \param isVolatile Whether the memory intrinsic is volatile.
  /// @return The created store instruction.
  StoreInst *CreateStore(Value *Val, Value *Ptr, bool isVolatile = false) {
    return CreateAlignedStore(Val, Ptr, MaybeAlign(), isVolatile);
  }

  /// Create a store instruction.
  /// \param Val Stored or broadcast value.
  /// \param Ptr Pointer operand.
  /// \param Props Load/store properties (volatile, align, ordering, etc.).
  /// @return The created store instruction.
  StoreInst *CreateStore(Value *Val, Value *Ptr,
                         const LoadStoreInstProperties &Props) {
    return Insert(new StoreInst(Val, Ptr, Props));
  }

  /// Create an aligned load.
  /// \param Ty Type of the loaded, allocated, or created value.
  /// \param Ptr Pointer operand.
  /// \param Align Required alignment of the memory access.
  /// \param Name Name of the new instruction or value.
  /// @return The created aligned load instruction.
  LoadInst *CreateAlignedLoad(Type *Ty, Value *Ptr, MaybeAlign Align,
                              const char *Name) {
    return CreateAlignedLoad(Ty, Ptr, Align, /*isVolatile*/false, Name);
  }

  /// Create an aligned load.
  /// \param Ty Type of the loaded, allocated, or created value.
  /// \param Ptr Pointer operand.
  /// \param Align Required alignment of the memory access.
  /// \param Name Name of the new instruction or value.
  /// @return The created aligned load instruction.
  LoadInst *CreateAlignedLoad(Type *Ty, Value *Ptr, MaybeAlign Align,
                              const Twine &Name = "") {
    return CreateAlignedLoad(Ty, Ptr, Align, /*isVolatile*/false, Name);
  }

  /// Create an aligned load.
  /// \param Ty Type of the loaded, allocated, or created value.
  /// \param Ptr Pointer operand.
  /// \param Align Required alignment of the memory access.
  /// \param isVolatile Whether the memory intrinsic is volatile.
  /// \param Name Name of the new instruction or value.
  /// @return The created aligned load instruction.
  LoadInst *CreateAlignedLoad(Type *Ty, Value *Ptr, MaybeAlign Align,
                              bool isVolatile, const Twine &Name = "") {
    if (!Align) {
      const DataLayout &DL = BB->getDataLayout();
      Align = DL.getABITypeAlign(Ty);
    }
    return Insert(new LoadInst(Ty, Ptr, Twine(), isVolatile, *Align), Name);
  }

  /// Create an aligned store.
  /// \param Val Stored or broadcast value.
  /// \param Ptr Pointer operand.
  /// \param Align Required alignment of the memory access.
  /// \param isVolatile Whether the memory intrinsic is volatile.
  /// @return The created aligned store instruction.
  StoreInst *CreateAlignedStore(Value *Val, Value *Ptr, MaybeAlign Align,
                                bool isVolatile = false) {
    if (!Align) {
      const DataLayout &DL = BB->getDataLayout();
      Align = DL.getABITypeAlign(Val->getType());
    }
    return Insert(new StoreInst(Val, Ptr, isVolatile, *Align));
  }
  /// Create a fence instruction.
  /// \param Ordering Atomic ordering constraint.
  /// \param SSID Synchronization scope for an atomic operation.
  /// \param Name Name of the new instruction or value.
  /// @return The created fence instruction.
  FenceInst *CreateFence(AtomicOrdering Ordering,
                         SyncScope::ID SSID = SyncScope::System,
                         const Twine &Name = "") {
    return Insert(new FenceInst(Context, Ordering, SSID), Name);
  }
  /// Create an atomic cmpxchg instruction.
  /// \param Ptr Pointer operand.
  /// \param Cmp Expected value for the compare.
  /// \param New New value to store on success.
  /// \param Align Required alignment of the memory access.
  /// \param SuccessOrdering Atomic ordering on success.
  /// \param FailureOrdering Atomic ordering on failure.
  /// \param SSID Synchronization scope for an atomic operation.
  /// @return The created cmpxchg instruction.
  AtomicCmpXchgInst *
  CreateAtomicCmpXchg(Value *Ptr, Value *Cmp, Value *New, MaybeAlign Align,
                      AtomicOrdering SuccessOrdering,
                      AtomicOrdering FailureOrdering,
                      SyncScope::ID SSID = SyncScope::System) {
    if (!Align) {
      const DataLayout &DL = BB->getDataLayout();
      Align = llvm::Align(DL.getTypeStoreSize(New->getType()));
    }

    return Insert(new AtomicCmpXchgInst(Ptr, Cmp, New, *Align, SuccessOrdering,
                                        FailureOrdering, SSID));
  }

  /// Create an atomicrmw instruction.
  /// \param Op Unary operand.
  /// \param Ptr Pointer operand.
  /// \param Val Stored or broadcast value.
  /// \param Align Required alignment of the memory access.
  /// \param Ordering Atomic ordering constraint.
  /// \param SSID Synchronization scope for an atomic operation.
  /// \param Elementwise The Elementwise parameter.
  /// @return The created atomicrmw instruction.
  AtomicRMWInst *CreateAtomicRMW(AtomicRMWInst::BinOp Op, Value *Ptr,
                                 Value *Val, MaybeAlign Align,
                                 AtomicOrdering Ordering,
                                 SyncScope::ID SSID = SyncScope::System,
                                 bool Elementwise = false) {
    if (!Align) {
      const DataLayout &DL = BB->getDataLayout();
      Align = llvm::Align(DL.getTypeStoreSize(Val->getType()));
    }

    return Insert(
        new AtomicRMWInst(Op, Ptr, Val, *Align, Ordering, SSID, Elementwise));
  }

  /// Create a structured gep.
  /// \param BaseType Element type attached as an attribute.
  /// \param PtrBase The PtrBase parameter.
  /// \param Indices Indices for the operation.
  /// \param Name Name of the new instruction or value.
  /// @return The created structured gep.
  Value *CreateStructuredGEP(Type *BaseType, Value *PtrBase,
                             ArrayRef<Value *> Indices,
                             const Twine &Name = "") {
    SmallVector<Value *> Args;
    Args.push_back(PtrBase);
    llvm::append_range(Args, Indices);

    return CreateIntrinsic(
        Intrinsic::structured_gep, {PtrBase->getType()}, Args, {}, Name, {},
        [&](CallInst *Output) {
          Output->addParamAttr(
              0,
              Attribute::get(getContext(), Attribute::ElementType, BaseType));
        });
  }

  /// Create a gep instruction.
  /// \param Ty Type of the loaded, allocated, or created value.
  /// \param Ptr Pointer operand.
  /// \param IdxList Index list for extractvalue or insertvalue.
  /// \param Name Name of the new instruction or value.
  /// \param NW GEP no-wrap flags.
  /// @return The created gep instruction.
  Value *CreateGEP(Type *Ty, Value *Ptr, ArrayRef<Value *> IdxList,
                   const Twine &Name = "",
                   GEPNoWrapFlags NW = GEPNoWrapFlags::none()) {
    if (auto *V = Folder.FoldGEP(Ty, Ptr, IdxList, NW))
      return V;
    return Insert(GetElementPtrInst::Create(Ty, Ptr, IdxList, NW), Name);
  }

  /// Create an in bounds gep.
  /// \param Ty Type of the loaded, allocated, or created value.
  /// \param Ptr Pointer operand.
  /// \param IdxList Index list for extractvalue or insertvalue.
  /// \param Name Name of the new instruction or value.
  /// @return The created in bounds gep.
  Value *CreateInBoundsGEP(Type *Ty, Value *Ptr, ArrayRef<Value *> IdxList,
                           const Twine &Name = "") {
    return CreateGEP(Ty, Ptr, IdxList, Name, GEPNoWrapFlags::inBounds());
  }

  /// Create a const gep1_32.
  /// \param Ty Type of the loaded, allocated, or created value.
  /// \param Ptr Pointer operand.
  /// \param Idx0 The Idx0 parameter.
  /// \param Name Name of the new instruction or value.
  /// @return The created const gep1_32.
  Value *CreateConstGEP1_32(Type *Ty, Value *Ptr, unsigned Idx0,
                            const Twine &Name = "") {
    Value *Idx = ConstantInt::get(Type::getInt32Ty(Context), Idx0);
    return CreateGEP(Ty, Ptr, Idx, Name, GEPNoWrapFlags::none());
  }

  /// Create a const in bounds gep1_32.
  /// \param Ty Type of the loaded, allocated, or created value.
  /// \param Ptr Pointer operand.
  /// \param Idx0 The Idx0 parameter.
  /// \param Name Name of the new instruction or value.
  /// @return The created const in bounds gep1_32.
  Value *CreateConstInBoundsGEP1_32(Type *Ty, Value *Ptr, unsigned Idx0,
                                    const Twine &Name = "") {
    Value *Idx = ConstantInt::get(Type::getInt32Ty(Context), Idx0);
    return CreateGEP(Ty, Ptr, Idx, Name, GEPNoWrapFlags::inBounds());
  }

  /// Create a const gep2_32.
  /// \param Ty Type of the loaded, allocated, or created value.
  /// \param Ptr Pointer operand.
  /// \param Idx0 The Idx0 parameter.
  /// \param Idx1 The Idx1 parameter.
  /// \param Name Name of the new instruction or value.
  /// \param NWFlags GEP no-wrap flags.
  /// @return The created const gep2_32.
  Value *CreateConstGEP2_32(Type *Ty, Value *Ptr, unsigned Idx0, unsigned Idx1,
                            const Twine &Name = "",
                            GEPNoWrapFlags NWFlags = GEPNoWrapFlags::none()) {
    Value *Idxs[] = {
      ConstantInt::get(Type::getInt32Ty(Context), Idx0),
      ConstantInt::get(Type::getInt32Ty(Context), Idx1)
    };
    return CreateGEP(Ty, Ptr, Idxs, Name, NWFlags);
  }

  /// Create a const in bounds gep2_32.
  /// \param Ty Type of the loaded, allocated, or created value.
  /// \param Ptr Pointer operand.
  /// \param Idx0 The Idx0 parameter.
  /// \param Idx1 The Idx1 parameter.
  /// \param Name Name of the new instruction or value.
  /// @return The created const in bounds gep2_32.
  Value *CreateConstInBoundsGEP2_32(Type *Ty, Value *Ptr, unsigned Idx0,
                                    unsigned Idx1, const Twine &Name = "") {
    Value *Idxs[] = {
      ConstantInt::get(Type::getInt32Ty(Context), Idx0),
      ConstantInt::get(Type::getInt32Ty(Context), Idx1)
    };
    return CreateGEP(Ty, Ptr, Idxs, Name, GEPNoWrapFlags::inBounds());
  }

  /// Create a const gep1_64.
  /// \param Ty Type of the loaded, allocated, or created value.
  /// \param Ptr Pointer operand.
  /// \param Idx0 The Idx0 parameter.
  /// \param Name Name of the new instruction or value.
  /// @return The created const gep1_64.
  Value *CreateConstGEP1_64(Type *Ty, Value *Ptr, uint64_t Idx0,
                            const Twine &Name = "") {
    Value *Idx = ConstantInt::get(Type::getInt64Ty(Context), Idx0);
    return CreateGEP(Ty, Ptr, Idx, Name, GEPNoWrapFlags::none());
  }

  /// Create a const in bounds gep1_64.
  /// \param Ty Type of the loaded, allocated, or created value.
  /// \param Ptr Pointer operand.
  /// \param Idx0 The Idx0 parameter.
  /// \param Name Name of the new instruction or value.
  /// @return The created const in bounds gep1_64.
  Value *CreateConstInBoundsGEP1_64(Type *Ty, Value *Ptr, uint64_t Idx0,
                                    const Twine &Name = "") {
    Value *Idx = ConstantInt::get(Type::getInt64Ty(Context), Idx0);
    return CreateGEP(Ty, Ptr, Idx, Name, GEPNoWrapFlags::inBounds());
  }

  /// Create a const gep2_64.
  /// \param Ty Type of the loaded, allocated, or created value.
  /// \param Ptr Pointer operand.
  /// \param Idx0 The Idx0 parameter.
  /// \param Idx1 The Idx1 parameter.
  /// \param Name Name of the new instruction or value.
  /// @return The created const gep2_64.
  Value *CreateConstGEP2_64(Type *Ty, Value *Ptr, uint64_t Idx0, uint64_t Idx1,
                            const Twine &Name = "") {
    Value *Idxs[] = {
      ConstantInt::get(Type::getInt64Ty(Context), Idx0),
      ConstantInt::get(Type::getInt64Ty(Context), Idx1)
    };
    return CreateGEP(Ty, Ptr, Idxs, Name, GEPNoWrapFlags::none());
  }

  /// Create a const in bounds gep2_64.
  /// \param Ty Type of the loaded, allocated, or created value.
  /// \param Ptr Pointer operand.
  /// \param Idx0 The Idx0 parameter.
  /// \param Idx1 The Idx1 parameter.
  /// \param Name Name of the new instruction or value.
  /// @return The created const in bounds gep2_64.
  Value *CreateConstInBoundsGEP2_64(Type *Ty, Value *Ptr, uint64_t Idx0,
                                    uint64_t Idx1, const Twine &Name = "") {
    Value *Idxs[] = {
      ConstantInt::get(Type::getInt64Ty(Context), Idx0),
      ConstantInt::get(Type::getInt64Ty(Context), Idx1)
    };
    return CreateGEP(Ty, Ptr, Idxs, Name, GEPNoWrapFlags::inBounds());
  }

  /// Create a struct gep.
  /// \param Ty Type of the loaded, allocated, or created value.
  /// \param Ptr Pointer operand.
  /// \param Idx Index at which to extract or insert.
  /// \param Name Name of the new instruction or value.
  /// @return The created struct gep.
  Value *CreateStructGEP(Type *Ty, Value *Ptr, unsigned Idx,
                         const Twine &Name = "") {
    GEPNoWrapFlags NWFlags =
        GEPNoWrapFlags::inBounds() | GEPNoWrapFlags::noUnsignedWrap();
    return CreateConstGEP2_32(Ty, Ptr, 0, Idx, Name, NWFlags);
  }

  /// Create a ptr add.
  /// \param Ptr Pointer operand.
  /// \param Offset Splice offset, or alignment offset from the pointer.
  /// \param Name Name of the new instruction or value.
  /// \param NW GEP no-wrap flags.
  /// @return The created ptr add.
  Value *CreatePtrAdd(Value *Ptr, Value *Offset, const Twine &Name = "",
                      GEPNoWrapFlags NW = GEPNoWrapFlags::none()) {
    return CreateGEP(getInt8Ty(), Ptr, Offset, Name, NW);
  }

  /// Create an in bounds ptr add.
  /// \param Ptr Pointer operand.
  /// \param Offset Splice offset, or alignment offset from the pointer.
  /// \param Name Name of the new instruction or value.
  /// @return The created in bounds ptr add.
  Value *CreateInBoundsPtrAdd(Value *Ptr, Value *Offset,
                              const Twine &Name = "") {
    return CreateGEP(getInt8Ty(), Ptr, Offset, Name,
                     GEPNoWrapFlags::inBounds());
  }

  //===--------------------------------------------------------------------===//
  // Instruction creation methods: Cast/Conversion Operators
  //===--------------------------------------------------------------------===//

  /// Create a trunc instruction.
  /// \param V Input value.
  /// \param DestTy Destination type of the cast.
  /// \param Name Name of the new instruction or value.
  /// \param IsNUW Whether the pointer difference is nuw.
  /// \param IsNSW Whether to set the nsw flag.
  /// @return The created trunc instruction.
  Value *CreateTrunc(Value *V, Type *DestTy, const Twine &Name = "",
                     bool IsNUW = false, bool IsNSW = false) {
    if (V->getType() == DestTy)
      return V;
    if (Value *Folded = Folder.FoldCast(Instruction::Trunc, V, DestTy))
      return Folded;
    Instruction *I = CastInst::Create(Instruction::Trunc, V, DestTy);
    if (IsNUW)
      I->setHasNoUnsignedWrap();
    if (IsNSW)
      I->setHasNoSignedWrap();
    return Insert(I, Name);
  }

  /// Create a z ext instruction.
  /// \param V Input value.
  /// \param DestTy Destination type of the cast.
  /// \param Name Name of the new instruction or value.
  /// \param IsNonNeg Whether to set the nneg flag.
  /// @return The created z ext instruction.
  Value *CreateZExt(Value *V, Type *DestTy, const Twine &Name = "",
                    bool IsNonNeg = false) {
    if (V->getType() == DestTy)
      return V;
    if (Value *Folded = Folder.FoldCast(Instruction::ZExt, V, DestTy))
      return Folded;
    Instruction *I = Insert(new ZExtInst(V, DestTy), Name);
    if (IsNonNeg)
      I->setNonNeg();
    return I;
  }

  /// Create a s ext instruction.
  /// \param V Input value.
  /// \param DestTy Destination type of the cast.
  /// \param Name Name of the new instruction or value.
  /// @return The created s ext instruction.
  Value *CreateSExt(Value *V, Type *DestTy, const Twine &Name = "") {
    return CreateCast(Instruction::SExt, V, DestTy, Name);
  }

  /// Create a ZExt or Trunc from the integer value V to DestTy. Return
  /// the value untouched if the type of V is already DestTy.
  /// \param V Input value.
  /// \param DestTy Destination type of the cast.
  /// \param Name Name of the new instruction or value.
  /// @return The created ZExt or Trunc from the integer value V to DestTy.
  Value *CreateZExtOrTrunc(Value *V, Type *DestTy,
                           const Twine &Name = "") {
    assert(V->getType()->isIntOrIntVectorTy() &&
           DestTy->isIntOrIntVectorTy() &&
           "Can only zero extend/truncate integers!");
    Type *VTy = V->getType();
    if (VTy->getScalarSizeInBits() < DestTy->getScalarSizeInBits())
      return CreateZExt(V, DestTy, Name);
    if (VTy->getScalarSizeInBits() > DestTy->getScalarSizeInBits())
      return CreateTrunc(V, DestTy, Name);
    return V;
  }

  /// Create a SExt or Trunc from the integer value V to DestTy. Return
  /// the value untouched if the type of V is already DestTy.
  /// \param V Input value.
  /// \param DestTy Destination type of the cast.
  /// \param Name Name of the new instruction or value.
  /// @return The created SExt or Trunc from the integer value V to DestTy.
  Value *CreateSExtOrTrunc(Value *V, Type *DestTy,
                           const Twine &Name = "") {
    assert(V->getType()->isIntOrIntVectorTy() &&
           DestTy->isIntOrIntVectorTy() &&
           "Can only sign extend/truncate integers!");
    Type *VTy = V->getType();
    if (VTy->getScalarSizeInBits() < DestTy->getScalarSizeInBits())
      return CreateSExt(V, DestTy, Name);
    if (VTy->getScalarSizeInBits() > DestTy->getScalarSizeInBits())
      return CreateTrunc(V, DestTy, Name);
    return V;
  }

  /// Create a fp to ui.
  /// \param V Input value.
  /// \param DestTy Destination type of the cast.
  /// \param Name Name of the new instruction or value.
  /// @return The created fp to ui.
  Value *CreateFPToUI(Value *V, Type *DestTy, const Twine &Name = "") {
    if (IsFPConstrained)
      return CreateConstrainedFPCast(Intrinsic::experimental_constrained_fptoui,
                                     V, DestTy, nullptr, Name);
    return CreateCast(Instruction::FPToUI, V, DestTy, Name);
  }

  /// Create a fp to si.
  /// \param V Input value.
  /// \param DestTy Destination type of the cast.
  /// \param Name Name of the new instruction or value.
  /// @return The created fp to si.
  Value *CreateFPToSI(Value *V, Type *DestTy, const Twine &Name = "") {
    if (IsFPConstrained)
      return CreateConstrainedFPCast(Intrinsic::experimental_constrained_fptosi,
                                     V, DestTy, nullptr, Name);
    return CreateCast(Instruction::FPToSI, V, DestTy, Name);
  }

  /// Create an ui to fp.
  /// \param V Input value.
  /// \param DestTy Destination type of the cast.
  /// \param Name Name of the new instruction or value.
  /// \param IsNonNeg Whether to set the nneg flag.
  /// \param FPMathTag Floating-point math metadata tag.
  /// @return The created ui to fp.
  Value *CreateUIToFP(Value *V, Type *DestTy, const Twine &Name = "",
                      bool IsNonNeg = false, MDNode *FPMathTag = nullptr) {
    if (IsFPConstrained)
      return CreateConstrainedFPCast(Intrinsic::experimental_constrained_uitofp,
                                     V, DestTy, nullptr, Name);
    Value *Val = CreateCast(Instruction::UIToFP, V, DestTy, Name, FPMathTag);
    if (auto *I = dyn_cast<Instruction>(Val))
      if (IsNonNeg)
        I->setNonNeg();
    return Val;
  }

  /// Create a si to fp.
  /// \param V Input value.
  /// \param DestTy Destination type of the cast.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// @return The created si to fp.
  Value *CreateSIToFP(Value *V, Type *DestTy, const Twine &Name = "",
                      MDNode *FPMathTag = nullptr) {
    if (IsFPConstrained)
      return CreateConstrainedFPCast(Intrinsic::experimental_constrained_sitofp,
                                     V, DestTy, nullptr, Name);
    return CreateCast(Instruction::SIToFP, V, DestTy, Name, FPMathTag);
  }

  /// Create a fp trunc.
  /// \param V Input value.
  /// \param DestTy Destination type of the cast.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// @return The created fp trunc.
  Value *CreateFPTrunc(Value *V, Type *DestTy, const Twine &Name = "",
                       MDNode *FPMathTag = nullptr) {
    return CreateFPTruncFMF(V, DestTy, {}, Name, FPMathTag);
  }

  /// Create a fptrunc with fast-math flags.
  /// \param V Input value.
  /// \param DestTy Destination type of the cast.
  /// \param FMFSource Optional source of fast-math flags.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// @return The created fptrunc with fast-math flags.
  Value *CreateFPTruncFMF(Value *V, Type *DestTy, FMFSource FMFSource,
                          const Twine &Name = "", MDNode *FPMathTag = nullptr) {
    if (IsFPConstrained)
      return CreateConstrainedFPCast(
          Intrinsic::experimental_constrained_fptrunc, V, DestTy, FMFSource,
          Name, FPMathTag);
    return CreateCast(Instruction::FPTrunc, V, DestTy, Name, FPMathTag,
                      FMFSource);
  }

  /// Create a fp ext.
  /// \param V Input value.
  /// \param DestTy Destination type of the cast.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// @return The created fp ext.
  Value *CreateFPExt(Value *V, Type *DestTy, const Twine &Name = "",
                     MDNode *FPMathTag = nullptr) {
    return CreateFPExtFMF(V, DestTy, {}, Name, FPMathTag);
  }

  /// Create a fpext with fast-math flags.
  /// \param V Input value.
  /// \param DestTy Destination type of the cast.
  /// \param FMFSource Optional source of fast-math flags.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// @return The created fpext with fast-math flags.
  Value *CreateFPExtFMF(Value *V, Type *DestTy, FMFSource FMFSource,
                        const Twine &Name = "", MDNode *FPMathTag = nullptr) {
    if (IsFPConstrained)
      return CreateConstrainedFPCast(Intrinsic::experimental_constrained_fpext,
                                     V, DestTy, FMFSource, Name, FPMathTag);
    return CreateCast(Instruction::FPExt, V, DestTy, Name, FPMathTag,
                      FMFSource);
  }
  /// Create a ptr to addr.
  /// \param V Input value.
  /// \param Name Name of the new instruction or value.
  /// @return The created ptr to addr.
  Value *CreatePtrToAddr(Value *V, const Twine &Name = "") {
    return CreateCast(Instruction::PtrToAddr, V,
                      BB->getDataLayout().getAddressType(V->getType()), Name);
  }
  /// Create a ptr to int.
  /// \param V Input value.
  /// \param DestTy Destination type of the cast.
  /// \param Name Name of the new instruction or value.
  /// @return The created ptr to int.
  Value *CreatePtrToInt(Value *V, Type *DestTy,
                        const Twine &Name = "") {
    return CreateCast(Instruction::PtrToInt, V, DestTy, Name);
  }

  /// Create an int to ptr.
  /// \param V Input value.
  /// \param DestTy Destination type of the cast.
  /// \param Name Name of the new instruction or value.
  /// @return The created int to ptr.
  Value *CreateIntToPtr(Value *V, Type *DestTy,
                        const Twine &Name = "") {
    return CreateCast(Instruction::IntToPtr, V, DestTy, Name);
  }

  /// Create a bit cast instruction.
  /// \param V Input value.
  /// \param DestTy Destination type of the cast.
  /// \param Name Name of the new instruction or value.
  /// @return The created bit cast instruction.
  Value *CreateBitCast(Value *V, Type *DestTy,
                       const Twine &Name = "") {
    return CreateCast(Instruction::BitCast, V, DestTy, Name);
  }

  /// Create an addr space cast.
  /// \param V Input value.
  /// \param DestTy Destination type of the cast.
  /// \param Name Name of the new instruction or value.
  /// @return The created addr space cast.
  Value *CreateAddrSpaceCast(Value *V, Type *DestTy,
                             const Twine &Name = "") {
    return CreateCast(Instruction::AddrSpaceCast, V, DestTy, Name);
  }

  /// Create a z ext or bit cast.
  /// \param V Input value.
  /// \param DestTy Destination type of the cast.
  /// \param Name Name of the new instruction or value.
  /// @return The created z ext or bit cast.
  Value *CreateZExtOrBitCast(Value *V, Type *DestTy, const Twine &Name = "") {
    Instruction::CastOps CastOp =
        V->getType()->getScalarSizeInBits() == DestTy->getScalarSizeInBits()
            ? Instruction::BitCast
            : Instruction::ZExt;
    return CreateCast(CastOp, V, DestTy, Name);
  }

  /// Create a s ext or bit cast.
  /// \param V Input value.
  /// \param DestTy Destination type of the cast.
  /// \param Name Name of the new instruction or value.
  /// @return The created s ext or bit cast.
  Value *CreateSExtOrBitCast(Value *V, Type *DestTy, const Twine &Name = "") {
    Instruction::CastOps CastOp =
        V->getType()->getScalarSizeInBits() == DestTy->getScalarSizeInBits()
            ? Instruction::BitCast
            : Instruction::SExt;
    return CreateCast(CastOp, V, DestTy, Name);
  }

  /// Create a trunc or bit cast.
  /// \param V Input value.
  /// \param DestTy Destination type of the cast.
  /// \param Name Name of the new instruction or value.
  /// @return The created trunc or bit cast.
  Value *CreateTruncOrBitCast(Value *V, Type *DestTy, const Twine &Name = "") {
    Instruction::CastOps CastOp =
        V->getType()->getScalarSizeInBits() == DestTy->getScalarSizeInBits()
            ? Instruction::BitCast
            : Instruction::Trunc;
    return CreateCast(CastOp, V, DestTy, Name);
  }

  /// Create a cast instruction.
  /// \param Op Unary operand.
  /// \param V Input value.
  /// \param DestTy Destination type of the cast.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// \param FMFSource Optional source of fast-math flags.
  /// @return The created cast instruction.
  Value *CreateCast(Instruction::CastOps Op, Value *V, Type *DestTy,
                    const Twine &Name = "", MDNode *FPMathTag = nullptr,
                    FMFSource FMFSource = {}) {
    if (V->getType() == DestTy)
      return V;
    if (Value *Folded = Folder.FoldCast(Op, V, DestTy))
      return Folded;
    Instruction *Cast = CastInst::Create(Op, V, DestTy);
    if (isa<FPMathOperator>(Cast))
      setFPAttrs(Cast, FPMathTag, FMFSource.get(FMF));
    return Insert(Cast, Name);
  }

  /// Create a pointer cast.
  /// \param V Input value.
  /// \param DestTy Destination type of the cast.
  /// \param Name Name of the new instruction or value.
  /// @return The created pointer cast.
  Value *CreatePointerCast(Value *V, Type *DestTy,
                           const Twine &Name = "") {
    if (V->getType() == DestTy)
      return V;
    if (auto *VC = dyn_cast<Constant>(V))
      return Insert(Folder.CreatePointerCast(VC, DestTy), Name);
    return Insert(CastInst::CreatePointerCast(V, DestTy), Name);
  }

  // With opaque pointers enabled, this can be substituted with
  // CreateAddrSpaceCast.
  // TODO: Replace uses of this method and remove the method itself.
  /// Create a pointer bit cast or addr space cast.
  /// \param V Input value.
  /// \param DestTy Destination type of the cast.
  /// \param Name Name of the new instruction or value.
  /// @return The created pointer bit cast or addr space cast.
  Value *CreatePointerBitCastOrAddrSpaceCast(Value *V, Type *DestTy,
                                             const Twine &Name = "") {
    if (V->getType() == DestTy)
      return V;

    if (auto *VC = dyn_cast<Constant>(V)) {
      return Insert(Folder.CreatePointerBitCastOrAddrSpaceCast(VC, DestTy),
                    Name);
    }

    return Insert(CastInst::CreatePointerBitCastOrAddrSpaceCast(V, DestTy),
                  Name);
  }

  /// Create an int cast.
  /// \param V Input value.
  /// \param DestTy Destination type of the cast.
  /// \param isSigned The isSigned parameter.
  /// \param Name Name of the new instruction or value.
  /// @return The created int cast.
  Value *CreateIntCast(Value *V, Type *DestTy, bool isSigned,
                       const Twine &Name = "") {
    Instruction::CastOps CastOp =
        V->getType()->getScalarSizeInBits() > DestTy->getScalarSizeInBits()
            ? Instruction::Trunc
            : (isSigned ? Instruction::SExt : Instruction::ZExt);
    return CreateCast(CastOp, V, DestTy, Name);
  }

  /// Create a bit or pointer cast.
  /// \param V Input value.
  /// \param DestTy Destination type of the cast.
  /// \param Name Name of the new instruction or value.
  /// @return The created bit or pointer cast.
  Value *CreateBitOrPointerCast(Value *V, Type *DestTy,
                                const Twine &Name = "") {
    if (V->getType() == DestTy)
      return V;
    if (V->getType()->isPtrOrPtrVectorTy() && DestTy->isIntOrIntVectorTy())
      return CreatePtrToInt(V, DestTy, Name);
    if (V->getType()->isIntOrIntVectorTy() && DestTy->isPtrOrPtrVectorTy())
      return CreateIntToPtr(V, DestTy, Name);

    return CreateBitCast(V, DestTy, Name);
  }

  /// Create a fp cast.
  /// \param V Input value.
  /// \param DestTy Destination type of the cast.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// @return The created fp cast.
  Value *CreateFPCast(Value *V, Type *DestTy, const Twine &Name = "",
                      MDNode *FPMathTag = nullptr) {
    Instruction::CastOps CastOp =
        V->getType()->getScalarSizeInBits() > DestTy->getScalarSizeInBits()
            ? Instruction::FPTrunc
            : Instruction::FPExt;
    return CreateCast(CastOp, V, DestTy, Name, FPMathTag);
  }

  /// Create a constrained fp cast.
  /// \param ID Intrinsic or statepoint identifier.
  /// \param V Input value.
  /// \param DestTy Destination type of the cast.
  /// \param FMFSource Optional source of fast-math flags.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// \param Rounding Rounding mode for a constrained FP intrinsic.
  /// \param Except Exception behavior for a constrained FP intrinsic.
  /// @return The created constrained floating-point cast.
  LLVM_ABI CallInst *CreateConstrainedFPCast(
      Intrinsic::ID ID, Value *V, Type *DestTy, FMFSource FMFSource = {},
      const Twine &Name = "", MDNode *FPMathTag = nullptr,
      std::optional<RoundingMode> Rounding = std::nullopt,
      std::optional<fp::ExceptionBehavior> Except = std::nullopt);

  // Provided to resolve 'CreateIntCast(Ptr, Ptr, "...")', giving a
  // compile time error, instead of converting the string to bool for the
  // isSigned parameter.
  /// Create an int cast.
  /// \param V Input value.
  /// \param DestTy Destination type of the cast.
  /// \param Name Name of the new instruction or value.
  /// @return The created int cast.
  Value *CreateIntCast(Value *V, Type *DestTy, const char *Name) = delete;

  /// Cast between aggregates with matching structure.
  ///
  /// Leaf values are recursively extracted, cast, and reinserted into a value
  /// of type DestTy. Aggregate structure must be identical; only leaf types may
  /// differ.
  /// \param V Input value.
  /// \param DestTy Destination type of the cast.
  /// @return The created instruction or value.
  LLVM_ABI Value *CreateAggregateCast(Value *V, Type *DestTy);

  /// Create casts that preserve the bit pattern of a value.
  ///
  /// May involve multiple casts (e.g., ptr -> i64 -> <2 x i32>). The created
  /// cast instructions are inserted at the current insert point.
  /// \param DL Data layout used for sizes, alignments, and casts.
  /// \param V Input value.
  /// \param NewTy Destination type of the bit-preserving cast chain.
  /// @return The created casts that preserve the bit pattern of a value.
  LLVM_ABI Value *CreateBitPreservingCastChain(const DataLayout &DL, Value *V,
                                               Type *NewTy);

  //===--------------------------------------------------------------------===//
  // Instruction creation methods: Compare Instructions
  //===--------------------------------------------------------------------===//

  /// Create an icmp eq instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// @return The created icmp eq instruction.
  Value *CreateICmpEQ(Value *LHS, Value *RHS, const Twine &Name = "") {
    return CreateICmp(ICmpInst::ICMP_EQ, LHS, RHS, Name);
  }

  /// Create an icmp ne instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// @return The created icmp ne instruction.
  Value *CreateICmpNE(Value *LHS, Value *RHS, const Twine &Name = "") {
    return CreateICmp(ICmpInst::ICMP_NE, LHS, RHS, Name);
  }

  /// Create an icmp ugt instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// @return The created icmp ugt instruction.
  Value *CreateICmpUGT(Value *LHS, Value *RHS, const Twine &Name = "") {
    return CreateICmp(ICmpInst::ICMP_UGT, LHS, RHS, Name);
  }

  /// Create an icmp uge instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// @return The created icmp uge instruction.
  Value *CreateICmpUGE(Value *LHS, Value *RHS, const Twine &Name = "") {
    return CreateICmp(ICmpInst::ICMP_UGE, LHS, RHS, Name);
  }

  /// Create an icmp ult instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// @return The created icmp ult instruction.
  Value *CreateICmpULT(Value *LHS, Value *RHS, const Twine &Name = "") {
    return CreateICmp(ICmpInst::ICMP_ULT, LHS, RHS, Name);
  }

  /// Create an icmp ule instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// @return The created icmp ule instruction.
  Value *CreateICmpULE(Value *LHS, Value *RHS, const Twine &Name = "") {
    return CreateICmp(ICmpInst::ICMP_ULE, LHS, RHS, Name);
  }

  /// Create an icmp sgt instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// @return The created icmp sgt instruction.
  Value *CreateICmpSGT(Value *LHS, Value *RHS, const Twine &Name = "") {
    return CreateICmp(ICmpInst::ICMP_SGT, LHS, RHS, Name);
  }

  /// Create an icmp sge instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// @return The created icmp sge instruction.
  Value *CreateICmpSGE(Value *LHS, Value *RHS, const Twine &Name = "") {
    return CreateICmp(ICmpInst::ICMP_SGE, LHS, RHS, Name);
  }

  /// Create an icmp slt instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// @return The created icmp slt instruction.
  Value *CreateICmpSLT(Value *LHS, Value *RHS, const Twine &Name = "") {
    return CreateICmp(ICmpInst::ICMP_SLT, LHS, RHS, Name);
  }

  /// Create an icmp sle instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// @return The created icmp sle instruction.
  Value *CreateICmpSLE(Value *LHS, Value *RHS, const Twine &Name = "") {
    return CreateICmp(ICmpInst::ICMP_SLE, LHS, RHS, Name);
  }

  /// Create an fcmp oeq instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// @return The created fcmp oeq instruction.
  Value *CreateFCmpOEQ(Value *LHS, Value *RHS, const Twine &Name = "",
                       MDNode *FPMathTag = nullptr) {
    return CreateFCmp(FCmpInst::FCMP_OEQ, LHS, RHS, Name, FPMathTag);
  }

  /// Create an fcmp ogt instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// @return The created fcmp ogt instruction.
  Value *CreateFCmpOGT(Value *LHS, Value *RHS, const Twine &Name = "",
                       MDNode *FPMathTag = nullptr) {
    return CreateFCmp(FCmpInst::FCMP_OGT, LHS, RHS, Name, FPMathTag);
  }

  /// Create an fcmp oge instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// @return The created fcmp oge instruction.
  Value *CreateFCmpOGE(Value *LHS, Value *RHS, const Twine &Name = "",
                       MDNode *FPMathTag = nullptr) {
    return CreateFCmp(FCmpInst::FCMP_OGE, LHS, RHS, Name, FPMathTag);
  }

  /// Create an fcmp olt instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// @return The created fcmp olt instruction.
  Value *CreateFCmpOLT(Value *LHS, Value *RHS, const Twine &Name = "",
                       MDNode *FPMathTag = nullptr) {
    return CreateFCmp(FCmpInst::FCMP_OLT, LHS, RHS, Name, FPMathTag);
  }

  /// Create an fcmp ole instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// @return The created fcmp ole instruction.
  Value *CreateFCmpOLE(Value *LHS, Value *RHS, const Twine &Name = "",
                       MDNode *FPMathTag = nullptr) {
    return CreateFCmp(FCmpInst::FCMP_OLE, LHS, RHS, Name, FPMathTag);
  }

  /// Create an fcmp one instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// @return The created fcmp one instruction.
  Value *CreateFCmpONE(Value *LHS, Value *RHS, const Twine &Name = "",
                       MDNode *FPMathTag = nullptr) {
    return CreateFCmp(FCmpInst::FCMP_ONE, LHS, RHS, Name, FPMathTag);
  }

  /// Create an fcmp ord instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// @return The created fcmp ord instruction.
  Value *CreateFCmpORD(Value *LHS, Value *RHS, const Twine &Name = "",
                       MDNode *FPMathTag = nullptr) {
    return CreateFCmp(FCmpInst::FCMP_ORD, LHS, RHS, Name, FPMathTag);
  }

  /// Create an fcmp uno instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// @return The created fcmp uno instruction.
  Value *CreateFCmpUNO(Value *LHS, Value *RHS, const Twine &Name = "",
                       MDNode *FPMathTag = nullptr) {
    return CreateFCmp(FCmpInst::FCMP_UNO, LHS, RHS, Name, FPMathTag);
  }

  /// Create an fcmp ueq instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// @return The created fcmp ueq instruction.
  Value *CreateFCmpUEQ(Value *LHS, Value *RHS, const Twine &Name = "",
                       MDNode *FPMathTag = nullptr) {
    return CreateFCmp(FCmpInst::FCMP_UEQ, LHS, RHS, Name, FPMathTag);
  }

  /// Create an fcmp ugt instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// @return The created fcmp ugt instruction.
  Value *CreateFCmpUGT(Value *LHS, Value *RHS, const Twine &Name = "",
                       MDNode *FPMathTag = nullptr) {
    return CreateFCmp(FCmpInst::FCMP_UGT, LHS, RHS, Name, FPMathTag);
  }

  /// Create an fcmp uge instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// @return The created fcmp uge instruction.
  Value *CreateFCmpUGE(Value *LHS, Value *RHS, const Twine &Name = "",
                       MDNode *FPMathTag = nullptr) {
    return CreateFCmp(FCmpInst::FCMP_UGE, LHS, RHS, Name, FPMathTag);
  }

  /// Create an fcmp ult instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// @return The created fcmp ult instruction.
  Value *CreateFCmpULT(Value *LHS, Value *RHS, const Twine &Name = "",
                       MDNode *FPMathTag = nullptr) {
    return CreateFCmp(FCmpInst::FCMP_ULT, LHS, RHS, Name, FPMathTag);
  }

  /// Create an fcmp ule instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// @return The created fcmp ule instruction.
  Value *CreateFCmpULE(Value *LHS, Value *RHS, const Twine &Name = "",
                       MDNode *FPMathTag = nullptr) {
    return CreateFCmp(FCmpInst::FCMP_ULE, LHS, RHS, Name, FPMathTag);
  }

  /// Create an fcmp une instruction.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// @return The created fcmp une instruction.
  Value *CreateFCmpUNE(Value *LHS, Value *RHS, const Twine &Name = "",
                       MDNode *FPMathTag = nullptr) {
    return CreateFCmp(FCmpInst::FCMP_UNE, LHS, RHS, Name, FPMathTag);
  }

  /// Create an i cmp.
  /// \param P Comparison predicate.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// @return The created i cmp.
  Value *CreateICmp(CmpInst::Predicate P, Value *LHS, Value *RHS,
                    const Twine &Name = "") {
    if (auto *V = Folder.FoldCmp(P, LHS, RHS))
      return V;
    return Insert(new ICmpInst(P, LHS, RHS), Name);
  }

  // Create a quiet floating-point comparison (i.e. one that raises an FP
  // exception only in the case where an input is a signaling NaN).
  // Note that this differs from CreateFCmpS only if IsFPConstrained is true.
  /// Create a f cmp.
  /// \param P Comparison predicate.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// @return The created f cmp.
  Value *CreateFCmp(CmpInst::Predicate P, Value *LHS, Value *RHS,
                    const Twine &Name = "", MDNode *FPMathTag = nullptr) {
    return CreateFCmpHelper(P, LHS, RHS, Name, FPMathTag, {}, false);
  }

  // Create a quiet floating-point comparison (i.e. one that raises an FP
  // exception only in the case where an input is a signaling NaN).
  // Note that this differs from CreateFCmpS only if IsFPConstrained is true.
  /// Create an fcmp fmf instruction.
  /// \param P Comparison predicate.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param FMFSource Optional source of fast-math flags.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// @return The created fcmp fmf instruction.
  Value *CreateFCmpFMF(CmpInst::Predicate P, Value *LHS, Value *RHS,
                       FMFSource FMFSource, const Twine &Name = "",
                       MDNode *FPMathTag = nullptr) {
    return CreateFCmpHelper(P, LHS, RHS, Name, FPMathTag, FMFSource, false);
  }

  /// Create a cmp.
  /// \param Pred The Pred parameter.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// @return The created cmp.
  Value *CreateCmp(CmpInst::Predicate Pred, Value *LHS, Value *RHS,
                   const Twine &Name = "", MDNode *FPMathTag = nullptr) {
    return CmpInst::isFPPredicate(Pred)
               ? CreateFCmp(Pred, LHS, RHS, Name, FPMathTag)
               : CreateICmp(Pred, LHS, RHS, Name);
  }

  // Create a signaling floating-point comparison (i.e. one that raises an FP
  // exception whenever an input is any NaN, signaling or quiet).
  // Note that this differs from CreateFCmp only if IsFPConstrained is true.
  /// Create an fcmp s instruction.
  /// \param P Comparison predicate.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// @return The created fcmp s instruction.
  Value *CreateFCmpS(CmpInst::Predicate P, Value *LHS, Value *RHS,
                     const Twine &Name = "", MDNode *FPMathTag = nullptr) {
    return CreateFCmpHelper(P, LHS, RHS, Name, FPMathTag, {}, true);
  }

private:
  // Helper routine to create either a signaling or a quiet FP comparison.
  /// Create an fcmp helper instruction.
  /// \param P Comparison predicate.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// \param FMFSource Optional source of fast-math flags.
  /// \param IsSignaling The IsSignaling parameter.
  LLVM_ABI Value *CreateFCmpHelper(CmpInst::Predicate P, Value *LHS, Value *RHS,
                                   const Twine &Name, MDNode *FPMathTag,
                                   FMFSource FMFSource, bool IsSignaling);

public:
  /// Create a constrained fp cmp.
  /// \param ID Intrinsic or statepoint identifier.
  /// \param P Comparison predicate.
  /// \param L Debug location to apply to created instructions.
  /// \param R Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// \param Except Exception behavior for a constrained FP intrinsic.
  /// @return The created constrained floating-point compare.
  LLVM_ABI CallInst *CreateConstrainedFPCmp(
      Intrinsic::ID ID, CmpInst::Predicate P, Value *L, Value *R,
      const Twine &Name = "",
      std::optional<fp::ExceptionBehavior> Except = std::nullopt);

  //===--------------------------------------------------------------------===//
  // Instruction creation methods: Other Instructions
  //===--------------------------------------------------------------------===//

  /// Create a phi instruction.
  /// \param Ty Type of the loaded, allocated, or created value.
  /// \param NumReservedValues Number of PHI incoming values to reserve.
  /// \param Name Name of the new instruction or value.
  /// @return The created PHI node.
  PHINode *CreatePHI(Type *Ty, unsigned NumReservedValues,
                     const Twine &Name = "") {
    PHINode *Phi = PHINode::Create(Ty, NumReservedValues);
    if (isa<FPMathOperator>(Phi))
      setFPAttrs(Phi, nullptr /* MDNode* */, FMF);
    return Insert(Phi, Name);
  }

private:
  /// Create call helper.
  /// \param Callee Function or function pointer being called.
  /// \param Ops Operands of the N-ary operation.
  /// \param Name Name of the new instruction or value.
  /// \param FMFSource Optional source of fast-math flags.
  /// \param OpBundles Operand bundles attached to the call.
  CallInst *createCallHelper(Function *Callee, ArrayRef<Value *> Ops,
                             const Twine &Name = "", FMFSource FMFSource = {},
                             ArrayRef<OperandBundleDef> OpBundles = {});

public:
  /// Create a call instruction.
  /// \param FTy Function type of the callee.
  /// \param Callee Function or function pointer being called.
  /// \param Args Argument list for the call.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// @return The created call instruction.
  CallInst *CreateCall(FunctionType *FTy, Value *Callee,
                       ArrayRef<Value *> Args = {}, const Twine &Name = "",
                       MDNode *FPMathTag = nullptr) {
    CallInst *CI = CallInst::Create(FTy, Callee, Args, DefaultOperandBundles);
    if (IsFPConstrained)
      setConstrainedFPCallAttr(CI);
    if (isa<FPMathOperator>(CI))
      setFPAttrs(CI, FPMathTag, FMF);
    return Insert(CI, Name);
  }

  /// Create a call instruction.
  /// \param FTy Function type of the callee.
  /// \param Callee Function or function pointer being called.
  /// \param Args Argument list for the call.
  /// \param FMFSource Optional source of fast-math flags.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// @return The created call instruction.
  CallInst *CreateCall(FunctionType *FTy, Value *Callee, ArrayRef<Value *> Args,
                       FMFSource FMFSource, const Twine &Name = "",
                       MDNode *FPMathTag = nullptr) {
    return CreateCall(FTy, Callee, Args, DefaultOperandBundles, FMFSource, Name,
                      FPMathTag);
  }

  /// Create a call instruction.
  /// \param FTy Function type of the callee.
  /// \param Callee Function or function pointer being called.
  /// \param Args Argument list for the call.
  /// \param OpBundles Operand bundles attached to the call.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// @return The created call instruction.
  CallInst *CreateCall(FunctionType *FTy, Value *Callee, ArrayRef<Value *> Args,
                       ArrayRef<OperandBundleDef> OpBundles,
                       const Twine &Name = "", MDNode *FPMathTag = nullptr) {
    CallInst *CI = CallInst::Create(FTy, Callee, Args, OpBundles);
    if (IsFPConstrained)
      setConstrainedFPCallAttr(CI);
    if (isa<FPMathOperator>(CI))
      setFPAttrs(CI, FPMathTag, FMF);
    return Insert(CI, Name);
  }

  /// Create a call instruction.
  /// \param FTy Function type of the callee.
  /// \param Callee Function or function pointer being called.
  /// \param Args Argument list for the call.
  /// \param OpBundles Operand bundles attached to the call.
  /// \param FMFSource Optional source of fast-math flags.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// @return The created call instruction.
  CallInst *CreateCall(FunctionType *FTy, Value *Callee, ArrayRef<Value *> Args,
                       ArrayRef<OperandBundleDef> OpBundles,
                       FMFSource FMFSource, const Twine &Name = "",
                       MDNode *FPMathTag = nullptr) {
    CallInst *CI = CallInst::Create(FTy, Callee, Args, OpBundles);
    if (IsFPConstrained)
      setConstrainedFPCallAttr(CI);
    if (isa<FPMathOperator>(CI))
      setFPAttrs(CI, FPMathTag, FMFSource.get(FMF));
    return Insert(CI, Name);
  }

  /// Create a call instruction.
  /// \param Callee Function or function pointer being called.
  /// \param Args Argument list for the call.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// @return The created call instruction.
  CallInst *CreateCall(FunctionCallee Callee, ArrayRef<Value *> Args = {},
                       const Twine &Name = "", MDNode *FPMathTag = nullptr) {
    return CreateCall(Callee.getFunctionType(), Callee.getCallee(), Args, Name,
                      FPMathTag);
  }

  /// Create a call instruction.
  /// \param Callee Function or function pointer being called.
  /// \param Args Argument list for the call.
  /// \param FMFSource Optional source of fast-math flags.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// @return The created call instruction.
  CallInst *CreateCall(FunctionCallee Callee, ArrayRef<Value *> Args,
                       FMFSource FMFSource, const Twine &Name = "",
                       MDNode *FPMathTag = nullptr) {
    return CreateCall(Callee.getFunctionType(), Callee.getCallee(), Args,
                      FMFSource, Name, FPMathTag);
  }

  /// Create a call instruction.
  /// \param Callee Function or function pointer being called.
  /// \param Args Argument list for the call.
  /// \param OpBundles Operand bundles attached to the call.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// @return The created call instruction.
  CallInst *CreateCall(FunctionCallee Callee, ArrayRef<Value *> Args,
                       ArrayRef<OperandBundleDef> OpBundles,
                       const Twine &Name = "", MDNode *FPMathTag = nullptr) {
    return CreateCall(Callee.getFunctionType(), Callee.getCallee(), Args,
                      OpBundles, Name, FPMathTag);
  }

  /// Create a call instruction.
  /// \param Callee Function or function pointer being called.
  /// \param Args Argument list for the call.
  /// \param OpBundles Operand bundles attached to the call.
  /// \param FMFSource Optional source of fast-math flags.
  /// \param Name Name of the new instruction or value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// @return The created call instruction.
  CallInst *CreateCall(FunctionCallee Callee, ArrayRef<Value *> Args,
                       ArrayRef<OperandBundleDef> OpBundles,
                       FMFSource FMFSource, const Twine &Name = "",
                       MDNode *FPMathTag = nullptr) {
    return CreateCall(Callee.getFunctionType(), Callee.getCallee(), Args,
                      OpBundles, FMFSource, Name, FPMathTag);
  }

  /// Create a constrained fp call.
  /// \param Callee Function or function pointer being called.
  /// \param Args Argument list for the call.
  /// \param Name Name of the new instruction or value.
  /// \param Rounding Rounding mode for a constrained FP intrinsic.
  /// \param Except Exception behavior for a constrained FP intrinsic.
  /// @return The created constrained floating-point call.
  LLVM_ABI CallInst *CreateConstrainedFPCall(
      Function *Callee, ArrayRef<Value *> Args, const Twine &Name = "",
      std::optional<RoundingMode> Rounding = std::nullopt,
      std::optional<fp::ExceptionBehavior> Except = std::nullopt);

  /// Create a select with unknown profile.
  /// \param C LLVM context, or integer constant value.
  /// \param True True successor basic block.
  /// \param False False successor basic block.
  /// \param PassName The PassName parameter.
  /// \param Name Name of the new instruction or value.
  /// @return The created select instruction.
  LLVM_ABI Value *CreateSelectWithUnknownProfile(Value *C, Value *True,
                                                 Value *False,
                                                 StringRef PassName,
                                                 const Twine &Name = "");

  /// Create a select fmf with unknown profile.
  /// \param C LLVM context, or integer constant value.
  /// \param True True successor basic block.
  /// \param False False successor basic block.
  /// \param FMFSource Optional source of fast-math flags.
  /// \param PassName The PassName parameter.
  /// \param Name Name of the new instruction or value.
  /// @return The created select instruction.
  LLVM_ABI Value *CreateSelectFMFWithUnknownProfile(Value *C, Value *True,
                                                    Value *False,
                                                    FMFSource FMFSource,
                                                    StringRef PassName,
                                                    const Twine &Name = "");

  /// Create a select instruction.
  /// \param C LLVM context, or integer constant value.
  /// \param True True successor basic block.
  /// \param False False successor basic block.
  /// \param Name Name of the new instruction or value.
  /// \param MDFrom The MDFrom parameter.
  /// @return The created select instruction.
  LLVM_ABI Value *CreateSelect(Value *C, Value *True, Value *False,
                               const Twine &Name = "",
                               Instruction *MDFrom = nullptr);
  /// Create a select with fast-math flags.
  /// \param C LLVM context, or integer constant value.
  /// \param True True successor basic block.
  /// \param False False successor basic block.
  /// \param FMFSource Optional source of fast-math flags.
  /// \param Name Name of the new instruction or value.
  /// \param MDFrom The MDFrom parameter.
  /// @return The created select instruction.
  LLVM_ABI Value *CreateSelectFMF(Value *C, Value *True, Value *False,
                                  FMFSource FMFSource, const Twine &Name = "",
                                  Instruction *MDFrom = nullptr);

  /// Create a va arg instruction.
  /// \param List va_list pointer operand.
  /// \param Ty Type of the loaded, allocated, or created value.
  /// \param Name Name of the new instruction or value.
  /// @return The created va_arg instruction.
  VAArgInst *CreateVAArg(Value *List, Type *Ty, const Twine &Name = "") {
    return Insert(new VAArgInst(List, Ty), Name);
  }

  /// Create an extract element.
  /// \param Vec Vector operand.
  /// \param Idx Index at which to extract or insert.
  /// \param Name Name of the new instruction or value.
  /// @return The created extract element.
  Value *CreateExtractElement(Value *Vec, Value *Idx,
                              const Twine &Name = "") {
    if (Value *V = Folder.FoldExtractElement(Vec, Idx))
      return V;
    return Insert(ExtractElementInst::Create(Vec, Idx), Name);
  }

  /// Create an extract element.
  /// \param Vec Vector operand.
  /// \param Idx Index at which to extract or insert.
  /// \param Name Name of the new instruction or value.
  /// @return The created extract element.
  Value *CreateExtractElement(Value *Vec, uint64_t Idx,
                              const Twine &Name = "") {
    return CreateExtractElement(Vec, getInt64(Idx), Name);
  }

  /// Create an insert element.
  /// \param VecTy Vector type of the result.
  /// \param NewElt Element value to insert into the vector.
  /// \param Idx Index at which to extract or insert.
  /// \param Name Name of the new instruction or value.
  /// @return The created insert element.
  Value *CreateInsertElement(Type *VecTy, Value *NewElt, Value *Idx,
                             const Twine &Name = "") {
    return CreateInsertElement(PoisonValue::get(VecTy), NewElt, Idx, Name);
  }

  /// Create an insert element.
  /// \param VecTy Vector type of the result.
  /// \param NewElt Element value to insert into the vector.
  /// \param Idx Index at which to extract or insert.
  /// \param Name Name of the new instruction or value.
  /// @return The created insert element.
  Value *CreateInsertElement(Type *VecTy, Value *NewElt, uint64_t Idx,
                             const Twine &Name = "") {
    return CreateInsertElement(PoisonValue::get(VecTy), NewElt, Idx, Name);
  }

  /// Create an insert element.
  /// \param Vec Vector operand.
  /// \param NewElt Element value to insert into the vector.
  /// \param Idx Index at which to extract or insert.
  /// \param Name Name of the new instruction or value.
  /// @return The created insert element.
  Value *CreateInsertElement(Value *Vec, Value *NewElt, Value *Idx,
                             const Twine &Name = "") {
    if (Value *V = Folder.FoldInsertElement(Vec, NewElt, Idx))
      return V;
    return Insert(InsertElementInst::Create(Vec, NewElt, Idx), Name);
  }

  /// Create an insert element.
  /// \param Vec Vector operand.
  /// \param NewElt Element value to insert into the vector.
  /// \param Idx Index at which to extract or insert.
  /// \param Name Name of the new instruction or value.
  /// @return The created insert element.
  Value *CreateInsertElement(Value *Vec, Value *NewElt, uint64_t Idx,
                             const Twine &Name = "") {
    return CreateInsertElement(Vec, NewElt, getInt64(Idx), Name);
  }

  /// Create a shuffle vector.
  /// \param V1 First vector operand.
  /// \param V2 Second vector operand.
  /// \param Mask Vector mask selecting active lanes.
  /// \param Name Name of the new instruction or value.
  /// @return The created shuffle vector.
  Value *CreateShuffleVector(Value *V1, Value *V2, Value *Mask,
                             const Twine &Name = "") {
    SmallVector<int, 16> IntMask;
    ShuffleVectorInst::getShuffleMask(cast<Constant>(Mask), IntMask);
    return CreateShuffleVector(V1, V2, IntMask, Name);
  }

  /// See class ShuffleVectorInst for a description of the mask representation.
  /// \param V1 First vector operand.
  /// \param V2 Second vector operand.
  /// \param Mask Vector mask selecting active lanes.
  /// \param Name Name of the new instruction or value.
  /// @return The created instruction or value.
  Value *CreateShuffleVector(Value *V1, Value *V2, ArrayRef<int> Mask,
                             const Twine &Name = "") {
    if (Value *V = Folder.FoldShuffleVector(V1, V2, Mask))
      return V;
    return Insert(new ShuffleVectorInst(V1, V2, Mask), Name);
  }

  /// Create an unary shuffle. The second vector operand of the IR instruction
  /// is poison.
  /// \param V Input value.
  /// \param Mask Vector mask selecting active lanes.
  /// \param Name Name of the new instruction or value.
  /// @return The created unary shuffle.
  Value *CreateShuffleVector(Value *V, ArrayRef<int> Mask,
                             const Twine &Name = "") {
    return CreateShuffleVector(V, PoisonValue::get(V->getType()), Mask, Name);
  }

  /// Create a vector interleave.
  /// \param Ops Operands of the N-ary operation.
  /// \param Name Name of the new instruction or value.
  /// @return The created vector interleave.
  LLVM_ABI Value *CreateVectorInterleave(ArrayRef<Value *> Ops,
                                         const Twine &Name = "");

  /// Create an extract value.
  /// \param Agg Aggregate value.
  /// \param Idxs Index list for the GEP or aggregate operation.
  /// \param Name Name of the new instruction or value.
  /// @return The created extract value.
  Value *CreateExtractValue(Value *Agg, ArrayRef<unsigned> Idxs,
                            const Twine &Name = "") {
    if (auto *V = Folder.FoldExtractValue(Agg, Idxs))
      return V;
    return Insert(ExtractValueInst::Create(Agg, Idxs), Name);
  }

  /// Create an insert value.
  /// \param Agg Aggregate value.
  /// \param Val Stored or broadcast value.
  /// \param Idxs Index list for the GEP or aggregate operation.
  /// \param Name Name of the new instruction or value.
  /// @return The created insert value.
  Value *CreateInsertValue(Value *Agg, Value *Val, ArrayRef<unsigned> Idxs,
                           const Twine &Name = "") {
    if (auto *V = Folder.FoldInsertValue(Agg, Val, Idxs))
      return V;
    return Insert(InsertValueInst::Create(Agg, Val, Idxs), Name);
  }

  /// Create a landing pad.
  /// \param Ty Type of the loaded, allocated, or created value.
  /// \param NumClauses Number of landingpad clauses to reserve.
  /// \param Name Name of the new instruction or value.
  /// @return The created landingpad instruction.
  LandingPadInst *CreateLandingPad(Type *Ty, unsigned NumClauses,
                                   const Twine &Name = "") {
    return Insert(LandingPadInst::Create(Ty, NumClauses), Name);
  }

  /// Create a freeze instruction.
  /// \param V Input value.
  /// \param Name Name of the new instruction or value.
  /// @return The created freeze instruction.
  Value *CreateFreeze(Value *V, const Twine &Name = "") {
    return Insert(new FreezeInst(V), Name);
  }

  //===--------------------------------------------------------------------===//
  // Utility creation methods
  //===--------------------------------------------------------------------===//

  /// Return a boolean value testing if \p Arg == 0.
  /// \param Arg Value to test.
  /// \param Name Name of the new instruction or value.
  /// @return A boolean value testing if \p Arg == 0.
  Value *CreateIsNull(Value *Arg, const Twine &Name = "") {
    return CreateICmpEQ(Arg, Constant::getNullValue(Arg->getType()), Name);
  }

  /// Return a boolean value testing if \p Arg != 0.
  /// \param Arg Value to test.
  /// \param Name Name of the new instruction or value.
  /// @return A boolean value testing if \p Arg != 0.
  Value *CreateIsNotNull(Value *Arg, const Twine &Name = "") {
    return CreateICmpNE(Arg, Constant::getNullValue(Arg->getType()), Name);
  }

  /// Return a boolean value testing if \p Arg < 0.
  /// \param Arg Value to test.
  /// \param Name Name of the new instruction or value.
  /// @return A boolean value testing if \p Arg < 0.
  Value *CreateIsNeg(Value *Arg, const Twine &Name = "") {
    return CreateICmpSLT(Arg, ConstantInt::getNullValue(Arg->getType()), Name);
  }

  /// Return a boolean value testing if \p Arg > -1.
  /// \param Arg Value to test.
  /// \param Name Name of the new instruction or value.
  /// @return A boolean value testing if \p Arg > -1.
  Value *CreateIsNotNeg(Value *Arg, const Twine &Name = "") {
    return CreateICmpSGT(Arg, ConstantInt::getAllOnesValue(Arg->getType()),
                         Name);
  }

  /// Return the difference between two pointer values. The returned value
  /// type is the address type of the pointers.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// \param IsNUW Whether the pointer difference is nuw.
  /// @return The difference between two pointer values.
  LLVM_ABI Value *CreatePtrDiff(Value *LHS, Value *RHS, const Twine &Name = "",
                                bool IsNUW = false);

  /// Return the difference between two pointer values, dividing out the size
  /// of the pointed-to objects. The returned value type is the address type
  /// of the pointers.
  ///
  /// This is intended to implement C-style pointer subtraction. As such, the
  /// pointers must be appropriately aligned for their element types and
  /// pointing into the same object.
  /// \param ElemTy Pointed-to element type.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Name Name of the new instruction or value.
  /// @return The difference between two pointer values, dividing out the size
  /// of the pointed-to objects.
  LLVM_ABI Value *CreatePtrDiff(Type *ElemTy, Value *LHS, Value *RHS,
                                const Twine &Name = "");

  /// Create a launder.invariant.group intrinsic call.
  ///
  /// If Ptr type is different from pointer to i8, it is cast to pointer to i8
  /// in the same address space before the call and cast back to Ptr type after
  /// the call.
  /// \param Ptr Pointer operand.
  /// @return The created launder.invariant.group intrinsic call.
  LLVM_ABI Value *CreateLaunderInvariantGroup(Value *Ptr);

  /// Create a strip.invariant.group intrinsic call.
  ///
  /// If Ptr type is different from pointer to i8, it is cast to pointer to i8
  /// in the same address space before the call and cast back to Ptr type after
  /// the call.
  /// \param Ptr Pointer operand.
  /// @return The created strip.invariant.group intrinsic call.
  LLVM_ABI Value *CreateStripInvariantGroup(Value *Ptr);

  /// Return a vector value that contains the vector V reversed
  /// \param V Input value.
  /// \param Name Name of the new instruction or value.
  /// @return A vector value that contains the vector V reversed.
  LLVM_ABI Value *CreateVectorReverse(Value *V, const Twine &Name = "");

  /// Create a vector.splice.left operation.
  ///
  /// Creates a vector.splice.left intrinsic call, or a shufflevector that
  /// produces the same result if the result type is a fixed-length vector and
  /// Offset is a constant.
  /// \param V1 First vector operand.
  /// \param V2 Second vector operand.
  /// \param Offset Splice offset, or alignment offset from the pointer.
  /// \param Name Name of the new instruction or value.
  /// @return The created vector.splice.left operation.
  LLVM_ABI Value *CreateVectorSpliceLeft(Value *V1, Value *V2, Value *Offset,
                                         const Twine &Name = "");

  /// Create a vector.splice.left operation.
  ///
  /// Creates a vector.splice.left intrinsic call, or a shufflevector that
  /// produces the same result if the result type is a fixed-length vector and
  /// Offset is a constant.
  /// \param V1 First vector operand.
  /// \param V2 Second vector operand.
  /// \param Offset Splice offset, or alignment offset from the pointer.
  /// \param Name Name of the new instruction or value.
  /// @return The created vector.splice.left operation.
  Value *CreateVectorSpliceLeft(Value *V1, Value *V2, uint32_t Offset,
                                const Twine &Name = "") {
    return CreateVectorSpliceLeft(V1, V2, getInt32(Offset), Name);
  }

  /// Create a vector.splice.right operation.
  ///
  /// Creates a vector.splice.right intrinsic call, or a shufflevector that
  /// produces the same result if the result type is a fixed-length vector and
  /// Offset is a constant.
  /// \param V1 First vector operand.
  /// \param V2 Second vector operand.
  /// \param Offset Splice offset, or alignment offset from the pointer.
  /// \param Name Name of the new instruction or value.
  /// @return The created vector.splice.right operation.
  LLVM_ABI Value *CreateVectorSpliceRight(Value *V1, Value *V2, Value *Offset,
                                          const Twine &Name = "");

  /// Create a vector.splice.right operation.
  ///
  /// Creates a vector.splice.right intrinsic call, or a shufflevector that
  /// produces the same result if the result type is a fixed-length vector and
  /// Offset is a constant.
  /// \param V1 First vector operand.
  /// \param V2 Second vector operand.
  /// \param Offset Splice offset, or alignment offset from the pointer.
  /// \param Name Name of the new instruction or value.
  /// @return The created vector.splice.right operation.
  Value *CreateVectorSpliceRight(Value *V1, Value *V2, uint32_t Offset,
                                 const Twine &Name = "") {
    return CreateVectorSpliceRight(V1, V2, getInt32(Offset), Name);
  }

  /// Return a vector value that contains \arg V broadcasted to \p
  /// NumElts elements.
  /// \param NumElts Number of vector elements.
  /// \param V Input value.
  /// \param Name Name of the new instruction or value.
  /// @return A vector value that contains \arg V broadcasted to \p NumElts elements.
  LLVM_ABI Value *CreateVectorSplat(unsigned NumElts, Value *V,
                                    const Twine &Name = "");

  /// Return a vector value that contains \arg V broadcasted to \p
  /// EC elements.
  /// \param EC Element count of the splat result.
  /// \param V Input value.
  /// \param Name Name of the new instruction or value.
  /// @return A vector value that contains \arg V broadcasted to \p EC elements.
  LLVM_ABI Value *CreateVectorSplat(ElementCount EC, Value *V,
                                    const Twine &Name = "");

  /// Create a preserve array access index.
  /// \param ElTy Element type for the preserved access.
  /// \param Base Base pointer or value being accessed.
  /// \param Dimension Array dimension being preserved.
  /// \param LastIndex Last array index being preserved.
  /// \param DbgInfo Debug info metadata for the preserved access.
  /// @return The created preserve array access index.
  LLVM_ABI Value *CreatePreserveArrayAccessIndex(Type *ElTy, Value *Base,
                                                 unsigned Dimension,
                                                 unsigned LastIndex,
                                                 MDNode *DbgInfo);

  /// Create a preserve union access index.
  /// \param Base Base pointer or value being accessed.
  /// \param FieldIndex Struct or union field index being preserved.
  /// \param DbgInfo Debug info metadata for the preserved access.
  /// @return The created preserve union access index.
  LLVM_ABI Value *CreatePreserveUnionAccessIndex(Value *Base,
                                                 unsigned FieldIndex,
                                                 MDNode *DbgInfo);

  /// Create a preserve struct access index.
  /// \param ElTy Element type for the preserved access.
  /// \param Base Base pointer or value being accessed.
  /// \param Index Struct member index being preserved.
  /// \param FieldIndex Struct or union field index being preserved.
  /// \param DbgInfo Debug info metadata for the preserved access.
  /// @return The created preserve struct access index.
  LLVM_ABI Value *CreatePreserveStructAccessIndex(Type *ElTy, Value *Base,
                                                  unsigned Index,
                                                  unsigned FieldIndex,
                                                  MDNode *DbgInfo);

  /// Create an is.fpclass intrinsic call.
  /// \param FPNum Floating-point value to classify.
  /// \param Test Floating-point class test bitmask.
  /// @return The created is.fpclass intrinsic call.
  LLVM_ABI Value *createIsFPClass(Value *FPNum, unsigned Test);

private:
  /// Create an alignment-assumption intrinsic helper call.
  ///
  /// Represents an alignment assumption on \p PtrValue with offset
  /// \p OffsetValue and alignment value \p AlignValue.
  /// \param DL Data layout used for sizes, alignments, and casts.
  /// \param PtrValue Pointer the assumption applies to.
  /// \param AlignValue Alignment value for the assumption.
  /// \param OffsetValue Optional byte offset subtracted from the pointer.
  CallInst *CreateAlignmentAssumptionHelper(const DataLayout &DL,
                                            Value *PtrValue, Value *AlignValue,
                                            Value *OffsetValue);

public:
  /// Create an assume intrinsic call that represents an alignment
  /// assumption on the provided pointer.
  ///
  /// An optional offset can be provided, and if it is provided, the offset
  /// must be subtracted from the provided pointer to get the pointer with the
  /// specified alignment.
  /// \param DL Data layout used for sizes, alignments, and casts.
  /// \param PtrValue Pointer the assumption applies to.
  /// \param Alignment Required alignment of the memory access.
  /// \param OffsetValue Optional byte offset subtracted from the pointer.
  /// @return The created alignment assumption call.
  LLVM_ABI CallInst *CreateAlignmentAssumption(const DataLayout &DL,
                                               Value *PtrValue,
                                               uint64_t Alignment,
                                               Value *OffsetValue = nullptr);

  /// Create an assume intrinsic call that represents an alignment
  /// assumption on the provided pointer.
  ///
  /// An optional offset can be provided, and if it is provided, the offset
  /// must be subtracted from the provided pointer to get the pointer with the
  /// specified alignment.
  ///
  /// This overload handles the condition where the Alignment is dependent
  /// on an existing value rather than a static value.
  /// \param DL Data layout used for sizes, alignments, and casts.
  /// \param PtrValue Pointer the assumption applies to.
  /// \param Alignment Required alignment of the memory access.
  /// \param OffsetValue Optional byte offset subtracted from the pointer.
  /// @return The created alignment assumption call.
  LLVM_ABI CallInst *CreateAlignmentAssumption(const DataLayout &DL,
                                               Value *PtrValue,
                                               Value *Alignment,
                                               Value *OffsetValue = nullptr);

  /// Create an assume intrinsic call that represents a dereferencable
  /// assumption on the provided pointer.
  /// \param PtrValue Pointer the assumption applies to.
  /// \param SizeValue Number of dereferenceable bytes.
  /// @return The created dereferenceable assumption call.
  LLVM_ABI CallInst *CreateDereferenceableAssumption(Value *PtrValue,
                                                     Value *SizeValue);

  /// Create an assume intrinsic call that represents a nonnull assumption on
  /// the provided pointer.
  /// \param PtrValue Pointer the assumption applies to.
  /// @return The created nonnull assumption call.
  LLVM_ABI CallInst *CreateNonnullAssumption(Value *PtrValue);
};

/// Uniform API for creating and inserting LLVM IR instructions.
///
/// Instructions are inserted either at the end of a BasicBlock or at a specific
/// iterator location. The builder does not expose the full generality of LLVM
/// instructions; use mutators (e.g. setVolatile) on created instructions for
/// extra properties. Convenience state exists for fast-math flags and fp-math
/// tags.
///
/// The first template argument specifies a class to use for creating constants
/// (defaults to minimally folded constants). The second allows clients to
/// specify custom insertion hooks called on every newly created insertion.
template <typename FolderTy = ConstantFolder,
          typename InserterTy = IRBuilderDefaultInserter>
class IRBuilder : public IRBuilderBase {
private:
  FolderTy Folder;
  InserterTy Inserter;
  /// Construct an IRBuilder.
  /// \param C LLVM context, or integer constant value.
  /// \param Folder Constant folder used by this builder.
  /// \param Inserter Custom instruction inserter.
  /// \param FPMathTag Floating-point math metadata tag.
  /// \param OpBundles Operand bundles attached to the call.
public:
  IRBuilder(LLVMContext &C, FolderTy Folder, InserterTy Inserter,
            MDNode *FPMathTag = nullptr,
            ArrayRef<OperandBundleDef> OpBundles = {})
      : IRBuilderBase(C, this->Folder, this->Inserter, FPMathTag, OpBundles),
        Folder(Folder), Inserter(Inserter) {}

  /// Construct an IRBuilder.
  /// \param C LLVM context, or integer constant value.
  /// \param Folder Constant folder used by this builder.
  /// \param FPMathTag Floating-point math metadata tag.
  /// \param OpBundles Operand bundles attached to the call.
  IRBuilder(LLVMContext &C, FolderTy Folder, MDNode *FPMathTag = nullptr,
            ArrayRef<OperandBundleDef> OpBundles = {})
      : IRBuilderBase(C, this->Folder, this->Inserter, FPMathTag, OpBundles),
        Folder(Folder) {}

  /// Construct an IRBuilder.
  /// \param C LLVM context, or integer constant value.
  /// \param FPMathTag Floating-point math metadata tag.
  /// \param OpBundles Operand bundles attached to the call.
  explicit IRBuilder(LLVMContext &C, MDNode *FPMathTag = nullptr,
                     ArrayRef<OperandBundleDef> OpBundles = {})
      : IRBuilderBase(C, this->Folder, this->Inserter, FPMathTag, OpBundles) {}

  /// Construct an IRBuilder.
  /// \param TheBB Basic block used as the insert point.
  /// \param Folder Constant folder used by this builder.
  /// \param FPMathTag Floating-point math metadata tag.
  /// \param OpBundles Operand bundles attached to the call.
  explicit IRBuilder(BasicBlock *TheBB, FolderTy Folder,
                     MDNode *FPMathTag = nullptr,
                     ArrayRef<OperandBundleDef> OpBundles = {})
      : IRBuilderBase(TheBB->getContext(), this->Folder, this->Inserter,
                      FPMathTag, OpBundles),
        Folder(Folder) {
    SetInsertPoint(TheBB);
  }

  /// Construct an IRBuilder.
  /// \param TheBB Basic block used as the insert point.
  /// \param FPMathTag Floating-point math metadata tag.
  /// \param OpBundles Operand bundles attached to the call.
  explicit IRBuilder(BasicBlock *TheBB, MDNode *FPMathTag = nullptr,
                     ArrayRef<OperandBundleDef> OpBundles = {})
      : IRBuilderBase(TheBB->getContext(), this->Folder, this->Inserter,
                      FPMathTag, OpBundles) {
    SetInsertPoint(TheBB);
  }

  /// Construct an IRBuilder.
  /// \param IP Instruction or saved insert point.
  /// \param FPMathTag Floating-point math metadata tag.
  /// \param OpBundles Operand bundles attached to the call.
  explicit IRBuilder(Instruction *IP, MDNode *FPMathTag = nullptr,
                     ArrayRef<OperandBundleDef> OpBundles = {})
      : IRBuilderBase(IP->getContext(), this->Folder, this->Inserter, FPMathTag,
                      OpBundles) {
    SetInsertPoint(IP);
  }

  /// Construct an IRBuilder.
  /// \param TheBB Basic block used as the insert point.
  /// \param IP Instruction or saved insert point.
  /// \param Folder Constant folder used by this builder.
  /// \param FPMathTag Floating-point math metadata tag.
  /// \param OpBundles Operand bundles attached to the call.
  IRBuilder(BasicBlock *TheBB, BasicBlock::iterator IP, FolderTy Folder,
            MDNode *FPMathTag = nullptr,
            ArrayRef<OperandBundleDef> OpBundles = {})
      : IRBuilderBase(TheBB->getContext(), this->Folder, this->Inserter,
                      FPMathTag, OpBundles),
        Folder(Folder) {
    SetInsertPoint(TheBB, IP);
  }

  /// Construct an IRBuilder.
  /// \param TheBB Basic block used as the insert point.
  /// \param IP Instruction or saved insert point.
  /// \param FPMathTag Floating-point math metadata tag.
  /// \param OpBundles Operand bundles attached to the call.
  IRBuilder(BasicBlock *TheBB, BasicBlock::iterator IP,
            MDNode *FPMathTag = nullptr,
            ArrayRef<OperandBundleDef> OpBundles = {})
      : IRBuilderBase(TheBB->getContext(), this->Folder, this->Inserter,
                      FPMathTag, OpBundles) {
    SetInsertPoint(TheBB, IP);
  }

  /// Avoid copying the full IRBuilder. Prefer using InsertPointGuard
  /// or FastMathFlagGuard instead.
  /// \param Unused Unused copy source (deleted).
  IRBuilder(const IRBuilder &Unused) = delete;

  /// Return the instruction inserter used by this builder.
  /// @return The instruction inserter used by this builder.
  InserterTy &getInserter() { return Inserter; }
  /// Return the instruction inserter used by this builder.
  /// @return The instruction inserter used by this builder.
  const InserterTy &getInserter() const { return Inserter; }
};

/// Deduction guide for \c IRBuilder.
template <typename FolderTy, typename InserterTy>
IRBuilder(LLVMContext &, FolderTy, InserterTy, MDNode *,
          ArrayRef<OperandBundleDef>) -> IRBuilder<FolderTy, InserterTy>;
/// Deduction guide for \c IRBuilder.
IRBuilder(LLVMContext &, MDNode *, ArrayRef<OperandBundleDef>) -> IRBuilder<>;
/// Deduction guide for \c IRBuilder.
template <typename FolderTy>
IRBuilder(BasicBlock *, FolderTy, MDNode *, ArrayRef<OperandBundleDef>)
    -> IRBuilder<FolderTy>;
/// Deduction guide for \c IRBuilder.
IRBuilder(BasicBlock *, MDNode *, ArrayRef<OperandBundleDef>) -> IRBuilder<>;
/// Deduction guide for \c IRBuilder.
IRBuilder(Instruction *, MDNode *, ArrayRef<OperandBundleDef>) -> IRBuilder<>;
/// Deduction guide for \c IRBuilder.
template <typename FolderTy>
IRBuilder(BasicBlock *, BasicBlock::iterator, FolderTy, MDNode *,
          ArrayRef<OperandBundleDef>) -> IRBuilder<FolderTy>;
/// Deduction guide for \c IRBuilder.
IRBuilder(BasicBlock *, BasicBlock::iterator, MDNode *,
          ArrayRef<OperandBundleDef>) -> IRBuilder<>;


// Create wrappers for C Binding types (see CBindingWrapping.h).
/// Convert an opaque \c LLVMBuilderRef to an \c IRBuilder<> pointer.
/// \param P Opaque C API IR builder reference to unwrap.
/// @return The IRBuilder corresponding to \p P.
inline IRBuilder<> *unwrap(LLVMBuilderRef P) {
  return reinterpret_cast<IRBuilder<> *>(P);
}

/// Convert an \c IRBuilder<> pointer to an opaque \c LLVMBuilderRef.
/// \param P IR builder to wrap for the C API.
/// @return An opaque C API builder reference for \p P.
inline LLVMBuilderRef wrap(const IRBuilder<> *P) {
  return reinterpret_cast<LLVMBuilderRef>(
      const_cast<IRBuilder<> *>(P));
}

} // end namespace llvm

#endif // LLVM_IR_IRBUILDER_H
