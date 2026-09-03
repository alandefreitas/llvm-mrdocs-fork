//===-- llvm/CodeGen/TargetCallingConv.h - Calling Convention ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines types for working with calling-convention information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_TARGETCALLINGCONV_H
#define LLVM_CODEGEN_TARGETCALLINGCONV_H

#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/MathExtras.h"
#include <cassert>
#include <climits>
#include <cstdint>

namespace llvm {
namespace ISD {

  /// Flags describing how a single calling-convention argument is passed.
  struct ArgFlagsTy {
  public:
    /// Flag bits describing an argument.
    enum Flags : uint32_t {
      NoFlags = 0, ///< No special flags
      ZExt = 1U << 0,     ///< Zero extended
      SExt = 1U << 1,     ///< Sign extended
      NoExt = 1U << 2,    ///< No extension
      InReg = 1U << 3,    ///< Passed in register
      SRet = 1U << 4,     ///< Hidden struct-ret ptr
      ByVal = 1U << 5,    ///< Struct passed by value
      ByRef = 1U << 6,    ///< Passed in memory
      Nest = 1U << 7,     ///< Nested fn static chain
      Returned = 1U << 8, ///< Always returned
      Split = 1U << 9,    ///< Part of a split argument
      InAlloca = 1U << 10,      ///< Passed with inalloca
      Preallocated = 1U << 11,  ///< ByVal without the copy
      SplitEnd = 1U << 12,      ///< Last part of a split
      SwiftSelf = 1U << 13,     ///< Swift self parameter
      SwiftAsync = 1U << 14,    ///< Swift async context parameter
      SwiftError = 1U << 15,    ///< Swift error parameter
      CFGuardTarget = 1U << 16, ///< Control Flow Guard target
      Hva = 1U << 17,           ///< HVA field
      HvaStart = 1U << 18,      ///< HVA structure start
      SecArgPass = 1U << 19,    ///< Second argument
      InConsecutiveRegsLast = 1U << 20, ///< Last of consecutive registers
      InConsecutiveRegs = 1U << 21,     ///< Passed in consecutive registers
      CopyElisionCandidate = 1U << 22, ///< Argument copy elision candidate
      Pointer = 1U << 23, ///< Pointer argument
      /// Whether this is part of a variable argument list (non-fixed).
      VarArg = 1U << 24,

      LLVM_MARK_AS_BITMASK_ENUM(/* LargestFlag = */ VarArg)
    };

  private:
    Flags FlagVals = NoFlags;
    unsigned MemAlign : 6;  ///< Log 2 of alignment when arg is passed in memory
                            ///< (including byval/byref). The max alignment is
                            ///< verified in IR verification.
    unsigned OrigAlign : 5; ///< Log 2 of original alignment

    unsigned ByValOrByRefSize = 0; ///< Byval or byref struct size

    unsigned PointerAddrSpace = 0; ///< Address space of pointer argument

    void setFlag(Flags Flag, bool Value = true) {
      FlagVals = (FlagVals & ~Flag) | (Value ? Flag : NoFlags);
    }

  public:
    /// Construct default argument flags with zero alignments.
    ArgFlagsTy() : MemAlign(0), OrigAlign(0) {
      static_assert(sizeof(*this) == 4 * sizeof(unsigned), "flags are too big");
    }

    /// Return the argument's boolean flags.
    /// \return The combined Flags bitmask for this argument.
    Flags getFlags() const { return FlagVals; }

    /// Return true if the argument is zero-extended.
    /// \return True if the zero-extend flag is set.
    bool isZExt() const { return FlagVals & ZExt; }
    /// Mark the argument as zero-extended.
    void setZExt() { setFlag(ZExt); }

    /// Return true if the argument is sign-extended.
    /// \return True if the sign-extend flag is set.
    bool isSExt() const { return FlagVals & SExt; }
    /// Mark the argument as sign-extended.
    void setSExt() { setFlag(SExt); }

    /// Return true if the argument has no extension.
    /// \return True if the no-extension flag is set.
    bool isNoExt() const { return FlagVals & NoExt; }
    /// Mark the argument as having no extension.
    void setNoExt() { setFlag(NoExt); }

    /// Return true if the argument is passed in a register.
    /// \return True if the in-register flag is set.
    bool isInReg() const { return FlagVals & InReg; }
    /// Mark the argument as passed in a register.
    void setInReg() { setFlag(InReg); }

