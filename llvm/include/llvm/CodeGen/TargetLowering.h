//===- llvm/CodeGen/TargetLowering.h - Target Lowering Info -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file describes how to lower LLVM code to machine code.  This has two
/// main components:
///
///  1. Which ValueTypes are natively supported by the target.
///  2. Which operations are supported for supported ValueTypes.
///  3. Cost thresholds for alternative implementations of certain operations.
///
/// In addition it has a few other components, like information about FP
/// immediates.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_TARGETLOWERING_H
#define LLVM_CODEGEN_TARGETLOWERING_H

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/DAGCombine.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/LibcallLoweringInfo.h"
#include "llvm/CodeGen/LowLevelTypeUtils.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RuntimeLibcallUtil.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGen/TargetCallingConv.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/RuntimeLibcalls.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/KnownFPClass.h"
#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace llvm {

class AssumptionCache;
class CCState;
class CCValAssign;
enum class ComplexDeinterleavingOperation;
enum class ComplexDeinterleavingRotation;
class Constant;
enum class ExceptionHandling : int;
class FastISel;
class FunctionLoweringInfo;
class GlobalValue;
class Loop;
class GISelValueTracking;
class IntrinsicInst;
class IRBuilderBase;
struct KnownBits;
class LLVMContext;
class MachineBasicBlock;
class MachineFunction;
class MachineInstr;
class MachineJumpTableInfo;
class MachineLoop;
class MachineRegisterInfo;
class MCContext;
class MCExpr;
class Module;
class ProfileSummaryInfo;
class TargetLibraryInfo;
class TargetMachine;
class MCRegisterClass;
using TargetRegisterClass = MCRegisterClass;
class TargetRegisterInfo;
class TargetTransformInfo;
class Value;
class VPIntrinsic;

/// Groups target instruction-scheduling preference definitions.
namespace Sched {

/// The kind of instruction-scheduling heuristic the target prefers.
enum Preference : uint8_t {
  /// No preference.
  None,
  /// Follow source order.
  Source,
  /// Schedule for lowest register pressure.
  RegPressure,
  /// Schedule for both latency and register pressure.
  Hybrid,
  /// Schedule for ILP in low register pressure mode.
  ILP,
  /// Schedule for VLIW targets.
  VLIW,
  /// Fast suboptimal list scheduling.
  Fast,
  /// Linearize the DAG without scheduling.
  Linearize,
  /// Marker for the last Sched::Preference.
  Last = Linearize
};

} // end namespace Sched

/// Describes a memset, memcpy, or memmove lowering candidate.
struct MemOp {
private:
  enum class MemOpKind {
    Memset,
    MemsetWithZero, // memset the memory with zeros
    Memcpy, // copy memory from source to destination, source and destination do
            // not overlap
    MemcpyStrSrc, // memcpy source is an in-register constant, so it does not
                  // need to be loaded
    Memmove, // memmove: like memcpy, but source and destination regions may
             // overlap
  };

  // Shared
  uint64_t Size;
  bool DstAlignCanChange; // true if destination alignment can satisfy any
                          // constraint.
  Align DstAlign;         // Specified alignment of the memory operation.

  bool IsVolatile;
  MemOpKind Kind;
  Align SrcAlign; // Inferred alignment of the source or default value if the
                  // memory operation does not need to load the value.
public:
  /// Construct a MemOp describing a memcpy operation.
  ///
  /// @return A MemOp describing the memory operation.
  ///
  /// \param Size Maximum jump-table size, or zero for unlimited.
  /// \param DstAlignCanChange Whether the destination alignment may be increased.
  /// \param DstAlign Alignment of the destination memory.
  /// \param SrcAlign Alignment of the source memory.
  /// \param IsVolatile Whether the memory operation is volatile.
  /// \param MemcpyStrSrc Whether the memcpy source is a string constant.
  static MemOp Copy(uint64_t Size, bool DstAlignCanChange, Align DstAlign,
                    Align SrcAlign, bool IsVolatile,
                    bool MemcpyStrSrc = false) {
    MemOp Op;
    Op.Size = Size;
    Op.DstAlignCanChange = DstAlignCanChange;
    Op.DstAlign = DstAlign;
    Op.IsVolatile = IsVolatile;
    Op.Kind = MemcpyStrSrc ? MemOpKind::MemcpyStrSrc : MemOpKind::Memcpy;
    Op.SrcAlign = SrcAlign;
    return Op;
  }

  /// Construct a MemOp describing a memmove operation.
  ///
  /// @return A MemOp describing the memory operation.
  ///
  /// \param Size Maximum jump-table size, or zero for unlimited.
  /// \param DstAlignCanChange Whether the destination alignment may be increased.
  /// \param DstAlign Alignment of the destination memory.
  /// \param SrcAlign Alignment of the source memory.
  /// \param IsVolatile Whether the memory operation is volatile.
  static MemOp Move(uint64_t Size, bool DstAlignCanChange, Align DstAlign,
                    Align SrcAlign, bool IsVolatile) {
    MemOp Op;
    Op.Size = Size;
    Op.DstAlignCanChange = DstAlignCanChange;
    Op.DstAlign = DstAlign;
    Op.IsVolatile = IsVolatile;
    Op.Kind = MemOpKind::Memmove;
    Op.SrcAlign = SrcAlign;
    return Op;
  }
  /// Build a MemOp describing a memset.
  ///
  /// @return A MemOp describing the memory operation.
  ///
  /// \param Size Number of bytes to set.
  /// \param DstAlignCanChange Whether the destination alignment may be increased.
  /// \param DstAlign Alignment of the destination memory.
  /// \param IsZeroMemset Whether the memset stores zeros.
  /// \param IsVolatile Whether the memory operation is volatile.
  static MemOp Set(uint64_t Size, bool DstAlignCanChange, Align DstAlign,
                   bool IsZeroMemset, bool IsVolatile) {
    MemOp Op;
    Op.Size = Size;
    Op.DstAlignCanChange = DstAlignCanChange;
    Op.DstAlign = DstAlign;
    Op.IsVolatile = IsVolatile;
    Op.Kind = IsZeroMemset ? MemOpKind::MemsetWithZero : MemOpKind::Memset;
    return Op;
  }

  /// The size, in bytes, of the memory location accessed.
  ///
  /// @return The size in bytes of the memory location accessed.
  uint64_t size() const { return Size; }
  /// Return the required destination alignment for this memory operation.
  ///
  /// @return The required destination alignment.
  Align getDstAlign() const {
    assert(!DstAlignCanChange);
    return DstAlign;
  }
  /// Return true if the destination alignment of this memory operation cannot
  /// change.
  ///
  /// @return True if the destination alignment of this memory operation cannot change.
  bool isFixedDstAlign() const { return !DstAlignCanChange; }
  /// Return true if this memory operation is volatile.
  ///
  /// @return True if this memory operation is volatile.
  bool isVolatile() const { return IsVolatile; }
  /// Return true if this memory operation is a memset.
  ///
  /// @return True if this memory operation is a memset.
  bool isMemset() const {
    return Kind == MemOpKind::Memset || Kind == MemOpKind::MemsetWithZero;
  }
  /// Return true if this memory operation is a memcpy.
  ///
  /// @return True if this memory operation is a memcpy.
  bool isMemcpy() const {
    return Kind == MemOpKind::Memcpy || Kind == MemOpKind::MemcpyStrSrc;
  }
  /// Return true if this memory operation is a memmove.
  ///
  /// @return True if this memory operation is a memmove.
  bool isMemmove() const { return Kind == MemOpKind::Memmove; }
  /// Return true if this memory operation is a memcpy or memmove.
  ///
  /// @return True if this memory operation is a memcpy or memmove.
  bool isMemcpyOrMemmove() const { return isMemcpy() || isMemmove(); }
  /// Return true if this is a memcpy or memmove whose destination alignment
  /// cannot change.
  ///
  /// @return True if this is a memcpy or memmove whose destination alignment cannot change.
  bool isMemcpyOrMemmoveWithFixedDstAlign() const {
    /// Return true if this is a memcpy or memmove.
    return isMemcpyOrMemmove() && !DstAlignCanChange;
  }
  /// Return true if this memset operation stores all zero bytes.
  ///
  /// @return True if this memset operation stores all zero bytes.
  bool isZeroMemset() const { return Kind == MemOpKind::MemsetWithZero; }
  /// Return true if this memcpy's source is an in-register constant string.
  ///
  /// @return True if this memcpy's source is an in-register constant string.
  bool isMemcpyStrSrc() const { return Kind == MemOpKind::MemcpyStrSrc; }
  /// Return the inferred alignment of the source for this memcpy or memmove
  /// operation.
  ///
  /// @return The inferred source alignment.
  Align getSrcAlign() const {
    assert(isMemcpyOrMemmove() && "Must be a memcpy or memmove");
    return SrcAlign;
  }
  /// Return true if the source of this memory operation is aligned to the given
  /// boundary.
  ///
  /// @return True if the source of this memory operation is aligned to the given boundary.
  ///
  /// \param AlignCheck Optional callback that reports whether an alignment is acceptable.
  bool isSrcAligned(Align AlignCheck) const {
    /// Return true if this is a memset.
    return isMemset() || llvm::isAligned(AlignCheck, SrcAlign.value());
  }
  /// Return true if the destination of this memory operation is aligned to the
  /// given boundary.
  ///
  /// @return True if the destination of this memory operation is aligned to the given boundary.
  ///
  /// \param AlignCheck Optional callback that reports whether an alignment is acceptable.
  bool isDstAligned(Align AlignCheck) const {
    return DstAlignCanChange || llvm::isAligned(AlignCheck, DstAlign.value());
  }
  /// Return true if both the source and destination of this memory operation are
  /// aligned.
  ///
  /// @return True if both the source and destination of this memory operation are aligned.
  ///
  /// \param AlignCheck Optional callback that reports whether an alignment is acceptable.
  bool isAligned(Align AlignCheck) const {
    /// Return true if the source meets AlignCheck.
    return isSrcAligned(AlignCheck) && isDstAligned(AlignCheck);
  }
};

/// This base class for TargetLowering contains the SelectionDAG-independent
/// parts that can be used from the rest of CodeGen.
class LLVM_ABI TargetLoweringBase {
public:
  /// This enum indicates whether operations are valid for a target, and if not,
  /// what action should be used to make them valid.
  enum LegalizeAction : uint8_t {
    /// The target natively supports this operation.
    Legal,
    /// Execute this operation in a larger type.
    Promote,
    /// Expand to other ops, otherwise use a libcall.
    Expand,
    /// Always lower via a libcall without trying other expansions.
    LibCall,
    /// Use the LowerOperation hook for custom lowering.
    Custom
  };

  /// This enum indicates whether a types are legal for a target, and if not,
  /// what action should be used to make them valid.
  enum LegalizeTypeAction : uint8_t {
    /// The target natively supports this type.
    TypeLegal,
    /// Replace this integer with a larger one.
    TypePromoteInteger,
    /// Split this integer into two of half the size.
    TypeExpandInteger,
    /// Convert this float to a same-size integer type.
    TypeSoftenFloat,
    /// Split this float into two of half the size.
    TypeExpandFloat,
    /// Replace this one-element vector with its element.
    TypeScalarizeVector,
    /// Split this vector into two of half the size.
    TypeSplitVector,
    /// Widen this vector into a larger vector.
    TypeWidenVector,
    /// Soften half to i16 and use float to do arithmetic.
    TypeSoftPromoteHalf,
    /// Left unimplemented; prefer widening or promoting scalable vectors.
    TypeScalarizeScalableVector,
  };

  /// LegalizeKind holds the legalization kind that needs to happen to EVT
  /// in order to type-legalize it.
  using LegalizeKind = std::pair<LegalizeTypeAction, EVT>;

  /// Enum that describes how the target represents true/false values.
  enum BooleanContent {
    /// Only bit 0 counts; the rest can hold garbage.
    UndefinedBooleanContent,
    /// All bits zero except for bit 0.
    ZeroOrOneBooleanContent,
    /// All bits equal to bit 0.
    ZeroOrNegativeOneBooleanContent
  };

  /// Enum that describes what type of support for selects the target has.
  enum SelectSupportKind {
    /// The target supports scalar selects (e.g. cmov).
    ScalarValSelect,
    /// The target supports selects with a scalar condition and vector values.
    ScalarCondVectorVal,
    /// The target supports vector selects with a vector mask.
    VectorMaskSelect
  };

  /// Specifies how an atomic instruction should be expanded, if at all.
  ///
  /// How an atomic load or AtomicRMW should be expanded, if at all.
  /// Exists because different targets have different levels of support for
  /// these atomic instructions, and also have different options w.r.t. what
  /// they should expand to.
  enum class AtomicExpansionKind {
    /// Don't expand the instruction.
    None,
    /// Cast the atomic instruction to another type (e.g. FP to integer).
    CastToInteger,
    /// Expand into load-linked / store-conditional.
    LLSC,
    /// Expand a load into just a load-linked.
    LLOnly,
    /// Expand the instruction into cmpxchg.
    CmpXChg,
    /// Use a target-specific intrinsic for the LL/SC loop.
    MaskedIntrinsic,
    /// Use a target-specific intrinsic for special bit operations.
    BitTestIntrinsic,
    /// Use a target-specific intrinsic for special compare operations.
    CmpArithIntrinsic,
    /// Generic expansion in terms of other atomic operations.
    Expand,
    /// Custom target-specific expansion using TLI hooks.
    CustomExpand,
    /// Rewrite to a non-atomic form in a known non-preemptible environment.
    NotAtomic
  };

  /// Enum that specifies when a multiplication should be expanded.
  enum class MulExpansionKind {
    /// Always expand the instruction.
    Always,
    /// Only expand when the resulting instructions are legal or custom.
    OnlyLegalOrCustom,
  };

  /// Enum that specifies when a float negation is beneficial.
  enum class NegatibleCost {
    /// Negated expression is cheaper.
    Cheaper = 0,
    /// Negated expression has the same cost.
    Neutral = 1,
    /// Negated expression is more expensive.
    Expensive = 2
  };

  /// Enum that specifies how expensive lowering an EXTRACT_SUBVECTOR is.
  enum class ExtractSubvectorCost {
    /// Lowers to no instruction at all, e.g. a subregister copy.
    Free = 0,
    /// Lowers to at most one instruction, possibly foldable.
    Cheap = 1,
    /// Needs a shuffle sequence that cannot be folded away.
    Expensive = 2
  };

  /// Enum of different potentially desirable ways to fold (and/or (setcc ...),
  /// (setcc ...)).
  enum AndOrSETCCFoldKind : uint8_t {
    /// No fold is preferable.
    None = 0,
    /// Prefer folding with Add and And.
    AddAnd = 1,
    /// Prefer folding with Not and And.
    NotAnd = 2,
    /// Prefer folding with llvm.abs.
    ABS = 4,
  };

  /// Describes a single call argument, including its value, type, and ABI
  /// attribute flags.
  class ArgListEntry {
  public:
    /// The IR value of the argument.
    Value *Val;
    /// The SelectionDAG value for this argument or node reference.
    SDValue Node;
    /// Original unlegalized argument type.
    Type *OrigTy;
    /// Same as OrigTy, or partially legalized for soft float libcalls.
    Type *Ty;
    /// Whether the value should be sign-extended.
    bool IsSExt : 1;
    /// Whether the value should be zero-extended.
    bool IsZExt : 1;
    /// Whether the argument should not be extended.
    bool IsNoExt : 1;
    /// Whether the value is passed in a register per the inreg attribute.
    bool IsInReg : 1;
    /// Whether the argument is a hidden struct-return pointer.
    bool IsSRet : 1;
    /// Whether the argument is a nested-function context pointer.
    bool IsNest : 1;
    /// Whether the argument is passed by value.
    bool IsByVal : 1;
    /// Whether the argument is passed by reference.
    bool IsByRef : 1;
    /// Whether the argument is passed via inalloca.
    bool IsInAlloca : 1;
    /// Whether the call or argument uses preallocated storage.
    bool IsPreallocated : 1;
    /// Whether the argument value is also returned by the callee.
    bool IsReturned : 1;
    /// Whether the argument is the Swift self parameter.
    bool IsSwiftSelf : 1;
    /// Whether the argument is the Swift async context parameter.
    bool IsSwiftAsync : 1;
    /// Whether the argument is the Swift error parameter.
    bool IsSwiftError : 1;
    /// Whether the argument is a Control Flow Guard target.
    bool IsCFGuardTarget : 1;
    /// The required or known alignment of the memory access.
    MaybeAlign Alignment = std::nullopt;
    /// Pointee type when the argument is passed indirectly.
    Type *IndirectType = nullptr;

    /// Construct an argument entry from an IR value, DAG node, and type.
    ///
    /// \param Val IR value of the argument.
    /// \param Node SelectionDAG value for the argument.
    /// \param Ty Type of the argument.
    ArgListEntry(Value *Val, SDValue Node, Type *Ty)
        : Val(Val), Node(Node), OrigTy(Ty), Ty(Ty), IsSExt(false),
          IsZExt(false), IsNoExt(false), IsInReg(false), IsSRet(false),
          IsNest(false), IsByVal(false), IsByRef(false), IsInAlloca(false),
          IsPreallocated(false), IsReturned(false), IsSwiftSelf(false),
          IsSwiftAsync(false), IsSwiftError(false), IsCFGuardTarget(false) {}

    /// Construct an argument entry from an IR value.
    ///
    /// \param Val IR value of the argument.
    /// \param Node Optional SelectionDAG value for the argument.
    explicit ArgListEntry(Value *Val, SDValue Node = SDValue())
        : ArgListEntry(Val, Node, Val->getType()) {}

    /// Construct an argument entry from a DAG node and type.
    ///
    /// \param Node SelectionDAG value for the argument.
    /// \param Ty Type of the argument.
    ArgListEntry(SDValue Node, Type *Ty) : ArgListEntry(nullptr, Node, Ty) {}

    /// Set the argument flags from the given call's attributes.
    ///
    /// \param Call Libcall identifier or call instruction.
    /// \param ArgIdx Index of the inline-asm operand being classified.
    LLVM_ABI void setAttributes(const CallBase *Call, unsigned ArgIdx);
  };
  /// A list of call arguments.
  using ArgListTy = std::vector<ArgListEntry>;

  /// Return the extension kind implied by the given boolean-content
  /// representation.
  ///
  /// @return The extension kind implied by the given boolean-content representation.
  ///
  /// \param Content Boolean contents encoding to extend from.
  static ISD::NodeType getExtendForContent(BooleanContent Content) {
    switch (Content) {
    case UndefinedBooleanContent:
      // Extend by adding rubbish bits.
      return ISD::ANY_EXTEND;
    case ZeroOrOneBooleanContent:
      // Extend by adding zero bits.
      return ISD::ZERO_EXTEND;
    case ZeroOrNegativeOneBooleanContent:
      // Extend by copying the sign bit.
      return ISD::SIGN_EXTEND;
    }
    llvm_unreachable("Invalid content kind");
  }

  /// Holds the SelectionDAG-independent parts of target lowering shared across
  /// CodeGen.
  ///
  /// \param TM Target machine.
  /// \param STI Target subtarget info.
  explicit TargetLoweringBase(const TargetMachine &TM,
                              const TargetSubtargetInfo &STI);
  /// Deleted copy constructor.
  ///
  /// \param RHS Source object (deleted).
  TargetLoweringBase(const TargetLoweringBase &RHS) = delete;
  /// Deleted copy-assignment operator.
  ///
  /// \param RHS Source object (deleted).
  TargetLoweringBase &operator=(const TargetLoweringBase &RHS) = delete;
  /// Virtual destructor.
  virtual ~TargetLoweringBase();

  /// Return true if the target support strict float operation
  ///
  /// @return True if the target support strict float operation.
  bool isStrictFPEnabled() const {
    return IsStrictFPEnabled;
  }

protected:
  /// Initialize all of the actions to default values.
  void initActions();

public:
  /// Return the target machine associated with this lowering.
  ///
  /// @return The target machine associated with this lowering.
  const TargetMachine &getTargetMachine() const { return TM; }

  /// Return true if the target should use software floating-point emulation.
  ///
  /// @return True if the target should use software floating-point emulation.
  virtual bool useSoftFloat() const { return false; }

  /// Return the pointer type for the given address space.
  ///
  /// Return the pointer type for the given address space, defaults to the
  /// pointer type from the data layout. FIXME: The default needs to be removed
  /// once all the code is updated.
  ///
  /// @return The pointer type for the given address space.
  ///
  /// \param DL Debug location or data layout, depending on context.
  /// \param AS Address space.
  virtual MVT getPointerTy(const DataLayout &DL, uint32_t AS = 0) const {
    return MVT::getIntegerVT(DL.getPointerSizeInBits(AS));
  }

  /// Return the in-memory pointer type for the given address space.
  ///
  /// Return the in-memory pointer type for the given address space, defaults to
  /// the pointer type from the data layout. FIXME: The default needs to be
  /// removed once all the code is updated.
  ///
  /// @return The in-memory pointer type for the given address space.
  ///
  /// \param DL Debug location or data layout, depending on context.
  /// \param AS Address space.
  virtual MVT getPointerMemTy(const DataLayout &DL, uint32_t AS = 0) const {
    return MVT::getIntegerVT(DL.getPointerSizeInBits(AS));
  }

  /// Return the type for frame index, which is determined by
  /// the alloca address space specified through the data layout.
  ///
  /// @return The type for frame index, which is determined by the alloca address space specified through the data layout.
  ///
  /// \param DL Debug location or data layout, depending on context.
  MVT getFrameIndexTy(const DataLayout &DL) const {
    return getPointerTy(DL, DL.getAllocaAddrSpace());
  }

  /// Return the type for code pointers, which is determined by the program
  /// address space specified through the data layout.
  ///
  /// @return The type for code pointers, which is determined by the program address space specified through the data layout.
  ///
  /// \param DL Debug location or data layout, depending on context.
  MVT getProgramPointerTy(const DataLayout &DL) const {
    return getPointerTy(DL, DL.getProgramAddressSpace());
  }

  /// Return the type for operands of fence.
  /// TODO: Let fence operands be of i32 type and remove this.
  ///
  /// @return The type for operands of fence. TODO: Let fence operands be of i32 type and remove this.
  ///
  /// \param DL Debug location or data layout, depending on context.
  virtual MVT getFenceOperandTy(const DataLayout &DL) const {
    return getPointerTy(DL);
  }

  /// Return the type to use for the amount operand of a scalar shift operation.
  ///
  /// Return the type to use for a scalar shift opcode, given the shifted amount
  /// type. Targets should return a legal type if the input type is legal.
  /// Targets can return a type that is too small if the input type is illegal.
  ///
  /// @return The type to use for the amount operand of a scalar shift operation.
  ///
  /// \param DL Debug location or data layout, depending on context.
  /// \param VT Value type being queried or transformed.
  virtual MVT getScalarShiftAmountTy(const DataLayout &DL, EVT VT) const;

  /// Return the type to use for the shift amount operand of a shift opcode.
  ///
  /// Returns the type for the shift amount of a shift opcode. For vectors,
  /// returns the input type. For scalars, calls getScalarShiftAmountTy. If
  /// getScalarShiftAmountTy type cannot represent all possible shift amounts,
  /// returns MVT::i32.
  ///
  /// @return The type to use for the shift amount operand of a shift opcode.
  ///
  /// \param LHSTy Type of the shifted value.
  /// \param DL Debug location or data layout, depending on context.
  EVT getShiftAmountTy(EVT LHSTy, const DataLayout &DL) const;

  /// Return the preferred type to use for a shift opcode, given the shifted
  /// amount type is \p ShiftValueTy.
  ///
  /// @return The preferred type to use for a shift opcode, given the shifted amount type is \p ShiftValueTy.
  ///
  /// \param ShiftValueTy Type of the shift amount value.
  LLVM_READONLY
  virtual LLT getPreferredShiftAmountTy(LLT ShiftValueTy) const {
    return ShiftValueTy;
  }

  /// Returns the type to be used for the index operand vector operations. By
  /// default we assume it will have the same size as an address space 0
  /// pointer.
  ///
  /// @return The type to be used for the index operand vector operations. By default we assume it will have the same size as an address space 0 pointer.
  ///
  /// \param DL Debug location or data layout, depending on context.
  virtual unsigned getVectorIdxWidth(const DataLayout &DL) const {
    return DL.getPointerSizeInBits(0);
  }

  /// Returns the type to be used for the index operand of:
  /// ISD::INSERT_VECTOR_ELT, ISD::EXTRACT_VECTOR_ELT,
  /// ISD::INSERT_SUBVECTOR, and ISD::EXTRACT_SUBVECTOR
  ///
  /// @return The type to be used for the index operand of: ISD::INSERT_VECTOR_ELT, ISD::EXTRACT_VECTOR_ELT, ISD::INSERT_SUBVECTOR, and ISD::EXTRACT_SUBVECTOR.
  ///
  /// \param DL Debug location or data layout, depending on context.
  MVT getVectorIdxTy(const DataLayout &DL) const {
    return MVT::getIntegerVT(getVectorIdxWidth(DL));
  }

  /// Returns the type to be used for the index operand of:
  /// G_INSERT_VECTOR_ELT, G_EXTRACT_VECTOR_ELT,
  /// G_INSERT_SUBVECTOR, and G_EXTRACT_SUBVECTOR
  ///
  /// @return The type to be used for the index operand of: G_INSERT_VECTOR_ELT, G_EXTRACT_VECTOR_ELT, G_INSERT_SUBVECTOR, and G_EXTRACT_SUBVECTOR.
  ///
  /// \param DL Debug location or data layout, depending on context.
  LLT getVectorIdxLLT(const DataLayout &DL) const {
    return LLT::integer(getVectorIdxWidth(DL));
  }

  /// Return the type to use for the explicit vector length operand of VP nodes.
  ///
  /// Returns the type to be used for the EVL/AVL operand of VP nodes:
  /// ISD::VP_UDIV, ISD::VP_SDIV, etc. It must be a legal scalar integer type,
  /// and must be at least as large as i32. The EVL is implicitly zero-extended
  /// to any larger type.
  ///
  /// @return The type to use for the explicit vector length operand of VP nodes.
  virtual MVT getVPExplicitVectorLengthTy() const { return MVT::i32; }

  /// This callback is used to inspect load/store instructions and add
  /// target-specific MachineMemOperand flags to them.  The default
  /// implementation does nothing.
  ///
  /// @return This callback is used to inspect load/store instructions and add target-specific MachineMemOperand flags to them.  The default implementation does nothing.
  ///
  /// \param I Instruction being queried.
  virtual MachineMemOperand::Flags getTargetMMOFlags(const Instruction &I) const {
    return MachineMemOperand::MONone;
  }

  /// This callback is used to inspect load/store SDNode.
  /// The default implementation does nothing.
  ///
  /// @return This callback is used to inspect load/store SDNode. The default implementation does nothing.
  ///
  /// \param Node SDNode being expanded or analyzed.
  virtual MachineMemOperand::Flags
  getTargetMMOFlags(const MemSDNode &Node) const {
    return MachineMemOperand::MONone;
  }

  /// Return the MachineMemOperand flags to use for the given load instruction.
  ///
  /// @return The MachineMemOperand flags to use for the given load instruction.
  ///
  /// \param LI Load instruction.
  /// \param DL Debug location or data layout, depending on context.
  /// \param AC Assumption cache used for profitability checks.
  /// \param LibInfo Target library info.
  /// \param OptLevel Optimization level guiding the decision.
  MachineMemOperand::Flags getLoadMemOperandFlags(
      const LoadInst &LI, const DataLayout &DL, AssumptionCache *AC = nullptr,
      const TargetLibraryInfo *LibInfo = nullptr,
      CodeGenOptLevel OptLevel = CodeGenOptLevel::Default) const;
  /// Return the MachineMemOperand flags to use for the given store instruction.
  ///
  /// @return The MachineMemOperand flags to use for the given store instruction.
  ///
  /// \param SI Switch or store instruction.
  /// \param DL Debug location or data layout, depending on context.
  MachineMemOperand::Flags getStoreMemOperandFlags(const StoreInst &SI,
                                                   const DataLayout &DL) const;
  /// Return the MachineMemOperand flags to use for the given atomic instruction.
  ///
  /// @return The MachineMemOperand flags to use for the given atomic instruction.
  ///
  /// \param AI Atomic RMW instruction.
  /// \param DL Debug location or data layout, depending on context.
  MachineMemOperand::Flags getAtomicMemOperandFlags(const Instruction &AI,
                                                    const DataLayout &DL) const;
  /// Return the MachineMemOperand flags to use for the given vector-predicated
  /// intrinsic.
  ///
  /// @return The MachineMemOperand flags to use for the given vector-predicated intrinsic.
  ///
  /// \param VPIntrin Vector-predicated intrinsic being queried.
  MachineMemOperand::Flags
  getVPIntrinsicMemOperandFlags(const VPIntrinsic &VPIntrin) const;

  /// Return true if the target supports the given kind of select operation.
  ///
  /// @return True if the target supports the given kind of select operation.
  ///
  /// \param Kind Kind of select support being queried.
  virtual bool isSelectSupported(SelectSupportKind Kind) const {
    return true;
  }

  /// Return true if the @llvm.get.active.lane.mask intrinsic should be expanded
  /// using generic code in SelectionDAGBuilder.
  ///
  /// @return True if the @llvm.get.active.lane.mask intrinsic should be expanded using generic code in SelectionDAGBuilder.
  ///
  /// \param VT Value type being queried or transformed.
  /// \param OpVT Operand value type.
  virtual bool shouldExpandGetActiveLaneMask(EVT VT, EVT OpVT) const {
    return true;
  }

  /// Return true if the get.vector.length intrinsic should be expanded using
  /// generic code.
  ///
  /// @return True if the get.vector.length intrinsic should be expanded using generic code.
  ///
  /// \param CountVT Value type of the element or lane count.
  /// \param VF Vectorization factor.
  /// \param IsScalable Whether the vector type is scalable.
  virtual bool shouldExpandGetVectorLength(EVT CountVT, unsigned VF,
                                           bool IsScalable) const {
    return true;
  }

  /// Return the minimum number of bits required to hold the maximum possible
  /// number of trailing zero vector elements.
  ///
  /// @return The minimum number of bits required to hold the maximum possible number of trailing zero vector elements.
  ///
  /// \param RetVT Return value type.
  /// \param EC Element count of the vector.
  /// \param ZeroIsPoison Whether a zero count is poison.
  /// \param VScaleRange Optional vscale range.
  unsigned getBitWidthForCttzElements(EVT RetVT, ElementCount EC,
                                      bool ZeroIsPoison,
                                      const ConstantRange *VScaleRange) const;

  /// Return true if two vector reductions combined by an operation should be
  /// reassociated into one reduction.
  ///
  /// @return True if two vector reductions combined by an operation should be reassociated into one reduction.
  ///
  /// \param RedOpc Reduction opcode being reassociated.
  /// \param VT Value type being queried or transformed.
  virtual bool shouldReassociateReduction(unsigned RedOpc, EVT VT) const {
    return true;
  }

  /// Return true if converting a select of FP constants to a conditional
  /// constant-pool load is profitable.
  ///
  /// Return true if it is profitable to convert a select of FP constants into a
  /// constant pool load whose address depends on the select condition. The
  /// parameter may be used to differentiate a select with FP compare from
  /// integer compare.
  ///
  /// @return True if converting a select of FP constants to a conditional constant-pool load is profitable.
  ///
  /// \param CmpOpVT Compare operand VT for the select-of-constants.
  virtual bool reduceSelectOfFPConstantLoads(EVT CmpOpVT) const {
    return true;
  }

  /// Return true if the target has multiple condition registers that can hold
  /// comparison results.
  ///
  /// Does the target have multiple (allocatable) condition registers that can be
  /// used to store the results of comparisons for use by selects and conditional
  /// branches. With multiple condition registers, the code generator will not
  /// aggressively sink comparisons into the blocks of their users. \p VT is the
  /// type of the condition value, e.g. the type of the result of a comparison.
  ///
  /// @return True if the target has multiple condition registers that can hold comparison results.
  ///
  /// \param VT Value type being queried or transformed.
  virtual bool hasMultipleConditionRegisters(EVT VT) const { return false; }

  /// Return true if the target has BitExtract instructions.
  ///
  /// @return True if the target has BitExtract instructions.
  bool hasExtractBitsInsn() const { return HasExtractBitsInsn; }

  /// Return the preferred vector type legalization action.
  ///
  /// @return The preferred vector type legalization action.
  ///
  /// \param VT Value type being queried or transformed.
  virtual TargetLoweringBase::LegalizeTypeAction
  getPreferredVectorAction(MVT VT) const {
    // The default action for one element vectors is to scalarize
    if (VT.getVectorElementCount().isScalar())
      return TypeScalarizeVector;
    // The default action for an odd-width vector is to widen.
    if (!VT.isPow2VectorType())
      return TypeWidenVector;
    // The default action for other vectors is to promote
    return TypePromoteInteger;
  }

  /// Return true if soft-promoted half values should be passed and returned as
  /// f32 in FP registers.
  ///
  /// @return True if soft-promoted half values should be passed and returned as f32 in FP registers.
  virtual bool useFPRegsForHalfType() const { return false; }

  /// Return true if BUILD_VECTOR should be expanded using shuffles rather than a
  /// stack store/load.
  ///
  /// @return True if BUILD_VECTOR should be expanded using shuffles rather than a stack store/load.
  ///
  /// \param VT Extended value type being queried.
  /// \param DefinedValues Number of defined values produced by the node.
  virtual bool
  shouldExpandBuildVectorWithShuffles(EVT VT,
                                      unsigned DefinedValues) const {
    return DefinedValues < 3;
  }

  /// Return true if integer division is cheaper than an equivalent sequence of
  /// shifts, adds, and multiplies.
  ///
  /// Return true if integer divide is usually cheaper than a sequence of several
  /// shifts, adds, and multiplies for this target. The definition of "cheaper"
  /// may depend on whether we're optimizing for speed or for size.
  ///
  /// @return True if integer division is cheaper than an equivalent sequence of shifts, adds, and multiplies.
  ///
  /// \param VT Value type being queried or transformed.
  /// \param Attr Function attribute list affecting cost.
  virtual bool isIntDivCheap(EVT VT, AttributeList Attr) const { return false; }

  /// Return true if the target can handle a standalone remainder operation.
  ///
  /// @return True if the target can handle a standalone remainder operation.
  ///
  /// \param VT Value type being queried or transformed.
  virtual bool hasStandaloneRem(EVT VT) const {
    return true;
  }

  /// Return true if SQRT(X) shouldn't be replaced with X*RSQRT(X).
  ///
  /// @return True if SQRT(X) shouldn't be replaced with X*RSQRT(X).
  ///
  /// \param X Non-identity operand of the select fold.
  /// \param DAG SelectionDAG providing context.
  virtual bool isFsqrtCheap(SDValue X, SelectionDAG &DAG) const {
    // Default behavior is to replace SQRT(X) with X*RSQRT(X).
    return false;
  }

  /// Reciprocal estimate status values used by the functions below.
  enum ReciprocalEstimate : int {
    /// Status was not overridden by function attributes.
    Unspecified = -1,
    /// Reciprocal estimates are disabled.
    Disabled = 0,
    /// Reciprocal estimates are enabled.
    Enabled = 1
  };

  /// Return whether reciprocal square-root estimation is enabled for the given
  /// type.
  ///
  /// Return a ReciprocalEstimate enum value for a square root of the given type
  /// based on the function's attributes. If the operation is not overridden by
  /// the function's attributes, "Unspecified" is returned and target defaults
  /// are expected to be used for instruction selection.
  ///
  /// @return Whether reciprocal square-root estimation is enabled for the given type.
  ///
  /// \param VT Value type being queried or transformed.
  /// \param MF Machine function being lowered.
  int getRecipEstimateSqrtEnabled(EVT VT, MachineFunction &MF) const;

  /// Return whether reciprocal division estimation is enabled for the given
  /// type.
  ///
  /// Return a ReciprocalEstimate enum value for a division of the given type
  /// based on the function's attributes. If the operation is not overridden by
  /// the function's attributes, "Unspecified" is returned and target defaults
  /// are expected to be used for instruction selection.
  ///
  /// @return Whether reciprocal division estimation is enabled for the given type.
  ///
  /// \param VT Value type being queried or transformed.
  /// \param MF Machine function being lowered.
  int getRecipEstimateDivEnabled(EVT VT, MachineFunction &MF) const;

  /// Return the number of Newton-Raphson refinement steps for a square-root
  /// estimate.
  ///
  /// Return the refinement step count for a square root of the given type based
  /// on the function's attributes. If the operation is not overridden by the
  /// function's attributes, "Unspecified" is returned and target defaults are
  /// expected to be used for instruction selection.
  ///
  /// @return The number of Newton-Raphson refinement steps for a square-root estimate.
  ///
  /// \param VT Value type being queried or transformed.
  /// \param MF Machine function being lowered.
  int getSqrtRefinementSteps(EVT VT, MachineFunction &MF) const;

  /// Return the number of Newton-Raphson refinement steps for a reciprocal
  /// division estimate.
  ///
  /// Return the refinement step count for a division of the given type based on
  /// the function's attributes. If the operation is not overridden by the
  /// function's attributes, "Unspecified" is returned and target defaults are
  /// expected to be used for instruction selection.
  ///
  /// @return The number of Newton-Raphson refinement steps for a reciprocal division estimate.
  ///
  /// \param VT Value type being queried or transformed.
  /// \param MF Machine function being lowered.
  int getDivRefinementSteps(EVT VT, MachineFunction &MF) const;

  /// Returns true if target has indicated at least one type should be bypassed.
  ///
  /// @return True if target has indicated at least one type should be bypassed.
  bool isSlowDivBypassed() const { return !BypassSlowDivWidths.empty(); }

  /// Returns map of slow types for division or remainder with corresponding
  /// fast types
  ///
  /// @return Map of slow types for division or remainder with corresponding fast types.
  const DenseMap<unsigned int, unsigned int> &getBypassSlowDivWidths() const {
    return BypassSlowDivWidths;
  }

  /// Return true if Flow Control is an expensive operation that should be
  /// avoided.
  ///
  /// @return True if Flow Control is an expensive operation that should be avoided.
  bool isJumpExpensive() const { return JumpIsExpensive; }

  /// Holds cost parameters that control whether two merged branch conditions
  /// should be split.
  struct CondMergingParams {
    /// The baseline cost threshold, in latency, for keeping two branch
    /// conditions merged.
    int BaseCost;
    /// The bias added to the base cost when both conditions are likely to be
    /// computed.
    int LikelyBias;
    /// The bias subtracted from the base cost when both conditions are unlikely
    /// to be computed.
    int UnlikelyBias;
  };
  /// Return the cost parameters used to decide whether to keep two branch
  /// conditions merged.
  ///
  /// @return The cost parameters used to decide whether to keep two branch
  /// conditions merged.
  ///
  /// \param Opc Binary opcode of the combined condition.
  /// \param LHS Left-hand side value of the condition.
  /// \param RHS Right-hand side value of the condition.
  /// \param F Function containing the branch.
  virtual CondMergingParams
  getJumpConditionMergingParams(Instruction::BinaryOps Opc,
                                const Value *LHS, const Value *RHS,
                                const Function *F) const {
    // -1 will always result in splitting.
    return {-1, -1, -1};
  }

  /// Return true if selects are only cheaper than branches if the branch is
  /// unlikely to be predicted right.
  ///
  /// @return True if selects are only cheaper than branches if the branch is unlikely to be predicted right.
  bool isPredictableSelectExpensive() const {
    return PredictableSelectIsExpensive;
  }

  /// Return true if GlobalISel should fall back to SelectionDAG ISel for the
  /// given instruction.
  ///
  /// @return True if GlobalISel should fall back to SelectionDAG ISel for the given instruction.
  ///
  /// \param Inst Atomic instruction being fenced.
  virtual bool fallBackToDAGISel(const Instruction &Inst) const {
    return false;
  }

  /// Return true if folding a bitcast into a load is beneficial for this target.
  ///
  /// Return true if the following transform is beneficial: fold (conv (load x))
  /// -> (load (conv*)x) On architectures that don't natively support some vector
  /// loads efficiently, casting the load to a smaller vector of larger types and
  /// loading is more efficient, however, this can be undone by optimizations in
  /// dag combiner.
  ///
  /// @return True if folding a bitcast into a load is beneficial for this target.
  ///
  /// \param LoadVT VT of the loads being merged.
  /// \param BitcastVT Value type after bitcast.
  /// \param DAG SelectionDAG providing context.
  /// \param MMO Machine memory operand.
  virtual bool isLoadBitCastBeneficial(EVT LoadVT, EVT BitcastVT,
                                       const SelectionDAG &DAG,
                                       const MachineMemOperand &MMO) const;

  /// Return true if the following transform is beneficial:
  /// (store (y (conv x)), y*)) -> (store x, (x*))
  ///
  /// @return True if the following transform is beneficial: (store (y (conv x)), y*)) -> (store x, (x*)).
  ///
  /// \param StoreVT VT of the merged store.
  /// \param BitcastVT Value type after bitcast.
  /// \param DAG SelectionDAG providing context.
  /// \param MMO Machine memory operand.
  virtual bool isStoreBitCastBeneficial(EVT StoreVT, EVT BitcastVT,
                                        const SelectionDAG &DAG,
                                        const MachineMemOperand &MMO) const {
    // Default to the same logic as loads.
    return isLoadBitCastBeneficial(StoreVT, BitcastVT, DAG, MMO);
  }

  /// Return true if storing this vector constant directly is cheaper than
  /// storing its scalar elements.
  ///
  /// Return true if it is expected to be cheaper to do a store of vector
  /// constant with the given size and type for the address space than to store
  /// the individual scalar element constants.
  ///
  /// @return True if storing this vector constant directly is cheaper than storing its scalar elements.
  ///
  /// \param IsZero Whether the constant vector is all zeros.
  /// \param MemVT Memory value type of the access.
  /// \param NumElem Number of vector elements.
  /// \param AddrSpace Address space of the memory access.
  virtual bool storeOfVectorConstantIsCheap(bool IsZero, EVT MemVT,
                                            unsigned NumElem,
                                            unsigned AddrSpace) const {
    return IsZero;
  }

  /// Return true if stores of this type may be merged even after legalization.
  ///
  /// Allow store merging for the specified type after legalization in addition
  /// to before legalization. This may transform stores that do not exist earlier
  /// (for example, stores created from intrinsics).
  ///
  /// @return True if stores of this type may be merged even after legalization.
  ///
  /// \param MemVT Memory value type of the access.
  virtual bool mergeStoresAfterLegalization(EVT MemVT) const {
    return true;
  }

  /// Returns if it's reasonable to merge stores to MemVT size.
  ///
  /// @return If it's reasonable to merge stores to MemVT size.
  ///
  /// \param AS Address space.
  /// \param MemVT Memory value type of the access.
  /// \param MF Machine function being lowered.
  virtual bool canMergeStoresTo(unsigned AS, EVT MemVT,
                                const MachineFunction &MF) const {
    return true;
  }

  /// Return true if it is cheap to speculate a call to intrinsic cttz.
  ///
  /// @return True if it is cheap to speculate a call to intrinsic cttz.
  ///
  /// \param Ty Type of the constant or operation.
  virtual bool isCheapToSpeculateCttz(Type *Ty) const {
    return false;
  }

  /// Return true if it is cheap to speculate a call to intrinsic ctlz.
  ///
  /// @return True if it is cheap to speculate a call to intrinsic ctlz.
  ///
  /// \param Ty Type of the constant or operation.
  virtual bool isCheapToSpeculateCtlz(Type *Ty) const {
    return false;
  }

  /// Return true if ctlz instruction is fast.
  ///
  /// @return True if ctlz instruction is fast.
  virtual bool isCtlzFast() const {
    return false;
  }

  /// Return true if ctpop instruction is fast.
  ///
  /// @return True if ctpop instruction is fast.
  ///
  /// \param VT Value type being queried or transformed.
  virtual bool isCtpopFast(EVT VT) const {
    return isOperationLegal(ISD::CTPOP, VT);
  }

  /// Return the maximum number of "x & (x - 1)" operations that can be done
  /// instead of deferring to a custom CTPOP.
  ///
  /// @return The maximum number of "x & (x - 1)" operations that can be done instead of deferring to a custom CTPOP.
  ///
  /// \param VT Value type being queried or transformed.
  /// \param Cond Condition code of the compare.
  virtual unsigned getCustomCtpopCost(EVT VT, ISD::CondCode Cond) const {
    return 1;
  }

  /// Return true if instruction generated for equality comparison is folded
  /// with instruction generated for signed comparison.
  ///
  /// @return True if instruction generated for equality comparison is folded with instruction generated for signed comparison.
  virtual bool isEqualityCmpFoldedWithSignedCmp() const { return true; }

  /// Return true if the heuristic to prefer icmp eq zero should be used in code
  /// gen prepare.
  ///
  /// @return True if the heuristic to prefer icmp eq zero should be used in code gen prepare.
  virtual bool preferZeroCompareBranch() const { return false; }

  /// Return true if it is cheaper to split the store of a merged int val
  /// from a pair of smaller values into multiple stores.
  ///
  /// @return True if it is cheaper to split the store of a merged int val from a pair of smaller values into multiple stores.
  ///
  /// \param LTy Low half type.
  /// \param HTy High half type.
  virtual bool isMultiStoresCheaperThanBitsMerge(EVT LTy, EVT HTy) const {
    return false;
  }

  /// Return if the target supports combining a
  /// chain like:
  /// \code
  ///   %andResult = and %val1, #mask
  ///   %icmpResult = icmp %andResult, 0
  /// \endcode
  /// into a single machine instruction of a form like:
  /// \code
  ///   cc = test %register, #mask
  /// \endcode
  /// @return If the target supports combining a chain like:.
  ///
  /// \param AndI And instruction being folded.
  virtual bool isMaskAndCmp0FoldingBeneficial(const Instruction &AndI) const {
    return false;
  }

  /// Return true if it is valid to merge the TargetMMOFlags in two SDNodes.
  ///
  /// @return True if it is valid to merge the TargetMMOFlags in two SDNodes.
  ///
  /// \param NodeX First SDNode whose MMO flags are merged.
  /// \param NodeY Second SDNode whose MMO flags are merged.
  virtual bool
  areTwoSDNodeTargetMMOFlagsMergeable(const MemSDNode &NodeX,
                                      const MemSDNode &NodeY) const {
    return true;
  }

  /// Return true if pairs of comparisons should be combined using bitwise logic
  /// instead of logical ops.
  ///
  /// Use bitwise logic to make pairs of compares more efficient. For example:
  /// and (seteq A, B), (seteq C, D) --> seteq (or (xor A, B), (xor C, D)), 0
  /// This should be true when it takes more than one instruction to lower setcc
  /// (cmp+set on x86 scalar), when bitwise ops are faster than logic on
  /// condition bits (crand on PowerPC), and/or when reducing cmp+br is a win.
  ///
  /// @return True if pairs of comparisons should be combined using bitwise logic instead of logical ops.
  ///
  /// \param VT Value type being queried or transformed.
  virtual bool convertSetCCLogicToBitwiseLogic(EVT VT) const {
    return false;
  }

  /// Return the preferred type for an efficient equality comparison of integers
  /// with the given bit width.
  ///
  /// Return the preferred operand type if the target has a quick way to compare
  /// integer values of the given size. Assume that any legal integer type can be
  /// compared efficiently. Targets may override this to allow illegal wide types
  /// to return a vector type if there is support to compare that type.
  ///
  /// @return The preferred type for an efficient equality comparison of integers with the given bit width.
  ///
  /// \param NumBits Bit size for the equality compare.
  virtual MVT hasFastEqualityCompare(unsigned NumBits) const {
    MVT VT = MVT::getIntegerVT(NumBits);
    return isTypeLegal(VT) ? VT : MVT::INVALID_SIMPLE_VALUE_TYPE;
  }

  /// Return true if the target should transform:
  /// (X & Y) == Y ---> (~X & Y) == 0
  /// (X & Y) != Y ---> (~X & Y) != 0
  ///
  /// This may be profitable if the target has a bitwise and-not operation that
  /// sets comparison flags. A target may want to limit the transformation based
  /// on the type of Y or if Y is a constant.
  ///
  /// Note that the transform will not occur if Y is known to be a power-of-2
  /// because a mask and compare of a single bit can be handled by inverting the
  /// predicate, for example:
  /// (X & 8) == 8 ---> (X & 8) != 0
  ///
  /// @return True if the target should transform: (X & Y) == Y ---> (~X & Y) == 0 (X & Y) != Y ---> (~X & Y) != 0.
  ///
  /// \param Y Possibly-identity operand of the select fold.
  virtual bool hasAndNotCompare(SDValue Y) const {
    return false;
  }

  /// Return true if the target has a bitwise and-not operation:
  /// X = ~A & B
  /// This can be used to simplify select or other instructions.
  ///
  /// @return True if the target has a bitwise and-not operation: X = ~A & B This can be used to simplify select or other instructions.
  ///
  /// \param X Non-identity operand of the select fold.
  virtual bool hasAndNot(SDValue X) const {
    // If the target has the more complex version of this operation, assume that
    // it has this operation too.
    return hasAndNotCompare(X);
  }

  /// Return true if the target has a bit-test instruction for testing a single
  /// bit of X selected by Y.
  ///
  /// Return true if the target has a bit-test instruction: (X & (1 << Y)) ==/!=
  /// 0 This knowledge can be used to prevent breaking the pattern, or creating
  /// it if it could be recognized.
  ///
  /// @return True if the target has a bit-test instruction for testing a single bit of X selected by Y.
  ///
  /// \param X Non-identity operand of the select fold.
  /// \param Y Possibly-identity operand of the select fold.
  virtual bool hasBitTest(SDValue X, SDValue Y) const { return false; }

  /// Return true if clearing extreme bits with two variable shifts is preferred
  /// over masking.
  ///
  /// There are two ways to clear extreme bits (either low or high): Mask: x &
  /// (-1 << y) (the instcombine canonical form) Shifts: x >> y << y Return true
  /// if the variant with 2 variable shifts is preferred. Return false if there
  /// is no preference.
  ///
  /// @return True if clearing extreme bits with two variable shifts is preferred over masking.
  ///
  /// \param X Non-identity operand of the select fold.
  virtual bool shouldFoldMaskToVariableShiftPair(SDValue X) const {
    // By default, let's assume that no one prefers shifts.
    return false;
  }

  /// Return true if folding a pair of constant shifts into a mask is profitable.
  ///
  /// Return true if it is profitable to fold a pair of shifts into a mask. This
  /// is usually true on most targets. But some targets, like Thumb1, have
  /// immediate shift instructions, but no immediate "and" instruction; this
  /// makes the fold unprofitable.
  ///
  /// @return True if folding a pair of constant shifts into a mask is profitable.
  ///
  /// \param N SDNode being queried.
  virtual bool shouldFoldConstantShiftPairToMask(const SDNode *N) const {
    return true;
  }

  /// Return true if a signed-truncation-overflow check should be rewritten using
  /// shifts.
  ///
  /// Should we tranform the IR-optimal check for whether given truncation down
  /// into KeptBits would be truncating or not: (add %x, (1 << (KeptBits-1)))
  /// srccond (1 << KeptBits) Into it's more traditional form: ((%x << C) a>> C)
  /// dstcond %x Return true if we should transform. Return false if there is no
  /// preference.
  ///
  /// @return True if a signed-truncation-overflow check should be rewritten using shifts.
  ///
  /// \param XVT Value type of X.
  /// \param KeptBits Number of bits kept after truncation.
  virtual bool shouldTransformSignedTruncationCheck(EVT XVT,
                                                    unsigned KeptBits) const {
    // By default, let's assume that no one prefers shifts.
    return false;
  }

  /// Return true if a shifted-constant AND pattern should be transformed by
  /// hoisting the constant out of the shift.
  ///
  /// Given the pattern (X & (C l>>/<< Y)) ==/!= 0 return true if it should be
  /// transformed into: ((X <</l>> Y) & C) ==/!= 0 WARNING: if 'X' is a constant,
  /// the fold may deadlock! FIXME: we could avoid passing XC, but we can't use
  /// isConstOrConstSplat() here because it can end up being not linked in.
  ///
  /// @return True if a shifted-constant AND pattern should be transformed by hoisting the constant out of the shift.
  ///
  /// \param X Non-identity operand of the select fold.
  /// \param XC Constant on the LHS of the and.
  /// \param CC Compare constant or calling convention.
  /// \param Y Possibly-identity operand of the select fold.
  /// \param OldShiftOpcode Original shift opcode in the pattern.
  /// \param NewShiftOpcode Proposed replacement shift opcode.
  /// \param DAG SelectionDAG providing context.
  virtual bool shouldProduceAndByConstByHoistingConstFromShiftsLHSOfAnd(
      SDValue X, ConstantSDNode *XC, ConstantSDNode *CC, SDValue Y,
      unsigned OldShiftOpcode, unsigned NewShiftOpcode,
      SelectionDAG &DAG) const {
    if (hasBitTest(X, Y)) {
      // One interesting pattern that we'd want to form is 'bit test':
      //   ((1 << Y) & C) ==/!= 0
      // But we also need to be careful not to try to reverse that fold.

      // Is this '1 << Y' ?
      if (OldShiftOpcode == ISD::SHL && CC->isOne())
        return false; // Keep the 'bit test' pattern.

      // Will it be '1 << Y' after the transform ?
      if (XC && NewShiftOpcode == ISD::SHL && XC->isOne())
        return true; // Do form the 'bit test' pattern.
    }

    // If 'X' is a constant, and we transform, then we will immediately
    // try to undo the fold, thus causing endless combine loop.
    // So by default, let's assume everyone prefers the fold
    // iff 'X' is not a constant.
    return !XC;
  }

  /// Return true if transforming a float multiply/divide by a power of two into
  /// a bitcast add/sub is desirable.
  ///
  /// @return True if transforming a float multiply/divide by a power of two into a bitcast add/sub is desirable.
  ///
  /// \param N SDNode being queried.
  /// \param FPConst Floating-point constant operand.
  /// \param IntPow2 Integer power-of-two constant used in the transform.
  virtual bool optimizeFMulOrFDivAsShiftAddBitcast(SDNode *N, SDValue FPConst,
                                                   SDValue IntPow2) const {
    // Default to avoiding fdiv which is often very expensive.
    return N->getOpcode() == ISD::FDIV;
  }

  // Given:
  //    (icmp eq/ne (and X, C0), (shift X, C1))
  // or
  //    (icmp eq/ne X, (rotate X, CPow2))

  /// Return the preferred shift or rotate opcode for comparing pieces of an
  /// operand for equality.
  ///
  /// @return The preferred shift or rotate opcode for comparing pieces of an operand for equality.
  ///
  /// \param VT Value type being queried or transformed.
  /// \param ShiftOpc Shift opcode in the pattern.
  /// \param MayTransformRotate Whether rotate forms of the pattern may be rewritten.
  /// \param ShiftOrRotateAmt Shift or rotate amount operand.
  /// \param AndMask Mask applied by the and operation in the pattern.
  virtual unsigned preferedOpcodeForCmpEqPiecesOfOperand(
      EVT VT, unsigned ShiftOpc, bool MayTransformRotate,
      const APInt &ShiftOrRotateAmt,
      const std::optional<APInt> &AndMask) const {
    return ShiftOpc;
  }

  /// These two forms are equivalent:
  ///   sub %y, (xor %x, -1)
  ///   add (add %x, 1), %y
  /// The variant with two add's is IR-canonical.
  /// Some targets may prefer one to the other.
  ///
  /// @return True if these two forms are equivalent: sub %y, (xor %x, -1) add (add %x, 1), %y The variant with two add's is IR-canonical. Some targets may prefer one to the other.
  ///
  /// \param VT Value type being queried or transformed.
  virtual bool preferIncOfAddToSubOfNot(EVT VT) const {
    // By default, let's assume that everyone prefers the form with two add's.
    return true;
  }

  /// Return true if folding abs(sub nsw x, y) into an absolute-difference op is
  /// preferred.
  ///
  /// @return True if folding abs(sub nsw x, y) into an absolute-difference op is preferred.
  ///
  /// \param VT Value type being queried or transformed.
  virtual bool preferABDSToABSWithNSW(EVT VT) const {
    return true;
  }

  /// Return true if scalarizing an operation on a splat vector is preferred.
  ///
  /// @return True if scalarizing an operation on a splat vector is preferred.
  ///
  /// \param N SDNode being queried.
  virtual bool preferScalarizeSplat(SDNode *N) const { return true; }

  /// Return true if hoisting a sign-extend-in-reg before a truncate is
  /// preferred.
  ///
  /// @return True if hoisting a sign-extend-in-reg before a truncate is preferred.
  ///
  /// \param TruncVT Truncated value type.
  /// \param VT Value type being queried or transformed.
  /// \param ExtVT Extended or destination value type.
  virtual bool preferSextInRegOfTruncate(EVT TruncVT, EVT VT, EVT ExtVT) const {
    return true;
  }

  /// Return true if extending loads should be promoted through chains of
  /// promotable instructions.
  ///
  /// Return true if the target wants to use the optimization that turns
  /// ext(promotableInst1(...(promotableInstN(load)))) into
  /// promotedInst1(...(promotedInstN(ext(load)))).
  ///
  /// @return True if extending loads should be promoted through chains of promotable instructions.
  bool enableExtLdPromotion() const { return EnableExtLdPromotion; }

  /// Return true if the target can combine store(extractelement VectorTy,
  /// Idx).
  /// \p Cost[out] gives the cost of that transformation when this is true.
  /// @return True if the target can combine store(extractelement VectorTy, Idx).
  ///
  /// \param VectorTy Vector type of the store/extract pattern.
  /// \param Idx Element index.
  /// \param Cost Maximum acceptable negation cost.
  virtual bool canCombineStoreAndExtract(Type *VectorTy, Value *Idx,
                                         unsigned &Cost) const {
    return false;
  }

  /// Return true if a constant vector splat should be stored by extracting a
  /// single element.
  ///
  /// Return true if the target shall perform extract vector element and store
  /// given that the vector is known to be splat of constant.
  ///
  /// \p Index[out] gives the index of the vector element to be extracted when
  /// this is true.
  ///
  /// @return True if a constant vector splat should be stored by extracting a single element.
  ///
  /// \param VectorTy Vector type of the store/extract pattern.
  /// \param ElemSizeInBits Element size in bits.
  /// \param Index Lane or subvector index.
  virtual bool shallExtractConstSplatVectorElementToStore(
      Type *VectorTy, unsigned ElemSizeInBits, unsigned &Index) const {
    return false;
  }

  /// Return true if inserting a scalar into a variable element of an undef
  /// vector is more efficiently handled by splatting the scalar instead.
  ///
  /// @return True if inserting a scalar into a variable element of an undef vector is more efficiently handled by splatting the scalar instead.
  ///
  /// \param VT Value type being queried or transformed.
  virtual bool shouldSplatInsEltVarIndex(EVT VT) const {
    return false;
  }

  /// Return true if the target always benefits from fusing multiply-add into FMA
  /// for this type.
  ///
  /// Return true if target always benefits from combining into FMA for a given
  /// value type. This must typically return false on targets where FMA takes
  /// more cycles to execute than FADD.
  ///
  /// @return True if the target always benefits from fusing multiply-add into FMA for this type.
  ///
  /// \param VT Value type being queried or transformed.
  virtual bool enableAggressiveFMAFusion(EVT VT) const { return false; }

  /// Return true if the target always benefits from fusing multiply-add into FMA
  /// for this type.
  ///
  /// Return true if target always benefits from combining into FMA for a given
  /// value type. This must typically return false on targets where FMA takes
  /// more cycles to execute than FADD.
  ///
  /// @return True if the target always benefits from fusing multiply-add into FMA for this type.
  ///
  /// \param Ty Type of the constant or operation.
  virtual bool enableAggressiveFMAFusion(LLT Ty) const { return false; }

  /// Return the ValueType of the result of SETCC operations.
  ///
  /// @return The ValueType of the result of SETCC operations.
  ///
  /// \param DL Debug location or data layout, depending on context.
  /// \param Context LLVMContext used for type creation.
  /// \param VT Value type being queried or transformed.
  virtual EVT getSetCCResultType(const DataLayout &DL, LLVMContext &Context,
                                 EVT VT) const;

  /// Return the value type used for floating-point comparison libcalls.
  ///
  /// Return the ValueType for comparison libcalls. Comparison libcalls include
  /// floating point comparison calls, and Ordered/Unordered check calls on
  /// floating point numbers.
  ///
  /// @return The value type used for floating-point comparison libcalls.
  virtual MVT::SimpleValueType getCmpLibcallReturnType() const {
    return MVT::i32; // return the default value
  }

  /// For targets without i1 registers, this gives the nature of the high-bits
  /// of boolean values held in types wider than i1.
  ///
  /// "Boolean values" are special true/false values produced by nodes like
  /// SETCC and consumed (as the condition) by nodes like SELECT and BRCOND.
  /// Not to be confused with general values promoted from i1.  Some cpus
  /// distinguish between vectors of boolean and scalars; the isVec parameter
  /// selects between the two kinds.  For example on X86 a scalar boolean should
  /// be zero extended from i1, while the elements of a vector of booleans
  /// should be sign extended from i1.
  ///
  /// Some cpus also treat floating point types the same way as they treat
  /// vectors instead of the way they treat scalars.
  ///
  /// @return For targets without i1 registers, this gives the nature of the high-bits of boolean values held in types wider than i1.
  ///
  /// \param isVec Whether the boolean is a vector boolean.
  /// \param isFloat Whether the boolean comes from a floating-point compare.
  BooleanContent getBooleanContents(bool isVec, bool isFloat) const {
    if (isVec)
      return BooleanVectorContents;
    return isFloat ? BooleanFloatContents : BooleanContents;
  }

  /// Return how boolean values of the given vector or scalar category are
  /// represented in wider types.
  ///
  /// @return How boolean values of the given vector or scalar category are represented in wider types.
  ///
  /// \param Type Type considered for libcall extension.
  BooleanContent getBooleanContents(EVT Type) const {
    return getBooleanContents(Type.isVector(), Type.isFloatingPoint());
  }

  /// Promote a target boolean value to the boolean representation of the given
  /// type.
  ///
  /// Promote the given target boolean to a target boolean of the given type. A
  /// target boolean is an integer value, not necessarily of type i1, the bits of
  /// which conform to getBooleanContents.
  /// ValVT is the type of values that produced the boolean.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param DAG SelectionDAG providing context.
  /// \param Bool Target boolean value to promote.
  /// \param ValVT Value type determining boolean contents.
  SDValue promoteTargetBoolean(SelectionDAG &DAG, SDValue Bool,
                               EVT ValVT) const {
    SDLoc dl(Bool);
    EVT BoolVT =
        getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), ValVT);
    ISD::NodeType ExtendCode = getExtendForContent(getBooleanContents(ValVT));
    return DAG.getNode(ExtendCode, dl, BoolVT, Bool);
  }

  /// Return the target's preferred scheduling heuristic, optionally specialized
  /// for a given node.
  ///
  /// Return target scheduling preference.
  ///
  /// @return The target's preferred scheduling heuristic, optionally specialized for a given node.
  Sched::Preference getSchedulingPreference() const {
    return SchedPreferenceInfo;
  }

  /// Return the target's preferred scheduling heuristic, optionally specialized
  /// for a given node.
  ///
  /// Some scheduler, e.g. hybrid, can switch to different scheduling heuristics
  /// for different nodes. This function returns the preference (or none) for the
  /// given node.
  ///
  /// @return The target's preferred scheduling heuristic, optionally specialized for a given node.
  ///
  /// \param N SDNode being queried.
  virtual Sched::Preference getSchedulingPreference(SDNode *N) const {
    return Sched::None;
  }

  /// Return the register class that should be used for the specified value
  /// type.
  ///
  /// @return The register class that should be used for the specified value type.
  ///
  /// \param VT Value type being queried.
  /// \param isDivergent Whether the value is divergent across control flow.
  virtual const TargetRegisterClass *getRegClassFor(MVT VT, bool isDivergent = false) const {
    (void)isDivergent;
    const TargetRegisterClass *RC = RegClassForVT[VT.SimpleTy];
    assert(RC && "This value type is not natively supported!");
    return RC;
  }

  /// Return true if the given value requires a uniform register class across
  /// divergent control flow.
  ///
  /// Allows target to decide about the register class of the specific value that
  /// is live outside the defining block. Returns true if the value needs uniform
  /// register class.
  ///
  /// @return True if the given value requires a uniform register class across divergent control flow.
  ///
  /// \param MF Machine function being lowered.
  /// \param V Value whose register class is queried.
  virtual bool requiresUniformRegister(MachineFunction &MF,
                                       const Value *V) const {
    return false;
  }

  /// Return the 'representative' register class for the specified value
  /// type.
  ///
  /// The 'representative' register class is the largest legal super-reg
  /// register class for the register class of the value type.  For example, on
  /// i386 the rep register class for i8, i16, and i32 are GR32; while the rep
  /// register class is GR64 on x86_64.
  ///
  /// @return The 'representative' register class for the specified value type.
  ///
  /// \param VT Value type being queried.
  virtual const TargetRegisterClass *getRepRegClassFor(MVT VT) const {
    const TargetRegisterClass *RC = RepRegClassForVT[VT.SimpleTy];
    return RC;
  }

  /// Return the cost of the 'representative' register class for the specified
  /// value type.
  ///
  /// @return The cost of the 'representative' register class for the specified value type.
  ///
  /// \param VT Value type being queried or transformed.
  virtual uint8_t getRepRegClassCostFor(MVT VT) const {
    return RepRegClassCostForVT[VT.SimpleTy];
  }

  /// Return the preferred strategy to legalize tihs SHIFT instruction, with
  /// \p ExpansionFactor being the recursion depth - how many expansion needed.
  enum class ShiftLegalizationStrategy {
    /// Expand the shift into smaller shift parts.
    ExpandToParts,
    /// Expand the shift using a stack temporary.
    ExpandThroughStack,
    /// Lower the shift to a runtime libcall.
    LowerToLibcall
  };
  /// Return the preferred strategy for legalizing an oversized shift
  /// instruction.
  ///
  /// @return The preferred strategy for legalizing an oversized shift instruction.
  ///
  /// \param DAG SelectionDAG providing context.
  /// \param N SDNode being queried.
  /// \param ExpansionFactor Recursion depth of shift expansion.
  virtual ShiftLegalizationStrategy
  preferredShiftLegalizationStrategy(SelectionDAG &DAG, SDNode *N,
                                     unsigned ExpansionFactor) const {
    if (ExpansionFactor == 1)
      return ShiftLegalizationStrategy::ExpandToParts;
    return ShiftLegalizationStrategy::ExpandThroughStack;
  }

  /// Return true if the target has native register support for the given value
  /// type.
  ///
  /// Return true if the target has native support for the specified value type.
  /// This means that it has a register that directly holds it without promotions
  /// or expansions.
  ///
  /// @return True if the target has native register support for the given value type.
  ///
  /// \param VT Value type being queried or transformed.
  bool isTypeLegal(EVT VT) const {
    assert(!VT.isSimple() ||
           (unsigned)VT.getSimpleVT().SimpleTy < std::size(RegClassForVT));
    return VT.isSimple() && RegClassForVT[VT.getSimpleVT().SimpleTy] != nullptr;
  }

  /// Tracks the legalize-type action to use for each value type.
  class ValueTypeActionImpl {
    /// ValueTypeActions - For each value type, keep a LegalizeTypeAction enum
    /// that indicates how instruction selection should deal with the type.
    LegalizeTypeAction ValueTypeActions[MVT::VALUETYPE_SIZE];

  public:
    /// Tracks the legalize-type action to use for each value type.
    ValueTypeActionImpl() { llvm::fill(ValueTypeActions, TypeLegal); }

    /// Return how values of this type should be legalized: as legal, promoted,
    /// or expanded.
    ///
    /// @return How values of this type should be legalized: as legal, promoted, or expanded.
    ///
    /// \param VT Value type being queried or transformed.
    LegalizeTypeAction getTypeAction(MVT VT) const {
      return ValueTypeActions[VT.SimpleTy];
    }

    /// Set the legalize-type action to use for the given value type.
    ///
    /// \param VT Value type being queried or transformed.
    /// \param Action Legalize action to record.
    void setTypeAction(MVT VT, LegalizeTypeAction Action) {
      ValueTypeActions[VT.SimpleTy] = Action;
    }
  };

  /// Return the table of per-EVT legalization actions.
  ///
  /// @return The table of per-EVT legalization actions.
  const ValueTypeActionImpl &getValueTypeActions() const {
    return ValueTypeActions;
  }

  /// Return pair that represents the legalization kind (first) that needs to
  /// happen to EVT (second) in order to type-legalize it.
  ///
  /// First: how we should legalize values of this type, either it is already
  /// legal (return 'Legal') or we need to promote it to a larger type (return
  /// 'Promote'), or we need to expand it into multiple registers of smaller
  /// integer type (return 'Expand').  'Custom' is not an option.
  ///
  /// Second: for types supported by the target, this is an identity function.
  /// For types that must be promoted to larger types, this returns the larger
  /// type to promote to.  For integer types that are larger than the largest
  /// integer register, this contains one step in the expansion to get to the
  /// smaller register. For illegal floating point types, this returns the
  /// integer type to transform to.
  ///
  /// @return Pair that represents the legalization kind (first) that needs to happen to EVT (second) in order to type-legalize it.
  ///
  /// \param Context LLVMContext used for type creation.
  /// \param VT Value type being queried or transformed.
  LegalizeKind getTypeConversion(LLVMContext &Context, EVT VT) const;

  /// Return how values of this type should be legalized: as legal, promoted, or
  /// expanded.
  ///
  /// Return how we should legalize values of this type, either it is already
  /// legal (return 'Legal') or we need to promote it to a larger type (return
  /// 'Promote'), or we need to expand it into multiple registers of smaller
  /// integer type (return 'Expand'). 'Custom' is not an option.
  ///
  /// @return How values of this type should be legalized: as legal, promoted, or expanded.
  ///
  /// \param Context LLVMContext used for type creation.
  /// \param VT Value type being queried or transformed.
  LegalizeTypeAction getTypeAction(LLVMContext &Context, EVT VT) const {
    return getTypeConversion(Context, VT).first;
  }
  /// Return how values of this type should be legalized: as legal, promoted, or
  /// expanded.
  ///
  /// @return How values of this type should be legalized: as legal, promoted, or expanded.
  ///
  /// \param VT Value type being queried or transformed.
  LegalizeTypeAction getTypeAction(MVT VT) const {
    return ValueTypeActions.getTypeAction(VT);
  }

  /// Return the type that this type is promoted, expanded, or otherwise
  /// transformed to.
  ///
  /// For types supported by the target, this is an identity function. For types
  /// that must be promoted to larger types, this returns the larger type to
  /// promote to. For integer types that are larger than the largest integer
  /// register, this contains one step in the expansion to get to the smaller
  /// register. For illegal floating point types, this returns the integer type
  /// to transform to.
  ///
  /// @return The type that this type is promoted, expanded, or otherwise transformed to.
  ///
  /// \param Context LLVMContext used for type creation.
  /// \param VT Value type being queried or transformed.
  virtual EVT getTypeToTransformTo(LLVMContext &Context, EVT VT) const {
    return getTypeConversion(Context, VT).second;
  }

  /// Perform getTypeToTransformTo repeatedly until a legal type is obtained.
  /// Useful for vector operations that might take multiple steps to legalize.
  ///
  /// @return Perform getTypeToTransformTo repeatedly until a legal type is obtained. Useful for vector operations that might take multiple steps to legalize.
  ///
  /// \param Context LLVMContext used for type creation.
  /// \param VT Value type being queried or transformed.
  EVT getLegalTypeToTransformTo(LLVMContext &Context, EVT VT) const {
    EVT LegalVT = getTypeToTransformTo(Context, VT);
    while (LegalVT != VT) {
      VT = LegalVT;
      LegalVT = getTypeToTransformTo(Context, VT);
    }
    return LegalVT;
  }

  /// Return the largest legal type that this type will be expanded to.
  ///
  /// For types supported by the target, this is an identity function. For types
  /// that must be expanded (i.e. integer types that are larger than the largest
  /// integer register or illegal floating point types), this returns the largest
  /// legal type it will be expanded to.
  ///
  /// @return The largest legal type that this type will be expanded to.
  ///
  /// \param Context LLVMContext used for type creation.
  /// \param VT Value type being queried or transformed.
  EVT getTypeToExpandTo(LLVMContext &Context, EVT VT) const {
    assert(!VT.isVector());
    while (true) {
      switch (getTypeAction(Context, VT)) {
      case TypeLegal:
        return VT;
      case TypeExpandInteger:
        VT = getTypeToTransformTo(Context, VT);
        break;
      default:
        llvm_unreachable("Type is not legal nor is it to be expanded!");
      }
    }
  }

  /// Return the number of registers and intermediate type needed to break a
  /// vector type into legal parts.
  ///
  /// Vector types are broken down into some number of legal first class types.
  /// For example, EVT::v8f32 maps to 2 EVT::v4f32 with Altivec or SSE1, or 8
  /// promoted EVT::f64 values with the X86 FP stack. Similarly, EVT::v2i64 turns
  /// into 4 EVT::i32 values with both PPC and X86.
  /// This method returns the number of registers needed, and the VT for each
  /// register.  It also returns the VT and quantity of the intermediate values
  /// before they are promoted/expanded.
  ///
  /// @return The number of registers and intermediate type needed to break a vector type into legal parts.
  ///
  /// \param Context LLVMContext used for type creation.
  /// \param VT Value type being queried or transformed.
  /// \param IntermediateVT Filled with the intermediate legal VT.
  /// \param NumIntermediates Filled with the number of intermediate values.
  /// \param RegisterVT Optional overriding register VT.
  unsigned getVectorTypeBreakdown(LLVMContext &Context, EVT VT,
                                  EVT &IntermediateVT,
                                  unsigned &NumIntermediates,
                                  MVT &RegisterVT) const {
    return getVectorTypeBreakdownImpl(Context, VT, IntermediateVT,
                                      NumIntermediates, RegisterVT,
                                      /*ForCallingConv=*/false);
  }

  /// Return true if fixed-length, non-power-of-two vectors should be broken
  /// down into legal vector parts instead of scalars for internal values.
  ///
  /// @return True if fixed-length, non-power-of-two vectors should be broken down into legal vector parts instead of scalars for internal values.
  virtual bool preferVectorizedNonPowerOfTwoTypeBreakdown() const {
    return false;
  }

  /// Return true if this vector type should use a dynamic, non-power-of-two
  /// register breakdown.
  ///
  /// @return True if this vector type should use a dynamic, non-power-of-two register breakdown.
  ///
  /// \param VT Value type being queried or transformed.
  /// \param ForCallingConv Whether the breakdown is for a calling convention.
  bool shouldUseDynamicVectorTypeBreakdown(EVT VT, bool ForCallingConv) const {
    return preferVectorizedNonPowerOfTwoTypeBreakdown() && !ForCallingConv &&
           VT.isFixedLengthVector() &&
           !isPowerOf2_32(VT.getVectorNumElements());
  }

  /// Return the register breakdown of a vector type under the given calling
  /// convention.
  ///
  /// Certain targets such as MIPS require that some types such as vectors are
  /// always broken down into scalars in some contexts. This occurs even if the
  /// vector type is legal.
  ///
  /// @return The register breakdown of a vector type under the given calling convention.
  ///
  /// \param Context LLVMContext used for type creation.
  /// \param CC Compare constant or calling convention.
  /// \param VT Value type being queried or transformed.
  /// \param IntermediateVT Filled with the intermediate legal VT.
  /// \param NumIntermediates Filled with the number of intermediate values.
  /// \param RegisterVT Optional overriding register VT.
  virtual unsigned getVectorTypeBreakdownForCallingConv(
      LLVMContext &Context, CallingConv::ID CC, EVT VT, EVT &IntermediateVT,
      unsigned &NumIntermediates, MVT &RegisterVT) const {
    return getVectorTypeBreakdownImpl(Context, VT, IntermediateVT,
                                      NumIntermediates, RegisterVT,
                                      /*ForCallingConv=*/true);
  }

  /// Holds memory-access information describing how a target intrinsic touches
  /// memory.
  struct IntrinsicInfo {
    /// Target opcode of the memory intrinsic.
    unsigned     opc = 0;
    /// Memory value type accessed by the intrinsic.
    EVT          memVT;

    /// Value representing the memory location.
    PointerUnion<const Value *, const PseudoSourceValue *> ptrVal;

    /// Fallback address space when ptrVal is null; nullopt means unknown.
    std::optional<unsigned> fallbackAddressSpace;

    /// Byte offset from ptrVal.
    int          offset = 0;
    /// Size of the memory location; taken from memVT when zero.
    uint64_t     size = 0;
    /// Alignment of the memory access.
    MaybeAlign align = Align(1);

    /// The MachineMemOperand flags describing this memory access.
    MachineMemOperand::Flags flags = MachineMemOperand::MONone;
    /// The synchronization scope of this atomic memory access.
    SyncScope::ID ssid = SyncScope::System;
    /// The atomic ordering to use for this memory access.
    AtomicOrdering order = AtomicOrdering::NotAtomic;
    /// The atomic ordering to use on a failed compare-and-swap.
    AtomicOrdering failureOrder = AtomicOrdering::NotAtomic;
    /// Holds memory-access information describing how a target intrinsic touches
    /// memory.
    IntrinsicInfo() = default;
  };

  /// Return the memory-access information for an intrinsic that touches memory,
  /// if any.
  ///
  /// Given an intrinsic, checks if on the target the intrinsic will need to map
  /// to a MemIntrinsicNode (touches memory). If this is the case, it stores the
  /// intrinsic information into the IntrinsicInfo vector passed to the function.
  /// The vector may contain multiple entries for intrinsics that access multiple
  /// memory locations.
  ///
  /// \param Infos Filled with target mem-intrinsic descriptors.
  /// \param I Instruction being queried.
  /// \param MF Machine function being lowered.
  /// \param Intrinsic Intrinsic ID being classified.
  virtual void getTgtMemIntrinsic(SmallVectorImpl<IntrinsicInfo> &Infos,
                                  const CallBase &I, MachineFunction &MF,
                                  unsigned Intrinsic) const {}

  /// Return true if the target can natively materialize the given floating-point
  /// immediate.
  ///
  /// Returns true if the target can instruction select the specified FP
  /// immediate natively. If false, the legalizer will materialize the FP
  /// immediate as a load from a constant pool.
  ///
  /// @return True if the target can natively materialize the given floating-point immediate.
  ///
  /// \param Imm Immediate constant operand.
  /// \param VT Value type being queried or transformed.
  /// \param ForCodeSize Whether the query is for code-size optimization.
  virtual bool isFPImmLegal(const APFloat &Imm, EVT VT,
                            bool ForCodeSize = false) const {
    return false;
  }

  /// Return true if the given shuffle mask is legal for this vector type.
  ///
  /// Targets can use this to indicate that they only support *some*
  /// VECTOR_SHUFFLE operations, those with specific masks. By default, if a
  /// target supports the VECTOR_SHUFFLE node, all mask values are assumed to be
  /// legal.
  ///
  /// @return True if the given shuffle mask is legal for this vector type.
  ///
  /// \param Mask Mask operand or value.
  /// \param VT Value type being queried or transformed.
  virtual bool isShuffleMaskLegal(ArrayRef<int> Mask, EVT VT) const {
    return true;
  }

  /// Returns true if the operation can trap for the value type.
  ///
  /// VT must be a legal type. By default, we optimistically assume most
  /// operations don't trap except for integer divide and remainder.
  ///
  /// @return True if the operation can trap for the value type.
  ///
  /// \param Op SDNode or value being queried.
  /// \param VT Value type being queried or transformed.
  virtual bool canOpTrap(unsigned Op, EVT VT) const;

  /// Return true if the given mask can replace a vector AND with a shuffle from
  /// a constant pool.
  ///
  /// Similar to isShuffleMaskLegal. Targets can use this to indicate if there is
  /// a suitable VECTOR_SHUFFLE that can be used to replace a VAND with a
  /// constant pool entry.
  ///
  /// @return True if the given mask can replace a vector AND with a shuffle from a constant pool.
  ///
  /// \param Mask Mask operand or value.
  /// \param VT Value type being queried or transformed.
  virtual bool isVectorClearMaskLegal(ArrayRef<int> Mask,
                                      EVT VT) const {
    return false;
  }

  /// How to legalize this custom operation?
  ///
  /// @return How to legalize this custom operation?.
  ///
  /// \param Op SDNode or value being queried.
  virtual LegalizeAction getCustomOperationAction(SDNode &Op) const {
    return Legal;
  }

  /// Return how the given operation should be treated for the given value type.
  ///
  /// Return how this operation should be treated: either it is legal, needs to
  /// be promoted to a larger size, needs to be expanded to some other code
  /// sequence, or the target has a custom expander for it.
  ///
  /// @return How the given operation should be treated for the given value type.
  ///
  /// \param Op SDNode or value being queried.
  /// \param VT Value type being queried or transformed.
  LegalizeAction getOperationAction(unsigned Op, EVT VT) const {
    // If a target-specific SDNode requires legalization, require the target
    // to provide custom legalization for it.
    if (Op >= std::size(OpActions[0]))
      return Custom;
    if (VT.isExtended())
      return Expand;
    return OpActions[(unsigned)VT.getSimpleVT().SimpleTy][Op];
  }

  /// Return true if the target natively supports the given fixed-point operation
  /// at the given scale.
  ///
  /// Custom method defined by each target to indicate if an operation which may
  /// require a scale is supported natively by the target. If not, the operation
  /// is illegal.
  ///
  /// @return True if the target natively supports the given fixed-point operation at the given scale.
  ///
  /// \param Op SDNode or value being queried.
  /// \param VT Value type being queried or transformed.
  /// \param Scale Fixed-point scale or addressing scale.
  virtual bool isSupportedFixedPointOperation(unsigned Op, EVT VT,
                                              unsigned Scale) const {
    return false;
  }

  /// Return how a fixed-point operation with the given scale should be treated
  /// by the target.
  ///
  /// Some fixed point operations may be natively supported by the target but
  /// only for specific scales. This method allows for checking if the width is
  /// supported by the target for a given operation that may depend on scale.
  ///
  /// @return How a fixed-point operation with the given scale should be treated by the target.
  ///
  /// \param Op SDNode or value being queried.
  /// \param VT Value type being queried or transformed.
  /// \param Scale Fixed-point scale or addressing scale.
  LegalizeAction getFixedPointOperationAction(unsigned Op, EVT VT,
                                              unsigned Scale) const {
    auto Action = getOperationAction(Op, VT);
    if (Action != Legal)
      return Action;

    // This operation is supported in this type but may only work on specific
    // scales.
    bool Supported;
    switch (Op) {
    default:
      llvm_unreachable("Unexpected fixed point operation.");
    case ISD::SMULFIX:
    case ISD::SMULFIXSAT:
    case ISD::UMULFIX:
    case ISD::UMULFIXSAT:
    case ISD::SDIVFIX:
    case ISD::SDIVFIXSAT:
    case ISD::UDIVFIX:
    case ISD::UDIVFIXSAT:
      Supported = isSupportedFixedPointOperation(Op, VT, Scale);
      break;
    }

    return Supported ? Action : Expand;
  }

  /// Return the legalization action for a strict floating-point operation using
  /// its non-strict equivalent.
  ///
  /// @return The legalization action for a strict floating-point operation using its non-strict equivalent.
  ///
  /// \param Op SDNode or value being queried.
  /// \param VT Value type being queried or transformed.
  LegalizeAction getStrictFPOperationAction(unsigned Op, EVT VT) const {
    unsigned EqOpc;
    switch (Op) {
      default: llvm_unreachable("Unexpected FP pseudo-opcode");
#define DAG_INSTRUCTION(NAME, NARG, ROUND_MODE, INTRINSIC, DAGN)               \
      case ISD::STRICT_##DAGN: EqOpc = ISD::DAGN; break;
#define CMP_INSTRUCTION(NAME, NARG, ROUND_MODE, INTRINSIC, DAGN)               \
      case ISD::STRICT_##DAGN: EqOpc = ISD::SETCC; break;
#include "llvm/IR/ConstrainedOps.def"
    }

    return getOperationAction(EqOpc, VT);
  }

  /// Return true if the given operation is legal or custom for this type.
  ///
  /// Return true if the specified operation is legal on this target or can be
  /// made legal with custom lowering. This is used to help guide high-level
  /// lowering decisions. LegalOnly is an optional convenience for code paths
  /// traversed pre and post legalisation.
  ///
  /// @return True if the given operation is legal or custom for this type.
  ///
  /// \param Op SDNode or value being queried.
  /// \param VT Value type being queried or transformed.
  /// \param LegalOnly If true, only treat Legal as acceptable.
  bool isOperationLegalOrCustom(unsigned Op, EVT VT,
                                bool LegalOnly = false) const {
    if (LegalOnly)
      return isOperationLegal(Op, VT);

    return (VT == MVT::Other || isTypeLegal(VT)) &&
      (getOperationAction(Op, VT) == Legal ||
       getOperationAction(Op, VT) == Custom);
  }

  /// Return true if the given operation is legal or can be made legal via
  /// promotion for this type.
  ///
  /// Return true if the specified operation is legal on this target or can be
  /// made legal using promotion. This is used to help guide high-level lowering
  /// decisions. LegalOnly is an optional convenience for code paths traversed
  /// pre and post legalisation.
  ///
  /// @return True if the given operation is legal or can be made legal via promotion for this type.
  ///
  /// \param Op SDNode or value being queried.
  /// \param VT Value type being queried or transformed.
  /// \param LegalOnly If true, only treat Legal as acceptable.
  bool isOperationLegalOrPromote(unsigned Op, EVT VT,
                                 bool LegalOnly = false) const {
    if (LegalOnly)
      return isOperationLegal(Op, VT);

    return (VT == MVT::Other || isTypeLegal(VT)) &&
      (getOperationAction(Op, VT) == Legal ||
       getOperationAction(Op, VT) == Promote);
  }

  /// Return true if the given operation is legal, custom, or promotable for this
  /// type.
  ///
  /// Return true if the specified operation is legal on this target or can be
  /// made legal with custom lowering or using promotion. This is used to help
  /// guide high-level lowering decisions. LegalOnly is an optional convenience
  /// for code paths traversed pre and post legalisation.
  ///
  /// @return True if the given operation is legal, custom, or promotable for this type.
  ///
  /// \param Op SDNode or value being queried.
  /// \param VT Value type being queried or transformed.
  /// \param LegalOnly If true, only treat Legal as acceptable.
  bool isOperationLegalOrCustomOrPromote(unsigned Op, EVT VT,
                                         bool LegalOnly = false) const {
    if (LegalOnly)
      return isOperationLegal(Op, VT);

    return (VT == MVT::Other || isTypeLegal(VT)) &&
      (getOperationAction(Op, VT) == Legal ||
       getOperationAction(Op, VT) == Custom ||
       getOperationAction(Op, VT) == Promote);
  }

  /// Return true if the operation uses custom lowering, regardless of whether
  /// the type is legal or not.
  ///
  /// @return True if the operation uses custom lowering, regardless of whether the type is legal or not.
  ///
  /// \param Op SDNode or value being queried.
  /// \param VT Value type being queried or transformed.
  bool isOperationCustom(unsigned Op, EVT VT) const {
    return getOperationAction(Op, VT) == Custom;
  }

  /// Return true if lowering to a jump table is allowed.
  ///
  /// @return True if lowering to a jump table is allowed.
  ///
  /// \param Fn Function for which jump tables are considered.
  virtual bool areJTsAllowed(const Function *Fn) const {
    if (Fn->getFnAttribute("no-jump-tables").getValueAsBool())
      return false;

    return isOperationLegalOrCustom(ISD::BR_JT, MVT::Other) ||
           isOperationLegalOrCustom(ISD::BRIND, MVT::Other);
  }

  /// Check whether the range [Low,High] fits in a machine word.
  ///
  /// @return True if check whether the range [Low,High] fits in a machine word.
  ///
  /// \param Low Lowest case value.
  /// \param High Highest case value.
  /// \param DL Debug location or data layout, depending on context.
  bool rangeFitsInWord(const APInt &Low, const APInt &High,
                       const DataLayout &DL) const {
    // FIXME: Using the pointer type doesn't seem ideal.
    uint64_t BW = DL.getIndexSizeInBits(0u);
    uint64_t Range = (High - Low).getLimitedValue(UINT64_MAX - 1) + 1;
    return Range <= BW;
  }

  /// Return true if lowering to a jump table is suitable for a set of case
  /// clusters which may contain \p NumCases cases, \p Range range of values.
  ///
  /// @return True if lowering to a jump table is suitable for a set of case clusters which may contain \p NumCases cases, \p Range range of values.
  ///
  /// \param SI Switch or store instruction.
  /// \param NumCases Number of switch cases.
  /// \param Range Span of case values.
  /// \param PSI Profile summary info.
  /// \param BFI Block frequency info.
  virtual bool isSuitableForJumpTable(const SwitchInst *SI, uint64_t NumCases,
                                      uint64_t Range, ProfileSummaryInfo *PSI,
                                      BlockFrequencyInfo *BFI) const;

  /// Returns preferred type for switch condition.
  ///
  /// @return Preferred type for switch condition.
  ///
  /// \param Context LLVMContext used for type creation.
  /// \param ConditionVT Current switch condition value type.
  virtual MVT getPreferredSwitchConditionType(LLVMContext &Context,
                                              EVT ConditionVT) const;

  /// Return true if a switch with the given case clusters is suitable for
  /// lowering to bit tests.
  ///
  /// Return true if lowering to a bit test is suitable for a set of case
  /// clusters which contains \p NumDests unique destinations, \p Low and
  ///
  /// \p High as its lowest and highest case values, and expects \p NumCmps
  /// case value comparisons. Check if the number of destinations, comparison
  /// metric, and range are all suitable.
  ///
  /// @return True if a switch with the given case clusters is suitable for lowering to bit tests.
  ///
  /// \param DestCmps Map of destinations to compare counts.
  /// \param Low Lowest case value.
  /// \param High Highest case value.
  /// \param DL Debug location or data layout, depending on context.
  bool isSuitableForBitTests(
      const DenseMap<const BasicBlock *, unsigned int> &DestCmps,
      const APInt &Low, const APInt &High, const DataLayout &DL) const {
    // FIXME: I don't think NumCmps is the correct metric: a single case and a
    // range of cases both require only one branch to lower. Just looking at the
    // number of clusters and destinations should be enough to decide whether to
    // build bit tests.

    // To lower a range with bit tests, the range must fit the bitwidth of a
    // machine word.
    if (!rangeFitsInWord(Low, High, DL))
      return false;

    unsigned NumDests = DestCmps.size();
    unsigned NumCmps = 0;
    unsigned int MaxBitTestEntry = 0;
    for (auto &DestCmp : DestCmps) {
      NumCmps += DestCmp.second;
      if (DestCmp.second > MaxBitTestEntry)
        MaxBitTestEntry = DestCmp.second;
    }

    // Comparisons might be cheaper for small number of comparisons, which can
    // be Arch Target specific.
    if (MaxBitTestEntry < getMinimumBitTestCmps())
      return false;

    // Decide whether it's profitable to lower this range with bit tests. Each
    // destination requires a bit test and branch, and there is an overall range
    // check branch. For a small number of clusters, separate comparisons might
    // be cheaper, and for many destinations, splitting the range might be
    // better.
    return (NumDests == 1 && NumCmps >= 3) || (NumDests == 2 && NumCmps >= 5) ||
           (NumDests == 3 && NumCmps >= 6);
  }

  /// Return true if the given operation must be expanded for this type.
  ///
  /// Return true if the specified operation is illegal on this target or
  /// unlikely to be made legal with custom lowering. This is used to help guide
  /// high-level lowering decisions.
  ///
  /// @return True if the given operation must be expanded for this type.
  ///
  /// \param Op SDNode or value being queried.
  /// \param VT Value type being queried or transformed.
  bool isOperationExpand(unsigned Op, EVT VT) const {
    return (!isTypeLegal(VT) || getOperationAction(Op, VT) == Expand);
  }

  /// Return true if the specified operation is legal on this target.
  ///
  /// @return True if the specified operation is legal on this target.
  ///
  /// \param Op SDNode or value being queried.
  /// \param VT Value type being queried or transformed.
  bool isOperationLegal(unsigned Op, EVT VT) const {
    return (VT == MVT::Other || isTypeLegal(VT)) &&
           getOperationAction(Op, VT) == Legal;
  }

  /// Return true if the given operation must be expanded or lowered to a libcall
  /// for this type.
  ///
  /// @return True if the given operation must be expanded or lowered to a libcall for this type.
  ///
  /// \param Op SDNode or value being queried.
  /// \param VT Value type being queried or transformed.
  bool isOperationExpandOrLibCall(unsigned Op, EVT VT) const {
    return isOperationExpand(Op, VT) || getOperationAction(Op, VT) == LibCall;
  }

  /// Return the custom action to use for a load when the coarse lookup yields
  /// Custom.
  ///
  /// Returns an alternative action to use when the coarser lookups (configured
  /// through `setLoadExtAction` and `setAtomicLoadExtAction`) yield
  /// `LegalizeAction::Custom`. Allows targets to use builtin behaviors (e.g.
  /// Legal, Promote) specialized by Alignment and AddrSpace, rather than just
  /// types.
  ///
  /// @return The custom action to use for a load when the coarse lookup yields Custom.
  ///
  /// \param ValVT Value type determining boolean contents.
  /// \param MemVT Memory value type of the access.
  /// \param Alignment Required or preferred alignment.
  /// \param AddrSpace Address space of the memory access.
  /// \param ExtType Load extension kind.
  /// \param Atomic Whether the access is atomic.
  virtual LegalizeAction
  getCustomLoadAction(EVT ValVT, EVT MemVT, Align Alignment, unsigned AddrSpace,
                      unsigned ExtType, bool Atomic) const {
    return LegalizeAction::Custom;
  }

  /// Return how a load with the given extension type should be treated for the
  /// given types.
  ///
  /// Return how this load with extension should be treated: either it is legal,
  /// needs to be promoted to a larger size, needs to be expanded to some other
  /// code sequence, or the target has a custom expander for it.
  ///
  /// @return How a load with the given extension type should be treated for the given types.
  ///
  /// \param ValVT Value type determining boolean contents.
  /// \param MemVT Memory value type of the access.
  /// \param Alignment Required or preferred alignment.
  /// \param AddrSpace Address space of the memory access.
  /// \param ExtType Load extension kind.
  /// \param Atomic Whether the access is atomic.
  LegalizeAction getLoadAction(EVT ValVT, EVT MemVT, Align Alignment,
                               unsigned AddrSpace, unsigned ExtType,
                               bool Atomic) const {
    if (ValVT.isExtended() || MemVT.isExtended())
      return Expand;
    unsigned ValI = (unsigned)ValVT.getSimpleVT().SimpleTy;
    unsigned MemI = (unsigned)MemVT.getSimpleVT().SimpleTy;
    assert(ExtType < ISD::LAST_LOADEXT_TYPE && ValI < MVT::VALUETYPE_SIZE &&
           MemI < MVT::VALUETYPE_SIZE && "Table isn't big enough!");
    unsigned Shift = 4 * ExtType;

    LegalizeAction Action;
    if (Atomic) {
      Action =
          (LegalizeAction)((AtomicLoadExtActions[ValI][MemI] >> Shift) & 0xf);
      assert((Action == Legal || Action == Expand) &&
             "Unsupported atomic load extension action.");
    } else {
      Action = (LegalizeAction)((LoadExtActions[ValI][MemI] >> Shift) & 0xf);
    }

    if (Action == LegalizeAction::Custom) {
      return getCustomLoadAction(ValVT, MemVT, Alignment, AddrSpace, ExtType,
                                 Atomic);
    }

    return Action;
  }

  /// Return true if the specified load with extension is legal on this target.
  ///
  /// @return True if the specified load with extension is legal on this target.
  ///
  /// \param ValVT Value type determining boolean contents.
  /// \param MemVT Memory value type of the access.
  /// \param Alignment Required or preferred alignment.
  /// \param AddrSpace Address space of the memory access.
  /// \param ExtType Load extension kind.
  /// \param Atomic Whether the access is atomic.
  bool isLoadLegal(EVT ValVT, EVT MemVT, Align Alignment, unsigned AddrSpace,
                   unsigned ExtType, bool Atomic) const {
    return getLoadAction(ValVT, MemVT, Alignment, AddrSpace, ExtType, Atomic) ==
           Legal;
  }

  /// Return true if the specified load with extension is legal or custom
  /// on this target.
  ///
  /// @return True if the specified load with extension is legal or custom on this target.
  ///
  /// \param ValVT Value type determining boolean contents.
  /// \param MemVT Memory value type of the access.
  /// \param Alignment Required or preferred alignment.
  /// \param AddrSpace Address space of the memory access.
  /// \param ExtType Load extension kind.
  /// \param Atomic Whether the access is atomic.
  bool isLoadLegalOrCustom(EVT ValVT, EVT MemVT, Align Alignment,
                           unsigned AddrSpace, unsigned ExtType,
                           bool Atomic) const {
    LegalizeAction Action =
        getLoadAction(ValVT, MemVT, Alignment, AddrSpace, ExtType, Atomic);
    return Action == Legal || Action == Custom;
  }

  /// Return the custom action to use for a truncating store when the coarse
  /// lookup yields Custom.
  ///
  /// Returns an alternative action to use when the coarser lookups (configured
  /// through `setTruncStoreAction` yield `LegalizeAction::Custom`. Allows
  /// targets to use builtin behaviors (e.g. Legal, Promote) specialized by
  /// Alignment and AddrSpace, rather than just types.
  ///
  /// @return The custom action to use for a truncating store when the coarse lookup yields Custom.
  ///
  /// \param ValVT Value type determining boolean contents.
  /// \param MemVT Memory value type of the access.
  /// \param Alignment Required or preferred alignment.
  /// \param AddrSpace Address space of the memory access.
  virtual LegalizeAction getCustomTruncStoreAction(EVT ValVT, EVT MemVT,
                                                   Align Alignment,
                                                   unsigned AddrSpace) const {
    return LegalizeAction::Custom;
  }

  /// Return how a truncating store should be treated for the given value and
  /// memory types.
  ///
  /// Return how this store with truncation should be treated: either it is
  /// legal, needs to be promoted to a larger size, needs to be expanded to some
  /// other code sequence, or the target has a custom expander for it.
  ///
  /// @return How a truncating store should be treated for the given value and memory types.
  ///
  /// \param ValVT Value type determining boolean contents.
  /// \param MemVT Memory value type of the access.
  /// \param Alignment Required or preferred alignment.
  /// \param AddrSpace Address space of the memory access.
  LegalizeAction getTruncStoreAction(EVT ValVT, EVT MemVT, Align Alignment,
                                     unsigned AddrSpace) const {
    if (ValVT.isExtended() || MemVT.isExtended())
      return Expand;
    unsigned ValI = (unsigned)ValVT.getSimpleVT().SimpleTy;
    unsigned MemI = (unsigned)MemVT.getSimpleVT().SimpleTy;
    assert(ValI < MVT::VALUETYPE_SIZE && MemI < MVT::VALUETYPE_SIZE &&
           "Table isn't big enough!");

    LegalizeAction Action = TruncStoreActions[ValI][MemI];

    if (Action == LegalizeAction::Custom) {
      return getCustomTruncStoreAction(ValVT, MemVT, Alignment, AddrSpace);
    }

    return Action;
  }

  /// Return true if the specified store with truncation is legal on this
  /// target.
  ///
  /// @return True if the specified store with truncation is legal on this target.
  ///
  /// \param ValVT Value type determining boolean contents.
  /// \param MemVT Memory value type of the access.
  /// \param Alignment Required or preferred alignment.
  /// \param AddrSpace Address space of the memory access.
  bool isTruncStoreLegal(EVT ValVT, EVT MemVT, Align Alignment,
                         unsigned AddrSpace) const {
    return isTypeLegal(ValVT) &&
           getTruncStoreAction(ValVT, MemVT, Alignment, AddrSpace) == Legal;
  }

  /// Return true if the specified store with truncation has solution on this
  /// target.
  ///
  /// @return True if the specified store with truncation has solution on this target.
  ///
  /// \param ValVT Value type determining boolean contents.
  /// \param MemVT Memory value type of the access.
  /// \param Alignment Required or preferred alignment.
  /// \param AddrSpace Address space of the memory access.
  bool isTruncStoreLegalOrCustom(EVT ValVT, EVT MemVT, Align Alignment,
                                 unsigned AddrSpace) const {
    if (!isTypeLegal(ValVT))
      return false;

    LegalizeAction Action =
        getTruncStoreAction(ValVT, MemVT, Alignment, AddrSpace);
    return (Action == Legal || Action == Custom);
  }

  /// Return true if a truncating store of the given types can be combined,
  /// optionally requiring it to be legal.
  ///
  /// @return True if a truncating store of the given types can be combined, optionally requiring it to be legal.
  ///
  /// \param ValVT Value type determining boolean contents.
  /// \param MemVT Memory value type of the access.
  /// \param Alignment Required or preferred alignment.
  /// \param AddrSpace Address space of the memory access.
  /// \param LegalOnly If true, only treat Legal as acceptable.
  virtual bool canCombineTruncStore(EVT ValVT, EVT MemVT, Align Alignment,
                                    unsigned AddrSpace, bool LegalOnly) const {
    if (LegalOnly)
      return isTruncStoreLegal(ValVT, MemVT, Alignment, AddrSpace);

    return isTruncStoreLegalOrCustom(ValVT, MemVT, Alignment, AddrSpace);
  }

  /// Return how the given indexed load addressing mode should be treated for the
  /// given type.
  ///
  /// Return how the indexed load should be treated: either it is legal, needs to
  /// be promoted to a larger size, needs to be expanded to some other code
  /// sequence, or the target has a custom expander for it.
  ///
  /// @return How the given indexed load addressing mode should be treated for the given type.
  ///
  /// \param IdxMode Indexed addressing mode.
  /// \param VT Value type being queried or transformed.
  LegalizeAction getIndexedLoadAction(unsigned IdxMode, MVT VT) const {
    return getIndexedModeAction(IdxMode, VT, IMAB_Load);
  }

  /// Return true if the specified indexed load is legal on this target.
  ///
  /// @return True if the specified indexed load is legal on this target.
  ///
  /// \param IdxMode Indexed addressing mode.
  /// \param VT Value type being queried or transformed.
  bool isIndexedLoadLegal(unsigned IdxMode, EVT VT) const {
    return VT.isSimple() &&
      (getIndexedLoadAction(IdxMode, VT.getSimpleVT()) == Legal ||
       getIndexedLoadAction(IdxMode, VT.getSimpleVT()) == Custom);
  }

  /// Return how the given indexed store addressing mode should be treated for
  /// the given type.
  ///
  /// Return how the indexed store should be treated: either it is legal, needs
  /// to be promoted to a larger size, needs to be expanded to some other code
  /// sequence, or the target has a custom expander for it.
  ///
  /// @return How the given indexed store addressing mode should be treated for the given type.
  ///
  /// \param IdxMode Indexed addressing mode.
  /// \param VT Value type being queried or transformed.
  LegalizeAction getIndexedStoreAction(unsigned IdxMode, MVT VT) const {
    return getIndexedModeAction(IdxMode, VT, IMAB_Store);
  }

  /// Return true if the specified indexed load is legal on this target.
  ///
  /// @return True if the specified indexed load is legal on this target.
  ///
  /// \param IdxMode Indexed addressing mode.
  /// \param VT Value type being queried or transformed.
  bool isIndexedStoreLegal(unsigned IdxMode, EVT VT) const {
    return VT.isSimple() &&
      (getIndexedStoreAction(IdxMode, VT.getSimpleVT()) == Legal ||
       getIndexedStoreAction(IdxMode, VT.getSimpleVT()) == Custom);
  }

  /// Return how the given indexed masked load addressing mode should be treated
  /// for the given type.
  ///
  /// Return how the indexed load should be treated: either it is legal, needs to
  /// be promoted to a larger size, needs to be expanded to some other code
  /// sequence, or the target has a custom expander for it.
  ///
  /// @return How the given indexed masked load addressing mode should be treated for the given type.
  ///
  /// \param IdxMode Indexed addressing mode.
  /// \param VT Value type being queried or transformed.
  LegalizeAction getIndexedMaskedLoadAction(unsigned IdxMode, MVT VT) const {
    return getIndexedModeAction(IdxMode, VT, IMAB_MaskedLoad);
  }

  /// Return true if the specified indexed load is legal on this target.
  ///
  /// @return True if the specified indexed load is legal on this target.
  ///
  /// \param IdxMode Indexed addressing mode.
  /// \param VT Value type being queried or transformed.
  bool isIndexedMaskedLoadLegal(unsigned IdxMode, EVT VT) const {
    return VT.isSimple() &&
           (getIndexedMaskedLoadAction(IdxMode, VT.getSimpleVT()) == Legal ||
            getIndexedMaskedLoadAction(IdxMode, VT.getSimpleVT()) == Custom);
  }

  /// Return how the given indexed masked store addressing mode should be treated
  /// for the given type.
  ///
  /// Return how the indexed store should be treated: either it is legal, needs
  /// to be promoted to a larger size, needs to be expanded to some other code
  /// sequence, or the target has a custom expander for it.
  ///
  /// @return How the given indexed masked store addressing mode should be treated for the given type.
  ///
  /// \param IdxMode Indexed addressing mode.
  /// \param VT Value type being queried or transformed.
  LegalizeAction getIndexedMaskedStoreAction(unsigned IdxMode, MVT VT) const {
    return getIndexedModeAction(IdxMode, VT, IMAB_MaskedStore);
  }

  /// Return true if the specified indexed load is legal on this target.
  ///
  /// @return True if the specified indexed load is legal on this target.
  ///
  /// \param IdxMode Indexed addressing mode.
  /// \param VT Value type being queried or transformed.
  bool isIndexedMaskedStoreLegal(unsigned IdxMode, EVT VT) const {
    return VT.isSimple() &&
           (getIndexedMaskedStoreAction(IdxMode, VT.getSimpleVT()) == Legal ||
            getIndexedMaskedStoreAction(IdxMode, VT.getSimpleVT()) == Custom);
  }

  /// Returns true if the index type for a masked gather/scatter requires
  /// extending
  ///
  /// @return True if the index type for a masked gather/scatter requires extending.
  ///
  /// \param VT Value type being queried or transformed.
  /// \param EltTy Element type after extension.
  virtual bool shouldExtendGSIndex(EVT VT, EVT &EltTy) const { return false; }

  /// Return true if an extend can be folded into the index operand of a masked
  /// gather or scatter.
  ///
  /// @return True if an extend can be folded into the index operand of a masked gather or scatter.
  ///
  /// \param Extend Extend opcode or kind being considered.
  /// \param DataVT Stored data VT.
  virtual bool shouldRemoveExtendFromGSIndex(SDValue Extend, EVT DataVT) const {
    return false;
  }

  /// Return true if the target supports scaling gather/scatter indices by the
  /// given amount.
  ///
  /// @return True if the target supports scaling gather/scatter indices by the given amount.
  ///
  /// \param Scale Fixed-point scale or addressing scale.
  /// \param ElemSize Element size in bits or bytes, as required by the hook.
  virtual bool isLegalScaleForGatherScatter(uint64_t Scale,
                                            uint64_t ElemSize) const {
    // MGATHER/MSCATTER are only required to support scaling by one or by the
    // element size.
    if (Scale != ElemSize && Scale != 1)
      return false;
    return true;
  }

  /// Return how the given condition code should be treated for comparisons of
  /// the given type.
  ///
  /// Return how the condition code should be treated: either it is legal, needs
  /// to be expanded to some other code sequence, or the target has a custom
  /// expander for it.
  ///
  /// @return How the given condition code should be treated for comparisons of the given type.
  ///
  /// \param CC Compare constant or calling convention.
  /// \param VT Value type being queried or transformed.
  LegalizeAction
  getCondCodeAction(ISD::CondCode CC, MVT VT) const {
    assert((unsigned)CC < std::size(CondCodeActions) &&
           ((unsigned)VT.SimpleTy >> 3) < std::size(CondCodeActions[0]) &&
           "Table isn't big enough!");
    // See setCondCodeAction for how this is encoded.
    uint32_t Shift = 4 * (VT.SimpleTy & 0x7);
    uint32_t Value = CondCodeActions[CC][VT.SimpleTy >> 3];
    LegalizeAction Action = (LegalizeAction) ((Value >> Shift) & 0xF);
    assert(Action != Promote && "Can't promote condition code!");
    return Action;
  }

  /// Return true if the specified condition code is legal for a comparison of
  /// the specified types on this target.
  ///
  /// @return True if the specified condition code is legal for a comparison of the specified types on this target.
  ///
  /// \param CC Compare constant or calling convention.
  /// \param VT Value type being queried or transformed.
  bool isCondCodeLegal(ISD::CondCode CC, MVT VT) const {
    return getCondCodeAction(CC, VT) == Legal;
  }

  /// Return true if the specified condition code is legal or custom for a
  /// comparison of the specified types on this target.
  ///
  /// @return True if the specified condition code is legal or custom for a comparison of the specified types on this target.
  ///
  /// \param CC Compare constant or calling convention.
  /// \param VT Value type being queried or transformed.
  bool isCondCodeLegalOrCustom(ISD::CondCode CC, MVT VT) const {
    return getCondCodeAction(CC, VT) == Legal ||
           getCondCodeAction(CC, VT) == Custom;
  }

  /// Return how a partial-reduce multiply-accumulate node with the given
  /// accumulator and input types should be treated.
  ///
  /// Return how a PARTIAL_REDUCE_U/SMLA node with Acc type AccVT and Input type
  /// InputVT should be treated. Either it's legal, needs to be promoted to a
  /// larger size, needs to be expanded to some other code sequence, or the
  /// target has a custom expander for it.
  ///
  /// @return How a partial-reduce multiply-accumulate node with the given accumulator and input types should be treated.
  ///
  /// \param Opc Operation opcode.
  /// \param AccVT Accumulator value type.
  /// \param InputVT Input value type.
  LegalizeAction getPartialReduceMLAAction(unsigned Opc, EVT AccVT,
                                           EVT InputVT) const {
    assert(Opc == ISD::PARTIAL_REDUCE_SMLA || Opc == ISD::PARTIAL_REDUCE_UMLA ||
           Opc == ISD::PARTIAL_REDUCE_SUMLA || Opc == ISD::PARTIAL_REDUCE_FMLA);
    PartialReduceActionTypes Key = {Opc, AccVT.getSimpleVT().SimpleTy,
                                    InputVT.getSimpleVT().SimpleTy};
    auto It = PartialReduceMLAActions.find(Key);
    return It != PartialReduceMLAActions.end() ? It->second : Expand;
  }

  /// Return true if a PARTIAL_REDUCE_U/SMLA node with the specified types is
  /// legal or custom for this target.
  ///
  /// @return True if a PARTIAL_REDUCE_U/SMLA node with the specified types is legal or custom for this target.
  ///
  /// \param Opc Operation opcode.
  /// \param AccVT Accumulator value type.
  /// \param InputVT Input value type.
  bool isPartialReduceMLALegalOrCustom(unsigned Opc, EVT AccVT,
                                       EVT InputVT) const {
    LegalizeAction Action = getPartialReduceMLAAction(Opc, AccVT, InputVT);
    return Action == Legal || Action == Custom;
  }

  /// If the action for this operation is to promote, this method returns the
  /// ValueType to promote to.
  ///
  /// @return If the action for this operation is to promote, this method returns the ValueType to promote to.
  ///
  /// \param Op SDNode or value being queried.
  /// \param VT Value type being queried or transformed.
  MVT getTypeToPromoteTo(unsigned Op, MVT VT) const {
    assert(getOperationAction(Op, VT) == Promote &&
           "This operation isn't promoted!");

    // See if this has an explicit type specified.
    std::map<std::pair<unsigned, MVT::SimpleValueType>,
             MVT::SimpleValueType>::const_iterator PTTI =
      PromoteToType.find(std::make_pair(Op, VT.SimpleTy));
    if (PTTI != PromoteToType.end()) return PTTI->second;

    assert((VT.isInteger() || VT.isFloatingPoint()) &&
           "Cannot autopromote this type, add it with AddPromotedToType.");

    uint64_t VTBits = VT.getScalarSizeInBits();
    MVT NVT = VT;
    do {
      NVT = (MVT::SimpleValueType)(NVT.SimpleTy+1);
      assert(NVT.isInteger() == VT.isInteger() &&
             NVT.isFloatingPoint() == VT.isFloatingPoint() &&
             "Didn't find type to promote to!");
    } while (VTBits >= NVT.getScalarSizeInBits() || !isTypeLegal(NVT) ||
             getOperationAction(Op, NVT) == Promote);
    return NVT;
  }

  /// Return the EVT to use for an inline asm operand of the given IR type.
  ///
  /// @return The EVT to use for an inline asm operand of the given IR type.
  ///
  /// \param DL Debug location or data layout, depending on context.
  /// \param Ty Type of the constant or operation.
  /// \param AllowUnknown Whether unknown types may map to Other.
  virtual EVT getAsmOperandValueType(const DataLayout &DL, Type *Ty,
                                     bool AllowUnknown = false) const {
    return getValueType(DL, Ty, AllowUnknown);
  }

  /// Return the EVT corresponding to the given LLVM IR type.
  ///
  /// Return the EVT corresponding to this LLVM type. This is fixed by the LLVM
  /// operations except for the pointer size. If AllowUnknown is true, this will
  /// return MVT::Other for types with no EVT counterpart (e.g. structs),
  /// otherwise it will assert.
  ///
  /// @return The EVT corresponding to the given LLVM IR type.
  ///
  /// \param DL Debug location or data layout, depending on context.
  /// \param Ty Type of the constant or operation.
  /// \param AllowUnknown Whether unknown types may map to Other.
  EVT getValueType(const DataLayout &DL, Type *Ty,
                   bool AllowUnknown = false) const {
    // Lower scalar pointers to native pointer types.
    if (auto *PTy = dyn_cast<PointerType>(Ty))
      return getPointerTy(DL, PTy->getAddressSpace());

    if (auto *VTy = dyn_cast<VectorType>(Ty)) {
      Type *EltTy = VTy->getElementType();
      // Lower vectors of pointers to native pointer types.
      EVT EltVT;
      if (auto *PTy = dyn_cast<PointerType>(EltTy))
        EltVT = getPointerTy(DL, PTy->getAddressSpace());
      else
        EltVT = EVT::getEVT(EltTy, false);
      return EVT::getVectorVT(Ty->getContext(), EltVT, VTy->getElementCount());
    }

    return EVT::getEVT(Ty, AllowUnknown);
  }

  /// Return the in-memory EVT corresponding to the given LLVM IR type.
  ///
  /// @return The in-memory EVT corresponding to the given LLVM IR type.
  ///
  /// \param DL Debug location or data layout, depending on context.
  /// \param Ty Type of the constant or operation.
  /// \param AllowUnknown Whether unknown types may map to Other.
  EVT getMemValueType(const DataLayout &DL, Type *Ty,
                      bool AllowUnknown = false) const {
    // Lower scalar pointers to native pointer types.
    if (auto *PTy = dyn_cast<PointerType>(Ty))
      return getPointerMemTy(DL, PTy->getAddressSpace());

    if (auto *VTy = dyn_cast<VectorType>(Ty)) {
      Type *EltTy = VTy->getElementType();
      EVT EltVT;
      if (auto *PTy = dyn_cast<PointerType>(EltTy))
        EltVT = getPointerMemTy(DL, PTy->getAddressSpace());
      else
        EltVT = EVT::getEVT(EltTy, false);
      return EVT::getVectorVT(Ty->getContext(), EltVT, VTy->getElementCount());
    }

    return getValueType(DL, Ty, AllowUnknown);
  }


  /// Return the MVT corresponding to this LLVM type. See getValueType.
  ///
  /// @return The MVT corresponding to this LLVM type. See getValueType.
  ///
  /// \param DL Debug location or data layout, depending on context.
  /// \param Ty Type of the constant or operation.
  /// \param AllowUnknown Whether unknown types may map to Other.
  MVT getSimpleValueType(const DataLayout &DL, Type *Ty,
                         bool AllowUnknown = false) const {
    return getValueType(DL, Ty, AllowUnknown).getSimpleVT();
  }

  /// Returns the desired alignment for ByVal or InAlloca aggregate function
  /// arguments in the caller parameter area.
  ///
  /// @return The desired alignment for ByVal or InAlloca aggregate function arguments in the caller parameter area.
  ///
  /// \param Ty Type of the constant or operation.
  /// \param DL Debug location or data layout, depending on context.
  virtual Align getByValTypeAlignment(Type *Ty, const DataLayout &DL) const;

  /// Return the type of registers that this ValueType will eventually require.
  ///
  /// @return The type of registers that this ValueType will eventually require.
  ///
  /// \param Context LLVMContext used for type creation.
  /// \param VT Value type being queried or transformed.
  MVT getRegisterType(LLVMContext &Context, EVT VT) const {
    return getRegisterTypeImpl(Context, VT, /*ForCallingConv=*/false);
  }

  /// Return the number of registers that this ValueType will eventually
  /// require.
  ///
  /// This is one for any types promoted to live in larger registers, but may be
  /// more than one for types (like i64) that are split into pieces.  For types
  /// like i140, which are first promoted then expanded, it is the number of
  /// registers needed to hold all the bits of the original type.  For an i140
  /// on a 32 bit machine this means 5 registers.
  ///
  /// RegisterVT may be passed as a way to override the default settings, for
  /// instance with i128 inline assembly operands on SystemZ.
  ///
  /// @return The number of registers that this ValueType will eventually require.
  ///
  /// \param Context LLVMContext used for type creation.
  /// \param VT Value type being queried or transformed.
  /// \param RegisterVT Optional overriding register VT.
  virtual unsigned
  getNumRegisters(LLVMContext &Context, EVT VT,
                  std::optional<MVT> RegisterVT = std::nullopt) const {
    return getNumRegistersImpl(Context, VT, /*ForCallingConv=*/false);
  }

  /// Return the register type to use for a value of this type under the given
  /// calling convention.
  ///
  /// Certain combinations of ABIs, Targets and features require that types are
  /// legal for some operations and not for other operations. For MIPS all vector
  /// types must be passed through the integer register set.
  ///
  /// @return The register type to use for a value of this type under the given calling convention.
  ///
  /// \param Context LLVMContext used for type creation.
  /// \param CC Compare constant or calling convention.
  /// \param VT Value type being queried or transformed.
  virtual MVT getRegisterTypeForCallingConv(LLVMContext &Context,
                                            CallingConv::ID CC, EVT VT) const {
    return getRegisterTypeImpl(Context, VT, /*ForCallingConv=*/true);
  }

  /// Return the number of registers needed to pass a value of this type under
  /// the given calling convention.
  ///
  /// Certain targets require unusual breakdowns of certain types. For MIPS, this
  /// occurs when a vector type is used, as vector are passed through the integer
  /// register set.
  ///
  /// @return The number of registers needed to pass a value of this type under the given calling convention.
  ///
  /// \param Context LLVMContext used for type creation.
  /// \param CC Compare constant or calling convention.
  /// \param VT Value type being queried or transformed.
  virtual unsigned getNumRegistersForCallingConv(LLVMContext &Context,
                                                 CallingConv::ID CC,
                                                 EVT VT) const {
    return getNumRegistersImpl(Context, VT, /*ForCallingConv=*/true);
  }

  /// Certain targets have context sensitive alignment requirements, where one
  /// type has the alignment requirement of another type.
  ///
  /// @return The requested alignment.
  ///
  /// \param ArgTy Argument type whose ABI alignment is requested.
  /// \param DL Debug location or data layout, depending on context.
  virtual Align getABIAlignmentForCallingConv(Type *ArgTy,
                                              const DataLayout &DL) const {
    return DL.getABITypeAlign(ArgTy);
  }

  /// Return true if the FP constant of the given type should be shrunk to a
  /// smaller type.
  ///
  /// If true, then instruction selection should seek to shrink the FP constant
  /// of the specified type to a smaller type in order to save space and / or
  /// reduce runtime.
  ///
  /// @return True if the FP constant of the given type should be shrunk to a smaller type.
  ///
  /// \param VT Value type being queried or transformed.
  virtual bool ShouldShrinkFPConstant(EVT VT) const { return true; }

  /// Return true if narrowing a load to a smaller type is profitable.
  ///
  /// Return true if it is profitable to reduce a load to a smaller type.
  ///
  /// \p ByteOffset is only set if we know the pointer offset at compile time
  /// otherwise we should assume that additional pointer math is required.
  /// Example: (i16 (trunc (i32 (load x))) -> i16 load x
  /// Example: (i16 (trunc (srl (i32 (load x)), 16)) -> i16 load x+2
  ///
  /// @return True if narrowing a load to a smaller type is profitable.
  ///
  /// \param Load Load instruction or SDNode being queried.
  /// \param ExtTy Extension type of the narrowed load.
  /// \param NewVT Proposed narrower load type.
  /// \param ByteOffset Optional known byte offset of the pointer.
  virtual bool shouldReduceLoadWidth(
      SDNode *Load, ISD::LoadExtType ExtTy, EVT NewVT,
      std::optional<unsigned> ByteOffset = std::nullopt) const {
    // By default, assume that it is cheaper to extract a subvector from a wide
    // vector load rather than creating multiple narrow vector loads.
    if (NewVT.isVector() && !SDValue(Load, 0).hasOneUse())
      return false;

    return true;
  }

  /// Return true (the default) if it is profitable to remove a sext_inreg(x)
  /// where the sext is redundant, and use x directly.
  ///
  /// @return True (the default) if it is profitable to remove a sext_inreg(x) where the sext is redundant, and use x directly.
  ///
  /// \param Op SDNode or value being queried.
  virtual bool shouldRemoveRedundantExtend(SDValue Op) const { return true; }

  /// Indicates if any padding is guaranteed to go at the most significant bits
  /// when storing the type to memory and the type size isn't equal to the store
  /// size.
  ///
  /// @return True if indicates if any padding is guaranteed to go at the most significant bits when storing the type to memory and the type size isn't equal to the store size.
  ///
  /// \param VT Value type being queried or transformed.
  bool isPaddedAtMostSignificantBitsWhenStored(EVT VT) const {
    return VT.isScalarInteger() && !VT.isByteSized();
  }

  /// Return true if the high part comes first when splitting a value of this
  /// type into parts.
  ///
  /// When splitting a value of the specified type into parts, does the Lo or Hi
  /// part come first? This usually follows the endianness, except for ppcf128,
  /// where the Hi part always comes first.
  ///
  /// @return True if the high part comes first when splitting a value of this type into parts.
  ///
  /// \param VT Value type being queried or transformed.
  /// \param DL Debug location or data layout, depending on context.
  bool hasBigEndianPartOrdering(EVT VT, const DataLayout &DL) const {
    return DL.isBigEndian() || VT == MVT::ppcf128;
  }

  /// If true, the target has custom DAG combine transformations that it can
  /// perform for the specified node.
  ///
  /// @return True if if true, the target has custom DAG combine transformations that it can perform for the specified node.
  ///
  /// \param NT ISD node type.
  bool hasTargetDAGCombine(ISD::NodeType NT) const {
    assert(unsigned(NT >> 3) < std::size(TargetDAGCombineArray));
    return TargetDAGCombineArray[NT >> 3] & (1 << (NT&7));
  }

  /// Return the maximum alias-chain search depth used by GatherAllAliases.
  ///
  /// @return The maximum alias-chain search depth used by GatherAllAliases.
  unsigned getGatherAllAliasesMaxDepth() const {
    return GatherAllAliasesMaxDepth;
  }

  /// Returns the size of the platform's va_list object.
  ///
  /// @return The size of the platform's va_list object.
  ///
  /// \param DL Debug location or data layout, depending on context.
  virtual unsigned getVaListSizeInBits(const DataLayout &DL) const {
    return getPointerTy(DL).getSizeInBits();
  }

  /// Get maximum # of store operations permitted for llvm.memset
  ///
  /// This function returns the maximum number of store operations permitted
  /// to replace a call to llvm.memset. The value is set by the target at the
  /// performance threshold for such a replacement. If OptSize is true,
  /// return the limit for functions that have OptSize attribute.
  ///
  /// @return Get maximum # of store operations permitted for llvm.memset.
  ///
  /// \param OptSize Whether optimizing for code size.
  unsigned getMaxStoresPerMemset(bool OptSize) const;

  /// Get maximum # of store operations permitted for llvm.memcpy
  ///
  /// This function returns the maximum number of store operations permitted
  /// to replace a call to llvm.memcpy. The value is set by the target at the
  /// performance threshold for such a replacement. If OptSize is true,
  /// return the limit for functions that have OptSize attribute.
  ///
  /// @return Get maximum # of store operations permitted for llvm.memcpy.
  ///
  /// \param OptSize Whether optimizing for code size.
  unsigned getMaxStoresPerMemcpy(bool OptSize) const;

  /// \brief Get maximum # of store operations to be glued together
  ///
  /// This function returns the maximum number of store operations permitted
  /// to glue together during lowering of llvm.memcpy. The value is set by
  ///
  /// @return The computed result.
  virtual unsigned getMaxGluedStoresPerMemcpy() const {
    return MaxGluedStoresPerMemcpy;
  }

  /// Get maximum # of load operations permitted for memcmp
  ///
  /// This function returns the maximum number of load operations permitted
  /// to replace a call to memcmp. The value is set by the target at the
  /// performance threshold for such a replacement. If OptSize is true,
  /// return the limit for functions that have OptSize attribute.
  ///
  /// @return Get maximum # of load operations permitted for memcmp.
  ///
  /// \param OptSize Whether optimizing for code size.
  unsigned getMaxExpandSizeMemcmp(bool OptSize) const {
    return OptSize ? MaxLoadsPerMemcmpOptSize : MaxLoadsPerMemcmp;
  }

  /// Get maximum # of store operations permitted for llvm.memmove
  ///
  /// This function returns the maximum number of store operations permitted
  /// to replace a call to llvm.memmove. The value is set by the target at the
  /// performance threshold for such a replacement. If OptSize is true,
  /// return the limit for functions that have OptSize attribute.
  ///
  /// @return Get maximum # of store operations permitted for llvm.memmove.
  ///
  /// \param OptSize Whether optimizing for code size.
  unsigned getMaxStoresPerMemmove(bool OptSize) const;

  /// Determine if the target supports unaligned memory accesses.
  ///
  /// This function returns true if the target allows unaligned memory accesses
  /// of the specified type in the given address space. If true, it also returns
  /// a relative speed of the unaligned memory access in the last argument by
  /// reference. The higher the speed number the faster the operation comparing
  /// to a number returned by another such call. This is used, for example, in
  /// situations where an array copy/move/set is converted to a sequence of
  /// store operations. Its use helps to ensure that such replacements don't
  /// generate code that causes an alignment error (trap) on the target machine.
  ///
  /// @return True if determine if the target supports unaligned memory accesses.
  ///
  /// \param VT Value type being queried or transformed.
  /// \param AddrSpace Address space of the memory access.
  /// \param Alignment Required or preferred alignment.
  /// \param Flags Flags controlling the operation.
  /// \param Fast Optional relative access-speed out-parameter.
  virtual bool allowsMisalignedMemoryAccesses(
      EVT VT, unsigned AddrSpace = 0, Align Alignment = Align(1),
      MachineMemOperand::Flags Flags = MachineMemOperand::MONone,
      unsigned *Fast = nullptr) const {
    return false;
  }

  /// LLT handling variant.
  ///
  /// @return True if lLT handling variant.
  ///
  /// \param Ty Type of the constant or operation.
  /// \param AddrSpace Address space of the memory access.
  /// \param Alignment Required or preferred alignment.
  /// \param Flags Flags controlling the operation.
  /// \param Fast Optional relative access-speed out-parameter.
  virtual bool allowsMisalignedMemoryAccesses(
      LLT Ty, unsigned AddrSpace = 0, Align Alignment = Align(1),
      MachineMemOperand::Flags Flags = MachineMemOperand::MONone,
      unsigned *Fast = nullptr) const {
    return false;
  }

  /// Return true if the memory access is aligned or the target allows the given
  /// unaligned access.
  ///
  /// This function returns true if the memory access is aligned or if the target
  /// allows this specific unaligned memory access. If the access is allowed, the
  /// optional final parameter returns a relative speed of the access (as defined
  /// by the target).
  ///
  /// @return True if the memory access is aligned or the target allows the given unaligned access.
  ///
  /// \param Context LLVMContext used for type creation.
  /// \param DL Debug location or data layout, depending on context.
  /// \param VT Value type being queried or transformed.
  /// \param AddrSpace Address space of the memory access.
  /// \param Alignment Required or preferred alignment.
  /// \param Flags Flags controlling the operation.
  /// \param Fast Optional relative access-speed out-parameter.
  bool allowsMemoryAccessForAlignment(
      LLVMContext &Context, const DataLayout &DL, EVT VT,
      unsigned AddrSpace = 0, Align Alignment = Align(1),
      MachineMemOperand::Flags Flags = MachineMemOperand::MONone,
      unsigned *Fast = nullptr) const;

  /// Return true if the memory access is aligned or the target allows the given
  /// unaligned access.
  ///
  /// Return true if the memory access of this type is aligned or if the target
  /// allows this specific unaligned access for the given MachineMemOperand. If
  /// the access is allowed, the optional final parameter returns a relative
  /// speed of the access (as defined by the target).
  ///
  /// @return True if the memory access is aligned or the target allows the given unaligned access.
  ///
  /// \param Context LLVMContext used for type creation.
  /// \param DL Debug location or data layout, depending on context.
  /// \param VT Value type being queried or transformed.
  /// \param MMO Machine memory operand.
  /// \param Fast Optional relative access-speed out-parameter.
  bool allowsMemoryAccessForAlignment(LLVMContext &Context,
                                      const DataLayout &DL, EVT VT,
                                      const MachineMemOperand &MMO,
                                      unsigned *Fast = nullptr) const;

  /// Return true if the target supports a memory access of the given type,
  /// address space, and alignment.
  ///
  /// Return true if the target supports a memory access of this type for the
  /// given address space and alignment. If the access is allowed, the optional
  /// final parameter returns the relative speed of the access (as defined by the
  /// target).
  ///
  /// @return True if the target supports a memory access of the given type, address space, and alignment.
  ///
  /// \param Context LLVMContext used for type creation.
  /// \param DL Debug location or data layout, depending on context.
  /// \param VT Value type being queried or transformed.
  /// \param AddrSpace Address space of the memory access.
  /// \param Alignment Required or preferred alignment.
  /// \param Flags Flags controlling the operation.
  /// \param Fast Optional relative access-speed out-parameter.
  virtual bool
  allowsMemoryAccess(LLVMContext &Context, const DataLayout &DL, EVT VT,
                     unsigned AddrSpace = 0, Align Alignment = Align(1),
                     MachineMemOperand::Flags Flags = MachineMemOperand::MONone,
                     unsigned *Fast = nullptr) const;

  /// Return true if the target supports a memory access of the given type,
  /// address space, and alignment.
  ///
  /// Return true if the target supports a memory access of this type for the
  /// given MachineMemOperand. If the access is allowed, the optional final
  /// parameter returns the relative access speed (as defined by the target).
  ///
  /// @return True if the target supports a memory access of the given type, address space, and alignment.
  ///
  /// \param Context LLVMContext used for type creation.
  /// \param DL Debug location or data layout, depending on context.
  /// \param VT Value type being queried or transformed.
  /// \param MMO Machine memory operand.
  /// \param Fast Optional relative access-speed out-parameter.
  bool allowsMemoryAccess(LLVMContext &Context, const DataLayout &DL, EVT VT,
                          const MachineMemOperand &MMO,
                          unsigned *Fast = nullptr) const;

  /// Return true if the target supports a memory access of the given type,
  /// address space, and alignment.
  ///
  /// LLT handling variant.
  ///
  /// @return True if the target supports a memory access of the given type, address space, and alignment.
  ///
  /// \param Context LLVMContext used for type creation.
  /// \param DL Debug location or data layout, depending on context.
  /// \param Ty Type of the constant or operation.
  /// \param MMO Machine memory operand.
  /// \param Fast Optional relative access-speed out-parameter.
  bool allowsMemoryAccess(LLVMContext &Context, const DataLayout &DL, LLT Ty,
                          const MachineMemOperand &MMO,
                          unsigned *Fast = nullptr) const;

  /// Return the optimal type to use for load/store operations when lowering
  /// memset, memcpy, or memmove.
  ///
  /// Returns the target specific optimal type for load and store operations as a
  /// result of memset, memcpy, and memmove lowering. It returns EVT::Other if
  /// the type should be determined using generic target-independent logic.
  ///
  /// @return The optimal type to use for load/store operations when lowering memset, memcpy, or memmove.
  ///
  /// \param Context LLVMContext used for type creation.
  /// \param Op Memory operation descriptor.
  /// \param FuncAttributes Function attributes affecting lowering.
  virtual EVT
  getOptimalMemOpType(LLVMContext &Context, const MemOp &Op,
                      const AttributeList &FuncAttributes) const {
    return MVT::Other;
  }

  /// Return the optimal LLT for load/store operations when lowering mem* ops.
  ///
  /// @return The optimal LLT for load/store operations when lowering mem* ops.
  ///
  /// \param Op Memory operation descriptor.
  /// \param FuncAttributes Function attributes affecting lowering.
  virtual LLT
  getOptimalMemOpLLT(const MemOp &Op,
                     const AttributeList &FuncAttributes) const {
    return LLT();
  }

  /// Returns true if it's safe to use load / store of the specified type to
  /// expand memcpy / memset inline.
  ///
  /// This is mostly true for all types except for some special cases. For
  /// example, on X86 targets without SSE2 f64 load / store are done with fldl /
  /// fstpl which also does type conversion. Note the specified type doesn't
  /// have to be legal as the hook is used before type legalization.
  ///
  /// @return True if it's safe to use load / store of the specified type to expand memcpy / memset inline.
  ///
  /// \param VT Value type being queried or transformed.
  virtual bool isSafeMemOpType(MVT VT) const { return true; }

  /// Return lower limit for number of blocks in a jump table.
  ///
  /// @return Lower limit for number of blocks in a jump table.
  virtual unsigned getMinimumJumpTableEntries() const;

  /// Return lower limit of the density in a jump table.
  ///
  /// @return Lower limit of the density in a jump table.
  ///
  /// \param OptForSize Optimize the negation for size.
  unsigned getMinimumJumpTableDensity(bool OptForSize) const;

  /// Return upper limit for number of entries in a jump table.
  /// Zero if no limit.
  ///
  /// @return Upper limit for number of entries in a jump table. Zero if no limit.
  unsigned getMaximumJumpTableSize() const;

  /// Return true if jump table entries are relative to the table's address.
  ///
  /// @return True if jump table entries are relative to the table's address.
  virtual bool isJumpTableRelative() const;

  /// Retuen the minimum of largest number of comparisons in BitTest.
  ///
  /// @return Retuen the minimum of largest number of comparisons in BitTest.
  unsigned getMinimumBitTestCmps() const;

  /// Return maximum known-legal store size, which can be guaranteed for
  /// scalable vectors.
  ///
  /// @return Maximum known-legal store size, which can be guaranteed for scalable vectors.
  unsigned getMaximumLegalStoreInBits() const {
    return MaximumLegalStoreInBits;
  }

  /// If a physical register, this specifies the register that
  /// llvm.savestack/llvm.restorestack should save and restore.
  ///
  /// @return If a physical register, this specifies the register that llvm.savestack/llvm.restorestack should save and restore.
  Register getStackPointerRegisterToSaveRestore() const {
    return StackPointerRegisterToSaveRestore;
  }

  /// If a physical register, this returns the register that receives the
  /// exception address on entry to an EH pad.
  ///
  /// @return If a physical register, this returns the register that receives the exception address on entry to an EH pad.
  ///
  /// \param EH Exception handling type info.
  /// \param PersonalityFn Personality function of the landing pad.
  virtual Register
  getExceptionPointerRegister(ExceptionHandling EH,
                              const Constant *PersonalityFn) const {
    return Register();
  }

  /// If a physical register, this returns the register that receives the
  /// exception typeid on entry to a landing pad.
  ///
  /// @return If a physical register, this returns the register that receives the exception typeid on entry to a landing pad.
  ///
  /// \param EH Exception handling type info.
  /// \param PersonalityFn Personality function of the landing pad.
  virtual Register
  getExceptionSelectorRegister(ExceptionHandling EH,
                               const Constant *PersonalityFn) const {
    return Register();
  }

  /// Return true if the target requires catch objects to be allocated at fixed
  /// stack offsets.
  ///
  /// @return True if the target requires catch objects to be allocated at fixed stack offsets.
  virtual bool needsFixedCatchObjects() const {
    reportFatalUsageError("Funclet EH is not implemented for this target");
  }

  /// Return the minimum stack alignment of an argument.
  ///
  /// @return The minimum stack alignment of an argument.
  Align getMinStackArgumentAlignment() const {
    return MinStackArgumentAlignment;
  }

  /// Return the minimum function alignment.
  ///
  /// @return The minimum function alignment.
  Align getMinFunctionAlignment() const { return MinFunctionAlignment; }

  /// Return the preferred function alignment.
  ///
  /// @return The preferred function alignment.
  Align getPrefFunctionAlignment() const { return PrefFunctionAlignment; }

  /// Return the preferred loop alignment.
  ///
  /// @return The preferred loop alignment.
  ///
  /// \param ML Machine loop whose alignment is requested.
  virtual Align getPrefLoopAlignment(MachineLoop *ML = nullptr) const;

  /// Return the maximum amount of bytes allowed to be emitted when padding for
  /// alignment
  ///
  /// @return The maximum amount of bytes allowed to be emitted when padding for alignment.
  ///
  /// \param MBB Machine basic block.
  virtual unsigned
  getMaxPermittedBytesForAlignment(MachineBasicBlock *MBB) const;

  /// Should loops be aligned even when the function is marked OptSize (but not
  /// MinSize).
  ///
  /// @return True if should loops be aligned even when the function is marked OptSize (but not MinSize).
  virtual bool alignLoopsWithOptSize() const { return false; }

  /// Return the address of the target's standard stack protector guard, or
  /// nullptr if none.
  ///
  /// If the target has a standard location for the stack protector guard,
  /// returns the address of that location. Otherwise, returns nullptr.
  /// DEPRECATED: please override useLoadStackGuardNode and customize
  /// LOAD_STACK_GUARD, or customize \@llvm.stackguard().
  ///
  /// @return The address of the target's standard stack protector guard, or nullptr if none.
  ///
  /// \param IRB IR builder used to materialize the guard.
  /// \param Libcalls Runtime libcall information.
  virtual Value *getIRStackGuard(IRBuilderBase &IRB,
                                 const LibcallLoweringInfo &Libcalls) const;

  /// Inserts necessary declarations for SSP (stack protection) purpose.
  /// Should be used only when getIRStackGuard returns nullptr.
  /// \param M Module being modified.
  /// \param Libcalls Runtime libcall information.
  virtual void insertSSPDeclarations(Module &M,
                                     const LibcallLoweringInfo &Libcalls) const;

  /// Return the stack guard variable previously inserted by
  /// insertSSPDeclarations.
  ///
  /// Return the variable that's previously inserted by insertSSPDeclarations, if
  /// any, otherwise return nullptr. Should be used only when getIRStackGuard
  /// returns nullptr.
  ///
  /// @return The stack guard variable previously inserted by insertSSPDeclarations.
  ///
  /// \param M Module being queried.
  /// \param Libcalls Runtime libcall information.
  virtual Value *getSDagStackGuard(const Module &M,
                                   const LibcallLoweringInfo &Libcalls) const;

  /// Return true if stack protector checks should mix the frame pointer into the
  /// guard value.
  ///
  /// If this function returns true, stack protection checks should mix the frame
  /// pointer (or whichever pointer is used to address locals) into the stack
  /// guard value before checking it. getIRStackGuard must return nullptr if this
  /// returns true.
  ///
  /// @return True if stack protector checks should mix the frame pointer into the guard value.
  virtual bool useStackGuardMixFP() const { return false; }

  /// Return the target's stack protector check function, if previously inserted.
  ///
  /// If the target has a standard stack protection check function that performs
  /// validation and error handling, returns the function. Otherwise, returns
  /// nullptr. Must be previously inserted by insertSSPDeclarations. Should be
  /// used only when getIRStackGuard returns nullptr.
  ///
  /// @return The target's stack protector check function, if previously inserted.
  ///
  /// \param M Module being queried.
  /// \param Libcalls Runtime libcall information.
  Function *getSSPStackGuardCheck(const Module &M,
                                  const LibcallLoweringInfo &Libcalls) const;

protected:
  /// Return the default safe-stack pointer location.
  ///
  /// @return The default safe-stack pointer location.
  ///
  /// \param IRB IR builder used to materialize the pointer.
  /// \param UseTLS Whether to use a thread-local safe-stack pointer.
  Value *getDefaultSafeStackPointerLocation(IRBuilderBase &IRB,
                                            bool UseTLS) const;

public:
  /// Returns the target-specific address of the unsafe stack pointer.
  ///
  /// @return The target-specific address of the unsafe stack pointer.
  ///
  /// \param IRB IR builder used to materialize the guard.
  /// \param Libcalls Runtime libcall information.
  virtual Value *
  getSafeStackPointerLocation(IRBuilderBase &IRB,
                              const LibcallLoweringInfo &Libcalls) const;

  /// Returns the name of the symbol used to emit stack probes or the empty
  /// string if not applicable.
  ///
  /// @return The name of the symbol used to emit stack probes or the empty string if not applicable.
  ///
  /// \param MF Machine function being lowered.
  virtual bool hasStackProbeSymbol(const MachineFunction &MF) const { return false; }

  /// Return true if the target emits inline stack probes instead of calling a
  /// probe function.
  ///
  /// @return True if the target emits inline stack probes instead of calling a probe function.
  ///
  /// \param MF Machine function being lowered.
  virtual bool hasInlineStackProbe(const MachineFunction &MF) const { return false; }

  /// Return the name of the symbol used to emit stack probes, or empty if not
  /// applicable.
  ///
  /// @return The name of the symbol used to emit stack probes, or empty if not applicable.
  ///
  /// \param MF Machine function being lowered.
  virtual StringRef getStackProbeSymbolName(const MachineFunction &MF) const {
    return "";
  }

  /// Return true if casting a pointer from SrcAS to DestAS is free.
  ///
  /// Returns true if a cast from SrcAS to DestAS is "cheap", such that e.g. we
  /// are happy to sink it into basic blocks. A cast may be free, but not
  /// necessarily a no-op. e.g. a free truncate from a 64-bit to 32-bit pointer.
  ///
  /// @return True if casting a pointer from SrcAS to DestAS is free.
  ///
  /// \param SrcAS Source address space.
  /// \param DestAS Destination address space.
  virtual bool isFreeAddrSpaceCast(unsigned SrcAS, unsigned DestAS) const;

  /// Return true if pointer arguments to this call should be aligned by aligning
  /// the pointee object.
  ///
  /// Return true if the pointer arguments to CI should be aligned by aligning
  /// the object whose address is being passed. If so then MinSize is set to the
  /// minimum size the object must be to be aligned and PrefAlign is set to the
  /// preferred alignment.
  ///
  /// @return True if pointer arguments to this call should be aligned by aligning the pointee object.
  ///
  /// \param CI Call instruction whose pointer args may be aligned.
  /// \param MinSize Filled with the minimum object size to align.
  /// \param PrefAlign Filled with the preferred alignment.
  virtual bool shouldAlignPointerArgs(CallInst *CI, unsigned &MinSize,
                                      Align &PrefAlign) const {
    return false;
  }

  //===--------------------------------------------------------------------===//
  /// \name Helpers for TargetTransformInfo implementations
  /// @{

  /// Get the ISD node that corresponds to the Instruction class opcode.
  ///
  /// @return Get the ISD node that corresponds to the Instruction class opcode.
  ///
  /// \param Opcode ISD or target opcode.
  int InstructionOpcodeToISD(unsigned Opcode) const;

  /// Get the ISD node that corresponds to the Intrinsic ID. Returns
  /// ISD::DELETED_NODE by default for an unsupported Intrinsic ID.
  ///
  /// @return Get the ISD node that corresponds to the Intrinsic ID. Returns ISD::DELETED_NODE by default for an unsupported Intrinsic ID.
  ///
  /// \param ID Intrinsic ID to map.
  int IntrinsicIDToISD(Intrinsic::ID ID) const;

  /// @}

  //===--------------------------------------------------------------------===//
  /// \name Helpers for atomic expansion.
  /// @{

  /// Return the maximum atomic operation size in bits supported by the backend.
  ///
  /// Returns the maximum atomic operation size (in bits) supported by the
  /// backend. Atomic operations greater than this size (as well as ones that are
  /// not naturally aligned), will be expanded by AtomicExpandPass into an
  /// __atomic_* library call.
  ///
  /// @return The maximum atomic operation size in bits supported by the backend.
  unsigned getMaxAtomicSizeInBitsSupported() const {
    return MaxAtomicSizeInBitsSupported;
  }

  /// Returns the size in bits of the maximum div/rem the backend supports.
  /// Larger operations will be expanded by ExpandIRInsts.
  ///
  /// @return The size in bits of the maximum div/rem the backend supports. Larger operations will be expanded by ExpandIRInsts.
  unsigned getMaxDivRemBitWidthSupported() const {
    return MaxDivRemBitWidthSupported;
  }

  /// Returns the size in bits of the maximum fp to/from int conversion the
  /// backend supports. Larger operations will be expanded by ExpandIRInsts.
  ///
  /// @return The size in bits of the maximum fp to/from int conversion the backend supports. Larger operations will be expanded by ExpandIRInsts.
  unsigned getMaxLargeFPConvertBitWidthSupported() const {
    return MaxLargeFPConvertBitWidthSupported;
  }

  /// Returns the size of the smallest cmpxchg or ll/sc instruction
  /// the backend supports.  Any smaller operations are widened in
  /// AtomicExpandPass.
  ///
  /// Note that *unlike* operations above the maximum size, atomic ops
  /// are still natively supported below the minimum; they just
  /// require a more complex expansion.
  ///
  /// @return The size of the smallest cmpxchg or ll/sc instruction the backend supports.  Any smaller operations are widened in AtomicExpandPass.
  unsigned getMinCmpXchgSizeInBits() const { return MinCmpXchgSizeInBits; }

  /// Whether the target supports unaligned atomic operations.
  ///
  /// @return True if whether the target supports unaligned atomic operations.
  bool supportsUnalignedAtomics() const { return SupportsUnalignedAtomics; }

  /// Return true if AtomicExpandPass should insert fences and relax ordering for
  /// this atomic instruction.
  ///
  /// Whether AtomicExpandPass should automatically insert fences and reduce
  /// ordering for this atomic. This should be true for most architectures with
  /// weak memory ordering. Defaults to false.
  ///
  /// @return True if AtomicExpandPass should insert fences and relax ordering for this atomic instruction.
  ///
  /// \param I Instruction being queried.
  virtual bool shouldInsertFencesForAtomic(const Instruction *I) const {
    return false;
  }

  /// Whether AtomicExpandPass should automatically insert a seq_cst trailing
  /// fence without reducing the ordering for this atomic store. Defaults to
  /// false.
  ///
  /// @return True if whether AtomicExpandPass should automatically insert a seq_cst trailing fence without reducing the ordering for this atomic store. Defaults to false.
  ///
  /// \param I Instruction being queried.
  virtual bool
  shouldInsertTrailingSeqCstFenceForAtomicStore(const Instruction *I) const {
    return false;
  }

  /// Return the memory ordering to assign to an atomic instruction after
  /// AtomicExpandPass splits out its fences.
  ///
  /// @return The memory ordering to assign to an atomic instruction after AtomicExpandPass splits out its fences.
  ///
  /// \param I Instruction being queried.
  virtual AtomicOrdering
  atomicOperationOrderAfterFenceSplit(const Instruction *I) const {
    return AtomicOrdering::Monotonic;
  }

  /// Return true if an atomic load should be issued for the initial value before
  /// an atomicrmw/cmpxchg emulation loop.
  ///
  /// @return True if an atomic load should be issued for the initial value before an atomicrmw/cmpxchg emulation loop.
  virtual bool shouldIssueAtomicLoadForAtomicEmulationLoop(void) const {
    return true;
  }

  /// Emit a load-linked operation on the given address for an ll/sc atomic
  /// sequence.
  ///
  /// Perform a load-linked operation on Addr, returning a "Value *" with the
  /// corresponding pointee type. This may entail some non-trivial operations to
  /// truncate or reconstruct types that will be illegal in the backend. See
  /// ARMISelLowering for an example implementation.
  ///
  /// @return A pointer to the requested object, or null if unavailable.
  ///
  /// \param Builder IR builder used to emit the load-linked.
  /// \param ValueTy Pointee type of the loaded value.
  /// \param Addr Address of the memory location.
  /// \param Ord Atomic ordering.
  virtual Value *emitLoadLinked(IRBuilderBase &Builder, Type *ValueTy,
                                Value *Addr, AtomicOrdering Ord) const {
    llvm_unreachable("Load linked unimplemented on this target");
  }

  /// Perform a store-conditional operation to Addr. Return the status of the
  /// store. This should be 0 if the store succeeded, non-zero otherwise.
  ///
  /// @return A pointer to the requested object, or null if unavailable.
  ///
  /// \param Builder IR builder used to emit the store-conditional.
  /// \param Val Value to store.
  /// \param Addr Address of the memory location.
  /// \param Ord Atomic ordering.
  virtual Value *emitStoreConditional(IRBuilderBase &Builder, Value *Val,
                                      Value *Addr, AtomicOrdering Ord) const {
    llvm_unreachable("Store conditional unimplemented on this target");
  }

  /// Expand a masked atomicrmw into a target-specific ll/sc intrinsic.
  ///
  /// Perform a masked atomicrmw using a target-specific intrinsic. This
  /// represents the core LL/SC loop which will be lowered at a late stage by the
  /// backend. The target-specific intrinsic returns the loaded value and is not
  /// responsible for masking and shifting the result.
  ///
  /// @return A pointer to the requested object, or null if unavailable.
  ///
  /// \param Builder IR builder used to emit the intrinsic.
  /// \param AI Atomic RMW instruction being expanded.
  /// \param AlignedAddr Aligned address of the atomic memory location.
  /// \param Incr Increment or operand value for the RMW operation.
  /// \param Mask Bitmask selecting the relevant bits of the atomic value.
  /// \param ShiftAmt Shift amount used to position the masked bits.
  /// \param Ord Atomic ordering.
  virtual Value *emitMaskedAtomicRMWIntrinsic(IRBuilderBase &Builder,
                                              AtomicRMWInst *AI,
                                              Value *AlignedAddr, Value *Incr,
                                              Value *Mask, Value *ShiftAmt,
                                              AtomicOrdering Ord) const {
    llvm_unreachable("Masked atomicrmw expansion unimplemented on this target");
  }

  /// Expand an atomicrmw instruction using a target-specific lowering.
  ///
  /// Perform a atomicrmw expansion using a target-specific way. This is expected
  /// to be called when masked atomicrmw and bit test atomicrmw don't work, and
  /// the target supports another way to lower atomicrmw.
  ///
  /// \param AI Atomic RMW instruction.
  virtual void emitExpandAtomicRMW(AtomicRMWInst *AI) const {
    llvm_unreachable(
        "Generic atomicrmw expansion unimplemented on this target");
  }

  /// Perform a atomic store using a target-specific way.
  /// \param SI Switch or store instruction.
  virtual void emitExpandAtomicStore(StoreInst *SI) const {
    llvm_unreachable(
        "Generic atomic store expansion unimplemented on this target");
  }

  /// Perform a atomic load using a target-specific way.
  /// \param LI Load instruction.
  virtual void emitExpandAtomicLoad(LoadInst *LI) const {
    llvm_unreachable(
        "Generic atomic load expansion unimplemented on this target");
  }

  /// Perform a cmpxchg expansion using a target-specific method.
  /// \param CI Call instruction whose pointer args may be aligned.
  virtual void emitExpandAtomicCmpXchg(AtomicCmpXchgInst *CI) const {
    llvm_unreachable("Generic cmpxchg expansion unimplemented on this target");
  }

  /// Expand an atomicrmw into a target-specific bit-test intrinsic.
  ///
  /// Perform a bit test atomicrmw using a target-specific intrinsic. This
  /// represents the combined bit test intrinsic which will be lowered at a late
  /// stage by the backend.
  ///
  /// \param AI Atomic RMW instruction.
  virtual void emitBitTestAtomicRMWIntrinsic(AtomicRMWInst *AI) const {
    llvm_unreachable(
        "Bit test atomicrmw expansion unimplemented on this target");
  }

  /// Expand an atomicrmw whose result is only compared into a target-specific
  /// compare-and-arith intrinsic.
  ///
  /// Perform a atomicrmw which the result is only used by comparison, using a
  /// target-specific intrinsic. This represents the combined atomic and compare
  /// intrinsic which will be lowered at a late stage by the backend.
  ///
  /// \param AI Atomic RMW instruction.
  virtual void emitCmpArithAtomicRMWIntrinsic(AtomicRMWInst *AI) const {
    llvm_unreachable(
        "Compare arith atomicrmw expansion unimplemented on this target");
  }

  /// Expand a masked cmpxchg into a target-specific ll/sc intrinsic.
  ///
  /// Perform a masked cmpxchg using a target-specific intrinsic. This represents
  /// the core LL/SC loop which will be lowered at a late stage by the backend.
  /// The target-specific intrinsic returns the loaded value and is not
  /// responsible for masking and shifting the result.
  ///
  /// @return A pointer to the requested object, or null if unavailable.
  ///
  /// \param Builder IR builder used to emit the intrinsic.
  /// \param CI Atomic compare-and-exchange instruction being expanded.
  /// \param AlignedAddr Aligned address of the atomic memory location.
  /// \param CmpVal Expected value for the compare.
  /// \param NewVal New value to store on success.
  /// \param Mask Bitmask selecting the relevant bits of the atomic value.
  /// \param Ord Atomic ordering.
  virtual Value *emitMaskedAtomicCmpXchgIntrinsic(
      IRBuilderBase &Builder, AtomicCmpXchgInst *CI, Value *AlignedAddr,
      Value *CmpVal, Value *NewVal, Value *Mask, AtomicOrdering Ord) const {
    llvm_unreachable("Masked cmpxchg expansion unimplemented on this target");
  }

  //===--------------------------------------------------------------------===//
  /// \name KCFI check lowering.
  /// @{

  /// Emit a KCFI type-check instruction before an indirect call.
  ///
  /// @return A pointer to the requested object, or null if unavailable.
  ///
  /// \param MBB Machine basic block.
  /// \param MBBI Iterator to the call instruction.
  /// \param TII Target instruction info.
  virtual MachineInstr *EmitKCFICheck(MachineBasicBlock &MBB,
                                      MachineBasicBlock::instr_iterator &MBBI,
                                      const TargetInstrInfo *TII) const {
    llvm_unreachable("KCFI is not supported on this target");
  }

  /// @}

  /// Insert a fence before an atomic instruction, as needed for the target's
  /// memory model.
  ///
  /// Inserts in the IR a target-specific intrinsic specifying a fence. It is
  /// called by AtomicExpandPass before expanding an
  /// AtomicRMW/AtomicCmpXchg/AtomicStore/AtomicLoad if
  /// shouldInsertFencesForAtomic returns true.
  ///
  /// @{
  /// @return A pointer to the requested object, or null if unavailable.
  ///
  /// \param Builder IR builder used to emit the fence.
  /// \param Inst Atomic instruction being expanded.
  /// \param Ord Atomic ordering.
  virtual Instruction *emitLeadingFence(IRBuilderBase &Builder,
                                        Instruction *Inst,
                                        AtomicOrdering Ord) const;

  /// Emit a trailing fence after expanding an atomic operation.
  ///
  /// @return A pointer to the requested object, or null if unavailable.
  ///
  /// \param Builder IR builder used to emit the fence.
  /// \param Inst Atomic instruction being expanded.
  /// \param Ord Atomic ordering.
  virtual Instruction *emitTrailingFence(IRBuilderBase &Builder,
                                         Instruction *Inst,
                                         AtomicOrdering Ord) const;
  /// @}

  /// Emit balancing code for the load-linked when the store-conditional of a
  /// cmpxchg will not execute.
  ///
  /// \param Builder IR builder used to emit instructions.
  virtual void emitAtomicCmpXchgNoStoreLLBalance(IRBuilderBase &Builder) const {}

  /// Returns true if arguments should be sign-extended in lib calls.
  ///
  /// @return True if arguments should be sign-extended in lib calls.
  ///
  /// \param Ty Type of the constant or operation.
  /// \param IsSigned Whether operands should be treated as signed.
  virtual bool shouldSignExtendTypeInLibCall(Type *Ty, bool IsSigned) const {
    return IsSigned;
  }

  /// Returns true if arguments should be extended in lib calls.
  ///
  /// @return True if arguments should be extended in lib calls.
  ///
  /// \param Type Type considered for libcall extension.
  virtual bool shouldExtendTypeInLibCall(EVT Type) const {
    return true;
  }

  /// Returns how the given (atomic) load should be expanded by the
  /// IR-level AtomicExpand pass.
  ///
  /// @return How the given (atomic) load should be expanded by the IR-level AtomicExpand pass.
  ///
  /// \param LI Load instruction.
  virtual AtomicExpansionKind shouldExpandAtomicLoadInIR(LoadInst *LI) const {
    return AtomicExpansionKind::None;
  }

  /// Returns how the given (atomic) load should be cast by the IR-level
  /// AtomicExpand pass.
  ///
  /// @return How the given (atomic) load should be cast by the IR-level AtomicExpand pass.
  ///
  /// \param LI Load instruction.
  virtual AtomicExpansionKind shouldCastAtomicLoadInIR(LoadInst *LI) const {
    if (LI->getType()->isFloatingPointTy())
      return AtomicExpansionKind::CastToInteger;
    return AtomicExpansionKind::None;
  }

  /// Return how an atomic store should be expanded by the IR-level AtomicExpand
  /// pass.
  ///
  /// Returns how the given (atomic) store should be expanded by the IR-level
  /// AtomicExpand pass into. For instance AtomicExpansionKind::CustomExpand will
  /// try to use an atomicrmw xchg.
  ///
  /// @return How an atomic store should be expanded by the IR-level AtomicExpand pass.
  ///
  /// \param SI Switch or store instruction.
  virtual AtomicExpansionKind shouldExpandAtomicStoreInIR(StoreInst *SI) const {
    return AtomicExpansionKind::None;
  }

  /// Return how an atomic store should be cast by the IR-level AtomicExpand
  /// pass.
  ///
  /// Returns how the given (atomic) store should be cast by the IR-level
  /// AtomicExpand pass into. For instance AtomicExpansionKind::CastToInteger
  /// will try to cast the operands to integer values.
  ///
  /// @return How an atomic store should be cast by the IR-level AtomicExpand pass.
  ///
  /// \param SI Switch or store instruction.
  virtual AtomicExpansionKind shouldCastAtomicStoreInIR(StoreInst *SI) const {
    if (SI->getValueOperand()->getType()->isFloatingPointTy())
      return AtomicExpansionKind::CastToInteger;
    return AtomicExpansionKind::None;
  }

  /// Returns how the given atomic cmpxchg should be expanded by the IR-level
  /// AtomicExpand pass.
  ///
  /// @return How the given atomic cmpxchg should be expanded by the IR-level AtomicExpand pass.
  ///
  /// \param AI Atomic RMW instruction.
  virtual AtomicExpansionKind
  shouldExpandAtomicCmpXchgInIR(const AtomicCmpXchgInst *AI) const {
    return AtomicExpansionKind::None;
  }

  /// Returns how the IR-level AtomicExpand pass should expand the given
  /// AtomicRMW, if at all. Default is to never expand.
  ///
  /// @return How the IR-level AtomicExpand pass should expand the given AtomicRMW, if at all. Default is to never expand.
  ///
  /// \param RMW Atomic RMW instruction.
  virtual AtomicExpansionKind
  shouldExpandAtomicRMWInIR(const AtomicRMWInst *RMW) const {
    if (RMW->isFloatingPointOperation())
      return AtomicExpansionKind::CmpXChg;
    if (RMW->getType()->isVectorTy())
      return AtomicExpansionKind::CmpXChg;
    return AtomicExpansionKind::None;
  }

  /// Returns how the given atomic atomicrmw should be cast by the IR-level
  /// AtomicExpand pass.
  ///
  /// @return How the given atomic atomicrmw should be cast by the IR-level AtomicExpand pass.
  ///
  /// \param RMWI Atomic RMW instruction.
  virtual AtomicExpansionKind
  shouldCastAtomicRMWIInIR(AtomicRMWInst *RMWI) const {
    Type *ValTy = RMWI->getValOperand()->getType();
    if (RMWI->getOperation() == AtomicRMWInst::Xchg &&
        (ValTy->isFloatingPointTy() || ValTy->isPointerTy() ||
         ValTy->isVectorTy()))
      return AtomicExpansionKind::CastToInteger;

    return AtomicExpansionKind::None;
  }

  /// Try to convert an idempotent atomicrmw into a fence followed by an atomic
  /// load.
  ///
  /// On some platforms, an AtomicRMW that never actually modifies the value
  /// (such as fetch_add of 0) can be turned into a fence followed by an atomic
  /// load. This may sound useless, but it makes it possible for the processor to
  /// keep the cacheline shared, dramatically improving performance. And such
  /// idempotent RMWs are useful for implementing some kinds of locks, see for
  /// example (justification + benchmarks):
  /// http://www.hpl.hp.com/techreports/2012/HPL-2012-68.pdf This method tries
  /// doing that transformation, returning the atomic load if it succeeds, and
  /// nullptr otherwise. If shouldExpandAtomicLoadInIR returns true on that load,
  /// it will undergo another round of expansion.
  ///
  /// @return A pointer to the requested object, or null if unavailable.
  ///
  /// \param RMWI Atomic RMW instruction.
  virtual LoadInst *
  lowerIdempotentRMWIntoFencedLoad(AtomicRMWInst *RMWI) const {
    return nullptr;
  }

  /// Returns how the platform's atomic operations are extended (ZERO_EXTEND,
  /// SIGN_EXTEND, or ANY_EXTEND).
  ///
  /// @return How the platform's atomic operations are extended (ZERO_EXTEND, SIGN_EXTEND, or ANY_EXTEND).
  virtual ISD::NodeType getExtendForAtomicOps() const {
    return ISD::ZERO_EXTEND;
  }

  /// Return how the comparison operand of an atomic compare-and-swap should be
  /// extended.
  ///
  /// Returns how the platform's atomic compare and swap expects its comparison
  /// value to be extended (ZERO_EXTEND, SIGN_EXTEND, or ANY_EXTEND). This is
  /// separate from getExtendForAtomicOps, which is concerned with the
  /// sign-extension of the instruction's output, whereas here we are concerned
  /// with the sign-extension of the input. For targets with compare-and-swap
  /// instructions (or sub-word comparisons in their LL/SC loop expansions), the
  /// input can be ANY_EXTEND, but the output will still have a specific
  /// extension.
  ///
  /// @return How the comparison operand of an atomic compare-and-swap should be extended.
  virtual ISD::NodeType getExtendForAtomicCmpSwapArg() const {
    return ISD::ANY_EXTEND;
  }

  /// Returns how the platform's atomic rmw operations expect their input
  /// argument to be extended (ZERO_EXTEND, SIGN_EXTEND, or ANY_EXTEND).
  ///
  /// @return How the platform's atomic rmw operations expect their input argument to be extended (ZERO_EXTEND, SIGN_EXTEND, or ANY_EXTEND).
  ///
  /// \param Op SDNode or value being queried.
  virtual ISD::NodeType getExtendForAtomicRMWArg(unsigned Op) const {
    return ISD::ANY_EXTEND;
  }

  /// @}

  /// Return true if a select on a logical combination of conditions should be
  /// normalized to nested selects.
  ///
  /// Returns true if we should normalize select(N0&N1, X, Y) => select(N0,
  /// select(N1, X, Y), Y) and select(N0|N1, X, Y) => select(N0, select(N1, X, Y,
  /// Y)) if it is likely that it saves us from materializing N0 and N1 in an
  /// integer register. Targets that are able to perform and/or on flags should
  /// return false here.
  ///
  /// \p VT is the type of the select (and X and Y). \p CCVT is the type of its
  /// condition (N0 and N1).
  ///
  /// @return True if a select on a logical combination of conditions should be normalized to nested selects.
  ///
  /// \param Context LLVMContext used for type creation.
  /// \param VT Value type being queried or transformed.
  /// \param CCVT Condition-code value type.
  virtual bool shouldNormalizeToSelectSequence(LLVMContext &Context, EVT VT,
                                               EVT CCVT) const {
    // If a target has multiple condition registers, then it likely has logical
    // operations on those registers.
    if (hasMultipleConditionRegisters(VT))
      return false;
    // Only do the transform if the value won't be split into multiple
    // registers.
    LegalizeTypeAction Action = getTypeAction(Context, VT);
    return Action != TypeExpandInteger && Action != TypeExpandFloat &&
      Action != TypeSplitVector;
  }

  /// Return true if combining min/max-num operations is profitable for this
  /// type.
  ///
  /// @return True if combining min/max-num operations is profitable for this type.
  ///
  /// \param VT Value type being queried or transformed.
  virtual bool isProfitableToCombineMinNumMaxNum(EVT VT) const { return true; }

  /// Return true if a select between two constants should be converted into
  /// simple arithmetic on the condition.
  ///
  /// Return true if a select of constants (select Cond, C1, C2) should be
  /// transformed into simple math ops with the condition value. For example:
  /// select Cond, C1, C1-1 --> add (zext Cond), C1-1
  ///
  /// @return True if a select between two constants should be converted into simple arithmetic on the condition.
  ///
  /// \param VT Value type being queried or transformed.
  virtual bool convertSelectOfConstantsToMath(EVT VT) const {
    return false;
  }

  /// Return true if multiplying by this constant should be decomposed into
  /// shifts and adds.
  ///
  /// Return true if it is profitable to transform an integer
  /// multiplication-by-constant into simpler operations like shifts and adds.
  /// This may be true if the target does not directly support the multiplication
  /// operation for the specified type or the sequence of simpler ops is faster
  /// than the multiply.
  ///
  /// @return True if multiplying by this constant should be decomposed into shifts and adds.
  ///
  /// \param Context LLVMContext used for type creation.
  /// \param VT Value type being queried or transformed.
  /// \param C Constant value being tested.
  virtual bool decomposeMulByConstant(LLVMContext &Context,
                                      EVT VT, SDValue C) const {
    return false;
  }

  /// Return true if distributing a constant multiply over a constant add is
  /// profitable.
  ///
  /// Return true if it may be profitable to transform (mul (add x, c1), c2) ->
  /// (add (mul x, c2), c1*c2). This may not be true if c1 and c2 can be
  /// represented as immediates but c1*c2 cannot, for example. The target should
  /// check if c1, c2 and c1*c2 can be represented as immediates, or have to be
  /// materialized into registers. If it is not sure about some cases, a default
  /// true can be returned to let the DAGCombiner decide. AddNode is (add x, c1),
  /// and ConstNode is c2.
  ///
  /// @return True if distributing a constant multiply over a constant add is profitable.
  ///
  /// \param AddNode Add node in the mul-add pattern.
  /// \param ConstNode Constant node in the mul-add pattern.
  virtual bool isMulAddWithConstProfitable(SDValue AddNode,
                                           SDValue ConstNode) const {
    return true;
  }

  /// Return true if a strict, canonicalizing FP-to-int conversion should be used
  /// instead of selecting after the fact.
  ///
  /// Return true if it is more correct/profitable to use strict FP_TO_INT
  /// conversion operations - canonicalizing the FP source value instead of
  /// converting all cases and then selecting based on value. This may be true if
  /// the target throws exceptions for out of bounds conversions or has fast FP
  /// CMOV.
  ///
  /// @return True if a strict, canonicalizing FP-to-int conversion should be used instead of selecting after the fact.
  ///
  /// \param FpVT Floating-point value type.
  /// \param IntVT Integer destination value type.
  /// \param IsSigned Whether operands should be treated as signed.
  virtual bool shouldUseStrictFP_TO_INT(EVT FpVT, EVT IntVT,
                                        bool IsSigned) const {
    return false;
  }

  /// Return true if it is beneficial to expand an llvm.powi intrinsic into
  /// multiplies.
  ///
  /// Return true if it is beneficial to expand an @llvm.powi.* intrinsic. If not
  /// optimizing for size, expanding @llvm.powi.* intrinsics is always considered
  /// beneficial. If optimizing for size, expansion is only considered beneficial
  /// for upto 5 multiplies and a divide (if the exponent is negative).
  ///
  /// @return True if it is beneficial to expand an llvm.powi intrinsic into multiplies.
  ///
  /// \param Exponent Power exponent being expanded.
  /// \param OptForSize Optimize the negation for size.
  bool isBeneficialToExpandPowI(int64_t Exponent, bool OptForSize) const {
    if (Exponent < 0)
      Exponent = -Exponent;
    uint64_t E = static_cast<uint64_t>(Exponent);
    return !OptForSize || (llvm::popcount(E) + Log2_64(E) < 7);
  }

  //===--------------------------------------------------------------------===//
  // TargetLowering Configuration Methods - These methods should be invoked by
  // the derived class constructor to configure this object for the target.
  //
protected:
  /// Specify how the target extends the result of integer and floating point
  /// boolean values from i1 to a wider type.  See getBooleanContents.
  /// \param Ty Type of the constant or operation.
  void setBooleanContents(BooleanContent Ty) {
    BooleanContents = Ty;
    BooleanFloatContents = Ty;
  }

  /// Specify how the target extends the result of integer and floating point
  /// boolean values from i1 to a wider type.  See getBooleanContents.
  /// \param IntTy Boolean contents for integer compares.
  /// \param FloatTy Boolean contents for floating-point compares.
  void setBooleanContents(BooleanContent IntTy, BooleanContent FloatTy) {
    BooleanContents = IntTy;
    BooleanFloatContents = FloatTy;
  }

  /// Specify how the target extends the result of a vector boolean value from a
  /// vector of i1 to a wider type.  See getBooleanContents.
  /// \param Ty Type of the constant or operation.
  void setBooleanVectorContents(BooleanContent Ty) {
    BooleanVectorContents = Ty;
  }

  /// Specify the target scheduling preference.
  /// \param Pref Scheduling preference.
  void setSchedulingPreference(Sched::Preference Pref) {
    SchedPreferenceInfo = Pref;
  }

  /// Indicate the minimum number of blocks to generate jump tables.
  /// \param Val Minimum number of bit-test compares.
  void setMinimumJumpTableEntries(unsigned Val);

  /// Indicate the maximum number of entries in jump tables.
  /// Set to zero to generate unlimited jump tables.
  /// \param Val Minimum number of bit-test compares.
  void setMaximumJumpTableSize(unsigned Val);

  /// Set the minimum of largest of number of comparisons to generate BitTest.
  /// \param Val Minimum number of bit-test compares.
  void setMinimumBitTestCmps(unsigned Val);

  /// If set to a physical register, this specifies the register that
  /// llvm.savestack/llvm.restorestack should save and restore.
  /// \param R Register or value being queried.
  void setStackPointerRegisterToSaveRestore(Register R) {
    StackPointerRegisterToSaveRestore = R;
  }

  /// Set whether the target has bit-extract instructions.
  ///
  /// Tells the code generator that the target has BitExtract instructions. The
  /// code generator will aggressively sink "shift"s into the blocks of their
  /// users if the users will generate "and" instructions which can be combined
  /// with "shift" to BitExtract instructions.
  ///
  /// \param hasExtractInsn Whether the target has bit-extract instructions.
  void setHasExtractBitsInsn(bool hasExtractInsn = true) {
    HasExtractBitsInsn = hasExtractInsn;
  }

  /// Tells the code generator not to expand logic operations on comparison
  /// predicates into separate sequences that increase the amount of flow
  /// control.
  /// \param isExpensive Whether jumps should be considered expensive.
  void setJumpIsExpensive(bool isExpensive = true);

  /// Tells the code generator which bitwidths to bypass.
  /// \param SlowBitWidth Bit width of the slow division.
  /// \param FastBitWidth Bit width of the fast bypass division.
  void addBypassSlowDiv(unsigned int SlowBitWidth, unsigned int FastBitWidth) {
    BypassSlowDivWidths[SlowBitWidth] = FastBitWidth;
  }

  /// Add the specified register class as an available regclass for the
  /// specified value type. This indicates the selector can handle values of
  /// that class natively.
  /// \param VT Value type being queried or transformed.
  /// \param RC Register class.
  void addRegisterClass(MVT VT, const TargetRegisterClass *RC) {
    assert((unsigned)VT.SimpleTy < std::size(RegClassForVT));
    RegClassForVT[VT.SimpleTy] = RC;
  }

  /// Return the largest legal super-reg register class of the register class
  /// for the specified type and its associated "cost".
  ///
  /// @return The largest legal super-reg register class of the register class for the specified type and its associated "cost".
  ///
  /// \param TRI Target register info.
  /// \param VT Value type being queried or transformed.
  virtual std::pair<const TargetRegisterClass *, uint8_t>
  findRepresentativeClass(const TargetRegisterInfo *TRI, MVT VT) const;

  /// Once all of the register classes are added, this allows us to compute
  /// derived properties we expose.
  /// \param TRI Target register info.
  void computeRegisterProperties(const TargetRegisterInfo *TRI);

  /// Set the legalization action to use for the given operation and value type.
  ///
  /// Indicate that the specified operation does not work with the specified type
  /// and indicate what to do about it. Note that VT may refer to either the type
  /// of a result or that of an operand of Op.
  ///
  /// \param Op SDNode or value being queried.
  /// \param VT Value type being queried or transformed.
  /// \param Action Legalize action to record.
  void setOperationAction(unsigned Op, MVT VT, LegalizeAction Action) {
    assert(Op < std::size(OpActions[0]) && "Table isn't big enough!");
    OpActions[(unsigned)VT.SimpleTy][Op] = Action;
  }
  /// Set the legalization action to use for the given operation and value type.
  ///
  /// \param Ops Operands passed to the libcall or asm.
  /// \param VT Value type being queried or transformed.
  /// \param Action Legalize action to record.
  void setOperationAction(ArrayRef<unsigned> Ops, MVT VT,
                          LegalizeAction Action) {
    for (auto Op : Ops)
      setOperationAction(Op, VT, Action);
  }
  /// Set the legalization action to use for the given operation and value type.
  ///
  /// \param Ops Operands passed to the libcall or asm.
  /// \param VTs Value types to configure or query.
  /// \param Action Legalize action to record.
  void setOperationAction(ArrayRef<unsigned> Ops, ArrayRef<MVT> VTs,
                          LegalizeAction Action) {
    for (auto VT : VTs)
      setOperationAction(Ops, VT, Action);
  }

  /// Indicate that the specified load with extension does not work with the
  /// specified type and indicate what to do about it.
  /// \param ExtType Load extension kind.
  /// \param ValVT Value type determining boolean contents.
  /// \param MemVT Memory value type of the access.
  /// \param Action Legalize action to record.
  void setLoadExtAction(unsigned ExtType, MVT ValVT, MVT MemVT,
                        LegalizeAction Action) {
    assert(ExtType < ISD::LAST_LOADEXT_TYPE && ValVT.isValid() &&
           MemVT.isValid() && "Table isn't big enough!");
    assert((unsigned)Action < 0x10 && "too many bits for bitfield array");
    unsigned Shift = 4 * ExtType;
    LoadExtActions[ValVT.SimpleTy][MemVT.SimpleTy] &= ~((uint16_t)0xF << Shift);
    LoadExtActions[ValVT.SimpleTy][MemVT.SimpleTy] |= (uint16_t)Action << Shift;
  }
  /// Set the legalization action for a load with the given extension type and
  /// value/memory types.
  ///
  /// \param ExtTypes Extension types to configure.
  /// \param ValVT Value type determining boolean contents.
  /// \param MemVT Memory value type of the access.
  /// \param Action Legalize action to record.
  void setLoadExtAction(ArrayRef<unsigned> ExtTypes, MVT ValVT, MVT MemVT,
                        LegalizeAction Action) {
    for (auto ExtType : ExtTypes)
      setLoadExtAction(ExtType, ValVT, MemVT, Action);
  }
  /// Set the legalization action for a load with the given extension type and
  /// value/memory types.
  ///
  /// \param ExtTypes Extension types to configure.
  /// \param ValVT Value type determining boolean contents.
  /// \param MemVTs Memory value types associated with the intrinsic.
  /// \param Action Legalize action to record.
  void setLoadExtAction(ArrayRef<unsigned> ExtTypes, MVT ValVT,
                        ArrayRef<MVT> MemVTs, LegalizeAction Action) {
    for (auto MemVT : MemVTs)
      setLoadExtAction(ExtTypes, ValVT, MemVT, Action);
  }

  /// Let target indicate that an extending atomic load of the specified type
  /// is legal.
  /// \param ExtType Load extension kind.
  /// \param ValVT Value type determining boolean contents.
  /// \param MemVT Memory value type of the access.
  /// \param Action Legalize action to record.
  void setAtomicLoadExtAction(unsigned ExtType, MVT ValVT, MVT MemVT,
                              LegalizeAction Action) {
    assert(ExtType < ISD::LAST_LOADEXT_TYPE && ValVT.isValid() &&
           MemVT.isValid() && "Table isn't big enough!");
    assert((unsigned)Action < 0x10 && "too many bits for bitfield array");
    unsigned Shift = 4 * ExtType;
    AtomicLoadExtActions[ValVT.SimpleTy][MemVT.SimpleTy] &=
        ~((uint16_t)0xF << Shift);
    AtomicLoadExtActions[ValVT.SimpleTy][MemVT.SimpleTy] |=
        ((uint16_t)Action << Shift);
  }
  /// Set the legalization action for an extending atomic load of the given type.
  ///
  /// \param ExtTypes Extension types to configure.
  /// \param ValVT Value type determining boolean contents.
  /// \param MemVT Memory value type of the access.
  /// \param Action Legalize action to record.
  void setAtomicLoadExtAction(ArrayRef<unsigned> ExtTypes, MVT ValVT, MVT MemVT,
                              LegalizeAction Action) {
    for (auto ExtType : ExtTypes)
      setAtomicLoadExtAction(ExtType, ValVT, MemVT, Action);
  }
  /// Set the legalization action for an extending atomic load of the given type.
  ///
  /// \param ExtTypes Extension types to configure.
  /// \param ValVT Value type determining boolean contents.
  /// \param MemVTs Memory value types associated with the intrinsic.
  /// \param Action Legalize action to record.
  void setAtomicLoadExtAction(ArrayRef<unsigned> ExtTypes, MVT ValVT,
                              ArrayRef<MVT> MemVTs, LegalizeAction Action) {
    for (auto MemVT : MemVTs)
      setAtomicLoadExtAction(ExtTypes, ValVT, MemVT, Action);
  }

  /// Indicate that the specified truncating store does not work with the
  /// specified type and indicate what to do about it.
  /// \param ValVT Value type determining boolean contents.
  /// \param MemVT Memory value type of the access.
  /// \param Action Legalize action to record.
  void setTruncStoreAction(MVT ValVT, MVT MemVT, LegalizeAction Action) {
    assert(ValVT.isValid() && MemVT.isValid() && "Table isn't big enough!");
    TruncStoreActions[(unsigned)ValVT.SimpleTy][MemVT.SimpleTy] = Action;
  }

  /// Indicate that the specified indexed load does or does not work with the
  /// specified type and indicate what to do abort it.
  ///
  /// NOTE: All indexed mode loads are initialized to Expand in
  /// TargetLowering.cpp
  /// \param IdxModes Indexed modes to configure.
  /// \param VT Value type being queried or transformed.
  /// \param Action Legalize action to record.
  void setIndexedLoadAction(ArrayRef<unsigned> IdxModes, MVT VT,
                            LegalizeAction Action) {
    for (auto IdxMode : IdxModes)
      setIndexedModeAction(IdxMode, VT, IMAB_Load, Action);
  }

  /// Set the legalization action for the given indexed load addressing modes and
  /// type.
  ///
  /// \param IdxModes Indexed modes to configure.
  /// \param VTs Value types to configure or query.
  /// \param Action Legalize action to record.
  void setIndexedLoadAction(ArrayRef<unsigned> IdxModes, ArrayRef<MVT> VTs,
                            LegalizeAction Action) {
    for (auto VT : VTs)
      setIndexedLoadAction(IdxModes, VT, Action);
  }

  /// Indicate that the specified indexed store does or does not work with the
  /// specified type and indicate what to do about it.
  ///
  /// NOTE: All indexed mode stores are initialized to Expand in
  /// TargetLowering.cpp
  /// \param IdxModes Indexed modes to configure.
  /// \param VT Value type being queried or transformed.
  /// \param Action Legalize action to record.
  void setIndexedStoreAction(ArrayRef<unsigned> IdxModes, MVT VT,
                             LegalizeAction Action) {
    for (auto IdxMode : IdxModes)
      setIndexedModeAction(IdxMode, VT, IMAB_Store, Action);
  }

  /// Set the legalization action for the given indexed store addressing modes
  /// and type.
  ///
  /// \param IdxModes Indexed modes to configure.
  /// \param VTs Value types to configure or query.
  /// \param Action Legalize action to record.
  void setIndexedStoreAction(ArrayRef<unsigned> IdxModes, ArrayRef<MVT> VTs,
                             LegalizeAction Action) {
    for (auto VT : VTs)
      setIndexedStoreAction(IdxModes, VT, Action);
  }

  /// Indicate that the specified indexed masked load does or does not work with
  /// the specified type and indicate what to do about it.
  ///
  /// NOTE: All indexed mode masked loads are initialized to Expand in
  /// TargetLowering.cpp
  /// \param IdxMode Indexed addressing mode.
  /// \param VT Value type being queried or transformed.
  /// \param Action Legalize action to record.
  void setIndexedMaskedLoadAction(unsigned IdxMode, MVT VT,
                                  LegalizeAction Action) {
    setIndexedModeAction(IdxMode, VT, IMAB_MaskedLoad, Action);
  }

  /// Indicate that the specified indexed masked store does or does not work
  /// with the specified type and indicate what to do about it.
  ///
  /// NOTE: All indexed mode masked stores are initialized to Expand in
  /// TargetLowering.cpp
  /// \param IdxMode Indexed addressing mode.
  /// \param VT Value type being queried or transformed.
  /// \param Action Legalize action to record.
  void setIndexedMaskedStoreAction(unsigned IdxMode, MVT VT,
                                   LegalizeAction Action) {
    setIndexedModeAction(IdxMode, VT, IMAB_MaskedStore, Action);
  }

  /// Indicate that the specified condition code is or isn't supported on the
  /// target and indicate what to do about it.
  /// \param CCs Condition codes to configure.
  /// \param VT Value type being queried or transformed.
  /// \param Action Legalize action to record.
  void setCondCodeAction(ArrayRef<ISD::CondCode> CCs, MVT VT,
                         LegalizeAction Action) {
    for (auto CC : CCs) {
      assert(VT.isValid() && (unsigned)CC < std::size(CondCodeActions) &&
             "Table isn't big enough!");
      assert((unsigned)Action < 0x10 && "too many bits for bitfield array");
      /// The lower 3 bits of the SimpleTy index into Nth 4bit set from the
      /// 32-bit value and the upper 29 bits index into the second dimension of
      /// the array to select what 32-bit value to use.
      uint32_t Shift = 4 * (VT.SimpleTy & 0x7);
      CondCodeActions[CC][VT.SimpleTy >> 3] &= ~((uint32_t)0xF << Shift);
      CondCodeActions[CC][VT.SimpleTy >> 3] |= (uint32_t)Action << Shift;
    }
  }
  /// Set the legalization action for the given condition codes and type.
  ///
  /// \param CCs Condition codes to configure.
  /// \param VTs Value types to configure or query.
  /// \param Action Legalize action to record.
  void setCondCodeAction(ArrayRef<ISD::CondCode> CCs, ArrayRef<MVT> VTs,
                         LegalizeAction Action) {
    for (auto VT : VTs)
      setCondCodeAction(CCs, VT, Action);
  }

  /// Set the legalization action for a partial-reduce multiply-accumulate node
  /// with the given types.
  ///
  /// Indicate how a PARTIAL_REDUCE_U/SMLA node with Acc type AccVT and Input
  /// type InputVT should be treated by the target. Either it's legal, needs to
  /// be promoted to a larger size, needs to be expanded to some other code
  /// sequence, or the target has a custom expander for it.
  ///
  /// \param Opc Operation opcode.
  /// \param AccVT Accumulator value type.
  /// \param InputVT Input value type.
  /// \param Action Legalize action to record.
  void setPartialReduceMLAAction(unsigned Opc, MVT AccVT, MVT InputVT,
                                 LegalizeAction Action) {
    assert(Opc == ISD::PARTIAL_REDUCE_SMLA || Opc == ISD::PARTIAL_REDUCE_UMLA ||
           Opc == ISD::PARTIAL_REDUCE_SUMLA || Opc == ISD::PARTIAL_REDUCE_FMLA);
    assert(AccVT.isValid() && InputVT.isValid() &&
           "setPartialReduceMLAAction types aren't valid");
    PartialReduceActionTypes Key = {Opc, AccVT.SimpleTy, InputVT.SimpleTy};
    PartialReduceMLAActions[Key] = Action;
  }
  /// Set the legalization action for a partial-reduce multiply-accumulate node
  /// with the given types.
  ///
  /// \param Opcodes Opcodes to register for target DAG combining.
  /// \param AccVT Accumulator value type.
  /// \param InputVT Input value type.
  /// \param Action Legalize action to record.
  void setPartialReduceMLAAction(ArrayRef<unsigned> Opcodes, MVT AccVT,
                                 MVT InputVT, LegalizeAction Action) {
    for (unsigned Opc : Opcodes)
      setPartialReduceMLAAction(Opc, AccVT, InputVT, Action);
  }

  /// Override the default promotion type for the specified operation and value
  /// type.
  ///
  /// If Opc/OrigVT is specified as being promoted, the promotion code defaults
  /// to trying a larger integer/fp until it can find one that works. If that
  /// default is insufficient, this method can be used by the target to override
  /// the default.
  ///
  /// \param Opc Operation opcode.
  /// \param OrigVT Original value type before promotion.
  /// \param DestVT Destination value type after promotion.
  void AddPromotedToType(unsigned Opc, MVT OrigVT, MVT DestVT) {
    PromoteToType[std::make_pair(Opc, OrigVT.SimpleTy)] = DestVT.SimpleTy;
  }

  /// Convenience method to set an operation to Promote and specify the type
  /// in a single call.
  /// \param Opc Operation opcode.
  /// \param OrigVT Original value type before promotion.
  /// \param DestVT Destination value type after promotion.
  void setOperationPromotedToType(unsigned Opc, MVT OrigVT, MVT DestVT) {
    /// Set the legalization action to use for the given operation and value
    /// type.
    ///
    /// \param Opc Operation opcode.
    /// \param OrigVT Original value type before promotion.
    /// \param Promote Whether promotion should be performed.
    setOperationAction(Opc, OrigVT, Promote);
    /// Override the default promotion type for the specified operation and value
    /// type.
    ///
    /// \param Opc Operation opcode.
    /// \param OrigVT Original value type before promotion.
    /// \param DestVT Destination value type after promotion.
    AddPromotedToType(Opc, OrigVT, DestVT);
  }
  /// Set an operation's legalization action to Promote and specify the type to
  /// promote to.
  ///
  /// \param Ops Operands passed to the libcall or asm.
  /// \param OrigVT Original value type before promotion.
  /// \param DestVT Destination value type after promotion.
  void setOperationPromotedToType(ArrayRef<unsigned> Ops, MVT OrigVT,
                                  MVT DestVT) {
    for (auto Op : Ops) {
      setOperationAction(Op, OrigVT, Promote);
      AddPromotedToType(Op, OrigVT, DestVT);
    }
  }

  /// Register the given target-independent node types for custom DAG combining.
  ///
  /// Targets should invoke this method for each target independent node that
  /// they want to provide a custom DAG combiner for by implementing the
  /// PerformDAGCombine virtual method.
  ///
  /// \param NTs Node types that get custom DAG combines.
  void setTargetDAGCombine(ArrayRef<ISD::NodeType> NTs) {
    for (auto NT : NTs) {
      assert(unsigned(NT >> 3) < std::size(TargetDAGCombineArray));
      TargetDAGCombineArray[NT >> 3] |= 1 << (NT & 7);
    }
  }

  /// Set the target's minimum function alignment.
  /// \param Alignment Required or preferred alignment.
  void setMinFunctionAlignment(Align Alignment) {
    MinFunctionAlignment = Alignment;
  }

  /// Set the target's preferred function alignment.  This should be set if
  /// there is a performance benefit to higher-than-minimum alignment
  /// \param Alignment Required or preferred alignment.
  void setPrefFunctionAlignment(Align Alignment) {
    PrefFunctionAlignment = Alignment;
  }

  /// Set the target's preferred loop alignment.
  ///
  /// Set the target's preferred loop alignment. Default alignment is one, it
  /// means the target does not care about loop alignment. The target may also
  /// override getPrefLoopAlignment to provide per-loop values.
  ///
  /// \param Alignment Required or preferred alignment.
  void setPrefLoopAlignment(Align Alignment) { PrefLoopAlignment = Alignment; }
  /// Set the maximum number of bytes allowed to be emitted for loop alignment
  /// padding.
  ///
  /// \param MaxBytes Maximum number of padding bytes permitted.
  void setMaxBytesForAlignment(unsigned MaxBytes) {
    MaxBytesForAlignment = MaxBytes;
  }

  /// Set the minimum stack alignment of an argument.
  /// \param Alignment Required or preferred alignment.
  void setMinStackArgumentAlignment(Align Alignment) {
    MinStackArgumentAlignment = Alignment;
  }

  /// Set the maximum atomic operation size, in bits, supported by the backend.
  ///
  /// Set the maximum atomic operation size supported by the backend. Atomic
  /// operations greater than this size (as well as ones that are not naturally
  /// aligned), will be expanded by AtomicExpandPass into an __atomic_* library
  /// call.
  ///
  /// \param SizeInBits Size limit in bits.
  void setMaxAtomicSizeInBitsSupported(unsigned SizeInBits) {
    MaxAtomicSizeInBitsSupported = SizeInBits;
  }

  /// Set the size in bits of the maximum div/rem the backend supports.
  /// Larger operations will be expanded by ExpandIRInsts.
  /// \param SizeInBits Size limit in bits.
  void setMaxDivRemBitWidthSupported(unsigned SizeInBits) {
    MaxDivRemBitWidthSupported = SizeInBits;
  }

  /// Set the size in bits of the maximum fp to/from int conversion the backend
  /// supports. Larger operations will be expanded by ExpandIRInsts.
  /// \param SizeInBits Size limit in bits.
  void setMaxLargeFPConvertBitWidthSupported(unsigned SizeInBits) {
    MaxLargeFPConvertBitWidthSupported = SizeInBits;
  }

  /// Sets the minimum cmpxchg or ll/sc size supported by the backend.
  /// \param SizeInBits Size limit in bits.
  void setMinCmpXchgSizeInBits(unsigned SizeInBits) {
    MinCmpXchgSizeInBits = SizeInBits;
  }

  /// Sets whether unaligned atomic operations are supported.
  /// \param UnalignedSupported Whether unaligned atomics are supported.
  void setSupportsUnalignedAtomics(bool UnalignedSupported) {
    SupportsUnalignedAtomics = UnalignedSupported;
  }

public:
  //===--------------------------------------------------------------------===//
  // Addressing mode description hooks (used by LSR etc).
  //

  /// Return the address operands of an intrinsic that should be sunk into the
  /// address mode.
  ///
  /// CodeGenPrepare sinks address calculations into the same BB as Load/Store
  /// instructions reading the address. This allows as much computation as
  /// possible to be done in the address mode for that operand. This hook lets
  /// targets also pass back when this should be done on intrinsics which
  /// load/store.
  ///
  /// @return The address operands of an intrinsic that should be sunk into the address mode.
  ///
  /// \param I Instruction being queried.
  /// \param Ops Operands passed to the libcall or asm.
  /// \param AccessTy Filled with the memory access type.
  virtual bool getAddrModeArguments(const IntrinsicInst *I,
                                    SmallVectorImpl<Value *> &Ops,
                                    Type *&AccessTy) const {
    return false;
  }

  /// Describes a target addressing mode of base, offset, register, and scale
  /// components.
  ///
  /// Describes a target addressing mode for loads and stores.
  /// This represents an addressing mode of:
  ///    BaseGV + BaseOffs + BaseReg + Scale*ScaleReg + ScalableOffset*vscale
  /// If BaseGV is null,  there is no BaseGV.
  /// If BaseOffs is zero, there is no base offset.
  /// If HasBaseReg is false, there is no base register.
  /// If Scale is zero, there is no ScaleReg.  Scale of 1 indicates a reg with
  /// no scale.
  /// If ScalableOffset is zero, there is no scalable offset.
  struct AddrMode {
    /// Optional global value base of the address.
    GlobalValue *BaseGV = nullptr;
    /// Constant byte offset from the base.
    int64_t      BaseOffs = 0;
    /// Whether a base register is present.
    bool         HasBaseReg = false;
    /// Scale applied to the scaled register.
    int64_t      Scale = 0;
    /// Scalable offset multiplied by vscale.
    int64_t ScalableOffset = 0;
    /// Describes a target addressing mode of base, offset, register, and scale
    /// components.
    ///
    /// Construct an empty addressing mode.
    AddrMode() = default;
  };

  /// Return true if the addressing mode represented by AM is legal for this
  /// target, for a load/store of the specified type.
  ///
  /// The type may be VoidTy, in which case only return true if the addressing
  /// mode is legal for a load/store of any legal type.  TODO: Handle
  /// pre/postinc as well.
  ///
  /// If the address space cannot be determined, it will be -1.
  ///
  /// TODO: Remove default argument
  ///
  /// @return True if the addressing mode represented by AM is legal for this target, for a load/store of the specified type.
  ///
  /// \param DL Debug location or data layout, depending on context.
  /// \param AM Filled with the indexed addressing mode.
  /// \param Ty Type of the constant or operation.
  /// \param AddrSpace Address space of the memory access.
  /// \param I Instruction being queried.
  virtual bool isLegalAddressingMode(const DataLayout &DL, const AddrMode &AM,
                                     Type *Ty, unsigned AddrSpace,
                                     Instruction *I = nullptr) const;

  /// Returns true if the targets addressing mode can target thread local
  /// storage (TLS).
  ///
  /// @return True if the targets addressing mode can target thread local storage (TLS).
  ///
  /// \param GV Global value whose TLS addressing is queried.
  virtual bool addressingModeSupportsTLS(const GlobalValue &GV) const {
    return false;
  }

  /// Return the prefered common base offset.
  ///
  /// @return The prefered common base offset.
  ///
  /// \param MinOffset Minimum GEP offset in the range.
  /// \param MaxOffset Maximum GEP offset in the range.
  virtual int64_t getPreferredLargeGEPBaseOffset(int64_t MinOffset,
                                                 int64_t MaxOffset) const {
    return 0;
  }

  /// Return true if the target can compare a register against this immediate
  /// without materializing it.
  ///
  /// Return true if the specified immediate is legal icmp immediate, that is the
  /// target has icmp instructions which can compare a register against the
  /// immediate without having to materialize the immediate into a register.
  ///
  /// @return True if the target can compare a register against this immediate without materializing it.
  ///
  /// \param Imm Immediate constant operand.
  virtual bool isLegalICmpImmediate(int64_t Imm) const {
    return true;
  }

  /// Return true if the target can add this immediate to a register without
  /// materializing it.
  ///
  /// Return true if the specified immediate is legal add immediate, that is the
  /// target has add instructions which can add a register with the immediate
  /// without having to materialize the immediate into a register.
  ///
  /// @return True if the target can add this immediate to a register without materializing it.
  ///
  /// \param Imm Immediate constant operand.
  virtual bool isLegalAddImmediate(int64_t Imm) const {
    return true;
  }

  /// Return true if the target can add this scalable immediate (multiplied by
  /// vscale) without materializing it.
  ///
  /// Return true if adding the specified scalable immediate is legal, that is
  /// the target has add instructions which can add a register with the immediate
  /// (multiplied by vscale) without having to materialize the immediate into a
  /// register.
  ///
  /// @return True if the target can add this scalable immediate (multiplied by vscale) without materializing it.
  ///
  /// \param Imm Immediate constant operand.
  virtual bool isLegalAddScalableImmediate(int64_t Imm) const { return false; }

  /// Return true if the specified immediate is legal for the value input of a
  /// store instruction.
  ///
  /// @return True if the specified immediate is legal for the value input of a store instruction.
  ///
  /// \param Value Switch/jump discriminant.
  virtual bool isLegalStoreImmediate(int64_t Value) const {
    // Default implementation assumes that at least 0 works since it is likely
    // that a zero register exists or a zero immediate is allowed.
    return Value == 0;
  }

  /// Return a more profitable scalar type to use for a vector splat, or nullptr
  /// if none.
  ///
  /// Given a shuffle vector SVI representing a vector splat, return a new scalar
  /// type of size equal to SVI's scalar type if the new type is more profitable.
  /// Returns nullptr otherwise. For example under MVE float splats are converted
  /// to integer to prevent the need to move from SPR to GPR registers.
  ///
  /// @return A more profitable scalar type to use for a vector splat, or nullptr if none.
  ///
  /// \param SVI Shuffle vector instruction.
  virtual Type* shouldConvertSplatType(ShuffleVectorInst* SVI) const {
    return nullptr;
  }

  /// Given a set in interconnected phis of type 'From' that are loaded/stored
  /// or bitcast to type 'To', return true if the set should be converted to
  /// 'To'.
  ///
  /// @return True if given a set in interconnected phis of type 'From' that are loaded/stored or bitcast to type 'To', return true if the set should be converted to 'To'.
  ///
  /// \param From Source PHI type.
  /// \param To Destination PHI type.
  virtual bool shouldConvertPhiType(Type *From, Type *To) const {
    return (From->isIntegerTy() || From->isFloatingPointTy()) &&
           (To->isIntegerTy() || To->isFloatingPointTy());
  }

  /// Returns true if the opcode is a commutative binary operation.
  ///
  /// @return True if the opcode is a commutative binary operation.
  ///
  /// \param Opcode ISD or target opcode.
  virtual bool isCommutativeBinOp(unsigned Opcode) const {
    // FIXME: This should get its info from the td file.
    switch (Opcode) {
    case ISD::ADD:
    case ISD::SMIN:
    case ISD::SMAX:
    case ISD::UMIN:
    case ISD::UMAX:
    case ISD::MUL:
    case ISD::CLMUL:
    case ISD::CLMULH:
    case ISD::CLMULR:
    case ISD::MULHU:
    case ISD::MULHS:
    case ISD::SMUL_LOHI:
    case ISD::UMUL_LOHI:
    case ISD::FADD:
    case ISD::FMUL:
    case ISD::AND:
    case ISD::OR:
    case ISD::XOR:
    case ISD::SADDO:
    case ISD::UADDO:
    case ISD::ADDC:
    case ISD::ADDE:
    case ISD::SADDSAT:
    case ISD::UADDSAT:
    case ISD::FMINNUM:
    case ISD::FMAXNUM:
    case ISD::FMINNUM_IEEE:
    case ISD::FMAXNUM_IEEE:
    case ISD::FMINIMUM:
    case ISD::FMAXIMUM:
    case ISD::FMINIMUMNUM:
    case ISD::FMAXIMUMNUM:
    case ISD::AVGFLOORS:
    case ISD::AVGFLOORU:
    case ISD::AVGCEILS:
    case ISD::AVGCEILU:
    case ISD::ABDS:
    case ISD::ABDU:
      return true;
    default: return false;
    }
  }

  /// Return true if the node is a math/logic binary operator.
  ///
  /// @return True if the node is a math/logic binary operator.
  ///
  /// \param Opcode ISD or target opcode.
  virtual bool isBinOp(unsigned Opcode) const {
    // A commutative binop must be a binop.
    if (isCommutativeBinOp(Opcode))
      return true;
    // These are non-commutative binops.
    switch (Opcode) {
    case ISD::SUB:
    case ISD::SHL:
    case ISD::SRL:
    case ISD::SRA:
    case ISD::ROTL:
    case ISD::ROTR:
    case ISD::SDIV:
    case ISD::UDIV:
    case ISD::SREM:
    case ISD::UREM:
    case ISD::SSUBSAT:
    case ISD::USUBSAT:
    case ISD::FSUB:
    case ISD::FDIV:
    case ISD::FREM:
    case ISD::PSEUDO_FMIN:
    case ISD::PSEUDO_FMAX:
      return true;
    default:
      return false;
    }
  }

  /// Return true if truncating a value from FromTy to ToTy is free.
  ///
  /// Return true if it's free to truncate a value of type FromTy to type ToTy.
  /// e.g. On x86 it's free to truncate a i32 value in register EAX to i16 by
  /// referencing its sub-register AX. Targets must return false when FromTy <=
  /// ToTy.
  ///
  /// @return True if truncating a value from FromTy to ToTy is free.
  ///
  /// \param FromTy Source type of the conversion.
  /// \param ToTy Destination type of the conversion.
  virtual bool isTruncateFree(Type *FromTy, Type *ToTy) const {
    return false;
  }

  /// Return true if truncating from FromTy to ToTy is permitted for a tail call.
  ///
  /// Return true if a truncation from FromTy to ToTy is permitted when deciding
  /// whether a call is in tail position. Typically this means that both results
  /// would be assigned to the same register or stack slot, but it could mean the
  /// target performs adequate checks of its own before proceeding with the tail
  /// call. Targets must return false when FromTy <= ToTy.
  ///
  /// @return True if truncating from FromTy to ToTy is permitted for a tail call.
  ///
  /// \param FromTy Source type of the conversion.
  /// \param ToTy Destination type of the conversion.
  virtual bool allowTruncateForTailCall(Type *FromTy, Type *ToTy) const {
    return false;
  }

  /// Return true if truncating a value from FromVT to ToVT is free.
  ///
  /// @return True if truncating a value from FromVT to ToVT is free.
  ///
  /// \param FromVT Source value type of the truncation.
  /// \param ToVT Destination value type of the truncation.
  virtual bool isTruncateFree(EVT FromVT, EVT ToVT) const { return false; }

  /// Return true if truncating a value from FromTy to ToTy is free.
  ///
  /// @return True if truncating a value from FromTy to ToTy is free.
  ///
  /// \param FromTy Source type of the truncation.
  /// \param ToTy Destination type of the truncation.
  /// \param Ctx LLVM context used to approximate EVTs for the LLTs.
  virtual bool isTruncateFree(LLT FromTy, LLT ToTy, LLVMContext &Ctx) const {
    return isTruncateFree(getApproximateEVTForLLT(FromTy, Ctx),
                          getApproximateEVTForLLT(ToTy, Ctx));
  }

  /// Return true if truncating a value from FromTy to ToTy is free.
  ///
  /// Return true if truncating the specific node Val to type VT2 is free.
  ///
  /// @return True if truncating a value from FromTy to ToTy is free.
  ///
  /// \param Val Minimum number of bit-test compares.
  /// \param VT2 Destination value type of the extension or truncate.
  virtual bool isTruncateFree(SDValue Val, EVT VT2) const {
    // Fallback to type matching.
    return isTruncateFree(Val.getValueType(), VT2);
  }

  /// Return true if it is profitable to hoist the given instruction out of a
  /// loop.
  ///
  /// @return True if it is profitable to hoist the given instruction out of a loop.
  ///
  /// \param I Instruction being queried.
  virtual bool isProfitableToHoist(Instruction *I) const { return true; }

  /// Return true if the sign, zero, or fp extension represented by this
  /// instruction is free.
  ///
  /// Return true if the extension represented by \p I is free. Unlikely the
  /// is[Z|FP]ExtFree family which is based on types, this method can use the
  /// context provided by \p I to decide whether or not \p I is free. This method
  /// extends the behavior of the is[Z|FP]ExtFree family. In other words, if
  /// is[Z|FP]Free returns true, then this method returns true as well. The
  /// converse is not true. The target can perform the adequate checks by
  /// overriding isExtFreeImpl.
  ///
  /// \pre \p I must be a sign, zero, or fp extension.
  /// @return True if the sign, zero, or fp extension represented by this instruction is free.
  ///
  /// \param I Instruction being queried.
  bool isExtFree(const Instruction *I) const {
    switch (I->getOpcode()) {
    case Instruction::FPExt:
      if (isFPExtFree(EVT::getEVT(I->getType()),
                      EVT::getEVT(I->getOperand(0)->getType())))
        return true;
      break;
    case Instruction::ZExt:
      if (isZExtFree(I->getOperand(0)->getType(), I->getType()))
        return true;
      break;
    case Instruction::SExt:
      break;
    default:
      llvm_unreachable("Instruction is not an extension");
    }
    return isExtFreeImpl(I);
  }

  /// Return true if a load and a subsequent extend can be combined into a single
  /// extending load.
  ///
  /// Return true if \p Load and \p Ext can form an ExtLoad. For example, in
  /// AArch64 %L = load i8, i8* %ptr %E = zext i8 %L to i32 can be lowered into
  /// one load instruction ldrb w0, [x0]
  ///
  /// @return True if a load and a subsequent extend can be combined into a single extending load.
  ///
  /// \param Load Load instruction or SDNode being queried.
  /// \param Ext Extension instruction paired with the load.
  /// \param DL Debug location or data layout, depending on context.
  bool isExtLoad(const LoadInst *Load, const Instruction *Ext,
                 const DataLayout &DL) const {
    EVT VT = getValueType(DL, Ext->getType());
    EVT LoadVT = getValueType(DL, Load->getType());

    // If the load has other users and the truncate is not free, the ext
    // probably isn't free.
    if (!Load->hasOneUse() && (isTypeLegal(LoadVT) || !isTypeLegal(VT)) &&
        !isTruncateFree(Ext->getType(), Load->getType()))
      return false;

    // Check whether the target supports casts folded into loads.
    unsigned LType;
    if (isa<ZExtInst>(Ext))
      LType = ISD::ZEXTLOAD;
    else {
      assert(isa<SExtInst>(Ext) && "Unexpected ext type!");
      LType = ISD::SEXTLOAD;
    }

    return isLoadLegal(VT, LoadVT, Load->getAlign(),
                       Load->getPointerAddressSpace(), LType, false);
  }

  /// Return true if a value of type FromTy is implicitly zero-extended to ToTy
  /// for free.
  ///
  /// Return true if any actual instruction that defines a value of type FromTy
  /// implicitly zero-extends the value to ToTy in the result register.
  /// The function should return true when it is likely that the truncate can
  /// be freely folded with an instruction defining a value of FromTy. If
  /// the defining instruction is unknown (because you're looking at a
  /// function argument, PHI, etc.) then the target may require an
  /// explicit truncate, which is not necessarily free, but this function
  /// does not deal with those cases.
  /// Targets must return false when FromTy >= ToTy.
  ///
  /// @return True if a value of type FromTy is implicitly zero-extended to ToTy for free.
  ///
  /// \param FromTy Source type of the conversion.
  /// \param ToTy Destination type of the conversion.
  virtual bool isZExtFree(Type *FromTy, Type *ToTy) const {
    return false;
  }

  /// Return true if a value of type FromTy is implicitly zero-extended to ToTy
  /// for free.
  ///
  /// @return True if a value of type FromTy is implicitly zero-extended to ToTy for free.
  ///
  /// \param FromTy Source type of the conversion.
  /// \param ToTy Destination type of the conversion.
  virtual bool isZExtFree(EVT FromTy, EVT ToTy) const { return false; }

  /// Return true if a value of type FromTy is implicitly zero-extended to ToTy
  /// for free.
  ///
  /// @return True if a value of type FromTy is implicitly zero-extended to ToTy for free.
  ///
  /// \param FromTy Source type of the conversion.
  /// \param ToTy Destination type of the conversion.
  /// \param Ctx LLVM context used to approximate EVTs for the LLTs.
  virtual bool isZExtFree(LLT FromTy, LLT ToTy, LLVMContext &Ctx) const {
    return isZExtFree(getApproximateEVTForLLT(FromTy, Ctx),
                      getApproximateEVTForLLT(ToTy, Ctx));
  }

  /// Return true if a value of type FromTy is implicitly zero-extended to ToTy
  /// for free.
  ///
  /// Return true if zero-extending the specific node Val to type VT2 is free
  /// (either because it's implicitly zero-extended such as ARM ldrb / ldrh or
  /// because it's folded such as X86 zero-extending loads).
  ///
  /// @return True if a value of type FromTy is implicitly zero-extended to ToTy for free.
  ///
  /// \param Val Minimum number of bit-test compares.
  /// \param VT2 Destination value type of the extension or truncate.
  virtual bool isZExtFree(SDValue Val, EVT VT2) const {
    return isZExtFree(Val.getValueType(), VT2);
  }

  /// Return true is an anyext is free from FromTy to ToTy. Usually true for
  /// scalar types when not trying to pack elements into vector lanes.
  ///
  /// @return True is an anyext is free from FromTy to ToTy. Usually true for scalar types when not trying to pack elements into vector lanes.
  ///
  /// \param FromTy Source type of the conversion.
  /// \param ToTy Destination type of the conversion.
  virtual bool isAnyExtFree(EVT FromTy, EVT ToTy) const {
    return !FromTy.isVector();
  }

  /// Return true if sign-extension from FromTy to ToTy is cheaper than
  /// zero-extension.
  ///
  /// @return True if sign-extension from FromTy to ToTy is cheaper than zero-extension.
  ///
  /// \param FromTy Source type of the conversion.
  /// \param ToTy Destination type of the conversion.
  virtual bool isSExtCheaperThanZExt(EVT FromTy, EVT ToTy) const {
    return false;
  }

  /// Return true if this constant should be sign extended when promoting to
  /// a larger type.
  ///
  /// @return True if this constant should be sign extended when promoting to a larger type.
  ///
  /// \param C Constant value being tested.
  virtual bool signExtendConstant(const ConstantInt *C) const { return false; }

  /// Try to optimize extending or truncating conversion instructions (like
  /// zext, trunc, fptoui, uitofp) for the target.
  ///
  /// @return True if try to optimize extending or truncating conversion instructions (like zext, trunc, fptoui, uitofp) for the target.
  ///
  /// \param I Instruction being queried.
  /// \param L Loop being optimized.
  /// \param TTI Target transform info.
  virtual bool
  optimizeExtendOrTruncateConversion(Instruction *I, Loop *L,
                                     const TargetTransformInfo &TTI) const {
    return false;
  }

  /// Return true if the target can combine two adjacent loads into a single
  /// paired load.
  ///
  /// Return true if the target supplies and combines to a paired load two loaded
  /// values of type LoadedType next to each other in memory. RequiredAlignment
  /// gives the minimal alignment constraints that must be met to be able to
  /// select this paired load.
  /// This information is *not* used to generate actual paired loads, but it is
  /// used to generate a sequence of loads that is easier to combine into a
  /// paired load.
  /// For instance, something like this:
  /// a = load i64* addr
  /// b = trunc i64 a to i32
  /// c = lshr i64 a, 32
  /// d = trunc i64 c to i32
  /// will be optimized into:
  /// b = load i32* addr1
  /// d = load i32* addr2
  /// Where addr1 = addr2 +/- sizeof(i32).
  ///
  /// In other words, unless the target performs a post-isel load combining,
  /// this information should not be provided because it will generate more
  /// loads.
  ///
  /// @return True if the target can combine two adjacent loads into a single paired load.
  ///
  /// \param LoadedType Type of each paired load.
  /// \param RequiredAlignment Filled with the minimum alignment required.
  virtual bool hasPairedLoad(EVT LoadedType,
                             Align &RequiredAlignment) const {
    return false;
  }

  /// Return true if the target has a vector blend instruction.
  ///
  /// @return True if the target has a vector blend instruction.
  virtual bool hasVectorBlend() const { return false; }

  /// Get the maximum supported factor for interleaved memory accesses.
  /// Default to be the minimum interleave factor: 2.
  ///
  /// @return Get the maximum supported factor for interleaved memory accesses. Default to be the minimum interleave factor: 2.
  virtual unsigned getMaxSupportedInterleaveFactor() const { return 2; }

  /// Lower an interleaved load to target specific intrinsics. Return
  /// true on success.
  ///
  /// \p Load is the vector load instruction. Can be either a plain load
  /// instruction or a vp.load intrinsic.
  /// \p Mask is a per-segment (i.e. number of lanes equal to that of one
  /// component being interwoven) mask.  Can be nullptr, in which case the
  /// result is uncondiitional.
  /// \p Shuffles is the shufflevector list to DE-interleave the loaded vector.
  /// \p Indices is the corresponding indices for each shufflevector.
  /// \p Factor is the interleave factor.
  /// \p GapMask is a mask with zeros for components / fields that may not be
  /// accessed.
  ///
  /// @return True if lower an interleaved load to target specific intrinsics. Return true on success.
  ///
  /// \param Load Load instruction or SDNode being queried.
  /// \param Mask Mask operand or value.
  /// \param Shuffles Shuffle uses of the interleaved load.
  /// \param Indices Start indices of each interleaved member.
  /// \param Factor Interleave factor.
  /// \param GapMask Gap mask for interleaved access.
  virtual bool lowerInterleavedLoad(Instruction *Load, Value *Mask,
                                    ArrayRef<ShuffleVectorInst *> Shuffles,
                                    ArrayRef<unsigned> Indices, unsigned Factor,
                                    const APInt &GapMask) const {
    return false;
  }

  /// Lower an interleaved store to target specific intrinsics. Return
  /// true on success.
  ///
  /// \p SI is the vector store instruction.  Can be either a plain store
  /// or a vp.store.
  /// \p Mask is a per-segment (i.e. number of lanes equal to that of one
  /// component being interwoven) mask.  Can be nullptr, in which case the
  /// result is unconditional.
  /// \p SVI is the shufflevector to RE-interleave the stored vector.
  /// \p Factor is the interleave factor.
  /// \p GapMask is a mask with zeros for components / fields that may not be
  /// accessed.
  ///
  /// @return True if lower an interleaved store to target specific intrinsics. Return true on success.
  ///
  /// \param Store Store instruction to lower.
  /// \param Mask Mask operand or value.
  /// \param SVI Shuffle vector instruction.
  /// \param Factor Interleave factor.
  /// \param GapMask Gap mask for interleaved access.
  virtual bool lowerInterleavedStore(Instruction *Store, Value *Mask,
                                     ShuffleVectorInst *SVI, unsigned Factor,
                                     const APInt &GapMask) const {
    return false;
  }

  /// Lower a deinterleave intrinsic to a target specific load intrinsic.
  /// Return true on success. Currently only supports
  /// llvm.vector.deinterleave{2,3,5,7}
  ///
  /// \p Load is the accompanying load instruction.  Can be either a plain load
  /// instruction or a vp.load intrinsic.
  /// \p DI represents the deinterleaveN intrinsic.
  /// \p GapMask is a mask with zeros for components / fields that may not be
  /// accessed.
  ///
  /// @return True if lower a deinterleave intrinsic to a target specific load intrinsic. Return true on success. Currently only supports llvm.vector.deinterleave{2,3,5,7}.
  ///
  /// \param Load Load instruction or SDNode being queried.
  /// \param Mask Mask operand or value.
  /// \param DI Deinterleave intrinsic.
  /// \param GapMask Gap mask for interleaved access.
  virtual bool lowerDeinterleaveIntrinsicToLoad(Instruction *Load, Value *Mask,
                                                IntrinsicInst *DI,
                                                const APInt &GapMask) const {
    return false;
  }

  /// Lower an interleave intrinsic to a target specific store intrinsic.
  /// Return true on success. Currently only supports
  /// llvm.vector.interleave{2,3,5,7}
  ///
  /// \p Store is the accompanying store instruction.  Can be either a plain
  /// store or a vp.store intrinsic.
  /// \p Mask is a per-segment (i.e. number of lanes equal to that of one
  /// component being interwoven) mask.  Can be nullptr, in which case the
  /// result is uncondiitional.
  /// \p InterleaveValues contains the interleaved values.
  /// @return True if lower an interleave intrinsic to a target specific store intrinsic. Return true on success. Currently only supports llvm.vector.interleave{2,3,5,7}.
  ///
  /// \param Store Store instruction to lower.
  /// \param Mask Mask operand or value.
  /// \param InterleaveValues Values to interleave into memory.
  virtual bool
  lowerInterleaveIntrinsicToStore(Instruction *Store, Value *Mask,
                                  ArrayRef<Value *> InterleaveValues) const {
    return false;
  }

  /// Return true if an fpext operation is free (for instance, because
  /// single-precision floating-point numbers are implicitly extended to
  /// double-precision).
  ///
  /// @return True if an fpext operation is free (for instance, because single-precision floating-point numbers are implicitly extended to double-precision).
  ///
  /// \param DestVT Destination value type after promotion.
  /// \param SrcVT Source vector type.
  virtual bool isFPExtFree(EVT DestVT, EVT SrcVT) const {
    assert(SrcVT.isFloatingPoint() && DestVT.isFloatingPoint() &&
           "invalid fpext types");
    return false;
  }

  /// Return true if an fpext feeding this opcode can be folded away, as for an
  /// FMA instruction.
  ///
  /// Return true if an fpext operation input to an \p Opcode operation is free
  /// (for instance, because half-precision floating-point numbers are implicitly
  /// extended to float-precision) for an FMA instruction.
  ///
  /// @return True if an fpext feeding this opcode can be folded away, as for an FMA instruction.
  ///
  /// \param MI Machine instruction being queried or modified.
  /// \param Opcode ISD or target opcode.
  /// \param DestTy Destination type of the FP extend.
  /// \param SrcTy Source type of the FP extend.
  virtual bool isFPExtFoldable(const MachineInstr &MI, unsigned Opcode,
                               LLT DestTy, LLT SrcTy) const {
    return false;
  }

  /// Return true if an fpext feeding this opcode can be folded away, as for an
  /// FMA instruction.
  ///
  /// Return true if an fpext operation input to an \p Opcode operation is free
  /// (for instance, because half-precision floating-point numbers are implicitly
  /// extended to float-precision) for an FMA instruction.
  ///
  /// @return True if an fpext feeding this opcode can be folded away, as for an FMA instruction.
  ///
  /// \param DAG SelectionDAG providing context.
  /// \param Opcode ISD or target opcode.
  /// \param DestVT Destination value type after promotion.
  /// \param SrcVT Source vector type.
  virtual bool isFPExtFoldable(const SelectionDAG &DAG, unsigned Opcode,
                               EVT DestVT, EVT SrcVT) const {
    assert(DestVT.isFloatingPoint() && SrcVT.isFloatingPoint() &&
           "invalid fpext types");
    return isFPExtFree(DestVT, SrcVT);
  }

  /// Return true if folding a vector load into ExtVal (a sign, zero, or any
  /// extend node) is profitable.
  ///
  /// @return True if folding a vector load into ExtVal (a sign, zero, or any extend node) is profitable.
  ///
  /// \param ExtVal Vector load+extend value.
  virtual bool isVectorLoadExtDesirable(SDValue ExtVal) const { return false; }

  /// Return true if an fneg operation is free to the point where it is never
  /// worthwhile to replace it with a bitwise operation.
  ///
  /// @return True if an fneg operation is free to the point where it is never worthwhile to replace it with a bitwise operation.
  ///
  /// \param VT Value type being queried or transformed.
  virtual bool isFNegFree(EVT VT) const {
    assert(VT.isFloatingPoint());
    return false;
  }

  /// Return true if an fabs operation is free to the point where it is never
  /// worthwhile to replace it with a bitwise operation.
  ///
  /// @return True if an fabs operation is free to the point where it is never worthwhile to replace it with a bitwise operation.
  ///
  /// \param VT Value type being queried or transformed.
  virtual bool isFAbsFree(EVT VT) const {
    assert(VT.isFloatingPoint());
    return false;
  }

  /// Return true if an FMA is faster than a separate fmul and fadd for this
  /// type.
  ///
  /// Return true if an FMA operation is faster than a pair of fmul and fadd
  /// instructions. fmuladd intrinsics will be expanded to FMAs when this method
  /// returns true, otherwise fmuladd is expanded to fmul + fadd.
  /// NOTE: This may be called before legalization on types for which FMAs are
  /// not legal, but should return true if those types will eventually legalize
  /// to types that support FMAs. After legalization, it will only be called on
  /// types that support FMAs (via Legal or Custom actions)
  ///
  /// Targets that care about soft float support should return false when soft
  /// float code is being generated (i.e. use-soft-float).
  ///
  /// @return True if an FMA is faster than a separate fmul and fadd for this type.
  ///
  /// \param MF Machine function being lowered.
  /// \param VT Value type being queried or transformed.
  virtual bool isFMAFasterThanFMulAndFAdd(const MachineFunction &MF,
                                          EVT VT) const {
    return false;
  }

  /// Return true if an FMA is faster than a separate fmul and fadd for this
  /// type.
  ///
  /// Return true if an FMA operation is faster than a pair of fmul and fadd
  /// instructions. fmuladd intrinsics will be expanded to FMAs when this method
  /// returns true, otherwise fmuladd is expanded to fmul + fadd.
  /// NOTE: This may be called before legalization on types for which FMAs are
  /// not legal, but should return true if those types will eventually legalize
  /// to types that support FMAs. After legalization, it will only be called on
  /// types that support FMAs (via Legal or Custom actions)
  ///
  /// @return True if an FMA is faster than a separate fmul and fadd for this type.
  ///
  /// \param MF Machine function being lowered.
  /// \param Ty Type of the constant or operation.
  virtual bool isFMAFasterThanFMulAndFAdd(const MachineFunction &MF,
                                          LLT Ty) const {
    return false;
  }

  /// Return true if an FMA is faster than a separate fmul and fadd for this
  /// type.
  ///
  /// @return True if an FMA is faster than a separate fmul and fadd for this type.
  ///
  /// \param F Function or floating-point value type context.
  /// \param Ty Type of the constant or operation.
  virtual bool isFMAFasterThanFMulAndFAdd(const Function &F, Type *Ty) const {
    return false;
  }

  /// Return true if the operation can be combined with another to form an FMAD
  /// node.
  ///
  /// Returns true if \p MI can be combined with another instruction to form
  /// TargetOpcode::G_FMAD. \p N may be an TargetOpcode::G_FADD,
  /// TargetOpcode::G_FSUB, or an TargetOpcode::G_FMUL which will be distributed
  /// into an fadd/fsub.
  ///
  /// @return True if the operation can be combined with another to form an FMAD node.
  ///
  /// \param MI Machine instruction being queried or modified.
  /// \param Ty Type of the constant or operation.
  virtual bool isFMADLegal(const MachineInstr &MI, LLT Ty) const {
    assert((MI.getOpcode() == TargetOpcode::G_FADD ||
            MI.getOpcode() == TargetOpcode::G_FSUB ||
            MI.getOpcode() == TargetOpcode::G_FMUL) &&
           "unexpected node in FMAD forming combine");
    switch (Ty.getScalarSizeInBits()) {
    case 16:
      return isOperationLegal(TargetOpcode::G_FMAD, MVT::f16);
    case 32:
      return isOperationLegal(TargetOpcode::G_FMAD, MVT::f32);
    case 64:
      return isOperationLegal(TargetOpcode::G_FMAD, MVT::f64);
    default:
      break;
    }

    return false;
  }

  /// Return true if the operation can be combined with another to form an FMAD
  /// node.
  ///
  /// Returns true if be combined with to form an ISD::FMAD. \p N may be an
  /// ISD::FADD, ISD::FSUB, or an ISD::FMUL which will be distributed into an
  /// fadd/fsub.
  ///
  /// @return True if the operation can be combined with another to form an FMAD node.
  ///
  /// \param DAG SelectionDAG providing context.
  /// \param N SDNode being queried.
  virtual bool isFMADLegal(const SelectionDAG &DAG, const SDNode *N) const {
    assert((N->getOpcode() == ISD::FADD || N->getOpcode() == ISD::FSUB ||
            N->getOpcode() == ISD::FMUL) &&
           "unexpected node in FMAD forming combine");
    return isOperationLegal(ISD::FMAD, N->getValueType(0));
  }

  /// Return true if forming FMAs should be delegated to the machine combiner
  /// instead of done during selection.
  ///
  /// @return True if forming FMAs should be delegated to the machine combiner instead of done during selection.
  ///
  /// \param VT Value type being queried or transformed.
  /// \param OptLevel Optimization level guiding the decision.
  virtual bool generateFMAsInMachineCombiner(EVT VT,
                                             CodeGenOptLevel OptLevel) const {
    return false;
  }

  /// Return true if it's profitable to narrow operations of type SrcVT to
  /// DestVT. e.g. on x86, it's profitable to narrow from i32 to i8 but not from
  /// i32 to i16.
  ///
  /// @return True if it's profitable to narrow operations of type SrcVT to DestVT. e.g. on x86, it's profitable to narrow from i32 to i8 but not from i32 to i16.
  ///
  /// \param N SDNode being queried.
  /// \param SrcVT Source vector type.
  /// \param DestVT Destination value type after promotion.
  virtual bool isNarrowingProfitable(SDNode *N, EVT SrcVT, EVT DestVT) const {
    return false;
  }

  /// Return true if folding a binary op into a select with an identity constant
  /// is profitable.
  ///
  /// Return true if pulling a binary operation into a select with an identity
  /// constant is profitable. This is the inverse of an IR transform. Example: X
  /// + (Cond ? Y : 0) --> Cond ? (X + Y) : X
  ///
  /// @return True if folding a binary op into a select with an identity constant is profitable.
  ///
  /// \param BinOpcode Opcode of the binary operation.
  /// \param VT Value type being queried or transformed.
  /// \param SelectOpcode Opcode of the select node.
  /// \param X Non-identity operand of the select fold.
  /// \param Y Possibly-identity operand of the select fold.
  virtual bool shouldFoldSelectWithIdentityConstant(unsigned BinOpcode, EVT VT,
                                                    unsigned SelectOpcode,
                                                    SDValue X,
                                                    SDValue Y) const {
    return false;
  }

  /// Return true if replacing a load of this constant with an immediate is
  /// beneficial.
  ///
  /// Return true if it is beneficial to convert a load of a constant to just the
  /// constant itself. On some targets it might be more efficient to use a
  /// combination of arithmetic instructions to materialize the constant instead
  /// of loading it from a constant pool.
  ///
  /// @return True if replacing a load of this constant with an immediate is beneficial.
  ///
  /// \param Imm Immediate constant operand.
  /// \param Ty Type of the constant or operation.
  virtual bool shouldConvertConstantLoadToIntImm(const APInt &Imm,
                                                 Type *Ty) const {
    return false;
  }

  /// Return the cost of extracting a subvector of type \p ResVT from a vector
  /// of type \p SrcVT, starting at element \p Index.
  ///
  /// Most callers only create a new EXTRACT_SUBVECTOR when the cost is at most
  /// ExtractSubvectorCost::Cheap. This hook exists because EXTRACT_SUBVECTOR
  /// usually has custom lowering that depends on the index of the first
  /// element, so only the target knows which lowering is cheap.
  ///
  /// @return The cost of extracting a subvector of type \p ResVT from a vector of type \p SrcVT, starting at element \p Index.
  ///
  /// \param ResVT Result type of the extract.
  /// \param SrcVT Source vector type.
  /// \param Index Lane or subvector index.
  virtual ExtractSubvectorCost getExtractSubvectorCost(EVT ResVT, EVT SrcVT,
                                                       unsigned Index) const {
    return ExtractSubvectorCost::Expensive;
  }

  /// Try to convert an extract element of a vector binary operation into an
  /// extract element followed by a scalar operation.
  ///
  /// @return True if try to convert an extract element of a vector binary operation into an extract element followed by a scalar operation.
  ///
  /// \param VecOp Vector binop considered for scalarization.
  virtual bool shouldScalarizeBinop(SDValue VecOp) const {
    return false;
  }

  /// Return true if extracting a scalar element at the given index from this
  /// vector type is cheap.
  ///
  /// Return true if extraction of a scalar element from the given vector type at
  /// the given index is cheap. For example, if scalar operations occur on the
  /// same register file as vector operations, then an extract element may be a
  /// sub-register rename rather than an actual instruction.
  ///
  /// @return True if extracting a scalar element at the given index from this vector type is cheap.
  ///
  /// \param VT Value type being queried or transformed.
  /// \param Index Lane or subvector index.
  virtual bool isExtractVecEltCheap(EVT VT, unsigned Index) const {
    return false;
  }

  /// Return true if an overflow-checked math operation should be formed for the
  /// given opcode and type.
  ///
  /// Try to convert math with an overflow comparison into the corresponding DAG
  /// node operation. Targets may want to override this independently of whether
  /// the operation is legal/custom for the given type because it may obscure
  /// matching of other patterns.
  ///
  /// @return True if an overflow-checked math operation should be formed for the given opcode and type.
  ///
  /// \param Opcode ISD or target opcode.
  /// \param VT Value type being queried or transformed.
  /// \param MathUsed Whether the non-overflow result is used.
  virtual bool shouldFormOverflowOp(unsigned Opcode, EVT VT,
                                    bool MathUsed) const {
    // Form it if it is legal.
    if (isOperationLegal(Opcode, VT))
      return true;

    // TODO: The default logic is inherited from code in CodeGenPrepare.
    // The opcode should not make a difference by default?
    if (Opcode != ISD::UADDO)
      return false;

    // Allow the transform as long as we have an integer type that is not
    // obviously illegal and unsupported and if the math result is used
    // besides the overflow check. On some targets (e.g. SPARC), it is
    // not profitable to form on overflow op if the math result has no
    // concrete users.
    if (VT.isVector())
      return false;
    return MathUsed && (VT.isSimple() || !isOperationExpand(Opcode, VT));
  }

  /// Return true if the multiply-with-overflow intrinsic should be optimized
  /// using known zero high bits.
  ///
  /// @return True if the multiply-with-overflow intrinsic should be optimized using known zero high bits.
  ///
  /// \param Context LLVMContext used for type creation.
  /// \param VT Value type being queried or transformed.
  virtual bool shouldOptimizeMulOverflowWithZeroHighBits(LLVMContext &Context,
                                                         EVT VT) const {
    return false;
  }

  /// Return true if it is profitable to use a scalar input to a BUILD_VECTOR
  /// even with multiple uses.
  ///
  /// @return True if it is profitable to use a scalar input to a BUILD_VECTOR even with multiple uses.
  ///
  /// \param VecVT Vector value type.
  virtual bool aggressivelyPreferBuildVectorSources(EVT VecVT) const {
    return false;
  }

  /// Return true if CodeGenPrepare should consider splitting a large GEP offset
  /// to fit the addressing mode.
  ///
  /// @return True if CodeGenPrepare should consider splitting a large GEP offset to fit the addressing mode.
  virtual bool shouldConsiderGEPOffsetSplit() const { return false; }

  /// Return true if creating a shift of the type by the given
  /// amount is not profitable.
  ///
  /// @return True if creating a shift of the type by the given amount is not profitable.
  ///
  /// \param VT Value type being queried or transformed.
  /// \param Amount Shift amount being considered.
  virtual bool shouldAvoidTransformToShift(EVT VT, unsigned Amount) const {
    return false;
  }

  /// Return true if a select on a single-bit AND test should be folded into a
  /// shift sequence.
  ///
  /// @return True if a select on a single-bit AND test should be folded into a shift sequence.
  ///
  /// \param VT Value type being queried or transformed.
  /// \param AndMask Mask applied by the and operation in the pattern.
  virtual bool shouldFoldSelectWithSingleBitTest(EVT VT,
                                                 const APInt &AndMask) const {
    unsigned ShCt = AndMask.getBitWidth() - 1;
    return !shouldAvoidTransformToShift(VT, ShCt);
  }

  /// Does this target require the clearing of high-order bits in a register
  /// passed to the fp16 to fp conversion library function.
  ///
  /// @return True if does this target require the clearing of high-order bits in a register passed to the fp16 to fp conversion library function.
  virtual bool shouldKeepZExtForFP16Conv() const { return false; }

  /// Return true if a saturating fp-to-int conversion should be generated for
  /// the given types.
  ///
  /// Should we generate fp_to_si_sat and fp_to_ui_sat from type FPVT to type VT.
  /// Used when folding idioms into a saturating fp-to-int conversion, such as
  /// min(max(fptoi)) clamps or NaN-guarded selects.
  ///
  /// @return True if a saturating fp-to-int conversion should be generated for the given types.
  ///
  /// \param Op SDNode or value being queried.
  /// \param FPVT Floating-point source value type.
  /// \param VT Value type being queried or transformed.
  virtual bool shouldConvertFpToSat(unsigned Op, EVT FPVT, EVT VT) const {
    return isOperationLegalOrCustom(Op, VT);
  }

  /// Should we prefer selects to doing arithmetic on boolean types
  ///
  /// @return True if should we prefer selects to doing arithmetic on boolean types.
  ///
  /// \param VT Value type being queried or transformed.
  virtual bool preferSelectsOverBooleanArithmetic(EVT VT) const {
    return false;
  }

  /// Return true if pointer arithmetic semantics should be preserved for this
  /// pointer type.
  ///
  /// True if target has some particular form of dealing with pointer arithmetic
  /// semantics for pointers with the given value type. False if pointer
  /// arithmetic should not be preserved for passes such as instruction
  /// selection, and can fallback to regular arithmetic. This should be removed
  /// when PTRADD nodes are widely supported by backends.
  ///
  /// @return True if pointer arithmetic semantics should be preserved for this pointer type.
  ///
  /// \param F Function or floating-point value type context.
  /// \param PtrVT Pointer value type.
  virtual bool shouldPreservePtrArith(const Function &F, EVT PtrVT) const {
    return false;
  }

  /// True if the target allows transformations of in-bounds pointer
  /// arithmetic that cause out-of-bounds intermediate results.
  ///
  /// @return True if the target allows transformations of in-bounds pointer arithmetic that cause out-of-bounds intermediate results.
  ///
  /// \param F Function or floating-point value type context.
  /// \param PtrVT Pointer value type.
  virtual bool canTransformPtrArithOutOfBounds(const Function &F,
                                               EVT PtrVT) const {
    return false;
  }

  /// Does this target support complex deinterleaving
  ///
  /// @return True if does this target support complex deinterleaving.
  virtual bool isComplexDeinterleavingSupported() const { return false; }

  /// Does this target support complex deinterleaving with the given operation
  /// and type
  ///
  /// @return True if does this target support complex deinterleaving with the given operation and type.
  ///
  /// \param Operation Complex deinterleaving operation kind.
  /// \param Ty Type of the constant or operation.
  virtual bool isComplexDeinterleavingOperationSupported(
      ComplexDeinterleavingOperation Operation, Type *Ty) const {
    return false;
  }

  /// Return the preferred FP-to-integer conversion opcode to use as a
  /// replacement for an illegal one.
  ///
  /// @return The preferred FP-to-integer conversion opcode to use as a replacement for an illegal one.
  ///
  /// \param Op SDNode or value being queried.
  /// \param FromVT Source value type.
  /// \param ToVT Destination value type.
  virtual unsigned getPreferredFPToIntOpcode(unsigned Op, EVT FromVT,
                                             EVT ToVT) const {
    if (isOperationLegal(Op, ToVT))
      return Op;
    switch (Op) {
    case ISD::FP_TO_UINT:
      if (isOperationLegalOrCustom(ISD::FP_TO_SINT, ToVT))
        return ISD::FP_TO_SINT;
      break;
    case ISD::STRICT_FP_TO_UINT:
      if (isOperationLegalOrCustom(ISD::STRICT_FP_TO_SINT, ToVT))
        return ISD::STRICT_FP_TO_SINT;
      break;
    default:
      break;
    }
    return Op;
  }

  /// Create the IR node for the given complex deinterleaving operation.
  /// If one cannot be created using all the given inputs, nullptr should be
  /// returned.
  ///
  /// @return A pointer to the requested object, or null if unavailable.
  ///
  /// \param B IR builder used to create the operation.
  /// \param OperationType Complex deinterleaving operation to emit.
  /// \param Rotation Rotation applied to the complex operands.
  /// \param InputA First complex input value.
  /// \param InputB Second complex input value.
  /// \param Accumulator Optional accumulator input, or null if unused.
  virtual Value *createComplexDeinterleavingIR(
      IRBuilderBase &B, ComplexDeinterleavingOperation OperationType,
      ComplexDeinterleavingRotation Rotation, Value *InputA, Value *InputB,
      Value *Accumulator = nullptr) const {
    return nullptr;
  }

  /// Return the runtime libcalls info for this target.
  ///
  /// @return The runtime libcalls info for this target.
  const RTLIB::RuntimeLibcallsInfo &getRuntimeLibcallsInfo() const {
    return RuntimeLibcallInfo;
  }

  /// Return the libcall lowering info for this target.
  ///
  /// @return The libcall lowering info for this target.
  const LibcallLoweringInfo &getLibcallLoweringInfo() const { return Libcalls; }

  /// Set the libcall implementation to use for the given libcall.
  ///
  /// \param Call Libcall identifier or call instruction.
  /// \param Impl Runtime libcall implementation identifier.
  void setLibcallImpl(RTLIB::Libcall Call, RTLIB::LibcallImpl Impl) {
    Libcalls.setLibcallImpl(Call, Impl);
  }

  /// Get the libcall impl routine name for the specified libcall.
  ///
  /// @return Get the libcall impl routine name for the specified libcall.
  ///
  /// \param Call Libcall identifier or call instruction.
  RTLIB::LibcallImpl getLibcallImpl(RTLIB::Libcall Call) const {
    return Libcalls.getLibcallImpl(Call);
  }

  /// Get the libcall routine name for the specified libcall.
  // FIXME: This should be removed. Only LibcallImpl should have a name.
  /// Return the libcall routine name for the specified libcall.
  ///
  /// @return The libcall routine name for the specified libcall.
  ///
  /// \param Call Libcall identifier.
  const char *getLibcallName(RTLIB::Libcall Call) const {
    return Libcalls.getLibcallName(Call);
  }

  /// Get the libcall routine name for the specified libcall implementation
  ///
  /// @return The string value.
  ///
  /// \param Call Libcall identifier or call instruction.
  static StringRef getLibcallImplName(RTLIB::LibcallImpl Call) {
    return RTLIB::RuntimeLibcallsInfo::getLibcallImplName(Call);
  }

  /// Return the libcall implementation to use for memcpy.
  ///
  /// @return The libcall implementation to use for memcpy.
  RTLIB::LibcallImpl getMemcpyImpl() const { return Libcalls.getMemcpyImpl(); }

  /// Check if this is valid libcall for the current module, otherwise
  /// RTLIB::Unsupported.
  ///
  /// @return The string value.
  ///
  /// \param FuncName Libcall implementation function name.
  RTLIB::LibcallImpl getSupportedLibcallImpl(StringRef FuncName) const {
    return RuntimeLibcallInfo.getSupportedLibcallImpl(FuncName);
  }

  /// Get the CallingConv that should be used for the specified libcall
  /// implementation.
  ///
  /// @return Get the CallingConv that should be used for the specified libcall implementation.
  ///
  /// \param Call Libcall identifier or call instruction.
  CallingConv::ID getLibcallImplCallingConv(RTLIB::LibcallImpl Call) const {
    return Libcalls.getLibcallImplCallingConv(Call);
  }

  /// Get the CallingConv that should be used for the specified libcall.
  /// Return the libcall Calling Conv.
  ///
  /// @return Get the CallingConv that should be used for the specified libcall. Return the libcall Calling Conv.
  ///
  /// \param Call Libcall identifier or call instruction.
  CallingConv::ID getLibcallCallingConv(RTLIB::Libcall Call) const {
    return Libcalls.getLibcallCallingConv(Call);
  }

  /// Perform target-specific finalization after instruction selection, such as
  /// freezing reserved registers.
  ///
  /// Execute target specific actions to finalize target lowering. This is used
  /// to set extra flags in MachineFrameInformation and freezing the set of
  /// reserved registers. The default implementation just freezes the set of
  /// reserved registers.
  ///
  /// \param MF Machine function being lowered.
  virtual void finalizeLowering(MachineFunction &MF) const;

  /// Returns true if it's profitable to allow merging store of loads when there
  /// are functions calls between the load and the store.
  ///
  /// @return True if it's profitable to allow merging store of loads when there are functions calls between the load and the store.
  ///
  /// \param StoreVT VT of the merged store.
  /// \param LoadVT VT of the loads being merged.
  virtual bool shouldMergeStoreOfLoadsOverCall(EVT StoreVT, EVT LoadVT) const { return true; }

  //===----------------------------------------------------------------------===//
  //  GlobalISel Hooks
  //===----------------------------------------------------------------------===//
  /// Check whether or not \p MI needs to be moved close to its uses.
  ///
  /// @return True if check whether or not \p MI needs to be moved close to its uses.
  ///
  /// \param MI Machine instruction being queried or modified.
  /// \param TTI Target transform info.
  virtual bool shouldLocalize(const MachineInstr &MI, const TargetTransformInfo *TTI) const;


private:
  const TargetMachine &TM;

  /// Tells the code generator that the target has BitExtract instructions.
  /// The code generator will aggressively sink "shift"s into the blocks of
  /// their users if the users will generate "and" instructions which can be
  /// combined with "shift" to BitExtract instructions.
  bool HasExtractBitsInsn;

  /// Tells the code generator to bypass slow divide or remainder
  /// instructions. For example, BypassSlowDivWidths[32,8] tells the code
  /// generator to bypass 32-bit integer div/rem with an 8-bit unsigned integer
  /// div/rem when the operands are positive and less than 256.
  DenseMap <unsigned int, unsigned int> BypassSlowDivWidths;

  /// Tells the code generator that it shouldn't generate extra flow control
  /// instructions and should attempt to combine flow control instructions via
  /// predication.
  bool JumpIsExpensive;

  /// Information about the contents of the high-bits in boolean values held in
  /// a type wider than i1. See getBooleanContents.
  BooleanContent BooleanContents;

  /// Information about the contents of the high-bits in boolean values held in
  /// a type wider than i1. See getBooleanContents.
  BooleanContent BooleanFloatContents;

  /// Information about the contents of the high-bits in boolean vector values
  /// when the element type is wider than i1. See getBooleanContents.
  BooleanContent BooleanVectorContents;

  /// The target scheduling preference: shortest possible total cycles or lowest
  /// register usage.
  Sched::Preference SchedPreferenceInfo;

  /// The minimum alignment that any argument on the stack needs to have.
  Align MinStackArgumentAlignment;

  /// The minimum function alignment (used when optimizing for size, and to
  /// prevent explicitly provided alignment from leading to incorrect code).
  Align MinFunctionAlignment;

  /// The preferred function alignment (used when alignment unspecified and
  /// optimizing for speed).
  Align PrefFunctionAlignment;

  /// The preferred loop alignment (in log2 bot in bytes).
  Align PrefLoopAlignment;
  /// The maximum amount of bytes permitted to be emitted for alignment.
  unsigned MaxBytesForAlignment;

  /// Size in bits of the maximum atomics size the backend supports.
  /// Accesses larger than this will be expanded by AtomicExpandPass.
  unsigned MaxAtomicSizeInBitsSupported;

  /// Size in bits of the maximum div/rem size the backend supports.
  /// Larger operations will be expanded by ExpandIRInsts.
  unsigned MaxDivRemBitWidthSupported;

  /// Size in bits of the maximum fp to/from int conversion size the
  /// backend supports. Larger operations will be expanded by
  /// ExpandIRInsts.
  unsigned MaxLargeFPConvertBitWidthSupported;

  /// Size in bits of the minimum cmpxchg or ll/sc operation the
  /// backend supports.
  unsigned MinCmpXchgSizeInBits;

  /// The minimum of largest number of comparisons to use bit test for switch.
  unsigned MinimumBitTestCmps;

  /// Maximum known-legal store size, which can be guaranteed for scalable
  /// vectors.
  unsigned MaximumLegalStoreInBits;

  /// This indicates if the target supports unaligned atomic operations.
  bool SupportsUnalignedAtomics;

  /// If set to a physical register, this specifies the register that
  /// llvm.savestack/llvm.restorestack should save and restore.
  Register StackPointerRegisterToSaveRestore;

  /// This indicates the default register class to use for each ValueType the
  /// target supports natively.
  const TargetRegisterClass *RegClassForVT[MVT::VALUETYPE_SIZE];
  uint16_t NumRegistersForVT[MVT::VALUETYPE_SIZE];
  MVT RegisterTypeForVT[MVT::VALUETYPE_SIZE];

  /// This indicates the "representative" register class to use for each
  /// ValueType the target supports natively. This information is used by the
  /// scheduler to track register pressure. By default, the representative
  /// register class is the largest legal super-reg register class of the
  /// register class of the specified type. e.g. On x86, i8, i16, and i32's
  /// representative class would be GR32.
  const TargetRegisterClass *RepRegClassForVT[MVT::VALUETYPE_SIZE] = {nullptr};

  /// This indicates the "cost" of the "representative" register class for each
  /// ValueType. The cost is used by the scheduler to approximate register
  /// pressure.
  uint8_t RepRegClassCostForVT[MVT::VALUETYPE_SIZE];

  /// For any value types we are promoting or expanding, this contains the value
  /// type that we are changing to.  For Expanded types, this contains one step
  /// of the expand (e.g. i64 -> i32), even if there are multiple steps required
  /// (e.g. i64 -> i16).  For types natively supported by the system, this holds
  /// the same type (e.g. i32 -> i32).
  MVT TransformToType[MVT::VALUETYPE_SIZE];

  /// For each operation and each value type, keep a LegalizeAction that
  /// indicates how instruction selection should deal with the operation.  Most
  /// operations are Legal (aka, supported natively by the target), but
  /// operations that are not should be described.  Note that operations on
  /// non-legal value types are not described here.
  LegalizeAction OpActions[MVT::VALUETYPE_SIZE][ISD::BUILTIN_OP_END];

  /// For each load extension type and each value type, keep a LegalizeAction
  /// that indicates how instruction selection should deal with a load of a
  /// specific value type and extension type. Uses 4-bits to store the action
  /// for each of the 4 load ext types.
  uint16_t LoadExtActions[MVT::VALUETYPE_SIZE][MVT::VALUETYPE_SIZE];

  /// Similar to LoadExtActions, but for atomic loads. Only Legal or Expand
  /// (default) values are supported.
  uint16_t AtomicLoadExtActions[MVT::VALUETYPE_SIZE][MVT::VALUETYPE_SIZE];

  /// For each value type pair keep a LegalizeAction that indicates whether a
  /// truncating store of a specific value type and truncating type is legal.
  LegalizeAction TruncStoreActions[MVT::VALUETYPE_SIZE][MVT::VALUETYPE_SIZE];

  /// For each indexed mode and each value type, keep a quad of LegalizeAction
  /// that indicates how instruction selection should deal with the load /
  /// store / maskedload / maskedstore.
  ///
  /// The first dimension is the value_type for the reference. The second
  /// dimension represents the various modes for load store.
  uint16_t IndexedModeActions[MVT::VALUETYPE_SIZE][ISD::LAST_INDEXED_MODE];

  /// For each condition code (ISD::CondCode) keep a LegalizeAction that
  /// indicates how instruction selection should deal with the condition code.
  ///
  /// Because each CC action takes up 4 bits, we need to have the array size be
  /// large enough to fit all of the value types. This can be done by rounding
  /// up the MVT::VALUETYPE_SIZE value to the next multiple of 8.
  uint32_t CondCodeActions[ISD::SETCC_INVALID][(MVT::VALUETYPE_SIZE + 7) / 8];

  using PartialReduceActionTypes =
      std::tuple<unsigned, MVT::SimpleValueType, MVT::SimpleValueType>;
  /// For each partial reduce opcode, result type and input type combination,
  /// keep a LegalizeAction which indicates how instruction selection should
  /// deal with this operation.
  DenseMap<PartialReduceActionTypes, LegalizeAction> PartialReduceMLAActions;

  ValueTypeActionImpl ValueTypeActions;

private:
  /// Targets can specify ISD nodes that they would like PerformDAGCombine
  /// callbacks for by calling setTargetDAGCombine(), which sets a bit in this
  /// array.
  unsigned char
  TargetDAGCombineArray[(ISD::BUILTIN_OP_END+CHAR_BIT-1)/CHAR_BIT];

  /// For operations that must be promoted to a specific type, this holds the
  /// destination type.  This map should be sparse, so don't hold it as an
  /// array.
  ///
  /// Targets add entries to this map with AddPromotedToType(..), clients access
  /// this with getTypeToPromoteTo(..).
  std::map<std::pair<unsigned, MVT::SimpleValueType>, MVT::SimpleValueType>
    PromoteToType;

  /// FIXME: This should not live here; it should come from an analysis.
  const RTLIB::RuntimeLibcallsInfo RuntimeLibcallInfo;

  /// The list of libcalls that the target will use.
  /// FIXME: This should not live here; it should come from an analysis.
  LibcallLoweringInfo Libcalls;

  /// The bits of IndexedModeActions used to store the legalisation actions
  /// We store the data as   | ML | MS |  L |  S | each taking 4 bits.
  enum IndexedModeActionsBits {
    IMAB_Store = 0,
    IMAB_Load = 4,
    IMAB_MaskedStore = 8,
    IMAB_MaskedLoad = 12
  };

  void setIndexedModeAction(unsigned IdxMode, MVT VT, unsigned Shift,
                            LegalizeAction Action) {
    assert(VT.isValid() && IdxMode < ISD::LAST_INDEXED_MODE &&
           (unsigned)Action < 0xf && "Table isn't big enough!");
    unsigned Ty = (unsigned)VT.SimpleTy;
    IndexedModeActions[Ty][IdxMode] &= ~(0xf << Shift);
    IndexedModeActions[Ty][IdxMode] |= ((uint16_t)Action) << Shift;
  }

  LegalizeAction getIndexedModeAction(unsigned IdxMode, MVT VT,
                                      unsigned Shift) const {
    assert(IdxMode < ISD::LAST_INDEXED_MODE && VT.isValid() &&
           "Table isn't big enough!");
    unsigned Ty = (unsigned)VT.SimpleTy;
    return (LegalizeAction)((IndexedModeActions[Ty][IdxMode] >> Shift) & 0xf);
  }

  unsigned getVectorTypeBreakdownImpl(LLVMContext &Context, EVT VT,
                                      EVT &IntermediateVT,
                                      unsigned &NumIntermediates,
                                      MVT &RegisterVT,
                                      bool ForCallingConv) const;

  unsigned getVectorTypeBreakdownMVT(MVT VT, MVT &IntermediateVT,
                                     unsigned &NumIntermediates,
                                     MVT &RegisterVT);

  /// Return the type of registers that this ValueType will eventually require.
  MVT getCachedRegisterType(MVT VT) const {
    assert((unsigned)VT.SimpleTy < std::size(RegisterTypeForVT));
    return RegisterTypeForVT[VT.SimpleTy];
  }

  MVT getRegisterTypeImpl(LLVMContext &Context, EVT VT,
                          bool ForCallingConv) const {
    if (VT.isSimple() &&
        !shouldUseDynamicVectorTypeBreakdown(VT, ForCallingConv))
      return getCachedRegisterType(VT.getSimpleVT());
    if (VT.isVector()) {
      EVT VT1;
      MVT RegisterVT;
      unsigned NumIntermediates;
      (void)getVectorTypeBreakdownImpl(Context, VT, VT1, NumIntermediates,
                                       RegisterVT, ForCallingConv);
      return RegisterVT;
    }
    if (VT.isInteger()) {
      return getRegisterTypeImpl(Context, getTypeToTransformTo(Context, VT),
                                 ForCallingConv);
    }
    llvm_unreachable("Unsupported extended type!");
  }

  unsigned getNumRegistersImpl(LLVMContext &Context, EVT VT,
                               bool ForCallingConv) const {
    if (VT.isSimple() &&
        !shouldUseDynamicVectorTypeBreakdown(VT, ForCallingConv)) {
      assert((unsigned)VT.getSimpleVT().SimpleTy <
             std::size(NumRegistersForVT));
      return NumRegistersForVT[VT.getSimpleVT().SimpleTy];
    }
    if (VT.isVector()) {
      EVT VT1;
      MVT VT2;
      unsigned NumIntermediates;
      return getVectorTypeBreakdownImpl(Context, VT, VT1, NumIntermediates, VT2,
                                        ForCallingConv);
    }
    if (VT.isInteger()) {
      unsigned BitWidth = VT.getSizeInBits();
      unsigned RegWidth =
          getRegisterTypeImpl(Context, VT, ForCallingConv).getSizeInBits();
      return (BitWidth + RegWidth - 1) / RegWidth;
    }
    llvm_unreachable("Unsupported extended type!");
  }

protected:
  /// Return true if the extension represented by \p I is free.
  /// \pre \p I is a sign, zero, or fp extension and
  ///      is[Z|FP]ExtFree of the related types is not true.
  ///
  /// @return True if the extension represented by \p I is free.
  ///
  /// \param I Instruction being queried.
  virtual bool isExtFreeImpl(const Instruction *I) const { return false; }

  /// The maximum depth to search for alias-chain dependencies when merging
  /// stores.
  ///
  /// Depth that GatherAllAliases should continue looking for chain dependencies
  /// when trying to find a more preferable chain. As an approximation, this
  /// should be more than the number of consecutive stores expected to be merged.
  unsigned GatherAllAliasesMaxDepth;

  /// \brief Specify maximum number of store instructions per memset call.
  ///
  /// When lowering \@llvm.memset this field specifies the maximum number of
  /// store operations that may be substituted for the call to memset. Targets
  /// must set this value based on the cost threshold for that target. Targets
  /// should assume that the memset will be done using as many of the largest
  /// store operations first, followed by smaller ones, if necessary, per
  /// alignment restrictions. For example, storing 9 bytes on a 32-bit machine
  /// with 16-bit alignment would result in four 2-byte stores and one 1-byte
  /// store.  This only applies to setting a constant array of a constant size.
  unsigned MaxStoresPerMemset;
  /// Likewise for functions with the OptSize attribute.
  unsigned MaxStoresPerMemsetOptSize;

  /// \brief Specify maximum number of store instructions per memcpy call.
  ///
  /// When lowering \@llvm.memcpy this field specifies the maximum number of
  /// store operations that may be substituted for a call to memcpy. Targets
  /// must set this value based on the cost threshold for that target. Targets
  /// should assume that the memcpy will be done using as many of the largest
  /// store operations first, followed by smaller ones, if necessary, per
  /// alignment restrictions. For example, storing 7 bytes on a 32-bit machine
  /// with 32-bit alignment would result in one 4-byte store, a one 2-byte store
  /// and one 1-byte store. This only applies to copying a constant array of
  /// constant size.
  unsigned MaxStoresPerMemcpy;
  /// Likewise for functions with the OptSize attribute.
  unsigned MaxStoresPerMemcpyOptSize;
  /// \brief Specify max number of store instructions to glue in inlined memcpy.
  ///
  /// When memcpy is inlined based on MaxStoresPerMemcpy, specify maximum number
  /// of store instructions to keep together. This helps in pairing and
  //  vectorization later on.
  unsigned MaxGluedStoresPerMemcpy = 0;

  /// \brief Specify maximum number of load instructions per memcmp call.
  ///
  /// When lowering \@llvm.memcmp this field specifies the maximum number of
  /// pairs of load operations that may be substituted for a call to memcmp.
  /// Targets must set this value based on the cost threshold for that target.
  /// Targets should assume that the memcmp will be done using as many of the
  /// largest load operations first, followed by smaller ones, if necessary, per
  /// alignment restrictions. For example, loading 7 bytes on a 32-bit machine
  /// with 32-bit alignment would result in one 4-byte load, a one 2-byte load
  /// and one 1-byte load. This only applies to copying a constant array of
  /// constant size.
  unsigned MaxLoadsPerMemcmp;
  /// Likewise for functions with the OptSize attribute.
  unsigned MaxLoadsPerMemcmpOptSize;

  /// \brief Specify maximum number of store instructions per memmove call.
  ///
  /// When lowering \@llvm.memmove this field specifies the maximum number of
  /// store instructions that may be substituted for a call to memmove. Targets
  /// must set this value based on the cost threshold for that target. Targets
  /// should assume that the memmove will be done using as many of the largest
  /// store operations first, followed by smaller ones, if necessary, per
  /// alignment restrictions. For example, moving 9 bytes on a 32-bit machine
  /// with 8-bit alignment would result in nine 1-byte stores.  This only
  /// applies to copying a constant array of constant size.
  unsigned MaxStoresPerMemmove;
  /// Likewise for functions with the OptSize attribute.
  unsigned MaxStoresPerMemmoveOptSize;

  /// Tells the code generator that select is more expensive than a branch if
  /// the branch is usually predicted right.
  bool PredictableSelectIsExpensive;

  /// Whether extending-load promotion through chains of promotable instructions
  /// is enabled.
  bool EnableExtLdPromotion;

  /// Return true if the value types that can be represented by the specified
  /// register class are all legal.
  ///
  /// @return True if the value types that can be represented by the specified register class are all legal.
  ///
  /// \param TRI Target register info.
  /// \param RC Register class.
  bool isLegalRC(const TargetRegisterInfo &TRI,
                 const TargetRegisterClass &RC) const;

  /// Replace/modify any TargetFrameIndex operands with a targte-dependent
  /// sequence of memory operands that is recognized by PrologEpilogInserter.
  ///
  /// @return A pointer to the requested object, or null if unavailable.
  ///
  /// \param MI Patchpoint machine instruction being rewritten.
  /// \param MBB Machine basic block containing \p MI.
  MachineBasicBlock *emitPatchPoint(MachineInstr &MI,
                                    MachineBasicBlock *MBB) const;

  /// Whether the target supports strict floating-point operations.
  bool IsStrictFPEnabled;
};

/// This class defines information used to lower LLVM code to legal SelectionDAG
/// operators that the target instruction selector can accept natively.
///
/// This class also defines callbacks that targets must implement to lower
/// target-specific constructs to SelectionDAG operators.
class LLVM_ABI TargetLowering : public TargetLoweringBase {
public:
  /// Holds context passed to target DAG-combine hooks, including the DAG and
  /// combine level.
  struct DAGCombinerInfo;
  /// Holds options that control how a call to a runtime library function is
  /// generated.
  struct MakeLibCallOptions;

  /// Deleted copy constructor.
  ///
  /// \param RHS Source object (deleted).
  TargetLowering(const TargetLowering &RHS) = delete;
  /// Deleted copy-assignment operator.
  ///
  /// \param RHS Source object (deleted).
  TargetLowering &operator=(const TargetLowering &RHS) = delete;

  /// Defines the interface used to lower LLVM IR and SelectionDAG constructs to
  /// target-specific machine code.
  ///
  /// \param TM Target machine.
  /// \param STI Target subtarget info.
  explicit TargetLowering(const TargetMachine &TM,
                          const TargetSubtargetInfo &STI);
  /// Virtual destructor.
  ~TargetLowering() override;

  /// Return true if code should be generated as position-independent.
  ///
  /// @return True if code should be generated as position-independent.
  bool isPositionIndependent() const;

  /// Return true if SelectionDAG nodes should be consistently processed in
  /// topological order.
  ///
  /// @return True if SelectionDAG nodes should be consistently processed in topological order.
  virtual bool useTopologicalSorting() const { return false; }

  /// Return true if the given node is a source of divergence across threads.
  ///
  /// @return True if the given node is a source of divergence across threads.
  ///
  /// \param N SDNode being queried.
  /// \param FLI Function lowering info for fast ISel.
  /// \param UA Uniformity analysis used for divergence queries.
  virtual bool isSDNodeSourceOfDivergence(const SDNode *N,
                                          FunctionLoweringInfo *FLI,
                                          UniformityInfo *UA) const {
    return false;
  }

  /// Return true if reassociating an operand chain rooted at N0 with N1 is
  /// profitable.
  ///
  /// @return True if reassociating an operand chain rooted at N0 with N1 is profitable.
  ///
  /// \param DAG SelectionDAG providing context.
  /// \param N0 First operand of the setcc.
  /// \param N1 Second operand of the setcc.
  virtual bool isReassocProfitable(SelectionDAG &DAG, SDValue N0,
                                   SDValue N1) const {
    return N0.hasOneUse();
  }

  /// Return true if reassociating an operand chain rooted at N0 with N1 is
  /// profitable.
  ///
  /// @return True if reassociating an operand chain rooted at N0 with N1 is profitable.
  ///
  /// \param MRI Machine register info.
  /// \param N0 First operand of the setcc.
  /// \param N1 Second operand of the setcc.
  virtual bool isReassocProfitable(MachineRegisterInfo &MRI, Register N0,
                                   Register N1) const {
    return MRI.hasOneNonDBGUse(N0);
  }

  /// Return true if the given node's result is always uniform across threads.
  ///
  /// @return True if the given node's result is always uniform across threads.
  ///
  /// \param N SDNode being queried.
  virtual bool isSDNodeAlwaysUniform(const SDNode * N) const {
    return false;
  }

  /// Return true and the base/offset/mode if a node's address can be represented
  /// as a pre-indexed load or store.
  ///
  /// Returns true by value, base pointer and offset pointer and addressing mode
  /// by reference if the node's address can be legally represented as
  /// pre-indexed load / store address.
  ///
  /// @return True and the base/offset/mode if a node's address can be represented as a pre-indexed load or store.
  ///
  /// \param N SDNode being queried.
  /// \param Base Filled with the base pointer.
  /// \param Offset Filled with the offset.
  /// \param AM Filled with the indexed addressing mode.
  /// \param DAG SelectionDAG providing context.
  virtual bool getPreIndexedAddressParts(SDNode *N, SDValue &Base,
                                         SDValue &Offset,
                                         ISD::MemIndexedMode &AM,
                                         SelectionDAG &DAG) const {
    return false;
  }

  /// Return true and the base/offset/mode if a node can form a post-indexed load
  /// or store.
  ///
  /// Returns true by value, base pointer and offset pointer and addressing mode
  /// by reference if this node can be combined with a load / store to form a
  /// post-indexed load / store.
  ///
  /// @return True and the base/offset/mode if a node can form a post-indexed load or store.
  ///
  /// \param N SDNode being queried.
  /// \param Op SDNode or value being queried.
  /// \param Base Filled with the base pointer.
  /// \param Offset Filled with the offset.
  /// \param AM Filled with the indexed addressing mode.
  /// \param DAG SelectionDAG providing context.
  virtual bool getPostIndexedAddressParts(SDNode *N, SDNode *Op,
                                          SDValue &Base,
                                          SDValue &Offset,
                                          ISD::MemIndexedMode &AM,
                                          SelectionDAG &DAG) const {
    return false;
  }

  /// Return true if the given base and offset form a legal indexed addressing
  /// mode for this instruction.
  ///
  /// Returns true if the specified base+offset is a legal indexed addressing
  /// mode for this target. \p MI is the load or store instruction that is being
  /// considered for transformation.
  ///
  /// @return True if the given base and offset form a legal indexed addressing mode for this instruction.
  ///
  /// \param MI Machine instruction being queried or modified.
  /// \param Base Filled with the base pointer.
  /// \param Offset Filled with the offset.
  /// \param IsPre True for pre-indexed mode.
  /// \param MRI Machine register info.
  virtual bool isIndexingLegal(MachineInstr &MI, Register Base, Register Offset,
                               bool IsPre, MachineRegisterInfo &MRI) const {
    return false;
  }

  /// Return the entry encoding for a jump table in the current function.  The
  /// returned value is a member of the MachineJumpTableInfo::JTEntryKind enum.
  ///
  /// @return The entry encoding for a jump table in the current function.  The returned value is a member of the MachineJumpTableInfo::JTEntryKind enum.
  virtual unsigned getJumpTableEncoding() const;

  /// Return the register type to use for jump table targets.
  ///
  /// @return The register type to use for jump table targets.
  ///
  /// \param DL Debug location or data layout, depending on context.
  virtual MVT getJumpTableRegTy(const DataLayout &DL) const {
    return getPointerTy(DL);
  }

  /// Lower a custom jump table entry to an MCExpr for targets with non-standard
  /// jump table encodings.
  ///
  /// @return A pointer to the requested object, or null if unavailable.
  ///
  /// \param MJTI Jump table info for the current function.
  /// \param MBB Basic block that owns the jump table entry.
  /// \param uid Jump table entry identifier.
  /// \param Ctx MC context used to build the expression.
  virtual const MCExpr *
  LowerCustomJumpTableEntry(const MachineJumpTableInfo *MJTI,
                            const MachineBasicBlock *MBB, unsigned uid,
                            MCContext &Ctx) const {
    llvm_unreachable("Need to implement this hook if target has custom JTIs");
  }

  /// Returns relocation base for the given PIC jumptable.
  ///
  /// @return Relocation base for the given PIC jumptable.
  ///
  /// \param Table PIC jump-table base.
  /// \param DAG SelectionDAG providing context.
  virtual SDValue getPICJumpTableRelocBase(SDValue Table,
                                           SelectionDAG &DAG) const;

  /// This returns the relocation base for the given PIC jumptable, the same as
  /// getPICJumpTableRelocBase, but as an MCExpr.
  ///
  /// @return A pointer to the requested object, or null if unavailable.
  ///
  /// \param MF Machine function being lowered.
  /// \param JTI Jump table index.
  /// \param Ctx LLVMContext.
  virtual const MCExpr *
  getPICJumpTableRelocBaseExpr(const MachineFunction *MF,
                               unsigned JTI, MCContext &Ctx) const;

  /// Return true if folding a constant offset with the given GlobalAddress is
  /// legal.  It is frequently not legal in PIC relocation models.
  ///
  /// @return True if folding a constant offset with the given GlobalAddress is legal.  It is frequently not legal in PIC relocation models.
  ///
  /// \param GA Global address node.
  virtual bool isOffsetFoldingLegal(const GlobalAddressSDNode *GA) const;

  /// Return true if the given inline asm operand is a call or jump target
  /// needing address-constraint handling.
  ///
  /// On x86, return true if the operand with index OpNo is a CALL or JUMP
  /// instruction, which can use either a memory constraint or an address
  /// constraint. -fasm-blocks "__asm call foo" lowers to call void asm
  /// sideeffect inteldialect "call ${0:P}", "*m...
  /// This function is used by a hack to choose the address constraint,
  /// lowering to a direct call.
  ///
  /// @return True if the given inline asm operand is a call or jump target needing address-constraint handling.
  ///
  /// \param AsmStrs Inline asm string.
  /// \param OpNo Operand number.
  virtual bool
  isInlineAsmTargetBranch(const SmallVectorImpl<StringRef> &AsmStrs,
                          unsigned OpNo) const {
    return false;
  }

  /// Return true if the given node is in tail-call position within its function.
  ///
  /// @return True if the given node is in tail-call position within its function.
  ///
  /// \param DAG SelectionDAG providing context.
  /// \param Node SDNode being expanded or analyzed.
  /// \param Chain Incoming token chain.
  bool isInTailCallPosition(SelectionDAG &DAG, SDNode *Node,
                            SDValue &Chain) const;

  /// Soften the operands of a SETCC for targets that lack hardware
  /// floating-point comparisons.
  ///
  /// \param DAG SelectionDAG providing context.
  /// \param VT Value type being queried or transformed.
  /// \param NewLHS Replacement left-hand operand.
  /// \param NewRHS Replacement right-hand operand.
  /// \param CCCode Condition code of the comparison.
  /// \param DL Debug location or data layout, depending on context.
  /// \param OldLHS Original left-hand operand.
  /// \param OldRHS Original right-hand operand.
  void softenSetCCOperands(SelectionDAG &DAG, EVT VT, SDValue &NewLHS,
                           SDValue &NewRHS, ISD::CondCode &CCCode,
                           const SDLoc &DL, const SDValue OldLHS,
                           const SDValue OldRHS) const;

  /// Soften the operands of a SETCC for targets that lack hardware
  /// floating-point comparisons.
  ///
  /// \param DAG SelectionDAG providing context.
  /// \param VT Value type being queried or transformed.
  /// \param NewLHS Replacement left-hand operand.
  /// \param NewRHS Replacement right-hand operand.
  /// \param CCCode Condition code of the comparison.
  /// \param DL Debug location or data layout, depending on context.
  /// \param OldLHS Original left-hand operand.
  /// \param OldRHS Original right-hand operand.
  /// \param Chain Incoming token chain.
  /// \param IsSignaling True for signaling FP compares.
  void softenSetCCOperands(SelectionDAG &DAG, EVT VT, SDValue &NewLHS,
                           SDValue &NewRHS, ISD::CondCode &CCCode,
                           const SDLoc &DL, const SDValue OldLHS,
                           const SDValue OldRHS, SDValue &Chain,
                           bool IsSignaling = false) const;

  /// Custom-lower a masked load node.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param DAG SelectionDAG providing context.
  /// \param DL Debug location or data layout, depending on context.
  /// \param Chain Incoming token chain.
  /// \param MMO Machine memory operand.
  /// \param NewLoad Replacement load value.
  /// \param Ptr Pointer to annotate.
  /// \param PassThru Passthrough value for inactive lanes.
  /// \param Mask Mask operand or value.
  virtual SDValue visitMaskedLoad(SelectionDAG &DAG, const SDLoc &DL,
                                  SDValue Chain, MachineMemOperand *MMO,
                                  SDValue &NewLoad, SDValue Ptr,
                                  SDValue PassThru, SDValue Mask) const {
    llvm_unreachable("Not Implemented");
  }

  /// Custom-lower a masked store node.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param DAG SelectionDAG providing context.
  /// \param DL Debug location or data layout, depending on context.
  /// \param Chain Incoming token chain.
  /// \param MMO Machine memory operand.
  /// \param Ptr Pointer to annotate.
  /// \param Val Minimum number of bit-test compares.
  /// \param Mask Mask operand or value.
  virtual SDValue visitMaskedStore(SelectionDAG &DAG, const SDLoc &DL,
                                   SDValue Chain, MachineMemOperand *MMO,
                                   SDValue Ptr, SDValue Val,
                                   SDValue Mask) const {
    llvm_unreachable("Not Implemented");
  }

  /// Returns a pair of (return value, chain).
  /// It is an error to pass RTLIB::Unsupported as \p LibcallImpl
  ///
  /// @return A pair of (return value, chain). It is an error to pass RTLIB::Unsupported as \p LibcallImpl.
  ///
  /// \param DAG SelectionDAG providing context.
  /// \param LibcallImpl Specific libcall implementation to emit.
  /// \param RetVT Return value type.
  /// \param Ops Operands passed to the libcall or asm.
  /// \param CallOptions Options controlling libcall emission.
  /// \param dl Debug location for newly created nodes.
  /// \param Chain Incoming token chain.
  std::pair<SDValue, SDValue>
  makeLibCall(SelectionDAG &DAG, RTLIB::LibcallImpl LibcallImpl, EVT RetVT,
              ArrayRef<SDValue> Ops, MakeLibCallOptions CallOptions,
              const SDLoc &dl, SDValue Chain = SDValue()) const;

  /// It is an error to pass RTLIB::UNKNOWN_LIBCALL as \p LC.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param DAG SelectionDAG providing context.
  /// \param LC Libcall kind to expand.
  /// \param RetVT Return value type.
  /// \param Ops Operands passed to the libcall or asm.
  /// \param CallOptions Options controlling libcall emission.
  /// \param dl Debug location for newly created nodes.
  /// \param Chain Incoming token chain.
  std::pair<SDValue, SDValue> makeLibCall(SelectionDAG &DAG, RTLIB::Libcall LC,
                                          EVT RetVT, ArrayRef<SDValue> Ops,
                                          MakeLibCallOptions CallOptions,
                                          const SDLoc &dl,
                                          SDValue Chain = SDValue()) const {
    return makeLibCall(DAG, getLibcallImpl(LC), RetVT, Ops, CallOptions, dl,
                       Chain);
  }

  /// Return true if arguments passed in callee-saved registers match those of
  /// the calling function.
  ///
  /// Check whether parameters to a call that are passed in callee saved
  /// registers are the same as from the calling function. This needs to be
  /// checked for tail call eligibility.
  ///
  /// @return True if arguments passed in callee-saved registers match those of the calling function.
  ///
  /// \param MRI Machine register info.
  /// \param CallerPreservedMask Callee-saved register mask of the caller.
  /// \param ArgLocs Argument locations of the call.
  /// \param OutVals Outgoing return or argument values.
  bool parametersInCSRMatch(const MachineRegisterInfo &MRI,
      const uint32_t *CallerPreservedMask,
      const SmallVectorImpl<CCValAssign> &ArgLocs,
      const SmallVectorImpl<SDValue> &OutVals) const;

  //===--------------------------------------------------------------------===//
  // TargetLowering Optimization Methods
  //

  /// Holds a DAG and Old/New values for returning combine results to clients.
  ///
  /// A convenience struct that encapsulates a DAG, and two SDValues for
  /// returning information from TargetLowering to its clients that want to
  /// combine.
  struct TargetLoweringOpt {
    /// SelectionDAG being optimized.
    SelectionDAG &DAG;
    /// Whether types are assumed to be legal for this optimization.
    bool LegalTys;
    /// Whether operations are assumed to be legal for this optimization.
    bool LegalOps;
    /// Original DAG value being replaced.
    SDValue Old;
    /// Replacement DAG value.
    SDValue New;

    /// Construct an optimization context for the given DAG.
    ///
    /// \param InDAG SelectionDAG being optimized.
    /// \param LT Whether types are assumed legal.
    /// \param LO Whether operations are assumed legal.
    explicit TargetLoweringOpt(SelectionDAG &InDAG,
                               bool LT, bool LO) :
      DAG(InDAG), LegalTys(LT), LegalOps(LO) {}

    /// Return true if types are assumed to be legal for this optimization.
    ///
    /// @return True if operations or types must already be legal.
    bool LegalTypes() const { return LegalTys; }
    /// Return true if operations are assumed to be legal for this optimization.
    ///
    /// @return True if operations or types must already be legal.
    bool LegalOperations() const { return LegalOps; }

    /// Record that an old value or node should be replaced by a new one.
    ///
    /// @return The replacement value after combining.
    ///
    /// \param O Original value being replaced.
    /// \param N Replacement value.
    bool CombineTo(SDValue O, SDValue N) {
      Old = O;
      New = N;
      return true;
    }
  };

  /// Determine the optimal sequence of memory operations to lower a memset or
  /// memcpy.
  ///
  /// Determines the optimal series of memory ops to replace the memset / memcpy.
  /// Return true if the number of memory ops is below the threshold (Limit).
  /// Note that this is always the case when Limit is ~0. It returns the types of
  /// the sequence of memory ops to perform memset / memcpy by reference. If
  /// LargestVT is non-null, the target may set it to the largest EVT that should
  /// be used for generating the memset value (e.g., for vector splats). If
  /// LargestVT is null or left unchanged, the caller will compute it from
  /// MemOps.
  ///
  /// @return True if determine the optimal sequence of memory operations to lower a memset or memcpy.
  ///
  /// \param Context LLVMContext used for type creation.
  /// \param MemOps Filled with chosen memops.
  /// \param Limit Max number of memops under the threshold.
  /// \param Op SDNode or value being queried.
  /// \param DstAS Destination address space.
  /// \param SrcAS Source address space.
  /// \param FuncAttributes Function attributes affecting lowering.
  /// \param LargestVT Largest VT to consider.
  virtual bool findOptimalMemOpLowering(LLVMContext &Context,
                                        std::vector<EVT> &MemOps,
                                        unsigned Limit, const MemOp &Op,
                                        unsigned DstAS, unsigned SrcAS,
                                        const AttributeList &FuncAttributes,
                                        EVT *LargestVT = nullptr) const;

  /// Shrink a constant operand by clearing bits that are not demanded.
  ///
  /// Check to see if the specified operand of the specified instruction is a
  /// constant integer. If so, check to see if there are any bits set in the
  /// constant that are not demanded. If so, shrink the constant and return true.
  ///
  /// @return True if shrink a constant operand by clearing bits that are not demanded.
  ///
  /// \param Op SDNode or value being queried.
  /// \param DemandedBits Bit mask of bits that are demanded.
  /// \param DemandedElts Mask of vector elements that are demanded.
  /// \param TLO Target lowering optimization state.
  bool ShrinkDemandedConstant(SDValue Op, const APInt &DemandedBits,
                              const APInt &DemandedElts,
                              TargetLoweringOpt &TLO) const;

  /// Shrink a constant operand by clearing bits that are not demanded.
  ///
  /// Helper wrapper around ShrinkDemandedConstant, demanding all elements.
  ///
  /// @return True if shrink a constant operand by clearing bits that are not demanded.
  ///
  /// \param Op SDNode or value being queried.
  /// \param DemandedBits Bit mask of bits that are demanded.
  /// \param TLO Target lowering optimization state.
  bool ShrinkDemandedConstant(SDValue Op, const APInt &DemandedBits,
                              TargetLoweringOpt &TLO) const;

  /// Perform target-specific constant shrinking, overriding the generic
  /// ShrinkDemandedConstant behavior.
  ///
  /// @return True if perform target-specific constant shrinking, overriding the generic ShrinkDemandedConstant behavior.
  ///
  /// \param Op SDNode or value being queried.
  /// \param DemandedBits Bit mask of bits that are demanded.
  /// \param DemandedElts Mask of vector elements that are demanded.
  /// \param TLO Target lowering optimization state.
  virtual bool targetShrinkDemandedConstant(SDValue Op,
                                            const APInt &DemandedBits,
                                            const APInt &DemandedElts,
                                            TargetLoweringOpt &TLO) const {
    return false;
  }

  /// Narrow an operation to a smaller type when the widening casts are free.
  ///
  /// Convert x+y to (VT)((SmallVT)x+(SmallVT)y) if the casts are free. This uses
  /// isTruncateFree/isZExtFree and ANY_EXTEND for the widening cast, but it
  /// could be generalized for targets with other types of implicit widening
  /// casts.
  ///
  /// @return True if narrow an operation to a smaller type when the widening casts are free.
  ///
  /// \param Op SDNode or value being queried.
  /// \param BitWidth Operand bit width.
  /// \param DemandedBits Bit mask of bits that are demanded.
  /// \param TLO Target lowering optimization state.
  bool ShrinkDemandedOp(SDValue Op, unsigned BitWidth,
                        const APInt &DemandedBits,
                        TargetLoweringOpt &TLO) const;

  /// Simplify Op using only its demanded bits, or compute its known bits.
  ///
  /// Look at Op. At this point, we know that only the DemandedBits bits of the
  /// result of Op are ever used downstream. If we can use this information to
  /// simplify Op, create a new simplified DAG node and return true, returning
  /// the original and new nodes in Old and New. Otherwise, analyze the
  /// expression and return a mask of KnownOne and KnownZero bits for the
  /// expression (used to simplify the caller). The KnownZero/One bits may only
  /// be accurate for those bits in the Demanded masks.
  ///
  /// \p AssumeSingleUse When this parameter is true, this function will
  ///    attempt to simplify \p Op even if there are multiple uses.
  ///    Callers are responsible for correctly updating the DAG based on the
  ///    results of this function, because simply replacing TLO.Old
  ///    with TLO.New will be incorrect when this parameter is true and TLO.Old
  ///    has multiple uses.
  ///
  /// @return True if simplify Op using only its demanded bits, or compute its known bits.
  ///
  /// \param Op SDNode or value being queried.
  /// \param DemandedBits Bit mask of bits that are demanded.
  /// \param DemandedElts Mask of vector elements that are demanded.
  /// \param Known Known-bits or known-FP-class result to fill.
  /// \param TLO Target lowering optimization state.
  /// \param Depth Recursion depth limit for the analysis.
  /// \param AssumeSingleUse Whether a single use may be assumed.
  bool SimplifyDemandedBits(SDValue Op, const APInt &DemandedBits,
                            const APInt &DemandedElts, KnownBits &Known,
                            TargetLoweringOpt &TLO, unsigned Depth = 0,
                            bool AssumeSingleUse = false) const;

  /// Simplify Op using only its demanded bits, or compute its known bits.
  ///
  /// Helper wrapper around SimplifyDemandedBits, demanding all elements. Adds Op
  /// back to the worklist upon success.
  ///
  /// @return True if simplify Op using only its demanded bits, or compute its known bits.
  ///
  /// \param Op SDNode or value being queried.
  /// \param DemandedBits Bit mask of bits that are demanded.
  /// \param Known Known-bits or known-FP-class result to fill.
  /// \param TLO Target lowering optimization state.
  /// \param Depth Recursion depth limit for the analysis.
  /// \param AssumeSingleUse Whether a single use may be assumed.
  bool SimplifyDemandedBits(SDValue Op, const APInt &DemandedBits,
                            KnownBits &Known, TargetLoweringOpt &TLO,
                            unsigned Depth = 0,
                            bool AssumeSingleUse = false) const;

  /// Simplify Op using only its demanded bits, or compute its known bits.
  ///
  /// Helper wrapper around SimplifyDemandedBits. Adds Op back to the worklist
  /// upon success.
  ///
  /// @return True if simplify Op using only its demanded bits, or compute its known bits.
  ///
  /// \param Op SDNode or value being queried.
  /// \param DemandedBits Bit mask of bits that are demanded.
  /// \param DCI DAG combiner info.
  bool SimplifyDemandedBits(SDValue Op, const APInt &DemandedBits,
                            DAGCombinerInfo &DCI) const;

  /// Simplify Op using only its demanded bits, or compute its known bits.
  ///
  /// Helper wrapper around SimplifyDemandedBits. Adds Op back to the worklist
  /// upon success.
  ///
  /// @return True if simplify Op using only its demanded bits, or compute its known bits.
  ///
  /// \param Op SDNode or value being queried.
  /// \param DemandedBits Bit mask of bits that are demanded.
  /// \param DemandedElts Mask of vector elements that are demanded.
  /// \param DCI DAG combiner info.
  bool SimplifyDemandedBits(SDValue Op, const APInt &DemandedBits,
                            const APInt &DemandedElts,
                            DAGCombinerInfo &DCI) const;

  /// More limited version of SimplifyDemandedBits that can be used to "look
  /// through" ops that don't contribute to the DemandedBits/DemandedElts -
  /// bitwise ops etc.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param Op SDNode or value being queried.
  /// \param DemandedBits Bit mask of bits that are demanded.
  /// \param DemandedElts Mask of vector elements that are demanded.
  /// \param DAG SelectionDAG providing context.
  /// \param Depth Recursion depth limit for the analysis.
  SDValue SimplifyMultipleUseDemandedBits(SDValue Op, const APInt &DemandedBits,
                                          const APInt &DemandedElts,
                                          SelectionDAG &DAG,
                                          unsigned Depth = 0) const;

  /// Helper wrapper around SimplifyMultipleUseDemandedBits, demanding all
  /// elements.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param Op SDNode or value being queried.
  /// \param DemandedBits Bit mask of bits that are demanded.
  /// \param DAG SelectionDAG providing context.
  /// \param Depth Recursion depth limit for the analysis.
  SDValue SimplifyMultipleUseDemandedBits(SDValue Op, const APInt &DemandedBits,
                                          SelectionDAG &DAG,
                                          unsigned Depth = 0) const;

  /// Helper wrapper around SimplifyMultipleUseDemandedBits, demanding all
  /// bits from only some vector elements.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param Op SDNode or value being queried.
  /// \param DemandedElts Mask of vector elements that are demanded.
  /// \param DAG SelectionDAG providing context.
  /// \param Depth Recursion depth limit for the analysis.
  SDValue SimplifyMultipleUseDemandedVectorElts(SDValue Op,
                                                const APInt &DemandedElts,
                                                SelectionDAG &DAG,
                                                unsigned Depth = 0) const;

  /// Simplify a vector operation using only its demanded elements, or compute
  /// known undef/zero elements.
  ///
  /// Look at Vector Op. At this point, we know that only the DemandedElts
  /// elements of the result of Op are ever used downstream. If we can use this
  /// information to simplify Op, create a new simplified DAG node and return
  /// true, storing the original and new nodes in TLO. Otherwise, analyze the
  /// expression and return a mask of KnownUndef and KnownZero elements for the
  /// expression (used to simplify the caller). The KnownUndef/Zero elements may
  /// only be accurate for those bits in the DemandedMask.
  ///
  /// \p AssumeSingleUse When this parameter is true, this function will
  ///    attempt to simplify \p Op even if there are multiple uses.
  ///    Callers are responsible for correctly updating the DAG based on the
  ///    results of this function, because simply replacing TLO.Old
  ///    with TLO.New will be incorrect when this parameter is true and TLO.Old
  ///    has multiple uses.
  ///
  /// @return True if simplify a vector operation using only its demanded elements, or compute known undef/zero elements.
  ///
  /// \param Op SDNode or value being queried.
  /// \param DemandedEltMask Demanded element bitmask.
  /// \param KnownUndef Filled with elements known undef.
  /// \param KnownZero Filled with elements or bits known zero.
  /// \param TLO Target lowering optimization state.
  /// \param Depth Recursion depth limit for the analysis.
  /// \param AssumeSingleUse Whether a single use may be assumed.
  bool SimplifyDemandedVectorElts(SDValue Op, const APInt &DemandedEltMask,
                                  APInt &KnownUndef, APInt &KnownZero,
                                  TargetLoweringOpt &TLO, unsigned Depth = 0,
                                  bool AssumeSingleUse = false) const;

  /// Simplify a vector operation using only its demanded elements, or compute
  /// known undef/zero elements.
  ///
  /// Helper wrapper around SimplifyDemandedVectorElts. Adds Op back to the
  /// worklist upon success.
  ///
  /// @return True if simplify a vector operation using only its demanded elements, or compute known undef/zero elements.
  ///
  /// \param Op SDNode or value being queried.
  /// \param DemandedElts Mask of vector elements that are demanded.
  /// \param DCI DAG combiner info.
  bool SimplifyDemandedVectorElts(SDValue Op, const APInt &DemandedElts,
                                  DAGCombinerInfo &DCI) const;

  /// Return true if the target supports simplifying demanded vector elements by
  /// converting them to undefs.
  ///
  /// @return True if the target supports simplifying demanded vector elements by converting them to undefs.
  ///
  /// \param Op SDNode or value being queried.
  /// \param TLO Target lowering optimization state.
  virtual bool
  shouldSimplifyDemandedVectorElts(SDValue Op,
                                   const TargetLoweringOpt &TLO) const {
    return true;
  }

  /// Return the bit size to which a vector operation should be shrunk when only
  /// its low elements are demanded.
  ///
  /// If only low elements of a vector are demanded, shrink the operation to the
  /// returned size in bits by converting (op x) to insert_subvector (op
  /// (extract_subvector x)).
  /// The returned size must be a multiple of the element size, greater than or
  /// equal to the demanded part of the vector and less than the original
  /// vector size. Return 0 to disable shrinking.
  ///
  /// @return The bit size to which a vector operation should be shrunk when only its low elements are demanded.
  ///
  /// \param Op SDNode or value being queried.
  /// \param DemandedElts Mask of vector elements that are demanded.
  virtual unsigned
  getPreferredShrunkVectorSizeInBits(SDValue Op,
                                     const APInt &DemandedElts) const {
    return 0;
  }

  /// Compute the known bits of a target-specific SelectionDAG node.
  ///
  /// Determine which of the bits specified in Mask are known to be either zero
  /// or one and return them in the KnownZero/KnownOne bitsets. The DemandedElts
  /// argument allows us to only collect the known bits that are shared by the
  /// requested vector elements.
  ///
  /// \param Op SDNode or value being queried.
  /// \param Known Known-bits or known-FP-class result to fill.
  /// \param DemandedElts Mask of vector elements that are demanded.
  /// \param DAG SelectionDAG providing context.
  /// \param Depth Recursion depth limit for the analysis.
  virtual void computeKnownBitsForTargetNode(const SDValue Op,
                                             KnownBits &Known,
                                             const APInt &DemandedElts,
                                             const SelectionDAG &DAG,
                                             unsigned Depth = 0) const;

  /// Compute the known bits of a value produced by a target-specific GlobalISel
  /// instruction.
  ///
  /// Determine which of the bits specified in Mask are known to be either zero
  /// or one and return them in the KnownZero/KnownOne bitsets. The DemandedElts
  /// argument allows us to only collect the known bits that are shared by the
  /// requested vector elements. This is for GISel.
  ///
  /// \param Analysis Known-bits or related analysis object.
  /// \param R Register or value being queried.
  /// \param Known Known-bits or known-FP-class result to fill.
  /// \param DemandedElts Mask of vector elements that are demanded.
  /// \param MRI Machine register info.
  /// \param Depth Recursion depth limit for the analysis.
  virtual void computeKnownBitsForTargetInstr(GISelValueTracking &Analysis,
                                              Register R, KnownBits &Known,
                                              const APInt &DemandedElts,
                                              const MachineRegisterInfo &MRI,
                                              unsigned Depth = 0) const;

  /// Compute known floating-point class information for a value produced by a
  /// target GlobalISel instruction.
  ///
  /// \param Analysis Known-bits or related analysis object.
  /// \param R Register or value being queried.
  /// \param Known Known-bits or known-FP-class result to fill.
  /// \param DemandedElts Mask of vector elements that are demanded.
  /// \param MRI Machine register info.
  /// \param Depth Recursion depth limit for the analysis.
  virtual void computeKnownFPClassForTargetInstr(GISelValueTracking &Analysis,
                                                 Register R,
                                                 KnownFPClass &Known,
                                                 const APInt &DemandedElts,
                                                 const MachineRegisterInfo &MRI,
                                                 unsigned Depth = 0) const;

  /// Compute the known alignment of a pointer value produced by a target
  /// GlobalISel instruction.
  ///
  /// Determine the known alignment for the pointer value \p R. This is can
  /// typically be inferred from the number of low known 0 bits. However, for a
  /// pointer with a non-integral address space, the alignment value may be
  /// independent from the known low bits.
  ///
  /// @return The requested alignment.
  ///
  /// \param Analysis Known-bits or related analysis object.
  /// \param R Register or value being queried.
  /// \param MRI Machine register info.
  /// \param Depth Recursion depth limit for the analysis.
  virtual Align computeKnownAlignForTargetInstr(GISelValueTracking &Analysis,
                                                Register R,
                                                const MachineRegisterInfo &MRI,
                                                unsigned Depth = 0) const;

  /// Determine known bits of a pointer to a known valid stack object.
  /// The default implementation computes low bits based on alignment.
  /// \param Known Known-bits or known-FP-class result to fill.
  /// \param MF Machine function being lowered.
  /// \param Alignment Required or preferred alignment.
  virtual void computeKnownBitsForStackObjectPointer(KnownBits &Known,
                                                     const MachineFunction &MF,
                                                     Align Alignment) const;

  /// Return the number of known sign bits for a target-specific SelectionDAG
  /// node.
  ///
  /// This method can be implemented by targets that want to expose additional
  /// information about sign bits to the DAG Combiner. The DemandedElts argument
  /// allows us to only collect the minimum sign bits that are shared by the
  /// requested vector elements.
  ///
  /// @return The number of known sign bits for a target-specific SelectionDAG node.
  ///
  /// \param Op SDNode or value being queried.
  /// \param DemandedElts Mask of vector elements that are demanded.
  /// \param DAG SelectionDAG providing context.
  /// \param Depth Recursion depth limit for the analysis.
  virtual unsigned ComputeNumSignBitsForTargetNode(SDValue Op,
                                                   const APInt &DemandedElts,
                                                   const SelectionDAG &DAG,
                                                   unsigned Depth = 0) const;

  /// Compute the number of known sign bits for a target-specific GlobalISel
  /// instruction.
  ///
  /// This method can be implemented by targets that want to expose additional
  /// information about sign bits to GlobalISel combiners. The DemandedElts
  /// argument allows us to only collect the minimum sign bits that are shared by
  /// the requested vector elements.
  ///
  /// @return Compute the number of known sign bits for a target-specific GlobalISel instruction.
  ///
  /// \param Analysis Known-bits or related analysis object.
  /// \param R Register or value being queried.
  /// \param DemandedElts Mask of vector elements that are demanded.
  /// \param MRI Machine register info.
  /// \param Depth Recursion depth limit for the analysis.
  virtual unsigned computeNumSignBitsForTargetInstr(
      GISelValueTracking &Analysis, Register R, const APInt &DemandedElts,
      const MachineRegisterInfo &MRI, unsigned Depth = 0) const;

  /// Simplify a target-specific node using only its demanded vector elements.
  ///
  /// Attempt to simplify any target nodes based on the demanded vector elements,
  /// returning true on success. Otherwise, analyze the expression and return a
  /// mask of KnownUndef and KnownZero elements for the expression (used to
  /// simplify the caller). The KnownUndef/Zero elements may only be accurate for
  /// those bits in the DemandedMask.
  ///
  /// @return True if simplify a target-specific node using only its demanded vector elements.
  ///
  /// \param Op SDNode or value being queried.
  /// \param DemandedElts Mask of vector elements that are demanded.
  /// \param KnownUndef Filled with elements known undef.
  /// \param KnownZero Filled with elements or bits known zero.
  /// \param TLO Target lowering optimization state.
  /// \param Depth Recursion depth limit for the analysis.
  virtual bool SimplifyDemandedVectorEltsForTargetNode(
      SDValue Op, const APInt &DemandedElts, APInt &KnownUndef,
      APInt &KnownZero, TargetLoweringOpt &TLO, unsigned Depth = 0) const;

  /// Simplify a target-specific node using only its demanded bits and elements.
  ///
  /// Attempt to simplify any target nodes based on the demanded bits/elts,
  /// returning true on success. Otherwise, analyze the expression and return a
  /// mask of KnownOne and KnownZero bits for the expression (used to simplify
  /// the caller). The KnownZero/One bits may only be accurate for those bits in
  /// the Demanded masks.
  ///
  /// @return True if simplify a target-specific node using only its demanded bits and elements.
  ///
  /// \param Op SDNode or value being queried.
  /// \param DemandedBits Bit mask of bits that are demanded.
  /// \param DemandedElts Mask of vector elements that are demanded.
  /// \param Known Known-bits or known-FP-class result to fill.
  /// \param TLO Target lowering optimization state.
  /// \param Depth Recursion depth limit for the analysis.
  virtual bool SimplifyDemandedBitsForTargetNode(SDValue Op,
                                                 const APInt &DemandedBits,
                                                 const APInt &DemandedElts,
                                                 KnownBits &Known,
                                                 TargetLoweringOpt &TLO,
                                                 unsigned Depth = 0) const;

  /// More limited version of SimplifyDemandedBits that can be used to "look
  /// through" ops that don't contribute to the DemandedBits/DemandedElts -
  /// bitwise ops etc.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param Op SDNode or value being queried.
  /// \param DemandedBits Bit mask of bits that are demanded.
  /// \param DemandedElts Mask of vector elements that are demanded.
  /// \param DAG SelectionDAG providing context.
  /// \param Depth Recursion depth limit for the analysis.
  virtual SDValue SimplifyMultipleUseDemandedBitsForTargetNode(
      SDValue Op, const APInt &DemandedBits, const APInt &DemandedElts,
      SelectionDAG &DAG, unsigned Depth) const;

  /// Return true if a target-specific node is proven to never be undef or
  /// poison.
  ///
  /// Return true if this function can prove that \p Op is never poison and, \p
  /// Kind can be used to track poison and/or undef bits. The DemandedElts
  /// argument limits the check to the requested vector elements.
  ///
  /// @return True if a target-specific node is proven to never be undef or poison.
  ///
  /// \param Op SDNode or value being queried.
  /// \param DemandedElts Mask of vector elements that are demanded.
  /// \param DAG SelectionDAG providing context.
  /// \param Kind Undef/poison tracking kind.
  /// \param Depth Recursion depth limit for the analysis.
  virtual bool isGuaranteedNotToBeUndefOrPoisonForTargetNode(
      SDValue Op, const APInt &DemandedElts, const SelectionDAG &DAG,
      UndefPoisonKind Kind, unsigned Depth) const;

  /// Return true if Op can create undef or poison from non-undef & non-poison
  /// operands. The DemandedElts argument limits the check to the requested
  /// vector elements.
  ///
  /// @return True if Op can create undef or poison from non-undef & non-poison operands. The DemandedElts argument limits the check to the requested vector elements.
  ///
  /// \param Op SDNode or value being queried.
  /// \param DemandedElts Mask of vector elements that are demanded.
  /// \param DAG SelectionDAG providing context.
  /// \param Kind Undef/poison tracking kind.
  /// \param ConsiderFlags Whether instruction flags are considered.
  /// \param Depth Recursion depth limit for the analysis.
  virtual bool canCreateUndefOrPoisonForTargetNode(
      SDValue Op, const APInt &DemandedElts, const SelectionDAG &DAG,
      UndefPoisonKind Kind, bool ConsiderFlags, unsigned Depth) const;

  /// Build a legal vector shuffle from the given operands and mask, trying
  /// equivalent variations if needed.
  ///
  /// Tries to build a legal vector shuffle using the provided parameters or
  /// equivalent variations. The Mask argument maybe be modified as the function
  /// tries different variations. Returns an empty SDValue if the operation
  /// fails.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param VT Value type being queried or transformed.
  /// \param DL Debug location or data layout, depending on context.
  /// \param N0 First operand of the setcc.
  /// \param N1 Second operand of the setcc.
  /// \param Mask Mask operand or value.
  /// \param DAG SelectionDAG providing context.
  SDValue buildLegalVectorShuffle(EVT VT, const SDLoc &DL, SDValue N0,
                                  SDValue N1, MutableArrayRef<int> Mask,
                                  SelectionDAG &DAG) const;

  /// This method returns the constant pool value that will be loaded by LD.
  /// NOTE: You must check for implicit extensions of the constant by LD.
  ///
  /// @return A pointer to the requested object, or null if unavailable.
  ///
  /// \param LD Load node whose constant pool value is queried.
  virtual const Constant *getTargetConstantFromLoad(LoadSDNode *LD) const;

  /// Compute known floating-point class information for a target-specific
  /// SelectionDAG node.
  ///
  /// Determine floating-point class information for a target node. The
  /// DemandedElts argument allows us to only collect the known FP classes that
  /// are shared by the requested vector elements.
  ///
  /// \param Op SDNode or value being queried.
  /// \param Known Known-bits or known-FP-class result to fill.
  /// \param DemandedElts Mask of vector elements that are demanded.
  /// \param DAG SelectionDAG providing context.
  /// \param Depth Recursion depth limit for the analysis.
  virtual void computeKnownFPClassForTargetNode(const SDValue Op,
                                                KnownFPClass &Known,
                                                const APInt &DemandedElts,
                                                const SelectionDAG &DAG,
                                                unsigned Depth = 0) const;

  /// If \p SNaN is false, \returns true if \p Op is known to never be any
  /// NaN. If \p sNaN is true, returns if \p Op is known to never be a signaling
  /// NaN.
  /// \param Op SDNode or value being queried.
  /// \param DemandedElts Mask of vector elements that are demanded.
  /// \param DAG SelectionDAG providing context.
  /// \param SNaN Restrict the query to signaling NaNs.
  /// \param Depth Recursion depth limit for the analysis.
  virtual bool isKnownNeverNaNForTargetNode(SDValue Op,
                                            const APInt &DemandedElts,
                                            const SelectionDAG &DAG,
                                            bool SNaN = false,
                                            unsigned Depth = 0) const;

  /// Return true if vector \p Op has the same value across all \p DemandedElts,
  /// indicating any elements which may be undef in the output \p UndefElts.
  ///
  /// @return True if vector \p Op has the same value across all \p DemandedElts, indicating any elements which may be undef in the output \p UndefElts.
  ///
  /// \param Op SDNode or value being queried.
  /// \param DemandedElts Mask of vector elements that are demanded.
  /// \param UndefElts Filled with undef element mask.
  /// \param DAG SelectionDAG providing context.
  /// \param Depth Recursion depth limit for the analysis.
  virtual bool isSplatValueForTargetNode(SDValue Op, const APInt &DemandedElts,
                                         APInt &UndefElts,
                                         const SelectionDAG &DAG,
                                         unsigned Depth = 0) const;

  /// Returns true if the given Opc is considered a canonical constant for the
  /// target, which should not be transformed back into a BUILD_VECTOR.
  ///
  /// @return True if the given Opc is considered a canonical constant for the target, which should not be transformed back into a BUILD_VECTOR.
  ///
  /// \param Op SDNode or value being queried.
  virtual bool isTargetCanonicalConstantNode(SDValue Op) const {
    return Op.getOpcode() == ISD::SPLAT_VECTOR ||
           Op.getOpcode() == ISD::SPLAT_VECTOR_PARTS;
  }

  /// Return true if the given select node is already in the target's canonical
  /// form.
  ///
  /// Return true if the given select/vselect should be considered canonical and
  /// not be transformed. Currently only used for "vselect (not Cond), N1, N2 ->
  /// vselect Cond, N2, N1".
  ///
  /// @return True if the given select node is already in the target's canonical form.
  ///
  /// \param N SDNode being queried.
  virtual bool isTargetCanonicalSelect(SDNode *N) const { return false; }

  /// Holds context passed to target DAG-combine hooks, including the DAG and
  /// combine level.
  struct DAGCombinerInfo {
    /// Opaque pointer to the DAGCombiner instance driving this combine.
    void *DC;
    /// Current DAG combine legalization level.
    CombineLevel Level;
    /// Whether this combine is being invoked from within the type or vector
    /// legalizer.
    bool CalledByLegalizer;

  public:
    /// SelectionDAG providing context for this combine.
    SelectionDAG &DAG;

    /// Construct combiner context for a DAG combine callback.
    ///
    /// \param dag SelectionDAG being combined.
    /// \param level Current DAG combine legalization level.
    /// \param cl Whether this combine was invoked from the legalizer.
    /// \param dc Opaque pointer to the DAGCombiner instance.
    DAGCombinerInfo(SelectionDAG &dag, CombineLevel level,  bool cl, void *dc)
      : DC(dc), Level(level), CalledByLegalizer(cl), DAG(dag) {}

    /// Return true if this combine is running before type legalization.
    ///
    /// @return True if this combine is running before type legalization.
    bool isBeforeLegalize() const { return Level == BeforeLegalizeTypes; }
    /// Return true if this combine is running before operation legalization.
    ///
    /// @return True if this combine is running before operation legalization.
    bool isBeforeLegalizeOps() const { return Level < AfterLegalizeVectorOps; }
    /// Return true if this combine is running after DAG legalization.
    ///
    /// @return True if this combine is running after DAG legalization.
    bool isAfterLegalizeDAG() const { return Level >= AfterLegalizeDAG; }
    /// Return the current DAG combine legalization level.
    ///
    /// @return The current DAG combine legalization level.
    CombineLevel getDAGCombineLevel() { return Level; }
    /// Return true if this combine was invoked from the legalizer.
    ///
    /// @return True if this combine was invoked from the legalizer.
    bool isCalledByLegalizer() const { return CalledByLegalizer; }

    /// Add the given node to the DAG combiner's worklist.
    ///
    /// \param N SDNode being queried.
    LLVM_ABI void AddToWorklist(SDNode *N);
    /// Record that an old value or node should be replaced by a new one.
    ///
    /// @return The replacement value after combining.
    ///
    /// \param N SDNode being queried.
    /// \param To Destination PHI type.
    /// \param AddTo Whether to add the result node to the combiner worklist.
    LLVM_ABI SDValue CombineTo(SDNode *N, ArrayRef<SDValue> To,
                               bool AddTo = true);
    /// Record that an old value or node should be replaced by a new one.
    ///
    /// @return The replacement value after combining.
    ///
    /// \param N SDNode being queried.
    /// \param Res Result value produced by the combine.
    /// \param AddTo Whether to add the result node to the combiner worklist.
    LLVM_ABI SDValue CombineTo(SDNode *N, SDValue Res, bool AddTo = true);
    /// Record that an old value or node should be replaced by a new one.
    ///
    /// @return The replacement value after combining.
    ///
    /// \param N SDNode being queried.
    /// \param Res0 First result value produced by the combine.
    /// \param Res1 Second result value produced by the combine.
    /// \param AddTo Whether to add the result node to the combiner worklist.
    LLVM_ABI SDValue CombineTo(SDNode *N, SDValue Res0, SDValue Res1,
                               bool AddTo = true);

    /// Recursively delete the given node and any operands that become unused.
    ///
    /// @return True if recursively delete the given node and any operands that become unused.
    ///
    /// \param N SDNode being queried.
    LLVM_ABI bool recursivelyDeleteUnusedNodes(SDNode *N);

    /// Apply the changes recorded in a TargetLoweringOpt to the DAG.
    ///
    /// \param TLO Target lowering optimization state.
    LLVM_ABI void CommitTargetLoweringOpt(const TargetLoweringOpt &TLO);
  };

  /// Return if the N is a constant or constant vector equal to the true value
  /// from getBooleanContents().
  ///
  /// @return If the N is a constant or constant vector equal to the true value from getBooleanContents().
  ///
  /// \param N SDNode being queried.
  bool isConstTrueVal(SDValue N) const;

  /// Return if the N is a constant or constant vector equal to the false value
  /// from getBooleanContents().
  ///
  /// @return If the N is a constant or constant vector equal to the false value from getBooleanContents().
  ///
  /// \param N SDNode being queried.
  bool isConstFalseVal(SDValue N) const;

  /// Return if \p N is a True value when extended to \p VT.
  ///
  /// @return If \p N is a True value when extended to \p VT.
  ///
  /// \param N SDNode being queried.
  /// \param VT Value type being queried or transformed.
  /// \param SExt Whether true is sign-extended.
  bool isExtendedTrueVal(const ConstantSDNode *N, EVT VT, bool SExt) const;

  /// Try to simplify a setcc built with the specified operands and cc. If it is
  /// unable to simplify it, return a null SDValue.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param VT Value type being queried or transformed.
  /// \param N0 First operand of the setcc.
  /// \param N1 Second operand of the setcc.
  /// \param Cond Condition code of the compare.
  /// \param foldBooleans Whether boolean setccs may be folded.
  /// \param DCI DAG combiner info.
  /// \param dl Debug location for newly created nodes.
  SDValue SimplifySetCC(EVT VT, SDValue N0, SDValue N1, ISD::CondCode Cond,
                        bool foldBooleans, DAGCombinerInfo &DCI,
                        const SDLoc &dl) const;

  /// Unwrap a target-specific address-wrapping node for analysis purposes.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param N SDNode being queried.
  virtual SDValue unwrapAddress(SDValue N) const { return N; }

  /// Returns true (and the GlobalValue and the offset) if the node is a
  /// GlobalAddress + offset.
  ///
  /// @return True (and the GlobalValue and the offset) if the node is a GlobalAddress + offset.
  ///
  /// \param N SDNode being queried.
  /// \param GA Global address node.
  /// \param Offset Filled with the offset.
  virtual bool
  isGAPlusOffset(SDNode *N, const GlobalValue* &GA, int64_t &Offset) const;

  /// This method will be invoked for all target nodes and for any
  /// target-independent nodes that the target has registered with invoke it
  /// for.
  ///
  /// The semantics are as follows:
  /// Return Value:
  ///   SDValue.Val == 0   - No change was made
  ///   SDValue.Val == N   - N was replaced, is dead, and is already handled.
  ///   otherwise          - N should be replaced by the returned Operand.
  ///
  /// In addition, methods provided by DAGCombinerInfo may be used to perform
  /// more complex transformations.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param N SDNode being queried.
  /// \param DCI DAG combiner info.
  virtual SDValue PerformDAGCombine(SDNode *N, DAGCombinerInfo &DCI) const;

  /// Return true if it is profitable to commute a shift by a constant with its
  /// operand.
  ///
  /// Return true if it is profitable to move this shift by a constant amount
  /// through its operand, adjusting any immediate operands as necessary to
  /// preserve semantics. This transformation may not be desirable if it disrupts
  /// a particularly auspicious target-specific tree (e.g. bitfield extraction in
  /// AArch64). By default, it returns true.
  ///
  /// @return True if it is profitable to commute a shift by a constant with its operand.
  ///
  /// @param N the shift node
  /// @param Level the current DAGCombine legalization level.
  virtual bool isDesirableToCommuteWithShift(const SDNode *N,
                                             CombineLevel Level) const {
    SDValue ShiftLHS = N->getOperand(0);
    if (!ShiftLHS->hasOneUse())
      return false;
    if (ShiftLHS.getOpcode() == ISD::SIGN_EXTEND &&
        !ShiftLHS.getOperand(0)->hasOneUse())
      return false;
    return true;
  }

  /// Return true if it is profitable to commute a shift by a constant with its
  /// operand.
  ///
  /// GlobalISel - return true if it is profitable to move this shift by a
  /// constant amount through its operand, adjusting any immediate operands as
  /// necessary to preserve semantics. This transformation may not be desirable
  /// if it disrupts a particularly auspicious target-specific tree (e.g.
  /// bitfield extraction in AArch64). By default, it returns true.
  ///
  /// @return True if it is profitable to commute a shift by a constant with its operand.
  ///
  /// @param MI the shift instruction
  /// @param IsAfterLegal true if running after legalization.
  virtual bool isDesirableToCommuteWithShift(const MachineInstr &MI,
                                             bool IsAfterLegal) const {
    return true;
  }

  /// GlobalISel - return true if it's profitable to perform the combine:
  /// shl ([sza]ext x), y => zext (shl x, y)
  ///
  /// @return True if globalISel - return true if it's profitable to perform the combine: shl ([sza]ext x), y => zext (shl x, y).
  ///
  /// \param MI Machine instruction being queried or modified.
  virtual bool isDesirableToPullExtFromShl(const MachineInstr &MI) const {
    return true;
  }

  /// Return the preferred fold kind for combining a logic op of two SETCC nodes.
  ///
  /// @return The preferred fold kind for combining a logic op of two SETCC nodes.
  ///
  /// \param LogicOp Logic opcode combining the SETCC results.
  /// \param SETCC0 First SETCC operand of the logic fold.
  /// \param SETCC1 Second SETCC operand of the logic fold.
  virtual AndOrSETCCFoldKind isDesirableToCombineLogicOpOfSETCC(
      const SDNode *LogicOp, const SDNode *SETCC0, const SDNode *SETCC1) const {
    return AndOrSETCCFoldKind::None;
  }

  /// Return true if it is profitable to commute a XOR with a logical shift to
  /// form a shifted NOT.
  ///
  /// Return true if it is profitable to combine an XOR of a logical shift to
  /// create a logical shift of NOT. This transformation may not be desirable if
  /// it disrupts a particularly auspicious target-specific tree (e.g. BIC on
  /// ARM/AArch64). By default, it returns true.
  ///
  /// @return True if it is profitable to commute a XOR with a logical shift to form a shifted NOT.
  ///
  /// \param N SDNode being queried.
  virtual bool isDesirableToCommuteXorWithShift(const SDNode *N) const {
    return true;
  }

  /// Return true if using this legal type for the given opcode is desirable, not
  /// just legal.
  ///
  /// Return true if the target has native support for the specified value type
  /// and it is 'desirable' to use the type for the given node type. e.g. On x86
  /// i16 is legal, but undesirable since i16 instruction encodings are longer
  /// and some i16 instructions are slow.
  ///
  /// @return True if using this legal type for the given opcode is desirable, not just legal.
  ///
  /// \param Opc Operation opcode.
  /// \param VT Value type being queried or transformed.
  virtual bool isTypeDesirableForOp(unsigned Opc, EVT VT) const {
    // By default, assume all legal types are desirable.
    return isTypeLegal(VT);
  }

  /// Return true if transforming a floating-point operation into an equivalent
  /// integer operation is profitable.
  ///
  /// Return true if it is profitable for dag combiner to transform a floating
  /// point op of specified opcode to a equivalent op of an integer type. e.g.
  /// f32 load -> i32 load can be profitable on ARM.
  ///
  /// @return True if transforming a floating-point operation into an equivalent integer operation is profitable.
  ///
  /// \param Opc Operation opcode.
  /// \param VT Value type being queried or transformed.
  virtual bool isDesirableToTransformToIntegerOp(unsigned Opc,
                                                 EVT VT) const {
    return false;
  }

  /// Return true if the DAG combiner should promote the specified node, and give
  /// the promotion type.
  ///
  /// This method query the target whether it is beneficial for dag combiner to
  /// promote the specified node. If true, it should return the desired promotion
  /// type by reference.
  ///
  /// @return True if the DAG combiner should promote the specified node, and give the promotion type.
  ///
  /// \param Op SDNode or value being queried.
  /// \param PVT Filled with the desired promotion type.
  virtual bool IsDesirableToPromoteOp(SDValue Op, EVT &PVT) const {
    return false;
  }

  /// Return true if the target supports swifterror attribute. It optimizes
  /// loads and stores to reading and writing a specific register.
  ///
  /// @return True if the target supports swifterror attribute. It optimizes loads and stores to reading and writing a specific register.
  virtual bool supportSwiftError() const {
    return false;
  }

  /// Return true if the target supports that a subset of CSRs for the given
  /// machine function is handled explicitly via copies.
  ///
  /// @return True if the target supports that a subset of CSRs for the given machine function is handled explicitly via copies.
  ///
  /// \param MF Machine function being lowered.
  virtual bool supportSplitCSR(MachineFunction *MF) const {
    return false;
  }

  /// Return true if the target supports kcfi operand bundles.
  ///
  /// @return True if the target supports kcfi operand bundles.
  virtual bool supportKCFIBundles() const { return false; }

  /// Return true if the target supports ptrauth operand bundles.
  ///
  /// @return True if the target supports ptrauth operand bundles.
  virtual bool supportPtrAuthBundles() const { return false; }

  /// Perform necessary initialization to handle a subset of CSRs explicitly
  /// via copies. This function is called at the beginning of instruction
  /// selection.
  /// \param Entry Entry MBB for split-CSR setup.
  virtual void initializeSplitCSR(MachineBasicBlock *Entry) const {
    llvm_unreachable("Not Implemented");
  }

  /// Insert copies of callee-saved registers in the entry and exit blocks for
  /// split-CSR handling.
  ///
  /// Insert explicit copies in entry and exit blocks. We copy a subset of CSRs
  /// to virtual registers in the entry block, and copy them back to physical
  /// registers in the exit blocks. This function is called at the end of
  /// instruction selection.
  ///
  /// \param Entry Entry MBB for split-CSR setup.
  /// \param Exits Exit MBBs receiving CSR restores.
  virtual void insertCopiesSplitCSR(
      MachineBasicBlock *Entry,
      const SmallVectorImpl<MachineBasicBlock *> &Exits) const {
    llvm_unreachable("Not Implemented");
  }

  /// Return the newly negated expression if the cost is not expensive and
  /// set the cost in \p Cost to indicate that if it is cheaper or neutral to
  /// do the negation.
  ///
  /// @return The newly negated expression if the cost is not expensive and set the cost in \p Cost to indicate that if it is cheaper or neutral to do the negation.
  ///
  /// \param Op SDNode or value being queried.
  /// \param DAG SelectionDAG providing context.
  /// \param LegalOps Restrict results to legal operations.
  /// \param OptForSize Optimize the negation for size.
  /// \param Cost Maximum acceptable negation cost.
  /// \param Depth Recursion depth limit for the analysis.
  virtual SDValue getNegatedExpression(SDValue Op, SelectionDAG &DAG,
                                       bool LegalOps, bool OptForSize,
                                       NegatibleCost &Cost,
                                       unsigned Depth = 0) const;

  /// Return the negated form of Op if it is no more expensive than the given
  /// threshold, else an empty value.
  ///
  /// @return The negated form of Op if it is no more expensive than the given threshold, else an empty value.
  ///
  /// \param Op SDNode or value being queried.
  /// \param DAG SelectionDAG providing context.
  /// \param LegalOps Restrict results to legal operations.
  /// \param OptForSize Optimize the negation for size.
  /// \param CostThreshold Maximum acceptable cost for the transform.
  /// \param Depth Recursion depth limit for the analysis.
  SDValue getCheaperOrNeutralNegatedExpression(
      SDValue Op, SelectionDAG &DAG, bool LegalOps, bool OptForSize,
      const NegatibleCost CostThreshold = NegatibleCost::Neutral,
      unsigned Depth = 0) const {
    NegatibleCost Cost = NegatibleCost::Expensive;
    SDValue Neg =
        getNegatedExpression(Op, DAG, LegalOps, OptForSize, Cost, Depth);
    if (!Neg)
      return SDValue();

    if (Cost <= CostThreshold)
      return Neg;

    // Remove the new created node to avoid the side effect to the DAG.
    if (Neg->use_empty())
      DAG.RemoveDeadNode(Neg.getNode());
    return SDValue();
  }

  /// This is the helper function to return the newly negated expression only
  /// when the cost is cheaper.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param Op SDNode or value being queried.
  /// \param DAG SelectionDAG providing context.
  /// \param LegalOps Restrict results to legal operations.
  /// \param OptForSize Optimize the negation for size.
  /// \param Depth Recursion depth limit for the analysis.
  SDValue getCheaperNegatedExpression(SDValue Op, SelectionDAG &DAG,
                                      bool LegalOps, bool OptForSize,
                                      unsigned Depth = 0) const {
    return getCheaperOrNeutralNegatedExpression(Op, DAG, LegalOps, OptForSize,
                                                NegatibleCost::Cheaper, Depth);
  }

  /// This is the helper function to return the newly negated expression if
  /// the cost is not expensive.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param Op SDNode or value being queried.
  /// \param DAG SelectionDAG providing context.
  /// \param LegalOps Restrict results to legal operations.
  /// \param OptForSize Optimize the negation for size.
  /// \param Depth Recursion depth limit for the analysis.
  SDValue getNegatedExpression(SDValue Op, SelectionDAG &DAG, bool LegalOps,
                               bool OptForSize, unsigned Depth = 0) const {
    NegatibleCost Cost = NegatibleCost::Expensive;
    return getNegatedExpression(Op, DAG, LegalOps, OptForSize, Cost, Depth);
  }

  //===--------------------------------------------------------------------===//
  // Lowering methods - These methods must be implemented by targets so that
  // the SelectionDAGBuilder code knows how to lower these.
  //

  /// Target-specific splitting of values into parts that fit a register
  /// storing a legal type
  ///
  /// @return True if target-specific splitting of values into parts that fit a register storing a legal type.
  ///
  /// \param DAG SelectionDAG providing context.
  /// \param DL Debug location or data layout, depending on context.
  /// \param Val Minimum number of bit-test compares.
  /// \param Parts Array of register parts.
  /// \param NumParts Number of parts.
  /// \param PartVT VT of each part.
  /// \param CC Compare constant or calling convention.
  virtual bool splitValueIntoRegisterParts(
      SelectionDAG & DAG, const SDLoc &DL, SDValue Val, SDValue *Parts,
      unsigned NumParts, MVT PartVT, std::optional<CallingConv::ID> CC) const {
    return false;
  }

  /// Target-specific combining of register parts into its original value
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param DAG SelectionDAG providing context.
  /// \param DL Debug location or data layout, depending on context.
  /// \param Parts Array of register parts.
  /// \param NumParts Number of parts.
  /// \param PartVT VT of each part.
  /// \param ValueVT Original VT being reassembled.
  /// \param CC Compare constant or calling convention.
  virtual SDValue
  joinRegisterPartsIntoValue(SelectionDAG &DAG, const SDLoc &DL,
                             const SDValue *Parts, unsigned NumParts,
                             MVT PartVT, EVT ValueVT,
                             std::optional<CallingConv::ID> CC) const {
    return SDValue();
  }

  /// Lower the incoming formal arguments of a function into the SelectionDAG.
  ///
  /// This hook must be implemented to lower the incoming (formal) arguments,
  /// described by the Ins array, into the specified DAG. The implementation
  /// should fill in the InVals array with legal-type argument values, and return
  /// the resulting token chain value.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param Chain Incoming token chain.
  /// \param CallConv Calling convention identifier.
  /// \param isVarArg Whether the prototype is vararg.
  /// \param Ins Incoming formal argument descriptors.
  /// \param dl Debug location for newly created nodes.
  /// \param DAG SelectionDAG providing context.
  /// \param InVals Filled with legal-type argument or return values.
  virtual SDValue LowerFormalArguments(
      SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
      const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &dl,
      SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
    llvm_unreachable("Not Implemented");
  }

  /// Optional target hook to add target-specific actions when entering EH pad
  /// blocks. The implementation should return the resulting token chain value.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param Chain Incoming token chain.
  /// \param DL Debug location or data layout, depending on context.
  /// \param DAG SelectionDAG providing context.
  virtual SDValue lowerEHPadEntry(SDValue Chain, const SDLoc &DL,
                                  SelectionDAG &DAG) const {
    return SDValue();
  }

  /// Attach target-specific attributes to the arguments of a libcall.
  ///
  /// \param MF Machine function being lowered.
  /// \param CC Calling convention of the libcall.
  /// \param Args Argument list whose attributes may be annotated.
  virtual void markLibCallAttributes(MachineFunction *MF, unsigned CC,
                                     ArgListTy &Args) const {}

  /// Holds the key and discriminator needed to lower a pointer-authenticated
  /// indirect call.
  ///
  /// This structure contains the information necessary for lowering
  /// pointer-authenticating indirect calls. It is equivalent to the "ptrauth"
  /// operand bundle found on the call instruction, if any.
  struct PtrAuthInfo {
    /// Pointer-authentication key identifier.
    uint64_t Key;
    /// The pointer-authentication discriminator value.
    SDValue Discriminator;
  };

  /// Holds all information necessary for lowering a call to SelectionDAG.
  ///
  /// This structure contains all information that is necessary for lowering
  /// calls. It is passed to TLI::LowerCallTo when the SelectionDAG builder needs
  /// to lower a call, and targets will see this struct in their LowerCall
  /// implementation.
  struct CallLoweringInfo {
    /// Token chain for the call sequence.
    SDValue Chain;
    /// Original unlegalized return type.
    Type *OrigRetTy = nullptr;
    /// Same as OrigRetTy, or partially legalized for soft float libcalls.
    Type *RetTy = nullptr;
    /// Whether the return value should be sign-extended.
    bool RetSExt           : 1;
    /// Whether the return value should be zero-extended.
    bool RetZExt           : 1;
    /// Whether the call is to a variadic function.
    bool IsVarArg          : 1;
    /// Whether the value is passed in a register per the inreg attribute.
    bool IsInReg           : 1;
    /// Whether the call does not return to its caller.
    bool DoesNotReturn     : 1;
    /// Whether the call's return value is used.
    bool IsReturnValueUsed : 1;
    /// Whether the call is convergent.
    bool IsConvergent      : 1;
    /// Whether the call is a patchpoint intrinsic.
    bool IsPatchPoint      : 1;
    /// Whether the call or argument uses preallocated storage.
    bool IsPreallocated : 1;
    /// Whether repeated calls to this function should not be merged.
    bool NoMerge           : 1;

    /// Whether the call has been marked for tail-call conversion.
    bool IsTailCall = false;

    /// Whether call lowering occurs after SelectionDAG type legalization.
    bool IsPostTypeLegalization = false;

    /// The number of fixed (non-variadic) arguments to the call.
    unsigned NumFixedArgs = -1;
    /// The calling convention to use for the call.
    CallingConv::ID CallConv = CallingConv::C;
    /// The callee value or address of the call.
    SDValue Callee;
    /// List of call arguments.
    ArgListTy Args;
    /// SelectionDAG used for lowering.
    SelectionDAG &DAG;
    /// Debug location of the call.
    SDLoc DL;
    /// Original CallBase instruction, if any.
    const CallBase *CB = nullptr;
    /// Outgoing argument descriptors.
    SmallVector<ISD::OutputArg, 32> Outs;
    /// The outgoing argument or return values for the call.
    SmallVector<SDValue, 32> OutVals;
    /// Incoming return-value descriptors.
    SmallVector<ISD::InputArg, 32> Ins;
    /// The incoming return or argument values produced by lowering the call.
    SmallVector<SDValue, 4> InVals;
    /// Control-flow integrity type id for the call.
    const ConstantInt *CFIType = nullptr;
    /// The convergence control token operand for the call, if any.
    SDValue ConvergenceControlToken;
    /// Deactivation symbol associated with the call.
    GlobalValue *DeactivationSymbol = nullptr;

    /// Optional pointer-authentication info for the call.
    std::optional<PtrAuthInfo> PAI;

    /// Construct call-lowering state for the given SelectionDAG.
    ///
    /// \param DAG SelectionDAG used to lower the call.
    CallLoweringInfo(SelectionDAG &DAG)
        : RetSExt(false), RetZExt(false), IsVarArg(false), IsInReg(false),
          DoesNotReturn(false), IsReturnValueUsed(true), IsConvergent(false),
          IsPatchPoint(false), IsPreallocated(false), NoMerge(false),
          DAG(DAG) {}

    /// Set the debug location for the call.
    ///
    /// @return A reference to this object for chaining.
    ///
    /// \param dl Debug location of the call.
    CallLoweringInfo &setDebugLoc(const SDLoc &dl) {
      DL = dl;
      return *this;
    }

    /// Set the incoming token chain for the call.
    ///
    /// @return A reference to this object for chaining.
    ///
    /// \param InChain Incoming token chain.
    CallLoweringInfo &setChain(SDValue InChain) {
      Chain = InChain;
      return *this;
    }

    // setCallee with target/module-specific attributes
    /// Configure this call as a libcall with the given callee and arguments.
    ///
    /// @return A reference to this object for chaining.
    ///
    /// \param CC Calling convention.
    /// \param ResultType Return type.
    /// \param Target Callee operand.
    /// \param ArgsList Argument list.
    CallLoweringInfo &setLibCallee(CallingConv::ID CC, Type *ResultType,
                                   SDValue Target, ArgListTy &&ArgsList) {
      return setLibCallee(CC, ResultType, ResultType, Target,
                          std::move(ArgsList));
    }

    /// Configure this call as a libcall, preserving the original result type.
    ///
    /// @return A reference to this object for chaining.
    ///
    /// \param CC Calling convention.
    /// \param ResultType Legalized return type.
    /// \param OrigResultType Original return type.
    /// \param Target Callee operand.
    /// \param ArgsList Argument list.
    CallLoweringInfo &setLibCallee(CallingConv::ID CC, Type *ResultType,
                                   Type *OrigResultType, SDValue Target,
                                   ArgListTy &&ArgsList) {
      OrigRetTy = OrigResultType;
      RetTy = ResultType;
      Callee = Target;
      CallConv = CC;
      NumFixedArgs = ArgsList.size();
      Args = std::move(ArgsList);

      DAG.getTargetLoweringInfo().markLibCallAttributes(
          &(DAG.getMachineFunction()), CC, Args);
      return *this;
    }

    /// Configure the callee, calling convention, and argument list.
    ///
    /// @return A reference to this object for chaining.
    ///
    /// \param CC Calling convention.
    /// \param ResultType Return type.
    /// \param Target Callee operand.
    /// \param ArgsList Argument list.
    /// \param ResultAttrs Optional return attributes.
    CallLoweringInfo &setCallee(CallingConv::ID CC, Type *ResultType,
                                SDValue Target, ArgListTy &&ArgsList,
                                AttributeSet ResultAttrs = {}) {
      RetTy = OrigRetTy = ResultType;
      IsInReg = ResultAttrs.hasAttribute(Attribute::InReg);
      RetSExt = ResultAttrs.hasAttribute(Attribute::SExt);
      RetZExt = ResultAttrs.hasAttribute(Attribute::ZExt);
      NoMerge = ResultAttrs.hasAttribute(Attribute::NoMerge);

      Callee = Target;
      CallConv = CC;
      NumFixedArgs = ArgsList.size();
      Args = std::move(ArgsList);
      return *this;
    }

    /// Configure the callee from an LLVM IR CallBase.
    ///
    /// @return A reference to this object for chaining.
    ///
    /// \param ResultType Return type.
    /// \param FTy Function type.
    /// \param Target Callee operand.
    /// \param ArgsList Argument list.
    /// \param Call Original call instruction.
    CallLoweringInfo &setCallee(Type *ResultType, FunctionType *FTy,
                                SDValue Target, ArgListTy &&ArgsList,
                                const CallBase &Call) {
      RetTy = OrigRetTy = ResultType;

      IsInReg = Call.hasRetAttr(Attribute::InReg);
      DoesNotReturn =
          Call.doesNotReturn() ||
          (!isa<InvokeInst>(Call) && isa<UnreachableInst>(Call.getNextNode()));
      IsVarArg = FTy->isVarArg();
      IsReturnValueUsed = !Call.use_empty();
      RetSExt = Call.hasRetAttr(Attribute::SExt);
      RetZExt = Call.hasRetAttr(Attribute::ZExt);
      NoMerge = Call.hasFnAttr(Attribute::NoMerge);

      Callee = Target;

      CallConv = Call.getCallingConv();
      NumFixedArgs = FTy->getNumParams();
      Args = std::move(ArgsList);

      CB = &Call;

      return *this;
    }

    /// Mark whether the return value is in a register.
    ///
    /// @return A reference to this object for chaining.
    ///
    /// \param Value Whether the return is inreg.
    CallLoweringInfo &setInRegister(bool Value = true) {
      IsInReg = Value;
      return *this;
    }

    /// Mark whether the callee does not return.
    ///
    /// @return A reference to this object for chaining.
    ///
    /// \param Value Whether the callee does not return.
    CallLoweringInfo &setNoReturn(bool Value = true) {
      DoesNotReturn = Value;
      return *this;
    }

    /// Mark whether the call is vararg.
    ///
    /// @return A reference to this object for chaining.
    ///
    /// \param Value Whether the call is vararg.
    CallLoweringInfo &setVarArg(bool Value = true) {
      IsVarArg = Value;
      return *this;
    }

    /// Mark whether the call is a tail call.
    ///
    /// @return A reference to this object for chaining.
    ///
    /// \param Value Whether the call is a tail call.
    CallLoweringInfo &setTailCall(bool Value = true) {
      IsTailCall = Value;
      return *this;
    }

    /// Mark whether the return value is unused.
    ///
    /// @return A reference to this object for chaining.
    ///
    /// \param Value Whether the return value is discarded.
    CallLoweringInfo &setDiscardResult(bool Value = true) {
      IsReturnValueUsed = !Value;
      return *this;
    }

    /// Mark whether the call is convergent.
    ///
    /// @return A reference to this object for chaining.
    ///
    /// \param Value Whether the call is convergent.
    CallLoweringInfo &setConvergent(bool Value = true) {
      IsConvergent = Value;
      return *this;
    }

    /// Mark whether the return value is sign-extended.
    ///
    /// @return A reference to this object for chaining.
    ///
    /// \param Value Whether the return is sign-extended.
    CallLoweringInfo &setSExtResult(bool Value = true) {
      RetSExt = Value;
      return *this;
    }

    /// Mark whether the return value is zero-extended.
    ///
    /// @return A reference to this object for chaining.
    ///
    /// \param Value Whether the return is zero-extended.
    CallLoweringInfo &setZExtResult(bool Value = true) {
      RetZExt = Value;
      return *this;
    }

    /// Mark whether the call is a patchpoint.
    ///
    /// @return A reference to this object for chaining.
    ///
    /// \param Value Whether the call is a patchpoint.
    CallLoweringInfo &setIsPatchPoint(bool Value = true) {
      IsPatchPoint = Value;
      return *this;
    }

    /// Mark whether the call uses preallocated memory.
    ///
    /// @return A reference to this object for chaining.
    ///
    /// \param Value Whether the call is preallocated.
    CallLoweringInfo &setIsPreallocated(bool Value = true) {
      IsPreallocated = Value;
      return *this;
    }

    /// Attach pointer-authentication info to the call.
    ///
    /// @return A reference to this object for chaining.
    ///
    /// \param Value Pointer-authentication descriptor.
    CallLoweringInfo &setPtrAuth(PtrAuthInfo Value) {
      PAI = Value;
      return *this;
    }

    /// Mark whether lowering runs after type legalization.
    ///
    /// @return A reference to this object for chaining.
    ///
    /// \param Value Whether after type legalization.
    CallLoweringInfo &setIsPostTypeLegalization(bool Value=true) {
      IsPostTypeLegalization = Value;
      return *this;
    }

    /// Set the CFI type id for the call.
    ///
    /// @return A reference to this object for chaining.
    ///
    /// \param Type CFI type identifier constant.
    CallLoweringInfo &setCFIType(const ConstantInt *Type) {
      CFIType = Type;
      return *this;
    }

    /// Set the convergence control token for the call.
    ///
    /// @return A reference to this object for chaining.
    ///
    /// \param Token Convergence control token.
    CallLoweringInfo &setConvergenceControlToken(SDValue Token) {
      ConvergenceControlToken = Token;
      return *this;
    }

    /// Set the deactivation symbol for the call.
    ///
    /// @return A reference to this object for chaining.
    ///
    /// \param Sym Deactivation symbol.
    CallLoweringInfo &setDeactivationSymbol(GlobalValue *Sym) {
      DeactivationSymbol = Sym;
      return *this;
    }

    /// Return the mutable argument list for this call.
    ///
    /// @return The list of call arguments.
    ArgListTy &getArgs() {
      return Args;
    }
  };

  /// This structure is used to pass arguments to makeLibCall function.
  struct MakeLibCallOptions {
    /// The operand types before float softening, used to recover the original
    /// types.
    ArrayRef<EVT> OpsVTBeforeSoften;
    /// The return type before float softening.
    EVT RetVTBeforeSoften;
    /// Optional overrides for the IR argument types passed to the libcall.
    ArrayRef<Type *> OpsTypeOverrides;

    /// Whether the libcall operands should be treated as signed.
    bool IsSigned : 1;
    /// Whether the call does not return to its caller.
    bool DoesNotReturn : 1;
    /// Whether the call's return value is used.
    bool IsReturnValueUsed : 1;
    /// Whether call lowering occurs after SelectionDAG type legalization.
    bool IsPostTypeLegalization : 1;
    /// Whether the call's types were derived from float-softening legalization.
    bool IsSoften : 1;

    /// Construct default libcall emission options.
    MakeLibCallOptions()
        : IsSigned(false), DoesNotReturn(false), IsReturnValueUsed(true),
          IsPostTypeLegalization(false), IsSoften(false) {}

    /// Set whether libcall operands are signed.
    ///
    /// @return A reference to this object for chaining.
    ///
    /// \param Value Whether operands are signed.
    MakeLibCallOptions &setIsSigned(bool Value = true) {
      IsSigned = Value;
      return *this;
    }

    /// Set whether the libcall does not return.
    ///
    /// @return A reference to this object for chaining.
    ///
    /// \param Value Whether the libcall does not return.
    MakeLibCallOptions &setNoReturn(bool Value = true) {
      DoesNotReturn = Value;
      return *this;
    }

    /// Set whether the libcall return value is unused.
    ///
    /// @return A reference to this object for chaining.
    ///
    /// \param Value Whether the return is discarded.
    MakeLibCallOptions &setDiscardResult(bool Value = true) {
      IsReturnValueUsed = !Value;
      return *this;
    }

    /// Set whether emission is after type legalization.
    ///
    /// @return A reference to this object for chaining.
    ///
    /// \param Value Whether after type legalization.
    MakeLibCallOptions &setIsPostTypeLegalization(bool Value = true) {
      IsPostTypeLegalization = Value;
      return *this;
    }

    /// Record operand and return VTs from before float softening.
    ///
    /// @return A reference to this object for chaining.
    ///
    /// \param OpsVT Operand VTs before softening.
    /// \param RetVT Return VT before softening.
    MakeLibCallOptions &setTypeListBeforeSoften(ArrayRef<EVT> OpsVT, EVT RetVT) {
      OpsVTBeforeSoften = OpsVT;
      RetVTBeforeSoften = RetVT;
      IsSoften = true;
      return *this;
    }

    /// Override the argument type for an operand. Leave the type as null to use
    /// the type from the operand's node.
    ///
    /// @return A reference to this object for chaining.
    ///
    /// \param OpsTypes Per-operand type overrides; null entries keep the node type.
    MakeLibCallOptions &setOpsTypeOverrides(ArrayRef<Type *> OpsTypes) {
      OpsTypeOverrides = OpsTypes;
      return *this;
    }
  };

  /// Lower an abstract call description into an actual call by invoking
  /// LowerCall.
  ///
  /// This function lowers an abstract call to a function into an actual call.
  /// This returns a pair of operands. The first element is the return value for
  /// the function (if RetTy is not VoidTy). The second element is the outgoing
  /// token chain. It calls LowerCall to do the actual lowering.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param CLI CallLoweringInfo for the call.
  std::pair<SDValue, SDValue> LowerCallTo(CallLoweringInfo &CLI) const;

  /// Lower a call into the SelectionDAG, filling in InVals with the call's
  /// return values.
  ///
  /// This hook must be implemented to lower calls into the specified DAG. The
  /// outgoing arguments to the call are described by the Outs array, and the
  /// values to be returned by the call are described by the Ins array. The
  /// implementation should fill in the InVals array with legal-type return
  /// values from the call, and return the resulting token chain value.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param CLI CallLoweringInfo for the call.
  /// \param InVals Filled with legal-type argument or return values.
  virtual SDValue
    LowerCall(CallLoweringInfo &CLI,
              SmallVectorImpl<SDValue> &InVals) const {
    llvm_unreachable("Not Implemented");
  }

  /// Target-specific cleanup for formal ByVal parameters.
  /// \param State Machine function or lowering state object.
  /// \param Size Maximum jump-table size, or zero for unlimited.
  /// \param Alignment Required or preferred alignment.
  virtual void HandleByVal(CCState *State, unsigned &Size, Align Alignment) const {}

  /// Return true if the specified return values can fit into the target's return
  /// registers.
  ///
  /// This hook should be implemented to check whether the return values
  /// described by the Outs array can fit into the return registers. If false is
  /// returned, an sret-demotion is performed.
  ///
  /// @return True if the specified return values can fit into the target's return registers.
  ///
  /// \param CallConv Calling convention identifier.
  /// \param MF Machine function being lowered.
  /// \param isVarArg Whether the prototype is vararg.
  /// \param Outs Filled with output argument infos.
  /// \param Context LLVMContext used for type creation.
  /// \param RetTy Candidate return type.
  virtual bool CanLowerReturn(CallingConv::ID CallConv,
                              MachineFunction &MF, bool isVarArg,
               const SmallVectorImpl<ISD::OutputArg> &Outs,
               LLVMContext &Context, const Type *RetTy) const
  {
    // Return true by default to get preexisting behavior.
    return true;
  }

  /// Annotate a stack object pointer with known-bits assertions.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param Ptr Pointer to annotate.
  /// \param DAG SelectionDAG providing context.
  /// \param DL Debug location or data layout, depending on context.
  /// \param Alignment Required or preferred alignment.
  SDValue annotateStackObjectPointer(SDValue Ptr, SelectionDAG &DAG,
                                     const SDLoc &DL, Align Alignment) const;

  /// Lower the outgoing return values of a function into the SelectionDAG.
  ///
  /// This hook must be implemented to lower outgoing return values, described by
  /// the Outs array, into the specified DAG. The implementation should return
  /// the resulting token chain value.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param Chain Incoming token chain.
  /// \param CallConv Calling convention identifier.
  /// \param isVarArg Whether the prototype is vararg.
  /// \param Outs Filled with output argument infos.
  /// \param OutVals Outgoing return or argument values.
  /// \param dl Debug location for newly created nodes.
  /// \param DAG SelectionDAG providing context.
  virtual SDValue LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                              bool isVarArg,
                              const SmallVectorImpl<ISD::OutputArg> &Outs,
                              const SmallVectorImpl<SDValue> &OutVals,
                              const SDLoc &dl,
                              SelectionDAG &DAG) const {
    llvm_unreachable("Not Implemented");
  }

  /// Return true if result of the specified node is used by a return node
  /// only. It also compute and return the input chain for the tail call.
  ///
  /// This is used to determine whether it is possible to codegen a libcall as
  /// tail call at legalization time.
  ///
  /// @return True if result of the specified node is used by a return node only. It also compute and return the input chain for the tail call.
  ///
  /// \param N SDNode being queried.
  /// \param Chain Incoming token chain.
  virtual bool isUsedByReturnOnly(SDNode *N, SDValue &Chain) const {
    return false;
  }

  /// Return true if the given call may be emitted as a tail call by the target.
  ///
  /// Return true if the target may be able emit the call instruction as a tail
  /// call. This is used by optimization passes to determine if it's profitable
  /// to duplicate return instructions to enable tailcall optimization.
  ///
  /// @return True if the given call may be emitted as a tail call by the target.
  ///
  /// \param CI Call instruction whose pointer args may be aligned.
  virtual bool mayBeEmittedAsTailCall(const CallInst *CI) const {
    return false;
  }

  /// Return the physical register corresponding to the given register name.
  ///
  /// Return the register ID of the name passed in. Used by named register global
  /// variables extension. There is no target-independent behaviour so the
  /// default action is to bail.
  ///
  /// @return The physical register corresponding to the given register name.
  ///
  /// \param RegName Name of the named register.
  /// \param Ty Type of the constant or operation.
  /// \param MF Machine function being lowered.
  virtual Register getRegisterByName(const char* RegName, LLT Ty,
                                     const MachineFunction &MF) const {
    reportFatalUsageError("Named registers not implemented for this target");
  }

  /// Return the type to use when zero- or sign-extending an integer return
  /// value.
  ///
  /// Return the type that should be used to zero or sign extend a
  /// zeroext/signext integer return value. FIXME: Some C calling conventions
  /// require the return type to be promoted, but this is not true all the time,
  /// e.g. i1/i8/i16 on x86/x86_64. It is also not necessary for non-C calling
  /// conventions. The frontend should handle this and include all of the
  /// necessary information.
  ///
  /// @return The type to use when zero- or sign-extending an integer return value.
  ///
  /// \param Context LLVMContext used for type creation.
  /// \param VT Value type being queried or transformed.
  /// \param ExtendKind Zero- or sign-extension kind for the return.
  virtual EVT getTypeForExtReturn(LLVMContext &Context, EVT VT,
                                       ISD::NodeType ExtendKind) const {
    EVT MinVT = getRegisterType(Context, MVT::i32);
    return VT.bitsLT(MinVT) ? MinVT : VT;
  }

  /// Return true if the argument type must be passed in a block of consecutive
  /// registers.
  ///
  /// For some targets, an LLVM struct type must be broken down into multiple
  /// simple types, but the calling convention specifies that the entire struct
  /// must be passed in a block of consecutive registers.
  ///
  /// @return True if the argument type must be passed in a block of consecutive registers.
  ///
  /// \param Ty Type of the constant or operation.
  /// \param CallConv Calling convention identifier.
  /// \param isVarArg Whether the prototype is vararg.
  /// \param DL Debug location or data layout, depending on context.
  virtual bool
  functionArgumentNeedsConsecutiveRegisters(Type *Ty, CallingConv::ID CallConv,
                                            bool isVarArg,
                                            const DataLayout &DL) const {
    return false;
  }

  /// Return true if split function arguments should be ordered as on a
  /// little-endian target.
  ///
  /// For most targets, an LLVM type must be broken down into multiple smaller
  /// types. Usually the halves are ordered according to the endianness but for
  /// some platform that would break. So this method will default to matching the
  /// endianness but can be overridden.
  ///
  /// @return True if split function arguments should be ordered as on a little-endian target.
  ///
  /// \param DL Debug location or data layout, depending on context.
  virtual bool
  shouldSplitFunctionArgumentsAsLittleEndian(const DataLayout &DL) const {
    return DL.isLittleEndian();
  }

  /// Returns a 0 terminated array of registers that can be safely used as
  /// scratch registers.
  ///
  /// @return A 0 terminated array of registers that can be safely used as scratch registers.
  ///
  /// \param CC Calling convention being queried.
  virtual const MCPhysReg *getScratchRegisters(CallingConv::ID CC) const {
    return nullptr;
  }

  /// Returns a 0 terminated array of rounding control registers that can be
  /// attached into strict FP call.
  ///
  /// @return A 0 terminated array of rounding control registers that can be attached into strict FP call.
  virtual ArrayRef<MCPhysReg> getRoundingControlRegisters() const {
    return ArrayRef<MCPhysReg>();
  }

  /// This callback is used to prepare for a volatile or atomic load.
  /// It takes a chain node as input and returns the chain for the load itself.
  ///
  /// Having a callback like this is necessary for targets like SystemZ,
  /// which allows a CPU to reuse the result of a previous load indefinitely,
  /// even if a cache-coherent store is performed by another CPU.  The default
  /// implementation does nothing.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param Chain Incoming token chain.
  /// \param DL Debug location or data layout, depending on context.
  /// \param DAG SelectionDAG providing context.
  virtual SDValue prepareVolatileOrAtomicLoad(SDValue Chain, const SDLoc &DL,
                                              SelectionDAG &DAG) const {
    return Chain;
  }

  /// Custom-lower a node with an illegal operand type but legal result types.
  ///
  /// This callback is invoked by the type legalizer to legalize nodes with an
  /// illegal operand type but legal result types. It replaces the LowerOperation
  /// callback in the type Legalizer. The reason we can not do away with
  /// LowerOperation entirely is that LegalizeDAG isn't yet ready to use this
  /// callback.
  /// TODO: Consider merging with ReplaceNodeResults.
  ///
  /// The target places new result values for the node in Results (their number
  /// and types must exactly match those of the original return values of
  /// the node), or leaves Results empty, which indicates that the node is not
  /// to be custom lowered after all.
  /// The default implementation calls LowerOperation.
  ///
  /// \param N SDNode being queried.
  /// \param Results Replacement results.
  /// \param DAG SelectionDAG providing context.
  virtual void LowerOperationWrapper(SDNode *N,
                                     SmallVectorImpl<SDValue> &Results,
                                     SelectionDAG &DAG) const;

  /// Lower an operation that the target has marked for custom lowering.
  ///
  /// This callback is invoked for operations that are unsupported by the target,
  /// which are registered to use 'custom' lowering, and whose defined values are
  /// all legal. If the target has no operations that require custom lowering, it
  /// need not implement this. The default implementation of this aborts.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param Op SDNode or value being queried.
  /// \param DAG SelectionDAG providing context.
  virtual SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const;

  /// Custom-lower a node whose result type is illegal for the target.
  ///
  /// This callback is invoked when a node result type is illegal for the target,
  /// and the operation was registered to use 'custom' lowering for that result
  /// type. The target places new result values for the node in Results (their
  /// number and types must exactly match those of the original return values of
  /// the node), or leaves Results empty, which indicates that the node is not to
  /// be custom lowered after all.
  /// If the target has no operations that require custom lowering, it need not
  /// implement this.  The default implementation aborts.
  ///
  /// \param N SDNode being queried.
  /// \param Results Replacement results.
  /// \param DAG SelectionDAG providing context.
  virtual void ReplaceNodeResults(SDNode *N,
                                  SmallVectorImpl<SDValue> &Results,
                                  SelectionDAG &DAG) const {
    llvm_unreachable("ReplaceNodeResults not implemented for this target!");
  }

  /// This method returns the name of a target specific DAG node.
  ///
  /// @return The name of the target-specific DAG node, or null if unknown.
  ///
  /// \param Opcode Target-specific DAG node opcode.
  virtual const char *getTargetNodeName(unsigned Opcode) const;

  /// This method returns a target specific FastISel object, or null if the
  /// target does not support "fast" ISel.
  ///
  /// @return A target-specific FastISel object, or null if unsupported.
  ///
  /// \param FLI Function lowering info for fast ISel.
  /// \param LibInfo Target library info, or null.
  /// \param LibcallInfo Libcall lowering info, or null.
  virtual FastISel *createFastISel(FunctionLoweringInfo &FLI,
                                   const TargetLibraryInfo *LibInfo,
                                   const LibcallLoweringInfo *LibcallInfo) const {
    return nullptr;
  }

  //===--------------------------------------------------------------------===//
  // Inline Asm Support hooks
  //

  /// Classification of an inline-asm constraint code.
  enum ConstraintType {
    /// Constraint represents specific register(s).
    C_Register,
    /// Constraint represents any register in a class.
    C_RegisterClass,
    /// Memory constraint.
    C_Memory,
    /// Address constraint.
    C_Address,
    /// Requires an immediate.
    C_Immediate,
    /// Something else.
    C_Other,
    /// Unsupported constraint.
    C_Unknown
  };

  /// Relative preference weight for matching an inline-asm constraint.
  enum ConstraintWeight {
    /// No match.
    CW_Invalid  = -1,
    /// Acceptable match.
    CW_Okay     = 0,
    /// Good match.
    CW_Good     = 1,
    /// Better match.
    CW_Better   = 2,
    /// Best match.
    CW_Best     = 3,

    /// Weight for specific-register operands.
    CW_SpecificReg  = CW_Okay,
    /// Weight for register operands.
    CW_Register     = CW_Good,
    /// Weight for memory operands.
    CW_Memory       = CW_Better,
    /// Weight for constant operands.
    CW_Constant     = CW_Best,
    /// Default weight when the constraint kind is unknown.
    CW_Default      = CW_Okay
  };

  /// This contains information for each constraint that we are lowering.
  /// \param Info InlineAsm::ConstraintInfo to wrap.
  struct AsmOperandInfo : public InlineAsm::ConstraintInfo {
    /// This contains the actual string for the code, like "m".  TargetLowering
    /// picks the 'best' code from ConstraintInfo::Codes that most closely
    /// matches the operand.
    std::string ConstraintCode;

    /// Information about the constraint code, e.g. Register, RegisterClass,
    /// Memory, Other, Unknown.
    TargetLowering::ConstraintType ConstraintType = TargetLowering::C_Unknown;

    /// Incoming operand value for this asm operand, or null for outputs/clobbers.
    ///
    /// If this is the result output operand or a clobber, this is null,
    /// otherwise it is the incoming operand to the CallInst.  This gets
    /// modified as the asm is processed.
    Value *CallOperandVal = nullptr;

    /// The ValueType for the operand value.
    MVT ConstraintVT = MVT::Other;

    /// Copy constructor for copying from a ConstraintInfo.
    ///
    /// \param Info Constraint info to copy from.
    AsmOperandInfo(InlineAsm::ConstraintInfo Info)
        : InlineAsm::ConstraintInfo(std::move(Info)) {}

    /// Return true of this is an input operand that is a matching constraint
    /// like "4".
    ///
    /// @return True if this is a matching input constraint.
    LLVM_ABI bool isMatchingInputConstraint() const;

    /// If this is an input matching constraint, this method returns the output
    /// operand it matches.
    ///
    /// @return The index of the matched operand.
    LLVM_ABI unsigned getMatchedOperand() const;
  };

  /// A list of inline asm operand descriptors.
  using AsmOperandInfoVector = std::vector<AsmOperandInfo>;

  /// Split an inline asm constraint string into individual constraints tied to
  /// their operands.
  ///
  /// Split up the constraint string from the inline assembly value into the
  /// specific constraints and their prefixes, and also tie in the associated
  /// operand values. If this returns an empty vector, and if the constraint
  /// string itself isn't empty, there was an error parsing.
  ///
  /// @return Split an inline asm constraint string into individual constraints tied to their operands.
  ///
  /// \param DL Debug location or data layout, depending on context.
  /// \param TRI Target register info.
  /// \param Call Libcall identifier or call instruction.
  virtual AsmOperandInfoVector ParseConstraints(const DataLayout &DL,
                                                const TargetRegisterInfo *TRI,
                                                const CallBase &Call) const;

  /// Examine constraint type and operand type and determine a weight value.
  /// The operand object must already have been set up with the operand type.
  ///
  /// @return Examine constraint type and operand type and determine a weight value. The operand object must already have been set up with the operand type.
  ///
  /// \param info AsmOperandInfo being weighed.
  /// \param maIndex Multiple-alternative constraint index.
  virtual ConstraintWeight getMultipleConstraintMatchWeight(
      AsmOperandInfo &info, int maIndex) const;

  /// Examine constraint string and operand type and determine a weight value.
  /// The operand object must already have been set up with the operand type.
  ///
  /// @return Examine constraint string and operand type and determine a weight value. The operand object must already have been set up with the operand type.
  ///
  /// \param info AsmOperandInfo being weighed.
  /// \param constraint Constraint code.
  virtual ConstraintWeight getSingleConstraintMatchWeight(
      AsmOperandInfo &info, const char *constraint) const;

  /// Determine the constraint code and constraint type to use for an inline asm
  /// operand.
  ///
  /// Determines the constraint code and constraint type to use for the specific
  /// AsmOperandInfo, setting OpInfo.ConstraintCode and OpInfo.ConstraintType. If
  /// the actual operand being passed in is available, it can be passed in as Op,
  /// otherwise an empty SDValue can be passed.
  ///
  /// \param OpInfo AsmOperandInfo being classified.
  /// \param Op SDNode or value being queried.
  /// \param DAG SelectionDAG providing context.
  virtual void ComputeConstraintToUse(AsmOperandInfo &OpInfo,
                                      SDValue Op,
                                      SelectionDAG *DAG = nullptr) const;

  /// Given a constraint, return the type of constraint it is for this target.
  ///
  /// @return The string value.
  ///
  /// \param Constraint Inline-asm constraint string.
  virtual ConstraintType getConstraintType(StringRef Constraint) const;

  /// A pairing of a constraint code string with its constraint type.
  using ConstraintPair = std::pair<StringRef, TargetLowering::ConstraintType>;
  /// A prioritized list of constraint codes and their types for an operand.
  using ConstraintGroup = SmallVector<ConstraintPair>;
  /// Return the inline asm constraint codes for an operand, sorted by
  /// preference.
  ///
  /// Given an OpInfo with list of constraints codes as strings, return a sorted
  /// Vector of pairs of constraint codes and their types in priority of what
  /// we'd prefer to lower them as. This may contain immediates that cannot be
  /// lowered, but it is meant to be a machine agnostic order of preferences.
  ///
  /// @return The inline asm constraint codes for an operand, sorted by preference.
  ///
  /// \param OpInfo AsmOperandInfo being classified.
  ConstraintGroup getConstraintPreferences(AsmOperandInfo &OpInfo) const;

  /// Given a physical register constraint (e.g.  {edx}), return the register
  /// number and the register class for the register.
  ///
  /// Given a register class constraint, like 'r', if this corresponds directly
  /// to an LLVM register class, return a register of 0 and the register class
  /// pointer.
  ///
  /// This should only be used for C_Register constraints.  On error, this
  /// returns a register number of 0 and a null register class pointer.
  ///
  /// @return A pair of SelectionDAG values produced by the expansion.
  ///
  /// \param TRI Target register info.
  /// \param Constraint Inline-asm constraint string.
  /// \param VT Value type being queried or transformed.
  virtual std::pair<unsigned, const TargetRegisterClass *>
  getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                               StringRef Constraint, MVT VT) const;

  /// Map an inline asm memory constraint code string to its ConstraintCode enum
  /// value.
  ///
  /// @return The string value.
  ///
  /// \param ConstraintCode Inline-asm constraint code string.
  virtual InlineAsm::ConstraintCode
  getInlineAsmMemConstraint(StringRef ConstraintCode) const {
    if (ConstraintCode == "m")
      return InlineAsm::ConstraintCode::m;
    if (ConstraintCode == "o")
      return InlineAsm::ConstraintCode::o;
    if (ConstraintCode == "X")
      return InlineAsm::ConstraintCode::X;
    if (ConstraintCode == "p")
      return InlineAsm::ConstraintCode::p;
    return InlineAsm::ConstraintCode::Unknown;
  }

  /// Replace the generic 'X' inline asm constraint with a more specific one
  /// based on the operand type.
  ///
  /// Try to replace an X constraint, which matches anything, with another that
  /// has more specific requirements based on the type of the corresponding
  /// operand. This returns null if there is no replacement to make.
  ///
  /// @return A pointer to the requested object, or null if unavailable.
  ///
  /// \param ConstraintVT Value type of the corresponding operand.
  virtual const char *LowerXConstraint(EVT ConstraintVT) const;

  /// Lower the specified operand into the Ops vector.  If it is invalid, don't
  /// add anything to Ops.
  /// \param Op SDNode or value being queried.
  /// \param Constraint Inline-asm constraint string.
  /// \param Ops Operands passed to the libcall or asm.
  /// \param DAG SelectionDAG providing context.
  virtual void LowerAsmOperandForConstraint(SDValue Op, StringRef Constraint,
                                            std::vector<SDValue> &Ops,
                                            SelectionDAG &DAG) const;

  /// Lower a custom inline asm output constraint into a value.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param Chain Incoming token chain.
  /// \param Glue Glue operand chaining the nodes.
  /// \param DL Debug location or data layout, depending on context.
  /// \param OpInfo AsmOperandInfo being classified.
  /// \param DAG SelectionDAG providing context.
  virtual SDValue LowerAsmOutputForConstraint(SDValue &Chain, SDValue &Glue,
                                              const SDLoc &DL,
                                              const AsmOperandInfo &OpInfo,
                                              SelectionDAG &DAG) const;

  /// Collect the SelectionDAG operands for a target intrinsic call.
  ///
  /// \param I Instruction being queried.
  /// \param Ops Operands passed to the libcall or asm.
  /// \param DAG SelectionDAG providing context.
  virtual void CollectTargetIntrinsicOperands(const CallInst &I,
                                              SmallVectorImpl<SDValue> &Ops,
                                              SelectionDAG &DAG) const;

  //===--------------------------------------------------------------------===//
  // Div utility functions
  //

  /// Build the expansion of a signed division using reciprocal multiplication.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param N SDNode being queried.
  /// \param DAG SelectionDAG providing context.
  /// \param IsAfterLegalization Whether full DAG legalization has completed.
  /// \param IsAfterLegalTypes Whether type legalization has completed.
  /// \param Created Nodes created by custom lowering.
  SDValue BuildSDIV(SDNode *N, SelectionDAG &DAG, bool IsAfterLegalization,
                    bool IsAfterLegalTypes,
                    SmallVectorImpl<SDNode *> &Created) const;
  /// Build the expansion of an unsigned division using reciprocal
  /// multiplication.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param N SDNode being queried.
  /// \param DAG SelectionDAG providing context.
  /// \param IsAfterLegalization Whether full DAG legalization has completed.
  /// \param IsAfterLegalTypes Whether type legalization has completed.
  /// \param Created Nodes created by custom lowering.
  SDValue BuildUDIV(SDNode *N, SelectionDAG &DAG, bool IsAfterLegalization,
                    bool IsAfterLegalTypes,
                    SmallVectorImpl<SDNode *> &Created) const;
  /// Build a signed division by a power of two using conditional-move
  /// instructions.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param N SDNode being queried.
  /// \param Divisor Constant power-of-two divisor.
  /// \param DAG SelectionDAG providing context.
  /// \param Created Nodes created by custom lowering.
  SDValue buildSDIVPow2WithCMov(SDNode *N, const APInt &Divisor,
                                SelectionDAG &DAG,
                                SmallVectorImpl<SDNode *> &Created) const;

  /// Provide custom lowering of signed division by a power-of-2 constant.
  ///
  /// Targets may override this function to provide custom SDIV lowering for
  /// power-of-2 denominators. If the target returns an empty SDValue, LLVM
  /// assumes SDIV is expensive and replaces it with a series of other integer
  /// operations.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param N SDNode being queried.
  /// \param Divisor Constant power-of-two divisor.
  /// \param DAG SelectionDAG providing context.
  /// \param Created Nodes created by custom lowering.
  virtual SDValue BuildSDIVPow2(SDNode *N, const APInt &Divisor,
                                SelectionDAG &DAG,
                                SmallVectorImpl<SDNode *> &Created) const;

  /// Provide custom lowering of signed remainder by a power-of-2 constant.
  ///
  /// Targets may override this function to provide custom SREM lowering for
  /// power-of-2 denominators. If the target returns an empty SDValue, LLVM
  /// assumes SREM is expensive and replaces it with a series of other integer
  /// operations.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param N SDNode being queried.
  /// \param Divisor Constant power-of-two divisor.
  /// \param DAG SelectionDAG providing context.
  /// \param Created Nodes created by custom lowering.
  virtual SDValue BuildSREMPow2(SDNode *N, const APInt &Divisor,
                                SelectionDAG &DAG,
                                SmallVectorImpl<SDNode *> &Created) const;

  /// Return the minimum number of uses of a divisor needed before combining
  /// repeated FDIVs by it.
  ///
  /// Indicate whether this target prefers to combine FDIVs with the same
  /// divisor. If the transform should never be done, return zero. If the
  /// transform should be done, return the minimum number of divisor uses that
  /// must exist.
  ///
  /// @return The minimum number of uses of a divisor needed before combining repeated FDIVs by it.
  virtual unsigned combineRepeatedFPDivisors() const {
    return 0;
  }

  /// Hooks for building estimates in place of slower divisions and square
  /// roots.

  /// Return a square-root or reciprocal square-root estimate for the input
  /// operand.
  ///
  /// Return either a square root or its reciprocal estimate value for the input
  /// operand.
  ///
  /// \p Enabled is a ReciprocalEstimate enum with value either 'Unspecified' or
  /// 'Enabled' as set by a potential default override attribute.
  /// If \p RefinementSteps is 'Unspecified', the number of Newton-Raphson
  /// refinement iterations required to generate a sufficient (though not
  /// necessarily IEEE-754 compliant) estimate is returned in that parameter.
  /// The boolean UseOneConstNR output is used to select a Newton-Raphson
  /// algorithm implementation that uses either one or two constants.
  /// The boolean Reciprocal is used to select whether the estimate is for the
  /// square root of the input operand or the reciprocal of its square root.
  /// A target may choose to implement its own refinement within this function.
  /// If that's true, then return '0' as the number of RefinementSteps to avoid
  /// any further refinement of the estimate.
  /// An empty SDValue return means no estimate sequence can be created.
  ///
  /// @return A square-root or reciprocal square-root estimate for the input operand.
  ///
  /// \param Operand Input operand to the estimate.
  /// \param DAG SelectionDAG providing context.
  /// \param Enabled Whether reciprocal estimates are enabled.
  /// \param RefinementSteps NR refinement step count.
  /// \param UseOneConstNR Use the one-constant NR form.
  /// \param Reciprocal Estimate rsqrt instead of sqrt.
  virtual SDValue getSqrtEstimate(SDValue Operand, SelectionDAG &DAG,
                                  int Enabled, int &RefinementSteps,
                                  bool &UseOneConstNR, bool Reciprocal) const {
    return SDValue();
  }

  /// Convert an FMINNUM/FMAXNUM node into an equivalent compare-and-select
  /// sequence.
  ///
  /// Try to convert the fminnum/fmaxnum to a compare/select sequence. This is
  /// required for correctness since InstCombine might have canonicalized a
  /// fcmp+select sequence to a FMINNUM/FMAXNUM intrinsic. If we were to fall
  /// through to the default expansion/soften to libcall, we might introduce a
  /// link-time dependency on libm into a file that originally did not have one.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param Node SDNode being expanded or analyzed.
  /// \param DAG SelectionDAG providing context.
  SDValue createSelectForFMINNUM_FMAXNUM(SDNode *Node, SelectionDAG &DAG) const;

  /// Return a reciprocal estimate for the input operand, or an empty value if
  /// unsupported.
  ///
  /// Return a reciprocal estimate value for the input operand.
  ///
  /// \p Enabled is a ReciprocalEstimate enum with value either 'Unspecified' or
  /// 'Enabled' as set by a potential default override attribute.
  /// If \p RefinementSteps is 'Unspecified', the number of Newton-Raphson
  /// refinement iterations required to generate a sufficient (though not
  /// necessarily IEEE-754 compliant) estimate is returned in that parameter.
  /// A target may choose to implement its own refinement within this function.
  /// If that's true, then return '0' as the number of RefinementSteps to avoid
  /// any further refinement of the estimate.
  /// An empty SDValue return means no estimate sequence can be created.
  ///
  /// @return A reciprocal estimate for the input operand, or an empty value if unsupported.
  ///
  /// \param Operand Input operand to the estimate.
  /// \param DAG SelectionDAG providing context.
  /// \param Enabled Whether reciprocal estimates are enabled.
  /// \param RefinementSteps NR refinement step count.
  virtual SDValue getRecipEstimate(SDValue Operand, SelectionDAG &DAG,
                                   int Enabled, int &RefinementSteps) const {
    return SDValue();
  }

  /// Return a target-specific test of whether the operand is suitable for a
  /// square-root estimate.
  ///
  /// Return a target-dependent comparison result if the input operand is
  /// suitable for use with a square root estimate calculation. For example, the
  /// comparison may check if the operand is NAN, INF, zero, normal, etc. The
  /// result should be used as the condition operand for a select or branch.
  ///
  /// @return A target-specific test of whether the operand is suitable for a square-root estimate.
  ///
  /// \param Operand Input operand to the estimate.
  /// \param DAG SelectionDAG providing context.
  /// \param Mode Denormal mode for the sqrt test.
  /// \param Flags Flags controlling the operation.
  virtual SDValue getSqrtInputTest(SDValue Operand, SelectionDAG &DAG,
                                   const DenormalMode &Mode,
                                   SDNodeFlags Flags = {}) const;

  /// Return a target-dependent result if the input operand is not suitable for
  /// use with a square root estimate calculation.
  ///
  /// @return A target-dependent result if the input operand is not suitable for use with a square root estimate calculation.
  ///
  /// \param Operand Input operand to the estimate.
  /// \param DAG SelectionDAG providing context.
  virtual SDValue getSqrtResultForDenormInput(SDValue Operand,
                                              SelectionDAG &DAG) const {
    return DAG.getConstantFP(0.0, SDLoc(Operand), Operand.getValueType());
  }

  //===--------------------------------------------------------------------===//
  // Legalization utility functions
  //

  /// Expand a MUL or [US]MUL_LOHI of n-bit values into two or four nodes,
  /// respectively, each computing an n/2-bit part of the result.
  /// \param Result A vector that will be filled with the parts of the result
  ///        in little-endian order.
  /// \param LL Low bits of the LHS of the MUL.  You can use this parameter
  ///        if you want to control how low bits are extracted from the LHS.
  /// \param LH High bits of the LHS of the MUL.  See LL for meaning.
  /// \param RL Low bits of the RHS of the MUL.  See LL for meaning
  /// \param RH High bits of the RHS of the MUL.  See LL for meaning.
  /// \returns true if the node has been expanded, false if it has not
  /// \param Opcode ISD or target opcode.
  /// \param VT Value type being queried or transformed.
  /// \param dl Debug location for newly created nodes.
  /// \param LHS Left-hand operand.
  /// \param RHS Right-hand operand.
  /// \param HiLoVT Value type used for the lo/hi halves.
  /// \param DAG SelectionDAG providing context.
  /// \param Kind Undef/poison tracking kind.
  bool expandMUL_LOHI(unsigned Opcode, EVT VT, const SDLoc &dl, SDValue LHS,
                      SDValue RHS, SmallVectorImpl<SDValue> &Result, EVT HiLoVT,
                      SelectionDAG &DAG, MulExpansionKind Kind,
                      SDValue LL = SDValue(), SDValue LH = SDValue(),
                      SDValue RL = SDValue(), SDValue RH = SDValue()) const;

  /// Expand a MUL into two nodes.  One that computes the high bits of
  /// the result and one that computes the low bits.
  /// \param HiLoVT The value type to use for the Lo and Hi nodes.
  /// \param LL Low bits of the LHS of the MUL.  You can use this parameter
  ///        if you want to control how low bits are extracted from the LHS.
  /// \param LH High bits of the LHS of the MUL.  See LL for meaning.
  /// \param RL Low bits of the RHS of the MUL.  See LL for meaning
  /// \param RH High bits of the RHS of the MUL.  See LL for meaning.
  /// \returns true if the node has been expanded. false if it has not
  /// \param N SDNode being queried.
  /// \param Lo Low half of a wide result.
  /// \param Hi High half of a wide result.
  /// \param DAG SelectionDAG providing context.
  /// \param Kind Undef/poison tracking kind.
  bool expandMUL(SDNode *N, SDValue &Lo, SDValue &Hi, EVT HiLoVT,
                 SelectionDAG &DAG, MulExpansionKind Kind,
                 SDValue LL = SDValue(), SDValue LH = SDValue(),
                 SDValue RL = SDValue(), SDValue RH = SDValue()) const;

  /// Expand a division or remainder by a constant using a narrower-width
  /// algorithm.
  ///
  /// Attempt to expand an n-bit div/rem/divrem by constant using an n/2-bit
  /// algorithm. First, attempt to expand the division using a n/2-bit urem by
  /// constant and other arithmetic ops. The n/2-bit urem by constant will be
  /// expanded by DAGCombiner. As this is not possible for all constant divisors,
  /// this method falls back to an implementation of the magic algorithm using
  /// n/2-bit operations.
  ///
  /// \param N Node to expand
  /// \param Result A vector that will be filled with the lo and high parts of
  ///        the results. For *DIVREM, this will be the quotient parts followed
  ///        by the remainder parts.
  /// \param HiLoVT The value type to use for the Lo and Hi parts. Should be
  ///        half of VT.
  /// \param LL Low bits of the LHS of the operation. You can use this
  ///        parameter if you want to control how low bits are extracted from
  ///        the LHS.
  /// \param LH High bits of the LHS of the operation. See LL for meaning.
  /// \returns true if the node has been expanded, false if it has not.
  /// \param DAG SelectionDAG providing context.
  bool expandDIVREMByConstant(SDNode *N, SmallVectorImpl<SDValue> &Result,
                              EVT HiLoVT, SelectionDAG &DAG,
                              SDValue LL = SDValue(),
                              SDValue LH = SDValue()) const;

  /// Expand funnel shift.
  /// \param N Node to expand
  /// \returns The expansion if successful, SDValue() otherwise
  /// \param DAG SelectionDAG providing context.
  SDValue expandFunnelShift(SDNode *N, SelectionDAG &DAG) const;

  /// Expand carryless multiply.
  /// \param N Node to expand
  /// \returns The expansion if successful, SDValue() otherwise
  /// \param DAG SelectionDAG providing context.
  SDValue expandCLMUL(SDNode *N, SelectionDAG &DAG) const;

  /// Expand parallel bit extract (compress).
  /// \param N Node to expand
  /// \returns The expansion if successful, SDValue() otherwise
  /// \param DAG SelectionDAG providing context.
  SDValue expandPEXT(SDNode *N, SelectionDAG &DAG) const;

  /// Expand parallel bit deposit (expand).
  /// \param N Node to expand
  /// \returns The expansion if successful, SDValue() otherwise
  /// \param DAG SelectionDAG providing context.
  SDValue expandPDEP(SDNode *N, SelectionDAG &DAG) const;

  /// Expand rotations.
  /// \param N Node to expand
  /// \param AllowVectorOps expand vector rotate, this should only be performed
  ///        if the legalization is happening outside of LegalizeVectorOps
  /// \returns The expansion if successful, SDValue() otherwise
  /// \param DAG SelectionDAG providing context.
  SDValue expandROT(SDNode *N, bool AllowVectorOps, SelectionDAG &DAG) const;

  /// Expand shift-by-parts.
  /// \param N Node to expand
  /// \param Lo lower-output-part after conversion
  /// \param Hi upper-output-part after conversion
  /// \param DAG SelectionDAG providing context.
  void expandShiftParts(SDNode *N, SDValue &Lo, SDValue &Hi,
                        SelectionDAG &DAG) const;

  /// Expand float(f32) to SINT(i64) conversion
  /// \param N Node to expand
  /// \param Result output after conversion
  /// \returns True, if the expansion was successful, false otherwise
  /// \param DAG SelectionDAG providing context.
  bool expandFP_TO_SINT(SDNode *N, SDValue &Result, SelectionDAG &DAG) const;

  /// Expand float to UINT conversion
  /// \param N Node to expand
  /// \param Result output after conversion
  /// \param Chain output chain after conversion
  /// \returns True, if the expansion was successful, false otherwise
  /// \param DAG SelectionDAG providing context.
  bool expandFP_TO_UINT(SDNode *N, SDValue &Result, SDValue &Chain,
                        SelectionDAG &DAG) const;

  /// Expand UINT(i64) to double(f64) conversion
  /// \param N Node to expand
  /// \param Result output after conversion
  /// \param Chain output chain after conversion
  /// \returns True, if the expansion was successful, false otherwise
  /// \param DAG SelectionDAG providing context.
  bool expandUINT_TO_FP(SDNode *N, SDValue &Result, SDValue &Chain,
                        SelectionDAG &DAG) const;

  /// Expand fminnum/fmaxnum into fminnum_ieee/fmaxnum_ieee with quieted inputs.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param N SDNode being queried.
  /// \param DAG SelectionDAG providing context.
  SDValue expandFMINNUM_FMAXNUM(SDNode *N, SelectionDAG &DAG) const;

  /// Expand fminimum/fmaximum into multiple comparison with selects.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param N SDNode being queried.
  /// \param DAG SelectionDAG providing context.
  SDValue expandFMINIMUM_FMAXIMUM(SDNode *N, SelectionDAG &DAG) const;

  /// Expand fminimumnum/fmaximumnum into multiple comparison with selects.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param N SDNode being queried.
  /// \param DAG SelectionDAG providing context.
  SDValue expandFMINIMUMNUM_FMAXIMUMNUM(SDNode *N, SelectionDAG &DAG) const;

  /// Expand FP_TO_[US]INT_SAT into FP_TO_[US]INT and selects or min/max.
  /// \param N Node to expand
  /// \returns The expansion result
  /// \param DAG SelectionDAG providing context.
  SDValue expandFP_TO_INT_SAT(SDNode *N, SelectionDAG &DAG) const;

  /// Truncate Op to ResultVT. If the result is exact, leave it alone. If it is
  /// not exact, force the result to be odd.
  /// \param ResultVT The type of result.
  /// \param Op The value to round.
  /// \returns The expansion result
  /// \param DL Debug location or data layout, depending on context.
  /// \param DAG SelectionDAG providing context.
  SDValue expandRoundInexactToOdd(EVT ResultVT, SDValue Op, const SDLoc &DL,
                                  SelectionDAG &DAG) const;

  /// Expand round(fp) to fp conversion.
  ///
  /// @return The expansion result.
  ///
  /// \param Node SDNode being expanded or analyzed.
  /// \param DAG SelectionDAG providing context.
  SDValue expandFP_ROUND(SDNode *Node, SelectionDAG &DAG) const;

  /// Expand check for floating point class.
  /// \param ResultVT The type of intrinsic call result.
  /// \param Op The tested value.
  /// \param Test The test to perform.
  /// \param Flags The optimization flags.
  /// \returns The expansion result or SDValue() if it fails.
  /// \param DL Debug location or data layout, depending on context.
  /// \param DAG SelectionDAG providing context.
  SDValue expandIS_FPCLASS(EVT ResultVT, SDValue Op, FPClassTest Test,
                           SDNodeFlags Flags, const SDLoc &DL,
                           SelectionDAG &DAG) const;

  /// Expand FCANONICALIZE to FMUL with 1.
  ///
  /// @return The expansion result.
  ///
  /// \param Node SDNode being expanded or analyzed.
  /// \param DAG SelectionDAG providing context.
  SDValue expandFCANONICALIZE(SDNode *Node, SelectionDAG &DAG) const;

  /// Expand CONVERT_TO_ARBITRARY_FP using bit manipulation.
  /// \param Node Node to expand.
  /// \returns The expansion result, or SDValue() if fails.
  /// \param DAG SelectionDAG providing context.
  SDValue expandCONVERT_TO_ARBITRARY_FP(SDNode *Node, SelectionDAG &DAG) const;

  /// Expand CONVERT_FROM_ARBITRARY_FP using bit manipulation.
  /// \param Node Node to expand.
  /// \returns The expansion result, or SDValue() if fails.
  /// \param DAG SelectionDAG providing context.
  SDValue expandCONVERT_FROM_ARBITRARY_FP(SDNode *Node,
                                          SelectionDAG &DAG) const;

  /// Expand CTPOP nodes. Expands vector/scalar CTPOP nodes,
  /// vector nodes can only succeed if all operations are legal/custom.
  /// \param N Node to expand
  /// \returns The expansion result or SDValue() if it fails.
  /// \param DAG SelectionDAG providing context.
  SDValue expandCTPOP(SDNode *N, SelectionDAG &DAG) const;

  /// Expand CTLZ/CTLZ_ZERO_POISON nodes. Expands vector/scalar CTLZ nodes,
  /// vector nodes can only succeed if all operations are legal/custom.
  /// \param N Node to expand
  /// \returns The expansion result or SDValue() if it fails.
  /// \param DAG SelectionDAG providing context.
  SDValue expandCTLZ(SDNode *N, SelectionDAG &DAG) const;

  /// Expand CTLS (count leading sign bits) nodes.
  /// CTLS(x) = CTLZ(OR(SHL(XOR(x, SRA(x, BW-1)), 1), 1))
  /// \param N Node to expand
  /// \returns The expansion result or SDValue() if it fails.
  /// \param DAG SelectionDAG providing context.
  SDValue expandCTLS(SDNode *N, SelectionDAG &DAG) const;

  /// Expand CTTZ via Table Lookup.
  /// \param N Node to expand
  /// \returns The expansion result or SDValue() if it fails.
  /// \param DAG SelectionDAG providing context.
  /// \param DL Debug location or data layout, depending on context.
  /// \param VT Value type being queried or transformed.
  /// \param Op SDNode or value being queried.
  /// \param NumBitsPerElt Bits per vector element.
  SDValue CTTZTableLookup(SDNode *N, SelectionDAG &DAG, const SDLoc &DL, EVT VT,
                          SDValue Op, unsigned NumBitsPerElt) const;

  /// Expand CTTZ/CTTZ_ZERO_POISON nodes. Expands vector/scalar CTTZ nodes,
  /// vector nodes can only succeed if all operations are legal/custom.
  /// \param N Node to expand
  /// \returns The expansion result or SDValue() if it fails.
  /// \param DAG SelectionDAG providing context.
  SDValue expandCTTZ(SDNode *N, SelectionDAG &DAG) const;

  /// Expand VP_CTTZ_ELTS/VP_CTTZ_ELTS_ZERO_POISON nodes.
  /// \param N Node to expand
  /// \returns The expansion result or SDValue() if it fails.
  /// \param DAG SelectionDAG providing context.
  SDValue expandVPCTTZElements(SDNode *N, SelectionDAG &DAG) const;

  /// Expand VECTOR_MATCH nodes.
  /// \param N Node to expand
  /// \returns The expansion result or SDValue() if it fails.
  /// \param DAG SelectionDAG providing context.
  SDValue expandVectorMatch(SDNode *N, SelectionDAG &DAG) const;

  /// Expand VECTOR_FIND_LAST_ACTIVE nodes
  /// \param N Node to expand
  /// \returns The expansion result or SDValue() if it fails.
  /// \param DAG SelectionDAG providing context.
  SDValue expandVectorFindLastActive(SDNode *N, SelectionDAG &DAG) const;

  /// Expand LOOP_DEPENDENCE_MASK nodes
  /// \param N Node to expand
  /// \returns The expansion result or SDValue() if it fails.
  /// \param DAG SelectionDAG providing context.
  SDValue expandLoopDependenceMask(SDNode *N, SelectionDAG &DAG) const;

  /// Expand an ABS node into shift, xor, and add operations.
  ///
  /// Expand ABS nodes. Expands vector/scalar ABS nodes, vector nodes can only
  /// succeed if all operations are legal/custom. (ABS x) -> (XOR (ADD x, (SRA x,
  /// type_size)), (SRA x, type_size))
  ///
  /// \param N Node to expand
  /// \param IsNegative indicate negated abs
  /// \returns The expansion result or SDValue() if it fails.
  /// \param DAG SelectionDAG providing context.
  SDValue expandABS(SDNode *N, SelectionDAG &DAG,
                    bool IsNegative = false) const;

  /// Expand ABDS/ABDU nodes. Expands vector/scalar ABDS/ABDU nodes.
  /// \param N Node to expand
  /// \returns The expansion result or SDValue() if it fails.
  /// \param DAG SelectionDAG providing context.
  SDValue expandABD(SDNode *N, SelectionDAG &DAG) const;

  /// Expand vector/scalar AVGCEILS/AVGCEILU/AVGFLOORS/AVGFLOORU nodes.
  /// \param N Node to expand
  /// \returns The expansion result or SDValue() if it fails.
  /// \param DAG SelectionDAG providing context.
  SDValue expandAVG(SDNode *N, SelectionDAG &DAG) const;

  /// Expand BSWAP nodes. Expands scalar/vector BSWAP nodes with i16/i32/i64
  /// scalar types. Returns SDValue() if expand fails.
  /// \param N Node to expand
  /// \returns The expansion result or SDValue() if it fails.
  /// \param DAG SelectionDAG providing context.
  SDValue expandBSWAP(SDNode *N, SelectionDAG &DAG) const;

  /// Expand BITREVERSE nodes. Expands scalar/vector BITREVERSE nodes.
  /// Returns SDValue() if expand fails.
  /// \param N Node to expand
  /// \returns The expansion result or SDValue() if it fails.
  /// \param DAG SelectionDAG providing context.
  SDValue expandBITREVERSE(SDNode *N, SelectionDAG &DAG) const;

  /// Turn load of vector type into a load of the individual elements.
  /// \param LD load to expand
  /// \returns BUILD_VECTOR and TokenFactor nodes.
  /// \param DAG SelectionDAG providing context.
  std::pair<SDValue, SDValue> scalarizeVectorLoad(LoadSDNode *LD,
                                                  SelectionDAG &DAG) const;

  /// Turn a store of a vector type into stores of its individual elements.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param ST Store node to scalarize.
  /// \param DAG SelectionDAG providing context.
  SDValue scalarizeVectorStore(StoreSDNode *ST, SelectionDAG &DAG) const;

  /// Expands an unaligned load to 2 half-size loads for an integer, and
  /// possibly more for vectors.
  ///
  /// @return A pair of the loaded value and the token chain.
  ///
  /// \param LD Unaligned load node.
  /// \param DAG SelectionDAG providing context.
  std::pair<SDValue, SDValue> expandUnalignedLoad(LoadSDNode *LD,
                                                  SelectionDAG &DAG) const;

  /// Expands an unaligned store to 2 half-size stores for integer values, and
  /// possibly more for vectors.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param ST Unaligned store node.
  /// \param DAG SelectionDAG providing context.
  SDValue expandUnalignedStore(StoreSDNode *ST, SelectionDAG &DAG) const;

  /// Increment a memory address according to the type and mask of the stored or
  /// loaded data.
  ///
  /// Increments memory address \p Addr according to the type of the value
  ///
  /// \p DataVT that should be stored. If the data is stored in compressed
  /// form, the memory address should be incremented according to the number of
  /// the stored elements. This number is equal to the number of '1's bits
  /// in the \p Mask.
  /// \p DataVT is a vector type. \p Mask is a vector value.
  /// \p DataVT and \p Mask have the same number of vector elements.
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param Addr Address operand.
  /// \param Mask Mask operand or value.
  /// \param DL Debug location or data layout, depending on context.
  /// \param DataVT Stored data VT.
  /// \param DAG SelectionDAG providing context.
  /// \param IsCompressedMemory True when storing compressed lanes.
  SDValue IncrementMemoryAddress(SDValue Addr, SDValue Mask, const SDLoc &DL,
                                 EVT DataVT, SelectionDAG &DAG,
                                 bool IsCompressedMemory) const;

  /// Get a pointer to a vector element at the given index within a vector stored
  /// in memory.
  ///
  /// Get a pointer to vector element \p Idx located in memory for a vector of
  /// type \p VecVT starting at a base address of \p VecPtr. If \p Idx is out of
  /// bounds the returned pointer is unspecified, but will be within the vector
  /// bounds. \p PtrArithFlags can be used to mark that arithmetic within the
  /// vector in memory is known to not wrap or to be inbounds.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param DAG SelectionDAG providing context.
  /// \param VecPtr Base pointer of the vector in memory.
  /// \param VecVT Vector value type.
  /// \param Index Lane or subvector index.
  /// \param PtrArithFlags GEP no-wrap flags.
  SDValue getVectorElementPointer(
      SelectionDAG &DAG, SDValue VecPtr, EVT VecVT, SDValue Index,
      const SDNodeFlags PtrArithFlags = SDNodeFlags()) const;

  /// Get an in-bounds pointer to a vector element for a vector known to fit in
  /// memory.
  ///
  /// Get a pointer to vector element \p Idx located in memory for a vector of
  /// type \p VecVT starting at a base address of \p VecPtr. If \p Idx is out of
  /// bounds the returned pointer is unspecified, but will be within the vector
  /// bounds. \p VecPtr is guaranteed to point to the beginning of a memory
  /// location large enough for the vector.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param DAG SelectionDAG providing context.
  /// \param VecPtr Base pointer of the vector in memory.
  /// \param VecVT Vector value type.
  /// \param Index Lane or subvector index.
  SDValue getInboundsVectorElementPointer(SelectionDAG &DAG, SDValue VecPtr,
                                          EVT VecVT, SDValue Index) const {
    return getVectorElementPointer(DAG, VecPtr, VecVT, Index,
                                   SDNodeFlags::NoUnsignedWrap |
                                       SDNodeFlags::InBounds);
  }

  /// Get a pointer to a sub-vector at the given index within a vector stored in
  /// memory.
  ///
  /// Get a pointer to a sub-vector of type \p SubVecVT at index \p Idx located
  /// in memory for a vector of type \p VecVT starting at a base address of
  ///
  /// \p VecPtr. If \p Idx plus the size of \p SubVecVT is out of bounds the
  /// returned pointer is unspecified, but the value returned will be such that
  /// the entire subvector would be within the vector bounds. \p PtrArithFlags
  /// can be used to mark that arithmetic within the vector in memory is known
  /// to not wrap or to be inbounds.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param DAG SelectionDAG providing context.
  /// \param VecPtr Base pointer of the vector in memory.
  /// \param VecVT Vector value type.
  /// \param SubVecVT Type of the addressed subvector.
  /// \param Index Lane or subvector index.
  /// \param PtrArithFlags GEP no-wrap flags.
  SDValue
  getVectorSubVecPointer(SelectionDAG &DAG, SDValue VecPtr, EVT VecVT,
                         EVT SubVecVT, SDValue Index,
                         const SDNodeFlags PtrArithFlags = SDNodeFlags()) const;

  /// Method for building the DAG expansion of ISD::[US][MIN|MAX]. This
  /// method accepts integers as its arguments.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param Node SDNode being expanded or analyzed.
  /// \param DAG SelectionDAG providing context.
  SDValue expandIntMINMAX(SDNode *Node, SelectionDAG &DAG) const;

  /// Method for building the DAG expansion of ISD::[US][ADD|SUB]SAT. This
  /// method accepts integers as its arguments.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param Node SDNode being expanded or analyzed.
  /// \param DAG SelectionDAG providing context.
  SDValue expandAddSubSat(SDNode *Node, SelectionDAG &DAG) const;

  /// Method for building the DAG expansion of ISD::[US]CMP. This
  /// method accepts integers as its arguments
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param Node SDNode being expanded or analyzed.
  /// \param DAG SelectionDAG providing context.
  SDValue expandCMP(SDNode *Node, SelectionDAG &DAG) const;

  /// Method for building the DAG expansion of ISD::[US]SHLSAT. This
  /// method accepts integers as its arguments.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param Node SDNode being expanded or analyzed.
  /// \param DAG SelectionDAG providing context.
  SDValue expandShlSat(SDNode *Node, SelectionDAG &DAG) const;

  /// Method for building the DAG expansion of ISD::[U|S]MULFIX[SAT]. This
  /// method accepts integers as its arguments.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param Node SDNode being expanded or analyzed.
  /// \param DAG SelectionDAG providing context.
  SDValue expandFixedPointMul(SDNode *Node, SelectionDAG &DAG) const;

  /// Expand a fixed-point division or saturating division node.
  ///
  /// Method for building the DAG expansion of ISD::[US]DIVFIX[SAT]. This method
  /// accepts integers as its arguments. Note: This method may fail if the
  /// division could not be performed within the type. Clients must retry with a
  /// wider type if this happens.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param Opcode ISD or target opcode.
  /// \param dl Debug location for newly created nodes.
  /// \param LHS Left-hand operand.
  /// \param RHS Right-hand operand.
  /// \param Scale Fixed-point scale or addressing scale.
  /// \param DAG SelectionDAG providing context.
  SDValue expandFixedPointDiv(unsigned Opcode, const SDLoc &dl,
                              SDValue LHS, SDValue RHS,
                              unsigned Scale, SelectionDAG &DAG) const;

  /// Method for building the DAG expansion of ISD::U(ADD|SUB)O. Expansion
  /// always suceeds and populates the Result and Overflow arguments.
  /// \param Node SDNode being expanded or analyzed.
  /// \param Result Filled with the expanded result.
  /// \param Overflow Filled with the overflow flag.
  /// \param DAG SelectionDAG providing context.
  void expandUADDSUBO(SDNode *Node, SDValue &Result, SDValue &Overflow,
                      SelectionDAG &DAG) const;

  /// Method for building the DAG expansion of ISD::S(ADD|SUB)O. Expansion
  /// always suceeds and populates the Result and Overflow arguments.
  /// \param Node SDNode being expanded or analyzed.
  /// \param Result Filled with the expanded result.
  /// \param Overflow Filled with the overflow flag.
  /// \param DAG SelectionDAG providing context.
  void expandSADDSUBO(SDNode *Node, SDValue &Result, SDValue &Overflow,
                      SelectionDAG &DAG) const;

  /// Method for building the DAG expansion of ISD::[US]MULO. Returns whether
  /// expansion was successful and populates the Result and Overflow arguments.
  ///
  /// @return True if method for building the DAG expansion of ISD::[US]MULO. Returns whether expansion was successful and populates the Result and Overflow arguments.
  ///
  /// \param Node SDNode being expanded or analyzed.
  /// \param Result Filled with the expanded result.
  /// \param Overflow Filled with the overflow flag.
  /// \param DAG SelectionDAG providing context.
  bool expandMULO(SDNode *Node, SDValue &Result, SDValue &Overflow,
                  SelectionDAG &DAG) const;

  /// Expand a wide multiply by splitting the operands into pieces multiplied and
  /// added without MULH.
  ///
  /// Calculate the product twice the width of LHS and RHS. If HiLHS/HiRHS are
  /// non-null they will be included in the multiplication. The expansion works
  /// by splitting the 2 inputs into 4 pieces that we can multiply and add
  /// together without neding MULH or MUL_LOHI.
  ///
  /// \param DAG SelectionDAG providing context.
  /// \param dl Debug location for newly created nodes.
  /// \param Signed True for signed multiply.
  /// \param Lo Low half of a wide result.
  /// \param Hi High half of a wide result.
  /// \param LHS Left-hand operand.
  /// \param RHS Right-hand operand.
  /// \param HiLHS Optional high bits of LHS.
  /// \param HiRHS Optional high bits of RHS.
  void forceExpandMultiply(SelectionDAG &DAG, const SDLoc &dl, bool Signed,
                           SDValue &Lo, SDValue &Hi, SDValue LHS, SDValue RHS,
                           SDValue HiLHS = SDValue(),
                           SDValue HiRHS = SDValue()) const;

  /// Compute the full double-width product of LHS and RHS via a libcall or
  /// brute-force expansion.
  ///
  /// Calculate full product of LHS and RHS either via a libcall or through brute
  /// force expansion of the multiplication. The expansion works by splitting the
  /// 2 inputs into 4 pieces that we can multiply and add together without
  /// needing MULH or MUL_LOHI.
  ///
  /// \param DAG SelectionDAG providing context.
  /// \param dl Debug location for newly created nodes.
  /// \param Signed True for signed multiply.
  /// \param LHS Left-hand operand.
  /// \param RHS Right-hand operand.
  /// \param Lo Low half of a wide result.
  /// \param Hi High half of a wide result.
  void forceExpandWideMUL(SelectionDAG &DAG, const SDLoc &dl, bool Signed,
                          const SDValue LHS, const SDValue RHS, SDValue &Lo,
                          SDValue &Hi) const;

  /// Expand a VECREDUCE_* into an explicit calculation. If Count is specified,
  /// only the first Count elements of the vector are used.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param Node SDNode being expanded or analyzed.
  /// \param DAG SelectionDAG providing context.
  SDValue expandVecReduce(SDNode *Node, SelectionDAG &DAG) const;

  /// Expand a VECREDUCE_SEQ_* into an explicit ordered calculation.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param Node SDNode being expanded or analyzed.
  /// \param DAG SelectionDAG providing context.
  SDValue expandVecReduceSeq(SDNode *Node, SelectionDAG &DAG) const;

  /// Expand an SREM or UREM using SDIV/UDIV or SDIVREM/UDIVREM, if legal.
  /// Returns true if the expansion was successful.
  ///
  /// @return True if expand an SREM or UREM using SDIV/UDIV or SDIVREM/UDIVREM, if legal. Returns true if the expansion was successful.
  ///
  /// \param Node SDNode being expanded or analyzed.
  /// \param Result Filled with the expanded result.
  /// \param DAG SelectionDAG providing context.
  bool expandREM(SDNode *Node, SDValue &Result, SelectionDAG &DAG) const;

  /// Method for building the DAG expansion of ISD::VECTOR_SPLICE. This
  /// method accepts vectors as its arguments.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param Node SDNode being expanded or analyzed.
  /// \param DAG SelectionDAG providing context.
  SDValue expandVectorSplice(SDNode *Node, SelectionDAG &DAG) const;

  /// Expand a vector VECTOR_COMPRESS into a sequence of extract element, store
  /// temporarily, advance store position, before re-loading the final vector.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param Node SDNode being expanded or analyzed.
  /// \param DAG SelectionDAG providing context.
  SDValue expandVECTOR_COMPRESS(SDNode *Node, SelectionDAG &DAG) const;

  /// Expand a CTTZ_ELTS or CTTZ_ELTS_ZERO_POISON by calculating (VL - i) for
  /// each active lane (i), getting the maximum and subtracting it from VL.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param Node SDNode being expanded or analyzed.
  /// \param DAG SelectionDAG providing context.
  SDValue expandCttzElts(SDNode *Node, SelectionDAG &DAG) const;

  /// Expands PARTIAL_REDUCE_S/UMLA nodes to a series of simpler operations,
  /// consisting of zext/sext, extract_subvector, mul and add operations.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param Node SDNode being expanded or analyzed.
  /// \param DAG SelectionDAG providing context.
  SDValue expandPartialReduceMLA(SDNode *Node, SelectionDAG &DAG) const;

  /// Expand a node with multiple results into a call to an FP or vector libcall.
  ///
  /// Expands a node with multiple results to an FP or vector libcall. The
  /// libcall is expected to take all the operands of the \p Node followed by
  /// output pointers for each of the results. \p CallRetResNo can be optionally
  /// set to indicate that one of the results comes from the libcall's return
  /// value.
  ///
  /// @return True if expand a node with multiple results into a call to an FP or vector libcall.
  ///
  /// \param DAG SelectionDAG providing context.
  /// \param LC Libcall kind to expand.
  /// \param Node SDNode being expanded or analyzed.
  /// \param Results Replacement results.
  /// \param CallRetResNo Result index returned in registers.
  bool expandMultipleResultFPLibCall(
      SelectionDAG &DAG, RTLIB::Libcall LC, SDNode *Node,
      SmallVectorImpl<SDValue> &Results,
      std::optional<unsigned> CallRetResNo = {}) const;

  /// Legalize a SETCC with given LHS and RHS and condition code CC on the
  /// current target.
  ///
  /// If the SETCC has been legalized using AND / OR, then the legalized node
  /// will be stored in LHS. RHS and CC will be set to SDValue(). NeedInvert
  /// will be set to false.
  ///
  /// If the SETCC has been legalized by using getSetCCSwappedOperands(), then
  /// the values of LHS and RHS will be swapped, CC will be set to the new
  /// condition, and NeedInvert will be set to false.
  ///
  /// If the SETCC has been legalized using the inverse condcode, then LHS and
  /// RHS will be unchanged, CC will set to the inverted condcode, and
  /// NeedInvert will be set to true. The caller must invert the result of the
  /// SETCC with SelectionDAG::getLogicalNOT() or take equivalent action to swap
  /// the effect of a true/false result.
  ///
  /// \returns true if the SETCC has been legalized, false if it hasn't.
  /// \param DAG SelectionDAG providing context.
  /// \param VT Value type being queried or transformed.
  /// \param LHS Left-hand operand.
  /// \param RHS Right-hand operand.
  /// \param CC Compare constant or calling convention.
  /// \param NeedInvert Set when CC must be inverted.
  /// \param dl Debug location for newly created nodes.
  /// \param Chain Incoming token chain.
  /// \param IsSignaling True for signaling FP compares.
  bool LegalizeSetCCCondCode(SelectionDAG &DAG, EVT VT, SDValue &LHS,
                             SDValue &RHS, SDValue &CC, bool &NeedInvert,
                             const SDLoc &dl, SDValue &Chain,
                             bool IsSignaling = false) const;

  //===--------------------------------------------------------------------===//
  // Instruction Emitting Hooks
  //

  /// Expand a pseudo-instruction marked usesCustomInserter into a sequence of
  /// instructions.
  ///
  /// This method should be implemented by targets that mark instructions with
  /// the 'usesCustomInserter' flag. These instructions are special in various
  /// ways, which require special support to insert. The specified MachineInstr
  /// is created but not inserted into any basic blocks, and this method is
  /// called to expand it into a sequence of instructions, potentially also
  /// creating new basic blocks and control flow. As long as the returned basic
  /// block is different (i.e., we created a new one), the custom inserter is
  /// free to modify the rest of \p MBB.
  ///
  /// @return A pointer to the requested object, or null if unavailable.
  ///
  /// \param MI Machine instruction being queried or modified.
  /// \param MBB Machine basic block.
  virtual MachineBasicBlock *
  EmitInstrWithCustomInserter(MachineInstr &MI, MachineBasicBlock *MBB) const;

  /// Adjust an instruction after instruction selection using target-specific
  /// hooks.
  ///
  /// This method should be implemented by targets that mark instructions with
  /// the 'hasPostISelHook' flag. These instructions must be adjusted after
  /// instruction selection by target hooks. e.g. To fill in optional defs for
  /// ARM 's' setting instructions.
  ///
  /// \param MI Machine instruction being queried or modified.
  /// \param Node SDNode being expanded or analyzed.
  virtual void AdjustInstrPostInstrSelection(MachineInstr &MI,
                                             SDNode *Node) const;

  /// If this function returns true, SelectionDAGBuilder emits a
  /// LOAD_STACK_GUARD node when it is lowering Intrinsic::stackprotector.
  ///
  /// @return True if if this function returns true, SelectionDAGBuilder emits a LOAD_STACK_GUARD node when it is lowering Intrinsic::stackprotector.
  ///
  /// \param M Module being modified.
  virtual bool useLoadStackGuardNode(const Module &M) const { return false; }

  /// Mix the frame pointer into the stack guard value for stack protector
  /// checks.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param DAG SelectionDAG providing context.
  /// \param Val Minimum number of bit-test compares.
  /// \param DL Debug location or data layout, depending on context.
  virtual SDValue emitStackGuardMixFP(SelectionDAG &DAG, SDValue Val,
                                      const SDLoc &DL) const {
    llvm_unreachable("not implemented for this target");
  }

  /// Lower TLS global address SDNode for target independent emulated TLS model.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param GA Global address node.
  /// \param DAG SelectionDAG providing context.
  virtual SDValue LowerToTLSEmulatedModel(const GlobalAddressSDNode *GA,
                                          SelectionDAG &DAG) const;

  /// Expands target specific indirect branch for the case of JumpTable
  /// expansion.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param dl Debug location for newly created nodes.
  /// \param Value Switch/jump discriminant.
  /// \param Addr Address operand.
  /// \param JTI Jump table index.
  /// \param DAG SelectionDAG providing context.
  virtual SDValue expandIndirectJTBranch(const SDLoc &dl, SDValue Value,
                                         SDValue Addr, int JTI,
                                         SelectionDAG &DAG) const;

  /// Lower an equality comparison against zero into a ctlz/srl pair when
  /// profitable.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param Op SDNode or value being queried.
  /// \param DAG SelectionDAG providing context.
  SDValue lowerCmpEqZeroToCtlzSrl(SDValue Op, SelectionDAG &DAG) const;

  /// Return true if testing `X & Y` against zero is preferable to testing it
  /// against Y.
  ///
  /// @return True if testing `X & Y` against zero is preferable to testing it
  /// against Y.
  ///
  /// \param Cond Compare condition code.
  /// \param VT Value type of the compared operands.
  virtual bool isXAndYEqZeroPreferableToXAndYEqY(ISD::CondCode Cond,
                                                 EVT VT) const {
    return true;
  }

  /// Expand a vector n-ary operation by splitting it into smaller-length
  /// operations and joining the results.
  ///
  /// @return The lowered or expanded SelectionDAG value, or an empty SDValue on failure.
  ///
  /// \param Node SDNode being expanded or analyzed.
  /// \param DAG SelectionDAG providing context.
  SDValue expandVectorNaryOpBySplitting(SDNode *Node, SelectionDAG &DAG) const;

  /// Replace an extraction of a load with a narrowed load.
  ///
  /// \param ResultVT type of the result extraction.
  /// \param InVecVT type of the input vector to with bitcasts resolved.
  /// \param EltNo index of the vector element to load.
  /// \param OriginalLoad vector load that to be replaced.
  /// \returns \p ResultVT Load on success SDValue() on failure.
  /// \param DL Debug location or data layout, depending on context.
  /// \param DAG SelectionDAG providing context.
  SDValue scalarizeExtractedVectorLoad(EVT ResultVT, const SDLoc &DL,
                                       EVT InVecVT, SDValue EltNo,
                                       LoadSDNode *OriginalLoad,
                                       SelectionDAG &DAG) const;

protected:
  /// Record the call-site type identifier used for indirect call type checking.
  ///
  /// \param CB Call base instruction being inspected.
  /// \param MF Machine function being lowered.
  /// \param CSInfo Call-site info being annotated.
  void setTypeIdForCallsiteInfo(const CallBase *CB, MachineFunction &MF,
                                MachineFunction::CallSiteInfo &CSInfo) const;

private:
  SDValue foldSetCCWithAnd(EVT VT, SDValue N0, SDValue N1, ISD::CondCode Cond,
                           const SDLoc &DL, DAGCombinerInfo &DCI) const;
  SDValue foldSetCCWithOr(EVT VT, SDValue N0, SDValue N1, ISD::CondCode Cond,
                          const SDLoc &DL, DAGCombinerInfo &DCI) const;
  SDValue foldSetCCWithBinOp(EVT VT, SDValue N0, SDValue N1, ISD::CondCode Cond,
                             const SDLoc &DL, DAGCombinerInfo &DCI) const;

  SDValue optimizeSetCCOfSignedTruncationCheck(EVT SCCVT, SDValue N0,
                                               SDValue N1, ISD::CondCode Cond,
                                               DAGCombinerInfo &DCI,
                                               const SDLoc &DL) const;

  // (X & (C l>>/<< Y)) ==/!= 0  -->  ((X <</l>> Y) & C) ==/!= 0
  SDValue optimizeSetCCByHoistingAndByConstFromLogicalShift(
      EVT SCCVT, SDValue N0, SDValue N1C, ISD::CondCode Cond,
      DAGCombinerInfo &DCI, const SDLoc &DL) const;

  SDValue prepareUREMEqFold(EVT SETCCVT, SDValue REMNode,
                            SDValue CompTargetNode, ISD::CondCode Cond,
                            DAGCombinerInfo &DCI, const SDLoc &DL,
                            SmallVectorImpl<SDNode *> &Created) const;
  SDValue buildUREMEqFold(EVT SETCCVT, SDValue REMNode, SDValue CompTargetNode,
                          ISD::CondCode Cond, DAGCombinerInfo &DCI,
                          const SDLoc &DL) const;

  SDValue prepareSREMEqFold(EVT SETCCVT, SDValue REMNode,
                            SDValue CompTargetNode, ISD::CondCode Cond,
                            DAGCombinerInfo &DCI, const SDLoc &DL,
                            SmallVectorImpl<SDNode *> &Created) const;
  SDValue buildSREMEqFold(EVT SETCCVT, SDValue REMNode, SDValue CompTargetNode,
                          ISD::CondCode Cond, DAGCombinerInfo &DCI,
                          const SDLoc &DL) const;

  bool expandUDIVREMByConstantViaUREMDecomposition(
      SDNode *N, APInt Divisor, SmallVectorImpl<SDValue> &Result, EVT HiLoVT,
      SelectionDAG &DAG, SDValue LL, SDValue LH) const;

  bool expandUDIVREMByConstantViaUMulHiMagic(SDNode *N, const APInt &Divisor,
                                             SmallVectorImpl<SDValue> &Result,
                                             EVT HiLoVT, SelectionDAG &DAG,
                                             SDValue LL, SDValue LH) const;
};

/// Compute the return value EVTs, flags, and offsets for a given LLVM IR return
/// type.
///
/// Given an LLVM IR type and return type attributes, compute the return value
/// EVTs and flags, and optionally also the offsets, if the return value is being
/// lowered to memory.
///
/// \param CC Calling convention of the function.
/// \param ReturnType LLVM IR return type.
/// \param attr Return-type attribute list.
/// \param Outs Filled with return-value output descriptors.
/// \param TLI TargetLowering providing ABI rules.
/// \param DL Data layout of the module.
LLVM_ABI void GetReturnInfo(CallingConv::ID CC, Type *ReturnType,
                            AttributeList attr,
                            SmallVectorImpl<ISD::OutputArg> &Outs,
                            const TargetLowering &TLI, const DataLayout &DL);

} // end namespace llvm

#endif // LLVM_CODEGEN_TARGETLOWERING_H
