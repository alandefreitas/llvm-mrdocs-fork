//===- FDRLogBuilder.h - XRay FDR Log Building Utility --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_XRAY_FDRLOGBUILDER_H
#define LLVM_XRAY_FDRLOGBUILDER_H

#include "llvm/XRay/FDRRecords.h"

namespace llvm::xray {

/// Builds ad-hoc collections of FDR records.
///
/// The LogBuilder class allows for creating ad-hoc collections of records
/// through the `add<...>(...)` function. An example use of this API is in
/// crafting arbitrary sequences of records:
///
///   auto Records = LogBuilder()
///       .add<BufferExtents>(256)
///       .add<NewBufferRecord>(1)
///       .consume();
///
class LogBuilder {
  std::vector<std::unique_ptr<Record>> Records;

public:
  /// Appends a newly constructed record of type \p R to the collection.
  /// \param A Arguments forwarded to the constructor of \p R.
  /// \return A reference to this builder for chaining.
  template <class R, class... T> LogBuilder &add(T &&... A) {
    Records.emplace_back(new R(std::forward<T>(A)...));
    return *this;
  }

  /// Moves ownership of the collected records out of this builder.
  /// \return The collected records, leaving this builder empty.
  std::vector<std::unique_ptr<Record>> consume() { return std::move(Records); }
};

} // namespace llvm::xray

#endif // LLVM_XRAY_FDRLOGBUILDER_H
