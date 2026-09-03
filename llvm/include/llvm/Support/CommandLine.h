//===- llvm/Support/CommandLine.h - Command line handler --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class implements a command line argument processor that is useful when
// creating a tool.  It provides a simple, minimalistic interface that is easily
// extensible and supports nonlocal (library) command line options.
//
// Note that rather than trying to figure out what this code does, you should
// read the library documentation located in docs/CommandLine.html or looks at
// the many example usages in tools/*/*.cpp
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_COMMANDLINE_H
#define LLVM_SUPPORT_COMMANDLINE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <climits>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <string>
#include <type_traits>
#include <vector>

namespace llvm {

namespace vfs {
class FileSystem;
}

class StringSaver;
class ElementCount;

/// This namespace contains all of the command line option processing machinery.
/// It is intentionally a short name to make qualified usage concise.
namespace cl {

/// Parse command-line options from \p argv (and optionally \p EnvVar).
///
/// Returns true on success. Otherwise, this will print the error message to
/// stderr and exit if \p Errs is not set (nullptr by default), or print the
/// error message to \p Errs and return false if \p Errs is provided.
///
/// If EnvVar is not nullptr, command-line options are also parsed from the
/// environment variable named by EnvVar.  Precedence is given to occurrences
/// from argv.  This precedence is currently implemented by parsing argv after
/// the environment variable, so it is only implemented correctly for options
/// that give precedence to later occurrences.  If your program supports options
/// that give precedence to earlier occurrences, you will need to extend this
/// function to support it correctly.
///
/// \param argc Argument count from main.
/// \param argv Argument vector from main.
/// \param Overview Optional overview text shown in \c -help output.
/// \param Errs Optional stream for error messages; if null, errors exit.
/// \param VFS Optional VFS used when expanding response files.
/// \param EnvVar Optional environment variable also providing options.
/// \param LongOptionsUseDoubleDash Require \c -- for long option names.
/// \return True on success; false when \p Errs is set and a parse error occurs.
LLVM_ABI bool ParseCommandLineOptions(int argc, const char *const *argv,
                                      StringRef Overview = "",
                                      raw_ostream *Errs = nullptr,
                                      vfs::FileSystem *VFS = nullptr,
                                      const char *EnvVar = nullptr,
                                      bool LongOptionsUseDoubleDash = false);

/// Function type used to print version information to a stream.
using VersionPrinterTy = std::function<void(raw_ostream &)>;

/// Override the default LLVM version printer used for \c --version.
///
/// This allows other systems using the CommandLine utilities to print their
/// own version string.
///
/// \param func Replacement version printer.
LLVM_ABI void SetVersionPrinter(VersionPrinterTy func);

/// Register an additional version printer invoked after the default one.
///
/// This can be called multiple times, and each time it adds a new function to
/// the list which will be called after the basic LLVM version printing is
/// complete. Each can then add additional information specific to the tool.
///
/// \param func Extra printer to invoke after the default version text.
LLVM_ABI void AddExtraVersionPrinter(VersionPrinterTy func);

/// Print option values for \c -print-options / \c -print-all-options.
///
/// With -print-options print the difference between option values and defaults.
/// With -print-all-options print all option values.
/// (Currently not perfect, but best-effort.)
LLVM_ABI void PrintOptionValues();

// Forward declaration - AddLiteralOption needs to be up here to make gcc happy.
class Option;

/// Adds a new option for parsing and provides the option it refers to.
///
/// \param O pointer to the option
/// \param Name the string name for the option to handle during parsing
///
/// Literal options are used by some parsers to register special option values.
/// This is how the PassNameParser registers pass names for opt.
LLVM_ABI void AddLiteralOption(Option &O, StringRef Name);

//===----------------------------------------------------------------------===//
// Flags permitted to be passed to command line arguments
//

/// Flags controlling how many times an option may appear on the command line.
enum NumOccurrencesFlag {
  /// Zero or one occurrence is allowed.
  Optional = 0x00,
  /// Zero or more occurrences of the option are allowed.
  ZeroOrMore = 0x01,
  /// Exactly one occurrence is required.
  Required = 0x02,
  /// The option must appear at least once on the command line.
  OneOrMore = 0x03,

  /// Capture all arguments after the last required positional argument.
  ///
  /// It is an error if there are zero positional arguments and a ConsumeAfter
  /// option is used. Thus, for example, all arguments to LLI are processed
  /// until a filename is found. Once a filename is found, all of the succeeding
  /// arguments are passed, unprocessed, to the ConsumeAfter option.
  ConsumeAfter = 0x04
};

/// Whether a separate value argument is required, optional, or forbidden.
enum ValueExpected {
  // zero reserved for the unspecified value
  /// The value may appear or be omitted.
  ValueOptional = 0x01,
  /// A value is required to appear.
  ValueRequired = 0x02,
  /// The option is a boolean flag; no separate value may follow it on the
  /// command line.
  ValueDisallowed = 0x03
};

/// Controls whether an option appears in \c -help / \c -help-hidden output.
enum OptionHidden {
  /// Option is shown in both \c -help and \c -help-hidden.
  NotHidden = 0x00,
  /// Option is omitted from \c -help but shown in \c -help-hidden.
  Hidden = 0x01,
  /// Option is omitted from both \c -help and \c -help-hidden.
  ReallyHidden = 0x02
};

// This controls special features that the option might have that cause it to be
// parsed differently...
//
// Prefix - This option allows arguments that are otherwise unrecognized to be
// matched by options that are a prefix of the actual value.  This is useful for
// cases like a linker, where options are typically of the form '-lfoo' or
// '-L../../include' where -l or -L are the actual flags.  When prefix is
// enabled, and used, the value for the flag comes from the suffix of the
// argument.
//
// AlwaysPrefix - Only allow the behavior enabled by the Prefix flag and reject
// the Option=Value form.
//

/// Flags controlling how an option's name and value are formatted on the
/// command line.
enum FormattingFlags {
  /// Standard \c -option [value] formatting.
  NormalFormatting = 0x00,
  /// Positional argument; no leading dash is required.
  Positional = 0x01,
  /// The option may directly prefix its value (for example \c -lfoo).
  Prefix = 0x02,
  /// The value must immediately follow the flag with no \c '=' separator
  /// (for example \c -lfoo rather than \c -l=foo).
  AlwaysPrefix = 0x03
};

/// Miscellaneous flags adjusting how an option is parsed.
enum MiscFlags {
  /// Split \c cl::list values on commas.
  CommaSeparated = 0x01,
  /// Allow a positional \c cl::list to consume dash-prefixed arguments.
  PositionalEatsArgs = 0x02,
  /// Consume unrecognized options into this \c cl::list.
  Sink = 0x04,

  /// Allow single-letter options to group after one hyphen (e.g. \c -la).
  ///
  /// If this is enabled, multiple letter options are allowed to bunch together
  /// with only a single hyphen for the whole group. This allows emulation of
  /// the behavior that ls uses for example: ls -la === ls -l -a.
  Grouping = 0x08,

  /// Treat this as a default option used when resolving ambiguities.
  DefaultOption = 0x10
};

/// Named group used to organize options in categorized \c -help output.
class OptionCategory {
private:
  StringRef const Name;
  StringRef const Description;

  LLVM_ABI void registerCategory();

public:
  /// Construct and register a category named \p Name.
  ///
  /// \param Name Category display name.
  /// \param Description Optional longer description for the category.
  OptionCategory(StringRef const Name,
                 StringRef const Description = "")
      : Name(Name), Description(Description) {
    registerCategory();
  }

  /// Return the category's display name.
  ///
  /// \return The category's display name.
  StringRef getName() const { return Name; }
  /// Return the category's longer description text.
  ///
  /// \return The category's longer description text.
  StringRef getDescription() const { return Description; }
};

// The general Option Category (used as default category).
/// Return the default option category used when none is specified.
///
/// \return The default option category.
LLVM_ABI OptionCategory &getGeneralCategory();

/// Named subcommand that owns a set of options and is selected by argv.
class SubCommand {
private:
  StringRef Name;
  StringRef Description;

protected:
  /// Register this subcommand with the global command-line system.
  LLVM_ABI void registerSubCommand();
  /// Unregister this subcommand from the global command-line system.
  LLVM_ABI void unregisterSubCommand();

public:
  /// Construct and register a subcommand named \p Name.
  ///
  /// \param Name Subcommand name matched on the command line.
  /// \param Description Optional help text for the subcommand.
  SubCommand(StringRef Name, StringRef Description = "")
      : Name(Name), Description(Description) {
        registerSubCommand();
  }
  /// Construct an unnamed subcommand placeholder.
  SubCommand() = default;

  /// Return the special subcommand representing no subcommand.
  ///
  /// \return The top-level (no-subcommand) SubCommand.
  LLVM_ABI static SubCommand &getTopLevel();

  /// Return the subcommand used to place an option into every subcommand.
  ///
  /// \return The SubCommand that places an option into every subcommand.
  LLVM_ABI static SubCommand &getAll();

  /// Clear registered options and reset this subcommand's parsing state.
  LLVM_ABI void reset();

  /// Return true if this subcommand is currently active.
  ///
  /// \return True if this subcommand is currently active.
  LLVM_ABI explicit operator bool() const;

  /// Return the subcommand name matched on the command line.
  ///
  /// \return The subcommand name.
  StringRef getName() const { return Name; }
  /// Return the subcommand's help description.
  ///
  /// \return The subcommand help description.
  StringRef getDescription() const { return Description; }

  /// Positional options registered for this subcommand.
  SmallVector<Option *, 4> PositionalOpts;
  /// Sink options that consume unrecognized arguments for this subcommand.
  SmallVector<Option *, 4> SinkOpts;
  /// Map from option argument strings to options in this subcommand.
  DenseMap<StringRef, Option *> OptionsMap;

  /// The \c ConsumeAfter option registered for this subcommand, if any.
  Option *ConsumeAfterOpt = nullptr;
};

/// Group of subcommands that can be assigned to an option together.
class SubCommandGroup {
  SmallVector<SubCommand *, 4> Subs;

public:
  /// Construct a group from the subcommands in \p IL.
  ///
  /// \param IL Initializer list of subcommand pointers.
  SubCommandGroup(std::initializer_list<SubCommand *> IL) : Subs(IL) {}

  /// Return the subcommands in this group.
  ///
  /// \return The subcommands in this group.
  ArrayRef<SubCommand *> getSubCommands() const { return Subs; }
};

/// Abstract base for a registered command-line option.
class LLVM_ABI Option {
  friend class alias;

  /// Handle one occurrence of this option; return true on parse error.
  ///
  /// \param pos argv index of this occurrence.
  /// \param ArgName Option name as written on the command line.
  /// \param Arg Option value string, if any.
  virtual bool handleOccurrence(unsigned pos, StringRef ArgName,
                                StringRef Arg) = 0;

  /// Return the default ValueExpected flag when none was set explicitly.
  virtual enum ValueExpected getValueExpectedFlagDefault() const {
    return ValueOptional;
  }

  /// Out-of-line virtual method providing a home for this class.
  virtual void anchor();

  uint16_t NumOccurrences; // The number of times specified
  // Occurrences, HiddenFlag, and Formatting are all enum types but to avoid
  // problems with signed enums in bitfields.
  uint16_t Occurrences : 3; // enum NumOccurrencesFlag
  // not using the enum type for 'Value' because zero is an implementation
  // detail representing the non-value
  uint16_t Value : 2;
  uint16_t HiddenFlag : 2; // enum OptionHidden
  uint16_t Formatting : 2; // enum FormattingFlags
  uint16_t Misc : 5;
  uint16_t FullyInitialized : 1; // Has addArgument been called?
  uint16_t Position;             // Position of last occurrence of the option
  uint16_t AdditionalVals;       // Greater than 0 for multi-valued option.

public:
  /// The argument string itself (for example \c "help" or \c "o").
  StringRef ArgStr;
  /// Descriptive text printed next to this option by \c -help and
  /// \c -help-hidden.
  StringRef HelpStr;
  /// Placeholder text describing the option's value in \c -help output.
  StringRef ValueStr;
  /// Categories this option belongs to for categorized help.
  SmallVector<OptionCategory *, 1> Categories;
  /// Subcommands this option belongs to.
  SmallPtrSet<SubCommand *, 1> Subs;

  /// Return how many times this option may appear.
  ///
  /// \return The NumOccurrencesFlag for this option.
  inline enum NumOccurrencesFlag getNumOccurrencesFlag() const {
    return (enum NumOccurrencesFlag)Occurrences;
  }

  /// Return whether this option expects a separate value argument on the
  /// command line.
  ///
  /// \return The ValueExpected flag for this option.
  inline enum ValueExpected getValueExpectedFlag() const {
    return Value ? ((enum ValueExpected)Value) : getValueExpectedFlagDefault();
  }

  /// Return the help-visibility flag for this option.
  ///
  /// \return The OptionHidden flag for this option.
  inline enum OptionHidden getOptionHiddenFlag() const {
    return (enum OptionHidden)HiddenFlag;
  }

  /// Return how this option expects its value to be formatted on the command
  /// line (normal, positional, prefix, or always-prefix).
  ///
  /// \return The FormattingFlags value for this option.
  inline enum FormattingFlags getFormattingFlag() const {
    return (enum FormattingFlags)Formatting;
  }

  /// Return the miscellaneous parsing flags for this option.
  ///
  /// \return The miscellaneous MiscFlags bitfield.
  inline unsigned getMiscFlags() const { return Misc; }
  /// Return the argv position of the last occurrence of this option.
  ///
  /// \return The argv index of the last occurrence.
  inline unsigned getPosition() const { return Position; }
  /// Return how many additional values a multi-valued option consumes.
  ///
  /// \return The number of additional values consumed.
  inline unsigned getNumAdditionalVals() const { return AdditionalVals; }

  /// Return true if this option has a non-empty argument string.
  ///
  /// \return True if ArgStr is non-empty.
  bool hasArgStr() const { return !ArgStr.empty(); }
  /// Return true if this option is parsed positionally.
  ///
  /// \return True if this option uses positional formatting.
  bool isPositional() const { return getFormattingFlag() == cl::Positional; }
  /// Return true if this option sinks unrecognized arguments.
  ///
  /// \return True if the Sink misc flag is set.
  bool isSink() const { return getMiscFlags() & cl::Sink; }
  /// Return true if this option is marked as a default option.
  ///
  /// \return True if the DefaultOption misc flag is set.
  bool isDefaultOption() const { return getMiscFlags() & cl::DefaultOption; }

  /// Return true if this is a ConsumeAfter option.
  ///
  /// \return True if the occurrence flag is ConsumeAfter.
  bool isConsumeAfter() const {
    return getNumOccurrencesFlag() == cl::ConsumeAfter;
  }

