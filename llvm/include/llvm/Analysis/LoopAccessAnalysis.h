//===- llvm/Analysis/LoopAccessAnalysis.h -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the interface for the loop memory dependence framework that
// was originally developed for the Loop Vectorizer.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_LOOPACCESSANALYSIS_H
#define LLVM_ANALYSIS_LOOPACCESSANALYSIS_H

#include "llvm/ADT/EquivalenceClasses.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/TypeSize.h"
#include <optional>
#include <variant>

namespace llvm {

class AAResults;
class DataLayout;
class Loop;
class raw_ostream;
class TargetTransformInfo;

/// Collection of parameters shared beetween the Loop Vectorizer and the
/// Loop Access Analysis.
struct VectorizerParams {
  /// Maximum SIMD width.
  LLVM_ABI static const unsigned MaxVectorWidth;

  /// VF as overridden by the user.
  LLVM_ABI static ElementCount VectorizationFactor;
  /// Interleave factor as overridden by the user.
  LLVM_ABI static unsigned VectorizationInterleave;
  /// True if force-vector-interleave was specified by the user.
  /// @return True if force-vector-interleave was specified by the user.
  LLVM_ABI static bool isInterleaveForced();

  /// \When performing memory disambiguation checks at runtime do not
  /// make more than this number of comparisons.
  LLVM_ABI static unsigned RuntimeMemoryCheckThreshold;

  /// When true, prefer runtime checks that can be hoisted from nested loops.
  ///
  /// When creating runtime checks for nested loops, where possible try to
  /// write the checks in a form that allows them to be easily hoisted out of
  /// the outermost loop. For example, we can do this by expanding the range of
  /// addresses considered to include the entire nested loop so that they are
  /// loop invariant.
  LLVM_ABI static bool HoistRuntimeChecks;
};

/// Checks memory dependences among accesses to the same underlying object.
///
/// Determines whether vectorization is legal and at which vectorization
/// factor.
///
/// Note: This class will compute a conservative dependence for access to
/// different underlying pointers. Clients, such as the loop vectorizer, will
/// sometimes deal these potential dependencies by emitting runtime checks.
///
/// We use the ScalarEvolution framework to symbolically evalutate access
/// functions pairs. Since we currently don't restructure the loop we can rely
/// on the program order of memory accesses to determine their safety.
/// At the moment we will only deem accesses as safe for:
///  * A negative constant distance assuming program order.
///
///      Safe: tmp = a[i + 1];     OR     a[i + 1] = x;
///            a[i] = tmp;                y = a[i];
///
///   The latter case is safe because later checks guarantuee that there can't
///   be a cycle through a phi node (that is, we check that "x" and "y" is not
///   the same variable: a header phi can only be an induction or a reduction, a
///   reduction can't have a memory sink, an induction can't have a memory
///   source). This is important and must not be violated (or we have to
///   resort to checking for cycles through memory).
///
///  * A positive constant distance assuming program order that is bigger
///    than the biggest memory access.
///
///     tmp = a[i]        OR              b[i] = x
///     a[i+2] = tmp                      y = b[i+2];
///
///     Safe distance: 2 x sizeof(a[0]), and 2 x sizeof(b[0]), respectively.
///
///  * Zero distances and all accesses have the same size.
///
class MemoryDepChecker {
public:
  /// Memory access identified by pointer and whether it is a write.
  using MemAccessInfo =
      PointerIntPair<Value * /* AccessPtr */, 1, bool /* IsWrite */>;
  /// Set of potential dependent memory accesses.
  using DepCandidates = EquivalenceClasses<MemAccessInfo>;

  /// Type to keep track of the status of the dependence check. The order of
  /// the elements is important and has to be from most permissive to least
  /// permissive.
  enum class VectorizationSafetyStatus {
    /// Can vectorize safely without RT checks. All dependences are known to be
    /// safe.
    Safe,
    /// Can possibly vectorize with RT checks to overcome unknown dependencies.
    PossiblySafeWithRtChecks,
    /// Cannot vectorize due to known unsafe dependencies.
    Unsafe,
  };

  /// Dependece between memory access instructions.
  struct Dependence {
    /// The type of the dependence.
    enum DepType {
      /// No dependence.
      NoDep,
      /// We couldn't determine the direction or the distance.
      Unknown,
      /// At least one of the memory access instructions may access a loop
      /// varying object, e.g. the address of underlying object is loaded inside
      /// the loop, like A[B[i]]. We cannot determine direction or distance in
      /// those cases, and also are unable to generate any runtime checks.
      IndirectUnsafe,
      /// Both accesses to the same loop-invariant address and at least one is a
      /// write. Vectorization is unsafe because different vector lanes would
      /// read/write the same memory location, and the ordering of accesses
      /// across lanes matters.
      InvariantUnsafe,

      /// Lexically forward.
      ///
      /// FIXME: If we only have loop-independent forward dependences (e.g. a
      /// read and write of A[i]), LAA will locally deem the dependence "safe"
      /// without querying the MemoryDepChecker.  Therefore we can miss
      /// enumerating loop-independent forward dependences in
      /// getDependences.  Note that as soon as there are different
      /// indices used to access the same array, the MemoryDepChecker *is*
      /// queried and the dependence list is complete.
      Forward,
      /// Forward, but if vectorized, is likely to prevent store-to-load
      /// forwarding.
      ForwardButPreventsForwarding,
      /// Lexically backward.
      Backward,
      /// Backward, but the distance allows a vectorization factor of dependent
      /// on MinDepDistBytes.
      BackwardVectorizable,
      /// Same, but may prevent store-to-load forwarding.
      BackwardVectorizableButPreventsForwarding
    };

    /// String version of the types.
    LLVM_ABI static const char *DepName[];

    /// Index of the source of the dependence in the InstMap vector.
    unsigned Source;
    /// Index of the destination of the dependence in the InstMap vector.
    unsigned Destination;
    /// The type of the dependence.
    DepType Type;

    /// Construct a dependence from \p Source to \p Destination of \p Type.
    /// @param Source Index of the source access in InstMap.
    /// @param Destination Index of the destination access in InstMap.
    /// @param Type Kind of dependence between the accesses.
    Dependence(unsigned Source, unsigned Destination, DepType Type)
        : Source(Source), Destination(Destination), Type(Type) {}

    /// Return the source instruction of the dependence.
    /// @param DepChecker Checker whose instruction map resolves \p Source.
    /// @return The source instruction of the dependence.
    Instruction *getSource(const MemoryDepChecker &DepChecker) const;
    /// Return the destination instruction of the dependence.
    /// @param DepChecker Checker whose instruction map resolves \p Destination.
    /// @return The destination instruction of the dependence.
    Instruction *getDestination(const MemoryDepChecker &DepChecker) const;

    /// Dependence types that don't prevent vectorization.
    /// @param Type Dependence kind to classify for vectorization safety.
    /// @return The vectorization safety status for \p Type.
    LLVM_ABI static VectorizationSafetyStatus
    isSafeForVectorization(DepType Type);

    /// Lexically forward dependence.
    /// @return True if this is a lexically forward dependence.
    LLVM_ABI bool isForward() const;
    /// Lexically backward dependence.
    /// @return True if this is a lexically backward dependence.
    LLVM_ABI bool isBackward() const;

    /// May be a lexically backward dependence type (includes Unknown).
    /// @return True if this may be a lexically backward dependence.
    LLVM_ABI bool isPossiblyBackward() const;

