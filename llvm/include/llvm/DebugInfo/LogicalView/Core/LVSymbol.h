//===-- LVSymbol.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the LVSymbol class, which is used to describe a debug
// information symbol.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVSYMBOL_H
#define LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVSYMBOL_H

#include "llvm/DebugInfo/LogicalView/Core/LVElement.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
namespace logicalview {

/// Kind flags that classify a logical-view symbol.
enum class LVSymbolKind {
  /// Symbol is a call-site parameter.
  IsCallSiteParameter,
  /// Symbol is a constant value.
  IsConstant,
  /// Symbol represents inheritance from a base class.
  IsInheritance,
  /// Symbol is a data member.
  IsMember,
  /// Symbol is a function or template parameter.
  IsParameter,
  /// Symbol has an unspecified type or role.
  IsUnspecified,
  /// Symbol is a variable.
  IsVariable,
  /// Sentinel past the last valid kind.
  LastEntry
};
/// Set of selected LVSymbolKind values.
using LVSymbolKindSet = std::set<LVSymbolKind>;
/// Map from LVSymbolKind to the corresponding getter member function.
using LVSymbolDispatch = std::map<LVSymbolKind, LVSymbolGetFunction>;
/// Ordered list of LVSymbol getter member functions used for requests.
using LVSymbolRequest = std::vector<LVSymbolGetFunction>;

/// Logical-view element that represents a debug-information symbol.
class LLVM_ABI LVSymbol final : public LVElement {
  enum class Property { HasLocation, FillGaps, LastEntry };

  // Typed bitvector with kinds and properties for this symbol.
  LVProperties<LVSymbolKind> Kinds;
  LVProperties<Property> Properties;
  static LVSymbolDispatch Dispatch;

  // CodeView symbol Linkage name.
  size_t LinkageNameIndex = 0;

  // Reference to DW_AT_specification, DW_AT_abstract_origin attribute.
  LVSymbol *Reference = nullptr;
  std::unique_ptr<LVLocations> Locations;
  LVLocation *CurrentLocation = nullptr;

  // Bitfields length.
  uint32_t BitSize = 0;

  // Index in the String pool representing any initial value.
  size_t ValueIndex = 0;

  // Coverage factor in units (bytes).
  unsigned CoverageFactor = 0;
  float CoveragePercentage = 0;

  // Add a location gap into the location list.
  LVLocations::iterator addLocationGap(LVLocations::iterator Pos,
                                       LVAddress LowPC, LVAddress HighPC);

  // Find the current symbol in the given 'Targets'.
  LVSymbol *findIn(const LVSymbols *Targets) const;

public:
  /// Construct a logical symbol element and mark it for printing.
  LVSymbol() : LVElement(LVSubclassID::LV_SYMBOL) {
    setIsSymbol();
    setIncludeInPrint();
  }
  /// Copy construction is not allowed.
  /// \param Other Unused source logical symbol.
  LVSymbol(const LVSymbol &Other) = delete;
  /// Copy assignment is not allowed.
  /// \param Other Unused source logical symbol.
  LVSymbol &operator=(const LVSymbol &Other) = delete;
  /// Destroy the logical symbol.
  ~LVSymbol() override = default;

  /// Return true when \p Element is an LVSymbol.
  /// \param Element Element to test for the LVSymbol subclass.
  /// \returns True if \p Element has subclass ID LV_SYMBOL.
  static bool classof(const LVElement *Element) {
    return Element->getSubclassID() == LVSubclassID::LV_SYMBOL;
  }

