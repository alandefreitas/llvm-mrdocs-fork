//===- llvm/Type.h - Classes for handling data types ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the Type class.  For more "Type"
// stuff, look in DerivedTypes.h.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_TYPE_H
#define LLVM_IR_TYPE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/CBindingWrapping.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TypeSize.h"
#include <cassert>
#include <cstdint>
#include <iterator>

namespace llvm {

class ByteType;
class IntegerType;
struct fltSemantics;
class LLVMContext;
class PointerType;
class raw_ostream;
class StringRef;
template <typename PtrType> class SmallPtrSetImpl;

/// The root of the LLVM type system hierarchy.
///
/// The instances of the Type class are immutable: once they are created,
/// they are never changed.  Also note that only one instance of a particular
/// type is ever created.  Thus seeing if two types are equal is a matter of
/// doing a trivial pointer comparison. To enforce that no two equal instances
/// are created, Type instances can only be created via static factory methods
/// in class Type and in derived classes.  Once allocated, Types are never
/// free'd.
///
class Type {
public:
  //===--------------------------------------------------------------------===//
  /// Identifiers for all base types in the LLVM type system.
  ///
  /// Based on this value, you can cast to a class defined in DerivedTypes.h.
  /// Note: If you add an element to this, you need to add an element to the
  /// Type::getPrimitiveType function, or else things will break!
  /// Also update LLVMTypeKind and LLVMGetTypeKind () in the C binding.
  ///
  enum TypeID {
    // PrimitiveTypes
    HalfTyID = 0,  ///< 16-bit floating point type
    BFloatTyID,    ///< 16-bit floating point type (7-bit significand)
    FloatTyID,     ///< 32-bit floating point type
    DoubleTyID,    ///< 64-bit floating point type
    X86_FP80TyID,  ///< 80-bit floating point type (X87)
    FP128TyID,     ///< 128-bit floating point type (112-bit significand)
    PPC_FP128TyID, ///< 128-bit floating point type (two 64-bits, PowerPC)
    VoidTyID,      ///< type with no size
    LabelTyID,     ///< Labels
    MetadataTyID,  ///< Metadata
    X86_AMXTyID,   ///< AMX vectors (8192 bits, X86 specific)
    TokenTyID,     ///< Tokens

    // Derived types... see DerivedTypes.h file.
    IntegerTyID,        ///< Arbitrary bit width integers
    ByteTyID,           ///< Arbitrary bit width bytes
    FunctionTyID,       ///< Functions
    PointerTyID,        ///< Pointers
    StructTyID,         ///< Structures
    ArrayTyID,          ///< Arrays
    FixedVectorTyID,    ///< Fixed width SIMD vector type
    ScalableVectorTyID, ///< Scalable SIMD vector type
    TypedPointerTyID,   ///< Typed pointer used by some GPU targets
    TargetExtTyID,      ///< Target extension type
  };

private:
  /// This refers to the LLVMContext in which this type was uniqued.
  LLVMContext &Context;

  TypeID   ID : 8;            // The current base type of this type.
  unsigned SubclassData : 24; // Space for subclasses to store data.
                              // Note that this should be synchronized with
                              // MAX_INT_BITS value in IntegerType class.

protected:
  friend class LLVMContextImpl;

  /// Construct a uniqued primitive or placeholder type with id \p tid in \p C.
  /// \param C LLVM context in which to unique the type.
  /// \param tid TypeID of the type being constructed.
  explicit Type(LLVMContext &C, TypeID tid)
    : Context(C), ID(tid), SubclassData(0) {}
  /// Destroy this type; types are never freed in practice.
  ~Type() = default;

  /// Return the subclass-specific data packed into this type.
  /// \return The subclass-specific data packed into this type.
  unsigned getSubclassData() const { return SubclassData; }

  /// Store subclass-specific data into this type's SubclassData field.
  /// \param val Value to store in the subclass data field.
  void setSubclassData(unsigned val) {
    SubclassData = val;
    // Ensure we don't have any accidental truncation.
    assert(getSubclassData() == val && "Subclass data too large for field");
  }

  /// Keeps track of how many Type*'s there are in the ContainedTys list.
  unsigned NumContainedTys = 0;

