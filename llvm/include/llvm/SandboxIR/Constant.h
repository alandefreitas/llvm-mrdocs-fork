//===- Constant.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SANDBOXIR_CONSTANT_H
#define LLVM_SANDBOXIR_CONSTANT_H

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalIFunc.h"
#include "llvm/IR/GlobalObject.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/SandboxIR/Argument.h"
#include "llvm/SandboxIR/BasicBlock.h"
#include "llvm/SandboxIR/Context.h"
#include "llvm/SandboxIR/Type.h"
#include "llvm/SandboxIR/User.h"
#include "llvm/Support/Compiler.h"

namespace llvm::sandboxir {

class BasicBlock;
class Function;

/// A SandboxIR wrapper for an LLVM constant value.
class Constant : public sandboxir::User {
protected:
  /// Construct a Constant wrapper around \p C.
  /// \param C Underlying LLVM constant.
  /// \param SBCtx SandboxIR context.
  Constant(llvm::Constant *C, sandboxir::Context &SBCtx)
      : sandboxir::User(ClassID::Constant, C, SBCtx) {}
  /// Construct a Constant-derived wrapper around \p C.
  /// \param ID SandboxIR class identifier.
  /// \param C Underlying LLVM constant.
  /// \param SBCtx SandboxIR context.
  Constant(ClassID ID, llvm::Constant *C, sandboxir::Context &SBCtx)
      : sandboxir::User(ID, C, SBCtx) {}
  friend class ConstantInt; // For constructor.
  friend class Function;    // For constructor
  friend class Context;     // For constructor.
  /// Return the Use for operand \p OpIdx.
  /// \param OpIdx Operand index.
  /// \param Verify Whether to verify the index.
  /// \Returns The \c Use for the given operand index.
  Use getOperandUseInternal(unsigned OpIdx, bool Verify) const override {
    return getOperandUseDefault(OpIdx, Verify);
  }

public:
  /// For isa/dyn_cast.
  /// \param From Value to test for Constant.
  /// \Returns True if \p From is a \c Constant.
  static bool classof(const sandboxir::Value *From) {
    switch (From->getSubclassID()) {
#define DEF_CONST(ID, CLASS) case ClassID::ID:
#include "llvm/SandboxIR/Values.def"
      return true;
    default:
      return false;
    }
  }
  /// Return the SandboxIR context that owns this constant.
  /// \Returns The SandboxIR context that owns this value.
  sandboxir::Context &getParent() const { return getContext(); }
  /// Return the operand number corresponding to \p Use.
  /// \param Use Operand use edge.
  /// \Returns The operand index corresponding to \p Use.
  unsigned getUseOperandNo(const Use &Use) const override {
    return getUseOperandNoDefault(Use);
  }
#ifndef NDEBUG
  /// Verify that this wraps an LLVM Constant.
  void verify() const override {
    assert(isa<llvm::Constant>(Val) && "Expected Constant!");
  }
  /// Dump this constant to \p OS.
  /// \param OS Output stream.
  void dumpOS(raw_ostream &OS) const override;
#endif
};

// TODO: This should inherit from ConstantData.
/// A SandboxIR wrapper for an integer constant.
class ConstantInt : public Constant {
  ConstantInt(llvm::ConstantInt *C, Context &Ctx)
      : Constant(ClassID::ConstantInt, C, Ctx) {}
  friend class Context; // For constructor.

  Use getOperandUseInternal(unsigned OpIdx, bool Verify) const final {
    llvm_unreachable("ConstantInt has no operands!");
  }

public:
  /// Return the boolean true constant for \p Ctx.
  /// \param Ctx SandboxIR context.
  /// \Returns The boolean true constant (or a splat of true).
  LLVM_ABI static ConstantInt *getTrue(Context &Ctx);
  /// Return the boolean false constant for \p Ctx.
  /// \param Ctx SandboxIR context.
  /// \Returns The boolean false constant (or a splat of false).
  LLVM_ABI static ConstantInt *getFalse(Context &Ctx);
  /// Return a boolean constant for \p V in \p Ctx.
  /// \param Ctx SandboxIR context.
  /// \param V Boolean value.
  /// \Returns A boolean constant (or splat) for the given value.
  LLVM_ABI static ConstantInt *getBool(Context &Ctx, bool V);
  /// Return true (or a splat of true) for type \p Ty.
  /// \param Ty Integer or vector-of-integer type.
  /// \Returns The boolean true constant (or a splat of true).
  LLVM_ABI static Constant *getTrue(Type *Ty);
  /// Return false (or a splat of false) for type \p Ty.
  /// \param Ty Integer or vector-of-integer type.
  /// \Returns The boolean false constant (or a splat of false).
  LLVM_ABI static Constant *getFalse(Type *Ty);
  /// Return a boolean constant (or splat) for \p V in type \p Ty.
  /// \param Ty Integer or vector-of-integer type.
  /// \param V Boolean value.
  /// \Returns A boolean constant (or splat) for the given value.
  LLVM_ABI static Constant *getBool(Type *Ty, bool V);

  /// If Ty is a vector type, return a splat; otherwise return a ConstantInt.
  /// \param Ty Integer or vector-of-integer type.
  /// \param V Integer value.
  /// \param IsSigned Whether to treat \p V as signed when extending.
  /// \Returns A constant (or vector splat) for the given value.
  LLVM_ABI static Constant *get(Type *Ty, uint64_t V, bool IsSigned = false);

  /// Return a ConstantInt with value \p V for type \p Ty.
  ///
  /// If the type is wider than 64 bits, the value will be zero-extended to fit
  /// the type, unless IsSigned is true, in which case the value will be
  /// interpreted as a 64-bit signed integer and sign-extended to fit the type.
  /// \param Ty Integer type.
  /// \param V Integer value.
  /// \param IsSigned Whether to treat \p V as signed when extending.
  /// \Returns A \c ConstantInt for the given arguments.
  LLVM_ABI static ConstantInt *get(IntegerType *Ty, uint64_t V,
                                   bool IsSigned = false);

  /// Return a ConstantInt with signed value \p V for type \p Ty.
  ///
  /// The value V will be canonicalized to an unsigned APInt. Accessing it with
  /// either getSExtValue() or getZExtValue() will yield a correctly sized and
  /// signed value for the type Ty.
  /// \param Ty Integer type.
  /// \param V Signed integer value.
  /// \Returns A signed integer constant (or splat) for the given value.
  LLVM_ABI static ConstantInt *getSigned(IntegerType *Ty, int64_t V);
  /// Return a Constant (or splat) with signed value \p V for type \p Ty.
  /// \param Ty Integer or vector-of-integer type.
  /// \param V Signed integer value.
  /// \Returns A signed integer constant (or splat) for the given value.
  LLVM_ABI static Constant *getSigned(Type *Ty, int64_t V);

  /// Return a ConstantInt with value \p V; the type matches the APInt width.
  /// \param Ctx SandboxIR context.
  /// \param V Integer value.
  /// \Returns A ConstantInt with value \p V; the type matches the APInt width.
  LLVM_ABI static ConstantInt *get(Context &Ctx, const APInt &V);

  /// Return a ConstantInt parsed from \p Str with radix \p Radix.
  /// \param Ty Integer type.
  /// \param Str Textual integer value.
  /// \param Radix Numeric radix.
  /// \Returns A ConstantInt parsed from \p Str with radix \p Radix.
  LLVM_ABI static ConstantInt *get(IntegerType *Ty, StringRef Str,
                                   uint8_t Radix);

  /// If Ty is a vector type, return a splat; otherwise return a ConstantInt.
  /// \param Ty Integer or vector-of-integer type.
  /// \param V Integer value.
  /// \Returns A constant (or vector splat) for the given value.
  LLVM_ABI static Constant *get(Type *Ty, const APInt &V);

  /// Return the constant as an APInt value reference. This allows clients to
  /// obtain a full-precision copy of the value.
  /// Return the constant's value.
  /// \Returns The constant's value.
  inline const APInt &getValue() const {
    return cast<llvm::ConstantInt>(Val)->getValue();
  }

  /// getBitWidth - Return the scalar bitwidth of this constant.
  /// \Returns The scalar bit width of this constant.
  unsigned getBitWidth() const {
    return cast<llvm::ConstantInt>(Val)->getBitWidth();
  }
  /// Return the constant as a zero-extended 64-bit unsigned integer.
  ///
  /// Note that this method can assert if the value does not fit in 64 bits.
  /// \Returns The zero-extended value as a 64-bit unsigned integer.
  inline uint64_t getZExtValue() const {
    return cast<llvm::ConstantInt>(Val)->getZExtValue();
  }

  /// Return the constant as a 64-bit integer value after it.
  ///
  /// has been sign extended as appropriate for the type of this constant. Note that this method can assert if the value does not fit in 64 bits. Return the sign extended value.
  /// \Returns The sign-extended value as a 64-bit signed integer.
  inline int64_t getSExtValue() const {
    return cast<llvm::ConstantInt>(Val)->getSExtValue();
  }

  /// Return the constant as an llvm::MaybeAlign.
  /// Note that this method can assert if the value does not fit in 64 bits or
  /// is not a power of two.
  /// \Returns The value as an \c llvm::MaybeAlign.
  inline MaybeAlign getMaybeAlignValue() const {
    return cast<llvm::ConstantInt>(Val)->getMaybeAlignValue();
  }

  /// Return the constant as an llvm::Align, interpreting `0` as `Align(1)`.
  ///
  /// Note that this method can assert if the value does not fit in 64 bits or is not a power of two.
  /// \Returns The value as an \c llvm::Align.
  inline Align getAlignValue() const {
    return cast<llvm::ConstantInt>(Val)->getAlignValue();
  }

  /// Return true if this constant's value equals \p V.
  ///
  /// This only works for very small values, because this is all that can be
  /// represented with all types.
  /// \param V Value to compare against.
  /// \Returns True if this constant equals \p V.
  bool equalsInt(uint64_t V) const {
    return cast<llvm::ConstantInt>(Val)->equalsInt(V);
  }

  /// Variant of the getType() method to always return an IntegerType, which
  /// reduces the amount of casting needed in parts of the compiler.
  /// \Returns This constant's type as an \c IntegerType.
  LLVM_ABI IntegerType *getIntegerType() const;

  /// Return true if type \p Ty is big enough to represent unsigned value \p V.
  ///
  /// This can be used to avoid having the get method assert when V is larger
  /// than Ty can represent. Note that there are two versions of this method,
  /// one for unsigned and one for signed integers. Although ConstantInt
  /// canonicalizes everything to an unsigned integer, the signed version avoids
  /// callers having to convert a signed quantity to the appropriate unsigned
  /// type before calling the method.
  /// \param Ty Type to test.
  /// \param V Unsigned value to represent.
  /// \returns true if V is a valid value for type Ty.
  LLVM_ABI static bool isValueValidForType(Type *Ty, uint64_t V);
  /// Return true if type \p Ty is big enough to represent signed value \p V.
  /// \param Ty Type to test.
  /// \param V Signed value to represent.
  /// \Returns True if \p V fits in type \p Ty.
  LLVM_ABI static bool isValueValidForType(Type *Ty, int64_t V);

  /// Return true if this constant is negative.
  /// \Returns True if this constant is negative.
  bool isNegative() const { return cast<llvm::ConstantInt>(Val)->isNegative(); }

  /// Return true if this constant is zero.
  ///
  /// This is just a convenience method to make client code smaller for a common
  /// case. It also correctly performs the comparison without the potential for
  /// an assertion from getZExtValue().
  /// \Returns True if this constant is zero.
  bool isZero() const { return cast<llvm::ConstantInt>(Val)->isZero(); }

