//===- SCCPSolver.h - SCCP Utility ----------------------------- *- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// \file
// This file implements Sparse Conditional Constant Propagation (SCCP) utility.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_SCCPSOLVER_H
#define LLVM_TRANSFORMS_UTILS_SCCPSOLVER_H

#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/DomTreeUpdater.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Transforms/Utils/PredicateInfo.h"
#include <vector>

namespace llvm {
class Argument;
class BasicBlock;
class CallInst;
class Constant;
class DataLayout;
class DominatorTree;
class Function;
class GlobalVariable;
class Instruction;
class LLVMContext;
class StructType;
class TargetLibraryInfo;
class Value;
class ValueLatticeElement;

/// Helper struct shared between Function Specialization and SCCP Solver.
struct ArgInfo {
  /// Formal argument being analysed.
  Argument *Formal;
  /// Corresponding actual constant argument.
  Constant *Actual;

  /// Construct from a formal argument and its actual constant.
  /// \param F Formal argument being analysed.
  /// \param A Corresponding actual constant argument.
  ArgInfo(Argument *F, Constant *A) : Formal(F), Actual(A) {}

  /// Return true if both formal and actual match \p Other.
  /// \param Other Other ArgInfo to compare against.
  /// \return True if both formal and actual match \p Other.
  bool operator==(const ArgInfo &Other) const {
    return Formal == Other.Formal && Actual == Other.Actual;
  }

  /// Return true if this differs from \p Other in formal or actual.
  /// \param Other Other ArgInfo to compare against.
  /// \return True if this differs from \p Other in formal or actual.
  bool operator!=(const ArgInfo &Other) const { return !(*this == Other); }

  /// Combine hashes of the formal and actual for use in hash-based containers.
  /// \param A ArgInfo whose members are hashed.
  /// \return Combined hash of the formal and actual members.
  friend hash_code hash_value(const ArgInfo &A) {
    return hash_combine(hash_value(A.Formal), hash_value(A.Actual));
  }
};

/// Instruction visitor that drives Sparse Conditional Constant Propagation.
class SCCPInstVisitor;

//===----------------------------------------------------------------------===//
//
/// SCCPSolver - This interface class is a general purpose solver for Sparse
/// Conditional Constant Propagation (SCCP).
///
class SCCPSolver {
  std::unique_ptr<SCCPInstVisitor> Visitor;

public:
  /// Construct an SCCP solver for the given data layout and TLI callback.
  /// \param DL Data layout of the module being analysed.
  /// \param GetTLI Callback returning the TargetLibraryInfo for a function.
  /// \param Ctx LLVM context used by the solver.
  LLVM_ABI
  SCCPSolver(const DataLayout &DL,
             std::function<const TargetLibraryInfo &(Function &)> GetTLI,
             LLVMContext &Ctx);

  /// Destroy the SCCP solver and its visitor.
  LLVM_ABI ~SCCPSolver();

  /// Return the data layout used by this solver.
  /// \return Data layout used by this solver.
  LLVM_ABI const DataLayout &getDataLayout() const;

  /// Build PredicateInfo for \p F and register it with the solver.
  /// \param F Function to analyse.
  /// \param DT Dominator tree for \p F.
  /// \param AC Assumption cache for \p F.
  LLVM_ABI void addPredicateInfo(Function &F, DominatorTree &DT,
                                 AssumptionCache &AC);

  /// Remove PredicateInfo SSA copies from \p F after solving.
  /// \param F Function whose PredicateInfo copies should be cleaned up.
  LLVM_ABI void removeSSACopies(Function &F);

  /// Mark \p BB executable if it was not already considered live.
  ///
  /// Clients use this to mark blocks known to be intrinsically live in the
  /// processed unit.
  /// \param BB Block to mark executable.
  /// \return True if the block was not considered live before.
  LLVM_ABI bool markBlockExecutable(BasicBlock *BB);

  /// Return the PredicateInfo associated with \p I, if any.
  /// \param I Instruction to look up.
  /// \return PredicateInfo for \p I, or nullptr if none is registered.
  LLVM_ABI const PredicateBase *getPredicateInfoFor(Instruction *I);

  /// Track loads and stores to \p GV during interprocedural SCCP.
  ///
  /// Clients can use this method to inform the SCCPSolver that it should track
  /// loads and stores to the specified global variable if it can. This is only
  /// legal to call if performing Interprocedural SCCP.
  /// \param GV Global variable to track.
  LLVM_ABI void trackValueOfGlobalVariable(GlobalVariable *GV);

  /// Register \p F so calls into and out of it are tracked.
  ///
  /// If the SCCP solver is supposed to track calls into and out of the
  /// specified function (which cannot have its address taken), this method must
  /// be called.
  /// \param F Function to track.
  LLVM_ABI void addTrackedFunction(Function *F);

