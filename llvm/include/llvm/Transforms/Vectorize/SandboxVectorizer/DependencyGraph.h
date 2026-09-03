//===- DependencyGraph.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the dependency graph used by the vectorizer's instruction
// scheduler.
//
// The nodes of the graph are objects of the `DGNode` class. Each `DGNode`
// object points to an instruction.
// The edges between `DGNode`s are implicitly defined by an ordered set of
// predecessor nodes, to save memory.
// Finally the whole dependency graph is an object of the `DependencyGraph`
// class, which also provides the API for creating/extending the graph from
// input Sandbox IR.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_DEPENDENCYGRAPH_H
#define LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_DEPENDENCYGRAPH_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/SandboxIR/Instruction.h"
#include "llvm/SandboxIR/IntrinsicInst.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Transforms/Vectorize/SandboxVectorizer/Interval.h"

namespace llvm::sandboxir {

class DependencyGraph;
class MemDGNode;
class SchedBundle;

/// Direction in which the scheduler walks the dependency graph.
enum class SchedDirection {
  /// Schedule from the bottom of the region upward.
  BottomUp,
  /// Schedule from the top of the region downward.
  TopDown,
};
#ifndef NDEBUG
/// Return a string name for scheduling direction \p Dir.
///
/// \param Dir Scheduling direction to convert.
/// \return A string literal naming \p Dir.
StringLiteral schedDirectionToStr(SchedDirection Dir);
#endif

/// SubclassIDs for isa/dyn_cast etc.
enum class DGNodeID {
  /// Ordinary dependency-graph node for a non-memory instruction.
  DGNode,
  /// Dependency-graph node for a memory or ordering-constrained instruction.
  MemDGNode,
};

class DGNode;
class MemDGNode;
class DependencyGraph;

// Defined in Transforms/Vectorize/SandboxVectorizer/Interval.cpp
/// Explicit instantiation of Interval specialized for MemDGNode.
extern template class LLVM_TEMPLATE_ABI Interval<MemDGNode>;

/// Iterate over both def-use and mem dependencies.
class PredIterator {
  User::op_iterator OpIt;
  User::op_iterator OpItE;
  DenseSet<MemDGNode *>::iterator MemIt;
  DGNode *N = nullptr;
  DependencyGraph *DAG = nullptr;

  PredIterator(const User::op_iterator &OpIt, const User::op_iterator &OpItE,
               const DenseSet<MemDGNode *>::iterator &MemIt, DGNode *N,
               DependencyGraph &DAG)
      : OpIt(OpIt), OpItE(OpItE), MemIt(MemIt), N(N), DAG(&DAG) {}
  PredIterator(const User::op_iterator &OpIt, const User::op_iterator &OpItE,
               DGNode *N, DependencyGraph &DAG)
      : OpIt(OpIt), OpItE(OpItE), N(N), DAG(&DAG) {}
  friend class DGNode;    // For constructor
  friend class MemDGNode; // For constructor

  /// Skip iterators that don't point instructions or are outside \p DAG,
  /// starting from \p OpIt and ending before \p OpItE.n
  LLVM_ABI static User::op_iterator skipBadIt(User::op_iterator OpIt,
                                              User::op_iterator OpItE,
                                              const DependencyGraph &DAG);

public:
  /// Signed type used to express distances between iterators.
  using difference_type = std::ptrdiff_t;
  /// Element type produced when the iterator is dereferenced.
  using value_type = DGNode *;
  /// Pointer type equivalent to the iterator's value type.
  using pointer = value_type *;
  /// Reference type returned by dereferencing the iterator.
  using reference = value_type &;
  /// Iterator category; supports single-pass input traversal.
  using iterator_category = std::input_iterator_tag;
  /// Return the predecessor node at the current position.
  ///
  /// \return The predecessor DGNode at the current iterator position.
  LLVM_ABI value_type operator*();
  /// Advance to the next predecessor and return this iterator.
  ///
  /// \return A reference to this iterator after advancement.
  LLVM_ABI PredIterator &operator++();
  /// Advance to the next predecessor and return the previous iterator.
  ///
  /// \param Unused Unused postfix-discriminator parameter.
  /// \return A copy of the iterator before advancement.
  PredIterator operator++(int Unused) {
    (void)Unused;
    auto Copy = *this;
    ++(*this);
    return Copy;
  }
  /// Return true if this iterator equals \p Other.
  ///
  /// \param Other Iterator to compare against.
  /// \return True if both iterators refer to the same position.
  LLVM_ABI bool operator==(const PredIterator &Other) const;
  /// Return true if this iterator differs from \p Other.
  ///
  /// \param Other Iterator to compare against.
  /// \return True if the iterators refer to different positions.
  bool operator!=(const PredIterator &Other) const { return !(*this == Other); }
};

/// Iterate over both def-use and mem dependencies.
class SuccIterator {
  User::user_iterator UserIt;
  User::user_iterator UserItE;
  DenseSet<MemDGNode *>::iterator MemIt;
  DGNode *N = nullptr;
  DependencyGraph *DAG = nullptr;

