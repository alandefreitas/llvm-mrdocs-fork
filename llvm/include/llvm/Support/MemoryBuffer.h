//===--- MemoryBuffer.h - Memory Buffer Interface ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the MemoryBuffer interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_MEMORYBUFFER_H
#define LLVM_SUPPORT_MEMORYBUFFER_H

#include "llvm-c/Types.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/CBindingWrapping.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/MemoryBufferRef.h"
#include <cstddef>
#include <cstdint>
#include <memory>

namespace llvm {
namespace sys {
namespace fs {
// Duplicated from FileSystem.h to avoid a dependency.
#if defined(_WIN32)
// A Win32 HANDLE is a typedef of void*
using file_t = void *;
#else
using file_t = int;
#endif
} // namespace fs
} // namespace sys

/// This interface provides simple read-only access to a block of memory, and
/// provides simple methods for reading files and standard input into a memory
/// buffer.  In addition to basic access to the characters in the file, this
/// interface guarantees you can read one character past the end of the file,
/// and that this character will read as '\0'.
///
/// The '\0' guarantee is needed to support an optimization -- it's intended to
/// be more efficient for clients which are reading all the data to stop
/// reading when they encounter a '\0' than to continually check the file
/// position to see if it has reached the end of the file.
class LLVM_ABI MemoryBuffer {
  const char *BufferStart; // Start of the buffer.
  const char *BufferEnd;   // End of the buffer.

protected:
  /// Default-construct an uninitialized memory buffer.
  MemoryBuffer() = default;

  /// Initialize buffer pointers and whether a null terminator is required.
  ///
  /// \param BufStart Pointer to the first byte of the buffer.
  /// \param BufEnd Pointer one past the last byte of the buffer.
  /// \param RequiresNullTerminator Whether the buffer must be null-terminated.
  void init(const char *BufStart, const char *BufEnd,
            bool RequiresNullTerminator);

public:
  /// Deleted copy constructor.
  ///
  /// \param Other Unused; copy construction is deleted.
  MemoryBuffer(const MemoryBuffer &Other) = delete;
  /// Deleted copy assignment.
  ///
  /// \param Other Unused; copy assignment is deleted.
  MemoryBuffer &operator=(const MemoryBuffer &Other) = delete;
  /// Virtual destructor for polymorphic MemoryBuffer subclasses.
  virtual ~MemoryBuffer();

  /// Pointer to the start of the buffer.
  ///
  /// \return A pointer to the first byte of the buffer.
  const char *getBufferStart() const { return BufferStart; }
  /// Pointer one past the last byte of the buffer.
  ///
  /// \return A pointer one past the last byte of the buffer.
  const char *getBufferEnd() const   { return BufferEnd; }
  /// Number of bytes in the buffer (end minus start).
  ///
  /// \return The size of the buffer in bytes.
  size_t getBufferSize() const { return BufferEnd-BufferStart; }

  /// Buffer contents as a StringRef spanning [start, end).
  ///
  /// \return A StringRef view of the buffer contents.
  StringRef getBuffer() const {
    return StringRef(BufferStart, getBufferSize());
  }

  /// Return an identifier for this buffer, typically the filename it was read
  /// from.
  ///
  /// \return The buffer identifier string.
  virtual StringRef getBufferIdentifier() const { return "Unknown buffer"; }

  /// Advise the kernel that a read-only mmap buffer is unused soon.
  ///
  /// For read-only MemoryBuffer_MMap, mark the buffer as unused in the near
  /// future and the kernel can free resources associated with it. Further
  /// access is supported but may be expensive. This calls
  /// madvise(MADV_DONTNEED) on read-only file mappings on *NIX systems. This
  /// function should not be called on a writable buffer.
  virtual void dontNeedIfMmap() {}

  /// Advise the kernel to prefetch this mmap buffer into memory.
  ///
  /// Mark the buffer as to-be-used in a near future. This shall trigger OS
  /// prefetching from the storage device and into memory, if possible.
  /// This should be use purely as an read optimization.
  virtual void willNeedIfMmap() {}

