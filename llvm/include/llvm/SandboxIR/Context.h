//===- Context.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SANDBOXIR_CONTEXT_H
#define LLVM_SANDBOXIR_CONTEXT_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/SandboxIR/Tracker.h"
#include "llvm/SandboxIR/Type.h"
#include "llvm/Support/Compiler.h"

#include <cstdint>

namespace llvm {
namespace sandboxir {

class Argument;
class BBIterator;
class Constant;
class Module;
class Region;
class Value;
class Use;

/// Owns SandboxIR objects, maps them to LLVM IR, and tracks IR changes.
class LLVM_ABI Context {
public:
  /// Callback that receives the instruction about to be erased.
  using EraseInstrCallback = std::function<void(Instruction *)>;
  /// Callback that receives the instruction that was just created.
  using CreateInstrCallback = std::function<void(Instruction *)>;
  /// Callback that receives an instruction about to be moved.
  ///
  /// Also receives the destination BB and an iterator pointing to the
  /// insertion position.
  using MoveInstrCallback =
      std::function<void(Instruction *, const BBIterator &)>;
  /// Callback that receives a Use about to get a new source value.
  using SetUseCallback = std::function<void(const Use &, Value *)>;

  /// An ID for a registered callback.
  ///
  /// Used for deregistration. A dedicated type is employed so as to keep IDs
  /// opaque to the end user; only Context should deal with its underlying
  /// representation.
  class CallbackID {
  public:
    /// Underlying integer type of a callback identifier.
    ///
    /// Uses a 64-bit integer so we don't have to worry about the unlikely case
    /// of overflowing a 32-bit counter.
    using ValTy = uint64_t;
    /// Sentinel value for an invalid callback ID.
    static constexpr ValTy InvalidVal = 0;

  private:
    // Default initialization results in an invalid ID.
    ValTy Val = InvalidVal;
    explicit CallbackID(ValTy Val) : Val{Val} {
      assert(Val != InvalidVal && "newly-created ID is invalid!");
    }

  public:
    /// Construct an invalid callback ID.
    CallbackID() = default;
    friend class Context;
    friend struct DenseMapInfo<CallbackID>;
  };

protected:
  /// Underlying LLVM context used by this SandboxIR context.
  LLVMContext &LLVMCtx;
  friend class Type;              // For LLVMCtx.
  friend class PointerType;       // For LLVMCtx.
  friend class IntegerType;       // For LLVMCtx.
  friend class ByteType;          // For LLVMCtx.
  friend class StructType;        // For LLVMCtx.
  friend class Region;            // For LLVMCtx.
  friend class IRSnapshotChecker; // To snapshot LLVMModuleToModuleMap.

  /// Tracker that records IR changes so they can be reverted or accepted.
  Tracker IRTracker;

  /// Maps LLVM Value to the corresponding sandboxir::Value. Owns all
  /// SandboxIR objects.
  DenseMap<llvm::Value *, std::unique_ptr<Value>> LLVMValueToValueMap;

  /// Maps an LLVM Module to the corresponding sandboxir::Module.
  DenseMap<llvm::Module *, std::unique_ptr<Module>> LLVMModuleToModuleMap;

  /// Custom deleter that can destroy sandboxir::Type objects.
  ///
  /// Type has a protected destructor to prohibit the user from managing the
  /// lifetime of the Type objects. Context is friend of Type, and this custom
  /// deleter can destroy Type.
  struct TypeDeleter {
    /// Delete the Type object \p Ty.
    ///
    /// \param Ty Type object to destroy.
    void operator()(Type *Ty) { delete Ty; }
  };
  /// Maps LLVM Type to the corresonding sandboxir::Type. Owns all Sandbox IR
  /// Type objects.
  DenseMap<llvm::Type *, std::unique_ptr<Type, TypeDeleter>> LLVMTypeToTypeMap;

