//===- OptTable.h - Option Table --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OPTION_OPTTABLE_H
#define LLVM_OPTION_OPTTABLE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringTable.h"
#include "llvm/Option/OptSpecifier.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/StringSaver.h"
#include <cassert>
#include <string>
#include <vector>

namespace llvm {

class raw_ostream;
template <typename Fn> class function_ref;

namespace opt {

class Arg;
class ArgList;
class InputArgList;
class Option;

/// Helper for overload resolution while transitioning from
/// FlagsToInclude/FlagsToExclude APIs to VisibilityMask APIs.
class Visibility {
  unsigned Mask = ~0U;

public:
  /// Construct a visibility mask from the given bit mask.
  ///
  /// \param Mask Visibility flags to include.
  explicit Visibility(unsigned Mask) : Mask(Mask) {}
  /// Construct a visibility that matches all visibility flags.
  Visibility() = default;

  /// Convert to the underlying visibility bit mask.
  ///
  /// \return The visibility flags bit mask.
  operator unsigned() const { return Mask; }
};

/// Provide access to the Option info table.
///
/// The OptTable class provides a layer of indirection which allows Option
/// instance to be created lazily. In the common case, only a few options will
/// be needed at runtime; the OptTable class maintains enough information to
/// parse command lines without instantiating Options, while letting other
/// parts of the driver still use Option instances where convenient.
class LLVM_ABI OptTable {
public:
  /// Represents a subcommand and its options in the option table.
  struct SubCommand {
    /// Subcommand name as it appears on the command line.
    const char *Name;
    /// Help text describing this subcommand.
    const char *HelpText;
    /// Usage string shown for this subcommand.
    const char *Usage;
  };

  /// Entry for a single option instance in the option data table.
  struct Info {
    /// Offset into OptTable's PrefixesTable for this option's prefixes.
    unsigned PrefixesOffset;
    /// Offset into OptTable's string table for the prefixed option name.
    StringTable::Offset PrefixedNameOffset;
    /// Generic help text for this option.
    const char *HelpText;
    /// Help text for specific visibilities.
    ///
    /// A list of pairs, where each pair is a list of visibilities and a
    /// specific help string for those visibilities. If no help text is found
    /// in this list for the visibility of the program, HelpText is used
    /// instead. This cannot use std::vector because OptTable is used in
    /// constexpr contexts. Increase the array sizes here if you need more
    /// entries and adjust the constants in
    /// OptionParserEmitter::EmitHelpTextsForVariants.
    std::array<std::pair<std::array<unsigned int, 2 /*MaxVisibilityPerHelp*/>,
                         const char *>,
               1 /*MaxVisibilityHelp*/>
        HelpTextsForVariants;
    /// Meta-variable name for option values in help text.
    const char *MetaVar;
    /// Unique option identifier.
    unsigned ID;
    /// Option kind (see Option::OptionClass).
    unsigned char Kind;
    /// Option-specific parameter (e.g. argument count for MultiArg).
    unsigned char Param;
    /// Option flags (see Option::DriverFlag and driver-specific flags).
    unsigned int Flags;
    /// Visibility flags controlling when this option is available.
    unsigned int Visibility;
    /// Identifier of the option group this option belongs to, if any.
    unsigned short GroupID;
    /// Identifier of the option this option aliases, if any.
    unsigned short AliasID;
    /// Alias argument values as a \\0-separated list, or null.
    const char *AliasArgs;
    /// Comma-separated list of acceptable values, or null.
    const char *Values;
    /// Offset into OptTable's SubCommandIDsTable.
    unsigned SubCommandIDsOffset;

  /// Return true if this option has no prefix.
  ///
  /// \return True if this option's prefix-set offset is zero.
  bool hasNoPrefix() const { return PrefixesOffset == 0; }

  /// Return the number of prefixes for this option.
  ///
  /// \param PrefixesTable Table of prefix-set offsets from the OptTable.
  /// \return The number of prefixes for this option.
  unsigned getNumPrefixes(ArrayRef<StringTable::Offset> PrefixesTable) const {
      // We embed the number of prefixes in the value of the first offset.
      return PrefixesTable[PrefixesOffset].value();
    }

