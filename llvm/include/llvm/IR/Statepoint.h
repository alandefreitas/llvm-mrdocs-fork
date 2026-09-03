//===- llvm/IR/Statepoint.h - gc.statepoint utilities -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains utility functions and a wrapper class analogous to
// CallBase for accessing the fields of gc.statepoint, gc.relocate,
// gc.result intrinsics; and some general utilities helpful when dealing with
// gc.statepoint.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_STATEPOINT_H
#define LLVM_IR_STATEPOINT_H

#include "llvm/ADT/iterator_range.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/MathExtras.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace llvm {

/// The statepoint intrinsic accepts a set of flags as its third argument.
/// Valid values come out of this set.
enum class StatepointFlags {
  None = 0, ///< No flags set.
  GCTransition = 1, ///< Indicates that this statepoint is a transition from
                    ///< GC-aware code to code that is not GC-aware.
  /// Mark the deopt arguments associated with the statepoint as only being
  /// "live-in". By default, deopt arguments are "live-through".  "live-through"
  /// requires that they the value be live on entry, on exit, and at any point
  /// during the call.  "live-in" only requires the value be available at the
  /// start of the call.  In particular, "live-in" values can be placed in
  /// unused argument registers or other non-callee saved registers.
  DeoptLiveIn = 2,

  MaskAll = 3 ///< A bitmask that includes all valid flags.
};

// These two are defined in IntrinsicInst since they're part of the
// IntrinsicInst class hierarchy.
class GCRelocateInst;

