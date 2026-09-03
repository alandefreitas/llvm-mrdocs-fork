//===- CodeGen/ValueTypes.h - Low-Level Target independ. types --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the set of low-level target independent types which various
// values in the code generator are.  This allows the target specific behavior
// of instructions to be described to target independent passes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_VALUETYPES_H
#define LLVM_CODEGEN_VALUETYPES_H

#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/TypeSize.h"
#include <cassert>
#include <cstdint>
#include <string>

namespace llvm {

  class LLVMContext;
  class Type;
  struct fltSemantics;

  /// Extended Value Type. Capable of holding value types which are not native
  /// for any processor (such as the i12345 type), as well as the types an MVT
  /// can represent.
  struct EVT {
  private:
    MVT V = MVT::INVALID_SIMPLE_VALUE_TYPE;
    Type *LLVMTy = nullptr;

  public:
    /// Construct an invalid EVT.
    constexpr EVT() = default;
    /// Construct an EVT from a simple value type.
    ///
    /// \param SVT Simple value type to wrap.
    constexpr EVT(MVT::SimpleValueType SVT) : V(SVT) {}
    /// Construct an EVT from a machine value type.
    ///
    /// \param S Machine value type to wrap.
    constexpr EVT(MVT S) : V(S) {}

    /// Return true if this EVT equals \p VT.
    ///
    /// \param VT Value type to compare against.
    /// \return True if the EVTs are equal.
    bool operator==(EVT VT) const {
      return !(*this != VT);
    }
    /// Return true if this EVT differs from \p VT.
    ///
    /// \param VT Value type to compare against.
    /// \return True if the EVTs differ.
    bool operator!=(EVT VT) const {
      return V.SimpleTy != VT.V.SimpleTy || LLVMTy != VT.LLVMTy;
    }

    /// Returns the EVT that represents a floating-point type with the given
    /// number of bits.
    ///
    /// There are two floating-point types with 128 bits - this returns f128
    /// rather than ppcf128.
    ///
    /// \param BitWidth Width of the floating-point type in bits.
    /// \return Floating-point EVT of the given bit width.
    static EVT getFloatingPointVT(unsigned BitWidth) {
      return MVT::getFloatingPointVT(BitWidth);
    }

    /// Returns the EVT that represents an integer with the given number of
    /// bits.
    ///
    /// \param Context LLVM context used to create extended integer types.
    /// \param BitWidth Width of the integer type in bits.
    /// \return Integer EVT of the given bit width.
    static EVT getIntegerVT(LLVMContext &Context, unsigned BitWidth) {
      MVT M = MVT::getIntegerVT(BitWidth);
      if (M.SimpleTy != MVT::INVALID_SIMPLE_VALUE_TYPE)
        return M;
      return getExtendedIntegerVT(Context, BitWidth);
    }

    /// Returns the EVT that represents a vector NumElements in length, where
    /// each element is of type VT.
    ///
    /// \param Context LLVM context used to create extended vector types.
    /// \param VT Element type of the vector.
    /// \param NumElements Number of elements in the vector.
    /// \param IsScalable True if the vector is scalable.
    /// \return Vector EVT with the given element type and length.
    static EVT getVectorVT(LLVMContext &Context, EVT VT, unsigned NumElements,
                           bool IsScalable = false) {
      MVT M = MVT::getVectorVT(VT.V, NumElements, IsScalable);
      if (M.SimpleTy != MVT::INVALID_SIMPLE_VALUE_TYPE)
        return M;
      return getExtendedVectorVT(Context, VT, NumElements, IsScalable);
    }

    /// Returns the EVT that represents a vector EC.Min elements in length,
    /// where each element is of type VT.
    ///
    /// \param Context LLVM context used to create extended vector types.
    /// \param VT Element type of the vector.
    /// \param EC Element count of the vector.
    /// \return Vector EVT with the given element type and element count.
    static EVT getVectorVT(LLVMContext &Context, EVT VT, ElementCount EC) {
      MVT M = MVT::getVectorVT(VT.V, EC);
      if (M.SimpleTy != MVT::INVALID_SIMPLE_VALUE_TYPE)
        return M;
      return getExtendedVectorVT(Context, VT, EC);
    }