  //-------------------------------------------------------------------------===
  // Accessor functions set by OptionModifiers
  //
  /// Set the argument string used to match this option on the command line.
  ///
  /// \param S Argument name without a leading dash.
  void setArgStr(StringRef S);
  /// Set the help description text shown for this option.
  ///
  /// \param S Help text printed by \c -help.
  void setDescription(StringRef S) { HelpStr = S; }
  /// Set the value placeholder string shown in \c -help output.
  ///
  /// \param S Placeholder describing the option value.
  void setValueStr(StringRef S) { ValueStr = S; }
  /// Set how many times this option may appear.
  ///
  /// \param Val Occurrence constraint flag.
  void setNumOccurrencesFlag(enum NumOccurrencesFlag Val) { Occurrences = Val; }
  /// Set whether a separate value argument is expected.
  ///
  /// \param Val Value-expected flag.
  void setValueExpectedFlag(enum ValueExpected Val) { Value = Val; }
  /// Set the help-visibility flag for this option.
  ///
  /// \param Val Hidden / not-hidden / really-hidden flag.
  void setHiddenFlag(enum OptionHidden Val) { HiddenFlag = Val; }
  /// Set how the option name and value are formatted.
  ///
  /// \param V Formatting flag.
  void setFormattingFlag(enum FormattingFlags V) { Formatting = V; }
  /// OR additional miscellaneous parsing flags onto this option.
  ///
  /// \param M Flags to set.
  void setMiscFlag(enum MiscFlags M) { Misc |= M; }
  /// Record the argv position of an occurrence.
  ///
  /// \param pos argv index.
  void setPosition(unsigned pos) { Position = pos; }
  /// Add this option to help category \p C.
  ///
  /// \param C Category to associate with this option.
  void addCategory(OptionCategory &C);
  /// Add this option to subcommand \p S.
  ///
  /// \param S Subcommand that should include this option.
  void addSubCommand(SubCommand &S) { Subs.insert(&S); }

protected:
  /// Construct an option with the given occurrence and visibility flags.
  ///
  /// \param OccurrencesFlag How many times the option may appear.
  /// \param Hidden Help-visibility flag.
  explicit Option(enum NumOccurrencesFlag OccurrencesFlag,
                  enum OptionHidden Hidden);

  /// Set how many additional values a multi-valued option consumes.
  ///
  /// \param n Number of additional values.
  inline void setNumAdditionalVals(unsigned n) { AdditionalVals = n; }

public:
  /// Destroy the option.
  virtual ~Option() = default;

  /// Register this argument with the command-line system.
  void addArgument();

  /// Unregisters this option from the CommandLine system.
  ///
  /// This option must have been the last option registered.
  /// For testing purposes only.
  void removeArgument();

  /// Return the width of the option tag for help formatting.
  ///
  /// \return The width of the option tag in characters.
  virtual size_t getOptionWidth() const = 0;

  /// Print help information about this option using \p GlobalWidth.
  ///
  /// \param GlobalWidth Column width reserved for option tags.
  virtual void printOptionInfo(size_t GlobalWidth) const = 0;

  /// Print this option's current value, honoring \p GlobalWidth formatting.
  ///
  /// \param GlobalWidth Column width reserved for option tags.
  /// \param Force Print even when the value equals the default.
  virtual void printOptionValue(size_t GlobalWidth, bool Force) const = 0;

  /// Restore this option to its default value and occurrence state.
  virtual void setDefault() = 0;

  /// Print multi-line help text for an option, preserving \p Indent and
  /// accounting for \p FirstLineIndentedBy characters already printed.
  ///
  /// \param HelpStr Help text to print.
  /// \param Indent Indentation for continuation lines.
  /// \param FirstLineIndentedBy Characters already printed on the first line.
  static void printHelpStr(StringRef HelpStr, size_t Indent,
                           size_t FirstLineIndentedBy);

  /// Print multi-line help text for an enum value.
  ///
  /// This maintains the Indent for multi-line descriptions.
  /// FirstLineIndentedBy is the count of chars of the first line
  /// (the one containing the =<value>).
  ///
  /// \param HelpStr Help text to print.
  /// \param Indent Indentation for continuation lines.
  /// \param FirstLineIndentedBy Characters already printed on the first line.
  static void printEnumValHelpStr(StringRef HelpStr, size_t Indent,
                                  size_t FirstLineIndentedBy);

  /// Append any additional option names this parser recognizes to \p OptionNames.
  ///
  /// \param OptionNames Destination for extra option name strings.
  virtual void getExtraOptionNames(SmallVectorImpl<StringRef> &OptionNames) {}

  /// Record an occurrence, enforcing occurrence flags; return true on error.
  ///
  /// \param pos argv index of this occurrence.
  /// \param ArgName Option name as written.
  /// \param Value Option value string.
  /// \param MultiArg Whether this is part of a multi-value occurrence.
  /// \return True on error.
  virtual bool addOccurrence(unsigned pos, StringRef ArgName, StringRef Value,
                             bool MultiArg = false);

  /// Print the option name followed by \p Message to \p Errs.
  ///
  /// Always returns true so callers can write \c return error(...).
  ///
  /// \param Message Error text to print.
  /// \param ArgName Optional argument name override.
  /// \param Errs Stream that receives the error message.
  /// \return Always true, so callers can write \c return error(...).
  bool error(const Twine &Message, StringRef ArgName = StringRef(), raw_ostream &Errs = llvm::errs());
  /// Print the option name followed by \p Message to \p Errs.
  ///
  /// \param Message Error text to print.
  /// \param Errs Stream that receives the error message.
  /// \return Always true, so callers can write \c return error(...).
  bool error(const Twine &Message, raw_ostream &Errs) {
    return error(Message, StringRef(), Errs);
  }

  /// Return how many times this option has been specified so far.
  ///
  /// \return The number of times this option has been specified.
  inline int getNumOccurrences() const { return NumOccurrences; }
  /// Reset occurrence counts and related parsing state for this option.
  void reset();
};

//===----------------------------------------------------------------------===//
// Command line option modifiers that can be used to modify the behavior of
// command line option parsers...
//

/// Option modifier that sets the help description shown in \c -help.
struct desc {
  /// Help description text for the option.
  StringRef Desc;

  /// Construct a description modifier from \p Str.
  ///
  /// \param Str Help text to show for the option.
  desc(StringRef Str) : Desc(Str) {}

  /// Apply this description to option \p O.
  ///
  /// \param O Option that receives the description.
  void apply(Option &O) const { O.setDescription(Desc); }
};

/// Option modifier that sets the value placeholder shown in \c -help.
struct value_desc {
  /// Placeholder text describing the option value (e.g. \c filename).
  StringRef Desc;

  /// Construct a value-description modifier from \p Str.
  ///
  /// \param Str Placeholder text for the option value.
  value_desc(StringRef Str) : Desc(Str) {}

  /// Apply this value description to option \p O.
  ///
  /// \param O Option that receives the value description.
  void apply(Option &O) const { O.setValueStr(Desc); }
};

/// Option modifier that sets a default initial value for an \c opt.
///
/// Specify a default (initial) value for the command line argument, if the
/// default constructor for the argument type does not give you what you want.
/// This is only valid on "opt" arguments, not on "list" arguments.
template <class Ty> struct initializer {
  /// Default value applied when the option is constructed.
  const Ty &Init;
  /// Construct an initializer holding \p Val.
  ///
  /// \param Val Default value for the option.
  initializer(const Ty &Val) : Init(Val) {}

  /// Apply the initial value to option \p O.
  ///
  /// \param O Option that receives the initial value.
  template <class Opt> void apply(Opt &O) const { O.setInitialValue(Init); }
};

/// Option modifier that sets default elements for a \c list option.
template <class Ty> struct list_initializer {
  /// Default elements applied when the list option is constructed.
  ArrayRef<Ty> Inits;
  /// Construct a list initializer from \p Vals.
  ///
  /// \param Vals Default elements for the list option.
  list_initializer(ArrayRef<Ty> Vals) : Inits(Vals) {}

  /// Apply the initial values to list option \p O.
  ///
  /// \param O List option that receives the initial values.
  template <class Opt> void apply(Opt &O) const { O.setInitialValues(Inits); }
};

/// Build an \c initializer modifier for default value \p Val.
///
/// \param Val Default value for an \c opt.
/// \return An initializer modifier holding \p Val.
template <class Ty> initializer<Ty> init(const Ty &Val) {
  return initializer<Ty>(Val);
}

/// Build a \c list_initializer modifier for default elements \p Vals.
///
/// \param Vals Default elements for a \c list.
/// \return A list_initializer modifier holding \p Vals.
template <class Ty>
list_initializer<Ty> list_init(ArrayRef<Ty> Vals) {
  return list_initializer<Ty>(Vals);
}

/// Option modifier that stores the parsed value in an external variable.
///
/// Allow the user to specify which external variable they want to store the
/// results of the command line argument processing into, if they don't want to
/// store it in the option itself.
template <class Ty> struct LocationClass {
  /// External variable that receives the parsed option value.
  Ty &Loc;

  /// Construct a location modifier bound to external variable \p L.
  ///
  /// \param L External storage for the option value.
  LocationClass(Ty &L) : Loc(L) {}

  /// Bind option \p O to store its value in the external location.
  ///
  /// \param O Option that should use external storage.
  template <class Opt> void apply(Opt &O) const { O.setLocation(O, Loc); }
};

/// Build a \c LocationClass modifier that stores the option value in \p L
/// instead of in the option object itself.
///
/// \param L External variable that receives the parsed option value.
/// \return A LocationClass modifier bound to \p L.
template <class Ty> LocationClass<Ty> location(Ty &L) {
  return LocationClass<Ty>(L);
}

/// Option modifier that assigns an option to \p Category for \c -help grouping.
struct cat {
  /// Help category that should include the option.
  OptionCategory &Category;

  /// Construct a category modifier for \p c.
  ///
  /// \param c Category to associate with the option.
  cat(OptionCategory &c) : Category(c) {}

  /// Add option \p O to this modifier's category.
  ///
  /// \param O Option that receives the category.
  template <class Opt> void apply(Opt &O) const { O.addCategory(Category); }
};

/// Option modifier that assigns an option to a subcommand or subcommand group.
struct sub {
  /// Subcommand that should include the option, if set.
  SubCommand *Sub = nullptr;
  /// Subcommand group whose members should include the option, if set.
  SubCommandGroup *Group = nullptr;

  /// Construct a modifier that attaches the option to subcommand \p S.
  ///
  /// \param S Subcommand that should include the option.
  sub(SubCommand &S) : Sub(&S) {}
  /// Construct a modifier that attaches the option to every command in \p G.
  ///
  /// \param G Group of subcommands that should include the option.
  sub(SubCommandGroup &G) : Group(&G) {}

  /// Attach option \p O to the configured subcommand or group.
  ///
  /// \param O Option that receives the subcommand association.
  template <class Opt> void apply(Opt &O) const {
    if (Sub)
      O.addSubCommand(*Sub);
    else if (Group)
      for (SubCommand *SC : Group->getSubCommands())
        O.addSubCommand(*SC);
  }
};

/// Option modifier that registers a callback invoked when the option is parsed.
template <typename R, typename Ty> struct cb {
  /// Callback invoked with the parsed option value.
  std::function<R(Ty)> CB;

  /// Construct a callback modifier from \p CB.
  ///
  /// \param CB Function invoked when the option is parsed.
  cb(std::function<R(Ty)> CB) : CB(CB) {}

  /// Register this modifier's callback on option \p O.
  ///
  /// \param O Option that receives the callback.
  template <typename Opt> void apply(Opt &O) const { O.setCallback(CB); }
};

namespace detail {
template <typename F>
struct callback_traits : public callback_traits<decltype(&F::operator())> {};

template <typename R, typename C, typename... Args>
struct callback_traits<R (C::*)(Args...) const> {
  using result_type = R;
  using arg_type = std::tuple_element_t<0, std::tuple<Args...>>;
  static_assert(sizeof...(Args) == 1, "callback function must have one and only one parameter");
  static_assert(std::is_same_v<result_type, void>,
                "callback return type must be void");
  static_assert(std::is_lvalue_reference_v<arg_type> &&
                    std::is_const_v<std::remove_reference_t<arg_type>>,
                "callback arg_type must be a const lvalue reference");
};
} // namespace detail

/// Build a \c cb modifier from callable \p CB.
///
/// \param CB Unary callback invoked when the option is parsed.
/// \return A cb modifier wrapping \p CB.
template <typename F>
cb<typename detail::callback_traits<F>::result_type,
   typename detail::callback_traits<F>::arg_type>
callback(F CB) {
  using result_type = typename detail::callback_traits<F>::result_type;
  using arg_type = typename detail::callback_traits<F>::arg_type;
  return cb<result_type, arg_type>(CB);
}

//===----------------------------------------------------------------------===//

/// Type-erased base for comparing and printing stored option values.
struct LLVM_ABI GenericOptionValue {
  /// Return whether this stored option value equals \p V.
  ///
  /// \param V Other generic option value to compare against.
  /// \return True if the stored values are considered equal.
  virtual bool compare(const GenericOptionValue &V) const = 0;

protected:
  /// Construct an empty generic option value.
  GenericOptionValue() = default;
  /// Copy-construct from another generic option value.
  ///
  /// \param Other Value to copy.
  GenericOptionValue(const GenericOptionValue &Other) = default;
  /// Copy-assign from another generic option value.
  ///
  /// \param Other Value to assign from.
  /// \return A reference to this object.
  GenericOptionValue &operator=(const GenericOptionValue &Other) = default;
  /// Destroy the generic option value.
  ~GenericOptionValue() = default;

private:
  /// Provide a key function for this polymorphic base.
  virtual void anchor();
};

/// Typed holder for an option's assigned value (specialized per data type).
template <class DataType> struct OptionValue;

/// Default \c OptionValue implementation used when no value has been assigned.
///
/// The default value safely does nothing. Option value printing is only
/// best-effort.
template <class DataType, bool isClass>
struct OptionValueBase : GenericOptionValue {
  /// Wrapper type used when passing this value through templates.
  using WrapperType = OptionValue<DataType>;

  /// Return false; the empty base never holds a value.
  ///
  /// \return Always false.
  bool hasValue() const { return false; }

  /// Unreachable; the empty base has no stored value.
  ///
  /// \return Never returns; always unreachable.
  const DataType &getValue() const { llvm_unreachable("no default value"); }

  /// Ignore \p V; the empty base cannot store a value.
  ///
  /// \param V Value that would be stored in a real option value.
  template <class DT> void setValue(const DT &V) {}

  /// Always return false; the empty base never matches \p V.
  ///
  /// \param V Value to compare against.
  /// \return Always false.
  bool compare(const DataType &V) const { return false; }

