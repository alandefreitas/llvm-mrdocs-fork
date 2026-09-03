//===- StringMapEntry.h - String Hash table map interface -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the StringMapEntry class - it is intended to be a low
/// dependency implementation detail of StringMap that is more suitable for
/// inclusion in public headers than StringMap.h itself is.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_STRINGMAPENTRY_H
#define LLVM_ADT_STRINGMAPENTRY_H

#include "llvm/ADT/StringRef.h"
#include <utility>

namespace llvm {

/// The "value type" of StringSet represented as an empty struct.
struct EmptyStringSetTag {};

/// StringMapEntryBase - Shared base class of StringMapEntry instances.
class StringMapEntryBase {
  size_t keyLength;

public:
  /// Construct a base entry that stores a key of length \p keyLength.
  ///
  /// \param keyLength Number of characters in the trailing key string.
  explicit StringMapEntryBase(size_t keyLength) : keyLength(keyLength) {}

  /// Return the length of the key string stored after this entry.
  ///
  /// @return Number of characters in the key.
  size_t getKeyLength() const { return keyLength; }

protected:
  /// Allocate an entry followed by a copy of \p Key.
  ///
  /// \param EntrySize Size of the entry object preceding the key bytes.
  /// \param EntryAlign Alignment of the entry object.
  /// \param Key Key string copied after the entry.
  /// \param Allocator Allocator that provides the storage.
  /// @return Pointer to the allocated entry storage.
  template <typename AllocatorTy>
  static void *allocateWithKey(size_t EntrySize, size_t EntryAlign,
                               StringRef Key, AllocatorTy &Allocator);
};

// Define out-of-line to dissuade inlining.
template <typename AllocatorTy>
void *StringMapEntryBase::allocateWithKey(size_t EntrySize, size_t EntryAlign,
                                          StringRef Key,
                                          AllocatorTy &Allocator) {
  size_t KeyLength = Key.size();

  // Allocate a new item with space for the string at the end and a null
  // terminator.
  size_t AllocSize = EntrySize + KeyLength + 1;
  void *Allocation = Allocator.Allocate(AllocSize, EntryAlign);
  assert(Allocation && "Unhandled out-of-memory");

  // Copy the string information.
  char *Buffer = reinterpret_cast<char *>(Allocation) + EntrySize;
  if (KeyLength > 0)
    ::memcpy(Buffer, Key.data(), KeyLength);
  Buffer[KeyLength] = 0; // Null terminate for convenience of clients.
  return Allocation;
}

/// StringMapEntryStorage - Holds the value in a StringMapEntry.
///
/// Factored out into a separate base class to make it easier to specialize.
/// This is primarily intended to support StringSet, which doesn't need a value
/// stored at all.
template <typename ValueTy>
class StringMapEntryStorage : public StringMapEntryBase {
public:
  /// Mapped value associated with the entry's key.
  ValueTy second;

  /// Construct storage with a default-constructed value and key length \p keyLength.
  ///
  /// \param keyLength Number of characters in the trailing key string.
  explicit StringMapEntryStorage(size_t keyLength)
      : StringMapEntryBase(keyLength), second() {}
  /// Construct storage, initializing the value from \p initVals.
  ///
  /// \param keyLength Number of characters in the trailing key string.
  /// \param initVals Arguments forwarded to construct \c second.
  template <typename... InitTy>
  StringMapEntryStorage(size_t keyLength, InitTy &&...initVals)
      : StringMapEntryBase(keyLength),
        second(std::forward<InitTy>(initVals)...) {}
  /// Copy construction is deleted; entries are owned by the map.
  ///
  /// \param e Unused; copy construction is not supported.
  StringMapEntryStorage(StringMapEntryStorage &e) = delete;

  /// Return a const reference to the mapped value.
  ///
  /// @return Const reference to \c second.
  const ValueTy &getValue() const { return second; }
  /// Return a mutable reference to the mapped value.
  ///
  /// @return Mutable reference to \c second.
  ValueTy &getValue() { return second; }

  /// Replace the mapped value with \p V.
  ///
  /// \param V New value to store.
  void setValue(const ValueTy &V) { second = V; }
};

/// StringMapEntryStorage specialization for StringSet entries with no value.
template <>
class StringMapEntryStorage<EmptyStringSetTag> : public StringMapEntryBase {
public:
  /// Construct storage for a key of length \p keyLength with no mapped value.
  ///
  /// \param keyLength Number of characters in the trailing key string.
  /// \param tag Unused optional tag distinguishing this specialization.
  explicit StringMapEntryStorage(size_t keyLength,
                                 EmptyStringSetTag tag = {})
      : StringMapEntryBase(keyLength) {}
  /// Copy construction is deleted; entries are owned by the set.
  ///
  /// \param entry Unused; copy construction is not supported.
  StringMapEntryStorage(StringMapEntryStorage &entry) = delete;

