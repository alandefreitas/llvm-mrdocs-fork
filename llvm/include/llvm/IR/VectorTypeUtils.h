//===------- VectorTypeUtils.h - Vector type utility functions -*- C++ -*-====//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_VECTORTYPEUTILS_H
#define LLVM_IR_VECTORTYPEUTILS_H

#include "llvm/IR/DerivedTypes.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

/// Convert a scalar type to a vector type.
///
/// If the incoming type is void, we return void. If the EC represents a
/// scalar, we return the scalar type.
/// \param Scalar Scalar (or void/metadata) type to widen.
/// \param EC Element count of the resulting vector.
/// \return The vector type with element type \p Scalar and element count
/// \p EC, or \p Scalar when it is void/metadata or \p EC is scalar.
inline Type *toVectorTy(Type *Scalar, ElementCount EC) {
  if (Scalar->isVoidTy() || Scalar->isMetadataTy() || EC.isScalar())
    return Scalar;
  return VectorType::get(Scalar, EC);
}

/// Convert a scalar type to a fixed-length vector type.
/// \param Scalar Scalar (or void/metadata) type to widen.
/// \param VF Fixed vector length (number of elements).
/// \return The fixed-length vector type with element type \p Scalar and
/// length \p VF.
inline Type *toVectorTy(Type *Scalar, unsigned VF) {
  return toVectorTy(Scalar, ElementCount::getFixed(VF));
}

/// Convert a struct of scalar types to a struct of vector types.
///
/// Note:
///   - If \p EC is scalar, \p StructTy is returned unchanged
///   - Only unpacked literal struct types are supported
/// \param StructTy Unpacked literal struct of scalar element types.
/// \param EC Element count used to widen each struct element.
/// \return A struct whose elements are the vectorized forms of
/// \p StructTy's elements, or \p StructTy when \p EC is scalar.
LLVM_ABI Type *toVectorizedStructTy(StructType *StructTy, ElementCount EC);

/// Convert a struct of vector types to a struct of scalar types.
///
/// Note: Only unpacked literal struct types are supported.
/// \param StructTy Unpacked literal struct of vector element types.
/// \return A struct whose elements are the scalarized forms of
/// \p StructTy's elements.
LLVM_ABI Type *toScalarizedStructTy(StructType *StructTy);

/// Returns true if `StructTy` is an unpacked literal struct where all elements
/// are vectors of matching element count. This does not include empty structs.
/// \param StructTy Struct type to test for a vectorized layout.
/// \return True if \p StructTy is a non-empty unpacked literal struct of
/// matching vector element types.
LLVM_ABI bool isVectorizedStructTy(StructType *StructTy);

/// Returns true if `StructTy` is an unpacked literal struct where all elements
/// are scalars that can be used as vector element types.
/// \param StructTy Struct type to test for vectorizable scalar elements.
/// \return True if \p StructTy is an unpacked literal struct of vectorizable
/// scalar element types.
LLVM_ABI bool canVectorizeStructTy(StructType *StructTy);

/// Convert a scalar or struct type to its vectorized form.
///
/// For scalar types, this is equivalent to calling `toVectorTy`. For struct
/// types, this returns a new struct where each element type has been widened to
/// a vector type.
/// Note:
///   - If the incoming type is void, we return void
///   - If \p EC is scalar, \p Ty is returned unchanged
///   - Only unpacked literal struct types are supported
/// \param Ty Scalar, void, or unpacked literal struct type to widen.
/// \param EC Element count used to widen \p Ty.
/// \return The vectorized form of \p Ty for element count \p EC.
inline Type *toVectorizedTy(Type *Ty, ElementCount EC) {
  if (StructType *StructTy = dyn_cast<StructType>(Ty))
    return toVectorizedStructTy(StructTy, EC);
  return toVectorTy(Ty, EC);
}

/// Convert a vectorized type to its scalarized (non-vector) form.
///
/// For vector types, this is equivalent to calling .getScalarType(). For struct
/// types, this returns a new struct where each element type has been converted
/// to a scalar type. Note: Only unpacked literal struct types are supported.
/// \param Ty Vector or unpacked literal struct of vectors to scalarize.
/// \return The scalarized (non-vector) form of \p Ty.
inline Type *toScalarizedTy(Type *Ty) {
  if (StructType *StructTy = dyn_cast<StructType>(Ty))
    return toScalarizedStructTy(StructTy);
  return Ty->getScalarType();
}

/// Returns true if `Ty` is a vector type or a struct of vector types where all
/// vector types share the same VF.
/// \param Ty Type to test for a vectorized layout.
/// \return True if \p Ty is a vector or a matching vectorized struct.
inline bool isVectorizedTy(Type *Ty) {
  if (StructType *StructTy = dyn_cast<StructType>(Ty))
    return isVectorizedStructTy(StructTy);
  return Ty->isVectorTy();
}

/// Returns true if `Ty` can be used as a vector element or vectorized struct.
///
/// That is, if \p Ty is a valid vector element type, void, or an unpacked
/// literal struct where all elements are valid vector element types.
/// Note: Even if a type can be vectorized that does not mean it is valid to do
/// so in all cases. For example, a vectorized struct (as returned by
/// toVectorizedTy) does not perform (de)interleaving, so it can't be used for
/// vectorizing loads/stores.
/// \param Ty Type to test for vectorizability.
/// \return True if \p Ty can be used as a vector element or vectorized struct.
inline bool canVectorizeTy(Type *Ty) {
  if (StructType *StructTy = dyn_cast<StructType>(Ty))
    return canVectorizeStructTy(StructTy);
  return Ty->isVoidTy() || VectorType::isValidElementType(Ty);
}

/// Returns the types contained in `Ty`. For struct types, it returns the
/// elements, all other types are returned directly.
/// \param Ty Type whose contained types are returned.
/// \return The element types of \p Ty when it is a struct, otherwise a
/// single-element array containing \p Ty.
inline ArrayRef<Type *> getContainedTypes(Type *const &Ty) {
  if (auto *StructTy = dyn_cast<StructType>(Ty))
    return StructTy->elements();
  return ArrayRef<Type *>(&Ty, 1);
}

/// Returns the number of vector elements for a vectorized type.
/// \param Ty Vectorized type whose element count is returned.
/// \return The shared vector element count of \p Ty.
inline ElementCount getVectorizedTypeVF(Type *Ty) {
  assert(isVectorizedTy(Ty) && "expected vectorized type");
  return cast<VectorType>(getContainedTypes(Ty).front())->getElementCount();
}

/// Returns true if \p StructTy is an unpacked (non-packed) literal struct.
/// \param StructTy Struct type to test.
/// \return True if \p StructTy is an unpacked literal struct.
inline bool isUnpackedStructLiteral(StructType *StructTy) {
  return StructTy->isLiteral() && !StructTy->isPacked();
}

} // namespace llvm

#endif
