//===- llvm/Analysis/MemoryDependenceAnalysis.h - Memory Deps ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the MemoryDependenceAnalysis analysis pass.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_MEMORYDEPENDENCEANALYSIS_H
#define LLVM_ANALYSIS_MEMORYDEPENDENCEANALYSIS_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PointerEmbeddedInt.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/PointerSumType.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/PHITransAddr.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/PredIteratorCache.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Pass.h"
#include <optional>

namespace llvm {

class AssumptionCache;
class DominatorTree;
class PHITransAddr;

/// A memory dependence query can return one of three different answers.
class MemDepResult {
  enum DepType {
    /// Clients of MemDep never see this.
    ///
    /// Entries with this marker occur in a LocalDeps map or NonLocalDeps map
    /// when the instruction they previously referenced was removed from
    /// MemDep.  In either case, the entry may include an instruction pointer.
    /// If so, the pointer is an instruction in the block where scanning can
    /// start from, saving some work.
    ///
    /// In a default-constructed MemDepResult object, the type will be Invalid
    /// and the instruction pointer will be null.
    Invalid = 0,

    /// This is a dependence on the specified instruction which clobbers the
    /// desired value.  The pointer member of the MemDepResult pair holds the
    /// instruction that clobbers the memory.  For example, this occurs when we
    /// see a may-aliased store to the memory location we care about.
    ///
    /// There are several cases that may be interesting here:
    ///   1. Loads are clobbered by may-alias stores.
    ///   2. Loads are considered clobbered by partially-aliased loads.  The
    ///      client may choose to analyze deeper into these cases.
    Clobber,

    /// This is a dependence on the specified instruction which defines or
    /// produces the desired memory location.  The pointer member of the
    /// MemDepResult pair holds the instruction that defines the memory.
    ///
    /// Cases of interest:
    ///   1. This could be a load or store for dependence queries on
    ///      load/store.  The value loaded or stored is the produced value.
    ///      Note that the pointer operand may be different than that of the
    ///      queried pointer due to must aliases and phi translation. Note
    ///      that the def may not be the same type as the query, the pointers
    ///      may just be must aliases.
    ///   2. For loads and stores, this could be an allocation instruction. In
    ///      this case, the load is loading an undef value or a store is the
    ///      first store to (that part of) the allocation.
    ///   3. Dependence queries on calls return Def only when they are readonly
    ///      calls or memory use intrinsics with identical callees and no
    ///      intervening clobbers.  No validation is done that the operands to
    ///      the calls are the same.
    ///   4. For loads and stores, this could be a select instruction that
    ///      defines pointer to this memory location. In this case, users can
    ///      find non-clobbered Defs for both select values that are reaching
    //       the desired memory location (there is still a guarantee that there
    //       are no clobbers between analyzed memory location and select).
    Def,

    /// This marker indicates that the query has no known dependency in the
    /// specified block.
    ///
    /// More detailed state info is encoded in the upper part of the pair (i.e.
    /// the Instruction*)
    Other
  };

  /// If DepType is "Other", the upper part of the sum type is an encoding of
  /// the following more detailed type information.
  enum OtherType {
    /// This marker indicates that the query has no dependency in the specified
    /// block.
    ///
    /// To find out more, the client should query other predecessor blocks.
    NonLocal = 1,
    /// This marker indicates that the query has no dependency in the specified
    /// function.
    NonFuncLocal,
    /// This marker indicates that the query depends on a select instruction,
    /// i.e. the address loaded is a select whose two sides each reach a
    /// non-clobbered value (see the \p Def documentation, item 4).
    Select,
    /// This marker indicates that the query dependency is unknown.
    Unknown
  };

  using ValueTy = PointerSumType<
      DepType, PointerSumTypeMember<Invalid, Instruction *>,
      PointerSumTypeMember<Clobber, Instruction *>,
      PointerSumTypeMember<Def, Instruction *>,
      PointerSumTypeMember<Other, PointerEmbeddedInt<OtherType, 3>>>;
  ValueTy Value;

  explicit MemDepResult(ValueTy V) : Value(V) {}

public:
  /// Construct a default MemDepResult in the Invalid state with a null instruction.
  MemDepResult() = default;

