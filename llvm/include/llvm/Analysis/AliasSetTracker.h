//===- llvm/Analysis/AliasSetTracker.h - Build Alias Sets -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines two classes: AliasSetTracker and AliasSet. These interfaces
// are used to classify a collection of memory locations into a maximal number
// of disjoint sets. Each AliasSet object constructed by the AliasSetTracker
// object refers to memory disjoint from the other sets.
//
// An AliasSetTracker can only be used on immutable IR.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_ALIASSETTRACKER_H
#define LLVM_ANALYSIS_ALIASSETTRACKER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/ilist.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ModRef.h"
#include <cassert>
#include <vector>

namespace llvm {

class AliasResult;
class AliasSetTracker;
class AnyMemSetInst;
class AnyMemTransferInst;
class BasicBlock;
class BatchAAResults;
class Function;
class Instruction;
class StoreInst;
class LoadInst;
class raw_ostream;
class VAArgInst;
class Value;

/// A set of memory locations that may alias each other.
///
/// Each AliasSet constructed by an AliasSetTracker refers to memory disjoint
/// from the other sets held by that tracker.
class AliasSet : public ilist_node<AliasSet> {
  friend class AliasSetTracker;

  // Forwarding pointer.
  AliasSet *Forward = nullptr;

  /// Memory locations in this alias set.
  SmallVector<MemoryLocation, 0> MemoryLocs;

  /// All instructions without a specific address in this alias set.
  std::vector<AssertingVH<Instruction>> UnknownInsts;

  /// Number of nodes pointing to this AliasSet plus the number of AliasSets
  /// forwarding to it.
  unsigned RefCount : 30;

  // Signifies that this set should be considered to alias any pointer.
  // Use when the tracker holding this set is saturated.
  unsigned AliasAny : 1;

  /// The kind of alias relationship between pointers of the set.
  ///
  /// These represent conservatively correct alias results between any members
  /// of the set. We represent these independently of the values of alias
  /// results in order to pack it into a single bit. Lattice goes from
  /// MustAlias to MayAlias.
  enum AliasLattice {
    SetMustAlias = 0, SetMayAlias = 1
  };
  unsigned Alias : 1;

  // The kinds of access this alias set models.
  ModRefInfo Access;

  void addRef() { ++RefCount; }

  void dropRef(AliasSetTracker &AST) {
    assert(RefCount >= 1 && "Invalid reference count detected!");
    if (--RefCount == 0)
      removeFromTracker(AST);
  }

public:
  /// Deleted copy constructor.
  /// @param Other Unused; copy construction is deleted.
  AliasSet(const AliasSet &Other) = delete;
  /// Deleted copy assignment.
  /// @param Other Unused; copy assignment is deleted.
  AliasSet &operator=(const AliasSet &Other) = delete;

  /// Return true if this set models a referencing access.
  /// @return True if this set models a referencing access.
  bool isRef() const { return isRefSet(Access); }
  /// Return true if this set models a modifying access.
  /// @return True if this set models a modifying access.
  bool isMod() const { return isModSet(Access); }
  /// Return true if all pointers in this set must alias each other.
  /// @return True if all pointers in this set must alias each other.
  bool isMustAlias() const { return Alias == SetMustAlias; }
  /// Return true if pointers in this set may alias each other.
  /// @return True if pointers in this set may alias each other.
  bool isMayAlias()  const { return Alias == SetMayAlias; }

  /// Return true if this alias set should be ignored as part of the
  /// AliasSetTracker object.
  /// @return True if this set forwards to another and should be ignored.
  bool isForwardingAliasSet() const { return Forward; }

  /// Merge the specified alias set into this alias set.
  /// @param AS Alias set to merge into this one.
  /// @param AST Tracker that owns both alias sets.
  /// @param BatchAA Batch alias analysis used while merging.
  LLVM_ABI void mergeSetIn(AliasSet &AS, AliasSetTracker &AST,
                           BatchAAResults &BatchAA);

  // Alias Set iteration - Allow access to all of the memory locations which are
  // part of this alias set.
  /// Const iterator over the memory locations in this alias set.
  using iterator = SmallVectorImpl<MemoryLocation>::const_iterator;
  /// Return an iterator to the first memory location in this set.
  /// @return An iterator to the first memory location in this set.
  iterator begin() const { return MemoryLocs.begin(); }
  /// Return an iterator past the last memory location in this set.
  /// @return An iterator past the last memory location in this set.
  iterator end() const { return MemoryLocs.end(); }

  /// Return the number of memory locations in this alias set.
  /// @return The number of memory locations in this alias set.
  unsigned size() const { return MemoryLocs.size(); }

