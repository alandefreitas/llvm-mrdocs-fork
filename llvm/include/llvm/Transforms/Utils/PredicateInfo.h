//===- PredicateInfo.h - Build PredicateInfo ----------------------*-C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
///  This file implements the PredicateInfo analysis, which creates an Extended
/// SSA form for operations used in branch comparisons and llvm.assume
/// comparisons.
///
/// Copies of these operations are inserted into the true/false edge (and after
/// assumes), and information attached to the copies.  All uses of the original
/// operation in blocks dominated by the true/false edge (and assume), are
/// replaced with uses of the copies.  This enables passes to easily and sparsely
/// propagate condition based info into the operations that may be affected.
///
/// Example:
/// %cmp = icmp eq i32 %x, 50
/// br i1 %cmp, label %true, label %false
/// true:
/// ret i32 %x
/// false:
/// ret i32 1
///
/// will become
///
/// %cmp = icmp eq i32, %x, 50
/// br i1 %cmp, label %true, label %false
/// true:
/// %x.0 = bitcast i32 %x to %x
/// ret i32 %x.0
/// false:
/// ret i32 1
///
/// Using getPredicateInfoFor on x.0 will give you the comparison it is
/// dominated by (the icmp), and that you are located in the true edge of that
/// comparison, which tells you x.0 is 50.
///
/// In order to reduce the number of copies inserted, predicateinfo is only
/// inserted where it would actually be live.  This means if there are no uses of
/// an operation dominated by the branch edges, or by an assume, the associated
/// predicate info is never inserted.
///
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_PREDICATEINFO_H
#define LLVM_TRANSFORMS_UTILS_PREDICATEINFO_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/IR/BundleAttributes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class AssumptionCache;
class DominatorTree;
class Function;
class Value;
class IntrinsicInst;
class raw_ostream;

/// Kind of predicate information attached to a renamed operand copy.
enum PredicateType {
  /// Predicate established by a conditional branch edge.
  PT_Branch,
  /// Predicate established by the condition of an llvm.assume.
  PT_ConditionAssume,
  /// Predicate established by an attribute on an llvm.assume operand bundle.
  PT_BundleAssume,
  /// Predicate established by a switch case edge.
  PT_Switch
};

/// Constraint for a predicate of the form "cmp Pred Op, OtherOp", where Op
/// is the value the constraint applies to (the bitcast result).
struct PredicateConstraint {
  /// Comparison predicate relating the constrained operand to OtherOp.
  CmpInst::Predicate Predicate;
  /// Other operand of the comparison constraint.
  Value *OtherOp;
};

/// Base class for all predicate information we provide.
///
/// All of our predicate information has at least a comparison.
class PredicateBase {
public:
  /// Discriminator describing which derived predicate this is.
  PredicateType Type;
  /// The original operand before we renamed it.
  ///
  /// This can be used by passes, when destroying predicateinfo, to know
  /// whether they can just drop the intrinsic, or have to merge metadata.
  Value *OriginalOp;
  /// The renamed operand in the condition used for this predicate.
  ///
  /// For nested predicates, this is different from OriginalOp which refers
  /// to the initial operand.
  Value *RenamedOp;
  /// The condition associated with this predicate.
  Value *Condition;

  /// Deleted copy constructor; PredicateBase is not copyable.
  ///
  /// \param Other Unused; copy construction is not allowed.
  PredicateBase(const PredicateBase &Other) = delete;
  /// Deleted copy assignment; PredicateBase cannot be copy-assigned.
  ///
  /// \param Other Unused; copy assignment is not allowed.
  PredicateBase &operator=(const PredicateBase &Other) = delete;
  /// Deleted default constructor; subclasses must supply operands.
  PredicateBase() = delete;
  /// Return true if \p PB is any concrete PredicateInfo predicate.
  ///
  /// \param PB Predicate to test.
  /// \return True if \p PB is a concrete PredicateInfo predicate.
  static bool classof(const PredicateBase *PB) {
    return PB->Type == PT_BundleAssume || PB->Type == PT_ConditionAssume ||
           PB->Type == PT_Branch || PB->Type == PT_Switch;
  }

  /// Fetch condition in the form of PredicateConstraint, if possible.
  ///
  /// \return The comparison constraint when Condition is a CmpInst; otherwise
  /// std::nullopt.
  LLVM_ABI std::optional<PredicateConstraint> getConstraint() const;

protected:
  /// Construct a predicate of kind \p PT for operand \p Op under \p Condition.
  ///
  /// \param PT Discriminator for the concrete derived predicate type.
  /// \param Op Original operand being renamed for this predicate.
  /// \param Condition Condition value associated with this predicate.
  PredicateBase(PredicateType PT, Value *Op, Value *Condition)
      : Type(PT), OriginalOp(Op), Condition(Condition) {}
};