  /// Return the prefix offsets for this option.
  ///
  /// \param PrefixesTable Table of prefix-set offsets from the OptTable.
  /// \return The prefix offsets, or an empty range if there is no prefix.
  ArrayRef<StringTable::Offset>
  getPrefixOffsets(ArrayRef<StringTable::Offset> PrefixesTable) const {
      return hasNoPrefix() ? ArrayRef<StringTable::Offset>()
                           : PrefixesTable.slice(PrefixesOffset + 1,
                                                 getNumPrefixes(PrefixesTable));
    }

  /// Return true if this option is restricted to specific subcommands.
  ///
  /// \return True if this option has an associated subcommand ID set.
  bool hasSubCommands() const { return SubCommandIDsOffset != 0; }

  /// Return the number of subcommand IDs associated with this option.
  ///
  /// \param SubCommandIDsTable Table of subcommand-ID sets from the OptTable.
  /// \return The number of subcommand IDs for this option.
  unsigned getNumSubCommandIDs(ArrayRef<unsigned> SubCommandIDsTable) const {
      // We embed the number of subcommand IDs in the value of the first offset.
      return SubCommandIDsTable[SubCommandIDsOffset];
    }

  /// Return the subcommand IDs associated with this option.
  ///
  /// \param SubCommandIDsTable Table of subcommand-ID sets from the OptTable.
  /// \return The subcommand IDs, or an empty range if unrestricted.
  ArrayRef<unsigned>
  getSubCommandIDs(ArrayRef<unsigned> SubCommandIDsTable) const {
      return hasSubCommands() ? SubCommandIDsTable.slice(
                                    SubCommandIDsOffset + 1,
                                    getNumSubCommandIDs(SubCommandIDsTable))
                              : ArrayRef<unsigned>();
    }

    /// Append this option's prefixes to \p Prefixes.
    ///
    /// \param StrTable String table holding prefix strings.
    /// \param PrefixesTable Table of prefix-set offsets from the OptTable.
    /// \param Prefixes Output vector to append prefixes to.
    void appendPrefixes(const StringTable &StrTable,
                        ArrayRef<StringTable::Offset> PrefixesTable,
                        SmallVectorImpl<StringRef> &Prefixes) const {
      for (auto PrefixOffset : getPrefixOffsets(PrefixesTable))
        Prefixes.push_back(StrTable[PrefixOffset]);
    }

  /// Return the prefix at \p PrefixIndex for this option.
  ///
  /// \param StrTable String table holding prefix strings.
  /// \param PrefixesTable Table of prefix-set offsets from the OptTable.
  /// \param PrefixIndex Index into this option's prefix list.
  /// \return The prefix string at \p PrefixIndex.
  StringRef getPrefix(const StringTable &StrTable,
                      ArrayRef<StringTable::Offset> PrefixesTable,
                      unsigned PrefixIndex) const {
      return StrTable[getPrefixOffsets(PrefixesTable)[PrefixIndex]];
    }

  /// Return the option name including its default prefix.
  ///
  /// \param StrTable String table holding option names.
  /// \return The prefixed option name from the string table.
  StringRef getPrefixedName(const StringTable &StrTable) const {
      return StrTable[PrefixedNameOffset];
    }

