//===- Option.h - Abstract Driver Options -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OPTION_OPTION_H
#define LLVM_OPTION_OPTION_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Option/OptSpecifier.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>

namespace llvm {

class raw_ostream;

namespace opt {

class Arg;
class ArgList;

/// ArgStringList - Type used for constructing argv lists for subprocesses.
using ArgStringList = SmallVector<const char *, 16>;

/// Base flags for all options. Custom flags may be added after.
enum DriverFlag {
  /// Do not show this option in --help, even if it has help text.
  HelpHidden       = (1 << 0),
  /// Render only the option's values, not its name, when rendered as input.
  RenderAsInput    = (1 << 1),
  /// Render the option joined to its value, even if it is a separate option.
  RenderJoined     = (1 << 2),
  /// Render the option separately from its value, even if it is a joined option.
  RenderSeparate   = (1 << 3)
};

/// Visibility bits that control which options are shown for a given driver.
enum DriverVisibility {
  /// Default visibility used when no other visibility is specified.
  DefaultVis = (1 << 0),
};

/// Option - Abstract representation for a single form of driver
/// argument.
///
/// An Option class represents a form of option that the driver
/// takes, for example how many arguments the option has and how
/// they can be provided. Individual option instances store
/// additional information about what group the option is a member
/// of (if any), if the option is an alias, and a number of
/// flags. At runtime the driver parses the command line into
/// concrete Arg instances, each of which corresponds to a
/// particular Option instance.
class Option {
public:
  /// Kind of option, controlling how arguments and values are parsed.
  enum OptionClass {
    /// An option group used to organize related options.
    GroupClass = 0,
    /// A positional input argument rather than a flagged option.
    InputClass,
    /// An unrecognized option spelling.
    UnknownClass,
    /// A flag with no values.
    FlagClass,
    /// An option that prefixes its (single) value.
    JoinedClass,
    /// An option that takes one of a fixed set of values.
    ValuesClass,
    /// An option followed by its value as a separate argument.
    SeparateClass,
    /// An option that consumes all remaining arguments, if any.
    RemainingArgsClass,
    /// An option that consumes an optional joined argument and any remaining
    /// arguments.
    RemainingArgsJoinedClass,
    /// An option followed by comma-separated values.
    CommaJoinedClass,
    /// An option that takes multiple separate argument values.
    MultiArgClass,
    /// An option that is either joined to its (non-empty) value or followed by
    /// its value.
    JoinedOrSeparateClass,
    /// An option that is both joined to its first value and followed by its
    /// second value.
    JoinedAndSeparateClass
  };

  /// How an option should be rendered when rewriting argument lists.
  enum RenderStyleKind {
    /// Render values as a single comma-joined argument.
    RenderCommaJoinedStyle,
    /// Render the option name joined directly to its value.
    RenderJoinedStyle,
    /// Render the option name and its value as separate arguments.
    RenderSeparateStyle,
    /// Render only the option's values (as with RenderAsInput).
    RenderValuesStyle
  };

protected:
  /// Tablegen'd option info for this option, or null if invalid.
  const OptTable::Info *Info;
  /// Owning option table used to resolve groups, aliases, and names.
  const OptTable *Owner;

public:
  /// Construct an option from table info and its owning OptTable.
  ///
  /// \param Info Table entry describing this option, or null for an invalid
  /// option.
  /// \param Owner OptTable that owns \p Info and related options.
  LLVM_ABI Option(const OptTable::Info *Info, const OptTable *Owner);

  /// Return true if this option refers to valid table info.
  ///
  /// \return True if this option refers to valid table info.
  bool isValid() const {
    return Info != nullptr;
  }

  /// Return the unique numeric ID of this option.
  ///
  /// \return The unique numeric ID of this option.
  unsigned getID() const {
    assert(Info && "Must have a valid info!");
    return Info->ID;
  }

  /// Return the OptionClass kind of this option.
  ///
  /// \return The OptionClass kind of this option.
  OptionClass getKind() const {
    assert(Info && "Must have a valid info!");
    return OptionClass(Info->Kind);
  }

  /// Get the name of this option without any prefix.
  ///
  /// \return The name of this option without any prefix.
  StringRef getName() const {
    assert(Info && "Must have a valid info!");
    assert(Owner && "Must have a valid owner!");
    return Owner->getOptionName(Info->ID);
  }

  /// Return the group this option belongs to, if any.
  ///
  /// \return The group this option belongs to, or an invalid option if none.
  const Option getGroup() const {
    assert(Info && "Must have a valid info!");
    assert(Owner && "Must have a valid owner!");
    return Owner->getOption(Info->GroupID);
  }

  /// Return the option this option aliases, if any.
  ///
  /// \return The aliased option, or an invalid option if none.
  const Option getAlias() const {
    assert(Info && "Must have a valid info!");
    assert(Owner && "Must have a valid owner!");
    return Owner->getOption(Info->AliasID);
  }

  /// Get the alias arguments as a \0 separated list.
  /// E.g. ["foo", "bar"] would be returned as "foo\0bar\0".
  ///
  /// \return Alias arguments as a \0-separated list, or null if none.
  const char *getAliasArgs() const {
    assert(Info && "Must have a valid info!");
    assert((!Info->AliasArgs || Info->AliasArgs[0] != 0) &&
           "AliasArgs should be either 0 or non-empty.");

    return Info->AliasArgs;
  }

  /// Get the default prefix for this option.
  ///
  /// \return The default prefix for this option.
  StringRef getPrefix() const {
    assert(Info && "Must have a valid info!");
    assert(Owner && "Must have a valid owner!");
    return Owner->getOptionPrefix(Info->ID);
  }