/// Predicate information for llvm.assume intrinsics.
///
/// Since assumes are always true, we simply provide the assume instruction, so
/// you can tell your relative position to it.
class PredicateAssume : public PredicateBase {
public:
  /// The llvm.assume intrinsic that establishes this predicate.
  IntrinsicInst *AssumeInst;

  /// Return true if \p PB is an assume-based predicate.
  ///
  /// \param PB Predicate to test.
  /// \return True if \p PB is a PredicateAssume (condition or bundle).
  static bool classof(const PredicateBase *PB) {
    return PB->Type == PT_ConditionAssume || PB->Type == PT_BundleAssume;
  }

protected:
  /// Construct an assume predicate of kind \p PT.
  ///
  /// \param PT Discriminator for the concrete assume subtype.
  /// \param Op Original operand being renamed for this predicate.
  /// \param AssumeInst llvm.assume that establishes the predicate.
  /// \param Condition Condition associated with this predicate, or nullptr.
  PredicateAssume(PredicateType PT, Value *Op, IntrinsicInst *AssumeInst,
                  Value *Condition)
      : PredicateBase(PT, Op, Condition), AssumeInst(AssumeInst) {}
};

/// Predicate information from an llvm.assume operand-bundle attribute.
class PredicateBundleAssume : public PredicateAssume {
public:
  /// Operand-bundle attribute kind that establishes this predicate.
  BundleAttr AttrKind;
  /// Construct a bundle-assume predicate for \p Op.
  ///
  /// \param Op Original operand being renamed for this predicate.
  /// \param AssumeInst llvm.assume carrying the operand bundle.
  /// \param AttrKind Bundle attribute kind that establishes the predicate.
  PredicateBundleAssume(Value *Op, IntrinsicInst *AssumeInst,
                        BundleAttr AttrKind)
      : PredicateAssume(PT_BundleAssume, Op, AssumeInst, nullptr),
        AttrKind(AttrKind) {}

  /// Return true if \p PB is a PredicateBundleAssume.
  ///
  /// \param PB Predicate to test.
  /// \return True if \p PB is a PredicateBundleAssume.
  static bool classof(const PredicateBase *PB) {
    return PB->Type == PT_BundleAssume;
  }
};

/// Predicate information from the boolean condition of an llvm.assume.
class PredicateConditionAssume : public PredicateAssume {
public:
  /// Construct a condition-assume predicate for \p Op.
  ///
  /// \param Op Original operand being renamed for this predicate.
  /// \param AssumeInst llvm.assume whose condition establishes the predicate.
  /// \param Condition Condition value associated with this predicate.
  PredicateConditionAssume(Value *Op, IntrinsicInst *AssumeInst,
                           Value *Condition)
      : PredicateAssume(PT_ConditionAssume, Op, AssumeInst, Condition) {}

  /// Return true if \p PB is a PredicateConditionAssume.
  ///
  /// \param PB Predicate to test.
  /// \return True if \p PB is a PredicateConditionAssume.
  static bool classof(const PredicateBase *PB) {
    return PB->Type == PT_ConditionAssume;
  }
};

/// Mixin for predicates that hold along a CFG edge.
///
/// The FROM block is the block where the predicate originates, and the TO
/// block is the block where the predicate is valid.
class PredicateWithEdge : public PredicateBase {
public:
  /// Basic block where the branch or switch that produces the predicate lives.
  BasicBlock *From;
  /// Successor block in which the renamed operand is known to satisfy the
  /// predicate.
  BasicBlock *To;
  /// Deleted default constructor; edge endpoints must be supplied.
  PredicateWithEdge() = delete;
  /// Return true if \p PB is a branch or switch edge predicate.
  ///
  /// \param PB Predicate to test.
  /// \return True if \p PB is a PredicateWithEdge (branch or switch).
  static bool classof(const PredicateBase *PB) {
    return PB->Type == PT_Branch || PB->Type == PT_Switch;
  }

protected:
  /// Construct an edge predicate of kind \p PType along \p From -> \p To.
  ///
  /// \param PType Discriminator for the concrete edge predicate type.
  /// \param Op Original operand being renamed for this predicate.
  /// \param From Block containing the branch or switch.
  /// \param To Successor block where the predicate holds.
  /// \param Cond Condition associated with this predicate.
  PredicateWithEdge(PredicateType PType, Value *Op, BasicBlock *From,
                    BasicBlock *To, Value *Cond)
      : PredicateBase(PType, Op, Cond), From(From), To(To) {}
};

