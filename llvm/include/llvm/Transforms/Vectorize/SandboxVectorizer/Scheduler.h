//===- Scheduler.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is the bottom-up list scheduler used by the vectorizer. It is used for
// checking the legality of vectorization and for scheduling instructions in
// such a way that makes vectorization possible, if legal.
//
// The legality check is performed by `trySchedule(Instrs)`, which will try to
// schedule the IR until all instructions in `Instrs` can be scheduled together
// back-to-back. If this fails then it is illegal to vectorize `Instrs`.
//
// Internally the scheduler uses the vectorizer-specific DependencyGraph class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_SCHEDULER_H
#define LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_SCHEDULER_H

#include "llvm/SandboxIR/Instruction.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Transforms/Vectorize/SandboxVectorizer/DependencyGraph.h"
#include <queue>
#include <variant>

namespace llvm::sandboxir {

/// Comparator that orders ready-list nodes by scheduling priority.
class PriorityCmp {
public:
  /// Return true if \p N1 should be ordered after \p N2 in the ready list.
  ///
  /// Terminators have lowest priority, PHIs have highest priority, and
  /// otherwise nodes follow instruction order.
  /// @param N1 Left-hand dependency-graph node.
  /// @param N2 Right-hand dependency-graph node.
  /// @return True if \p N1 has lower priority than \p N2.
  bool operator()(const DGNode *N1, const DGNode *N2) {
    // Given that the DAG does not model dependencies such that PHIs are always
    // at the top, or terminators always at the bottom, we need to force the
    // priority here in the comparator of the ready list container.
    auto *I1 = N1->getInstruction();
    auto *I2 = N2->getInstruction();
    bool IsTerm1 = I1->isTerminator();
    bool IsTerm2 = I2->isTerminator();
    if (IsTerm1 != IsTerm2)
      // Terminators have the lowest priority.
      return IsTerm1 > IsTerm2;
    bool IsPHI1 = isa<PHINode>(I1);
    bool IsPHI2 = isa<PHINode>(I2);
    if (IsPHI1 != IsPHI2)
      // PHIs have the highest priority.
      return IsPHI1 < IsPHI2;
    // Otherwise rely on the instruction order.
    return I2->comesBefore(I1);
  }
};

/// The list holding nodes that are ready to schedule. Used by the scheduler.
class ReadyListContainer {
  PriorityCmp Cmp;
  /// Control/Other dependencies are not modeled by the DAG to save memory.
  /// These have to be modeled in the ready list for correctness.
  /// This means that the list will hold back nodes that need to meet such
  /// unmodeled dependencies.
  std::priority_queue<DGNode *, std::vector<DGNode *>, PriorityCmp> List;

public:
  /// Construct an empty ready list.
  ReadyListContainer() : List(Cmp) {}
  /// Insert ready node \p N into the ready list.
  /// @param N Node that is ready to schedule.
  void insert(DGNode *N) {
#ifndef NDEBUG
    assert(!N->scheduled() && "Don't insert a scheduled node!");
    assert(!contains(N) && "Node already exists in ready list!");
#endif
    List.push(N);
  }
  /// Remove and return the highest-priority ready node.
  /// @return The highest-priority DGNode removed from the list.
  DGNode *pop() {
    auto *Back = List.top();
    List.pop();
    return Back;
  }
  /// Return true if the ready list is empty.
  /// @return True if the ready list contains no nodes.
  bool empty() const { return List.empty(); }
  /// Remove all nodes from the ready list.
  void clear() { List = {}; }
  /// Return true if the ready list contains \p N.
  /// @param N Node to look up.
  /// @return True if \p N is present in the ready list.
  bool contains(DGNode *N) const {
    // TODO: We should update the data structure to make this O(1).
    auto ListCopy = List;
    while (!ListCopy.empty()) {
      DGNode *Top = ListCopy.top();
      if (Top == N)
        return true;
      ListCopy.pop();
    }
    return false;
  }
  /// Remove \p N if found in the ready list.
  /// @param N Node to remove.
  void remove(DGNode *N) {
    // TODO: Use a more efficient data-structure for the ready list because the
    // priority queue does not support fast removals.
    SmallVector<DGNode *, 8> Keep;
    Keep.reserve(List.size());
    while (!List.empty()) {
      auto *Top = List.top();
      List.pop();
      if (Top == N)
        break;
      Keep.push_back(Top);
    }
    for (auto *KeepN : Keep)
      List.push(KeepN);
  }
#ifndef NDEBUG
  /// Print the ready list to \p OS.
  /// @param OS Stream to write to.
  void dump(raw_ostream &OS) const;
  /// Dump the ready list to the debug stream.
  LLVM_DUMP_METHOD void dump() const;
#endif // NDEBUG
};

/// The nodes that need to be scheduled back-to-back in a single scheduling
/// cycle form a SchedBundle.
class SchedBundle {
public:
  /// Container type holding the dependency-graph nodes in this bundle.
  using ContainerTy = SmallVector<DGNode *, 4>;

private:
  ContainerTy Nodes;