  /// This is just a convenience method to make client code smaller for a common case.
  ///
  /// It also correctly performs the comparison without the potential for an assertion from getZExtValue(). Determine if the value is one.
  /// \Returns True if the condition described by this query holds.
  bool isOne() const { return cast<llvm::ConstantInt>(Val)->isOne(); }

  /// This function will return true iff every bit in this constant is set
  /// to true.
  /// @returns true iff this constant's bits are all set to true.
  /// Determine if the value is all ones.
  bool isMinusOne() const { return cast<llvm::ConstantInt>(Val)->isMinusOne(); }

  /// This function will return true iff this constant represents the largest
  /// value that may be represented by the constant's type.
  /// @returns true iff this is the largest value that may be represented
  /// by this type.
  /// Return true if this is the maximum value representable by this type.
  /// \param IsSigned Whether to interpret the value as signed.
  bool isMaxValue(bool IsSigned) const {
    return cast<llvm::ConstantInt>(Val)->isMaxValue(IsSigned);
  }

  /// Return true if this is the minimum value representable by this type.
  ///
  /// \param IsSigned Whether to interpret the value as signed.
  /// \returns true if this is the smallest value that may be represented by
  /// this type.
  bool isMinValue(bool IsSigned) const {
    return cast<llvm::ConstantInt>(Val)->isMinValue(IsSigned);
  }

  /// Return true if this constant is greater than or equal to \p Num.
  ///
  /// This is true if the constant has active bits bigger than 64 bits or a
  /// value greater than or equal to the given uint64_t value.
  /// \param Num Unsigned value to compare against.
  /// \returns true iff this constant is greater or equal to the given number.
  bool uge(uint64_t Num) const {
    return cast<llvm::ConstantInt>(Val)->uge(Num);
  }

  /// Return this constant's value saturated to \p Limit.
  ///
  /// If the value is smaller than the specified limit, return it, otherwise
  /// return the limit value.
  /// \param Limit Saturation limit.
  /// \returns the min of the value of the constant and the specified value.
  uint64_t getLimitedValue(uint64_t Limit = ~0ULL) const {
    return cast<llvm::ConstantInt>(Val)->getLimitedValue(Limit);
  }

  /// For isa/dyn_cast.
  /// \param From Value to test for ConstantInt.
  /// \Returns True if \p From is a \c ConstantInt.
  static bool classof(const sandboxir::Value *From) {
    return From->getSubclassID() == ClassID::ConstantInt;
  }
  /// Return the operand index for \p Use. Unreachable: this constant has no operands.
  /// \param Use Operand use edge (unused).
  /// \Returns The operand index corresponding to \p Use.
  unsigned getUseOperandNo(const Use &Use) const override {
    llvm_unreachable("ConstantInt has no operands!");
  }
#ifndef NDEBUG
  /// Verify that this wraps an LLVM ConstantInt.
  void verify() const override {
    assert(isa<llvm::ConstantInt>(Val) && "Expected a ConstantInst!");
  }
  /// Dump this integer constant to \p OS.
  /// \param OS Output stream.
  void dumpOS(raw_ostream &OS) const override {
    dumpCommonPrefix(OS);
    dumpCommonSuffix(OS);
  }
#endif
};

// TODO: This should inherit from ConstantData.
/// A SandboxIR wrapper for a floating-point constant.
class ConstantFP final : public Constant {
  ConstantFP(llvm::ConstantFP *C, Context &Ctx)
      : Constant(ClassID::ConstantFP, C, Ctx) {}
  friend class Context; // For constructor.

public:
  /// Return a ConstantFP (or splat vector) for \p V in type \p Ty.
  ///
  /// This should only be used for simple constant values like 2.0/1.0 etc, that
  /// are known-valid both as host double and as the target format.
  /// \param Ty Floating-point or vector-of-floating-point type.
  /// \param V Host double value.
  /// \Returns A \c ConstantFP for the given arguments.
  LLVM_ABI static Constant *get(Type *Ty, double V);

  /// If Ty is a vector type, return a splat; otherwise return a ConstantFP.
  /// \param Ty Floating-point or vector-of-floating-point type.
  /// \param V APFloat value.
  /// \Returns A constant (or vector splat) for the given value.
  LLVM_ABI static Constant *get(Type *Ty, const APFloat &V);

  /// Return a ConstantFP (or splat) parsed from string \p Str.
  /// \param Ty Floating-point or vector-of-floating-point type.
  /// \param Str Textual floating-point value.
  /// \Returns A ConstantFP (or splat) parsed from string \p Str.
  LLVM_ABI static Constant *get(Type *Ty, StringRef Str);

  /// Return a ConstantFP for \p V in \p Ctx.
  /// \param V APFloat value.
  /// \param Ctx SandboxIR context.
  /// \Returns A ConstantFP for \p V in \p Ctx.
  LLVM_ABI static ConstantFP *get(const APFloat &V, Context &Ctx);

  /// Return a NaN constant of type \p Ty.
  /// \param Ty Floating-point or vector type.
  /// \param Negative Whether the NaN is negative.
  /// \param Payload Optional NaN payload bits.
  /// \Returns A NaN constant of the given type.
  LLVM_ABI static Constant *getNaN(Type *Ty, bool Negative = false,
                                   uint64_t Payload = 0);
  /// Return a quiet NaN constant of type \p Ty.
  /// \param Ty Floating-point or vector type.
  /// \param Negative Whether the NaN is negative.
  /// \param Payload Optional NaN payload.
  /// \Returns A quiet NaN constant of the given type.
  LLVM_ABI static Constant *getQNaN(Type *Ty, bool Negative = false,
                                    APInt *Payload = nullptr);
  /// Return a signaling NaN constant of type \p Ty.
  /// \param Ty Floating-point or vector type.
  /// \param Negative Whether the NaN is negative.
  /// \param Payload Optional NaN payload.
  /// \Returns A signaling NaN constant of the given type.
  LLVM_ABI static Constant *getSNaN(Type *Ty, bool Negative = false,
                                    APInt *Payload = nullptr);
  /// Return a zero constant of type \p Ty.
  /// \param Ty Floating-point or vector type.
  /// \param Negative Whether to return negative zero.
  /// \Returns A zero constant of the given type.
  LLVM_ABI static Constant *getZero(Type *Ty, bool Negative = false);

  /// Return a negative-zero constant of type \p Ty.
  /// \param Ty Floating-point or vector type.
  /// \Returns A negative-zero constant of the given type.
  LLVM_ABI static Constant *getNegativeZero(Type *Ty);
  /// Return an infinity constant of type \p Ty.
  /// \param Ty Floating-point or vector type.
  /// \param Negative Whether to return negative infinity.
  /// \Returns An infinity constant of the given type.
  LLVM_ABI static Constant *getInfinity(Type *Ty, bool Negative = false);

  /// Return true if \p Ty is big enough to represent \p V.
  /// \param Ty Type to test.
  /// \param V Value to represent.
  /// \Returns True if \p V fits in type \p Ty.
  LLVM_ABI static bool isValueValidForType(Type *Ty, const APFloat &V);

  /// Return the constant as an APFloat.
  /// \Returns The floating-point value as an \c APFloat.
  inline const APFloat &getValueAPF() const {
    return cast<llvm::ConstantFP>(Val)->getValueAPF();
  }
  /// Return the constant as an APFloat.
  /// \Returns The constant's value.
  inline const APFloat &getValue() const {
    return cast<llvm::ConstantFP>(Val)->getValue();
  }

  /// Return true if the value is positive or negative zero.
  /// \Returns True if the value is positive or negative zero.
  bool isZero() const { return cast<llvm::ConstantFP>(Val)->isZero(); }

  /// Return true if the sign bit is set.
  /// \Returns True if the sign bit is set.
  bool isNegative() const { return cast<llvm::ConstantFP>(Val)->isNegative(); }

  /// Return true if the value is infinity
  /// \Returns True if the value is infinity.
  bool isInfinity() const { return cast<llvm::ConstantFP>(Val)->isInfinity(); }

  /// Return true if the value is a NaN.
  /// \Returns True if the value is a NaN.
  bool isNaN() const { return cast<llvm::ConstantFP>(Val)->isNaN(); }

  /// Return true if this constant exactly matches \p V bit-for-bit.
  ///
  /// We don't rely on operator== working on double values, as it returns true
  /// for things that are clearly not equal, like -0.0 and 0.0. As such, this
  /// method can be used to do an exact bit-for-bit comparison of two floating
  /// point values. The version with a double operand is retained because it's
  /// so convenient to write isExactlyValue(2.0), but please use it only for
  /// simple constants.
  /// \param V Value to compare against.
  /// \Returns True if this constant equals the given floating-point value.
  bool isExactlyValue(const APFloat &V) const {
    return cast<llvm::ConstantFP>(Val)->isExactlyValue(V);
  }

  /// Return true if this constant exactly matches host double \p V.
  /// \param V Host double to compare against.
  /// \Returns True if this constant equals the given floating-point value.
  bool isExactlyValue(double V) const {
    return cast<llvm::ConstantFP>(Val)->isExactlyValue(V);
  }

  /// For isa/dyn_cast.
  /// \param From Value to test for ConstantFP.
  /// \Returns True if \p From is a \c ConstantFP.
  static bool classof(const sandboxir::Value *From) {
    return From->getSubclassID() == ClassID::ConstantFP;
  }

  // TODO: Better name: getOperandNo(const Use&). Should be private.
  /// Return the operand index for \p Use. Unreachable: this constant has no operands.
  /// \param Use Operand use edge (unused).
  /// \Returns The operand index corresponding to \p Use.
  unsigned getUseOperandNo(const Use &Use) const final {
    llvm_unreachable("ConstantFP has no operands!");
  }
#ifndef NDEBUG
  /// Verify that this wraps an LLVM ConstantFP.
  void verify() const override {
    assert(isa<llvm::ConstantFP>(Val) && "Expected a ConstantFP!");
  }
  /// Dump this floating-point constant to \p OS.
  /// \param OS Output stream.
  void dumpOS(raw_ostream &OS) const override {
    dumpCommonPrefix(OS);
    dumpCommonSuffix(OS);
  }
#endif
};

/// Base class for aggregate constants (with operands).
class ConstantAggregate : public Constant {
protected:
  /// Construct a ConstantAggregate wrapper around \p C.
  /// \param ID SandboxIR class identifier.
  /// \param C Underlying LLVM constant.
  /// \param Ctx SandboxIR context.
  ConstantAggregate(ClassID ID, llvm::Constant *C, Context &Ctx)
      : Constant(ID, C, Ctx) {}

public:
  /// For isa/dyn_cast.
  /// \param From Value to test for ConstantAggregate.
  /// \Returns True if \p From is a \c ConstantAggregate.
  static bool classof(const sandboxir::Value *From) {
    auto ID = From->getSubclassID();
    return ID == ClassID::ConstantVector || ID == ClassID::ConstantStruct ||
           ID == ClassID::ConstantArray;
  }
};

/// A SandboxIR wrapper for a constant array.
class ConstantArray final : public ConstantAggregate {
  ConstantArray(llvm::ConstantArray *C, Context &Ctx)
      : ConstantAggregate(ClassID::ConstantArray, C, Ctx) {}
  friend class Context; // For constructor.

public:
  /// Return a constant array of type \p T with elements \p V.
  /// \param T Array type.
  /// \param V Element constants.
  /// \Returns A constant array of type \p T with elements \p V.
  LLVM_ABI static Constant *get(ArrayType *T, ArrayRef<Constant *> V);
  /// Return the array type of this constant.
  /// \Returns The type of this \c ConstantArray.
  LLVM_ABI ArrayType *getType() const;

