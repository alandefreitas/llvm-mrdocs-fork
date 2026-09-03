//===- llvm/CallingConvLower.h - Calling Conventions ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the CCState and CCValAssign classes, used for lowering
// and implementing calling conventions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_CALLINGCONVLOWER_H
#define LLVM_CODEGEN_CALLINGCONVLOWER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/CodeGen/TargetCallingConv.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Compiler.h"
#include <variant>

namespace llvm {

class CCState;
class MachineFunction;
class MVT;
class TargetRegisterInfo;

/// CCValAssign - Represent assignment of one arg/retval to a location.
class CCValAssign {
public:
  /// How a value is stored in its assigned location.
  enum LocInfo {
    /// The value fills the full location.
    Full,
    /// The value is sign extended in the location.
    SExt,
    /// The value is zero extended in the location.
    ZExt,
    /// The value is extended with undefined upper bits.
    AExt,
    /// The value is in the upper bits of the location and should be sign
    /// extended when retrieved.
    SExtUpper,
    /// The value is in the upper bits of the location and should be zero
    /// extended when retrieved.
    ZExtUpper,
    /// The value is in the upper bits of the location and should be extended
    /// with undefined upper bits when retrieved.
    AExtUpper,
    /// The value is bit-converted in the location.
    BCvt,
    /// The value is truncated in the location.
    Trunc,
    /// The value is vector-widened in the location.
    ///
    /// FIXME: Not implemented yet. Code that uses AExt to mean vector-widen
    /// should be fixed to use VExt instead.
    VExt,
    /// The floating-point value is fp-extended in the location.
    FPExt,
    /// The location contains a pointer to the value.
    Indirect
    // TODO: a subset of the value is in the location.
  };

private:
  // Holds one of:
  // - the register that the value is assigned to;
  // - the memory offset at which the value resides;
  // - additional information about pending location; the exact interpretation
  //   of the data is target-dependent.
  std::variant<Register, int64_t, unsigned> Data;

  /// ValNo - This is the value number being assigned (e.g. an argument number).
  unsigned ValNo;

  /// isCustom - True if this arg/retval requires special handling.
  unsigned isCustom : 1;

  /// Information about how the value is assigned.
  LocInfo HTP : 6;

  /// ValVT - The type of the value being assigned.
  MVT ValVT;

  /// LocVT - The type of the location being assigned to.
  MVT LocVT;

  CCValAssign(LocInfo HTP, unsigned ValNo, MVT ValVT, MVT LocVT, bool IsCustom)
      : ValNo(ValNo), isCustom(IsCustom), HTP(HTP), ValVT(ValVT), LocVT(LocVT) {
  }

public:
  /// Create a register assignment for value \p ValNo.
  ///
  /// \param ValNo Index of the argument or return value being assigned.
  /// \param ValVT Value type of the argument or return value.
  /// \param Reg Physical register that receives the value.
  /// \param LocVT Type of the assigned register location.
  /// \param HTP How the value is stored in the location.
  /// \param IsCustom Whether the assignment needs custom target handling.
  /// \returns A register assignment for the value.
  static CCValAssign getReg(unsigned ValNo, MVT ValVT, MCRegister Reg,
                            MVT LocVT, LocInfo HTP, bool IsCustom = false) {
    CCValAssign Ret(HTP, ValNo, ValVT, LocVT, IsCustom);
    Ret.Data = Register(Reg);
    return Ret;
  }

  /// Create a custom register assignment for value \p ValNo.
  ///
  /// \param ValNo Index of the argument or return value being assigned.
  /// \param ValVT Value type of the argument or return value.
  /// \param Reg Physical register that receives the value.
  /// \param LocVT Type of the assigned register location.
  /// \param HTP How the value is stored in the location.
  /// \returns A custom register assignment for the value.
  static CCValAssign getCustomReg(unsigned ValNo, MVT ValVT, MCRegister Reg,
                                  MVT LocVT, LocInfo HTP) {
    return getReg(ValNo, ValVT, Reg, LocVT, HTP, /*IsCustom=*/true);
  }

