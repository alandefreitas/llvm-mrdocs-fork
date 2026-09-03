//===- CallGraphUpdater.h - A (lazy) call graph update helper ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file provides interfaces used to manipulate a call graph, regardless
/// if it is a "old style" CallGraph or an "new style" LazyCallGraph.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_CALLGRAPHUPDATER_H
#define LLVM_TRANSFORMS_UTILS_CALLGRAPHUPDATER_H

#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class CallGraph;
class CallGraphSCC;

/// Wrapper to unify "old style" CallGraph and "new style" LazyCallGraph.
///
/// This simplifies the interface and the call sites, e.g., new and old pass manager passes can share the same code.
class CallGraphUpdater {
  /// Containers for functions which we did replace or want to delete when
  /// `finalize` is called. This can happen explicitly or as part of the
  /// destructor. Dead functions in comdat sections are tracked separately
  /// because a function with discardable linakage in a COMDAT should only
  /// be dropped if the entire COMDAT is dropped, see git ac07703842cf.
  ///{
  SmallPtrSet<Function *, 16> ReplacedFunctions;
  SmallVector<Function *, 16> DeadFunctions;
  SmallVector<Function *, 16> DeadFunctionsInComdats;
  ///}

  /// New PM variables
  ///{
  LazyCallGraph *LCG = nullptr;
  LazyCallGraph::SCC *SCC = nullptr;
  CGSCCAnalysisManager *AM = nullptr;
  CGSCCUpdateResult *UR = nullptr;
  FunctionAnalysisManager *FAM = nullptr;
  ///}

public:
  /// Default-construct a CallGraphUpdater with no attached call graph.
  CallGraphUpdater() = default;
  /// Destroy the updater and finalize pending call-graph updates.
  ~CallGraphUpdater() { finalize(); }

  /// Initialize the updater for use inside a new-PM CGSCC pass.
  ///
  /// Suitable for usage outside of a CGSCC pass, or inside a CGSCC pass in
  /// the old and new pass manager (PM).
  /// \param LCG Lazy call graph being updated.
  /// \param SCC SCC currently being processed.
  /// \param AM CGSCC analysis manager for the pass.
  /// \param UR Update result that records call-graph changes.
  ///{
  void initialize(LazyCallGraph &LCG, LazyCallGraph::SCC &SCC,
                  CGSCCAnalysisManager &AM, CGSCCUpdateResult &UR) {
    this->LCG = &LCG;
    this->SCC = &SCC;
    this->AM = &AM;
    this->UR = &UR;
    FAM =
        &AM.getResult<FunctionAnalysisManagerCGSCCProxy>(SCC, LCG).getManager();
  }
  ///}

  /// Finalizer that will trigger actions like function removal from the CG.
  /// \return True if any dead functions were removed from the call graph.
  LLVM_ABI bool finalize();

  /// Remove \p Fn from the call graph.
  /// \param Fn Function to remove from the call graph.
  LLVM_ABI void removeFunction(Function &Fn);

  /// After an CGSCC pass changes a function in ways that affect the call
  /// graph, this method can be called to update it.
  /// \param Fn Function whose call-graph edges need to be reanalyzed.
  LLVM_ABI void reanalyzeFunction(Function &Fn);

  /// Update the call graph for a function created by outlining.
  ///
  /// If a new function was created by outlining, this method can be called
  /// to update the call graph for the new function. Note that the old one
  /// still needs to be re-analyzed or manually updated.
  /// \param OriginalFn Function from which code was outlined.
  /// \param NewFn Newly outlined function to register in the call graph.
  LLVM_ABI void registerOutlinedFunction(Function &OriginalFn, Function &NewFn);

  /// Replace \p OldFn in the call graph (and SCC) with \p NewFn.
  ///
  /// The uses outside the call graph and the function \p OldFn are not
  /// modified. Note that \p OldFn is also removed from the call graph
  /// (\see removeFunction).
  /// \param OldFn Function to replace in the call graph.
  /// \param NewFn Function that takes the place of \p OldFn.
  LLVM_ABI void replaceFunctionWith(Function &OldFn, Function &NewFn);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_CALLGRAPHUPDATER_H
