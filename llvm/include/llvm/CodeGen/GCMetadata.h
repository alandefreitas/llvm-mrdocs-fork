//===- GCMetadata.h - Garbage collector metadata ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the GCFunctionInfo and GCModuleInfo classes, which are
// used as a communication channel from the target code generator to the target
// garbage collectors. This interface allows code generators and garbage
// collectors to be developed independently.
//
// The GCFunctionInfo class logs the data necessary to build a type accurate
// stack map. The code generator outputs:
//
//   - Safe points as specified by the GCStrategy's NeededSafePoints.
//   - Stack offsets for GC roots, as specified by calls to llvm.gcroot
//
// As a refinement, liveness analysis calculates the set of live roots at each
// safe point. Liveness analysis is not presently performed by the code
// generator, so all roots are assumed live.
//
// GCModuleInfo simply collects GCFunctionInfo instances for each Function as
// they are compiled. This accretion is necessary for collectors which must emit
// a stack map for the compilation unit as a whole. Therefore, GCFunctionInfo
// outlives the MachineFunction from which it is derived and must not refer to
// any code generator data structures.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GCMETADATA_H
#define LLVM_CODEGEN_GCMETADATA_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/GCStrategy.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/Compiler.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace llvm {

class Constant;
class Function;
class MCSymbol;

/// GCPoint - Metadata for a collector-safe point in machine code.
///
struct GCPoint {
  MCSymbol *Label;    ///< A label.
  /// Source location of the safe point, if available.
  DebugLoc Loc;

  /// Construct a safe-point record from a label and debug location.
  /// \param L Label identifying the safe point in machine code.
  /// \param DL Debug location associated with the safe point.
  GCPoint(MCSymbol *L, DebugLoc DL)
      : Label(L), Loc(std::move(DL)) {}
};

/// GCRoot - Metadata for a pointer to an object managed by the garbage
/// collector.
struct GCRoot {
  int Num;                  ///< Usually a frame index.
  int StackOffset = -1;     ///< Offset from the stack pointer.
  const Constant *Metadata; ///< Metadata straight from the call
                            ///< to llvm.gcroot.

  /// Construct a stack-root record.
  /// \param N Stack object ID (typically a frame index).
  /// \param MD Metadata constant from the llvm.gcroot intrinsic.
  GCRoot(int N, const Constant *MD) : Num(N), Metadata(MD) {}
};

/// Garbage collection metadata for a single function.  Currently, this
/// information only applies to GCStrategies which use GCRoot.
class GCFunctionInfo {
public:
  /// Iterator over safe points in this function.
  using iterator = std::vector<GCPoint>::iterator;
  /// Iterator over all GC roots in this function.
  using roots_iterator = std::vector<GCRoot>::iterator;
  /// Const iterator over roots treated as live at a safe point.
  using live_iterator = std::vector<GCRoot>::const_iterator;

private:
  const Function &F;
  GCStrategy &S;
  uint64_t FrameSize;
  std::vector<GCRoot> Roots;
  std::vector<GCPoint> SafePoints;

  // FIXME: Liveness. A 2D BitVector, perhaps?
  //
  //   BitVector Liveness;
  //
  //   bool islive(int point, int root) =
  //     Liveness[point * SafePoints.size() + root]
  //
  // The bit vector is the more compact representation where >3.2% of roots
  // are live per safe point (1.5% on 64-bit hosts).

public:
  /// Construct GC metadata for function \p F using strategy \p S.
  /// \param F Function this metadata describes.
  /// \param S GC strategy that owns collection policy for \p F.
  LLVM_ABI GCFunctionInfo(const Function &F, GCStrategy &S);
  /// Destroy this function's GC metadata.
  LLVM_ABI ~GCFunctionInfo();

  /// Handle invalidation explicitly.
  /// \param F Function whose analyses may be stale.
  /// \param PA Set of analyses preserved by the transform.
  /// \param Inv Invalidator for resolving analysis dependencies.
  /// \return True if this analysis result should be discarded.
  LLVM_ABI bool invalidate(Function &F, const PreservedAnalyses &PA,
                           FunctionAnalysisManager::Invalidator &Inv);

