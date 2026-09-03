//===- ABI/Types.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the type system for the LLVMABI library, which mirrors
/// ABI-relevant aspects of frontend types.
///
//===----------------------------------------------------------------------===//
#ifndef LLVM_ABI_TYPES_H
#define LLVM_ABI_TYPES_H

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/TypeSize.h"

namespace llvm {
namespace abi {

/// Discriminator for the concrete subclass of \c Type.
enum class TypeKind {
  /// The void type.
  Void,
  /// A C++ pointer-to-member type.
  MemberPointer,
  /// A complex number type with real and imaginary parts.
  Complex,
  /// An integer type, including bool and _BitInt.
  Integer,
  /// A floating-point type.
  Float,
  /// A pointer type.
  Pointer,
  /// An array type, including matrix types.
  Array,
  /// A vector type, fixed or scalable.
  Vector,
  /// A struct, class, or union type.
  Record,
};

/// Represents the ABI-specific view of a type in LLVM.
///
/// This abstracts platform and language-specific ABI details from the
/// frontend, providing a consistent interface for the ABI Library.
class Type {
private:
  TypeSize getTypeStoreSize() const {
    TypeSize StoreSizeInBits = getTypeStoreSizeInBits();
    return {StoreSizeInBits.getKnownMinValue() / 8,
            StoreSizeInBits.isScalable()};
  }
  TypeSize getTypeStoreSizeInBits() const {
    TypeSize BaseSize = getSizeInBits();
    uint64_t AlignedSizeInBits =
        alignToPowerOf2(BaseSize.getKnownMinValue(), 8);
    return {AlignedSizeInBits, BaseSize.isScalable()};
  }

protected:
  /// The kind of this type.
  TypeKind Kind;
  /// The size of this type in bits.
  TypeSize SizeInBits;
  /// The ABI alignment of this type.
  Align ABIAlignment;

  /// Construct a Type with the given kind, size, and alignment.
  ///
  /// \param K          The type kind discriminator.
  /// \param SizeInBits The size of the type in bits.
  /// \param ABIAlign   The ABI alignment of the type.
  Type(TypeKind K, TypeSize SizeInBits, Align ABIAlign)
      : Kind(K), SizeInBits(SizeInBits), ABIAlignment(ABIAlign) {}

public:
  /// Return the kind of this type.
  ///
  /// \return The kind of this type.
  TypeKind getKind() const { return Kind; }
  /// Return the size of this type in bits.
  ///
  /// \return The size of this type in bits.
  TypeSize getSizeInBits() const { return SizeInBits; }
  /// Return the ABI alignment of this type.
  ///
  /// \return The ABI alignment of this type.
  Align getAlignment() const { return ABIAlignment; }

  /// Return the allocation size of this type in bytes.
  ///
  /// \return The allocation size of this type in bytes.
  TypeSize getTypeAllocSize() const {
    return alignTo(getTypeStoreSize(), getAlignment().value());
  }

  /// Return true if this is the void type.
  ///
  /// \return True if this is the void type.
  bool isVoid() const { return Kind == TypeKind::Void; }
  /// Return true if this is an integer type.
  ///
  /// \return True if this is an integer type.
  bool isInteger() const { return Kind == TypeKind::Integer; }
  /// Return true if this is a floating-point type.
  ///
  /// \return True if this is a floating-point type.
  bool isFloat() const { return Kind == TypeKind::Float; }
  /// Return true if this is a pointer type.
  ///
  /// \return True if this is a pointer type.
  bool isPointer() const { return Kind == TypeKind::Pointer; }
  /// Return true if this is an array type.
  ///
  /// \return True if this is an array type.
  bool isArray() const { return Kind == TypeKind::Array; }
  /// Return true if this is a vector type.
  ///
  /// \return True if this is a vector type.
  bool isVector() const { return Kind == TypeKind::Vector; }
  /// Return true if this is a record type.
  ///
  /// \return True if this is a record type.
  bool isRecord() const { return Kind == TypeKind::Record; }
  /// Return true if this is a member-pointer type.
  ///
  /// \return True if this is a member-pointer type.
  bool isMemberPointer() const { return Kind == TypeKind::MemberPointer; }
  /// Return true if this is a complex type.
  ///
  /// \return True if this is a complex type.
  bool isComplex() const { return Kind == TypeKind::Complex; }
  /// Return true if this type has zero size.
  ///
  /// \return True if this type has zero size.
  bool isZeroSize() const { return getSizeInBits().getFixedValue() == 0; }
};

/// The void type.
class VoidType : public Type {
public:
  /// Construct a void type.
  VoidType() : Type(TypeKind::Void, TypeSize::getFixed(0), Align(1)) {}