  /// Pointer to the array of Types contained by this Type.
  ///
  /// For example, this includes the arguments of a function type, the elements
  /// of a structure, the pointee of a pointer, the element type of an array,
  /// etc. This pointer may be 0 for types that don't contain other types
  /// (Integer, Double, Float).
  Type * const *ContainedTys = nullptr;

public:
  /// Print the current type.
  ///
  /// Omit the type details if \p NoDetails == true.
  /// E.g., let %st = type { i32, i16 }
  /// When \p NoDetails is true, we only print %st.
  /// Put differently, \p NoDetails prints the type as if
  /// inlined with the operands when printing an instruction.
  /// \param O Output stream.
  /// \param IsForDebug Whether to use debug-oriented printing.
  /// \param NoDetails If true, print only the type name without body details.
  LLVM_ABI void print(raw_ostream &O, bool IsForDebug = false,
                      bool NoDetails = false) const;

  /// Dump this type to stderr for debugging.
  LLVM_ABI void dump() const;

  /// Return the LLVMContext in which this type was uniqued.
  /// \return The LLVMContext in which this type was uniqued.
  LLVMContext &getContext() const { return Context; }

  //===--------------------------------------------------------------------===//
  // Accessors for working with types.
  //

  /// Return the type id for the type. This will return one of the TypeID enum
  /// elements defined above.
  /// \return One of the TypeID enum elements defined above.
  TypeID getTypeID() const { return ID; }

  /// Return true if this is 'void'.
  /// \return true if this is the void type.
  bool isVoidTy() const { return getTypeID() == VoidTyID; }

  /// Return true if this is 'half', a 16-bit IEEE fp type.
  /// \return true if this is the half floating-point type.
  bool isHalfTy() const { return getTypeID() == HalfTyID; }

  /// Return true if this is 'bfloat', a 16-bit bfloat type.
  /// \return true if this is the bfloat floating-point type.
  bool isBFloatTy() const { return getTypeID() == BFloatTyID; }

  /// Return true if this is a 16-bit float type.
  /// \return true if this is half or bfloat.
  bool is16bitFPTy() const {
    return getTypeID() == BFloatTyID || getTypeID() == HalfTyID;
  }

  /// Return true if this is 'float', a 32-bit IEEE fp type.
  /// \return true if this is the float type.
  bool isFloatTy() const { return getTypeID() == FloatTyID; }

  /// Return true if this is 'double', a 64-bit IEEE fp type.
  /// \return true if this is the double type.
  bool isDoubleTy() const { return getTypeID() == DoubleTyID; }

  /// Return true if this is x86 long double.
  /// \return true if this is the x86_fp80 type.
  bool isX86_FP80Ty() const { return getTypeID() == X86_FP80TyID; }

  /// Return true if this is 'fp128'.
  /// \return true if this is the fp128 type.
  bool isFP128Ty() const { return getTypeID() == FP128TyID; }

  /// Return true if this is powerpc long double.
  /// \return true if this is the ppc_fp128 type.
  bool isPPC_FP128Ty() const { return getTypeID() == PPC_FP128TyID; }

  /// Return true if this is a well-behaved IEEE-like type, which has a IEEE
  /// compatible layout, and does not have non-IEEE values, such as x86_fp80's
  /// unnormal values.
  /// \return true if this is an IEEE-like floating-point type.
  bool isIEEELikeFPTy() const {
    switch (getTypeID()) {
    case DoubleTyID:
    case FloatTyID:
    case HalfTyID:
    case BFloatTyID:
    case FP128TyID:
      return true;
    default:
      return false;
    }
  }

  /// Return true if this is one of the floating-point types.
  /// \return true if this is any floating-point type.
  bool isFloatingPointTy() const {
    return isIEEELikeFPTy() || getTypeID() == X86_FP80TyID ||
           getTypeID() == PPC_FP128TyID;
  }

  /// Return true if this is a multi-unit floating-point type.
  ///
  /// Returns true if this is a floating-point type that is an unevaluated sum
  /// of multiple floating-point units.
  /// An example of such a type is ppc_fp128, also known as double-double, which
  /// consists of two IEEE 754 doubles.
  /// \return true if this is a multi-unit floating-point type.
  bool isMultiUnitFPType() const {
    return getTypeID() == PPC_FP128TyID;
  }

  /// Return the floating-point semantics for this floating-point type.
  /// \return The floating-point semantics for this type.
  LLVM_ABI const fltSemantics &getFltSemantics() const;