  /// Return an empty tag standing in for the absent mapped value.
  ///
  /// @return Default-constructed \c EmptyStringSetTag.
  EmptyStringSetTag getValue() const { return {}; }
};

/// StringMapEntry - This is used to represent one value that is inserted into
/// a StringMap.  It contains the Value itself and the key: the string length
/// and data.
template <typename ValueTy>
class StringMapEntry final : public StringMapEntryStorage<ValueTy> {
public:
  /// Inherit constructors from the storage base class.
  using StringMapEntryStorage<ValueTy>::StringMapEntryStorage;

  /// Type of the mapped value stored in this entry.
  using ValueType = ValueTy;

  /// Return the key string as a StringRef.
  ///
  /// @return StringRef over the key bytes stored after this entry.
  StringRef getKey() const {
    return StringRef(getKeyData(), this->getKeyLength());
  }

  /// getKeyData - Return the start of the string data that is the key for this
  /// value. The string data is always stored immediately after the
  /// StringMapEntry object.
  ///
  /// @return Pointer to the first character of the key.
  const char *getKeyData() const {
    return reinterpret_cast<const char *>(this + 1);
  }

  /// Alias for getKey(), enabling structured-binding-like access as \c first.
  ///
  /// @return StringRef over the key bytes stored after this entry.
  StringRef first() const { return getKey(); }

  /// Create a StringMapEntry for the specified key construct the value using
  /// \p InitiVals.
  ///
  /// \param key Key string stored after the entry.
  /// \param allocator Allocator that provides the storage.
  /// \param initVals Arguments forwarded to construct the mapped value.
  /// @return Pointer to the newly allocated and constructed entry.
  template <typename AllocatorTy, typename... InitTy>
  static StringMapEntry *create(StringRef key, AllocatorTy &allocator,
                                InitTy &&...initVals) {
    return new (StringMapEntryBase::allocateWithKey(
        sizeof(StringMapEntry), alignof(StringMapEntry), key, allocator))
        StringMapEntry(key.size(), std::forward<InitTy>(initVals)...);
  }

  /// GetStringMapEntryFromKeyData - Given key data that is known to be embedded
  /// into a StringMapEntry, return the StringMapEntry itself.
  ///
  /// \param keyData Pointer to the key bytes stored after a StringMapEntry.
  /// @return Reference to the StringMapEntry that precedes \p keyData.
  static StringMapEntry &GetStringMapEntryFromKeyData(const char *keyData) {
    char *ptr = const_cast<char *>(keyData) - sizeof(StringMapEntry<ValueTy>);
    return *reinterpret_cast<StringMapEntry *>(ptr);
  }

  /// Destroy - Destroy this StringMapEntry, releasing memory back to the
  /// specified allocator.
  ///
  /// \param allocator Allocator that originally provided this entry's storage.
  template <typename AllocatorTy> void Destroy(AllocatorTy &allocator) {
    // Free memory referenced by the item.
    size_t AllocSize = sizeof(StringMapEntry) + this->getKeyLength() + 1;
    this->~StringMapEntry();
    allocator.Deallocate(static_cast<void *>(this), AllocSize,
                         alignof(StringMapEntry));
  }
};

// Allow structured bindings on StringMapEntry.

/// Structured-binding accessor for element \p Index of mutable entry \p E.
///
/// \tparam Index 0 for the key, 1 for the value.
/// \param E Entry to project.
/// @return The key (\c Index 0) or mapped value (\c Index 1) of \p E.
template <std::size_t Index, typename ValueTy>
decltype(auto) get(StringMapEntry<ValueTy> &E) {
  static_assert(Index < 2);
  if constexpr (Index == 0)
    return E.getKey();
  else
    return E.getValue();
}

/// Structured-binding accessor for element \p Index of const entry \p E.
///
/// \tparam Index 0 for the key, 1 for the value.
/// \param E Entry to project.
/// @return The key (\c Index 0) or mapped value (\c Index 1) of \p E.
template <std::size_t Index, typename ValueTy>
decltype(auto) get(const StringMapEntry<ValueTy> &E) {
  static_assert(Index < 2);
  if constexpr (Index == 0)
    return E.getKey();
  else
    return E.getValue();
}

} // end namespace llvm

template <typename ValueTy>
struct std::tuple_size<llvm::StringMapEntry<ValueTy>>
    : std::integral_constant<std::size_t, 2> {};

template <std::size_t Index, typename ValueTy>
struct std::tuple_element<Index, llvm::StringMapEntry<ValueTy>>
    : std::tuple_element<Index, std::pair<llvm::StringRef, ValueTy>> {};

#endif // LLVM_ADT_STRINGMAPENTRY_H