  /// Return true if \p T is a VoidType.
  ///
  /// \param T The type to test.
  /// \return True if \p T is a VoidType.
  static bool classof(const Type *T) { return T->getKind() == TypeKind::Void; }
};

/// A complex number type with real and imaginary parts of the same element
/// type.
class ComplexType : public Type {
public:
  /// Construct a complex type.
  ///
  /// \param ElementType The type of each component (real and imaginary).
  /// \param SizeInBits  The total size of the complex type in bits.
  /// \param Alignment   The ABI alignment of the complex type.
  ComplexType(const Type *ElementType, uint64_t SizeInBits, Align Alignment)
      : Type(TypeKind::Complex, TypeSize::getFixed(SizeInBits), Alignment),
        ElementType(ElementType) {}

  /// Return the element type of the real and imaginary parts.
  ///
  /// \return The element type of the real and imaginary parts.
  const Type *getElementType() const { return ElementType; }

  /// Return true if \p T is a ComplexType.
  ///
  /// \param T The type to test.
  /// \return True if \p T is a ComplexType.
  static bool classof(const Type *T) {
    return T->getKind() == TypeKind::Complex;
  }

private:
  const Type *ElementType;
};

/// An integer type, including bool and _BitInt.
class IntegerType : public Type {
private:
  bool IsSigned;
  bool IsBitInt;

public:
  /// Construct an integer type.
  ///
  /// \param BitWidth The width of the integer in bits.
  /// \param ABIAlign The ABI alignment of the integer.
  /// \param IsSigned True if the integer is signed.
  /// \param IsBitInt True if this is a C/_BitInt type.
  IntegerType(uint64_t BitWidth, Align ABIAlign, bool IsSigned,
              bool IsBitInt = false)
      : Type(TypeKind::Integer, TypeSize::getFixed(BitWidth), ABIAlign),
        IsSigned(IsSigned), IsBitInt(IsBitInt) {}

  /// Return true if this integer is signed.
  ///
  /// \return True if this integer is signed.
  bool isSigned() const { return IsSigned; }
  /// Return true if this is a _BitInt type.
  ///
  /// \return True if this is a _BitInt type.
  bool isBitInt() const { return IsBitInt; }
  /// Return true if this is a 1-bit bool type (not _BitInt).
  ///
  /// \return True if this is a 1-bit bool type (not _BitInt).
  bool isBool() const {
    return getSizeInBits().getFixedValue() == 1 && !IsBitInt;
  }

  /// Return true if \p T is an IntegerType.
  ///
  /// \param T The type to test.
  /// \return True if \p T is an IntegerType.
  static bool classof(const Type *T) {
    return T->getKind() == TypeKind::Integer;
  }
};

/// A floating-point type described by APFloat semantics.
class FloatType : public Type {
private:
  const fltSemantics *Semantics;

public:
  /// Construct a floating-point type.
  ///
  /// \param FloatSemantics The APFloat semantics describing the format.
  /// \param ABIAlign       The ABI alignment of the floating-point type.
  FloatType(const fltSemantics &FloatSemantics, Align ABIAlign)
      : Type(TypeKind::Float,
             TypeSize::getFixed(APFloat::getSizeInBits(FloatSemantics)),
             ABIAlign),
        Semantics(&FloatSemantics) {}