  /// getFunction - Return the function to which this metadata applies.
  /// \return The function associated with this GC metadata.
  const Function &getFunction() const { return F; }

  /// getStrategy - Return the GC strategy for the function.
  /// \return The GC strategy that owns collection policy for this function.
  GCStrategy &getStrategy() { return S; }

  /// Register a root that lives on the stack.
  ///
  /// \p Num is the stack object ID for the alloca (if the code generator is
  /// using MachineFrameInfo).
  /// \param Num Stack object ID for the alloca.
  /// \param Metadata Metadata constant from the llvm.gcroot intrinsic.
  void addStackRoot(int Num, const Constant *Metadata) {
    Roots.push_back(GCRoot(Num, Metadata));
  }

  /// removeStackRoot - Removes a root.
  /// \param position Iterator to the root to remove.
  /// \return Iterator following the removed root.
  roots_iterator removeStackRoot(roots_iterator position) {
    return Roots.erase(position);
  }

  /// addSafePoint - Notes the existence of a safe point. Num is the ID of the
  /// label just prior to the safe point (if the code generator is using
  /// MachineModuleInfo).
  /// \param Label Label identifying the safe point.
  /// \param DL Debug location of the safe point.
  void addSafePoint(MCSymbol *Label, const DebugLoc &DL) {
    SafePoints.emplace_back(Label, DL);
  }

  /// getFrameSize/setFrameSize - Records the function's frame size.
  /// \return The function's frame size in bytes.
  uint64_t getFrameSize() const { return FrameSize; }
  /// Record the function's frame size in bytes.
  /// \param S Frame size to store.
  void setFrameSize(uint64_t S) { FrameSize = S; }

  /// begin/end - Iterators for safe points.
  /// \return Iterator to the first safe point.
  iterator begin() { return SafePoints.begin(); }
  /// Return an iterator past the last safe point.
  /// \return Iterator past the last safe point.
  iterator end() { return SafePoints.end(); }
  /// Return the number of safe points in this function.
  /// \return The number of safe points.
  size_t size() const { return SafePoints.size(); }

  /// roots_begin/roots_end - Iterators for all roots in the function.
  /// \return Iterator to the first root in the function.
  roots_iterator roots_begin() { return Roots.begin(); }
  /// Return an iterator past the last root in the function.
  /// \return Iterator past the last root.
  roots_iterator roots_end() { return Roots.end(); }
  /// Return the number of roots in the function.
  /// \return The number of roots.
  size_t roots_size() const { return Roots.size(); }

  /// live_begin/live_end - Iterators for live roots at a given safe point.
  /// \param p Safe-point iterator whose live roots are requested.
  /// \return Iterator to the first live root at safe point \p p.
  live_iterator live_begin(const iterator &p) { return roots_begin(); }
  /// Return an iterator past the last live root at safe point \p p.
  /// \param p Safe-point iterator whose live roots are requested.
  /// \return Iterator past the last live root at safe point \p p.
  live_iterator live_end(const iterator &p) { return roots_end(); }
  /// Return the number of live roots at safe point \p p.
  /// \param p Safe-point iterator whose live roots are counted.
  /// \return The number of live roots at safe point \p p.
  size_t live_size(const iterator &p) const { return roots_size(); }
};

/// Map from GC strategy name to the corresponding GCStrategy instance.
class GCStrategyMap {
  using MapT =
      MapVector<StringRef, std::unique_ptr<GCStrategy>, StringMap<unsigned>>;
  MapT Strategies;

public:
  /// Construct an empty strategy map.
  GCStrategyMap() = default;
  /// Move-construct a strategy map, taking ownership of \p Other's strategies.
  /// \param Other Map to move from.
  GCStrategyMap(GCStrategyMap &&Other) = default;

  /// Handle invalidation explicitly.
  /// \param M Module whose analyses may be stale.
  /// \param PA Set of analyses preserved by the transform.
  /// \param Inv Invalidator for resolving analysis dependencies.
  /// \return True if this analysis result should be discarded.
  LLVM_ABI bool invalidate(Module &M, const PreservedAnalyses &PA,
                           ModuleAnalysisManager::Invalidator &Inv);

