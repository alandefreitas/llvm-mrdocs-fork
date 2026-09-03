//===- Arg.h - Parsed Argument Classes --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines the llvm::Arg class for parsed arguments.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_OPTION_ARG_H
#define LLVM_OPTION_ARG_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/Compiler.h"
#include <string>

namespace llvm {

class raw_ostream;

/// Command-line option and argument parsing utilities.
namespace opt {

class ArgList;

/// A concrete instance of a particular driver option.
///
/// The Arg class encodes just enough information to be able to
/// derive the argument values efficiently.
class Arg {
private:
  /// The option this argument is an instance of.
  const Option Opt;

  /// The argument this argument was derived from (during tool chain
  /// argument translation), if any.
  const Arg *BaseArg;

  /// How this instance of the option was spelled.
  StringRef Spelling;

  /// The index at which this argument appears in the containing
  /// ArgList.
  unsigned Index;

  /// Was this argument used to affect compilation?
  ///
  /// This is used to generate an "argument unused" warning (without
  /// clang::options::TargetSpecific) or "unsupported option" error
  /// (with TargetSpecific).
  mutable unsigned Claimed : 1;

  /// Used by an unclaimed option with the TargetSpecific flag. If set, report
  /// an "argument unused" warning instead of an "unsupported option" error.
  unsigned IgnoredTargetSpecific : 1;

  /// Does this argument own its values?
  mutable unsigned OwnsValues : 1;

  /// The argument values, as C strings.
  SmallVector<const char *, 2> Values;

  /// If this arg was created through an alias, this is the original alias arg.
  /// For example, *this might be "-finput-charset=utf-8" and Alias might
  /// point to an arg representing "/source-charset:utf-8".
  std::unique_ptr<Arg> Alias;

public:
  /// Construct an argument with no values.
  ///
  /// \param Opt Option this argument is an instance of.
  /// \param Spelling How this instance of the option was spelled.
  /// \param Index Index of this argument in the containing ArgList.
  /// \param BaseArg Argument this was derived from during tool-chain
  /// translation, if any.
  LLVM_ABI Arg(const Option Opt, StringRef Spelling, unsigned Index,
               const Arg *BaseArg = nullptr);
  /// Construct an argument with a single value.
  ///
  /// \param Opt Option this argument is an instance of.
  /// \param Spelling How this instance of the option was spelled.
  /// \param Index Index of this argument in the containing ArgList.
  /// \param Value0 First (and only) argument value.
  /// \param BaseArg Argument this was derived from during tool-chain
  /// translation, if any.
  LLVM_ABI Arg(const Option Opt, StringRef Spelling, unsigned Index,
               const char *Value0, const Arg *BaseArg = nullptr);
  /// Construct an argument with two values.
  ///
  /// \param Opt Option this argument is an instance of.
  /// \param Spelling How this instance of the option was spelled.
  /// \param Index Index of this argument in the containing ArgList.
  /// \param Value0 First argument value.
  /// \param Value1 Second argument value.
  /// \param BaseArg Argument this was derived from during tool-chain
  /// translation, if any.
  LLVM_ABI Arg(const Option Opt, StringRef Spelling, unsigned Index,
               const char *Value0, const char *Value1,
               const Arg *BaseArg = nullptr);
  /// Deleted copy constructor; Args are not copyable.
  ///
  /// \param Other Unused; copy construction is deleted.
  Arg(const Arg &Other) = delete;
  /// Deleted copy assignment; Args are not copyable.
  ///
  /// \param Other Unused; copy assignment is deleted.
  Arg &operator=(const Arg &Other) = delete;
  /// Destroy the argument, freeing owned values if any.
  LLVM_ABI ~Arg();

  /// Return the option this argument is an instance of.
  ///
  /// \return The option this argument is an instance of.
  const Option &getOption() const { return Opt; }

  /// Return the used prefix and name of the option.
  ///
  /// For `--foo=bar`, returns `--foo=`.
  /// This is often the wrong function to call:
  /// * Use `getValue()` to get `bar`.
  /// * Use `getAsString()` to get a string suitable for printing an Arg in
  ///   a diagnostic.
  /// \return The used prefix and name of the option.
  StringRef getSpelling() const { return Spelling; }

  /// Return the index of this argument in its containing ArgList.
  ///
  /// \return The index of this argument in its containing ArgList.
  unsigned getIndex() const { return Index; }

