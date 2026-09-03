//===-- LVObject.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the LVObject class, which is used to describe a debug
// information object.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVOBJECT_H
#define LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVOBJECT_H

#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/DebugInfo/LogicalView/Core/LVSupport.h"
#include "llvm/Support/Compiler.h"
#include <limits>
#include <string>

namespace llvm {
namespace dwarf {
/// Synthetic DWARF tag used for CodeView ModifierOptions::Unaligned.
constexpr Tag DW_TAG_unaligned = Tag(dwarf::DW_TAG_hi_user + 1);
} // namespace dwarf
} // namespace llvm

namespace llvm {
namespace logicalview {

/// Section index within an object file.
using LVSectionIndex = uint64_t;
/// Virtual or absolute address value.
using LVAddress = uint64_t;
/// Half-word integer used for discriminators and similar fields.
using LVHalf = uint16_t;
/// Nesting level of a scope in the logical view.
using LVLevel = uint16_t;
/// Byte offset of a DIE or similar debug record.
using LVOffset = uint64_t;
/// Signed 64-bit integer value from debug information.
using LVSigned = int64_t;
/// Unsigned 64-bit integer value from debug information.
using LVUnsigned = uint64_t;
/// Small unsigned integer used for opcodes and compact fields.
using LVSmall = uint8_t;

class LVElement;
class LVLine;
class LVLocation;
class LVLocationSymbol;
class LVObject;
class LVOperation;
class LVScope;
class LVSymbol;
class LVType;

class LVOptions;
class LVPatterns;

/// Return the placeholder string used when no type is available.
/// \returns Placeholder string used when no type is available.
LLVM_ABI StringRef typeNone();
/// Return the string spelling for the void type.
/// \returns String spelling for the void type.
LLVM_ABI StringRef typeVoid();
/// Return the string spelling for the int type.
/// \returns String spelling for the int type.
LLVM_ABI StringRef typeInt();
/// Return the placeholder string used for an unknown type.
/// \returns Placeholder string used for an unknown type.
LLVM_ABI StringRef typeUnknown();
/// Return an empty string reference.
/// \returns Empty string reference.
LLVM_ABI StringRef emptyString();

/// Pointer-to-member used to set a boolean property on an LVElement.
using LVElementSetFunction = void (LVElement::*)();
/// Pointer-to-member used to query a boolean property on an LVElement.
using LVElementGetFunction = bool (LVElement::*)() const;
/// Pointer-to-member used to set a boolean property on an LVLine.
using LVLineSetFunction = void (LVLine::*)();
/// Pointer-to-member used to query a boolean property on an LVLine.
using LVLineGetFunction = bool (LVLine::*)() const;
/// Pointer-to-member used to set a boolean property on an LVObject.
using LVObjectSetFunction = void (LVObject::*)();
/// Pointer-to-member used to query a boolean property on an LVObject.
using LVObjectGetFunction = bool (LVObject::*)() const;
/// Pointer-to-member used to set a boolean property on an LVScope.
using LVScopeSetFunction = void (LVScope::*)();
/// Pointer-to-member used to query a boolean property on an LVScope.
using LVScopeGetFunction = bool (LVScope::*)() const;
/// Pointer-to-member used to set a boolean property on an LVSymbol.
using LVSymbolSetFunction = void (LVSymbol::*)();
/// Pointer-to-member used to query a boolean property on an LVSymbol.
using LVSymbolGetFunction = bool (LVSymbol::*)() const;
/// Pointer-to-member used to set a boolean property on an LVType.
using LVTypeSetFunction = void (LVType::*)();
/// Pointer-to-member used to query a boolean property on an LVType.
using LVTypeGetFunction = bool (LVType::*)() const;

/// Ordered collection of logical elements.
using LVElements = SmallVector<LVElement *, 8>;
/// Ordered collection of logical lines.
using LVLines = SmallVector<LVLine *, 8>;
/// Ordered collection of logical locations.
using LVLocations = SmallVector<LVLocation *, 8>;
/// Ordered collection of logical operations.
using LVOperations = SmallVector<LVOperation *, 8>;
/// Ordered collection of logical scopes.
using LVScopes = SmallVector<LVScope *, 8>;
/// Ordered collection of logical symbols.
using LVSymbols = SmallVector<LVSymbol *, 8>;
/// Ordered collection of logical types.
using LVTypes = SmallVector<LVType *, 8>;

/// Concatenated view over scopes, types, and symbols as LVElement pointers.
using LVElementsView = detail::concat_range<LVElement *const, const LVScopes &,
                                            const LVTypes &, const LVSymbols &>;
/// Ordered collection of DIE offsets.
using LVOffsets = SmallVector<LVOffset, 8>;

// The following DWARF documents detail the 'tombstone' concept:
//   https://dwarfstd.org/issues/231013.1.html
//   https://dwarfstd.org/issues/200609.1.html
//
// The value of the largest representable address offset (for example,
// 0xffffffff when the size of an address is 32 bits).
//
// -1 (0xffffffff) => Valid tombstone
/// Largest representable address value, used as a DWARF tombstone.
const LVAddress MaxAddress = std::numeric_limits<uint64_t>::max();

/// Kind of binary containing the debug information.
enum class LVBinaryType {
  /// No binary type.
  NONE,
  /// ELF object or executable.
  ELF,
  /// COFF object or executable.
  COFF
};
/// Pass of a logical-view comparison that classifies an element.
enum class LVComparePass {
  /// Element present in the reference but missing from the target.
  Missing,
  /// Element present in the target but added relative to the reference.
  Added
};

/// Pointer-to-member used to validate a location.
using LVValidLocation = bool (LVLocation::*)();

/// Counters for the main kinds of logical-view objects.
struct LVCounter {
  /// Number of logical lines counted.
  unsigned Lines = 0;
  /// Number of logical scopes counted.
  unsigned Scopes = 0;
  /// Number of logical symbols counted.
  unsigned Symbols = 0;
  /// Number of logical types counted.
  unsigned Types = 0;
  /// Reset all counters to zero.
  void reset() {
    Lines = 0;
    Scopes = 0;
    Symbols = 0;
    Types = 0;
  }
};

/// Base class for a logical-view object extracted from debug information.
class LLVM_ABI LVObject {
  enum class Property {
    IsLocation,          // Location.
    IsGlobalReference,   // This object is being referenced from another CU.
    IsGeneratedName,     // The Object name was generated.
    IsResolved,          // Object has been resolved.
    IsResolvedName,      // Object name has been resolved.
    IsDiscarded,         // Object has been stripped by the linker.
    IsOptimized,         // Object has been optimized by the compiler.
    IsAdded,             // Object has been 'added'.
    IsMatched,           // Object has been matched to a given pattern.
    IsMissing,           // Object is 'missing'.
    IsMissingLink,       // Object is indirectly 'missing'.
    IsInCompare,         // In 'compare' mode.
    IsFileFromReference, // File ID from specification.
    IsLineFromReference, // Line No from specification.
    HasMoved,            // The object was moved from 'target' to 'reference'.
    HasPattern,          // The object has a pattern.
    IsFinalized,         // CodeView object is finalized.
    IsReferenced,        // CodeView object being referenced.
    HasCodeViewLocation, // CodeView object with debug location.
    LastEntry
  };

