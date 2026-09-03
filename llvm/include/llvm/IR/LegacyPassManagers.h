//===- LegacyPassManagers.h - Legacy Pass Infrastructure --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the LLVM Pass Manager infrastructure.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_LEGACYPASSMANAGERS_H
#define LLVM_IR_LEGACYPASSMANAGERS_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Pass.h"
#include "llvm/Support/Compiler.h"
#include <vector>

//===----------------------------------------------------------------------===//
// Overview:
// The Pass Manager Infrastructure manages passes. It's responsibilities are:
//
//   o Manage optimization pass execution order
//   o Make required Analysis information available before pass P is run
//   o Release memory occupied by dead passes
//   o If Analysis information is dirtied by a pass then regenerate Analysis
//     information before it is consumed by another pass.
//
// Pass Manager Infrastructure uses multiple pass managers.  They are
// PassManager, FunctionPassManager, MPPassManager, FPPassManager, BBPassManager.
// This class hierarchy uses multiple inheritance but pass managers do not
// derive from another pass manager.
//
// PassManager and FunctionPassManager are two top-level pass manager that
// represents the external interface of this entire pass manager infrastucture.
//
// Important classes :
//
// [o] class PMTopLevelManager;
//
// Two top level managers, PassManager and FunctionPassManager, derive from
// PMTopLevelManager. PMTopLevelManager manages information used by top level
// managers such as last user info.
//
// [o] class PMDataManager;
//
// PMDataManager manages information, e.g. list of available analysis info,
// used by a pass manager to manage execution order of passes. It also provides
// a place to implement common pass manager APIs. All pass managers derive from
// PMDataManager.
//
// [o] class FunctionPassManager;
//
// This is a external interface used to manage FunctionPasses. This
// interface relies on FunctionPassManagerImpl to do all the tasks.
//
// [o] class FunctionPassManagerImpl : public ModulePass, PMDataManager,
//                                     public PMTopLevelManager;
//
// FunctionPassManagerImpl is a top level manager. It manages FPPassManagers
//
// [o] class FPPassManager : public ModulePass, public PMDataManager;
//
// FPPassManager manages FunctionPasses and BBPassManagers
//
// [o] class MPPassManager : public Pass, public PMDataManager;
//
// MPPassManager manages ModulePasses and FPPassManagers
//
// [o] class PassManager;
//
// This is a external interface used by various tools to manages passes. It
// relies on PassManagerImpl to do all the tasks.
//
// [o] class PassManagerImpl : public Pass, public PMDataManager,
//                             public PMTopLevelManager
//
// PassManagerImpl is a top level pass manager responsible for managing
// MPPassManagers.
//===----------------------------------------------------------------------===//

#include "llvm/Support/PrettyStackTrace.h"

namespace llvm {
template <typename T> class ArrayRef;
class Module;
class StringRef;
class Value;
class PMDataManager;

/// Fragments used to format -debug-pass status messages.
enum PassDebuggingString {
  EXECUTION_MSG,   ///< Prefix for "Executing Pass '" + PassName
  MODIFICATION_MSG,///< Prefix for "Made Modification '" + PassName
  FREEING_MSG,     ///< Prefix for " Freeing Pass '" + PassName
  ON_FUNCTION_MSG, ///< Suffix for "' on Function '" + FunctionName + "'...\n"
  ON_MODULE_MSG,   ///< Suffix for "' on Module '" + ModuleName + "'...\n"
  ON_REGION_MSG,   ///< Suffix for "' on Region '" + Msg + "'...\n'"
  ON_LOOP_MSG,     ///< Suffix for "' on Loop '" + Msg + "'...\n'"
  ON_CG_MSG        ///< Suffix for "' on Call Graph Nodes '" + Msg + "'...\n'"
};

/// PassManagerPrettyStackEntry - This is used to print informative information
/// about what pass is running when/if a stack trace is generated.
class LLVM_ABI PassManagerPrettyStackEntry : public PrettyStackTraceEntry {
  Pass *P;
  Value *V;
  Module *M;

public:
  /// Construct an entry for a pass that may have released its memory.
  /// \param p Pass associated with this stack frame.
  explicit PassManagerPrettyStackEntry(Pass *p)
    : P(p), V(nullptr), M(nullptr) {}  // When P is releaseMemory'd.
  /// Construct an entry for a pass running on a Value.
  /// \param p Pass associated with this stack frame.
  /// \param v Value the pass is running on.
  PassManagerPrettyStackEntry(Pass *p, Value &v)
    : P(p), V(&v), M(nullptr) {} // When P is run on V
  /// Construct an entry for a pass running on a Module.
  /// \param p Pass associated with this stack frame.
  /// \param m Module the pass is running on.
  PassManagerPrettyStackEntry(Pass *p, Module &m)
    : P(p), V(nullptr), M(&m) {} // When P is run on M

