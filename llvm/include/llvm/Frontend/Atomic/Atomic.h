//===--- Atomic.h - Codegen of atomic operations ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FRONTEND_ATOMIC_ATOMIC_H
#define LLVM_FRONTEND_ATOMIC_ATOMIC_H

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

/// Helper for emitting IR for atomic loads, stores, and compare-exchange.
///
/// Tracks the atomic value type, sizes, alignments, and whether a libcall is
/// required, and provides helpers used by frontend atomic codegen.
class AtomicInfo {
protected:
  /// IR builder used to emit atomic instructions and libcalls.
  IRBuilderBase *Builder;
  /// LLVM type of the atomic value being accessed.
  Type *Ty;
  /// Size in bits of the atomic object, including any padding.
  uint64_t AtomicSizeInBits;
  /// Size in bits of the underlying value type, excluding padding.
  uint64_t ValueSizeInBits;
  /// Required alignment of the atomic object.
  Align AtomicAlign;
  /// Preferred or natural alignment of the value type.
  Align ValueAlign;
  /// Whether atomic operations must be emitted via runtime libcalls.
  bool UseLibcall;
  /// Insertion point for allocating temporaries used by atomic libcalls.
  IRBuilderBase::InsertPoint AllocaIP;

public:
  /// Construct atomic codegen state for a value of type \p Ty.
  ///
  /// \param Builder IR builder used to emit instructions.
  /// \param Ty LLVM type of the atomic value.
  /// \param AtomicSizeInBits Size in bits of the atomic object.
  /// \param ValueSizeInBits Size in bits of the value type.
  /// \param AtomicAlign Required alignment of the atomic object.
  /// \param ValueAlign Alignment of the value type.
  /// \param UseLibcall Whether to lower atomics via runtime libcalls.
  /// \param AllocaIP Insertion point for temporary allocations.
  AtomicInfo(IRBuilderBase *Builder, Type *Ty, uint64_t AtomicSizeInBits,
             uint64_t ValueSizeInBits, Align AtomicAlign, Align ValueAlign,
             bool UseLibcall, IRBuilderBase::InsertPoint AllocaIP)
      : Builder(Builder), Ty(Ty), AtomicSizeInBits(AtomicSizeInBits),
        ValueSizeInBits(ValueSizeInBits), AtomicAlign(AtomicAlign),
        ValueAlign(ValueAlign), UseLibcall(UseLibcall), AllocaIP(AllocaIP) {}

  /// Destroy the atomic info; subclasses may override for cleanup.
  virtual ~AtomicInfo() = default;

  /// Return the required alignment of the atomic object.
  ///
  /// \return The required alignment of the atomic object.
  Align getAtomicAlignment() const { return AtomicAlign; }
  /// Return the size in bits of the atomic object.
  ///
  /// \return Size in bits of the atomic object.
  uint64_t getAtomicSizeInBits() const { return AtomicSizeInBits; }
  /// Return the size in bits of the underlying value type.
  ///
  /// \return Size in bits of the underlying value type.
  uint64_t getValueSizeInBits() const { return ValueSizeInBits; }
  /// Return whether atomic operations should use runtime libcalls.
  ///
  /// \return True if atomic operations should use runtime libcalls.
  bool shouldUseLibcall() const { return UseLibcall; }
  /// Return the LLVM type of the atomic value.
  ///
  /// \return The LLVM type of the atomic value.
  Type *getAtomicTy() const { return Ty; }

  /// Return a pointer to the atomic object in memory.
  ///
  /// \return Pointer to the atomic object.
  virtual Value *getAtomicPointer() const = 0;
  /// Attach TBAA metadata to an atomic memory instruction.
  ///
  /// \param I Instruction to decorate with type-based alias analysis info.
  virtual void decorateWithTBAA(Instruction *I) = 0;
  /// Create an alloca of type \p Ty with the given name at \c AllocaIP.
  ///
  /// \param Ty Element type of the alloca.
  /// \param Name Name to assign to the alloca instruction.
  /// \return The newly created alloca instruction.
  virtual AllocaInst *CreateAlloca(Type *Ty, const Twine &Name) const = 0;

  /// Return whether the atomic size is larger than the value type.
  ///
  /// Note that the absence of padding does not mean that atomic objects are
  /// completely interchangeable with non-atomic objects: we might have
  /// promoted the alignment of a type without making it bigger.
  ///
  /// \return True if the atomic object is larger than the value type.
  bool hasPadding() const { return (ValueSizeInBits != AtomicSizeInBits); }

  /// Return the LLVM context associated with the IR builder.
  ///
  /// \return The LLVM context from the IR builder.
  LLVMContext &getLLVMContext() const { return Builder->getContext(); }

  /// Return whether \p ValTy should be cast to an integer for atomics.
  ///
  /// \param ValTy Type of the value involved in the atomic operation.
  /// \param CmpXchg Whether the cast is for a compare-exchange.
  /// \return True if the value type should be cast to an integer.
  LLVM_ABI bool shouldCastToInt(Type *ValTy, bool CmpXchg);

