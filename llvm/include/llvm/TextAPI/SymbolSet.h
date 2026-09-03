//===- llvm/TextAPI/SymbolSet.h - TAPI Symbol Set --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TEXTAPI_SYMBOLSET_H
#define LLVM_TEXTAPI_SYMBOLSET_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TextAPI/Architecture.h"
#include "llvm/TextAPI/ArchitectureSet.h"
#include "llvm/TextAPI/Symbol.h"
#include <stddef.h>

namespace llvm {

/// Key used to look up symbols by encode kind and name.
struct SymbolsMapKey {
  /// Encode kind of the symbol.
  MachO::EncodeKind Kind;
  /// Symbol name.
  StringRef Name;

  /// Construct a map key from an encode kind and name.
  ///
  /// \param Kind The encode kind of the symbol.
  /// \param Name The symbol name.
  SymbolsMapKey(MachO::EncodeKind Kind, StringRef Name)
      : Kind(Kind), Name(Name) {}
};

/// DenseMapInfo specialization so SymbolsMapKey can be used as a DenseMap key.
template <> struct DenseMapInfo<SymbolsMapKey> {
  /// Compute a hash value for \p Key.
  ///
  /// \param Key The map key to hash.
  /// \return The combined hash of the key's kind and name.
  static unsigned getHashValue(const SymbolsMapKey &Key) {
    return hash_combine(hash_value(Key.Kind), hash_value(Key.Name));
  }

  /// Return true if \p LHS and \p RHS compare equal.
  ///
  /// \param LHS Left-hand map key.
  /// \param RHS Right-hand map key.
  /// \return True if both keys have the same kind and name.
  static bool isEqual(const SymbolsMapKey &LHS, const SymbolsMapKey &RHS) {
    return std::tie(LHS.Kind, LHS.Name) == std::tie(RHS.Kind, RHS.Name);
  }
};

/// Compare two DenseMaps of SymbolsMapKey to Symbol pointers for equality.
///
/// \param LHS Left-hand map.
/// \param RHS Right-hand map.
/// \return True if both maps have the same size and equal symbol values.
template <typename DerivedT, typename KeyInfoT, typename BucketT>
bool operator==(const DenseMapBase<DerivedT, SymbolsMapKey, MachO::Symbol *,
                                   KeyInfoT, BucketT> &LHS,
                const DenseMapBase<DerivedT, SymbolsMapKey, MachO::Symbol *,
                                   KeyInfoT, BucketT> &RHS) {
  if (LHS.size() != RHS.size())
    return false;
  for (const auto &KV : LHS) {
    auto I = RHS.find(KV.first);
    if (I == RHS.end() || *I->second != *KV.second)
      return false;
  }
  return true;
}

/// Compare two DenseMaps of SymbolsMapKey to Symbol pointers for inequality.
///
/// \param LHS Left-hand map.
/// \param RHS Right-hand map.
/// \return True if the maps are not equal.
template <typename DerivedT, typename KeyInfoT, typename BucketT>
bool operator!=(const DenseMapBase<DerivedT, SymbolsMapKey, MachO::Symbol *,
                                   KeyInfoT, BucketT> &LHS,
                const DenseMapBase<DerivedT, SymbolsMapKey, MachO::Symbol *,
                                   KeyInfoT, BucketT> &RHS) {
  return !(LHS == RHS);
}

namespace MachO {

/// Collection of TextAPI symbols keyed by encode kind and name.
class SymbolSet {
private:
  llvm::BumpPtrAllocator Allocator;
  StringRef copyString(StringRef String) {
    if (String.empty())
      return {};
    void *Ptr = Allocator.Allocate(String.size(), 1);
    memcpy(Ptr, String.data(), String.size());
    return StringRef(reinterpret_cast<const char *>(Ptr), String.size());
  }

  using SymbolsMapType = llvm::DenseMap<SymbolsMapKey, Symbol *>;
  SymbolsMapType Symbols;

  LLVM_ABI Symbol *addGlobalImpl(EncodeKind, StringRef Name, SymbolFlags Flags);

public:
  /// Construct an empty symbol set.
  SymbolSet() = default;
  /// Deleted copy constructor.
  ///
  /// \param other The symbol set that would be copied.
  SymbolSet(const SymbolSet &other) = delete;
  /// Deleted copy assignment operator.
  ///
  /// \param other The symbol set that would be assigned from.
  /// \return Reference to this symbol set.
  SymbolSet &operator=(const SymbolSet &other) = delete;
  /// Destroy the symbol set and free owned symbols.
  LLVM_ABI ~SymbolSet();

  /// Add a global symbol for a single target.
  ///
  /// \param Kind The encode kind of the symbol.
  /// \param Name The symbol name.
  /// \param Flags The flags that describe attributes of the symbol.
  /// \param Targ The target to associate with the symbol.
  /// \return Non-owning pointer to the added or existing symbol.
  LLVM_ABI Symbol *addGlobal(EncodeKind Kind, StringRef Name, SymbolFlags Flags,
                             const Target &Targ);

