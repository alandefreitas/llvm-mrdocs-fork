//===-- LVScope.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the LVScope class, which is used to describe a debug
// information scope.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVSCOPE_H
#define LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVSCOPE_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/DebugInfo/LogicalView/Core/LVElement.h"
#include "llvm/DebugInfo/LogicalView/Core/LVLocation.h"
#include "llvm/DebugInfo/LogicalView/Core/LVSort.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Compiler.h"
#include <map>
#include <set>

namespace llvm {
namespace logicalview {

/// Pair of a public name's address and its code size in bytes.
using LVNameInfo = std::pair<LVAddress, uint64_t>;
/// Map from public scopes to their address and code-size info.
using LVPublicNames = std::map<LVScope *, LVNameInfo>;
/// Map from public addresses to name address and code-size info.
using LVPublicAddresses = std::map<LVAddress, LVNameInfo>;

class LVRange;

/// Kind flags that classify a logical-view scope.
enum class LVScopeKind {
  /// Scope is an aggregate (class, structure, or union).
  IsAggregate,
  /// Scope is an array type.
  IsArray,
  /// Scope is a block.
  IsBlock,
  /// Scope is a call site.
  IsCallSite,
  /// Scope is a catch block.
  IsCatchBlock,
  /// Scope is a class.
  IsClass,
  /// Scope is a compile unit.
  IsCompileUnit,
  /// Scope is an entry point.
  IsEntryPoint,
  /// Scope is an enumeration.
  IsEnumeration,
  /// Scope is a function.
  IsFunction,
  /// Scope is a function type.
  IsFunctionType,
  /// Scope is an inlined function.
  IsInlinedFunction,
  /// Scope is a label.
  IsLabel,
  /// Scope is a lexical block.
  IsLexicalBlock,
  /// Scope is a member.
  IsMember,
  /// Scope is a module.
  IsModule,
  /// Scope is a namespace.
  IsNamespace,
  /// Scope is the root of the logical view.
  IsRoot,
  /// Scope is a structure.
  IsStructure,
  /// Scope is a subprogram.
  IsSubprogram,
  /// Scope is a template.
  IsTemplate,
  /// Scope is a template alias.
  IsTemplateAlias,
  /// Scope is a template parameter pack.
  IsTemplatePack,
  /// Scope is a try block.
  IsTryBlock,
  /// Scope is a union.
  IsUnion,
  /// Sentinel past the last valid kind.
  LastEntry
};
/// Set of selected LVScopeKind values.
using LVScopeKindSet = std::set<LVScopeKind>;
/// Map from LVScopeKind to the corresponding getter member function.
using LVScopeDispatch = std::map<LVScopeKind, LVScopeGetFunction>;
/// Ordered list of LVScope getter member functions used for requests.
using LVScopeRequest = std::vector<LVScopeGetFunction>;

/// Map from DIE offsets to logical elements.
using LVOffsetElementMap = std::map<LVOffset, LVElement *>;
/// Map from DIE offsets to collections of logical lines.
using LVOffsetLinesMap = std::map<LVOffset, LVLines>;
/// Map from DIE offsets to collections of logical locations.
using LVOffsetLocationsMap = std::map<LVOffset, LVLocations>;
/// Map from DIE offsets to logical symbols.
using LVOffsetSymbolMap = std::map<LVOffset, LVSymbol *>;
/// Map from DWARF tags to lists of DIE offsets.
using LVTagOffsetsMap = std::map<dwarf::Tag, LVOffsets>;

/// Logical-view element that represents a DWARF debug-information scope.
class LLVM_ABI LVScope : public LVElement {
  enum class Property {
    HasDiscriminator,
    CanHaveRanges,
    CanHaveLines,
    HasGlobals,
    HasLocals,
    HasLines,
    HasScopes,
    HasSymbols,
    HasTypes,
    IsComdat,
    HasComdatScopes, // Compile Unit has comdat functions.
    HasRanges,
    AddedMissing, // Added missing referenced symbols.
    LastEntry
  };

  // Typed bitvector with kinds and properties for this scope.
  LVProperties<LVScopeKind> Kinds;
  LVProperties<Property> Properties;
  static LVScopeDispatch Dispatch;
  // Empty containers used in `getChildren()` in case there is no Types,
  // Symbols, or Scopes.
  static const LVTypes EmptyTypes;
  static const LVSymbols EmptySymbols;
  static const LVScopes EmptyScopes;

  // Size in bits if this scope represents also a compound type.
  uint32_t BitSize = 0;

  // Coverage factor in units (bytes).
  unsigned CoverageFactor = 0;

  // Calculate coverage factor.
  void calculateCoverage() {
    float CoveragePercentage = 0;
    LVLocation::calculateCoverage(Ranges.get(), CoverageFactor,
                                  CoveragePercentage);
  }

  // Decide if the scope will be printed, using some conditions given by:
  // only-globals, only-locals, a-pattern.
  bool resolvePrinting() const;

  // Find the current scope in the given 'Targets'.
  LVScope *findIn(const LVScopes *Targets) const;

  // Traverse the scope parent tree, executing the given callback function
  // on each scope.
  void traverseParents(LVScopeGetFunction GetFunction,
                       LVScopeSetFunction SetFunction);

protected:
  /// Child types owned by this scope.
  std::unique_ptr<LVTypes> Types;
  /// Child symbols owned by this scope.
  std::unique_ptr<LVSymbols> Symbols;
  /// Child scopes owned by this scope.
  std::unique_ptr<LVScopes> Scopes;
  /// Child line records owned by this scope.
  std::unique_ptr<LVLines> Lines;
  /// Address ranges associated with this scope.
  std::unique_ptr<LVLocations> Ranges;

  /// Resolve the template parameters and arguments relationship.
  void resolveTemplate();
  /// Print encoded template arguments for this scope.
  /// \param OS Stream that receives the printed arguments.
  /// \param Full Whether to include full detail.
  void printEncodedArgs(raw_ostream &OS, bool Full) const;

  /// Print the active address ranges for this scope.
  /// \param OS Stream that receives the printed ranges.
  /// \param Full Whether to include full detail.
  void printActiveRanges(raw_ostream &OS, bool Full = true) const;
  /// Print size contribution information for this scope.
  /// \param OS Stream that receives the printed sizes.
  virtual void printSizes(raw_ostream &OS) const {}
  /// Print a summary of element counts for this scope.
  /// \param OS Stream that receives the printed summary.
  virtual void printSummary(raw_ostream &OS) const {}

  /// Return the encoded template arguments string.
  /// \returns Encoded template arguments, or empty if none.
  virtual StringRef getEncodedArgs() const { return StringRef(); }
  /// Store the encoded template arguments string.
  /// \param EncodedArgs Encoded template arguments to associate with this scope.
  virtual void setEncodedArgs(StringRef EncodedArgs) {}

public:
  /// Construct a logical scope and mark it for printing.
  LVScope() : LVElement(LVSubclassID::LV_SCOPE) {
    setIsScope();
    setIncludeInPrint();
  }
  /// Copy construction is not allowed.
  /// \param Other Unused source logical scope.
  LVScope(const LVScope &Other) = delete;
  /// Copy assignment is not allowed.
  /// \param Other Unused source logical scope.
  LVScope &operator=(const LVScope &Other) = delete;
  /// Destroy the logical scope.
  ~LVScope() override = default;

  /// Return true when \p Element is an LVScope.
  /// \param Element Element to test for the LVScope subclass.
  /// \returns True if \p Element has subclass ID LV_SCOPE.
  static bool classof(const LVElement *Element) {
    return Element->getSubclassID() == LVSubclassID::LV_SCOPE;
  }