  /// Return true if this is X86 AMX.
  /// \return true if this is the X86 AMX type.
  bool isX86_AMXTy() const { return getTypeID() == X86_AMXTyID; }

  /// Return true if this is a target extension type.
  /// \return true if this is a target extension type.
  bool isTargetExtTy() const { return getTypeID() == TargetExtTyID; }

  /// Return true if this is a target extension type with a scalable layout.
  /// \return true if this is a scalable target extension type.
  LLVM_ABI bool isScalableTargetExtTy() const;

  /// Return true if this is a type whose size is a known multiple of vscale.
  /// \param Visited Set used to detect cycles while walking contained types.
  /// \return true if this type's size is a known multiple of vscale.
  LLVM_ABI bool isScalableTy(SmallPtrSetImpl<const Type *> &Visited) const;
  /// Return true if this type's size depends on the runtime vscale factor.
  /// \return true if this type's size depends on the runtime vscale factor.
  LLVM_ABI bool isScalableTy() const;

  /// Return true if this type is or contains a target extension type that
  /// disallows being used as a global.
  /// \param Visited Set used to detect cycles while walking contained types.
  /// \return true if a non-global-allowed target extension type is present.
  LLVM_ABI bool
  containsNonGlobalTargetExtType(SmallPtrSetImpl<const Type *> &Visited) const;
  /// Return true if this type is or contains a target extension type that
  /// disallows being used as a global.
  /// \return true if a non-global-allowed target extension type is present.
  LLVM_ABI bool containsNonGlobalTargetExtType() const;

  /// Return true if this type is or contains a target extension type that
  /// disallows being used as a local.
  /// \param Visited Set used to detect cycles while walking contained types.
  /// \return true if a non-local-allowed target extension type is present.
  LLVM_ABI bool
  containsNonLocalTargetExtType(SmallPtrSetImpl<const Type *> &Visited) const;
  /// Return true if this type is or contains a target extension type that
  /// disallows being used as a local.
  /// \return true if a non-local-allowed target extension type is present.
  LLVM_ABI bool containsNonLocalTargetExtType() const;

  /// Return true if this is a FP type or a vector of FP.
  /// \return true if this is a floating-point type or vector thereof.
  bool isFPOrFPVectorTy() const { return getScalarType()->isFloatingPointTy(); }

  /// Return true if this is 'label'.
  /// \return true if this is the label type.
  bool isLabelTy() const { return getTypeID() == LabelTyID; }

  /// Return true if this is 'metadata'.
  /// \return true if this is the metadata type.
  bool isMetadataTy() const { return getTypeID() == MetadataTyID; }

  /// Return true if this is 'token'.
  /// \return true if this is the token type.
  bool isTokenTy() const { return getTypeID() == TokenTyID; }

  /// Returns true if this is 'token' or a token-like target type.s
  /// \return true if this is token or a token-like target type.
  LLVM_ABI bool isTokenLikeTy() const;

  /// True if this is an instance of ByteType.
  /// \return true if this is a ByteType.
  bool isByteTy() const { return getTypeID() == ByteTyID; }

  /// Return true if this is a ByteType of the given width.
  /// \param BitWidth Required byte bit width.
  /// \return true if this is a ByteType of the given width.
  LLVM_ABI bool isByteTy(unsigned BitWidth) const;

  /// Return true if this is a byte type or a vector of byte types.
  /// \return true if this is a byte type or vector thereof.
  bool isByteOrByteVectorTy() const { return getScalarType()->isByteTy(); }

  /// Return true if this is a byte type or a vector of byte types of
  /// the given width.
  /// \param BitWidth Required byte (element) bit width.
  /// \return true if this is a byte type or vector thereof of the given width.
  bool isByteOrByteVectorTy(unsigned BitWidth) const {
    return getScalarType()->isByteTy(BitWidth);
  }

  /// True if this is an instance of IntegerType.
  /// \return true if this is an IntegerType.
  bool isIntegerTy() const { return getTypeID() == IntegerTyID; }

  /// Return true if this is an IntegerType of the given width.
  /// \param BitWidth Required integer bit width.
  /// \return true if this is an IntegerType of the given width.
  LLVM_ABI inline bool isIntegerTy(unsigned BitWidth) const;

  /// Return true if this is an integer type or a vector of integer types.
  /// \return true if this is an integer type or vector thereof.
  bool isIntOrIntVectorTy() const { return getScalarType()->isIntegerTy(); }