  /// Called by the DGNode destructor to avoid accessing freed memory.
  void eraseFromBundle(DGNode *N) { llvm::erase(Nodes, N); }
  friend void DGNode::setSchedBundle(SchedBundle &); // For eraseFromBunde().
  friend DGNode::~DGNode();                          // For eraseFromBundle().

public:
  /// Construct an empty scheduling bundle.
  SchedBundle() = default;
  /// Construct a scheduling bundle by taking ownership of \p Nodes.
  /// @param Nodes Dependency-graph nodes moved into this bundle.
  SchedBundle(ContainerTy &&Nodes) : Nodes(std::move(Nodes)) {
    for (auto *N : this->Nodes)
      N->setSchedBundle(*this);
  }
  /// Deleted copy constructor; SchedBundle objects are not copyable.
  /// @param Other Unused source object; copying is deleted.
  SchedBundle(const SchedBundle &Other) = delete;
  /// Deleted copy assignment; SchedBundle objects are not copyable.
  /// @param Other Unused source object; assignment is deleted.
  SchedBundle &operator=(const SchedBundle &Other) = delete;
  /// Destroy this scheduling bundle and clear node back-pointers.
  ~SchedBundle() {
    for (auto *N : this->Nodes)
      N->clearSchedBundle();
  }
  /// Return true if the bundle contains no nodes.
  /// @return True if the bundle is empty.
  bool empty() const { return Nodes.empty(); }
  /// Return true if this is a singleton (non-vector) scheduling bundle.
  ///
  /// Singleton bundles are created when scheduling instructions temporarily to
  /// fill in the schedule until we schedule the vector bundle. These are
  /// non-vector bundles containing just a single instruction.
  /// @return True if the bundle contains exactly one node.
  bool isSingleton() const { return Nodes.size() == 1u; }
  /// Return the last dependency-graph node in the bundle.
  /// @return The last DGNode in the bundle.
  DGNode *back() const { return Nodes.back(); }
  /// Iterator over the dependency-graph nodes in this bundle.
  using iterator = ContainerTy::iterator;
  /// Const iterator over the dependency-graph nodes in this bundle.
  using const_iterator = ContainerTy::const_iterator;
  /// Return an iterator to the first node in the bundle.
  /// @return Iterator to the first node.
  iterator begin() { return Nodes.begin(); }
  /// Return an iterator past the last node in the bundle.
  /// @return Iterator past the last node.
  iterator end() { return Nodes.end(); }
  /// Return a const iterator to the first node in the bundle.
  /// @return Const iterator to the first node.
  const_iterator begin() const { return Nodes.begin(); }
  /// Return a const iterator past the last node in the bundle.
  /// @return Const iterator past the last node.
  const_iterator end() const { return Nodes.end(); }
  /// Return the bundle node that comes before the others in program order.
  /// @return The top-most DGNode in program order.
  LLVM_ABI DGNode *getTop() const;
  /// Return the bundle node that comes after the others in program order.
  /// @return The bottom-most DGNode in program order.
  LLVM_ABI DGNode *getBot() const;
  /// Move all bundle instructions to \p Where back-to-back.
  /// @param Where Insertion point for the clustered instructions.
  LLVM_ABI void cluster(BasicBlock::iterator Where);
  /// Return true if all nodes in the bundle are ready.
  /// @param Dir Scheduling direction used by the ready check.
  /// @return True if every node in the bundle is ready.
  bool ready(SchedDirection Dir) const {
    return all_of(Nodes, [](const auto *N) { return N->ready(); });
  }
#ifndef NDEBUG
  /// Print this scheduling bundle to \p OS.
  /// @param OS Stream to write to.
  void dump(raw_ostream &OS) const;
  /// Dump this scheduling bundle to the debug stream.
  LLVM_DUMP_METHOD void dump() const;
#endif
};

/// Scheduling cursor used by the Scheduler during top-down or bottom-up
/// scheduling.
///
/// The scheduling point in the context of the Scheduler points to the
/// top-of-schedule (i.e., the top-most instruction of the top bundle) during
/// bottom-up scheduling or the bottom of the schedule (i.e., the bottom-most
/// instruction of the bottom bundle) during top-down.
///
/// This class can be thought of as an extended BB::iterator, one that can
/// not only point to after the last instruction in a BB (i.e., BB.end()), but
/// also before the first instruction (i.e., something equivalent to
/// prev(BB.begin()), which is not a legal BasicBlock::iterator).
///
/// This is needed for symmetric implementations of top-down and bottom-up
/// scheduling. More specifically, if this is the first scheduling attempt we
/// need the scheduling front to still point to a hypothetical last scheduling
/// point. In bottom-up this can be at BB.end() but in top-down this can be
/// before BB.begin(). This is why a BasicBlock::iterator is not suitable for
/// this.
class SchedulingPoint {
  /// If Where contains a Block, then we are pointing before BB.begin(),
  /// otherwise if it contains an iterator then we point to anywhere in the BB
  /// or at BB.end().
  std::variant<BasicBlock::iterator, BasicBlock *> Where;

