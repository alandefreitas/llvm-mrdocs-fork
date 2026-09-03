//== llvm/CodeGenTypes/LowLevelType.h -------------------------- -*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// Implement a low-level type suitable for MachineInstr level instruction
/// selection.
///
/// For a type attached to a MachineInstr, we care about total
/// size, the number of vector lanes (if any)
/// and the kind of the type (anyscalar, integer, float and etc).
/// Floating point are filled with APFloat::Semantics to make them
/// distinguishable.
///
/// Earlier other information required for correct selection was expected to be
/// carried only by the opcode, or non-type flags. For example the distinction
/// between G_ADD and G_FADD for int/float or fast-math flags.
///
/// Now we also able to rely on the kind of the type.
/// This may be useful to distinguish different types of the same size used at
/// the same opcode, for example, G_FADD with half vs G_FADD with bfloat16.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_LOWLEVELTYPE_H
#define LLVM_CODEGEN_LOWLEVELTYPE_H

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/bit.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>

namespace llvm {

class Type;
class raw_ostream;

/// Low-level type used at MachineInstr selection time, capturing size, vector
/// lane count, and kind (scalar/integer/float/pointer and vector variants).
class LLT {
public:
  /// Floating-point semantics type used by LLT float kinds.
  using FpSemantics = APFloat::Semantics;

  /// Discriminator for the low-level type kind packed into an LLT.
  enum class Kind : uint8_t {
    /// Invalid or uninitialized type.
    INVALID,
    /// Untyped scalar ("bag of bits").
    ANY_SCALAR,
    /// Integer scalar.
    INTEGER,
    /// Floating-point scalar.
    FLOAT,
    /// Pointer scalar.
    POINTER,
    /// Vector of untyped scalar elements.
    VECTOR_ANY,
    /// Vector of integer elements.
    VECTOR_INTEGER,
    /// Vector of floating-point elements.
    VECTOR_FLOAT,
    /// Vector of pointer elements.
    VECTOR_POINTER,
  };

  /// Map a scalar kind to its corresponding vector kind.
  /// @param Ty Scalar or pointer kind to convert.
  /// @return Corresponding vector kind.
  constexpr static Kind toVector(Kind Ty) {
    if (Ty == Kind::POINTER)
      return Kind::VECTOR_POINTER;

    if (Ty == Kind::INTEGER)
      return Kind::VECTOR_INTEGER;

    if (Ty == Kind::FLOAT)
      return Kind::VECTOR_FLOAT;

    return Kind::VECTOR_ANY;
  }

  /// Map a vector element kind to its corresponding scalar kind.
  /// @param Ty Vector kind to convert.
  /// @return Corresponding scalar kind.
  constexpr static Kind toScalar(Kind Ty) {
    if (Ty == Kind::VECTOR_POINTER)
      return Kind::POINTER;

    if (Ty == Kind::VECTOR_INTEGER)
      return Kind::INTEGER;

    if (Ty == Kind::VECTOR_FLOAT)
      return Kind::FLOAT;

    return Kind::ANY_SCALAR;
  }

  /// Get a low-level scalar or aggregate "bag of bits".
  /// @param SizeInBits Width of the scalar in bits.
  /// @return Untyped scalar LLT of the given width.
  static constexpr LLT scalar(unsigned SizeInBits) {
    return LLT{Kind::ANY_SCALAR, ElementCount::getFixed(0), SizeInBits};
  }

  /// Get a low-level integer scalar of the given bit width.
  /// @param SizeInBits Width of the integer in bits.
  /// @return Integer scalar LLT of the given width.
  static LLT integer(unsigned SizeInBits) {
    if (!getUseExtended())
      return LLT::scalar(SizeInBits);

    return LLT{Kind::INTEGER, ElementCount::getFixed(0), SizeInBits};
  }

  /// Get a low-level floating-point scalar with the given \p Sem semantics.
  ///
  /// When extended LLT is disabled, this is represented as a scalar whose size
  /// in bits is taken from \p Sem. When extended LLT is enabled, the returned
  /// LLT retains \p Sem as its floating-point semantics.
  /// @param Sem Floating-point semantics for the scalar.
  /// @return Floating-point scalar LLT with the given semantics.
  static LLT floatingPoint(const FpSemantics &Sem) {
    if (!getUseExtended())
      return LLT::scalar(
          APFloat::getSizeInBits(APFloatBase::EnumToSemantics(Sem)));

    return LLT{Kind::FLOAT, ElementCount::getFixed(0),
               APFloat::getSizeInBits(APFloatBase::EnumToSemantics(Sem)), Sem};
  }

  /// Get a low-level token; just a scalar with zero bits (or no size).
  /// @return Token LLT (zero-sized scalar).
  static constexpr LLT token() {
    return LLT{Kind::ANY_SCALAR, ElementCount::getFixed(0),
               /*SizeInBits=*/0};
  }