  /// Callbacks called when an IR instruction is about to get erased. Keys are
  /// used as IDs for deregistration.
  MapVector<CallbackID, EraseInstrCallback> EraseInstrCallbacks;
  /// Callbacks called when an IR instruction is about to get created. Keys are
  /// used as IDs for deregistration.
  MapVector<CallbackID, CreateInstrCallback> CreateInstrCallbacks;
  /// Callbacks called when an IR instruction is about to get moved. Keys are
  /// used as IDs for deregistration.
  MapVector<CallbackID, MoveInstrCallback> MoveInstrCallbacks;
  /// Callbacks called when a Use gets its source set. Keys are used as IDs for
  /// deregistration.
  MapVector<CallbackID, SetUseCallback> SetUseCallbacks;

  /// A counter used for assigning callback IDs during registration.
  ///
  /// The same counter is used for all kinds of callbacks so we can detect
  /// mismatched registration/deregistration.
  CallbackID::ValTy NextCallbackID = 1;

  /// Remove \p V from the maps and returns the unique_ptr.
  ///
  /// \param V LLVM IR value to detach.
  /// \Returns Ownership of the SandboxIR value formerly mapped to \p V.
  std::unique_ptr<Value> detachLLVMValue(llvm::Value *V);
  /// Remove \p V from all SandboxIR maps and stop owning it. This effectively
  /// detaches \p V from the underlying IR.
  ///
  /// \param V SandboxIR value to detach.
  /// \Returns Ownership of the detached SandboxIR value.
  std::unique_ptr<Value> detach(Value *V);
  friend class Instruction; // For detach().
  /// Take ownership of VPtr and store it in `LLVMValueToValueMap`.
  ///
  /// \param VPtr SandboxIR value to own.
  /// \Returns A non-owning pointer to the registered Value.
  Value *registerValue(std::unique_ptr<Value> &&VPtr);
  friend class EraseFromParent; // For registerValue().
  /// This is the actual function that creates sandboxir values for \p V,
  /// and among others handles all instruction types.
  ///
  /// \param V LLVM IR value to wrap.
  /// \param U Optional LLVM user of \p V, used when wrapping some values.
  /// \Returns The existing or newly created sandboxir::Value for \p V.
  Value *getOrCreateValueInternal(llvm::Value *V, llvm::User *U = nullptr);
  /// Get or create a sandboxir::Argument for an existing LLVM IR \p LLVMArg.
  ///
  /// \param LLVMArg Existing LLVM IR argument to wrap.
  /// \Returns The existing or newly created sandboxir::Argument.
  Argument *getOrCreateArgument(llvm::Argument *LLVMArg);
  /// Get or create a sandboxir::Value for an existing LLVM IR \p LLVMV.
  ///
  /// \param LLVMV Existing LLVM IR value to wrap.
  /// \Returns The existing or newly created sandboxir::Value.
  Value *getOrCreateValue(llvm::Value *LLVMV) {
    return getOrCreateValueInternal(LLVMV, 0);
  }
  /// Get or create a sandboxir::Constant from an existing LLVM IR \p LLVMC.
  ///
  /// \param LLVMC Existing LLVM IR constant to wrap.
  /// \Returns The existing or newly created sandboxir::Constant.
  Constant *getOrCreateConstant(llvm::Constant *LLVMC);
  friend class ConstantDataSequential; // For getOrCreateConstant().
  friend class Utils; // For getMemoryBase

  /// Invoke all registered erase-instruction callbacks with \p I.
  ///
  /// \param I Instruction about to be erased.
  void runEraseInstrCallbacks(Instruction *I);
  /// Invoke all registered create-instruction callbacks with \p I.
  ///
  /// \param I Newly created instruction.
  void runCreateInstrCallbacks(Instruction *I);
  /// Invoke all registered move-instruction callbacks.
  ///
  /// \param I Instruction about to be moved.
  /// \param Where Insertion position in the destination block.
  void runMoveInstrCallbacks(Instruction *I, const BBIterator &Where);
  /// Invoke all registered set-use callbacks.
  ///
  /// \param U Use whose source is about to change.
  /// \param NewSrc New source value.
  void runSetUseCallbacks(const Use &U, Value *NewSrc);

  friend class User;  // For runSetUseCallbacks().
  friend class Value; // For runSetUseCallbacks().

  // Friends for getOrCreateConstant().
#define DEF_CONST(ID, CLASS) friend class CLASS;
#include "llvm/SandboxIR/Values.def"

