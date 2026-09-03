//===-- LVOptions.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the LVOptions class, which is used to record the command
// line options.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVOPTIONS_H
#define LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVOPTIONS_H

#include "llvm/ADT/StringSet.h"
#include "llvm/DebugInfo/LogicalView/Core/LVLine.h"
#include "llvm/DebugInfo/LogicalView/Core/LVScope.h"
#include "llvm/DebugInfo/LogicalView/Core/LVSymbol.h"
#include "llvm/DebugInfo/LogicalView/Core/LVType.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Regex.h"
#include <set>
#include <string>

namespace llvm {
namespace logicalview {

/// Set of debug-info offsets used for selection patterns.
using LVOffsetSet = std::set<uint64_t>;

/// Attribute kinds selectable with \c --attribute=.
enum class LVAttributeKind {
  /// Select all attribute kinds.
  All,
  /// Show function-argument attributes.
  Argument,
  /// Show base-type attributes.
  Base,
  /// Show location-coverage attributes.
  Coverage,
  /// Show directory-table attributes.
  Directories,
  /// Show attributes for entities discarded by the linker.
  Discarded,
  /// Show DWARF discriminator attributes.
  Discriminator,
  /// Show encoded-name attributes.
  Encoded,
  /// Show extended attributes.
  Extended,
  /// Show source-filename attributes.
  Filename,
  /// Show file-table attributes.
  Files,
  /// Show file-format attributes.
  Format,
  /// Show location-gap attributes.
  Gaps,
  /// Show attributes for compiler-generated entities.
  Generated,
  /// Show global-symbol attributes.
  Global,
  /// Show attributes for entities inserted during comparison.
  Inserted,
  /// Show source-language attributes.
  Language,
  /// Show lexical-level attributes.
  Level,
  /// Show linkage-name attributes.
  Linkage,
  /// Show local-symbol attributes.
  Local,
  /// Show debug-location attributes.
  Location,
  /// Show DIE-offset attributes.
  Offset,
  /// Show full-pathname attributes.
  Pathname,
  /// Show producer-string attributes.
  Producer,
  /// Show public-symbol attributes.
  Publics,
  /// Show fully-qualified-name attributes.
  Qualified,
  /// Show type-qualifier attributes.
  Qualifier,
  /// Show address-range attributes.
  Range,
  /// Show reference attributes.
  Reference,
  /// Show register-location attributes.
  Register,
  /// Show size attributes.
  Size,
  /// Show standard attributes.
  Standard,
  /// Show array-subrange attributes.
  Subrange,
  /// Show system attributes.
  System,
  /// Show type-name attributes.
  Typename,
  /// Show underlying-type attributes.
  Underlying,
  /// Show zero-value attributes.
  Zero
};
/// Set of selected \c LVAttributeKind values.
using LVAttributeKindSet = std::set<LVAttributeKind>;

/// Element kinds selectable with \c --compare=.
enum class LVCompareKind {
  /// Compare all supported element kinds.
  All,
  /// Compare line records.
  Lines,
  /// Compare scopes.
  Scopes,
  /// Compare symbols.
  Symbols,
  /// Compare types.
  Types
};
/// Set of selected \c LVCompareKind values.
using LVCompareKindSet = std::set<LVCompareKind>;

/// Output formats selectable with \c --output=.
enum class LVOutputKind {
  /// Enable all output formats.
  All,
  /// Split output into separate files.
  Split,
  /// Emit JSON output.
  Json,
  /// Emit text output.
  Text
};
/// Set of selected \c LVOutputKind values.
using LVOutputKindSet = std::set<LVOutputKind>;

/// Print categories selectable with \c --print=.
enum class LVPrintKind {
  /// Print all supported categories.
  All,
  /// Print logical elements.
  Elements,
  /// Print instructions.
  Instructions,
  /// Print lines.
  Lines,
  /// Print scopes.
  Scopes,
  /// Print size information.
  Sizes,
  /// Print symbols.
  Symbols,
  /// Print a summary.
  Summary,
  /// Print types.
  Types,
  /// Print warnings.
  Warnings
};
/// Set of selected \c LVPrintKind values.
using LVPrintKindSet = std::set<LVPrintKind>;

/// Report layouts selectable with \c --report=.
enum class LVReportKind {
  /// Enable all report layouts.
  All,
  /// Include children in the report.
  Children,
  /// Emit a list report.
  List,
  /// Include parents in the report.
  Parents,
  /// Emit a view report.
  View
};
/// Set of selected \c LVReportKind values.
using LVReportKindSet = std::set<LVReportKind>;

/// Warning categories selectable with \c --warning=.
enum class LVWarningKind {
  /// Enable all warning categories.
  All,
  /// Warn about coverage issues.
  Coverages,
  /// Warn about line issues.
  Lines,
  /// Warn about location issues.
  Locations,
  /// Warn about range issues.
  Ranges
};
/// Set of selected \c LVWarningKind values.
using LVWarningKindSet = std::set<LVWarningKind>;

/// Internal diagnostics selectable with \c --internal=.
enum class LVInternalKind {
  /// Enable all internal diagnostics.
  All,
  /// Dump the effective command line.
  Cmdline,
  /// Dump unique element IDs.
  ID,
  /// Run integrity checks.
  Integrity,
  /// Disable internal diagnostics.
  None,
  /// Dump DWARF tags.
  Tag
};
/// Set of selected \c LVInternalKind values.
using LVInternalKindSet = std::set<LVInternalKind>;

/// Records command-line options for logical-view analysis and printing.
///
/// The \c Kinds members are a one-to-one mapping to the associated command
/// options that supports comma separated values. There are other \c bool
/// members that in very few cases point to a command option (see associated
/// comment). Other cases for \c bool refers to internal values derivated from
/// the command options.
class LVOptions {
  class LVAttribute {
  public:
    LVAttributeKindSet Kinds; // --attribute=<Kind>
    bool Added = false;       // Added elements found during comparison.
    bool AnyLocation = false; // Any kind of location information.
    bool AnySource = false;   // Any kind of source information.
    bool Missing = false;     // Missing elements found during comparison.
  };

  class LVCompare {
  public:
    LVCompareKindSet Elements; // --compare=<kind>
    bool Context = false;      // --compare-context
    bool Execute = false;      // Compare requested.
    bool Print = false;        // Enable any printing.
  };

  class LVPrint {
  public:
    LVPrintKindSet Kinds;      // --print=<Kind>
    bool AnyElement = false;   // Request to print any element.
    bool AnyLine = false;      // Print 'lines' or 'instructions'.
    bool Execute = false;      // Print requested.
    bool Formatting = true;    // Disable formatting during printing.
    bool Offset = false;       // Print offsets while formatting is disabled.
    bool SizesSummary = false; // Print 'sizes' or 'summary'.
  };

  class LVReport {
  public:
    LVReportKindSet Kinds; // --report=<kind>
    bool AnyView = false;  // View, Parents or Children.
    bool Execute = false;  // Report requested.
  };

  class LVSelect {
  public:
    bool IgnoreCase = false;     // --select-ignore-case
    bool UseRegex = false;       // --select-use-regex
    bool Execute = false;        // Select requested.
    bool GenericKind = false;    // We have collected generic kinds.
    bool GenericPattern = false; // We have collected generic patterns.
    bool OffsetPattern = false;  // We have collected offset patterns.
    StringSet<> Generic;         // --select=<Pattern>
    LVOffsetSet Offsets;         // --select-offset=<Offset>
    LVElementKindSet Elements;   // --select-elements=<Kind>
    LVLineKindSet Lines;         // --select-lines=<Kind>
    LVScopeKindSet Scopes;       // --select-scopes=<Kind>
    LVSymbolKindSet Symbols;     // --select-symbols=<Kind>
    LVTypeKindSelection Types;   // --select-types=<Kind>
  };

  class LVOutput {
  public:
    LVOutputKindSet Kinds;                  // --output=<kind>
    LVSortMode SortMode = LVSortMode::None; // --output-sort=<SortMode>
    std::string Folder;                     // --output-folder=<Folder>
    unsigned Level = -1U;                   // --output-level=<level>
  };

  class LVWarning {
  public:
    LVWarningKindSet Kinds; // --warning=<Kind>
  };

  class LVInternal {
  public:
    LVInternalKindSet Kinds; // --internal=<Kind>
  };

  class LVGeneral {
  public:
    bool CollectRanges = false; // Collect ranges information.
  };

  // Filters the output of the filename associated with the element being
  // printed in order to see clearly which logical elements belongs to
  // a particular filename. It is value is reset after the element
  // that represents the Compile Unit is printed.
  size_t LastFilenameIndex = 0;