  /// Create a memory assignment for value \p ValNo at stack \p Offset.
  ///
  /// \param ValNo Index of the argument or return value being assigned.
  /// \param ValVT Value type of the argument or return value.
  /// \param Offset Stack offset of the assigned memory location.
  /// \param LocVT Type of the assigned memory location.
  /// \param HTP How the value is stored in the location.
  /// \param IsCustom Whether the assignment needs custom target handling.
  /// \returns A memory assignment for the value.
  static CCValAssign getMem(unsigned ValNo, MVT ValVT, int64_t Offset,
                            MVT LocVT, LocInfo HTP, bool IsCustom = false) {
    CCValAssign Ret(HTP, ValNo, ValVT, LocVT, IsCustom);
    Ret.Data = Offset;
    return Ret;
  }

  /// Create a custom memory assignment for value \p ValNo at stack \p Offset.
  ///
  /// \param ValNo Index of the argument or return value being assigned.
  /// \param ValVT Value type of the argument or return value.
  /// \param Offset Stack offset of the assigned memory location.
  /// \param LocVT Type of the assigned memory location.
  /// \param HTP How the value is stored in the location.
  /// \returns A custom memory assignment for the value.
  static CCValAssign getCustomMem(unsigned ValNo, MVT ValVT, int64_t Offset,
                                  MVT LocVT, LocInfo HTP) {
    return getMem(ValNo, ValVT, Offset, LocVT, HTP, /*IsCustom=*/true);
  }

  /// Create a pending assignment whose location is not yet decided.
  ///
  /// \param ValNo Index of the argument or return value being assigned.
  /// \param ValVT Value type of the argument or return value.
  /// \param LocVT Type of the eventual location.
  /// \param HTP How the value will be stored in the location.
  /// \param ExtraInfo Target-dependent extra data stored with the assignment.
  /// \returns A pending assignment for the value.
  static CCValAssign getPending(unsigned ValNo, MVT ValVT, MVT LocVT,
                                LocInfo HTP, unsigned ExtraInfo = 0) {
    CCValAssign Ret(HTP, ValNo, ValVT, LocVT, false);
    Ret.Data = ExtraInfo;
    return Ret;
  }

  /// Convert this assignment into a register location using \p Reg.
  ///
  /// \param Reg Physical register that receives the value.
  void convertToReg(MCRegister Reg) { Data = Register(Reg); }

  /// Convert this assignment into a memory location at stack \p Offset.
  ///
  /// \param Offset Stack offset of the assigned memory location.
  void convertToMem(int64_t Offset) { Data = Offset; }

  /// Return the index of the argument or return value being assigned.
  ///
  /// \returns The index of the argument or return value being assigned.
  unsigned getValNo() const { return ValNo; }
  /// Return the type of the value being assigned.
  ///
  /// \returns The type of the value being assigned.
  MVT getValVT() const { return ValVT; }

  /// Return true if the value is assigned to a register.
  ///
  /// \returns True if the value is assigned to a register.
  bool isRegLoc() const { return std::holds_alternative<Register>(Data); }
  /// Return true if the value is assigned to a stack location.
  ///
  /// \returns True if the value is assigned to a stack location.
  bool isMemLoc() const { return std::holds_alternative<int64_t>(Data); }
  /// Return true if the assignment location has not been decided yet.
  ///
  /// \returns True if the assignment location has not been decided yet.
  bool isPendingLoc() const { return std::holds_alternative<unsigned>(Data); }

  /// Return true if this assignment requires custom target handling.
  ///
  /// \returns True if this assignment requires custom target handling.
  bool needsCustom() const { return isCustom; }

  /// Return the physical register assigned to this value.
  ///
  /// \returns The physical register assigned to this value.
  Register getLocReg() const { return std::get<Register>(Data); }
  /// Return the stack offset assigned to this value.
  ///
  /// \returns The stack offset assigned to this value.
  int64_t getLocMemOffset() const { return std::get<int64_t>(Data); }
  /// Return target-dependent extra data for a pending assignment.
  ///
  /// \returns Target-dependent extra data for a pending assignment.
  unsigned getExtraInfo() const { return std::get<unsigned>(Data); }

  /// Return the type of the assigned location.
  ///
  /// \returns The type of the assigned location.
  MVT getLocVT() const { return LocVT; }

