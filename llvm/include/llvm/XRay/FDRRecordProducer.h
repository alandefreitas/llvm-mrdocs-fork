//===- FDRRecordProducer.h - XRay FDR Mode Record Producer ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_XRAY_FDRRECORDPRODUCER_H
#define LLVM_XRAY_FDRRECORDPRODUCER_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/XRay/FDRRecords.h"
#include "llvm/XRay/XRayRecord.h"
#include <memory>

namespace llvm::xray {

/// Abstract source of successive FDR records.
class RecordProducer {
public:
  /// All producer implementations must yield either an Error or a non-nullptr
  /// unique_ptr<Record>.
  /// \return The next record, or an Error on failure.
  virtual Expected<std::unique_ptr<Record>> produce() = 0;
  /// Destroy a record producer.
  virtual ~RecordProducer() = default;
};

/// RecordProducer that loads FDR records from a file-backed DataExtractor.
class LLVM_ABI FileBasedRecordProducer : public RecordProducer {
  const XRayFileHeader &Header;
  DataExtractor &E;
  uint64_t &OffsetPtr;
  uint32_t CurrentBufferBytes = 0;

  // Helper function which gets the next record by speculatively reading through
  // the log, finding a buffer extents record.
  Expected<std::unique_ptr<Record>> findNextBufferExtent();

public:
  /// Construct a producer over header \p FH, extractor \p DE, and offset \p OP.
  /// \param FH Parsed XRay file header describing the log.
  /// \param DE Data extractor positioned over the FDR log bytes.
  /// \param OP Byte offset into \p DE that advances as records are produced.
  FileBasedRecordProducer(const XRayFileHeader &FH, DataExtractor &DE,
                          uint64_t &OP)
      : Header(FH), E(DE), OffsetPtr(OP) {}

  /// This producer encapsulates the logic for loading a File-backed
  /// RecordProducer hidden behind a DataExtractor.
  /// \return The next FDR record from the extractor, or an Error on failure.
  Expected<std::unique_ptr<Record>> produce() override;
};

} // namespace llvm::xray

#endif // LLVM_XRAY_FDRRECORDPRODUCER_H