    /// Return true if the argument is a hidden struct-return pointer.
    /// \return True if the struct-return flag is set.
    bool isSRet() const { return FlagVals & SRet; }
    /// Mark the argument as a hidden struct-return pointer.
    void setSRet() { setFlag(SRet); }

    /// Return true if the argument is a struct passed by value.
    /// \return True if the byval flag is set.
    bool isByVal() const { return FlagVals & ByVal; }
    /// Mark the argument as a struct passed by value.
    void setByVal() { setFlag(ByVal); }

    /// Return true if the argument is passed by reference in memory.
    /// \return True if the byref flag is set.
    bool isByRef() const { return FlagVals & ByRef; }
    /// Mark the argument as passed by reference in memory.
    void setByRef() { setFlag(ByRef); }

    /// Return true if the argument is passed with inalloca.
    /// \return True if the inalloca flag is set.
    bool isInAlloca() const { return FlagVals & InAlloca; }
    /// Mark the argument as passed with inalloca.
    void setInAlloca() { setFlag(InAlloca); }

    /// Return true if the argument is preallocated (byval without the copy).
    /// \return True if the preallocated flag is set.
    bool isPreallocated() const { return FlagVals & Preallocated; }
    /// Mark the argument as preallocated (byval without the copy).
    void setPreallocated() { setFlag(Preallocated); }

    /// Return true if the argument is a Swift self parameter.
    /// \return True if the Swift self flag is set.
    bool isSwiftSelf() const { return FlagVals & SwiftSelf; }
    /// Mark the argument as a Swift self parameter.
    void setSwiftSelf() { setFlag(SwiftSelf); }

    /// Return true if the argument is a Swift async context parameter.
    /// \return True if the Swift async flag is set.
    bool isSwiftAsync() const { return FlagVals & SwiftAsync; }
    /// Mark the argument as a Swift async context parameter.
    void setSwiftAsync() { setFlag(SwiftAsync); }

    /// Return true if the argument is a Swift error parameter.
    /// \return True if the Swift error flag is set.
    bool isSwiftError() const { return FlagVals & SwiftError; }
    /// Mark the argument as a Swift error parameter.
    void setSwiftError() { setFlag(SwiftError); }

    /// Return true if the argument is a Control Flow Guard target.
    /// \return True if the Control Flow Guard target flag is set.
    bool isCFGuardTarget() const { return FlagVals & CFGuardTarget; }
    /// Mark the argument as a Control Flow Guard target.
    void setCFGuardTarget() { setFlag(CFGuardTarget); }

    /// Return true if the argument is an HVA field.
    /// \return True if the HVA field flag is set.
    bool isHva() const { return FlagVals & Hva; }
    /// Mark the argument as an HVA field.
    void setHva() { setFlag(Hva); }

    /// Return true if the argument starts an HVA structure.
    /// \return True if the HVA start flag is set.
    bool isHvaStart() const { return FlagVals & HvaStart; }
    /// Mark the argument as the start of an HVA structure.
    void setHvaStart() { setFlag(HvaStart); }

    /// Return true if the argument is a second-argument-pass value.
    /// \return True if the second-argument-pass flag is set.
    bool isSecArgPass() const { return FlagVals & SecArgPass; }
    /// Mark the argument as a second-argument-pass value.
    void setSecArgPass() { setFlag(SecArgPass); }

    /// Return true if the argument is a nested-function static chain.
    /// \return True if the nest flag is set.
    bool isNest() const { return FlagVals & Nest; }
    /// Mark the argument as a nested-function static chain.
    void setNest() { setFlag(Nest); }

    /// Return true if the argument is always returned.
    /// \return True if the returned flag is set.
    bool isReturned() const { return FlagVals & Returned; }
    /// Mark whether the argument is always returned.
    ///
    /// \param V True to set the returned flag; false to clear it.
    void setReturned(bool V = true) { setFlag(Returned, V); }

    /// Return true if the argument is passed in consecutive registers.
    /// \return True if the consecutive-registers flag is set.
    bool isInConsecutiveRegs() const { return FlagVals & InConsecutiveRegs; }
    /// Mark whether the argument is passed in consecutive registers.
    ///
    /// \param Flag True to set the flag; false to clear it.
    void setInConsecutiveRegs(bool Flag = true) {
      setFlag(InConsecutiveRegs, Flag);
    }