  /// Return whether this scope is an aggregate (class, structure, or union).
  /// \returns True when the aggregate kind is set.
  bool getIsAggregate() const { return Kinds.get(LVScopeKind::IsAggregate); }
  /// Mark this scope as an aggregate (class, structure, or union).
  void setIsAggregate() { Kinds.set(LVScopeKind::IsAggregate); }
  /// Clear the aggregate kind on this scope.
  void resetIsAggregate() { Kinds.reset(LVScopeKind::IsAggregate); }
  /// Return whether this scope is an array type.
  /// \returns True when the array kind is set.
  bool getIsArray() const { return Kinds.get(LVScopeKind::IsArray); }
  /// Mark this scope as an array type.
  void setIsArray() { Kinds.set(LVScopeKind::IsArray); }
  /// Clear the array-type kind on this scope.
  void resetIsArray() { Kinds.reset(LVScopeKind::IsArray); }
  /// Return whether this scope is a block scope.
  /// \returns True when the block kind is set.
  bool getIsBlock() const { return Kinds.get(LVScopeKind::IsBlock); }
  /// Mark this scope as a block scope.
  void setIsBlock() {
    Kinds.set(LVScopeKind::IsBlock);
    setCanHaveRanges();
    setCanHaveLines();
  }
  /// Clear the block kind on this scope.
  void resetIsBlock() { Kinds.reset(LVScopeKind::IsBlock); }
  /// Return whether this scope is a call site.
  /// \returns True when the call-site kind is set.
  bool getIsCallSite() const { return Kinds.get(LVScopeKind::IsCallSite); }
  /// Mark this scope as a call site.
  void setIsCallSite() {
    Kinds.set(LVScopeKind::IsCallSite);
    setIsFunction();
  }
  /// Clear the call-site kind on this scope.
  void resetIsCallSite() { Kinds.reset(LVScopeKind::IsCallSite); }
  /// Return whether this scope is a catch block.
  /// \returns True when the catch-block kind is set.
  bool getIsCatchBlock() const { return Kinds.get(LVScopeKind::IsCatchBlock); }
  /// Mark this scope as a catch block.
  void setIsCatchBlock() {
    Kinds.set(LVScopeKind::IsCatchBlock);
    setIsBlock();
  }
  /// Clear the catch-block kind on this scope.
  void resetIsCatchBlock() { Kinds.reset(LVScopeKind::IsCatchBlock); }
  /// Return whether this scope is a class.
  /// \returns True when the class kind is set.
  bool getIsClass() const { return Kinds.get(LVScopeKind::IsClass); }
  /// Mark this scope as a class.
  void setIsClass() {
    Kinds.set(LVScopeKind::IsClass);
    setIsAggregate();
  }
  /// Clear the class kind on this scope.
  void resetIsClass() { Kinds.reset(LVScopeKind::IsClass); }
  /// Return whether this scope is a compile unit.
  /// \returns True when the compile-unit kind is set.
  bool getIsCompileUnit() const { return Kinds.get(LVScopeKind::IsCompileUnit); }
  /// Mark this scope as a compile unit.
  void setIsCompileUnit() {
    Kinds.set(LVScopeKind::IsCompileUnit);
    setCanHaveRanges();
    setCanHaveLines();
    setTransformName();
  }
  /// Clear the compile-unit kind on this scope.
  void resetIsCompileUnit() { Kinds.reset(LVScopeKind::IsCompileUnit); }
  /// Return whether this scope is an entry point.
  /// \returns True when the entry-point kind is set.
  bool getIsEntryPoint() const { return Kinds.get(LVScopeKind::IsEntryPoint); }
  /// Mark this scope as an entry point.
  void setIsEntryPoint() {
    Kinds.set(LVScopeKind::IsEntryPoint);
    setIsFunction();
  }
  /// Clear the entry-point kind on this scope.
  void resetIsEntryPoint() { Kinds.reset(LVScopeKind::IsEntryPoint); }
  /// Return whether this scope is an enumeration.
  /// \returns True when the enumeration kind is set.
  bool getIsEnumeration() const { return Kinds.get(LVScopeKind::IsEnumeration); }
  /// Mark this scope as an enumeration.
  void setIsEnumeration() { Kinds.set(LVScopeKind::IsEnumeration); }
  /// Clear the enumeration kind on this scope.
  void resetIsEnumeration() { Kinds.reset(LVScopeKind::IsEnumeration); }
  /// Return whether this scope is a function.
  /// \returns True when the function kind is set.
  bool getIsFunction() const { return Kinds.get(LVScopeKind::IsFunction); }
  /// Mark this scope as a function.
  void setIsFunction() {
    Kinds.set(LVScopeKind::IsFunction);
    setCanHaveRanges();
    setCanHaveLines();
  }
  /// Clear the function kind on this scope.
  void resetIsFunction() { Kinds.reset(LVScopeKind::IsFunction); }
  /// Return whether this scope is a function type.
  /// \returns True when the function-type kind is set.
  bool getIsFunctionType() const { return Kinds.get(LVScopeKind::IsFunctionType); }
  /// Mark this scope as a function type.
  void setIsFunctionType() {
    Kinds.set(LVScopeKind::IsFunctionType);
    setIsFunction();
  }
  /// Clear the function-type kind on this scope.
  void resetIsFunctionType() { Kinds.reset(LVScopeKind::IsFunctionType); }
  /// Return whether this scope is an inlined function.
  /// \returns True when the inlined-function kind is set.
  bool getIsInlinedFunction() const { return Kinds.get(LVScopeKind::IsInlinedFunction); }
  /// Mark this scope as an inlined function.
  void setIsInlinedFunction() {
    Kinds.set(LVScopeKind::IsInlinedFunction);
    setIsFunction();
    setIsInlined();
  }
  /// Clear the inlined-function kind on this scope.
  void resetIsInlinedFunction() { Kinds.reset(LVScopeKind::IsInlinedFunction); }
  /// Return whether this scope is a label.
  /// \returns True when the label kind is set.
  bool getIsLabel() const { return Kinds.get(LVScopeKind::IsLabel); }
  /// Mark this scope as a label.
  void setIsLabel() {
    Kinds.set(LVScopeKind::IsLabel);
    setIsFunction();
  }
  /// Clear the label kind on this scope.
  void resetIsLabel() { Kinds.reset(LVScopeKind::IsLabel); }
  /// Return whether this scope is a lexical block.
  /// \returns True when the lexical-block kind is set.
  bool getIsLexicalBlock() const { return Kinds.get(LVScopeKind::IsLexicalBlock); }
  /// Mark this scope as a lexical block.
  void setIsLexicalBlock() {
    Kinds.set(LVScopeKind::IsLexicalBlock);
    setIsBlock();
  }
  /// Clear the lexical-block kind on this scope.
  void resetIsLexicalBlock() { Kinds.reset(LVScopeKind::IsLexicalBlock); }
  /// Return whether this scope is a member.
  /// \returns True when the member kind is set.
  bool getIsMember() const { return Kinds.get(LVScopeKind::IsMember); }
  /// Mark this scope as a member.
  void setIsMember() { Kinds.set(LVScopeKind::IsMember); }
  /// Clear the member kind on this scope.
  void resetIsMember() { Kinds.reset(LVScopeKind::IsMember); }
  /// Return whether this scope is a namespace.
  /// \returns True when the namespace kind is set.
  bool getIsNamespace() const { return Kinds.get(LVScopeKind::IsNamespace); }
  /// Mark this scope as a namespace.
  void setIsNamespace() { Kinds.set(LVScopeKind::IsNamespace); }
  /// Clear the namespace kind on this scope.
  void resetIsNamespace() { Kinds.reset(LVScopeKind::IsNamespace); }
  /// Return whether this scope is the root scope.
  /// \returns True when the root kind is set.
  bool getIsRoot() const { return Kinds.get(LVScopeKind::IsRoot); }
  /// Mark this scope as the root scope.
  void setIsRoot() {
    Kinds.set(LVScopeKind::IsRoot);
    setTransformName();
  }
  /// Clear the root-scope kind on this scope.
  void resetIsRoot() { Kinds.reset(LVScopeKind::IsRoot); }
  /// Return whether this scope is a structure.
  /// \returns True when the structure kind is set.
  bool getIsStructure() const { return Kinds.get(LVScopeKind::IsStructure); }
  /// Mark this scope as a structure.
  void setIsStructure() {
    Kinds.set(LVScopeKind::IsStructure);
    setIsAggregate();
  }
  /// Clear the structure kind on this scope.
  void resetIsStructure() { Kinds.reset(LVScopeKind::IsStructure); }
  /// Return whether this scope is a subprogram.
  /// \returns True when the subprogram kind is set.
  bool getIsSubprogram() const { return Kinds.get(LVScopeKind::IsSubprogram); }
  /// Mark this scope as a subprogram.
  void setIsSubprogram() {
    Kinds.set(LVScopeKind::IsSubprogram);
    setIsFunction();
  }
  /// Clear the subprogram kind on this scope.
  void resetIsSubprogram() { Kinds.reset(LVScopeKind::IsSubprogram); }
  /// Return whether this scope is a template.
  /// \returns True when the template kind is set.
  bool getIsTemplate() const { return Kinds.get(LVScopeKind::IsTemplate); }
  /// Mark this scope as a template.
  void setIsTemplate() { Kinds.set(LVScopeKind::IsTemplate); }
  /// Clear the template kind on this scope.
  void resetIsTemplate() { Kinds.reset(LVScopeKind::IsTemplate); }
  /// Return whether this scope is a template alias.
  /// \returns True when the template-alias kind is set.
  bool getIsTemplateAlias() const { return Kinds.get(LVScopeKind::IsTemplateAlias); }
  /// Mark this scope as a template alias.
  void setIsTemplateAlias() { Kinds.set(LVScopeKind::IsTemplateAlias); }
  /// Clear the template-alias kind on this scope.
  void resetIsTemplateAlias() { Kinds.reset(LVScopeKind::IsTemplateAlias); }
  /// Return whether this scope is a template parameter pack.
  /// \returns True when the template-parameter-pack kind is set.
  bool getIsTemplatePack() const { return Kinds.get(LVScopeKind::IsTemplatePack); }
  /// Mark this scope as a template parameter pack.
  void setIsTemplatePack() { Kinds.set(LVScopeKind::IsTemplatePack); }
  /// Clear the template-parameter-pack kind on this scope.
  void resetIsTemplatePack() { Kinds.reset(LVScopeKind::IsTemplatePack); }
  /// Return whether this scope is a try block.
  /// \returns True when the try-block kind is set.
  bool getIsTryBlock() const { return Kinds.get(LVScopeKind::IsTryBlock); }
  /// Mark this scope as a try block.
  void setIsTryBlock() {
    Kinds.set(LVScopeKind::IsTryBlock);
    setIsBlock();
  }
  /// Clear the try-block kind on this scope.
  void resetIsTryBlock() { Kinds.reset(LVScopeKind::IsTryBlock); }
  /// Return whether this scope is a union.
  /// \returns True when the union kind is set.
  bool getIsUnion() const { return Kinds.get(LVScopeKind::IsUnion); }
  /// Mark this scope as a union.
  void setIsUnion() {
    Kinds.set(LVScopeKind::IsUnion);
    setIsAggregate();
  }
  /// Clear the union kind on this scope.
  void resetIsUnion() { Kinds.reset(LVScopeKind::IsUnion); }
  /// Return whether this scope is a module.
  /// \returns True when the module kind is set.
  bool getIsModule() const { return Kinds.get(LVScopeKind::IsModule); }
  /// Mark this scope as a module.
  void setIsModule() {
    Kinds.set(LVScopeKind::IsModule);
    setCanHaveRanges();
    setCanHaveLines();
  }
  /// Clear the module kind on this scope.
  void resetIsModule() { Kinds.reset(LVScopeKind::IsModule); }

