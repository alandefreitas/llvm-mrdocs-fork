//===- GVN.h - Eliminate redundant values and loads -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file provides the interface for LLVM's Global Value Numbering pass
/// which eliminates fully redundant instructions. It also does somewhat Ad-Hoc
/// PRE and dead load elimination.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_GVN_H
#define LLVM_TRANSFORMS_SCALAR_GVN_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/PHITransAddr.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace llvm {

class AAResults;
class AssumeInst;
class AssumptionCache;
class BasicBlock;
class BatchAAResults;
class CallInst;
class CondBrInst;
class EarliestEscapeAnalysis;
class ExtractValueInst;
class Function;
class FunctionPass;
/// Legacy FunctionPass wrapper around GVNPass.
class GVNLegacyPass;
class GetElementPtrInst;
class ImplicitControlFlowTracking;
class LoadInst;
class LoopInfo;
class MemDepResult;
class MemoryAccess;
class MemoryDependenceResults;
class MemoryLocation;
class MemorySSA;
class MemorySSAUpdater;
class NonLocalDepResult;
class OptimizationRemarkEmitter;
class PHINode;
class TargetLibraryInfo;
class Value;
class IntrinsicInst;

/// Parameters controlling which transforms GVN may perform.
///
/// Each of the optional boolean parameters can be set to:
///      true - enabling the transformation.
///      false - disabling the transformation.
///      None - relying on a global default.
/// Intended use is to create a default object, modify parameters with
/// additional setters and then pass it to GVN.
struct GVNOptions {
  /// Whether to allow PRE of scalar instructions; None uses the global default.
  std::optional<bool> AllowScalarPRE;
  /// Whether to allow PRE of loads; None uses the global default.
  std::optional<bool> AllowLoadPRE;
  /// Whether to allow load PRE inside loops; None uses the global default.
  std::optional<bool> AllowLoadInLoopPRE;
  /// Whether load PRE may split critical backedges; None uses the global
  /// default.
  std::optional<bool> AllowLoadPRESplitBackedge;
  /// Whether to use MemoryDependenceAnalysis; None uses the global default.
  std::optional<bool> AllowMemDep;
  /// Whether to use MemorySSA; None uses the global default.
  std::optional<bool> AllowMemorySSA;

  /// Construct GVN options with all settings left at their defaults.
  GVNOptions() = default;

  /// Enables or disables PRE of scalars in GVN.
  /// @param ScalarPRE True to enable scalar PRE, false to disable it.
  /// @return A reference to these options.
  GVNOptions &setScalarPRE(bool ScalarPRE) {
    AllowScalarPRE = ScalarPRE;
    return *this;
  }

  /// Enables or disables PRE of loads in GVN.
  /// @param LoadPRE True to enable load PRE, false to disable it.
  /// @return A reference to these options.
  GVNOptions &setLoadPRE(bool LoadPRE) {
    AllowLoadPRE = LoadPRE;
    return *this;
  }

  /// Enables or disables PRE of loads inside loops in GVN.
  /// @param LoadInLoopPRE True to enable in-loop load PRE, false to disable
  /// it.
  /// @return A reference to these options.
  GVNOptions &setLoadInLoopPRE(bool LoadInLoopPRE) {
    AllowLoadInLoopPRE = LoadInLoopPRE;
    return *this;
  }

  /// Enables or disables PRE of loads in GVN.
  /// @param LoadPRESplitBackedge True to allow splitting critical backedges
  /// during load PRE, false to disable it.
  /// @return A reference to these options.
  GVNOptions &setLoadPRESplitBackedge(bool LoadPRESplitBackedge) {
    AllowLoadPRESplitBackedge = LoadPRESplitBackedge;
    return *this;
  }

  /// Enables or disables use of MemDepAnalysis.
  /// @param MemDep True to use MemoryDependenceAnalysis, false to disable it.
  /// @return A reference to these options.
  GVNOptions &setMemDep(bool MemDep) {
    AllowMemDep = MemDep;
    return *this;
  }