  /// Return true if this is an integer type or a vector of integer types of
  /// the given width.
  /// \param BitWidth Required integer (element) bit width.
  /// \return true if the scalar type is an IntegerType of the given width.
  LLVM_ABI inline bool isIntOrIntVectorTy(unsigned BitWidth) const;

  /// Return true if this is an integer type or a pointer type.
  /// \return true if this is an integer type or a pointer type.
  bool isIntOrPtrTy() const { return isIntegerTy() || isPointerTy(); }

  /// True if this is an instance of FunctionType.
  /// \return true if this is a FunctionType.
  bool isFunctionTy() const { return getTypeID() == FunctionTyID; }

  /// True if this is an instance of StructType.
  /// \return true if this is a StructType.
  bool isStructTy() const { return getTypeID() == StructTyID; }

  /// True if this is an instance of ArrayType.
  /// \return true if this is an ArrayType.
  bool isArrayTy() const { return getTypeID() == ArrayTyID; }

  /// True if this is an instance of PointerType.
  /// \return true if this is a PointerType.
  bool isPointerTy() const { return getTypeID() == PointerTyID; }

  /// Return true if this is a pointer type or a vector of pointer types.
  /// \return true if this is a pointer type or vector thereof.
  bool isPtrOrPtrVectorTy() const { return getScalarType()->isPointerTy(); }

  /// True if this is an instance of VectorType.
  /// \return true if this is a VectorType.
  inline bool isVectorTy() const {
    return getTypeID() == ScalableVectorTyID || getTypeID() == FixedVectorTyID;
  }

  /// True if this is an instance of TargetExtType of RISC-V vector tuple.
  /// \return true if this is a RISC-V vector tuple target extension type.
  LLVM_ABI bool isRISCVVectorTupleTy() const;

  /// Return true if this type could be converted with a lossless BitCast to Ty.
  ///
  /// For example, i8* to i32*. BitCasts are valid for types of the same size
  /// only where no re-interpretation of the bits is done.
  /// \param Ty Destination type for the bitcast.
  /// \return true if a lossless bitcast to \p Ty is possible.
  LLVM_ABI bool canLosslesslyBitCastTo(Type *Ty) const;

  /// Return true if this type is empty, that is, it has no elements or all of
  /// its elements are empty.
  /// \return true if this type is empty.
  LLVM_ABI bool isEmptyTy() const;

  /// Return true if the type is "first class", meaning it is a valid type for a
  /// Value.
  /// \return true if this is a first-class type.
  LLVM_ABI bool isFirstClassType() const;

  /// Return true if the type is a valid type for a register in codegen. This
  /// includes all first-class types except struct and array types.
  /// \return true if this is a single-value type.
  bool isSingleValueType() const {
    return isFloatingPointTy() || isIntegerTy() || isPointerTy() ||
           isVectorTy() || isX86_AMXTy() || isTargetExtTy() || isByteTy();
  }

  /// Return true if the type is an aggregate type.
  ///
  /// This means it is valid as the first operand of an insertvalue or
  /// extractvalue instruction. This includes struct and array types, but does
  /// not include vector types.
  /// \return true if this is an aggregate type.
  bool isAggregateType() const {
    return getTypeID() == StructTyID || getTypeID() == ArrayTyID;
  }

  /// Return true if it makes sense to take the size of this type.
  ///
  /// To get the actual size for a particular target, it is reasonable to use
  /// the DataLayout subsystem to do this.
  /// \param Visited Optional set used to detect cycles while sizing derived
  ///        types.
  /// \return true if this type has a meaningful size.
  bool isSized(SmallPtrSetImpl<Type*> *Visited = nullptr) const {
    // If it's a primitive, it is always sized.
    if (getTypeID() == IntegerTyID || isFloatingPointTy() ||
        getTypeID() == PointerTyID || getTypeID() == X86_AMXTyID ||
        getTypeID() == ByteTyID)
      return true;
    // If it is not something that can have a size (e.g. a function or label),
    // it doesn't have a size.
    if (getTypeID() != StructTyID && getTypeID() != ArrayTyID &&
        !isVectorTy() && getTypeID() != TargetExtTyID)
      return false;
    // Otherwise we have to try harder to decide.
    return isSizedDerivedType(Visited);
  }

