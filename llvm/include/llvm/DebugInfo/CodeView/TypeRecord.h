//===- TypeRecord.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_TYPERECORD_H
#define LLVM_DEBUGINFO_CODEVIEW_TYPERECORD_H

#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/GUID.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/Support/BinaryStreamArray.h"
#include "llvm/Support/Endian.h"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace llvm {
namespace codeview {

using support::little32_t;
using support::ulittle16_t;
using support::ulittle32_t;

/// A raw CodeView member record: leaf kind plus undecoded payload bytes.
struct CVMemberRecord {
  /// Leaf kind of this member record.
  TypeLeafKind Kind;
  /// Raw bytes of the member record payload.
  ArrayRef<uint8_t> Data;
};

/// Equvalent to CV_fldattr_t in cvinfo.h.
struct MemberAttributes {
  /// Packed access, method kind, and method option flags.
  uint16_t Attrs = 0;

  /// Bit-field layout constants for \c Attrs.
  enum {
    MethodKindShift = 2, ///< Bit offset of the method kind within \c Attrs.
  };

  /// Construct member attributes with all fields cleared.
  MemberAttributes() = default;

  /// Construct member attributes with the given access specifier.
  ///
  /// \param Access Member access control.
  explicit MemberAttributes(MemberAccess Access)
      : Attrs(static_cast<uint16_t>(Access)) {}

  /// Construct member attributes from access, method kind, and flags.
  ///
  /// \param Access Member access control.
  /// \param Kind Method kind (virtual, static, friend, and so on).
  /// \param Flags Additional method option flags.
  MemberAttributes(MemberAccess Access, MethodKind Kind, MethodOptions Flags) {
    Attrs = static_cast<uint16_t>(Access);
    Attrs |= (static_cast<uint16_t>(Kind) << MethodKindShift);
    Attrs |= static_cast<uint16_t>(Flags);
  }

  /// Get the access specifier. Valid for any kind of member.
  ///
  /// \returns The member access specifier.
  MemberAccess getAccess() const {
    return MemberAccess(unsigned(Attrs) & unsigned(MethodOptions::AccessMask));
  }

  /// Indicates if a method is defined with friend, virtual, static, etc.
  ///
  /// \returns The method kind encoded in the attributes.
  MethodKind getMethodKind() const {
    return MethodKind(
        (unsigned(Attrs) & unsigned(MethodOptions::MethodKindMask)) >>
        MethodKindShift);
  }

  /// Get the flags that are not included in access control or method
  /// properties.
  ///
  /// \returns The method option flags excluding access and method kind.
  MethodOptions getFlags() const {
    return MethodOptions(
        unsigned(Attrs) &
        ~unsigned(MethodOptions::AccessMask | MethodOptions::MethodKindMask));
  }

  /// Is this method virtual.
  ///
  /// \returns True if the method is virtual.
  bool isVirtual() const {
    auto MP = getMethodKind();
    return MP != MethodKind::Vanilla && MP != MethodKind::Friend &&
           MP != MethodKind::Static;
  }

  /// Does this member introduce a new virtual method.
  ///
  /// \returns True if the member introduces a new virtual method.
  bool isIntroducedVirtual() const {
    auto MP = getMethodKind();
    return MP == MethodKind::IntroducingVirtual ||
           MP == MethodKind::PureIntroducingVirtual;
  }

  /// Is this method static.
  ///
  /// \returns True if the method is static.
  bool isStatic() const {
    return getMethodKind() == MethodKind::Static;
  }
};

/// Tail of an LF_POINTER record when the pointer is a pointer-to-member.
class MemberPointerInfo {
public:
  /// Construct empty member-pointer information.
  MemberPointerInfo() = default;

  /// Construct member-pointer information for a containing class and representation.
  ///
  /// \param ContainingType Type index of the class that contains the member.
  /// \param Representation Pointer-to-member representation kind.
  MemberPointerInfo(TypeIndex ContainingType,
                    PointerToMemberRepresentation Representation)
      : ContainingType(ContainingType), Representation(Representation) {}

  /// Return the type index of the containing class.
  ///
  /// \returns The type index of the containing class.
  TypeIndex getContainingType() const { return ContainingType; }
  /// Return the pointer-to-member representation kind.
  ///
  /// \returns The pointer-to-member representation kind.
  PointerToMemberRepresentation getRepresentation() const {
    return Representation;
  }

  /// Type index of the class that contains the member.
  TypeIndex ContainingType;
  /// How the pointer-to-member is represented.
  PointerToMemberRepresentation Representation =
      PointerToMemberRepresentation::Unknown;
};

/// Base class for decoded CodeView type records.
class TypeRecord {
protected:
  /// Construct a type record with an unspecified kind.
  TypeRecord() = default;
  /// Construct a type record with the given kind.
  ///
  /// \param Kind Type record kind.
  explicit TypeRecord(TypeRecordKind Kind) : Kind(Kind) {}

public:
  /// Return the kind of this type record.
  ///
  /// \returns The kind of this type record.
  TypeRecordKind getKind() const { return Kind; }

  /// Kind of this type record.
  TypeRecordKind Kind;
};

/// LF_MODIFIER record describing a type with const/volatile/unaligned modifiers.
class ModifierRecord : public TypeRecord {
public:
  /// Construct an empty modifier record.
  ModifierRecord() = default;
  /// Construct a modifier record with the given kind.
  ///
  /// \param Kind Type record kind.
  explicit ModifierRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  /// Construct a modifier record for \p ModifiedType with \p Modifiers.
  ///
  /// \param ModifiedType Type being modified.
  /// \param Modifiers Modifier flags applied to the type.
  ModifierRecord(TypeIndex ModifiedType, ModifierOptions Modifiers)
      : TypeRecord(TypeRecordKind::Modifier), ModifiedType(ModifiedType),
        Modifiers(Modifiers) {}

  /// Return the type index of the type being modified.
  ///
  /// \returns The type index of the type being modified.
  TypeIndex getModifiedType() const { return ModifiedType; }
  /// Return the modifier flags applied to the type.
  ///
  /// \returns The modifier flags applied to the type.
  ModifierOptions getModifiers() const { return Modifiers; }

  /// Type index of the type being modified.
  TypeIndex ModifiedType;
  /// Modifier flags applied to \c ModifiedType.
  ModifierOptions Modifiers = ModifierOptions::None;
};

/// LF_PROCEDURE record describing a non-member function type.
class ProcedureRecord : public TypeRecord {
public:
  /// Construct an empty procedure record.
  ProcedureRecord() = default;
  /// Construct a procedure record with the given kind.
  ///
  /// \param Kind Type record kind.
  explicit ProcedureRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  /// Construct a procedure record from its calling-convention fields.
  ///
  /// \param ReturnType Return type of the procedure.
  /// \param CallConv Calling convention.
  /// \param Options Function option flags.
  /// \param ParameterCount Number of parameters.
  /// \param ArgumentList Type index of the argument list.
  ProcedureRecord(TypeIndex ReturnType, CallingConvention CallConv,
                  FunctionOptions Options, uint16_t ParameterCount,
                  TypeIndex ArgumentList)
      : TypeRecord(TypeRecordKind::Procedure), ReturnType(ReturnType),
        CallConv(CallConv), Options(Options), ParameterCount(ParameterCount),
        ArgumentList(ArgumentList) {}

  /// Return the return type of the procedure.
  ///
  /// \returns The return type of the procedure.
  TypeIndex getReturnType() const { return ReturnType; }
  /// Return the calling convention.
  ///
  /// \returns The calling convention.
  CallingConvention getCallConv() const { return CallConv; }
  /// Return the function option flags.
  ///
  /// \returns The function option flags.
  FunctionOptions getOptions() const { return Options; }
  /// Return the number of parameters.
  ///
  /// \returns The number of parameters.
  uint16_t getParameterCount() const { return ParameterCount; }
  /// Return the type index of the argument list.
  ///
  /// \returns The type index of the argument list.
  TypeIndex getArgumentList() const { return ArgumentList; }

  /// Return type of the procedure.
  TypeIndex ReturnType;
  /// Calling convention of the procedure.
  CallingConvention CallConv = CallingConvention::NearC;
  /// Function option flags.
  FunctionOptions Options = FunctionOptions::None;
  /// Number of parameters.
  uint16_t ParameterCount = 0;
  /// Type index of the LF_ARGLIST argument list.
  TypeIndex ArgumentList;
};

/// LF_MFUNCTION record describing a member function type.
class MemberFunctionRecord : public TypeRecord {
public:
  /// Construct an empty member function record.
  MemberFunctionRecord() = default;
  /// Construct a member function record with the given kind.
  ///
  /// \param Kind Type record kind.
  explicit MemberFunctionRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}

