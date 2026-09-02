//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file declares interface for OnDiskDataAllocator, a file backed data
/// pool can be used to allocate space to store data packed in a single file. It
/// is based on MappedFileRegionArena and includes a header in the beginning to
/// provide metadata.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CAS_ONDISKDATAALLOCATOR_H
#define LLVM_CAS_ONDISKDATAALLOCATOR_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/CAS/FileOffset.h"
#include "llvm/CAS/OnDiskCASLogger.h"
#include "llvm/Support/Error.h"

namespace llvm::cas {

/// Variable-length on-disk data sink with 8-byte alignment.
///
/// Does not track size of data, which is assumed to known from context, or
/// embedded. Uses 0-padding but does not guarantee 0-termination.
class OnDiskDataAllocator {
public:
  /// Mutable view of allocated on-disk bytes.
  using ValueProxy = MutableArrayRef<char>;

  /// A pointer to data stored on disk.
  class OnDiskPtr {
  public:
    /// Return the file offset of the allocated data.
    FileOffset getOffset() const { return Offset; }
    /// Return true if this pointer refers to stored data.
    explicit operator bool() const { return bool(getOffset()); }
    /// Return a reference to the on-disk value proxy.
    const ValueProxy &operator*() const {
      assert(Offset && "Null dereference");
      return Value;
    }
    /// Access the on-disk value proxy through this pointer.
    const ValueProxy *operator->() const {
      assert(Offset && "Null dereference");
      return &Value;
    }

    /// Construct a null on-disk pointer.
    OnDiskPtr() = default;

  private:
    friend class OnDiskDataAllocator;
    OnDiskPtr(FileOffset Offset, ValueProxy Value)
        : Offset(Offset), Value(Value) {}
    FileOffset Offset;
    ValueProxy Value;
  };

  /// Get the data of \p Size stored at the given \p Offset. Note the allocator
  /// doesn't keep track of the allocation size, thus \p Size doesn't need to
  /// match the size of allocation but needs to be smaller.
  LLVM_ABI Expected<ArrayRef<char>> get(FileOffset Offset, size_t Size) const;

  /// Allocate at least \p Size with 8-byte alignment.
  LLVM_ABI Expected<OnDiskPtr> allocate(size_t Size);

  /// \returns the buffer that was allocated at \p create time, with size
  /// \p UserHeaderSize.
  LLVM_ABI MutableArrayRef<uint8_t> getUserHeader() const;

  /// Return the number of bytes currently allocated.
  LLVM_ABI size_t size() const;
  /// Return the maximum number of bytes this allocator may grow to.
  LLVM_ABI size_t capacity() const;

  /// Create or open an on-disk allocator at \p Path named \p TableName.
  LLVM_ABI static Expected<OnDiskDataAllocator>
  create(const Twine &Path, const Twine &TableName, uint64_t MaxFileSize,
         std::optional<uint64_t> NewFileInitialSize,
         uint32_t UserHeaderSize = 0,
         std::shared_ptr<ondisk::OnDiskCASLogger> Logger = nullptr,
         function_ref<void(void *)> UserHeaderInit = nullptr);

  /// Move-construct an allocator from \p RHS.
  LLVM_ABI OnDiskDataAllocator(OnDiskDataAllocator &&RHS);
  /// Move-assign this allocator from \p RHS.
  LLVM_ABI OnDiskDataAllocator &operator=(OnDiskDataAllocator &&RHS);

  // No copy. Just call \a create() again.
  /// Copy construction is not allowed.
  OnDiskDataAllocator(const OnDiskDataAllocator &) = delete;
  /// Copy assignment is not allowed.
  OnDiskDataAllocator &operator=(const OnDiskDataAllocator &) = delete;

  /// Destroy the allocator and release resources.
  LLVM_ABI ~OnDiskDataAllocator();

private:
  struct ImplType;
  explicit OnDiskDataAllocator(std::unique_ptr<ImplType> Impl);
  std::unique_ptr<ImplType> Impl;
};

} // namespace llvm::cas

#endif // LLVM_CAS_ONDISKDATAALLOCATOR_H