  /// Return the option name without any prefix.
  ///
  /// \param StrTable String table holding option names.
  /// \param PrefixesTable Table of prefix-set offsets from the OptTable.
  /// \return The option name with any leading prefix removed.
  StringRef getName(const StringTable &StrTable,
                    ArrayRef<StringTable::Offset> PrefixesTable) const {
      unsigned PrefixLength =
          hasNoPrefix() ? 0 : getPrefix(StrTable, PrefixesTable, 0).size();
      return getPrefixedName(StrTable).drop_front(PrefixLength);
    }
  };

public:
  /// Return true if \p CandidateInfo is valid for the named subcommand.
  ///
  /// \param CandidateInfo Option info to check.
  /// \param SubCommand Name of a registered subcommand.
  /// \return True if the option is valid for \p SubCommand.
  bool isValidForSubCommand(const Info *CandidateInfo,
                            StringRef SubCommand) const {
    assert(!SubCommand.empty() &&
           "This helper is only for valid registered subcommands.");
    auto SCIT = llvm::find_if(
        SubCommands, [&](const auto &C) { return SubCommand == C.Name; });
    assert(SCIT != SubCommands.end() &&
           "This helper is only for valid registered subcommands.");
    auto SubCommandIDs = CandidateInfo->getSubCommandIDs(SubCommandIDsTable);
    unsigned CurrentSubCommandID = SCIT - &SubCommands[0];
    return llvm::is_contained(SubCommandIDs, CurrentSubCommandID);
  }

private:
  // A unified string table for these options. Individual strings are stored as
  // null terminated C-strings at offsets within this table.
  const StringTable *StrTable;

  // A table of different sets of prefixes. Each set starts with the number of
  // prefixes in that set followed by that many offsets into the string table
  // for each of the prefix strings. This is essentially a Pascal-string style
  // encoding.
  ArrayRef<StringTable::Offset> PrefixesTable;

  /// The option information table.
  ArrayRef<Info> OptionInfos;

  bool IgnoreCase;

  /// The subcommand information table.
  ArrayRef<SubCommand> SubCommands;

  /// The subcommand IDs table.
  ArrayRef<unsigned> SubCommandIDsTable;

  bool GroupedShortOptions = false;
  bool DashDashParsing = false;
  const char *EnvVar = nullptr;

  unsigned InputOptionID = 0;
  unsigned UnknownOptionID = 0;

protected:
  /// The index of the first option which can be parsed (i.e., is not a
  /// special option like 'input' or 'unknown', and is not an option group).
  unsigned FirstSearchableIndex = 0;

  /// The union of all option prefixes. If an argument does not begin with
  /// one of these, it is an input.
  SmallVector<StringRef> PrefixesUnion;

  /// The union of the first element of all option prefixes.
  SmallString<8> PrefixChars;

private:
  const Info &getInfo(OptSpecifier Opt) const {
    unsigned id = Opt.getID();
    assert(id > 0 && id - 1 < getNumOptions() && "Invalid Option ID.");
    return OptionInfos[id - 1];
  }

  std::unique_ptr<Arg> parseOneArgGrouped(InputArgList &Args,
                                          unsigned &Index) const;

protected:
  /// Initialize OptTable using Tablegen'ed OptionInfos. Child class must
  /// manually call \c buildPrefixChars once they are fully constructed.
  ///
  /// \param StrTable Unified string table for option names and prefixes.
  /// \param PrefixesTable Table of prefix-set offsets into \p StrTable.
  /// \param OptionInfos Tablegen'ed option information entries.
  /// \param IgnoreCase Whether option name matching is case-insensitive.
  /// \param SubCommands Subcommand information table, if any.
  /// \param SubCommandIDsTable Table of per-option subcommand ID sets.
  OptTable(const StringTable &StrTable,
           ArrayRef<StringTable::Offset> PrefixesTable,
           ArrayRef<Info> OptionInfos, bool IgnoreCase = false,
           ArrayRef<SubCommand> SubCommands = {},
           ArrayRef<unsigned> SubCommandIDsTable = {});

  /// Build (or rebuild) the PrefixChars member.
  void buildPrefixChars();

public:
  /// Destroy the option table.
  virtual ~OptTable();

  /// Return the string table used for option names.
  ///
  /// \return The unified string table for option names and prefixes.
  const StringTable &getStrTable() const { return *StrTable; }

  /// Return the subcommand information table.
  ///
  /// \return The table of registered subcommands.
  ArrayRef<SubCommand> getSubCommands() const { return SubCommands; }

  /// Return the prefixes table used for option names.
  ///
  /// \return The table of prefix-set offsets into the string table.
  ArrayRef<StringTable::Offset> getPrefixesTable() const {
    return PrefixesTable;
  }

