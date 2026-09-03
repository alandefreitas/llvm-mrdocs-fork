//===- ArgList.h - Argument List Management ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OPTION_ARGLIST_H
#define LLVM_OPTION_ARGLIST_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/OptSpecifier.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace llvm {

class raw_ostream;

namespace opt {

/// arg_iterator - Iterates through arguments stored inside an ArgList.
template<typename BaseIter, unsigned NumOptSpecifiers = 0>
class arg_iterator {
  /// The current argument and the end of the sequence we're iterating.
  BaseIter Current, End;

  /// Optional filters on the arguments which will be match. To avoid a
  /// zero-sized array, we store one specifier even if we're asked for none.
  OptSpecifier Ids[NumOptSpecifiers ? NumOptSpecifiers : 1];

  void SkipToNextArg() {
    for (; Current != End; ++Current) {
      // Skip erased elements.
      if (!*Current)
        continue;

      // Done if there are no filters.
      if (!NumOptSpecifiers)
        return;

      // Otherwise require a match.
      const Option &O = (*Current)->getOption();
      for (auto Id : Ids) {
        if (!Id.isValid())
          break;
        if (O.matches(Id))
          return;
      }
    }
  }

  using Traits = std::iterator_traits<BaseIter>;

public:
  /// Type of values obtained by dereferencing this iterator.
  using value_type = typename Traits::value_type;
  /// Type of references obtained by dereferencing this iterator.
  using reference = typename Traits::reference;
  /// Type of pointers obtained from this iterator.
  using pointer = typename Traits::pointer;
  /// Iterator category tag for this forward iterator.
  using iterator_category = std::forward_iterator_tag;
  /// Type used to represent distances between iterators.
  using difference_type = std::ptrdiff_t;

  /// Construct an argument iterator over [\p Current, \p End).
  ///
  /// \param Current Iterator to the first argument to consider.
  /// \param End Iterator one past the last argument.
  /// \param Ids Optional option filters; only matching args are visited.
  arg_iterator(
      BaseIter Current, BaseIter End,
      const OptSpecifier (&Ids)[NumOptSpecifiers ? NumOptSpecifiers : 1] = {})
      : Current(Current), End(End) {
    for (unsigned I = 0; I != NumOptSpecifiers; ++I)
      this->Ids[I] = Ids[I];
    SkipToNextArg();
  }

  /// Return a reference to the current argument.
  ///
  /// \return A reference to the current argument.
  reference operator*() const { return *Current; }
  /// Return a pointer to the current argument.
  ///
  /// \return A pointer to the current argument.
  pointer operator->() const { return Current; }

  /// Advance to the next matching argument and return this iterator.
  ///
  /// \return A reference to this iterator after advancing.
  arg_iterator &operator++() {
    ++Current;
    SkipToNextArg();
    return *this;
  }

  /// Advance to the next matching argument and return the prior iterator.
  ///
  /// \param Unused Unused postfix-discriminator parameter.
  /// \return A copy of the iterator before advancing.
  arg_iterator operator++(int Unused) {
    arg_iterator tmp(*this);
    ++(*this);
    return tmp;
  }

  /// Return true if \p LHS and \p RHS refer to the same position.
  ///
  /// \param LHS Left-hand iterator.
  /// \param RHS Right-hand iterator.
  /// \return True if both iterators refer to the same position.
  friend bool operator==(arg_iterator LHS, arg_iterator RHS) {
    return LHS.Current == RHS.Current;
  }
  /// Return true if \p LHS and \p RHS refer to different positions.
  ///
  /// \param LHS Left-hand iterator.
  /// \param RHS Right-hand iterator.
  /// \return True if the iterators refer to different positions.
  friend bool operator!=(arg_iterator LHS, arg_iterator RHS) {
    return !(LHS == RHS);
  }
};

/// ArgList - Ordered collection of driver arguments.
///
/// The ArgList class manages a list of Arg instances as well as
/// auxiliary data and convenience methods to allow Tools to quickly
/// check for the presence of Arg instances for a particular Option
/// and to iterate over groups of arguments.
class ArgList {
public:
  /// Underlying storage type for the argument pointer list.
  using arglist_type = SmallVector<Arg *, 16>;
  /// Mutable forward iterator over arguments.
  using iterator = arg_iterator<arglist_type::iterator>;
  /// Const forward iterator over arguments.
  using const_iterator = arg_iterator<arglist_type::const_iterator>;
  /// Mutable reverse iterator over arguments.
  using reverse_iterator = arg_iterator<arglist_type::reverse_iterator>;
  /// Const reverse iterator over arguments.
  using const_reverse_iterator =
      arg_iterator<arglist_type::const_reverse_iterator>;