    /// Print the dependence.  \p Instr is used to map the instruction
    /// indices to instructions.
    /// @param OS Stream to write to.
    /// @param Depth Indentation depth for nested printing.
    /// @param Instrs Instructions indexed by dependence source/destination.
    LLVM_ABI void print(raw_ostream &OS, unsigned Depth,
                        const SmallVectorImpl<Instruction *> &Instrs) const;
  };

  /// Construct a dependence checker for loop \p L.
  /// @param PSE Predicated SCEV used to analyze access functions.
  /// @param AC Assumption cache used to strengthen proofs.
  /// @param DT Dominator tree used for context-sensitive reasoning.
  /// @param L Innermost loop whose memory accesses are checked.
  /// @param SymbolicStrides Map from pointers to symbolic stride SCEVs.
  /// @param MaxTargetVectorWidthInBits Max vector register width times two.
  /// @param LoopGuards Cached loop guards for \p L.
  MemoryDepChecker(PredicatedScalarEvolution &PSE, AssumptionCache *AC,
                   DominatorTree *DT, const Loop *L,
                   const DenseMap<Value *, const SCEV *> &SymbolicStrides,
                   unsigned MaxTargetVectorWidthInBits,
                   std::optional<ScalarEvolution::LoopGuards> &LoopGuards)
      : PSE(PSE), AC(AC), DT(DT), InnermostLoop(L),
        SymbolicStrides(SymbolicStrides),
        MaxTargetVectorWidthInBits(MaxTargetVectorWidthInBits),
        LoopGuards(LoopGuards) {}

  /// Register the location (instructions are given increasing numbers)
  /// of a write access.
  /// @param SI Store instruction to register as a write access.
  LLVM_ABI void addAccess(StoreInst *SI);

  /// Register the location (instructions are given increasing numbers)
  /// of a write access.
  /// @param LI Load instruction to register as a read access.
  LLVM_ABI void addAccess(LoadInst *LI);

  /// Check whether the dependencies between the accesses are safe, and records
  /// the dependence information in Dependences if so.
  ///
  /// Only checks sets with elements in \p CheckDeps.
  /// @param AccessSets Equivalence classes of potentially dependent accesses.
  /// @param CheckDeps Accesses whose dependence sets must be checked.
  /// @return True if the checked dependencies are safe for vectorization.
  LLVM_ABI bool areDepsSafe(const DepCandidates &AccessSets,
                            ArrayRef<MemAccessInfo> CheckDeps);

  /// No memory dependence was encountered that would inhibit
  /// vectorization.
  /// @return True if no dependence inhibits vectorization.
  bool isSafeForVectorization() const {
    return Status == VectorizationSafetyStatus::Safe;
  }

  /// Return true if the number of elements that are safe to operate on
  /// simultaneously is not bounded.
  /// @return True if the safe element count is unbounded.
  bool isSafeForAnyVectorWidth() const {
    return MaxSafeVectorWidthInBits == UINT_MAX;
  }

  /// Return the number of elements that are safe to operate on
  /// simultaneously, multiplied by the size of the element in bits.
  /// @return The max safe width in bits (elements times element size).
  uint64_t getMaxSafeVectorWidthInBits() const {
    return MaxSafeVectorWidthInBits;
  }

  /// Return true if there are no store-load forwarding dependencies.
  /// @return True if there are no store-load forwarding dependencies.
  bool isSafeForAnyStoreLoadForwardDistances() const {
    return MaxStoreLoadForwardSafeDistanceInBits ==
           std::numeric_limits<uint64_t>::max();
  }

  /// Return safe power-of-2 number of elements, which do not prevent store-load
  /// forwarding, multiplied by the size of the elements in bits.
  /// @return The store-load forwarding safe distance in bits.
  uint64_t getStoreLoadForwardSafeDistanceInBits() const {
    assert(!isSafeForAnyStoreLoadForwardDistances() &&
           "Expected the distance, that prevent store-load forwarding, to be "
           "set.");
    return MaxStoreLoadForwardSafeDistanceInBits;
  }

  /// In same cases when the dependency check fails we can still
  /// vectorize the loop with a dynamic array access check.
  /// @return True if vectorization may succeed with runtime checks.
  bool shouldRetryWithRuntimeChecks() const {
    return ShouldRetryWithRuntimeChecks &&
           Status == VectorizationSafetyStatus::PossiblySafeWithRtChecks;
  }

  /// Returns the memory dependences.  If null is returned we exceeded
  /// the MaxDependences threshold and this information is not
  /// available.
  /// @return The dependence list, or null if the MaxDependences threshold was
  /// exceeded.
  const SmallVectorImpl<Dependence> *getDependences() const {
    return RecordDependences ? &Dependences : nullptr;
  }

  /// Clear the recorded dependence list.
  void clearDependences() { Dependences.clear(); }

  /// The vector of memory access instructions.  The indices are used as
  /// instruction identifiers in the Dependence class.
  /// @return The memory access instructions in program order.
  const SmallVectorImpl<Instruction *> &getMemoryInstructions() const {
    return InstMap;
  }

  /// Generate a mapping between the memory instructions and their
  /// indices according to program order.
  /// @return A map from memory instructions to their program-order indices.
  DenseMap<Instruction *, unsigned> generateInstructionOrderMap() const {
    DenseMap<Instruction *, unsigned> OrderMap;

    for (unsigned I = 0; I < InstMap.size(); ++I)
      OrderMap[InstMap[I]] = I;

    return OrderMap;
  }

  /// Find the set of instructions that read or write via \p Ptr.
  /// @param Ptr Pointer value whose accesses are queried.
  /// @param isWrite When true, collect stores; otherwise collect loads.
  /// @return The instructions that access \p Ptr as specified by \p isWrite.
  LLVM_ABI SmallVector<Instruction *, 4>
  getInstructionsForAccess(Value *Ptr, bool isWrite) const;

  /// Return the program order indices for the access location (Ptr, IsWrite).
  /// Returns an empty ArrayRef if there are no accesses for the location.
  /// @param Ptr Pointer value of the access location.
  /// @param IsWrite Whether the location is a write access.
  /// @return Program-order indices for the access, or empty if none.
  ArrayRef<unsigned> getOrderForAccess(Value *Ptr, bool IsWrite) const {
    auto I = Accesses.find({Ptr, IsWrite});
    if (I != Accesses.end())
      return I->second;
    return {};
  }

  /// Return the innermost loop being checked.
  /// @return The innermost loop being checked.
  const Loop *getInnermostLoop() const { return InnermostLoop; }

  /// Return the cache of expanded pointer start/end bounds.
  /// @return The cache of expanded pointer start/end bounds.
  DenseMap<std::pair<const SCEV *, const SCEV *>,
           std::pair<const SCEV *, const SCEV *>> &
  getPointerBounds() {
    return PointerBounds;
  }

  /// Return the dominator tree used by this checker.
  /// @return The dominator tree used by this checker.
  DominatorTree *getDT() const {
    assert(DT && "requested DT, but it is not available");
    return DT;
  }
  /// Return the assumption cache used by this checker.
  /// @return The assumption cache used by this checker.
  AssumptionCache *getAC() const {
    assert(AC && "requested AC, but it is not available");
    return AC;
  }

private:
  /// A wrapper around ScalarEvolution, used to add runtime SCEV checks, and
  /// applies dynamic knowledge to simplify SCEV expressions and convert them
  /// to a more usable form. We need this in case assumptions about SCEV
  /// expressions need to be made in order to avoid unknown dependences. For
  /// example we might assume a unit stride for a pointer in order to prove
  /// that a memory access is strided and doesn't wrap.
  PredicatedScalarEvolution &PSE;