  /// Return how the value is stored in its assigned location.
  ///
  /// \returns How the value is stored in its assigned location.
  LocInfo getLocInfo() const { return HTP; }
  /// Return true if the value is extended in the lower bits of the location.
  ///
  /// \returns True if the value is extended in the lower bits of the location.
  bool isExtInLoc() const {
    return (HTP == AExt || HTP == SExt || HTP == ZExt);
  }

  /// Return true if the value occupies the upper bits of the location.
  ///
  /// \returns True if the value occupies the upper bits of the location.
  bool isUpperBitsInLoc() const {
    return HTP == AExtUpper || HTP == SExtUpper || HTP == ZExtUpper;
  }
};

/// Describes a register that needs to be forwarded from the prologue to a
/// musttail call.
struct ForwardedRegister {
  /// Construct a forwarded register mapping \p VReg to \p PReg with type \p VT.
  ///
  /// \param VReg Virtual register that receives the live-in value.
  /// \param PReg Physical register forwarded from the prologue.
  /// \param VT Machine value type of the forwarded register.
  ForwardedRegister(Register VReg, MCPhysReg PReg, MVT VT)
      : VReg(VReg), PReg(PReg), VT(VT) {}
  /// Virtual register that receives the live-in physical register.
  Register VReg;
  /// Physical register forwarded from the prologue to a musttail call.
  MCPhysReg PReg;
  /// Machine value type of the forwarded register.
  MVT VT;
};

/// CCAssignFn - This function assigns a location for Val, updating State to
/// reflect the change.  It returns 'true' if it failed to handle Val.
typedef bool CCAssignFn(unsigned ValNo, MVT ValVT, MVT LocVT,
                        CCValAssign::LocInfo LocInfo, ISD::ArgFlagsTy ArgFlags,
                        Type *OrigTy, CCState &State);

/// Callback that assigns a custom location for a calling-convention value.
///
/// The function may update all arguments to reflect changes and indicates
/// whether it handled the value. It must set isCustom if it handles the arg
/// and returns true.
typedef bool CCCustomFn(unsigned &ValNo, MVT &ValVT,
                        MVT &LocVT, CCValAssign::LocInfo &LocInfo,
                        ISD::ArgFlagsTy &ArgFlags, CCState &State);

/// Tracks register and stack assignments while lowering arguments and returns.
///
/// It captures which registers are already assigned and which stack slots are
/// used. It provides accessors to allocate these values.
class CCState {
private:
  CallingConv::ID CallingConv;
  bool IsVarArg;
  bool AnalyzingMustTailForwardedRegs = false;
  MachineFunction &MF;
  const TargetRegisterInfo &TRI;
  SmallVectorImpl<CCValAssign> &Locs;
  LLVMContext &Context;
  // True if arguments should be allocated at negative offsets.
  bool NegativeOffsets;

  uint64_t StackSize;
  Align MaxStackArgAlign;
  SmallVector<uint32_t, 16> UsedRegs;
  SmallVector<CCValAssign, 4> PendingLocs;
  SmallVector<ISD::ArgFlagsTy, 4> PendingArgFlags;

  // ByValInfo and SmallVector<ByValInfo, 4> ByValRegs:
  //
  // Vector of ByValInfo instances (ByValRegs) is introduced for byval registers
  // tracking.
  // Or, in another words it tracks byval parameters that are stored in
  // general purpose registers.
  //
  // For 4 byte stack alignment,
  // instance index means byval parameter number in formal
  // arguments set. Assume, we have some "struct_type" with size = 4 bytes,
  // then, for function "foo":
  //
  // i32 foo(i32 %p, %struct_type* %r, i32 %s, %struct_type* %t)
  //
  // ByValRegs[0] describes how "%r" is stored (Begin == r1, End == r2)
  // ByValRegs[1] describes how "%t" is stored (Begin == r3, End == r4).
  //
  // In case of 8 bytes stack alignment,
  // In function shown above, r3 would be wasted according to AAPCS rules.
  // ByValRegs vector size still would be 2,
  // while "%t" goes to the stack: it wouldn't be described in ByValRegs.
  //
  // Supposed use-case for this collection:
  // 1. Initially ByValRegs is empty, InRegsParamsProcessed is 0.
  // 2. HandleByVal fills up ByValRegs.
  // 3. Argument analysis (LowerFormatArguments, for example). After
  // some byval argument was analyzed, InRegsParamsProcessed is increased.
  struct ByValInfo {
    ByValInfo(unsigned B, unsigned E) : Begin(B), End(E) {}