  /// Creates a scheduling point pointing before the beginning of BB.
  SchedulingPoint(BasicBlock &BB) : Where(&BB) {}

public:
  /// Construct a scheduling point pointing at \p It.
  ///
  /// \p It may refer to any instruction in a BB or to BB.end().
  /// @param It Basic-block iterator to point to.
  SchedulingPoint(BasicBlock::iterator It) : Where(It) {}
  /// Return a SchedulingPoint that points to \p It.
  /// @param It Basic-block iterator to point to.
  /// @return A SchedulingPoint at \p It.
  static SchedulingPoint createAt(BasicBlock::iterator It) {
    return SchedulingPoint(It);
  }
  /// Return a SchedulingPoint that points to one element before \p It.
  /// @param It Basic-block iterator used as the reference point.
  /// @return A SchedulingPoint one element before \p It.
  static SchedulingPoint createBefore(BasicBlock::iterator It) {
    BasicBlock &BB = *It.getNodeParent();
    if (It == BB.begin())
      return SchedulingPoint(BB);
    return SchedulingPoint(std::prev(It));
  }
  /// Return a SchedulingPoint that points to one element after \p It.
  /// @param It Basic-block iterator used as the reference point.
  /// @return A SchedulingPoint one element after \p It.
  static SchedulingPoint createAfter(BasicBlock::iterator It) {
    assert(It != It.getNodeParent()->end() && "Already at end!");
    return SchedulingPoint(std::next(It));
  }