  /// Const forward iterator that visits only matching option ids.
  template<unsigned N> using filtered_iterator =
      arg_iterator<arglist_type::const_iterator, N>;
  /// Const reverse iterator that visits only matching option ids.
  template<unsigned N> using filtered_reverse_iterator =
      arg_iterator<arglist_type::const_reverse_iterator, N>;

private:
  /// The internal list of arguments.
  arglist_type Args;

  using OptRange = std::pair<unsigned, unsigned>;
  static OptRange emptyRange() { return {-1u, 0u}; }

  /// The first and last index of each different OptSpecifier ID.
  DenseMap<unsigned, OptRange> OptRanges;

  /// Get the range of indexes in which options with the specified IDs might
  /// reside, or (0, 0) if there are no such options.
  LLVM_ABI OptRange getRange(std::initializer_list<OptSpecifier> Ids) const;

protected:
  // Make the default special members protected so they won't be used to slice
  // derived objects, but can still be used by derived objects to implement
  // their own special members.
  /// Construct an empty argument list.
  ArgList() = default;

  // Explicit move operations to ensure the container is cleared post-move
  // otherwise it could lead to a double-delete in the case of moving of an
  // InputArgList which deletes the contents of the container. If we could fix
  // up the ownership here (delegate storage/ownership to the derived class so
  // it can be a container of unique_ptr) this would be simpler.
  /// Move-construct an argument list, clearing the source.
  ///
  /// \param RHS Argument list to move from.
  ArgList(ArgList &&RHS)
      : Args(std::move(RHS.Args)), OptRanges(std::move(RHS.OptRanges)) {
    RHS.Args.clear();
    RHS.OptRanges.clear();
  }

  /// Move-assign an argument list, clearing the source.
  ///
  /// \param RHS Argument list to move from.
  /// \return A reference to this argument list.
  ArgList &operator=(ArgList &&RHS) {
    Args = std::move(RHS.Args);
    RHS.Args.clear();
    OptRanges = std::move(RHS.OptRanges);
    RHS.OptRanges.clear();
    return *this;
  }

  // Protect the dtor to ensure this type is never destroyed polymorphically.
  /// Destroy the argument list.
  ~ArgList() = default;

  // Implicitly convert a value to an OptSpecifier. Used to work around a bug
  // in MSVC's implementation of narrowing conversion checking.
  /// Convert \p S to an OptSpecifier (MSVC narrowing workaround).
  ///
  /// \param S Specifier to return unchanged.
  /// \return \p S unchanged as an OptSpecifier.
  static OptSpecifier toOptSpecifier(OptSpecifier S) { return S; }

public:
  /// @name Arg Access
  /// @{

  /// append - Append \p A to the arg list.
  ///
  /// \param A Argument to append; ownership is not taken.
  LLVM_ABI void append(Arg *A);

  /// Return the underlying list of argument pointers.
  ///
  /// \return The underlying list of argument pointers.
  const arglist_type &getArgs() const { return Args; }

  /// Return the number of arguments in the list.
  ///
  /// \return The number of arguments in the list.
  unsigned size() const { return Args.size(); }

  /// @}
  /// @name Arg Iteration
  /// @{

  /// Return an iterator to the first argument.
  ///
  /// \return An iterator to the first argument.
  iterator begin() { return {Args.begin(), Args.end()}; }
  /// Return an iterator past the last argument.
  ///
  /// \return An iterator past the last argument.
  iterator end() { return {Args.end(), Args.end()}; }

