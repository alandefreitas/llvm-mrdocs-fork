//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file declares interface for MappedFileRegionArena, a bump pointer
/// allocator, backed by a memory-mapped file.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CAS_MAPPEDFILEREGIONARENA_H
#define LLVM_CAS_MAPPEDFILEREGIONARENA_H

#include "llvm/Support/Alignment.h"
#include "llvm/Support/FileSystem.h"
#include <atomic>

namespace llvm::cas {

namespace ondisk {
class OnDiskCASLogger;
} // namespace ondisk

/// Allocator for an owned mapped file region that supports thread-safe and
/// process-safe bump pointer allocation.
///
/// This allocator is designed to create a sparse file when supported by the
/// filesystem's \c ftruncate so that it can be used with a large maximum size.
/// It will also attempt to shrink the underlying file down to its current
/// allocation size when the last concurrent mapping is closed.
///
/// Process-safe. Uses file locks when resizing the file during initialization
/// and destruction.
///
/// Thread-safe. Requires OS support thread-safe file lock.
///
/// Provides 8-byte alignment for all allocations.
class MappedFileRegionArena {
public:
  /// Memory-mapped file region type used for on-disk storage.
  using RegionT = sys::fs::mapped_file_region;

  /// Header for MappedFileRegionArena. It can be configured to be located
  /// at any location within the file and the allocation will be appended after
  /// the header.
  struct Header {
    /// Next allocation offset from the start of the mapped region.
    std::atomic<uint64_t> BumpPtr;
    /// Number of bytes currently allocated on disk.
    std::atomic<uint64_t> AllocatedSize;
    /// Maximum size the mapped file may grow to.
    std::atomic<uint64_t> Capacity;
    /// Offset from the beginning of the file to this header (for verification).
    std::atomic<uint64_t> HeaderOffset;
  };

  /// Create a \c MappedFileRegionArena.
  ///
  /// \param Path the path to open the mapped region.
  /// \param Capacity the maximum size for the mapped file region.
  /// \param HeaderOffset the offset at which to store the header. This is so
  /// that information can be stored before the header, like a file magic.
  /// \param NewFileConstructor is for constructing new files. It has exclusive
  /// access to the file. Must call \c initializeBumpPtr.
  LLVM_ABI static Expected<MappedFileRegionArena>
  create(const Twine &Path, uint64_t Capacity, uint64_t HeaderOffset,
         std::shared_ptr<ondisk::OnDiskCASLogger> Logger,
         function_ref<Error(MappedFileRegionArena &)> NewFileConstructor);

  /// Minimum alignment for allocations, currently hardcoded to 8B.
  static constexpr Align getAlign() {
    // Trick Align into giving us '8' as a constexpr.
    struct alignas(8) T {};
    static_assert(alignof(T) == 8, "Tautology failed?");
    return Align::Of<T>();
  }

  /// Allocate at least \p AllocSize. Rounds up to \a getAlign().
  Expected<char *> allocate(uint64_t AllocSize) {
    auto Offset = allocateOffset(AllocSize);
    if (LLVM_UNLIKELY(!Offset))
      return Offset.takeError();
    return data() + *Offset;
  }
  /// Allocate, returning the offset from \a data() instead of a pointer.
  LLVM_ABI Expected<int64_t> allocateOffset(uint64_t AllocSize);

  /// Return a pointer to the start of the mapped region.
  char *data() const { return Region.data(); }
  /// Return the number of bytes currently allocated from the bump pointer.
  uint64_t size() const { return H->BumpPtr; }
  /// Return the size of the mapped file region.
  uint64_t capacity() const { return Region.size(); }

  /// Return the underlying mapped file region.
  RegionT &getRegion() { return Region; }

  /// Unmap the region and shrink the backing file when this is the last mapping.
  ~MappedFileRegionArena() { destroyImpl(); }

  /// Default-construct an empty arena with no mapped region.
  MappedFileRegionArena() = default;
  /// Move-construct, taking ownership of \p RHS's mapped region and metadata.
  MappedFileRegionArena(MappedFileRegionArena &&RHS) { moveImpl(RHS); }
  /// Move-assign, releasing this arena's resources and taking \p RHS's.
  MappedFileRegionArena &operator=(MappedFileRegionArena &&RHS) {
    destroyImpl();
    moveImpl(RHS);
    return *this;
  }

  /// Copy construction is not allowed.
  MappedFileRegionArena(const MappedFileRegionArena &) = delete;
  /// Copy assignment is not allowed.
  MappedFileRegionArena &operator=(const MappedFileRegionArena &) = delete;

private:
  // initialize header from offset.
  Error initializeHeader(uint64_t HeaderOffset);

  LLVM_ABI void destroyImpl();
  void moveImpl(MappedFileRegionArena &RHS) {
    std::swap(Region, RHS.Region);
    std::swap(H, RHS.H);
    std::swap(Path, RHS.Path);
    std::swap(FD, RHS.FD);
    std::swap(SharedLockFD, RHS.SharedLockFD);
    std::swap(Logger, RHS.Logger);
  }

private:
  RegionT Region;
  Header *H = nullptr;
  std::string Path;
  // File descriptor for the main storage file.
  std::optional<int> FD;
  // File descriptor for the file used as reader/writer lock.
  std::optional<int> SharedLockFD;
  std::shared_ptr<ondisk::OnDiskCASLogger> Logger = nullptr;
};

} // namespace llvm::cas

#endif // LLVM_CAS_MAPPEDFILEREGIONARENA_H
