//===- RegionPass.h - RegionPass class --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the RegionPass class. All region based analysis,
// optimization and transformation passes are derived from RegionPass.
// This class is implemented following the some ideas of the LoopPass.h class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_REGIONPASS_H
#define LLVM_ANALYSIS_REGIONPASS_H

#include "llvm/IR/LegacyPassManagers.h"
#include "llvm/Pass.h"
#include "llvm/Support/Compiler.h"
#include <deque>

namespace llvm {
class Function;
class RGPassManager;
class Region;
class RegionInfo;

//===----------------------------------------------------------------------===//
/// A pass that runs on each Region in a function.
///
/// RegionPass is managed by RGPassManager.
class LLVM_ABI RegionPass : public Pass {
public:
  /// Construct a region pass with Pass ID \p pid.
  /// @param pid Static Pass ID for this pass instance.
  explicit RegionPass(char &pid) : Pass(PT_Region, pid) {}

  //===--------------------------------------------------------------------===//
  /// @name To be implemented by every RegionPass
  ///
  //@{
  /// Run the pass on a specific Region
  ///
  /// Accessing regions not contained in the current region is not allowed.
  ///
  /// @param R The region this pass is run on.
  /// @param RGM The RegionPassManager that manages this Pass.
  ///
  /// @return True if the pass modifies this Region.
  virtual bool runOnRegion(Region *R, RGPassManager &RGM) = 0;

  /// Get a pass to print the LLVM IR in the region.
  ///
  /// @param O      The output stream to print the Region.
  /// @param Banner The banner to separate different printed passes.
  ///
  /// @return The pass to print the LLVM IR in the region.
  Pass *createPrinterPass(raw_ostream &O,
                          const std::string &Banner) const override;

  /// Bring \c Pass::doInitialization into scope for overload resolution.
  using llvm::Pass::doInitialization;
  /// Bring \c Pass::doFinalization into scope for overload resolution.
  using llvm::Pass::doFinalization;

  /// Perform initialization before processing regions.
  /// @param R Region used for initialization context.
  /// @param RGM Region pass manager that manages this pass.
  /// @return True if the pass modifies the region nest during initialization.
  virtual bool doInitialization(Region *R, RGPassManager &RGM) { return false; }
  /// Perform finalization after all regions have been processed.
  /// @return True if the pass modifies the function during finalization.
  virtual bool doFinalization() { return false; }
  //@}

  //===--------------------------------------------------------------------===//
  /// @name PassManager API
  ///
  //@{
  /// Prepare the pass manager stack for this region pass.
  /// @param PMS Stack of pass managers to search or update.
  void preparePassManager(PMStack &PMS) override;

  /// Assign pass manager to manage this pass.
  /// @param PMS Stack of pass managers to search or update.
  /// @param PMT Preferred pass manager type for this pass.
  void assignPassManager(PMStack &PMS,
                         PassManagerType PMT = PMT_RegionPassManager) override;

  /// Return what kind of Pass Manager can manage this pass.
  /// @return \c PMT_RegionPassManager.
  PassManagerType getPotentialPassManagerType() const override {
    return PMT_RegionPassManager;
  }
  //@}

protected:
  /// Optional passes call this function to check whether the pass should be
  /// skipped. This is the case when optimization bisect is over the limit.
  /// @param R Region being considered for processing.
  /// @return True if optional passes should skip region \p R.
  bool skipRegion(Region &R) const;
};

/// The pass manager to schedule RegionPasses.
class LLVM_ABI RGPassManager : public FunctionPass, public PMDataManager {
  std::deque<Region*> RQ;
  RegionInfo *RI;
  Region *CurrentRegion;

public:
  /// Pass identification, replacement for typeid.
  static char ID;
  /// Construct a region pass manager.
  explicit RGPassManager();

  /// Execute all of the passes scheduled for execution.
  ///
  /// @param F Function whose regions are processed.
  /// @return True if any of the passes modifies the function.
  bool runOnFunction(Function &F) override;

  /// Pass Manager itself does not invalidate any analysis info.
  /// RGPassManager needs RegionInfo.
  /// @param Info Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &Info) const override;

  /// Return the name of this pass.
  /// @return The name of this pass.
  StringRef getPassName() const override { return "Region Pass Manager"; }

  /// Return this manager as a PMDataManager.
  /// @return This manager as a PMDataManager.
  PMDataManager *getAsPMDataManager() override { return this; }
  /// Return this manager as a Pass.
  /// @return This manager as a Pass.
  Pass *getAsPass() override { return this; }

  /// Print passes managed by this manager.
  /// @param Offset Indentation offset for the printed structure.
  void dumpPassStructure(unsigned Offset) override;

  /// Get passes contained by this manager.
  /// @param N Index of the pass in this manager.
  /// @return The contained region pass at index \p N.
  Pass *getContainedPass(unsigned N) {
    assert(N < PassVector.size() && "Pass number out of range!");
    Pass *FP = static_cast<Pass *>(PassVector[N]);
    return FP;
  }

  /// Return the type of this pass manager.
  /// @return \c PMT_RegionPassManager.
  PassManagerType getPassManagerType() const override {
    return PMT_RegionPassManager;
  }
};

} // End llvm namespace

#endif
