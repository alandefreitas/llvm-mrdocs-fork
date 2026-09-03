//===-- LVType.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the LVType class, which is used to describe a debug
// information type.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVTYPE_H
#define LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVTYPE_H

#include "llvm/DebugInfo/LogicalView/Core/LVElement.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
namespace logicalview {

/// Kind flags describing the category of a logical type.
enum class LVTypeKind {
  /// Primitive or base type.
  IsBase,
  /// Const-qualified type.
  IsConst,
  /// Enumerator constant within an enumeration.
  IsEnumerator,
  /// Imported name, module, or declaration.
  IsImport,
  /// Imported declaration (`DW_TAG_imported_declaration`).
  IsImportDeclaration,
  /// Imported module (`DW_TAG_imported_module`).
  IsImportModule,
  /// Pointer type.
  IsPointer,
  /// Pointer-to-member type.
  IsPointerMember,
  /// Lvalue reference type.
  IsReference,
  /// Restrict-qualified type.
  IsRestrict,
  /// Rvalue reference type.
  IsRvalueReference,
  /// Array subrange type (`DW_TAG_subrange_type`).
  IsSubrange,
  /// Template parameter of any kind.
  IsTemplateParam,
  /// Template template parameter.
  IsTemplateTemplateParam,
  /// Template type parameter.
  IsTemplateTypeParam,
  /// Template value (non-type) parameter.
  IsTemplateValueParam,
  /// Typedef (`DW_TAG_typedef`).
  IsTypedef,
  /// Unaligned-qualified type.
  IsUnaligned,
  /// Unspecified type.
  IsUnspecified,
  /// Volatile-qualified type.
  IsVolatile,
  /// CodeView type modifier (`LF_MODIFIER`).
  IsModifier,
  /// Sentinel past the last kind enumerator.
  LastEntry
};
/// Set of selected logical type kinds.
using LVTypeKindSelection = std::set<LVTypeKind>;
/// Map from a type kind to the getter used to query that kind.
using LVTypeDispatch = std::map<LVTypeKind, LVTypeGetFunction>;
/// Ordered list of type-kind getter callbacks.
using LVTypeRequest = std::vector<LVTypeGetFunction>;

/// Logical representation of a DWARF or CodeView type.
class LLVM_ABI LVType : public LVElement {
  enum class Property { IsSubrangeCount, LastEntry };

  // Typed bitvector with kinds and properties for this type.
  LVProperties<LVTypeKind> Kinds;
  LVProperties<Property> Properties;
  static LVTypeDispatch Dispatch;

  // Size in bits of a symbol of this type.
  uint32_t BitSize = 0;

  // Find the current type in the given 'Targets'.
  LVType *findIn(const LVTypes *Targets) const;

public:
  /// Construct an empty logical type.
  LVType() : LVElement(LVSubclassID::LV_TYPE) { setIsType(); }
  /// Copy construction is not allowed.
  /// \param Other Unused source logical type.
  LVType(const LVType &Other) = delete;
  /// Copy assignment is not allowed.
  /// \param Other Unused source logical type.
  LVType &operator=(const LVType &Other) = delete;
  /// Destroy the logical type.
  ~LVType() override = default;

  /// Return true if \p Element is an `LVType`.
  /// \param Element Element to test for the type subclass.
  /// \returns True when \p Element has subclass ID `LV_TYPE`.
  static bool classof(const LVElement *Element) {
    return Element->getSubclassID() == LVSubclassID::LV_TYPE;
  }