  /// Return whether this scope has a discriminator.
  /// \returns True when the discriminator property is set.
  bool getHasDiscriminator() const { return Properties.get(Property::HasDiscriminator); }
  /// Mark this scope as having a discriminator.
  void setHasDiscriminator() { Properties.set(Property::HasDiscriminator); }
  /// Clear the discriminator property on this scope.
  void resetHasDiscriminator() { Properties.reset(Property::HasDiscriminator); }
  /// Return whether this scope can have address ranges.
  /// \returns True when the can-have-ranges property is set.
  bool getCanHaveRanges() const { return Properties.get(Property::CanHaveRanges); }
  /// Mark this scope as able to have address ranges.
  void setCanHaveRanges() { Properties.set(Property::CanHaveRanges); }
  /// Clear the can-have-ranges property on this scope.
  void resetCanHaveRanges() { Properties.reset(Property::CanHaveRanges); }
  /// Return whether this scope can have line records.
  /// \returns True when the can-have-lines property is set.
  bool getCanHaveLines() const { return Properties.get(Property::CanHaveLines); }
  /// Mark this scope as able to have line records.
  void setCanHaveLines() { Properties.set(Property::CanHaveLines); }
  /// Clear the can-have-lines property on this scope.
  void resetCanHaveLines() { Properties.reset(Property::CanHaveLines); }
  /// Return whether this scope contains global symbols.
  /// \returns True when the has-globals property is set.
  bool getHasGlobals() const { return Properties.get(Property::HasGlobals); }
  /// Mark this scope as containing global symbols.
  void setHasGlobals() { Properties.set(Property::HasGlobals); }
  /// Clear the has-globals property on this scope.
  void resetHasGlobals() { Properties.reset(Property::HasGlobals); }
  /// Return whether this scope contains local symbols.
  /// \returns True when the has-locals property is set.
  bool getHasLocals() const { return Properties.get(Property::HasLocals); }
  /// Mark this scope as containing local symbols.
  void setHasLocals() { Properties.set(Property::HasLocals); }
  /// Clear the has-locals property on this scope.
  void resetHasLocals() { Properties.reset(Property::HasLocals); }
  /// Return whether this scope has line records.
  /// \returns True when the has-lines property is set.
  bool getHasLines() const { return Properties.get(Property::HasLines); }
  /// Mark this scope as having line records.
  void setHasLines() { Properties.set(Property::HasLines); }
  /// Clear the has-lines property on this scope.
  void resetHasLines() { Properties.reset(Property::HasLines); }
  /// Return whether this scope has nested child scopes.
  /// \returns True when the has-scopes property is set.
  bool getHasScopes() const { return Properties.get(Property::HasScopes); }
  /// Mark this scope as having nested child scopes.
  void setHasScopes() { Properties.set(Property::HasScopes); }
  /// Clear the has-scopes property on this scope.
  void resetHasScopes() { Properties.reset(Property::HasScopes); }
  /// Return whether this scope has symbols.
  /// \returns True when the has-symbols property is set.
  bool getHasSymbols() const { return Properties.get(Property::HasSymbols); }
  /// Mark this scope as having symbols.
  void setHasSymbols() { Properties.set(Property::HasSymbols); }
  /// Clear the has-symbols property on this scope.
  void resetHasSymbols() { Properties.reset(Property::HasSymbols); }
  /// Return whether this scope has types.
  /// \returns True when the has-types property is set.
  bool getHasTypes() const { return Properties.get(Property::HasTypes); }
  /// Mark this scope as having types.
  void setHasTypes() { Properties.set(Property::HasTypes); }
  /// Clear the has-types property on this scope.
  void resetHasTypes() { Properties.reset(Property::HasTypes); }
  /// Return whether this scope is a COMDAT.
  /// \returns True when the COMDAT property is set.
  bool getIsComdat() const { return Properties.get(Property::IsComdat); }
  /// Mark this scope as a COMDAT.
  void setIsComdat() { Properties.set(Property::IsComdat); }
  /// Clear the COMDAT property on this scope.
  void resetIsComdat() { Properties.reset(Property::IsComdat); }
  /// Return whether this compile unit has COMDAT scopes.
  /// \returns True when the has-COMDAT-scopes property is set.
  bool getHasComdatScopes() const { return Properties.get(Property::HasComdatScopes); }
  /// Mark this compile unit as having COMDAT scopes.
  void setHasComdatScopes() { Properties.set(Property::HasComdatScopes); }
  /// Clear the has-COMDAT-scopes property on this scope.
  void resetHasComdatScopes() { Properties.reset(Property::HasComdatScopes); }
  /// Return whether this scope has address ranges.
  /// \returns True when the has-ranges property is set.
  bool getHasRanges() const { return Properties.get(Property::HasRanges); }
  /// Mark this scope as having address ranges.
  void setHasRanges() { Properties.set(Property::HasRanges); }
  /// Clear the has-ranges property on this scope.
  void resetHasRanges() { Properties.reset(Property::HasRanges); }
  /// Return whether missing referenced symbols were added.
  /// \returns True when missing referenced symbols were added.
  bool getAddedMissing() const { return Properties.get(Property::AddedMissing); }
  /// Mark this scope as having added missing referenced symbols.
  void setAddedMissing() { Properties.set(Property::AddedMissing); }
  /// Clear the added-missing property on this scope.
  void resetAddedMissing() { Properties.reset(Property::AddedMissing); }

  /// Return whether this scope is a compile unit.
  /// \returns True when the compile-unit kind is set.
  bool isCompileUnit() const override { return getIsCompileUnit(); }
  /// Return whether this scope is the root scope.
  /// \returns True when the root kind is set.
  bool isRoot() const override { return getIsRoot(); }

  /// Return a string naming the kind of this scope.
  /// \returns C string describing the scope kind.
  const char *kind() const override;

  /// Return the child line records of this scope.
  /// \returns Pointer to the lines collection, or nullptr if none.
  const LVLines *getLines() const { return Lines.get(); }
  /// Return the address ranges of this scope.
  /// \returns Pointer to the ranges collection, or nullptr if none.
  const LVLocations *getRanges() const { return Ranges.get(); }
  /// Return the child scopes of this scope.
  /// \returns Pointer to the scopes collection, or nullptr if none.
  const LVScopes *getScopes() const { return Scopes.get(); }
  /// Return the child symbols of this scope.
  /// \returns Pointer to the symbols collection, or nullptr if none.
  const LVSymbols *getSymbols() const { return Symbols.get(); }
  /// Return the child types of this scope.
  /// \returns Pointer to the types collection, or nullptr if none.
  const LVTypes *getTypes() const { return Types.get(); }
  /// Return a view over child scopes, types, and symbols, in that order.
  ///
  /// Calling `LVScope::sort()` ensures that each of groups is sorted according
  /// to the given criteria (see also `LVOptions::setSortMode()`). Because
  /// `getChildren()` iterates over the concatenation, the result returned by
  /// this function is not necessarily sorted. If order is important, use
  /// `getSortedChildren()`.
  /// \returns Concatenated view of child scopes, types, and symbols.
  LVElementsView getChildren() const {
    return llvm::concat<LVElement *const>(Scopes ? *Scopes : EmptyScopes,
                                          Types ? *Types : EmptyTypes,
                                          Symbols ? *Symbols : EmptySymbols);
  }
  /// Return child scopes, types, and symbols sorted with \p SortFunction.
  ///
  /// This requires copy + sort; if order is not important, use
  /// `getChildren()` instead.
  /// \param SortFunction Comparator used to order the children.
  /// \returns Sorted vector of child elements.
  LVElements getSortedChildren(
      LVSortFunction SortFunction = llvm::logicalview::getSortFunction()) const;