  SuccIterator(const Value::user_iterator &UserIt,
               const Value::user_iterator &UserItE,
               const DenseSet<MemDGNode *>::iterator &MemIt, DGNode *N,
               DependencyGraph &DAG)
      : UserIt(UserIt), UserItE(UserItE), MemIt(MemIt), N(N), DAG(&DAG) {}
  SuccIterator(const User::user_iterator &UserIt,
               const User::user_iterator &UserItE, DGNode *N,
               DependencyGraph &DAG)
      : UserIt(UserIt), UserItE(UserItE), N(N), DAG(&DAG) {}
  friend class DGNode;    // For constructor
  friend class MemDGNode; // For constructor

  /// Skip iterators that don't point to instructions or are outside \p DAG,
  /// starting from \p OpIt and ending before \p OpItE.
  LLVM_ABI static User::user_iterator
  skipOutOfScope(User::user_iterator UserIt, User::user_iterator UserItE,
                 const DependencyGraph &DAG);

public:
  /// Signed type used to express distances between iterators.
  using difference_type = std::ptrdiff_t;
  /// Element type produced when the iterator is dereferenced.
  using value_type = DGNode *;
  /// Pointer type equivalent to the iterator's value type.
  using pointer = value_type *;
  /// Reference type returned by dereferencing the iterator.
  using reference = value_type &;
  /// Iterator category; supports single-pass input traversal.
  using iterator_category = std::input_iterator_tag;
  /// Return the successor node at the current position.
  ///
  /// \return The successor DGNode at the current iterator position.
  LLVM_ABI value_type operator*();
  /// Advance to the next successor and return this iterator.
  ///
  /// \return A reference to this iterator after advancement.
  LLVM_ABI SuccIterator &operator++();
  /// Advance to the next successor and return the previous iterator.
  ///
  /// \param Unused Unused postfix-discriminator parameter.
  /// \return A copy of the iterator before advancement.
  SuccIterator operator++(int Unused) {
    (void)Unused;
    auto Copy = *this;
    ++(*this);
    return Copy;
  }
  /// Return true if this iterator equals \p Other.
  ///
  /// \param Other Iterator to compare against.
  /// \return True if both iterators refer to the same position.
  LLVM_ABI bool operator==(const SuccIterator &Other) const;
  /// Return true if this iterator differs from \p Other.
  ///
  /// \param Other Iterator to compare against.
  /// \return True if the iterators refer to different positions.
  bool operator!=(const SuccIterator &Other) const { return !(*this == Other); }
};

/// A DependencyGraph Node that points to an Instruction and contains memory
/// dependency edges.
class LLVM_ABI DGNode {
protected:
  /// Instruction associated with this dependency-graph node.
  Instruction *I;
  // TODO: Use a PointerIntPair for SubclassID and I.
  /// For isa/dyn_cast etc.
  DGNodeID SubclassID;
  /// Count of unscheduled successors or predecessors for scheduling.
  ///
  /// The counted side depends on the scheduling direction. Optional represents
  /// whether the value is meaningless, e.g., after a node gets scheduled.
  std::optional<unsigned> UnscheduledDeps = 0;
  /// This is true if this node has been scheduled.
  bool Scheduled = false;
  /// The scheduler bundle that this node belongs to.
  SchedBundle *SB = nullptr;

  /// Record that this node belongs to scheduling bundle \p SB.
  ///
  /// \param SB Scheduling bundle that should own this node.
  void setSchedBundle(SchedBundle &SB);
  /// Clear the scheduling bundle association for this node.
  void clearSchedBundle() { this->SB = nullptr; }
  friend class SchedBundle; // For setSchedBundle(), clearSchedBundle().