  /// Enables or disables use of MemorySSA.
  /// @param MemSSA True to use MemorySSA, false to disable it.
  /// @return A reference to these options.
  GVNOptions &setMemorySSA(bool MemSSA) {
    AllowMemorySSA = MemSSA;
    return *this;
  }
};

/// The core GVN pass object.
///
/// FIXME: We should have a good summary of the GVN algorithm implemented by
/// this particular pass here.
class GVNPass : public OptionalPassInfoMixin<GVNPass> {
  GVNOptions Options;

public:
  /// Expression key used for value numbering.
  struct Expression;
  /// A value available for rematerializing a load.
  struct AvailableValue;
  /// An AvailableValue associated with a specific basic block.
  struct AvailableValueInBlock;

  /// Construct a GVN pass with the given options.
  /// @param Options Configuration controlling which GVN transforms run.
  GVNPass(GVNOptions Options = {}) : Options(Options) {}

  /// Run the pass over the function.
  /// @param F Function to run global value numbering on.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  /// Print this pass's pipeline representation to \p OS.
  /// @param OS Stream to write the pipeline string to.
  /// @param MapClassName2PassName Maps class names to pass names.
  LLVM_ABI void
  printPipeline(raw_ostream &OS,
                function_ref<StringRef(StringRef)> MapClassName2PassName);

  /// This removes the specified instruction from
  /// our various maps and marks it for deletion.
  /// @param I Instruction to salvage debug info from and remove.
  LLVM_ABI void salvageAndRemoveInstruction(Instruction *I);

  /// Return the dominator tree used by this pass.
  /// @return The dominator tree used by this pass.
  DominatorTree &getDominatorTree() const { return *DT; }
  /// Return the alias analysis results used by this pass.
  /// @return The alias analysis results used by this pass.
  AAResults *getAliasAnalysis() const { return VN.getAliasAnalysis(); }
  /// Return the memory dependence analysis used by this pass.
  /// @return The memory dependence analysis used by this pass.
  MemoryDependenceResults &getMemDep() const { return *MD; }

  /// Return true if PRE of scalar instructions is enabled.
  /// @return True if PRE of scalar instructions is enabled.
  LLVM_ABI bool isScalarPREEnabled() const;
  /// Return true if PRE of loads is enabled.
  /// @return True if PRE of loads is enabled.
  LLVM_ABI bool isLoadPREEnabled() const;
  /// Return true if PRE of loads inside loops is enabled.
  /// @return True if PRE of loads inside loops is enabled.
  LLVM_ABI bool isLoadInLoopPREEnabled() const;
  /// Return true if load PRE may split critical backedges.
  /// @return True if load PRE may split critical backedges.
  LLVM_ABI bool isLoadPRESplitBackedgeEnabled() const;
  /// Return true if MemoryDependenceAnalysis is enabled for this pass.
  /// @return True if MemoryDependenceAnalysis is enabled for this pass.
  LLVM_ABI bool isMemDepEnabled() const;
  /// Return true if MemorySSA is enabled for this pass.
  /// @return True if MemorySSA is enabled for this pass.
  LLVM_ABI bool isMemorySSAEnabled() const;

  /// This class holds the mapping between values and value numbers.  It is used
  /// as an efficient mechanism to determine the expression-wise equivalence of
  /// two values.
  class ValueTable {
    DenseMap<Value *, uint32_t> ValueNumbering;
    DenseMap<Expression, uint32_t> ExpressionNumbering;

    // Expressions is the vector of Expression. ExprIdx is the mapping from
    // value number to the index of Expression in Expressions. We use it
    // instead of a DenseMap because filling such mapping is faster than
    // filling a DenseMap and the compile time is a little better.
    uint32_t NextExprNumber = 0;

    std::vector<Expression> Expressions;
    std::vector<uint32_t> ExprIdx;

    // Value number to PHINode mapping. Used for phi-translate in scalarpre.
    DenseMap<uint32_t, PHINode *> NumberingPhi;

