//==- ConstantHoisting.h - Prepare code for expensive constants --*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass identifies expensive constants to hoist and coalesces them to
// better prepare it for SelectionDAG-based code generation. This works around
// the limitations of the basic-block-at-a-time approach.
//
// First it scans all instructions for integer constants and calculates its
// cost. If the constant can be folded into the instruction (the cost is
// TCC_Free) or the cost is just a simple operation (TCC_BASIC), then we don't
// consider it expensive and leave it alone. This is the default behavior and
// the default implementation of getIntImmCostInst will always return TCC_Free.
//
// If the cost is more than TCC_BASIC, then the integer constant can't be folded
// into the instruction and it might be beneficial to hoist the constant.
// Similar constants are coalesced to reduce register pressure and
// materialization code.
//
// When a constant is hoisted, it is also hidden behind a bitcast to force it to
// be live-out of the basic block. Otherwise the constant would be just
// duplicated and each basic block would have its own copy in the SelectionDAG.
// The SelectionDAG recognizes such constants as opaque and doesn't perform
// certain transformations on them, which would create a new expensive constant.
//
// This optimization is only applied to integer constants in instructions and
// simple (this means not nested) constant cast expressions. For example:
// %0 = load i64* inttoptr (i64 big_constant to i64*)
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_CONSTANTHOISTING_H
#define LLVM_TRANSFORMS_SCALAR_CONSTANTHOISTING_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/PassManager.h"
#include <algorithm>
#include <vector>

namespace llvm {

class BasicBlock;
class BlockFrequencyInfo;
class Constant;
class ConstantInt;
class ConstantExpr;
class DominatorTree;
class Function;
class GlobalVariable;
class Instruction;
class ProfileSummaryInfo;
class TargetTransformInfo;
class TargetTransformInfo;

/// A private "module" namespace for types and utilities used by
/// ConstantHoisting. These are implementation details and should not be used by
/// clients.
namespace consthoist {

/// Keeps track of the user of a constant and the operand index where the
/// constant is used.
struct ConstantUser {
  /// Instruction that uses the constant.
  Instruction *Inst;
  /// Operand index of the constant use in the user instruction.
  unsigned OpndIdx;

  /// Create a constant user for instruction \p Inst at operand \p Idx.
  /// @param Inst Instruction that uses the constant.
  /// @param Idx Operand index of the constant use in \p Inst.
  ConstantUser(Instruction *Inst, unsigned Idx) : Inst(Inst), OpndIdx(Idx) {}
};

/// List of instructions that use a constant candidate.
using ConstantUseListType = SmallVector<ConstantUser, 8>;

/// Keeps track of a constant candidate and its uses.
struct ConstantCandidate {
  /// Uses of this constant candidate across the function.
  ConstantUseListType Uses;
  /// Integer constant, or offset from a base GV for a constant GEP candidate.
  ///
  /// For ConstantExpr candidates (currently only constant GEP expressions
  /// whose base pointers are GlobalVariables), this records the offset from
  /// the base GV.
  ConstantInt *ConstInt;
  /// Optional constant GEP expression for this candidate, or nullptr.
  ///
  /// When non-null, tracks the candidate GEP whose base is a GlobalVariable;
  /// ConstInt then holds the offset from that base.
  ConstantExpr *ConstExpr;
  /// Sum of materialization costs across all recorded uses.
  unsigned CumulativeCost = 0;

  /// Create a candidate for \p ConstInt, optionally backed by \p ConstExpr.
  /// @param ConstInt Integer constant or GEP offset for this candidate.
  /// @param ConstExpr Optional constant GEP expression, or nullptr.
  ConstantCandidate(ConstantInt *ConstInt, ConstantExpr *ConstExpr=nullptr) :
      ConstInt(ConstInt), ConstExpr(ConstExpr) {}

  /// Add the user to the use list and update the cost.
  /// @param Inst Instruction that uses the constant.
  /// @param Idx Operand index of the constant use in \p Inst.
  /// @param Cost Materialization cost contributed by this use.
  void addUser(Instruction *Inst, unsigned Idx, unsigned Cost) {
    CumulativeCost += Cost;
    Uses.push_back(ConstantUser(Inst, Idx));
  }
};

/// This represents a constant that has been rebased with respect to a
/// base constant. The difference to the base constant is recorded in Offset.
struct RebasedConstantInfo {
  /// Uses of the original constant that will be rewritten relative to the base.
  ConstantUseListType Uses;
  /// Difference from the shared base constant used to re-materialize this one.
  Constant *Offset;
  /// Optional type used when adjusting users of this rebased constant.
  Type *Ty;

