//===-- LVElement.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the LVElement class, which is used to describe a debug
// information element.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVELEMENT_H
#define LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVELEMENT_H

#include "llvm/DebugInfo/LogicalView/Core/LVObject.h"
#include "llvm/DebugInfo/LogicalView/Core/LVSourceLanguage.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MathExtras.h"
#include <map>
#include <set>
#include <vector>

namespace llvm {
namespace logicalview {

/// RTTI identifiers for LVElement subclasses.
enum class LVSubclassID : unsigned char {
  /// Base LVElement class.
  LV_ELEMENT,
  /// First marker for line subclass IDs.
  LV_LINE_FIRST,
  /// Generic logical line.
  LV_LINE,
  /// Line from debug line information.
  LV_LINE_DEBUG,
  /// Line from assembler output.
  LV_LINE_ASSEMBLER,
  /// Last marker for line subclass IDs.
  LV_LINE_LAST,
  /// First marker for scope subclass IDs.
  lV_SCOPE_FIRST,
  /// Generic logical scope.
  LV_SCOPE,
  /// Aggregate scope (class, structure, or union).
  LV_SCOPE_AGGREGATE,
  /// Alias (typedef) scope.
  LV_SCOPE_ALIAS,
  /// Array type scope.
  LV_SCOPE_ARRAY,
  /// Compilation unit scope.
  LV_SCOPE_COMPILE_UNIT,
  /// Enumeration scope.
  LV_SCOPE_ENUMERATION,
  /// Formal template parameter pack scope.
  LV_SCOPE_FORMAL_PACK,
  /// Function scope.
  LV_SCOPE_FUNCTION,
  /// Inlined function scope.
  LV_SCOPE_FUNCTION_INLINED,
  /// Function type scope.
  LV_SCOPE_FUNCTION_TYPE,
  /// Module scope.
  LV_SCOPE_MODULE,
  /// Namespace scope.
  LV_SCOPE_NAMESPACE,
  /// Root scope of the logical view.
  LV_SCOPE_ROOT,
  /// Template parameter pack scope.
  LV_SCOPE_TEMPLATE_PACK,
  /// Last marker for scope subclass IDs.
  LV_SCOPE_LAST,
  /// First marker for symbol subclass IDs.
  LV_SYMBOL_FIRST,
  /// Generic logical symbol.
  LV_SYMBOL,
  /// Last marker for symbol subclass IDs.
  LV_SYMBOL_LAST,
  /// First marker for type subclass IDs.
  LV_TYPE_FIRST,
  /// Generic logical type.
  LV_TYPE,
  /// Type definition.
  LV_TYPE_DEFINITION,
  /// Enumerator constant type.
  LV_TYPE_ENUMERATOR,
  /// Imported declaration type.
  LV_TYPE_IMPORT,
  /// Template parameter type.
  LV_TYPE_PARAM,
  /// Array subrange type.
  LV_TYPE_SUBRANGE,
  /// Last marker for type subclass IDs.
  LV_TYPE_LAST
};

/// Classification kind for selecting logical elements.
enum class LVElementKind {
  /// Element discarded by the linker.
  Discarded,
  /// Element with global visibility.
  Global,
  /// Element optimized by the compiler.
  Optimized,
  /// Sentinel marking the end of valid kinds.
  LastEntry
};
/// Set of LVElementKind values used for filtering.
using LVElementKindSet = std::set<LVElementKind>;
/// Map from element kind to a property query function.
using LVElementDispatch = std::map<LVElementKind, LVElementGetFunction>;
/// Ordered list of element property query functions.
using LVElementRequest = std::vector<LVElementGetFunction>;

/// Number of bits in a DWARF character (byte).
///
/// Assume 8-bit bytes; this is consistent, e.g. with
/// lldb/source/Plugins/SymbolFile/DWARF/DWARFASTParserClang.cpp.
constexpr unsigned int DWARF_CHAR_BIT = 8u;

/// Base class for a named logical-view element from debug information.
class LLVM_ABI LVElement : public LVObject {
  enum class Property {
    IsLine,   // A logical line.
    IsScope,  // A logical scope.
    IsSymbol, // A logical symbol.
    IsType,   // A logical type.
    IsEnumClass,
    IsExternal,
    HasType,
    HasAugmentedName,
    IsTypedefReduced,
    IsArrayResolved,
    IsMemberPointerResolved,
    IsTemplateResolved,
    IsInlined,
    IsInlinedAbstract,
    InvalidFilename,
    HasReference,
    HasReferenceAbstract,
    HasReferenceExtension,
    HasReferenceSpecification,
    QualifiedResolved,
    IncludeInPrint,
    IsStatic,
    TransformName,
    IsScoped,        // CodeView local type.
    IsNested,        // CodeView nested type.
    IsScopedAlready, // CodeView nested type inserted in correct scope.
    IsArtificial,
    IsReferencedType,
    IsSystem,
    OffsetFromTypeIndex,
    IsAnonymous,
    LastEntry
  };
  static LVElementDispatch Dispatch;