  AssumptionCache *AC;
  DominatorTree *DT;

  const Loop *InnermostLoop;

  /// Reference to map of pointer values to
  /// their stride symbols, if they have a symbolic stride.
  const DenseMap<Value *, const SCEV *> &SymbolicStrides;

  /// Maps access locations (ptr, read/write) to program order.
  DenseMap<MemAccessInfo, std::vector<unsigned> > Accesses;

  /// Memory access instructions in program order.
  SmallVector<Instruction *, 16> InstMap;

  /// The program order index to be used for the next instruction.
  unsigned AccessIdx = 0;

  /// The smallest dependence distance in bytes in the loop. This may not be
  /// the same as the maximum number of bytes that are safe to operate on
  /// simultaneously.
  uint64_t MinDepDistBytes = 0;

  /// Number of elements (from consecutive iterations) that are safe to
  /// operate on simultaneously, multiplied by the size of the element in bits.
  /// The size of the element is taken from the memory access that is most
  /// restrictive.
  uint64_t MaxSafeVectorWidthInBits = -1U;

  /// Maximum power-of-2 number of elements, which do not prevent store-load
  /// forwarding, multiplied by the size of the elements in bits.
  uint64_t MaxStoreLoadForwardSafeDistanceInBits =
      std::numeric_limits<uint64_t>::max();

  /// Whether we should try to vectorize the loop with runtime checks, if the
  /// dependencies are not safe.
  bool ShouldRetryWithRuntimeChecks = false;

  /// Result of the dependence checks, indicating whether the checked
  /// dependences are safe for vectorization, require RT checks or are known to
  /// be unsafe.
  VectorizationSafetyStatus Status = VectorizationSafetyStatus::Safe;

  //// True if Dependences reflects the dependences in the
  //// loop.  If false we exceeded MaxDependences and
  //// Dependences is invalid.
  bool RecordDependences = true;

  /// Memory dependences collected during the analysis.  Only valid if
  /// RecordDependences is true.
  SmallVector<Dependence, 8> Dependences;

  /// The maximum width of a target's vector registers multiplied by 2 to also
  /// roughly account for additional interleaving. Is used to decide if a
  /// backwards dependence with non-constant stride should be classified as
  /// backwards-vectorizable or unknown (triggering a runtime check).
  unsigned MaxTargetVectorWidthInBits = 0;

  /// Mapping of SCEV expressions to their expanded pointer bounds (pair of
  /// start and end pointer expressions).
  DenseMap<std::pair<const SCEV *, const SCEV *>,
           std::pair<const SCEV *, const SCEV *>>
      PointerBounds;

  /// Cache for the loop guards of InnermostLoop.
  std::optional<ScalarEvolution::LoopGuards> &LoopGuards;

  /// Check whether there is a plausible dependence between the two
  /// accesses.
  ///
  /// Access \p A must happen before \p B in program order. The two indices
  /// identify the index into the program order map.
  ///
  /// This function checks  whether there is a plausible dependence (or the
  /// absence of such can't be proved) between the two accesses. If there is a
  /// plausible dependence but the dependence distance is bigger than one
  /// element access it records this distance in \p MinDepDistBytes (if this
  /// distance is smaller than any other distance encountered so far).
  /// Otherwise, this function returns true signaling a possible dependence.
  Dependence::DepType isDependent(const MemAccessInfo &A, unsigned AIdx,
                                  const MemAccessInfo &B, unsigned BIdx);

  /// Check whether the data dependence could prevent store-load
  /// forwarding.
  ///
  /// \return false if we shouldn't vectorize at all or avoid larger
  /// vectorization factors by limiting MinDepDistBytes.
  bool couldPreventStoreLoadForward(uint64_t Distance, uint64_t TypeByteSize,
                                    unsigned CommonStride = 0);

  /// Updates the current safety status with \p S. We can go from Safe to
  /// either PossiblySafeWithRtChecks or Unsafe and from
  /// PossiblySafeWithRtChecks to Unsafe.
  void mergeInStatus(VectorizationSafetyStatus S);

  struct DepDistanceStrideAndSizeInfo {
    const SCEV *Dist;

    /// Strides here are scaled; i.e. in bytes, taking the size of the
    /// underlying type into account.
    uint64_t MaxStride;
    std::optional<uint64_t> CommonStride;

    /// TypeByteSize is either the common store size of both accesses, or 0 when
    /// store sizes mismatch.
    uint64_t TypeByteSize;

    bool AIsWrite;
    bool BIsWrite;

    DepDistanceStrideAndSizeInfo(const SCEV *Dist, uint64_t MaxStride,
                                 std::optional<uint64_t> CommonStride,
                                 uint64_t TypeByteSize, bool AIsWrite,
                                 bool BIsWrite)
        : Dist(Dist), MaxStride(MaxStride), CommonStride(CommonStride),
          TypeByteSize(TypeByteSize), AIsWrite(AIsWrite), BIsWrite(BIsWrite) {}
  };

  /// Get the dependence distance, strides, type size and whether it is a write
  /// for the dependence between A and B. Returns a DepType, if we can prove
  /// there's no dependence or the analysis fails. Outlined to lambda to limit
  /// he scope of various temporary variables, like A/BPtr, StrideA/BPtr and
  /// others. Returns either the dependence result, if it could already be
  /// determined, or a DepDistanceStrideAndSizeInfo struct, noting that
  /// TypeByteSize could be 0 when store sizes mismatch, and this should be
  /// checked in the caller.
  std::variant<Dependence::DepType, DepDistanceStrideAndSizeInfo>
  getDependenceDistanceStrideAndSize(const MemAccessInfo &A, Instruction *AInst,
                                     const MemAccessInfo &B,
                                     Instruction *BInst);

  // Return true if we can prove that \p Sink only accesses memory after \p
  // Src's end or vice versa.
  bool areAccessesCompletelyBeforeOrAfter(const SCEV *Src, Type *SrcTy,
                                          const SCEV *Sink, Type *SinkTy);
};

class RuntimePointerChecking;
/// A grouping of pointers. A single memcheck is required between
/// two groups.
struct RuntimeCheckingPtrGroup {
  /// Create a new pointer checking group containing a single
  /// pointer, with index \p Index in RtCheck.
  /// @param Index Index of the initial pointer in \p RtCheck.
  /// @param RtCheck Runtime checking info that owns the pointer.
  LLVM_ABI RuntimeCheckingPtrGroup(unsigned Index,
                                   const RuntimePointerChecking &RtCheck);