  /// Advise the kernel that accesses to this mmap buffer will be random.
  ///
  /// For read-only MemoryBuffer_MMap, advise the kernel that accesses will be
  /// random, disabling readahead. This calls madvise(MADV_RANDOM) on *NIX.
  /// This function should not be called on a writable buffer.
  virtual void randomAccessIfMmap() {}

  /// Open the specified file as a MemoryBuffer, returning a new MemoryBuffer
  /// if successful, otherwise returning null.
  ///
  /// \param Filename Path of the file to open.
  /// \param IsText Set to true to indicate that the file should be read in
  /// text mode.
  /// \param RequiresNullTerminator Whether the buffer must be null-terminated.
  /// \param IsVolatile Set to true to indicate that the contents of the file
  /// can change outside the user's control, e.g. when libclang tries to parse
  /// while the user is editing/updating the file or if the file is on an NFS.
  /// \param Alignment Set to indicate that the buffer should be aligned to at
  /// least the specified alignment.
  /// \return The file contents on success, or an error on failure.
  static ErrorOr<std::unique_ptr<MemoryBuffer>>
  getFile(const Twine &Filename, bool IsText = false,
          bool RequiresNullTerminator = true, bool IsVolatile = false,
          std::optional<Align> Alignment = std::nullopt);

  /// Read the specified file into a MemoryBuffer until EOF.
  ///
  /// This is useful for special files that look like a regular file but have 0
  /// size (e.g. /proc/cpuinfo on Linux).
  ///
  /// \param Filename Path of the file to read as a stream.
  /// \return The file contents on success, or an error on failure.
  static ErrorOr<std::unique_ptr<MemoryBuffer>>
  getFileAsStream(const Twine &Filename);

  /// Map a slice of an already-open file into a MemoryBuffer.
  ///
  /// The slice is specified by an \p Offset and \p MapSize. Since this is in
  /// the middle of a file, the buffer is not null terminated.
  ///
  /// \param FD Already-open file descriptor to map from.
  /// \param Filename Identifier for the buffer (typically the path).
  /// \param MapSize Number of bytes to map from the file.
  /// \param Offset Byte offset into the file at which to begin the mapping.
  /// \param IsVolatile Set to true to indicate that the contents of the file
  /// can change outside the user's control.
  /// \param Alignment Set to indicate that the buffer should be aligned to at
  /// least the specified alignment.
  /// \return The mapped slice on success, or an error on failure.
  static ErrorOr<std::unique_ptr<MemoryBuffer>>
  getOpenFileSlice(sys::fs::file_t FD, const Twine &Filename, uint64_t MapSize,
                   int64_t Offset, bool IsVolatile = false,
                   std::optional<Align> Alignment = std::nullopt);

  /// Given an already-open file descriptor, read the file and return a
  /// MemoryBuffer.
  ///
  /// \param FD Already-open file descriptor to read from.
  /// \param Filename Identifier for the buffer (typically the path).
  /// \param FileSize Size of the file in bytes.
  /// \param RequiresNullTerminator Whether the buffer must be null-terminated.
  /// \param IsVolatile Set to true to indicate that the contents of the file
  /// can change outside the user's control, e.g. when libclang tries to parse
  /// while the user is editing/updating the file or if the file is on an NFS.
  /// \param Alignment Set to indicate that the buffer should be aligned to at
  /// least the specified alignment.
  /// \return The file contents on success, or an error on failure.
  static ErrorOr<std::unique_ptr<MemoryBuffer>>
  getOpenFile(sys::fs::file_t FD, const Twine &Filename, uint64_t FileSize,
              bool RequiresNullTerminator = true, bool IsVolatile = false,
              std::optional<Align> Alignment = std::nullopt);

  /// Open the specified memory range as a MemoryBuffer.
  ///
  /// Note that InputData must be null terminated if RequiresNullTerminator is
  /// true.
  ///
  /// \param InputData Memory range to wrap (not copied).
  /// \param BufferName Identifier for the buffer (e.g. for diagnostics).
  /// \param RequiresNullTerminator Whether the buffer must be null-terminated.
  /// \return An owned MemoryBuffer that refers to InputData.
  static std::unique_ptr<MemoryBuffer>
  getMemBuffer(StringRef InputData, StringRef BufferName = "",
               bool RequiresNullTerminator = true);

