//===- SwitchLoweringUtils.h - Switch Lowering ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SWITCHLOWERINGUTILS_H
#define LLVM_CODEGEN_SWITCHLOWERINGUTILS_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/Support/BranchProbability.h"
#include <vector>

namespace llvm {

class BlockFrequencyInfo;
class ConstantInt;
class FunctionLoweringInfo;
class MachineBasicBlock;
class ProfileSummaryInfo;
class TargetLowering;
class TargetMachine;

/// Utilities and data structures for lowering multi-case switch statements.
namespace SwitchCG {

/// Kind of case cluster produced during switch lowering.
enum CaseClusterKind {
  /// A cluster of adjacent case labels with the same destination, or just one
  /// case.
  CC_Range,
  /// A cluster of cases suitable for jump table lowering.
  CC_JumpTable,
  /// A cluster of cases suitable for bit test lowering.
  CC_BitTests
};

/// A cluster of case labels.
struct CaseCluster {
  /// Kind of this cluster (range, jump table, or bit tests).
  CaseClusterKind Kind;
  const ConstantInt *Low,  ///< Inclusive low case value covered by this cluster.
      *High;               ///< Inclusive high case value covered by this cluster.
  union {
    /// Destination MBB when \c Kind is \c CC_Range.
    MachineBasicBlock *MBB;
    /// Index into the jump-table case list when \c Kind is \c CC_JumpTable.
    unsigned JTCasesIndex;
    /// Index into the bit-test case list when \c Kind is \c CC_BitTests.
    unsigned BTCasesIndex;
  };
  /// Branch probability of taking this cluster.
  BranchProbability Prob;

  /// Create a range cluster covering [\p Low, \p High] that branches to \p MBB.
  /// @param Low Inclusive low case value.
  /// @param High Inclusive high case value.
  /// @param MBB Destination block for the range.
  /// @param Prob Branch probability of this cluster.
  /// @return A range CaseCluster for the given bounds and destination.
  static CaseCluster range(const ConstantInt *Low, const ConstantInt *High,
                           MachineBasicBlock *MBB, BranchProbability Prob) {
    CaseCluster C;
    C.Kind = CC_Range;
    C.Low = Low;
    C.High = High;
    C.MBB = MBB;
    C.Prob = Prob;
    return C;
  }

  /// Create a jump-table cluster covering [\p Low, \p High].
  /// @param Low Inclusive low case value.
  /// @param High Inclusive high case value.
  /// @param JTCasesIndex Index of the jump table in the case list.
  /// @param Prob Branch probability of this cluster.
  /// @return A jump-table CaseCluster for the given bounds and index.
  static CaseCluster jumpTable(const ConstantInt *Low, const ConstantInt *High,
                               unsigned JTCasesIndex, BranchProbability Prob) {
    CaseCluster C;
    C.Kind = CC_JumpTable;
    C.Low = Low;
    C.High = High;
    C.JTCasesIndex = JTCasesIndex;
    C.Prob = Prob;
    return C;
  }

  /// Create a bit-test cluster covering [\p Low, \p High].
  /// @param Low Inclusive low case value.
  /// @param High Inclusive high case value.
  /// @param BTCasesIndex Index of the bit-test info in the case list.
  /// @param Prob Branch probability of this cluster.
  /// @return A bit-test CaseCluster for the given bounds and index.
  static CaseCluster bitTests(const ConstantInt *Low, const ConstantInt *High,
                              unsigned BTCasesIndex, BranchProbability Prob) {
    CaseCluster C;
    C.Kind = CC_BitTests;
    C.Low = Low;
    C.High = High;
    C.BTCasesIndex = BTCasesIndex;
    C.Prob = Prob;
    return C;
  }
};

/// Vector of case clusters produced while lowering a switch.
using CaseClusterVector = std::vector<CaseCluster>;
/// Iterator into a \c CaseClusterVector.
using CaseClusterIt = CaseClusterVector::iterator;

/// Sort Clusters and merge adjacent cases.
/// @param Clusters Case clusters to sort and rangeify in place.
LLVM_ABI void sortAndRangeify(CaseClusterVector &Clusters);

/// Bit-mask description of case values that share a destination.
struct CaseBits {
  /// Bit mask of case values relative to the bit-test base.
  uint64_t Mask = 0;
  /// Destination block for cases matching \c Mask.
  MachineBasicBlock *BB = nullptr;
  /// Number of bits set in \c Mask.
  unsigned Bits = 0;
  /// Extra branch probability associated with this bit-test case.
  BranchProbability ExtraProb;