  /// Construct a member function record from its type and calling fields.
  ///
  /// \param ReturnType Return type of the member function.
  /// \param ClassType Type of the containing class.
  /// \param ThisType Type of the \c this pointer.
  /// \param CallConv Calling convention.
  /// \param Options Function option flags.
  /// \param ParameterCount Number of parameters.
  /// \param ArgumentList Type index of the argument list.
  /// \param ThisPointerAdjustment Byte adjustment applied to \c this.
  MemberFunctionRecord(TypeIndex ReturnType, TypeIndex ClassType,
                       TypeIndex ThisType, CallingConvention CallConv,
                       FunctionOptions Options, uint16_t ParameterCount,
                       TypeIndex ArgumentList, int32_t ThisPointerAdjustment)
      : TypeRecord(TypeRecordKind::MemberFunction), ReturnType(ReturnType),
        ClassType(ClassType), ThisType(ThisType), CallConv(CallConv),
        Options(Options), ParameterCount(ParameterCount),
        ArgumentList(ArgumentList),
        ThisPointerAdjustment(ThisPointerAdjustment) {}

  /// Return the return type of the member function.
  ///
  /// \returns The return type of the member function.
  TypeIndex getReturnType() const { return ReturnType; }
  /// Return the type of the containing class.
  ///
  /// \returns The type of the containing class.
  TypeIndex getClassType() const { return ClassType; }
  /// Return the type of the \c this pointer.
  ///
  /// \returns The type of the \c this pointer.
  TypeIndex getThisType() const { return ThisType; }
  /// Return the calling convention.
  ///
  /// \returns The calling convention.
  CallingConvention getCallConv() const { return CallConv; }
  /// Return the function option flags.
  ///
  /// \returns The function option flags.
  FunctionOptions getOptions() const { return Options; }
  /// Return the number of parameters.
  ///
  /// \returns The number of parameters.
  uint16_t getParameterCount() const { return ParameterCount; }
  /// Return the type index of the argument list.
  ///
  /// \returns The type index of the argument list.
  TypeIndex getArgumentList() const { return ArgumentList; }
  /// Return the byte adjustment applied to the \c this pointer.
  ///
  /// \returns The byte adjustment applied to the \c this pointer.
  int32_t getThisPointerAdjustment() const { return ThisPointerAdjustment; }

  /// Return type of the member function.
  TypeIndex ReturnType;
  /// Type of the containing class.
  TypeIndex ClassType;
  /// Type of the \c this pointer.
  TypeIndex ThisType;
  /// Calling convention of the member function.
  CallingConvention CallConv = CallingConvention::NearC;
  /// Function option flags.
  FunctionOptions Options = FunctionOptions::None;
  /// Number of parameters.
  uint16_t ParameterCount = 0;
  /// Type index of the LF_ARGLIST argument list.
  TypeIndex ArgumentList;
  /// Byte adjustment applied to the \c this pointer.
  int32_t ThisPointerAdjustment = 0;
};

/// LF_LABEL record describing a near or far code label.
class LabelRecord : public TypeRecord {
public:
  /// Construct an empty label record.
  LabelRecord() = default;
  /// Construct a label record with the given kind.
  ///
  /// \param Kind Type record kind.
  explicit LabelRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}

  /// Construct a label record with the given addressing mode.
  ///
  /// \param Mode Near or far label mode.
  LabelRecord(LabelType Mode) : TypeRecord(TypeRecordKind::Label), Mode(Mode) {}

  /// Near or far addressing mode of the label.
  LabelType Mode = LabelType::Near;
};

/// LF_MFUNC_ID record identifying a member function by class, type, and name.
class MemberFuncIdRecord : public TypeRecord {
public:
  /// Construct an empty member function ID record.
  MemberFuncIdRecord() = default;
  /// Construct a member function ID record with the given kind.
  ///
  /// \param Kind Type record kind.
  explicit MemberFuncIdRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  /// Construct a member function ID record from class, type, and name.
  ///
  /// \param ClassType Type of the containing class.
  /// \param FunctionType Type of the member function.
  /// \param Name Name of the member function.
  MemberFuncIdRecord(TypeIndex ClassType, TypeIndex FunctionType,
                         StringRef Name)
      : TypeRecord(TypeRecordKind::MemberFuncId), ClassType(ClassType),
        FunctionType(FunctionType), Name(Name) {}

  /// Return the type of the containing class.
  ///
  /// \returns The type of the containing class.
  TypeIndex getClassType() const { return ClassType; }
  /// Return the type of the member function.
  ///
  /// \returns The type of the member function.
  TypeIndex getFunctionType() const { return FunctionType; }
  /// Return the name of the member function.
  ///
  /// \returns The name of the member function.
  StringRef getName() const { return Name; }

  /// Type of the containing class.
  TypeIndex ClassType;
  /// Type of the member function.
  TypeIndex FunctionType;
  /// Name of the member function.
  StringRef Name;
};

/// LF_ARGLIST record listing the parameter types of a function.
class ArgListRecord : public TypeRecord {
public:
  /// Construct an empty argument list record.
  ArgListRecord() = default;
  /// Construct an argument list record with the given kind.
  ///
  /// \param Kind Type record kind.
  explicit ArgListRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}

  /// Construct an argument list record from kind and type indices.
  ///
  /// \param Kind Type record kind.
  /// \param Indices Type indices of the argument types.
  ArgListRecord(TypeRecordKind Kind, ArrayRef<TypeIndex> Indices)
      : TypeRecord(Kind), ArgIndices(Indices) {}

  /// Return the type indices of the argument types.
  ///
  /// \returns The type indices of the argument types.
  ArrayRef<TypeIndex> getIndices() const { return ArgIndices; }

  /// Type indices of the argument types.
  std::vector<TypeIndex> ArgIndices;
};

/// LF_SUBSTR_LIST record listing substring type indices.
class StringListRecord : public TypeRecord {
public:
  /// Construct an empty string list record.
  StringListRecord() = default;
  /// Construct a string list record with the given kind.
  ///
  /// \param Kind Type record kind.
  explicit StringListRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}

  /// Construct a string list record from kind and string type indices.
  ///
  /// \param Kind Type record kind.
  /// \param Indices Type indices of the listed strings.
  StringListRecord(TypeRecordKind Kind, ArrayRef<TypeIndex> Indices)
      : TypeRecord(Kind), StringIndices(Indices) {}

  /// Return the type indices of the listed strings.
  ///
  /// \returns The type indices of the listed strings.
  ArrayRef<TypeIndex> getIndices() const { return StringIndices; }

  /// Type indices of the listed strings.
  std::vector<TypeIndex> StringIndices;
};

/// LF_POINTER record describing a pointer, reference, or pointer-to-member.
class PointerRecord : public TypeRecord {
public:
  // ---------------------------XXXXX
  /// Bit shift for the pointer kind field in \c Attrs.
  static const uint32_t PointerKindShift = 0;
  /// Bit mask for the pointer kind field in \c Attrs.
  static const uint32_t PointerKindMask = 0x1F;

  // ------------------------XXX-----
  /// Bit shift for the pointer mode field in \c Attrs.
  static const uint32_t PointerModeShift = 5;
  /// Bit mask for the pointer mode field in \c Attrs.
  static const uint32_t PointerModeMask = 0x07;

  // ----------XXX------XXXXX--------
  /// Bit mask for the pointer option flags in \c Attrs.
  static const uint32_t PointerOptionMask = 0x381f00;

  // -------------XXXXXX------------
  /// Bit shift for the pointer size field in \c Attrs.
  static const uint32_t PointerSizeShift = 13;
  /// Bit mask for the pointer size field in \c Attrs.
  static const uint32_t PointerSizeMask = 0xFF;

