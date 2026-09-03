//===- llvm/Analysis/AliasAnalysis.h - Alias Analysis Interface -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the generic AliasAnalysis interface, which is used as the
// common interface used by all clients of alias analysis information, and
// implemented by all alias analysis implementations.  Mod/Ref information is
// also captured by this interface.
//
// Implementations of this interface must implement the various virtual methods,
// which automatically provides functionality for the entire suite of client
// APIs.
//
// This API identifies memory regions with the MemoryLocation class. The pointer
// component specifies the base memory address of the region. The Size specifies
// the maximum size (in address units) of the memory region, or
// MemoryLocation::UnknownSize if the size is not known. The TBAA tag
// identifies the "type" of the memory reference; see the
// TypeBasedAliasAnalysis class for details.
//
// Some non-obvious details include:
//  - Pointers that point to two completely different objects in memory never
//    alias, regardless of the value of the Size component.
//  - NoAlias doesn't imply inequal pointers. The most obvious example of this
//    is two pointers to constant memory. Even if they are equal, constant
//    memory is never stored to, so there will never be any dependencies.
//    In this and other situations, the pointers may be both NoAlias and
//    MustAlias at the same time. The current API can only return one result,
//    though this is rarely a problem in practice.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_ALIASANALYSIS_H
#define LLVM_ANALYSIS_ALIASANALYSIS_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/CaptureTracking.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ModRef.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace llvm {

class AtomicCmpXchgInst;
class BasicBlock;
class CatchPadInst;
class CatchReturnInst;
class CycleInfo;
class DominatorTree;
class FenceInst;
class LoopInfo;
class TargetLibraryInfo;

/// The possible results of an alias query.
///
/// These results are always computed between two MemoryLocation objects as
/// a query to some alias analysis.
///
/// Note that these are unscoped enumerations because we would like to support
/// implicitly testing a result for the existence of any possible aliasing with
/// a conversion to bool, but an "enum class" doesn't support this. The
/// canonical names from the literature are suffixed and unique anyways, and so
/// they serve as global constants in LLVM for these results.
///
/// See docs/AliasAnalysis.html for more information on the specific meanings
/// of these values.
class AliasResult {
private:
  static const int OffsetBits = 23;
  static const int AliasBits = 8;
  static_assert(AliasBits + 1 + OffsetBits <= 32,
                "AliasResult size is intended to be 4 bytes!");

  unsigned int Alias : AliasBits;
  unsigned int HasOffset : 1;
  signed int Offset : OffsetBits;

public:
  /// Classification of whether two memory locations alias.
  enum Kind : uint8_t {
    /// The two locations do not alias at all.
    ///
    /// This value is arranged to convert to false, while all other values
    /// convert to true. This allows a boolean context to convert the result to
    /// a binary flag indicating whether there is the possibility of aliasing.
    NoAlias = 0,
    /// The two locations may or may not alias. This is the least precise
    /// result.
    MayAlias,
    /// The two locations alias, but only due to a partial overlap.
    PartialAlias,
    /// The two locations precisely alias each other.
    MustAlias,
  };
  static_assert(MustAlias < (1 << AliasBits),
                "Not enough bit field size for the enum!");

  /// Deleted default constructor; an AliasResult requires a Kind.
  explicit AliasResult() = delete;
  /// Construct an AliasResult with the given kind and no offset.
  /// @param Alias Alias kind for this result.
  constexpr AliasResult(const Kind &Alias)
      : Alias(Alias), HasOffset(false), Offset(0) {}

  /// Convert this result to its Kind.
  /// @return The Kind classification of this result.
  operator Kind() const { return static_cast<Kind>(Alias); }

  /// Return true if this result equals \p Other.
  /// @param Other AliasResult to compare against.
  /// @return True if this result equals \p Other.
  bool operator==(const AliasResult &Other) const {
    return Alias == Other.Alias && HasOffset == Other.HasOffset &&
           Offset == Other.Offset;
  }
  /// Return true if this result differs from \p Other.
  /// @param Other AliasResult to compare against.
  /// @return True if this result differs from \p Other.
  bool operator!=(const AliasResult &Other) const { return !(*this == Other); }

  /// Return true if this result's kind equals \p K.
  /// @param K Alias kind to compare against.
  /// @return True if this result's kind equals \p K.
  bool operator==(Kind K) const { return Alias == K; }
  /// Return true if this result's kind differs from \p K.
  /// @param K Alias kind to compare against.
  /// @return True if this result's kind differs from \p K.
  bool operator!=(Kind K) const { return !(*this == K); }

  /// Return true if this result has an associated offset.
  /// @return True if this result has an associated offset.
  constexpr bool hasOffset() const { return HasOffset; }
  /// Return the offset between the aliased locations.
  /// @return The offset between the aliased locations.
  constexpr int32_t getOffset() const {
    assert(HasOffset && "No offset!");
    return Offset;
  }
  /// Set the offset between the aliased locations.
  /// @param NewOffset Offset to store when it fits in OffsetBits.
  void setOffset(int32_t NewOffset) {
    if (isInt<OffsetBits>(NewOffset)) {
      HasOffset = true;
      Offset = NewOffset;
    }
  }

  /// Helper for processing AliasResult for swapped memory location pairs.
  /// @param DoSwap When true, negate any stored offset.
  void swap(bool DoSwap = true) {
    if (DoSwap && hasOffset())
      setOffset(-getOffset());
  }
};

static_assert(sizeof(AliasResult) == 4,
              "AliasResult size is intended to be 4 bytes!");

/// Write \p AR to \p OS in a human-readable form.
/// @param OS Stream to write to.
/// @param AR AliasResult to print.
/// @return The stream \p OS after writing.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, AliasResult AR);

/// Virtual base class for providers of capture analysis.
struct LLVM_ABI CaptureAnalysis {
  /// Destroy this CaptureAnalysis.
  virtual ~CaptureAnalysis() = 0;

  /// Return how Object may be captured before instruction I.
  ///
  /// Considers only provenance captures. If OrAt is true, captures by
  /// instruction I itself are also considered.
  ///
  /// If I is nullptr, then captures at any point will be considered.
  /// @param Object Pointer value whose captures are queried.
  /// @param I Instruction before which captures are considered, or null.
  /// @param OrAt When true, also consider captures by \p I itself.
  /// @param ReturnCaptures Whether return captures should be included.
  /// @return Capture components describing how \p Object may be captured.
  virtual CaptureComponents getCapturesBefore(const Value *Object,
                                              const Instruction *I, bool OrAt,
                                              bool ReturnCaptures) = 0;
};

/// Context-free CaptureAnalysis provider.
///
/// Computes and caches whether an object is captured in the function at all,
/// but does not distinguish whether it was captured before or after the
/// context instruction.
class LLVM_ABI SimpleCaptureAnalysis final : public CaptureAnalysis {
  SmallDenseMap<const Value *, CaptureResult, 8> IsCapturedCache;

public:
  /// Return how Object may be captured before instruction I.
  /// @param Object Pointer value whose captures are queried.
  /// @param I Instruction before which captures are considered, or null.
  /// @param OrAt When true, also consider captures by \p I itself.
  /// @param ReturnCaptures Whether return captures should be included.
  /// @return Capture components describing how \p Object may be captured.
  CaptureComponents getCapturesBefore(const Value *Object, const Instruction *I,
                                      bool OrAt, bool ReturnCaptures) override;
};

/// Context-sensitive CaptureAnalysis using earliest common dominators.
///
/// Computes and caches the earliest common dominator closure of all captures.
/// It provides a good approximation to a precise "captures before" analysis.
class LLVM_ABI EarliestEscapeAnalysis final : public CaptureAnalysis {
  DominatorTree &DT;
  const LoopInfo *LI;
  const CycleInfo *CI;

  /// Map from identified local object to an instruction before which it does
  /// not escape (or nullptr if it never escapes) and the possible components
  /// that may be captured (by any instruction, not necessarily the earliest
  /// one). The "earliest" instruction may be a conservative approximation,
  /// e.g. the first instruction in the function is always a legal choice.
  DenseMap<const Value *, std::pair<Instruction *, CaptureResult>>
      EarliestEscapes;

  /// Reverse map from instruction to the objects it is the earliest escape for.
  /// This is used for cache invalidation purposes.
  DenseMap<Instruction *, TinyPtrVector<const Value *>> Inst2Obj;

public:
  /// Construct analysis over \p DT, optionally using loop and cycle info.
  /// @param DT Dominator tree used to find earliest escape points.
  /// @param LI Optional loop info to refine escape analysis.
  /// @param CI Optional cycle info to refine escape analysis.
  EarliestEscapeAnalysis(DominatorTree &DT, const LoopInfo *LI = nullptr,
                         const CycleInfo *CI = nullptr)
      : DT(DT), LI(LI), CI(CI) {}