  /// Vector of distinct pointer values from this set's memory locations.
  using PointerVector = SmallVector<const Value *, 8>;
  /// Retrieve the pointer values for the memory locations in this alias set.
  ///
  /// The order matches that of the memory locations, but duplicate pointer
  /// values are omitted.
  /// @return Distinct pointer values from this set's memory locations.
  LLVM_ABI PointerVector getPointers() const;

  /// Print this alias set to \p OS.
  /// @param OS Output stream.
  LLVM_ABI void print(raw_ostream &OS) const;
  /// Dump this alias set to stderr for debugging.
  LLVM_ABI void dump() const;

private:
  // Can only be created by AliasSetTracker.
  AliasSet()
      : RefCount(0), AliasAny(false), Alias(SetMustAlias),
        Access(ModRefInfo::NoModRef) {}

  LLVM_ABI void removeFromTracker(AliasSetTracker &AST);

  void addMemoryLocation(AliasSetTracker &AST, const MemoryLocation &MemLoc,
                         bool KnownMustAlias = false);
  void addUnknownInst(Instruction *I, BatchAAResults &AA);

public:
  /// If the specified memory location "may" (or must) alias one of the members
  /// in the set return the appropriate AliasResult. Otherwise return NoAlias.
  /// @param MemLoc Memory location to test against this set.
  /// @param AA Batch alias analysis used for the query.
  /// @return An AliasResult for \p MemLoc against this set, or NoAlias.
  LLVM_ABI AliasResult aliasesMemoryLocation(const MemoryLocation &MemLoc,
                                             BatchAAResults &AA) const;

  /// Return how instruction \p Inst may modify or reference this alias set.
  /// @param Inst Instruction with unknown memory effects to test.
  /// @param AA Batch alias analysis used for the query.
  /// @return ModRef info describing how \p Inst may access this set.
  LLVM_ABI ModRefInfo aliasesUnknownInst(const Instruction *Inst,
                                         BatchAAResults &AA) const;
};

/// Write AliasSet \p AS to stream \p OS.
/// @param OS Output stream.
/// @param AS Alias set to print.
/// @return The stream \p OS after writing.
inline raw_ostream& operator<<(raw_ostream &OS, const AliasSet &AS) {
  AS.print(OS);
  return OS;
}

/// Tracks a collection of disjoint AliasSets over immutable IR.
///
/// Classifies memory locations into a maximal number of disjoint sets. Each
/// AliasSet refers to memory disjoint from the other sets held by this tracker.
class AliasSetTracker {
  BatchAAResults &AA;
  ilist<AliasSet> AliasSets;

  using PointerMapType = DenseMap<AssertingVH<const Value>, AliasSet *>;

  // Map from pointer values to the alias set holding one or more memory
  // locations with that pointer value.
  PointerMapType PointerMap;

public:
  /// Create an empty collection of AliasSets, and use the specified alias
  /// analysis object to disambiguate load and store addresses.
  /// @param AA Batch alias analysis used to classify memory locations.
  explicit AliasSetTracker(BatchAAResults &AA) : AA(AA) {}
  /// Destroy this tracker and clear all alias sets.
  ~AliasSetTracker() { clear(); }

  /// Add memory location \p Loc to the tracker.
  ///
  /// Adding a location or instruction can create a new set, add to exactly one
  /// set, or merge multiple aliasing sets before incorporating the new item.
  /// @param Loc Memory location to incorporate.
  LLVM_ABI void add(const MemoryLocation &Loc);
  /// Add load instruction \p LI to the tracker.
  /// @param LI Load to incorporate.
  LLVM_ABI void add(LoadInst *LI);
  /// Add store instruction \p SI to the tracker.
  /// @param SI Store to incorporate.
  LLVM_ABI void add(StoreInst *SI);
  /// Add store \p SI without using its AA metadata tags.
  /// @param SI Store to incorporate without AA tags.
  LLVM_ABI void addWithoutAATags(StoreInst *SI);
  /// Add va_arg instruction \p VAAI to the tracker.
  /// @param VAAI VAArg instruction to incorporate.
  LLVM_ABI void add(VAArgInst *VAAI);
  /// Add memset-like intrinsic \p MSI to the tracker.
  /// @param MSI Memset intrinsic to incorporate.
  LLVM_ABI void add(AnyMemSetInst *MSI);
  /// Add memtransfer-like intrinsic \p MTI to the tracker.
  /// @param MTI Memtransfer intrinsic to incorporate.
  LLVM_ABI void add(AnyMemTransferInst *MTI);
  /// Add instruction \p I by dispatching to a typed add overload.
  /// @param I Instruction to incorporate.
  LLVM_ABI void
  add(Instruction *I); // Dispatch to one of the other add methods...
  /// Add all instructions in basic block \p BB to the tracker.
  /// @param BB Basic block whose instructions are incorporated.
  LLVM_ABI void add(BasicBlock &BB); // Add all instructions in basic block
  /// Merge alias relations from another tracker \p AST into this one.
  /// @param AST Source alias set tracker whose relations are added.
  LLVM_ABI void
  add(const AliasSetTracker &AST); // Add alias relations from another AST
  /// Add an instruction with an unknown memory effect to the tracker.
  /// @param I Instruction with unknown aliasing to incorporate.
  LLVM_ABI void addUnknown(Instruction *I);

