//===- llvm/Attributes.h - Container for Attributes -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file contains the simple types necessary to represent the
/// attributes associated with functions and their calls.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_ATTRIBUTES_H
#define LLVM_IR_ATTRIBUTES_H

#include "llvm-c/Types.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ModRef.h"
#include "llvm/Support/PointerLikeTypeTraits.h"
#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace llvm {

class AttrBuilder;
class AttributeMask;
/// Internal uniquified storage for a single \c Attribute value.
class AttributeImpl;
/// Internal uniquified storage for an \c AttributeList.
class AttributeListImpl;
/// Internal uniquified storage for an \c AttributeSet.
class AttributeSetNode;
class ConstantRange;
/// Ordered list of disjoint constant ranges used by attributes such as
/// \c initializes.
class ConstantRangeList;
class FoldingSetNodeID;
class Function;
class LLVMContext;
class Instruction;
class Type;
class raw_ostream;
enum FPClassTest : unsigned;
struct DenormalFPEnv;
struct DenormalMode;

/// Bitmask describing the role and properties of an allocation function for
/// the \c allockind attribute (alloc, realloc, free, and returned memory state).
enum class AllocFnKind : uint64_t {
  Unknown = 0, ///< Unknown or unspecified allocation-function kind.
  Alloc = 1 << 0, ///< Allocator function returns a new allocation.
  Realloc = 1 << 1, ///< Allocator function resizes the \c allocptr argument
  Free = 1 << 2,          ///< Allocator function frees the \c allocptr argument.
  Uninitialized = 1 << 3, ///< Allocator function returns uninitialized memory.
  Zeroed = 1 << 4, ///< Allocator function returns zero-initialized memory
  Aligned = 1 << 5, ///< Allocator function aligns allocations per the
                    ///< `allocalign` argument
  LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */ Aligned) ///< Sentinel equal to the largest enumerator.
};

/// Describes how many bytes are dead when a pointer argument is returned from
/// a call. Used by the \c dead_on_return parameter attribute.
class DeadOnReturnInfo {
public:
  /// All reachable memory through the pointer is dead on return.
  DeadOnReturnInfo() : DeadBytes(std::nullopt) {}
  /// Only the first \p DeadOnReturnBytes of the pointed-to object are dead.
  /// \param DeadOnReturnBytes Number of leading bytes that become dead on return.
  DeadOnReturnInfo(uint64_t DeadOnReturnBytes) : DeadBytes(DeadOnReturnBytes) {}

  /// Return the number of bytes that are dead on return from the call.
  ///
  /// Requires that \ref coversAllReachableMemory() is false.
  /// \return The number of bytes that are dead on return from the call.
  uint64_t getNumberOfDeadBytes() const {
    assert(DeadBytes.has_value() &&
           "This attribute does not specify a byte count. Did you forget to "
           "check if the attribute covers all reachable memory?");
    return DeadBytes.value();
  }

  /// Return true if all memory reachable through the pointer is dead on return.
  /// \return true if all memory reachable through the pointer is dead on return.
  bool coversAllReachableMemory() const { return !DeadBytes.has_value(); }

  /// Decode a packed integer encoding of dead-on-return information.
  /// \param Data Encoded value; \c UINT64_MAX means all reachable memory is dead.
  /// \return Decoded dead-on-return information.
  static DeadOnReturnInfo createFromIntValue(uint64_t Data) {
    if (Data == std::numeric_limits<uint64_t>::max())
      return DeadOnReturnInfo();
    return DeadOnReturnInfo(Data);
  }

  /// Encode this info as an integer for attribute storage.
  /// \return An integer encoding of this dead-on-return information.
  uint64_t toIntValue() const {
    if (DeadBytes.has_value())
      return DeadBytes.value();
    return std::numeric_limits<uint64_t>::max();
  }

  /// Return true if zero bytes are marked dead on return.
  /// \return true if zero bytes are marked dead on return.
  bool isZeroSized() const {
    return DeadBytes.has_value() && DeadBytes.value() == 0;
  }

private:
  std::optional<uint64_t> DeadBytes;
};

//===----------------------------------------------------------------------===//
/// \class
/// Represents a single IR attribute on a function, parameter, or return value.
///
/// Functions, function parameters, and return types can have attributes
/// to indicate how they should be treated by optimizations and code
/// generation. It's light-weight and should be passed around by-value.
class Attribute {
public:
  /// This enumeration lists the attributes that can be associated with
  /// parameters, function results, or the function itself.
  ///
  /// Note: The `uwtable' attribute is about the ABI or the user mandating an
  /// entry in the unwind table. The `nounwind' attribute is about an exception
  /// passing by the function.
  ///
  /// In a theoretical system that uses tables for profiling and SjLj for
  /// exceptions, they would be fully independent. In a normal system that uses
  /// tables for both, the semantics are:
  ///
  /// nil                = Needs an entry because an exception might pass by.
  /// nounwind           = No need for an entry
  /// uwtable            = Needs an entry because the ABI says so and because
  ///                      an exception might pass by.
  /// uwtable + nounwind = Needs an entry because the ABI says so.

  enum AttrKind {
    // IR-Level Attributes
    None,                  ///< No attributes have been set
    #define GET_ATTR_ENUM
    #include "llvm/IR/Attributes.inc"
    EndAttrKinds,          ///< Sentinel value useful for loops
    EmptyKey,              ///< Use as Empty key for DenseMap of AttrKind
    TombstoneKey,          ///< Use as Tombstone key for DenseMap of AttrKind
  };

  /// Number of integer-valued attribute kinds.
  static const unsigned NumIntAttrKinds = LastIntAttr - FirstIntAttr + 1;
  /// Number of type-valued attribute kinds.
  static const unsigned NumTypeAttrKinds = LastTypeAttr - FirstTypeAttr + 1;

  /// Return true if \p Kind is an enum (flag) attribute kind.
  /// \param Kind Attribute kind to classify.
  /// \return true if \p Kind is an enum (flag) attribute kind.
  static bool isEnumAttrKind(AttrKind Kind) {
    return Kind >= FirstEnumAttr && Kind <= LastEnumAttr;
  }
  /// Return true if \p Kind is an integer-valued attribute kind.
  /// \param Kind Attribute kind to classify.
  /// \return true if \p Kind is an integer-valued attribute kind.
  static bool isIntAttrKind(AttrKind Kind) {
    return Kind >= FirstIntAttr && Kind <= LastIntAttr;
  }
  /// Return true if \p Kind is a type-valued attribute kind.
  /// \param Kind Attribute kind to classify.
  /// \return true if \p Kind is a type-valued attribute kind.
  static bool isTypeAttrKind(AttrKind Kind) {
    return Kind >= FirstTypeAttr && Kind <= LastTypeAttr;
  }
  /// Return true if \p Kind is a ConstantRange attribute kind.
  /// \param Kind Attribute kind to classify.
  /// \return true if \p Kind is a ConstantRange attribute kind.
  static bool isConstantRangeAttrKind(AttrKind Kind) {
    return Kind >= FirstConstantRangeAttr && Kind <= LastConstantRangeAttr;
  }
  /// Return true if \p Kind is a ConstantRangeList attribute kind.
  /// \param Kind Attribute kind to classify.
  /// \return true if \p Kind is a ConstantRangeList attribute kind.
  static bool isConstantRangeListAttrKind(AttrKind Kind) {
    return Kind >= FirstConstantRangeListAttr &&
           Kind <= LastConstantRangeListAttr;
  }

  /// Return true if \p Kind may be used as a function attribute.
  /// \param Kind Attribute kind to test.
  /// \return true if \p Kind may be used as a function attribute.
  LLVM_ABI static bool canUseAsFnAttr(AttrKind Kind);
  /// Return true if \p Kind may be used as a parameter attribute.
  /// \param Kind Attribute kind to test.
  /// \return true if \p Kind may be used as a parameter attribute.
  LLVM_ABI static bool canUseAsParamAttr(AttrKind Kind);
  /// Return true if \p Kind may be used as a return-value attribute.
  /// \param Kind Attribute kind to test.
  /// \return true if \p Kind may be used as a return-value attribute.
  LLVM_ABI static bool canUseAsRetAttr(AttrKind Kind);

  /// Return true if intersecting sets must preserve this attribute kind.
  /// \param Kind Attribute kind whose intersection rule is queried.
  /// \return true if intersecting sets must preserve this attribute kind.
  LLVM_ABI static bool intersectMustPreserve(AttrKind Kind);
  /// Return true if intersecting sets combine this kind with bitwise AND.
  /// \param Kind Attribute kind whose intersection rule is queried.
  /// \return true if intersecting sets combine this kind with bitwise AND.
  LLVM_ABI static bool intersectWithAnd(AttrKind Kind);
  /// Return true if intersecting sets combine this kind by taking the minimum.
  /// \param Kind Attribute kind whose intersection rule is queried.
  /// \return true if intersecting sets combine this kind by taking the minimum.
  LLVM_ABI static bool intersectWithMin(AttrKind Kind);
  /// Return true if intersecting two attribute sets with this kind requires a
  /// custom rule rather than bitwise \c and, minimum, or preservation.
  /// \param Kind Attribute kind whose intersection rule is queried.
  /// \return true if intersecting two attribute sets with this kind requires a custom rule rather than bitwise \c and, minimum, or preservation.
  LLVM_ABI static bool intersectWithCustom(AttrKind Kind);

private:
  AttributeImpl *pImpl = nullptr;

  Attribute(AttributeImpl *A) : pImpl(A) {}

public:
  /// Construct an empty attribute that compares equal to a default-constructed
  /// Attribute and has no kind or value.
  Attribute() = default;

  //===--------------------------------------------------------------------===//
  // Attribute Construction
  //===--------------------------------------------------------------------===//

  /// Return a uniquified Attribute object.
  /// \param Context Context used to uniquify the attribute.
  /// \param Kind Enum attribute kind.
  /// \param Val Optional integer payload for integer attributes.
  /// \return A uniquified Attribute object.
  LLVM_ABI static Attribute get(LLVMContext &Context, AttrKind Kind,
                                uint64_t Val = 0);
  /// Return a uniquified string attribute with kind \p Kind and value \p Val.
  /// \param Context Context used to uniquify the attribute.
  /// \param Kind Target-dependent attribute kind string.
  /// \param Val Attribute value string.
  /// \return A uniquified string attribute with kind \p Kind and value \p Val.
  LLVM_ABI static Attribute get(LLVMContext &Context, StringRef Kind,
                                StringRef Val = StringRef());
  /// Return a uniquified type attribute with kind \p Kind and type \p Ty.
  /// \param Context Context used to uniquify the attribute.
  /// \param Kind Type attribute kind.
  /// \param Ty Type payload for the attribute.
  /// \return A uniquified type attribute with kind \p Kind and type \p Ty.
  LLVM_ABI static Attribute get(LLVMContext &Context, AttrKind Kind, Type *Ty);
  /// Return a uniquified ConstantRange attribute with value \p CR.
  ///
  /// \p Kind must be a ConstantRange attribute kind and \p CR must not be the
  /// full set.
  /// \param Context Context used to uniquify the attribute.
  /// \param Kind ConstantRange attribute kind.
  /// \param CR Constant range payload.
  /// \return A uniquified ConstantRange attribute with value \p CR.
  LLVM_ABI static Attribute get(LLVMContext &Context, AttrKind Kind,
                                const ConstantRange &CR);
  /// Return a uniquified ConstantRangeList attribute with values \p Val.
  ///
  /// \p Kind must be a ConstantRangeList attribute kind.
  /// \param Context Context used to uniquify the attribute.
  /// \param Kind ConstantRangeList attribute kind.
  /// \param Val List of constant ranges.
  /// \return A uniquified ConstantRangeList attribute with values \p Val.
  LLVM_ABI static Attribute get(LLVMContext &Context, AttrKind Kind,
                                ArrayRef<ConstantRange> Val);