    /// Return a vector with the same number of elements as this vector, but
    /// with the element type converted to an integer type with the same
    /// bitwidth.
    /// \return Vector EVT with integer element type of the same bit width.
    EVT changeVectorElementTypeToInteger() const {
      if (isSimple())
        return getSimpleVT().changeVectorElementTypeToInteger();
      return changeExtendedVectorElementTypeToInteger();
    }

    /// Return a VT for a vector type whose attributes match ourselves
    /// with the exception of the element type that is chosen by the caller.
    ///
    /// \param Context LLVM context used to create extended types.
    /// \param EltVT New vector element type.
    /// \return Vector EVT with the same shape and the given element type.
    EVT changeVectorElementType(LLVMContext &Context, EVT EltVT) const {
      if (isSimple() && EltVT.isSimple()) {
        MVT M = MVT::getVectorVT(EltVT.getSimpleVT(), getVectorElementCount());
        if (M != MVT::INVALID_SIMPLE_VALUE_TYPE)
          return M;
      }
      return getVectorVT(Context, EltVT, getVectorElementCount());
    }

    /// Return a VT for a vector type whose attributes match ourselves
    /// with the exception of the element count that is chosen by the caller.
    ///
    /// \param Context LLVM context used to create extended types.
    /// \param EC New vector element count.
    /// \return Vector EVT with the same element type and the given element count.
    EVT changeVectorElementCount(LLVMContext &Context, ElementCount EC) const {
      assert(isVector() && "Not a vector EVT!");
      if (isSimple()) {
        MVT M = getSimpleVT().changeVectorElementCount(EC);
        if (M != MVT::INVALID_SIMPLE_VALUE_TYPE)
          return M;
      }
      return getVectorVT(Context, getVectorElementType(), EC);
    }

    /// Return a VT for a type whose attributes match ourselves with the
    /// exception of the element type that is chosen by the caller.
    ///
    /// \param Context LLVM context used to create extended types.
    /// \param EltVT New scalar or vector element type.
    /// \return Scalar or vector EVT with the given element type.
    EVT changeElementType(LLVMContext &Context, EVT EltVT) const {
      EltVT = EltVT.getScalarType();
      return isVector() ? changeVectorElementType(Context, EltVT) : EltVT;
    }

    /// Return the type converted to an equivalently sized integer or vector
    /// with integer element type.
    ///
    /// Similar to changeVectorElementTypeToInteger, but also handles scalars.
    /// \return Integer or integer-vector EVT of equivalent size.
    EVT changeTypeToInteger() const {
      if (isVector())
        return changeVectorElementTypeToInteger();

      if (isSimple())
        return getSimpleVT().changeTypeToInteger();
      return changeExtendedTypeToInteger();
    }

    /// Test if the given EVT has zero size, this will fail if called on a
    /// scalable type
    /// \return True if the type has zero size.
    bool isZeroSized() const {
      return getSizeInBits().isZero();
    }

    /// Test if the given EVT is simple (as opposed to being extended).
    /// \return True if this is a simple (non-extended) EVT.
    bool isSimple() const {
      return V.SimpleTy != MVT::INVALID_SIMPLE_VALUE_TYPE;
    }

    /// Test if the given EVT is extended (as opposed to being simple).
    /// \return True if this is an extended EVT.
    bool isExtended() const {
      return !isSimple();
    }

    /// Return true if this is a FP or a vector FP type.
    /// \return True if this is a floating-point or vector floating-point type.
    bool isFloatingPoint() const {
      return isSimple() ? V.isFloatingPoint() : isExtendedFloatingPoint();
    }

    /// Return true if this is an integer or a vector integer type.
    /// \return True if this is an integer or vector integer type.
    bool isInteger() const {
      return isSimple() ? V.isInteger() : isExtendedInteger();
    }

    /// Return true if this is an integer, but not a vector.
    /// \return True if this is a non-vector integer type.
    bool isScalarInteger() const {
      return isSimple() ? V.isScalarInteger() : isExtendedScalarInteger();
    }

    /// Return true if this is a vector type where the runtime
    /// length is machine dependent
    /// \return True if this is a scalable target-extension value type.
    bool isScalableTargetExtVT() const {
      return isSimple() && V.isScalableTargetExtVT();
    }

    /// Return true if this is a vector value type.
    /// \return True if this is a vector type.
    bool isVector() const {
      return isSimple() ? V.isVector() : isExtendedVector();
    }