  /// Create a MemoryBuffer referring to the contents of \p Ref (no copy).
  ///
  /// \param Ref Memory buffer reference whose contents are referred to.
  /// \param RequiresNullTerminator Whether the buffer must be null-terminated.
  /// \return An owned MemoryBuffer that refers to Ref's data.
  static std::unique_ptr<MemoryBuffer>
  getMemBuffer(MemoryBufferRef Ref, bool RequiresNullTerminator = true);

  /// Open the specified memory range as a MemoryBuffer, copying the contents
  /// and taking ownership of it.
  ///
  /// InputData does not have to be null terminated.
  ///
  /// \param InputData Memory range to copy into the new buffer.
  /// \param BufferName Identifier for the buffer (e.g. for diagnostics).
  /// \return An owned MemoryBuffer containing a copy of InputData.
  static std::unique_ptr<MemoryBuffer>
  getMemBufferCopy(StringRef InputData, const Twine &BufferName = "");

  /// Read all of stdin into a file buffer, and return it.
  ///
  /// \return The stdin contents on success, or an error on failure.
  static ErrorOr<std::unique_ptr<MemoryBuffer>> getSTDIN();

  /// Open the specified file as a MemoryBuffer, or open stdin if the Filename
  /// is "-".
  ///
  /// \param Filename Path of the file to open, or "-" for stdin.
  /// \param IsText Set to true to indicate that the file should be read in
  /// text mode.
  /// \param RequiresNullTerminator Whether the buffer must be null-terminated.
  /// \param Alignment Set to indicate that the buffer should be aligned to at
  /// least the specified alignment.
  /// \return The file or stdin buffer on success, or an error on failure.
  static ErrorOr<std::unique_ptr<MemoryBuffer>>
  getFileOrSTDIN(const Twine &Filename, bool IsText = false,
                 bool RequiresNullTerminator = true,
                 std::optional<Align> Alignment = std::nullopt);

  /// Map a subrange of the specified file as a MemoryBuffer.
  ///
  /// \param Filename Path of the file to map.
  /// \param MapSize Number of bytes to map from the file.
  /// \param Offset Byte offset into the file at which to begin the mapping.
  /// \param IsVolatile Set to true to indicate that the contents of the file
  /// can change outside the user's control.
  /// \param Alignment Set to indicate that the buffer should be aligned to at
  /// least the specified alignment.
  /// \return The mapped buffer on success, or an error on failure.
  static ErrorOr<std::unique_ptr<MemoryBuffer>>
  getFileSlice(const Twine &Filename, uint64_t MapSize, uint64_t Offset,
               bool IsVolatile = false,
               std::optional<Align> Alignment = std::nullopt);

  //===--------------------------------------------------------------------===//
  // Provided for performance analysis.
  //===--------------------------------------------------------------------===//

  /// The kind of memory backing used to support the MemoryBuffer.
  enum BufferKind {
    MemoryBuffer_Malloc, ///< Heap-allocated (malloc) buffer.
    MemoryBuffer_MMap ///< Memory-mapped file buffer.
  };

  /// Return information on the memory mechanism used to support the
  /// MemoryBuffer.
  ///
  /// \return Whether the buffer is malloc-backed or memory-mapped.
  virtual BufferKind getBufferKind() const = 0;

  /// Return a lightweight reference to this buffer's data and identifier.
  ///
  /// \return A MemoryBufferRef to this buffer's contents and identifier.
  MemoryBufferRef getMemBufferRef() const;
};

/// MemoryBuffer with copy-on-write access to a writable backing store.
class WritableMemoryBuffer : public MemoryBuffer {
protected:
  /// Default-construct an uninitialized writable memory buffer.
  WritableMemoryBuffer() = default;

public:
  /// Inherit the const getBuffer() overload from MemoryBuffer.
  using MemoryBuffer::getBuffer;
  /// Inherit getBufferEnd() from MemoryBuffer.
  using MemoryBuffer::getBufferEnd;
  /// Inherit getBufferStart() from MemoryBuffer.
  using MemoryBuffer::getBufferStart;