  /// Add function to the list of functions whose return cannot be modified.
  /// \param F Function whose return must be preserved.
  LLVM_ABI void addToMustPreserveReturnsInFunctions(Function *F);

  /// Returns true if the return of the given function cannot be modified.
  /// \param F Function to query.
  /// \return True if the return of \p F cannot be modified.
  LLVM_ABI bool mustPreserveReturn(Function *F);

  /// Register \p F so its arguments are tracked by the solver.
  /// \param F Function whose arguments should be tracked.
  LLVM_ABI void addArgumentTrackedFunction(Function *F);

  /// Returns true if the given function is in the solver's set of
  /// argument-tracked functions.
  /// \param F Function to query.
  /// \return True if \p F is argument-tracked by the solver.
  LLVM_ABI bool isArgumentTrackedFunction(Function *F);

  /// Return the set of functions whose arguments are tracked.
  /// \return Set of functions whose arguments are tracked by the solver.
  LLVM_ABI const SmallPtrSetImpl<Function *> &
  getArgumentTrackedFunctions() const;

  /// Solve - Solve for constants and executable blocks.
  LLVM_ABI void solve();

  /// Resolve undef branch assumptions after solving dataflow for \p F.
  ///
  /// While solving the dataflow for a function, we assume that branches on
  /// undef values cannot reach any of their successors. However, this is not a
  /// safe assumption. After we solve dataflow, this method should be used to
  /// handle this. If this returns true, the solver should be rerun.
  /// \param F Function whose undef uses should be resolved.
  /// \return True if undefs were resolved and the solver should be rerun.
  LLVM_ABI bool resolvedUndefsIn(Function &F);

  /// Solve and resolve undefs repeatedly until a fixed point for module \p M.
  /// \param M Module whose functions are solved.
  LLVM_ABI void solveWhileResolvedUndefsIn(Module &M);

  /// Solve and resolve undefs repeatedly for functions in \p WorkList.
  /// \param WorkList Functions to solve until undef resolution is stable.
  LLVM_ABI void
  solveWhileResolvedUndefsIn(SmallVectorImpl<Function *> &WorkList);

  /// Solve and resolve invalidated undefs until a fixed point.
  LLVM_ABI void solveWhileResolvedUndefs();

  /// Return true if \p BB is currently marked executable.
  /// \param BB Block to query.
  /// \return True if \p BB is currently marked executable.
  LLVM_ABI bool isBlockExecutable(BasicBlock *BB) const;

  /// Return true if the control-flow edge from \p From to \p To is feasible.
  /// \param From Source basic block of the edge.
  /// \param To Destination basic block of the edge.
  /// \return True if the edge from \p From to \p To is feasible.
  LLVM_ABI bool isEdgeFeasible(BasicBlock *From, BasicBlock *To) const;

  /// Return the lattice values for each element of struct-typed value \p V.
  /// \param V Struct-typed value to query.
  /// \return Lattice value for each element of struct-typed \p V.
  LLVM_ABI std::vector<ValueLatticeElement>
  getStructLatticeValueFor(Value *V) const;

  /// Erase the lattice value stored for \p V.
  /// \param V Value whose lattice entry should be removed.
  LLVM_ABI void removeLatticeValueFor(Value *V);

  /// Invalidate the Lattice Value of \p Call and its users after specializing
  /// the call. Then recompute it.
  /// \param Call Call whose lattice value should be reset.
  LLVM_ABI void resetLatticeValueFor(CallBase *Call);

  /// Return the lattice value inferred for non-struct value \p V.
  /// \param V Value to query.
  /// \return Lattice value inferred for \p V.
  LLVM_ABI const ValueLatticeElement &getLatticeValueFor(Value *V) const;

  /// getTrackedRetVals - Get the inferred return value map.
  /// \return Map from tracked functions to their inferred return lattice values.
  LLVM_ABI const MapVector<Function *, ValueLatticeElement> &
  getTrackedRetVals() const;

  /// getTrackedGlobals - Get and return the set of inferred initializers for
  /// global variables.
  /// \return Map from tracked globals to their inferred lattice values.
  LLVM_ABI const DenseMap<GlobalVariable *, ValueLatticeElement> &
  getTrackedGlobals() const;

  /// getMRVFunctionsTracked - Get the set of functions which return multiple
  /// values tracked by the pass.
  /// \return Set of multi-return-value functions tracked by the solver.
  LLVM_ABI const SmallPtrSet<Function *, 16> &getMRVFunctionsTracked() const;

  /// markOverdefined - Mark the specified value overdefined.  This
  /// works with both scalars and structs.
  /// \param V Value to mark overdefined.
  LLVM_ABI void markOverdefined(Value *V);

  /// trackValueOfArgument - Mark the specified argument overdefined unless it
  /// have range attribute.  This works with both scalars and structs.
  /// \param V Argument to track.
  LLVM_ABI void trackValueOfArgument(Argument *V);

