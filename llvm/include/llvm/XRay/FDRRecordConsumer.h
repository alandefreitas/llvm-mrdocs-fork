//===- FDRRecordConsumer.h - XRay Flight Data Recorder Mode Records -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_XRAY_FDRRECORDCONSUMER_H
#define LLVM_XRAY_FDRRECORDCONSUMER_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/XRay/FDRRecords.h"
#include <memory>
#include <vector>

namespace llvm::xray {

/// Abstract consumer of FDR mode records.
class RecordConsumer {
public:
  /// Process a single FDR record.
  /// \param R Record to consume; ownership is transferred to the consumer.
  /// \return Success, or an error if consumption failed.
  virtual Error consume(std::unique_ptr<Record> R) = 0;
  /// Virtual destructor.
  virtual ~RecordConsumer() = default;
};

/// RecordConsumer that appends records into a vector in arrival order.
///
/// This consumer will collect all the records into a vector of records, in
/// arrival order.
class LLVM_ABI LogBuilderConsumer : public RecordConsumer {
  std::vector<std::unique_ptr<Record>> &Records;

public:
  /// Construct a consumer that stores records into \p R.
  /// \param R Vector that receives consumed records in arrival order.
  explicit LogBuilderConsumer(std::vector<std::unique_ptr<Record>> &R)
      : Records(R) {}

  /// Append a record to the collected log.
  /// \param R Record to append; ownership is transferred into the vector.
  /// \return Success, or an error if the record could not be appended.
  Error consume(std::unique_ptr<Record> R) override;
};

/// RecordConsumer that applies a pipeline of visitors to each record.
///
/// A PipelineConsumer applies a set of visitors to every consumed Record, in
/// the order by which the visitors are added to the pipeline in the order of
/// appearance.
class LLVM_ABI PipelineConsumer : public RecordConsumer {
  std::vector<RecordVisitor *> Visitors;

public:
  /// Construct a consumer that runs the given visitors on each record.
  /// \param V Visitors applied in initializer-list order to each consumed
  /// record.
  PipelineConsumer(std::initializer_list<RecordVisitor *> V) : Visitors(V) {}

  /// Apply each visitor in the pipeline to the given record.
  /// \param R Record to visit; ownership is transferred for the duration of
  /// the visit sequence.
  /// \return Success, or an error if any visitor failed.
  Error consume(std::unique_ptr<Record> R) override;
};

} // namespace llvm::xray

#endif // LLVM_XRAY_FDRRECORDCONSUMER_H