  /// Return the base argument which generated this arg.
  ///
  /// This is either the argument itself or the argument it was
  /// derived from during tool chain specific argument translation.
  /// \return The base argument which generated this arg.
  const Arg &getBaseArg() const {
    return BaseArg ? *BaseArg : *this;
  }
  /// Return the base argument which generated this arg.
  ///
  /// \return The base argument which generated this arg.
  Arg &getBaseArg() { return BaseArg ? const_cast<Arg &>(*BaseArg) : *this; }
  /// Set the base argument this argument was derived from.
  ///
  /// \param BaseArg Argument this was derived from, or nullptr for none.
  void setBaseArg(const Arg *BaseArg) { this->BaseArg = BaseArg; }

  /// Args are converted to their unaliased form.  For args that originally
  /// came from an alias, this returns the alias the arg was produced from.
  ///
  /// \return The original alias Arg, or nullptr if this was not produced from
  /// an alias.
  const Arg* getAlias() const { return Alias.get(); }
  /// Set the original alias argument this unaliased argument was produced from.
  ///
  /// \param Alias Ownership of the alias Arg is taken.
  void setAlias(std::unique_ptr<Arg> Alias) { this->Alias = std::move(Alias); }

  /// Return whether this argument owns the memory of its values.
  ///
  /// \return True if this argument owns the memory of its values.
  bool getOwnsValues() const { return OwnsValues; }
  /// Set whether this argument owns the memory of its values.
  ///
  /// \param Value True if the Arg should delete its values on destruction.
  void setOwnsValues(bool Value) const { OwnsValues = Value; }

  /// Return whether this argument (or its base) has been claimed.
  ///
  /// \return True if this argument (or its base) has been claimed.
  bool isClaimed() const { return getBaseArg().Claimed; }
  /// Mark this argument (via its base) as claimed/used.
  void claim() const { getBaseArg().Claimed = true; }

  /// Return whether an unclaimed TargetSpecific option should warn instead of
  /// error.
  ///
  /// \return True if an unclaimed TargetSpecific option should warn instead of
  /// error.
  bool isIgnoredTargetSpecific() const {
    return getBaseArg().IgnoredTargetSpecific;
  }
  /// Mark this TargetSpecific argument to warn as unused instead of erroring.
  void ignoreTargetSpecific() {
    getBaseArg().IgnoredTargetSpecific = true;
  }

  /// Return the number of values attached to this argument.
  ///
  /// \return The number of values attached to this argument.
  unsigned getNumValues() const { return Values.size(); }

  /// Return the value at index \p N.
  ///
  /// \param N Zero-based index of the value to return.
  /// \return The C-string value at index \p N.
  const char *getValue(unsigned N = 0) const {
    return Values[N];
  }

  /// Return a mutable reference to the argument's values.
  ///
  /// \return A mutable reference to the argument's value list.
  SmallVectorImpl<const char *> &getValues() { return Values; }
  /// Return a const reference to the argument's values.
  ///
  /// \return A const reference to the argument's value list.
  const SmallVectorImpl<const char *> &getValues() const { return Values; }

  /// Return true if \p Value is among this argument's values.
  ///
  /// \param Value Value string to search for.
  /// \return True if \p Value is present among the argument's values.
  bool containsValue(StringRef Value) const {
    return llvm::is_contained(Values, Value);
  }

  /// Append the argument onto the given array as strings.
  ///
  /// \param Args Argument list used when rendering joined argument strings.
  /// \param Output Destination list that receives the rendered strings.
  LLVM_ABI void render(const ArgList &Args, ArgStringList &Output) const;

  /// Append the argument, render as an input, onto the given
  /// array as strings.
  ///
  /// The distinction is that some options only render their values
  /// when rendered as a input (e.g., Xlinker).
  /// \param Args Argument list used when rendering joined argument strings.
  /// \param Output Destination list that receives the rendered strings.
  LLVM_ABI void renderAsInput(const ArgList &Args, ArgStringList &Output) const;

  /// Print a debug representation of this argument to \p O.
  ///
  /// \param O Stream to write the representation to.
  LLVM_ABI void print(raw_ostream &O) const;
  /// Dump a debug representation of this argument to the debug stream.
  LLVM_ABI void dump() const;

  /// Return a formatted version of the argument and its values for diagnostics.
  ///
  /// Since this is for diagnostics, if this Arg was produced through an alias,
  /// this returns the string representation of the alias that the user wrote.
  /// \param Args Argument list used when rendering the diagnostic string.
  /// \return A string suitable for printing this Arg in a diagnostic.
  LLVM_ABI std::string getAsString(const ArgList &Args) const;
};

} // end namespace opt

} // end namespace llvm

#endif // LLVM_OPTION_ARG_H