  /// print - Emit information about this stack frame to OS.
  /// \param OS Stream to write the stack-frame description to.
  void print(raw_ostream &OS) const override;
};

//===----------------------------------------------------------------------===//
// PMStack
//
/// PMStack - This class implements a stack data structure of PMDataManager
/// pointers.
///
/// Top level pass managers (see PassManager.cpp) maintain active Pass Managers
/// using PMStack. Each Pass implements assignPassManager() to connect itself
/// with appropriate manager. assignPassManager() walks PMStack to find
/// suitable manager.
class PMStack {
public:
  /// Reverse iterator over the active pass managers.
  typedef std::vector<PMDataManager *>::const_reverse_iterator iterator;
  /// Return an iterator to the top (most recently pushed) manager.
  /// \return An iterator to the top (most recently pushed) manager.
  iterator begin() const { return S.rbegin(); }
  /// Return an iterator past the bottom of the stack.
  /// \return An iterator past the bottom of the stack.
  iterator end() const { return S.rend(); }

  /// Remove the top pass manager from the stack.
  LLVM_ABI void pop();
  /// Return the top (most recently pushed) pass manager.
  /// \return The top (most recently pushed) pass manager.
  PMDataManager *top() const { return S.back(); }
  /// Push a pass manager onto the active stack.
  /// \param PM Pass manager to become the new top of the stack.
  LLVM_ABI void push(PMDataManager *PM);
  /// Return true if the stack contains no pass managers.
  /// \return True if the stack contains no pass managers.
  bool empty() const { return S.empty(); }

  /// Print the contents of the active pass-manager stack.
  LLVM_ABI void dump() const;

private:
  std::vector<PMDataManager *> S;
};

//===----------------------------------------------------------------------===//
// PMTopLevelManager
//
/// PMTopLevelManager manages LastUser info and collects common APIs used by
/// top level pass managers.
class LLVM_ABI PMTopLevelManager {
protected:
  /// Construct a top-level manager that owns the given data manager.
  /// \param PMDM Pass data manager used by this top-level manager.
  explicit PMTopLevelManager(PMDataManager *PMDM);

  /// Return the number of pass managers directly owned by this manager.
  /// \return The number of pass managers directly owned by this manager.
  unsigned getNumContainedManagers() const {
    return (unsigned)PassManagers.size();
  }

  /// Initialize analysis availability information for all managed passes.
  void initializeAllAnalysisInfo();

private:
  virtual PMDataManager *getAsPMDataManager() = 0;
  virtual PassManagerType getTopLevelPassManagerType() = 0;

public:
  /// Schedule pass P for execution.
  ///
  /// Make sure that passes required by P are run before P is run. Update
  /// analysis info maintained by the manager. Remove dead passes. This is a
  /// recursive function.
  /// \param P Pass to schedule for execution.
  void schedulePass(Pass *P);

  /// Set pass P as the last user of the given analysis passes.
  /// \param AnalysisPasses Analysis passes whose last user is updated.
  /// \param P Pass recorded as the last user of each analysis pass.
  void setLastUser(ArrayRef<Pass*> AnalysisPasses, Pass *P);

  /// Collect passes whose last user is P.
  /// \param LastUses Vector filled with passes last used by \p P.
  /// \param P Pass whose last-use set is collected.
  void collectLastUses(SmallVectorImpl<Pass *> &LastUses, Pass *P);

  /// Find the pass that implements Analysis AID.
  ///
  /// Search immutable passes and all pass managers. If desired pass is not
  /// found then return NULL.
  /// \param AID Analysis identity to look up.
  /// \return The pass that implements \p AID, or null if not found.
  Pass *findAnalysisPass(AnalysisID AID);

  /// Retrieve the PassInfo for an analysis.
  /// \param AID Analysis identity whose PassInfo is requested.
  /// \return The PassInfo for analysis \p AID, or null if not found.
  const PassInfo *findAnalysisPassInfo(AnalysisID AID) const;

