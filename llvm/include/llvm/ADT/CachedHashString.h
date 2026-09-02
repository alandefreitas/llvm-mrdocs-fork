//===- llvm/ADT/CachedHashString.h - Prehashed string/StringRef -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines CachedHashString and CachedHashStringRef.  These are
/// owning and not-owning string types that store their hash in addition to
/// their string data.
///
/// Unlike std::string, CachedHashString can be used in DenseSet/DenseMap
/// (because, unlike std::string, CachedHashString lets us have an empty
/// value).
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_CACHEDHASHSTRING_H
#define LLVM_ADT_CACHEDHASHSTRING_H

#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/StringRef.h"

namespace llvm {

/// A container which contains a StringRef plus a precomputed hash.
class CachedHashStringRef {
  const char *P;
  uint32_t Size;
  uint32_t Hash;

public:
  /// Construct from string \p S, computing its DenseMap hash.
  ///
  /// Explicit because hashing a string isn't free.
  explicit CachedHashStringRef(StringRef S)
      : CachedHashStringRef(S, DenseMapInfo<StringRef>::getHashValue(S)) {}

  /// Construct from string \p S with a precomputed \p Hash.
  ///
  /// \param S Non-owning string data.
  /// \param Hash Precomputed hash of \p S.
  CachedHashStringRef(StringRef S, uint32_t Hash)
      : P(S.data()), Size(S.size()), Hash(Hash) {
    assert(S.size() <= std::numeric_limits<uint32_t>::max());
  }

  /// Return the wrapped string as a StringRef.
  StringRef val() const { return StringRef(P, Size); }
  /// Return a pointer to the first character.
  const char *data() const { return P; }
  /// Return the string length in bytes.
  uint32_t size() const { return Size; }
  /// Return the cached hash value.
  uint32_t hash() const { return Hash; }
};

template <> struct DenseMapInfo<CachedHashStringRef> {
  static unsigned getHashValue(const CachedHashStringRef &S) {
    return S.hash();
  }
  static bool isEqual(const CachedHashStringRef &LHS,
                      const CachedHashStringRef &RHS) {
    return LHS.hash() == RHS.hash() &&
           DenseMapInfo<StringRef>::isEqual(LHS.val(), RHS.val());
  }
};

/// A container which contains a string, which it owns, plus a precomputed hash.
///
/// We do not null-terminate the string.
class CachedHashString {
  friend struct DenseMapInfo<CachedHashString>;

  char *P;
  uint32_t Size;
  uint32_t Hash;

  static char *getEmptyKeyPtr() {
    // Assume no pointer requires more than 4096 (1 << 12) bytes of alignment.
    return reinterpret_cast<char *>(static_cast<uintptr_t>(-1) << 12);
  }

  bool isEmpty() const { return P == getEmptyKeyPtr(); }

  // TODO: Use small-string optimization to avoid allocating.

public:
  /// Construct by copying the null-terminated C string \p S.
  explicit CachedHashString(const char *S) : CachedHashString(StringRef(S)) {}

  /// Construct by copying \p S and computing its DenseMap hash.
  ///
  /// Explicit because copying and hashing a string isn't free.
  explicit CachedHashString(StringRef S)
      : CachedHashString(S, DenseMapInfo<StringRef>::getHashValue(S)) {}

  /// Construct by copying \p S with a precomputed \p Hash.
  ///
  /// \param S String data to own a copy of.
  /// \param Hash Precomputed hash of \p S.
  CachedHashString(StringRef S, uint32_t Hash)
      : P(new char[S.size()]), Size(S.size()), Hash(Hash) {
    memcpy(P, S.data(), S.size());
  }

  /// Copy-construct, duplicating owned storage unless \p Other is empty.
  ///
  /// Ideally this class would not be copyable.  But SetVector requires copyable
  /// keys, and we want this to be usable there.
  CachedHashString(const CachedHashString &Other)
      : Size(Other.Size), Hash(Other.Hash) {
    if (Other.isEmpty()) {
      P = Other.P;
    } else {
      P = new char[Size];
      memcpy(P, Other.P, Size);
    }
  }

  /// Assign from \p Other using copy-and-swap.
  CachedHashString &operator=(CachedHashString Other) {
    swap(*this, Other);
    return *this;
  }

  /// Move-construct, leaving \p Other in the empty-key state.
  CachedHashString(CachedHashString &&Other) noexcept
      : P(Other.P), Size(Other.Size), Hash(Other.Hash) {
    Other.P = getEmptyKeyPtr();
  }

  /// Destroy and free owned string storage when not empty.
  ~CachedHashString() {
    if (!isEmpty())
      delete[] P;
  }

  /// Return the owned string as a StringRef.
  StringRef val() const { return StringRef(P, Size); }
  /// Return the string length in bytes.
  uint32_t size() const { return Size; }
  /// Return the cached hash value.
  uint32_t hash() const { return Hash; }

  /// Implicit conversion to a non-owning StringRef view.
  operator StringRef() const { return val(); }
  /// Implicit conversion to a non-owning CachedHashStringRef.
  operator CachedHashStringRef() const {
    return CachedHashStringRef(val(), Hash);
  }

  /// Exchange owned storage and hash state between \p LHS and \p RHS.
  friend void swap(CachedHashString &LHS, CachedHashString &RHS) {
    using std::swap;
    swap(LHS.P, RHS.P);
    swap(LHS.Size, RHS.Size);
    swap(LHS.Hash, RHS.Hash);
  }
};

template <> struct DenseMapInfo<CachedHashString> {
  static unsigned getHashValue(const CachedHashString &S) { return S.hash(); }
  static bool isEqual(const CachedHashString &LHS,
                      const CachedHashString &RHS) {
    if (LHS.hash() != RHS.hash())
      return false;
    if (LHS.P == CachedHashString::getEmptyKeyPtr())
      return RHS.P == CachedHashString::getEmptyKeyPtr();

    // This is safe because if RHS.P is the empty key, it will have length 0, so
    // we'll never dereference its pointer.
    return LHS.val() == RHS.val();
  }
};

} // namespace llvm

#endif
