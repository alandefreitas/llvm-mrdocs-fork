//=== FileOutputBuffer.h - File Output Buffer -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Utility for creating a in-memory buffer that will be written to a file.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_FILEOUTPUTBUFFER_H
#define LLVM_SUPPORT_FILEOUTPUTBUFFER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/Error.h"

namespace llvm {
/// In-memory buffer that is written to a file when committed.
///
/// During the lifetime of these objects, the content or existence of the
/// specified file is undefined. That is, creating an OutputBuffer for a file
/// may immediately remove the file. If the FileOutputBuffer is committed, the
/// target file's content will become the buffer content at the time of the
/// commit. If the FileOutputBuffer is not committed, the file will be deleted
/// in the FileOutputBuffer destructor.
class FileOutputBuffer {
public:
  /// Flags controlling FileOutputBuffer::create behavior.
  enum {
    /// Set the 'x' bit on the resulting file.
    F_executable = 1,

    /// Use mmap for in-memory file buffer.
    F_mmap = 2,
  };

  /// Create a FileOutputBuffer for a read/write buffer of the given size.
  ///
  /// When committed, the buffer will be written to the file at the specified
  /// path.
  ///
  /// \param FilePath Path of the file to write when the buffer is committed.
  /// \param Size Size in bytes of the in-memory buffer.
  /// \param Flags Bitmask of create flags such as \c F_executable or
  /// \c F_mmap.
  /// \return An owned buffer on success, or an error if creation fails.
  LLVM_ABI static Expected<std::unique_ptr<FileOutputBuffer>>
  create(StringRef FilePath, size_t Size, unsigned Flags = 0);

  /// Returns a pointer to the start of the buffer.
  ///
  /// \return A pointer to the first byte of the buffer.
  virtual uint8_t *getBufferStart() const = 0;

  /// Returns a pointer to the end of the buffer.
  ///
  /// \return A pointer one past the last byte of the buffer.
  virtual uint8_t *getBufferEnd() const = 0;

  /// Returns size of the buffer.
  ///
  /// \return The size of the buffer in bytes.
  virtual size_t getBufferSize() const = 0;

  /// Returns path where file will show up if buffer is committed.
  ///
  /// \return The final path of the file if the buffer is committed.
  StringRef getPath() const { return FinalPath; }

  /// Flush the buffer to its file and deallocate it.
  ///
  /// If commit() is not called before this object's destructor is called, the
  /// file is deleted in the destructor.
  ///
  /// \return Success, or an error if the buffer could not be written.
  virtual Error commit() = 0;

  /// Destroy this buffer, omitting the file if it was never committed.
  ///
  /// If this object was previously committed, the destructor just deletes this
  /// object. If this object was not committed, the destructor deallocates the
  /// buffer and the target file is never written.
  virtual ~FileOutputBuffer() = default;

  /// This removes the temporary file (unless it already was committed)
  /// but keeps the memory mapping alive.
  virtual void discard() {}

protected:
  /// Construct a FileOutputBuffer that will write to \p Path when committed.
  ///
  /// \param Path Final path where the file will appear if the buffer is
  /// committed.
  FileOutputBuffer(StringRef Path) : FinalPath(Path) {}

  /// Path where the file will appear if the buffer is committed.
  std::string FinalPath;
};
} // end namespace llvm

#endif