  /// Return whether this symbol is a call-site parameter.
  /// \returns True when the call-site-parameter kind is set.
  bool getIsCallSiteParameter() const {
    return Kinds.get(LVSymbolKind::IsCallSiteParameter);
  }
  /// Mark this symbol as a call-site parameter.
  void setIsCallSiteParameter() {
    Kinds.set(LVSymbolKind::IsCallSiteParameter);
  }
  /// Clear the call-site-parameter kind on this symbol.
  void resetIsCallSiteParameter() {
    Kinds.reset(LVSymbolKind::IsCallSiteParameter);
  }
  /// Return whether this symbol is a constant.
  /// \returns True when the constant kind is set.
  bool getIsConstant() const { return Kinds.get(LVSymbolKind::IsConstant); }
  /// Mark this symbol as a constant.
  void setIsConstant() { Kinds.set(LVSymbolKind::IsConstant); }
  /// Clear the constant kind on this symbol.
  void resetIsConstant() { Kinds.reset(LVSymbolKind::IsConstant); }
  /// Return whether this symbol represents inheritance.
  /// \returns True when the inheritance kind is set.
  bool getIsInheritance() const {
    return Kinds.get(LVSymbolKind::IsInheritance);
  }
  /// Mark this symbol as representing inheritance.
  void setIsInheritance() { Kinds.set(LVSymbolKind::IsInheritance); }
  /// Clear the inheritance kind on this symbol.
  void resetIsInheritance() { Kinds.reset(LVSymbolKind::IsInheritance); }
  /// Return whether this symbol is a data member.
  /// \returns True when the member kind is set.
  bool getIsMember() const { return Kinds.get(LVSymbolKind::IsMember); }
  /// Mark this symbol as a data member.
  void setIsMember() { Kinds.set(LVSymbolKind::IsMember); }
  /// Clear the member kind on this symbol.
  void resetIsMember() { Kinds.reset(LVSymbolKind::IsMember); }
  /// Return whether this symbol is a parameter.
  /// \returns True when the parameter kind is set.
  bool getIsParameter() const { return Kinds.get(LVSymbolKind::IsParameter); }
  /// Mark this symbol as a parameter.
  void setIsParameter() { Kinds.set(LVSymbolKind::IsParameter); }
  /// Clear the parameter kind on this symbol.
  void resetIsParameter() { Kinds.reset(LVSymbolKind::IsParameter); }
  /// Return whether this symbol is unspecified.
  /// \returns True when the unspecified kind is set.
  bool getIsUnspecified() const {
    return Kinds.get(LVSymbolKind::IsUnspecified);
  }
  /// Mark this symbol as unspecified.
  void setIsUnspecified() { Kinds.set(LVSymbolKind::IsUnspecified); }
  /// Clear the unspecified kind on this symbol.
  void resetIsUnspecified() { Kinds.reset(LVSymbolKind::IsUnspecified); }
  /// Return whether this symbol is a variable.
  /// \returns True when the variable kind is set.
  bool getIsVariable() const { return Kinds.get(LVSymbolKind::IsVariable); }
  /// Mark this symbol as a variable.
  void setIsVariable() { Kinds.set(LVSymbolKind::IsVariable); }
  /// Clear the variable kind on this symbol.
  void resetIsVariable() { Kinds.reset(LVSymbolKind::IsVariable); }

  /// Return whether this symbol has location information.
  /// \returns True when the has-location property is set.
  bool getHasLocation() const { return Properties.get(Property::HasLocation); }
  /// Mark this symbol as having location information.
  void setHasLocation() { Properties.set(Property::HasLocation); }
  /// Clear the has-location property on this symbol.
  void resetHasLocation() { Properties.reset(Property::HasLocation); }
  /// Return whether location gaps should be filled for this symbol.
  /// \returns True when the fill-gaps property is set.
  bool getFillGaps() const { return Properties.get(Property::FillGaps); }
  /// Mark this symbol so that location gaps are filled.
  void setFillGaps() { Properties.set(Property::FillGaps); }
  /// Clear the fill-gaps property on this symbol.
  void resetFillGaps() { Properties.reset(Property::FillGaps); }

  /// Return a string naming the kind of this symbol.
  /// \returns C string describing the symbol kind.
  const char *kind() const override;

