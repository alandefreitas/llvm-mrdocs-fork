//===- FastISel.h - Definition of the FastISel class ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the FastISel class.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_FASTISEL_H
#define LLVM_CODEGEN_FASTISEL_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/InstrTypes.h"
#include <cstdint>
#include <utility>

namespace llvm {

class AllocaInst;
class Instruction;
class IntrinsicInst;
class BasicBlock;
class CallInst;
class Constant;
class ConstantFP;
class DataLayout;
class FunctionLoweringInfo;
class LoadInst;
class MachineConstantPool;
class MachineFrameInfo;
class MachineFunction;
class MachineInstr;
class MachineMemOperand;
class MachineOperand;
class MachineRegisterInfo;
class MCContext;
class MCInstrDesc;
class MCSymbol;
class TargetInstrInfo;
class TargetLibraryInfo;
class TargetMachine;
class MCRegisterClass;
/// Register class type used throughout CodeGen.
using TargetRegisterClass = MCRegisterClass;
class TargetRegisterInfo;
class Type;
class User;
class Value;

/// This is a fast-path instruction selection class that generates poor
/// code and doesn't support illegal types or non-trivial lowering, but runs
/// quickly.
class LLVM_ABI FastISel {
public:
  /// A single argument in a FastISel call argument list.
  using ArgListEntry = TargetLoweringBase::ArgListEntry;
  /// A list of arguments for a call being lowered.
  using ArgListTy = TargetLoweringBase::ArgListTy;
  /// Holds the information needed to lower a call in FastISel.
  struct CallLoweringInfo {
    /// Return type of the call.
    Type *RetTy = nullptr;
    /// True if the return value is sign-extended.
    bool RetSExt : 1;
    /// True if the return value is zero-extended.
    bool RetZExt : 1;
    /// True if the callee is a vararg function.
    bool IsVarArg : 1;
    /// True if the return value is returned in-register.
    bool IsInReg : 1;
    /// True if the callee is marked noreturn.
    bool DoesNotReturn : 1;
    /// True if the call's return value has uses.
    bool IsReturnValueUsed : 1;
    /// True if this call is a patchpoint.
    bool IsPatchPoint : 1;

    /// True if this call should be lowered as a tail call.
    ///
    /// Implementations of FastLowerCall that perform tail call conversions
    /// should modify this.
    bool IsTailCall = false;

    /// Number of fixed (non-varargs) arguments.
    unsigned NumFixedArgs = -1;
    /// Calling convention of the call.
    CallingConv::ID CallConv = CallingConv::C;
    /// Callee value, if the target is an IR value.
    const Value *Callee = nullptr;
    /// Callee symbol, if the target is an MCSymbol.
    MCSymbol *Symbol = nullptr;
    /// Argument list for the call.
    ArgListTy Args;
    /// Original call instruction, if lowering an IR call.
    const CallBase *CB = nullptr;
    /// Emitted call machine instruction.
    MachineInstr *Call = nullptr;
    /// First virtual register holding the call result.
    Register ResultReg;
    /// Number of registers used for the call result.
    unsigned NumResultRegs = 0;

    /// Outgoing argument values.
    SmallVector<Value *, 16> OutVals;
    /// Outgoing argument flags.
    SmallVector<ISD::ArgFlagsTy, 16> OutFlags;
    /// Registers assigned to outgoing arguments.
    SmallVector<Register, 16> OutRegs;
    /// Incoming return-value descriptions.
    SmallVector<ISD::InputArg, 4> Ins;
    /// Registers assigned to the call result.
    SmallVector<Register, 4> InRegs;

    /// Construct a CallLoweringInfo with default flags.
    CallLoweringInfo()
        : RetSExt(false), RetZExt(false), IsVarArg(false), IsInReg(false),
          DoesNotReturn(false), IsReturnValueUsed(true), IsPatchPoint(false) {}

    /// Configure this call from a CallBase with a Value callee.
    ///
    /// \param ResultTy Return type of the call.
    /// \param FuncTy Function type of the callee.
    /// \param Target Callee value.
    /// \param ArgsList Arguments to pass.
    /// \param Call IR call providing attributes and calling convention.
    /// \return This CallLoweringInfo, for chaining.
    CallLoweringInfo &setCallee(Type *ResultTy, FunctionType *FuncTy,
                                const Value *Target, ArgListTy &&ArgsList,
                                const CallBase &Call) {
      RetTy = ResultTy;
      Callee = Target;

      IsInReg = Call.hasRetAttr(Attribute::InReg);
      DoesNotReturn = Call.doesNotReturn();
      IsVarArg = FuncTy->isVarArg();
      IsReturnValueUsed = !Call.use_empty();
      RetSExt = Call.hasRetAttr(Attribute::SExt);
      RetZExt = Call.hasRetAttr(Attribute::ZExt);

      CallConv = Call.getCallingConv();
      Args = std::move(ArgsList);
      NumFixedArgs = FuncTy->getNumParams();

      CB = &Call;

      return *this;
    }