  /// Always returns false; the empty option value never matches.
  ///
  /// \param V Other generic option value to compare against.
  /// \return Always false.
  bool compare(const GenericOptionValue &V) const override {
    return false;
  }

protected:
  /// Destroy the empty option-value base.
  ~OptionValueBase() = default;
};

/// Stores a typed copy of an option's assigned value.
template <class DataType> class OptionValueCopy : public GenericOptionValue {
  DataType Value;
  bool Valid = false;

protected:
  /// Copy-construct from another stored option value.
  ///
  /// \param Other Value to copy.
  OptionValueCopy(const OptionValueCopy &Other) = default;
  /// Copy-assign from another stored option value.
  ///
  /// \param Other Value to assign from.
  /// \return A reference to this object.
  OptionValueCopy &operator=(const OptionValueCopy &Other) = default;
  /// Destroy the stored option value.
  ~OptionValueCopy() = default;

public:
  /// Construct an empty stored value; \c hasValue() is false until assigned.
  OptionValueCopy() = default;

  /// Return whether a value has been assigned to this storage.
  ///
  /// \return True if a value has been assigned.
  bool hasValue() const { return Valid; }

  /// Return the stored value; requires \c hasValue() to be true.
  ///
  /// \return The stored typed value.
  const DataType &getValue() const {
    assert(Valid && "invalid option value");
    return Value;
  }

  /// Assign \p V as the stored option value.
  ///
  /// \param V Value to store.
  void setValue(const DataType &V) {
    Valid = true;
    Value = V;
  }

  /// Return whether this instance matches \p V.
  ///
  /// \param V Value to compare against.
  /// \return True if a value is set and equals \p V.
  bool compare(const DataType &V) const { return Valid && (Value == V); }

  /// Return whether this instance matches generic option value \p V.
  ///
  /// \param V Other generic option value to compare against.
  /// \return True if both hold a value and the values are equal.
  bool compare(const GenericOptionValue &V) const override {
    const OptionValueCopy<DataType> &VC =
        static_cast<const OptionValueCopy<DataType> &>(V);
    if (!VC.hasValue())
      return false;
    return compare(VC.getValue());
  }
};

/// Option-value base specialization for non-class (POD-like) data types.
template <class DataType>
struct OptionValueBase<DataType, false> : OptionValueCopy<DataType> {
  /// Non-class values are passed by the data type itself.
  using WrapperType = DataType;

protected:
  /// Construct an empty non-class option value.
  OptionValueBase() = default;
  /// Copy-construct from another non-class option value.
  ///
  /// \param Other Value to copy.
  OptionValueBase(const OptionValueBase &Other) = default;
  /// Copy-assign from another non-class option value.
  ///
  /// \param Other Value to assign from.
  /// \return A reference to this object.
  OptionValueBase &operator=(const OptionValueBase &Other) = default;
  /// Destroy the non-class option value.
  ~OptionValueBase() = default;
};

/// Top-level typed storage for an option's assigned value.
template <class DataType>
struct OptionValue final
    : OptionValueBase<DataType, std::is_class_v<DataType>> {
  /// Construct an empty option value.
  OptionValue() = default;

  /// Construct and assign \p V as the stored option value.
  ///
  /// \param V Initial value to store.
  OptionValue(const DataType &V) { this->setValue(V); }

  /// Assign \p V to the stored option value, converting types when needed.
  ///
  /// \param V Value to assign.
  /// \return A reference to this option value.
  template <class DT> OptionValue<DataType> &operator=(const DT &V) {
    this->setValue(V);
    return *this;
  }
};

/// Tri-state boolean used by options that may be unset, true, or false.
enum class boolOrDefault {
  /// No explicit value has been provided.
  BOU_UNSET,
  /// The option was set to true.
  BOU_TRUE,
  /// The option was set to false.
  BOU_FALSE
};
/// Option-value specialization for \c cl::boolOrDefault.
template <>
struct LLVM_ABI OptionValue<cl::boolOrDefault> final
    : OptionValueCopy<cl::boolOrDefault> {
  /// Wrapper type used when passing this value through templates.
  using WrapperType = cl::boolOrDefault;

  /// Construct an empty \c boolOrDefault option value.
  OptionValue() = default;

  /// Construct and assign \p V as the stored value.
  ///
  /// \param V Initial \c boolOrDefault value.
  OptionValue(const cl::boolOrDefault &V) { this->setValue(V); }

  /// Assign \p V to the stored \c boolOrDefault value.
  ///
  /// \param V Value to assign.
  /// \return A reference to this option value.
  OptionValue<cl::boolOrDefault> &operator=(const cl::boolOrDefault &V) {
    setValue(V);
    return *this;
  }

private:
  /// Provide a key function for this specialization.
  void anchor() override;
};

/// Option-value specialization for \c std::string using \c StringRef wrappers.
template <>
struct LLVM_ABI OptionValue<std::string> final : OptionValueCopy<std::string> {
  /// Wrapper type used when passing this value through templates.
  using WrapperType = StringRef;

  /// Construct an empty string option value.
  OptionValue() = default;

  /// Construct and assign \p V as the stored string.
  ///
  /// \param V Initial string value.
  OptionValue(const std::string &V) { this->setValue(V); }

