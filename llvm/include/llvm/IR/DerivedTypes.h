//===- llvm/DerivedTypes.h - Classes for handling data types ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations of classes that represent "derived
// types".  These are things like "arrays of x" or "structure of x, y, z" or
// "function returning x taking (y,z) as parameters", etc...
//
// The implementations of these classes live in the Type.cpp file.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_DERIVEDTYPES_H
#define LLVM_IR_DERIVEDTYPES_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/TypeSize.h"
#include <cassert>
#include <cstdint>

namespace llvm {

class Value;
class APInt;
class LLVMContext;
template <typename T> class Expected;
class Error;

/// Class to represent integer types.
///
/// Note that this class is also used to represent the built-in integer types:
/// Int1Ty, Int8Ty, Int16Ty, Int32Ty and Int64Ty.
class IntegerType : public Type {
  friend class LLVMContextImpl;

protected:
  /// Construct an integer type with bit width \p NumBits in context \p C.
  /// \param C LLVM context in which to unique the type.
  /// \param NumBits Bit width of the integer type.
  explicit IntegerType(LLVMContext &C, unsigned NumBits) : Type(C, IntegerTyID){
    setSubclassData(NumBits);
  }

public:
  /// This enum is just used to hold constants we need for IntegerType.
  enum {
    MIN_INT_BITS = 1,        ///< Minimum number of bits that can be specified
    MAX_INT_BITS = (1<<23)   ///< Maximum number of bits that can be specified
      ///< Note that bit width is stored in the Type classes SubclassData field
      ///< which has 24 bits. SelectionDAG type legalization can require a
      ///< power of 2 IntegerType, so limit to the largest representable power
      ///< of 2, 8388608.
  };

  /// Get or create an IntegerType instance.
  ///
  /// This static method is the primary way of constructing an IntegerType.
  /// If an IntegerType with the same NumBits value was previously instantiated,
  /// that instance will be returned. Otherwise a new one will be created. Only
  /// one instance with a given NumBits value is ever created.
  /// \param C LLVM context in which to unique the type.
  /// \param NumBits Bit width of the integer type.
  /// \return The uniqued IntegerType with the given bit width.
  LLVM_ABI static IntegerType *get(LLVMContext &C, unsigned NumBits);

  /// Returns type twice as wide the input type.
  /// \return An IntegerType with twice this type's bit width.
  IntegerType *getExtendedType() const {
    return Type::getIntNTy(getContext(), 2 * getBitWidth());
  }

  /// Returns type half as wide the input type.
  /// \return An IntegerType with half this type's bit width.
  IntegerType *getTruncatedType() const {
    unsigned BitWidth = getBitWidth();
    assert((BitWidth & 1) == 0 &&
           "Cannot truncate integer type with odd bit-width");
    return Type::getIntNTy(getContext(), BitWidth / 2);
  }

  /// Get the number of bits in this IntegerType.
  /// \return The bit width of this integer type.
  unsigned getBitWidth() const { return getSubclassData(); }

  /// Return a bitmask with ones set for all of the bits that can be set by an
  /// unsigned version of this type. This is 0xFF for i8, 0xFFFF for i16, etc.
  /// \return A bitmask with ones for every bit of this unsigned integer type.
  uint64_t getBitMask() const {
    return ~uint64_t(0UL) >> (64-getBitWidth());
  }

  /// Return a uint64_t with just the most significant bit set (the sign bit, if
  /// the value is treated as a signed number).
  /// \return A value with only this type's sign bit set.
  uint64_t getSignBit() const {
    return 1ULL << (getBitWidth()-1);
  }

  /// For example, this is 0xFF for an 8 bit integer, 0xFFFF for i16, etc.
  /// @returns a bit mask with ones set for all the bits of this type.
  /// Get a bit mask for this type.
  LLVM_ABI APInt getMask() const;

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param T Type to test.
  /// \return true if \p T is an IntegerType.
  static bool classof(const Type *T) {
    return T->getTypeID() == IntegerTyID;
  }
};

/// Return the bit width of this integer type.
/// \return The bit width of this IntegerType.
unsigned Type::getIntegerBitWidth() const {
  return cast<IntegerType>(this)->getBitWidth();
}

/// Return true if this is an integer type of bit width \p BitWidth.
/// \param BitWidth Required integer bit width.
/// \return true if this is an IntegerType of the given width.
bool Type::isIntegerTy(unsigned BitWidth) const {
  return isIntegerTy() && getIntegerBitWidth() == BitWidth;
}

/// Return true if this is an integer or integer-vector of width \p BitWidth.
/// \param BitWidth Required integer (element) bit width.
/// \return true if the scalar type is an IntegerType of the given width.
bool Type::isIntOrIntVectorTy(unsigned BitWidth) const {
  return getScalarType()->isIntegerTy(BitWidth);
}

/// Class to represent byte types.
class ByteType : public Type {
  friend class LLVMContextImpl;

protected:
  /// Construct a byte type with bit width \p NumBits in context \p C.
  /// \param C LLVM context in which to unique the type.
  /// \param NumBits Bit width of the byte type.
  explicit ByteType(LLVMContext &C, unsigned NumBits) : Type(C, ByteTyID) {
    setSubclassData(NumBits);
  }

public:
  /// This enum is just used to hold constants we need for ByteType.
  enum {
    MIN_BYTE_BITS = 1, ///< Minimum number of bits that can be specified
    /// Maximum number of bits that can be specified.
    ///
    /// Bit width is stored in the Type classes SubclassData field which has 24
    /// bits. SelectionDAG type legalization can require a power of 2 ByteType,
    /// so limit to the largest representable power of 2, 8388608.
    MAX_BYTE_BITS = (1 << 23)
  };

  /// Get or create a ByteType instance.
  ///
  /// This static method is the primary way of constructing a ByteType.
  /// If a ByteType with the same NumBits value was previously instantiated,
  /// that instance will be returned. Otherwise a new one will be created. Only
  /// one instance with a given NumBits value is ever created.
  /// \param C LLVM context in which to unique the type.
  /// \param NumBits Bit width of the byte type.
  /// \return The uniqued ByteType with the given bit width.
  LLVM_ABI static ByteType *get(LLVMContext &C, unsigned NumBits);

  /// Get the number of bits in this ByteType.
  /// \return The bit width of this byte type.
  unsigned getBitWidth() const { return getSubclassData(); }

  /// For example, this is 0xFF for an 8 bit byte, 0xFFFF for b16, etc.
  /// @returns a bit mask with ones set for all the bits of this type.
  /// Get a bit mask for this type.
  LLVM_ABI APInt getMask() const;

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param T Type to test.
  /// \return true if \p T is a ByteType.
  static bool classof(const Type *T) { return T->getTypeID() == ByteTyID; }
};

unsigned Type::getByteBitWidth() const {
  return cast<ByteType>(this)->getBitWidth();
}

/// Class to represent function types.
class FunctionType : public Type {
  FunctionType(Type *Result, ArrayRef<Type*> Params, bool IsVarArgs);

public:
  /// Copy construction is deleted; function types are uniqued and immutable.
  /// \param Unused Unused copy source (deleted).
  FunctionType(const FunctionType &Unused) = delete;
  /// Copy assignment is deleted; function types are uniqued and immutable.
  /// \param Unused Unused copy source (deleted).
  FunctionType &operator=(const FunctionType &Unused) = delete;