    /// Configure this call from a CallBase with an MCSymbol callee.
    ///
    /// \param ResultTy Return type of the call.
    /// \param FuncTy Function type of the callee.
    /// \param Target Callee symbol.
    /// \param ArgsList Arguments to pass.
    /// \param Call IR call providing attributes and calling convention.
    /// \param FixedArgs Number of fixed arguments, or ~0U to use the function
    /// type.
    /// \return This CallLoweringInfo, for chaining.
    CallLoweringInfo &setCallee(Type *ResultTy, FunctionType *FuncTy,
                                MCSymbol *Target, ArgListTy &&ArgsList,
                                const CallBase &Call,
                                unsigned FixedArgs = ~0U) {
      RetTy = ResultTy;
      Callee = Call.getCalledOperand();
      Symbol = Target;

      IsInReg = Call.hasRetAttr(Attribute::InReg);
      DoesNotReturn = Call.doesNotReturn();
      IsVarArg = FuncTy->isVarArg();
      IsReturnValueUsed = !Call.use_empty();
      RetSExt = Call.hasRetAttr(Attribute::SExt);
      RetZExt = Call.hasRetAttr(Attribute::ZExt);

      CallConv = Call.getCallingConv();
      Args = std::move(ArgsList);
      NumFixedArgs = (FixedArgs == ~0U) ? FuncTy->getNumParams() : FixedArgs;

      CB = &Call;

      return *this;
    }

    /// Configure this call with a calling convention and Value callee.
    ///
    /// \param CC Calling convention of the call.
    /// \param ResultTy Return type of the call.
    /// \param Target Callee value.
    /// \param ArgsList Arguments to pass.
    /// \param FixedArgs Number of fixed arguments, or ~0U to use the argument
    /// list size.
    /// \return This CallLoweringInfo, for chaining.
    CallLoweringInfo &setCallee(CallingConv::ID CC, Type *ResultTy,
                                const Value *Target, ArgListTy &&ArgsList,
                                unsigned FixedArgs = ~0U) {
      RetTy = ResultTy;
      Callee = Target;
      CallConv = CC;
      Args = std::move(ArgsList);
      NumFixedArgs = (FixedArgs == ~0U) ? Args.size() : FixedArgs;
      return *this;
    }

    /// Configure this call with a mangled symbol name as the callee.
    ///
    /// \param DL Data layout used to mangle \p Target.
    /// \param Ctx Context used to create or look up the symbol.
    /// \param CC Calling convention of the call.
    /// \param ResultTy Return type of the call.
    /// \param Target Unmangled callee name.
    /// \param ArgsList Arguments to pass.
    /// \param FixedArgs Number of fixed arguments, or ~0U to use the argument
    /// list size.
    /// \return This CallLoweringInfo, for chaining.
    LLVM_ABI CallLoweringInfo &setCallee(const DataLayout &DL, MCContext &Ctx,
                                         CallingConv::ID CC, Type *ResultTy,
                                         StringRef Target, ArgListTy &&ArgsList,
                                         unsigned FixedArgs = ~0U);

    /// Configure this call with a calling convention and MCSymbol callee.
    ///
    /// \param CC Calling convention of the call.
    /// \param ResultTy Return type of the call.
    /// \param Target Callee symbol.
    /// \param ArgsList Arguments to pass.
    /// \param FixedArgs Number of fixed arguments, or ~0U to use the argument
    /// list size.
    /// \return This CallLoweringInfo, for chaining.
    CallLoweringInfo &setCallee(CallingConv::ID CC, Type *ResultTy,
                                MCSymbol *Target, ArgListTy &&ArgsList,
                                unsigned FixedArgs = ~0U) {
      RetTy = ResultTy;
      Symbol = Target;
      CallConv = CC;
      Args = std::move(ArgsList);
      NumFixedArgs = (FixedArgs == ~0U) ? Args.size() : FixedArgs;
      return *this;
    }

    /// Set whether this call should be a tail call.
    ///
    /// \param Value True if the call is a tail call.
    /// \return This CallLoweringInfo, for chaining.
    CallLoweringInfo &setTailCall(bool Value = true) {
      IsTailCall = Value;
      return *this;
    }

    /// Set whether this call is a patchpoint.
    ///
    /// \param Value True if the call is a patchpoint.
    /// \return This CallLoweringInfo, for chaining.
    CallLoweringInfo &setIsPatchPoint(bool Value = true) {
      IsPatchPoint = Value;
      return *this;
    }

    /// Return the list of call arguments.
    ///
    /// \return The argument list for this call.
    ArgListTy &getArgs() { return Args; }

    /// Clear outgoing argument values, flags, and registers.
    void clearOuts() {
      OutVals.clear();
      OutFlags.clear();
      OutRegs.clear();
    }