    // First register allocated for current parameter.
    unsigned Begin;

    // First after last register allocated for current parameter.
    unsigned End;
  };
  SmallVector<ByValInfo, 4 > ByValRegs;

  // InRegsParamsProcessed - shows how many instances of ByValRegs was proceed
  // during argument analysis.
  unsigned InRegsParamsProcessed;

public:
  /// Construct calling-convention state for \p CC on machine function \p MF.
  ///
  /// \param CC Calling convention being lowered.
  /// \param IsVarArg True if the function or call is variadic.
  /// \param MF Machine function whose calling convention is being lowered.
  /// \param Locs Output list of value-to-location assignments.
  /// \param Context LLVM context used when synthesizing types.
  /// \param NegativeOffsets True if stack arguments use negative offsets.
  LLVM_ABI CCState(CallingConv::ID CC, bool IsVarArg, MachineFunction &MF,
                   SmallVectorImpl<CCValAssign> &Locs, LLVMContext &Context,
                   bool NegativeOffsets = false);

  /// Append assignment \p V to the list of value locations.
  ///
  /// \param V Assignment to record.
  void addLoc(const CCValAssign &V) {
    Locs.push_back(V);
  }

  /// Return the LLVM context associated with this lowering.
  ///
  /// \returns The LLVM context associated with this lowering.
  LLVMContext &getContext() const { return Context; }
  /// Return the machine function whose calling convention is being lowered.
  ///
  /// \returns The machine function being lowered.
  MachineFunction &getMachineFunction() const { return MF; }
  /// Return the calling convention being lowered.
  ///
  /// \returns The calling convention being lowered.
  CallingConv::ID getCallingConv() const { return CallingConv; }
  /// Return true if the function or call being lowered is variadic.
  ///
  /// \returns True if the function or call being lowered is variadic.
  bool isVarArg() const { return IsVarArg; }

  /// Returns the size of the currently allocated portion of the stack.
  ///
  /// \returns The size of the currently allocated portion of the stack.
  uint64_t getStackSize() const { return StackSize; }

  /// Return the aligned call-frame size needed to store all arguments.
  ///
  /// The result is large enough to store all arguments and such that the
  /// alignment requirement of each of the arguments is satisfied.
  ///
  /// \returns The aligned call-frame size needed to store all arguments.
  uint64_t getAlignedCallFrameSize() const {
    return alignTo(StackSize, MaxStackArgAlign);
  }

  /// isAllocated - Return true if the specified register (or an alias) is
  /// allocated.
  ///
  /// \param Reg Register (or alias) to test for allocation.
  /// \returns True if the register or an alias is allocated.
  bool isAllocated(MCRegister Reg) const {
    return UsedRegs[Reg.id() / 32] & (1 << (Reg.id() & 31));
  }

  /// AnalyzeFormalArguments - Analyze an array of argument values,
  /// incorporating info about the formals into this state.
  ///
  /// \param Ins Formal input arguments to analyze.
  /// \param Fn Calling-convention assignment function.
  LLVM_ABI void
  AnalyzeFormalArguments(const SmallVectorImpl<ISD::InputArg> &Ins,
                         CCAssignFn Fn);

  /// The function will invoke AnalyzeFormalArguments.
  ///
  /// \param Ins Formal input arguments to analyze.
  /// \param Fn Calling-convention assignment function.
  void AnalyzeArguments(const SmallVectorImpl<ISD::InputArg> &Ins,
                        CCAssignFn Fn) {
    AnalyzeFormalArguments(Ins, Fn);
  }

  /// AnalyzeReturn - Analyze the returned values of a return,
  /// incorporating info about the result values into this state.
  ///
  /// \param Outs Return values to analyze.
  /// \param Fn Calling-convention assignment function.
  LLVM_ABI void AnalyzeReturn(const SmallVectorImpl<ISD::OutputArg> &Outs,
                              CCAssignFn Fn);