  /// Return how Object may be captured before instruction I.
  /// @param Object Pointer value whose captures are queried.
  /// @param I Instruction before which captures are considered, or null.
  /// @param OrAt When true, also consider captures by \p I itself.
  /// @param ReturnCaptures Whether return captures should be included.
  /// @return Capture components describing how \p Object may be captured.
  CaptureComponents getCapturesBefore(const Value *Object, const Instruction *I,
                                      bool OrAt, bool ReturnCaptures) override;

  /// Invalidate cached escape info that depended on instruction \p I.
  /// @param I Instruction being removed from the function.
  void removeInstruction(Instruction *I);
};

/// Cache key for BasicAA results.
///
/// It only includes the pointer and size from MemoryLocation, as BasicAA is
/// AATags independent. Additionally, it includes the value of
/// MayBeCrossIteration, which may affect BasicAA results.
struct AACacheLoc {
  /// Pointer paired with a MayBeCrossIteration flag.
  using PtrTy = PointerIntPair<const Value *, 1, bool>;
  /// Cached pointer and cross-iteration bit.
  PtrTy Ptr;
  /// Cached access size.
  LocationSize Size;

  /// Construct a cache key from an already-packed pointer and size.
  /// @param Ptr Packed pointer and MayBeCrossIteration flag.
  /// @param Size Access size for this location.
  AACacheLoc(PtrTy Ptr, LocationSize Size) : Ptr(Ptr), Size(Size) {}
  /// Construct a cache key from a pointer, size, and cross-iteration flag.
  /// @param Ptr Base pointer of the memory location.
  /// @param Size Access size for this location.
  /// @param MayBeCrossIteration Whether accesses may span cycle iterations.
  AACacheLoc(const Value *Ptr, LocationSize Size, bool MayBeCrossIteration)
      : Ptr(Ptr, MayBeCrossIteration), Size(Size) {}
};

/// DenseMapInfo specialization for AACacheLoc keys.
template <> struct DenseMapInfo<AACacheLoc> {
  /// Return a hash of \p Val suitable for DenseMap.
  /// @param Val Cache location to hash.
  /// @return Hash of \p Val suitable for DenseMap.
  static unsigned getHashValue(const AACacheLoc &Val) {
    return DenseMapInfo<AACacheLoc::PtrTy>::getHashValue(Val.Ptr) ^
           DenseMapInfo<LocationSize>::getHashValue(Val.Size);
  }
  /// Return true if \p LHS and \p RHS are equal keys.
  /// @param LHS Left-hand cache location.
  /// @param RHS Right-hand cache location.
  /// @return True if \p LHS and \p RHS are equal keys.
  static bool isEqual(const AACacheLoc &LHS, const AACacheLoc &RHS) {
    return LHS.Ptr == RHS.Ptr && LHS.Size == RHS.Size;
  }
};

class AAResults;

/// State retained across or within an alias query.
///
/// This class stores info we want to provide to or retain within an alias
/// query. By default, the root query is stateless and starts with a freshly
/// constructed info object. Specific alias analyses can use this query info to
/// store per-query state that is important for recursive or nested queries to
/// avoid recomputing. To enable preserving this state across multiple queries
/// where safe (due to the IR not changing), use a `BatchAAResults` wrapper.
/// The information stored in an `AAQueryInfo` is currently limitted to the
/// caches used by BasicAA, but can further be extended to fit other AA needs.
class AAQueryInfo {
public:
  /// Pair of cache locations used as an alias-query cache key.
  using LocPair = std::pair<AACacheLoc, AACacheLoc>;
  /// Cached alias result and metadata about assumption use.
  struct CacheEntry {
    /// Cache entry is neither an assumption nor does it use a (non-definitive)
    /// assumption.
    static constexpr int Definitive = -2;
    /// Cache entry is not an assumption itself, but may be using an assumption
    /// from higher up the stack.
    static constexpr int AssumptionBased = -1;

    /// Cached alias result for this location pair.
    AliasResult Result;
    /// Times a NoAlias assumption was used, or a sentinel value.
    ///
    /// Zero means the assumption has not been used. Can also take one of the
    /// Definitive or AssumptionBased values documented above.
    int NumAssumptionUses;

    /// Whether this is a definitive (non-assumption) result.
    /// @return True if this is a definitive (non-assumption) result.
    bool isDefinitive() const { return NumAssumptionUses == Definitive; }
    /// Whether this is an assumption that has not been proven yet.
    /// @return True if this is an assumption that has not been proven yet.
    bool isAssumption() const { return NumAssumptionUses >= 0; }
  };

  /// Alias analysis result aggregation used to perform this query.
  ///
  /// Can be used to perform recursive queries.
  AAResults &AAR;

  /// Dense map type caching alias results for location pairs.
  using AliasCacheT = SmallDenseMap<LocPair, CacheEntry, 8>;
  /// Cache of alias results keyed by location pairs.
  AliasCacheT AliasCache;

  /// Capture analysis provider used by this query.
  CaptureAnalysis *CA;

  /// Query depth used to distinguish recursive queries.
  unsigned Depth = 0;

  /// How many active NoAlias assumption uses there are.
  int NumAssumptionUses = 0;

  /// Location pairs with an assumption-based result currently stored.
  ///
  /// Used to remove all potentially incorrect results from the cache if an
  /// assumption is disproven.
  SmallVector<AAQueryInfo::LocPair, 4> AssumptionBasedResults;

  /// Tracks whether the accesses may be on different cycle iterations.
  ///
  /// When interpret "Value" pointer equality as value equality we need to make
  /// sure that the "Value" is not part of a cycle. Otherwise, two uses could
  /// come from different "iterations" of a cycle and see different values for
  /// the same "Value" pointer.
  ///
  /// The following example shows the problem:
  ///   %p = phi(%alloca1, %addr2)
  ///   %l = load %ptr
  ///   %addr1 = gep, %alloca2, 0, %l
  ///   %addr2 = gep  %alloca2, 0, (%l + 1)
  ///      alias(%p, %addr1) -> MayAlias !
  ///   store %l, ...
  bool MayBeCrossIteration = false;

  /// Whether alias analysis is allowed to use the dominator tree, for use by
  /// passes that lazily update the DT while performing AA queries.
  bool UseDominatorTree = true;

  /// Construct query info over \p AAR using capture analysis \p CA.
  /// @param AAR Aggregated AA results for recursive queries.
  /// @param CA Capture analysis provider for this query.
  AAQueryInfo(AAResults &AAR, CaptureAnalysis *CA) : AAR(AAR), CA(CA) {}
};

/// AAQueryInfo that uses SimpleCaptureAnalysis.
class SimpleAAQueryInfo : public AAQueryInfo {
  SimpleCaptureAnalysis CA;

public:
  /// Construct query info over \p AAR using a SimpleCaptureAnalysis.
  /// @param AAR Aggregated AA results for recursive queries.
  SimpleAAQueryInfo(AAResults &AAR) : AAQueryInfo(AAR, &CA) {}
};

class BatchAAResults;

/// Aggregated alias analysis results for a function.
class AAResults {
public:
  // Make these results default constructable and movable. We have to spell
  // these out because MSVC won't synthesize them.
  /// Construct aggregated AA results using target library info \p TLI.
  /// @param TLI Target library info used by component analyses.
  LLVM_ABI AAResults(const TargetLibraryInfo &TLI);
  /// Move-construct aggregated AA results from \p Arg.
  /// @param Arg AAResults to move from.
  LLVM_ABI AAResults(AAResults &&Arg);
  /// Destroy this AAResults aggregation.
  LLVM_ABI ~AAResults();

  /// Register a specific AA result.
  /// @param AAResult Component analysis result to aggregate.
  template <typename AAResultT> void addAAResult(AAResultT &AAResult) {
    // FIXME: We should use a much lighter weight system than the usual
    // polymorphic pattern because we don't own AAResult. It should
    // ideally involve two pointers and no separate allocation.
    AAs.emplace_back(new Model<AAResultT>(AAResult, *this));
  }

  /// Register a function analysis ID that the results aggregation depends on.
  ///
  /// This is used in the new pass manager to implement the invalidation logic
  /// where we must invalidate the results aggregation if any of our component
  /// analyses become invalid.
  /// @param ID Analysis key identifying a dependency of this aggregation.
  void addAADependencyID(AnalysisKey *ID) { AADeps.push_back(ID); }