  /// Construct an empty pointer record.
  PointerRecord() = default;
  /// Construct a pointer record with the given kind.
  ///
  /// \param Kind Type record kind.
  explicit PointerRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}

  /// Construct a pointer record from a referent type and packed attributes.
  ///
  /// \param ReferentType Type pointed to.
  /// \param Attrs Packed pointer attributes.
  PointerRecord(TypeIndex ReferentType, uint32_t Attrs)
      : TypeRecord(TypeRecordKind::Pointer), ReferentType(ReferentType),
        Attrs(Attrs) {}

  /// Construct a pointer record from kind, mode, options, and size.
  ///
  /// \param ReferentType Type pointed to.
  /// \param PK Pointer kind.
  /// \param PM Pointer mode.
  /// \param PO Pointer option flags.
  /// \param Size Size of the pointer in bytes.
  PointerRecord(TypeIndex ReferentType, PointerKind PK, PointerMode PM,
                PointerOptions PO, uint8_t Size)
      : TypeRecord(TypeRecordKind::Pointer), ReferentType(ReferentType),
        Attrs(calcAttrs(PK, PM, PO, Size)) {}

  /// Construct a pointer-to-member record including member-pointer info.
  ///
  /// \param ReferentType Type pointed to.
  /// \param PK Pointer kind.
  /// \param PM Pointer mode.
  /// \param PO Pointer option flags.
  /// \param Size Size of the pointer in bytes.
  /// \param MPI Member-pointer containing type and representation.
  PointerRecord(TypeIndex ReferentType, PointerKind PK, PointerMode PM,
                PointerOptions PO, uint8_t Size, const MemberPointerInfo &MPI)
      : TypeRecord(TypeRecordKind::Pointer), ReferentType(ReferentType),
        Attrs(calcAttrs(PK, PM, PO, Size)), MemberInfo(MPI) {}

  /// Return the type pointed to by this pointer.
  ///
  /// \returns The type pointed to by this pointer.
  TypeIndex getReferentType() const { return ReferentType; }

  /// Return the pointer kind encoded in \c Attrs.
  ///
  /// \returns The pointer kind encoded in \c Attrs.
  PointerKind getPointerKind() const {
    return static_cast<PointerKind>((Attrs >> PointerKindShift) &
                                    PointerKindMask);
  }

  /// Return the pointer mode encoded in \c Attrs.
  ///
  /// \returns The pointer mode encoded in \c Attrs.
  PointerMode getMode() const {
    return static_cast<PointerMode>((Attrs >> PointerModeShift) &
                                    PointerModeMask);
  }

  /// Return the pointer option flags encoded in \c Attrs.
  ///
  /// \returns The pointer option flags encoded in \c Attrs.
  PointerOptions getOptions() const {
    return static_cast<PointerOptions>(Attrs & PointerOptionMask);
  }

  /// Return the pointer size in bytes encoded in \c Attrs.
  ///
  /// \returns The pointer size in bytes encoded in \c Attrs.
  uint8_t getSize() const {
    return (Attrs >> PointerSizeShift) & PointerSizeMask;
  }

  /// Return the member-pointer information for a pointer-to-member.
  ///
  /// \returns The member-pointer information for a pointer-to-member.
  MemberPointerInfo getMemberInfo() const { return *MemberInfo; }

  /// Return true if this is a pointer to a data member or member function.
  ///
  /// \returns True if this is a pointer to a data member or member function.
  bool isPointerToMember() const {
    return getMode() == PointerMode::PointerToDataMember ||
           getMode() == PointerMode::PointerToMemberFunction;
  }

  /// Return true if the pointer uses the flat 32-bit address model.
  ///
  /// \returns True if the pointer uses the flat 32-bit address model.
  bool isFlat() const { return !!(Attrs & uint32_t(PointerOptions::Flat32)); }
  /// Return true if the pointed-to type is const-qualified.
  ///
  /// \returns True if the pointed-to type is const-qualified.
  bool isConst() const { return !!(Attrs & uint32_t(PointerOptions::Const)); }

  /// Return true if the pointed-to type is volatile-qualified.
  ///
  /// \returns True if the pointed-to type is volatile-qualified.
  bool isVolatile() const {
    return !!(Attrs & uint32_t(PointerOptions::Volatile));
  }

  /// Return true if the pointed-to type is unaligned.
  ///
  /// \returns True if the pointed-to type is unaligned.
  bool isUnaligned() const {
    return !!(Attrs & uint32_t(PointerOptions::Unaligned));
  }

  /// Return true if the pointer is restrict-qualified.
  ///
  /// \returns True if the pointer is restrict-qualified.
  bool isRestrict() const {
    return !!(Attrs & uint32_t(PointerOptions::Restrict));
  }

  /// Return true if \c this is an lvalue reference in a member function.
  ///
  /// \returns True if \c this is an lvalue reference in a member function.
  bool isLValueReferenceThisPtr() const {
    return !!(Attrs & uint32_t(PointerOptions::LValueRefThisPointer));
  }

  /// Return true if \c this is an rvalue reference in a member function.
  ///
  /// \returns True if \c this is an rvalue reference in a member function.
  bool isRValueReferenceThisPtr() const {
    return !!(Attrs & uint32_t(PointerOptions::RValueRefThisPointer));
  }

  /// Type index of the type pointed to.
  TypeIndex ReferentType;
  /// Packed pointer kind, mode, options, and size.
  uint32_t Attrs = 0;
  /// Optional member-pointer information when this is a pointer-to-member.
  std::optional<MemberPointerInfo> MemberInfo;

  /// Replace \c Attrs with the packed encoding of the given fields.
  ///
  /// \param PK Pointer kind.
  /// \param PM Pointer mode.
  /// \param PO Pointer option flags.
  /// \param Size Size of the pointer in bytes.
  void setAttrs(PointerKind PK, PointerMode PM, PointerOptions PO,
                uint8_t Size) {
    Attrs = calcAttrs(PK, PM, PO, Size);
  }

private:
  static uint32_t calcAttrs(PointerKind PK, PointerMode PM, PointerOptions PO,
                            uint8_t Size) {
    uint32_t A = 0;
    A |= static_cast<uint32_t>(PK);
    A |= static_cast<uint32_t>(PO);
    A |= (static_cast<uint32_t>(PM) << PointerModeShift);
    A |= (static_cast<uint32_t>(Size) << PointerSizeShift);
    return A;
  }
};

/// LF_NESTTYPE record naming a nested type within a class.
class NestedTypeRecord : public TypeRecord {
public:
  /// Construct an empty nested type record.
  NestedTypeRecord() = default;
  /// Construct a nested type record with the given kind.
  ///
  /// \param Kind Type record kind.
  explicit NestedTypeRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  /// Construct a nested type record from type and name.
  ///
  /// \param Type Type index of the nested type.
  /// \param Name Name of the nested type.
  NestedTypeRecord(TypeIndex Type, StringRef Name)
      : TypeRecord(TypeRecordKind::NestedType), Type(Type), Name(Name) {}

  /// Return the type index of the nested type.
  ///
  /// \returns The type index of the nested type.
  TypeIndex getNestedType() const { return Type; }
  /// Return the name of the nested type.
  ///
  /// \returns The name of the nested type.
  StringRef getName() const { return Name; }

  /// Type index of the nested type.
  TypeIndex Type;
  /// Name of the nested type.
  StringRef Name;
};

/// LF_FIELDLIST record holding the raw bytes of a class or enum field list.
class FieldListRecord : public TypeRecord {
public:
  /// Construct an empty field list record.
  FieldListRecord() = default;
  /// Construct a field list record with the given kind.
  ///
  /// \param Kind Type record kind.
  explicit FieldListRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  /// Construct a field list record from raw field-list bytes.
  ///
  /// \param Data Serialized field list payload.
  explicit FieldListRecord(ArrayRef<uint8_t> Data)
      : TypeRecord(TypeRecordKind::FieldList), Data(Data) {}

  /// Serialized field list payload bytes.
  ArrayRef<uint8_t> Data;
};

/// LF_ARRAY record describing an array type.
class ArrayRecord : public TypeRecord {
public:
  /// Construct an empty array record.
  ArrayRecord() = default;
  /// Construct an array record with the given kind.
  ///
  /// \param Kind Type record kind.
  explicit ArrayRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  /// Construct an array record from element type, index type, size, and name.
  ///
  /// \param ElementType Type of each array element.
  /// \param IndexType Type used to index the array.
  /// \param Size Total size of the array in bytes.
  /// \param Name Optional name of the array type.
  ArrayRecord(TypeIndex ElementType, TypeIndex IndexType, uint64_t Size,
              StringRef Name)
      : TypeRecord(TypeRecordKind::Array), ElementType(ElementType),
        IndexType(IndexType), Size(Size), Name(Name) {}

  /// Return the type of each array element.
  ///
  /// \returns The type of each array element.
  TypeIndex getElementType() const { return ElementType; }
  /// Return the type used to index the array.
  ///
  /// \returns The type used to index the array.
  TypeIndex getIndexType() const { return IndexType; }
  /// Return the total size of the array in bytes.
  ///
  /// \returns The total size of the array in bytes.
  uint64_t getSize() const { return Size; }
  /// Return the optional name of the array type.
  ///
  /// \returns The optional name of the array type.
  StringRef getName() const { return Name; }