  /// Construct an empty CaseBits with default field values.
  CaseBits() = default;
  /// Construct a CaseBits with the given mask, destination, and probability.
  /// @param mask Bit mask of matching case values.
  /// @param bb Destination block for matching cases.
  /// @param bits Number of bits set in \p mask.
  /// @param Prob Extra branch probability for this case.
  CaseBits(uint64_t mask, MachineBasicBlock *bb, unsigned bits,
           BranchProbability Prob)
      : Mask(mask), BB(bb), Bits(bits), ExtraProb(Prob) {}
};

/// Vector of \c CaseBits entries for bit-test lowering.
using CaseBitsVector = std::vector<CaseBits>;

/// Comparison block emitted while lowering a multi-case switch.
///
/// This structure is used to communicate between SelectionDAGBuilder and
/// SDISel for the code generation of additional basic blocks needed by
/// multi-case switch statements.
struct CaseBlock {
  /// Predicate information for the GISel CaseBlock interface.
  struct PredInfoPair {
    /// Comparison predicate to emit.
    CmpInst::Predicate Pred;
    /// Set when no comparison should be emitted.
    bool NoCmp;
  };
  union {
    /// The condition code to use for the case block's setcc node.
    ///
    /// Besides the integer condition codes, this can also be SETTRUE, in which
    /// case no comparison gets emitted.
    ISD::CondCode CC;
    /// Predicate information used by the GISel interface.
    struct PredInfoPair PredInfo;
  };

  const Value *CmpLHS, ///< Left-hand side of the comparison to emit.
      *CmpMHS, ///< Middle operand for range compares (LHS <= MHS <= RHS).
      *CmpRHS; ///< Right-hand side of the comparison to emit.

  MachineBasicBlock *TrueBB,  ///< Block to branch to if the setcc is true.
      *FalseBB;               ///< Block to branch to if the setcc is false.

  /// The block into which to emit the code for the setcc and branches.
  MachineBasicBlock *ThisBB;

  /// The debug location of the instruction this CaseBlock was
  /// produced from.
  SDLoc DL;
  /// Debug location used by the GISel CaseBlock constructor.
  DebugLoc DbgLoc;

  BranchProbability TrueProb,  ///< Branch probability of the true successor.
      FalseProb;               ///< Branch probability of the false successor.
  /// True if the branch should be marked unpredictable.
  bool IsUnpredictable;

  /// Construct a CaseBlock for SelectionDAG lowering.
  /// @param cc Condition code for the comparison, or SETTRUE for none.
  /// @param cmplhs Left-hand side of the comparison.
  /// @param cmprhs Right-hand side of the comparison.
  /// @param cmpmiddle Optional middle operand for range comparisons.
  /// @param truebb Block to branch to when the condition is true.
  /// @param falsebb Block to branch to when the condition is false.
  /// @param me Block into which the comparison and branches are emitted.
  /// @param dl SelectionDAG debug location.
  /// @param trueprob Branch probability of the true successor.
  /// @param falseprob Branch probability of the false successor.
  /// @param isunpredictable Whether the branch is unpredictable.
  CaseBlock(ISD::CondCode cc, const Value *cmplhs, const Value *cmprhs,
            const Value *cmpmiddle, MachineBasicBlock *truebb,
            MachineBasicBlock *falsebb, MachineBasicBlock *me, SDLoc dl,
            BranchProbability trueprob = BranchProbability::getUnknown(),
            BranchProbability falseprob = BranchProbability::getUnknown(),
            bool isunpredictable = false)
      : CC(cc), CmpLHS(cmplhs), CmpMHS(cmpmiddle), CmpRHS(cmprhs),
        TrueBB(truebb), FalseBB(falsebb), ThisBB(me), DL(dl),
        TrueProb(trueprob), FalseProb(falseprob),
        IsUnpredictable(isunpredictable) {}

