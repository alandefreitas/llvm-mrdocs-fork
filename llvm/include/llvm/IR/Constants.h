//===-- llvm/Constants.h - Constant class subclass definitions --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// @file
/// This file contains the declarations for the subclasses of Constant,
/// which represent the different flavors of constant values that live in LLVM.
/// Note that Constants are immutable (once created they never change) and are
/// fully shared by structural equivalence.  This means that two structurally
/// equivalent constants will always have the same address.  Constants are
/// created on demand as needed and never deleted: thus clients don't have to
/// worry about the lifetime of the objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_CONSTANTS_H
#define LLVM_IR_CONSTANTS_H

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GEPNoWrapFlags.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/OperandTraits.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace llvm {

/// Key type used to unique aggregate constants in the constant pool.
template <class ConstantClass> struct ConstantAggrKeyType;
/// Key type used to unique ConstantExpr values in the constant pool.
struct ConstantExprKeyType;
/// Key type used to unique ConstantPtrAuth values in the constant pool.
struct ConstantPtrAuthKeyType;

/// Base class for constants with no operands.
///
/// These constants have no operands; they represent their data directly.
/// Since they can be in use by unrelated modules (and are never based on
/// GlobalValues), it never makes sense to RAUW them.
///
/// These do not have use lists. It is illegal to inspect the uses. These behave
/// as if they have no uses (i.e. use_empty() is always true).
class ConstantData : public Constant {
  constexpr static IntrusiveOperandsAllocMarker AllocMarker{0};

  friend class Constant;

  Value *handleOperandChangeImpl(Value *From, Value *To) {
    llvm_unreachable("Constant data does not have operands!");
  }

protected:
  /// Construct a constant-data value of type \p Ty with subclass ID \p VT.
  /// \param Ty The type of the constant.
  /// \param VT The ValueTy subclass identifier.
  explicit ConstantData(Type *Ty, ValueTy VT) : Constant(Ty, VT, AllocMarker) {}

  /// Allocate a ConstantData with the ordinary global allocator.
  /// \param S Allocation size in bytes.
  /// @return A pointer to the allocated storage.
  void *operator new(size_t S) { return ::operator new(S); }

public:
  /// Deallocate a ConstantData allocated with \c operator new.
  /// \param Ptr Pointer returned by \c operator new.
  void operator delete(void *Ptr) { ::operator delete(Ptr); }

  /// ConstantData values are uniqued and cannot be copied.
  /// \param Other The constant that would be copied (deleted).
  ConstantData(const ConstantData &Other) = delete;

  /// Methods to support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a \c ConstantData.
  static bool classof(const Value *V) {
    static_assert(Value::ConstantDataFirstVal == 0,
                  "V->getValueID() >= Value::ConstantDataFirstVal");
    return V->getValueID() <= ConstantDataLastVal;
  }
};

//===----------------------------------------------------------------------===//
/// This is the shared class of boolean and integer constants. This class
/// represents both boolean and integral constants.
/// Class for constant integers.
class ConstantInt final : public ConstantData {
  friend class Constant;
  friend class ConstantVector;

  APInt Val;

  ConstantInt(Type *Ty, const APInt &V);

  void destroyConstantImpl();

  /// Return a ConstantInt with the specified value and an implied Type. The
  /// type is the vector type whose integer element type corresponds to the bit
  /// width of the value.
  /// @return A ConstantInt with the specified value and an implied Type.
  static ConstantInt *get(LLVMContext &Context, ElementCount EC,
                          const APInt &V);

public:
  /// ConstantInt values are uniqued and cannot be copied.
  /// \param Other The constant that would be copied (deleted).
  ConstantInt(const ConstantInt &Other) = delete;

  /// Return the i1 true constant for \p Context.
  /// \param Context The LLVM context that owns the constant.
  /// @return The i1 true constant for \p Context.
  LLVM_ABI static ConstantInt *getTrue(LLVMContext &Context);
  /// Return the i1 false constant for \p Context.
  /// \param Context The LLVM context that owns the constant.
  /// @return The i1 false constant for \p Context.
  LLVM_ABI static ConstantInt *getFalse(LLVMContext &Context);
  /// Return an i1 constant for boolean \p V in \p Context.
  /// \param Context The LLVM context that owns the constant.
  /// \param V The boolean value.
  /// @return An i1 constant for boolean \p V in \p Context.
  LLVM_ABI static ConstantInt *getBool(LLVMContext &Context, bool V);
  /// Return true as a constant of type \p Ty (scalar or vector splat).
  /// \param Ty The boolean type (i1 or vector of i1).
  /// @return A true constant of type \p Ty (scalar or vector splat).
  LLVM_ABI static Constant *getTrue(Type *Ty);
  /// Return false as a constant of type \p Ty (scalar or vector splat).
  /// \param Ty The boolean type (i1 or vector of i1).
  /// @return A false constant of type \p Ty (scalar or vector splat).
  LLVM_ABI static Constant *getFalse(Type *Ty);
  /// Return \p V as a boolean constant of type \p Ty (scalar or vector splat).
  /// \param Ty The boolean type (i1 or vector of i1).
  /// \param V The boolean value.
  /// @return \p V as a boolean constant of type \p Ty (scalar or vector splat).
  LLVM_ABI static Constant *getBool(Type *Ty, bool V);

  /// If Ty is a vector type, return a Constant with a splat of the given
  /// value. Otherwise return a ConstantInt for the given value.
  /// \param Ty The type of the constant (integer or vector of integer).
  /// \param V The integer value.
  /// \param IsSigned Whether to treat \p V as signed when extending.
  /// \param ImplicitTrunc Whether to allow implicit truncation of the value.
  /// @return A Constant with a splat of the given value.
  LLVM_ABI static Constant *get(Type *Ty, uint64_t V, bool IsSigned = false,
                                bool ImplicitTrunc = false);

  /// Return a ConstantInt with the specified integer value for the specified type.
  ///
  /// If the type is wider than 64 bits, the value will be zero-extended
  /// to fit the type, unless IsSigned is true, in which case the value will
  /// be interpreted as a 64-bit signed integer and sign-extended to fit
  /// the type.
  /// \param Ty The integer type of the constant.
  /// \param V The integer value.
  /// \param IsSigned Whether to treat \p V as signed when extending.
  /// \param ImplicitTrunc Whether to allow implicit truncation of the value.
  /// @return A ConstantInt with the specified integer value for the specified type.
  LLVM_ABI static ConstantInt *get(IntegerType *Ty, uint64_t V,
                                   bool IsSigned = false,
                                   bool ImplicitTrunc = false);

  /// Get a ConstantInt for a specific signed value.
  ///
  /// The value \p V will be canonicalized to an unsigned APInt. Accessing it
  /// with either getSExtValue() or getZExtValue() will yield a correctly sized
  /// and signed value for the type \p Ty.
  /// \param Ty The integer type of the constant.
  /// \param V The signed integer value.
  /// \param ImplicitTrunc Whether to allow implicit truncation of the value.
  /// @return A ConstantInt for a specific signed value.
  static ConstantInt *getSigned(IntegerType *Ty, int64_t V,
                                bool ImplicitTrunc = false) {
    return get(Ty, V, /*IsSigned=*/true, ImplicitTrunc);
  }
  /// Get a signed ConstantInt, or a vector splat, for type \p Ty.
  /// \param Ty The type of the constant (integer or vector of integer).
  /// \param V The signed integer value.
  /// \param ImplicitTrunc Whether to allow implicit truncation of the value.
  /// @return A signed ConstantInt, or a vector splat, for type \p Ty.
  static Constant *getSigned(Type *Ty, int64_t V, bool ImplicitTrunc = false) {
    return get(Ty, V, /*IsSigned=*/true, ImplicitTrunc);
  }

  /// Return a ConstantInt with the specified value and an implied Type.
  ///
  /// The type is the integer type that corresponds to the bit width of the value.
  /// \param Context The LLVM context that owns the constant.
  /// \param V The arbitrary-precision integer value.
  /// @return A ConstantInt with the specified value and an implied Type.
  LLVM_ABI static ConstantInt *get(LLVMContext &Context, const APInt &V);

  /// Return a ConstantInt constructed from the string \p Str with the given radix.
  /// \param Ty The integer type of the constant.
  /// \param Str The textual representation of the value.
  /// \param Radix The digit radix of \p Str.
  /// @return A ConstantInt constructed from the string \p Str with the given radix.
  LLVM_ABI static ConstantInt *get(IntegerType *Ty, StringRef Str,
                                   uint8_t Radix);

  /// If Ty is a vector type, return a Constant with a splat of the given
  /// value. Otherwise return a ConstantInt for the given value.
  /// \param Ty The type of the constant (integer or vector of integer).
  /// \param V The arbitrary-precision integer value.
  /// @return A Constant with a splat of the given value.
  LLVM_ABI static Constant *get(Type *Ty, const APInt &V);

  /// Return the constant as an APInt value reference. This allows clients to
  /// obtain a full-precision copy of the value.
  /// Return the constant's value.
  /// @return The constant's value.
  inline const APInt &getValue() const { return Val; }

  /// getBitWidth - Return the scalar bitwidth of this constant.
  /// @return The scalar bit width of this constant.
  unsigned getBitWidth() const { return Val.getBitWidth(); }

  /// Return the zero-extended value as a 64-bit unsigned integer.
  ///
  /// The value is zero extended as appropriate for the type of this constant.
  /// Note that this method can assert if the value does not fit in 64 bits.
  /// @return The zero-extended value as a 64-bit unsigned integer.
  inline uint64_t getZExtValue() const { return Val.getZExtValue(); }

  /// Return the sign-extended value as a 64-bit signed integer.
  ///
  /// The value is sign extended as appropriate for the type of this constant.
  /// Note that this method can assert if the value does not fit in 64 bits.
  /// @return The sign-extended value as a 64-bit signed integer.
  inline int64_t getSExtValue() const { return Val.getSExtValue(); }

  /// Return the constant as an llvm::MaybeAlign.
  /// Note that this method can assert if the value does not fit in 64 bits or
  /// is not a power of two.
  /// @return The value as an \c llvm::MaybeAlign.
  inline MaybeAlign getMaybeAlignValue() const {
    return MaybeAlign(getZExtValue());
  }

  /// Return the constant as an llvm::Align, interpreting `0` as `Align(1)`.
  ///
  /// Note that this method can assert if the value does not fit in 64 bits or
  /// is not a power of two.
  /// @return The value as an \c llvm::Align.
  inline Align getAlignValue() const {
    return getMaybeAlignValue().valueOrOne();
  }

  /// Determine if this constant's value is the same as unsigned integer \p V.
  ///
  /// This only works for very small values, because this is all that can be
  /// represented with all types.
  /// \param V The unsigned integer to compare against.
  /// @return True if this constant equals \p V.
  bool equalsInt(uint64_t V) const { return Val == V; }

  /// Variant of the getType() method to always return an IntegerType, which
  /// reduces the amount of casting needed in parts of the compiler.
  /// @return This constant's type as an \c IntegerType.
  inline IntegerType *getIntegerType() const {
    return cast<IntegerType>(Value::getType());
  }

  /// Determine if the value is in range for the given type.
  ///
  /// This static method returns true if the type \p Ty is big enough to
  /// represent the value \p V. This can be used to avoid having the get method
  /// assert when \p V is larger than \p Ty can represent. Note that there are
  /// two versions of this method, one for unsigned and one for signed integers.
  /// Although ConstantInt canonicalizes everything to an unsigned integer,
  /// the signed version avoids callers having to convert a signed quantity
  /// to the appropriate unsigned type before calling the method.
  /// \param Ty The integer type to test capacity for.
  /// \param V The value to represent.
  /// \returns true if \p V is a valid value for type \p Ty
  LLVM_ABI static bool isValueValidForType(Type *Ty, uint64_t V);
  /// Determine if the signed value is in range for the given type.
  /// \param Ty The integer type to test capacity for.
  /// \param V The signed value to represent.
  /// @return True if \p V fits in type \p Ty.
  LLVM_ABI static bool isValueValidForType(Type *Ty, int64_t V);

  /// Return true if this constant is negative.
  /// @return True if this constant is negative.
  bool isNegative() const { return Val.isNegative(); }

  /// Return true if this constant is zero.
  ///
  /// This is a convenience method to make client code smaller for a common
  /// case. It also correctly performs the comparison without the potential for
  /// an assertion from getZExtValue().
  /// @return True if this constant is zero.
  bool isZero() const { return isNullValue(); }

  /// Determine if the value is one.
  ///
  /// This is a convenience method to make client code smaller for a common
  /// case. It also correctly performs the comparison without the potential for
  /// an assertion from getZExtValue().
  /// @return True if the value is one.
  bool isOne() const { return Val.isOne(); }

  /// This function will return true iff every bit in this constant is set
  /// to true.
  /// @returns true iff this constant's bits are all set to true.
  /// Determine if the value is all ones.
  bool isMinusOne() const { return Val.isAllOnes(); }

  /// This function will return true iff this constant represents the largest
  /// value that may be represented by the constant's type.
  /// @returns true iff this is the largest value that may be represented
  /// by this type.
  /// Determine if the value is maximal.
  /// \param IsSigned Whether to interpret the type as signed.
  bool isMaxValue(bool IsSigned) const {
    if (IsSigned)
      return Val.isMaxSignedValue();
    else
      return Val.isMaxValue();
  }

  /// Determine if the value is minimal.
  ///
  /// This function will return true iff this constant represents the smallest
  /// value that may be represented by this constant's type.
  /// \param IsSigned Whether to interpret the type as signed.
  /// \returns true if this is the smallest value that may be represented by
  /// this type.
  bool isMinValue(bool IsSigned) const {
    if (IsSigned)
      return Val.isMinSignedValue();
    else
      return Val.isMinValue();
  }

  /// Determine if the value is greater or equal to the given number.
  ///
  /// This function will return true iff this constant represents a value with
  /// active bits bigger than 64 bits or a value greater than the given uint64_t
  /// value.
  /// \param Num The unsigned threshold to compare against.
  /// \returns true iff this constant is greater or equal to the given number.
  bool uge(uint64_t Num) const { return Val.uge(Num); }

  /// Get the constant's value with a saturation limit.
  ///
  /// If the value is smaller than the specified limit, return it, otherwise
  /// return the limit value. This causes the value to saturate to the limit.
  /// \param Limit The saturation ceiling.
  /// \returns the min of the value of the constant and the specified value
  uint64_t getLimitedValue(uint64_t Limit = ~0ULL) const {
    return Val.getLimitedValue(Limit);
  }

  /// Methods to support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a \c ConstantInt.
  static bool classof(const Value *V) {
    return V->getValueID() == ConstantIntVal;
  }
};

//===----------------------------------------------------------------------===//
/// Class for constant bytes.
class ConstantByte final : public ConstantData {
  friend class Constant;
  friend class ConstantVector;

  APInt Val;

  ConstantByte(Type *Ty, const APInt &V);

  void destroyConstantImpl();

  /// Return a ConstantByte with the specified value and an implied Type. The
  /// type is the vector type whose byte element type corresponds to the bit
  /// width of the value.
  /// @return A ConstantByte with the specified value and an implied Type.
  static ConstantByte *get(LLVMContext &Context, ElementCount EC,
                           const APInt &V);

public:
  /// ConstantByte values are uniqued and cannot be copied.
  /// \param Other The constant that would be copied (deleted).
  ConstantByte(const ConstantByte &Other) = delete;

  /// If Ty is a vector type, return a Constant with a splat of the given
  /// value. Otherwise return a ConstantByte for the given value.
  /// \param Ty The type of the constant (byte or vector of byte).
  /// \param V The byte value.
  /// \param isSigned Whether to treat \p V as signed when extending.
  /// \param ImplicitTrunc Whether to allow implicit truncation of the value.
  /// @return A Constant with a splat of the given value.
  LLVM_ABI static Constant *get(Type *Ty, uint64_t V, bool isSigned = false,
                                bool ImplicitTrunc = false);

  /// Return a ConstantByte with the specified byte value for the specified type.
  ///
  /// If the type is wider than 64 bits, the value will be zero-extended
  /// to fit the type, unless IsSigned is true, in which case the value will
  /// be interpreted as a 64-bit signed byte and sign-extended to fit
  /// the type.
  /// \param Ty The byte type of the constant.
  /// \param V The byte value.
  /// \param isSigned Whether to treat \p V as signed when extending.
  /// \param ImplicitTrunc Whether to allow implicit truncation of the value.
  /// @return A ConstantByte with the specified byte value for the specified type.
  LLVM_ABI static ConstantByte *get(ByteType *Ty, uint64_t V,
                                    bool isSigned = false,
                                    bool ImplicitTrunc = false);

  /// Get a ConstantByte for a specific signed value.
  ///
  /// The value \p V will be canonicalized to an unsigned APInt. Accessing it
  /// with either getSExtValue() or getZExtValue() will yield a correctly sized
  /// and signed value for the type \p Ty.
  /// \param Ty The byte type of the constant.
  /// \param V The signed byte value.
  /// \param ImplicitTrunc Whether to allow implicit truncation of the value.
  /// @return A ConstantByte for a specific signed value.
  static ConstantByte *getSigned(ByteType *Ty, int64_t V,
                                 bool ImplicitTrunc = false) {
    return get(Ty, V, /*IsSigned=*/true, ImplicitTrunc);
  }
  /// Get a signed ConstantByte, or a vector splat, for type \p Ty.
  /// \param Ty The type of the constant (byte or vector of byte).
  /// \param V The signed byte value.
  /// \param ImplicitTrunc Whether to allow implicit truncation of the value.
  /// @return A signed ConstantByte, or a vector splat, for type \p Ty.
  static Constant *getSigned(Type *Ty, int64_t V, bool ImplicitTrunc = false) {
    return get(Ty, V, /*IsSigned=*/true, ImplicitTrunc);
  }

  /// Return a ConstantByte with the specified value and an implied Type.
  ///
  /// The type is the byte type that corresponds to the bit width of the value.
  /// \param Context The LLVM context that owns the constant.
  /// \param V The arbitrary-precision integer value.
  /// @return A ConstantByte with the specified value and an implied Type.
  LLVM_ABI static ConstantByte *get(LLVMContext &Context, const APInt &V);

  /// Return a ConstantByte constructed from the string \p Str with the given radix.
  /// \param Ty The byte type of the constant.
  /// \param Str The textual representation of the value.
  /// \param Radix The digit radix of \p Str.
  /// @return A ConstantByte constructed from the string \p Str with the given radix.
  LLVM_ABI static ConstantByte *get(ByteType *Ty, StringRef Str, uint8_t Radix);

  /// If Ty is a vector type, return a Constant with a splat of the given
  /// value. Otherwise return a ConstantByte for the given value.
  /// \param Ty The type of the constant (byte or vector of byte).
  /// \param V The arbitrary-precision integer value.
  /// @return A Constant with a splat of the given value.
  LLVM_ABI static Constant *get(Type *Ty, const APInt &V);

  /// Return the constant as an APInt value reference. This allows clients to
  /// obtain a full-precision copy of the value.
  /// Return the constant's value.
  /// @return The constant's value.
  inline const APInt &getValue() const { return Val; }

  /// getBitWidth - Return the scalar bitwidth of this constant.
  /// @return The scalar bit width of this constant.
  unsigned getBitWidth() const { return Val.getBitWidth(); }

  /// Return the zero-extended value as a 64-bit unsigned integer.
  ///
  /// The value is zero extended as appropriate for the type of this constant.
  /// Note that this method can assert if the value does not fit in 64 bits.
  /// @return The zero-extended value as a 64-bit unsigned integer.
  inline uint64_t getZExtValue() const { return Val.getZExtValue(); }

  /// Return the sign-extended value as a 64-bit signed integer.
  ///
  /// The value is sign extended as appropriate for the type of this constant.
  /// Note that this method can assert if the value does not fit in 64 bits.
  /// @return The sign-extended value as a 64-bit signed integer.
  inline int64_t getSExtValue() const { return Val.getSExtValue(); }

  /// Variant of the getType() method to always return a ByteType, which
  /// reduces the amount of casting needed in parts of the compiler.
  /// @return This constant's type as a \c ByteType.
  inline ByteType *getByteType() const {
    return cast<ByteType>(Value::getType());
  }

  /// Return true if this constant is negative.
  /// @return True if this constant is negative.
  bool isNegative() const { return Val.isNegative(); }

  /// Return true if this constant is zero.
  ///
  /// This is a convenience method to make client code smaller for a common
  /// case. It also correctly performs the comparison without the potential for
  /// an assertion from getZExtValue().
  /// @return True if this constant is zero.
  bool isZero() const { return Val.isZero(); }

  /// Determine if the value is one.
  ///
  /// This is a convenience method to make client code smaller for a common
  /// case. It also correctly performs the comparison without the potential for
  /// an assertion from getZExtValue().
  /// @return True if the value is one.
  bool isOne() const { return Val.isOne(); }

  /// This function will return true iff every bit in this constant is set
  /// to true.
  /// @returns true iff this constant's bits are all set to true.
  /// Determine if the value is all ones.
  bool isMinusOne() const { return Val.isAllOnes(); }

  /// This function will return true iff this constant represents the largest
  /// value that may be represented by the constant's type.
  /// @returns true iff this is the largest value that may be represented
  /// by this type.
  /// Determine if the value is maximal.
  /// \param IsSigned Whether to interpret the type as signed.
  bool isMaxValue(bool IsSigned) const {
    if (IsSigned)
      return Val.isMaxSignedValue();
    else
      return Val.isMaxValue();
  }

  /// Determine if the value is minimal.
  ///
  /// This function will return true iff this constant represents the smallest
  /// value that may be represented by this constant's type.
  /// \param IsSigned Whether to interpret the type as signed.
  /// \returns true if this is the smallest value that may be represented by
  /// this type.
  bool isMinValue(bool IsSigned) const {
    if (IsSigned)
      return Val.isMinSignedValue();
    else
      return Val.isMinValue();
  }

  /// Methods to support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a \c ConstantByte.
  static bool classof(const Value *V) {
    return V->getValueID() == ConstantByteVal;
  }
};

//===----------------------------------------------------------------------===//
/// ConstantFP - Floating Point Values [float, double]
///
class ConstantFP final : public ConstantData {
  friend class Constant;
  friend class ConstantVector;

  APFloat Val;

  ConstantFP(Type *Ty, const APFloat &V);

  void destroyConstantImpl();

  /// Return a ConstantFP with the specified value and an implied Type. The
  /// type is the vector type whose element type has the same floating point
  /// semantics as the value.
  /// @return A ConstantFP with the specified value and an implied Type.
  static ConstantFP *get(LLVMContext &Context, ElementCount EC,
                         const APFloat &V);

public:
  /// ConstantFP values are uniqued and cannot be copied.
  /// \param Other The constant that would be copied (deleted).
  ConstantFP(const ConstantFP &Other) = delete;

  /// Return a ConstantFP (or vector splat) for a simple host double value.
  ///
  /// This should only be used for simple constant values like 2.0/1.0 etc, that
  /// are known-valid both as host double and as the target format.
  /// \param Ty The floating-point type (or vector thereof).
  /// \param V The host double value to convert.
  /// @return A ConstantFP (or vector splat) for a simple host double value.
  LLVM_ABI static ConstantFP *get(Type *Ty, double V);

  /// If Ty is a vector type, return a Constant with a splat of the given
  /// value. Otherwise return a ConstantFP for the given value.
  /// \param Ty The floating-point type (or vector thereof).
  /// \param V The floating-point value.
  /// @return A Constant with a splat of the given value.
  LLVM_ABI static ConstantFP *get(Type *Ty, const APFloat &V);

  /// Return a ConstantFP parsed from string \p Str for type \p Ty.
  /// \param Ty The floating-point type.
  /// \param Str The textual representation of the value.
  /// @return A ConstantFP parsed from string \p Str for type \p Ty.
  LLVM_ABI static ConstantFP *get(Type *Ty, StringRef Str);
  /// Return a ConstantFP for \p V, inferring the type from \p V's semantics.
  /// \param Context The LLVM context that owns the constant.
  /// \param V The floating-point value.
  /// @return A ConstantFP for \p V, inferring the type from \p V's semantics.
  LLVM_ABI static ConstantFP *get(LLVMContext &Context, const APFloat &V);
  /// Return a NaN constant of type \p Ty.
  /// \param Ty The floating-point type.
  /// \param Negative Whether the NaN should have a negative sign bit.
  /// \param Payload Optional NaN payload bits.
  /// @return A NaN constant of type \p Ty.
  LLVM_ABI static ConstantFP *getNaN(Type *Ty, bool Negative = false,
                                     uint64_t Payload = 0);
  /// Return a quiet NaN constant of type \p Ty.
  /// \param Ty The floating-point type.
  /// \param Negative Whether the NaN should have a negative sign bit.
  /// \param Payload Optional NaN payload bits (may be null).
  /// @return A quiet NaN constant of type \p Ty.
  LLVM_ABI static ConstantFP *getQNaN(Type *Ty, bool Negative = false,
                                      APInt *Payload = nullptr);
  /// Return a signaling NaN constant of type \p Ty.
  /// \param Ty The floating-point type.
  /// \param Negative Whether the NaN should have a negative sign bit.
  /// \param Payload Optional NaN payload bits (may be null).
  /// @return A signaling NaN constant of type \p Ty.
  LLVM_ABI static ConstantFP *getSNaN(Type *Ty, bool Negative = false,
                                      APInt *Payload = nullptr);
  /// Return a positive or negative zero constant of type \p Ty.
  /// \param Ty The floating-point type.
  /// \param Negative Whether to return negative zero.
  /// @return A positive or negative zero constant of type \p Ty.
  LLVM_ABI static ConstantFP *getZero(Type *Ty, bool Negative = false);
  /// Return a negative zero constant of type \p Ty.
  /// \param Ty The floating-point type.
  /// @return A negative zero constant of type \p Ty.
  static ConstantFP *getNegativeZero(Type *Ty) { return getZero(Ty, true); }
  /// Return a positive or negative infinity constant of type \p Ty.
  /// \param Ty The floating-point type.
  /// \param Negative Whether to return negative infinity.
  /// @return A positive or negative infinity constant of type \p Ty.
  LLVM_ABI static ConstantFP *getInfinity(Type *Ty, bool Negative = false);

  /// Return true if Ty is big enough to represent V.
  /// \param Ty The floating-point type to test.
  /// \param V The value to represent.
  /// @return True if \p V fits in type \p Ty.
  LLVM_ABI static bool isValueValidForType(Type *Ty, const APFloat &V);
  /// Return the floating-point value as an APFloat.
  /// @return The floating-point value as an \c APFloat.
  inline const APFloat &getValueAPF() const { return Val; }
  /// Return the floating-point value as an APFloat.
  /// @return The constant's value.
  inline const APFloat &getValue() const { return Val; }

  /// Return true if the value is positive or negative zero.
  /// @return True if the value is positive or negative zero.
  bool isZero() const { return Val.isZero(); }

  /// Return true if the value is positive zero.
  /// @return True if the value is positive zero.
  bool isPosZero() const { return Val.isPosZero(); }

  /// Return true if the value is negative zero.
  /// @return True if the value is negative zero.
  bool isNegZero() const { return Val.isNegZero(); }

  /// Return true if the sign bit is set.
  /// @return True if the sign bit is set.
  bool isNegative() const { return Val.isNegative(); }

  /// Return true if the value is infinity
  /// @return True if the value is infinity.
  bool isInfinity() const { return Val.isInfinity(); }

  /// Return true if the value is a NaN.
  /// @return True if the value is a NaN.
  bool isNaN() const { return Val.isNaN(); }

  /// Returns true if this value is exactly +1.0.
  /// @return True if this value is exactly +1.0.
  bool isOne() const { return Val.isOne(); }

  /// Returns true if this value is exactly -1.0.
  /// @return True if this value is exactly -1.0.
  bool isMinusOne() const { return Val.isMinusOne(); }

  /// Return true if this constant matches \p V bit-for-bit.
  ///
  /// We don't rely on operator== working on double values, as it returns true
  /// for things that are clearly not equal, like -0.0 and 0.0. As such, this
  /// method can be used to do an exact bit-for-bit comparison of two floating
  /// point values. The version with a double operand is retained because it's
  /// so convenient to write isExactlyValue(2.0), but please use it only for
  /// simple constants.
  /// \param V The APFloat value to compare against.
  /// @return True if this constant equals the given floating-point value.
  LLVM_ABI bool isExactlyValue(const APFloat &V) const;

  /// Return true if this constant matches host double \p V bit-for-bit.
  /// \param V The host double to compare against (for simple constants only).
  /// @return True if this constant equals the given floating-point value.
  bool isExactlyValue(double V) const {
    bool ignored;
    APFloat FV(V);
    FV.convert(Val.getSemantics(), APFloat::rmNearestTiesToEven, &ignored);
    return isExactlyValue(FV);
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a \c ConstantFP.
  static bool classof(const Value *V) {
    return V->getValueID() == ConstantFPVal;
  }
};

//===----------------------------------------------------------------------===//
/// All zero aggregate value
///
class ConstantAggregateZero final : public ConstantData {
  friend class Constant;

  explicit ConstantAggregateZero(Type *Ty)
      : ConstantData(Ty, ConstantAggregateZeroVal) {
    SubclassOptionalData = IsNullValue;
  }

  void destroyConstantImpl();

public:
  /// ConstantAggregateZero values are uniqued and cannot be copied.
  /// \param Other The constant that would be copied (deleted).
  ConstantAggregateZero(const ConstantAggregateZero &Other) = delete;

  /// Return an all-zero aggregate constant of type \p Ty.
  /// \param Ty The aggregate type (array, struct, or vector).
  /// @return An all-zero aggregate constant of type \p Ty.
  LLVM_ABI static ConstantAggregateZero *get(Type *Ty);

  /// If this CAZ has array or vector type, return a zero with the right element
  /// type.
  /// @return A zero constant of the element type.
  LLVM_ABI Constant *getSequentialElement() const;

  /// If this CAZ has struct type, return a zero with the right element type for
  /// the specified element.
  /// \param Elt The zero-based struct element index.
  /// @return A zero constant of the specified element type.
  LLVM_ABI Constant *getStructElement(unsigned Elt) const;

  /// Return a zero of the right value for the specified GEP index if we can,
  /// otherwise return null (e.g. if C is a ConstantExpr).
  /// \param C The GEP index constant.
  /// @return A zero of the indexed element type, or null if unavailable.
  LLVM_ABI Constant *getElementValue(Constant *C) const;

  /// Return a zero of the right value for the specified GEP index.
  /// \param Idx The zero-based element index.
  /// @return A zero of the indexed element type.
  LLVM_ABI Constant *getElementValue(unsigned Idx) const;

  /// Return the number of elements in the array, vector, or struct.
  /// @return The number of elements in this aggregate.
  LLVM_ABI ElementCount getElementCount() const;

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a \c ConstantAggregateZero.
  static bool classof(const Value *V) {
    return V->getValueID() == ConstantAggregateZeroVal;
  }
};

/// Base class for aggregate constants (with operands).
///
/// These constants are aggregates of other constants, which are stored as
/// operands.
///
/// Subclasses are \a ConstantStruct, \a ConstantArray, and \a
/// ConstantVector.
///
/// \note Some subclasses of \a ConstantData are semantically aggregates --
/// such as \a ConstantDataArray -- but are not subclasses of this because they
/// use operands.
class ConstantAggregate : public Constant {
protected:
  /// Construct an aggregate constant of type \p T with operands \p V.
  /// \param T The aggregate type.
  /// \param VT The ValueTy subclass identifier.
  /// \param V The element constants.
  /// \param AllocInfo Operand allocation information for User.
  LLVM_ABI ConstantAggregate(Type *T, ValueTy VT, ArrayRef<Constant *> V,
                             AllocInfo AllocInfo);

public:
  /// Return operand at index \p i_nocapture.
  /// \param i_nocapture The zero-based operand index.
  /// @return The operand value at that index.
  inline Constant *getOperand(unsigned i_nocapture) const;
  /// Set operand at index \p i_nocapture to \p Val_nocapture.
  /// \param i_nocapture The zero-based operand index.
  /// \param Val_nocapture The new operand value.
  inline void setOperand(unsigned i_nocapture, Constant *Val_nocapture);
  /// Return an iterator to the first operand.
  /// @return Iterator to the first operand.
  inline op_iterator op_begin();
  /// Return a const iterator to the first operand.
  /// @return Const iterator to the first operand.
  inline const_op_iterator op_begin() const;
  /// Return an iterator past the last operand.
  /// @return Iterator past the last operand.
  inline op_iterator op_end();
  /// Return a const iterator past the last operand.
  /// @return Const iterator past the last operand.
  inline const_op_iterator op_end() const;
protected:
  /// Return a reference to the operand at a compile-time index.
  /// @return Reference to the operand Use.
  template <int> inline Use &Op();
  /// Return a const reference to the operand at a compile-time index.
  /// @return Const reference to the operand Use.
  template <int> inline const Use &Op() const;
public:
  /// Return the number of operands.
  /// @return The operand count.
  inline unsigned getNumOperands() const;

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a \c ConstantAggregate.
  static bool classof(const Value *V) {
    return V->getValueID() >= ConstantAggregateFirstVal &&
           V->getValueID() <= ConstantAggregateLastVal;
  }
};

/// Operand layout traits for ConstantAggregate.
/// @return Operand layout traits for ConstantAggregate.
template <>
struct OperandTraits<ConstantAggregate>
    : public VariadicOperandTraits<ConstantAggregate> {};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(ConstantAggregate, Constant)

//===----------------------------------------------------------------------===//
/// ConstantArray - Constant Array Declarations
///
class ConstantArray final : public ConstantAggregate {
  friend struct ConstantAggrKeyType<ConstantArray>;
  friend class Constant;

  ConstantArray(ArrayType *T, ArrayRef<Constant *> Val, AllocInfo AllocInfo);

  void destroyConstantImpl();
  Value *handleOperandChangeImpl(Value *From, Value *To);

public:
  /// Return a constant array of type \p T with elements \p V.
  /// \param T The array type.
  /// \param V The element constants.
  /// @return A constant array of type \p T with elements \p V.
  LLVM_ABI static Constant *get(ArrayType *T, ArrayRef<Constant *> V);

private:
  static Constant *getImpl(ArrayType *T, ArrayRef<Constant *> V);

public:
  /// Specialize the getType() method to always return an ArrayType,
  /// which reduces the amount of casting needed in parts of the compiler.
  /// @return The type of this \c ConstantArray.
  inline ArrayType *getType() const {
    return cast<ArrayType>(Value::getType());
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a \c ConstantArray.
  static bool classof(const Value *V) {
    return V->getValueID() == ConstantArrayVal;
  }
};

//===----------------------------------------------------------------------===//
/// Constant struct value composed of other constants.
class ConstantStruct final : public ConstantAggregate {
  friend struct ConstantAggrKeyType<ConstantStruct>;
  friend class Constant;

  ConstantStruct(StructType *T, ArrayRef<Constant *> Val, AllocInfo AllocInfo);

  void destroyConstantImpl();
  Value *handleOperandChangeImpl(Value *From, Value *To);

public:
  /// Return a constant struct of type \p T with elements \p V.
  /// \param T The struct type.
  /// \param V The element constants.
  /// @return A constant struct of type \p T with elements \p V.
  LLVM_ABI static Constant *get(StructType *T, ArrayRef<Constant *> V);

  /// Return a constant struct of type \p T from the given element constants.
  /// \param T The struct type.
  /// \param Vs The element constants.
  /// @return A constant struct of type \p T from the given element constants.
  template <typename... Csts>
  static std::enable_if_t<are_base_of<Constant, Csts...>::value, Constant *>
  get(StructType *T, Csts *...Vs) {
    return get(T, ArrayRef<Constant *>({Vs...}));
  }

  /// Return an anonymous struct that has the specified elements.
  ///
  /// If the struct is possibly empty, then you must specify a context.
  /// \param V The struct element constants.
  /// \param Packed Whether the anonymous struct type should be packed.
  /// @return An anonymous constant struct of the given elements.
  static Constant *getAnon(ArrayRef<Constant *> V, bool Packed = false) {
    return get(getTypeForElements(V, Packed), V);
  }
  /// Return an anonymous struct that has the specified elements.
  /// \param Ctx The LLVM context used when \p V may be empty.
  /// \param V The struct element constants.
  /// \param Packed Whether the anonymous struct type should be packed.
  /// @return An anonymous constant struct of the given elements.
  static Constant *getAnon(LLVMContext &Ctx, ArrayRef<Constant *> V,
                           bool Packed = false) {
    return get(getTypeForElements(Ctx, V, Packed), V);
  }

  /// Return an anonymous struct type to use for a constant with the specified
  /// set of elements. The list must not be empty.
  /// \param V The element constants (must be non-empty).
  /// \param Packed Whether the struct type should be packed.
  /// @return A suitable anonymous struct type for the element values.
  LLVM_ABI static StructType *getTypeForElements(ArrayRef<Constant *> V,
                                                 bool Packed = false);
  /// Return an anonymous struct type for \p V, allowing an empty element list.
  /// \param Ctx The LLVM context that owns the type.
  /// \param V The element constants (may be empty).
  /// \param Packed Whether the struct type should be packed.
  /// @return A suitable anonymous struct type for the element values.
  LLVM_ABI static StructType *getTypeForElements(LLVMContext &Ctx,
                                                 ArrayRef<Constant *> V,
                                                 bool Packed = false);

  /// Specialization - reduce amount of casting.
  /// @return The type of this \c ConstantStruct.
  inline StructType *getType() const {
    return cast<StructType>(Value::getType());
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a \c ConstantStruct.
  static bool classof(const Value *V) {
    return V->getValueID() == ConstantStructVal;
  }
};

//===----------------------------------------------------------------------===//
/// Constant Vector Declarations
///
class ConstantVector final : public ConstantAggregate {
  friend struct ConstantAggrKeyType<ConstantVector>;
  friend class Constant;

  ConstantVector(VectorType *T, ArrayRef<Constant *> Val, AllocInfo AllocInfo);

  void destroyConstantImpl();
  Value *handleOperandChangeImpl(Value *From, Value *To);

public:
  /// Return a constant vector with elements \p V.
  /// \param V The element constants.
  /// @return A constant vector with elements \p V.
  LLVM_ABI static Constant *get(ArrayRef<Constant *> V);

private:
  static Constant *getImpl(ArrayRef<Constant *> V);

public:
  /// Return a ConstantVector with the specified constant in each element.
  ///
  /// Note that this might not return an instance of ConstantVector.
  /// \param EC The vector element count.
  /// \param Elt The value to splat into every element.
  /// @return A ConstantVector with the specified constant in each element.
  LLVM_ABI static Constant *getSplat(ElementCount EC, Constant *Elt);

  /// Specialize the getType() method to always return a FixedVectorType,
  /// which reduces the amount of casting needed in parts of the compiler.
  /// @return The type of this \c ConstantVector.
  inline FixedVectorType *getType() const {
    return cast<FixedVectorType>(Value::getType());
  }

  /// If all elements of the vector constant have the same value, return that
  /// value. Otherwise, return nullptr. Ignore poison elements by setting
  /// AllowPoison to true.
  /// \param AllowPoison Whether poison elements may be ignored when matching.
  /// @return The splat element constant, or null if not a splat.
  LLVM_ABI Constant *getSplatValue(bool AllowPoison = false) const;

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a \c ConstantVector.
  static bool classof(const Value *V) {
    return V->getValueID() == ConstantVectorVal;
  }
};

//===----------------------------------------------------------------------===//
/// A constant pointer value that points to null. This represents both scalar
/// pointer nulls and vector splats of pointer nulls.
///
class ConstantPointerNull final : public ConstantData {
  friend class Constant;

  explicit ConstantPointerNull(Type *T)
      : ConstantData(T, Value::ConstantPointerNullVal) {
    SubclassOptionalData = IsNullValue;
  }

  void destroyConstantImpl();

public:
  /// ConstantPointerNull values are uniqued and cannot be copied.
  /// \param Other The constant that would be copied (deleted).
  ConstantPointerNull(const ConstantPointerNull &Other) = delete;

  /// Return a null pointer constant of pointer type \p T.
  ///
  /// If \p T is a vector type, return a ConstantPointerNull with a splat of
  /// null pointer values. Otherwise return a ConstantPointerNull for the given
  /// pointer type.
  /// \param T The pointer type of the null constant.
  /// @return A null pointer constant of pointer type \p T.
  LLVM_ABI static ConstantPointerNull *get(PointerType *T);
  /// Return a null pointer constant of type \p T (pointer or vector thereof).
  /// \param T The pointer or vector-of-pointer type.
  /// @return A null pointer constant of type \p T (pointer or vector thereof).
  LLVM_ABI static ConstantPointerNull *get(Type *T);

  /// Return the scalar pointer type for this null value.
  /// @return The pointer type of this null constant.
  PointerType *getPointerType() const {
    return cast<PointerType>(Value::getType()->getScalarType());
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a \c ConstantPointerNull.
  static bool classof(const Value *V) {
    return V->getValueID() == ConstantPointerNullVal;
  }
};

//===----------------------------------------------------------------------===//
/// Dense array or vector constant of simple integer, byte, or FP elements.
///
/// A vector or array constant whose element type is a simple 1/2/4/8-byte
/// integer/byte or half/bfloat/float/double, and whose elements are just simple
/// data values (i.e. ConstantInt/ConstantByte/ConstantFP). This Constant node
/// has no operands because it stores all of the elements of the constant as
/// densely packed data, instead of as Value*'s.
///
/// This is the common base class of ConstantDataArray and ConstantDataVector.
class ConstantDataSequential : public ConstantData {
  friend class LLVMContextImpl;
  friend class Constant;

  /// A pointer to the bytes underlying this constant (which is owned by the
  /// uniquing StringMap).
  const char *DataElements;

  /// This forms a link list of ConstantDataSequential nodes that have
  /// the same value but different type.  For example, 0,0,0,1 could be a 4
  /// element array of i8, or a 1-element array of i32.  They'll both end up in
  /// the same StringMap bucket, linked up.
  std::unique_ptr<ConstantDataSequential> Next;

  void destroyConstantImpl();

protected:
  /// Construct a dense sequential constant of type \p ty from raw bytes \p Data.
  /// \param ty The array or vector type.
  /// \param VT The ValueTy subclass identifier.
  /// \param Data Pointer to the densely packed element bytes.
  explicit ConstantDataSequential(Type *ty, ValueTy VT, const char *Data)
      : ConstantData(ty, VT), DataElements(Data) {}

  /// Return a ConstantDataSequential (or CAZ) for raw bytes \p Bytes of type \p Ty.
  /// \param Bytes The densely packed element bytes.
  /// \param Ty The array or vector type.
  /// @return The constant data sequential for the raw data and type.
  LLVM_ABI static Constant *getImpl(StringRef Bytes, Type *Ty);

public:
  /// ConstantDataSequential values are uniqued and cannot be copied.
  /// \param Other The constant that would be copied (deleted).
  ConstantDataSequential(const ConstantDataSequential &Other) = delete;

  /// Return true if \p Ty can be an element type of a ConstantDataSequential.
  ///
  /// ConstantDataArray only works with normal float and int types that are
  /// stored densely in memory, not with things like i42 or x86_f80.
  /// \param Ty The proposed element type.
  /// @return True if the type can be an element of a ConstantDataSequential.
  LLVM_ABI static bool isElementTypeCompatible(Type *Ty);

  /// If this is a sequential container of integers (of any size), return the
  /// specified element in the low bits of a uint64_t.
  /// \param i The zero-based element index.
  /// @return The element at the given index as an integer.
  LLVM_ABI uint64_t getElementAsInteger(uint64_t i) const;

  /// If this is a sequential container of integers (of any size), return the
  /// specified element as an APInt.
  /// \param i The zero-based element index.
  /// @return The element at the given index as an \c APInt.
  LLVM_ABI APInt getElementAsAPInt(uint64_t i) const;

  /// If this is a sequential container of floating point type, return the
  /// specified element as an APFloat.
  /// \param i The zero-based element index.
  /// @return The element at the given index as an \c APFloat.
  LLVM_ABI APFloat getElementAsAPFloat(uint64_t i) const;

  /// If this is an sequential container of floats, return the specified element
  /// as a float.
  /// \param i The zero-based element index.
  /// @return The element at the given index as a float.
  LLVM_ABI float getElementAsFloat(uint64_t i) const;

  /// If this is an sequential container of doubles, return the specified
  /// element as a double.
  /// \param i The zero-based element index.
  /// @return The element at the given index as a double.
  LLVM_ABI double getElementAsDouble(uint64_t i) const;

  /// Return a Constant for a specified index's element.
  ///
  /// Note that this has to compute a new constant to return, so it isn't as
  /// efficient as getElementAsInteger/Float/Double.
  /// \param i The zero-based element index.
  /// @return The element at the given index as a \c Constant.
  LLVM_ABI Constant *getElementAsConstant(uint64_t i) const;

  /// Return the element type of the array/vector.
  /// @return The element type.
  LLVM_ABI Type *getElementType() const;

  /// Return the number of elements in the array or vector.
  /// @return The number of elements.
  LLVM_ABI uint64_t getNumElements() const;

  /// Return the size (in bytes) of each element in the array/vector.
  /// The size of the elements is known to be a multiple of one byte.
  /// @return The size in bytes of one element.
  LLVM_ABI uint64_t getElementByteSize() const;

  /// This method returns true if this is an array of \p CharSize integers or
  /// bytes.
  /// \param CharSize The integer/byte width in bits that counts as a character.
  /// @return True if this is an array of i8.
  LLVM_ABI bool isString(unsigned CharSize = 8) const;

  /// This method returns true if the array "isString", ends with a null byte,
  /// and does not contains any other null bytes.
  /// @return True if this is a null-terminated C string.
  LLVM_ABI bool isCString() const;

  /// If this array is isString(), then this method returns the array as a
  /// StringRef. Otherwise, it asserts out.
  /// @return The contents as a string.
  StringRef getAsString() const {
    assert(isString() && "Not a string");
    return getRawDataValues();
  }

  /// If this array is isCString(), then this method returns the array (without
  /// the trailing null byte) as a StringRef. Otherwise, it asserts out.
  /// @return The contents as a C string (without the trailing null).
  StringRef getAsCString() const {
    assert(isCString() && "Isn't a C string");
    StringRef Str = getAsString();
    return Str.drop_back();
  }

  /// Return the raw underlying bytes of this data.
  ///
  /// Note that this is an extremely tricky thing to work with, as it exposes
  /// the host endianness of the data elements.
  /// @return The raw bytes of the sequential data.
  LLVM_ABI StringRef getRawDataValues() const;

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a \c ConstantDataSequential.
  static bool classof(const Value *V) {
    return V->getValueID() == ConstantDataArrayVal ||
           V->getValueID() == ConstantDataVectorVal;
  }

private:
  const char *getElementPointer(uint64_t Elt) const;
};

//===----------------------------------------------------------------------===//
/// Dense array constant of simple integer, byte, or floating-point elements.
///
/// An array constant whose element type is a simple 1/2/4/8-byte integer, bytes
/// or float/double, and whose elements are just simple data values
/// (i.e. ConstantInt/ConstantFP). This Constant node has no operands because it
/// stores all of the elements of the constant as densely packed data, instead
/// of as Value*'s.
class ConstantDataArray final : public ConstantDataSequential {
  friend class ConstantDataSequential;

  explicit ConstantDataArray(Type *ty, const char *Data)
      : ConstantDataSequential(ty, ConstantDataArrayVal, Data) {}

public:
  /// ConstantDataArray values are uniqued and cannot be copied.
  /// \param Other The constant that would be copied (deleted).
  ConstantDataArray(const ConstantDataArray &Other) = delete;

  /// Return a dense array constant matching the element count and type of \p Elts.
  ///
  /// Note that this can return a ConstantAggregateZero object.
  /// \param Context The LLVM context that owns the constant.
  /// \param Elts The element values.
  /// @return A dense array constant matching the element count and type of \p Elts.
  template <typename ElementTy>
  static Constant *get(LLVMContext &Context, ArrayRef<ElementTy> Elts) {
    const char *Data = reinterpret_cast<const char *>(Elts.data());
    return getRaw(StringRef(Data, Elts.size() * sizeof(ElementTy)), Elts.size(),
                  Type::getScalarTy<ElementTy>(Context));
  }

  /// Return a dense array constant from a container compatible with ArrayRef.
  /// \param Context The LLVM context that owns the constant.
  /// \param Elts The element values.
  /// @return A dense array constant from a container compatible with ArrayRef.
  template <typename ArrayTy>
  static Constant *get(LLVMContext &Context, ArrayTy &Elts) {
    return ConstantDataArray::get(Context, ArrayRef(Elts));
  }

  /// Return a dense array constant from a raw element buffer.
  ///
  /// Note that this can return a ConstantAggregateZero object. ElementTy must
  /// be one of i8/i16/i32/i64/b8/b16/b32/b64/half/bfloat/float/double. Data is
  /// the buffer containing the elements. Be careful to make sure Data uses the
  /// right endianness; the buffer will be used as-is.
  /// \param Data The raw element bytes.
  /// \param NumElements The number of elements in \p Data.
  /// \param ElementTy The element type of the array.
  /// @return A dense array constant from a raw element buffer.
  static Constant *getRaw(StringRef Data, uint64_t NumElements,
                          Type *ElementTy) {
    Type *Ty = ArrayType::get(ElementTy, NumElements);
    return getImpl(Data, Ty);
  }

  /// Return a dense float array constant from bit-pattern elements.
  ///
  /// The amount of bits of the contained type must match the number of bits of
  /// the type contained in the passed in ArrayRef (i.e. half or bfloat for
  /// 16bits, float for 32bits, double for 64bits). Note that this can return a
  /// ConstantAggregateZero object.
  /// \param ElementType The floating-point element type.
  /// \param Elts The element bit patterns.
  /// @return A dense float array constant from bit-pattern elements.
  LLVM_ABI static Constant *getFP(Type *ElementType, ArrayRef<uint16_t> Elts);
  /// Return a dense float array constant from 32-bit element patterns.
  /// \param ElementType The floating-point element type.
  /// \param Elts The element bit patterns.
  /// @return A dense float array constant from 32-bit element patterns.
  LLVM_ABI static Constant *getFP(Type *ElementType, ArrayRef<uint32_t> Elts);
  /// Return a dense float array constant from 64-bit element patterns.
  /// \param ElementType The floating-point element type.
  /// \param Elts The element bit patterns.
  /// @return A dense float array constant from 64-bit element patterns.
  LLVM_ABI static Constant *getFP(Type *ElementType, ArrayRef<uint64_t> Elts);

  /// Return a dense byte array constant from element values.
  ///
  /// The amount of bits of the contained type must match the number of bits of
  /// the type contained in the passed in ArrayRef. Note that this can return a
  /// ConstantAggregateZero object.
  /// \param ElementType The byte element type.
  /// \param Elts The element values.
  /// @return A dense byte array constant from element values.
  LLVM_ABI static Constant *getByte(Type *ElementType, ArrayRef<uint8_t> Elts);
  /// Return a dense byte array constant from 16-bit element values.
  /// \param ElementType The byte element type.
  /// \param Elts The element values.
  /// @return A dense byte array constant from 16-bit element values.
  LLVM_ABI static Constant *getByte(Type *ElementType, ArrayRef<uint16_t> Elts);
  /// Return a dense byte array constant from 32-bit element values.
  /// \param ElementType The byte element type.
  /// \param Elts The element values.
  /// @return A dense byte array constant from 32-bit element values.
  LLVM_ABI static Constant *getByte(Type *ElementType, ArrayRef<uint32_t> Elts);
  /// Return a dense byte array constant from 64-bit element values.
  /// \param ElementType The byte element type.
  /// \param Elts The element values.
  /// @return A dense byte array constant from 64-bit element values.
  LLVM_ABI static Constant *getByte(Type *ElementType, ArrayRef<uint64_t> Elts);

  /// Construct a ConstantDataArray initialized from a text string.
  ///
  /// The default behavior (AddNull==true) causes a null terminator to be placed
  /// at the end of the array (increasing the length of the string by one more
  /// than the StringRef would normally indicate). Pass AddNull=false to disable
  /// this behavior.
  /// \param Context The LLVM context that owns the constant.
  /// \param Initializer The string contents.
  /// \param AddNull Whether to append a trailing null terminator.
  /// \param ByteString Whether to build a byte-string rather than i8.
  /// @return A ConstantDataArray initialized from \p Initializer.
  LLVM_ABI static Constant *getString(LLVMContext &Context,
                                      StringRef Initializer,
                                      bool AddNull = true,
                                      bool ByteString = false);

  /// Specialize the getType() method to always return an ArrayType,
  /// which reduces the amount of casting needed in parts of the compiler.
  /// @return The type of this \c ConstantDataArray.
  inline ArrayType *getType() const {
    return cast<ArrayType>(Value::getType());
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a \c ConstantDataArray.
  static bool classof(const Value *V) {
    return V->getValueID() == ConstantDataArrayVal;
  }
};

//===----------------------------------------------------------------------===//
/// Dense vector constant of simple integer or floating-point elements.
///
/// A vector constant whose element type is a simple 1/2/4/8-byte integer or
/// float/double, and whose elements are just simple data values
/// (i.e. ConstantInt/ConstantFP). This Constant node has no operands because it
/// stores all of the elements of the constant as densely packed data, instead
/// of as Value*'s.
class ConstantDataVector final : public ConstantDataSequential {
  friend class ConstantDataSequential;

  explicit ConstantDataVector(Type *ty, const char *Data)
      : ConstantDataSequential(ty, ConstantDataVectorVal, Data),
        IsSplatSet(false) {}
  // Cache whether or not the constant is a splat.
  mutable bool IsSplatSet : 1;
  mutable bool IsSplat : 1;
  bool isSplatData() const;

public:
  /// ConstantDataVector values are uniqued and cannot be copied.
  /// \param Other The constant that would be copied (deleted).
  ConstantDataVector(const ConstantDataVector &Other) = delete;

  /// Return a dense vector constant matching the element count and type of \p Elts.
  ///
  /// Note that this can return a ConstantAggregateZero object.
  /// \param Context The LLVM context that owns the constant.
  /// \param Elts The element values.
  /// @return A dense vector constant matching the element count and type of \p Elts.
  LLVM_ABI static Constant *get(LLVMContext &Context, ArrayRef<uint8_t> Elts);
  /// Return a dense vector constant of 16-bit integer elements.
  /// \param Context The LLVM context that owns the constant.
  /// \param Elts The element values.
  /// @return A dense vector constant of 16-bit integer elements.
  LLVM_ABI static Constant *get(LLVMContext &Context, ArrayRef<uint16_t> Elts);
  /// Return a dense vector constant of 32-bit integer elements.
  /// \param Context The LLVM context that owns the constant.
  /// \param Elts The element values.
  /// @return A dense vector constant of 32-bit integer elements.
  LLVM_ABI static Constant *get(LLVMContext &Context, ArrayRef<uint32_t> Elts);
  /// Return a dense vector constant of 64-bit integer elements.
  /// \param Context The LLVM context that owns the constant.
  /// \param Elts The element values.
  /// @return A dense vector constant of 64-bit integer elements.
  LLVM_ABI static Constant *get(LLVMContext &Context, ArrayRef<uint64_t> Elts);
  /// Return a dense vector constant of float elements.
  /// \param Context The LLVM context that owns the constant.
  /// \param Elts The element values.
  /// @return A dense vector constant of float elements.
  LLVM_ABI static Constant *get(LLVMContext &Context, ArrayRef<float> Elts);
  /// Return a dense vector constant of double elements.
  /// \param Context The LLVM context that owns the constant.
  /// \param Elts The element values.
  /// @return A dense vector constant of double elements.
  LLVM_ABI static Constant *get(LLVMContext &Context, ArrayRef<double> Elts);

  /// Return a dense vector constant from a raw element buffer.
  ///
  /// Note that this can return a ConstantAggregateZero object. ElementTy must
  /// be one of i8/i16/i32/i64/b8/b16/b32/b64/half/bfloat/float/double. Data is
  /// the buffer containing the elements. Be careful to make sure Data uses the
  /// right endianness; the buffer will be used as-is.
  /// \param Data The raw element bytes.
  /// \param NumElements The number of elements in \p Data.
  /// \param ElementTy The element type of the vector.
  /// @return A dense vector constant from a raw element buffer.
  static Constant *getRaw(StringRef Data, uint64_t NumElements,
                          Type *ElementTy) {
    Type *Ty = VectorType::get(ElementTy, ElementCount::getFixed(NumElements));
    return getImpl(Data, Ty);
  }

  /// Return a dense byte vector constant from element values.
  ///
  /// The amount of bits of the contained type must match the number of bits of
  /// the type contained in the passed in ArrayRef.
  /// \param ElementType The byte element type.
  /// \param Elts The element values.
  /// @return A dense byte vector constant from element values.
  LLVM_ABI static Constant *getByte(Type *ElementType, ArrayRef<uint8_t> Elts);
  /// Return a dense byte vector constant from 16-bit element values.
  /// \param ElementType The byte element type.
  /// \param Elts The element values.
  /// @return A dense byte vector constant from 16-bit element values.
  LLVM_ABI static Constant *getByte(Type *ElementType, ArrayRef<uint16_t> Elts);
  /// Return a dense byte vector constant from 32-bit element values.
  /// \param ElementType The byte element type.
  /// \param Elts The element values.
  /// @return A dense byte vector constant from 32-bit element values.
  LLVM_ABI static Constant *getByte(Type *ElementType, ArrayRef<uint32_t> Elts);
  /// Return a dense byte vector constant from 64-bit element values.
  /// \param ElementType The byte element type.
  /// \param Elts The element values.
  /// @return A dense byte vector constant from 64-bit element values.
  LLVM_ABI static Constant *getByte(Type *ElementType, ArrayRef<uint64_t> Elts);

  /// Return a dense float vector constant from bit-pattern elements.
  ///
  /// The amount of bits of the contained type must match the number of bits of
  /// the type contained in the passed in ArrayRef (i.e. half or bfloat for
  /// 16bits, float for 32bits, double for 64bits). Note that this can return a
  /// ConstantAggregateZero object.
  /// \param ElementType The floating-point element type.
  /// \param Elts The element bit patterns.
  /// @return A dense float vector constant from bit-pattern elements.
  LLVM_ABI static Constant *getFP(Type *ElementType, ArrayRef<uint16_t> Elts);
  /// Return a dense float vector constant from 32-bit element patterns.
  /// \param ElementType The floating-point element type.
  /// \param Elts The element bit patterns.
  /// @return A dense float vector constant from 32-bit element patterns.
  LLVM_ABI static Constant *getFP(Type *ElementType, ArrayRef<uint32_t> Elts);
  /// Return a dense float vector constant from 64-bit element patterns.
  /// \param ElementType The floating-point element type.
  /// \param Elts The element bit patterns.
  /// @return A dense float vector constant from 64-bit element patterns.
  LLVM_ABI static Constant *getFP(Type *ElementType, ArrayRef<uint64_t> Elts);

  /// Return a vector constant with \p Elt in each of \p NumElts elements.
  ///
  /// The specified constant has to be of a compatible type
  /// (i8/i16/i32/i64/b8/b16/b32/b64/half/bfloat/float/double) and must be a
  /// ConstantFP, ConstantByte or ConstantInt.
  /// \param NumElts The number of vector elements.
  /// \param Elt The value to splat into every element.
  /// @return A vector constant with \p Elt in each of \p NumElts elements.
  LLVM_ABI static Constant *getSplat(unsigned NumElts, Constant *Elt);

  /// Returns true if this is a splat constant, meaning that all elements have
  /// the same value.
  /// @return True if every element is the same.
  LLVM_ABI bool isSplat() const;

  /// If this is a splat constant, meaning that all of the elements have the
  /// same value, return that value. Otherwise return NULL.
  /// @return The splat element constant, or null if not a splat.
  LLVM_ABI Constant *getSplatValue() const;

  /// Specialize the getType() method to always return a FixedVectorType,
  /// which reduces the amount of casting needed in parts of the compiler.
  /// @return The type of this \c ConstantDataVector.
  inline FixedVectorType *getType() const {
    return cast<FixedVectorType>(Value::getType());
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a \c ConstantDataVector.
  static bool classof(const Value *V) {
    return V->getValueID() == ConstantDataVectorVal;
  }
};

//===----------------------------------------------------------------------===//
/// A constant token which is empty
///
class ConstantTokenNone final : public ConstantData {
  friend class Constant;

  explicit ConstantTokenNone(LLVMContext &Context)
      : ConstantData(Type::getTokenTy(Context), ConstantTokenNoneVal) {
    SubclassOptionalData = IsNullValue;
  }

  void destroyConstantImpl();

public:
  /// ConstantTokenNone values are uniqued and cannot be copied.
  /// \param Other The constant that would be copied (deleted).
  ConstantTokenNone(const ConstantTokenNone &Other) = delete;

  /// Return the empty token constant for \p Context.
  /// \param Context The LLVM context that owns the constant.
  /// @return The empty token constant for \p Context.
  LLVM_ABI static ConstantTokenNone *get(LLVMContext &Context);

  /// Methods to support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a \c ConstantTokenNone.
  static bool classof(const Value *V) {
    return V->getValueID() == ConstantTokenNoneVal;
  }
};

/// A constant target extension type default initializer
class ConstantTargetNone final : public ConstantData {
  friend class Constant;

  explicit ConstantTargetNone(TargetExtType *T)
      : ConstantData(T, Value::ConstantTargetNoneVal) {
    SubclassOptionalData = IsNullValue;
  }

  void destroyConstantImpl();

public:
  /// ConstantTargetNone values are uniqued and cannot be copied.
  /// \param Other The constant that would be copied (deleted).
  ConstantTargetNone(const ConstantTargetNone &Other) = delete;

  /// Return the default target-extension constant of type \p T.
  /// \param T The target extension type.
  /// @return The default target-extension constant of type \p T.
  LLVM_ABI static ConstantTargetNone *get(TargetExtType *T);

  /// Specialize the getType() method to always return an TargetExtType,
  /// which reduces the amount of casting needed in parts of the compiler.
  /// @return The type of this \c ConstantTargetNone.
  inline TargetExtType *getType() const {
    return cast<TargetExtType>(Value::getType());
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a \c ConstantTargetNone.
  static bool classof(const Value *V) {
    return V->getValueID() == ConstantTargetNoneVal;
  }
};

/// The address of a basic block.
///
class BlockAddress final : public Constant {
  friend class Constant;

  constexpr static IntrusiveOperandsAllocMarker AllocMarker{0};

  BasicBlock *Block;

  BlockAddress(Type *Ty, BasicBlock *BB);

  void *operator new(size_t S) { return User::operator new(S, AllocMarker); }

  void destroyConstantImpl();
  Value *handleOperandChangeImpl(Value *From, Value *To);

public:
  /// Deallocate a BlockAddress created with the fixed-size allocator.
  /// \param Ptr Pointer returned by the fixed-size \c operator new.
  void operator delete(void *Ptr) { User::operator delete(Ptr, AllocMarker); }

  /// Return a BlockAddress for the specified function and basic block.
  /// \param F The function that contains \p BB.
  /// \param BB The basic block whose address is taken.
  /// @return A BlockAddress for the specified function and basic block.
  LLVM_ABI static BlockAddress *get(Function *F, BasicBlock *BB);

  /// Return a BlockAddress for the specified basic block.
  ///
  /// The basic block must be embedded into a function.
  /// \param BB The basic block whose address is taken.
  /// @return A BlockAddress for the specified basic block.
  LLVM_ABI static BlockAddress *get(BasicBlock *BB);

  /// Return a BlockAddress for a basic block that may not yet be in a function.
  ///
  /// The specified type must match the type of the function the block will be
  /// inserted into.
  /// \param Ty The function type that will own \p BB.
  /// \param BB The basic block whose address is taken.
  /// @return A BlockAddress for a basic block that may not yet be in a function.
  LLVM_ABI static BlockAddress *get(Type *Ty, BasicBlock *BB);

  /// Lookup an existing \c BlockAddress constant for the given BasicBlock.
  ///
  /// \param BB The basic block to look up.
  /// \returns 0 if \c !BB->hasAddressTaken(), otherwise the \c BlockAddress.
  LLVM_ABI static BlockAddress *lookup(const BasicBlock *BB);

  /// Return operand at index \p i_nocapture.
  /// \param i_nocapture The zero-based operand index.
  /// @return The operand value at that index.
  inline Value *getOperand(unsigned i_nocapture) const;
  /// Set operand at index \p i_nocapture to \p Val_nocapture.
  /// \param i_nocapture The zero-based operand index.
  /// \param Val_nocapture The new operand value.
  inline void setOperand(unsigned i_nocapture, Value *Val_nocapture);
  /// Return an iterator to the first operand.
  /// @return Iterator to the first operand.
  inline op_iterator op_begin();
  /// Return a const iterator to the first operand.
  /// @return Const iterator to the first operand.
  inline const_op_iterator op_begin() const;
  /// Return an iterator past the last operand.
  /// @return Iterator past the last operand.
  inline op_iterator op_end();
  /// Return a const iterator past the last operand.
  /// @return Const iterator past the last operand.
  inline const_op_iterator op_end() const;
protected:
  /// Return a reference to the operand at a compile-time index.
  /// @return Reference to the operand Use.
  template <int> inline Use &Op();
  /// Return a const reference to the operand at a compile-time index.
  /// @return Const reference to the operand Use.
  template <int> inline const Use &Op() const;
public:
  /// Return the number of operands.
  /// @return The operand count.
  inline unsigned getNumOperands() const;

  /// Return the basic block whose address this constant represents.
  /// @return The referenced basic block.
  BasicBlock *getBasicBlock() const { return Block; }
  /// Return the function that contains the referenced basic block.
  /// @return The referenced basic block.
  Function *getFunction() const { return getBasicBlock()->getParent(); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a \c BlockAddress.
  static bool classof(const Value *V) {
    return V->getValueID() == BlockAddressVal;
  }
};

/// Operand layout traits for BlockAddress.
/// @return Operand layout traits for BlockAddress.
template <>
struct OperandTraits<BlockAddress>
    : public FixedNumOperandTraits<BlockAddress, 0> {};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(BlockAddress, Value)

/// Constant wrapper for a DSO-local equivalent of a global value.
///
/// Wrapper for a function that represents a value that functionally represents
/// the original function. This can be a function, global alias to a function,
/// or an ifunc.
class DSOLocalEquivalent final : public Constant {
  friend class Constant;

  constexpr static IntrusiveOperandsAllocMarker AllocMarker{1};

  DSOLocalEquivalent(GlobalValue *GV);

  void *operator new(size_t S) { return User::operator new(S, AllocMarker); }

  void destroyConstantImpl();
  Value *handleOperandChangeImpl(Value *From, Value *To);

public:
  /// Deallocate a DSOLocalEquivalent created with the fixed-size allocator.
  /// \param Ptr Pointer returned by the fixed-size \c operator new.
  void operator delete(void *Ptr) { User::operator delete(Ptr, AllocMarker); }

  /// Return a DSOLocalEquivalent for the specified global value.
  /// \param GV The global value to wrap.
  /// @return A DSOLocalEquivalent for the specified global value.
  LLVM_ABI static DSOLocalEquivalent *get(GlobalValue *GV);

  /// Return operand at index \p i_nocapture.
  /// \param i_nocapture The zero-based operand index.
  /// @return The operand value at that index.
  inline Value *getOperand(unsigned i_nocapture) const;
  /// Set operand at index \p i_nocapture to \p Val_nocapture.
  /// \param i_nocapture The zero-based operand index.
  /// \param Val_nocapture The new operand value.
  inline void setOperand(unsigned i_nocapture, Value *Val_nocapture);
  /// Return an iterator to the first operand.
  /// @return Iterator to the first operand.
  inline op_iterator op_begin();
  /// Return a const iterator to the first operand.
  /// @return Const iterator to the first operand.
  inline const_op_iterator op_begin() const;
  /// Return an iterator past the last operand.
  /// @return Iterator past the last operand.
  inline op_iterator op_end();
  /// Return a const iterator past the last operand.
  /// @return Const iterator past the last operand.
  inline const_op_iterator op_end() const;
protected:
  /// Return a reference to the operand at a compile-time index.
  /// @return Reference to the operand Use.
  template <int> inline Use &Op();
  /// Return a const reference to the operand at a compile-time index.
  /// @return Const reference to the operand Use.
  template <int> inline const Use &Op() const;
public:
  /// Return the number of operands.
  /// @return The operand count.
  inline unsigned getNumOperands() const;

  /// Return the wrapped global value.
  /// @return The referenced global value.
  GlobalValue *getGlobalValue() const {
    return cast<GlobalValue>(Op<0>().get());
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a \c DSOLocalEquivalent.
  static bool classof(const Value *V) {
    return V->getValueID() == DSOLocalEquivalentVal;
  }
};

/// Operand layout traits for DSOLocalEquivalent.
/// @return Operand layout traits for DSOLocalEquivalent.
template <>
struct OperandTraits<DSOLocalEquivalent>
    : public FixedNumOperandTraits<DSOLocalEquivalent, 1> {};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(DSOLocalEquivalent, Value)

/// Wrapper for a value that won't be replaced with a CFI jump table
/// pointer in LowerTypeTestsModule.
class NoCFIValue final : public Constant {
  friend class Constant;

  constexpr static IntrusiveOperandsAllocMarker AllocMarker{1};

  NoCFIValue(GlobalValue *GV);

  void *operator new(size_t S) { return User::operator new(S, AllocMarker); }

  void destroyConstantImpl();
  Value *handleOperandChangeImpl(Value *From, Value *To);

public:
  /// Return a NoCFIValue for the specified function.
  /// \param GV The global value to wrap.
  /// @return A NoCFIValue for the specified function.
  LLVM_ABI static NoCFIValue *get(GlobalValue *GV);

  /// Return operand at index \p i_nocapture.
  /// \param i_nocapture The zero-based operand index.
  /// @return The operand value at that index.
  inline Value *getOperand(unsigned i_nocapture) const;
  /// Set operand at index \p i_nocapture to \p Val_nocapture.
  /// \param i_nocapture The zero-based operand index.
  /// \param Val_nocapture The new operand value.
  inline void setOperand(unsigned i_nocapture, Value *Val_nocapture);
  /// Return an iterator to the first operand.
  /// @return Iterator to the first operand.
  inline op_iterator op_begin();
  /// Return a const iterator to the first operand.
  /// @return Const iterator to the first operand.
  inline const_op_iterator op_begin() const;
  /// Return an iterator past the last operand.
  /// @return Iterator past the last operand.
  inline op_iterator op_end();
  /// Return a const iterator past the last operand.
  /// @return Const iterator past the last operand.
  inline const_op_iterator op_end() const;
protected:
  /// Return a reference to the operand at a compile-time index.
  /// @return Reference to the operand Use.
  template <int> inline Use &Op();
  /// Return a const reference to the operand at a compile-time index.
  /// @return Const reference to the operand Use.
  template <int> inline const Use &Op() const;
public:
  /// Return the number of operands.
  /// @return The operand count.
  inline unsigned getNumOperands() const;

  /// Return the wrapped global value.
  /// @return The referenced global value.
  GlobalValue *getGlobalValue() const {
    return cast<GlobalValue>(Op<0>().get());
  }

  /// NoCFIValue is always a pointer.
  /// @return The type of this \c NoCFIValue.
  PointerType *getType() const {
    return cast<PointerType>(Value::getType());
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a \c NoCFIValue.
  static bool classof(const Value *V) {
    return V->getValueID() == NoCFIValueVal;
  }
};

/// Operand layout traits for NoCFIValue.
/// @return Operand layout traits for NoCFIValue.
template <>
struct OperandTraits<NoCFIValue> : public FixedNumOperandTraits<NoCFIValue, 1> {
};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(NoCFIValue, Value)

/// A signed pointer, in the ptrauth sense.
class ConstantPtrAuth final : public Constant {
  friend struct ConstantPtrAuthKeyType;
  friend class Constant;

  constexpr static IntrusiveOperandsAllocMarker AllocMarker{5};

  ConstantPtrAuth(Constant *Ptr, ConstantInt *Key, ConstantInt *Disc,
                  Constant *AddrDisc, Constant *DeactivationSymbol);

  void *operator new(size_t s) { return User::operator new(s, AllocMarker); }

  void destroyConstantImpl();
  Value *handleOperandChangeImpl(Value *From, Value *To);

public:
  /// Return a pointer signed with the specified parameters.
  /// \param Ptr The pointer to sign.
  /// \param Key The ptrauth key ID.
  /// \param Disc The integer discriminator.
  /// \param AddrDisc The address discriminator, or null.
  /// \param DeactivationSymbol Optional deactivation symbol constant.
  /// @return A pointer signed with the specified parameters.
  LLVM_ABI static ConstantPtrAuth *get(Constant *Ptr, ConstantInt *Key,
                                       ConstantInt *Disc, Constant *AddrDisc,
                                       Constant *DeactivationSymbol);

  /// Produce a new ptrauth expression signing the given value using
  /// the same schema as is stored in one.
  /// \param Pointer The pointer value to sign with this schema.
  /// @return A pointer-auth constant with the same schema and a new pointer.
  LLVM_ABI ConstantPtrAuth *getWithSameSchema(Constant *Pointer) const;

  /// Return operand at index \p i_nocapture.
  /// \param i_nocapture The zero-based operand index.
  /// @return The operand value at that index.
  inline Constant *getOperand(unsigned i_nocapture) const;
  /// Set operand at index \p i_nocapture to \p Val_nocapture.
  /// \param i_nocapture The zero-based operand index.
  /// \param Val_nocapture The new operand value.
  inline void setOperand(unsigned i_nocapture, Constant *Val_nocapture);
  /// Return an iterator to the first operand.
  /// @return Iterator to the first operand.
  inline op_iterator op_begin();
  /// Return a const iterator to the first operand.
  /// @return Const iterator to the first operand.
  inline const_op_iterator op_begin() const;
  /// Return an iterator past the last operand.
  /// @return Iterator past the last operand.
  inline op_iterator op_end();
  /// Return a const iterator past the last operand.
  /// @return Const iterator past the last operand.
  inline const_op_iterator op_end() const;
protected:
  /// Return a reference to the operand at a compile-time index.
  /// @return Reference to the operand Use.
  template <int> inline Use &Op();
  /// Return a const reference to the operand at a compile-time index.
  /// @return Const reference to the operand Use.
  template <int> inline const Use &Op() const;
public:
  /// Return the number of operands.
  /// @return The operand count.
  inline unsigned getNumOperands() const;

  /// The pointer that is signed in this ptrauth signed pointer.
  /// @return Reference to the operand Use.
  Constant *getPointer() const { return cast<Constant>(Op<0>().get()); }

  /// The Key ID, an i32 constant.
  /// @return Reference to the operand Use.
  ConstantInt *getKey() const { return cast<ConstantInt>(Op<1>().get()); }

  /// The integer discriminator, an i64 constant, or 0.
  /// @return The discriminator constant.
  ConstantInt *getDiscriminator() const {
    return cast<ConstantInt>(Op<2>().get());
  }

  /// Return the address discriminator if any, or the null constant.
  ///
  /// If present, this must be a value equivalent to the storage location of
  /// the only global-initializer user of the ptrauth signed pointer.
  /// @return The discriminator constant.
  Constant *getAddrDiscriminator() const {
    return cast<Constant>(Op<3>().get());
  }

  /// Whether there is any non-null address discriminator.
  /// @return True if an address discriminator is present.
  bool hasAddressDiscriminator() const {
    return !isa<ConstantPointerNull>(getAddrDiscriminator());
  }

  /// Return the optional deactivation symbol operand.
  /// @return The deactivation symbol constant.
  Constant *getDeactivationSymbol() const {
    return cast<Constant>(Op<4>().get());
  }

  /// Special address-discriminator values used by ptrauth lowering.
  ///
  /// A constant value for the address discriminator which has special
  /// significance to ctors/dtors lowering. Regular address discrimination can't
  /// be applied for them since uses of llvm.global_{c|d}tors are disallowed
  /// (see Verifier::visitGlobalVariable) and we can't emit getelementptr
  /// expressions referencing these special arrays.
  enum {
    /// Discriminator reserved for llvm.global_ctors / llvm.global_dtors.
    AddrDiscriminator_CtorsDtors = 1
  };

  /// Whether the address uses a special address discriminator.
  ///
  /// These discriminators can't be used in real pointer-auth values; they can
  /// only be used in "prototype" values that indicate how some real schema is
  /// supposed to be produced.
  /// \param Value The address discriminator constant to test.
  /// @return True if an address discriminator is present.
  LLVM_ABI bool hasSpecialAddressDiscriminator(uint64_t Value) const;

  /// Check whether authentication with \p Key and \p Discriminator is compatible.
  ///
  /// Returns true when an authentication operation with key \p Key and
  /// (possibly blended) discriminator \p Discriminator is known to be compatible
  /// with this ptrauth signed pointer.
  /// \param Key The authentication key.
  /// \param Discriminator The (possibly blended) discriminator.
  /// \param DL The data layout used for compatibility analysis.
  /// @return True if this pointer-auth constant is known compatible with the given key and discriminator.
  LLVM_ABI bool isKnownCompatibleWith(const Value *Key,
                                      const Value *Discriminator,
                                      const DataLayout &DL) const;

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a \c ConstantPtrAuth.
  static bool classof(const Value *V) {
    return V->getValueID() == ConstantPtrAuthVal;
  }
};

/// Operand layout traits for ConstantPtrAuth.
/// @return Operand layout traits for ConstantPtrAuth.
template <>
struct OperandTraits<ConstantPtrAuth>
    : public FixedNumOperandTraits<ConstantPtrAuth, 5> {};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(ConstantPtrAuth, Constant)

//===----------------------------------------------------------------------===//
/// A constant value that is initialized with an expression using
/// other constant values.
///
/// This class uses the standard Instruction opcodes to define the various
/// constant expressions.  The Opcode field for the ConstantExpr class is
/// maintained in the Value::SubclassData field.
class ConstantExpr : public Constant {
  friend struct ConstantExprKeyType;
  friend class Constant;

  void destroyConstantImpl();
  Value *handleOperandChangeImpl(Value *From, Value *To);

protected:
  /// Construct a constant expression of type \p ty with opcode \p Opcode.
  /// \param ty The result type of the expression.
  /// \param Opcode The Instruction opcode for the expression.
  /// \param AllocInfo Operand allocation information for User.
  ConstantExpr(Type *ty, unsigned Opcode, AllocInfo AllocInfo)
      : Constant(ty, ConstantExprVal, AllocInfo) {
    // Operation type (an Instruction opcode) is stored as the SubclassData.
    setValueSubclassData(Opcode);
  }

  /// Destroy a constant expression.
  ~ConstantExpr() = default;

public:
  // Static methods to construct a ConstantExpr of different kinds.  Note that
  // these methods may return a object that is not an instance of the
  // ConstantExpr class, because they will attempt to fold the constant
  // expression into something simpler if possible.

  /// Return a constant expression for the alignment of type \p Ty (as i64).
  /// \param Ty The type whose alignment is computed.
  /// @return A constant expression for the alignment of type \p Ty (as i64).
  LLVM_ABI static Constant *getAlignOf(Type *Ty);

  /// Return a constant expression for the alloc size of type \p Ty (as i64).
  ///
  /// Computes the (alloc) size of a type in address-units, not bits, in a
  /// target independent way.
  /// \param Ty The type whose size is computed.
  /// @return A constant expression for the alloc size of type \p Ty (as i64).
  LLVM_ABI static Constant *getSizeOf(Type *Ty);

  /// Return a constant expression for the negation of \p C.
  /// \param C The constant to negate.
  /// \param HasNSW Whether the result has the nsw flag.
  /// @return A constant expression for the negation of \p C.
  LLVM_ABI static Constant *getNeg(Constant *C, bool HasNSW = false);
  /// Return a constant expression for the bitwise NOT of \p C.
  /// \param C The constant to complement.
  /// @return A constant expression for the bitwise NOT of \p C.
  LLVM_ABI static Constant *getNot(Constant *C);
  /// Return a constant expression for the addition of \p C1 and \p C2.
  /// \param C1 The first addend.
  /// \param C2 The second addend.
  /// \param HasNUW Whether the result has the nuw flag.
  /// \param HasNSW Whether the result has the nsw flag.
  /// @return A constant expression for the addition of \p C1 and \p C2.
  LLVM_ABI static Constant *getAdd(Constant *C1, Constant *C2,
                                   bool HasNUW = false, bool HasNSW = false);
  /// Return a constant expression for the subtraction of \p C2 from \p C1.
  /// \param C1 The minuend.
  /// \param C2 The subtrahend.
  /// \param HasNUW Whether the result has the nuw flag.
  /// \param HasNSW Whether the result has the nsw flag.
  /// @return A constant expression for the subtraction of \p C2 from \p C1.
  LLVM_ABI static Constant *getSub(Constant *C1, Constant *C2,
                                   bool HasNUW = false, bool HasNSW = false);
  /// Return a constant expression for the bitwise XOR of \p C1 and \p C2.
  /// \param C1 The first operand.
  /// \param C2 The second operand.
  /// @return A constant expression for the bitwise XOR of \p C1 and \p C2.
  LLVM_ABI static Constant *getXor(Constant *C1, Constant *C2);
  /// Return a truncating cast of \p C to type \p Ty.
  /// \param C The constant to truncate.
  /// \param Ty The destination type.
  /// \param OnlyIfReduced If true, return null unless the expression reduces.
  /// @return A truncating cast of \p C to type \p Ty.
  LLVM_ABI static Constant *getTrunc(Constant *C, Type *Ty,
                                     bool OnlyIfReduced = false);
  /// Return a ptrtoaddr cast of \p C to type \p Ty.
  /// \param C The pointer constant to cast.
  /// \param Ty The destination integer type.
  /// \param OnlyIfReduced If true, return null unless the expression reduces.
  /// @return A ptrtoaddr cast of \p C to type \p Ty.
  LLVM_ABI static Constant *getPtrToAddr(Constant *C, Type *Ty,
                                         bool OnlyIfReduced = false);
  /// Return a ptrtoint cast of \p C to type \p Ty.
  /// \param C The pointer constant to cast.
  /// \param Ty The destination integer type.
  /// \param OnlyIfReduced If true, return null unless the expression reduces.
  /// @return A ptrtoint cast of \p C to type \p Ty.
  LLVM_ABI static Constant *getPtrToInt(Constant *C, Type *Ty,
                                        bool OnlyIfReduced = false);
  /// Return an inttoptr cast of \p C to type \p Ty.
  /// \param C The integer constant to cast.
  /// \param Ty The destination pointer type.
  /// \param OnlyIfReduced If true, return null unless the expression reduces.
  /// @return An inttoptr cast of \p C to type \p Ty.
  LLVM_ABI static Constant *getIntToPtr(Constant *C, Type *Ty,
                                        bool OnlyIfReduced = false);
  /// Return a bitcast of \p C to type \p Ty.
  /// \param C The constant to bitcast.
  /// \param Ty The destination type.
  /// \param OnlyIfReduced If true, return null unless the expression reduces.
  /// @return A bitcast of \p C to type \p Ty.
  LLVM_ABI static Constant *getBitCast(Constant *C, Type *Ty,
                                       bool OnlyIfReduced = false);
  /// Return an addrspacecast of \p C to type \p Ty.
  /// \param C The pointer constant to cast.
  /// \param Ty The destination pointer type.
  /// \param OnlyIfReduced If true, return null unless the expression reduces.
  /// @return An addrspacecast of \p C to type \p Ty.
  LLVM_ABI static Constant *getAddrSpaceCast(Constant *C, Type *Ty,
                                             bool OnlyIfReduced = false);

  /// Return a NSW negation of \p C.
  /// \param C The constant to negate.
  /// @return A NSW negation of \p C.
  static Constant *getNSWNeg(Constant *C) { return getNeg(C, /*HasNSW=*/true); }

  /// Return a NSW addition of \p C1 and \p C2.
  /// \param C1 The first addend.
  /// \param C2 The second addend.
  /// @return A NSW addition of \p C1 and \p C2.
  static Constant *getNSWAdd(Constant *C1, Constant *C2) {
    return getAdd(C1, C2, false, true);
  }

  /// Return a NUW addition of \p C1 and \p C2.
  /// \param C1 The first addend.
  /// \param C2 The second addend.
  /// @return A NUW addition of \p C1 and \p C2.
  static Constant *getNUWAdd(Constant *C1, Constant *C2) {
    return getAdd(C1, C2, true, false);
  }

  /// Return a NSW subtraction of \p C2 from \p C1.
  /// \param C1 The minuend.
  /// \param C2 The subtrahend.
  /// @return A NSW subtraction of \p C2 from \p C1.
  static Constant *getNSWSub(Constant *C1, Constant *C2) {
    return getSub(C1, C2, false, true);
  }

  /// Return a NUW subtraction of \p C2 from \p C1.
  /// \param C1 The minuend.
  /// \param C2 The subtrahend.
  /// @return A NUW subtraction of \p C2 from \p C1.
  static Constant *getNUWSub(Constant *C1, Constant *C2) {
    return getSub(C1, C2, true, false);
  }

  /// Return logBase2 of \p C when every element is a known power of two.
  ///
  /// If \p C is a scalar/fixed width vector of known powers of 2, then this
  /// function returns a new scalar/fixed width vector obtained from logBase2 of
  /// \p C. Undef vector elements are set to zero. Return a null pointer
  /// otherwise.
  /// \param C The power-of-two constant to take the log of.
  /// @return The base-2 logarithm constant, or null if not exact.
  LLVM_ABI static Constant *getExactLogBase2(Constant *C);

  /// Return the identity constant for a binary opcode.
  ///
  /// If the binop is not commutative, callers can acquire the operand 1
  /// identity constant by setting AllowRHSConstant to true. For example, any
  /// shift has a zero identity constant for operand 1: X shift 0 = X. If this
  /// is a fadd/fsub operation and we don't care about signed zeros, then
  /// setting NSZ to true returns the identity +0.0 instead of -0.0. Return
  /// nullptr if the operator does not have an identity constant.
  /// \param Opcode The binary instruction opcode.
  /// \param Ty The type of the identity constant.
  /// \param AllowRHSConstant Whether to allow a non-commutative RHS identity.
  /// \param NSZ Whether to ignore signed-zero distinctions for FP ops.
  /// @return The identity constant for the operation, or null.
  LLVM_ABI static Constant *getBinOpIdentity(unsigned Opcode, Type *Ty,
                                             bool AllowRHSConstant = false,
                                             bool NSZ = false);

  /// Return the identity constant for intrinsic \p ID of type \p Ty.
  /// \param ID The intrinsic identifier.
  /// \param Ty The type of the identity constant.
  /// @return The identity constant for the operation, or null.
  LLVM_ABI static Constant *getIntrinsicIdentity(Intrinsic::ID ID, Type *Ty);

  /// Return the identity constant for a binary or intrinsic Instruction.
  ///
  /// The identity constant C is defined as X op C = X and C op X = X where C
  /// and X are the first two operands, and the operation is commutative.
  /// \param I The instruction whose identity is requested.
  /// \param Ty The type of the identity constant.
  /// \param AllowRHSConstant Whether to allow a non-commutative RHS identity.
  /// \param NSZ Whether to ignore signed-zero distinctions for FP ops.
  /// @return The identity constant for the operation, or null.
  LLVM_ABI static Constant *getIdentity(Instruction *I, Type *Ty,
                                        bool AllowRHSConstant = false,
                                        bool NSZ = false);

  /// Return the absorbing element for the given binary operation.
  ///
  /// That is, a constant C such that X op C = C and C op X = C for every X.
  /// For example, this returns zero for integer multiplication. If
  /// AllowLHSConstant is true, the LHS operand is a constant C that must be
  /// defined as C op X = C. It returns null if the operator doesn't have an
  /// absorbing element.
  /// \param Opcode The binary instruction opcode.
  /// \param Ty The type of the absorbing constant.
  /// \param AllowLHSConstant Whether to allow a non-commutative LHS absorber.
  /// @return The absorber constant for the binary opcode, or null.
  LLVM_ABI static Constant *getBinOpAbsorber(unsigned Opcode, Type *Ty,
                                             bool AllowLHSConstant = false);

  /// Return operand at index \p i_nocapture.
  /// \param i_nocapture The zero-based operand index.
  /// @return The operand value at that index.
  inline Constant *getOperand(unsigned i_nocapture) const;
  /// Set operand at index \p i_nocapture to \p Val_nocapture.
  /// \param i_nocapture The zero-based operand index.
  /// \param Val_nocapture The new operand value.
  inline void setOperand(unsigned i_nocapture, Constant *Val_nocapture);
  /// Return an iterator to the first operand.
  /// @return Iterator to the first operand.
  inline op_iterator op_begin();
  /// Return a const iterator to the first operand.
  /// @return Const iterator to the first operand.
  inline const_op_iterator op_begin() const;
  /// Return an iterator past the last operand.
  /// @return Iterator past the last operand.
  inline op_iterator op_end();
  /// Return a const iterator past the last operand.
  /// @return Const iterator past the last operand.
  inline const_op_iterator op_end() const;
protected:
  /// Return a reference to the operand at a compile-time index.
  /// @return Reference to the operand Use.
  template <int> inline Use &Op();
  /// Return a const reference to the operand at a compile-time index.
  /// @return Const reference to the operand Use.
  template <int> inline const Use &Op() const;
public:
  /// Return the number of operands.
  /// @return The operand count.
  inline unsigned getNumOperands() const;

  /// Convenience function for getting a Cast operation.
  ///
  /// \param ops The opcode for the conversion
  /// \param C  The constant to be converted
  /// \param Ty The type to which the constant is converted
  /// \param OnlyIfReduced see \a getWithOperands() docs.
  /// @return A cast constant expression converting \p C to \p Ty.
  LLVM_ABI static Constant *getCast(unsigned ops, Constant *C, Type *Ty,
                                    bool OnlyIfReduced = false);

  /// Create a Trunc or BitCast cast constant expression.
  /// \param C The constant to trunc or bitcast.
  /// \param Ty The type to trunc or bitcast \p C to.
  /// @return A trunc or bitcast constant expression.
  LLVM_ABI static Constant *getTruncOrBitCast(Constant *C, Type *Ty);

  /// Create a BitCast, AddrSpaceCast, or a PtrToInt cast constant expression.
  /// \param C The pointer value to be casted (operand 0).
  /// \param Ty The type to which cast should be made.
  /// @return A BitCast, AddrSpaceCast, or PtrToInt constant expression.
  LLVM_ABI static Constant *getPointerCast(Constant *C, Type *Ty);

  /// Create a BitCast or AddrSpaceCast for a pointer type depending on
  /// the address space.
  /// \param C The constant to addrspacecast or bitcast.
  /// \param Ty The type to bitcast or addrspacecast \p C to.
  /// @return A BitCast or AddrSpaceCast constant expression.
  LLVM_ABI static Constant *getPointerBitCastOrAddrSpaceCast(Constant *C,
                                                             Type *Ty);

  /// Return true if this is a convert constant expression
  /// @return True if this constant expression is a cast.
  LLVM_ABI bool isCast() const;

  /// Return a binary or shift operator constant expression, folding if possible.
  ///
  /// \param Opcode The binary or shift opcode.
  /// \param C1 The first operand.
  /// \param C2 The second operand.
  /// \param Flags Optional IR flags for the operation.
  /// \param OnlyIfReducedTy see \a getWithOperands() docs.
  /// @return A binary or shift operator constant expression, folding if possible.
  LLVM_ABI static Constant *get(unsigned Opcode, Constant *C1, Constant *C2,
                                unsigned Flags = 0,
                                Type *OnlyIfReducedTy = nullptr);

  /// Return a getelementptr constant expression over \p C.
  ///
  /// Value* is only accepted for convenience; all elements must be Constants.
  /// \param Ty The source element type for the GEP.
  /// \param C The pointer operand.
  /// \param IdxList The index operands.
  /// \param NW No-wrap flags for the GEP.
  /// \param InRange the inrange range if present or std::nullopt.
  /// \param OnlyIfReducedTy see \a getWithOperands() docs.
  /// @return A getelementptr constant expression over \p C.
  static Constant *
  getGetElementPtr(Type *Ty, Constant *C, ArrayRef<Constant *> IdxList,
                   GEPNoWrapFlags NW = GEPNoWrapFlags::none(),
                   std::optional<ConstantRange> InRange = std::nullopt,
                   Type *OnlyIfReducedTy = nullptr) {
    return getGetElementPtr(
        Ty, C, ArrayRef((Value *const *)IdxList.data(), IdxList.size()), NW,
        InRange, OnlyIfReducedTy);
  }
  /// Return a getelementptr with a single index operand \p Idx.
  /// \param Ty The source element type for the GEP.
  /// \param C The pointer operand.
  /// \param Idx The single index operand.
  /// \param NW No-wrap flags for the GEP.
  /// \param InRange the inrange range if present or std::nullopt.
  /// \param OnlyIfReducedTy see \a getWithOperands() docs.
  /// @return A getelementptr with a single index operand \p Idx.
  static Constant *
  getGetElementPtr(Type *Ty, Constant *C, Constant *Idx,
                   GEPNoWrapFlags NW = GEPNoWrapFlags::none(),
                   std::optional<ConstantRange> InRange = std::nullopt,
                   Type *OnlyIfReducedTy = nullptr) {
    // This form of the function only exists to avoid ambiguous overload
    // warnings about whether to convert Idx to ArrayRef<Constant *> or
    // ArrayRef<Value *>.
    return getGetElementPtr(Ty, C, cast<Value>(Idx), NW, InRange,
                            OnlyIfReducedTy);
  }
  /// Return a getelementptr constant expression with Value* indices.
  /// \param Ty The source element type for the GEP.
  /// \param C The pointer operand.
  /// \param IdxList The index operands.
  /// \param NW No-wrap flags for the GEP.
  /// \param InRange the inrange range if present or std::nullopt.
  /// \param OnlyIfReducedTy see \a getWithOperands() docs.
  /// @return A getelementptr constant expression with Value* indices.
  LLVM_ABI static Constant *
  getGetElementPtr(Type *Ty, Constant *C, ArrayRef<Value *> IdxList,
                   GEPNoWrapFlags NW = GEPNoWrapFlags::none(),
                   std::optional<ConstantRange> InRange = std::nullopt,
                   Type *OnlyIfReducedTy = nullptr);

  /// Create a getelementptr i8, ptr, offset constant expression.
  /// \param Ptr The pointer operand.
  /// \param Offset The byte offset to add.
  /// \param NW No-wrap flags for the GEP.
  /// \param InRange the inrange range if present or std::nullopt.
  /// \param OnlyIfReduced see \a getWithOperands() docs.
  /// @return A ptradd constant expression.
  static Constant *
  getPtrAdd(Constant *Ptr, Constant *Offset,
            GEPNoWrapFlags NW = GEPNoWrapFlags::none(),
            std::optional<ConstantRange> InRange = std::nullopt,
            Type *OnlyIfReduced = nullptr) {
    return getGetElementPtr(Type::getInt8Ty(Ptr->getContext()), Ptr, Offset, NW,
                            InRange, OnlyIfReduced);
  }

  /// Create an "inbounds" getelementptr. See the documentation for the
  /// "inbounds" flag in LangRef.html for details.
  /// \param Ty The source element type for the GEP.
  /// \param C The pointer operand.
  /// \param IdxList The index operands.
  /// @return An inbounds getelementptr constant expression.
  static Constant *getInBoundsGetElementPtr(Type *Ty, Constant *C,
                                            ArrayRef<Constant *> IdxList) {
    return getGetElementPtr(Ty, C, IdxList, GEPNoWrapFlags::inBounds());
  }
  /// Create an inbounds getelementptr with a single index operand.
  /// \param Ty The source element type for the GEP.
  /// \param C The pointer operand.
  /// \param Idx The single index operand.
  /// @return An inbounds getelementptr constant expression.
  static Constant *getInBoundsGetElementPtr(Type *Ty, Constant *C,
                                            Constant *Idx) {
    // This form of the function only exists to avoid ambiguous overload
    // warnings about whether to convert Idx to ArrayRef<Constant *> or
    // ArrayRef<Value *>.
    return getGetElementPtr(Ty, C, Idx, GEPNoWrapFlags::inBounds());
  }
  /// Create an inbounds getelementptr with Value* indices.
  /// \param Ty The source element type for the GEP.
  /// \param C The pointer operand.
  /// \param IdxList The index operands.
  /// @return An inbounds getelementptr constant expression.
  static Constant *getInBoundsGetElementPtr(Type *Ty, Constant *C,
                                            ArrayRef<Value *> IdxList) {
    return getGetElementPtr(Ty, C, IdxList, GEPNoWrapFlags::inBounds());
  }

  /// Create a getelementptr inbounds i8, ptr, offset constant expression.
  /// \param Ptr The pointer operand.
  /// \param Offset The byte offset to add.
  /// @return An inbounds ptradd constant expression.
  static Constant *getInBoundsPtrAdd(Constant *Ptr, Constant *Offset) {
    return getPtrAdd(Ptr, Offset, GEPNoWrapFlags::inBounds());
  }

  /// Return an extractelement constant expression.
  /// \param Vec The vector constant.
  /// \param Idx The element index.
  /// \param OnlyIfReducedTy If non-null, return null unless the expression reduces.
  /// @return An extractelement constant expression.
  LLVM_ABI static Constant *getExtractElement(Constant *Vec, Constant *Idx,
                                              Type *OnlyIfReducedTy = nullptr);
  /// Return an insertelement constant expression.
  /// \param Vec The vector constant.
  /// \param Elt The element value to insert.
  /// \param Idx The element index.
  /// \param OnlyIfReducedTy If non-null, return null unless the expression reduces.
  /// @return An insertelement constant expression.
  LLVM_ABI static Constant *getInsertElement(Constant *Vec, Constant *Elt,
                                             Constant *Idx,
                                             Type *OnlyIfReducedTy = nullptr);
  /// Return a shufflevector constant expression.
  /// \param V1 The first vector operand.
  /// \param V2 The second vector operand.
  /// \param Mask The shuffle mask.
  /// \param OnlyIfReducedTy If non-null, return null unless the expression reduces.
  /// @return A shufflevector constant expression.
  LLVM_ABI static Constant *getShuffleVector(Constant *V1, Constant *V2,
                                             ArrayRef<int> Mask,
                                             Type *OnlyIfReducedTy = nullptr);

  /// Return the opcode at the root of this constant expression
  /// @return The opcode of this constant expression.
  unsigned getOpcode() const { return getSubclassDataFromValue(); }

  /// Assert that this is a shufflevector and return the mask. See class
  /// ShuffleVectorInst for a description of the mask representation.
  /// @return The shuffle mask indices.
  LLVM_ABI ArrayRef<int> getShuffleMask() const;

  /// Assert that this is a shufflevector and return the mask.
  ///
  /// TODO: This is a temporary hack until we update the bitcode format for
  /// shufflevector.
  /// @return The shuffle mask constant for bitcode emission.
  LLVM_ABI Constant *getShuffleMaskForBitcode() const;

  /// Return a string representation for an opcode.
  /// @return The opcode name as a C string.
  LLVM_ABI const char *getOpcodeName() const;

  /// Return this expression with operands replaced by \p Ops.
  ///
  /// The specified array must have the same number of operands as our current
  /// one.
  /// \param Ops The replacement operands.
  /// @return A constant expression with the given operands.
  Constant *getWithOperands(ArrayRef<Constant *> Ops) const {
    return getWithOperands(Ops, getType());
  }

  /// Get the current expression with the operands replaced.
  ///
  /// Return the current constant expression with the operands replaced with \c
  /// Ops and the type with \c Ty.  The new operands must have the same number
  /// as the current ones.
  ///
  /// If \c OnlyIfReduced is \c true, nullptr will be returned unless something
  /// gets constant-folded, the type changes, or the expression is otherwise
  /// canonicalized.  This parameter should almost always be \c false.
  /// \param Ops The replacement operands.
  /// \param Ty The result type of the rebuilt expression.
  /// \param OnlyIfReduced If true, return null unless the expression reduces.
  /// \param SrcTy Optional source element type for GEP-like expressions.
  /// @return A constant expression with the given operands.
  LLVM_ABI Constant *getWithOperands(ArrayRef<Constant *> Ops, Type *Ty,
                                     bool OnlyIfReduced = false,
                                     Type *SrcTy = nullptr) const;

  /// Returns an Instruction which implements the same operation as this
  /// ConstantExpr. It is not inserted into any basic block.
  ///
  /// A better approach to this could be to have a constructor for Instruction
  /// which would take a ConstantExpr parameter, but that would have spread
  /// implementation details of ConstantExpr outside of Constants.cpp, which
  /// would make it harder to remove ConstantExprs altogether.
  /// @return A new instruction equivalent to this constant expression.
  LLVM_ABI Instruction *getAsInstruction() const;

  /// Whether creating a constant expression for this binary operator is
  /// desirable.
  /// \param Opcode The binary instruction opcode.
  /// @return True if a constant expression for \p Opcode is desirable.
  LLVM_ABI static bool isDesirableBinOp(unsigned Opcode);

  /// Whether creating a constant expression for this binary operator is
  /// supported.
  /// \param Opcode The binary instruction opcode.
  /// @return True if a constant expression for \p Opcode is supported.
  LLVM_ABI static bool isSupportedBinOp(unsigned Opcode);

  /// Whether creating a constant expression for this cast is desirable.
  /// \param Opcode The cast instruction opcode.
  /// @return True if a constant expression for \p Opcode is desirable.
  LLVM_ABI static bool isDesirableCastOp(unsigned Opcode);

  /// Whether creating a constant expression for this cast is supported.
  /// \param Opcode The cast instruction opcode.
  /// @return True if a constant expression for \p Opcode is supported.
  LLVM_ABI static bool isSupportedCastOp(unsigned Opcode);

  /// Whether creating a constant expression for this getelementptr type is
  /// supported.
  /// \param SrcElemTy The GEP source element type.
  /// @return True if a GEP constant expression over \p SrcElemTy is supported.
  static bool isSupportedGetElementPtr(const Type *SrcElemTy) {
    return !SrcElemTy->isScalableTy();
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a \c ConstantExpr.
  static bool classof(const Value *V) {
    return V->getValueID() == ConstantExprVal;
  }

private:
  // Shadow Value::setValueSubclassData with a private forwarding method so that
  // subclasses cannot accidentally use it.
  void setValueSubclassData(unsigned short D) {
    Value::setValueSubclassData(D);
  }
};

/// Operand layout traits for ConstantExpr.
/// @return Operand layout traits for ConstantExpr.
template <>
struct OperandTraits<ConstantExpr>
    : public VariadicOperandTraits<ConstantExpr> {};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(ConstantExpr, Constant)

//===----------------------------------------------------------------------===//
/// Constant representing an undefined value of a first-class type.
///
/// 'undef' values are things that do not have specified contents. These are
/// used for a variety of purposes, including global variable initializers and
/// operands to instructions. 'undef' values can occur with any first-class
/// type.
///
/// Undef values aren't exactly constants; if they have multiple uses, they can
/// appear to have different bit patterns at each use. See
/// LangRef.html#undefvalues for details.
class UndefValue : public ConstantData {
  friend class Constant;

  explicit UndefValue(Type *T) : ConstantData(T, UndefValueVal) {}

  void destroyConstantImpl();

protected:
  /// Construct an undef-like constant of type \p T with subclass ID \p vty.
  /// \param T The type of the undef value.
  /// \param vty The ValueTy subclass identifier.
  explicit UndefValue(Type *T, ValueTy vty) : ConstantData(T, vty) {}

public:
  /// UndefValue values are uniqued and cannot be copied.
  /// \param Other The constant that would be copied (deleted).
  UndefValue(const UndefValue &Other) = delete;

  /// Return an 'undef' object of the specified type.
  /// \param T The type of the undef value.
  /// @return An 'undef' object of the specified type.
  LLVM_ABI static UndefValue *get(Type *T);

  /// If this Undef has array or vector type, return a undef with the right
  /// element type.
  /// @return An undef of the element type.
  LLVM_ABI UndefValue *getSequentialElement() const;

  /// If this undef has struct type, return a undef with the right element type
  /// for the specified element.
  /// \param Elt The zero-based struct element index.
  /// @return An undef of the specified element type.
  LLVM_ABI UndefValue *getStructElement(unsigned Elt) const;

  /// Return an undef of the right value for the specified GEP index if we can,
  /// otherwise return null (e.g. if C is a ConstantExpr).
  /// \param C The GEP index constant.
  /// @return An undef of the indexed element type, or null if unavailable.
  LLVM_ABI UndefValue *getElementValue(Constant *C) const;

  /// Return an undef of the right value for the specified GEP index.
  /// \param Idx The zero-based element index.
  /// @return An undef of the indexed element type.
  LLVM_ABI UndefValue *getElementValue(unsigned Idx) const;

  /// Return the number of elements in the array, vector, or struct.
  /// @return The number of elements.
  LLVM_ABI unsigned getNumElements() const;

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a \c UndefValue.
  static bool classof(const Value *V) {
    return V->getValueID() == UndefValueVal ||
           V->getValueID() == PoisonValueVal;
  }
};

//===----------------------------------------------------------------------===//
/// Constant representing a poison value used for deferred undefined behavior.
///
/// In order to facilitate speculative execution, many instructions do not
/// invoke immediate undefined behavior when provided with illegal operands, and
/// return a poison value instead.
///
/// see LangRef.html#poisonvalues for details.
class PoisonValue final : public UndefValue {
  friend class Constant;

  explicit PoisonValue(Type *T) : UndefValue(T, PoisonValueVal) {}

  void destroyConstantImpl();

public:
  /// PoisonValue values are uniqued and cannot be copied.
  /// \param Other The constant that would be copied (deleted).
  PoisonValue(const PoisonValue &Other) = delete;

  /// Return a poison object of the specified type.
  /// \param T The type of the poison value.
  /// @return A poison object of the specified type.
  LLVM_ABI static PoisonValue *get(Type *T);

  /// If this poison has array or vector type, return a poison with the right
  /// element type.
  /// @return A poison of the element type.
  LLVM_ABI PoisonValue *getSequentialElement() const;

  /// If this poison has struct type, return a poison with the right element
  /// type for the specified element.
  /// \param Elt The zero-based struct element index.
  /// @return A poison of the specified element type.
  LLVM_ABI PoisonValue *getStructElement(unsigned Elt) const;

  /// Return a poison of the right value for the specified GEP index if we can,
  /// otherwise return null (e.g. if C is a ConstantExpr).
  /// \param C The GEP index constant.
  /// @return A poison of the indexed element type, or null if unavailable.
  LLVM_ABI PoisonValue *getElementValue(Constant *C) const;

  /// Return a poison of the right value for the specified GEP index.
  /// \param Idx The zero-based element index.
  /// @return A poison of the indexed element type.
  LLVM_ABI PoisonValue *getElementValue(unsigned Idx) const;

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a \c PoisonValue.
  static bool classof(const Value *V) {
    return V->getValueID() == PoisonValueVal;
  }
};

} // end namespace llvm

#endif // LLVM_IR_CONSTANTS_H