  /// Return the APFloat semantics of this floating-point type.
  ///
  /// \return The APFloat semantics of this floating-point type.
  const fltSemantics *getSemantics() const { return Semantics; }
  /// Return true if \p T is a FloatType.
  ///
  /// \param T The type to test.
  /// \return True if \p T is a FloatType.
  static bool classof(const Type *T) { return T->getKind() == TypeKind::Float; }
};

/// Base class for pointer and member-pointer types.
class PointerLikeType : public Type {
protected:
  /// The address space of the pointer.
  unsigned AddrSpace;
  /// Construct a pointer-like type.
  ///
  /// \param K          The concrete type kind (Pointer or MemberPointer).
  /// \param SizeInBits The size of the pointer in bits.
  /// \param ABIAlign   The ABI alignment of the pointer.
  /// \param AS         The address space of the pointer.
  PointerLikeType(TypeKind K, TypeSize SizeInBits, Align ABIAlign, unsigned AS)
      : Type(K, SizeInBits, ABIAlign), AddrSpace(AS) {}

public:
  /// Return the address space of this pointer-like type.
  ///
  /// \return The address space of this pointer-like type.
  unsigned getAddrSpace() const { return AddrSpace; }
  /// Return true if this is a member-pointer type.
  ///
  /// \return True if this is a member-pointer type.
  bool isMemberPointer() const { return getKind() == TypeKind::MemberPointer; }

  /// Return true if \p T is a PointerType or MemberPointerType.
  ///
  /// \param T The type to test.
  /// \return True if \p T is a PointerType or MemberPointerType.
  static bool classof(const Type *T) {
    return T->getKind() == TypeKind::Pointer ||
           T->getKind() == TypeKind::MemberPointer;
  }
};

/// A pointer type.
class PointerType : public PointerLikeType {
public:
  /// Construct a pointer type.
  ///
  /// \param Size         The size of the pointer in bits.
  /// \param ABIAlign     The ABI alignment of the pointer.
  /// \param AddressSpace The address space of the pointer.
  PointerType(uint64_t Size, Align ABIAlign, unsigned AddressSpace = 0)
      : PointerLikeType(TypeKind::Pointer, TypeSize::getFixed(Size), ABIAlign,
                        AddressSpace) {}

  /// Return true if \p T is a PointerType.
  ///
  /// \param T The type to test.
  /// \return True if \p T is a PointerType.
  static bool classof(const Type *T) {
    return T->getKind() == TypeKind::Pointer;
  }
};

/// A C++ pointer-to-member type.
class MemberPointerType : public PointerLikeType {
private:
  bool IsFunctionPointer;

public:
  /// Construct a member-pointer type.
  ///
  /// \param IsFunctionPointer True if this points to a member function.
  /// \param SizeInBits        The size of the member pointer in bits.
  /// \param ABIAlign          The ABI alignment of the member pointer.
  /// \param AddressSpace      The address space of the member pointer.
  MemberPointerType(bool IsFunctionPointer, uint64_t SizeInBits, Align ABIAlign,
                    unsigned AddressSpace = 0)
      : PointerLikeType(TypeKind::MemberPointer, TypeSize::getFixed(SizeInBits),
                        ABIAlign, AddressSpace),
        IsFunctionPointer(IsFunctionPointer) {}
  /// Return true if this member pointer points to a function.
  ///
  /// \return True if this member pointer points to a function.
  bool isFunctionPointer() const { return IsFunctionPointer; }

  /// Return true if \p T is a MemberPointerType.
  ///
  /// \param T The type to test.
  /// \return True if \p T is a MemberPointerType.
  static bool classof(const Type *T) {
    return T->getKind() == TypeKind::MemberPointer;
  }
};

/// An array type, optionally representing a matrix type.
class ArrayType : public Type {
private:
  const Type *ElementType;
  uint64_t NumElements;
  bool IsMatrix;

public:
  /// Construct an array type.
  ///
  /// \param ElementType The type of each array element.
  /// \param NumElements The number of elements in the array.
  /// \param SizeInBits  The total size of the array in bits.
  /// \param IsMatrixType True if this array represents a matrix type.
  ArrayType(const Type *ElementType, uint64_t NumElements, uint64_t SizeInBits,
            bool IsMatrixType = false)
      : Type(TypeKind::Array, TypeSize::getFixed(SizeInBits),
             ElementType->getAlignment()),
        ElementType(ElementType), NumElements(NumElements),
        IsMatrix(IsMatrixType) {}

  /// Return the element type of this array.
  ///
  /// \return The element type of this array.
  const Type *getElementType() const { return ElementType; }
  /// Return the number of elements in this array.
  ///
  /// \return The number of elements in this array.
  uint64_t getNumElements() const { return NumElements; }
  /// Return true if this array represents a matrix type.
  ///
  /// \return True if this array represents a matrix type.
  bool isMatrixType() const { return IsMatrix; }

