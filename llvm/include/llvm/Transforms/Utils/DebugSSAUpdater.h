//===- DebugSSAUpdater.h - Debug SSA Update Tool ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the DebugSSAUpdater class, which is used to evaluate the
// live values of debug variables in IR. This uses SSA construction, treating
// debug value records as definitions, to determine at each point in the program
// which definition(s) are live at a given point. This is useful for analysis of
// the state of debug variables, such as measuring the change in values of a
// variable over time, or calculating coverage stats.
//
// NB: This is an expensive analysis that is generally not suitable for use in
// LLVM passes, but may be useful for standalone tools.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_DEBUGSSAUPDATER_H
#define LLVM_TRANSFORMS_UTILS_DEBUGSSAUPDATER_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugProgramInstruction.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>

namespace llvm {

/// PHI node that merges live debug-value definitions from predecessor blocks.
class DbgSSAPhi;
template <typename T> class SSAUpdaterTraits;

/// A definition of a variable; can represent either a debug.
///
/// value, no definition (the variable has not yet been defined), or a phi value*. *Meaning multiple definitions that are live-in to a block from different predecessors, not a debug value that uses an IR PHINode.
struct DbgValueDef {
  /// SSA phi that produces this definition, or null if this is a concrete def.
  DbgSSAPhi *Phi;
  /// True when this definition is undef (the variable is not yet defined).
  bool IsUndef;
  /// True when \c Locations describes a memory address of the variable.
  bool IsMemory;
  /// Metadata describing the location(s) of the variable value.
  Metadata *Locations;
  /// DIExpression applied to \c Locations for this definition.
  DIExpression *Expression;

  /// Construct an undef definition with no locations or expression.
  DbgValueDef()
      : Phi(nullptr), IsUndef(true), IsMemory(false), Locations(nullptr),
        Expression(nullptr) {}
  /// Construct an undef definition; the integer argument is ignored.
  /// \param Unused Ignored; present only to select this overload.
  DbgValueDef(int Unused)
      : Phi(nullptr), IsUndef(true), IsMemory(false), Locations(nullptr),
        Expression(nullptr) {}
  /// Construct a concrete definition from location metadata and expression.
  /// \param IsMemory Whether \p Locations is a memory address of the variable.
  /// \param Locations Metadata describing the value or address location(s).
  /// \param Expression DIExpression applied to \p Locations.
  DbgValueDef(bool IsMemory, Metadata *Locations, DIExpression *Expression)
      : Phi(nullptr), IsUndef(false), IsMemory(IsMemory), Locations(Locations),
        Expression(Expression) {}
  /// Construct a definition from a \c DbgVariableRecord.
  /// \param DVR Debug variable record to copy location and expression from.
  DbgValueDef(DbgVariableRecord *DVR) : Phi(nullptr) {
    assert(!DVR->isDbgAssign() && "#dbg_assign not yet supported");
    IsUndef = DVR->isKillLocation();
    IsMemory = DVR->isAddressOfVariable();
    Locations = DVR->getRawLocation();
    Expression = DVR->getExpression();
  }
  /// Construct a definition that refers to an SSA phi.
  /// \param Phi Phi whose merged value this definition represents.
  DbgValueDef(DbgSSAPhi *Phi)
      : Phi(Phi), IsUndef(false), IsMemory(false), Locations(nullptr),
        Expression(nullptr) {}

  /// Return true if this definition agrees with \p Other.
  ///
  /// Undef definitions always agree with each other; otherwise all fields must
  /// match.
  /// \param Other Other definition to compare against.
  /// \return True if the definitions agree.
  bool agreesWith(DbgValueDef Other) const {
    if (IsUndef && Other.IsUndef)
      return true;
    return std::tie(Phi, IsUndef, IsMemory, Locations, Expression) ==
           std::tie(Other.Phi, Other.IsUndef, Other.IsMemory, Other.Locations,
                    Other.Expression);
  }

  /// Return true if this is not an undef definition.
  /// \return True if this definition is not undef.
  operator bool() const { return !IsUndef; }
  /// Return true if this definition agrees with \p Other.
  /// \param Other Other definition to compare against.
  /// \return True if the definitions agree.
  bool operator==(DbgValueDef Other) const { return agreesWith(Other); }
  /// Return true if this definition does not agree with \p Other.
  /// \param Other Other definition to compare against.
  /// \return True if the definitions do not agree.
  bool operator!=(DbgValueDef Other) const { return !agreesWith(Other); }

