//===- LoopPass.h - LoopPass class ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines LoopPass class. All loop optimization
// and transformation passes are derived from LoopPass.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_LOOPPASS_H
#define LLVM_ANALYSIS_LOOPPASS_H

#include "llvm/IR/LegacyPassManagers.h"
#include "llvm/Pass.h"
#include "llvm/Support/Compiler.h"
#include <deque>

namespace llvm {

class Loop;
class LoopInfo;
class LPPassManager;
class Function;

/// Base class for loop optimization and transformation passes.
class LLVM_ABI LoopPass : public Pass {
public:
  /// Construct a loop pass with Pass ID \p pid.
  /// @param pid Static Pass ID for this pass instance.
  explicit LoopPass(char &pid) : Pass(PT_Loop, pid) {}

  /// Get a pass to print the function corresponding to a Loop.
  /// @param O Stream to write the printed IR to.
  /// @param Banner Banner text printed before the IR.
  /// @return A pass that prints the function corresponding to a Loop.
  Pass *createPrinterPass(raw_ostream &O,
                          const std::string &Banner) const override;

  /// Run this pass on the specified Loop.
  /// @param L Loop to process.
  /// @param LPM Loop pass manager that manages this pass.
  /// @return True if the pass modifies this Loop.
  virtual bool runOnLoop(Loop *L, LPPassManager &LPM) = 0;

  /// Bring \c Pass::doInitialization into scope for overload resolution.
  using llvm::Pass::doInitialization;
  /// Bring \c Pass::doFinalization into scope for overload resolution.
  using llvm::Pass::doFinalization;

  /// Perform initialization before processing loops.
  /// @param L Loop used for initialization context.
  /// @param LPM Loop pass manager that manages this pass.
  /// @return True if the pass modifies the loop nest during initialization.
  virtual bool doInitialization(Loop *L, LPPassManager &LPM) {
    return false;
  }

  /// Perform finalization after all loops have been processed.
  ///
  /// The finalization hook does not supply a Loop because at this time
  /// the loop nest is completely different.
  /// @return True if the pass modifies the function during finalization.
  virtual bool doFinalization() { return false; }

  /// Prepare the pass manager stack for this loop pass.
  ///
  /// Check if this pass is suitable for the current LPPassManager, if
  /// available. This pass P is not suitable for a LPPassManager if P
  /// is not preserving higher level analysis info used by other
  /// LPPassManager passes. In such case, pop LPPassManager from the
  /// stack. This will force assignPassManager() to create new
  /// LPPassManger as expected.
  /// @param PMS Stack of pass managers to search or update.
  void preparePassManager(PMStack &PMS) override;

  /// Assign pass manager to manage this pass.
  /// @param PMS Stack of pass managers to search or update.
  /// @param PMT Preferred pass manager type for this pass.
  void assignPassManager(PMStack &PMS, PassManagerType PMT) override;

  ///  Return what kind of Pass Manager can manage this pass.
  /// @return \c PMT_LoopPassManager.
  PassManagerType getPotentialPassManagerType() const override {
    return PMT_LoopPassManager;
  }

protected:
  /// Return true if optional passes should skip loop \p L.
  ///
  /// Optional passes call this function to check whether the pass should be
  /// skipped. This is the case when Attribute::OptimizeNone is set or when
  /// optimization bisect is over the limit.
  /// @param L Loop being considered for processing.
  /// @return True if optional passes should skip loop \p L.
  bool skipLoop(const Loop *L) const;
};

/// Pass manager that schedules and runs LoopPasses on a function.
class LLVM_ABI LPPassManager : public FunctionPass, public PMDataManager {
public:
  /// Pass identification, replacement for typeid.
  static char ID;
  /// Construct a loop pass manager.
  explicit LPPassManager();

  /// Execute all of the passes scheduled for execution.
  ///
  /// Keep track of whether any of the passes modifies the module, and if so,
  /// return true.
  /// @param F Function whose loops are processed.
  /// @return True if any of the passes modifies the function.
  bool runOnFunction(Function &F) override;

  /// Collect analysis usage for this pass manager.
  ///
  /// Pass Manager itself does not invalidate any analysis info.
  /// LPPassManager needs LoopInfo.
  /// @param Info Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &Info) const override;

  /// Return the name of this pass.
  /// @return The name of this pass.
  StringRef getPassName() const override { return "Loop Pass Manager"; }

  /// Return this manager as a PMDataManager.
  /// @return This manager as a PMDataManager.
  PMDataManager *getAsPMDataManager() override { return this; }
  /// Return this manager as a Pass.
  /// @return This manager as a Pass.
  Pass *getAsPass() override { return this; }

  /// Print passes managed by this manager.
  /// @param Offset Indentation offset for the printed structure.
  void dumpPassStructure(unsigned Offset) override;

  /// Return the contained loop pass at index \p N.
  /// @param N Index of the pass in this manager.
  /// @return The contained loop pass at index \p N.
  LoopPass *getContainedPass(unsigned N) {
    assert(N < PassVector.size() && "Pass number out of range!");
    LoopPass *LP = static_cast<LoopPass *>(PassVector[N]);
    return LP;
  }

  /// Return the type of this pass manager.
  /// @return \c PMT_LoopPassManager.
  PassManagerType getPassManagerType() const override {
    return PMT_LoopPassManager;
  }

public:
  /// Add a new loop into the loop queue.
  /// @param L Loop to enqueue for processing.
  void addLoop(Loop &L);

  /// Mark loop \p L as deleted.
  /// @param L Loop that has been deleted.
  void markLoopAsDeleted(Loop &L);

private:
  std::deque<Loop *> LQ;
  LoopInfo *LI;
  Loop *CurrentLoop;
  bool CurrentLoopDeleted;
};

/// Function pass that verifies LCSSA form is preserved by loop passes.
///
/// This pass is required by the LCSSA transformation. It is used inside
/// LPPassManager to check if current pass preserves LCSSA form, and if it does
/// pass manager calls lcssa verification for the current loop.
struct LCSSAVerificationPass : public FunctionPass {
  /// Pass identification, replacement for typeid.
  LLVM_ABI static char ID;
  /// Construct an LCSSA verification pass.
  LLVM_ABI LCSSAVerificationPass();

  /// Run this pass on \p F without modifying the function.
  /// @param F Function to visit.
  /// @return False; this pass does not modify the function.
  bool runOnFunction(Function &F) override { return false; }

  /// Preserve all analyses; this pass makes no transformations.
  /// @param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }
};

} // End llvm namespace

#endif
