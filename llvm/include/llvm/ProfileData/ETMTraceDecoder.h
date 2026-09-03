//===-- ETMTraceDecoder.h - ETM Trace Decoder -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_PROFILEDATA_ETMTRACEDECODER_H
#define LLVM_PROFILEDATA_ETMTRACEDECODER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#include <cstdint>
#include <memory>

namespace llvm {

class Triple;
namespace object {
class Binary;
}

/// Decoder for Arm Embedded Trace Macrocell (ETM) instruction traces.
class ETMDecoder {
public:
  /// Destroy the decoder.
  virtual ~ETMDecoder() = default;

  /// Callback interface for decoded instruction ranges.
  class Callback {
  public:
    /// Destroy the callback.
    virtual ~Callback() = default;
    /// Process a contiguous range of executed instructions.
    /// \param Start Inclusive start address of the instruction range.
    /// \param End Inclusive end address of the instruction range.
    virtual void processInstructionRange(uint64_t Start, uint64_t End) = 0;
  };

  /// Create an ETM decoder for \p Binary targeting \p TargetTriple.
  /// \param Binary Object file used to map executable segments.
  /// \param TargetTriple Target triple describing the traced architecture.
  /// \param TraceID Trace stream identifier expected by the OpenCSD decoder.
  /// \return A decoder on success, or an error if creation fails.
  LLVM_ABI static Expected<std::unique_ptr<ETMDecoder>>
  create(const object::Binary &Binary, const Triple &TargetTriple,
         uint8_t TraceID = 0x10);

  /// Decode \p TraceData and report instruction ranges via \p TraceCallback.
  /// \param TraceData Raw ETM bitstream to decode.
  /// \param TraceCallback Callback invoked for each decoded instruction range.
  /// \return Success, or an error if decoding fails.
  virtual Error processTrace(ArrayRef<uint8_t> TraceData,
                             Callback &TraceCallback) = 0;
};

} // namespace llvm

#endif // LLVM_PROFILEDATA_ETMTRACEDECODER_H
