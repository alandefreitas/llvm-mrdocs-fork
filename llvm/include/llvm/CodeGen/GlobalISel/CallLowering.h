//===- llvm/CodeGen/GlobalISel/CallLowering.h - Call lowering ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file describes how to lower LLVM calls to machine code calls.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALISEL_CALLLOWERING_H
#define LLVM_CODEGEN_GLOBALISEL_CALLLOWERING_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/TargetCallingConv.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGenTypes/LowLevelType.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include <cstdint>
#include <functional>

namespace llvm {

class AttributeList;
class CallBase;
class DataLayout;
class Function;
class FunctionLoweringInfo;
class MachineIRBuilder;
class MachineFunction;
struct MachinePointerInfo;
class MachineRegisterInfo;
class TargetLowering;

/// Interface for lowering LLVM calls and returns to GlobalISel MIR.
class LLVM_ABI CallLowering {
  const TargetLowering *TLI;

  virtual void anchor();
public:
  /// Common type and ISD argument flags for a formal, call, or return value.
  struct BaseArgInfo {
    /// IR type of the argument or return value.
    Type *Ty;
    /// ISD argument flags describing how the value is passed or returned.
    SmallVector<ISD::ArgFlagsTy, 4> Flags;

    /// Construct from an IR type and optional argument flags.
    /// \param Ty IR type of the argument or return value.
    /// \param Flags ISD argument flags; empty means default flags.
    BaseArgInfo(Type *Ty,
                ArrayRef<ISD::ArgFlagsTy> Flags = ArrayRef<ISD::ArgFlagsTy>())
        : Ty(Ty), Flags(Flags) {}

    /// Construct an empty argument descriptor with a null type.
    BaseArgInfo() : Ty(nullptr) {}
  };

  /// Descriptor for an argument or return value being lowered.
  struct ArgInfo : public BaseArgInfo {
    /// Virtual registers holding the (possibly split) value.
    SmallVector<Register, 4> Regs;
    /// Original vregs before calling-convention splitting for incoming args.
    ///
    /// If the argument had to be split into multiple parts according to the
    /// target calling convention, then this contains the original vregs
    /// if the argument was an incoming arg.
    SmallVector<Register, 2> OrigRegs;

    /// Optional original IR value used for MachinePointerInfo aliasing.
    ///
    /// Optionally track the original IR value for the argument. This may not be
    /// meaningful in all contexts. This should only be used on for forwarding
    /// through to use for aliasing information in MachinePointerInfo for memory
    /// arguments.
    const Value *OrigValue = nullptr;

    /// Index original Function's argument.
    unsigned OrigArgIndex;

    /// Sentinel value for implicit machine-level input arguments.
    static const unsigned NoArgIndex = UINT_MAX;

    /// Construct from registers, type, original argument index, and flags.
    /// \param Regs Virtual registers holding the value.
    /// \param Ty IR type of the argument.
    /// \param OrigIndex Index of the original function argument, or
    ///        \c NoArgIndex for implicit machine-level inputs.
    /// \param Flags ISD argument flags; empty means a single default flag.
    /// \param OrigValue Optional original IR value for aliasing info.
    ArgInfo(ArrayRef<Register> Regs, Type *Ty, unsigned OrigIndex,
            ArrayRef<ISD::ArgFlagsTy> Flags = ArrayRef<ISD::ArgFlagsTy>(),
            const Value *OrigValue = nullptr)
        : BaseArgInfo(Ty, Flags), Regs(Regs), OrigValue(OrigValue),
          OrigArgIndex(OrigIndex) {
      if (!Regs.empty() && Flags.empty())
        this->Flags.push_back(ISD::ArgFlagsTy());
      // FIXME: We should have just one way of saying "no register".
      assert(((Ty->isVoidTy() || Ty->isEmptyTy()) ==
              (Regs.empty() || Regs[0] == 0)) &&
             "only void types should have no register");
    }

    /// Construct from registers and an IR value used as type and OrigValue.
    /// \param Regs Virtual registers holding the value.
    /// \param OrigValue IR value whose type and pointer are recorded.
    /// \param OrigIndex Index of the original function argument.
    /// \param Flags ISD argument flags; empty means a single default flag.
    ArgInfo(ArrayRef<Register> Regs, const Value &OrigValue, unsigned OrigIndex,
            ArrayRef<ISD::ArgFlagsTy> Flags = ArrayRef<ISD::ArgFlagsTy>())
        : ArgInfo(Regs, OrigValue.getType(), OrigIndex, Flags, &OrigValue) {}

    /// Construct an empty argument descriptor.
    ArgInfo() = default;
  };

  /// Pointer-authentication key and discriminator for an authenticated call.
  struct PtrAuthInfo {
    /// Pointer-authentication key used for the call.
    uint64_t Key;
    /// Register holding the pointer-authentication discriminator.
    Register Discriminator;
  };

  /// Bundled state describing a call to be lowered.
  struct CallLoweringInfo {
    /// Calling convention to be used for the call.
    CallingConv::ID CallConv = CallingConv::C;

    /// Destination of the call. It should be either a register, globaladdress,
    /// or externalsymbol.
    MachineOperand Callee = MachineOperand::CreateImm(0);

    /// Descriptor for the return type of the function.
    ArgInfo OrigRet;

    /// List of descriptors of the arguments passed to the function.
    SmallVector<ArgInfo, 32> OrigArgs;