  /// Return whether this type is a base type.
  /// \returns True when the base-type kind is set.
  bool getIsBase() const { return Kinds.get(LVTypeKind::IsBase); }
  /// Mark this type as a base type.
  void setIsBase() { Kinds.set(LVTypeKind::IsBase); }
  /// Clear the base-type kind on this type.
  void resetIsBase() { Kinds.reset(LVTypeKind::IsBase); }
  /// Return whether this type is const-qualified.
  /// \returns True when the const-qualified kind is set.
  bool getIsConst() const { return Kinds.get(LVTypeKind::IsConst); }
  /// Mark this type as const-qualified.
  void setIsConst() { Kinds.set(LVTypeKind::IsConst); }
  /// Clear the const-qualified kind on this type.
  void resetIsConst() { Kinds.reset(LVTypeKind::IsConst); }
  /// Return whether this type is an enumerator.
  /// \returns True when the enumerator kind is set.
  bool getIsEnumerator() const { return Kinds.get(LVTypeKind::IsEnumerator); }
  /// Mark this type as an enumerator.
  void setIsEnumerator() { Kinds.set(LVTypeKind::IsEnumerator); }
  /// Clear the enumerator kind on this type.
  void resetIsEnumerator() { Kinds.reset(LVTypeKind::IsEnumerator); }
  /// Return whether this type is an import.
  /// \returns True when the import kind is set.
  bool getIsImport() const { return Kinds.get(LVTypeKind::IsImport); }
  /// Mark this type as an import.
  void setIsImport() { Kinds.set(LVTypeKind::IsImport); }
  /// Clear the import kind on this type.
  void resetIsImport() { Kinds.reset(LVTypeKind::IsImport); }
  /// Return whether this type is an imported declaration.
  /// \returns True when the imported-declaration kind is set.
  bool getIsImportDeclaration() const {
    return Kinds.get(LVTypeKind::IsImportDeclaration);
  }
  /// Mark this type as an imported declaration and as an import.
  void setIsImportDeclaration() {
    Kinds.set(LVTypeKind::IsImportDeclaration);
    setIsImport();
  }
  /// Clear the imported-declaration kind on this type.
  void resetIsImportDeclaration() {
    Kinds.reset(LVTypeKind::IsImportDeclaration);
  }
  /// Return whether this type is an imported module.
  /// \returns True when the imported-module kind is set.
  bool getIsImportModule() const {
    return Kinds.get(LVTypeKind::IsImportModule);
  }
  /// Mark this type as an imported module and as an import.
  void setIsImportModule() {
    Kinds.set(LVTypeKind::IsImportModule);
    setIsImport();
  }
  /// Clear the imported-module kind on this type.
  void resetIsImportModule() { Kinds.reset(LVTypeKind::IsImportModule); }
  /// Return whether this type is a pointer.
  /// \returns True when the pointer kind is set.
  bool getIsPointer() const { return Kinds.get(LVTypeKind::IsPointer); }
  /// Mark this type as a pointer.
  void setIsPointer() { Kinds.set(LVTypeKind::IsPointer); }
  /// Clear the pointer kind on this type.
  void resetIsPointer() { Kinds.reset(LVTypeKind::IsPointer); }
  /// Return whether this type is a pointer-to-member.
  /// \returns True when the pointer-to-member kind is set.
  bool getIsPointerMember() const {
    return Kinds.get(LVTypeKind::IsPointerMember);
  }
  /// Mark this type as a pointer-to-member.
  void setIsPointerMember() { Kinds.set(LVTypeKind::IsPointerMember); }
  /// Clear the pointer-to-member kind on this type.
  void resetIsPointerMember() { Kinds.reset(LVTypeKind::IsPointerMember); }
  /// Return whether this type is an lvalue reference.
  /// \returns True when the lvalue-reference kind is set.
  bool getIsReference() const { return Kinds.get(LVTypeKind::IsReference); }
  /// Mark this type as an lvalue reference.
  void setIsReference() { Kinds.set(LVTypeKind::IsReference); }
  /// Clear the lvalue-reference kind on this type.
  void resetIsReference() { Kinds.reset(LVTypeKind::IsReference); }
  /// Return whether this type is restrict-qualified.
  /// \returns True when the restrict-qualified kind is set.
  bool getIsRestrict() const { return Kinds.get(LVTypeKind::IsRestrict); }
  /// Mark this type as restrict-qualified.
  void setIsRestrict() { Kinds.set(LVTypeKind::IsRestrict); }
  /// Clear the restrict-qualified kind on this type.
  void resetIsRestrict() { Kinds.reset(LVTypeKind::IsRestrict); }
  /// Return whether this type is an rvalue reference.
  /// \returns True when the rvalue-reference kind is set.
  bool getIsRvalueReference() const {
    return Kinds.get(LVTypeKind::IsRvalueReference);
  }
  /// Mark this type as an rvalue reference.
  void setIsRvalueReference() { Kinds.set(LVTypeKind::IsRvalueReference); }
  /// Clear the rvalue-reference kind on this type.
  void resetIsRvalueReference() { Kinds.reset(LVTypeKind::IsRvalueReference); }
  /// Return whether this type is a subrange.
  /// \returns True when the subrange kind is set.
  bool getIsSubrange() const { return Kinds.get(LVTypeKind::IsSubrange); }
  /// Mark this type as a subrange.
  void setIsSubrange() { Kinds.set(LVTypeKind::IsSubrange); }
  /// Clear the subrange kind on this type.
  void resetIsSubrange() { Kinds.reset(LVTypeKind::IsSubrange); }
  /// Return whether this type is a template parameter.
  /// \returns True when the template-parameter kind is set.
  bool getIsTemplateParam() const {
    return Kinds.get(LVTypeKind::IsTemplateParam);
  }
  /// Mark this type as a template parameter.
  void setIsTemplateParam() { Kinds.set(LVTypeKind::IsTemplateParam); }
  /// Clear the template-parameter kind on this type.
  void resetIsTemplateParam() { Kinds.reset(LVTypeKind::IsTemplateParam); }
  /// Return whether this type is a template template parameter.
  /// \returns True when the template-template-parameter kind is set.
  bool getIsTemplateTemplateParam() const {
    return Kinds.get(LVTypeKind::IsTemplateTemplateParam);
  }
  /// Mark this type as a template template and template parameter.
  void setIsTemplateTemplateParam() {
    Kinds.set(LVTypeKind::IsTemplateTemplateParam);
    setIsTemplateParam();
  }
  /// Clear the template-template-parameter kind on this type.
  void resetIsTemplateTemplateParam() {
    Kinds.reset(LVTypeKind::IsTemplateTemplateParam);
  }
  /// Return whether this type is a template type parameter.
  /// \returns True when the template-type-parameter kind is set.
  bool getIsTemplateTypeParam() const {
    return Kinds.get(LVTypeKind::IsTemplateTypeParam);
  }
  /// Mark this type as a template type parameter and as a template parameter.
  void setIsTemplateTypeParam() {
    Kinds.set(LVTypeKind::IsTemplateTypeParam);
    setIsTemplateParam();
  }
  /// Clear the template-type-parameter kind on this type.
  void resetIsTemplateTypeParam() {
    Kinds.reset(LVTypeKind::IsTemplateTypeParam);
  }
  /// Return whether this type is a template value parameter.
  /// \returns True when the template-value-parameter kind is set.
  bool getIsTemplateValueParam() const {
    return Kinds.get(LVTypeKind::IsTemplateValueParam);
  }
  /// Mark this type as a template value parameter and as a template parameter.
  void setIsTemplateValueParam() {
    Kinds.set(LVTypeKind::IsTemplateValueParam);
    setIsTemplateParam();
  }
  /// Clear the template-value-parameter kind on this type.
  void resetIsTemplateValueParam() {
    Kinds.reset(LVTypeKind::IsTemplateValueParam);
  }
  /// Return whether this type is a typedef.
  /// \returns True when the typedef kind is set.
  bool getIsTypedef() const { return Kinds.get(LVTypeKind::IsTypedef); }
  /// Mark this type as a typedef.
  void setIsTypedef() { Kinds.set(LVTypeKind::IsTypedef); }
  /// Clear the typedef kind on this type.
  void resetIsTypedef() { Kinds.reset(LVTypeKind::IsTypedef); }
  /// Return whether this type is unaligned-qualified.
  /// \returns True when the unaligned-qualified kind is set.
  bool getIsUnaligned() const { return Kinds.get(LVTypeKind::IsUnaligned); }
  /// Mark this type as unaligned-qualified.
  void setIsUnaligned() { Kinds.set(LVTypeKind::IsUnaligned); }
  /// Clear the unaligned-qualified kind on this type.
  void resetIsUnaligned() { Kinds.reset(LVTypeKind::IsUnaligned); }
  /// Return whether this type is unspecified.
  /// \returns True when the unspecified kind is set.
  bool getIsUnspecified() const { return Kinds.get(LVTypeKind::IsUnspecified); }
  /// Mark this type as unspecified.
  void setIsUnspecified() { Kinds.set(LVTypeKind::IsUnspecified); }
  /// Clear the unspecified kind on this type.
  void resetIsUnspecified() { Kinds.reset(LVTypeKind::IsUnspecified); }
  /// Return whether this type is volatile-qualified.
  /// \returns True when the volatile-qualified kind is set.
  bool getIsVolatile() const { return Kinds.get(LVTypeKind::IsVolatile); }
  /// Mark this type as volatile-qualified.
  void setIsVolatile() { Kinds.set(LVTypeKind::IsVolatile); }
  /// Clear the volatile-qualified kind on this type.
  void resetIsVolatile() { Kinds.reset(LVTypeKind::IsVolatile); }
  /// Return whether this type is a CodeView modifier.
  /// \returns True when the CodeView-modifier kind is set.
  bool getIsModifier() const { return Kinds.get(LVTypeKind::IsModifier); }
  /// Mark this type as a CodeView modifier.
  void setIsModifier() { Kinds.set(LVTypeKind::IsModifier); }
  /// Clear the CodeView-modifier kind on this type.
  void resetIsModifier() { Kinds.reset(LVTypeKind::IsModifier); }

