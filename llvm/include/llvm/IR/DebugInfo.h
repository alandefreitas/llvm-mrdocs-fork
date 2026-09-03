//===- DebugInfo.h - Debug Information Helpers ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a bunch of datatypes that are useful for creating and
// walking debug info in LLVM IR form. They essentially provide wrappers around
// the information in the global variables that's needed when constructing the
// DWARF information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_DEBUGINFO_H
#define LLVM_IR_DEBUGINFO_H

#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/TinyPtrVector.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"
#include <optional>

namespace llvm {

class DbgDeclareInst;
class DbgValueInst;
class DbgVariableIntrinsic;
class DbgVariableRecord;
class Instruction;
class Module;

/// Finds dbg.declare records declaring local variables as living in the
/// memory that 'V' points to.
/// \param V Value whose address is searched for as a declare location.
/// \return dbg.declare records that declare local variables living in \p V.
LLVM_ABI TinyPtrVector<DbgVariableRecord *> findDVRDeclares(Value *V);
/// As above, for DVRValues.
/// \param V Value whose dbg.value-style records are collected.
/// \return dbg.value-style records associated with \p V.
LLVM_ABI TinyPtrVector<DbgVariableRecord *> findDVRValues(Value *V);
/// As above, for DVRDeclareValues.
/// \param V Value whose dbg.declare_value-style records are collected.
/// \return dbg.declare_value-style records associated with \p V.
LLVM_ABI TinyPtrVector<DbgVariableRecord *> findDVRDeclareValues(Value *V);

/// Finds the debug info records describing a value.
/// \param V Value whose debug uses are collected.
/// \param DbgVariableRecords Output vector that receives matching records.
LLVM_ABI void
findDbgUsers(Value *V,
             SmallVectorImpl<DbgVariableRecord *> &DbgVariableRecords);
/// Finds the dbg.values describing a value.
/// \param V Value whose dbg.value records are collected.
/// \param DbgVariableRecords Output vector that receives matching records.
LLVM_ABI void
findDbgValues(Value *V,
              SmallVectorImpl<DbgVariableRecord *> &DbgVariableRecords);

/// Find subprogram that is enclosing this scope.
/// \param Scope Debug scope metadata to walk upward from.
/// \return Enclosing DISubprogram for \p Scope, or nullptr if none.
LLVM_ABI DISubprogram *getDISubprogram(const MDNode *Scope);

/// Produce a DebugLoc to use for each dbg.declare that is promoted to a
/// dbg.value.
/// \param DVR dbg.declare-style record being promoted.
/// \return DebugLoc suitable for the promoted dbg.value.
LLVM_ABI DebugLoc getDebugValueLoc(DbgVariableRecord *DVR);

/// Strip debug info in the module if it exists.
///
/// To do this, we remove all calls to the debugger intrinsics and any named
/// metadata for debugging. We also remove debug locations for instructions.
/// \param M Module whose debug info is stripped.
/// \return True if the module was modified.
LLVM_ABI bool StripDebugInfo(Module &M);
/// Strip debug info from a single function.
/// \param F Function whose debug info is stripped.
/// \return True if \p F was modified.
LLVM_ABI bool stripDebugInfo(Function &F);

/// Downgrade the debug info in a module to contain only line table information.
///
/// In order to convert debug info to what -gline-tables-only would have
/// created, this does the following:
///   1) Delete all debug intrinsics.
///   2) Delete all non-CU named metadata debug info nodes.
///   3) Create new DebugLocs for each instruction.
///   4) Create a new CU debug info, and similarly for every metadata node
///      that's reachable from the CU debug info.
///   All debug type metadata nodes are unreachable and garbage collected.
/// \param M Module whose debug info is downgraded.
/// \return True if the module was modified.
LLVM_ABI bool stripNonLineTableDebugInfo(Module &M);

/// Update debug locations in any MD_loop metadata attached to \p I.
///
/// \p Updater is applied to Metadata operand in the MD_loop metadata: the
/// returned value is included in the updated loop metadata node if it is
/// non-null.
/// \param I Instruction whose MD_loop metadata is updated, if present.
/// \param Updater Callback applied to each Metadata operand in MD_loop.
LLVM_ABI void
updateLoopMetadataDebugLocations(Instruction &I,
                                 function_ref<Metadata *(Metadata *)> Updater);

/// Return Debug Info Metadata Version by checking module flags.
/// \param M Module whose debug-info version flag is read.
/// \return Debug info metadata version from the module flags.
LLVM_ABI unsigned getDebugMetadataVersionFromModule(const Module &M);

/// Utility to find all debug info in a module.
///
/// DebugInfoFinder tries to list all debug info MDNodes used in a module. To
/// list debug info MDNodes used by an instruction, DebugInfoFinder uses
/// processDeclare, processValue and processLocation to handle DbgDeclareInst,
/// DbgValueInst and DbgLoc attached to instructions. processModule will go
/// through all DICompileUnits in llvm.dbg.cu and list debug info MDNodes
/// used by the CUs.
class DebugInfoFinder {
public:
  /// Process entire module and collect debug info anchors.
  /// \param M Module to scan for debug info.
  LLVM_ABI void processModule(const Module &M);
  /// Process a single instruction and collect debug info anchors.
  /// \param M Module that owns \p I.
  /// \param I Instruction whose attached debug info is collected.
  LLVM_ABI void processInstruction(const Module &M, const Instruction &I);