  /// Return the total number of option classes.
  ///
  /// \return The number of entries in the option information table.
  unsigned getNumOptions() const { return OptionInfos.size(); }

  /// Get the given Opt's Option instance, lazily creating it
  /// if necessary.
  ///
  /// \param Opt Option specifier to look up.
  /// \return The option, or null for the INVALID option id.
  const Option getOption(OptSpecifier Opt) const;

  /// Lookup the name of the given option.
  ///
  /// \param id Option specifier to look up.
  /// \return The option name without any prefix.
  StringRef getOptionName(OptSpecifier id) const {
    return getInfo(id).getName(*StrTable, PrefixesTable);
  }

  /// Lookup the prefix of the given option.
  ///
  /// \param id Option specifier to look up.
  /// \return The option's default prefix, or an empty string if none.
  StringRef getOptionPrefix(OptSpecifier id) const {
    const Info &I = getInfo(id);
    return I.hasNoPrefix() ? StringRef()
                           : I.getPrefix(*StrTable, PrefixesTable, 0);
  }

  /// Append the prefixes of the given option to \p Prefixes.
  ///
  /// \param id Option specifier to look up.
  /// \param Prefixes Output vector to append prefixes to.
  void appendOptionPrefixes(OptSpecifier id,
                            SmallVectorImpl<StringRef> &Prefixes) const {
    const Info &I = getInfo(id);
    I.appendPrefixes(*StrTable, PrefixesTable, Prefixes);
  }

  /// Lookup the prefixed name of the given option.
  ///
  /// \param id Option specifier to look up.
  /// \return The option name including its default prefix.
  StringRef getOptionPrefixedName(OptSpecifier id) const {
    return getInfo(id).getPrefixedName(*StrTable);
  }

  /// Get the kind of the given option.
  ///
  /// \param id Option specifier to look up.
  /// \return The option kind (see Option::OptionClass).
  unsigned getOptionKind(OptSpecifier id) const {
    return getInfo(id).Kind;
  }

  /// Get the group id for the given option.
  ///
  /// \param id Option specifier to look up.
  /// \return The identifier of the option group, if any.
  unsigned getOptionGroupID(OptSpecifier id) const {
    return getInfo(id).GroupID;
  }

  /// Get the help text to use to describe this option.
  ///
  /// \param id Option specifier to look up.
  /// \return The help text string for the option, or null if none is set.
  const char *getOptionHelpText(OptSpecifier id) const {
    return getOptionHelpText(id, Visibility(0));
  }

  /// Get the help text to use to describe this option.
  ///
  /// If it has visibility specific help text and that visibility is in the
  /// visibility mask, use that text instead of the generic text.
  ///
  /// \param id Option specifier to look up.
  /// \param VisibilityMask Visibility flags that select variant help text.
  /// \return The help text string for the option, or null if none is set.
  const char *getOptionHelpText(OptSpecifier id,
                                Visibility VisibilityMask) const {
    auto Info = getInfo(id);
    for (auto [Visibilities, Text] : Info.HelpTextsForVariants)
      for (auto Visibility : Visibilities)
        if (VisibilityMask & Visibility)
          return Text;
    return Info.HelpText;
  }

  /// Get the meta-variable name to use when describing
  /// this options values in the help text.
  ///
  /// \param id Option specifier to look up.
  /// \return The meta-variable name, or null if none is set.
  const char *getOptionMetaVar(OptSpecifier id) const {
    return getInfo(id).MetaVar;
  }

  /// Specify the environment variable where initial options should be read.
  ///
  /// \param E Name of the environment variable to read.
  void setInitialOptionsFromEnvironment(const char *E) { EnvVar = E; }

  /// Support grouped short options. e.g. -ab represents -a -b.
  ///
  /// \param Value True to enable grouped short option parsing.
  void setGroupedShortOptions(bool Value) { GroupedShortOptions = Value; }