  /// Get a low-level pointer in the given address space.
  /// @param AddressSpace Address space of the pointer.
  /// @param SizeInBits Width of the pointer in bits.
  /// @return Pointer LLT in the given address space.
  static constexpr LLT pointer(unsigned AddressSpace, unsigned SizeInBits) {
    assert(SizeInBits > 0 && "invalid pointer size");
    return LLT{Kind::POINTER, ElementCount::getFixed(0), SizeInBits,
               AddressSpace};
  }

  /// Get a low-level vector of some number of elements and element width.
  /// @param EC Element count of the vector.
  /// @param ScalarSizeInBits Width of each element in bits.
  /// @return Vector LLT with the given element width.
  static constexpr LLT vector(ElementCount EC, unsigned ScalarSizeInBits) {
    assert(!EC.isScalar() && "invalid number of vector elements");
    return LLT{Kind::VECTOR_ANY, EC, ScalarSizeInBits};
  }

  /// Get a low-level vector of some number of elements and element type.
  /// @param EC Element count of the vector.
  /// @param ScalarTy Scalar element type.
  /// @return Vector LLT with the given element type.
  static constexpr LLT vector(ElementCount EC, LLT ScalarTy) {
    assert(!EC.isScalar() && "invalid number of vector elements");
    assert(!ScalarTy.isVector() && "invalid vector element type");

    Kind Info = toVector(ScalarTy.Info);
    if (ScalarTy.isPointer())
      return LLT{Info, EC, ScalarTy.getSizeInBits().getFixedValue(),
                 ScalarTy.getAddressSpace()};
    if (ScalarTy.isFloat())
      return LLT{Info, EC, ScalarTy.getSizeInBits().getFixedValue(),
                 ScalarTy.getFpSemantics()};

    return LLT{Info, EC, ScalarTy.getSizeInBits().getFixedValue()};
  }

  // FIXME: Remove this builder
  /// Get an IEEE floating-point LLT of the given bit width.
  /// @param SizeInBits Width in bits (16, 32, 64, or 128).
  /// @return IEEE float LLT of the given width.
  static LLT floatIEEE(unsigned SizeInBits) {
    if (!getUseExtended())
      return LLT::scalar(SizeInBits);

    switch (SizeInBits) {
    default:
      llvm_unreachable("Wrong SizeInBits for IEEE Floating point!");
    case 16:
      return float16();
    case 32:
      return float32();
    case 64:
      return float64();
    case 128:
      return float128();
    }
  }

  /// Get a bfloat16 value.
  /// @return BFloat16 float LLT.
  static constexpr LLT bfloat16() {
    return LLT{Kind::FLOAT, ElementCount::getFixed(0), 16,
               FpSemantics::S_BFloat};
  }
  /// Get a 16-bit IEEE half value.
  /// @return IEEE half float LLT.
  static constexpr LLT float16() {
    return LLT{Kind::FLOAT, ElementCount::getFixed(0), 16,
               FpSemantics::S_IEEEhalf};
  }
  /// Get a 32-bit IEEE float value.
  /// @return IEEE single float LLT.
  static constexpr LLT float32() {
    return LLT{Kind::FLOAT, ElementCount::getFixed(0), 32,
               FpSemantics::S_IEEEsingle};
  }
  /// Get a 64-bit IEEE double value.
  /// @return IEEE double float LLT.
  static constexpr LLT float64() {
    return LLT{Kind::FLOAT, ElementCount::getFixed(0), 64,
               FpSemantics::S_IEEEdouble};
  }

  /// Get a 80-bit X86 floating point value.
  /// @return X87 80-bit float LLT.
  static constexpr LLT x86fp80() {
    return LLT{Kind::FLOAT, ElementCount::getFixed(0), 80,
               FpSemantics::S_x87DoubleExtended};
  }

  /// Get a 128-bit IEEE quad value.
  /// @return IEEE quad float LLT.
  static constexpr LLT float128() {
    return LLT{Kind::FLOAT, ElementCount::getFixed(0), 128,
               FpSemantics::S_IEEEquad};
  }

  /// Get a 128-bit PowerPC double double value.
  /// @return PowerPC double-double float LLT.
  static constexpr LLT ppcf128() {
    return LLT{Kind::FLOAT, ElementCount::getFixed(0), 128,
               FpSemantics::S_PPCDoubleDouble};
  }

  /// Get a low-level fixed-width vector of some number of elements and element
  /// width.
  /// @param NumElements Fixed number of vector elements.
  /// @param ScalarSizeInBits Width of each element in bits.
  /// @return Fixed-width vector LLT of the given shape.
  static constexpr LLT fixed_vector(unsigned NumElements,
                                    unsigned ScalarSizeInBits) {
    return vector(ElementCount::getFixed(NumElements),
                  LLT::scalar(ScalarSizeInBits));
  }