  // const_cast is well-defined here, because the underlying buffer is
  // guaranteed to have been initialized with a mutable buffer.
  /// Return a mutable pointer to the start of the buffer.
  ///
  /// \return A mutable pointer to the first byte of the buffer.
  char *getBufferStart() {
    return const_cast<char *>(MemoryBuffer::getBufferStart());
  }
  /// Return a mutable pointer past the end of the buffer.
  ///
  /// \return A mutable pointer one past the last byte of the buffer.
  char *getBufferEnd() {
    return const_cast<char *>(MemoryBuffer::getBufferEnd());
  }
  /// Return the buffer as a mutable array reference.
  ///
  /// \return A mutable view of the buffer contents.
  MutableArrayRef<char> getBuffer() {
    return {getBufferStart(), getBufferEnd()};
  }

  /// Open \p Filename as a writable memory buffer.
  ///
  /// \param Filename Path of the file to open.
  /// \param IsVolatile Set when the file may change outside the caller's
  /// control (e.g. NFS or concurrent edits).
  /// \param Alignment Requested minimum alignment of the mapped buffer.
  /// \return The writable buffer on success, or an error on failure.
  LLVM_ABI static ErrorOr<std::unique_ptr<WritableMemoryBuffer>>
  getFile(const Twine &Filename, bool IsVolatile = false,
          std::optional<Align> Alignment = std::nullopt);

  /// Map a subrange of the specified file as a WritableMemoryBuffer.
  ///
  /// \param Filename Path of the file to map.
  /// \param MapSize Number of bytes to map from the file.
  /// \param Offset Byte offset into the file at which to begin the mapping.
  /// \param IsVolatile Set when the file may change outside the caller's
  /// control (e.g. NFS or concurrent edits).
  /// \param Alignment Requested minimum alignment of the mapped buffer.
  /// \return The writable buffer on success, or an error on failure.
  LLVM_ABI static ErrorOr<std::unique_ptr<WritableMemoryBuffer>>
  getFileSlice(const Twine &Filename, uint64_t MapSize, uint64_t Offset,
               bool IsVolatile = false,
               std::optional<Align> Alignment = std::nullopt);

  /// Allocate a new uninitialized MemoryBuffer of the specified size.
  ///
  /// Note that the caller should initialize the memory allocated by this
  /// method. The memory is owned by the MemoryBuffer object.
  ///
  /// \param Size Number of bytes to allocate.
  /// \param BufferName Identifier for the buffer (e.g. for diagnostics).
  /// \param Alignment Set to indicate that the buffer should be aligned to at
  /// least the specified alignment.
  /// \return An owned uninitialized writable buffer of the given size.
  LLVM_ABI static std::unique_ptr<WritableMemoryBuffer>
  getNewUninitMemBuffer(size_t Size, const Twine &BufferName = "",
                        std::optional<Align> Alignment = std::nullopt);

  /// Allocate a new zero-initialized MemoryBuffer of the specified size.
  ///
  /// Note that the caller need not initialize the memory allocated by this
  /// method. The memory is owned by the MemoryBuffer object.
  ///
  /// \param Size Number of bytes to allocate.
  /// \param BufferName Identifier for the buffer (e.g. for diagnostics).
  /// \return An owned zero-initialized writable buffer of the given size.
  LLVM_ABI static std::unique_ptr<WritableMemoryBuffer>
  getNewMemBuffer(size_t Size, const Twine &BufferName = "");

private:
  // Hide these base class factory function so one can't write
  //   WritableMemoryBuffer::getXXX()
  // and be surprised that he got a read-only Buffer.
  using MemoryBuffer::getFileAsStream;
  using MemoryBuffer::getFileOrSTDIN;
  using MemoryBuffer::getMemBuffer;
  using MemoryBuffer::getMemBufferCopy;
  using MemoryBuffer::getOpenFile;
  using MemoryBuffer::getOpenFileSlice;
  using MemoryBuffer::getSTDIN;
};

/// MemoryBuffer that maps a file writable and can commit changes back.
class WriteThroughMemoryBuffer : public MemoryBuffer {
protected:
  /// Construct an empty write-through memory buffer.
  WriteThroughMemoryBuffer() = default;

public:
  /// Inherit the const getBuffer() overload from MemoryBuffer.
  using MemoryBuffer::getBuffer;
  /// Inherit getBufferEnd() from MemoryBuffer.
  using MemoryBuffer::getBufferEnd;
  /// Inherit getBufferStart() from MemoryBuffer.
  using MemoryBuffer::getBufferStart;