    /// Valid if the call has a swifterror inout parameter, and contains the
    /// vreg that the swifterror should be copied into after the call.
    Register SwiftErrorVReg;

    /// Valid if the call is a controlled convergent operation.
    Register ConvergenceCtrlToken;

    /// Original IR callsite corresponding to this call, if available.
    const CallBase *CB = nullptr;

    /// Optional !callees metadata describing known callees of an indirect call.
    MDNode *KnownCallees = nullptr;

    /// The auth-call information in the "ptrauth" bundle, if present.
    std::optional<PtrAuthInfo> PAI;

    /// True if the call must be tail call optimized.
    bool IsMustTailCall = false;

    /// True if the call passes all target-independent checks for tail call
    /// optimization.
    bool IsTailCall = false;

    /// True if the call was lowered as a tail call. This is consumed by the
    /// legalizer. This allows the legalizer to lower libcalls as tail calls.
    bool LoweredTailCall = false;

    /// True if the call is to a vararg function.
    bool IsVarArg = false;

    /// True if the function's return value can be lowered to registers.
    bool CanLowerReturn = true;

    /// VReg to hold the hidden sret parameter.
    Register DemoteRegister;

    /// The stack index for sret demotion.
    int DemoteStackIndex;

    /// Expected type identifier for indirect calls with a CFI check.
    const ConstantInt *CFIType = nullptr;

    /// True if this call results in convergent operations.
    bool IsConvergent = true;

    /// Optional deactivation symbol for PGO instrumentation of the call.
    GlobalValue *DeactivationSymbol = nullptr;
  };

  /// Assigns calling-convention locations for args and returns.
  ///
  /// Argument handling is mostly uniform between the four places that
  /// make these decisions: function formal arguments, call
  /// instruction args, call instruction returns and function
  /// returns. However, once a decision has been made on where an
  /// argument should go, exactly what happens can vary slightly. This
  /// class abstracts the differences.
  ///
  /// ValueAssigner should not depend on any specific function state, and
  /// only determine the types and locations for arguments.
  struct LLVM_ABI ValueAssigner {
    /// Construct an assigner for incoming or outgoing values.
    /// \param IsIncoming True if assigning incoming arguments or returns.
    /// \param AssignFn_ Assignment function for a non-variadic call.
    /// \param AssignFnVarArg_ Assignment function for a variadic call; defaults
    ///        to \p AssignFn_ when null.
    ValueAssigner(bool IsIncoming, CCAssignFn *AssignFn_,
                  CCAssignFn *AssignFnVarArg_ = nullptr)
        : AssignFn(AssignFn_), AssignFnVarArg(AssignFnVarArg_),
          IsIncomingArgumentHandler(IsIncoming) {

      // Some targets change the handler depending on whether the call is
      // varargs or not. If
      if (!AssignFnVarArg)
        AssignFnVarArg = AssignFn;
    }

    /// Virtual destructor for polymorphic ValueAssigner subclasses.
    virtual ~ValueAssigner() = default;

    /// Returns true if the handler is dealing with incoming arguments,
    /// i.e. those that move values from some physical location to vregs.
    /// \return True if this handler deals with incoming arguments.
    bool isIncomingArgumentHandler() const {
      return IsIncomingArgumentHandler;
    }

    /// Assign one argument or return value via the target CCAssignFn.
    ///
    /// Wrap call to (typically tablegenerated CCAssignFn). This may be
    /// overridden to track additional state information as arguments are
    /// assigned or apply target specific hacks around the legacy
    /// infrastructure.
    /// \param ValNo Index of the value being assigned.
    /// \param OrigVT Original EVT of the value before legalization.
    /// \param ValVT MVT of the value being assigned.
    /// \param LocVT MVT of the assigned location.
    /// \param LocInfo LocInfo describing how the value is stored in LocVT.
    /// \param Info Descriptor for the argument or return value.
    /// \param Flags ISD argument flags for this value.
    /// \param State Calling-convention state being populated.
    /// \return True if the assignment failed.
    virtual bool assignArg(unsigned ValNo, EVT OrigVT, MVT ValVT, MVT LocVT,
                           CCValAssign::LocInfo LocInfo, const ArgInfo &Info,
                           ISD::ArgFlagsTy Flags, CCState &State) {
      if (getAssignFn(State.isVarArg())(ValNo, ValVT, LocVT, LocInfo, Flags,
                                        Info.Ty, State))
        return true;
      StackSize = State.getStackSize();
      return false;
    }

    /// Assignment function to use for a general call.
    CCAssignFn *AssignFn;

    /// Assignment function to use for a variadic call. This is usually the same
    /// as AssignFn on most targets.
    CCAssignFn *AssignFnVarArg;

    /// The size of the currently allocated portion of the stack.
    uint64_t StackSize = 0;

    /// Select the assignment function for a fixed or variadic call.
    /// \param IsVarArg True to use the variadic assignment function.
    /// \return The assignment function for a fixed or variadic call.
    CCAssignFn *getAssignFn(bool IsVarArg) const {
      return IsVarArg ? AssignFnVarArg : AssignFn;
    }

  private:
    const bool IsIncomingArgumentHandler;
    virtual void anchor();
  };