  /// If the SchedulingPoint points to before the beginning of a BB, then this
  /// returns that BB, else returns nullptr.
  /// @return The basic block when pointing before begin, otherwise nullptr.
  BasicBlock *atBeforeBeginOrNull() const {
    if (std::holds_alternative<BasicBlock::iterator>(Where))
      return nullptr;
    return std::get<BasicBlock *>(Where);
  }
  /// If the SchedulingPoint points after the last instruction in the BB then
  /// this returns the corresponding BasicBlock, nullptr otherwise.
  /// @return The basic block when pointing at end, otherwise nullptr.
  BasicBlock *atEndOrNull() const {
    if (std::holds_alternative<BasicBlock *>(Where))
      return nullptr;
    auto It = std::get<BasicBlock::iterator>(Where);
    return It == It.getNodeParent()->end() ? It.getNodeParent() : nullptr;
  }
  /// Returns the instruction pointed to by this SchedulingPoint or null if we
  /// are before/after BB.
  /// @return The instruction at this point, or nullptr if before/after the BB.
  Instruction *atInstrOrNull() const {
    if (atBeforeBeginOrNull() || atEndOrNull())
      return nullptr;
    return &*std::get<BasicBlock::iterator>(Where);
  }
  /// Cast to Instruction *. Asserts that we are pointing to an instruction and
  /// not before/after the beginning/end of a BB.
  /// @return The instruction at this scheduling point, or nullptr.
  operator Instruction *() const { return atInstrOrNull(); }
  /// Returns the corresponding BB::iterator. Asserts that we are not pointing
  /// before BB begin.
  /// @return The basic-block iterator for this scheduling point.
  BasicBlock::iterator getIterator() const {
    assert(!atBeforeBeginOrNull() && "Expected in/after BB!");
    return std::get<BasicBlock::iterator>(Where);
  }
  /// Convert this scheduling point to a basic-block iterator.
  /// @return The basic-block iterator for this scheduling point.
  operator BasicBlock::iterator() const { return getIterator(); }
  /// Returns the SchedulingPoint pointing after this.
  /// @return The scheduling point one position after this one.
  SchedulingPoint getNext() const {
    assert(!atEndOrNull() && "Expected before/in BB!");
    if (BasicBlock *BB = atBeforeBeginOrNull())
      return BB->begin();
    return std::next(getIterator());
  }
  /// Returns the SchedulingPoint pointing before this.
  /// @return The scheduling point one position before this one.
  SchedulingPoint getPrev() const {
    assert(!atBeforeBeginOrNull() && "Expected in/after BB!");
    auto It = getIterator();
    auto *BB = It.getNodeParent();
    if (It == BB->begin())
      return *BB;
    return std::prev(It);
  }
  /// Return true if this scheduling point equals \p Other.
  /// @param Other Scheduling point to compare against.
  /// @return True if both points refer to the same location.
  bool operator==(const SchedulingPoint &Other) const {
    return Where == Other.Where;
  }
#ifndef NDEBUG
  /// Print this scheduling point to \p OS.
  /// @param OS Stream to write to.
  void print(raw_ostream &OS) const;
  /// Dump this scheduling point to the debug stream.
  LLVM_DUMP_METHOD void dump() const;
#endif
};

/// The list scheduler.
class Scheduler {
  /// This is a list-scheduler and this is the list containing the instructions
  /// that are ready, meaning that all their dependency successors have already
  /// been scheduled.
  ReadyListContainer ReadyList;
  /// The dependency graph is used by the scheduler to determine the legal
  /// ordering of instructions.
  DependencyGraph DAG;
  friend class SchedulerInternalsAttorney; // For DAG.
  Context &Ctx;
  /// This is the top of the schedule during bottom-up scheduling and the bottom
  /// of the schedule during top-down. It points to the position of the last
  /// top-most/bottom-most instruction scheduled. It may get updated after every
  /// trySchedule() attempt, regardless of whether scheduling succeeded or not.
  /// It is nullopt if we have not scheduled before.
  std::optional<SchedulingPoint> ScheduleTopItOpt;
  // TODO: This is wasting memory in exchange for fast removal using a raw ptr.
  DenseMap<SchedBundle *, std::unique_ptr<SchedBundle>> Bndls;
  /// The BB that we are currently scheduling.
  BasicBlock *ScheduledBB = nullptr;
  /// The ID of the callback we register with Sandbox IR.
  std::optional<Context::CallbackID> CreateInstrCB;
  /// Called by Sandbox IR's callback system, after \p I has been created.
  /// NOTE: This should run after DAG's callback has run.
  // TODO: Perhaps call DAG's notify function from within this one?
  LLVM_ABI void notifyCreateInstr(Instruction *I);