  // const_cast is well-defined here, because the underlying buffer is
  // guaranteed to have been initialized with a mutable buffer.
  /// Return a mutable pointer to the start of the buffer.
  ///
  /// \return A mutable pointer to the first byte of the buffer.
  char *getBufferStart() {
    return const_cast<char *>(MemoryBuffer::getBufferStart());
  }
  /// Return a mutable pointer past the end of the buffer.
  ///
  /// \return A mutable pointer one past the last byte of the buffer.
  char *getBufferEnd() {
    return const_cast<char *>(MemoryBuffer::getBufferEnd());
  }
  /// Return the buffer as a mutable array reference.
  ///
  /// \return A mutable view of the buffer contents.
  MutableArrayRef<char> getBuffer() {
    return {getBufferStart(), getBufferEnd()};
  }

  /// Map the specified file as a write-through memory buffer.
  ///
  /// \param Filename Path of the file to map.
  /// \param FileSize Size of the file in bytes, or -1 to determine it from the
  /// file itself.
  /// \return The write-through buffer on success, or an error on failure.
  LLVM_ABI static ErrorOr<std::unique_ptr<WriteThroughMemoryBuffer>>
  getFile(const Twine &Filename, int64_t FileSize = -1);

  /// Map a subrange of the specified file as a ReadWriteMemoryBuffer.
  ///
  /// \param Filename Path of the file to map.
  /// \param MapSize Number of bytes to map from the file.
  /// \param Offset Byte offset into the file at which to begin the mapping.
  /// \return The write-through buffer on success, or an error on failure.
  LLVM_ABI static ErrorOr<std::unique_ptr<WriteThroughMemoryBuffer>>
  getFileSlice(const Twine &Filename, uint64_t MapSize, uint64_t Offset);

private:
  // Hide these base class factory function so one can't write
  //   WritableMemoryBuffer::getXXX()
  // and be surprised that he got a read-only Buffer.
  using MemoryBuffer::getFileAsStream;
  using MemoryBuffer::getFileOrSTDIN;
  using MemoryBuffer::getMemBuffer;
  using MemoryBuffer::getMemBufferCopy;
  using MemoryBuffer::getOpenFile;
  using MemoryBuffer::getOpenFileSlice;
  using MemoryBuffer::getSTDIN;
};

// Create wrappers for C Binding types (see CBindingWrapping.h).
/// Convert an LLVMMemoryBufferRef to a MemoryBuffer pointer.
///
/// @param P Opaque C binding reference to unwrap.
/// @return The corresponding MemoryBuffer pointer.
inline MemoryBuffer *unwrap(LLVMMemoryBufferRef P) {
  return reinterpret_cast<MemoryBuffer *>(P);
}

/// Convert a MemoryBuffer pointer to an LLVMMemoryBufferRef.
///
/// @param P MemoryBuffer pointer to wrap.
/// @return The corresponding opaque C binding reference.
inline LLVMMemoryBufferRef wrap(const MemoryBuffer *P) {
  return reinterpret_cast<LLVMMemoryBufferRef>(
      const_cast<MemoryBuffer *>(P));
}

} // end namespace llvm

#endif // LLVM_SUPPORT_MEMORYBUFFER_H