  /// ValueAssigner specialized for incoming formal arguments and call results.
  struct IncomingValueAssigner : public ValueAssigner {
    /// Construct an incoming-value assigner.
    /// \param AssignFn_ Assignment function for a non-variadic call.
    /// \param AssignFnVarArg_ Assignment function for a variadic call.
    IncomingValueAssigner(CCAssignFn *AssignFn_,
                          CCAssignFn *AssignFnVarArg_ = nullptr)
        : ValueAssigner(true, AssignFn_, AssignFnVarArg_) {}
  };

  /// ValueAssigner specialized for outgoing call arguments and returns.
  struct OutgoingValueAssigner : public ValueAssigner {
    /// Construct an outgoing-value assigner.
    /// \param AssignFn_ Assignment function for a non-variadic call.
    /// \param AssignFnVarArg_ Assignment function for a variadic call.
    OutgoingValueAssigner(CCAssignFn *AssignFn_,
                          CCAssignFn *AssignFnVarArg_ = nullptr)
        : ValueAssigner(false, AssignFn_, AssignFnVarArg_) {}
  };

  /// Moves values between virtual registers and CC-assigned locations.
  struct LLVM_ABI ValueHandler {
    /// Builder used to insert the assignment instructions.
    MachineIRBuilder &MIRBuilder;
    /// Register info for the function being lowered.
    MachineRegisterInfo &MRI;
    /// True if this handler moves values into the current function.
    const bool IsIncomingArgumentHandler;

    /// Construct a handler for incoming or outgoing values.
    /// \param IsIncoming True for incoming arguments or received returns.
    /// \param MIRBuilder Builder used to insert assignment instructions.
    /// \param MRI Register info for the function being lowered.
    ValueHandler(bool IsIncoming, MachineIRBuilder &MIRBuilder,
                 MachineRegisterInfo &MRI)
        : MIRBuilder(MIRBuilder), MRI(MRI),
          IsIncomingArgumentHandler(IsIncoming) {}

    /// Virtual destructor for polymorphic ValueHandler subclasses.
    virtual ~ValueHandler() = default;

    /// Returns true if the handler is dealing with incoming arguments,
    /// i.e. those that move values from some physical location to vregs.
    /// \return True if this handler deals with incoming arguments.
    bool isIncomingArgumentHandler() const {
      return IsIncomingArgumentHandler;
    }

    /// Materialize a VReg holding the address of a stack-passed object.
    ///
    /// This is either based on a FrameIndex or direct SP manipulation,
    /// depending on the context. \p MPO should be initialized to an
    /// appropriate description of the address created.
    /// \param MemSize Size in bytes of the stack object being addressed.
    /// \param Offset Byte offset from the stack object's base.
    /// \param MPO Machine pointer info to initialize for the address.
    /// \param Flags ISD argument flags for the stack-passed value.
    /// \return Virtual register holding the materialized address.
    virtual Register getStackAddress(uint64_t MemSize, int64_t Offset,
                                     MachinePointerInfo &MPO,
                                     ISD::ArgFlagsTy Flags) = 0;

    /// Return the in-memory store type for the argument at \p VA.
    ///
    /// This may be smaller than the allocated stack slot size.
    ///
    /// This is overridable primarily for targets to maintain compatibility with
    /// hacks around the existing DAG call lowering infrastructure.
    /// \param DL Data layout used to compute the store type.
    /// \param VA Calling-convention assignment describing the stack location.
    /// \param Flags ISD argument flags for the value.
    /// \return LLT of the in-memory store type for the argument at \p VA.
    virtual LLT getStackValueStoreType(const DataLayout &DL,
                                       const CCValAssign &VA,
                                       ISD::ArgFlagsTy Flags) const;

    /// Assign a value between a virtual and physical register.
    ///
    /// The specified value has been assigned to a physical register,
    /// handle the appropriate COPY (either to or from) and mark any
    /// relevant uses/defines as needed.
    /// \param ValVReg Virtual register holding the IR-level value.
    /// \param PhysReg Physical register assigned by the calling convention.
    /// \param VA Calling-convention assignment for this value.
    /// \param Flags ISD argument flags for this value.
    virtual void assignValueToReg(Register ValVReg, Register PhysReg,
                                  const CCValAssign &VA,
                                  ISD::ArgFlagsTy Flags) = 0;

    /// Assign a value to or from a stack location.
    ///
    /// The specified value has been assigned to a stack location. Load or
    /// store it there, with appropriate extension if necessary.
    /// \param ValVReg Virtual register holding the IR-level value.
    /// \param Addr Virtual register holding the stack address.
    /// \param MemTy LLT of the memory access.
    /// \param MPO Machine pointer info for the stack access.
    /// \param VA Calling-convention assignment for this value.
    virtual void assignValueToAddress(Register ValVReg, Register Addr,
                                      LLT MemTy, const MachinePointerInfo &MPO,
                                      const CCValAssign &VA) = 0;

    /// Assign a value to or from a stack location using ArgInfo.
    ///
    /// An overload which takes an ArgInfo if additional information about the
    /// arg is needed. \p ValRegIndex is the index in \p Arg.Regs for the value
    /// to store.
    /// \param Arg Argument descriptor providing registers and flags.
    /// \param ValRegIndex Index into \p Arg.Regs of the value to assign.
    /// \param Addr Virtual register holding the stack address.
    /// \param MemTy LLT of the memory access.
    /// \param MPO Machine pointer info for the stack access.
    /// \param VA Calling-convention assignment for this value.
    virtual void assignValueToAddress(const ArgInfo &Arg, unsigned ValRegIndex,
                                      Register Addr, LLT MemTy,
                                      const MachinePointerInfo &MPO,
                                      const CCValAssign &VA) {
      assignValueToAddress(Arg.Regs[ValRegIndex], Addr, MemTy, MPO, VA);
    }

