//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file declares interface for OnDiskCASLogger, an interface that can be
/// used to log CAS events to help debugging CAS errors.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CAS_ONDISKLOGGER_H
#define LLVM_CAS_ONDISKLOGGER_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <memory>

namespace llvm {
class raw_fd_ostream;
class Twine;
} // namespace llvm

namespace llvm::cas {
/// On-disk content-addressable storage utilities.
namespace ondisk {

/// Interface for logging low-level on-disk cas operations.
///
/// This log is intended to mirror the low-level details of the CAS in order to
/// aid with debugging corruption or other issues with the on-disk format.
class OnDiskCASLogger {
public:
  /// Create or append to a log file inside the given CAS directory \p Path.
  ///
  /// \param Path The parent directory of the log file.
  /// \param LogAllocations Whether to log all low-level allocations. This is
  ///                       on the order of twice as expensive to log.
  /// \returns the opened logger, or an error on failure.
  LLVM_ABI static Expected<std::unique_ptr<OnDiskCASLogger>>
  open(const Twine &Path, bool LogAllocations);

  /// Open a CAS log file when \c LLVM_CAS_LOG enables logging.
  ///
  /// Create or append to a log file inside the given CAS directory \p Path if
  /// logging is enabled by the environment variable \c LLVM_CAS_LOG. If
  /// LLVM_CAS_LOG is set >= 2 then also log allocations.
  ///
  /// \param Path The parent directory of the log file.
  /// \returns the opened logger when logging is enabled, a null unique_ptr
  ///          when disabled, or an error on failure.
  LLVM_ABI static Expected<std::unique_ptr<OnDiskCASLogger>>
  openIfEnabled(const Twine &Path);

  /// Destroy the logger and flush pending output.
  LLVM_ABI ~OnDiskCASLogger();

  /// An offset into an \c OnDiskTrieRawHashMap.
  using TrieOffset = int64_t;

  /// Log a compare-exchange on a subtrie slot within mapped region \p Region.
  ///
  /// \param Region Base address of the mapped file region.
  /// \param Trie File offset of the subtrie being updated.
  /// \param SlotI Index of the slot within the subtrie.
  /// \param Expected Offset value expected in the slot before the exchange.
  /// \param New Offset value written on a successful compare-exchange.
  /// \param Previous Offset value observed in the slot after the attempt.
  LLVM_ABI void logSubtrieHandleCmpXchg(void *Region, TrieOffset Trie,
                                        size_t SlotI, TrieOffset Expected,
                                        TrieOffset New, TrieOffset Previous);
  /// Log creation of a subtrie handle covering bits [\p StartBit, \p StartBit+\p NumBits).
  ///
  /// \param Region Base address of the mapped file region.
  /// \param Trie File offset of the newly created subtrie.
  /// \param StartBit First hash bit covered by this subtrie.
  /// \param NumBits Number of hash bits covered by this subtrie.
  LLVM_ABI void logSubtrieHandleCreate(void *Region, TrieOffset Trie,
                                       uint32_t StartBit, uint32_t NumBits);
  /// Log creation of a hash-mapped trie record at \p TrieOffset for \p Hash.
  ///
  /// \param Region Base address of the mapped file region.
  /// \param TrieOffset File offset of the created record.
  /// \param Hash Hash key stored in the record.
  LLVM_ABI void logHashMappedTrieHandleCreateRecord(void *Region,
                                                    TrieOffset TrieOffset,
                                                    ArrayRef<uint8_t> Hash);
  /// Log resizing of mapped-file arena at \p Path from \p Before to \p After.
  ///
  /// \param Path Path of the mapped-file arena.
  /// \param Before File size before the resize.
  /// \param After File size after the resize.
  LLVM_ABI void logMappedFileRegionArenaResizeFile(StringRef Path,
                                                   size_t Before, size_t After);
  /// Log creation of mapped-file arena at \p Path with capacity \p Capacity.
  ///
  /// \param Path Path of the mapped-file arena.
  /// \param FD File descriptor of the underlying arena file.
  /// \param Region Base address of the mapped file region.
  /// \param Capacity Maximum capacity of the arena.
  /// \param Size Current allocated size of the arena.
  LLVM_ABI void logMappedFileRegionArenaCreate(StringRef Path, int FD,
                                               void *Region, size_t Capacity,
                                               size_t Size);
  /// Log an out-of-memory failure while growing the mapped-file arena at \p Path.
  ///
  /// \param Path Path of the mapped-file arena.
  /// \param Capacity Maximum capacity of the arena.
  /// \param Size Current allocated size of the arena.
  /// \param AllocSize Allocation size that could not be satisfied.
  LLVM_ABI void logMappedFileRegionArenaOom(StringRef Path, size_t Capacity,
                                            size_t Size, size_t AllocSize);
  /// Log closing of the mapped-file arena at \p Path.
  ///
  /// \param Path Path of the mapped-file arena being closed.
  LLVM_ABI void logMappedFileRegionArenaClose(StringRef Path);
  /// Log an allocation of \p Size bytes at \p Off within mapped region \p Region.
  ///
  /// \param Region Base address of the mapped file region.
  /// \param Off Offset of the allocation within the region.
  /// \param Size Number of bytes allocated.
  LLVM_ABI void logMappedFileRegionArenaAllocate(void *Region, TrieOffset Off,
                                                 size_t Size);
  /// Log garbage collection of the unified on-disk cache at \p Path.
  ///
  /// \param Path Path of the unified on-disk cache.
  LLVM_ABI void logUnifiedOnDiskCacheCollectGarbage(StringRef Path);
  /// Log a validate-if-needed pass for the unified on-disk cache at \p Path.
  ///
  /// \param Path Path of the unified on-disk cache.
  /// \param BootTime Recorded boot time used for validation checks.
  /// \param ValidationTime Timestamp of this validation attempt.
  /// \param CheckHash Whether content hashes were verified.
  /// \param AllowRecovery Whether recovery was permitted on failure.
  /// \param Force Whether validation was forced regardless of staleness.
  /// \param LLVMCas Optional LLVM CAS identifier associated with the cache.
  /// \param ValidationError Error message produced by validation, if any.
  /// \param Skipped Whether validation was skipped.
  /// \param Recovered Whether a recovery path was taken.
  LLVM_ABI void logUnifiedOnDiskCacheValidateIfNeeded(
      StringRef Path, uint64_t BootTime, uint64_t ValidationTime,
      bool CheckHash, bool AllowRecovery, bool Force,
      std::optional<StringRef> LLVMCas, StringRef ValidationError, bool Skipped,
      bool Recovered);
  /// Log creation of temporary file \p Name.
  ///
  /// \param Name Path of the temporary file that was created.
  LLVM_ABI void logTempFileCreate(StringRef Name);
  /// Log keeping temporary file \p TmpName as permanent name \p Name.
  ///
  /// \param TmpName Path of the temporary file being kept.
  /// \param Name Permanent path the temporary file is renamed to.
  /// \param EC Error code from the keep/rename operation.
  LLVM_ABI void logTempFileKeep(StringRef TmpName, StringRef Name,
                                std::error_code EC);
  /// Log removal of temporary file \p TmpName.
  ///
  /// \param TmpName Path of the temporary file being removed.
  /// \param EC Error code from the removal operation.
  LLVM_ABI void logTempFileRemove(StringRef TmpName, std::error_code EC);

private:
  OnDiskCASLogger(raw_fd_ostream &OS, bool LogAllocations);

  raw_fd_ostream &OS;
  bool LogAllocations;
};

} // namespace ondisk
} // namespace llvm::cas

#endif // LLVM_CAS_ONDISKLOGGER_H
