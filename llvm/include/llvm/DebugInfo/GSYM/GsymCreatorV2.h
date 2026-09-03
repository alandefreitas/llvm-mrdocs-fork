//===- GsymCreatorV2.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_GSYM_GSYMCREATORV2_H
#define LLVM_DEBUGINFO_GSYM_GSYMCREATORV2_H

#include "llvm/DebugInfo/GSYM/GsymCreator.h"
#include "llvm/DebugInfo/GSYM/HeaderV2.h"

namespace llvm {
namespace gsym {

/// GsymCreatorV2 emits GSYM V2 data with a GlobalData-based section layout.
class LLVM_ABI GsymCreatorV2 : public GsymCreator {
  uint64_t calculateHeaderAndTableSize() const override;
  std::unique_ptr<GsymCreator> createNew() const override {
    return std::make_unique<GsymCreatorV2>();
  }

public:
  /// Get the size in bytes needed for encoding string offsets.
  ///
  /// \returns The size in bytes needed for encoding string offsets.
  uint8_t getStringOffsetSize() const override {
    return HeaderV2::getStringOffsetSize();
  }

  /// Encode a GSYM V2 file into the file writer stream at the current position.
  ///
  /// \param O The stream to save the binary data to.
  /// \returns An error object that indicates success or failure of the save.
  llvm::Error encode(FileWriter &O) const override;
};

} // namespace gsym
} // namespace llvm

#endif // LLVM_DEBUGINFO_GSYM_GSYMCREATORV2_H