  /// Return a reverse iterator to the last argument.
  ///
  /// \return A reverse iterator to the last argument.
  reverse_iterator rbegin() { return {Args.rbegin(), Args.rend()}; }
  /// Return a reverse iterator past the first argument.
  ///
  /// \return A reverse iterator past the first argument.
  reverse_iterator rend() { return {Args.rend(), Args.rend()}; }

  /// Return a const iterator to the first argument.
  ///
  /// \return A const iterator to the first argument.
  const_iterator begin() const { return {Args.begin(), Args.end()}; }
  /// Return a const iterator past the last argument.
  ///
  /// \return A const iterator past the last argument.
  const_iterator end() const { return {Args.end(), Args.end()}; }

  /// Return a const reverse iterator to the last argument.
  ///
  /// \return A const reverse iterator to the last argument.
  const_reverse_iterator rbegin() const { return {Args.rbegin(), Args.rend()}; }
  /// Return a const reverse iterator past the first argument.
  ///
  /// \return A const reverse iterator past the first argument.
  const_reverse_iterator rend() const { return {Args.rend(), Args.rend()}; }

  /// Return a range of arguments matching any of \p Ids.
  ///
  /// \param Ids Option specifiers to include in the filtered range.
  /// \return A range of arguments matching any of \p Ids.
  template<typename ...OptSpecifiers>
  iterator_range<filtered_iterator<sizeof...(OptSpecifiers)>>
  filtered(OptSpecifiers ...Ids) const {
    OptRange Range = getRange({toOptSpecifier(Ids)...});
    auto B = Args.begin() + Range.first;
    auto E = Args.begin() + Range.second;
    using Iterator = filtered_iterator<sizeof...(OptSpecifiers)>;
    return make_range(Iterator(B, E, {toOptSpecifier(Ids)...}),
                      Iterator(E, E, {toOptSpecifier(Ids)...}));
  }

  /// Return a reverse range of arguments matching any of \p Ids.
  ///
  /// \param Ids Option specifiers to include in the filtered range.
  /// \return A reverse range of arguments matching any of \p Ids.
  template<typename ...OptSpecifiers>
  iterator_range<filtered_reverse_iterator<sizeof...(OptSpecifiers)>>
  filtered_reverse(OptSpecifiers ...Ids) const {
    OptRange Range = getRange({toOptSpecifier(Ids)...});
    auto B = Args.rend() - Range.second;
    auto E = Args.rend() - Range.first;
    using Iterator = filtered_reverse_iterator<sizeof...(OptSpecifiers)>;
    return make_range(Iterator(B, E, {toOptSpecifier(Ids)...}),
                      Iterator(E, E, {toOptSpecifier(Ids)...}));
  }

  /// @}
  /// @name Arg Removal
  /// @{

  /// eraseArg - Remove any option matching \p Id.
  ///
  /// \param Id Option specifier of arguments to remove.
  LLVM_ABI void eraseArg(OptSpecifier Id);

  /// @}
  /// @name Arg Access
  /// @{

  /// Return true if any option matching \p Ids is present, without claiming.
  ///
  /// \param Ids Option specifiers to search for.
  /// \return True if any matching option is present.
  template<typename ...OptSpecifiers>
  bool hasArgNoClaim(OptSpecifiers ...Ids) const {
    return getLastArgNoClaim(Ids...) != nullptr;
  }
  /// Return true if any option matching \p Ids is present.
  ///
  /// Matching arguments are claimed.
  ///
  /// \param Ids Option specifiers to search for.
  /// \return True if any matching option is present.
  template<typename ...OptSpecifiers>
  bool hasArg(OptSpecifiers ...Ids) const {
    return getLastArg(Ids...) != nullptr;
  }

  /// Return true if the arg list contains multiple arguments matching \p Id.
  ///
  /// \param Id Option specifier to search for.
  /// \return True if more than one matching argument is present.
  bool hasMultipleArgs(OptSpecifier Id) const {
    auto Args = filtered(Id);
    return (Args.begin() != Args.end()) && (++Args.begin()) != Args.end();
  }

