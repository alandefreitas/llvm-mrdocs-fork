//===- DirectiveEmitter.h - Directive Language Emitter ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// DirectiveEmitter uses the descriptions of directives and clauses to construct
// common code declarations to be used in Frontends.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TABLEGEN_DIRECTIVEEMITTER_H
#define LLVM_TABLEGEN_DIRECTIVEEMITTER_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Frontend/Directive/Spelling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/TableGen/Record.h"
#include <string>
#include <vector>

namespace llvm {

/// Accessors for a DirectiveLanguage record defined in DirectiveBase.td.
class DirectiveLanguage {
public:
  /// Construct a language view from the first DirectiveLanguage in \p Records.
  ///
  /// \param Records TableGen record keeper that defines the language.
  explicit DirectiveLanguage(const RecordKeeper &Records) : Records(Records) {
    const auto &DirectiveLanguages = getDirectiveLanguages();
    Def = DirectiveLanguages[0];
  }

  /// Return the language name from the TableGen record.
  /// \returns The language name.
  StringRef getName() const { return Def->getValueAsString("name"); }

  /// Return the C++ namespace used for generated declarations.
  /// \returns The C++ namespace string.
  StringRef getCppNamespace() const {
    return Def->getValueAsString("cppNamespace");
  }

  /// Return the prefix applied to directive enum enumerators.
  /// \returns The directive enum prefix.
  StringRef getDirectivePrefix() const {
    return Def->getValueAsString("directivePrefix");
  }

  /// Return the prefix applied to clause enum enumerators.
  /// \returns The clause enum prefix.
  StringRef getClausePrefix() const {
    return Def->getValueAsString("clausePrefix");
  }

  /// Return the prefix applied to loop-modifier enum enumerators.
  /// \returns The loop-modifier enum prefix.
  StringRef getLoopModifierPrefix() const {
    return Def->getValueAsString("loopModifierPrefix");
  }

  /// Return the class name used for clause enum sets.
  /// \returns The clause enum set class name.
  StringRef getClauseEnumSetClass() const {
    return Def->getValueAsString("clauseEnumSetClass");
  }

  /// Return the Flang base class name for generated clause types.
  /// \returns The Flang clause base class name.
  StringRef getFlangClauseBaseClass() const {
    return Def->getValueAsString("flangClauseBaseClass");
  }

  /// Return whether enumerators should be made available in the namespace.
  /// \returns true if enumerators are available in the namespace.
  bool hasMakeEnumAvailableInNamespace() const {
    return Def->getValueAsBit("makeEnumAvailableInNamespace");
  }

  /// Return whether bitmask enums should be enabled in the namespace.
  /// \returns true if bitmask enums are enabled in the namespace.
  bool hasEnableBitmaskEnumInNamespace() const {
    return Def->getValueAsBit("enableBitmaskEnumInNamespace");
  }

  /// Return all Association records defined for this language.
  /// \returns The Association records for this language.
  ArrayRef<const Record *> getAssociations() const {
    return Records.getAllDerivedDefinitions("Association");
  }

  /// Return all Category records defined for this language.
  /// \returns The Category records for this language.
  ArrayRef<const Record *> getCategories() const {
    return Records.getAllDerivedDefinitions("Category");
  }

  /// Return all SourceLanguage records defined for this language.
  /// \returns The SourceLanguage records for this language.
  ArrayRef<const Record *> getSourceLanguages() const {
    return Records.getAllDerivedDefinitions("SourceLanguage");
  }

  /// Return all Directive records defined for this language.
  /// \returns The Directive records for this language.
  ArrayRef<const Record *> getDirectives() const {
    return Records.getAllDerivedDefinitions("Directive");
  }

  /// Return all Clause records defined for this language.
  /// \returns The Clause records for this language.
  ArrayRef<const Record *> getClauses() const {
    return Records.getAllDerivedDefinitions("Clause");
  }

  /// Return all LoopModifier records defined for this language.
  /// \returns The LoopModifier records for this language.
  ArrayRef<const Record *> getLoopModifiers() const {
    return Records.getAllDerivedDefinitions("LoopModifier");
  }

  /// Return true if the language description has validity errors.
  /// \returns true if the language description has validity errors.
  bool HasValidityErrors() const;

private:
  const Record *Def;
  const RecordKeeper &Records;

  ArrayRef<const Record *> getDirectiveLanguages() const {
    return Records.getAllDerivedDefinitions("DirectiveLanguage");
  }
};

/// Helpers for reading min/max version fields from a TableGen record.
class Versioned {
public:
  /// Return the minimum version stored on \p R.
  ///
  /// \param R Record that defines a `minVersion` field.
  /// \returns The minimum version stored on \p R.
  int getMinVersion(const Record *R) const {
    int64_t Min = R->getValueAsInt("minVersion");
    assert(llvm::isInt<IntWidth>(Min) && "Value out of range of 'int'");
    return Min;
  }