  /// This static method is the primary way of constructing a FunctionType.
  /// \param Result Return type of the function.
  /// \param Params Formal parameter types.
  /// \param isVarArg Whether the function is variadic.
  /// \return The uniqued FunctionType for the given signature.
  LLVM_ABI static FunctionType *get(Type *Result, ArrayRef<Type *> Params,
                                    bool isVarArg);

  /// Create a FunctionType taking no parameters.
  /// \param Result Return type of the function.
  /// \param isVarArg Whether the function is variadic.
  /// \return The uniqued FunctionType with no fixed parameters.
  LLVM_ABI static FunctionType *get(Type *Result, bool isVarArg);

  /// Return true if the specified type is valid as a return type.
  /// \param RetTy Candidate return type.
  /// \return true if \p RetTy may be used as a function return type.
  LLVM_ABI static bool isValidReturnType(Type *RetTy);

  /// Return true if the specified type is valid as an argument type.
  /// \param ArgTy Candidate argument type.
  /// \return true if \p ArgTy may be used as a function argument type.
  LLVM_ABI static bool isValidArgumentType(Type *ArgTy);

  /// Return true if this function type is variadic.
  /// \return true if this function type accepts a variable argument list.
  bool isVarArg() const { return getSubclassData()!=0; }
  /// Return the return type of this function type.
  /// \return The function's result type.
  Type *getReturnType() const { return ContainedTys[0]; }

  /// Iterator over this function type's formal parameter types.
  using param_iterator = Type::subtype_iterator;

  /// Return an iterator to the first formal parameter type.
  /// \return Iterator to the first formal parameter type.
  param_iterator param_begin() const { return ContainedTys + 1; }
  /// Return an iterator past the last formal parameter type.
  /// \return Iterator past the last formal parameter type.
  param_iterator param_end() const { return &ContainedTys[NumContainedTys]; }
  /// Return an array view of this function type's formal parameter types.
  /// \return The formal parameter types of this function type.
  ArrayRef<Type *> params() const {
    return ArrayRef(param_begin(), param_end());
  }

  /// Return the type of formal parameter \p i.
  /// \param i Zero-based parameter index.
  /// \return The type of the formal parameter at index \p i.
  Type *getParamType(unsigned i) const {
    assert(i < getNumParams() && "getParamType() out of range!");
    return ContainedTys[i + 1];
  }

  /// Return the number of fixed parameters this function type requires.
  /// This does not consider varargs.
  /// \return The number of fixed formal parameters.
  unsigned getNumParams() const { return NumContainedTys - 1; }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param T Type to test.
  /// \return true if \p T is a FunctionType.
  static bool classof(const Type *T) {
    return T->getTypeID() == FunctionTyID;
  }
};
static_assert(alignof(FunctionType) >= alignof(Type *),
              "Alignment sufficient for objects appended to FunctionType");

/// Return true if this function type is variadic.
/// \return true if this FunctionType accepts a variable argument list.
bool Type::isFunctionVarArg() const {
  return cast<FunctionType>(this)->isVarArg();
}

/// Return the type of formal parameter \p i of this function type.
/// \param i Zero-based parameter index.
/// \return The type of the formal parameter at index \p i.
Type *Type::getFunctionParamType(unsigned i) const {
  return cast<FunctionType>(this)->getParamType(i);
}

/// Return the number of fixed parameters of this function type.
/// \return The number of fixed formal parameters.
unsigned Type::getFunctionNumParams() const {
  return cast<FunctionType>(this)->getNumParams();
}

/// Container for a FunctionType and callee Value pair.
///
/// A handy container for a FunctionType+Callee-pointer pair, which can be
/// passed around as a single entity. This assists in replacing the use of
/// PointerType::getElementType() to access the function's type, since that's
/// slated for removal as part of the [opaque pointer types] project.
class FunctionCallee {
public:
  /// Construct from a callable that provides \c getFunctionType().
  ///
  /// Allows implicit conversion from types which have a getFunctionType member
  /// (e.g. Function and InlineAsm).
  /// \param Fn Callable whose function type and value are captured.
  template <typename T, typename U = decltype(&T::getFunctionType)>
  FunctionCallee(T *Fn)
      : FnTy(Fn ? Fn->getFunctionType() : nullptr), Callee(Fn) {}

  /// Construct from an explicit function type and callee value.
  /// \param FnTy Function type of the callee.
  /// \param Callee Callee value (function, inline asm, etc.).
  FunctionCallee(FunctionType *FnTy, Value *Callee)
      : FnTy(FnTy), Callee(Callee) {
    assert((FnTy == nullptr) == (Callee == nullptr));
  }

  /// Construct an empty callee from nullptr.
  /// \param Unused Unused nullptr literal used to select this overload.
  FunctionCallee(std::nullptr_t Unused) {}

  /// Default-construct an empty callee with no type or target value.
  FunctionCallee() = default;

  /// Return the function type of the callee.
  /// \return The FunctionType of the callee, or nullptr if empty.
  FunctionType *getFunctionType() { return FnTy; }

  /// Return the callee value.
  /// \return The callee Value, or nullptr if empty.
  Value *getCallee() { return Callee; }

  /// Return true if this holds a non-null callee.
  /// \return true if this holds a non-null callee value.
  explicit operator bool() { return Callee; }

private:
  FunctionType *FnTy = nullptr;
  Value *Callee = nullptr;
};

/// Class to represent struct types. There are two different kinds of struct
/// types: Literal structs and Identified structs.
///
/// Literal struct types (e.g. { i32, i32 }) are uniqued structurally, and must
/// always have a body when created.  You can get one of these by using one of
/// the StructType::get() forms.
///
/// Identified structs (e.g. %foo or %42) may optionally have a name and are not
/// uniqued.  The names for identified structs are managed at the LLVMContext
/// level, so there can only be a single identified struct with a given name in
/// a particular LLVMContext.  Identified structs may also optionally be opaque
/// (have no body specified).  You get one of these by using one of the
/// StructType::create() forms.
///
/// Independent of what kind of struct you have, the body of a struct type are
/// laid out in memory consecutively with the elements directly one after the
/// other (if the struct is packed) or (if not packed) with padding between the
/// elements as defined by DataLayout (which is required to match what the code
/// generator for a target expects).
///
class StructType : public Type {
  StructType(LLVMContext &C) : Type(C, StructTyID) {}