  /// Handle invalidation events in the new pass manager.
  ///
  /// The aggregation is invalidated if any of the underlying analyses is
  /// invalidated.
  /// @param F Function whose analyses may have been invalidated.
  /// @param PA Set of analyses preserved by the invalidating transform.
  /// @param Inv Invalidator used to check dependent analyses.
  /// @return True if this aggregation should be invalidated.
  LLVM_ABI bool invalidate(Function &F, const PreservedAnalyses &PA,
                           FunctionAnalysisManager::Invalidator &Inv);

  //===--------------------------------------------------------------------===//
  /// \name Alias Queries
  /// @{

  /// Query whether two memory locations may alias.
  ///
  /// Returns an AliasResult indicating whether the two pointers are aliased to
  /// each other. This is the interface that must be implemented by specific
  /// alias analysis implementations.
  /// @param LocA First memory location.
  /// @param LocB Second memory location.
  /// @return An AliasResult indicating whether the locations alias.
  LLVM_ABI AliasResult alias(const MemoryLocation &LocA,
                             const MemoryLocation &LocB);

  /// A convenience wrapper around the primary \c alias interface.
  /// @param V1 First pointer value.
  /// @param V1Size Access size for \p V1.
  /// @param V2 Second pointer value.
  /// @param V2Size Access size for \p V2.
  /// @return An AliasResult indicating whether the pointers alias.
  AliasResult alias(const Value *V1, LocationSize V1Size, const Value *V2,
                    LocationSize V2Size) {
    return alias(MemoryLocation(V1, V1Size), MemoryLocation(V2, V2Size));
  }

  /// A convenience wrapper around the primary \c alias interface.
  /// @param V1 First pointer value.
  /// @param V2 Second pointer value.
  /// @return An AliasResult indicating whether the pointers alias.
  AliasResult alias(const Value *V1, const Value *V2) {
    return alias(MemoryLocation::getBeforeOrAfter(V1),
                 MemoryLocation::getBeforeOrAfter(V2));
  }

  /// A trivial helper function to check to see if the specified pointers are
  /// no-alias.
  /// @param LocA First memory location.
  /// @param LocB Second memory location.
  /// @return True if the locations are known not to alias.
  bool isNoAlias(const MemoryLocation &LocA, const MemoryLocation &LocB) {
    return alias(LocA, LocB) == AliasResult::NoAlias;
  }

  /// A convenience wrapper around the \c isNoAlias helper interface.
  /// @param V1 First pointer value.
  /// @param V1Size Access size for \p V1.
  /// @param V2 Second pointer value.
  /// @param V2Size Access size for \p V2.
  /// @return True if the pointers are known not to alias.
  bool isNoAlias(const Value *V1, LocationSize V1Size, const Value *V2,
                 LocationSize V2Size) {
    return isNoAlias(MemoryLocation(V1, V1Size), MemoryLocation(V2, V2Size));
  }

  /// A convenience wrapper around the \c isNoAlias helper interface.
  /// @param V1 First pointer value.
  /// @param V2 Second pointer value.
  /// @return True if the pointers are known not to alias.
  bool isNoAlias(const Value *V1, const Value *V2) {
    return isNoAlias(MemoryLocation::getBeforeOrAfter(V1),
                     MemoryLocation::getBeforeOrAfter(V2));
  }

  /// A trivial helper function to check to see if the specified pointers are
  /// must-alias.
  /// @param LocA First memory location.
  /// @param LocB Second memory location.
  /// @return True if the locations are known to must-alias.
  bool isMustAlias(const MemoryLocation &LocA, const MemoryLocation &LocB) {
    return alias(LocA, LocB) == AliasResult::MustAlias;
  }

  /// A convenience wrapper around the \c isMustAlias helper interface.
  /// @param V1 First pointer value.
  /// @param V2 Second pointer value.
  /// @return True if the pointers are known to must-alias.
  bool isMustAlias(const Value *V1, const Value *V2) {
    return alias(V1, LocationSize::precise(1), V2, LocationSize::precise(1)) ==
           AliasResult::MustAlias;
  }

  /// Checks whether the given location points to constant memory, or if
  /// \p OrLocal is true whether it points to a local alloca.
  /// @param Loc Memory location to test.
  /// @param OrLocal When true, also treat local allocas as constant.
  /// @return True if the location points to constant (or local) memory.
  bool pointsToConstantMemory(const MemoryLocation &Loc, bool OrLocal = false) {
    return isNoModRef(getModRefInfoMask(Loc, OrLocal));
  }

  /// A convenience wrapper around the primary \c pointsToConstantMemory
  /// interface.
  /// @param P Pointer value to test.
  /// @param OrLocal When true, also treat local allocas as constant.
  /// @return True if the pointer points to constant (or local) memory.
  bool pointsToConstantMemory(const Value *P, bool OrLocal = false) {
    return pointsToConstantMemory(MemoryLocation::getBeforeOrAfter(P), OrLocal);
  }

  /// @}
  //===--------------------------------------------------------------------===//
  /// \name Simple mod/ref information
  /// @{

  /// Return a ModRef bitmask for a memory location.
  ///
  /// Returns a bitmask that should be unconditionally applied to the ModRef
  /// info of a memory location. This allows us to eliminate Mod and/or Ref
  /// from the ModRef info based on the knowledge that the memory location
  /// points to constant and/or locally-invariant memory.
  ///
  /// If IgnoreLocals is true, then this method returns NoModRef for memory
  /// that points to a local alloca.
  /// @param Loc Memory location whose ModRef mask is requested.
  /// @param IgnoreLocals When true, treat local allocas as NoModRef.
  /// @return A ModRef bitmask that can be applied to ModRef info for \p Loc.
  LLVM_ABI ModRefInfo getModRefInfoMask(const MemoryLocation &Loc,
                                        bool IgnoreLocals = false);

  /// A convenience wrapper around the primary \c getModRefInfoMask
  /// interface.
  /// @param P Pointer value whose ModRef mask is requested.
  /// @param IgnoreLocals When true, treat local allocas as NoModRef.
  /// @return A ModRef bitmask that can be applied to ModRef info for \p P.
  ModRefInfo getModRefInfoMask(const Value *P, bool IgnoreLocals = false) {
    return getModRefInfoMask(MemoryLocation::getBeforeOrAfter(P), IgnoreLocals);
  }

  /// Get the ModRef info for a pointer argument of a call.
  ///
  /// The result's bits are set to indicate the allowed aliasing ModRef kinds.
  /// Note that these bits do not necessarily account for the overall behavior
  /// of the function, but rather only provide additional per-argument
  /// information.
  /// @param Call Call whose argument ModRef info is queried.
  /// @param ArgIdx Zero-based index of the pointer argument.
  /// @return ModRef info describing how the argument may be accessed.
  LLVM_ABI ModRefInfo getArgModRefInfo(const CallBase *Call, unsigned ArgIdx);

  /// Return the behavior of the given call site.
  /// @param Call Call site whose memory effects are queried.
  /// @return Memory effects of the call site.
  LLVM_ABI MemoryEffects getMemoryEffects(const CallBase *Call);

  /// Return the behavior when calling the given function.
  /// @param F Function whose memory effects are queried.
  /// @return Memory effects when calling the function.
  LLVM_ABI MemoryEffects getMemoryEffects(const Function *F);

  /// Checks if the specified call is known to never read or write memory.
  ///
  /// Note that if the call only reads from known-constant memory, it is also
  /// legal to return true. Also, calls that unwind the stack are legal for
  /// this predicate.
  ///
  /// Many optimizations (such as CSE and LICM) can be performed on such calls
  /// without worrying about aliasing properties, and many calls have this
  /// property (e.g. calls to 'sin' and 'cos').
  ///
  /// This property corresponds to the GCC 'const' attribute.
  /// @param Call Call site to test.
  /// @return True if the call is known never to read or write memory.
  bool doesNotAccessMemory(const CallBase *Call) {
    return getMemoryEffects(Call).doesNotAccessMemory();
  }

  /// Checks if the specified function is known to never read or write memory.
  ///
  /// Note that if the function only reads from known-constant memory, it is
  /// also legal to return true. Also, function that unwind the stack are legal
  /// for this predicate.
  ///
  /// Many optimizations (such as CSE and LICM) can be performed on such calls
  /// to such functions without worrying about aliasing properties, and many
  /// functions have this property (e.g. 'sin' and 'cos').
  ///
  /// This property corresponds to the GCC 'const' attribute.
  /// @param F Function to test.
  /// @return True if the function is known never to read or write memory.
  bool doesNotAccessMemory(const Function *F) {
    return getMemoryEffects(F).doesNotAccessMemory();
  }