  /// Return a uniquified Attribute object that has the specific
  /// alignment set.
  /// \param Context Context used to uniquify the attribute.
  /// \param Alignment Required alignment.
  /// \return A uniquified Attribute object that has the specific alignment set.
  LLVM_ABI static Attribute getWithAlignment(LLVMContext &Context,
                                             Align Alignment);
  /// Return a uniquified \c alignstack attribute with stack alignment \p Alignment.
  /// \param Context Context used to uniquify the attribute.
  /// \param Alignment Required stack alignment.
  /// \return A uniquified \c alignstack attribute with stack alignment \p Alignment.
  LLVM_ABI static Attribute getWithStackAlignment(LLVMContext &Context,
                                                  Align Alignment);
  /// Return a uniquified \c dereferenceable attribute for \p Bytes bytes.
  /// \param Context Context used to uniquify the attribute.
  /// \param Bytes Number of dereferenceable bytes.
  /// \return A uniquified \c dereferenceable attribute for \p Bytes bytes.
  LLVM_ABI static Attribute getWithDereferenceableBytes(LLVMContext &Context,
                                                        uint64_t Bytes);
  /// Return a uniquified \c dereferenceable_or_null attribute for \p Bytes bytes.
  /// \param Context Context used to uniquify the attribute.
  /// \param Bytes Number of dereferenceable-or-null bytes.
  /// \return A uniquified \c dereferenceable_or_null attribute for \p Bytes bytes.
  LLVM_ABI static Attribute
  getWithDereferenceableOrNullBytes(LLVMContext &Context, uint64_t Bytes);
  /// Return a uniquified \c allocsize attribute for the given argument indexes.
  /// \param Context Context used to uniquify the attribute.
  /// \param ElemSizeArg Argument index holding the element size.
  /// \param NumElemsArg Optional argument index holding the element count.
  /// \return A uniquified \c allocsize attribute for the given argument indexes.
  LLVM_ABI static Attribute
  getWithAllocSizeArgs(LLVMContext &Context, unsigned ElemSizeArg,
                       const std::optional<unsigned> &NumElemsArg);
  /// Return a uniquified \c allockind attribute with allocator kind \p Kind.
  /// \param Context Context used to uniquify the attribute.
  /// \param Kind Allocator-function kind bitmask.
  /// \return A uniquified \c allockind attribute with allocator kind \p Kind.
  LLVM_ABI static Attribute getWithAllocKind(LLVMContext &Context,
                                             AllocFnKind Kind);
  /// Return a uniquified \c vscale_range attribute for the given min/max.
  /// \param Context Context used to uniquify the attribute.
  /// \param MinValue Minimum vscale.
  /// \param MaxValue Maximum vscale.
  /// \return A uniquified \c vscale_range attribute for the given min/max.
  LLVM_ABI static Attribute getWithVScaleRangeArgs(LLVMContext &Context,
                                                   unsigned MinValue,
                                                   unsigned MaxValue);
  /// Return a uniquified \c byval attribute carrying the structure type
  /// passed by value in the caller's stack frame.
  /// \param Context Context used to uniquify the attribute.
  /// \param Ty Structure type passed by value.
  /// \return A uniquified \c byval attribute carrying the structure type passed by value in the caller's stack frame.
  LLVM_ABI static Attribute getWithByValType(LLVMContext &Context, Type *Ty);
  /// Return a uniquified \c sret attribute carrying the structure return type.
  /// \param Context Context used to uniquify the attribute.
  /// \param Ty Structure return type.
  /// \return A uniquified \c sret attribute carrying the structure return type.
  LLVM_ABI static Attribute getWithStructRetType(LLVMContext &Context,
                                                 Type *Ty);
  /// Return a uniquified \c byref attribute carrying the referenced pointee
  /// type.
  /// \param Context Context used to uniquify the attribute.
  /// \param Ty Referenced pointee type.
  /// \return A uniquified \c byref attribute carrying the referenced pointee type.
  LLVM_ABI static Attribute getWithByRefType(LLVMContext &Context, Type *Ty);
  /// Return a uniquified \c preallocated attribute carrying the allocated type.
  /// \param Context Context used to uniquify the attribute.
  /// \param Ty Preallocated type.
  /// \return A uniquified \c preallocated attribute carrying the allocated type.
  LLVM_ABI static Attribute getWithPreallocatedType(LLVMContext &Context,
                                                    Type *Ty);
  /// Return a uniquified \c inalloca attribute carrying the allocated type.
  /// \param Context Context used to uniquify the attribute.
  /// \param Ty Inalloca type.
  /// \return A uniquified \c inalloca attribute carrying the allocated type.
  LLVM_ABI static Attribute getWithInAllocaType(LLVMContext &Context, Type *Ty);
  /// Return a uniquified \c uwtable attribute with unwind-table kind \p Kind.
  /// \param Context Context used to uniquify the attribute.
  /// \param Kind Unwind-table kind.
  /// \return A uniquified \c uwtable attribute with unwind-table kind \p Kind.
  LLVM_ABI static Attribute getWithUWTableKind(LLVMContext &Context,
                                               UWTableKind Kind);
  /// Return a uniquified \c memory attribute encoding effects \p ME.
  /// \param Context Context used to uniquify the attribute.
  /// \param ME Memory effects to encode.
  /// \return A uniquified \c memory attribute encoding effects \p ME.
  LLVM_ABI static Attribute getWithMemoryEffects(LLVMContext &Context,
                                                 MemoryEffects ME);
  /// Return a uniquified \c nofpclass attribute excluding classes in \p Mask.
  /// \param Context Context used to uniquify the attribute.
  /// \param Mask Disallowed floating-point classes.
  /// \return A uniquified \c nofpclass attribute excluding classes in \p Mask.
  LLVM_ABI static Attribute getWithNoFPClass(LLVMContext &Context,
                                             FPClassTest Mask);
  /// Return a uniquified \c dead_on_return attribute describing how many bytes
  /// of the pointed-to object become dead when the pointer is returned.
  /// \param Context Context used to uniquify the attribute.
  /// \param DI Dead-on-return byte information.
  /// \return A uniquified \c dead_on_return attribute describing how many bytes of the pointed-to object become dead when the pointer is returned.
  LLVM_ABI static Attribute getWithDeadOnReturnInfo(LLVMContext &Context,
                                                    DeadOnReturnInfo DI);
  /// Return a uniquified \c captures attribute with capture info \p CI.
  /// \param Context Context used to uniquify the attribute.
  /// \param CI Capture information to encode.
  /// \return A uniquified \c captures attribute with capture info \p CI.
  LLVM_ABI static Attribute getWithCaptureInfo(LLVMContext &Context,
                                               CaptureInfo CI);

  /// For a typed attribute, return the equivalent attribute with the type
  /// changed to \p ReplacementTy.
  /// \param Context Context used to uniquify the attribute.
  /// \param ReplacementTy Replacement type payload.
  /// \return An equivalent attribute with type \p ReplacementTy.
  Attribute getWithNewType(LLVMContext &Context, Type *ReplacementTy) {
    assert(isTypeAttribute() && "this requires a typed attribute");
    return get(Context, getKindAsEnum(), ReplacementTy);
  }

  /// Return the enum attribute kind corresponding to IR name \p AttrName.
  /// \param AttrName Attribute spelling such as \c "noalias".
  /// \return The enum attribute kind corresponding to IR name \p AttrName.
  LLVM_ABI static Attribute::AttrKind getAttrKindFromName(StringRef AttrName);

  /// Return the IR spelling of \p AttrKind (for example, \c noalias).
  /// \param AttrKind Enum attribute kind.
  /// \return The IR spelling of \p AttrKind (for example, \c noalias).
  LLVM_ABI static StringRef getNameFromAttrKind(Attribute::AttrKind AttrKind);

  /// Return true if the provided string matches the IR name of an attribute.
  ///
  /// For example, \c "noalias" returns true but not \c "NoAlias".
  /// \param Name Candidate attribute name string.
  /// \return true if the provided string matches the IR name of an attribute.
  LLVM_ABI static bool isExistingAttribute(StringRef Name);

  //===--------------------------------------------------------------------===//
  // Attribute Accessors
  //===--------------------------------------------------------------------===//

  /// Return true if the attribute is an Attribute::AttrKind type.
  /// \return true if the attribute is an Attribute::AttrKind type.
  LLVM_ABI bool isEnumAttribute() const;

  /// Return true if the attribute is an integer attribute.
  /// \return true if the attribute is an integer attribute.
  LLVM_ABI bool isIntAttribute() const;

  /// Return true if the attribute is a string (target-dependent)
  /// attribute.
  /// \return true if the attribute is a string (target-dependent) attribute.
  LLVM_ABI bool isStringAttribute() const;

  /// Return true if the attribute is a type attribute.
  /// \return true if the attribute is a type attribute.
  LLVM_ABI bool isTypeAttribute() const;

  /// Return true if the attribute is a ConstantRange attribute.
  /// \return true if the attribute is a ConstantRange attribute.
  LLVM_ABI bool isConstantRangeAttribute() const;

  /// Return true if the attribute is a ConstantRangeList attribute.
  /// \return true if the attribute is a ConstantRangeList attribute.
  LLVM_ABI bool isConstantRangeListAttribute() const;

  /// Return true if the attribute is any kind of attribute.
  /// \return true if the attribute is any kind of attribute.
  bool isValid() const { return pImpl; }

  /// Return true if the attribute is present.
  /// \param Val Enum attribute kind to test for.
  /// \return true if the attribute is present.
  LLVM_ABI bool hasAttribute(AttrKind Val) const;

  /// Return true if the target-dependent attribute is present.
  /// \param Val Target-dependent attribute kind string.
  /// \return true if the target-dependent attribute is present.
  LLVM_ABI bool hasAttribute(StringRef Val) const;

  /// Returns true if the attribute's kind can be represented as an enum (Enum,
  /// Integer, Type, ConstantRange, or ConstantRangeList attribute).
  /// \return true if the attribute's kind can be represented as an enum (Enum, Integer, Type, ConstantRange, or ConstantRangeList attribute).
  bool hasKindAsEnum() const { return !isStringAttribute(); }

  /// Return the attribute's kind as an enum (Attribute::AttrKind). This
  /// requires the attribute be representable as an enum (see: `hasKindAsEnum`).
  /// \return The attribute's kind as an enum (Attribute::AttrKind).
  LLVM_ABI Attribute::AttrKind getKindAsEnum() const;

  /// Return the attribute's value as an integer. This requires that the
  /// attribute be an integer attribute.
  /// \return The attribute's value as an integer.
  LLVM_ABI uint64_t getValueAsInt() const;

  /// Return the attribute's value as a boolean. This requires that the
  /// attribute be a string attribute.
  /// \return The attribute's value as a boolean.
  LLVM_ABI bool getValueAsBool() const;

  /// Return the attribute's kind as a string. This requires the
  /// attribute to be a string attribute.
  /// \return The attribute's kind as a string.
  LLVM_ABI StringRef getKindAsString() const;

  /// Return the attribute's value as a string. This requires the
  /// attribute to be a string attribute.
  /// \return The attribute's value as a string.
  LLVM_ABI StringRef getValueAsString() const;

  /// Return the attribute's value as a Type. This requires the attribute to be
  /// a type attribute.
  /// \return The attribute's value as a Type.
  LLVM_ABI Type *getValueAsType() const;

  /// Return the attribute's value as a ConstantRange. This requires the
  /// attribute to be a ConstantRange attribute.
  /// \return The attribute's value as a ConstantRange.
  LLVM_ABI const ConstantRange &getValueAsConstantRange() const;

  /// Return the attribute's value as a ConstantRange array. This requires the
  /// attribute to be a ConstantRangeList attribute.
  /// \return The attribute's value as a ConstantRange array.
  LLVM_ABI ArrayRef<ConstantRange> getValueAsConstantRangeList() const;

  /// Returns the alignment field of an attribute as a byte alignment
  /// value.
  /// \return The alignment field of an attribute as a byte alignment value.
  LLVM_ABI MaybeAlign getAlignment() const;

  /// Returns the stack alignment field of an attribute as a byte
  /// alignment value.
  /// \return The stack alignment field of an attribute as a byte alignment value.
  LLVM_ABI MaybeAlign getStackAlignment() const;

  /// Returns the number of dereferenceable bytes from the
  /// dereferenceable attribute.
  /// \return The number of dereferenceable bytes from the dereferenceable attribute.
  LLVM_ABI uint64_t getDereferenceableBytes() const;

  /// Return dead-on-return information from this attribute.
  ///
  /// Returns the number of dead_on_return bytes from the dead_on_return
  /// attribute, or std::nullopt if all memory reachable through the pointer is
  /// marked dead on return.
  /// \return Dead-on-return information from this attribute.
  LLVM_ABI DeadOnReturnInfo getDeadOnReturnInfo() const;

  /// Returns the number of dereferenceable_or_null bytes from the
  /// dereferenceable_or_null attribute.
  /// \return The number of dereferenceable_or_null bytes from the dereferenceable_or_null attribute.
  LLVM_ABI uint64_t getDereferenceableOrNullBytes() const;

  /// Returns the argument numbers for the allocsize attribute.
  /// \return The argument numbers for the allocsize attribute.
  LLVM_ABI std::pair<unsigned, std::optional<unsigned>>
  getAllocSizeArgs() const;

  /// Returns the minimum value for the vscale_range attribute.
  /// \return The minimum value for the vscale_range attribute.
  LLVM_ABI unsigned getVScaleRangeMin() const;

  /// Returns the maximum value for the vscale_range attribute or std::nullopt
  /// when unknown.
  /// \return The maximum value for the vscale_range attribute or std::nullopt when unknown.
  LLVM_ABI std::optional<unsigned> getVScaleRangeMax() const;

  /// Returns the unwind table kind.
  /// \return The unwind table kind.
  LLVM_ABI UWTableKind getUWTableKind() const;

  /// Returns the allocator function kind.
  /// \return The allocator function kind.
  LLVM_ABI AllocFnKind getAllocKind() const;

  /// Returns memory effects.
  /// \return The memory effects encoded in this attribute.
  LLVM_ABI MemoryEffects getMemoryEffects() const;

  /// Returns denormal_fpenv.
  /// \return The denormal floating-point environment from this attribute.
  LLVM_ABI struct DenormalFPEnv getDenormalFPEnv() const;