    /// Return true if this is a vector with matching element type.
    ///
    /// \param EltVT Element type to match.
    /// \return True if this is a vector with the given element type.
    bool isVectorOf(EVT EltVT) const {
      return isVector() && getVectorElementType() == EltVT;
    }

    /// Return true if this is a vector type where the runtime
    /// length is machine dependent
    /// \return True if this is a scalable vector type.
    bool isScalableVector() const {
      return isSimple() ? V.isScalableVector() : isExtendedScalableVector();
    }

    /// Return true if this is a scalable vector with matching element type.
    ///
    /// \param EltVT Element type to match.
    /// \return True if this is a scalable vector with the given element type.
    bool isScalableVectorOf(EVT EltVT) const {
      return isScalableVector() && getVectorElementType() == EltVT;
    }

    /// Return true if this is a vector value type.
    /// \return True if this is a RISC-V vector tuple type.
    bool isRISCVVectorTuple() const { return V.isRISCVVectorTuple(); }

    /// Return true if this is a fixed-length vector type.
    /// \return True if this is a fixed-length vector type.
    bool isFixedLengthVector() const {
      return isSimple() ? V.isFixedLengthVector()
                        : isExtendedFixedLengthVector();
    }

    /// Return true if this is a fixed length vector with matching element type.
    ///
    /// \param EltVT Element type to match.
    /// \return True if this is a fixed-length vector with the given element type.
    bool isFixedLengthVectorOf(EVT EltVT) const {
      return isFixedLengthVector() && getVectorElementType() == EltVT;
    }

    /// Return true if the type is a scalable type.
    /// \return True if this is a scalable vector or scalable target-ext type.
    bool isScalableVT() const {
      return isScalableVector() || isScalableTargetExtVT();
    }

    /// Return true if this is a 16-bit vector type.
    /// \return True if this is a 16-bit vector type.
    bool is16BitVector() const {
      return isSimple() ? V.is16BitVector() : isExtended16BitVector();
    }

    /// Return true if this is a 32-bit vector type.
    /// \return True if this is a 32-bit vector type.
    bool is32BitVector() const {
      return isSimple() ? V.is32BitVector() : isExtended32BitVector();
    }

    /// Return true if this is a 64-bit vector type.
    /// \return True if this is a 64-bit vector type.
    bool is64BitVector() const {
      return isSimple() ? V.is64BitVector() : isExtended64BitVector();
    }

    /// Return true if this is a 128-bit vector type.
    /// \return True if this is a 128-bit vector type.
    bool is128BitVector() const {
      return isSimple() ? V.is128BitVector() : isExtended128BitVector();
    }

    /// Return true if this is a 256-bit vector type.
    /// \return True if this is a 256-bit vector type.
    bool is256BitVector() const {
      return isSimple() ? V.is256BitVector() : isExtended256BitVector();
    }

    /// Return true if this is a 512-bit vector type.
    /// \return True if this is a 512-bit vector type.
    bool is512BitVector() const {
      return isSimple() ? V.is512BitVector() : isExtended512BitVector();
    }

    /// Return true if this is a 1024-bit vector type.
    /// \return True if this is a 1024-bit vector type.
    bool is1024BitVector() const {
      return isSimple() ? V.is1024BitVector() : isExtended1024BitVector();
    }

    /// Return true if this is a 2048-bit vector type.
    /// \return True if this is a 2048-bit vector type.
    bool is2048BitVector() const {
      return isSimple() ? V.is2048BitVector() : isExtended2048BitVector();
    }

    /// Return true if this is a capability type.
    /// \return True if this is a CHERI capability type.
    bool isCheriCapability() const {
      return isSimple() ? V.isCheriCapability() : false;
    }

    /// Return true if this is an overloaded type for TableGen.
    /// \return True if this is an overloaded TableGen type.
    bool isOverloaded() const {
      return (V == MVT::iAny || V == MVT::fAny || V == MVT::vAny ||
              V == MVT::pAny);
    }

    /// Return true if the bit size is a multiple of 8.
    /// \return True if the bit size is a multiple of 8.
    bool isByteSized() const {
      return !isZeroSized() && getSizeInBits().isKnownMultipleOf(8);
    }