  /// Construct a node for instruction \p I with subclass id \p ID.
  ///
  /// \param I Instruction associated with this node.
  /// \param ID Subclass identifier for isa/dyn_cast.
  DGNode(Instruction *I, DGNodeID ID) : I(I), SubclassID(ID) {}
  friend class MemDGNode;       // For constructor.
  friend class DependencyGraph; // For UnscheduledSuccs

public:
  /// Construct a non-memory dependency node for instruction \p I.
  ///
  /// \param I Non-memory instruction associated with this node.
  DGNode(Instruction *I) : I(I), SubclassID(DGNodeID::DGNode) {
    assert(!isMemDepNodeCandidate(I) && "Expected Non-Mem instruction, ");
  }
  /// Copy construction is deleted; nodes are not copyable.
  DGNode(const DGNode &Other) = delete;
  /// Destroy this dependency-graph node.
  virtual ~DGNode();
  /// Return the number of unscheduled successors.
  ///
  /// \return The count of unscheduled dependent nodes for this node.
  unsigned getNumUnscheduledDeps() const {
    assert((bool)UnscheduledDeps && "Invalid UnscheduledDeps!");
    return *UnscheduledDeps;
  }
#ifndef NDEBUG
  /// Return true if the unscheduled-deps counter holds valid data.
  ///
  /// Unscheduled successors (or predecessors) may be invalid after scheduling;
  /// used for testing.
  /// \return True if UnscheduledDeps currently holds a meaningful value.
  bool validUnscheduledDeps() const { return (bool)UnscheduledDeps; }
#endif
  // TODO: Make this private?
  /// Decrement the unscheduled-deps counter by one.
  void decrUnscheduledDeps() {
    assert(*UnscheduledDeps > 0 && "Counting error!");
    --*UnscheduledDeps;
  }
  /// Increment the unscheduled-deps counter by one.
  void incrUnscheduledDeps() { ++*UnscheduledDeps; }

  /// Reset unscheduled-deps and scheduled state for a fresh schedule.
  void resetScheduleState() {
    UnscheduledDeps = 0;
    Scheduled = false;
  }
  /// Return true if all dependent successors have been scheduled.
  ///
  /// During top-down scheduling this checks predecessors instead.
  /// \return True if there are no remaining unscheduled dependencies.
  bool ready() const { return UnscheduledDeps == 0; }
  /// Return true if this node has been scheduled.
  ///
  /// \return True if this node has already been scheduled.
  bool scheduled() const { return Scheduled; }
  /// Mark this node as scheduled and invalidate UnscheduledDeps.
  void setScheduled() {
    Scheduled = true;
    // UnscheduledDeps is meaningless from this point on, so prohibit its use.
    UnscheduledDeps = std::nullopt;
  }
  /// Return the scheduling bundle that this node belongs to, or nullptr.
  ///
  /// \return The SchedBundle for this node, or nullptr if none.
  SchedBundle *getSchedBundle() const { return SB; }
  /// Return true if this node comes before \p Other in program order.
  ///
  /// \param Other Node to compare against in program order.
  /// \return True if this node's instruction precedes \p Other's.
  bool comesBefore(const DGNode *Other) { return I->comesBefore(Other->I); }
  /// Iterator type over predecessor dependency-graph nodes.
  using iterator = PredIterator;
  /// Return an iterator to the first predecessor in \p DAG.
  ///
  /// \param DAG Dependency graph that owns the predecessor edges.
  /// \return An iterator to the first predecessor of this node.
  virtual iterator preds_begin(DependencyGraph &DAG) {
    return PredIterator(
        PredIterator::skipBadIt(I->op_begin(), I->op_end(), DAG), I->op_end(),
        this, DAG);
  }
  /// Return an iterator past the last predecessor in \p DAG.
  ///
  /// \param DAG Dependency graph that owns the predecessor edges.
  /// \return An end iterator for the predecessor range.
  virtual iterator preds_end(DependencyGraph &DAG) {
    return PredIterator(I->op_end(), I->op_end(), this, DAG);
  }
  /// Return an iterator to the first predecessor in \p DAG.
  ///
  /// \param DAG Dependency graph that owns the predecessor edges.
  /// \return An iterator to the first predecessor of this node.
  iterator preds_begin(DependencyGraph &DAG) const {
    return const_cast<DGNode *>(this)->preds_begin(DAG);
  }
  /// Return an iterator past the last predecessor in \p DAG.
  ///
  /// \param DAG Dependency graph that owns the predecessor edges.
  /// \return An end iterator for the predecessor range.
  iterator preds_end(DependencyGraph &DAG) const {
    return const_cast<DGNode *>(this)->preds_end(DAG);
  }
  /// Return a range of DAG predecessor nodes.
  ///
  /// If this is a MemDGNode then this will also include the memory dependency
  /// predecessors. Please note that this can include the same node more than
  /// once, if for example it's both a use-def predecessor and a mem dep
  /// predecessor.
  /// \param DAG Dependency graph that owns the predecessor edges.
  /// \return An iterator range over this node's predecessors in \p DAG.
  iterator_range<iterator> preds(DependencyGraph &DAG) const {
    return make_range(preds_begin(DAG), preds_end(DAG));
  }