    // Value number to BasicBlock mapping. Used for phi-translate across
    // MemoryPhis.
    DenseMap<uint32_t, BasicBlock *> NumberingBB;

    // Cache for phi-translate in scalarpre.
    using PhiTranslateMap =
        DenseMap<std::pair<uint32_t, const BasicBlock *>, uint32_t>;
    PhiTranslateMap PhiTranslateTable;

    AAResults *AA = nullptr;
    MemoryDependenceResults *MD = nullptr;
    bool IsMDEnabled = false;
    MemorySSA *MSSA = nullptr;
    bool IsMSSAEnabled = false;
    DominatorTree *DT = nullptr;

    uint32_t NextValueNumber = 1;

    Expression createExpr(Instruction *I);
    Expression createCmpExpr(unsigned Opcode, CmpInst::Predicate Predicate,
                             Value *LHS, Value *RHS);
    Expression createExtractvalueExpr(ExtractValueInst *EI);
    Expression createGEPExpr(GetElementPtrInst *GEP);
    uint32_t lookupOrAddCall(CallInst *C);
    uint32_t computeLoadStoreVN(Instruction *I);
    uint32_t phiTranslateImpl(const BasicBlock *BB, const BasicBlock *PhiBlock,
                              uint32_t Num, GVNPass &GVN);
    bool areCallValsEqual(uint32_t Num, uint32_t NewNum, const BasicBlock *Pred,
                          const BasicBlock *PhiBlock, GVNPass &GVN);
    std::pair<uint32_t, bool> assignExpNewValueNum(Expression &Exp);
    bool areAllValsInBB(uint32_t Num, const BasicBlock *BB, GVNPass &GVN);
    void addMemoryStateToExp(Instruction *I, Expression &Exp);

  public:
    /// Construct an empty value table.
    LLVM_ABI ValueTable();
    /// Copy-construct a value table.
    /// @param Arg Value table to copy.
    LLVM_ABI ValueTable(const ValueTable &Arg);
    /// Move-construct a value table.
    /// @param Arg Value table to move from.
    LLVM_ABI ValueTable(ValueTable &&Arg);
    /// Destroy the value table.
    LLVM_ABI ~ValueTable();
    /// Copy-assign a value table.
    /// @param Arg Value table to copy.
    /// @return A reference to this value table.
    LLVM_ABI ValueTable &operator=(const ValueTable &Arg);