  /// Return whether this subrange stores a count rather than bounds.
  /// \returns True when the subrange-count property is set.
  bool getIsSubrangeCount() const {
    return Properties.get(Property::IsSubrangeCount);
  }
  /// Mark this subrange as storing a count rather than bounds.
  void setIsSubrangeCount() { Properties.set(Property::IsSubrangeCount); }
  /// Clear the subrange-count property on this type.
  void resetIsSubrangeCount() { Properties.reset(Property::IsSubrangeCount); }

  /// Return a string naming the kind of this type.
  /// \returns Null-terminated kind name for this type.
  const char *kind() const override;

  /// Resolve the type name via abstract-origin and specification links.
  ///
  /// Follow a chain of references given by DW_AT_abstract_origin and/or
  /// DW_AT_specification and update the type name.
  /// \returns Resolved name after walking the reference chain.
  StringRef resolveReferencesChain();

  /// Return whether this type is a base type.
  /// \returns True when this type is a base type.
  bool isBase() const override { return getIsBase(); }
  /// Return whether this type is a template parameter.
  /// \returns True when this type is a template parameter.
  bool isTemplateParam() const override { return getIsTemplateParam(); }

  /// Append this type's encoding as a template argument to \p Name.
  /// \param Name String that receives the encoded template argument.
  virtual void encodeTemplateArgument(std::string &Name) const {}

