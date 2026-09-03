//===- llvm/Analysis/AssumptionCache.h - Track @llvm.assume -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a pass that keeps track of @llvm.assume intrinsics in
// the functions of a module (allowing assumptions within any function to be
// found cheaply by other parts of the optimizer).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_ASSUMPTIONCACHE_H
#define LLVM_ANALYSIS_ASSUMPTIONCACHE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Pass.h"
#include "llvm/Support/Compiler.h"
#include <memory>

namespace llvm {

class AssumeInst;
struct OperandBundleUse;
class Function;
class raw_ostream;
class TargetTransformInfo;
class Value;

/// A cache of \@llvm.assume calls within a function.
///
/// This cache provides fast lookup of assumptions within a function by caching
/// them and amortizing the cost of scanning for them across all queries. Passes
/// that create new assumptions are required to call registerAssumption() to
/// register any new \@llvm.assume calls that they create. Deletions of
/// \@llvm.assume calls do not require special handling.
class AssumptionCache {
public:
  /// Sentinel Index values used by ResultElem.
  enum : unsigned {
    /// Index indicating the knowledge applies to the \@llvm.assume argument.
    ExprResultIdx = std::numeric_limits<unsigned>::max()
  };

  /// An assumption and the Index that relates it to an affected value.
  struct ResultElem {
    /// Weak handle to the \@llvm.assume call providing the knowledge.
    WeakVH Assume;

    /// contains either ExprResultIdx or the index of the operand bundle
    /// containing the knowledge.
    unsigned Index;
    /// Convert this result to the underlying assume value.
    /// @return The assume Value* held by this result, or null if expired.
    operator Value *() const { return Assume; }
  };

private:
  /// The function for which this cache is handling assumptions.
  ///
  /// We track this to lazily populate our assumptions.
  Function &F;

  TargetTransformInfo *TTI;

  /// Vector of weak value handles to calls of the \@llvm.assume
  /// intrinsic.
  SmallVector<WeakVH, 4> AssumeHandles;

  class LLVM_ABI AffectedValueCallbackVH final : public CallbackVH {
    AssumptionCache *AC;

    void deleted() override;
    void allUsesReplacedWith(Value *) override;

  public:
    using DMI = DenseMapInfo<Value *>;

    AffectedValueCallbackVH(Value *V, AssumptionCache *AC = nullptr)
        : CallbackVH(V), AC(AC) {}
  };

  friend AffectedValueCallbackVH;

  /// A map of values about which an assumption might be providing
  /// information to the relevant set of assumptions.
  using AffectedValuesMap =
      DenseMap<AffectedValueCallbackVH, SmallVector<ResultElem, 1>,
               AffectedValueCallbackVH::DMI>;
  AffectedValuesMap AffectedValues;

  /// Get the vector of assumptions which affect a value from the cache.
  SmallVector<ResultElem, 1> &getOrInsertAffectedValues(Value *V);

  /// Move affected values in the cache for OV to be affected values for NV.
  void transferAffectedValuesInCache(Value *OV, Value *NV);

  /// Remove the entries in the affected-values cache that point to \p CI.
  void removeAffectedValues(AssumeInst *CI);

  /// Flag tracking whether we have scanned the function yet.
  ///
  /// We want to be as lazy about this as possible, and so we scan the function
  /// at the last moment.
  bool Scanned = false;

  /// Scan the function for assumptions and add them to the cache.
  LLVM_ABI void scanFunction();

public:
  /// Construct an AssumptionCache from a function by scanning all of
  /// its instructions.
  /// @param F Function whose \@llvm.assume calls are cached.
  /// @param TTI Optional target transform info used when scanning assumes.
  AssumptionCache(Function &F, TargetTransformInfo *TTI = nullptr)
      : F(F), TTI(TTI) {}

