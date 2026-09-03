//===- CodeGenTypes/MachineValueType.h - Machine-Level types ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the set of machine-level target independent types which
// legal values in the code generator use.
//
// Constants and properties are defined in ValueTypes.td.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEVALUETYPE_H
#define LLVM_CODEGEN_MACHINEVALUETYPE_H

#include "llvm/ADT/Sequence.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/TypeSize.h"
#include <cassert>
#include <cstdint>

namespace llvm {

  class Type;
  struct fltSemantics;
  class raw_ostream;

  /// Machine value type for types natively supported by some LLVM target.
  ///
  /// Every type that is supported natively by some processor targeted by LLVM
  /// occurs here. This means that any legal value type can be represented by an
  /// MVT.
  class MVT {
  public:
    /// Enumeration of simple (non-extended) machine value types.
    ///
    /// Simple value types that aren't explicitly part of this enumeration are
    /// considered extended value types. Concrete type enumerators are generated
    /// from ValueTypes.td via GenVT.inc.
    enum SimpleValueType : uint16_t {
      // Simple value types that aren't explicitly part of this enumeration
      // are considered extended value types.
      INVALID_SIMPLE_VALUE_TYPE = 0, ///< Sentinel for an invalid/unset simple MVT.

#define GET_VT_ATTR(Ty, sz, Any, Int, FP, Vec, Sc, Tup, NF, NElem, EltTy) Ty,
#define GET_VT_RANGES
#include "llvm/CodeGen/GenVT.inc"
#undef GET_VT_ATTR
#undef GET_VT_RANGES

      VALUETYPE_SIZE = LAST_VALUETYPE + 1, ///< One past the last simple value type.
    };

    static_assert(FIRST_VALUETYPE > 0);
    static_assert(LAST_VALUETYPE < token);

    SimpleValueType SimpleTy = INVALID_SIMPLE_VALUE_TYPE; ///< Underlying simple type.

    /// Construct an invalid/unset machine value type.
    constexpr MVT() = default;
    /// Construct an MVT from the given simple value type.
    /// @param SVT Simple value type to wrap.
    constexpr MVT(SimpleValueType SVT) : SimpleTy(SVT) {}

    /// Return true if this MVT's simple type is greater than \p S.
    /// @param S Other MVT to compare against.
    /// @return True if this MVT's simple type is greater than \p S.
    bool operator>(const MVT& S)  const { return SimpleTy >  S.SimpleTy; }
    /// Return true if this MVT's simple type is less than \p S.
    /// @param S Other MVT to compare against.
    /// @return True if this MVT's simple type is less than \p S.
    bool operator<(const MVT& S)  const { return SimpleTy <  S.SimpleTy; }
    /// Return true if this MVT's simple type equals \p S.
    /// @param S Other MVT to compare against.
    /// @return True if this MVT's simple type equals \p S.
    bool operator==(const MVT& S) const { return SimpleTy == S.SimpleTy; }
    /// Return true if this MVT's simple type differs from \p S.
    /// @param S Other MVT to compare against.
    /// @return True if this MVT's simple type differs from \p S.
    bool operator!=(const MVT& S) const { return SimpleTy != S.SimpleTy; }
    /// Return true if this MVT's simple type is greater than or equal to \p S.
    /// @param S Other MVT to compare against.
    /// @return True if this MVT's simple type is greater than or equal to \p S.
    bool operator>=(const MVT& S) const { return SimpleTy >= S.SimpleTy; }
    /// Return true if this MVT's simple type is less than or equal to \p S.
    /// @param S Other MVT to compare against.
    /// @return True if this MVT's simple type is less than or equal to \p S.
    bool operator<=(const MVT& S) const { return SimpleTy <= S.SimpleTy; }

    // Support comparison with SimpleValueType.
    /// Return true if this MVT's simple type equals \p S.
    /// @param S Simple value type to compare against.
    /// @return True if this MVT's simple type equals \p S.
    bool operator==(SimpleValueType S) const { return SimpleTy == S; }
    /// Return true if this MVT's simple type differs from \p S.
    /// @param S Simple value type to compare against.
    /// @return True if this MVT's simple type differs from \p S.
    bool operator!=(SimpleValueType S) const { return SimpleTy != S; }

    /// Support for debugging, callable in GDB: VT.dump()
    LLVM_ABI void dump() const;

    /// Implement operator<<.
    /// @param OS Stream to write this MVT to.
    LLVM_ABI void print(raw_ostream &OS) const;