  /// Add \p Element as a child of this scope.
  /// \param Element Element to add.
  void addElement(LVElement *Element);
  /// Add \p Line as a child line of this scope.
  /// \param Line Line to add.
  void addElement(LVLine *Line);
  /// Add \p Scope as a nested child scope.
  /// \param Scope Scope to add.
  void addElement(LVScope *Scope);
  /// Add \p Symbol as a child symbol of this scope.
  /// \param Symbol Symbol to add.
  void addElement(LVSymbol *Symbol);
  /// Add \p Type as a child type of this scope.
  /// \param Type Type to add.
  void addElement(LVType *Type);
  /// Add \p Location as an address-range object of this scope.
  /// \param Location Location describing an address range.
  void addObject(LVLocation *Location);
  /// Add an address range from \p LowerAddress to \p UpperAddress.
  /// \param LowerAddress Inclusive lower bound of the range.
  /// \param UpperAddress Exclusive upper bound of the range.
  void addObject(LVAddress LowerAddress, LVAddress UpperAddress);

  /// Add missing elements from the specification or abstract-origin reference.
  ///
  /// \p Reference is the scope associated with any DW_AT_specification or
  /// DW_AT_abstract_origin.
  /// \param Reference Referenced scope providing elements to merge in.
  void addMissingElements(LVScope *Reference);

  /// Traverse parent scopes and children, applying the given callbacks.
  /// \param GetFunction Callback that queries an object property.
  /// \param SetFunction Callback that sets an object property.
  void traverseParentsAndChildren(LVObjectGetFunction GetFunction,
                                  LVObjectSetFunction SetFunction);

  /// Return the number of child line records.
  /// \returns Count of lines, or zero if none.
  size_t lineCount() const { return Lines ? Lines->size() : 0; }
  /// Return the number of address ranges.
  /// \returns Count of ranges, or zero if none.
  size_t rangeCount() const { return Ranges ? Ranges->size() : 0; }
  /// Return the number of child scopes.
  /// \returns Count of scopes, or zero if none.
  size_t scopeCount() const { return Scopes ? Scopes->size() : 0; }
  /// Return the number of child symbols.
  /// \returns Count of symbols, or zero if none.
  size_t symbolCount() const { return Symbols ? Symbols->size() : 0; }
  /// Return the number of child types.
  /// \returns Count of types, or zero if none.
  size_t typeCount() const { return Types ? Types->size() : 0; }

  /// Find the outermost parent scope that contains \p Address.
  /// \param Address Code address to locate.
  /// \returns Containing parent scope, or nullptr if none.
  LVScope *outermostParent(LVAddress Address);

  /// Collect symbol locations into \p LocationList.
  /// \param LocationList Destination list of collected locations.
  /// \param ValidLocation Predicate used to validate each location.
  /// \param RecordInvalid Whether to record invalid locations as well.
  void getLocations(LVLocations &LocationList, LVValidLocation ValidLocation,
                    bool RecordInvalid = false);
  /// Collect address ranges into \p LocationList.
  /// \param LocationList Destination list of collected ranges.
  /// \param ValidLocation Predicate used to validate each range.
  /// \param RecordInvalid Whether to record invalid ranges as well.
  void getRanges(LVLocations &LocationList, LVValidLocation ValidLocation,
                 bool RecordInvalid = false);
  /// Collect address ranges into \p RangeList.
  /// \param RangeList Destination range collection.
  void getRanges(LVRange &RangeList);

  /// Return the coverage factor in bytes for this scope.
  /// \returns Coverage factor in units of bytes.
  unsigned getCoverageFactor() const { return CoverageFactor; }

  /// Print this scope and its children according to the print options.
  /// \param Split Whether to split output across files.
  /// \param Match Whether to print only matched elements.
  /// \param Print Whether printing is enabled.
  /// \param OS Stream that receives the printed output.
  /// \param Full Whether to include full detail.
  /// \returns Success or an error describing the print failure.
  Error doPrint(bool Split, bool Match, bool Print, raw_ostream &OS,
                bool Full = true) const override;
  /// Sort child elements using the `--output-sort` command-line option.
  void sort();

  /// Collect template parameter types into \p Params.
  /// \param Params Destination list of template parameter types.
  /// \returns True when template parameter types were found.
  bool getTemplateParameterTypes(LVTypes &Params);

  /// Return the DW_AT_specification, DW_AT_abstract_origin, or DW_AT_extension
  /// referenced scope.
  /// \returns Referenced scope, or nullptr if none.
  virtual LVScope *getReference() const { return nullptr; }

  /// Return the compile-unit parent of this scope.
  /// \returns Owning compile-unit scope, or nullptr if none.
  LVScope *getCompileUnitParent() const override {
    return LVElement::getCompileUnitParent();
  }

  /// Follow abstract-origin and specification references and update the name.
  ///
  /// Follows a chain of references given by DW_AT_abstract_origin and/or
  /// DW_AT_specification and updates the scope name.
  /// \returns Resolved name after following the reference chain.
  StringRef resolveReferencesChain();

  /// Remove \p Element from this scope's children.
  /// \param Element Child element to remove.
  /// \returns True when the element was removed.
  bool removeElement(LVElement *Element) override;
  /// Update the nesting level after a parent change.
  /// \param Parent New parent scope.
  /// \param Moved Whether the element was moved rather than first attached.
  void updateLevel(LVScope *Parent, bool Moved) override;

  /// Return the bit size when this scope also represents a compound type.
  /// \returns Size in bits, or zero if unset.
  uint32_t getBitSize() const override { return BitSize; }
  /// Set the bit size when this scope also represents a compound type.
  /// \param Size Size in bits to store.
  void setBitSize(uint32_t Size) override { BitSize = Size; }

  /// Resolve attributes and children for this scope.
  void resolve() override;
  /// Resolve the display name for this scope.
  void resolveName() override;
  /// Resolve references from this scope to other elements.
  void resolveReferences() override;

  /// Append the chain of parent names into \p QualifiedName.
  /// \param QualifiedName String that receives the qualified name.
  void getQualifiedName(std::string &QualifiedName) const;
  /// Encode this scope's template arguments into \p Name.
  /// \param Name String that receives the encoded template arguments.
  void encodeTemplateArguments(std::string &Name) const;
  /// Encode template arguments from \p Types into \p Name.
  /// \param Name String that receives the encoded template arguments.
  /// \param Types Template argument types to encode.
  void encodeTemplateArguments(std::string &Name, const LVTypes *Types) const;

  /// Resolve all child elements of this scope.
  void resolveElements();

  /// Mark parents of reference scopes that are missing from the targets.
  ///
  /// Iterate through the \p References set and check that all its elements
  /// are present in the \p Targets set. For a missing element, mark its
  /// parents as missing.
  /// \param References Scopes expected to appear in the target set.
  /// \param Targets Scopes available for matching.
  /// \param TraverseChildren Whether to recurse into child scopes.
  static void markMissingParents(const LVScopes *References,
                                 const LVScopes *Targets,
                                 bool TraverseChildren);

  /// Mark parents when this scope is missing from \p Target.
  ///
  /// Checks if the current scope is contained within the target scope.
  /// Depending on the result, the callback may be performed.
  /// \param Target Scope that should contain this scope.
  /// \param TraverseChildren Whether to recurse into child scopes.
  virtual void markMissingParents(const LVScope *Target, bool TraverseChildren);

  /// Return true if this scope and \p Scope have the same number of children.
  /// \param Scope Scope to compare child counts against.
  /// \returns True when both scopes have matching child counts.
  virtual bool equalNumberOfChildren(const LVScope *Scope) const;

  /// Return true if this scope is logically equal to \p Scope.
  /// \param Scope Scope to compare against.
  /// \returns True when the scopes are logically equal.
  virtual bool equals(const LVScope *Scope) const;

  /// Return true if \p References are logically equal to \p Targets.
  /// \param References Reference scope set.
  /// \param Targets Target scope set.
  /// \returns True when both sets are logically equal.
  static bool equals(const LVScopes *References, const LVScopes *Targets);

  /// Find a scope in \p Scopes that is logically equal to this scope.
  /// \param Scopes Candidate scopes to search.
  /// \returns Matching scope, or nullptr if none.
  virtual LVScope *findEqualScope(const LVScopes *Scopes) const;

  /// Report this scope as missing or added during comparison.
  /// \param Pass Comparison pass that classifies the scope.
  void report(LVComparePass Pass) override;

  /// Return the shared dispatch map from scope kinds to getters.
  /// \returns Reference to the static LVScopeDispatch table.
  static LVScopeDispatch &getDispatch() { return Dispatch; }