    /// Clear incoming return-value descriptions and registers.
    void clearIns() {
      Ins.clear();
      InRegs.clear();
    }
  };

protected:
  /// Map from IR values to registers materialized in the current block.
  DenseMap<const Value *, Register> LocalValueMap;
  /// Per-function lowering state shared with SelectionDAG ISel.
  FunctionLoweringInfo &FuncInfo;
  /// Machine function currently being selected.
  MachineFunction *MF;
  /// Register information for the current machine function.
  MachineRegisterInfo &MRI;
  /// Frame information for the current machine function.
  MachineFrameInfo &MFI;
  /// Constant pool for the current machine function.
  MachineConstantPool &MCP;
  /// Metadata attached to newly created machine instructions.
  MIMetadata MIMD;
  /// Target machine for the current compilation.
  const TargetMachine &TM;
  /// Data layout of the current module.
  const DataLayout &DL;
  /// Target instruction information for the current subtarget.
  const TargetInstrInfo &TII;
  /// Target lowering interface for the current subtarget.
  const TargetLowering &TLI;
  /// Target register information for the current subtarget.
  const TargetRegisterInfo &TRI;
  /// Target library information for the current function.
  const TargetLibraryInfo *LibInfo;
  /// Libcall lowering information for the current target.
  const LibcallLoweringInfo *LibcallLowering;
  /// If true, skip generic FastISel and use only the target hook.
  bool SkipTargetIndependentISel;

  /// Last instruction that materialized a constant in this block.
  ///
  /// The position of the last instruction for materializing constants
  /// for use in the current block. It resets to EmitStartPt when it makes sense
  /// (for example, it's usually profitable to avoid function calls between the
  /// definition and the use)
  MachineInstr *LastLocalValue = nullptr;

  /// First instruction in the block that may emit local values.
  ///
  /// The top most instruction in the current block that is allowed for
  /// emitting local variables. LastLocalValue resets to EmitStartPt when it
  /// makes sense (for example, on function calls)
  MachineInstr *EmitStartPt = nullptr;

public:
  /// Destroy the FastISel instance.
  virtual ~FastISel();

  /// Return the position of the last instruction emitted for
  /// materializing constants for use in the current block.
  ///
  /// \return The last local-value materialization instruction, or nullptr.
  MachineInstr *getLastLocalValue() { return LastLocalValue; }

  /// Update the position of the last instruction emitted for
  /// materializing constants for use in the current block.
  ///
  /// \param I Instruction to treat as both the emit-start and last local value.
  void setLastLocalValue(MachineInstr *I) {
    EmitStartPt = I;
    LastLocalValue = I;
  }

  /// Set the current block to which generated machine instructions will
  /// be appended.
  void startNewBlock();

  /// Flush the local value map.
  void finishBasicBlock();

  /// Return current debug location information.
  ///
  /// \return The debug location attached to newly created instructions.
  DebugLoc getCurDebugLoc() const { return MIMD.getDL(); }

  /// Do "fast" instruction selection for function arguments and append
  /// the machine instructions to the current block. Returns true when
  /// successful.
  ///
  /// \return True if argument lowering succeeded.
  bool lowerArguments();

  /// Select an LLVM IR instruction into the current machine block.
  ///
  /// Do "fast" instruction selection for the given LLVM IR instruction
  /// and append the generated machine instructions to the current block.
  /// Returns true if selection was successful.
  ///
  /// \param I Instruction to select.
  /// \return True if selection was successful.
  bool selectInstruction(const Instruction *I);

  /// Select an LLVM IR operator into the current machine block.
  ///
  /// Do "fast" instruction selection for the given LLVM IR operator
  /// (Instruction or ConstantExpr), and append generated machine instructions
  /// to the current block. Return true if selection was successful.
  ///
  /// \param I Operator (instruction or constant expression) to select.
  /// \param Opcode Opcode of \p I.
  /// \return True if selection was successful.
  bool selectOperator(const User *I, unsigned Opcode);

  /// Create a virtual register and arrange for it to be assigned the
  /// value for the given LLVM value.
  ///
  /// \param V Value to materialize or look up.
  /// \return Virtual register holding the value of \p V.
  Register getRegForValue(const Value *V);

  /// Look up the value to see if its value is already cached in a
  /// register. It may be defined by instructions across blocks or defined
  /// locally.
  ///
  /// \param V Value to look up.
  /// \return Cached register for \p V, or an empty register if none.
  Register lookUpRegForValue(const Value *V);

  /// This is a wrapper around getRegForValue that also takes care of
  /// truncating or sign-extending the given getelementptr index value.
  ///
  /// \param PtrVT Value type of the pointer being indexed.
  /// \param Idx GEP index value to materialize.
  /// \return Register holding the truncated or extended GEP index.
  Register getRegForGEPIndex(MVT PtrVT, const Value *Idx);