  /// Checks if the specified call is known to only read from non-volatile
  /// memory (or not access memory at all).
  ///
  /// Calls that unwind the stack are legal for this predicate.
  ///
  /// This property allows many common optimizations to be performed in the
  /// absence of interfering store instructions, such as CSE of strlen calls.
  ///
  /// This property corresponds to the GCC 'pure' attribute.
  /// @param Call Call site to test.
  /// @return True if the call only reads non-volatile memory or does not access memory.
  bool onlyReadsMemory(const CallBase *Call) {
    return getMemoryEffects(Call).onlyReadsMemory();
  }

  /// Checks if the specified function is known to only read from non-volatile
  /// memory (or not access memory at all).
  ///
  /// Functions that unwind the stack are legal for this predicate.
  ///
  /// This property allows many common optimizations to be performed in the
  /// absence of interfering store instructions, such as CSE of strlen calls.
  ///
  /// This property corresponds to the GCC 'pure' attribute.
  /// @param F Function to test.
  /// @return True if the function only reads non-volatile memory or does not access memory.
  bool onlyReadsMemory(const Function *F) {
    return getMemoryEffects(F).onlyReadsMemory();
  }

  /// Check whether or not an instruction may read or write the optionally
  /// specified memory location.
  ///
  ///
  /// An instruction that doesn't read or write memory may be trivially LICM'd
  /// for example.
  ///
  /// For function calls, this delegates to the alias-analysis specific
  /// call-site mod-ref behavior queries. Otherwise it delegates to the specific
  /// helpers above.
  /// @param I Instruction whose ModRef behavior is queried.
  /// @param OptLoc Optional memory location to check against, or nullopt for
  ///        any location.
  /// @return ModRef info describing how the instruction may access memory.
  ModRefInfo getModRefInfo(const Instruction *I,
                           const std::optional<MemoryLocation> &OptLoc) {
    SimpleAAQueryInfo AAQIP(*this);
    return getModRefInfo(I, OptLoc, AAQIP);
  }

  /// A convenience wrapper for constructing the memory location.
  /// @param I Instruction whose ModRef behavior is queried.
  /// @param P Pointer of the memory location.
  /// @param Size Access size of the memory location.
  /// @return ModRef info describing how the instruction may access the location.
  ModRefInfo getModRefInfo(const Instruction *I, const Value *P,
                           LocationSize Size) {
    return getModRefInfo(I, MemoryLocation(P, Size));
  }

  /// Return information about whether a call and an instruction may refer to
  /// the same memory locations.
  /// @param I Instruction to compare against the call.
  /// @param Call Call site to compare against the instruction.
  /// @return ModRef info describing shared memory access between \p I and \p Call.
  LLVM_ABI ModRefInfo getModRefInfo(const Instruction *I, const CallBase *Call);

  /// Return information about whether two instructions may refer to the same
  /// memory locations.
  /// @param I1 First instruction.
  /// @param I2 Second instruction.
  /// @return ModRef info describing shared memory access between the instructions.
  LLVM_ABI ModRefInfo getModRefInfo(const Instruction *I1,
                                    const Instruction *I2);

  /// Return information about whether a particular call site modifies
  /// or reads the specified memory location \p MemLoc before instruction \p I
  /// in a BasicBlock.
  /// @param I Instruction providing the query context.
  /// @param MemLoc Memory location that may be captured.
  /// @param DT Dominator tree used for capture analysis.
  /// @return ModRef info describing capture-based access before \p I.
  ModRefInfo callCapturesBefore(const Instruction *I,
                                const MemoryLocation &MemLoc,
                                DominatorTree *DT) {
    SimpleAAQueryInfo AAQIP(*this);
    return callCapturesBefore(I, MemLoc, DT, AAQIP);
  }

  /// A convenience wrapper to synthesize a memory location.
  /// @param I Instruction providing the query context.
  /// @param P Pointer of the memory location.
  /// @param Size Access size of the memory location.
  /// @param DT Dominator tree used for capture analysis.
  /// @return ModRef info describing capture-based access before \p I.
  ModRefInfo callCapturesBefore(const Instruction *I, const Value *P,
                                LocationSize Size, DominatorTree *DT) {
    return callCapturesBefore(I, MemoryLocation(P, Size), DT);
  }

  /// @}
  //===--------------------------------------------------------------------===//
  /// \name Higher level methods for querying mod/ref information.
  /// @{

  /// Check if it is possible for execution of the specified basic block to
  /// modify the location Loc.
  /// @param BB Basic block whose instructions are examined.
  /// @param Loc Memory location that may be modified.
  /// @return True if execution of \p BB may modify \p Loc.
  LLVM_ABI bool canBasicBlockModify(const BasicBlock &BB,
                                    const MemoryLocation &Loc);

  /// A convenience wrapper synthesizing a memory location.
  /// @param BB Basic block whose instructions are examined.
  /// @param P Pointer of the memory location.
  /// @param Size Access size of the memory location.
  /// @return True if execution of \p BB may modify the location.
  bool canBasicBlockModify(const BasicBlock &BB, const Value *P,
                           LocationSize Size) {
    return canBasicBlockModify(BB, MemoryLocation(P, Size));
  }

  /// Check if it is possible for the execution of the specified instructions
  /// to mod\ref (according to the mode) the location Loc.
  ///
  /// The instructions to consider are all of the instructions in the range of
  /// [I1,I2] INCLUSIVE. I1 and I2 must be in the same basic block.
  /// @param I1 First instruction in the inclusive range.
  /// @param I2 Last instruction in the inclusive range.
  /// @param Loc Memory location that may be accessed.
  /// @param Mode Required ModRef bits to consider a hit.
  /// @return True if any instruction in [I1, I2] may access \p Loc as \p Mode.
  LLVM_ABI bool canInstructionRangeModRef(const Instruction &I1,
                                          const Instruction &I2,
                                          const MemoryLocation &Loc,
                                          const ModRefInfo Mode);

  /// A convenience wrapper synthesizing a memory location.
  /// @param I1 First instruction in the inclusive range.
  /// @param I2 Last instruction in the inclusive range.
  /// @param Ptr Pointer of the memory location.
  /// @param Size Access size of the memory location.
  /// @param Mode Required ModRef bits to consider a hit.
  /// @return True if any instruction in [I1, I2] may access the location as \p Mode.
  bool canInstructionRangeModRef(const Instruction &I1, const Instruction &I2,
                                 const Value *Ptr, LocationSize Size,
                                 const ModRefInfo Mode) {
    return canInstructionRangeModRef(I1, I2, MemoryLocation(Ptr, Size), Mode);
  }

  /// Query whether two memory locations may alias using query state \p AAQI.
  ///
  /// \p CtxI can be nullptr, in which case the query is whether or not the
  /// aliasing relationship holds through the entire function.
  /// @param LocA First memory location.
  /// @param LocB Second memory location.
  /// @param AAQI Query state and caches for this alias query.
  /// @param CtxI Optional context instruction for the query.
  /// @return An AliasResult indicating whether the locations alias.
  LLVM_ABI AliasResult alias(const MemoryLocation &LocA,
                             const MemoryLocation &LocB, AAQueryInfo &AAQI,
                             const Instruction *CtxI = nullptr);
  /// Return whether \p Loc may alias errno at context \p CtxI.
  /// @param Loc Memory location that may alias errno.
  /// @param CtxI Context instruction for the errno query.
  /// @return An AliasResult indicating whether \p Loc may alias errno.
  LLVM_ABI AliasResult aliasErrno(const MemoryLocation &Loc,
                                  const Instruction *CtxI);

