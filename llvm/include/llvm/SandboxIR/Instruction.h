//===- Instruction.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SANDBOXIR_INSTRUCTION_H
#define LLVM_SANDBOXIR_INSTRUCTION_H

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/SandboxIR/BasicBlock.h"
#include "llvm/SandboxIR/Constant.h"
#include "llvm/SandboxIR/User.h"
#include "llvm/Support/Compiler.h"

namespace llvm::sandboxir {

// Forward declaration for MSVC.
/// SandboxIR class IntrinsicInst.
class IntrinsicInst;

/// Insertion point for creating SandboxIR instructions.
class InsertPosition {
  /// Iterator naming the insertion point.
  BBIterator InsertAt;

public:
  /// Construct an insert position at the end of \p InsertAtEnd.
  /// \param InsertAtEnd Basic block whose end is the insertion point.
  InsertPosition(BasicBlock *InsertAtEnd) {
    assert(InsertAtEnd != nullptr && "Expected non-null!");
    InsertAt = InsertAtEnd->end();
  }
  /// Construct an insert position from iterator \p InsertAt.
  /// \param InsertAt Iterator naming the insertion point.
  InsertPosition(BBIterator InsertAt) : InsertAt(InsertAt) {}
  /// Convert to the underlying basic-block iterator.
  ///
  /// \return The underlying basic-block iterator.
  operator BBIterator() { return InsertAt; }
  /// Return the underlying basic-block iterator.
  ///
  /// \return The underlying basic-block iterator.
  const BBIterator &getIterator() const { return InsertAt; }
  /// Return the instruction at this insert position.
  ///
  /// \return The instruction at this insert position.
  Instruction &operator*() { return *InsertAt; }
  /// Return the basic block that owns this insert position.
  ///
  /// \return The basic block that owns this insert position.
  BasicBlock *getBasicBlock() const { return InsertAt.getNodeParent(); }
};

/// A sandboxir::User with operands, opcode and linked with previous/next
/// instructions in an instruction list.
class Instruction : public User {
public:
  /// SandboxIR opcode identifying the concrete instruction kind.
  enum class Opcode {
#define OP(OPC) OPC,
#define OPCODES(...) __VA_ARGS__
#define DEF_INSTR(ID, OPC, CLASS) OPC
#define DEF_DISABLE_AUTO_UNDEF // ValuesDefFilesList.def includes multiple .def
#include "llvm/SandboxIR/ValuesDefFilesList.def"
#undef OP
#undef OPCODES
#undef DEF_INSTR
  };

protected:
  /// Construct an Instruction wrapping LLVM IR instruction \p I.
  /// \param ID Subclass identifier.
  /// \param Opc SandboxIR opcode for this instruction.
  /// \param I Underlying LLVM IR instruction.
  /// \param SBCtx SandboxIR context that owns this value.
  Instruction(ClassID ID, Opcode Opc, llvm::Instruction *I,
              sandboxir::Context &SBCtx)
      : User(ID, I, SBCtx), Opc(Opc) {}

  /// SandboxIR opcode for this instruction.
  Opcode Opc;

  /// A SandboxIR Instruction may map to multiple LLVM IR Instruction. This
  /// returns its topmost LLVM IR instruction.
  ///
  /// \return The topmost LLVM IR instruction this SandboxIR instruction maps to.
  LLVM_ABI llvm::Instruction *getTopmostLLVMInstruction() const;
  friend class VAArgInst;          // For getTopmostLLVMInstruction().
  friend class FreezeInst;         // For getTopmostLLVMInstruction().
  friend class FenceInst;          // For getTopmostLLVMInstruction().
  friend class SelectInst;         // For getTopmostLLVMInstruction().
  friend class ExtractElementInst; // For getTopmostLLVMInstruction().
  friend class InsertElementInst;  // For getTopmostLLVMInstruction().
  friend class ShuffleVectorInst;  // For getTopmostLLVMInstruction().
  friend class ExtractValueInst;   // For getTopmostLLVMInstruction().
  friend class InsertValueInst;    // For getTopmostLLVMInstruction().
  friend class LoadInst;           // For getTopmostLLVMInstruction().
  friend class StoreInst;          // For getTopmostLLVMInstruction().
  friend class ReturnInst;         // For getTopmostLLVMInstruction().
  friend class CallInst;           // For getTopmostLLVMInstruction().
  friend class InvokeInst;         // For getTopmostLLVMInstruction().
  friend class CallBrInst;         // For getTopmostLLVMInstruction().
  friend class LandingPadInst;     // For getTopmostLLVMInstruction().
  friend class CatchPadInst;       // For getTopmostLLVMInstruction().
  friend class CleanupPadInst;     // For getTopmostLLVMInstruction().
  friend class CatchReturnInst;    // For getTopmostLLVMInstruction().
  friend class CleanupReturnInst;  // For getTopmostLLVMInstruction().
  friend class GetElementPtrInst;  // For getTopmostLLVMInstruction().
  friend class ResumeInst;         // For getTopmostLLVMInstruction().
  friend class CatchSwitchInst;    // For getTopmostLLVMInstruction().
  friend class SwitchInst;         // For getTopmostLLVMInstruction().
  friend class UnaryOperator;      // For getTopmostLLVMInstruction().
  friend class BinaryOperator;     // For getTopmostLLVMInstruction().
  friend class AtomicRMWInst;      // For getTopmostLLVMInstruction().
  friend class AtomicCmpXchgInst;  // For getTopmostLLVMInstruction().
  friend class AllocaInst;         // For getTopmostLLVMInstruction().
  friend class CastInst;           // For getTopmostLLVMInstruction().
  friend class PHINode;            // For getTopmostLLVMInstruction().
  friend class UnreachableInst;    // For getTopmostLLVMInstruction().
  friend class CmpInst;            // For getTopmostLLVMInstruction().

  /// Return the LLVM IR Instructions that this SandboxIR maps to in program
  /// order.
  ///
  /// \return The LLVM IR Instructions that this SandboxIR maps to in program order.
  virtual SmallVector<llvm::Instruction *, 1> getLLVMInstrs() const = 0;
  friend class EraseFromParent; // For getLLVMInstrs().

  /// Set the IRBuilder insert position from \p Pos and return the builder.
  ///
  /// \return The IRBuilder configured for \p Pos.
  /// \param Pos Insert position to apply to the builder.
  static IRBuilder<> &setInsertPos(InsertPosition Pos) {
    auto *WhereBB = Pos.getBasicBlock();
    auto WhereIt = Pos.getIterator();
    auto &Ctx = WhereBB->getContext();
    auto &Builder = Ctx.getLLVMIRBuilder();
    if (WhereIt != WhereBB->end())
      Builder.SetInsertPoint((*Pos).getTopmostLLVMInstruction());
    else
      Builder.SetInsertPoint(cast<llvm::BasicBlock>(WhereBB->Val));
    return Builder;
  }

public:
  /// Return the string name of opcode \p Opc.
  ///
  /// \return The string name of opcode \p Opc.
  /// \param Opc Opcode whose name is requested.
  static const char *getOpcodeName(Opcode Opc) {
    switch (Opc) {
#define OP(OPC)                                                                \
  case Opcode::OPC:                                                            \
    return #OPC;
#define OPCODES(...) __VA_ARGS__
#define DEF_INSTR(ID, OPC, CLASS) OPC
#define DEF_DISABLE_AUTO_UNDEF // ValuesDefFilesList.def includes multiple .def
#include "llvm/SandboxIR/ValuesDefFilesList.def"
#undef OPCODES
#undef DEF_INSTR
    }
    llvm_unreachable("Unknown Opcode");
  }

  /// Return how many LLVM IR instructions this SandboxIR instruction maps to.
  ///
  /// Used by BasicBlock::iterator.
  /// \return The number of underlying LLVM IR instructions.
  virtual unsigned getNumOfIRInstrs() const = 0;
  /// Return a BasicBlock::iterator for this Instruction.
  ///
  /// \return A BasicBlock::iterator for this Instruction.
  LLVM_ABI BBIterator getIterator() const;
  /// Return the next sandboxir::Instruction in the block, or nullptr if at the
  /// end of the block.
  ///
  /// \return The next Instruction, or nullptr if at the end of the block.
  LLVM_ABI Instruction *getNextNode() const;
  /// Return the previous sandboxir::Instruction in the block, or nullptr if at
  /// the beginning of the block.
  ///
  /// \return The previous Instruction, or nullptr if at the beginning of the block.
  LLVM_ABI Instruction *getPrevNode() const;
  /// \Returns this Instruction's opcode.
  ///
  /// Note that SandboxIR has its own opcode state to allow for new
  /// SandboxIR-specific instructions.
  Opcode getOpcode() const { return Opc; }

  /// Return the string name of this instruction's opcode.
  ///
  /// \return The string name of this instruction's opcode.
  const char *getOpcodeName() const { return getOpcodeName(Opc); }

  /// Return the data layout of the module that contains this instruction.
  ///
  /// \return The data layout of the module that contains this instruction.
  const DataLayout &getDataLayout() const {
    return cast<llvm::Instruction>(Val)->getModule()->getDataLayout();
  }
  // Note that these functions below are calling into llvm::Instruction.
  // A sandbox IR instruction could introduce a new opcode that could change the
  // behavior of one of these functions. It is better that these functions are
  // only added as needed and new sandbox IR instructions must explicitly check
  // if any of these functions could have a different behavior.

  /// Return true if this instruction is a terminator.
  ///
  /// \return True if this instruction is a terminator.
  bool isTerminator() const {
    return cast<llvm::Instruction>(Val)->isTerminator();
  }
  /// Return true if this instruction is a unary operator.
  ///
  /// \return True if this instruction is a unary operator.
  bool isUnaryOp() const { return cast<llvm::Instruction>(Val)->isUnaryOp(); }
  /// Return true if this instruction is a binary operator.
  ///
  /// \return True if this instruction is a binary operator.
  bool isBinaryOp() const { return cast<llvm::Instruction>(Val)->isBinaryOp(); }
  /// Return true if this instruction is an integer divide or remainder.
  ///
  /// \return True if this instruction is an integer divide or remainder.
  bool isIntDivRem() const {
    return cast<llvm::Instruction>(Val)->isIntDivRem();
  }
  /// Return true if this instruction is a shift.
  ///
  /// \return True if this instruction is a shift.
  bool isShift() const { return cast<llvm::Instruction>(Val)->isShift(); }
  /// Return true if this instruction is a cast.
  ///
  /// \return True if this instruction is a cast.
  bool isCast() const { return cast<llvm::Instruction>(Val)->isCast(); }
  /// Return true if this instruction is a funclet pad.
  ///
  /// \return True if this instruction is a funclet pad.
  bool isFuncletPad() const {
    return cast<llvm::Instruction>(Val)->isFuncletPad();
  }
  /// Return true if this instruction is a special terminator.
  ///
  /// \return True if this instruction is a special terminator.
  bool isSpecialTerminator() const {
    return cast<llvm::Instruction>(Val)->isSpecialTerminator();
  }
  /// Return true if this instruction is the only user of any of its operands.
  ///
  /// \return True if this instruction is the only user of any of its operands.
  bool isOnlyUserOfAnyOperand() const {
    return cast<llvm::Instruction>(Val)->isOnlyUserOfAnyOperand();
  }
  /// Return true if this instruction is a logical shift.
  ///
  /// \return True if this instruction is a logical shift.
  bool isLogicalShift() const {
    return cast<llvm::Instruction>(Val)->isLogicalShift();
  }

  //===--------------------------------------------------------------------===//
  // Metadata manipulation.
  //===--------------------------------------------------------------------===//

  /// Return true if the instruction has any metadata attached to it.
  ///
  /// \return True if the instruction has any metadata attached to it.
  bool hasMetadata() const {
    return cast<llvm::Instruction>(Val)->hasMetadata();
  }

  /// Return true if this instruction has metadata attached to it other than a
  /// debug location.
  ///
  /// \return True if this instruction has metadata attached to it other than a debug location.
  bool hasMetadataOtherThanDebugLoc() const {
    return cast<llvm::Instruction>(Val)->hasMetadataOtherThanDebugLoc();
  }

  /// Return true if this instruction has the given type of metadata attached.
  ///
  /// \return True if this instruction has the given type of metadata attached.
  /// \param KindID Metadata kind identifier to check for.
  bool hasMetadata(unsigned KindID) const {
    return cast<llvm::Instruction>(Val)->hasMetadata(KindID);
  }

  // TODO: Implement getMetadata and getAllMetadata after sandboxir::MDNode is
  // available.

  // TODO: More missing functions

  /// Detach this from its parent BasicBlock without deleting it.
  LLVM_ABI void removeFromParent();
  /// Detach this Value from its parent and delete it.
  LLVM_ABI void eraseFromParent();
  /// Insert this detached instruction before \p BeforeI.
  /// \param BeforeI Instruction to insert before.
  LLVM_ABI void insertBefore(Instruction *BeforeI);
  /// Insert this detached instruction after \p AfterI.
  /// \param AfterI Instruction to insert after.
  LLVM_ABI void insertAfter(Instruction *AfterI);
  /// Insert this detached instruction into \p BB at \p WhereIt.
  /// \param BB Basic block to insert into.
  /// \param WhereIt Iterator naming the insertion point within \p BB.
  LLVM_ABI void insertInto(BasicBlock *BB, const BBIterator &WhereIt);
  /// Move this instruction to \p WhereIt in \p BB.
  /// \param BB Destination basic block.
  /// \param WhereIt Iterator naming the new position within \p BB.
  LLVM_ABI void moveBefore(BasicBlock &BB, const BBIterator &WhereIt);
  /// Move this instruction before \p Before.
  /// \param Before Instruction to move before.
  void moveBefore(Instruction *Before) {
    moveBefore(*Before->getParent(), Before->getIterator());
  }
  /// Move this instruction after \p After.
  /// \param After Instruction to move after.
  void moveAfter(Instruction *After) {
    moveBefore(*After->getParent(), std::next(After->getIterator()));
  }
  // TODO: This currently relies on LLVM IR Instruction::comesBefore which is
  // can be linear-time.
  /// Return true if this instruction comes before \p Other in the same block.
  ///
  /// \return True if this instruction comes before \p Other in the same block.
  /// \param Other Instruction in the same basic block to compare against.
  bool comesBefore(const Instruction *Other) const {
    return cast<llvm::Instruction>(Val)->comesBefore(
        cast<llvm::Instruction>(Other->Val));
  }
  /// Return the BasicBlock containing this Instruction, or null if detached.
  ///
  /// \return The BasicBlock containing this Instruction, or null if detached.
  LLVM_ABI BasicBlock *getParent() const;
  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test for Instruction.
  LLVM_ABI static bool classof(const sandboxir::Value *From);

  /// Determine whether the no unsigned wrap flag is set.
  ///
  /// \return True if the no unsigned wrap flag is set.
  bool hasNoUnsignedWrap() const {
    return cast<llvm::Instruction>(Val)->hasNoUnsignedWrap();
  }
  /// Set or clear the nuw flag on this instruction.
  ///
  /// The instruction must be an operator which supports this flag. See
  /// LangRef.html for the meaning of this flag.
  /// \param B True to set the flag, false to clear it.
  LLVM_ABI void setHasNoUnsignedWrap(bool B = true);
  /// Determine whether the no signed wrap flag is set.
  ///
  /// \return True if the no signed wrap flag is set.
  bool hasNoSignedWrap() const {
    return cast<llvm::Instruction>(Val)->hasNoSignedWrap();
  }
  /// Set or clear the nsw flag on this instruction.
  ///
  /// The instruction must be an operator which supports this flag. See
  /// LangRef.html for the meaning of this flag.
  /// \param B True to set the flag, false to clear it.
  LLVM_ABI void setHasNoSignedWrap(bool B = true);
  /// Determine whether all fast-math-flags are set.
  ///
  /// \return True if all fast-math-flags are set.
  bool isFast() const { return cast<llvm::Instruction>(Val)->isFast(); }
  /// Set or clear all fast-math-flags on this instruction.
  ///
  /// The instruction must be an operator which supports this flag. See
  /// LangRef.html for the meaning of this flag.
  /// \param B True to set all flags, false to clear them.
  LLVM_ABI void setFast(bool B);
  /// Determine whether the allow-reassociation flag is set.
  ///
  /// \return True if the allow-reassociation flag is set.
  bool hasAllowReassoc() const {
    return cast<llvm::Instruction>(Val)->hasAllowReassoc();
  }
  /// Set or clear the reassociation flag on this instruction.
  ///
  /// The instruction must be an operator which supports this flag. See
  /// LangRef.html for the meaning of this flag.
  /// \param B True to set the flag, false to clear it.
  LLVM_ABI void setHasAllowReassoc(bool B);
  /// Determine whether the exact flag is set.
  ///
  /// \return True if the exact flag is set.
  bool isExact() const { return cast<llvm::Instruction>(Val)->isExact(); }
  /// Set or clear the exact flag on this instruction.
  ///
  /// The instruction must be an operator which supports this flag. See
  /// LangRef.html for the meaning of this flag.
  /// \param B True to set the flag, false to clear it.
  LLVM_ABI void setIsExact(bool B = true);
  /// Determine whether the no-NaNs flag is set.
  ///
  /// \return True if the no-NaNs flag is set.
  bool hasNoNaNs() const { return cast<llvm::Instruction>(Val)->hasNoNaNs(); }
  /// Set or clear the no-nans flag on this instruction.
  ///
  /// The instruction must be an operator which supports this flag. See
  /// LangRef.html for the meaning of this flag.
  /// \param B True to set the flag, false to clear it.
  LLVM_ABI void setHasNoNaNs(bool B);
  /// Determine whether the no-infs flag is set.
  ///
  /// \return True if the no-infs flag is set.
  bool hasNoInfs() const { return cast<llvm::Instruction>(Val)->hasNoInfs(); }
  /// Set or clear the no-infs flag on this instruction.
  ///
  /// The instruction must be an operator which supports this flag. See
  /// LangRef.html for the meaning of this flag.
  /// \param B True to set the flag, false to clear it.
  LLVM_ABI void setHasNoInfs(bool B);
  /// Determine whether the no-signed-zeros flag is set.
  ///
  /// \return True if the no-signed-zeros flag is set.
  bool hasNoSignedZeros() const {
    return cast<llvm::Instruction>(Val)->hasNoSignedZeros();
  }
  /// Set or clear the no-signed-zeros flag on this instruction.
  ///
  /// The instruction must be an operator which supports this flag. See
  /// LangRef.html for the meaning of this flag.
  /// \param B True to set the flag, false to clear it.
  LLVM_ABI void setHasNoSignedZeros(bool B);
  /// Determine whether the allow-reciprocal flag is set.
  ///
  /// \return True if the allow-reciprocal flag is set.
  bool hasAllowReciprocal() const {
    return cast<llvm::Instruction>(Val)->hasAllowReciprocal();
  }
  /// Set or clear the allow-reciprocal flag on this instruction.
  ///
  /// The instruction must be an operator which supports this flag. See
  /// LangRef.html for the meaning of this flag.
  /// \param B True to set the flag, false to clear it.
  LLVM_ABI void setHasAllowReciprocal(bool B);
  /// Determine whether the allow-contract flag is set.
  ///
  /// \return True if the allow-contract flag is set.
  bool hasAllowContract() const {
    return cast<llvm::Instruction>(Val)->hasAllowContract();
  }
  /// Set or clear the allow-contract flag on this instruction.
  ///
  /// The instruction must be an operator which supports this flag. See
  /// LangRef.html for the meaning of this flag.
  /// \param B True to set the flag, false to clear it.
  LLVM_ABI void setHasAllowContract(bool B);
  /// Determine whether the approximate-math-functions flag is set.
  ///
  /// \return True if the approximate-math-functions flag is set.
  bool hasApproxFunc() const {
    return cast<llvm::Instruction>(Val)->hasApproxFunc();
  }
  /// Set or clear the approximate-math-functions flag on this instruction.
  ///
  /// The instruction must be an operator which supports this flag. See
  /// LangRef.html for the meaning of this flag.
  /// \param B True to set the flag, false to clear it.
  LLVM_ABI void setHasApproxFunc(bool B);
  /// \Returns all fast-math flags on this instruction.
  ///
  /// The instruction must be an operator which supports these flags. See
  /// LangRef.html for the meaning of these flags.
  FastMathFlags getFastMathFlags() const {
    return cast<llvm::Instruction>(Val)->getFastMathFlags();
  }
  /// Set multiple fast-math flags on this instruction.
  ///
  /// The instruction must be an operator which supports these flags. See
  /// LangRef.html for the meaning of these flags.
  /// \param FMF Fast-math flags to apply.
  LLVM_ABI void setFastMathFlags(FastMathFlags FMF);
  /// Copy all fast-math flag values onto this instruction.
  ///
  /// The instruction must be an operator which supports these flags. See
  /// LangRef.html for the meaning of these flags.
  /// \param FMF Fast-math flags to copy.
  LLVM_ABI void copyFastMathFlags(FastMathFlags FMF);

  /// Return true if this instruction is associative.
  ///
  /// \return True if this instruction is associative.
  bool isAssociative() const {
    return cast<llvm::Instruction>(Val)->isAssociative();
  }

  /// Return true if this instruction is commutative.
  ///
  /// \return True if this instruction is commutative.
  bool isCommutative() const {
    return cast<llvm::Instruction>(Val)->isCommutative();
  }

  /// Return true if this instruction is idempotent.
  ///
  /// \return True if this instruction is idempotent.
  bool isIdempotent() const {
    return cast<llvm::Instruction>(Val)->isIdempotent();
  }

  /// Return true if this instruction is nilpotent.
  ///
  /// \return True if this instruction is nilpotent.
  bool isNilpotent() const {
    return cast<llvm::Instruction>(Val)->isNilpotent();
  }

  /// Return true if this instruction may write to memory.
  ///
  /// \return True if this instruction may write to memory.
  bool mayWriteToMemory() const {
    return cast<llvm::Instruction>(Val)->mayWriteToMemory();
  }

  /// Return true if this instruction may read from memory.
  ///
  /// \return True if this instruction may read from memory.
  bool mayReadFromMemory() const {
    return cast<llvm::Instruction>(Val)->mayReadFromMemory();
  }
  /// Return true if this instruction may read or write memory.
  ///
  /// \return True if this instruction may read or write memory.
  bool mayReadOrWriteMemory() const {
    return cast<llvm::Instruction>(Val)->mayReadOrWriteMemory();
  }

  /// Return true if this instruction is atomic.
  ///
  /// \return True if this instruction is atomic.
  bool isAtomic() const { return cast<llvm::Instruction>(Val)->isAtomic(); }

  /// Return true if this instruction has an atomic load.
  ///
  /// \return True if this instruction has an atomic load.
  bool hasAtomicLoad() const {
    return cast<llvm::Instruction>(Val)->hasAtomicLoad();
  }

  /// Return true if this instruction has an atomic store.
  ///
  /// \return True if this instruction has an atomic store.
  bool hasAtomicStore() const {
    return cast<llvm::Instruction>(Val)->hasAtomicStore();
  }

  /// Return true if this instruction is volatile.
  ///
  /// \return True if this instruction is volatile.
  bool isVolatile() const { return cast<llvm::Instruction>(Val)->isVolatile(); }

  /// Return the type of memory accessed by this instruction, if any.
  ///
  /// \return The type of memory accessed by this instruction, if any.
  LLVM_ABI Type *getAccessType() const;

  /// Return true if this instruction may throw an exception.
  ///
  /// \return True if this instruction may throw an exception.
  /// \param IncludePhaseOneUnwind Whether phase-one unwind counts as throwing.
  bool mayThrow(bool IncludePhaseOneUnwind = false) const {
    return cast<llvm::Instruction>(Val)->mayThrow(IncludePhaseOneUnwind);
  }

  /// Return true if this instruction acts like a memory fence.
  ///
  /// \return True if this instruction acts like a memory fence.
  bool isFenceLike() const {
    return cast<llvm::Instruction>(Val)->isFenceLike();
  }

  /// Return true if this instruction may have side effects.
  ///
  /// \return True if this instruction may have side effects.
  bool mayHaveSideEffects() const {
    return cast<llvm::Instruction>(Val)->mayHaveSideEffects();
  }