  /// Create a sandboxir::BasicBlock for an existing LLVM IR \p BB. This will
  /// also create all contents of the block.
  ///
  /// \param BB Existing LLVM IR basic block to wrap.
  /// \Returns The newly created sandboxir::BasicBlock.
  BasicBlock *createBasicBlock(llvm::BasicBlock *BB);
  friend class BasicBlock; // For getOrCreateValue().

  /// IRBuilder used when creating LLVM IR from SandboxIR.
  IRBuilder<ConstantFolder> LLVMIRBuilder;
  /// Return the IRBuilder used to create LLVM IR.
  ///
  /// \Returns The IRBuilder used when creating LLVM IR from SandboxIR.
  auto &getLLVMIRBuilder() { return LLVMIRBuilder; }

  /// Create a sandboxir::VAArgInst for existing LLVM IR \p SI.
  ///
  /// \param SI Existing llvm::VAArgInst to wrap.
  /// \Returns The newly created sandboxir::VAArgInst.
  VAArgInst *createVAArgInst(llvm::VAArgInst *SI);
  friend VAArgInst; // For createVAArgInst()
  /// Create a sandboxir::FreezeInst for existing LLVM IR \p SI.
  ///
  /// \param SI Existing llvm::FreezeInst to wrap.
  /// \Returns The newly created sandboxir::FreezeInst.
  FreezeInst *createFreezeInst(llvm::FreezeInst *SI);
  friend FreezeInst; // For createFreezeInst()
  /// Create a sandboxir::FenceInst for existing LLVM IR \p SI.
  ///
  /// \param SI Existing llvm::FenceInst to wrap.
  /// \Returns The newly created sandboxir::FenceInst.
  FenceInst *createFenceInst(llvm::FenceInst *SI);
  friend FenceInst; // For createFenceInst()
  /// Create a sandboxir::SelectInst for existing LLVM IR \p SI.
  ///
  /// \param SI Existing llvm::SelectInst to wrap.
  /// \Returns The newly created sandboxir::SelectInst.
  SelectInst *createSelectInst(llvm::SelectInst *SI);
  friend SelectInst; // For createSelectInst()
  /// Create a sandboxir::InsertElementInst for existing LLVM IR \p IEI.
  ///
  /// \param IEI Existing llvm::InsertElementInst to wrap.
  /// \Returns The newly created sandboxir::InsertElementInst.
  InsertElementInst *createInsertElementInst(llvm::InsertElementInst *IEI);
  friend InsertElementInst; // For createInsertElementInst()
  /// Create a sandboxir::ExtractElementInst for existing LLVM IR \p EEI.
  ///
  /// \param EEI Existing llvm::ExtractElementInst to wrap.
  /// \Returns The newly created sandboxir::ExtractElementInst.
  ExtractElementInst *createExtractElementInst(llvm::ExtractElementInst *EEI);
  friend ExtractElementInst; // For createExtractElementInst()
  /// Create a sandboxir::ShuffleVectorInst for existing LLVM IR \p SVI.
  ///
  /// \param SVI Existing llvm::ShuffleVectorInst to wrap.
  /// \Returns The newly created sandboxir::ShuffleVectorInst.
  ShuffleVectorInst *createShuffleVectorInst(llvm::ShuffleVectorInst *SVI);
  friend ShuffleVectorInst; // For createShuffleVectorInst()
  /// Create a sandboxir::ExtractValueInst for existing LLVM IR \p IVI.
  ///
  /// \param IVI Existing llvm::ExtractValueInst to wrap.
  /// \Returns The newly created sandboxir::ExtractValueInst.
  ExtractValueInst *createExtractValueInst(llvm::ExtractValueInst *IVI);
  friend ExtractValueInst; // For createExtractValueInst()
  /// Create a sandboxir::InsertValueInst for existing LLVM IR \p IVI.
  ///
  /// \param IVI Existing llvm::InsertValueInst to wrap.
  /// \Returns The newly created sandboxir::InsertValueInst.
  InsertValueInst *createInsertValueInst(llvm::InsertValueInst *IVI);
  friend InsertValueInst; // For createInsertValueInst()
  /// Create a sandboxir::UncondBrInst for existing LLVM IR \p UBI.
  ///
  /// \param UBI Existing llvm::UncondBrInst to wrap.
  /// \Returns The newly created sandboxir::UncondBrInst.
  UncondBrInst *createUncondBrInst(llvm::UncondBrInst *UBI);
  friend UncondBrInst; // For createUncondBrInst()
  /// Create a sandboxir::CondBrInst for existing LLVM IR \p CBI.
  ///
  /// \param CBI Existing llvm::CondBrInst to wrap.
  /// \Returns The newly created sandboxir::CondBrInst.
  CondBrInst *createCondBrInst(llvm::CondBrInst *CBI);
  friend CondBrInst; // For createCondBrInst()
  /// Create a sandboxir::LoadInst for existing LLVM IR \p LI.
  ///
  /// \param LI Existing llvm::LoadInst to wrap.
  /// \Returns The newly created sandboxir::LoadInst.
  LoadInst *createLoadInst(llvm::LoadInst *LI);
  friend LoadInst; // For createLoadInst()
  /// Create a sandboxir::StoreInst for existing LLVM IR \p SI.
  ///
  /// \param SI Existing llvm::StoreInst to wrap.
  /// \Returns The newly created sandboxir::StoreInst.
  StoreInst *createStoreInst(llvm::StoreInst *SI);
  friend StoreInst; // For createStoreInst()
  /// Create a sandboxir::ReturnInst for existing LLVM IR \p I.
  ///
  /// \param I Existing llvm::ReturnInst to wrap.
  /// \Returns The newly created sandboxir::ReturnInst.
  ReturnInst *createReturnInst(llvm::ReturnInst *I);
  friend ReturnInst; // For createReturnInst()
  /// Create a sandboxir::CallInst for existing LLVM IR \p I.
  ///
  /// \param I Existing llvm::CallInst to wrap.
  /// \Returns The newly created sandboxir::CallInst.
  CallInst *createCallInst(llvm::CallInst *I);
  friend CallInst; // For createCallInst()
  /// Create a sandboxir::InvokeInst for existing LLVM IR \p I.
  ///
  /// \param I Existing llvm::InvokeInst to wrap.
  /// \Returns The newly created sandboxir::InvokeInst.
  InvokeInst *createInvokeInst(llvm::InvokeInst *I);
  friend InvokeInst; // For createInvokeInst()
  /// Create a sandboxir::CallBrInst for existing LLVM IR \p I.
  ///
  /// \param I Existing llvm::CallBrInst to wrap.
  /// \Returns The newly created sandboxir::CallBrInst.
  CallBrInst *createCallBrInst(llvm::CallBrInst *I);
  friend CallBrInst; // For createCallBrInst()
  /// Create a sandboxir::LandingPadInst for existing LLVM IR \p I.
  ///
  /// \param I Existing llvm::LandingPadInst to wrap.
  /// \Returns The newly created sandboxir::LandingPadInst.
  LandingPadInst *createLandingPadInst(llvm::LandingPadInst *I);
  friend LandingPadInst; // For createLandingPadInst()
  /// Create a sandboxir::CatchPadInst for existing LLVM IR \p I.
  ///
  /// \param I Existing llvm::CatchPadInst to wrap.
  /// \Returns The newly created sandboxir::CatchPadInst.
  CatchPadInst *createCatchPadInst(llvm::CatchPadInst *I);
  friend CatchPadInst; // For createCatchPadInst()
  /// Create a sandboxir::CleanupPadInst for existing LLVM IR \p I.
  ///
  /// \param I Existing llvm::CleanupPadInst to wrap.
  /// \Returns The newly created sandboxir::CleanupPadInst.
  CleanupPadInst *createCleanupPadInst(llvm::CleanupPadInst *I);
  friend CleanupPadInst; // For createCleanupPadInst()
  /// Create a sandboxir::CatchReturnInst for existing LLVM IR \p I.
  ///
  /// \param I Existing llvm::CatchReturnInst to wrap.
  /// \Returns The newly created sandboxir::CatchReturnInst.
  CatchReturnInst *createCatchReturnInst(llvm::CatchReturnInst *I);
  friend CatchReturnInst; // For createCatchReturnInst()
  /// Create a sandboxir::CleanupReturnInst for existing LLVM IR \p I.
  ///
  /// \param I Existing llvm::CleanupReturnInst to wrap.
  /// \Returns The newly created sandboxir::CleanupReturnInst.
  CleanupReturnInst *createCleanupReturnInst(llvm::CleanupReturnInst *I);
  friend CleanupReturnInst; // For createCleanupReturnInst()
  /// Create a sandboxir::GetElementPtrInst for existing LLVM IR \p I.
  ///
  /// \param I Existing llvm::GetElementPtrInst to wrap.
  /// \Returns The newly created sandboxir::GetElementPtrInst.
  GetElementPtrInst *createGetElementPtrInst(llvm::GetElementPtrInst *I);
  friend GetElementPtrInst; // For createGetElementPtrInst()
  /// Create a sandboxir::CatchSwitchInst for existing LLVM IR \p I.
  ///
  /// \param I Existing llvm::CatchSwitchInst to wrap.
  /// \Returns The newly created sandboxir::CatchSwitchInst.
  CatchSwitchInst *createCatchSwitchInst(llvm::CatchSwitchInst *I);
  friend CatchSwitchInst; // For createCatchSwitchInst()
  /// Create a sandboxir::ResumeInst for existing LLVM IR \p I.
  ///
  /// \param I Existing llvm::ResumeInst to wrap.
  /// \Returns The newly created sandboxir::ResumeInst.
  ResumeInst *createResumeInst(llvm::ResumeInst *I);
  friend ResumeInst; // For createResumeInst()
  /// Create a sandboxir::SwitchInst for existing LLVM IR \p I.
  ///
  /// \param I Existing llvm::SwitchInst to wrap.
  /// \Returns The newly created sandboxir::SwitchInst.
  SwitchInst *createSwitchInst(llvm::SwitchInst *I);
  friend SwitchInst; // For createSwitchInst()
  /// Create a sandboxir::UnaryOperator for existing LLVM IR \p I.
  ///
  /// \param I Existing llvm::UnaryOperator to wrap.
  /// \Returns The newly created sandboxir::UnaryOperator.
  UnaryOperator *createUnaryOperator(llvm::UnaryOperator *I);
  friend UnaryOperator; // For createUnaryOperator()
  /// Create a sandboxir::BinaryOperator for existing LLVM IR \p I.
  ///
  /// \param I Existing llvm::BinaryOperator to wrap.
  /// \Returns The newly created sandboxir::BinaryOperator.
  BinaryOperator *createBinaryOperator(llvm::BinaryOperator *I);
  friend BinaryOperator; // For createBinaryOperator()
  /// Create a sandboxir::AtomicRMWInst for existing LLVM IR \p I.
  ///
  /// \param I Existing llvm::AtomicRMWInst to wrap.
  /// \Returns The newly created sandboxir::AtomicRMWInst.
  AtomicRMWInst *createAtomicRMWInst(llvm::AtomicRMWInst *I);
  friend AtomicRMWInst; // For createAtomicRMWInst()
  /// Create a sandboxir::AtomicCmpXchgInst for existing LLVM IR \p I.
  ///
  /// \param I Existing llvm::AtomicCmpXchgInst to wrap.
  /// \Returns The newly created sandboxir::AtomicCmpXchgInst.
  AtomicCmpXchgInst *createAtomicCmpXchgInst(llvm::AtomicCmpXchgInst *I);
  friend AtomicCmpXchgInst; // For createAtomicCmpXchgInst()
  /// Create a sandboxir::AllocaInst for existing LLVM IR \p I.
  ///
  /// \param I Existing llvm::AllocaInst to wrap.
  /// \Returns The newly created sandboxir::AllocaInst.
  AllocaInst *createAllocaInst(llvm::AllocaInst *I);
  friend AllocaInst; // For createAllocaInst()
  /// Create a sandboxir::CastInst for existing LLVM IR \p I.
  ///
  /// \param I Existing llvm::CastInst to wrap.
  /// \Returns The newly created sandboxir::CastInst.
  CastInst *createCastInst(llvm::CastInst *I);
  friend CastInst; // For createCastInst()
  /// Create a sandboxir::PHINode for existing LLVM IR \p I.
  ///
  /// \param I Existing llvm::PHINode to wrap.
  /// \Returns The newly created sandboxir::PHINode.
  PHINode *createPHINode(llvm::PHINode *I);
  friend PHINode; // For createPHINode()
  /// Create a sandboxir::UnreachableInst for existing LLVM IR \p UI.
  ///
  /// \param UI Existing llvm::UnreachableInst to wrap.
  /// \Returns The newly created sandboxir::UnreachableInst.
  UnreachableInst *createUnreachableInst(llvm::UnreachableInst *UI);
  friend UnreachableInst; // For createUnreachableInst()
  /// Create a sandboxir::CmpInst for existing LLVM IR \p I.
  ///
  /// \param I Existing llvm::CmpInst to wrap.
  /// \Returns The newly created sandboxir::CmpInst.
  CmpInst *createCmpInst(llvm::CmpInst *I);
  friend CmpInst; // For createCmpInst()
  /// Create a sandboxir::ICmpInst for existing LLVM IR \p I.
  ///
  /// \param I Existing llvm::ICmpInst to wrap.
  /// \Returns The newly created sandboxir::ICmpInst.
  ICmpInst *createICmpInst(llvm::ICmpInst *I);
  friend ICmpInst; // For createICmpInst()
  /// Create a sandboxir::FCmpInst for existing LLVM IR \p I.
  ///
  /// \param I Existing llvm::FCmpInst to wrap.
  /// \Returns The newly created sandboxir::FCmpInst.
  FCmpInst *createFCmpInst(llvm::FCmpInst *I);
  friend FCmpInst; // For createFCmpInst()

public:
  /// Construct a SandboxIR context using \p LLVMCtx.
  ///
  /// \param LLVMCtx Underlying LLVM context.
  Context(LLVMContext &LLVMCtx);
  /// Destroy the context and the SandboxIR objects it owns.
  virtual ~Context();
  /// Clears function-level state.
  void clear();