  /// Type of each array element.
  TypeIndex ElementType;
  /// Type used to index the array.
  TypeIndex IndexType;
  /// Total size of the array in bytes.
  uint64_t Size = 0;
  /// Optional name of the array type.
  StringRef Name;
};

/// Shared base for LF_CLASS, LF_STRUCTURE, LF_INTERFACE, LF_UNION, and LF_ENUM.
class TagRecord : public TypeRecord {
protected:
  /// Construct an empty tag record.
  TagRecord() = default;
  /// Construct a tag record with the given kind.
  ///
  /// \param Kind Type record kind.
  explicit TagRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  /// Construct a tag record from member count, options, field list, and names.
  ///
  /// \param Kind Type record kind.
  /// \param MemberCount Number of members in the tag.
  /// \param Options Class option flags.
  /// \param FieldList Type index of the field list.
  /// \param Name Display name of the tag.
  /// \param UniqueName Unique decorated name of the tag, if present.
  TagRecord(TypeRecordKind Kind, uint16_t MemberCount, ClassOptions Options,
            TypeIndex FieldList, StringRef Name, StringRef UniqueName)
      : TypeRecord(Kind), MemberCount(MemberCount), Options(Options),
        FieldList(FieldList), Name(Name), UniqueName(UniqueName) {}

public:
  /// Bit shift for the HFA kind within \c Options.
  static const int HfaKindShift = 11;
  /// Bit mask for the HFA kind within \c Options.
  static const int HfaKindMask = 0x1800;
  /// Bit shift for the WinRT class kind within \c Options.
  static const int WinRTKindShift = 14;
  /// Bit mask for the WinRT class kind within \c Options.
  static const int WinRTKindMask = 0xC000;

  /// Return true if this tag carries a unique decorated name.
  ///
  /// \returns True if this tag carries a unique decorated name.
  bool hasUniqueName() const {
    return (Options & ClassOptions::HasUniqueName) != ClassOptions::None;
  }

  /// Return true if this tag is nested inside another type.
  ///
  /// \returns True if this tag is nested inside another type.
  bool isNested() const {
    return (Options & ClassOptions::Nested) != ClassOptions::None;
  }

  /// Return true if this record is a forward reference.
  ///
  /// \returns True if this record is a forward reference.
  bool isForwardRef() const {
    return (Options & ClassOptions::ForwardReference) != ClassOptions::None;
  }

  /// Return true if this tag contains a nested class.
  ///
  /// \returns True if this tag contains a nested class.
  bool containsNestedClass() const {
    return (Options & ClassOptions::ContainsNestedClass) != ClassOptions::None;
  }

  /// Return true if this tag is scoped (local to a function).
  ///
  /// \returns True if this tag is scoped (local to a function).
  bool isScoped() const {
    return (Options & ClassOptions::Scoped) != ClassOptions::None;
  }

  /// Return the number of members in the tag.
  ///
  /// \returns The number of members in the tag.
  uint16_t getMemberCount() const { return MemberCount; }
  /// Return the class option flags.
  ///
  /// \returns The class option flags.
  ClassOptions getOptions() const { return Options; }
  /// Return the type index of the field list.
  ///
  /// \returns The type index of the field list.
  TypeIndex getFieldList() const { return FieldList; }
  /// Return the display name of the tag.
  ///
  /// \returns The display name of the tag.
  StringRef getName() const { return Name; }
  /// Return the unique decorated name of the tag.
  ///
  /// \returns The unique decorated name of the tag.
  StringRef getUniqueName() const { return UniqueName; }

  /// Number of members in the tag.
  uint16_t MemberCount = 0;
  /// Class option flags.
  ClassOptions Options = ClassOptions::None;
  /// Type index of the LF_FIELDLIST.
  TypeIndex FieldList;
  /// Display name of the tag.
  StringRef Name;
  /// Unique decorated name of the tag, if present.
  StringRef UniqueName;
};

/// LF_CLASS, LF_STRUCTURE, or LF_INTERFACE record describing a class-like type.
class ClassRecord : public TagRecord {
public:
  /// Construct an empty class record.
  ClassRecord() = default;
  /// Construct a class record with the given kind.
  ///
  /// \param Kind Type record kind.
  explicit ClassRecord(TypeRecordKind Kind) : TagRecord(Kind) {}
  /// Construct a class record from its layout and naming fields.
  ///
  /// \param Kind Type record kind.
  /// \param MemberCount Number of members.
  /// \param Options Class option flags.
  /// \param FieldList Type index of the field list.
  /// \param DerivationList Type index of the derivation list.
  /// \param VTableShape Type index of the vtable shape.
  /// \param Size Size of the class in bytes.
  /// \param Name Display name of the class.
  /// \param UniqueName Unique decorated name, if present.
  ClassRecord(TypeRecordKind Kind, uint16_t MemberCount, ClassOptions Options,
              TypeIndex FieldList, TypeIndex DerivationList,
              TypeIndex VTableShape, uint64_t Size, StringRef Name,
              StringRef UniqueName)
      : TagRecord(Kind, MemberCount, Options, FieldList, Name, UniqueName),
        DerivationList(DerivationList), VTableShape(VTableShape), Size(Size) {}

  /// Return the homogeneous floating-point aggregate kind.
  ///
  /// \returns The homogeneous floating-point aggregate kind.
  HfaKind getHfa() const {
    uint16_t Value = static_cast<uint16_t>(Options);
    Value = (Value & HfaKindMask) >> HfaKindShift;
    return static_cast<HfaKind>(Value);
  }

  /// Return the Windows Runtime class kind.
  ///
  /// \returns The Windows Runtime class kind.
  WindowsRTClassKind getWinRTKind() const {
    uint16_t Value = static_cast<uint16_t>(Options);
    Value = (Value & WinRTKindMask) >> WinRTKindShift;
    return static_cast<WindowsRTClassKind>(Value);
  }

  /// Return the type index of the derivation list.
  ///
  /// \returns The type index of the derivation list.
  TypeIndex getDerivationList() const { return DerivationList; }
  /// Return the type index of the vtable shape.
  ///
  /// \returns The type index of the vtable shape.
  TypeIndex getVTableShape() const { return VTableShape; }
  /// Return the size of the class in bytes.
  ///
  /// \returns The size of the class in bytes.
  uint64_t getSize() const { return Size; }

  /// Type index of the derivation list.
  TypeIndex DerivationList;
  /// Type index of the vtable shape.
  TypeIndex VTableShape;
  /// Size of the class in bytes.
  uint64_t Size = 0;
};

/// LF_UNION record describing a union type.
struct UnionRecord : public TagRecord {
  /// Construct an empty union record.
  UnionRecord() = default;
  /// Construct a union record with the given kind.
  ///
  /// \param Kind Type record kind.
  explicit UnionRecord(TypeRecordKind Kind) : TagRecord(Kind) {}
  /// Construct a union record from its layout and naming fields.
  ///
  /// \param MemberCount Number of members.
  /// \param Options Class option flags.
  /// \param FieldList Type index of the field list.
  /// \param Size Size of the union in bytes.
  /// \param Name Display name of the union.
  /// \param UniqueName Unique decorated name, if present.
  UnionRecord(uint16_t MemberCount, ClassOptions Options, TypeIndex FieldList,
              uint64_t Size, StringRef Name, StringRef UniqueName)
      : TagRecord(TypeRecordKind::Union, MemberCount, Options, FieldList, Name,
                  UniqueName),
        Size(Size) {}

  /// Return the homogeneous floating-point aggregate kind.
  ///
  /// \returns The homogeneous floating-point aggregate kind.
  HfaKind getHfa() const {
    uint16_t Value = static_cast<uint16_t>(Options);
    Value = (Value & HfaKindMask) >> HfaKindShift;
    return static_cast<HfaKind>(Value);
  }

  /// Return the size of the union in bytes.
  ///
  /// \returns The size of the union in bytes.
  uint64_t getSize() const { return Size; }

  /// Size of the union in bytes.
  uint64_t Size = 0;
};