  /// Print this definition to \p OS.
  /// \param OS Stream to print to.
  LLVM_ABI void print(raw_ostream &OS) const;
};

class DbgSSABlock;
class DebugSSAUpdater;

/// PHI node that merges live debug-value definitions from predecessor blocks.
class DbgSSAPhi {
public:
  /// Incoming (predecessor block, value) pairs for this phi.
  SmallVector<std::pair<DbgSSABlock *, DbgValueDef>, 4> IncomingValues;
  /// Block that owns this phi.
  DbgSSABlock *ParentBlock;
  /// Construct a phi owned by \p ParentBlock.
  /// \param ParentBlock Block that will own this phi.
  DbgSSAPhi(DbgSSABlock *ParentBlock) : ParentBlock(ParentBlock) {}

  /// Return the block that owns this phi.
  /// \return The parent \c DbgSSABlock.
  DbgSSABlock *getParent() { return ParentBlock; }
  /// Return the number of incoming values.
  /// \return The number of incoming (block, value) pairs.
  unsigned getNumIncomingValues() const { return IncomingValues.size(); }
  /// Return the predecessor block for incoming value \p Idx.
  /// \param Idx Index into the incoming-value list.
  /// \return The predecessor block for the incoming value at \p Idx.
  DbgSSABlock *getIncomingBlock(size_t Idx) {
    return IncomingValues[Idx].first;
  }
  /// Return the incoming value at index \p Idx.
  /// \param Idx Index into the incoming-value list.
  /// \return The incoming debug value at \p Idx.
  DbgValueDef getIncomingValue(size_t Idx) {
    return IncomingValues[Idx].second;
  }
  /// Append an incoming value from predecessor \p BB.
  /// \param BB Predecessor block providing \p DV.
  /// \param DV Debug value live out of \p BB.
  void addIncoming(DbgSSABlock *BB, DbgValueDef DV) {
    IncomingValues.push_back({BB, DV});
  }

  /// Print this phi to \p OS.
  /// \param OS Stream to print to.
  LLVM_ABI void print(raw_ostream &OS) const;
};

/// Print \p DV to \p OS.
/// \param OS Stream to print to.
/// \param DV Debug value definition to print.
/// \return The output stream \p OS.
inline raw_ostream &operator<<(raw_ostream &OS, const DbgValueDef &DV) {
  DV.print(OS);
  return OS;
}
/// Print \p PHI to \p OS.
/// \param OS Stream to print to.
/// \param PHI Debug SSA phi to print.
/// \return The output stream \p OS.
inline raw_ostream &operator<<(raw_ostream &OS, const DbgSSAPhi &PHI) {
  PHI.print(OS);
  return OS;
}

/// Thin wrapper around a block successor iterator.
class DbgSSABlockSuccIterator {
public:
  /// Underlying IR successor iterator.
  succ_iterator SuccIt;
  /// Updater used to map IR blocks to \c DbgSSABlock wrappers.
  DebugSSAUpdater &Updater;

  /// Construct an iterator over successors starting at \p SuccIt.
  /// \param SuccIt Underlying IR successor iterator.
  /// \param Updater Updater that owns \c DbgSSABlock wrappers.
  DbgSSABlockSuccIterator(succ_iterator SuccIt, DebugSSAUpdater &Updater)
      : SuccIt(SuccIt), Updater(Updater) {}

  /// Return true if this iterator does not equal \p OtherIt.
  /// \param OtherIt Iterator to compare against.
  /// \return True if the iterators are not equal.
  bool operator!=(const DbgSSABlockSuccIterator &OtherIt) const {
    return OtherIt.SuccIt != SuccIt;
  }

  /// Advance to the next successor and return this iterator.
  /// \return Reference to this iterator.
  DbgSSABlockSuccIterator &operator++() {
    ++SuccIt;
    return *this;
  }

  /// Return the \c DbgSSABlock for the current successor.
  /// \return The \c DbgSSABlock for the current successor.
  LLVM_ABI DbgSSABlock *operator*();
};

/// Thin wrapper around a block successor iterator.
class DbgSSABlockPredIterator {
public:
  /// Underlying IR predecessor iterator.
  pred_iterator PredIt;
  /// Updater used to map IR blocks to \c DbgSSABlock wrappers.
  DebugSSAUpdater &Updater;