  /// Construct a CaseBlock for GlobalISel lowering.
  /// @param pred Comparison predicate to emit.
  /// @param nocmp If true, no comparison is emitted.
  /// @param cmplhs Left-hand side of the comparison.
  /// @param cmprhs Right-hand side of the comparison.
  /// @param cmpmiddle Optional middle operand for range comparisons.
  /// @param truebb Block to branch to when the condition is true.
  /// @param falsebb Block to branch to when the condition is false.
  /// @param me Block into which the comparison and branches are emitted.
  /// @param dl Debug location for GISel.
  /// @param trueprob Branch probability of the true successor.
  /// @param falseprob Branch probability of the false successor.
  /// @param isunpredictable Whether the branch is unpredictable.
  CaseBlock(CmpInst::Predicate pred, bool nocmp, const Value *cmplhs,
            const Value *cmprhs, const Value *cmpmiddle,
            MachineBasicBlock *truebb, MachineBasicBlock *falsebb,
            MachineBasicBlock *me, DebugLoc dl,
            BranchProbability trueprob = BranchProbability::getUnknown(),
            BranchProbability falseprob = BranchProbability::getUnknown(),
            bool isunpredictable = false)
      : PredInfo({pred, nocmp}), CmpLHS(cmplhs), CmpMHS(cmpmiddle),
        CmpRHS(cmprhs), TrueBB(truebb), FalseBB(falsebb), ThisBB(me),
        DbgLoc(dl), TrueProb(trueprob), FalseProb(falseprob),
        IsUnpredictable(isunpredictable) {}
};

/// Jump-table lowering record for a switch range.
struct JumpTable {
  /// The virtual register containing the index of the jump table entry
  /// to jump to.
  Register Reg;
  /// The JumpTableIndex for this jump table in the function.
  unsigned JTI;
  /// The MBB into which to emit the code for the indirect jump.
  MachineBasicBlock *MBB;
  /// The MBB of the default bb, which is a successor of the range
  /// check MBB.  This is when updating PHI nodes in successors.
  MachineBasicBlock *Default;

  /// The debug location of the instruction this JumpTable was produced from.
  std::optional<SDLoc> SL; // For SelectionDAG

  /// Construct a JumpTable with the given register, index, and blocks.
  /// @param R Virtual register holding the jump-table index.
  /// @param J Jump-table index in the function.
  /// @param M Block that emits the indirect jump.
  /// @param D Default successor block.
  /// @param SL Optional SelectionDAG debug location.
  JumpTable(Register R, unsigned J, MachineBasicBlock *M, MachineBasicBlock *D,
            std::optional<SDLoc> SL)
      : Reg(R), JTI(J), MBB(M), Default(D), SL(SL) {}
};

/// Header metadata for a jump-table range check.
struct JumpTableHeader {
  /// Inclusive first case value covered by the jump table.
  APInt First;
  /// Inclusive last case value covered by the jump table.
  APInt Last;
  /// Switch operand value being compared.
  const Value *SValue;
  /// Block that performs the range check before the jump table.
  MachineBasicBlock *HeaderBB;
  /// Whether code for this jump-table header has been emitted.
  bool Emitted;
  /// True if fallthrough from the header is unreachable.
  bool FallthroughUnreachable = false;

  /// Construct a jump-table header for the range [\p F, \p L].
  /// @param F Inclusive first case value.
  /// @param L Inclusive last case value.
  /// @param SV Switch operand being tested.
  /// @param H Header block for the range check.
  /// @param E Whether the header has already been emitted.
  JumpTableHeader(APInt F, APInt L, const Value *SV, MachineBasicBlock *H,
                  bool E = false)
      : First(std::move(F)), Last(std::move(L)), SValue(SV), HeaderBB(H),
        Emitted(E) {}
};
/// Pair of jump-table header metadata and the jump table itself.
using JumpTableBlock = std::pair<JumpTableHeader, JumpTable>;

/// Single bit-test case: a mask and its true/false destinations.
struct BitTestCase {
  /// Bit mask of case values relative to the bit-test base.
  uint64_t Mask;
  /// Block that performs this bit test.
  MachineBasicBlock *ThisBB;
  /// Destination when the tested bit(s) match.
  MachineBasicBlock *TargetBB;
  /// Extra branch probability associated with this bit-test case.
  BranchProbability ExtraProb;

  /// Construct a bit-test case with the given mask and destinations.
  /// @param M Bit mask of matching case values.
  /// @param T Block that performs this bit test.
  /// @param Tr Destination when the mask matches.
  /// @param Prob Extra branch probability for this case.
  BitTestCase(uint64_t M, MachineBasicBlock *T, MachineBasicBlock *Tr,
              BranchProbability Prob)
      : Mask(M), ThisBB(T), TargetBB(Tr), ExtraProb(Prob) {}
};

/// Small vector of bit-test cases for one bit-test block.
using BitTestInfo = SmallVector<BitTestCase, 3>;

/// Bit-test lowering record for a contiguous or sparse case range.
struct BitTestBlock {
  /// Inclusive first case value of the bit-test range.
  APInt First;
  /// Span of the bit-test range (Last - First).
  APInt Range;
  /// Switch operand value being tested.
  const Value *SValue;
  /// Virtual register holding the value shifted for bit tests.
  Register Reg;
  /// Machine value type of \c Reg.
  MVT RegVT;
  /// Whether code for this bit-test block has been emitted.
  bool Emitted;
  /// True if the covered cases form a contiguous range.
  bool ContiguousRange;
  /// Parent block that transfers control into the bit tests.
  MachineBasicBlock *Parent;
  /// Default destination when no bit-test case matches.
  MachineBasicBlock *Default;
  /// Ordered bit-test cases for this block.
  BitTestInfo Cases;
  /// Branch probability of entering the bit-test sequence.
  BranchProbability Prob;
  /// Branch probability of taking the default successor.
  BranchProbability DefaultProb;
  /// True if fallthrough from this block is unreachable.
  bool FallthroughUnreachable = false;