  enum {
    /// This is the contents of the SubClassData field.
    SCDB_HasBody = 1,
    SCDB_Packed = 2,
    SCDB_IsLiteral = 4,
    SCDB_IsSized = 8,
    SCDB_ContainsScalableVector = 16,
    SCDB_NotContainsScalableVector = 32,
    SCDB_ContainsNonGlobalTargetExtType = 64,
    SCDB_NotContainsNonGlobalTargetExtType = 128,
    SCDB_ContainsNonLocalTargetExtType = 64,
    SCDB_NotContainsNonLocalTargetExtType = 128,
  };

  /// For a named struct that actually has a name, this is a pointer to the
  /// symbol table entry (maintained by LLVMContext) for the struct.
  /// This is null if the type is an literal struct or if it is a identified
  /// type that has an empty name.
  void *SymbolTableEntry = nullptr;

public:
  /// Copy construction is deleted; identified structs are not copyable this way.
  /// \param Unused Unused copy source (deleted).
  StructType(const StructType &Unused) = delete;
  /// Copy assignment is deleted; identified structs are not assignable this way.
  /// \param Unused Unused copy source (deleted).
  StructType &operator=(const StructType &Unused) = delete;

  /// Create a named identified struct with an opaque body in \p Context.
  ///
  /// The body can be filled in later with \ref setBody().
  /// \param Context LLVM context that owns the type.
  /// \param Name Identified struct name.
  /// \return A new identified StructType with the given name.
  LLVM_ABI static StructType *create(LLVMContext &Context, StringRef Name);
  /// Create an anonymous identified struct with an opaque body in \p Context.
  /// \param Context LLVM context that owns the type.
  /// \return A new anonymous identified StructType.
  LLVM_ABI static StructType *create(LLVMContext &Context);

  /// Create a named identified struct with the given element types.
  /// \param Elements Element types of the struct.
  /// \param Name Identified struct name.
  /// \param isPacked Whether the struct should be packed.
  /// \return A new identified StructType with the given body.
  LLVM_ABI static StructType *create(ArrayRef<Type *> Elements, StringRef Name,
                                     bool isPacked = false);
  /// Create an anonymous identified struct with the given element types.
  /// \param Elements Element types of the struct.
  /// \return A new anonymous identified StructType with the given body.
  LLVM_ABI static StructType *create(ArrayRef<Type *> Elements);
  /// Create a named identified struct with the given element types.
  /// \param Context LLVM context that owns the type.
  /// \param Elements Element types of the struct.
  /// \param Name Identified struct name.
  /// \param isPacked Whether the struct should be packed.
  /// \return A new identified StructType with the given body.
  LLVM_ABI static StructType *create(LLVMContext &Context,
                                     ArrayRef<Type *> Elements, StringRef Name,
                                     bool isPacked = false);
  /// Create an anonymous identified struct with the given element types.
  /// \param Context LLVM context that owns the type.
  /// \param Elements Element types of the struct.
  /// \return A new anonymous identified StructType with the given body.
  LLVM_ABI static StructType *create(LLVMContext &Context,
                                     ArrayRef<Type *> Elements);
  /// Create a named identified struct from element type arguments.
  /// \param Name Identified struct name.
  /// \param elt1 First element type.
  /// \param elts Additional element types.
  /// \return A new identified StructType with the given elements.
  template <class... Tys>
  static std::enable_if_t<are_base_of<Type, Tys...>::value, StructType *>
  create(StringRef Name, Type *elt1, Tys *... elts) {
    assert(elt1 && "Cannot create a struct type with no elements with this");
    return create(ArrayRef<Type *>({elt1, elts...}), Name);
  }

  /// This static method is the primary way to create a literal StructType.
  /// \param Context LLVM context in which to unique the type.
  /// \param Elements Element types of the struct.
  /// \param isPacked Whether the struct should be packed.
  /// \return The uniqued literal StructType for the given layout.
  LLVM_ABI static StructType *
  get(LLVMContext &Context, ArrayRef<Type *> Elements, bool isPacked = false);

  /// Create an empty structure type.
  /// \param Context LLVM context in which to unique the type.
  /// \param isPacked Whether the empty struct should be packed.
  /// \return The uniqued empty literal StructType.
  LLVM_ABI static StructType *get(LLVMContext &Context, bool isPacked = false);

  /// Create a non-packed literal struct from element type arguments.
  ///
  /// This convenience overload requires at least one element type and always
  /// returns a non-packed struct.
  /// \param elt1 First element type.
  /// \param elts Additional element types.
  /// \return The uniqued non-packed literal StructType.
  template <class... Tys>
  static std::enable_if_t<are_base_of<Type, Tys...>::value, StructType *>
  get(Type *elt1, Tys *... elts) {
    assert(elt1 && "Cannot create a struct type with no elements with this");
    LLVMContext &Ctx = elt1->getContext();
    return StructType::get(Ctx, ArrayRef<Type *>({elt1, elts...}));
  }

  /// Return the type with the specified name, or null if there is none by that
  /// name.
  /// \param C LLVM context to search.
  /// \param Name Identified struct name to look up.
  /// \return The identified StructType with \p Name, or nullptr if none exists.
  LLVM_ABI static StructType *getTypeByName(LLVMContext &C, StringRef Name);

  /// Return true if this struct is packed (no padding between elements).
  /// \return true if this struct has no padding between elements.
  bool isPacked() const { return (getSubclassData() & SCDB_Packed) != 0; }

  /// Return true if this type is uniqued by structural equivalence, false if it
  /// is a struct definition.
  /// \return true if this is a literal (structurally uniqued) struct.
  bool isLiteral() const { return (getSubclassData() & SCDB_IsLiteral) != 0; }

  /// Return true if this is a type with an identity that has no body specified
  /// yet. These prints as 'opaque' in .ll files.
  /// \return true if this identified struct still has no body.
  bool isOpaque() const { return (getSubclassData() & SCDB_HasBody) == 0; }

  /// Return true if this is a sized type.
  /// \param Visited Optional set of types already visited during the recursive walk.
  /// \return true if this struct has a known size.
  LLVM_ABI bool isSized(SmallPtrSetImpl<Type *> *Visited = nullptr) const;

  /// Return true if this struct contains a scalable vector.
  /// \param Visited Set of types already visited during the recursive walk.
  /// \return true if this struct contains a scalable vector type.
  LLVM_ABI bool isScalableTy(SmallPtrSetImpl<const Type *> &Visited) const;
  /// Inherit \c Type::isScalableTy() overloads that do not take a visited set.
  using Type::isScalableTy;

  /// Return true if this type is or contains a target extension type that
  /// disallows being used as a global.
  /// \param Visited Set of types already visited during the recursive walk.
  /// \return true if a disallowed target-ext type appears in this struct.
  LLVM_ABI bool
  containsNonGlobalTargetExtType(SmallPtrSetImpl<const Type *> &Visited) const;
  /// Inherit \c Type::containsNonGlobalTargetExtType overloads that do not
  /// take a visited set.
  using Type::containsNonGlobalTargetExtType;

