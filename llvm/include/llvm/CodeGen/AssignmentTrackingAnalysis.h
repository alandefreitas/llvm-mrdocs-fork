//===-- llvm/CodeGen/AssignmentTrackingAnalysis.h --------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_ASSIGNMENTTRACKINGANALYSIS_H
#define LLVM_CODEGEN_ASSIGNMENTTRACKINGANALYSIS_H

#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

namespace llvm {
class Instruction;
class raw_ostream;
} // namespace llvm
class FunctionVarLocsBuilder;

namespace llvm {
/// Type wrapper for integer ID for Variables. 0 is reserved.
enum class VariableID : unsigned {
  /// Unused sentinel; UniqueVector IDs are one-based.
  Reserved = 0
};
/// Variable location definition used by FunctionVarLocs.
struct VarLocInfo {
  /// Variable this location definition belongs to.
  llvm::VariableID VariableID;
  /// Expression applied to \ref Values to describe the location.
  DIExpression *Expr = nullptr;
  /// Debug location of this definition.
  DebugLoc DL;
  /// Location operands (registers or memory) for this definition.
  RawLocationWrapper Values = RawLocationWrapper();
};

/// Describes the variable locations in a function.
///
/// Used as the result of the AssignmentTrackingAnalysis pass. Essentially
/// read-only outside of AssignmentTrackingAnalysis where it is built.
class FunctionVarLocs {
  /// Maps VarLocInfo.VariableID to a DebugVariable for VarLocRecords.
  SmallVector<DebugVariable> Variables;
  /// List of variable location changes grouped by the instruction the
  /// change occurs before (see VarLocsBeforeInst). The elements from
  /// zero to SingleVarLocEnd represent variables with a single location.
  SmallVector<VarLocInfo> VarLocRecords;
  /// End of range of VarLocRecords that represent variables with a single
  /// location that is valid for the entire scope. Range starts at 0.
  unsigned SingleVarLocEnd = 0;
  /// Maps an instruction to a range of VarLocs that start just before it.
  DenseMap<const Instruction *, std::pair<unsigned, unsigned>>
      VarLocsBeforeInst;

public:
  /// Return the DILocalVariable for the location definition represented by \p
  /// Loc.
  ///
  /// \param Loc Location definition whose variable is requested.
  /// \return The DILocalVariable for \p Loc.
  DILocalVariable *getDILocalVariable(const VarLocInfo *Loc) const {
    VariableID VarID = Loc->VariableID;
    return getDILocalVariable(VarID);
  }
  /// Return the DILocalVariable of the variable represented by \p ID.
  ///
  /// \param ID Identifier of the variable to look up.
  /// \return The DILocalVariable for \p ID.
  DILocalVariable *getDILocalVariable(VariableID ID) const {
    return const_cast<DILocalVariable *>(getVariable(ID).getVariable());
  }
  /// Return the DebugVariable represented by \p ID.
  ///
  /// \param ID Identifier of the variable to look up.
  /// \return The DebugVariable for \p ID.
  const DebugVariable &getVariable(VariableID ID) const {
    return Variables[static_cast<unsigned>(ID)];
  }

  ///@name iterators
  ///@{
  /// First single-location variable location definition.
  ///
  /// \return Pointer to the first single-location definition.
  const VarLocInfo *single_locs_begin() const { return VarLocRecords.begin(); }
  /// One past the last single-location variable location definition.
  ///
  /// \return Past-the-end pointer for the single-location definitions.
  const VarLocInfo *single_locs_end() const {
    const auto *It = VarLocRecords.begin();
    std::advance(It, SingleVarLocEnd);
    return It;
  }
  /// First variable location definition that comes before \p Before.
  ///
  /// \param Before Instruction the returned range precedes.
  /// \return Pointer to the first location definition before \p Before.
  const VarLocInfo *locs_begin(const Instruction *Before) const {
    auto Span = VarLocsBeforeInst.lookup(Before);
    const auto *It = VarLocRecords.begin();
    std::advance(It, Span.first);
    return It;
  }
  /// One past the last variable location definition that comes before \p
  /// Before.
  ///
  /// \param Before Instruction the returned range precedes.
  /// \return Past-the-end pointer for the location definitions before
  /// \p Before.
  const VarLocInfo *locs_end(const Instruction *Before) const {
    auto Span = VarLocsBeforeInst.lookup(Before);
    const auto *It = VarLocRecords.begin();
    std::advance(It, Span.second);
    return It;
  }
  ///@}

  /// Print the variable locations in \p Fn to \p OS.
  ///
  /// \param OS Output stream.
  /// \param Fn Function whose locations are printed.
  LLVM_ABI void print(raw_ostream &OS, const Function &Fn) const;

  ///@name Mutators
  ///@{
  /// Copy variable locations from \p Builder into this object.
  ///
  /// Non-const method used by AssignmentTrackingAnalysis; calling it
  /// incorrectly invalidates analysis results.
  ///
  /// \param Builder Source of the location records to install.
  LLVM_ABI void init(FunctionVarLocsBuilder &Builder);
  /// Discard all recorded variable locations.
  ///
  /// Non-const method used by AssignmentTrackingAnalysis; calling it
  /// incorrectly invalidates analysis results.
  LLVM_ABI void clear();
  ///@}
};

/// New pass manager analysis that computes assignment-tracking locations.
class DebugAssignmentTrackingAnalysis
    : public AnalysisInfoMixin<DebugAssignmentTrackingAnalysis> {
  friend AnalysisInfoMixin<DebugAssignmentTrackingAnalysis>;
  static AnalysisKey Key;

public:
  /// Result type produced by this analysis.
  using Result = FunctionVarLocs;
  /// Compute assignment-tracking locations for \p F.
  ///
  /// \param F Function to analyze.
  /// \param FAM Function analysis manager.
  /// \return Locations for \p F, or empty if assignment tracking is disabled.
  LLVM_ABI Result run(Function &F, FunctionAnalysisManager &FAM);
};

/// Printer pass for \c DebugAssignmentTrackingAnalysis results.
class DebugAssignmentTrackingPrinterPass
    : public RequiredPassInfoMixin<DebugAssignmentTrackingPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes to \p OS.
  ///
  /// \param OS Output stream for the printed locations.
  DebugAssignmentTrackingPrinterPass(raw_ostream &OS) : OS(OS) {}
  /// Print assignment-tracking locations for \p F.
  ///
  /// \param F Function whose locations are printed.
  /// \param FAM Function analysis manager providing the analysis.
  /// \return All analyses preserved.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
};

/// Legacy analysis pass that computes assignment-tracking locations.
class LLVM_ABI AssignmentTrackingAnalysis : public FunctionPass {
  std::unique_ptr<FunctionVarLocs> Results;

public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct an AssignmentTrackingAnalysis pass.
  AssignmentTrackingAnalysis();

  /// Compute assignment-tracking locations for \p F.
  ///
  /// \param F Function to analyze.
  /// \return False; this analysis does not modify the function.
  bool runOnFunction(Function &F) override;

  /// Declare that this analysis preserves all other analyses.
  ///
  /// \param AU Analysis usage to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }

  /// Return the locations computed for the last analyzed function.
  ///
  /// \return Pointer to the computed locations, or null if none.
  const FunctionVarLocs *getResults() { return Results.get(); }
};

} // end namespace llvm
#endif // LLVM_CODEGEN_ASSIGNMENTTRACKINGANALYSIS_H