  // TODO: Missing functions.

#ifndef NDEBUG
  /// Dump this instruction to \p OS.
  /// \param OS Output stream.
  void dumpOS(raw_ostream &OS) const override;
#endif
};

/// Instructions that contain a single LLVM Instruction can inherit from this.
template <typename LLVMT> class SingleLLVMInstructionImpl : public Instruction {
  /// SingleLLVMInstructionImpl.
  /// \param ID The ID parameter.
  /// \param Opc Instruction opcode.
  /// \param I Instruction or value being inspected.
  /// \param SBCtx SandboxIR context.
  SingleLLVMInstructionImpl(ClassID ID, Opcode Opc, llvm::Instruction *I,
                            sandboxir::Context &SBCtx)
      : Instruction(ID, Opc, I, SBCtx) {}

  // All instructions are friends with this so they can call the constructor.
#define DEF_INSTR(ID, OPC, CLASS) friend class CLASS;
#include "llvm/SandboxIR/Values.def"
  friend class UnaryInstruction;
  friend class CallBase;
  friend class FuncletPadInst;
  friend class CmpInst;

  /// Return the operand use internal.
  /// \param OpIdx Operand index.
  /// \param Verify The Verify parameter.
  Use getOperandUseInternal(unsigned OpIdx, bool Verify) const final {
    return getOperandUseDefault(OpIdx, Verify);
  }
  SmallVector<llvm::Instruction *, 1> getLLVMInstrs() const final {
    return {cast<llvm::Instruction>(Val)};
  }

public:
  /// Return the operand index of \p Use.
  ///
  /// \return The operand index of \p Use.
  /// \param Use Operand use whose index is requested.
  unsigned getUseOperandNo(const Use &Use) const final {
    return getUseOperandNoDefault(Use);
  }
  /// Return 1 because this wraps a single LLVM IR instruction.
  ///
  /// \return 1 because this wraps a single LLVM IR instruction.
  unsigned getNumOfIRInstrs() const final { return 1u; }
#ifndef NDEBUG
  /// Verify that this wraps an LLVM instruction of type LLVMT.
  void verify() const final { assert(isa<LLVMT>(Val) && "Expected LLVMT!"); }
  /// Dump this instruction to \p OS.
  /// \param OS Output stream.
  void dumpOS(raw_ostream &OS) const override {
    dumpCommonPrefix(OS);
    dumpCommonSuffix(OS);
  }
#endif
};

/// SandboxIR wrapper for an LLVM fence instruction.
class FenceInst : public SingleLLVMInstructionImpl<llvm::FenceInst> {
  /// Construct a FenceInst wrapping LLVM IR fence \p FI.
  /// \param FI Underlying LLVM FenceInst to wrap.
  /// \param Ctx SandboxIR context.
  FenceInst(llvm::FenceInst *FI, Context &Ctx)
      : SingleLLVMInstructionImpl(ClassID::Fence, Opcode::Fence, FI, Ctx) {}
  friend Context; // For constructor;

public:
  /// Create and insert a new FenceInst.
  ///
  /// \return The newly created instruction.
  /// \param Ordering Atomic ordering constraint.
  /// \param Pos Insert position for the new instruction.
  /// \param Ctx SandboxIR context.
  /// \param SSID Synchronization scope identifier.
  LLVM_ABI static FenceInst *create(AtomicOrdering Ordering, InsertPosition Pos,
                                    Context &Ctx,
                                    SyncScope::ID SSID = SyncScope::System);
  /// Returns the ordering constraint of this fence instruction.
  ///
  /// \return The ordering constraint of this fence instruction.
  AtomicOrdering getOrdering() const {
    return cast<llvm::FenceInst>(Val)->getOrdering();
  }
  /// Set the ordering constraint of this fence instruction.
  ///
  /// May only be Acquire, Release, AcquireRelease, or SequentiallyConsistent.
  /// \param Ordering Atomic ordering constraint.
  LLVM_ABI void setOrdering(AtomicOrdering Ordering);
  /// Returns the synchronization scope ID of this fence instruction.
  ///
  /// \return The synchronization scope ID of this fence instruction.
  SyncScope::ID getSyncScopeID() const {
    return cast<llvm::FenceInst>(Val)->getSyncScopeID();
  }
  /// Sets the synchronization scope ID of this fence instruction.
  /// \param SSID Synchronization scope identifier.
  LLVM_ABI void setSyncScopeID(SyncScope::ID SSID);
  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test for FenceInst.
  static bool classof(const Value *From) {
    return From->getSubclassID() == ClassID::Fence;
  }
};

/// SandboxIR wrapper for an LLVM select instruction.
class SelectInst : public SingleLLVMInstructionImpl<llvm::SelectInst> {
  /// Use Context::createSelectInst(). Don't call the constructor directly.
  /// \param CI Underlying LLVM SelectInst to wrap.
  /// \param Ctx SandboxIR context.
  SelectInst(llvm::SelectInst *CI, Context &Ctx)
      : SingleLLVMInstructionImpl(ClassID::Select, Opcode::Select, CI, Ctx) {}
  friend Context; // for SelectInst()

public:
  /// Create and insert a new SelectInst.
  ///
  /// \return The newly created instruction.
  /// \param Cond Condition value.
  /// \param True True value or successor.
  /// \param False False value or successor.
  /// \param Pos Insert position for the new instruction.
  /// \param Ctx SandboxIR context.
  /// \param Name Name of the new instruction.
  LLVM_ABI static Value *create(Value *Cond, Value *True, Value *False,
                                InsertPosition Pos, Context &Ctx,
                                const Twine &Name = "");

  /// Return the condition operand.
  ///
  /// \return The condition operand.
  const Value *getCondition() const { return getOperand(0); }
  /// Return the true value operand.
  ///
  /// \return The true value operand.
  const Value *getTrueValue() const { return getOperand(1); }
  /// Return the false value operand.
  ///
  /// \return The false value operand.
  const Value *getFalseValue() const { return getOperand(2); }
  /// Return the condition operand.
  ///
  /// \return The condition operand.
  Value *getCondition() { return getOperand(0); }
  /// Return the true value operand.
  ///
  /// \return The true value operand.
  Value *getTrueValue() { return getOperand(1); }
  /// Return the false value operand.
  ///
  /// \return The false value operand.
  Value *getFalseValue() { return getOperand(2); }

  /// Set the condition operand.
  /// \param New New condition value.
  void setCondition(Value *New) { setOperand(0, New); }
  /// Set the true value operand.
  /// \param New New true value.
  void setTrueValue(Value *New) { setOperand(1, New); }
  /// Set the false value operand.
  /// \param New New false value.
  void setFalseValue(Value *New) { setOperand(2, New); }
  /// Swap the true and false values of this select.
  LLVM_ABI void swapValues();

  /// Return an error string if select operands are invalid, else null.
  ///
  /// \return An error string if select operands are invalid, else null.
  /// \param Cond Condition value.
  /// \param True True value.
  /// \param False False value.
  static const char *areInvalidOperands(Value *Cond, Value *True,
                                        Value *False) {
    return llvm::SelectInst::areInvalidOperands(Cond->Val, True->Val,
                                                False->Val);
  }

  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test for SelectInst.
  LLVM_ABI static bool classof(const Value *From);
};

/// SandboxIR wrapper for an LLVM insertelement instruction.
class InsertElementInst final
    : public SingleLLVMInstructionImpl<llvm::InsertElementInst> {
  /// Use Context::createInsertElementInst() instead.
  /// \param I Instruction or value being inspected.
  /// \param Ctx SandboxIR context.
  InsertElementInst(llvm::Instruction *I, Context &Ctx)
      : SingleLLVMInstructionImpl(ClassID::InsertElement, Opcode::InsertElement,
                                  I, Ctx) {}
  friend class Context; // For accessing the constructor in create*()

public:
  /// Create and insert a new InsertElementInst.
  ///
  /// \return The newly created instruction.
  /// \param Vec Vector operand.
  /// \param NewElt Element to insert.
  /// \param Idx Element or aggregate index.
  /// \param Pos Insert position for the new instruction.
  /// \param Ctx SandboxIR context.
  /// \param Name Name of the new instruction.
  LLVM_ABI static Value *create(Value *Vec, Value *NewElt, Value *Idx,
                                InsertPosition Pos, Context &Ctx,
                                const Twine &Name = "");
  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test for InsertElementInst.
  static bool classof(const Value *From) {
    return From->getSubclassID() == ClassID::InsertElement;
  }
  /// Return true if the operands are valid for this instruction.
  ///
  /// \return True if the operands are valid for this instruction.
  /// \param Vec Vector operand.
  /// \param NewElt Element to insert.
  /// \param Idx Index.
  static bool isValidOperands(const Value *Vec, const Value *NewElt,
                              const Value *Idx) {
    return llvm::InsertElementInst::isValidOperands(Vec->Val, NewElt->Val,
                                                    Idx->Val);
  }
};

/// SandboxIR wrapper for an LLVM extractelement instruction.
class ExtractElementInst final
    : public SingleLLVMInstructionImpl<llvm::ExtractElementInst> {
  /// Use Context::createExtractElementInst() instead.
  /// \param I Instruction or value being inspected.
  /// \param Ctx SandboxIR context.
  ExtractElementInst(llvm::Instruction *I, Context &Ctx)
      : SingleLLVMInstructionImpl(ClassID::ExtractElement,
                                  Opcode::ExtractElement, I, Ctx) {}
  friend class Context; // For accessing the constructor in
                        // create*()

public:
  /// Create and insert a new ExtractElementInst.
  ///
  /// \return The newly created instruction.
  /// \param Vec Vector operand.
  /// \param Idx Element or aggregate index.
  /// \param Pos Insert position for the new instruction.
  /// \param Ctx SandboxIR context.
  /// \param Name Name of the new instruction.
  LLVM_ABI static Value *create(Value *Vec, Value *Idx, InsertPosition Pos,
                                Context &Ctx, const Twine &Name = "");
  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test for ExtractElementInst.
  static bool classof(const Value *From) {
    return From->getSubclassID() == ClassID::ExtractElement;
  }

  /// Return true if the operands are valid for this instruction.
  ///
  /// \return True if the operands are valid for this instruction.
  /// \param Vec Vector operand.
  /// \param Idx Index.
  static bool isValidOperands(const Value *Vec, const Value *Idx) {
    return llvm::ExtractElementInst::isValidOperands(Vec->Val, Idx->Val);
  }
  /// Return the vector operand.
  ///
  /// \return The vector operand.
  Value *getVectorOperand() { return getOperand(0); }
  /// Return the index operand.
  ///
  /// \return The index operand.
  Value *getIndexOperand() { return getOperand(1); }
  /// Return the vector operand.
  ///
  /// \return The vector operand.
  const Value *getVectorOperand() const { return getOperand(0); }
  /// Return the index operand.
  ///
  /// \return The index operand.
  const Value *getIndexOperand() const { return getOperand(1); }
  /// Return the type of the vector operand.
  ///
  /// \return The type of the vector operand.
  LLVM_ABI VectorType *getVectorOperandType() const;
};

/// SandboxIR wrapper for an LLVM shufflevector instruction.
class ShuffleVectorInst final
    : public SingleLLVMInstructionImpl<llvm::ShuffleVectorInst> {
  /// Use Context::createShuffleVectorInst() instead.
  /// \param I Instruction or value being inspected.
  /// \param Ctx SandboxIR context.
  ShuffleVectorInst(llvm::Instruction *I, Context &Ctx)
      : SingleLLVMInstructionImpl(ClassID::ShuffleVector, Opcode::ShuffleVector,
                                  I, Ctx) {}
  friend class Context; // For accessing the constructor in create*()

public:
  /// Create and insert a new ShuffleVectorInst.
  ///
  /// \return The newly created instruction.
  /// \param V1 First vector operand.
  /// \param V2 Second vector operand.
  /// \param Mask Shuffle mask.
  /// \param Pos Insert position for the new instruction.
  /// \param Ctx SandboxIR context.
  /// \param Name Name of the new instruction.
  LLVM_ABI static Value *create(Value *V1, Value *V2, Value *Mask,
                                InsertPosition Pos, Context &Ctx,
                                const Twine &Name = "");
  /// Create and insert a new ShuffleVectorInst.
  ///
  /// \return The newly created instruction.
  /// \param V1 First vector operand.
  /// \param V2 Second vector operand.
  /// \param Mask Shuffle mask.
  /// \param Pos Insert position for the new instruction.
  /// \param Ctx SandboxIR context.
  /// \param Name Name of the new instruction.
  LLVM_ABI static Value *create(Value *V1, Value *V2, ArrayRef<int> Mask,
                                InsertPosition Pos, Context &Ctx,
                                const Twine &Name = "");
  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test for ShuffleVectorInst.
  static bool classof(const Value *From) {
    return From->getSubclassID() == ClassID::ShuffleVector;
  }

  /// Swap the operands and adjust the mask to preserve the semantics of the
  /// instruction.
  LLVM_ABI void commute();

  /// Return true if a shufflevector instruction can be formed with the
  /// specified operands.
  ///
  /// \return True if a shufflevector instruction can be formed with the specified operands.
  /// \param V1 First vector or operand.
  /// \param V2 Second vector or operand.
  /// \param Mask Shuffle mask.
  static bool isValidOperands(const Value *V1, const Value *V2,
                              const Value *Mask) {
    return llvm::ShuffleVectorInst::isValidOperands(V1->Val, V2->Val,
                                                    Mask->Val);
  }
  /// Return true if the operands are valid for this instruction.
  ///
  /// \return True if the operands are valid for this instruction.
  /// \param V1 First vector or operand.
  /// \param V2 Second vector or operand.
  /// \param Mask Shuffle mask.
  static bool isValidOperands(const Value *V1, const Value *V2,
                              ArrayRef<int> Mask) {
    return llvm::ShuffleVectorInst::isValidOperands(V1->Val, V2->Val, Mask);
  }

  /// Overload to return most specific vector type.
  ///
  /// \return The requested value, or null if unavailable.
  LLVM_ABI VectorType *getType() const;

  /// Return the shuffle mask value for one result element.
  ///
  /// Returns PoisonMaskElem if the element is undef.
  ///
  /// \return The shuffle mask value for one result element.
  /// \param Elt Element index into the shuffle mask.
  int getMaskValue(unsigned Elt) const {
    return cast<llvm::ShuffleVectorInst>(Val)->getMaskValue(Elt);
  }

  /// Convert the input shuffle mask operand to a vector of integers. Undefined
  /// elements of the mask are returned as PoisonMaskElem.
  /// \param Mask Shuffle mask.
  /// \param Result Output vector of mask integers.
  static void getShuffleMask(const Constant *Mask,
                             SmallVectorImpl<int> &Result) {
    llvm::ShuffleVectorInst::getShuffleMask(cast<llvm::Constant>(Mask->Val),
                                            Result);
  }

  /// Return the mask for this instruction as a vector of integers. Undefined
  /// elements of the mask are returned as PoisonMaskElem.
  /// \param Result Output vector of mask integers.
  void getShuffleMask(SmallVectorImpl<int> &Result) const {
    cast<llvm::ShuffleVectorInst>(Val)->getShuffleMask(Result);
  }

  /// Return the mask for this instruction, for use in bitcode.
  ///
  /// \return The mask for this instruction, for use in bitcode.
  LLVM_ABI Constant *getShuffleMaskForBitcode() const;

  /// Convert a shuffle mask to a bitcode constant.
  ///
  /// \return The requested type.
  /// \param Mask Shuffle mask.
  /// \param ResultTy Result type of the constant.
  LLVM_ABI static Constant *convertShuffleMaskForBitcode(ArrayRef<int> Mask,
                                                         Type *ResultTy);

  /// Replace this instruction's shuffle mask.
  /// \param Mask Shuffle mask.
  LLVM_ABI void setShuffleMask(ArrayRef<int> Mask);

  /// Return or fill the shuffle mask as integers.
  ///
  /// \return Or fill the shuffle mask as integers.
  ArrayRef<int> getShuffleMask() const {
    return cast<llvm::ShuffleVectorInst>(Val)->getShuffleMask();
  }

  /// Return true if this shuffle changes vector length.
  ///
  /// Examples: shufflevector <4 x n> A, <4 x n> B, <1,2,3> shufflevector <4 x
  /// n> A, <4 x n> B, <1,2,3,4,5>
  ///
  /// \return True if this shuffle changes vector length.
  bool changesLength() const {
    return cast<llvm::ShuffleVectorInst>(Val)->changesLength();
  }

  /// Return true if this shuffle returns a vector with a greater number of
  /// elements than its source vectors.
  /// Example: shufflevector <2 x n> A, <2 x n> B, <1,2,3>
  ///
  /// \return True if this shuffle returns a vector with a greater number of elements than its source vectors. Example: shufflevector <2 x n> A, <2 x n> B, <1,2,3>.
  bool increasesLength() const {
    return cast<llvm::ShuffleVectorInst>(Val)->increasesLength();
  }

  /// Return true if the mask selects from one source vector.
  ///
  /// Example: <7,5,undef,7>. This assumes that vector operands (of length
  /// NumSrcElts) are the same length as the mask.
  ///
  /// \return True if the mask selects from one source vector.
  /// \param Mask Shuffle mask.
  /// \param NumSrcElts Number of elements in each source vector.
  static bool isSingleSourceMask(ArrayRef<int> Mask, int NumSrcElts) {
    return llvm::ShuffleVectorInst::isSingleSourceMask(Mask, NumSrcElts);
  }
  /// Return true if the mask selects from one source vector.
  ///
  /// Example: <7,5,undef,7>. This assumes that vector operands (of length
  /// NumSrcElts) are the same length as the mask.
  ///
  /// \return True if the mask selects from one source vector.
  /// \param Mask Shuffle mask.
  /// \param NumSrcElts Number of elements in each source vector.
  static bool isSingleSourceMask(const Constant *Mask, int NumSrcElts) {
    return llvm::ShuffleVectorInst::isSingleSourceMask(
        cast<llvm::Constant>(Mask->Val), NumSrcElts);
  }

  /// Return true if this shuffle takes elements from exactly one source vector.
  ///
  /// The length of that vector is unchanged.
  ///
  /// \return True if this shuffle takes elements from exactly one source vector.
  bool isSingleSource() const {
    return cast<llvm::ShuffleVectorInst>(Val)->isSingleSource();
  }

  /// Return true if the mask is an identity without lane crossings.
  ///
  /// A shuffle using this mask is not necessarily a no-op because it may change
  /// the number of elements from its input vectors or it may provide demanded
  /// bits knowledge via undef lanes. Example: <undef,undef,2,3>
  ///
  /// \return True if the mask is an identity without lane crossings.
  /// \param Mask Shuffle mask.
  /// \param NumSrcElts Number of elements in each source vector.
  static bool isIdentityMask(ArrayRef<int> Mask, int NumSrcElts) {
    return llvm::ShuffleVectorInst::isIdentityMask(Mask, NumSrcElts);
  }
  /// Return true if the mask is an identity without lane crossings.
  ///
  /// A shuffle using this mask is not necessarily a no-op because it may change
  /// the number of elements from its input vectors or it may provide demanded
  /// bits knowledge via undef lanes. Example: <undef,undef,2,3>
  ///
  /// \return True if the mask is an identity without lane crossings.
  /// \param Mask Shuffle mask.
  /// \param NumSrcElts Number of elements in each source vector.
  static bool isIdentityMask(const Constant *Mask, int NumSrcElts) {
    return llvm::ShuffleVectorInst::isIdentityMask(
        cast<llvm::Constant>(Mask->Val), NumSrcElts);
  }

  /// Return true if this shuffle is a single-source identity without lane crossings.
  ///
  /// The number of elements is unchanged from the input vectors.
  ///
  /// \return True if this shuffle is a single-source identity without lane crossings.
  bool isIdentity() const {
    return cast<llvm::ShuffleVectorInst>(Val)->isIdentity();
  }

  /// Return true if this shuffle lengthens exactly one source vector with
  /// undefs in the high elements.
  ///
  /// \return True if this shuffle lengthens exactly one source vector with undefs in the high elements.
  bool isIdentityWithPadding() const {
    return cast<llvm::ShuffleVectorInst>(Val)->isIdentityWithPadding();
  }

  /// Return true if this shuffle extracts the first N elements of exactly one
  /// source vector.
  ///
  /// \return True if this shuffle extracts the first N elements of exactly one source vector.
  bool isIdentityWithExtract() const {
    return cast<llvm::ShuffleVectorInst>(Val)->isIdentityWithExtract();
  }

  /// Return true if this shuffle concatenates its two source vectors.
  ///
  /// Returns false if either input is undefined; then the shuffle is better
  /// classified as an identity with padding operation.
  ///
  /// \return True if this shuffle concatenates its two source vectors.
  bool isConcat() const {
    return cast<llvm::ShuffleVectorInst>(Val)->isConcat();
  }

  /// Return true if the mask is equivalent to a vector select.
  ///
  /// A shuffle using this mask would be equivalent to a vector select with a
  /// constant condition operand. Example: <4,1,6,undef>. This returns false if
  /// the mask does not choose from both input vectors. In that case, the
  /// shuffle is better classified as an identity shuffle. This assumes that
  /// vector operands are the same length as the mask (a length-changing shuffle
  /// can never be equivalent to a vector select).
  ///
  /// \return True if the mask is equivalent to a vector select.
  /// \param Mask Shuffle mask.
  /// \param NumSrcElts Number of elements in each source vector.
  static bool isSelectMask(ArrayRef<int> Mask, int NumSrcElts) {
    return llvm::ShuffleVectorInst::isSelectMask(Mask, NumSrcElts);
  }
  /// Return true if the mask is equivalent to a vector select.
  ///
  /// A shuffle using this mask would be equivalent to a vector select with a
  /// constant condition operand. Example: <4,1,6,undef>. This returns false if
  /// the mask does not choose from both input vectors. In that case, the
  /// shuffle is better classified as an identity shuffle. This assumes that
  /// vector operands are the same length as the mask (a length-changing shuffle
  /// can never be equivalent to a vector select).
  ///
  /// \return True if the mask is equivalent to a vector select.
  /// \param Mask Shuffle mask.
  /// \param NumSrcElts Number of elements in each source vector.
  static bool isSelectMask(const Constant *Mask, int NumSrcElts) {
    return llvm::ShuffleVectorInst::isSelectMask(
        cast<llvm::Constant>(Mask->Val), NumSrcElts);
  }

  /// Return true if this shuffle is equivalent to a vector select.
  ///
  /// Requires that all operands have the same number of elements and that the
  /// mask chooses without lane crossings. Example: shufflevector <4 x n> A, <4
  /// x n> B, <undef,1,6,3>. Returns false if the mask does not choose from both
  /// input vectors; then the shuffle is better classified as an identity
  /// shuffle.
  ///
  /// \return True if this shuffle is equivalent to a vector select.
  bool isSelect() const {
    return cast<llvm::ShuffleVectorInst>(Val)->isSelect();
  }

  /// Return true if the mask reverses one source vector.
  ///
  /// Example: <7,6,undef,4>. This assumes that vector operands (of length
  /// NumSrcElts) are the same length as the mask.
  ///
  /// \return True if the mask reverses one source vector.
  /// \param Mask Shuffle mask.
  /// \param NumSrcElts Number of elements in each source vector.
  static bool isReverseMask(ArrayRef<int> Mask, int NumSrcElts) {
    return llvm::ShuffleVectorInst::isReverseMask(Mask, NumSrcElts);
  }
  /// Return true if the mask reverses one source vector.
  ///
  /// Example: <7,6,undef,4>. This assumes that vector operands (of length
  /// NumSrcElts) are the same length as the mask.
  ///
  /// \return True if the mask reverses one source vector.
  /// \param Mask Shuffle mask.
  /// \param NumSrcElts Number of elements in each source vector.
  static bool isReverseMask(const Constant *Mask, int NumSrcElts) {
    return llvm::ShuffleVectorInst::isReverseMask(
        cast<llvm::Constant>(Mask->Val), NumSrcElts);
  }

  /// Return true if this shuffle swaps the order of elements from exactly
  /// one source vector.
  /// Example: shufflevector <4 x n> A, <4 x n> B, <3,undef,1,undef>
  ///
  /// \return True if this shuffle swaps the order of elements from exactly one source vector. Example: shufflevector <4 x n> A, <4 x n> B, <3,undef,1,undef>.
  bool isReverse() const {
    return cast<llvm::ShuffleVectorInst>(Val)->isReverse();
  }

  /// Return true if the mask splats the first element of one source vector.
  ///
  /// This assumes vector operands (of length NumSrcElts) match the mask length.
  ///
  /// \return True if the mask splats the first element of one source vector.
  /// \param Mask Shuffle mask.
  /// \param NumSrcElts Number of elements in each source vector.
  static bool isZeroEltSplatMask(ArrayRef<int> Mask, int NumSrcElts) {
    return llvm::ShuffleVectorInst::isZeroEltSplatMask(Mask, NumSrcElts);
  }
  /// Return true if the mask splats element 0 of one source.
  ///
  /// \return True if the mask splats element 0 of one source.
  /// \param Mask Shuffle mask.
  /// \param NumSrcElts Number of elements in each source vector.
  static bool isZeroEltSplatMask(const Constant *Mask, int NumSrcElts) {
    return llvm::ShuffleVectorInst::isZeroEltSplatMask(
        cast<llvm::Constant>(Mask->Val), NumSrcElts);
  }

  /// Return true if all elements splat the first element of one source.
  ///
  /// The shuffle does not change the length of that vector.
  ///
  /// \return True if all elements splat the first element of one source.
  bool isZeroEltSplat() const {
    return cast<llvm::ShuffleVectorInst>(Val)->isZeroEltSplat();
  }

  /// Return true if this shuffle mask is a transpose mask.
  ///
  /// Transpose masks transpose a 2xn matrix by reading corresponding even-
  /// or odd-numbered elements from two n-dimensional source vectors.
  ///
  /// \return True if this shuffle mask is a transpose mask.
  /// \param Mask Shuffle mask.
  /// \param NumSrcElts Number of elements in each source vector.
  static bool isTransposeMask(ArrayRef<int> Mask, int NumSrcElts) {
    return llvm::ShuffleVectorInst::isTransposeMask(Mask, NumSrcElts);
  }
  /// Return true if the mask is a transpose mask.
  ///
  /// \return True if the mask is a transpose mask.
  /// \param Mask Shuffle mask.
  /// \param NumSrcElts Number of elements in each source vector.
  static bool isTransposeMask(const Constant *Mask, int NumSrcElts) {
    return llvm::ShuffleVectorInst::isTransposeMask(
        cast<llvm::Constant>(Mask->Val), NumSrcElts);
  }

  /// Return true if this shuffle transposes its inputs without changing length.
  ///
  /// This operation may also be known as a merge or interleave. See the
  /// description for isTransposeMask() for the exact specification.
  /// Example: shufflevector <4 x n> A, <4 x n> B, <0,4,2,6>
  ///
  /// \return True if this shuffle transposes its inputs without changing length.
  bool isTranspose() const {
    return cast<llvm::ShuffleVectorInst>(Val)->isTranspose();
  }

  /// Return true if this shuffle mask is a splice mask.
  ///
  /// A splice concatenates the two inputs and extracts an original-width
  /// vector starting from the splice index.
  ///
  /// \return True if this shuffle mask is a splice mask.
  /// \param Mask Shuffle mask.
  /// \param NumSrcElts Number of elements in each source vector.
  /// \param Index Index written on success.
  static bool isSpliceMask(ArrayRef<int> Mask, int NumSrcElts, int &Index) {
    return llvm::ShuffleVectorInst::isSpliceMask(Mask, NumSrcElts, Index);
  }
  /// Return true if the mask is a splice mask.
  ///
  /// \return True if the mask is a splice mask.
  /// \param Mask Shuffle mask.
  /// \param NumSrcElts Number of elements in each source vector.
  /// \param Index Index written on success.
  static bool isSpliceMask(const Constant *Mask, int NumSrcElts, int &Index) {
    return llvm::ShuffleVectorInst::isSpliceMask(
        cast<llvm::Constant>(Mask->Val), NumSrcElts, Index);
  }

  /// Return true if this shuffle splices two inputs without changing length.
  ///
  /// This concatenates the two inputs and extracts an original-width vector
  /// starting from the splice index.
  ///
  /// \return True if this shuffle splices two inputs without changing length.
  /// \param Index Index written on success.
  bool isSplice(int &Index) const {
    return cast<llvm::ShuffleVectorInst>(Val)->isSplice(Index);
  }

  /// Return true if this shuffle mask is an extract subvector mask.
  ///
  /// A valid extract subvector mask returns a smaller vector from a single
  /// source operand. The base extraction index is returned as well.
  ///
  /// \return True if this shuffle mask is an extract subvector mask.
  /// \param Mask Shuffle mask.
  /// \param NumSrcElts Number of elements in each source vector.
  /// \param Index Index written on success.
  static bool isExtractSubvectorMask(ArrayRef<int> Mask, int NumSrcElts,
                                     int &Index) {
    return llvm::ShuffleVectorInst::isExtractSubvectorMask(Mask, NumSrcElts,
                                                           Index);
  }
  /// Return true if the mask extracts a subvector.
  ///
  /// \return True if the mask extracts a subvector.
  /// \param Mask Shuffle mask.
  /// \param NumSrcElts Number of elements in each source vector.
  /// \param Index Index written on success.
  static bool isExtractSubvectorMask(const Constant *Mask, int NumSrcElts,
                                     int &Index) {
    return llvm::ShuffleVectorInst::isExtractSubvectorMask(
        cast<llvm::Constant>(Mask->Val), NumSrcElts, Index);
  }

  /// Return true if this shuffle mask is an extract subvector mask.
  ///
  /// \return True if this shuffle mask is an extract subvector mask.
  /// \param Index Index written on success.
  bool isExtractSubvectorMask(int &Index) const {
    return cast<llvm::ShuffleVectorInst>(Val)->isExtractSubvectorMask(Index);
  }

  /// Return true if the mask inserts a subvector.
  ///
  /// A valid insert subvector mask inserts the lowest elements of a second
  /// source operand into an in-place first source operand. Both the sub vector
  /// width and the insertion index is returned.
  ///
  /// \return True if the mask inserts a subvector.
  /// \param Mask Shuffle mask.
  /// \param NumSrcElts Number of elements in each source vector.
  /// \param NumSubElts Number of subvector elements.
  /// \param Index Index written on success.
  static bool isInsertSubvectorMask(ArrayRef<int> Mask, int NumSrcElts,
                                    int &NumSubElts, int &Index) {
    return llvm::ShuffleVectorInst::isInsertSubvectorMask(Mask, NumSrcElts,
                                                          NumSubElts, Index);
  }
  /// Return true if the mask inserts a subvector.
  ///
  /// A valid insert subvector mask inserts the lowest elements of a second
  /// source operand into an in-place first source operand. Both the sub vector
  /// width and the insertion index is returned.
  ///
  /// \return True if the mask inserts a subvector.
  /// \param Mask Shuffle mask.
  /// \param NumSrcElts Number of elements in each source vector.
  /// \param NumSubElts Number of subvector elements.
  /// \param Index Index written on success.
  static bool isInsertSubvectorMask(const Constant *Mask, int NumSrcElts,
                                    int &NumSubElts, int &Index) {
    return llvm::ShuffleVectorInst::isInsertSubvectorMask(
        cast<llvm::Constant>(Mask->Val), NumSrcElts, NumSubElts, Index);
  }

  /// Return true if the mask inserts a subvector.
  ///
  /// A valid insert subvector mask inserts the lowest elements of a second
  /// source operand into an in-place first source operand. Both the sub vector
  /// width and the insertion index is returned.
  ///
  /// \return True if the mask inserts a subvector.
  /// \param NumSubElts Number of subvector elements.
  /// \param Index Index written on success.
  bool isInsertSubvectorMask(int &NumSubElts, int &Index) const {
    return cast<llvm::ShuffleVectorInst>(Val)->isInsertSubvectorMask(NumSubElts,
                                                                     Index);
  }

  /// Return true if the mask replicates each element.
  ///
  /// Each of the VF elements in a vector is replicated ReplicationFactor times.
  /// For example, the mask for ReplicationFactor=3 and VF=4 is:
  /// <0,0,0,1,1,1,2,2,2,3,3,3>
  ///
  /// \return True if the mask replicates each element.
  /// \param Mask Shuffle mask.
  /// \param ReplicationFactor How many times each element is replicated.
  /// \param VF Vectorization factor / number of elements.
  static bool isReplicationMask(ArrayRef<int> Mask, int &ReplicationFactor,
                                int &VF) {
    return llvm::ShuffleVectorInst::isReplicationMask(Mask, ReplicationFactor,
                                                      VF);
  }
  /// Return true if the mask replicates each element.
  ///
  /// Each of the VF elements in a vector is replicated ReplicationFactor times.
  /// For example, the mask for ReplicationFactor=3 and VF=4 is:
  /// <0,0,0,1,1,1,2,2,2,3,3,3>
  ///
  /// \return True if the mask replicates each element.
  /// \param Mask Shuffle mask.
  /// \param ReplicationFactor How many times each element is replicated.
  /// \param VF Vectorization factor / number of elements.
  static bool isReplicationMask(const Constant *Mask, int &ReplicationFactor,
                                int &VF) {
    return llvm::ShuffleVectorInst::isReplicationMask(
        cast<llvm::Constant>(Mask->Val), ReplicationFactor, VF);
  }

  /// Return true if the mask replicates each element.
  ///
  /// Each of the VF elements in a vector is replicated ReplicationFactor times.
  /// For example, the mask for ReplicationFactor=3 and VF=4 is:
  /// <0,0,0,1,1,1,2,2,2,3,3,3>
  ///
  /// \return True if the mask replicates each element.
  /// \param ReplicationFactor How many times each element is replicated.
  /// \param VF Vectorization factor / number of elements.
  bool isReplicationMask(int &ReplicationFactor, int &VF) const {
    return cast<llvm::ShuffleVectorInst>(Val)->isReplicationMask(
        ReplicationFactor, VF);
  }

  /// Return true if this shuffle mask is a clustered mask of size VF.
  ///
  /// Each index between [0..VF) is used exactly once in each submask of size VF.
  ///
  /// \return True if this shuffle mask is a clustered mask of size VF.
  /// \param Mask Shuffle mask.
  /// \param VF Vectorization factor / number of elements.
  static bool isOneUseSingleSourceMask(ArrayRef<int> Mask, int VF) {
    return llvm::ShuffleVectorInst::isOneUseSingleSourceMask(Mask, VF);
  }

  /// Return true if this shuffle mask is a one-use-single-source("clustered")
  /// mask.
  ///
  /// \return True if this shuffle mask is a one-use-single-source("clustered") mask.
  /// \param VF Vectorization factor / number of elements.
  bool isOneUseSingleSourceMask(int VF) const {
    return cast<llvm::ShuffleVectorInst>(Val)->isOneUseSingleSourceMask(VF);
  }

  /// Change values in a shuffle permute mask assuming the two vector operands
  /// of length InVecNumElts have swapped position.
  /// \param Mask Shuffle mask.
  /// \param InVecNumElts Number of elements in each input vector.
  static void commuteShuffleMask(MutableArrayRef<int> Mask,
                                 unsigned InVecNumElts) {
    llvm::ShuffleVectorInst::commuteShuffleMask(Mask, InVecNumElts);
  }

  /// Return if this shuffle interleaves its two input vectors together.
  ///
  /// \return If this shuffle interleaves its two input vectors together.
  /// \param Factor Interleave or de-interleave factor.
  bool isInterleave(unsigned Factor) const {
    return cast<llvm::ShuffleVectorInst>(Val)->isInterleave(Factor);
  }

  /// Return true if the mask interleaves one or more input vectors together.
  ///
  /// I.e. <0, LaneLen, ... , LaneLen*(Factor - 1), 1, LaneLen + 1, ...>
  /// E.g. For a Factor of 2 (LaneLen=4):
  ///   <0, 4, 1, 5, 2, 6, 3, 7>
  /// E.g. For a Factor of 3 (LaneLen=4):
  ///   <4, 0, 9, 5, 1, 10, 6, 2, 11, 7, 3, 12>
  /// E.g. For a Factor of 4 (LaneLen=2):
  ///   <0, 2, 6, 4, 1, 3, 7, 5>
  ///
  /// NumInputElts is the total number of elements in the input vectors.
  ///
  /// StartIndexes are the first indexes of each vector being interleaved,
  /// substituting any indexes that were undef
  /// E.g. <4, -1, 2, 5, 1, 3> (Factor=3): StartIndexes=<4, 0, 2>
  ///
  /// Note that this does not check if the input vectors are consecutive:
  /// It will return true for masks such as
  /// <0, 4, 6, 1, 5, 7> (Factor=3, LaneLen=2)
  ///
  /// \return True if the mask interleaves one or more input vectors together.
  /// \param Mask Shuffle mask.
  /// \param Factor Interleave or de-interleave factor.
  /// \param NumInputElts Total number of elements in the input vectors.
  /// \param StartIndexes First indexes of each interleaved vector.
  static bool isInterleaveMask(ArrayRef<int> Mask, unsigned Factor,
                               unsigned NumInputElts,
                               SmallVectorImpl<unsigned> &StartIndexes) {
    return llvm::ShuffleVectorInst::isInterleaveMask(Mask, Factor, NumInputElts,
                                                     StartIndexes);
  }
  /// Return true if the mask interleaves input vectors.
  ///
  /// \return True if the mask interleaves input vectors.
  /// \param Mask Shuffle mask.
  /// \param Factor Interleave or de-interleave factor.
  /// \param NumInputElts Total number of elements in the input vectors.
  static bool isInterleaveMask(ArrayRef<int> Mask, unsigned Factor,
                               unsigned NumInputElts) {
    return llvm::ShuffleVectorInst::isInterleaveMask(Mask, Factor,
                                                     NumInputElts);
  }

  /// Check if the mask is a DE-interleave mask of the given factor
  /// \p Factor like:
  ///     <Index, Index+Factor, ..., Index+(NumElts-1)*Factor>
  ///
  /// \return True if the condition described by this query holds.
  /// \param Mask Shuffle mask.
  /// \param Factor Interleave or de-interleave factor.
  /// \param Index Index written on success.
  static bool isDeInterleaveMaskOfFactor(ArrayRef<int> Mask, unsigned Factor,
                                         unsigned &Index) {
    return llvm::ShuffleVectorInst::isDeInterleaveMaskOfFactor(Mask, Factor,
                                                               Index);
  }
  /// Return true if the mask de-interleaves with a factor.
  ///
  /// \return True if the mask de-interleaves with a factor.
  /// \param Mask Shuffle mask.
  /// \param Factor Interleave or de-interleave factor.
  static bool isDeInterleaveMaskOfFactor(ArrayRef<int> Mask, unsigned Factor) {
    return llvm::ShuffleVectorInst::isDeInterleaveMaskOfFactor(Mask, Factor);
  }

  /// Checks if the shuffle is a bit rotation of the first operand across
  /// multiple subelements, e.g:
  ///
  /// shuffle <8 x i8> %a, <8 x i8> poison, <8 x i32> <1, 0, 3, 2, 5, 4, 7, 6>
  ///
  /// could be expressed as
  ///
  /// rotl <4 x i16> %a, 8
  ///
  /// If it can be expressed as a rotation, returns the number of subelements to
  /// group by in NumSubElts and the number of bits to rotate left in RotateAmt.
  ///
  /// \return True if the condition described by this query holds.
  /// \param Mask Shuffle mask.
  /// \param EltSizeInBits Element size in bits.
  /// \param MinSubElts Minimum number of sub-elements.
  /// \param MaxSubElts Maximum number of sub-elements.
  /// \param NumSubElts Number of subvector elements.
  /// \param RotateAmt Rotate amount written on success.
  static bool isBitRotateMask(ArrayRef<int> Mask, unsigned EltSizeInBits,
                              unsigned MinSubElts, unsigned MaxSubElts,
                              unsigned &NumSubElts, unsigned &RotateAmt) {
    return llvm::ShuffleVectorInst::isBitRotateMask(
        Mask, EltSizeInBits, MinSubElts, MaxSubElts, NumSubElts, RotateAmt);
  }
};

/// SandboxIR wrapper for an LLVM insertvalue instruction.
class InsertValueInst
    : public SingleLLVMInstructionImpl<llvm::InsertValueInst> {
  /// Use Context::createInsertValueInst(). Don't call the constructor directly.
  /// \param IVI The IVI parameter.
  /// \param Ctx SandboxIR context.
  InsertValueInst(llvm::InsertValueInst *IVI, Context &Ctx)
      : SingleLLVMInstructionImpl(ClassID::InsertValue, Opcode::InsertValue,
                                  IVI, Ctx) {}
  friend Context; // for InsertValueInst()

public:
  /// Create and insert a new InsertValueInst.
  ///
  /// \return The newly created instruction.
  /// \param Agg Aggregate operand.
  /// \param Val Value to insert or store.
  /// \param Idxs Index list.
  /// \param Pos Insert position for the new instruction.
  /// \param Ctx SandboxIR context.
  /// \param Name Name of the new instruction.
  LLVM_ABI static Value *create(Value *Agg, Value *Val, ArrayRef<unsigned> Idxs,
                                InsertPosition Pos, Context &Ctx,
                                const Twine &Name = "");

  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test for InsertValueInst.
  static bool classof(const Value *From) {
    return From->getSubclassID() == ClassID::InsertValue;
  }

  /// Iterator over aggregate indices.
  using idx_iterator = llvm::InsertValueInst::idx_iterator;
  /// Return an iterator to the first index.
  ///
  /// \return An iterator to the first index.
  inline idx_iterator idx_begin() const {
    return cast<llvm::InsertValueInst>(Val)->idx_begin();
  }
  /// Return an iterator past the last index.
  ///
  /// \return An iterator past the last index.
  inline idx_iterator idx_end() const {
    return cast<llvm::InsertValueInst>(Val)->idx_end();
  }
  /// Return a range over the indices.
  ///
  /// \return A range over the indices.
  inline iterator_range<idx_iterator> indices() const {
    return cast<llvm::InsertValueInst>(Val)->indices();
  }

  /// Return the aggregate operand.
  ///
  /// \return The aggregate operand.
  Value *getAggregateOperand() {
    return getOperand(getAggregateOperandIndex());
  }
  /// Return the aggregate operand.
  ///
  /// \return The aggregate operand.
  const Value *getAggregateOperand() const {
    return getOperand(getAggregateOperandIndex());
  }
  /// Return the operand index of the aggregate.
  ///
  /// \return The operand index of the aggregate.
  static unsigned getAggregateOperandIndex() {
    return llvm::InsertValueInst::getAggregateOperandIndex();
  }

  /// Return the inserted-value operand.
  ///
  /// \return The inserted-value operand.
  Value *getInsertedValueOperand() {
    return getOperand(getInsertedValueOperandIndex());
  }
  /// Return the inserted-value operand.
  ///
  /// \return The inserted-value operand.
  const Value *getInsertedValueOperand() const {
    return getOperand(getInsertedValueOperandIndex());
  }
  /// Return the operand index of the inserted value.
  ///
  /// \return The operand index of the inserted value.
  static unsigned getInsertedValueOperandIndex() {
    return llvm::InsertValueInst::getInsertedValueOperandIndex();
  }

  /// Return the index list.
  ///
  /// \return The index list.
  ArrayRef<unsigned> getIndices() const {
    return cast<llvm::InsertValueInst>(Val)->getIndices();
  }

  /// Return the number of indices.
  ///
  /// \return The number of indices.
  unsigned getNumIndices() const {
    return cast<llvm::InsertValueInst>(Val)->getNumIndices();
  }

  /// Return true if this instruction has indices.
  ///
  /// \return True if this instruction has indices.
  unsigned hasIndices() const {
    return cast<llvm::InsertValueInst>(Val)->hasIndices();
  }
};

/// Shared base for UncondBrInst and CondBrInst successor accessors.
///
/// Avoids duplication of the successor iterators and successors(). Does not
/// hold any state.
class BrInstCommon {
private:
  /// SandboxIR class LLVMBBToSBBB.
  struct LLVMBBToSBBB {
    Context &Ctx;
    LLVMBBToSBBB(Context &Ctx) : Ctx(Ctx) {}
    LLVM_ABI BasicBlock *operator()(llvm::BasicBlock *BB) const;
  };