  // Indexes in the String Pool.
  size_t NameIndex = 0;
  size_t QualifiedNameIndex = 0;
  size_t FilenameIndex = 0;

  // Typed bitvector with properties for this element.
  LVProperties<Property> Properties;
  /// RTTI.
  const LVSubclassID SubclassID;

  uint16_t AccessibilityCode : 2; // DW_AT_accessibility.
  uint16_t InlineCode : 2;        // DW_AT_inline.
  uint16_t VirtualityCode : 2;    // DW_AT_virtuality.

  // The given Specification points to an element that is connected via the
  // DW_AT_specification, DW_AT_abstract_origin or DW_AT_extension attribute.
  void setFileLine(LVElement *Specification);

  // Get the qualified name that include its parents name.
  void resolveQualifiedName();

protected:
  /// Type associated with this element, or nullptr if none.
  LVElement *ElementType = nullptr;

  /// Print the filename index associated with this element.
  /// \param OS Stream that receives the printed file index.
  /// \param Full Whether to print full path information.
  void printFileIndex(raw_ostream &OS, bool Full = true) const override;

public:
  /// Construct an element with the given RTTI subclass \p ID.
  /// \param ID Subclass identifier for this element.
  LVElement(LVSubclassID ID)
      : LVObject(), SubclassID(ID), AccessibilityCode(0), InlineCode(0),
        VirtualityCode(0) {}
  /// Copy construction is not allowed.
  /// \param Other Unused source element instance.
  LVElement(const LVElement &Other) = delete;
  /// Copy assignment is not allowed.
  /// \param Other Unused source element instance.
  LVElement &operator=(const LVElement &Other) = delete;
  /// Destroy the logical-view element.
  ~LVElement() override = default;

  /// Return the RTTI subclass identifier for this element.
  /// \returns RTTI subclass identifier for this element.
  LVSubclassID getSubclassID() const { return SubclassID; }