  /// Return the underlying type for a type definition, or nullptr.
  /// \returns Underlying type element, or nullptr when none is set.
  virtual LVElement *getUnderlyingType() { return nullptr; }
  /// Set the underlying type for a type definition.
  /// \param Element Element that becomes the underlying type.
  virtual void setUnderlyingType(LVElement *Element) {}

  /// Return the size in bits of an entity of this type.
  /// \returns Bit size stored for this type.
  uint32_t getBitSize() const override { return BitSize; }
  /// Set the size in bits of an entity of this type.
  /// \param Size Bit size to store for this type.
  void setBitSize(uint32_t Size) override { BitSize = Size; }

  /// Resolve the printable name of this type.
  void resolveName() override;
  /// Resolve references held by this type to other logical elements.
  void resolveReferences() override;

  /// Return the shared dispatch table mapping kinds to getters.
  /// \returns Reference to the static kind-to-getter dispatch map.
  static LVTypeDispatch &getDispatch() { return Dispatch; }

  /// Return whether template parameters in \p References match \p Targets.
  /// \param References Reference parameter types to compare.
  /// \param Targets Target parameter types to compare against.
  /// \returns True when the parameter sequences are logically equal.
  static bool parametersMatch(const LVTypes *References,
                              const LVTypes *Targets);

