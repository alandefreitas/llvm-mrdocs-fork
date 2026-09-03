//===- llvm/IR/UseListOrder.h - LLVM Use List Order -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file has structures and command-line options for preserving use-list
// order.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_USELISTORDER_H
#define LLVM_IR_USELISTORDER_H

#include <cstddef>
#include <vector>

namespace llvm {

class Function;
class Value;

/// Structure to hold a use-list order.
struct UseListOrder {
  /// Value whose use-list order is recorded.
  const Value *V = nullptr;
  /// Function that owns \c V, or null for module-level values.
  const Function *F = nullptr;
  /// Permutation of use indices that restores the original use-list order.
  std::vector<unsigned> Shuffle;

  /// Construct a use-list order for \p V with a shuffle of size \p ShuffleSize.
  /// \param V Value whose use-list order is recorded.
  /// \param F Function that owns \p V, or null for module-level values.
  /// \param ShuffleSize Number of use indices in the shuffle permutation.
  UseListOrder(const Value *V, const Function *F, size_t ShuffleSize)
      : V(V), F(F), Shuffle(ShuffleSize) {}

  /// Construct an empty use-list order with null value and function.
  UseListOrder() = default;
  /// Move-construct from use-list order \p Other.
  /// \param Other Use-list order to move from.
  UseListOrder(UseListOrder &&Other) = default;
  /// Move-assign from use-list order \p Other.
  /// \param Other Use-list order to move from.
  /// \return Reference to this object.
  UseListOrder &operator=(UseListOrder &&Other) = default;
};

/// Stack of use-list orders collected while predicting serialization order.
using UseListOrderStack = std::vector<UseListOrder>;

} // end namespace llvm

#endif // LLVM_IR_USELISTORDER_H
