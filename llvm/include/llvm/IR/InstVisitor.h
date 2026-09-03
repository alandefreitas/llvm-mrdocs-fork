//===- InstVisitor.h - Instruction visitor templates ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//


#ifndef LLVM_IR_INSTVISITOR_H
#define LLVM_IR_INSTVISITOR_H

#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"

namespace llvm {

// We operate on opaque instruction classes, so forward declare all instruction
// types now...
//
#define HANDLE_INST(NUM, OPCODE, CLASS)   class CLASS;
#include "llvm/IR/Instruction.def"

#define DELEGATE(CLASS_TO_VISIT) \
  return static_cast<SubClass*>(this)-> \
               visit##CLASS_TO_VISIT(static_cast<CLASS_TO_VISIT&>(I))


/// Base class for instruction visitors
///
/// Instruction visitors are used when you want to perform different actions
/// for different kinds of instructions without having to use lots of casts
/// and a big switch statement (in your code, that is).
///
/// To define your own visitor, inherit from this class, specifying your
/// new type for the 'SubClass' template parameter, and "override" visitXXX
/// functions in your class. I say "override" because this class is defined
/// in terms of statically resolved overloading, not virtual functions.
///
/// For example, here is a visitor that counts the number of malloc
/// instructions processed:
///
///  /// Declare the class.  Note that we derive from InstVisitor instantiated
///  /// with _our new subclasses_ type.
///  ///
///  struct CountAllocaVisitor : public InstVisitor<CountAllocaVisitor> {
///    unsigned Count;
///    CountAllocaVisitor() : Count(0) {}
///
///    void visitAllocaInst(AllocaInst &AI) { ++Count; }
///  };
///
///  And this class would be used like this:
///    CountAllocaVisitor CAV;
///    CAV.visit(function);
///    NumAllocas = CAV.Count;
///
/// The defined has 'visit' methods for Instruction, and also for BasicBlock,
/// Function, and Module, which recursively process all contained instructions.
///
/// Note that if you don't implement visitXXX for some instruction type,
/// the visitXXX method for instruction superclass will be invoked. So
/// if instructions are added in the future, they will be automatically
/// supported, if you handle one of their superclasses.
///
/// The optional second template argument specifies the type that instruction
/// visitation functions should return. If you specify this, you *MUST* provide
/// an implementation of visitInstruction though!.
///
/// Note that this class is specifically designed as a template to avoid
/// virtual function call overhead.  Defining and using an InstVisitor is just
/// as efficient as having your own switch statement over the instruction
/// opcode.
template<typename SubClass, typename RetTy=void>
class InstVisitor {
  //===--------------------------------------------------------------------===//
  // Interface code - This is the public interface of the InstVisitor that you
  // use to visit instructions...
  //

public:
  /// Visit every instruction in the half-open iterator range [\p Start, \p End).
  ///
  /// \param Start Beginning of the instruction range.
  /// \param End Past-the-end of the instruction range.
  template<class Iterator>
  void visit(Iterator Start, Iterator End) {
    while (Start != End)
      static_cast<SubClass*>(this)->visit(*Start++);
  }

  /// Visit all functions and instructions in module \p M.
  ///
  /// Calls \c visitModule, then recursively visits each function.
  /// \param M The module to visit.
  void visit(Module &M) {
    static_cast<SubClass*>(this)->visitModule(M);
    visit(M.begin(), M.end());
  }

  /// Visit all basic blocks and instructions in function \p F.
  ///
  /// Calls \c visitFunction, then recursively visits each basic block.
  /// \param F The function to visit.
  void visit(Function &F) {
    static_cast<SubClass*>(this)->visitFunction(F);
    visit(F.begin(), F.end());
  }

  /// Visit all instructions in basic block \p BB.
  ///
  /// Calls \c visitBasicBlock, then visits each instruction in the block.
  /// \param BB The basic block to visit.
  void visit(BasicBlock &BB) {
    static_cast<SubClass*>(this)->visitBasicBlock(BB);
    visit(BB.begin(), BB.end());
  }

  /// Visit the module pointed to by \p M.
  ///
  /// \param M Non-null pointer to the module to visit.
  void visit(Module       *M)  { visit(*M); }

  /// Visit the function pointed to by \p F.
  ///
  /// \param F Non-null pointer to the function to visit.
  void visit(Function     *F)  { visit(*F); }

