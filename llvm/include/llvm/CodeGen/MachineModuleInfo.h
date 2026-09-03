//===-- llvm/CodeGen/MachineModuleInfo.h ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Collect meta information for a module.  This information should be in a
// neutral form that can be used by different debugging and exception handling
// schemes.
//
// The organization of information is primarily clustered around the source
// compile units.  The main exception is source line correspondence where
// inlining may interleave code from various compile units.
//
// The following information can be retrieved from the MachineModuleInfo.
//
//  -- Source directories - Directories are uniqued based on their canonical
//     string and assigned a sequential numeric ID (base 1.)
//  -- Source files - Files are also uniqued based on their name and directory
//     ID.  A file ID is sequential number (base 1.)
//  -- Source line correspondence - A vector of file ID, line#, column# triples.
//     A DEBUG_LOCATION instruction is generated  by the DAG Legalizer
//     corresponding to each entry in the source line list.  This allows a debug
//     emitter to generate labels referenced by debug information tables.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEMODULEINFO_H
#define LLVM_CODEGEN_MACHINEMODULEINFO_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/EquivalenceClasses.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/IR/PassManager.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Pass.h"
#include "llvm/Support/Compiler.h"
#include <memory>
#include <utility>
#include <vector>

namespace llvm {

class Function;
class TargetMachine;
class MachineFunction;
class Module;

//===----------------------------------------------------------------------===//
/// Base class for target-specific per-module object-file information.
///
/// This class can be derived from and used by targets to hold private
/// target-specific information for each Module.  Objects of type are
/// accessed/created with MachineModuleInfo::getObjFileInfo and destroyed when
/// the MachineModuleInfo is destroyed.
///
class LLVM_ABI MachineModuleInfoImpl {
public:
  /// Pair of an MCSymbol and a one-bit flag used as a stub value.
  using StubValueTy = PointerIntPair<MCSymbol *, 1, bool>;
  /// Sorted list of MCSymbol stubs paired with their StubValueTy.
  using SymbolListTy = std::vector<std::pair<MCSymbol *, StubValueTy>>;

  /// A variant of SymbolListTy where the stub is a generalized MCExpr.
  using ExprStubListTy = std::vector<std::pair<MCSymbol *, const MCExpr *>>;

  /// Destroy this target-specific module info object.
  virtual ~MachineModuleInfoImpl();

protected:
  /// Return the entries from a DenseMap in a deterministic sorted orer.
  /// Clears the map.
  ///
  /// \param Map DenseMap of stub symbols to clear and sort.
  /// \return Stub entries sorted in a deterministic order.
  static SymbolListTy getSortedStubs(DenseMap<MCSymbol *, StubValueTy> &Map);

  /// Return the entries from a DenseMap in a deterministic sorted orer.
  /// Clears the map.
  ///
  /// \param Map DenseMap of expression stubs to clear and sort.
  /// \return Expression stub entries sorted in a deterministic order.
  static ExprStubListTy
  getSortedExprStubs(DenseMap<MCSymbol *, const MCExpr *> &Map);
};

//===----------------------------------------------------------------------===//
/// Holds module-level metadata for code generation.
///
/// This class contains meta information specific to a module.  Queries can be
/// made by different debugging and exception handling schemes and reformated
/// for specific use.
///
class MachineModuleInfo {
  friend class MachineModuleInfoWrapperPass;
  friend class MachineModuleAnalysis;

  const TargetMachine &TM;

  /// This is the MCContext used for the entire code generator.
  MCContext Context;
  // This is an external context, that if assigned, will be used instead of the
  // internal context.
  MCContext *ExternalContext = nullptr;

  /// This is the LLVM Module being worked on.
  const Module *TheModule = nullptr;

  /// This is the object-file-format-specific implementation of
  /// MachineModuleInfoImpl, which lets targets accumulate whatever info they
  /// want.
  MachineModuleInfoImpl *ObjFileMMI;

  /// Maps IR Functions to their corresponding MachineFunctions.
  DenseMap<const Function*, std::unique_ptr<MachineFunction>> MachineFunctions;
  /// Next unique number available for a MachineFunction.
  unsigned NextFnNum = 0;
  const Function *LastRequest = nullptr; ///< Used for shortcut/cache.
  MachineFunction *LastResult = nullptr; ///< Used for shortcut/cache.

