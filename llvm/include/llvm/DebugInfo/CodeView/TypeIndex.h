//===- TypeIndex.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_TYPEINDEX_H
#define LLVM_DEBUGINFO_CODEVIEW_TYPEINDEX_H

#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"
#include <cassert>
#include <cinttypes>

namespace llvm {

class ScopedPrinter;
class StringRef;

namespace codeview {

class TypeCollection;

/// Built-in CodeView simple type kinds encoded in the low byte of a simple
/// TypeIndex.
enum class SimpleTypeKind : uint32_t {
  None = 0x0000,          ///< Uncharacterized type (no type).
  Void = 0x0003,          ///< Void type.
  NotTranslated = 0x0007, ///< Type not translated by cvpack.
  HResult = 0x0008,       ///< OLE/COM HRESULT.

  SignedCharacter = 0x0010,   ///< 8-bit signed character.
  UnsignedCharacter = 0x0020, ///< 8-bit unsigned character.
  NarrowCharacter = 0x0070,   ///< Narrow character (\c char).
  WideCharacter = 0x0071,     ///< Wide character.
  Character16 = 0x007a,       ///< \c char16_t.
  Character32 = 0x007b,       ///< \c char32_t.
  Character8 = 0x007c,        ///< \c char8_t.

  SByte = 0x0068,       ///< 8-bit signed integer.
  Byte = 0x0069,        ///< 8-bit unsigned integer.
  Int16Short = 0x0011,  ///< 16-bit signed short.
  UInt16Short = 0x0021, ///< 16-bit unsigned short.
  Int16 = 0x0072,       ///< 16-bit signed integer.
  UInt16 = 0x0073,      ///< 16-bit unsigned integer.
  Int32Long = 0x0012,   ///< 32-bit signed long.
  UInt32Long = 0x0022,  ///< 32-bit unsigned long.
  Int32 = 0x0074,       ///< 32-bit signed integer.
  UInt32 = 0x0075,      ///< 32-bit unsigned integer.
  Int64Quad = 0x0013,   ///< 64-bit signed quad.
  UInt64Quad = 0x0023,  ///< 64-bit unsigned quad.
  Int64 = 0x0076,       ///< 64-bit signed integer.
  UInt64 = 0x0077,      ///< 64-bit unsigned integer.
  Int128Oct = 0x0014,   ///< 128-bit signed integer.
  UInt128Oct = 0x0024,  ///< 128-bit unsigned integer.
  Int128 = 0x0078,      ///< 128-bit signed integer.
  UInt128 = 0x0079,     ///< 128-bit unsigned integer.

  Float16 = 0x0046,                 ///< 16-bit floating-point.
  Float32 = 0x0040,                 ///< 32-bit floating-point.
  Float32PartialPrecision = 0x0045, ///< 32-bit partial-precision floating-point.
  Float48 = 0x0044,                 ///< 48-bit floating-point.
  Float64 = 0x0041,                 ///< 64-bit floating-point.
  Float80 = 0x0042,                 ///< 80-bit floating-point.
  Float128 = 0x0043,                ///< 128-bit floating-point.

  Complex16 = 0x0056,                 ///< 16-bit complex.
  Complex32 = 0x0050,                 ///< 32-bit complex.
  Complex32PartialPrecision = 0x0055, ///< 32-bit partial-precision complex.
  Complex48 = 0x0054,                 ///< 48-bit complex.
  Complex64 = 0x0051,                 ///< 64-bit complex.
  Complex80 = 0x0052,                 ///< 80-bit complex.
  Complex128 = 0x0053,                ///< 128-bit complex.