  /// Create a Def result that depends on \p Inst.
  /// @param Inst Instruction that defines or produces the memory location.
  /// @return A Def MemDepResult that depends on \p Inst.
  static MemDepResult getDef(Instruction *Inst) {
    assert(Inst && "Def requires inst");
    return MemDepResult(ValueTy::create<Def>(Inst));
  }
  /// Create a Clobber result that depends on \p Inst.
  /// @param Inst Instruction that clobbers the queried memory location.
  /// @return A Clobber MemDepResult that depends on \p Inst.
  static MemDepResult getClobber(Instruction *Inst) {
    assert(Inst && "Clobber requires inst");
    return MemDepResult(ValueTy::create<Clobber>(Inst));
  }
  /// Create a NonLocal result indicating no dependency in the queried block.
  /// @return A NonLocal MemDepResult.
  static MemDepResult getNonLocal() {
    return MemDepResult(ValueTy::create<Other>(NonLocal));
  }
  /// Create a NonFuncLocal result indicating no dependency in the function.
  /// @return A NonFuncLocal MemDepResult.
  static MemDepResult getNonFuncLocal() {
    return MemDepResult(ValueTy::create<Other>(NonFuncLocal));
  }
  /// Create a Select result indicating dependence on a select of two addresses.
  /// @return A Select MemDepResult.
  static MemDepResult getSelect() {
    return MemDepResult(ValueTy::create<Other>(Select));
  }
  /// Create an Unknown result for a dependency that cannot be computed.
  /// @return An Unknown MemDepResult.
  static MemDepResult getUnknown() {
    return MemDepResult(ValueTy::create<Other>(Unknown));
  }

  /// Tests if this MemDepResult represents a query that is an instruction
  /// clobber dependency.
  /// @return True if this is a clobber dependency.
  bool isClobber() const { return Value.is<Clobber>(); }

  /// Tests if this MemDepResult represents a query that is an instruction
  /// definition dependency.
  /// @return True if this is a definition dependency.
  bool isDef() const { return Value.is<Def>(); }

  /// Tests if this MemDepResult represents a valid local query (Clobber/Def).
  /// @return True if this is a valid local query (Clobber/Def).
  bool isLocal() const { return isClobber() || isDef(); }

  /// Tests if this MemDepResult represents a query that is transparent to the
  /// start of the block, but where a non-local hasn't been done.
  /// @return True if the query is transparent to the start of the block.
  bool isNonLocal() const {
    return Value.is<Other>() && Value.cast<Other>() == NonLocal;
  }

  /// Tests if this MemDepResult represents a query that is transparent to the
  /// start of the function.
  /// @return True if the query is transparent to the start of the function.
  bool isNonFuncLocal() const {
    return Value.is<Other>() && Value.cast<Other>() == NonFuncLocal;
  }

  /// Tests if this MemDepResult represents a query that depends on a select
  /// instruction whose two sides each reach a non-clobbered value.
  /// @return True if the query depends on a select instruction.
  bool isSelect() const {
    return Value.is<Other>() && Value.cast<Other>() == Select;
  }

  /// Tests if this MemDepResult represents a query which cannot and/or will
  /// not be computed.
  /// @return True if the query cannot or will not be computed.
  bool isUnknown() const {
    return Value.is<Other>() && Value.cast<Other>() == Unknown;
  }

  /// If this is a normal dependency, returns the instruction that is depended
  /// on.  Otherwise, returns null.
  /// @return The depended-on instruction, or null if this is not a normal
  /// dependency.
  Instruction *getInst() const {
    switch (Value.getTag()) {
    case Invalid:
      return Value.cast<Invalid>();
    case Clobber:
      return Value.cast<Clobber>();
    case Def:
      return Value.cast<Def>();
    case Other:
      return nullptr;
    }
    llvm_unreachable("Unknown discriminant!");
  }

  /// Return true if this result equals \p M.
  /// @param M Other MemDepResult to compare against.
  /// @return True if this result equals \p M.
  bool operator==(const MemDepResult &M) const { return Value == M.Value; }
  /// Return true if this result differs from \p M.
  /// @param M Other MemDepResult to compare against.
  /// @return True if this result differs from \p M.
  bool operator!=(const MemDepResult &M) const { return Value != M.Value; }
  /// Return true if this result is ordered before \p M.
  /// @param M Other MemDepResult to compare against.
  /// @return True if this result is ordered before \p M.
  bool operator<(const MemDepResult &M) const { return Value < M.Value; }
  /// Return true if this result is ordered after \p M.
  /// @param M Other MemDepResult to compare against.
  /// @return True if this result is ordered after \p M.
  bool operator>(const MemDepResult &M) const { return Value > M.Value; }

private:
  friend class MemoryDependenceResults;

