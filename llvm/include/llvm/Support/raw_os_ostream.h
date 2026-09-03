//===- raw_os_ostream.h - std::ostream adaptor for raw_ostream --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the raw_os_ostream class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_RAW_OS_OSTREAM_H
#define LLVM_SUPPORT_RAW_OS_OSTREAM_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"
#include <iosfwd>

namespace llvm {

/// A raw_ostream that writes to an std::ostream.
///
/// This is a simple adaptor class. It does not check for output errors;
/// clients should use the underlying stream to detect errors.
class LLVM_ABI raw_os_ostream : public raw_ostream {
  std::ostream &OS;

  /// write_impl - See raw_ostream::write_impl.
  void write_impl(const char *Ptr, size_t Size) override;

  /// current_pos - Return the current position within the stream, not
  /// counting the bytes currently in the buffer.
  uint64_t current_pos() const override;

public:
  /// Construct a stream that writes into std::ostream \p O.
  ///
  /// \param O Stream that receives all written output.
  raw_os_ostream(std::ostream &O) : OS(O) {}
  /// Destroy the stream, flushing any buffered output.
  ~raw_os_ostream() override;
};

} // end llvm namespace

#endif
