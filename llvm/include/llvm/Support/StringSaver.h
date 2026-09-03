//===- llvm/Support/StringSaver.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_STRINGSAVER_H
#define LLVM_SUPPORT_STRINGSAVER_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

/// Saves strings in the provided stable storage and returns a
/// StringRef with a stable character pointer.
class StringSaver final {
  BumpPtrAllocator &Alloc;

public:
  /// Use \p Alloc to allocate copies of saved strings.
  ///
  /// \param Alloc Allocator that owns the stable storage for saved strings.
  StringSaver(BumpPtrAllocator &Alloc) : Alloc(Alloc) {}

  /// Return the bump allocator used to store saved strings.
  ///
  /// \return The bump allocator that owns the stable storage for saved
  /// strings.
  BumpPtrAllocator &getAllocator() const { return Alloc; }

  /// Copy \p S into stable storage and return a null-terminated \c StringRef.
  ///
  /// All returned strings are null-terminated: *save(S).end() == 0.
  ///
  /// \param S Null-terminated C string to copy into stable storage.
  /// \return A null-terminated \c StringRef referencing the stable copy.
  StringRef save(const char *S) { return save(StringRef(S)); }
  /// Copy \p S into stable storage owned by this saver and return a
  /// \c StringRef referencing the copy.
  ///
  /// \param S String to copy into stable storage.
  /// \return A null-terminated \c StringRef referencing the stable copy.
  LLVM_ABI StringRef save(StringRef S);
  /// Copy \p S into stable storage and return a \c StringRef to the copy.
  ///
  /// \param S Twine to materialize and copy into stable storage.
  /// \return A null-terminated \c StringRef referencing the stable copy.
  LLVM_ABI StringRef save(const Twine &S);
  /// Copy \p S into stable storage and return a \c StringRef to the copy.
  ///
  /// \param S String to copy into stable storage.
  /// \return A null-terminated \c StringRef referencing the stable copy.
  StringRef save(const std::string &S) { return save(StringRef(S)); }
};

/// Saves strings in the provided stable storage and returns a StringRef with a
/// stable character pointer. Saving the same string yields the same StringRef.
///
/// Compared to StringSaver, it does more work but avoids saving the same string
/// multiple times.
///
/// Compared to StringPool, it performs fewer allocations but doesn't support
/// refcounting/deletion.
class UniqueStringSaver final {
  StringSaver Strings;
  llvm::DenseSet<llvm::StringRef> Unique;

public:
  /// Construct a saver that stores unique string copies in \p Alloc.
  ///
  /// \param Alloc Allocator that owns the stable storage for saved strings.
  UniqueStringSaver(BumpPtrAllocator &Alloc) : Strings(Alloc) {}

  /// Save \p S once, reusing a prior copy when the string was already saved.
  ///
  /// All returned strings are null-terminated: *save(S).end() == 0.
  ///
  /// \param S Null-terminated C string to save uniquely.
  /// \return A null-terminated \c StringRef to the unique stable copy.
  StringRef save(const char *S) { return save(StringRef(S)); }
  /// Save \p S once, reusing a prior copy when the string was already saved.
  ///
  /// \param S String to save uniquely into stable storage.
  /// \return A null-terminated \c StringRef to the unique stable copy.
  LLVM_ABI StringRef save(StringRef S);
  /// Save \p S once, reusing a prior copy when the string was already saved.
  ///
  /// \param S Twine to materialize and save uniquely into stable storage.
  /// \return A null-terminated \c StringRef to the unique stable copy.
  LLVM_ABI StringRef save(const Twine &S);
  /// Save \p S once, reusing a prior copy when the string was already saved.
  ///
  /// \param S String to save uniquely into stable storage.
  /// \return A null-terminated \c StringRef to the unique stable copy.
  StringRef save(const std::string &S) { return save(StringRef(S)); }
};

} // namespace llvm
#endif