  /// Try to fold a load into a later instruction.
  ///
  /// We're checking to see if we can fold \p LI into \p FoldInst. Note
  /// that we could have a sequence where multiple LLVM IR instructions are
  /// folded into the same machineinstr.  For example we could have:
  ///
  ///   A: x = load i32 *P
  ///   B: y = icmp A, 42
  ///   C: br y, ...
  ///
  /// In this scenario, \p LI is "A", and \p FoldInst is "C".  We know about "B"
  /// (and any other folded instructions) because it is between A and C.
  ///
  /// If we succeed folding, return true.
  ///
  /// \param LI Load instruction considered for folding.
  /// \param FoldInst Instruction that would consume the loaded value.
  /// \return True if the load was folded successfully.
  bool tryToFoldLoad(const LoadInst *LI, const Instruction *FoldInst);

  /// Try to fold a load into an operand of a machine instruction.
  ///
  /// The specified machine instr operand is a vreg, and that vreg is
  /// being provided by the specified load instruction.  If possible, try to
  /// fold the load as an operand to the instruction, returning true if
  /// possible.
  ///
  /// This method should be implemented by targets.
  ///
  /// \param MI Machine instruction that would consume the loaded value.
  /// \param OpNo Operand number of the vreg provided by the load.
  /// \param LI Load instruction that defines the vreg operand.
  /// \return True if the load was folded into \p MI.
  virtual bool tryToFoldLoadIntoMI(MachineInstr *MI, unsigned OpNo,
                                   const LoadInst *LI) {
    return false;
  }

  /// Reset InsertPt to prepare for inserting instructions into the
  /// current block.
  void recomputeInsertPt();

  /// Remove all dead instructions between the I and E.
  ///
  /// \param I Start of the instruction range to inspect.
  /// \param E End of the instruction range to inspect.
  void removeDeadCode(MachineBasicBlock::iterator I,
                      MachineBasicBlock::iterator E);

  /// Saved insert point for restoring after local-value emission.
  using SavePoint = MachineBasicBlock::iterator;

  /// Prepare InsertPt to begin inserting instructions into the local
  /// value area and return the old insert position.
  ///
  /// \return The previous insert position, for restoring later.
  SavePoint enterLocalValueArea();

  /// Reset InsertPt to the given old insert position.
  ///
  /// \param Old Insert point to restore.
  void leaveLocalValueArea(SavePoint Old);

  /// Target-independent lowering of non-instruction debug info associated with
  /// this instruction.
  ///
  /// \param II Instruction whose attached debug info should be lowered.
  void handleDbgInfo(const Instruction *II);

protected:
  /// Construct a FastISel for the given function lowering state.
  ///
  /// \param FuncInfo Per-function lowering state.
  /// \param LibInfo Target library information.
  /// \param LibcallLowering Libcall lowering information for the target.
  /// \param SkipTargetIndependentISel If true, skip generic FastISel and go
  /// straight to the target hook.
  explicit FastISel(FunctionLoweringInfo &FuncInfo,
                    const TargetLibraryInfo *LibInfo,
                    const LibcallLoweringInfo *LibcallLowering,
                    bool SkipTargetIndependentISel = false);

  /// Select an instruction that generic FastISel could not handle.
  ///
  /// This method is called by target-independent code when the normal
  /// FastISel process fails to select an instruction. This gives targets a
  /// chance to emit code for anything that doesn't fit into FastISel's
  /// framework. It returns true if it was successful.
  ///
  /// \param I Instruction that generic selection failed to handle.
  /// \return True if target-specific selection succeeded.
  virtual bool fastSelectInstruction(const Instruction *I) = 0;

  /// This method is called by target-independent code to do target-
  /// specific argument lowering. It returns true if it was successful.
  ///
  /// \return True if target-specific argument lowering succeeded.
  virtual bool fastLowerArguments();

  /// This method is called by target-independent code to do target-
  /// specific call lowering. It returns true if it was successful.
  ///
  /// \param CLI Call lowering description to emit.
  /// \return True if target-specific call lowering succeeded.
  virtual bool fastLowerCall(CallLoweringInfo &CLI);

  /// This method is called by target-independent code to do target-
  /// specific intrinsic lowering. It returns true if it was successful.
  ///
  /// \param II Intrinsic to lower.
  /// \return True if target-specific intrinsic lowering succeeded.
  virtual bool fastLowerIntrinsicCall(const IntrinsicInst *II);

  /// This method is called by target-independent code to request that an
  /// instruction with the given type and opcode be emitted.
  ///
  /// \param VT Value type of the operation.
  /// \param RetVT Value type of the result.
  /// \param Opcode ISD opcode to emit.
  /// \return Result register of the emitted instruction, or an empty register.
  virtual Register fastEmit_(MVT VT, MVT RetVT, unsigned Opcode);