  /// Return a ModRef bitmask for \p Loc using query state \p AAQI.
  /// @param Loc Memory location whose ModRef mask is requested.
  /// @param AAQI Query state and caches for this query.
  /// @param IgnoreLocals When true, treat local allocas as NoModRef.
  /// @return A ModRef bitmask that can be applied to ModRef info for \p Loc.
  LLVM_ABI ModRefInfo getModRefInfoMask(const MemoryLocation &Loc,
                                        AAQueryInfo &AAQI,
                                        bool IgnoreLocals = false);
  /// Return ModRef info between instruction \p I and call \p Call2.
  /// @param I Instruction to compare against the call.
  /// @param Call2 Call site to compare against the instruction.
  /// @param AAQIP Query state and caches for this query.
  /// @return ModRef info describing shared memory access between \p I and \p Call2.
  LLVM_ABI ModRefInfo getModRefInfo(const Instruction *I, const CallBase *Call2,
                                    AAQueryInfo &AAQIP);
  /// Return ModRef info for call \p Call against location \p Loc.
  /// @param Call Call site whose ModRef behavior is queried.
  /// @param Loc Memory location to check against the call.
  /// @param AAQI Query state and caches for this query.
  /// @return ModRef info describing how the call may access \p Loc.
  LLVM_ABI ModRefInfo getModRefInfo(const CallBase *Call,
                                    const MemoryLocation &Loc,
                                    AAQueryInfo &AAQI);
  /// Return ModRef info between two call sites.
  /// @param Call1 First call site.
  /// @param Call2 Second call site.
  /// @param AAQI Query state and caches for this query.
  /// @return ModRef info describing shared memory access between the call sites.
  LLVM_ABI ModRefInfo getModRefInfo(const CallBase *Call1,
                                    const CallBase *Call2, AAQueryInfo &AAQI);
  /// Return ModRef info for a va_arg against location \p Loc.
  /// @param V VAArg instruction whose ModRef behavior is queried.
  /// @param Loc Memory location to check against the instruction.
  /// @param AAQI Query state and caches for this query.
  /// @return ModRef info describing how the va_arg may access \p Loc.
  LLVM_ABI ModRefInfo getModRefInfo(const VAArgInst *V,
                                    const MemoryLocation &Loc,
                                    AAQueryInfo &AAQI);
  /// Return ModRef info for a load against location \p Loc.
  /// @param L Load instruction whose ModRef behavior is queried.
  /// @param Loc Memory location to check against the load.
  /// @param AAQI Query state and caches for this query.
  /// @return ModRef info describing how the load may access \p Loc.
  LLVM_ABI ModRefInfo getModRefInfo(const LoadInst *L,
                                    const MemoryLocation &Loc,
                                    AAQueryInfo &AAQI);
  /// Return ModRef info for a store against location \p Loc.
  /// @param S Store instruction whose ModRef behavior is queried.
  /// @param Loc Memory location to check against the store.
  /// @param AAQI Query state and caches for this query.
  /// @return ModRef info describing how the store may access \p Loc.
  LLVM_ABI ModRefInfo getModRefInfo(const StoreInst *S,
                                    const MemoryLocation &Loc,
                                    AAQueryInfo &AAQI);
  /// Return ModRef info for a fence against location \p Loc.
  /// @param S Fence instruction whose ModRef behavior is queried.
  /// @param Loc Memory location to check against the fence.
  /// @param AAQI Query state and caches for this query.
  /// @return ModRef info describing how the fence may access \p Loc.
  LLVM_ABI ModRefInfo getModRefInfo(const FenceInst *S,
                                    const MemoryLocation &Loc,
                                    AAQueryInfo &AAQI);
  /// Return ModRef info for an atomic cmpxchg against location \p Loc.
  /// @param CX Atomic cmpxchg whose ModRef behavior is queried.
  /// @param Loc Memory location to check against the cmpxchg.
  /// @param AAQI Query state and caches for this query.
  /// @return ModRef info describing how the cmpxchg may access \p Loc.
  LLVM_ABI ModRefInfo getModRefInfo(const AtomicCmpXchgInst *CX,
                                    const MemoryLocation &Loc,
                                    AAQueryInfo &AAQI);
  /// Return ModRef info for an atomic RMW against location \p Loc.
  /// @param RMW Atomic RMW whose ModRef behavior is queried.
  /// @param Loc Memory location to check against the RMW.
  /// @param AAQI Query state and caches for this query.
  /// @return ModRef info describing how the RMW may access \p Loc.
  LLVM_ABI ModRefInfo getModRefInfo(const AtomicRMWInst *RMW,
                                    const MemoryLocation &Loc,
                                    AAQueryInfo &AAQI);
  /// Return ModRef info for a catchpad against location \p Loc.
  /// @param I CatchPad instruction whose ModRef behavior is queried.
  /// @param Loc Memory location to check against the catchpad.
  /// @param AAQI Query state and caches for this query.
  /// @return ModRef info describing how the catchpad may access \p Loc.
  LLVM_ABI ModRefInfo getModRefInfo(const CatchPadInst *I,
                                    const MemoryLocation &Loc,
                                    AAQueryInfo &AAQI);
  /// Return ModRef info for a catchreturn against location \p Loc.
  /// @param I CatchReturn instruction whose ModRef behavior is queried.
  /// @param Loc Memory location to check against the catchreturn.
  /// @param AAQI Query state and caches for this query.
  /// @return ModRef info describing how the catchreturn may access \p Loc.
  LLVM_ABI ModRefInfo getModRefInfo(const CatchReturnInst *I,
                                    const MemoryLocation &Loc,
                                    AAQueryInfo &AAQI);
  /// Return ModRef info for instruction \p I against optional location \p OptLoc.
  /// @param I Instruction whose ModRef behavior is queried.
  /// @param OptLoc Optional memory location to check against, or nullopt.
  /// @param AAQIP Query state and caches for this query.
  /// @return ModRef info describing how the instruction may access memory.
  LLVM_ABI ModRefInfo getModRefInfo(const Instruction *I,
                                    const std::optional<MemoryLocation> &OptLoc,
                                    AAQueryInfo &AAQIP);
  /// Return ModRef info between two instructions using query state \p AAQI.
  /// @param I1 First instruction.
  /// @param I2 Second instruction.
  /// @param AAQI Query state and caches for this query.
  /// @return ModRef info describing shared memory access between the instructions.
  LLVM_ABI ModRefInfo getModRefInfo(const Instruction *I1,
                                    const Instruction *I2, AAQueryInfo &AAQI);
  /// Return whether a call may capture \p MemLoc before \p I.
  /// @param I Instruction providing the query context.
  /// @param MemLoc Memory location that may be captured.
  /// @param DT Dominator tree used for capture analysis.
  /// @param AAQIP Query state and caches for this query.
  /// @return ModRef info describing capture-based access before \p I.
  LLVM_ABI ModRefInfo callCapturesBefore(const Instruction *I,
                                         const MemoryLocation &MemLoc,
                                         DominatorTree *DT, AAQueryInfo &AAQIP);
  /// Return the memory effects of call \p Call using query state \p AAQI.
  /// @param Call Call site whose memory effects are queried.
  /// @param AAQI Query state and caches for this query.
  /// @return Memory effects of the call site.
  LLVM_ABI MemoryEffects getMemoryEffects(const CallBase *Call,
                                          AAQueryInfo &AAQI);

private:
  class Concept;

  template <typename T> class Model;

  friend class AAResultBase;

  const TargetLibraryInfo &TLI;

  std::vector<std::unique_ptr<Concept>> AAs;

  std::vector<AnalysisKey *> AADeps;

  friend class BatchAAResults;
};

/// Batch wrapper that reuses AAQueryInfo across alias queries.
///
/// This class is a wrapper over an AAResults, and it is intended to be used
/// only when there are no IR changes inbetween queries. BatchAAResults is
/// reusing the same `AAQueryInfo` to preserve the state across queries,
/// esentially making AA work in "batch mode". The internal state cannot be
/// cleared, so to go "out-of-batch-mode", the user must either use AAResults,
/// or create a new BatchAAResults.
class BatchAAResults {
  AAResults &AA;
  AAQueryInfo AAQI;
  SimpleCaptureAnalysis SimpleCA;

  friend class BatchAACrossIterationScope;

public:
  /// Construct batch AA over \p AAR using a SimpleCaptureAnalysis.
  /// @param AAR Aggregated AA results to wrap.
  BatchAAResults(AAResults &AAR) : AA(AAR), AAQI(AAR, &SimpleCA) {}
  /// Construct batch AA over \p AAR using capture analysis \p CA.
  /// @param AAR Aggregated AA results to wrap.
  /// @param CA Capture analysis provider for batch queries.
  BatchAAResults(AAResults &AAR, CaptureAnalysis *CA)
      : AA(AAR), AAQI(AAR, CA) {}