  /// Return true if every lattice element of struct return type \p STy is
  /// constant for function \p F.
  /// \param F Function whose return lattice is queried.
  /// \param STy Struct return type whose elements are checked.
  /// \return True if every element of \p STy is constant for \p F.
  LLVM_ABI bool isStructLatticeConstant(Function *F, StructType *STy);

  /// Helper to return a Constant if \p LV is either a constant or a constant
  /// range with a single element.
  /// \param LV Lattice value to convert.
  /// \param Ty Type of the constant to produce.
  /// \return A Constant of type \p Ty, or nullptr if \p LV is not a constant.
  LLVM_ABI Constant *getConstant(const ValueLatticeElement &LV, Type *Ty) const;

  /// Return either a Constant or nullptr for a given Value.
  /// \param V Value whose constant form is requested.
  /// \return A Constant if one can be inferred, otherwise nullptr.
  LLVM_ABI Constant *getConstantOrNull(Value *V) const;

  /// Set lattice values for arguments of specialized function \p F.
  ///
  /// If an argument is Constant then its lattice value is marked with the
  /// corresponding actual argument in \p Args. Otherwise, its lattice value is
  /// inherited (copied) from the corresponding formal argument in \p Args.
  /// \param F Specialized function whose arguments are initialized.
  /// \param Args Formal/actual pairs describing the specialization.
  LLVM_ABI void setLatticeValueForSpecializationArguments(
      Function *F, const SmallVectorImpl<ArgInfo> &Args);

  /// Mark every block in \p F non-executable.
  ///
  /// Clients can use this method to erase a function from the module (e.g., if
  /// it has been completely specialized and is no longer needed).
  /// \param F Function whose blocks are marked non-executable.
  LLVM_ABI void markFunctionUnreachable(Function *F);

  /// Visit \p I to update lattice values and executable edges.
  /// \param I Instruction to process.
  LLVM_ABI void visit(Instruction *I);

  /// Visit call \p I to propagate argument and return lattice values.
  /// \param I Call instruction to process.
  LLVM_ABI void visitCall(CallInst &I);

  /// Simplify instructions in \p BB using inferred constants and ranges.
  /// \param BB Block whose instructions are simplified.
  /// \param InsertedValues Set of values inserted by prior simplifications.
  /// \param InstRemovedStat Statistic incremented when instructions are erased.
  /// \param InstReplacedStat Statistic incremented when instructions are replaced.
  /// \return True if any instruction in the block was changed.
  LLVM_ABI bool simplifyInstsInBlock(BasicBlock &BB,
                                     SmallPtrSetImpl<Value *> &InsertedValues,
                                     Statistic &InstRemovedStat,
                                     Statistic &InstReplacedStat);

  /// Remove CFG edges from \p BB that the solver proved non-feasible.
  /// \param BB Block whose outgoing edges are pruned.
  /// \param DTU Dominator-tree updater for edge deletions and insertions.
  /// \param NewUnreachableBB Optional shared unreachable block for pruned defaults.
  /// \return True if any edge was removed or rewritten.
  LLVM_ABI bool removeNonFeasibleEdges(BasicBlock *BB, DomTreeUpdater &DTU,
                                       BasicBlock *&NewUnreachableBB) const;

  /// Infer range attributes from tracked return lattice values.
  LLVM_ABI void inferReturnAttributes() const;

  /// Infer argument attributes from tracked argument lattice values.
  LLVM_ABI void inferArgAttributes() const;

  /// Replace uses of \p V with a constant when the lattice value allows it.
  /// \param V Value to try to replace with a constant.
  /// \return True if \p V was replaced with a constant.
  LLVM_ABI bool tryToReplaceWithConstant(Value *V);

  /// Return true if \p LV is a constant or a single-element constant range.
  ///
  /// This should cover exactly the same cases as the old
  /// ValueLatticeElement::isConstant() and is intended to be used in the
  /// transition to ValueLatticeElement.
  /// \param LV Lattice value to test.
  /// \return True if \p LV is a constant or a single-element constant range.
  LLVM_ABI static bool isConstant(const ValueLatticeElement &LV);

  /// Return true if \p LV is overdefined or a multi-element constant range.
  ///
  /// This should cover exactly the same cases as the old
  /// ValueLatticeElement::isOverdefined() and is intended to be used in the
  /// transition to ValueLatticeElement.
  /// \param LV Lattice value to test.
  /// \return True if \p LV is overdefined or a multi-element constant range.
  LLVM_ABI static bool isOverdefined(const ValueLatticeElement &LV);

  /// Return true if \p LV is a constant that can be unconditionally propagated.
  ///
  /// A pointer constant with potentially different provenance may not be
  /// unconditionally propagated to all uses.
  /// \param LV Lattice value to test.
  /// \return True if \p LV is a replaceable constant.
  LLVM_ABI static bool isReplaceableConstant(const ValueLatticeElement &LV);
};
} // namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_SCCPSOLVER_H