  /// Get a low-level fixed-width vector of some number of elements and element
  /// type.
  /// @param NumElements Fixed number of vector elements.
  /// @param ScalarTy Scalar element type.
  /// @return Fixed-width vector LLT with the given element type.
  static constexpr LLT fixed_vector(unsigned NumElements, LLT ScalarTy) {
    return vector(ElementCount::getFixed(NumElements), ScalarTy);
  }

  /// Get a low-level scalable vector of some number of elements and element
  /// width.
  /// @param MinNumElements Minimum number of scalable vector elements.
  /// @param ScalarSizeInBits Width of each element in bits.
  /// @return Scalable vector LLT of the given shape.
  static constexpr LLT scalable_vector(unsigned MinNumElements,
                                       unsigned ScalarSizeInBits) {
    return vector(ElementCount::getScalable(MinNumElements),
                  LLT::scalar(ScalarSizeInBits));
  }

  /// Get a low-level scalable vector of some number of elements and element
  /// type.
  /// @param MinNumElements Minimum number of scalable vector elements.
  /// @param ScalarTy Scalar element type.
  /// @return Scalable vector LLT with the given element type.
  static constexpr LLT scalable_vector(unsigned MinNumElements, LLT ScalarTy) {
    return vector(ElementCount::getScalable(MinNumElements), ScalarTy);
  }

  /// Get \p ScalarTy when \p EC is scalar, otherwise a vector of \p EC elements
  /// with element type \p ScalarTy.
  /// @param EC Element count; scalar when EC.isScalar().
  /// @param ScalarTy Scalar type to return or use as the vector element type.
  /// @return ScalarTy, or a vector of EC such elements.
  static constexpr LLT scalarOrVector(ElementCount EC, LLT ScalarTy) {
    return EC.isScalar() ? ScalarTy : LLT::vector(EC, ScalarTy);
  }

  /// Get a scalar or vector of \p EC elements each of width \p ScalarSize bits.
  /// @param EC Element count; scalar when EC.isScalar().
  /// @param ScalarSize Width of each element in bits.
  /// @return Scalar or vector LLT of the given shape.
  static constexpr LLT scalarOrVector(ElementCount EC, uint64_t ScalarSize) {
    assert(ScalarSize <= std::numeric_limits<unsigned>::max() &&
           "Not enough bits in LLT to represent size");
    return scalarOrVector(EC, LLT::scalar(static_cast<unsigned>(ScalarSize)));
  }

  /// Construct an LLT of kind \p Info with element count \p EC and size
  /// \p SizeInBits.
  /// @param Info Kind of the LLT.
  /// @param EC Element count (scalar uses a fixed count of 0).
  /// @param SizeInBits Size of the scalar or element in bits.
  explicit constexpr LLT(Kind Info, ElementCount EC, uint64_t SizeInBits)
      : LLT() {
    init(Info, EC, SizeInBits);
  }

  /// Construct an LLT of kind \p Info with element count \p EC, size
  /// \p SizeInBits, and pointer address space \p AddressSpace.
  /// @param Info Kind of the LLT (pointer or pointer vector).
  /// @param EC Element count (scalar uses a fixed count of 0).
  /// @param SizeInBits Size of the pointer in bits.
  /// @param AddressSpace Address space of the pointer.
  explicit constexpr LLT(Kind Info, ElementCount EC, uint64_t SizeInBits,
                         unsigned AddressSpace)
      : LLT() {
    init(Info, EC, SizeInBits, AddressSpace);
  }

  /// Construct an LLT of kind \p Info with element count \p EC, size
  /// \p SizeInBits, and floating-point semantics \p Sem.
  /// @param Info Kind of the LLT (float or float vector).
  /// @param EC Element count (scalar uses a fixed count of 0).
  /// @param SizeInBits Size of the float in bits.
  /// @param Sem Floating-point semantics.
  explicit constexpr LLT(Kind Info, ElementCount EC, uint64_t SizeInBits,
                         FpSemantics Sem)
      : LLT() {
    init(Info, EC, SizeInBits, Sem);
  }

  /// Construct an LLT from a MachineValueType.
  /// @param VT Machine value type to convert.
  LLVM_ABI explicit LLT(MVT VT);
  /// Construct an invalid/empty LLT.
  explicit constexpr LLT() : RawData(0), Info(static_cast<Kind>(0)) {}