    /// Return true if this is the last of consecutive-register parts.
    /// \return True if the last consecutive-registers flag is set.
    bool isInConsecutiveRegsLast() const {
      return FlagVals & InConsecutiveRegsLast;
    }
    /// Mark whether this is the last of consecutive-register parts.
    ///
    /// \param Flag True to set the flag; false to clear it.
    void setInConsecutiveRegsLast(bool Flag = true) {
      setFlag(InConsecutiveRegsLast, Flag);
    }

    /// Return true if the argument is part of a split.
    /// \return True if the split flag is set.
    bool isSplit() const { return FlagVals & Split; }
    /// Mark the argument as part of a split.
    void setSplit() { setFlag(Split); }

    /// Return true if this is the last part of a split argument.
    /// \return True if the split-end flag is set.
    bool isSplitEnd() const { return FlagVals & SplitEnd; }
    /// Mark this as the last part of a split argument.
    void setSplitEnd() { setFlag(SplitEnd); }

    /// Return true if the argument is a copy-elision candidate.
    /// \return True if the copy-elision candidate flag is set.
    bool isCopyElisionCandidate() const {
      return FlagVals & CopyElisionCandidate;
    }
    /// Mark the argument as a copy-elision candidate.
    void setCopyElisionCandidate() { setFlag(CopyElisionCandidate); }

    /// Return true if the argument is a pointer.
    /// \return True if the pointer flag is set.
    bool isPointer() const { return FlagVals & Pointer; }
    /// Mark the argument as a pointer.
    void setPointer() { setFlag(Pointer); }

    /// Return true if the argument is part of a variable argument list.
    /// \return True if the vararg flag is set.
    bool isVarArg() const { return FlagVals & VarArg; }
    /// Mark the argument as part of a variable argument list.
    void setVarArg() { setFlag(VarArg); }

    /// Return the memory alignment, or 1 if unset.
    /// \return Memory alignment of the argument, or Align(1) if unset.
    Align getNonZeroMemAlign() const {
      return decodeMaybeAlign(MemAlign).valueOrOne();
    }

    /// Set the alignment used when the argument is passed in memory.
    ///
    /// \param A Alignment to encode into the memory-align bitfield.
    void setMemAlign(Align A) {
      MemAlign = encode(A);
      assert(getNonZeroMemAlign() == A && "bitfield overflow");
    }

    /// Return the byval alignment; requires the byval flag to be set.
    /// \return Byval memory alignment of the argument.
    Align getNonZeroByValAlign() const {
      assert(isByVal());
      MaybeAlign A = decodeMaybeAlign(MemAlign);
      assert(A && "ByValAlign must be defined");
      return *A;
    }

    /// Return the original alignment, or 1 if unset.
    /// \return Original alignment of the argument, or Align(1) if unset.
    Align getNonZeroOrigAlign() const {
      return decodeMaybeAlign(OrigAlign).valueOrOne();
    }

    /// Set the original alignment of the argument.
    ///
    /// \param A Alignment to encode into the original-align bitfield.
    void setOrigAlign(Align A) {
      OrigAlign = encode(A);
      assert(getNonZeroOrigAlign() == A && "bitfield overflow");
    }

    /// Return the size in bytes of a byval argument.
    /// \return Size in bytes of the byval struct.
    unsigned getByValSize() const {
      assert(isByVal() && !isByRef());
      return ByValOrByRefSize;
    }
    /// Set the size in bytes of a byval argument.
    ///
    /// \param S Size in bytes of the byval struct.
    void setByValSize(unsigned S) {
      assert(isByVal() && !isByRef());
      ByValOrByRefSize = S;
    }

    /// Return the size in bytes of a byref argument.
    /// \return Size in bytes of the byref struct.
    unsigned getByRefSize() const {
      assert(!isByVal() && isByRef());
      return ByValOrByRefSize;
    }
    /// Set the size in bytes of a byref argument.
    ///
    /// \param S Size in bytes of the byref struct.
    void setByRefSize(unsigned S) {
      assert(!isByVal() && isByRef());
      ByValOrByRefSize = S;
    }

    /// Return the address space of a pointer argument.
    /// \return Address space identifier of the pointer argument.
    unsigned getPointerAddrSpace() const { return PointerAddrSpace; }
    /// Set the address space of a pointer argument.
    ///
    /// \param AS Address space identifier for the pointer.
    void setPointerAddrSpace(unsigned AS) { PointerAddrSpace = AS; }
};

