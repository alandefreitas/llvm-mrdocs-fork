//===-- SymbolStringPool.h -- Thread-safe pool for JIT symbols --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Contains a thread-safe string pool suitable for use with ORC.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_SYMBOLSTRINGPOOL_H
#define LLVM_EXECUTIONENGINE_ORC_SYMBOLSTRINGPOOL_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Compiler.h"
#include <atomic>
#include <mutex>

namespace llvm {

class raw_ostream;

namespace orc {

class SymbolStringPtrBase;
class SymbolStringPtr;
class NonOwningSymbolStringPtr;

/// String pool for symbol names used by the JIT.
class SymbolStringPool {
  /// Unit test fixture with access to SymbolStringPool internals.
  friend class SymbolStringPoolTest;
  friend class SymbolStringPtrBase;
  friend class SymbolStringPoolEntryUnsafe;

  // Implemented in DebugUtils.h.
  LLVM_ABI friend raw_ostream &operator<<(raw_ostream &OS,
                                          const SymbolStringPool &SSP);

public:
  /// Destroy a SymbolStringPool.
  ~SymbolStringPool();

  /// Create a symbol string pointer from the given string.
  /// @param S Symbol name to intern.
  /// @return Owning pointer to the interned symbol name.
  SymbolStringPtr intern(StringRef S);

  /// Remove from the pool any entries that are no longer referenced.
  void clearDeadEntries();

  /// Returns true if the pool is empty.
  /// @return True if the pool is empty.
  bool empty() const;

private:
  size_t getRefCount(const SymbolStringPtrBase &S) const;

  using RefCountType = std::atomic<size_t>;
  using PoolMap = StringMap<RefCountType>;
  using PoolMapEntry = StringMapEntry<RefCountType>;
  mutable std::mutex PoolMutex;
  PoolMap Pool;
};

/// Base class for both owning and non-owning symbol-string ptrs.
///
/// All symbol-string ptrs are convertible to bool, dereferenceable and
/// comparable.
///
/// SymbolStringPtrBases are default-constructible and constructible
/// from nullptr to enable comparison with these values.
class SymbolStringPtrBase {
  friend class SymbolStringPool;
  friend class SymbolStringPoolEntryUnsafe;
  friend struct DenseMapInfo<SymbolStringPtr>;
  friend struct DenseMapInfo<NonOwningSymbolStringPtr>;

public:
  /// Construct a null symbol-string pointer.
  SymbolStringPtrBase() = default;
  /// Construct a null symbol-string pointer from nullptr.
  /// @param Null Unused nullptr literal used to select this overload.
  SymbolStringPtrBase(std::nullptr_t Null) {}

  /// Copy-assign from \p Other.
  /// @param Other Source pointer to copy.
  /// @return Reference to this pointer.
  SymbolStringPtrBase &operator=(const SymbolStringPtrBase &Other) = default;
  /// Move-assign from \p Other.
  /// @param Other Source pointer to move from.
  /// @return Reference to this pointer.
  SymbolStringPtrBase &operator=(SymbolStringPtrBase &&Other) = default;

  /// Return true if this pointer refers to a pool entry.
  /// @return True if this pointer refers to a pool entry.
  explicit operator bool() const { return S; }

  /// Return the pooled symbol name as a StringRef.
  /// @return Pooled symbol name as a StringRef.
  StringRef operator*() const { return S->first(); }

  /// Return true if \p LHS and \p RHS refer to the same pool entry.
  /// @param LHS Left-hand symbol-string pointer.
  /// @param RHS Right-hand symbol-string pointer.
  /// @return True if \p LHS and \p RHS refer to the same pool entry.
  friend bool operator==(SymbolStringPtrBase LHS, SymbolStringPtrBase RHS) {
    return LHS.S == RHS.S;
  }

  /// Return true if \p LHS and \p RHS refer to different pool entries.
  /// @param LHS Left-hand symbol-string pointer.
  /// @param RHS Right-hand symbol-string pointer.
  /// @return True if \p LHS and \p RHS refer to different pool entries.
  friend bool operator!=(SymbolStringPtrBase LHS, SymbolStringPtrBase RHS) {
    return !(LHS == RHS);
  }