  /// Visit the basic block pointed to by \p BB.
  ///
  /// \param BB Non-null pointer to the basic block to visit.
  void visit(BasicBlock   *BB) { visit(*BB); }

  /// Visit the instruction pointed to by \p I.
  ///
  /// \param I Non-null pointer to the instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visit(Instruction *I)  { return visit(*I); }

  /// Dispatch visitation to the opcode-specific handler for instruction \p I.
  ///
  /// Selects the appropriate \c visitXXX method from the instruction opcode.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visit(Instruction &I) {
    static_assert(std::is_base_of<InstVisitor, SubClass>::value,
                  "Must pass the derived type to this template!");

    switch (I.getOpcode()) {
    default: llvm_unreachable("Unknown instruction type encountered!");
      // Build the switch statement using the Instruction.def file...
#define HANDLE_INST(NUM, OPCODE, CLASS) \
    case Instruction::OPCODE: return \
           static_cast<SubClass*>(this)-> \
                      visit##OPCODE(static_cast<CLASS&>(I));
#include "llvm/IR/Instruction.def"
    }
  }

  //===--------------------------------------------------------------------===//
  // Visitation functions... these functions provide default fallbacks in case
  // the user does not specify what to do for a particular instruction type.
  // The default behavior is to generalize the instruction type to its subtype
  // and try visiting the subtype.  All of this should be inlined perfectly,
  // because there are no virtual functions to get in the way.
  //

  /// Called when beginning visitation of module \p M.
  ///
  /// Override to run code once per module before its functions are visited.
  /// The default implementation does nothing.
  /// \param M The module being entered.
  void visitModule    (Module &M) {}

  /// Called when beginning visitation of function \p F.
  ///
  /// Override to run code once per function before its blocks are visited.
  /// The default implementation does nothing.
  /// \param F The function being entered.
  void visitFunction  (Function &F) {}

  /// Called when beginning visitation of basic block \p BB.
  ///
  /// Override to run code once per block before its instructions are visited.
  /// The default implementation does nothing.
  /// \param BB The basic block being entered.
  void visitBasicBlock(BasicBlock &BB) {}

  // Define instruction specific visitor functions that can be overridden to
  // handle SPECIFIC instructions.  These functions automatically define
  // visitMul to proxy to visitBinaryOperator for instance in case the user does
  // not need this generality.
  //
  // These functions can also implement fan-out, when a single opcode and
  // instruction have multiple more specific Instruction subclasses. The Call
  // instruction currently supports this. We implement that by redirecting that
  // instruction to a special delegation helper.
#define HANDLE_INST(NUM, OPCODE, CLASS) \
    RetTy visit##OPCODE(CLASS &I) { \
      if (NUM == Instruction::Call) \
        return delegateCallInst(I); \
      else \
        DELEGATE(CLASS); \
    }