  /// Return true if this is a token type (zero-sized scalar).
  /// @return True if this is a token type.
  constexpr bool isToken() const {
    return Info == Kind::ANY_SCALAR && RawData == 0;
  }
  /// Return true if this LLT is valid (token or non-zero encoding).
  /// @return True if this LLT is valid.
  constexpr bool isValid() const { return isToken() || RawData != 0; }
  /// Return true if this is an untyped scalar ("bag of bits").
  /// @return True if this is an untyped scalar.
  constexpr bool isAnyScalar() const { return Info == Kind::ANY_SCALAR; }
  /// Return true if this is an integer scalar.
  /// @return True if this is an integer scalar.
  constexpr bool isInteger() const { return Info == Kind::INTEGER; }
  /// Return true if this is a floating-point scalar.
  /// @return True if this is a floating-point scalar.
  constexpr bool isFloat() const { return Info == Kind::FLOAT; }
  /// Return true if this is a pointer scalar.
  /// @return True if this is a pointer scalar.
  constexpr bool isPointer() const { return Info == Kind::POINTER; }
  /// Return true if this is a vector of untyped scalar elements.
  /// @return True if this is a vector of untyped scalars.
  constexpr bool isAnyVector() const { return Info == Kind::VECTOR_ANY; }
  /// Return true if this is a vector of integer elements.
  /// @return True if this is a vector of integers.
  constexpr bool isIntegerVector() const {
    return Info == Kind::VECTOR_INTEGER;
  }
  /// Return true if this is a vector of floating-point elements.
  /// @return True if this is a vector of floats.
  constexpr bool isFloatVector() const { return Info == Kind::VECTOR_FLOAT; }
  /// Return true if this is a vector of pointer elements.
  /// @return True if this is a vector of pointers.
  constexpr bool isPointerVector() const {
    return Info == Kind::VECTOR_POINTER;
  }
  /// Return true if this is a pointer or a vector of pointers.
  /// @return True if this is a pointer or pointer vector.
  constexpr bool isPointerOrPointerVector() const {
    return isPointer() || isPointerVector();
  }
  /// Return true if this is a scalar or vector floating-point type.
  /// @return True if this is a scalar or vector float.
  constexpr bool isFloatOrFloatVector() const {
    return isFloat() || isFloatVector();
  }

  /// Return true if this is a scalar (any, integer, or float).
  /// @return True if this is a scalar.
  constexpr bool isScalar() const {
    return Info == Kind::ANY_SCALAR || Info == Kind::INTEGER ||
           Info == Kind::FLOAT;
  }
  /// Return true if this is a scalar of the given bit width.
  /// @param Size Expected scalar size in bits.
  /// @return True if this is a scalar of the given width.
  constexpr bool isScalar(unsigned Size) const {
    return isScalar() && getScalarSizeInBits() == Size;
  }
  /// Return true if this is a vector type.
  /// @return True if this is a vector type.
  constexpr bool isVector() const {
    return Info == Kind::VECTOR_ANY || Info == Kind::VECTOR_INTEGER ||
           Info == Kind::VECTOR_FLOAT || Info == Kind::VECTOR_POINTER;
  }

  /// Return true if this is an integer scalar of the given bit width.
  /// @param Size Expected integer size in bits.
  /// @return True if this is an integer of the given width.
  constexpr bool isInteger(unsigned Size) const {
    return isInteger() && getScalarSizeInBits() == Size;
  }

  /// Return true if this is a floating-point scalar of the given bit width.
  /// @param Size Expected float size in bits.
  /// @return True if this is a float of the given width.
  constexpr bool isFloat(unsigned Size) const {
    return isFloat() && getScalarSizeInBits() == Size;
  }
  /// Return true if this is a floating-point scalar with the given semantics.
  /// @param Sem Expected floating-point semantics.
  /// @return True if this is a float with the given semantics.
  constexpr bool isFloat(FpSemantics Sem) const {
    return isFloat() && getFpSemantics() == Sem;
  }
  // FIXME: Remove or rework this predicate
  /// Return true if this is an IEEE half, single, double, or quad float.
  /// @return True if this is an IEEE half, single, double, or quad float.
  constexpr bool isFloatIEEE() const {
    return isFloat(APFloatBase::S_IEEEhalf) ||
           isFloat(APFloatBase::S_IEEEsingle) ||
           isFloat(APFloatBase::S_IEEEdouble) ||
           isFloat(APFloatBase::S_IEEEquad);
  }

  /// Return true if this is an IEEE half float.
  /// @return True if this is an IEEE half float.
  bool isFloat16() const {
    if (!getUseExtended())
      return isAnyScalar() && getSizeInBits() == 16;
    return isFloat(APFloatBase::S_IEEEhalf);
  }
  /// Return true if this is an IEEE single float.
  /// @return True if this is an IEEE single float.
  bool isFloat32() const {
    if (!getUseExtended())
      return isAnyScalar() && getSizeInBits() == 32;
    return isFloat(APFloatBase::S_IEEEsingle);
  }
  /// Return true if this is an IEEE double float.
  /// @return True if this is an IEEE double float.
  bool isFloat64() const {
    if (!getUseExtended())
      return isAnyScalar() && getSizeInBits() == 64;
    return isFloat(APFloatBase::S_IEEEdouble);
  }
  /// Return true if this is an IEEE quad float.
  /// @return True if this is an IEEE quad float.
  bool isFloat128() const {
    if (!getUseExtended())
      return isAnyScalar() && getSizeInBits() == 128;
    return isFloat(APFloatBase::S_IEEEquad);
  }
  /// Return true if this is a bfloat16 float.
  /// @return True if this is a bfloat16 float.
  bool isBFloat16() const {
    if (!getUseExtended())
      return false;
    return isFloat(FpSemantics::S_BFloat);
  }
  /// Return true if this is an x87 80-bit float.
  /// @return True if this is an x87 80-bit float.
  bool isX86FP80() const {
    if (!getUseExtended())
      return false;
    return isFloat(FpSemantics::S_x87DoubleExtended);
  }
  /// Return true if this is a PowerPC double-double float.
  /// @return True if this is a PowerPC double-double float.
  bool isPPCF128() const {
    if (!getUseExtended())
      return false;
    return isFloat(FpSemantics::S_PPCDoubleDouble);
  }

