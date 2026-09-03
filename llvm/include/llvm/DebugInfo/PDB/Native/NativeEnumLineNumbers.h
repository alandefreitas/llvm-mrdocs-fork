//==- NativeEnumLineNumbers.h - Native Line Number Enumerator ------------*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVEENUMLINENUMBERS_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVEENUMLINENUMBERS_H

#include "llvm/DebugInfo/PDB/IPDBEnumChildren.h"
#include "llvm/DebugInfo/PDB/IPDBLineNumber.h"
#include "llvm/DebugInfo/PDB/Native/NativeLineNumber.h"
#include <vector>

namespace llvm {
namespace pdb {

/// Native enumerator over a fixed list of PDB line-number records.
class LLVM_ABI NativeEnumLineNumbers : public IPDBEnumChildren<IPDBLineNumber> {
public:
  /// Construct an enumerator that yields the given line-number records.
  ///
  /// \param LineNums The line numbers to expose through this enumerator.
  explicit NativeEnumLineNumbers(std::vector<NativeLineNumber> LineNums);

  /// Deleted copy constructor.
  ///
  /// \param Other Unused; copy construction is not supported.
  NativeEnumLineNumbers(const NativeEnumLineNumbers &Other) = delete;
  /// Deleted copy assignment operator.
  ///
  /// \param Other Unused; copy assignment is not supported.
  ///
  /// \returns Never returns; copy assignment is deleted.
  NativeEnumLineNumbers &operator=(const NativeEnumLineNumbers &Other) = delete;

  /// Return the number of line numbers available from this enumerator.
  ///
  /// \returns The total number of line-number records.
  uint32_t getChildCount() const override;
  /// Return the line number at the given zero-based \p Index.
  ///
  /// \param Index Zero-based index of the line number to retrieve.
  ///
  /// \returns An owning pointer to the line number, or null if \p Index is out
  ///     of range.
  ChildTypePtr getChildAtIndex(uint32_t Index) const override;
  /// Advance the enumerator and return the next line number.
  ///
  /// \returns An owning pointer to the next line number, or null when
  ///     exhausted.
  ChildTypePtr getNext() override;
  /// Reset the enumerator to its initial position.
  void reset() override;

private:
  std::vector<NativeLineNumber> Lines;
  uint32_t Index;
};
} // namespace pdb
} // namespace llvm

#endif