  /// Construct an iterator over predecessors starting at \p PredIt.
  /// \param PredIt Underlying IR predecessor iterator.
  /// \param Updater Updater that owns \c DbgSSABlock wrappers.
  DbgSSABlockPredIterator(pred_iterator PredIt, DebugSSAUpdater &Updater)
      : PredIt(PredIt), Updater(Updater) {}

  /// Return true if this iterator does not equal \p OtherIt.
  /// \param OtherIt Iterator to compare against.
  /// \return True if the iterators are not equal.
  bool operator!=(const DbgSSABlockPredIterator &OtherIt) const {
    return OtherIt.PredIt != PredIt;
  }

  /// Advance to the next predecessor and return this iterator.
  /// \return Reference to this iterator.
  DbgSSABlockPredIterator &operator++() {
    ++PredIt;
    return *this;
  }

  /// Return the \c DbgSSABlock for the current predecessor.
  /// \return The \c DbgSSABlock for the current predecessor.
  LLVM_ABI DbgSSABlock *operator*();
};

/// Wrapper around an IR \c BasicBlock for debug-variable SSA construction.
class DbgSSABlock {
public:
  /// Underlying IR basic block.
  BasicBlock &BB;
  /// Updater that owns this block wrapper.
  DebugSSAUpdater &Updater;
  /// List type used to store PHIs created for this block.
  using PHIListT = SmallVector<DbgSSAPhi, 1>;
  /// List of PHIs in this block. There should only ever be one, but this needs
  /// to be a list for the SSAUpdater.
  PHIListT PHIList;

  /// Construct a wrapper for \p BB owned by \p Updater.
  /// \param BB IR basic block to wrap.
  /// \param Updater Updater that owns this wrapper.
  DbgSSABlock(BasicBlock &BB, DebugSSAUpdater &Updater)
      : BB(BB), Updater(Updater) {}

  /// Return an iterator to the first predecessor \c DbgSSABlock.
  /// \return Iterator to the first predecessor.
  DbgSSABlockPredIterator pred_begin() {
    return DbgSSABlockPredIterator(llvm::pred_begin(&BB), Updater);
  }

  /// Return an iterator past the last predecessor \c DbgSSABlock.
  /// \return Iterator past the last predecessor.
  DbgSSABlockPredIterator pred_end() {
    return DbgSSABlockPredIterator(llvm::pred_end(&BB), Updater);
  }

  /// Return the range of predecessor \c DbgSSABlock wrappers.
  /// \return Range covering all predecessor \c DbgSSABlock wrappers.
  iterator_range<DbgSSABlockPredIterator> predecessors() {
    return iterator_range(pred_begin(), pred_end());
  }

  /// Return an iterator to the first successor \c DbgSSABlock.
  /// \return Iterator to the first successor.
  DbgSSABlockSuccIterator succ_begin() {
    return DbgSSABlockSuccIterator(llvm::succ_begin(&BB), Updater);
  }

  /// Return an iterator past the last successor \c DbgSSABlock.
  /// \return Iterator past the last successor.
  DbgSSABlockSuccIterator succ_end() {
    return DbgSSABlockSuccIterator(llvm::succ_end(&BB), Updater);
  }

  /// Return the range of successor \c DbgSSABlock wrappers.
  /// \return Range covering all successor \c DbgSSABlock wrappers.
  iterator_range<DbgSSABlockSuccIterator> successors() {
    return iterator_range(succ_begin(), succ_end());
  }

  /// SSAUpdater has requested a PHI: create that within this block record.
  /// \return Pointer to the newly created PHI.
  DbgSSAPhi *newPHI() {
    assert(PHIList.empty() &&
           "Only one PHI should exist per-block per-variable");
    PHIList.emplace_back(this);
    return &PHIList.back();
  }

  /// SSAUpdater wishes to know what PHIs already exist in this block.
  /// \return Reference to the list of PHIs in this block.
  PHIListT &phis() { return PHIList; }
};

/// Class used to determine the live ranges of debug variables in IR using
/// SSA construction (via the SSAUpdaterImpl class), used for analysis purposes.
class DebugSSAUpdater {
  friend class SSAUpdaterTraits<DebugSSAUpdater>;
  using AvailableValsTy = DenseMap<DbgSSABlock *, DbgValueDef>;

private:
  /// This keeps track of which value to use on a per-block basis. When we
  /// insert PHI nodes, we keep track of them here.
  AvailableValsTy AV;