  /// Return whether this element is a logical line.
  /// \returns True when the logical-line property is set.
  bool getIsLine() const { return Properties.get(Property::IsLine); }
  /// Mark this element as a logical line.
  void setIsLine() { Properties.set(Property::IsLine); }
  /// Clear the logical-line property on this element.
  void resetIsLine() { Properties.reset(Property::IsLine); }
  /// Return whether this element is a logical scope.
  /// \returns True when the logical-scope property is set.
  bool getIsScope() const { return Properties.get(Property::IsScope); }
  /// Mark this element as a logical scope.
  void setIsScope() { Properties.set(Property::IsScope); }
  /// Clear the logical-scope property on this element.
  void resetIsScope() { Properties.reset(Property::IsScope); }
  /// Return whether this element is a logical symbol.
  /// \returns True when the logical-symbol property is set.
  bool getIsSymbol() const { return Properties.get(Property::IsSymbol); }
  /// Mark this element as a logical symbol.
  void setIsSymbol() { Properties.set(Property::IsSymbol); }
  /// Clear the logical-symbol property on this element.
  void resetIsSymbol() { Properties.reset(Property::IsSymbol); }
  /// Return whether this element is a logical type.
  /// \returns True when the logical-type property is set.
  bool getIsType() const { return Properties.get(Property::IsType); }
  /// Mark this element as a logical type.
  void setIsType() { Properties.set(Property::IsType); }
  /// Clear the logical-type property on this element.
  void resetIsType() { Properties.reset(Property::IsType); }
  /// Return whether this element is a C++ enum class.
  /// \returns True when the enum-class property is set.
  bool getIsEnumClass() const { return Properties.get(Property::IsEnumClass); }
  /// Mark this element as a C++ enum class.
  void setIsEnumClass() { Properties.set(Property::IsEnumClass); }
  /// Clear the enum-class property on this element.
  void resetIsEnumClass() { Properties.reset(Property::IsEnumClass); }
  /// Return whether this element has external linkage.
  /// \returns True when the external-linkage property is set.
  bool getIsExternal() const { return Properties.get(Property::IsExternal); }
  /// Mark this element as having external linkage.
  void setIsExternal() { Properties.set(Property::IsExternal); }
  /// Clear the external-linkage property on this element.
  void resetIsExternal() { Properties.reset(Property::IsExternal); }
  /// Return whether this element has an associated type.
  /// \returns True when the has-type property is set.
  bool getHasType() const { return Properties.get(Property::HasType); }
  /// Mark this element as having an associated type.
  void setHasType() { Properties.set(Property::HasType); }
  /// Clear the has-type property on this element.
  void resetHasType() { Properties.reset(Property::HasType); }
  /// Return whether this element has an augmented name.
  /// \returns True when the augmented-name property is set.
  bool getHasAugmentedName() const {
    return Properties.get(Property::HasAugmentedName);
  }
  /// Mark this element as having an augmented name.
  void setHasAugmentedName() { Properties.set(Property::HasAugmentedName); }
  /// Clear the augmented-name property on this element.
  void resetHasAugmentedName() { Properties.reset(Property::HasAugmentedName); }
  /// Return whether a typedef chain for this element has been reduced.
  /// \returns True when the typedef-reduced property is set.
  bool getIsTypedefReduced() const {
    return Properties.get(Property::IsTypedefReduced);
  }
  /// Mark the typedef chain for this element as reduced.
  void setIsTypedefReduced() { Properties.set(Property::IsTypedefReduced); }
  /// Clear the typedef-reduced property on this element.
  void resetIsTypedefReduced() { Properties.reset(Property::IsTypedefReduced); }
  /// Return whether array dimensions for this element have been resolved.
  /// \returns True when the array-resolved property is set.
  bool getIsArrayResolved() const {
    return Properties.get(Property::IsArrayResolved);
  }
  /// Mark array dimensions for this element as resolved.
  void setIsArrayResolved() { Properties.set(Property::IsArrayResolved); }
  /// Clear the array-resolved property on this element.
  void resetIsArrayResolved() { Properties.reset(Property::IsArrayResolved); }
  /// Return whether a member-pointer type for this element has been resolved.
  /// \returns True when the member-pointer-resolved property is set.
  bool getIsMemberPointerResolved() const {
    return Properties.get(Property::IsMemberPointerResolved);
  }
  /// Mark the member-pointer type for this element as resolved.
  void setIsMemberPointerResolved() {
    Properties.set(Property::IsMemberPointerResolved);
  }
  /// Clear the member-pointer-resolved property on this element.
  void resetIsMemberPointerResolved() {
    Properties.reset(Property::IsMemberPointerResolved);
  }
  /// Return whether template information for this element has been resolved.
  /// \returns True when the template-resolved property is set.
  bool getIsTemplateResolved() const {
    return Properties.get(Property::IsTemplateResolved);
  }
  /// Mark template information for this element as resolved.
  void setIsTemplateResolved() { Properties.set(Property::IsTemplateResolved); }
  /// Clear the template-resolved property on this element.
  void resetIsTemplateResolved() {
    Properties.reset(Property::IsTemplateResolved);
  }
  /// Return whether this element represents an inlined instance.
  /// \returns True when the inlined property is set.
  bool getIsInlined() const { return Properties.get(Property::IsInlined); }
  /// Mark this element as an inlined instance.
  void setIsInlined() { Properties.set(Property::IsInlined); }
  /// Clear the inlined property on this element.
  void resetIsInlined() { Properties.reset(Property::IsInlined); }
  /// Return whether this element is an abstract inlined origin.
  /// \returns True when the inlined-abstract property is set.
  bool getIsInlinedAbstract() const {
    return Properties.get(Property::IsInlinedAbstract);
  }
  /// Mark this element as an abstract inlined origin.
  void setIsInlinedAbstract() { Properties.set(Property::IsInlinedAbstract); }
  /// Clear the inlined-abstract property on this element.
  void resetIsInlinedAbstract() {
    Properties.reset(Property::IsInlinedAbstract);
  }
  /// Return whether this element has an invalid filename.
  /// \returns True when the invalid-filename property is set.
  bool getInvalidFilename() const {
    return Properties.get(Property::InvalidFilename);
  }
  /// Mark this element as having an invalid filename.
  void setInvalidFilename() { Properties.set(Property::InvalidFilename); }
  /// Clear the invalid-filename property on this element.
  void resetInvalidFilename() { Properties.reset(Property::InvalidFilename); }
  /// Return whether this element has a reference to another element.
  /// \returns True when the has-reference property is set.
  bool getHasReference() const {
    return Properties.get(Property::HasReference);
  }
  /// Mark this element as having a reference to another element.
  void setHasReference() { Properties.set(Property::HasReference); }
  /// Clear the has-reference property on this element.
  void resetHasReference() { Properties.reset(Property::HasReference); }
  /// Return whether this element has an abstract-origin reference.
  /// \returns True when the abstract-origin-reference property is set.
  bool getHasReferenceAbstract() const {
    return Properties.get(Property::HasReferenceAbstract);
  }
  /// Mark this element as having an abstract-origin reference.
  void setHasReferenceAbstract() {
    Properties.set(Property::HasReferenceAbstract);
  }
  /// Clear the abstract-origin-reference property on this element.
  void resetHasReferenceAbstract() {
    Properties.reset(Property::HasReferenceAbstract);
  }
  /// Return whether this element has an extension reference.
  /// \returns True when the extension-reference property is set.
  bool getHasReferenceExtension() const {
    return Properties.get(Property::HasReferenceExtension);
  }
  /// Mark this element as having an extension reference.
  void setHasReferenceExtension() {
    Properties.set(Property::HasReferenceExtension);
  }
  /// Clear the extension-reference property on this element.
  void resetHasReferenceExtension() {
    Properties.reset(Property::HasReferenceExtension);
  }
  /// Return whether this element has a specification reference.
  /// \returns True when the specification-reference property is set.
  bool getHasReferenceSpecification() const {
    return Properties.get(Property::HasReferenceSpecification);
  }
  /// Mark this element as having a specification reference.
  void setHasReferenceSpecification() {
    Properties.set(Property::HasReferenceSpecification);
  }
  /// Clear the specification-reference property on this element.
  void resetHasReferenceSpecification() {
    Properties.reset(Property::HasReferenceSpecification);
  }
  /// Return whether the qualified name for this element has been resolved.
  /// \returns True when the qualified-resolved property is set.
  bool getQualifiedResolved() const {
    return Properties.get(Property::QualifiedResolved);
  }
  /// Mark the qualified name for this element as resolved.
  void setQualifiedResolved() { Properties.set(Property::QualifiedResolved); }
  /// Clear the qualified-resolved property on this element.
  void resetQualifiedResolved() {
    Properties.reset(Property::QualifiedResolved);
  }
  /// Return whether this element should be included when printing.
  /// \returns True when the include-in-print property is set.
  bool getIncludeInPrint() const {
    return Properties.get(Property::IncludeInPrint);
  }
  /// Mark this element to be included when printing.
  void setIncludeInPrint() { Properties.set(Property::IncludeInPrint); }
  /// Clear the include-in-print property on this element.
  void resetIncludeInPrint() { Properties.reset(Property::IncludeInPrint); }
  /// Return whether this element has static storage duration or linkage.
  /// \returns True when the static property is set.
  bool getIsStatic() const { return Properties.get(Property::IsStatic); }
  /// Mark this element as having static storage duration or linkage.
  void setIsStatic() { Properties.set(Property::IsStatic); }
  /// Clear the static property on this element.
  void resetIsStatic() { Properties.reset(Property::IsStatic); }
  /// Return whether this element's name should be transformed for display.
  /// \returns True when the transform-name property is set.
  bool getTransformName() const {
    return Properties.get(Property::TransformName);
  }
  /// Mark this element's name for display transformation.
  void setTransformName() { Properties.set(Property::TransformName); }
  /// Clear the transform-name property on this element.
  void resetTransformName() { Properties.reset(Property::TransformName); }
  /// Return whether this element is a CodeView local (scoped) type.
  /// \returns True when the scoped property is set.
  bool getIsScoped() const { return Properties.get(Property::IsScoped); }
  /// Mark this element as a CodeView local (scoped) type.
  void setIsScoped() { Properties.set(Property::IsScoped); }
  /// Clear the scoped property on this element.
  void resetIsScoped() { Properties.reset(Property::IsScoped); }
  /// Return whether this element is a CodeView nested type.
  /// \returns True when the nested property is set.
  bool getIsNested() const { return Properties.get(Property::IsNested); }
  /// Mark this element as a CodeView nested type.
  void setIsNested() { Properties.set(Property::IsNested); }
  /// Clear the nested property on this element.
  void resetIsNested() { Properties.reset(Property::IsNested); }
  /// Return whether a CodeView nested type was inserted in its correct scope.
  /// \returns True when the scoped-already property is set.
  bool getIsScopedAlready() const {
    return Properties.get(Property::IsScopedAlready);
  }
  /// Mark this CodeView nested type as already inserted in its correct scope.
  void setIsScopedAlready() { Properties.set(Property::IsScopedAlready); }
  /// Clear the scoped-already property on this element.
  void resetIsScopedAlready() { Properties.reset(Property::IsScopedAlready); }
  /// Return whether this element was artificially generated by the compiler.
  /// \returns True when the artificial property is set.
  bool getIsArtificial() const {
    return Properties.get(Property::IsArtificial);
  }
  /// Mark this element as artificially generated by the compiler.
  void setIsArtificial() { Properties.set(Property::IsArtificial); }
  /// Clear the artificial property on this element.
  void resetIsArtificial() { Properties.reset(Property::IsArtificial); }
  /// Return whether this element is used as a referenced type.
  /// \returns True when the referenced-type property is set.
  bool getIsReferencedType() const {
    return Properties.get(Property::IsReferencedType);
  }
  /// Mark this element as a referenced type.
  void setIsReferencedType() { Properties.set(Property::IsReferencedType); }
  /// Clear the referenced-type property on this element.
  void resetIsReferencedType() {
    Properties.reset(Property::IsReferencedType);
  }
  /// Return whether this element comes from a system header or library.
  /// \returns True when the system property is set.
  bool getIsSystem() const { return Properties.get(Property::IsSystem); }
  /// Mark this element as coming from a system header or library.
  void setIsSystem() { Properties.set(Property::IsSystem); }
  /// Clear the system property on this element.
  void resetIsSystem() { Properties.reset(Property::IsSystem); }
  /// Return whether this element's offset is derived from a type index.
  /// \returns True when the offset-from-type-index property is set.
  bool getOffsetFromTypeIndex() const {
    return Properties.get(Property::OffsetFromTypeIndex);
  }
  /// Mark this element's offset as derived from a type index.
  void setOffsetFromTypeIndex() {
    Properties.set(Property::OffsetFromTypeIndex);
  }
  /// Clear the offset-from-type-index property on this element.
  void resetOffsetFromTypeIndex() {
    Properties.reset(Property::OffsetFromTypeIndex);
  }
  /// Return whether this element is anonymous.
  /// \returns True when the anonymous property is set.
  bool getIsAnonymous() const { return Properties.get(Property::IsAnonymous); }
  /// Mark this element as anonymous.
  void setIsAnonymous() { Properties.set(Property::IsAnonymous); }
  /// Clear the anonymous property on this element.
  void resetIsAnonymous() { Properties.reset(Property::IsAnonymous); }