  // TODO: Missing functions: getType(), getTypeForElements(), getAnon(), get().

  /// For isa/dyn_cast.
  /// \param From Value to test for ConstantArray.
  /// \Returns True if \p From is a \c ConstantArray.
  static bool classof(const Value *From) {
    return From->getSubclassID() == ClassID::ConstantArray;
  }
};

/// A SandboxIR wrapper for a constant struct.
class ConstantStruct final : public ConstantAggregate {
  ConstantStruct(llvm::ConstantStruct *C, Context &Ctx)
      : ConstantAggregate(ClassID::ConstantStruct, C, Ctx) {}
  friend class Context; // For constructor.

public:
  /// Return a constant struct of type \p T with elements \p V.
  /// \param T Struct type.
  /// \param V Element constants.
  /// \Returns A constant struct of type \p T with elements \p V.
  LLVM_ABI static Constant *get(StructType *T, ArrayRef<Constant *> V);

  /// Return a constant struct of type \p T with the given element constants.
  /// \param T Struct type.
  /// \param Vs Element constants.
  /// \Returns A constant struct of type \p T with the given element constants.
  template <typename... Csts>
  static std::enable_if_t<are_base_of<Constant, Csts...>::value, Constant *>
  get(StructType *T, Csts *...Vs) {
    return get(T, ArrayRef<Constant *>({Vs...}));
  }
  /// Return an anonymous struct that has the specified elements.
  ///
  /// If the struct is possibly empty, then you must specify a context.
  /// \param V Element constants.
  /// \param Packed Whether the anonymous struct is packed.
  /// \Returns An anonymous constant struct of the given elements.
  static Constant *getAnon(ArrayRef<Constant *> V, bool Packed = false) {
    return get(getTypeForElements(V, Packed), V);
  }
  /// Return an anonymous struct that has the specified elements.
  /// \param Ctx SandboxIR context (required if \p V may be empty).
  /// \param V Element constants.
  /// \param Packed Whether the anonymous struct is packed.
  /// \Returns An anonymous constant struct of the given elements.
  static Constant *getAnon(Context &Ctx, ArrayRef<Constant *> V,
                           bool Packed = false) {
    return get(getTypeForElements(Ctx, V, Packed), V);
  }
  /// Return an anonymous struct type for elements \p V (may be empty).
  /// \param Ctx SandboxIR context.
  /// \param V Element constants.
  /// \param Packed Whether the anonymous struct is packed.
  /// \Returns A suitable anonymous struct type for the element values.
  LLVM_ABI static StructType *
  getTypeForElements(Context &Ctx, ArrayRef<Constant *> V, bool Packed = false);
  /// Return an anonymous struct type for the specified non-empty element list.
  /// \param V Element constants (must not be empty).
  /// \param Packed Whether the anonymous struct is packed.
  /// \Returns A suitable anonymous struct type for the element values.
  static StructType *getTypeForElements(ArrayRef<Constant *> V,
                                        bool Packed = false) {
    assert(!V.empty() &&
           "ConstantStruct::getTypeForElements cannot be called on empty list");
    return getTypeForElements(V[0]->getContext(), V, Packed);
  }

  /// Specialization - reduce amount of casting.
  /// \Returns The type of this \c ConstantStruct.
  inline StructType *getType() const {
    return cast<StructType>(Value::getType());
  }

  /// For isa/dyn_cast.
  /// \param From Value to test for ConstantStruct.
  /// \Returns True if \p From is a \c ConstantStruct.
  static bool classof(const Value *From) {
    return From->getSubclassID() == ClassID::ConstantStruct;
  }
};

/// A SandboxIR wrapper for a constant vector with operand elements.
class ConstantVector final : public ConstantAggregate {
  ConstantVector(llvm::ConstantVector *C, Context &Ctx)
      : ConstantAggregate(ClassID::ConstantVector, C, Ctx) {}
  friend class Context; // For constructor.

public:
  /// Return a constant vector with elements \p V.
  /// \param V Vector element constants.
  /// \Returns A constant vector with elements \p V.
  LLVM_ABI static Constant *get(ArrayRef<Constant *> V);
  /// Return a ConstantVector with \p Elt in each element.
  ///
  /// Note that this might not return an instance of ConstantVector.
  /// \param EC Element count.
  /// \param Elt Splat element value.
  /// \Returns A splat constant vector of the given value.
  LLVM_ABI static Constant *getSplat(ElementCount EC, Constant *Elt);
  /// Specialize the getType() method to always return a FixedVectorType,
  /// which reduces the amount of casting needed in parts of the compiler.
  /// \Returns The type of this \c ConstantVector.
  inline FixedVectorType *getType() const {
    return cast<FixedVectorType>(Value::getType());
  }
  /// If all elements are equal, return that value; otherwise return nullptr.
  ///
  /// Ignore poison elements by setting AllowPoison to true.
  /// \param AllowPoison Whether to ignore poison elements when detecting a splat.
  /// \Returns The splat element constant, or null if not a splat.
  LLVM_ABI Constant *getSplatValue(bool AllowPoison = false) const;

  /// For isa/dyn_cast.
  /// \param From Value to test for ConstantVector.
  /// \Returns True if \p From is a \c ConstantVector.
  static bool classof(const Value *From) {
    return From->getSubclassID() == ClassID::ConstantVector;
  }
};

// TODO: Inherit from ConstantData.
/// A SandboxIR wrapper for an all-zero aggregate constant.
class ConstantAggregateZero final : public Constant {
  ConstantAggregateZero(llvm::ConstantAggregateZero *C, Context &Ctx)
      : Constant(ClassID::ConstantAggregateZero, C, Ctx) {}
  friend class Context; // For constructor.

public:
  /// Return a zero aggregate constant of type \p Ty.
  /// \param Ty Aggregate type.
  /// \Returns A zero aggregate constant of type \p Ty.
  LLVM_ABI static ConstantAggregateZero *get(Type *Ty);
  /// If this CAZ has array or vector type, return a zero with the right element
  /// type.
  /// \Returns A zero constant of the element type.
  LLVM_ABI Constant *getSequentialElement() const;
  /// If this CAZ has struct type, return a zero for element \p Elt.
  /// \param Elt Struct element index.
  /// \Returns A zero constant of the element type.
  LLVM_ABI Constant *getStructElement(unsigned Elt) const;
  /// Return a zero for GEP index \p C, or null if it cannot be determined.
  /// \param C GEP index constant.
  /// \Returns A zero constant of the element type.
  LLVM_ABI Constant *getElementValue(Constant *C) const;
  /// Return a zero for GEP index \p Idx.
  /// \param Idx GEP index.
  /// \Returns A zero constant of the element type.
  LLVM_ABI Constant *getElementValue(unsigned Idx) const;
  /// Return the number of elements in the array, vector, or struct.
  /// \Returns The number of elements in this aggregate.
  ElementCount getElementCount() const {
    return cast<llvm::ConstantAggregateZero>(Val)->getElementCount();
  }

  /// For isa/dyn_cast.
  /// \param From Value to test for ConstantAggregateZero.
  /// \Returns True if \p From is a \c ConstantAggregateZero.
  static bool classof(const sandboxir::Value *From) {
    return From->getSubclassID() == ClassID::ConstantAggregateZero;
  }
  /// Return the operand index for \p Use. Unreachable: this constant has no operands.
  /// \param Use Operand use edge (unused).
  /// \Returns The operand index corresponding to \p Use.
  unsigned getUseOperandNo(const Use &Use) const final {
    llvm_unreachable("ConstantAggregateZero has no operands!");
  }
#ifndef NDEBUG
  /// Verify that this wraps an LLVM ConstantAggregateZero.
  void verify() const override {
    assert(isa<llvm::ConstantAggregateZero>(Val) && "Expected a CAZ!");
  }
  /// Dump this zero aggregate to \p OS.
  /// \param OS Output stream.
  void dumpOS(raw_ostream &OS) const override {
    dumpCommonPrefix(OS);
    dumpCommonSuffix(OS);
  }
#endif
};

/// Base class for densely packed array/vector constants of simple data elements.
///
/// A vector or array constant whose element type is a simple 1/2/4/8-byte
/// integer or half/bfloat/float/double, and whose elements are just simple data
/// values (i.e. ConstantInt/ConstantFP). This Constant node has no operands
/// because it stores all of the elements of the constant as densely packed
/// data, instead of as Value*'s.
///
/// This is the common base class of ConstantDataArray and ConstantDataVector.
class ConstantDataSequential : public Constant {
protected:
  /// Construct a ConstantDataSequential wrapper around \p C.
  /// \param ID SandboxIR class identifier.
  /// \param C Underlying LLVM constant.
  /// \param Ctx SandboxIR context.
  ConstantDataSequential(ClassID ID, llvm::ConstantDataSequential *C,
                         Context &Ctx)
      : Constant(ID, C, Ctx) {}

public:
  /// Return true if \p Ty can be used as a ConstantDataSequential element type.
  ///
  /// ConstantDataArray only works with normal float and int types that are
  /// stored densely in memory, not with things like i42 or x86_f80.
  /// \param Ty Element type to test.
  /// \Returns True if the type can be an element of a ConstantDataSequential.
  static bool isElementTypeCompatible(Type *Ty) {
    return llvm::ConstantDataSequential::isElementTypeCompatible(Ty->LLVMTy);
  }
  /// If this is a sequential container of integers, return element \p ElmIdx.
  /// \param ElmIdx Element index.
  /// \Returns The element at the given index as an integer.
  uint64_t getElementAsInteger(unsigned ElmIdx) const {
    return cast<llvm::ConstantDataSequential>(Val)->getElementAsInteger(ElmIdx);
  }
  /// If this is a sequential container of integers, return element \p ElmIdx as APInt.
  /// \param ElmIdx Element index.
  /// \Returns The element at the given index as an \c APInt.
  APInt getElementAsAPInt(unsigned ElmIdx) const {
    return cast<llvm::ConstantDataSequential>(Val)->getElementAsAPInt(ElmIdx);
  }
  /// If this is a sequential container of floating point type, return element \p ElmIdx.
  /// \param ElmIdx Element index.
  /// \Returns The element at the given index as an \c APFloat.
  APFloat getElementAsAPFloat(unsigned ElmIdx) const {
    return cast<llvm::ConstantDataSequential>(Val)->getElementAsAPFloat(ElmIdx);
  }
  /// If this is a sequential container of floats, return element \p ElmIdx.
  /// \param ElmIdx Element index.
  /// \Returns The element at the given index as a float.
  float getElementAsFloat(unsigned ElmIdx) const {
    return cast<llvm::ConstantDataSequential>(Val)->getElementAsFloat(ElmIdx);
  }
  /// If this is a sequential container of doubles, return element \p ElmIdx.
  /// \param ElmIdx Element index.
  /// \Returns The element at the given index as a double.
  double getElementAsDouble(unsigned ElmIdx) const {
    return cast<llvm::ConstantDataSequential>(Val)->getElementAsDouble(ElmIdx);
  }
  /// Return a Constant for element \p ElmIdx.
  ///
  /// Note that this has to compute a new constant to return, so it isn't as
  /// efficient as getElementAsInteger/Float/Double.
  /// \param ElmIdx Element index.
  /// \Returns The element at the given index as a \c Constant.
  Constant *getElementAsConstant(unsigned ElmIdx) const {
    return Ctx.getOrCreateConstant(
        cast<llvm::ConstantDataSequential>(Val)->getElementAsConstant(ElmIdx));
  }
  /// Return the element type of the array/vector.
  /// \Returns The element type.
  Type *getElementType() const {
    return Ctx.getType(
        cast<llvm::ConstantDataSequential>(Val)->getElementType());
  }
  /// Return the number of elements in the array or vector.
  /// \Returns The number of elements.
  unsigned getNumElements() const {
    return cast<llvm::ConstantDataSequential>(Val)->getNumElements();
  }
  /// Return the size (in bytes) of each element in the array/vector.
  /// The size of the elements is known to be a multiple of one byte.
  /// \Returns The size in bytes of one element.
  uint64_t getElementByteSize() const {
    return cast<llvm::ConstantDataSequential>(Val)->getElementByteSize();
  }
  /// Return true if this is an array of \p CharSize integers.
  /// \param CharSize Character size in bits (default 8).
  /// \Returns True if this is an array of i8.
  bool isString(unsigned CharSize = 8) const {
    return cast<llvm::ConstantDataSequential>(Val)->isString(CharSize);
  }
  /// This method returns true if the array "isString", ends with a null byte,
  /// and does not contains any other null bytes.
  /// \Returns True if this is a null-terminated C string.
  bool isCString() const {
    return cast<llvm::ConstantDataSequential>(Val)->isCString();
  }
  /// If this array is isString(), then this method returns the array as a
  /// StringRef. Otherwise, it asserts out.
  /// \Returns The contents as a string.
  StringRef getAsString() const {
    return cast<llvm::ConstantDataSequential>(Val)->getAsString();
  }
  /// If this array is isCString(), then this method returns the array (without
  /// the trailing null byte) as a StringRef. Otherwise, it asserts out.
  /// \Returns The contents as a C string (without the trailing null).
  StringRef getAsCString() const {
    return cast<llvm::ConstantDataSequential>(Val)->getAsCString();
  }
  /// Return the raw underlying bytes of this data.
  ///
  /// Note that this is an extremely tricky thing to work with, as it exposes
  /// the host endianness of the data elements.
  /// \Returns The raw bytes of the sequential data.
  StringRef getRawDataValues() const {
    return cast<llvm::ConstantDataSequential>(Val)->getRawDataValues();
  }

