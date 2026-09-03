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
    ///
    /// \returns File offset of the stored data, or a null offset when empty.
    FileOffset getOffset() const { return Offset; }
    /// Return true if this pointer refers to stored data.
    ///
    /// \returns True if this pointer has a non-null file offset.
    explicit operator bool() const { return bool(getOffset()); }
    /// Return a reference to the on-disk value proxy.
    ///
    /// \returns Const reference to the mutable on-disk byte view.
    const ValueProxy &operator*() const {
      assert(Offset && "Null dereference");
      return Value;
    }
    /// Access the on-disk value proxy through this pointer.
    ///
    /// \returns Pointer to the mutable on-disk byte view.
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

  /// Get the data of \p Size stored at the given \p Offset.
  ///
  /// Note the allocator doesn't keep track of the allocation size, thus
  /// \p Size doesn't need to match the size of allocation but needs to be
  /// smaller.
  ///
  /// \param Offset File offset of the stored data.
  /// \param Size Number of bytes to read; must not exceed the allocation.
  /// \returns Array of \p Size bytes at \p Offset, or an error on failure.
  LLVM_ABI Expected<ArrayRef<char>> get(FileOffset Offset, size_t Size) const;

  /// Allocate at least \p Size with 8-byte alignment.
  ///
  /// \param Size Minimum number of bytes to allocate.
  /// \returns Pointer to the allocated on-disk region, or an error on failure.
  LLVM_ABI Expected<OnDiskPtr> allocate(size_t Size);

  /// Return the user header buffer reserved at create time.
  ///
  /// \returns the buffer that was allocated at \p create time, with size
  /// \p UserHeaderSize.
  LLVM_ABI MutableArrayRef<uint8_t> getUserHeader() const;

  /// Return the number of bytes currently allocated.
  ///
  /// \returns Current allocated size in bytes.
  LLVM_ABI size_t size() const;
  /// Return the maximum number of bytes this allocator may grow to.
  ///
  /// \returns Maximum capacity of the on-disk allocator in bytes.
  LLVM_ABI size_t capacity() const;

  /// Create or open an on-disk allocator at \p Path named \p TableName.
  ///
  /// \param Path Path of the on-disk allocator file.
  /// \param TableName Identifier name for the data allocator table.
  /// \param MaxFileSize Maximum size of the mapped file region.
  /// \param NewFileInitialSize Starting size when creating a new file.
  /// \param UserHeaderSize Size of the optional user header reserved after the
  /// allocator header; requires \p UserHeaderInit when non-zero.
  /// \param Logger Optional logger for on-disk CAS operations.
  /// \param UserHeaderInit Callback invoked to initialize the user header of a
  /// newly created file; receives a pointer to the user header bytes.
  /// \returns Opened or newly created on-disk allocator, or an error on
  /// failure.
  LLVM_ABI static Expected<OnDiskDataAllocator>
  create(const Twine &Path, const Twine &TableName, uint64_t MaxFileSize,
         std::optional<uint64_t> NewFileInitialSize,
         uint32_t UserHeaderSize = 0,
         std::shared_ptr<ondisk::OnDiskCASLogger> Logger = nullptr,
         function_ref<void(void *)> UserHeaderInit = nullptr);

  /// Move-construct an allocator from \p RHS.
  ///
  /// \param RHS Allocator to move from.
  LLVM_ABI OnDiskDataAllocator(OnDiskDataAllocator &&RHS);
  /// Move-assign this allocator from \p RHS.
  ///
  /// \param RHS Allocator to move from.
  /// \returns Reference to this allocator after the move.
  LLVM_ABI OnDiskDataAllocator &operator=(OnDiskDataAllocator &&RHS);

  // No copy. Just call \a create() again.
  /// Copy construction is not allowed.
  ///
  /// \param RHS Unused; copy construction is not supported.
  OnDiskDataAllocator(const OnDiskDataAllocator &RHS) = delete;
  /// Copy assignment is not allowed.
  ///
  /// \param RHS Unused; copy assignment is not supported.
  OnDiskDataAllocator &operator=(const OnDiskDataAllocator &RHS) = delete;

  /// Destroy the allocator and release resources.
  LLVM_ABI ~OnDiskDataAllocator();

private:
  struct ImplType;
  explicit OnDiskDataAllocator(std::unique_ptr<ImplType> Impl);
  std::unique_ptr<ImplType> Impl;
};

} // namespace llvm::cas

#endif // LLVM_CAS_ONDISKDATAALLOCATOR_H