    /// Return the value number for \p MA, assigning a new one if needed.
    /// @param MA Memory access whose value number is requested.
    /// @return The value number for \p MA.
    LLVM_ABI uint32_t lookupOrAdd(MemoryAccess *MA);
    /// Return the value number for \p V, assigning a new one if needed.
    /// @param V Value whose value number is requested.
    /// @return The value number for \p V.
    LLVM_ABI uint32_t lookupOrAdd(Value *V);
    /// Return the existing value number for \p V.
    /// @param V Value whose value number is requested.
    /// @param Verify If true, assert that \p V has already been numbered.
    /// @return The value number for \p V, or 0 if missing and \p Verify is
    /// false.
    LLVM_ABI uint32_t lookup(Value *V, bool Verify = true) const;
    /// Return the value number for a comparison, assigning a new one if needed.
    /// @param Opcode Comparison opcode (ICmp or FCmp).
    /// @param Pred Comparison predicate.
    /// @param LHS Left-hand operand of the comparison.
    /// @param RHS Right-hand operand of the comparison.
    /// @return The value number for the comparison expression.
    LLVM_ABI uint32_t lookupOrAddCmp(unsigned Opcode, CmpInst::Predicate Pred,
                                     Value *LHS, Value *RHS);
    /// Return the value number of a ptrtoint of \p Ptr to \p Ty.
    /// @param Ptr Pointer value being converted.
    /// @param Ty Integer type of the ptrtoint expression.
    /// @return The value number for the ptrtoint expression, or 0 if absent.
    LLVM_ABI uint32_t lookupPtrToInt(Value *Ptr, Type *Ty);
    /// Translate value number \p Num through phis from \p PhiBlock into \p BB.
    /// @param BB Predecessor block whose incoming values are used.
    /// @param PhiBlock Block containing the phi(s) to translate across.
    /// @param Num Value number to translate.
    /// @param GVN Parent GVN pass providing leader information.
    /// @return The phi-translated value number.
    LLVM_ABI uint32_t phiTranslate(const BasicBlock *BB,
                                   const BasicBlock *PhiBlock, uint32_t Num,
                                   GVNPass &GVN);
    /// Erase stale phi-translate cache entries for \p Num at \p CurrBlock.
    /// @param Num Value number whose cache entries should be invalidated.
    /// @param CurrBlock Block whose predecessor cache entries are erased.
    LLVM_ABI void eraseTranslateCacheEntry(uint32_t Num,
                                           const BasicBlock &CurrBlock);
    /// Return true if \p V already has a value number.
    /// @param V Value to query.
    /// @return True if \p V is present in the value table.
    LLVM_ABI bool exists(Value *V) const;
    /// Insert \p V into the table with value number \p Num.
    /// @param V Value to insert.
    /// @param Num Value number to assign to \p V.
    LLVM_ABI void add(Value *V, uint32_t Num);
    /// Remove all entries from the value table.
    LLVM_ABI void clear();
    /// Remove \p V from the value numbering maps.
    /// @param V Value to erase.
    LLVM_ABI void erase(Value *V);
    /// Set the alias analysis used by the value table.
    /// @param A Alias analysis results to use, or nullptr.
    void setAliasAnalysis(AAResults *A) { AA = A; }
    /// Return the alias analysis used by the value table.
    /// @return The alias analysis used by the value table, or nullptr.
    AAResults *getAliasAnalysis() const { return AA; }
    /// Set the memory dependence analysis used by the value table.
    /// @param M Memory dependence results to use, or nullptr.
    /// @param MDEnabled Whether memory dependence queries are enabled.
    void setMemDep(MemoryDependenceResults *M, bool MDEnabled = true) {
      MD = M;
      IsMDEnabled = MDEnabled;
    }
    /// Set the MemorySSA instance used by the value table.
    /// @param M MemorySSA to use, or nullptr.
    /// @param MSSAEnabled Whether MemorySSA-based numbering is enabled.
    void setMemorySSA(MemorySSA *M, bool MSSAEnabled = false) {
      MSSA = M;
      IsMSSAEnabled = MSSAEnabled;
    }
    /// Set the dominator tree used by the value table.
    /// @param D Dominator tree to use, or nullptr.
    void setDomTree(DominatorTree *D) { DT = D; }
    /// Return the next unused value number.
    /// @return The next unused value number.
    uint32_t getNextUnusedValueNumber() { return NextValueNumber; }
    /// Verify that \p V is absent from all value-numbering maps.
    /// @param V Value expected to have been removed.
    LLVM_ABI void verifyRemoved(const Value *V) const;
  };

private:
  friend class GVNLegacyPass;
  friend struct DenseMapInfo<Expression>;

  MemoryDependenceResults *MD = nullptr;
  DominatorTree *DT = nullptr;
  const TargetLibraryInfo *TLI = nullptr;
  AssumptionCache *AC = nullptr;
  SetVector<BasicBlock *> DeadBlocks;
  OptimizationRemarkEmitter *ORE = nullptr;
  ImplicitControlFlowTracking *ICF = nullptr;
  LoopInfo *LI = nullptr;
  AAResults *AA = nullptr;
  MemorySSAUpdater *MSSAU = nullptr;

  ValueTable VN;

  /// A mapping from value numbers to lists of Value*'s that
  /// have that value number.  Use findLeader to query it.
  class LeaderMap {
  public:
    struct LeaderTableEntry {
      // Use AssertingVH here to catch dangling Value*'s in the leader table.
      // Will crash if the value gets deleted before the AssertingVH is
      // destroyed.
      AssertingVH<Value> Val;
      const BasicBlock *BB;
      LeaderTableEntry(Value *V, const BasicBlock *BB) : Val(V), BB(BB) {}
    };