  /// Iterator type over successor dependency-graph nodes.
  using succ_iterator = SuccIterator;
  /// Return an iterator to the first successor in \p DAG.
  ///
  /// \param DAG Dependency graph that owns the successor edges.
  /// \return An iterator to the first successor of this node.
  virtual succ_iterator succs_begin(DependencyGraph &DAG) {
    return SuccIterator(
        SuccIterator::skipOutOfScope(I->user_begin(), I->user_end(), DAG),
        I->user_end(), this, DAG);
  }
  /// Return an iterator past the last successor in \p DAG.
  ///
  /// \param DAG Dependency graph that owns the successor edges.
  /// \return An end iterator for the successor range.
  virtual succ_iterator succs_end(DependencyGraph &DAG) {
    return SuccIterator(I->user_end(), I->user_end(), this, DAG);
  }
  /// Return an iterator to the first successor in \p DAG.
  ///
  /// \param DAG Dependency graph that owns the successor edges.
  /// \return An iterator to the first successor of this node.
  succ_iterator succs_begin(DependencyGraph &DAG) const {
    return const_cast<DGNode *>(this)->succs_begin(DAG);
  }
  /// Return an iterator past the last successor in \p DAG.
  ///
  /// \param DAG Dependency graph that owns the successor edges.
  /// \return An end iterator for the successor range.
  succ_iterator succs_end(DependencyGraph &DAG) const {
    return const_cast<DGNode *>(this)->succs_end(DAG);
  }
  /// Return a range of DAG successor nodes.
  ///
  /// If this is a MemDGNode then this will also include the memory dependency
  /// successors. Please note that this can include the same node more than
  /// once, if for example it's both a use-def predecessor and a mem dep
  /// successor.
  /// \param DAG Dependency graph that owns the successor edges.
  /// \return An iterator range over this node's successors in \p DAG.
  iterator_range<succ_iterator> succs(DependencyGraph &DAG) const {
    return make_range(succs_begin(DAG), succs_end(DAG));
  }

  /// Return true if \p I is a stacksave or stackrestore intrinsic.
  ///
  /// \param I Instruction to query.
  /// \return True if \p I is stacksave or stackrestore.
  static bool isStackSaveOrRestoreIntrinsic(Instruction *I) {
    if (auto *II = dyn_cast<IntrinsicInst>(I)) {
      auto IID = II->getIntrinsicID();
      return IID == Intrinsic::stackrestore || IID == Intrinsic::stacksave;
    }
    return false;
  }

  /// Return true if intrinsic \p I touches memory.
  ///
  /// This is used by the dependency graph.
  /// \param I Intrinsic to query.
  /// \return True if \p I is treated as a memory-touching intrinsic.
  static bool isMemIntrinsic(IntrinsicInst *I) {
    auto IID = I->getIntrinsicID();
    return IID != Intrinsic::sideeffect && IID != Intrinsic::pseudoprobe;
  }

  /// Return true if \p I is a memory dependency candidate.
  ///
  /// We consider \p I as a Memory Dependency Candidate instruction if it
  /// reads/write memory or if it has side-effects. This is used by the
  /// dependency graph.
  /// \param I Instruction to query.
  /// \return True if \p I may participate in a memory dependency.
  static bool isMemDepCandidate(Instruction *I) {
    IntrinsicInst *II;
    return I->mayReadOrWriteMemory() &&
           (!(II = dyn_cast<IntrinsicInst>(I)) || isMemIntrinsic(II));
  }

  /// Return true if \p I is fence-like, excluding non-mem intrinsics.
  ///
  /// \param I Instruction to query.
  /// \return True if \p I is fence-like for dependency-graph purposes.
  static bool isFenceLike(Instruction *I) {
    IntrinsicInst *II;
    return I->isFenceLike() &&
           (!(II = dyn_cast<IntrinsicInst>(I)) || isMemIntrinsic(II));
  }