  /// Return the number of symbols in the set.
  ///
  /// \return The number of stored symbols.
  size_t size() const { return Symbols.size(); }

  /// Add a global symbol for a range of targets.
  ///
  /// \param Kind The encode kind of the symbol.
  /// \param Name The symbol name.
  /// \param Flags The flags that describe attributes of the symbol.
  /// \param Targets The range of targets to associate with the symbol.
  /// \return Non-owning pointer to the added or existing symbol.
  template <typename RangeT, typename ElT = std::remove_reference_t<
                                 decltype(*std::begin(std::declval<RangeT>()))>>
  Symbol *addGlobal(EncodeKind Kind, StringRef Name, SymbolFlags Flags,
                    RangeT &&Targets) {
    auto *Global = addGlobalImpl(Kind, Name, Flags);
    for (const auto &Targ : Targets)
      Global->addTarget(Targ);
    if (Kind == EncodeKind::ObjectiveCClassEHType)
      addGlobal(EncodeKind::ObjectiveCClass, Name, Flags, Targets);
    return Global;
  }

  /// Find a symbol by kind and name.
  ///
  /// \param Kind The encode kind of the symbol.
  /// \param Name The symbol name.
  /// \param ObjCIF Objective-C interface symbol kind filter.
  /// \return Non-owning pointer to the symbol, or nullptr if not found.
  LLVM_ABI const Symbol *
  findSymbol(EncodeKind Kind, StringRef Name,
             ObjCIFSymbolKind ObjCIF = ObjCIFSymbolKind::None) const;

  /// Forward iterator over the symbols in a SymbolSet.
  struct const_symbol_iterator
      : public iterator_adaptor_base<
            const_symbol_iterator, SymbolsMapType::const_iterator,
            std::forward_iterator_tag, const Symbol *, ptrdiff_t,
            const Symbol *, const Symbol *> {
    /// Construct a default (singular) symbol iterator.
    const_symbol_iterator() = default;

    /// Construct a symbol iterator from an underlying map iterator.
    ///
    /// \param u The underlying const_iterator to adapt.
    template <typename U>
    const_symbol_iterator(U &&u)
        : iterator_adaptor_base(std::forward<U &&>(u)) {}

    /// Dereference the iterator to the pointed-to symbol.
    ///
    /// \return Pointer to the current symbol.
    reference operator*() const { return I->second; }
    /// Access the pointed-to symbol through the iterator.
    ///
    /// \return Pointer to the current symbol.
    pointer operator->() const { return I->second; }
  };

  /// Range of all symbols in the set.
  using const_symbol_range = iterator_range<const_symbol_iterator>;

  /// Filtered forward iterator over symbols in the set.
  using const_filtered_symbol_iterator =
      filter_iterator<const_symbol_iterator,
                      std::function<bool(const Symbol *)>>;
  /// Range of filtered symbols in the set.
  using const_filtered_symbol_range =
      iterator_range<const_filtered_symbol_iterator>;

  /// Range that contains all symbols.
  ///
  /// \return A range over every symbol in the set.
  const_symbol_range symbols() const { return Symbols; }

  /// Range that contains all defined and exported symbols.
  ///
  /// \return A filtered range of defined, non-reexported symbols.
  const_filtered_symbol_range exports() const {
    std::function<bool(const Symbol *)> fn = [](const Symbol *Symbol) {
      return !Symbol->isUndefined() && !Symbol->isReexported();
    };
    return make_filter_range(symbols(), fn);
  }

  /// Range that contains all reexported symbols.
  ///
  /// \return A filtered range of reexported symbols.
  const_filtered_symbol_range reexports() const {
    std::function<bool(const Symbol *)> fn = [](const Symbol *Symbol) {
      return Symbol->isReexported();
    };
    return make_filter_range(symbols(), fn);
  }

  /// Range that contains all undefined and exported symbols.
  ///
  /// \return A filtered range of undefined symbols.
  const_filtered_symbol_range undefineds() const {
    std::function<bool(const Symbol *)> fn = [](const Symbol *Symbol) {
      return Symbol->isUndefined();
    };
    return make_filter_range(symbols(), fn);
  }

  /// Compare this symbol set with another for equality.
  ///
  /// \param O The other symbol set.
  /// \return True if both sets contain equal symbols.
  LLVM_ABI bool operator==(const SymbolSet &O) const;

  /// Compare this symbol set with another for inequality.
  ///
  /// \param O The other symbol set.
  /// \return True if the sets are not equal.
  bool operator!=(const SymbolSet &O) const { return !(Symbols == O.Symbols); }

  /// Allocate memory from the set's bump allocator.
  ///
  /// \param Size Number of bytes to allocate.
  /// \param Align Alignment requirement in bytes.
  /// \return Pointer to the allocated memory.
  void *allocate(size_t Size, unsigned Align = 8) {
    return Allocator.Allocate(Size, Align);
  }
};

} // namespace MachO
} // namespace llvm
#endif // LLVM_TEXTAPI_SYMBOLSET_H