  /// Return true if this type is or contains a target extension type that
  /// disallows being used as a local.
  /// \param Visited Set of types already visited during the recursive walk.
  /// \return true if a disallowed target-ext type appears in this struct.
  LLVM_ABI bool
  containsNonLocalTargetExtType(SmallPtrSetImpl<const Type *> &Visited) const;
  /// Bring \c Type::containsNonLocalTargetExtType overloads into this scope.
  using Type::containsNonLocalTargetExtType;

  /// Return true if this struct contains homogeneous scalable vector types.
  ///
  /// Note that the definition of homogeneous scalable vector type is not
  /// recursive here. That means the following structure will return false
  /// when calling this function.
  /// {{<vscale x 2 x i32>, <vscale x 4 x i64>},
  ///  {<vscale x 2 x i32>, <vscale x 4 x i64>}}
  /// \return true if all elements are the same scalable vector type.
  LLVM_ABI bool containsHomogeneousScalableVectorTypes() const;

  /// Return true if this struct is non-empty and all element types are the
  /// same.
  /// \return true if every element type is identical and the struct is non-empty.
  LLVM_ABI bool containsHomogeneousTypes() const;

  /// Return true if this is a named struct that has a non-empty name.
  /// \return true if this identified struct has a non-empty name.
  bool hasName() const { return SymbolTableEntry != nullptr; }

  /// Return the name for this struct type if it has an identity.
  /// This may return an empty string for an unnamed struct type.  Do not call
  /// this on an literal type.
  /// \return The identified struct name, which may be empty.
  LLVM_ABI StringRef getName() const;

  /// Change the name of this type to the specified name, or to a name with a
  /// suffix if there is a collision. Do not call this on an literal type.
  /// \param Name Desired struct name.
  LLVM_ABI void setName(StringRef Name);

  /// Specify a body for an opaque identified type, which must not make the type
  /// recursive.
  /// \param Elements Element types for the body.
  /// \param isPacked Whether the struct should be packed.
  LLVM_ABI void setBody(ArrayRef<Type *> Elements, bool isPacked = false);

  /// Specify a body for an opaque identified type or return an error if it
  /// would make the type recursive.
  /// \param Elements Element types for the body.
  /// \param isPacked Whether the struct should be packed.
  /// \return Success, or an Error if the body would make the type recursive.
  LLVM_ABI Error setBodyOrError(ArrayRef<Type *> Elements,
                                bool isPacked = false);

  /// Return an error if the body for an opaque identified type would make it
  /// recursive.
  /// \param Elements Proposed element types for the body.
  /// \return Success, or an Error if \p Elements would make the type recursive.
  LLVM_ABI Error checkBody(ArrayRef<Type *> Elements);

  /// Return true if the specified type is valid as a element type.
  /// \param ElemTy Candidate struct element type.
  /// \return true if \p ElemTy may be used as a struct element.
  LLVM_ABI static bool isValidElementType(Type *ElemTy);

  /// Iterator over this structure's element types.
  using element_iterator = Type::subtype_iterator;

  /// Return an iterator to the first element type.
  /// \return Iterator to the first struct element type.
  element_iterator element_begin() const { return ContainedTys; }
  /// End iterator over the structure's element types.
  /// \return Iterator past the last struct element type.
  element_iterator element_end() const { return &ContainedTys[NumContainedTys];}
  /// Return an array view of this struct's element types.
  /// \return The element types of this struct.
  ArrayRef<Type *> elements() const {
    return ArrayRef(element_begin(), element_end());
  }

  /// Return true if this is layout identical to the specified struct.
  /// \param Other Struct type to compare layout against.
  /// \return true if both structs have the same layout.
  LLVM_ABI bool isLayoutIdentical(StructType *Other) const;

  /// Return the number of elements in this struct.
  /// \return The number of element types in this struct.
  unsigned getNumElements() const { return NumContainedTys; }
  /// Return the type of element \p N.
  /// \param N Zero-based element index.
  /// \return The type of the element at index \p N.
  Type *getElementType(unsigned N) const {
    assert(N < NumContainedTys && "Element number out of range!");
    return ContainedTys[N];
  }
  /// Given an index value into the type, return the type of the element.
  /// \param V Constant integer index into the struct.
  /// \return The type of the element at index \p V.
  LLVM_ABI Type *getTypeAtIndex(const Value *V) const;
  /// Return the type of element \p N.
  /// \param N Zero-based element index.
  /// \return The type of the element at index \p N.
  Type *getTypeAtIndex(unsigned N) const { return getElementType(N); }
  /// Return true if constant integer \p V is a valid element index.
  /// \param V Constant integer index into the struct.
  /// \return true if \p V is a valid element index for this struct.
  LLVM_ABI bool indexValid(const Value *V) const;
  /// Return true if \p Idx is in range for this struct's element list.
  /// \param Idx Zero-based element index.
  /// \return true if \p Idx is a valid element index for this struct.
  bool indexValid(unsigned Idx) const { return Idx < getNumElements(); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param T Type to test.
  /// \return true if \p T is a StructType.
  static bool classof(const Type *T) {
    return T->getTypeID() == StructTyID;
  }
};

/// Return the name of this identified struct type.
/// \return The name of this StructType.
StringRef Type::getStructName() const {
  return cast<StructType>(this)->getName();
}

/// Return the number of elements in this struct type.
/// \return The number of elements in this StructType.
unsigned Type::getStructNumElements() const {
  return cast<StructType>(this)->getNumElements();
}

/// Return the type of element \p N in this struct type.
/// \param N Zero-based element index.
/// \return The type of the element at index \p N.
Type *Type::getStructElementType(unsigned N) const {
  return cast<StructType>(this)->getElementType(N);
}

/// Class to represent array types.
class ArrayType : public Type {
  /// The element type of the array.
  Type *ContainedType;
  /// Number of elements in the array.
  uint64_t NumElements;

  ArrayType(Type *ElType, uint64_t NumEl);

public:
  /// Copy construction is deleted; array types are uniqued.
  /// \param Unused Unused copy source (deleted).
  ArrayType(const ArrayType &Unused) = delete;
  /// Copy assignment is deleted; array types are uniqued.
  /// \param Unused Unused copy source (deleted).
  ArrayType &operator=(const ArrayType &Unused) = delete;

  /// Return the number of elements in this array.
  /// \return The number of elements in this array.
  uint64_t getNumElements() const { return NumElements; }
  /// Return the element type of this array.
  /// \return The element type of this array.
  Type *getElementType() const { return ContainedType; }

  /// This static method is the primary way to construct an ArrayType.
  /// \param ElementType Element type of the array.
  /// \param NumElements Number of elements in the array.
  /// \return The uniqued ArrayType for the given element type and length.
  LLVM_ABI static ArrayType *get(Type *ElementType, uint64_t NumElements);