  /// Returns the number of elements in a vector LLT. Must only be called on
  /// vector types.
  /// @return Number of elements in the vector.
  constexpr uint16_t getNumElements() const {
    if (isScalable())
      llvm::reportFatalInternalError(
          "Possible incorrect use of LLT::getNumElements() for "
          "scalable vector. Scalable flag may be dropped, use "
          "LLT::getElementCount() instead");
    return getElementCount().getKnownMinValue();
  }

  /// Returns true if the LLT is a scalable vector. Must only be called on
  /// vector types.
  /// @return True if this is a scalable vector.
  constexpr bool isScalable() const {
    assert(isVector() && "Expected a vector type");
    return getFieldValue(VectorScalableFieldInfo);
  }

  /// Returns true if the LLT is a fixed vector. Returns false otherwise, even
  /// if the LLT is not a vector type.
  /// @return True if this is a fixed vector.
  constexpr bool isFixedVector() const { return isVector() && !isScalable(); }

  /// Returns true if the LLT is a fixed vector of the given shape.
  /// @param NumElements Expected number of elements.
  /// @param ScalarSize Expected element size in bits.
  /// @return True if this is a fixed vector of the given shape.
  constexpr bool isFixedVector(unsigned NumElements,
                               unsigned ScalarSize) const {
    return isFixedVector() && getNumElements() == NumElements &&
           getScalarSizeInBits() == ScalarSize;
  }

  /// Returns true if the LLT is a scalable vector. Returns false otherwise,
  /// even if the LLT is not a vector type.
  /// @return True if this is a scalable vector.
  constexpr bool isScalableVector() const { return isVector() && isScalable(); }

  /// Returns the number of elements in a vector LLT as an ElementCount.
  /// Must only be called on vector types.
  /// @return Element count of the vector.
  constexpr ElementCount getElementCount() const {
    assert(isVector() && "cannot get number of elements on scalar/aggregate");
    return ElementCount::get(getFieldValue(VectorElementsFieldInfo),
                             isScalable());
  }

  /// Returns the total size of the type. Must only be called on sized types.
  /// @return Total size of the type in bits.
  constexpr TypeSize getSizeInBits() const {
    if (isPointer() || isScalar())
      return TypeSize::getFixed(getScalarSizeInBits());
    auto EC = getElementCount();
    return TypeSize(getScalarSizeInBits() * EC.getKnownMinValue(),
                    EC.isScalable());
  }

  /// Returns the total size of the type in bytes, i.e. number of whole bytes
  /// needed to represent the size in bits. Must only be called on sized types.
  /// @return Total size of the type in whole bytes.
  constexpr TypeSize getSizeInBytes() const {
    TypeSize BaseSize = getSizeInBits();
    return {(BaseSize.getKnownMinValue() + 7) / 8, BaseSize.isScalable()};
  }

  /// Return the scalar type, or the vector element type when this is a vector.
  /// @return Scalar type, or the vector element type.
  LLT getScalarType() const { return isVector() ? getElementType() : *this; }

  /// Return the floating-point semantics of this float or float vector.
  /// @return Floating-point semantics of this float type.
  constexpr FpSemantics getFpSemantics() const {
    assert((isFloat() || isFloatVector()) &&
           "cannot get FP info for non float type");
    return FpSemantics(getFieldValue(FpSemanticFieldInfo));
  }

  /// Return the kind discriminator of this LLT.
  /// @return Kind discriminator of this LLT.
  constexpr Kind getKind() const { return Info; }

  /// Returns a vector with the same number of elements but the new element
  /// type. Must only be called on vector types.
  /// @param NewEltTy New element type for the vector.
  /// @return Vector with the new element type.
  constexpr LLT changeVectorElementType(LLT NewEltTy) const {
    return LLT::vector(getElementCount(), NewEltTy);
  }

  /// If this type is a vector, return a vector with the same number of elements
  /// but the new element type. Otherwise, return the new element type.
  /// @param NewEltTy New element type.
  /// @return Type with the new element type.
  constexpr LLT changeElementType(LLT NewEltTy) const {
    return isVector() ? changeVectorElementType(NewEltTy) : NewEltTy;
  }