  /// Set whether "--" stops option parsing and treats all subsequent arguments
  /// as positional. E.g. -- -a -b gives two positional inputs.
  ///
  /// \param Value True to treat "--" as end of options.
  void setDashDashParsing(bool Value) { DashDashParsing = Value; }

  /// Find possible value for given flags. This is used for shell
  /// autocompletion.
  ///
  /// \param [in] Option - Key flag like "-stdlib=" when "-stdlib=l"
  /// was passed to clang.
  ///
  /// \param [in] Arg - Value which we want to autocomplete like "l"
  /// when "-stdlib=l" was passed to clang.
  ///
  /// \return The vector of possible values.
  std::vector<std::string> suggestValueCompletions(StringRef Option,
                                                   StringRef Arg) const;

  /// Find flags from OptTable which starts with Cur.
  ///
  /// \param [in] Cur - String prefix that all returned flags need
  /// to start with.
  /// \param VisibilityMask Only include options with any of these visibility
  /// flags set.
  /// \param DisableFlags Exclude options that have any of these flags set.
  ///
  /// \return The vector of flags which start with Cur.
  std::vector<std::string> findByPrefix(StringRef Cur,
                                        Visibility VisibilityMask,
                                        unsigned int DisableFlags) const;

  /// Find the OptTable option that most closely matches the given string.
  ///
  /// \param [in] Option - A string, such as "-stdlibs=l", that represents user
  /// input of an option that may not exist in the OptTable. Note that the
  /// string includes prefix dashes "-" as well as values "=l".
  /// \param [out] NearestString - The nearest option string found in the
  /// OptTable.
  /// \param [in] VisibilityMask - Only include options with any of these
  ///                              visibility flags set.
  /// \param [in] MinimumLength - Don't find options shorter than this length.
  /// For example, a minimum length of 3 prevents "-x" from being considered
  /// near to "-S".
  /// \param [in] MaximumDistance - Don't find options whose distance is greater
  /// than this value.
  ///
  /// \return The edit distance of the nearest string found.
  unsigned findNearest(StringRef Option, std::string &NearestString,
                       Visibility VisibilityMask = Visibility(),
                       unsigned MinimumLength = 4,
                       unsigned MaximumDistance = UINT_MAX) const;

  /// Find the OptTable option that most closely matches the given string.
  ///
  /// \param Option A string that represents user input of an option that may
  /// not exist in the OptTable.
  /// \param NearestString The nearest option string found in the OptTable.
  /// \param FlagsToInclude Only include options with any of these flags set, or
  /// all options if zero.
  /// \param FlagsToExclude Exclude options that have any of these flags set.
  /// \param MinimumLength Don't find options shorter than this length.
  /// \param MaximumDistance Don't find options whose distance is greater than
  /// this value.
  /// \return The edit distance of the nearest string found.
  unsigned findNearest(StringRef Option, std::string &NearestString,
                       unsigned FlagsToInclude, unsigned FlagsToExclude = 0,
                       unsigned MinimumLength = 4,
                       unsigned MaximumDistance = UINT_MAX) const;

private:
  unsigned
  internalFindNearest(StringRef Option, std::string &NearestString,
                      unsigned MinimumLength, unsigned MaximumDistance,
                      std::function<bool(const Info &)> ExcludeOption) const;

public:
  /// Return true if \p Option exactly matches an option in the table.
  ///
  /// \param Option Option string to look up, including any prefix.
  /// \param ExactString Set to the matched option string on success.
  /// \param VisibilityMask Only include options with any of these visibility
  /// flags set.
  /// \return True if an exact match was found.
  bool findExact(StringRef Option, std::string &ExactString,
                 Visibility VisibilityMask = Visibility()) const {
    return findNearest(Option, ExactString, VisibilityMask, 4, 0) == 0;
  }