  /// Return the last argument matching \p Ids, or null.
  ///
  /// Matching arguments are claimed.
  ///
  /// \param Ids Option specifiers to search for.
  /// \return The last matching argument, or null if none match.
  template <typename... OptSpecifiers>
  LLVM_ATTRIBUTE_NOINLINE Arg *getLastArg(OptSpecifiers... Ids) const {
    Arg *Res = nullptr;
    for (Arg *A : filtered(Ids...)) {
      Res = A;
      Res->claim();
    }
    return Res;
  }

  /// Return the last argument matching \p Ids, or null, without claiming.
  ///
  /// Do not mark matching options as having been used.
  ///
  /// \param Ids Option specifiers to search for.
  /// \return The last matching argument, or null if none match.
  template <typename... OptSpecifiers>
  LLVM_ATTRIBUTE_NOINLINE Arg *getLastArgNoClaim(OptSpecifiers... Ids) const {
    for (Arg *A : filtered_reverse(Ids...))
      return A;
    return nullptr;
  }

  /// getArgString - Return the input argument string at \p Index.
  ///
  /// \param Index Index into the argument string list.
  /// \return The argument string at \p Index.
  virtual const char *getArgString(unsigned Index) const = 0;

  /// getNumInputArgStrings - Return the number of original argument strings,
  /// which are guaranteed to be the first strings in the argument string
  /// list.
  ///
  /// \return The number of original input argument strings.
  virtual unsigned getNumInputArgStrings() const = 0;

  /// getSubCommand - Find subcommand from the arguments if the usage is valid.
  ///
  /// \param AllSubCommands - A list of all valid subcommands.
  /// \param HandleMultipleSubcommands - A callback for the case where multiple
  /// subcommands are present in the arguments. It gets a list of all found
  /// subcommands.
  /// \param HandleOtherPositionals - A callback for the case where positional
  /// arguments that are not subcommands are present.
  /// \return The name of the subcommand found. If no subcommand is found,
  /// this returns an empty StringRef. If multiple subcommands are found, the
  /// first one is returned.
  LLVM_ABI StringRef getSubCommand(
      ArrayRef<OptTable::SubCommand> AllSubCommands,
      std::function<void(ArrayRef<StringRef>)> HandleMultipleSubcommands,
      std::function<void(ArrayRef<StringRef>)> HandleOtherPositionals) const;

  /// @}
  /// @name Argument Lookup Utilities
  /// @{

  /// getLastArgValue - Return the value of the last argument, or a default.
  ///
  /// \param Id Option specifier whose last value is requested.
  /// \param Default Value returned when the option is absent.
  /// \return The last matching argument value, or \p Default if absent.
  LLVM_ABI StringRef getLastArgValue(OptSpecifier Id,
                                     StringRef Default = "") const;

  /// getAllArgValues - Get the values of all instances of the given argument
  /// as strings.
  ///
  /// \param Id Option specifier whose values are collected.
  /// \return The values of all matching arguments as strings.
  LLVM_ABI std::vector<std::string> getAllArgValues(OptSpecifier Id) const;

  /// @}
  /// @name Translation Utilities
  /// @{

  /// Return whether a positive option is set, considering its negation.
  ///
  /// Given an option \p Pos and its negative form \p Neg, return true if the
  /// option is present, false if the negation is present, and \p Default if
  /// neither option is given. If both the option and its negation are present,
  /// the last one wins.
  ///
  /// \param Pos Positive form of the option.
  /// \param Neg Negative form of the option.
  /// \param Default Value when neither option is present.
  /// \return True if the positive option wins, false if the negation wins, or
  /// \p Default if neither is present.
  LLVM_ABI bool hasFlag(OptSpecifier Pos, OptSpecifier Neg, bool Default) const;
  /// Return whether a positive option is set, without claiming arguments.
  ///
  /// \param Pos Positive form of the option.
  /// \param Neg Negative form of the option.
  /// \param Default Value when neither option is present.
  /// \return True if the positive option wins, false if the negation wins, or
  /// \p Default if neither is present.
  LLVM_ABI bool hasFlagNoClaim(OptSpecifier Pos, OptSpecifier Neg,
                               bool Default) const;

