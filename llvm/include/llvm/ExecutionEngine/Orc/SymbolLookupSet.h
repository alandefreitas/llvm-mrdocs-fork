//===------ SymbolLookupSet.h - Symbol set for ORC lookups ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// SymbolLookupSet class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_SYMBOLLOOKUPSET_H
#define LLVM_EXECUTIONENGINE_ORC_SYMBOLLOOKUPSET_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ExecutionEngine/Orc/CoreContainers.h"
#include "llvm/Support/Error.h"

#include <initializer_list>
#include <type_traits>
#include <utility>
#include <vector>

namespace llvm::orc {

/// Lookup flags that apply to each symbol in a lookup.
///
/// If RequiredSymbol is used (the default) for a given symbol then that symbol
/// must be found during the lookup or the lookup will fail returning a
/// SymbolNotFound error. If WeaklyReferencedSymbol is used and the given
/// symbol is not found then the query will continue, and no result for the
/// missing symbol will be present in the result (assuming the rest of the
/// lookup succeeds).
enum class SymbolLookupFlags {
  /// Symbol must be found or the lookup fails with SymbolNotFound.
  RequiredSymbol,
  /// Symbol may be absent; lookup continues without a result for it.
  WeaklyReferencedSymbol
};

/// A set of symbols to look up, each associated with a SymbolLookupFlags
/// value.
///
/// This class is backed by a vector and optimized for fast insertion,
/// deletion and iteration. It does not guarantee a stable order between
/// operations, and will not automatically detect duplicate elements (they
/// can be manually checked by calling the validate method).
class SymbolLookupSet {
public:
  /// Pair of a symbol name and its lookup flags.
  using value_type = std::pair<SymbolStringPtr, SymbolLookupFlags>;
  /// Vector storage backing this lookup set.
  using UnderlyingVector = std::vector<value_type>;
  /// Mutable iterator over lookup-set elements.
  using iterator = UnderlyingVector::iterator;
  /// Const iterator over lookup-set elements.
  using const_iterator = UnderlyingVector::const_iterator;

  /// Construct an empty SymbolLookupSet.
  SymbolLookupSet() = default;

  /// Construct a SymbolLookupSet from an initializer list of name/flags pairs.
  /// @param Elems Initial symbol/flag pairs to insert.
  SymbolLookupSet(std::initializer_list<value_type> Elems) {
    for (auto &E : Elems)
      Symbols.push_back(std::move(E));
  }

  /// Construct a SymbolLookupSet containing a single symbol.
  /// @param Name Symbol name to look up.
  /// @param Flags Lookup flags for \p Name.
  explicit SymbolLookupSet(
      SymbolStringPtr Name,
      SymbolLookupFlags Flags = SymbolLookupFlags::RequiredSymbol) {
    add(std::move(Name), Flags);
  }

  /// Construct a SymbolLookupSet from an initializer list of SymbolStringPtrs.
  /// @param Names Symbol names to look up.
  /// @param Flags Lookup flags applied to every name.
  explicit SymbolLookupSet(
      std::initializer_list<SymbolStringPtr> Names,
      SymbolLookupFlags Flags = SymbolLookupFlags::RequiredSymbol) {
    Symbols.reserve(Names.size());
    for (const auto &Name : Names)
      add(std::move(Name), Flags);
  }

  /// Construct a SymbolLookupSet from a SymbolNameSet with the given
  /// Flags used for each value.
  /// @param Names Set of symbol names to look up.
  /// @param Flags Lookup flags applied to every name.
  explicit SymbolLookupSet(
      const SymbolNameSet &Names,
      SymbolLookupFlags Flags = SymbolLookupFlags::RequiredSymbol) {
    Symbols.reserve(Names.size());
    for (const auto &Name : Names)
      add(Name, Flags);
  }