  /// Assign \p V to the stored string value.
  ///
  /// \param V String to assign.
  /// \return A reference to this option value.
  OptionValue<std::string> &operator=(const std::string &V) {
    setValue(V);
    return *this;
  }

private:
  /// Provide a key function for this specialization.
  void anchor() override;
};

//===----------------------------------------------------------------------===//
// Enum valued command line option
//

/// A single named enum alternative for an enum-valued command-line option.
struct OptionEnumValue {
  /// Literal name recognized on the command line.
  StringRef Name;
  /// Integer value associated with this enum alternative.
  int Value;
  /// Help text describing this enum value in \c -help output.
  StringRef Description;
};

#define clEnumVal(ENUMVAL, DESC)                                               \
  llvm::cl::OptionEnumValue { #ENUMVAL, int(ENUMVAL), DESC }
#define clEnumValN(ENUMVAL, FLAGNAME, DESC)                                    \
  llvm::cl::OptionEnumValue { FLAGNAME, int(ENUMVAL), DESC }

/// Modifier that registers a set of literal enum values on an option parser.
///
/// For custom data types, allow specifying a group of values together as the
/// values that go into the mapping that the option handler uses.
class ValuesClass {
  // Use a vector instead of a map, because the lists should be short,
  // the overhead is less, and most importantly, it keeps them in the order
  // inserted so we can print our option out nicely.
  SmallVector<OptionEnumValue, 4> Values;

public:
  /// Construct from the enum alternatives in \p Options.
  ///
  /// \param Options Literal enum values to register with the parser.
  ValuesClass(std::initializer_list<OptionEnumValue> Options)
      : Values(Options) {}

  /// Register each literal value on option \p O's parser.
  ///
  /// \param O Option whose parser receives the literal mappings.
  template <class Opt> void apply(Opt &O) const {
    for (const auto &Value : Values)
      O.getParser().addLiteralOption(Value.Name, Value.Value,
                                     Value.Description);
  }
};

/// Helper to build a ValuesClass by forwarding a variable number of arguments
/// as an initializer list to the ValuesClass constructor.
///
/// \param Options Enum value descriptors to include in the mapping.
/// \return A ValuesClass holding the forwarded enum alternatives.
template <typename... OptsTy> ValuesClass values(OptsTy... Options) {
  return ValuesClass({Options...});
}

//===----------------------------------------------------------------------===//
// Parameterizable parser for different data types. By default, known data types
// (string, int, bool) have specialized parsers, that do what you would expect.
// The default parser, used for data types that are not built-in, uses a mapping
// table to map specific options to values, which is used, among other things,
// to handle enum types.

//--------------------------------------------------
/// Non-template base for mapping parsers that register literal option values.
///
/// This class holds all the non-generic code that we do not need replicated for
/// every instance of the generic parser.  This also allows us to put stuff into
/// CommandLine.cpp.
class LLVM_ABI generic_parser_base {
protected:
  /// Name and help text for one literal alternative in a mapping parser.
  class GenericOptionInfo {
  public:
    /// Construct info for literal \p name with help \p helpStr.
    ///
    /// \param name Command-line spelling of this alternative.
    /// \param helpStr Help text shown for this alternative.
    GenericOptionInfo(StringRef name, StringRef helpStr)
        : Name(name), HelpStr(helpStr) {}
    /// Command-line spelling of this alternative.
    StringRef Name;
    /// Help text shown for this alternative.
    StringRef HelpStr;
  };

public:
  /// Construct a generic parser owned by option \p O.
  ///
  /// \param O Option that owns this parser.
  generic_parser_base(Option &O) : Owner(O) {}

  /// Destroy the generic parser base.
  virtual ~generic_parser_base() = default;

  /// Return how many literal alternatives are registered.
  ///
  /// \return The number of registered literal alternatives.
  virtual unsigned getNumOptions() const = 0;

  /// Return the command-line name of alternative \p N.
  ///
  /// \param N Index of the alternative.
  /// \return The command-line name of alternative \p N.
  virtual StringRef getOption(unsigned N) const = 0;

  /// Return the help description for option entry \p N.
  ///
  /// \param N Index of the alternative.
  /// \return The help description for alternative \p N.
  virtual StringRef getDescription(unsigned N) const = 0;

  /// Return the width of the option tag for printing.
  ///
  /// \param O Option whose tag width is computed.
  /// \return The width of the option tag in characters.
  virtual size_t getOptionWidth(const Option &O) const;

  /// Return the stored value associated with alternative \p N.
  ///
  /// \param N Index of the alternative.
  /// \return The generic option value for alternative \p N.
  virtual const GenericOptionValue &getOptionValue(unsigned N) const = 0;

  /// Print help information about option \p O using \p GlobalWidth.
  ///
  /// \param O Option being described.
  /// \param GlobalWidth Column width reserved for option tags.
  virtual void printOptionInfo(const Option &O, size_t GlobalWidth) const;

  /// Print how value \p V differs from \p Default for option \p O.
  ///
  /// \param O Option whose value is printed.
  /// \param V Current option value.
  /// \param Default Default option value.
  /// \param GlobalWidth Column width reserved for option tags.
  void printGenericOptionDiff(const Option &O, const GenericOptionValue &V,
                              const GenericOptionValue &Default,
                              size_t GlobalWidth) const;

  /// Print the value of an option and its default.
  ///
  /// Template definition ensures that the option and default have the same
  /// DataType (via the same AnyOptionValue).
  ///
  /// \param O Option whose value is printed.
  /// \param V Current typed option value.
  /// \param Default Default typed option value.
  /// \param GlobalWidth Column width reserved for option tags.
  template <class AnyOptionValue>
  void printOptionDiff(const Option &O, const AnyOptionValue &V,
                       const AnyOptionValue &Default,
                       size_t GlobalWidth) const {
    printGenericOptionDiff(O, V, Default, GlobalWidth);
  }

  /// Perform any parser-specific initialization after registration.
  void initialize() {}

  /// Append any additional option names this parser recognizes to \p OptionNames.
  ///
  /// \param OptionNames Destination for extra option name strings.
  void getExtraOptionNames(SmallVectorImpl<StringRef> &OptionNames) {
    // If there has been no argstr specified, that means that we need to add an
    // argument for every possible option.  This ensures that our options are
    // vectored to us.
    if (!Owner.hasArgStr())
      for (unsigned i = 0, e = getNumOptions(); i != e; ++i)
        OptionNames.push_back(getOption(i));
  }

  /// Return whether this parser expects, forbids, or optionally takes a value.
  ///
  /// \return The default ValueExpected policy for this parser.
  enum ValueExpected getValueExpectedFlagDefault() const {
    // If there is an ArgStr specified, then we are of the form:
    //
    //    -opt=O2   or   -opt O2  or  -optO2
    //
    // In which case, the value is required.  Otherwise if an arg str has not
    // been specified, we are of the form:
    //
    //    -O2 or O2 or -la (where -l and -a are separate options)
    //
    // If this is the case, we cannot allow a value.
    //
    if (Owner.hasArgStr())
      return ValueRequired;
    else
      return ValueDisallowed;
  }

  /// Look up the index of the option named \p Name.
  ///
  /// Return the option number corresponding to the specified argument string.
  /// If the option is not found, getNumOptions() is returned.
  ///
  /// \param Name Literal option name to look up.
  /// \return The index of \p Name, or getNumOptions() if not found.
  unsigned findOption(StringRef Name);

protected:
  /// Option that owns this parser instance.
  Option &Owner;
};

//--------------------------------------------------
/// Default mapping parser for custom / enum-like option data types.
///
/// This implementation depends on having a mapping of recognized options to
/// values of some sort.  In addition to this, each entry in the mapping also
/// tracks a help message that is printed with the command line option for
/// -help.  Because this is a simple mapping parser, the data type can be any
/// unsupported type.
template <class DataType> class parser : public generic_parser_base {
protected:
  /// One literal name/value/help entry in the mapping table.
  class OptionInfo : public GenericOptionInfo {
  public:
    /// Construct a mapping entry for \p name with value \p v and help \p helpStr.
    ///
    /// \param name Command-line spelling of this alternative.
    /// \param v Typed value associated with this alternative.
    /// \param helpStr Help text shown for this alternative.
    OptionInfo(StringRef name, DataType v, StringRef helpStr)
        : GenericOptionInfo(name, helpStr), V(v) {}

    /// Stored typed value for this alternative.
    OptionValue<DataType> V;
  };
  /// Registered literal alternatives for this parser.
  SmallVector<OptionInfo, 8> Values;

public:
  /// Construct a mapping parser owned by option \p O.
  ///
  /// \param O Option that owns this parser.
  parser(Option &O) : generic_parser_base(O) {}

  /// Data type produced by a successful parse.
  using parser_data_type = DataType;

  /// Return how many literal alternatives are registered.
  ///
  /// \return The number of registered literal alternatives.
  unsigned getNumOptions() const override { return unsigned(Values.size()); }
  /// Return the command-line name of alternative \p N.
  ///
  /// \param N Index of the alternative.
  /// \return The command-line name of alternative \p N.
  StringRef getOption(unsigned N) const override { return Values[N].Name; }
  /// Return the help description for alternative \p N.
  ///
  /// \param N Index of the alternative.
  /// \return The help description for alternative \p N.
  StringRef getDescription(unsigned N) const override {
    return Values[N].HelpStr;
  }

  /// Return the stored value associated with alternative \p N.
  ///
  /// \param N Index of the alternative.
  /// \return The generic option value for alternative \p N.
  const GenericOptionValue &getOptionValue(unsigned N) const override {
    return Values[N].V;
  }

  /// Parse \p Arg / \p ArgName into \p V using the literal mapping table.
  ///
  /// \param O Option being parsed (used for error reporting).
  /// \param ArgName Option name as written on the command line.
  /// \param Arg Option value string, when an ArgStr is present.
  /// \param V Destination for the parsed typed value.
  /// \return True on error.
  bool parse(Option &O, StringRef ArgName, StringRef Arg, DataType &V) {
    StringRef ArgVal;
    if (Owner.hasArgStr())
      ArgVal = Arg;
    else
      ArgVal = ArgName;

    for (size_t i = 0, e = Values.size(); i != e; ++i)
      if (Values[i].Name == ArgVal) {
        V = Values[i].V.getValue();
        return false;
      }

    return O.error("Cannot find option named '" + ArgVal + "'!");
  }

  /// Add an entry to the mapping table.
  ///
  /// \param Name Command-line spelling of the alternative.
  /// \param V Typed value associated with \p Name.
  /// \param HelpStr Help text shown for this alternative.
  template <class DT>
  void addLiteralOption(StringRef Name, const DT &V, StringRef HelpStr) {
#ifndef NDEBUG
    if (findOption(Name) != Values.size())
      report_fatal_error("Option '" + Name + "' already exists!");
#endif
    OptionInfo X(Name, static_cast<DataType>(V), HelpStr);
    Values.push_back(X);
    AddLiteralOption(Owner, Name);
  }

  /// Remove the specified option.
  ///
  /// \param Name Command-line spelling of the alternative to remove.
  void removeLiteralOption(StringRef Name) {
    unsigned N = findOption(Name);
    assert(N != Values.size() && "Option not found!");
    Values.erase(Values.begin() + N);
  }
};

//--------------------------------------------------
/// Non-template base implementing shared option-parser boilerplate.
class LLVM_ABI
    basic_parser_impl { // non-template implementation of basic_parser<t>
public:
  /// Construct a basic parser associated with option \p O.
  ///
  /// \param O Option that owns this parser.
  basic_parser_impl(Option &O) {}

  /// Destroy the basic parser implementation.
  virtual ~basic_parser_impl() = default;

  /// Return the default value-expected policy for basic parsers.
  ///
  /// \return Always ValueRequired.
  enum ValueExpected getValueExpectedFlagDefault() const {
    return ValueRequired;
  }

  /// Append any additional option names; basic parsers add none.
  ///
  /// \param OptionNames Destination for extra option name strings.
  void getExtraOptionNames(SmallVectorImpl<StringRef> &OptionNames) {}

  /// Perform any parser-specific initialization after registration.
  void initialize() {}

  /// Return the width of the option tag for printing.
  ///
  /// \param O Option whose tag width is computed.
  /// \return The width of the option tag in characters.
  size_t getOptionWidth(const Option &O) const;

  /// Print help information about option \p O using \p GlobalWidth.
  ///
  /// \param O Option being described.
  /// \param GlobalWidth Column width reserved for option tags.
  void printOptionInfo(const Option &O, size_t GlobalWidth) const;

  /// Print a placeholder when this option type cannot show a value diff.
  ///
  /// \param O Option whose value would be printed.
  /// \param GlobalWidth Column width reserved for option tags.
  void printOptionNoValue(const Option &O, size_t GlobalWidth) const;

  /// Return the placeholder name used for this option's value in help text.
  ///
  /// \return The value placeholder name (default \c "value").
  virtual StringRef getValueName() const { return "value"; }

  /// Provide a key function for this polymorphic base.
  virtual void anchor();

protected:
  /// Print the option name padded to \p GlobalWidth for value-diff output.
  ///
  /// \param O Option whose name is printed.
  /// \param GlobalWidth Column width reserved for option tags.
  void printOptionName(const Option &O, size_t GlobalWidth) const;
};

/// Typed wrapper around \c basic_parser_impl for parsed data type \p DataType.
template <class DataType> class basic_parser : public basic_parser_impl {
public:
  /// Data type produced by a successful parse.
  using parser_data_type = DataType;
  /// Option-value wrapper type for \p DataType.
  using OptVal = OptionValue<DataType>;

  /// Construct a typed basic parser owned by option \p O.
  ///
  /// \param O Option that owns this parser.
  basic_parser(Option &O) : basic_parser_impl(O) {}
};

//--------------------------------------------------

/// Explicit instantiation declaration for \c basic_parser<bool>.
extern template class LLVM_TEMPLATE_ABI basic_parser<bool>;

/// Parser specialization that reads boolean command-line option values.
template <> class LLVM_ABI parser<bool> : public basic_parser<bool> {
public:
  /// Construct a bool parser owned by option \p O.
  ///
  /// \param O Option that owns this parser.
  parser(Option &O) : basic_parser(O) {}

  /// Parse a bool value from the argument string; return true on error.
  ///
  /// \param O Option being parsed (used for error reporting).
  /// \param ArgName Option name as written on the command line.
  /// \param Arg Raw argument text to parse.
  /// \param Val Destination for the parsed value.
  /// \return True on error.
  bool parse(Option &O, StringRef ArgName, StringRef Arg, bool &Val);

  /// Perform any parser-specific initialization after registration.
  void initialize() {}

  /// Return that a value is optional for this boolean-like parser.
  ///
  /// \return Always ValueOptional.
  enum ValueExpected getValueExpectedFlagDefault() const {
    return ValueOptional;
  }

  /// Do not print a \c =<value> placeholder for this option type.
  ///
  /// \return An empty string.
  StringRef getValueName() const override { return StringRef(); }

  /// Print how value \p V differs from \p Default for option \p O.
  ///
  /// \param O Option whose value is printed.
  /// \param V Current option value.
  /// \param Default Default option value.
  /// \param GlobalWidth Column width reserved for option tags.
  void printOptionDiff(const Option &O, bool V, OptVal Default,
                       size_t GlobalWidth) const;

  /// Provide a key function for this parser specialization.
  void anchor() override;
};

//--------------------------------------------------

/// Explicit instantiation declaration for \c basic_parser<boolOrDefault>.
extern template class LLVM_TEMPLATE_ABI basic_parser<boolOrDefault>;

/// Parser specialization that reads \c boolOrDefault command-line values.
template <>
class LLVM_ABI parser<boolOrDefault> : public basic_parser<boolOrDefault> {
public:
  /// Construct a boolOrDefault parser owned by option \p O.
  ///
  /// \param O Option that owns this parser.
  parser(Option &O) : basic_parser(O) {}

  /// Parse a boolOrDefault value from the argument string; return true on error.
  ///
  /// \param O Option being parsed (used for error reporting).
  /// \param ArgName Option name as written on the command line.
  /// \param Arg Raw argument text to parse.
  /// \param Val Destination for the parsed value.
  /// \return True on error.
  bool parse(Option &O, StringRef ArgName, StringRef Arg, boolOrDefault &Val);

  /// Return that a value is optional for this boolean-like parser.
  ///
  /// \return Always ValueOptional.
  enum ValueExpected getValueExpectedFlagDefault() const {
    return ValueOptional;
  }

  /// Do not print a \c =<value> placeholder for this option type.
  ///
  /// \return An empty string.
  StringRef getValueName() const override { return StringRef(); }

  /// Print how value \p V differs from \p Default for option \p O.
  ///
  /// \param O Option whose value is printed.
  /// \param V Current option value.
  /// \param Default Default option value.
  /// \param GlobalWidth Column width reserved for option tags.
  void printOptionDiff(const Option &O, boolOrDefault V, OptVal Default,
                       size_t GlobalWidth) const;

  /// Provide a key function for this parser specialization.
  void anchor() override;
};

//--------------------------------------------------

/// Explicit instantiation declaration for \c basic_parser<int>.
extern template class LLVM_TEMPLATE_ABI basic_parser<int>;

/// Parser specialization that reads signed \c int command-line values.
template <> class LLVM_ABI parser<int> : public basic_parser<int> {
public:
  /// Construct a int parser owned by option \p O.
  ///
  /// \param O Option that owns this parser.
  parser(Option &O) : basic_parser(O) {}

  /// Parse a int value from the argument string; return true on error.
  ///
  /// \param O Option being parsed (used for error reporting).
  /// \param ArgName Option name as written on the command line.
  /// \param Arg Raw argument text to parse.
  /// \param Val Destination for the parsed value.
  /// \return True on error.
  bool parse(Option &O, StringRef ArgName, StringRef Arg, int &Val);

  /// Return the help placeholder name for this option value.
  ///
  /// \return The placeholder string \c "int".
  StringRef getValueName() const override { return "int"; }

  /// Print how value \p V differs from \p Default for option \p O.
  ///
  /// \param O Option whose value is printed.
  /// \param V Current option value.
  /// \param Default Default option value.
  /// \param GlobalWidth Column width reserved for option tags.
  void printOptionDiff(const Option &O, int V, OptVal Default,
                       size_t GlobalWidth) const;

  /// Provide a key function for this parser specialization.
  void anchor() override;
};

//--------------------------------------------------

/// Explicit instantiation declaration for \c basic_parser<long>.
extern template class LLVM_TEMPLATE_ABI basic_parser<long>;

/// Parser specialization that reads \c long command-line values.
template <> class LLVM_ABI parser<long> final : public basic_parser<long> {
public:
  /// Construct a long parser owned by option \p O.
  ///
  /// \param O Option that owns this parser.
  parser(Option &O) : basic_parser(O) {}

  /// Parse a long value from the argument string; return true on error.
  ///
  /// \param O Option being parsed (used for error reporting).
  /// \param ArgName Option name as written on the command line.
  /// \param Arg Raw argument text to parse.
  /// \param Val Destination for the parsed value.
  /// \return True on error.
  bool parse(Option &O, StringRef ArgName, StringRef Arg, long &Val);

  /// Return the help placeholder name for this option value.
  ///
  /// \return The placeholder string \c "long".
  StringRef getValueName() const override { return "long"; }

  /// Print how value \p V differs from \p Default for option \p O.
  ///
  /// \param O Option whose value is printed.
  /// \param V Current option value.
  /// \param Default Default option value.
  /// \param GlobalWidth Column width reserved for option tags.
  void printOptionDiff(const Option &O, long V, OptVal Default,
                       size_t GlobalWidth) const;

  /// Provide a key function for this parser specialization.
  void anchor() override;
};

//--------------------------------------------------

/// Explicit instantiation declaration for \c basic_parser<long long>.
extern template class LLVM_TEMPLATE_ABI basic_parser<long long>;

/// Parser specialization that reads \c long long command-line values.
template <> class LLVM_ABI parser<long long> : public basic_parser<long long> {
public:
  /// Construct a long long parser owned by option \p O.
  ///
  /// \param O Option that owns this parser.
  parser(Option &O) : basic_parser(O) {}

  /// Parse a long long value from the argument string; return true on error.
  ///
  /// \param O Option being parsed (used for error reporting).
  /// \param ArgName Option name as written on the command line.
  /// \param Arg Raw argument text to parse.
  /// \param Val Destination for the parsed value.
  /// \return True on error.
  bool parse(Option &O, StringRef ArgName, StringRef Arg, long long &Val);

  /// Return the help placeholder name for this option value.
  ///
  /// \return The placeholder string \c "long".
  StringRef getValueName() const override { return "long"; }

  /// Print how value \p V differs from \p Default for option \p O.
  ///
  /// \param O Option whose value is printed.
  /// \param V Current option value.
  /// \param Default Default option value.
  /// \param GlobalWidth Column width reserved for option tags.
  void printOptionDiff(const Option &O, long long V, OptVal Default,
                       size_t GlobalWidth) const;

  /// Provide a key function for this parser specialization.
  void anchor() override;
};

//--------------------------------------------------

/// Explicit instantiation declaration for \c basic_parser<unsigned>.
extern template class LLVM_TEMPLATE_ABI basic_parser<unsigned>;

/// Parser specialization that reads \c unsigned command-line values.
template <> class LLVM_ABI parser<unsigned> : public basic_parser<unsigned> {
public:
  /// Construct a unsigned parser owned by option \p O.
  ///
  /// \param O Option that owns this parser.
  parser(Option &O) : basic_parser(O) {}

  /// Parse a unsigned value from the argument string; return true on error.
  ///
  /// \param O Option being parsed (used for error reporting).
  /// \param ArgName Option name as written on the command line.
  /// \param Arg Raw argument text to parse.
  /// \param Val Destination for the parsed value.
  /// \return True on error.
  bool parse(Option &O, StringRef ArgName, StringRef Arg, unsigned &Val);

  /// Return the help placeholder name for this option value.
  ///
  /// \return The placeholder string \c "uint".
  StringRef getValueName() const override { return "uint"; }

  /// Print how value \p V differs from \p Default for option \p O.
  ///
  /// \param O Option whose value is printed.
  /// \param V Current option value.
  /// \param Default Default option value.
  /// \param GlobalWidth Column width reserved for option tags.
  void printOptionDiff(const Option &O, unsigned V, OptVal Default,
                       size_t GlobalWidth) const;

  /// Provide a key function for this parser specialization.
  void anchor() override;
};

//--------------------------------------------------

/// Explicit instantiation declaration for \c basic_parser<unsigned long>.
extern template class LLVM_TEMPLATE_ABI basic_parser<unsigned long>;

/// Parser specialization that reads \c unsigned long command-line values.
template <>
class LLVM_ABI parser<unsigned long> final
    : public basic_parser<unsigned long> {
public:
  /// Construct a unsigned long parser owned by option \p O.
  ///
  /// \param O Option that owns this parser.
  parser(Option &O) : basic_parser(O) {}

  /// Parse a unsigned long value from the argument string; return true on error.
  ///
  /// \param O Option being parsed (used for error reporting).
  /// \param ArgName Option name as written on the command line.
  /// \param Arg Raw argument text to parse.
  /// \param Val Destination for the parsed value.
  /// \return True on error.
  bool parse(Option &O, StringRef ArgName, StringRef Arg, unsigned long &Val);

  /// Return the help placeholder name for this option value.
  ///
  /// \return The placeholder string \c "ulong".
  StringRef getValueName() const override { return "ulong"; }

  /// Print how value \p V differs from \p Default for option \p O.
  ///
  /// \param O Option whose value is printed.
  /// \param V Current option value.
  /// \param Default Default option value.
  /// \param GlobalWidth Column width reserved for option tags.
  void printOptionDiff(const Option &O, unsigned long V, OptVal Default,
                       size_t GlobalWidth) const;

  /// Provide a key function for this parser specialization.
  void anchor() override;
};

//--------------------------------------------------

/// Explicit instantiation declaration for \c basic_parser<unsigned long long>.
extern template class LLVM_TEMPLATE_ABI basic_parser<unsigned long long>;

/// Parser specialization that reads \c unsigned long long values.
template <>
class LLVM_ABI parser<unsigned long long>
    : public basic_parser<unsigned long long> {
public:
  /// Construct a unsigned long long parser owned by option \p O.
  ///
  /// \param O Option that owns this parser.
  parser(Option &O) : basic_parser(O) {}

  /// Parse a unsigned long long value from the argument string; return true on error.
  ///
  /// \param O Option being parsed (used for error reporting).
  /// \param ArgName Option name as written on the command line.
  /// \param Arg Raw argument text to parse.
  /// \param Val Destination for the parsed value.
  /// \return True on error.
  bool parse(Option &O, StringRef ArgName, StringRef Arg,
             unsigned long long &Val);

  /// Return the help placeholder name for this option value.
  ///
  /// \return The placeholder string \c "ulong".
  StringRef getValueName() const override { return "ulong"; }

  /// Print how value \p V differs from \p Default for option \p O.
  ///
  /// \param O Option whose value is printed.
  /// \param V Current option value.
  /// \param Default Default option value.
  /// \param GlobalWidth Column width reserved for option tags.
  void printOptionDiff(const Option &O, unsigned long long V, OptVal Default,
                       size_t GlobalWidth) const;

  /// Provide a key function for this parser specialization.
  void anchor() override;
};

//--------------------------------------------------

/// Explicit instantiation declaration for \c basic_parser<double>.
extern template class LLVM_TEMPLATE_ABI basic_parser<double>;

/// Parser specialization that reads \c double command-line values.
template <> class LLVM_ABI parser<double> : public basic_parser<double> {
public:
  /// Construct a double parser owned by option \p O.
  ///
  /// \param O Option that owns this parser.
  parser(Option &O) : basic_parser(O) {}

  /// Parse a double value from the argument string; return true on error.
  ///
  /// \param O Option being parsed (used for error reporting).
  /// \param ArgName Option name as written on the command line.
  /// \param Arg Raw argument text to parse.
  /// \param Val Destination for the parsed value.
  /// \return True on error.
  bool parse(Option &O, StringRef ArgName, StringRef Arg, double &Val);

  /// Return the help placeholder name for this option value.
  ///
  /// \return The placeholder string \c "number".
  StringRef getValueName() const override { return "number"; }

  /// Print how value \p V differs from \p Default for option \p O.
  ///
  /// \param O Option whose value is printed.
  /// \param V Current option value.
  /// \param Default Default option value.
  /// \param GlobalWidth Column width reserved for option tags.
  void printOptionDiff(const Option &O, double V, OptVal Default,
                       size_t GlobalWidth) const;

  /// Provide a key function for this parser specialization.
  void anchor() override;
};

//--------------------------------------------------

/// Explicit instantiation declaration for \c basic_parser<float>.
extern template class LLVM_TEMPLATE_ABI basic_parser<float>;

/// Parser specialization that reads \c float command-line values.
template <> class LLVM_ABI parser<float> : public basic_parser<float> {
public:
  /// Construct a float parser owned by option \p O.
  ///
  /// \param O Option that owns this parser.
  parser(Option &O) : basic_parser(O) {}

  /// Parse a float value from the argument string; return true on error.
  ///
  /// \param O Option being parsed (used for error reporting).
  /// \param ArgName Option name as written on the command line.
  /// \param Arg Raw argument text to parse.
  /// \param Val Destination for the parsed value.
  /// \return True on error.
  bool parse(Option &O, StringRef ArgName, StringRef Arg, float &Val);

  /// Return the help placeholder name for this option value.
  ///
  /// \return The placeholder string \c "number".
  StringRef getValueName() const override { return "number"; }

  /// Print how value \p V differs from \p Default for option \p O.
  ///
  /// \param O Option whose value is printed.
  /// \param V Current option value.
  /// \param Default Default option value.
  /// \param GlobalWidth Column width reserved for option tags.
  void printOptionDiff(const Option &O, float V, OptVal Default,
                       size_t GlobalWidth) const;

  /// Provide a key function for this parser specialization.
  void anchor() override;
};

//--------------------------------------------------

/// Explicit instantiation declaration for \c basic_parser<std::string>.
extern template class LLVM_TEMPLATE_ABI basic_parser<std::string>;

/// Parser specialization that reads \c std::string command-line values.
template <>
class LLVM_ABI parser<std::string> : public basic_parser<std::string> {
public:
  /// Construct a string parser owned by option \p O.
  ///
  /// \param O Option that owns this parser.
  parser(Option &O) : basic_parser(O) {}

  /// Parse a string value from \p Arg into \p Value.
  ///
  /// \param O Unused option (error reporting not needed for strings).
  /// \param ArgName Unused option name.
  /// \param Arg Raw argument text to store.
  /// \param Value Destination for the parsed string.
  /// \return Always false (success).
  bool parse(Option &O, StringRef ArgName, StringRef Arg, std::string &Value) {
    Value = Arg.str();
    return false;
  }

  /// Return the help placeholder name for this option value.
  ///
  /// \return The placeholder string \c "string".
  StringRef getValueName() const override { return "string"; }

  /// Print how value \p V differs from \p Default for option \p O.
  ///
  /// \param O Option whose value is printed.
  /// \param V Current option value.
  /// \param Default Default option value.
  /// \param GlobalWidth Column width reserved for option tags.
  void printOptionDiff(const Option &O, StringRef V, const OptVal &Default,
                       size_t GlobalWidth) const;

  /// Provide a key function for this parser specialization.
  void anchor() override;
};

//--------------------------------------------------

/// Parser specialization that reads optional string command-line values.
template <>
class LLVM_ABI parser<std::optional<std::string>>
    : public basic_parser<std::optional<std::string>> {
public:
  /// Construct an optional-string parser owned by option \p O.
  ///
  /// \param O Option that owns this parser.
  parser(Option &O) : basic_parser(O) {}

  /// Parse an optional string from \p Arg into \p Value.
  ///
  /// \param O Unused option (error reporting not needed for strings).
  /// \param ArgName Unused option name.
  /// \param Arg Raw argument text to store.
  /// \param Value Destination for the parsed optional string.
  /// \return Always false (success).
  bool parse(Option &O, StringRef ArgName, StringRef Arg,
             std::optional<std::string> &Value) {
    Value = Arg.str();
    return false;
  }

  /// Return the help placeholder name for this option value.
  ///
  /// \return The placeholder string \c "optional string".
  StringRef getValueName() const override { return "optional string"; }

  /// Print how value \p V differs from \p Default for option \p O.
  ///
  /// \param O Option whose value is printed.
  /// \param V Current option value.
  /// \param Default Default option value.
  /// \param GlobalWidth Column width reserved for option tags.
  void printOptionDiff(const Option &O, std::optional<StringRef> V,
                       const OptVal &Default, size_t GlobalWidth) const;

  /// Provide a key function for this parser specialization.
  void anchor() override;
};

//--------------------------------------------------

/// Explicit instantiation declaration for \c basic_parser<char>.
extern template class LLVM_TEMPLATE_ABI basic_parser<char>;

/// Parser specialization that reads a single \c char command-line value.
template <> class LLVM_ABI parser<char> : public basic_parser<char> {
public:
  /// Construct a char parser owned by option \p O.
  ///
  /// \param O Option that owns this parser.
  parser(Option &O) : basic_parser(O) {}

  /// Parse the first character of \p Arg into \p Value.
  ///
  /// \param O Unused option (error reporting not needed for chars).
  /// \param ArgName Unused option name.
  /// \param Arg Raw argument text; first character is used.
  /// \param Value Destination for the parsed character.
  /// \return Always false (success).
  bool parse(Option &O, StringRef ArgName, StringRef Arg, char &Value) {
    Value = Arg[0];
    return false;
  }

  /// Return the help placeholder name for this option value.
  ///
  /// \return The placeholder string \c "char".
  StringRef getValueName() const override { return "char"; }

  /// Print how value \p V differs from \p Default for option \p O.
  ///
  /// \param O Option whose value is printed.
  /// \param V Current option value.
  /// \param Default Default option value.
  /// \param GlobalWidth Column width reserved for option tags.
  void printOptionDiff(const Option &O, char V, OptVal Default,
                       size_t GlobalWidth) const;

  /// Provide a key function for this parser specialization.
  void anchor() override;
};

//--------------------------------------------------

/// Explicit instantiation declaration for \c basic_parser<ElementCount>.
extern template class LLVM_TEMPLATE_ABI basic_parser<ElementCount>;

/// Parser specialization that reads \c ElementCount command-line values.
template <>
class LLVM_ABI parser<ElementCount> : public basic_parser<ElementCount> {
public:
  /// Construct a ElementCount parser owned by option \p O.
  ///
  /// \param O Option that owns this parser.
  parser(Option &O) : basic_parser(O) {}

  /// Parse a ElementCount value from the argument string; return true on error.
  ///
  /// \param O Option being parsed (used for error reporting).
  /// \param ArgName Option name as written on the command line.
  /// \param Arg Raw argument text to parse.
  /// \param Value Destination for the parsed value.
  /// \return True on error.
  bool parse(Option &O, StringRef ArgName, StringRef Arg, ElementCount &Value);

  /// Return the help placeholder name for this option value.
  ///
  /// \return The placeholder string \c "ElementCount".
  StringRef getValueName() const override { return "ElementCount"; }

  /// Print how value \p V differs from \p Default for option \p O.
  ///
  /// \param O Option whose value is printed.
  /// \param V Current option value.
  /// \param Default Default option value.
  /// \param GlobalWidth Column width reserved for option tags.
  void printOptionDiff(const Option &O, ElementCount V, OptVal Default,
                       size_t GlobalWidth) const;

  /// Provide a key function for this parser specialization.
  void anchor() override;
};

//--------------------------------------------------
// This collection of wrappers is the intermediary between class opt and class
// parser to handle all the template nastiness.

/// Print a value diff for a generic (mapping) parser.
///
/// \param O Option whose value is printed.
/// \param P Generic parser used to format the diff.
/// \param V Current option value.
/// \param Default Default option value.
/// \param GlobalWidth Column width reserved for option tags.
template <class ParserClass, class DT>
void printOptionDiff(const Option &O, const generic_parser_base &P, const DT &V,
                     const OptionValue<DT> &Default, size_t GlobalWidth) {
  OptionValue<DT> OV = V;
  P.printOptionDiff(O, OV, Default, GlobalWidth);
}

/// Diff printer used when the parsed type differs from the option value type.
template <class ParserDT, class ValDT> struct OptionDiffPrinter {
  /// Print a placeholder because typed diffing is unavailable for \p V.
  ///
  /// \param O Option whose value would be printed.
  /// \param P Parser that cannot print a typed value diff.
  /// \param V Current option value (unused when types differ).
  /// \param Default Default option value (unused when types differ).
  /// \param GlobalWidth Column width reserved for option tags.
  void print(const Option &O, const parser<ParserDT> &P, const ValDT &V,
             const OptionValue<ValDT> &Default, size_t GlobalWidth) {
    P.printOptionNoValue(O, GlobalWidth);
  }
};

/// Diff printer used when the parsed type matches the option value type.
template <class DT> struct OptionDiffPrinter<DT, DT> {
  /// Print how value \p V differs from \p Default using parser \p P.
  ///
  /// \param O Option whose value is printed.
  /// \param P Parser that formats the typed value diff.
  /// \param V Current option value.
  /// \param Default Default option value.
  /// \param GlobalWidth Column width reserved for option tags.
  void print(const Option &O, const parser<DT> &P, const DT &V,
             const OptionValue<DT> &Default, size_t GlobalWidth) {
    P.printOptionDiff(O, V, Default, GlobalWidth);
  }
};

/// Print how the parsed value \p V differs from \p Default for option \p O,
/// dispatching to the appropriate \c OptionDiffPrinter specialization.
///
/// \param O Option whose value is printed.
/// \param P Typed basic parser used to format the value diff.
/// \param V Current option value.
/// \param Default Default option value.
/// \param GlobalWidth Column width reserved for option tags.
template <class ParserClass, class ValDT>
void printOptionDiff(
    const Option &O,
    const basic_parser<typename ParserClass::parser_data_type> &P,
    const ValDT &V, const OptionValue<ValDT> &Default, size_t GlobalWidth) {

  OptionDiffPrinter<typename ParserClass::parser_data_type, ValDT> printer;
  printer.print(O, static_cast<const ParserClass &>(P), V, Default,
                GlobalWidth);
}

//===----------------------------------------------------------------------===//
/// Helper that applies a command-line option modifier to an option object.
///
/// This class is used because we must use partial specialization to handle
/// literal string arguments specially (const char* does not correctly respond to
/// the apply method). Because the syntax to use this is a pain, we have the
/// 'apply' method below to handle the nastiness...
template <class Mod> struct applicator {
  /// Apply modifier \p M to option \p O via \c M.apply(O).
  ///
  /// \param M Modifier to apply.
  /// \param O Option that receives the modifier.
  template <class Opt> static void opt(const Mod &M, Opt &O) { M.apply(O); }
};

/// Applicator specialization that treats a string literal as an option name.
template <unsigned n> struct applicator<char[n]> {
  /// Set option \p O's argument string from \p Str.
  ///
  /// \param Str Option name string.
  /// \param O Option that receives the name.
  template <class Opt> static void opt(StringRef Str, Opt &O) {
    O.setArgStr(Str);
  }
};
/// Applicator specialization that treats a const string literal as an option name.
template <unsigned n> struct applicator<const char[n]> {
  /// Set option \p O's argument string from \p Str.
  ///
  /// \param Str Option name string.
  /// \param O Option that receives the name.
  template <class Opt> static void opt(StringRef Str, Opt &O) {
    O.setArgStr(Str);
  }
};
/// Applicator specialization that treats a \c StringRef as an option name.
template <> struct applicator<StringRef > {
  /// Set option \p O's argument string from \p Str.
  ///
  /// \param Str Option name string.
  /// \param O Option that receives the name.
  template <class Opt> static void opt(StringRef Str, Opt &O) {
    O.setArgStr(Str);
  }
};

/// Applicator specialization for \c NumOccurrencesFlag modifiers.
template <> struct applicator<NumOccurrencesFlag> {
  /// Apply occurrence flag \p N to option \p O.
  ///
  /// \param N Occurrence policy to set.
  /// \param O Option that receives the flag.
  static void opt(NumOccurrencesFlag N, Option &O) {
    O.setNumOccurrencesFlag(N);
  }
};

/// Applicator specialization for \c ValueExpected modifiers.
template <> struct applicator<ValueExpected> {
  /// Apply value-expected flag \p VE to option \p O.
  ///
  /// \param VE Value-expected policy to set.
  /// \param O Option that receives the flag.
  static void opt(ValueExpected VE, Option &O) { O.setValueExpectedFlag(VE); }
};

/// Applicator specialization for \c OptionHidden modifiers.
template <> struct applicator<OptionHidden> {
  /// Apply hiddenness flag \p OH to option \p O.
  ///
  /// \param OH Help-visibility policy to set.
  /// \param O Option that receives the flag.
  static void opt(OptionHidden OH, Option &O) { O.setHiddenFlag(OH); }
};

/// Applicator specialization for \c FormattingFlags modifiers.
template <> struct applicator<FormattingFlags> {
  /// Apply formatting flag \p FF to option \p O.
  ///
  /// \param FF Formatting policy to set.
  /// \param O Option that receives the flag.
  static void opt(FormattingFlags FF, Option &O) { O.setFormattingFlag(FF); }
};

/// Applicator specialization for \c MiscFlags modifiers.
template <> struct applicator<MiscFlags> {
  /// Apply miscellaneous flag \p MF to option \p O.
  ///
  /// \param MF Miscellaneous parsing flag to set.
  /// \param O Option that receives the flag.
  static void opt(MiscFlags MF, Option &O) {
    assert((MF != Grouping || O.ArgStr.size() == 1) &&
           "cl::Grouping can only apply to single character Options.");
    O.setMiscFlag(MF);
  }
};

/// Apply \p M and remaining modifiers to option \p O.
///
/// \param O Option that receives the modifiers.
/// \param M First modifier to apply.
/// \param Ms Remaining modifiers to apply.
template <class Opt, class Mod, class... Mods>
void apply(Opt *O, const Mod &M, const Mods &... Ms) {
  applicator<Mod>::opt(M, *O);
  apply(O, Ms...);
}

/// Apply modifier \p M to option \p O.
///
/// \param O Option that receives the modifier.
/// \param M Modifier to apply.
template <class Opt, class Mod> void apply(Opt *O, const Mod &M) {
  applicator<Mod>::opt(M, *O);
}

//===----------------------------------------------------------------------===//
/// External-storage helper for \c cl::opt; stores the value via \c cl::location.
///
/// This implementation assumes the user will specify a variable to store the
/// data into with the cl::location(x) modifier.
template <class DataType, bool ExternalStorage, bool isClass>
class opt_storage {
  DataType *Location = nullptr; // Where to store the object...
  OptionValue<DataType> Default;

  void check_location() const {
    assert(Location && "cl::location(...) not specified for a command "
                       "line option with external storage, "
                       "or cl::init specified before cl::location()!!");
  }

public:
  /// Construct empty external option storage.
  opt_storage() = default;

  /// Point storage for this option at \p L; reports an error if already set.
  ///
  /// \param O Option used for error reporting.
  /// \param L External variable that receives the parsed value.
  /// \return True if location was already set; false on success.
  bool setLocation(Option &O, DataType &L) {
    if (Location)
      return O.error("cl::location(x) specified more than once!");
    Location = &L;
    Default = L;
    return false;
  }

  /// Assign \p V into external storage; when \p initial, also record default.
  ///
  /// \param V Value to store.
  /// \param initial Whether \p V should also become the default.
  template <class T> void setValue(const T &V, bool initial = false) {
    check_location();
    *Location = V;
    if (initial)
      Default = V;
  }

  /// Return a mutable reference to the externally stored value.
  ///
  /// \return A mutable reference to the externally stored value.
  DataType &getValue() {
    check_location();
    return *Location;
  }
  /// Return a const reference to the externally stored value.
  ///
  /// \return A const reference to the externally stored value.
  const DataType &getValue() const {
    check_location();
    return *Location;
  }

  /// Implicitly convert to the stored data type.
  ///
  /// \return The externally stored value.
  operator DataType() const { return this->getValue(); }

  /// Return the default value recorded for this option.
  ///
  /// \return The recorded default option value.
  const OptionValue<DataType> &getDefault() const { return Default; }
};

/// Internal storage for class-typed \c cl::opt values via inheritance.
///
/// Since we can inherit from a class, we do so. This makes us exactly
/// compatible with the object in all cases that it is used.
template <class DataType>
class opt_storage<DataType, false, true> : public DataType {
public:
  /// Default value recorded for this option.
  OptionValue<DataType> Default;

  /// Assign \p V into this storage; when \p initial, also record default.
  ///
  /// \param V Value to store.
  /// \param initial Whether \p V should also become the default.
  template <class T> void setValue(const T &V, bool initial = false) {
    DataType::operator=(V);
    if (initial)
      Default = V;
  }

  /// Return a mutable reference to the stored class value.
  ///
  /// \return A mutable reference to the stored class value.
  DataType &getValue() { return *this; }
  /// Return a const reference to the stored class value.
  ///
  /// \return A const reference to the stored class value.
  const DataType &getValue() const { return *this; }

  /// Return the default value recorded for this option.
  ///
  /// \return The recorded default option value.
  const OptionValue<DataType> &getDefault() const { return Default; }
};

/// Internal storage for non-class \c cl::opt values via containment.
///
/// In this case, we store an instance through containment, and overload
/// operators to get at the value.
template <class DataType> class opt_storage<DataType, false, false> {
public:
  /// Contained option value.
  DataType Value;
  /// Default value recorded for this option.
  OptionValue<DataType> Default;

  /// Construct storage with a default-constructed value.
  opt_storage() : Value(DataType()), Default() {}

  /// Assign \p V into this storage; when \p initial, also record default.
  ///
  /// \param V Value to store.
  /// \param initial Whether \p V should also become the default.
  template <class T> void setValue(const T &V, bool initial = false) {
    Value = V;
    if (initial)
      Default = V;
  }
  /// Return a mutable reference to the contained value.
  ///
  /// \return A mutable reference to the contained value.
  DataType &getValue() { return Value; }
  /// Return a copy of the contained value.
  ///
  /// \return A copy of the contained value.
  DataType getValue() const { return Value; }

  /// Return the default value recorded for this option.
  ///
  /// \return The recorded default option value.
  const OptionValue<DataType> &getDefault() const { return Default; }

  /// Implicitly convert to the stored data type.
  ///
  /// \return The contained value.
  operator DataType() const { return getValue(); }

  /// If the datatype is a pointer, support \c -> on it.
  ///
  /// \return The contained pointer (or pointer-like) value.
  DataType operator->() const { return Value; }
};

//===----------------------------------------------------------------------===//
/// A scalar command-line option storing a single parsed value of type \p DataType.
template <class DataType, bool ExternalStorage = false,
          class ParserClass = parser<DataType>>
class opt
    : public Option,
      public opt_storage<DataType, ExternalStorage, std::is_class_v<DataType>> {
  ParserClass Parser;

  bool handleOccurrence(unsigned pos, StringRef ArgName,
                        StringRef Arg) override {
    typename ParserClass::parser_data_type Val =
        typename ParserClass::parser_data_type();
    if (Parser.parse(*this, ArgName, Arg, Val))
      return true; // Parse error!
    this->setValue(Val);
    this->setPosition(pos);
    if (Callback)
      Callback(Val);
    return false;
  }

  enum ValueExpected getValueExpectedFlagDefault() const override {
    return Parser.getValueExpectedFlagDefault();
  }

  void getExtraOptionNames(SmallVectorImpl<StringRef> &OptionNames) override {
    return Parser.getExtraOptionNames(OptionNames);
  }

  // Forward printing stuff to the parser...
  size_t getOptionWidth() const override {
    return Parser.getOptionWidth(*this);
  }

  void printOptionInfo(size_t GlobalWidth) const override {
    Parser.printOptionInfo(*this, GlobalWidth);
  }

  void printOptionValue(size_t GlobalWidth, bool Force) const override {
    if (Force || !this->getDefault().compare(this->getValue())) {
      cl::printOptionDiff<ParserClass>(*this, Parser, this->getValue(),
                                       this->getDefault(), GlobalWidth);
    }
  }

  void setDefault() override {
    if constexpr (std::is_assignable_v<DataType &, DataType>) {
      const OptionValue<DataType> &V = this->getDefault();
      if (V.hasValue())
        this->setValue(V.getValue());
      else
        this->setValue(DataType());
    }
  }

  void done() {
    addArgument();
    Parser.initialize();
  }

public:
  /// Command line options are registered singletons and must not be copied.
  ///
  /// \param Other Unused source option.
  opt(const opt &Other) = delete;
  /// Command line options are registered singletons and must not be copied.
  ///
  /// \param Other Unused source option.
  opt &operator=(const opt &Other) = delete;

  /// Set the initial default value used by the \c cl::init modifier.
  ///
  /// \param V Initial value to record as default.
  void setInitialValue(const DataType &V) { this->setValue(V, true); }

  /// Return the parser used to interpret this option's argument text.
  ///
  /// \return A reference to this option's parser.
  ParserClass &getParser() { return Parser; }

  /// Assign \p Val to this option and invoke the optional callback.
  ///
  /// \param Val Value to assign.
  /// \return A reference to the stored option value.
  template <class T> DataType &operator=(const T &Val) {
    this->setValue(Val);
    if (Callback)
      Callback(Val);
    return this->getValue();
  }

  /// Move-assign \p Val to this option and invoke the optional callback.
  ///
  /// \param Val Value to move-assign.
  /// \return A reference to the stored option value.
  template <class T> DataType &operator=(T &&Val) {
    this->getValue() = std::forward<T>(Val);
    if (Callback)
      Callback(this->getValue());
    return this->getValue();
  }

  /// Construct an optional-value command-line option, applying modifiers \p Ms.
  ///
  /// \param Ms Option modifiers such as name, init, desc, and category.
  template <class... Mods>
  explicit opt(const Mods &... Ms)
      : Option(llvm::cl::Optional, NotHidden), Parser(*this) {
    apply(this, Ms...);
    done();
  }

  /// Register \p CB to run whenever this option receives a new parsed value.
  ///
  /// \param CB Callback invoked with each newly parsed value.
  void setCallback(
      std::function<void(const typename ParserClass::parser_data_type &)> CB) {
    Callback = CB;
  }

  /// Optional function invoked whenever this option receives a new value.
  std::function<void(const typename ParserClass::parser_data_type &)> Callback;
};

#if !(defined(LLVM_ENABLE_LLVM_EXPORT_ANNOTATIONS) && defined(_MSC_VER))
// Only instantiate opt<std::string> when not building a Windows DLL. When
// exporting opt<std::string>, MSVC implicitly exports symbols for
// std::basic_string through transitive inheritance via std::string. These
// symbols may appear in clients, leading to duplicate symbol conflicts.
/// Explicit instantiation declaration for \c opt<std::string>.
extern template class LLVM_TEMPLATE_ABI opt<std::string>;
#endif

/// Explicit instantiation declaration for \c opt<unsigned>.
extern template class LLVM_TEMPLATE_ABI opt<unsigned>;
/// Explicit instantiation declaration for \c opt<int>.
extern template class LLVM_TEMPLATE_ABI opt<int>;
/// Explicit instantiation declaration for \c opt<char>.
extern template class LLVM_TEMPLATE_ABI opt<char>;
/// Explicit instantiation declaration for \c opt<bool>.
extern template class LLVM_TEMPLATE_ABI opt<bool>;

//===----------------------------------------------------------------------===//
// Default storage class definition: external storage.  This implementation
// assumes the user will specify a variable to store the data into with the
// cl::location(x) modifier.
//
/// External-storage helper for \c cl::list; appends parsed values via
/// \c cl::location.
template <class DataType, class StorageClass> class list_storage {
  StorageClass *Location = nullptr; // Where to store the object...
  std::vector<OptionValue<DataType>> Default =
      std::vector<OptionValue<DataType>>();
  bool DefaultAssigned = false;

public:
  /// Construct empty external list storage.
  list_storage() = default;

  /// Clear is a no-op for external list storage.
  void clear() {}

  /// Point storage for this list at \p L; reports an error if already set.
  ///
  /// \param O Option used for error reporting.
  /// \param L External container that receives parsed values.
  /// \return True if location was already set; false on success.
  bool setLocation(Option &O, StorageClass &L) {
    if (Location)
      return O.error("cl::location(x) specified more than once!");
    Location = &L;
    return false;
  }

  /// Append \p V to the external list; when \p initial, also record it as default.
  ///
  /// \param V Value to append.
  /// \param initial Whether \p V should also be recorded as a default element.
  template <class T> void addValue(const T &V, bool initial = false) {
    assert(Location != nullptr &&
           "cl::location(...) not specified for a command "
           "line option with external storage!");
    Location->push_back(V);
    if (initial)
      Default.push_back(V);
  }

  /// Return the recorded default elements for this list option.
  ///
  /// \return The recorded default elements.
  const std::vector<OptionValue<DataType>> &getDefault() const {
    return Default;
  }

  /// Mark that default elements have been assigned.
  void assignDefault() { DefaultAssigned = true; }
  /// Clear the default-assigned marker so new values replace defaults.
  void overwriteDefault() { DefaultAssigned = false; }
  /// Return whether default elements have been assigned.
  ///
  /// \return True if default elements have been assigned.
  bool isDefaultAssigned() { return DefaultAssigned; }
};

/// Internal vector-like storage for \c cl::list option values.
///
/// Originally this code inherited from std::vector. In transitioning to a new
/// API for command line options we should change this. The new implementation
/// of this list_storage specialization implements the minimum subset of the
/// std::vector API required for all the current clients.
///
/// FIXME: Reduce this API to a more narrow subset of std::vector
template <class DataType> class list_storage<DataType, bool> {
  std::vector<DataType> Storage;
  std::vector<OptionValue<DataType>> Default;
  bool DefaultAssigned = false;

public:
  /// Iterator over stored list elements.
  using iterator = typename std::vector<DataType>::iterator;

  /// Return an iterator to the first stored element.
  ///
  /// \return An iterator to the first stored element.
  iterator begin() { return Storage.begin(); }
  /// Return an iterator past the last stored element.
  ///
  /// \return An iterator past the last stored element.
  iterator end() { return Storage.end(); }

  /// Const iterator over stored list elements.
  using const_iterator = typename std::vector<DataType>::const_iterator;

  /// Return a const iterator to the first stored element.
  ///
  /// \return A const iterator to the first stored element.
  const_iterator begin() const { return Storage.begin(); }
  /// Return a const iterator past the last stored element.
  ///
  /// \return A const iterator past the last stored element.
  const_iterator end() const { return Storage.end(); }

  /// Size type used by the underlying vector.
  using size_type = typename std::vector<DataType>::size_type;

  /// Return how many elements are currently stored.
  ///
  /// \return The number of stored elements.
  size_type size() const { return Storage.size(); }

  /// Return whether the list currently holds no elements.
  ///
  /// \return True if the list is empty.
  bool empty() const { return Storage.empty(); }

  /// Append \p value to the stored list.
  ///
  /// \param value Element to append.
  void push_back(const DataType &value) { Storage.push_back(value); }
  /// Append moved \p value to the stored list.
  ///
  /// \param value Element to move-append.
  void push_back(DataType &&value) { Storage.push_back(value); }

  /// Mutable reference to a stored element.
  using reference = typename std::vector<DataType>::reference;
  /// Const reference to a stored element.
  using const_reference = typename std::vector<DataType>::const_reference;

  /// Return the element at index \p pos.
  ///
  /// \param pos Zero-based element index.
  /// \return A mutable reference to the element at \p pos.
  reference operator[](size_type pos) { return Storage[pos]; }
  /// Return the const element at index \p pos.
  ///
  /// \param pos Zero-based element index.
  /// \return A const reference to the element at \p pos.
  const_reference operator[](size_type pos) const { return Storage[pos]; }

  /// Remove all stored elements.
  void clear() {
    Storage.clear();
  }

  /// Erase the element at const iterator \p pos.
  ///
  /// \param pos Element to erase.
  /// \return An iterator following the erased element.
  iterator erase(const_iterator pos) { return Storage.erase(pos); }
  /// Erase the half-open range \p first to \p last.
  ///
  /// \param first Start of the range to erase.
  /// \param last End of the range to erase.
  /// \return An iterator following the last erased element.
  iterator erase(const_iterator first, const_iterator last) {
    return Storage.erase(first, last);
  }

  /// Erase the element at iterator \p pos.
  ///
  /// \param pos Element to erase.
  /// \return An iterator following the erased element.
  iterator erase(iterator pos) { return Storage.erase(pos); }
  /// Erase the half-open range \p first to \p last.
  ///
  /// \param first Start of the range to erase.
  /// \param last End of the range to erase.
  /// \return An iterator following the last erased element.
  iterator erase(iterator first, iterator last) {
    return Storage.erase(first, last);
  }

  /// Insert \p value before const iterator \p pos.
  ///
  /// \param pos Insertion position.
  /// \param value Element to insert.
  /// \return An iterator to the inserted element.
  iterator insert(const_iterator pos, const DataType &value) {
    return Storage.insert(pos, value);
  }
  /// Insert moved \p value before const iterator \p pos.
  ///
  /// \param pos Insertion position.
  /// \param value Element to move-insert.
  /// \return An iterator to the inserted element.
  iterator insert(const_iterator pos, DataType &&value) {
    return Storage.insert(pos, value);
  }

  /// Insert \p value before iterator \p pos.
  ///
  /// \param pos Insertion position.
  /// \param value Element to insert.
  /// \return An iterator to the inserted element.
  iterator insert(iterator pos, const DataType &value) {
    return Storage.insert(pos, value);
  }
  /// Insert moved \p value before iterator \p pos.
  ///
  /// \param pos Insertion position.
  /// \param value Element to move-insert.
  /// \return An iterator to the inserted element.
  iterator insert(iterator pos, DataType &&value) {
    return Storage.insert(pos, value);
  }

  /// Return a mutable reference to the first element.
  ///
  /// \return A mutable reference to the first element.
  reference front() { return Storage.front(); }
  /// Return a const reference to the first element.
  ///
  /// \return A const reference to the first element.
  const_reference front() const { return Storage.front(); }

  /// Convert to a mutable reference to the underlying vector.
  ///
  /// \return A mutable reference to the underlying vector.
  operator std::vector<DataType> &() { return Storage; }
  /// Convert to an \c ArrayRef view of the stored elements.
  ///
  /// \return An ArrayRef view of the stored elements.
  operator ArrayRef<DataType>() const { return Storage; }
  /// Return a pointer to the underlying vector.
  ///
  /// \return A pointer to the underlying vector.
  std::vector<DataType> *operator&() { return &Storage; }
  /// Return a const pointer to the underlying vector.
  ///
  /// \return A const pointer to the underlying vector.
  const std::vector<DataType> *operator&() const { return &Storage; }

  /// Append \p V; when \p initial, also record it as a default element.
  ///
  /// \param V Value to append.
  /// \param initial Whether \p V should also be recorded as a default element.
  template <class T> void addValue(const T &V, bool initial = false) {
    Storage.push_back(V);
    if (initial)
      Default.push_back(OptionValue<DataType>(V));
  }

  /// Return the recorded default elements for this list option.
  ///
  /// \return The recorded default elements.
  const std::vector<OptionValue<DataType>> &getDefault() const {
    return Default;
  }

  /// Mark that default elements have been assigned.
  void assignDefault() { DefaultAssigned = true; }
  /// Clear the default-assigned marker so new values replace defaults.
  void overwriteDefault() { DefaultAssigned = false; }
  /// Return whether default elements have been assigned.
  ///
  /// \return True if default elements have been assigned.
  bool isDefaultAssigned() { return DefaultAssigned; }
};

//===----------------------------------------------------------------------===//
/// A list command-line option that accumulates parsed values of type \p DataType.
template <class DataType, class StorageClass = bool,
          class ParserClass = parser<DataType>>
class list : public Option, public list_storage<DataType, StorageClass> {
  std::vector<unsigned> Positions;
  ParserClass Parser;

  enum ValueExpected getValueExpectedFlagDefault() const override {
    return Parser.getValueExpectedFlagDefault();
  }

  void getExtraOptionNames(SmallVectorImpl<StringRef> &OptionNames) override {
    return Parser.getExtraOptionNames(OptionNames);
  }

  bool handleOccurrence(unsigned pos, StringRef ArgName,
                        StringRef Arg) override {
    typename ParserClass::parser_data_type Val =
        typename ParserClass::parser_data_type();
    if (list_storage<DataType, StorageClass>::isDefaultAssigned()) {
      clear();
      list_storage<DataType, StorageClass>::overwriteDefault();
    }
    if (Parser.parse(*this, ArgName, Arg, Val))
      return true; // Parse Error!
    list_storage<DataType, StorageClass>::addValue(Val);
    setPosition(pos);
    Positions.push_back(pos);
    if (Callback)
      Callback(Val);
    return false;
  }

  // Forward printing stuff to the parser...
  size_t getOptionWidth() const override {
    return Parser.getOptionWidth(*this);
  }

  void printOptionInfo(size_t GlobalWidth) const override {
    Parser.printOptionInfo(*this, GlobalWidth);
  }

  // Unimplemented: list options don't currently store their default value.
  void printOptionValue(size_t /*GlobalWidth*/, bool /*Force*/) const override {
  }

  void setDefault() override {
    Positions.clear();
    list_storage<DataType, StorageClass>::clear();
    for (auto &Val : list_storage<DataType, StorageClass>::getDefault())
      list_storage<DataType, StorageClass>::addValue(Val.getValue());
  }

  void done() {
    addArgument();
    Parser.initialize();
  }

public:
  /// Command line options are registered singletons and must not be copied.
  ///
  /// \param Other Unused source list.
  list(const list &Other) = delete;
  /// Command line options are registered singletons and must not be copied.
  ///
  /// \param Other Unused source list.
  list &operator=(const list &Other) = delete;

  /// Return the parser used to interpret this list option's argument text.
  ///
  /// \return A reference to this list option's parser.
  ParserClass &getParser() { return Parser; }

  /// Return the argv position recorded for list element \p optnum.
  ///
  /// \param optnum Zero-based index of a parsed list element.
  /// \return The argv index recorded for element \p optnum.
  unsigned getPosition(unsigned optnum) const {
    assert(optnum < this->size() && "Invalid option index");
    return Positions[optnum];
  }

  /// Clear all parsed elements and recorded positions.
  void clear() {
    Positions.clear();
    list_storage<DataType, StorageClass>::clear();
  }

  /// Set default elements for this list option before parsing.
  ///
  /// \param Vs Default elements used by the \c cl::list_init modifier.
  void setInitialValues(ArrayRef<DataType> Vs) {
    assert(!(list_storage<DataType, StorageClass>::isDefaultAssigned()) &&
           "Cannot have two default values");
    list_storage<DataType, StorageClass>::assignDefault();
    for (auto &Val : Vs)
      list_storage<DataType, StorageClass>::addValue(Val, true);
  }

  /// Set how many additional values this multi-valued list consumes.
  ///
  /// \param n Number of additional values.
  void setNumAdditionalVals(unsigned n) { Option::setNumAdditionalVals(n); }

  /// Construct a zero-or-more list option, applying modifiers \p Ms.
  ///
  /// \param Ms Option modifiers such as name, desc, and category.
  template <class... Mods>
  explicit list(const Mods &... Ms)
      : Option(ZeroOrMore, NotHidden), Parser(*this) {
    apply(this, Ms...);
    done();
  }

  /// Register \p CB to run whenever this list receives a new parsed value.
  ///
  /// \param CB Callback invoked with each newly parsed value.
  void setCallback(
      std::function<void(const typename ParserClass::parser_data_type &)> CB) {
    Callback = CB;
  }

  /// Optional function invoked whenever this list receives a new value.
  std::function<void(const typename ParserClass::parser_data_type &)> Callback;
};

/// Modifier that sets how many additional values a \c list option consumes.
struct multi_val {
  /// Number of additional values consumed after the primary argument.
  unsigned AdditionalVals;
  /// Construct a multi-value modifier requesting \p N additional values.
  ///
  /// \param N Number of additional values.
  explicit multi_val(unsigned N) : AdditionalVals(N) {}

  /// Apply this modifier to list option \p L.
  ///
  /// \param L List option that receives the additional-value count.
  template <typename D, typename S, typename P>
  void apply(list<D, S, P> &L) const {
    L.setNumAdditionalVals(AdditionalVals);
  }
};

//===----------------------------------------------------------------------===//
// Default storage class definition: external storage.  This implementation
// assumes the user will specify a variable to store the data into with the
// cl::location(x) modifier.
//
/// Storage for a multi-bit enum option that updates an external unsigned bit mask.
template <class DataType, class StorageClass> class bits_storage {
  unsigned *Location = nullptr; // Where to store the bits...

  template <class T> static unsigned Bit(const T &V) {
    unsigned BitPos = static_cast<unsigned>(V);
    assert(BitPos < sizeof(unsigned) * CHAR_BIT &&
           "enum exceeds width of bit vector!");
    return 1 << BitPos;
  }

public:
  /// Construct empty external bits storage.
  bits_storage() = default;

  /// Bind external storage \p L for the bit vector updated by this option.
  ///
  /// \param O Option used for error reporting.
  /// \param L External unsigned bit mask to update.
  /// \return True if location was already set; false on success.
  bool setLocation(Option &O, unsigned &L) {
    if (Location)
      return O.error("cl::location(x) specified more than once!");
    Location = &L;
    return false;
  }

  /// Set the bit corresponding to enum value \p V in the external bit vector.
  ///
  /// \param V Enum value whose bit should be set.
  template <class T> void addValue(const T &V) {
    assert(Location != nullptr &&
           "cl::location(...) not specified for a command "
           "line option with external storage!");
    *Location |= Bit(V);
  }

  /// Return the current bit vector from the external storage location.
  ///
  /// \return The current external bit mask.
  unsigned getBits() { return *Location; }

  /// Clear all bits in the external storage location.
  void clear() {
    if (Location)
      *Location = 0;
  }

  /// Return whether the bit for enum value \p V is set.
  ///
  /// \param V Enum value whose bit is tested.
  /// \return True if the bit for \p V is set.
  template <class T> bool isSet(const T &V) {
    return (*Location & Bit(V)) != 0;
  }
};

/// Internal storage for a multi-bit enum option held inside the option object.
template <class DataType> class bits_storage<DataType, bool> {
  unsigned Bits{0}; // Where to store the bits...

  template <class T> static unsigned Bit(const T &V) {
    unsigned BitPos = static_cast<unsigned>(V);
    assert(BitPos < sizeof(unsigned) * CHAR_BIT &&
           "enum exceeds width of bit vector!");
    return 1 << BitPos;
  }

public:
  /// Set the bit corresponding to enum value \p V.
  ///
  /// \param V Enum value whose bit should be set.
  template <class T> void addValue(const T &V) { Bits |= Bit(V); }

  /// Return the current internal bit vector.
  ///
  /// \return The current internal bit mask.
  unsigned getBits() { return Bits; }

  /// Clear all bits in the internal bit vector.
  void clear() { Bits = 0; }

  /// Return whether the bit for enum value \p V is set.
  ///
  /// \param V Enum value whose bit is tested.
  /// \return True if the bit for \p V is set.
  template <class T> bool isSet(const T &V) { return (Bits & Bit(V)) != 0; }
};

//===----------------------------------------------------------------------===//
/// A bits command-line option that sets enum-valued flags in a bit vector.
template <class DataType, class Storage = bool,
          class ParserClass = parser<DataType>>
class bits : public Option, public bits_storage<DataType, Storage> {
  std::vector<unsigned> Positions;
  ParserClass Parser;

  enum ValueExpected getValueExpectedFlagDefault() const override {
    return Parser.getValueExpectedFlagDefault();
  }

  void getExtraOptionNames(SmallVectorImpl<StringRef> &OptionNames) override {
    return Parser.getExtraOptionNames(OptionNames);
  }

  bool handleOccurrence(unsigned pos, StringRef ArgName,
                        StringRef Arg) override {
    typename ParserClass::parser_data_type Val =
        typename ParserClass::parser_data_type();
    if (Parser.parse(*this, ArgName, Arg, Val))
      return true; // Parse Error!
    this->addValue(Val);
    setPosition(pos);
    Positions.push_back(pos);
    if (Callback)
      Callback(Val);
    return false;
  }

  // Forward printing stuff to the parser...
  size_t getOptionWidth() const override {
    return Parser.getOptionWidth(*this);
  }

  void printOptionInfo(size_t GlobalWidth) const override {
    Parser.printOptionInfo(*this, GlobalWidth);
  }

  // Unimplemented: bits options don't currently store their default values.
  void printOptionValue(size_t /*GlobalWidth*/, bool /*Force*/) const override {
  }

  void setDefault() override { bits_storage<DataType, Storage>::clear(); }

  void done() {
    addArgument();
    Parser.initialize();
  }

public:
  /// Command line options are registered singletons and must not be copied.
  ///
  /// \param Other Unused source bits option.
  bits(const bits &Other) = delete;
  /// Command line options are registered singletons and must not be copied.
  ///
  /// \param Other Unused source bits option.
  bits &operator=(const bits &Other) = delete;

  /// Return the parser used to interpret this bits option's argument text.
  ///
  /// \return A reference to this bits option's parser.
  ParserClass &getParser() { return Parser; }

  /// Return the argv position recorded for bits occurrence \p optnum.
  ///
  /// \param optnum Zero-based index of a parsed bits occurrence.
  /// \return The argv index recorded for occurrence \p optnum.
  unsigned getPosition(unsigned optnum) const {
    assert(optnum < this->size() && "Invalid option index");
    return Positions[optnum];
  }

  /// Construct a zero-or-more bits option, applying modifiers \p Ms.
  ///
  /// \param Ms Option modifiers such as name, desc, and category.
  template <class... Mods>
  explicit bits(const Mods &... Ms)
      : Option(ZeroOrMore, NotHidden), Parser(*this) {
    apply(this, Ms...);
    done();
  }

  /// Register \p CB to run whenever this bits option receives a new value.
  ///
  /// \param CB Callback invoked with each newly parsed value.
  void setCallback(
      std::function<void(const typename ParserClass::parser_data_type &)> CB) {
    Callback = CB;
  }

  /// Optional function invoked whenever this bits option receives a new value.
  std::function<void(const typename ParserClass::parser_data_type &)> Callback;
};

//===----------------------------------------------------------------------===//
/// Command-line alias that forwards occurrences to another registered option.
class LLVM_ABI alias : public Option {
  Option *AliasFor;

  bool handleOccurrence(unsigned pos, StringRef /*ArgName*/,
                        StringRef Arg) override {
    return AliasFor->handleOccurrence(pos, AliasFor->ArgStr, Arg);
  }

  bool addOccurrence(unsigned pos, StringRef /*ArgName*/, StringRef Value,
                     bool MultiArg = false) override {
    return AliasFor->addOccurrence(pos, AliasFor->ArgStr, Value, MultiArg);
  }

  // Handle printing stuff...
  size_t getOptionWidth() const override;
  void printOptionInfo(size_t GlobalWidth) const override;

  // Aliases do not need to print their values.
  void printOptionValue(size_t /*GlobalWidth*/, bool /*Force*/) const override {
  }

  void setDefault() override { AliasFor->setDefault(); }

  ValueExpected getValueExpectedFlagDefault() const override {
    return AliasFor->getValueExpectedFlag();
  }

  void done() {
    if (!hasArgStr())
      error("cl::alias must have argument name specified!");
    if (!AliasFor)
      error("cl::alias must have an cl::aliasopt(option) specified!");
    if (!Subs.empty())
      error("cl::alias must not have cl::sub(), aliased option's cl::sub() will be used!");
    Subs = AliasFor->Subs;
    Categories = AliasFor->Categories;
    addArgument();
  }

public:
  /// Command line aliases are registered singletons and must not be copied.
  ///
  /// \param Other Unused source alias.
  alias(const alias &Other) = delete;
  /// Command line aliases are registered singletons and must not be copied.
  ///
  /// \param Other Unused source alias.
  alias &operator=(const alias &Other) = delete;

  /// Set the canonical option that this alias forwards to.
  ///
  /// \param O Option that receives aliased occurrences.
  void setAliasFor(Option &O) {
    if (AliasFor)
      error("cl::alias must only have one cl::aliasopt(...) specified!");
    AliasFor = &O;
  }

  /// Construct a hidden optional alias, applying modifiers \p Ms.
  ///
  /// \param Ms Option modifiers including name and \c aliasopt.
  template <class... Mods>
  explicit alias(const Mods &... Ms)
      : Option(Optional, Hidden), AliasFor(nullptr) {
    apply(this, Ms...);
    done();
  }
};

/// Modifier that binds an \c alias to the canonical option it forwards to.
struct aliasopt {
  /// Canonical option that receives aliased occurrences.
  Option &Opt;

  /// Bind \p O as the canonical option that this alias forwards to.
  ///
  /// \param O Option that receives aliased occurrences.
  explicit aliasopt(Option &O) : Opt(O) {}

  /// Apply this binding to alias \p A.
  ///
  /// \param A Alias option that should forward to \c Opt.
  void apply(alias &A) const { A.setAliasFor(Opt); }
};

/// Modifier that appends \p help to the text printed after the standard
/// \c -help output when the program exits via the help path.
struct extrahelp {
  /// Extra help text printed after the standard \c -help output.
  StringRef morehelp;

  /// Register \p help to print after the standard \c -help text on exit.
  ///
  /// \param help Additional help text to append.
  LLVM_ABI explicit extrahelp(StringRef help);
};

/// Print the registered version message to the standard output stream.
LLVM_ABI void PrintVersionMessage();

/// This function just prints the help message, exactly the same way as if the
/// -help or -help-hidden option had been given on the command line.
///
/// \param Hidden if true will print hidden options
/// \param Categorized if true print options in categories
LLVM_ABI void PrintHelpMessage(bool Hidden = false, bool Categorized = false);

/// Return enabled LLVM build-configuration tags such as \c +assertions.
///
/// An array of optional enabled settings in the LLVM build configuration,
/// which may be of interest to compiler developers. For example, includes
/// "+assertions" if assertions are enabled. Used by printBuildConfig.
///
/// \return The enabled build-configuration tags.
LLVM_ABI ArrayRef<StringRef> getCompilerBuildConfig();

/// Print the compiler build configuration to \p OS.
///
/// Designed for compiler developers, not compiler end-users.
/// Intended to be used in --version output when enabled.
///
/// \param OS Stream that receives the build-configuration text.
LLVM_ABI void printBuildConfig(raw_ostream &OS);

//===----------------------------------------------------------------------===//
// Public interface for accessing registered options.
//

/// Return the map of registered named options for subcommand \p Sub.
///
/// Use this to get a map of all registered named options (e.g. -help).
///
/// \param Sub Subcommand whose registered options are returned.
/// \return A reference to the map used by the cl APIs to parse options.
///
/// Access to unnamed arguments (i.e. positional) are not provided because
/// it is expected that the client already has access to these.
///
/// Typical usage:
/// \code
/// main(int argc,char* argv[]) {
/// DenseMap<llvm::StringRef, llvm::cl::Option*> &opts =
///     llvm::cl::getRegisteredOptions();
/// assert(opts.count("help") == 1)
/// opts["help"]->setDescription("Show alphabetical help information")
/// // More code
/// llvm::cl::ParseCommandLineOptions(argc,argv);
/// //More code
/// }
/// \endcode
///
/// This interface is useful for modifying options in libraries that are out of
/// the control of the client. The options should be modified before calling
/// llvm::cl::ParseCommandLineOptions().
///
/// Hopefully this API can be deprecated soon. Any situation where options need
/// to be modified by tools or libraries should be handled by sane APIs rather
/// than just handing around a global list.
LLVM_ABI DenseMap<StringRef, Option *> &
getRegisteredOptions(SubCommand &Sub = SubCommand::getTopLevel());

/// Use this to get all registered SubCommands from the provided parser.
///
/// \return A range of all SubCommand pointers registered with the parser.
///
/// Typical usage:
/// \code
/// main(int argc, char* argv[]) {
///   llvm::cl::ParseCommandLineOptions(argc, argv);
///   for (auto* S : llvm::cl::getRegisteredSubcommands()) {
///     if (*S) {
///       std::cout << "Executing subcommand: " << S->getName() << std::endl;
///       // Execute some function based on the name...
///     }
///   }
/// }
/// \endcode
///
/// This interface is useful for defining subcommands in libraries and
/// the dispatch from a single point (like in the main function).
LLVM_ABI iterator_range<SmallPtrSet<SubCommand *, 4>::iterator>
getRegisteredSubcommands();

//===----------------------------------------------------------------------===//
// Standalone command line processing utilities.
//

/// Tokenize a GNU-style command line that may contain escapes and quotes.
///
/// The quoting rules match those used by GCC and other tools that use
/// libiberty's buildargv() or expandargv() utilities, and do not match bash.
/// They differ from buildargv() on treatment of backslashes that do not escape
/// a special character to make it possible to accept most Windows file paths.
///
/// \param [in] Source The string to be split on whitespace with quotes.
/// \param [in] Saver Delegates back to the caller for saving parsed strings.
/// \param [in] MarkEOLs true if tokenizing a response file and you want end of
/// lines and end of the response file to be marked with a nullptr string.
/// \param [out] NewArgv All parsed strings are appended to NewArgv.
LLVM_ABI void TokenizeGNUCommandLine(StringRef Source, StringSaver &Saver,
                                     SmallVectorImpl<const char *> &NewArgv,
                                     bool MarkEOLs = false);

/// Tokenizes a string of Windows command line arguments, which may contain
/// quotes and escaped quotes.
///
/// See MSDN docs for CommandLineToArgvW for information on the quoting rules.
/// http://msdn.microsoft.com/en-us/library/windows/desktop/17w5ykft(v=vs.85).aspx
///
/// For handling a full Windows command line including the executable name at
/// the start, see TokenizeWindowsCommandLineFull below.
///
/// \param [in] Source The string to be split on whitespace with quotes.
/// \param [in] Saver Delegates back to the caller for saving parsed strings.
/// \param [in] MarkEOLs true if tokenizing a response file and you want end of
/// lines and end of the response file to be marked with a nullptr string.
/// \param [out] NewArgv All parsed strings are appended to NewArgv.
LLVM_ABI void TokenizeWindowsCommandLine(StringRef Source, StringSaver &Saver,
                                         SmallVectorImpl<const char *> &NewArgv,
                                         bool MarkEOLs = false);

/// Tokenize a Windows command line while avoiding copies when possible.
///
/// If no quoting or escaping was used, this produces substrings of the original
/// string. If a token requires unquoting, it will be allocated with the
/// StringSaver.
///
/// \param Source Command-line text to tokenize.
/// \param Saver Allocator used when a token must be copied.
/// \param NewArgv Destination that receives the parsed tokens.
LLVM_ABI void
TokenizeWindowsCommandLineNoCopy(StringRef Source, StringSaver &Saver,
                                 SmallVectorImpl<StringRef> &NewArgv);

/// Tokenize a full Windows command line, including the leading command name.
///
/// This uses the same syntax rules as TokenizeWindowsCommandLine for all but
/// the first token. But the first token is expected to be parsed as the
/// executable file name in the way CreateProcess would do it, rather than the
/// way the C library startup code would do it: CreateProcess does not consider
/// that \ is ever an escape character (because " is not a valid filename char,
/// hence there's never a need to escape it to be used literally).
///
/// Parameters are the same as for TokenizeWindowsCommandLine. In particular,
/// if you set MarkEOLs = true, then the first word of every line will be
/// parsed using the special rules for command names, making this function
/// suitable for parsing a file full of commands to execute.
///
/// \param Source Full command-line text to tokenize.
/// \param Saver Allocator used for owning parsed token storage.
/// \param NewArgv Destination that receives the parsed tokens.
/// \param MarkEOLs When true, mark end-of-line boundaries with nullptrs.
LLVM_ABI void
TokenizeWindowsCommandLineFull(StringRef Source, StringSaver &Saver,
                               SmallVectorImpl<const char *> &NewArgv,
                               bool MarkEOLs = false);

/// String tokenization function type.  Should be compatible with either
/// Windows or Unix command line tokenizers.
using TokenizerCallback = void (*)(StringRef Source, StringSaver &Saver,
                                   SmallVectorImpl<const char *> &NewArgv,
                                   bool MarkEOLs);

/// Tokenizes content of configuration file.
///
/// \param [in] Source The string representing content of config file.
/// \param [in] Saver Delegates back to the caller for saving parsed strings.
/// \param [out] NewArgv All parsed strings are appended to NewArgv.
/// \param [in] MarkEOLs Added for compatibility with TokenizerCallback.
///
/// It works like TokenizeGNUCommandLine with ability to skip comment lines.
///
LLVM_ABI void tokenizeConfigFile(StringRef Source, StringSaver &Saver,
                                 SmallVectorImpl<const char *> &NewArgv,
                                 bool MarkEOLs = false);

/// Contains options that control response file expansion.
class ExpansionContext {
  /// Provides persistent storage for parsed strings.
  StringSaver Saver;

  /// Tokenization strategy. Typically Unix or Windows.
  TokenizerCallback Tokenizer;

  /// File system used for all file access when running the expansion.
  vfs::FileSystem *FS;

  /// Path used to resolve relative rsp files. If empty, the file system
  /// current directory is used instead.
  StringRef CurrentDir;

  /// Directories used for search of config files.
  ArrayRef<StringRef> SearchDirs;

  /// True if names of nested response files must be resolved relative to
  /// including file.
  bool RelativeNames = false;

  /// If true, mark end of lines and the end of the response file with nullptrs
  /// in the Argv vector.
  bool MarkEOLs = false;

  /// If true, body of config file is expanded.
  bool InConfigFile = false;

  llvm::Error expandResponseFile(StringRef FName,
                                 SmallVectorImpl<const char *> &NewArgv);

public:
  /// Construct an expansion context using allocator \p A and tokenizer \p T.
  ///
  /// \param A Allocator providing persistent storage for expanded strings.
  /// \param T Tokenizer used for response-file and config content.
  /// \param FS Optional VFS used for file access during expansion.
  LLVM_ABI ExpansionContext(BumpPtrAllocator &A, TokenizerCallback T,
                            vfs::FileSystem *FS = nullptr);

  /// Configure whether end-of-line markers are inserted as nullptrs.
  ///
  /// \param X True to mark end-of-line boundaries in Argv.
  /// \return A reference to this expansion context.
  ExpansionContext &setMarkEOLs(bool X) {
    MarkEOLs = X;
    return *this;
  }

  /// Configure whether nested response-file names are relative to the includer.
  ///
  /// \param X True to resolve nested names relative to the including file.
  /// \return A reference to this expansion context.
  ExpansionContext &setRelativeNames(bool X) {
    RelativeNames = X;
    return *this;
  }

  /// Set the directory used to resolve relative response-file paths.
  ///
  /// \param X Current directory path used during expansion.
  /// \return A reference to this expansion context.
  ExpansionContext &setCurrentDir(StringRef X) {
    CurrentDir = X;
    return *this;
  }

  /// Set the directories searched when looking up config files.
  ///
  /// \param X Search-path list for config-file lookup.
  /// \return A reference to this expansion context.
  ExpansionContext &setSearchDirs(ArrayRef<StringRef> X) {
    SearchDirs = X;
    return *this;
  }

  /// Set the VFS used for all file access during expansion.
  ///
  /// \param X File system used to read response and config files.
  /// \return A reference to this expansion context.
  ExpansionContext &setVFS(vfs::FileSystem *X) {
    FS = X;
    return *this;
  }

  /// Looks for the specified configuration file.
  ///
  /// \param[in]  FileName Name of the file to search for.
  /// \param[out] FilePath File absolute path, if it was found.
  /// \return True if file was found.
  ///
  /// If the specified file name contains a directory separator, it is searched
  /// for by its absolute path. Otherwise looks for file sequentially in
  /// directories specified by SearchDirs field.
  LLVM_ABI bool findConfigFile(StringRef FileName,
                               SmallVectorImpl<char> &FilePath);

  /// Reads command line options from the given configuration file.
  ///
  /// \param [in] CfgFile Path to configuration file.
  /// \param [out] Argv Array to which the read options are added.
  /// \return true if the file was successfully read.
  ///
  /// It reads content of the specified file, tokenizes it and expands "@file"
  /// commands resolving file names in them relative to the directory where
  /// CfgFilename resides. It also expands "<CFGDIR>" to the base path of the
  /// current config file.
  LLVM_ABI Error readConfigFile(StringRef CfgFile,
                                SmallVectorImpl<const char *> &Argv);

  /// Expand \c @file constructs in \p Argv recursively.
  ///
  /// \param Argv Argument vector that may contain response-file references.
  /// \return Success, or an Error describing why expansion failed.
  LLVM_ABI Error expandResponseFiles(SmallVectorImpl<const char *> &Argv);
};

/// Concatenate \p EnvVar options with \p Argv, then expand response files.
///
/// A convenience helper which concatenates the options specified by the
/// environment variable EnvVar and command line options, then expands
/// response files recursively.
///
/// \param Argc Argument count from main.
/// \param Argv Argument vector from main.
/// \param EnvVar Environment variable also providing options.
/// \param NewArgv Destination that receives the expanded arguments.
/// \return true if all @files were expanded successfully or there were none.
LLVM_ABI bool expandResponseFiles(int Argc, const char *const *Argv,
                                  const char *EnvVar,
                                  SmallVectorImpl<const char *> &NewArgv);

/// Expand response files in \p Argv using \p Saver and \p Tokenizer.
///
/// A convenience helper which supports the typical use case of expansion
/// function call.
///
/// \param Saver Allocator used for owning expanded token storage.
/// \param Tokenizer Tokenizer used for response-file content.
/// \param Argv Argument vector that may contain response-file references.
/// \return True if all @files were expanded successfully or there were none.
LLVM_ABI bool ExpandResponseFiles(StringSaver &Saver,
                                  TokenizerCallback Tokenizer,
                                  SmallVectorImpl<const char *> &Argv);

/// Concatenate \p EnvVar options with \p Argv using \p Saver, then expand.
///
/// A convenience helper which concatenates the options specified by the
/// environment variable EnvVar and command line options, then expands response
/// files recursively. The tokenizer is a predefined GNU or Windows one.
///
/// \param Argc Argument count from main.
/// \param Argv Argument vector from main.
/// \param EnvVar Environment variable also providing options.
/// \param Saver Allocator used for owning expanded token storage.
/// \param NewArgv Destination that receives the expanded arguments.
/// \return true if all @files were expanded successfully or there were none.
LLVM_ABI bool expandResponseFiles(int Argc, const char *const *Argv,
                                  const char *EnvVar, StringSaver &Saver,
                                  SmallVectorImpl<const char *> &NewArgv);

/// Mark all options not part of this category as cl::ReallyHidden.
///
/// \param Category the category of options to keep displaying
/// \param Sub Subcommand whose options are filtered.
///
/// Some tools (like clang-format) like to be able to hide all options that are
/// not specific to the tool. This function allows a tool to specify a single
/// option category to display in the -help output.
LLVM_ABI void HideUnrelatedOptions(cl::OptionCategory &Category,
                                   SubCommand &Sub = SubCommand::getTopLevel());

/// Mark all options not part of the categories as cl::ReallyHidden.
///
/// \param Categories the categories of options to keep displaying.
/// \param Sub Subcommand whose options are filtered.
///
/// Some tools (like clang-format) like to be able to hide all options that are
/// not specific to the tool. This function allows a tool to specify a single
/// option category to display in the -help output.
LLVM_ABI void
HideUnrelatedOptions(ArrayRef<const cl::OptionCategory *> Categories,
                     SubCommand &Sub = SubCommand::getTopLevel());

/// Reset every option as if it had never appeared on the command line.
///
/// This is useful for being able to parse a command line multiple times
/// (especially useful for writing tests).
LLVM_ABI void ResetAllOptionOccurrences();

/// Reset the command-line parser to a state with no registered options.
///
/// This removes all options, categories, and subcommands and returns the
/// parser to a state where no options are supported.
LLVM_ABI void ResetCommandLineParser();

/// Parse positional argument \p Arg with option handler \p Handler.
///
/// \param Handler Option that consumes the positional argument.
/// \param Arg Positional argument text to parse.
/// \param i argv index of this positional argument.
/// \return True on error.
LLVM_ABI bool ProvidePositionalOption(Option *Handler, StringRef Arg, int i);

} // end namespace cl

} // end namespace llvm

#endif // LLVM_SUPPORT_COMMANDLINE_H
