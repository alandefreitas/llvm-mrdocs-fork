//===- Transforms/Utils/SampleProfileInference.h ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file provides the interface for the profile inference algorithm, profi.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_SAMPLEPROFILEINFERENCE_H
#define LLVM_TRANSFORMS_UTILS_SAMPLEPROFILEINFERENCE_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/SmallVector.h"

namespace llvm {

struct FlowJump;

/// A wrapper of a binary basic block.
struct FlowBlock {
  /// Index of this block in the enclosing flow function.
  uint64_t Index;
  /// Sampled or assigned weight of this block.
  uint64_t Weight{0};
  /// True if \p Weight was not provided by samples.
  bool HasUnknownWeight{true};
  /// True if this block is considered unlikely to execute.
  bool IsUnlikely{false};
  /// Inferred execution flow through this block.
  uint64_t Flow{0};
  /// Outgoing jumps from this block.
  std::vector<FlowJump *> SuccJumps;
  /// Incoming jumps to this block.
  std::vector<FlowJump *> PredJumps;

  /// Check if it is the entry block in the function.
  /// \return True if the block has no predecessors.
  bool isEntry() const { return PredJumps.empty(); }

  /// Check if it is an exit block in the function.
  /// \return True if the block has no successors.
  bool isExit() const { return SuccJumps.empty(); }
};

/// A wrapper of a jump between two basic blocks.
struct FlowJump {
  /// Index of the source block.
  uint64_t Source;
  /// Index of the target block.
  uint64_t Target;
  /// Sampled or assigned weight of this jump.
  uint64_t Weight{0};
  /// True if \p Weight was not provided by samples.
  bool HasUnknownWeight{true};
  /// True if this jump is considered unlikely to be taken.
  bool IsUnlikely{false};
  /// Inferred execution flow along this jump.
  uint64_t Flow{0};
};

/// A wrapper of binary function with basic blocks and jumps.
struct FlowFunction {
  /// Basic blocks in the function.
  std::vector<FlowBlock> Blocks;
  /// Jumps between the basic blocks.
  std::vector<FlowJump> Jumps;
  /// The index of the entry block.
  uint64_t Entry{0};
};

/// Thresholds and options for the profile inference algorithm.
///
/// Default values are tuned for several large-scale applications, and can be
/// modified via corresponding command-line flags.
struct ProfiParams {
  /// Evenly distribute flow when there are multiple equally likely options.
  bool EvenFlowDistribution{false};

  /// Evenly re-distribute flow among unknown subgraphs.
  bool RebalanceUnknown{false};

  /// Join isolated components having positive flow.
  bool JoinIslands{false};

  /// The cost of increasing a block's count by one.
  unsigned CostBlockInc{0};

  /// The cost of decreasing a block's count by one.
  unsigned CostBlockDec{0};

  /// The cost of increasing a count of zero-weight block by one.
  unsigned CostBlockZeroInc{0};

  /// The cost of increasing the entry block's count by one.
  unsigned CostBlockEntryInc{0};

  /// The cost of decreasing the entry block's count by one.
  unsigned CostBlockEntryDec{0};

  /// The cost of increasing an unknown block's count by one.
  unsigned CostBlockUnknownInc{0};

  /// The cost of increasing a jump's count by one.
  unsigned CostJumpInc{0};

  /// The cost of increasing a fall-through jump's count by one.
  unsigned CostJumpFTInc{0};

  /// The cost of decreasing a jump's count by one.
  unsigned CostJumpDec{0};

  /// The cost of decreasing a fall-through jump's count by one.
  unsigned CostJumpFTDec{0};

  /// The cost of increasing an unknown jump's count by one.
  unsigned CostJumpUnknownInc{0};

  /// The cost of increasing an unknown fall-through jump's count by one.
  unsigned CostJumpUnknownFTInc{0};

