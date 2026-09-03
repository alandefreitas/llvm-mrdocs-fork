//===- SmallVectorMemoryBuffer.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares a wrapper class to hold the memory into which an
// object will be generated.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_SMALLVECTORMEMORYBUFFER_H
#define LLVM_SUPPORT_SMALLVECTORMEMORYBUFFER_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

/// SmallVector-backed MemoryBuffer instance.
///
/// This class enables efficient construction of MemoryBuffers from SmallVector
/// instances. This is useful for MCJIT and Orc, where object files are streamed
/// into SmallVectors, then inspected using ObjectFile (which takes a
/// MemoryBuffer).
class LLVM_ABI SmallVectorMemoryBuffer : public MemoryBuffer {
public:
  /// Construct a SmallVectorMemoryBuffer from the given SmallVector r-value.
  ///
  /// \param SV Buffer contents taken by move.
  /// \param RequiresNullTerminator Whether the buffer must be null-terminated.
  SmallVectorMemoryBuffer(SmallVectorImpl<char> &&SV,
                          bool RequiresNullTerminator = true)
      : SmallVectorMemoryBuffer(std::move(SV), "<in-memory object>",
                                RequiresNullTerminator) {}

  /// Construct a named SmallVectorMemoryBuffer from the given SmallVector
  /// r-value and StringRef.
  ///
  /// \param SV Buffer contents taken by move.
  /// \param Name Identifier for this buffer (typically a descriptive label).
  /// \param RequiresNullTerminator Whether the buffer must be null-terminated.
  SmallVectorMemoryBuffer(SmallVectorImpl<char> &&SV, StringRef Name,
                          bool RequiresNullTerminator = true)
      : SV(std::move(SV)), BufferName(std::string(Name)) {
    if (RequiresNullTerminator) {
      this->SV.push_back('\0');
      this->SV.pop_back();
    }
    init(this->SV.begin(), this->SV.end(), false);
  }

  /// Destroy this SmallVectorMemoryBuffer.
  ///
  /// Key function for the vtable.
  ~SmallVectorMemoryBuffer() override;

  /// Return the identifier for this buffer (the name given at construction).
  ///
  /// \return The buffer name as a StringRef.
  StringRef getBufferIdentifier() const override { return BufferName; }

  /// Return that this buffer is backed by heap-allocated (malloc) memory.
  ///
  /// \return MemoryBuffer_Malloc.
  BufferKind getBufferKind() const override { return MemoryBuffer_Malloc; }

private:
  SmallVector<char, 0> SV;
  std::string BufferName;
};

} // namespace llvm

#endif
