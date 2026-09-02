//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file declares interface for FileOffset that represent stored data at an
/// offset from the beginning of a file.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CAS_FILEOFFSET_H
#define LLVM_CAS_FILEOFFSET_H

#include <cstdint>

namespace llvm::cas {

/// FileOffset is a wrapper around `uint64_t` to represent the offset of data
/// from the beginning of the file.
class FileOffset {
public:
  /// Return the byte offset from the beginning of the file.
  uint64_t get() const { return Offset; }

  /// Return true if the offset is non-zero.
  explicit operator bool() const { return Offset; }

  /// Construct a zero file offset.
  FileOffset() = default;
  /// Construct a file offset of \p Offset bytes from the start of the file.
  explicit FileOffset(uint64_t Offset) : Offset(Offset) {}

private:
  uint64_t Offset = 0;
};

} // namespace llvm::cas

#endif // LLVM_CAS_FILEOFFSET_H