  /// Return the maximum version stored on \p R.
  ///
  /// \param R Record that defines a `maxVersion` field.
  /// \returns The maximum version stored on \p R.
  int getMaxVersion(const Record *R) const {
    int64_t Max = R->getValueAsInt("maxVersion");
    assert(llvm::isInt<IntWidth>(Max) && "Value out of range of 'int'");
    return Max;
  }

private:
  constexpr static int IntWidth = 8 * sizeof(int);
};

/// Wrapper for a spelling record with an associated version range.
class Spelling : public Versioned {
public:
  /// Spelling text paired with the versions where it is valid.
  using Value = directive::Spelling;

  /// Construct a spelling view over TableGen record \p Def.
  ///
  /// \param Def Spelling record from DirectiveBase.td.
  Spelling(const Record *Def) : Def(Def) {}

  /// Return the spelling text from the record.
  /// \returns The spelling text.
  StringRef getText() const { return Def->getValueAsString("spelling"); }

  /// Return the version range for which this spelling is valid.
  /// \returns The version range for this spelling.
  llvm::directive::VersionRange getVersions() const {
    return llvm::directive::VersionRange{getMinVersion(Def),
                                         getMaxVersion(Def)};
  }

  /// Return the spelling text and version range as a Value.
  /// \returns The spelling text and version range.
  Value get() const { return Value{getText(), getVersions()}; }

private:
  const Record *Def;
};

// Note: In all the classes below, allow implicit construction from Record *,
// to allow writing code like:
//  for (const Directive D : getDirectives()) {
//
//  instead of:
//
//  for (const Record *R : getDirectives()) {
//    Directive D(R);

/// Shared accessors for Directive and Clause records in DirectiveBase.td.
class BaseRecord {
public:
  /// Construct a base record view over TableGen record \p Def.
  ///
  /// \param Def Directive or Clause record from DirectiveBase.td.
  BaseRecord(const Record *Def) : Def(Def) {}

  /// Return all spellings defined on this record.
  /// \returns The spellings defined on this record.
  std::vector<Spelling::Value> getSpellings() const {
    std::vector<Spelling::Value> List;
    llvm::transform(Def->getValueAsListOfDefs("spellings"),
                    std::back_inserter(List),
                    [](const Record *R) { return Spelling(R).get(); });
    return List;
  }

  /// Return a stable spelling suitable for generating an identifier name.
  ///
  /// From all spellings, pick the first one with the minimum version
  /// (i.e. pick the first from all the oldest ones). This guarantees
  /// that given several equivalent (in terms of versions) names, the
  /// first one is used, e.g. given
  ///   Clause<[Spelling<"foo">, Spelling<"bar">]> ...
  /// "foo" will be the selected spelling.
  ///
  /// This is a suitable spelling for generating an identifier name,
  /// since it will remain unchanged when any potential new spellings
  /// are added.
  /// \returns A stable spelling suitable for generating an identifier name.
  StringRef getSpellingForIdentifier() const {
    Spelling::Value Oldest{"not found", {/*Min=*/INT_MAX, 0}};
    for (auto V : getSpellings())
      if (V.Versions.Min < Oldest.Versions.Min)
        Oldest = V;
    return Oldest.Name;
  }

  /// Return \p Name with whitespace replaced by underscores.
  ///
  /// \param Name Spelling or name to format for output identifiers.
  /// \returns \p Name with whitespace replaced by underscores.
  static std::string getSnakeName(StringRef Name) {
    std::string N = Name.str();
    llvm::replace(N, ' ', '_');
    return N;
  }

  /// Return \p Name converted to UpperCamelCase by removing separators.
  ///
  /// Take a string Name with sub-words separated with characters from Sep,
  /// and return a string with each of the sub-words capitalized, and the
  /// separators removed, e.g.
  ///   Name = "some_directive^name", Sep = "_^"  ->  "SomeDirectiveName".
  ///
  /// \param Name Input name whose sub-words are separated by \p Sep.
  /// \param Sep Characters treated as sub-word separators.
  /// \returns \p Name converted to UpperCamelCase with separators removed.
  static std::string getUpperCamelName(StringRef Name, StringRef Sep) {
    std::string Camel = Name.str();
    // Convert to uppercase
    bool Cap = true;
    llvm::transform(Camel, Camel.begin(), [&](unsigned char C) {
      if (Sep.contains(C)) {
        assert(!Cap && "No initial or repeated separators");
        Cap = true;
      } else if (Cap) {
        C = llvm::toUpper(C);
        Cap = false;
      }
      return C;
    });
    size_t Out = 0;
    // Remove separators
    for (size_t In = 0, End = Camel.size(); In != End; ++In) {
      unsigned char C = Camel[In];
      if (!Sep.contains(C))
        Camel[Out++] = C;
    }
    Camel.resize(Out);
    return Camel;
  }