  /// This method is called by target-independent code to request that an
  /// instruction with the given type, opcode, and register operand be emitted.
  ///
  /// \param VT Value type of the operation.
  /// \param RetVT Value type of the result.
  /// \param Opcode ISD opcode to emit.
  /// \param Op0 Register operand.
  /// \return Result register of the emitted instruction, or an empty register.
  virtual Register fastEmit_r(MVT VT, MVT RetVT, unsigned Opcode, Register Op0);

  /// This method is called by target-independent code to request that an
  /// instruction with the given type, opcode, and register operands be emitted.
  ///
  /// \param VT Value type of the operation.
  /// \param RetVT Value type of the result.
  /// \param Opcode ISD opcode to emit.
  /// \param Op0 First register operand.
  /// \param Op1 Second register operand.
  /// \return Result register of the emitted instruction, or an empty register.
  virtual Register fastEmit_rr(MVT VT, MVT RetVT, unsigned Opcode, Register Op0,
                               Register Op1);

  /// This method is called by target-independent code to request that an
  /// instruction with the given type, opcode, and register and immediate
  /// operands be emitted.
  ///
  /// \param VT Value type of the operation.
  /// \param RetVT Value type of the result.
  /// \param Opcode ISD opcode to emit.
  /// \param Op0 Register operand.
  /// \param Imm Immediate operand.
  /// \return Result register of the emitted instruction, or an empty register.
  virtual Register fastEmit_ri(MVT VT, MVT RetVT, unsigned Opcode, Register Op0,
                               uint64_t Imm);

  /// This method is a wrapper of fastEmit_ri.
  ///
  /// It first tries to emit an instruction with an immediate operand using
  /// fastEmit_ri.  If that fails, it materializes the immediate into a register
  /// and try fastEmit_rr instead.
  ///
  /// \param VT Value type of the operation and result.
  /// \param Opcode ISD opcode to emit.
  /// \param Op0 Register operand.
  /// \param Imm Immediate operand.
  /// \param ImmType Value type of \p Imm when materializing it.
  /// \return Result register of the emitted instruction, or an empty register.
  Register fastEmit_ri_(MVT VT, unsigned Opcode, Register Op0, uint64_t Imm,
                        MVT ImmType);

  /// This method is called by target-independent code to request that an
  /// instruction with the given type, opcode, and immediate operand be emitted.
  ///
  /// \param VT Value type of the operation.
  /// \param RetVT Value type of the result.
  /// \param Opcode ISD opcode to emit.
  /// \param Imm Immediate operand.
  /// \return Result register of the emitted instruction, or an empty register.
  virtual Register fastEmit_i(MVT VT, MVT RetVT, unsigned Opcode, uint64_t Imm);

  /// This method is called by target-independent code to request that an
  /// instruction with the given type, opcode, and floating-point immediate
  /// operand be emitted.
  ///
  /// \param VT Value type of the operation.
  /// \param RetVT Value type of the result.
  /// \param Opcode ISD opcode to emit.
  /// \param FPImm Floating-point immediate operand.
  /// \return Result register of the emitted instruction, or an empty register.
  virtual Register fastEmit_f(MVT VT, MVT RetVT, unsigned Opcode,
                              const ConstantFP *FPImm);

  /// Emit a MachineInstr with no operands and a result register in the
  /// given register class.
  ///
  /// \param MachineInstOpcode Target opcode of the instruction to emit.
  /// \param RC Register class of the result.
  /// \return Result register of the emitted instruction.
  Register fastEmitInst_(unsigned MachineInstOpcode,
                         const TargetRegisterClass *RC);

  /// Emit a MachineInstr with one register operand and a result register
  /// in the given register class.
  ///
  /// \param MachineInstOpcode Target opcode of the instruction to emit.
  /// \param RC Register class of the result.
  /// \param Op0 Register operand.
  /// \return Result register of the emitted instruction.
  Register fastEmitInst_r(unsigned MachineInstOpcode,
                          const TargetRegisterClass *RC, Register Op0);

  /// Emit a MachineInstr with two register operands and a result
  /// register in the given register class.
  ///
  /// \param MachineInstOpcode Target opcode of the instruction to emit.
  /// \param RC Register class of the result.
  /// \param Op0 First register operand.
  /// \param Op1 Second register operand.
  /// \return Result register of the emitted instruction.
  Register fastEmitInst_rr(unsigned MachineInstOpcode,
                           const TargetRegisterClass *RC, Register Op0,
                           Register Op1);

  /// Emit a MachineInstr with three register operands and a result
  /// register in the given register class.
  ///
  /// \param MachineInstOpcode Target opcode of the instruction to emit.
  /// \param RC Register class of the result.
  /// \param Op0 First register operand.
  /// \param Op1 Second register operand.
  /// \param Op2 Third register operand.
  /// \return Result register of the emitted instruction.
  Register fastEmitInst_rrr(unsigned MachineInstOpcode,
                            const TargetRegisterClass *RC, Register Op0,
                            Register Op1, Register Op2);