  /// Process a DILocalVariable.
  /// \param DVI Local variable metadata to collect.
  LLVM_ABI void processVariable(const DILocalVariable *DVI);
  /// Process debug info location.
  /// \param M Module that owns \p Loc.
  /// \param Loc Debug location metadata to collect.
  LLVM_ABI void processLocation(const Module &M, const DILocation *Loc);
  /// Process a DbgRecord.
  /// \param M Module that owns \p DR.
  /// \param DR Debug record whose metadata is collected.
  LLVM_ABI void processDbgRecord(const Module &M, const DbgRecord &DR);

  /// Process subprogram.
  /// \param SP Subprogram metadata to collect.
  LLVM_ABI void processSubprogram(DISubprogram *SP);

  /// Clear all lists.
  LLVM_ABI void reset();

private:
  void processCompileUnit(DICompileUnit *CU);
  void processGlobalVariableExpression(DIGlobalVariableExpression *GVE);
  void processScope(DIScope *Scope);
  void processType(DIType *DT);
  void processVariable(DIVariable *DV);
  void processImportedEntity(const DIImportedEntity *Import);
  void processMacroNode(DIMacroNode *Macro, DIMacroFile *CurrentMacroFile);
  bool addCompileUnit(DICompileUnit *CU);
  bool addGlobalVariable(DIGlobalVariableExpression *DIG);
  bool addScope(DIScope *Scope);
  bool addSubprogram(DISubprogram *SP);
  bool addType(DIType *DT);
  bool addMacro(DIMacro *Macro, DIMacroFile *MacroFile);

public:
  /// Pair of a macro and the macro file that contains it.
  using DIMacroEntry = std::pair<DIMacro *, DIMacroFile *>;
  /// Iterator over collected compile units.
  using compile_unit_iterator =
      SmallVectorImpl<DICompileUnit *>::const_iterator;
  /// Iterator over collected subprograms.
  using subprogram_iterator = SmallVectorImpl<DISubprogram *>::const_iterator;
  /// Iterator over collected global variable expressions.
  using global_variable_expression_iterator =
      SmallVectorImpl<DIGlobalVariableExpression *>::const_iterator;
  /// Iterator over collected types.
  using type_iterator = SmallVectorImpl<DIType *>::const_iterator;
  /// Iterator over collected scopes.
  using scope_iterator = SmallVectorImpl<DIScope *>::const_iterator;
  /// Iterator over collected macros.
  using macro_iterator = SmallVectorImpl<DIMacroEntry>::const_iterator;

  /// Return the range of collected compile units.
  /// \return Range of collected compile units.
  iterator_range<compile_unit_iterator> compile_units() const { return CUs; }

  /// Return the range of collected subprograms.
  /// \return Range of collected subprograms.
  iterator_range<subprogram_iterator> subprograms() const { return SPs; }

  /// Return the range of collected global variable expressions.
  /// \return Range of collected global variable expressions.
  iterator_range<global_variable_expression_iterator> global_variables() const {
    return GVs;
  }

