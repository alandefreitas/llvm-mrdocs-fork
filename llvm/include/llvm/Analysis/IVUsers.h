//===- llvm/Analysis/IVUsers.h - Induction Variable Users -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements bookkeeping for "interesting" users of expressions
// computed from induction variables.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_IVUSERS_H
#define LLVM_ANALYSIS_IVUSERS_H

#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/ScalarEvolutionNormalization.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/ValueHandle.h"

namespace llvm {

class AssumptionCache;
class DominatorTree;
class ScalarEvolution;
class SCEV;
class IVUsers;

/// Tracks one use of a strided induction variable.
///
/// The Expr member keeps track of the expression, User is the actual user
/// instruction of the operand, and 'OperandValToReplace' is the operand of
/// the User that is the use.
class LLVM_ABI IVStrideUse final : public CallbackVH,
                                   public ilist_node<IVStrideUse> {
  friend class IVUsers;
public:
  /// Construct an IVStrideUse owned by \p P for user \p U and operand \p O.
  /// @param P IVUsers analysis that owns this use.
  /// @param U User instruction of the IV operand.
  /// @param O Operand value in \p U that is the IV use.
  IVStrideUse(IVUsers *P, Instruction* U, Value *O)
    : CallbackVH(U), Parent(P), OperandValToReplace(O) {
  }

  /// getUser - Return the user instruction for this use.
  /// @return The user instruction for this use.
  Instruction *getUser() const {
    return cast<Instruction>(getValPtr());
  }

  /// setUser - Assign a new user instruction for this use.
  /// @param NewUser Instruction to record as the user.
  void setUser(Instruction *NewUser) {
    setValPtr(NewUser);
  }

  /// getOperandValToReplace - Return the Value of the operand in the user
  /// instruction that this IVStrideUse is representing.
  /// @return The operand Value in the user instruction that this use represents.
  Value *getOperandValToReplace() const {
    return OperandValToReplace;
  }

  /// setOperandValToReplace - Assign a new Value as the operand value
  /// to replace.
  /// @param Op Operand value this use should represent.
  void setOperandValToReplace(Value *Op) {
    OperandValToReplace = Op;
  }

  /// getPostIncLoops - Return the set of loops for which the expression has
  /// been adjusted to use post-inc mode.
  /// @return The set of loops for which the expression uses post-inc mode.
  const PostIncLoopSet &getPostIncLoops() const {
    return PostIncLoops;
  }

  /// transformToPostInc - Transform the expression to post-inc form for the
  /// given loop.
  /// @param L Loop for which the expression should use post-inc form.
  void transformToPostInc(const Loop *L);

private:
  /// Parent - a pointer to the IVUsers that owns this IVStrideUse.
  IVUsers *Parent;

  /// OperandValToReplace - The Value of the operand in the user instruction
  /// that this IVStrideUse is representing.
  WeakTrackingVH OperandValToReplace;

  /// PostIncLoops - The set of loops for which Expr has been adjusted to
  /// use post-inc mode. This corresponds with SCEVExpander's post-inc concept.
  PostIncLoopSet PostIncLoops;

  /// Deleted - Implementation of CallbackVH virtual function to
  /// receive notification when the User is deleted.
  void deleted() override;
};

/// Bookkeeping for interesting users of induction-variable expressions.
class IVUsers {
  friend class IVStrideUse;
  Loop *L;
  AssumptionCache *AC;
  LoopInfo *LI;
  DominatorTree *DT;
  ScalarEvolution *SE;
  SmallPtrSet<Instruction*, 16> Processed;

  /// IVUses - A list of all tracked IV uses of induction variable expressions
  /// we are interested in.
  ilist<IVStrideUse> IVUses;

  // Ephemeral values used by @llvm.assume in this function.
  SmallPtrSet<const Value *, 32> EphValues;

public:
  /// Construct IVUsers for loop \p L using the given analyses.
  /// @param L Loop whose IV users are collected.
  /// @param AC Assumption cache used to identify ephemeral values.
  /// @param LI Loop info for the function.
  /// @param DT Dominator tree used to choose pre-inc vs post-inc form.
  /// @param SE Scalar evolution used to classify IV expressions.
  LLVM_ABI IVUsers(Loop *L, AssumptionCache *AC, LoopInfo *LI,
                   DominatorTree *DT, ScalarEvolution *SE);

  /// Move-construct from \p X, reparenting tracked uses to this instance.
  /// @param X IVUsers to move from.
  IVUsers(IVUsers &&X)
      : L(std::move(X.L)), AC(std::move(X.AC)), DT(std::move(X.DT)),
        SE(std::move(X.SE)), Processed(std::move(X.Processed)),
        IVUses(std::move(X.IVUses)), EphValues(std::move(X.EphValues)) {
    for (IVStrideUse &U : IVUses)
      U.Parent = this;
  }
  /// Deleted copy constructor; IVUsers is not copyable.
  /// @param Other Unused source object; copying is not supported.
  IVUsers(const IVUsers &Other) = delete;
  /// Deleted move assignment; IVUsers cannot be move-assigned.
  /// @param RHS Unused right-hand side; move assignment is not supported.
  IVUsers &operator=(IVUsers &&RHS) = delete;
  /// Deleted copy assignment; IVUsers is not copyable.
  /// @param RHS Unused right-hand side; copying is not supported.
  IVUsers &operator=(const IVUsers &RHS) = delete;

  /// Return the loop whose induction-variable users are tracked.
  /// @return The loop whose induction-variable users are tracked.
  Loop *getLoop() const { return L; }