  /// Tests if this is a MemDepResult in its dirty/invalid. state.
  bool isDirty() const { return Value.is<Invalid>(); }

  static MemDepResult getDirty(Instruction *Inst) {
    return MemDepResult(ValueTy::create<Invalid>(Inst));
  }
};

/// This is an entry in the NonLocalDepInfo cache.
///
/// For each BasicBlock (the BB entry) it keeps a MemDepResult.
class NonLocalDepEntry {
  BasicBlock *BB;
  MemDepResult Result;

public:
  /// Construct a cache entry for \p BB with dependence result \p Result.
  /// @param BB Basic block this entry describes.
  /// @param Result Dependence result for \p BB.
  NonLocalDepEntry(BasicBlock *BB, MemDepResult Result)
      : BB(BB), Result(Result) {}

  /// Construct a search key entry for \p BB with a default result.
  ///
  /// Used for binary searches into sorted NonLocalDepInfo vectors.
  /// @param BB Basic block used as the sort key.
  NonLocalDepEntry(BasicBlock *BB) : BB(BB) {}

  /// Return the basic block for this entry.
  ///
  /// BB is the sort key and cannot be changed.
  /// @return The basic block for this entry.
  BasicBlock *getBB() const { return BB; }

  /// Set the cached dependence result to \p R.
  /// @param R New dependence result for this block.
  void setResult(const MemDepResult &R) { Result = R; }

  /// Return the cached dependence result for this block.
  /// @return The cached dependence result for this block.
  const MemDepResult &getResult() const { return Result; }

  /// Return true if this entry's block is ordered before \p RHS.
  /// @param RHS Other entry to compare against by basic block address.
  /// @return True if this entry's block is ordered before \p RHS.
  bool operator<(const NonLocalDepEntry &RHS) const { return BB < RHS.BB; }
};

/// This is a result from a NonLocal dependence query.
///
/// For each BasicBlock (the BB entry) it keeps a MemDepResult and the
/// (potentially phi translated) address that was live in the block.
class NonLocalDepResult {
  NonLocalDepEntry Entry;
  SelectAddr Address;

public:
  /// Construct a non-local dependence result for \p BB.
  /// @param BB Basic block this result describes.
  /// @param Result Dependence result for \p BB.
  /// @param Address Potentially phi-translated address live in \p BB.
  NonLocalDepResult(BasicBlock *BB, MemDepResult Result,
                    const SelectAddr &Address)
      : Entry(BB, Result), Address(Address) {}

  /// Return the basic block for this result.
  ///
  /// BB is the sort key and cannot be changed.
  /// @return The basic block for this result.
  BasicBlock *getBB() const { return Entry.getBB(); }

  /// Set the dependence result and address for this block.
  /// @param R New dependence result.
  /// @param Addr Potentially phi-translated address for this block.
  void setResult(const MemDepResult &R, const SelectAddr &Addr) {
    Entry.setResult(R);
    Address = Addr;
  }

  /// Return the dependence result for this block.
  /// @return The dependence result for this block.
  const MemDepResult &getResult() const { return Entry.getResult(); }