    /// Handle a custom calling-convention value assignment.
    ///
    /// Handle custom values, which may be passed into one or more of \p VAs.
    /// If the handler wants the assignments to be delayed until after
    /// mem loc assignments, then it sets \p Thunk to the thunk to do the
    /// assignment.
    /// \param Arg Argument descriptor being assigned.
    /// \param VAs Calling-convention assignments covering the custom value.
    /// \param Thunk Optional out-parameter set to a delayed assignment thunk.
    /// \return The number of \p VAs that have been assigned including the
    ///         first one, and which should therefore be skipped from further
    ///         processing.
    virtual unsigned assignCustomValue(ArgInfo &Arg, ArrayRef<CCValAssign> VAs,
                                       std::function<void()> *Thunk = nullptr) {
      // This is not a pure virtual method because not all targets need to worry
      // about custom values.
      llvm_unreachable("Custom values not supported");
    }

    /// Copy \p MemSize bytes from \p SrcPtr to \p DstPtr for byval args.
    ///
    /// Do a memory copy of \p MemSize bytes from \p SrcPtr to \p DstPtr. This
    /// is necessary for outgoing stack-passed byval arguments.
    /// \param Arg Argument descriptor for the byval value.
    /// \param DstPtr Destination pointer register.
    /// \param SrcPtr Source pointer register.
    /// \param DstPtrInfo Machine pointer info for the destination.
    /// \param DstAlign Alignment of the destination.
    /// \param SrcPtrInfo Machine pointer info for the source.
    /// \param SrcAlign Alignment of the source.
    /// \param MemSize Number of bytes to copy.
    /// \param VA Calling-convention assignment for this memory argument.
    void
    copyArgumentMemory(const ArgInfo &Arg, Register DstPtr, Register SrcPtr,
                       const MachinePointerInfo &DstPtrInfo, Align DstAlign,
                       const MachinePointerInfo &SrcPtrInfo, Align SrcAlign,
                       uint64_t MemSize, CCValAssign &VA) const;

    /// Extend \p ValReg to the location type in \p VA, capped by \p MaxSizeBits.
    ///
    /// Extend a register to the location type given in VA, capped at extending
    /// to at most MaxSize bits. If MaxSizeBits is 0 then no maximum is set.
    /// \param ValReg Register to extend.
    /// \param VA Calling-convention assignment providing the location type.
    /// \param MaxSizeBits Maximum bit width to extend to, or 0 for no maximum.
    /// \return Register holding the extended value.
    Register extendRegister(Register ValReg, const CCValAssign &VA,
                            unsigned MaxSizeBits = 0);
  };

  /// Base class for ValueHandlers used for arguments coming into the current
  /// function, or for return values received from a call.
  struct LLVM_ABI IncomingValueHandler : public ValueHandler {
    /// Construct an incoming-value handler.
    /// \param MIRBuilder Builder used to insert assignment instructions.
    /// \param MRI Register info for the function being lowered.
    IncomingValueHandler(MachineIRBuilder &MIRBuilder, MachineRegisterInfo &MRI)
        : ValueHandler(/*IsIncoming*/ true, MIRBuilder, MRI) {}

    /// Insert an extension hint based on \p VA if needed.
    ///
    /// Insert G_ASSERT_ZEXT/G_ASSERT_SEXT or other hint instruction based on \p
    /// VA, returning the new register if a hint was inserted.
    /// \param VA Calling-convention assignment describing the extension.
    /// \param SrcReg Source register to hint or extend.
    /// \param NarrowTy Narrow LLT of the source value before extension.
    /// \return Register after any inserted hint, or \p SrcReg if none.
    Register buildExtensionHint(const CCValAssign &VA, Register SrcReg,
                                LLT NarrowTy);

    /// Assign an incoming value from a physical register to a vreg.
    ///
    /// Provides a default implementation for argument handling.
    /// \param ValVReg Destination virtual register.
    /// \param PhysReg Source physical register.
    /// \param VA Calling-convention assignment for this value.
    /// \param Flags ISD argument flags for this value.
    void assignValueToReg(Register ValVReg, Register PhysReg,
                          const CCValAssign &VA,
                          ISD::ArgFlagsTy Flags = {}) override;
  };

  /// Base class for ValueHandlers used for arguments passed to a function call,
  /// or for return values.
  struct OutgoingValueHandler : public ValueHandler {
    /// Construct an outgoing-value handler.
    /// \param MIRBuilder Builder used to insert assignment instructions.
    /// \param MRI Register info for the function being lowered.
    OutgoingValueHandler(MachineIRBuilder &MIRBuilder, MachineRegisterInfo &MRI)
        : ValueHandler(/*IsIncoming*/ false, MIRBuilder, MRI) {}
  };

protected:
  /// Getter for generic TargetLowering class.
  /// \return Pointer to the generic TargetLowering instance.
  const TargetLowering *getTLI() const {
    return TLI;
  }