  /// Return true if the specified type is valid as a element type.
  /// \param ElemTy Candidate array element type.
  /// \return true if \p ElemTy may be used as an array element.
  LLVM_ABI static bool isValidElementType(Type *ElemTy);

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param T Type to test.
  /// \return true if \p T is an ArrayType.
  static bool classof(const Type *T) {
    return T->getTypeID() == ArrayTyID;
  }
};

/// Return the number of elements in this array type.
/// \return The number of elements in this ArrayType.
uint64_t Type::getArrayNumElements() const {
  return cast<ArrayType>(this)->getNumElements();
}

/// Base class of all SIMD vector types
class VectorType : public Type {
  /// A fully specified VectorType is of the form <vscale x n x Ty>. 'n' is the
  /// minimum number of elements of type Ty contained within the vector, and
  /// 'vscale x' indicates that the total element count is an integer multiple
  /// of 'n', where the multiple is either guaranteed to be one, or is
  /// statically unknown at compile time.
  ///
  /// If the multiple is known to be 1, then the extra term is discarded in
  /// textual IR:
  ///
  /// <4 x i32>          - a vector containing 4 i32s
  /// <vscale x 4 x i32> - a vector containing an unknown integer multiple
  ///                      of 4 i32s

  /// The element type of the vector.
  Type *ContainedType;

protected:
  /// Element quantity of this vector (exact count or vscale minimum).
  ///
  /// The meaning of this value depends on the type of vector:
  /// - For FixedVectorType = <ElementQuantity x ty>, there are
  ///   exactly ElementQuantity elements in this vector.
  /// - For ScalableVectorType = <vscale x ElementQuantity x ty>,
  ///   there are vscale * ElementQuantity elements in this vector, where
  ///   vscale is a runtime-constant integer greater than 0.
  const unsigned ElementQuantity;

  /// Construct a vector type with element type \p ElType and quantity \p EQ.
  /// \param ElType Element type of the vector.
  /// \param EQ Element quantity (exact or minimum, depending on \p TID).
  /// \param TID Fixed or scalable vector type ID.
  /// Construct a vector type with element type \p ElType and quantity \p EQ.
  /// \param ElType Element type of the vector.
  /// \param EQ Element quantity (exact or minimum, depending on \p TID).
  /// \param TID Fixed or scalable vector type ID.
  LLVM_ABI VectorType(Type *ElType, unsigned EQ, Type::TypeID TID);

public:
  /// Copy construction is deleted; vector types are uniqued.
  /// \param Unused Unused copy source (deleted).
  VectorType(const VectorType &Unused) = delete;
  /// Copy assignment is deleted; vector types are uniqued.
  /// \param Unused Unused copy source (deleted).
  VectorType &operator=(const VectorType &Unused) = delete;

  /// Return the element type of this vector.
  /// \return The element type of this vector.
  Type *getElementType() const { return ContainedType; }

  /// This static method is the primary way to construct a VectorType.
  /// \param ElementType Element type of the vector.
  /// \param EC Element count (fixed or scalable).
  /// \return The uniqued VectorType for the given element type and count.
  LLVM_ABI static VectorType *get(Type *ElementType, ElementCount EC);

  /// Construct a VectorType from an element type, count, and scalability flag.
  /// \param ElementType Element type of the vector.
  /// \param NumElements Minimum (or exact) number of elements.
  /// \param Scalable Whether the vector is scalable.
  /// \return The uniqued VectorType for the given shape.
  static VectorType *get(Type *ElementType, unsigned NumElements,
                         bool Scalable) {
    return VectorType::get(ElementType,
                           ElementCount::get(NumElements, Scalable));
  }

  /// Construct a VectorType with \p ElementType and the same count as \p Other.
  /// \param ElementType Element type of the vector.
  /// \param Other Vector whose element count is reused.
  /// \return The uniqued VectorType matching \p Other's element count.
  static VectorType *get(Type *ElementType, const VectorType *Other) {
    return VectorType::get(ElementType, Other->getElementCount());
  }

  /// Return a VectorType of integer elements matching \p VTy's shape and width.
  ///
  /// The result has the same number of elements as \p VTy, and each element is
  /// an integer type of the same width as the input element type.
  /// \param VTy Input vector type.
  /// \return A vector of integers with the same shape and element width as \p VTy.
  static VectorType *getInteger(VectorType *VTy) {
    unsigned EltBits =
        VTy->getElementType()->getPrimitiveSizeInBits().getFixedValue();
    assert(EltBits && "Element size must be of a non-zero size");
    Type *EltTy = IntegerType::get(VTy->getContext(), EltBits);
    return VectorType::get(EltTy, VTy->getElementCount());
  }

  /// Return a VectorType with the same length and double-width integer elements.
  /// \param VTy Input vector of integer elements.
  /// \return A vector with the same length and double-width integer elements.
  static VectorType *getExtendedElementVectorType(VectorType *VTy) {
    assert(VTy->isIntOrIntVectorTy() && "VTy expected to be a vector of ints.");
    auto *EltTy = cast<IntegerType>(VTy->getElementType());
    return VectorType::get(EltTy->getExtendedType(), VTy->getElementCount());
  }

  /// Return a VectorType with the same length and half-width element type.
  ///
  /// The element type is an integer or floating-point type half as wide as the
  /// elements in \p VTy.
  /// \param VTy Input vector type.
  /// \return A vector with the same length and half-width elements.
  static VectorType *getTruncatedElementVectorType(VectorType *VTy) {
    Type *EltTy = VTy->getElementType();
    if (EltTy->isFloatingPointTy()) {
      switch (EltTy->getTypeID()) {
      case DoubleTyID:
        EltTy = Type::getFloatTy(VTy->getContext());
        break;
      case FloatTyID:
        EltTy = Type::getHalfTy(VTy->getContext());
        break;
      default:
        llvm_unreachable("Cannot create narrower fp vector element type");
      }
    } else {
      EltTy = cast<IntegerType>(EltTy)->getTruncatedType();
    }
    return VectorType::get(EltTy, VTy->getElementCount());
  }

  /// Return a VectorType with more elements of a narrower type than \p VTy.
  ///
  /// For example, a \c <4 x i64> subdivided twice would return \c <16 x i16>.
  /// \param VTy Input vector type.
  /// \param NumSubdivs Number of subdivision steps to apply.
  /// \return A vector with more, narrower elements after \p NumSubdivs steps.
  static VectorType *getSubdividedVectorType(VectorType *VTy, int NumSubdivs) {
    for (int i = 0; i < NumSubdivs; ++i) {
      VTy = VectorType::getDoubleElementsVectorType(VTy);
      VTy = VectorType::getTruncatedElementVectorType(VTy);
    }
    return VTy;
  }

  /// Return a VectorType with half as many elements as \p VTy.
  /// \param VTy Input vector type.
  /// \return A vector with half as many elements as \p VTy.
  static VectorType *getHalfElementsVectorType(VectorType *VTy) {
    auto EltCnt = VTy->getElementCount();
    assert(EltCnt.isKnownEven() &&
           "Cannot halve vector with odd number of elements.");
    return VectorType::get(VTy->getElementType(),
                           EltCnt.divideCoefficientBy(2));
  }