  /// Return true if \p I should be represented as a MemDGNode.
  ///
  /// \param I Instruction to query.
  /// \return True if \p I should use a MemDGNode in the graph.
  static bool isMemDepNodeCandidate(Instruction *I) {
    AllocaInst *Alloca;
    return isMemDepCandidate(I) ||
           ((Alloca = dyn_cast<AllocaInst>(I)) &&
            Alloca->isUsedWithInAlloca()) ||
           isStackSaveOrRestoreIntrinsic(I) || isFenceLike(I);
  }

  /// Return the instruction associated with this node.
  ///
  /// \return The Sandbox IR instruction this node represents.
  Instruction *getInstruction() const { return I; }

#ifndef NDEBUG
  /// Print this node to \p OS, optionally including dependency edges.
  ///
  /// \param OS Destination stream.
  /// \param PrintDeps If true, also print dependency information.
  virtual void print(raw_ostream &OS, bool PrintDeps = true) const;
  /// Write a textual representation of \p N to \p OS.
  ///
  /// \param OS Destination stream.
  /// \param N Node to print.
  /// \return The output stream \p OS after printing.
  friend raw_ostream &operator<<(raw_ostream &OS, DGNode &N) {
    N.print(OS);
    return OS;
  }
  /// Dump this node to the debug stream.
  LLVM_DUMP_METHOD void dump() const;
#endif // NDEBUG
};

/// A DependencyGraph Node for instructions that may read/write memory, or have
/// some ordering constraints, like with stacksave/stackrestore and
/// alloca/inalloca.
class MemDGNode final : public DGNode {
  MemDGNode *PrevMemN = nullptr;
  MemDGNode *NextMemN = nullptr;
  /// Memory predecessors.
  DenseSet<MemDGNode *> MemPreds;
  /// Memory successors.
  DenseSet<MemDGNode *> MemSuccs;
  friend class PredIterator; // For MemPreds.
  friend class SuccIterator; // For MemSuccs.
  /// Creates both edges: this<->N.
  void setNextNode(MemDGNode *N) {
    assert(N != this && "About to point to self!");
    NextMemN = N;
    if (NextMemN != nullptr)
      NextMemN->PrevMemN = this;
  }
  /// Creates both edges: N<->this.
  void setPrevNode(MemDGNode *N) {
    assert(N != this && "About to point to self!");
    PrevMemN = N;
    if (PrevMemN != nullptr)
      PrevMemN->NextMemN = this;
  }
  friend class DependencyGraph; // For setNextNode(), setPrevNode().
  void detachFromChain() {
    if (PrevMemN != nullptr)
      PrevMemN->NextMemN = NextMemN;
    if (NextMemN != nullptr)
      NextMemN->PrevMemN = PrevMemN;
    PrevMemN = nullptr;
    NextMemN = nullptr;
  }

public:
  /// Construct a memory dependency node for instruction \p I.
  ///
  /// \param I Memory or ordering-constrained instruction for this node.
  MemDGNode(Instruction *I) : DGNode(I, DGNodeID::MemDGNode) {
    assert(isMemDepNodeCandidate(I) && "Expected Mem instruction!");
  }
  /// Return true if \p Other is a MemDGNode.
  ///
  /// \param Other Node to test for the MemDGNode subclass.
  /// \return True if \p Other is a MemDGNode.
  static bool classof(const DGNode *Other) {
    return Other->SubclassID == DGNodeID::MemDGNode;
  }
  /// Return an iterator to the first predecessor in \p DAG.
  ///
  /// \param DAG Dependency graph that owns the predecessor edges.
  /// \return An iterator to the first predecessor of this node.
  iterator preds_begin(DependencyGraph &DAG) override {
    auto OpEndIt = I->op_end();
    return PredIterator(PredIterator::skipBadIt(I->op_begin(), OpEndIt, DAG),
                        OpEndIt, MemPreds.begin(), this, DAG);
  }
  /// Return an iterator past the last predecessor in \p DAG.
  ///
  /// \param DAG Dependency graph that owns the predecessor edges.
  /// \return An end iterator for the predecessor range.
  iterator preds_end(DependencyGraph &DAG) override {
    return PredIterator(I->op_end(), I->op_end(), MemPreds.end(), this, DAG);
  }
  /// Return an iterator to the first successor in \p DAG.
  ///
  /// \param DAG Dependency graph that owns the successor edges.
  /// \return An iterator to the first successor of this node.
  succ_iterator succs_begin(DependencyGraph &DAG) override {
    auto UserEndIt = I->user_end();
    return SuccIterator(
        SuccIterator::skipOutOfScope(I->user_begin(), UserEndIt, DAG),
        UserEndIt, MemSuccs.begin(), this, DAG);
  }
  /// Return an iterator past the last successor in \p DAG.
  ///
  /// \param DAG Dependency graph that owns the successor edges.
  /// \return An end iterator for the successor range.
  succ_iterator succs_end(DependencyGraph &DAG) override {
    return SuccIterator(I->user_end(), I->user_end(), MemSuccs.end(), this,
                        DAG);
  }
  /// Return the previous Mem DGNode in instruction order.
  ///
  /// \return The previous MemDGNode, or nullptr if none.
  MemDGNode *getPrevNode() const { return PrevMemN; }
  /// Return the next Mem DGNode in instruction order.
  ///
  /// \return The next MemDGNode, or nullptr if none.
  MemDGNode *getNextNode() const { return NextMemN; }

