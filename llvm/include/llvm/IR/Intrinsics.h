//===- Intrinsics.h - LLVM Intrinsic Function Handling ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a set of enums which allow processing of intrinsic
// functions. Values of these enum types are returned by
// Function::getIntrinsicID.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_INTRINSICS_H
#define LLVM_IR_INTRINSICS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/TypeSize.h"
#include <optional>
#include <string>
#include <tuple>

namespace llvm {

class Type;
class FunctionType;
class Function;
class LLVMContext;
class Module;
class AttributeList;
class AttributeSet;
class raw_ostream;
class Constant;

namespace Intrinsic {
// Abstraction for the arguments of the noalias intrinsics
static const int NoAliasScopeDeclScopeArg = 0;

// Intrinsic ID type. This is an opaque typedef to facilitate splitting up
// the enum into target-specific enums.
typedef unsigned ID;

/// Enumeration of target-independent intrinsic function IDs.
///
/// Values are generated from \c Intrinsics.td and returned by
/// \c Function::getIntrinsicID(). Target-specific intrinsics use
/// separate enums in their respective headers.
enum IndependentIntrinsics : unsigned {
  not_intrinsic = 0, ///< Sentinel for a non-intrinsic; must be zero.

// Get the intrinsic enums generated from Intrinsics.td
#define GET_INTRINSIC_ENUM_VALUES
#include "llvm/IR/IntrinsicEnums.inc"
};

/// Return the LLVM name for an intrinsic, such as "llvm.ppc.altivec.lvx".
///
/// Note, this version is for intrinsics with no overloads.  Use the other
/// version of getName if overloads are required.
///
/// \param id Intrinsic ID whose name is requested.
/// \return The LLVM name of the intrinsic.
LLVM_ABI StringRef getName(ID id);

/// Return the LLVM name for an intrinsic, without encoded types for
/// overloading, such as "llvm.ssa.copy".
///
/// \param id Intrinsic ID whose base name is requested.
/// \return The unmangled LLVM name of the intrinsic.
LLVM_ABI StringRef getBaseName(ID id);

/// Return the target feature expression required by an intrinsic.
///
/// \param id Intrinsic ID whose required features are requested.
/// \return The required target-feature expression, or empty if none.
LLVM_ABI StringRef getRequiredTargetFeatures(ID id);

/// Return the LLVM name for an overloaded intrinsic.
///
/// Returns a name such as "llvm.ppc.altivec.lvx" or "llvm.ssa.copy.p0s_s.1".
/// Note, this version of getName supports overloads. This is less efficient
/// than the StringRef version of this function.  If no overloads are required,
/// it is safe to use this version, but better to use the StringRef version. If
/// one of the types is based on an unnamed type, a function type will be
/// computed. Providing FT will avoid this computation.
///
/// \param Id Intrinsic ID whose mangled name is requested.
/// \param OverloadTys Overload types that select the mangled name.
/// \param M Module used when resolving unnamed types.
/// \param FT Optional function type that avoids recomputing unnamed types.
/// \return The mangled LLVM name of the overloaded intrinsic.
LLVM_ABI std::string getName(ID Id, ArrayRef<Type *> OverloadTys, Module *M,
                             FunctionType *FT = nullptr);

/// Return the LLVM name for an intrinsic overloaded on named types only.
///
/// This is a special version only to be used by
/// LLVMIntrinsicCopyOverloadedName. It only supports overloads based on named
/// types.
///
/// \param Id Intrinsic ID whose mangled name is requested.
/// \param OverloadTys Named overload types that select the mangled name.
/// \return The mangled name for the named-type overloads.
LLVM_ABI std::string getNameNoUnnamedTypes(ID Id, ArrayRef<Type *> OverloadTys);

/// Return the function type for an intrinsic.
///
/// \param Context Context in which to create the function type.
/// \param id Intrinsic ID whose type is requested.
/// \param OverloadTys Overload types for overloaded intrinsics.
/// \return The function type of the intrinsic.
LLVM_ABI FunctionType *getType(LLVMContext &Context, ID id,
                               ArrayRef<Type *> OverloadTys = {});

/// Returns true if the intrinsic can be overloaded.
///
/// \param id Intrinsic ID to query.
/// \return True if the intrinsic can be overloaded.
LLVM_ABI bool isOverloaded(ID id);

/// Returns true if the intrinsic is trivially scalarizable.
///
/// This means that the intrinsic's argument types are all scalars for the
/// scalar form and all vectors for the vector form.
///
/// \param id Intrinsic ID to query.
/// \return True if the intrinsic is trivially scalarizable.
LLVM_ABI bool isTriviallyScalarizable(ID id);

/// Returns true if the intrinsic has pretty printed immediate arguments.
///
/// \param id Intrinsic ID to query.
/// \return True if the intrinsic has pretty-printed immediate arguments.
LLVM_ABI bool hasPrettyPrintedArgs(ID id);

/// Return the default-argument index and values for intrinsic \p IID.
///
/// Returns the first default argument index and an ArrayRef of all
/// default values for the trailing parameters of intrinsic IID.
/// Returns {0, empty} if the intrinsic has no default arguments.
///
/// The defaults are stored contiguously starting at FirstDefault and
/// extending to the last parameter (mirrors C++ default-argument
/// rules).
///
/// \param IID Intrinsic ID whose default argument values are requested.
/// \return Pair of the first default-argument index and the default values, or
///         {0, empty} if none.
LLVM_ABI std::pair<unsigned, ArrayRef<uint64_t>> getAllDefaultArgValues(ID IID);

/// isTargetIntrinsic - Returns true if IID is an intrinsic specific to a
/// certain target. If it is a generic intrinsic false is returned.
///
/// \param IID Intrinsic ID to classify.
/// \return True if \p IID is target-specific.
LLVM_ABI bool isTargetIntrinsic(ID IID);

/// Look up an intrinsic ID by its name string.
///
/// Returns \c not_intrinsic if \p Name does not match a known intrinsic.
///
/// \param Name Intrinsic name to look up, such as "llvm.ssa.copy".
/// \return The matching intrinsic ID, or \c not_intrinsic if none matches.
LLVM_ABI ID lookupIntrinsicID(StringRef Name);

/// Return the attributes for an intrinsic.
///
/// \param C Context used to construct the attribute list.
/// \param id Intrinsic ID whose attributes are requested.
/// \param FT Function type of the intrinsic declaration.
/// \return The attribute list for the intrinsic.
LLVM_ABI AttributeList getAttributes(LLVMContext &C, ID id, FunctionType *FT);

/// Return the function attributes for an intrinsic.
///
/// \param C Context used to construct the attribute set.
/// \param id Intrinsic ID whose function attributes are requested.
/// \return The function attribute set for the intrinsic.
LLVM_ABI AttributeSet getFnAttributes(LLVMContext &C, ID id);

/// Look up or insert the Function declaration for intrinsic \p id in \p M.
///
/// If it does not exist, add a declaration and return it. Otherwise, return
/// the existing declaration.
///
/// The \p OverloadTys parameter is for intrinsics with overloaded types
/// (e.g., those using iAny, fAny, vAny, or pAny).  For a declaration of an
/// overloaded intrinsic, OverloadTys must provide exactly one type for each
/// overloaded type in the intrinsic.
///
/// \param M Module in which to look up or insert the declaration.
/// \param id Intrinsic ID to declare.
/// \param OverloadTys Overload types for overloaded intrinsics.
/// \return The existing or newly inserted Function declaration.
LLVM_ABI Function *getOrInsertDeclaration(Module *M, ID id,
                                          ArrayRef<Type *> OverloadTys = {});

/// Look up or insert the Function declaration for intrinsic \p IID in \p M.
///
/// If it does not exist, add a declaration and return it. Otherwise, return
/// the existing declaration.
///
/// This overload automatically resolves overloaded intrinsics based on the
/// provided return type and argument types. For non-overloaded intrinsics,
/// the return type and argument types are ignored.
///
/// \param M - The module to get or insert the intrinsic declaration.
/// \param IID - The intrinsic ID.
/// \param RetTy - The return type of the intrinsic.
/// \param ArgTys - The argument types of the intrinsic.
/// \return The existing or newly inserted Function declaration.
LLVM_ABI Function *getOrInsertDeclaration(Module *M, ID IID, Type *RetTy,
                                          ArrayRef<Type *> ArgTys);

/// Look up the Function declaration of intrinsic \p id if it exists.
///
/// Returns the declaration in Module \p M when present; otherwise returns
/// nullptr. This version supports non-overloaded intrinsics.
///
/// \param M Module in which to look up the declaration.
/// \param id Intrinsic ID to look up.
/// \return The Function declaration if present; otherwise nullptr.
LLVM_ABI Function *getDeclarationIfExists(const Module *M, ID id);

/// This version supports overloaded intrinsics.
///
/// \param M Module in which to look up the declaration.
/// \param id Intrinsic ID to look up.
/// \param OverloadTys Overload types that select the mangled name.
/// \param FT Optional function type used when resolving unnamed types.
/// \return The Function declaration if present; otherwise nullptr.
LLVM_ABI Function *getDeclarationIfExists(Module *M, ID id,
                                          ArrayRef<Type *> OverloadTys,
                                          FunctionType *FT = nullptr);

/// Map a Clang builtin name to an intrinsic ID.
///
/// \param TargetPrefix Target prefix for the builtin, or empty for generic.
/// \param BuiltinName Clang builtin name to map.
/// \return The corresponding intrinsic ID, or \c not_intrinsic if none.
LLVM_ABI ID getIntrinsicForClangBuiltin(StringRef TargetPrefix,
                                        StringRef BuiltinName);

/// Map a MS builtin name to an intrinsic ID.
///
/// \param TargetPrefix Target prefix for the builtin, or empty for generic.
/// \param BuiltinName MS builtin name to map.
/// \return The corresponding intrinsic ID, or \c not_intrinsic if none.
LLVM_ABI ID getIntrinsicForMSBuiltin(StringRef TargetPrefix,
                                     StringRef BuiltinName);

/// Returns true if the intrinsic ID is for one of the "Constrained
/// Floating-Point Intrinsics".
///
/// \param QID Intrinsic ID to classify.
/// \return True if \p QID is a constrained floating-point intrinsic.
LLVM_ABI bool isConstrainedFPIntrinsic(ID QID);

/// Returns true if the intrinsic ID is for one of the "Constrained
/// Floating-Point Intrinsics" that take rounding mode metadata.
///
/// \param QID Intrinsic ID to classify.
/// \return True if \p QID takes a rounding-mode metadata operand.
LLVM_ABI bool hasConstrainedFPRoundingModeOperand(ID QID);

/// This is a type descriptor which explains the type requirements of an
/// intrinsic. This is returned by getIntrinsicInfoTableEntries.
struct IITDescriptor {
  /// Tag for an entry in an intrinsic IIT (Intrinsic Instruction Type) table.
  enum IITDescriptorKind {
    // Concrete types. Additional qualifiers listed in comments.
    Void, ///< Void type.
    /// Marks remaining intrinsic arguments; consumed by \c getIntrinsicInfoTableEntries and omitted from its returned descriptor slice.
    VarArg,
    MMX, ///< X86 AMX tile type.
    Token, ///< Token type.
    Metadata, ///< Metadata type.
    Half, ///< IEEE half-precision (16-bit) floating-point type.
    BFloat, ///< Brain floating-point (16-bit) type.
    Float, ///< IEEE single-precision (32-bit) floating-point type.
    Double, ///< Double-precision floating-point type.
    Quad,   ///< Quad-precision (fp128) floating-point type.
    Integer, ///< Integer type; width in \c IntegerWidth.
    Vector,  ///< Vector type; width in \c VectorWidth.
    Pointer, ///< Pointer type; address space in \c PointerAddressSpace.
    Struct,  ///< Struct type; element count in \c StructNumElements.
    AMX, ///< X86 AMX type.
    PPCQuad, ///< PowerPC double-double (ppc_fp128) floating-point type.
    AArch64Svcount, ///< AArch64 \c aarch64.svcount target-extension type.
    WasmExternref, ///< WebAssembly \c wasm.externref target-extension type.
    WasmFuncref, ///< WebAssembly \c wasm.funcref target-extension type.