  /// Print this scope to \p OS.
  /// \param OS Stream that receives the printed scope.
  /// \param Full Whether to include full detail.
  void print(raw_ostream &OS, bool Full = true) const override;
  /// Print scope-specific extra details to \p OS.
  /// \param OS Stream that receives the printed details.
  /// \param Full Whether to include full detail.
  void printExtra(raw_ostream &OS, bool Full = true) const override;
  /// Print warnings associated with this scope to \p OS.
  /// \param OS Stream that receives the printed warnings.
  /// \param Full Whether to include full detail.
  virtual void printWarnings(raw_ostream &OS, bool Full = true) const {}
  /// Print elements that matched a selection pattern.
  /// \param OS Stream that receives the printed matches.
  /// \param UseMatchedElements Whether to print matched elements rather than scopes.
  virtual void printMatchedElements(raw_ostream &OS, bool UseMatchedElements) {}
};

/// Logical scope representing a DWARF union, structure, or class.
class LLVM_ABI LVScopeAggregate final : public LVScope {
  LVScope *Reference = nullptr; // DW_AT_specification, DW_AT_abstract_origin.
  size_t EncodedArgsIndex = 0;  // Template encoded arguments.

public:
  /// Construct an aggregate scope.
  LVScopeAggregate() : LVScope() {}
  /// Copy construction is not allowed.
  /// \param Other Unused source aggregate scope.
  LVScopeAggregate(const LVScopeAggregate &Other) = delete;
  /// Copy assignment is not allowed.
  /// \param Other Unused source aggregate scope.
  LVScopeAggregate &operator=(const LVScopeAggregate &Other) = delete;
  /// Destroy the aggregate scope.
  ~LVScopeAggregate() override = default;

  /// Return the DW_AT_specification or DW_AT_abstract_origin reference.
  /// \returns Referenced scope, or nullptr if none.
  LVScope *getReference() const override { return Reference; }
  /// Set the DW_AT_specification or DW_AT_abstract_origin reference.
  /// \param Scope Referenced aggregate or related scope.
  void setReference(LVScope *Scope) override {
    Reference = Scope;
    setHasReference();
  }
  /// Set the reference from a generic element.
  /// \param Element Element cast to an LVScope reference.
  void setReference(LVElement *Element) override {
    setReference(static_cast<LVScope *>(Element));
  }

  /// Return the encoded template arguments string.
  /// \returns Encoded template arguments from the string pool.
  StringRef getEncodedArgs() const override {
    return getStringPool().getString(EncodedArgsIndex);
  }
  /// Store the encoded template arguments string.
  /// \param EncodedArgs Encoded template arguments to store.
  void setEncodedArgs(StringRef EncodedArgs) override {
    EncodedArgsIndex = getStringPool().getIndex(EncodedArgs);
  }

  /// Return true if this scope is logically equal to \p Scope.
  /// \param Scope Scope to compare against.
  /// \returns True when the scopes are logically equal.
  bool equals(const LVScope *Scope) const override;

  /// Find a scope in \p Scopes that is logically equal to this scope.
  /// \param Scopes Candidate scopes to search.
  /// \returns Matching scope, or nullptr if none.
  LVScope *findEqualScope(const LVScopes *Scopes) const override;

  /// Print aggregate-specific extra details to \p OS.
  /// \param OS Stream that receives the printed details.
  /// \param Full Whether to include full detail.
  void printExtra(raw_ostream &OS, bool Full = true) const override;
};

/// Logical scope representing a DWARF template alias.
class LLVM_ABI LVScopeAlias final : public LVScope {
public:
  /// Construct a template-alias scope and mark it as such.
  LVScopeAlias() : LVScope() {
    setIsTemplateAlias();
    setIsTemplate();
  }
  /// Copy construction is not allowed.
  /// \param Other Unused source template-alias scope.
  LVScopeAlias(const LVScopeAlias &Other) = delete;
  /// Copy assignment is not allowed.
  /// \param Other Unused source template-alias scope.
  LVScopeAlias &operator=(const LVScopeAlias &Other) = delete;
  /// Destroy the template-alias scope.
  ~LVScopeAlias() override = default;

  /// Return true if this scope is logically equal to \p Scope.
  /// \param Scope Scope to compare against.
  /// \returns True when the scopes are logically equal.
  bool equals(const LVScope *Scope) const override;

  /// Print template-alias-specific extra details to \p OS.
  /// \param OS Stream that receives the printed details.
  /// \param Full Whether to include full detail.
  void printExtra(raw_ostream &OS, bool Full = true) const override;
};

/// Logical scope representing a DWARF array type (DW_TAG_array_type).
class LLVM_ABI LVScopeArray final : public LVScope {
public:
  /// Construct an array scope and mark it as such.
  LVScopeArray() : LVScope() { setIsArray(); }
  /// Copy construction is not allowed.
  /// \param Other Unused source array scope.
  LVScopeArray(const LVScopeArray &Other) = delete;
  /// Copy assignment is not allowed.
  /// \param Other Unused source array scope.
  LVScopeArray &operator=(const LVScopeArray &Other) = delete;
  /// Destroy the array scope.
  ~LVScopeArray() override = default;

  /// Resolve array-specific attributes and children.
  void resolveExtra() override;

  /// Return true if this scope is logically equal to \p Scope.
  /// \param Scope Scope to compare against.
  /// \returns True when the scopes are logically equal.
  bool equals(const LVScope *Scope) const override;

  /// Print array-specific extra details to \p OS.
  /// \param OS Stream that receives the printed details.
  /// \param Full Whether to include full detail.
  void printExtra(raw_ostream &OS, bool Full = true) const override;
};

/// Logical scope representing a DWARF compilation unit (CU).
class LLVM_ABI LVScopeCompileUnit final : public LVScope {
  // Names (files and directories) used by the Compile Unit.
  std::vector<size_t> Filenames;

  // As the .debug_pubnames section has been removed in DWARF5, we have a
  // similar functionality, which is used by the decoded functions. We use
  // the low-pc and high-pc for those scopes that are marked as public, in
  // order to support DWARF and CodeView.
  LVPublicNames PublicNames;

  // Toolchain producer.
  size_t ProducerIndex = 0;

  // Compilation directory name.
  size_t CompilationDirectoryIndex = 0;

  // Source language.
  LVSourceLanguage SourceLanguage{};

  // Used by the CodeView Reader.
  codeview::CPUType CompilationCPUType = codeview::CPUType::X64;

  // Keep record of elements. They are needed at the compilation unit level
  // to print the summary at the end of the printing.
  LVCounter Allocated;
  LVCounter Found;
  LVCounter Printed;

  // Elements that match a given command line pattern.
  LVElements MatchedElements;
  LVScopes MatchedScopes;

  // It records the mapping between logical lines representing a debug line
  // entry and its address in the text section. It is used to find a line
  // giving its exact or closest address. To support comdat functions, all
  // addresses for the same section are recorded in the same map.
  using LVAddressToLine = std::map<LVAddress, LVLine *>;
  LVDoubleMap<LVSectionIndex, LVAddress, LVLine *> SectionMappings;

  // DWARF Tags (Tag, Element list).
  LVTagOffsetsMap DebugTags;

  // Offsets associated with objects being flagged as having invalid data
  // (ranges, locations, lines zero or coverages).
  LVOffsetElementMap WarningOffsets;

  // Symbols with invalid locations. (Symbol, Location List).
  LVOffsetLocationsMap InvalidLocations;

  // Symbols with invalid coverage values.
  LVOffsetSymbolMap InvalidCoverages;

  // Scopes with invalid ranges (Scope, Range list).
  LVOffsetLocationsMap InvalidRanges;

  // Scopes with lines zero (Scope, Line list).
  LVOffsetLinesMap LinesZero;

  // Record scopes contribution in bytes to the debug information.
  using LVSizesMap = std::map<const LVScope *, LVOffset>;
  LVSizesMap Sizes;
  LVOffset CUContributionSize = 0;

  // Helper function to add an invalid location/range.
  void addInvalidLocationOrRange(LVLocation *Location, LVElement *Element,
                                 LVOffsetLocationsMap *Map) {
    LVOffset Offset = Element->getOffset();
    addInvalidOffset(Offset, Element);
    addItem<LVOffsetLocationsMap, LVOffset, LVLocation *>(Map, Offset,
                                                          Location);
  }

  // Record scope sizes indexed by lexical level.
  // Setting an initial size that will cover a very deep nested scopes.
  static constexpr size_t TotalInitialSize = 8;
  using LVTotalsEntry = std::pair<unsigned, float>;
  SmallVector<LVTotalsEntry> Totals;
  // Maximum seen lexical level. It is used to control how many entries
  // in the 'Totals' vector are valid values.
  LVLevel MaxSeenLevel = 0;

  // Get the line located at the given address.
  LVLine *lineLowerBound(LVAddress Address, LVScope *Scope) const;
  LVLine *lineUpperBound(LVAddress Address, LVScope *Scope) const;

  void printScopeSize(const LVScope *Scope, raw_ostream &OS);
  void printScopeSize(const LVScope *Scope, raw_ostream &OS) const {
    (const_cast<LVScopeCompileUnit *>(this))->printScopeSize(Scope, OS);
  }
  void printTotals(raw_ostream &OS) const;

protected:
  /// Print size contribution information for this compile unit.
  /// \param OS Stream that receives the printed sizes.
  void printSizes(raw_ostream &OS) const override;
  /// Print a summary of element counts for this compile unit.
  /// \param OS Stream that receives the printed summary.
  void printSummary(raw_ostream &OS) const override;

public:
  /// Construct a compile-unit scope and mark it as such.
  LVScopeCompileUnit() : LVScope(), Totals(TotalInitialSize, {0, 0.0}) {
    setIsCompileUnit();
  }
  /// Copy construction is not allowed.
  /// \param Other Unused source compile-unit scope.
  LVScopeCompileUnit(const LVScopeCompileUnit &Other) = delete;
  /// Copy assignment is not allowed.
  /// \param Other Unused source compile-unit scope.
  LVScopeCompileUnit &operator=(const LVScopeCompileUnit &Other) = delete;
  /// Destroy the compile-unit scope.
  ~LVScopeCompileUnit() override = default;