  /// Return the DW_AT_specification or DW_AT_abstract_origin reference.
  /// \returns Referenced symbol, or nullptr if none is set.
  LVSymbol *getReference() const { return Reference; }
  /// Set the DW_AT_specification or DW_AT_abstract_origin reference.
  /// \param Symbol Symbol referenced by this symbol's specification attributes.
  void setReference(LVSymbol *Symbol) override {
    Reference = Symbol;
    setHasReference();
  }
  /// Set the reference from a generic element that must be an LVSymbol.
  /// \param Element Element to store as the reference; must be null or an LVSymbol.
  void setReference(LVElement *Element) override {
    assert((!Element || isa<LVSymbol>(Element)) && "Invalid element");
    setReference(static_cast<LVSymbol *>(Element));
  }

  /// Store \p LinkageName in the string pool as this symbol's linkage name.
  /// \param LinkageName Linkage name to associate with this symbol.
  void setLinkageName(StringRef LinkageName) override {
    LinkageNameIndex = getStringPool().getIndex(LinkageName);
  }
  /// Return the linkage name stored for this symbol.
  /// \returns Linkage name from the string pool.
  StringRef getLinkageName() const override {
    return getStringPool().getString(LinkageNameIndex);
  }
  /// Return the string-pool index of this symbol's linkage name.
  /// \returns Index of the linkage name in the string pool.
  size_t getLinkageNameIndex() const override { return LinkageNameIndex; }

  /// Return the bit size associated with this symbol.
  /// \returns Bit size previously stored for this symbol.
  uint32_t getBitSize() const override { return BitSize; }
  /// Store the bit size associated with this symbol.
  /// \param Size Bit size to record.
  void setBitSize(uint32_t Size) override { BitSize = Size; }

  /// Return the constant value associated with this symbol.
  ///
  /// Processes the values for a DW_AT_const_value.
  /// \returns Constant value from the string pool.
  StringRef getValue() const override {
    return getStringPool().getString(ValueIndex);
  }
  /// Store \p Value as this symbol's constant value in the string pool.
  ///
  /// Processes the values for a DW_AT_const_value.
  /// \param Value Constant value text to store.
  void setValue(StringRef Value) override {
    ValueIndex = getStringPool().getIndex(Value);
  }
  /// Return the string-pool index of this symbol's constant value.
  /// \returns Index of the constant value in the string pool.
  size_t getValueIndex() const override { return ValueIndex; }

  /// Add a location entry that holds a constant operand.
  /// \param Attr DWARF attribute identifying the location.
  /// \param Constant Constant value recorded in the location.
  /// \param LocDescOffset Offset of the location description.
  void addLocationConstant(dwarf::Attribute Attr, LVUnsigned Constant,
                           uint64_t LocDescOffset);
  /// Append location operands to the current location entry.
  /// \param Opcode Location expression opcode.
  /// \param Operands Operand values associated with \p Opcode.
  void addLocationOperands(LVSmall Opcode, ArrayRef<uint64_t> Operands);
  /// Add a location entry covering [\p LowPC, \p HighPC].
  /// \param Attr DWARF attribute identifying the location.
  /// \param LowPC Inclusive lower address of the location range.
  /// \param HighPC Upper address of the location range.
  /// \param SectionOffset Section offset of the location list entry.
  /// \param LocDescOffset Offset of the location description.
  /// \param CallSiteLocation Whether the location describes a call site.
  void addLocation(dwarf::Attribute Attr, LVAddress LowPC, LVAddress HighPC,
                   LVUnsigned SectionOffset, uint64_t LocDescOffset,
                   bool CallSiteLocation = false);

  /// Insert gap entries for missing ranges in the location list.
  void fillLocationGaps();

  /// Collect locations that fail \p ValidLocation into \p LocationList.
  /// \param LocationList Destination list that receives matching locations.
  /// \param ValidLocation Member function used to validate each location.
  /// \param RecordInvalid Whether to record locations that fail validation.
  void getLocations(LVLocations &LocationList, LVValidLocation ValidLocation,
                    bool RecordInvalid = false);
  /// Append every location associated with this symbol to \p LocationList.
  /// \param LocationList Destination list that receives all locations.
  void getLocations(LVLocations &LocationList) const;