  LVOffset Offset = 0;
  uint32_t LineNumber = 0;
  LVLevel ScopeLevel = 0;
  union {
    dwarf::Tag Tag;
    dwarf::Attribute Attr;
    LVSmall Opcode;
  } TagAttrOpcode = {dwarf::DW_TAG_null};
  // Typed bitvector with properties for this object.
  LVProperties<Property> Properties;

  // This is an internal ID used for debugging logical elements. It is used
  // for cases where an unique offset within the binary input file is not
  // available.
  static uint32_t GID;
  uint32_t ID = 0;

  // The parent of this object (nullptr if the root scope). For locations,
  // the parent is a symbol object; otherwise it is a scope object.
  union {
    LVElement *Element;
    LVScope *Scope;
    LVSymbol *Symbol;
  } Parent = {nullptr};

  // We do not support any object duplication, as they are created by parsing
  // the debug information. There is only the case where we need a very basic
  // object, to manipulate its offset, line number and scope level. Allow the
  // copy constructor to create that object; it is used to print a reference
  // to another object and in the case of templates, to print its encoded args.
  LVObject(const LVObject &Object) {
    incID();
    Properties = Object.Properties;
    Offset = Object.Offset;
    LineNumber = Object.LineNumber;
    ScopeLevel = Object.ScopeLevel;
    TagAttrOpcode = Object.TagAttrOpcode;
    Parent = Object.Parent;
  }