  /// Order \p LHS and \p RHS by their underlying pool-entry addresses.
  /// @param LHS Left-hand symbol-string pointer.
  /// @param RHS Right-hand symbol-string pointer.
  /// @return True if \p LHS's pool-entry address is less than \p RHS's.
  friend bool operator<(SymbolStringPtrBase LHS, SymbolStringPtrBase RHS) {
    return LHS.S < RHS.S;
  }

  /// Print the symbol name of \p Sym to \p OS.
  /// @param OS Output stream.
  /// @param Sym Symbol-string pointer to print.
  /// @return Reference to \p OS.
  LLVM_ABI friend raw_ostream &operator<<(raw_ostream &OS,
                                          const SymbolStringPtrBase &Sym);

#ifndef NDEBUG
  /// Return true if the pool entry is still alive.
  ///
  /// Returns true if the pool entry's ref count is above zero (or if the entry
  /// is an empty or tombstone value). Useful for debugging and testing -- this
  /// method can be used to identify SymbolStringPtrs and
  /// NonOwningSymbolStringPtrs that are pointing to abandoned pool entries.
  /// @return True if the pool entry is still alive.
  bool poolEntryIsAlive() const {
    return isRealPoolEntry(S) ? S->getValue() != 0 : true;
  }
#endif

protected:
  /// Pool map entry type storing a symbol name and its reference count.
  using PoolEntry = SymbolStringPool::PoolMapEntry;
  /// Pointer to a pool map entry.
  using PoolEntryPtr = PoolEntry *;

  /// Construct from an existing pool entry pointer.
  /// @param S Pool entry to wrap; may be null or a sentinel value.
  SymbolStringPtrBase(PoolEntryPtr S) : S(S) {}

  /// Bit mask identifying invalid/sentinel pool-entry pointer values.
  constexpr static uintptr_t InvalidPtrMask =
      (std::numeric_limits<uintptr_t>::max() - 3)
      << PointerLikeTypeTraits<PoolEntryPtr>::NumLowBitsAvailable;

  /// Return false for null, empty, and tombstone values, true otherwise.
  /// @param P Pool entry pointer to test.
  /// @return False for null, empty, and tombstone values; true otherwise.
  static bool isRealPoolEntry(PoolEntryPtr P) {
    return ((reinterpret_cast<uintptr_t>(P) - 1) & InvalidPtrMask) !=
           InvalidPtrMask;
  }

  /// Return the reference count of the pooled entry, or zero if not real.
  /// @return Reference count of the pooled entry, or zero if not real.
  size_t getRefCount() const {
    return isRealPoolEntry(S) ? size_t(S->getValue()) : size_t(0);
  }

  /// Underlying pool entry pointer; null when this is a null symbol pointer.
  PoolEntryPtr S = nullptr;
};

/// Pointer to a pooled string representing a symbol name.
class SymbolStringPtr : public SymbolStringPtrBase {
  friend class SymbolStringPool;
  friend class SymbolStringPoolEntryUnsafe;
  friend struct DenseMapInfo<SymbolStringPtr>;

public:
  /// Construct a null owning symbol-string pointer.
  SymbolStringPtr() = default;
  /// Construct a null owning symbol-string pointer from nullptr.
  /// @param Null Unused nullptr literal used to select this overload.
  SymbolStringPtr(std::nullptr_t Null) {}
  /// Copy-construct, retaining the shared pool entry.
  /// @param Other Source owning pointer to copy.
  SymbolStringPtr(const SymbolStringPtr &Other) : SymbolStringPtrBase(Other.S) {
    incRef();
  }

  /// Construct an owning pointer from a non-owning pointer, retaining the
  /// entry.
  /// @param Other Non-owning pointer whose pool entry is retained.
  explicit SymbolStringPtr(NonOwningSymbolStringPtr Other);

  /// Copy-assign, releasing the current entry and retaining \p Other's.
  /// @param Other Source owning pointer to copy.
  /// @return Reference to this pointer.
  SymbolStringPtr& operator=(const SymbolStringPtr &Other) {
    decRef();
    S = Other.S;
    incRef();
    return *this;
  }

  /// Move-construct, transferring ownership from \p Other.
  /// @param Other Source owning pointer left null after the move.
  SymbolStringPtr(SymbolStringPtr &&Other) { std::swap(S, Other.S); }