  private:
    struct LeaderListNode {
      LeaderTableEntry Entry;
      LeaderListNode *Next;
      LeaderListNode(Value *V, const BasicBlock *BB, LeaderListNode *Next)
          : Entry(V, BB), Next(Next) {}
    };
    DenseMap<uint32_t, LeaderListNode> NumToLeaders;
    BumpPtrAllocator TableAllocator;

  public:
    class leader_iterator {
      const LeaderListNode *Current;

    public:
      using iterator_category = std::forward_iterator_tag;
      using value_type = const LeaderTableEntry;
      using difference_type = std::ptrdiff_t;
      using pointer = value_type *;
      using reference = value_type &;

      leader_iterator(const LeaderListNode *C) : Current(C) {}
      leader_iterator &operator++() {
        assert(Current && "Dereferenced end of leader list!");
        Current = Current->Next;
        return *this;
      }
      bool operator==(const leader_iterator &Other) const {
        return Current == Other.Current;
      }
      bool operator!=(const leader_iterator &Other) const {
        return Current != Other.Current;
      }
      reference operator*() const { return Current->Entry; }
    };

    iterator_range<leader_iterator> getLeaders(uint32_t N) {
      auto I = NumToLeaders.find(N);
      if (I == NumToLeaders.end()) {
        return iterator_range(leader_iterator(nullptr),
                              leader_iterator(nullptr));
      }

      return iterator_range(leader_iterator(&I->second),
                            leader_iterator(nullptr));
    }

    LLVM_ABI void insert(uint32_t N, Value *V, const BasicBlock *BB);
    LLVM_ABI void erase(uint32_t N, Instruction *I, const BasicBlock *BB);
    void clear() {
      // Manually destroy non-head nodes (in BumpPtrAllocator) to properly
      // clean up AssertingVH handles before Reset(). Head nodes are destroyed
      // by NumToLeaders.clear() below.
      for (auto &[_, HeadNode] : NumToLeaders) {
        LeaderListNode *N = HeadNode.Next;
        while (N) {
          auto *Next = N->Next;
          N->~LeaderListNode();
          N = Next;
        }
      }
      NumToLeaders.clear();
      TableAllocator.Reset();
    }
  };
  LeaderMap LeaderTable;

  // Map the block to reversed postorder traversal number. It is used to
  // find back edge easily.
  DenseMap<AssertingVH<BasicBlock>, uint32_t> BlockRPONumber;

  // This is set 'true' initially and also when new blocks have been added to
  // the function being analyzed. This boolean is used to control the updating
  // of BlockRPONumber prior to accessing the contents of BlockRPONumber.
  bool InvalidBlockRPONumbers = true;

  using LoadDepVect = SmallVector<NonLocalDepResult, 64>;
  using AvailValInBlkVect = SmallVector<AvailableValueInBlock, 64>;
  using UnavailBlkVect = SmallVector<BasicBlock *, 64>;

  bool runImpl(Function &F, AssumptionCache &RunAC, DominatorTree &RunDT,
               const TargetLibraryInfo &RunTLI, AAResults &RunAA,
               MemoryDependenceResults *RunMD, LoopInfo &LI,
               OptimizationRemarkEmitter *ORE, MemorySSA *MSSA = nullptr);

  // List of critical edges to be split between iterations.
  SmallVector<std::pair<Instruction *, unsigned>, 4> ToSplit;

  enum class DepKind {
    Other = 0, // Unknown value.
    Def,       // Exactly overlapping locations.
    Clobber,   // Reaching value superset of needed bits.
    Select,    // Reaching value is a select of two reaching addresses.
  };