  /// SandboxIR class ConstLLVMBBToSBBB.
  struct ConstLLVMBBToSBBB {
    Context &Ctx;
    ConstLLVMBBToSBBB(Context &Ctx) : Ctx(Ctx) {}
    LLVM_ABI const BasicBlock *operator()(const llvm::BasicBlock *BB) const;
  };

protected:
  template <typename LLVMBrTy>
  /// SandboxIR iterator over successor operands.
  using sb_succ_op_iterator =
      mapped_iterator<typename LLVMBrTy::succ_iterator, LLVMBBToSBBB>;
  template <typename LLVMBrTy>
  /// Return a range over the successor basic blocks.
  ///
  /// \return A range over the successor basic blocks.
  /// \param Val Value operand.
  /// \param Ctx SandboxIR context.
  iterator_range<sb_succ_op_iterator<LLVMBrTy>> successors(llvm::Value *Val,
                                                           Context &Ctx) {
    iterator_range<typename LLVMBrTy::succ_iterator> LLVMRange =
        cast<LLVMBrTy>(Val)->successors();
    LLVMBBToSBBB BBMap(Ctx);
    sb_succ_op_iterator<LLVMBrTy> MappedBegin =
        map_iterator(LLVMRange.begin(), BBMap);
    sb_succ_op_iterator<LLVMBrTy> MappedEnd =
        map_iterator(LLVMRange.end(), BBMap);
    return make_range(MappedBegin, MappedEnd);
  }