    /// Return true if the size is a power-of-two number of bytes.
    /// \return True if the size is a power-of-two number of bytes.
    bool isRound() const {
      if (isScalableVector())
        return false;
      unsigned BitSize = getSizeInBits();
      return BitSize >= 8 && !(BitSize & (BitSize - 1));
    }

    /// Return true if this has the same number of bits as VT.
    ///
    /// \param VT Value type to compare against.
    /// \return True if both types have the same bit size.
    bool bitsEq(EVT VT) const {
      if (EVT::operator==(VT)) return true;
      return getSizeInBits() == VT.getSizeInBits();
    }

    /// Return true if we know at compile time this has more bits than VT.
    ///
    /// \param VT Value type to compare against.
    /// \return True if this type is known to have more bits than VT.
    bool knownBitsGT(EVT VT) const {
      return TypeSize::isKnownGT(getSizeInBits(), VT.getSizeInBits());
    }

    /// Return true if we know at compile time this has more than or the same
    /// bits as VT.
    ///
    /// \param VT Value type to compare against.
    /// \return True if this type is known to have at least as many bits as VT.
    bool knownBitsGE(EVT VT) const {
      return TypeSize::isKnownGE(getSizeInBits(), VT.getSizeInBits());
    }

    /// Return true if we know at compile time this has fewer bits than VT.
    ///
    /// \param VT Value type to compare against.
    /// \return True if this type is known to have fewer bits than VT.
    bool knownBitsLT(EVT VT) const {
      return TypeSize::isKnownLT(getSizeInBits(), VT.getSizeInBits());
    }

    /// Return true if we know at compile time this has fewer than or the same
    /// bits as VT.
    ///
    /// \param VT Value type to compare against.
    /// \return True if this type is known to have no more bits than VT.
    bool knownBitsLE(EVT VT) const {
      return TypeSize::isKnownLE(getSizeInBits(), VT.getSizeInBits());
    }

    /// Return true if this has more bits than VT.
    ///
    /// \param VT Value type to compare against.
    /// \return True if this type has more bits than VT.
    bool bitsGT(EVT VT) const {
      if (EVT::operator==(VT)) return false;
      assert(isScalableVector() == VT.isScalableVector() &&
             "Comparison between scalable and fixed types");
      return knownBitsGT(VT);
    }

    /// Return true if this has no less bits than VT.
    ///
    /// \param VT Value type to compare against.
    /// \return True if this type has no fewer bits than VT.
    bool bitsGE(EVT VT) const {
      if (EVT::operator==(VT)) return true;
      assert(isScalableVector() == VT.isScalableVector() &&
             "Comparison between scalable and fixed types");
      return knownBitsGE(VT);
    }

    /// Return true if this has less bits than VT.
    ///
    /// \param VT Value type to compare against.
    /// \return True if this type has fewer bits than VT.
    bool bitsLT(EVT VT) const {
      if (EVT::operator==(VT)) return false;
      assert(isScalableVector() == VT.isScalableVector() &&
             "Comparison between scalable and fixed types");
      return knownBitsLT(VT);
    }

    /// Return true if this has no more bits than VT.
    ///
    /// \param VT Value type to compare against.
    /// \return True if this type has no more bits than VT.
    bool bitsLE(EVT VT) const {
      if (EVT::operator==(VT)) return true;
      assert(isScalableVector() == VT.isScalableVector() &&
             "Comparison between scalable and fixed types");
      return knownBitsLE(VT);
    }

    /// Return the SimpleValueType held in the specified simple EVT.
    /// \return The simple machine value type.
    MVT getSimpleVT() const {
      assert(isSimple() && "Expected a SimpleValueType!");
      return V;
    }

    /// If this is a vector type, return the element type, otherwise return
    /// this.
    /// \return The element type if a vector, otherwise this type.
    EVT getScalarType() const {
      return isVector() ? getVectorElementType() : *this;
    }

    /// Given a vector type, return the type of each element.
    /// \return The element type of this vector.
    EVT getVectorElementType() const {
      assert(isVector() && "Invalid vector type!");
      if (isSimple())
        return V.getVectorElementType();
      return getExtendedVectorElementType();
    }