  /// Return the range of collected types.
  /// \return Range of collected types.
  iterator_range<type_iterator> types() const { return TYs; }

  /// Return the range of collected scopes.
  /// \return Range of collected scopes.
  iterator_range<scope_iterator> scopes() const { return Scopes; }

  /// Return the range of collected macros.
  /// \return Range of collected macros.
  iterator_range<macro_iterator> macros() const { return Macros; }

  /// Return the number of collected compile units.
  /// \return Number of collected compile units.
  unsigned compile_unit_count() const { return CUs.size(); }
  /// Return the number of collected global variable expressions.
  /// \return Number of collected global variable expressions.
  unsigned global_variable_count() const { return GVs.size(); }
  /// Return the number of collected subprograms.
  /// \return Number of collected subprograms.
  unsigned subprogram_count() const { return SPs.size(); }
  /// Return the number of collected types.
  /// \return Number of collected types.
  unsigned type_count() const { return TYs.size(); }
  /// Return the number of collected scopes.
  /// \return Number of collected scopes.
  unsigned scope_count() const { return Scopes.size(); }
  /// Return the number of collected macros.
  /// \return Number of collected macros.
  unsigned macro_count() const { return Macros.size(); }

private:
  SmallVector<DICompileUnit *, 8> CUs;
  SmallVector<DISubprogram *, 8> SPs;
  SmallVector<DIGlobalVariableExpression *, 8> GVs;
  SmallVector<DIType *, 8> TYs;
  SmallVector<DIScope *, 8> Scopes;
  SmallVector<DIMacroEntry, 8> Macros;
  SmallPtrSet<const MDNode *, 32> NodesSeen;
};

/// Assignment Tracking (at).
namespace at {
//
// Utilities for enumerating storing instructions from an assignment ID.
//
/// A range of instructions.
using AssignmentInstRange =
    iterator_range<SmallVectorImpl<Instruction *>::iterator>;
/// Return instructions that have \p ID as a DIAssignID attachment.
///
/// Typically the range contains just one instruction. Iterators are
/// invalidated by adding or removing DIAssignID metadata to/from any
/// instruction (including by deleting or cloning instructions).
/// \param ID Assignment ID whose attached instructions are returned.
/// \return Range of instructions attached to \p ID.
LLVM_ABI AssignmentInstRange getAssignmentInsts(DIAssignID *ID);

/// Return instructions that perform the assignment encoded by \p DVR.
/// \param DVR dbg.assign record whose assign ID is looked up.
/// \return Range of instructions that perform the assignment encoded by \p DVR.
inline AssignmentInstRange getAssignmentInsts(const DbgVariableRecord *DVR) {
  assert(DVR->isDbgAssign() &&
         "Can't get assignment instructions for non-assign DVR!");
  return getAssignmentInsts(DVR->getAssignID());
}

/// Return a range of dbg_assign records for which \p Inst performs the
/// assignment they encode.
/// \param Inst Instruction whose DIAssignID users are returned.
/// \return dbg_assign records for which \p Inst performs the encoded assignment.
inline SmallVector<DbgVariableRecord *>
getDVRAssignmentMarkers(const Instruction *Inst) {
  if (auto *ID = Inst->getMetadata(LLVMContext::MD_DIAssignID))
    return cast<DIAssignID>(ID)->getAllDbgVariableRecordUsers();
  return {};
}

/// Delete the llvm.dbg.assign intrinsics linked to \p Inst.
/// \param Inst Instruction whose linked assignment markers are deleted.
LLVM_ABI void deleteAssignmentMarkers(const Instruction *Inst);

/// Replace all uses (and attachments) of \p Old with \p New.
/// \param Old Assignment ID being replaced.
/// \param New Assignment ID that replaces \p Old.
LLVM_ABI void RAUW(DIAssignID *Old, DIAssignID *New);

/// Remove all Assignment Tracking related intrinsics and metadata from \p F.
/// \param F Function from which assignment-tracking metadata is removed.
LLVM_ABI void deleteAll(Function *F);

/// Calculate the fragment of the variable in \p DVRAssign covered
/// from (Dest + SliceOffsetInBits) to
///   to (Dest + SliceOffsetInBits + SliceSizeInBits)
///
/// Result is set to nullopt if the intersect equals the variable fragment (or
/// variable size) in DVRAssign.
///
/// Result contains a zero-sized fragment if there's no intersect.
/// \param DL Data layout used to compute sizes and offsets.
/// \param Dest Destination address of the store/memcpy slice.
/// \param SliceOffsetInBits Bit offset of the slice into \p Dest.
/// \param SliceSizeInBits Size in bits of the slice.
/// \param DVRAssign dbg.assign whose variable fragment is intersected.
/// \param Result Set to the intersecting fragment, or nullopt if it matches
///        the whole variable fragment.
/// \return False if the intersect cannot be calculated for any reason.
LLVM_ABI bool
calculateFragmentIntersect(const DataLayout &DL, const Value *Dest,
                           uint64_t SliceOffsetInBits, uint64_t SliceSizeInBits,
                           const DbgVariableRecord *DVRAssign,
                           std::optional<DIExpression::FragmentInfo> &Result);

/// Replace DIAssignID uses and attachments with IDs from \p Map.
/// If an ID is unmapped a new ID is generated and added to \p Map.
/// \param Map Mapping from old assignment IDs to replacement IDs.
/// \param I Instruction whose DIAssignID uses and attachments are remapped.
LLVM_ABI void remapAssignID(DenseMap<DIAssignID *, DIAssignID *> &Map,
                            Instruction &I);

/// Variable identity for trackAssignments, without fragment info.
///
/// We don't use the similar DebugVariable class because trackAssignments
/// doesn't (yet?) understand partial variables (fragment info) as input and
/// want to make that clear and explicit using types. In addition, eventually
/// we will want to understand expressions that modify the base address too,
/// which a DebugVariable doesn't capture.
struct VarRecord {
  DILocalVariable *Var; ///< Local variable being tracked.
  DILocation *DL;       ///< Debug location associated with the variable.