  /// For isa/dyn_cast.
  /// \param From Value to test for ConstantDataSequential.
  /// \Returns True if \p From is a \c ConstantDataSequential.
  static bool classof(const Value *From) {
    return From->getSubclassID() == ClassID::ConstantDataArray ||
           From->getSubclassID() == ClassID::ConstantDataVector;
  }
};

/// A densely packed array constant of simple integer or floating-point elements.
class ConstantDataArray final : public ConstantDataSequential {
  ConstantDataArray(llvm::ConstantDataArray *C, Context &Ctx)
      : ConstantDataSequential(ClassID::ConstantDataArray, C, Ctx) {}
  friend class Context;

public:
  /// For isa/dyn_cast.
  /// \param From Value to test for ConstantDataArray.
  /// \Returns True if \p From is a \c ConstantDataArray.
  static bool classof(const Value *From) {
    return From->getSubclassID() == ClassID::ConstantDataArray;
  }
  /// Return an array constant matching the element count and type of \p Elts.
  ///
  /// Note that this can return a ConstantAggregateZero object.
  /// \param Ctx SandboxIR context.
  /// \param Elts Element values.
  /// \Returns A \c ConstantDataArray for the given arguments.
  template <typename ElementTy>
  static Constant *get(Context &Ctx, ArrayRef<ElementTy> Elts) {
    auto *NewLLVMC = llvm::ConstantDataArray::get(Ctx.LLVMCtx, Elts);
    return Ctx.getOrCreateConstant(NewLLVMC);
  }

  /// Return an array constant from a container compatible with ArrayRef.
  /// \param Ctx SandboxIR context.
  /// \param Elts Element values.
  /// \Returns An array constant from a container compatible with ArrayRef.
  template <typename ArrayTy>
  static Constant *get(Context &Ctx, ArrayTy &Elts) {
    return ConstantDataArray::get(Ctx, ArrayRef(Elts));
  }

  /// Return an array constant from raw element bytes.
  ///
  /// Element count and type match \p NumElements and \p ElementTy. Note that
  /// this can return a ConstantAggregateZero object. ElementTy must be one of
  /// i8/i16/i32/i64/half/bfloat/float/double. Data is the buffer containing the
  /// elements. Be careful to make sure Data uses the right endianness, the
  /// buffer will be used as-is.
  /// \param Data Raw element bytes.
  /// \param NumElements Number of elements.
  /// \param ElementTy Element type.
  /// \Returns A constant data sequential for the raw data and element type.
  static Constant *getRaw(StringRef Data, uint64_t NumElements,
                          Type *ElementTy) {
    auto *LLVMC =
        llvm::ConstantDataArray::getRaw(Data, NumElements, ElementTy->LLVMTy);
    return ElementTy->getContext().getOrCreateConstant(LLVMC);
  }
  /// Return an array constant of float element type from bit patterns in \p Elts.
  ///
  /// The amount of bits of the contained type must match the number of bits of
  /// the type contained in the passed in ArrayRef (i.e. half or bfloat for
  /// 16bits, float for 32bits, double for 64bits). Note that this can return a
  /// ConstantAggregateZero object.
  /// \param ElementType Floating-point element type.
  /// \param Elts Bit patterns for each element.
  /// \Returns A constant data sequential of floating-point elements.
  static Constant *getFP(Type *ElementType, ArrayRef<uint16_t> Elts) {
    auto *LLVMC = llvm::ConstantDataArray::getFP(ElementType->LLVMTy, Elts);
    return ElementType->getContext().getOrCreateConstant(LLVMC);
  }
  /// Return an array constant of float element type from bit patterns in \p Elts.
  /// \param ElementType Floating-point element type.
  /// \param Elts Bit patterns for each element.
  /// \Returns A constant data sequential of floating-point elements.
  static Constant *getFP(Type *ElementType, ArrayRef<uint32_t> Elts) {
    auto *LLVMC = llvm::ConstantDataArray::getFP(ElementType->LLVMTy, Elts);
    return ElementType->getContext().getOrCreateConstant(LLVMC);
  }
  /// Return an array constant of float element type from bit patterns in \p Elts.
  /// \param ElementType Floating-point element type.
  /// \param Elts Bit patterns for each element.
  /// \Returns A constant data sequential of floating-point elements.
  static Constant *getFP(Type *ElementType, ArrayRef<uint64_t> Elts) {
    auto *LLVMC = llvm::ConstantDataArray::getFP(ElementType->LLVMTy, Elts);
    return ElementType->getContext().getOrCreateConstant(LLVMC);
  }
  /// Construct a constant data array initialized from a text string.
  ///
  /// The default behavior (AddNull==true) causes a null terminator to be placed
  /// at the end of the array (increasing the length of the string by one more
  /// than the StringRef would normally indicate). Pass AddNull=false to disable
  /// this behavior.
  /// \param Ctx SandboxIR context.
  /// \param Initializer String contents.
  /// \param AddNull Whether to append a null terminator.
  /// \Returns A constant data array containing the string.
  static Constant *getString(Context &Ctx, StringRef Initializer,
                             bool AddNull = true) {
    auto *LLVMC =
        llvm::ConstantDataArray::getString(Ctx.LLVMCtx, Initializer, AddNull);
    return Ctx.getOrCreateConstant(LLVMC);
  }

  /// Specialize the getType() method to always return an ArrayType,
  /// which reduces the amount of casting needed in parts of the compiler.
  /// \Returns The type of this \c ConstantDataArray.
  inline ArrayType *getType() const {
    return cast<ArrayType>(Value::getType());
  }
};

/// A densely packed vector constant of simple integer or floating-point elements.
///
/// Element type is a simple 1/2/4/8-byte integer or float/double, and elements
/// are just simple data values (i.e. ConstantInt/ConstantFP). This Constant
/// node has no operands because it stores all of the elements of the constant
/// as densely packed data, instead of as Value*'s.
class ConstantDataVector final : public ConstantDataSequential {
  ConstantDataVector(llvm::ConstantDataVector *C, Context &Ctx)
      : ConstantDataSequential(ClassID::ConstantDataVector, C, Ctx) {}
  friend class Context;

public:
  /// For isa/dyn_cast.
  /// \param From Value to test for ConstantDataVector.
  /// \Returns True if \p From is a \c ConstantDataVector.
  static bool classof(const Value *From) {
    return From->getSubclassID() == ClassID::ConstantDataVector;
  }
  /// Return a vector constant matching the element count and type of \p Elts.
  ///
  /// Note that this can return a ConstantAggregateZero object.
  /// \param Ctx SandboxIR context.
  /// \param Elts Element values.
  /// \Returns A \c ConstantDataVector for the given arguments.
  static Constant *get(Context &Ctx, ArrayRef<uint8_t> Elts) {
    auto *NewLLVMC = llvm::ConstantDataVector::get(Ctx.LLVMCtx, Elts);
    return Ctx.getOrCreateConstant(NewLLVMC);
  }
  /// Return a vector constant matching the element count and type of \p Elts.
  /// \param Ctx SandboxIR context.
  /// \param Elts Element values.
  /// \Returns A vector constant matching the element count and type of \p Elts.
  static Constant *get(Context &Ctx, ArrayRef<uint16_t> Elts) {
    auto *NewLLVMC = llvm::ConstantDataVector::get(Ctx.LLVMCtx, Elts);
    return Ctx.getOrCreateConstant(NewLLVMC);
  }
  /// Return a vector constant matching the element count and type of \p Elts.
  /// \param Ctx SandboxIR context.
  /// \param Elts Element values.
  /// \Returns A vector constant matching the element count and type of \p Elts.
  static Constant *get(Context &Ctx, ArrayRef<uint32_t> Elts) {
    auto *NewLLVMC = llvm::ConstantDataVector::get(Ctx.LLVMCtx, Elts);
    return Ctx.getOrCreateConstant(NewLLVMC);
  }
  /// Return a vector constant matching the element count and type of \p Elts.
  /// \param Ctx SandboxIR context.
  /// \param Elts Element values.
  /// \Returns A vector constant matching the element count and type of \p Elts.
  static Constant *get(Context &Ctx, ArrayRef<uint64_t> Elts) {
    auto *NewLLVMC = llvm::ConstantDataVector::get(Ctx.LLVMCtx, Elts);
    return Ctx.getOrCreateConstant(NewLLVMC);
  }
  /// Return a vector constant matching the element count and type of \p Elts.
  /// \param Ctx SandboxIR context.
  /// \param Elts Element values.
  /// \Returns A vector constant matching the element count and type of \p Elts.
  static Constant *get(Context &Ctx, ArrayRef<float> Elts) {
    auto *NewLLVMC = llvm::ConstantDataVector::get(Ctx.LLVMCtx, Elts);
    return Ctx.getOrCreateConstant(NewLLVMC);
  }
  /// Return a vector constant matching the element count and type of \p Elts.
  /// \param Ctx SandboxIR context.
  /// \param Elts Element values.
  /// \Returns A vector constant matching the element count and type of \p Elts.
  static Constant *get(Context &Ctx, ArrayRef<double> Elts) {
    auto *NewLLVMC = llvm::ConstantDataVector::get(Ctx.LLVMCtx, Elts);
    return Ctx.getOrCreateConstant(NewLLVMC);
  }