  /// Returns information from captures attribute.
  /// \return Capture information from this attribute.
  LLVM_ABI CaptureInfo getCaptureInfo() const;

  /// Return the FPClassTest for nofpclass
  /// \return The FPClassTest for nofpclass.
  LLVM_ABI FPClassTest getNoFPClass() const;

  /// Returns the value of the range attribute.
  /// \return The value of the range attribute.
  LLVM_ABI const ConstantRange &getRange() const;

  /// Returns the value of the initializes attribute.
  /// \return The value of the initializes attribute.
  LLVM_ABI ArrayRef<ConstantRange> getInitializes() const;

  /// The Attribute is converted to a string of equivalent mnemonic. This
  /// is, presumably, for writing out the mnemonics for the assembly writer.
  /// \param InAttrGrp True when formatting inside an attribute group.
  /// \return A string of equivalent mnemonic for the assembly writer.
  LLVM_ABI std::string getAsString(bool InAttrGrp = false) const;

  /// Return true if this attribute belongs to the LLVMContext.
  /// \param C Context that should own this attribute.
  /// \return true if this attribute belongs to the LLVMContext.
  LLVM_ABI bool hasParentContext(LLVMContext &C) const;

  /// Return true if both attributes refer to the same uniquified value.
  /// \param A Attribute to compare against.
  /// \return true if both attributes refer to the same uniquified value.
  bool operator==(Attribute A) const { return pImpl == A.pImpl; }
  /// Return true if the attributes refer to different uniquified values.
  /// \param A Attribute to compare against.
  /// \return true if the attributes refer to different uniquified values.
  bool operator!=(Attribute A) const { return pImpl != A.pImpl; }

  /// Used to sort attribute by kind.
  /// \param A Attribute whose kind is compared with this one.
  /// \return Negative, zero, or positive according to attribute kind order.
  LLVM_ABI int cmpKind(Attribute A) const;

  /// Less-than operator. Useful for sorting the attributes list.
  /// \param A Attribute to order against.
  /// \return true if this attribute sorts before \p A.
  LLVM_ABI bool operator<(Attribute A) const;

  /// Profile this attribute into \p ID for FoldingSet uniquing.
  /// \param ID Folding set node ID to update.
  LLVM_ABI void Profile(FoldingSetNodeID &ID) const;

  /// Return a raw pointer that uniquely identifies this attribute.
  /// \return A raw pointer that uniquely identifies this attribute.
  void *getRawPointer() const {
    return pImpl;
  }

  /// Get an attribute from a raw pointer created by getRawPointer.
  /// \param RawPtr Opaque pointer previously returned by getRawPointer.
  /// \return An Attribute reconstructed from \p RawPtr.
  static Attribute fromRawPointer(void *RawPtr) {
    return Attribute(reinterpret_cast<AttributeImpl*>(RawPtr));
  }
};

/// Wrap an Attribute as an opaque C API reference.
/// \param Attr Attribute to wrap.
/// \return An opaque C API reference for \p Attr.
inline LLVMAttributeRef wrap(Attribute Attr) {
  return reinterpret_cast<LLVMAttributeRef>(Attr.getRawPointer());
}

/// Unwrap an opaque C API attribute reference.
/// \param Attr Opaque C API attribute reference to unwrap.
/// \return The Attribute corresponding to \p Attr.
inline Attribute unwrap(LLVMAttributeRef Attr) {
  return Attribute::fromRawPointer(Attr);
}

//===----------------------------------------------------------------------===//
/// \class
/// Immutable set of attributes for one argument, parameter, function, or return.
///
/// This class holds the attributes for a particular argument, parameter,
/// function, or return value. It is an immutable value type that is cheap to
/// copy. Adding and removing enum attributes is intended to be fast, but adding
/// and removing string or integer attributes involves a FoldingSet lookup.
class AttributeSet {
  friend AttributeListImpl;
  template <typename Ty, typename Enable> friend struct DenseMapInfo;

  // TODO: Extract AvailableAttrs from AttributeSetNode and store them here.
  // This will allow an efficient implementation of addAttribute and
  // removeAttribute for enum attrs.

  /// Private implementation pointer.
  AttributeSetNode *SetNode = nullptr;

private:
  explicit AttributeSet(AttributeSetNode *ASN) : SetNode(ASN) {}

public:
  /// AttributeSet is a trivially copyable value type.
  AttributeSet() = default;
  /// Copy an attribute set; both refer to the same uniquified node.
  /// \param AS Attribute set to copy.
  AttributeSet(const AttributeSet &AS) = default;
  /// Destroy this attribute set (trivially; uniquified storage is owned elsewhere).
  ~AttributeSet() = default;

  /// Return a uniquified attribute set built from AttrBuilder \p B.
  /// \param C Context used to uniquify the set.
  /// \param B Builder providing the attributes.
  /// \return A uniquified attribute set built from AttrBuilder \p B.
  LLVM_ABI static AttributeSet get(LLVMContext &C, const AttrBuilder &B);
  /// Return a uniquified attribute set containing \p Attrs.
  /// \param C Context used to uniquify the set.
  /// \param Attrs Attributes to include.
  /// \return A uniquified attribute set containing \p Attrs.
  LLVM_ABI static AttributeSet get(LLVMContext &C, ArrayRef<Attribute> Attrs);

  /// Return true if both sets refer to the same uniquified node.
  /// \param O Attribute set to compare against.
  /// \return true if both sets refer to the same uniquified node.
  bool operator==(const AttributeSet &O) const { return SetNode == O.SetNode; }
  /// Return true if the sets refer to different uniquified nodes.
  /// \param O Attribute set to compare against.
  /// \return true if the sets refer to different uniquified nodes.
  bool operator!=(const AttributeSet &O) const { return !(*this == O); }

  /// Add an argument attribute. Returns a new set because attribute sets are
  /// immutable.
  /// \param C Context used to uniquify the result.
  /// \param Kind Enum attribute kind to add.
  /// \return A new attribute set with the attributes added.
  [[nodiscard]] LLVM_ABI AttributeSet
  addAttribute(LLVMContext &C, Attribute::AttrKind Kind) const;

  /// Add a target-dependent attribute. Returns a new set because attribute sets
  /// are immutable.
  /// \param C Context used to uniquify the result.
  /// \param Kind Target-dependent attribute kind.
  /// \param Value Attribute value string.
  /// \return A new attribute set with the attributes added.
  [[nodiscard]] LLVM_ABI AttributeSet addAttribute(
      LLVMContext &C, StringRef Kind, StringRef Value = StringRef()) const;

  /// Add attributes to the attribute set. Returns a new set because attribute
  /// sets are immutable.
  /// \param C Context used to uniquify the result.
  /// \param AS Attributes to merge in.
  /// \return A new attribute set with the attributes added.
  [[nodiscard]] LLVM_ABI AttributeSet addAttributes(LLVMContext &C,
                                                    AttributeSet AS) const;

  /// Add attributes to the attribute set. Returns a new set because attribute
  /// sets are immutable.
  /// \param C Context used to uniquify the result.
  /// \param B Builder providing attributes to merge in.
  /// \return A new attribute set with the attributes added.
  LLVM_ABI AttributeSet addAttributes(LLVMContext &C,
                                      const AttrBuilder &B) const;

  /// Remove the specified attribute from this set. Returns a new set because
  /// attribute sets are immutable.
  /// \param C Context used to uniquify the result.
  /// \param Kind Enum attribute kind to remove.
  /// \return A new attribute set with the attributes removed.
  [[nodiscard]] LLVM_ABI AttributeSet
  removeAttribute(LLVMContext &C, Attribute::AttrKind Kind) const;

  /// Remove the specified attribute from this set. Returns a new set because
  /// attribute sets are immutable.
  /// \param C Context used to uniquify the result.
  /// \param Kind Target-dependent attribute kind to remove.
  /// \return A new attribute set with the attributes removed.
  [[nodiscard]] LLVM_ABI AttributeSet removeAttribute(LLVMContext &C,
                                                      StringRef Kind) const;

  /// Remove the specified attributes from this set. Returns a new set because
  /// attribute sets are immutable.
  /// \param C Context used to uniquify the result.
  /// \param AttrsToRemove Mask of attributes to remove.
  /// \return A new attribute set with the attributes removed.
  [[nodiscard]] LLVM_ABI AttributeSet
  removeAttributes(LLVMContext &C, const AttributeMask &AttrsToRemove) const;

  /// Try to intersect this AttributeSet with Other. Returns std::nullopt if
  /// the two lists are inherently incompatible (imply different behavior, not
  /// just analysis).
  /// \param C Context used to uniquify the result.
  /// \param Other Attribute set to intersect with.
  /// \return The intersection, or std::nullopt if the sets are incompatible.
  [[nodiscard]] LLVM_ABI std::optional<AttributeSet>
  intersectWith(LLVMContext &C, AttributeSet Other) const;

  /// Return the number of attributes in this set.
  /// \return The number of attributes in this set.
  LLVM_ABI unsigned getNumAttributes() const;

  /// Return true if attributes exists in this set.
  /// \return true if attributes exists in this set.
  bool hasAttributes() const { return SetNode != nullptr; }

  /// Return true if the attribute exists in this set.
  /// \param Kind Enum attribute kind to look up.
  /// \return true if the attribute exists in this set.
  LLVM_ABI bool hasAttribute(Attribute::AttrKind Kind) const;

  /// Return true if the attribute exists in this set.
  /// \param Kind Target-dependent attribute kind to look up.
  /// \return true if the attribute exists in this set.
  LLVM_ABI bool hasAttribute(StringRef Kind) const;

  /// Return the attribute object.
  /// \param Kind Enum attribute kind to look up.
  /// \return The attribute object.
  LLVM_ABI Attribute getAttribute(Attribute::AttrKind Kind) const;

  /// Return the target-dependent attribute object.
  /// \param Kind Target-dependent attribute kind to look up.
  /// \return The target-dependent attribute object.
  LLVM_ABI Attribute getAttribute(StringRef Kind) const;

  /// Return the alignment from this attribute set, if present.
  /// \return The alignment from this attribute set, if present.
  LLVM_ABI MaybeAlign getAlignment() const;
  /// Return the stack alignment from this attribute set, if present.
  /// \return The stack alignment from this attribute set, if present.
  LLVM_ABI MaybeAlign getStackAlignment() const;
  /// Return the byte count from \c dereferenceable, or 0 if absent.
  /// \return The byte count from \c dereferenceable, or 0 if absent.
  LLVM_ABI uint64_t getDereferenceableBytes() const;
  /// Return dead-on-return information from this attribute set.
  /// \return Dead-on-return information from this attribute set.
  LLVM_ABI DeadOnReturnInfo getDeadOnReturnInfo() const;
  /// Return the byte count from \c dereferenceable_or_null, or 0 if absent.
  /// \return The byte count from \c dereferenceable_or_null, or 0 if absent.
  LLVM_ABI uint64_t getDereferenceableOrNullBytes() const;
  /// Return the type for the \c byval attribute, or null if absent.
  /// \return The type for the \c byval attribute, or null if absent.
  LLVM_ABI Type *getByValType() const;
  /// Return the structure type for the \c sret attribute, or null if absent.
  /// \return The structure type for the \c sret attribute, or null if absent.
  LLVM_ABI Type *getStructRetType() const;
  /// Return the type for the \c byref attribute, or null if absent.
  /// \return The type for the \c byref attribute, or null if absent.
  LLVM_ABI Type *getByRefType() const;
  /// Return the type for the \c preallocated attribute, or null if absent.
  /// \return The type for the \c preallocated attribute, or null if absent.
  LLVM_ABI Type *getPreallocatedType() const;
  /// Return the type for the \c inalloca attribute, or null if absent.
  /// \return The type for the \c inalloca attribute, or null if absent.
  LLVM_ABI Type *getInAllocaType() const;
  /// Return the type for the \c elementtype attribute, or null if absent.
  /// \return The type for the \c elementtype attribute, or null if absent.
  LLVM_ABI Type *getElementType() const;
  /// Return the \c allocsize argument indexes, if present.
  /// \return The \c allocsize argument indexes, if present.
  LLVM_ABI std::optional<std::pair<unsigned, std::optional<unsigned>>>
  getAllocSizeArgs() const;
  /// Return the minimum vscale for the \c vscale_range attribute, or 1 if
  /// absent.
  /// \return The minimum vscale for the \c vscale_range attribute, or 1 if absent.
  LLVM_ABI unsigned getVScaleRangeMin() const;
  /// Return the maximum vscale from \c vscale_range, or nullopt if unbounded or
  /// absent.
  /// \return The maximum vscale from \c vscale_range, or nullopt if unbounded or absent.
  LLVM_ABI std::optional<unsigned> getVScaleRangeMax() const;
  /// Return the unwind table kind from this attribute set.
  /// \return The unwind table kind from this attribute set.
  LLVM_ABI UWTableKind getUWTableKind() const;
  /// Return the \c allockind bitmask describing this allocation function.
  /// \return The \c allockind bitmask describing this allocation function.
  LLVM_ABI AllocFnKind getAllocKind() const;
  /// Return the memory effects encoded in this attribute set.
  /// \return The memory effects encoded in this attribute set.
  LLVM_ABI MemoryEffects getMemoryEffects() const;
  /// Return capture information from this attribute set.
  /// \return Capture information from this attribute set.
  LLVM_ABI CaptureInfo getCaptureInfo() const;
  /// Return the \c nofpclass mask from this attribute set.
  /// \return The \c nofpclass mask from this attribute set.
  LLVM_ABI FPClassTest getNoFPClass() const;
  /// Return a textual IR representation of the attributes in this set.
  /// \param InAttrGrp True when formatting inside an attribute group.
  /// \return A textual IR representation of the attributes in this set.
  LLVM_ABI std::string getAsString(bool InAttrGrp = false) const;