    // Overloaded type.
    /// Overloaded argument; AnyKind constraints and overload index in \c OverloadInfo.
    Overloaded,

    // Fully dependent types. Overload index in OverloadInfo.
    Match, ///< Must match the type of an earlier overloaded argument.
    Extend, ///< Extended integer type derived from an overloaded integer or vector.
    Trunc, ///< Truncated integer type derived from an overloaded integer or vector.
    OneNthEltsVec, ///< Vector with one nth the elements of an overloaded vector.
    SameVecWidth, ///< Same vector width as an overloaded vector type.
    VecElement, ///< Element type of an overloaded vector.
    Subdivide2, ///< Half the element count of an overloaded vector type.
    Subdivide4, ///< Quarter the element count of an overloaded vector type.
    VecOfBitcastsToInt, ///< Vector of integers bitcast from an overloaded vector.

    // Partially dependent types. Overload index (self and of the overload
    // type it depends on) in OverloadInfo.
    /// Vector of pointers with the same width as a reference overloaded vector and pointing to its element type.
    VecOfAnyPtrsToElt,

  } Kind;

  union {
    unsigned IntegerWidth; ///< Bit width for an \c Integer descriptor.
    unsigned PointerAddressSpace; ///< Address space for a \c Pointer descriptor.
    unsigned StructNumElements; ///< Element count for a \c Struct descriptor.
    unsigned OverloadInfo; ///< Packed overload index and constraint data.
    ElementCount VectorWidth; ///< Element count for a \c Vector descriptor.
  };