  /// Return the basic size of this type if it is a primitive type.
  ///
  /// These are fixed by LLVM and are not target-dependent.
  /// This will return zero if the type does not have a size or is not a
  /// primitive type.
  ///
  /// If this is a scalable vector type, the scalable property will be set and
  /// the runtime size will be a positive integer multiple of the base size.
  ///
  /// Note that this may not reflect the size of memory allocated for an
  /// instance of the type or the number of bytes that are written when an
  /// instance of the type is stored to memory. The DataLayout class provides
  /// additional query functions to provide this information.
  ///
  /// \return The primitive size in bits, or zero if the type has no size.
  LLVM_ABI TypeSize getPrimitiveSizeInBits() const LLVM_READONLY;

  /// If this is a vector type, return the getPrimitiveSizeInBits value for the
  /// element type. Otherwise return the getPrimitiveSizeInBits value for this
  /// type.
  /// \return The scalar (element) size in bits.
  LLVM_ABI unsigned getScalarSizeInBits() const LLVM_READONLY;

  /// Return the width of the mantissa of this floating-point type.
  ///
  /// This is only valid on floating-point types. If the FP type does not have a
  /// stable mantissa (e.g. ppc long double), this method returns -1.
  /// \return The mantissa width in bits, or -1 if unstable.
  LLVM_ABI int getFPMantissaWidth() const;

  /// If this is a vector type, return the element type, otherwise return
  /// 'this'.
  /// \return The element type of a vector, or this type otherwise.
  inline Type *getScalarType() const {
    if (isVectorTy())
      return getContainedType(0);
    return const_cast<Type *>(this);
  }

  //===--------------------------------------------------------------------===//
  // Type Iteration support.
  //
  /// Iterator over this type's contained (subtype) types.
  using subtype_iterator = Type * const *;

  /// Iterator to the first contained (subtype) type.
  /// \return Iterator to the first contained subtype.
  subtype_iterator subtype_begin() const { return ContainedTys; }
  /// Iterator past the last contained (subtype) type.
  /// \return Iterator past the last contained subtype.
  subtype_iterator subtype_end() const { return &ContainedTys[NumContainedTys];}
  /// Return an array view of this type's contained subtypes.
  /// \return An array view of this type's contained subtypes.
  ArrayRef<Type*> subtypes() const {
    return ArrayRef(subtype_begin(), subtype_end());
  }

  /// Reverse iterator over contained (subtype) types.
  using subtype_reverse_iterator = std::reverse_iterator<subtype_iterator>;

  /// Reverse iterator to the last contained (subtype) type.
  /// \return Reverse iterator to the last contained subtype.
  subtype_reverse_iterator subtype_rbegin() const {
    return subtype_reverse_iterator(subtype_end());
  }
  /// Reverse iterator past the first contained (subtype) type.
  /// \return Reverse iterator past the first contained subtype.
  subtype_reverse_iterator subtype_rend() const {
    return subtype_reverse_iterator(subtype_begin());
  }

  /// Return the type contained at index \p i in this derived type.
  ///
  /// This method is used to implement the type iterator (defined at the end of
  /// the file). For derived types, this returns the types 'contained' in the
  /// derived type.
  /// \param i Zero-based index into the contained types.
  /// \return The contained type at index \p i.
  Type *getContainedType(unsigned i) const {
    assert(i < NumContainedTys && "Index out of range!");
    return ContainedTys[i];
  }

  /// Return the number of types in the derived type.
  /// \return The number of contained types.
  unsigned getNumContainedTypes() const { return NumContainedTys; }

  //===--------------------------------------------------------------------===//
  // Helper methods corresponding to subclass methods.  This forces a cast to
  // the specified subclass and calls its accessor.  "getArrayNumElements" (for
  // example) is shorthand for cast<ArrayType>(Ty)->getNumElements().  This is
  // only intended to cover the core methods that are frequently used, helper
  // methods should not be added here.

  LLVM_ABI inline unsigned getIntegerBitWidth() const;
  /// Return the bit width of this byte type (or of its scalar element).
  /// \return The bit width of this byte type or its scalar element.
  LLVM_ABI inline unsigned getByteBitWidth() const;

  LLVM_ABI inline Type *getFunctionParamType(unsigned i) const;
  LLVM_ABI inline unsigned getFunctionNumParams() const;
  LLVM_ABI inline bool isFunctionVarArg() const;