  /// Query whether two memory locations may alias.
  /// @param LocA First memory location.
  /// @param LocB Second memory location.
  /// @return An AliasResult indicating whether the locations alias.
  AliasResult alias(const MemoryLocation &LocA, const MemoryLocation &LocB) {
    return AA.alias(LocA, LocB, AAQI);
  }
  /// Return true if \p Loc points to constant (or local) memory.
  /// @param Loc Memory location to test.
  /// @param OrLocal When true, also treat local allocas as constant.
  /// @return True if the location points to constant (or local) memory.
  bool pointsToConstantMemory(const MemoryLocation &Loc, bool OrLocal = false) {
    return isNoModRef(AA.getModRefInfoMask(Loc, AAQI, OrLocal));
  }
  /// Return true if \p P points to constant (or local) memory.
  /// @param P Pointer value to test.
  /// @param OrLocal When true, also treat local allocas as constant.
  /// @return True if the pointer points to constant (or local) memory.
  bool pointsToConstantMemory(const Value *P, bool OrLocal = false) {
    return pointsToConstantMemory(MemoryLocation::getBeforeOrAfter(P), OrLocal);
  }
  /// Return a ModRef bitmask for memory location \p Loc.
  /// @param Loc Memory location whose ModRef mask is requested.
  /// @param IgnoreLocals When true, treat local allocas as NoModRef.
  /// @return A ModRef bitmask that can be applied to ModRef info for \p Loc.
  ModRefInfo getModRefInfoMask(const MemoryLocation &Loc,
                               bool IgnoreLocals = false) {
    return AA.getModRefInfoMask(Loc, AAQI, IgnoreLocals);
  }
  /// Return ModRef info for instruction \p I against optional location \p OptLoc.
  /// @param I Instruction whose ModRef behavior is queried.
  /// @param OptLoc Optional memory location to check against, or nullopt.
  /// @return ModRef info describing how the instruction may access memory.
  ModRefInfo getModRefInfo(const Instruction *I,
                           const std::optional<MemoryLocation> &OptLoc) {
    return AA.getModRefInfo(I, OptLoc, AAQI);
  }
  /// Return ModRef info between instruction \p I and call \p Call2.
  /// @param I Instruction to compare against the call.
  /// @param Call2 Call site to compare against the instruction.
  /// @return ModRef info describing shared memory access between \p I and \p Call2.
  ModRefInfo getModRefInfo(const Instruction *I, const CallBase *Call2) {
    return AA.getModRefInfo(I, Call2, AAQI);
  }
  /// Return ModRef info between two instructions.
  /// @param I First instruction.
  /// @param I2 Second instruction.
  /// @return ModRef info describing shared memory access between the instructions.
  ModRefInfo getModRefInfo(const Instruction *I, const Instruction *I2) {
    return AA.getModRefInfo(I, I2, AAQI);
  }
  /// Return ModRef info for pointer argument \p ArgIdx of \p Call.
  /// @param Call Call whose argument ModRef info is queried.
  /// @param ArgIdx Zero-based index of the pointer argument.
  /// @return ModRef info describing how the argument may be accessed.
  ModRefInfo getArgModRefInfo(const CallBase *Call, unsigned ArgIdx) {
    return AA.getArgModRefInfo(Call, ArgIdx);
  }
  /// Return the memory effects of call site \p Call.
  /// @param Call Call site whose memory effects are queried.
  /// @return Memory effects of the call site.
  MemoryEffects getMemoryEffects(const CallBase *Call) {
    return AA.getMemoryEffects(Call, AAQI);
  }
  /// Return true if \p LocA and \p LocB must alias.
  /// @param LocA First memory location.
  /// @param LocB Second memory location.
  /// @return True if the locations are known to must-alias.
  bool isMustAlias(const MemoryLocation &LocA, const MemoryLocation &LocB) {
    return alias(LocA, LocB) == AliasResult::MustAlias;
  }
  /// Return true if \p V1 and \p V2 must alias.
  /// @param V1 First pointer value.
  /// @param V2 Second pointer value.
  /// @return True if the pointers are known to must-alias.
  bool isMustAlias(const Value *V1, const Value *V2) {
    return alias(MemoryLocation(V1, LocationSize::precise(1)),
                 MemoryLocation(V2, LocationSize::precise(1))) ==
           AliasResult::MustAlias;
  }
  /// Return true if \p LocA and \p LocB do not alias.
  /// @param LocA First memory location.
  /// @param LocB Second memory location.
  /// @return True if the locations are known not to alias.
  bool isNoAlias(const MemoryLocation &LocA, const MemoryLocation &LocB) {
    return alias(LocA, LocB) == AliasResult::NoAlias;
  }
  /// Return whether a call may capture \p MemLoc before \p I.
  /// @param I Instruction providing the query context.
  /// @param MemLoc Memory location that may be captured.
  /// @param DT Dominator tree used for capture analysis.
  /// @return ModRef info describing capture-based access before \p I.
  ModRefInfo callCapturesBefore(const Instruction *I,
                                const MemoryLocation &MemLoc,
                                DominatorTree *DT) {
    return AA.callCapturesBefore(I, MemLoc, DT, AAQI);
  }

  /// Assume that values may come from different cycle iterations.
  void enableCrossIterationMode() {
    AAQI.MayBeCrossIteration = true;
  }

  /// Disable the use of the dominator tree during alias analysis queries.
  void disableDominatorTree() { AAQI.UseDominatorTree = false; }
};

/// Temporarily set the cross iteration mode on a BatchAA instance.
class BatchAACrossIterationScope {
  BatchAAResults &BAA;
  bool OrigCrossIteration;

public:
  /// Set cross-iteration mode on \p BAA for the lifetime of this object.
  /// @param BAA Batch AA whose cross-iteration flag is adjusted.
  /// @param CrossIteration Desired MayBeCrossIteration value.
  BatchAACrossIterationScope(BatchAAResults &BAA, bool CrossIteration)
      : BAA(BAA), OrigCrossIteration(BAA.AAQI.MayBeCrossIteration) {
    BAA.AAQI.MayBeCrossIteration = CrossIteration;
  }
  /// Restore the previous cross-iteration mode on the BatchAA instance.
  ~BatchAACrossIterationScope() {
    BAA.AAQI.MayBeCrossIteration = OrigCrossIteration;
  }
};

/// Temporary typedef for legacy code that uses a generic \c AliasAnalysis
/// pointer or reference.
using AliasAnalysis = AAResults;

/// A private abstract base class describing the concept of an individual alias
/// analysis implementation.
///
/// This interface is implemented by any \c Model instantiation. It is also the
/// interface which a type used to instantiate the model must provide.
///
/// All of these methods model methods by the same name in the \c
/// AAResults class. Only differences and specifics to how the
/// implementations are called are documented here.
class LLVM_ABI AAResults::Concept {
public:
  virtual ~Concept() = 0;

  //===--------------------------------------------------------------------===//
  /// \name Alias Queries
  /// @{

  /// The main low level interface to the alias analysis implementation.
  /// Returns an AliasResult indicating whether the two pointers are aliased to
  /// each other. This is the interface that must be implemented by specific
  /// alias analysis implementations.
  virtual AliasResult alias(const MemoryLocation &LocA,
                            const MemoryLocation &LocB, AAQueryInfo &AAQI,
                            const Instruction *CtxI) = 0;

  /// Returns an AliasResult indicating whether a specific memory location
  /// aliases errno.
  virtual AliasResult aliasErrno(const MemoryLocation &Loc,
                                 const Instruction *CtxI) = 0;

  /// @}
  //===--------------------------------------------------------------------===//
  /// \name Simple mod/ref information
  /// @{

  /// Returns a bitmask that should be unconditionally applied to the ModRef
  /// info of a memory location. This allows us to eliminate Mod and/or Ref from
  /// the ModRef info based on the knowledge that the memory location points to
  /// constant and/or locally-invariant memory.
  virtual ModRefInfo getModRefInfoMask(const MemoryLocation &Loc,
                                       AAQueryInfo &AAQI,
                                       bool IgnoreLocals) = 0;

  /// Get the ModRef info associated with a pointer argument of a callsite. The
  /// result's bits are set to indicate the allowed aliasing ModRef kinds. Note
  /// that these bits do not necessarily account for the overall behavior of
  /// the function, but rather only provide additional per-argument
  /// information.
  virtual ModRefInfo getArgModRefInfo(const CallBase *Call,
                                      unsigned ArgIdx) = 0;

  /// Return the behavior of the given call site.
  virtual MemoryEffects getMemoryEffects(const CallBase *Call,
                                         AAQueryInfo &AAQI) = 0;

  /// Return the behavior when calling the given function.
  virtual MemoryEffects getMemoryEffects(const Function *F) = 0;

  /// getModRefInfo (for call sites) - Return information about whether
  /// a particular call site modifies or reads the specified memory location.
  virtual ModRefInfo getModRefInfo(const CallBase *Call,
                                   const MemoryLocation &Loc,
                                   AAQueryInfo &AAQI) = 0;

  /// Return information about whether two call sites may refer to the same set
  /// of memory locations. See the AA documentation for details:
  ///   http://llvm.org/docs/AliasAnalysis.html#ModRefInfo
  virtual ModRefInfo getModRefInfo(const CallBase *Call1, const CallBase *Call2,
                                   AAQueryInfo &AAQI) = 0;