  /// Return a vector constant from raw element bytes.
  ///
  /// Element count and type match \p NumElements and \p ElementTy. Note that
  /// this can return a ConstantAggregateZero object. ElementTy must be one of
  /// i8/i16/i32/i64/half/bfloat/float/double. Data is the buffer containing the
  /// elements. Be careful to make sure Data uses the right endianness, the
  /// buffer will be used as-is.
  /// \param Data Raw element bytes.
  /// \param NumElements Number of elements.
  /// \param ElementTy Element type.
  /// \Returns A constant data sequential for the raw data and element type.
  static Constant *getRaw(StringRef Data, uint64_t NumElements,
                          Type *ElementTy) {
    auto *NewLLVMC =
        llvm::ConstantDataVector::getRaw(Data, NumElements, ElementTy->LLVMTy);
    return ElementTy->getContext().getOrCreateConstant(NewLLVMC);
  }
  /// Return a vector constant of float element type from bit patterns in \p Elts.
  ///
  /// The amount of bits of the contained type must match the number of bits of
  /// the type contained in the passed in ArrayRef (i.e. half or bfloat for
  /// 16bits, float for 32bits, double for 64bits). Note that this can return a
  /// ConstantAggregateZero object.
  /// \param ElementType Floating-point element type.
  /// \param Elts Bit patterns for each element.
  /// \Returns A constant data sequential of floating-point elements.
  static Constant *getFP(Type *ElementType, ArrayRef<uint16_t> Elts) {
    auto *NewLLVMC = llvm::ConstantDataVector::getFP(ElementType->LLVMTy, Elts);
    return ElementType->getContext().getOrCreateConstant(NewLLVMC);
  }
  /// Return a vector constant of float element type from bit patterns in \p Elts.
  /// \param ElementType Floating-point element type.
  /// \param Elts Bit patterns for each element.
  /// \Returns A constant data sequential of floating-point elements.
  static Constant *getFP(Type *ElementType, ArrayRef<uint32_t> Elts) {
    auto *NewLLVMC = llvm::ConstantDataVector::getFP(ElementType->LLVMTy, Elts);
    return ElementType->getContext().getOrCreateConstant(NewLLVMC);
  }
  /// Return a vector constant of float element type from bit patterns in \p Elts.
  /// \param ElementType Floating-point element type.
  /// \param Elts Bit patterns for each element.
  /// \Returns A constant data sequential of floating-point elements.
  static Constant *getFP(Type *ElementType, ArrayRef<uint64_t> Elts) {
    auto *NewLLVMC = llvm::ConstantDataVector::getFP(ElementType->LLVMTy, Elts);
    return ElementType->getContext().getOrCreateConstant(NewLLVMC);
  }

  /// Return a splat vector with \p Elt in each of \p NumElts elements.
  ///
  /// The specified constant has to be of a compatible type
  /// (i8/i16/i32/i64/half/bfloat/float/double) and must be a ConstantFP or
  /// ConstantInt.
  /// \param NumElts Number of vector elements.
  /// \param Elt Splat element value.
  /// \Returns A splat constant vector of the given value.
  static Constant *getSplat(unsigned NumElts, Constant *Elt) {
    auto *NewLLVMC = llvm::ConstantDataVector::getSplat(
        NumElts, cast<llvm::Constant>(Elt->Val));
    return Elt->getContext().getOrCreateConstant(NewLLVMC);
  }

  /// Returns true if this is a splat constant, meaning that all elements have
  /// the same value.
  /// \Returns True if every element is the same.
  bool isSplat() const {
    return cast<llvm::ConstantDataVector>(Val)->isSplat();
  }

  /// If this is a splat constant, meaning that all of the elements have the
  /// same value, return that value. Otherwise return NULL.
  /// \Returns The splat element constant, or null if not a splat.
  Constant *getSplatValue() const {
    return Ctx.getOrCreateConstant(
        cast<llvm::ConstantDataVector>(Val)->getSplatValue());
  }

  /// Specialize the getType() method to always return a FixedVectorType,
  /// which reduces the amount of casting needed in parts of the compiler.
  /// \Returns The type of this \c ConstantDataVector.
  inline FixedVectorType *getType() const {
    return cast<FixedVectorType>(Value::getType());
  }
};

// TODO: Inherit from ConstantData.
/// A SandboxIR wrapper for a null pointer constant.
class ConstantPointerNull final : public Constant {
  ConstantPointerNull(llvm::ConstantPointerNull *C, Context &Ctx)
      : Constant(ClassID::ConstantPointerNull, C, Ctx) {}
  friend class Context; // For constructor.

public:
  /// Return a null pointer constant of type \p Ty.
  /// \param Ty Pointer type of the null constant.
  /// \Returns A null pointer constant of type \p Ty.
  LLVM_ABI static ConstantPointerNull *get(PointerType *Ty);

  /// Return the type of this null pointer constant.
  /// \Returns The type of this \c ConstantPointerNull.
  LLVM_ABI Type *getType() const;
  /// Return the pointer type of this null pointer constant.
  /// \Returns The pointer type of this null constant.
  LLVM_ABI PointerType *getPointerType() const;

  /// For isa/dyn_cast.
  /// \param From Value to test for ConstantPointerNull.
  /// \Returns True if \p From is a \c ConstantPointerNull.
  static bool classof(const sandboxir::Value *From) {
    return From->getSubclassID() == ClassID::ConstantPointerNull;
  }
  /// Return the operand index for \p Use. Unreachable: this constant has no operands.
  /// \param Use Operand use edge (unused).
  /// \Returns The operand index corresponding to \p Use.
  unsigned getUseOperandNo(const Use &Use) const final {
    llvm_unreachable("ConstantPointerNull has no operands!");
  }
#ifndef NDEBUG
  /// Verify that this wraps an LLVM ConstantPointerNull.
  void verify() const override {
    assert(isa<llvm::ConstantPointerNull>(Val) && "Expected a CPNull!");
  }
  /// Dump this null pointer constant to \p OS.
  /// \param OS Output stream.
  void dumpOS(raw_ostream &OS) const override {
    dumpCommonPrefix(OS);
    dumpCommonSuffix(OS);
  }
#endif
};

// TODO: Inherit from ConstantData.
/// A SandboxIR wrapper for an LLVM undef value.
class UndefValue : public Constant {
protected:
  /// Construct an UndefValue wrapper around \p C.
  /// \param C Underlying LLVM undef value.
  /// \param Ctx SandboxIR context.
  UndefValue(llvm::UndefValue *C, Context &Ctx)
      : Constant(ClassID::UndefValue, C, Ctx) {}
  /// Construct an UndefValue-derived wrapper around \p C.
  /// \param ID SandboxIR class identifier.
  /// \param C Underlying LLVM constant.
  /// \param Ctx SandboxIR context.
  UndefValue(ClassID ID, llvm::Constant *C, Context &Ctx)
      : Constant(ID, C, Ctx) {}
  friend class Context; // For constructor.

public:
  /// Return an undef object of the specified type.
  /// \param T Type of the undef value.
  /// \Returns An undef object of the specified type.
  LLVM_ABI static UndefValue *get(Type *T);

  /// If this Undef has array or vector type, return a undef with the right
  /// element type.
  /// \Returns An undef constant of the element type.
  LLVM_ABI UndefValue *getSequentialElement() const;

  /// If this undef has struct type, return a undef for element \p Elt.
  /// \param Elt Struct element index.
  /// \Returns An undef constant of the element type.
  LLVM_ABI UndefValue *getStructElement(unsigned Elt) const;

  /// Return an undef for GEP index \p C, or null if it cannot be determined.
  /// \param C GEP index constant.
  /// \Returns An undef constant of the element type.
  LLVM_ABI UndefValue *getElementValue(Constant *C) const;

  /// Return an undef for GEP index \p Idx.
  /// \param Idx GEP index.
  /// \Returns An undef constant of the element type.
  LLVM_ABI UndefValue *getElementValue(unsigned Idx) const;

  /// Return the number of elements in the array, vector, or struct.
  /// \Returns The number of elements.
  unsigned getNumElements() const {
    return cast<llvm::UndefValue>(Val)->getNumElements();
  }

  /// For isa/dyn_cast.
  /// \param From Value to test for UndefValue or PoisonValue.
  /// \Returns True if \p From is a \c UndefValue.
  static bool classof(const sandboxir::Value *From) {
    return From->getSubclassID() == ClassID::UndefValue ||
           From->getSubclassID() == ClassID::PoisonValue;
  }
  /// Return the operand index for \p Use. Unreachable: this constant has no operands.
  /// \param Use Operand use edge (unused).
  /// \Returns The operand index corresponding to \p Use.
  unsigned getUseOperandNo(const Use &Use) const final {
    llvm_unreachable("UndefValue has no operands!");
  }
#ifndef NDEBUG
  /// Verify that this wraps an LLVM UndefValue.
  void verify() const override {
    assert(isa<llvm::UndefValue>(Val) && "Expected an UndefValue!");
  }
  /// Dump this undef value to \p OS.
  /// \param OS Output stream.
  void dumpOS(raw_ostream &OS) const override {
    dumpCommonPrefix(OS);
    dumpCommonSuffix(OS);
  }
#endif
};

/// A SandboxIR wrapper for an LLVM poison value.
class PoisonValue final : public UndefValue {
  PoisonValue(llvm::PoisonValue *C, Context &Ctx)
      : UndefValue(ClassID::PoisonValue, C, Ctx) {}
  friend class Context; // For constructor.

public:
  /// Return a poison object of the specified type.
  /// \param T Type of the poison value.
  /// \Returns A poison object of the specified type.
  LLVM_ABI static PoisonValue *get(Type *T);

  /// If this poison has array or vector type, return a poison with the right
  /// element type.
  /// \Returns A poison constant of the element type.
  LLVM_ABI PoisonValue *getSequentialElement() const;

  /// If this poison has struct type, return a poison for element \p Elt.
  /// \param Elt Struct element index.
  /// \Returns A poison constant of the element type.
  LLVM_ABI PoisonValue *getStructElement(unsigned Elt) const;

  /// Return a poison for GEP index \p C, or null if it cannot be determined.
  /// \param C GEP index constant.
  /// \Returns A poison constant of the element type.
  LLVM_ABI PoisonValue *getElementValue(Constant *C) const;

  /// Return a poison for GEP index \p Idx.
  /// \param Idx GEP index.
  /// \Returns A poison constant of the element type.
  LLVM_ABI PoisonValue *getElementValue(unsigned Idx) const;

  /// For isa/dyn_cast.
  /// \param From Value to test for PoisonValue.
  /// \Returns True if \p From is a \c PoisonValue.
  static bool classof(const sandboxir::Value *From) {
    return From->getSubclassID() == ClassID::PoisonValue;
  }
#ifndef NDEBUG
  /// Verify that this wraps an LLVM PoisonValue.
  void verify() const override {
    assert(isa<llvm::PoisonValue>(Val) && "Expected a PoisonValue!");
  }
  /// Dump this poison value to \p OS.
  /// \param OS Output stream.
  void dumpOS(raw_ostream &OS) const override {
    dumpCommonPrefix(OS);
    dumpCommonSuffix(OS);
  }
#endif
};

/// A SandboxIR wrapper for an LLVM GlobalValue.
class GlobalValue : public Constant {
protected:
  /// Construct a GlobalValue wrapper around \p C.
  /// \param ID SandboxIR class identifier.
  /// \param C Underlying LLVM global value.
  /// \param Ctx SandboxIR context.
  GlobalValue(ClassID ID, llvm::GlobalValue *C, Context &Ctx)
      : Constant(ID, C, Ctx) {}
  friend class Context; // For constructor.

public:
  /// Linkage kinds from the underlying LLVM GlobalValue.
  using LinkageTypes = llvm::GlobalValue::LinkageTypes;
  /// For isa/dyn_cast.
  /// \param From Value to test for GlobalValue.
  /// \Returns True if \p From is a \c GlobalValue.
  static bool classof(const sandboxir::Value *From) {
    switch (From->getSubclassID()) {
    case ClassID::Function:
    case ClassID::GlobalVariable:
    case ClassID::GlobalAlias:
    case ClassID::GlobalIFunc:
      return true;
    default:
      return false;
    }
  }