  /// Return this type with each element's size replaced by \p NewEltSize.
  ///
  /// If this type is a vector, return a vector with the same number of elements
  /// but the new element size. Otherwise, return the new element type. Invalid
  /// for pointer types. For these, use changeElementType.
  /// @param NewEltSize New element size in bits.
  /// @return Type with each element's size replaced.
  LLT changeElementSize(unsigned NewEltSize) const {
    assert(!isPointerOrPointerVector() &&
           "invalid to directly change element size for pointers");
    if (isVector())
      return LLT::vector(getElementCount(),
                         getElementType().changeElementSize(NewEltSize));

    if (isInteger())
      return LLT::integer(NewEltSize);

    if (isFloatIEEE())
      return LLT::floatIEEE(NewEltSize);

    return LLT::scalar(NewEltSize);
  }

  /// Return a vector with the same element type and the new element count. Must
  /// be called on vector types.
  /// @param EC New element count for the vector.
  /// @return Vector with the new element count.
  LLT changeVectorElementCount(ElementCount EC) const {
    assert(isVector() &&
           "cannot change vector element count of non-vector type");
    return LLT::vector(EC, getElementType());
  }

  /// Return a vector or scalar with the same element type and the new element
  /// count.
  /// @param EC New element count; scalar when EC.isScalar().
  /// @return Type with the new element count.
  LLT changeElementCount(ElementCount EC) const {
    return LLT::scalarOrVector(EC, getScalarType());
  }

  /// Return a vector or scalar with the same element type and \p NumElements
  /// elements.
  /// @param NumElements Fixed number of elements.
  /// @return Type with NumElements elements.
  LLT changeElementCount(unsigned NumElements) const {
    return changeElementCount(ElementCount::getFixed(NumElements));
  }

  /// Return a type that is \p Factor times smaller.
  ///
  /// Reduces the number of elements if this is a vector, or the bitwidth for
  /// scalar/pointers. Does not attempt to handle cases that aren't evenly
  /// divisible.
  /// @param Factor Divisor applied to element count or bit width.
  /// @return Type Factor times smaller.
  LLT divide(int Factor) const {
    assert(Factor != 1);
    assert((!isScalar() || getScalarSizeInBits() != 0) && !isFloat() &&
           "cannot divide scalar of size zero and floats");
    if (isVector()) {
      assert(getElementCount().isKnownMultipleOf(Factor));
      return scalarOrVector(getElementCount().divideCoefficientBy(Factor),
                            getElementType());
    }

    assert(getScalarSizeInBits() % Factor == 0);
    if (isInteger())
      return integer(getScalarSizeInBits() / Factor);

    return scalar(getScalarSizeInBits() / Factor);
  }

  /// Produce a vector type that is \p Factor times bigger, preserving the
  /// element type. For a scalar or pointer, this will produce a new vector with
  /// \p Factor elements.
  /// @param Factor Multiplier for the number of elements.
  /// @return Type with Factor times more elements.
  LLT multiplyElements(int Factor) const {
    if (isVector()) {
      return scalarOrVector(getElementCount().multiplyCoefficientBy(Factor),
                            getElementType());
    }

    return fixed_vector(Factor, *this);
  }

  /// Return true if the size in bits is a multiple of 8.
  /// @return True if the size in bits is a multiple of 8.
  constexpr bool isByteSized() const {
    return getSizeInBits().isKnownMultipleOf(8);
  }

  /// Return the size in bits of the scalar or of each vector element.
  /// @return Size in bits of the scalar or each element.
  constexpr unsigned getScalarSizeInBits() const {
    if (isPointerOrPointerVector())
      return getFieldValue(PointerSizeFieldInfo);
    return getFieldValue(ScalarSizeFieldInfo);
  }

  /// Return the address space of this pointer or pointer vector.
  /// @return Address space of the pointer.
  constexpr unsigned getAddressSpace() const {
    assert(isPointerOrPointerVector() &&
           "cannot get address space of non-pointer type");
    return getFieldValue(PointerAddressSpaceFieldInfo);
  }

  /// Returns the vector's element type. Only valid for vector types.
  /// @return The vector element type.
  LLT getElementType() const {
    assert(isVector() && "cannot get element type of scalar/aggregate");
    if (isPointerVector())
      return pointer(getAddressSpace(), getScalarSizeInBits());

    if (isFloatVector())
      return floatingPoint(getFpSemantics());

    if (isIntegerVector())
      return integer(getScalarSizeInBits());

    return scalar(getScalarSizeInBits());
  }

  /// Return this type with floating-point/any-scalar kinds changed to integer.
  /// @return This type with float/any-scalar kinds as integer.
  LLT changeToInteger() const {
    if (isPointer() || isPointerVector())
      return *this;

    if (isVector())
      return vector(getElementCount(), LLT::integer(getScalarSizeInBits()));

    return integer(getSizeInBits());
  }

  /// Print this LLT to the given stream.
  /// @param OS Output stream.
  LLVM_ABI void print(raw_ostream &OS) const;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Dump this LLT to the debug stream.
  LLVM_DUMP_METHOD void dump() const;
#endif