  /// Return true if this attribute set belongs to the LLVMContext.
  /// \param C Context that should own this attribute set.
  /// \return true if this attribute set belongs to the LLVMContext.
  LLVM_ABI bool hasParentContext(LLVMContext &C) const;

  /// Iterator over attributes in this set.
  using iterator = const Attribute *;

  /// Return an iterator to the first attribute in this set.
  /// \return An iterator to the first attribute in this set.
  LLVM_ABI iterator begin() const;
  /// Return an iterator past the last attribute in this set.
  /// \return An iterator past the last attribute in this set.
  LLVM_ABI iterator end() const;
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Dump this attribute set to stderr (for debugging).
  void dump() const;
#endif
};

//===----------------------------------------------------------------------===//
/// \class
/// Provide DenseMapInfo for AttributeSet.
template <> struct DenseMapInfo<AttributeSet, void> {
  /// Return a hash value for attribute set \p AS.
  /// \param AS Attribute set to hash.
  /// \return A hash value for attribute set \p AS.
  static unsigned getHashValue(AttributeSet AS) {
    return DenseMapInfo<const void *>::getHashValue(AS.SetNode);
  }

  /// Return true if \p LHS and \p RHS refer to the same attribute set.
  /// \param LHS First attribute set.
  /// \param RHS Second attribute set.
  /// \return true if \p LHS and \p RHS refer to the same attribute set.
  static bool isEqual(AttributeSet LHS, AttributeSet RHS) { return LHS == RHS; }
};

//===----------------------------------------------------------------------===//
/// \class
/// Holds attributes for a function, its return value, and its parameters.
///
/// You access the attributes for each of them via an index into
/// the AttributeList object. The function attributes are at index
/// `AttributeList::FunctionIndex', the return value is at index
/// `AttributeList::ReturnIndex', and the attributes for the parameters start at
/// index `AttributeList::FirstArgIndex'.
class AttributeList {
public:
  /// Indices used to address attribute sets within an \c AttributeList.
  enum AttrIndex : unsigned {
    ReturnIndex = 0U, ///< Index of the return-value attribute set.
    FunctionIndex = ~0U, ///< Index of the function attribute set.
    FirstArgIndex = 1, ///< Index of the first parameter attribute set.
  };

private:
  friend class AttrBuilder;
  friend class AttributeListImpl;
  friend class AttributeSet;
  friend class AttributeSetNode;
  template <typename Ty, typename Enable> friend struct DenseMapInfo;

  /// The attributes that we are managing. This can be null to represent
  /// the empty attributes list.
  AttributeListImpl *pImpl = nullptr;

public:
  /// Create an AttributeList with the specified parameters in it.
  /// \param C Context used to uniquify the list.
  /// \param Attrs Index/attribute pairs to include.
  /// \return An AttributeList with the specified parameters.
  LLVM_ABI static AttributeList
  get(LLVMContext &C, ArrayRef<std::pair<unsigned, Attribute>> Attrs);
  /// Create an AttributeList from index/attribute-set pairs.
  /// \param C Context used to uniquify the list.
  /// \param Attrs Index/attribute-set pairs to include.
  /// \return An AttributeList from the given index/attribute-set pairs.
  LLVM_ABI static AttributeList
  get(LLVMContext &C, ArrayRef<std::pair<unsigned, AttributeSet>> Attrs);

  /// Create an AttributeList from attribute sets for a function, its
  /// return value, and all of its arguments.
  /// \param C Context used to uniquify the list.
  /// \param FnAttrs Function attributes.
  /// \param RetAttrs Return-value attributes.
  /// \param ArgAttrs Per-argument attribute sets.
  /// \return An AttributeList for the function, return value, and arguments.
  LLVM_ABI static AttributeList get(LLVMContext &C, AttributeSet FnAttrs,
                                    AttributeSet RetAttrs,
                                    ArrayRef<AttributeSet> ArgAttrs);

private:
  explicit AttributeList(AttributeListImpl *LI) : pImpl(LI) {}

  static AttributeList getImpl(LLVMContext &C, ArrayRef<AttributeSet> AttrSets);

public:
  /// Construct an empty attribute list.
  AttributeList() = default;

  //===--------------------------------------------------------------------===//
  // AttributeList Construction and Mutation
  //===--------------------------------------------------------------------===//

  /// Return an AttributeList with the specified parameters in it.
  /// \param C Context used to uniquify the list.
  /// \param Attrs Attribute lists to merge.
  /// \return An AttributeList with the specified parameters in it.
  LLVM_ABI static AttributeList get(LLVMContext &C,
                                    ArrayRef<AttributeList> Attrs);
  /// Return an AttributeList with the given enum attribute kinds at \p Index.
  /// \param C Context used to uniquify the list.
  /// \param Index Attribute-set index (function, return, or argument).
  /// \param Kinds Enum attribute kinds to place at \p Index.
  /// \return An AttributeList with the given enum attribute kinds at \p Index.
  LLVM_ABI static AttributeList get(LLVMContext &C, unsigned Index,
                                    ArrayRef<Attribute::AttrKind> Kinds);
  /// Return an AttributeList with enum attribute kinds and matching integer
  /// values at \p Index.
  /// \param C Context used to uniquify the list.
  /// \param Index Attribute-set index (function, return, or argument).
  /// \param Kinds Enum attribute kinds.
  /// \param Values Integer values matching \p Kinds.
  /// \return An AttributeList with enum attribute kinds and matching integer values at \p Index.
  LLVM_ABI static AttributeList get(LLVMContext &C, unsigned Index,
                                    ArrayRef<Attribute::AttrKind> Kinds,
                                    ArrayRef<uint64_t> Values);
  /// Return an AttributeList with the given string attribute kinds at \p Index.
  /// \param C Context used to uniquify the list.
  /// \param Index Attribute-set index (function, return, or argument).
  /// \param Kind String attribute kinds to place at \p Index.
  /// \return An AttributeList with the given string attribute kinds at \p Index.
  LLVM_ABI static AttributeList get(LLVMContext &C, unsigned Index,
                                    ArrayRef<StringRef> Kind);
  /// Return an AttributeList containing \p Attrs at \p Index.
  /// \param C Context used to uniquify the list.
  /// \param Index Attribute-set index (function, return, or argument).
  /// \param Attrs Attribute set to place at \p Index.
  /// \return An AttributeList containing \p Attrs at \p Index.
  LLVM_ABI static AttributeList get(LLVMContext &C, unsigned Index,
                                    AttributeSet Attrs);
  /// Return an AttributeList containing the attributes from \p B at \p Index.
  /// \param C Context used to uniquify the list.
  /// \param Index Attribute-set index (function, return, or argument).
  /// \param B Builder providing attributes for \p Index.
  /// \return An AttributeList containing the attributes from \p B at \p Index.
  LLVM_ABI static AttributeList get(LLVMContext &C, unsigned Index,
                                    const AttrBuilder &B);

  /// Set the attribute set at the given index.
  /// Returns a new list because attribute lists are immutable.
  /// \param C Context used to uniquify the result.
  /// \param Index Attribute-set index to replace.
  /// \param Attrs Replacement attribute set.
  /// \return A new attribute list with the attribute set replaced.
  [[nodiscard]] LLVM_ABI AttributeList setAttributesAtIndex(
      LLVMContext &C, unsigned Index, AttributeSet Attrs) const;

  // TODO: remove non-AtIndex versions of these methods.
  /// Add an attribute to the attribute set at the given index.
  /// Returns a new list because attribute lists are immutable.
  /// \param C Context used to uniquify the result.
  /// \param Index Attribute-set index to modify.
  /// \param Kind Enum attribute kind to add.
  /// \return A new attribute list with the attributes added.
  [[nodiscard]] LLVM_ABI AttributeList addAttributeAtIndex(
      LLVMContext &C, unsigned Index, Attribute::AttrKind Kind) const;

  /// Add an attribute to the attribute set at the given index.
  /// Returns a new list because attribute lists are immutable.
  /// \param C Context used to uniquify the result.
  /// \param Index Attribute-set index to modify.
  /// \param Kind Target-dependent attribute kind.
  /// \param Value Attribute value string.
  /// \return A new attribute list with the attributes added.
  [[nodiscard]] LLVM_ABI AttributeList
  addAttributeAtIndex(LLVMContext &C, unsigned Index, StringRef Kind,
                      StringRef Value = StringRef()) const;

  /// Add an attribute to the attribute set at the given index.
  /// Returns a new list because attribute lists are immutable.
  /// \param C Context used to uniquify the result.
  /// \param Index Attribute-set index to modify.
  /// \param A Attribute to add.
  /// \return A new attribute list with the attributes added.
  [[nodiscard]] LLVM_ABI AttributeList addAttributeAtIndex(LLVMContext &C,
                                                           unsigned Index,
                                                           Attribute A) const;

  /// Add attributes to the attribute set at the given index.
  /// Returns a new list because attribute lists are immutable.
  /// \param C Context used to uniquify the result.
  /// \param Index Attribute-set index to modify.
  /// \param B Builder providing attributes to add.
  /// \return A new attribute list with the attributes added.
  [[nodiscard]] LLVM_ABI AttributeList addAttributesAtIndex(
      LLVMContext &C, unsigned Index, const AttrBuilder &B) const;

  /// Add a function attribute to the list. Returns a new list because
  /// attribute lists are immutable.
  /// \param C Context used to uniquify the result.
  /// \param Kind Enum attribute kind to add.
  /// \return A new attribute list with the attributes added.
  [[nodiscard]] AttributeList addFnAttribute(LLVMContext &C,
                                             Attribute::AttrKind Kind) const {
    return addAttributeAtIndex(C, FunctionIndex, Kind);
  }

  /// Add a function attribute to the list. Returns a new list because
  /// attribute lists are immutable.
  /// \param C Context used to uniquify the result.
  /// \param Attr Attribute to add at the function index.
  /// \return A new attribute list with the attributes added.
  [[nodiscard]] AttributeList addFnAttribute(LLVMContext &C,
                                             Attribute Attr) const {
    return addAttributeAtIndex(C, FunctionIndex, Attr);
  }

  /// Add a function attribute to the list. Returns a new list because
  /// attribute lists are immutable.
  /// \param C Context used to uniquify the result.
  /// \param Kind Target-dependent attribute kind.
  /// \param Value Attribute value string.
  /// \return A new attribute list with the attributes added.
  [[nodiscard]] AttributeList
  addFnAttribute(LLVMContext &C, StringRef Kind,
                 StringRef Value = StringRef()) const {
    return addAttributeAtIndex(C, FunctionIndex, Kind, Value);
  }

  /// Add function attribute to the list. Returns a new list because
  /// attribute lists are immutable.
  /// \param C Context used to uniquify the result.
  /// \param B Builder providing function attributes to add.
  /// \return A new attribute list with the attributes added.
  [[nodiscard]] AttributeList addFnAttributes(LLVMContext &C,
                                              const AttrBuilder &B) const {
    return addAttributesAtIndex(C, FunctionIndex, B);
  }

  /// Add a return value attribute to the list. Returns a new list because
  /// attribute lists are immutable.
  /// \param C Context used to uniquify the result.
  /// \param Kind Enum attribute kind to add.
  /// \return A new attribute list with the attributes added.
  [[nodiscard]] AttributeList addRetAttribute(LLVMContext &C,
                                              Attribute::AttrKind Kind) const {
    return addAttributeAtIndex(C, ReturnIndex, Kind);
  }

  /// Add a return value attribute to the list. Returns a new list because
  /// attribute lists are immutable.
  /// \param C Context used to uniquify the result.
  /// \param Attr Attribute to add at the return index.
  /// \return A new attribute list with the attributes added.
  [[nodiscard]] AttributeList addRetAttribute(LLVMContext &C,
                                              Attribute Attr) const {
    return addAttributeAtIndex(C, ReturnIndex, Attr);
  }

  /// Add a return value attribute to the list. Returns a new list because
  /// attribute lists are immutable.
  /// \param C Context used to uniquify the result.
  /// \param B Builder providing return attributes to add.
  /// \return A new attribute list with the attributes added.
  [[nodiscard]] AttributeList addRetAttributes(LLVMContext &C,
                                               const AttrBuilder &B) const {
    return addAttributesAtIndex(C, ReturnIndex, B);
  }