  /// Pointer to an optionally-passed vector into which, if it is non-null,
  /// the PHIs that describe ambiguous variable locations will be inserted.
  SmallVectorImpl<DbgSSAPhi *> *InsertedPHIs;

  DenseMap<BasicBlock *, DbgSSABlock *> BlockMap;

public:
  /// If InsertedPHIs is specified, it will be filled
  /// in with all PHI Nodes created by rewriting.
  /// \param InsertedPHIs Optional vector to receive created PHI nodes.
  LLVM_ABI explicit DebugSSAUpdater(
      SmallVectorImpl<DbgSSAPhi *> *InsertedPHIs = nullptr);
  /// Deleted copy constructor.
  /// \param Other Unused; copy construction is not allowed.
  DebugSSAUpdater(const DebugSSAUpdater &Other) = delete;
  /// Deleted copy assignment.
  /// \param Other Unused; copy assignment is not allowed.
  DebugSSAUpdater &operator=(const DebugSSAUpdater &Other) = delete;

  /// Destroy owned \c DbgSSABlock wrappers.
  ~DebugSSAUpdater() {
    for (auto &Block : BlockMap)
      delete Block.second;
  }

  /// Reset available values and destroy owned block wrappers.
  void reset() {
    for (auto &Block : BlockMap)
      delete Block.second;

    if (InsertedPHIs)
      InsertedPHIs->clear();
    BlockMap.clear();
  }

  /// Clear available values in preparation for a new SSA construction.
  LLVM_ABI void initialize();

  /// For a given BB, create a wrapper block for it. Stores it in the
  /// DebugSSAUpdater block map.
  /// \param BB IR basic block to wrap or look up.
  /// \return The \c DbgSSABlock wrapper for \p BB.
  DbgSSABlock *getDbgSSABlock(BasicBlock *BB) {
    auto it = BlockMap.find(BB);
    if (it == BlockMap.end()) {
      BlockMap[BB] = new DbgSSABlock(*BB, *this);
      it = BlockMap.find(BB);
    }
    return it->second;
  }

  /// Indicate that a rewritten value is available in the specified block
  /// with the specified value.
  /// \param BB Block in which \p DV is available.
  /// \param DV Debug value available in \p BB.
  LLVM_ABI void addAvailableValue(DbgSSABlock *BB, DbgValueDef DV);

  /// Return true if the DebugSSAUpdater already has a value for the specified
  /// block.
  /// \param BB Block to query.
  /// \return True if a value is already available for \p BB.
  LLVM_ABI bool hasValueForBlock(DbgSSABlock *BB) const;

  /// Return the value for the specified block if the DebugSSAUpdater has one,
  /// otherwise return nullptr.
  /// \param BB Block to look up.
  /// \return The available value for \p BB, or an empty value if none.
  LLVM_ABI DbgValueDef findValueForBlock(DbgSSABlock *BB) const;

  /// Construct SSA form, materializing a value that is live at the end
  /// of the specified block.
  /// \param BB Block whose live-out value is requested.
  /// \return The debug value live at the end of \p BB.
  LLVM_ABI DbgValueDef getValueAtEndOfBlock(DbgSSABlock *BB);