/// Predicate information for conditional branches.
class PredicateBranch : public PredicateWithEdge {
public:
  /// True when \c To is the true successor; false for the false successor.
  bool TrueEdge;
  /// Construct a branch predicate for \p Op along the taken edge.
  ///
  /// \param Op Original operand being renamed for this predicate.
  /// \param BranchBB Block containing the conditional branch.
  /// \param SplitBB Successor block where the predicate holds.
  /// \param Condition Condition of the branch.
  /// \param TakenEdge True if \p SplitBB is the true successor.
  PredicateBranch(Value *Op, BasicBlock *BranchBB, BasicBlock *SplitBB,
                  Value *Condition, bool TakenEdge)
      : PredicateWithEdge(PT_Branch, Op, BranchBB, SplitBB, Condition),
        TrueEdge(TakenEdge) {}
  /// Deleted default constructor; branch endpoints must be supplied.
  PredicateBranch() = delete;
  /// Return true if \p PB is a PredicateBranch.
  ///
  /// \param PB Predicate to test.
  /// \return True if \p PB is a PredicateBranch.
  static bool classof(const PredicateBase *PB) {
    return PB->Type == PT_Branch;
  }
};

/// Predicate information for a switch case edge.
class PredicateSwitch : public PredicateWithEdge {
public:
  /// Constant case value that selects the \c To successor.
  Value *CaseValue;
  /// Switch instruction that produces this predicate.
  SwitchInst *Switch;
  /// Construct a switch-case predicate for \p Op.
  ///
  /// \param Op Original operand being renamed for this predicate.
  /// \param SwitchBB Block containing the switch.
  /// \param TargetBB Case successor where the predicate holds.
  /// \param CaseValue Constant value of the taken case.
  /// \param SI Switch instruction that produces this predicate.
  PredicateSwitch(Value *Op, BasicBlock *SwitchBB, BasicBlock *TargetBB,
                  Value *CaseValue, SwitchInst *SI)
      : PredicateWithEdge(PT_Switch, Op, SwitchBB, TargetBB,
                          SI->getCondition()),
        CaseValue(CaseValue), Switch(SI) {}
  /// Deleted default constructor; switch endpoints must be supplied.
  PredicateSwitch() = delete;
  /// Return true if \p PB is a PredicateSwitch.
  ///
  /// \param PB Predicate to test.
  /// \return True if \p PB is a PredicateSwitch.
  static bool classof(const PredicateBase *PB) {
    return PB->Type == PT_Switch;
  }
};

/// Encapsulates PredicateInfo, including all data associated with memory
/// accesses.
class PredicateInfo {
public:
  /// Build PredicateInfo for \p F using \p DT and \p AC.
  ///
  /// \param F Function to analyze.
  /// \param DT Dominator tree for \p F.
  /// \param AC Assumption cache for \p F.
  /// \param Allocator Allocator used for predicate objects.
  LLVM_ABI PredicateInfo(Function &F, DominatorTree &DT, AssumptionCache &AC,
                         BumpPtrAllocator &Allocator);

  /// Verify internal PredicateInfo invariants.
  LLVM_ABI void verifyPredicateInfo() const;

  /// Dump PredicateInfo to stderr for debugging.
  LLVM_ABI void dump() const;
  /// Print PredicateInfo to \p OS.
  ///
  /// \param OS Output stream.
  LLVM_ABI void print(raw_ostream &OS) const;

  /// Return the predicate info attached to renamed value \p V, or nullptr.
  ///
  /// \param V Renamed copy operand to look up.
  /// \return Predicate info for \p V, or nullptr if none is attached.
  const PredicateBase *getPredicateInfoFor(const Value *V) const {
    return PredicateMap.lookup(V);
  }

protected:
  // Used by PredicateInfo annotater, dumpers, and wrapper pass.
  /// Annotated writer that attaches PredicateInfo to IR assembly dumps.
  friend class PredicateInfoAnnotatedWriter;
  /// Builds PredicateInfo by analyzing branches, switches, and assumes.
  friend class PredicateInfoBuilder;

private:
  Function &F;

  // This maps from copy operands to Predicate Info. Note that it does not own
  // the Predicate Info, they belong to the ValueInfo structs in the ValueInfos
  // vector.
  DenseMap<const Value *, const PredicateBase *> PredicateMap;
};

/// Printer pass for \c PredicateInfo.
class PredicateInfoPrinterPass
    : public RequiredPassInfoMixin<PredicateInfoPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer pass that writes to \p OS.
  ///
  /// \param OS Output stream for the printed PredicateInfo.
  explicit PredicateInfoPrinterPass(raw_ostream &OS) : OS(OS) {}
  /// Run the PredicateInfo printer over \p F.
  ///
  /// \param F Function to analyze and print.
  /// \param AM Function analysis manager providing analyses for the pass.
  /// \return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Verifier pass for \c PredicateInfo.
struct PredicateInfoVerifierPass
    : RequiredPassInfoMixin<PredicateInfoVerifierPass> {
  /// Run the PredicateInfo verifier over \p F.
  ///
  /// \param F Function whose PredicateInfo should be verified.
  /// \param AM Function analysis manager providing analyses for the pass.
  /// \return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_PREDICATEINFO_H