/// LF_ENUM record describing an enumeration type.
class EnumRecord : public TagRecord {
public:
  /// Construct an empty enum record.
  EnumRecord() = default;
  /// Construct an enum record with the given kind.
  ///
  /// \param Kind Type record kind.
  explicit EnumRecord(TypeRecordKind Kind) : TagRecord(Kind) {}
  /// Construct an enum record from its members, names, and underlying type.
  ///
  /// \param MemberCount Number of enumerators.
  /// \param Options Class option flags.
  /// \param FieldList Type index of the field list.
  /// \param Name Display name of the enumeration.
  /// \param UniqueName Unique decorated name, if present.
  /// \param UnderlyingType Underlying integral type of the enumeration.
  EnumRecord(uint16_t MemberCount, ClassOptions Options, TypeIndex FieldList,
             StringRef Name, StringRef UniqueName, TypeIndex UnderlyingType)
      : TagRecord(TypeRecordKind::Enum, MemberCount, Options, FieldList, Name,
                  UniqueName),
        UnderlyingType(UnderlyingType) {}

  /// Return the underlying integral type of the enumeration.
  ///
  /// \returns The underlying integral type of the enumeration.
  TypeIndex getUnderlyingType() const { return UnderlyingType; }

  /// Underlying integral type of the enumeration.
  TypeIndex UnderlyingType;
};

/// LF_BITFIELD record describing a bit-field member type.
class BitFieldRecord : public TypeRecord {
public:
  /// Construct an empty bit-field record.
  BitFieldRecord() = default;
  /// Construct a bit-field record with the given kind.
  ///
  /// \param Kind Type record kind.
  explicit BitFieldRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  /// Construct a bit-field record from storage type, size, and offset.
  ///
  /// \param Type Storage type of the bit-field.
  /// \param BitSize Width of the bit-field in bits.
  /// \param BitOffset Bit offset within the storage type.
  BitFieldRecord(TypeIndex Type, uint8_t BitSize, uint8_t BitOffset)
      : TypeRecord(TypeRecordKind::BitField), Type(Type), BitSize(BitSize),
        BitOffset(BitOffset) {}

  /// Return the storage type of the bit-field.
  ///
  /// \returns The storage type of the bit-field.
  TypeIndex getType() const { return Type; }
  /// Return the bit offset within the storage type.
  ///
  /// \returns The bit offset within the storage type.
  uint8_t getBitOffset() const { return BitOffset; }
  /// Return the width of the bit-field in bits.
  ///
  /// \returns The width of the bit-field in bits.
  uint8_t getBitSize() const { return BitSize; }

  /// Storage type of the bit-field.
  TypeIndex Type;
  /// Width of the bit-field in bits.
  uint8_t BitSize = 0;
  /// Bit offset within the storage type.
  uint8_t BitOffset = 0;
};

/// LF_VTSHAPE record describing the shape of a virtual function table.
class VFTableShapeRecord : public TypeRecord {
public:
  /// Construct an empty vtable shape record.
  VFTableShapeRecord() = default;
  /// Construct a vtable shape record with the given kind.
  ///
  /// \param Kind Type record kind.
  explicit VFTableShapeRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  /// Construct a vtable shape record from a referenced slot array.
  ///
  /// \param Slots Virtual function table slot kinds.
  explicit VFTableShapeRecord(ArrayRef<VFTableSlotKind> Slots)
      : TypeRecord(TypeRecordKind::VFTableShape), SlotsRef(Slots) {}
  /// Construct a vtable shape record that owns its slot array.
  ///
  /// \param Slots Virtual function table slot kinds.
  explicit VFTableShapeRecord(std::vector<VFTableSlotKind> Slots)
      : TypeRecord(TypeRecordKind::VFTableShape), Slots(std::move(Slots)) {}

  /// Return the virtual function table slot kinds.
  ///
  /// \returns The virtual function table slot kinds.
  ArrayRef<VFTableSlotKind> getSlots() const {
    if (!SlotsRef.empty())
      return SlotsRef;
    return Slots;
  }

  /// Return the number of entries in the vtable shape.
  ///
  /// \returns The number of entries in the vtable shape.
  uint32_t getEntryCount() const { return getSlots().size(); }

  /// Non-owning view of the vtable slot kinds, when available.
  ArrayRef<VFTableSlotKind> SlotsRef;
  /// Owned storage for the vtable slot kinds.
  std::vector<VFTableSlotKind> Slots;
};

/// LF_TYPESERVER2 record referencing an external type server PDB.
class TypeServer2Record : public TypeRecord {
public:
  /// Construct an empty type server record.
  TypeServer2Record() = default;
  /// Construct a type server record with the given kind.
  ///
  /// \param Kind Type record kind.
  explicit TypeServer2Record(TypeRecordKind Kind) : TypeRecord(Kind) {}
  /// Construct a type server record from GUID, age, and PDB name.
  ///
  /// \param GuidStr Sixteen-byte GUID of the type server PDB.
  /// \param Age Age of the type server PDB.
  /// \param Name Path or name of the type server PDB.
  TypeServer2Record(StringRef GuidStr, uint32_t Age, StringRef Name)
      : TypeRecord(TypeRecordKind::TypeServer2), Age(Age), Name(Name) {
    assert(GuidStr.size() == 16 && "guid isn't 16 bytes");
    ::memcpy(Guid.Guid, GuidStr.data(), 16);
  }

  /// Return the GUID of the type server PDB.
  ///
  /// \returns The GUID of the type server PDB.
  const GUID &getGuid() const { return Guid; }
  /// Return the age of the type server PDB.
  ///
  /// \returns The age of the type server PDB.
  uint32_t getAge() const { return Age; }
  /// Return the path or name of the type server PDB.
  ///
  /// \returns The path or name of the type server PDB.
  StringRef getName() const { return Name; }

  /// GUID of the type server PDB.
  GUID Guid = {};
  /// Age of the type server PDB.
  uint32_t Age = 0;
  /// Path or name of the type server PDB.
  StringRef Name;
};

/// LF_STRING_ID record binding a string to an optional ID type index.
class StringIdRecord : public TypeRecord {
public:
  /// Construct an empty string ID record.
  StringIdRecord() = default;
  /// Construct a string ID record with the given kind.
  ///
  /// \param Kind Type record kind.
  explicit StringIdRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  /// Construct a string ID record from ID and string.
  ///
  /// \param Id Optional substring or related type index.
  /// \param String String contents.
  StringIdRecord(TypeIndex Id, StringRef String)
      : TypeRecord(TypeRecordKind::StringId), Id(Id), String(String) {}

  /// Return the optional related type index.
  ///
  /// \returns The optional related type index.
  TypeIndex getId() const { return Id; }
  /// Return the string contents.
  ///
  /// \returns The string contents.
  StringRef getString() const { return String; }

  /// Optional substring or related type index.
  TypeIndex Id;
  /// String contents.
  StringRef String;
};

/// LF_FUNC_ID record identifying a non-member function by scope, type, and name.
class FuncIdRecord : public TypeRecord {
public:
  /// Construct an empty function ID record.
  FuncIdRecord() = default;
  /// Construct a function ID record with the given kind.
  ///
  /// \param Kind Type record kind.
  explicit FuncIdRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  /// Construct a function ID record from scope, type, and name.
  ///
  /// \param ParentScope Type index of the parent scope.
  /// \param FunctionType Type of the function.
  /// \param Name Name of the function.
  FuncIdRecord(TypeIndex ParentScope, TypeIndex FunctionType, StringRef Name)
      : TypeRecord(TypeRecordKind::FuncId), ParentScope(ParentScope),
        FunctionType(FunctionType), Name(Name) {}

  /// Return the type index of the parent scope.
  ///
  /// \returns The type index of the parent scope.
  TypeIndex getParentScope() const { return ParentScope; }
  /// Return the type of the function.
  ///
  /// \returns The type of the function.
  TypeIndex getFunctionType() const { return FunctionType; }
  /// Return the name of the function.
  ///
  /// \returns The name of the function.
  StringRef getName() const { return Name; }

  /// Type index of the parent scope.
  TypeIndex ParentScope;
  /// Type of the function.
  TypeIndex FunctionType;
  /// Name of the function.
  StringRef Name;
};

/// LF_UDT_SRC_LINE record mapping a UDT to a source file and line.
class UdtSourceLineRecord : public TypeRecord {
public:
  /// Construct an empty UDT source line record.
  UdtSourceLineRecord() = default;
  /// Construct a UDT source line record with the given kind.
  ///
  /// \param Kind Type record kind.
  explicit UdtSourceLineRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  /// Construct a UDT source line record from UDT, file, and line.
  ///
  /// \param UDT Type index of the user-defined type.
  /// \param SourceFile Type index of the source file string.
  /// \param LineNumber Source line number.
  UdtSourceLineRecord(TypeIndex UDT, TypeIndex SourceFile, uint32_t LineNumber)
      : TypeRecord(TypeRecordKind::UdtSourceLine), UDT(UDT),
        SourceFile(SourceFile), LineNumber(LineNumber) {}