  /// CheckReturn - Analyze the return values of a function, returning
  /// true if the return can be performed without sret-demotion, and
  /// false otherwise.
  ///
  /// \param Outs Return values to analyze.
  /// \param Fn Calling-convention assignment function.
  /// \returns True if the return can be performed without sret-demotion.
  LLVM_ABI bool CheckReturn(const SmallVectorImpl<ISD::OutputArg> &Outs,
                            CCAssignFn Fn);

  /// AnalyzeCallOperands - Analyze the outgoing arguments to a call,
  /// incorporating info about the passed values into this state.
  ///
  /// \param Outs Outgoing call operands to analyze.
  /// \param Fn Calling-convention assignment function.
  LLVM_ABI void AnalyzeCallOperands(const SmallVectorImpl<ISD::OutputArg> &Outs,
                                    CCAssignFn Fn);

  /// AnalyzeCallOperands - Same as above except it takes vectors of types
  /// and argument flags.
  ///
  /// \param ArgVTs Value types of the outgoing call operands.
  /// \param Flags Argument flags for each operand.
  /// \param OrigTys Original IR types of each operand.
  /// \param Fn Calling-convention assignment function.
  LLVM_ABI void AnalyzeCallOperands(SmallVectorImpl<MVT> &ArgVTs,
                                    SmallVectorImpl<ISD::ArgFlagsTy> &Flags,
                                    SmallVectorImpl<Type *> &OrigTys,
                                    CCAssignFn Fn);

  /// The function will invoke AnalyzeCallOperands.
  ///
  /// \param Outs Outgoing call operands to analyze.
  /// \param Fn Calling-convention assignment function.
  void AnalyzeArguments(const SmallVectorImpl<ISD::OutputArg> &Outs,
                        CCAssignFn Fn) {
    AnalyzeCallOperands(Outs, Fn);
  }

  /// AnalyzeCallResult - Analyze the return values of a call,
  /// incorporating info about the passed values into this state.
  ///
  /// \param Ins Incoming return values from the call.
  /// \param Fn Calling-convention assignment function.
  LLVM_ABI void AnalyzeCallResult(const SmallVectorImpl<ISD::InputArg> &Ins,
                                  CCAssignFn Fn);

  /// A shadow allocated register is a register that was allocated
  /// but wasn't added to the location list (Locs).
  ///
  /// \param Reg Register to test for shadow allocation.
  /// \returns true if the register was allocated as shadow or false otherwise.
  LLVM_ABI bool IsShadowAllocatedReg(MCRegister Reg) const;

  /// AnalyzeCallResult - Same as above except it's specialized for calls which
  /// produce a single value.
  ///
  /// \param VT Value type of the single call result.
  /// \param OrigTy Original IR type of the call result.
  /// \param Fn Calling-convention assignment function.
  LLVM_ABI void AnalyzeCallResult(MVT VT, Type *OrigTy, CCAssignFn Fn);

  /// getFirstUnallocated - Return the index of the first unallocated register
  /// in the set, or Regs.size() if they are all allocated.
  ///
  /// \param Regs Candidate physical registers to search.
  /// \returns The index of the first unallocated register, or Regs.size().
  unsigned getFirstUnallocated(ArrayRef<MCPhysReg> Regs) const {
    for (unsigned i = 0; i < Regs.size(); ++i)
      if (!isAllocated(Regs[i]))
        return i;
    return Regs.size();
  }

  /// Mark \p Reg and its aliases as unallocated.
  ///
  /// \param Reg Physical register to deallocate.
  void DeallocateReg(MCPhysReg Reg) {
    assert(isAllocated(Reg) && "Trying to deallocate an unallocated register");
    MarkUnallocated(Reg);
  }

  /// AllocateReg - Attempt to allocate one register.  If it is not available,
  /// return zero.  Otherwise, return the register, marking it and any aliases
  /// as allocated.
  ///
  /// \param Reg Physical register to allocate if it is still available.
  /// \returns The allocated register, or zero if it was already allocated.
  MCRegister AllocateReg(MCPhysReg Reg) {
    if (isAllocated(Reg))
      return MCRegister();
    MarkAllocated(Reg);
    return Reg;
  }