    /// Return true if this is a valid simple valuetype.
    /// @return True if this is a valid simple valuetype.
    bool isValid() const {
      return (SimpleTy >= MVT::FIRST_VALUETYPE &&
              SimpleTy <= MVT::LAST_VALUETYPE);
    }

    /// Return true if this is a FP or a vector FP type.
    /// @return True if this is a floating-point or vector floating-point type.
    bool isFloatingPoint() const {
      return ((SimpleTy >= MVT::FIRST_FP_VALUETYPE &&
               SimpleTy <= MVT::LAST_FP_VALUETYPE) ||
              (SimpleTy >= MVT::FIRST_FP_FIXEDLEN_VECTOR_VALUETYPE &&
               SimpleTy <= MVT::LAST_FP_FIXEDLEN_VECTOR_VALUETYPE) ||
              (SimpleTy >= MVT::FIRST_FP_SCALABLE_VECTOR_VALUETYPE &&
               SimpleTy <= MVT::LAST_FP_SCALABLE_VECTOR_VALUETYPE));
    }

    /// Return true if this is an integer or a vector integer type.
    /// @return True if this is an integer or vector integer type.
    bool isInteger() const {
      return ((SimpleTy >= MVT::FIRST_INTEGER_VALUETYPE &&
               SimpleTy <= MVT::LAST_INTEGER_VALUETYPE) ||
              (SimpleTy >= MVT::FIRST_INTEGER_FIXEDLEN_VECTOR_VALUETYPE &&
               SimpleTy <= MVT::LAST_INTEGER_FIXEDLEN_VECTOR_VALUETYPE) ||
              (SimpleTy >= MVT::FIRST_INTEGER_SCALABLE_VECTOR_VALUETYPE &&
               SimpleTy <= MVT::LAST_INTEGER_SCALABLE_VECTOR_VALUETYPE));
    }

    /// Return true if this is an integer, not including vectors.
    /// @return True if this is a scalar integer type.
    bool isScalarInteger() const {
      return (SimpleTy >= MVT::FIRST_INTEGER_VALUETYPE &&
              SimpleTy <= MVT::LAST_INTEGER_VALUETYPE);
    }

    /// Return true if this is a vector value type.
    /// @return True if this is a vector value type.
    bool isVector() const {
      return (SimpleTy >= MVT::FIRST_VECTOR_VALUETYPE &&
              SimpleTy <= MVT::LAST_VECTOR_VALUETYPE);
    }

    /// Return true if this is a vector with matching element type.
    /// @param EltVT Expected vector element type.
    /// @return True if this is a vector with element type \p EltVT.
    bool isVectorOf(MVT EltVT) const {
      return isVector() && getVectorElementType() == EltVT;
    }

    /// Return true if this is a vector value type where the
    /// runtime length is machine dependent
    /// @return True if this is a scalable vector value type.
    bool isScalableVector() const {
      return (SimpleTy >= MVT::FIRST_SCALABLE_VECTOR_VALUETYPE &&
              SimpleTy <= MVT::LAST_SCALABLE_VECTOR_VALUETYPE);
    }

    /// Return true if this is a scalable vector with matching element type.
    /// @param EltVT Expected vector element type.
    /// @return True if this is a scalable vector with element type \p EltVT.
    bool isScalableVectorOf(MVT EltVT) const {
      return isScalableVector() && getVectorElementType() == EltVT;
    }

    /// Return true if this is a RISCV vector tuple type where the
    /// runtime length is machine dependent
    /// @return True if this is a RISC-V vector tuple type.
    bool isRISCVVectorTuple() const {
      return (SimpleTy >= MVT::FIRST_RISCV_VECTOR_TUPLE_VALUETYPE &&
              SimpleTy <= MVT::LAST_RISCV_VECTOR_TUPLE_VALUETYPE);
    }

    /// Return true if this is a custom target type that has a scalable size.
    /// @return True if this is a custom target type with scalable size.
    bool isScalableTargetExtVT() const {
      return SimpleTy == MVT::aarch64svcount || isRISCVVectorTuple();
    }

    /// Return true if the type is a scalable type.
    /// @return True if this is a scalable type.
    bool isScalableVT() const {
      return isScalableVector() || isScalableTargetExtVT();
    }

    /// Return true if this is a fixed-length vector value type.
    /// @return True if this is a fixed-length vector value type.
    bool isFixedLengthVector() const {
      return (SimpleTy >= MVT::FIRST_FIXEDLEN_VECTOR_VALUETYPE &&
              SimpleTy <= MVT::LAST_FIXEDLEN_VECTOR_VALUETYPE);
    }