  /// Find analysis usage information for the pass P.
  /// \param P Pass whose AnalysisUsage is looked up or created.
  /// \return Analysis usage information for \p P.
  AnalysisUsage *findAnalysisUsage(Pass *P);

  /// Destroy the top-level manager and its owned state.
  virtual ~PMTopLevelManager();

  /// Add immutable pass and initialize it.
  /// \param P Immutable pass to add and initialize.
  void addImmutablePass(ImmutablePass *P);

  /// Return the list of immutable passes managed by this top-level manager.
  /// \return The list of immutable passes managed by this top-level manager.
  inline SmallVectorImpl<ImmutablePass *>& getImmutablePasses() {
    return ImmutablePasses;
  }

  /// Add a pass manager that is directly maintained by this top-level manager.
  /// \param Manager Pass data manager to record.
  void addPassManager(PMDataManager *Manager) {
    PassManagers.push_back(Manager);
  }

  /// Add a manager not directly maintained by this top-level pass manager.
  /// \param Manager Indirect pass data manager to record.
  inline void addIndirectPassManager(PMDataManager *Manager) {
    IndirectPassManagers.push_back(Manager);
  }

  /// Print passes managed by this top level manager.
  void dumpPasses() const;
  /// Print command-line arguments for passes managed by this manager.
  void dumpArguments() const;

  /// Active pass managers currently on the assignment stack.
  PMStack activeStack;

protected:
  /// Collection of pass managers
  SmallVector<PMDataManager *, 8> PassManagers;

private:
  /// Collection of pass managers that are not directly maintained
  /// by this pass manager
  SmallVector<PMDataManager *, 8> IndirectPassManagers;

  // Map to keep track of last user of the analysis pass.
  // LastUser->second is the last user of Lastuser->first.
  // This is kept in sync with InversedLastUser.
  DenseMap<Pass *, Pass *> LastUser;

  // Map to keep track of passes that are last used by a pass.
  // This is kept in sync with LastUser.
  DenseMap<Pass *, SmallPtrSet<Pass *, 8> > InversedLastUser;

  /// Immutable passes are managed by top level manager.
  SmallVector<ImmutablePass *, 16> ImmutablePasses;

  /// Map from ID to immutable passes.
  SmallDenseMap<AnalysisID, ImmutablePass *, 8> ImmutablePassMap;


  /// A wrapper around AnalysisUsage for the purpose of uniqueing.  The wrapper
  /// is used to avoid needing to make AnalysisUsage itself a folding set node.
  struct AUFoldingSetNode : public FoldingSetNode {
    AnalysisUsage AU;
    AUFoldingSetNode(const AnalysisUsage &AU) : AU(AU) {}
    void Profile(FoldingSetNodeID &ID) const {
      Profile(ID, AU);
    }
    static void Profile(FoldingSetNodeID &ID, const AnalysisUsage &AU) {
      // TODO: We could consider sorting the dependency arrays within the
      // AnalysisUsage (since they are conceptually unordered).
      ID.AddBoolean(AU.getPreservesAll());
      auto ProfileVec = [&](const SmallVectorImpl<AnalysisID>& Vec) {
        ID.AddInteger(Vec.size());
        for(AnalysisID AID : Vec)
          ID.AddPointer(AID);
      };
      ProfileVec(AU.getRequiredSet());
      ProfileVec(AU.getRequiredTransitiveSet());
      ProfileVec(AU.getPreservedSet());
      ProfileVec(AU.getUsedSet());
    }
  };

  // Contains all of the unique combinations of AnalysisUsage.  This is helpful
  // when we have multiple instances of the same pass since they'll usually
  // have the same analysis usage and can share storage.
  FoldingSet<AUFoldingSetNode> UniqueAnalysisUsages;

  // Allocator used for allocating UAFoldingSetNodes.  This handles deletion of
  // all allocated nodes in one fell swoop.
  SpecificBumpPtrAllocator<AUFoldingSetNode> AUFoldingSetNodeAllocator;

  // Maps from a pass to it's associated entry in UniqueAnalysisUsages.  Does
  // not own the storage associated with either key or value..
  DenseMap<Pass *, AnalysisUsage*> AnUsageMap;

  /// Collection of PassInfo objects found via analysis IDs and in this top
  /// level manager. This is used to memoize queries to the pass registry.
  /// FIXME: This is an egregious hack because querying the pass registry is
  /// either slow or racy.
  mutable DenseMap<AnalysisID, const PassInfo *> AnalysisPassInfos;
};

//===----------------------------------------------------------------------===//
// PMDataManager

/// PMDataManager provides the common place to manage the analysis data
/// used by pass managers.
class LLVM_ABI PMDataManager {
public:
  /// Construct a pass data manager with empty analysis state.
  explicit PMDataManager() { initializeAnalysisInfo(); }