  /// Add an argument attribute to the list. Returns a new list because
  /// attribute lists are immutable.
  /// \param C Context used to uniquify the result.
  /// \param ArgNo Zero-based argument index.
  /// \param Kind Enum attribute kind to add.
  /// \return A new attribute list with the attributes added.
  [[nodiscard]] AttributeList
  addParamAttribute(LLVMContext &C, unsigned ArgNo,
                    Attribute::AttrKind Kind) const {
    return addAttributeAtIndex(C, ArgNo + FirstArgIndex, Kind);
  }

  /// Add an argument attribute to the list. Returns a new list because
  /// attribute lists are immutable.
  /// \param C Context used to uniquify the result.
  /// \param ArgNo Zero-based argument index.
  /// \param Kind Target-dependent attribute kind.
  /// \param Value Attribute value string.
  /// \return A new attribute list with the attributes added.
  [[nodiscard]] AttributeList
  addParamAttribute(LLVMContext &C, unsigned ArgNo, StringRef Kind,
                    StringRef Value = StringRef()) const {
    return addAttributeAtIndex(C, ArgNo + FirstArgIndex, Kind, Value);
  }

  /// Add an attribute to the attribute list at the given arg indices. Returns a
  /// new list because attribute lists are immutable.
  /// \param C Context used to uniquify the result.
  /// \param ArgNos Zero-based argument indexes to update.
  /// \param A Attribute to add at each index.
  /// \return A new attribute list with the attributes added.
  [[nodiscard]] LLVM_ABI AttributeList addParamAttribute(
      LLVMContext &C, ArrayRef<unsigned> ArgNos, Attribute A) const;

  /// Add an argument attribute to the list. Returns a new list because
  /// attribute lists are immutable.
  /// \param C Context used to uniquify the result.
  /// \param ArgNo Zero-based argument index.
  /// \param B Builder providing parameter attributes to add.
  /// \return A new attribute list with the attributes added.
  [[nodiscard]] AttributeList addParamAttributes(LLVMContext &C, unsigned ArgNo,
                                                 const AttrBuilder &B) const {
    return addAttributesAtIndex(C, ArgNo + FirstArgIndex, B);
  }

  /// Remove the specified attribute at the specified index from this
  /// attribute list. Returns a new list because attribute lists are immutable.
  /// \param C Context used to uniquify the result.
  /// \param Index Attribute-set index to modify.
  /// \param Kind Enum attribute kind to remove.
  /// \return A new attribute list with the attributes removed.
  [[nodiscard]] LLVM_ABI AttributeList removeAttributeAtIndex(
      LLVMContext &C, unsigned Index, Attribute::AttrKind Kind) const;

  /// Remove the specified attribute at the specified index from this
  /// attribute list. Returns a new list because attribute lists are immutable.
  /// \param C Context used to uniquify the result.
  /// \param Index Attribute-set index to modify.
  /// \param Kind Target-dependent attribute kind to remove.
  /// \return A new attribute list with the attributes removed.
  [[nodiscard]] LLVM_ABI AttributeList
  removeAttributeAtIndex(LLVMContext &C, unsigned Index, StringRef Kind) const;
  /// Remove the specified attribute at the specified index from this
  /// attribute list. Returns a new list because attribute lists are immutable.
  /// \param C Context used to uniquify the result.
  /// \param Index Attribute-set index to modify.
  /// \param Kind Target-dependent attribute kind to remove.
  /// \return A new attribute list with the attributes removed.
  [[nodiscard]] AttributeList removeAttribute(LLVMContext &C, unsigned Index,
                                              StringRef Kind) const {
    return removeAttributeAtIndex(C, Index, Kind);
  }

  /// Remove the specified attributes at the specified index from this
  /// attribute list. Returns a new list because attribute lists are immutable.
  /// \param C Context used to uniquify the result.
  /// \param Index Attribute-set index to modify.
  /// \param AttrsToRemove Mask of attributes to remove.
  /// \return A new attribute list with the attributes removed.
  [[nodiscard]] LLVM_ABI AttributeList removeAttributesAtIndex(
      LLVMContext &C, unsigned Index, const AttributeMask &AttrsToRemove) const;

  /// Remove all attributes at the specified index from this
  /// attribute list. Returns a new list because attribute lists are immutable.
  /// \param C Context used to uniquify the result.
  /// \param Index Attribute-set index to clear.
  /// \return A new attribute list with the attributes removed.
  [[nodiscard]] LLVM_ABI AttributeList
  removeAttributesAtIndex(LLVMContext &C, unsigned Index) const;

  /// Remove the specified attribute at the function index from this
  /// attribute list. Returns a new list because attribute lists are immutable.
  /// \param C Context used to uniquify the result.
  /// \param Kind Enum attribute kind to remove.
  /// \return A new attribute list with the attributes removed.
  [[nodiscard]] AttributeList
  removeFnAttribute(LLVMContext &C, Attribute::AttrKind Kind) const {
    return removeAttributeAtIndex(C, FunctionIndex, Kind);
  }

  /// Remove the specified attribute at the function index from this
  /// attribute list. Returns a new list because attribute lists are immutable.
  /// \param C Context used to uniquify the result.
  /// \param Kind Target-dependent attribute kind to remove.
  /// \return A new attribute list with the attributes removed.
  [[nodiscard]] AttributeList removeFnAttribute(LLVMContext &C,
                                                StringRef Kind) const {
    return removeAttributeAtIndex(C, FunctionIndex, Kind);
  }

  /// Remove the specified attribute at the function index from this
  /// attribute list. Returns a new list because attribute lists are immutable.
  /// \param C Context used to uniquify the result.
  /// \param AttrsToRemove Mask of attributes to remove.
  /// \return A new attribute list with the attributes removed.
  [[nodiscard]] AttributeList
  removeFnAttributes(LLVMContext &C, const AttributeMask &AttrsToRemove) const {
    return removeAttributesAtIndex(C, FunctionIndex, AttrsToRemove);
  }

  /// Remove the attributes at the function index from this
  /// attribute list. Returns a new list because attribute lists are immutable.
  /// \param C Context used to uniquify the result.
  /// \return A new attribute list with the attributes removed.
  [[nodiscard]] AttributeList removeFnAttributes(LLVMContext &C) const {
    return removeAttributesAtIndex(C, FunctionIndex);
  }

  /// Remove the specified attribute at the return value index from this
  /// attribute list. Returns a new list because attribute lists are immutable.
  /// \param C Context used to uniquify the result.
  /// \param Kind Enum attribute kind to remove.
  /// \return A new attribute list with the attributes removed.
  [[nodiscard]] AttributeList
  removeRetAttribute(LLVMContext &C, Attribute::AttrKind Kind) const {
    return removeAttributeAtIndex(C, ReturnIndex, Kind);
  }

  /// Remove the specified attribute at the return value index from this
  /// attribute list. Returns a new list because attribute lists are immutable.
  /// \param C Context used to uniquify the result.
  /// \param Kind Target-dependent attribute kind to remove.
  /// \return A new attribute list with the attributes removed.
  [[nodiscard]] AttributeList removeRetAttribute(LLVMContext &C,
                                                 StringRef Kind) const {
    return removeAttributeAtIndex(C, ReturnIndex, Kind);
  }

  /// Remove the specified attribute at the return value index from this
  /// attribute list. Returns a new list because attribute lists are immutable.
  /// \param C Context used to uniquify the result.
  /// \param AttrsToRemove Mask of attributes to remove.
  /// \return A new attribute list with the attributes removed.
  [[nodiscard]] AttributeList
  removeRetAttributes(LLVMContext &C,
                      const AttributeMask &AttrsToRemove) const {
    return removeAttributesAtIndex(C, ReturnIndex, AttrsToRemove);
  }

  /// Remove the specified attribute at the specified arg index from this
  /// attribute list. Returns a new list because attribute lists are immutable.
  /// \param C Context used to uniquify the result.
  /// \param ArgNo Zero-based argument index.
  /// \param Kind Enum attribute kind to remove.
  /// \return A new attribute list with the attributes removed.
  [[nodiscard]] AttributeList
  removeParamAttribute(LLVMContext &C, unsigned ArgNo,
                       Attribute::AttrKind Kind) const {
    return removeAttributeAtIndex(C, ArgNo + FirstArgIndex, Kind);
  }

  /// Remove the specified attribute at the specified arg index from this
  /// attribute list. Returns a new list because attribute lists are immutable.
  /// \param C Context used to uniquify the result.
  /// \param ArgNo Zero-based argument index.
  /// \param Kind Target-dependent attribute kind to remove.
  /// \return A new attribute list with the attributes removed.
  [[nodiscard]] AttributeList
  removeParamAttribute(LLVMContext &C, unsigned ArgNo, StringRef Kind) const {
    return removeAttributeAtIndex(C, ArgNo + FirstArgIndex, Kind);
  }

  /// Remove the specified attribute at the specified arg index from this
  /// attribute list. Returns a new list because attribute lists are immutable.
  /// \param C Context used to uniquify the result.
  /// \param ArgNo Zero-based argument index.
  /// \param AttrsToRemove Mask of attributes to remove.
  /// \return A new attribute list with the attributes removed.
  [[nodiscard]] AttributeList
  removeParamAttributes(LLVMContext &C, unsigned ArgNo,
                        const AttributeMask &AttrsToRemove) const {
    return removeAttributesAtIndex(C, ArgNo + FirstArgIndex, AttrsToRemove);
  }

  /// Remove all attributes at the specified arg index from this
  /// attribute list. Returns a new list because attribute lists are immutable.
  /// \param C Context used to uniquify the result.
  /// \param ArgNo Zero-based argument index to clear.
  /// \return A new attribute list with the attributes removed.
  [[nodiscard]] AttributeList removeParamAttributes(LLVMContext &C,
                                                    unsigned ArgNo) const {
    return removeAttributesAtIndex(C, ArgNo + FirstArgIndex);
  }

  /// Replace the type contained by attribute \p AttrKind at index \p ArgNo wih
  /// \p ReplacementTy, preserving all other attributes.
  /// \param C Context used to uniquify the result.
  /// \param ArgNo Attribute-set index whose typed attribute is replaced.
  /// \param Kind Typed attribute kind to replace.
  /// \param ReplacementTy New type payload.
  /// \return A new attribute list with the type replaced.
  [[nodiscard]] AttributeList
  replaceAttributeTypeAtIndex(LLVMContext &C, unsigned ArgNo,
                              Attribute::AttrKind Kind,
                              Type *ReplacementTy) const {
    Attribute Attr = getAttributeAtIndex(ArgNo, Kind);
    auto Attrs = removeAttributeAtIndex(C, ArgNo, Kind);
    return Attrs.addAttributeAtIndex(C, ArgNo,
                                     Attr.getWithNewType(C, ReplacementTy));
  }

  /// Add the dereferenceable attribute to the return-value attribute set.
  /// Returns a new list because attribute lists are immutable.
  /// \param C Context used to uniquify the result.
  /// \param Bytes Number of dereferenceable bytes.
  /// \return A new attribute list with the attributes added.
  [[nodiscard]] LLVM_ABI AttributeList
  addDereferenceableRetAttr(LLVMContext &C, uint64_t Bytes) const;

  /// Add the dereferenceable attribute to the attribute set at the given
  /// arg index. Returns a new list because attribute lists are immutable.
  /// \param C Context used to uniquify the result.
  /// \param ArgNo Zero-based argument index.
  /// \param Bytes Number of dereferenceable bytes.
  /// \return A new attribute list with the attributes added.
  [[nodiscard]] LLVM_ABI AttributeList addDereferenceableParamAttr(
      LLVMContext &C, unsigned ArgNo, uint64_t Bytes) const;

  /// Add the dereferenceable_or_null attribute to the attribute set at
  /// the given arg index. Returns a new list because attribute lists are
  /// immutable.
  /// \param C Context used to uniquify the result.
  /// \param ArgNo Zero-based argument index.
  /// \param Bytes Number of dereferenceable-or-null bytes.
  /// \return A new attribute list with the attributes added.
  [[nodiscard]] LLVM_ABI AttributeList addDereferenceableOrNullParamAttr(
      LLVMContext &C, unsigned ArgNo, uint64_t Bytes) const;

  /// Add the range attribute to the attribute set at the return value index.
  /// Returns a new list because attribute lists are immutable.
  /// \param C Context used to uniquify the result.
  /// \param CR Constant range for the return value.
  /// \return A new attribute list with the attributes added.
  [[nodiscard]] LLVM_ABI AttributeList
  addRangeRetAttr(LLVMContext &C, const ConstantRange &CR) const;

  /// Add the allocsize attribute to the attribute set at the given arg index.
  /// Returns a new list because attribute lists are immutable.
  /// \param C Context used to uniquify the result.
  /// \param ArgNo Zero-based argument index receiving the attribute.
  /// \param ElemSizeArg Argument index holding the element size.
  /// \param NumElemsArg Optional argument index holding the element count.
  /// \return A new attribute list with the attributes added.
  [[nodiscard]] LLVM_ABI AttributeList
  addAllocSizeParamAttr(LLVMContext &C, unsigned ArgNo, unsigned ElemSizeArg,
                        const std::optional<unsigned> &NumElemsArg) const;

