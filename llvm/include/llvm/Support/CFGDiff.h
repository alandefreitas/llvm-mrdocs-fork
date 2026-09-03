//===- CFGDiff.h - Define a CFG snapshot. -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines specializations of GraphTraits that allows generic
// algorithms to see a different snapshot of a CFG.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_CFGDIFF_H
#define LLVM_SUPPORT_CFGDIFF_H

#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/CFGUpdate.h"
#include "llvm/Support/type_traits.h"
#include <cassert>
#include <cstddef>

// Two booleans are used to define orders in graphs:
// InverseGraph defines when we need to reverse the whole graph and is as such
// also equivalent to applying updates in reverse.
// InverseEdge defines whether we want to change the edges direction. E.g., for
// a non-inversed graph, the children are naturally the successors when
// InverseEdge is false and the predecessors when InverseEdge is true.

namespace llvm {

namespace detail {
template <typename Range>
auto reverse_if_helper(Range &&R, std::bool_constant<false>) {
  return std::forward<Range>(R);
}

template <typename Range>
auto reverse_if_helper(Range &&R, std::bool_constant<true>) {
  return llvm::reverse(std::forward<Range>(R));
}

template <bool B, typename Range> auto reverse_if(Range &&R) {
  return reverse_if_helper(std::forward<Range>(R), std::bool_constant<B>{});
}
} // namespace detail

/// CFG snapshot that overlays insert/delete edge updates on a graph.
///
/// Given a set of Update<NodePtr>, provides a getChildren method to get a
/// Node's children based on the additional updates in the snapshot. The
/// current diff treats the CFG as a graph rather than a multigraph. Added
/// edges are pruned to be unique, and deleted edges will remove all existing
/// edges between two blocks.
template <typename NodePtr, bool InverseGraph = false> class GraphDiff {
  struct DeletesInserts {
    SmallVector<NodePtr, 2> DI[2];
  };
  using UpdateMapType = SmallDenseMap<NodePtr, DeletesInserts>;
  UpdateMapType Succ;
  UpdateMapType Pred;

  // By default, it is assumed that, given a CFG and a set of updates, we wish
  // to apply these updates as given. If UpdatedAreReverseApplied is set, the
  // updates will be applied in reverse: deleted edges are considered re-added
  // and inserted edges are considered deleted when returning children.
  bool UpdatedAreReverseApplied;

  // Keep the list of legalized updates for a deterministic order of updates
  // when using a GraphDiff for incremental updates in the DominatorTree.
  // The list is kept in reverse to allow popping from end.
  SmallVector<cfg::Update<NodePtr>, 4> LegalizedUpdates;

  void printMap(raw_ostream &OS, const UpdateMapType &M) const {
    StringRef DIText[2] = {"Delete", "Insert"};
    for (auto Pair : M) {
      for (unsigned IsInsert = 0; IsInsert <= 1; ++IsInsert) {
        OS << DIText[IsInsert] << " edges: \n";
        for (auto Child : Pair.second.DI[IsInsert]) {
          OS << "(";
          Pair.first->printAsOperand(OS, false);
          OS << ", ";
          Child->printAsOperand(OS, false);
          OS << ") ";
        }
      }
    }
    OS << "\n";
  }

public:
  /// Construct an empty CFG snapshot with no edge updates.
  GraphDiff() : UpdatedAreReverseApplied(false) {}
  /// Construct a CFG snapshot from \p Updates, optionally reverse-applied.
  ///
  /// \param Updates Edge insert/delete updates that define the snapshot.
  /// \param ReverseApplyUpdates If true, treat deletes as inserts and inserts
  ///        as deletes when returning children.
  GraphDiff(ArrayRef<cfg::Update<NodePtr>> Updates,
            bool ReverseApplyUpdates = false) {
    cfg::LegalizeUpdates<NodePtr>(Updates, LegalizedUpdates, InverseGraph);
    for (auto U : LegalizedUpdates) {
      unsigned IsInsert =
          (U.getKind() == cfg::UpdateKind::Insert) == !ReverseApplyUpdates;
      Succ[U.getFrom()].DI[IsInsert].push_back(U.getTo());
      Pred[U.getTo()].DI[IsInsert].push_back(U.getFrom());
    }
    UpdatedAreReverseApplied = ReverseApplyUpdates;
  }

  /// Return a range over the legalized edge updates in this snapshot.
  ///
  /// \returns A range covering the legalized edge updates.
  auto getLegalizedUpdates() const {
    return make_range(LegalizedUpdates.begin(), LegalizedUpdates.end());
  }

  /// Return the number of legalized edge updates in this snapshot.
  ///
  /// \returns The number of legalized edge updates.
  unsigned getNumLegalizedUpdates() const { return LegalizedUpdates.size(); }

  /// Pop and return the next legalized update for incremental DomTree updates.
  ///
  /// \returns The next legalized edge update, removed from the snapshot.
  cfg::Update<NodePtr> popUpdateForIncrementalUpdates() {
    assert(!LegalizedUpdates.empty() && "No updates to apply!");
    auto U = LegalizedUpdates.pop_back_val();
    unsigned IsInsert =
        (U.getKind() == cfg::UpdateKind::Insert) == !UpdatedAreReverseApplied;
    auto &SuccDIList = Succ[U.getFrom()];
    auto &SuccList = SuccDIList.DI[IsInsert];
    assert(SuccList.back() == U.getTo());
    SuccList.pop_back();
    if (SuccList.empty() && SuccDIList.DI[!IsInsert].empty())
      Succ.erase(U.getFrom());

    auto &PredDIList = Pred[U.getTo()];
    auto &PredList = PredDIList.DI[IsInsert];
    assert(PredList.back() == U.getFrom());
    PredList.pop_back();
    if (PredList.empty() && PredDIList.DI[!IsInsert].empty())
      Pred.erase(U.getTo());
    return U;
  }

  /// Small vector type used to return a node's children in the snapshot.
  using VectRet = SmallVector<NodePtr, 8>;
  /// Return the children of \p N as seen through this CFG snapshot.
  ///
  /// \tparam InverseEdge If true, walk predecessors instead of successors.
  /// \param N Node whose children (or predecessors) to collect.
  /// \returns The children (or predecessors) of \p N after applying the
  ///          snapshot's edge inserts and deletes.
  template <bool InverseEdge> VectRet getChildren(NodePtr N) const {
    using DirectedNodeT =
        std::conditional_t<InverseEdge, Inverse<NodePtr>, NodePtr>;
    auto R = children<DirectedNodeT>(N);
    VectRet Res = VectRet(detail::reverse_if<!InverseEdge>(R));

    auto &Children = (InverseEdge != InverseGraph) ? Pred : Succ;
    auto It = Children.find(N);
    if (It == Children.end())
      return Res;

    // Remove children present in the CFG but not in the snapshot.
    for (auto *Child : It->second.DI[0])
      llvm::erase(Res, Child);

    // Add children present in the snapshot for not in the real CFG.
    auto &AddedChildren = It->second.DI[1];
    llvm::append_range(Res, AddedChildren);

    return Res;
  }

  /// Print this CFG snapshot's edge insert/delete maps to \p OS.
  ///
  /// \param OS Stream to write to.
  void print(raw_ostream &OS) const {
    OS << "===== GraphDiff: CFG edge changes to create a CFG snapshot. \n"
          "===== (Note: notion of children/inverse_children depends on "
          "the direction of edges and the graph.)\n";
    OS << "Children to delete/insert:\n\t";
    printMap(OS, Succ);
    OS << "Inverse_children to delete/insert:\n\t";
    printMap(OS, Pred);
    OS << "\n";
  }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Dump this CFG snapshot to the debug stream.
  LLVM_DUMP_METHOD void dump() const { print(dbgs()); }
#endif
};
} // end namespace llvm

#endif // LLVM_SUPPORT_CFGDIFF_H