  // Controls the amount of additional spaces to insert when printing
  // object attributes, in order to get a consistent printing layout.
  size_t IndentationSize = 0;

  // Calculate the indentation size, so we can use that value when printing
  // additional attributes to objects, such as location.
  void calculateIndentationSize();

public:
  /// Reset the cached last-printed filename index to zero.
  void resetFilenameIndex() { LastFilenameIndex = 0; }
  /// Update the cached filename index and report whether it changed.
  /// \param Index New filename index associated with the element being printed.
  /// \returns True if \p Index differs from the previously cached index.
  bool changeFilenameIndex(size_t Index) {
    bool IndexChanged = (Index != LastFilenameIndex);
    if (IndexChanged)
      LastFilenameIndex = Index;
    return IndexChanged;
  }

  /// Return the process-wide logical-view options instance.
  /// \returns Pointer to the process-wide logical-view options instance.
  LLVM_ABI static LVOptions *getOptions();
  /// Install \p Options as the process-wide logical-view options instance.
  /// \param Options Options object to install; ownership is not transferred.
  LLVM_ABI static void setOptions(LVOptions *Options);

  /// Construct options with default command-line settings.
  LVOptions() = default;
  /// Copy-construct options from \p Other.
  /// \param Other Source options to copy.
  LVOptions(const LVOptions &Other) = default;
  /// Copy-assign options from \p Other.
  /// \param Other Source options to copy.
  /// \returns Reference to this options object.
  LVOptions &operator=(const LVOptions &Other) = default;
  /// Destroy the options object.
  ~LVOptions() = default;

  /// Resolve shortcuts and cross-option dependencies in the recorded options.
  ///
  /// Some command line options support shortcuts. For example:
  /// The command line option '--print=elements' is a shortcut for:
  /// '--print=instructions,lines,scopes,symbols,types'.
  /// In the case of logical view comparison, some options related to
  /// attributes must be set or reset for a proper comparison.
  LLVM_ABI void resolveDependencies();
  /// Return the indentation size used when printing object attributes.
  /// \returns Indentation size used when printing object attributes.
  size_t indentationSize() const { return IndentationSize; }

  /// Attribute-display options selected with \c --attribute=.
  LVAttribute Attribute;
  /// Comparison options selected with \c --compare=.
  LVCompare Compare;
  /// Output-format options selected with \c --output=.
  LVOutput Output;
  /// Printing options selected with \c --print=.
  LVPrint Print;
  /// Report-layout options selected with \c --report=.
  LVReport Report;
  /// Selection options selected with \c --select=.
  LVSelect Select;
  /// Warning options selected with \c --warning=.
  LVWarning Warning;
  /// Internal-diagnostic options selected with \c --internal=.
  LVInternal Internal;
  /// General derived option shortcuts.
  LVGeneral General;