  /// This cache is designed to be self-updating and so it should never be
  /// invalidated.
  /// @param F Function being invalidated (unused).
  /// @param PA Set of preserved analyses (unused).
  /// @param Inv Invalidator for dependent analyses (unused).
  /// @return Always false; the cache is never invalidated.
  bool invalidate(Function &F, const PreservedAnalyses &PA,
                  FunctionAnalysisManager::Invalidator &Inv) {
    return false;
  }

  /// Add an \@llvm.assume intrinsic to this function's cache.
  ///
  /// The call passed in must be an instruction within this function and must
  /// not already be in the cache.
  /// @param CI The \@llvm.assume to register.
  LLVM_ABI void registerAssumption(AssumeInst *CI);

  /// Remove an \@llvm.assume intrinsic from this function's cache if it has
  /// been added to the cache earlier.
  /// @param CI The \@llvm.assume to unregister.
  LLVM_ABI void unregisterAssumption(AssumeInst *CI);

  /// Replace the assumption referenced by \p Handle (must be a valid handle for
  /// a registered assumption) with \p New.
  /// @param Handle Weak handle to the registered assumption to replace.
  /// @param New Replacement \@llvm.assume intrinsic.
  LLVM_ABI void replaceAssumption(WeakVH &Handle, AssumeInst *New);

  /// Update the cache of values being affected by this assumption (i.e.
  /// the values about which this assumption provides information).
  /// @param CI The \@llvm.assume whose affected values should be refreshed.
  LLVM_ABI void updateAffectedValues(AssumeInst *CI);

  /// Clear the cache of \@llvm.assume intrinsics for a function.
  ///
  /// It will be re-scanned the next time it is requested.
  void clear() {
    AssumeHandles.clear();
    AffectedValues.clear();
    Scanned = false;
  }

  /// Access the list of assumption handles currently tracked for this
  /// function.
  ///
  /// Note that these produce weak handles that may be null. The caller must
  /// handle that case.
  /// FIXME: We should replace this with pointee_iterator<filter_iterator<...>>
  /// when we can write that to filter out the null values. Then caller code
  /// will become simpler.
  /// @return Mutable view of weak handles to tracked \@llvm.assume calls.
  MutableArrayRef<WeakVH> assumptions() {
    if (!Scanned)
      scanFunction();
    return AssumeHandles;
  }

  /// Access the list of assumptions which affect this value.
  ///
  /// No more than -max-assumes-per-value of them are cached.
  /// @param V Value whose affecting assumptions are requested.
  /// @return Mutable view of ResultElem entries that affect \p V.
  MutableArrayRef<ResultElem> assumptionsFor(const Value *V) {
    if (!Scanned)
      scanFunction();

    auto AVI = AffectedValues.find_as(const_cast<Value *>(V));
    if (AVI == AffectedValues.end())
      return MutableArrayRef<ResultElem>();

    return AVI->second;
  }

  /// Determine which values are affected by this assume operand bundle.
  /// @param Bundle Operand bundle from an \@llvm.assume.
  /// @param InsertAffected Callback invoked for each affected value.
  LLVM_ABI static void
  findValuesAffectedByOperandBundle(OperandBundleUse Bundle,
                                    function_ref<void(Value *)> InsertAffected);
};

/// A function analysis which provides an \c AssumptionCache.
///
/// This analysis is intended for use with the new pass manager and will vend
/// assumption caches for a given function.
class AssumptionAnalysis : public AnalysisInfoMixin<AssumptionAnalysis> {
  friend AnalysisInfoMixin<AssumptionAnalysis>;

  LLVM_ABI static AnalysisKey Key;

public:
  /// The analysis result type; an AssumptionCache for a function.
  using Result = AssumptionCache;

  /// Run the assumption analysis on function \p F.
  /// @param F Function to analyze.
  /// @param FAM Function analysis manager providing dependencies.
  /// @return An AssumptionCache for \p F.
  LLVM_ABI AssumptionCache run(Function &F, FunctionAnalysisManager &FAM);
};

/// Printer pass for the \c AssumptionAnalysis results.
class AssumptionPrinterPass
    : public RequiredPassInfoMixin<AssumptionPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes assumption info to \p OS.
  /// @param OS Output stream for the printed assumptions.
  explicit AssumptionPrinterPass(raw_ostream &OS) : OS(OS) {}

