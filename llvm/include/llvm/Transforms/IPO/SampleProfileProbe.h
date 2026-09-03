//===- Transforms/IPO/SampleProfileProbe.h ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file provides the interface for the pseudo probe implementation for
/// AutoFDO.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_SAMPLEPROFILEPROBE_H
#define LLVM_TRANSFORMS_IPO_SAMPLEPROFILEPROBE_H

#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/IR/IRUnitRef.h"
#include "llvm/IR/PassManager.h"
#include "llvm/ProfileData/SampleProf.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class BasicBlock;
class Function;
class Instruction;
class Loop;
class PassInstrumentationCallbacks;
class TargetMachine;

class Module;

using namespace sampleprof;
/// Map from a basic block to its pseudo-probe ID.
using BlockIdMap = DenseMap<BasicBlock *, uint32_t>;
/// Map from an instruction to its pseudo-probe ID.
using InstructionIdMap = DenseMap<Instruction *, uint32_t>;
/// Map from tuples of Probe id and inline stack hash code to distribution
/// factors.
using ProbeFactorMap = DenseMap<std::pair<uint64_t, uint64_t>, float>;
/// Map from a function name to that function's probe distribution factors.
using FuncProbeFactorMap = StringMap<ProbeFactorMap>;


/// Pseudo probe verifier.
///
/// A verifier that can be run after each IR pass to detect the violation of
/// updating probe factors. In principle, the sum of distribution factor for a
/// probe should be identical before and after a pass. For a function pass, the
/// factor sum for a probe would be typically 100%.
class PseudoProbeVerifier {
public:
  /// Register after-pass callbacks that verify probe factors.
  ///
  /// \param PIC Pass instrumentation callbacks to register with.
  LLVM_ABI void registerCallbacks(PassInstrumentationCallbacks &PIC);

  /// Verify probe factors on \p IR after the pass named \p PassID.
  ///
  /// Implementation of pass instrumentation callbacks for the new pass
  /// manager.
  ///
  /// \param PassID Name of the pass that just ran.
  /// \param IR IR unit the pass ran on.
  LLVM_ABI void runAfterPass(StringRef PassID, IRUnitRef IR);

private:
  // Allow a little bias due the rounding to integral factors.
  constexpr static float DistributionFactorVariance = 0.02f;
  // Distribution factors from last pass.
  FuncProbeFactorMap FunctionProbeFactors;

  void collectProbeFactors(const BasicBlock *BB, ProbeFactorMap &ProbeFactors);
  void runAfterPass(const Module *M);
  void runAfterPass(const LazyCallGraph::SCC *C);
  void runAfterPass(const Function *F);
  void runAfterPass(const Loop *L);
  bool shouldVerifyFunction(const Function *F);
  void verifyProbeFactors(const Function *F,
                          const ProbeFactorMap &ProbeFactors);
};

/// Sample profile pseudo prober.
///
/// Insert pseudo probes for block sampling and value sampling.
class SampleProfileProber {
public:
  /// Construct a sample-profile pseudo prober for \p F.
  ///
  /// \param F Function whose CFG is assigned pseudo-probe IDs.
  LLVM_ABI SampleProfileProber(Function &F);
  /// Insert pseudo probes for block and callsite sampling into \p F.
  ///
  /// \param F Function to instrument.
  /// \param TM Target machine used when instrumenting \p F.
  LLVM_ABI void instrumentOneFunc(Function &F, TargetMachine *TM);

private:
  Function *getFunction() const { return F; }
  uint64_t getFunctionHash() const { return FunctionHash; }
  uint32_t getBlockId(const BasicBlock *BB) const;
  uint32_t getCallsiteId(const Instruction *Call) const;
  void findUnreachableBlocks(DenseSet<BasicBlock *> &BlocksToIgnore);
  void findInvokeNormalDests(DenseSet<BasicBlock *> &InvokeNormalDests);
  void computeBlocksToIgnore(DenseSet<BasicBlock *> &BlocksToIgnore,
                             DenseSet<BasicBlock *> &BlocksAndCallsToIgnore);
  const Instruction *
  getOriginalTerminator(const BasicBlock *Head,
                        const DenseSet<BasicBlock *> &BlocksToIgnore);
  void computeCFGHash(const DenseSet<BasicBlock *> &BlocksToIgnore);
  void computeProbeId(const DenseSet<BasicBlock *> &BlocksToIgnore,
                      const DenseSet<BasicBlock *> &BlocksAndCallsToIgnore);

  Function *F;

  /// The current module ID that is used to name a static object as a comdat
  /// group.
  std::string CurModuleUniqueId;

  /// A CFG hash code used to identify a function code changes.
  uint64_t FunctionHash;

  /// Map basic blocks to the their pseudo probe ids.
  BlockIdMap BlockProbeIds;

  /// Map indirect calls to the their pseudo probe ids.
  InstructionIdMap CallProbeIds;

  /// The ID of the last probe, Can be used to number a new probe.
  uint32_t LastProbeId;
};

/// Pass that inserts sample-profile pseudo probes into a module.
class SampleProfileProbePass
    : public OptionalPassInfoMixin<SampleProfileProbePass> {
  TargetMachine *TM;

public:
  /// Construct a sample-profile probe pass.
  ///
  /// \param TM Target machine used when instrumenting functions.
  SampleProfileProbePass(TargetMachine *TM) : TM(TM) {}
  /// Run pseudo-probe instrumentation over the given module.
  ///
  /// \param M Module whose functions receive pseudo probes.
  /// \param AM Module analysis manager providing analyses for the pass.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

/// Pseudo probe distribution factor updater.
///
/// Sample profile annotation can happen in both LTO prelink and postlink. The
/// postlink-time re-annotation can degrade profile quality because of prelink
/// code duplication transformation, such as loop unrolling, jump threading,
/// indirect call promotion etc. As such, samples corresponding to a source
/// location may be aggregated multiple times in postlink. With a concept of
/// distribution factor for pseudo probes, samples can be distributed among
/// duplicated probes reasonable based on the assumption that optimizations
/// duplicating code well-maintain the branch frequency information (BFI). This
/// pass updates distribution factors for each pseudo probe at the end of the
/// prelink pipeline, to reflect an estimated portion of the real execution
/// count.
class PseudoProbeUpdatePass
    : public OptionalPassInfoMixin<PseudoProbeUpdatePass> {
  void runOnFunction(Function &F, FunctionAnalysisManager &FAM);

public:
  /// Construct a pseudo-probe factor update pass.
  PseudoProbeUpdatePass() = default;
  /// Run the pseudo-probe factor update over the given module.
  ///
  /// \param M Module whose probe distribution factors are updated.
  /// \param AM Module analysis manager providing analyses for the pass.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // end namespace llvm
#endif // LLVM_TRANSFORMS_IPO_SAMPLEPROFILEPROBE_H