  /// Return the record's name formatted with spaces as underscores.
  /// \returns The record name with spaces replaced by underscores.
  std::string getFormattedName() const {
    if (auto maybeName = Def->getValueAsOptionalString("name"))
      return getSnakeName(*maybeName);
    return getSnakeName(getSpellingForIdentifier());
  }

  /// Return whether this record is marked as the default entry.
  /// \returns true if this record is the default entry.
  bool isDefault() const { return Def->getValueAsBit("isDefault"); }

  /// Return the TableGen record name.
  /// \returns The TableGen record name.
  StringRef getRecordName() const { return Def->getName(); }

  /// Return the underlying TableGen record.
  /// \returns The underlying TableGen record.
  const Record *getRecord() const { return Def; }

protected:
  /// Underlying TableGen record for this directive or clause.
  const Record *Def;
};

/// Accessors for a Directive record defined in DirectiveBase.td.
class Directive : public BaseRecord {
public:
  /// Construct a directive view over TableGen record \p Def.
  ///
  /// \param Def Directive record from DirectiveBase.td.
  Directive(const Record *Def) : BaseRecord(Def) {}

  /// Return the clauses that may appear any number of times.
  /// \returns The clauses that may appear any number of times.
  std::vector<const Record *> getAllowedClauses() const {
    return Def->getValueAsListOfDefs("allowedClauses");
  }

  /// Return the clauses that may appear at most once.
  /// \returns The clauses that may appear at most once.
  std::vector<const Record *> getAllowedOnceClauses() const {
    return Def->getValueAsListOfDefs("allowedOnceClauses");
  }

  /// Return the clauses that are mutually exclusive with each other.
  /// \returns The mutually exclusive clauses for this directive.
  std::vector<const Record *> getAllowedExclusiveClauses() const {
    return Def->getValueAsListOfDefs("allowedExclusiveClauses");
  }

  /// Return the clauses that are required on this directive.
  /// \returns The clauses required on this directive.
  std::vector<const Record *> getRequiredClauses() const {
    return Def->getValueAsListOfDefs("requiredClauses");
  }

  /// Return the leaf constructs that compose this combined directive.
  /// \returns The leaf constructs of this combined directive.
  std::vector<const Record *> getLeafConstructs() const {
    return Def->getValueAsListOfDefs("leafConstructs");
  }

  /// Return the association record that describes what this directive binds to.
  /// \returns The association record for this directive.
  const Record *getAssociation() const {
    return Def->getValueAsDef("association");
  }

  /// Return the category record for this directive.
  /// \returns The category record for this directive.
  const Record *getCategory() const { return Def->getValueAsDef("category"); }

  /// Return the source languages in which this directive is valid.
  /// \returns The source languages in which this directive is valid.
  std::vector<const Record *> getSourceLanguages() const {
    return Def->getValueAsListOfDefs("languages");
  }

  /// Return the loop modifiers allowed on this directive.
  /// \returns The loop modifiers allowed on this directive.
  std::vector<const Record *> getAllowedLoopModifiers() const {
    return Def->getValueAsListOfDefs("allowedLoopModifiers");
  }

  /// Return the language version since which this directive is pure.
  /// \returns The language version since which this directive is pure.
  int getPureSince() const { return Def->getValueAsInt("pureSince"); }

  /// Return the Clang OpenACC enumerator spelling for this directive.
  ///
  /// Clang uses a different format for names of its directives enum.
  /// \returns The Clang OpenACC enumerator spelling.
  std::string getClangAccSpelling() const {
    StringRef Name = getSpellingForIdentifier();

    // Clang calls the 'unknown' value 'invalid'.
    if (Name == "unknown")
      return "Invalid";

    return BaseRecord::getUpperCamelName(Name, " _");
  }
};

/// Accessors for a Clause record defined in DirectiveBase.td.
class Clause : public BaseRecord {
public:
  /// Construct a clause view over TableGen record \p Def.
  ///
  /// \param Def Clause record from DirectiveBase.td.
  Clause(const Record *Def) : BaseRecord(Def) {}

  /// Return the optional Clang class name associated with this clause.
  /// \returns The optional Clang class name for this clause.
  StringRef getClangClass() const {
    return Def->getValueAsString("clangClass");
  }

  /// Return the optional Flang class name associated with this clause.
  /// \returns The optional Flang class name for this clause.
  StringRef getFlangClass() const {
    return Def->getValueAsString("flangClass");
  }