  /// Iterator over (name, strategy) entries.
  using iterator = MapT::iterator;
  /// Const iterator over (name, strategy) entries.
  using const_iterator = MapT::const_iterator;
  /// Reverse iterator over (name, strategy) entries.
  using reverse_iterator = MapT::reverse_iterator;
  /// Const reverse iterator over (name, strategy) entries.
  using const_reverse_iterator = MapT::const_reverse_iterator;

  /// Return an iterator to the first strategy entry.
  /// \return Iterator to the first strategy entry.
  iterator begin() { return Strategies.begin(); }
  /// Return a const iterator to the first strategy entry.
  /// \return Const iterator to the first strategy entry.
  const_iterator begin() const { return Strategies.begin(); }
  /// Return an iterator past the last strategy entry.
  /// \return Iterator past the last strategy entry.
  iterator end() { return Strategies.end(); }
  /// Return a const iterator past the last strategy entry.
  /// \return Const iterator past the last strategy entry.
  const_iterator end() const { return Strategies.end(); }

  /// Return a reverse iterator to the last strategy entry.
  /// \return Reverse iterator to the last strategy entry.
  reverse_iterator rbegin() { return Strategies.rbegin(); }
  /// Return a const reverse iterator to the last strategy entry.
  /// \return Const reverse iterator to the last strategy entry.
  const_reverse_iterator rbegin() const { return Strategies.rbegin(); }
  /// Return a reverse iterator past the first strategy entry.
  /// \return Reverse iterator past the first strategy entry.
  reverse_iterator rend() { return Strategies.rend(); }
  /// Return a const reverse iterator past the first strategy entry.
  /// \return Const reverse iterator past the first strategy entry.
  const_reverse_iterator rend() const { return Strategies.rend(); }

  /// Return true if no GC strategies are registered.
  /// \return True if the map contains no strategies.
  bool empty() const { return Strategies.empty(); }

  /// Return the GC strategy registered under \p GCName.
  /// \param GCName Name of the GC strategy to look up.
  /// \return The GC strategy registered under \p GCName.
  const GCStrategy &operator[](StringRef GCName) const {
    auto I = Strategies.find(GCName);
    assert(I != Strategies.end() && "Required strategy doesn't exist!");
    return *I->second;
  }

  /// Insert an empty strategy slot for \p GCName if none exists.
  /// \param GCName Name of the GC strategy to insert.
  /// \return A pair of an iterator to the entry and whether insertion occurred.
  std::pair<iterator, bool> try_emplace(StringRef GCName) {
    return Strategies.try_emplace(GCName);
  }

  /// Return true if a strategy named \p GCName is present.
  /// \param GCName Name of the GC strategy to test.
  /// \return True if a strategy named \p GCName is registered.
  bool contains(StringRef GCName) const { return Strategies.contains(GCName); }
};

/// An analysis pass which caches information about the entire Module.
/// Records a cache of the 'active' gc strategy objects for the current Module.
class CollectorMetadataAnalysis
    : public AnalysisInfoMixin<CollectorMetadataAnalysis> {
  friend struct AnalysisInfoMixin<CollectorMetadataAnalysis>;
  LLVM_ABI static AnalysisKey Key;

public:
  /// Cached map of active GC strategies for the module.
  using Result = GCStrategyMap;
  /// Run the analysis and build the module's GC strategy map.
  /// \param M Module to analyze.
  /// \param MAM Module analysis manager providing dependencies.
  /// \return The cached map of active GC strategies for \p M.
  LLVM_ABI Result run(Module &M, ModuleAnalysisManager &MAM);
};