  // --attribute.
  /// Return whether all attribute kinds are selected.
  /// \returns True when all attribute kinds are selected.
  bool getAttributeAll() const {
    return Attribute.Kinds.find(LVAttributeKind::All) != Attribute.Kinds.end();
  }
  /// Enable showing all attribute kinds.
  void setAttributeAll() { Attribute.Kinds.insert(LVAttributeKind::All); }
  /// Disable showing all attribute kinds.
  void resetAttributeAll() { Attribute.Kinds.erase(LVAttributeKind::All); }
  /// Return whether function-argument attributes are shown.
  /// \returns True when function-argument attributes are shown.
  bool getAttributeArgument() const {
    return Attribute.Kinds.find(LVAttributeKind::Argument) != Attribute.Kinds.end();
  }
  /// Enable showing function-argument attributes.
  void setAttributeArgument() { Attribute.Kinds.insert(LVAttributeKind::Argument); }
  /// Disable showing function-argument attributes.
  void resetAttributeArgument() { Attribute.Kinds.erase(LVAttributeKind::Argument); }
  /// Return whether base-type attributes are shown.
  /// \returns True when base-type attributes are shown.
  bool getAttributeBase() const {
    return Attribute.Kinds.find(LVAttributeKind::Base) != Attribute.Kinds.end();
  }
  /// Enable showing base-type attributes.
  void setAttributeBase() { Attribute.Kinds.insert(LVAttributeKind::Base); }
  /// Disable showing base-type attributes.
  void resetAttributeBase() { Attribute.Kinds.erase(LVAttributeKind::Base); }
  /// Return whether location-coverage attributes are shown.
  /// \returns True when location-coverage attributes are shown.
  bool getAttributeCoverage() const {
    return Attribute.Kinds.find(LVAttributeKind::Coverage) != Attribute.Kinds.end();
  }
  /// Enable showing location-coverage attributes.
  void setAttributeCoverage() { Attribute.Kinds.insert(LVAttributeKind::Coverage); }
  /// Disable showing location-coverage attributes.
  void resetAttributeCoverage() { Attribute.Kinds.erase(LVAttributeKind::Coverage); }
  /// Return whether directory-table attributes are shown.
  /// \returns True when directory-table attributes are shown.
  bool getAttributeDirectories() const {
    return Attribute.Kinds.find(LVAttributeKind::Directories) != Attribute.Kinds.end();
  }
  /// Enable showing directory-table attributes.
  void setAttributeDirectories() { Attribute.Kinds.insert(LVAttributeKind::Directories); }
  /// Disable showing directory-table attributes.
  void resetAttributeDirectories() { Attribute.Kinds.erase(LVAttributeKind::Directories); }
  /// Return whether discarded-by-linker attributes are shown.
  /// \returns True when discarded-by-linker attributes are shown.
  bool getAttributeDiscarded() const {
    return Attribute.Kinds.find(LVAttributeKind::Discarded) != Attribute.Kinds.end();
  }
  /// Enable showing discarded-by-linker attributes.
  void setAttributeDiscarded() { Attribute.Kinds.insert(LVAttributeKind::Discarded); }
  /// Disable showing discarded-by-linker attributes.
  void resetAttributeDiscarded() { Attribute.Kinds.erase(LVAttributeKind::Discarded); }
  /// Return whether DWARF discriminator attributes are shown.
  /// \returns True when DWARF discriminator attributes are shown.
  bool getAttributeDiscriminator() const {
    return Attribute.Kinds.find(LVAttributeKind::Discriminator) != Attribute.Kinds.end();
  }
  /// Enable showing DWARF discriminator attributes.
  void setAttributeDiscriminator() { Attribute.Kinds.insert(LVAttributeKind::Discriminator); }
  /// Disable showing DWARF discriminator attributes.
  void resetAttributeDiscriminator() { Attribute.Kinds.erase(LVAttributeKind::Discriminator); }
  /// Return whether encoded-name attributes are shown.
  /// \returns True when encoded-name attributes are shown.
  bool getAttributeEncoded() const {
    return Attribute.Kinds.find(LVAttributeKind::Encoded) != Attribute.Kinds.end();
  }
  /// Enable showing encoded-name attributes.
  void setAttributeEncoded() { Attribute.Kinds.insert(LVAttributeKind::Encoded); }
  /// Disable showing encoded-name attributes.
  void resetAttributeEncoded() { Attribute.Kinds.erase(LVAttributeKind::Encoded); }
  /// Return whether extended attributes are shown.
  /// \returns True when extended attributes are shown.
  bool getAttributeExtended() const {
    return Attribute.Kinds.find(LVAttributeKind::Extended) != Attribute.Kinds.end();
  }
  /// Enable showing extended attributes.
  void setAttributeExtended() { Attribute.Kinds.insert(LVAttributeKind::Extended); }
  /// Disable showing extended attributes.
  void resetAttributeExtended() { Attribute.Kinds.erase(LVAttributeKind::Extended); }
  /// Return whether source-filename attributes are shown.
  /// \returns True when source-filename attributes are shown.
  bool getAttributeFilename() const {
    return Attribute.Kinds.find(LVAttributeKind::Filename) != Attribute.Kinds.end();
  }
  /// Enable showing source-filename attributes.
  void setAttributeFilename() { Attribute.Kinds.insert(LVAttributeKind::Filename); }
  /// Disable showing source-filename attributes.
  void resetAttributeFilename() { Attribute.Kinds.erase(LVAttributeKind::Filename); }
  /// Return whether file-table attributes are shown.
  /// \returns True when file-table attributes are shown.
  bool getAttributeFiles() const {
    return Attribute.Kinds.find(LVAttributeKind::Files) != Attribute.Kinds.end();
  }
  /// Enable showing file-table attributes.
  void setAttributeFiles() { Attribute.Kinds.insert(LVAttributeKind::Files); }
  /// Disable showing file-table attributes.
  void resetAttributeFiles() { Attribute.Kinds.erase(LVAttributeKind::Files); }
  /// Return whether file-format attributes are shown.
  /// \returns True when file-format attributes are shown.
  bool getAttributeFormat() const {
    return Attribute.Kinds.find(LVAttributeKind::Format) != Attribute.Kinds.end();
  }
  /// Enable showing file-format attributes.
  void setAttributeFormat() { Attribute.Kinds.insert(LVAttributeKind::Format); }
  /// Disable showing file-format attributes.
  void resetAttributeFormat() { Attribute.Kinds.erase(LVAttributeKind::Format); }
  /// Return whether location-gap attributes are shown.
  /// \returns True when location-gap attributes are shown.
  bool getAttributeGaps() const {
    return Attribute.Kinds.find(LVAttributeKind::Gaps) != Attribute.Kinds.end();
  }
  /// Enable showing location-gap attributes.
  void setAttributeGaps() { Attribute.Kinds.insert(LVAttributeKind::Gaps); }
  /// Disable showing location-gap attributes.
  void resetAttributeGaps() { Attribute.Kinds.erase(LVAttributeKind::Gaps); }
  /// Return whether compiler-generated attributes are shown.
  /// \returns True when compiler-generated attributes are shown.
  bool getAttributeGenerated() const {
    return Attribute.Kinds.find(LVAttributeKind::Generated) != Attribute.Kinds.end();
  }
  /// Enable showing compiler-generated attributes.
  void setAttributeGenerated() { Attribute.Kinds.insert(LVAttributeKind::Generated); }
  /// Disable showing compiler-generated attributes.
  void resetAttributeGenerated() { Attribute.Kinds.erase(LVAttributeKind::Generated); }
  /// Return whether global-symbol attributes are shown.
  /// \returns True when global-symbol attributes are shown.
  bool getAttributeGlobal() const {
    return Attribute.Kinds.find(LVAttributeKind::Global) != Attribute.Kinds.end();
  }
  /// Enable showing global-symbol attributes.
  void setAttributeGlobal() { Attribute.Kinds.insert(LVAttributeKind::Global); }
  /// Disable showing global-symbol attributes.
  void resetAttributeGlobal() { Attribute.Kinds.erase(LVAttributeKind::Global); }
  /// Return whether inserted-during-comparison attributes are shown.
  /// \returns True when inserted-during-comparison attributes are shown.
  bool getAttributeInserted() const {
    return Attribute.Kinds.find(LVAttributeKind::Inserted) != Attribute.Kinds.end();
  }
  /// Enable showing inserted-during-comparison attributes.
  void setAttributeInserted() { Attribute.Kinds.insert(LVAttributeKind::Inserted); }
  /// Disable showing inserted-during-comparison attributes.
  void resetAttributeInserted() { Attribute.Kinds.erase(LVAttributeKind::Inserted); }
  /// Return whether source-language attributes are shown.
  /// \returns True when source-language attributes are shown.
  bool getAttributeLanguage() const {
    return Attribute.Kinds.find(LVAttributeKind::Language) != Attribute.Kinds.end();
  }
  /// Enable showing source-language attributes.
  void setAttributeLanguage() { Attribute.Kinds.insert(LVAttributeKind::Language); }
  /// Disable showing source-language attributes.
  void resetAttributeLanguage() { Attribute.Kinds.erase(LVAttributeKind::Language); }
  /// Return whether lexical-level attributes are shown.
  /// \returns True when lexical-level attributes are shown.
  bool getAttributeLevel() const {
    return Attribute.Kinds.find(LVAttributeKind::Level) != Attribute.Kinds.end();
  }
  /// Enable showing lexical-level attributes.
  void setAttributeLevel() { Attribute.Kinds.insert(LVAttributeKind::Level); }
  /// Disable showing lexical-level attributes.
  void resetAttributeLevel() { Attribute.Kinds.erase(LVAttributeKind::Level); }
  /// Return whether linkage-name attributes are shown.
  /// \returns True when linkage-name attributes are shown.
  bool getAttributeLinkage() const {
    return Attribute.Kinds.find(LVAttributeKind::Linkage) != Attribute.Kinds.end();
  }
  /// Enable showing linkage-name attributes.
  void setAttributeLinkage() { Attribute.Kinds.insert(LVAttributeKind::Linkage); }
  /// Disable showing linkage-name attributes.
  void resetAttributeLinkage() { Attribute.Kinds.erase(LVAttributeKind::Linkage); }
  /// Return whether debug-location attributes are shown.
  /// \returns True when debug-location attributes are shown.
  bool getAttributeLocation() const {
    return Attribute.Kinds.find(LVAttributeKind::Location) != Attribute.Kinds.end();
  }
  /// Enable showing debug-location attributes.
  void setAttributeLocation() { Attribute.Kinds.insert(LVAttributeKind::Location); }
  /// Disable showing debug-location attributes.
  void resetAttributeLocation() { Attribute.Kinds.erase(LVAttributeKind::Location); }
  /// Return whether local-symbol attributes are shown.
  /// \returns True when local-symbol attributes are shown.
  bool getAttributeLocal() const {
    return Attribute.Kinds.find(LVAttributeKind::Local) != Attribute.Kinds.end();
  }
  /// Enable showing local-symbol attributes.
  void setAttributeLocal() { Attribute.Kinds.insert(LVAttributeKind::Local); }
  /// Disable showing local-symbol attributes.
  void resetAttributeLocal() { Attribute.Kinds.erase(LVAttributeKind::Local); }
  /// Return whether DIE-offset attributes are shown.
  /// \returns True when DIE-offset attributes are shown.
  bool getAttributeOffset() const {
    return Attribute.Kinds.find(LVAttributeKind::Offset) != Attribute.Kinds.end();
  }
  /// Enable showing DIE-offset attributes.
  void setAttributeOffset() { Attribute.Kinds.insert(LVAttributeKind::Offset); }
  /// Disable showing DIE-offset attributes.
  void resetAttributeOffset() { Attribute.Kinds.erase(LVAttributeKind::Offset); }
  /// Return whether full-pathname attributes are shown.
  /// \returns True when full-pathname attributes are shown.
  bool getAttributePathname() const {
    return Attribute.Kinds.find(LVAttributeKind::Pathname) != Attribute.Kinds.end();
  }
  /// Enable showing full-pathname attributes.
  void setAttributePathname() { Attribute.Kinds.insert(LVAttributeKind::Pathname); }
  /// Disable showing full-pathname attributes.
  void resetAttributePathname() { Attribute.Kinds.erase(LVAttributeKind::Pathname); }
  /// Return whether producer-string attributes are shown.
  /// \returns True when producer-string attributes are shown.
  bool getAttributeProducer() const {
    return Attribute.Kinds.find(LVAttributeKind::Producer) != Attribute.Kinds.end();
  }
  /// Enable showing producer-string attributes.
  void setAttributeProducer() { Attribute.Kinds.insert(LVAttributeKind::Producer); }
  /// Disable showing producer-string attributes.
  void resetAttributeProducer() { Attribute.Kinds.erase(LVAttributeKind::Producer); }
  /// Return whether public-symbol attributes are shown.
  /// \returns True when public-symbol attributes are shown.
  bool getAttributePublics() const {
    return Attribute.Kinds.find(LVAttributeKind::Publics) != Attribute.Kinds.end();
  }
  /// Enable showing public-symbol attributes.
  void setAttributePublics() { Attribute.Kinds.insert(LVAttributeKind::Publics); }
  /// Disable showing public-symbol attributes.
  void resetAttributePublics() { Attribute.Kinds.erase(LVAttributeKind::Publics); }
  /// Return whether fully-qualified-name attributes are shown.
  /// \returns True when fully-qualified-name attributes are shown.
  bool getAttributeQualified() const {
    return Attribute.Kinds.find(LVAttributeKind::Qualified) != Attribute.Kinds.end();
  }
  /// Enable showing fully-qualified-name attributes.
  void setAttributeQualified() { Attribute.Kinds.insert(LVAttributeKind::Qualified); }
  /// Disable showing fully-qualified-name attributes.
  void resetAttributeQualified() { Attribute.Kinds.erase(LVAttributeKind::Qualified); }
  /// Return whether type-qualifier attributes are shown.
  /// \returns True when type-qualifier attributes are shown.
  bool getAttributeQualifier() const {
    return Attribute.Kinds.find(LVAttributeKind::Qualifier) != Attribute.Kinds.end();
  }
  /// Enable showing type-qualifier attributes.
  void setAttributeQualifier() { Attribute.Kinds.insert(LVAttributeKind::Qualifier); }
  /// Disable showing type-qualifier attributes.
  void resetAttributeQualifier() { Attribute.Kinds.erase(LVAttributeKind::Qualifier); }
  /// Return whether address-range attributes are shown.
  /// \returns True when address-range attributes are shown.
  bool getAttributeRange() const {
    return Attribute.Kinds.find(LVAttributeKind::Range) != Attribute.Kinds.end();
  }
  /// Enable showing address-range attributes.
  void setAttributeRange() { Attribute.Kinds.insert(LVAttributeKind::Range); }
  /// Disable showing address-range attributes.
  void resetAttributeRange() { Attribute.Kinds.erase(LVAttributeKind::Range); }
  /// Return whether reference attributes are shown.
  /// \returns True when reference attributes are shown.
  bool getAttributeReference() const {
    return Attribute.Kinds.find(LVAttributeKind::Reference) != Attribute.Kinds.end();
  }
  /// Enable showing reference attributes.
  void setAttributeReference() { Attribute.Kinds.insert(LVAttributeKind::Reference); }
  /// Disable showing reference attributes.
  void resetAttributeReference() { Attribute.Kinds.erase(LVAttributeKind::Reference); }
  /// Return whether register-location attributes are shown.
  /// \returns True when register-location attributes are shown.
  bool getAttributeRegister() const {
    return Attribute.Kinds.find(LVAttributeKind::Register) != Attribute.Kinds.end();
  }
  /// Enable showing register-location attributes.
  void setAttributeRegister() { Attribute.Kinds.insert(LVAttributeKind::Register); }
  /// Disable showing register-location attributes.
  void resetAttributeRegister() { Attribute.Kinds.erase(LVAttributeKind::Register); }
  /// Return whether size attributes are shown.
  /// \returns True when size attributes are shown.
  bool getAttributeSize() const {
    return Attribute.Kinds.find(LVAttributeKind::Size) != Attribute.Kinds.end();
  }
  /// Enable showing size attributes.
  void setAttributeSize() { Attribute.Kinds.insert(LVAttributeKind::Size); }
  /// Disable showing size attributes.
  void resetAttributeSize() { Attribute.Kinds.erase(LVAttributeKind::Size); }
  /// Return whether standard attributes are shown.
  /// \returns True when standard attributes are shown.
  bool getAttributeStandard() const {
    return Attribute.Kinds.find(LVAttributeKind::Standard) != Attribute.Kinds.end();
  }
  /// Enable showing standard attributes.
  void setAttributeStandard() { Attribute.Kinds.insert(LVAttributeKind::Standard); }
  /// Disable showing standard attributes.
  void resetAttributeStandard() { Attribute.Kinds.erase(LVAttributeKind::Standard); }
  /// Return whether array-subrange attributes are shown.
  /// \returns True when array-subrange attributes are shown.
  bool getAttributeSubrange() const {
    return Attribute.Kinds.find(LVAttributeKind::Subrange) != Attribute.Kinds.end();
  }
  /// Enable showing array-subrange attributes.
  void setAttributeSubrange() { Attribute.Kinds.insert(LVAttributeKind::Subrange); }
  /// Disable showing array-subrange attributes.
  void resetAttributeSubrange() { Attribute.Kinds.erase(LVAttributeKind::Subrange); }
  /// Return whether system attributes are shown.
  /// \returns True when system attributes are shown.
  bool getAttributeSystem() const {
    return Attribute.Kinds.find(LVAttributeKind::System) != Attribute.Kinds.end();
  }
  /// Enable showing system attributes.
  void setAttributeSystem() { Attribute.Kinds.insert(LVAttributeKind::System); }
  /// Disable showing system attributes.
  void resetAttributeSystem() { Attribute.Kinds.erase(LVAttributeKind::System); }
  /// Return whether type-name attributes are shown.
  /// \returns True when type-name attributes are shown.
  bool getAttributeTypename() const {
    return Attribute.Kinds.find(LVAttributeKind::Typename) != Attribute.Kinds.end();
  }
  /// Enable showing type-name attributes.
  void setAttributeTypename() { Attribute.Kinds.insert(LVAttributeKind::Typename); }
  /// Disable showing type-name attributes.
  void resetAttributeTypename() { Attribute.Kinds.erase(LVAttributeKind::Typename); }
  /// Return whether underlying-type attributes are shown.
  /// \returns True when underlying-type attributes are shown.
  bool getAttributeUnderlying() const {
    return Attribute.Kinds.find(LVAttributeKind::Underlying) != Attribute.Kinds.end();
  }
  /// Enable showing underlying-type attributes.
  void setAttributeUnderlying() { Attribute.Kinds.insert(LVAttributeKind::Underlying); }
  /// Disable showing underlying-type attributes.
  void resetAttributeUnderlying() { Attribute.Kinds.erase(LVAttributeKind::Underlying); }
  /// Return whether zero-value attributes are shown.
  /// \returns True when zero-value attributes are shown.
  bool getAttributeZero() const {
    return Attribute.Kinds.find(LVAttributeKind::Zero) != Attribute.Kinds.end();
  }
  /// Enable showing zero-value attributes.
  void setAttributeZero() { Attribute.Kinds.insert(LVAttributeKind::Zero); }
  /// Disable showing zero-value attributes.
  void resetAttributeZero() { Attribute.Kinds.erase(LVAttributeKind::Zero); }
  /// Return whether elements added during comparison are highlighted.
  /// \returns True when elements added during comparison are highlighted.
  bool getAttributeAdded() const { return Attribute.Added; }
  /// Enable highlighting of elements added during comparison.
  void setAttributeAdded() { Attribute.Added = true; }
  /// Disable highlighting of elements added during comparison.
  void resetAttributeAdded() { Attribute.Added = false; }
  /// Return whether any location-related attribute display is enabled.
  /// \returns True when any location-related attribute display is enabled.
  bool getAttributeAnyLocation() const { return Attribute.AnyLocation; }
  /// Enable any location-related attribute display.
  void setAttributeAnyLocation() { Attribute.AnyLocation = true; }
  /// Disable any location-related attribute display.
  void resetAttributeAnyLocation() { Attribute.AnyLocation = false; }
  /// Return whether any source-related attribute display is enabled.
  /// \returns True when any source-related attribute display is enabled.
  bool getAttributeAnySource() const { return Attribute.AnySource; }
  /// Enable any source-related attribute display.
  void setAttributeAnySource() { Attribute.AnySource = true; }
  /// Disable any source-related attribute display.
  void resetAttributeAnySource() { Attribute.AnySource = false; }
  /// Return whether elements missing during comparison are highlighted.
  /// \returns True when elements missing during comparison are highlighted.
  bool getAttributeMissing() const { return Attribute.Missing; }
  /// Enable highlighting of elements missing during comparison.
  void setAttributeMissing() { Attribute.Missing = true; }
  /// Disable highlighting of elements missing during comparison.
  void resetAttributeMissing() { Attribute.Missing = false; }