  /// Emit a MachineInstr with a register operand, an immediate, and a
  /// result register in the given register class.
  ///
  /// \param MachineInstOpcode Target opcode of the instruction to emit.
  /// \param RC Register class of the result.
  /// \param Op0 Register operand.
  /// \param Imm Immediate operand.
  /// \return Result register of the emitted instruction.
  Register fastEmitInst_ri(unsigned MachineInstOpcode,
                           const TargetRegisterClass *RC, Register Op0,
                           uint64_t Imm);

  /// Emit a MachineInstr with one register operand and two immediate
  /// operands.
  ///
  /// \param MachineInstOpcode Target opcode of the instruction to emit.
  /// \param RC Register class of the result.
  /// \param Op0 Register operand.
  /// \param Imm1 First immediate operand.
  /// \param Imm2 Second immediate operand.
  /// \return Result register of the emitted instruction.
  Register fastEmitInst_rii(unsigned MachineInstOpcode,
                            const TargetRegisterClass *RC, Register Op0,
                            uint64_t Imm1, uint64_t Imm2);

  /// Emit a MachineInstr with a floating point immediate, and a result
  /// register in the given register class.
  ///
  /// \param MachineInstOpcode Target opcode of the instruction to emit.
  /// \param RC Register class of the result.
  /// \param FPImm Floating-point immediate operand.
  /// \return Result register of the emitted instruction.
  Register fastEmitInst_f(unsigned MachineInstOpcode,
                          const TargetRegisterClass *RC,
                          const ConstantFP *FPImm);

  /// Emit a MachineInstr with two register operands, an immediate, and a
  /// result register in the given register class.
  ///
  /// \param MachineInstOpcode Target opcode of the instruction to emit.
  /// \param RC Register class of the result.
  /// \param Op0 First register operand.
  /// \param Op1 Second register operand.
  /// \param Imm Immediate operand.
  /// \return Result register of the emitted instruction.
  Register fastEmitInst_rri(unsigned MachineInstOpcode,
                            const TargetRegisterClass *RC, Register Op0,
                            Register Op1, uint64_t Imm);

  /// Emit a MachineInstr with a single immediate operand, and a result
  /// register in the given register class.
  ///
  /// \param MachineInstOpcode Target opcode of the instruction to emit.
  /// \param RC Register class of the result.
  /// \param Imm Immediate operand.
  /// \return Result register of the emitted instruction.
  Register fastEmitInst_i(unsigned MachineInstOpcode,
                          const TargetRegisterClass *RC, uint64_t Imm);

  /// Emit a MachineInstr for an extract_subreg from a specified index of
  /// a superregister to a specified type.
  ///
  /// \param RetVT Value type of the extracted subregister.
  /// \param Op0 Superregister to extract from.
  /// \param Idx Subregister index to extract.
  /// \return Result register holding the extracted subregister.
  Register fastEmitInst_extractsubreg(MVT RetVT, Register Op0, uint32_t Idx);

  /// Emit MachineInstrs to compute the value of Op with all but the
  /// least significant bit set to zero.
  ///
  /// \param VT Value type of the result.
  /// \param Op0 i1 register to zero-extend.
  /// \return Result register holding the zero-extended value.
  Register fastEmitZExtFromI1(MVT VT, Register Op0);

  /// Emit an unconditional branch to the given block, unless it is the
  /// immediate (fall-through) successor, and update the CFG.
  ///
  /// \param MSucc Successor block to branch to.
  /// \param DbgLoc Debug location for the branch.
  void fastEmitBranch(MachineBasicBlock *MSucc, const DebugLoc &DbgLoc);

  /// Emit an unconditional branch to \p FalseMBB, obtains the branch weight
  /// and adds TrueMBB and FalseMBB to the successor list.
  ///
  /// \param BranchBB IR block containing the conditional branch.
  /// \param TrueMBB Successor taken when the condition is true.
  /// \param FalseMBB Successor taken when the condition is false.
  void finishCondBranch(const BasicBlock *BranchBB, MachineBasicBlock *TrueMBB,
                        MachineBasicBlock *FalseMBB);

  /// Update the value map to include the new mapping for this
  /// instruction, or insert an extra copy to get the result in a previous
  /// determined register.
  ///
  /// NOTE: This is only necessary because we might select a block that uses a
  /// value before we select the block that defines the value. It might be
  /// possible to fix this by selecting blocks in reverse postorder.
  ///
  /// \param I Value whose register mapping should be updated.
  /// \param Reg Register (or first of several) assigned to \p I.
  /// \param NumRegs Number of consecutive registers assigned to \p I.
  void updateValueMap(const Value *I, Register Reg, unsigned NumRegs = 1);

  /// Create a virtual register in the given register class.
  ///
  /// \param RC Register class of the new virtual register.
  /// \return Newly created virtual register in \p RC.
  Register createResultReg(const TargetRegisterClass *RC);