  /// Try to intersect this AttributeList with Other. Returns std::nullopt if
  /// the two lists are inherently incompatible (imply different behavior, not
  /// just analysis).
  /// \param C Context used to uniquify the result.
  /// \param Other Attribute list to intersect with.
  /// \return The intersection, or std::nullopt if the lists are incompatible.
  [[nodiscard]] LLVM_ABI std::optional<AttributeList>
  intersectWith(LLVMContext &C, AttributeList Other) const;

  //===--------------------------------------------------------------------===//
  // AttributeList Accessors
  //===--------------------------------------------------------------------===//

  /// The attributes for the specified index are returned.
  /// \param Index Attribute-set index (function, return, or argument).
  /// \return The attributes for the specified index.
  LLVM_ABI AttributeSet getAttributes(unsigned Index) const;

  /// The attributes for the argument or parameter at the given index are
  /// returned.
  /// \param ArgNo Zero-based argument index.
  /// \return The attributes for the argument or parameter at the given index.
  LLVM_ABI AttributeSet getParamAttrs(unsigned ArgNo) const;

  /// The attributes for the ret value are returned.
  /// \return The attributes for the ret value.
  LLVM_ABI AttributeSet getRetAttrs() const;

  /// The function attributes are returned.
  /// \return The function attributes.
  LLVM_ABI AttributeSet getFnAttrs() const;

  /// Return true if the attribute exists at the given index.
  /// \param Index Attribute-set index to query.
  /// \param Kind Enum attribute kind to look up.
  /// \return true if the attribute exists at the given index.
  LLVM_ABI bool hasAttributeAtIndex(unsigned Index,
                                    Attribute::AttrKind Kind) const;

  /// Return true if the attribute exists at the given index.
  /// \param Index Attribute-set index to query.
  /// \param Kind Target-dependent attribute kind to look up.
  /// \return true if the attribute exists at the given index.
  LLVM_ABI bool hasAttributeAtIndex(unsigned Index, StringRef Kind) const;

  /// Return true if attribute exists at the given index.
  /// \param Index Attribute-set index to query.
  /// \return true if attribute exists at the given index.
  LLVM_ABI bool hasAttributesAtIndex(unsigned Index) const;

  /// Return true if the attribute exists for the given argument
  /// \param ArgNo Zero-based argument index.
  /// \param Kind Enum attribute kind to look up.
  /// \return true if the attribute exists for the given argument.
  bool hasParamAttr(unsigned ArgNo, Attribute::AttrKind Kind) const {
    return hasAttributeAtIndex(ArgNo + FirstArgIndex, Kind);
  }

  /// Return true if the attribute exists for the given argument
  /// \param ArgNo Zero-based argument index.
  /// \param Kind Target-dependent attribute kind to look up.
  /// \return true if the attribute exists for the given argument.
  bool hasParamAttr(unsigned ArgNo, StringRef Kind) const {
    return hasAttributeAtIndex(ArgNo + FirstArgIndex, Kind);
  }

  /// Return true if attributes exists for the given argument
  /// \param ArgNo Zero-based argument index.
  /// \return true if attributes exists for the given argument.
  bool hasParamAttrs(unsigned ArgNo) const {
    return hasAttributesAtIndex(ArgNo + FirstArgIndex);
  }

  /// Return true if the attribute exists for the return value.
  /// \param Kind Enum attribute kind to look up.
  /// \return true if the attribute exists for the return value.
  bool hasRetAttr(Attribute::AttrKind Kind) const {
    return hasAttributeAtIndex(ReturnIndex, Kind);
  }

  /// Return true if the attribute exists for the return value.
  /// \param Kind Target-dependent attribute kind to look up.
  /// \return true if the attribute exists for the return value.
  bool hasRetAttr(StringRef Kind) const {
    return hasAttributeAtIndex(ReturnIndex, Kind);
  }

  /// Return true if attributes exist for the return value.
  /// \return true if attributes exist for the return value.
  bool hasRetAttrs() const { return hasAttributesAtIndex(ReturnIndex); }

  /// Return true if the attribute exists for the function.
  /// \param Kind Enum attribute kind to look up.
  /// \return true if the attribute exists for the function.
  LLVM_ABI bool hasFnAttr(Attribute::AttrKind Kind) const;

  /// Return true if the attribute exists for the function.
  /// \param Kind Target-dependent attribute kind to look up.
  /// \return true if the attribute exists for the function.
  LLVM_ABI bool hasFnAttr(StringRef Kind) const;

  /// Return true the attributes exist for the function.
  /// \return true the attributes exist for the function.
  bool hasFnAttrs() const { return hasAttributesAtIndex(FunctionIndex); }

  /// Return true if \p Kind is set on any parameter or the return value.
  ///
  /// If \p Index is not nullptr, the index of a parameter with the specified
  /// attribute is provided.
  /// \param Kind Enum attribute kind to search for.
  /// \param Index Optional out-parameter receiving a matching attribute index.
  /// \return true if \p Kind is set on any parameter or the return value.
  LLVM_ABI bool hasAttrSomewhere(Attribute::AttrKind Kind,
                                 unsigned *Index = nullptr) const;

  /// Return the attribute object that exists at the given index.
  /// \param Index Attribute-set index to query.
  /// \param Kind Enum attribute kind to look up.
  /// \return The attribute object that exists at the given index.
  LLVM_ABI Attribute getAttributeAtIndex(unsigned Index,
                                         Attribute::AttrKind Kind) const;

  /// Return the attribute object that exists at the given index.
  /// \param Index Attribute-set index to query.
  /// \param Kind Target-dependent attribute kind to look up.
  /// \return The attribute object that exists at the given index.
  LLVM_ABI Attribute getAttributeAtIndex(unsigned Index, StringRef Kind) const;

  /// Return the attribute object that exists at the arg index.
  /// \param ArgNo Zero-based argument index.
  /// \param Kind Enum attribute kind to look up.
  /// \return The attribute object that exists at the arg index.
  Attribute getParamAttr(unsigned ArgNo, Attribute::AttrKind Kind) const {
    return getAttributeAtIndex(ArgNo + FirstArgIndex, Kind);
  }

  /// Return the attribute object that exists at the given index.
  /// \param ArgNo Zero-based argument index.
  /// \param Kind Target-dependent attribute kind to look up.
  /// \return The attribute object that exists at the given index.
  Attribute getParamAttr(unsigned ArgNo, StringRef Kind) const {
    return getAttributeAtIndex(ArgNo + FirstArgIndex, Kind);
  }

  /// Return the attribute object that exists for the function.
  /// \param Kind Enum attribute kind to look up.
  /// \return The attribute object that exists for the function.
  Attribute getFnAttr(Attribute::AttrKind Kind) const {
    return getAttributeAtIndex(FunctionIndex, Kind);
  }

  /// Return the attribute object that exists for the function.
  /// \param Kind Target-dependent attribute kind to look up.
  /// \return The attribute object that exists for the function.
  Attribute getFnAttr(StringRef Kind) const {
    return getAttributeAtIndex(FunctionIndex, Kind);
  }

  /// Return the attribute for the given attribute kind for the return value.
  /// \param Kind Enum attribute kind to look up.
  /// \return The attribute for the given attribute kind for the return value.
  Attribute getRetAttr(Attribute::AttrKind Kind) const {
    return getAttributeAtIndex(ReturnIndex, Kind);
  }

  /// Return the alignment of the return value.
  /// \return The alignment of the return value.
  LLVM_ABI MaybeAlign getRetAlignment() const;

  /// Return the alignment for the specified function parameter.
  /// \param ArgNo Zero-based argument index.
  /// \return The alignment for the specified function parameter.
  LLVM_ABI MaybeAlign getParamAlignment(unsigned ArgNo) const;

  /// Return the stack alignment for the specified function parameter.
  /// \param ArgNo Zero-based argument index.
  /// \return The stack alignment for the specified function parameter.
  LLVM_ABI MaybeAlign getParamStackAlignment(unsigned ArgNo) const;

  /// Return the byval type for the specified function parameter.
  /// \param ArgNo Zero-based argument index.
  /// \return The byval type for the specified function parameter.
  LLVM_ABI Type *getParamByValType(unsigned ArgNo) const;

  /// Return the sret type for the specified function parameter.
  /// \param ArgNo Zero-based argument index.
  /// \return The sret type for the specified function parameter.
  LLVM_ABI Type *getParamStructRetType(unsigned ArgNo) const;

  /// Return the byref type for the specified function parameter.
  /// \param ArgNo Zero-based argument index.
  /// \return The byref type for the specified function parameter.
  LLVM_ABI Type *getParamByRefType(unsigned ArgNo) const;

  /// Return the preallocated type for the specified function parameter.
  /// \param ArgNo Zero-based argument index.
  /// \return The preallocated type for the specified function parameter.
  LLVM_ABI Type *getParamPreallocatedType(unsigned ArgNo) const;

  /// Return the inalloca type for the specified function parameter.
  /// \param ArgNo Zero-based argument index.
  /// \return The inalloca type for the specified function parameter.
  LLVM_ABI Type *getParamInAllocaType(unsigned ArgNo) const;

  /// Return the elementtype type for the specified function parameter.
  /// \param ArgNo Zero-based argument index.
  /// \return The elementtype type for the specified function parameter.
  LLVM_ABI Type *getParamElementType(unsigned ArgNo) const;

  /// Get the stack alignment of the function.
  /// \return The stack alignment of the function.
  LLVM_ABI MaybeAlign getFnStackAlignment() const;

  /// Get the stack alignment of the return value.
  /// \return The stack alignment of the return value.
  LLVM_ABI MaybeAlign getRetStackAlignment() const;

  /// Get the number of dereferenceable bytes (or zero if unknown) of the return
  /// value.
  /// \return The number of dereferenceable bytes (or zero if unknown) of the return value.
  LLVM_ABI uint64_t getRetDereferenceableBytes() const;

  /// Get the number of dereferenceable bytes (or zero if unknown) of an arg.
  /// \param Index Zero-based argument index.
  /// \return The number of dereferenceable bytes (or zero if unknown) of an arg.
  LLVM_ABI uint64_t getParamDereferenceableBytes(unsigned Index) const;

  /// Get the number of dereferenceable_or_null bytes (or zero if unknown) of
  /// the return value.
  /// \return The number of dereferenceable_or_null bytes (or zero if unknown) of the return value.
  LLVM_ABI uint64_t getRetDereferenceableOrNullBytes() const;

  /// Get the number of dead_on_return bytes (or zero if unknown) of an arg.
  /// \param Index Zero-based argument index.
  /// \return The number of dead_on_return bytes (or zero if unknown) of an arg.
  LLVM_ABI DeadOnReturnInfo getDeadOnReturnInfo(unsigned Index) const;

  /// Get the number of dereferenceable_or_null bytes (or zero if unknown) of an
  /// arg.
  /// \param ArgNo Zero-based argument index.
  /// \return The number of dereferenceable_or_null bytes (or zero if unknown) of an arg.
  LLVM_ABI uint64_t getParamDereferenceableOrNullBytes(unsigned ArgNo) const;

  /// Get range (or std::nullopt if unknown) of an arg.
  /// \param ArgNo Zero-based argument index.
  /// \return The range (or std::nullopt if unknown) of an arg.
  LLVM_ABI std::optional<ConstantRange> getParamRange(unsigned ArgNo) const;

  /// Get the disallowed floating-point classes of the return value.
  /// \return The disallowed floating-point classes of the return value.
  LLVM_ABI FPClassTest getRetNoFPClass() const;

  /// Get the disallowed floating-point classes of the argument value.
  /// \param ArgNo Zero-based argument index.
  /// \return The disallowed floating-point classes of the argument value.
  LLVM_ABI FPClassTest getParamNoFPClass(unsigned ArgNo) const;

  /// Get the unwind table kind requested for the function.
  /// \return The unwind table kind requested for the function.
  LLVM_ABI UWTableKind getUWTableKind() const;

  /// Return the \c allockind bits for the function, if present.
  /// \return The \c allockind bits for the function, if present.
  LLVM_ABI AllocFnKind getAllocKind() const;

  /// Returns memory effects of the function.
  /// \return The memory effects of the function.
  LLVM_ABI MemoryEffects getMemoryEffects() const;

  /// Return the attributes at the index as a string.
  /// \param Index Attribute-set index to format.
  /// \param InAttrGrp True when formatting inside an attribute group.
  /// \return The attributes at the index as a string.
  LLVM_ABI std::string getAsString(unsigned Index,
                                   bool InAttrGrp = false) const;

  /// Return true if this attribute list belongs to the LLVMContext.
  /// \param C Context that should own this attribute list.
  /// \return true if this attribute list belongs to the LLVMContext.
  LLVM_ABI bool hasParentContext(LLVMContext &C) const;

  //===--------------------------------------------------------------------===//
  // AttributeList Introspection
  //===--------------------------------------------------------------------===//

  /// Iterator over attribute sets in this list.
  using iterator = const AttributeSet *;