#include "llvm/IR/Instruction.def"

  // Specific Instruction type classes... note that all of the casts are
  // necessary because we use the instruction classes as opaque types...
  //

  /// Visit an ICmpInst; defaults to \c visitCmpInst.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitICmpInst(ICmpInst &I)                { DELEGATE(CmpInst);}

  /// Visit an FCmpInst; defaults to \c visitCmpInst.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitFCmpInst(FCmpInst &I)                { DELEGATE(CmpInst);}

  /// Visit an AllocaInst; defaults to \c visitUnaryInstruction.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitAllocaInst(AllocaInst &I)            { DELEGATE(UnaryInstruction);}

  /// Visit a LoadInst; defaults to \c visitUnaryInstruction.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitLoadInst(LoadInst     &I)            { DELEGATE(UnaryInstruction);}

  /// Visit a StoreInst; defaults to \c visitInstruction.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitStoreInst(StoreInst   &I)            { DELEGATE(Instruction);}

  /// Visit an AtomicCmpXchgInst; defaults to \c visitInstruction.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitAtomicCmpXchgInst(AtomicCmpXchgInst &I) { DELEGATE(Instruction);}

  /// Visit an AtomicRMWInst; defaults to \c visitInstruction.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitAtomicRMWInst(AtomicRMWInst &I)      { DELEGATE(Instruction);}

  /// Visit a FenceInst; defaults to \c visitInstruction.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitFenceInst(FenceInst   &I)            { DELEGATE(Instruction);}

  /// Visit a GetElementPtrInst; defaults to \c visitInstruction.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitGetElementPtrInst(GetElementPtrInst &I){ DELEGATE(Instruction);}

  /// Visit a PHINode; defaults to \c visitInstruction.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitPHINode(PHINode       &I)            { DELEGATE(Instruction);}

  /// Visit a TruncInst; defaults to \c visitCastInst.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitTruncInst(TruncInst &I)              { DELEGATE(CastInst);}

  /// Visit a ZExtInst; defaults to \c visitCastInst.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitZExtInst(ZExtInst &I)                { DELEGATE(CastInst);}

  /// Visit an SExtInst; defaults to \c visitCastInst.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitSExtInst(SExtInst &I)                { DELEGATE(CastInst);}

  /// Visit an FPTruncInst; defaults to \c visitCastInst.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitFPTruncInst(FPTruncInst &I)          { DELEGATE(CastInst);}

  /// Visit an FPExtInst; defaults to \c visitCastInst.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitFPExtInst(FPExtInst &I)              { DELEGATE(CastInst);}

  /// Visit an FPToUIInst; defaults to \c visitCastInst.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitFPToUIInst(FPToUIInst &I)            { DELEGATE(CastInst);}

  /// Visit an FPToSIInst; defaults to \c visitCastInst.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitFPToSIInst(FPToSIInst &I)            { DELEGATE(CastInst);}

  /// Visit a UIToFPInst; defaults to \c visitCastInst.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitUIToFPInst(UIToFPInst &I)            { DELEGATE(CastInst);}

  /// Visit an SIToFPInst; defaults to \c visitCastInst.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitSIToFPInst(SIToFPInst &I)            { DELEGATE(CastInst);}

  /// Visit a PtrToIntInst; defaults to \c visitCastInst.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitPtrToIntInst(PtrToIntInst &I)        { DELEGATE(CastInst);}

  /// Visit a PtrToAddrInst; defaults to \c visitCastInst.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitPtrToAddrInst(PtrToAddrInst &I)      { DELEGATE(CastInst);}

  /// Visit an IntToPtrInst; defaults to \c visitCastInst.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitIntToPtrInst(IntToPtrInst &I)        { DELEGATE(CastInst);}

  /// Visit a BitCastInst; defaults to \c visitCastInst.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitBitCastInst(BitCastInst &I)          { DELEGATE(CastInst);}

  /// Visit an AddrSpaceCastInst; defaults to \c visitCastInst.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitAddrSpaceCastInst(AddrSpaceCastInst &I) { DELEGATE(CastInst);}

  /// Visit a SelectInst; defaults to \c visitInstruction.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitSelectInst(SelectInst &I)            { DELEGATE(Instruction);}

  /// Visit a VAArgInst; defaults to \c visitUnaryInstruction.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitVAArgInst(VAArgInst   &I)            { DELEGATE(UnaryInstruction);}

  /// Visit an ExtractElementInst; defaults to \c visitInstruction.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitExtractElementInst(ExtractElementInst &I) { DELEGATE(Instruction);}

  /// Visit an InsertElementInst; defaults to \c visitInstruction.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitInsertElementInst(InsertElementInst &I) { DELEGATE(Instruction);}

  /// Visit a ShuffleVectorInst; defaults to \c visitInstruction.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitShuffleVectorInst(ShuffleVectorInst &I) { DELEGATE(Instruction);}

  /// Visit an ExtractValueInst; defaults to \c visitUnaryInstruction.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitExtractValueInst(ExtractValueInst &I){ DELEGATE(UnaryInstruction);}

  /// Visit an InsertValueInst; defaults to \c visitInstruction.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitInsertValueInst(InsertValueInst &I)  { DELEGATE(Instruction); }

  /// Visit a LandingPadInst; defaults to \c visitInstruction.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitLandingPadInst(LandingPadInst &I)    { DELEGATE(Instruction); }

  /// Visit a FuncletPadInst; defaults to \c visitInstruction.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitFuncletPadInst(FuncletPadInst &I) { DELEGATE(Instruction); }

  /// Visit a CleanupPadInst; defaults to \c visitFuncletPadInst.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitCleanupPadInst(CleanupPadInst &I) { DELEGATE(FuncletPadInst); }

  /// Visit a CatchPadInst; defaults to \c visitFuncletPadInst.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitCatchPadInst(CatchPadInst &I)     { DELEGATE(FuncletPadInst); }

  /// Visit a FreezeInst; defaults to \c visitInstruction.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitFreezeInst(FreezeInst &I)         { DELEGATE(Instruction); }

  /// Visit a MemSetInst; defaults to \c visitMemIntrinsic.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitMemSetInst(MemSetInst &I)            { DELEGATE(MemIntrinsic); }

  /// Visit a MemSetPatternInst; defaults to \c visitIntrinsicInst.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitMemSetPatternInst(MemSetPatternInst &I) {
    DELEGATE(IntrinsicInst);
  }

  /// Visit a MemCpyInst; defaults to \c visitMemTransferInst.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitMemCpyInst(MemCpyInst &I)            { DELEGATE(MemTransferInst); }

  /// Visit a MemMoveInst; defaults to \c visitMemTransferInst.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitMemMoveInst(MemMoveInst &I)          { DELEGATE(MemTransferInst); }

  /// Visit a MemTransferInst; defaults to \c visitMemIntrinsic.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitMemTransferInst(MemTransferInst &I)  { DELEGATE(MemIntrinsic); }

  /// Visit a MemIntrinsic; defaults to \c visitIntrinsicInst.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitMemIntrinsic(MemIntrinsic &I)        { DELEGATE(IntrinsicInst); }

  /// Visit a VAStartInst; defaults to \c visitIntrinsicInst.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitVAStartInst(VAStartInst &I)          { DELEGATE(IntrinsicInst); }

  /// Visit a VAEndInst; defaults to \c visitIntrinsicInst.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitVAEndInst(VAEndInst &I)              { DELEGATE(IntrinsicInst); }

  /// Visit a VACopyInst; defaults to \c visitIntrinsicInst.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitVACopyInst(VACopyInst &I)            { DELEGATE(IntrinsicInst); }

  /// Visit an IntrinsicInst; defaults to \c visitCallInst.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitIntrinsicInst(IntrinsicInst &I)      { DELEGATE(CallInst); }

  /// Visit a CallInst; defaults to \c visitCallBase.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitCallInst(CallInst &I)                { DELEGATE(CallBase); }

  /// Visit an InvokeInst; defaults to \c visitCallBase.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitInvokeInst(InvokeInst &I)            { DELEGATE(CallBase); }

  /// Visit a CallBrInst; defaults to \c visitCallBase.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitCallBrInst(CallBrInst &I)            { DELEGATE(CallBase); }

  // While terminators don't have a distinct type modeling them, we support
  // intercepting them with dedicated a visitor callback.

  /// Visit a ReturnInst; defaults to \c visitTerminator.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitReturnInst(ReturnInst &I) {
    return static_cast<SubClass *>(this)->visitTerminator(I);
  }

  /// Visit an unconditional BranchInst; defaults to \c visitTerminator.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitUncondBrInst(UncondBrInst &I) {
    return static_cast<SubClass *>(this)->visitTerminator(I);
  }

  /// Visit a conditional BranchInst; defaults to \c visitTerminator.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitCondBrInst(CondBrInst &I) {
    return static_cast<SubClass *>(this)->visitTerminator(I);
  }

  /// Visit a SwitchInst; defaults to \c visitTerminator.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitSwitchInst(SwitchInst &I) {
    return static_cast<SubClass *>(this)->visitTerminator(I);
  }

  /// Visit an IndirectBrInst; defaults to \c visitTerminator.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitIndirectBrInst(IndirectBrInst &I) {
    return static_cast<SubClass *>(this)->visitTerminator(I);
  }

  /// Visit a ResumeInst; defaults to \c visitTerminator.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitResumeInst(ResumeInst &I) {
    return static_cast<SubClass *>(this)->visitTerminator(I);
  }

  /// Visit an UnreachableInst; defaults to \c visitTerminator.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitUnreachableInst(UnreachableInst &I) {
    return static_cast<SubClass *>(this)->visitTerminator(I);
  }

  /// Visit a CleanupReturnInst; defaults to \c visitTerminator.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitCleanupReturnInst(CleanupReturnInst &I) {
    return static_cast<SubClass *>(this)->visitTerminator(I);
  }

  /// Visit a CatchReturnInst; defaults to \c visitTerminator.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitCatchReturnInst(CatchReturnInst &I) {
    return static_cast<SubClass *>(this)->visitTerminator(I);
  }

  /// Visit a CatchSwitchInst; defaults to \c visitTerminator.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitCatchSwitchInst(CatchSwitchInst &I) {
    return static_cast<SubClass *>(this)->visitTerminator(I);
  }

  /// Visit a terminator instruction; defaults to \c visitInstruction.
  ///
  /// Shared callback for terminator kinds that do not have a dedicated
  /// Instruction subclass modeling.
  /// \param I The terminator instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitTerminator(Instruction &I)    { DELEGATE(Instruction);}

  // Next level propagators: If the user does not overload a specific
  // instruction type, they can overload one of these to get the whole class
  // of instructions...
  //

  /// Visit a CastInst; defaults to \c visitUnaryInstruction.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitCastInst(CastInst &I)                { DELEGATE(UnaryInstruction);}

  /// Visit a UnaryOperator; defaults to \c visitUnaryInstruction.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitUnaryOperator(UnaryOperator &I)      { DELEGATE(UnaryInstruction);}

  /// Visit an FPUnaryOperator; defaults to \c visitUnaryOperator.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitFPUnaryOperator(FPUnaryOperator &I) { DELEGATE(UnaryOperator); }

  /// Visit a BinaryOperator; defaults to \c visitInstruction.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitBinaryOperator(BinaryOperator &I)    { DELEGATE(Instruction);}

  /// Visit an FPBinaryOperator; defaults to \c visitBinaryOperator.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitFPBinaryOperator(FPBinaryOperator &I) { DELEGATE(BinaryOperator); }

  /// Visit a CmpInst; defaults to \c visitInstruction.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitCmpInst(CmpInst &I)                  { DELEGATE(Instruction);}

  /// Visit a UnaryInstruction; defaults to \c visitInstruction.
  /// \param I The instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitUnaryInstruction(UnaryInstruction &I){ DELEGATE(Instruction);}

  /// Visit a CallBase; defaults to \c visitInstruction or \c visitTerminator.
  ///
  /// Invoke and callbr forms are also terminators, so they are forwarded to
  /// \c visitTerminator; other call-base instructions go to \c visitInstruction.
  /// \param I The call-base instruction to visit.
  /// \return The value returned by the selected visitor method.
  RetTy visitCallBase(CallBase &I) {
    if (isa<InvokeInst>(I) || isa<CallBrInst>(I))
      return static_cast<SubClass *>(this)->visitTerminator(I);

    DELEGATE(Instruction);
  }

  /// Default handler for instructions not handled by a more specific visitor.
  ///
  /// Override this for a catch-all. Required when \c RetTy is not void; the
  /// default ignores the instruction and returns nothing.
  /// \param I The instruction that was not handled by a more specific visitor.
  void visitInstruction(Instruction &I) {}  // Ignore unhandled instructions