  // TODO: addMemPred() and removeMemPred() should be private.
  /// Add the mem dependency edge PredN->this.
  ///
  /// This also increments the UnscheduledDeps counter of the predecessor if
  /// this node has not been scheduled.
  /// \param PredN Memory predecessor to connect to this node.
  /// \param Dir Scheduling direction that controls which side's counter moves.
  void addMemPred(MemDGNode *PredN, SchedDirection Dir) {
    [[maybe_unused]] auto Inserted = MemPreds.insert(PredN).second;
    assert(Inserted && "PredN already exists!");
    assert(PredN != this && "Trying to add a dependency to self!");
    PredN->MemSuccs.insert(this);
    if (!Scheduled) {
      if (!PredN->Scheduled) {
        if (Dir == SchedDirection::BottomUp)
          PredN->incrUnscheduledDeps();
        else
          incrUnscheduledDeps();
      }
    }
  }
  /// Remove the memory dependency PredN->this.
  ///
  /// This also updates the UnscheduledSuccs counter of PredN if this node has
  /// not been scheduled.
  /// \param PredN Memory predecessor to disconnect from this node.
  /// \param Dir Scheduling direction that controls which side's counter moves.
  void removeMemPred(MemDGNode *PredN, SchedDirection Dir) {
    MemPreds.erase(PredN);
    PredN->MemSuccs.erase(this);
    if (!Scheduled) {
      if (!PredN->Scheduled) {
        if (Dir == SchedDirection::BottomUp)
          PredN->decrUnscheduledDeps();
        else
          decrUnscheduledDeps();
      }
    }
  }

  /// Return true if there is a memory dependency N->this.
  ///
  /// \param N Candidate predecessor node to test.
  /// \return True if \p N is a memory predecessor of this node.
  bool hasMemPred(DGNode *N) const {
    if (auto *MN = dyn_cast<MemDGNode>(N))
      return MemPreds.count(MN);
    return false;
  }
  /// Return all memory dependency predecessors.
  ///
  /// Used by tests.
  /// \return An iterator range over this node's memory predecessors.
  iterator_range<DenseSet<MemDGNode *>::const_iterator> memPreds() const {
    return make_range(MemPreds.begin(), MemPreds.end());
  }
  /// Return all memory dependency successors.
  ///
  /// \return An iterator range over this node's memory successors.
  iterator_range<DenseSet<MemDGNode *>::const_iterator> memSuccs() const {
    return make_range(MemSuccs.begin(), MemSuccs.end());
  }
#ifndef NDEBUG
  /// Print this memory node to \p OS, optionally including dependency edges.
  ///
  /// \param OS Destination stream.
  /// \param PrintDeps If true, also print dependency information.
  void print(raw_ostream &OS, bool PrintDeps = true) const override;
#endif // NDEBUG
};

/// Convenience builders for a MemDGNode interval.
class MemDGNodeIntervalBuilder {
public:
  /// Return the top-most MemDGNode in \p Intvl, or nullptr.
  ///
  /// Scans the instruction chain in \p Intvl top-down.
  /// \param Intvl Instruction interval to scan.
  /// \param DAG Dependency graph that maps instructions to nodes.
  /// \return The top-most MemDGNode in \p Intvl, or nullptr if none.
  LLVM_ABI static MemDGNode *getTopMemDGNode(const Interval<Instruction> &Intvl,
                                             const DependencyGraph &DAG);
  /// Return the bottom-most MemDGNode in \p Intvl, or nullptr.
  ///
  /// Scans the instruction chain in \p Intvl bottom-up.
  /// \param Intvl Instruction interval to scan.
  /// \param DAG Dependency graph that maps instructions to nodes.
  /// \return The bottom-most MemDGNode in \p Intvl, or nullptr if none.
  LLVM_ABI static MemDGNode *getBotMemDGNode(const Interval<Instruction> &Intvl,
                                             const DependencyGraph &DAG);
  /// Return the MemDGNode interval covering the closest mem nodes of \p Instrs.
  ///
  /// Given \p Instrs it finds their closest mem nodes in the interval and
  /// returns the corresponding mem range. Note: BotN (or its neighboring mem
  /// node) is included in the range.
  /// \param Instrs Instruction interval whose nearest mem nodes define the
  /// range.
  /// \param DAG Dependency graph that maps instructions to mem nodes.
  /// \return The MemDGNode interval covering the nearest mem nodes of \p Instrs.
  LLVM_ABI static Interval<MemDGNode> make(const Interval<Instruction> &Instrs,
                                           DependencyGraph &DAG);
  /// Return an empty MemDGNode interval.
  ///
  /// \return An empty Interval of MemDGNode.
  static Interval<MemDGNode> makeEmpty() { return {}; }
};

/// Dependency graph of Sandbox IR instructions used by the vectorizer scheduler.
class DependencyGraph {
private:
  DenseMap<Instruction *, std::unique_ptr<DGNode>> InstrToNodeMap;
  /// The DAG spans across all instructions in this interval.
  Interval<Instruction> DAGInterval;