    /// Return true if this is a fixed length vector with matching element type.
    /// @param EltVT Expected vector element type.
    /// @return True if this is a fixed-length vector with element type \p EltVT.
    bool isFixedLengthVectorOf(MVT EltVT) const {
      return isFixedLengthVector() && getVectorElementType() == EltVT;
    }

    /// Return true if this is a 16-bit vector type.
    /// @return True if this is a 16-bit vector type.
    bool is16BitVector() const {
      return (isFixedLengthVector() && getFixedSizeInBits() == 16);
    }

    /// Return true if this is a 32-bit vector type.
    /// @return True if this is a 32-bit vector type.
    bool is32BitVector() const {
      return (isFixedLengthVector() && getFixedSizeInBits() == 32);
    }

    /// Return true if this is a 64-bit vector type.
    /// @return True if this is a 64-bit vector type.
    bool is64BitVector() const {
      return (isFixedLengthVector() && getFixedSizeInBits() == 64);
    }

    /// Return true if this is a 128-bit vector type.
    /// @return True if this is a 128-bit vector type.
    bool is128BitVector() const {
      return (isFixedLengthVector() && getFixedSizeInBits() == 128);
    }

    /// Return true if this is a 256-bit vector type.
    /// @return True if this is a 256-bit vector type.
    bool is256BitVector() const {
      return (isFixedLengthVector() && getFixedSizeInBits() == 256);
    }

    /// Return true if this is a 512-bit vector type.
    /// @return True if this is a 512-bit vector type.
    bool is512BitVector() const {
      return (isFixedLengthVector() && getFixedSizeInBits() == 512);
    }

    /// Return true if this is a 1024-bit vector type.
    /// @return True if this is a 1024-bit vector type.
    bool is1024BitVector() const {
      return (isFixedLengthVector() && getFixedSizeInBits() == 1024);
    }

    /// Return true if this is a 2048-bit vector type.
    /// @return True if this is a 2048-bit vector type.
    bool is2048BitVector() const {
      return (isFixedLengthVector() && getFixedSizeInBits() == 2048);
    }

    /// Return true if this is a CHERI capability type.
    /// @return True if this is a CHERI capability type.
    bool isCheriCapability() const {
      return (SimpleTy >= MVT::FIRST_CHERI_CAPABILITY_VALUETYPE) &&
             (SimpleTy <= MVT::LAST_CHERI_CAPABILITY_VALUETYPE);
    }

    /// Return true if this is an overloaded type for TableGen.
    /// @return True if this is an overloaded type for TableGen.
    bool isOverloaded() const {
      switch (SimpleTy) {
#define GET_VT_ATTR(Ty, sz, Any, Int, FP, Vec, Sc, Tup, NF, NElem, EltTy)    \
    case Ty:                                                                   \
      return Any;
#include "llvm/CodeGen/GenVT.inc"
#undef GET_VT_ATTR
      default:
        return false;
      }
    }

    /// Return a vector with the same number of elements as this vector, but
    /// with the element type converted to an integer type with the same
    /// bitwidth.
    /// @return Vector MVT with integer elements of the same bit width.
    MVT changeVectorElementTypeToInteger() const {
      MVT EltTy = getVectorElementType();
      MVT IntTy = MVT::getIntegerVT(EltTy.getFixedSizeInBits());
      MVT VecTy = MVT::getVectorVT(IntTy, getVectorElementCount());
      assert(VecTy.SimpleTy != MVT::INVALID_SIMPLE_VALUE_TYPE &&
             "Simple vector VT not representable by simple integer vector VT!");
      return VecTy;
    }

    /// Return a VT for a vector type whose attributes match ourselves
    /// with the exception of the element type that is chosen by the caller.
    /// @param EltVT New element type for the vector.
    /// @return Vector MVT with the same shape and element type \p EltVT.
    MVT changeVectorElementType(MVT EltVT) const {
      MVT VecTy = MVT::getVectorVT(EltVT, getVectorElementCount());
      assert(VecTy.SimpleTy != MVT::INVALID_SIMPLE_VALUE_TYPE &&
             "Simple vector VT not representable by simple integer vector VT!");
      return VecTy;
    }

    /// Return a VT for a vector type whose attributes match ourselves with
    /// the exception of the element count that is chosen by the caller.
    /// @param EC New element count for the vector.
    /// @return Vector MVT with the same element type and element count \p EC.
    MVT changeVectorElementCount(ElementCount EC) const {
      assert(isVector() && "Not a vector MVT!");
      return MVT::getVectorVT(getVectorElementType(), EC);
    }