  /// \Returns a scheduling bundle containing \p Instrs.
  SchedBundle *createBundle(ArrayRef<Instruction *> Instrs);
  void eraseBundle(SchedBundle *SB);
  /// Schedule nodes until we can schedule \p Instrs back-to-back.
  bool tryScheduleUntil(ArrayRef<Instruction *> Instrs);
  /// Schedules all nodes in \p Bndl, marks them as scheduled, updates the
  /// UnscheduledSuccs counter of all dependency predecessors, and adds any of
  /// them that become ready to the ready list.
  void scheduleAndUpdateReadyList(SchedBundle &Bndl);
  /// The scheduling state of the instructions in the bundle.
  enum class BndlSchedState {
    NoneScheduled, ///> No instruction in the bundle was previously scheduled.
    AlreadyScheduled, ///> At least one instruction in the bundle belongs to a
                      /// different non-singleton scheduling bundle.
    TemporarilyScheduled, ///> Instructions were temporarily scheduled as
                          /// singleton bundles or some of them were not
                          /// scheduled at all. None of them were in a vector
                          ///(non-singleton) bundle.
    FullyScheduled, ///> All instrs in the bundle were previously scheduled and
                    /// were in the same SchedBundle.
  };
  /// \Returns whether none/some/all of \p Instrs have been scheduled.
  LLVM_ABI BndlSchedState
  getBndlSchedState(ArrayRef<Instruction *> Instrs) const;
  /// Destroy the top-most part of the schedule that includes \p Instrs.
  void trimSchedule(ArrayRef<Instruction *> Instrs);
  /// Disable copies.
  Scheduler(const Scheduler &) = delete;
  Scheduler &operator=(const Scheduler &) = delete;

private:
  SchedDirection Dir = SchedDirection::BottomUp;

public:
  /// Construct a list scheduler for direction \p Dir.
  /// @param AA Alias analysis used to build the dependency graph.
  /// @param Ctx Sandbox IR context that owns instructions and callbacks.
  /// @param Dir Bottom-up or top-down scheduling direction.
  Scheduler(AAResults &AA, Context &Ctx, SchedDirection Dir)
      : DAG(Dir, AA, Ctx), Ctx(Ctx), Dir(Dir) {
    // NOTE: The scheduler's callback depends on the DAG's callback running
    // before it and updating the DAG accordingly.
    CreateInstrCB = Ctx.registerCreateInstrCallback(
        [this](Instruction *I) { notifyCreateInstr(I); });
  }
  /// Destroy the scheduler and unregister its create-instruction callback.
  ~Scheduler() {
    if (CreateInstrCB)
      Ctx.unregisterCreateInstrCallback(*CreateInstrCB);
  }
  /// Try to schedule all of \p Instrs in the same scheduling cycle.
  ///
  /// This essentially checks that there are no dependencies among \p Instrs.
  /// This function may involve scheduling intermediate instructions or
  /// canceling and re-scheduling if needed.
  /// \Returns true on success, false otherwise.
  /// @param Instrs Instructions that must be scheduled together.
  LLVM_ABI bool trySchedule(ArrayRef<Instruction *> Instrs);
  /// Clear the scheduler's state, including the DAG.
  void clear() {
    Bndls.clear();
    // TODO: clear view once it lands.
    DAG.clear();
    ReadyList.clear();
    ScheduleTopItOpt = std::nullopt;
    ScheduledBB = nullptr;
    assert(Bndls.empty() && DAG.empty() && ReadyList.empty() &&
           !ScheduleTopItOpt && ScheduledBB == nullptr &&
           "Expected empty state!");
  }

#ifndef NDEBUG
  /// Print the scheduler state to \p OS.
  /// @param OS Stream to write to.
  void dump(raw_ostream &OS) const;
  /// Dump the scheduler state to the debug stream.
  LLVM_DUMP_METHOD void dump() const;
#endif
};

/// A client-attorney class for accessing the Scheduler's internals (used for
/// unit tests).
class SchedulerInternalsAttorney {
public:
  /// Return the dependency graph owned by \p Sched.
  /// @param Sched Scheduler whose DAG should be exposed.
  /// @return Reference to \p Sched's dependency graph.
  static DependencyGraph &getDAG(Scheduler &Sched) { return Sched.DAG; }
  /// Bundle scheduling state exposed for unit tests.
  using BndlSchedState = Scheduler::BndlSchedState;
  /// Return the scheduling state of \p Instrs in \p Sched.
  /// @param Sched Scheduler that owns the schedule state.
  /// @param Instrs Instructions whose scheduling state is queried.
  /// @return The bundle scheduling state of \p Instrs.
  static BndlSchedState getBndlSchedState(const Scheduler &Sched,
                                          ArrayRef<Instruction *> Instrs) {
    return Sched.getBndlSchedState(Instrs);
  }
};

} // namespace llvm::sandboxir

#endif // LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_SCHEDULER_H