  /// Return the IR change tracker for this context.
  ///
  /// \Returns The Tracker that records IR changes for this context.
  Tracker &getTracker() { return IRTracker; }
  /// Convenience function for `getTracker().save()`
  void save() { IRTracker.save(); }
  /// Convenience function for `getTracker().revert()`
  ///
  /// \param RevertAll If true, revert all nested checkpoints; otherwise only
  /// the last checkpoint.
  void revert(bool RevertAll = false) { IRTracker.revert(RevertAll); }
  /// Convenience function for `getTracker().accept()`
  ///
  /// \param AcceptAll If true, accept all nested checkpoints; otherwise only
  /// the last checkpoint.
  void accept(bool AcceptAll = false) { IRTracker.accept(AcceptAll); }

  /// Return the sandboxir::Value for \p V, or null if none exists.
  ///
  /// \param V LLVM IR value to look up.
  /// \Returns The sandboxir::Value for \p V, or null if none exists.
  sandboxir::Value *getValue(llvm::Value *V) const;
  /// Return the sandboxir::Value for \p V, or null if none exists.
  ///
  /// \param V LLVM IR value to look up.
  /// \Returns The sandboxir::Value for \p V, or null if none exists.
  const sandboxir::Value *getValue(const llvm::Value *V) const {
    return getValue(const_cast<llvm::Value *>(V));
  }