  void incID() {
    ++GID;
    ID = GID;
  }

protected:
  /// Format \p LineNumber and optional \p Discriminator as a display string.
  /// \param LineNumber Source line number to format.
  /// \param Discriminator Optional discriminator; zero means none.
  /// \param ShowZero Whether to show a zero line number instead of padding.
  /// \returns Formatted line number string for display.
  std::string lineAsString(uint32_t LineNumber, LVHalf Discriminator,
                           bool ShowZero) const;

  /// Format \p LineNumber as a reference string, optionally with spaces.
  /// \param LineNumber Line number used as a reference.
  /// \param Spaces Whether to include leading spaces in the result.
  /// \returns Formatted reference string.
  std::string referenceAsString(uint32_t LineNumber, bool Spaces) const;

  /// Print the filename or pathname associated with this object.
  ///
  /// Empty implementation for those objects that do not have any user
  /// source file references, such as debug locations.
  /// \param OS Stream that receives the printed file index.
  /// \param Full Whether to print full path information.
  virtual void printFileIndex(raw_ostream &OS, bool Full = true) const {}

public:
  /// Construct an empty logical-view object and assign a unique ID.
  LVObject() { incID(); };
  /// Copy assignment is not allowed.
  /// \param Other Unused source object.
  LVObject &operator=(const LVObject &Other) = delete;
  /// Destroy the logical-view object.
  virtual ~LVObject() = default;