    /// Return a VT for a type whose attributes match ourselves with the
    /// exception of the element type that is chosen by the caller.
    /// @param EltVT New scalar or element type.
    /// @return Type with scalar or element type replaced by \p EltVT.
    MVT changeElementType(MVT EltVT) const {
      EltVT = EltVT.getScalarType();
      return isVector() ? changeVectorElementType(EltVT) : EltVT;
    }

    /// Convert this type to an equivalently sized integer type.
    ///
    /// For vectors, converts to a vector with integer element type. Similar to
    /// changeVectorElementTypeToInteger, but also handles scalars.
    /// @return Equivalently sized integer or integer-vector MVT.
    MVT changeTypeToInteger() {
      if (isVector())
        return changeVectorElementTypeToInteger();
      return MVT::getIntegerVT(getFixedSizeInBits());
    }

    /// Return a VT for a vector type with the same element type but
    /// half the number of elements.
    /// @return Vector MVT with half as many elements.
    MVT getHalfNumVectorElementsVT() const {
      MVT EltVT = getVectorElementType();
      auto EltCnt = getVectorElementCount();
      assert(EltCnt.isKnownEven() && "Splitting vector, but not in half!");
      return getVectorVT(EltVT, EltCnt.divideCoefficientBy(2));
    }

    /// Return a VT for a vector type with the same element type but
    /// double the number of elements.
    /// @return Vector MVT with twice as many elements.
    MVT getDoubleNumVectorElementsVT() const {
      MVT EltVT = getVectorElementType();
      auto EltCnt = getVectorElementCount();
      return MVT::getVectorVT(EltVT, EltCnt * 2);
    }

    /// Return an integer or integer-vector VT with element size doubled.
    /// @return Integer or integer-vector MVT with doubled element size.
    MVT widenIntegerElementType() const {
      MVT BaseTy = getScalarType();
      assert(BaseTy.isInteger() && "Not an integer or vector of integer MVT!");
      assert((BaseTy != MVT::LAST_INTEGER_VALUETYPE) &&
             "Widening of this Integer type not supported !");
      MVT SclTy = getIntegerVT(BaseTy.getScalarSizeInBits() * 2);
      assert((SclTy != MVT::INVALID_SIMPLE_VALUE_TYPE) &&
             "Failed to widen to a valid scalar MVT!");
      MVT ResTy = changeElementType(SclTy);
      assert((ResTy != MVT::INVALID_SIMPLE_VALUE_TYPE) &&
             "Failed to widen to a valid vector MVT!");
      return ResTy;
    }

    /// Returns true if the given vector is a power of 2.
    /// @return True if the vector length is a power of two.
    bool isPow2VectorType() const {
      unsigned NElts = getVectorMinNumElements();
      return !(NElts & (NElts - 1));
    }

    /// Widens the length of the given vector MVT up to the nearest power of 2
    /// and returns that type.
    /// @return This vector type widened to the next power-of-two length.
    MVT getPow2VectorType() const {
      if (isPow2VectorType())
        return *this;

      ElementCount NElts = getVectorElementCount();
      unsigned NewMinCount = 1 << Log2_32_Ceil(NElts.getKnownMinValue());
      NElts = ElementCount::get(NewMinCount, NElts.isScalable());
      return MVT::getVectorVT(getVectorElementType(), NElts);
    }

    /// If this is a vector, return the element type, otherwise return this.
    /// @return The vector element type, or this type if scalar.
    MVT getScalarType() const {
      return isVector() ? getVectorElementType() : *this;
    }

    /// Return the element type of this vector MVT.
    /// @return The element type of this vector MVT.
    MVT getVectorElementType() const {
      assert(SimpleTy >= FIRST_VALUETYPE && SimpleTy <= LAST_VALUETYPE);
      static constexpr SimpleValueType EltTyTable[] = {
#define GET_VT_ATTR(Ty, Sz, Any, Int, FP, Vec, Sc, Tup, NF, NElem, EltTy)    \
    EltTy,
#include "llvm/CodeGen/GenVT.inc"
#undef GET_VT_ATTR
      };
      SimpleValueType VT = EltTyTable[SimpleTy - FIRST_VALUETYPE];
      assert(VT != INVALID_SIMPLE_VALUE_TYPE && "Not a vector MVT!");
      return VT;
    }