  Boolean8 = 0x0030,   ///< 8-bit boolean.
  Boolean16 = 0x0031,  ///< 16-bit boolean.
  Boolean32 = 0x0032,  ///< 32-bit boolean.
  Boolean64 = 0x0033,  ///< 64-bit boolean.
  Boolean128 = 0x0034, ///< 128-bit boolean.
};

/// Pointer/indirection mode encoded with a simple TypeIndex kind.
enum class SimpleTypeMode : uint32_t {
  Direct = 0x00000000,        ///< Not a pointer.
  NearPointer = 0x00000100,   ///< Near pointer.
  FarPointer = 0x00000200,    ///< Far pointer.
  HugePointer = 0x00000300,   ///< Huge pointer.
  NearPointer32 = 0x00000400, ///< 32-bit near pointer.
  FarPointer32 = 0x00000500,  ///< 32-bit far pointer.
  NearPointer64 = 0x00000600, ///< 64-bit near pointer.
  NearPointer128 = 0x00000700 ///< 128-bit near pointer.
};

/// A 32-bit CodeView type reference.
///
/// Types are indexed by their order of appearance in .debug$T plus 0x1000.
/// Type indices less than 0x1000 are "simple" types, composed of a
/// SimpleTypeMode byte followed by a SimpleTypeKind byte.
class TypeIndex {
public:
  /// First TypeIndex value reserved for complex (non-simple) types.
  static const uint32_t FirstNonSimpleIndex = 0x1000;
  /// Bit mask selecting the SimpleTypeKind portion of a simple TypeIndex.
  static const uint32_t SimpleKindMask = 0x000000ff;
  /// Bit mask selecting the SimpleTypeMode portion of a simple TypeIndex.
  static const uint32_t SimpleModeMask = 0x00000700;
  /// High bit set on TypeIndex values that refer to item IDs rather than types.
  static const uint32_t DecoratedItemIdMask = 0x80000000;

public:
  /// Construct a TypeIndex for the none/null simple type.
  TypeIndex() : Index(static_cast<uint32_t>(SimpleTypeKind::None)) {}
  /// Construct a TypeIndex from a raw 32-bit index value.
  ///
  /// \param Index Raw TypeIndex encoding.
  explicit TypeIndex(uint32_t Index) : Index(Index) {}
  /// Construct a direct simple TypeIndex for \p Kind.
  ///
  /// \param Kind Simple type kind.
  explicit TypeIndex(SimpleTypeKind Kind)
      : Index(static_cast<uint32_t>(Kind)) {}
  /// Construct a simple TypeIndex for \p Kind with pointer mode \p Mode.
  ///
  /// \param Kind Simple type kind.
  /// \param Mode Simple type pointer/indirection mode.
  TypeIndex(SimpleTypeKind Kind, SimpleTypeMode Mode)
      : Index(static_cast<uint32_t>(Kind) | static_cast<uint32_t>(Mode)) {}

  /// Return the raw 32-bit TypeIndex value.
  ///
  /// \returns The raw 32-bit TypeIndex encoding.
  uint32_t getIndex() const { return Index; }
  /// Set the raw 32-bit TypeIndex value.
  ///
  /// \param I New raw index.
  void setIndex(uint32_t I) { Index = I; }
  /// Return true if this is a simple type (index less than 0x1000).
  ///
  /// \returns True if the index is less than \c FirstNonSimpleIndex.
  bool isSimple() const { return Index < FirstNonSimpleIndex; }
  /// Return true if the decorated item-ID bit is set.
  ///
  /// \returns True if the decorated item-ID bit is set.
  bool isDecoratedItemId() const { return !!(Index & DecoratedItemIdMask); }

  /// Return true if this is the none/null simple type.
  ///
  /// \returns True if this equals the none/null simple type.
  bool isNoneType() const { return *this == None(); }

  /// Return the zero-based array index of this complex TypeIndex.
  ///
  /// \returns The zero-based index into the complex type record stream.
  uint32_t toArrayIndex() const {
    assert(!isSimple());
    return (getIndex() & ~DecoratedItemIdMask) - FirstNonSimpleIndex;
  }

  /// Build a TypeIndex from a zero-based complex type array index.
  ///
  /// \param Index Zero-based index into the type record stream.
  /// \returns A TypeIndex for the complex type at \p Index.
  static TypeIndex fromArrayIndex(uint32_t Index) {
    return TypeIndex(Index + FirstNonSimpleIndex);
  }

  /// Build a TypeIndex from a zero-based array index, optionally as an item ID.
  ///
  /// \param IsItem If true, set the decorated item-ID bit.
  /// \param Index Zero-based index into the type or item stream.
  /// \returns A TypeIndex for the record at \p Index, optionally decorated.
  static TypeIndex fromDecoratedArrayIndex(bool IsItem, uint32_t Index) {
    return TypeIndex((Index + FirstNonSimpleIndex) |
                     (IsItem ? DecoratedItemIdMask : 0));
  }

  /// Return a copy of this TypeIndex with the item-ID decoration cleared.
  ///
  /// \returns A TypeIndex equal to this one without the item-ID bit.
  TypeIndex removeDecoration() {
    return TypeIndex(Index & ~DecoratedItemIdMask);
  }