  // MachineFunctions are freed only when all the functions in the same
  // deletion grouping have been finalized.
  EquivalenceClasses<const Function *> MFDeletionGrouping;
  // Add to this set once a function has been fully processed.
  DenseSet<const Function *> FinalizedMFs;

  MachineModuleInfo &operator=(MachineModuleInfo &&MMII) = delete;

public:
  /// Construct MachineModuleInfo for optional target machine \p TM.
  ///
  /// \param TM Target machine, or nullptr if none is provided.
  LLVM_ABI explicit MachineModuleInfo(const TargetMachine *TM = nullptr);

  /// Construct MachineModuleInfo using an external MCContext.
  ///
  /// \param TM Target machine for this module.
  /// \param ExtContext External MCContext to use instead of an owned one.
  LLVM_ABI explicit MachineModuleInfo(const TargetMachine *TM,
                                      MCContext *ExtContext);

  /// Move-construct MachineModuleInfo from \p MMII.
  ///
  /// \param MMII Source MachineModuleInfo to move from.
  LLVM_ABI MachineModuleInfo(MachineModuleInfo &&MMII);

  /// Destroy this MachineModuleInfo and owned MachineFunctions.
  LLVM_ABI ~MachineModuleInfo();

  /// Initialize per-module state for a new Module.
  LLVM_ABI void initialize();
  /// Finalize and tear down per-module state.
  LLVM_ABI void finalize();

  /// Return the target machine associated with this module info.
  ///
  /// \return Const reference to the associated TargetMachine.
  const TargetMachine &getTarget() const { return TM; }

  /// Return the MCContext used for code generation.
  ///
  /// \return Const reference to the MCContext in use.
  const MCContext &getContext() const {
    return ExternalContext ? *ExternalContext : Context;
  }
  /// Return the MCContext used for code generation.
  ///
  /// \return Reference to the MCContext in use.
  MCContext &getContext() {
    return ExternalContext ? *ExternalContext : Context;
  }

  /// Return the LLVM Module currently being worked on.
  ///
  /// \return Pointer to the Module being processed, or nullptr if none.
  const Module *getModule() const { return TheModule; }

  /// Return the MachineFunction for IR function \p F, creating one if needed.
  ///
  /// Creates a new MachineFunction if none exists yet.
  /// NOTE: New pass manager clients shall not use this method to get
  /// the `MachineFunction`, use `MachineFunctionAnalysis` instead.
  ///
  /// \param F IR function whose MachineFunction is requested.
  /// \return Existing or newly created MachineFunction for \p F.
  LLVM_ABI MachineFunction &getOrCreateMachineFunction(Function &F);

  /// Return the MachineFunction for IR function \p F, or nullptr if none.
  ///
  /// NOTE: New pass manager clients shall not use this method to get
  /// the `MachineFunction`, use `MachineFunctionAnalysis` instead.
  ///
  /// \param F IR function whose MachineFunction is requested.
  /// \return Associated MachineFunction, or nullptr if none exists.
  LLVM_ABI MachineFunction *getMachineFunction(const Function &F) const;

  /// Group two IR functions so their MachineFunctions are deleted together once
  /// both functions have been finalized.
  ///
  /// \param F1 First IR function to group for joint deletion.
  /// \param F2 Second IR function to group for joint deletion.
  void groupMachineFunctionsForDeletion(const Function &F1,
                                        const Function &F2) {
    if (FinalizedMFs.count(&F1) || FinalizedMFs.count(&F2))
      return;
    MFDeletionGrouping.unionSets(&F1, &F2);
  }

  /// Delete the MachineFunction for IR function \p F when its group is done.
  ///
  /// When a function is not grouped with any other function, its MF gets
  /// deleted right away. When a function is grouped with other functions, its
  /// MF gets deleted when all functions in the same group have been finalized.
  /// Also resets the link in the IR Function to Machine Function map.
  ///
  /// \param F IR function whose MachineFunction should be deleted.
  LLVM_ABI void deleteMachineFunctionFor(Function &F);

  /// Add an externally created MachineFunction \p MF for \p F.
  ///
  /// \param F IR function that owns the MachineFunction.
  /// \param MF Externally created MachineFunction to insert.
  LLVM_ABI void insertFunction(const Function &F,
                               std::unique_ptr<MachineFunction> &&MF);