  /// getModRefInfo (for fences) - Return information about whether
  /// a particular fence modifies or reads the specified memory location.
  virtual ModRefInfo getModRefInfo(const FenceInst *F,
                                   const MemoryLocation &Loc,
                                   AAQueryInfo &AAQI) = 0;

  /// @}
};

/// A private class template which derives from \c Concept and wraps some other
/// type.
///
/// This models the concept by directly forwarding each interface point to the
/// wrapped type which must implement a compatible interface. This provides
/// a type erased binding.
template <typename AAResultT> class AAResults::Model final : public Concept {
  AAResultT &Result;

public:
  explicit Model(AAResultT &Result, AAResults &AAR) : Result(Result) {}
  ~Model() override = default;

  AliasResult alias(const MemoryLocation &LocA, const MemoryLocation &LocB,
                    AAQueryInfo &AAQI, const Instruction *CtxI) override {
    return Result.alias(LocA, LocB, AAQI, CtxI);
  }

  AliasResult aliasErrno(const MemoryLocation &Loc,
                         const Instruction *CtxI) override {
    return Result.aliasErrno(Loc, CtxI);
  }

  ModRefInfo getModRefInfoMask(const MemoryLocation &Loc, AAQueryInfo &AAQI,
                               bool IgnoreLocals) override {
    return Result.getModRefInfoMask(Loc, AAQI, IgnoreLocals);
  }

  ModRefInfo getArgModRefInfo(const CallBase *Call, unsigned ArgIdx) override {
    return Result.getArgModRefInfo(Call, ArgIdx);
  }

  MemoryEffects getMemoryEffects(const CallBase *Call,
                                 AAQueryInfo &AAQI) override {
    return Result.getMemoryEffects(Call, AAQI);
  }

  MemoryEffects getMemoryEffects(const Function *F) override {
    return Result.getMemoryEffects(F);
  }

  ModRefInfo getModRefInfo(const CallBase *Call, const MemoryLocation &Loc,
                           AAQueryInfo &AAQI) override {
    return Result.getModRefInfo(Call, Loc, AAQI);
  }

  ModRefInfo getModRefInfo(const CallBase *Call1, const CallBase *Call2,
                           AAQueryInfo &AAQI) override {
    return Result.getModRefInfo(Call1, Call2, AAQI);
  }

  ModRefInfo getModRefInfo(const FenceInst *F, const MemoryLocation &Loc,
                           AAQueryInfo &AAQI) override {
    return Result.getModRefInfo(F, Loc, AAQI);
  }
};

/// A base class to help implement the function alias analysis results concept.
///
/// Because of the nature of many alias analysis implementations, they often
/// only implement a subset of the interface. This base class will attempt to
/// implement the remaining portions of the interface in terms of simpler forms
/// of the interface where possible, and otherwise provide conservatively
/// correct fallback implementations.
///
/// Implementors of an alias analysis should derive from this class, and then
/// override specific methods that they wish to customize. There is no need to
/// use virtual anywhere.
class AAResultBase {
protected:
  /// Default-construct a base AA result with conservative fallbacks.
  explicit AAResultBase() = default;

  // Provide all the copy and move constructors so that derived types aren't
  // constrained.
  /// Copy-construct a base AA result.
  /// @param Arg AAResultBase to copy.
  AAResultBase(const AAResultBase &Arg) = default;
  /// Move-construct a base AA result.
  /// @param Arg AAResultBase to move from.
  AAResultBase(AAResultBase &&Arg) {}

public:
  /// Conservatively report that \p LocA and \p LocB may alias.
  /// @param LocA First memory location.
  /// @param LocB Second memory location.
  /// @param AAQI Query state and caches for this query.
  /// @param I Optional context instruction for the query.
  /// @return AliasResult::MayAlias as a conservative fallback.
  AliasResult alias(const MemoryLocation &LocA, const MemoryLocation &LocB,
                    AAQueryInfo &AAQI, const Instruction *I) {
    return AliasResult::MayAlias;
  }

  /// Conservatively report that \p Loc may alias errno.
  /// @param Loc Memory location that may alias errno.
  /// @param CtxI Context instruction for the errno query.
  /// @return AliasResult::MayAlias as a conservative fallback.
  AliasResult aliasErrno(const MemoryLocation &Loc, const Instruction *CtxI) {
    return AliasResult::MayAlias;
  }

  /// Conservatively return a full ModRef mask for \p Loc.
  /// @param Loc Memory location whose ModRef mask is requested.
  /// @param AAQI Query state and caches for this query.
  /// @param IgnoreLocals When true, local allocas may be treated specially.
  /// @return ModRefInfo::ModRef as a conservative full mask.
  ModRefInfo getModRefInfoMask(const MemoryLocation &Loc, AAQueryInfo &AAQI,
                               bool IgnoreLocals) {
    return ModRefInfo::ModRef;
  }

  /// Conservatively return ModRef for pointer argument \p ArgIdx of \p Call.
  /// @param Call Call whose argument ModRef info is queried.
  /// @param ArgIdx Zero-based index of the pointer argument.
  /// @return ModRefInfo::ModRef as a conservative fallback.
  ModRefInfo getArgModRefInfo(const CallBase *Call, unsigned ArgIdx) {
    return ModRefInfo::ModRef;
  }

  /// Conservatively return unknown memory effects for call site \p Call.
  /// @param Call Call site whose memory effects are queried.
  /// @param AAQI Query state and caches for this query.
  /// @return Unknown memory effects as a conservative fallback.
  MemoryEffects getMemoryEffects(const CallBase *Call, AAQueryInfo &AAQI) {
    return MemoryEffects::unknown();
  }

  /// Conservatively return unknown memory effects for function \p F.
  /// @param F Function whose memory effects are queried.
  /// @return Unknown memory effects as a conservative fallback.
  MemoryEffects getMemoryEffects(const Function *F) {
    return MemoryEffects::unknown();
  }

  /// Conservatively return ModRef for call \p Call against location \p Loc.
  /// @param Call Call site whose ModRef behavior is queried.
  /// @param Loc Memory location to check against the call.
  /// @param AAQI Query state and caches for this query.
  /// @return ModRefInfo::ModRef as a conservative fallback.
  ModRefInfo getModRefInfo(const CallBase *Call, const MemoryLocation &Loc,
                           AAQueryInfo &AAQI) {
    return ModRefInfo::ModRef;
  }

  /// Conservatively return ModRef between two call sites.
  /// @param Call1 First call site.
  /// @param Call2 Second call site.
  /// @param AAQI Query state and caches for this query.
  /// @return ModRefInfo::ModRef as a conservative fallback.
  ModRefInfo getModRefInfo(const CallBase *Call1, const CallBase *Call2,
                           AAQueryInfo &AAQI) {
    return ModRefInfo::ModRef;
  }

  /// Conservatively return ModRef for fence \p F against location \p Loc.
  /// @param F Fence instruction whose ModRef behavior is queried.
  /// @param Loc Memory location to check against the fence.
  /// @param AAQI Query state and caches for this query.
  /// @return ModRefInfo::ModRef as a conservative fallback.
  ModRefInfo getModRefInfo(const FenceInst *F, const MemoryLocation &Loc,
                           AAQueryInfo &AAQI) {
    return ModRefInfo::ModRef;
  }
};

/// Return true if this pointer is returned by a noalias function.
/// @param V Pointer value to test.
/// @return True if \p V is returned by a noalias function.
LLVM_ABI bool isNoAliasCall(const Value *V);

/// Return true if this pointer refers to a distinct and identifiable object.
///
/// This returns true for:
///    Global Variables and Functions (but not Global Aliases)
///    Allocas
///    ByVal and NoAlias Arguments
///    NoAlias returns (e.g. calls to malloc)
/// @param V Pointer value to test.
/// @return True if \p V refers to a distinct and identifiable object.
LLVM_ABI bool isIdentifiedObject(const Value *V);

/// Return true if V is unambiguously identified at the function level.
///
/// Different IdentifiedFunctionLocals can't alias. Further, an
/// IdentifiedFunctionLocal can not alias with any function arguments other
/// than itself, which is not necessarily true for IdentifiedObjects.
/// @param V Pointer value to test.
/// @return True if \p V is unambiguously identified at the function level.
LLVM_ABI bool isIdentifiedFunctionLocal(const Value *V);