  /// Return true if \p T is an ArrayType.
  ///
  /// \param T The type to test.
  /// \return True if \p T is an ArrayType.
  static bool classof(const Type *T) { return T->getKind() == TypeKind::Array; }
};

/// A vector type with a fixed or scalable element count.
class VectorType : public Type {
private:
  const Type *ElementType;
  ElementCount NumElements;

public:
  /// Construct a vector type.
  ///
  /// \param ElementType The type of each vector element.
  /// \param NumElements The number of elements (fixed or scalable).
  /// \param ABIAlign    The ABI alignment of the vector.
  VectorType(const Type *ElementType, ElementCount NumElements, Align ABIAlign)
      : Type(TypeKind::Vector,
             TypeSize(ElementType->getSizeInBits().getFixedValue() *
                          NumElements.getKnownMinValue(),
                      NumElements.isScalable()),
             ABIAlign),
        ElementType(ElementType), NumElements(NumElements) {}

  /// Return the element type of this vector.
  ///
  /// \return The element type of this vector.
  const Type *getElementType() const { return ElementType; }
  /// Return the number of elements in this vector.
  ///
  /// \return The number of elements in this vector.
  ElementCount getNumElements() const { return NumElements; }

  /// Return true if \p T is a VectorType.
  ///
  /// \param T The type to test.
  /// \return True if \p T is a VectorType.
  static bool classof(const Type *T) {
    return T->getKind() == TypeKind::Vector;
  }
};

/// Describes a field, base class, or virtual base within a record.
struct FieldInfo {
  /// The type of this field or base.
  const Type *FieldType;
  /// The bit offset of this field or base within the record.
  uint64_t OffsetInBits;
  /// The width in bits when this is a bit-field; otherwise unused.
  uint64_t BitFieldWidth;
  /// True if this field is a bit-field.
  bool IsBitField;
  /// True if this is an unnamed bit-field.
  bool IsUnnamedBitfield;
  /// True if this entry represents a virtual base class.
  bool IsVirtualBase;

  /// Construct field info for a record member or base.
  ///
  /// \param FieldType         The type of the field or base.
  /// \param OffsetInBits      Bit offset within the containing record.
  /// \param IsBitField        True if this is a bit-field.
  /// \param BitFieldWidth     Width in bits when \p IsBitField is true.
  /// \param IsUnnamedBitField True if this is an unnamed bit-field.
  /// \param IsVirtualBase     True if this entry is a virtual base.
  FieldInfo(const Type *FieldType, uint64_t OffsetInBits = 0,
            bool IsBitField = false, uint64_t BitFieldWidth = 0,
            bool IsUnnamedBitField = false, bool IsVirtualBase = false)
      : FieldType(FieldType), OffsetInBits(OffsetInBits),
        BitFieldWidth(BitFieldWidth), IsBitField(IsBitField),
        IsUnnamedBitfield(IsUnnamedBitField), IsVirtualBase(IsVirtualBase) {}

  /// Return true if this field or base has zero size.
  ///
  /// \return True if this field or base has zero size.
  LLVM_ABI bool isEmpty() const;
};

/// How fields of a record are packed in memory.
enum class StructPacking {
  /// Use the default ABI packing for the target.
  Default,
  /// Pack fields with no padding between them.
  Packed,
  /// Use an explicitly specified packing alignment.
  ExplicitPacking,
};

/// Bitmask flags describing properties of a record type.
enum RecordFlags : unsigned {
  /// No special record properties.
  None = 0,
  /// The record can be passed in registers under the ABI.
  CanPassInRegisters = 1 << 0,
  /// The record is a union.
  IsUnion = 1 << 1,
  /// The record is a transparent union.
  IsTransparent = 1 << 2,
  /// The record is a C++ class or struct.
  IsCXXRecord = 1 << 3,
  /// The record is polymorphic (has a vtable).
  IsPolymorphic = 1 << 4,
  /// The record has a flexible array member.
  HasFlexibleArrayMember = 1 << 5,
  LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */ HasFlexibleArrayMember),
};

/// A struct, class, or union type with fields and optional base classes.
class RecordType : public Type {
private:
  ArrayRef<FieldInfo> Fields;
  ArrayRef<FieldInfo> BaseClasses;
  ArrayRef<FieldInfo> VirtualBaseClasses;
  StructPacking Packing;
  RecordFlags Flags;

public:
  /// Construct a record type.
  ///
  /// \param StructFields The fields of the record.
  /// \param Bases        The direct base classes.
  /// \param VBases       The virtual base classes.
  /// \param Size         The size of the record.
  /// \param Align        The ABI alignment of the record.
  /// \param Pack         How fields are packed in memory.
  /// \param RecFlags     Bitmask flags describing the record.
  RecordType(ArrayRef<FieldInfo> StructFields, ArrayRef<FieldInfo> Bases,
             ArrayRef<FieldInfo> VBases, TypeSize Size, Align Align,
             StructPacking Pack = StructPacking::Default,
             RecordFlags RecFlags = RecordFlags::None)
      : Type(TypeKind::Record, Size, Align), Fields(StructFields),
        BaseClasses(Bases), VirtualBaseClasses(VBases), Packing(Pack),
        Flags(RecFlags) {}
  /// Return the number of fields in this record.
  ///
  /// \return The number of fields in this record.
  uint32_t getNumFields() const { return Fields.size(); }
  /// Return how fields of this record are packed.
  ///
  /// \return How fields of this record are packed.
  StructPacking getPacking() const { return Packing; }

