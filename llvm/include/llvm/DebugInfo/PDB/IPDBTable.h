//===- IPDBTable.h - Base Interface for a PDB Symbol Context ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_IPDBTABLE_H
#define LLVM_DEBUGINFO_PDB_IPDBTABLE_H

#include "PDBTypes.h"

namespace llvm {
namespace pdb {
/// IPDBTable defines an interface used to represent a table of records in a
/// PDB whose formats depend on the particular table type.
class LLVM_ABI IPDBTable {
public:
  /// Destroy the PDB table.
  virtual ~IPDBTable();

  /// Returns the name of this table.
  ///
  /// \returns The name of this table.
  virtual std::string getName() const = 0;
  /// Returns the number of items in this table.
  ///
  /// \returns The number of items in this table.
  virtual uint32_t getItemCount() const = 0;
  /// Returns the type of this table.
  ///
  /// \returns The type of this table.
  virtual PDB_TableType getTableType() const = 0;
};
}
}

#endif // LLVM_DEBUGINFO_PDB_IPDBTABLE_H