/// Represents a gc.statepoint intrinsic call.  This extends directly from
/// CallBase as the IntrinsicInst only supports calls and gc.statepoint is
/// invokable.
class GCStatepointInst : public CallBase {
public:
  /// Default construction is deleted; cast existing calls instead.
  GCStatepointInst() = delete;
  /// Copy construction is deleted; GCStatepointInst is non-copyable.
  /// \param Other Unused; copy construction is deleted.
  GCStatepointInst(const GCStatepointInst &Other) = delete;
  /// Copy assignment is deleted; GCStatepointInst is non-copyable.
  /// \param Other Unused; copy assignment is deleted.
  GCStatepointInst &operator=(const GCStatepointInst &Other) = delete;

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The call to test.
  /// \return True if \p I is a gc.statepoint intrinsic call.
  static bool classof(const CallBase *I) {
    if (const Function *CF = I->getCalledFunction())
      return CF->getIntrinsicID() == Intrinsic::experimental_gc_statepoint;
    return false;
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// \return True if \p V is a gc.statepoint intrinsic call.
  static bool classof(const Value *V) {
    return isa<CallBase>(V) && classof(cast<CallBase>(V));
  }

  /// Indices of fixed operands in a gc.statepoint call.
  enum {
    IDPos = 0,             ///< Statepoint ID operand.
    NumPatchBytesPos = 1,  ///< Number of patchable bytes operand.
    CalledFunctionPos = 2, ///< Callee (or callee pointer) operand.
    NumCallArgsPos = 3,    ///< Number of call arguments operand.
    FlagsPos = 4,          ///< Statepoint flags operand.
    CallArgsBeginPos = 5,  ///< First operand of the underlying call arguments.
  };

  /// Return the ID associated with this statepoint.
  /// \return The statepoint ID.
  uint64_t getID() const {
    return cast<ConstantInt>(getArgOperand(IDPos))->getZExtValue();
  }

  /// Return the number of patchable bytes associated with this statepoint.
  /// \return The number of patchable bytes.
  uint32_t getNumPatchBytes() const {
    const Value *NumPatchBytesVal = getArgOperand(NumPatchBytesPos);
    uint64_t NumPatchBytes =
      cast<ConstantInt>(NumPatchBytesVal)->getZExtValue();
    assert(isInt<32>(NumPatchBytes) && "should fit in 32 bits!");
    return NumPatchBytes;
  }

  /// Number of arguments to be passed to the actual callee.
  /// \return The number of call arguments.
  int getNumCallArgs() const {
    return cast<ConstantInt>(getArgOperand(NumCallArgsPos))->getZExtValue();
  }

  /// Return the flags associated with this statepoint.
  /// \return The statepoint flags bitmask.
  uint64_t getFlags() const {
    return cast<ConstantInt>(getArgOperand(FlagsPos))->getZExtValue();
  }

  /// Return the value actually being called or invoked.
  /// \return The callee operand of the underlying call.
  Value *getActualCalledOperand() const {
    return getArgOperand(CalledFunctionPos);
  }

  /// Returns the function called if this is a wrapping a direct call, and null
  /// otherwise.
  /// \return The called function, or null if the callee is indirect.
  Function *getActualCalledFunction() const {
    return dyn_cast_or_null<Function>(getActualCalledOperand());
  }

  /// Return the type of the value returned by the call underlying the
  /// statepoint.
  /// \return The return type of the underlying call.
  Type *getActualReturnType() const {
    auto *FT = cast<FunctionType>(getParamElementType(CalledFunctionPos));
    return FT->getReturnType();
  }


  /// Return the number of arguments to the underlying call.
  /// \return The number of arguments to the underlying call.
  size_t actual_arg_size() const { return getNumCallArgs(); }
  /// Return an iterator to the begining of the arguments to the underlying call
  /// \return An iterator to the first argument of the underlying call.
  const_op_iterator actual_arg_begin() const {
    assert(CallArgsBeginPos <= (int)arg_size());
    return arg_begin() + CallArgsBeginPos;
  }
  /// Return an end iterator of the arguments to the underlying call
  /// \return An end iterator past the last argument of the underlying call.
  const_op_iterator actual_arg_end() const {
    auto I = actual_arg_begin() + actual_arg_size();
    assert((arg_end() - I) == 2);
    return I;
  }
  /// range adapter for actual call arguments
  /// \return A range over the arguments of the underlying call.
  iterator_range<const_op_iterator> actual_args() const {
    return make_range(actual_arg_begin(), actual_arg_end());
  }

  /// Return an iterator to the beginning of the GC transition arguments.
  /// \return An iterator to the first GC transition argument, or \c arg_end()
  /// if none.
  const_op_iterator gc_transition_args_begin() const {
    if (auto Opt = getOperandBundle(LLVMContext::OB_gc_transition))
      return Opt->Inputs.begin();
    return arg_end();
  }
  /// Return an end iterator for the GC transition arguments.
  /// \return An end iterator for the GC transition arguments, or \c arg_end()
  /// if none.
  const_op_iterator gc_transition_args_end() const {
    if (auto Opt = getOperandBundle(LLVMContext::OB_gc_transition))
      return Opt->Inputs.end();
    return arg_end();
  }

  /// range adapter for GC transition arguments
  /// \return A range over the GC transition arguments.
  iterator_range<const_op_iterator> gc_transition_args() const {
    return make_range(gc_transition_args_begin(), gc_transition_args_end());
  }

  /// Return an iterator to the beginning of the deopt operands.
  /// \return An iterator to the first deopt operand, or \c arg_end() if none.
  const_op_iterator deopt_begin() const {
    if (auto Opt = getOperandBundle(LLVMContext::OB_deopt))
      return Opt->Inputs.begin();
    return arg_end();
  }
  /// Return an end iterator for the deopt operands.
  /// \return An end iterator for the deopt operands, or \c arg_end() if none.
  const_op_iterator deopt_end() const {
    if (auto Opt = getOperandBundle(LLVMContext::OB_deopt))
      return Opt->Inputs.end();
    return arg_end();
  }

  /// range adapter for vm state arguments
  /// \return A range over the deopt operands.
  iterator_range<const_op_iterator> deopt_operands() const {
    return make_range(deopt_begin(), deopt_end());
  }

  /// Returns an iterator to the begining of the argument range describing gc
  /// live values for the statepoint.
  /// \return An iterator to the first gc live value, or \c arg_end() if none.
  const_op_iterator gc_live_begin() const {
    if (auto Opt = getOperandBundle(LLVMContext::OB_gc_live))
      return Opt->Inputs.begin();
    return arg_end();
  }

  /// Return an end iterator for the gc live range
  /// \return An end iterator for the gc live values, or \c arg_end() if none.
  const_op_iterator gc_live_end() const {
    if (auto Opt = getOperandBundle(LLVMContext::OB_gc_live))
      return Opt->Inputs.end();
    return arg_end();
  }

  /// range adapter for gc live arguments
  /// \return A range over the gc live values.
  iterator_range<const_op_iterator> gc_live() const {
    return make_range(gc_live_begin(), gc_live_end());
  }


  /// Get list of all gc relocates linked to this statepoint.
  ///
  /// May contain several relocations for the same base/derived pair.
  /// For example this could happen due to relocations on unwinding
  /// path of invoke.
  /// \return All gc.relocate instructions linked to this statepoint.
  inline std::vector<const GCRelocateInst *> getGCRelocates() const;
};

std::vector<const GCRelocateInst *> GCStatepointInst::getGCRelocates() const {
  std::vector<const GCRelocateInst *> Result;

  // Search for relocated pointers.  Note that working backwards from the
  // gc_relocates ensures that we only get pairs which are actually relocated
  // and used after the statepoint.
  for (const User *U : users())
    if (auto *Relocate = dyn_cast<GCRelocateInst>(U))
      Result.push_back(Relocate);

  auto *StatepointInvoke = dyn_cast<InvokeInst>(this);
  if (!StatepointInvoke)
    return Result;

  // We need to scan thorough exceptional relocations if it is invoke statepoint
  LandingPadInst *LandingPad = StatepointInvoke->getLandingPadInst();

  // Search for gc relocates that are attached to this landingpad.
  for (const User *LandingPadUser : LandingPad->users()) {
    if (auto *Relocate = dyn_cast<GCRelocateInst>(LandingPadUser))
      Result.push_back(Relocate);
  }
  return Result;
}

/// Directives describing properties of a future gc.statepoint wrapper.
///
/// Call sites that get wrapped by a gc.statepoint (currently only in
/// RewriteStatepointsForGC and potentially in other passes in the future) can
/// have attributes that describe properties of gc.statepoint call they will be
/// eventually be wrapped in.  This struct is used represent such directives.
struct StatepointDirectives {
  /// Optional number of patchable bytes for the statepoint.
  std::optional<uint32_t> NumPatchBytes;
  /// Optional statepoint ID to use when wrapping the call site.
  std::optional<uint64_t> StatepointID;

  /// Default statepoint ID used when none is specified.
  static const uint64_t DefaultStatepointID = 0xABCDEF00;
  /// Statepoint ID used for deopt-bundle statepoints.
  static const uint64_t DeoptBundleStatepointID = 0xABCDEF0F;
};

/// Parse out statepoint directives from the function attributes present in \p
/// AS.
/// \param AS The attribute list to parse.
/// \return The statepoint directives encoded in \p AS.
LLVM_ABI StatepointDirectives
parseStatepointDirectivesFromAttrs(AttributeList AS);

/// Return \c true if the \p Attr is an attribute that is a statepoint
/// directive.
/// \param Attr The attribute to test.
/// \return True if \p Attr is a statepoint directive attribute.
LLVM_ABI bool isStatepointDirectiveAttr(Attribute Attr);

} // end namespace llvm

#endif // LLVM_IR_STATEPOINT_H