  /// Return true if this record is a union.
  ///
  /// \return True if this record is a union.
  bool isUnion() const {
    return static_cast<unsigned>(Flags & RecordFlags::IsUnion) != 0;
  }
  /// Return true if this record is a C++ class or struct.
  ///
  /// \return True if this record is a C++ class or struct.
  bool isCXXRecord() const {
    return static_cast<unsigned>(Flags & RecordFlags::IsCXXRecord) != 0;
  }
  /// Return true if this record is polymorphic.
  ///
  /// \return True if this record is polymorphic.
  bool isPolymorphic() const {
    return static_cast<unsigned>(Flags & RecordFlags::IsPolymorphic) != 0;
  }
  /// Return true if this record can be passed in registers.
  ///
  /// \return True if this record can be passed in registers.
  bool canPassInRegisters() const {
    return static_cast<unsigned>(Flags & RecordFlags::CanPassInRegisters) != 0;
  }
  /// Return true if this record has a flexible array member.
  ///
  /// \return True if this record has a flexible array member.
  bool hasFlexibleArrayMember() const {
    return static_cast<unsigned>(Flags & RecordFlags::HasFlexibleArrayMember) !=
           0;
  }
  /// Return the number of direct base classes.
  ///
  /// \return The number of direct base classes.
  uint32_t getNumBaseClasses() const { return BaseClasses.size(); }
  /// Return the number of virtual base classes.
  ///
  /// \return The number of virtual base classes.
  uint32_t getNumVirtualBaseClasses() const {
    return VirtualBaseClasses.size();
  }
  /// Return true if this record is a transparent union.
  ///
  /// \return True if this record is a transparent union.
  bool isTransparentUnion() const {
    return static_cast<unsigned>(Flags & RecordFlags::IsTransparent) != 0;
  }
  /// Return the fields of this record.
  ///
  /// \return The fields of this record.
  ArrayRef<FieldInfo> getFields() const { return Fields; }

  /// Returns the direct base classes, both virtual and non-virtual.
  ///
  /// Mirrors clang::CXXRecordDecl::bases(). A virtual base is marked with
  /// FieldInfo::IsVirtualBase, and its offset is only meaningful when this
  /// record is the most-derived object.
  ///
  /// \return The direct base classes of this record.
  ArrayRef<FieldInfo> getBaseClasses() const { return BaseClasses; }

  /// Returns the virtual base classes, both direct and indirect.
  ///
  /// Mirrors clang::CXXRecordDecl::vbases(). Direct virtual bases therefore
  /// appear both here and in getBaseClasses().
  ///
  /// \return The virtual base classes of this record.
  ArrayRef<FieldInfo> getVirtualBaseClasses() const {
    return VirtualBaseClasses;
  }

  /// Return true if this record has no non-empty fields or bases.
  ///
  /// \return True if this record has no non-empty fields or bases.
  LLVM_ABI bool isEmpty() const;

  /// Returns the field or base whose extent contains \p OffsetInBits.
  ///
  /// Empty bases and unnamed bitfields are skipped. Returns nullptr if no
  /// such element exists.
  ///
  /// \param OffsetInBits Bit offset into the record to look up.
  /// \return The field or base containing \p OffsetInBits, or nullptr if none.
  LLVM_ABI const FieldInfo *
  getElementContainingOffset(unsigned OffsetInBits) const;