  /// Construct a bit-test block for the range starting at \p F.
  /// @param F Inclusive first case value.
  /// @param R Span of the bit-test range.
  /// @param SV Switch operand being tested.
  /// @param Rg Virtual register holding the shifted value.
  /// @param RgVT Machine value type of \p Rg.
  /// @param E Whether the block has already been emitted.
  /// @param CR Whether the covered cases are contiguous.
  /// @param P Parent block that enters the bit tests.
  /// @param D Default destination on no match.
  /// @param C Bit-test cases for this block.
  /// @param Pr Branch probability of entering the bit tests.
  BitTestBlock(APInt F, APInt R, const Value *SV, Register Rg, MVT RgVT, bool E,
               bool CR, MachineBasicBlock *P, MachineBasicBlock *D,
               BitTestInfo C, BranchProbability Pr)
      : First(std::move(F)), Range(std::move(R)), SValue(SV), Reg(Rg),
        RegVT(RgVT), Emitted(E), ContiguousRange(CR), Parent(P), Default(D),
        Cases(std::move(C)), Prob(Pr) {}
};

/// Return the range of values within a range.
/// @param Clusters Case clusters being considered for a jump table.
/// @param First Index of the first cluster in the candidate range.
/// @param Last Index of the last cluster in the candidate range.
/// @return The span of case values from the first to last cluster.
LLVM_ABI uint64_t getJumpTableRange(const CaseClusterVector &Clusters,
                                    unsigned First, unsigned Last);

/// Return the number of cases within a range.
/// @param TotalCases Per-cluster case counts aligned with the cluster vector.
/// @param First Index of the first cluster in the candidate range.
/// @param Last Index of the last cluster in the candidate range.
/// @return The total number of cases in the candidate cluster range.
LLVM_ABI uint64_t getJumpTableNumCases(
    const SmallVectorImpl<unsigned> &TotalCases, unsigned First, unsigned Last);

/// Work-list item describing a sub-range of clusters still to lower.
struct SwitchWorkListItem {
  /// Block that owns the current cluster sub-range.
  MachineBasicBlock *MBB = nullptr;
  /// Iterator to the first cluster in this work item.
  CaseClusterIt FirstCluster;
  /// Iterator to the last cluster in this work item.
  CaseClusterIt LastCluster;
  /// Known lower bound on the switch value for this work item, if any.
  const ConstantInt *GE = nullptr;
  /// Known exclusive upper bound on the switch value for this work item, if any.
  const ConstantInt *LT = nullptr;
  /// Branch probability of taking the default destination from this range.
  BranchProbability DefaultProb;
};
/// Work list of switch sub-ranges pending binary-search or cluster lowering.
using SwitchWorkList = SmallVector<SwitchWorkListItem, 4>;

/// Shared helper that finds jump tables and bit tests while lowering switches.
class SwitchLowering {
public:
  /// Construct a SwitchLowering helper bound to \p funcinfo.
  /// @param funcinfo Function lowering info for the current function.
  SwitchLowering(FunctionLoweringInfo &funcinfo) : FuncInfo(funcinfo) {}

  /// Initialize target-dependent lowering state.
  /// @param tli Target lowering info.
  /// @param tm Target machine.
  /// @param dl Data layout of the module being lowered.
  void init(const TargetLowering &tli, const TargetMachine &tm,
            const DataLayout &dl) {
    TLI = &tli;
    TM = &tm;
    DL = &dl;
  }

  /// Vector of CaseBlock structures used to communicate SwitchInst code
  /// generation information.
  std::vector<CaseBlock> SwitchCases;

  /// Vector of JumpTable structures used to communicate SwitchInst code
  /// generation information.
  std::vector<JumpTableBlock> JTCases;

  /// Vector of BitTestBlock structures used to communicate SwitchInst code
  /// generation information.
  std::vector<BitTestBlock> BitTestCases;