  /// Return true if this LLT equals \p RHS.
  /// @param RHS Other LLT to compare.
  /// @return True if the LLTs are equal.
  bool operator==(const LLT &RHS) const {
    if (isAnyScalar() || RHS.isAnyScalar())
      return isScalar() == RHS.isScalar() &&
             getScalarSizeInBits() == RHS.getScalarSizeInBits();

    if (isVector() && RHS.isVector())
      return getElementType() == RHS.getElementType() &&
             getElementCount() == RHS.getElementCount();

    return Info == RHS.Info && RawData == RHS.RawData;
  }

  /// Return true if this LLT is not equal to \p RHS.
  /// @param RHS Other LLT to compare.
  /// @return True if the LLTs are not equal.
  bool operator!=(const LLT &RHS) const { return !(*this == RHS); }

  friend struct DenseMapInfo<LLT>;
  friend class GISelInstProfileBuilder;

private:
  /// LLT is packed into 64 bits as follows:
  /// RawData : 60
  /// Info : 4
  /// RawData remaining for Kind-specific data, packed in
  /// bitfields as described below. As there isn't a simple portable way to pack
  /// bits into bitfields, here the different fields in the packed structure is
  /// described in static const *Field variables. Each of these variables
  /// is a 2-element array, with the first element describing the bitfield size
  /// and the second element describing the bitfield offset.
  ///
  /*
                                --- LLT ---

   63       56       47       39       31       23       15       7      0
   |        |        |        |        |        |        |        |      |
  |xxxxxxxx|xxxxxxxx|xxxxxxxx|xxxxxxxx|xxxxxxxx|xxxxxxxx|xxxxxxxx|xxxxxxxx|
   %%%%                                                                     (1)
       .... ........ ........ ........ ....                                 (2)
       **** ******** ****                                                   (3)
                         ~~~~ ~~~~~~~~ ~~~~~~~~ ~~~~                        (4)
                                           #### ####                        (5)
                                                    ^^^^ ^^^^^^^^ ^^^^      (6)
                                                                         @  (7)

  (1) Kind:                [63:60]
  (2) ScalarSize:          [59:28]
  (3) PointerSize:         [59:44]
  (4) PointerAddressSpace: [43:20]
  (5) FpSemantics:         [27:20]
  (6) VectorElements:      [19:4]
  (7) VectorScalable:      [0:0]

  */

  /// This is how the LLT are packed per Kind:
  /// * Invalid:
  ///   Info: [63:60] = 0
  ///   RawData: [59:0] = 0;
  ///
  /// * Non-pointer scalar (isPointer == 0 && isVector == 0):
  ///   Info: [63:60];
  ///   SizeOfElement: [59:28];
  ///   FpSemantics: [27:20];
  ///
  /// * Pointer (isPointer == 1 && isVector == 0):
  ///   Info: [63:60];
  ///   SizeInBits: [59:44];
  ///   AddressSpace: [43:20];
  ///
  /// * Vector-of-non-pointer (isPointer == 0 && isVector == 1):
  ///   Info: [63:60]
  ///   SizeOfElement: [59:28];
  ///   FpSemantics: [27:20];
  ///   VectorElements: [19:4];
  ///   Scalable: [0:0];
  ///
  /// * Vector-of-pointer (isPointer == 1 && isVector == 1):
  ///   Info: [63:60];
  ///   SizeInBits: [59:44];
  ///   AddressSpace: [43:20];
  ///   VectorElements: [19:4];
  ///   Scalable: [0:0];

  /// BitFieldInfo: {Size, Offset}
  typedef int BitFieldInfo[2];
  static_assert(bit_width_constexpr((uint32_t)APFloat::S_MaxSemantics) <= 8);
  static constexpr BitFieldInfo VectorScalableFieldInfo{1, 0};
  static constexpr BitFieldInfo VectorElementsFieldInfo{16, 4};
  static constexpr BitFieldInfo FpSemanticFieldInfo{8, 20};
  static constexpr BitFieldInfo PointerAddressSpaceFieldInfo{24, 20};
  static constexpr BitFieldInfo ScalarSizeFieldInfo{32, 28};
  static constexpr BitFieldInfo PointerSizeFieldInfo{16, 44};

  uint64_t RawData : 60;
  Kind Info : 4;

  static constexpr uint64_t getMask(const BitFieldInfo FieldInfo) {
    const int FieldSizeInBits = FieldInfo[0];
    return (((uint64_t)1) << FieldSizeInBits) - 1;
  }
  static constexpr uint64_t maskAndShift(uint64_t Val, uint64_t Mask,
                                         uint8_t Shift) {
    assert(Val <= Mask && "Value too large for field");
    return (Val & Mask) << Shift;
  }
  static constexpr uint64_t maskAndShift(uint64_t Val,
                                         const BitFieldInfo FieldInfo) {
    return maskAndShift(Val, getMask(FieldInfo), FieldInfo[1]);
  }