  /// Version of AllocateReg with extra register to be shadowed.
  ///
  /// \param Reg Physical register to allocate if it is still available.
  /// \param ShadowReg Additional register marked allocated with \p Reg.
  /// \returns The allocated register, or zero if it was already allocated.
  MCRegister AllocateReg(MCPhysReg Reg, MCPhysReg ShadowReg) {
    if (isAllocated(Reg))
      return MCRegister();
    MarkAllocated(Reg);
    MarkAllocated(ShadowReg);
    return Reg;
  }

  /// Allocate the first available register from \p Regs, or return zero.
  ///
  /// If none are available, return zero. Otherwise, return the first one
  /// available, marking it and any aliases as allocated.
  ///
  /// \param Regs Candidate physical registers, tried in order.
  /// \returns The first available register, or zero if none were available.
  MCRegister AllocateReg(ArrayRef<MCPhysReg> Regs) {
    unsigned FirstUnalloc = getFirstUnallocated(Regs);
    if (FirstUnalloc == Regs.size())
      return MCRegister();    // Didn't find the reg.

    // Mark the register and any aliases as allocated.
    MCPhysReg Reg = Regs[FirstUnalloc];
    MarkAllocated(Reg);
    return Reg;
  }

  /// Allocate \p RegsRequired consecutive registers from \p Regs, if available.
  ///
  /// If this is not possible, return an empty range. Otherwise, return a range
  /// of consecutive registers, marking the entire block as allocated.
  ///
  /// \param Regs Candidate physical registers in consecutive order.
  /// \param RegsRequired Number of consecutive registers required.
  /// \returns A range of consecutive allocated registers, or an empty range.
  ArrayRef<MCPhysReg> AllocateRegBlock(ArrayRef<MCPhysReg> Regs,
                                       unsigned RegsRequired) {
    if (RegsRequired > Regs.size())
      return {};

    for (unsigned StartIdx = 0; StartIdx <= Regs.size() - RegsRequired;
         ++StartIdx) {
      bool BlockAvailable = true;
      // Check for already-allocated regs in this block
      for (unsigned BlockIdx = 0; BlockIdx < RegsRequired; ++BlockIdx) {
        if (isAllocated(Regs[StartIdx + BlockIdx])) {
          BlockAvailable = false;
          break;
        }
      }
      if (BlockAvailable) {
        // Mark the entire block as allocated
        for (unsigned BlockIdx = 0; BlockIdx < RegsRequired; ++BlockIdx) {
          MarkAllocated(Regs[StartIdx + BlockIdx]);
        }
        return Regs.slice(StartIdx, RegsRequired);
      }
    }
    // No block was available
    return {};
  }

  /// Version of AllocateReg with list of registers to be shadowed.
  ///
  /// \param Regs Candidate physical registers, tried in order.
  /// \param ShadowRegs Parallel list of registers marked allocated with each
  ///                   chosen candidate.
  /// \returns The allocated register, or zero if none were available.
  MCRegister AllocateReg(ArrayRef<MCPhysReg> Regs, const MCPhysReg *ShadowRegs) {
    unsigned FirstUnalloc = getFirstUnallocated(Regs);
    if (FirstUnalloc == Regs.size())
      return MCRegister();    // Didn't find the reg.

    // Mark the register and any aliases as allocated.
    MCRegister Reg = Regs[FirstUnalloc], ShadowReg = ShadowRegs[FirstUnalloc];
    MarkAllocated(Reg);
    MarkAllocated(ShadowReg);
    return Reg;
  }

  /// AllocateStack - Allocate a chunk of stack space with the specified size
  /// and alignment.
  ///
  /// \param Size Number of bytes to allocate.
  /// \param Alignment Required alignment of the allocated stack space.
  /// \returns The stack offset of the allocated space.
  int64_t AllocateStack(unsigned Size, Align Alignment) {
    int64_t Offset;
    if (NegativeOffsets) {
      StackSize = alignTo(StackSize + Size, Alignment);
      Offset = -StackSize;
    } else {
      Offset = alignTo(StackSize, Alignment);
      StackSize = Offset + Size;
    }
    MaxStackArgAlign = std::max(Alignment, MaxStackArgAlign);
    ensureMaxAlignment(Alignment);
    return Offset;
  }