  /// Construct a SymbolLookupSet from a vector of symbols.
  ///
  /// The given Flags are used for each value. If the ArrayRef contains
  /// duplicates it is up to the client to remove these before using this
  /// instance for lookup.
  /// @param Names Symbol names to look up.
  /// @param Flags Lookup flags applied to every name.
  explicit SymbolLookupSet(
      ArrayRef<SymbolStringPtr> Names,
      SymbolLookupFlags Flags = SymbolLookupFlags::RequiredSymbol) {
    Symbols.reserve(Names.size());
    for (const auto &Name : Names)
      add(Name, Flags);
  }

  /// Construct a SymbolLookupSet from DenseMap keys.
  /// @param M Map whose keys become the symbols to look up.
  /// @param Flags Lookup flags applied to every key.
  /// @return A SymbolLookupSet containing the map's keys.
  template <typename ValT>
  static SymbolLookupSet
  fromMapKeys(const DenseMap<SymbolStringPtr, ValT> &M,
              SymbolLookupFlags Flags = SymbolLookupFlags::RequiredSymbol) {
    SymbolLookupSet Result;
    Result.Symbols.reserve(M.size());
    for (const auto &[Name, Val] : M)
      Result.add(Name, Flags);
    return Result;
  }

  /// Add an element to the set. The client is responsible for checking that
  /// duplicates are not added.
  /// @param Name Symbol name to insert.
  /// @param Flags Lookup flags for \p Name.
  /// @return A reference to this set for chaining.
  SymbolLookupSet &
  add(SymbolStringPtr Name,
      SymbolLookupFlags Flags = SymbolLookupFlags::RequiredSymbol) {
    Symbols.push_back(std::make_pair(std::move(Name), Flags));
    return *this;
  }

  /// Quickly append one lookup set to another.
  /// @param Other Lookup set whose elements are appended.
  /// @return A reference to this set for chaining.
  SymbolLookupSet &append(SymbolLookupSet Other) {
    Symbols.reserve(Symbols.size() + Other.size());
    for (auto &KV : Other)
      Symbols.push_back(std::move(KV));
    return *this;
  }

  /// Return true if this set contains no symbols.
  /// @return True if this set contains no symbols.
  bool empty() const { return Symbols.empty(); }
  /// Return the number of symbols in this set.
  /// @return The number of symbols in this set.
  UnderlyingVector::size_type size() const { return Symbols.size(); }
  /// Return an iterator to the first element.
  /// @return An iterator to the first element.
  iterator begin() { return Symbols.begin(); }
  /// Return an iterator past the last element.
  /// @return An iterator past the last element.
  iterator end() { return Symbols.end(); }
  /// Return a const iterator to the first element.
  /// @return A const iterator to the first element.
  const_iterator begin() const { return Symbols.begin(); }
  /// Return a const iterator past the last element.
  /// @return A const iterator past the last element.
  const_iterator end() const { return Symbols.end(); }

  /// Removes the Ith element of the vector, replacing it with the last element.
  /// @param I Index of the element to remove.
  void remove(UnderlyingVector::size_type I) {
    std::swap(Symbols[I], Symbols.back());
    Symbols.pop_back();
  }

  /// Removes the element pointed to by the given iterator. This iterator and
  /// all subsequent ones (including end()) are invalidated.
  /// @param I Iterator pointing at the element to remove.
  void remove(iterator I) { remove(I - begin()); }

  /// Removes all elements matching the given predicate, which must be callable
  /// as bool(const SymbolStringPtr &, SymbolLookupFlags Flags).
  /// @param Pred Predicate returning true for elements that should be removed.
  template <typename PredFn> void remove_if(PredFn &&Pred) {
    UnderlyingVector::size_type I = 0;
    while (I != Symbols.size()) {
      const auto &Name = Symbols[I].first;
      auto Flags = Symbols[I].second;
      if (Pred(Name, Flags))
        remove(I);
      else
        ++I;
    }
  }

  /// Apply Body to each element, removing those for which it returns true.
  ///
  /// Body must be callable as bool(const SymbolStringPtr &, SymbolLookupFlags).
  /// If Body returns true then the element just passed in is removed from the
  /// set. If Body returns false then the element is retained.
  /// @param Body Callback invoked for each element; true means remove.
  template <typename BodyFn>
  auto forEachWithRemoval(BodyFn &&Body) -> std::enable_if_t<
      std::is_same<decltype(Body(std::declval<const SymbolStringPtr &>(),
                                 std::declval<SymbolLookupFlags>())),
                   bool>::value> {
    UnderlyingVector::size_type I = 0;
    while (I != Symbols.size()) {
      const auto &Name = Symbols[I].first;
      auto Flags = Symbols[I].second;
      if (Body(Name, Flags))
        remove(I);
      else
        ++I;
    }
  }