  /// Tries to add the pointer at \p Index to this checking group.
  ///
  /// We can only add a pointer to a checking group if we will still be able to
  /// get the upper and lower bounds of the check. Returns true in case of
  /// success, false otherwise.
  /// @param Index Index of the pointer in \p RtCheck to add.
  /// @param RtCheck Runtime checking info that owns the pointer.
  /// @return True if the pointer was added successfully.
  LLVM_ABI bool addPointer(unsigned Index,
                           const RuntimePointerChecking &RtCheck);
  /// Tries to add a pointer with explicit bounds to this checking group.
  /// @param Index Index of the pointer being added.
  /// @param Start Lower-bound SCEV of the pointer region.
  /// @param End Upper-bound SCEV of the pointer region.
  /// @param AS Address space of the pointer.
  /// @param NeedsFreeze Whether the expanded pointer must be frozen.
  /// @param SE ScalarEvolution used to compare and combine bounds.
  /// @return True if the pointer was added successfully.
  LLVM_ABI bool addPointer(unsigned Index, const SCEV *Start, const SCEV *End,
                           unsigned AS, bool NeedsFreeze, ScalarEvolution &SE);

  /// The SCEV expression which represents the upper bound of all the
  /// pointers in this group.
  const SCEV *High;
  /// The SCEV expression which represents the lower bound of all the
  /// pointers in this group.
  const SCEV *Low;
  /// Indices of all the pointers that constitute this grouping.
  SmallVector<unsigned, 2> Members;
  /// Address space of the involved pointers.
  unsigned AddressSpace;
  /// Whether the pointer needs to be frozen after expansion, e.g. because it
  /// may be poison outside the loop.
  bool NeedsFreeze = false;
};

/// A memcheck which made up of a pair of grouped pointers.
using RuntimePointerCheck =
    std::pair<const RuntimeCheckingPtrGroup *, const RuntimeCheckingPtrGroup *>;

/// Diff-check operands for proving independence via pointer differences.
struct PointerDiffInfo {
  /// Start SCEV of the source access region.
  const SCEV *SrcStart;
  /// Start SCEV of the sink access region.
  const SCEV *SinkStart;
  /// Access size in bytes used by the diff check.
  unsigned AccessSize;
  /// Whether either pointer expression needs freezing after expansion.
  bool NeedsFreeze;

  /// Construct a pointer-difference check description.
  /// @param SrcStart Start SCEV of the source access.
  /// @param SinkStart Start SCEV of the sink access.
  /// @param AccessSize Access size in bytes.
  /// @param NeedsFreeze Whether expanded pointers must be frozen.
  PointerDiffInfo(const SCEV *SrcStart, const SCEV *SinkStart,
                  unsigned AccessSize, bool NeedsFreeze)
      : SrcStart(SrcStart), SinkStart(SinkStart), AccessSize(AccessSize),
        NeedsFreeze(NeedsFreeze) {}
};

/// Holds information about the memory runtime legality checks to verify
/// that a group of pointers do not overlap.
class RuntimePointerChecking {
  friend struct RuntimeCheckingPtrGroup;

public:
  /// Information about one pointer that may need a runtime check.
  struct PointerInfo {
    /// Holds the pointer value that we need to check.
    TrackingVH<Value> PointerValue;
    /// Holds the smallest byte address accessed by the pointer throughout all
    /// iterations of the loop.
    const SCEV *Start;
    /// Holds the largest byte address accessed by the pointer throughout all
    /// iterations of the loop, plus 1.
    const SCEV *End;
    /// Holds the information if this pointer is used for writing to memory.
    bool IsWritePtr;
    /// Holds the id of the set of pointers that could be dependent because of a
    /// shared underlying object.
    unsigned DependencySetId;
    /// Holds the id of the disjoint alias set to which this pointer belongs.
    unsigned AliasSetId;
    /// SCEV for the access.
    const SCEV *Expr;
    /// True if the pointer expressions needs to be frozen after expansion.
    bool NeedsFreeze;

    /// Construct pointer checking info for one access.
    /// @param PointerValue Pointer value that may need checking.
    /// @param Start Smallest accessed byte address across iterations.
    /// @param End One past the largest accessed byte address.
    /// @param IsWritePtr Whether the pointer is used for a store.
    /// @param DependencySetId Id of the shared-object dependence set.
    /// @param AliasSetId Id of the disjoint alias set.
    /// @param Expr SCEV expression for the pointer access.
    /// @param NeedsFreeze Whether the expanded pointer must be frozen.
    PointerInfo(Value *PointerValue, const SCEV *Start, const SCEV *End,
                bool IsWritePtr, unsigned DependencySetId, unsigned AliasSetId,
                const SCEV *Expr, bool NeedsFreeze)
        : PointerValue(PointerValue), Start(Start), End(End),
          IsWritePtr(IsWritePtr), DependencySetId(DependencySetId),
          AliasSetId(AliasSetId), Expr(Expr), NeedsFreeze(NeedsFreeze) {}
  };

  /// Construct runtime pointer checking state.
  /// @param DC Dependence checker providing access and bound info.
  /// @param SE ScalarEvolution used to build pointer expressions.
  /// @param LoopGuards Cached loop guards used while computing bounds.
  RuntimePointerChecking(MemoryDepChecker &DC, ScalarEvolution *SE,
                         std::optional<ScalarEvolution::LoopGuards> &LoopGuards)
      : DC(DC), SE(SE), LoopGuards(LoopGuards) {}

  /// Reset the state of the pointer runtime information.
  void reset() {
    Need = false;
    CanUseDiffCheck = true;
    Pointers.clear();
    Checks.clear();
    DiffChecks.clear();
    CheckingGroups.clear();
  }

  /// Insert a pointer and calculate the start and end SCEVs.
  ///
  /// We need \p PSE in order to compute the SCEV expression of the pointer
  /// according to the assumptions that we've made during the analysis.
  /// The method might also version the pointer stride according to \p Strides,
  /// and add new predicates to \p PSE.
  /// @param Lp Loop in which the pointer is accessed.
  /// @param Ptr Pointer value being tracked.
  /// @param PtrExpr SCEV expression for \p Ptr.
  /// @param AccessTy Type of the memory access through \p Ptr.
  /// @param WritePtr Whether the pointer is used for a store.
  /// @param DepSetId Dependence-set id for shared underlying objects.
  /// @param ASId Alias-set id for this pointer.
  /// @param PSE Predicated SCEV used to compute and version the expression.
  /// @param NeedsFreeze Whether the expanded pointer must be frozen.
  LLVM_ABI void insert(Loop *Lp, Value *Ptr, const SCEV *PtrExpr,
                       Type *AccessTy, bool WritePtr, unsigned DepSetId,
                       unsigned ASId, PredicatedScalarEvolution &PSE,
                       bool NeedsFreeze);

  /// No run-time memory checking is necessary.
  /// @return True if no run-time memory checking is necessary.
  bool empty() const { return Pointers.empty(); }

  /// Generate the checks and store it.  This also performs the grouping
  /// of pointers to reduce the number of memchecks necessary.
  /// @param DepCands Dependence candidate sets used to group pointers.
  LLVM_ABI void generateChecks(MemoryDepChecker::DepCandidates &DepCands);

  /// Returns the checks that generateChecks created. They can be used to ensure
  /// no read/write accesses overlap across all loop iterations.
  /// @return The runtime pointer checks created by generateChecks.
  const SmallVectorImpl<RuntimePointerCheck> &getChecks() const {
    return Checks;
  }

  /// Return optional pointer-difference checks, if they can be used.
  ///
  /// Returns an optional list of (pointer-difference expressions, access size)
  /// pairs that can be used to prove that there are no vectorization-preventing
  /// dependencies at runtime. There are is a vectorization-preventing
  /// dependency if any pointer-difference is <u VF * InterleaveCount * access
  /// size. Returns std::nullopt if pointer-difference checks cannot be used.
  /// @return Pointer-difference checks, or std::nullopt if they cannot be used.
  std::optional<ArrayRef<PointerDiffInfo>> getDiffChecks() const {
    if (!CanUseDiffCheck)
      return std::nullopt;
    return {DiffChecks};
  }