  /// Return true if \p Option exactly matches an option in the table.
  ///
  /// \param Option Option string to look up, including any prefix.
  /// \param ExactString Set to the matched option string on success.
  /// \param FlagsToInclude Only include options with any of these flags set, or
  /// all options if zero.
  /// \param FlagsToExclude Exclude options that have any of these flags set.
  /// \return True if an exact match was found.
  bool findExact(StringRef Option, std::string &ExactString,
                 unsigned FlagsToInclude, unsigned FlagsToExclude = 0) const {
    return findNearest(Option, ExactString, FlagsToInclude, FlagsToExclude, 4,
                       0) == 0;
  }

  /// Parse a single argument; returning the new argument and
  /// updating Index.
  ///
  /// \param Args Argument list being parsed.
  /// \param [in,out] Index - The current parsing position in the argument
  /// string list; on return this will be the index of the next argument
  /// string to parse.
  /// \param [in] VisibilityMask - Only include options with any of these
  /// visibility flags set.
  ///
  /// \return The parsed argument, or 0 if the argument is missing values
  /// (in which case Index still points at the conceptual next argument string
  /// to parse).
  std::unique_ptr<Arg>
  ParseOneArg(const ArgList &Args, unsigned &Index,
              Visibility VisibilityMask = Visibility()) const;

  /// Parse a single argument; returning the new argument and updating Index.
  ///
  /// \param Args Argument list being parsed.
  /// \param Index The current parsing position in the argument string list; on
  /// return this will be the index of the next argument string to parse.
  /// \param FlagsToInclude Only include options with any of these flags set, or
  /// all options if zero.
  /// \param FlagsToExclude Exclude options that have any of these flags set.
  /// \return The parsed argument, or 0 if the argument is missing values.
  std::unique_ptr<Arg> ParseOneArg(const ArgList &Args, unsigned &Index,
                                   unsigned FlagsToInclude,
                                   unsigned FlagsToExclude) const;

private:
  std::unique_ptr<Arg>
  internalParseOneArg(const ArgList &Args, unsigned &Index,
                      std::function<bool(const Option &)> ExcludeOption) const;

public:
  /// Parse an list of arguments into an InputArgList.
  ///
  /// The resulting InputArgList will reference the strings in [\p ArgBegin,
  /// \p ArgEnd), and their lifetime should extend past that of the returned
  /// InputArgList.
  ///
  /// The only error that can occur in this routine is if an argument is
  /// missing values; in this case \p MissingArgCount will be non-zero.
  ///
  /// \param Args Argument strings to parse.
  /// \param MissingArgIndex - On error, the index of the option which could
  /// not be parsed.
  /// \param MissingArgCount - On error, the number of missing options.
  /// \param VisibilityMask - Only include options with any of these
  /// visibility flags set.
  /// \return An InputArgList; on error this will contain all the options
  /// which could be parsed.
  InputArgList ParseArgs(ArrayRef<const char *> Args, unsigned &MissingArgIndex,
                         unsigned &MissingArgCount,
                         Visibility VisibilityMask = Visibility()) const;

  /// Parse a list of arguments into an InputArgList.
  ///
  /// \param Args Argument strings to parse.
  /// \param MissingArgIndex On error, the index of the option which could not
  /// be parsed.
  /// \param MissingArgCount On error, the number of missing options.
  /// \param FlagsToInclude Only include options with any of these flags set, or
  /// all options if zero.
  /// \param FlagsToExclude Exclude options that have any of these flags set.
  /// \return An InputArgList; on error this will contain all the options which
  /// could be parsed.
  InputArgList ParseArgs(ArrayRef<const char *> Args, unsigned &MissingArgIndex,
                         unsigned &MissingArgCount, unsigned FlagsToInclude,
                         unsigned FlagsToExclude = 0) const;

private:
  InputArgList
  internalParseArgs(ArrayRef<const char *> Args, unsigned &MissingArgIndex,
                    unsigned &MissingArgCount,
                    std::function<bool(const Option &)> ExcludeOption) const;

public:
  /// A convenience helper which handles optional initial options populated from
  /// an environment variable, expands response files recursively and parses
  /// options.
  ///
  /// \param Argc Argument count, as in main.
  /// \param Argv Argument vector, as in main.
  /// \param Unknown Specifier for the unknown-option sentinel.
  /// \param Saver String saver used when expanding response files.
  /// \param ErrorFn - Called on a formatted error message for missing arguments
  /// or unknown options.
  /// \return An InputArgList; on error this will contain all the options which
  /// could be parsed.
  InputArgList parseArgs(int Argc, char *const *Argv, OptSpecifier Unknown,
                         StringSaver &Saver,
                         std::function<void(StringRef)> ErrorFn) const;

