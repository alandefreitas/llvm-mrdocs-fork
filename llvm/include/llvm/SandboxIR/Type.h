//===- llvm/SandboxIR/Type.h - Classes for handling data types --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is a thin wrapper over llvm::Type.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SANDBOXIR_TYPE_H
#define LLVM_SANDBOXIR_TYPE_H

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm::sandboxir {

class Context;
// Forward declare friend classes for MSVC.
class ArrayType;
class ByteType;
class CallBase;
class CmpInst;
class ConstantDataSequential;
class FixedVectorType;
class FPMathOperator;
class FunctionType;
class IntegerType;
class Module;
class PointerType;
class ScalableVectorType;
class StructType;
/// Target extension type wrapper (defined elsewhere).
class TargetExtType;
class VectorType;
#define DEF_INSTR(ID, OPCODE, CLASS) class CLASS;
#define DEF_CONST(ID, CLASS) class CLASS;
#define DEF_DISABLE_AUTO_UNDEF // ValuesDefFilesList.def includes multiple .def
#include "llvm/SandboxIR/ValuesDefFilesList.def"

/// Just like llvm::Type these are immutable, unique, never get freed and
/// can only be created via static factory methods.
class Type {
protected:
  /// The underlying LLVM IR type this SandboxIR type wraps.
  llvm::Type *LLVMTy;
  friend class ArrayType;          // For LLVMTy.
  friend class ByteType;           // For LLVMTy.
  friend class StructType;         // For LLVMTy.
  friend class VectorType;         // For LLVMTy.
  friend class FixedVectorType;    // For LLVMTy.
  friend class ScalableVectorType; // For LLVMTy.
  friend class PointerType;        // For LLVMTy.
  friend class FunctionType;       // For LLVMTy.
  friend class IntegerType;        // For LLVMTy.
  friend class Function;           // For LLVMTy.
  friend class CallBase;           // For LLVMTy.
  friend class ConstantInt;        // For LLVMTy.
  friend class ConstantArray;      // For LLVMTy.
  friend class ConstantStruct;     // For LLVMTy.
  friend class ConstantVector;     // For LLVMTy.
  friend class CmpInst;            // For LLVMTy. TODO: Cleanup after
                                   // sandboxir::VectorType is more complete.
  friend class Utils;              // for LLVMTy
  friend class TargetExtType;      // For LLVMTy.
  friend class Module;             // For LLVMTy.
  friend class FPMathOperator;     // For LLVMTy.
  friend class ConstantDataSequential; // For LLVMTy.

  // Friend all instruction classes because `create()` functions use LLVMTy.
#define DEF_INSTR(ID, OPCODE, CLASS) friend class CLASS;
#define DEF_CONST(ID, CLASS) friend class CLASS;
#define DEF_DISABLE_AUTO_UNDEF // ValuesDefFilesList.def includes multiple .def
#include "llvm/SandboxIR/ValuesDefFilesList.def"
#undef DEF_INSTR
#undef DEF_CONST
  /// SandboxIR context that owns and uniques this type.
  Context &Ctx;

  /// Construct a SandboxIR type wrapping \p LLVMTy in \p Ctx.
  /// \param LLVMTy Underlying LLVM IR type.
  /// \param Ctx SandboxIR context that owns this type.
  Type(llvm::Type *LLVMTy, Context &Ctx) : LLVMTy(LLVMTy), Ctx(Ctx) {}
  friend class Context; // For constructor and ~Type().
  /// Destroy this type; only Context may delete types.
  ~Type() = default;

public:
  /// Print the current type.
  ///
  /// Omit the type details if \p NoDetails == true.
  /// E.g., let %st = type { i32, i16 }
  /// When \p NoDetails is true, we only print %st.
  /// Put differently, \p NoDetails prints the type as if
  /// inlined with the operands when printing an instruction.
  /// \param OS Output stream.
  /// \param IsForDebug Whether to use debug-oriented printing.
  /// \param NoDetails If true, print only the type name without body details.
  void print(raw_ostream &OS, bool IsForDebug = false,
             bool NoDetails = false) const {
    LLVMTy->print(OS, IsForDebug, NoDetails);
  }

  /// Return the SandboxIR context that owns this type.
  /// \Returns The SandboxIR context that owns this type.
  Context &getContext() const { return Ctx; }

  /// Return true if this is 'void'.
  /// \Returns True if this is the void type.
  bool isVoidTy() const { return LLVMTy->isVoidTy(); }

  /// Return true if this is 'half', a 16-bit IEEE fp type.
  /// \Returns True if this is the half floating-point type.
  bool isHalfTy() const { return LLVMTy->isHalfTy(); }

  /// Return true if this is 'bfloat', a 16-bit bfloat type.
  /// \Returns True if this is the bfloat floating-point type.
  bool isBFloatTy() const { return LLVMTy->isBFloatTy(); }

  /// Return true if this is a 16-bit float type.
  /// \Returns True if this is half or bfloat.
  bool is16bitFPTy() const { return LLVMTy->is16bitFPTy(); }

  /// Return true if this is 'float', a 32-bit IEEE fp type.
  /// \Returns True if this is the float type.
  bool isFloatTy() const { return LLVMTy->isFloatTy(); }

  /// Return true if this is 'double', a 64-bit IEEE fp type.
  /// \Returns True if this is the double type.
  bool isDoubleTy() const { return LLVMTy->isDoubleTy(); }

  /// Return true if this is x86 long double.
  /// \Returns True if this is the x86_fp80 type.
  bool isX86_FP80Ty() const { return LLVMTy->isX86_FP80Ty(); }

  /// Return true if this is 'fp128'.
  /// \Returns True if this is the fp128 type.
  bool isFP128Ty() const { return LLVMTy->isFP128Ty(); }

  /// Return true if this is powerpc long double.
  /// \Returns True if this is the ppc_fp128 type.
  bool isPPC_FP128Ty() const { return LLVMTy->isPPC_FP128Ty(); }

  /// Return true if this is a well-behaved IEEE-like type, which has a IEEE
  /// compatible layout, and does not have non-IEEE values, such as x86_fp80's
  /// unnormal values.
  /// \Returns True if this is an IEEE-like floating-point type.
  bool isIEEELikeFPTy() const { return LLVMTy->isIEEELikeFPTy(); }

  /// Return true if this is one of the floating-point types
  /// \Returns True if this is any floating-point type.
  bool isFloatingPointTy() const { return LLVMTy->isFloatingPointTy(); }

  /// Return true if this is a multi-unit floating-point type.
  ///
  /// Returns true if this is a floating-point type that is an unevaluated sum
  /// of multiple floating-point units.
  /// An example of such a type is ppc_fp128, also known as double-double, which
  /// consists of two IEEE 754 doubles.
  /// \Returns True if this is a multi-unit floating-point type.
  bool isMultiUnitFPType() const { return LLVMTy->isMultiUnitFPType(); }

  /// Return the floating-point semantics for this floating-point type.
  /// \Returns The floating-point semantics for this type.
  const fltSemantics &getFltSemantics() const {
    return LLVMTy->getFltSemantics();
  }

  /// Return true if this is X86 AMX.
  /// \Returns True if this is the X86 AMX type.
  bool isX86_AMXTy() const { return LLVMTy->isX86_AMXTy(); }

  /// Return true if this is a target extension type.
  /// \Returns True if this is a target extension type.
  bool isTargetExtTy() const { return LLVMTy->isTargetExtTy(); }

  /// Return true if this is a target extension type with a scalable layout.
  /// \Returns True if this is a scalable target extension type.
  bool isScalableTargetExtTy() const { return LLVMTy->isScalableTargetExtTy(); }

  /// Return true if this is a type whose size is a known multiple of vscale.
  /// \Returns True if this type's size is a known multiple of vscale.
  bool isScalableTy() const { return LLVMTy->isScalableTy(); }

  /// Return true if this is a FP type or a vector of FP.
  /// \Returns True if this is a floating-point type or vector thereof.
  bool isFPOrFPVectorTy() const { return LLVMTy->isFPOrFPVectorTy(); }

  /// Return true if this is 'label'.
  /// \Returns True if this is the label type.
  bool isLabelTy() const { return LLVMTy->isLabelTy(); }

  /// Return true if this is 'metadata'.
  /// \Returns True if this is the metadata type.
  bool isMetadataTy() const { return LLVMTy->isMetadataTy(); }

  /// Return true if this is 'token'.
  /// \Returns True if this is the token type.
  bool isTokenTy() const { return LLVMTy->isTokenTy(); }

  /// True if this is an instance of IntegerType.
  /// \Returns True if this is an IntegerType.
  bool isIntegerTy() const { return LLVMTy->isIntegerTy(); }

  /// True if this is an instance of ByteType.
  /// \Returns True if this is a ByteType.
  bool isByteTy() const { return LLVMTy->isByteTy(); }

  /// Return true if this is a ByteType of the given width.
  /// \param Bitwidth Required byte type bit width.
  /// \Returns True if this is a ByteType of width \p Bitwidth.
  bool isByteTy(unsigned Bitwidth) const { return LLVMTy->isByteTy(Bitwidth); }

  /// Return true if this is an IntegerType of the given width.
  /// \param Bitwidth Required integer bit width.
  /// \Returns True if this is an IntegerType of width \p Bitwidth.
  bool isIntegerTy(unsigned Bitwidth) const {
    return LLVMTy->isIntegerTy(Bitwidth);
  }

  /// Return true if this is an integer type or a vector of integer types.
  /// \Returns True if this is an integer type or a vector of integer types.
  bool isIntOrIntVectorTy() const { return LLVMTy->isIntOrIntVectorTy(); }

  /// Return true if this is an integer type or a vector of integer types of
  /// the given width.
  /// \param BitWidth Required integer (or integer-vector element) bit width.
  /// \Returns True if this is an integer or integer-vector type of width
  /// \p BitWidth.
  bool isIntOrIntVectorTy(unsigned BitWidth) const {
    return LLVMTy->isIntOrIntVectorTy(BitWidth);
  }

  /// Return true if this is an integer type or a pointer type.
  /// \Returns True if this is an integer type or a pointer type.
  bool isIntOrPtrTy() const { return LLVMTy->isIntOrPtrTy(); }

  /// True if this is an instance of FunctionType.
  /// \Returns True if this is a FunctionType.
  bool isFunctionTy() const { return LLVMTy->isFunctionTy(); }

  /// True if this is an instance of StructType.
  /// \Returns True if this is a StructType.
  bool isStructTy() const { return LLVMTy->isStructTy(); }

  /// True if this is an instance of ArrayType.
  /// \Returns True if this is an ArrayType.
  bool isArrayTy() const { return LLVMTy->isArrayTy(); }

  /// True if this is an instance of PointerType.
  /// \Returns True if this is a PointerType.
  bool isPointerTy() const { return LLVMTy->isPointerTy(); }

  /// Return true if this is a pointer type or a vector of pointer types.
  /// \Returns True if this is a pointer type or a vector of pointer types.
  bool isPtrOrPtrVectorTy() const { return LLVMTy->isPtrOrPtrVectorTy(); }

  /// True if this is an instance of VectorType.
  /// \Returns True if this is a VectorType.
  inline bool isVectorTy() const { return LLVMTy->isVectorTy(); }

  /// Return true if this type could be converted with a lossless BitCast to Ty.
  ///
  /// For example, i8* to i32*. BitCasts are valid for types of the same size
  /// only where no re-interpretation of the bits is done.
  /// Determine if this type could be losslessly bitcast to Ty
  /// \param Ty Destination type for the bitcast.
  /// \Returns True if this type can be losslessly bitcast to \p Ty.
  bool canLosslesslyBitCastTo(Type *Ty) const {
    return LLVMTy->canLosslesslyBitCastTo(Ty->LLVMTy);
  }

  /// Return true if this type is empty, that is, it has no elements or all of
  /// its elements are empty.
  /// \Returns True if this type is empty.
  bool isEmptyTy() const { return LLVMTy->isEmptyTy(); }

  /// Return true if the type is "first class", meaning it is a valid type for a
  /// Value.
  /// \Returns True if this is a first-class type.
  bool isFirstClassType() const { return LLVMTy->isFirstClassType(); }

  /// Return true if the type is a valid type for a register in codegen. This
  /// includes all first-class types except struct and array types.
  /// \Returns True if this is a single-value type.
  bool isSingleValueType() const { return LLVMTy->isSingleValueType(); }

  /// Return true if the type is an aggregate type.
  ///
  /// This means it is valid as the first operand of an insertvalue or
  /// extractvalue instruction. This includes struct and array types, but does
  /// not include vector types.
  /// \Returns True if this is an aggregate type.
  bool isAggregateType() const { return LLVMTy->isAggregateType(); }

  /// Return true if it makes sense to take the size of this type.
  ///
  /// To get the actual size for a particular target, it is reasonable to use
  /// the DataLayout subsystem to do this.
  /// \param Visited Optional set used to detect cycles while sizing derived
  ///        types.
  /// \Returns True if this type has a meaningful size.
  bool isSized(SmallPtrSetImpl<Type *> *Visited = nullptr) const {
    SmallPtrSet<llvm::Type *, 8> LLVMVisited;
    LLVMVisited.reserve(Visited->size());
    for (Type *Ty : *Visited)
      LLVMVisited.insert(Ty->LLVMTy);
    return LLVMTy->isSized(&LLVMVisited);
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
  /// \Returns The primitive size in bits, or zero if not applicable.
  TypeSize getPrimitiveSizeInBits() const {
    return LLVMTy->getPrimitiveSizeInBits();
  }

  /// If this is a vector type, return the getPrimitiveSizeInBits value for the
  /// element type. Otherwise return the getPrimitiveSizeInBits value for this
  /// type.
  /// \Returns The scalar size in bits.
  unsigned getScalarSizeInBits() const { return LLVMTy->getScalarSizeInBits(); }

  /// Return the width of the mantissa of this floating-point type.
  ///
  /// This is only valid on floating-point types. If the FP type does not have a
  /// stable mantissa (e.g. ppc long double), this method returns -1.
  /// \Returns The mantissa width in bits, or -1 if unstable.
  int getFPMantissaWidth() const { return LLVMTy->getFPMantissaWidth(); }

  /// If this is a vector type, return the element type, otherwise return
  /// 'this'.
  /// \Returns The scalar (element) type, or this type if not a vector.
  LLVM_ABI Type *getScalarType() const;

  // TODO: ADD MISSING

  /// Return the 64-bit integer type.
  /// \param Ctx SandboxIR context in which to unique the type.
  /// \Returns The 64-bit IntegerType for \p Ctx.
  LLVM_ABI static IntegerType *getInt64Ty(Context &Ctx);
  /// Return the 32-bit integer type.
  /// \param Ctx SandboxIR context in which to unique the type.
  /// \Returns The 32-bit IntegerType for \p Ctx.
  LLVM_ABI static IntegerType *getInt32Ty(Context &Ctx);
  /// Return the 16-bit integer type.
  /// \param Ctx SandboxIR context in which to unique the type.
  /// \Returns The 16-bit IntegerType for \p Ctx.
  LLVM_ABI static IntegerType *getInt16Ty(Context &Ctx);
  /// Return the 8-bit integer type.
  /// \param Ctx SandboxIR context in which to unique the type.
  /// \Returns The 8-bit IntegerType for \p Ctx.
  LLVM_ABI static IntegerType *getInt8Ty(Context &Ctx);
  /// Return the 1-bit integer type.
  /// \param Ctx SandboxIR context in which to unique the type.
  /// \Returns The 1-bit IntegerType for \p Ctx.
  LLVM_ABI static IntegerType *getInt1Ty(Context &Ctx);
  /// Return a byte type with bit width \p N.
  /// \param Ctx SandboxIR context in which to unique the type.
  /// \param N Bit width of the byte type.
  /// \Returns The ByteType of width \p N for \p Ctx.
  LLVM_ABI static ByteType *getByteNTy(Context &Ctx, unsigned N);
  /// Return the 1-bit byte type.
  /// \param Ctx SandboxIR context in which to unique the type.
  /// \Returns The 1-bit ByteType for \p Ctx.
  LLVM_ABI static ByteType *getByte1Ty(Context &Ctx);
  /// Return the 8-bit byte type.
  /// \param Ctx SandboxIR context in which to unique the type.
  /// \Returns The 8-bit ByteType for \p Ctx.
  LLVM_ABI static ByteType *getByte8Ty(Context &Ctx);
  /// Return the 16-bit byte type.
  /// \param Ctx SandboxIR context in which to unique the type.
  /// \Returns The 16-bit ByteType for \p Ctx.
  LLVM_ABI static ByteType *getByte16Ty(Context &Ctx);
  /// Return the 32-bit byte type.
  /// \param Ctx SandboxIR context in which to unique the type.
  /// \Returns The 32-bit ByteType for \p Ctx.
  LLVM_ABI static ByteType *getByte32Ty(Context &Ctx);
  /// Return the 64-bit byte type.
  /// \param Ctx SandboxIR context in which to unique the type.
  /// \Returns The 64-bit ByteType for \p Ctx.
  LLVM_ABI static ByteType *getByte64Ty(Context &Ctx);
  /// Return the 128-bit byte type.
  /// \param Ctx SandboxIR context in which to unique the type.
  /// \Returns The 128-bit ByteType for \p Ctx.
  LLVM_ABI static ByteType *getByte128Ty(Context &Ctx);
  /// Returns an integer (vector of integer) type with the same size of a byte
  /// of the given byte (vector of byte) type.
  /// \param Ty Byte or byte-vector type to convert.
  /// \Returns An integer or integer-vector type matching \p Ty's size.
  LLVM_ABI static Type *getIntFromByteType(Type *Ty);
  /// Returns a byte (vector of byte) type with the same size of an integer of
  /// the given integer (vector of integer) type.
  /// \param Ty Integer or integer-vector type to convert.
  /// \Returns A byte or byte-vector type matching \p Ty's size.
  LLVM_ABI static Type *getByteFromIntType(Type *Ty);
  /// Return the IEEE double-precision (64-bit) floating-point type.
  /// \param Ctx SandboxIR context in which to unique the type.
  /// \Returns The double floating-point type for \p Ctx.
  LLVM_ABI static Type *getDoubleTy(Context &Ctx);
  /// Return the IEEE single-precision (32-bit) floating-point type.
  /// \param Ctx SandboxIR context in which to unique the type.
  /// \Returns The float floating-point type for \p Ctx.
  LLVM_ABI static Type *getFloatTy(Context &Ctx);
  /// Return the IEEE half-precision (16-bit) floating-point type.
  /// \param Ctx SandboxIR context in which to unique the type.
  /// \Returns The half floating-point type for \p Ctx.
  LLVM_ABI static Type *getHalfTy(Context &Ctx);
  // TODO: missing get*

  /// Get the address space of this pointer or pointer vector type.
  /// \Returns The address space number of this pointer or pointer-vector type.
  inline unsigned getPointerAddressSpace() const {
    return LLVMTy->getPointerAddressSpace();
  }

#ifndef NDEBUG
  /// Dump this type to \p OS.
  /// \param OS Output stream.
  void dumpOS(raw_ostream &OS);
  /// Dump this type to stderr for debugging.
  LLVM_DUMP_METHOD void dump();
#endif // NDEBUG
};

/// Class to represent pointers.
class PointerType : public Type {
public:
  // TODO: add missing functions

  /// This constructs an opaque pointer to an object in a numbered address
  /// space.
  /// \param Ctx SandboxIR context in which to unique the type.
  /// \param AddressSpace Address space number for the pointer.
  /// \Returns The PointerType for \p AddressSpace in \p Ctx.
  LLVM_ABI static PointerType *get(Context &Ctx, unsigned AddressSpace);

  /// For isa/dyn_cast.
  /// \param From Type to test for PointerType.
  /// \Returns True if \p From is a PointerType.
  static bool classof(const Type *From) {
    return isa<llvm::PointerType>(From->LLVMTy);
  }
};

/// Class to represent array types.
class ArrayType : public Type {
public:
  /// This static method is the primary way to construct an ArrayType.
  /// \param ElementType Element type of the array.
  /// \param NumElements Number of elements in the array.
  /// \Returns The ArrayType with \p NumElements of \p ElementType.
  LLVM_ABI static ArrayType *get(Type *ElementType, uint64_t NumElements);
  // TODO: add missing functions
  /// For isa/dyn_cast.
  /// \param From Type to test for ArrayType.
  /// \Returns True if \p From is an ArrayType.
  static bool classof(const Type *From) {
    return isa<llvm::ArrayType>(From->LLVMTy);
  }
};

/// Class to represent struct types.
class StructType : public Type {
public:
  /// This static method is the primary way to create a literal StructType.
  /// \param Ctx SandboxIR context in which to unique the type.
  /// \param Elements Element types of the struct.
  /// \param IsPacked Whether the struct is packed (no padding between elements).
  /// \Returns The StructType with the given elements in \p Ctx.
  LLVM_ABI static StructType *get(Context &Ctx, ArrayRef<Type *> Elements,
                                  bool IsPacked = false);

  /// Return true if this is a packed struct.
  /// \Returns True if this is a packed struct.
  bool isPacked() const { return cast<llvm::StructType>(LLVMTy)->isPacked(); }

  // TODO: add missing functions
  /// For isa/dyn_cast.
  /// \param From Type to test for StructType.
  /// \Returns True if \p From is a StructType.
  static bool classof(const Type *From) {
    return isa<llvm::StructType>(From->LLVMTy);
  }
};

/// Base class of all SIMD vector types.
class VectorType : public Type {
public:
  /// This static method is the primary way to construct a VectorType.
  /// \param ElementType Element type of the vector.
  /// \param EC Element count (fixed or scalable).
  /// \Returns The VectorType with \p ElementType and count \p EC.
  LLVM_ABI static VectorType *get(Type *ElementType, ElementCount EC);
  /// Construct a VectorType from an element type, count, and scalability flag.
  /// \param ElementType Element type of the vector.
  /// \param NumElements Minimum (or exact) number of elements.
  /// \param Scalable Whether the vector is scalable.
  /// \Returns The VectorType with the given shape.
  static VectorType *get(Type *ElementType, unsigned NumElements,
                         bool Scalable) {
    return VectorType::get(ElementType,
                           ElementCount::get(NumElements, Scalable));
  }
  /// Return the element type of this vector.
  /// \Returns The element type of this vector.
  LLVM_ABI Type *getElementType() const;

  /// Construct a VectorType with \p ElementType and the same count as \p Other.
  /// \param ElementType Element type of the vector.
  /// \param Other Vector whose element count is reused.
  /// \Returns The VectorType with \p ElementType and \p Other's element count.
  static VectorType *get(Type *ElementType, const VectorType *Other) {
    return VectorType::get(ElementType, Other->getElementCount());
  }

  /// Return an ElementCount instance to represent the (possibly scalable)
  /// number of elements in the vector.
  /// \Returns The element count of this vector.
  inline ElementCount getElementCount() const {
    return cast<llvm::VectorType>(LLVMTy)->getElementCount();
  }
  /// Return a VectorType of integer elements matching \p VTy's shape and width.
  /// \param VTy Input vector type.
  /// \Returns A vector of integer elements matching \p VTy's shape and width.
  LLVM_ABI static VectorType *getInteger(VectorType *VTy);
  /// Return a VectorType with the same length and double-width integer elements.
  /// \param VTy Input vector of integer elements.
  /// \Returns A vector with the same length and double-width integer elements.
  LLVM_ABI static VectorType *getExtendedElementVectorType(VectorType *VTy);
  /// Return a VectorType with the same length and half-width element type.
  /// \param VTy Input vector type.
  /// \Returns A vector with the same length and half-width element type.
  LLVM_ABI static VectorType *getTruncatedElementVectorType(VectorType *VTy);
  /// Return a VectorType with more elements of a narrower type than \p VTy.
  /// \param VTy Input vector type.
  /// \param NumSubdivs Number of subdivision steps to apply.
  /// \Returns A subdivided vector type derived from \p VTy.
  LLVM_ABI static VectorType *getSubdividedVectorType(VectorType *VTy,
                                                      int NumSubdivs);
  /// Return a VectorType with half as many elements as \p VTy.
  /// \param VTy Input vector type.
  /// \Returns A vector with half as many elements as \p VTy.
  LLVM_ABI static VectorType *getHalfElementsVectorType(VectorType *VTy);
  /// Return a VectorType with twice as many elements as \p VTy.
  /// \param VTy Input vector type.
  /// \Returns A vector with twice as many elements as \p VTy.
  LLVM_ABI static VectorType *getDoubleElementsVectorType(VectorType *VTy);
  /// Return true if the specified type is valid as a element type.
  /// \param ElemTy Candidate vector element type.
  /// \Returns True if \p ElemTy is a valid vector element type.
  LLVM_ABI static bool isValidElementType(Type *ElemTy);

  /// For isa/dyn_cast.
  /// \param From Type to test for VectorType.
  /// \Returns True if \p From is a VectorType.
  static bool classof(const Type *From) {
    return isa<llvm::VectorType>(From->LLVMTy);
  }
};

/// Class to represent fixed width SIMD vectors.
class FixedVectorType : public VectorType {
public:
  /// Get or create a fixed vector type with the given element type and length.
  /// \param ElementType Element type of the vector.
  /// \param NumElts Number of elements in the vector.
  /// \Returns The FixedVectorType with \p NumElts of \p ElementType.
  LLVM_ABI static FixedVectorType *get(Type *ElementType, unsigned NumElts);

  /// Get a fixed vector with \p ElementType and the same length as \p FVTy.
  /// \param ElementType Element type of the vector.
  /// \param FVTy Fixed vector whose element count is reused.
  /// \Returns The FixedVectorType with \p ElementType and \p FVTy's length.
  static FixedVectorType *get(Type *ElementType, const FixedVectorType *FVTy) {
    return get(ElementType, FVTy->getNumElements());
  }

  /// Return a fixed vector of integer elements with the same width as \p VTy.
  /// \param VTy Input fixed vector type.
  /// \Returns A fixed vector of integer elements matching \p VTy's shape.
  static FixedVectorType *getInteger(FixedVectorType *VTy) {
    return cast<FixedVectorType>(VectorType::getInteger(VTy));
  }

  /// Return a fixed vector with the same lane count and element type twice as
  /// wide as \p VTy's elements.
  /// \param VTy Input fixed vector type.
  /// \Returns A fixed vector with double-width elements.
  static FixedVectorType *getExtendedElementVectorType(FixedVectorType *VTy) {
    return cast<FixedVectorType>(VectorType::getExtendedElementVectorType(VTy));
  }

  /// Return a fixed vector with the same lane count and element type half as
  /// wide as \p VTy's elements.
  /// \param VTy Input fixed vector type.
  /// \Returns A fixed vector with half-width elements.
  static FixedVectorType *getTruncatedElementVectorType(FixedVectorType *VTy) {
    return cast<FixedVectorType>(
        VectorType::getTruncatedElementVectorType(VTy));
  }

  /// Return a fixed vector with more elements of a narrower type than \p VTy.
  /// \param VTy Input fixed vector type.
  /// \param NumSubdivs Number of subdivision steps to apply.
  /// \Returns A subdivided fixed vector type derived from \p VTy.
  static FixedVectorType *getSubdividedVectorType(FixedVectorType *VTy,
                                                  int NumSubdivs) {
    return cast<FixedVectorType>(
        VectorType::getSubdividedVectorType(VTy, NumSubdivs));
  }

  /// Return a fixed vector with half as many elements as \p VTy.
  /// \param VTy Input fixed vector type.
  /// \Returns A fixed vector with half as many elements as \p VTy.
  static FixedVectorType *getHalfElementsVectorType(FixedVectorType *VTy) {
    return cast<FixedVectorType>(VectorType::getHalfElementsVectorType(VTy));
  }

  /// Return a fixed vector with twice as many elements as \p VTy.
  /// \param VTy Input fixed vector type.
  /// \Returns A fixed vector with twice as many elements as \p VTy.
  static FixedVectorType *getDoubleElementsVectorType(FixedVectorType *VTy) {
    return cast<FixedVectorType>(VectorType::getDoubleElementsVectorType(VTy));
  }

  /// For isa/dyn_cast.
  /// \param T Type to test for FixedVectorType.
  /// \Returns True if \p T is a FixedVectorType.
  static bool classof(const Type *T) {
    return isa<llvm::FixedVectorType>(T->LLVMTy);
  }

  /// Return the number of lanes in this fixed-width vector.
  /// \Returns The number of elements in this fixed-width vector.
  unsigned getNumElements() const {
    return cast<llvm::FixedVectorType>(LLVMTy)->getNumElements();
  }
};

/// Class to represent scalable SIMD vectors.
class ScalableVectorType : public VectorType {
public:
  /// Get or create a scalable vector type with the given element type and min
  /// length.
  /// \param ElementType Element type of the vector.
  /// \param MinNumElts Minimum number of elements (vscale multiple).
  /// \Returns The ScalableVectorType with min length \p MinNumElts.
  LLVM_ABI static ScalableVectorType *get(Type *ElementType,
                                          unsigned MinNumElts);

  /// Get a scalable vector with \p ElementType and the same min length as
  /// \p SVTy.
  /// \param ElementType Element type of the vector.
  /// \param SVTy Scalable vector whose minimum element count is reused.
  /// \Returns The ScalableVectorType with \p ElementType and \p SVTy's min
  /// length.
  static ScalableVectorType *get(Type *ElementType,
                                 const ScalableVectorType *SVTy) {
    return get(ElementType, SVTy->getMinNumElements());
  }

  /// Return a scalable vector with the same minimum lane count as \p VTy and
  /// integer elements of the same bit width as \p VTy's element type.
  /// \param VTy Input scalable vector type.
  /// \Returns A scalable vector of integer elements matching \p VTy's shape.
  static ScalableVectorType *getInteger(ScalableVectorType *VTy) {
    return cast<ScalableVectorType>(VectorType::getInteger(VTy));
  }

  /// Return a scalable vector with the same min lane count and double-width
  /// elements.
  /// \param VTy Input scalable vector type.
  /// \Returns A scalable vector with double-width elements.
  static ScalableVectorType *
  getExtendedElementVectorType(ScalableVectorType *VTy) {
    return cast<ScalableVectorType>(
        VectorType::getExtendedElementVectorType(VTy));
  }

  /// Return a scalable vector with the same min lane count and half-width
  /// elements.
  /// \param VTy Input scalable vector type.
  /// \Returns A scalable vector with half-width elements.
  static ScalableVectorType *
  getTruncatedElementVectorType(ScalableVectorType *VTy) {
    return cast<ScalableVectorType>(
        VectorType::getTruncatedElementVectorType(VTy));
  }

  /// Return a scalable vector with more elements of a narrower type than
  /// \p VTy.
  /// \param VTy Input scalable vector type.
  /// \param NumSubdivs Number of subdivision steps to apply.
  /// \Returns A subdivided scalable vector type derived from \p VTy.
  static ScalableVectorType *getSubdividedVectorType(ScalableVectorType *VTy,
                                                     int NumSubdivs) {
    return cast<ScalableVectorType>(
        VectorType::getSubdividedVectorType(VTy, NumSubdivs));
  }

  /// Return a scalable vector with half as many elements as \p VTy.
  /// \param VTy Input scalable vector type.
  /// \Returns A scalable vector with half as many elements as \p VTy.
  static ScalableVectorType *
  getHalfElementsVectorType(ScalableVectorType *VTy) {
    return cast<ScalableVectorType>(VectorType::getHalfElementsVectorType(VTy));
  }

  /// Return a scalable vector with twice as many elements as \p VTy.
  /// \param VTy Input scalable vector type.
  /// \Returns A scalable vector with twice as many elements as \p VTy.
  static ScalableVectorType *
  getDoubleElementsVectorType(ScalableVectorType *VTy) {
    return cast<ScalableVectorType>(
        VectorType::getDoubleElementsVectorType(VTy));
  }

  /// Get the minimum number of elements in this vector.
  /// \Returns The minimum number of elements in this scalable vector.
  unsigned getMinNumElements() const {
    return cast<llvm::ScalableVectorType>(LLVMTy)->getMinNumElements();
  }

  /// For isa/dyn_cast.
  /// \param T Type to test for ScalableVectorType.
  /// \Returns True if \p T is a ScalableVectorType.
  static bool classof(const Type *T) {
    return isa<llvm::ScalableVectorType>(T->LLVMTy);
  }
};

/// Class to represent function types.
class FunctionType : public Type {
public:
  // TODO: add missing functions
  /// For isa/dyn_cast.
  /// \param From Type to test for FunctionType.
  /// \Returns True if \p From is a FunctionType.
  static bool classof(const Type *From) {
    return isa<llvm::FunctionType>(From->LLVMTy);
  }
};

/// Class to represent integer types.
///
/// Note that this class is also used to represent the built-in integer types:
/// Int1Ty, Int8Ty, Int16Ty, Int32Ty and Int64Ty.
class IntegerType : public Type {
public:
  /// Get or create an IntegerType instance.
  /// \param C SandboxIR context in which to unique the type.
  /// \param NumBits Bit width of the integer type.
  /// \Returns The IntegerType of width \p NumBits in \p C.
  LLVM_ABI static IntegerType *get(Context &C, unsigned NumBits);
  // TODO: add missing functions
  /// For isa/dyn_cast.
  /// \param From Type to test for IntegerType.
  /// \Returns True if \p From is an IntegerType.
  static bool classof(const Type *From) {
    return isa<llvm::IntegerType>(From->LLVMTy);
  }
  /// Convert to the underlying LLVM IntegerType reference.
  /// \Returns A reference to the underlying llvm::IntegerType.
  operator llvm::IntegerType &() const {
    return *cast<llvm::IntegerType>(LLVMTy);
  }
};

/// Class to represent byte types.
class ByteType : public Type {
public:
  /// Get or create a ByteType instance.
  /// \param C SandboxIR context in which to unique the type.
  /// \param NumBits Bit width of the byte type.
  /// \Returns The ByteType of width \p NumBits in \p C.
  LLVM_ABI static ByteType *get(Context &C, unsigned NumBits);

  /// Get the number of bits in this ByteType
  /// \Returns The bit width of this ByteType.
  unsigned getBitWidth() const {
    return cast<llvm::ByteType>(LLVMTy)->getBitWidth();
  }

  /// Get a bit mask for this type.
  /// \Returns An APInt mask with all bits set for this ByteType's width.
  APInt getMask() const { return cast<llvm::ByteType>(LLVMTy)->getMask(); }

  /// For isa/dyn_cast.
  /// \param From Type to test for ByteType.
  /// \Returns True if \p From is a ByteType.
  static bool classof(const Type *From) {
    return isa<llvm::ByteType>(From->LLVMTy);
  }
};

} // namespace llvm::sandboxir

#endif // LLVM_SANDBOXIR_TYPE_H