  /// Destroy the pass data manager.
  virtual ~PMDataManager();

  /// Return this manager as a Pass.
  /// \return This manager as a \c Pass.
  virtual Pass *getAsPass() = 0;

  /// Augment AvailableAnalysis by adding analysis made available by pass P.
  /// \param P Pass whose analysis results are recorded as available.
  void recordAvailableAnalysis(Pass *P);

  /// Verify analysis preserved by pass P.
  /// \param P Pass whose preserved analyses are verified.
  void verifyPreservedAnalysis(Pass *P);

  /// Remove Analysis that is not preserved by the pass.
  /// \param P Pass whose non-preserved analyses are removed.
  void removeNotPreservedAnalysis(Pass *P);

  /// Remove dead passes used by P.
  /// \param P Pass whose dead analysis users are freed.
  /// \param Msg Context string included in debug-pass output.
  /// \param DBG_STR Debug-pass message fragment describing the IR unit.
  void removeDeadPasses(Pass *P, StringRef Msg,
                        enum PassDebuggingString DBG_STR);

  /// Remove P.
  /// \param P Pass to free and remove from available analysis.
  /// \param Msg Context string included in debug-pass output.
  /// \param DBG_STR Debug-pass message fragment describing the IR unit.
  void freePass(Pass *P, StringRef Msg,
                enum PassDebuggingString DBG_STR);

  /// Add pass P into the PassVector.
  ///
  /// Update AvailableAnalysis appropriately if ProcessAnalysis is true.
  /// \param P Pass to add to this manager.
  /// \param ProcessAnalysis If true, update available-analysis state for \p P.
  void add(Pass *P, bool ProcessAnalysis = true);

  /// Add RequiredPass into list of lower level passes required by pass P.
  ///
  /// RequiredPass is run on the fly by Pass Manager when P requests it
  /// through getAnalysis interface.
  /// \param P Pass that requires the lower-level analysis.
  /// \param RequiredPass Lower-level pass to run on demand for \p P.
  virtual void addLowerLevelRequiredPass(Pass *P, Pass *RequiredPass);

  /// Return an on-the-fly analysis pass instance for \p P.
  /// \param P Pass requesting the on-the-fly analysis.
  /// \param PI Analysis identity of the requested pass.
  /// \param F Function the on-the-fly pass should run on.
  /// \return A tuple of the requested analysis pass and whether \p F changed.
  virtual std::tuple<Pass *, bool> getOnTheFlyPass(Pass *P, AnalysisID PI,
                                                   Function &F);

  /// Initialize available analysis information.
  void initializeAnalysisInfo() {
    AvailableAnalysis.clear();
    llvm::fill(InheritedAnalysis, nullptr);
  }

  /// Return true if P preserves higher-level analysis used by this manager.
  /// \param P Pass whose preservation of higher-level analyses is tested.
  /// \return True if \p P preserves higher-level analysis used by this manager.
  bool preserveHigherLevelAnalysis(Pass *P);

  /// Collect available used analyses and missing required analyses for P.
  ///
  /// Populate UsedPasses with analysis pass that are used or required by pass
  /// P and are available. Populate ReqPassNotAvailable with analysis pass that
  /// are required by pass P but are not available.
  /// \param UsedPasses Available analyses used or required by \p P.
  /// \param ReqPassNotAvailable Required analysis IDs that are not available.
  /// \param P Pass whose required and used analyses are inspected.
  void collectRequiredAndUsedAnalyses(
      SmallVectorImpl<Pass *> &UsedPasses,
      SmallVectorImpl<AnalysisID> &ReqPassNotAvailable, Pass *P);

  /// Fill P's AnalysisImpls from currently available required analyses.
  ///
  /// All Required analyses should be available to the pass as it runs!  Here
  /// we fill in the AnalysisImpls member of the pass so that it can
  /// successfully use the getAnalysis() method to retrieve the
  /// implementations it needs.
  /// \param P Pass whose AnalysisImpls are initialized.
  void initializeAnalysisImpl(Pass *P);