  /// Collect type and scope template parameters from \p Types.
  /// \param Types Types that may contain template parameters.
  /// \param TypesParam Destination for type template parameters.
  /// \param ScopesParam Destination for scope template parameters.
  static void getParameters(const LVTypes *Types, LVTypes *TypesParam,
                            LVScopes *ScopesParam);

  /// Mark parents of types in \p References that are missing from \p Targets.
  ///
  /// Iterate through the 'References' set and check that all its elements
  /// are present in the 'Targets' set. For a missing element, mark its
  /// parents as missing.
  /// \param References Types expected to be present in the target set.
  /// \param Targets Types available for comparison.
  static void markMissingParents(const LVTypes *References,
                                 const LVTypes *Targets);

  /// Return whether this type is logically equal to \p Type.
  /// \param Type Type to compare against.
  /// \returns True when the types are logically equal.
  virtual bool equals(const LVType *Type) const;

  /// Return whether the types in \p References equal those in \p Targets.
  /// \param References Reference types to compare.
  /// \param Targets Target types to compare against.
  /// \returns True when both sequences are logically equal.
  static bool equals(const LVTypes *References, const LVTypes *Targets);

  /// Report this type as missing or added during comparison.
  /// \param Pass Comparison pass that classifies the type.
  void report(LVComparePass Pass) override;

  /// Print this type to \p OS.
  /// \param OS Stream that receives the printed type.
  /// \param Full Whether to print full detail.
  void print(raw_ostream &OS, bool Full = true) const override;
  /// Print type-specific details to \p OS.
  /// \param OS Stream that receives the extra output.
  /// \param Full Whether to print full detail.
  void printExtra(raw_ostream &OS, bool Full = true) const override;
};

/// Logical representation of a `DW_TAG_typedef` type.
class LLVM_ABI LVTypeDefinition final : public LVType {
public:
  /// Construct a typedef logical type.
  LVTypeDefinition() : LVType() {
    setIsTypedef();
    setIncludeInPrint();
  }
  /// Copy construction is not allowed.
  /// \param Other Unused source typedef logical type.
  LVTypeDefinition(const LVTypeDefinition &Other) = delete;
  /// Copy assignment is not allowed.
  /// \param Other Unused source typedef logical type.
  LVTypeDefinition &operator=(const LVTypeDefinition &Other) = delete;
  /// Destroy the typedef logical type.
  ~LVTypeDefinition() override = default;

  /// Return the underlying type for this typedef.
  /// \returns Underlying type element for this typedef.
  LVElement *getUnderlyingType() override;
  /// Set the underlying type for this typedef.
  /// \param Element Element that becomes the underlying type.
  void setUnderlyingType(LVElement *Element) override { setType(Element); }

  /// Resolve typedef-specific attributes after general resolution.
  void resolveExtra() override;

  /// Return whether this typedef is logically equal to \p Type.
  /// \param Type Type to compare against.
  /// \returns True when the types are logically equal.
  bool equals(const LVType *Type) const override;