  /// Return the SimpleTypeKind portion of a simple TypeIndex.
  ///
  /// \returns The SimpleTypeKind encoded in this simple TypeIndex.
  SimpleTypeKind getSimpleKind() const {
    assert(isSimple());
    return static_cast<SimpleTypeKind>(Index & SimpleKindMask);
  }

  /// Return the SimpleTypeMode portion of a simple TypeIndex.
  ///
  /// \returns The SimpleTypeMode encoded in this simple TypeIndex.
  SimpleTypeMode getSimpleMode() const {
    assert(isSimple());
    return static_cast<SimpleTypeMode>(Index & SimpleModeMask);
  }

  /// Return this simple TypeIndex with pointer mode cleared (direct).
  ///
  /// \returns A direct TypeIndex with the same simple kind.
  TypeIndex makeDirect() const { return TypeIndex{getSimpleKind()}; }

  /// Return a TypeIndex for the none/null simple type.
  ///
  /// \returns A TypeIndex for the none/null simple type.
  static TypeIndex None() { return TypeIndex(SimpleTypeKind::None); }
  /// Return a TypeIndex for the void simple type.
  ///
  /// \returns A TypeIndex for the void simple type.
  static TypeIndex Void() { return TypeIndex(SimpleTypeKind::Void); }
  /// Return a TypeIndex for a 32-bit near pointer to void.
  ///
  /// \returns A TypeIndex for a 32-bit near pointer to void.
  static TypeIndex VoidPointer32() {
    return TypeIndex(SimpleTypeKind::Void, SimpleTypeMode::NearPointer32);
  }
  /// Return a TypeIndex for a 64-bit near pointer to void.
  ///
  /// \returns A TypeIndex for a 64-bit near pointer to void.
  static TypeIndex VoidPointer64() {
    return TypeIndex(SimpleTypeKind::Void, SimpleTypeMode::NearPointer64);
  }

  /// Return a TypeIndex representing \c std::nullptr_t.
  ///
  /// std::nullptr_t uses the pointer mode that doesn't indicate bit-width,
  /// presumably because std::nullptr_t is intended to be compatible with any
  /// pointer type.
  ///
  /// \returns A TypeIndex representing \c std::nullptr_t.
  static TypeIndex NullptrT() {
    return TypeIndex(SimpleTypeKind::Void, SimpleTypeMode::NearPointer);
  }

  /// Return a TypeIndex for an 8-bit signed character.
  ///
  /// \returns A TypeIndex for an 8-bit signed character.
  static TypeIndex SignedCharacter() {
    return TypeIndex(SimpleTypeKind::SignedCharacter);
  }
  /// Return a TypeIndex for an 8-bit unsigned character.
  ///
  /// \returns A TypeIndex for an 8-bit unsigned character.
  static TypeIndex UnsignedCharacter() {
    return TypeIndex(SimpleTypeKind::UnsignedCharacter);
  }
  /// Return a TypeIndex for a narrow character (\c char).
  ///
  /// \returns A TypeIndex for a narrow character (\c char).
  static TypeIndex NarrowCharacter() {
    return TypeIndex(SimpleTypeKind::NarrowCharacter);
  }
  /// Return a TypeIndex for a wide character.
  ///
  /// \returns A TypeIndex for a wide character.
  static TypeIndex WideCharacter() {
    return TypeIndex(SimpleTypeKind::WideCharacter);
  }
  /// Return a TypeIndex for a 16-bit signed short.
  ///
  /// \returns A TypeIndex for a 16-bit signed short.
  static TypeIndex Int16Short() {
    return TypeIndex(SimpleTypeKind::Int16Short);
  }
  /// Return a TypeIndex for a 16-bit unsigned short.
  ///
  /// \returns A TypeIndex for a 16-bit unsigned short.
  static TypeIndex UInt16Short() {
    return TypeIndex(SimpleTypeKind::UInt16Short);
  }
  /// Return a TypeIndex for a 32-bit signed integer.
  ///
  /// \returns A TypeIndex for a 32-bit signed integer.
  static TypeIndex Int32() { return TypeIndex(SimpleTypeKind::Int32); }
  /// Return a TypeIndex for a 32-bit unsigned integer.
  ///
  /// \returns A TypeIndex for a 32-bit unsigned integer.
  static TypeIndex UInt32() { return TypeIndex(SimpleTypeKind::UInt32); }
  /// Return a TypeIndex for a 32-bit signed long.
  ///
  /// \returns A TypeIndex for a 32-bit signed long.
  static TypeIndex Int32Long() { return TypeIndex(SimpleTypeKind::Int32Long); }
  /// Return a TypeIndex for a 32-bit unsigned long.
  ///
  /// \returns A TypeIndex for a 32-bit unsigned long.
  static TypeIndex UInt32Long() {
    return TypeIndex(SimpleTypeKind::UInt32Long);
  }
  /// Return a TypeIndex for a 64-bit signed integer.
  ///
  /// \returns A TypeIndex for a 64-bit signed integer.
  static TypeIndex Int64() { return TypeIndex(SimpleTypeKind::Int64); }
  /// Return a TypeIndex for a 64-bit unsigned integer.
  ///
  /// \returns A TypeIndex for a 64-bit unsigned integer.
  static TypeIndex UInt64() { return TypeIndex(SimpleTypeKind::UInt64); }
  /// Return a TypeIndex for a 64-bit signed quad.
  ///
  /// \returns A TypeIndex for a 64-bit signed quad.
  static TypeIndex Int64Quad() { return TypeIndex(SimpleTypeKind::Int64Quad); }
  /// Return a TypeIndex for a 64-bit unsigned quad.
  ///
  /// \returns A TypeIndex for a 64-bit unsigned quad.
  static TypeIndex UInt64Quad() {
    return TypeIndex(SimpleTypeKind::UInt64Quad);
  }