  /// Find the pass that implements Analysis AID.
  ///
  /// If desired pass is not found then return NULL.
  /// \param AID Analysis identity to look up.
  /// \param Direction If true, also search parent managers via the top-level
  ///        manager when the analysis is not locally available.
  /// \return The pass that implements \p AID, or null if not found.
  Pass *findAnalysisPass(AnalysisID AID, bool Direction);

  /// Return the top-level manager that owns this data manager.
  /// \return The top-level manager that owns this data manager.
  PMTopLevelManager *getTopLevelManager() { return TPM; }
  /// Set the top-level manager that owns this data manager.
  /// \param T Top-level manager to associate with this data manager.
  void setTopLevelManager(PMTopLevelManager *T) { TPM = T; }

  /// Return the nesting depth of this pass manager.
  /// \return The nesting depth of this pass manager.
  unsigned getDepth() const { return Depth; }
  /// Set the nesting depth of this pass manager.
  /// \param newDepth Nesting depth to assign to this manager.
  void setDepth(unsigned newDepth) { Depth = newDepth; }

  /// Print last-use information for pass P.
  /// \param P Pass whose last uses are dumped.
  /// \param Offset Indentation offset for the dump output.
  void dumpLastUses(Pass *P, unsigned Offset) const;
  /// Print command-line arguments for passes managed here.
  void dumpPassArguments() const;
  /// Print debug-pass status for pass P.
  /// \param P Pass whose status is dumped.
  /// \param S1 Leading debug-pass message fragment.
  /// \param S2 Trailing debug-pass message fragment.
  /// \param Msg Context string included in the dump.
  void dumpPassInfo(Pass *P, enum PassDebuggingString S1,
                    enum PassDebuggingString S2, StringRef Msg);
  /// Print the required analysis set for pass P.
  /// \param P Pass whose required set is dumped.
  void dumpRequiredSet(const Pass *P) const;
  /// Print the preserved analysis set for pass P.
  /// \param P Pass whose preserved set is dumped.
  void dumpPreservedSet(const Pass *P) const;
  /// Print the used analysis set for pass P.
  /// \param P Pass whose used set is dumped.
  void dumpUsedSet(const Pass *P) const;

  /// Return the number of passes contained by this manager.
  /// \return The number of passes contained by this manager.
  unsigned getNumContainedPasses() const {
    return (unsigned)PassVector.size();
  }

  /// Return the kind of pass manager represented by this object.
  /// \return The kind of pass manager represented by this object.
  virtual PassManagerType getPassManagerType() const {
    assert ( 0 && "Invalid use of getPassManagerType");
    return PMT_Unknown;
  }

  /// Return the map of analyses currently available to this manager.
  /// \return The map of analyses currently available to this manager.
  DenseMap<AnalysisID, Pass*> *getAvailableAnalysis() {
    return &AvailableAnalysis;
  }

  /// Collect AvailableAnalysis from all the active Pass Managers.
  /// \param PMS Active pass-manager stack whose analyses are inherited.
  void populateInheritedAnalysis(PMStack &PMS) {
    unsigned Index = 0;
    for (PMDataManager *PMDM : PMS)
      InheritedAnalysis[Index++] = PMDM->getAvailableAnalysis();
  }

  /// Set the initial size of the module if the user has specified that they
  /// want remarks for size.
  ///
  /// \param M Module whose per-function instruction counts are recorded.
  /// \param FunctionToInstrCount Map filled with per-function IR counts.
  /// \return The total IR instruction count of \p M.
  unsigned initSizeRemarkInfo(
      Module &M,
      StringMap<std::pair<unsigned, unsigned>> &FunctionToInstrCount);

  /// Emit a remark when the module's IR instruction count changes.
  ///
  /// \p F is optionally passed by passes which run on Functions, and thus
  /// always know whether or not a non-empty function is available.
  ///
  /// \p FunctionToInstrCount maps the name of a \p Function to a pair. The
  /// first member of the pair is the IR count of the \p Function before running
  /// \p P, and the second member is the IR count of the \p Function after
  /// running \p P.
  /// \param P Pass that produced the instruction-count change.
  /// \param M Module whose instruction count changed.
  /// \param Delta Signed change in module instruction count.
  /// \param CountBefore Module instruction count before running \p P.
  /// \param FunctionToInstrCount Per-function before/after IR counts.
  /// \param F Optional function the pass ran on, if known.
  void emitInstrCountChangedRemark(
      Pass *P, Module &M, int64_t Delta, unsigned CountBefore,
      StringMap<std::pair<unsigned, unsigned>> &FunctionToInstrCount,
      Function *F = nullptr);

protected:
  /// Top level manager that owns this data manager.
  PMTopLevelManager *TPM = nullptr;