  /// Return whether this element has a non-empty name.
  /// \returns True when a name index is set.
  bool isNamed() const override { return NameIndex != 0; }
  /// Return whether this element has an associated type.
  /// \returns True when an associated type is set.
  bool isTyped() const override { return ElementType != nullptr; }
  /// Return whether this element has an associated source file.
  /// \returns True when a filename index is set.
  bool isFiled() const override { return FilenameIndex != 0; }

  /// Return whether the associated type is itself a type element.
  /// \returns True when the associated type is a type element.
  bool getIsKindType() const { return ElementType && ElementType->getIsType(); }
  /// Return whether the associated type is itself a scope element.
  /// \returns True when the associated type is a scope element.
  bool getIsKindScope() const {
    return ElementType && ElementType->getIsScope();
  }

  /// Return the name of this element from the string pool.
  /// \returns Name from the string pool.
  StringRef getName() const override {
    return getStringPool().getString(NameIndex);
  }
  /// Set the name of this element.
  /// \param ElementName Name to store in the string pool.
  void setName(StringRef ElementName) override;

  /// Return the pathname associated with this element.
  /// \returns Pathname from the string pool.
  StringRef getPathname() const {
    return getStringPool().getString(getFilenameIndex());
  }

  /// Set the filename associated with this element.
  /// \param Filename Source filename to associate with this element.
  void setFilename(StringRef Filename);