  /// Apply Body to each element, removing those for which it returns true.
  ///
  /// Body must be callable as Expected<bool>(const SymbolStringPtr &,
  /// SymbolLookupFlags). If Body returns a failure value, the loop exits
  /// immediately. If Body returns true then the element just passed in is
  /// removed from the set. If Body returns false then the element is retained.
  /// @param Body Callback invoked for each element; true means remove.
  /// @return Success, or the first error returned by Body.
  template <typename BodyFn>
  auto forEachWithRemoval(BodyFn &&Body) -> std::enable_if_t<
      std::is_same<decltype(Body(std::declval<const SymbolStringPtr &>(),
                                 std::declval<SymbolLookupFlags>())),
                   Expected<bool>>::value,
      Error> {
    UnderlyingVector::size_type I = 0;
    while (I != Symbols.size()) {
      const auto &Name = Symbols[I].first;
      auto Flags = Symbols[I].second;
      auto Remove = Body(Name, Flags);
      if (!Remove)
        return Remove.takeError();
      if (*Remove)
        remove(I);
      else
        ++I;
    }
    return Error::success();
  }

  /// Construct a SymbolNameVector from this instance by dropping the Flags
  /// values.
  /// @return A vector of the symbol names without flags.
  SymbolNameVector getSymbolNames() const {
    SymbolNameVector Names;
    Names.reserve(Symbols.size());
    for (const auto &KV : Symbols)
      Names.push_back(KV.first);
    return Names;
  }

  /// Sort the lookup set by pointer value. This sort is fast but sensitive to
  /// allocation order and so should not be used where a consistent order is
  /// required.
  void sortByAddress() { llvm::sort(Symbols, llvm::less_first()); }

  /// Sort the lookup set lexicographically. This sort is slow but the order
  /// is unaffected by allocation order.
  void sortByName() {
    llvm::sort(Symbols, [](const value_type &LHS, const value_type &RHS) {
      return *LHS.first < *RHS.first;
    });
  }

  /// Merge duplicate names so each name appears exactly once.
  ///
  /// If a SymbolLookupSet is not duplicate-free by construction, this method
  /// can be used to turn it into a proper set.
  ///
  /// Entries sharing a name need not agree on their flags. Where they differ
  /// the strongest requirement wins: if any entry required the symbol then the
  /// merged entry requires it too, so that a missing definition still fails the
  /// lookup.
  void mergeEntries() {
    if (Symbols.size() < 2)
      return;
    sortByAddress();

    auto Out = Symbols.begin();
    for (auto In = Out + 1; In != Symbols.end(); ++In) {
      if (In->first == Out->first) {
        // Same name: keep the stronger requirement.
        if (In->second == SymbolLookupFlags::RequiredSymbol)
          Out->second = SymbolLookupFlags::RequiredSymbol;
      } else {
        // New name: compact it down next to the previous survivor.
        ++Out;
        if (Out != In)
          *Out = std::move(*In);
      }
    }
    Symbols.erase(std::next(Out), Symbols.end());
  }

#ifndef NDEBUG
  /// Returns true if this set contains any duplicates. This should only be used
  /// in assertions.
  /// @return True if this set contains any duplicate names.
  bool containsDuplicates() {
    if (Symbols.size() < 2)
      return false;
    sortByAddress();
    for (UnderlyingVector::size_type I = 1; I != Symbols.size(); ++I)
      if (Symbols[I].first == Symbols[I - 1].first)
        return true;
    return false;
  }
#endif

private:
  UnderlyingVector Symbols;
};

} // namespace llvm::orc

#endif // LLVM_EXECUTIONENGINE_ORC_SYMBOLLOOKUPSET_H