  /// Return a VectorType with one \p Denominator-th as many elements as \p VTy.
  /// \param VTy Input vector type.
  /// \param Denominator Factor by which to divide the element count.
  /// \return A vector with element count divided by \p Denominator.
  static VectorType *getOneNthElementsVectorType(VectorType *VTy,
                                                 unsigned Denominator) {
    auto EltCnt = VTy->getElementCount();
    assert(EltCnt.isKnownMultipleOf(Denominator) &&
           "Cannot take one-nth of a vector");
    return VectorType::get(VTy->getScalarType(),
                           EltCnt.divideCoefficientBy(Denominator));
  }

  /// Return a VectorType with twice as many elements as \p VTy.
  /// \param VTy Input vector type.
  /// \return A vector with twice as many elements as \p VTy.
  static VectorType *getDoubleElementsVectorType(VectorType *VTy) {
    auto EltCnt = VTy->getElementCount();
    assert((EltCnt.getKnownMinValue() * 2ull) <= UINT_MAX &&
           "Too many elements in vector");
    return VectorType::get(VTy->getElementType(), EltCnt * 2);
  }

  /// Construct a vector matching \p SizeTy's bit size with \p EltTy's scalar type.
  ///
  /// Attempts to build a VectorType with the same size-in-bits as \p SizeTy but
  /// with an element type that matches the scalar type of \p EltTy. Returns the
  /// VectorType on success, or nullptr otherwise.
  /// \param SizeTy Vector type providing the desired overall bit size.
  /// \param EltTy Type whose scalar type becomes the new element type.
  /// \return The matching VectorType, or nullptr if no such type exists.
  static VectorType *getWithSizeAndScalar(VectorType *SizeTy, Type *EltTy) {
    if (SizeTy->getScalarType() == EltTy->getScalarType())
      return SizeTy;

    unsigned EltSize = EltTy->getScalarSizeInBits();
    if (!SizeTy->getPrimitiveSizeInBits().isKnownMultipleOf(EltSize))
      return nullptr;

    ElementCount EC = SizeTy->getElementCount()
                          .multiplyCoefficientBy(SizeTy->getScalarSizeInBits())
                          .divideCoefficientBy(EltSize);
    return VectorType::get(EltTy->getScalarType(), EC);
  }

  /// Return true if the specified type is valid as a element type.
  /// \param ElemTy Candidate vector element type.
  /// \return true if \p ElemTy may be used as a vector element.
  LLVM_ABI static bool isValidElementType(Type *ElemTy);

  /// Return an ElementCount instance to represent the (possibly scalable)
  /// number of elements in the vector.
  /// \return The fixed or scalable element count of this vector.
  inline ElementCount getElementCount() const;

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param T Type to test.
  /// \return true if \p T is a fixed or scalable VectorType.
  static bool classof(const Type *T) {
    return T->getTypeID() == FixedVectorTyID ||
           T->getTypeID() == ScalableVectorTyID;
  }
};

/// Class to represent fixed width SIMD vectors
class FixedVectorType : public VectorType {
protected:
  /// Construct a fixed vector with element type \p ElTy and length \p NumElts.
  /// \param ElTy Element type of the vector.
  /// \param NumElts Number of elements in the vector.
  FixedVectorType(Type *ElTy, unsigned NumElts)
      : VectorType(ElTy, NumElts, FixedVectorTyID) {}

public:
  /// Get or create a fixed vector type with the given element type and length.
  /// \param ElementType Element type of the vector.
  /// \param NumElts Number of elements in the vector.
  /// \return The uniqued FixedVectorType for the given shape.
  LLVM_ABI static FixedVectorType *get(Type *ElementType, unsigned NumElts);

  /// Get a fixed vector with \p ElementType and the same length as \p FVTy.
  /// \param ElementType Element type of the vector.
  /// \param FVTy Fixed vector whose element count is reused.
  /// \return The uniqued FixedVectorType matching \p FVTy's length.
  static FixedVectorType *get(Type *ElementType, const FixedVectorType *FVTy) {
    return get(ElementType, FVTy->getNumElements());
  }

  /// Return a fixed vector of integer elements with the same width as \p VTy.
  /// \param VTy Input fixed vector type.
  /// \return A fixed vector of integers matching \p VTy's shape and width.
  static FixedVectorType *getInteger(FixedVectorType *VTy) {
    return cast<FixedVectorType>(VectorType::getInteger(VTy));
  }

  /// Return a fixed vector with the same lane count and element type twice as
  /// wide as \p VTy's elements.
  /// \param VTy Input fixed vector type.
  /// \return A fixed vector with double-width elements.
  static FixedVectorType *getExtendedElementVectorType(FixedVectorType *VTy) {
    return cast<FixedVectorType>(VectorType::getExtendedElementVectorType(VTy));
  }

  /// Return a fixed vector with the same lane count and element type half as
  /// wide as \p VTy's elements.
  /// \param VTy Input fixed vector type.
  /// \return A fixed vector with half-width elements.
  static FixedVectorType *getTruncatedElementVectorType(FixedVectorType *VTy) {
    return cast<FixedVectorType>(
        VectorType::getTruncatedElementVectorType(VTy));
  }

  /// Return a fixed vector with more elements of a narrower type than \p VTy.
  /// \param VTy Input fixed vector type.
  /// \param NumSubdivs Number of subdivision steps to apply.
  /// \return A fixed vector with more, narrower elements after subdivision.
  static FixedVectorType *getSubdividedVectorType(FixedVectorType *VTy,
                                                  int NumSubdivs) {
    return cast<FixedVectorType>(
        VectorType::getSubdividedVectorType(VTy, NumSubdivs));
  }

  /// Return a fixed vector with half as many elements as \p VTy.
  /// \param VTy Input fixed vector type.
  /// \return A fixed vector with half as many elements as \p VTy.
  static FixedVectorType *getHalfElementsVectorType(FixedVectorType *VTy) {
    return cast<FixedVectorType>(VectorType::getHalfElementsVectorType(VTy));
  }

  /// Return a fixed vector with twice as many elements as \p VTy.
  /// \param VTy Input fixed vector type.
  /// \return A fixed vector with twice as many elements as \p VTy.
  static FixedVectorType *getDoubleElementsVectorType(FixedVectorType *VTy) {
    return cast<FixedVectorType>(VectorType::getDoubleElementsVectorType(VTy));
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param T Type to test.
  /// \return true if \p T is a FixedVectorType.
  static bool classof(const Type *T) {
    return T->getTypeID() == FixedVectorTyID;
  }

  /// Return the number of lanes in this fixed-width vector.
  /// \return The exact number of elements in this fixed vector.
  unsigned getNumElements() const { return ElementQuantity; }
};

/// Class to represent scalable SIMD vectors
class ScalableVectorType : public VectorType {
protected:
  /// Construct a scalable vector with element type \p ElTy and min length \p MinNumElts.
  /// \param ElTy Element type of the vector.
  /// \param MinNumElts Minimum number of elements (vscale multiple).
  ScalableVectorType(Type *ElTy, unsigned MinNumElts)
      : VectorType(ElTy, MinNumElts, ScalableVectorTyID) {}

public:
  /// Get or create a scalable vector type with the given element type and min length.
  /// \param ElementType Element type of the vector.
  /// \param MinNumElts Minimum number of elements (vscale multiple).
  /// \return The uniqued ScalableVectorType for the given shape.
  LLVM_ABI static ScalableVectorType *get(Type *ElementType,
                                          unsigned MinNumElts);

