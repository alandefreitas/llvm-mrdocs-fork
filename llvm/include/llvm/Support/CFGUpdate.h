//===- CFGUpdate.h - Encode a CFG Edge Update. ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a CFG Edge Update: Insert or Delete, and two Nodes as the
// Edge ends.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_CFGUPDATE_H
#define LLVM_SUPPORT_CFGUPDATE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
/// Helpers for encoding and legalizing CFG edge insert/delete updates.
namespace cfg {
/// Kind of CFG edge update: insert or delete an edge.
enum class UpdateKind : unsigned char {
  /// Insert an edge between two nodes.
  Insert,
  /// Delete an edge between two nodes.
  Delete
};

/// Represents a CFG edge update (insert or delete) between two nodes.
///
/// This class stores the edge endpoints and update kind, and provides
/// accessors, comparison, and printing helpers.
///
/// \tparam NodePtr Pointer type of the CFG nodes at each end of the edge.
template <typename NodePtr> class Update {
  using NodeKindPair = PointerIntPair<NodePtr, 1, UpdateKind>;
  NodePtr From;
  NodeKindPair ToAndKind;

public:
  /// Construct an update of the given kind from \p From to \p To.
  ///
  /// \param Kind Whether to insert or delete the edge.
  /// \param From Source node of the edge.
  /// \param To Destination node of the edge.
  Update(UpdateKind Kind, NodePtr From, NodePtr To)
      : From(From), ToAndKind(To, Kind) {}

  /// Return whether this update inserts or deletes an edge.
  ///
  /// \returns Whether this update inserts or deletes an edge.
  UpdateKind getKind() const { return ToAndKind.getInt(); }
  /// Return the source node of the edge.
  ///
  /// \returns The source node of the edge.
  NodePtr getFrom() const { return From; }
  /// Return the destination node of the edge.
  ///
  /// \returns The destination node of the edge.
  NodePtr getTo() const { return ToAndKind.getPointer(); }
  /// Return true if this update equals \p RHS in kind and endpoints.
  ///
  /// \param RHS Update to compare against.
  /// \returns True if kind and endpoints match \p RHS.
  bool operator==(const Update &RHS) const {
    return From == RHS.From && ToAndKind == RHS.ToAndKind;
  }

  /// Print this update to \p OS.
  ///
  /// \param OS Stream to write to.
  void print(raw_ostream &OS) const {
    OS << (getKind() == UpdateKind::Insert ? "Insert " : "Delete ");
    getFrom()->printAsOperand(OS, false);
    OS << " -> ";
    getTo()->printAsOperand(OS, false);
  }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Dump this update to the debug stream.
  LLVM_DUMP_METHOD void dump() const { print(dbgs()); }
#endif
};

/// Simplify a sequence of CFG edge updates by removing redundancy and cancels.
///
/// This function serves a double purpose:
/// a) It removes redundant updates, which makes it easier to reverse-apply
///    them when traversing CFG.
/// b) It optimizes away updates that cancel each other out, as the end result
///    is the same.
///
/// \param AllUpdates Input sequence of edge updates to legalize.
/// \param Result Receives the simplified list of updates.
/// \param InverseGraph If true, reverse each edge (e.g. for postdominators).
/// \param ReverseResultOrder If true, sort results in ascending original order.
template <typename NodePtr>
void LegalizeUpdates(ArrayRef<Update<NodePtr>> AllUpdates,
                     SmallVectorImpl<Update<NodePtr>> &Result,
                     bool InverseGraph, bool ReverseResultOrder = false) {
  // Count the total number of inserions of each edge.
  // Each insertion adds 1 and deletion subtracts 1. The end number should be
  // one of {-1 (deletion), 0 (NOP), +1 (insertion)}. Otherwise, the sequence
  // of updates contains multiple updates of the same kind and we assert for
  // that case.
  SmallDenseMap<std::pair<NodePtr, NodePtr>, int, 4> Operations;
  Operations.reserve(AllUpdates.size());

  for (const auto &U : AllUpdates) {
    NodePtr From = U.getFrom();
    NodePtr To = U.getTo();
    if (InverseGraph)
      std::swap(From, To); // Reverse edge for postdominators.

    Operations[{From, To}] += (U.getKind() == UpdateKind::Insert ? 1 : -1);
  }

  Result.clear();
  Result.reserve(Operations.size());
  for (auto &Op : Operations) {
    const int NumInsertions = Op.second;
    assert(std::abs(NumInsertions) <= 1 && "Unbalanced operations!");
    if (NumInsertions == 0)
      continue;
    const UpdateKind UK =
        NumInsertions > 0 ? UpdateKind::Insert : UpdateKind::Delete;
    Result.push_back({UK, Op.first.first, Op.first.second});
  }

  // Make the order consistent by not relying on pointer values within the
  // set. Reuse the old Operations map.
  // In the future, we should sort by something else to minimize the amount
  // of work needed to perform the series of updates.
  for (size_t i = 0, e = AllUpdates.size(); i != e; ++i) {
    const auto &U = AllUpdates[i];
    if (!InverseGraph)
      Operations[{U.getFrom(), U.getTo()}] = int(i);
    else
      Operations[{U.getTo(), U.getFrom()}] = int(i);
  }

  llvm::sort(Result, [&](const Update<NodePtr> &A, const Update<NodePtr> &B) {
    const auto &OpA = Operations[{A.getFrom(), A.getTo()}];
    const auto &OpB = Operations[{B.getFrom(), B.getTo()}];
    return ReverseResultOrder ? OpA < OpB : OpA > OpB;
  });
}

} // end namespace cfg
} // end namespace llvm

#endif // LLVM_SUPPORT_CFGUPDATE_H