  /// Return whether this object represents a location.
  /// \returns True when this object represents a location.
  bool getIsLocation() const { return Properties.get(Property::IsLocation); }
  /// Mark this object as a location.
  void setIsLocation() { Properties.set(Property::IsLocation); }
  /// Clear the location property on this object.
  void resetIsLocation() { Properties.reset(Property::IsLocation); }
  /// Return whether this object is referenced from another compile unit.
  /// \returns True when this object is referenced from another compile unit.
  bool getIsGlobalReference() const {
    return Properties.get(Property::IsGlobalReference);
  }
  /// Mark this object as referenced from another compile unit.
  void setIsGlobalReference() { Properties.set(Property::IsGlobalReference); }
  /// Clear the global-reference property on this object.
  void resetIsGlobalReference() {
    Properties.reset(Property::IsGlobalReference);
  }
  /// Return whether this object's name was generated.
  /// \returns True when this object's name was generated.
  bool getIsGeneratedName() const {
    return Properties.get(Property::IsGeneratedName);
  }
  /// Mark this object's name as generated.
  void setIsGeneratedName() { Properties.set(Property::IsGeneratedName); }
  /// Clear the generated-name property on this object.
  void resetIsGeneratedName() { Properties.reset(Property::IsGeneratedName); }
  /// Return whether this object has been resolved.
  /// \returns True when this object has been resolved.
  bool getIsResolved() const { return Properties.get(Property::IsResolved); }
  /// Mark this object as resolved.
  void setIsResolved() { Properties.set(Property::IsResolved); }
  /// Clear the resolved property on this object.
  void resetIsResolved() { Properties.reset(Property::IsResolved); }
  /// Return whether this object's name has been resolved.
  /// \returns True when this object's name has been resolved.
  bool getIsResolvedName() const {
    return Properties.get(Property::IsResolvedName);
  }
  /// Mark this object's name as resolved.
  void setIsResolvedName() { Properties.set(Property::IsResolvedName); }
  /// Clear the resolved-name property on this object.
  void resetIsResolvedName() { Properties.reset(Property::IsResolvedName); }
  /// Return whether this object was stripped by the linker.
  /// \returns True when this object was stripped by the linker.
  bool getIsDiscarded() const { return Properties.get(Property::IsDiscarded); }
  /// Mark this object as discarded by the linker.
  void setIsDiscarded() { Properties.set(Property::IsDiscarded); }
  /// Clear the discarded property on this object.
  void resetIsDiscarded() { Properties.reset(Property::IsDiscarded); }
  /// Return whether this object was optimized by the compiler.
  /// \returns True when this object was optimized by the compiler.
  bool getIsOptimized() const { return Properties.get(Property::IsOptimized); }
  /// Mark this object as optimized by the compiler.
  void setIsOptimized() { Properties.set(Property::IsOptimized); }
  /// Clear the optimized property on this object.
  void resetIsOptimized() { Properties.reset(Property::IsOptimized); }
  /// Return whether this object was added during comparison.
  /// \returns True when this object was added during comparison.
  bool getIsAdded() const { return Properties.get(Property::IsAdded); }
  /// Mark this object as added during comparison.
  void setIsAdded() { Properties.set(Property::IsAdded); }
  /// Clear the added property on this object.
  void resetIsAdded() { Properties.reset(Property::IsAdded); }
  /// Return whether this object matched a selection pattern.
  /// \returns True when this object matched a selection pattern.
  bool getIsMatched() const { return Properties.get(Property::IsMatched); }
  /// Mark this object as matched to a selection pattern.
  void setIsMatched() { Properties.set(Property::IsMatched); }
  /// Clear the matched property on this object.
  void resetIsMatched() { Properties.reset(Property::IsMatched); }
  /// Return whether this object is missing during comparison.
  /// \returns True when this object is missing during comparison.
  bool getIsMissing() const { return Properties.get(Property::IsMissing); }
  /// Mark this object as missing during comparison.
  void setIsMissing() { Properties.set(Property::IsMissing); }
  /// Clear the missing property on this object.
  void resetIsMissing() { Properties.reset(Property::IsMissing); }
  /// Return whether this object is indirectly missing during comparison.
  /// \returns True when this object is indirectly missing during comparison.
  bool getIsMissingLink() const {
    return Properties.get(Property::IsMissingLink);
  }
  /// Mark this object as indirectly missing during comparison.
  void setIsMissingLink() { Properties.set(Property::IsMissingLink); }
  /// Clear the missing-link property on this object.
  void resetIsMissingLink() { Properties.reset(Property::IsMissingLink); }
  /// Return whether this object is being processed in compare mode.
  /// \returns True when this object is being processed in compare mode.
  bool getIsInCompare() const { return Properties.get(Property::IsInCompare); }
  /// Mark this object as being processed in compare mode.
  void setIsInCompare() { Properties.set(Property::IsInCompare); }
  /// Clear the in-compare property on this object.
  void resetIsInCompare() { Properties.reset(Property::IsInCompare); }
  /// Return whether the file ID comes from a specification reference.
  /// \returns True when the file ID comes from a specification reference.
  bool getIsFileFromReference() const {
    return Properties.get(Property::IsFileFromReference);
  }
  /// Mark the file ID as coming from a specification reference.
  void setIsFileFromReference() {
    Properties.set(Property::IsFileFromReference);
  }
  /// Clear the file-from-reference property on this object.
  void resetIsFileFromReference() {
    Properties.reset(Property::IsFileFromReference);
  }
  /// Return whether the line number comes from a specification reference.
  /// \returns True when the line number comes from a specification reference.
  bool getIsLineFromReference() const {
    return Properties.get(Property::IsLineFromReference);
  }
  /// Mark the line number as coming from a specification reference.
  void setIsLineFromReference() {
    Properties.set(Property::IsLineFromReference);
  }
  /// Clear the line-from-reference property on this object.
  void resetIsLineFromReference() {
    Properties.reset(Property::IsLineFromReference);
  }
  /// Return whether this object was moved from target to reference.
  /// \returns True when this object was moved from target to reference.
  bool getHasMoved() const { return Properties.get(Property::HasMoved); }
  /// Mark this object as moved from target to reference.
  void setHasMoved() { Properties.set(Property::HasMoved); }
  /// Clear the moved property on this object.
  void resetHasMoved() { Properties.reset(Property::HasMoved); }
  /// Return whether this object has an associated selection pattern.
  /// \returns True when this object has an associated selection pattern.
  bool getHasPattern() const { return Properties.get(Property::HasPattern); }
  /// Mark this object as having an associated selection pattern.
  void setHasPattern() { Properties.set(Property::HasPattern); }
  /// Clear the pattern property on this object.
  void resetHasPattern() { Properties.reset(Property::HasPattern); }
  /// Return whether this CodeView object has been finalized.
  /// \returns True when this CodeView object has been finalized.
  bool getIsFinalized() const { return Properties.get(Property::IsFinalized); }
  /// Mark this CodeView object as finalized.
  void setIsFinalized() { Properties.set(Property::IsFinalized); }
  /// Clear the finalized property on this object.
  void resetIsFinalized() { Properties.reset(Property::IsFinalized); }
  /// Return whether this CodeView object is being referenced.
  /// \returns True when this CodeView object is being referenced.
  bool getIsReferenced() const {
    return Properties.get(Property::IsReferenced);
  }
  /// Mark this CodeView object as being referenced.
  void setIsReferenced() { Properties.set(Property::IsReferenced); }
  /// Clear the referenced property on this object.
  void resetIsReferenced() { Properties.reset(Property::IsReferenced); }
  /// Return whether this CodeView object has a debug location.
  /// \returns True when this CodeView object has a debug location.
  bool getHasCodeViewLocation() const {
    return Properties.get(Property::HasCodeViewLocation);
  }
  /// Mark this CodeView object as having a debug location.
  void setHasCodeViewLocation() {
    Properties.set(Property::HasCodeViewLocation);
  }
  /// Clear the CodeView-location property on this object.
  void resetHasCodeViewLocation() {
    Properties.reset(Property::HasCodeViewLocation);
  }