    /// Given a vector type, return the number of elements it contains.
    /// \return The number of elements in this fixed-length vector.
    unsigned getVectorNumElements() const {
      assert(isVector() && "Invalid vector type!");

      if (isScalableVector())
        llvm::reportFatalInternalError(
            "Possible incorrect use of EVT::getVectorNumElements() for "
            "scalable vector. Scalable flag may be dropped, use "
            "EVT::getVectorElementCount() instead");

      return isSimple() ? V.getVectorNumElements()
                        : getExtendedVectorNumElements();
    }

    /// Given a (possibly scalable) vector type, return the ElementCount.
    /// \return The element count of this vector type.
    ElementCount getVectorElementCount() const {
      assert((isVector()) && "Invalid vector type!");
      if (isSimple())
        return V.getVectorElementCount();

      return getExtendedVectorElementCount();
    }

    /// Given a vector type, return the minimum number of elements it contains.
    /// \return The minimum number of elements in this vector.
    unsigned getVectorMinNumElements() const {
      return getVectorElementCount().getKnownMinValue();
    }

    /// Given a RISCV vector tuple type, return the num_fields.
    /// \return The number of fields in this RISC-V vector tuple.
    unsigned getRISCVVectorTupleNumFields() const {
      return V.getRISCVVectorTupleNumFields();
    }

    /// Return the size of the specified value type in bits.
    ///
    /// If the value type is a scalable vector type, the scalable property will
    /// be set and the runtime size will be a positive integer multiple of the
    /// base size.
    /// \return The size of this type in bits.
    TypeSize getSizeInBits() const {
      if (isSimple())
        return V.getSizeInBits();
      return getExtendedSizeInBits();
    }

    /// Return the size of the specified fixed width value type in bits. The
    /// function will assert if the type is scalable.
    /// \return The fixed bit size of this type.
    uint64_t getFixedSizeInBits() const {
      return getSizeInBits().getFixedValue();
    }

    /// Return the bit size of this type, or of its element type if a vector.
    /// \return The bit size of this type, or of its element type if a vector.
    uint64_t getScalarSizeInBits() const {
      return getScalarType().getSizeInBits().getFixedValue();
    }

    /// Return the number of bytes overwritten by a store of the specified value
    /// type.
    ///
    /// If the value type is a scalable vector type, the scalable property will
    /// be set and the runtime size will be a positive integer multiple of the
    /// base size.
    /// \return The store size of this type in bytes.
    TypeSize getStoreSize() const {
      TypeSize BaseSize = getSizeInBits();
      return {(BaseSize.getKnownMinValue() + 7) / 8, BaseSize.isScalable()};
    }

    /// Return the number of bytes overwritten by a store of this value type or
    /// this value type's element type in the case of a vector.
    /// \return The store size in bytes of this type or its element type.
    uint64_t getScalarStoreSize() const {
      return getScalarType().getStoreSize().getFixedValue();
    }

    /// Return the number of bits overwritten by a store of the specified value
    /// type.
    ///
    /// If the value type is a scalable vector type, the scalable property will
    /// be set and the runtime size will be a positive integer multiple of the
    /// base size.
    /// \return The store size of this type in bits.
    TypeSize getStoreSizeInBits() const {
      return getStoreSize() * 8;
    }

    /// Rounds the bit-width of the given integer EVT up to the nearest power of
    /// two (and at least to eight), and returns the integer EVT with that
    /// number of bits.
    ///
    /// \param Context LLVM context used to create extended integer types.
    /// \return An integer EVT with bit width rounded up to a power of two.
    EVT getRoundIntegerType(LLVMContext &Context) const {
      assert(isInteger() && !isVector() && "Invalid integer type!");
      unsigned BitWidth = getSizeInBits();
      if (BitWidth <= 8)
        return EVT(MVT::i8);
      return getIntegerVT(Context, llvm::bit_ceil(BitWidth));
    }

    /// Finds the smallest simple value type that is greater than or equal to
    /// half the width of this EVT.
    ///
    /// If no simple value type can be found, an extended integer value type of
    /// half the size (rounded up) is returned.
    ///
    /// \param Context LLVM context used to create extended integer types.
    /// \return An integer EVT at least half as wide as this type.
    EVT getHalfSizedIntegerVT(LLVMContext &Context) const {
      assert(isInteger() && !isVector() && "Invalid integer type!");
      unsigned EVTSize = getSizeInBits();
      for (unsigned IntVT = MVT::FIRST_INTEGER_VALUETYPE;
          IntVT <= MVT::LAST_INTEGER_VALUETYPE; ++IntVT) {
        EVT HalfVT = EVT((MVT::SimpleValueType)IntVT);
        if (HalfVT.getSizeInBits() * 2 >= EVTSize)
          return HalfVT;
      }
      return getIntegerVT(Context, (EVTSize + 1) / 2);
    }