  /// Construct SSA form, materializing a value that is live in the
  /// middle of the specified block.
  ///
  /// \c getValueInMiddleOfBlock is the same as \c GetValueAtEndOfBlock except
  /// in one important case: if there is a definition of the rewritten value
  /// after the 'use' in BB.  Consider code like this:
  ///
  /// \code
  ///      X1 = ...
  ///   SomeBB:
  ///      use(X)
  ///      X2 = ...
  ///      br Cond, SomeBB, OutBB
  /// \endcode
  ///
  /// In this case, there are two values (X1 and X2) added to the AvailableVals
  /// set by the client of the rewriter, and those values are both live out of
  /// their respective blocks.  However, the use of X happens in the *middle* of
  /// a block.  Because of this, we need to insert a new PHI node in SomeBB to
  /// merge the appropriate values, and this value isn't live out of the block.
  /// \param BB Block whose mid-block live value is requested.
  /// \return The debug value live in the middle of \p BB.
  LLVM_ABI DbgValueDef getValueInMiddleOfBlock(DbgSSABlock *BB);

private:
  DbgValueDef getValueAtEndOfBlockInternal(DbgSSABlock *BB);
};

/// Interval in a basic block where a single debug value is live.
struct DbgRangeEntry {
  /// First instruction in the live range (inclusive).
  BasicBlock::iterator Start;
  /// One-past-the-last instruction in the live range.
  BasicBlock::iterator End;
  // Should be non-PHI.
  /// Non-PHI debug value live over [\c Start, \c End).
  DbgValueDef Value;
};

/// Utility class used to store the names of SSA values after their owning modules have been destroyed.
///
/// Values are added via \c addValue to receive a corresponding ID, which can then be used to retrieve the name of the SSA value via \c getName at any point. Adding the same value multiple times returns the same ID, making \c addValue idempotent.
class SSAValueNameMap {
  struct Config : ValueMapConfig<Value *> {
    enum { FollowRAUW = false };
  };

public:
  /// Opaque identifier for a recorded SSA value name.
  using ValueID = uint64_t;
  /// Record \p V and return a stable ID for its name.
  /// \param V Value whose name should be stored.
  /// \return A stable ID corresponding to the stored name of \p V.
  LLVM_ABI ValueID addValue(Value *V);
  /// Return the stored name for \p ID.
  /// \param ID Identifier previously returned by \c addValue.
  /// \return The name previously recorded for \p ID.
  std::string getName(ValueID ID) { return ValueIDToNameMap[ID]; }

private:
  DenseMap<ValueID, std::string> ValueIDToNameMap;
  ValueMap<Value *, ValueID, Config> ValueToIDMap;
  ValueID NextID = 0;
};

/// Utility class used to find and store the live debug ranges for variables in a module.
///
/// This class uses the DebugSSAUpdater for each variable added with \c addVariable to find either a single-location value, e.g. #dbg_declare, or a set of live value ranges corresponding to the set of #dbg_value records.
/// \c addVariable to find either a single-location value, e.g. #dbg_declare, or
class DbgValueRangeTable {
  DenseMap<DebugVariableAggregate, SmallVector<DbgRangeEntry>>
      OrigVariableValueRangeTable;
  DenseMap<DebugVariableAggregate, DbgValueDef> OrigSingleLocVariableValueTable;

public:
  /// Compute and store live ranges (or a single location) for \p DVA in \p F.
  /// \param F Function containing \p DVA.
  /// \param DVA Debug variable aggregate to analyze.
  LLVM_ABI void addVariable(Function *F, DebugVariableAggregate DVA);
  /// Return true if \p DVA has a stored range or single-location entry.
  /// \param DVA Debug variable aggregate to query.
  /// \return True if \p DVA has a stored range or single-location entry.
  bool hasVariableEntry(DebugVariableAggregate DVA) const {
    return OrigVariableValueRangeTable.contains(DVA) ||
           OrigSingleLocVariableValueTable.contains(DVA);
  }
  /// Return true if \p DVA has a stored single-location entry.
  /// \param DVA Debug variable aggregate to query.
  /// \return True if \p DVA has a stored single-location entry.
  bool hasSingleLocEntry(DebugVariableAggregate DVA) const {
    return OrigSingleLocVariableValueTable.contains(DVA);
  }
  /// Return the live value ranges stored for \p DVA.
  /// \param DVA Debug variable aggregate whose ranges are requested.
  /// \return The live value ranges stored for \p DVA.
  ArrayRef<DbgRangeEntry> getVariableRanges(DebugVariableAggregate DVA) {
    return OrigVariableValueRangeTable[DVA];
  }
  /// Return the single-location value stored for \p DVA.
  /// \param DVA Debug variable aggregate whose single location is requested.
  /// \return The single-location value stored for \p DVA.
  DbgValueDef getSingleLoc(DebugVariableAggregate DVA) {
    return OrigSingleLocVariableValueTable[DVA];
  }

  /// Print stored values for \p DVA to \p OS.
  /// \param DVA Debug variable aggregate to print.
  /// \param OS Stream to print to.
  LLVM_ABI void printValues(DebugVariableAggregate DVA, raw_ostream &OS);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_DEBUGSSAUPDATER_H