  /// Returns the address of this pointer in this block.
  ///
  /// This can be different than the address queried for the non-local result
  /// because of phi translation.  This returns null if the address was not
  /// available in a block (i.e. because phi translation failed) or if this is
  /// a cached result and that address was deleted.
  ///
  /// The address is always null for a non-local 'call' dependence.
  ///
  /// If the result is a select dependency (\see MemDepResult::isSelect), the
  /// returned SelectAddr instead carries the select condition and the two
  /// translated addresses (true/false side).
  /// @return The (possibly phi-translated) address in this block, or a null
  /// SelectAddr if unavailable.
  SelectAddr getAddress() const { return Address; }
};

/// Provides a lazy, caching interface for making common memory aliasing
/// information queries, backed by LLVM's alias analysis passes.
///
/// The dependency information returned is somewhat unusual, but is pragmatic.
/// If queried about a store or call that might modify memory, the analysis
/// will return the instruction[s] that may either load from that memory or
/// store to it.  If queried with a load or call that can never modify memory,
/// the analysis will return calls and stores that might modify the pointer,
/// but generally does not return loads unless a) they are volatile, or
/// b) they load from *must-aliased* pointers.  Returning a dependence on
/// must-alias'd pointers instead of all pointers interacts well with the
/// internal caching mechanism.
class MemoryDependenceResults {
  // A map from instructions to their dependency.
  using LocalDepMapType = DenseMap<Instruction *, MemDepResult>;
  LocalDepMapType LocalDeps;

public:
  /// Vector of per-block non-local dependence cache entries.
  using NonLocalDepInfo = std::vector<NonLocalDepEntry>;

private:
  /// A pair<Value*, bool> where the bool is true if the dependence is a read
  /// only dependence, false if read/write.
  using ValueIsLoadPair = PointerIntPair<const Value *, 1, bool>;

  /// This pair is used when caching information for a block.
  ///
  /// If the pointer is null, the cache value is not a full query that starts
  /// at the specified block.  If non-null, the bool indicates whether or not
  /// the contents of the block was skipped.
  using BBSkipFirstBlockPair = PointerIntPair<BasicBlock *, 1, bool>;

  /// This record is the information kept for each (value, is load) pair.
  struct NonLocalPointerInfo {
    /// The pair of the block and the skip-first-block flag.
    BBSkipFirstBlockPair Pair;
    /// The results of the query for each relevant block.
    NonLocalDepInfo NonLocalDeps;
    /// The maximum size of the dereferences of the pointer.
    ///
    /// May be UnknownSize if the sizes are unknown.
    LocationSize Size = LocationSize::afterPointer();
    /// The AA tags associated with dereferences of the pointer.
    ///
    /// The members may be null if there are no tags or conflicting tags.
    AAMDNodes AATags;

    NonLocalPointerInfo() = default;
  };

  /// Cache storing single nonlocal def for the instruction.
  /// It is set when nonlocal def would be found in function returning only
  /// local dependencies.
  DenseMap<AssertingVH<const Value>, NonLocalDepResult> NonLocalDefsCache;
  using ReverseNonLocalDefsCacheTy =
    DenseMap<Instruction *, SmallPtrSet<const Value*, 4>>;
  ReverseNonLocalDefsCacheTy ReverseNonLocalDefsCache;

  /// This map stores the cached results of doing a pointer lookup at the
  /// bottom of a block.
  ///
  /// The key of this map is the pointer+isload bit, the value is a list of
  /// <bb->result> mappings.
  using CachedNonLocalPointerInfo =
      DenseMap<ValueIsLoadPair, NonLocalPointerInfo>;
  CachedNonLocalPointerInfo NonLocalPointerDeps;

  // A map from instructions to their non-local pointer dependencies.
  using ReverseNonLocalPtrDepTy =
      DenseMap<Instruction *, SmallPtrSet<ValueIsLoadPair, 4>>;
  ReverseNonLocalPtrDepTy ReverseNonLocalPtrDeps;

  /// This is the instruction we keep for each cached access that we have for
  /// an instruction.
  ///
  /// The pointer is an owning pointer and the bool indicates whether we have
  /// any dirty bits in the set.
  using PerInstNLInfo = std::pair<NonLocalDepInfo, bool>;

  // A map from instructions to their non-local dependencies.
  using NonLocalDepMapType = DenseMap<Instruction *, PerInstNLInfo>;

  NonLocalDepMapType NonLocalDepsMap;

  // A reverse mapping from dependencies to the dependees.  This is
  // used when removing instructions to keep the cache coherent.
  using ReverseDepMapType =
      DenseMap<Instruction *, SmallPtrSet<Instruction *, 4>>;
  ReverseDepMapType ReverseLocalDeps;

  // A reverse mapping from dependencies to the non-local dependees.
  ReverseDepMapType ReverseNonLocalDeps;

  /// Visited map for getNonLocalPointerDependency. Stored here to reuse the
  /// allocation. Map from block number to Value; second value is epoch to
  /// avoid clearing the vector for each query.
  SmallVector<std::pair<Value *, unsigned>, 0> NonLocalPointerDepVisited;
  unsigned NonLocalPointerDepEpoch = 0;