  /// Return the sandboxir::Module for \p LLVMM, or null if none exists.
  ///
  /// \param LLVMM LLVM IR module to look up.
  /// \Returns The sandboxir::Module for \p LLVMM, or null if none exists.
  Module *getModule(llvm::Module *LLVMM) const;

  /// Return the sandboxir::Module for \p LLVMM, creating it if needed.
  ///
  /// \param LLVMM LLVM IR module to wrap.
  /// \Returns The existing or newly created sandboxir::Module.
  Module *getOrCreateModule(llvm::Module *LLVMM);

  /// Return the sandboxir::Type for \p LLVMTy, creating it if needed.
  ///
  /// \param LLVMTy LLVM IR type to wrap; may be null.
  /// \Returns The sandboxir::Type for \p LLVMTy, or null if \p LLVMTy is null.
  Type *getType(llvm::Type *LLVMTy) {
    if (LLVMTy == nullptr)
      return nullptr;
    auto Pair = LLVMTypeToTypeMap.try_emplace(LLVMTy);
    auto It = Pair.first;
    if (Pair.second)
      It->second = std::unique_ptr<Type, TypeDeleter>(new Type(LLVMTy, *this));
    return It->second.get();
  }

  /// Create a sandboxir::Function for an existing LLVM IR \p F, including all
  /// blocks and instructions.
  ///
  /// This is the main API function for creating Sandbox IR.
  /// Note: this will not fully populate its parent module. The only globals
  /// that will be available are those used within the function.
  /// \param F Existing LLVM IR function to wrap.
  /// \Returns The newly created sandboxir::Function.
  Function *createFunction(llvm::Function *F);