  /// Getter for target specific TargetLowering class.
  /// \return Pointer to the target-specific TargetLowering instance.
  template <class XXXTargetLowering>
    const XXXTargetLowering *getTLI() const {
    return static_cast<const XXXTargetLowering *>(TLI);
  }

  /// Return ISD flags for the attributes on call argument \p ArgIdx.
  /// \param Call Call instruction providing the attributes.
  /// \param ArgIdx Index of the argument whose attributes are read.
  /// \return ISD argument flags for the attributes on argument \p ArgIdx.
  ISD::ArgFlagsTy getAttributesForArgIdx(const CallBase &Call,
                                         unsigned ArgIdx) const;

  /// Return ISD flags for the attributes on the return of \p Call.
  /// \param Call Call instruction providing the return attributes.
  /// \return ISD argument flags for the return attributes of \p Call.
  ISD::ArgFlagsTy getAttributesForReturn(const CallBase &Call) const;

  /// Add ISD flags from attribute list entry \p OpIdx into \p Flags.
  /// \param Flags Flags to update.
  /// \param Attrs Attribute list to read.
  /// \param OpIdx Index in \p Attrs from which to add flags.
  void addArgFlagsFromAttributes(ISD::ArgFlagsTy &Flags,
                                 const AttributeList &Attrs,
                                 unsigned OpIdx) const;

  /// Set ISD argument flags on \p Arg from \p FuncInfo at operand \p OpIdx.
  /// \param Arg Argument descriptor whose flags are updated.
  /// \param OpIdx Operand index used to look up attributes.
  /// \param DL Data layout used when deriving flag details.
  /// \param FuncInfo Function or CallBase providing the attributes.
  template <typename FuncInfoTy>
  void setArgFlags(ArgInfo &Arg, unsigned OpIdx, const DataLayout &DL,
                   const FuncInfoTy &FuncInfo) const;

  /// Split \p OrigArgInfo into CC-processable pieces in \p SplitArgs.
  ///
  /// Break \p OrigArgInfo into one or more pieces the calling convention can
  /// process, returned in \p SplitArgs. For example, this should break structs
  /// down into individual fields.
  ///
  /// If \p Offsets is non-null, it points to a vector to be filled in
  /// with the in-memory offsets of each of the individual values.
  /// \param OrigArgInfo Original argument descriptor to split.
  /// \param SplitArgs Output vector of split argument descriptors.
  /// \param DL Data layout used when computing field layouts.
  /// \param CallConv Calling convention that governs the split.
  /// \param Offsets Optional output of in-memory offsets for each piece.
  void splitToValueTypes(const ArgInfo &OrigArgInfo,
                         SmallVectorImpl<ArgInfo> &SplitArgs,
                         const DataLayout &DL, CallingConv::ID CallConv,
                         SmallVectorImpl<TypeSize> *Offsets = nullptr) const;

  /// Analyze \p Args with \p Assigner to populate locations in \p CCInfo.
  ///
  /// Analyze the argument list in \p Args, using \p Assigner to populate \p
  /// CCInfo. This will determine the types and locations to use for passed or
  /// returned values. This may resize fields in \p Args if the value is split
  /// across multiple registers or stack slots.
  ///
  /// This is independent of the function state and can be used
  /// to determine how a call would pass arguments without needing to change the
  /// function. This can be used to check if arguments are suitable for tail
  /// call lowering.
  ///
  /// \param Assigner Assigner used to choose locations for each value.
  /// \param Args Argument descriptors to analyze; may be resized on split.
  /// \param CCInfo Calling-convention state to populate.
  /// \return True if everything has succeeded, false otherwise.
  bool determineAssignments(ValueAssigner &Assigner,
                            SmallVectorImpl<ArgInfo> &Args,
                            CCState &CCInfo) const;

  /// Assign and then handle each of \p Args with \p Assigner and \p Handler.
  ///
  /// Invoke ValueAssigner::assignArg on each of the given \p Args and then use
  /// \p Handler to move them to the assigned locations.
  ///
  /// \param Handler Handler that moves values to assigned locations.
  /// \param Assigner Assigner that chooses locations for each value.
  /// \param Args Argument descriptors to assign and handle.
  /// \param MIRBuilder Builder used to insert assignment instructions.
  /// \param CallConv Calling convention for the assignment.
  /// \param IsVarArg True if the call or function is variadic.
  /// \param ThisReturnRegs Optional registers for a "this return" value.
  /// \return True if everything has succeeded, false otherwise.
  bool
  determineAndHandleAssignments(ValueHandler &Handler, ValueAssigner &Assigner,
                                SmallVectorImpl<ArgInfo> &Args,
                                MachineIRBuilder &MIRBuilder,
                                CallingConv::ID CallConv, bool IsVarArg,
                                ArrayRef<Register> ThisReturnRegs = {}) const;

  /// Insert code to move \p Args according to prior CC assignments.
  ///
  /// Use \p Handler to insert code to handle the argument/return values
  /// represented by \p Args. It's expected determineAssignments previously
  /// processed these arguments to populate \p CCState and \p ArgLocs.
  /// \param Handler Handler that moves values to assigned locations.
  /// \param Args Argument descriptors to handle.
  /// \param CCState Calling-convention state from determineAssignments.
  /// \param ArgLocs Locations assigned to each argument component.
  /// \param MIRBuilder Builder used to insert assignment instructions.
  /// \param ThisReturnRegs Optional registers for a "this return" value.
  /// \return True if the assignments were handled successfully.
  bool handleAssignments(ValueHandler &Handler, SmallVectorImpl<ArgInfo> &Args,
                         CCState &CCState,
                         SmallVectorImpl<CCValAssign> &ArgLocs,
                         MachineIRBuilder &MIRBuilder,
                         ArrayRef<Register> ThisReturnRegs = {}) const;