  /// Print typedef-specific details to \p OS.
  /// \param OS Stream that receives the extra output.
  /// \param Full Whether to print full detail.
  void printExtra(raw_ostream &OS, bool Full = true) const override;
};

/// Logical representation of a `DW_TAG_enumerator` constant.
class LLVM_ABI LVTypeEnumerator final : public LVType {
  // Index in the String pool representing any initial value.
  size_t ValueIndex = 0;

public:
  /// Construct an enumerator logical type.
  LVTypeEnumerator() : LVType() {
    setIsEnumerator();
    setIncludeInPrint();
  }
  /// Copy construction is not allowed.
  /// \param Other Unused source enumerator logical type.
  LVTypeEnumerator(const LVTypeEnumerator &Other) = delete;
  /// Copy assignment is not allowed.
  /// \param Other Unused source enumerator logical type.
  LVTypeEnumerator &operator=(const LVTypeEnumerator &Other) = delete;
  /// Destroy the enumerator logical type.
  ~LVTypeEnumerator() override = default;

  /// Return the enumerator constant value as a string.
  /// \returns String form of the enumerator constant.
  StringRef getValue() const override {
    return getStringPool().getString(ValueIndex);
  }
  /// Set the enumerator constant value from \p Value.
  /// \param Value String form of the enumerator constant.
  void setValue(StringRef Value) override {
    ValueIndex = getStringPool().getIndex(Value);
  }
  /// Return the string-pool index of the enumerator value.
  /// \returns String-pool index of the enumerator constant value.
  size_t getValueIndex() const override { return ValueIndex; }

  /// Return whether this enumerator is logically equal to \p Type.
  /// \param Type Type to compare against.
  /// \returns True when the types are logically equal.
  bool equals(const LVType *Type) const override;

  /// Print enumerator-specific details to \p OS.
  /// \param OS Stream that receives the extra output.
  /// \param Full Whether to print full detail.
  void printExtra(raw_ostream &OS, bool Full = true) const override;
};

/// Logical representation of an imported module or declaration.
class LLVM_ABI LVTypeImport final : public LVType {
public:
  /// Construct an import logical type.
  LVTypeImport() : LVType() { setIncludeInPrint(); }
  /// Copy construction is not allowed.
  /// \param Other Unused source import logical type.
  LVTypeImport(const LVTypeImport &Other) = delete;
  /// Copy assignment is not allowed.
  /// \param Other Unused source import logical type.
  LVTypeImport &operator=(const LVTypeImport &Other) = delete;
  /// Destroy the import logical type.
  ~LVTypeImport() override = default;

  /// Return whether this import is logically equal to \p Type.
  /// \param Type Type to compare against.
  /// \returns True when the types are logically equal.
  bool equals(const LVType *Type) const override;

  /// Print import-specific details to \p OS.
  /// \param OS Stream that receives the extra output.
  /// \param Full Whether to print full detail.
  void printExtra(raw_ostream &OS, bool Full = true) const override;
};

/// Logical representation of a DWARF template parameter (type or value).
class LLVM_ABI LVTypeParam final : public LVType {
  // Index in the String pool representing any initial value.
  size_t ValueIndex = 0;

public:
  /// Construct a template-parameter logical type.
  LVTypeParam();
  /// Copy construction is not allowed.
  /// \param Other Unused source template-parameter logical type.
  LVTypeParam(const LVTypeParam &Other) = delete;
  /// Copy assignment is not allowed.
  /// \param Other Unused source template-parameter logical type.
  LVTypeParam &operator=(const LVTypeParam &Other) = delete;
  /// Destroy the template-parameter logical type.
  ~LVTypeParam() override = default;