  LLVM_ABI inline StringRef getStructName() const;
  LLVM_ABI inline unsigned getStructNumElements() const;
  LLVM_ABI inline Type *getStructElementType(unsigned N) const;

  LLVM_ABI inline uint64_t getArrayNumElements() const;

  /// Return the element type of this array type.
  /// \return The element type of this array type.
  Type *getArrayElementType() const {
    assert(getTypeID() == ArrayTyID);
    return ContainedTys[0];
  }

  LLVM_ABI inline StringRef getTargetExtName() const;

  /// Given vector type, change the element type,
  /// whilst keeping the old number of elements.
  /// For non-vectors simply returns \p EltTy.
  /// \param EltTy Replacement scalar or vector element type.
  /// \return This type with its scalar or element type replaced by \p EltTy.
  LLVM_ABI inline Type *getWithNewType(Type *EltTy) const;

  /// Given an integer or vector type, change the lane bitwidth to NewBitwidth,
  /// whilst keeping the old number of lanes.
  /// \param NewBitWidth Desired integer (or integer-element) bit width.
  /// \return This type with integer bit width replaced by \p NewBitWidth.
  LLVM_ABI inline Type *getWithNewBitWidth(unsigned NewBitWidth) const;

  /// Given scalar/vector integer type, returns a type with elements twice as
  /// wide as in the original type. For vectors, preserves element count.
  /// \return A type with elements twice as wide as in this type.
  LLVM_ABI inline Type *getExtendedType() const;

  /// Given scalar/vector integer type, returns a type with elements half as
  /// wide as in the original type. For vectors, preserves element count.
  /// \return A type with elements half as wide as in this type.
  LLVM_ABI inline Type *getTruncatedType() const;

  /// Get the address space of this pointer or pointer vector type.
  /// \return The address space of this pointer or pointer-vector type.
  LLVM_ABI inline unsigned getPointerAddressSpace() const;

  //===--------------------------------------------------------------------===//
  // Static members exported by the Type class itself.  Useful for getting
  // instances of Type.
  //

  /// Return a type based on an identifier.
  /// \param C LLVM context in which to unique the type.
  /// \param IDNumber TypeID of the primitive type to return.
  /// \return The uniqued primitive type for \p IDNumber, or null if invalid.
  LLVM_ABI static Type *getPrimitiveType(LLVMContext &C, TypeID IDNumber);