  /// Return the target-specific object-file info of type \p Ty, creating it.
  ///
  /// Keep track of various per-module pieces of information for backends
  /// that would like to do so.
  ///
  /// \return Reference to the target-specific MachineModuleInfoImpl of type Ty.
  template<typename Ty>
  Ty &getObjFileInfo() {
    if (ObjFileMMI == nullptr)
      ObjFileMMI = new Ty(*this);
    return *static_cast<Ty*>(ObjFileMMI);
  }

  /// Return the const target-specific object-file info of type \p Ty.
  ///
  /// \return Const reference to the target-specific MachineModuleInfoImpl of
  /// type Ty.
  template<typename Ty>
  const Ty &getObjFileInfo() const {
    return const_cast<MachineModuleInfo*>(this)->getObjFileInfo<Ty>();
  }

  /// \}
}; // End class MachineModuleInfo

/// ImmutablePass wrapper that owns a MachineModuleInfo.
class LLVM_ABI MachineModuleInfoWrapperPass : public ImmutablePass {
  MachineModuleInfo MMI;

public:
  /// Pass identification, replacement for typeid.
  static char ID;
  /// Construct a wrapper pass with optional target machine \p TM.
  ///
  /// \param TM Target machine, or nullptr if none is provided.
  explicit MachineModuleInfoWrapperPass(const TargetMachine *TM = nullptr);

  /// Construct a wrapper pass using an external MCContext.
  ///
  /// \param TM Target machine for the owned MachineModuleInfo.
  /// \param ExtContext External MCContext to use instead of an owned one.
  explicit MachineModuleInfoWrapperPass(const TargetMachine *TM,
                                        MCContext *ExtContext);

  /// Initialize the owned MachineModuleInfo for \p M.
  ///
  /// \param M Module being processed.
  /// \return False; this pass does not modify the Module.
  bool doInitialization(Module &M) override;
  /// Finalize the owned MachineModuleInfo for \p M.
  ///
  /// \param M Module being processed.
  /// \return False; this pass does not modify the Module.
  bool doFinalization(Module &M) override;

  /// Return the owned MachineModuleInfo.
  ///
  /// \return Reference to the owned MachineModuleInfo.
  MachineModuleInfo &getMMI() { return MMI; }
  /// Return the owned MachineModuleInfo.
  ///
  /// \return Const reference to the owned MachineModuleInfo.
  const MachineModuleInfo &getMMI() const { return MMI; }
};

/// Analysis that produces MachineModuleInfo for a module.
///
/// This does not produce its own MachineModuleInfo because we need a consistent
/// MachineModuleInfo to keep ownership of MachineFunctions regardless of
/// analysis invalidation/clearing. So something outside the analysis
/// infrastructure must own the MachineModuleInfo.
class MachineModuleAnalysis : public AnalysisInfoMixin<MachineModuleAnalysis> {
  friend AnalysisInfoMixin<MachineModuleAnalysis>;
  LLVM_ABI static AnalysisKey Key;

  MachineModuleInfo &MMI;

public:
  /// Cached analysis result providing access to MachineModuleInfo.
  class Result {
    MachineModuleInfo &MMI;
    Result(MachineModuleInfo &MMI) : MMI(MMI) {}
    friend class MachineModuleAnalysis;

  public:
    /// Return the MachineModuleInfo referenced by this result.
    ///
    /// \return Reference to the MachineModuleInfo owned outside the analysis.
    MachineModuleInfo &getMMI() { return MMI; }

    /// Always preserve this result; MMI owns the MCContext.
    ///
    /// MMI owes MCContext. It should never be invalidated.
    ///
    /// \param M Module for which invalidation is queried (unused).
    /// \param PA Set of analyses preserved by the last transformation.
    /// \param Inv Invalidator for other module analyses (unused).
    /// \return Always false; this result is never discarded.
    bool invalidate(Module &M, const PreservedAnalyses &PA,
                    ModuleAnalysisManager::Invalidator &Inv) {
      return false;
    }
  };

  /// Construct an analysis that exposes externally owned \p MMI.
  ///
  /// \param MMI MachineModuleInfo owned outside the analysis manager.
  MachineModuleAnalysis(MachineModuleInfo &MMI) : MMI(MMI) {}

  /// Run the analysis pass and produce machine module information.
  ///
  /// \param M Module for which MachineModuleInfo is requested.
  /// \param MAM Module analysis manager (unused).
  /// \return Result referencing the externally owned MachineModuleInfo.
  LLVM_ABI Result run(Module &M, ModuleAnalysisManager &MAM);
};

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINEMODULEINFO_H
