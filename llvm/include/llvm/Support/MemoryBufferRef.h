//===- MemoryBufferRef.h - Memory Buffer Reference --------------*- C++ -*-===//
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

#ifndef LLVM_SUPPORT_MEMORYBUFFERREF_H
#define LLVM_SUPPORT_MEMORYBUFFERREF_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class MemoryBuffer;

/// A non-owning reference to a memory buffer and its identifier.
class MemoryBufferRef {
  StringRef Buffer;
  StringRef Identifier;

public:
  /// Default-construct an empty memory buffer reference.
  MemoryBufferRef() = default;
  /// Construct a reference from an existing MemoryBuffer.
  ///
  /// \param Buffer Buffer whose contents and identifier are referenced.
  LLVM_ABI MemoryBufferRef(const MemoryBuffer &Buffer);
  /// Construct a reference to \p Buffer identified by \p Identifier.
  ///
  /// \param Buffer Contents of the referenced buffer.
  /// \param Identifier Name associated with the buffer (often a filename).
  MemoryBufferRef(StringRef Buffer, StringRef Identifier)
      : Buffer(Buffer), Identifier(Identifier) {}

  /// Return the referenced buffer contents.
  ///
  /// \return The referenced buffer contents as a StringRef.
  StringRef getBuffer() const { return Buffer; }
  /// Return the buffer's identifier (often a filename).
  ///
  /// \return The buffer identifier as a StringRef.
  StringRef getBufferIdentifier() const { return Identifier; }

  /// Pointer to the start of the referenced buffer.
  ///
  /// \return Pointer to the first byte of the referenced buffer.
  const char *getBufferStart() const { return Buffer.begin(); }
  /// Pointer one past the last byte of the referenced buffer.
  ///
  /// \return Pointer one past the last byte of the referenced buffer.
  const char *getBufferEnd() const { return Buffer.end(); }
  /// Number of bytes in the referenced buffer.
  ///
  /// \return The number of bytes in the referenced buffer.
  size_t getBufferSize() const { return Buffer.size(); }

  /// Check pointer identity (not value) of identifier and data.
  ///
  /// \param LHS Left-hand memory buffer reference.
  /// \param RHS Right-hand memory buffer reference.
  /// \return True if \p LHS and \p RHS refer to the same buffer pointers.
  friend bool operator==(const MemoryBufferRef &LHS,
                         const MemoryBufferRef &RHS) {
    return LHS.Buffer.begin() == RHS.Buffer.begin() &&
           LHS.Buffer.end() == RHS.Buffer.end() &&
           LHS.Identifier.begin() == RHS.Identifier.begin() &&
           LHS.Identifier.end() == RHS.Identifier.end();
  }

  /// True if \p LHS and \p RHS do not refer to the same buffer pointers.
  ///
  /// \param LHS Left-hand memory buffer reference.
  /// \param RHS Right-hand memory buffer reference.
  /// \return True if \p LHS and \p RHS do not refer to the same buffer pointers.
  friend bool operator!=(const MemoryBufferRef &LHS,
                         const MemoryBufferRef &RHS) {
    return !(LHS == RHS);
  }
};

} // namespace llvm

#endif // LLVM_SUPPORT_MEMORYBUFFERREF_H