  /// Return the type index of the user-defined type.
  ///
  /// \returns The type index of the user-defined type.
  TypeIndex getUDT() const { return UDT; }
  /// Return the type index of the source file string.
  ///
  /// \returns The type index of the source file string.
  TypeIndex getSourceFile() const { return SourceFile; }
  /// Return the source line number.
  ///
  /// \returns The source line number.
  uint32_t getLineNumber() const { return LineNumber; }

  /// Type index of the user-defined type.
  TypeIndex UDT;
  /// Type index of the source file string.
  TypeIndex SourceFile;
  /// Source line number.
  uint32_t LineNumber = 0;
};

/// LF_UDT_MOD_SRC_LINE record mapping a UDT to a module, source file, and line.
class UdtModSourceLineRecord : public TypeRecord {
public:
  /// Construct an empty UDT module source line record.
  UdtModSourceLineRecord() = default;
  /// Construct a UDT module source line record with the given kind.
  ///
  /// \param Kind Type record kind.
  explicit UdtModSourceLineRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  /// Construct a UDT module source line record from UDT, file, line, and module.
  ///
  /// \param UDT Type index of the user-defined type.
  /// \param SourceFile Type index of the source file string.
  /// \param LineNumber Source line number.
  /// \param Module Module index that owns the source file.
  UdtModSourceLineRecord(TypeIndex UDT, TypeIndex SourceFile,
                         uint32_t LineNumber, uint16_t Module)
      : TypeRecord(TypeRecordKind::UdtSourceLine), UDT(UDT),
        SourceFile(SourceFile), LineNumber(LineNumber), Module(Module) {}

  /// Return the type index of the user-defined type.
  ///
  /// \returns The type index of the user-defined type.
  TypeIndex getUDT() const { return UDT; }
  /// Return the type index of the source file string.
  ///
  /// \returns The type index of the source file string.
  TypeIndex getSourceFile() const { return SourceFile; }
  /// Return the source line number.
  ///
  /// \returns The source line number.
  uint32_t getLineNumber() const { return LineNumber; }
  /// Return the module index that owns the source file.
  ///
  /// \returns The module index that owns the source file.
  uint16_t getModule() const { return Module; }

  /// Type index of the user-defined type.
  TypeIndex UDT;
  /// Type index of the source file string.
  TypeIndex SourceFile;
  /// Source line number.
  uint32_t LineNumber = 0;
  /// Module index that owns the source file.
  uint16_t Module = 0;
};

/// LF_BUILDINFO record describing how a translation unit was built.
class BuildInfoRecord : public TypeRecord {
public:
  /// Construct an empty build info record.
  BuildInfoRecord() = default;
  /// Construct a build info record with the given kind.
  ///
  /// \param Kind Type record kind.
  explicit BuildInfoRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  /// Construct a build info record from argument type indices.
  ///
  /// \param ArgIndices Type indices of the build info arguments.
  BuildInfoRecord(ArrayRef<TypeIndex> ArgIndices)
      : TypeRecord(TypeRecordKind::BuildInfo), ArgIndices(ArgIndices) {}

  /// Return the type indices of the build info arguments.
  ///
  /// \returns The type indices of the build info arguments.
  ArrayRef<TypeIndex> getArgs() const { return ArgIndices; }

  /// Indices of known build info arguments.
  enum BuildInfoArg {
    CurrentDirectory, ///< Absolute CWD path
    BuildTool,        ///< Absolute compiler path
    SourceFile,       ///< Path to main source file, relative or absolute
    TypeServerPDB,    ///< Absolute path of type server PDB (/Fd)
    CommandLine,      ///< Full canonical command line (maybe -cc1)
    MaxArgs           ///< Number of known build info argument slots.
  };

  /// Type indices of the build info arguments.
  SmallVector<TypeIndex, MaxArgs> ArgIndices;
};

/// LF_VFTABLE record describing a virtual function table.
class VFTableRecord : public TypeRecord {
public:
  /// Construct an empty vtable record.
  VFTableRecord() = default;
  /// Construct a vtable record with the given kind.
  ///
  /// \param Kind Type record kind.
  explicit VFTableRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  /// Construct a vtable record from class, override, offset, and method names.
  ///
  /// \param CompleteClass Type index of the complete class.
  /// \param OverriddenVFTable Type index of the overridden vtable, if any.
  /// \param VFPtrOffset Offset of the vtable pointer within the class.
  /// \param Name Name of the vtable.
  /// \param Methods Names of the methods in the vtable.
  VFTableRecord(TypeIndex CompleteClass, TypeIndex OverriddenVFTable,
                uint32_t VFPtrOffset, StringRef Name,
                ArrayRef<StringRef> Methods)
      : TypeRecord(TypeRecordKind::VFTable), CompleteClass(CompleteClass),
        OverriddenVFTable(OverriddenVFTable), VFPtrOffset(VFPtrOffset) {
    MethodNames.push_back(Name);
    llvm::append_range(MethodNames, Methods);
  }

  /// Return the type index of the complete class.
  ///
  /// \returns The type index of the complete class.
  TypeIndex getCompleteClass() const { return CompleteClass; }
  /// Return the type index of the overridden vtable, if any.
  ///
  /// \returns The type index of the overridden vtable, if any.
  TypeIndex getOverriddenVTable() const { return OverriddenVFTable; }
  /// Return the offset of the vtable pointer within the class.
  ///
  /// \returns The offset of the vtable pointer within the class.
  uint32_t getVFPtrOffset() const { return VFPtrOffset; }
  /// Return the name of the vtable.
  ///
  /// \returns The name of the vtable.
  StringRef getName() const { return ArrayRef(MethodNames).front(); }

  /// Return the names of the methods in the vtable.
  ///
  /// \returns The names of the methods in the vtable.
  ArrayRef<StringRef> getMethodNames() const {
    return ArrayRef(MethodNames).drop_front();
  }

  /// Type index of the complete class.
  TypeIndex CompleteClass;
  /// Type index of the overridden vtable, if any.
  TypeIndex OverriddenVFTable;
  /// Offset of the vtable pointer within the class.
  uint32_t VFPtrOffset = 0;
  /// Vtable name followed by the method names.
  std::vector<StringRef> MethodNames;
};

/// LF_ONEMETHOD record describing a single method in a field list.
class OneMethodRecord : public TypeRecord {
public:
  /// Construct an empty one-method record.
  OneMethodRecord() = default;
  /// Construct a one-method record with the given kind.
  ///
  /// \param Kind Type record kind.
  explicit OneMethodRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  /// Construct a one-method record from type, attributes, offset, and name.
  ///
  /// \param Type Type of the method.
  /// \param Attrs Member attributes for the method.
  /// \param VFTableOffset Offset into the vtable for introducing virtuals.
  /// \param Name Name of the method.
  OneMethodRecord(TypeIndex Type, MemberAttributes Attrs, int32_t VFTableOffset,
                  StringRef Name)
      : TypeRecord(TypeRecordKind::OneMethod), Type(Type), Attrs(Attrs),
        VFTableOffset(VFTableOffset), Name(Name) {}
  /// Construct a one-method record from access, kind, options, offset, and name.
  ///
  /// \param Type Type of the method.
  /// \param Access Member access control.
  /// \param MK Method kind.
  /// \param Options Method option flags.
  /// \param VFTableOffset Offset into the vtable for introducing virtuals.
  /// \param Name Name of the method.
  OneMethodRecord(TypeIndex Type, MemberAccess Access, MethodKind MK,
                  MethodOptions Options, int32_t VFTableOffset, StringRef Name)
      : TypeRecord(TypeRecordKind::OneMethod), Type(Type),
        Attrs(Access, MK, Options), VFTableOffset(VFTableOffset), Name(Name) {}

  /// Return the type of the method.
  ///
  /// \returns The type of the method.
  TypeIndex getType() const { return Type; }
  /// Return the method kind.
  ///
  /// \returns The method kind.
  MethodKind getMethodKind() const { return Attrs.getMethodKind(); }
  /// Return the method option flags.
  ///
  /// \returns The method option flags.
  MethodOptions getOptions() const { return Attrs.getFlags(); }
  /// Return the member access control.
  ///
  /// \returns The member access control.
  MemberAccess getAccess() const { return Attrs.getAccess(); }
  /// Return the offset into the vtable for introducing virtuals.
  ///
  /// \returns The offset into the vtable for introducing virtuals.
  int32_t getVFTableOffset() const { return VFTableOffset; }
  /// Return the name of the method.
  ///
  /// \returns The name of the method.
  StringRef getName() const { return Name; }