  /// Return whether a positive option or alias is set, considering negation.
  ///
  /// Given an option \p Pos, an alias \p PosAlias and its negative form \p Neg,
  /// return true if the option or its alias is present, false if the negation
  /// is present, and \p Default if none of the options are given. If multiple
  /// options are present, the last one wins.
  ///
  /// \param Pos Positive form of the option.
  /// \param PosAlias Alias of the positive option.
  /// \param Neg Negative form of the option.
  /// \param Default Value when none of the options are present.
  /// \return True if the positive option or alias wins, false if the negation
  /// wins, or \p Default if none are present.
  LLVM_ABI bool hasFlag(OptSpecifier Pos, OptSpecifier PosAlias,
                        OptSpecifier Neg, bool Default) const;

  /// Given an option Pos and its negative form Neg, render the option if Pos is
  /// present.
  ///
  /// \param Output Destination list that receives the rendered strings.
  /// \param Pos Positive form of the option to render when present.
  /// \param Neg Negative form that suppresses the positive option.
  LLVM_ABI void addOptInFlag(ArgStringList &Output, OptSpecifier Pos,
                             OptSpecifier Neg) const;
  /// Render the option if Neg is present.
  ///
  /// \param Output Destination list that receives the rendered strings.
  /// \param Pos Positive form of the option.
  /// \param Neg Negative form to render when present.
  void addOptOutFlag(ArgStringList &Output, OptSpecifier Pos,
                     OptSpecifier Neg) const {
    addOptInFlag(Output, Neg, Pos);
  }

  /// Render only the last argument matching \p Ids, if present.
  ///
  /// \param Output Destination list that receives the rendered strings.
  /// \param Ids Option specifiers to search for.
  template <typename... OptSpecifiers>
  void addLastArg(ArgStringList &Output, OptSpecifiers... Ids) const {
    if (Arg *A = getLastArg(Ids...)) // Calls claim() on all Ids's Args.
      A->render(*this, Output);
  }
  /// Render only the last argument matching \p Ids, if present.
  ///
  /// \param Output Destination list that receives the rendered strings.
  /// \param Ids Option specifiers to search for.
  template <typename... OptSpecifiers>
  void AddLastArg(ArgStringList &Output, OptSpecifiers... Ids) const {
    addLastArg(Output, Ids...);
  }

  /// AddAllArgsExcept - Render all arguments matching any of the given ids
  /// and not matching any of the excluded ids.
  ///
  /// \param Output Destination list that receives the rendered strings.
  /// \param Ids Option specifiers to include.
  /// \param ExcludeIds Option specifiers to exclude.
  LLVM_ABI void AddAllArgsExcept(ArgStringList &Output,
                                 ArrayRef<OptSpecifier> Ids,
                                 ArrayRef<OptSpecifier> ExcludeIds) const;
  /// Render all arguments matching any of the given ids.
  ///
  /// \param Output Destination list that receives the rendered strings.
  /// \param Ids Option specifiers to match.
  LLVM_ABI void addAllArgs(ArgStringList &Output,
                           ArrayRef<OptSpecifier> Ids) const;

  /// AddAllArgs - Render all arguments matching the given ids.
  ///
  /// \param Output Destination list that receives the rendered strings.
  /// \param Id0 Option specifier to match.
  LLVM_ABI void AddAllArgs(ArgStringList &Output, OptSpecifier Id0) const;

  /// AddAllArgValues - Render the argument values of all arguments
  /// matching the given ids.
  ///
  /// \param Output Destination list that receives the rendered values.
  /// \param Id0 First option specifier to match.
  /// \param Id1 Optional second option specifier to match.
  /// \param Id2 Optional third option specifier to match.
  LLVM_ABI void AddAllArgValues(ArgStringList &Output, OptSpecifier Id0,
                                OptSpecifier Id1 = 0U,
                                OptSpecifier Id2 = 0U) const;