  /// Return the UpperCamelCase Flang parser class name for this clause.
  ///
  /// The generic formatted class name is constructed from the name where the
  /// first letter of each word is capitalized and the underscores are removed.
  /// ex: async -> Async
  ///     num_threads -> NumThreads
  /// \returns The UpperCamelCase Flang parser class name.
  std::string getFormattedParserClassName() const {
    std::string Name = getFormattedName();
    return BaseRecord::getUpperCamelName(Name, "_");
  }

  /// Return the Clang OpenACC enumerator spelling for this clause.
  ///
  /// Clang uses a different format for names of its clause enum, which can be
  /// overwritten with the `clangSpelling` value.
  /// \returns The Clang OpenACC enumerator spelling.
  std::string getClangAccSpelling() const {
    if (StringRef ClangSpelling = Def->getValueAsString("clangAccSpelling");
        !ClangSpelling.empty())
      return ClangSpelling.str();

    StringRef Name = getSpellingForIdentifier();
    return BaseRecord::getUpperCamelName(Name, "_");
  }

  /// Return the optional enum type name used for this clause's values.
  /// \returns The optional enum type name for this clause's values.
  StringRef getEnumName() const {
    return Def->getValueAsString("enumClauseValue");
  }

  /// Return the allowed enumerated values for this clause.
  /// \returns The allowed enumerated values for this clause.
  std::vector<const Record *> getClauseVals() const {
    return Def->getValueAsListOfDefs("allowedClauseValues");
  }

  /// Return whether Flang should skip unparsing this clause.
  /// \returns true if Flang should skip unparsing this clause.
  bool skipFlangUnparser() const {
    return Def->getValueAsBit("skipFlangUnparser");
  }

  /// Return whether the clause value may be omitted.
  /// \returns true if the clause value may be omitted.
  bool isValueOptional() const { return Def->getValueAsBit("isValueOptional"); }

  /// Return whether the clause takes a list of values.
  /// \returns true if the clause takes a list of values.
  bool isValueList() const { return Def->getValueAsBit("isValueList"); }

  /// Return the default value used when the clause value is omitted.
  /// \returns The default value used when the clause value is omitted.
  StringRef getDefaultValue() const {
    return Def->getValueAsString("defaultValue");
  }

  /// Return whether this clause is implicit rather than written by the user.
  /// \returns true if this clause is implicit.
  bool isImplicit() const { return Def->getValueAsBit("isImplicit"); }

  /// Return alternate spellings accepted for this clause.
  /// \returns The alternate spellings accepted for this clause.
  std::vector<StringRef> getAliases() const {
    return Def->getValueAsListOfStrings("aliases");
  }

  /// Return the optional prefix text that may precede the clause value.
  /// \returns The optional prefix text for the clause value.
  StringRef getPrefix() const { return Def->getValueAsString("prefix"); }

  /// Return whether the clause prefix may be omitted.
  /// \returns true if the clause prefix may be omitted.
  bool isPrefixOptional() const {
    return Def->getValueAsBit("isPrefixOptional");
  }
};

/// Accessors for a VersionedClause record defined in DirectiveBase.td.
class VersionedClause {
public:
  /// Construct a versioned-clause view over TableGen record \p Def.
  ///
  /// \param Def VersionedClause record from DirectiveBase.td.
  VersionedClause(const Record *Def) : Def(Def) {}

  /// Return the specific clause record wrapped in the Clause class.
  /// \returns The clause wrapped in the Clause class.
  Clause getClause() const { return Clause(Def->getValueAsDef("clause")); }

  /// Return the minimum language version for which this clause is allowed.
  /// \returns The minimum language version for this clause.
  int64_t getMinVersion() const { return Def->getValueAsInt("minVersion"); }

  /// Return the maximum language version for which this clause is allowed.
  /// \returns The maximum language version for this clause.
  int64_t getMaxVersion() const { return Def->getValueAsInt("maxVersion"); }

private:
  const Record *Def;
};

/// Accessors for an enumeration value record used by clauses.
class EnumVal : public BaseRecord {
public:
  /// Construct an enum-value view over TableGen record \p Def.
  ///
  /// \param Def EnumVal record from DirectiveBase.td.
  EnumVal(const Record *Def) : BaseRecord(Def) {}

  /// Return the integer value associated with this enumerator.
  /// \returns The integer value of this enumerator.
  int getValue() const { return Def->getValueAsInt("value"); }

  /// Return whether this enumerator is visible to end users.
  /// \returns true if this enumerator is visible to end users.
  bool isUserVisible() const { return Def->getValueAsBit("isUserValue"); }
};

} // namespace llvm

#endif // LLVM_TABLEGEN_DIRECTIVEEMITTER_H