  /// Return the template parameter value as a string.
  /// \returns String form of the template parameter value.
  StringRef getValue() const override {
    return getStringPool().getString(ValueIndex);
  }
  /// Set the template parameter value from \p Value.
  /// \param Value String form of the template parameter value.
  void setValue(StringRef Value) override {
    ValueIndex = getStringPool().getIndex(Value);
  }
  /// Return the string-pool index of the template parameter value.
  /// \returns String-pool index of the template parameter value.
  size_t getValueIndex() const override { return ValueIndex; }

  /// Append this parameter's encoding as a template argument to \p Name.
  /// \param Name String that receives the encoded template argument.
  void encodeTemplateArgument(std::string &Name) const override;

  /// Return whether this parameter is logically equal to \p Type.
  /// \param Type Type to compare against.
  /// \returns True when the types are logically equal.
  bool equals(const LVType *Type) const override;

  /// Print template-parameter-specific details to \p OS.
  /// \param OS Stream that receives the extra output.
  /// \param Full Whether to print full detail.
  void printExtra(raw_ostream &OS, bool Full = true) const override;
};

/// Logical representation of a `DW_TAG_subrange_type`.
class LLVM_ABI LVTypeSubrange final : public LVType {
  // Values describing the subrange bounds.
  int64_t LowerBound = 0; // DW_AT_lower_bound or DW_AT_count value.
  int64_t UpperBound = 0; // DW_AT_upper_bound value.

public:
  /// Construct a subrange logical type.
  LVTypeSubrange() : LVType() {
    setIsSubrange();
    setIncludeInPrint();
  }
  /// Copy construction is not allowed.
  /// \param Other Unused source subrange logical type.
  LVTypeSubrange(const LVTypeSubrange &Other) = delete;
  /// Copy assignment is not allowed.
  /// \param Other Unused source subrange logical type.
  LVTypeSubrange &operator=(const LVTypeSubrange &Other) = delete;
  /// Destroy the subrange logical type.
  ~LVTypeSubrange() override = default;

  /// Return the element count when this subrange stores a count, else 0.
  /// \returns Element count when count-based; otherwise 0.
  int64_t getCount() const override {
    return getIsSubrangeCount() ? LowerBound : 0;
  }
  /// Set the element count and mark this subrange as count-based.
  /// \param Value Element count stored as the lower-bound field.
  void setCount(int64_t Value) override {
    LowerBound = Value;
    setIsSubrangeCount();
  }

  /// Return the lower bound of this subrange.
  /// \returns Lower bound value for this subrange.
  int64_t getLowerBound() const override { return LowerBound; }
  /// Set the lower bound of this subrange.
  /// \param Value Lower bound to store.
  void setLowerBound(int64_t Value) override { LowerBound = Value; }

  /// Return the upper bound of this subrange.
  /// \returns Upper bound value for this subrange.
  int64_t getUpperBound() const override { return UpperBound; }
  /// Set the upper bound of this subrange.
  /// \param Value Upper bound to store.
  void setUpperBound(int64_t Value) override { UpperBound = Value; }

  /// Return the lower and upper bounds as a pair.
  /// \returns Pair of lower and upper bounds.
  std::pair<unsigned, unsigned> getBounds() const override {
    return {LowerBound, UpperBound};
  }
  /// Set both the lower and upper bounds of this subrange.
  /// \param Lower Lower bound to store.
  /// \param Upper Upper bound to store.
  void setBounds(unsigned Lower, unsigned Upper) override {
    LowerBound = Lower;
    UpperBound = Upper;
  }

  /// Resolve subrange-specific attributes after general resolution.
  void resolveExtra() override;

  /// Return whether this subrange is logically equal to \p Type.
  /// \param Type Type to compare against.
  /// \returns True when the types are logically equal.
  bool equals(const LVType *Type) const override;

  /// Print subrange-specific details to \p OS.
  /// \param OS Stream that receives the extra output.
  /// \param Full Whether to print full detail.
  void printExtra(raw_ostream &OS, bool Full = true) const override;
};

} // end namespace logicalview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVTYPE_H