  /// Decide if we need to add a check between two groups of pointers,
  /// according to needsChecking.
  /// @param M First pointer checking group.
  /// @param N Second pointer checking group.
  /// @return True if a check between \p M and \p N is required.
  LLVM_ABI bool needsChecking(const RuntimeCheckingPtrGroup &M,
                              const RuntimeCheckingPtrGroup &N) const;

  /// Returns the number of run-time checks required according to
  /// needsChecking.
  /// @return The number of run-time checks required.
  unsigned getNumberOfChecks() const { return Checks.size(); }

  /// Print the list run-time memory checks necessary.
  /// @param OS Stream to write to.
  /// @param Depth Indentation depth for nested printing.
  LLVM_ABI void print(raw_ostream &OS, unsigned Depth = 0) const;

  /// Print \p Checks.
  /// @param OS Stream to write to.
  /// @param Checks Runtime pointer checks to print.
  /// @param Depth Indentation depth for nested printing.
  LLVM_ABI void printChecks(raw_ostream &OS,
                            const SmallVectorImpl<RuntimePointerCheck> &Checks,
                            unsigned Depth = 0) const;

  /// This flag indicates if we need to add the runtime check.
  bool Need = false;

  /// Information about the pointers that may require checking.
  SmallVector<PointerInfo, 2> Pointers;

  /// Holds a partitioning of pointers into "check groups".
  SmallVector<RuntimeCheckingPtrGroup, 2> CheckingGroups;

  /// Check if pointers are in the same partition
  ///
  /// \p PtrToPartition contains the partition number for pointers (-1 if the
  /// pointer belongs to multiple partitions).
  /// @param PtrToPartition Partition number per pointer index (-1 if shared).
  /// @param PtrIdx1 First pointer index.
  /// @param PtrIdx2 Second pointer index.
  /// @return True if both pointers are in the same partition.
  LLVM_ABI static bool
  arePointersInSamePartition(const SmallVectorImpl<int> &PtrToPartition,
                             unsigned PtrIdx1, unsigned PtrIdx2);

  /// Decide whether we need to issue a run-time check for pointer at
  /// index \p I and \p J to prove their independence.
  /// @param I Index of the first pointer in Pointers.
  /// @param J Index of the second pointer in Pointers.
  /// @return True if a run-time check between pointers \p I and \p J is
  /// required.
  LLVM_ABI bool needsChecking(unsigned I, unsigned J) const;

  /// Return PointerInfo for pointer at index \p PtrIdx.
  /// @param PtrIdx Index into Pointers.
  /// @return The PointerInfo for the pointer at \p PtrIdx.
  const PointerInfo &getPointerInfo(unsigned PtrIdx) const {
    return Pointers[PtrIdx];
  }

  /// Return the ScalarEvolution analysis used for pointer checks.
  /// @return The ScalarEvolution analysis used for pointer checks.
  ScalarEvolution *getSE() const { return SE; }

private:
  /// Groups pointers such that a single memcheck is required
  /// between two different groups. This will clear the CheckingGroups vector
  /// and re-compute it.
  void groupChecks(MemoryDepChecker::DepCandidates &DepCands);

  /// Generate the checks and return them.
  SmallVector<RuntimePointerCheck, 4> generateChecks();

  /// Try to create add a new (pointer-difference, access size) pair to
  /// DiffCheck for checking groups \p CGI and \p CGJ. If pointer-difference
  /// checks cannot be used for the groups, set CanUseDiffCheck to false.
  bool tryToCreateDiffCheck(const RuntimeCheckingPtrGroup &CGI,
                            const RuntimeCheckingPtrGroup &CGJ);

  MemoryDepChecker &DC;

  /// Holds a pointer to the ScalarEvolution analysis.
  ScalarEvolution *SE;

  /// Cache for the loop guards of the loop.
  std::optional<ScalarEvolution::LoopGuards> &LoopGuards;

  /// Set of run-time checks required to establish independence of
  /// otherwise may-aliasing pointers in the loop.
  SmallVector<RuntimePointerCheck, 4> Checks;

  /// Flag indicating if pointer-difference checks can be used
  bool CanUseDiffCheck = true;

  /// A list of (pointer-difference, access size) pairs that can be used to
  /// prove that there are no vectorization-preventing dependencies.
  SmallVector<PointerDiffInfo> DiffChecks;
};

/// Drive the analysis of memory accesses in the loop
///
/// This class is responsible for analyzing the memory accesses of a loop.  It
/// collects the accesses and then its main helper the AccessAnalysis class
/// finds and categorizes the dependences in buildDependenceSets.
///
/// For memory dependences that can be analyzed at compile time, it determines
/// whether the dependence is part of cycle inhibiting vectorization.  This work
/// is delegated to the MemoryDepChecker class.
///
/// For memory dependences that cannot be determined at compile time, it
/// generates run-time checks to prove independence.  This is done by
/// AccessAnalysis::canCheckPtrAtRT and the checks are maintained by the
/// RuntimePointerCheck class. \p AllowPartial determines whether partial checks
/// are generated when not all pointers could be analyzed.
///
/// If pointers can wrap or can't be expressed as affine AddRec expressions by
/// ScalarEvolution, we will generate run-time checks by emitting a
/// SCEVUnionPredicate.
///
/// Checks for both memory dependences and the SCEV predicates contained in the
/// PSE must be emitted in order for the results of this analysis to be valid.
class LoopAccessInfo {
public:
  /// Analyze memory accesses in loop \p L.
  /// @param L Loop whose memory accesses are analyzed.
  /// @param SE ScalarEvolution used for access functions and predicates.
  /// @param TTI Target transform info for cost and legality queries.
  /// @param TLI Target library info used during analysis.
  /// @param AA Alias analysis used to classify memory accesses.
  /// @param DT Dominator tree used for predication and context.
  /// @param LI Loop info for the function containing \p L.
  /// @param AC Assumption cache used to strengthen proofs.
  /// @param AllowPartial When true, keep partial runtime checks on failure.
  LLVM_ABI LoopAccessInfo(Loop *L, ScalarEvolution *SE,
                          const TargetTransformInfo *TTI,
                          const TargetLibraryInfo *TLI, AAResults *AA,
                          DominatorTree *DT, LoopInfo *LI, AssumptionCache *AC,
                          bool AllowPartial = false);

  /// Return true if memory accesses can be analyzed without dependence cycles.
  ///
  /// Note that for dependences between loads & stores with uniform addresses,
  /// hasStoreStoreDependenceInvolvingLoopInvariantAddress and
  /// hasLoadStoreDependenceInvolvingLoopInvariantAddress also need to be
  /// checked.
  /// @return True if memory accesses can be analyzed without dependence cycles.
  bool canVectorizeMemory() const { return CanVecMem; }

  /// Return true if the loop contains a convergent operation.
  ///
  /// There may still be reported runtime pointer checks that would be
  /// required, but it is not legal to insert them.
  /// @return True if the loop contains a convergent operation.
  bool hasConvergentOp() const { return HasConvergentOp; }