  // --compare.
  /// Return whether all compare element kinds are selected.
  /// \returns True when all compare element kinds are selected.
  bool getCompareAll() const {
    return Compare.Elements.find(LVCompareKind::All) != Compare.Elements.end();
  }
  /// Enable all compare element kinds.
  void setCompareAll() { Compare.Elements.insert(LVCompareKind::All); }
  /// Disable all compare element kinds.
  void resetCompareAll() { Compare.Elements.erase(LVCompareKind::All); }
  /// Return whether line comparison is enabled.
  /// \returns True when line comparison is enabled.
  bool getCompareLines() const {
    return Compare.Elements.find(LVCompareKind::Lines) != Compare.Elements.end();
  }
  /// Enable line comparison.
  void setCompareLines() { Compare.Elements.insert(LVCompareKind::Lines); }
  /// Disable line comparison.
  void resetCompareLines() { Compare.Elements.erase(LVCompareKind::Lines); }
  /// Return whether scope comparison is enabled.
  /// \returns True when scope comparison is enabled.
  bool getCompareScopes() const {
    return Compare.Elements.find(LVCompareKind::Scopes) != Compare.Elements.end();
  }
  /// Enable scope comparison.
  void setCompareScopes() { Compare.Elements.insert(LVCompareKind::Scopes); }
  /// Disable scope comparison.
  void resetCompareScopes() { Compare.Elements.erase(LVCompareKind::Scopes); }
  /// Return whether symbol comparison is enabled.
  /// \returns True when symbol comparison is enabled.
  bool getCompareSymbols() const {
    return Compare.Elements.find(LVCompareKind::Symbols) != Compare.Elements.end();
  }
  /// Enable symbol comparison.
  void setCompareSymbols() { Compare.Elements.insert(LVCompareKind::Symbols); }
  /// Disable symbol comparison.
  void resetCompareSymbols() { Compare.Elements.erase(LVCompareKind::Symbols); }
  /// Return whether type comparison is enabled.
  /// \returns True when type comparison is enabled.
  bool getCompareTypes() const {
    return Compare.Elements.find(LVCompareKind::Types) != Compare.Elements.end();
  }
  /// Enable type comparison.
  void setCompareTypes() { Compare.Elements.insert(LVCompareKind::Types); }
  /// Disable type comparison.
  void resetCompareTypes() { Compare.Elements.erase(LVCompareKind::Types); }
  /// Return whether compare-context mode is enabled.
  /// \returns True when compare-context mode is enabled.
  bool getCompareContext() const { return Compare.Context; }
  /// Enable compare-context mode.
  void setCompareContext() { Compare.Context = true; }
  /// Disable compare-context mode.
  void resetCompareContext() { Compare.Context = false; }
  /// Return whether a comparison run has been requested.
  /// \returns True when a comparison run has been requested.
  bool getCompareExecute() const { return Compare.Execute; }
  /// Enable the comparison-execute request.
  void setCompareExecute() { Compare.Execute = true; }
  /// Disable the comparison-execute request.
  void resetCompareExecute() { Compare.Execute = false; }
  /// Return whether printing during comparison is enabled.
  /// \returns True when printing during comparison is enabled.
  bool getComparePrint() const { return Compare.Print; }
  /// Enable printing during comparison.
  void setComparePrint() { Compare.Print = true; }
  /// Disable printing during comparison.
  void resetComparePrint() { Compare.Print = false; }