  template <typename LLVMBrTy>
  /// Const SandboxIR iterator over successor operands.
  using const_sb_succ_op_iterator =
      mapped_iterator<typename LLVMBrTy::const_succ_iterator,
                      ConstLLVMBBToSBBB>;
  template <typename LLVMBrTy>
  /// Return a range over the successor basic blocks.
  /// \param Val Value operand.
  /// \param Ctx SandboxIR context.
  iterator_range<const_sb_succ_op_iterator<LLVMBrTy>>
  /// Return a range over the successor basic blocks.
  ///
  /// \return A range over the successor basic blocks.
  /// \param Val Value operand.
  /// \param Ctx SandboxIR context.
  successors(llvm::Value *Val, Context &Ctx) const {
    llvm::iterator_range<typename LLVMBrTy::const_succ_iterator>
        ConstLLVMRange =
            static_cast<const LLVMBrTy *>(cast<LLVMBrTy>(Val))->successors();
    ConstLLVMBBToSBBB ConstBBMap(Ctx);
    const_sb_succ_op_iterator<LLVMBrTy> ConstMappedBegin =
        map_iterator(ConstLLVMRange.begin(), ConstBBMap);
    const_sb_succ_op_iterator<LLVMBrTy> ConstMappedEnd =
        map_iterator(ConstLLVMRange.end(), ConstBBMap);
    return make_range(ConstMappedBegin, ConstMappedEnd);
  }
};

/// SandboxIR wrapper for an unconditional branch instruction.
class UncondBrInst : public SingleLLVMInstructionImpl<llvm::UncondBrInst>,
                     public BrInstCommon {
  /// Use Context::createUncondBrInst(). Don't call the constructor directly.
  /// \param UBI The UBI parameter.
  /// \param Ctx SandboxIR context.
  UncondBrInst(llvm::UncondBrInst *UBI, Context &Ctx)
      : SingleLLVMInstructionImpl(ClassID::UncondBr, Opcode::UncondBr, UBI,
                                  Ctx) {}
  friend Context; // for UncondBrInst()

public:
  LLVM_ABI static UncondBrInst *
  /// Create and insert a new UncondBrInst.
  ///
  /// \return The newly created instruction.
  /// \param Target The Target parameter.
  /// \param InsertBefore Instruction to insert before.
  /// \param Ctx SandboxIR context.
  create(BasicBlock *Target, InsertPosition InsertBefore, Context &Ctx);
  /// Return the successor basic block.
  ///
  /// \return The successor basic block.
  LLVM_ABI BasicBlock *getSuccessor() const;
  /// Set the successor at the given index.
  /// \param NewSucc The NewSucc parameter.
  LLVM_ABI void setSuccessor(BasicBlock *NewSucc);
  /// Return the number of successors.
  ///
  /// \return The number of successors.
  unsigned getNumSuccessors() const { return 1; }
  /// Iterator over successor operands as BasicBlock pointers.
  using succ_op_iterator = sb_succ_op_iterator<llvm::UncondBrInst>;
  /// Const iterator over successor operands as BasicBlock pointers.
  using const_succ_op_iterator = const_sb_succ_op_iterator<llvm::UncondBrInst>;
  /// Return a range over the successor basic blocks.
  ///
  /// \return A range over the successor basic blocks.
  iterator_range<succ_op_iterator> successors() {
    return BrInstCommon::successors<llvm::UncondBrInst>(Val, Ctx);
  }
  /// Return a range over the successor basic blocks.
  ///
  /// \return A range over the successor basic blocks.
  iterator_range<const_succ_op_iterator> successors() const {
    return BrInstCommon::successors<llvm::UncondBrInst>(Val, Ctx);
  }

  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test.
  LLVM_ABI static bool classof(const Value *From);
};

/// SandboxIR wrapper for a conditional branch instruction.
class CondBrInst : public SingleLLVMInstructionImpl<llvm::CondBrInst>,
                   public BrInstCommon {
  /// Use Context::createUncondBrInst(). Don't call the constructor directly.
  /// \param CBI The CBI parameter.
  /// \param Ctx SandboxIR context.
  CondBrInst(llvm::CondBrInst *CBI, Context &Ctx)
      : SingleLLVMInstructionImpl(ClassID::CondBr, Opcode::CondBr, CBI, Ctx) {}
  friend Context; // for UcnondBrInst()

public:
  /// Create and insert a new CondBrInst.
  ///
  /// \return The newly created instruction.
  /// \param Cond Condition value.
  /// \param IfTrue True successor basic block.
  /// \param IfFalse False successor basic block.
  /// \param InsertBefore Instruction to insert before.
  /// \param Ctx SandboxIR context.
  LLVM_ABI static CondBrInst *create(Value *Cond, BasicBlock *IfTrue,
                                     BasicBlock *IfFalse,
                                     InsertPosition InsertBefore, Context &Ctx);
  /// Return the condition operand.
  ///
  /// \return The condition operand.
  LLVM_ABI Value *getCondition() const;
  /// Set the condition operand.
  /// \param V Value or flag.
  LLVM_ABI void setCondition(Value *V);
  /// Return the successor basic block at index \p SuccIdx.
  ///
  /// \return The successor basic block at index \p SuccIdx.
  /// \param SuccIdx Successor index (0 = true, 1 = false).
  LLVM_ABI BasicBlock *getSuccessor(unsigned SuccIdx) const;
  /// Set the successor at the given index.
  /// \param Idx Index.
  /// \param NewSucc The NewSucc parameter.
  LLVM_ABI void setSuccessor(unsigned Idx, BasicBlock *NewSucc);
  /// Return the number of successors.
  ///
  /// \return The number of successors.
  unsigned getNumSuccessors() const { return 2; }
  /// Swap the true and false successors of this branch.
  void swapSuccessors() { swapOperandsInternal(1, 2); }
  /// Iterator over successor operands as BasicBlock pointers.
  using succ_op_iterator = sb_succ_op_iterator<llvm::CondBrInst>;
  /// Const iterator over successor operands as BasicBlock pointers.
  using const_succ_op_iterator = const_sb_succ_op_iterator<llvm::CondBrInst>;
  /// Return a range over the successor basic blocks.
  ///
  /// \return A range over the successor basic blocks.
  iterator_range<succ_op_iterator> successors() {
    return BrInstCommon::successors<llvm::CondBrInst>(Val, Ctx);
  }
  /// Return a range over the successor basic blocks.
  ///
  /// \return A range over the successor basic blocks.
  iterator_range<const_succ_op_iterator> successors() const {
    return BrInstCommon::successors<llvm::CondBrInst>(Val, Ctx);
  }

  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test.
  LLVM_ABI static bool classof(const Value *From);
};

/// An abstract class, parent of unary instructions.
class UnaryInstruction
    : public SingleLLVMInstructionImpl<llvm::UnaryInstruction> {
protected:
  /// Construct a SandboxIR unary instruction.
  /// \param ID The ID parameter.
  /// \param Opc Instruction opcode.
  /// \param LLVMI The LLVMI parameter.
  /// \param Ctx SandboxIR context.
  UnaryInstruction(ClassID ID, Opcode Opc, llvm::Instruction *LLVMI,
                   Context &Ctx)
      : SingleLLVMInstructionImpl(ID, Opc, LLVMI, Ctx) {}

public:
  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param I Instruction or value being inspected.
  static bool classof(const Instruction *I) {
    return isa<LoadInst>(I) || isa<CastInst>(I) || isa<FreezeInst>(I);
  }
  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param V Value or flag.
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

/// SandboxIR wrapper for an LLVM extractvalue instruction.
class ExtractValueInst : public UnaryInstruction {
  /// Use Context::createExtractValueInst() instead.
  /// \param EVI The EVI parameter.
  /// \param Ctx SandboxIR context.
  ExtractValueInst(llvm::ExtractValueInst *EVI, Context &Ctx)
      : UnaryInstruction(ClassID::ExtractValue, Opcode::ExtractValue, EVI,
                         Ctx) {}
  friend Context; // for ExtractValueInst()

public:
  /// Create and insert a new ExtractValueInst.
  ///
  /// \return The newly created instruction.
  /// \param Agg Aggregate operand.
  /// \param Idxs Index list.
  /// \param Pos Insert position for the new instruction.
  /// \param Ctx SandboxIR context.
  /// \param Name Name of the new instruction.
  LLVM_ABI static Value *create(Value *Agg, ArrayRef<unsigned> Idxs,
                                InsertPosition Pos, Context &Ctx,
                                const Twine &Name = "");

  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test for ExtractValueInst.
  static bool classof(const Value *From) {
    return From->getSubclassID() == ClassID::ExtractValue;
  }

  /// Returns the type of the element that would be extracted
  /// with an extractvalue instruction with the specified parameters.
  ///
  /// Null is returned if the indices are invalid for the specified type.
  ///
  /// \return The type of the element that would be extracted with an extractvalue instruction with the specified parameters.
  /// \param Agg Aggregate type being indexed.
  /// \param Idxs Index list into the aggregate.
  LLVM_ABI static Type *getIndexedType(Type *Agg, ArrayRef<unsigned> Idxs);

  /// Iterator over aggregate indices.
  using idx_iterator = llvm::ExtractValueInst::idx_iterator;

  /// Return an iterator to the first index.
  ///
  /// \return An iterator to the first index.
  inline idx_iterator idx_begin() const {
    return cast<llvm::ExtractValueInst>(Val)->idx_begin();
  }
  /// Return an iterator past the last index.
  ///
  /// \return An iterator past the last index.
  inline idx_iterator idx_end() const {
    return cast<llvm::ExtractValueInst>(Val)->idx_end();
  }
  /// Return a range over the indices.
  ///
  /// \return A range over the indices.
  inline iterator_range<idx_iterator> indices() const {
    return cast<llvm::ExtractValueInst>(Val)->indices();
  }

  /// Return the aggregate operand.
  ///
  /// \return The aggregate operand.
  Value *getAggregateOperand() {
    return getOperand(getAggregateOperandIndex());
  }
  /// Return the aggregate operand.
  ///
  /// \return The aggregate operand.
  const Value *getAggregateOperand() const {
    return getOperand(getAggregateOperandIndex());
  }
  /// Return the operand index of the aggregate.
  ///
  /// \return The operand index of the aggregate.
  static unsigned getAggregateOperandIndex() {
    return llvm::ExtractValueInst::getAggregateOperandIndex();
  }

  /// Return the index list.
  ///
  /// \return The index list.
  ArrayRef<unsigned> getIndices() const {
    return cast<llvm::ExtractValueInst>(Val)->getIndices();
  }

  /// Return the number of indices.
  ///
  /// \return The number of indices.
  unsigned getNumIndices() const {
    return cast<llvm::ExtractValueInst>(Val)->getNumIndices();
  }

  /// Return true if this instruction has indices.
  ///
  /// \return True if this instruction has indices.
  unsigned hasIndices() const {
    return cast<llvm::ExtractValueInst>(Val)->hasIndices();
  }
};

/// SandboxIR wrapper for an LLVM va_arg instruction.
class VAArgInst : public UnaryInstruction {
  /// VAArgInst.
  /// \param FI The FI parameter.
  /// \param Ctx SandboxIR context.
  VAArgInst(llvm::VAArgInst *FI, Context &Ctx)
      : UnaryInstruction(ClassID::VAArg, Opcode::VAArg, FI, Ctx) {}
  friend Context; // For constructor;

public:
  /// Create and insert a new VAArgInst.
  ///
  /// \return The newly created instruction.
  /// \param List va_list pointer.
  /// \param Ty Result or allocated type.
  /// \param Pos Insert position for the new instruction.
  /// \param Ctx SandboxIR context.
  /// \param Name Name of the new instruction.
  LLVM_ABI static VAArgInst *create(Value *List, Type *Ty, InsertPosition Pos,
                                    Context &Ctx, const Twine &Name = "");
  /// Return the pointer operand.
  ///
  /// \return The pointer operand.
  LLVM_ABI Value *getPointerOperand();
  /// Return the pointer operand.
  ///
  /// \return The pointer operand.
  const Value *getPointerOperand() const {
    return const_cast<VAArgInst *>(this)->getPointerOperand();
  }
  /// Return the operand index of the pointer.
  ///
  /// \return The operand index of the pointer.
  static unsigned getPointerOperandIndex() {
    return llvm::VAArgInst::getPointerOperandIndex();
  }
  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test for VAArgInst.
  static bool classof(const Value *From) {
    return From->getSubclassID() == ClassID::VAArg;
  }
};

/// SandboxIR wrapper for an LLVM freeze instruction.
class FreezeInst : public UnaryInstruction {
  /// FreezeInst.
  /// \param FI The FI parameter.
  /// \param Ctx SandboxIR context.
  FreezeInst(llvm::FreezeInst *FI, Context &Ctx)
      : UnaryInstruction(ClassID::Freeze, Opcode::Freeze, FI, Ctx) {}
  friend Context; // For constructor;

public:
  /// Create and insert a new FreezeInst.
  ///
  /// \return The newly created instruction.
  /// \param V Value operand.
  /// \param Pos Insert position for the new instruction.
  /// \param Ctx SandboxIR context.
  /// \param Name Name of the new instruction.
  LLVM_ABI static FreezeInst *create(Value *V, InsertPosition Pos, Context &Ctx,
                                     const Twine &Name = "");
  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test for FreezeInst.
  static bool classof(const Value *From) {
    return From->getSubclassID() == ClassID::Freeze;
  }
};

/// SandboxIR wrapper for an LLVM load instruction.
class LoadInst final : public UnaryInstruction {
  /// Use LoadInst::create() instead of calling the constructor.
  /// \param LI The LI parameter.
  /// \param Ctx SandboxIR context.
  LoadInst(llvm::LoadInst *LI, Context &Ctx)
      : UnaryInstruction(ClassID::Load, Opcode::Load, LI, Ctx) {}
  friend Context; // for LoadInst()

public:
  /// Return true if this is a load from a volatile memory location.
  ///
  /// \return True if this is a load from a volatile memory location.
  bool isVolatile() const { return cast<llvm::LoadInst>(Val)->isVolatile(); }
  /// Specify whether this is a volatile load or not.
  /// \param V True if the access is volatile.
  LLVM_ABI void setVolatile(bool V);

  /// Create and insert a new LoadInst.
  ///
  /// \return The newly created instruction.
  /// \param Ty Result or allocated type.
  /// \param Ptr Pointer operand.
  /// \param Align Required alignment.
  /// \param Pos Insert position for the new instruction.
  /// \param IsVolatile Whether the access is volatile.
  /// \param Ctx SandboxIR context.
  /// \param Name Name of the new instruction.
  LLVM_ABI static LoadInst *create(Type *Ty, Value *Ptr, MaybeAlign Align,
                                   InsertPosition Pos, bool IsVolatile,
                                   Context &Ctx, const Twine &Name = "");
  /// Create and insert a new LoadInst.
  ///
  /// \return The newly created instruction.
  /// \param Ty Result or allocated type.
  /// \param Ptr Pointer operand.
  /// \param Align Required alignment.
  /// \param Pos Insert position for the new instruction.
  /// \param Ctx SandboxIR context.
  /// \param Name Name of the new instruction.
  static LoadInst *create(Type *Ty, Value *Ptr, MaybeAlign Align,
                          InsertPosition Pos, Context &Ctx,
                          const Twine &Name = "") {
    return create(Ty, Ptr, Align, Pos, /*IsVolatile=*/false, Ctx, Name);
  }

  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test for LoadInst.
  LLVM_ABI static bool classof(const Value *From);
  /// Return the pointer operand.
  ///
  /// \return The pointer operand.
  LLVM_ABI Value *getPointerOperand() const;
  /// Return the type of the pointer operand.
  ///
  /// \return The type of the pointer operand.
  Type *getPointerOperandType() const { return getPointerOperand()->getType(); }
  /// Return the address space of the pointer operand.
  ///
  /// \return The address space of the pointer operand.
  unsigned getPointerAddressSpace() const {
    return getPointerOperandType()->getPointerAddressSpace();
  }
  /// Return the alignment of this memory access.
  ///
  /// \return The alignment of this memory access.
  Align getAlign() const { return cast<llvm::LoadInst>(Val)->getAlign(); }
  /// Return true if this load has unordered or non-atomic ordering.
  ///
  /// \return True if this load has unordered or non-atomic ordering.
  bool isUnordered() const { return cast<llvm::LoadInst>(Val)->isUnordered(); }
  /// Return true if this access is non-atomic and non-volatile.
  ///
  /// \return True if this access is non-atomic and non-volatile.
  bool isSimple() const { return cast<llvm::LoadInst>(Val)->isSimple(); }
};

/// SandboxIR wrapper for an LLVM store instruction.
class StoreInst final : public SingleLLVMInstructionImpl<llvm::StoreInst> {
  /// Use StoreInst::create(). Don't call the constructor directly.
  /// \param SI Underlying LLVM StoreInst to wrap.
  /// \param Ctx SandboxIR context.
  StoreInst(llvm::StoreInst *SI, Context &Ctx)
      : SingleLLVMInstructionImpl(ClassID::Store, Opcode::Store, SI, Ctx) {}
  friend Context; // for StoreInst()

public:
  /// Return true if this is a store from a volatile memory location.
  ///
  /// \return True if this is a store from a volatile memory location.
  bool isVolatile() const { return cast<llvm::StoreInst>(Val)->isVolatile(); }
  /// Specify whether this is a volatile store or not.
  /// \param V True if the access is volatile.
  LLVM_ABI void setVolatile(bool V);

  /// Create and insert a new StoreInst.
  ///
  /// \return The newly created instruction.
  /// \param V Value operand.
  /// \param Ptr Pointer operand.
  /// \param Align Required alignment.
  /// \param Pos Insert position for the new instruction.
  /// \param IsVolatile Whether the access is volatile.
  /// \param Ctx SandboxIR context.
  LLVM_ABI static StoreInst *create(Value *V, Value *Ptr, MaybeAlign Align,
                                    InsertPosition Pos, bool IsVolatile,
                                    Context &Ctx);
  /// Create and insert a new StoreInst.
  ///
  /// \return The newly created instruction.
  /// \param V Value operand.
  /// \param Ptr Pointer operand.
  /// \param Align Required alignment.
  /// \param Pos Insert position for the new instruction.
  /// \param Ctx SandboxIR context.
  static StoreInst *create(Value *V, Value *Ptr, MaybeAlign Align,
                           InsertPosition Pos, Context &Ctx) {
    return create(V, Ptr, Align, Pos, /*IsVolatile=*/false, Ctx);
  }

  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test.
  LLVM_ABI static bool classof(const Value *From);
  /// Return the value operand.
  ///
  /// \return The value operand.
  LLVM_ABI Value *getValueOperand() const;
  /// Return the pointer operand.
  ///
  /// \return The pointer operand.
  LLVM_ABI Value *getPointerOperand() const;
  /// Return the type of the pointer operand.
  ///
  /// \return The type of the pointer operand.
  Type *getPointerOperandType() const { return getPointerOperand()->getType(); }
  /// Return the address space of the pointer operand.
  ///
  /// \return The address space of the pointer operand.
  unsigned getPointerAddressSpace() const {
    return getPointerOperandType()->getPointerAddressSpace();
  }
  /// Return the alignment of this memory access.
  ///
  /// \return The alignment of this memory access.
  Align getAlign() const { return cast<llvm::StoreInst>(Val)->getAlign(); }
  /// Return true if this access is non-atomic and non-volatile.
  ///
  /// \return True if this access is non-atomic and non-volatile.
  bool isSimple() const { return cast<llvm::StoreInst>(Val)->isSimple(); }
  /// Return true if this store has unordered or non-atomic ordering.
  ///
  /// \return True if this store has unordered or non-atomic ordering.
  bool isUnordered() const { return cast<llvm::StoreInst>(Val)->isUnordered(); }
};

/// SandboxIR wrapper for an LLVM unreachable instruction.
class UnreachableInst final : public Instruction {
  /// Use UnreachableInst::create() instead of calling the constructor.
  /// \param I Instruction or value being inspected.
  /// \param Ctx SandboxIR context.
  UnreachableInst(llvm::UnreachableInst *I, Context &Ctx)
      : Instruction(ClassID::Unreachable, Opcode::Unreachable, I, Ctx) {}
  friend Context;
  /// Return the operand use internal.
  /// \param OpIdx Operand index.
  /// \param Verify The Verify parameter.
  Use getOperandUseInternal(unsigned OpIdx, bool Verify) const final {
    return getOperandUseDefault(OpIdx, Verify);
  }
  SmallVector<llvm::Instruction *, 1> getLLVMInstrs() const final {
    return {cast<llvm::Instruction>(Val)};
  }

public:
  /// Create and insert a new UnreachableInst.
  ///
  /// \return The newly created instruction.
  /// \param Pos Insert position for the new instruction.
  /// \param Ctx SandboxIR context.
  LLVM_ABI static UnreachableInst *create(InsertPosition Pos, Context &Ctx);
  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test for UnreachableInst.
  LLVM_ABI static bool classof(const Value *From);
  /// Return the number of successors.
  ///
  /// \return The number of successors.
  unsigned getNumSuccessors() const { return 0; }
  /// Return the operand index of a Use.
  ///
  /// \return The operand index of a Use.
  /// \param Use Operand use whose index is requested.
  unsigned getUseOperandNo(const Use &Use) const final {
    llvm_unreachable("UnreachableInst has no operands!");
  }
  /// Return how many LLVM IR instructions this maps to.
  ///
  /// \return How many LLVM IR instructions this maps to.
  unsigned getNumOfIRInstrs() const final { return 1u; }
};

/// SandboxIR wrapper for an LLVM return instruction.
class ReturnInst final : public SingleLLVMInstructionImpl<llvm::ReturnInst> {
  /// Use ReturnInst::create() instead of calling the constructor.
  /// \param I Instruction or value being inspected.
  /// \param Ctx SandboxIR context.
  ReturnInst(llvm::Instruction *I, Context &Ctx)
      : SingleLLVMInstructionImpl(ClassID::Ret, Opcode::Ret, I, Ctx) {}
  /// ReturnInst.
  /// \param SubclassID The SubclassID parameter.
  /// \param I Instruction or value being inspected.
  /// \param Ctx SandboxIR context.
  ReturnInst(ClassID SubclassID, llvm::Instruction *I, Context &Ctx)
      : SingleLLVMInstructionImpl(SubclassID, Opcode::Ret, I, Ctx) {}
  friend class Context; // For accessing the constructor in create*()
  static ReturnInst *createCommon(Value *RetVal, IRBuilder<> &Builder,
                                  Context &Ctx);

public:
  /// Create and insert a new ReturnInst.
  ///
  /// \return The newly created instruction.
  /// \param RetVal Returned value, or nullptr for void.
  /// \param Pos Insert position for the new instruction.
  /// \param Ctx SandboxIR context.
  LLVM_ABI static ReturnInst *create(Value *RetVal, InsertPosition Pos,
                                     Context &Ctx);
  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test for ReturnInst.
  static bool classof(const Value *From) {
    return From->getSubclassID() == ClassID::Ret;
  }
  /// Return the return value, or null if this is a void return.
  ///
  /// \return The return value, or null if this is a void return.
  LLVM_ABI Value *getReturnValue() const;
};

/// Base class for SandboxIR call-like instructions.
class CallBase : public SingleLLVMInstructionImpl<llvm::CallBase> {
  /// CallBase.
  /// \param ID The ID parameter.
  /// \param Opc Instruction opcode.
  /// \param I Instruction or value being inspected.
  /// \param Ctx SandboxIR context.
  CallBase(ClassID ID, Opcode Opc, llvm::Instruction *I, Context &Ctx)
      : SingleLLVMInstructionImpl(ID, Opc, I, Ctx) {}
  friend class CallInst;   // For constructor.
  friend class InvokeInst; // For constructor.
  friend class CallBrInst; // For constructor.

public:
  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test for CallBase.
  static bool classof(const Value *From) {
    auto Opc = From->getSubclassID();
    return Opc == Instruction::ClassID::Call ||
           Opc == Instruction::ClassID::Invoke ||
           Opc == Instruction::ClassID::CallBr;
  }

  /// Return the function type of the callee.
  ///
  /// \return The function type of the callee.
  LLVM_ABI FunctionType *getFunctionType() const;

  /// Return an iterator to the first data operand.
  ///
  /// \return An iterator to the first data operand.
  op_iterator data_operands_begin() { return op_begin(); }
  /// Return an iterator to the first data operand.
  ///
  /// \return An iterator to the first data operand.
  const_op_iterator data_operands_begin() const {
    return const_cast<CallBase *>(this)->data_operands_begin();
  }
  /// Return an iterator past the last data operand.
  ///
  /// \return An iterator past the last data operand.
  op_iterator data_operands_end() {
    auto *LLVMCB = cast<llvm::CallBase>(Val);
    auto Dist = LLVMCB->data_operands_end() - LLVMCB->data_operands_begin();
    return op_begin() + Dist;
  }
  /// Return an iterator past the last data operand.
  ///
  /// \return An iterator past the last data operand.
  const_op_iterator data_operands_end() const {
    auto *LLVMCB = cast<llvm::CallBase>(Val);
    auto Dist = LLVMCB->data_operands_end() - LLVMCB->data_operands_begin();
    return op_begin() + Dist;
  }
  /// Return a range over the data operands.
  ///
  /// \return A range over the data operands.
  iterator_range<op_iterator> data_ops() {
    return make_range(data_operands_begin(), data_operands_end());
  }
  /// Return a range over the data operands.
  ///
  /// \return A range over the data operands.
  iterator_range<const_op_iterator> data_ops() const {
    return make_range(data_operands_begin(), data_operands_end());
  }
  /// Return true if there are no data operands.
  ///
  /// \return True if there are no data operands.
  bool data_operands_empty() const {
    return data_operands_end() == data_operands_begin();
  }
  /// Return the number of data operands.
  ///
  /// \return The number of data operands.
  unsigned data_operands_size() const {
    return std::distance(data_operands_begin(), data_operands_end());
  }
  /// Return true if the Use is a data operand.
  ///
  /// \return True if the Use is a data operand.
  /// \param U Use being tested.
  bool isDataOperand(Use U) const {
    assert(this == U.getUser() &&
           "Only valid to query with a use of this instruction!");
    return cast<llvm::CallBase>(Val)->isDataOperand(U.LLVMUse);
  }
  /// Return the data-operand number for a Use.
  ///
  /// \return The data-operand number for a Use.
  /// \param U Use being tested.
  unsigned getDataOperandNo(Use U) const {
    assert(isDataOperand(U) && "Data operand # out of range!");
    return cast<llvm::CallBase>(Val)->getDataOperandNo(U.LLVMUse);
  }

  /// Return the total number operands (not operand bundles) used by
  /// every operand bundle in this OperandBundleUser.
  ///
  /// \return The total number operands (not operand bundles) used by every operand bundle in this OperandBundleUser.
  unsigned getNumTotalBundleOperands() const {
    return cast<llvm::CallBase>(Val)->getNumTotalBundleOperands();
  }

  /// Return an iterator to the first argument.
  ///
  /// \return An iterator to the first argument.
  op_iterator arg_begin() { return op_begin(); }
  /// Return an iterator to the first argument.
  ///
  /// \return An iterator to the first argument.
  const_op_iterator arg_begin() const { return op_begin(); }
  /// Return an iterator past the last argument.
  ///
  /// \return An iterator past the last argument.
  op_iterator arg_end() {
    return data_operands_end() - getNumTotalBundleOperands();
  }
  /// Return an iterator past the last argument.
  ///
  /// \return An iterator past the last argument.
  const_op_iterator arg_end() const {
    return const_cast<CallBase *>(this)->arg_end();
  }
  /// Return a range over the arguments.
  ///
  /// \return A range over the arguments.
  iterator_range<op_iterator> args() {
    return make_range(arg_begin(), arg_end());
  }
  /// Return a range over the arguments.
  ///
  /// \return A range over the arguments.
  iterator_range<const_op_iterator> args() const {
    return make_range(arg_begin(), arg_end());
  }
  /// Return true if there are no arguments.
  ///
  /// \return True if there are no arguments.
  bool arg_empty() const { return arg_end() == arg_begin(); }
  /// Return the number of arguments.
  ///
  /// \return The number of arguments.
  unsigned arg_size() const { return arg_end() - arg_begin(); }

  /// Return the argument operand at index \p OpIdx.
  ///
  /// \return The argument operand at index \p OpIdx.
  /// \param OpIdx Argument index.
  Value *getArgOperand(unsigned OpIdx) const {
    assert(OpIdx < arg_size() && "Out of bounds!");
    return getOperand(OpIdx);
  }
  /// Set an argument operand.
  /// \param OpIdx Operand index.
  /// \param NewOp The NewOp parameter.
  void setArgOperand(unsigned OpIdx, Value *NewOp) {
    assert(OpIdx < arg_size() && "Out of bounds!");
    setOperand(OpIdx, NewOp);
  }

  /// Return the Use of an argument operand.
  ///
  /// \return The Use of an argument operand.
  /// \param Idx Index.
  Use getArgOperandUse(unsigned Idx) const {
    assert(Idx < arg_size() && "Out of bounds!");
    return getOperandUse(Idx);
  }
  /// Return the Use of an argument operand.
  ///
  /// \return The Use of an argument operand.
  /// \param Idx Index.
  Use getArgOperandUse(unsigned Idx) {
    assert(Idx < arg_size() && "Out of bounds!");
    return getOperandUse(Idx);
  }

  /// Return true if the Use is an argument operand.
  ///
  /// \return True if the Use is an argument operand.
  /// \param U Use being tested.
  bool isArgOperand(Use U) const {
    return cast<llvm::CallBase>(Val)->isArgOperand(U.LLVMUse);
  }
  /// Return the argument number for a Use.
  ///
  /// \return The argument number for a Use.
  /// \param U Use being tested.
  unsigned getArgOperandNo(Use U) const {
    return cast<llvm::CallBase>(Val)->getArgOperandNo(U.LLVMUse);
  }
  /// Return true if the given value is used as an argument.
  ///
  /// \return True if the given value is used as an argument.
  /// \param V Value or flag.
  bool hasArgument(const Value *V) const { return is_contained(args(), V); }

  /// Return the called operand.
  ///
  /// \return The called operand.
  LLVM_ABI Value *getCalledOperand() const;
  /// Return the Use of the called operand.
  ///
  /// \return The Use of the called operand.
  LLVM_ABI Use getCalledOperandUse() const;

  /// Return the called function, or nullptr if this is an indirect call.
  ///
  /// \return The called function, or nullptr if this is an indirect call.
  LLVM_ABI Function *getCalledFunction() const;
  /// Return true if this is an indirect call.
  ///
  /// \return True if this is an indirect call.
  bool isIndirectCall() const {
    return cast<llvm::CallBase>(Val)->isIndirectCall();
  }
  /// Return true if the Use is the callee operand.
  ///
  /// \return True if the Use is the callee operand.
  /// \param U Use being tested.
  bool isCallee(Use U) const {
    return cast<llvm::CallBase>(Val)->isCallee(U.LLVMUse);
  }
  /// Return the function that contains this call.
  ///
  /// \return The function that contains this call.
  LLVM_ABI Function *getCaller();
  /// Return the function that contains this call.
  ///
  /// \return The function that contains this call.
  const Function *getCaller() const {
    return const_cast<CallBase *>(this)->getCaller();
  }
  /// Return true if this call is marked as a musttail call.
  ///
  /// \return True if this call is marked as a musttail call.
  bool isMustTailCall() const {
    return cast<llvm::CallBase>(Val)->isMustTailCall();
  }
  /// Return true if this call is marked as a tail call.
  ///
  /// \return True if this call is marked as a tail call.
  bool isTailCall() const { return cast<llvm::CallBase>(Val)->isTailCall(); }
  /// Return the intrinsic ID if this call is an intrinsic.
  ///
  /// \return The intrinsic ID if this call is an intrinsic.
  Intrinsic::ID getIntrinsicID() const {
    return cast<llvm::CallBase>(Val)->getIntrinsicID();
  }
  /// Set the called operand.
  /// \param V Value or flag.
  void setCalledOperand(Value *V) { getCalledOperandUse().set(V); }
  /// Set the called function.
  /// \param F The F parameter.
  LLVM_ABI void setCalledFunction(Function *F);
  /// Return the calling convention.
  ///
  /// \return The calling convention.
  CallingConv::ID getCallingConv() const {
    return cast<llvm::CallBase>(Val)->getCallingConv();
  }
  /// Return true if the callee is inline assembly.
  ///
  /// \return True if the callee is inline assembly.
  bool isInlineAsm() const { return cast<llvm::CallBase>(Val)->isInlineAsm(); }
};

/// SandboxIR wrapper for an LLVM call instruction.
class CallInst : public CallBase {
  /// Use Context::createCallInst(). Don't call the
  /// constructor directly.
  /// \param I Instruction or value being inspected.
  /// \param Ctx SandboxIR context.
  CallInst(llvm::Instruction *I, Context &Ctx)
      : CallBase(ClassID::Call, Opcode::Call, I, Ctx) {}
  friend class Context;       // For accessing the constructor in create*()
  friend class IntrinsicInst; // For constructor

public:
  /// Create and insert a new CallInst.
  ///
  /// \return The newly created instruction.
  /// \param FTy Function type of the callee.
  /// \param Func Called function or value.
  /// \param Args Call arguments.
  /// \param Pos Insert position for the new instruction.
  /// \param Ctx SandboxIR context.
  /// \param NameStr The NameStr parameter.
  LLVM_ABI static CallInst *create(FunctionType *FTy, Value *Func,
                                   ArrayRef<Value *> Args, InsertPosition Pos,
                                   Context &Ctx, const Twine &NameStr = "");

  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test for CallInst.
  static bool classof(const Value *From) {
    return From->getSubclassID() == ClassID::Call;
  }
};

/// SandboxIR wrapper for an LLVM invoke instruction.
class InvokeInst final : public CallBase {
  /// Use Context::createInvokeInst(). Don't call the
  /// constructor directly.
  /// \param I Instruction or value being inspected.
  /// \param Ctx SandboxIR context.
  InvokeInst(llvm::Instruction *I, Context &Ctx)
      : CallBase(ClassID::Invoke, Opcode::Invoke, I, Ctx) {}
  friend class Context; // For accessing the constructor in
                        // create*()

public:
  /// Create and insert a new InvokeInst.
  ///
  /// \return The newly created instruction.
  /// \param FTy Function type of the callee.
  /// \param Func Called function or value.
  /// \param IfNormal The IfNormal parameter.
  /// \param IfException The IfException parameter.
  /// \param Args Call arguments.
  /// \param Pos Insert position for the new instruction.
  /// \param Ctx SandboxIR context.
  /// \param NameStr The NameStr parameter.
  LLVM_ABI static InvokeInst *create(FunctionType *FTy, Value *Func,
                                     BasicBlock *IfNormal,
                                     BasicBlock *IfException,
                                     ArrayRef<Value *> Args, InsertPosition Pos,
                                     Context &Ctx, const Twine &NameStr = "");

  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test for InvokeInst.
  static bool classof(const Value *From) {
    return From->getSubclassID() == ClassID::Invoke;
  }
  /// Return the normal destination basic block.
  ///
  /// \return The normal destination basic block.
  LLVM_ABI BasicBlock *getNormalDest() const;
  /// Return the unwind destination basic block.
  ///
  /// \return The unwind destination basic block.
  LLVM_ABI BasicBlock *getUnwindDest() const;
  /// Set the normal destination basic block.
  /// \param BB Basic block.
  LLVM_ABI void setNormalDest(BasicBlock *BB);
  /// Set the unwind destination basic block.
  /// \param BB Basic block.
  LLVM_ABI void setUnwindDest(BasicBlock *BB);
  /// Return the landingpad instruction in the unwind destination.
  ///
  /// \return The landingpad instruction in the unwind destination.
  LLVM_ABI LandingPadInst *getLandingPadInst() const;
  /// Return the successor basic block at index \p SuccIdx.
  ///
  /// \return The successor basic block at index \p SuccIdx.
  /// \param SuccIdx Successor index (0 = normal, 1 = unwind).
  LLVM_ABI BasicBlock *getSuccessor(unsigned SuccIdx) const;
  /// Set the successor at the given index.
  /// \param SuccIdx Index SuccIdx.
  /// \param NewSucc The NewSucc parameter.
  void setSuccessor(unsigned SuccIdx, BasicBlock *NewSucc) {
    assert(SuccIdx < 2 && "Successor # out of range for invoke!");
    if (SuccIdx == 0)
      setNormalDest(NewSucc);
    else
      setUnwindDest(NewSucc);
  }
  /// Return the number of successors.
  ///
  /// \return The number of successors.
  unsigned getNumSuccessors() const {
    return cast<llvm::InvokeInst>(Val)->getNumSuccessors();
  }
};

/// SandboxIR wrapper for an LLVM callbr instruction.
class CallBrInst final : public CallBase {
  /// Use Context::createCallBrInst(). Don't call the
  /// constructor directly.
  /// \param I Instruction or value being inspected.
  /// \param Ctx SandboxIR context.
  CallBrInst(llvm::Instruction *I, Context &Ctx)
      : CallBase(ClassID::CallBr, Opcode::CallBr, I, Ctx) {}
  friend class Context; // For accessing the constructor in
                        // create*()

public:
  /// Create and insert a new CallBrInst.
  ///
  /// \return The newly created instruction.
  /// \param FTy Function type of the callee.
  /// \param Func Called function or value.
  /// \param DefaultDest Default destination basic block.
  /// \param IndirectDests Indirect destination basic blocks.
  /// \param Args Call arguments.
  /// \param Pos Insert position for the new instruction.
  /// \param Ctx SandboxIR context.
  /// \param NameStr The NameStr parameter.
  LLVM_ABI static CallBrInst *create(FunctionType *FTy, Value *Func,
                                     BasicBlock *DefaultDest,
                                     ArrayRef<BasicBlock *> IndirectDests,
                                     ArrayRef<Value *> Args, InsertPosition Pos,
                                     Context &Ctx, const Twine &NameStr = "");
  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test for CallBrInst.
  static bool classof(const Value *From) {
    return From->getSubclassID() == ClassID::CallBr;
  }
  /// Return the number of indirect destinations.
  ///
  /// \return The number of indirect destinations.
  unsigned getNumIndirectDests() const {
    return cast<llvm::CallBrInst>(Val)->getNumIndirectDests();
  }
  /// Return the label for indirect destination \p Idx.
  ///
  /// \return The label for indirect destination \p Idx.
  /// \param Idx Indirect destination index.
  LLVM_ABI Value *getIndirectDestLabel(unsigned Idx) const;
  /// Return the Use of the label for indirect destination \p Idx.
  ///
  /// \return The Use of the label for indirect destination \p Idx.
  /// \param Idx Indirect destination index.
  LLVM_ABI Value *getIndirectDestLabelUse(unsigned Idx) const;
  /// Return the default destination basic block.
  ///
  /// \return The default destination basic block.
  LLVM_ABI BasicBlock *getDefaultDest() const;
  /// Return the indirect destination basic block at index \p Idx.
  ///
  /// \return The indirect destination basic block at index \p Idx.
  /// \param Idx Indirect destination index.
  LLVM_ABI BasicBlock *getIndirectDest(unsigned Idx) const;
  /// Return all indirect destination basic blocks.
  ///
  /// \return All indirect destination basic blocks.
  LLVM_ABI SmallVector<BasicBlock *, 16> getIndirectDests() const;
  /// Set the default destination basic block.
  /// \param BB Basic block.
  LLVM_ABI void setDefaultDest(BasicBlock *BB);
  /// Set an indirect destination basic block.
  /// \param Idx Index.
  /// \param BB Basic block.
  LLVM_ABI void setIndirectDest(unsigned Idx, BasicBlock *BB);
  /// Return the successor basic block at index \p Idx.
  ///
  /// \return The successor basic block at index \p Idx.
  /// \param Idx Successor index.
  LLVM_ABI BasicBlock *getSuccessor(unsigned Idx) const;
  /// Return the number of successors.
  ///
  /// \return The number of successors.
  unsigned getNumSuccessors() const {
    return cast<llvm::CallBrInst>(Val)->getNumSuccessors();
  }
};

/// SandboxIR wrapper for an LLVM landingpad instruction.
class LandingPadInst : public SingleLLVMInstructionImpl<llvm::LandingPadInst> {
  /// LandingPadInst.
  /// \param LP The LP parameter.
  /// \param Ctx SandboxIR context.
  LandingPadInst(llvm::LandingPadInst *LP, Context &Ctx)
      : SingleLLVMInstructionImpl(ClassID::LandingPad, Opcode::LandingPad, LP,
                                  Ctx) {}
  friend class Context; // For constructor.

public:
  /// Create and insert a new LandingPadInst.
  ///
  /// \return The newly created instruction.
  /// \param RetTy Landingpad result type.
  /// \param NumReservedClauses Number of landingpad clauses to reserve.
  /// \param Pos Insert position for the new instruction.
  /// \param Ctx SandboxIR context.
  /// \param Name Name of the new instruction.
  LLVM_ABI static LandingPadInst *create(Type *RetTy,
                                         unsigned NumReservedClauses,
                                         InsertPosition Pos, Context &Ctx,
                                         const Twine &Name = "");
  /// Return 'true' if this landingpad instruction is a
  /// cleanup. I.e., it should be run when unwinding even if its landing pad
  /// doesn't catch the exception.
  ///
  /// \return 'true' if this landingpad instruction is a cleanup. I.e., it should be run when unwinding even if its landing pad doesn't catch the exception.
  bool isCleanup() const {
    return cast<llvm::LandingPadInst>(Val)->isCleanup();
  }
  /// Indicate that this landingpad instruction is a cleanup.
  /// \param V Value or flag.
  LLVM_ABI void setCleanup(bool V);

  // TODO: We are not implementing addClause() because we have no way to revert
  // it for now.

  /// Get the value of the clause at index Idx. Use isCatch/isFilter to
  /// determine what type of clause this is.
  ///
  /// \return The clause value at index \p Idx.
  /// \param Idx Clause index.
  LLVM_ABI Constant *getClause(unsigned Idx) const;

  /// Return 'true' if the clause and index Idx is a catch clause.
  ///
  /// \return 'true' if the clause and index Idx is a catch clause.
  /// \param Idx Index.
  bool isCatch(unsigned Idx) const {
    return cast<llvm::LandingPadInst>(Val)->isCatch(Idx);
  }
  /// Return 'true' if the clause and index Idx is a filter clause.
  ///
  /// \return 'true' if the clause and index Idx is a filter clause.
  /// \param Idx Index.
  bool isFilter(unsigned Idx) const {
    return cast<llvm::LandingPadInst>(Val)->isFilter(Idx);
  }
  /// Get the number of clauses for this landing pad.
  ///
  /// \return The requested integer value.
  unsigned getNumClauses() const {
    return cast<llvm::LandingPadInst>(Val)->getNumOperands();
  }
  // TODO: We are not implementing reserveClauses() because we can't revert it.
  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test for LandingPadInst.
  static bool classof(const Value *From) {
    return From->getSubclassID() == ClassID::LandingPad;
  }
};

/// Base class for SandboxIR catchpad and cleanuppad instructions.
class FuncletPadInst : public SingleLLVMInstructionImpl<llvm::FuncletPadInst> {
  /// FuncletPadInst.
  /// \param SubclassID The SubclassID parameter.
  /// \param Opc Instruction opcode.
  /// \param I Instruction or value being inspected.
  /// \param Ctx SandboxIR context.
  FuncletPadInst(ClassID SubclassID, Opcode Opc, llvm::Instruction *I,
                 Context &Ctx)
      : SingleLLVMInstructionImpl(SubclassID, Opc, I, Ctx) {}
  friend class CatchPadInst;   // For constructor.
  friend class CleanupPadInst; // For constructor.

public:
  /// Return the number of funcletpad arguments.
  ///
  /// \return The number of funcletpad arguments.
  unsigned arg_size() const {
    return cast<llvm::FuncletPadInst>(Val)->arg_size();
  }
  /// Return the outer EH-pad this funclet is nested within.
  ///
  /// Note: This returns the associated CatchSwitchInst if this FuncletPadInst
  /// is a CatchPadInst.
  ///
  /// \return The outer EH-pad this funclet is nested within.
  LLVM_ABI Value *getParentPad() const;
  /// Set the parent pad token.
  /// \param ParentPad Parent pad token.
  LLVM_ABI void setParentPad(Value *ParentPad);
  /// Return the Idx-th funcletpad argument.
  ///
  /// \return The Idx-th funcletpad argument.
  /// \param Idx Argument index.
  LLVM_ABI Value *getArgOperand(unsigned Idx) const;
  /// Set the Idx-th funcletpad argument.
  /// \param Idx Index.
  /// \param V Value or flag.
  LLVM_ABI void setArgOperand(unsigned Idx, Value *V);

  // TODO: Implement missing functions: arg_operands().
  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test for FuncletPadInst.
  static bool classof(const Value *From) {
    return From->getSubclassID() == ClassID::CatchPad ||
           From->getSubclassID() == ClassID::CleanupPad;
  }
};

/// SandboxIR wrapper for an LLVM catchpad instruction.
class CatchPadInst : public FuncletPadInst {
  /// CatchPadInst.
  /// \param CPI The CPI parameter.
  /// \param Ctx SandboxIR context.
  CatchPadInst(llvm::CatchPadInst *CPI, Context &Ctx)
      : FuncletPadInst(ClassID::CatchPad, Opcode::CatchPad, CPI, Ctx) {}
  friend class Context; // For constructor.

public:
  /// Return the catchswitch that owns this catchpad.
  ///
  /// \return The catchswitch that owns this catchpad.
  LLVM_ABI CatchSwitchInst *getCatchSwitch() const;
  // TODO: We have not implemented setCatchSwitch() because we can't revert it
  // for now, as there is no CatchPadInst member function that can undo it.

  /// Create and insert a new CatchPadInst.
  ///
  /// \return The newly created instruction.
  /// \param ParentPad Parent pad token.
  /// \param Args Call arguments.
  /// \param Pos Insert position for the new instruction.
  /// \param Ctx SandboxIR context.
  /// \param Name Name of the new instruction.
  LLVM_ABI static CatchPadInst *create(Value *ParentPad, ArrayRef<Value *> Args,
                                       InsertPosition Pos, Context &Ctx,
                                       const Twine &Name = "");
  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test for CatchPadInst.
  static bool classof(const Value *From) {
    return From->getSubclassID() == ClassID::CatchPad;
  }
};

/// SandboxIR wrapper for an LLVM cleanuppad instruction.
class CleanupPadInst : public FuncletPadInst {
  /// CleanupPadInst.
  /// \param CPI The CPI parameter.
  /// \param Ctx SandboxIR context.
  CleanupPadInst(llvm::CleanupPadInst *CPI, Context &Ctx)
      : FuncletPadInst(ClassID::CleanupPad, Opcode::CleanupPad, CPI, Ctx) {}
  friend class Context; // For constructor.

public:
  /// Create and insert a new CleanupPadInst.
  ///
  /// \return The newly created instruction.
  /// \param ParentPad Parent pad token.
  /// \param Args Call arguments.
  /// \param Pos Insert position for the new instruction.
  /// \param Ctx SandboxIR context.
  /// \param Name Name of the new instruction.
  LLVM_ABI static CleanupPadInst *create(Value *ParentPad,
                                         ArrayRef<Value *> Args,
                                         InsertPosition Pos, Context &Ctx,
                                         const Twine &Name = "");
  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test for CleanupPadInst.
  static bool classof(const Value *From) {
    return From->getSubclassID() == ClassID::CleanupPad;
  }
};

/// SandboxIR wrapper for an LLVM catchret instruction.
class CatchReturnInst
    : public SingleLLVMInstructionImpl<llvm::CatchReturnInst> {
  /// CatchReturnInst.
  /// \param CRI The CRI parameter.
  /// \param Ctx SandboxIR context.
  CatchReturnInst(llvm::CatchReturnInst *CRI, Context &Ctx)
      : SingleLLVMInstructionImpl(ClassID::CatchRet, Opcode::CatchRet, CRI,
                                  Ctx) {}
  friend class Context; // For constructor.

public:
  /// Create and insert a new CatchReturnInst.
  ///
  /// \return The newly created instruction.
  /// \param CatchPad Catch pad value.
  /// \param BB Successor basic block.
  /// \param Pos Insert position for the new instruction.
  /// \param Ctx SandboxIR context.
  LLVM_ABI static CatchReturnInst *create(CatchPadInst *CatchPad,
                                          BasicBlock *BB, InsertPosition Pos,
                                          Context &Ctx);
  /// Return the catch pad operand.
  ///
  /// \return The catch pad operand.
  LLVM_ABI CatchPadInst *getCatchPad() const;
  /// Set the catch pad operand.
  /// \param CatchPad Catch pad value.
  LLVM_ABI void setCatchPad(CatchPadInst *CatchPad);
  /// Return the successor basic block.
  ///
  /// \return The successor basic block.
  LLVM_ABI BasicBlock *getSuccessor() const;
  /// Set the successor at the given index.
  /// \param NewSucc The NewSucc parameter.
  LLVM_ABI void setSuccessor(BasicBlock *NewSucc);
  /// Return the number of successors.
  ///
  /// \return The number of successors.
  unsigned getNumSuccessors() {
    return cast<llvm::CatchReturnInst>(Val)->getNumSuccessors();
  }
  /// Return the parent pad of the associated catchswitch.
  ///
  /// \return The parent pad of the associated catchswitch.
  LLVM_ABI Value *getCatchSwitchParentPad() const;
  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test for CatchReturnInst.
  static bool classof(const Value *From) {
    return From->getSubclassID() == ClassID::CatchRet;
  }
};

/// SandboxIR wrapper for an LLVM cleanupret instruction.
class CleanupReturnInst
    : public SingleLLVMInstructionImpl<llvm::CleanupReturnInst> {
  /// CleanupReturnInst.
  /// \param CRI The CRI parameter.
  /// \param Ctx SandboxIR context.
  CleanupReturnInst(llvm::CleanupReturnInst *CRI, Context &Ctx)
      : SingleLLVMInstructionImpl(ClassID::CleanupRet, Opcode::CleanupRet, CRI,
                                  Ctx) {}
  friend class Context; // For constructor.

public:
  /// Create and insert a new CleanupReturnInst.
  ///
  /// \return The newly created instruction.
  /// \param CleanupPad Cleanup pad value.
  /// \param UnwindBB The UnwindBB parameter.
  /// \param Pos Insert position for the new instruction.
  /// \param Ctx SandboxIR context.
  LLVM_ABI static CleanupReturnInst *create(CleanupPadInst *CleanupPad,
                                            BasicBlock *UnwindBB,
                                            InsertPosition Pos, Context &Ctx);
  /// Return true if this instruction has an unwind destination.
  ///
  /// \return True if this instruction has an unwind destination.
  bool hasUnwindDest() const {
    return cast<llvm::CleanupReturnInst>(Val)->hasUnwindDest();
  }
  /// Return true if unwinding returns to the caller.
  ///
  /// \return True if unwinding returns to the caller.
  bool unwindsToCaller() const {
    return cast<llvm::CleanupReturnInst>(Val)->unwindsToCaller();
  }
  /// Return the cleanup pad operand.
  ///
  /// \return The cleanup pad operand.
  LLVM_ABI CleanupPadInst *getCleanupPad() const;
  /// Set the cleanup pad operand.
  /// \param CleanupPad Cleanup pad value.
  LLVM_ABI void setCleanupPad(CleanupPadInst *CleanupPad);
  /// Return the number of successors.
  ///
  /// \return The number of successors.
  unsigned getNumSuccessors() const {
    return cast<llvm::CleanupReturnInst>(Val)->getNumSuccessors();
  }
  /// Return the unwind destination basic block.
  ///
  /// \return The unwind destination basic block, or nullptr if none.
  LLVM_ABI BasicBlock *getUnwindDest() const;
  /// Set the unwind destination basic block.
  /// \param NewDest The NewDest parameter.
  LLVM_ABI void setUnwindDest(BasicBlock *NewDest);

  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test for CleanupReturnInst.
  static bool classof(const Value *From) {
    return From->getSubclassID() == ClassID::CleanupRet;
  }
};

/// SandboxIR wrapper for an LLVM getelementptr instruction.
class GetElementPtrInst final
    : public SingleLLVMInstructionImpl<llvm::GetElementPtrInst> {
  /// Use Context::createGetElementPtrInst(). Don't call
  /// the constructor directly.
  /// \param I Instruction or value being inspected.
  /// \param Ctx SandboxIR context.
  GetElementPtrInst(llvm::Instruction *I, Context &Ctx)
      : SingleLLVMInstructionImpl(ClassID::GetElementPtr, Opcode::GetElementPtr,
                                  I, Ctx) {}
  /// GetElementPtrInst.
  /// \param SubclassID The SubclassID parameter.
  /// \param I Instruction or value being inspected.
  /// \param Ctx SandboxIR context.
  GetElementPtrInst(ClassID SubclassID, llvm::Instruction *I, Context &Ctx)
      : SingleLLVMInstructionImpl(SubclassID, Opcode::GetElementPtr, I, Ctx) {}
  friend class Context; // For accessing the constructor in
                        // create*()

public:
  /// Create and insert a new GetElementPtrInst.
  ///
  /// \return The newly created instruction.
  /// \param Ty Result or allocated type.
  /// \param Ptr Pointer operand.
  /// \param IdxList GEP index list.
  /// \param Pos Insert position for the new instruction.
  /// \param Ctx SandboxIR context.
  /// \param NameStr The NameStr parameter.
  LLVM_ABI static Value *create(Type *Ty, Value *Ptr, ArrayRef<Value *> IdxList,
                                InsertPosition Pos, Context &Ctx,
                                const Twine &NameStr = "");

  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test for GetElementPtrInst.
  static bool classof(const Value *From) {
    return From->getSubclassID() == ClassID::GetElementPtr;
  }

  /// Return the source element type of this GEP.
  ///
  /// \return The source element type of this GEP.
  LLVM_ABI Type *getSourceElementType() const;
  /// Return the result element type of this GEP.
  ///
  /// \return The result element type of this GEP.
  LLVM_ABI Type *getResultElementType() const;
  /// Return the address space of the result pointer.
  ///
  /// \return The address space of the result pointer.
  unsigned getAddressSpace() const {
    return cast<llvm::GetElementPtrInst>(Val)->getAddressSpace();
  }

  /// Return an iterator to the first index.
  ///
  /// \return An iterator to the first index.
  inline op_iterator idx_begin() { return op_begin() + 1; }
  /// Return an iterator to the first index.
  ///
  /// \return An iterator to the first index.
  inline const_op_iterator idx_begin() const {
    return const_cast<GetElementPtrInst *>(this)->idx_begin();
  }
  /// Return an iterator past the last index.
  ///
  /// \return An iterator past the last index.
  inline op_iterator idx_end() { return op_end(); }
  /// Return an iterator past the last index.
  ///
  /// \return An iterator past the last index.
  inline const_op_iterator idx_end() const {
    return const_cast<GetElementPtrInst *>(this)->idx_end();
  }
  /// Return a range over the indices.
  ///
  /// \return A range over the indices.
  inline iterator_range<op_iterator> indices() {
    return make_range(idx_begin(), idx_end());
  }
  /// Return a range over the indices.
  ///
  /// \return A range over the indices.
  inline iterator_range<const_op_iterator> indices() const {
    return const_cast<GetElementPtrInst *>(this)->indices();
  }

  /// Return the pointer operand.
  ///
  /// \return The pointer operand.
  LLVM_ABI Value *getPointerOperand() const;
  /// Return the operand index of the pointer.
  ///
  /// \return The operand index of the pointer.
  static unsigned getPointerOperandIndex() {
    return llvm::GetElementPtrInst::getPointerOperandIndex();
  }
  /// Return the type of the pointer operand.
  ///
  /// \return The type of the pointer operand.
  LLVM_ABI Type *getPointerOperandType() const;
  /// Return the address space of the pointer operand.
  ///
  /// \return The address space of the pointer operand.
  unsigned getPointerAddressSpace() const {
    return cast<llvm::GetElementPtrInst>(Val)->getPointerAddressSpace();
  }
  /// Return the number of indices.
  ///
  /// \return The number of indices.
  unsigned getNumIndices() const {
    return cast<llvm::GetElementPtrInst>(Val)->getNumIndices();
  }
  /// Return true if this instruction has indices.
  ///
  /// \return True if this instruction has indices.
  bool hasIndices() const {
    return cast<llvm::GetElementPtrInst>(Val)->hasIndices();
  }
  /// Return true if every index is a constant integer.
  ///
  /// \return True if every index is a constant integer.
  bool hasAllConstantIndices() const {
    return cast<llvm::GetElementPtrInst>(Val)->hasAllConstantIndices();
  }
  /// Return the GEP no-wrap flags.
  ///
  /// \return The GEP no-wrap flags.
  GEPNoWrapFlags getNoWrapFlags() const {
    return cast<llvm::GetElementPtrInst>(Val)->getNoWrapFlags();
  }
  /// Return true if this GEP is marked inbounds.
  ///
  /// \return True if this GEP is marked inbounds.
  bool isInBounds() const {
    return cast<llvm::GetElementPtrInst>(Val)->isInBounds();
  }
  /// Return true if the nusw flag is set.
  ///
  /// \return True if the nusw flag is set.
  bool hasNoUnsignedSignedWrap() const {
    return cast<llvm::GetElementPtrInst>(Val)->hasNoUnsignedSignedWrap();
  }
  /// Return true if the nuw flag is set.
  ///
  /// \return True if the nuw flag is set.
  bool hasNoUnsignedWrap() const {
    return cast<llvm::GetElementPtrInst>(Val)->hasNoUnsignedWrap();
  }
  /// Accumulate a constant GEP offset into APInt.
  ///
  /// \return True if the condition described by this query holds.
  /// \param DL Data layout used for size computation.
  /// \param Offset The Offset parameter.
  bool accumulateConstantOffset(const DataLayout &DL, APInt &Offset) const {
    return cast<llvm::GetElementPtrInst>(Val)->accumulateConstantOffset(DL,
                                                                        Offset);
  }
  // TODO: Add missing member functions.
};

/// SandboxIR wrapper for an LLVM catchswitch instruction.
class CatchSwitchInst
    : public SingleLLVMInstructionImpl<llvm::CatchSwitchInst> {
  /// CatchSwitchInst.
  /// \param CSI The CSI parameter.
  /// \param Ctx SandboxIR context.
  CatchSwitchInst(llvm::CatchSwitchInst *CSI, Context &Ctx)
      : SingleLLVMInstructionImpl(ClassID::CatchSwitch, Opcode::CatchSwitch,
                                  CSI, Ctx) {}
  friend class Context; // For accessing the constructor in create*()

public:
  LLVM_ABI static CatchSwitchInst *
  /// Create and insert a new CatchSwitchInst.
  ///
  /// \return The newly created instruction.
  /// \param ParentPad Parent pad token.
  /// \param UnwindBB Basic block UnwindBB.
  /// \param NumHandlers Expected number of catch handlers.
  /// \param Pos Insert position for the new instruction.
  /// \param Ctx SandboxIR context.
  /// \param Name Name of the new instruction.
  create(Value *ParentPad, BasicBlock *UnwindBB, unsigned NumHandlers,
         InsertPosition Pos, Context &Ctx, const Twine &Name = "");

  /// Return the parent pad token.
  ///
  /// \return The parent pad token.
  LLVM_ABI Value *getParentPad() const;
  /// Set the parent pad token.
  /// \param ParentPad Parent pad token.
  LLVM_ABI void setParentPad(Value *ParentPad);

  /// Return true if this instruction has an unwind destination.
  ///
  /// \return True if this instruction has an unwind destination.
  bool hasUnwindDest() const {
    return cast<llvm::CatchSwitchInst>(Val)->hasUnwindDest();
  }
  /// Return true if unwinding returns to the caller.
  ///
  /// \return True if unwinding returns to the caller.
  bool unwindsToCaller() const {
    return cast<llvm::CatchSwitchInst>(Val)->unwindsToCaller();
  }
  /// Return the unwind destination basic block.
  ///
  /// \return The unwind destination basic block, or nullptr if none.
  LLVM_ABI BasicBlock *getUnwindDest() const;
  /// Set the unwind destination basic block.
  /// \param UnwindDest Unwind destination basic block.
  LLVM_ABI void setUnwindDest(BasicBlock *UnwindDest);

  /// Return the number of catch handlers.
  ///
  /// \return The number of catch handlers.
  unsigned getNumHandlers() const {
    return cast<llvm::CatchSwitchInst>(Val)->getNumHandlers();
  }

private:
  static BasicBlock *handler_helper(Value *V) { return cast<BasicBlock>(V); }
  static const BasicBlock *handler_helper(const Value *V) {
    return cast<BasicBlock>(V);
  }

public:
  /// Function type used to map handler operands to BasicBlock pointers.
  using DerefFnTy = BasicBlock *(*)(Value *);
  /// Iterator over catchswitch handlers.
  using handler_iterator = mapped_iterator<op_iterator, DerefFnTy>;
  /// Range of catchswitch handlers.
  using handler_range = iterator_range<handler_iterator>;
  /// Function type used to map handler operands to const BasicBlock pointers.
  using ConstDerefFnTy = const BasicBlock *(*)(const Value *);
  /// Const iterator over catchswitch handlers.
  using const_handler_iterator =
      mapped_iterator<const_op_iterator, ConstDerefFnTy>;
  /// Const range of catchswitch handlers.
  using const_handler_range = iterator_range<const_handler_iterator>;

  /// Return an iterator to the first catch handler.
  ///
  /// \return An iterator to the first catch handler.
  handler_iterator handler_begin() {
    op_iterator It = op_begin() + 1;
    if (hasUnwindDest())
      ++It;
    return handler_iterator(It, DerefFnTy(handler_helper));
  }
  /// Return an iterator to the first catch handler.
  ///
  /// \return An iterator to the first catch handler.
  const_handler_iterator handler_begin() const {
    const_op_iterator It = op_begin() + 1;
    if (hasUnwindDest())
      ++It;
    return const_handler_iterator(It, ConstDerefFnTy(handler_helper));
  }
  /// Return an iterator past the last catch handler.
  ///
  /// \return An iterator past the last catch handler.
  handler_iterator handler_end() {
    return handler_iterator(op_end(), DerefFnTy(handler_helper));
  }
  /// Return an iterator past the last catch handler.
  ///
  /// \return An iterator past the last catch handler.
  const_handler_iterator handler_end() const {
    return const_handler_iterator(op_end(), ConstDerefFnTy(handler_helper));
  }
  /// Return a range over the catch handlers.
  ///
  /// \return A range over the catch handlers.
  handler_range handlers() {
    return make_range(handler_begin(), handler_end());
  }
  /// Return a range over the catch handlers.
  ///
  /// \return A range over the catch handlers.
  const_handler_range handlers() const {
    return make_range(handler_begin(), handler_end());
  }

  /// Add a catch handler basic block.
  /// \param Dest Destination basic block.
  LLVM_ABI void addHandler(BasicBlock *Dest);

  // TODO: removeHandler() cannot be reverted because there is no equivalent
  // addHandler() with a handler_iterator to specify the position. So we can't
  // implement it for now.

  /// Return the number of successors.
  ///
  /// \return The number of successors.
  unsigned getNumSuccessors() const { return getNumOperands() - 1; }
  /// Return the successor basic block.
  ///
  /// \return The successor basic block.
  /// \param Idx Element or aggregate index.
  BasicBlock *getSuccessor(unsigned Idx) const {
    assert(Idx < getNumSuccessors() &&
           "Successor # out of range for catchswitch!");
    return cast<BasicBlock>(getOperand(Idx + 1));
  }
  /// Set the successor at the given index.
  /// \param Idx Index.
  /// \param NewSucc The NewSucc parameter.
  void setSuccessor(unsigned Idx, BasicBlock *NewSucc) {
    assert(Idx < getNumSuccessors() &&
           "Successor # out of range for catchswitch!");
    setOperand(Idx + 1, NewSucc);
  }

  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test for CatchSwitchInst.
  static bool classof(const Value *From) {
    return From->getSubclassID() == ClassID::CatchSwitch;
  }
};

/// SandboxIR wrapper for an LLVM resume instruction.
class ResumeInst : public SingleLLVMInstructionImpl<llvm::ResumeInst> {
  /// ResumeInst.
  /// \param CSI The CSI parameter.
  /// \param Ctx SandboxIR context.
  ResumeInst(llvm::ResumeInst *CSI, Context &Ctx)
      : SingleLLVMInstructionImpl(ClassID::Resume, Opcode::Resume, CSI, Ctx) {}
  friend class Context; // For accessing the constructor in create*()

public:
  /// Create and insert a new ResumeInst.
  ///
  /// \return The newly created instruction.
  /// \param Exn The Exn parameter.
  /// \param Pos Insert position for the new instruction.
  /// \param Ctx SandboxIR context.
  LLVM_ABI static ResumeInst *create(Value *Exn, InsertPosition Pos,
                                     Context &Ctx);
  /// Return the exception value being resumed.
  ///
  /// \return The exception value being resumed.
  LLVM_ABI Value *getValue() const;
  /// Return the number of successors.
  ///
  /// \return The number of successors.
  unsigned getNumSuccessors() const {
    return cast<llvm::ResumeInst>(Val)->getNumSuccessors();
  }
  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test for ResumeInst.
  static bool classof(const Value *From) {
    return From->getSubclassID() == ClassID::Resume;
  }
};

/// SandboxIR wrapper for an LLVM switch instruction.
class SwitchInst : public SingleLLVMInstructionImpl<llvm::SwitchInst> {
  /// Construct a SwitchInst wrapping LLVM IR switch \p SI.
  /// \param SI Underlying LLVM SwitchInst to wrap.
  /// \param Ctx SandboxIR context.
  SwitchInst(llvm::SwitchInst *SI, Context &Ctx)
      : SingleLLVMInstructionImpl(ClassID::Switch, Opcode::Switch, SI, Ctx) {}
  friend class Context; // For accessing the constructor in create*()

public:
  /// Sentinel index representing the default switch case.
  static constexpr unsigned DefaultPseudoIndex =
      llvm::SwitchInst::DefaultPseudoIndex;

  /// Create and insert a new SwitchInst.
  ///
  /// \return The newly created instruction.
  /// \param V Condition value.
  /// \param Dest Default destination basic block.
  /// \param NumCases Expected number of switch cases.
  /// \param Pos Insert position for the new instruction.
  /// \param Ctx SandboxIR context.
  /// \param Name Name of the new instruction.
  LLVM_ABI static SwitchInst *create(Value *V, BasicBlock *Dest,
                                     unsigned NumCases, InsertPosition Pos,
                                     Context &Ctx, const Twine &Name = "");

  /// Return the condition operand.
  ///
  /// \return The condition operand.
  LLVM_ABI Value *getCondition() const;
  /// Set the condition operand.
  /// \param V New condition value.
  LLVM_ABI void setCondition(Value *V);
  /// Return the default destination basic block.
  ///
  /// \return The default destination basic block.
  LLVM_ABI BasicBlock *getDefaultDest() const;
  /// Return true if the default destination is unreachable.
  ///
  /// \return True if the default destination is unreachable.
  bool defaultDestUnreachable() const {
    return cast<llvm::SwitchInst>(Val)->defaultDestUnreachable();
  }
  /// Set the default destination basic block.
  /// \param DefaultCase Default destination basic block.
  LLVM_ABI void setDefaultDest(BasicBlock *DefaultCase);
  /// Return the number of switch cases.
  ///
  /// \return The number of switch cases.
  unsigned getNumCases() const {
    return cast<llvm::SwitchInst>(Val)->getNumCases();
  }

  template <typename LLVMCaseItT, typename BlockT, typename ConstT>
  /// Iterator over the cases of a SwitchInst.
  class CaseItImpl;

  // The template helps avoid code duplication for const and non-const
  // CaseHandle variants.
  template <typename LLVMCaseItT, typename BlockT, typename ConstT>
  /// Handle referring to one case of a SwitchInst.
  class CaseHandleImpl {
    Context &Ctx;
    // NOTE: We are not wrapping an LLVM CaseHande here because it is not
    // default-constructible. Instead we are wrapping the LLVM CaseIt
    // iterator, as we can always get an LLVM CaseHandle by de-referencing it.
    LLVMCaseItT LLVMCaseIt;
    template <typename T1, typename T2, typename T3> friend class CaseItImpl;

  public:
    /// Construct a handle to a switch case.
    /// \param Ctx SandboxIR context.
    /// \param LLVMCaseIt Underlying LLVM case iterator.
    CaseHandleImpl(Context &Ctx, LLVMCaseItT LLVMCaseIt)
        : Ctx(Ctx), LLVMCaseIt(LLVMCaseIt) {}
    /// Return the case constant value.
    ///
    /// \return The case constant value.
    LLVM_ABI ConstT *getCaseValue() const;
    /// Return the case successor basic block.
    ///
    /// \return The case successor basic block.
    LLVM_ABI BlockT *getCaseSuccessor() const;
    /// Return the case index.
    ///
    /// \return The case index.
    unsigned getCaseIndex() const {
      const auto &LLVMCaseHandle = *LLVMCaseIt;
      return LLVMCaseHandle.getCaseIndex();
    }
    /// Return the successor index for this case.
    ///
    /// \return The successor index for this case.
    unsigned getSuccessorIndex() const {
      const auto &LLVMCaseHandle = *LLVMCaseIt;
      return LLVMCaseHandle.getSuccessorIndex();
    }
  };

  // The template helps avoid code duplication for const and non-const CaseIt
  // variants.
  template <typename LLVMCaseItT, typename BlockT, typename ConstT>
  /// Iterator over the cases of a SwitchInst.
  class CaseItImpl : public iterator_facade_base<
                         CaseItImpl<LLVMCaseItT, BlockT, ConstT>,
                         std::random_access_iterator_tag,
                         const CaseHandleImpl<LLVMCaseItT, BlockT, ConstT>> {
    CaseHandleImpl<LLVMCaseItT, BlockT, ConstT> CH;

  public:
    /// Construct a case iterator from an LLVM case iterator.
    /// \param Ctx SandboxIR context.
    /// \param It Underlying LLVM case iterator.
    CaseItImpl(Context &Ctx, LLVMCaseItT It) : CH(Ctx, It) {}
    /// Construct a case iterator for case number \p CaseNum of \p SI.
    /// \param SI Switch instruction being iterated.
    /// \param CaseNum Zero-based case index.
    CaseItImpl(SwitchInst *SI, ptrdiff_t CaseNum)
        : CH(SI->getContext(), llvm::SwitchInst::CaseIt(
                                   cast<llvm::SwitchInst>(SI->Val), CaseNum)) {}
    /// Advance this case iterator by \p N.
    ///
    /// \return The requested iterator.
    /// \param N Offset to add.
    CaseItImpl &operator+=(ptrdiff_t N) {
      CH.LLVMCaseIt += N;
      return *this;
    }
    /// Retreat this case iterator by \p N.
    ///
    /// \return The requested iterator.
    /// \param N Offset to subtract.
    CaseItImpl &operator-=(ptrdiff_t N) {
      CH.LLVMCaseIt -= N;
      return *this;
    }
    /// Return the distance to case iterator \p Other.
    ///
    /// \return The distance to case iterator \p Other.
    /// \param Other Other case iterator.
    ptrdiff_t operator-(const CaseItImpl &Other) const {
      return CH.LLVMCaseIt - Other.CH.LLVMCaseIt;
    }
    /// Return true if this iterator equals \p Other.
    ///
    /// \return True if this iterator equals \p Other.
    /// \param Other Other case iterator.
    bool operator==(const CaseItImpl &Other) const {
      return CH.LLVMCaseIt == Other.CH.LLVMCaseIt;
    }
    /// Return true if this iterator precedes \p Other.
    ///
    /// \return True if this iterator precedes \p Other.
    /// \param Other Other case iterator.
    bool operator<(const CaseItImpl &Other) const {
      return CH.LLVMCaseIt < Other.CH.LLVMCaseIt;
    }
    /// Return the case handle at this iterator.
    ///
    /// \return The case handle at this iterator.
    const CaseHandleImpl<LLVMCaseItT, BlockT, ConstT> &operator*() const {
      return CH;
    }
  };

  /// Mutable handle to a switch case.
  using CaseHandle =
      CaseHandleImpl<llvm::SwitchInst::CaseIt, BasicBlock, ConstantInt>;
  /// Mutable iterator over switch cases.
  using CaseIt = CaseItImpl<llvm::SwitchInst::CaseIt, BasicBlock, ConstantInt>;

  /// Const handle to a switch case.
  using ConstCaseHandle = CaseHandleImpl<llvm::SwitchInst::ConstCaseIt,
                                         const BasicBlock, const ConstantInt>;
  /// Const iterator over switch cases.
  using ConstCaseIt = CaseItImpl<llvm::SwitchInst::ConstCaseIt,
                                 const BasicBlock, const ConstantInt>;

  /// Returns a read/write iterator that points to the first case in the
  /// SwitchInst.
  ///
  /// \return A read/write iterator that points to the first case in the SwitchInst.
  CaseIt case_begin() {
    return CaseIt(Ctx, cast<llvm::SwitchInst>(Val)->case_begin());
  }
  /// Return an iterator to the first switch case.
  ///
  /// \return An iterator to the first switch case.
  ConstCaseIt case_begin() const {
    return ConstCaseIt(Ctx, cast<llvm::SwitchInst>(Val)->case_begin());
  }
  /// Returns a read/write iterator that points one past the last in the
  /// SwitchInst.
  ///
  /// \return A read/write iterator that points one past the last in the SwitchInst.
  CaseIt case_end() {
    return CaseIt(Ctx, cast<llvm::SwitchInst>(Val)->case_end());
  }
  /// Return an iterator past the last switch case.
  ///
  /// \return An iterator past the last switch case.
  ConstCaseIt case_end() const {
    return ConstCaseIt(Ctx, cast<llvm::SwitchInst>(Val)->case_end());
  }
  /// Iteration adapter for range-for loops.
  ///
  /// \return The requested iterator.
  iterator_range<CaseIt> cases() {
    return make_range(case_begin(), case_end());
  }
  /// Return a range over the switch cases.
  ///
  /// \return A range over the switch cases.
  iterator_range<ConstCaseIt> cases() const {
    return make_range(case_begin(), case_end());
  }
  /// Return a handle to the default case.
  ///
  /// \return A handle to the default case.
  CaseIt case_default() {
    return CaseIt(Ctx, cast<llvm::SwitchInst>(Val)->case_default());
  }
  /// Return a handle to the default case.
  ///
  /// \return A handle to the default case.
  ConstCaseIt case_default() const {
    return ConstCaseIt(Ctx, cast<llvm::SwitchInst>(Val)->case_default());
  }
  /// Find the case matching a constant value.
  ///
  /// \return The requested iterator.
  /// \param C Case constant to look up.
  CaseIt findCaseValue(const ConstantInt *C) {
    const llvm::ConstantInt *LLVMC = cast<llvm::ConstantInt>(C->Val);
    return CaseIt(Ctx, cast<llvm::SwitchInst>(Val)->findCaseValue(LLVMC));
  }
  /// Find the case matching a constant value.
  ///
  /// \return The requested iterator.
  /// \param C Case constant to look up.
  ConstCaseIt findCaseValue(const ConstantInt *C) const {
    const llvm::ConstantInt *LLVMC = cast<llvm::ConstantInt>(C->Val);
    return ConstCaseIt(Ctx, cast<llvm::SwitchInst>(Val)->findCaseValue(LLVMC));
  }
  /// Find the case constant that targets basic block \p BB.
  ///
  /// \return The requested value, or null if unavailable.
  /// \param BB Successor basic block to search for.
  LLVM_ABI ConstantInt *findCaseDest(BasicBlock *BB);

  /// Add a switch case.
  /// \param OnVal Case constant value.
  /// \param Dest Destination basic block.
  LLVM_ABI void addCase(ConstantInt *OnVal, BasicBlock *Dest);
  /// Remove a switch case and return the next iterator.
  ///
  /// This may reorder the remaining cases at index idx and above. This action
  /// invalidates iterators for all cases following the one removed, including
  /// the case_end() iterator.
  ///
  /// \return The requested iterator.
  /// \param It Case iterator to remove.
  LLVM_ABI CaseIt removeCase(CaseIt It);

  /// Return the number of successors.
  ///
  /// \return The number of successors.
  unsigned getNumSuccessors() const {
    return cast<llvm::SwitchInst>(Val)->getNumSuccessors();
  }
  /// Return the successor at index \p Idx.
  ///
  /// \return The successor at index \p Idx.
  /// \param Idx Successor index.
  LLVM_ABI BasicBlock *getSuccessor(unsigned Idx) const;
  /// Set the successor at the given index.
  /// \param Idx Successor index.
  /// \param NewSucc Replacement successor basic block.
  LLVM_ABI void setSuccessor(unsigned Idx, BasicBlock *NewSucc);
  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test for SwitchInst.
  static bool classof(const Value *From) {
    return From->getSubclassID() == ClassID::Switch;
  }
};

/// SandboxIR wrapper for an LLVM unary operator.
class UnaryOperator : public UnaryInstruction {
  /// Map an LLVM unary opcode to a SandboxIR opcode.
  /// \param UnOp LLVM IR unary opcode to map.
  static Opcode getUnaryOpcode(llvm::Instruction::UnaryOps UnOp) {
    switch (UnOp) {
    case llvm::Instruction::FNeg:
      return Opcode::FNeg;
    case llvm::Instruction::UnaryOpsEnd:
      llvm_unreachable("Bad UnOp!");
    }
    llvm_unreachable("Unhandled UnOp!");
  }
  /// Construct a UnaryOperator wrapping LLVM IR unary operator \p UO.
  /// \param UO Underlying LLVM UnaryOperator to wrap.
  /// \param Ctx SandboxIR context.
  UnaryOperator(llvm::UnaryOperator *UO, Context &Ctx)
      : UnaryInstruction(ClassID::UnOp, getUnaryOpcode(UO->getOpcode()), UO,
                         Ctx) {}
  friend Context; // for constructor.
public:
  /// Create and insert a new UnaryOperator.
  ///
  /// \return The newly created instruction.
  /// \param Op Unary opcode.
  /// \param OpV Operand value.
  /// \param Pos Insert position for the new instruction.
  /// \param Ctx SandboxIR context.
  /// \param Name Name of the new instruction.
  LLVM_ABI static Value *create(Instruction::Opcode Op, Value *OpV,
                                InsertPosition Pos, Context &Ctx,
                                const Twine &Name = "");
  /// Create a UnaryOperator copying flags from \p CopyFrom.
  ///
  /// \return The newly created instruction.
  /// \param Op Unary opcode.
  /// \param OpV Operand value.
  /// \param CopyFrom Instruction whose flags are copied.
  /// \param Pos Insert position for the new instruction.
  /// \param Ctx SandboxIR context.
  /// \param Name Name of the new instruction.
  LLVM_ABI static Value *createWithCopiedFlags(Instruction::Opcode Op,
                                               Value *OpV, Value *CopyFrom,
                                               InsertPosition Pos, Context &Ctx,
                                               const Twine &Name = "");
  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test for UnaryOperator.
  static bool classof(const Value *From) {
    return From->getSubclassID() == ClassID::UnOp;
  }
};

/// SandboxIR wrapper for an LLVM binary operator.
class BinaryOperator : public SingleLLVMInstructionImpl<llvm::BinaryOperator> {
protected:
  /// Map a SandboxIR opcode to an LLVM binary opcode.
  ///
  /// \return The computed result.
  /// \param BinOp LLVM IR binary opcode to map.
  static Opcode getBinOpOpcode(llvm::Instruction::BinaryOps BinOp) {
    switch (BinOp) {
    case llvm::Instruction::Add:
      return Opcode::Add;
    case llvm::Instruction::FAdd:
      return Opcode::FAdd;
    case llvm::Instruction::Sub:
      return Opcode::Sub;
    case llvm::Instruction::FSub:
      return Opcode::FSub;
    case llvm::Instruction::Mul:
      return Opcode::Mul;
    case llvm::Instruction::FMul:
      return Opcode::FMul;
    case llvm::Instruction::UDiv:
      return Opcode::UDiv;
    case llvm::Instruction::SDiv:
      return Opcode::SDiv;
    case llvm::Instruction::FDiv:
      return Opcode::FDiv;
    case llvm::Instruction::URem:
      return Opcode::URem;
    case llvm::Instruction::SRem:
      return Opcode::SRem;
    case llvm::Instruction::FRem:
      return Opcode::FRem;
    case llvm::Instruction::Shl:
      return Opcode::Shl;
    case llvm::Instruction::LShr:
      return Opcode::LShr;
    case llvm::Instruction::AShr:
      return Opcode::AShr;
    case llvm::Instruction::And:
      return Opcode::And;
    case llvm::Instruction::Or:
      return Opcode::Or;
    case llvm::Instruction::Xor:
      return Opcode::Xor;
    case llvm::Instruction::BinaryOpsEnd:
      llvm_unreachable("Bad BinOp!");
    }
    llvm_unreachable("Unhandled BinOp!");
  }
  /// Construct a SandboxIR binary operator.
  /// \param BinOp Atomic RMW binary operation.
  /// \param Ctx SandboxIR context.
  BinaryOperator(llvm::BinaryOperator *BinOp, Context &Ctx)
      : SingleLLVMInstructionImpl(ClassID::BinaryOperator,
                                  getBinOpOpcode(BinOp->getOpcode()), BinOp,
                                  Ctx) {}
  friend class Context; // For constructor.

public:
  /// Create and insert a new BinaryOperator.
  ///
  /// \return The newly created instruction.
  /// \param Op Instruction opcode.
  /// \param LHS The LHS parameter.
  /// \param RHS The RHS parameter.
  /// \param Pos Insert position for the new instruction.
  /// \param Ctx SandboxIR context.
  /// \param Name Name of the new instruction.
  LLVM_ABI static Value *create(Instruction::Opcode Op, Value *LHS, Value *RHS,
                                InsertPosition Pos, Context &Ctx,
                                const Twine &Name = "");

  /// Create a BinaryOperator copying flags from another instruction.
  ///
  /// \return The newly created instruction.
  /// \param Op Instruction opcode.
  /// \param LHS The LHS parameter.
  /// \param RHS The RHS parameter.
  /// \param CopyFrom Instruction whose flags are copied.
  /// \param Pos Insert position for the new instruction.
  /// \param Ctx SandboxIR context.
  /// \param Name Name of the new instruction.
  LLVM_ABI static Value *createWithCopiedFlags(Instruction::Opcode Op,
                                               Value *LHS, Value *RHS,
                                               Value *CopyFrom,
                                               InsertPosition Pos, Context &Ctx,
                                               const Twine &Name = "");
  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test.
  static bool classof(const Value *From) {
    return From->getSubclassID() == ClassID::BinaryOperator;
  }
  /// Swap the instruction's operands.
  void swapOperands() { swapOperandsInternal(0, 1); }
};

/// An or instruction that can be marked "disjoint".
///
/// When marked disjoint, the inputs do not have a 1 in the same bit
/// position, so this instruction can also be treated as an add.
class PossiblyDisjointInst : public BinaryOperator {
public:
  /// Set the disjoint flag.
  /// \param B Flag value to set or clear.
  LLVM_ABI void setIsDisjoint(bool B);
  /// Return true if the disjoint flag is set.
  ///
  /// \return True if the disjoint flag is set.
  bool isDisjoint() const {
    return cast<llvm::PossiblyDisjointInst>(Val)->isDisjoint();
  }
  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test.
  static bool classof(const Value *From) {
    return isa<Instruction>(From) &&
           cast<Instruction>(From)->getOpcode() == Opcode::Or;
  }
};

/// SandboxIR wrapper for an LLVM atomicrmw instruction.
class AtomicRMWInst : public SingleLLVMInstructionImpl<llvm::AtomicRMWInst> {
  /// AtomicRMWInst.
  /// \param Atomic The Atomic parameter.
  /// \param Ctx SandboxIR context.
  AtomicRMWInst(llvm::AtomicRMWInst *Atomic, Context &Ctx)
      : SingleLLVMInstructionImpl(ClassID::AtomicRMW,
                                  Instruction::Opcode::AtomicRMW, Atomic, Ctx) {
  }
  friend class Context; // For constructor.

public:
  /// Binary operation performed by an atomicrmw.
  using BinOp = llvm::AtomicRMWInst::BinOp;
  /// Return the atomic RMW binary operation.
  ///
  /// \return The atomic RMW binary operation.
  BinOp getOperation() const {
    return cast<llvm::AtomicRMWInst>(Val)->getOperation();
  }
  /// Return the name of an atomic RMW operation.
  ///
  /// \return The name of an atomic RMW operation.
  /// \param Op Binary or unary opcode.
  static StringRef getOperationName(BinOp Op) {
    return llvm::AtomicRMWInst::getOperationName(Op);
  }
  /// Return true if the RMW operation is floating-point.
  ///
  /// \return True if the RMW operation is floating-point.
  /// \param Op Binary or unary opcode.
  static bool isFPOperation(BinOp Op) {
    return llvm::AtomicRMWInst::isFPOperation(Op);
  }
  /// Set the atomic RMW binary operation.
  /// \param Op Binary or unary opcode.
  void setOperation(BinOp Op) {
    cast<llvm::AtomicRMWInst>(Val)->setOperation(Op);
  }
  /// Return the alignment of this memory access.
  ///
  /// \return The alignment of this memory access.
  Align getAlign() const { return cast<llvm::AtomicRMWInst>(Val)->getAlign(); }
  /// Set the alignment of this memory access.
  /// \param Align Required alignment.
  LLVM_ABI void setAlignment(Align Align);
  /// Return true if this memory access is volatile.
  ///
  /// \return True if this memory access is volatile.
  bool isVolatile() const {
    return cast<llvm::AtomicRMWInst>(Val)->isVolatile();
  }
  /// Set whether this memory access is volatile.
  /// \param V True if the access is volatile.
  LLVM_ABI void setVolatile(bool V);
  /// Return the atomic ordering constraint.
  ///
  /// \return The atomic ordering constraint.
  AtomicOrdering getOrdering() const {
    return cast<llvm::AtomicRMWInst>(Val)->getOrdering();
  }
  /// Set the atomic ordering constraint.
  /// \param Ordering Atomic ordering constraint.
  LLVM_ABI void setOrdering(AtomicOrdering Ordering);
  /// Return the synchronization scope ID.
  ///
  /// \return The synchronization scope ID.
  SyncScope::ID getSyncScopeID() const {
    return cast<llvm::AtomicRMWInst>(Val)->getSyncScopeID();
  }
  /// Set the synchronization scope ID.
  /// \param SSID Synchronization scope identifier.
  LLVM_ABI void setSyncScopeID(SyncScope::ID SSID);
  /// Return the pointer operand.
  ///
  /// \return The pointer operand.
  LLVM_ABI Value *getPointerOperand();
  /// Return the pointer operand.
  ///
  /// \return The pointer operand.
  const Value *getPointerOperand() const {
    return const_cast<AtomicRMWInst *>(this)->getPointerOperand();
  }
  /// Return the value operand.
  ///
  /// \return The value operand.
  LLVM_ABI Value *getValOperand();
  /// Return the value operand.
  ///
  /// \return The value operand.
  const Value *getValOperand() const {
    return const_cast<AtomicRMWInst *>(this)->getValOperand();
  }
  /// Return the address space of the pointer operand.
  ///
  /// \return The address space of the pointer operand.
  unsigned getPointerAddressSpace() const {
    return cast<llvm::AtomicRMWInst>(Val)->getPointerAddressSpace();
  }
  /// Return true if this RMW is a floating-point operation.
  ///
  /// \return True if this RMW is a floating-point operation.
  bool isFloatingPointOperation() const {
    return cast<llvm::AtomicRMWInst>(Val)->isFloatingPointOperation();
  }
  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test for AtomicRMWInst.
  static bool classof(const Value *From) {
    return From->getSubclassID() == ClassID::AtomicRMW;
  }

  LLVM_ABI static AtomicRMWInst *
  /// Create and insert a new AtomicRMWInst.
  ///
  /// \return The newly created instruction.
  /// \param Op Binary or unary opcode.
  /// \param Ptr Pointer operand.
  /// \param Val Value operand.
  /// \param Align Required alignment.
  /// \param Ordering Atomic ordering constraint.
  /// \param Pos Insert position for the new instruction.
  /// \param Ctx SandboxIR context.
  /// \param SSID Synchronization scope identifier.
  /// \param Name Name of the new instruction.
  create(BinOp Op, Value *Ptr, Value *Val, MaybeAlign Align,
         AtomicOrdering Ordering, InsertPosition Pos, Context &Ctx,
         SyncScope::ID SSID = SyncScope::System, const Twine &Name = "");
};

/// SandboxIR wrapper for an LLVM cmpxchg instruction.
class AtomicCmpXchgInst
    : public SingleLLVMInstructionImpl<llvm::AtomicCmpXchgInst> {
  /// AtomicCmpXchgInst.
  /// \param Atomic The Atomic parameter.
  /// \param Ctx SandboxIR context.
  AtomicCmpXchgInst(llvm::AtomicCmpXchgInst *Atomic, Context &Ctx)
      : SingleLLVMInstructionImpl(ClassID::AtomicCmpXchg,
                                  Instruction::Opcode::AtomicCmpXchg, Atomic,
                                  Ctx) {}
  friend class Context; // For constructor.

public:
  /// Return the alignment of the memory that is being allocated by the
  /// instruction.
  ///
  /// \return The alignment of the memory that is being allocated by the instruction.
  Align getAlign() const {
    return cast<llvm::AtomicCmpXchgInst>(Val)->getAlign();
  }

  /// Set the alignment of this memory access.
  /// \param Align Required alignment.
  LLVM_ABI void setAlignment(Align Align);
  /// Return true if this is a cmpxchg from a volatile memory
  /// location.
  ///
  /// \return True if this is a cmpxchg from a volatile memory location.
  bool isVolatile() const {
    return cast<llvm::AtomicCmpXchgInst>(Val)->isVolatile();
  }
  /// Specify whether this is a volatile cmpxchg.
  /// \param V True if the access is volatile.
  LLVM_ABI void setVolatile(bool V);
  /// Return true if this cmpxchg may spuriously fail.
  ///
  /// \return True if this cmpxchg may spuriously fail.
  bool isWeak() const { return cast<llvm::AtomicCmpXchgInst>(Val)->isWeak(); }
  /// Set whether this cmpxchg is weak.
  /// \param IsWeak The IsWeak parameter.
  LLVM_ABI void setWeak(bool IsWeak);
  /// Return true if the ordering is valid on success.
  ///
  /// \return True if the ordering is valid on success.
  /// \param Ordering Atomic ordering constraint.
  static bool isValidSuccessOrdering(AtomicOrdering Ordering) {
    return llvm::AtomicCmpXchgInst::isValidSuccessOrdering(Ordering);
  }
  /// Return true if the ordering is valid on failure.
  ///
  /// \return True if the ordering is valid on failure.
  /// \param Ordering Atomic ordering constraint.
  static bool isValidFailureOrdering(AtomicOrdering Ordering) {
    return llvm::AtomicCmpXchgInst::isValidFailureOrdering(Ordering);
  }
  /// Return the success atomic ordering.
  ///
  /// \return The success atomic ordering.
  AtomicOrdering getSuccessOrdering() const {
    return cast<llvm::AtomicCmpXchgInst>(Val)->getSuccessOrdering();
  }
  /// Set the success atomic ordering.
  /// \param Ordering Atomic ordering constraint.
  LLVM_ABI void setSuccessOrdering(AtomicOrdering Ordering);

  /// Return the failure atomic ordering.
  ///
  /// \return The failure atomic ordering.
  AtomicOrdering getFailureOrdering() const {
    return cast<llvm::AtomicCmpXchgInst>(Val)->getFailureOrdering();
  }
  /// Set the failure atomic ordering.
  /// \param Ordering Atomic ordering constraint.
  LLVM_ABI void setFailureOrdering(AtomicOrdering Ordering);
  /// Return the merged atomic ordering.
  ///
  /// \return The merged atomic ordering.
  AtomicOrdering getMergedOrdering() const {
    return cast<llvm::AtomicCmpXchgInst>(Val)->getMergedOrdering();
  }
  /// Return the synchronization scope ID.
  ///
  /// \return The synchronization scope ID.
  SyncScope::ID getSyncScopeID() const {
    return cast<llvm::AtomicCmpXchgInst>(Val)->getSyncScopeID();
  }
  /// Set the synchronization scope ID.
  /// \param SSID Synchronization scope identifier.
  LLVM_ABI void setSyncScopeID(SyncScope::ID SSID);
  /// Return the pointer operand.
  ///
  /// \return The pointer operand.
  LLVM_ABI Value *getPointerOperand();
  /// Return the pointer operand.
  ///
  /// \return The pointer operand.
  const Value *getPointerOperand() const {
    return const_cast<AtomicCmpXchgInst *>(this)->getPointerOperand();
  }

  /// Return the compare operand.
  ///
  /// \return The compare operand.
  LLVM_ABI Value *getCompareOperand();
  /// Return the compare operand.
  ///
  /// \return The compare operand.
  const Value *getCompareOperand() const {
    return const_cast<AtomicCmpXchgInst *>(this)->getCompareOperand();
  }

  /// Return the new-value operand.
  ///
  /// \return The new-value operand.
  LLVM_ABI Value *getNewValOperand();
  /// Return the new-value operand.
  ///
  /// \return The new-value operand.
  const Value *getNewValOperand() const {
    return const_cast<AtomicCmpXchgInst *>(this)->getNewValOperand();
  }

  /// Returns the address space of the pointer operand.
  ///
  /// \return The address space of the pointer operand.
  unsigned getPointerAddressSpace() const {
    return cast<llvm::AtomicCmpXchgInst>(Val)->getPointerAddressSpace();
  }

  LLVM_ABI static AtomicCmpXchgInst *
  /// Create and insert a new AtomicCmpXchgInst.
  ///
  /// \return The newly created instruction.
  /// \param Ptr Pointer operand.
  /// \param Cmp Comparison operand.
  /// \param New Replacement basic block or value.
  /// \param Align Required alignment.
  /// \param SuccessOrdering Atomic ordering used on success.
  /// \param FailureOrdering Atomic ordering used on failure.
  /// \param Pos Insert position for the new instruction.
  /// \param Ctx SandboxIR context.
  /// \param SSID Synchronization scope identifier.
  /// \param Name Name of the new instruction.
  create(Value *Ptr, Value *Cmp, Value *New, MaybeAlign Align,
         AtomicOrdering SuccessOrdering, AtomicOrdering FailureOrdering,
         InsertPosition Pos, Context &Ctx,
         SyncScope::ID SSID = SyncScope::System, const Twine &Name = "");

  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test for AtomicCmpXchgInst.
  static bool classof(const Value *From) {
    return From->getSubclassID() == ClassID::AtomicCmpXchg;
  }
};

/// SandboxIR wrapper for an LLVM alloca instruction.
class AllocaInst final : public UnaryInstruction {
  /// Construct an AllocaInst wrapping LLVM IR alloca \p AI.
  /// \param AI Underlying LLVM AllocaInst to wrap.
  /// \param Ctx SandboxIR context.
  AllocaInst(llvm::AllocaInst *AI, Context &Ctx)
      : UnaryInstruction(ClassID::Alloca, Instruction::Opcode::Alloca, AI,
                         Ctx) {}
  friend class Context; // For constructor.

public:
  /// Create and insert a new AllocaInst.
  ///
  /// \return The newly created instruction.
  /// \param Ty Type being allocated.
  /// \param AddrSpace Address space of the allocation.
  /// \param Pos Insert position for the new instruction.
  /// \param Ctx SandboxIR context.
  /// \param ArraySize Number of elements to allocate.
  /// \param Name Name of the new instruction.
  LLVM_ABI static AllocaInst *create(Type *Ty, unsigned AddrSpace,
                                     InsertPosition Pos, Context &Ctx,
                                     Value *ArraySize = nullptr,
                                     const Twine &Name = "");

  /// Return true if there is an allocation size parameter to the allocation
  /// instruction that is not 1.
  ///
  /// \return True if there is an allocation size parameter to the allocation instruction that is not 1.
  bool isArrayAllocation() const {
    return cast<llvm::AllocaInst>(Val)->isArrayAllocation();
  }
  /// Get the number of elements allocated. For a simple allocation of a single
  /// element, this will return a constant 1 value.
  ///
  /// \return The requested value, or null if unavailable.
  LLVM_ABI Value *getArraySize();
  /// Return the array size operand.
  ///
  /// \return The array size operand.
  const Value *getArraySize() const {
    return const_cast<AllocaInst *>(this)->getArraySize();
  }
  /// Overload to return most specific pointer type.
  ///
  /// \return The requested value, or null if unavailable.
  LLVM_ABI PointerType *getType() const;
  /// Return the address space for the allocation.
  ///
  /// \return The address space for the allocation.
  unsigned getAddressSpace() const {
    return cast<llvm::AllocaInst>(Val)->getAddressSpace();
  }
  /// Get allocation size in bytes. Returns std::nullopt if size can't be
  /// determined, e.g. in case of a VLA.
  ///
  /// \return The computed result.
  /// \param DL Data layout used for size computation.
  std::optional<TypeSize> getAllocationSize(const DataLayout &DL) const {
    return cast<llvm::AllocaInst>(Val)->getAllocationSize(DL);
  }
  /// Get allocation size in bits. Returns std::nullopt if size can't be
  /// determined, e.g. in case of a VLA.
  ///
  /// \return The computed result.
  /// \param DL Data layout used for size computation.
  std::optional<TypeSize> getAllocationSizeInBits(const DataLayout &DL) const {
    return cast<llvm::AllocaInst>(Val)->getAllocationSizeInBits(DL);
  }
  /// Return the type that is being allocated by the instruction.
  ///
  /// \return The type that is being allocated by the instruction.
  LLVM_ABI Type *getAllocatedType() const;
  /// for use only in special circumstances that need to generically
  /// transform a whole instruction (eg: IR linking and vectorization).
  /// \param Ty Type.
  LLVM_ABI void setAllocatedType(Type *Ty);
  /// Return the alignment of the memory that is being allocated by the
  /// instruction.
  ///
  /// \return The alignment of the memory that is being allocated by the instruction.
  Align getAlign() const { return cast<llvm::AllocaInst>(Val)->getAlign(); }
  /// Set the alignment of this memory access.
  /// \param Align Required alignment.
  LLVM_ABI void setAlignment(Align Align);
  /// Return true if this is a free static entry-block alloca.
  ///
  /// True when the alloca is in the entry block of the function and has a
  /// constant size. If so, the code generator will fold it into the
  /// prolog/epilog code, so it is basically free.
  ///
  /// \return True if this is a free static entry-block alloca.
  bool isStaticAlloca() const {
    return cast<llvm::AllocaInst>(Val)->isStaticAlloca();
  }
  /// Return true if this alloca is used as an inalloca argument to a call. Such
  /// allocas are never considered static even if they are in the entry block.
  ///
  /// \return True if this alloca is used as an inalloca argument to a call. Such allocas are never considered static even if they are in the entry block.
  bool isUsedWithInAlloca() const {
    return cast<llvm::AllocaInst>(Val)->isUsedWithInAlloca();
  }
  /// Specify whether this alloca is used to represent the arguments to a call.
  /// \param V Value or flag.
  LLVM_ABI void setUsedWithInAlloca(bool V);

  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test for AllocaInst.
  static bool classof(const Value *From) {
    if (auto *I = dyn_cast<Instruction>(From))
      return I->getSubclassID() == Instruction::ClassID::Alloca;
    return false;
  }
};

/// Base class for SandboxIR cast instructions.
class CastInst : public UnaryInstruction {
  /// Return the cast opcode.
  /// \param CastOp The CastOp parameter.
  static Opcode getCastOpcode(llvm::Instruction::CastOps CastOp) {
    switch (CastOp) {
    case llvm::Instruction::ZExt:
      return Opcode::ZExt;
    case llvm::Instruction::SExt:
      return Opcode::SExt;
    case llvm::Instruction::FPToUI:
      return Opcode::FPToUI;
    case llvm::Instruction::FPToSI:
      return Opcode::FPToSI;
    case llvm::Instruction::FPExt:
      return Opcode::FPExt;
    case llvm::Instruction::PtrToAddr:
      return Opcode::PtrToAddr;
    case llvm::Instruction::PtrToInt:
      return Opcode::PtrToInt;
    case llvm::Instruction::IntToPtr:
      return Opcode::IntToPtr;
    case llvm::Instruction::SIToFP:
      return Opcode::SIToFP;
    case llvm::Instruction::UIToFP:
      return Opcode::UIToFP;
    case llvm::Instruction::Trunc:
      return Opcode::Trunc;
    case llvm::Instruction::FPTrunc:
      return Opcode::FPTrunc;
    case llvm::Instruction::BitCast:
      return Opcode::BitCast;
    case llvm::Instruction::AddrSpaceCast:
      return Opcode::AddrSpaceCast;
    case llvm::Instruction::CastOpsEnd:
      llvm_unreachable("Bad CastOp!");
    }
    llvm_unreachable("Unhandled CastOp!");
  }
  /// Use Context::createCastInst(). Don't call the
  /// constructor directly.
  /// \param CI Underlying LLVM CmpInst to wrap.
  /// \param Ctx SandboxIR context.
  CastInst(llvm::CastInst *CI, Context &Ctx)
      : UnaryInstruction(ClassID::Cast, getCastOpcode(CI->getOpcode()), CI,
                         Ctx) {}
  friend Context; // for SBCastInstruction()

public:
  /// Create and insert a new CastInst.
  ///
  /// \return The newly created instruction.
  /// \param DestTy The DestTy parameter.
  /// \param Op Instruction opcode.
  /// \param Operand The Operand parameter.
  /// \param Pos Insert position for the new instruction.
  /// \param Ctx SandboxIR context.
  /// \param Name Name of the new instruction.
  LLVM_ABI static Value *create(Type *DestTy, Opcode Op, Value *Operand,
                                InsertPosition Pos, Context &Ctx,
                                const Twine &Name = "");
  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test.
  LLVM_ABI static bool classof(const Value *From);
  /// Return the source type of this cast.
  ///
  /// \return The source type of this cast.
  LLVM_ABI Type *getSrcTy() const;
  /// Return the destination type of this cast.
  ///
  /// \return The destination type of this cast.
  LLVM_ABI Type *getDestTy() const;
};

/// Instruction that can have a nneg flag (zext/uitofp).
class PossiblyNonNegInst : public CastInst {
public:
  /// Return true if the nneg flag is set.
  ///
  /// \return True if the nneg flag is set.
  bool hasNonNeg() const {
    return cast<llvm::PossiblyNonNegInst>(Val)->hasNonNeg();
  }
  /// Set the nneg flag.
  /// \param B Flag value to set or clear.
  LLVM_ABI void setNonNeg(bool B);
  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test.
  static bool classof(const Value *From) {
    if (auto *I = dyn_cast<Instruction>(From)) {
      switch (I->getOpcode()) {
      case Opcode::ZExt:
      case Opcode::UIToFP:
        return true;
      default:
        return false;
      }
    }
    return false;
  }
};

/// CRTP helper implementing concrete SandboxIR cast instructions.
template <Instruction::Opcode Op> class CastInstImpl : public CastInst {
public:
  /// Create and insert a cast of opcode Op.
  ///
  /// \return The newly created instruction.
  /// \param Src Source value of the cast.
  /// \param DestTy Destination type of the cast.
  /// \param Pos Insert position for the new instruction.
  /// \param Ctx SandboxIR context.
  /// \param Name Name of the new instruction.
  static Value *create(Value *Src, Type *DestTy, InsertPosition Pos,
                       Context &Ctx, const Twine &Name = "") {
  /// Create and insert a new PossiblyNonNegInst.
  ///
  /// \return The newly created instruction.
  /// \param DestTy The DestTy parameter.
  /// \param Op Instruction opcode.
  /// \param Src The Src parameter.
  /// \param Pos Insert position for the new instruction.
  /// \param Ctx SandboxIR context.
  /// \param Name Name of the new instruction.
    return CastInst::create(DestTy, Op, Src, Pos, Ctx, Name);
  }

  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test for this cast opcode.
  static bool classof(const Value *From) {
    if (auto *I = dyn_cast<Instruction>(From))
      return I->getOpcode() == Op;
    return false;
  }
};

/// SandboxIR wrapper for an LLVM trunc instruction.
class TruncInst final : public CastInstImpl<Instruction::Opcode::Trunc> {};
/// SandboxIR wrapper for an LLVM zext instruction.
class ZExtInst final : public CastInstImpl<Instruction::Opcode::ZExt> {};
/// SandboxIR wrapper for an LLVM sext instruction.
class SExtInst final : public CastInstImpl<Instruction::Opcode::SExt> {};
/// SandboxIR wrapper for an LLVM fptrunc instruction.
class FPTruncInst final : public CastInstImpl<Instruction::Opcode::FPTrunc> {};
/// SandboxIR wrapper for an LLVM fpext instruction.
class FPExtInst final : public CastInstImpl<Instruction::Opcode::FPExt> {};
/// SandboxIR wrapper for an LLVM uitofp instruction.
class UIToFPInst final : public CastInstImpl<Instruction::Opcode::UIToFP> {};
/// SandboxIR wrapper for an LLVM sitofp instruction.
class SIToFPInst final : public CastInstImpl<Instruction::Opcode::SIToFP> {};
/// SandboxIR wrapper for an LLVM fptoui instruction.
class FPToUIInst final : public CastInstImpl<Instruction::Opcode::FPToUI> {};
/// SandboxIR wrapper for an LLVM fptosi instruction.
class FPToSIInst final : public CastInstImpl<Instruction::Opcode::FPToSI> {};
/// SandboxIR wrapper for an LLVM inttoptr instruction.
class IntToPtrInst final : public CastInstImpl<Instruction::Opcode::IntToPtr> {
};
/// SandboxIR wrapper for an LLVM ptrtoaddr instruction.
class PtrToAddrInst final
    : public CastInstImpl<Instruction::Opcode::PtrToAddr> {};
/// SandboxIR wrapper for an LLVM ptrtoint instruction.
class PtrToIntInst final : public CastInstImpl<Instruction::Opcode::PtrToInt> {
};
/// SandboxIR wrapper for an LLVM bitcast instruction.
class BitCastInst final : public CastInstImpl<Instruction::Opcode::BitCast> {};
/// SandboxIR wrapper for an LLVM addrspacecast instruction.
class AddrSpaceCastInst final
    : public CastInstImpl<Instruction::Opcode::AddrSpaceCast> {
public:
  /// Return the pointer operand.
  ///
  /// \return The pointer operand.
  Value *getPointerOperand() { return getOperand(0); }
  /// Return the pointer operand.
  ///
  /// \return The pointer operand.
  const Value *getPointerOperand() const {
    return const_cast<AddrSpaceCastInst *>(this)->getPointerOperand();
  }
  /// Return the operand index of the pointer operand.
  ///
  /// \return The operand index of the pointer operand.
  static unsigned getPointerOperandIndex() { return 0u; }
  /// Return the address space of the pointer operand.
  ///
  /// \return The address space of the pointer operand.
  unsigned getSrcAddressSpace() const {
    return getPointerOperand()->getType()->getPointerAddressSpace();
  }
  /// Return the address space of the result.
  ///
  /// \return The address space of the result.
  unsigned getDestAddressSpace() const {
    return getType()->getPointerAddressSpace();
  }
};

/// SandboxIR wrapper for an LLVM PHI node.
class PHINode final : public SingleLLVMInstructionImpl<llvm::PHINode> {
  /// Use Context::createPHINode(). Don't call the constructor directly.
  /// \param PHI Underlying LLVM PHINode to wrap.
  /// \param Ctx SandboxIR context.
  PHINode(llvm::PHINode *PHI, Context &Ctx)
      : SingleLLVMInstructionImpl(ClassID::PHI, Opcode::PHI, PHI, Ctx) {}
  friend Context; // for PHINode()
  /// Helper for mapped_iterator.
  struct LLVMBBToBB {
    Context &Ctx;
    LLVMBBToBB(Context &Ctx) : Ctx(Ctx) {}
    LLVM_ABI BasicBlock *operator()(llvm::BasicBlock *LLVMBB) const;
  };

public:
  /// Create and insert a new PHINode.
  ///
  /// \return The newly created instruction.
  /// \param Ty Result type of the PHI.
  /// \param NumReservedValues Number of PHI incoming values to reserve.
  /// \param Pos Insert position for the new instruction.
  /// \param Ctx SandboxIR context.
  /// \param Name Name of the new instruction.
  LLVM_ABI static PHINode *create(Type *Ty, unsigned NumReservedValues,
                                  InsertPosition Pos, Context &Ctx,
                                  const Twine &Name = "");
  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test for PHINode.
  LLVM_ABI static bool classof(const Value *From);

  /// Const iterator over PHI incoming basic blocks.
  using const_block_iterator =
      mapped_iterator<llvm::PHINode::const_block_iterator, LLVMBBToBB>;

  /// Return an iterator to the first incoming block.
  ///
  /// \return An iterator to the first incoming block.
  const_block_iterator block_begin() const {
    LLVMBBToBB BBGetter(Ctx);
    return const_block_iterator(cast<llvm::PHINode>(Val)->block_begin(),
                                BBGetter);
  }
  /// Return an iterator past the last incoming block.
  ///
  /// \return An iterator past the last incoming block.
  const_block_iterator block_end() const {
    LLVMBBToBB BBGetter(Ctx);
    return const_block_iterator(cast<llvm::PHINode>(Val)->block_end(),
                                BBGetter);
  }
  /// Return a range over the incoming basic blocks.
  ///
  /// \return A range over the incoming basic blocks.
  iterator_range<const_block_iterator> blocks() const {
    return make_range(block_begin(), block_end());
  }

  /// Return a range over the incoming values.
  ///
  /// \return A range over the incoming values.
  op_range incoming_values() { return operands(); }

  /// Return a range over the incoming values.
  ///
  /// \return A range over the incoming values.
  const_op_range incoming_values() const { return operands(); }

  /// Return the number of PHI incoming values.
  ///
  /// \return The number of PHI incoming values.
  unsigned getNumIncomingValues() const {
    return cast<llvm::PHINode>(Val)->getNumIncomingValues();
  }
  /// Return the incoming value at index \p Idx.
  ///
  /// \return The incoming value at index \p Idx.
  /// \param Idx Incoming-value index.
  LLVM_ABI Value *getIncomingValue(unsigned Idx) const;
  /// Set an incoming PHI value.
  /// \param Idx Index.
  /// \param V Value or flag.
  LLVM_ABI void setIncomingValue(unsigned Idx, Value *V);
  /// Map an incoming-value index to an operand number.
  ///
  /// \return The requested integer value.
  /// \param Idx Index.
  static unsigned getOperandNumForIncomingValue(unsigned Idx) {
    return llvm::PHINode::getOperandNumForIncomingValue(Idx);
  }
  /// Map an operand number to an incoming-value index.
  ///
  /// \return The requested integer value.
  /// \param Idx Index.
  static unsigned getIncomingValueNumForOperand(unsigned Idx) {
    return llvm::PHINode::getIncomingValueNumForOperand(Idx);
  }
  /// Return the incoming basic block at index \p Idx.
  ///
  /// \return The incoming basic block at index \p Idx.
  /// \param Idx Incoming-value index.
  LLVM_ABI BasicBlock *getIncomingBlock(unsigned Idx) const;
  /// Return the incoming basic block for use \p U.
  ///
  /// \return The incoming basic block for use \p U.
  /// \param U Use of an incoming value.
  LLVM_ABI BasicBlock *getIncomingBlock(const Use &U) const;

  /// Set an incoming PHI basic block.
  /// \param Idx Index.
  /// \param BB Basic block.
  LLVM_ABI void setIncomingBlock(unsigned Idx, BasicBlock *BB);

  /// Add an incoming value and basic block.
  /// \param V Value or flag.
  /// \param BB Basic block.
  LLVM_ABI void addIncoming(Value *V, BasicBlock *BB);

  /// Remove the incoming value at index \p Idx and return it.
  ///
  /// \return The removed incoming value.
  /// \param Idx Incoming-value index to remove.
  LLVM_ABI Value *removeIncomingValue(unsigned Idx);
  /// Remove the incoming value for basic block \p BB and return it.
  ///
  /// \return The removed incoming value.
  /// \param BB Incoming basic block whose value is removed.
  LLVM_ABI Value *removeIncomingValue(BasicBlock *BB);

  /// Return the index of an incoming basic block.
  ///
  /// \return The index of an incoming basic block.
  /// \param BB Basic block.
  LLVM_ABI int getBasicBlockIndex(const BasicBlock *BB) const;
  /// Return the incoming value for basic block \p BB.
  ///
  /// \return The incoming value for basic block \p BB.
  /// \param BB Incoming basic block.
  LLVM_ABI Value *getIncomingValueForBlock(const BasicBlock *BB) const;

  /// Return the constant value if all incomings are the same constant.
  ///
  /// \return The constant value if all incomings match, otherwise nullptr.
  LLVM_ABI Value *hasConstantValue() const;

  /// Return true if all incomings are the same constant or undef.
  ///
  /// \return True if all incomings are the same constant or undef.
  bool hasConstantOrUndefValue() const {
    return cast<llvm::PHINode>(Val)->hasConstantOrUndefValue();
  }
  /// Return true if every predecessor has an incoming value.
  ///
  /// \return True if every predecessor has an incoming value.
  bool isComplete() const { return cast<llvm::PHINode>(Val)->isComplete(); }
  /// Replace all uses of an incoming basic block.
  /// \param Old Basic block to replace.
  /// \param New Replacement basic block or value.
  LLVM_ABI void replaceIncomingBlockWith(const BasicBlock *Old,
                                         BasicBlock *New);
  /// Remove incoming values matching a predicate.
  /// \param Predicate Comparison predicate.
  LLVM_ABI void removeIncomingValueIf(function_ref<bool(unsigned)> Predicate);
  // TODO: Implement
  // void copyIncomingBlocks(iterator_range<const_block_iterator> BBRange,
  //                         uint32_t ToIdx = 0)
};

// Wraps a static function that takes a single Predicate parameter
// LLVMValType should be the type of the wrapped class
#define WRAP_STATIC_PREDICATE(FunctionName)                                    \
  static auto FunctionName(Predicate P) { return LLVMValType::FunctionName(P); }
// Wraps a member function that takes no parameters
// LLVMValType should be the type of the wrapped class
#define WRAP_MEMBER(FunctionName)                                              \
  auto FunctionName() const { return cast<LLVMValType>(Val)->FunctionName(); }
// Wraps both--a common idiom in the CmpInst classes
#define WRAP_BOTH(FunctionName)                                                \
  WRAP_STATIC_PREDICATE(FunctionName)                                          \
  WRAP_MEMBER(FunctionName)

/// Base class for SandboxIR integer and floating-point compares.
class CmpInst : public SingleLLVMInstructionImpl<llvm::CmpInst> {
protected:
  /// Underlying LLVM IR instruction type wrapped by this class.
  using LLVMValType = llvm::CmpInst;
  /// Use Context::createCmpInst(). Don't call the constructor directly.
  /// \param CI Underlying LLVM CmpInst to wrap.
  /// \param Ctx SandboxIR context.
  /// \param Id SandboxIR class identifier.
  /// \param Opc Instruction opcode.
  CmpInst(llvm::CmpInst *CI, Context &Ctx, ClassID Id, Opcode Opc)
      : SingleLLVMInstructionImpl(Id, Opc, CI, Ctx) {}
  friend Context; // for CmpInst()
  /// Shared implementation used by CmpInst::create helpers.
  ///
  /// \return The newly created instruction.
  /// \param Cond Condition value.
  /// \param True True value.
  /// \param False False value.
  /// \param Name Name of the new instruction.
  /// \param Builder LLVM IR builder used for creation.
  /// \param Ctx SandboxIR context.
  LLVM_ABI static Value *createCommon(Value *Cond, Value *True, Value *False,
                                      const Twine &Name, IRBuilder<> &Builder,
                                      Context &Ctx);

public:
  /// Comparison predicate kind.
  using Predicate = llvm::CmpInst::Predicate;

  /// Create and insert a new CmpInst.
  ///
  /// \return The newly created instruction.
  /// \param Pred Comparison predicate.
  /// \param S1 First operand.
  /// \param S2 Second operand.
  /// \param Pos Insert position for the new instruction.
  /// \param Ctx SandboxIR context.
  /// \param Name Name of the new instruction.
  LLVM_ABI static Value *create(Predicate Pred, Value *S1, Value *S2,
                                InsertPosition Pos, Context &Ctx,
                                const Twine &Name = "");
  /// Create a CmpInst copying flags from \p FlagsSource.
  ///
  /// \return The newly created instruction.
  /// \param Pred Comparison predicate.
  /// \param S1 First operand.
  /// \param S2 Second operand.
  /// \param FlagsSource Instruction whose flags are copied.
  /// \param Pos Insert position for the new instruction.
  /// \param Ctx SandboxIR context.
  /// \param Name Name of the new instruction.
  LLVM_ABI static Value *createWithCopiedFlags(Predicate Pred, Value *S1,
                                               Value *S2,
                                               const Instruction *FlagsSource,
                                               InsertPosition Pos, Context &Ctx,
                                               const Twine &Name = "");
  /// Set the comparison predicate.
  /// \param P Comparison predicate.
  LLVM_ABI void setPredicate(Predicate P);
  /// Swap the instruction's operands.
  LLVM_ABI void swapOperands();

  /// Return the comparison predicate.
  ///
  /// \return The comparison predicate.
  WRAP_MEMBER(getPredicate);
  /// Return true if the predicate is a floating-point predicate.
  ///
  /// \return True if the predicate is a floating-point predicate.
  /// \param P Comparison predicate.
  WRAP_BOTH(isFPPredicate);
  /// Return true if the predicate is an integer predicate.
  ///
  /// \return True if the predicate is an integer predicate.
  /// \param P Comparison predicate.
  WRAP_BOTH(isIntPredicate);
  /// Return the name of a comparison predicate.
  ///
  /// \return The name of a comparison predicate.
  /// \param P Comparison predicate.
  WRAP_STATIC_PREDICATE(getPredicateName);
  /// Return the inverse of a comparison predicate.
  ///
  /// \return The inverse of a comparison predicate.
  /// \param P Comparison predicate.
  WRAP_BOTH(getInversePredicate);
  /// Return the ordered form of a floating-point predicate.
  ///
  /// \return The ordered form of a floating-point predicate.
  /// \param P Comparison predicate.
  WRAP_BOTH(getOrderedPredicate);
  /// Return the unordered form of a floating-point predicate.
  ///
  /// \return The unordered form of a floating-point predicate.
  /// \param P Comparison predicate.
  WRAP_BOTH(getUnorderedPredicate);
  /// Return the predicate with operands swapped.
  ///
  /// \return The predicate with operands swapped.
  /// \param P Comparison predicate.
  WRAP_BOTH(getSwappedPredicate);
  /// Return true if the predicate is strict.
  ///
  /// \return True if the predicate is strict.
  /// \param P Comparison predicate.
  WRAP_BOTH(isStrictPredicate);
  /// Return true if the predicate is non-strict.
  ///
  /// \return True if the predicate is non-strict.
  /// \param P Comparison predicate.
  WRAP_BOTH(isNonStrictPredicate);
  /// Return the strict form of a relational predicate.
  ///
  /// \return The strict form of a relational predicate.
  /// \param P Comparison predicate.
  WRAP_BOTH(getStrictPredicate);
  /// Return the non-strict form of a relational predicate.
  ///
  /// \return The non-strict form of a relational predicate.
  /// \param P Comparison predicate.
  WRAP_BOTH(getNonStrictPredicate);
  /// Return the predicate with flipped strictness.
  ///
  /// \return The predicate with flipped strictness.
  /// \param P Comparison predicate.
  WRAP_BOTH(getFlippedStrictnessPredicate);
  /// Return true if the predicate is commutative.
  ///
  /// \return True if the predicate is commutative.
  WRAP_MEMBER(isCommutative);
  /// Return true if the predicate is an equality comparison.
  ///
  /// \return True if the predicate is an equality comparison.
  /// \param P Comparison predicate.
  WRAP_BOTH(isEquality);
  /// Return true if the predicate is relational.
  ///
  /// \return True if the predicate is relational.
  /// \param P Comparison predicate.
  WRAP_BOTH(isRelational);
  /// Return true if the predicate is signed.
  ///
  /// \return True if the predicate is signed.
  /// \param P Comparison predicate.
  WRAP_BOTH(isSigned);
  /// Return true if the predicate is true when operands are equal.
  ///
  /// \return True if the predicate is true when operands are equal.
  /// \param P Comparison predicate.
  WRAP_BOTH(isTrueWhenEqual);
  /// Return true if the predicate is false when operands are equal.
  ///
  /// \return True if the predicate is false when operands are equal.
  /// \param P Comparison predicate.
  WRAP_BOTH(isFalseWhenEqual);
  /// Return true if the predicate is unsigned.
  ///
  /// \return True if the predicate is unsigned.
  /// \param P Comparison predicate.
  WRAP_BOTH(isUnsigned);
  /// Return true if the floating-point predicate is ordered.
  ///
  /// \return True if the floating-point predicate is ordered.
  /// \param P Comparison predicate.
  WRAP_STATIC_PREDICATE(isOrdered);
  /// Return true if the floating-point predicate is unordered.
  ///
  /// \return True if the floating-point predicate is unordered.
  /// \param P Comparison predicate.
  WRAP_STATIC_PREDICATE(isUnordered);
  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test for CmpInst.
  static bool classof(const Value *From) {
    return From->getSubclassID() == ClassID::ICmp ||
           From->getSubclassID() == ClassID::FCmp;
  }

  /// Create a result type for fcmp/icmp.
  ///
  /// \return The compare result type derived from \p OpndType.
  /// \param OpndType Operand type used to derive the compare result type.
  LLVM_ABI static Type *makeCmpResultType(Type *OpndType);

#ifndef NDEBUG
  /// Dump this instruction to an output stream.
  /// \param OS Output stream.
  void dumpOS(raw_ostream &OS) const override;
  /// Dump this instruction to the debug stream.
  LLVM_DUMP_METHOD void dump() const;
#endif
};

/// SandboxIR wrapper for an LLVM icmp instruction.
class ICmpInst : public CmpInst {
  /// Use Context::createICmpInst(). Don't call the constructor directly.
  /// \param CI Underlying LLVM CmpInst to wrap.
  /// \param Ctx SandboxIR context.
  ICmpInst(llvm::ICmpInst *CI, Context &Ctx)
      : CmpInst(CI, Ctx, ClassID::ICmp, Opcode::ICmp) {}
  friend class Context; // For constructor.
  /// Underlying LLVM IR instruction type wrapped by this class.
  using LLVMValType = llvm::ICmpInst;

public:
  /// Swap the instruction's operands.
  LLVM_ABI void swapOperands();

  /// Return the signed form of an integer predicate.
  ///
  /// \return The signed form of an integer predicate.
  /// \param P Comparison predicate.
  WRAP_BOTH(getSignedPredicate);
  /// Return the unsigned form of an integer predicate.
  ///
  /// \return The unsigned form of an integer predicate.
  /// \param P Comparison predicate.
  WRAP_BOTH(getUnsignedPredicate);
  /// Return the predicate with flipped signedness.
  ///
  /// \return The predicate with flipped signedness.
  /// \param P Comparison predicate.
  WRAP_BOTH(getFlippedSignednessPredicate);
  /// Return true if the predicate is an equality comparison.
  ///
  /// \return True if the predicate is an equality comparison.
  /// \param P Comparison predicate.
  WRAP_BOTH(isEquality);
  /// Return true if the predicate is commutative.
  ///
  /// \return True if the predicate is commutative.
  WRAP_MEMBER(isCommutative);
  /// Return true if the predicate is relational.
  ///
  /// \return True if the predicate is relational.
  WRAP_MEMBER(isRelational);
  /// Return true if the predicate is a greater-than compare.
  ///
  /// \return True if the predicate is a greater-than compare.
  /// \param P Comparison predicate.
  WRAP_STATIC_PREDICATE(isGT);
  /// Return true if the predicate is a less-than compare.
  ///
  /// \return True if the predicate is a less-than compare.
  /// \param P Comparison predicate.
  WRAP_STATIC_PREDICATE(isLT);
  /// Return true if the predicate is a greater-or-equal compare.
  ///
  /// \return True if the predicate is a greater-or-equal compare.
  /// \param P Comparison predicate.
  WRAP_STATIC_PREDICATE(isGE);
  /// Return true if the predicate is a less-or-equal compare.
  ///
  /// \return True if the predicate is a less-or-equal compare.
  /// \param P Comparison predicate.
  WRAP_STATIC_PREDICATE(isLE);
  /// Return true if one icmp implies another with matching operands.
  ///
  /// \return True if one icmp implies another with matching operands.
  /// \param Pred1 The Pred1 parameter.
  /// \param Pred2 The Pred2 parameter.
  static std::optional<bool> isImpliedByMatchingCmp(CmpPredicate Pred1,
                                                    CmpPredicate Pred2) {
    return llvm::ICmpInst::isImpliedByMatchingCmp(Pred1, Pred2);
  }

  /// Return the range of valid predicates for this compare kind.
  ///
  /// \return The range of valid predicates for this compare kind.
  static auto predicates() { return llvm::ICmpInst::predicates(); }
  /// Evaluate the comparison of two constants.
  ///
  /// \return True if the comparison holds for the given constants.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Pred Comparison predicate.
  static bool compare(const APInt &LHS, const APInt &RHS,
                      ICmpInst::Predicate Pred) {
    return llvm::ICmpInst::compare(LHS, RHS, Pred);
  }

  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test for ICmpInst.
  static bool classof(const Value *From) {
    return From->getSubclassID() == ClassID::ICmp;
  }
};

/// SandboxIR wrapper for an LLVM fcmp instruction.
class FCmpInst : public CmpInst {
  /// Use Context::createFCmpInst(). Don't call the constructor directly.
  /// \param CI Underlying LLVM CmpInst to wrap.
  /// \param Ctx SandboxIR context.
  FCmpInst(llvm::FCmpInst *CI, Context &Ctx)
      : CmpInst(CI, Ctx, ClassID::FCmp, Opcode::FCmp) {}
  friend class Context; // For constructor.
  /// Underlying LLVM IR instruction type wrapped by this class.
  using LLVMValType = llvm::FCmpInst;

public:
  /// Swap the instruction's operands.
  LLVM_ABI void swapOperands();

  /// Return true if the predicate is an equality comparison.
  ///
  /// \return True if the predicate is an equality comparison.
  /// \param P Comparison predicate.
  WRAP_BOTH(isEquality);
  /// Return true if the predicate is commutative.
  ///
  /// \return True if the predicate is commutative.
  WRAP_MEMBER(isCommutative);
  /// Return true if the predicate is relational.
  ///
  /// \return True if the predicate is relational.
  WRAP_MEMBER(isRelational);
  /// Return the range of valid predicates for this compare kind.
  ///
  /// \return The range of valid predicates for this compare kind.
  static auto predicates() { return llvm::FCmpInst::predicates(); }
  /// Evaluate the comparison of two constants.
  ///
  /// \return True if the comparison holds for the given constants.
  /// \param LHS Left-hand side operand.
  /// \param RHS Right-hand side operand.
  /// \param Pred Comparison predicate.
  static bool compare(const APFloat &LHS, const APFloat &RHS,
                      FCmpInst::Predicate Pred) {
    return llvm::FCmpInst::compare(LHS, RHS, Pred);
  }

  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test for FCmpInst.
  static bool classof(const Value *From) {
    return From->getSubclassID() == ClassID::FCmp;
  }
};

#undef WRAP_STATIC_PREDICATE
#undef WRAP_MEMBER
#undef WRAP_BOTH

/// SandboxIR instruction with no specialized API surface.
///
/// An LLVM Instruction that has no SandboxIR equivalent class gets mapped to
/// an OpaqueInst.
class OpaqueInst : public SingleLLVMInstructionImpl<llvm::Instruction> {
  /// Construct an OpaqueInst wrapping LLVM IR instruction \p I.
  /// \param I Underlying LLVM Instruction to wrap.
  /// \param Ctx SandboxIR context.
  OpaqueInst(llvm::Instruction *I, sandboxir::Context &Ctx)
      : SingleLLVMInstructionImpl(ClassID::Opaque, Opcode::Opaque, I, Ctx) {}
  /// Construct an OpaqueInst with an explicit subclass id.
  /// \param SubclassID SandboxIR class identifier.
  /// \param I Underlying LLVM Instruction to wrap.
  /// \param Ctx SandboxIR context.
  OpaqueInst(ClassID SubclassID, llvm::Instruction *I, sandboxir::Context &Ctx)
      : SingleLLVMInstructionImpl(SubclassID, Opcode::Opaque, I, Ctx) {}
  friend class Context; // For constructor.

public:
  /// For isa/dyn_cast.
  ///
  /// \return True if \p From is an instance of this class.
  /// \param From Value to test for OpaqueInst.
  static bool classof(const sandboxir::Value *From) {
    return From->getSubclassID() == ClassID::Opaque;
  }
};

//===----------------------------------------------------------------------===//
//                          Helper functions
//===----------------------------------------------------------------------===//

/// Return the address space of a load or store pointer.
///
/// \return The address space of a load or store pointer.
/// \param I Load or store instruction.
inline unsigned getLoadStoreAddressSpace(const Instruction *I) {
  assert((isa<LoadInst>(I) || isa<StoreInst>(I)) &&
         "Expected Load or Store instruction");
  if (auto *LI = dyn_cast<LoadInst>(I))
    return LI->getPointerAddressSpace();
  return cast<StoreInst>(I)->getPointerAddressSpace();
}

} // namespace llvm::sandboxir

#endif // LLVM_SANDBOXIR_INSTRUCTION_H