  SchedDirection Dir;

  Context *Ctx = nullptr;
  std::optional<Context::CallbackID> CreateInstrCB;
  std::optional<Context::CallbackID> EraseInstrCB;
  std::optional<Context::CallbackID> MoveInstrCB;
  std::optional<Context::CallbackID> SetUseCB;

  std::unique_ptr<BatchAAResults> BatchAA;

  enum class DependencyType {
    ReadAfterWrite,  ///> Memory dependency write -> read
    WriteAfterWrite, ///> Memory dependency write -> write
    WriteAfterRead,  ///> Memory dependency read -> write
    Control,         ///> Control-related dependency, like with PHI/Terminator
    Other,           ///> Currently used for stack related instrs
    None,            ///> No memory/other dependency
  };
  /// \Returns the dependency type depending on whether instructions may
  /// read/write memory or whether they are some specific opcode-related
  /// restrictions.
  /// Note: It does not check whether a memory dependency is actually correct,
  /// as it won't call AA. Therefore it returns the worst-case dep type.
  static DependencyType getRoughDepType(Instruction *FromI, Instruction *ToI);

  // TODO: Implement AABudget.
  /// \Returns true if there is a memory/other dependency \p SrcI->DstI.
  bool alias(Instruction *SrcI, Instruction *DstI, DependencyType DepType);

  bool hasDep(sandboxir::Instruction *SrcI, sandboxir::Instruction *DstI);

  /// Go through all mem nodes in \p SrcScanRange and try to add dependencies to
  /// \p DstN.
  void scanAndAddDeps(MemDGNode &DstN, const Interval<MemDGNode> &SrcScanRange);

  /// Sets the UnscheduledSuccs of all DGNodes in \p NewInterval based on
  /// def-use edges.
  void setDefUseUnscheduledSuccs(const Interval<Instruction> &NewInterval);

  /// Create DAG nodes for instrs in \p NewInterval and update the MemNode
  /// chain.
  void createNewNodes(const Interval<Instruction> &NewInterval);

  /// Helper for `notify*Instr()`. \Returns the first MemDGNode that comes
  /// before \p N, skipping \p SkipN, including or excluding \p N based on
  /// \p IncludingN, or nullptr if not found.
  MemDGNode *getMemDGNodeBefore(DGNode *N, bool IncludingN,
                                MemDGNode *SkipN = nullptr) const;
  /// Helper for `notifyMoveInstr()`. \Returns the first MemDGNode that comes
  /// after \p N, skipping \p SkipN, including or excluding \p N based on \p
  /// IncludingN, or nullptr if not found.
  MemDGNode *getMemDGNodeAfter(DGNode *N, bool IncludingN,
                               MemDGNode *SkipN = nullptr) const;