  /// Get the name of this option with the default prefix.
  ///
  /// \return The name of this option with the default prefix.
  StringRef getPrefixedName() const {
    assert(Info && "Must have a valid info!");
    assert(Owner && "Must have a valid owner!");
    return Owner->getOptionPrefixedName(Info->ID);
  }

  /// Get the help text for this option.
  ///
  /// \return The help text for this option.
  StringRef getHelpText() const {
    assert(Info && "Must have a valid info!");
    return Info->HelpText;
  }

  /// Get the meta-variable list for this option.
  ///
  /// \return The meta-variable list for this option.
  StringRef getMetaVar() const {
    assert(Info && "Must have a valid info!");
    return Info->MetaVar;
  }

  /// Return the number of values this MultiArg option expects.
  ///
  /// \return The number of values this MultiArg option expects.
  unsigned getNumArgs() const { return Info->Param; }

  /// Return true if this option should render as values only (RenderAsInput).
  ///
  /// \return True if RenderAsInput is set for this option.
  bool hasNoOptAsInput() const { return Info->Flags & RenderAsInput;}

  /// Return how this option should be rendered when rewriting argv lists.
  ///
  /// \return The RenderStyleKind used when rewriting argv lists.
  RenderStyleKind getRenderStyle() const {
    if (Info->Flags & RenderJoined)
      return RenderJoinedStyle;
    if (Info->Flags & RenderSeparate)
      return RenderSeparateStyle;
    switch (getKind()) {
    case GroupClass:
    case InputClass:
    case UnknownClass:
      return RenderValuesStyle;
    case JoinedClass:
    case JoinedAndSeparateClass:
      return RenderJoinedStyle;
    case CommaJoinedClass:
      return RenderCommaJoinedStyle;
    case FlagClass:
    case ValuesClass:
    case SeparateClass:
    case MultiArgClass:
    case JoinedOrSeparateClass:
    case RemainingArgsClass:
    case RemainingArgsJoinedClass:
      return RenderSeparateStyle;
    }
    llvm_unreachable("Unexpected kind!");
  }

  /// Test if this option has the flag \a Val.
  ///
  /// \param Val DriverFlag bit (or custom flag) to test for.
  /// \return True if the flag \p Val is set.
  bool hasFlag(unsigned Val) const {
    return Info->Flags & Val;
  }

  /// Test if this option has the visibility flag \a Val.
  ///
  /// \param Val DriverVisibility bit to test for.
  /// \return True if the visibility flag \p Val is set.
  bool hasVisibilityFlag(unsigned Val) const {
    return Info->Visibility & Val;
  }

  /// getUnaliasedOption - Return the final option this option
  /// aliases (itself, if the option has no alias).
  ///
  /// \return The final unaliased option, or this option if it has no alias.
  const Option getUnaliasedOption() const {
    const Option Alias = getAlias();
    if (Alias.isValid()) return Alias.getUnaliasedOption();
    return *this;
  }

  /// getRenderName - Return the name to use when rendering this
  /// option.
  ///
  /// \return The name to use when rendering this option.
  StringRef getRenderName() const {
    return getUnaliasedOption().getName();
  }

  /// matches - Predicate for whether this option is part of the
  /// given option (which may be a group).
  ///
  /// Note that matches against options which are an alias should never be
  /// done -- aliases do not participate in matching and so such a query will
  /// always be false.
  ///
  /// \param ID Option or group specifier to match against.
  /// \return True if this option is part of the given option or group.
  LLVM_ABI bool matches(OptSpecifier ID) const;

  /// Return true if this option is registered for the given subcommand.
  ///
  /// \param SubCommand Name of the subcommand to check registration for.
  /// \return True if this option is registered for \p SubCommand.
  bool isRegisteredSC(StringRef SubCommand) const {
    assert(Info && "Must have a valid info!");
    assert(Owner && "Must have a valid owner!");
    return Owner->isValidForSubCommand(Info, SubCommand);
  }

  /// Potentially accept the current argument, returning a new Arg instance,
  /// or 0 if the option does not accept this argument (or the argument is
  /// missing values).
  ///
  /// If the option accepts the current argument, accept() sets
  /// Index to the position where argument parsing should resume
  /// (even if the argument is missing values).
  ///
  /// \param Args Argument list being parsed.
  /// \param CurArg The argument to be matched. It may be shorter than the
  /// underlying storage to represent a Joined argument.
  /// \param GroupedShortOption If true, we are handling the fallback case of
  /// parsing a prefix of the current argument as a short option.
  /// \param Index On entry, the index of the current argument; on success,
  /// updated to the index where parsing should resume.
  /// \return A new Arg for the accepted option, or null if not accepted.
  LLVM_ABI std::unique_ptr<Arg> accept(const ArgList &Args, StringRef CurArg,
                                       bool GroupedShortOption,
                                       unsigned &Index) const;

private:
  std::unique_ptr<Arg> acceptInternal(const ArgList &Args, StringRef CurArg,
                                      unsigned &Index) const;

public:
  /// Print a debug representation of this option to \p O.
  ///
  /// \param O Stream to write the representation to.
  /// \param AddNewLine If true, append a newline after the representation.
  LLVM_ABI void print(raw_ostream &O, bool AddNewLine = true) const;
  /// Dump a debug representation of this option to the debug stream.
  LLVM_ABI void dump() const;
};

} // end namespace opt

} // end namespace llvm

#endif // LLVM_OPTION_OPTION_H