  /// Return true if, when runtime pointer checking does not have complete
  /// results, it instead has partial results for those memory accesses that
  /// could be analyzed.
  /// @return True if partial runtime pointer checking results are kept.
  bool hasAllowPartial() const { return AllowPartial; }

  /// Return the runtime pointer checking info for this loop.
  /// @return The runtime pointer checking info for this loop.
  const RuntimePointerChecking *getRuntimePointerChecking() const {
    return PtrRtChecking.get();
  }

  /// Number of memchecks required to prove independence of otherwise
  /// may-alias pointers.
  /// @return The number of memchecks required to prove independence.
  unsigned getNumRuntimePointerChecks() const {
    return PtrRtChecking->getNumberOfChecks();
  }

  /// Return true if the block BB needs to be predicated in order for the loop
  /// to be vectorized.
  /// \pre \p TheLoop has a unique latch.
  /// @param BB Basic block to test for required predication.
  /// @param TheLoop Loop that contains \p BB.
  /// @param DT Dominator tree used to reason about control dependence.
  /// @return True if \p BB needs to be predicated for vectorization.
  LLVM_ABI static bool blockNeedsPredication(const BasicBlock *BB,
                                             const Loop *TheLoop,
                                             const DominatorTree *DT);

  /// Returns true if value \p V is loop invariant.
  /// @param V Value to test for loop invariance.
  /// @return True if \p V is loop invariant.
  LLVM_ABI bool isInvariant(Value *V) const;

  /// Return the number of store instructions in the loop.
  /// @return The number of store instructions in the loop.
  unsigned getNumStores() const { return NumStores; }
  /// Return the number of load instructions in the loop.
  /// @return The number of load instructions in the loop.
  unsigned getNumLoads() const { return NumLoads;}

  /// The diagnostics report generated for the analysis.  E.g. why we
  /// couldn't analyze the loop.
  /// @return The diagnostics report for the analysis, or null if none.
  const OptimizationRemarkAnalysis *getReport() const { return Report.get(); }

  /// the Memory Dependence Checker which can determine the
  /// loop-independent and loop-carried dependences between memory accesses.
  /// @return The memory dependence checker for this loop.
  const MemoryDepChecker &getDepChecker() const { return *DepChecker; }

  /// Return the list of instructions that use \p Ptr to read or write
  /// memory.
  /// @param Ptr Pointer value whose users are queried.
  /// @param isWrite When true, collect stores; otherwise collect loads.
  /// @return The instructions that access \p Ptr as specified by \p isWrite.
  SmallVector<Instruction *, 4> getInstructionsForAccess(Value *Ptr,
                                                         bool isWrite) const {
    return DepChecker->getInstructionsForAccess(Ptr, isWrite);
  }

  /// If an access has a symbolic strides, this maps the pointer value to
  /// the stride symbol.
  /// @return The map from pointers with symbolic strides to their stride SCEVs.
  const DenseMap<Value *, const SCEV *> &getSymbolicStrides() const {
    return SymbolicStrides;
  }

  /// Print the information about the memory accesses in the loop.
  /// @param OS Stream to write to.
  /// @param Depth Indentation depth for nested printing.
  LLVM_ABI void print(raw_ostream &OS, unsigned Depth = 0) const;

  /// Return true if the loop has memory dependence involving two stores to an
  /// invariant address, else return false.
  /// @return True if there is a store-store dependence on an invariant address.
  bool hasStoreStoreDependenceInvolvingLoopInvariantAddress() const {
    return HasStoreStoreDependenceInvolvingLoopInvariantAddress;
  }

  /// Return true if the loop has memory dependence involving a load and a store
  /// to an invariant address, else return false.
  /// @return True if there is a load-store dependence on an invariant address.
  bool hasLoadStoreDependenceInvolvingLoopInvariantAddress() const {
    return HasLoadStoreDependenceInvolvingLoopInvariantAddress;
  }

  /// Return the list of stores to invariant addresses.
  /// @return The list of stores to invariant addresses.
  ArrayRef<StoreInst *> getStoresToInvariantAddresses() const {
    return StoresToInvariantAddresses;
  }

  /// Return the predicated ScalarEvolution used by this analysis.
  ///
  /// Used to add runtime SCEV checks. Simplifies SCEV expressions and converts
  /// them to a more usable form. All SCEV expressions during the analysis
  /// should be re-written (and therefore simplified) according to PSE.
  /// A user of LoopAccessAnalysis will need to emit the runtime checks
  /// associated with this predicate.
  /// @return The predicated ScalarEvolution used by this analysis.
  const PredicatedScalarEvolution &getPSE() const { return *PSE; }

private:
  /// Analyze the loop. Returns true if all memory access in the loop can be
  /// vectorized.
  bool analyzeLoop(AAResults *AA, const LoopInfo *LI,
                   const TargetLibraryInfo *TLI, DominatorTree *DT);

  /// Check if the structure of the loop allows it to be analyzed by this
  /// pass.
  bool canAnalyzeLoop();

  /// Save the analysis remark.
  ///
  /// LAA does not directly emits the remarks.  Instead it stores it which the
  /// client can retrieve and presents as its own analysis
  /// (e.g. -Rpass-analysis=loop-vectorize).
  OptimizationRemarkAnalysis &
  recordAnalysis(StringRef RemarkName, const Instruction *Instr = nullptr);

  /// Collect memory access with loop invariant strides.
  ///
  /// Looks for accesses like "a[i * StrideA]" where "StrideA" is loop
  /// invariant.
  void collectStridedAccess(Value *LoadOrStoreInst);

  // Emits the first unsafe memory dependence in a loop.
  // Emits nothing if there are no unsafe dependences
  // or if the dependences were not recorded.
  void emitUnsafeDependenceRemark();

  std::unique_ptr<PredicatedScalarEvolution> PSE;

  /// We need to check that all of the pointers in this list are disjoint
  /// at runtime. Using std::unique_ptr to make using move ctor simpler.
  /// If AllowPartial is true then this list may contain only partial
  /// information when we've failed to analyze all the memory accesses in the
  /// loop, in which case HasCompletePtrRtChecking will be false.
  std::unique_ptr<RuntimePointerChecking> PtrRtChecking;

  /// The Memory Dependence Checker which can determine the
  /// loop-independent and loop-carried dependences between memory accesses.
  /// This will be empty if we've failed to analyze all the memory access in the
  /// loop (i.e. CanVecMem is false).
  std::unique_ptr<MemoryDepChecker> DepChecker;

  Loop *TheLoop;

  /// Cache for the loop guards of TheLoop.
  std::optional<ScalarEvolution::LoopGuards> LoopGuards;

  /// Determines whether we should generate partial runtime checks when not all
  /// memory accesses could be analyzed.
  bool AllowPartial;

  unsigned NumLoads = 0;
  unsigned NumStores = 0;

  /// Cache the result of analyzeLoop.
  bool CanVecMem = false;
  bool HasConvergentOp = false;
  bool HasCompletePtrRtChecking = false;

  /// Indicator that there are two non vectorizable stores to the same uniform
  /// address.
  bool HasStoreStoreDependenceInvolvingLoopInvariantAddress = false;
  /// Indicator that there is non vectorizable load and store to the same
  /// uniform address.
  bool HasLoadStoreDependenceInvolvingLoopInvariantAddress = false;

  /// List of stores to invariant addresses.
  SmallVector<StoreInst *> StoresToInvariantAddresses;