  /// Return this compile unit as its own compile-unit parent.
  /// \returns This scope cast to LVScope.
  LVScope *getCompileUnitParent() const override {
    return static_cast<LVScope *>(const_cast<LVScopeCompileUnit *>(this));
  }

  /// Add \p Line to the address-to-line mapping for \p SectionIndex.
  /// \param Line Line to record at its address.
  /// \param SectionIndex Section that owns the line's address.
  void addMapping(LVLine *Line, LVSectionIndex SectionIndex);
  /// Return the line range covering \p Location.
  /// \param Location Location whose address range selects lines.
  /// \returns Pair of lower and upper bound lines for the location.
  LVLineRange lineRange(LVLocation *Location) const;

  /// Sentinel name info used when a public name is not found.
  static constexpr LVNameInfo NameNone = {UINT64_MAX, 0};
  /// Record \p Scope as a public name spanning [\p LowPC, \p HighPC).
  /// \param Scope Public scope to register.
  /// \param LowPC Inclusive lower address of the public name.
  /// \param HighPC Exclusive upper address of the public name.
  void addPublicName(LVScope *Scope, LVAddress LowPC, LVAddress HighPC) {
    PublicNames.emplace(std::piecewise_construct, std::forward_as_tuple(Scope),
                        std::forward_as_tuple(LowPC, HighPC - LowPC));
  }
  /// Look up the public name info for \p Scope.
  /// \param Scope Scope whose public name is requested.
  /// \returns Matching name info, or NameNone if absent.
  const LVNameInfo &findPublicName(LVScope *Scope) {
    LVPublicNames::iterator Iter = PublicNames.find(Scope);
    return (Iter != PublicNames.end()) ? Iter->second : NameNone;
  }
  /// Return the map of public names for this compile unit.
  /// \returns Const reference to the public-names map.
  const LVPublicNames &getPublicNames() const { return PublicNames; }

  /// Return the base address of this compile unit.
  ///
  /// The base address of the scope for any of the debugging information
  /// entries listed is given by either the DW_AT_low_pc attribute or the
  /// first address in the first range entry in the list of ranges given by
  /// the DW_AT_ranges attribute.
  /// \returns Base address, or zero if no ranges are present.
  LVAddress getBaseAddress() const {
    return Ranges ? Ranges->front()->getLowerAddress() : 0;
  }

  /// Return the compilation directory path.
  /// \returns Compilation directory string from the string pool.
  StringRef getCompilationDirectory() const {
    return getStringPool().getString(CompilationDirectoryIndex);
  }
  /// Set the compilation directory path.
  /// \param CompilationDirectory Directory path to store.
  void setCompilationDirectory(StringRef CompilationDirectory) {
    CompilationDirectoryIndex = getStringPool().getIndex(CompilationDirectory);
  }

  /// Return the filename at \p Index in this compile unit's file table.
  /// \param Index Zero-based index into the file name table.
  /// \returns Filename string, or empty if out of range.
  StringRef getFilename(size_t Index) const;
  /// Append \p Name to this compile unit's file name table.
  /// \param Name Source file name to record.
  void addFilename(StringRef Name) {
    Filenames.push_back(getStringPool().getIndex(Name));
  }

  /// Return the toolchain producer string.
  /// \returns Producer string from the string pool.
  StringRef getProducer() const override {
    return getStringPool().getString(ProducerIndex);
  }
  /// Set the toolchain producer string.
  /// \param ProducerName Producer identification string to store.
  void setProducer(StringRef ProducerName) override {
    ProducerIndex = getStringPool().getIndex(ProducerName);
  }

  /// Return the source language of this compile unit.
  /// \returns Source language descriptor.
  LVSourceLanguage getSourceLanguage() const override { return SourceLanguage; }
  /// Set the source language of this compile unit.
  /// \param SL Source language descriptor to store.
  void setSourceLanguage(LVSourceLanguage SL) override { SourceLanguage = SL; }

  /// Set the CodeView CPU type for this compile unit.
  /// \param Type CPU type value from CodeView.
  void setCPUType(codeview::CPUType Type) { CompilationCPUType = Type; }
  /// Return the CodeView CPU type for this compile unit.
  /// \returns Stored CPU type value.
  codeview::CPUType getCPUType() { return CompilationCPUType; }

  /// Record a DWARF tag seen at \p Offset.
  /// \param Target DWARF tag to record.
  /// \param Offset DIE offset where the tag appears.
  void addDebugTag(dwarf::Tag Target, LVOffset Offset);
  /// Record an element with an invalid offset.
  /// \param Offset Invalid DIE offset.
  /// \param Element Element associated with the offset.
  void addInvalidOffset(LVOffset Offset, LVElement *Element);
  /// Record a symbol with an invalid coverage value.
  /// \param Symbol Symbol whose coverage is invalid.
  void addInvalidCoverage(LVSymbol *Symbol);
  /// Record a symbol location that failed validation.
  /// \param Location Invalid location to record.
  void addInvalidLocation(LVLocation *Location);
  /// Record a scope range that failed validation.
  /// \param Location Invalid range location to record.
  void addInvalidRange(LVLocation *Location);
  /// Record a line with line number zero.
  /// \param Line Line that has a zero line number.
  void addLineZero(LVLine *Line);

  /// Return the map of recorded DWARF tags.
  /// \returns Const reference to the tag-to-offsets map.
  const LVTagOffsetsMap &getDebugTags() const { return DebugTags; }
  /// Return offsets associated with warning-flagged elements.
  /// \returns Const reference to the warning-offsets map.
  const LVOffsetElementMap &getWarningOffsets() const { return WarningOffsets; }
  /// Return symbols with invalid locations.
  /// \returns Const reference to the invalid-locations map.
  const LVOffsetLocationsMap &getInvalidLocations() const {
    return InvalidLocations;
  }
  /// Return symbols with invalid coverage values.
  /// \returns Const reference to the invalid-coverages map.
  const LVOffsetSymbolMap &getInvalidCoverages() const {
    return InvalidCoverages;
  }
  /// Return scopes with invalid ranges.
  /// \returns Const reference to the invalid-ranges map.
  const LVOffsetLocationsMap &getInvalidRanges() const { return InvalidRanges; }
  /// Return scopes that contain line-zero records.
  /// \returns Const reference to the lines-zero map.
  const LVOffsetLinesMap &getLinesZero() const { return LinesZero; }

  /// Process ranges and locations and calculate coverage.
  /// \param ValidLocation Predicate used to validate ranges and locations.
  void processRangeLocationCoverage(
      LVValidLocation ValidLocation = &LVLocation::validateRanges);

  /// Record \p Element as matching a selection pattern.
  /// \param Element Matched element to store.
  void addMatched(LVElement *Element) { MatchedElements.push_back(Element); }
  /// Record \p Scope as matching a selection pattern.
  /// \param Scope Matched scope to store.
  void addMatched(LVScope *Scope) { MatchedScopes.push_back(Scope); }
  /// Propagate pattern-match flags through the scopes tree.
  void propagatePatternMatch();

  /// Return elements that matched a selection pattern.
  /// \returns Const reference to the matched-elements list.
  const LVElements &getMatchedElements() const { return MatchedElements; }
  /// Return scopes that matched a selection pattern.
  /// \returns Const reference to the matched-scopes list.
  const LVScopes &getMatchedScopes() const { return MatchedScopes; }

  /// Print local names associated with this compile unit.
  /// \param OS Stream that receives the printed names.
  /// \param Full Whether to include full detail.
  void printLocalNames(raw_ostream &OS, bool Full = true) const;
  /// Print a labeled summary using \p Counter.
  /// \param OS Stream that receives the printed summary.
  /// \param Counter Counters to print.
  /// \param Header Header label for the summary block.
  void printSummary(raw_ostream &OS, const LVCounter &Counter,
                    const char *Header) const;

  /// Increment the count of printed lines.
  void incrementPrintedLines();
  /// Increment the count of printed scopes.
  void incrementPrintedScopes();
  /// Increment the count of printed symbols.
  void incrementPrintedSymbols();
  /// Increment the count of printed types.
  void incrementPrintedTypes();

  /// Increment allocated counters for \p Line (used by `--summary`).
  /// \param Line Line that was allocated.
  void increment(LVLine *Line);
  /// Increment allocated counters for \p Scope (used by `--summary`).
  /// \param Scope Scope that was allocated.
  void increment(LVScope *Scope);
  /// Increment allocated counters for \p Symbol (used by `--summary`).
  /// \param Symbol Symbol that was allocated.
  void increment(LVSymbol *Symbol);
  /// Increment allocated counters for \p Type (used by `--summary`).
  /// \param Type Type that was allocated.
  void increment(LVType *Type);