  /// Render matching arguments with a translated option name.
  ///
  /// Render all the arguments matching the given ids, but forced to separate
  /// args and using the provided name instead of the first option value.
  ///
  /// \param Output Destination list that receives the rendered strings.
  /// \param Id0 Option specifier to match.
  /// \param Translation Name to use instead of the first option value.
  /// \param Joined If true, render the argument as joined with the option
  /// specifier.
  LLVM_ABI void AddAllArgsTranslated(ArgStringList &Output, OptSpecifier Id0,
                                     const char *Translation,
                                     bool Joined = false) const;

  /// ClaimAllArgs - Claim all arguments which match the given
  /// option id.
  ///
  /// \param Id0 Option specifier of arguments to claim.
  LLVM_ABI void ClaimAllArgs(OptSpecifier Id0) const;

  /// Claim all arguments matching any of \p Ids.
  ///
  /// \param Ids Option specifiers of arguments to claim.
  template <typename... OptSpecifiers>
  void claimAllArgs(OptSpecifiers... Ids) const {
    for (Arg *A : filtered(Ids...))
      A->claim();
  }

  /// ClaimAllArgs - Claim all arguments.
  ///
  LLVM_ABI void ClaimAllArgs() const;
  /// @}
  /// @name Arg Synthesis
  /// @{

  /// Construct a constant string pointer whose
  /// lifetime will match that of the ArgList.
  ///
  /// \param Str String to intern in this ArgList.
  /// \return A pointer to the interned string.
  virtual const char *MakeArgStringRef(StringRef Str) const = 0;
  /// Construct a constant string pointer from \p Str.
  ///
  /// The returned pointer's lifetime matches that of the ArgList.
  ///
  /// \param Str String to intern in this ArgList.
  /// \return A pointer to the interned string.
  const char *MakeArgString(const Twine &Str) const {
    SmallString<256> Buf;
    return MakeArgStringRef(Str.toStringRef(Buf));
  }

  /// Create an arg string for (\p LHS + \p RHS), reusing the
  /// string at \p Index if possible.
  ///
  /// \param Index Existing argument-string index to reuse when possible.
  /// \param LHS Left-hand substring of the joined result.
  /// \param RHS Right-hand substring of the joined result.
  /// \return A pointer to the joined string, possibly reused from \p Index.
  LLVM_ABI const char *GetOrMakeJoinedArgString(unsigned Index, StringRef LHS,
                                                StringRef RHS) const;

  /// Print a debug representation of this argument list to \p O.
  ///
  /// \param O Stream to write the representation to.
  LLVM_ABI void print(raw_ostream &O) const;
  /// Dump a debug representation of this argument list to the debug stream.
  LLVM_ABI void dump() const;

  /// @}
};

/// An ArgList that owns the memory for its input argument strings.
class LLVM_ABI InputArgList final : public ArgList {
private:
  /// List of argument strings used by the contained Args.
  ///
  /// This is mutable since we treat the ArgList as being the list
  /// of Args, and allow routines to add new strings (to have a
  /// convenient place to store the memory) via MakeIndex.
  mutable ArgStringList ArgStrings;

  /// Strings for synthesized arguments.
  ///
  /// This is mutable since we treat the ArgList as being the list
  /// of Args, and allow routines to add new strings (to have a
  /// convenient place to store the memory) via MakeIndex.
  mutable std::list<std::string> SynthesizedStrings;

  /// The number of original input argument strings.
  unsigned NumInputArgStrings;

  /// Release allocated arguments.
  void releaseMemory();

public:
  /// Construct an empty input argument list.
  InputArgList() : NumInputArgStrings(0) {}

  /// Construct an input argument list from a C argv range.
  ///
  /// \param ArgBegin Pointer to the first argument string.
  /// \param ArgEnd Pointer one past the last argument string.
  InputArgList(const char* const *ArgBegin, const char* const *ArgEnd);