  /// Return the address space of this global value.
  /// \Returns The address space of this global value.
  unsigned getAddressSpace() const {
    return cast<llvm::GlobalValue>(Val)->getAddressSpace();
  }
  /// Return true if this global has globally unnamed_addr.
  /// \Returns True if this global has globally unnamed_addr.
  bool hasGlobalUnnamedAddr() const {
    return cast<llvm::GlobalValue>(Val)->hasGlobalUnnamedAddr();
  }

  /// Return true if this value's address is not significant in this module.
  ///
  /// This attribute is intended to be used only by the code generator and LTO
  /// to allow the linker to decide whether the global needs to be in the symbol
  /// table. It should probably not be used in optimizations, as the value may
  /// have uses outside the module; use hasGlobalUnnamedAddr() instead.
  /// \Returns True if this value's address is not significant in this module.
  bool hasAtLeastLocalUnnamedAddr() const {
    return cast<llvm::GlobalValue>(Val)->hasAtLeastLocalUnnamedAddr();
  }

  /// Unnamed address significance from the underlying LLVM GlobalValue.
  using UnnamedAddr = llvm::GlobalValue::UnnamedAddr;

  /// Return the unnamed_addr kind of this global.
  /// \Returns The unnamed_addr kind of this global.
  UnnamedAddr getUnnamedAddr() const {
    return cast<llvm::GlobalValue>(Val)->getUnnamedAddr();
  }
  /// Set the unnamed_addr kind of this global.
  /// \param V New unnamed_addr kind.
  LLVM_ABI void setUnnamedAddr(UnnamedAddr V);

  /// Return the more restrictive of two unnamed_addr kinds.
  /// \param A First unnamed_addr kind.
  /// \param B Second unnamed_addr kind.
  /// \Returns The more restrictive of the two unnamed_addr kinds.
  static UnnamedAddr getMinUnnamedAddr(UnnamedAddr A, UnnamedAddr B) {
    return llvm::GlobalValue::getMinUnnamedAddr(A, B);
  }

  /// Return true if this global has a COMDAT.
  /// \Returns True if this global has a COMDAT.
  bool hasComdat() const { return cast<llvm::GlobalValue>(Val)->hasComdat(); }

  // TODO: We need a SandboxIR Comdat if we want to implement getComdat().
  /// Visibility kinds from the underlying LLVM GlobalValue.
  using VisibilityTypes = llvm::GlobalValue::VisibilityTypes;
  /// Return the visibility of this global value.
  /// \Returns The visibility of this global value.
  VisibilityTypes getVisibility() const {
    return cast<llvm::GlobalValue>(Val)->getVisibility();
  }
  /// Return true if this global has default visibility.
  /// \Returns True if this global has default visibility.
  bool hasDefaultVisibility() const {
    return cast<llvm::GlobalValue>(Val)->hasDefaultVisibility();
  }
  /// Return true if this global has hidden visibility.
  /// \Returns True if this global has hidden visibility.
  bool hasHiddenVisibility() const {
    return cast<llvm::GlobalValue>(Val)->hasHiddenVisibility();
  }
  /// Return true if this global has protected visibility.
  /// \Returns True if this global has protected visibility.
  bool hasProtectedVisibility() const {
    return cast<llvm::GlobalValue>(Val)->hasProtectedVisibility();
  }
  /// Set the visibility of this global value.
  /// \param V New visibility.
  LLVM_ABI void setVisibility(VisibilityTypes V);

  // TODO: Add missing functions.
};

/// A SandboxIR wrapper for an LLVM GlobalObject (function, variable, or ifunc).
class GlobalObject : public GlobalValue {
protected:
  /// Construct a GlobalObject wrapper around \p C.
  /// \param ID SandboxIR class identifier.
  /// \param C Underlying LLVM global object.
  /// \param Ctx SandboxIR context.
  GlobalObject(ClassID ID, llvm::GlobalObject *C, Context &Ctx)
      : GlobalValue(ID, C, Ctx) {}
  friend class Context; // For constructor.
  /// Return the Use for operand \p OpIdx.
  /// \param OpIdx Operand index.
  /// \param Verify Whether to verify the index.
  /// \Returns The \c Use for the given operand index.
  Use getOperandUseInternal(unsigned OpIdx, bool Verify) const final {
    return getOperandUseDefault(OpIdx, Verify);
  }

public:
  /// Return the operand number corresponding to \p Use.
  /// \param Use Operand use edge.
  /// \Returns The operand index corresponding to \p Use.
  unsigned getUseOperandNo(const Use &Use) const final {
    return getUseOperandNoDefault(Use);
  }
  /// For isa/dyn_cast.
  /// \param From Value to test for GlobalObject.
  /// \Returns True if \p From is a \c GlobalObject.
  static bool classof(const sandboxir::Value *From) {
    switch (From->getSubclassID()) {
    case ClassID::Function:
    case ClassID::GlobalVariable:
    case ClassID::GlobalIFunc:
      return true;
    default:
      return false;
    }
  }

  /// Check if this global has a custom object file section.
  ///
  /// This is more efficient than calling getSection() and checking for an empty
  /// string.
  /// \Returns True if this global has an explicit section.
  bool hasSection() const {
    return cast<llvm::GlobalObject>(Val)->hasSection();
  }

  /// Get the custom section of this global if it has one.
  ///
  /// If this global does not have a custom section, this will be empty and the
  /// default object file section (.text, .data, etc) will be used.
  /// \Returns The explicit section name.
  StringRef getSection() const {
    return cast<llvm::GlobalObject>(Val)->getSection();
  }

  /// Change the section for this global.
  ///
  /// Setting the section to the empty string tells LLVM to choose an
  /// appropriate default object file section.
  /// \param S Section name, or empty for the default.
  LLVM_ABI void setSection(StringRef S);

  /// Return true if this global has a COMDAT.
  /// \Returns True if this global has a COMDAT.
  bool hasComdat() const { return cast<llvm::GlobalObject>(Val)->hasComdat(); }

  // TODO: implement get/setComdat(), etc. once we have a sandboxir::Comdat.

  // TODO: We currently don't support Metadata in sandboxir so all
  // Metadata-related functions are missing.

  /// Virtual call visibility kind from the underlying LLVM GlobalObject.
  using VCallVisibility = llvm::GlobalObject::VCallVisibility;

  /// Return the virtual call visibility of this global object.
  /// \Returns The vcall visibility of this global object.
  VCallVisibility getVCallVisibility() const {
    return cast<llvm::GlobalObject>(Val)->getVCallVisibility();
  }

  /// Returns true if the alignment of the value can be unilaterally
  /// increased.
  ///
  /// Note that for functions this is the alignment of the code, not the
  /// alignment of a function pointer.
  /// \Returns True if the alignment of this object may be increased.
  bool canIncreaseAlignment() const {
    return cast<llvm::GlobalObject>(Val)->canIncreaseAlignment();
  }
};

/// Mixin that adds module list iterators to SandboxIR globals.
///
/// Provides API functions like getIterator() and getReverseIterator() to
/// GlobalIFunc, Function, GlobalVariable and GlobalAlias. In LLVM IR these are
/// provided by ilist_node.
/// \Returns Mixin that adds module list iterators to SandboxIR globals.
template <typename GlobalT, typename LLVMGlobalT, typename ParentT,
          typename LLVMParentT>
class GlobalWithNodeAPI : public ParentT {
  /// Helper for mapped_iterator.
  struct LLVMGVToGV {
    Context &Ctx;
    LLVMGVToGV(Context &Ctx) : Ctx(Ctx) {}
    LLVM_ABI GlobalT &operator()(LLVMGlobalT &LLVMGV) const;
  };

public:
  /// Construct a GlobalWithNodeAPI wrapper around \p C.
  /// \param ID SandboxIR class identifier.
  /// \param C Underlying LLVM global.
  /// \param Ctx SandboxIR context.
  GlobalWithNodeAPI(Value::ClassID ID, LLVMParentT *C, Context &Ctx)
      : ParentT(ID, C, Ctx) {}

  /// Return the parent Module that contains this global.
  /// \Returns The parent module, or null if none.
  Module *getParent() const {
    llvm::Module *LLVMM = cast<LLVMGlobalT>(this->Val)->getParent();
    return this->Ctx.getModule(LLVMM);
  }