  /// Constrain a register operand to the class an instruction requires.
  ///
  /// Try to constrain Op so that it is usable by argument OpNum of the
  /// provided MCInstrDesc. If this fails, create a new virtual register in the
  /// correct class and COPY the value there.
  ///
  /// \param II Descriptor of the instruction that will use the operand.
  /// \param Op Register operand to constrain.
  /// \param OpNum Operand index in \p II that \p Op will fill.
  /// \return \p Op if already suitable, otherwise a new constrained copy.
  Register constrainOperandRegClass(const MCInstrDesc &II, Register Op,
                                    unsigned OpNum);

  /// Emit a constant in a register using target-specific logic, such as
  /// constant pool loads.
  ///
  /// \param C Constant to materialize.
  /// \return Register holding the constant, or an empty register on failure.
  virtual Register fastMaterializeConstant(const Constant *C) {
    return Register();
  }

  /// Emit an alloca address in a register using target-specific logic.
  ///
  /// \param C Alloca whose address should be materialized.
  /// \return Register holding the alloca address, or an empty register.
  virtual Register fastMaterializeAlloca(const AllocaInst *C) {
    return Register();
  }

  /// Emit the floating-point constant +0.0 in a register using target-
  /// specific logic.
  ///
  /// \param CF Floating-point +0.0 constant to materialize.
  /// \return Register holding +0.0, or an empty register on failure.
  virtual Register fastMaterializeFloatZero(const ConstantFP *CF) {
    return Register();
  }

  /// Check if \c Add is an add that can be safely folded into \c GEP.
  ///
  /// \c Add can be folded into \c GEP if:
  /// - \c Add is an add,
  /// - \c Add's size matches \c GEP's,
  /// - \c Add is in the same basic block as \c GEP, and
  /// - \c Add has a constant operand.
  ///
  /// \param GEP Getelementptr that would absorb the add.
  /// \param Add Value tested as a foldable add.
  /// \return True if \p Add can be safely folded into \p GEP.
  bool canFoldAddIntoGEP(const User *GEP, const Value *Add);

  /// Create a machine mem operand from the given instruction.
  ///
  /// \param I Load or store instruction to describe.
  /// \return Newly created machine memory operand for \p I.
  MachineMemOperand *createMachineMemOperandFor(const Instruction *I) const;

  /// Fold a compare of a value against itself to a simpler predicate.
  ///
  /// \param CI Compare instruction whose predicate may be simplified.
  /// \return Simplified compare predicate for \p CI.
  CmpInst::Predicate optimizeCmpPredicate(const CmpInst *CI) const;

  /// Lower a call to an MCSymbol.
  ///
  /// \param CI Call instruction being lowered.
  /// \param Symbol Callee symbol.
  /// \param NumArgs Number of arguments to pass.
  /// \return True if the call was lowered successfully.
  bool lowerCallTo(const CallInst *CI, MCSymbol *Symbol, unsigned NumArgs);
  /// Lower a call to a named symbol.
  ///
  /// \param CI Call instruction being lowered.
  /// \param SymName Unmangled callee name.
  /// \param NumArgs Number of arguments to pass.
  /// \return True if the call was lowered successfully.
  bool lowerCallTo(const CallInst *CI, const char *SymName,
                   unsigned NumArgs);
  /// Lower a call described by \p CLI.
  ///
  /// \param CLI Call lowering description to emit.
  /// \return True if the call was lowered successfully.
  bool lowerCallTo(CallLoweringInfo &CLI);