  /// Called by the callbacks when a new instruction \p I has been created.
  LLVM_ABI void notifyCreateInstr(Instruction *I);
  /// Called by the callbacks when instruction \p I is about to get
  /// deleted.
  LLVM_ABI void notifyEraseInstr(Instruction *I);
  /// Called by the callbacks when instruction \p I is about to be moved to
  /// \p To.
  LLVM_ABI void notifyMoveInstr(Instruction *I, const BBIterator &To);
  /// Called by the callbacks when \p U's source is about to be set to \p NewSrc
  LLVM_ABI void notifySetUse(const Use &U, Value *NewSrc);

public:
  /// Construct a dependency graph and register Sandbox IR callbacks.
  ///
  /// \param Dir Scheduling direction used when maintaining unscheduled-deps.
  /// \param AA Alias analysis results used to build memory edges.
  /// \param Ctx Sandbox IR context whose callbacks track IR mutations.
  DependencyGraph(SchedDirection Dir, AAResults &AA, Context &Ctx)
      : Dir(Dir), Ctx(&Ctx), BatchAA(std::make_unique<BatchAAResults>(AA)) {
    CreateInstrCB = Ctx.registerCreateInstrCallback(
        [this](Instruction *I) { notifyCreateInstr(I); });
    EraseInstrCB = Ctx.registerEraseInstrCallback(
        [this](Instruction *I) { notifyEraseInstr(I); });
    MoveInstrCB = Ctx.registerMoveInstrCallback(
        [this](Instruction *I, const BBIterator &To) {
          notifyMoveInstr(I, To);
        });
    SetUseCB = Ctx.registerSetUseCallback(
        [this](const Use &U, Value *NewSrc) { notifySetUse(U, NewSrc); });
  }
  /// Destroy the graph and unregister Sandbox IR callbacks.
  ~DependencyGraph() {
    if (CreateInstrCB)
      Ctx->unregisterCreateInstrCallback(*CreateInstrCB);
    if (EraseInstrCB)
      Ctx->unregisterEraseInstrCallback(*EraseInstrCB);
    if (MoveInstrCB)
      Ctx->unregisterMoveInstrCallback(*MoveInstrCB);
    if (SetUseCB)
      Ctx->unregisterSetUseCallback(*SetUseCB);
  }

  /// Return the dependency node for instruction \p I, or nullptr if none.
  ///
  /// \param I Instruction whose node is queried.
  /// \return The DGNode for \p I, or nullptr if none exists.
  DGNode *getNode(Instruction *I) const {
    auto It = InstrToNodeMap.find(I);
    return It != InstrToNodeMap.end() ? It->second.get() : nullptr;
  }
  /// Return the dependency node for \p I, or nullptr if \p I is nullptr.
  ///
  /// \param I Instruction whose node is queried; may be nullptr.
  /// \return The DGNode for \p I, or nullptr if \p I is null or unmapped.
  DGNode *getNodeOrNull(Instruction *I) const {
    if (I == nullptr)
      return nullptr;
    return getNode(I);
  }
  /// Return the existing node for \p I, or create one if missing.
  ///
  /// \param I Instruction to look up or create a node for.
  /// \return The existing or newly created DGNode for \p I.
  DGNode *getOrCreateNode(Instruction *I) {
    auto [It, NotInMap] = InstrToNodeMap.try_emplace(I);
    if (NotInMap) {
      if (DGNode::isMemDepNodeCandidate(I))
        It->second = std::make_unique<MemDGNode>(I);
      else
        It->second = std::make_unique<DGNode>(I);
    }
    return It->second.get();
  }
  /// Build or extend the dependency graph to include \p Instrs.
  ///
  /// Returns the range of instructions added to the DAG.
  /// \param Instrs Instructions that must be covered by the graph.
  /// \return The interval of instructions added to the DAG.
  LLVM_ABI Interval<Instruction> extend(ArrayRef<Instruction *> Instrs);
  /// Return the range of instructions included in the DAG.
  ///
  /// \return The instruction interval currently covered by the DAG.
  Interval<Instruction> getInterval() const { return DAGInterval; }
  /// Clear all nodes and reset the DAG interval.
  void clear() {
    InstrToNodeMap.clear();
    DAGInterval = {};
  }
#ifndef NDEBUG
  /// Return true if the DAG's state is clear.
  ///
  /// Used in assertions.
  /// \return True if the instruction-to-node map and interval are empty.
  bool empty() const {
    bool IsEmpty = InstrToNodeMap.empty();
    assert(IsEmpty == DAGInterval.empty() &&
           "Interval and InstrToNodeMap out of sync!");
    return IsEmpty;
  }
  /// Print the dependency graph to \p OS.
  ///
  /// \param OS Destination stream.
  void print(raw_ostream &OS) const;
  /// Dump the dependency graph to the debug stream.
  LLVM_DUMP_METHOD void dump() const;
#endif // NDEBUG
};
} // namespace llvm::sandboxir

#endif // LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_DEPENDENCYGRAPH_H