  /// Calculate the coverage factor and percentage for this symbol's locations.
  void calculateCoverage();

  /// Return the coverage factor in bytes.
  /// \returns Coverage factor previously stored for this symbol.
  unsigned getCoverageFactor() const { return CoverageFactor; }
  /// Store the coverage factor in bytes.
  /// \param Value Coverage factor to record.
  void setCoverageFactor(unsigned Value) { CoverageFactor = Value; }
  /// Return the coverage percentage for this symbol.
  /// \returns Coverage percentage previously stored for this symbol.
  float getCoveragePercentage() const { return CoveragePercentage; }
  /// Store the coverage percentage for this symbol.
  /// \param Value Coverage percentage to record.
  void setCoveragePercentage(float Value) { CoveragePercentage = Value; }

  /// Print this symbol's locations to \p OS in raw format.
  /// \param OS Stream that receives the printed locations.
  /// \param Full Whether to include full location detail.
  void printLocations(raw_ostream &OS, bool Full = true) const;

  /// Follow abstract-origin and specification references to resolve the name.
  ///
  /// Follows a chain of references given by DW_AT_abstract_origin and/or
  /// DW_AT_specification and updates the symbol name.
  /// \returns Resolved name for this symbol.
  StringRef resolveReferencesChain();

  /// Resolve this symbol's name and any matching selection patterns.
  void resolveName() override;
  /// Resolve type and specification references for this symbol.
  void resolveReferences() override;

  /// Return the shared dispatch map from symbol kinds to getters.
  /// \returns Reference to the static LVSymbolDispatch table.
  static LVSymbolDispatch &getDispatch() { return Dispatch; }

  /// Return true if parameter lists in \p References and \p Targets match.
  /// \param References Reference symbol set providing parameters.
  /// \param Targets Target symbol set providing parameters.
  /// \returns True when the parameter sequences match.
  static bool parametersMatch(const LVSymbols *References,
                              const LVSymbols *Targets);

  /// Collect parameter symbols from \p Symbols into \p Parameters.
  /// \param Symbols Symbol set to scan for parameters.
  /// \param Parameters Destination list that receives parameter symbols.
  static void getParameters(const LVSymbols *Symbols, LVSymbols *Parameters);

  /// Mark parents of reference symbols that are missing from the targets.
  ///
  /// Iterate through the \p References set and check that all its elements
  /// are present in the \p Targets set. For a missing element, mark its
  /// parents as missing.
  /// \param References Symbols expected to appear in the target set.
  /// \param Targets Symbols available for matching.
  static void markMissingParents(const LVSymbols *References,
                                 const LVSymbols *Targets);

  /// Return true if this symbol is logically equal to \p Symbol.
  /// \param Symbol Symbol to compare against.
  /// \returns True when the symbols are logically equal.
  bool equals(const LVSymbol *Symbol) const;

  /// Return true if \p References are logically equal to \p Targets.
  /// \param References Reference symbol set.
  /// \param Targets Target symbol set.
  /// \returns True when both sets are logically equal.
  static bool equals(const LVSymbols *References, const LVSymbols *Targets);

  /// Report this symbol as missing or added during comparison.
  /// \param Pass Comparison pass that classifies the symbol.
  void report(LVComparePass Pass) override;

  /// Print this symbol to \p OS.
  /// \param OS Stream that receives the printed symbol.
  /// \param Full Whether to include full detail.
  void print(raw_ostream &OS, bool Full = true) const override;
  /// Print kind-specific extra information for this symbol.
  /// \param OS Stream that receives the printed extras.
  /// \param Full Whether to include full extra detail.
  void printExtra(raw_ostream &OS, bool Full = true) const override;
};

} // end namespace logicalview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVSYMBOL_H
