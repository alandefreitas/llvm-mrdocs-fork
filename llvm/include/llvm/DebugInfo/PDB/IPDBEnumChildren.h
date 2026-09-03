//===- IPDBEnumChildren.h - base interface for child enumerator -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_IPDBENUMCHILDREN_H
#define LLVM_DEBUGINFO_PDB_IPDBENUMCHILDREN_H

#include "llvm/DebugInfo/CodeView/LazyRandomTypeCollection.h"
#include <cassert>
#include <cstdint>
#include <memory>

namespace llvm {
namespace pdb {

/// IPDBEnumChildren defines an interface for enumerating child objects of type
/// \c ChildType from a PDB or related debug data source.
template <typename ChildType> class IPDBEnumChildren {
public:
  /// Owning pointer to a single enumerated child of type \c ChildType.
  using ChildTypePtr = std::unique_ptr<ChildType>;
  /// This enumerator specialization for the current \c ChildType.
  using MyType = IPDBEnumChildren<ChildType>;

  /// Destroy the child enumerator.
  virtual ~IPDBEnumChildren() = default;

  /// Return the number of children available from this enumerator.
  ///
  /// \returns The total number of child objects.
  virtual uint32_t getChildCount() const = 0;

  /// Return the child at the given zero-based \p Index.
  ///
  /// \param Index Zero-based index of the child to retrieve.
  ///
  /// \returns An owning pointer to the child, or null if \p Index is out of
  ///     range or the child is unavailable.
  virtual ChildTypePtr getChildAtIndex(uint32_t Index) const = 0;

  /// Advance the enumerator and return the next child.
  ///
  /// \returns An owning pointer to the next child, or null when exhausted.
  virtual ChildTypePtr getNext() = 0;

  /// Reset the enumerator to its initial position.
  virtual void reset() = 0;
};

/// Empty enumerator that reports no children of type \c ChildType.
template <typename ChildType>
class NullEnumerator : public IPDBEnumChildren<ChildType> {
  uint32_t getChildCount() const override { return 0; }
  std::unique_ptr<ChildType> getChildAtIndex(uint32_t Index) const override {
    return nullptr;
  }
  std::unique_ptr<ChildType> getNext() override { return nullptr; }
  void reset() override {}
};

} // end namespace pdb
} // end namespace llvm

#endif // LLVM_DEBUGINFO_PDB_IPDBENUMCHILDREN_H