  /// Construct from a dbg variable record.
  /// \param DVR Record providing the variable and a suitable debug location.
  VarRecord(DbgVariableRecord *DVR)
      : Var(DVR->getVariable()), DL(getDebugValueLoc(DVR)) {}
  /// Construct from an explicit variable and debug location.
  /// \param Var Local variable to track.
  /// \param DL Debug location associated with \p Var.
  VarRecord(DILocalVariable *Var, DILocation *DL) : Var(Var), DL(DL) {}
  /// Order by variable pointer, then debug location.
  /// \param LHS Left-hand VarRecord.
  /// \param RHS Right-hand VarRecord.
  /// \return True if \p LHS is ordered before \p RHS.
  friend bool operator<(const VarRecord &LHS, const VarRecord &RHS) {
    return std::tie(LHS.Var, LHS.DL) < std::tie(RHS.Var, RHS.DL);
  }
  /// Return true if both records refer to the same variable and location.
  /// \param LHS Left-hand VarRecord.
  /// \param RHS Right-hand VarRecord.
  /// \return True if both records refer to the same variable and location.
  friend bool operator==(const VarRecord &LHS, const VarRecord &RHS) {
    return std::tie(LHS.Var, LHS.DL) == std::tie(RHS.Var, RHS.DL);
  }
};

} // namespace at

/// DenseMapInfo specialization so \c at::VarRecord can be a DenseMap key.
template <> struct DenseMapInfo<at::VarRecord> {
  /// Compute a hash code for \p Var.
  /// \param Var VarRecord to hash.
  /// \return Hash code for \p Var.
  static unsigned getHashValue(const at::VarRecord &Var) {
    return hash_combine(Var.Var, Var.DL);
  }

  /// Return true if \p A and \p B are equal.
  /// \param A Left-hand VarRecord.
  /// \param B Right-hand VarRecord.
  /// \return True if \p A and \p B are equal.
  static bool isEqual(const at::VarRecord &A, const at::VarRecord &B) {
    return A == B;
  }
};

namespace at {
/// Map from alloca backing storage to variables stored there.
///
/// TODO: Backing storage shouldn't be limited to allocas only. Some local
/// variables have their storage allocated by the calling function (addresses
/// passed in with sret & byval parameters).
using StorageToVarsMap =
    DenseMap<const AllocaInst *, SmallSetVector<VarRecord, 2>>;

/// Track assignments to \p Vars between \p Start and \p End.
/// \param Start Beginning of the instruction range to scan.
/// \param End End of the instruction range to scan (exclusive).
/// \param Vars Map of alloca storage to variables to track.
/// \param DL Data layout used when analyzing stores.
/// \param DebugPrints If true, emit debug prints while tracking.
LLVM_ABI void trackAssignments(Function::iterator Start, Function::iterator End,
                               const StorageToVarsMap &Vars,
                               const DataLayout &DL, bool DebugPrints = false);

/// Describes properties of a store that has a static size and offset into a
/// some base storage. Used by the getAssignmentInfo functions.
struct AssignmentInfo {
  AllocaInst const *Base;  ///< Base storage.
  uint64_t OffsetInBits;   ///< Offset into Base.
  uint64_t SizeInBits;     ///< Number of bits stored.
  bool StoreToWholeAlloca; ///< SizeInBits equals the size of the base storage.