  /// Set the qualified name of this element.
  /// \param Name Fully qualified name to store in the string pool.
  void setQualifiedName(StringRef Name) {
    QualifiedNameIndex = getStringPool().getIndex(Name);
  }
  /// Return the qualified name of this element.
  /// \returns Qualified name from the string pool.
  StringRef getQualifiedName() const {
    return getStringPool().getString(QualifiedNameIndex);
  }

  /// Return the string-pool index of this element's name.
  /// \returns String-pool index of the name.
  size_t getNameIndex() const { return NameIndex; }
  /// Return the string-pool index of this element's qualified name.
  /// \returns String-pool index of the qualified name.
  size_t getQualifiedNameIndex() const { return QualifiedNameIndex; }

  /// Set the inner name component from the current element name.
  void setInnerComponent() { setInnerComponent(getName()); }
  /// Set the inner name component from \p Name.
  /// \param Name Name whose innermost component is stored.
  void setInnerComponent(StringRef Name);

  /// Return the name of the type associated with this element.
  /// \returns Name of the associated type.
  StringRef getTypeName() const;

  /// Return the producer string for this element, if any.
  /// \returns Producer string, or empty if none.
  virtual StringRef getProducer() const { return StringRef(); }
  /// Set the producer string for this element.
  /// \param ProducerName Producer identification string to store.
  virtual void setProducer(StringRef ProducerName) {}

  /// Return the source language associated with this element.
  /// \returns Source language value, or a default-constructed value if none.
  virtual LVSourceLanguage getSourceLanguage() const { return {}; }
  /// Set the source language associated with this element.
  /// \param SL Source language value to store.
  virtual void setSourceLanguage(LVSourceLanguage SL) {}