  /// Create rebased-constant info for \p Uses with difference \p Offset.
  /// @param Uses Uses of the original constant being rebased.
  /// @param Offset Difference from the shared base constant.
  /// @param Ty Optional type used when adjusting users, or nullptr.
  RebasedConstantInfo(ConstantUseListType &&Uses, Constant *Offset,
      Type *Ty=nullptr) : Uses(std::move(Uses)), Offset(Offset), Ty(Ty) {}
};

/// List of constants rebased relative to a shared base constant.
using RebasedConstantListType = SmallVector<RebasedConstantInfo, 4>;

/// A base constant and all its rebased constants.
struct ConstantInfo {
  /// Base integer constant, or offset from a base GV for a constant GEP.
  ///
  /// For ConstantExpr bases (currently only constant GEP expressions whose
  /// base pointers are GlobalVariables), this records the offset from the
  /// base GV.
  ConstantInt *BaseInt;
  /// Optional constant GEP expression used as the base, or nullptr.
  ///
  /// When non-null, tracks the base GEP whose base is a GlobalVariable;
  /// BaseInt then holds the offset from that base.
  ConstantExpr *BaseExpr;
  /// Constants rebased relative to this base, including their uses and offsets.
  RebasedConstantListType RebasedConstants;
};

} // end namespace consthoist

/// Pass that hoists and coalesces expensive constants for codegen.
///
/// Identifies expensive integer constants and simple constant cast
/// expressions, coalesces similar ones to reduce register pressure, and
/// hides hoisted values behind bitcasts so SelectionDAG keeps a single
/// live-out copy instead of duplicating materialization per basic block.
class ConstantHoistingPass
    : public OptionalPassInfoMixin<ConstantHoistingPass> {
public:
  /// Run constant hoisting over the function.
  /// @param F Function whose expensive constants may be hoisted.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  /// Run constant hoisting using already-fetched analyses (legacy PM glue).
  /// @param F Function whose expensive constants may be hoisted.
  /// @param TTI Target transform info used to cost immediate materialization.
  /// @param DT Dominator tree used to place hoisted constants.
  /// @param BFI Optional block frequency info for cost heuristics.
  /// @param Entry Function entry block used as a fallback insertion point.
  /// @param PSI Profile summary info used with BFI for size/opt decisions.
  /// @return True if the function was modified.
  LLVM_ABI bool runImpl(Function &F, TargetTransformInfo &TTI,
                        DominatorTree &DT, BlockFrequencyInfo *BFI,
                        BasicBlock &Entry, ProfileSummaryInfo *PSI);

  /// Clear pass-local candidate and cast-clone maps after a run.
  void cleanup() {
    ClonedCastMap.clear();
    ConstIntCandVec.clear();
    ConstGEPCandMap.clear();
    ConstIntInfoVec.clear();
    ConstGEPInfoMap.clear();
  }

private:
  using ConstPtrUnionType = PointerUnion<ConstantInt *, ConstantExpr *>;
  using ConstCandMapType = DenseMap<ConstPtrUnionType, unsigned>;

  const TargetTransformInfo *TTI;
  DominatorTree *DT;
  BlockFrequencyInfo *BFI;
  LLVMContext *Ctx;
  const DataLayout *DL;
  BasicBlock *Entry;
  ProfileSummaryInfo *PSI;
  bool OptForSize;

  /// Keeps track of constant candidates found in the function.
  using ConstCandVecType = std::vector<consthoist::ConstantCandidate>;
  using GVCandVecMapType = MapVector<GlobalVariable *, ConstCandVecType>;
  ConstCandVecType ConstIntCandVec;
  GVCandVecMapType ConstGEPCandMap;

  /// These are the final constants we decided to hoist.
  using ConstInfoVecType = SmallVector<consthoist::ConstantInfo, 8>;
  using GVInfoVecMapType = MapVector<GlobalVariable *, ConstInfoVecType>;
  ConstInfoVecType ConstIntInfoVec;
  GVInfoVecMapType ConstGEPInfoMap;

  /// Keep track of cast instructions we already cloned.
  MapVector<Instruction *, Instruction *> ClonedCastMap;

  void collectMatInsertPts(
      const consthoist::RebasedConstantListType &RebasedConstants,
      SmallVectorImpl<BasicBlock::iterator> &MatInsertPts) const;
  BasicBlock::iterator findMatInsertPt(Instruction *Inst,
                                       unsigned Idx = ~0U) const;
  SetVector<BasicBlock::iterator> findConstantInsertionPoint(
      const consthoist::ConstantInfo &ConstInfo,
      const ArrayRef<BasicBlock::iterator> MatInsertPts) const;
  void collectConstantCandidates(ConstCandMapType &ConstCandMap,
                                 Instruction *Inst, unsigned Idx,
                                 ConstantInt *ConstInt);
  void collectConstantCandidates(ConstCandMapType &ConstCandMap,
                                 Instruction *Inst, unsigned Idx,
                                 ConstantExpr *ConstExpr);
  void collectConstantCandidates(ConstCandMapType &ConstCandMap,
                                 Instruction *Inst, unsigned Idx);
  void collectConstantCandidates(ConstCandMapType &ConstCandMap,
                                 Instruction *Inst);
  void collectConstantCandidates(Function &Fn);
  void findAndMakeBaseConstant(ConstCandVecType::iterator S,
                               ConstCandVecType::iterator E,
      SmallVectorImpl<consthoist::ConstantInfo> &ConstInfoVec);
  unsigned maximizeConstantsInRange(ConstCandVecType::iterator S,
                                    ConstCandVecType::iterator E,
                                    ConstCandVecType::iterator &MaxCostItr);
  // If BaseGV is nullptr, find base among Constant Integer candidates;
  // otherwise find base among constant GEPs sharing BaseGV as base pointer.
  void findBaseConstants(GlobalVariable *BaseGV);

  /// A ConstantUser grouped with the Type and Constant adjustment. The user
  /// will be adjusted by Offset.
  struct UserAdjustment {
    Constant *Offset;
    Type *Ty;
    BasicBlock::iterator MatInsertPt;
    const consthoist::ConstantUser User;
    UserAdjustment(Constant *O, Type *T, BasicBlock::iterator I,
                   consthoist::ConstantUser U)
        : Offset(O), Ty(T), MatInsertPt(I), User(U) {}
  };
  void emitBaseConstants(Instruction *Base, UserAdjustment *Adj);
  // If BaseGV is nullptr, emit Constant Integer base; otherwise emit
  // constant GEP base.
  bool emitBaseConstants(GlobalVariable *BaseGV);
  void deleteDeadCastInst() const;
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_CONSTANTHOISTING_H