  /// Construct assignment info for a store into \p Base.
  /// \param DL Data layout used to size \p Base.
  /// \param Base Alloca that is the store destination.
  /// \param OffsetInBits Bit offset into \p Base.
  /// \param SizeInBits Number of bits stored.
  AssignmentInfo(const DataLayout &DL, AllocaInst const *Base,
                 uint64_t OffsetInBits, uint64_t SizeInBits)
      : Base(Base), OffsetInBits(OffsetInBits), SizeInBits(SizeInBits),
        StoreToWholeAlloca(
            OffsetInBits == 0 &&
            SizeInBits == DL.getTypeSizeInBits(Base->getAllocatedType())) {}
};

/// Return assignment info for a memory intrinsic, if it has static shape.
/// \param DL Data layout used to analyze \p I.
/// \param I Memory intrinsic to describe.
/// \return Assignment info for \p I, or nullopt if it lacks static shape.
LLVM_ABI std::optional<AssignmentInfo> getAssignmentInfo(const DataLayout &DL,
                                                         const MemIntrinsic *I);
/// Return assignment info for a store, if it has static shape.
/// \param DL Data layout used to analyze \p SI.
/// \param SI Store instruction to describe.
/// \return Assignment info for \p SI, or nullopt if it lacks static shape.
LLVM_ABI std::optional<AssignmentInfo> getAssignmentInfo(const DataLayout &DL,
                                                         const StoreInst *SI);
/// Return assignment info for an alloca used as a whole-object store.
/// \param DL Data layout used to analyze \p AI.
/// \param AI Alloca instruction to describe.
/// \return Assignment info treating \p AI as a whole-object store.
LLVM_ABI std::optional<AssignmentInfo> getAssignmentInfo(const DataLayout &DL,
                                                         const AllocaInst *AI);

} // end namespace at

/// Convert dbg.declare intrinsics into sets of dbg.assign intrinsics.
///
/// Stores to the dbg.declare'd address are treated as assignments to the
/// variable. Not all kinds of variables are supported yet; those will be left
/// with their dbg.declare intrinsics. The pass sets the
/// debug-info-assignment-tracking module flag to true to indicate assignment
/// tracking has been enabled.
class AssignmentTrackingPass
    : public OptionalPassInfoMixin<AssignmentTrackingPass> {
  /// Note: this method does not set the debug-info-assignment-tracking module
  /// flag.
  bool runOnFunction(Function &F);

public:
  /// Run assignment tracking conversion on a function.
  /// \param F Function to process.
  /// \param AM Function analysis manager.
  /// \return Analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  /// Run assignment tracking conversion on a module.
  /// \param M Module to process.
  /// \param AM Module analysis manager.
  /// \return Analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

/// Return true if assignment tracking is enabled for module \p M.
/// \param M Module whose assignment-tracking flag is checked.
/// \return True if assignment tracking is enabled for \p M.
LLVM_ABI bool isAssignmentTrackingEnabled(const Module &M);

} // end namespace llvm

#endif // LLVM_IR_DEBUGINFO_H