  /// Get a scalable vector with \p ElementType and the same min length as \p SVTy.
  /// \param ElementType Element type of the vector.
  /// \param SVTy Scalable vector whose minimum element count is reused.
  /// \return The uniqued ScalableVectorType matching \p SVTy's min length.
  static ScalableVectorType *get(Type *ElementType,
                                 const ScalableVectorType *SVTy) {
    return get(ElementType, SVTy->getMinNumElements());
  }

  /// Return a scalable vector with the same minimum lane count as \p VTy and
  /// integer elements of the same bit width as \p VTy's element type.
  /// \param VTy Input scalable vector type.
  /// \return A scalable vector of integers matching \p VTy's shape and width.
  static ScalableVectorType *getInteger(ScalableVectorType *VTy) {
    return cast<ScalableVectorType>(VectorType::getInteger(VTy));
  }

  /// Return a scalable vector with the same min lane count and double-width elements.
  /// \param VTy Input scalable vector type.
  /// \return A scalable vector with double-width elements.
  static ScalableVectorType *
  getExtendedElementVectorType(ScalableVectorType *VTy) {
    return cast<ScalableVectorType>(
        VectorType::getExtendedElementVectorType(VTy));
  }

  /// Return a scalable vector with the same min lane count and half-width elements.
  /// \param VTy Input scalable vector type.
  /// \return A scalable vector with half-width elements.
  static ScalableVectorType *
  getTruncatedElementVectorType(ScalableVectorType *VTy) {
    return cast<ScalableVectorType>(
        VectorType::getTruncatedElementVectorType(VTy));
  }

  /// Return a scalable vector with more elements of a narrower type than \p VTy.
  ///
  /// For example, subdividing \c <vscale x 4 x i64> twice yields
  /// \c <vscale x 16 x i16>.
  /// \param VTy Input scalable vector type.
  /// \param NumSubdivs Number of subdivision steps to apply.
  /// \return A scalable vector with more, narrower elements after subdivision.
  static ScalableVectorType *getSubdividedVectorType(ScalableVectorType *VTy,
                                                     int NumSubdivs) {
    return cast<ScalableVectorType>(
        VectorType::getSubdividedVectorType(VTy, NumSubdivs));
  }

  /// Return a scalable vector with half as many elements as \p VTy.
  /// \param VTy Input scalable vector type.
  /// \return A scalable vector with half as many elements as \p VTy.
  static ScalableVectorType *
  getHalfElementsVectorType(ScalableVectorType *VTy) {
    return cast<ScalableVectorType>(VectorType::getHalfElementsVectorType(VTy));
  }

  /// Return a scalable vector with twice as many elements as \p VTy.
  /// \param VTy Input scalable vector type.
  /// \return A scalable vector with twice as many elements as \p VTy.
  static ScalableVectorType *
  getDoubleElementsVectorType(ScalableVectorType *VTy) {
    return cast<ScalableVectorType>(
        VectorType::getDoubleElementsVectorType(VTy));
  }

  /// Get the minimum number of elements in this vector. The actual number of
  /// elements in the vector is an integer multiple of this value.
  /// \return The minimum (vscale multiple) element count of this vector.
  unsigned getMinNumElements() const { return ElementQuantity; }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param T Type to test.
  /// \return true if \p T is a ScalableVectorType.
  static bool classof(const Type *T) {
    return T->getTypeID() == ScalableVectorTyID;
  }
};

inline ElementCount VectorType::getElementCount() const {
  return ElementCount::get(ElementQuantity, isa<ScalableVectorType>(this));
}

/// Class to represent pointers.
class PointerType : public Type {
  explicit PointerType(LLVMContext &C, unsigned AddrSpace);

public:
  /// Copy construction is deleted; PointerType is uniquified by address space.
  /// \param Unused Unused copy source (deleted).
  PointerType(const PointerType &Unused) = delete;
  /// Copy assignment is deleted; PointerType is uniquified by address space.
  /// \param Unused Unused copy source (deleted).
  PointerType &operator=(const PointerType &Unused) = delete;

  /// This constructs an opaque pointer to an object in a numbered address
  /// space.
  /// \param C LLVM context in which to unique the type.
  /// \param AddressSpace Address space number for the pointer.
  /// \return The uniqued PointerType for the given address space.
  LLVM_ABI static PointerType *get(LLVMContext &C, unsigned AddressSpace);

  /// This constructs an opaque pointer to an object in the
  /// default address space (address space zero).
  /// \param C LLVM context in which to unique the type.
  /// \return The uniqued PointerType in address space zero.
  static PointerType *getUnqual(LLVMContext &C) {
    return PointerType::get(C, 0);
  }

  /// Return true if the specified type is valid as a element type.
  /// \param ElemTy Candidate pointee type.
  /// \return true if \p ElemTy may be used as a pointee type.
  LLVM_ABI static bool isValidElementType(Type *ElemTy);

  /// Return true if we can load or store from a pointer to this type.
  /// \param ElemTy Candidate pointee type.
  /// \return true if a pointer to \p ElemTy may be loaded or stored.
  LLVM_ABI static bool isLoadableOrStorableType(Type *ElemTy);

  /// Return the address space of the Pointer type.
  /// \return The address space number of this pointer type.
  inline unsigned getAddressSpace() const { return getSubclassData(); }