  /// Return true if \p T is a RecordType.
  ///
  /// \param T The type to test.
  /// \return True if \p T is a RecordType.
  static bool classof(const Type *T) {
    return T->getKind() == TypeKind::Record;
  }
};

/// TypeBuilder manages the lifecycle of ABI types using bump pointer
/// allocation. Types created by a TypeBuilder are valid for the lifetime of the
/// allocator.
///
/// Example usage:
/// \code
///   BumpPtrAllocator Alloc;
///   TypeBuilder Builder(Alloc);
///   const auto *IntTy = Builder.getIntegerType(32, Align(4), true);
/// \endcode
class TypeBuilder {
private:
  BumpPtrAllocator &Allocator;

public:
  /// Construct a TypeBuilder that allocates types in \p Alloc.
  ///
  /// \param Alloc The bump-pointer allocator that owns created types.
  explicit TypeBuilder(BumpPtrAllocator &Alloc) : Allocator(Alloc) {}

  /// Create and return a void type.
  ///
  /// \return The newly allocated void type.
  const VoidType *getVoidType() {
    return new (Allocator.Allocate<VoidType>()) VoidType();
  }

  /// Create and return an integer type.
  ///
  /// \param BitWidth The width of the integer in bits.
  /// \param Align    The ABI alignment of the integer.
  /// \param Signed   True if the integer is signed.
  /// \param IsBitInt True if this is a _BitInt type.
  /// \return The newly allocated integer type.
  const IntegerType *getIntegerType(uint64_t BitWidth, Align Align, bool Signed,
                                    bool IsBitInt = false) {
    return new (Allocator.Allocate<IntegerType>())
        IntegerType(BitWidth, Align, Signed, IsBitInt);
  }

  /// Create and return a floating-point type.
  ///
  /// \param Semantics The APFloat semantics describing the format.
  /// \param Align     The ABI alignment of the floating-point type.
  /// \return The newly allocated floating-point type.
  const FloatType *getFloatType(const fltSemantics &Semantics, Align Align) {
    return new (Allocator.Allocate<FloatType>()) FloatType(Semantics, Align);
  }

  /// Create and return a pointer type.
  ///
  /// \param Size      The size of the pointer in bits.
  /// \param Align     The ABI alignment of the pointer.
  /// \param Addrspace The address space of the pointer.
  /// \return The newly allocated pointer type.
  const PointerType *getPointerType(uint64_t Size, Align Align,
                                    unsigned Addrspace = 0) {
    return new (Allocator.Allocate<PointerType>())
        PointerType(Size, Align, Addrspace);
  }

  /// Create and return an array type.
  ///
  /// \param ElementType  The type of each array element.
  /// \param NumElements  The number of elements in the array.
  /// \param SizeInBits   The total size of the array in bits.
  /// \param IsMatrixType True if this array represents a matrix type.
  /// \return The newly allocated array type.
  const ArrayType *getArrayType(const Type *ElementType, uint64_t NumElements,
                                uint64_t SizeInBits,
                                bool IsMatrixType = false) {
    return new (Allocator.Allocate<ArrayType>())
        ArrayType(ElementType, NumElements, SizeInBits, IsMatrixType);
  }

  /// Create and return a vector type.
  ///
  /// \param ElementType The type of each vector element.
  /// \param NumElements The number of elements (fixed or scalable).
  /// \param Align       The ABI alignment of the vector.
  /// \return The newly allocated vector type.
  const VectorType *getVectorType(const Type *ElementType,
                                  ElementCount NumElements, Align Align) {
    return new (Allocator.Allocate<VectorType>())
        VectorType(ElementType, NumElements, Align);
  }