  /// Return whether this object has been named.
  /// \returns True when this object has been named.
  virtual bool isNamed() const { return false; }
  /// Return whether this object has been typed.
  /// \returns True when this object has been typed.
  virtual bool isTyped() const { return false; }
  /// Return whether this object has an associated source file.
  /// \returns True when this object has an associated source file.
  virtual bool isFiled() const { return false; }
  /// Return whether this object has a non-zero line number.
  /// \returns True when this object has a non-zero line number.
  bool isLined() const { return LineNumber != 0; }

  /// Return the DWARF tag stored for this object.
  /// \returns DWARF tag stored for this object.
  dwarf::Tag getTag() const { return TagAttrOpcode.Tag; }
  /// Set the DWARF tag stored for this object.
  /// \param Tag DWARF tag value to store.
  void setTag(dwarf::Tag Tag) { TagAttrOpcode.Tag = Tag; }
  /// Return the DWARF attribute stored for this object.
  /// \returns DWARF attribute stored for this object.
  dwarf::Attribute getAttr() const { return TagAttrOpcode.Attr; }
  /// Set the DWARF attribute stored for this object.
  /// \param Attr DWARF attribute value to store.
  void setAttr(dwarf::Attribute Attr) { TagAttrOpcode.Attr = Attr; }
  /// Return the expression opcode stored for this object.
  /// \returns Expression opcode stored for this object.
  LVSmall getOpcode() const { return TagAttrOpcode.Opcode; }
  /// Set the expression opcode stored for this object.
  /// \param Opcode Expression opcode value to store.
  void setOpcode(LVSmall Opcode) { TagAttrOpcode.Opcode = Opcode; }