  /// Move-construct an input argument list.
  ///
  /// \param RHS Input argument list to move from.
  InputArgList(InputArgList &&RHS)
      : ArgList(std::move(RHS)), ArgStrings(std::move(RHS.ArgStrings)),
        SynthesizedStrings(std::move(RHS.SynthesizedStrings)),
        NumInputArgStrings(RHS.NumInputArgStrings) {}

  /// Move-assign an input argument list.
  ///
  /// \param RHS Input argument list to move from.
  /// \return A reference to this input argument list.
  InputArgList &operator=(InputArgList &&RHS) {
    if (this == &RHS)
      return *this;
    releaseMemory();
    ArgList::operator=(std::move(RHS));
    ArgStrings = std::move(RHS.ArgStrings);
    SynthesizedStrings = std::move(RHS.SynthesizedStrings);
    NumInputArgStrings = RHS.NumInputArgStrings;
    return *this;
  }

  /// Destroy the input argument list and release owned Args.
  ~InputArgList() { releaseMemory(); }

  /// Return the argument string at \p Index.
  ///
  /// \param Index Index into the argument string list.
  /// \return The argument string at \p Index.
  const char *getArgString(unsigned Index) const override {
    return ArgStrings[Index];
  }

  /// Replace the argument string at \p Index with \p S.
  ///
  /// \param Index Index of the string to replace.
  /// \param S New string value to store.
  void replaceArgString(unsigned Index, const Twine &S) {
    ArgStrings[Index] = MakeArgString(S);
  }

  /// Return the number of original input argument strings.
  ///
  /// \return The number of original input argument strings.
  unsigned getNumInputArgStrings() const override {
    return NumInputArgStrings;
  }

  /// @name Arg Synthesis
  /// @{

public:
  /// MakeIndex - Get an index for the given string(s).
  ///
  /// \param String0 First string to intern and index.
  /// \return The index of the interned string.
  unsigned MakeIndex(StringRef String0) const;
  /// MakeIndex - Get an index for the given pair of strings.
  ///
  /// \param String0 First string to intern and index.
  /// \param String1 Second string to intern and index.
  /// \return The index of the first interned string in the pair.
  unsigned MakeIndex(StringRef String0, StringRef String1) const;

  /// Bring MakeArgString overloads into this class.
  using ArgList::MakeArgString;
  /// Construct a constant string pointer whose lifetime matches this list.
  ///
  /// \param Str String to intern in this ArgList.
  /// \return A pointer to the interned string.
  const char *MakeArgStringRef(StringRef Str) const override;

  /// @}
};

/// DerivedArgList - An ordered collection of driver arguments,
/// whose storage may be in another argument list.
class LLVM_ABI DerivedArgList final : public ArgList {
  const InputArgList &BaseArgs;

  /// The list of arguments we synthesized.
  mutable SmallVector<std::unique_ptr<Arg>, 16> SynthesizedArgs;

public:
  /// Construct a new derived arg list from \p BaseArgs.
  ///
  /// \param BaseArgs Underlying input argument list that owns string storage.
  DerivedArgList(const InputArgList &BaseArgs);

  /// Return the argument string at \p Index from the base list.
  ///
  /// \param Index Index into the base argument string list.
  /// \return The argument string at \p Index from the base list.
  const char *getArgString(unsigned Index) const override {
    return BaseArgs.getArgString(Index);
  }

  /// Return the number of original input argument strings from the base list.
  ///
  /// \return The number of original input argument strings from the base list.
  unsigned getNumInputArgStrings() const override {
    return BaseArgs.getNumInputArgStrings();
  }

  /// Return the underlying input argument list.
  ///
  /// \return The underlying input argument list.
  const InputArgList &getBaseArgs() const {
    return BaseArgs;
  }

  /// @name Arg Synthesis
  /// @{

  /// AddSynthesizedArg - Add a argument to the list of synthesized arguments
  /// (to be freed).
  ///
  /// \param A Synthesized argument whose ownership is taken.
  void AddSynthesizedArg(Arg *A);