  /// Return an iterator to the first attribute set in this list.
  /// \return An iterator to the first attribute set in this list.
  LLVM_ABI iterator begin() const;
  /// Return an iterator past the last attribute set in this list.
  /// \return An iterator past the last attribute set in this list.
  LLVM_ABI iterator end() const;

  /// Return the number of attribute sets stored in this list.
  /// \return The number of attribute sets stored in this list.
  LLVM_ABI unsigned getNumAttrSets() const;

  // Implementation of indexes(). Produces iterators that wrap an index. Mostly
  // to hide the awkwardness of unsigned wrapping when iterating over valid
  // indexes.
  /// Iterator over valid attribute indexes, including \c FunctionIndex and
  /// argument slots.
  struct index_iterator {
    /// Number of attribute-set slots in the owning \c AttributeList.
    unsigned NumAttrSets;
    /// Construct an index iterator covering \p NumAttrSets attribute slots.
    /// \param NumAttrSets Number of attribute-set slots in the list.
    index_iterator(int NumAttrSets) : NumAttrSets(NumAttrSets) {}
    /// Thin wrapper around an attribute-set index for range-based iteration.
    struct int_wrapper {
      /// Construct a wrapper around attribute index \p i.
      /// \param i Attribute-set index to wrap.
      int_wrapper(unsigned i) : i(i) {}
      /// Current attribute index, including \c FunctionIndex and argument slots.
      unsigned i;
      /// Return the current attribute index.
      /// \return The current attribute index.
      unsigned operator*() { return i; }
      /// Return true if this wrapper's index differs from \p Other.
      /// \param Other Wrapper to compare against.
      /// \return true if this wrapper's index differs from \p Other.
      bool operator!=(const int_wrapper &Other) { return i != Other.i; }
      /// Advance to the next attribute index.
      /// \return A reference to this wrapper after advancing.
      int_wrapper &operator++() {
        // This is expected to undergo unsigned wrapping since FunctionIndex is
        // ~0 and that's where we start.
        ++i;
        return *this;
      }
    };

    /// Iterator to the function attribute index (\c FunctionIndex).
    /// \return An iterator to the function attribute index (\c FunctionIndex).
    int_wrapper begin() { return int_wrapper(AttributeList::FunctionIndex); }

    /// Iterator past the last valid attribute index.
    /// \return An iterator past the last valid attribute index.
    int_wrapper end() { return int_wrapper(NumAttrSets - 1); }
  };

  /// Use this to iterate over the valid attribute indexes.
  /// \return An index iterator covering this list's attribute slots.
  index_iterator indexes() const { return index_iterator(getNumAttrSets()); }

  /// Return true if both lists refer to the same uniquified value.
  /// \param RHS Attribute list to compare against.
  /// \return true if both lists refer to the same uniquified value.
  bool operator==(const AttributeList &RHS) const { return pImpl == RHS.pImpl; }
  /// Return true if the lists refer to different uniquified values.
  /// \param RHS Attribute list to compare against.
  /// \return true if the lists refer to different uniquified values.
  bool operator!=(const AttributeList &RHS) const { return pImpl != RHS.pImpl; }

  /// Return a raw pointer that uniquely identifies this attribute list.
  /// \return A raw pointer that uniquely identifies this attribute list.
  void *getRawPointer() const {
    return pImpl;
  }

  /// Return true if there are no attributes.
  /// \return true if there are no attributes.
  bool isEmpty() const { return pImpl == nullptr; }

  /// Print this attribute list to stream \p O.
  /// \param O Output stream.
  LLVM_ABI void print(raw_ostream &O) const;

  /// Dump this attribute list to stderr (for debugging).
  LLVM_ABI void dump() const;
};

//===----------------------------------------------------------------------===//
/// \class
/// Provide DenseMapInfo for AttributeList.
template <> struct DenseMapInfo<AttributeList, void> {
  /// Return a hash value for attribute list \p AS.
  /// \param AS Attribute list to hash.
  /// \return A hash value for attribute list \p AS.
  static unsigned getHashValue(AttributeList AS) {
    return DenseMapInfo<const void *>::getHashValue(AS.pImpl);
  }

  /// Return true if \p LHS and \p RHS refer to the same attribute list.
  /// \param LHS First attribute list.
  /// \param RHS Second attribute list.
  /// \return true if \p LHS and \p RHS refer to the same attribute list.
  static bool isEqual(AttributeList LHS, AttributeList RHS) {
    return LHS == RHS;
  }
};

//===----------------------------------------------------------------------===//
/// \class
/// Mutable builder used with Attribute::get to assemble attributes.
///
/// This class is used in conjunction with the Attribute::get method to
/// create an Attribute object. The object itself is uniquified. The Builder's
/// value, however, is not. So this can be used as a quick way to test for
/// equality, presence of attributes, etc.
class AttrBuilder {
  LLVMContext &Ctx;
  SmallVector<Attribute, 8> Attrs;

public:
  /// Construct an empty builder that will uniquify attributes in \p Ctx.
  /// \param Ctx Context used when materializing Attribute values.
  AttrBuilder(LLVMContext &Ctx) : Ctx(Ctx) {}
  /// Copying is deleted; AttrBuilder owns mutable attribute state.
  /// \param B Builder that would have been copied.
  AttrBuilder(const AttrBuilder &B) = delete;
  /// Move-construct from builder \p B.
  /// \param B Builder to move from.
  AttrBuilder(AttrBuilder &&B) = default;

  /// Construct a builder pre-populated with attribute \p A.
  /// \param Ctx Context used when materializing Attribute values.
  /// \param A Initial attribute to add.
  AttrBuilder(LLVMContext &Ctx, const Attribute &A) : Ctx(Ctx) {
    addAttribute(A);
  }

  /// Construct a builder containing the attributes from \p AS.
  /// \param Ctx Context used when materializing Attribute values.
  /// \param AS Attribute set to copy into the builder.
  LLVM_ABI AttrBuilder(LLVMContext &Ctx, AttributeSet AS);

  /// Remove all attributes from the builder.
  LLVM_ABI void clear();

  /// Add an attribute to the builder.
  /// \param Val Enum attribute kind to add.
  /// \return A reference to this builder.
  LLVM_ABI AttrBuilder &addAttribute(Attribute::AttrKind Val);

  /// Add the Attribute object to the builder.
  /// \param A Attribute to add.
  /// \return A reference to this builder.
  LLVM_ABI AttrBuilder &addAttribute(Attribute A);

  /// Add the target-dependent attribute to the builder.
  /// \param A Target-dependent attribute kind.
  /// \param V Attribute value string.
  /// \return A reference to this builder.
  LLVM_ABI AttrBuilder &addAttribute(StringRef A, StringRef V = StringRef());

  /// Remove an attribute from the builder.
  /// \param Val Enum attribute kind to remove.
  /// \return A reference to this builder.
  LLVM_ABI AttrBuilder &removeAttribute(Attribute::AttrKind Val);

  /// Remove the target-dependent attribute from the builder.
  /// \param A Target-dependent attribute kind to remove.
  /// \return A reference to this builder.
  LLVM_ABI AttrBuilder &removeAttribute(StringRef A);

  /// Remove the target-dependent attribute from the builder.
  /// \param A Attribute whose kind is removed from the builder.
  /// \return A reference to this builder.
  AttrBuilder &removeAttribute(Attribute A) {
    if (A.isStringAttribute())
      return removeAttribute(A.getKindAsString());
    else
      return removeAttribute(A.getKindAsEnum());
  }

  /// Add the attributes from the builder. Attributes in the passed builder
  /// overwrite attributes in this builder if they have the same key.
  /// \param B Builder whose attributes are merged in.
  /// \return A reference to this builder.
  LLVM_ABI AttrBuilder &merge(const AttrBuilder &B);

  /// Remove the attributes from the builder.
  /// \param AM Mask of attributes to remove.
  /// \return A reference to this builder.
  LLVM_ABI AttrBuilder &remove(const AttributeMask &AM);

  /// Return true if the builder has any attribute that's in the
  /// specified builder.
  /// \param AM Mask of attributes to test for overlap.
  /// \return true if the builder has any attribute that's in the specified builder.
  LLVM_ABI bool overlaps(const AttributeMask &AM) const;

  /// Return true if the builder has the specified attribute.
  /// \param A Enum attribute kind to look up.
  /// \return true if the builder has the specified attribute.
  LLVM_ABI bool contains(Attribute::AttrKind A) const;

  /// Return true if the builder has the specified target-dependent
  /// attribute.
  /// \param A Target-dependent attribute kind to look up.
  /// \return true if the builder has the specified target-dependent attribute.
  LLVM_ABI bool contains(StringRef A) const;

  /// Return true if the builder has IR-level attributes.
  /// \return true if the builder has IR-level attributes.
  bool hasAttributes() const { return !Attrs.empty(); }

  /// Return Attribute with the given Kind. The returned attribute will be
  /// invalid if the Kind is not present in the builder.
  /// \param Kind Enum attribute kind to look up.
  /// \return The attribute with the given kind, if present.
  LLVM_ABI Attribute getAttribute(Attribute::AttrKind Kind) const;

  /// Return Attribute with the given Kind. The returned attribute will be
  /// invalid if the Kind is not present in the builder.
  /// \param Kind Target-dependent attribute kind to look up.
  /// \return The attribute with the given kind, if present.
  LLVM_ABI Attribute getAttribute(StringRef Kind) const;

  /// Retrieve the range if the attribute exists (std::nullopt is returned
  /// otherwise).
  /// \return The range if the attribute exists (std::nullopt is returned otherwise).
  LLVM_ABI std::optional<ConstantRange> getRange() const;

  /// Return raw (possibly packed/encoded) value of integer attribute or
  /// std::nullopt if not set.
  /// \param Kind Integer attribute kind to look up.
  /// \return The raw integer attribute value, or std::nullopt if not set.
  LLVM_ABI std::optional<uint64_t>
  getRawIntAttr(Attribute::AttrKind Kind) const;

  /// Retrieve the alignment attribute, if it exists.
  /// \return The alignment attribute, if it exists.
  MaybeAlign getAlignment() const {
    return MaybeAlign(getRawIntAttr(Attribute::Alignment).value_or(0));
  }

  /// Retrieve the stack alignment attribute, if it exists.
  /// \return The stack alignment attribute, if it exists.
  MaybeAlign getStackAlignment() const {
    return MaybeAlign(getRawIntAttr(Attribute::StackAlignment).value_or(0));
  }

  /// Retrieve the number of dereferenceable bytes, if the
  /// dereferenceable attribute exists (zero is returned otherwise).
  /// \return The number of dereferenceable bytes, if the dereferenceable attribute exists (zero is returned otherwise).
  uint64_t getDereferenceableBytes() const {
    return getRawIntAttr(Attribute::Dereferenceable).value_or(0);
  }

  /// Retrieve the number of dereferenceable_or_null bytes, if the
  /// dereferenceable_or_null attribute exists (zero is returned otherwise).
  /// \return The number of dereferenceable_or_null bytes, if the dereferenceable_or_null attribute exists (zero is returned otherwise).
  uint64_t getDereferenceableOrNullBytes() const {
    return getRawIntAttr(Attribute::DereferenceableOrNull).value_or(0);
  }

  /// Retrieve the bitmask for nofpclass, if the nofpclass attribute exists
  /// (fcNone is returned otherwise).
  /// \return The bitmask for nofpclass, if the nofpclass attribute exists (fcNone is returned otherwise).
  FPClassTest getNoFPClass() const {
    std::optional<uint64_t> Raw = getRawIntAttr(Attribute::NoFPClass);
    return static_cast<FPClassTest>(Raw.value_or(0));
  }

  /// Retrieve type for the given type attribute.
  /// \param Kind Type attribute kind to look up.
  /// \return The type for the given type attribute, or null if absent.
  LLVM_ABI Type *getTypeAttr(Attribute::AttrKind Kind) const;

  /// Retrieve the byval type.
  /// \return The byval type.
  Type *getByValType() const { return getTypeAttr(Attribute::ByVal); }

  /// Retrieve the sret type.
  /// \return The sret type.
  Type *getStructRetType() const { return getTypeAttr(Attribute::StructRet); }

  /// Retrieve the byref type.
  /// \return The byref type.
  Type *getByRefType() const { return getTypeAttr(Attribute::ByRef); }

  /// Retrieve the preallocated type.
  /// \return The preallocated type.
  Type *getPreallocatedType() const {
    return getTypeAttr(Attribute::Preallocated);
  }

  /// Retrieve the inalloca type.
  /// \return The inalloca type.
  Type *getInAllocaType() const { return getTypeAttr(Attribute::InAlloca); }

  /// Retrieve the allocsize args, or std::nullopt if the attribute does not
  /// exist.
  /// \return The allocsize args, or std::nullopt if the attribute does not exist.
  LLVM_ABI std::optional<std::pair<unsigned, std::optional<unsigned>>>
  getAllocSizeArgs() const;

  /// Add integer attribute with raw value (packed/encoded if necessary).
  /// \param Kind Integer attribute kind.
  /// \param Value Raw integer payload.
  /// \return A reference to this builder.
  LLVM_ABI AttrBuilder &addRawIntAttr(Attribute::AttrKind Kind, uint64_t Value);