private:
  // Special helper function to delegate to CallInst subclass visitors.
  RetTy delegateCallInst(CallInst &I) {
    if (const Function *F = I.getCalledFunction()) {
      switch (F->getIntrinsicID()) {
      default:                     DELEGATE(IntrinsicInst);
      case Intrinsic::memcpy:
      case Intrinsic::memcpy_inline:
        DELEGATE(MemCpyInst);
      case Intrinsic::memmove:     DELEGATE(MemMoveInst);
      case Intrinsic::memset:
      case Intrinsic::memset_inline:
        DELEGATE(MemSetInst);
      case Intrinsic::experimental_memset_pattern:
        DELEGATE(MemSetPatternInst);
      case Intrinsic::vastart:     DELEGATE(VAStartInst);
      case Intrinsic::vaend:       DELEGATE(VAEndInst);
      case Intrinsic::vacopy:      DELEGATE(VACopyInst);
      case Intrinsic::not_intrinsic: break;
      }
    }
    DELEGATE(CallInst);
  }

  // An overload that will never actually be called, it is used only from dead
  // code in the dispatching from opcodes to instruction subclasses.
  RetTy delegateCallInst(Instruction &I) {
    llvm_unreachable("delegateCallInst called for non-CallInst");
  }
};

#undef DELEGATE

} // End llvm namespace

#endif