  /// Return a TypeIndex for a 32-bit floating-point type.
  ///
  /// \returns A TypeIndex for a 32-bit floating-point type.
  static TypeIndex Float32() { return TypeIndex(SimpleTypeKind::Float32); }
  /// Return a TypeIndex for a 64-bit floating-point type.
  ///
  /// \returns A TypeIndex for a 64-bit floating-point type.
  static TypeIndex Float64() { return TypeIndex(SimpleTypeKind::Float64); }

  /// Add \p N to this TypeIndex in place.
  ///
  /// \param N Amount to add to the raw index.
  /// \returns A reference to this TypeIndex after adding \p N.
  TypeIndex &operator+=(unsigned N) {
    Index += N;
    return *this;
  }

  /// Pre-increment this TypeIndex by one.
  ///
  /// \returns A reference to this TypeIndex after incrementing.
  TypeIndex &operator++() {
    Index += 1;
    return *this;
  }

  /// Post-increment this TypeIndex by one.
  ///
  /// \param Unused Dummy argument distinguishing the postfix form.
  /// \returns A copy of this TypeIndex before incrementing.
  TypeIndex operator++(int Unused) {
    TypeIndex Copy = *this;
    operator++();
    return Copy;
  }

  /// Subtract \p N from this TypeIndex in place.
  ///
  /// \param N Amount to subtract from the raw index.
  /// \returns A reference to this TypeIndex after subtracting \p N.
  TypeIndex &operator-=(unsigned N) {
    assert(Index >= N);
    Index -= N;
    return *this;
  }

  /// Pre-decrement this TypeIndex by one.
  ///
  /// \returns A reference to this TypeIndex after decrementing.
  TypeIndex &operator--() {
    Index -= 1;
    return *this;
  }

  /// Post-decrement this TypeIndex by one.
  ///
  /// \param Unused Dummy argument distinguishing the postfix form.
  /// \returns A copy of this TypeIndex before decrementing.
  TypeIndex operator--(int Unused) {
    TypeIndex Copy = *this;
    operator--();
    return Copy;
  }

  /// Return true if \p A and \p B have the same raw index.
  ///
  /// \param A Left-hand TypeIndex.
  /// \param B Right-hand TypeIndex.
  /// \returns True if \p A and \p B have the same raw index.
  friend inline bool operator==(const TypeIndex &A, const TypeIndex &B) {
    return A.getIndex() == B.getIndex();
  }

  /// Return true if \p A and \p B have different raw indices.
  ///
  /// \param A Left-hand TypeIndex.
  /// \param B Right-hand TypeIndex.
  /// \returns True if \p A and \p B have different raw indices.
  friend inline bool operator!=(const TypeIndex &A, const TypeIndex &B) {
    return A.getIndex() != B.getIndex();
  }

  /// Return true if \p A has a smaller raw index than \p B.
  ///
  /// \param A Left-hand TypeIndex.
  /// \param B Right-hand TypeIndex.
  /// \returns True if \p A has a smaller raw index than \p B.
  friend inline bool operator<(const TypeIndex &A, const TypeIndex &B) {
    return A.getIndex() < B.getIndex();
  }