  /// Replace eligible contiguous clusters with jump-table clusters.
  /// @param Clusters Case clusters to scan and update in place.
  /// @param SI Switch instruction being lowered.
  /// @param SL Optional SelectionDAG debug location.
  /// @param DefaultMBB Default destination of the switch.
  /// @param PSI Optional profile summary info for density heuristics.
  /// @param BFI Optional block frequency info for density heuristics.
  LLVM_ABI void findJumpTables(CaseClusterVector &Clusters,
                               const SwitchInst *SI, std::optional<SDLoc> SL,
                               MachineBasicBlock *DefaultMBB,
                               ProfileSummaryInfo *PSI,
                               BlockFrequencyInfo *BFI);

  /// Build a jump-table cluster from Clusters[\p First..\p Last] if profitable.
  /// @param Clusters Case clusters being lowered.
  /// @param First Index of the first cluster in the candidate range.
  /// @param Last Index of the last cluster in the candidate range.
  /// @param SI Switch instruction being lowered.
  /// @param SL Optional SelectionDAG debug location.
  /// @param DefaultMBB Default destination of the switch.
  /// @param JTCluster Output cluster describing the built jump table.
  /// @return True if a jump table was built.
  LLVM_ABI bool buildJumpTable(const CaseClusterVector &Clusters,
                               unsigned First, unsigned Last,
                               const SwitchInst *SI,
                               const std::optional<SDLoc> &SL,
                               MachineBasicBlock *DefaultMBB,
                               CaseCluster &JTCluster);

  /// Replace eligible clusters with bit-test clusters.
  /// @param Clusters Case clusters to scan and update in place.
  /// @param SI Switch instruction being lowered.
  LLVM_ABI void findBitTestClusters(CaseClusterVector &Clusters,
                                    const SwitchInst *SI);

  /// Build a bit test cluster from Clusters[First..Last].
  ///
  /// Returns false if it decides it's not a good idea.
  /// @param Clusters Case clusters being lowered.
  /// @param First Index of the first cluster in the candidate range.
  /// @param Last Index of the last cluster in the candidate range.
  /// @param SI Switch instruction being lowered.
  /// @param BTCluster Output cluster describing the built bit tests.
  /// @return True if a bit-test cluster was built.
  LLVM_ABI bool buildBitTests(CaseClusterVector &Clusters, unsigned First,
                              unsigned Last, const SwitchInst *SI,
                              CaseCluster &BTCluster);

  /// Add \p Dst as a successor of \p Src with the given branch probability.
  /// @param Src Source machine basic block.
  /// @param Dst Successor machine basic block.
  /// @param Prob Branch probability of the edge from \p Src to \p Dst.
  virtual void addSuccessorWithProb(
      MachineBasicBlock *Src, MachineBasicBlock *Dst,
      BranchProbability Prob = BranchProbability::getUnknown()) = 0;

  /// Determine the rank by weight of CC in [First,Last]. If CC has more weight
  /// than each cluster in the range, its rank is 0.
  /// @param CC Cluster whose weight rank is computed.
  /// @param First Iterator to the first cluster in the range.
  /// @param Last Iterator to the last cluster in the range.
  /// @return The zero-based weight rank of \p CC among clusters in the range.
  LLVM_ABI unsigned caseClusterRank(const CaseCluster &CC, CaseClusterIt First,
                                    CaseClusterIt Last);

  /// Split points and probabilities for balancing a binary search tree.
  struct SplitWorkItemInfo {
    /// Iterator to the last cluster on the left side of the split.
    CaseClusterIt LastLeft;
    /// Iterator to the first cluster on the right side of the split.
    CaseClusterIt FirstRight;
    /// Total branch probability of the left partition.
    BranchProbability LeftProb;
    /// Total branch probability of the right partition.
    BranchProbability RightProb;
  };
  /// Compute split points that balance a near-optimal binary search tree.
  ///
  /// Compute information to balance the tree based on branch probabilities to
  /// create a near-optimal (in terms of search time given key frequency) binary
  /// search tree. See e.g. Kurt Mehlhorn "Nearly Optimal Binary Search Trees"
  /// (1975).
  /// @param W Work-list item describing the cluster range to split.
  /// @return Split iterators and left/right branch probabilities for \p W.
  LLVM_ABI SplitWorkItemInfo
  computeSplitWorkItemInfo(const SwitchWorkListItem &W);
  /// Virtual destructor for polymorphic cleanup of derived lowering helpers.
  virtual ~SwitchLowering() = default;

private:
  const TargetLowering *TLI = nullptr;
  const TargetMachine *TM = nullptr;
  const DataLayout *DL = nullptr;
  FunctionLoweringInfo &FuncInfo;
};

} // namespace SwitchCG
} // namespace llvm

#endif // LLVM_CODEGEN_SWITCHLOWERINGUTILS_H