  /// Collection of passes managed by this manager.
  SmallVector<Pass *, 16> PassVector;

  /// Analysis maps inherited from parent pass managers on the active stack.
  ///
  /// At any time there can not be more then PMT_Last active pass managers.
  DenseMap<AnalysisID, Pass *> *InheritedAnalysis[PMT_Last];

  /// isPassDebuggingExecutionsOrMore - Return true if -debug-pass=Executions
  /// or higher is specified.
  /// \return True if \c -debug-pass=Executions or higher is specified.
  bool isPassDebuggingExecutionsOrMore() const;

private:
  void dumpAnalysisUsage(StringRef Msg, const Pass *P,
                         const AnalysisUsage::VectorType &Set) const;

  // Set of available Analysis. This information is used while scheduling
  // pass. If a pass requires an analysis which is not available then
  // the required analysis pass is scheduled to run before the pass itself is
  // scheduled to run.
  DenseMap<AnalysisID, Pass*> AvailableAnalysis;

  // Collection of higher level analysis used by the pass managed by
  // this manager.
  SmallVector<Pass *, 16> HigherLevelAnalysis;

  unsigned Depth = 0;
};

//===----------------------------------------------------------------------===//
// FPPassManager
//
/// FPPassManager manages BBPassManagers and FunctionPasses.
///
/// It batches all function passes and basic block pass managers together and
/// sequences them to process one function at a time before processing next
/// function.
class LLVM_ABI FPPassManager : public ModulePass, public PMDataManager {
public:
  /// Pass identification, replacement for typeid.
  static char ID;
  /// Construct a function pass manager pass.
  explicit FPPassManager() : ModulePass(ID) {}

  /// Execute all of the passes scheduled for execution on F.
  ///
  /// Keep track of whether any of the passes modifies the function, and if so,
  /// return true.
  /// \param F Function to run contained passes on.
  /// \return True if any contained pass modified the function.
  bool runOnFunction(Function &F);
  /// Execute contained passes on every function in module M.
  /// \param M Module whose functions are processed.
  /// \return True if any contained pass modified the module.
  bool runOnModule(Module &M) override;

  /// cleanup - After running all passes, clean up pass manager cache.
  void cleanup();

  /// doInitialization - Overrides ModulePass doInitialization for global
  /// initialization tasks
  ///
  using ModulePass::doInitialization;

  /// doInitialization - Run all of the initializers for the function passes.
  ///
  /// \param M Module used for function-pass initialization.
  /// \return True if any initializer modified the module.
  bool doInitialization(Module &M) override;

  /// doFinalization - Overrides ModulePass doFinalization for global
  /// finalization tasks
  ///
  using ModulePass::doFinalization;

  /// doFinalization - Run all of the finalizers for the function passes.
  ///
  /// \param M Module used for function-pass finalization.
  /// \return True if any finalizer modified the module.
  bool doFinalization(Module &M) override;

  /// Return this manager as a PMDataManager.
  /// \return This manager as a \c PMDataManager.
  PMDataManager *getAsPMDataManager() override { return this; }
  /// Return this manager as a Pass.
  /// \return This manager as a \c Pass.
  Pass *getAsPass() override { return this; }

  /// Pass Manager itself does not invalidate any analysis info.
  /// \param Info Analysis usage updated to preserve all analyses.
  void getAnalysisUsage(AnalysisUsage &Info) const override {
    Info.setPreservesAll();
  }

  /// Print the structure of passes managed by this manager.
  /// \param Offset Indentation offset for the dump output.
  void dumpPassStructure(unsigned Offset) override;

  /// Return the name of this pass manager.
  /// \return The name of this pass manager.
  StringRef getPassName() const override { return "Function Pass Manager"; }

  /// Return the Nth contained function pass.
  /// \param N Zero-based index of the contained pass.
  /// \return The function pass at index \p N.
  FunctionPass *getContainedPass(unsigned N) {
    assert ( N < PassVector.size() && "Pass number out of range!");
    FunctionPass *FP = static_cast<FunctionPass *>(PassVector[N]);
    return FP;
  }

  /// Return that this is a function pass manager.
  /// \return \c PMT_FunctionPassManager.
  PassManagerType getPassManagerType() const override {
    return PMT_FunctionPassManager;
  }
};
}

#endif