  /// Forward iterator over globals of this kind in the parent module.
  using iterator = mapped_iterator<
      decltype(static_cast<LLVMGlobalT *>(nullptr)->getIterator()), LLVMGVToGV>;
  /// Reverse iterator over globals of this kind in the parent module.
  using reverse_iterator = mapped_iterator<
      decltype(static_cast<LLVMGlobalT *>(nullptr)->getReverseIterator()),
      LLVMGVToGV>;
  /// Return an iterator to this global in its parent module list.
  /// \Returns An iterator pointing at this global in its parent module.
  iterator getIterator() const {
    auto *LLVMGV = cast<LLVMGlobalT>(this->Val);
    LLVMGVToGV ToGV(this->Ctx);
    return map_iterator(LLVMGV->getIterator(), ToGV);
  }
  /// Return a reverse iterator to this global in its parent module list.
  /// \Returns A reverse iterator pointing at this global in its parent module.
  reverse_iterator getReverseIterator() const {
    auto *LLVMGV = cast<LLVMGlobalT>(this->Val);
    LLVMGVToGV ToGV(this->Ctx);
    return map_iterator(LLVMGV->getReverseIterator(), ToGV);
  }
};

// Explicit instantiations.
/// Explicit instantiation of GlobalWithNodeAPI for GlobalIFunc.
extern template class LLVM_TEMPLATE_ABI GlobalWithNodeAPI<
    GlobalIFunc, llvm::GlobalIFunc, GlobalObject, llvm::GlobalObject>;
/// Explicit instantiation of GlobalWithNodeAPI for Function.
extern template class LLVM_TEMPLATE_ABI GlobalWithNodeAPI<
    Function, llvm::Function, GlobalObject, llvm::GlobalObject>;
/// Explicit instantiation of GlobalWithNodeAPI for GlobalVariable.
extern template class LLVM_TEMPLATE_ABI GlobalWithNodeAPI<
    GlobalVariable, llvm::GlobalVariable, GlobalObject, llvm::GlobalObject>;
/// Explicit instantiation of GlobalWithNodeAPI for GlobalAlias.
extern template class LLVM_TEMPLATE_ABI GlobalWithNodeAPI<
    GlobalAlias, llvm::GlobalAlias, GlobalValue, llvm::GlobalValue>;

/// A SandboxIR wrapper for an LLVM indirect function (ifunc).
class GlobalIFunc final
    : public GlobalWithNodeAPI<GlobalIFunc, llvm::GlobalIFunc, GlobalObject,
                               llvm::GlobalObject> {
  GlobalIFunc(llvm::GlobalObject *C, Context &Ctx)
      : GlobalWithNodeAPI(ClassID::GlobalIFunc, C, Ctx) {}
  friend class Context; // For constructor.

public:
  /// For isa/dyn_cast.
  /// \param From Value to test for GlobalIFunc.
  /// \Returns True if \p From is a \c LLVMGVToGV.
  static bool classof(const sandboxir::Value *From) {
    return From->getSubclassID() == ClassID::GlobalIFunc;
  }

  // TODO: Missing create() because we don't have a sandboxir::Module yet.

  // TODO: Missing functions: copyAttributesFrom(), removeFromParent(),
  // eraseFromParent()

  /// Set the resolver constant to \p Resolver.
  /// \param Resolver Resolver function or constant expression.
  LLVM_ABI void setResolver(Constant *Resolver);

  /// Return the resolver constant.
  /// \Returns The resolver constant.
  LLVM_ABI Constant *getResolver() const;

  /// Return the resolver function after peeling off ConstantExpr indirection.
  /// \Returns The resolver function, or null.
  LLVM_ABI Function *getResolverFunction();
  /// Return the resolver function after peeling off ConstantExpr indirection.
  /// \Returns The resolver function, or null.
  const Function *getResolverFunction() const {
    return const_cast<GlobalIFunc *>(this)->getResolverFunction();
  }

  /// Return true if \p L is a valid linkage for a global ifunc.
  /// \param L Linkage to test.
  /// \Returns True if \p L is a valid linkage for this kind of global.
  static bool isValidLinkage(LinkageTypes L) {
    return llvm::GlobalIFunc::isValidLinkage(L);
  }

  // TODO: Missing applyAlongResolverPath().

#ifndef NDEBUG
  /// Verify that this wraps an LLVM GlobalIFunc.
  void verify() const override {
    assert(isa<llvm::GlobalIFunc>(Val) && "Expected a GlobalIFunc!");
  }
  /// Dump this global ifunc to \p OS.
  /// \param OS Output stream.
  void dumpOS(raw_ostream &OS) const override {
    dumpCommonPrefix(OS);
    dumpCommonSuffix(OS);
  }
#endif
};

/// A SandboxIR wrapper for an LLVM global variable.
class GlobalVariable final
    : public GlobalWithNodeAPI<GlobalVariable, llvm::GlobalVariable,
                               GlobalObject, llvm::GlobalObject> {
  GlobalVariable(llvm::GlobalObject *C, Context &Ctx)
      : GlobalWithNodeAPI(ClassID::GlobalVariable, C, Ctx) {}
  friend class Context; // For constructor.

  /// Helper for mapped_iterator.
  struct LLVMGVToGV {
    Context &Ctx;
    LLVMGVToGV(Context &Ctx) : Ctx(Ctx) {}
    LLVM_ABI GlobalVariable &operator()(llvm::GlobalVariable &LLVMGV) const;
  };

public:
  /// For isa/dyn_cast.
  /// \param From Value to test for GlobalVariable.
  /// \Returns True if \p From is a \c LLVMGVToGV.
  static bool classof(const sandboxir::Value *From) {
    return From->getSubclassID() == ClassID::GlobalVariable;
  }

  /// Definitions have initializers, declarations don't.
  /// \Returns True if this variable has an initializer.
  inline bool hasInitializer() const {
    return cast<llvm::GlobalVariable>(Val)->hasInitializer();
  }

  /// hasDefinitiveInitializer - Whether the global variable has an initializer, and.
  ///
  /// any other instances of the global (this can happen due to weak linkage) are guaranteed to have the same initializer. Note that if you want to transform a global, you must use hasUniqueInitializer() instead, because of the *_odr linkage type. Example: @a = global SomeType* null - Initializer is both definitive and unique. @b = global weak SomeType* null - Initializer is neither definitive nor unique. @c = global weak_odr SomeType* null - Initializer is definitive, but not unique.
  /// @a = global SomeType* null - Initializer is both definitive and unique.
  /// @b = global weak SomeType* null - Initializer is neither definitive nor
  /// @c = global weak_odr SomeType* null - Initializer is definitive, but not
  /// \Returns True if this variable has a definitive initializer.
  inline bool hasDefinitiveInitializer() const {
    return cast<llvm::GlobalVariable>(Val)->hasDefinitiveInitializer();
  }

  /// hasUniqueInitializer - Whether the global variable has an initializer, and
  /// any changes made to the initializer will turn up in the final executable.
  /// \Returns True if this variable has a unique initializer.
  inline bool hasUniqueInitializer() const {
    return cast<llvm::GlobalVariable>(Val)->hasUniqueInitializer();
  }

  /// Return the initializer for this global variable.
  ///
  /// It is illegal to call this method if the global is external, because we
  /// cannot tell what the value is initialized to!
  /// \Returns The initializer constant.
  LLVM_ABI Constant *getInitializer() const;
  /// Set the initializer for this global variable.
  ///
  /// Removes any existing initializer if \p InitVal is null. The initializer
  /// must have the type getValueType().
  /// \param InitVal New initializer, or null to remove it.
  LLVM_ABI void setInitializer(Constant *InitVal);

  // TODO: Add missing replaceInitializer(). Requires special tracker

  /// If the value is a global constant, its value is immutable throughout the runtime execution of the program.
  ///
  /// Assigning a value into the constant leads to undefined behavior.
  /// \Returns True if the condition described by this query holds.
  bool isConstant() const {
    return cast<llvm::GlobalVariable>(Val)->isConstant();
  }
  /// Mark whether this global variable is a constant.
  /// \param V True if the global is a constant.
  LLVM_ABI void setConstant(bool V);

  /// Return true if this global is marked as externally initialized.
  /// \Returns True if this variable is marked externally initialized.
  bool isExternallyInitialized() const {
    return cast<llvm::GlobalVariable>(Val)->isExternallyInitialized();
  }
  /// Set whether this global is externally initialized.
  /// \param Val True if externally initialized.
  LLVM_ABI void setExternallyInitialized(bool Val);

  // TODO: Missing copyAttributesFrom()

  // TODO: Missing removeFromParent(), eraseFromParent(), dropAllReferences()

  // TODO: Missing addDebugInfo(), getDebugInfo()

  // TODO: Missing attribute setter functions: addAttribute(), setAttributes().
  //       There seems to be no removeAttribute() so we can't undo them.

  /// Return true if the attribute exists.
  /// \param Kind Attribute kind to look up.
  /// \Returns True if this global has the given attribute.
  bool hasAttribute(Attribute::AttrKind Kind) const {
    return cast<llvm::GlobalVariable>(Val)->hasAttribute(Kind);
  }

  /// Return true if the attribute exists.
  /// \param Kind Attribute name to look up.
  /// \Returns True if this global has the given attribute.
  bool hasAttribute(StringRef Kind) const {
    return cast<llvm::GlobalVariable>(Val)->hasAttribute(Kind);
  }

  /// Return true if any attributes exist.
  /// \Returns True if this global has any attributes.
  bool hasAttributes() const {
    return cast<llvm::GlobalVariable>(Val)->hasAttributes();
  }

  /// Return the attribute object.
  /// \param Kind Attribute kind to look up.
  /// \Returns The requested attribute, if present.
  Attribute getAttribute(Attribute::AttrKind Kind) const {
    return cast<llvm::GlobalVariable>(Val)->getAttribute(Kind);
  }

  /// Return the attribute object.
  /// \param Kind Attribute name to look up.
  /// \Returns The requested attribute, if present.
  Attribute getAttribute(StringRef Kind) const {
    return cast<llvm::GlobalVariable>(Val)->getAttribute(Kind);
  }

  /// Return the attribute set for this global
  /// \Returns The attribute set on this global variable.
  AttributeSet getAttributes() const {
    return cast<llvm::GlobalVariable>(Val)->getAttributes();
  }

  /// Return attribute set as list with index.
  ///
  /// FIXME: This may not be required once ValueEnumerators in bitcode-writer
  /// can enumerate attribute-set.
  /// \param Index Attribute list index.
  /// \Returns The attributes as an attribute list.
  AttributeList getAttributesAsList(unsigned Index) const {
    return cast<llvm::GlobalVariable>(Val)->getAttributesAsList(Index);
  }

  /// Check if section name is present
  /// \Returns True if this global has an implicit section.
  bool hasImplicitSection() const {
    return cast<llvm::GlobalVariable>(Val)->hasImplicitSection();
  }

  /// Get the custom code model raw value of this global.
  /// \Returns The raw code-model encoding.
  unsigned getCodeModelRaw() const {
    return cast<llvm::GlobalVariable>(Val)->getCodeModelRaw();
  }

  /// Get the custom code model of this global if it has one.
  ///
  /// If this global does not have a custom code model, the empty instance
  /// will be returned.
  /// \Returns The code model, if set.
  std::optional<CodeModel::Model> getCodeModel() const {
    return cast<llvm::GlobalVariable>(Val)->getCodeModel();
  }

  /// Returns the alignment of the given variable.
  /// \Returns The preferred alignment, if set.
  MaybeAlign getAlign() const {
    return cast<llvm::GlobalVariable>(Val)->getAlign();
  }

  // TODO: Add missing: setAligment(Align)

  /// Sets the alignment attribute of the GlobalVariable.
  ///
  /// This method will be deprecated as the alignment property should always be
  /// defined.
  /// \param Align Alignment to set.
  LLVM_ABI void setAlignment(MaybeAlign Align);

  // TODO: Missing setCodeModel(). Requires custom tracker.

#ifndef NDEBUG
  /// Verify that this wraps an LLVM GlobalVariable.
  void verify() const override {
    assert(isa<llvm::GlobalVariable>(Val) && "Expected a GlobalVariable!");
  }
  /// Dump this global variable to \p OS.
  /// \param OS Output stream.
  void dumpOS(raw_ostream &OS) const override {
    dumpCommonPrefix(OS);
    dumpCommonSuffix(OS);
  }
#endif
};

/// A SandboxIR wrapper for an LLVM global alias.
class GlobalAlias final
    : public GlobalWithNodeAPI<GlobalAlias, llvm::GlobalAlias, GlobalValue,
                               llvm::GlobalValue> {
  GlobalAlias(llvm::GlobalAlias *C, Context &Ctx)
      : GlobalWithNodeAPI(ClassID::GlobalAlias, C, Ctx) {}
  friend class Context; // For constructor.

public:
  /// For isa/dyn_cast.
  /// \param From Value to test for GlobalAlias.
  /// \Returns True if \p From is a \c LLVMGVToGV.
  static bool classof(const sandboxir::Value *From) {
    return From->getSubclassID() == ClassID::GlobalAlias;
  }

  // TODO: Missing create() due to unimplemented sandboxir::Module.

  // TODO: Missing copyAttributresFrom().
  // TODO: Missing removeFromParent(), eraseFromParent().

  /// Set the aliasee to \p Aliasee.
  /// \param Aliasee Constant that this alias refers to.
  LLVM_ABI void setAliasee(Constant *Aliasee);
  /// Return the aliasee constant.
  /// \Returns The aliasee constant.
  LLVM_ABI Constant *getAliasee() const;

  /// Return the underlying global object after stripping ConstantExpr wrappers.
  /// \Returns The underlying global object after stripping ConstantExpr wrappers.
  LLVM_ABI const GlobalObject *getAliaseeObject() const;
  /// Return the underlying global object after stripping ConstantExpr wrappers.
  /// \Returns The underlying global object after stripping ConstantExpr wrappers.
  GlobalObject *getAliaseeObject() {
    return const_cast<GlobalObject *>(
        static_cast<const GlobalAlias *>(this)->getAliaseeObject());
  }

  /// Return true if \p L is a valid linkage for a global alias.
  /// \param L Linkage to test.
  /// \Returns True if \p L is a valid linkage for this kind of global.
  static bool isValidLinkage(LinkageTypes L) {
    return llvm::GlobalAlias::isValidLinkage(L);
  }
};

/// A constant that wraps a global value without CFI instrumentation.
class NoCFIValue final : public Constant {
  NoCFIValue(llvm::NoCFIValue *C, Context &Ctx)
      : Constant(ClassID::NoCFIValue, C, Ctx) {}
  friend class Context; // For constructor.