  /// Emit an inline atomic load of the atomic object.
  ///
  /// \param AO Memory ordering for the load.
  /// \param IsVolatile Whether the load is volatile.
  /// \param CmpXchg Whether the load feeds a compare-exchange.
  /// \return The loaded value.
  LLVM_ABI Value *EmitAtomicLoadOp(AtomicOrdering AO, bool IsVolatile,
                                   bool CmpXchg = false);

  /// Emit a call to an atomic runtime library function.
  ///
  /// \param fnName Name of the libcall to invoke.
  /// \param ResultType Return type of the libcall.
  /// \param Args Arguments to pass to the libcall.
  /// \return The call instruction.
  LLVM_ABI CallInst *EmitAtomicLibcall(StringRef fnName, Type *ResultType,
                                       ArrayRef<Value *> Args);

  /// Return the atomic object size in bytes as an IR value.
  ///
  /// \return Constant integer holding the atomic size in bytes.
  Value *getAtomicSizeValue() const {
    LLVMContext &ctx = getLLVMContext();
    // TODO: Get from llvm::TargetMachine / clang::TargetInfo
    // if clang shares this codegen in future
    constexpr uint16_t SizeTBits = 64;
    constexpr uint16_t BitsPerByte = 8;
    return ConstantInt::get(IntegerType::get(ctx, SizeTBits),
                            AtomicSizeInBits / BitsPerByte);
  }

  /// Emit an atomic compare-exchange via the runtime libcall.
  ///
  /// \param ExpectedVal Pointer to the expected value (updated on failure).
  /// \param DesiredVal Pointer to the desired value to store on success.
  /// \param Success Ordering used when the exchange succeeds.
  /// \param Failure Ordering used when the exchange fails.
  /// \return Pair of the expected pointer and the success flag.
  LLVM_ABI std::pair<Value *, Value *>
  EmitAtomicCompareExchangeLibcall(Value *ExpectedVal, Value *DesiredVal,
                                   AtomicOrdering Success,
                                   AtomicOrdering Failure);

  /// Cast an address to a pointer suitable for atomic integer ops.
  ///
  /// With opaque pointers this is a no-op and returns \p addr unchanged.
  ///
  /// \param addr Pointer to cast.
  /// \return The pointer typed for atomic integer access.
  Value *castToAtomicIntPointer(Value *addr) const {
    return addr; // opaque pointer
  }

  /// Return the atomic address typed as an atomic integer pointer.
  ///
  /// \return The atomic address cast for atomic integer access.
  Value *getAtomicAddressAsAtomicIntPointer() const {
    return castToAtomicIntPointer(getAtomicPointer());
  }

  /// Emit an inline atomic compare-exchange instruction.
  ///
  /// \param ExpectedVal Value expected at the atomic address.
  /// \param DesiredVal Value to store if the comparison succeeds.
  /// \param Success Ordering used when the exchange succeeds.
  /// \param Failure Ordering used when the exchange fails.
  /// \param IsVolatile Whether the operation is volatile.
  /// \param IsWeak Whether to emit a weak compare-exchange.
  /// \return Pair of the previous value and the success flag.
  LLVM_ABI std::pair<Value *, Value *>
  EmitAtomicCompareExchangeOp(Value *ExpectedVal, Value *DesiredVal,
                              AtomicOrdering Success, AtomicOrdering Failure,
                              bool IsVolatile = false, bool IsWeak = false);

  /// Emit an atomic compare-exchange, using a libcall when required.
  ///
  /// \param ExpectedVal Expected value (or pointer for the libcall path).
  /// \param DesiredVal Desired value (or pointer for the libcall path).
  /// \param Success Ordering used when the exchange succeeds.
  /// \param Failure Ordering used when the exchange fails.
  /// \param IsVolatile Whether the inline path is volatile.
  /// \param IsWeak Whether the inline path is weak.
  /// \return Pair of the previous/expected value and the success flag.
  LLVM_ABI std::pair<Value *, Value *>
  EmitAtomicCompareExchange(Value *ExpectedVal, Value *DesiredVal,
                            AtomicOrdering Success, AtomicOrdering Failure,
                            bool IsVolatile, bool IsWeak);

  /// Emit an atomic load via the `__atomic_load` runtime libcall.
  ///
  /// \param AO Memory ordering for the load.
  /// \return Pair of a load from the temporary and the temporary alloca.
  LLVM_ABI std::pair<LoadInst *, AllocaInst *>
  EmitAtomicLoadLibcall(AtomicOrdering AO);

  /// Emit an atomic store via the `__atomic_store` runtime libcall.
  ///
  /// \param AO Memory ordering for the store.
  /// \param Source Value to store into the atomic object.
  LLVM_ABI void EmitAtomicStoreLibcall(AtomicOrdering AO, Value *Source);
};
} // end namespace llvm

#endif /* LLVM_FRONTEND_ATOMIC_ATOMIC_H */