  /// Return true if this method introduces a new virtual function.
  ///
  /// \returns True if this method introduces a new virtual function.
  bool isIntroducingVirtual() const {
    return getMethodKind() == MethodKind::IntroducingVirtual ||
           getMethodKind() == MethodKind::PureIntroducingVirtual;
  }

  /// Type of the method.
  TypeIndex Type;
  /// Member attributes for the method.
  MemberAttributes Attrs;
  /// Offset into the vtable for introducing virtuals.
  int32_t VFTableOffset = 0;
  /// Name of the method.
  StringRef Name;
};

/// LF_METHODLIST record listing the overloads of a method name.
class MethodOverloadListRecord : public TypeRecord {
public:
  /// Construct an empty method overload list record.
  MethodOverloadListRecord() = default;
  /// Construct a method overload list record with the given kind.
  ///
  /// \param Kind Type record kind.
  explicit MethodOverloadListRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  /// Construct a method overload list record from the overload methods.
  ///
  /// \param Methods Overload method records.
  MethodOverloadListRecord(ArrayRef<OneMethodRecord> Methods)
      : TypeRecord(TypeRecordKind::MethodOverloadList), Methods(Methods) {}

  /// Return the overload method records.
  ///
  /// \returns The overload method records.
  ArrayRef<OneMethodRecord> getMethods() const { return Methods; }

  /// Overload method records.
  std::vector<OneMethodRecord> Methods;
};

/// For method overload sets.  LF_METHOD
class OverloadedMethodRecord : public TypeRecord {
public:
  /// Construct an empty overloaded method record.
  OverloadedMethodRecord() = default;
  /// Construct an overloaded method record with the given kind.
  ///
  /// \param Kind Type record kind.
  explicit OverloadedMethodRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  /// Construct an overloaded method record from count, list, and name.
  ///
  /// \param NumOverloads Number of overloads in the set.
  /// \param MethodList Type index of the method overload list.
  /// \param Name Name of the overloaded method.
  OverloadedMethodRecord(uint16_t NumOverloads, TypeIndex MethodList,
                         StringRef Name)
      : TypeRecord(TypeRecordKind::OverloadedMethod),
        NumOverloads(NumOverloads), MethodList(MethodList), Name(Name) {}

  /// Return the number of overloads in the set.
  ///
  /// \returns The number of overloads in the set.
  uint16_t getNumOverloads() const { return NumOverloads; }
  /// Return the type index of the method overload list.
  ///
  /// \returns The type index of the method overload list.
  TypeIndex getMethodList() const { return MethodList; }
  /// Return the name of the overloaded method.
  ///
  /// \returns The name of the overloaded method.
  StringRef getName() const { return Name; }

  /// Number of overloads in the set.
  uint16_t NumOverloads = 0;
  /// Type index of the LF_METHODLIST.
  TypeIndex MethodList;
  /// Name of the overloaded method.
  StringRef Name;
};

/// LF_MEMBER record describing a non-static data member.
class DataMemberRecord : public TypeRecord {
public:
  /// Construct an empty data member record.
  DataMemberRecord() = default;
  /// Construct a data member record with the given kind.
  ///
  /// \param Kind Type record kind.
  explicit DataMemberRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  /// Construct a data member record from attributes, type, offset, and name.
  ///
  /// \param Attrs Member attributes.
  /// \param Type Type of the data member.
  /// \param Offset Byte offset of the member within the class.
  /// \param Name Name of the data member.
  DataMemberRecord(MemberAttributes Attrs, TypeIndex Type, uint64_t Offset,
                   StringRef Name)
      : TypeRecord(TypeRecordKind::DataMember), Attrs(Attrs), Type(Type),
        FieldOffset(Offset), Name(Name) {}
  /// Construct a data member record from access, type, offset, and name.
  ///
  /// \param Access Member access control.
  /// \param Type Type of the data member.
  /// \param Offset Byte offset of the member within the class.
  /// \param Name Name of the data member.
  DataMemberRecord(MemberAccess Access, TypeIndex Type, uint64_t Offset,
                   StringRef Name)
      : TypeRecord(TypeRecordKind::DataMember), Attrs(Access), Type(Type),
        FieldOffset(Offset), Name(Name) {}

  /// Return the member access control.
  ///
  /// \returns The member access control.
  MemberAccess getAccess() const { return Attrs.getAccess(); }
  /// Return the type of the data member.
  ///
  /// \returns The type of the data member.
  TypeIndex getType() const { return Type; }
  /// Return the byte offset of the member within the class.
  ///
  /// \returns The byte offset of the member within the class.
  uint64_t getFieldOffset() const { return FieldOffset; }
  /// Return the name of the data member.
  ///
  /// \returns The name of the data member.
  StringRef getName() const { return Name; }

  /// Member attributes.
  MemberAttributes Attrs;
  /// Type of the data member.
  TypeIndex Type;
  /// Byte offset of the member within the class.
  uint64_t FieldOffset = 0;
  /// Name of the data member.
  StringRef Name;
};

/// LF_STMEMBER record describing a static data member.
class StaticDataMemberRecord : public TypeRecord {
public:
  /// Construct an empty static data member record.
  StaticDataMemberRecord() = default;
  /// Construct a static data member record with the given kind.
  ///
  /// \param Kind Type record kind.
  explicit StaticDataMemberRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  /// Construct a static data member record from attributes, type, and name.
  ///
  /// \param Attrs Member attributes.
  /// \param Type Type of the static data member.
  /// \param Name Name of the static data member.
  StaticDataMemberRecord(MemberAttributes Attrs, TypeIndex Type, StringRef Name)
      : TypeRecord(TypeRecordKind::StaticDataMember), Attrs(Attrs), Type(Type),
        Name(Name) {}
  /// Construct a static data member record from access, type, and name.
  ///
  /// \param Access Member access control.
  /// \param Type Type of the static data member.
  /// \param Name Name of the static data member.
  StaticDataMemberRecord(MemberAccess Access, TypeIndex Type, StringRef Name)
      : TypeRecord(TypeRecordKind::StaticDataMember), Attrs(Access), Type(Type),
        Name(Name) {}

  /// Return the member access control.
  ///
  /// \returns The member access control.
  MemberAccess getAccess() const { return Attrs.getAccess(); }
  /// Return the type of the static data member.
  ///
  /// \returns The type of the static data member.
  TypeIndex getType() const { return Type; }
  /// Return the name of the static data member.
  ///
  /// \returns The name of the static data member.
  StringRef getName() const { return Name; }

  /// Member attributes.
  MemberAttributes Attrs;
  /// Type of the static data member.
  TypeIndex Type;
  /// Name of the static data member.
  StringRef Name;
};

/// LF_ENUMERATE record describing a single enumerator constant.
class EnumeratorRecord : public TypeRecord {
public:
  /// Construct an empty enumerator record.
  EnumeratorRecord() = default;
  /// Construct an enumerator record with the given kind.
  ///
  /// \param Kind Type record kind.
  explicit EnumeratorRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  /// Construct an enumerator record from attributes, value, and name.
  ///
  /// \param Attrs Member attributes.
  /// \param Value Enumerator constant value.
  /// \param Name Name of the enumerator.
  EnumeratorRecord(MemberAttributes Attrs, APSInt Value, StringRef Name)
      : TypeRecord(TypeRecordKind::Enumerator), Attrs(Attrs),
        Value(std::move(Value)), Name(Name) {}
  /// Construct an enumerator record from access, value, and name.
  ///
  /// \param Access Member access control.
  /// \param Value Enumerator constant value.
  /// \param Name Name of the enumerator.
  EnumeratorRecord(MemberAccess Access, APSInt Value, StringRef Name)
      : TypeRecord(TypeRecordKind::Enumerator), Attrs(Access),
        Value(std::move(Value)), Name(Name) {}

  /// Return the member access control.
  ///
  /// \returns The member access control.
  MemberAccess getAccess() const { return Attrs.getAccess(); }
  /// Return the enumerator constant value.
  ///
  /// \returns The enumerator constant value.
  APSInt getValue() const { return Value; }
  /// Return the name of the enumerator.
  ///
  /// \returns The name of the enumerator.
  StringRef getName() const { return Name; }

  /// Member attributes.
  MemberAttributes Attrs;
  /// Enumerator constant value.
  APSInt Value;
  /// Name of the enumerator.
  StringRef Name;
};