/// Analysis pass that caches per-function GC root metadata.
///
/// Records the function level information used by GCRoots.
/// This pass depends on `CollectorMetadataAnalysis`.
class GCFunctionAnalysis : public AnalysisInfoMixin<GCFunctionAnalysis> {
  friend struct AnalysisInfoMixin<GCFunctionAnalysis>;
  LLVM_ABI static AnalysisKey Key;

public:
  /// Cached GC metadata for a single function.
  using Result = GCFunctionInfo;
  /// Run the analysis and build GC metadata for \p F.
  /// \param F Function to analyze.
  /// \param FAM Function analysis manager providing dependencies.
  /// \return The cached GC metadata for \p F.
  LLVM_ABI Result run(Function &F, FunctionAnalysisManager &FAM);
};

/// Pass that lowers GC read/write intrinsics and initializes roots.
///
/// LowerIntrinsics rewrites calls to the llvm.gcread or llvm.gcwrite
/// intrinsics, replacing them with simple loads and stores as directed by the
/// GCStrategy. It also performs automatic root initialization and custom
/// intrinsic lowering.
///
/// This pass requires `CollectorMetadataAnalysis`.
class GCLoweringPass : public RequiredPassInfoMixin<GCLoweringPass> {
public:
  /// Lower GC intrinsics in \p F according to its GC strategy.
  /// \param F Function whose GC intrinsics are rewritten.
  /// \param FAM Function analysis manager providing dependencies.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
};

/// Module analysis that caches per-function GC info and active strategies.
///
/// Records both the function level information used by GCRoots and a
/// cache of the 'active' gc strategy objects for the current Module.
class GCModuleInfo : public ImmutablePass {
  /// An owning list of all GCStrategies which have been created
  SmallVector<std::unique_ptr<GCStrategy>, 1> GCStrategyList;
  /// A helper map to speedup lookups into the above list
  StringMap<GCStrategy*> GCStrategyMap;

public:
  /// Lookup the GCStrategy object associated with the given gc name.
  /// Objects are owned internally; No caller should attempt to delete the
  /// returned objects.
  /// \param Name Name of the GC strategy to look up.
  /// \return The GC strategy for \p Name, created if it did not already exist.
  LLVM_ABI GCStrategy *getGCStrategy(const StringRef Name);

  /// List of per function info objects.  In theory, Each of these
  /// may be associated with a different GC.
  using FuncInfoVec = std::vector<std::unique_ptr<GCFunctionInfo>>;

  /// Return an iterator to the first per-function GC info.
  /// \return Iterator to the first per-function GC info.
  FuncInfoVec::iterator funcinfo_begin() { return Functions.begin(); }
  /// Return an iterator past the last per-function GC info.
  /// \return Iterator past the last per-function GC info.
  FuncInfoVec::iterator funcinfo_end() { return Functions.end(); }

private:
  /// Owning list of all GCFunctionInfos associated with this Module
  FuncInfoVec Functions;

  /// Non-owning map to bypass linear search when finding the GCFunctionInfo
  /// associated with a particular Function.
  using finfo_map_type = DenseMap<const Function *, GCFunctionInfo *>;
  finfo_map_type FInfoMap;

public:
  /// Const iterator over the GC strategies used by this module.
  using iterator = SmallVector<std::unique_ptr<GCStrategy>, 1>::const_iterator;

  /// Pass identification, replacement for typeid.
  LLVM_ABI static char ID;

  /// Construct an empty GC module info pass.
  LLVM_ABI GCModuleInfo();

  /// clear - Resets the pass. Any pass, which uses GCModuleInfo, should
  /// call it in doFinalization().
  ///
  LLVM_ABI void clear();

  /// begin/end - Iterators for used strategies.
  ///
  /// \return Const iterator to the first GC strategy used by this module.
  iterator begin() const { return GCStrategyList.begin(); }
  /// Return an iterator past the last GC strategy used by this module.
  /// \return Const iterator past the last GC strategy used by this module.
  iterator end() const { return GCStrategyList.end(); }

  /// get - Look up function metadata.  This is currently assumed
  /// have the side effect of initializing the associated GCStrategy.  That
  /// will soon change.
  /// \param F Function whose GC metadata is requested.
  /// \return The GC metadata for \p F, creating it if necessary.
  LLVM_ABI GCFunctionInfo &getFunctionInfo(const Function &F);
};

} // end namespace llvm

#endif // LLVM_CODEGEN_GCMETADATA_H