  /// Bring MakeArgString overloads into this class.
  using ArgList::MakeArgString;
  /// Construct a constant string pointer whose lifetime matches the base list.
  ///
  /// \param Str String to intern via the base ArgList.
  /// \return A pointer to the interned string.
  const char *MakeArgStringRef(StringRef Str) const override;

  /// AddFlagArg - Construct a new FlagArg for the given option \p Id and
  /// append it to the argument list.
  ///
  /// \param BaseArg Argument this synthesized arg is derived from, if any.
  /// \param Opt Option to instantiate as a flag argument.
  void AddFlagArg(const Arg *BaseArg, const Option Opt) {
    append(MakeFlagArg(BaseArg, Opt));
  }

  /// AddPositionalArg - Construct a new Positional arg for the given option
  /// \p Id, with the provided \p Value and append it to the argument
  /// list.
  ///
  /// \param BaseArg Argument this synthesized arg is derived from, if any.
  /// \param Opt Option to instantiate as a positional argument.
  /// \param Value Positional value string.
  void AddPositionalArg(const Arg *BaseArg, const Option Opt,
                        StringRef Value) {
    append(MakePositionalArg(BaseArg, Opt, Value));
  }

  /// AddSeparateArg - Construct a new Positional arg for the given option
  /// \p Id, with the provided \p Value and append it to the argument
  /// list.
  ///
  /// \param BaseArg Argument this synthesized arg is derived from, if any.
  /// \param Opt Option to instantiate as a separate argument.
  /// \param Value Separate value string.
  void AddSeparateArg(const Arg *BaseArg, const Option Opt,
                      StringRef Value) {
    append(MakeSeparateArg(BaseArg, Opt, Value));
  }

  /// AddJoinedArg - Construct a new Positional arg for the given option
  /// \p Id, with the provided \p Value and append it to the argument list.
  ///
  /// \param BaseArg Argument this synthesized arg is derived from, if any.
  /// \param Opt Option to instantiate as a joined argument.
  /// \param Value Joined value string.
  void AddJoinedArg(const Arg *BaseArg, const Option Opt,
                    StringRef Value) {
    append(MakeJoinedArg(BaseArg, Opt, Value));
  }

  /// MakeFlagArg - Construct a new FlagArg for the given option \p Id.
  ///
  /// \param BaseArg Argument this synthesized arg is derived from, if any.
  /// \param Opt Option to instantiate as a flag argument.
  /// \return The newly constructed flag argument.
  Arg *MakeFlagArg(const Arg *BaseArg, const Option Opt) const;

  /// MakePositionalArg - Construct a new Positional arg for the
  /// given option \p Id, with the provided \p Value.
  ///
  /// \param BaseArg Argument this synthesized arg is derived from, if any.
  /// \param Opt Option to instantiate as a positional argument.
  /// \param Value Positional value string.
  /// \return The newly constructed positional argument.
  Arg *MakePositionalArg(const Arg *BaseArg, const Option Opt,
                          StringRef Value) const;

  /// MakeSeparateArg - Construct a new Positional arg for the
  /// given option \p Id, with the provided \p Value.
  ///
  /// \param BaseArg Argument this synthesized arg is derived from, if any.
  /// \param Opt Option to instantiate as a separate argument.
  /// \param Value Separate value string.
  /// \return The newly constructed separate argument.
  Arg *MakeSeparateArg(const Arg *BaseArg, const Option Opt,
                        StringRef Value) const;

  /// MakeJoinedArg - Construct a new Positional arg for the
  /// given option \p Id, with the provided \p Value.
  ///
  /// \param BaseArg Argument this synthesized arg is derived from, if any.
  /// \param Opt Option to instantiate as a joined argument.
  /// \param Value Joined value string.
  /// \return The newly constructed joined argument.
  Arg *MakeJoinedArg(const Arg *BaseArg, const Option Opt,
                      StringRef Value) const;

  /// @}
};

} // end namespace opt

} // end namespace llvm

#endif // LLVM_OPTION_ARGLIST_H