  // AnyKindVectorConstraint and AnyKindElementConstraint defined in
  // Intrinsics.td
#define GET_INTRINSIC_ANYKIND_ENUMS
#include "llvm/IR/IntrinsicEnums.inc"

  /// Return the packed overload index from \c OverloadInfo.
  ///
  /// \return The packed overload index.
  unsigned getOverloadIndex() const {
    assert(Kind == Overloaded || Kind == Match || Kind == Extend ||
           Kind == Trunc || Kind == SameVecWidth || Kind == VecElement ||
           Kind == Subdivide2 || Kind == Subdivide4 ||
           Kind == VecOfBitcastsToInt || Kind == VecOfAnyPtrsToElt ||
           Kind == OneNthEltsVec);
    // Overload index is packed into byte[0] of OverloadInfo.
    return OverloadInfo & 0xf;
  }

  /// Return the AnyKind vector and element constraints for an Overloaded entry.
  ///
  /// \return The AnyKind vector and element constraints.
  std::pair<AnyKindVectorConstraint, AnyKindElementConstraint>
  getOverloadConstraints() const {
    // Overload constraints are packed into byte[1] of OverloadInfo.
    assert(Kind == Overloaded);
    uint8_t AKEnumsPacked = OverloadInfo >> 8;
    AnyKindVectorConstraint VC = (AnyKindVectorConstraint)(AKEnumsPacked >> 4);
    AnyKindElementConstraint EC =
        (AnyKindElementConstraint)(AKEnumsPacked & 0xf);
    return {VC, EC};
  }

