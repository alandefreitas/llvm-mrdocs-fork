//===- IPDBDataStream.h - base interface for child enumerator ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_IPDBDATASTREAM_H
#define LLVM_DEBUGINFO_PDB_IPDBDATASTREAM_H

#include "llvm/ADT/SmallVector.h"
#include <cstdint>
#include <optional>
#include <string>

namespace llvm {
namespace pdb {

/// IPDBDataStream defines an interface used to represent a stream consisting
/// of a name and a series of records whose formats depend on the particular
/// stream type.
class LLVM_ABI IPDBDataStream {
public:
  /// Opaque byte buffer holding a single record from this data stream.
  using RecordType = SmallVector<uint8_t, 32>;

  /// Destroys the data stream.
  virtual ~IPDBDataStream();

  /// Returns the number of records in this stream.
  ///
  /// \returns The number of records in this stream.
  virtual uint32_t getRecordCount() const = 0;
  /// Returns the name of this data stream.
  ///
  /// \returns The name of this data stream.
  virtual std::string getName() const = 0;
  /// Returns the record at \p Index, or \c std::nullopt if it cannot be read.
  ///
  /// \param Index Zero-based index of the record to retrieve.
  ///
  /// \returns The record bytes, or \c std::nullopt on failure.
  virtual std::optional<RecordType> getItemAtIndex(uint32_t Index) const = 0;
  /// Advances the enumerator and writes the next record into \p Record.
  ///
  /// \param Record On success, receives the next record's bytes.
  ///
  /// \returns \c true if a record was retrieved, or \c false if there are no
  /// more records.
  virtual bool getNext(RecordType &Record) = 0;
  /// Resets the enumerator to the beginning of the stream.
  virtual void reset() = 0;
};

} // end namespace pdb
} // end namespace llvm

#endif // LLVM_DEBUGINFO_PDB_IPDBDATASTREAM_H