  /// Flags and type information for one incoming argument or return value.
  ///
  /// Carries flags and type information about a single incoming (formal)
  /// argument or incoming (from the perspective of the caller) return value
  /// virtual register.
  struct InputArg {
    /// Calling-convention flags for this argument part.
    ArgFlagsTy Flags;
    /// Legalized type of this argument part.
    MVT VT = MVT::Other;
    /// Non-legalized type of the argument, or a legalized type for libcalls.
    ///
    /// Usually the non-legalized type of the argument, which is the EVT
    /// corresponding to the OrigTy IR type. However, for post-legalization
    /// libcalls, this will be a legalized type.
    EVT ArgVT;
    /// Original IR type of the argument. For aggregates, this is the type of
    /// an individual aggregate element, not the whole aggregate.
    Type *OrigTy;
    /// Whether this incoming argument value is used.
    bool Used;

    /// Index original Function's argument.
    unsigned OrigArgIndex;
    /// Sentinel value for implicit machine-level input arguments.
    static const unsigned NoArgIndex = UINT_MAX;

    /// Byte offset of this part within the original argument.
    ///
    /// Offset in bytes of current input value relative to the beginning of
    /// original argument. E.g. if argument was splitted into four 32 bit
    /// registers, we got 4 InputArgs with PartOffsets 0, 4, 8 and 12.
    unsigned PartOffset;

    /// Construct an input argument descriptor.
    ///
    /// \param Flags Calling-convention flags for this argument part.
    /// \param VT Legalized machine value type of this part.
    /// \param ArgVT Non-legalized (or libcall-legalized) type of the argument.
    /// \param OrigTy Original IR type of the argument or aggregate element.
    /// \param Used Whether this incoming value is used.
    /// \param OrigArgIndex Index of the original function argument, or
    ///        NoArgIndex for an implicit machine-level argument.
    /// \param PartOffset Byte offset of this part within the original argument.
    InputArg(ArgFlagsTy Flags, MVT VT, EVT ArgVT, Type *OrigTy, bool Used,
             unsigned OrigArgIndex, unsigned PartOffset)
        : Flags(Flags), VT(VT), ArgVT(ArgVT), OrigTy(OrigTy), Used(Used),
          OrigArgIndex(OrigArgIndex), PartOffset(PartOffset) {}

    /// Return true if this corresponds to an original IR function argument.
    /// \return True if OrigArgIndex is not NoArgIndex.
    bool isOrigArg() const {
      return OrigArgIndex != NoArgIndex;
    }

    /// Return the index of the original IR function argument.
    /// \return Index of the original IR function argument.
    unsigned getOrigArgIndex() const {
      assert(OrigArgIndex != NoArgIndex && "Implicit machine-level argument");
      return OrigArgIndex;
    }
  };

  /// Flags and value for one outgoing argument or return value.
  ///
  /// Carries flags and a value for a single outgoing (actual) argument or
  /// outgoing (from the perspective of the caller) return value virtual
  /// register.
  struct OutputArg {
    /// Calling-convention flags for this argument part.
    ArgFlagsTy Flags;
    /// Legalized type of this argument part.
    MVT VT;
    /// Non-legalized type of the argument. This is the EVT corresponding to
    /// the OrigTy IR type.
    EVT ArgVT;
    /// Original IR type of the argument. For aggregates, this is the type of
    /// an individual aggregate element, not the whole aggregate.
    Type *OrigTy;

    /// Index original Function's argument.
    unsigned OrigArgIndex;

    /// Byte offset of this part within the original argument.
    ///
    /// Offset in bytes of current output value relative to the beginning of
    /// original argument. E.g. if argument was splitted into four 32 bit
    /// registers, we got 4 OutputArgs with PartOffsets 0, 4, 8 and 12.
    unsigned PartOffset;

    /// Construct an output argument descriptor.
    ///
    /// \param Flags Calling-convention flags for this argument part.
    /// \param VT Legalized machine value type of this part.
    /// \param ArgVT Non-legalized type of the argument.
    /// \param OrigTy Original IR type of the argument or aggregate element.
    /// \param OrigArgIndex Index of the original function argument.
    /// \param PartOffset Byte offset of this part within the original argument.
    OutputArg(ArgFlagsTy Flags, MVT VT, EVT ArgVT, Type *OrigTy,
              unsigned OrigArgIndex, unsigned PartOffset)
        : Flags(Flags), VT(VT), ArgVT(ArgVT), OrigTy(OrigTy),
          OrigArgIndex(OrigArgIndex), PartOffset(PartOffset) {}
  };

} // end namespace ISD
} // end namespace llvm

#endif // LLVM_CODEGEN_TARGETCALLINGCONV_H