  // OneNthEltsVecArguments uses both a divisor N and a reference argument for
  // the full-width vector to match.
  /// Return the divisor N packed into a OneNthEltsVec descriptor.
  ///
  /// \return The divisor N from the OneNthEltsVec descriptor.
  unsigned getVectorDivisor() const {
    assert(Kind == OneNthEltsVec);
    return OverloadInfo >> 16;
  }

  /// Return the overload index of the referenced pointer-element type.
  ///
  /// \return The overload index of the referenced pointer-element type.
  unsigned getRefOverloadIndex() const {
    assert(Kind == VecOfAnyPtrsToElt);
    return OverloadInfo >> 16;
  }

  /// Create a descriptor with kind \p K and a single unsigned field.
  ///
  /// \param K Descriptor kind to construct.
  /// \param Field Value stored in the descriptor union.
  /// \return The constructed IITDescriptor.
  static IITDescriptor get(IITDescriptorKind K, unsigned Field) {
    IITDescriptor Result = {K, {Field}};
    return Result;
  }

  /// Create a descriptor with kind \p K and a 32-bit field from high/low halves.
  ///
  /// \param K Descriptor kind to construct.
  /// \param Hi High 16 bits of the packed field.
  /// \param Lo Low 16 bits of the packed field.
  /// \return The constructed IITDescriptor.
  static IITDescriptor get(IITDescriptorKind K, unsigned short Hi,
                           unsigned short Lo) {
    unsigned Field = Hi << 16 | Lo;
    IITDescriptor Result = {K, {Field}};
    return Result;
  }