  /// Render the help text for an option table.
  ///
  /// \param OS - The stream to write the help text to.
  /// \param Usage - USAGE: Usage
  /// \param Title - OVERVIEW: Title
  /// \param ShowHidden     - If true, display options marked as HelpHidden
  /// \param ShowAllAliases - If true, display all options including aliases
  ///                         that don't have help texts. By default, we display
  ///                         only options that are not hidden and have help
  ///                         texts.
  /// \param VisibilityMask - Only include options with any of these
  ///                         visibility flags set.
  /// \param SubCommand - Subcommand whose options should be shown, or empty
  ///                     for the default command.
  void printHelp(raw_ostream &OS, const char *Usage, const char *Title,
                 bool ShowHidden = false, bool ShowAllAliases = false,
                 Visibility VisibilityMask = Visibility(),
                 StringRef SubCommand = {}) const;

  /// Render the help text for an option table.
  ///
  /// \param OS The stream to write the help text to.
  /// \param Usage USAGE: Usage
  /// \param Title OVERVIEW: Title
  /// \param FlagsToInclude Only include options with any of these flags set, or
  /// all options if zero.
  /// \param FlagsToExclude Exclude options that have any of these flags set.
  /// \param ShowAllAliases If true, display all options including aliases that
  /// don't have help texts.
  void printHelp(raw_ostream &OS, const char *Usage, const char *Title,
                 unsigned FlagsToInclude, unsigned FlagsToExclude,
                 bool ShowAllAliases) const;

private:
  void internalPrintHelp(raw_ostream &OS, const char *Usage, const char *Title,
                         StringRef SubCommand, bool ShowHidden,
                         bool ShowAllAliases,
                         std::function<bool(const Info &)> ExcludeOption,
                         Visibility VisibilityMask) const;
};

/// Specialization of OptTable that builds prefix characters during
/// construction.
class GenericOptTable : public OptTable {
protected:
  /// Construct a GenericOptTable from Tablegen'ed option data.
  ///
  /// \param StrTable Unified string table for option names and prefixes.
  /// \param PrefixesTable Table of prefix-set offsets into \p StrTable.
  /// \param OptionInfos Tablegen'ed option information entries.
  /// \param IgnoreCase Whether option name matching is case-insensitive.
  /// \param SubCommands Subcommand information table, if any.
  /// \param SubCommandIDsTable Table of per-option subcommand ID sets.
  LLVM_ABI GenericOptTable(const StringTable &StrTable,
                           ArrayRef<StringTable::Offset> PrefixesTable,
                           ArrayRef<Info> OptionInfos, bool IgnoreCase = false,
                           ArrayRef<SubCommand> SubCommands = {},
                           ArrayRef<unsigned> SubCommandIDsTable = {});
};

/// Specialization of OptTable that uses a precomputed prefixes union.
class PrecomputedOptTable : public OptTable {
protected:
  /// Construct a PrecomputedOptTable from Tablegen'ed option data.
  ///
  /// \param StrTable Unified string table for option names and prefixes.
  /// \param PrefixesTable Table of prefix-set offsets into \p StrTable.
  /// \param OptionInfos Tablegen'ed option information entries.
  /// \param PrefixesUnionOffsets Offsets of the precomputed prefixes union.
  /// \param IgnoreCase Whether option name matching is case-insensitive.
  /// \param SubCommands Subcommand information table, if any.
  /// \param SubCommandIDsTable Table of per-option subcommand ID sets.
  PrecomputedOptTable(const StringTable &StrTable,
                      ArrayRef<StringTable::Offset> PrefixesTable,
                      ArrayRef<Info> OptionInfos,
                      ArrayRef<StringTable::Offset> PrefixesUnionOffsets,
                      bool IgnoreCase = false,
                      ArrayRef<SubCommand> SubCommands = {},
                      ArrayRef<unsigned> SubCommandIDsTable = {})
      : OptTable(StrTable, PrefixesTable, OptionInfos, IgnoreCase, SubCommands,
                 SubCommandIDsTable) {
    for (auto PrefixOffset : PrefixesUnionOffsets)
      PrefixesUnion.push_back(StrTable[PrefixOffset]);
    buildPrefixChars();
  }
};

} // end namespace opt

} // end namespace llvm