  /// Check CSR-passed call args match the caller's incoming values.
  ///
  /// Check whether parameters to a call that are passed in callee saved
  /// registers are the same as from the calling function. This needs to be
  /// checked for tail call eligibility.
  /// \param MRI Register info for the calling function.
  /// \param CallerPreservedMask Callee-saved register mask of the caller.
  /// \param ArgLocs Locations assigned to the outgoing call arguments.
  /// \param OutVals Descriptors of the outgoing call argument values.
  /// \return True if CSR-passed call args match the caller's incoming values.
  bool parametersInCSRMatch(const MachineRegisterInfo &MRI,
                            const uint32_t *CallerPreservedMask,
                            const SmallVectorImpl<CCValAssign> &ArgLocs,
                            const SmallVectorImpl<ArgInfo> &OutVals) const;

  /// Return true if callee and caller pass results the same way.
  ///
  /// Typically used for tail call eligibility checks.
  /// \param Info CallLoweringInfo for the call.
  /// \param MF MachineFunction for the caller.
  /// \param InArgs Results of the call.
  /// \param CalleeAssigner Target handling of argument types for the callee.
  /// \param CallerAssigner Target handling of argument types for the caller.
  /// \return True if callee and caller pass results the same way.
  bool resultsCompatible(CallLoweringInfo &Info, MachineFunction &MF,
                         SmallVectorImpl<ArgInfo> &InArgs,
                         ValueAssigner &CalleeAssigner,
                         ValueAssigner &CallerAssigner) const;

public:
  /// Construct a CallLowering using the target's TargetLowering.
  /// \param TLI Target lowering info providing calling-convention hooks.
  CallLowering(const TargetLowering *TLI) : TLI(TLI) {}
  /// Virtual destructor for polymorphic CallLowering subclasses.
  virtual ~CallLowering() = default;

  /// Return true if the target supports swifterror in a dedicated register.
  ///
  /// The extended versions of lowerReturn and lowerCall should be implemented.
  /// \return True if the target supports swifterror in a dedicated register.
  virtual bool supportSwiftError() const {
    return false;
  }

  /// Load an sret return value from the stack into \p VRegs.
  ///
  /// Load the returned value from the stack into virtual registers in \p VRegs.
  /// It uses the frame index \p FI and the start offset from \p DemoteReg.
  /// The loaded data size will be determined from \p RetTy.
  /// \param MIRBuilder Builder used to insert the loads.
  /// \param RetTy IR return type determining the loaded data size.
  /// \param VRegs Destination virtual registers for the loaded value.
  /// \param DemoteReg Register holding the sret pointer / offset base.
  /// \param FI Frame index of the sret stack slot.
  void insertSRetLoads(MachineIRBuilder &MIRBuilder, Type *RetTy,
                       ArrayRef<Register> VRegs, Register DemoteReg,
                       int FI) const;

  /// Store return registers \p VRegs through the hidden sret pointer.
  ///
  /// Store the return value given by \p VRegs into stack starting at the offset
  /// specified in \p DemoteReg.
  /// \param MIRBuilder Builder used to insert the stores.
  /// \param RetTy IR return type determining the stored data size.
  /// \param VRegs Source virtual registers holding the return value.
  /// \param DemoteReg Register holding the sret pointer / offset base.
  void insertSRetStores(MachineIRBuilder &MIRBuilder, Type *RetTy,
                        ArrayRef<Register> VRegs, Register DemoteReg) const;

  /// Insert a hidden sret argument at the front of \p SplitArgs.
  ///
  /// This function should be called from the target specific
  /// lowerFormalArguments when \p F requires the sret demotion.
  /// \param F Function whose formal arguments are being lowered.
  /// \param SplitArgs Argument list to prepend the hidden sret arg to.
  /// \param DemoteReg Set to the vreg created for the sret pointer.
  /// \param MRI Register info used to create the demote register.
  /// \param DL Data layout used when building the sret argument.
  void insertSRetIncomingArgument(const Function &F,
                                  SmallVectorImpl<ArgInfo> &SplitArgs,
                                  Register &DemoteReg, MachineRegisterInfo &MRI,
                                  const DataLayout &DL) const;

  /// Insert a hidden sret argument for call \p CB into \p Info.
  ///
  /// For the call-base described by \p CB, insert the hidden sret ArgInfo to
  /// the OrigArgs field of \p Info.
  /// \param MIRBuilder Builder used when materializing the sret argument.
  /// \param CB Call whose return requires sret demotion.
  /// \param Info CallLoweringInfo whose OrigArgs receive the sret argument.
  void insertSRetOutgoingArgument(MachineIRBuilder &MIRBuilder,
                                  const CallBase &CB,
                                  CallLoweringInfo &Info) const;