  // --output.
  /// Return whether all output formats are selected.
  /// \returns True when all output formats are selected.
  bool getOutputAll() const {
    return Output.Kinds.find(LVOutputKind::All) != Output.Kinds.end();
  }
  /// Enable all output formats.
  void setOutputAll() { Output.Kinds.insert(LVOutputKind::All); }
  /// Disable all output formats.
  void resetOutputAll() { Output.Kinds.erase(LVOutputKind::All); }
  /// Return whether split output is enabled.
  /// \returns True when split output is enabled.
  bool getOutputSplit() const {
    return Output.Kinds.find(LVOutputKind::Split) != Output.Kinds.end();
  }
  /// Enable split output.
  void setOutputSplit() { Output.Kinds.insert(LVOutputKind::Split); }
  /// Disable split output.
  void resetOutputSplit() { Output.Kinds.erase(LVOutputKind::Split); }
  /// Return whether text output is enabled.
  /// \returns True when text output is enabled.
  bool getOutputText() const {
    return Output.Kinds.find(LVOutputKind::Text) != Output.Kinds.end();
  }
  /// Enable text output.
  void setOutputText() { Output.Kinds.insert(LVOutputKind::Text); }
  /// Disable text output.
  void resetOutputText() { Output.Kinds.erase(LVOutputKind::Text); }
  /// Return whether JSON output is enabled.
  /// \returns True when JSON output is enabled.
  bool getOutputJson() const {
    return Output.Kinds.find(LVOutputKind::Json) != Output.Kinds.end();
  }
  /// Enable JSON output.
  void setOutputJson() { Output.Kinds.insert(LVOutputKind::Json); }
  /// Disable JSON output.
  void resetOutputJson() { Output.Kinds.erase(LVOutputKind::Json); }
  /// Return the configured output folder path.
  /// \returns Configured output folder path.
  std::string getOutputFolder() const { return Output.Folder; }
  /// Set the output folder path to \p Folder.
  /// \param Folder Destination directory for split or file output.
  void setOutputFolder(std::string Folder) {
    Output.Folder = std::move(Folder);
  }
  /// Clear the configured output folder path.
  void resetOutputFolder() { Output.Folder = ""; }

  /// Return the maximum output nesting level, or \c -1U if unset.
  /// \returns Maximum output nesting level, or \c -1U if unset.
  unsigned getOutputLevel() const { return Output.Level; }
  /// Set the maximum output nesting level to \p Value.
  /// \param Value Nesting level limit for printed output.
  void setOutputLevel(unsigned Value) { Output.Level = Value; }
  /// Reset the maximum output nesting level to unset (\c -1U).
  void resetOutputLevel() { Output.Level = -1U; }

  /// Return the sort mode used when emitting output.
  /// \returns Sort mode used when emitting output.
  LVSortMode getSortMode() const { return Output.SortMode; }
  /// Set the sort mode used when emitting output.
  /// \param SortMode Sort mode applied to printed logical-view objects.
  void setSortMode(LVSortMode SortMode) { Output.SortMode = SortMode; }

  // --print.
  /// Return whether all print categories are selected.
  /// \returns True when all print categories are selected.
  bool getPrintAll() const {
    return Print.Kinds.find(LVPrintKind::All) != Print.Kinds.end();
  }
  /// Enable all print categories.
  void setPrintAll() { Print.Kinds.insert(LVPrintKind::All); }
  /// Disable all print categories.
  void resetPrintAll() { Print.Kinds.erase(LVPrintKind::All); }
  /// Return whether printing of logical elements is enabled.
  /// \returns True when printing of logical elements is enabled.
  bool getPrintElements() const {
    return Print.Kinds.find(LVPrintKind::Elements) != Print.Kinds.end();
  }
  /// Enable printing of logical elements.
  void setPrintElements() { Print.Kinds.insert(LVPrintKind::Elements); }
  /// Disable printing of logical elements.
  void resetPrintElements() { Print.Kinds.erase(LVPrintKind::Elements); }
  /// Return whether printing of instructions is enabled.
  /// \returns True when printing of instructions is enabled.
  bool getPrintInstructions() const {
    return Print.Kinds.find(LVPrintKind::Instructions) != Print.Kinds.end();
  }
  /// Enable printing of instructions.
  void setPrintInstructions() { Print.Kinds.insert(LVPrintKind::Instructions); }
  /// Disable printing of instructions.
  void resetPrintInstructions() { Print.Kinds.erase(LVPrintKind::Instructions); }
  /// Return whether printing of lines is enabled.
  /// \returns True when printing of lines is enabled.
  bool getPrintLines() const {
    return Print.Kinds.find(LVPrintKind::Lines) != Print.Kinds.end();
  }
  /// Enable printing of lines.
  void setPrintLines() { Print.Kinds.insert(LVPrintKind::Lines); }
  /// Disable printing of lines.
  void resetPrintLines() { Print.Kinds.erase(LVPrintKind::Lines); }
  /// Return whether printing of scopes is enabled.
  /// \returns True when printing of scopes is enabled.
  bool getPrintScopes() const {
    return Print.Kinds.find(LVPrintKind::Scopes) != Print.Kinds.end();
  }
  /// Enable printing of scopes.
  void setPrintScopes() { Print.Kinds.insert(LVPrintKind::Scopes); }
  /// Disable printing of scopes.
  void resetPrintScopes() { Print.Kinds.erase(LVPrintKind::Scopes); }
  /// Return whether printing of size information is enabled.
  /// \returns True when printing of size information is enabled.
  bool getPrintSizes() const {
    return Print.Kinds.find(LVPrintKind::Sizes) != Print.Kinds.end();
  }
  /// Enable printing of size information.
  void setPrintSizes() { Print.Kinds.insert(LVPrintKind::Sizes); }
  /// Disable printing of size information.
  void resetPrintSizes() { Print.Kinds.erase(LVPrintKind::Sizes); }
  /// Return whether printing of symbols is enabled.
  /// \returns True when printing of symbols is enabled.
  bool getPrintSymbols() const {
    return Print.Kinds.find(LVPrintKind::Symbols) != Print.Kinds.end();
  }
  /// Enable printing of symbols.
  void setPrintSymbols() { Print.Kinds.insert(LVPrintKind::Symbols); }
  /// Disable printing of symbols.
  void resetPrintSymbols() { Print.Kinds.erase(LVPrintKind::Symbols); }
  /// Return whether printing of a summary is enabled.
  /// \returns True when printing of a summary is enabled.
  bool getPrintSummary() const {
    return Print.Kinds.find(LVPrintKind::Summary) != Print.Kinds.end();
  }
  /// Enable printing of a summary.
  void setPrintSummary() { Print.Kinds.insert(LVPrintKind::Summary); }
  /// Disable printing of a summary.
  void resetPrintSummary() { Print.Kinds.erase(LVPrintKind::Summary); }
  /// Return whether printing of types is enabled.
  /// \returns True when printing of types is enabled.
  bool getPrintTypes() const {
    return Print.Kinds.find(LVPrintKind::Types) != Print.Kinds.end();
  }
  /// Enable printing of types.
  void setPrintTypes() { Print.Kinds.insert(LVPrintKind::Types); }
  /// Disable printing of types.
  void resetPrintTypes() { Print.Kinds.erase(LVPrintKind::Types); }
  /// Return whether printing of warnings is enabled.
  /// \returns True when printing of warnings is enabled.
  bool getPrintWarnings() const {
    return Print.Kinds.find(LVPrintKind::Warnings) != Print.Kinds.end();
  }
  /// Enable printing of warnings.
  void setPrintWarnings() { Print.Kinds.insert(LVPrintKind::Warnings); }
  /// Disable printing of warnings.
  void resetPrintWarnings() { Print.Kinds.erase(LVPrintKind::Warnings); }
  /// Return whether printing of any element category is requested.
  /// \returns True when printing of any element category is requested.
  bool getPrintAnyElement() const { return Print.AnyElement; }
  /// Enable the any-element print request.
  void setPrintAnyElement() { Print.AnyElement = true; }
  /// Disable the any-element print request.
  void resetPrintAnyElement() { Print.AnyElement = false; }
  /// Return whether printing of lines or instructions is requested.
  /// \returns True when printing of lines or instructions is requested.
  bool getPrintAnyLine() const { return Print.AnyLine; }
  /// Enable the any-line print request.
  void setPrintAnyLine() { Print.AnyLine = true; }
  /// Disable the any-line print request.
  void resetPrintAnyLine() { Print.AnyLine = false; }
  /// Return whether a print run has been requested.
  /// \returns True when a print run has been requested.
  bool getPrintExecute() const { return Print.Execute; }
  /// Enable the print-execute request.
  void setPrintExecute() { Print.Execute = true; }
  /// Disable the print-execute request.
  void resetPrintExecute() { Print.Execute = false; }
  /// Return whether print formatting is enabled.
  /// \returns True when print formatting is enabled.
  bool getPrintFormatting() const { return Print.Formatting; }
  /// Enable print formatting.
  void setPrintFormatting() { Print.Formatting = true; }
  /// Disable print formatting.
  void resetPrintFormatting() { Print.Formatting = false; }
  /// Return whether offset printing with formatting disabled is enabled.
  /// \returns True when offset printing with formatting disabled is enabled.
  bool getPrintOffset() const { return Print.Offset; }
  /// Enable offset printing with formatting disabled.
  void setPrintOffset() { Print.Offset = true; }
  /// Disable offset printing with formatting disabled.
  void resetPrintOffset() { Print.Offset = false; }
  /// Return whether printing of sizes or a summary is requested.
  /// \returns True when printing of sizes or a summary is requested.
  bool getPrintSizesSummary() const { return Print.SizesSummary; }
  /// Enable the sizes-or-summary print request.
  void setPrintSizesSummary() { Print.SizesSummary = true; }
  /// Disable the sizes-or-summary print request.
  void resetPrintSizesSummary() { Print.SizesSummary = false; }