#define LLVM_MAKE_OPT_ID_WITH_ID_PREFIX(                                       \
    ID_PREFIX, PREFIXES_OFFSET, PREFIXED_NAME_OFFSET, ID, KIND, GROUP, ALIAS,  \
    ALIASARGS, FLAGS, VISIBILITY, PARAM, HELPTEXT, HELPTEXTSFORVARIANTS,       \
    METAVAR, VALUES, SUBCOMMANDIDS_OFFSET)                                     \
  ID_PREFIX##ID

#define LLVM_MAKE_OPT_ID(PREFIXES_OFFSET, PREFIXED_NAME_OFFSET, ID, KIND,      \
                         GROUP, ALIAS, ALIASARGS, FLAGS, VISIBILITY, PARAM,    \
                         HELPTEXT, HELPTEXTSFORVARIANTS, METAVAR, VALUES,      \
                         SUBCOMMANDIDS_OFFSET)                                 \
  LLVM_MAKE_OPT_ID_WITH_ID_PREFIX(                                             \
      OPT_, PREFIXES_OFFSET, PREFIXED_NAME_OFFSET, ID, KIND, GROUP, ALIAS,     \
      ALIASARGS, FLAGS, VISIBILITY, PARAM, HELPTEXT, HELPTEXTSFORVARIANTS,     \
      METAVAR, VALUES, SUBCOMMANDIDS_OFFSET)

#define LLVM_CONSTRUCT_OPT_INFO_WITH_ID_PREFIX(                                \
    ID_PREFIX, PREFIXES_OFFSET, PREFIXED_NAME_OFFSET, ID, KIND, GROUP, ALIAS,  \
    ALIASARGS, FLAGS, VISIBILITY, PARAM, HELPTEXT, HELPTEXTSFORVARIANTS,       \
    METAVAR, VALUES, SUBCOMMANDIDS_OFFSET)                                     \
  llvm::opt::OptTable::Info {                                                  \
    PREFIXES_OFFSET, PREFIXED_NAME_OFFSET, HELPTEXT, HELPTEXTSFORVARIANTS,     \
        METAVAR, ID_PREFIX##ID, llvm::opt::Option::KIND##Class, PARAM, FLAGS,  \
        VISIBILITY, ID_PREFIX##GROUP, ID_PREFIX##ALIAS, ALIASARGS, VALUES,     \
        SUBCOMMANDIDS_OFFSET                                                   \
  }

#define LLVM_CONSTRUCT_OPT_INFO(                                               \
    PREFIXES_OFFSET, PREFIXED_NAME_OFFSET, ID, KIND, GROUP, ALIAS, ALIASARGS,  \
    FLAGS, VISIBILITY, PARAM, HELPTEXT, HELPTEXTSFORVARIANTS, METAVAR, VALUES, \
    SUBCOMMANDIDS_OFFSET)                                                      \
  LLVM_CONSTRUCT_OPT_INFO_WITH_ID_PREFIX(                                      \
      OPT_, PREFIXES_OFFSET, PREFIXED_NAME_OFFSET, ID, KIND, GROUP, ALIAS,     \
      ALIASARGS, FLAGS, VISIBILITY, PARAM, HELPTEXT, HELPTEXTSFORVARIANTS,     \
      METAVAR, VALUES, SUBCOMMANDIDS_OFFSET)

#endif // LLVM_OPTION_OPTTABLE_H