  /// Create a Vector descriptor with the given width and scalability.
  ///
  /// \param Width Number of vector elements.
  /// \param IsScalable Whether the vector is scalable.
  /// \return The constructed Vector IITDescriptor.
  static IITDescriptor getVector(unsigned Width, bool IsScalable) {
    IITDescriptor Result = {Vector, {0}};
    Result.VectorWidth = ElementCount::get(Width, IsScalable);
    return Result;
  }
};

/// Returns true if \p id has a struct return type.
///
/// \param id Intrinsic ID to query.
/// \return True if \p id has a struct return type.
LLVM_ABI bool hasStructReturnType(ID id);

/// Fill the IIT table descriptors for an intrinsic into \p T.
///
/// Returns a tuple of 3 values:
///  - ArrayRef for the descriptor table (for convenience).
///  - Number of arguments.
///  - if it's a variable argument intrinsic.
///
/// Note that for VarArg intrinsics, the last IIT `VarArg` token will be
/// consumed and not a part of the returned ArrayRef.
///
/// \param id Intrinsic ID whose IIT descriptors are requested.
/// \param T Storage filled with the IIT descriptors for \p id.
/// \return Tuple of the descriptor ArrayRef, argument count, and whether the
///         intrinsic is VarArg.
LLVM_ABI std::tuple<ArrayRef<IITDescriptor>, unsigned, bool>
getIntrinsicInfoTableEntries(ID id, SmallVectorImpl<IITDescriptor> &T);

/// Returns true if \p FT is a valid function type for intrinsic \p ID. If
/// `ID` is an overloaded intrinsic, the overload types are pushed into the
/// OverloadTys vector.
///
/// Returns false if the given ID and function type combination is not a
/// valid intrinsic call. Also prints the error message to indicate the reason
/// of the mismatch to \p OS.
///
/// \param ID Intrinsic ID to validate against.
/// \param FT Function type to check.
/// \param OverloadTys Filled with overload types when \p ID is overloaded.
/// \param OS Stream that receives a mismatch diagnostic on failure.
/// \return True if \p FT is a valid signature for \p ID.
LLVM_ABI bool isSignatureValid(Intrinsic::ID ID, FunctionType *FT,
                               SmallVectorImpl<Type *> &OverloadTys,
                               raw_ostream &OS = nulls());

/// Same as previous, but accepts a Function instead of ID and FunctionType.
///
/// \param F Function whose intrinsic ID and type are validated.
/// \param OverloadTys Filled with overload types when the intrinsic is
///        overloaded.
/// \param OS Stream that receives a mismatch diagnostic on failure.
/// \return True if \p F has a valid intrinsic signature.
LLVM_ABI bool isSignatureValid(Function *F,
                               SmallVectorImpl<Type *> &OverloadTys,
                               raw_ostream &OS = nulls());

/// Remangle an intrinsic if its name does not match its signature.
///
/// Checks whether the intrinsic name matches its signature and, if not,
/// returns the declaration with the same signature and remangled name. An
/// existing GlobalValue with the wanted name but with a wrong prototype or of
/// the wrong kind will be renamed by adding ".renamed" to the name.
///
/// \param F Function whose name is checked against its signature.
/// \return The remangled Function when renaming was needed; otherwise
///         \c std::nullopt.
LLVM_ABI std::optional<Function *> remangleIntrinsicFunction(Function *F);

/// Returns the corresponding llvm.vector.interleaveN intrinsic for factor N.
///
/// \param Factor Interleave factor \c N selecting \c llvm.vector.interleaveN.
/// \return The intrinsic ID for \c llvm.vector.interleaveN.
LLVM_ABI Intrinsic::ID getInterleaveIntrinsicID(unsigned Factor);

/// Returns the corresponding llvm.vector.deinterleaveN intrinsic for factor
/// N.
///
/// \param Factor Deinterleave factor \c N selecting
///        \c llvm.vector.deinterleaveN.
/// \return The intrinsic ID for \c llvm.vector.deinterleaveN.
LLVM_ABI Intrinsic::ID getDeinterleaveIntrinsicID(unsigned Factor);

/// Print the argument info for the arguments with ArgInfo.
///
/// \param IID Intrinsic ID whose immediate argument is printed.
/// \param ArgIdx Zero-based index of the immediate argument.
/// \param OS Stream to write the pretty-printed argument to.
/// \param ImmArgVal Immediate constant value for the argument.
LLVM_ABI void printImmArg(ID IID, unsigned ArgIdx, raw_ostream &OS,
                          const Constant *ImmArgVal);

/// Print an immediate floating-point class mask constant.
///
/// \param OS Stream to write the pretty-printed mask to.
/// \param ImmArgVal Immediate constant encoding the FP class mask.
LLVM_ABI void printFPClassMask(raw_ostream &OS, const Constant *ImmArgVal);

} // namespace Intrinsic

} // namespace llvm

#endif // LLVM_IR_INTRINSICS_H