  /// Combine register-typed pieces in \p Regs into \p OrigRegs.
  ///
  /// Create a sequence of instructions to combine pieces split into register
  /// typed values to the original IR value. \p OrigRegs contains the
  /// destination value registers of type \p LLTy, and \p Regs contains the
  /// legalized pieces with type \p PartLLT. This is used for incoming values
  /// (physregs to vregs).
  /// \param B Builder used to insert the combine instructions.
  /// \param OrigRegs Destination registers of type \p LLTy.
  /// \param Regs Legalized piece registers of type \p PartLLT.
  /// \param LLTy LLT of the original combined value.
  /// \param PartLLT LLT of each legalized piece.
  /// \param Flags ISD argument flags controlling the combine.
  static void buildCopyFromRegs(MachineIRBuilder &B,
                                ArrayRef<Register> OrigRegs,
                                ArrayRef<Register> Regs, LLT LLTy, LLT PartLLT,
                                const ISD::ArgFlagsTy Flags);

  /// Expand \p SrcReg into the register pieces in \p DstRegs.
  ///
  /// Create a sequence of instructions to expand the value in \p SrcReg (of
  /// type \p SrcTy) to the types in \p DstRegs (of type \p PartTy). \p ExtendOp
  /// should contain the type of scalar value extension if necessary.
  ///
  /// This is used for outgoing values (vregs to physregs).
  /// \param B Builder used to insert the expand instructions.
  /// \param DstRegs Destination piece registers of type \p PartTy.
  /// \param SrcReg Source register of type \p SrcTy.
  /// \param SrcTy LLT of the source value.
  /// \param PartTy LLT of each destination piece.
  /// \param ExtendOp Opcode used to extend scalar pieces when needed.
  static void buildCopyToRegs(MachineIRBuilder &B, ArrayRef<Register> DstRegs,
                              Register SrcReg, LLT SrcTy, LLT PartTy,
                              unsigned ExtendOp = TargetOpcode::G_ANYEXT);

  /// Return true if \p Outs can be returned without sret demotion.
  /// \param CCInfo Calling-convention state used for the check.
  /// \param Outs Split return-value descriptors to check.
  /// \param Fn Calling-convention assignment function to apply.
  /// \return True if \p Outs can be returned without sret demotion.
  bool checkReturn(CCState &CCInfo, SmallVectorImpl<BaseArgInfo> &Outs,
                   CCAssignFn *Fn) const;

  /// Fill \p Outs with split return types and flags for \p RetTy.
  ///
  /// Get the type and the ArgFlags for the split components of \p RetTy as
  /// returned by \c ComputeValueVTs.
  /// \param CallConv Calling convention used for the return.
  /// \param RetTy IR return type to split.
  /// \param Attrs Attribute list providing return attributes.
  /// \param Outs Output vector of split return descriptors.
  /// \param DL Data layout used when computing the split.
  void getReturnInfo(CallingConv::ID CallConv, Type *RetTy, AttributeList Attrs,
                     SmallVectorImpl<BaseArgInfo> &Outs,
                     const DataLayout &DL) const;

  /// Check whether \p MF's return can avoid sret demotion.
  ///
  /// Toplevel function to check the return type based on the target calling
  /// convention. \return True if the return value of \p MF can be returned
  /// without performing sret demotion.
  /// \param MF Function whose return type is checked.
  bool checkReturnTypeForCallConv(MachineFunction &MF) const;

  /// Return true if return values in \p Outs fit in return registers.
  ///
  /// This hook must be implemented to check whether the return values
  /// described by \p Outs can fit into the return registers. If false
  /// is returned, an sret-demotion is performed.
  /// \param MF Function whose return is being checked.
  /// \param CallConv Calling convention of the function.
  /// \param Outs Split return-value descriptors to check.
  /// \param IsVarArg True if the function is variadic.
  /// \return True if the return values fit in registers without sret demotion.
  virtual bool canLowerReturn(MachineFunction &MF, CallingConv::ID CallConv,
                              SmallVectorImpl<BaseArgInfo> &Outs,
                              bool IsVarArg) const {
    return true;
  }

  /// Lower outgoing return value \p Val into registers \p VRegs.
  ///
  /// This hook must be implemented to lower outgoing return values, described
  /// by \p Val, into the specified virtual registers \p VRegs.
  /// This hook is used by GlobalISel.
  ///
  /// \p FLI is required for sret demotion.
  ///
  /// \p SwiftErrorVReg is non-zero if the function has a swifterror parameter
  /// that needs to be implicitly returned.
  ///
  /// \param MIRBuilder Builder used to insert the return lowering.
  /// \param Val IR return value, or null if there is none.
  /// \param VRegs Virtual registers holding the return value pieces.
  /// \param FLI Function lowering info, required for sret demotion.
  /// \param SwiftErrorVReg Non-zero if a swifterror value must be returned.
  /// \return True if the lowering succeeds, false otherwise.
  virtual bool lowerReturn(MachineIRBuilder &MIRBuilder, const Value *Val,
                           ArrayRef<Register> VRegs, FunctionLoweringInfo &FLI,
                           Register SwiftErrorVReg) const {
    if (!supportSwiftError()) {
      assert(SwiftErrorVReg == 0 && "attempt to use unsupported swifterror");
      return lowerReturn(MIRBuilder, Val, VRegs, FLI);
    }
    return false;
  }

