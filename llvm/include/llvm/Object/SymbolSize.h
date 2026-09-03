//===- SymbolSize.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_SYMBOLSIZE_H
#define LLVM_OBJECT_SYMBOLSIZE_H

#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
namespace object {

/// Symbol address entry used when inferring sizes from adjacent addresses.
struct SymEntry {
  /// Iterator to the symbol, or \c symbol_end for a section-end sentinel.
  symbol_iterator I;
  /// Symbol (or section-end) address; later overwritten with the computed size.
  uint64_t Address;
  /// Original index of the symbol among object-file symbols.
  unsigned Number;
  /// Section identity used to group symbols before sorting by address.
  unsigned SectionID;
};

/// Compare two symbol entries by section ID, then by address.
///
/// \param A Left-hand symbol entry.
/// \param B Right-hand symbol entry.
/// \returns Negative if \p A sorts before \p B, positive if after, zero if
///          equal.
LLVM_ABI int compareAddress(const SymEntry *A, const SymEntry *B);

/// Compute the size of each symbol in \p O.
///
/// For formats that store sizes natively (ELF, XCOFF, Wasm), sizes are read
/// from the object. Otherwise, sizes are inferred as the gap to the next
/// symbol (or section end) at a greater address in the same section.
///
/// \param O Object file whose symbols should be sized.
/// \returns Pairs of each symbol and its size, in original symbol-table order.
LLVM_ABI std::vector<std::pair<SymbolRef, uint64_t>>
computeSymbolSizes(const ObjectFile &O);
}
} // namespace llvm

#endif