  /// Return whether this element is a compilation unit.
  /// \returns True when this element is a compilation unit.
  virtual bool isCompileUnit() const { return false; }
  /// Return whether this element is the logical-view root.
  /// \returns True when this element is the logical-view root.
  virtual bool isRoot() const { return false; }

  /// Set a generic reference to another element.
  /// \param Element Element referenced by this one.
  virtual void setReference(LVElement *Element) {}
  /// Set a reference to a scope.
  /// \param Scope Scope referenced by this element.
  virtual void setReference(LVScope *Scope) {}
  /// Set a reference to a symbol.
  /// \param Symbol Symbol referenced by this element.
  virtual void setReference(LVSymbol *Symbol) {}
  /// Set a reference to a type.
  /// \param Type Type referenced by this element.
  virtual void setReference(LVType *Type) {}

  /// Set the linkage (mangled) name of this element.
  /// \param LinkageName Linkage name to store.
  virtual void setLinkageName(StringRef LinkageName) {}
  /// Return the linkage (mangled) name of this element.
  /// \returns Linkage name, or empty if none.
  virtual StringRef getLinkageName() const { return StringRef(); }
  /// Return the string-pool index of this element's linkage name.
  /// \returns String-pool index of the linkage name, or zero if none.
  virtual size_t getLinkageNameIndex() const { return 0; }

  /// Return the call-site line number for an inlined instance.
  /// \returns Call-site line number, or zero if unset.
  virtual uint32_t getCallLineNumber() const { return 0; }
  /// Set the call-site line number for an inlined instance.
  /// \param Number Call-site source line number.
  virtual void setCallLineNumber(uint32_t Number) {}
  /// Return the string-pool index of the call-site filename.
  /// \returns String-pool index of the call-site filename, or zero if none.
  virtual size_t getCallFilenameIndex() const { return 0; }
  /// Set the string-pool index of the call-site filename.
  /// \param Index String-pool index of the call-site filename.
  virtual void setCallFilenameIndex(size_t Index) {}
  /// Return the string-pool index of this element's filename.
  /// \returns String-pool index of the filename.
  size_t getFilenameIndex() const { return FilenameIndex; }
  /// Set the string-pool index of this element's filename.
  /// \param Index String-pool index of the filename.
  void setFilenameIndex(size_t Index) { FilenameIndex = Index; }

  /// Set the file location for this element.
  /// \param Reference Optional specification or origin providing file info.
  void setFile(LVElement *Reference = nullptr);

  /// Return whether this element is a base class.
  /// \returns True when this element is a base class.
  virtual bool isBase() const { return false; }
  /// Return whether this element is a template parameter.
  /// \returns True when this element is a template parameter.
  virtual bool isTemplateParam() const { return false; }

  /// Return the storage size of this element in bytes.
  /// \returns Storage size in bytes derived from the bit size.
  uint32_t getStorageSizeInBytes() const {
    return llvm::divideCeil(getBitSize(), DWARF_CHAR_BIT);
  }
  /// Return the bit size of this element.
  /// \returns Bit size, or zero if unset.
  virtual uint32_t getBitSize() const { return 0; }
  /// Set the bit size of this element.
  /// \param Size Bit size to store.
  virtual void setBitSize(uint32_t Size) {}

  /// Return the element count for a subrange or similar bound.
  /// \returns Element count, or zero if unset.
  virtual int64_t getCount() const { return 0; }
  /// Set the element count for a subrange or similar bound.
  /// \param Value Count value to store.
  virtual void setCount(int64_t Value) {}
  /// Return the lower bound of a subrange.
  /// \returns Lower bound value, or zero if unset.
  virtual int64_t getLowerBound() const { return 0; }
  /// Set the lower bound of a subrange.
  /// \param Value Lower bound value to store.
  virtual void setLowerBound(int64_t Value) {}
  /// Return the upper bound of a subrange.
  /// \returns Upper bound value, or zero if unset.
  virtual int64_t getUpperBound() const { return 0; }
  /// Set the upper bound of a subrange.
  /// \param Value Upper bound value to store.
  virtual void setUpperBound(int64_t Value) {}
  /// Return the lower and upper bounds as a pair.
  /// \returns Pair of lower and upper bound values.
  virtual std::pair<unsigned, unsigned> getBounds() const { return {}; }
  /// Set both the lower and upper bounds of a subrange.
  /// \param Lower Lower bound value to store.
  /// \param Upper Upper bound value to store.
  virtual void setBounds(unsigned Lower, unsigned Upper) {}

  /// Return the DW_AT_GNU_discriminator attribute value.
  /// \returns Discriminator value, or zero if unset.
  virtual uint32_t getDiscriminator() const { return 0; }
  /// Set the DW_AT_GNU_discriminator attribute value.
  /// \param Value Discriminator value to store.
  virtual void setDiscriminator(uint32_t Value) {}