  /// Create a sandboxir::Module corresponding to \p LLVMM.
  ///
  /// \param LLVMM Existing LLVM IR module to wrap.
  /// \Returns The newly created sandboxir::Module.
  Module *createModule(llvm::Module *LLVMM);

  /// Return the number of values registered with Context.
  ///
  /// \Returns The number of SandboxIR values owned by this context.
  size_t getNumValues() const { return LLVMValueToValueMap.size(); }

  /// Register a callback that gets called when a SandboxIR instruction is about
  /// to be removed from its parent.
  ///
  /// Note that this will also be called when reverting the creation of an
  /// instruction.
  /// \param CB Callback to invoke.
  /// \Returns a callback ID for later deregistration.
  CallbackID registerEraseInstrCallback(EraseInstrCallback CB);
  /// Unregister the erase-instruction callback identified by \p ID.
  ///
  /// \param ID Identifier returned by registerEraseInstrCallback.
  void unregisterEraseInstrCallback(CallbackID ID);

  /// Register a callback that gets called right after a SandboxIR instruction
  /// is created.
  ///
  /// Note that this will also be called when reverting the removal of an
  /// instruction.
  /// \param CB Callback to invoke.
  /// \Returns a callback ID for later deregistration.
  CallbackID registerCreateInstrCallback(CreateInstrCallback CB);
  /// Unregister the create-instruction callback identified by \p ID.
  ///
  /// \param ID Identifier returned by registerCreateInstrCallback.
  void unregisterCreateInstrCallback(CallbackID ID);