  /// Inspect \p I and add its users if it has a reducible SCEV.
  ///
  /// If it is a reducible SCEV, recursively add its users to the IVUsesByStride
  /// set and return true. Otherwise, return false.
  /// @param I Instruction to inspect.
  /// @return True if \p I has a reducible SCEV and its users were added.
  LLVM_ABI bool AddUsersIfInteresting(Instruction *I);

  /// Record that \p User uses IV operand \p Operand.
  /// @param User Instruction that uses the IV.
  /// @param Operand Operand of \p User that is the IV value.
  /// @return A reference to the newly recorded IVStrideUse.
  LLVM_ABI IVStrideUse &AddUser(Instruction *User, Value *Operand);

  /// getReplacementExpr - Return a SCEV expression which computes the
  /// value of the OperandValToReplace of the given IVStrideUse.
  /// @param IU IV use whose replacement expression is requested.
  /// @return A SCEV expression that computes the OperandValToReplace of \p IU.
  LLVM_ABI const SCEV *getReplacementExpr(const IVStrideUse &IU) const;

  /// getExpr - Return the expression for the use. Returns nullptr if the result
  /// is not invertible.
  /// @param IU IV use whose expression is requested.
  /// @return The SCEV expression for \p IU, or nullptr if not invertible.
  LLVM_ABI const SCEV *getExpr(const IVStrideUse &IU) const;

  /// Return the stride of \p IU in loop \p L, or nullptr if none.
  /// @param IU IV use whose stride is requested.
  /// @param L Loop whose add-recurrence step is extracted.
  /// @return The stride of \p IU in \p L, or nullptr if none.
  LLVM_ABI const SCEV *getStride(const IVStrideUse &IU, const Loop *L) const;

  /// Mutable iterator over the tracked IV uses.
  typedef ilist<IVStrideUse>::iterator iterator;
  /// Const iterator over the tracked IV uses.
  typedef ilist<IVStrideUse>::const_iterator const_iterator;
  /// Return an iterator to the first tracked IV use.
  /// @return An iterator to the first tracked IV use.
  iterator begin() { return IVUses.begin(); }
  /// Return an iterator past the last tracked IV use.
  /// @return An iterator past the last tracked IV use.
  iterator end()   { return IVUses.end(); }
  /// Return a const iterator to the first tracked IV use.
  /// @return A const iterator to the first tracked IV use.
  const_iterator begin() const { return IVUses.begin(); }
  /// Return a const iterator past the last tracked IV use.
  /// @return A const iterator past the last tracked IV use.
  const_iterator end() const   { return IVUses.end(); }
  /// Return true if there are no tracked IV uses.
  /// @return True if there are no tracked IV uses.
  bool empty() const { return IVUses.empty(); }

  /// Return true if \p Inst is a tracked IV user or operand of one.
  /// @param Inst Instruction to test.
  /// @return True if \p Inst is a tracked IV user or operand of one.
  bool isIVUserOrOperand(Instruction *Inst) const {
    return Processed.count(Inst);
  }

  /// Return true if \p V is an ephemeral value used only by llvm.assume.
  /// @param V Value to test.
  /// @return True if \p V is an ephemeral value used only by llvm.assume.
  bool isEphemeral(const Value *V) const { return EphValues.count(V); }

  /// Release the lists of processed instructions and tracked IV uses.
  LLVM_ABI void releaseMemory();

  /// Print the tracked IV users to \p OS.
  /// @param OS Output stream.
  /// @param M Optional module (unused).
  LLVM_ABI void print(raw_ostream &OS, const Module *M = nullptr) const;

  /// dump - This method is used for debugging.
  LLVM_ABI void dump() const;
};

/// Creates an instance of \c IVUsersWrapperPass.
/// @return A new IVUsersWrapperPass instance.
LLVM_ABI Pass *createIVUsersPass();

/// Legacy analysis pass which computes \c IVUsers for a loop.
class LLVM_ABI IVUsersWrapperPass : public LoopPass {
  std::unique_ptr<IVUsers> IU;

public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct the legacy IVUsers wrapper pass.
  IVUsersWrapperPass();

  /// Return the IVUsers computed by this pass.
  /// @return The IVUsers computed by this pass.
  IVUsers &getIU() { return *IU; }
  /// Return the IVUsers computed by this pass.
  /// @return The IVUsers computed by this pass.
  const IVUsers &getIU() const { return *IU; }

  /// Declare the analyses required and preserved by this pass.
  /// @param AU Analysis usage to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// Compute IVUsers for loop \p L.
  /// @param L Loop to analyze.
  /// @param LPM Loop pass manager (unused).
  /// @return False; this analysis does not modify the loop.
  bool runOnLoop(Loop *L, LPPassManager &LPM) override;

  /// Release the IVUsers computed by this pass.
  void releaseMemory() override;

  /// Print the IVUsers computed by this pass.
  /// @param OS Output stream.
  /// @param M Optional module forwarded to IVUsers::print.
  void print(raw_ostream &OS, const Module *M = nullptr) const override;
};

/// Analysis pass that exposes the \c IVUsers for a loop.
class IVUsersAnalysis : public AnalysisInfoMixin<IVUsersAnalysis> {
  friend AnalysisInfoMixin<IVUsersAnalysis>;
  static AnalysisKey Key;

public:
  /// Analysis result type produced by this pass.
  typedef IVUsers Result;

  /// Compute IVUsers for loop \p L.
  /// @param L Loop to analyze.
  /// @param AM Loop analysis manager (unused).
  /// @param AR Standard loop analyses providing AC, LI, DT, and SE.
  /// @return IVUsers for \p L built from the analyses in \p AR.
  LLVM_ABI IVUsers run(Loop &L, LoopAnalysisManager &AM,
                       LoopStandardAnalysisResults &AR);
};

}

#endif