  /// Raise the recorded max stack argument alignment to at least \p Alignment.
  ///
  /// \param Alignment Required alignment to record on the frame.
  LLVM_ABI void ensureMaxAlignment(Align Alignment);

  /// Version of AllocateStack with list of extra registers to be shadowed.
  /// Note that, unlike AllocateReg, this shadows ALL of the shadow registers.
  ///
  /// \param Size Number of bytes to allocate.
  /// \param Alignment Required alignment of the allocated stack space.
  /// \param ShadowRegs Registers marked allocated in addition to the stack
  ///                   slot.
  /// \returns The stack offset of the allocated space.
  int64_t AllocateStack(unsigned Size, Align Alignment,
                        ArrayRef<MCPhysReg> ShadowRegs) {
    for (MCPhysReg Reg : ShadowRegs)
      MarkAllocated(Reg);
    return AllocateStack(Size, Alignment);
  }

  /// Allocate a stack slot large enough to pass an argument by value.
  ///
  /// The size and alignment information of the argument is encoded in its
  /// parameter attribute.
  ///
  /// \param ValNo Index of the argument being assigned.
  /// \param ValVT Value type of the argument.
  /// \param LocVT Type of the assigned location.
  /// \param LocInfo How the value is stored in the location.
  /// \param MinSize Minimum stack size required for the byval argument.
  /// \param MinAlign Minimum alignment required for the byval argument.
  /// \param ArgFlags Argument flags encoding size, alignment, and byval info.
  LLVM_ABI void HandleByVal(unsigned ValNo, MVT ValVT, MVT LocVT,
                            CCValAssign::LocInfo LocInfo, int MinSize,
                            Align MinAlign, ISD::ArgFlagsTy ArgFlags);

  /// Return the number of byval arguments stored (even partly) in registers.
  ///
  /// \returns The number of byval arguments stored in registers.
  unsigned getInRegsParamsCount() const { return ByValRegs.size(); }

  /// Return the number of byval in-register arguments already processed.
  ///
  /// \returns The number of byval in-register arguments already processed.
  unsigned getInRegsParamsProcessed() const { return InRegsParamsProcessed; }

  /// Get the register range for the N-th byval parameter stored in registers.
  ///
  /// \param InRegsParamRecordIndex Index N of the byval-in-regs parameter.
  /// \param BeginReg Set to the first register allocated for the parameter.
  /// \param EndReg Set to one past the last register allocated for the
  ///               parameter.
  void getInRegsParamInfo(unsigned InRegsParamRecordIndex,
                          unsigned& BeginReg, unsigned& EndReg) const {
    assert(InRegsParamRecordIndex < ByValRegs.size() &&
           "Wrong ByVal parameter index");

    const ByValInfo& info = ByValRegs[InRegsParamRecordIndex];
    BeginReg = info.Begin;
    EndReg = info.End;
  }

  /// Record that a byval parameter occupies registers [\p RegBegin, \p RegEnd).
  ///
  /// \param RegBegin First register allocated for the parameter.
  /// \param RegEnd One-past-last register allocated for the parameter.
  void addInRegsParamInfo(unsigned RegBegin, unsigned RegEnd) {
    ByValRegs.push_back(ByValInfo(RegBegin, RegEnd));
  }

  /// Advance to the next byval-in-regs parameter, skipping waste records.
  ///
  /// \returns False if the end of the collection is reached, true otherwise.
  bool nextInRegsParam() {
    unsigned e = ByValRegs.size();
    if (InRegsParamsProcessed < e)
      ++InRegsParamsProcessed;
    return InRegsParamsProcessed < e;
  }

  /// Clear byval register tracking information.
  void clearByValRegsInfo() {
    InRegsParamsProcessed = 0;
    ByValRegs.clear();
  }

  /// Rewind byval register tracking to the first recorded parameter.
  void rewindByValRegsInfo() {
    InRegsParamsProcessed = 0;
  }