  // --report.
  /// Return whether all report layouts are selected.
  /// \returns True when all report layouts are selected.
  bool getReportAll() const {
    return Report.Kinds.find(LVReportKind::All) != Report.Kinds.end();
  }
  /// Enable all report layouts.
  void setReportAll() { Report.Kinds.insert(LVReportKind::All); }
  /// Disable all report layouts.
  void resetReportAll() { Report.Kinds.erase(LVReportKind::All); }
  /// Return whether children are included in reports.
  /// \returns True when children are included in reports.
  bool getReportChildren() const {
    return Report.Kinds.find(LVReportKind::Children) != Report.Kinds.end();
  }
  /// Enable including children in reports.
  void setReportChildren() { Report.Kinds.insert(LVReportKind::Children); }
  /// Disable including children in reports.
  void resetReportChildren() { Report.Kinds.erase(LVReportKind::Children); }
  /// Return whether list reporting is enabled.
  /// \returns True when list reporting is enabled.
  bool getReportList() const {
    return Report.Kinds.find(LVReportKind::List) != Report.Kinds.end();
  }
  /// Enable list reporting.
  void setReportList() { Report.Kinds.insert(LVReportKind::List); }
  /// Disable list reporting.
  void resetReportList() { Report.Kinds.erase(LVReportKind::List); }
  /// Return whether parents are included in reports.
  /// \returns True when parents are included in reports.
  bool getReportParents() const {
    return Report.Kinds.find(LVReportKind::Parents) != Report.Kinds.end();
  }
  /// Enable including parents in reports.
  void setReportParents() { Report.Kinds.insert(LVReportKind::Parents); }
  /// Disable including parents in reports.
  void resetReportParents() { Report.Kinds.erase(LVReportKind::Parents); }
  /// Return whether view reporting is enabled.
  /// \returns True when view reporting is enabled.
  bool getReportView() const {
    return Report.Kinds.find(LVReportKind::View) != Report.Kinds.end();
  }
  /// Enable view reporting.
  void setReportView() { Report.Kinds.insert(LVReportKind::View); }
  /// Disable view reporting.
  void resetReportView() { Report.Kinds.erase(LVReportKind::View); }
  /// Return whether any view-related report layout is enabled.
  /// \returns True when any view-related report layout is enabled.
  bool getReportAnyView() const { return Report.AnyView; }
  /// Enable any view-related report layout.
  void setReportAnyView() { Report.AnyView = true; }
  /// Disable any view-related report layout.
  void resetReportAnyView() { Report.AnyView = false; }
  /// Return whether a report run has been requested.
  /// \returns True when a report run has been requested.
  bool getReportExecute() const { return Report.Execute; }
  /// Enable the report-execute request.
  void setReportExecute() { Report.Execute = true; }
  /// Disable the report-execute request.
  void resetReportExecute() { Report.Execute = false; }

  // --select.
  /// Return whether case-insensitive selection matching is enabled.
  /// \returns True when case-insensitive selection matching is enabled.
  bool getSelectIgnoreCase() const { return Select.IgnoreCase; }
  /// Enable case-insensitive selection matching.
  void setSelectIgnoreCase() { Select.IgnoreCase = true; }
  /// Disable case-insensitive selection matching.
  void resetSelectIgnoreCase() { Select.IgnoreCase = false; }
  /// Return whether regular-expression selection matching is enabled.
  /// \returns True when regular-expression selection matching is enabled.
  bool getSelectUseRegex() const { return Select.UseRegex; }
  /// Enable regular-expression selection matching.
  void setSelectUseRegex() { Select.UseRegex = true; }
  /// Disable regular-expression selection matching.
  void resetSelectUseRegex() { Select.UseRegex = false; }
  /// Return whether a selection run has been requested.
  /// \returns True when a selection run has been requested.
  bool getSelectExecute() const { return Select.Execute; }
  /// Enable the select-execute request.
  void setSelectExecute() { Select.Execute = true; }
  /// Disable the select-execute request.
  void resetSelectExecute() { Select.Execute = false; }
  /// Return whether generic element-kind selection is active.
  /// \returns True when generic element-kind selection is active.
  bool getSelectGenericKind() const { return Select.GenericKind; }
  /// Enable generic element-kind selection.
  void setSelectGenericKind() { Select.GenericKind = true; }
  /// Disable generic element-kind selection.
  void resetSelectGenericKind() { Select.GenericKind = false; }
  /// Return whether generic string-pattern selection is active.
  /// \returns True when generic string-pattern selection is active.
  bool getSelectGenericPattern() const { return Select.GenericPattern; }
  /// Enable generic string-pattern selection.
  void setSelectGenericPattern() { Select.GenericPattern = true; }
  /// Disable generic string-pattern selection.
  void resetSelectGenericPattern() { Select.GenericPattern = false; }
  /// Return whether offset-pattern selection is active.
  /// \returns True when offset-pattern selection is active.
  bool getSelectOffsetPattern() const { return Select.OffsetPattern; }
  /// Enable offset-pattern selection.
  void setSelectOffsetPattern() { Select.OffsetPattern = true; }
  /// Disable offset-pattern selection.
  void resetSelectOffsetPattern() { Select.OffsetPattern = false; }