  /// Remove all alias sets from this tracker.
  LLVM_ABI void clear();

  /// Return the alias sets that are active.
  /// @return The list of active alias sets owned by this tracker.
  const ilist<AliasSet> &getAliasSets() const { return AliasSets; }

  /// Return the alias set that contains \p MemLoc, merging sets if needed.
  ///
  /// If the memory location aliases two or more existing alias sets, those
  /// sets are merged before the single resulting alias set is returned.
  /// @param MemLoc Memory location whose alias set is requested.
  /// @return The alias set that contains \p MemLoc.
  LLVM_ABI AliasSet &getAliasSetFor(const MemoryLocation &MemLoc);

  /// Return the underlying alias analysis object used by this tracker.
  /// @return The batch alias analysis results used by this tracker.
  BatchAAResults &getAliasAnalysis() const { return AA; }

  /// Mutable iterator over the alias sets in this tracker.
  using iterator = ilist<AliasSet>::iterator;
  /// Const iterator over the alias sets in this tracker.
  using const_iterator = ilist<AliasSet>::const_iterator;

  /// Return a const iterator to the first alias set.
  /// @return A const iterator to the first alias set.
  const_iterator begin() const { return AliasSets.begin(); }
  /// Return a const iterator past the last alias set.
  /// @return A const iterator past the last alias set.
  const_iterator end()   const { return AliasSets.end(); }

  /// Return an iterator to the first alias set.
  /// @return An iterator to the first alias set.
  iterator begin() { return AliasSets.begin(); }
  /// Return an iterator past the last alias set.
  /// @return An iterator past the last alias set.
  iterator end()   { return AliasSets.end(); }

  /// Print this alias set tracker to \p OS.
  /// @param OS Output stream.
  LLVM_ABI void print(raw_ostream &OS) const;
  /// Dump this alias set tracker to stderr for debugging.
  LLVM_ABI void dump() const;

private:
  friend class AliasSet;

  // The total number of memory locations contained in all alias sets.
  unsigned TotalAliasSetSize = 0;

  // A non-null value signifies this AST is saturated. A saturated AST lumps
  // all elements into a single "May" set.
  AliasSet *AliasAnyAS = nullptr;

  void removeAliasSet(AliasSet *AS);

  // Update an alias set field to point to its real destination. If the field is
  // pointing to a set that has been merged with another set and is forwarding,
  // the field is updated to point to the set obtained by following the
  // forwarding links. The Forward fields of intermediate alias sets are
  // collapsed as well, and alias set reference counts are updated to reflect
  // the new situation.
  void collapseForwardingIn(AliasSet *&AS) {
    if (AS->Forward) {
      collapseForwardingIn(AS->Forward);
      // Swap out AS for AS->Forward, while updating reference counts.
      AliasSet *NewAS = AS->Forward;
      NewAS->addRef();
      AS->dropRef(*this);
      AS = NewAS;
    }
  }

  AliasSet &addMemoryLocation(MemoryLocation Loc, ModRefInfo MR);
  AliasSet *mergeAliasSetsForMemoryLocation(const MemoryLocation &MemLoc,
                                            AliasSet *PtrAS,
                                            bool &MustAliasAll);

  /// Merge all alias sets into a single set that is considered to alias
  /// any memory location or instruction.
  AliasSet &mergeAllAliasSets();

  AliasSet *findAliasSetForUnknownInst(Instruction *Inst);
};

/// Write AliasSetTracker \p AST to stream \p OS.
/// @param OS Output stream.
/// @param AST Alias set tracker to print.
/// @return The stream \p OS after writing.
inline raw_ostream& operator<<(raw_ostream &OS, const AliasSetTracker &AST) {
  AST.print(OS);
  return OS;
}

/// Printer pass for \c AliasSetTracker results.
class AliasSetsPrinterPass
    : public RequiredPassInfoMixin<AliasSetsPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes alias sets to \p OS.
  /// @param OS Output stream for the printed alias sets.
  LLVM_ABI explicit AliasSetsPrinterPass(raw_ostream &OS);

  /// Print alias sets for \p F and return all analyses preserved.
  /// @param F Function whose alias sets are printed.
  /// @param AM Function analysis manager providing analyses.
  /// @return A set indicating that all analyses are preserved.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_ALIASSETTRACKER_H