    /// Return a VT for an integer element type with doubled bit width.
    /// The type returned may be an extended type.
    ///
    /// \param Context LLVM context used to create extended types.
    /// \return An EVT with integer elements of doubled bit width.
    EVT widenIntegerElementType(LLVMContext &Context) const {
      unsigned EVTSize = getScalarSizeInBits();
      EVT EltVT = EVT::getIntegerVT(Context, 2 * EVTSize);
      return changeElementType(Context, EltVT);
    }

    /// Return a VT for an integer vector type with the size of the
    /// elements doubled. The type returned may be an extended type.
    ///
    /// \param Context LLVM context used to create extended types.
    /// \return An integer vector EVT with doubled element bit width.
    EVT widenIntegerVectorElementType(LLVMContext &Context) const {
      EVT EltVT = getVectorElementType();
      EltVT = EVT::getIntegerVT(Context, 2 * EltVT.getSizeInBits());
      return EVT::getVectorVT(Context, EltVT, getVectorElementCount());
    }

    /// Return a VT for a vector type with the same element type but
    /// half the number of elements.
    ///
    /// The type returned may be an extended type.
    ///
    /// \param Context LLVM context used to create extended types.
    /// \return A vector EVT with half as many elements.
    EVT getHalfNumVectorElementsVT(LLVMContext &Context) const {
      EVT EltVT = getVectorElementType();
      auto EltCnt = getVectorElementCount();
      assert(EltCnt.isKnownEven() && "Splitting vector, but not in half!");
      return EVT::getVectorVT(Context, EltVT, EltCnt.divideCoefficientBy(2));
    }

    /// Return a VT for a vector type with the same element type but
    /// double the number of elements.
    ///
    /// The type returned may be an extended type.
    ///
    /// \param Context LLVM context used to create extended types.
    /// \return A vector EVT with twice as many elements.
    EVT getDoubleNumVectorElementsVT(LLVMContext &Context) const {
      EVT EltVT = getVectorElementType();
      auto EltCnt = getVectorElementCount();
      return EVT::getVectorVT(Context, EltVT, EltCnt * 2);
    }

    /// Returns true if the given vector is a power of 2.
    /// \return True if the vector length is a power of two.
    bool isPow2VectorType() const {
      unsigned NElts = getVectorMinNumElements();
      return !(NElts & (NElts - 1));
    }

    /// Widens the length of the given vector EVT up to the nearest power of 2
    /// and returns that type.
    ///
    /// \param Context LLVM context used to create extended types.
    /// \return A vector EVT with length rounded up to a power of two.
    EVT getPow2VectorType(LLVMContext &Context) const {
      if (!isPow2VectorType()) {
        ElementCount NElts = getVectorElementCount();
        unsigned NewMinCount = 1 << Log2_32_Ceil(NElts.getKnownMinValue());
        NElts = ElementCount::get(NewMinCount, NElts.isScalable());
        return EVT::getVectorVT(Context, getVectorElementType(), NElts);
      }
      else {
        return *this;
      }
    }

    /// This function returns value type as a string, e.g. "i32".
    /// \return A string representation of this value type.
    LLVM_ABI std::string getEVTString() const;

    /// Support for debugging, callable in GDB: VT.dump()
    LLVM_ABI void dump() const;

    /// Implement operator<<.
    ///
    /// \param OS Stream to print to.
    void print(raw_ostream &OS) const {
      OS << getEVTString();
    }

    /// This method returns an LLVM type corresponding to the specified EVT.
    ///
    /// For integer types, this returns an unsigned type. Note that this will
    /// abort for types that cannot be represented.
    ///
    /// \param Context LLVM context used to create the type.
    /// \return The LLVM Type corresponding to this EVT.
    LLVM_ABI Type *getTypeForEVT(LLVMContext &Context) const;