  // --warning.
  /// Return whether all warning categories are selected.
  /// \returns True when all warning categories are selected.
  bool getWarningAll() const {
    return Warning.Kinds.find(LVWarningKind::All) != Warning.Kinds.end();
  }
  /// Enable all warning categories.
  void setWarningAll() { Warning.Kinds.insert(LVWarningKind::All); }
  /// Disable all warning categories.
  void resetWarningAll() { Warning.Kinds.erase(LVWarningKind::All); }
  /// Return whether coverage warnings are enabled.
  /// \returns True when coverage warnings are enabled.
  bool getWarningCoverages() const {
    return Warning.Kinds.find(LVWarningKind::Coverages) != Warning.Kinds.end();
  }
  /// Enable coverage warnings.
  void setWarningCoverages() { Warning.Kinds.insert(LVWarningKind::Coverages); }
  /// Disable coverage warnings.
  void resetWarningCoverages() { Warning.Kinds.erase(LVWarningKind::Coverages); }
  /// Return whether line warnings are enabled.
  /// \returns True when line warnings are enabled.
  bool getWarningLines() const {
    return Warning.Kinds.find(LVWarningKind::Lines) != Warning.Kinds.end();
  }
  /// Enable line warnings.
  void setWarningLines() { Warning.Kinds.insert(LVWarningKind::Lines); }
  /// Disable line warnings.
  void resetWarningLines() { Warning.Kinds.erase(LVWarningKind::Lines); }
  /// Return whether location warnings are enabled.
  /// \returns True when location warnings are enabled.
  bool getWarningLocations() const {
    return Warning.Kinds.find(LVWarningKind::Locations) != Warning.Kinds.end();
  }
  /// Enable location warnings.
  void setWarningLocations() { Warning.Kinds.insert(LVWarningKind::Locations); }
  /// Disable location warnings.
  void resetWarningLocations() { Warning.Kinds.erase(LVWarningKind::Locations); }
  /// Return whether range warnings are enabled.
  /// \returns True when range warnings are enabled.
  bool getWarningRanges() const {
    return Warning.Kinds.find(LVWarningKind::Ranges) != Warning.Kinds.end();
  }
  /// Enable range warnings.
  void setWarningRanges() { Warning.Kinds.insert(LVWarningKind::Ranges); }
  /// Disable range warnings.
  void resetWarningRanges() { Warning.Kinds.erase(LVWarningKind::Ranges); }

  // --internal.
  /// Return whether all internal diagnostics are selected.
  /// \returns True when all internal diagnostics are selected.
  bool getInternalAll() const {
    return Internal.Kinds.find(LVInternalKind::All) != Internal.Kinds.end();
  }
  /// Enable all internal diagnostics.
  void setInternalAll() { Internal.Kinds.insert(LVInternalKind::All); }
  /// Disable all internal diagnostics.
  void resetInternalAll() { Internal.Kinds.erase(LVInternalKind::All); }
  /// Return whether command-line dumping is enabled.
  /// \returns True when command-line dumping is enabled.
  bool getInternalCmdline() const {
    return Internal.Kinds.find(LVInternalKind::Cmdline) != Internal.Kinds.end();
  }
  /// Enable command-line dumping.
  void setInternalCmdline() { Internal.Kinds.insert(LVInternalKind::Cmdline); }
  /// Disable command-line dumping.
  void resetInternalCmdline() { Internal.Kinds.erase(LVInternalKind::Cmdline); }
  /// Return whether unique-ID dumping is enabled.
  /// \returns True when unique-ID dumping is enabled.
  bool getInternalID() const {
    return Internal.Kinds.find(LVInternalKind::ID) != Internal.Kinds.end();
  }
  /// Enable unique-ID dumping.
  void setInternalID() { Internal.Kinds.insert(LVInternalKind::ID); }
  /// Disable unique-ID dumping.
  void resetInternalID() { Internal.Kinds.erase(LVInternalKind::ID); }
  /// Return whether integrity checking is enabled.
  /// \returns True when integrity checking is enabled.
  bool getInternalIntegrity() const {
    return Internal.Kinds.find(LVInternalKind::Integrity) != Internal.Kinds.end();
  }
  /// Enable integrity checking.
  void setInternalIntegrity() { Internal.Kinds.insert(LVInternalKind::Integrity); }
  /// Disable integrity checking.
  void resetInternalIntegrity() { Internal.Kinds.erase(LVInternalKind::Integrity); }
  /// Return whether the internal-none setting is selected.
  /// \returns True when the internal-none setting is selected.
  bool getInternalNone() const {
    return Internal.Kinds.find(LVInternalKind::None) != Internal.Kinds.end();
  }
  /// Enable the internal-none setting.
  void setInternalNone() { Internal.Kinds.insert(LVInternalKind::None); }
  /// Disable the internal-none setting.
  void resetInternalNone() { Internal.Kinds.erase(LVInternalKind::None); }
  /// Return whether DWARF-tag dumping is enabled.
  /// \returns True when DWARF-tag dumping is enabled.
  bool getInternalTag() const {
    return Internal.Kinds.find(LVInternalKind::Tag) != Internal.Kinds.end();
  }
  /// Enable DWARF-tag dumping.
  void setInternalTag() { Internal.Kinds.insert(LVInternalKind::Tag); }
  /// Disable DWARF-tag dumping.
  void resetInternalTag() { Internal.Kinds.erase(LVInternalKind::Tag); }

  // General shortcuts to some combinations.
  /// Return whether collecting address-range information is enabled.
  /// \returns True when collecting address-range information is enabled.
  bool getGeneralCollectRanges() const { return General.CollectRanges; }
  /// Enable collecting address-range information.
  void setGeneralCollectRanges() { General.CollectRanges = true; }
  /// Disable collecting address-range information.
  void resetGeneralCollectRanges() { General.CollectRanges = false; }

  /// Print the recorded options to \p OS.
  /// \param OS Stream that receives the printed options.
  LLVM_ABI void print(raw_ostream &OS) const;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Dump the recorded options to the debug stream.
  void dump() const { print(dbgs()); }
#endif
};

/// Return a reference to the process-wide logical-view options.
/// \returns Reference to the process-wide logical-view options.
inline LVOptions &options() { return (*LVOptions::getOptions()); }
/// Install \p Options as the process-wide logical-view options.
/// \param Options Options object to install; ownership is not transferred.
inline void setOptions(LVOptions *Options) { LVOptions::setOptions(Options); }

/// Collects and applies selection patterns for logical-view elements.
class LVPatterns final {
  // Pattern Mode.
  enum class LVMatchMode {
    None = 0, // No given pattern.
    Match,    // Perfect match.
    NoCase,   // Ignore case.
    Regex     // Regular expression.
  };

  // Keep the search pattern information.
  struct LVMatch {
    std::string Pattern;                  // Normal pattern.
    std::shared_ptr<Regex> RE;            // Regular Expression Pattern.
    LVMatchMode Mode = LVMatchMode::None; // Match mode.
  };

  using LVMatchInfo = std::vector<LVMatch>;
  LVMatchInfo GenericMatchInfo;
  using LVMatchOffsets = std::vector<uint64_t>;
  LVMatchOffsets OffsetMatchInfo;

  // Element selection.
  LVElementDispatch ElementDispatch;
  LVLineDispatch LineDispatch;
  LVScopeDispatch ScopeDispatch;
  LVSymbolDispatch SymbolDispatch;
  LVTypeDispatch TypeDispatch;

  // Element selection request.
  LVElementRequest ElementRequest;
  LVLineRequest LineRequest;
  LVScopeRequest ScopeRequest;
  LVSymbolRequest SymbolRequest;
  LVTypeRequest TypeRequest;

  // Check an element printing Request.
  template <typename T, typename U>
  bool checkElementRequest(const T *Element, const U &Requests) const {
    assert(Element && "Element must not be nullptr");
    for (const auto &Request : Requests)
      if ((Element->*Request)())
        return true;
    // Check generic element requests.
    for (const LVElementGetFunction &Request : ElementRequest)
      if ((Element->*Request)())
        return true;
    return false;
  }

  // Add an element printing request based on its kind.
  template <typename T, typename U, typename V>
  void addRequest(const T &Selection, const U &Dispatch, V &Request) const {
    for (const auto &Entry : Selection) {
      // Find target function to fullfit request.
      typename U::const_iterator Iter = Dispatch.find(Entry);
      if (Iter != Dispatch.end())
        Request.push_back(Iter->second);
    }
  }

  LLVM_ABI void addElement(LVElement *Element);

