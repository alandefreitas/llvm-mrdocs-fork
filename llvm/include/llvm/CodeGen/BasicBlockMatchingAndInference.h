//===- llvm/CodeGen/BasicBlockMatchingAndInference.h ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Infer weights for all basic blocks using matching and inference.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_BASIC_BLOCK_AND_INFERENCE_H
#define LLVM_CODEGEN_BASIC_BLOCK_AND_INFERENCE_H

#include "llvm/CodeGen/BasicBlockSectionsProfileReader.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/Transforms/Utils/SampleProfileInference.h"

namespace llvm {

/// Pass that matches Propeller profile basic blocks and infers their weights.
///
/// Matches profiled blocks to the current MachineFunction by block hash, then
/// infers weights for remaining blocks and edges.
class LLVM_ABI BasicBlockMatchingAndInference : public MachineFunctionPass {
private:
  using Edge = std::pair<const MachineBasicBlock *, const MachineBasicBlock *>;
  using BlockWeightMap = DenseMap<const MachineBasicBlock *, uint64_t>;
  using EdgeWeightMap = DenseMap<Edge, uint64_t>;
  using BlockEdgeMap = DenseMap<const MachineBasicBlock *,
                                SmallVector<const MachineBasicBlock *, 8>>;

  struct WeightInfo {
    // Weight of basic blocks.
    BlockWeightMap BlockWeights;
    // Weight of edges.
    EdgeWeightMap EdgeWeights;
  };

public:
  /// Pass identification, replacement for typeid.
  static char ID;
  /// Construct the basic-block matching and inference pass.
  BasicBlockMatchingAndInference();

  /// Return the name of this pass.
  ///
  /// \return Name of this pass as a string reference.
  StringRef getPassName() const override {
    return "Basic Block Matching and Inference";
  }

  /// Declare analyses required and preserved by this pass.
  ///
  /// \param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// Match and infer basic-block weights for machine function \p F.
  ///
  /// \param F Machine function to analyze.
  /// \return False; this analysis does not modify the machine function.
  bool runOnMachineFunction(MachineFunction &F) override;

  /// Return inferred weight info for function \p FuncName, if available.
  ///
  /// \param FuncName Name of the function whose weights are requested.
  /// \return Weight info for \p FuncName, or std::nullopt if none was computed.
  std::optional<WeightInfo> getWeightInfo(StringRef FuncName) const;

private:
  StringMap<WeightInfo> ProgramWeightInfo;

  WeightInfo initWeightInfoByMatching(MachineFunction &MF);

  void generateWeightInfoByInference(MachineFunction &MF,
                                     WeightInfo &MatchWeight);
};

} // end namespace llvm

#endif // LLVM_CODEGEN_BASIC_BLOCK_AND_INFERENCE_H