  /// Current AA implementation, just a cache.
  AAResults &AA;
  AssumptionCache &AC;
  const TargetLibraryInfo &TLI;
  DominatorTree &DT;
  PredIteratorCache PredCache;
  EarliestEscapeAnalysis EEA;

  unsigned DefaultBlockScanLimit;

  /// Offsets to dependant clobber loads.
  using ClobberOffsetsMapType = DenseMap<LoadInst *, int32_t>;
  ClobberOffsetsMapType ClobberOffsets;

public:
  /// Construct memory dependence results using the given analyses and scan limit.
  /// @param AA Alias analysis results used for dependence queries.
  /// @param AC Assumption cache for context-sensitive facts.
  /// @param TLI Target library info for recognizing library calls.
  /// @param DT Dominator tree used by escape analysis and queries.
  /// @param DefaultBlockScanLimit Default cap on instructions scanned per block.
  MemoryDependenceResults(AAResults &AA, AssumptionCache &AC,
                          const TargetLibraryInfo &TLI, DominatorTree &DT,
                          unsigned DefaultBlockScanLimit)
      : AA(AA), AC(AC), TLI(TLI), DT(DT), EEA(DT),
        DefaultBlockScanLimit(DefaultBlockScanLimit) {}

  /// Handle invalidation in the new PM.
  /// @param F Function whose analysis result may be invalidated.
  /// @param PA Set of analyses preserved by the transform.
  /// @param Inv Invalidator for resolving analysis dependencies.
  /// @return True if this analysis result should be invalidated.
  LLVM_ABI bool invalidate(Function &F, const PreservedAnalyses &PA,
                           FunctionAnalysisManager::Invalidator &Inv);

  /// Return the default per-block instruction scan limit.
  ///
  /// Some methods limit the number of instructions they will examine. The
  /// return value of this method is the default limit that will be used if no
  /// limit is explicitly passed in.
  /// @return The default per-block instruction scan limit.
  LLVM_ABI unsigned getDefaultBlockScanLimit() const;

  /// Returns the instruction on which a memory operation depends.
  ///
  /// See the class comment for more details. It is illegal to call this on
  /// non-memory instructions.
  /// @param QueryInst Memory instruction whose dependence is queried.
  /// @return The local MemDepResult for \p QueryInst.
  LLVM_ABI MemDepResult getDependency(Instruction *QueryInst);

  /// Perform a full dependency query for the specified call, returning the set
  /// of blocks that the value is potentially live across.
  ///
  /// The returned set of results will include a "NonLocal" result for all
  /// blocks where the value is live across.
  ///
  /// This method assumes the instruction returns a "NonLocal" dependency
  /// within its own block.
  ///
  /// This returns a reference to an internal data structure that may be
  /// invalidated on the next non-local query or when an instruction is
  /// removed.  Clients must copy this data if they want it around longer than
  /// that.
  /// @param QueryCall Call whose non-local dependence is queried.
  /// @return A reference to the cached non-local dependence info for
  /// \p QueryCall.
  LLVM_ABI const NonLocalDepInfo &
  getNonLocalCallDependency(CallBase *QueryCall);

  /// Return the set of instructions that define or clobber \p QueryInst's memory.
  ///
  /// Perform a full dependency query for an access to the QueryInst's
  /// specified memory location, returning the set of instructions that either
  /// define or clobber the value.
  ///
  /// Warning: For a volatile query instruction, the dependencies will be
  /// accurate, and thus usable for reordering, but it is never legal to
  /// remove the query instruction.
  ///
  /// This method assumes the pointer has a "NonLocal" dependency within
  /// QueryInst's parent basic block.
  /// @param QueryInst Instruction whose memory location is queried.
  /// @param Result Filled with per-block non-local dependence results.
  LLVM_ABI void
  getNonLocalPointerDependency(Instruction *QueryInst,
                               SmallVectorImpl<NonLocalDepResult> &Result);

  /// Removes an instruction from the dependence analysis, updating the
  /// dependence of instructions that previously depended on it.
  /// @param InstToRemove Instruction to remove from the dependence cache.
  LLVM_ABI void removeInstruction(Instruction *InstToRemove);