  /// Return the DIE offset of this object.
  /// \returns DIE offset of this object.
  LVOffset getOffset() const { return Offset; }
  /// Set the DIE offset of this object.
  /// \param DieOffset Byte offset of the DIE in the debug section.
  void setOffset(LVOffset DieOffset) { Offset = DieOffset; }

  /// Return the scope nesting level of this object.
  /// \returns Scope nesting level of this object.
  LVLevel getLevel() const { return ScopeLevel; }
  /// Set the scope nesting level of this object.
  /// \param Level Nesting level where this object is located.
  void setLevel(LVLevel Level) { ScopeLevel = Level; }

  /// Return the name of this object, or an empty string if unnamed.
  /// \returns Name of this object, or an empty string if unnamed.
  virtual StringRef getName() const { return StringRef(); }
  /// Set the name of this object.
  /// \param ObjectName Name to associate with this object.
  virtual void setName(StringRef ObjectName) {}

  /// Return the parent element of this object, or nullptr if none.
  /// \returns Parent element of this object, or nullptr if none.
  LVElement *getParent() const {
    assert((!Parent.Element || static_cast<LVElement *>(Parent.Element)) &&
           "Invalid element");
    return Parent.Element;
  }
  /// Return the parent scope of this object, or nullptr if none.
  /// \returns Parent scope of this object, or nullptr if none.
  LVScope *getParentScope() const {
    assert((!Parent.Scope || static_cast<LVScope *>(Parent.Scope)) &&
           "Invalid scope");
    return Parent.Scope;
  }
  /// Return the parent symbol of this object, or nullptr if none.
  /// \returns Parent symbol of this object, or nullptr if none.
  LVSymbol *getParentSymbol() const {
    assert((!Parent.Symbol || static_cast<LVSymbol *>(Parent.Symbol)) &&
           "Invalid symbol");
    return Parent.Symbol;
  }
  /// Set the parent of this object to \p Scope.
  /// \param Scope Scope that owns this object.
  void setParent(LVScope *Scope);
  /// Set the parent of this object to \p Symbol.
  /// \param Symbol Symbol that owns this object.
  void setParent(LVSymbol *Symbol);
  /// Clear the parent of this object.
  void resetParent() { Parent = {nullptr}; }

  /// Return the lower address covered by this object.
  /// \returns Lower address covered by this object.
  virtual LVAddress getLowerAddress() const { return 0; }
  /// Set the lower address covered by this object.
  /// \param Address Lower bound of the covered address range.
  virtual void setLowerAddress(LVAddress Address) {}
  /// Return the upper address covered by this object.
  /// \returns Upper address covered by this object.
  virtual LVAddress getUpperAddress() const { return 0; }
  /// Set the upper address covered by this object.
  /// \param Address Upper bound of the covered address range.
  virtual void setUpperAddress(LVAddress Address) {}

  /// Return the source line number associated with this object.
  /// \returns Source line number associated with this object.
  uint32_t getLineNumber() const { return LineNumber; }
  /// Set the source line number associated with this object.
  /// \param Number Source line number to store.
  void setLineNumber(uint32_t Number) { LineNumber = Number; }

  /// Return a string naming the kind of this object, or nullptr if none.
  /// \returns Kind name string, or nullptr if none.
  virtual const char *kind() const { return nullptr; }