  /// Return the enumerator value string for a DW_TAG_enumerator.
  /// \returns Enumerator value string, or empty if none.
  virtual StringRef getValue() const { return {}; }
  /// Set the enumerator value string for a DW_TAG_enumerator.
  /// \param Value Enumerator constant value to store.
  virtual void setValue(StringRef Value) {}
  /// Return the string-pool index of the enumerator value.
  /// \returns String-pool index of the enumerator value, or zero if none.
  virtual size_t getValueIndex() const { return 0; }

  /// Return the DWARF accessibility code for this element.
  /// \returns Stored DWARF accessibility code.
  uint32_t getAccessibilityCode() const { return AccessibilityCode; }
  /// Set the DWARF accessibility code for this element.
  /// \param Access DWARF accessibility code to store.
  void setAccessibilityCode(uint32_t Access) { AccessibilityCode = Access; }
  /// Return a display string for a DWARF accessibility code.
  /// \param Access DWARF accessibility code to format.
  /// \returns Human-readable accessibility spelling.
  StringRef
  accessibilityString(uint32_t Access = dwarf::DW_ACCESS_private) const;

  /// Map a CodeView member access to a DWARF accessibility code.
  /// \param Access CodeView member-access enumeration value.
  /// \returns Matching DWARF accessibility code, if any.
  std::optional<uint32_t> getAccessibilityCode(codeview::MemberAccess Access);
  /// Set accessibility from a CodeView member-access value.
  /// \param Access CodeView member-access enumeration value.
  void setAccessibilityCode(codeview::MemberAccess Access) {
    if (std::optional<uint32_t> Code = getAccessibilityCode(Access))
      AccessibilityCode = Code.value();
  }

  /// Return the DWARF inline code for this element.
  /// \returns Stored DWARF inline code.
  uint32_t getInlineCode() const { return InlineCode; }
  /// Set the DWARF inline code for this element.
  /// \param Code DWARF inline code to store.
  void setInlineCode(uint32_t Code) { InlineCode = Code; }
  /// Return a display string for a DWARF inline code.
  /// \param Code DWARF inline code to format.
  /// \returns Human-readable inline-code spelling.
  StringRef inlineCodeString(uint32_t Code) const;

  /// Return the DWARF virtuality code for this element.
  /// \returns Stored DWARF virtuality code.
  uint32_t getVirtualityCode() const { return VirtualityCode; }
  /// Set the DWARF virtuality code for this element.
  /// \param Virtuality DWARF virtuality code to store.
  void setVirtualityCode(uint32_t Virtuality) { VirtualityCode = Virtuality; }
  /// Return a display string for a DWARF virtuality code.
  /// \param Virtuality DWARF virtuality code to format.
  /// \returns Human-readable virtuality spelling.
  StringRef
  virtualityString(uint32_t Virtuality = dwarf::DW_VIRTUALITY_none) const;

  /// Map a CodeView method kind to a DWARF virtuality code.
  /// \param Virtuality CodeView method-kind enumeration value.
  /// \returns Matching DWARF virtuality code, if any.
  std::optional<uint32_t> getVirtualityCode(codeview::MethodKind Virtuality);
  /// Set virtuality from a CodeView method-kind value.
  /// \param Virtuality CodeView method-kind enumeration value.
  void setVirtualityCode(codeview::MethodKind Virtuality) {
    if (std::optional<uint32_t> Code = getVirtualityCode(Virtuality))
      VirtualityCode = Code.value();
  }

  /// Return a display string for the external-linkage attribute.
  /// \returns Human-readable external-linkage spelling.
  StringRef externalString() const;

  /// Return the type element associated with this element.
  /// \returns Associated type element, or nullptr if none.
  LVElement *getType() const { return ElementType; }
  /// Return the associated type cast as an LVType, or nullptr.
  /// \returns Associated type as an LVType, or nullptr if not a type.
  LVType *getTypeAsType() const;
  /// Return the associated type cast as an LVScope, or nullptr.
  /// \returns Associated type as an LVScope, or nullptr if not a scope.
  LVScope *getTypeAsScope() const;

  /// Set the type associated with this element.
  /// \param Element Type element to associate, or nullptr to clear.
  void setType(LVElement *Element = nullptr) {
    ElementType = Element;
    if (Element) {
      setHasType();
      Element->setIsReferencedType();
    }
  }

  /// Set the type for this element, handling template parameters.
  /// \param Element Type or template-parameter element to associate.
  void setGenericType(LVElement *Element);

  /// Return the qualified name of the associated type, if any.
  /// \returns Qualified type name, or an empty string if none.
  StringRef getTypeQualifiedName() const {
    return ElementType ? ElementType->getQualifiedName() : "";
  }