  /// Return the list of pending value assignments.
  ///
  /// \returns The pending value assignments.
  SmallVectorImpl<CCValAssign> &getPendingLocs() {
    return PendingLocs;
  }

  /// Return the argument flags associated with pending assignments.
  ///
  /// \returns The argument flags for pending assignments.
  SmallVectorImpl<ISD::ArgFlagsTy> &getPendingArgFlags() {
    return PendingArgFlags;
  }

  /// Compute unused register parameters remaining for the given value type.
  ///
  /// This is useful when varargs are passed in the registers that normal
  /// prototyped parameters would be passed in, or for implementing perfect
  /// forwarding.
  ///
  /// \param Regs Output list of remaining unused parameter registers.
  /// \param VT Value type whose unused register parameters are computed.
  /// \param Fn Calling-convention assignment function.
  LLVM_ABI void getRemainingRegParmsForType(SmallVectorImpl<MCRegister> &Regs,
                                            MVT VT, CCAssignFn Fn);

  /// Compute the set of registers that need to be preserved and forwarded to
  /// any musttail calls.
  ///
  /// \param Forwards Output list of registers to forward from the prologue.
  /// \param RegParmTypes Value types that may be passed in registers.
  /// \param Fn Calling-convention assignment function.
  LLVM_ABI void analyzeMustTailForwardedRegisters(
      SmallVectorImpl<ForwardedRegister> &Forwards, ArrayRef<MVT> RegParmTypes,
      CCAssignFn Fn);

  /// Returns true if the results of the two calling conventions are compatible.
  /// This is usually part of the check for tailcall eligibility.
  ///
  /// \param CalleeCC Calling convention of the callee.
  /// \param CallerCC Calling convention of the caller.
  /// \param MF Machine function used to construct temporary CCState objects.
  /// \param C LLVM context used to construct temporary CCState objects.
  /// \param Ins Return values whose assigned locations are compared.
  /// \param CalleeFn Assignment function for the callee convention.
  /// \param CallerFn Assignment function for the caller convention.
  /// \returns True if the result locations are compatible under both
  ///          conventions.
  LLVM_ABI static bool
  resultsCompatible(CallingConv::ID CalleeCC, CallingConv::ID CallerCC,
                    MachineFunction &MF, LLVMContext &C,
                    const SmallVectorImpl<ISD::InputArg> &Ins,
                    CCAssignFn CalleeFn, CCAssignFn CallerFn);

  /// Run a second analysis pass over function arguments, then sort locations.
  ///
  /// Each argument is marked with the attribute flag SecArgPass. After
  /// running, the locs list is sorted back into original argument order.
  ///
  /// \param Args Arguments to re-analyze in the second pass.
  /// \param Fn Calling-convention assignment function.
  template <class T>
  void AnalyzeArgumentsSecondPass(const SmallVectorImpl<T> &Args,
                                  CCAssignFn Fn) {
    unsigned NumFirstPassLocs = Locs.size();

    /// Creates similar argument list to \p Args in which each argument is
    /// marked using SecArgPass flag.
    SmallVector<T, 16> SecPassArg;
    // SmallVector<ISD::InputArg, 16> SecPassArg;
    for (auto Arg : Args) {
      Arg.Flags.setSecArgPass();
      SecPassArg.push_back(Arg);
    }

    // Run the second argument pass
    AnalyzeArguments(SecPassArg, Fn);

    // Sort the locations of the arguments according to their original position.
    SmallVector<CCValAssign, 16> TmpArgLocs;
    TmpArgLocs.swap(Locs);
    auto B = TmpArgLocs.begin(), E = TmpArgLocs.end();
    std::merge(B, B + NumFirstPassLocs, B + NumFirstPassLocs, E,
               std::back_inserter(Locs),
               [](const CCValAssign &A, const CCValAssign &B) -> bool {
                 return A.getValNo() < B.getValNo();
               });
  }

private:
  /// MarkAllocated - Mark a register and all of its aliases as allocated.
  LLVM_ABI void MarkAllocated(MCPhysReg Reg);

  LLVM_ABI void MarkUnallocated(MCPhysReg Reg);
};

} // end namespace llvm

#endif // LLVM_CODEGEN_CALLINGCONVLOWER_H