  /// Return indentation whitespace for this object's scope level.
  /// \returns Indentation whitespace for this object's scope level.
  std::string indentAsString() const;
  /// Return indentation whitespace for the given scope \p Level.
  /// \param Level Scope nesting level to indent for.
  /// \returns Indentation whitespace for the given scope level.
  std::string indentAsString(LVLevel Level) const;

  /// Return padding used when printing objects with no line number.
  /// \param ShowZero Whether a zero line number should be shown.
  /// \returns Padding string for objects without a line number.
  virtual std::string noLineAsString(bool ShowZero) const;

  /// Return the line number string used for display.
  ///
  /// For inlined functions, uses the DW_AT_call_line attribute; otherwise
  /// uses the DW_AT_decl_line attribute.
  /// \param ShowZero Whether a zero line number should be shown.
  /// \returns Formatted line number string for display.
  virtual std::string lineNumberAsString(bool ShowZero = false) const {
    return lineAsString(getLineNumber(), 0, ShowZero);
  }
  /// Return the display line number with path prefixes stripped.
  /// \param ShowZero Whether a zero line number should be shown.
  /// \returns Formatted line number string without path prefixes.
  std::string lineNumberAsStringStripped(bool ShowZero = false) const;

  /// Print the logical view for this object to \p OS.
  ///
  /// Split prints the compilation unit view to a file. Match prints the
  /// object only if it satisfies the patterns collected from the command
  /// line (see the '--select' option). Print prints the object only if it
  /// satisfies the conditions specified by the different '--print' options.
  /// Full prints full information for objects representing debug locations,
  /// aggregated scopes, compile unit, functions and namespaces.
  /// \param Split Whether to print the compilation unit view to a file.
  /// \param Match Whether to require a selection-pattern match.
  /// \param Print Whether to honor '--print' filtering options.
  /// \param OS Stream that receives the printed output.
  /// \param Full Whether to print full object details.
  /// \returns Success or an error describing why printing failed.
  virtual Error doPrint(bool Split, bool Match, bool Print, raw_ostream &OS,
                        bool Full = true) const;
  /// Print the common attributes of this object to \p OS.
  /// \param OS Stream that receives the printed attributes.
  /// \param Full Whether to print full attribute details.
  void printAttributes(raw_ostream &OS, bool Full = true) const;
  /// Print a named attribute of this object to \p OS.
  /// \param OS Stream that receives the printed attribute.
  /// \param Full Whether to print full attribute details.
  /// \param Name Attribute name to print.
  /// \param Parent Parent object providing context for the attribute.
  /// \param Value Attribute value to print.
  /// \param UseQuotes Whether to quote the printed value.
  /// \param PrintRef Whether to print a reference marker with the value.
  void printAttributes(raw_ostream &OS, bool Full, StringRef Name,
                       LVObject *Parent, StringRef Value,
                       bool UseQuotes = false, bool PrintRef = false) const;

  /// Mark this object and its parents as missing during comparison.
  void markBranchAsMissing();

  /// Print the common information for an object (name, type, etc).
  /// \param OS Stream that receives the printed object.
  /// \param Full Whether to print full object details.
  virtual void print(raw_ostream &OS, bool Full = true) const;
  /// Print kind-specific extra information for an object.
  ///
  /// Depending on the object kind, this may include class attributes,
  /// debug ranges, files, directories, and similar details.
  /// \param OS Stream that receives the printed extras.
  /// \param Full Whether to print full extra details.
  virtual void printExtra(raw_ostream &OS, bool Full = true) const {}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Dump this object to the debug stream.
  void dump() const { print(dbgs()); }
#endif

  /// Return the internal unique ID assigned to this object.
  /// \returns Internal unique ID assigned to this object.
  uint32_t getID() const { return ID; }
};

} // end namespace logicalview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVOBJECT_H