  // Describe a memory location value, such that there exists a path to a point
  // in the program, along which that memory location is not modified.
  struct ReachingMemVal {
    DepKind Kind;
    BasicBlock *Block;
    const Value *Addr;
    Instruction *Inst;
    int32_t Offset;
    // For DepKind::Select only: the condition and the two addresses referenced
    // by the "true" and "false" side of the select-dependent load.
    const Value *SelCond = nullptr;
    const Value *SelTrueAddr = nullptr;
    const Value *SelFalseAddr = nullptr;

    static ReachingMemVal getUnknown(BasicBlock *BB, const Value *Addr,
                                     Instruction *Inst = nullptr) {
      return {DepKind::Other, BB, Addr, Inst, -1};
    }

    static ReachingMemVal getDef(const Value *Addr, Instruction *Inst) {
      return {DepKind::Def, Inst->getParent(), Addr, Inst, -1};
    }

    static ReachingMemVal getClobber(const Value *Addr, Instruction *Inst,
                                     int32_t Offset = -1) {
      return {DepKind::Clobber, Inst->getParent(), Addr, Inst, Offset};
    }

    static ReachingMemVal getSelect(BasicBlock *BB, const Value *Cond,
                                    const Value *TrueAddr,
                                    const Value *FalseAddr) {
      return {DepKind::Select, BB,       nullptr, nullptr, -1, Cond,
              TrueAddr,        FalseAddr};
    }
  };

  struct DependencyBlockInfo {
    DependencyBlockInfo() = delete;
    DependencyBlockInfo(const PHITransAddr &Addr, MemoryAccess *ClobberMA)
        : Addr(Addr), InitialClobberMA(ClobberMA), ClobberMA(ClobberMA),
          ForceUnknown(false), Visited(false) {}
    PHITransAddr Addr;
    MemoryAccess *InitialClobberMA;
    MemoryAccess *ClobberMA;
    std::optional<ReachingMemVal> MemVal;
    bool ForceUnknown : 1;
    bool Visited : 1;
  };

  using DependencyBlockSet = DenseMap<BasicBlock *, DependencyBlockInfo>;

  std::optional<GVNPass::ReachingMemVal> scanMemoryAccessesUsers(
      const MemoryLocation &Loc, bool IsInvariantLoad, BasicBlock *BB,
      const SmallVectorImpl<MemoryAccess *> &ClobbersList, MemorySSA &MSSA,
      BatchAAResults &AA, LoadInst *L = nullptr);

  std::optional<GVNPass::ReachingMemVal>
  accessMayModifyLocation(MemoryAccess *ClobberMA, const MemoryLocation &Loc,
                          bool IsInvariantLoad, BasicBlock *BB, MemorySSA &MSSA,
                          BatchAAResults &AA);

  bool collectPredecessors(BasicBlock *BB, const PHITransAddr &Addr,
                           MemoryAccess *ClobberMA, DependencyBlockSet &Blocks,
                           SmallVectorImpl<BasicBlock *> &Worklist);

  void collectClobberList(SmallVectorImpl<MemoryAccess *> &Clobbers,
                          BasicBlock *BB, const DependencyBlockInfo &StartInfo,
                          const DependencyBlockSet &Blocks, MemorySSA &MSSA);

  bool findReachingValuesForLoad(LoadInst *Inst,
                                 SmallVectorImpl<ReachingMemVal> &Values,
                                 MemorySSA &MSSA, AAResults &AA);

  // Helper functions of redundant load elimination.
  bool processLoad(LoadInst *L);
  bool processMaskedLoad(IntrinsicInst *I);
  bool processNonLocalLoad(LoadInst *L);
  bool processNonLocalLoad(LoadInst *L, SmallVectorImpl<ReachingMemVal> &Deps);
  bool processAssumeIntrinsic(AssumeInst *II);

  /// Given a local dependency (Def or Clobber) determine if a value is
  /// available for the load.
  std::optional<AvailableValue>
  analyzeLoadAvailability(LoadInst *Load, const ReachingMemVal &Dep,
                          Value *Address);