/// LF_VFUNCTAB record describing a virtual function pointer member.
class VFPtrRecord : public TypeRecord {
public:
  /// Construct an empty virtual function pointer record.
  VFPtrRecord() = default;
  /// Construct a virtual function pointer record with the given kind.
  ///
  /// \param Kind Type record kind.
  explicit VFPtrRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  /// Construct a virtual function pointer record from its pointer type.
  ///
  /// \param Type Type of the virtual function pointer.
  VFPtrRecord(TypeIndex Type)
      : TypeRecord(TypeRecordKind::VFPtr), Type(Type) {}

  /// Return the type of the virtual function pointer.
  ///
  /// \returns The type of the virtual function pointer.
  TypeIndex getType() const { return Type; }

  /// Type of the virtual function pointer.
  TypeIndex Type;
};

/// LF_BCLASS or LF_BINTERFACE record describing a non-virtual base class.
class BaseClassRecord : public TypeRecord {
public:
  /// Construct an empty base class record.
  BaseClassRecord() = default;
  /// Construct a base class record with the given kind.
  ///
  /// \param Kind Type record kind.
  explicit BaseClassRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  /// Construct a base class record from attributes, type, and offset.
  ///
  /// \param Attrs Member attributes.
  /// \param Type Type of the base class.
  /// \param Offset Byte offset of the base subobject.
  BaseClassRecord(MemberAttributes Attrs, TypeIndex Type, uint64_t Offset)
      : TypeRecord(TypeRecordKind::BaseClass), Attrs(Attrs), Type(Type),
        Offset(Offset) {}
  /// Construct a base class record from access, type, and offset.
  ///
  /// \param Access Member access control.
  /// \param Type Type of the base class.
  /// \param Offset Byte offset of the base subobject.
  BaseClassRecord(MemberAccess Access, TypeIndex Type, uint64_t Offset)
      : TypeRecord(TypeRecordKind::BaseClass), Attrs(Access), Type(Type),
        Offset(Offset) {}

  /// Return the member access control.
  ///
  /// \returns The member access control.
  MemberAccess getAccess() const { return Attrs.getAccess(); }
  /// Return the type of the base class.
  ///
  /// \returns The type of the base class.
  TypeIndex getBaseType() const { return Type; }
  /// Return the byte offset of the base subobject.
  ///
  /// \returns The byte offset of the base subobject.
  uint64_t getBaseOffset() const { return Offset; }

  /// Member attributes.
  MemberAttributes Attrs;
  /// Type of the base class.
  TypeIndex Type;
  /// Byte offset of the base subobject.
  uint64_t Offset = 0;
};

/// LF_VBCLASS or LF_IVBCLASS record describing a virtual base class.
class VirtualBaseClassRecord : public TypeRecord {
public:
  /// Construct an empty virtual base class record.
  VirtualBaseClassRecord() = default;
  /// Construct a virtual base class record with the given kind.
  ///
  /// \param Kind Type record kind.
  explicit VirtualBaseClassRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  /// Construct a virtual base class record from attributes and layout fields.
  ///
  /// \param Kind Type record kind.
  /// \param Attrs Member attributes.
  /// \param BaseType Type of the virtual base class.
  /// \param VBPtrType Type of the virtual base pointer.
  /// \param Offset Offset of the virtual base pointer.
  /// \param Index Index into the virtual base table.
  VirtualBaseClassRecord(TypeRecordKind Kind, MemberAttributes Attrs,
                         TypeIndex BaseType, TypeIndex VBPtrType,
                         uint64_t Offset, uint64_t Index)
      : TypeRecord(Kind), Attrs(Attrs), BaseType(BaseType),
        VBPtrType(VBPtrType), VBPtrOffset(Offset), VTableIndex(Index) {}
  /// Construct a virtual base class record from access and layout fields.
  ///
  /// \param Kind Type record kind.
  /// \param Access Member access control.
  /// \param BaseType Type of the virtual base class.
  /// \param VBPtrType Type of the virtual base pointer.
  /// \param Offset Offset of the virtual base pointer.
  /// \param Index Index into the virtual base table.
  VirtualBaseClassRecord(TypeRecordKind Kind, MemberAccess Access,
                         TypeIndex BaseType, TypeIndex VBPtrType,
                         uint64_t Offset, uint64_t Index)
      : TypeRecord(Kind), Attrs(Access), BaseType(BaseType),
        VBPtrType(VBPtrType), VBPtrOffset(Offset), VTableIndex(Index) {}

  /// Return the member access control.
  ///
  /// \returns The member access control.
  MemberAccess getAccess() const { return Attrs.getAccess(); }
  /// Return the type of the virtual base class.
  ///
  /// \returns The type of the virtual base class.
  TypeIndex getBaseType() const { return BaseType; }
  /// Return the type of the virtual base pointer.
  ///
  /// \returns The type of the virtual base pointer.
  TypeIndex getVBPtrType() const { return VBPtrType; }
  /// Return the offset of the virtual base pointer.
  ///
  /// \returns The offset of the virtual base pointer.
  uint64_t getVBPtrOffset() const { return VBPtrOffset; }
  /// Return the index into the virtual base table.
  ///
  /// \returns The index into the virtual base table.
  uint64_t getVTableIndex() const { return VTableIndex; }

  /// Member attributes.
  MemberAttributes Attrs;
  /// Type of the virtual base class.
  TypeIndex BaseType;
  /// Type of the virtual base pointer.
  TypeIndex VBPtrType;
  /// Offset of the virtual base pointer.
  uint64_t VBPtrOffset = 0;
  /// Index into the virtual base table.
  uint64_t VTableIndex = 0;
};

/// LF_INDEX - Used to chain two large LF_FIELDLIST or LF_METHODLIST records
/// together. The first will end in an LF_INDEX record that points to the next.
class ListContinuationRecord : public TypeRecord {
public:
  /// Construct an empty list continuation record.
  ListContinuationRecord() = default;
  /// Construct a list continuation record with the given kind.
  ///
  /// \param Kind Type record kind.
  explicit ListContinuationRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}
  /// Construct a list continuation record pointing at the next list.
  ///
  /// \param ContinuationIndex Type index of the continuing field or method list.
  ListContinuationRecord(TypeIndex ContinuationIndex)
      : TypeRecord(TypeRecordKind::ListContinuation),
        ContinuationIndex(ContinuationIndex) {}

  /// Return the type index of the continuing field or method list.
  ///
  /// \returns The type index of the continuing field or method list.
  TypeIndex getContinuationIndex() const { return ContinuationIndex; }

  /// Type index of the continuing field or method list.
  TypeIndex ContinuationIndex;
};

/// LF_PRECOMP record referencing types from a precompiled header.
class PrecompRecord : public TypeRecord {
public:
  /// Construct an empty precompiled type reference record.
  PrecompRecord() = default;
  /// Construct a precompiled type reference record with the given kind.
  ///
  /// \param Kind Type record kind.
  explicit PrecompRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}

  /// Return the starting type index covered by the precompiled header.
  ///
  /// \returns The starting type index covered by the precompiled header.
  uint32_t getStartTypeIndex() const { return StartTypeIndex; }
  /// Return the number of types covered by the precompiled header.
  ///
  /// \returns The number of types covered by the precompiled header.
  uint32_t getTypesCount() const { return TypesCount; }
  /// Return the signature identifying the precompiled header.
  ///
  /// \returns The signature identifying the precompiled header.
  uint32_t getSignature() const { return Signature; }
  /// Return the path of the precompiled header file.
  ///
  /// \returns The path of the precompiled header file.
  StringRef getPrecompFilePath() const { return PrecompFilePath; }

  /// Starting type index covered by the precompiled header.
  uint32_t StartTypeIndex = 0;
  /// Number of types covered by the precompiled header.
  uint32_t TypesCount = 0;
  /// Signature identifying the precompiled header.
  uint32_t Signature = 0;
  /// Path of the precompiled header file.
  StringRef PrecompFilePath;
};

/// LF_ENDPRECOMP record marking the end of a precompiled type stream.
class EndPrecompRecord : public TypeRecord {
public:
  /// Construct an empty end-precomp record.
  EndPrecompRecord() = default;
  /// Construct an end-precomp record with the given kind.
  ///
  /// \param Kind Type record kind.
  explicit EndPrecompRecord(TypeRecordKind Kind) : TypeRecord(Kind) {}

  /// Return the signature identifying the precompiled header.
  ///
  /// \returns The signature identifying the precompiled header.
  uint32_t getSignature() const { return Signature; }

  /// Signature identifying the precompiled header.
  uint32_t Signature = 0;
};

} // end namespace codeview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_TYPERECORD_H