    /// Given a vector type, return the minimum number of elements it contains.
    /// @return Minimum number of elements in this vector.
    unsigned getVectorMinNumElements() const {
      assert(SimpleTy >= FIRST_VALUETYPE && SimpleTy <= LAST_VALUETYPE);
      static constexpr uint16_t NElemTable[] = {
#define GET_VT_ATTR(Ty, Sz, Any, Int, FP, Vec, Sc, Tup, NF, NElem, EltTy)    \
    NElem,
#include "llvm/CodeGen/GenVT.inc"
#undef GET_VT_ATTR
      };
      unsigned NElem = NElemTable[SimpleTy - FIRST_VALUETYPE];
      assert(NElem != 0 && "Not a vector MVT!");
      return NElem;
    }

    /// Return the element count of this vector type.
    /// @return Element count of this vector type.
    ElementCount getVectorElementCount() const {
      return ElementCount::get(getVectorMinNumElements(), isScalableVector());
    }

    /// Return the number of elements in this fixed-length vector.
    ///
    /// Reports a fatal error if called on a scalable vector; use
    /// getVectorElementCount() instead.
    /// @return Number of elements in this fixed-length vector.
    unsigned getVectorNumElements() const {
      if (isScalableVector())
        llvm::reportFatalInternalError(
            "Possible incorrect use of MVT::getVectorNumElements() for "
            "scalable vector. Scalable flag may be dropped, use "
            "MVT::getVectorElementCount() instead");
      return getVectorMinNumElements();
    }

    /// Returns the size of the specified MVT in bits.
    ///
    /// If the value type is a scalable vector type, the scalable property will
    /// be set and the runtime size will be a positive integer multiple of the
    /// base size.
    /// @return Size of this MVT in bits, possibly scalable.
    TypeSize getSizeInBits() const {
      static constexpr TypeSize SizeTable[] = {
#define GET_VT_ATTR(Ty, Sz, Any, Int, FP, Vec, Sc, Tup, NF, NElem, EltTy)    \
    TypeSize(Sz, Sc || Tup || Ty == aarch64svcount /* FIXME: Not in the td.    \
                                                    */),
#include "llvm/CodeGen/GenVT.inc"
#undef GET_VT_ATTR
      };

      switch (SimpleTy) {
      case INVALID_SIMPLE_VALUE_TYPE:
        llvm_unreachable("getSizeInBits called on extended MVT.");
      case Other:
        llvm_unreachable("Value type is non-standard value, Other.");
      case iPTR:
        llvm_unreachable("Value type size is target-dependent. Ask TLI.");
      case pAny:
      case iAny:
      case fAny:
      case vAny:
      case Any:
        llvm_unreachable("Value type is overloaded.");
      case token:
        llvm_unreachable("Token type is a sentinel that cannot be used "
                         "in codegen and has no size");
      case Metadata:
        llvm_unreachable("Value type is metadata.");
      default:
        assert(SimpleTy < VALUETYPE_SIZE && "Unexpected value type!");
        return SizeTable[SimpleTy - FIRST_VALUETYPE];
      }
    }

    /// Return the size of the specified fixed width value type in bits. The
    /// function will assert if the type is scalable.
    /// @return Fixed size of this value type in bits.
    uint64_t getFixedSizeInBits() const {
      return getSizeInBits().getFixedValue();
    }

    /// Return the size in bits of the scalar type, or of each vector element.
    /// @return Size in bits of the scalar type or each vector element.
    uint64_t getScalarSizeInBits() const {
      return getScalarType().getSizeInBits().getFixedValue();
    }

    /// Return the number of bytes overwritten by a store of the specified value
    /// type.
    ///
    /// If the value type is a scalable vector type, the scalable property will
    /// be set and the runtime size will be a positive integer multiple of the
    /// base size.
    /// @return Number of bytes overwritten by a store of this value type.
    TypeSize getStoreSize() const {
      TypeSize BaseSize = getSizeInBits();
      return {(BaseSize.getKnownMinValue() + 7) / 8, BaseSize.isScalable()};
    }

    /// Return the number of bytes overwritten by a store of this value type or,
    /// for vectors, of one element.
    /// @return Store size in bytes of the scalar type or one vector element.
    uint64_t getScalarStoreSize() const {
      return getScalarType().getStoreSize().getFixedValue();
    }

    /// Return the number of bits overwritten by a store of the specified value
    /// type.
    ///
    /// If the value type is a scalable vector type, the scalable property will
    /// be set and the runtime size will be a positive integer multiple of the
    /// base size.
    /// @return Number of bits overwritten by a store of this value type.
    TypeSize getStoreSizeInBits() const {
      return getStoreSize() * 8;
    }