  /// Add an alignment attribute from \p Align.
  ///
  /// This turns an alignment into the form used internally in Attribute.
  /// This call has no effect if Align is not set.
  /// \param Align Alignment to encode; unset means no change.
  /// \return A reference to this builder.
  LLVM_ABI AttrBuilder &addAlignmentAttr(MaybeAlign Align);

  /// Add an alignment attribute from an integer power-of-two alignment.
  ///
  /// This turns an int alignment (which must be a power of 2) into the
  /// form used internally in Attribute.
  /// This call has no effect if Align is 0.
  /// Deprecated, use the version using a MaybeAlign.
  /// \param Align Alignment in bytes, or 0 to leave unchanged.
  /// \return A reference to this builder.
  inline AttrBuilder &addAlignmentAttr(unsigned Align) {
    return addAlignmentAttr(MaybeAlign(Align));
  }

  /// Add a stack-alignment attribute from \p Align.
  ///
  /// This turns a stack alignment into the form used internally in Attribute.
  /// This call has no effect if Align is not set.
  /// \param Align Stack alignment to encode; unset means no change.
  /// \return A reference to this builder.
  LLVM_ABI AttrBuilder &addStackAlignmentAttr(MaybeAlign Align);

  /// Add a stack-alignment attribute from an integer power-of-two alignment.
  ///
  /// This turns an int stack alignment (which must be a power of 2) into
  /// the form used internally in Attribute.
  /// This call has no effect if Align is 0.
  /// Deprecated, use the version using a MaybeAlign.
  /// \param Align Stack alignment in bytes, or 0 to leave unchanged.
  /// \return A reference to this builder.
  inline AttrBuilder &addStackAlignmentAttr(unsigned Align) {
    return addStackAlignmentAttr(MaybeAlign(Align));
  }

  /// This turns the number of dereferenceable bytes into the form used
  /// internally in Attribute.
  /// \param Bytes Number of dereferenceable bytes.
  /// \return A reference to this builder.
  LLVM_ABI AttrBuilder &addDereferenceableAttr(uint64_t Bytes);

  /// This turns the number of dead_on_return bytes into the form used
  /// internally in Attribute.
  /// \param Info Dead-on-return byte information.
  /// \return A reference to this builder.
  LLVM_ABI AttrBuilder &addDeadOnReturnAttr(DeadOnReturnInfo Info);

  /// This turns the number of dereferenceable_or_null bytes into the
  /// form used internally in Attribute.
  /// \param Bytes Number of dereferenceable-or-null bytes.
  /// \return A reference to this builder.
  LLVM_ABI AttrBuilder &addDereferenceableOrNullAttr(uint64_t Bytes);

  /// This turns one (or two) ints into the form used internally in Attribute.
  /// \param ElemSizeArg Argument index holding the element size.
  /// \param NumElemsArg Optional argument index holding the element count.
  /// \return A reference to this builder.
  LLVM_ABI AttrBuilder &
  addAllocSizeAttr(unsigned ElemSizeArg,
                   const std::optional<unsigned> &NumElemsArg);

  /// This turns two ints into the form used internally in Attribute.
  /// \param MinValue Minimum vscale.
  /// \param MaxValue Maximum vscale, or nullopt if unbounded.
  /// \return A reference to this builder.
  LLVM_ABI AttrBuilder &addVScaleRangeAttr(unsigned MinValue,
                                           std::optional<unsigned> MaxValue);

  /// Add a type attribute with the given type.
  /// \param Kind Type attribute kind.
  /// \param Ty Type payload.
  /// \return A reference to this builder.
  LLVM_ABI AttrBuilder &addTypeAttr(Attribute::AttrKind Kind, Type *Ty);

  /// This turns a byval type into the form used internally in Attribute.
  /// \param Ty Structure type passed by value.
  /// \return A reference to this builder.
  LLVM_ABI AttrBuilder &addByValAttr(Type *Ty);

  /// This turns a sret type into the form used internally in Attribute.
  /// \param Ty Structure return type.
  /// \return A reference to this builder.
  LLVM_ABI AttrBuilder &addStructRetAttr(Type *Ty);

  /// This turns a byref type into the form used internally in Attribute.
  /// \param Ty Referenced pointee type.
  /// \return A reference to this builder.
  LLVM_ABI AttrBuilder &addByRefAttr(Type *Ty);

  /// This turns a preallocated type into the form used internally in Attribute.
  /// \param Ty Preallocated type.
  /// \return A reference to this builder.
  LLVM_ABI AttrBuilder &addPreallocatedAttr(Type *Ty);

  /// This turns an inalloca type into the form used internally in Attribute.
  /// \param Ty Inalloca type.
  /// \return A reference to this builder.
  LLVM_ABI AttrBuilder &addInAllocaAttr(Type *Ty);

  /// Add an allocsize attribute, using the representation returned by
  /// Attribute.getIntValue().
  /// \param RawAllocSizeRepr Packed allocsize representation.
  /// \return A reference to this builder.
  LLVM_ABI AttrBuilder &addAllocSizeAttrFromRawRepr(uint64_t RawAllocSizeRepr);

  /// Add a vscale_range attribute, using the representation returned by
  /// Attribute.getIntValue().
  /// \param RawVScaleRangeRepr Packed vscale_range representation.
  /// \return A reference to this builder.
  LLVM_ABI AttrBuilder &
  addVScaleRangeAttrFromRawRepr(uint64_t RawVScaleRangeRepr);

  /// This turns the unwind table kind into the form used internally in
  /// Attribute.
  /// \param Kind Unwind-table kind.
  /// \return A reference to this builder.
  LLVM_ABI AttrBuilder &addUWTableAttr(UWTableKind Kind);

  /// Encode an \c alloc_kind attribute from the given allocator-function kind.
  /// \param Kind Allocator-function kind bitmask.
  /// \return A reference to this builder.
  LLVM_ABI AttrBuilder &addAllocKindAttr(AllocFnKind Kind);

  /// Add memory effect attribute.
  /// \param ME Memory effects to encode.
  /// \return A reference to this builder.
  LLVM_ABI AttrBuilder &addMemoryAttr(MemoryEffects ME);

  /// Add captures attribute.
  /// \param CI Capture information to encode.
  /// \return A reference to this builder.
  LLVM_ABI AttrBuilder &addCapturesAttr(CaptureInfo CI);

  /// Add denormal_fpenv attribute.
  /// \param Mode Denormal floating-point environment mode.
  /// \return A reference to this builder.
  LLVM_ABI AttrBuilder &addDenormalFPEnvAttr(DenormalFPEnv Mode);

  /// Add a \c nofpclass attribute excluding classes in \p NoFPClassMask.
  /// \param NoFPClassMask Disallowed floating-point classes.
  /// \return A reference to this builder.
  LLVM_ABI AttrBuilder &addNoFPClassAttr(FPClassTest NoFPClassMask);

  /// Add a ConstantRange attribute with the given range.
  /// \param Kind ConstantRange attribute kind.
  /// \param CR Constant range payload.
  /// \return A reference to this builder.
  LLVM_ABI AttrBuilder &addConstantRangeAttr(Attribute::AttrKind Kind,
                                             const ConstantRange &CR);

  /// Add range attribute.
  /// \param CR Constant range for the value.
  /// \return A reference to this builder.
  LLVM_ABI AttrBuilder &addRangeAttr(const ConstantRange &CR);

  /// Add a ConstantRangeList attribute with the given ranges.
  /// \param Kind ConstantRangeList attribute kind.
  /// \param Val List of constant ranges.
  /// \return A reference to this builder.
  LLVM_ABI AttrBuilder &addConstantRangeListAttr(Attribute::AttrKind Kind,
                                                 ArrayRef<ConstantRange> Val);

  /// Add initializes attribute.
  /// \param CRL Constant-range list describing initialized offsets.
  /// \return A reference to this builder.
  LLVM_ABI AttrBuilder &addInitializesAttr(const ConstantRangeList &CRL);

  /// Add parameter attributes equivalent to metadata on instruction \p I.
  ///
  /// Adds 0 or more parameter attributes which are equivalent to metadata
  /// attached to \p I. e.g. !align -> align. This assumes the argument type is
  /// the same as the original instruction and the attribute is compatible.
  /// \param I Instruction whose metadata is converted to attributes.
  /// \return A reference to this builder.
  LLVM_ABI AttrBuilder &addFromEquivalentMetadata(const Instruction &I);

  /// Return the attributes currently stored in this builder.
  /// \return The attributes currently stored in this builder.
  ArrayRef<Attribute> attrs() const { return Attrs; }

  /// Return true if this builder's attributes equal those in \p B.
  /// \param B Builder to compare against.
  /// \return true if this builder's attributes equal those in \p B.
  LLVM_ABI bool operator==(const AttrBuilder &B) const;
  /// Return true if this builder's attribute set differs from \p B.
  /// \param B Builder to compare against.
  /// \return true if this builder's attribute set differs from \p B.
  bool operator!=(const AttrBuilder &B) const { return !(*this == B); }
};

/// Helpers for attribute/type compatibility and intersection.
namespace AttributeFuncs {

/// Selects which attributes to consider when checking type compatibility.
enum AttributeSafetyKind : uint8_t {
  ASK_SAFE_TO_DROP = 1, ///< Include only attributes that are safe to drop.
  ASK_UNSAFE_TO_DROP = 2, ///< Include only attributes that may be unsafe to drop.
  ASK_ALL = ASK_SAFE_TO_DROP | ASK_UNSAFE_TO_DROP, ///< Include all attributes.
};

/// Returns true if this is a type legal for the 'nofpclass' attribute. This
/// follows the same type rules as FPMathOperator.
/// \param Ty Type to test for nofpclass compatibility.
/// \return true if this is a type legal for the 'nofpclass' attribute.
LLVM_ABI bool isNoFPClassCompatibleType(Type *Ty);

/// Return the mask of attributes incompatible with type \p Ty.
///
/// The argument \p AS is used as a hint for the attributes whose compatibility
/// is being checked against \p Ty. This does not mean the return will be a
/// subset of \p AS, just that attributes that have specific dynamic type
/// compatibilities (i.e `range`) will be checked against what is contained in
/// \p AS. The argument \p ASK indicates, if only attributes that are known to
/// be safely droppable are contained in the mask; only attributes that might be
/// unsafe to drop (e.g., ABI-related attributes) are in the mask; or both.
/// \param Ty Type being checked for attribute compatibility.
/// \param AS Hint attribute set used for dynamic compatibility checks.
/// \param ASK Which safety class of attributes to consider.
/// \return The mask of attributes incompatible with type \p Ty.
LLVM_ABI AttributeMask typeIncompatible(Type *Ty, AttributeSet AS,
                                        AttributeSafetyKind ASK = ASK_ALL);

/// Return param/return attributes that imply UB for invalid values.
///
/// For example, this includes noundef (where undef implies UB), but not nonnull
/// (where null implies poison). It also does not include attributes like
/// nocapture, which constrain the function implementation rather than the
/// passed value.
/// \return Param/return attributes that imply UB for invalid values.
LLVM_ABI AttributeMask getUBImplyingAttributes();

/// Return true if Caller and Callee have compatible attributes for inlining.
///
/// Compatibility here is for target-independent attributes.
/// \param Caller Function that would contain the inlined callee.
/// \param Callee Function being considered for inlining.
/// \return true if Caller and Callee have compatible attributes for inlining.
LLVM_ABI bool areInlineCompatible(const Function &Caller,
                                  const Function &Callee);

/// Return true unless Callee is strictfp while Caller is not.
/// \param Caller Function that would contain the inlined callee.
/// \param Callee Function being considered for inlining.
/// \return true unless Callee is strictfp while Caller is not.
LLVM_ABI bool isStrictFPInlineCompatible(const Function &Caller,
                                         const Function &Callee);

/// Checks  if there are any incompatible function attributes between
/// \p A and \p B.
///
/// \param [in] A - The first function to be compared with.
/// \param [in] B - The second function to be compared with.
/// \returns true if the functions have compatible attributes.
LLVM_ABI bool areOutlineCompatible(const Function &A, const Function &B);

/// Merge caller's and callee's attributes.
/// \param Caller Function receiving merged attributes from inlining.
/// \param Callee Function whose attributes are merged into the caller.
LLVM_ABI void mergeAttributesForInlining(Function &Caller,
                                         const Function &Callee);

/// Merges the functions attributes from \p ToMerge into function \p Base.
///
/// \param [in,out] Base - The function being merged into.
/// \param [in] ToMerge - The function to merge attributes from.
LLVM_ABI void mergeAttributesForOutlining(Function &Base,
                                          const Function &ToMerge);

/// Update min-legal-vector-width if it is in Attribute and less than Width.
/// \param Fn Function whose min-legal-vector-width may be raised.
/// \param Width Candidate minimum legal vector width in bits.
LLVM_ABI void updateMinLegalVectorWidthAttr(Function &Fn, uint64_t Width);

} // end namespace AttributeFuncs

} // end namespace llvm

#endif // LLVM_IR_ATTRIBUTES_H
