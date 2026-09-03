//===- GenericDomTreeUpdater.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the GenericDomTreeUpdater class, which provides a uniform
// way to update dominator tree related data structures.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_GENERICDOMTREEUPDATER_H
#define LLVM_ANALYSIS_GENERICDOMTREEUPDATER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

/// Provides a uniform way to update dominator tree related data structures.
///
/// CRTP base that applies CFG updates to \c DomTreeT and/or \c PostDomTreeT
/// under Eager or Lazy strategies. Derived classes specialize basic-block
/// deletion for their IR or Machine IR representation.
template <typename DerivedT, typename DomTreeT, typename PostDomTreeT>
class GenericDomTreeUpdater {
  DerivedT &derived() { return *static_cast<DerivedT *>(this); }
  const DerivedT &derived() const {
    return *static_cast<const DerivedT *>(this);
  }

public:
  /// Controls whether CFG updates are applied immediately or deferred.
  enum class UpdateStrategy : unsigned char {
    /// Apply each submitted update immediately.
    Eager = 0,
    /// Queue updates until an explicit flush.
    Lazy = 1
  };
  /// Basic-block type used by the dominator trees being updated.
  using BasicBlockT = typename DomTreeT::NodeType;
  /// CFG edge update type used by the dominator trees being updated.
  using UpdateT = typename DomTreeT::UpdateType;

  /// Construct with an update strategy and no trees.
  /// \param Strategy_ Eager or Lazy update strategy.
  explicit GenericDomTreeUpdater(UpdateStrategy Strategy_)
      : Strategy(Strategy_) {}
  /// Construct with a dominator tree reference and update strategy.
  /// \param DT_ Dominator tree to update.
  /// \param Strategy_ Eager or Lazy update strategy.
  GenericDomTreeUpdater(DomTreeT &DT_, UpdateStrategy Strategy_)
      : DT(&DT_), Strategy(Strategy_) {}
  /// Construct with an optional dominator tree pointer and update strategy.
  /// \param DT_ Dominator tree to update, or null.
  /// \param Strategy_ Eager or Lazy update strategy.
  GenericDomTreeUpdater(DomTreeT *DT_, UpdateStrategy Strategy_)
      : DT(DT_), Strategy(Strategy_) {}
  /// Construct with a post-dominator tree reference and update strategy.
  /// \param PDT_ Post-dominator tree to update.
  /// \param Strategy_ Eager or Lazy update strategy.
  GenericDomTreeUpdater(PostDomTreeT &PDT_, UpdateStrategy Strategy_)
      : PDT(&PDT_), Strategy(Strategy_) {}
  /// Construct with an optional post-dominator tree pointer and update strategy.
  /// \param PDT_ Post-dominator tree to update, or null.
  /// \param Strategy_ Eager or Lazy update strategy.
  GenericDomTreeUpdater(PostDomTreeT *PDT_, UpdateStrategy Strategy_)
      : PDT(PDT_), Strategy(Strategy_) {}
  /// Construct with dominator and post-dominator tree references.
  /// \param DT_ Dominator tree to update.
  /// \param PDT_ Post-dominator tree to update.
  /// \param Strategy_ Eager or Lazy update strategy.
  GenericDomTreeUpdater(DomTreeT &DT_, PostDomTreeT &PDT_,
                        UpdateStrategy Strategy_)
      : DT(&DT_), PDT(&PDT_), Strategy(Strategy_) {}
  /// Construct with optional dominator and post-dominator tree pointers.
  /// \param DT_ Dominator tree to update, or null.
  /// \param PDT_ Post-dominator tree to update, or null.
  /// \param Strategy_ Eager or Lazy update strategy.
  GenericDomTreeUpdater(DomTreeT *DT_, PostDomTreeT *PDT_,
                        UpdateStrategy Strategy_)
      : DT(DT_), PDT(PDT_), Strategy(Strategy_) {}

  /// Assert that pending updates were flushed, then destroy this updater.
  ~GenericDomTreeUpdater() {
    // We cannot call into derived() here as it will already be destroyed.
    assert(!hasPendingUpdates() &&
           "Pending updates were not flushed by derived class.");
  }

  /// Returns true if the current strategy is Lazy.
  /// @return True if the update strategy is Lazy.
  bool isLazy() const { return Strategy == UpdateStrategy::Lazy; }

  /// Returns true if the current strategy is Eager.
  /// @return True if the update strategy is Eager.
  bool isEager() const { return Strategy == UpdateStrategy::Eager; }

  /// Returns true if it holds a DomTreeT.
  /// @return True if a dominator tree is held.
  bool hasDomTree() const { return DT != nullptr; }

  /// Returns true if it holds a PostDomTreeT.
  /// @return True if a post-dominator tree is held.
  bool hasPostDomTree() const { return PDT != nullptr; }

  /// Returns true if a basic block is awaiting deletion.
  ///
  /// The deletion will only happen until a flush event and all available trees
  /// are up-to-date. Returns false under Eager UpdateStrategy.
  /// @return True if any basic block is awaiting deletion.
  bool hasPendingDeletedBB() const { return !DeletedBBs.empty(); }