  /// Invalidates cached information about the specified pointer, because it
  /// may be too conservative in memdep.
  ///
  /// This is an optional call that can be used when the client detects an
  /// equivalence between the pointer and some other value and replaces the
  /// other value with ptr. This can make Ptr available in more places that
  /// cached info does not necessarily keep.
  /// @param Ptr Pointer whose cached dependence info should be discarded.
  LLVM_ABI void invalidateCachedPointerInfo(Value *Ptr);

  /// Clears the PredIteratorCache info.
  ///
  /// This needs to be done when the CFG changes, e.g., due to splitting
  /// critical edges.
  LLVM_ABI void invalidateCachedPredecessors();

  /// Returns the instruction on which a memory location depends.
  ///
  /// If isLoad is true, this routine ignores may-aliases with read-only
  /// operations.  If isLoad is false, this routine ignores may-aliases
  /// with reads from read-only locations. If possible, pass the query
  /// instruction as well; this function may take advantage of the metadata
  /// annotated to the query instruction to refine the result. \p Limit
  /// can be used to set the maximum number of instructions that will be
  /// examined to find the pointer dependency. On return, it will be set to
  /// the number of instructions left to examine. If a null pointer is passed
  /// in, the limit will default to the value of -memdep-block-scan-limit.
  ///
  /// Note that this is an uncached query, and thus may be inefficient.
  /// @param Loc Memory location whose dependence is queried.
  /// @param isLoad True if the query is a load-like access.
  /// @param ScanIt Iterator to start scanning backwards from in \p BB.
  /// @param BB Basic block in which to scan for a dependence.
  /// @param QueryInst Optional instruction providing query metadata.
  /// @param Limit Optional remaining instruction scan budget; updated on return.
  /// @return The MemDepResult for the dependence of \p Loc.
  LLVM_ABI MemDepResult getPointerDependencyFrom(
      const MemoryLocation &Loc, bool isLoad, BasicBlock::iterator ScanIt,
      BasicBlock *BB, Instruction *QueryInst = nullptr,
      unsigned *Limit = nullptr);

  /// Returns the instruction on which a memory location depends, using \p BatchAA.
  /// @param Loc Memory location whose dependence is queried.
  /// @param isLoad True if the query is a load-like access.
  /// @param ScanIt Iterator to start scanning backwards from in \p BB.
  /// @param BB Basic block in which to scan for a dependence.
  /// @param QueryInst Optional instruction providing query metadata.
  /// @param Limit Remaining instruction scan budget; updated on return.
  /// @param BatchAA Batched alias analysis used for this query.
  /// @return The MemDepResult for the dependence of \p Loc.
  LLVM_ABI MemDepResult getPointerDependencyFrom(
      const MemoryLocation &Loc, bool isLoad, BasicBlock::iterator ScanIt,
      BasicBlock *BB, Instruction *QueryInst, unsigned *Limit,
      BatchAAResults &BatchAA);

  /// Scan for a simple pointer dependence without invariant-group handling.
  /// @param MemLoc Memory location whose dependence is queried.
  /// @param isLoad True if the query is a load-like access.
  /// @param ScanIt Iterator to start scanning backwards from in \p BB.
  /// @param BB Basic block in which to scan for a dependence.
  /// @param QueryInst Optional instruction providing query metadata.
  /// @param Limit Remaining instruction scan budget; updated on return.
  /// @param BatchAA Batched alias analysis used for this query.
  /// @return The MemDepResult for the simple pointer dependence.
  LLVM_ABI MemDepResult getSimplePointerDependencyFrom(
      const MemoryLocation &MemLoc, bool isLoad, BasicBlock::iterator ScanIt,
      BasicBlock *BB, Instruction *QueryInst, unsigned *Limit,
      BatchAAResults &BatchAA);

  /// Find a dependence among invariant.group loads/stores of the same pointer.
  ///
  /// This analysis looks for other loads and stores with invariant.group
  /// metadata and the same pointer operand. Returns Unknown if it does not
  /// find anything, and Def if it can be assumed that 2 instructions load or
  /// store the same value and NonLocal which indicate that non-local Def was
  /// found, which can be retrieved by calling getNonLocalPointerDependency
  /// with the same queried instruction.
  /// @param LI Load with invariant.group metadata to query.
  /// @param BB Basic block in which to look for an invariant-group dependence.
  /// @return Def, NonLocal, or Unknown depending on whether a matching
  /// invariant.group access is found.
  LLVM_ABI MemDepResult getInvariantGroupPointerDependency(LoadInst *LI,
                                                           BasicBlock *BB);