  /// Lower a return for targets without swifterror promotion.
  ///
  /// This hook behaves as the extended lowerReturn function, but for targets
  /// that do not support swifterror value promotion.
  /// \param MIRBuilder Builder used to insert the return lowering.
  /// \param Val IR return value, or null if there is none.
  /// \param VRegs Virtual registers holding the return value pieces.
  /// \param FLI Function lowering info, required for sret demotion.
  /// \return True if the lowering succeeds, false otherwise.
  virtual bool lowerReturn(MachineIRBuilder &MIRBuilder, const Value *Val,
                           ArrayRef<Register> VRegs,
                           FunctionLoweringInfo &FLI) const {
    return false;
  }

  /// Return true if \p MF should fall back from GlobalISel to DAG ISel.
  /// \param MF Function being considered for GlobalISel.
  /// \return True if \p MF should fall back to DAG ISel.
  virtual bool fallBackToDAGISel(const MachineFunction &MF) const {
    return false;
  }

  /// Lower formal arguments of \p F into the registers in \p VRegs.
  ///
  /// This hook must be implemented to lower the incoming (formal)
  /// arguments, described by \p VRegs, for GlobalISel. Each argument
  /// must end up in the related virtual registers described by \p VRegs.
  /// In other words, the first argument should end up in \c VRegs[0],
  /// the second in \c VRegs[1], and so on. For each argument, there will be one
  /// register for each non-aggregate type, as returned by \c computeValueLLTs.
  /// \p MIRBuilder is set to the proper insertion for the argument
  /// lowering. \p FLI is required for sret demotion.
  ///
  /// \param MIRBuilder Builder positioned for formal-argument lowering.
  /// \param F Function whose formal arguments are being lowered.
  /// \param VRegs Per-argument lists of destination virtual registers.
  /// \param FLI Function lowering info, required for sret demotion.
  /// \return True if the lowering succeeded, false otherwise.
  virtual bool lowerFormalArguments(MachineIRBuilder &MIRBuilder,
                                    const Function &F,
                                    ArrayRef<ArrayRef<Register>> VRegs,
                                    FunctionLoweringInfo &FLI) const {
    return false;
  }

  /// Lower the call described by \p Info, including args and return.
  ///
  /// This hook must be implemented to lower the given call instruction,
  /// including argument and return value marshalling.
  ///
  /// \param MIRBuilder Builder used to insert the call lowering.
  /// \param Info Bundled state describing the call to lower.
  /// \return true if the lowering succeeded, false otherwise.
  virtual bool lowerCall(MachineIRBuilder &MIRBuilder,
                         CallLoweringInfo &Info) const {
    return false;
  }

  /// Lower call \p Call, including argument and return marshalling.
  ///
  /// \param MIRBuilder Builder used to insert the call lowering.
  /// \param Call Call or invoke instruction being lowered.
  /// \param ResRegs Registers where the call's return value should be stored
  ///        (or 0 if there is no return value). There will be one register for
  ///        each non-aggregate type, as returned by \c computeValueLLTs.
  /// \param ArgRegs Per-argument lists of virtual registers containing each
  ///        argument that needs to be passed (argument \c i should be placed in
  ///        \c ArgRegs[i]). For each argument, there will be one register for
  ///        each non-aggregate type, as returned by \c computeValueLLTs.
  /// \param SwiftErrorVReg Non-zero if the call has a swifterror inout
  ///        parameter, containing the vreg that the swifterror should be copied
  ///        into after the call.
  /// \param PAI Optional pointer-authentication info from a "ptrauth" bundle.
  /// \param ConvergenceCtrlToken Token for a controlled convergent operation.
  /// \param GetCalleeReg Callback to materialize a register for the callee if
  ///        the target cannot jump to the destination based purely on \p Call.
  ///        This might be because \p Call is indirect, or because of the
  ///        limited range of an immediate jump.
  /// \return true if the lowering succeeded, false otherwise.
  bool lowerCall(MachineIRBuilder &MIRBuilder, const CallBase &Call,
                 ArrayRef<Register> ResRegs,
                 ArrayRef<ArrayRef<Register>> ArgRegs, Register SwiftErrorVReg,
                 std::optional<PtrAuthInfo> PAI, Register ConvergenceCtrlToken,
                 std::function<Register()> GetCalleeReg) const;

  /// For targets which want to use big-endian can enable it with
  /// enableBigEndian() hook
  /// \return True if the target uses big-endian call lowering.
  virtual bool enableBigEndian() const { return false; }

  /// Return true if \p Ty is valid with the "returned" parameter attribute.
  ///
  /// For targets which support the "returned" parameter attribute, returns
  /// true if the given type is a valid one to use with "returned".
  /// \param Ty Type proposed for use with the "returned" attribute.
  /// \return True if \p Ty is valid with the "returned" attribute.
  virtual bool isTypeIsValidForThisReturn(EVT Ty) const { return false; }
};

extern template LLVM_ABI void
CallLowering::setArgFlags<Function>(CallLowering::ArgInfo &Arg, unsigned OpIdx,
                                    const DataLayout &DL,
                                    const Function &FuncInfo) const;

extern template LLVM_ABI void
CallLowering::setArgFlags<CallBase>(CallLowering::ArgInfo &Arg, unsigned OpIdx,
                                    const DataLayout &DL,
                                    const CallBase &FuncInfo) const;
} // end namespace llvm

#endif // LLVM_CODEGEN_GLOBALISEL_CALLLOWERING_H