  /// Returns true if DelBB is awaiting deletion.
  ///
  /// Returns false under Eager UpdateStrategy.
  /// \param DelBB Basic block to check for pending deletion.
  /// @return True if \p DelBB is pending deletion.
  bool isBBPendingDeletion(BasicBlockT *DelBB) const {
    if (Strategy == UpdateStrategy::Eager || DeletedBBs.empty())
      return false;
    return DeletedBBs.contains(DelBB);
  }

  /// Returns true if either DT or PDT has at least one update pending.
  ///
  /// If DT or PDT is nullptr it is treated as having no pending updates. This
  /// function does not check whether there is MachineBasicBlock awaiting
  /// deletion. Returns false under Eager UpdateStrategy.
  /// @return True if either tree has pending updates.
  bool hasPendingUpdates() const {
    return hasPendingDomTreeUpdates() || hasPendingPostDomTreeUpdates();
  }

  /// Returns true if there are DomTreeT updates queued.
  /// Returns false under Eager UpdateStrategy or DT is nullptr.
  /// @return True if dominator tree updates are pending.
  bool hasPendingDomTreeUpdates() const {
    if (!DT)
      return false;
    return PendUpdates.size() != PendDTUpdateIndex;
  }

  /// Returns true if there are PostDomTreeT updates queued.
  /// Returns false under Eager UpdateStrategy or PDT is nullptr.
  /// @return True if post-dominator tree updates are pending.
  bool hasPendingPostDomTreeUpdates() const {
    if (!PDT)
      return false;
    return PendUpdates.size() != PendPDTUpdateIndex;
  }

  ///@{
  /// \name Mutation APIs
  ///
  /// These methods provide APIs for submitting updates to the DomTreeT and
  /// the PostDominatorTree.
  ///
  /// Note: There are two strategies to update the DomTreeT and the
  /// PostDominatorTree:
  /// 1. Eager UpdateStrategy: Updates are submitted and then flushed
  /// immediately.
  /// 2. Lazy UpdateStrategy: Updates are submitted but only flushed when you
  /// explicitly call Flush APIs. It is recommended to use this update strategy
  /// when you submit a bunch of updates multiple times which can then
  /// add up to a large number of updates between two queries on the
  /// DomTreeT. The incremental updater can reschedule the updates or
  /// decide to recalculate the dominator tree in order to speedup the updating
  /// process depending on the number of updates.
  ///
  /// Although GenericDomTree provides several update primitives,
  /// it is not encouraged to use these APIs directly.

  /// Notify DTU that the entry block was replaced.
  ///
  /// Recalculate all available trees and flush all BasicBlocks awaiting
  /// deletion immediately.
  /// \param F Function whose trees should be recalculated.
  template <typename FuncT> void recalculate(FuncT &F);

  /// Submit updates to all available trees.
  ///
  /// The Eager Strategy flushes updates immediately while the Lazy Strategy
  /// queues the updates.
  ///
  /// Note: The "existence" of an edge in a CFG refers to the CFG which DTU is
  /// in sync with + all updates before that single update.
  ///
  /// CAUTION!
  /// 1. It is required for the state of the LLVM IR to be updated
  /// *before* submitting the updates because the internal update routine will
  /// analyze the current state of the CFG to determine whether an update
  /// is valid.
  /// 2. It is illegal to submit any update that has already been submitted,
  /// i.e., you are supposed not to insert an existent edge or delete a
  /// nonexistent edge.
  /// \param Updates CFG edge updates to apply.
  void applyUpdates(ArrayRef<UpdateT> Updates);

  /// Apply updates that the critical edge (FromBB, ToBB) has been
  /// split with NewBB.
  /// \param FromBB Source block of the critical edge.
  /// \param ToBB Destination block of the critical edge.
  /// \param NewBB Block inserted to split the critical edge.
  void splitCriticalEdge(BasicBlockT *FromBB, BasicBlockT *ToBB,
                         BasicBlockT *NewBB);

  /// Submit updates to all available trees, filtering duplicates and invalids.
  ///
  /// It will also
  /// 1. discard duplicated updates,
  /// 2. remove invalid updates. (Invalid updates means deletion of an edge that
  /// still exists or insertion of an edge that does not exist.)
  /// The Eager Strategy flushes updates immediately while the Lazy Strategy
  /// queues the updates.
  ///
  /// Note: The "existence" of an edge in a CFG refers to the CFG which DTU is
  /// in sync with + all updates before that single update.
  ///
  /// CAUTION!
  /// 1. It is required for the state of the LLVM IR to be updated
  /// *before* submitting the updates because the internal update routine will
  /// analyze the current state of the CFG to determine whether an update
  /// is valid.
  /// 2. It is illegal to submit any update that has already been submitted,
  /// i.e., you are supposed not to insert an existent edge or delete a
  /// nonexistent edge.
  /// 3. It is only legal to submit updates to an edge in the order CFG changes
  /// are made. The order you submit updates on different edges is not
  /// restricted.
  /// \param Updates CFG edge updates to apply permissively.
  void applyUpdatesPermissive(ArrayRef<UpdateT> Updates);