  /// Create and return a record (struct or class) type.
  ///
  /// \param Fields             The fields of the record.
  /// \param Size               The size of the record.
  /// \param Align              The ABI alignment of the record.
  /// \param Pack               How fields are packed in memory.
  /// \param BaseClasses        The direct base classes.
  /// \param VirtualBaseClasses The virtual base classes.
  /// \param RecFlags           Bitmask flags describing the record.
  /// \return The newly allocated record type.
  const RecordType *getRecordType(ArrayRef<FieldInfo> Fields, TypeSize Size,
                                  Align Align,
                                  StructPacking Pack = StructPacking::Default,
                                  ArrayRef<FieldInfo> BaseClasses = {},
                                  ArrayRef<FieldInfo> VirtualBaseClasses = {},
                                  RecordFlags RecFlags = RecordFlags::None) {
    FieldInfo *FieldArray = Allocator.Allocate<FieldInfo>(Fields.size());
    std::copy(Fields.begin(), Fields.end(), FieldArray);

    FieldInfo *BaseArray = nullptr;
    if (!BaseClasses.empty()) {
      BaseArray = Allocator.Allocate<FieldInfo>(BaseClasses.size());
      std::copy(BaseClasses.begin(), BaseClasses.end(), BaseArray);
    }

    FieldInfo *VBaseArray = nullptr;
    if (!VirtualBaseClasses.empty()) {
      VBaseArray = Allocator.Allocate<FieldInfo>(VirtualBaseClasses.size());
      std::copy(VirtualBaseClasses.begin(), VirtualBaseClasses.end(),
                VBaseArray);
    }

    ArrayRef<FieldInfo> FieldsRef(FieldArray, Fields.size());
    ArrayRef<FieldInfo> BasesRef(BaseArray, BaseClasses.size());
    ArrayRef<FieldInfo> VBasesRef(VBaseArray, VirtualBaseClasses.size());

    return new (Allocator.Allocate<RecordType>())
        RecordType(FieldsRef, BasesRef, VBasesRef, Size, Align, Pack, RecFlags);
  }

  /// Create and return a union type.
  ///
  /// All field offsets are forced to zero. The \c IsUnion flag is set on the
  /// resulting record.
  ///
  /// \param Fields   The members of the union.
  /// \param Size     The size of the union.
  /// \param Align    The ABI alignment of the union.
  /// \param Pack     How members are packed in memory.
  /// \param RecFlags Additional bitmask flags for the union.
  /// \return The newly allocated union record type.
  const RecordType *getUnionType(ArrayRef<FieldInfo> Fields, TypeSize Size,
                                 Align Align,
                                 StructPacking Pack = StructPacking::Default,
                                 RecordFlags RecFlags = RecordFlags::None) {
    FieldInfo *FieldArray = Allocator.Allocate<FieldInfo>(Fields.size());

    for (size_t I = 0, E = Fields.size(); I != E; ++I) {
      const FieldInfo &Field = Fields[I];
      new (&FieldArray[I])
          FieldInfo(Field.FieldType, 0, Field.IsBitField, Field.BitFieldWidth,
                    Field.IsUnnamedBitfield);
    }

    ArrayRef<FieldInfo> FieldsRef(FieldArray, Fields.size());

    return new (Allocator.Allocate<RecordType>())
        RecordType(FieldsRef, ArrayRef<FieldInfo>(), ArrayRef<FieldInfo>(),
                   Size, Align, Pack, RecFlags | RecordFlags::IsUnion);
  }

  /// Create and return a complex type with real and imaginary parts.
  ///
  /// \param ElementType The type of each component.
  /// \param Align       The ABI alignment of the complex type.
  /// \return The newly allocated complex type.
  const ComplexType *getComplexType(const Type *ElementType, Align Align) {
    // Complex types have two elements (real and imaginary parts)
    uint64_t ElementSizeInBits = ElementType->getSizeInBits().getFixedValue();
    uint64_t ComplexSizeInBits = ElementSizeInBits * 2;

    return new (Allocator.Allocate<ComplexType>())
        ComplexType(ElementType, ComplexSizeInBits, Align);
  }

  /// Create and return a member-pointer type.
  ///
  /// \param IsFunctionPointer True if this points to a member function.
  /// \param SizeInBits        The size of the member pointer in bits.
  /// \param Align             The ABI alignment of the member pointer.
  /// \return The newly allocated member-pointer type.
  const MemberPointerType *getMemberPointerType(bool IsFunctionPointer,
                                                uint64_t SizeInBits,
                                                Align Align) {
    return new (Allocator.Allocate<MemberPointerType>())
        MemberPointerType(IsFunctionPointer, SizeInBits, Align);
  }
};

} // namespace abi
} // namespace llvm

#endif