  /// Lower an IR call instruction.
  ///
  /// \param I Call instruction to lower.
  /// \return True if the call was lowered successfully.
  bool lowerCall(const CallInst *I);
  /// Select and emit code for a binary operator instruction, which has
  /// an opcode which directly corresponds to the given ISD opcode.
  ///
  /// \param I Binary operator to select.
  /// \param ISDOpcode ISD opcode corresponding to \p I.
  /// \return True if the binary operator was selected successfully.
  bool selectBinaryOp(const User *I, unsigned ISDOpcode);
  /// Select a floating-point negation.
  ///
  /// \param I Negation instruction or expression being selected.
  /// \param In Operand being negated.
  /// \return True if the negation was selected successfully.
  bool selectFNeg(const User *I, const Value *In);
  /// Select a getelementptr instruction or constant expression.
  ///
  /// \param I Getelementptr to select.
  /// \return True if the getelementptr was selected successfully.
  bool selectGetElementPtr(const User *I);
  /// Select an llvm.experimental.stackmap intrinsic call.
  ///
  /// \param I Stackmap intrinsic to select.
  /// \return True if the stackmap was selected successfully.
  bool selectStackmap(const CallInst *I);
  /// Select an llvm.experimental.patchpoint intrinsic call.
  ///
  /// \param I Patchpoint intrinsic to select.
  /// \return True if the patchpoint was selected successfully.
  bool selectPatchpoint(const CallInst *I);
  /// Select a call instruction, including simple inline assembly.
  ///
  /// \param I Call to select.
  /// \return True if the call was selected successfully.
  bool selectCall(const User *I);
  /// Select an intrinsic call, or defer it to target-specific lowering.
  ///
  /// \param II Intrinsic to select.
  /// \return True if the intrinsic was selected successfully.
  bool selectIntrinsicCall(const IntrinsicInst *II);
  /// Select a bitcast instruction or constant expression.
  ///
  /// \param I Bitcast to select.
  /// \return True if the bitcast was selected successfully.
  bool selectBitCast(const User *I);
  /// Select a freeze instruction.
  ///
  /// \param I Freeze to select.
  /// \return True if the freeze was selected successfully.
  bool selectFreeze(const User *I);
  /// Select a cast instruction with the given ISD opcode.
  ///
  /// \param I Cast instruction to select.
  /// \param Opcode ISD opcode corresponding to the cast.
  /// \return True if the cast was selected successfully.
  bool selectCast(const User *I, unsigned Opcode);
  /// Select an extractvalue instruction.
  ///
  /// \param U Extractvalue instruction to select.
  /// \return True if the extractvalue was selected successfully.
  bool selectExtractValue(const User *U);
  /// Select an llvm.xray.customevent intrinsic call.
  ///
  /// \param II Custom-event intrinsic to select.
  /// \return True if the custom event was selected successfully.
  bool selectXRayCustomEvent(const CallInst *II);
  /// Select an llvm.xray.typedevent intrinsic call.
  ///
  /// \param II Typed-event intrinsic to select.
  /// \return True if the typed event was selected successfully.
  bool selectXRayTypedEvent(const CallInst *II);

  /// Check whether the function should be optimized for size.
  ///
  /// \param MF Machine function to query.
  /// \return True if \p MF should be optimized for size.
  bool shouldOptForSize(const MachineFunction *MF) const {
    // TODO: Implement PGSO.
    return MF->getFunction().hasOptSize();
  }

  /// Target-independent lowering of debug information. Returns false if the
  /// debug information couldn't be lowered and was instead discarded.
  ///
  /// \param V Value described by the debug info, if any.
  /// \param Expr DWARF expression applied to \p V.
  /// \param Var Local variable being described.
  /// \param DL Debug location of the debug info.
  /// \return True if the debug value was lowered; false if discarded.
  virtual bool lowerDbgValue(const Value *V, DIExpression *Expr,
                             DILocalVariable *Var, const DebugLoc &DL);

  /// Target-independent lowering of debug information. Returns false if the
  /// debug information couldn't be lowered and was instead discarded.
  ///
  /// \param V Address of the local variable, if any.
  /// \param Expr DWARF expression applied to \p V.
  /// \param Var Local variable being described.
  /// \param DL Debug location of the debug info.
  /// \return True if the debug declare was lowered; false if discarded.
  virtual bool lowerDbgDeclare(const Value *V, DIExpression *Expr,
                               DILocalVariable *Var, const DebugLoc &DL);

private:
  /// Handle PHI nodes in successor blocks.
  ///
  /// Emit code to ensure constants are copied into registers when needed.
  /// Remember the virtual registers that need to be added to the Machine PHI
  /// nodes as input.  We cannot just directly add them, because expansion might
  /// result in multiple MBB's for one BB.  As such, the start of the BB might
  /// correspond to a different MBB than the end.
  bool handlePHINodesInSuccessorBlocks(const BasicBlock *LLVMBB);

  /// Helper for materializeRegForValue to materialize a constant in a
  /// target-independent way.
  Register materializeConstant(const Value *V, MVT VT);

  /// Helper for getRegForVale. This function is called when the value
  /// isn't already available in a register and must be materialized with new
  /// instructions.
  Register materializeRegForValue(const Value *V, MVT VT);

  /// Clears LocalValueMap and moves the area for the new local variables
  /// to the beginning of the block. It helps to avoid spilling cached variables
  /// across heavy instructions like calls.
  void flushLocalValueMap();

  /// Removes dead local value instructions after SavedLastLocalvalue.
  void removeDeadLocalValueCode(MachineInstr *SavedLastLocalValue);

  /// Insertion point before trying to select the current instruction.
  MachineBasicBlock::iterator SavedInsertPt;

  /// Add a stackmap or patchpoint intrinsic call's live variable
  /// operands to a stackmap or patchpoint machine instruction.
  bool addStackMapLiveVars(SmallVectorImpl<MachineOperand> &Ops,
                           const CallInst *CI, unsigned StartIdx);
  bool lowerCallOperands(const CallInst *CI, unsigned ArgIdx, unsigned NumArgs,
                         const Value *Callee, bool ForceRetVoidTy,
                         CallLoweringInfo &CLI);
};

} // end namespace llvm

#endif // LLVM_CODEGEN_FASTISEL_H