  /// Move-assign, releasing the current entry and taking ownership from \p
  /// Other.
  /// @param Other Source owning pointer left null after the move.
  /// @return Reference to this pointer.
  SymbolStringPtr& operator=(SymbolStringPtr &&Other) {
    decRef();
    S = nullptr;
    std::swap(S, Other.S);
    return *this;
  }

  /// Destroy this pointer, releasing its pool entry.
  ~SymbolStringPtr() { decRef(); }

private:
  SymbolStringPtr(PoolEntryPtr S) : SymbolStringPtrBase(S) { incRef(); }

  void incRef() {
    if (isRealPoolEntry(S))
      ++S->getValue();
  }

  void decRef() {
    if (isRealPoolEntry(S)) {
      assert(S->getValue() && "Releasing SymbolStringPtr with zero ref count");
      --S->getValue();
    }
  }
};

/// Provides unsafe access to ownership operations on SymbolStringPtr.
/// This class can be used to manage SymbolStringPtr instances from C.
class SymbolStringPoolEntryUnsafe {
public:
  /// Pool map entry type storing a symbol name and its reference count.
  using PoolEntry = SymbolStringPool::PoolMapEntry;

  /// Construct from a raw pool entry pointer.
  /// @param E Pool entry to wrap; ownership is not changed.
  SymbolStringPoolEntryUnsafe(PoolEntry *E) : E(E) {}

  /// Create an unsafe pool entry ref without changing the ref-count.
  /// @param S Symbol-string pointer whose underlying entry is borrowed.
  /// @return Unsafe handle to the pool entry of \p S.
  static SymbolStringPoolEntryUnsafe from(const SymbolStringPtrBase &S) {
    return S.S;
  }

  /// Consumes the given SymbolStringPtr without releasing the pool entry.
  /// @param S Owning pointer whose entry is taken; left null afterwards.
  /// @return Unsafe handle to the taken pool entry.
  static SymbolStringPoolEntryUnsafe take(SymbolStringPtr &&S) {
    PoolEntry *E = nullptr;
    std::swap(E, S.S);
    return E;
  }

  /// Return the underlying raw pool entry pointer.
  /// @return Underlying pool entry pointer.
  PoolEntry *rawPtr() { return E; }

  /// Creates a SymbolStringPtr for this entry, with the SymbolStringPtr
  /// retaining the entry as usual.
  /// @return Owning pointer that retains this entry.
  SymbolStringPtr copyToSymbolStringPtr() { return SymbolStringPtr(E); }

  /// Creates a SymbolStringPtr for this entry *without* performing a retain
  /// operation during construction.
  /// @return Owning pointer that takes this entry without retaining.
  SymbolStringPtr moveToSymbolStringPtr() {
    SymbolStringPtr S;
    std::swap(S.S, E);
    return S;
  }

  /// Increment the pool entry's reference count.
  void retain() { ++E->getValue(); }
  /// Decrement the pool entry's reference count.
  void release() { --E->getValue(); }

private:
  PoolEntry *E = nullptr;
};

/// Non-owning pointer to a SymbolStringPool entry.
///
/// Instances are comparable with SymbolStringPtr instances and guaranteed to
/// have the same hash, but do not affect the ref-count of the pooled string
/// (and are therefore cheaper to copy).
///
/// NonOwningSymbolStringPtrs are silently invalidated if the pool entry's
/// ref-count drops to zero, so they should only be used in contexts where a
/// corresponding SymbolStringPtr is known to exist (which will guarantee that
/// the ref-count stays above zero). E.g. in a graph where nodes are
/// represented by SymbolStringPtrs the edges can be represented by pairs of
/// NonOwningSymbolStringPtrs and this will make the introduction of deletion
/// of edges cheaper.
class NonOwningSymbolStringPtr : public SymbolStringPtrBase {
  friend struct DenseMapInfo<orc::NonOwningSymbolStringPtr>;

public:
  /// Construct a null non-owning symbol-string pointer.
  NonOwningSymbolStringPtr() = default;
  /// Construct a non-owning view of the pool entry held by \p S.
  /// @param S Owning pointer whose entry is viewed without retaining.
  explicit NonOwningSymbolStringPtr(const SymbolStringPtr &S)
      : SymbolStringPtrBase(S) {}