  //===--------------------------------------------------------------------===//
  // These are the builtin types that are always available.
  //
  /// Return the void type.
  /// \param C LLVM context in which to unique the type.
  /// \return The void type.
  LLVM_ABI static Type *getVoidTy(LLVMContext &C);
  /// Return the label type.
  /// \param C LLVM context in which to unique the type.
  /// \return The label type.
  LLVM_ABI static Type *getLabelTy(LLVMContext &C);
  /// Return the IEEE half-precision (16-bit) floating-point type.
  /// \param C LLVM context in which to unique the type.
  /// \return The IEEE half-precision floating-point type.
  LLVM_ABI static Type *getHalfTy(LLVMContext &C);
  /// Return the bfloat (16-bit) floating-point type.
  /// \param C LLVM context in which to unique the type.
  /// \return The bfloat floating-point type.
  LLVM_ABI static Type *getBFloatTy(LLVMContext &C);
  /// Return the IEEE single-precision (32-bit) floating-point type.
  /// \param C LLVM context in which to unique the type.
  /// \return The IEEE single-precision floating-point type.
  LLVM_ABI static Type *getFloatTy(LLVMContext &C);
  /// Return the IEEE double-precision (64-bit) floating-point type.
  /// \param C LLVM context in which to unique the type.
  /// \return The IEEE double-precision floating-point type.
  LLVM_ABI static Type *getDoubleTy(LLVMContext &C);
  /// Return the metadata type used for metadata operands in the IR.
  /// \param C LLVM context in which to unique the type.
  /// \return The metadata type.
  LLVM_ABI static Type *getMetadataTy(LLVMContext &C);
  /// Return the x86 80-bit extended-precision floating-point type.
  /// \param C LLVM context in which to unique the type.
  /// \return The x86 80-bit extended-precision floating-point type.
  LLVM_ABI static Type *getX86_FP80Ty(LLVMContext &C);
  /// Return the 128-bit IEEE floating-point type (fp128).
  /// \param C LLVM context in which to unique the type.
  /// \return The 128-bit IEEE floating-point type (fp128).
  LLVM_ABI static Type *getFP128Ty(LLVMContext &C);
  /// Return the PowerPC double-double (ppc_fp128) floating-point type.
  /// \param C LLVM context in which to unique the type.
  /// \return The PowerPC double-double floating-point type.
  LLVM_ABI static Type *getPPC_FP128Ty(LLVMContext &C);
  /// Return the X86 AMX type for the given context.
  /// \param C LLVM context in which to unique the type.
  /// \return The X86 AMX type.
  LLVM_ABI static Type *getX86_AMXTy(LLVMContext &C);
  /// Return the token type.
  /// \param C LLVM context in which to unique the type.
  /// \return The token type.
  LLVM_ABI static Type *getTokenTy(LLVMContext &C);
  /// Return a byte type with bit width \p N.
  /// \param C LLVM context in which to unique the type.
  /// \param N Bit width of the byte type.
  /// \return The uniqued ByteType with bit width \p N.
  LLVM_ABI static ByteType *getByteNTy(LLVMContext &C, unsigned N);
  /// Return the 1-bit byte type.
  /// \param C LLVM context in which to unique the type.
  /// \return The 1-bit byte type.
  LLVM_ABI static ByteType *getByte1Ty(LLVMContext &C);
  /// Return the 8-bit byte type.
  /// \param C LLVM context in which to unique the type.
  /// \return The 8-bit byte type.
  LLVM_ABI static ByteType *getByte8Ty(LLVMContext &C);
  /// Return the 16-bit byte type for the given context.
  /// \param C LLVM context in which to unique the type.
  /// \return The 16-bit byte type.
  LLVM_ABI static ByteType *getByte16Ty(LLVMContext &C);
  /// Return the 32-bit byte type for the given context.
  /// \param C LLVM context in which to unique the type.
  /// \return The 32-bit byte type.
  LLVM_ABI static ByteType *getByte32Ty(LLVMContext &C);
  /// Return the 64-bit byte type.
  /// \param C LLVM context in which to unique the type.
  /// \return The 64-bit byte type.
  LLVM_ABI static ByteType *getByte64Ty(LLVMContext &C);
  /// Return the 128-bit byte type.
  /// \param C LLVM context in which to unique the type.
  /// \return The 128-bit byte type.
  LLVM_ABI static ByteType *getByte128Ty(LLVMContext &C);
  /// Return an integer type with bit width \p N.
  /// \param C LLVM context in which to unique the type.
  /// \param N Bit width of the integer type.
  /// \return The uniqued IntegerType with bit width \p N.
  LLVM_ABI static IntegerType *getIntNTy(LLVMContext &C, unsigned N);
  /// Return the 1-bit integer type.
  /// \param C LLVM context in which to unique the type.
  /// \return The 1-bit integer type.
  LLVM_ABI static IntegerType *getInt1Ty(LLVMContext &C);
  /// Return the 8-bit integer type.
  /// \param C LLVM context in which to unique the type.
  /// \return The 8-bit integer type.
  LLVM_ABI static IntegerType *getInt8Ty(LLVMContext &C);
  /// Return the 16-bit integer type.
  /// \param C LLVM context in which to unique the type.
  /// \return The 16-bit integer type.
  LLVM_ABI static IntegerType *getInt16Ty(LLVMContext &C);
  /// Return the 32-bit integer type.
  /// \param C LLVM context in which to unique the type.
  /// \return The 32-bit integer type.
  LLVM_ABI static IntegerType *getInt32Ty(LLVMContext &C);
  /// Return the 64-bit integer type.
  /// \param C LLVM context in which to unique the type.
  /// \return The 64-bit integer type.
  LLVM_ABI static IntegerType *getInt64Ty(LLVMContext &C);
  /// Return the 128-bit integer type.
  /// \param C LLVM context in which to unique the type.
  /// \return The 128-bit integer type.
  LLVM_ABI static IntegerType *getInt128Ty(LLVMContext &C);
  /// Return the LLVM scalar type corresponding to a C++ scalar type \p ScalarTy.
  /// \param C LLVM context in which to unique the type.
  /// \return The LLVM type matching C++ scalar type \p ScalarTy.
  template <typename ScalarTy> static Type *getScalarTy(LLVMContext &C) {
    int noOfBits = sizeof(ScalarTy) * CHAR_BIT;
    if (std::is_integral<ScalarTy>::value) {
      return (Type*) Type::getIntNTy(C, noOfBits);
    } else if (std::is_floating_point<ScalarTy>::value) {
      switch (noOfBits) {
      case 32:
        return Type::getFloatTy(C);
      case 64:
        return Type::getDoubleTy(C);
      }
    }
    llvm_unreachable("Unsupported type in Type::getScalarTy");
  }
  /// Return the floating-point type matching semantics \p S.
  /// \param C LLVM context in which to unique the type.
  /// \param S Floating-point semantics that select the type.
  /// \return The floating-point type matching semantics \p S.
  LLVM_ABI static Type *getFloatingPointTy(LLVMContext &C,
                                           const fltSemantics &S);