  /// Print assumption cache results for \p F.
  /// @param F Function whose assumptions are printed.
  /// @param AM Function analysis manager providing AssumptionAnalysis.
  /// @return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// An immutable pass that tracks lazily created \c AssumptionCache
/// objects.
///
/// This is essentially a workaround for the legacy pass manager's weaknesses
/// which associates each assumption cache with Function and clears it if the
/// function is deleted. The nature of the AssumptionCache is that it is not
/// invalidated by any changes to the function body and so this is sufficient
/// to be conservatively correct.
class LLVM_ABI AssumptionCacheTracker : public ImmutablePass {
  /// A callback value handle applied to function objects, which we use to
  /// delete our cache of intrinsics for a function when it is deleted.
  class LLVM_ABI FunctionCallbackVH final : public CallbackVH {
    AssumptionCacheTracker *ACT;

    void deleted() override;

  public:
    using DMI = DenseMapInfo<Value *>;

    FunctionCallbackVH(Value *V, AssumptionCacheTracker *ACT = nullptr)
        : CallbackVH(V), ACT(ACT) {}
  };

  friend FunctionCallbackVH;

  using FunctionCallsMap =
      DenseMap<FunctionCallbackVH, std::unique_ptr<AssumptionCache>,
               FunctionCallbackVH::DMI>;

  FunctionCallsMap AssumptionCaches;

public:
  /// Get the cached assumptions for a function.
  ///
  /// If no assumptions are cached, this will scan the function. Otherwise, the
  /// existing cache will be returned.
  /// @param F Function whose assumption cache is requested.
  /// @return The AssumptionCache for \p F, creating it if needed.
  AssumptionCache &getAssumptionCache(Function &F);

  /// Return the cached assumptions for a function if it has already been
  /// scanned. Otherwise return nullptr.
  /// @param F Function whose assumption cache is looked up.
  /// @return The existing AssumptionCache, or nullptr if not yet scanned.
  AssumptionCache *lookupAssumptionCache(Function &F);

  /// Construct an AssumptionCacheTracker pass.
  AssumptionCacheTracker();
  /// Destroy this AssumptionCacheTracker and its cached data.
  ~AssumptionCacheTracker() override;

  /// Release all cached AssumptionCache objects.
  void releaseMemory() override {
    verifyAnalysis();
    AssumptionCaches.shrink_and_clear();
  }

  /// Verify the integrity of cached assumption data.
  void verifyAnalysis() const override;

  /// Finalize the pass for module \p M by verifying analysis state.
  /// @param M Module being finalized (unused beyond verification).
  /// @return Always false; this pass does not modify the module.
  bool doFinalization(Module &M) override {
    verifyAnalysis();
    return false;
  }

  /// Pass identification, replacement for typeid.
  static char ID;
};

/// Allow clients to treat ResultElem as a Value* when using casting operators.
template<> struct simplify_type<AssumptionCache::ResultElem> {
  /// The simplified type; a Value pointer to the assume.
  using SimpleType = Value *;

  /// Return the assume value held by \p Val.
  /// @param Val Result element to simplify.
  /// @return The assume Value* stored in \p Val.
  static SimpleType getSimplifiedValue(AssumptionCache::ResultElem &Val) {
    return Val;
  }
};
/// Allow clients to treat const ResultElem as a Value* when using casting operators.
template<> struct simplify_type<const AssumptionCache::ResultElem> {
  /// The simplified type; a Value pointer to the assume.
  using SimpleType = /*const*/ Value *;

  /// Return the assume value held by \p Val.
  /// @param Val Result element to simplify.
  /// @return The assume Value* stored in \p Val.
  static SimpleType getSimplifiedValue(const AssumptionCache::ResultElem &Val) {
    return Val;
  }
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_ASSUMPTIONCACHE_H