  /// Return a display string for the associated type.
  /// \returns Human-readable type spelling.
  StringRef typeAsString() const;
  /// Return a display string for the associated type's offset.
  /// \returns Human-readable type-offset spelling.
  std::string typeOffsetAsString() const;
  /// Return a display string for the discriminator attribute.
  /// \returns Human-readable discriminator spelling.
  std::string discriminatorAsString() const;

  /// Walk parent scopes until \p GetFunction returns true.
  /// \param GetFunction Predicate invoked on each parent scope.
  /// \returns First parent scope for which the predicate holds, or nullptr.
  LVScope *traverseParents(LVScopeGetFunction GetFunction) const;

  /// Return the nearest enclosing function scope, or nullptr.
  /// \returns Nearest enclosing function scope, or nullptr if none.
  LVScope *getFunctionParent() const;
  /// Return the enclosing compile-unit scope, or nullptr.
  /// \returns Enclosing compile-unit scope, or nullptr if none.
  virtual LVScope *getCompileUnitParent() const;

  /// Print a reference from this element to another element.
  /// \param OS Stream that receives the printed reference.
  /// \param Full Whether to print full reference details.
  /// \param Parent Parent element providing print context.
  void printReference(raw_ostream &OS, bool Full, LVElement *Parent) const;

  /// Print the linkage name for symbols and functions with a scope.
  /// \param OS Stream that receives the printed linkage name.
  /// \param Full Whether to print full linkage-name details.
  /// \param Parent Parent element providing print context.
  /// \param Scope Scope associated with the linkage name.
  void printLinkageName(raw_ostream &OS, bool Full, LVElement *Parent,
                        LVScope *Scope) const;
  /// Print the linkage name for symbols and functions.
  /// \param OS Stream that receives the printed linkage name.
  /// \param Full Whether to print full linkage-name details.
  /// \param Parent Parent element providing print context.
  void printLinkageName(raw_ostream &OS, bool Full, LVElement *Parent) const;

  /// Generate the full name for this element from a base type.
  /// \param BaseType Base type used when building the full name.
  /// \param Name Optional name prefix or component to include.
  void resolveFullname(LVElement *BaseType, StringRef Name = emptyString());

  /// Append a generated name for an unnamed element to \p Prefix.
  /// \param Prefix String that receives the generated name suffix.
  void generateName(std::string &Prefix) const;
  /// Generate and store a name for an unnamed element.
  void generateName();

  /// Remove \p Element from this element's children, if present.
  /// \param Element Child element to remove.
  /// \returns True if the element was removed.
  virtual bool removeElement(LVElement *Element) { return false; }
  /// Update the nesting level of this element under \p Parent.
  /// \param Parent Scope that owns this element after the update.
  /// \param Moved Whether the element was moved into a different parent.
  virtual void updateLevel(LVScope *Parent, bool Moved = false);

  /// Resolve deferred properties collected during parsing.
  ///
  /// During the parsing of the debug information, the logical elements are
  /// created with information extracted from its description entries (DIE).
  /// But they are not complete for the logical view concept. A second pass
  /// is executed in order to collect their additional information.
  /// The following functions 'resolve' some of their properties, such as
  /// name, references, parents, extra information based on the element kind.
  virtual void resolve();
  /// Resolve kind-specific extra information for this element.
  virtual void resolveExtra() {}
  /// Resolve the name of this element.
  virtual void resolveName();
  /// Resolve references from this element to other elements.
  virtual void resolveReferences() {}
  /// Resolve parent relationships for this element.
  void resolveParents();

  /// Return whether this element's references match those of \p Element.
  /// \param Element Element whose references are compared.
  /// \returns True if the reference relationships match.
  bool referenceMatch(const LVElement *Element) const;

  /// Return whether this element is logically equal to \p Element.
  /// \param Element Element to compare against.
  /// \returns True if the elements are logically equal.
  bool equals(const LVElement *Element) const;

  /// Report this element as missing or added during comparison.
  /// \param Pass Comparison pass that classifies the element.
  virtual void report(LVComparePass Pass) {}

  /// Print basic and extra information for debugging IR.
  /// \param OS Stream that receives the printed information.
  /// \param Full Whether to print full element details.
  void printCommon(raw_ostream &OS, bool Full = true) const;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Dump common element information to the debug stream.
  void dumpCommon() const { printCommon(dbgs(), /*Full=*/true); }
#endif

  /// Return the dispatch table mapping element kinds to query functions.
  /// \returns Reference to the static LVElementDispatch table.
  static LVElementDispatch &getDispatch() { return Dispatch; }
};

} // end namespace logicalview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVELEMENT_H