  /// Return the clobber offset to dependent instruction.
  /// @param DepInst Dependent load whose cached clobber offset is requested.
  /// @return The cached clobber offset for \p DepInst, or std::nullopt if none.
  std::optional<int32_t> getClobberOffset(LoadInst *DepInst) const {
    const auto Off = ClobberOffsets.find(DepInst);
    if (Off != ClobberOffsets.end())
      return Off->getSecond();
    return std::nullopt;
  }

private:
  MemDepResult getCallDependencyFrom(CallBase *Call, bool isReadOnlyCall,
                                     BasicBlock::iterator ScanIt,
                                     BasicBlock *BB);
  void setNonLocalPointerDepVisited(BasicBlock *BB, Value *V);
  bool isNonLocalPointerDepVisited(BasicBlock *BB) const;
  Value *lookupNonLocalPointerDepVisited(BasicBlock *BB) const;
  bool getNonLocalPointerDepFromBB(Instruction *QueryInst,
                                   const PHITransAddr &Pointer,
                                   const MemoryLocation &Loc, bool isLoad,
                                   BasicBlock *BB,
                                   SmallVectorImpl<NonLocalDepResult> &Result,
                                   bool SkipFirstBlock = false,
                                   bool IsIncomplete = false);
  MemDepResult getNonLocalInfoForBlock(Instruction *QueryInst,
                                       const MemoryLocation &Loc, bool isLoad,
                                       BasicBlock *BB, NonLocalDepInfo *Cache,
                                       unsigned NumSortedEntries,
                                       BatchAAResults &BatchAA);

  void removeCachedNonLocalPointerDependencies(ValueIsLoadPair P);

  void verifyRemoved(Instruction *Inst) const;
};

/// An analysis that produces \c MemoryDependenceResults for a function.
///
/// This is essentially a no-op because the results are computed entirely
/// lazily.
class MemoryDependenceAnalysis
    : public AnalysisInfoMixin<MemoryDependenceAnalysis> {
  friend AnalysisInfoMixin<MemoryDependenceAnalysis>;

  static AnalysisKey Key;

  unsigned DefaultBlockScanLimit;

public:
  /// Analysis result type produced for each function.
  using Result = MemoryDependenceResults;

  /// Construct the analysis with the default block scan limit.
  LLVM_ABI MemoryDependenceAnalysis();
  /// Construct the analysis with a custom default block scan limit.
  /// @param DefaultBlockScanLimit Default cap on instructions scanned per block.
  MemoryDependenceAnalysis(unsigned DefaultBlockScanLimit) : DefaultBlockScanLimit(DefaultBlockScanLimit) { }

  /// Run the analysis over \p F and produce MemoryDependenceResults.
  /// @param F Function to analyze.
  /// @param AM Function analysis manager providing dependencies.
  /// @return MemoryDependenceResults for \p F.
  LLVM_ABI MemoryDependenceResults run(Function &F,
                                       FunctionAnalysisManager &AM);
};

/// A wrapper analysis pass for the legacy pass manager that exposes a \c
/// MemoryDepnedenceResults instance.
class LLVM_ABI MemoryDependenceWrapperPass : public FunctionPass {
  std::optional<MemoryDependenceResults> MemDep;

public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct the legacy MemoryDependenceResults wrapper pass.
  MemoryDependenceWrapperPass();
  /// Destroy the legacy MemoryDependenceResults wrapper pass.
  ~MemoryDependenceWrapperPass() override;

  /// Pass Implementation stuff.  This doesn't do any analysis eagerly.
  /// @param F Function to prepare MemoryDependenceResults for.
  /// @return False; this analysis does not modify the function.
  bool runOnFunction(Function &F) override;

  /// Clean up memory in between runs
  void releaseMemory() override;

  /// Does not modify anything.  It uses Value Numbering and Alias Analysis.
  /// @param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// Return the MemoryDependenceResults computed by this pass.
  /// @return The MemoryDependenceResults computed by this pass.
  MemoryDependenceResults &getMemDep() { return *MemDep; }
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_MEMORYDEPENDENCEANALYSIS_H