    /// Return the value type corresponding to the specified type.
    ///
    /// If HandleUnknown is true, unknown types are returned as Other,
    /// otherwise they are invalid.
    /// NB: This includes pointer types, which require a DataLayout to convert
    /// to a concrete value type.
    ///
    /// \param Ty LLVM type to convert.
    /// \param HandleUnknown If true, unknown types are returned as Other;
    /// otherwise they are invalid.
    /// \return The EVT corresponding to the given LLVM type.
    LLVM_ABI static EVT getEVT(Type *Ty, bool HandleUnknown = false);

    /// Return the raw simple type or LLVM type pointer identifying this EVT.
    /// \return The raw simple type or LLVM type pointer for this EVT.
    intptr_t getRawBits() const {
      if (isSimple())
        return V.SimpleTy;
      else
        return (intptr_t)(LLVMTy);
    }

    /// A meaningless but well-behaved order, useful for constructing
    /// containers.
    struct compareRawBits {
      /// Return true if \p L orders before \p R by simple type then LLVM type.
      ///
      /// \param L Left-hand EVT to compare.
      /// \param R Right-hand EVT to compare.
      /// \return True if L orders before R by simple type then LLVM type.
      bool operator()(EVT L, EVT R) const {
        if (L.V.SimpleTy == R.V.SimpleTy)
          return L.LLVMTy < R.LLVMTy;
        else
          return L.V.SimpleTy < R.V.SimpleTy;
      }
    };

    /// Returns an APFloat semantics tag appropriate for the value type. If this
    /// is a vector type, the element semantics are returned.
    /// \return APFloat semantics for this type or its element type.
    LLVM_ABI const fltSemantics &getFltSemantics() const;

  private:
    // Methods for handling the Extended-type case in functions above.
    // These are all out-of-line to prevent users of this header file
    // from having a dependency on Type.h.
    LLVM_ABI EVT changeExtendedTypeToInteger() const;
    LLVM_ABI EVT changeExtendedVectorElementType(EVT EltVT) const;
    LLVM_ABI EVT changeExtendedVectorElementTypeToInteger() const;
    LLVM_ABI static EVT getExtendedIntegerVT(LLVMContext &C, unsigned BitWidth);
    LLVM_ABI static EVT getExtendedVectorVT(LLVMContext &C, EVT VT,
                                            unsigned NumElements,
                                            bool IsScalable);
    LLVM_ABI static EVT getExtendedVectorVT(LLVMContext &Context, EVT VT,
                                            ElementCount EC);
    LLVM_ABI bool isExtendedFloatingPoint() const LLVM_READONLY;
    LLVM_ABI bool isExtendedInteger() const LLVM_READONLY;
    LLVM_ABI bool isExtendedScalarInteger() const LLVM_READONLY;
    LLVM_ABI bool isExtendedVector() const LLVM_READONLY;
    LLVM_ABI bool isExtended16BitVector() const LLVM_READONLY;
    LLVM_ABI bool isExtended32BitVector() const LLVM_READONLY;
    LLVM_ABI bool isExtended64BitVector() const LLVM_READONLY;
    LLVM_ABI bool isExtended128BitVector() const LLVM_READONLY;
    LLVM_ABI bool isExtended256BitVector() const LLVM_READONLY;
    LLVM_ABI bool isExtended512BitVector() const LLVM_READONLY;
    LLVM_ABI bool isExtended1024BitVector() const LLVM_READONLY;
    LLVM_ABI bool isExtended2048BitVector() const LLVM_READONLY;
    LLVM_ABI bool isExtendedFixedLengthVector() const LLVM_READONLY;
    LLVM_ABI bool isExtendedScalableVector() const LLVM_READONLY;
    LLVM_ABI EVT getExtendedVectorElementType() const;
    LLVM_ABI unsigned getExtendedVectorNumElements() const LLVM_READONLY;
    LLVM_ABI ElementCount getExtendedVectorElementCount() const LLVM_READONLY;
    LLVM_ABI TypeSize getExtendedSizeInBits() const LLVM_READONLY;
  };

  /// Print \p V to \p OS.
  ///
  /// \param OS Stream to print to.
  /// \param V Value type to print.
  /// \return The output stream OS.
  inline raw_ostream &operator<<(raw_ostream &OS, const EVT &V) {
    V.print(OS);
    return OS;
  }
} // end namespace llvm

#endif // LLVM_CODEGEN_VALUETYPES_H