  ///@}

  ///@{
  /// \name Flush APIs
  ///
  /// CAUTION! By the moment these flush APIs are called, the current CFG needs
  /// to be the same as the CFG which DTU is in sync with + all updates
  /// submitted.

  /// Flush DomTree updates and return DomTree.
  /// It flushes Deleted BBs if both trees are up-to-date.
  /// It must only be called when it has a DomTree.
  /// @return The dominator tree after flushing pending updates.
  DomTreeT &getDomTree();

  /// Flush PostDomTree updates and return PostDomTree.
  /// It flushes Deleted BBs if both trees are up-to-date.
  /// It must only be called when it has a PostDomTree.
  /// @return The post-dominator tree after flushing pending updates.
  PostDomTreeT &getPostDomTree();

  /// Apply all pending updates to available trees and flush all BasicBlocks
  /// awaiting deletion.

  void flush() {
    applyDomTreeUpdates();
    applyPostDomTreeUpdates();
    dropOutOfDateUpdates();
  }

  ///@}

  /// Debug method to help view the internal state of this class.
  LLVM_DUMP_METHOD void dump() const;

protected:
  /// Helper structure used to hold all the basic blocks
  /// involved in the split of a critical edge.
  struct CriticalEdge {
    /// Source block of the critical edge being split.
    BasicBlockT *FromBB;
    /// Destination block of the critical edge being split.
    BasicBlockT *ToBB;
    /// New block inserted on the critical edge.
    BasicBlockT *NewBB;
  };

  /// Pending CFG update, either a normal edge update or a critical-edge split.
  struct DomTreeUpdate {
    /// True when this entry records a critical-edge split rather than an UpdateT.
    bool IsCriticalEdgeSplit = false;
    union {
      /// Ordinary insert/delete edge update.
      UpdateT Update;
      /// Critical-edge split describing FromBB, ToBB, and NewBB.
      CriticalEdge EdgeSplit;
    };
    /// Construct from an ordinary CFG edge update.
    /// \param Update Edge insertion or deletion to queue.
    DomTreeUpdate(UpdateT Update) : Update(Update) {}
    /// Construct from a critical-edge split description.
    /// \param E Critical edge that was split.
    DomTreeUpdate(CriticalEdge E) : IsCriticalEdgeSplit(true), EdgeSplit(E) {}
  };

  /// Queued CFG updates not yet applied to available trees.
  SmallVector<DomTreeUpdate, 16> PendUpdates;
  /// Index of the next pending update to apply to the dominator tree.
  size_t PendDTUpdateIndex = 0;
  /// Index of the next pending update to apply to the post-dominator tree.
  size_t PendPDTUpdateIndex = 0;
  /// Dominator tree being updated, or null if none is held.
  DomTreeT *DT = nullptr;
  /// Post-dominator tree being updated, or null if none is held.
  PostDomTreeT *PDT = nullptr;
  /// Whether updates are applied eagerly or lazily.
  const UpdateStrategy Strategy;
  /// Basic blocks awaiting deletion under the Lazy strategy.
  SmallPtrSet<BasicBlockT *, 8> DeletedBBs;
  /// True while the dominator tree is being fully recalculated.
  bool IsRecalculatingDomTree = false;
  /// True while the post-dominator tree is being fully recalculated.
  bool IsRecalculatingPostDomTree = false;

  /// Returns true if the update is self dominance.
  /// \param Update Edge update whose endpoints are compared.
  /// @return True if the update's from and to blocks are the same.
  bool isSelfDominance(UpdateT Update) const {
    // Won't affect DomTree and PostDomTree.
    return Update.getFrom() == Update.getTo();
  }

  /// Helper function to apply all pending DomTree updates.
  void applyDomTreeUpdates() { applyUpdatesImpl<true>(); }

  /// Helper function to apply all pending PostDomTree updates.
  void applyPostDomTreeUpdates() { applyUpdatesImpl<false>(); }

  /// Returns true if the update appears in the LLVM IR.
  ///
  /// It is used to check whether an update is valid in insertEdge/deleteEdge or
  /// is unnecessary in the batch update.
  /// \param Update Edge update to validate against the current CFG.
  /// @return True if the update is present in the current CFG.
  bool isUpdateValid(UpdateT Update) const;

  /// Erase Basic Block node before it is unlinked from Function
  /// in the DomTree and PostDomTree.
  /// \param DelBB Basic block whose tree nodes should be erased.
  void eraseDelBBNode(BasicBlockT *DelBB);

  /// Helper function to flush deleted BasicBlocks if all available
  /// trees are up-to-date.
  void tryFlushDeletedBB();

  /// Drop all updates applied by all available trees and delete BasicBlocks if
  /// all available trees are up-to-date.
  void dropOutOfDateUpdates();

private:
  void splitDTCriticalEdges(ArrayRef<CriticalEdge> Updates);
  void splitPDTCriticalEdges(ArrayRef<CriticalEdge> Updates);
  template <bool IsForward> void applyUpdatesImpl();
};

} // namespace llvm

#endif // LLVM_ANALYSIS_GENERICDOMTREEUPDATER_H