  /// Given a select-dependency for the load (the load address is a select of
  /// \p TrueAddr and \p FalseAddr guarded by \p Cond), determine whether a
  /// value is available by finding dominating values for both addresses.  If
  /// so, the load can be rematerialized as a select of those two values.
  std::optional<AvailableValue>
  analyzeSelectAvailability(LoadInst *Load, Value *Cond, Value *TrueAddr,
                            Value *FalseAddr, Instruction *From);

  /// Given a list of non-local dependencies, determine if a value is
  /// available for the load in each specified block.  If it is, add it to
  /// ValuesPerBlock.  If not, add it to UnavailableBlocks.
  void analyzeLoadAvailability(LoadInst *Load,
                               SmallVectorImpl<ReachingMemVal> &Deps,
                               AvailValInBlkVect &ValuesPerBlock,
                               UnavailBlkVect &UnavailableBlocks);

  /// Given a critical edge from Pred to LoadBB, find a load instruction
  /// which is identical to Load from another successor of Pred.
  LoadInst *findLoadToHoistIntoPred(BasicBlock *Pred, BasicBlock *LoadBB,
                                    LoadInst *Load);

  bool performLoadPRE(LoadInst *Load, AvailValInBlkVect &ValuesPerBlock,
                      UnavailBlkVect &UnavailableBlocks);

  /// Try to replace a load which executes on each loop iteraiton with Phi
  /// translation of load in preheader and load(s) in conditionally executed
  /// paths.
  bool performLoopLoadPRE(LoadInst *Load, AvailValInBlkVect &ValuesPerBlock,
                          UnavailBlkVect &UnavailableBlocks);

  /// Eliminates partially redundant \p Load, replacing it with \p
  /// AvailableLoads (connected by Phis if needed).
  void eliminatePartiallyRedundantLoad(
      LoadInst *Load, AvailValInBlkVect &ValuesPerBlock,
      MapVector<BasicBlock *, Value *> &AvailableLoads,
      MapVector<BasicBlock *, LoadInst *> *CriticalEdgePredAndLoad);

  // Other helper routines.
  bool processInstruction(Instruction *I);
  bool processBlock(BasicBlock *BB);
  bool iterateOnFunction(Function &F);
  bool performPRE(Function &F);
  bool performScalarPRE(Instruction *I);
  bool performScalarPREInsertion(Instruction *Instr, BasicBlock *Pred,
                                 BasicBlock *Curr, unsigned int ValNo);
  Value *findLeader(const BasicBlock *BB, uint32_t Num);
  void cleanupGlobalSets();
  void removeInstruction(Instruction *I);
  void verifyRemoved(const Instruction *I) const;
  bool splitCriticalEdges();
  BasicBlock *splitCriticalEdges(BasicBlock *Pred, BasicBlock *Succ);
  bool
  propagateEquality(Value *LHS, Value *RHS,
                    const std::variant<BasicBlockEdge, Instruction *> &Root);
  bool processFoldableCondBr(CondBrInst *BI);
  void addDeadBlock(BasicBlock *BB);
  void assignValNumForDeadCode();
  void assignBlockRPONumber(Function &F);
};

/// Create a legacy GVN pass.
/// @param ScalarPRE Whether to enable PRE of scalar instructions.
/// @return A new legacy FunctionPass that runs GVN.
LLVM_ABI FunctionPass *createGVNPass(bool ScalarPRE);
/// Create a legacy GVN pass with default options.
/// @return A new legacy FunctionPass that runs GVN.
LLVM_ABI FunctionPass *createGVNPass();

/// A simple and fast domtree-based GVN pass to hoist common expressions
/// from sibling branches.
struct GVNHoistPass : OptionalPassInfoMixin<GVNHoistPass> {
  /// Run the pass over the function.
  /// @param F Function whose common expressions may be hoisted.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Uses an "inverted" value numbering to decide the similarity of
/// expressions and sinks similar expressions into successors.
struct GVNSinkPass : OptionalPassInfoMixin<GVNSinkPass> {
  /// Run the pass over the function.
  /// @param F Function whose similar expressions may be sunk.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_GVN_H