  //===--------------------------------------------------------------------===//
  // Convenience methods for getting byte/integer types.
  //
  /// Returns an integer (vector of integer) type with the same size of a byte
  /// of the given byte (vector of byte) type.
  /// \param Ty Byte or byte-vector type to convert.
  /// \return An integer or integer-vector type matching the size of \p Ty.
  LLVM_ABI static Type *getIntFromByteType(Type *Ty);

  /// Returns a byte (vector of byte) type with the same size of an integer of
  /// the given integer (vector of integer) type.
  /// \param Ty Integer or integer-vector type to convert.
  /// \return A byte or byte-vector type matching the size of \p Ty.
  LLVM_ABI static Type *getByteFromIntType(Type *Ty);

  //===--------------------------------------------------------------------===//
  // Convenience methods for getting pointer types.
  //
  /// Return the WebAssembly externref target-extension type.
  /// \param C LLVM context in which to unique the type.
  /// \return The WebAssembly externref target-extension type.
  LLVM_ABI static Type *getWasm_ExternrefTy(LLVMContext &C);
  /// Return the WebAssembly funcref target-extension type.
  /// \param C LLVM context in which to unique the type.
  /// \return The WebAssembly funcref target-extension type.
  LLVM_ABI static Type *getWasm_FuncrefTy(LLVMContext &C);

private:
  /// Derived types like structures and arrays are sized iff all of the members
  /// of the type are sized as well. Since asking for their size is relatively
  /// uncommon, move this operation out-of-line.
  LLVM_ABI bool
  isSizedDerivedType(SmallPtrSetImpl<Type *> *Visited = nullptr) const;
};

// Printing of types.
/// Print \p T to \p OS using its LLVM assembly syntax.
/// \param OS Output stream.
/// \param T Type to print.
/// \return The output stream \p OS.
inline raw_ostream &operator<<(raw_ostream &OS, const Type &T) {
  T.print(OS);
  return OS;
}

// allow isa<PointerType>(x) to work without DerivedTypes.h included.
/// Specialization of \c isa_impl so \c isa<PointerType> works without DerivedTypes.h.
template <> struct isa_impl<PointerType, Type> {
  /// Return true if \p Ty is a pointer type.
  /// \param Ty Type to test.
  /// \return true if \p Ty is a pointer type.
  static inline bool doit(const Type &Ty) {
    return Ty.getTypeID() == Type::PointerTyID;
  }
};

// Create wrappers for C Binding types (see CBindingWrapping.h).
/// C API conversion helpers for \c Type / \c LLVMTypeRef, including typed \c unwrap.
/// \param P Pointer or opaque reference to convert.
/// \return The converted C++ \c Type pointer or C \c LLVMTypeRef.
DEFINE_ISA_CONVERSION_FUNCTIONS(Type, LLVMTypeRef)

/* Specialized opaque type conversions.
 */
/// Unwrap an array of opaque \c LLVMTypeRef values as \c Type pointers.
/// \param Tys Array of opaque type references.
/// \return The array reinterpreted as \c Type pointers.
inline Type **unwrap(LLVMTypeRef* Tys) {
  return reinterpret_cast<Type**>(Tys);
}

/// Wrap an array of \c Type pointers for the C API.
/// \param Tys Array of Type pointers.
/// \return The array reinterpreted as \c LLVMTypeRef values.
inline LLVMTypeRef *wrap(Type **Tys) {
  return reinterpret_cast<LLVMTypeRef *>(Tys);
}

} // end namespace llvm

#endif // LLVM_IR_TYPE_H