  /// Notify that \p Line was added to the scopes tree.
  ///
  /// Increases the added-element counters for the printed summary. During
  /// comparison, notifies the Reader of the new element.
  /// \param Line Line that was added.
  void addedElement(LVLine *Line);
  /// Notify that \p Scope was added to the scopes tree.
  /// \param Scope Scope that was added.
  void addedElement(LVScope *Scope);
  /// Notify that \p Symbol was added to the scopes tree.
  /// \param Symbol Symbol that was added.
  void addedElement(LVSymbol *Symbol);
  /// Notify that \p Type was added to the scopes tree.
  /// \param Type Type that was added.
  void addedElement(LVType *Type);

  /// Record the debug-info size contribution of \p Scope.
  /// \param Scope Scope whose contribution is recorded.
  /// \param Lower Inclusive lower DIE offset of the contribution.
  /// \param Upper Exclusive upper DIE offset of the contribution.
  void addSize(LVScope *Scope, LVOffset Lower, LVOffset Upper);

  /// Return true if this scope is logically equal to \p Scope.
  /// \param Scope Scope to compare against.
  /// \returns True when the scopes are logically equal.
  bool equals(const LVScope *Scope) const override;

  /// Print this compile unit to \p OS.
  /// \param OS Stream that receives the printed compile unit.
  /// \param Full Whether to include full detail.
  void print(raw_ostream &OS, bool Full = true) const override;
  /// Print compile-unit-specific extra details to \p OS.
  /// \param OS Stream that receives the printed details.
  /// \param Full Whether to include full detail.
  void printExtra(raw_ostream &OS, bool Full = true) const override;
  /// Print warnings collected for this compile unit.
  /// \param OS Stream that receives the printed warnings.
  /// \param Full Whether to include full detail.
  void printWarnings(raw_ostream &OS, bool Full = true) const override;
  /// Print elements that matched a selection pattern.
  /// \param OS Stream that receives the printed matches.
  /// \param UseMatchedElements Whether to print matched elements rather than scopes.
  void printMatchedElements(raw_ostream &OS, bool UseMatchedElements) override;
};

/// Logical scope representing a DWARF enumeration (DW_TAG_enumeration_type).
class LLVM_ABI LVScopeEnumeration final : public LVScope {
public:
  /// Construct an enumeration scope and mark it as such.
  LVScopeEnumeration() : LVScope() { setIsEnumeration(); }
  /// Copy construction is not allowed.
  /// \param Other Unused source enumeration scope.
  LVScopeEnumeration(const LVScopeEnumeration &Other) = delete;
  /// Copy assignment is not allowed.
  /// \param Other Unused source enumeration scope.
  LVScopeEnumeration &operator=(const LVScopeEnumeration &Other) = delete;
  /// Destroy the enumeration scope.
  ~LVScopeEnumeration() override = default;

  /// Return true if this scope is logically equal to \p Scope.
  /// \param Scope Scope to compare against.
  /// \returns True when the scopes are logically equal.
  bool equals(const LVScope *Scope) const override;

  /// Print enumeration-specific extra details to \p OS.
  /// \param OS Stream that receives the printed details.
  /// \param Full Whether to include full detail.
  void printExtra(raw_ostream &OS, bool Full = true) const override;
};

/// Logical scope representing a DWARF formal parameter pack
/// (DW_TAG_GNU_formal_parameter_pack).
class LLVM_ABI LVScopeFormalPack final : public LVScope {
public:
  /// Construct a formal-parameter-pack scope and mark it as a template pack.
  LVScopeFormalPack() : LVScope() { setIsTemplatePack(); }
  /// Copy construction is not allowed.
  /// \param Other Unused source formal-parameter-pack scope.
  LVScopeFormalPack(const LVScopeFormalPack &Other) = delete;
  /// Copy assignment is not allowed.
  /// \param Other Unused source formal-parameter-pack scope.
  LVScopeFormalPack &operator=(const LVScopeFormalPack &Other) = delete;
  /// Destroy the formal-parameter-pack scope.
  ~LVScopeFormalPack() override = default;

  /// Return true if this scope is logically equal to \p Scope.
  /// \param Scope Scope to compare against.
  /// \returns True when the scopes are logically equal.
  bool equals(const LVScope *Scope) const override;

  /// Print formal-pack-specific extra details to \p OS.
  /// \param OS Stream that receives the printed details.
  /// \param Full Whether to include full detail.
  void printExtra(raw_ostream &OS, bool Full = true) const override;
};

/// Logical scope representing a DWARF function or subprogram.
class LLVM_ABI LVScopeFunction : public LVScope {
  LVScope *Reference = nullptr; // DW_AT_specification, DW_AT_abstract_origin.
  size_t LinkageNameIndex = 0;  // Function DW_AT_linkage_name attribute.
  size_t EncodedArgsIndex = 0;  // Template encoded arguments.

public:
  /// Construct a function scope.
  LVScopeFunction() : LVScope() {}
  /// Copy construction is not allowed.
  /// \param Other Unused source function scope.
  LVScopeFunction(const LVScopeFunction &Other) = delete;
  /// Copy assignment is not allowed.
  /// \param Other Unused source function scope.
  LVScopeFunction &operator=(const LVScopeFunction &Other) = delete;
  /// Destroy the function scope.
  ~LVScopeFunction() override = default;

  /// Return the DW_AT_specification or DW_AT_abstract_origin reference.
  /// \returns Referenced scope, or nullptr if none.
  LVScope *getReference() const override { return Reference; }
  /// Set the DW_AT_specification or DW_AT_abstract_origin reference.
  /// \param Scope Referenced function or related scope.
  void setReference(LVScope *Scope) override {
    Reference = Scope;
    setHasReference();
  }
  /// Set the reference from a generic element.
  /// \param Element Element cast to an LVScope reference.
  void setReference(LVElement *Element) override {
    setReference(static_cast<LVScope *>(Element));
  }

  /// Return the encoded template arguments string.
  /// \returns Encoded template arguments from the string pool.
  StringRef getEncodedArgs() const override {
    return getStringPool().getString(EncodedArgsIndex);
  }
  /// Store the encoded template arguments string.
  /// \param EncodedArgs Encoded template arguments to store.
  void setEncodedArgs(StringRef EncodedArgs) override {
    EncodedArgsIndex = getStringPool().getIndex(EncodedArgs);
  }

  /// Set the DW_AT_linkage_name for this function.
  /// \param LinkageName Linkage name to store in the string pool.
  void setLinkageName(StringRef LinkageName) override {
    LinkageNameIndex = getStringPool().getIndex(LinkageName);
  }
  /// Return the DW_AT_linkage_name for this function.
  /// \returns Linkage name from the string pool.
  StringRef getLinkageName() const override {
    return getStringPool().getString(LinkageNameIndex);
  }
  /// Return the string-pool index of the linkage name.
  /// \returns Index of the linkage name in the string pool.
  size_t getLinkageNameIndex() const override { return LinkageNameIndex; }

  /// Set the display name of this function.
  /// \param ObjectName Name to associate with this function.
  void setName(StringRef ObjectName) override;

  /// Resolve function-specific attributes and children.
  void resolveExtra() override;
  /// Resolve references from this function to other elements.
  void resolveReferences() override;

  /// Return true if this scope is logically equal to \p Scope.
  /// \param Scope Scope to compare against.
  /// \returns True when the scopes are logically equal.
  bool equals(const LVScope *Scope) const override;

  /// Find a scope in \p Scopes that is logically equal to this scope.
  /// \param Scopes Candidate scopes to search.
  /// \returns Matching scope, or nullptr if none.
  LVScope *findEqualScope(const LVScopes *Scopes) const override;

  /// Print function-specific extra details to \p OS.
  /// \param OS Stream that receives the printed details.
  /// \param Full Whether to include full detail.
  void printExtra(raw_ostream &OS, bool Full = true) const override;
};

/// Logical scope representing a DWARF inlined function.
class LLVM_ABI LVScopeFunctionInlined final : public LVScopeFunction {
  size_t CallFilenameIndex = 0;
  uint32_t CallLineNumber = 0;
  uint32_t Discriminator = 0;

public:
  /// Construct an inlined-function scope and mark it as such.
  LVScopeFunctionInlined() : LVScopeFunction() { setIsInlinedFunction(); }
  /// Copy construction is not allowed.
  /// \param Other Unused source inlined-function scope.
  LVScopeFunctionInlined(const LVScopeFunctionInlined &Other) = delete;
  /// Copy assignment is not allowed.
  /// \param Other Unused source inlined-function scope.
  LVScopeFunctionInlined &operator=(const LVScopeFunctionInlined &Other) = delete;
  /// Destroy the inlined-function scope.
  ~LVScopeFunctionInlined() override = default;

  /// Return the DWARF discriminator for this inlined call.
  /// \returns Discriminator value, or zero if none.
  uint32_t getDiscriminator() const override { return Discriminator; }
  /// Set the DWARF discriminator for this inlined call.
  /// \param Value Discriminator to store.
  void setDiscriminator(uint32_t Value) override {
    Discriminator = Value;
    setHasDiscriminator();
  }

