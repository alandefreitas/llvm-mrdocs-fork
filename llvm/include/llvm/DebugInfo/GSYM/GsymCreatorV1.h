//===- GsymCreatorV1.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_GSYM_GSYMCREATORV1_H
#define LLVM_DEBUGINFO_GSYM_GSYMCREATORV1_H

#include "llvm/DebugInfo/GSYM/GsymCreator.h"
#include "llvm/DebugInfo/GSYM/Header.h"

namespace llvm {
namespace gsym {

/// GsymCreatorV1 emits GSYM V1 data with the classic header and table layout.
class LLVM_ABI GsymCreatorV1 : public GsymCreator {
  uint64_t calculateHeaderAndTableSize() const override;
  std::unique_ptr<GsymCreator> createNew() const override {
    return std::make_unique<GsymCreatorV1>();
  }

public:
  /// Get the size in bytes needed for encoding string offsets.
  ///
  /// \returns The number of bytes used to encode a string table offset.
  uint8_t getStringOffsetSize() const override {
    return Header::getStringOffsetSize();
  }
  /// Encode a GSYM into the file writer stream at the current position.
  ///
  /// \param O The stream to save the binary data to.
  /// \returns An error object that indicates success or failure of the save.
  llvm::Error encode(FileWriter &O) const override;
};

} // namespace gsym
} // namespace llvm

#endif // LLVM_DEBUGINFO_GSYM_GSYMCREATORV1_H