  /// The diagnostics report generated for the analysis.  E.g. why we
  /// couldn't analyze the loop.
  std::unique_ptr<OptimizationRemarkAnalysis> Report;

  /// If an access has a symbolic strides, this maps the pointer value to
  /// the stride symbol.
  DenseMap<Value *, const SCEV *> SymbolicStrides;
};

/// Return the SCEV corresponding to a pointer with the symbolic stride
/// replaced with constant one, assuming the SCEV predicate associated with
/// \p PSE is true.
///
/// If necessary this method will version the stride of the pointer according
/// to \p PtrToStride and therefore add further predicates to \p PSE.
///
/// \p PtrToStride provides the mapping between the pointer value and its
/// stride as collected by LoopVectorizationLegality::collectStridedAccess.
/// @param PSE Predicated SCEV that may gain stride versioning predicates.
/// @param PtrToStride Map from pointer values to their symbolic strides.
/// @param Ptr Pointer whose SCEV should use a unit symbolic stride.
/// @return The SCEV for \p Ptr with symbolic stride replaced by one.
LLVM_ABI const SCEV *
replaceSymbolicStrideSCEV(PredicatedScalarEvolution &PSE,
                          const DenseMap<Value *, const SCEV *> &PtrToStride,
                          Value *Ptr);

/// Return the constant step of affine AddRec \p AR in units of \p AccessTy.
///
/// If \p AR is an affine AddRec for \p Lp with a constant step, return the
/// step in units of \p AccessTy's allocation size. Returns std::nullopt if the
/// step is not constant, does not divide the access size, or \p AccessTy is a
/// scalable vector. \p Ptr is only used for debug output and may be null.
/// @param AR Candidate affine add-recurrence for the pointer.
/// @param Lp Loop that \p AR must be an AddRec for.
/// @param AccessTy Access type whose allocation size scales the step.
/// @param Ptr Optional pointer value used only for debug output.
/// @param PSE Predicated SCEV used to analyze \p AR.
/// @return The constant step in units of \p AccessTy, or std::nullopt.
LLVM_ABI std::optional<int64_t>
getStrideFromAddRec(const SCEVAddRecExpr *AR, const Loop *Lp, Type *AccessTy,
                    Value *Ptr, PredicatedScalarEvolution &PSE);

/// If the pointer has a constant stride return it in units of the access type
/// size. If the pointer is loop-invariant, return 0. Otherwise return
/// std::nullopt.
///
/// Ensure that it does not wrap in the address space, assuming the predicate
/// associated with \p PSE is true.
///
/// If necessary this method will version the stride of the pointer according
/// to \p PtrToStride and therefore add further predicates to \p PSE.
///
/// If \p Predicates is non-null, add no-wrap SCEV predicates if needed.
///
/// Note that the analysis results are defined if-and-only-if the original
/// memory access was defined.  If that access was dead, or UB, then the
/// result of this function is undefined.
/// @param PSE Predicated SCEV used to analyze and version the pointer.
/// @param AccessTy Type of the memory access through \p Ptr.
/// @param Ptr Pointer whose stride is queried.
/// @param Lp Loop in which \p Ptr is accessed.
/// @param DT Dominator tree used for context-sensitive reasoning.
/// @param StridesMap Map from pointers to symbolic stride SCEVs.
/// @param ShouldCheckWrap When true, ensure the pointer does not wrap.
/// @param Predicates Optional vector that collects required SCEV predicates.
/// @return The constant stride in access-type units, 0 if invariant, or
/// std::nullopt.
LLVM_ABI std::optional<int64_t>
getPtrStride(PredicatedScalarEvolution &PSE, Type *AccessTy, Value *Ptr,
             const Loop *Lp, const DominatorTree &DT,
             const DenseMap<Value *, const SCEV *> &StridesMap =
                 DenseMap<Value *, const SCEV *>(),
             bool ShouldCheckWrap = true,
             SmallVectorImpl<const SCEVPredicate *> *Predicates = nullptr);

/// Overload of getPtrStride that stores no-wrap predicates in \p PSE.
///
/// The \p Assume parameter indicates whether such additional run-time
/// assumptions are allowed.
/// @param PSE Predicated SCEV that may receive no-wrap predicates.
/// @param AccessTy Type of the memory access through \p Ptr.
/// @param Ptr Pointer whose stride is queried.
/// @param Lp Loop in which \p Ptr is accessed.
/// @param DT Dominator tree used for context-sensitive reasoning.
/// @param StridesMap Map from pointers to symbolic stride SCEVs.
/// @param Assume When true, allow adding run-time assumptions to \p PSE.
/// @param ShouldCheckWrap When true, ensure the pointer does not wrap.
/// @return The constant stride in access-type units, 0 if invariant, or
/// std::nullopt.
LLVM_ABI std::optional<int64_t>
getPtrStride(PredicatedScalarEvolution &PSE, Type *AccessTy, Value *Ptr,
             const Loop *Lp, const DominatorTree &DT,
             const DenseMap<Value *, const SCEV *> &StridesMap, bool Assume,
             bool ShouldCheckWrap = true);

/// Returns the distance between compatible pointers \p PtrA and \p PtrB.
///
/// Returns the distance iff they are compatible and it is possible to calculate
/// the distance between them. This is a simple API that does not depend on the
/// analysis pass.
/// @param ElemTyA Element type of the access through \p PtrA.
/// @param PtrA First pointer value.
/// @param ElemTyB Element type of the access through \p PtrB.
/// @param PtrB Second pointer value.
/// @param DL Data layout used to compute type sizes and offsets.
/// @param SE ScalarEvolution used to analyze the pointers.
/// @param StrictCheck Ensure that the calculated distance matches the
/// type-based one after all the bitcasts removal in the provided pointers.
/// @param CheckType When true, require the element types to match.
/// @return The distance between the pointers, or std::nullopt if not
/// computable.
LLVM_ABI std::optional<int64_t>
getPointersDiff(Type *ElemTyA, Value *PtrA, Type *ElemTyB, Value *PtrB,
                const DataLayout &DL, ScalarEvolution &SE,
                bool StrictCheck = false, bool CheckType = true);

/// Attempt to sort the pointers in \p VL and return the sorted indices
/// in \p SortedIndices, if reordering is required.
///
/// Returns 'true' if sorting is legal, otherwise returns 'false'.
///
/// For example, for a given \p VL of memory accesses in program order, a[i+4],
/// a[i+0], a[i+1] and a[i+7], this function will sort the \p VL and save the
/// sorted indices in \p SortedIndices as a[i+0], a[i+1], a[i+4], a[i+7] and
/// saves the mask for actual memory accesses in program order in
/// \p SortedIndices as <1,2,0,3>
/// @param VL Pointer values of the memory accesses in program order.
/// @param ElemTy Element type of the accesses in \p VL.
/// @param DL Data layout used to compute type sizes and offsets.
/// @param SE ScalarEvolution used to compare pointer distances.
/// @param SortedIndices Filled with the permutation that sorts \p VL.
/// @return True if sorting is legal; false otherwise.
LLVM_ABI bool sortPtrAccesses(ArrayRef<Value *> VL, Type *ElemTy,
                              const DataLayout &DL, ScalarEvolution &SE,
                              SmallVectorImpl<unsigned> &SortedIndices);

/// Returns true if the memory operations \p A and \p B are consecutive.
/// This is a simple API that does not depend on the analysis pass.
/// @param A First memory operation or pointer value.
/// @param B Second memory operation or pointer value.
/// @param DL Data layout used to compute type sizes and offsets.
/// @param SE ScalarEvolution used to compare the addresses.
/// @param CheckType When true, require the accessed types to match.
/// @return True if the memory operations are consecutive.
LLVM_ABI bool isConsecutiveAccess(Value *A, Value *B, const DataLayout &DL,
                                  ScalarEvolution &SE, bool CheckType = true);

/// Calculate Start and End points of memory access using exact backedge taken
/// count \p BTC if computable or maximum backedge taken count \p MaxBTC
/// otherwise.
///
/// Let's assume A is the first access and B is a memory access on N-th loop
/// iteration. Then B is calculated as:
///   B = A + Step*N .
/// Step value may be positive or negative.
/// N is a calculated back-edge taken count:
///     N = (TripCount > 0) ? RoundDown(TripCount -1 , VF) : 0
/// Start and End points are calculated in the following way:
/// Start = UMIN(A, B) ; End = UMAX(A, B) + SizeOfElt,
/// where SizeOfElt is the size of single memory access in bytes.
///
/// There is no conflict when the intervals are disjoint:
/// NoConflict = (P2.Start >= P1.End) || (P1.Start >= P2.End)
/// @param Lp Loop in which the access executes.
/// @param PtrExpr SCEV for the pointer being accessed.
/// @param AccessTy Type of the memory access.
/// @param BTC Exact backedge-taken count when available.
/// @param MaxBTC Maximum backedge-taken count used as a fallback.
/// @param SE ScalarEvolution used to compute start and end expressions.
/// @param PointerBounds Optional cache of previously computed bounds.
/// @param DT Dominator tree used for context-sensitive reasoning.
/// @param AC Assumption cache used to strengthen proofs.
/// @param LoopGuards Cached loop guards used while computing bounds.
/// @return The start and end SCEVs of the accessed memory region.
LLVM_ABI std::pair<const SCEV *, const SCEV *> getStartAndEndForAccess(
    const Loop *Lp, const SCEV *PtrExpr, Type *AccessTy, const SCEV *BTC,
    const SCEV *MaxBTC, ScalarEvolution *SE,
    DenseMap<std::pair<const SCEV *, const SCEV *>,
             std::pair<const SCEV *, const SCEV *>> *PointerBounds,
    DominatorTree *DT, AssumptionCache *AC,
    std::optional<ScalarEvolution::LoopGuards> &LoopGuards);
/// Overload of getStartAndEndForAccess taking the element size as a SCEV.
/// @param Lp Loop in which the access executes.
/// @param PtrExpr SCEV for the pointer being accessed.
/// @param EltSizeSCEV SCEV for the access size in bytes.
/// @param BTC Exact backedge-taken count when available.
/// @param MaxBTC Maximum backedge-taken count used as a fallback.
/// @param SE ScalarEvolution used to compute start and end expressions.
/// @param PointerBounds Optional cache of previously computed bounds.
/// @param DT Dominator tree used for context-sensitive reasoning.
/// @param AC Assumption cache used to strengthen proofs.
/// @param LoopGuards Cached loop guards used while computing bounds.
/// @return The start and end SCEVs of the accessed memory region.
LLVM_ABI std::pair<const SCEV *, const SCEV *> getStartAndEndForAccess(
    const Loop *Lp, const SCEV *PtrExpr, const SCEV *EltSizeSCEV,
    const SCEV *BTC, const SCEV *MaxBTC, ScalarEvolution *SE,
    DenseMap<std::pair<const SCEV *, const SCEV *>,
             std::pair<const SCEV *, const SCEV *>> *PointerBounds,
    DominatorTree *DT, AssumptionCache *AC,
    std::optional<ScalarEvolution::LoopGuards> &LoopGuards);

/// Manages and caches LoopAccessInfo results for loops in a function.
class LoopAccessInfoManager {
  /// The cache.
  DenseMap<Loop *, std::unique_ptr<LoopAccessInfo>> LoopAccessInfoMap;