  /// Return true if \p A has a raw index less than or equal to \p B.
  ///
  /// \param A Left-hand TypeIndex.
  /// \param B Right-hand TypeIndex.
  /// \returns True if \p A has a raw index less than or equal to \p B.
  friend inline bool operator<=(const TypeIndex &A, const TypeIndex &B) {
    return A.getIndex() <= B.getIndex();
  }

  /// Return true if \p A has a greater raw index than \p B.
  ///
  /// \param A Left-hand TypeIndex.
  /// \param B Right-hand TypeIndex.
  /// \returns True if \p A has a greater raw index than \p B.
  friend inline bool operator>(const TypeIndex &A, const TypeIndex &B) {
    return A.getIndex() > B.getIndex();
  }

  /// Return true if \p A has a raw index greater than or equal to \p B.
  ///
  /// \param A Left-hand TypeIndex.
  /// \param B Right-hand TypeIndex.
  /// \returns True if \p A has a raw index greater than or equal to \p B.
  friend inline bool operator>=(const TypeIndex &A, const TypeIndex &B) {
    return A.getIndex() >= B.getIndex();
  }

  /// Return a TypeIndex equal to \p A plus \p N.
  ///
  /// \param A Base TypeIndex.
  /// \param N Amount to add.
  /// \returns A TypeIndex equal to \p A plus \p N.
  friend inline TypeIndex operator+(const TypeIndex &A, uint32_t N) {
    TypeIndex Result(A);
    Result += N;
    return Result;
  }

  /// Return a TypeIndex equal to \p A minus \p N.
  ///
  /// \param A Base TypeIndex.
  /// \param N Amount to subtract.
  /// \returns A TypeIndex equal to \p A minus \p N.
  friend inline TypeIndex operator-(const TypeIndex &A, uint32_t N) {
    assert(A.getIndex() >= N);
    TypeIndex Result(A);
    Result -= N;
    return Result;
  }

  /// Return the difference between the array indices of \p A and \p B.
  ///
  /// \param A Left-hand TypeIndex.
  /// \param B Right-hand TypeIndex.
  /// \returns The difference between the array indices of \p A and \p B.
  friend inline uint32_t operator-(const TypeIndex &A, const TypeIndex &B) {
    assert(A >= B);
    return A.toArrayIndex() - B.toArrayIndex();
  }

  /// Return a printable name for the simple type \p TI.
  ///
  /// \param TI Simple TypeIndex whose name to return.
  /// \returns A printable name for the simple type \p TI.
  LLVM_ABI static StringRef simpleTypeName(TypeIndex TI);

private:
  support::ulittle32_t Index;
};

/// Maps a TypeIndex to a byte offset for log(N) lookup in a type stream.
///
/// Used for pseudo-indexing an array of type records. An array of such records
/// sorted by TypeIndex can allow log(N) lookups even though such a type record
/// stream does not provide random access.
struct TypeIndexOffset {
  /// TypeIndex of the record at \c Offset.
  TypeIndex Type;
  /// Byte offset of the corresponding type record.
  support::ulittle32_t Offset;
};

/// Print TypeIndex \p TI named \p FieldName to \p Printer using \p Types.
///
/// \param Printer Destination scoped printer.
/// \param FieldName Field label to print.
/// \param TI TypeIndex to print.
/// \param Types Type collection used to resolve names.
LLVM_ABI void printTypeIndex(ScopedPrinter &Printer, StringRef FieldName,
                             TypeIndex TI, TypeCollection &Types);
}

/// DenseMapInfo specialization for CodeView TypeIndex keys.
template <> struct DenseMapInfo<codeview::TypeIndex> {
  /// Return a hash of the raw TypeIndex value in \p TI.
  ///
  /// \param TI TypeIndex to hash.
  /// \returns A hash of the raw TypeIndex value in \p TI.
  static unsigned getHashValue(const codeview::TypeIndex &TI) {
    return DenseMapInfo<uint32_t>::getHashValue(TI.getIndex());
  }
  /// Return true if \p LHS and \p RHS compare equal.
  ///
  /// \param LHS Left-hand TypeIndex.
  /// \param RHS Right-hand TypeIndex.
  /// \returns True if \p LHS and \p RHS compare equal.
  static bool isEqual(const codeview::TypeIndex &LHS,
                      const codeview::TypeIndex &RHS) {
    return LHS == RHS;
  }
};

} // namespace llvm

#endif