  /// Return the DW_AT_call_line for this inlined function.
  /// \returns Call-site line number.
  uint32_t getCallLineNumber() const override { return CallLineNumber; }
  /// Set the DW_AT_call_line for this inlined function.
  /// \param Number Call-site line number to store.
  void setCallLineNumber(uint32_t Number) override { CallLineNumber = Number; }
  /// Return the string-pool index of the call-site filename.
  /// \returns Index of the call filename in the string pool.
  size_t getCallFilenameIndex() const override { return CallFilenameIndex; }
  /// Set the string-pool index of the call-site filename.
  /// \param Index Filename index to store.
  void setCallFilenameIndex(size_t Index) override {
    CallFilenameIndex = Index;
  }

  /// Format this inlined function's call line for display.
  ///
  /// For inlined functions, uses the DW_AT_call_line attribute; otherwise
  /// uses the DW_AT_decl_line attribute.
  /// \param ShowZero Whether to show a zero line number instead of padding.
  /// \returns Formatted call-line string for display.
  std::string lineNumberAsString(bool ShowZero = false) const override {
    return lineAsString(getCallLineNumber(), getDiscriminator(), ShowZero);
  }

  /// Resolve inlined-function-specific attributes and children.
  void resolveExtra() override;

  /// Return true if this scope is logically equal to \p Scope.
  /// \param Scope Scope to compare against.
  /// \returns True when the scopes are logically equal.
  bool equals(const LVScope *Scope) const override;

  /// Find a scope in \p Scopes that is logically equal to this scope.
  /// \param Scopes Candidate scopes to search.
  /// \returns Matching scope, or nullptr if none.
  LVScope *findEqualScope(const LVScopes *Scopes) const override;

  /// Print inlined-function-specific extra details to \p OS.
  /// \param OS Stream that receives the printed details.
  /// \param Full Whether to include full detail.
  void printExtra(raw_ostream &OS, bool Full = true) const override;
};

/// Logical scope representing a DWARF subroutine type.
class LLVM_ABI LVScopeFunctionType final : public LVScopeFunction {
public:
  /// Construct a function-type scope and mark it as such.
  LVScopeFunctionType() : LVScopeFunction() { setIsFunctionType(); }
  /// Copy construction is not allowed.
  /// \param Other Unused source function-type scope.
  LVScopeFunctionType(const LVScopeFunctionType &Other) = delete;
  /// Copy assignment is not allowed.
  /// \param Other Unused source function-type scope.
  LVScopeFunctionType &operator=(const LVScopeFunctionType &Other) = delete;
  /// Destroy the function-type scope.
  ~LVScopeFunctionType() override = default;

  /// Resolve function-type-specific attributes and children.
  void resolveExtra() override;
};

/// Logical scope representing a DWARF module.
class LLVM_ABI LVScopeModule final : public LVScope {
public:
  /// Construct a module scope and mark it as a module and lexical block.
  LVScopeModule() : LVScope() {
    setIsModule();
    setIsLexicalBlock();
  }
  /// Copy construction is not allowed.
  /// \param Other Unused source module scope.
  LVScopeModule(const LVScopeModule &Other) = delete;
  /// Copy assignment is not allowed.
  /// \param Other Unused source module scope.
  LVScopeModule &operator=(const LVScopeModule &Other) = delete;
  /// Destroy the module scope.
  ~LVScopeModule() override = default;

  /// Return true if this scope is logically equal to \p Scope.
  /// \param Scope Scope to compare against.
  /// \returns True when the scopes are logically equal.
  bool equals(const LVScope *Scope) const override;

  /// Print module-specific extra details to \p OS.
  /// \param OS Stream that receives the printed details.
  /// \param Full Whether to include full detail.
  void printExtra(raw_ostream &OS, bool Full = true) const override;
};

/// Logical scope representing a DWARF namespace.
class LLVM_ABI LVScopeNamespace final : public LVScope {
  LVScope *Reference = nullptr; // Reference to DW_AT_extension attribute.

public:
  /// Construct a namespace scope and mark it as such.
  LVScopeNamespace() : LVScope() { setIsNamespace(); }
  /// Copy construction is not allowed.
  /// \param Other Unused source namespace scope.
  LVScopeNamespace(const LVScopeNamespace &Other) = delete;
  /// Copy assignment is not allowed.
  /// \param Other Unused source namespace scope.
  LVScopeNamespace &operator=(const LVScopeNamespace &Other) = delete;
  /// Destroy the namespace scope.
  ~LVScopeNamespace() override = default;

  /// Return the DW_AT_extension referenced scope.
  /// \returns Referenced scope, or nullptr if none.
  LVScope *getReference() const override { return Reference; }
  /// Set the DW_AT_extension referenced scope.
  /// \param Scope Namespace extension reference to store.
  void setReference(LVScope *Scope) override {
    Reference = Scope;
    setHasReference();
  }
  /// Set the extension reference from a generic element.
  /// \param Element Element cast to an LVScope reference.
  void setReference(LVElement *Element) override {
    setReference(static_cast<LVScope *>(Element));
  }

  /// Return true if this scope is logically equal to \p Scope.
  /// \param Scope Scope to compare against.
  /// \returns True when the scopes are logically equal.
  bool equals(const LVScope *Scope) const override;

  /// Find a scope in \p Scopes that is logically equal to this scope.
  /// \param Scopes Candidate scopes to search.
  /// \returns Matching scope, or nullptr if none.
  LVScope *findEqualScope(const LVScopes *Scopes) const override;

  /// Print namespace-specific extra details to \p OS.
  /// \param OS Stream that receives the printed details.
  /// \param Full Whether to include full detail.
  void printExtra(raw_ostream &OS, bool Full = true) const override;
};

/// Logical scope representing the binary file being analyzed.
class LLVM_ABI LVScopeRoot final : public LVScope {
  size_t FileFormatNameIndex = 0;

public:
  /// Construct a root scope and mark it as such.
  LVScopeRoot() : LVScope() { setIsRoot(); }
  /// Copy construction is not allowed.
  /// \param Other Unused source root scope.
  LVScopeRoot(const LVScopeRoot &Other) = delete;
  /// Copy assignment is not allowed.
  /// \param Other Unused source root scope.
  LVScopeRoot &operator=(const LVScopeRoot &Other) = delete;
  /// Destroy the root scope.
  ~LVScopeRoot() override = default;

  /// Return the object-file format name.
  /// \returns File format name from the string pool.
  StringRef getFileFormatName() const {
    return getStringPool().getString(FileFormatNameIndex);
  }
  /// Set the object-file format name.
  /// \param FileFormatName Format name to store in the string pool.
  void setFileFormatName(StringRef FileFormatName) {
    FileFormatNameIndex = getStringPool().getIndex(FileFormatName);
  }

  /// Recursively shorten scoped CodeView names to their innermost component.
  ///
  /// The CodeView Reader uses scoped names. Recursively transform the
  /// element name to use just the most inner component.
  void transformScopedName();

  /// Process collected locations and ranges and calculate coverage.
  void processRangeInformation();

  /// Return true if this scope is logically equal to \p Scope.
  /// \param Scope Scope to compare against.
  /// \returns True when the scopes are logically equal.
  bool equals(const LVScope *Scope) const override;

  /// Print this root scope to \p OS.
  /// \param OS Stream that receives the printed root.
  /// \param Full Whether to include full detail.
  void print(raw_ostream &OS, bool Full = true) const override;
  /// Print root-specific extra details to \p OS.
  /// \param OS Stream that receives the printed details.
  /// \param Full Whether to include full detail.
  void printExtra(raw_ostream &OS, bool Full = true) const override;
  /// Print elements that matched a selection pattern.
  /// \param Split Whether to split output across files.
  /// \param OS Stream that receives the printed matches.
  /// \param UseMatchedElements Whether to print matched elements rather than scopes.
  /// \returns Success or an error describing the print failure.
  Error doPrintMatches(bool Split, raw_ostream &OS,
                       bool UseMatchedElements) const;
};

/// Logical scope representing a DWARF template parameter pack
/// (DW_TAG_GNU_template_parameter_pack).
class LLVM_ABI LVScopeTemplatePack final : public LVScope {
public:
  /// Construct a template-parameter-pack scope and mark it as such.
  LVScopeTemplatePack() : LVScope() { setIsTemplatePack(); }
  /// Copy construction is not allowed.
  /// \param Other Unused source template-parameter-pack scope.
  LVScopeTemplatePack(const LVScopeTemplatePack &Other) = delete;
  /// Copy assignment is not allowed.
  /// \param Other Unused source template-parameter-pack scope.
  LVScopeTemplatePack &operator=(const LVScopeTemplatePack &Other) = delete;
  /// Destroy the template-parameter-pack scope.
  ~LVScopeTemplatePack() override = default;

  /// Return true if this scope is logically equal to \p Scope.
  /// \param Scope Scope to compare against.
  /// \returns True when the scopes are logically equal.
  bool equals(const LVScope *Scope) const override;

  /// Print template-pack-specific extra details to \p OS.
  /// \param OS Stream that receives the printed details.
  /// \param Full Whether to include full detail.
  void printExtra(raw_ostream &OS, bool Full = true) const override;
};

} // end namespace logicalview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVSCOPE_H