  Use getOperandUseInternal(unsigned OpIdx, bool Verify) const final {
    return getOperandUseDefault(OpIdx, Verify);
  }

public:
  /// Return a NoCFIValue for the specified function.
  /// \param GV Global value to wrap.
  /// \Returns A NoCFIValue for the specified function.
  LLVM_ABI static NoCFIValue *get(GlobalValue *GV);

  /// Return the underlying global value.
  /// \Returns The referenced global value.
  LLVM_ABI GlobalValue *getGlobalValue() const;

  /// NoCFIValue is always a pointer.
  /// \Returns The type of this \c NoCFIValue.
  LLVM_ABI PointerType *getType() const;
  /// For isa/dyn_cast.
  /// \param From Value to test for NoCFIValue.
  /// \Returns True if \p From is a \c NoCFIValue.
  static bool classof(const sandboxir::Value *From) {
    return From->getSubclassID() == ClassID::NoCFIValue;
  }

  /// Return the operand number corresponding to \p Use.
  /// \param Use Operand use edge.
  /// \Returns The operand index corresponding to \p Use.
  unsigned getUseOperandNo(const Use &Use) const final {
    return getUseOperandNoDefault(Use);
  }

#ifndef NDEBUG
  /// Verify that this wraps an LLVM NoCFIValue.
  void verify() const override {
    assert(isa<llvm::NoCFIValue>(Val) && "Expected a NoCFIValue!");
  }
  /// Dump this constant to \p OS.
  /// \param OS Output stream.
  void dumpOS(raw_ostream &OS) const override {
    dumpCommonPrefix(OS);
    dumpCommonSuffix(OS);
  }
#endif
};

/// A constant that represents a pointer authenticated (signed) pointer value.
class ConstantPtrAuth final : public Constant {
  ConstantPtrAuth(llvm::ConstantPtrAuth *C, Context &Ctx)
      : Constant(ClassID::ConstantPtrAuth, C, Ctx) {}
  friend class Context; // For constructor.

public:
  /// Return a pointer signed with the specified parameters.
  /// \param Ptr Pointer to sign.
  /// \param Key Ptrauth key ID (i32 constant).
  /// \param Disc Integer discriminator (i64 constant, or 0).
  /// \param AddrDisc Address discriminator, or null constant.
  /// \param DeactivationSymbol Optional deactivation symbol.
  /// \Returns A pointer signed with the specified parameters.
  LLVM_ABI static ConstantPtrAuth *get(Constant *Ptr, ConstantInt *Key,
                                       ConstantInt *Disc, Constant *AddrDisc,
                                       Constant *DeactivationSymbol);
  /// The pointer that is signed in this ptrauth signed pointer.
  /// \Returns The pointer being signed.
  LLVM_ABI Constant *getPointer() const;

  /// The Key ID, an i32 constant.
  /// \Returns The pointer-authentication key constant.
  LLVM_ABI ConstantInt *getKey() const;

  /// The integer discriminator, an i64 constant, or 0.
  /// \Returns The integer discriminator constant.
  LLVM_ABI ConstantInt *getDiscriminator() const;

  /// Return the address discriminator, or the null constant if none.
  ///
  /// If present, this must be a value equivalent to the storage location of
  /// the only global-initializer user of the ptrauth signed pointer.
  /// \Returns The address discriminator constant, or null.
  LLVM_ABI Constant *getAddrDiscriminator() const;

  /// Return the deactivation symbol for this ptrauth constant.
  /// \Returns The deactivation symbol constant.
  LLVM_ABI Constant *getDeactivationSymbol() const;

  /// Whether there is any non-null address discriminator.
  /// \Returns True if a non-null address discriminator is present.
  bool hasAddressDiscriminator() const {
    return cast<llvm::ConstantPtrAuth>(Val)->hasAddressDiscriminator();
  }

  /// Whether the address uses a special address discriminator.
  ///
  /// These discriminators can't be used in real pointer-auth values; they can
  /// only be used in "prototype" values that indicate how some real schema is
  /// supposed to be produced.
  /// \param Value Special discriminator value to test for.
  /// \Returns True if the address uses the given special discriminator.
  bool hasSpecialAddressDiscriminator(uint64_t Value) const {
    return cast<llvm::ConstantPtrAuth>(Val)->hasSpecialAddressDiscriminator(
        Value);
  }

  /// Check whether authentication with \p Key and \p Discriminator is compatible.
  ///
  /// (Possibly blended) discriminator \p Discriminator is known to be
  /// compatible with this ptrauth signed pointer.
  /// \param Key Authentication key.
  /// \param Discriminator Discriminator value (possibly blended).
  /// \param DL Data layout used for the compatibility check.
  /// \Returns True if this pointer-auth constant is known compatible with the given key and discriminator.
  bool isKnownCompatibleWith(const Value *Key, const Value *Discriminator,
                             const DataLayout &DL) const {
    return cast<llvm::ConstantPtrAuth>(Val)->isKnownCompatibleWith(
        Key->Val, Discriminator->Val, DL);
  }

  /// Produce a new ptrauth expression signing \p Pointer with this schema.
  /// \param Pointer Value to sign with the same schema.
  /// \Returns A pointer-auth constant with the same schema and a new pointer.
  LLVM_ABI ConstantPtrAuth *getWithSameSchema(Constant *Pointer) const;

  /// For isa/dyn_cast.
  /// \param From Value to test for ConstantPtrAuth.
  /// \Returns True if \p From is a \c ConstantPtrAuth.
  static bool classof(const sandboxir::Value *From) {
    return From->getSubclassID() == ClassID::ConstantPtrAuth;
  }
};

/// A constant expression computed from other constants.
class ConstantExpr : public Constant {
  ConstantExpr(llvm::ConstantExpr *C, Context &Ctx)
      : Constant(ClassID::ConstantExpr, C, Ctx) {}
  friend class Context; // For constructor.

public:
  /// For isa/dyn_cast.
  /// \param From Value to test for ConstantExpr.
  /// \Returns True if \p From is a \c ConstantExpr.
  static bool classof(const sandboxir::Value *From) {
    return From->getSubclassID() == ClassID::ConstantExpr;
  }
  // TODO: Missing functions.
};

/// A constant that refers to the address of a basic block.
class BlockAddress final : public Constant {
  BlockAddress(llvm::BlockAddress *C, Context &Ctx)
      : Constant(ClassID::BlockAddress, C, Ctx) {}
  friend class Context; // For constructor.

public:
  /// Return a BlockAddress for the specified function and basic block.
  /// \param F Function that contains \p BB.
  /// \param BB Basic block whose address is taken.
  /// \Returns A BlockAddress for the specified function and basic block.
  LLVM_ABI static BlockAddress *get(Function *F, BasicBlock *BB);

  /// Return a BlockAddress for the specified basic block.
  ///
  /// The basic block must be embedded into a function.
  /// \param BB Basic block whose address is taken.
  /// \Returns A \c BlockAddress for the given arguments.
  LLVM_ABI static BlockAddress *get(BasicBlock *BB);

  /// Lookup an existing \c BlockAddress constant for the given BasicBlock.
  ///
  /// \param BB Basic block to look up.
  /// \returns 0 if \c !BB->hasAddressTaken(), otherwise the \c BlockAddress.
  LLVM_ABI static BlockAddress *lookup(const BasicBlock *BB);

  /// Return the function that contains this block address.
  /// \Returns The function containing the basic block.
  LLVM_ABI Function *getFunction() const;
  /// Return the basic block referred to by this constant.
  /// \Returns The referenced basic block.
  LLVM_ABI BasicBlock *getBasicBlock() const;

  /// For isa/dyn_cast.
  /// \param From Value to test for BlockAddress.
  /// \Returns True if \p From is a \c BlockAddress.
  static bool classof(const sandboxir::Value *From) {
    return From->getSubclassID() == ClassID::BlockAddress;
  }
};

/// A wrapper representing a dso_local_equivalent constant for a global value.
class DSOLocalEquivalent final : public Constant {
  DSOLocalEquivalent(llvm::DSOLocalEquivalent *C, Context &Ctx)
      : Constant(ClassID::DSOLocalEquivalent, C, Ctx) {}
  friend class Context; // For constructor.

public:
  /// Return a DSOLocalEquivalent for the specified global value.
  /// \param GV Global value to wrap.
  /// \Returns A DSOLocalEquivalent for the specified global value.
  LLVM_ABI static DSOLocalEquivalent *get(GlobalValue *GV);

  /// Return the underlying global value.
  /// \Returns The referenced global value.
  LLVM_ABI GlobalValue *getGlobalValue() const;

  /// For isa/dyn_cast.
  /// \param From Value to test for DSOLocalEquivalent.
  /// \Returns True if \p From is a \c DSOLocalEquivalent.
  static bool classof(const sandboxir::Value *From) {
    return From->getSubclassID() == ClassID::DSOLocalEquivalent;
  }

  /// Return the operand index for \p Use. Unreachable: this constant has no operands.
  /// \param Use Operand use edge (unused).
  /// \Returns The operand index corresponding to \p Use.
  unsigned getUseOperandNo(const Use &Use) const final {
    llvm_unreachable("DSOLocalEquivalent has no operands!");
  }

#ifndef NDEBUG
  /// Verify that this wraps an LLVM DSOLocalEquivalent.
  void verify() const override {
    assert(isa<llvm::DSOLocalEquivalent>(Val) &&
           "Expected a DSOLocalEquivalent!");
  }
  /// Dump this constant to \p OS.
  /// \param OS Output stream.
  void dumpOS(raw_ostream &OS) const override {
    dumpCommonPrefix(OS);
    dumpCommonSuffix(OS);
  }
#endif
};

// TODO: This should inherit from ConstantData.
/// A constant token with no value, used for untyped token operands.
class ConstantTokenNone final : public Constant {
  ConstantTokenNone(llvm::ConstantTokenNone *C, Context &Ctx)
      : Constant(ClassID::ConstantTokenNone, C, Ctx) {}
  friend class Context; // For constructor.

public:
  /// Return the ConstantTokenNone singleton for \p Ctx.
  /// \param Ctx SandboxIR context that owns the constant.
  /// \Returns The ConstantTokenNone singleton for \p Ctx.
  LLVM_ABI static ConstantTokenNone *get(Context &Ctx);

  /// For isa/dyn_cast.
  /// \param From Value to test for ConstantTokenNone.
  /// \Returns True if \p From is a \c ConstantTokenNone.
  static bool classof(const sandboxir::Value *From) {
    return From->getSubclassID() == ClassID::ConstantTokenNone;
  }

  /// Return the operand index for \p Use. Unreachable: this constant has no operands.
  /// \param Use Operand use edge (unused).
  /// \Returns The operand index corresponding to \p Use.
  unsigned getUseOperandNo(const Use &Use) const final {
    llvm_unreachable("ConstantTokenNone has no operands!");
  }

#ifndef NDEBUG
  /// Verify that this wraps an LLVM ConstantTokenNone.
  void verify() const override {
    assert(isa<llvm::ConstantTokenNone>(Val) &&
           "Expected a ConstantTokenNone!");
  }
  /// Dump this constant to \p OS.
  /// \param OS Output stream.
  void dumpOS(raw_ostream &OS) const override {
    dumpCommonPrefix(OS);
    dumpCommonSuffix(OS);
  }
#endif
};

} // namespace llvm::sandboxir

#endif // LLVM_SANDBOXIR_CONSTANT_H