  /// The cost of taking an unlikely block/jump.
  const int64_t CostUnlikely = ((int64_t)1) << 30;
};

/// Apply the profile inference algorithm to \p Func with custom \p Params.
/// \param Params Inference cost and policy parameters.
/// \param Func Flow function whose block and jump flows are inferred in place.
LLVM_ABI void applyFlowInference(const ProfiParams &Params, FlowFunction &Func);

/// Apply the profile inference algorithm to \p Func with default parameters.
/// \param Func Flow function whose block and jump flows are inferred in place.
LLVM_ABI void applyFlowInference(FlowFunction &Func);

/// Sample profile inference pass.
template <typename FT> class SampleProfileInference {
public:
  /// Graph node reference type for \p FT.
  using NodeRef = typename GraphTraits<FT *>::NodeRef;
  /// Basic block type obtained by stripping the pointer from \p NodeRef.
  using BasicBlockT = std::remove_pointer_t<NodeRef>;
  /// Function type provided as the class template argument.
  using FunctionT = FT;
  /// Directed edge between two basic blocks.
  using Edge = std::pair<const BasicBlockT *, const BasicBlockT *>;
  /// Map from basic blocks to their weights.
  using BlockWeightMap = DenseMap<const BasicBlockT *, uint64_t>;
  /// Map from edges to their weights.
  using EdgeWeightMap = DenseMap<Edge, uint64_t>;
  /// Map from each basic block to its successors in the CFG.
  using BlockEdgeMap =
      DenseMap<const BasicBlockT *, SmallVector<const BasicBlockT *, 8>>;

  /// Construct inference state from block samples only.
  /// \param F Function whose profile is being inferred.
  /// \param Successors Successor lists for each basic block in \p F.
  /// \param SampleBlockWeights Sampled weights for basic blocks.
  SampleProfileInference(FunctionT &F, BlockEdgeMap &Successors,
                         BlockWeightMap &SampleBlockWeights)
      : F(F), Successors(Successors), SampleBlockWeights(SampleBlockWeights) {}

  /// Construct inference state from block and edge samples.
  /// \param F Function whose profile is being inferred.
  /// \param Successors Successor lists for each basic block in \p F.
  /// \param SampleBlockWeights Sampled weights for basic blocks.
  /// \param SampleEdgeWeights Sampled weights for edges.
  SampleProfileInference(FunctionT &F, BlockEdgeMap &Successors,
                         BlockWeightMap &SampleBlockWeights,
                         EdgeWeightMap &SampleEdgeWeights)
      : F(F), Successors(Successors), SampleBlockWeights(SampleBlockWeights),
        SampleEdgeWeights(SampleEdgeWeights) {}

  /// Apply the profile inference algorithm for a given function.
  /// \param BlockWeights [out] Inferred block weights.
  /// \param EdgeWeights [out] Inferred edge weights.
  void apply(BlockWeightMap &BlockWeights, EdgeWeightMap &EdgeWeights);

private:
  /// Initialize flow function blocks, jumps and misc metadata.
  FlowFunction
  createFlowFunction(const std::vector<const BasicBlockT *> &BasicBlocks,
                     DenseMap<const BasicBlockT *, uint64_t> &BlockIndex);

  /// Try to infer branch probabilities mimicking implementation of
  /// BranchProbabilityInfo. Unlikely taken branches are marked so that the
  /// inference algorithm can avoid sending flow along corresponding edges.
  void findUnlikelyJumps(const std::vector<const BasicBlockT *> &BasicBlocks,
                         BlockEdgeMap &Successors, FlowFunction &Func);

  /// Determine whether the block is an exit in the CFG.
  bool isExit(const BasicBlockT *BB);

  /// Function.
  const FunctionT &F;

  /// Successors for each basic block in the CFG.
  BlockEdgeMap &Successors;

  /// Map basic blocks to their sampled weights.
  BlockWeightMap &SampleBlockWeights;

  /// Map edges to their sampled weights.
  EdgeWeightMap SampleEdgeWeights;
};

template <typename BT>
void SampleProfileInference<BT>::apply(BlockWeightMap &BlockWeights,
                                       EdgeWeightMap &EdgeWeights) {
  // Find all forwards reachable blocks which the inference algorithm will be
  // applied on.
  df_iterator_default_set<const BasicBlockT *> Reachable;
  for (auto *BB : depth_first_ext(&F, Reachable))
    (void)BB /* Mark all reachable blocks */;

  // Find all backwards reachable blocks which the inference algorithm will be
  // applied on.
  df_iterator_default_set<const BasicBlockT *> InverseReachable;
  for (const auto &BB : F) {
    // An exit block is a block without any successors.
    if (isExit(&BB)) {
      for (auto *RBB : inverse_depth_first_ext(&BB, InverseReachable))
        (void)RBB;
    }
  }

  // Keep a stable order for reachable blocks
  DenseMap<const BasicBlockT *, uint64_t> BlockIndex;
  std::vector<const BasicBlockT *> BasicBlocks;
  BlockIndex.reserve(Reachable.size());
  BasicBlocks.reserve(Reachable.size());
  for (const auto &BB : F) {
    if (Reachable.count(&BB) && InverseReachable.count(&BB)) {
      BlockIndex[&BB] = BasicBlocks.size();
      BasicBlocks.push_back(&BB);
    }
  }

  BlockWeights.clear();
  EdgeWeights.clear();
  bool HasSamples = false;
  for (const auto *BB : BasicBlocks) {
    auto It = SampleBlockWeights.find(BB);
    if (It != SampleBlockWeights.end() && It->second > 0) {
      HasSamples = true;
      BlockWeights[BB] = It->second;
    }
  }
  // Quit early for functions with a single block or ones w/o samples
  if (BasicBlocks.size() <= 1 || !HasSamples) {
    return;
  }

  // Create necessary objects
  FlowFunction Func = createFlowFunction(BasicBlocks, BlockIndex);

  // Create and apply the inference network model.
  applyFlowInference(Func);

  // Extract the resulting weights from the control flow
  // All weights are increased by one to avoid propagation errors introduced by
  // zero weights.
  for (const auto *BB : BasicBlocks) {
    BlockWeights[BB] = Func.Blocks[BlockIndex[BB]].Flow;
  }
  for (auto &Jump : Func.Jumps) {
    Edge E = std::make_pair(BasicBlocks[Jump.Source], BasicBlocks[Jump.Target]);
    EdgeWeights[E] = Jump.Flow;
  }

#ifndef NDEBUG
  // Unreachable blocks and edges should not have a weight.
  for (auto &I : BlockWeights) {
    assert(Reachable.contains(I.first));
    assert(InverseReachable.contains(I.first));
  }
  for (auto &I : EdgeWeights) {
    assert(Reachable.contains(I.first.first) &&
           Reachable.contains(I.first.second));
    assert(InverseReachable.contains(I.first.first) &&
           InverseReachable.contains(I.first.second));
  }
#endif
}

template <typename BT>
FlowFunction SampleProfileInference<BT>::createFlowFunction(
    const std::vector<const BasicBlockT *> &BasicBlocks,
    DenseMap<const BasicBlockT *, uint64_t> &BlockIndex) {
  FlowFunction Func;
  Func.Blocks.reserve(BasicBlocks.size());
  // Create FlowBlocks
  for (const auto *BB : BasicBlocks) {
    FlowBlock Block;
    auto It = SampleBlockWeights.find(BB);
    if (It != SampleBlockWeights.end()) {
      Block.HasUnknownWeight = false;
      Block.Weight = It->second;
    } else {
      Block.HasUnknownWeight = true;
      Block.Weight = 0;
    }
    Block.Index = Func.Blocks.size();
    Func.Blocks.push_back(Block);
  }
  // Create FlowEdges
  for (const auto *BB : BasicBlocks) {
    for (auto *Succ : Successors[BB]) {
      if (!BlockIndex.count(Succ))
        continue;
      FlowJump Jump;
      Jump.Source = BlockIndex[BB];
      Jump.Target = BlockIndex[Succ];
      auto It = SampleEdgeWeights.find(std::make_pair(BB, Succ));
      if (It != SampleEdgeWeights.end()) {
        Jump.HasUnknownWeight = false;
        Jump.Weight = It->second;
      } else {
        Jump.HasUnknownWeight = true;
        Jump.Weight = 0;
      }
      Func.Jumps.push_back(Jump);
    }
  }
  for (auto &Jump : Func.Jumps) {
    uint64_t Src = Jump.Source;
    uint64_t Dst = Jump.Target;
    Func.Blocks[Src].SuccJumps.push_back(&Jump);
    Func.Blocks[Dst].PredJumps.push_back(&Jump);
  }

  // Try to infer probabilities of jumps based on the content of basic block
  findUnlikelyJumps(BasicBlocks, Successors, Func);

  // Find the entry block
  for (size_t I = 0; I < Func.Blocks.size(); I++) {
    if (Func.Blocks[I].isEntry()) {
      Func.Entry = I;
      break;
    }
  }
  assert(Func.Entry == 0 && "incorrect index of the entry block");

  // Pre-process data: make sure the entry weight is at least 1
  auto &EntryBlock = Func.Blocks[Func.Entry];
  if (EntryBlock.Weight == 0 && !EntryBlock.HasUnknownWeight) {
    EntryBlock.Weight = 1;
    EntryBlock.HasUnknownWeight = false;
  }

  return Func;
}

template <typename BT>
inline void SampleProfileInference<BT>::findUnlikelyJumps(
    const std::vector<const BasicBlockT *> &BasicBlocks,
    BlockEdgeMap &Successors, FlowFunction &Func) {}

template <typename BT>
inline bool SampleProfileInference<BT>::isExit(const BasicBlockT *BB) {
  return BB->succ_empty();
}

} // end namespace llvm
#endif // LLVM_TRANSFORMS_UTILS_SAMPLEPROFILEINFERENCE_H
