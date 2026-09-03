//===- ConcreteSymbolEnumerator.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_CONCRETESYMBOLENUMERATOR_H
#define LLVM_DEBUGINFO_PDB_CONCRETESYMBOLENUMERATOR_H

#include "llvm/DebugInfo/PDB/IPDBEnumChildren.h"
#include "llvm/DebugInfo/PDB/PDBSymbol.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"
#include "llvm/Support/Casting.h"
#include <algorithm>
#include <cstdint>
#include <memory>

namespace llvm {
namespace pdb {

/// Enumerator that yields PDB symbols cast to a concrete child type.
///
/// Wraps an \c IPDBEnumSymbols and dynamically casts each child to
/// \c ChildType.
template <typename ChildType>
class ConcreteSymbolEnumerator : public IPDBEnumChildren<ChildType> {
public:
  /// Construct a concrete enumerator that owns \p SymbolEnumerator.
  ///
  /// \param SymbolEnumerator Underlying enumerator of untyped PDB symbols.
  ConcreteSymbolEnumerator(std::unique_ptr<IPDBEnumSymbols> SymbolEnumerator)
      : Enumerator(std::move(SymbolEnumerator)) {}

  /// Destroy the concrete symbol enumerator.
  ~ConcreteSymbolEnumerator() override = default;

  /// Return the number of children in the underlying enumerator.
  ///
  /// \returns The child count reported by the wrapped enumerator.
  uint32_t getChildCount() const override {
    return Enumerator->getChildCount();
  }

  /// Return the child at \p Index cast to \c ChildType.
  ///
  /// \param Index Zero-based index of the child to retrieve.
  ///
  /// \returns A \c ChildType instance, or null if the child is missing or has
  ///     a different dynamic type.
  std::unique_ptr<ChildType> getChildAtIndex(uint32_t Index) const override {
    std::unique_ptr<PDBSymbol> Child = Enumerator->getChildAtIndex(Index);
    return unique_dyn_cast_or_null<ChildType>(Child);
  }

  /// Advance and return the next child cast to \c ChildType.
  ///
  /// \returns The next \c ChildType instance, or null if exhausted or the cast
  ///     fails.
  std::unique_ptr<ChildType> getNext() override {
    return unique_dyn_cast_or_null<ChildType>(Enumerator->getNext());
  }

  /// Reset the underlying enumerator to its initial position.
  void reset() override { Enumerator->reset(); }

private:

  std::unique_ptr<IPDBEnumSymbols> Enumerator;
};

} // end namespace pdb
} // end namespace llvm

#endif // LLVM_DEBUGINFO_PDB_CONCRETESYMBOLENUMERATOR_H