/// Return true if V is known to be the base of its memory object.
///
/// This implies that any address less than V must be out of bounds for the
/// underlying object. Note that just being isIdentifiedObject() is not enough
/// - For example, a negative offset from a noalias argument or call can be
/// inbounds w.r.t the actual underlying object.
/// @param V Pointer value to test.
/// @return True if \p V is known to be the base of its memory object.
LLVM_ABI bool isBaseOfObject(const Value *V);

/// Returns true if the pointer is one which would have been considered an
/// escape by isNotCapturedBefore.
/// @param V Pointer value to test.
/// @return True if \p V would be considered an escape by isNotCapturedBefore.
LLVM_ABI bool isEscapeSource(const Value *V);

/// Return true if Object memory is not visible after an unwind.
///
/// Program semantics cannot depend on Object containing any particular value
/// on unwind. If the RequiresNoCaptureBeforeUnwind out parameter is set to
/// true, then the memory is only not visible if the object has not been
/// captured prior to the unwind. Otherwise it is not visible even if captured.
/// @param Object Pointer to the object being tested.
/// @param RequiresNoCaptureBeforeUnwind Set to true when invisibility also
///        requires that the object was not captured before the unwind.
/// @return True if \p Object memory is not visible after an unwind.
LLVM_ABI bool isNotVisibleOnUnwind(const Value *Object,
                                   bool &RequiresNoCaptureBeforeUnwind);

/// Return true if Object is writable without trapping.
///
/// Any location based on this pointer that can be loaded can also be stored to
/// without trapping. Additionally, at the point Object is declared, stores can
/// be introduced without data races. At later points, this is only the case if
/// the pointer can not escape to a different thread.
///
/// If ExplicitlyDereferenceableOnly is set to true, this property only holds
/// for the part of Object that is explicitly marked as dereferenceable, e.g.
/// using the dereferenceable(N) attribute. It does not necessarily hold for
/// parts that are only known to be dereferenceable due to the presence of
/// loads.
/// @param Object Pointer to the object being tested.
/// @param ExplicitlyDereferenceableOnly Set to true when the property holds
///        only for explicitly dereferenceable parts of the object.
/// @return True if \p Object is writable without trapping.
LLVM_ABI bool isWritableObject(const Value *Object,
                               bool &ExplicitlyDereferenceableOnly);

/// Get ModRefInfo for a synchronizing operation, such as a fence or stronger
/// than monotonic atomic load/store.
/// @param AA Aggregated AA results used for the query.
/// @param Loc Memory location affected by the synchronizing operation.
/// @param AAQI Query state and caches for this query.
/// @return ModRef info for the synchronizing operation against \p Loc.
LLVM_ABI ModRefInfo getSyncEffects(AAResults *AA, const MemoryLocation &Loc,
                                   AAQueryInfo &AAQI);

/// A manager for alias analyses.
///
/// This class can have analyses registered with it and when run, it will run
/// all of them and aggregate their results into single AA results interface
/// that dispatches across all of the alias analysis results available.
///
/// Note that the order in which analyses are registered is very significant.
/// That is the order in which the results will be aggregated and queried.
///
/// This manager effectively wraps the AnalysisManager for registering alias
/// analyses. When you register your alias analysis with this manager, it will
/// ensure the analysis itself is registered with its AnalysisManager.
///
/// The result of this analysis is only invalidated if one of the particular
/// aggregated AA results end up being invalidated. This removes the need to
/// explicitly preserve the results of `AAManager`. Note that analyses should no
/// longer be registered once the `AAManager` is run.
class AAManager : public AnalysisInfoMixin<AAManager> {
public:
  /// Aggregated AA results produced by this manager.
  using Result = AAResults;

  /// Register a specific AA result.
  template <typename AnalysisT> void registerFunctionAnalysis() {
    ResultGetters.push_back(&getFunctionAAResultImpl<AnalysisT>);
  }

  /// Register a specific AA result.
  template <typename AnalysisT> void registerModuleAnalysis() {
    ResultGetters.push_back(&getModuleAAResultImpl<AnalysisT>);
  }

  /// Run registered alias analyses on \p F and aggregate their results.
  /// @param F Function to analyze.
  /// @param AM Function analysis manager providing registered AA results.
  /// @return Aggregated AA results for \p F.
  LLVM_ABI Result run(Function &F, FunctionAnalysisManager &AM);

private:
  friend AnalysisInfoMixin<AAManager>;

  LLVM_ABI static AnalysisKey Key;

  SmallVector<void (*)(Function &F, FunctionAnalysisManager &AM,
                       AAResults &AAResults),
              4> ResultGetters;

  template <typename AnalysisT>
  static void getFunctionAAResultImpl(Function &F,
                                      FunctionAnalysisManager &AM,
                                      AAResults &AAResults) {
    AAResults.addAAResult(AM.template getResult<AnalysisT>(F));
    AAResults.addAADependencyID(AnalysisT::ID());
  }

  template <typename AnalysisT>
  static void getModuleAAResultImpl(Function &F, FunctionAnalysisManager &AM,
                                    AAResults &AAResults) {
    auto &MAMProxy = AM.getResult<ModuleAnalysisManagerFunctionProxy>(F);
    if (auto *R =
            MAMProxy.template getCachedResult<AnalysisT>(*F.getParent())) {
      AAResults.addAAResult(*R);
      MAMProxy
          .template registerOuterAnalysisInvalidation<AnalysisT, AAManager>();
    }
  }
};

/// A wrapper pass to provide the legacy pass manager access to a suitably
/// prepared AAResults object.
class LLVM_ABI AAResultsWrapperPass : public FunctionPass {
  std::unique_ptr<AAResults> AAR;

public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct an AAResultsWrapperPass.
  AAResultsWrapperPass();

  /// Return the aggregated AA results computed for the last function.
  /// @return Aggregated AA results for the last function.
  AAResults &getAAResults() { return *AAR; }
  /// Return the aggregated AA results computed for the last function.
  /// @return Aggregated AA results for the last function.
  const AAResults &getAAResults() const { return *AAR; }

  /// Compute aggregated AA results for function \p F.
  /// @param F Function to analyze.
  /// @return False; this analysis pass does not modify the function.
  bool runOnFunction(Function &F) override;

  /// Declare the analyses required and preserved by this pass.
  /// @param AU Analysis usage to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

/// A wrapper pass for external alias analyses. This just squirrels away the
/// callback used to run any analyses and register their results.
struct ExternalAAWrapperPass : ImmutablePass {
  /// Callback used to register external AA results into an AAResults.
  using CallbackT = std::function<void(Pass &, Function &, AAResults &)>;

  /// Callback invoked when preparing AAResults for a function.
  CallbackT CB;

  /// Pass identification, replacement for typeid.
  LLVM_ABI static char ID;

  /// Construct an ExternalAAWrapperPass with no callback.
  LLVM_ABI ExternalAAWrapperPass();

  /// Construct an ExternalAAWrapperPass with callback \p CB.
  /// @param CB Callback used to populate AAResults.
  /// @param RunEarly When true, run this external AA before BasicAA.
  LLVM_ABI explicit ExternalAAWrapperPass(CallbackT CB, bool RunEarly = false);

  /// Flag indicating whether this external AA should run before Basic AA.
  ///
  /// This flag is for LegacyPassManager only. To run an external AA early
  /// with the NewPassManager, override the registerEarlyDefaultAliasAnalyses
  /// method on the target machine.
  ///
  /// By default, external AA passes are run after Basic AA. If this flag is
  /// set to true, the external AA will be run before Basic AA during alias
  /// analysis.
  ///
  /// For some targets, we prefer to run the external AA early to improve
  /// compile time as it has more target-specific information. This is
  /// particularly useful when the external AA can provide more precise results
  /// than Basic AA so that Basic AA does not need to spend time recomputing
  /// them.
  bool RunEarly = false;

  /// Declare that this pass preserves all analyses.
  /// @param AU Analysis usage to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }
};

/// A wrapper pass around a callback which can be used to populate the
/// AAResults in the AAResultsWrapperPass from an external AA.
///
/// The callback provided here will be used each time we prepare an AAResults
/// object, and will receive a reference to the function wrapper pass, the
/// function, and the AAResults object to populate. This should be used when
/// setting up a custom pass pipeline to inject a hook into the AA results.
/// @param Callback Callback used to populate AAResults for each function.
/// @param RunEarly When true, run this external AA before BasicAA.
/// @return A new ImmutablePass that registers \p Callback.
LLVM_ABI ImmutablePass *createExternalAAWrapperPass(
    std::function<void(Pass &, Function &, AAResults &)> Callback,
    bool RunEarly = false);

} // end namespace llvm

#endif // LLVM_ANALYSIS_ALIASANALYSIS_H