    /// Returns true if the number of bits for the type is a multiple of an
    /// 8-bit byte.
    /// @return True if the bit width is a multiple of 8.
    bool isByteSized() const { return getSizeInBits().isKnownMultipleOf(8); }

    /// Return true if we know at compile time this has more bits than \p VT.
    /// @param VT Type to compare bit width against.
    /// @return True if this is known at compile time to have more bits than \p VT.
    bool knownBitsGT(MVT VT) const {
      return TypeSize::isKnownGT(getSizeInBits(), VT.getSizeInBits());
    }

    /// Return true if we know at compile time this has more than or the same
    /// bits as \p VT.
    /// @param VT Type to compare bit width against.
    /// @return True if this is known at compile time to have at least as many bits as \p VT.
    bool knownBitsGE(MVT VT) const {
      return TypeSize::isKnownGE(getSizeInBits(), VT.getSizeInBits());
    }

    /// Return true if we know at compile time this has fewer bits than \p VT.
    /// @param VT Type to compare bit width against.
    /// @return True if this is known at compile time to have fewer bits than \p VT.
    bool knownBitsLT(MVT VT) const {
      return TypeSize::isKnownLT(getSizeInBits(), VT.getSizeInBits());
    }

    /// Return true if we know at compile time this has fewer than or the same
    /// bits as \p VT.
    /// @param VT Type to compare bit width against.
    /// @return True if this is known at compile time to have no more bits than \p VT.
    bool knownBitsLE(MVT VT) const {
      return TypeSize::isKnownLE(getSizeInBits(), VT.getSizeInBits());
    }

    /// Return true if this has more bits than \p VT.
    /// @param VT Type to compare bit width against.
    /// @return True if this has more bits than \p VT.
    bool bitsGT(MVT VT) const {
      assert(isScalableVector() == VT.isScalableVector() &&
             "Comparison between scalable and fixed types");
      return knownBitsGT(VT);
    }

    /// Return true if this has no less bits than \p VT.
    /// @param VT Type to compare bit width against.
    /// @return True if this has no fewer bits than \p VT.
    bool bitsGE(MVT VT) const {
      assert(isScalableVector() == VT.isScalableVector() &&
             "Comparison between scalable and fixed types");
      return knownBitsGE(VT);
    }

    /// Return true if this has less bits than \p VT.
    /// @param VT Type to compare bit width against.
    /// @return True if this has fewer bits than \p VT.
    bool bitsLT(MVT VT) const {
      assert(isScalableVector() == VT.isScalableVector() &&
             "Comparison between scalable and fixed types");
      return knownBitsLT(VT);
    }

    /// Return true if this has no more bits than \p VT.
    /// @param VT Type to compare bit width against.
    /// @return True if this has no more bits than \p VT.
    bool bitsLE(MVT VT) const {
      assert(isScalableVector() == VT.isScalableVector() &&
             "Comparison between scalable and fixed types");
      return knownBitsLE(VT);
    }

    /// Return the floating-point MVT with the given bit width.
    /// @param BitWidth Width of the floating-point type in bits.
    /// @return Floating-point MVT with the given bit width.
    static MVT getFloatingPointVT(unsigned BitWidth) {
#define GET_VT_ATTR(Ty, sz, Any, Int, FP, Vec, Sc, Tup, NF, NElem, EltTy)    \
    if (FP == 3 && sz == BitWidth)                                             \
      return Ty;
#include "llvm/CodeGen/GenVT.inc"
#undef GET_VT_ATTR

      llvm_unreachable("Bad bit width!");
    }

    /// Return the integer MVT with the given bit width, or invalid if none.
    /// @param BitWidth Width of the integer type in bits.
    /// @return Integer MVT with the given bit width, or invalid if none.
    static MVT getIntegerVT(unsigned BitWidth) {
#define GET_VT_ATTR(Ty, sz, Any, Int, FP, Vec, Sc, Tup, NF, NElem, EltTy)    \
    if (Int == 3 && sz == BitWidth)                                            \
      return Ty;
#include "llvm/CodeGen/GenVT.inc"
#undef GET_VT_ATTR

      return (MVT::SimpleValueType)(MVT::INVALID_SIMPLE_VALUE_TYPE);
    }

    /// Return a fixed-length vector MVT with element type \p VT.
    /// @param VT Element type of the vector.
    /// @param NumElements Number of elements in the vector.
    /// @return Fixed-length vector MVT, or invalid if none.
    static MVT getVectorVT(MVT VT, unsigned NumElements) {
#define GET_VT_VECATTR(Ty, Sc, Tup, nElem, ElTy)                             \
    if (!Sc && !Tup && VT.SimpleTy == ElTy && NumElements == nElem)            \
      return Ty;
#include "llvm/CodeGen/GenVT.inc"
#undef GET_VT_VECATTR

      return (MVT::SimpleValueType)(MVT::INVALID_SIMPLE_VALUE_TYPE);
    }