  /// Implement support type inquiry through isa, cast, and dyn_cast.
  /// \param T Type to test.
  /// \return true if \p T is a PointerType.
  static bool classof(const Type *T) {
    return T->getTypeID() == PointerTyID;
  }
};

Type *Type::getExtendedType() const {
  assert(
      isIntOrIntVectorTy() &&
      "Original type expected to be a vector of integers or a scalar integer.");
  if (auto *VTy = dyn_cast<VectorType>(this))
    return VectorType::getExtendedElementVectorType(
        const_cast<VectorType *>(VTy));
  return cast<IntegerType>(this)->getExtendedType();
}

Type *Type::getTruncatedType() const {
  assert(
      isIntOrIntVectorTy() &&
      "Original type expected to be a vector of integers or a scalar integer.");
  if (auto *VTy = dyn_cast<VectorType>(this))
    return VectorType::getTruncatedElementVectorType(
        const_cast<VectorType *>(VTy));
  return cast<IntegerType>(this)->getTruncatedType();
}

/// Return this type with its scalar/element type replaced by \p EltTy.
/// \param EltTy Replacement scalar or vector element type.
/// \return This type with its scalar or element type replaced by \p EltTy.
Type *Type::getWithNewType(Type *EltTy) const {
  if (auto *VTy = dyn_cast<VectorType>(this))
    return VectorType::get(EltTy, VTy->getElementCount());
  return EltTy;
}

/// Return this type with integer bit width replaced by \p NewBitWidth.
/// \param NewBitWidth Desired integer (or integer-element) bit width.
/// \return This type with integer bit width replaced by \p NewBitWidth.
Type *Type::getWithNewBitWidth(unsigned NewBitWidth) const {
  assert(
      isIntOrIntVectorTy() &&
      "Original type expected to be a vector of integers or a scalar integer.");
  return getWithNewType(getIntNTy(getContext(), NewBitWidth));
}

unsigned Type::getPointerAddressSpace() const {
  return cast<PointerType>(getScalarType())->getAddressSpace();
}

/// Class to represent target extensions types, which are generally
/// unintrospectable from target-independent optimizations.
///
/// Target extension types have a string name, and optionally have type and/or
/// integer parameters. The exact meaning of any parameters is dependent on the
/// target.
class TargetExtType : public Type {
  TargetExtType(LLVMContext &C, StringRef Name, ArrayRef<Type *> Types,
                ArrayRef<unsigned> Ints);

  // These strings are ultimately owned by the context.
  StringRef Name;
  unsigned *IntParams;

public:
  /// Copy construction is deleted; target extension types are uniqued.
  /// \param Unused Unused copy source (deleted).
  TargetExtType(const TargetExtType &Unused) = delete;
  /// Copy assignment is deleted; target extension types are uniqued.
  /// \param Unused Unused copy source (deleted).
  TargetExtType &operator=(const TargetExtType &Unused) = delete;

  /// Return a target extension type having the specified name and optional
  /// type and integer parameters.
  /// \param Context LLVM context in which to unique the type.
  /// \param Name Target-specific type name.
  /// \param Types Optional type parameters.
  /// \param Ints Optional integer parameters.
  /// \return The uniqued TargetExtType for the given name and parameters.
  LLVM_ABI static TargetExtType *get(LLVMContext &Context, StringRef Name,
                                     ArrayRef<Type *> Types = {},
                                     ArrayRef<unsigned> Ints = {});

  /// Return a target extension type having the specified name and optional
  /// type and integer parameters, or an appropriate Error if it fails the
  /// parameters check.
  /// \param Context LLVM context in which to unique the type.
  /// \param Name Target-specific type name.
  /// \param Types Optional type parameters.
  /// \param Ints Optional integer parameters.
  /// \return The TargetExtType on success, or an Error if parameters are invalid.
  LLVM_ABI static Expected<TargetExtType *>
  getOrError(LLVMContext &Context, StringRef Name, ArrayRef<Type *> Types = {},
             ArrayRef<unsigned> Ints = {});

  /// Validate the parameter counts of a newly created target extension type.
  ///
  /// Returns the type itself if the expected number of type and integer
  /// parameters match, or an appropriate Error if not.
  /// \param TTy Target extension type to validate.
  /// \return \p TTy on success, or an Error if its parameters are invalid.
  LLVM_ABI static Expected<TargetExtType *> checkParams(TargetExtType *TTy);

  /// Return the name for this target extension type. Two distinct target
  /// extension types may have the same name if their type or integer parameters
  /// differ.
  /// \return The target-specific name of this extension type.
  StringRef getName() const { return Name; }

  /// Return the type parameters for this particular target extension type. If
  /// there are no parameters, an empty array is returned.
  /// \return The type parameters of this target extension type.
  ArrayRef<Type *> type_params() const {
    return ArrayRef(type_param_begin(), type_param_end());
  }

  /// Iterator over this target extension type's type parameters.
  using type_param_iterator = Type::subtype_iterator;
  /// Return an iterator to the first type parameter.
  /// \return Iterator to the first type parameter.
  type_param_iterator type_param_begin() const { return ContainedTys; }
  /// Return an iterator past the last type parameter.
  /// \return Iterator past the last type parameter.
  type_param_iterator type_param_end() const {
    return &ContainedTys[NumContainedTys];
  }

  /// Return the type parameter at index \p i.
  /// \param i Zero-based index into the type parameter list.
  /// \return The type parameter at index \p i.
  Type *getTypeParameter(unsigned i) const { return getContainedType(i); }
  /// Return the number of type parameters of this target extension type.
  /// \return The number of type parameters.
  unsigned getNumTypeParameters() const { return getNumContainedTypes(); }

  /// Return the integer parameters for this particular target extension type.
  /// If there are no parameters, an empty array is returned.
  /// \return The integer parameters of this target extension type.
  ArrayRef<unsigned> int_params() const {
    return ArrayRef(IntParams, getNumIntParameters());
  }

  /// Return the integer parameter at index \p i.
  /// \param i Zero-based index into the integer parameter list.
  /// \return The integer parameter at index \p i.
  unsigned getIntParameter(unsigned i) const { return IntParams[i]; }
  /// Return the number of integer parameters of this target extension type.
  /// \return The number of integer parameters.
  unsigned getNumIntParameters() const { return getSubclassData(); }

  /// Target-dependent properties describing how a target extension type may be
  /// used in IR.
  enum Property {
    /// zeroinitializer is valid for this target extension type.
    HasZeroInit = 1U << 0,
    /// This type may be used as the value type of a global variable.
    CanBeGlobal = 1U << 1,
    /// This type may be allocated on the stack, either as the allocated type
    /// of an alloca instruction or as a byval function parameter.
    CanBeLocal = 1U << 2,
    /// This type may be used as an element in a vector.
    CanBeVectorElement = 1U << 3,
    /// This type can only be used in intrinsic arguments and return values.
    ///
    /// In particular, it cannot be used in select and phi instructions.
    IsTokenLike = 1U << 4,
  };

  /// Return true if the target extension type contains the given property.
  /// \param Prop Property bit to test.
  /// \return true if this type has property \p Prop.
  LLVM_ABI bool hasProperty(Property Prop) const;

  /// Return an underlying layout type for this target extension type.
  ///
  /// This type can be used to query size and alignment information, if it is
  /// appropriate (although note that the layout type may also be void). It is
  /// not legal to bitcast between this type and the layout type, however.
  /// \return The layout type used for size and alignment queries.
  LLVM_ABI Type *getLayoutType() const;

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param T Type to test.
  /// \return true if \p T is a TargetExtType.
  static bool classof(const Type *T) { return T->getTypeID() == TargetExtTyID; }
};

/// Return the name of this target extension type.
/// \return The name of this TargetExtType.
StringRef Type::getTargetExtName() const {
  return cast<TargetExtType>(this)->getName();
}

} // end namespace llvm

#endif // LLVM_IR_DERIVEDTYPES_H