  constexpr uint64_t getFieldValue(const BitFieldInfo FieldInfo) const {
    return getMask(FieldInfo) & (RawData >> FieldInfo[1]);
  }

  // Init for scalar and integer single or vector types
  constexpr void init(Kind Info, ElementCount EC, uint64_t SizeInBits) {
    assert(SizeInBits <= std::numeric_limits<unsigned>::max() &&
           "Not enough bits in LLT to represent size");
    assert((Info == Kind::ANY_SCALAR || Info == Kind::INTEGER ||
            Info == Kind::VECTOR_ANY || Info == Kind::VECTOR_INTEGER) &&
           "Called initializer for wrong LLT Kind");
    this->Info = Info;
    RawData = maskAndShift(SizeInBits, ScalarSizeFieldInfo);

    if (Info == Kind::VECTOR_ANY || Info == Kind::VECTOR_INTEGER) {
      RawData = maskAndShift(SizeInBits, ScalarSizeFieldInfo) |
                maskAndShift(EC.getKnownMinValue(), VectorElementsFieldInfo) |
                maskAndShift(EC.isScalable() ? 1 : 0, VectorScalableFieldInfo);
    }
  }

  // Init pointer or pointer vector
  constexpr void init(Kind Info, ElementCount EC, uint64_t SizeInBits,
                      unsigned AddressSpace) {
    assert(SizeInBits <= std::numeric_limits<unsigned>::max() &&
           "Not enough bits in LLT to represent size");
    assert((Info == Kind::POINTER || Info == Kind::VECTOR_POINTER) &&
           "Called initializer for wrong LLT Kind");
    this->Info = Info;
    RawData = maskAndShift(SizeInBits, PointerSizeFieldInfo) |
              maskAndShift(AddressSpace, PointerAddressSpaceFieldInfo);

    if (Info == Kind::VECTOR_POINTER) {
      RawData |= maskAndShift(EC.getKnownMinValue(), VectorElementsFieldInfo) |
                 maskAndShift(EC.isScalable() ? 1 : 0, VectorScalableFieldInfo);
    }
  }

  constexpr void init(Kind Info, ElementCount EC, uint64_t SizeInBits,
                      FpSemantics Sem) {
    assert(SizeInBits <= std::numeric_limits<unsigned>::max() &&
           "Not enough bits in LLT to represent size");
    assert((Info == Kind::FLOAT || Info == Kind::VECTOR_FLOAT) &&
           "Called initializer for wrong LLT Kind");
    this->Info = Info;
    RawData = maskAndShift(SizeInBits, ScalarSizeFieldInfo) |
              maskAndShift((uint64_t)Sem, FpSemanticFieldInfo);

    if (Info == Kind::VECTOR_FLOAT) {
      RawData |= maskAndShift(EC.getKnownMinValue(), VectorElementsFieldInfo) |
                 maskAndShift(EC.isScalable() ? 1 : 0, VectorScalableFieldInfo);
    }
  }

public:
  /// Return a unique raw encoding of this LLT suitable for hashing and map keys.
  ///
  /// Packs the internal representation into a single \c uint64_t by combining
  /// \c RawData with the \c Kind in the upper bits.
  /// @return Unique 64-bit encoding of this LLT.
  constexpr uint64_t getUniqueRAWLLTData() const {
    return ((uint64_t)RawData) | ((uint64_t)Info) << 60;
  }

  /// Return whether extended LLT kinds are enabled.
  /// @return True if extended LLT kinds are enabled.
  static bool getUseExtended() { return ExtendedLLT; }
  /// Enable or disable extended LLT kinds (integer/float/pointer distinctions).
  /// @param Enable True to enable extended kinds.
  static void setUseExtended(bool Enable) { ExtendedLLT = Enable; }

private:
  LLVM_ABI static bool ExtendedLLT;
};

/// Write \p Ty to \p OS.
/// @param OS Output stream.
/// @param Ty LLT to print.
/// @return Reference to OS after printing.
inline raw_ostream &operator<<(raw_ostream &OS, const LLT &Ty) {
  Ty.print(OS);
  return OS;
}

/// DenseMapInfo specialization so LLT can be used as a DenseMap key.
template <> struct DenseMapInfo<LLT> {
  /// Compute a hash value for \p Ty.
  /// @param Ty The LLT to hash.
  /// @return Hash value for Ty.
  static inline unsigned getHashValue(const LLT &Ty) {
    uint64_t Val = Ty.getUniqueRAWLLTData();
    return DenseMapInfo<uint64_t>::getHashValue(Val);
  }
  /// Return true if \p LHS and \p RHS compare equal.
  /// @param LHS Left-hand LLT.
  /// @param RHS Right-hand LLT.
  /// @return True if LHS and RHS are equal.
  static bool isEqual(const LLT &LHS, const LLT &RHS) { return LHS == RHS; }
};

} // namespace llvm

#endif // LLVM_CODEGEN_LOWLEVELTYPE_H