  /// Inherit assignment operators from SymbolStringPtrBase.
  using SymbolStringPtrBase::operator=;

private:
  NonOwningSymbolStringPtr(PoolEntryPtr S) : SymbolStringPtrBase(S) {}
};

/// Construct an owning pointer from a non-owning pointer, retaining the entry.
/// @param Other Non-owning pointer whose pool entry is retained.
inline SymbolStringPtr::SymbolStringPtr(NonOwningSymbolStringPtr Other)
    : SymbolStringPtrBase(Other) {
  assert(poolEntryIsAlive() &&
         "SymbolStringPtr constructed from invalid non-owning pointer.");

  if (isRealPoolEntry(S))
    ++S->getValue();
}

inline SymbolStringPool::~SymbolStringPool() {
#ifndef NDEBUG
  clearDeadEntries();
  assert(Pool.empty() && "Dangling references at pool destruction time");
#endif // NDEBUG
}

inline SymbolStringPtr SymbolStringPool::intern(StringRef S) {
  std::lock_guard<std::mutex> Lock(PoolMutex);
  PoolMap::iterator I;
  bool Added;
  std::tie(I, Added) = Pool.try_emplace(S, 0);
  return SymbolStringPtr(&*I);
}

inline void SymbolStringPool::clearDeadEntries() {
  std::lock_guard<std::mutex> Lock(PoolMutex);
  Pool.remove_if([](PoolMapEntry &E) { return E.getValue() == 0; });
}

inline bool SymbolStringPool::empty() const {
  std::lock_guard<std::mutex> Lock(PoolMutex);
  return Pool.empty();
}

inline size_t
SymbolStringPool::getRefCount(const SymbolStringPtrBase &S) const {
  return S.getRefCount();
}

LLVM_ABI raw_ostream &operator<<(raw_ostream &OS,
                                 const SymbolStringPtrBase &Sym);

/// Compute a hash value for symbol-string pointer \p S.
/// @param S Symbol-string pointer to hash.
/// @return Hash value for \p S.
inline hash_code hash_value(const orc::SymbolStringPtrBase &S) {
  return hash_value(orc::SymbolStringPoolEntryUnsafe::from(S).rawPtr());
}

} // end namespace orc

/// DenseMapInfo specialization for owning symbol-string pointers.
template <>
struct DenseMapInfo<orc::SymbolStringPtr> {

  /// Compute a hash value for \p V.
  /// @param V Symbol-string pointer to hash.
  /// @return Hash value for \p V.
  static unsigned getHashValue(const orc::SymbolStringPtrBase &V) {
    return DenseMapInfo<orc::SymbolStringPtr::PoolEntryPtr>::getHashValue(V.S);
  }

  /// Return true if \p LHS and \p RHS refer to the same pool entry.
  /// @param LHS Left-hand symbol-string pointer.
  /// @param RHS Right-hand symbol-string pointer.
  /// @return True if \p LHS and \p RHS refer to the same pool entry.
  static bool isEqual(const orc::SymbolStringPtrBase &LHS,
                      const orc::SymbolStringPtrBase &RHS) {
    return LHS.S == RHS.S;
  }
};

/// DenseMapInfo specialization for non-owning symbol-string pointers.
template <> struct DenseMapInfo<orc::NonOwningSymbolStringPtr> {

  /// Compute a hash value for \p V.
  /// @param V Symbol-string pointer to hash.
  /// @return Hash value for \p V.
  static unsigned getHashValue(const orc::SymbolStringPtrBase &V) {
    return DenseMapInfo<
        orc::NonOwningSymbolStringPtr::PoolEntryPtr>::getHashValue(V.S);
  }

  /// Return true if \p LHS and \p RHS refer to the same pool entry.
  /// @param LHS Left-hand symbol-string pointer.
  /// @param RHS Right-hand symbol-string pointer.
  /// @return True if \p LHS and \p RHS refer to the same pool entry.
  static bool isEqual(const orc::SymbolStringPtrBase &LHS,
                      const orc::SymbolStringPtrBase &RHS) {
    return LHS.S == RHS.S;
  }
};

} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_SYMBOLSTRINGPOOL_H