  // The used analysis passes.
  ScalarEvolution &SE;
  AAResults &AA;
  DominatorTree &DT;
  LoopInfo &LI;
  TargetTransformInfo *TTI;
  const TargetLibraryInfo *TLI = nullptr;
  AssumptionCache *AC;

public:
  /// Construct a manager over the given analyses.
  /// @param SE ScalarEvolution used to build LoopAccessInfo.
  /// @param AA Alias analysis used to build LoopAccessInfo.
  /// @param DT Dominator tree used to build LoopAccessInfo.
  /// @param LI Loop info for the function being analyzed.
  /// @param TTI Optional target transform info.
  /// @param TLI Optional target library info.
  /// @param AC Optional assumption cache.
  LoopAccessInfoManager(ScalarEvolution &SE, AAResults &AA, DominatorTree &DT,
                        LoopInfo &LI, TargetTransformInfo *TTI,
                        const TargetLibraryInfo *TLI, AssumptionCache *AC)
      : SE(SE), AA(AA), DT(DT), LI(LI), TTI(TTI), TLI(TLI), AC(AC) {}

  /// Return cached or freshly computed LoopAccessInfo for \p L.
  /// @param L Loop whose access info is requested.
  /// @param AllowPartial When true, keep partial runtime checks on failure.
  /// @return The LoopAccessInfo for \p L.
  LLVM_ABI const LoopAccessInfo &getInfo(Loop &L, bool AllowPartial = false);

  /// Clear all cached LoopAccessInfo results.
  LLVM_ABI void clear();

  /// Invalidate cached results when analyses are not preserved.
  /// @param F Function whose analyses may have changed.
  /// @param PA Set of analyses preserved by the transformation.
  /// @param Inv Invalidator used to check dependent analyses.
  /// @return True if this manager result should be invalidated.
  LLVM_ABI bool invalidate(Function &F, const PreservedAnalyses &PA,
                           FunctionAnalysisManager::Invalidator &Inv);
};

/// This analysis provides dependence information for the memory
/// accesses of a loop.
///
/// It runs the analysis for a loop on demand.  This can be initiated by
/// querying the loop access info via AM.getResult<LoopAccessAnalysis>.
/// getResult return a LoopAccessInfo object.  See this class for the
/// specifics of what information is provided.
class LoopAccessAnalysis
    : public AnalysisInfoMixin<LoopAccessAnalysis> {
  friend AnalysisInfoMixin<LoopAccessAnalysis>;
  LLVM_ABI static AnalysisKey Key;

public:
  /// Result type providing per-loop LoopAccessInfo through a manager.
  using Result = LoopAccessInfoManager;

  /// Run the loop-access analysis over function \p F.
  /// @param F Function whose loops are analyzed on demand.
  /// @param AM Function analysis manager providing required analyses.
  /// @return A LoopAccessInfoManager for loops in \p F.
  LLVM_ABI Result run(Function &F, FunctionAnalysisManager &AM);
};

inline Instruction *MemoryDepChecker::Dependence::getSource(
    const MemoryDepChecker &DepChecker) const {
  return DepChecker.getMemoryInstructions()[Source];
}

inline Instruction *MemoryDepChecker::Dependence::getDestination(
    const MemoryDepChecker &DepChecker) const {
  return DepChecker.getMemoryInstructions()[Destination];
}

} // End llvm namespace

#endif