    /// Return a scalable vector MVT with element type \p VT.
    /// @param VT Element type of the vector.
    /// @param NumElements Minimum number of elements in the scalable vector.
    /// @return Scalable vector MVT, or invalid if none.
    static MVT getScalableVectorVT(MVT VT, unsigned NumElements) {
#define GET_VT_VECATTR(Ty, Sc, Tup, nElem, ElTy)                             \
    if (Sc && VT.SimpleTy == ElTy && NumElements == nElem)                     \
      return Ty;
#include "llvm/CodeGen/GenVT.inc"
#undef GET_VT_VECATTR

      return (MVT::SimpleValueType)(MVT::INVALID_SIMPLE_VALUE_TYPE);
    }

    /// Return the MVT for a RISC-V vector tuple of total size \p Sz with
    /// \p NFields fields.
    /// @param Sz Total size of the vector tuple in bits.
    /// @param NFields Number of fields in the tuple.
    /// @return RISC-V vector tuple MVT for the given size and field count.
    static MVT getRISCVVectorTupleVT(unsigned Sz, unsigned NFields) {
#define GET_VT_ATTR(Ty, sz, Any, Int, FP, Vec, Sc, Tup, NF, nElem, EltTy)    \
    if (Tup && sz == Sz && NF == NFields)                                      \
      return Ty;
#include "llvm/CodeGen/GenVT.inc"
#undef GET_VT_ATTR

      llvm_unreachable("Invalid RISCV vector tuple type");
    }

    /// Given a RISC-V vector tuple type, return the num_fields.
    /// @return Number of fields in this RISC-V vector tuple.
    unsigned getRISCVVectorTupleNumFields() const {
      assert(isRISCVVectorTuple() && SimpleTy >= FIRST_VALUETYPE &&
             SimpleTy <= LAST_VALUETYPE);
      static constexpr uint8_t NFTable[] = {
#define GET_VT_ATTR(Ty, Sz, Any, Int, FP, Vec, Sc, Tup, NF, NElem, EltTy) NF,
#include "llvm/CodeGen/GenVT.inc"
#undef GET_VT_ATTR
      };
      return NFTable[SimpleTy - FIRST_VALUETYPE];
    }

    /// Return a vector MVT with element type \p VT and \p NumElements elements.
    /// @param VT Element type of the vector.
    /// @param NumElements Number of elements (minimum if scalable).
    /// @param IsScalable Whether the vector is scalable.
    /// @return Vector MVT with the given element type, count, and scalability.
    static MVT getVectorVT(MVT VT, unsigned NumElements, bool IsScalable) {
      if (IsScalable)
        return getScalableVectorVT(VT, NumElements);
      return getVectorVT(VT, NumElements);
    }

    /// Return a vector MVT with element type \p VT and element count \p EC.
    /// @param VT Element type of the vector.
    /// @param EC Element count, including whether the vector is scalable.
    /// @return Vector MVT with the given element type and element count.
    static MVT getVectorVT(MVT VT, ElementCount EC) {
      if (EC.isScalable())
        return getScalableVectorVT(VT, EC.getKnownMinValue());
      return getVectorVT(VT, EC.getKnownMinValue());
    }

    /// Return the MVT corresponding to the given LLVM IR type.
    ///
    /// If \p HandleUnknown is true, unknown types are returned as Other,
    /// otherwise they are invalid. NB: This includes pointer types, which
    /// require a DataLayout to convert to a concrete value type.
    /// @param Ty LLVM IR type to convert.
    /// @param HandleUnknown If true, map unknown types to Other; else invalid.
    /// @return MVT corresponding to \p Ty.
    LLVM_ABI static MVT getVT(Type *Ty, bool HandleUnknown = false);

    /// Returns an APFloat semantics tag appropriate for the value type. If this
    /// is a vector type, the element semantics are returned.
    /// @return APFloat semantics for this value type or its element type.
    LLVM_ABI const fltSemantics &getFltSemantics() const;

  public:
    /// SimpleValueType Iteration
    /// @{
    /// @return Inclusive sequence of all simple value types.
    static auto all_valuetypes() {
      return enum_seq_inclusive(MVT::FIRST_VALUETYPE, MVT::LAST_VALUETYPE,
                                force_iteration_on_noniterable_enum);
    }