  /// Register a callback that gets called when a SandboxIR instruction is about
  /// to be moved.
  ///
  /// Note that this will also be called when reverting a move.
  /// \param CB Callback to invoke.
  /// \Returns a callback ID for later deregistration.
  CallbackID registerMoveInstrCallback(MoveInstrCallback CB);
  /// Unregister the move-instruction callback identified by \p ID.
  ///
  /// \param ID Identifier returned by registerMoveInstrCallback.
  void unregisterMoveInstrCallback(CallbackID ID);

  /// Register a callback that gets called when a Use gets set.
  ///
  /// \param CB Callback to invoke.
  /// \Returns a callback ID for later deregistration.
  CallbackID registerSetUseCallback(SetUseCallback CB);
  /// Unregister the set-use callback identified by \p ID.
  ///
  /// \param ID Identifier returned by registerSetUseCallback.
  void unregisterSetUseCallback(CallbackID ID);
};

} // namespace sandboxir

/// DenseMapInfo specialization for sandboxir::Context::CallbackID.
template <> struct DenseMapInfo<sandboxir::Context::CallbackID> {
  /// CallbackID type being specialized.
  using CallbackID = sandboxir::Context::CallbackID;
  /// DenseMapInfo for the underlying integer representation.
  using ReprInfo = DenseMapInfo<CallbackID::ValTy>;

  /// Compute a hash value for callback ID \p ID.
  ///
  /// \param ID Callback identifier to hash.
  /// \Returns A hash value for \p ID.
  static unsigned getHashValue(const CallbackID &ID) {
    return ReprInfo::getHashValue(ID.Val);
  }
  /// Return true if \p LHS and \p RHS are equal.
  ///
  /// \param LHS Left-hand callback ID.
  /// \param RHS Right-hand callback ID.
  /// \Returns True if \p LHS and \p RHS are equal.
  static bool isEqual(const CallbackID &LHS, const CallbackID &RHS) {
    return ReprInfo::isEqual(LHS.Val, RHS.Val);
  }
};

} // namespace llvm

#endif // LLVM_SANDBOXIR_CONTEXT_H