  template <typename T, typename U>
  void resolveGenericPatternMatch(T *Element, const U &Requests) {
    assert(Element && "Element must not be nullptr");
    auto CheckPattern = [this, Element]() -> bool {
      return (Element->isNamed() &&
              (matchGenericPattern(Element->getName()) ||
               matchGenericPattern(Element->getLinkageName()))) ||
             (Element->isTyped() &&
              matchGenericPattern(Element->getTypeName()));
    };
    auto CheckOffset = [this, Element]() -> bool {
      return matchOffsetPattern(Element->getOffset());
    };
    if ((options().getSelectGenericPattern() && CheckPattern()) ||
        (options().getSelectOffsetPattern() && CheckOffset()) ||
        ((Requests.size() || ElementRequest.size()) &&
         checkElementRequest(Element, Requests)))
      addElement(Element);
  }

  template <typename U>
  void resolveGenericPatternMatch(LVLine *Line, const U &Requests) {
    assert(Line && "Line must not be nullptr");
    auto CheckPattern = [this, Line]() -> bool {
      return matchGenericPattern(Line->lineNumberAsStringStripped()) ||
             matchGenericPattern(Line->getName()) ||
             matchGenericPattern(Line->getPathname());
    };
    auto CheckOffset = [this, Line]() -> bool {
      return matchOffsetPattern(Line->getAddress());
    };
    if ((options().getSelectGenericPattern() && CheckPattern()) ||
        (options().getSelectOffsetPattern() && CheckOffset()) ||
        (Requests.size() && checkElementRequest(Line, Requests)))
      addElement(Line);
  }

  Error createMatchEntry(LVMatchInfo &Filters, StringRef Pattern,
                         bool IgnoreCase, bool UseRegex);

public:
  /// Return the process-wide selection-patterns instance.
  /// \returns Pointer to the process-wide selection-patterns instance.
  LLVM_ABI static LVPatterns *getPatterns();

  /// Construct patterns and initialize element-kind dispatch tables.
  LVPatterns() {
    ElementDispatch = LVElement::getDispatch();
    LineDispatch = LVLine::getDispatch();
    ScopeDispatch = LVScope::getDispatch();
    SymbolDispatch = LVSymbol::getDispatch();
    TypeDispatch = LVType::getDispatch();
  }
  /// Copy construction is not allowed.
  /// \param Other Unused source patterns instance.
  LVPatterns(const LVPatterns &Other) = delete;
  /// Copy assignment is not allowed.
  /// \param Other Unused source patterns instance.
  LVPatterns &operator=(const LVPatterns &Other) = delete;
  /// Destroy the patterns object.
  ~LVPatterns() = default;

  /// Clear any existing selection patterns and related option flags.
  void clear() {
    GenericMatchInfo.clear();
    OffsetMatchInfo.clear();
    ElementRequest.clear();
    LineRequest.clear();
    ScopeRequest.clear();
    SymbolRequest.clear();
    TypeRequest.clear();

    options().resetSelectGenericKind();
    options().resetSelectGenericPattern();
    options().resetSelectOffsetPattern();
  }

  /// Add element-kind selection requests from \p Selection.
  /// \param Selection Set of element kinds to select.
  void addRequest(LVElementKindSet &Selection) {
    addRequest(Selection, ElementDispatch, ElementRequest);
  }
  /// Add line-kind selection requests from \p Selection.
  /// \param Selection Set of line kinds to select.
  void addRequest(LVLineKindSet &Selection) {
    addRequest(Selection, LineDispatch, LineRequest);
  }
  /// Add scope-kind selection requests from \p Selection.
  /// \param Selection Set of scope kinds to select.
  void addRequest(LVScopeKindSet &Selection) {
    addRequest(Selection, ScopeDispatch, ScopeRequest);
  }
  /// Add symbol-kind selection requests from \p Selection.
  /// \param Selection Set of symbol kinds to select.
  void addRequest(LVSymbolKindSet &Selection) {
    addRequest(Selection, SymbolDispatch, SymbolRequest);
  }
  /// Add type-kind selection requests from \p Selection.
  /// \param Selection Set of type kinds to select.
  void addRequest(LVTypeKindSelection &Selection) {
    addRequest(Selection, TypeDispatch, TypeRequest);
  }

  /// Update report options that depend on the collected selection patterns.
  LLVM_ABI void updateReportOptions();

  /// Return whether \p Input matches any entry in \p MatchInfo.
  /// \param Input Text to test against the collected patterns.
  /// \param MatchInfo Pattern entries to match against.
  /// \returns True if \p Input matches at least one pattern.
  LLVM_ABI bool matchPattern(StringRef Input, const LVMatchInfo &MatchInfo);
  /// Return whether \p Input matches a generic \c --select pattern.
  /// \param Input Text to test against generic selection patterns.
  /// \returns True if \p Input matches a generic pattern.
  bool matchGenericPattern(StringRef Input) {
    return matchPattern(Input, GenericMatchInfo);
  }
  /// Return whether \p Offset matches a collected offset pattern.
  /// \param Offset Debug-info offset to test.
  /// \returns True if \p Offset is present in the offset patterns.
  bool matchOffsetPattern(LVOffset Offset) {
    return llvm::is_contained(OffsetMatchInfo, Offset);
  }

  /// Resolve selection patterns against \p Line and mark it if matched.
  /// \param Line Line element to test against the collected patterns.
  void resolvePatternMatch(LVLine *Line) {
    resolveGenericPatternMatch(Line, LineRequest);
  }

  /// Resolve selection patterns against \p Scope and mark it if matched.
  /// \param Scope Scope element to test against the collected patterns.
  void resolvePatternMatch(LVScope *Scope) {
    resolveGenericPatternMatch(Scope, ScopeRequest);
  }

  /// Resolve selection patterns against \p Symbol and mark it if matched.
  /// \param Symbol Symbol element to test against the collected patterns.
  void resolvePatternMatch(LVSymbol *Symbol) {
    resolveGenericPatternMatch(Symbol, SymbolRequest);
  }

  /// Resolve selection patterns against \p Type and mark it if matched.
  /// \param Type Type element to test against the collected patterns.
  void resolvePatternMatch(LVType *Type) {
    resolveGenericPatternMatch(Type, TypeRequest);
  }

  /// Compile \p Patterns into match filters stored in \p Filters.
  /// \param Patterns Source pattern strings to compile.
  /// \param Filters Destination list that receives compiled match entries.
  LLVM_ABI void addPatterns(StringSet<> &Patterns, LVMatchInfo &Filters);

  /// Add generic string selection patterns from \p Patterns.
  /// \param Patterns Pattern strings from \c --select=.
  LLVM_ABI void addGenericPatterns(StringSet<> &Patterns);
  /// Add offset selection patterns from \p Patterns.
  /// \param Patterns Offsets from \c --select-offset=.
  LLVM_ABI void addOffsetPatterns(const LVOffsetSet &Patterns);

  /// Return whether \p Line should be printed under the current options.
  /// \param Line Line element to test for printing.
  /// \returns True if \p Line should be printed.
  LLVM_ABI bool printElement(const LVLine *Line) const;
  /// Return whether \p Location should be printed under the current options.
  /// \param Location Location object to test for printing.
  /// \returns True if \p Location should be printed.
  LLVM_ABI bool printObject(const LVLocation *Location) const;
  /// Return whether \p Scope should be printed under the current options.
  /// \param Scope Scope element to test for printing.
  /// \returns True if \p Scope should be printed.
  LLVM_ABI bool printElement(const LVScope *Scope) const;
  /// Return whether \p Symbol should be printed under the current options.
  /// \param Symbol Symbol element to test for printing.
  /// \returns True if \p Symbol should be printed.
  LLVM_ABI bool printElement(const LVSymbol *Symbol) const;
  /// Return whether \p Type should be printed under the current options.
  /// \param Type Type element to test for printing.
  /// \returns True if \p Type should be printed.
  LLVM_ABI bool printElement(const LVType *Type) const;

  /// Print the collected selection patterns to \p OS.
  /// \param OS Stream that receives the printed patterns.
  LLVM_ABI void print(raw_ostream &OS) const;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Dump the collected selection patterns to the debug stream.
  void dump() const { print(dbgs()); }
#endif
};

/// Return a reference to the process-wide selection-patterns instance.
/// \returns Reference to the process-wide selection-patterns instance.
inline LVPatterns &patterns() { return *LVPatterns::getPatterns(); }

} // namespace logicalview
} // namespace llvm

#endif // LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVOPTIONS_H