    /// Iterate over all scalar integer value types.
    /// @return Inclusive sequence of all scalar integer value types.
    static auto integer_valuetypes() {
      return enum_seq_inclusive(MVT::FIRST_INTEGER_VALUETYPE,
                                MVT::LAST_INTEGER_VALUETYPE,
                                force_iteration_on_noniterable_enum);
    }

    /// Iterate over all scalar floating-point value types.
    /// @return Inclusive sequence of all scalar floating-point value types.
    static auto fp_valuetypes() {
      return enum_seq_inclusive(MVT::FIRST_FP_VALUETYPE, MVT::LAST_FP_VALUETYPE,
                                force_iteration_on_noniterable_enum);
    }

    /// Iterate over all vector value types.
    /// @return Inclusive sequence of all vector value types.
    static auto vector_valuetypes() {
      return enum_seq_inclusive(MVT::FIRST_VECTOR_VALUETYPE,
                                MVT::LAST_VECTOR_VALUETYPE,
                                force_iteration_on_noniterable_enum);
    }

    /// Iterate over all fixed-length vector value types.
    /// @return Inclusive sequence of all fixed-length vector value types.
    static auto fixedlen_vector_valuetypes() {
      return enum_seq_inclusive(MVT::FIRST_FIXEDLEN_VECTOR_VALUETYPE,
                                MVT::LAST_FIXEDLEN_VECTOR_VALUETYPE,
                                force_iteration_on_noniterable_enum);
    }

    /// Iterate over all scalable vector value types.
    /// @return Inclusive sequence of all scalable vector value types.
    static auto scalable_vector_valuetypes() {
      return enum_seq_inclusive(MVT::FIRST_SCALABLE_VECTOR_VALUETYPE,
                                MVT::LAST_SCALABLE_VECTOR_VALUETYPE,
                                force_iteration_on_noniterable_enum);
    }

    /// Iterate over all fixed-length integer vector value types.
    /// @return Inclusive sequence of all fixed-length integer vector value types.
    static auto integer_fixedlen_vector_valuetypes() {
      return enum_seq_inclusive(MVT::FIRST_INTEGER_FIXEDLEN_VECTOR_VALUETYPE,
                                MVT::LAST_INTEGER_FIXEDLEN_VECTOR_VALUETYPE,
                                force_iteration_on_noniterable_enum);
    }

    /// Iterate over all fixed-length floating-point vector value types.
    /// @return Inclusive sequence of all fixed-length floating-point vector value types.
    static auto fp_fixedlen_vector_valuetypes() {
      return enum_seq_inclusive(MVT::FIRST_FP_FIXEDLEN_VECTOR_VALUETYPE,
                                MVT::LAST_FP_FIXEDLEN_VECTOR_VALUETYPE,
                                force_iteration_on_noniterable_enum);
    }

    /// Iterate over all scalable integer vector value types.
    /// @return Inclusive sequence of all scalable integer vector value types.
    static auto integer_scalable_vector_valuetypes() {
      return enum_seq_inclusive(MVT::FIRST_INTEGER_SCALABLE_VECTOR_VALUETYPE,
                                MVT::LAST_INTEGER_SCALABLE_VECTOR_VALUETYPE,
                                force_iteration_on_noniterable_enum);
    }

    /// Iterate over all scalable floating-point vector value types.
    /// @return Inclusive sequence of all scalable floating-point vector value types.
    static auto fp_scalable_vector_valuetypes() {
      return enum_seq_inclusive(MVT::FIRST_FP_SCALABLE_VECTOR_VALUETYPE,
                                MVT::LAST_FP_SCALABLE_VECTOR_VALUETYPE,
                                force_iteration_on_noniterable_enum);
    }

    /// Iterate over all CHERI capability value types.
    /// @return Inclusive sequence of all CHERI capability value types.
    static auto cheri_capability_valuetypes() {
      return enum_seq_inclusive(MVT::FIRST_CHERI_CAPABILITY_VALUETYPE,
                                MVT::LAST_CHERI_CAPABILITY_VALUETYPE,
                                force_iteration_on_noniterable_enum);
    }
    /// @}
  };

  /// Print \p VT to \p OS.
  /// @param OS Stream to write to.
  /// @param VT Machine value type to print.
  /// @return Reference to \p OS after printing.
  inline raw_ostream &operator<<(raw_ostream &OS, const MVT &VT) {
    VT.print(OS);
    return OS;
  }

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINEVALUETYPE_H
